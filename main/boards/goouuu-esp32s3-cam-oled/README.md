# Goouuu ESP32-S3-CAM + OLED

Custom board profile for Goouuu ESP32-S3-WROOM-1-N16R8 camera dev board with the following external peripherals:

- INMP441 microphone
  - WS: GPIO1
  - SCK: GPIO2
  - SD: GPIO42
- MAX98357A speaker amplifier
  - DIN: GPIO39
  - BCLK: GPIO40
  - LRC: GPIO41
- SSD1306 OLED 128x32 (I2C)
  - SCL: GPIO21
  - SDA: GPIO47

## Build

```bash
idf.py set-target esp32s3
idf.py menuconfig
# Xiaozhi Assistant -> Board Type -> Goouuu ESP32-S3-CAM + OLED (INMP441/MAX98357A)
idf.py build
```

Or package with release script:

```bash
python scripts/release.py main/boards/goouuu-esp32s3-cam-oled
```
