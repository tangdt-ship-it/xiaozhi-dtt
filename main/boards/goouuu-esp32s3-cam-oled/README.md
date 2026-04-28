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

## Camera clarity notes (important)

- This board profile uses VGA + sharpness tuning for clearer frames in object-recognition scenarios.
- Many ESP32-S3-CAM modules use manual-focus lenses. If the image is still blurry, rotate the camera lens ring slightly to refocus.
- OLED 128x32 is monochrome and extremely low resolution, so it is not suitable for meaningful camera preview. Use server-side snapshot/vision results for recognition feedback.

## Camera health mode on OLED

- Double click the BOOT button to toggle camera health mode.
- When enabled, OLED will periodically show:
  - `CAM OK L:xxx` (capture success + average brightness)
  - `CAM FAIL` (capture failure)

## Default settings in this board profile

- Language: Vietnamese (`CONFIG_LANGUAGE_VI_VN=y`)
- Board type: Goouuu ESP32-S3-CAM + OLED
- Wake word: custom wake word `hi lily` (display text `Hi LiLy`)

## About playing music from YouTube/Zing MP3

This firmware profile itself does not directly implement platform-specific music streaming clients for youtube.com/zingmp3.vn.
To support voice-command music playback from those services, the server-side assistant/MCP tools must provide URL parsing, search, authorization and stream proxy capability.

## Online music tools (implemented in firmware MCP)

The firmware now exposes MCP tools to integrate with a backend music gateway:

- `self.music.set_gateway_url(url)`
- `self.music.play_online(query, provider="zingmp3")`
- `self.music.control(action)` where action is one of: `pause`, `resume`, `next`, `prev`, `stop`

### Suggested gateway API contract

- `POST {gateway_url}/v1/music/play`
  - Request JSON:
    - `query`, `provider`, `device_id`, `client_id`
  - Response JSON (example):
    - `{"ok": true, "message": "play dispatched"}`

- `POST {gateway_url}/v1/music/control`
  - Request JSON:
    - `action`, `device_id`, `client_id`
  - Response JSON (example):
    - `{"ok": true}`

The gateway is responsible for searching tracks on zingmp3.vn, handling authorization/token logic, and dispatching playable audio streams to this device session.
