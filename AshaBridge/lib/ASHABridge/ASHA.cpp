#include "ASHA.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Lua.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

#include <lua/lua.hpp>

ASHA* ASHA::instance = nullptr;

// Lua_asha
//---------------------------------------------------------------------------------------

static int lua_ashaCommand(lua_State* L) {
    const char* raw_command = luaL_checkstring(L, 1);
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, raw_command);
    // TODO: the error here fails silenty, print it out so that we know whats wrong
    if (!error) {
        String action = doc["action"];

        if (action == "batch") {
            JsonArray commands = doc["commands"];
            for (JsonVariant cmd : commands) {
                ASHA::handleCommand(cmd);
            }
        } else {
            ASHA::handleCommand(doc.as<JsonVariant>());
        }
    }
    return 0;
}

static int lua_digitalWrite(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    int value = luaL_checkinteger(L, 2);
    digitalWrite(pin, value);
    return 0;
}

int lua_digitalRead(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    int val = digitalRead(pin);
    lua_pushinteger(L, val);
    return 1;
}

int lua_delay(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    delay(ms);
    return 0;
}

int lua_analogRead(lua_State* L) {
    int pin = luaL_checkinteger(L, 1);
    int val = analogRead(pin);
    lua_pushinteger(L, val);
    return 1;
}

int lua_ledcRead(lua_State* L) {
    int channel = luaL_checkinteger(L, 1);
    int duty = ledcRead(channel);
    lua_pushinteger(L, duty);
    return 1;
}

int lua_sleep(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return 0;
}

static const luaL_Reg ashalib[] = {
    {"command", lua_ashaCommand}, {"analogRead", lua_analogRead}, {"digitalRead", lua_digitalRead},
    {"ledcRead", lua_ledcRead},   {"sleep", lua_sleep},           {nullptr, nullptr}};

int luaopen_asha(lua_State* L) {
    luaL_newlib(L, ashalib);
    return 1;
}

//--------------------------------------------------------------------------------------

// WIFI
void ASHA_WIFI::onConnect(void (*userdefinedfunc)()) {
    userdefinedfunLoc = userdefinedfunc;
}

void ASHA_WIFI::begin(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    IPAddress primaryDNS(8, 8, 8, 8);
    IPAddress secondaryDNS(8, 8, 4, 4);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, primaryDNS, secondaryDNS);

    WiFi.begin(ssid, password);
    Serial.begin(115200);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");

    if (userdefinedfunLoc != nullptr) {
        userdefinedfunLoc();
    }
}

// devices is the name of the array that holds all the registered devices.
int ASHA_Devices::addDevice(const DeviceType& deviceType, int pin) {
    if (count >= MAX_DEVICES) {
        return -1;  // this flag signifies that its full
    }

    int newId = count + 1;
    devices[count] = {newId, deviceType, pin};
    count++;

    return newId;
}

DeviceType ASHA_Actuators::LED(const std::string& metadata, BusType bus) {
    DeviceType dt;
    dt.category = DeviceCategory::Actuator;
    dt.metadata = metadata;
    dt.bus = bus;
    return dt;
}

DeviceType ASHA::genericDev(DeviceCategory deviceCategory, const std::string& metadata,
                            BusType busType) {
    DeviceType dt;
    dt.category = deviceCategory;
    dt.metadata = metadata;
    dt.bus = busType;
    return dt;
}

