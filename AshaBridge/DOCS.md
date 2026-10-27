# ASHABridge Documentation

## Current Capabilities

- Connect an ESP32 to a WiFi network and register it with the ASHA cloud backend
- Register up to 30 hardware devices (sensors and actuators) with descriptive metadata
- Control hardware pins over MQTT: digital read/write, PWM, I2C, SPI, UART, IR
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

### mqttCallback — The Two-Path Fork

Every incoming MQTT message passes through `mqttCallback`. The first thing it does is check which topic the message arrived on:

```
message arrives on any topic
        ↓
is it on asha/commands/{this_device_id}?
        │
        ├── YES → handle as a command
        │         (digital, pwm, lua, batch, i2c, spi, uart...)
        │
        └── NO  → store raw payload in topicPayloads[topic]
                  and return early
                  (Lua scripts poll this via asha.readMessage())
```

**Why this matters for the vision feature:**

- The agent sends the Lua script to `asha/commands/abc123` → condition is FALSE → falls through to command handling → script runs → `asha.subscribe("asha/asha_vision/abc123")` is called → device opts in to the vision topic.
- Later, the vision server publishes `"person"` to `asha/asha_vision/abc123` → condition is TRUE → stored in `topicPayloads` → Lua reads it via `asha.readMessage("asha/asha_vision/abc123")`.

The Lua script arrives on the **commands path**. The vision data arrives on the **topicPayloads path**. They never interfere with each other.

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
    asha.connectWiFi(ssid, password, afterConnection);
    // ...
}
```

You can also pass `nullptr` or omit `afterConnection` if you don't need a callback:
```cpp
asha.connectWiFi(ssid, password);
```

### 3. Adding Devices

Call `addDevice` directly on `asha` for each piece of hardware before calling `init`:

```cpp
asha.addDevice(DeviceCategory::Sensor, "My water sensor", BusType::Analog, 34);
asha.addDevice(DeviceCategory::Actuator, "My light", BusType::PWM, 18);
asha.addDevice(DeviceCategory::Actuator, "My buzzer", BusType::PWM, 23);
```

Alternatively, use shorthand helpers for sensors and actuators:
```cpp
asha.addSensor("My water sensor", BusType::Analog, 34);
asha.addActuator("My light", BusType::PWM, 18);
asha.addActuator("My buzzer", BusType::PWM, 23);
```

**`addDevice` signature:**
```cpp
int addDevice(DeviceCategory category, const std::string& metadata, BusType bus, int pin);
```

| Parameter  | Options                                          |
| ---------- | ------------------------------------------------ |
| `category` | `DeviceCategory::Sensor`, `DeviceCategory::Actuator` |
| `metadata` | Any human-readable label — the agent uses this   |
| `bus`      | `BusType::Digital`, `BusType::Analog`, `BusType::PWM`, `BusType::I2C`, `BusType::SPI`, `BusType::IR` |
| `pin`      | GPIO pin number                                  |

**SPI devices — custom pin overload:**

For SPI devices, use the overloaded `addDevice` with four explicit pin numbers instead of a single GPIO. SPI requires four wires: clock, data in, data out, and a per-device chip select.

```cpp
// Signature: addDevice(category, metadata, bus, sck, miso, mosi, cs)
asha.addDevice(DeviceCategory::Sensor, "Daddy's RFID RC522", BusType::SPI,
               25, 33, 27, 26);  // sck=25, miso=33, mosi=27, cs=26
