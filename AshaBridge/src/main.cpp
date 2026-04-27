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
    asha.asha_devices.addDevice(ashaActuators.LED("test LED", "digital"), 18);
    asha.asha_devices.addDevice(ashaActuators.LED("test LED2", "analog"), 19);
    asha.init("17fdab2c-1140-40e9-9572-16f802eb3b5e");
}

void loop() {
    // put your main code here, to run repeatedly:
}
