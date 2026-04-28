# Music Gateway for ZingMP3 Online Playback

This document describes a practical path to "real" online music playback with XiaoZhi firmware + MCP tools.

## Why gateway is needed

The ESP32 firmware exposes MCP tools to ask a backend to play/control music:

- `self.music.set_gateway_url(url)`
- `self.music.play_online(query, provider="zingmp3")`
- `self.music.control(action)`

Firmware sends requests to:

- `POST /v1/music/play`
- `POST /v1/music/control`

A gateway is responsible for provider-specific resolving/auth/token handling.

## Reference gateway implementation

A minimal reference service is included at:

- `scripts/music_gateway_server.py`

Capabilities:

- Resolve direct audio stream URL from a ZingMP3 track URL via `yt-dlp`.
- Resolve by Zing keyword query (best-effort) or direct Zing track URL.
- Better handling for album/playlist links by trying entries in order and selecting the first playable audio stream.
- Includes yt-dlp flat playlist extraction + HTML fallback extraction for Zing track pages when direct stream URL is unavailable.
- Keep per-device session state.
- Accept control commands (`pause/resume/next/prev/stop`) for orchestration integration.
- Optional dispatch hook to your assistant orchestrator by setting `MUSIC_DISPATCH_URL`.
- Local Vietnamese music library mode (`provider=local` / `local_vn`) that serves files directly from server disk.

## Run locally

```bash
pip install flask yt-dlp
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

### Windows (PowerShell) - exactly where to run each step

Use **2 PowerShell windows**:

- **Window A (server window):** start and keep gateway running.
- **Window B (test window):** send API requests (`healthz`, `library`, `play`...).

In **both windows**, go to repo folder first:

```powershell
cd C:\Espressif\frameworks\esp-idf-v5.5.2\xiaozhi-dtt
```

Run with local music directory:

```bash
MUSIC_LIBRARY_DIR=/path/to/your-vietnamese-music \
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

With dispatch enabled (recommended for real playback flow):

```bash
MUSIC_DISPATCH_URL=http://127.0.0.1:9000/v1/device/music/dispatch \
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

If Zing requires authenticated session/cookies for playable audio URLs, add:

```bash
YTDLP_COOKIES_FILE=/path/to/cookies.txt \
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

If `YTDLP_COOKIES_FILE` points to a missing path, resolver will continue without cookie mode. Check `/healthz` fields:
- `yt_dlp_cookiefile_enabled`
- `yt_dlp_cookiefile_exists`

Health check:

```bash
curl http://127.0.0.1:8787/healthz
```

If you get `404` for `/v1/music/library/reload`:

1. Usually you are running an older script version. Pull latest branch, then restart gateway.
2. Verify route exists in your local file:

```powershell
rg -n "v1/music/library/reload" scripts/music_gateway_server.py
```

3. Verify health reports current version/features:

```powershell
Invoke-RestMethod http://127.0.0.1:8787/healthz
```

You should see:
- `version`
- `features.local_library = true`

## API examples

### 1) Resolve and play a ZingMP3 URL

```bash
curl -X POST http://127.0.0.1:8787/v1/music/play \
  -H 'Content-Type: application/json' \
  -d '{
    "provider": "zingmp3",
    "query": "https://zingmp3.vn/bai-hat/...",
    "device_id": "AA:BB:CC:DD:EE:FF",
    "client_id": "device-client-id"
  }'
```

### 1b) Resolve and play by keyword (Vietnamese song name)

```bash
curl -X POST http://127.0.0.1:8787/v1/music/play \
  -H 'Content-Type: application/json' \
  -d '{
    "provider": "zingmp3",
    "query": "Sơn Tùng M-TP Nắng Ấm Xa Dần",
    "device_id": "AA:BB:CC:DD:EE:FF",
    "client_id": "device-client-id"
  }'
```

### 2) Control

```bash
curl -X POST http://127.0.0.1:8787/v1/music/control \
  -H 'Content-Type: application/json' \
  -d '{
    "action": "pause",
    "device_id": "AA:BB:CC:DD:EE:FF",
    "client_id": "device-client-id"
  }'
```

### 3) Read current device session

```bash
curl http://127.0.0.1:8787/v1/music/session/AA:BB:CC:DD:EE:FF
```

### 4) Local library APIs (recommended for stable "add Vietnamese music to server")

List imported tracks:

```bash
curl http://127.0.0.1:8787/v1/music/library
```

Reload after adding/removing files in `MUSIC_LIBRARY_DIR`:

```bash
curl -X POST http://127.0.0.1:8787/v1/music/library/reload
```

Play by local provider:

```bash
curl -X POST http://127.0.0.1:8787/v1/music/play \
  -H 'Content-Type: application/json' \
  -d '{
    "provider": "local_vn",
    "query": "em cua ngay hom qua",
    "device_id": "AA:BB:CC:DD:EE:FF",
    "client_id": "device-client-id"
  }'
```

`query` can be:
- `track_id` from `/v1/music/library`
- substring of song title
- substring of relative file path

## Important notes

- Keyword search is implemented by best-effort HTML parsing on Zing search page and may need updates if Zing changes markup.
- In production, use `MUSIC_DISPATCH_URL` to forward resolved stream info into your assistant orchestration path that actually sends playback commands to device sessions.
- For "add Vietnamese music to my own server and play reliably", prioritize `provider=local_vn` + `MUSIC_LIBRARY_DIR`.
