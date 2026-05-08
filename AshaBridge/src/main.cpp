#include <ASHA.h>
#include <Arduino.h>
ASHA asha;
ASHA_Actuators ashaActuators;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "Piusxel";
const char* password = "voed5056";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Green LED", BusType::PWM), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "RED LED", BusType::Digital), 19);
    asha.init("17fdab2c-1140-40e9-9572-16f802eb3b5e");

    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("Chip model: %s\n", ESP.getChipModel());
    Serial.printf("Flash size: %d MB\n", ESP.getFlashChipSize() / 1024 / 1024);
}

void loop() {
    asha.run();
}
