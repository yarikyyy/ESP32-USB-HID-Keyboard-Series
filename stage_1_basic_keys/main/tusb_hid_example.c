
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"
#include <string.h>

#define APP_BUTTON (GPIO_NUM_0)
static const char *TAG = "USB HID Example";


#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

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
uint8_t char_to_hid_keycode(unsigned char c) {
    if (c < sizeof(conv_table) / sizeof(conv_table[0])) {
        return conv_table[c].keycode;
    } else {
        return 0; // Return 0 for unsupported characters
    }
}

/**
 * @brief Send a single key press and release
 * @param keycode HID keycode to send
 */
void send_key(uint8_t keycode) {
    uint8_t keycodes[6] = {keycode}; // Prepare an array for keycodes
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodes);
    vTaskDelay(pdMS_TO_TICKS(10)); // Delay to simulate key press
    // Release the key
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay before next key

}

/**
 * @brief Send a key press with modifier and release
 * @param modifier Modifier key (e.g., KEYBOARD_MODIFIER_LEFTCTRL)
 * @param keycode HID keycode to send
 */
void send_key_with_modifier(uint8_t modifier, const char keycode) {
    uint8_t keycodes[6] = {0}; // Prepare an array for keycodes
    keycodes[0] = char_to_hid_keycode(keycode);
    if (keycodes[0] != 0) {
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
     
    for (size_t i = 0; str[i] != '\0' && i < strlen(str); i++) {
        keycodes[0] = char_to_hid_keycode(str[i]);
       
        if (keycodes[0] != 0) { // Only send valid keycodes
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycodes);
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
 * @brief Send a series of HID reports: a string, a key with modifier, and a single key
 */
static void send_hid_report(void)
{

    const char* message = "abcdefg123456789"; // Message to type
    send_string(message); // Send string as HID key presses
    send_key_with_modifier(KEYBOARD_MODIFIER_LEFTCTRL, 'a'); // Send 'a' with Ctrl for 'Select All'
    send_key(HID_KEY_DELETE); // Send 'Delete'
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

    while (1) {
        if (tud_mounted()) {
            static bool send_hid_data = true;
            if (send_hid_data) {
                send_hid_report();
            }
            send_hid_data = !gpio_get_level(APP_BUTTON);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}