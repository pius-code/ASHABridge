#include "ASHA.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Lua.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>

#include <lua/lua.hpp>

#include "lwip/dns.h"

ASHA* ASHA::instance = nullptr;

// * ============================================================
// * LUA API — functions exposed to Lua scripts via asha.*
// * ============================================================

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
    int pin = luaL_checkinteger(L, 1);
    int duty = ledcRead(pin);
    lua_pushinteger(L, duty);
    return 1;
}

int lua_sleep(lua_State* L) {
    int ms = luaL_checkinteger(L, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    if (ASHA::getInstance()->getStopLuaRequested()) {
        luaL_error(L, "asha agent stopped current script");
    }
    return 0;
}

int lua_subscribe(lua_State* L) {
    const char* topic = luaL_checkstring(L, 1);
    ASHA::getInstance()->subscribeTopic(topic);
    return 0;
}

int lua_publish(lua_State* L) {
    const char* topic = luaL_checkstring(L, 1);
    const char* message = luaL_checkstring(L, 2);
    PublishMessage msg;
    msg.topic = strdup(topic);
    msg.message = strdup(message);
    ASHA::getInstance()->publishLuaMessage(msg);
    return 0;
}

int lua_getVar(lua_State* L) {
    const char* topic = luaL_checkstring(L, 1);
    const std::string message = ASHA::getInstance()->gettopicPayload(topic);
    lua_pushstring(L, message.c_str());
    return 1;
}

// sends a table of bytes over SPI and returns a table of bytes received
// usage: local rx = asha.spiTransfer(cs_pin, {byte1, byte2, ...})
int lua_spiTransfer(lua_State* L) {
    int cs_pin = luaL_checkinteger(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    int len = lua_rawlen(L, 2);

    SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_pin, LOW);

    lua_newtable(L);
    for (int i = 1; i <= len; i++) {
        lua_rawgeti(L, 2, i);
        byte tx = (byte)lua_tointeger(L, -1);
        lua_pop(L, 1);
        byte rx = SPI.transfer(tx);
        lua_pushinteger(L, rx);
        lua_rawseti(L, -2, i);
    }

    digitalWrite(cs_pin, HIGH);
    SPI.endTransaction();

    return 1;
}

static const luaL_Reg ashalib[] = {{"command", lua_ashaCommand},
                                   {"analogRead", lua_analogRead},
                                   {"digitalRead", lua_digitalRead},
                                   {"ledcRead", lua_ledcRead},
                                   {"sleep", lua_sleep},
                                   {"subscribe", lua_subscribe},
                                   {"readMessage", lua_getVar},
                                   {"publish", lua_publish},
                                   {"spiTransfer", lua_spiTransfer},
                                   {nullptr, nullptr}};

int luaopen_asha(lua_State* L) {
    luaL_newlib(L, ashalib);
    return 1;
}

// * ============================================================
// * MQTT SUBSCRIPTION — queued from Lua (Core 0), drained by run() on Core 1
// * ============================================================
void ASHA::subscribeTopic(const char* topic) {
    char* copy = strdup(topic);
    xQueueSend(pendingSubscriptions, &copy, 0);
}

void ASHA::publishLuaMessage(PublishMessage publishMessage) {
    xQueueSend(pendingPublish, &publishMessage, 0);
}

// * ============================================================
// * WIFI
// * ============================================================
void ASHA_WIFI::onConnect(void (*userdefinedfunc)()) {
    userdefinedfunLoc = userdefinedfunc;
}

void ASHA_WIFI::begin(const char* ssid, const char* password) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.begin(115200);
    Serial.print("Connecting");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi");

    // Set DNS directly in lwIP — WiFi.config() doesn't reliably propagate DNS
    ip_addr_t dns1, dns2;
    IP_ADDR4(&dns1, 8, 8, 8, 8);
    IP_ADDR4(&dns2, 8, 8, 4, 4);
    dns_setserver(0, &dns1);
    dns_setserver(1, &dns2);

    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("DNS: ");
    Serial.println(WiFi.dnsIP());

    if (userdefinedfunLoc != nullptr) {
        userdefinedfunLoc();
    }
}

// * ============================================================
// * DEVICES — registry and actuator factory
// * ============================================================
int ASHA_Devices::addDevice(const DeviceType& deviceType, int pin) {
    if (count >= MAX_DEVICES) {
        return -1;  // this flag signifies that its full
    }

    int newId = count + 1;
    devices[count] = {newId, deviceType, pin, {}};
    count++;

    return newId;
}

