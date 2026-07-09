#include <ASHA.h>
#include <Arduino.h>
ASHA asha;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
    pinMode(23, OUTPUT);
    digitalWrite(23, 1);
    delay(100);
    digitalWrite(23, 0);
}

const char* ssid = "ashapius";
const char* password = "piuspiusz";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Red Light", BusType::Digital), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Green Light", BusType::Digital), 19);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Blue Light", BusType::Digital), 21);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Alarm Buzzer", BusType::PWM), 15);
    asha.asha_devices.addDevice(asha.genericDev(DeviceCategory::Actuator, "My fan", BusType::PWM),
                                4);

    asha.init("17fdab2c-1140-40e9-9572-16f802eb3b5e");
}

void loop() {
    asha.run();
}
