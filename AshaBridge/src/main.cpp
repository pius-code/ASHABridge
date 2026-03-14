#include <Arduino.h>
#include <ASHA.h>

ASHA_WIFI ashaWifi;


void afterConnection() {
  Serial.println("ASHA: Internet Connected ");
}

const char* ssid = "Tecnocx";
const char* password = "voed5055";
void setup() {
ashaWifi.onConnect(afterConnection);
ashaWifi.begin(ssid,password);
}

void loop() {
  // put your main code here, to run repeatedly:
}


