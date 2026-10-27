#include <ASHA.h>
#include <Arduino.h>
ASHA asha;

void afterConnection() {
    Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "ashapius";
const char* password = "piuspiusz";

void setup() {
    asha.connectWiFi(ssid, password, afterConnection);

    asha.addDevice(DeviceCategory::Actuator, "Bedroom light", BusType::Digital, 2);
    asha.addDevice(DeviceCategory::Actuator, "Hall's fan", BusType::PWM, 4);
    asha.addDevice(DeviceCategory::Actuator, "Front door 90° open 0° close", BusType::PWM, 5);
    asha.addDevice(DeviceCategory::Actuator, "side door 90° open 0° close", BusType::PWM, 18);
    asha.addDevice(DeviceCategory::Actuator,
                   "buzzer - you can use as security alarm or door bell", BusType::PWM, 19);
    asha.init("Pius House");
}

void loop() {
    asha.run();
}
