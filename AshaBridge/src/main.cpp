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
    asha.asha_devices.addDevice(asha.genericDev(DeviceCategory::Actuator, DeviceComponent::LED,
                                                "My test LCD", BusType::PWM),
                                18);
}

void loop() {
    asha.run();
}
