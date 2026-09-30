/*
  Built by ZXT ELECTRONICS - Vaibhav V K Naik, CEO & CTO

  Balloon LoRa Weather Station - RECEIVER (ground station)
  Board : Arduino Uno/Nano
  Parts : BMP280, Ra-02 (SX1278, 433 MHz), 0.96" SSD1306 OLED, push button

  Compares balloon (TX) data with ground (RX) data and shows balloon height
  above the ground station. Falls back to ground-only data if no balloon signal.
  Button (D3 to GND): short press = reconnect LoRa, long press = sleep / wake
*/

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h>

#define LORA_SS    10
#define LORA_RST    9
#define LORA_DIO0   2
#define LORA_FREQ  433E6

#define BTN_PIN         3        // button between D3 and GND
#define LONG_PRESS_MS   1000UL   // hold this long = sleep / wake
#define DEBOUNCE_MS     40UL

#define OLED_ADDRESS   0x3C      // try 0x3D if blank
#define TX_TIMEOUT_MS  6000UL    // balloon counts as "lost" after this
#define CSV_OUTPUT     0         // set to 1 to also print a CSV line per packet

// Sea-level pressure in hPa (set to your local value for accurate absolute altitude)
#define SEA_LEVEL_HPA  1013.25

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Adafruit_BMP280 bmp;

// Balloon (TX) data
float txT = 0, txH = -1, txP = 0, txAlt = 0;
unsigned long txPkt = 0;
int   rssi = 0;
float snr = 0;
bool  haveRx = false;
unsigned long lastRxMs = 0;

// Ground (RX) data
float rxT = 0, rxP = 0, rxAlt = 0;
unsigned long lastLocalMs = 0;

// State
bool loraOk = false;     // LoRa module connected
bool asleep = false;     // sleep mode
bool loraEverBegun = false;

bool txValid() {
  return loraOk && haveRx && (millis() - lastRxMs <= TX_TIMEOUT_MS);
}

float altFromPressure(float p) {
  if (p <= 0) return 0;
  return 44330.0 * (1.0 - pow(p / SEA_LEVEL_HPA, 0.1903));
}

// ---------- helpers ----------
void showMsg(const __FlashStringHelper *line1, const __FlashStringHelper *line2 = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  if (line2) {
    display.setCursor(0, 34);
    display.println(line2);
  }
  display.display();
}

void bmpNormal() {
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                  Adafruit_BMP280::SAMPLING_X2,
                  Adafruit_BMP280::SAMPLING_X16,
                  Adafruit_BMP280::FILTER_X16,
                  Adafruit_BMP280::STANDBY_MS_500);
}

// (Re)connect to the LoRa module. Returns true on success.
bool connectLoRa() {
  Serial.println(F("Connecting to LoRa..."));
  showMsg(F("Connecting LoRa..."));

  if (loraEverBegun) LoRa.end();          // clean restart
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

  loraOk = true;
  Serial.println(F("LoRa connected - waiting for balloon packets..."));
  showMsg(F("LoRa connected!"));
  delay(700);
  return true;
}

void goToSleep() {
  Serial.println(F("[SLEEP] Display, LoRa and BMP280 off. Long press to wake."));
  showMsg(F("Sleeping..."), F("Long press = wake"));
  delay(800);
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (loraOk) LoRa.sleep();
  bmp.setSampling(Adafruit_BMP280::MODE_SLEEP);
  asleep = true;
}

void wakeUp() {
  Serial.println(F("[WAKE] Waking up..."));
  asleep = false;
  display.ssd1306_command(SSD1306_DISPLAYON);
  bmpNormal();
  showMsg(F("Waking up..."));
  delay(300);
  connectLoRa();
  readLocal();
  drawScreen();
}

