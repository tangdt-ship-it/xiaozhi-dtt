# DTT ESP32-S3 N16R8 Xiaozhi Hardware

This project targets an ESP32-S3 N16R8 module: 16 MB flash and 8 MB PSRAM.

## OLED SSD1306 128x32

| OLED | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO47 |
| SCL | GPIO21 |

I2C address: `0x3C`.

## INMP441 Microphone

| INMP441 | ESP32-S3 |
| --- | --- |
| VDD | 3V3 |
| GND | GND |
| L/R | GND |
| SD | GPIO42 |
| WS | GPIO1 |
| SCK | GPIO2 |

The microphone is the left channel because `L/R` is tied to GND.

## MAX98357A Speaker Amplifier

| MAX98357A | ESP32-S3 |
| --- | --- |
| VIN | 3V3 or 5V, matching your module wiring |
| GND | GND |
| SD | VCC |
| GAIN | GND |
| DIN | GPIO39 |
| BCLK | GPIO40 |
| LRC | GPIO41 |

## PS2 Wireless Receiver

| PS2 RX | ESP32-S3 |
| --- | --- |
| VCC | 3V3 |
| GND | GND |
| CLK | GPIO14 |
| CMD | GPIO13 |
| ATT / CS | GPIO12 |
| DAT | GPIO11 |

PS2 button behavior:

- Any pressed button is shown on OLED.
- `O / CIRCLE` turns GPIO8 LED on.
- `TRIANGLE` turns GPIO8 LED off.

## Xiaozhi Defaults

- Language: Vietnamese (`vi-VN`)
- Initial server: official Xiaozhi service
- Wake word target: `He lo`
- Wake word threshold: `20`
- Wake word model: English MultiNet 7 (`mn7_en`), custom duration 7000 ms
- WakeNet9s multiple wake words: disabled
- OLED remains on GPIO47/GPIO21

## Important

The full Xiaozhi firmware is now built from `upstream_xiaozhi`, which is based on `78/xiaozhi-esp32`. Build and flash steps are recorded in `DTT_XIAOZHI_BUILD.md`.
