// #include <ASHA.h>
#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
// ASHA asha;
// ASHA_Actuators ashaActuators;

#define IR_PIN 19

// void afterConnection() {
//     Serial.println("ASHA: Internet Connected ");
//     pinMode(2, OUTPUT);
//     digitalWrite(2, 1);
// }

// const char* ssid = "ashapius";
// const char* password = "piuspiusy";
IRsend irsend(IR_PIN);

void setup() {
    // asha.asha_wifi.onConnect(afterConnection);
    // asha.asha_wifi.begin(ssid, password);
    // asha.asha_devices.addDevice(
    //     asha.genericDev(DeviceCategory::Actuator, "My House light", BusType::Digital), 21);
    irsend.begin();
    // asha.init("299c90fc-7724-48f5-b94c-8398f2bab1f9");
}
void loop() {
    irsend.sendSAMSUNG(0xE0E040BF, 32);
    delay(3000);
    // asha.run();
}
