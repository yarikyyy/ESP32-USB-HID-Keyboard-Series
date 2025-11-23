
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include <string.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include <sys/socket.h>

#include <esp_http_server.h>

#define APP_BUTTON (GPIO_NUM_0)



#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

static const char *TAG = "USB HID";
int WIFI_INIT = 0;                       /* 0 -> fresh init, 1 -> AP mode, 2 -> STA mode */
int WIFI_CONNECT = 0;
const int WIFI_CONNECTED_BIT = BIT0;     /* connected bit for wifi station mode */
int clients_num = 0;
int clients_rssi = 0;

static const char* WIFITAG = "WIFI_SETUP";   
static EventGroupHandle_t wifi_event_group;  // Event group for wifi connection

wifi_sta_list_t clients;
esp_netif_t *esp_netif;
wifi_ap_record_t ap_0;
esp_netif_ip_info_t info; 

char ap_ip[20];
uint ap_rssi = 0;

int usb_hid_mounted = 0;

struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

/* ---------- Embedded web assets ---------- */
static const char index_html[] = R"rawliteral(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>ESP32 Web HID</title>
  <style>
    body { font-family: Arial, sans-serif; padding: 16px; }
    textarea { width: 100%; height: 120px; font-size: 16px; }
    .row { display:flex; gap:8px; margin-top:8px; flex-wrap:wrap; }
    button { padding:10px 14px; font-size:16px; cursor:pointer; }
    #log { white-space:pre-wrap; border:1px solid #ddd; padding:8px; height:140px; overflow:auto; margin-top:12px; }
  </style>
</head>
<body>
  <h2>ESP32 Web HID</h2>
  <p>Type in the textbox below and click <strong>Send</strong>, or click demo buttons to send keystrokes to the USB host (ESP32 acts as a keyboard).</p>

  <textarea id="txt" placeholder="Type here..."></textarea>
  <div class="row">
    <button onclick="sendText()">Send</button>
    <button onclick="sendKey('ENTER')">Enter</button>
    <button onclick="sendKey('BACKSPACE')">Backspace</button>
    <button onclick="sendKey('ARROW_UP')">Up</button>
    <button onclick="sendKey('ARROW_DOWN')">Down</button>
    <button onclick="sendKey('ARROW_LEFT')">Left</button>
    <button onclick="sendKey('ARROW_RIGHT')">Right</button>
    <button onclick="sendCombo(['GUI','r'])">Win+R</button>
    <button onclick="sendString('cmd')">Type 'cmd'</button>
  </div>

  <h3>Log</h3>
  <div id="log"></div>

<script src="/app.js"></script>
</body>
</html>
)rawliteral";

static const char app_js[] = R"rawliteral(
(function(){
  const wsProto = (location.protocol === 'https:') ? 'wss' : 'ws';
  const wsUrl = wsProto + '://' + location.host + '/ws';
  let ws;

  function log(msg) {
    const el = document.getElementById('log');
    el.textContent += msg + '\n';
    el.scrollTop = el.scrollHeight;
  }

  function connect() {
    ws = new WebSocket(wsUrl);
    ws.onopen = () => log('WS: connected');
    ws.onclose = () => { log('WS: closed, reconnect in 2s'); setTimeout(connect, 2000); };
    ws.onmessage = (ev) => log('RX: ' + ev.data);
    ws.onerror = (e) => log('WS: error');
  }
  connect();

  window.sendText = function() {
    const t = document.getElementById('txt').value;
    if (!t) return;
    const msg = { type: 'string', value: t };
    ws.send(JSON.stringify(msg));
    log('TX: ' + JSON.stringify(msg));
  }

  window.sendString = function(s) {
    const msg = { type: 'string', value: s };
    ws.send(JSON.stringify(msg));
    log('TX: ' + JSON.stringify(msg));
  }

  window.sendKey = function(k) {
    const msg = { type: 'key', value: k };
    ws.send(JSON.stringify(msg));
    log('TX: ' + JSON.stringify(msg));
  }

  window.sendCombo = function(arr) {
    const msg = { type: 'combo', value: arr };
    ws.send(JSON.stringify(msg));
    log('TX: ' + JSON.stringify(msg));
  }
})();
)rawliteral";


