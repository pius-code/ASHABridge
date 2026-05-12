#include <ASHA.h>
#include <Arduino.h>
ASHA asha;
ASHA_Actuators ashaActuators;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "ashapius";
const char* password = "piuspiusy";

void setup() {
    asha.asha_wifi.onConnect(afterConnection);
    asha.asha_wifi.begin(ssid, password);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Green LED", BusType::PWM), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "RED LED", BusType::Digital), 19);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "alarm buzzer", BusType::PWM), 21);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "in built ping LED", BusType::Digital), 2);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Pius room light", BusType::PWM), 32);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Pius room light", BusType::Digital), 25);

    digitalWrite(25, 1);
    digitalWrite(32, 1);
    digitalWrite(2, 1);

    asha.init("17fdab2c-1140-40e9-9572-16f802eb3b5e");
}

void loop() {
    asha.run();
}
