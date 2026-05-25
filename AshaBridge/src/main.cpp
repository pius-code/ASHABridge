#include <ASHA.h>
#include <Arduino.h>
ASHA asha;
ASHA_Actuators ashaActuators;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
    pinMode(2, OUTPUT);
    digitalWrite(2, 1);
}

const char* ssid = "ashapius";
const char* password = "piuspiusy";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "My Sumsung TV controller", BusType::IR), 19);
    asha.init("299c90fc-7724-48f5-b94c-8398f2bab1f9");
}
void loop() {
    asha.run();
}
