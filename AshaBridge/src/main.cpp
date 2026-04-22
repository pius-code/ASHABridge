#include <Arduino.h>
#include <ASHA.h>

ASHA_WIFI ashaWifi;
ASHA_Devices ashaDevices;
ASHA_Actuators ashaActuators;


void afterConnection() {
  Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "Piusxel";
const char* password = "voed5056";




void setup() {
ashaWifi.onConnect(afterConnection);
ashaWifi.begin(ssid,password);
ashaDevices.addDevice(ashaActuators.LED("test LED","digital"),12);
}

void loop() {
  // put your main code here, to run repeatedly:
}


