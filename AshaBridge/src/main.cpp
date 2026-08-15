#include <ASHA.h>
#include <Arduino.h>
ASHA asha;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "ashapius";
const char* password = "piuspiusz";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Dad's red light", BusType::Digital), 23);

    asha.init("My Dad's house");
}

void loop() {
    asha.run();
}