/** 
 * @brief Struct to map ASCII characters to HID keycodes
 * 
*/
typedef struct {
    int modifier;
    int keycode;
} KeyCodeMapping;

/**
 * @brief Conversion table from ASCII to HID keycodes
 */
KeyCodeMapping conv_table[] = {HID_ASCII_TO_KEYCODE};

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
   
};

/**
 * @brief String descriptor
 */
const char* hid_string_descriptor[5] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "TinyUSB",             // 1: Manufacturer
    "TinyUSB Device",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "Example HID interface",  // 4: HID
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};



/**
 * @brief Invoked when received GET HID REPORT DESCRIPTOR request
 */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

/**
 * @brief Invoked when received GET_REPORT control request
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;

    return 0;
}

/**
 * @brief Invoked when received SET_REPORT control request or
 *        received data on OUT endpoint ( Report ID = 0, Type = 0 )
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
}

/**
 * @brief Convert ASCII character to HID keycode
 * @param c ASCII character
 * @return HID keycode
 */
uint8_t char_to_hid_keycode(unsigned char c, uint8_t *modifier) {
    uint8_t mod = 0;
    uint8_t keycode = 0;
    if (c < sizeof(conv_table) / sizeof(conv_table[0])) {
        mod = conv_table[(uint8_t)c].modifier;
        keycode = conv_table[c].keycode;
    } else {
        mod = 0;
        keycode = 0; // Return 0 for unsupported characters
    }
    if (modifier != NULL) {
        *modifier = (mod != 0) ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
        
    }
    return keycode;
}

/* parse a simple token like "ENTER", "SPACE", "R", "A" etc. returns keycode and modifier via pointers */
static void parse_token_to_hid(const char *token, uint8_t *modifier_out, uint8_t *keycode_out)
{
    uint8_t mod = 0;
    uint8_t key = 0;
    if (strlen(token) == 1) {
        key = char_to_hid_keycode(token[0], &mod);
    } else if (strcasecmp(token, "ENTER") == 0) {
        key = HID_KEY_ENTER;
    } else if (strcasecmp(token, "SPACE") == 0) {
        key = HID_KEY_SPACE;
    } else if (strcasecmp(token, "TAB") == 0) {
        key = HID_KEY_TAB;
    } else if (strcasecmp(token, "BACKSPACE") == 0) {
        key = HID_KEY_BACKSPACE;
    } else if (strcasecmp(token, "ARROW_UP") == 0 || strcasecmp(token, "UP") == 0) {
        key = HID_KEY_ARROW_UP;
    } else if (strcasecmp(token, "ARROW_DOWN") == 0 || strcasecmp(token, "DOWN") == 0) {
        key = HID_KEY_ARROW_DOWN;
    } else if (strcasecmp(token, "ARROW_LEFT") == 0 || strcasecmp(token, "LEFT") == 0) {
        key = HID_KEY_ARROW_LEFT;
    } else if (strcasecmp(token, "ARROW_RIGHT") == 0 || strcasecmp(token, "RIGHT") == 0) {
        key = HID_KEY_ARROW_RIGHT;
    } else if (strcasecmp(token, "R") == 0) {
        key = HID_KEY_R;
    } else {
        /* leave key 0 */
    }
    if (modifier_out) *modifier_out = mod;
    if (keycode_out) *keycode_out = key;
}


/**
 * @brief Send a single key press and release
 * @param keycode HID keycode to send
 */
void send_key(uint8_t keycode) {
    ESP_LOGI(TAG, "Sending Keyboard report for keycode: 0x%02X", keycode);
    uint8_t keycodes[6] = {keycode}; // Prepare an array for keycodes
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodes);
    vTaskDelay(pdMS_TO_TICKS(25)); // Delay to simulate key press
    // Release the key
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(25)); // Short delay before next key

}

/**
 * @brief Send a key press with modifier and release
 * @param modifier Modifier key (e.g., KEYBOARD_MODIFIER_LEFTCTRL)
 * @param keycode HID keycode to send
 */