int ASHA_Devices::addDevice(const DeviceType& deviceType, int sck, int miso, int mosi, int cs) {
    if (count >= MAX_DEVICES) return -1;

    int newId = count + 1;
    SPIPins pins;
    pins.sck = sck;
    pins.miso = miso;
    pins.mosi = mosi;
    pins.cs = cs;
    devices[count] = {newId, deviceType, 0, pins};
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

// * ============================================================
// * INIT — register devices with cloud, connect MQTT, start Lua task
// * ============================================================
std::string ASHA::init(const std::string& projectName) {
    instance = this;
    JsonDocument doc;

    doc["project_name"] = projectName;
    doc["mac_address"] = WiFi.macAddress().c_str();
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
                deviceObj["sck"] = rd.spiPins.sck;
                deviceObj["miso"] = rd.spiPins.miso;
                deviceObj["mosi"] = rd.spiPins.mosi;
                deviceObj["cs"] = rd.spiPins.cs;
                break;
            case BusType::IR:
                deviceObj["bus"] = "IR";
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
                SPI.begin(rd.spiPins.sck, rd.spiPins.miso, rd.spiPins.mosi, rd.spiPins.cs);
                spiInitialized = true;
            }
            pinMode(rd.spiPins.cs, OUTPUT);
            digitalWrite(rd.spiPins.cs, HIGH);
        } else if (rd.deviceType.bus == BusType::IR) {
            wirelessSenders[rd.pin] = new IRSender(rd.pin);
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
        WiFiClientSecure client;
        client.setInsecure();

        Serial.println("ASHA: Payload ready to be sent to the cloud.");
        http.begin(client,
                   "https://anywhere-encyclopedia-intention-grade.trycloudflare.com/api/v1/asha/"
                   "verify_and_register_device");

        http.addHeader("Content-Type", "application/json");

        int httpResponseCode = http.POST(String(payload.c_str()));

        if (httpResponseCode > 0) {
            Serial.print("HTTP Response code: ");
            Serial.println(httpResponseCode);
            String response = http.getString();
            JsonDocument doc;
            deserializeJson(doc, response);
            currentAshaID = doc["asha_id"].as<std::string>();
            const char* code = doc["pairing_code"];
            Serial.println("ASHA: Pairing code: ");
            Serial.println(code);
        } else {
            Serial.print("Error sending request. Error code: ");
            Serial.println(httpResponseCode);
        }

        http.end();

    } else {
        Serial.println("ASHA: Not connected to WiFi. Cannot send payload.");
    }
    Serial.println("ASHA: Setting up MQTT connection ...");

    espClient.setInsecure();
    mqttClient.setClient(espClient);
    mqttClient.setServer("d1fb95ffc6654d6e98effc66d26fed74.s1.eu.hivemq.cloud", 8883);
    mqttClient.setBufferSize(16 * 1024);
    mqttClient.setCallback(mqttCallback);

    // LUA -------------------------------------------------------------------------------
    ashaLua.addModule("asha", luaopen_asha);
    luaScriptQueue = xQueueCreate(5, sizeof(char*));
    pendingPublish = xQueueCreate(5, sizeof(PublishMessage));
    pendingSubscriptions = xQueueCreate(10, sizeof(char*));

    xTaskCreatePinnedToCore(luaTask, "luaTask", 10 * 1024, this, 1, nullptr, 0);
    // ------------------------------------------------------------------------------------

    return payload;
}

