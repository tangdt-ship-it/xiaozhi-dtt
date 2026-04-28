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
import re
import time
from dataclasses import dataclass, asdict
from typing import Dict, Any, Optional
from urllib import request as urlrequest
from urllib.error import URLError
from urllib.parse import quote

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
    original_query: str
    query: str
    title: str
    stream_url: str
    action: str = "play"
    updated_at: int = 0


_sessions: Dict[str, SessionState] = {}
_dispatch_url: str = os.getenv("MUSIC_DISPATCH_URL", "").strip()


def _pick_stream_url_from_info(info: Dict[str, Any]) -> Optional[str]:
    # 1) direct url
    direct = info.get("url")
    if isinstance(direct, str) and direct:
        return direct

    # 2) requested formats (if extractor populated)
    req_fmts = info.get("requested_formats") or []
    for fmt in req_fmts:
        u = fmt.get("url")
        if isinstance(u, str) and u:
            return u

    # 3) pick best audio from formats
    fmts = info.get("formats") or []
    audio_candidates = []
    for fmt in fmts:
        # Keep formats with audio
        acodec = fmt.get("acodec")
        if acodec in (None, "none"):
            continue
        u = fmt.get("url")
        if not isinstance(u, str) or not u:
            continue
        abr = fmt.get("abr") or 0
        audio_candidates.append((abr, u))

    if audio_candidates:
        audio_candidates.sort(key=lambda x: x[0], reverse=True)
        return audio_candidates[0][1]

    return None


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

    # yt-dlp may return a playlist wrapper (album, playlist...)
    if "entries" in info and info["entries"]:
        first_entry = info["entries"][0]
        # Sometimes first entry has partial metadata without direct url.
        # If webpage_url exists, re-resolve for richer fields.
        first_webpage_url = first_entry.get("webpage_url")
        if isinstance(first_webpage_url, str) and first_webpage_url:
            info = ydl.extract_info(first_webpage_url, download=False)
        else:
            info = first_entry

    stream_url = _pick_stream_url_from_info(info)
    title = info.get("title", "Unknown")

    if not stream_url:
        raise RuntimeError("No stream URL returned by resolver")

    return stream_url, title


def _fetch_text(url: str, timeout: int = 8) -> str:
    req = urlrequest.Request(
        url,
        headers={
            "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
        },
        method="GET",
    )
    with urlrequest.urlopen(req, timeout=timeout) as resp:
        charset = resp.headers.get_content_charset() or "utf-8"
        return resp.read().decode(charset, errors="ignore")


def _search_zing_track_url(keyword: str) -> str:
    """
    Resolve first Zing track URL by scraping Zing search page.
    This is best-effort and may need updates if Zing changes markup.
    """
    search_url = f"https://zingmp3.vn/tim-kiem/tat-ca?q={quote(keyword)}"
    html = _fetch_text(search_url)

    # Typical pattern: /bai-hat/<slug>/<ZW....>.html
    pattern = r'href=\"(/bai-hat/[^\"\\s]+/ZW[0-9A-Z]+\\.html)\"'
    match = re.search(pattern, html)
    if not match:
        # Fallback: looser route match
        pattern_fallback = r'href=\"(/bai-hat/[^\"\\s]+\\.html)\"'
        match = re.search(pattern_fallback, html)
    if not match:
        raise RuntimeError("No track URL found from Zing search page")

    return "https://zingmp3.vn" + match.group(1)


def _resolve_provider_query(provider: str, query: str) -> tuple[str, str]:
    """
    Return (resolved_input, title):
    - resolved_input is either direct media/page url for yt-dlp.
    - title may be replaced after final yt-dlp resolution.
    """
    provider = provider.lower().strip()
    if provider == "zingmp3":
        if "zingmp3.vn" in query:
            return query, ""
        # query is keyword => search first track URL from Zing
        track_url = _search_zing_track_url(query)
        return track_url, ""
    # fallback for other providers / generic URLs
    return query, ""


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

    try:
        resolved_query, _ = _resolve_provider_query(provider, query)
        stream_url, title = _resolve_audio_with_ytdlp(resolved_query)
    except Exception as exc:
        return jsonify({"ok": False, "error": f"resolver failed: {exc}"}), 502

    session = SessionState(
        device_id=device_id,
        client_id=client_id,
        provider=provider,
        original_query=query,
        query=resolved_query,
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
