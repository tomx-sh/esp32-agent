#include <Arduino.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-S3 project ready");
}

void loop() {
  Serial.println("Hello from ESP32-S3");
  delay(1000);
}
