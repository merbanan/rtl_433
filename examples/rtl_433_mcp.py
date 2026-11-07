#!/usr/bin/env python3
"""Simple MCP adapter for the rtl_433's HTTP/WebSocket server.

Run rtl_433 -F http:127.0.0.1:8433 separately, then integrate this program
as an MCP stdio server, e.g. into VSC.

Dependencies:
    - mcp>=2
    - websocket-client

Environment Variables:
    - RTL433_URL: rtl_433 HTTP server endpoint (default: http://127.0.0.1:8433)
    - RTLLOG: Logger verbosity level (default: WARNING)

Example:
    RTL433_URL=http://192.168.1.10:8433 python rtl_433_mcp.py
"""

import json
import logging
import os
import threading
import time
from collections import deque
from datetime import datetime, timezone
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit
from urllib.request import urlopen

import websocket
from mcp.server import MCPServer

# Configuration:

# DEFAULT_RTL433_URL: Fallback receiver address if RTL433_URL env var is not set.
# Override via: RTL433_URL=http://192.168.178.76:8433 (e.g., in mcp.json "env" section)
DEFAULT_RTL433_URL = "http://127.0.0.1:8433"

# LOGGING_LEVEL: Verbosity for the MCP server logger. Set via RTLLOG env var.
LOGGING_LEVEL = os.getenv("RTLLOG", "WARNING")

# LOGGER_NAME: Name for the MCP server logger.
LOGGER_NAME = "rtl433-mcp"

# MCP_SERVER_NAME: Name for the MCP server instance.
MCP_SERVER_NAME = "rtl_433"

# RECENT_EVENTS_MAX: Maximum number of events to buffer in the recent events deque.
RECENT_EVENTS_MAX = 500

# Global vars:

# BASE_URL: Resolved receiver HTTP endpoint. Set via RTL433_URL env var or uses DEFAULT.
BASE_URL = os.getenv("RTL433_URL", DEFAULT_RTL433_URL).rstrip("/")
if urlsplit(BASE_URL).scheme not in ("http", "https"):
    raise ValueError("RTL433_URL must start with http:// or https://")
# WS_URL: Automatic WebSocket conversion from BASE_URL (http → ws, https → wss)
WS_URL = BASE_URL.replace("http://", "ws://", 1).replace("https://", "wss://", 1) + "/ws"

logging.basicConfig(level=LOGGING_LEVEL)
log = logging.getLogger(LOGGER_NAME)
mcp = MCPServer(MCP_SERVER_NAME)
lock = threading.Lock()
recent = deque(maxlen=RECENT_EVENTS_MAX)
latest = {}
latest_timestamp = {}  # Track (timestamp, event) to detect duplicates within 2 seconds
connection = {"connected": False, "last_error": None}

def send_http_command(cmd: str, *, val: int | None = None, arg: str | None = None):
    """Send an HTTP command to rtl_433 and return the result. Handles errors and JSON parsing."""
    params = {"cmd": cmd}
    if val is not None:
        params["val"] = val
    if arg is not None:  # Important: gain='' means automatic gain.
        params["arg"] = arg
    url = BASE_URL + "/cmd?" + urlencode(params)
    try:
        with urlopen(url, timeout=5) as response:
            body = response.read().decode("utf-8")
    except (HTTPError, URLError, TimeoutError) as exc:
        raise RuntimeError(f"rtl_433 command {cmd!r} failed: {exc}") from exc
    try:
        payload = json.loads(body)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid rtl_433 response: {body[:200]}") from exc
    if isinstance(payload, dict) and "error" in payload:
        raise RuntimeError(str(payload["error"]))
    return payload.get("result", payload) if isinstance(payload, dict) else payload


def generate_sensor_id(event: dict) -> str:
    """Generate a unique key for a sensor based on model, ID, channel, and MIC."""
    # An ID alone is not globally unique; include model and channel.
    identity = [str(event.get("model", "unknown"))]
    for field in ("id", "channel", "mic"):
        if field in event:
            identity.append(f"{field}={event[field]}")
    # ID-less button presses may not represent distinct persistent devices.
    return "/".join(identity)


