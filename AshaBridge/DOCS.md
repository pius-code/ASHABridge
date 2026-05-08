# ASHABridge Documentation

## Overview

ASHABridge is an ESP32 firmware library that connects IoT devices to an AI agent via MQTT. The agent can control hardware pins, run batch commands, and execute real-time Lua scripts — all without reflashing the device.

---

## Architecture

### Classes

- `ASHA` — the main class. Owns WiFi, MQTT, Lua runtime, and device registry.
- `ASHA_WIFI` — handles WiFi connection and exposes an `onConnect` callback.
- `ASHA_Devices` — holds registered devices (pin, bus type, category, metadata).
- `ASHA_Actuators` — convenience factory for common actuator types (e.g. LED).

> Future: merge subclasses into one unified `ASHA` class.

### Core Assignment (FreeRTOS)

| Core   | Responsibility                                              |
| ------ | ----------------------------------------------------------- |
| Core 1 | Arduino `loop()`, MQTT client, WiFi stack                   |
| Core 0 | Lua task — sleeps on `portMAX_DELAY` until a script arrives |

The Lua task runs at priority 1 (low). MQTT and WiFi run at higher priorities — this is intentional. If the Lua task starved the MQTT loop, the WiFi would drop and Lua would never receive new scripts.

### Static Instance Pattern

`handleCommand` and `mqttCallback` are `static` because C callback pointers cannot be member functions (they have no `this`). To access instance members from these static functions, `ASHA` stores a pointer to itself:

```cpp
static ASHA* instance;  // set to `this` at the start of init()
```

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

### Lua limitations

- Blocking `while` loops in Lua are safe for MQTT (Core 0 is isolated) but prevent the Lua task from receiving new scripts until the loop exits.
- No raw C++/Arduino code — the agent writes Lua and calls `asha.command()` for hardware.

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

## Known Issues & Future Work

### Security

- **HTTPS not working**: `HTTPClient` without a root SSL certificate fails against strict servers (e.g. ngrok). The ESP32 needs either a pinned certificate or explicit `setInsecure()`. Needs a proper TLS solution before production.
- **MQTT topic is public**: The free broker allows anyone to subscribe to `asha/commands/<id>`. Mitigation: encrypt/hash the device ID on both publisher and subscriber so intercepted IDs are useless.

### Race condition on shared pins

If the agent sends both a direct MQTT command and a Lua script targeting the same pin simultaneously, both cores call `handleCommand` at the same time — undefined behavior on hardware. Planned fix: a unified pin-ownership database the agent checks before sending commands, so it knows which pins are under Lua control vs direct MQTT control.

### WiFi callback

`ASHA_WIFI::onConnect` accepts a user-defined callback that fires after connection. Currently used to print to Serial in `main.cpp`.

### Pin reassignment

If a user forgets their pin configuration, they can reassign pins via agent command without reflashing — the device registry in `ASHA_Devices` supports this at runtime.

### Agent control modes

Two control modes are planned:

1. **Direct** — agent sends individual commands on demand.
   useful when you just want to control a device one time like turn off the light or do something
2. **Lua** — agent sends a persistent script that runs on Core 0 indefinitely, reacting to sensor inputs without needing the agent online., useful for realtime
3. **workflow** - runs in the cloud, using for when you want timed tasks, like every 5 minutes check a sensor / turn on the light everyday at 6am and turn it off at 7pm..