```

| Parameter | Description |
| --------- | ----------- |
| `sck`     | SPI clock — driven by the ESP32 |
| `miso`    | Master-In-Slave-Out: data flowing from device → ESP32 |
| `mosi`    | Master-Out-Slave-In: data flowing from ESP32 → device |
| `cs`      | Chip Select — pulled LOW by the ESP32 during a transaction; one line per device |

> **GPIO 34 and 35 are input-only on ESP32** — never use them for MOSI or CS (both are outputs). Safe alternatives: GPIO 26, 27, 32, 33.

The first SPI device registered calls `SPI.begin()` with its pins. Multiple SPI devices share the same SCK/MISO/MOSI bus but each gets its own CS line.

The `sck`, `miso`, `mosi`, and `cs` values are persisted to MongoDB and returned by the backend's `get_user_projects_and_devices` endpoint — the agent reads them from there rather than guessing.

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

### SPI Read

```json
{
  "action": "spi_read",
  "cs_pin": 26,
  "speed": 1000000,
  "mode": 0,
  "data": [183, 0]
}
```

SPI is full-duplex — every byte you transmit, a byte comes back simultaneously. The `data` array is what the ESP32 sends; the device clocks back one response byte per sent byte.

For reads, the first byte is the register address with the device's read bit set; remaining bytes are dummy `0x00` to clock the response out.

**Read/write bit convention varies by device — always check the datasheet:**

| Device | Convention |
| ------ | ---------- |
| RC522 RFID | Bit 7 = `1` means read. Register `0x37` → send `0xB7` (`0x37 \| 0x80`) |
| Most ADC chips | Bit 7 = `0` means read |
| Some devices | Separate command byte triggers the read entirely |

**RC522 wiring sanity check:** send `[0xB7, 0x00]`. Response byte 2 should be `0x91` (v1.0) or `0x92` (v2.0). Any other value means a wiring problem.

`spi_read` is for one-off reads. For continuous polling (e.g. RFID card detection), use `asha.spiTransfer()` in a Lua script — bytes return directly to Lua without an MQTT round-trip.

### IR Send (Universal Raw Mode)

IR is implemented as a **raw timing transmitter** — the agent sends pulse durations in microseconds directly, with no protocol-specific knowledge required on the device. This means any IR device (any brand, any remote) works without firmware changes, as long as the agent has the raw timings.

The ESP32 uses its RMT (Remote Control Transceiver) hardware peripheral to reproduce the timings precisely. Each `IRSender` object is created once during `init()` per registered IR pin and stored for the lifetime of the program — no re-initialization on every command.

```json
{
  "action": "ir_send",
  "pin": 4,
  "freq": 38,
  "timings": [4500, 4500, 560, 1690, 560, 1690, 560, 560]
}
```

| Field | Description |
| ----- | ----------- |
| `pin` | GPIO pin the IR LED is connected to — must be registered with `BusType::IR` |
| `freq` | Carrier frequency in kHz — 38 is standard for most consumer IR remotes |
| `timings` | Array of pulse durations in microseconds — alternating on/off. Odd indices = LED on, even indices = LED off |

**Where to get timings:** IR code databases like [IRDB](https://github.com/probonopd/irdb) contain raw timings for thousands of devices. Alternatively, point the original remote at an IR receiver module connected to the ESP32 and record the raw signal.

**Why raw instead of protocol-specific methods?**

The IRremoteESP8266 library has dozens of protocol methods (`sendSAMSUNG`, `sendNEC`, `sendSONY`, etc.). Wrapping each one would require firmware updates every time a new brand is added. Raw mode means the firmware never needs to change — the agent carries the timing data and the device is a universal transmitter.

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

### Stop PWM

```json
{ "pin": 21, "action": "stop_pwm" }
```

Detaches the LEDC peripheral from the pin and drives it LOW. Use this to fully silence a PWM device (buzzer, motor, fan). Setting `duty: 0` reduces output but the LEDC hardware signal keeps running — `stop_pwm` is the correct way to stop. Works in direct commands, batch sequences, and from Lua via `asha.command('{"pin": 21, "action": "stop_pwm"}')`.

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

| Lua call                       | What it does                                        |
| ------------------------------ | --------------------------------------------------- |
| `asha.command(jsonStr)`        | Runs any command JSON through `handleCommand`       |
| `asha.digitalRead(pin)`        | Returns 0 or 1                                      |
| `asha.analogRead(pin)`         | Returns 0–4095 (ADC pins GPIO 32–39 only)           |
| `asha.ledcRead(channel)`       | Returns current PWM duty (0=off, >0=on, -1=uninit)  |
| `asha.sleep(ms)`               | Non-blocking task delay; also the script kill point |
| `asha.subscribe(topic)`        | Subscribe to an extra MQTT topic from Lua           |
| `asha.readMessage(topic)`      | Returns last message on a subscribed topic          |
| `asha.spiTransfer(cs_pin, {bytes})` | Sends bytes over SPI, returns table of received bytes (one per sent byte) |
| `asha.publish(topic, message)` | Publish a string to an MQTT topic (thread-safe)     |
| `print(...)`                   | Prints to Serial                                    |
| `millis()`                     | Returns uptime in milliseconds                      |

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
  asha.sleep(10)
end
```

