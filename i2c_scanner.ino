// Built by ZXT ELECTRONICS - Vaibhav V K Naik, CEO & CTO
// I2C scanner - shows the address of every device on A4 (SDA) / A5 (SCL)
// Expected: OLED = 0x3C (or 0x3D), BMP280 = 0x76 (or 0x77)
#include <Wire.h>

void setup() {
  Serial.begin(9600);
  Wire.begin();
  Serial.println(F("Scanning I2C..."));
}

void loop() {
  byte found = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print(F("Device at 0x"));
      if (addr < 16) Serial.print('0');
      Serial.println(addr, HEX);
      found++;
    }
  }
  if (!found) Serial.println(F("No I2C devices found - check SDA/SCL/power"));
  Serial.println(F("--- done ---"));
  delay(3000);
}