// * ============================================================
// * IR SENDER — universal raw timing transmitter via ESP32 RMT peripheral
// * ============================================================
void IRSender::send(JsonVariant doc) {
    JsonArray timing = doc["timing"];
    int freq = doc["freq"];
    uint16_t buf[timing.size()];
    for (int i = 0; i < timing.size(); i++) {
        buf[i] = timing[i];
    }

    int size = timing.size();
    irsend.sendRaw(buf, size, freq);
}
// * ============================================================
// * COMMAND HANDLER — executes all MQTT commands (digital, pwm, i2c, spi, uart, ir, batch)
// * ============================================================
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
        int value = doc["value"] | 0;

        if (value == -1) {
            // -1 for read
            int ledc_duty = ledcRead(pin);
            Serial.printf("Read PWM pin:%d ledc_duty:%d\n", pin, ledc_duty);

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
            ledcAttach(pin, freq, 13);
            ledcWrite(pin, duty >> 3);
            Serial.printf("Set Pin %d to PWM freq:%d duty:%d\n", pin, freq, duty);
        }
    } else if (action == "analog") {
        int reading = analogRead(pin);
        Serial.printf("Read Pin %d: Analog %d\n", pin, reading);

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
        Wire.beginTransmission(addr);
        Wire.write(reg);
        Wire.endTransmission(false);
        Wire.requestFrom(addr, len);
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
    } else if (action == "spi_read") {
        int cs_pin = doc["cs_pin"];
        long speed = doc["speed"] | 1000000;
        int spi_mode = doc["mode"] | SPI_MODE0;
        JsonArray data = doc["data"];

        pinMode(cs_pin, OUTPUT);
        SPI.beginTransaction(SPISettings(speed, MSBFIRST, spi_mode));
        digitalWrite(cs_pin, LOW);

        String result = "SPI read → cs:" + String(cs_pin) + " rx:[";
        bool first = true;
        for (byte b : data) {
            byte rx = SPI.transfer(b);
            if (!first) result += ",";
            result += String(rx);
            first = false;
        }
        result += "]";

        digitalWrite(cs_pin, HIGH);
        SPI.endTransaction();

        Serial.println(result);
    } else if (action == "uart_write") {
        int baud = doc["baud"] | 9600;
        int tx_pin = doc["tx_pin"] | 17;
        int rx_pin = doc["rx_pin"] | 16;
        String data = doc["data"];

        Serial2.begin(baud, SERIAL_8N1, rx_pin, tx_pin);
        Serial2.print(data);

        Serial.printf("UART write → baud:%d data:%s\n", baud, data.c_str());
    } else if (action == "ir_send") {
        int pin = doc["pin"];
        instance->wirelessSenders[pin]->send(doc);
    } else if (action == "stop_pwm") {
        int pin = doc["pin"];
        ledcDetach(pin);
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
}

// * ============================================================
// * MQTT CALLBACK — routes incoming messages: commands path or topicPayloads store
// * ============================================================
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
    std::string topicStr(topic);
    if (topicStr != "asha/commands/" + instance->currentAshaID) {
        instance->topicPayloads[topicStr] = message;
        return;
    }
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
            Serial.println("Received Lua script:");
            Serial.println(script.c_str());
            char* copy = strdup(script.c_str());
            instance->stopLuaRequested = true;
            char* old;
            while (xQueueReceive(instance->luaScriptQueue, &old, 0) == pdTRUE) {
                free(old);
            }

            xQueueSend(instance->luaScriptQueue, &copy, 0);

        } else {
            handleCommand(doc.as<JsonVariant>());
        }
    }
}

// * ============================================================
// * LUA TASK — runs on Core 0, sleeps until a script arrives via queue
// * ============================================================
void ASHA::luaTask(void* param) {
    ASHA* self = (ASHA*)param;
    while (true) {
        char* script;
        if (xQueueReceive(self->luaScriptQueue, &script, portMAX_DELAY) == pdTRUE) {
            self->stopLuaRequested = false;
            self->ashaLua.run(script);
            free(script);
        }
    }
}

// * ============================================================
// * MQTT RECONNECT — throttled to one attempt every 5 seconds
// * ============================================================
void ASHA::reconnectMQTT() {
    unsigned long now = millis();
    if (now - lastReconnectAttempt < 5000) return;
    lastReconnectAttempt = now;

    Serial.print("Attempting MQTT connection...");
    String clientId = "ASHA_B_DEVICE-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), "ashaESP", "ashatheworldtothefuture")) {
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

// * ============================================================
// * RUN — called every loop() tick: WiFi watchdog, MQTT reconnect, pending subscriptions
// * ============================================================
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
    if (mqttClient.connected()) {
        char* subTopic;
        while (xQueueReceive(pendingSubscriptions, &subTopic, 0) == pdTRUE) {
            mqttClient.subscribe(subTopic);
            free(subTopic);
        }
        PublishMessage publishMessage;
        while (xQueueReceive(pendingPublish, &publishMessage, 0) == pdTRUE) {
            mqttClient.publish(publishMessage.topic, publishMessage.message);
            free(publishMessage.topic);
            free(publishMessage.message);
        }
    }

    mqttClient.loop();
}
