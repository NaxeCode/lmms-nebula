# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp>=1.9,<2"]
# ///
"""An MCP server that drives a running LMMS-Nebula, so an assistant can work on a song in LMMS
while you listen: read and set faders, move the playhead, play and stop, read and rewrite a clip's
notes, reload a sample track's audio after you re-render it, open and save the project.

    uv run nebula/lmms_mcp.py                  # stdio
    claude mcp add lmms -- uv run /path/to/lmms-nebula/nebula/lmms_mcp.py

It talks to LMMS through control.py (the socket protocol is there). Positions are ticks (48 to a
beat, 192 to a bar of 4/4) or bars from 1, as LMMS shows them; track volume is LMMS's percent
(100 = unity); note volume 0-200 (MIDI import maps velocity 127 to 200).
"""
import math
import sys
from pathlib import Path

from mcp.server.fastmcp import FastMCP

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import control  # noqa: E402

mcp = FastMCP("lmms", instructions=(
    "Drives LMMS-Nebula, a running LMMS with a control socket. Use lmms_tracks to learn the "
    "track names first; every other tool names tracks exactly as they appear there."))


def where(bar, tick):
    return {k: v for k, v in (("bar", bar), ("tick", tick)) if v is not None}


@mcp.tool()
def lmms_status() -> dict:
    """The open project, whether it plays, the playhead (tick and bar from 1), tempo and length."""
    return control.call("status")


@mcp.tool()
def lmms_tracks(clips: bool = False) -> list:
    """Every track: name, type (sample, instrument, ...), volume (percent), pan, muted, solo.
    With clips=True, each track's clips too (name, pos and len in ticks, sample file, note count)."""
    tracks = control.call("tracks")["tracks"]
    if not clips:
        for t in tracks:
            t["clips"] = len(t["clips"])
    return tracks


@mcp.tool()
def lmms_set_track(track: str, volume: float | None = None, gain_db: float | None = None, pan: float | None = None,
                   muted: bool | None = None, solo: bool | None = None) -> dict:
    """Set a track's fader, pan, mute or solo, by its exact name. volume is LMMS's percent (100 =
    unity, 0-200); gain_db is the same fader in dB (0 dB = 100 %, +6 dB is the top). Solo works as
    LMMS's solo button: it mutes every other track until unsoloed."""
    args = {"track": track}
    if gain_db is not None:
        volume = 100 * 10 ** (gain_db / 20)
    for key, value in (("volume", volume), ("pan", pan), ("muted", muted), ("solo", solo)):
        if value is not None:
            args[key] = value
    result = control.call("set_track", **args)
    if "volume" in result:
        result["gain_db"] = round(20 * math.log10(result["volume"] / 100), 2) if result["volume"] > 0 else None
    return result


@mcp.tool()
def lmms_play(bar: float | None = None, tick: int | None = None) -> dict:
    """Play the song, from bar (from 1) or tick if given, else from the playhead."""
    return control.call("play", **where(bar, tick))


@mcp.tool()
def lmms_pause() -> dict:
    """Pause playback where it is."""
    return control.call("pause")


@mcp.tool()
def lmms_stop() -> dict:
    """Stop playback (LMMS returns the playhead to where play started)."""
    return control.call("stop")


@mcp.tool()
def lmms_seek(bar: float | None = None, tick: int | None = None) -> dict:
    """Move the playhead to bar (from 1; 5.5 is half way through bar 5) or tick, playing or not."""
    return control.call("seek", **where(bar, tick))


@mcp.tool()
def lmms_get_notes(track: str, clip: str | None = None, bar: float | None = None, tick: int | None = None) -> dict:
    """The notes of a MIDI clip: on track (e.g. 'Piano'), the clip named clip, or the one at bar/tick. Each note: pos and len in
    ticks from the clip's start (48 a beat), key (MIDI number, 60 = middle C), vol 0-200, pan."""
    result = control.call("get_notes", track=track, **({"clip": clip} if clip else where(bar, tick)))
    for n in result["notes"]:
        n["beat"] = n["pos"] / 48
    return result


@mcp.tool()
def lmms_set_notes(track: str, notes: list[dict], clip: str | None = None, bar: float | None = None,
                   tick: int | None = None) -> dict:
    """Replace every note of a MIDI clip (chosen as in lmms_get_notes) with notes: each
    {pos, len, key, vol?, pan?}, pos and len in ticks from the clip's start. All are checked before
    the clip changes."""
    return control.call("set_notes", track=track, notes=notes, **({"clip": clip} if clip else where(bar, tick)))


@mcp.tool()
def lmms_reload_samples(track: str, file: str | None = None) -> dict:
    """Re-read a sample track's audio from disk (every clip on it), without reopening the project:
    the way to hear a stem after re-rendering it. file, if given, replaces the clip's file."""
    return control.call("reload_samples", track=track, **({"file": file} if file else {}))


@mcp.tool()
def lmms_open(path: str, discard_changes: bool = False) -> dict:
    """Open a project (.mmp/.mmpz). It refuses while the open project has unsaved changes, unless discard_changes."""
    return control.call("open", timeout=600, path=str(Path(path).resolve()), discard_changes=discard_changes)


@mcp.tool()
def lmms_save(path: str | None = None) -> dict:
    """Save the project, to its own file or to path."""
    return control.call("save", **({"path": str(Path(path).resolve())} if path else {}))




if __name__ == "__main__":
    mcp.run()
