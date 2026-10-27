#include <ASHA.h>
#include <Arduino.h>

ASHA asha;

void afterConnection() {
    Serial.println("ASHA: Internet Connected");
}

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
    // 1. Connect to WiFi with optional post-connection callback
    asha.connectWiFi(ssid, password, afterConnection);

    // 2. Register devices (category, description, bus type, pin)
    asha.addDevice(DeviceCategory::Actuator, "Bedroom light", BusType::Digital, 2);
    asha.addDevice(DeviceCategory::Actuator, "Hall fan", BusType::PWM, 4);
    asha.addDevice(DeviceCategory::Actuator, "Front door servo", BusType::PWM, 5);
    asha.addDevice(DeviceCategory::Sensor, "Porch light sensor", BusType::Analog, 34);

    // 3. Connect to cloud backend and MQTT broker
    asha.init("My Home Project");
}

void loop() {
    // 4. Handle MQTT keepalive, watchdog, and dynamic Lua tasks
    asha.run();
}

