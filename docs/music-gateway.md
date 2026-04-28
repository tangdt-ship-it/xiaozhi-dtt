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
- Keep per-device session state.
- Accept control commands (`pause/resume/next/prev/stop`) for orchestration integration.
- Optional dispatch hook to your assistant orchestrator by setting `MUSIC_DISPATCH_URL`.

## Run locally

```bash
pip install flask yt-dlp
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

With dispatch enabled (recommended for real playback flow):

```bash
MUSIC_DISPATCH_URL=http://127.0.0.1:9000/v1/device/music/dispatch \
python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
```

Health check:

```bash
curl http://127.0.0.1:8787/healthz
```

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

## Important notes

- For `provider=zingmp3`, this reference implementation expects a full Zing track URL as `query`.
- Search-by-keyword on Zing is intentionally not hard-coded because endpoint policies may change.
- In production, use `MUSIC_DISPATCH_URL` to forward resolved stream info into your assistant orchestration path that actually sends playback commands to device sessions.