#### SPI device polling — RFID card detection

For SPI sensors, use `asha.spiTransfer()` in a Lua script to poll continuously. It sends a byte array over SPI and returns the response bytes directly to Lua — no MQTT round-trip, no async callback.

```lua
local rx = asha.spiTransfer(cs_pin, {byte1, byte2, ...})
-- rx[1] is the byte received while sending byte1
-- rx[2] is the byte received while sending byte2
-- ...and so on
```

**Critical — always debounce with a state variable.** Without it, the script publishes on every loop tick while the card is held on the reader, flooding the agent with hundreds of duplicate messages.

```lua
local cs = 26       -- cs_pin from get_user_projects_and_devices()
local light = 18    -- actuator pin
local card_present = false

while true do
    -- Replace with the card-presence detection bytes for your device.
    -- For RC522: search the datasheet for REQA/ComIrqReg sequence, or use web search.
    local rx = asha.spiTransfer(cs, {<detection bytes>})
    local detected = <your condition on rx>

    if detected and not card_present then
        card_present = true    -- card just ARRIVED → fire exactly once
        asha.command('{"pin":' .. light .. ',"action":"digital","value":1}')
        asha.publish("asha/ashaSensor/YOUR_ASHA_ID", "Someone scanned an RFID card — light turned on")
    elseif not detected and card_present then
        card_present = false   -- card just LEFT → reset state
        asha.command('{"pin":' .. light .. ',"action":"digital","value":0}')
        asha.publish("asha/ashaSensor/YOUR_ASHA_ID", "RFID card removed — light off")
    end

    asha.sleep(200)
end
```

The `card_present` flag makes the notification fire exactly once when the card arrives, and once when it leaves. The same pattern applies to any SPI sensor that can hold state across multiple loop ticks — proximity sensors, door sensors, stable thresholds.

**Why not `spi_read` via MQTT for this?** `handleCommand` publishes the raw bytes back to MQTT but returns nothing to Lua. The script would have to subscribe and wait for the response — adding latency and complexity. `spiTransfer` returns bytes directly onto the Lua stack, so the script can react inline without leaving the loop.

#### Publishing sensor data back to the backend

`asha.publish(topic, message)` lets a Lua script push data upstream over MQTT without the agent needing to poll. The call is thread-safe — it queues the message on Core 0 and Core 1 drains it in `run()` before calling `mqttClient.publish()`.

Convention: publish to `asha/sensorData/<ashaID>`. The backend subscribes to `asha/sensorData/+` (wildcard) and routes incoming data to the correct user by looking up the phone number tied to the ashaID.

Change-detection pattern — only publish when the value actually changes:

```lua
local prev = -1
while true do
  local val = asha.analogRead(34)
  if val ~= prev then
    asha.publish("asha/sensorData/YOUR_ASHA_ID", tostring(val))
    prev = val
  end
  asha.sleep(100)
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

- **`while` loops without `asha.sleep()` will crash the device.** The watchdog timer reboots the ESP32 if the OS scheduler is starved. Every loop must call `asha.sleep()`. Minimum: `asha.sleep(10)`.

### Multiple concurrent real-time tasks — FIXED

The device can run as many concurrent real-time conditions as resources allow. You are not limited to one behavior at a time.

**How it works:** the agent merges all active conditions into a single Lua script and sends it as a replacement. The firmware stops the running script cleanly at the next `asha.sleep()` call and immediately runs the merged replacement. From the user's perspective, all behaviors are running simultaneously.

**Example — button + touch sensor + vision, all active at once:**

```lua
asha.subscribe('asha/asha_vision/abc123')
while true do
  local btn     = asha.digitalRead(21)
  local touch   = asha.analogRead(34)
  local detected = asha.readMessage('asha/asha_vision/abc123')

  if btn == 1 then
    asha.command('{"pin": 18, "action": "digital", "value": 1}')
  end
  if touch > 3000 then
    asha.command('{"pin": 25, "action": "digital", "value": 1}')
  end
  if detected == 'person' then
    asha.command('{"pin": 23, "action": "digital", "value": 1}')
  end
  asha.sleep(10)
