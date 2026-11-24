const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const score = require("../web/score.js");
const web = path.join(__dirname, "../web");
test("checked-in starter exports match the score and transport clock", () => {
  const project = score.create();
  assert.equal(score.mmp(project), fs.readFileSync(path.join(web, "low-tide-relay.mmp"), "utf8"));
  const expected = fs.readFileSync(path.join(web, "low-tide-relay.jsonl"), "utf8").trim().split("\n").map(JSON.parse);
  assert.deepEqual(score.requests(project), expected);
  assert.equal(expected.reduce((total, command) => total + command.notes.length, 0), 74);
  assert.equal(score.length * 60 / (score.beat * project.bpm), 10);
});
test("exporters reject a note that exceeds the song, MIDI or velocity range", () => {
  for (const change of [{pos: -1}, {key: 128}, {vol: 201}, {len: 769}, {pan: -101}, {len: 0}, {key: 69.5}]) {
    const project = score.create();
    Object.assign(project.tracks[0].notes[0], change);
    assert.throws(() => score.mmp(project));
    assert.throws(() => score.requests(project));
  }
});
test("a track mute changes only the native project while pitch edits reach both exports", () => {
  const project = score.create();
  project.tracks[0].muted = true;
  project.tracks[0].notes[0].key = 70;
  assert.match(score.mmp(project), /name="Glass lead" type="0" muted="1"/);
  assert.match(score.mmp(project), /pos="0" len="36" key="70"/);
  assert.equal(score.requests(project)[0].notes[0].key, 70);
});