std::string ASHA::init(const std::string& ashaID) {
    instance = this;
    JsonDocument doc;

    doc["auth_id"] = ashaID;
    JsonArray jsonDevices = doc["devices"].to<JsonArray>();

    bool i2cInitialized = false;
    bool spiInitialized = false;
    for (int i = 0; i < asha_devices.getCount(); ++i) {
        RegisteredDevice rd = asha_devices.getDevice(i);

        JsonVariant deviceObj = jsonDevices.add<JsonVariant>();
        deviceObj["device_id"] = rd.id;
        deviceObj["pin"] = rd.pin;

        if (rd.deviceType.category == DeviceCategory::Actuator) {
            deviceObj["category"] = "Actuator";
        } else {
            deviceObj["category"] = "Sensor";
        }

        deviceObj["metadata"] = rd.deviceType.metadata;

        // 2. Convert Bus to String
        switch (rd.deviceType.bus) {
            case BusType::Digital:
                deviceObj["bus"] = "Digital";
                break;
            case BusType::Analog:
                deviceObj["bus"] = "Analog";
                break;
            case BusType::PWM:
                deviceObj["bus"] = "PWM";
                break;
            case BusType::I2C:
                deviceObj["bus"] = "I2C";
                break;
            case BusType::SPI:
                deviceObj["bus"] = "SPI";
                break;
        }

        // hardware init
        if (rd.deviceType.bus == BusType::Digital) {
            if (rd.deviceType.category == DeviceCategory::Actuator) {
                pinMode(rd.pin, OUTPUT);
            } else {
                pinMode(rd.pin, INPUT);
            }
        } else if (rd.deviceType.bus == BusType::I2C) {
            if (!i2cInitialized) {
                Wire.begin();
                i2cInitialized = true;
            }
        } else if (rd.deviceType.bus == BusType::SPI) {
            if (!spiInitialized) {
                SPI.begin();
                spiInitialized = true;
            }
            pinMode(rd.pin, OUTPUT);
            digitalWrite(rd.pin, HIGH);
        }
    }

    // sending payload
    std::string payload;
    serializeJson(doc, payload);

    Serial.println("--- Generated ASHA Payload ---");
    Serial.println(payload.c_str());
    Serial.println("------------------------------");

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        WiFiClient client;

        Serial.println("ASHA: Payload ready to be sent to the cloud.");
        http.begin(
            client,
            "http://aa39-154-161-58-101.ngrok-free.app/api/v1/asha/verify_and_register_device");

        http.addHeader("Content-Type", "application/json");
        http.addHeader("ngrok-skip-browser-warning", "69420");

        int httpResponseCode = http.POST(String(payload.c_str()));

        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String response = http.getString();
            Serial.println(response);
        } else {
            Serial.print("Error sending request. Error code: ");
            Serial.println(httpResponseCode);
        }

        http.end();

    } else {
        Serial.println("ASHA: Not connected to WiFi. Cannot send payload.");
    }
    Serial.println("ASHA: Setting up MQTT connection ...");

    mqttClient.setClient(espClient);
    mqttClient.setServer("10.63.64.41", 1883);
    mqttClient.setBufferSize(16 * 1024);
    mqttClient.setCallback(mqttCallback);
    currentAshaID = ashaID;

    // LUA -------------------------------------------------------------------------------
    ashaLua.addModule("asha", luaopen_asha);
    luaScriptQueue = xQueueCreate(5, sizeof(char*));

    xTaskCreatePinnedToCore(luaTask, "luaTask", 10 * 1024, this, 1, nullptr, 0);
    // ------------------------------------------------------------------------------------

    return payload;
}