// ---------- button ----------
void onShortPress() {
  if (asleep) return;                     // ignore short press while sleeping
  Serial.println(F("[BUTTON] Short press - reconnecting LoRa"));
  connectLoRa();
  readLocal();
  drawScreen();
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
    if (stable == LOW) {                  // pressed
      pressStart = millis();
      longDone = false;
    } else {                              // released
      if (!longDone) onShortPress();
    }
  }

  // long press triggers while still held
  if (stable == LOW && !longDone && millis() - pressStart >= LONG_PRESS_MS) {
    longDone = true;
    onLongPress();
  }
}

// ---------- setup / loop ----------
void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);
  Serial.println(F("Balloon ground station starting..."));

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("OLED not found!"));
    while (true);
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Balloon ground stn"));
  display.display();

  if (!bmp.begin(0x76) && !bmp.begin(0x77)) {
    Serial.println(F("BMP280 not found!"));
    display.println(F("BMP280 ERROR"));
    display.display();
    while (true);
  }
  bmpNormal();

  connectLoRa();          // does not halt if it fails; short press retries

#if CSV_OUTPUT
  Serial.println(F("pkt,txT,rxT,txH,txP,rxP,txAlt,rxAlt,height_diff,rssi,snr"));
#endif
  readLocal();
  drawScreen();
}

void loop() {
  handleButton();
  if (asleep) return;     // sleeping: only watch the button

  // ---- balloon packet ----
  if (loraOk) {
    int size = LoRa.parsePacket();
    if (size) {
      char buf[40];
      int n = 0;
      while (LoRa.available() && n < (int)sizeof(buf) - 1) buf[n++] = (char)LoRa.read();
      buf[n] = 0;

      char *a = strtok(buf, ",");
      char *b = strtok(NULL, ",");
      char *c = strtok(NULL, ",");
      char *d = strtok(NULL, ",");

      if (a && b && c && d) {
        txT   = atof(a);
        txH   = atof(b);
        txP   = atof(c);
        txPkt = atol(d);
        txAlt = altFromPressure(txP);
        rssi  = LoRa.packetRssi();
        snr   = LoRa.packetSnr();
        haveRx = true;
        lastRxMs = millis();

        readLocal();
        printSerial();
        drawScreen();
      }
    }
  }

  // ---- every 2 s: refresh ground data + screen ----
  if (millis() - lastLocalMs >= 2000) {
    readLocal();
    if (!txValid()) printGroundOnly();
    drawScreen();
  }
}

void readLocal() {
  rxT   = bmp.readTemperature();
  rxP   = bmp.readPressure() / 100.0F;
  rxAlt = altFromPressure(rxP);
  lastLocalMs = millis();
}

// ---------- Serial: ground data only ----------
void printGroundOnly() {
  Serial.print(F("[GROUND ONLY] "));
  if (!loraOk) {
    Serial.print(F("LoRa offline | "));
  } else if (haveRx) {
    Serial.print(F("Balloon lost ("));
    Serial.print((millis() - lastRxMs) / 1000);
    Serial.print(F("s ago, last #"));
    Serial.print(txPkt);
    Serial.print(F(") | "));
  } else {
    Serial.print(F("No balloon packet yet | "));
  }
  Serial.print(F("Temp: "));
  Serial.print(rxT, 1);
  Serial.print(F(" C | Pressure: "));
  Serial.print(rxP, 1);
  Serial.print(F(" hPa | Altitude: "));
  Serial.print(rxAlt, 1);
  Serial.println(F(" m"));
}

