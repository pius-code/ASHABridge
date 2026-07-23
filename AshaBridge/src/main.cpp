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
        asha.genericDev(DeviceCategory::Actuator, "Red Light", BusType::Digital), 18);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Green Light", BusType::Digital), 19);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Blue Light", BusType::Digital), 21);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "Alarm Buzzer", BusType::PWM), 15);
    asha.asha_devices.addDevice(asha.genericDev(DeviceCategory::Actuator, "My fan", BusType::PWM),
                                4);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Actuator, "My Door. open_angle: 90 closed_angle: 0",
                        BusType::PWM),
        23);
    asha.asha_devices.addDevice(
        asha.genericDev(DeviceCategory::Sensor, "Daddy's bathroom tap sensor", BusType::Analog),
        32);

    asha.init("957aab4d-fba2-421e-94b9-0191459fa408");
}

void loop() {
    asha.run();
}
