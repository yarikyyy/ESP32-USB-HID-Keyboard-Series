# ESP32-S3 USB HID Keyboard Basic Keystroke

This tutorial demonstrates how to use the ESP32-S3’s native USB interface to emulate a USB Keyboard using ESP-IDF and TinyUSB.
When the ESP32-S3 is connected to a computer, it will automatically identify as a USB keyboard and send keystrokes.
This example is based on and modified from the official Espressif ESP-IDF USB HID example

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
stage_1_basic_keys/
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

This example is a simplified and modified version of the official Espressif ESP-IDF TinyUSB HID example.  
The code is reorganized to make it easier to understand how USB HID keyboard emulation works on the ESP32-S3.  
Below is a breakdown of the key components in the C file used in this project
### 1. HID Report Descriptor

```c
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1))
};
```

This descriptor tells the host computer that the ESP32-S3 should be treated as a USB Keyboard.
The host uses this information to understand how to interpret HID reports from the device.

### 2. ASCII → HID Keycode Conversion

```c
KeyCodeMapping conv_table[] = {HID_ASCII_TO_KEYCODE};
```
TinyUSB provides a built-in lookup table that converts ASCII characters (like 'a', 'A', 1, space, etc.) into HID keycodes.
This allows you to send normal characters simply by passing them through this table.

### 3. Sending a String

```c
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
```
Each character is converted to a HID keycode and sent individually using the same press–release pattern.


A text editor is required to open actively prior to run this on the ESP32-S3.

When powered via USB:
- The ESP32-S3 enumerates as a USB keyboard
- It waits until the PC recognizes the USB device
- When the BOOT button (GPIO 0) is pressed:
- It sends the string "abcdefg123456789"
- Then it sends Ctrl + A (Select All)
- Then sends the Delete key
- You will see this happen in any text editor where your cursor is active.

Expected Serial Output:

```log
I (478) TinyUSB: TinyUSB Driver installed
I (488) USB HID Example: USB initialization DONE
I (788) USB HID Example: Sending Keyboard report for string: Hello World ESP32
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
- How USB HID works on the ESP32-S3
- How to send keyboard HID reports
- How ASCII is converted to HID keycodes
- How to send a full string as keystrokes

---

## Important Disclaimer — Educational Use Only

This project demonstrates how to use the ESP32-S3 as a USB HID keyboard for learning and experimentation.  
It must **not** be used for any illegal, harmful, or unauthorized activity.

You must **only** run USB HID keystroke automation on computers and systems you own or have explicit permission to test.

Misusing this project for unauthorized computer access or automated keystroke injection may violate computer security laws.

This tutorial is intended strictly for **educational and ethical purposes**.

---

## License
MIT License © 2025 — Chandima Jayaneththi