void send_key_with_modifier(uint8_t modifier, const char keycode) {
    ESP_LOGI(TAG, "Sending Keyboard report for keycode: %c with modifier: 0x%02X", keycode, modifier);
    uint8_t keycodes[6] = {0}; // Prepare an array for keycodes
    
    keycodes[0] = char_to_hid_keycode(keycode, NULL);
    if (keycodes[0] != 0) { // Only send valid keycodes
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycodes);
        vTaskDelay(pdMS_TO_TICKS(10)); // Delay to simulate key press
        // Release the key
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10)); // Short delay before next key
    }
    else {
        ESP_LOGW(TAG, "Unsupported character for key with modifier: %c", keycode);
    }
}

/**
 * @brief Send a string as individual key presses
 * @param str String to send
 */
void send_string(const char* str) {
    ESP_LOGI(TAG, "Sending Keyboard report for string: %s", str);
    uint8_t keycodes[6] = {0}; // Prepare an array for keycodes
    uint8_t modifier = 0;
    for (size_t i = 0; str[i] != '\0' && i < strlen(str); i++) {
        keycodes[0] = char_to_hid_keycode(str[i], &modifier);
        
       
        if (keycodes[0] != 0) { // Only send valid keycodes

            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycodes);
            vTaskDelay(pdMS_TO_TICKS(10)); // Delay to simulate key press
            // Release the key
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(10)); // Short delay before next key
        }
        else {
            ESP_LOGW(TAG, "Unsupported character: %c", str[i]);
        }
    }
}

/**
 * @brief WiFi event handler
 * @param arg Event handler argument
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data Event data
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)   // Wifi event handler
{
    
    if (event_base == WIFI_EVENT) {
        switch (event_id)
        {
            case WIFI_EVENT_AP_STACONNECTED:
                if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) 
                {
                    clients_num = clients.num;
                    clients_rssi = clients.sta[0].rssi;
                }
                break;

            case WIFI_EVENT_AP_STADISCONNECTED:
                if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) 
                {
                    clients_num = clients.num;
                }
                break;

            case WIFI_EVENT_STA_START:
                ESP_LOGI(WIFITAG,"- Wifi STA start event\n\n");
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(WIFITAG,"Disconnected from Router retry connection\r\n");
                WIFI_CONNECT = 0;
                esp_wifi_connect();
                break;

            default:
                break;
        }
    }

    if (event_base == IP_EVENT ) {
        switch (event_id)
        {
            case IP_EVENT_STA_GOT_IP:
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
                ESP_LOGI(WIFITAG,"Connected to Router and obtained IP\r\n");
                ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif, &info));
                ip4addr_ntoa_r((ip4_addr_t*)&info.ip,ap_ip,sizeof(ap_ip));
                ESP_LOGI(WIFITAG,"STA IP: %s \n", ap_ip);
                esp_wifi_sta_get_ap_info(&ap_0);
                ap_rssi = ap_0.rssi;

                ESP_LOGI(WIFITAG,"Connected AP RSSI = %d\n", ap_rssi);
                WIFI_CONNECT = 1;
                break;
        }
    }
    
}
/**
 * @brief Initialize WiFi
 */
void wifi_initialise()
{ 
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    // connect to the wifi network
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
}

