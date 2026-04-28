#!/usr/bin/env python3
"""
Lightweight music gateway for Xiaozhi MCP music tools.

This service is meant to be used with firmware MCP tools:
- self.music.set_gateway_url
- self.music.play_online
- self.music.control

Current capabilities:
- Resolve direct audio stream URL from a ZingMP3 track URL via yt-dlp.
- Keep simple per-device playback intent state for control actions.

Run:
  pip install flask yt-dlp
  python scripts/music_gateway_server.py --host 0.0.0.0 --port 8787
"""

from __future__ import annotations

import argparse
import json
import os
import time
from dataclasses import dataclass, asdict
from typing import Dict
from urllib import request as urlrequest
from urllib.error import URLError

from flask import Flask, jsonify, request

try:
    import yt_dlp
except Exception as exc:  # pragma: no cover
    raise RuntimeError("yt-dlp is required. Install with: pip install yt-dlp") from exc


app = Flask(__name__)


@dataclass
class SessionState:
    device_id: str
    client_id: str
    provider: str
    query: str
    title: str
    stream_url: str
    action: str = "play"
    updated_at: int = 0


_sessions: Dict[str, SessionState] = {}
_dispatch_url: str = os.getenv("MUSIC_DISPATCH_URL", "").strip()


def _resolve_audio_with_ytdlp(url_or_query: str) -> tuple[str, str]:
    """Resolve direct audio url and title using yt-dlp."""
    ydl_opts = {
        "quiet": True,
        "skip_download": True,
        "format": "bestaudio/best",
        "noplaylist": True,
    }
    with yt_dlp.YoutubeDL(ydl_opts) as ydl:
        info = ydl.extract_info(url_or_query, download=False)

    if info is None:
        raise RuntimeError("Unable to resolve media info")

    # yt-dlp may return a playlist wrapper
    if "entries" in info and info["entries"]:
        info = info["entries"][0]

    stream_url = info.get("url")
    title = info.get("title", "Unknown")

    if not stream_url:
        raise RuntimeError("No stream URL returned by resolver")

    return stream_url, title


def _dispatch_play_to_orchestrator(session: SessionState) -> tuple[bool, str]:
    """
    Optional: dispatch resolved stream URL to assistant orchestrator / device bridge.
    Set MUSIC_DISPATCH_URL env var to enable.
    """
    if not _dispatch_url:
        return False, "dispatch disabled (MUSIC_DISPATCH_URL is empty)"

    payload = {
        "type": "music.play",
        "device_id": session.device_id,
        "client_id": session.client_id,
        "provider": session.provider,
        "title": session.title,
        "stream_url": session.stream_url,
        "query": session.query,
    }
    body = json.dumps(payload).encode("utf-8")
    req = urlrequest.Request(
        _dispatch_url,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlrequest.urlopen(req, timeout=8) as resp:
            status = getattr(resp, "status", 200)
            if status < 200 or status >= 300:
                return False, f"dispatch http status={status}"
    except URLError as exc:
        return False, f"dispatch error: {exc}"
    return True, "dispatch success"


@app.route("/healthz", methods=["GET"])
def healthz():
    return jsonify(
        {
            "ok": True,
            "service": "music-gateway",
            "time": int(time.time()),
            "dispatch_enabled": bool(_dispatch_url),
            "dispatch_url": _dispatch_url,
        }
    )


@app.route("/v1/music/play", methods=["POST"])
def play_music():
    body = request.get_json(silent=True) or {}
    query = (body.get("query") or "").strip()
    provider = (body.get("provider") or "zingmp3").strip().lower()
    device_id = (body.get("device_id") or "").strip()
    client_id = (body.get("client_id") or "").strip()

    if not query:
        return jsonify({"ok": False, "error": "missing query"}), 400
    if not device_id:
        return jsonify({"ok": False, "error": "missing device_id"}), 400

    # For "real" Zing playback, pass full zingmp3.vn track URL as query.
    # Example: https://zingmp3.vn/bai-hat/.../ZWxxxx.html
    if provider == "zingmp3" and "zingmp3.vn" not in query:
        return jsonify(
            {
                "ok": False,
                "error": "For provider=zingmp3, query should be a full zingmp3.vn track URL",
            }
        ), 400

    try:
        stream_url, title = _resolve_audio_with_ytdlp(query)
    except Exception as exc:
        return jsonify({"ok": False, "error": f"resolver failed: {exc}"}), 502

    session = SessionState(
        device_id=device_id,
        client_id=client_id,
        provider=provider,
        query=query,
        title=title,
        stream_url=stream_url,
        action="play",
        updated_at=int(time.time()),
    )
    _sessions[device_id] = session
    dispatched, dispatch_message = _dispatch_play_to_orchestrator(session)

    return jsonify(
        {
            "ok": True,
            "message": "play resolved",
            "device_id": device_id,
            "provider": provider,
            "title": title,
            "stream_url": stream_url,
            "dispatched": dispatched,
            "dispatch_message": dispatch_message,
            "session": asdict(session),
        }
    )


@app.route("/v1/music/control", methods=["POST"])
def control_music():
    body = request.get_json(silent=True) or {}
    action = (body.get("action") or "").strip().lower()
    device_id = (body.get("device_id") or "").strip()

    if action not in {"pause", "resume", "next", "prev", "stop"}:
        return jsonify({"ok": False, "error": "invalid action"}), 400
    if not device_id:
        return jsonify({"ok": False, "error": "missing device_id"}), 400

    session = _sessions.get(device_id)
    if session is None:
        return jsonify({"ok": False, "error": "no active session for device"}), 404

    session.action = action
    session.updated_at = int(time.time())
    _sessions[device_id] = session

    return jsonify({"ok": True, "message": "control updated", "session": asdict(session)})


@app.route("/v1/music/session/<device_id>", methods=["GET"])
def get_session(device_id: str):
    session = _sessions.get(device_id)
    if session is None:
        return jsonify({"ok": False, "error": "session not found"}), 404
    return jsonify({"ok": True, "session": asdict(session)})


def main() -> None:
    parser = argparse.ArgumentParser(description="Xiaozhi music gateway")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()

    app.run(host=args.host, port=args.port)


if __name__ == "__main__":
    main()
