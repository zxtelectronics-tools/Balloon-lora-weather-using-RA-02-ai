/*
  Built by ZXT ELECTRONICS - Vaibhav V K Naik, CEO & CTO

  Balloon LoRa Weather Station - TRANSMITTER (balloon)
  Board : Arduino Uno/Nano
  Parts : BMP280, DHT11, Ra-02 (SX1278, 433 MHz), push button, optional OLED

  Packet format sent: temp,humidity,pressure,packetNumber
  Button (D3 to GND): short press = reconnect LoRa, long press = sleep / wake
*/

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>

#define USE_OLED   0        // set to 1 if the transmitter also has the 0.96" OLED

#if USE_OLED
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #define OLED_ADDRESS 0x3C
  Adafruit_SSD1306 display(128, 64, &Wire, -1);
#endif

#define LORA_SS    10
#define LORA_RST    9
#define LORA_DIO0   2
#define LORA_FREQ  433E6

#define DHT_PIN     4
#define DHT_TYPE    DHT11

#define BTN_PIN         3        // button between D3 and GND
#define LONG_PRESS_MS   1000UL   // hold this long = sleep / wake
#define DEBOUNCE_MS     40UL

#define SEND_INTERVAL_MS  3000UL // keep >= 2000 for the DHT11
#define SEA_LEVEL_HPA     1013.25

Adafruit_BMP280 bmp;
DHT dht(DHT_PIN, DHT_TYPE);

// State
bool loraOk = false;
bool asleep = false;
bool loraEverBegun = false;
unsigned long packetNo = 0;
unsigned long lastSendMs = 0;

// Latest readings
float t = 0, h = -1, p = 0, alt = 0;

// ---------- OLED helpers ----------
void showMsg(const __FlashStringHelper *line1, const __FlashStringHelper *line2 = nullptr) {
#if USE_OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  if (line2) {
    display.setCursor(0, 34);
    display.println(line2);
  }
  display.display();
#endif
}

void drawScreen() {
#if USE_OLED
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);   display.print(F("BALLOON TX"));

  display.setCursor(0, 11);  display.print(F("Temp  : "));
  display.print(t, 1);       display.print(F(" C"));

  display.setCursor(0, 20);  display.print(F("Humid : "));
  if (h >= 0) { display.print(h, 0); display.print(F(" %")); }
  else display.print(F("ERR"));

  display.setCursor(0, 29);  display.print(F("Press : "));
  display.print(p, 1);       display.print(F(" hPa"));

  display.setCursor(0, 38);  display.print(F("Alt   : "));
  display.print(alt, 0);     display.print(F(" m"));

  display.setCursor(0, 47);
  if (loraOk) display.print(F("LoRa OK"));
  else        display.print(F("LoRa OFFLINE (tap)"));

  display.setCursor(0, 56);
  display.print(F("Sent #"));
  display.print(packetNo);

  display.display();
#endif
}

// ---------- sensors ----------
void bmpNormal() {
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);
}

void readSensors() {
  t   = bmp.readTemperature();
  p   = bmp.readPressure() / 100.0F;
  alt = bmp.readAltitude(SEA_LEVEL_HPA);
  float hh = dht.readHumidity();
  h = isnan(hh) ? -1 : hh;
}

// ---------- LoRa ----------
bool connectLoRa() {
  Serial.println(F("Connecting to LoRa..."));
  showMsg(F("Connecting LoRa..."));

  if (loraEverBegun) LoRa.end();
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  loraEverBegun = true;

  if (!LoRa.begin(LORA_FREQ)) {
    loraOk = false;
    Serial.println(F("LoRa NOT found - check wiring/power. Short press to retry."));
    showMsg(F("LoRa NOT found"), F("Short press = retry"));
    delay(1000);
    return false;
  }

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.setTxPower(17);

  loraOk = true;
  Serial.println(F("LoRa connected - transmitting."));
  showMsg(F("LoRa connected!"));
  delay(700);
  return true;
}

// ---------- sleep / wake ----------
void goToSleep() {
  Serial.println(F("[SLEEP] Transmitter paused. Long press to wake."));
  showMsg(F("Sleeping..."), F("Long press = wake"));
  delay(800);
#if USE_OLED
  display.ssd1306_command(SSD1306_DISPLAYOFF);
#endif
  if (loraOk) LoRa.sleep();
  bmp.setSampling(Adafruit_BMP280::MODE_SLEEP);
  asleep = true;
}

void wakeUp() {
  Serial.println(F("[WAKE] Waking up..."));
  asleep = false;
#if USE_OLED
  display.ssd1306_command(SSD1306_DISPLAYON);
#endif
  bmpNormal();
  showMsg(F("Waking up..."));
  delay(300);
  connectLoRa();
  lastSendMs = millis() - SEND_INTERVAL_MS;   // send right away
}

// ---------- button ----------
void onShortPress() {
  if (asleep) return;
  Serial.println(F("[BUTTON] Short press - reconnecting LoRa"));
  connectLoRa();
  lastSendMs = millis() - SEND_INTERVAL_MS;   // send right away
}

void onLongPress() {
  Serial.println(F("[BUTTON] Long press"));
  if (asleep) wakeUp(); else goToSleep();
}

void handleButton() {
  static bool lastRaw = HIGH;
  static bool stable = HIGH;
  static unsigned long lastChange = 0, pressStart = 0;
  static bool longDone = false;

  bool raw = digitalRead(BTN_PIN);
  if (raw != lastRaw) {
    lastRaw = raw;
    lastChange = millis();
  }

  if (millis() - lastChange > DEBOUNCE_MS && raw != stable) {
    stable = raw;
    if (stable == LOW) {
      pressStart = millis();
      longDone = false;
    } else {
      if (!longDone) onShortPress();
    }
  }

  if (stable == LOW && !longDone && millis() - pressStart >= LONG_PRESS_MS) {
    longDone = true;
    onLongPress();
  }
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.println(F("Balloon transmitter starting..."));

  dht.begin();

#if USE_OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("OLED not found!"));
    while (true);
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Balloon TX"));
  display.display();
#endif

  if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
    Serial.println(F("BMP280 not found! Check wiring."));
    showMsg(F("BMP280 ERROR"));
    while (true);
  }
  bmpNormal();

  connectLoRa();          // does not halt if it fails; short press retries

  Serial.println(F("Ready."));
  lastSendMs = millis() - SEND_INTERVAL_MS;   // first packet immediately
}

void loop() {
  handleButton();
  if (asleep) return;

  if (millis() - lastSendMs < SEND_INTERVAL_MS) return;
  lastSendMs = millis();

  readSensors();

  if (loraOk) {
    // Packet format: temp,humidity,pressure,packetNumber
    LoRa.beginPacket();
    LoRa.print(t, 1);  LoRa.print(',');
    LoRa.print(h, 0);  LoRa.print(',');
    LoRa.print(p, 1);  LoRa.print(',');
    LoRa.print(packetNo);
    LoRa.endPacket();
    Serial.print(F("Sent #"));
    Serial.print(packetNo);
  } else {
    Serial.print(F("[LOCAL ONLY - LoRa offline] #"));
    Serial.print(packetNo);
  }

  Serial.print(F("  T=")); Serial.print(t, 1);
  Serial.print(F(" C  H="));
  if (h >= 0) Serial.print(h, 0); else Serial.print(F("ERR"));
  Serial.print(F(" %  P=")); Serial.print(p, 1);
  Serial.print(F(" hPa  Alt=")); Serial.print(alt, 0);
  Serial.println(F(" m"));

  drawScreen();
  if (loraOk) packetNo++;
}
