# ASHABridge Documentation

## Current Capabilities

- Connect an ESP32 to a WiFi network and register it with the ASHA cloud backend
- Register up to 30 hardware devices (sensors and actuators) with descriptive metadata
- Control hardware pins over MQTT: digital read/write, PWM, I2C, SPI, UART
- Send batched multi-step commands with non-blocking delays
- Run real-time Lua scripts on the device (Core 0) without reflashing
- Read sensor values back to the agent via MQTT request-response
- Automatic WiFi and MQTT reconnection with a watchdog safety net

---

## How It Works

### Overview

ASHABridge is an ESP32 firmware library. You register your hardware devices in `setup()`, call `asha.init()` to connect to the cloud backend and MQTT broker, then call `asha.run()` in `loop()`. From that point the agent controls the device entirely over MQTT — no reflashing needed.

### Core Assignment (FreeRTOS)

| Core   | Responsibility                                              |
| ------ | ----------------------------------------------------------- |
| Core 1 | Arduino `loop()`, MQTT client, WiFi stack                   |
| Core 0 | Lua task — sleeps on `portMAX_DELAY` until a script arrives |

The Lua task runs at priority 1 (low). MQTT and WiFi run at higher priorities — this is intentional. If the Lua task starved the MQTT loop, the WiFi would drop and Lua would never receive new scripts.

### Classes

- `ASHA` — the main class. Owns WiFi, MQTT, Lua runtime, and device registry.
- `ASHA_WIFI` — handles WiFi connection and exposes an `onConnect` callback.
- `ASHA_Devices` — holds registered devices (pin, bus type, category, metadata).
- `ASHA_Actuators` — convenience factory for common actuator types (e.g. LED).

> Future: merge subclasses into one unified `ASHA` class.

### Static Instance Pattern

`handleCommand` and `mqttCallback` are `static` because C callback pointers cannot be member functions (they have no `this`). To access instance members from these static functions, `ASHA` stores a pointer to itself:

```cpp
static ASHA* instance;  // set to `this` at the start of init()
```

---

## Getting Started

### 1. Import

Include the library and declare the global objects at the top of your `main.cpp`:

```cpp
#include <ASHA.h>
#include <Arduino.h>

ASHA asha;
ASHA_Actuators ashaActuators;
```

### 2. WiFi Setup

Define your credentials and wire up an optional post-connection callback:

```cpp
const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void afterConnection() {
    Serial.println("ASHA: Internet Connected");
    // anything you want to run right after WiFi connects
}

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    // ...
}
```

### 3. Adding Devices

Call `addDevice` for each piece of hardware before calling `init`. Each device needs a type (built with `genericDev`) and the GPIO pin it is on.

```cpp
asha.asha_devices.addDevice(
    asha.genericDev(DeviceCategory::Sensor, "My water sensor", BusType::Analog), 34);

asha.asha_devices.addDevice(
    asha.genericDev(DeviceCategory::Actuator, "My light", BusType::PWM), 18);

asha.asha_devices.addDevice(
    asha.genericDev(DeviceCategory::Actuator, "My buzzer", BusType::PWM), 23);
```

**`genericDev` signature:**
```cpp
DeviceType genericDev(DeviceCategory category, const std::string& metadata, BusType bus);
```

| Parameter  | Options                                          |
| ---------- | ------------------------------------------------ |
| `category` | `DeviceCategory::Sensor`, `DeviceCategory::Actuator` |
| `metadata` | Any human-readable label — the agent uses this   |
| `bus`      | `BusType::Digital`, `BusType::Analog`, `BusType::PWM`, `BusType::I2C`, `BusType::SPI` |

The library automatically calls `pinMode` for Digital and SPI devices during `init`. For I2C, `Wire.begin()` is called once when the first I2C device is registered.

### 4. Init and Run

```cpp
void setup() {
    // ... wifi and devices above ...
    asha.init("YOUR_ASHA_ID");
}

void loop() {
    asha.run();
}
```

`init` registers your devices with the cloud backend and connects to the MQTT broker.  
`run` must be called every loop tick — it handles WiFi watchdog, MQTT reconnection, and the MQTT client loop.

---

## Command Format (MQTT)

The agent publishes JSON to `asha/commands/<device_id>`.

### Digital

```json
{ "pin": 18, "action": "digital", "value": 1 }
```

Set `value` to `-1` to read the pin instead of writing.

### PWM

```json
{ "pin": 18, "action": "pwm", "channel": 0, "freq": 5000, "duty": 65535 }
```

### I2C Write