/**
 * @brief Start WiFi in Access Point mode
 * @param ssid SSID of the Access Point
 * @param pass Password of the Access Point
 * @param ip_addr Static IP address
 * @param gtw_addr Gateway address
 * @param netmask_addr Subnet mask address
 */
 void WIFI_start_AP_Mode(char * ssid, char * pass, char * ip_addr, char* gtw_addr, char * netmask_addr)
 {
    if (WIFI_INIT == 0)
    {	
        wifi_initialise(); 
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif = esp_netif_create_default_wifi_ap();
    }
    else    
    {                                                                   
        ESP_LOGI(WIFITAG,"WIFI is stopping to reinitialise to WIFI AP\r\n");
        ESP_ERROR_CHECK(esp_wifi_stop()); 
    }	
    
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(esp_netif));          // To stop the DHCP server

    esp_netif_ip_info_t info;                                  // To assign a static IP to the network interface
    memset(&info, 0, sizeof(info));
    inet_pton(AF_INET, ip_addr, &info.ip);                     // Custom IP address
    inet_pton(AF_INET, gtw_addr, &info.gw);                    // Custom gateway address
    inet_pton(AF_INET, netmask_addr, &info.netmask);           // Subnet address

    ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif, &info));  // To set adapter with custom settings	 
    ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif));         // To start the DHCP server with the set IP

    if (WIFI_INIT == 0)
    {		
        wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &event_handler,
                                                            NULL,
                                                            NULL));
        ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));          
    } 
        
    wifi_config_t ap_config = {          // configure the wifi connection and start the interface
        .ap = {
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strcpy((char *)ap_config.sta.ssid, (const char *)ssid);
    strcpy((char *)ap_config.sta.password, (const char *)pass);

    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));	
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(WIFITAG,"Starting WIFI AP\n");
    WIFI_INIT = 1; 
 }

