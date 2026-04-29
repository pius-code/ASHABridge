#include "ASHA.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>

// WIFI
void ASHA_WIFI::onConnect(void (*userdefinedfunc)()) {
    userdefinedfunLoc = userdefinedfunc;
}

void ASHA_WIFI::begin(const char* ssid, const char* password) {
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
    dt.type = DeviceComponent::LED;
    dt.metadata = metadata;
    dt.bus = bus;
    return dt;
}

DeviceType ASHA::genericDev(DeviceCategory deviceCategory, DeviceComponent deviceComponent,
                            const std::string& metadata, BusType busType) {
    DeviceType dt;
    dt.category = deviceCategory;
    dt.type = deviceComponent;
    dt.metadata = metadata;
    dt.bus = busType;
    return dt;
}

std::string ASHA::init(const std::string& ashaID) {
    // sending the payload to the cloud.
    JsonDocument doc;

    doc["auth_id"] = ashaID;
    JsonArray jsonDevices = doc["devices"].to<JsonArray>();

    bool i2cInitialized = false;
    bool spiInitialized = false;
    for (int i = 0; i < asha_devices.getCount(); ++i) {
        RegisteredDevice rd = asha_devices.getDevice(i);

        JsonObject deviceObj = jsonDevices.add<JsonObject>();
        deviceObj["device_id"] = rd.id;
        deviceObj["pin"] = rd.pin;
        deviceObj["category"] = rd.deviceType.category;
        deviceObj["type"] = rd.deviceType.type;
        deviceObj["metadata"] = rd.deviceType.metadata;
        deviceObj["bus"] = rd.deviceType.bus;

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
            "http://230d-154-161-30-38.ngrok-free.app/api/v1/asha/verify_and_register_device");

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

    Serial.println("ASHA: Setting up MQTT connection to HiveMQ...");

    mqttClient.setClient(espClient);
    mqttClient.setServer("broker.hivemq.com", 1883);
    mqttClient.setCallback(mqttCallback);
    currentAshaID = ashaID;

    return payload;
}

void ASHA::mqttCallback(char* topic, byte* payload, unsigned int length) {
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
        int pin = doc["pin"];
        String action = doc["action"];
        int value = doc["value"];

        if (action == "digital") {
            if (value == -1) {
                int reading = digitalRead(pin);
                Serial.printf("Read Pin %d: Digital %d\n", pin, reading);
            } else {
                digitalWrite(pin, value);
                Serial.printf("Set Pin %d to Digital %d\n", pin, value);
            }
        } else if (action == "pwm") {
            int channel = doc["channel"];
            int freq = doc["freq"];
            int duty = doc["duty"];

            ledcSetup(channel, freq, 16);
            ledcAttachPin(pin, channel);
            ledcWrite(channel, duty);
            Serial.printf("Set Pin %d to PWM ch:%d freq:%d duty:%d\n", pin, channel, freq, duty);
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
}

void ASHA::reconnectMQTT() {
    while (!mqttClient.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ASHA_B_DEVICE-";
        clientId += String(random(0xffff), HEX);
        if (mqttClient.connect(clientId.c_str())) {
            Serial.println("connected to MQTT Broker!");
            String topic = "asha/commands/" + String(currentAshaID.c_str());
            mqttClient.subscribe(topic.c_str());
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void ASHA::run() {
    if (!mqttClient.connected()) {
        reconnectMQTT();
    }
    mqttClient.loop();
}
