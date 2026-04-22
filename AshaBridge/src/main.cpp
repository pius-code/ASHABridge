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
    asha.asha_devices.addDevice(ashaActuators.LED("test LED", "digital"), 12);
    asha.init("318f8vnie84su4jso3jfjw7");
}

void loop() {
    // put your main code here, to run repeatedly:
}