```json
{ "pin": 21, "action": "i2c_write", "addr": 60, "reg": 0, "data": [174] }
```

### I2C Read

```json
{ "action": "i2c_read", "addr": 60, "reg": 0, "len": 6 }
```

### SPI Write

```json
{
  "action": "spi_write",
  "cs_pin": 5,
  "speed": 25000000,
  "mode": 0,
  "data": [1, 2, 3]
}
```

### UART Write

```json
{
  "action": "uart_write",
  "baud": 9600,
  "tx_pin": 17,
  "rx_pin": 16,
  "data": "AT+CREG?\r\n"
}
```

### Batch (multiple commands in sequence)

```json
{
  "action": "batch",
  "commands": [
    { "pin": 18, "action": "digital", "value": 1 },
    { "delay_ms": 2000 },
    { "pin": 18, "action": "digital", "value": 0 }
  ]
}
```

### Non-blocking Delay (use inside batch)

```json
{ "delay_ms": 2000 }
```

The delay uses `millis()` internally and keeps calling `mqttClient.loop()` every 10ms so MQTT stays alive during the wait.

### Lua Script

```json
{
  "action": "lua",
  "script": "asha.command('{\"pin\": 18, \"action\": \"digital\", \"value\": 1}')"
}
```

The script runs on Core 0. See the Lua section below.

---

## Lua Scripting

The agent can send real-time Lua scripts that run persistent logic on the device — conditions, loops, decisions — without reflashing.

### How it works

1. Agent sends `{"action": "lua", "script": "..."}` over MQTT.
2. `mqttCallback` (Core 1) `strdup`s the script onto the heap and pushes it into a FreeRTOS queue.
3. `luaTask` (Core 0) wakes up, runs the script, then frees the heap copy.

### Available Lua functions

| Lua call                | What it does                                  |
| ----------------------- | --------------------------------------------- |
| `asha.command(jsonStr)` | Runs any command JSON through `handleCommand` |
| `asha.digitalRead(pin)` | Returns 0 or 1                                |
| `asha.analogRead(pin)`  | Returns 0–4095                                |
| `asha.sleep(ms)`        | Non-blocking task delay                       |
| `print(...)`            | Prints to Serial                              |
| `millis()`              | Returns uptime in milliseconds                |

### Example script

```lua
local btn = asha.digitalRead(21)
if btn == 1 then
  asha.command('{"action": "batch", "commands": [{"pin": 18, "action": "digital", "value": 0}, {"pin": 20, "action": "digital", "value": 1}]}')
else
  asha.command('{"pin": 18, "action": "digital", "value": 1}')
end
```

### Real-world use cases

#### Monitoring a PWM-controlled device (e.g. LED or motor)

Turn on a device at full brightness via PWM, then send a Lua script that watches for when it turns off and triggers a reaction (alarm, beep, fallback action).

This works because of how PWM duty cycle maps to pin state. In `handleCommand`, the PWM resolution is 13 bits:

```cpp
ledcSetup(channel, freq, 13);  // 13-bit resolution, max value = 8191
ledcWrite(channel, duty >> 3); // duty divided by 8 before writing
```

At full brightness (`duty = 65535 → 65535 >> 3 = 8191`), the pin is **always HIGH**. `digitalRead` reliably returns 1.  
When off (`duty = 0`), the pin is **always LOW**. `digitalRead` reliably returns 0.  
At intermediate brightness (e.g. 50% duty), the pin genuinely toggles — `digitalRead` will give inconsistent results.

```lua
-- watching pin 18: when the LED turns off, beep the buzzer on pin 25 every 10 seconds
while true do
  local led = asha.digitalRead(18)
  if led == 0 then
    asha.command('{"action": "batch", "commands": [{"pin": 25, "action": "digital", "value": 1}, {"delay_ms": 200}, {"pin": 25, "action": "digital", "value": 0}, {"delay_ms": 9800}]}')
  end
end
```

#### Sensor threshold monitoring (e.g. heart rate, temperature)

For analog sensors, use `asha.analogRead()` on an ADC-capable pin (GPIO 32–39 on ESP32).

```lua
while true do
  local bpm = asha.analogRead(34)
  if bpm < 300 then
    asha.command('{"pin": 25, "action": "digital", "value": 1}')  -- alarm on
  else
    asha.command('{"pin": 25, "action": "digital", "value": 0}')  -- alarm off
  end
end
```

### Workarounds when a Lua loop is running

Raw MQTT commands (`digital`, `pwm`, `analog`, `batch`) are handled directly in `mqttCallback` on Core 1 — they **bypass the Lua queue entirely** and still execute immediately even while a Lua loop is occupying Core 0.