void ASHA::handleCommand(JsonVariant doc) {
    if (doc.containsKey("delay_ms")) {
        int delayTime = doc["delay_ms"];
        unsigned long start = millis();
        while (millis() - start < delayTime) {
            delay(10);
            if (instance) instance->mqttClient.loop();
        }
        Serial.printf("Delayed for %d ms\n", delayTime);
        return;
    }
    int pin = doc["pin"];
    String action = doc["action"];
    int value = doc["value"];

    if (action == "digital") {
        if (value == -1) {
            int reading = digitalRead(pin);
            Serial.printf("Read Pin %d: Digital %d\n", pin, reading);
            const char* corrID = doc["correlation_id"] | "";
            if (strlen(corrID) > 0) {
                JsonDocument response;
                response["correlation_id"] = corrID;
                response["pin"] = pin;
                response["value"] = reading;

                String topic = "asha/response/" + String(instance->currentAshaID.c_str());
                String payload;
                serializeJson(response, payload);
                instance->mqttClient.publish(topic.c_str(), payload.c_str());
            }
        } else {
            digitalWrite(pin, value);
            Serial.printf("Set Pin %d to Digital %d\n", pin, value);
        }
    } else if (action == "pwm") {
        int channel = doc["channel"];
        int value = doc["value"] | 0;

        if (value == -1) {
            int ledc_duty = ledcRead(channel);
            Serial.printf("Read PWM ch:%d ledc_duty:%d\n", channel, ledc_duty);

            const char* corrID = doc["correlation_id"] | "";
            if (strlen(corrID) > 0) {
                JsonDocument response;
                response["correlation_id"] = corrID;
                response["pin"] = pin;
                response["ledc_duty"] = ledc_duty << 3;
                String topic = "asha/response/" + String(instance->currentAshaID.c_str());
                String payload;
                serializeJson(response, payload);
                instance->mqttClient.publish(topic.c_str(), payload.c_str());
            }
        } else {
            int freq = doc["freq"];
            int duty = doc["duty"];
            ledcSetup(channel, freq, 13);
            ledcAttachPin(pin, channel);
            ledcWrite(channel, duty >> 3);
            Serial.printf("Set Pin %d to PWM ch:%d freq:%d duty:%d\n", pin, channel, freq, duty);
        }
    } else if (action == "i2c_write") {
        int addr = doc["addr"];
        int reg = doc["reg"];
        JsonArray data = doc["data"];

        Wire.beginTransmission(addr);
        Wire.write(reg);
        for (byte b : data) {
            Wire.write(b);
        }
        byte error = Wire.endTransmission();

        if (error == 0) {
            Serial.printf("I2C write success → addr:0x%02X reg:0x%02X\n", addr, reg);
        } else {
            Serial.printf("I2C write failed → addr:0x%02X error:%d\n", addr, error);
        }
    } else if (action == "i2c_read") {
        int addr = doc["addr"];
        int reg = doc["reg"];
        int len = doc["len"];

        // Step 1: tell device which register to read from
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.endTransmission(false);  // false = don't release the bus yet

        // Step 2: request bytes back
        Wire.requestFrom(addr, len);

        // Step 3: collect the bytes
        String result = "I2C read → addr:0x" + String(addr, HEX) + " data:[";
        for (int i = 0; i < len; i++) {
            if (Wire.available()) {
                byte b = Wire.read();
                result += String(b);
                if (i < len - 1) result += ",";
            }
        }
        result += "]";
        Serial.println(result);

        // later this becomes mqttClient.publish("asha/response", result.c_str());
    } else if (action == "spi_write") {
        int cs_pin = doc["cs_pin"];
        long speed = doc["speed"] | 1000000;
        int spi_mode = doc["mode"] | SPI_MODE0;
        JsonArray data = doc["data"];

        pinMode(cs_pin, OUTPUT);
        SPI.beginTransaction(SPISettings(speed, MSBFIRST, spi_mode));
        digitalWrite(cs_pin, LOW);
        for (byte b : data) {
            SPI.transfer(b);
        }
        digitalWrite(cs_pin, HIGH);
        SPI.endTransaction();

        Serial.printf("SPI write success → cs_pin:%d\n", cs_pin);
    } else if (action == "uart_write") {
        int baud = doc["baud"] | 9600;
        int tx_pin = doc["tx_pin"] | 17;
        int rx_pin = doc["rx_pin"] | 16;
        String data = doc["data"];

        Serial2.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
        Serial2.print(data);

        Serial.printf("UART write → baud:%d data:%s\n", baud, data.c_str());
    }
}

void ASHA::mqttCallback(char* topic, byte* payload, unsigned int length) {
    instance->lastMqttActivity = millis();
    Serial.print("Message arrived on topic: ");
    Serial.println(topic);

    std::string message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.print("Payload: ");
    Serial.println(message.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);

    if (!error) {
        String action = doc["action"];

        if (action == "batch") {
            JsonArray commands = doc["commands"];
            for (JsonVariant cmd : commands) {
                handleCommand(cmd);
            }
        } else if (action == "lua") {
            std::string script = doc["script"];
            char* copy = strdup(script.c_str());
            xQueueSend(instance->luaScriptQueue, &copy, 0);
        } else {
            handleCommand(doc.as<JsonVariant>());
        }
    }
}

void ASHA::luaTask(void* param) {
    ASHA* self = (ASHA*)param;
    while (true) {
        char* script;
        if (xQueueReceive(self->luaScriptQueue, &script, portMAX_DELAY) == pdTRUE) {
            self->ashaLua.run(script);
            free(script);
        }
    }
}

void ASHA::reconnectMQTT() {
    unsigned long now = millis();
    if (now - lastReconnectAttempt < 5000) return;
    lastReconnectAttempt = now;

    Serial.print("Attempting MQTT connection...");
    String clientId = "ASHA_B_DEVICE-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), "Piusasha", "Piuspius27")) {
        Serial.println("connected to MQTT Broker!");
        lastMqttActivity = millis();
        String topic = "asha/commands/" + String(currentAshaID.c_str());
        mqttClient.subscribe(topic.c_str());
    } else {
        Serial.print("failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" will retry in 5 seconds");
    }
}

void ASHA::run() {
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.reconnect();
        Serial.println("ASHA: WiFi down");
        return;
    }
    if (mqttClient.connected() && (millis() - lastMqttActivity > 1800000)) {
        Serial.println("ASHA: MQTT watchdog — forcing reconnect");
        mqttClient.disconnect();
        lastReconnectAttempt = 0;
    }
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();
}
