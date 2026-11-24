"""Compare an exported LMMS song and JSONL commands without running the DAW."""

import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET

FIELDS = ("pos", "len", "key", "vol", "pan")
WEB = Path(__file__).resolve().parents[1] / "web"


def sort_notes(notes):
    return sorted(notes, key=lambda n: tuple(n[k] for k in FIELDS))


def check(project_path, bridge_path):
    root = ET.parse(project_path).getroot()
    assert root.tag == "lmms-project" and root.get("type") == "song"
    requests = [json.loads(line) for line in bridge_path.read_text().splitlines()]
    tracks = root.findall("./song/trackcontainer/track")
    assert len(tracks) == len(requests) == 4
    for track, request in zip(tracks, requests):
        clip = track.find("./midiclip")
        assert request["cmd"] == "set_notes"
        assert (request["track"], request["clip"]) == (track.get("name"), clip.get("name"))
        xml_notes = [{k: int(n.get(k)) for k in FIELDS} for n in clip.findall("note")]
        assert sort_notes(xml_notes) == sort_notes(request["notes"])
        for n in xml_notes:
            assert 0 <= n["key"] <= 127 and n["pos"] >= 0 and n["len"] > 0
            assert n["pos"] + n["len"] <= int(clip.get("len"))
            assert 0 <= n["vol"] <= 200 and -100 <= n["pan"] <= 100
    return requests


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mmp", nargs="?", type=Path, default=WEB / "low-tide-relay.mmp")
    parser.add_argument("jsonl", nargs="?", type=Path, default=WEB / "low-tide-relay.jsonl")
    args = parser.parse_args()
    requests = check(args.mmp, args.jsonl)
    print(f'{len(requests)} tracks, {sum(len(r["notes"]) for r in requests)} notes match in both exports.')
