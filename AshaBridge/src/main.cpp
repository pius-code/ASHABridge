#include <ASHA.h>
#include <Arduino.h>
ASHA asha;
ASHA_Actuators ashaActuators;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
    pinMode(2, OUTPUT);
    digitalWrite(2, 1);
}

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Sensor, "My water sensor", BusType::Analog), 34);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "My light", BusType::PWM), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "My buzzer", BusType::PWM), 23);

    asha.init("YOUR_ASHA_ID");
}
void loop() {
    asha.run();
}