def collect_sensor_events():
    """Continuously connect to rtl_433 WebSocket, buffer events, and update latest sensor states."""
    while True:
        ws = None
        try:
            ws = websocket.create_connection(WS_URL, timeout=10)
            ws.settimeout(2)
            with lock:
                connection.update(connected=True, last_error=None)
            while True:
                try:
                    message = ws.recv()
                except websocket.WebSocketTimeoutException:
                    continue
                if not message:
                    raise ConnectionError("WebSocket closed")
                try:
                    event = json.loads(message)
                except (ValueError, TypeError):
                    continue
                if not isinstance(event, dict) or "model" not in event:
                    continue  # Ignore initial meta and protocol messages.
                now_dt = datetime.now(timezone.utc)
                now_iso = now_dt.isoformat()
                item = {"received_at": now_iso, **event}
                key = generate_sensor_id(event)
                with lock:
                    # Skip duplicate if same values received within 2 seconds
                    is_duplicate = False
                    if key in latest_timestamp:
                        last_time, last_event = latest_timestamp[key]
                        if (now_dt - last_time).total_seconds() < 2 and last_event == event:
                            is_duplicate = True
                    if not is_duplicate:
                        latest_timestamp[key] = (now_dt, event)
                        recent.append(item)
                        latest[key] = item
        except Exception as exc:
            with lock:
                connection.update(connected=False, last_error=str(exc))
            log.warning("rtl_433 WebSocket disconnected: %s", exc)
            time.sleep(3)
        finally:
            if ws is not None:
                ws.close()


@mcp.tool()
def get_receiver_info() -> dict:
    """Get rtl_433 frequency, sample rate, gain and PPM correction."""
    return {
        "meta": send_http_command("get_meta"),
        "gain": send_http_command("get_gain"),
        "ppm_error": send_http_command("get_ppm_error"),
        "device": send_http_command("get_dev_info"),
    }


@mcp.tool()
def get_statistics() -> dict:
    """Get some rtl_433 decoder and receive statistics."""
    return send_http_command("get_stats")


@mcp.tool()
def list_sensors(sort_by: str = "rssi", descending: bool = True) -> dict:
    """List sensors and latest received values. Sort by rssi, snr, temperature_C or received_at."""
    allowed = {"rssi", "snr", "temperature_C", "received_at"}
    if sort_by not in allowed:
        raise ValueError(f"sort_by must be one of {sorted(allowed)}")
    with lock:
        rows = [{"sensor": key, **event} for key, event in latest.items()]
        status = dict(connection)
    # Numeric sorts do not include sensors missing that measurement.
    present = [x for x in rows if x.get(sort_by) is not None]
    missing = [x for x in rows if x.get(sort_by) is None]
    present.sort(key=lambda x: x[sort_by], reverse=descending)
    return {"connection": status, "count": len(rows), "sensors": present + missing}


@mcp.tool()
def get_recent_events(limit: int = 20, model: str = "") -> list[dict]:
    """Return recently received decoded events, optionally filtered by model."""
    if not 1 <= limit <= 100:
        raise ValueError("limit must be between 1 and 100")
    with lock:
        events = list(recent)
    if model:
        events = [e for e in events if model.casefold() in str(e.get("model", "")).casefold()]
    return list(reversed(events[-limit:]))


@mcp.tool()
def set_frequency(frequency_hz: int) -> dict:
    """Retune the RTL-SDR. Frequency is specified in Hz, e.g. 433920000."""
    if not 100_000 <= frequency_hz <= 2_000_000_000:
        raise ValueError("Unreasonable frequency")
    result = send_http_command("center_frequency", val=frequency_hz)
    return {"requested_frequency_hz": frequency_hz, "response": result}


@mcp.tool()
def set_sample_rate(sample_rate_hz: int) -> dict:
    """Set SDR sample rate in Hz, e.g. 250000 or 1024000."""
    if not 100_000 <= sample_rate_hz <= 3_200_000:
        raise ValueError("Sample rate outside configured safety bounds")
    result = send_http_command("sample_rate", val=sample_rate_hz)
    return {"requested_sample_rate_hz": sample_rate_hz, "response": result}


@mcp.tool()
def set_gain(gain_db: float | None = None) -> dict:
    """Set tuner gain in dB. Omit gain_db to enable automatic gain."""
    if gain_db is not None and not -10 <= gain_db <= 60:
        raise ValueError("Gain outside configured safety bounds")
    arg = "" if gain_db is None else str(gain_db)
    result = send_http_command("gain", arg=arg)
    return {"requested_gain_db": gain_db, "response": result}


if __name__ == "__main__":
    threading.Thread(target=collect_sensor_events, daemon=True).start()
    mcp.run(transport="stdio")
