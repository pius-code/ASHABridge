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
        asha.genericDev(DeviceCategory::Sensor, "Cinderella's water sensor", BusType::Analog), 34);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Cinderella's Light", BusType::PWM), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Cinderella's buzzer", BusType::PWM), 23);

    asha.init("dc9e148e-e97d-4d55-839a-4ff927a87b41");
}

void loop() {
    asha.run();
}
