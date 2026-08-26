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
        asha.genericDev(DeviceCategory::Actuator, "Bedroom light", BusType::Digital), 2);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Hall's fan", BusType::PWM), 4);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Front door 90° open 0° close", BusType::PWM), 5);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "side door 90° open 0° close", BusType::PWM), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator,
                        "buzzer - you can use as security alarm or door bell", BusType::PWM),
        19);
    asha.init("Pius House");
}

void loop() {
    asha.run();
}
