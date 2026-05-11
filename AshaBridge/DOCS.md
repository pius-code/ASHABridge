# ASHABridge Documentation

## Overview

ASHABridge is an ESP32 firmware library that connects IoT devices to an AI agent MCP via MQTT. The agent can control hardware pins, run batch commands, and execute real-time Lua scripts — all without reflashing the device.

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

### Real-world use cases

#### Monitoring a PWM-controlled device (e.g. LED or motor)

A common scenario: turn on a device at full brightness via PWM, then send a Lua script that watches for when it turns off and triggers a reaction (alarm, beep, fallback action).

This works because of how PWM duty cycle maps to pin state. In `handleCommand`, the PWM resolution is 13 bits and the duty is written as:

```cpp
ledcSetup(channel, freq, 13);  // 13-bit resolution, max value = 8191
ledcWrite(channel, duty >> 3); // duty divided by 8 before writing
```

At full brightness (`duty = 65535 → 65535 >> 3 = 8191`), the duty cycle is 100% — the pin is **always HIGH**. `digitalRead` reliably returns 1.

When the device turns off (`duty = 0` or a digital 0 command), the pin is **always LOW**. `digitalRead` reliably returns 0.

So `digitalRead` works correctly at the extremes (full on / full off). The only unreliable case is intermediate brightness (e.g. 50% duty), where the pin genuinely toggles and `digitalRead` could catch it either way.

```lua
-- runs on Core 0, watching pin 18
-- when the LED turns off, beep the buzzer on pin 25 every 10 seconds
while true do
  local led = asha.digitalRead(18)
  if led == 0 then
    asha.command('{"action": "batch", "commands": [{"pin": 25, "action": "digital", "value": 1}, {"delay_ms": 200}, {"pin": 25, "action": "digital", "value": 0}, {"delay_ms": 9800}]}')
  end
end
```

#### Sensor threshold monitoring (e.g. heart rate, temperature)

For analog sensors, use `asha.analogRead()` on an ADC-capable pin (GPIO 32–39 on ESP32). If the reading drops below a threshold — sensor disconnected, patient removed, abnormal reading — trigger an alarm immediately.

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

Note: `analogRead` only works on ADC-capable pins. Calling it on a non-ADC pin (e.g. GPIO 18) returns garbage.

### Lua limitations

- Blocking `while` loops in Lua are safe for MQTT (Core 0 is isolated) but prevent the Lua task from receiving new scripts until the loop exits.
- No raw C++/Arduino code — the agent writes Lua and calls `asha.command()` for hardware.
- `digitalRead` on a PWM pin is only reliable at full on (100% duty) or full off (0% duty). Intermediate brightness levels will give inconsistent readings.
- `analogRead` only works on GPIO 32–39. Do not call it on digital-only pins.

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

## Before Production

### MQTT Silent Dropout — FIXED

**Symptom:** Commands from the agent stopped reaching the ESP32 after 1–2 minutes of inactivity. The MQTT watchdog stopped firing entirely. Reflashing restored it temporarily.

**Root cause:** The underlying issue was the **WiFi connection dropping**, not MQTT. A phone hotspot silently disconnects idle clients. Once WiFi dropped, `run()` exited early on the WiFi guard before reaching either the watchdog check or the MQTT reconnect — so Serial went completely silent.

**Fix:**

1. Use a laptop or router hotspot instead of a phone hotspot. Phone hotspots silently drop idle devices regardless of keepalive traffic.
2. `WiFi.reconnect()` is called inside `run()` whenever `WiFi.status() != WL_CONNECTED`, so the ESP32 actively retries the WiFi connection instead of waiting passively.
3. The MQTT watchdog timer was increased to 30 minutes (`1800000 ms`) to avoid interfering with normal command flow — it is a last-resort safety net, not the primary reconnect mechanism.

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

### Lua script persistence (not yet implemented)

Lua scripts are stored only in RAM. When the ESP32 reboots, the queue and Lua runtime are wiped — all scripts are lost and must be resent by the agent.

To survive reboots, scripts need to be saved to flash. Two options:

- **NVS** (Non-Volatile Storage) — ESP32's built-in key-value store, good for a single active script per key.
- **LittleFS** — a small filesystem in flash, better if multiple named scripts need to be stored.

On boot, `init()` would read the saved script(s) from flash and push them into the Lua queue automatically.

### Multiple concurrent Lua scripts

The current architecture has one Lua task (Core 0). Scripts queue and run one after the other. If a script contains a `while true do` infinite loop, it occupies the Lua task permanently — any subsequent script sits in the queue and never runs.

To run two persistent scripts simultaneously (e.g. one monitoring a button, one monitoring a sensor), the options are:

- Spin up multiple Lua tasks, each with its own queue slot.
- Use Lua coroutines for cooperative multitasking within a single task.

### Agent control modes

Two control modes are planned:

1. **Direct** — agent sends individual commands on demand.
   useful when you just want to control a device one time like turn off the light or do something
2. **Lua** — agent sends a persistent script that runs on Core 0 indefinitely, reacting to sensor inputs without needing the agent online., useful for realtime
3. **workflow** - runs in the cloud, using for when you want timed tasks, like every 5 minutes check a sensor / turn on the light everyday at 6am and turn it off at 7pm..

### An actual feedback mechanism - FUTURE WORK

## monitoring PWM pin has an issue since pwm uses frequency to alternate quicky between high and low, if you check for example if low you will get a low..

ecxample probelm: I wanted to check if pin(18) using pwm was on,if off turn the bizzer on, and the agent ised digitalRead but because of rapid frequency(500) it kept buzzing so we used ledcRead instead which reads the hardware Register where the PWM stire the duty value
but :
analogRead → measures physical reality (voltage) → unreliable for PWM
ledcRead → reads a config register → always returns exactly what you set, instantly

if the device is faulty we will never know because we are reading what we set it to.

**the problem is we want to actually know the state of the device, if we say turn on the light, we want to know if the light is actually on, for humans we can just look and know but for a truly autonomous agent it wouldnt know, now we want to check the curremt on the pin to know if the voltage has dropped solutions below**
The actual hardware solution
You're right that someone has thought about this. It's called current sensing and voltage feedback and it's done all the time in embedded systems.
Option 1: ADC on the LED pin
When an LED is lit, there's a voltage drop across it (~2V for red, ~3.2V for blue/white). The remaining voltage appears at the pin.
You can't do this directly because the pin is configured as OUTPUT — it's driving the voltage, not reading it. But you can:
cpp// briefly switch to input, sample, switch back
pinMode(18, INPUT);
int voltage = analogRead(18); // read actual voltage
pinMode(18, OUTPUT);
The problem — this creates a tiny glitch on the pin. Milliseconds, but the LED flickers. Not ideal but functional.
Option 2: Voltage divider feedback
Connect a second wire from the LED's positive leg through a resistor to a separate ADC input pin. That pin reads the actual voltage at the LED, completely independently:
ESP32 pin 18 → resistor → LED → GND
↓
100kΩ to pin 34 (ADC)
Pin 34 reads the voltage. If LED is on — high voltage. If LED is off or burnt out — near zero. No extra sensor needed, just one resistor and one wire.
This is how professional embedded systems do feedback sensing without external sensors.
Option 3: Current mirror
Even more precise — put a small sense resistor (0.1Ω) in series with the LED. Measure the voltage drop across it via ADC. Ohm's law gives you the current. Current flowing = LED on. No current = LED off or dead.
ESP32 pin 18 → LED → 0.1Ω sense resistor → GND
↓
ADC pin reads voltage drop
