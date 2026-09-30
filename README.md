# Balloon LoRa Weather Station

**Built by ZXT ELECTRONICS** - Vaibhav V K Naik, CEO & CTO

A two-part weather balloon telemetry system using Arduino Uno/Nano and Ra-02 (SX1278, 433 MHz) LoRa modules.

- **Transmitter (balloon):** BMP280 + DHT11 + Ra-02 (optional OLED)
- **Receiver (ground station):** BMP280 + Ra-02 + 0.96" SSD1306 OLED

The receiver compares the balloon's readings with its own ground readings (temperature, pressure, altitude) and shows the balloon's height above the ground station. Both boards have a button for reconnecting LoRa and for sleep / wake.

## Features

- Temperature, humidity and pressure sent over LoRa every 3 seconds
- Side-by-side TX vs RX comparison with a Diff column on the OLED and Serial Monitor
- Altitude for both balloon and ground, plus balloon height above the ground station
- Falls back to ground-only data when the balloon signal is lost
- RSSI / SNR link quality display
- Button on both boards: short press reconnects LoRa, long press sleeps or wakes
- Optional CSV output for logging to a spreadsheet

## Repository layout

- `transmitter/transmitter.ino` : balloon firmware
- `receiver/receiver.ino` : ground station firmware
- `tools/i2c_scanner/i2c_scanner.ino` : finds I2C addresses of the OLED and BMP280

## Libraries (Arduino Library Manager)

| Library | Transmitter | Receiver |
|---|---|---|
| LoRa (Sandeep Mistry) | yes | yes |
| Adafruit BMP280 Library | yes | yes |
| Adafruit Unified Sensor | yes | yes |
| DHT sensor library (Adafruit) | yes | no |
| Adafruit SSD1306 | optional | yes |
| Adafruit GFX Library | optional | yes |

## Wiring

The Ra-02 is **3.3V only**. Do not connect it to 5V.

### Ra-02 (both boards)

| Ra-02 | Arduino Uno/Nano |
|---|---|
| 3.3V | 3.3V |
| GND | GND |
| SCK | D13 |
| MISO | D12 |
| MOSI | D11 |
| NSS | D10 |
| RST | D9 |
| DIO0 | D2 |

### I2C devices (BMP280, OLED)

| Pin | Arduino |
|---|---|
| SDA | A4 |
| SCL | A5 |
| VCC | 3.3V |
| GND | GND |

BMP280 is usually at 0x76 or 0x77 and the OLED at 0x3C or 0x3D. They share the same two I2C wires.

### Transmitter only

| Part | Connection |
|---|---|
| DHT11 DATA | D4 (bare 4-pin DHT11 needs a 10k resistor between VCC and DATA) |
| Button | D3 to GND |

### Receiver only

| Part | Connection |
|---|---|
| Button | D3 to GND |

## LoRa settings (must match on both boards)

- Frequency: 433 MHz
- Spreading factor: 7
- Bandwidth: 125 kHz
- Coding rate: 4/5
- Sync word: 0x12

## Packet format

Plain CSV text: `temp,humidity,pressure,packetNumber`

Example: `23.5,55,1012.3,17`

A humidity of `-1` means the DHT11 read failed.

## Button behaviour (both boards)

- **Short press:** restart the LoRa module and try to connect
- **Long press (1 second):** sleep, turning off the display, LoRa and BMP280. Long press again to wake and reconnect.

This is a soft sleep. The Arduino chip itself keeps running.

## Settings you may want to change

Receiver and transmitter:
- `SEA_LEVEL_HPA` : your local sea-level pressure for accurate absolute altitude (the TX-RX height difference is accurate either way)
- `LONG_PRESS_MS` : how long to hold for sleep / wake

Receiver:
- `OLED_ADDRESS` : 0x3C or 0x3D
- `TX_TIMEOUT_MS` : how long without a packet before the balloon is shown as lost (keep it above twice the send interval)
- `CSV_OUTPUT` : set to 1 for a CSV line per packet

Transmitter:
- `SEND_INTERVAL_MS` : time between packets (keep it at 2000 or more for the DHT11)
- `USE_OLED` : set to 1 if the transmitter also has an OLED

## Usage

1. Install the libraries above.
2. Upload `transmitter/transmitter.ino` to the balloon board.
3. Upload `receiver/receiver.ino` to the ground board.
4. Open the Serial Monitor at **9600 baud** on either board.

## Troubleshooting

- **Blank OLED, nothing in Serial Monitor:** check the baud rate (9600) and port, then run `tools/i2c_scanner` to confirm the OLED and BMP280 addresses.
- **"LoRa NOT found":** check the 3.3V supply and SPI wiring, then short press the button to retry.
- **No packets received:** check that both boards use the same frequency and sync word, and that the antennas are attached.
- **Random resets or odd behaviour:** the Uno has only 2 KB of RAM and the OLED uses 1 KB of it. Check the memory figure when compiling.
- **Humidity shows ERR or --:** the DHT11 read failed. Check its wiring and pull-up resistor.

## License

MIT

## Credits

Built by **ZXT ELECTRONICS** - Vaibhav V K Naik, CEO & CTO