// ---------- Serial: full comparison ----------
void printSerial() {
  Serial.println();
  Serial.print(F("===== Packet #")); Serial.print(txPkt); Serial.println(F(" ====="));
  Serial.println(F("                  TX(balloon)   RX(ground)   Diff(TX-RX)"));

  Serial.print(F("Temperature C     "));
  Serial.print(txT, 1);  Serial.print(F("          "));
  Serial.print(rxT, 1);  Serial.print(F("         "));
  Serial.println(txT - rxT, 1);

  Serial.print(F("Humidity %        "));
  if (txH >= 0) Serial.print(txH, 0); else Serial.print(F("--"));
  Serial.println(F("            n/a          --"));

  Serial.print(F("Pressure hPa      "));
  Serial.print(txP, 1);  Serial.print(F("        "));
  Serial.print(rxP, 1);  Serial.print(F("       "));
  Serial.println(txP - rxP, 1);

  Serial.print(F("Altitude m        "));
  Serial.print(txAlt, 1);  Serial.print(F("        "));
  Serial.print(rxAlt, 1);  Serial.print(F("       "));
  Serial.println(txAlt - rxAlt, 1);

  Serial.print(F("Balloon height above ground station: "));
  Serial.print(txAlt - rxAlt, 1);
  Serial.println(F(" m"));

  Serial.print(F("Signal: RSSI "));
  Serial.print(rssi);
  Serial.print(F(" dBm, SNR "));
  Serial.print(snr, 1);
  Serial.println(F(" dB"));

#if CSV_OUTPUT
  Serial.print(txPkt);  Serial.print(',');
  Serial.print(txT, 1); Serial.print(',');
  Serial.print(rxT, 1); Serial.print(',');
  Serial.print(txH, 0); Serial.print(',');
  Serial.print(txP, 1); Serial.print(',');
  Serial.print(rxP, 1); Serial.print(',');
  Serial.print(txAlt, 1); Serial.print(',');
  Serial.print(rxAlt, 1); Serial.print(',');
  Serial.print(txAlt - rxAlt, 1); Serial.print(',');
  Serial.print(rssi);   Serial.print(',');
  Serial.println(snr, 1);
#endif
}

// ---------- OLED ----------
void drawScreen() {
  bool v = txValid();

  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(24, 0);  display.print(F("TX"));
  display.setCursor(62, 0);  display.print(F("RX"));
  display.setCursor(98, 0);  display.print(F("Diff"));

  // Temperature
  display.setCursor(0, 9);   display.print(F("T C"));
  display.setCursor(24, 9);
  if (v) display.print(txT, 1); else display.print(F("--"));
  display.setCursor(62, 9);  display.print(rxT, 1);
  display.setCursor(98, 9);
  if (v) display.print(txT - rxT, 1); else display.print(F("--"));

  // Humidity (balloon only)
  display.setCursor(0, 18);  display.print(F("H %"));
  display.setCursor(24, 18);
  if (v && txH >= 0) display.print(txH, 0); else display.print(F("--"));
  display.setCursor(62, 18); display.print(F("n/a"));
  display.setCursor(98, 18); display.print(F("--"));

  // Pressure
  display.setCursor(0, 27);  display.print(F("P h"));
  display.setCursor(24, 27);
  if (v) display.print(txP, 1); else display.print(F("--"));
  display.setCursor(62, 27); display.print(rxP, 1);
  display.setCursor(98, 27);
  if (v) display.print(txP - rxP, 1); else display.print(F("--"));

  // Altitude
  display.setCursor(0, 36);  display.print(F("Alt m"));
  display.setCursor(30, 36);
  if (v) display.print(txAlt, 0); else display.print(F("--"));
  display.setCursor(66, 36); display.print(rxAlt, 0);
  display.setCursor(98, 36);
  if (v) display.print(txAlt - rxAlt, 0); else display.print(F("--"));

  // Link status
  display.setCursor(0, 45);
  if (!loraOk) {
    display.print(F("LoRa offline"));
  } else if (v) {
    display.print(F("RSSI"));  display.print(rssi);
    display.print(F(" SNR"));  display.print(snr, 1);
  } else {
    display.print(F("Ground data only"));
  }

  display.setCursor(0, 54);
  if (!loraOk) {
    display.print(F("Short press=retry"));
  } else if (!haveRx) {
    display.print(F("Waiting for TX..."));
  } else if (v) {
    display.print(F("Pkt #"));
    display.print(txPkt);
  } else {
    display.print(F("Lost "));
    display.print((millis() - lastRxMs) / 1000);
    display.print(F("s, last #"));
    display.print(txPkt);
  }

  display.display();
}