/**
 * @brief HTTP GET handler for index page
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html, strlen(index_html));
    return ESP_OK;
}

/**
 * @brief HTTP GET handler for app.js
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t appjs_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, app_js, strlen(app_js));
    return ESP_OK;
}

/**
 * @brief Send a text message over WebSocket
 * @param req HTTP request
 * @param msg Message to send
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t ws_send_text(httpd_req_t *req, const char *msg)
{
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    ws_pkt.payload = (uint8_t *)msg;
    ws_pkt.len = strlen(msg);
    return httpd_ws_send_frame(req, &ws_pkt);
}

/**
 * @brief WebSocket handler
 * @param req HTTP request
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new WS client connected");
        return ESP_OK;
    }

    /* Receive frame length first (non-blocking) */
    httpd_ws_frame_t ws_pkt = {
        .final = true,
        .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0
    };

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Websocket get frame len failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Allocate buffer (len + 1 for null-terminator) */
    size_t buf_len = ws_pkt.len + 1;
    uint8_t *buf = (uint8_t *)malloc(buf_len);
    if (!buf) {
        ESP_LOGE(TAG, "Out of memory for WS frame (%d bytes)", (int)buf_len);
        return ESP_ERR_NO_MEM;
    }

    /* Read actual payload */
    ws_pkt.payload = buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, buf_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Websocket receive failed: %s", esp_err_to_name(ret));
        free(buf);
        return ret;
    }
    /* Null-terminate for safe string handling */
    buf[ws_pkt.len] = '\0';

    ESP_LOGI(TAG, "WS RX: %s", (char*)buf);

    /* Simple JSON-ish parsing (no dependencies) */
    char *s = (char *)buf;

    /* Check type:key */
    if (strstr(s, "\"type\":\"key\"") || strstr(s, "'type':'key'")) {
        char *v = strstr(s, "\"value\":\"");
        if (!v) v = strstr(s, "'value':'");
        if (v) {
            v += strlen("\"value\":\"");
            v[strlen(v)-2] = '\0'; //to remove " and } at the end of the string
          
           
            uint8_t mod = 0;
            uint8_t key = 0;
            parse_token_to_hid(v, &mod, &key);
            if (key){
                
                send_key(key);
            } 
            if(usb_hid_mounted)
            {
                ws_send_text(req, "OK");
            }
            else
            {
                ws_send_text(req, "USB HID is not connected");
            }
            
        } else {
            ws_send_text(req, "ERR_NO_VALUE");
        }
    }
    /* type:string */
    else if (strstr(s, "\"type\":\"string\"") || strstr(s, "'type':'string'")) {
        char *v = strstr(s, "\"value\":\"");
        if (!v) v = strstr(s, "'value':'");
        if (v) {
            v += strlen("\"value\":\"");
            /* naive parsing: find next quote */
            char *end = strchr(v, '"');
            if (!end) end = strchr(v, '\'');
            if (end) *end = '\0';
            // send_string_as_hid(v);
            send_string(v);
           
           if(usb_hid_mounted)
            {
                ws_send_text(req, "OK");
            }
            else
            {
                ws_send_text(req, "USB HID is not connected");
            }
        } else {
            ws_send_text(req, "ERR_NO_VALUE");
        }
    }
    /* type:combo, value is an array like ["GUI","r"] or ["CTRL","SHIFT","A"] */
    else if (strstr(s, "\"type\":\"combo\"") || strstr(s, "'type':'combo'")) {
        /* Very simple tokenizer: extract tokens between double quotes */
        const char *p = s;
        const char *tokens[8];
        int tcount = 0;
        while ((p = strchr(p, '\"')) && tcount < 8) {
            p++;
            const char *q = strchr(p, '\"');
            if (!q) break;
            size_t len = q - p;
            char *tok = malloc(len + 1);
            if (!tok) break;
            memcpy(tok, p, len);
            tok[len] = '\0';
            tokens[tcount++] = tok;
            p = q + 1;
        }
      
        if (tcount > 0) {
            /* Find payload tokens: skip "type", "combo", "value" etc. */
            const char *payload[6];
            int pcnt = 0;
            for (int i = 0; i < tcount; i++) {
                if (strcmp(tokens[i], "type") == 0 || strcmp(tokens[i], "combo") == 0 || strcmp(tokens[i], "value") == 0) {
                    continue;
                }
                if (pcnt < 6) {
                    
                    payload[pcnt++] = tokens[i];
                }
            }
            
            /* If payload_count >= 1, interpret last token as main key */
            if (pcnt >= 1) {
                const char *last = payload[pcnt - 1];
                uint8_t modifier = 0;
                uint8_t key = last[0];
                /* token parsing: first tokens (except last) are modifiers */
                for (int i = 0; i < pcnt - 1; i++) {
                   
                    if (strcasecmp(payload[i], "CTRL") == 0) 
                    {
                        
                        modifier |= KEYBOARD_MODIFIER_LEFTCTRL;
                    }
                    else if (strcasecmp(payload[i], "SHIFT") == 0) {
                        
                        modifier |= KEYBOARD_MODIFIER_LEFTSHIFT;
                    }
                    else if (strcasecmp(payload[i], "ALT") == 0) {
                       
                        modifier |= KEYBOARD_MODIFIER_LEFTALT;
                    }
                    else if (strcasecmp(payload[i], "GUI") == 0) {
                        
                        modifier |= KEYBOARD_MODIFIER_LEFTGUI;
                    }
                }
                
                if (key)
                {
                    
                    send_key_with_modifier(modifier,key);
                   
                } 
            }
            /* free allocated token strings */
            for (int i = 0; i < tcount; i++) free((void*)tokens[i]);
        }
        if(usb_hid_mounted)
        {
            ws_send_text(req, "OK");
        }
        else
        {
            ws_send_text(req, "USB HID is not connected");
        }
    }
    else {
        ws_send_text(req, "UNKNOWN");
    }

    free(buf);
    return ESP_OK;
}


static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};

httpd_uri_t index_req = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
        .user_ctx = NULL
    };

httpd_uri_t appjs = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = appjs_get_handler,
        .user_ctx = NULL
    };
/**
 * @brief Start the web server
 * @return HTTP server handle
 */
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ws);
        httpd_register_uri_handler(server, &index_req);
        httpd_register_uri_handler(server, &appjs);
        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}


void app_main(void)
{
    
    // Initialize button that will trigger HID reports
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true,
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
        .configuration_descriptor = hid_configuration_descriptor,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    WIFI_start_AP_Mode("ESP HID", "12345678", "192.168.10.10", "192.168.10.10", "255.255.255.0");
    while( clients_num < 1)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
    /* Start the server for the first time */
    start_webserver();
    ESP_LOGI(TAG, "Starting webserver done!");

    while (1) {
        if (tud_mounted() && tud_connected()) {
            static bool send_hid_data = true;
            if (send_hid_data) {
                usb_hid_mounted = 1;
                ESP_LOGI(TAG, "USB HID is mounted");
            }
            send_hid_data = !gpio_get_level(APP_BUTTON);
        }
        else
        {
            usb_hid_mounted = 0;
            ESP_LOGI(TAG, "USB HID is unmounted");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}