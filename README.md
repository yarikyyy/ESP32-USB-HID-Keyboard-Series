# ESP32-S3 USB HID Keyboard Tutorial Series




This repository contains a complete, step-by-step tutorial series that teaches how to transform the ESP32-S3 into a fully functional USB HID Keyboard, and gradually extend its capability from basic keystrokes → modifier keys → full WebApp with WebSocket communication.

Each stage builds on the previous one and focuses on a specific concept, making it easy to follow and learn progressively.

**Important Warning**: 
This tutorial is for educational purposes only.
USB HID automation can be misused for malicious activities (BadUSB attacks).
Do not use this code in any illegal, harmful, or unauthorized environment.

## Project Overview

### [Stage 1 — Basic USB HID Keystrokes](stage_1_basic_keys/README.md)
- Basic ASCII → HID mapping
- Single key presses
- String typing (simple characters only)
- Based on Espressif TinyUSB HID example, minimally modified

### [Stage 2 — Advanced HID & Modifier Keys](stage2_advanced_keystrokes/README.md)
- Ctrl / Shift key handling
- Automatic uppercase → Shift conversion
- Special keys (Enter, Backspace, Arrows, Win+R)

### [Stage 3 — WebApp-Controlled USB HID Keyboard](stage3_keystrokes_web_ui/README.md)
- ESP32-S3 runs a Wi-Fi Access Point
- Web server + WebSocket endpoint
- HTML/JS WebApp to send keystrokes
- JSON-based commands (string, key, combinations)
- ESP32 injects keystrokes into PC via USB
---

## Software Requirements

- **ESP-IDF v5.1 or later**
- **Serial terminal** (`idf.py monitor` or VS Code IDF Monitor)

---

## Important Safety Statement

USB HID devices can control a host computer without permission.
This can be misused for harmful purposes (e.g., BadUSB attacks).

- Only run this on machines you fully own and trust.
- Do not use at workplaces or public environments.
- This series is strictly for learning and experimentation.

## License
MIT License © 2025 — Chandima Jayaneththi