**Caveat:** if the raw command targets the **same pin** the Lua loop is already controlling, both cores will call `handleCommand` on that pin simultaneously — undefined hardware behavior. Keep raw commands on pins the Lua loop is not touching.

---

## Sensor Reading (Implemented)

The feedback mechanism is implemented for Digital, PWM, and Analog buses via MQTT request-response.

**How it works:**

1. Agent publishes a read command to `asha/commands/<device_id>` with `value: -1` and a `correlation_id`
2. ESP32 reads the pin/channel and publishes the result to `asha/response/<device_id>`
3. The server waits up to 5 seconds for the response, matched by `correlation_id`

**Digital read response:**
```json
{ "correlation_id": "...", "pin": 18, "value": 0 }
```

**PWM read response:**
```json
{ "correlation_id": "...", "pin": 32, "ledc_duty": 65535 }
```
`ledc_duty` is 0–65535, reflecting what was last written to the channel — it does not confirm the device is physically working.

---

## Agent Control Modes

| Mode         | How it works                                                                 |
| ------------ | ---------------------------------------------------------------------------- |
| **Direct**   | Agent sends individual one-shot commands on demand                           |
| **Lua**      | Agent sends a persistent script that runs on Core 0, reacting to sensor input in real time |
| **Workflow** | Agent polls from the cloud on a schedule (e.g. every 5 minutes, or at 6am daily) |

---

## Memory

| Resource            | Size    | Notes                          |
| ------------------- | ------- | ------------------------------ |
| ESP32 RAM           | 320 KB  | Total                          |
| MQTT receive buffer | 16 KB   | Set via `setBufferSize`        |
| Lua task stack      | 10 KB   | Heap-allocated by FreeRTOS     |
| Lua script queue    | 5 slots | Each slot is a `char*` pointer |

Payloads over 16 KB will be silently dropped by PubSubClient.

---

## Limitations

- **`while` loops in Lua are dangerous.** The Lua VM is single-threaded — a `while` loop permanently occupies it, blocking all 5 queue slots. Any subsequent scripts sent by the agent will queue and never execute until the device reboots. There is no way to stop a running `while` loop remotely — **the only recovery is a device restart.** For persistent monitoring, prefer the Workflow control mode.
- **`analogRead` only works on GPIO 32–39.** Calling it on a digital-only pin returns garbage.
- **`digitalRead` on a PWM pin is only reliable at full on (100% duty) or full off (0%).** Intermediate brightness gives inconsistent readings.
- **`analogRead` cannot be used on an active PWM pin.** Calling it detaches the pin from the LEDC peripheral, turning the device off.
- **Lua scripts do not survive reboots.** Scripts are stored only in RAM. On reboot, all scripts are lost and must be resent by the agent. (NVS or LittleFS persistence is planned.)
- **MQTT topic is public.** The broker allows anyone to subscribe to `asha/commands/<id>`. A hashed/encrypted device ID is planned.
- **HTTPS without a pinned certificate fails** against strict servers. A proper TLS solution is needed before production.
- **Race condition on shared pins.** If the agent sends both a direct MQTT command and a Lua script targeting the same pin simultaneously, both cores call `handleCommand` at the same time — undefined hardware behavior.

---

## Known Issues

### MQTT Silent Dropout — FIXED

**Symptom:** Commands from the agent stopped reaching the ESP32 after 1–2 minutes of inactivity.

**Root cause:** The WiFi connection was dropping (phone hotspot silently disconnects idle clients), not MQTT itself. Once WiFi dropped, `run()` exited early before reaching the MQTT reconnect logic.

**Fix:**
1. Use a laptop or router hotspot — phone hotspots silently drop idle devices.
2. `WiFi.reconnect()` is called inside `run()` whenever WiFi drops.
3. MQTT watchdog is set to 30 minutes (`1800000 ms`) as a last-resort safety net, not the primary reconnect mechanism.

### PWM Fault Detection Limitation

`ledc_duty` reads what the controller was *told* to output, not what the device is *actually* doing. A burnt-out LED reads `ledc_duty = 65535` even though it is off.

**Hardware solution — Voltage Divider Feedback:**

```
ESP32 PWM pin → resistor → LED → GND
                    ↓
              100kΩ to ADC pin (GPIO 32-39)
```

- LED on → ADC pin reads ~3.3V
- LED off or burnt out → ADC pin reads ~0V
- `ledc_duty` high but ADC near zero → device is faulty
