# ASHABridge

[![PlatformIO Registry](https://badges.registry.platformio.org/packages/pius-code/library/ASHA.svg)](https://registry.platformio.org/libraries/pius-code/ASHA)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform](https://img.shields.io/badge/Platform-ESP32-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![Framework](https://img.shields.io/badge/Framework-Arduino-green.svg)](https://www.arduino.cc/)

**ASHABridge** is an ESP32 firmware library that turns microcontrollers into hardware endpoints for AI agents. Connect actuators, sensors, and peripherals to your ESP32, and the ASHA agent can discover them, read pins, execute batched commands, send raw IR/SPI/I2C signals, and run dynamic Lua scripts over the air — **with zero firmware reflashing**.

---

## Features

- **Dynamic Hardware Control**: Digital read/write, PWM frequency & duty adjustment, analog sensing, I2C, SPI (with custom pin assignments), UART, and raw IR transmission.
- **Over-The-Air Lua Engine**: Run Lua scripts on FreeRTOS Core 0 without reflashing firmware. Scripts can sleep, read pins, subscribe to MQTT topics, and publish telemetry back to the agent.
- **Multi-Step Batching**: Execute sequence-based hardware steps with non-blocking delays.
- **Automated Cloud Registration**: Automatically registers connected hardware, pin modes, bus types, and metadata with the ASHA Cloud backend upon boot.
- **Dual-Path MQTT Watchdog**: Dedicated command execution pipeline and streaming message pipeline with automatic reconnection.
- **Simplified API**: Clean, ergonomic C++ interface for quick prototyping.

---

## Installation

### PlatformIO

Add `pius-code/ASHA` to your `lib_deps` in `platformio.ini`:

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    pius-code/ASHA @ ^1.0.0
```

Or install via the PlatformIO Core CLI:
```bash
pio pkg install -l "pius-code/ASHA"
```

### Arduino IDE

1. Search for **ASHABridge** in the Arduino Library Manager (`Tools` → `Manage Libraries...`) or download from [Releases](https://github.com/pius-code/ASHABridge/releases).
2. The following dependencies are required (Arduino IDE will prompt to install them):
   - `ArduinoJson` (v7+)
   - `PubSubClient`
   - `Esp32Lua`
   - `IRremoteESP8266`

---

## Quick Start

Open `examples/BasicBridge/BasicBridge.ino` or paste the following into your `main.cpp`:

```cpp
#include <ASHA.h>
#include <Arduino.h>

ASHA asha;

void afterConnection() {
    Serial.println("ASHA: Internet Connected");
}

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
    // 1. Connect WiFi with optional callback
    asha.connectWiFi(ssid, password, afterConnection);

    // 2. Register devices (Category, Label, Bus, Pin)
    asha.addDevice(DeviceCategory::Actuator, "Bedroom light", BusType::Digital, 2);
    asha.addDevice(DeviceCategory::Actuator, "Ceiling fan", BusType::PWM, 4);
    asha.addDevice(DeviceCategory::Actuator, "Front door servo", BusType::PWM, 5);
    asha.addDevice(DeviceCategory::Sensor, "Ambient light sensor", BusType::Analog, 34);

    // Shorthand helpers are also available:
    // asha.addActuator("Hall buzzer", BusType::PWM, 19);
    // asha.addSensor("Living room temp", BusType::Analog, 35);

    // SPI devices with custom pins (sck, miso, mosi, cs):
    // asha.addDevice(DeviceCategory::Sensor, "RFID Reader", BusType::SPI, 25, 33, 27, 26);

    // 3. Connect to ASHA Cloud backend and register project
    asha.init("My Home Project");
}

void loop() {
    // 4. Handle MQTT keepalive, connection watchdog, and Lua tasks
    asha.run();
}
```

---

## Supported Bus Types & Hardware

| Bus Type | Arguments | Description |
| :--- | :--- | :--- |
| `BusType::Digital` | `pin` | Digital input / output (`pinMode`, `digitalWrite`, `digitalRead`) |
| `BusType::PWM` | `pin` | Hardware timer PWM via ESP32 LEDC (frequency, duty cycle) |
| `BusType::Analog` | `pin` | 12-bit ADC reads (`analogRead`) |
| `BusType::I2C` | Default I2C pins | Registers `Wire.begin()` and handles `i2c_read` / `i2c_write` |
| `BusType::SPI` | `sck`, `miso`, `mosi`, `cs` | Custom SPI bus configuration with per-device chip select |
| `BusType::IR` | `pin` | Raw timing infrared transmission via ESP32 RMT peripheral |
| `BusType::UART` | `baud`, `tx`, `rx` | Serial communication over UART2 |

---

## How It Works

```
 ┌──────────────────────┐             MQTT              ┌────────────────────────┐
 │      ASHA Cloud      │ ◄───────────────────────────► │       ESP32 Node       │
 │   & AI Agent Core    │   asha/commands/{asha_id}     │       (ASHABridge)     │
 └──────────────────────┘                               └────────────────────────┘
            ▲                                                        │
            │                                           ┌────────────┴───────────┐
            │ HTTP (Device Registration)                ▼                        ▼
            │                                       [Core 1]                 [Core 0]
            │                                    MQTT Watchdog           Lua VM Runtime
            └─────────────────────────────────── Hardware Control       OTA Script Tasks
```

1. **Boot & Registration**: In `setup()`, you define your connected devices. Calling `asha.init()` sends an HTTP registration payload to the backend with your device MAC address, device schema, and custom pin layouts.
2. **Pairing**: The backend returns a unique `asha_id` and pairing code.
3. **Agent Control**: The cloud agent publishes structured JSON commands (or Lua scripts) over MQTT to `asha/commands/<ashaID>`.
4. **Execution**: Core 1 handles incoming MQTT actions immediately, while Core 0 runs real-time Lua control loops without blocking network communication.

---

## FreeRTOS Core Allocation

| Core | Task | Priority | Description |
| :--- | :--- | :--- | :--- |
| **Core 1** | Main Arduino Loop | High | Runs `asha.run()`, WiFi health checks, MQTT keepalive & command execution |
| **Core 0** | Lua Engine | Low (1) | Executes dynamic OTA scripts without starving network or MQTT stack |

---

## Documentation

For full command JSON formats, MQTT topic structures, Lua API reference, and troubleshooting, read the **[Full Documentation (DOCS.md)](AshaBridge/DOCS.md)**.

---

## Roadmap

- [x] Simplified high-level API (`asha.addDevice`, `asha.connectWiFi`, `asha.addActuator`, `asha.addSensor`)
- [x] Custom SPI bus pin mappings with cloud persistence
- [x] Thread-safe Lua `publish()` and SPI transfer over-the-air
- [x] Official PlatformIO registry release (`pius-code/ASHA`)
- [ ] Hardware interrupts for Lua events (`attachInterrupt` callback bindings)
- [ ] SHA-256 hashed ASHA ID transport
- [ ] RF 433MHz transmitter/receiver support
- [ ] ESP Matter integration (exposing devices to Apple Home, Google Home, and Alexa)

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## FreeRTOS Core Allocation

| Core | Task | Priority | Description |
| :--- | :--- | :--- | :--- |
| **Core 1** | Main Arduino Loop | High | Runs `asha.run()`, WiFi health checks, MQTT keepalive & command execution |
| **Core 0** | Lua Engine | Low (1) | Executes dynamic OTA scripts without starving network or MQTT stack |

---

## Documentation

For full command JSON formats, MQTT topic structures, Lua API reference, and troubleshooting, read the **[Full Documentation (DOCS.md)](AshaBridge/DOCS.md)**.

---

## Roadmap

- [x] Simplified high-level API (`asha.addDevice`, `asha.connectWiFi`, `asha.addActuator`, `asha.addSensor`)
- [x] Custom SPI bus pin mappings with cloud persistence
- [x] Thread-safe Lua `publish()` and SPI transfer over-the-air
- [x] Official PlatformIO registry release (`pius-code/ASHA`)
- [ ] Hardware interrupts for Lua events (`attachInterrupt` callback bindings)
- [ ] SHA-256 hashed ASHA ID transport
- [ ] RF 433MHz transmitter/receiver support
- [ ] ESP Matter integration (exposing devices to Apple Home, Google Home, and Alexa)

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