end
```

**How the kill mechanism works:**

Three approaches were considered when a new script arrives while a loop is running:

1. **Spawn a new Lua process per script** — each VM takes ~40 KB of heap. Multiple scripts would exhaust RAM quickly. Rejected.
2. **Context-switch between scripts rapidly** — not feasible without a custom scheduler, and doesn't solve shared hardware race conditions. Rejected.
3. **Agent merges conditions, firmware kills and replaces** — one Lua VM runs at all times. When a new script arrives, a `stopLuaRequested` flag is set. The next `asha.sleep()` call detects it, throws a Lua error to unwind the running loop, and `luaTask` picks up the merged replacement. **Chosen approach.**

Benefits:
- Fixed memory cost regardless of how many conditions are active
- No hardware race conditions — one script owns all pins at all times
- `asha.sleep()` serves double duty: feeds the watchdog AND is the safe kill checkpoint
- **`analogRead` only works on GPIO 32–39.** Calling it on a digital-only pin returns garbage.
- **`digitalRead` on a PWM pin is only reliable at full on (100% duty) or full off (0%).** Intermediate brightness gives inconsistent readings.
- **`analogRead` cannot be used on an active PWM pin.** Calling it detaches the pin from the LEDC peripheral, turning the device off.
- **Lua scripts do not survive reboots.** Scripts are stored only in RAM. On reboot, all scripts are lost and must be resent by the agent. (NVS or LittleFS persistence is planned.)
- **MQTT topic is public.** The broker allows anyone to subscribe to `asha/commands/<id>`. A hashed/encrypted device ID is planned.
- **HTTPS without a pinned certificate fails** against strict servers. A proper TLS solution is needed before production.
- **Race condition on shared pins.** If the agent sends both a direct MQTT command and a Lua script targeting the same pin simultaneously, both cores call `handleCommand` at the same time — undefined hardware behavior.

---

## Build Notes

### ESP32 Arduino Core 3.x Migration (pioarduino)

The official PlatformIO `espressif32` platform is frozen on Arduino Core 2.x and will not receive Core 3.x support. To use Core 3.x, the project uses the community-maintained **pioarduino** fork:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
```

**Why Core 3.x?** The LEDC (PWM) API changed completely. Core 2.x required explicit channel numbers that had to be tracked manually — two devices sharing a channel would conflict silently. Core 3.x manages channels internally; you only deal with pin numbers.

| Core 2.x (old) | Core 3.x (new) |
|---|---|
| `ledcSetup(channel, freq, res)` | removed |
| `ledcAttachPin(pin, channel)` | removed |
| `ledcWrite(channel, duty)` | `ledcWrite(pin, duty)` |
| `ledcRead(channel)` | `ledcRead(pin)` |
| `ledcDetachPin(pin)` | `ledcDetach(pin)` |
| — | `ledcAttach(pin, freq, res)` |

### pioarduino Python Dependencies

On first build, pioarduino tries to install its own Python dependencies into PlatformIO's virtual environment using `uv`. If the build fails with `Failed to install Python dependencies`, run this manually:

```
C:\Users\<you>\.platformio\penv\Scripts\pip.exe install certifi uv pioarduino littlefs-python fatfs-ng pyyaml rich-click zopfli intelhex rich cryptography ecdsa bitstring "reedsolo>=1.5.3,<1.8" esp-idf-size esp-coredump pyelftools
```

### Esp32Lua — Duplicate `main()` Fix

The Esp32Lua library bundles the full Lua source tree, including `lua.c` (the Lua REPL) and `luac.c` (the Lua bytecode compiler). Both define a `main()` function because they are meant to be standalone desktop tools, not embedded components. On Core 2.x / GCC 8, the linker silently ignored the duplicate. On Core 3.x / GCC 14, duplicate symbol definitions are a hard linker error.

**Fix:** `filter_lua.py` (in the project root) is a PlatformIO pre-build script that renames these two files to `.disabled` before the compiler sees them. It is registered in `platformio.ini`:

```ini
extra_scripts = pre:filter_lua.py
```

The `pre:` prefix is critical — without it the script runs after PlatformIO has already registered the files in its build graph, causing a SCons error even though the files were renamed.

If you ever run `pio pkg update` or reinstall Esp32Lua, the files will be restored and the script will disable them again on the next build automatically.

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
