#include "ASHA.h"

#include <ArduinoJson.h>
#include <WiFi.h>

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

DeviceType ASHA_Actuators::LED(const std::string& metadata, const std::string& DorA) {
    DeviceType dt;
    dt.category = "Actuator";
    dt.type = "LED";
    dt.metadata = metadata;
    dt.DorA = DorA;
    return dt;
}

std::string ASHA::init(const std::string& ashaID) {
    JsonDocument doc;

    doc["auth_id"] = ashaID;
    JsonArray jsonDevices = doc["devices"].to<JsonArray>();

    for (int i = 0; i < asha_devices.getCount(); ++i) {
        RegisteredDevice rd = asha_devices.getDevice(i);

        JsonObject deviceObj = jsonDevices.add<JsonObject>();
        deviceObj["id"] = rd.id;
        deviceObj["pin"] = rd.pin;
        deviceObj["category"] = rd.deviceType.category;
        deviceObj["type"] = rd.deviceType.type;
        deviceObj["metadata"] = rd.deviceType.metadata;
        deviceObj["signal"] = rd.deviceType.DorA;
    }
    std::string payload;
    serializeJson(doc, payload);

    Serial.println("--- Generated ASHA Payload ---");
    Serial.println(payload.c_str());
    Serial.println("------------------------------");
    return payload;
}