"""Launch a separate LMMS-Nebula process and round-trip the starter score through its socket."""

import argparse
from copy import deepcopy
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

from check_score import WEB, check, sort_notes

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import control


def verify(binary):
    template = WEB / "low-tide-relay.mmp"
    requests = check(template, WEB / "low-tide-relay.jsonl")
    with tempfile.TemporaryDirectory(prefix="lmms-session-", dir="/tmp") as directory:
        root = Path(directory)
        score = root / "fixture.mmp"
        shutil.copyfile(template, score)
        socket = str(root / "control.sock")
        config = ET.Element("lmms", version="1.3.0-alpha.2")
        ET.SubElement(config, "paths", workingdir=str(root / "workspace") + "/")
        ET.SubElement(config, "mixer", audiodev="Dummy (no sound output)", mididev="Dummy (no MIDI support)")
        config_path = root / "settings.xml"
        ET.ElementTree(config).write(config_path)
        (root / "workspace").mkdir()
        env = dict(os.environ, LMMS_CONTROL_SOCKET=socket, QT_QPA_PLATFORM="offscreen",
                   HOME=directory, XDG_CONFIG_HOME=directory, XDG_RUNTIME_DIR=directory)
        control.SOCKET = socket
        with (root / "startup.log").open("w+") as log:
            process = subprocess.Popen([str(binary), "--config", str(config_path), str(score)],
                                       cwd=root, env=env, stdout=log, stderr=log)
            try:
                for _ in range(100):
                    if process.poll() is not None:
                        break
                    try:
                        status = control.call("status", timeout=1)
                        if status["project"] == str(score) and not status["loading"]:
                            break
                    except (control.LmmsError, OSError):
                        pass
                    time.sleep(0.1)
                else:
                    raise RuntimeError("The fork did not expose the fixture on its private test socket.")
                if process.poll() is not None:
                    raise RuntimeError(f"LMMS exited early with {process.returncode}.")
                for request in requests:
                    target = {"track": request["track"], "clip": request["clip"]}
                    expected = sort_notes(request["notes"])
                    assert sort_notes(control.call("get_notes", **target)["notes"]) == expected
                    assert sort_notes(control.call("set_notes", notes=expected, **target)["notes"]) == expected
                target = {"track": requests[0]["track"], "clip": requests[0]["clip"]}
                changed = deepcopy(requests[0]["notes"])
                changed[0]["vol"] -= 1
                control.call("set_notes", notes=changed, **target)
                try:
                    control.call("set_notes", notes=changed + [{**changed[0], "key": 128}], **target)
                except control.LmmsError:
                    pass
                else:
                    raise AssertionError("The fork accepted a note with a key beyond MIDI range.")
                assert sort_notes(control.call("get_notes", **target)["notes"]) == sort_notes(changed)
                saved = root / "saved.mmp"
                control.call("save", path=str(saved))
                control.call("open", path=str(saved))
                assert sort_notes(control.call("get_notes", **target)["notes"]) == sort_notes(changed)
                print(f'Native socket verified {len(requests)} tracks and {sum(len(r["notes"]) for r in requests)} notes. '
                      "One velocity edit survived save/reopen. An invalid batch preserved the clip.")
            except Exception:
                log.flush()
                log.seek(0)
                print(log.read()[-2400:], file=sys.stderr)
                raise
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, help="Path to the built fork's lmms executable")
    verify(parser.parse_args().binary.resolve())
