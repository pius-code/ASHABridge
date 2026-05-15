# ASHABridge

**ASHABridge** is an microcontroller firmware library that connects IoT hardware to an AI agent via MQTT. The agent can control pins, run batch sequences, and execute real-time Lua scripts on the device — all without reflashing.

---

> **Development Phase Notice**
>
> This project is actively under development. It works for most common use cases (digital control, PWM, analog sensing, I2C, SPI, UART, Lua scripting), but some features are still in progress. You are welcome to test it, open issues, and contribute. See the [To-Do](#to-do) section for what is planned next.

---

## Platform Support

| Platform       | Status                  |
| -------------- | ----------------------- |
| ESP32          | Supported               |
| Raspberry Pi   | In progress             |
| Arduino        | In progress             |

---

## Quick Start

### 1. Install dependencies

This project uses [PlatformIO](https://platformio.org/). Dependencies are declared in `platformio.ini` and installed automatically on first build.

### 2. Configure credentials

Before building, open the following files and replace the placeholder values:

**`src/main.cpp`** — your WiFi credentials and ASHA device ID:
```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

asha.init("YOUR_ASHA_ID");
```

**`lib/ASHABridge/ASHA.cpp`** — your backend URL and MQTT broker:

see {% embed https://github.com/pius-code/ashaBackend %} for ashaBackend and while do a localhost:8080/docs creating a new project gives you the ashaID(sometimes callled the auth id)
```cpp
http.begin(client, "YOUR_BACKEND_URL/api/v1/asha/verify_and_register_device");

mqttClient.setServer("YOUR_MQTT_BROKER_IP", 1883);

mqttClient.connect(clientId.c_str(), "YOUR_MQTT_USERNAME", "YOUR_MQTT_PASSWORD");
```

### 3. Add your devices

In `setup()`, register each piece of hardware with `addDevice` before calling `init`:

```cpp
#include <ASHA.h>
#include <Arduino.h>

ASHA asha;

void setup() {
    asha.asha_wifi.onConnect([]() {
        Serial.println("Connected!");
    });
    asha.asha_wifi.begin("YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD");

    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Sensor, "My water sensor", BusType::Analog), 34);

    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "My light", BusType::PWM), 18);

    asha.init("YOUR_ASHA_ID");
}

void loop() {
    asha.run();
}
```

See [DOCS.md](DOCS.md) for the full API reference, command format, Lua scripting guide, and known limitations.

---

## To-Do

1. **Wireless device support (IR and RF)** — add command handlers for IR blasters and RF transmitters so the agent can control wireless devices (e.g. TVs, remote-controlled sockets) the same way it controls wired GPIO.

2. **Replace `while` loops with interrupts** — the current Lua monitoring pattern uses `while true` loops which permanently lock the Lua VM. The fix is to use hardware interrupts (via `attachInterrupt`) on the C++ side and expose a callback registration API to Lua, so the Lua VM stays free between events. The agent's tool descriptions should also be updated to reflect the interrupt-based approach.

3. **Hash the ASHA ID with SHA-256** — the device ID is currently stored and transmitted in plain text. Encoding it with SHA-256 on both the device and the backend means that even if someone reads the compiled binary or intercepts the MQTT topic, the raw ID cannot be recovered.
