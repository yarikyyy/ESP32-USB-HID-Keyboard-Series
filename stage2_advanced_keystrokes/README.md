# ESP32-S3 USB HID Keyboard with Modifier Support

This stage builds on [Stage 1 — ESP32-S3 USB HID Keyboard Basic Keystroke](../stage_1_basic_keys/README.md) and demonstrates how to use the ESP32-S3 as a USB HID keyboard capable of fully automated tasks, including modifier keys such as Shift, Ctrl, and GUI (Windows key).

This example uses a modified version of the official ESP-IDF HID example, extended to support automatic ASCII-to-HID conversion, uppercase handling, modifier keys, and multi-step keystroke workflows.

This entire example is purely for educational purposes only.
It demonstrates how HID works — do not use this for malicious or unauthorized actions.

---

## Hardware Setup

No external wiring needed.

The example uses:
- GPIO 0 (BOOT button) → to trigger the HID sequence
- USB port → used as USB device HID keyboard

---

## Software Requirements

- **ESP-IDF v5.1 or later**
- **Serial terminal** (`idf.py monitor` or VS Code IDF Monitor)

---

## Project Structure

```bash
stage2_advanced_keystrokes/
│── main/
│   ├── tusb_hid_example.c   # HID keyboard implementation
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│── CMakeLists.txt
│── sdkconfig
│── sdkconfig.defaults
│── README.md
```
---

## Build & Flash

You can run this example **either using the ESP-IDF command line**  
or **directly through VS Code’s ESP-IDF Extension**.

### Option 1 — Using ESP-IDF Terminal

``` bash
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

### Option 2 — Using VS Code

1. Open this project folder in VS Code.
2. Ensure the Espressif IDF extension is installed and configured.
3. In the VS Code status bar, select your target:
    - Target: `esp32s3`
    - Port: your board’s COM port
4. Click the “Build, Flash and Monitor” button
5. The program output will appear in the VS Code serial monitor.

---

## What the Code Does

In this stage, pressing the BOOT button on the ESP32-S3 triggers the following automated sequence on your PC:
1. Press `Win` + `R`
2. Type `cmd`
3. Press `Enter`
4. Type:
    ```bash
    echo Hello World ESP32
    ```
5. Press `Enter`

This demonstrates how ESP32-S3 can behave as a programmable USB keystroke automation device.

### 1. ASCII → HID conversion with automatic Shift
The lookup table from ESP-IDF (`HID_ASCII_TO_KEYCODE`) maps ASCII characters to a keycode and a modifier (like Shift).

```c
uint8_t char_to_hid_keycode(unsigned char c, uint8_t *modifier) {
    uint8_t mod = 0;
    uint8_t key = 0;

    if (c < sizeof(conv_table) / sizeof(conv_table[0])) {
        mod = conv_table[(uint8_t)c].modifier;
        key = conv_table[c].keycode;
    }

    if (modifier != NULL) {
        *modifier = (mod != 0) ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
    }

    return key;
}
```


### 2. Sending a key with modifier

```c
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
```


### 3. Sending a string


```c
void send_string(const char* str) {
    uint8_t keycodes[6] = {0};
    uint8_t modifier = 0;

    for (size_t i = 0; str[i] != '\0'; i++) {
        keycodes[0] = char_to_hid_keycode(str[i], &modifier);

        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycodes);
        vTaskDelay(pdMS_TO_TICKS(10));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

### 4. Automation flow

```c
    // Win + R
    send_key_with_modifier(KEYBOARD_MODIFIER_LEFTGUI,'r');
    vTaskDelay(pdMS_TO_TICKS(500));

    // Type "cmd"
    send_string("cmd");
    send_key(HID_KEY_ENTER);

    vTaskDelay(pdMS_TO_TICKS(600));

    // Type echo Hello World
    send_string("echo Hello World ESP32");
    send_key(HID_KEY_ENTER);

```

When powered via USB:
- The ESP32-S3 enumerates as a USB keyboard
- It waits until the PC recognizes the USB device
- When the BOOT button (GPIO 0) is pressed:
- It opens the CMD
- Then it prints Hello World ESP32

Expected Serial Output:

```log
I (478) TinyUSB: TinyUSB Driver installed
I (488) USB HID Example: USB initialization DONE
I (788) USB HID Example: Sending Keyboard report for keycode: r with modifier: 0x08
I (1308) USB HID Example: Sending Keyboard report for string: cmd
I (1368) USB HID Example: Sending Keyboard report for keycode: 0x28
I (1988) USB HID Example: Sending Keyboard report for string: echo Hello World ESP32
I (2428) USB HID Example: Sending Keyboard report for keycode: 0x28
```

---

## Notes
This project was tested on:
- Board: ESP32-S3-DevKitC-1
- ESP-IDF: v5.5.1
- OS: Windows 11 + VS Code + ESP IDF Extension
- Serial Baud: 115200 bps
---

## What You’ll Learn
- How to send advanced keystrokes as a USB keyboard
- How to send modifier keys such as Shift, Ctrl, GUI
- How to automatically handle uppercase characters using Shift
- How to send full strings via HID using ESP32-S3
- How to build a keystroke automation workflow

## Important Disclaimer — Educational Use Only

This project demonstrates how to use the ESP32-S3 as a USB HID keyboard for learning and experimentation.  
It must **not** be used for any illegal, harmful, or unauthorized activity.

You must **only** run USB HID keystroke automation on computers and systems you own or have explicit permission to test.

Misusing this project for unauthorized computer access or automated keystroke injection may violate computer security laws.

This tutorial is intended strictly for **educational and ethical purposes**.

---

## License
MIT License © 2025 — Chandima Jayaneththi

