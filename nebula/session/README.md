# Low Tide Relay

Play a four-bar D Dorian game cue with a bell lead, bass, sustained chords and mallet ticks. The browser editor and the two score exports share one 48-tick-per-beat note representation. The default cue has 74 notes at 96 BPM.

Open [web/index.html](web/index.html) directly from a local checkout. The page uses local JavaScript and CSS files. In the arrangement, choose a track or bar and press **Play**. Select a note to change its pitch, step, length or velocity. Click an empty step or use **Add note**. **Undo** restores the previous edit.

| Export | What it carries |
| --- | --- |
| LMMS project | A four-track MMP with TripleOscillator instruments, note data, tempo and track mutes |
| Bridge JSONL | One `set_notes` request per named clip for LMMS-Nebula’s local socket |
| WAV | One four-bar stereo PCM loop rendered by the browser at the edited tempo |

The MMP and bridge commands use the same note positions, durations, MIDI keys, velocities and panning. The JSONL replaces clip notes in an existing project. Open the corresponding MMP first so the named tracks and clips exist. The browser exports the commands to a file. Socket access stays local to the desktop client.

The [native starter recording](web/low-tide-relay.ogg) came from rendering the [default MMP](web/low-tide-relay.mmp) with upstream LMMS 1.3.0-alpha.2. Web Audio uses its own oscillator envelopes and partials. The two engines play the same score with different timbres. The cue was written for this demo. LMMS and TripleOscillator are upstream work. Aladdin Ali’s fork contributes the C++ socket and Python control client.

## Reproduce the checks

From the repository root, compare every exported note with the bridge file. Supply two paths to check edited downloads.

```bash
python3 nebula/session/tests/check_score.py
python3 nebula/session/tests/check_score.py edited.mmp edited.jsonl
node --test nebula/session/tests/score.test.cjs
```

To test the C++ integration, point the native check at a built LMMS-Nebula executable. It starts its own process and temporary project, verifies four clips, edits one velocity, attempts an invalid batch, saves and reopens the edited song.

```bash
python3 nebula/session/tests/check_native.py /path/to/LMMS-Nebula/lmms
```

[Score and exporters](web/score.js) · [C++ commands](../../src/gui/ControlServer.cpp) · [Python client](../control.py) · [Upstream LMMS](https://github.com/LMMS/lmms)
