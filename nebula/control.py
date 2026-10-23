"""A client for LMMS-Nebula's control socket (README.md has the protocol).

LMMS-Nebula listens on a Unix socket when LMMS_CONTROL_SOCKET names one; the macOS app's launcher
(build-macos-app.sh) sets it to SOCKET below. A request is one line of JSON, {"cmd": ..., args}; the answer is one line,
{"ok": true, "result": ...} or {"ok": false, "error": ...}. Units are LMMS's own: positions in
ticks (48 to a beat, 192 to a bar of 4/4) or bars counted from 1, track volume in percent
(100 = unity), pan -100..100, note volume 0-200 (100 is LMMS's default).

    python3 nebula/control.py status
    python3 nebula/control.py set_track '{"track": "Bass", "volume": 80}'
"""
import json
import os
import socket
import sys
from pathlib import Path

SOCKET = os.environ.get("LMMS_CONTROL_SOCKET", str(
    Path.home() / "Library/Application Support/LMMS-Nebula/control.sock" if sys.platform == "darwin"
    else Path(os.environ.get("XDG_RUNTIME_DIR", "/tmp")) / "lmms-nebula.sock"))


class LmmsError(RuntimeError):
    pass


def call(cmd, timeout=120, **args):
    """One request; returns its result or raises LmmsError. Opening a project can take a minute."""
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
            s.settimeout(timeout)
            s.connect(SOCKET)
            s.sendall(json.dumps({"cmd": cmd, **args}).encode() + b"\n")
            data = b""
            while not data.endswith(b"\n"):
                chunk = s.recv(1 << 16)
                if not chunk:
                    break
                data += chunk
    except (FileNotFoundError, ConnectionRefusedError):
        raise LmmsError(f"LMMS-Nebula is not running (no control socket at {SOCKET}); "
                        "start LMMS-Nebula, or LMMS with LMMS_CONTROL_SOCKET set") from None
    answer = json.loads(data)
    if not answer.get("ok"):
        raise LmmsError(answer.get("error", "no answer"))
    return answer["result"]


def running():
    try:
        call("status", timeout=5)
        return True
    except (LmmsError, OSError):
        return False


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    try:
        print(json.dumps(call(sys.argv[1], **(json.loads(sys.argv[2]) if len(sys.argv) == 3 else {})), indent=1))
    except LmmsError as e:
        sys.exit(f"lmms: {e}")
