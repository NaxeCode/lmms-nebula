"""Exercise the offline studio on desktop and mobile using Playwright's Chromium."""

import os
from pathlib import Path
import struct
import tempfile
import wave
import xml.etree.ElementTree as ET

from playwright.sync_api import expect, sync_playwright

from check_score import WEB, check


def save_download(page, button, path):
    with page.expect_download() as result:
        page.locator(button).click()
    result.value.save_as(path)


def peak(path):
    with wave.open(str(path)) as audio:
        samples = audio.readframes(audio.getnframes())
        return max(abs(v[0]) for v in struct.iter_unpack("<h", samples))


def exercise(browser, size, directory):
    page = browser.new_page(viewport=size, accept_downloads=True)
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    try:
        page.goto((WEB / "index.html").as_uri())
        expect(page.locator(".track")).to_have_count(4)
        page.locator("#play").click()
        expect(page.locator("#play")).to_have_text("Ⅱ Pause")
        page.wait_for_function('document.querySelector("#position").value !== "1.1"')
        page.locator("#play").click()
        page.locator("#stop").click()
        expect(page.locator("#position")).to_have_text("1.1")

        page.get_by_role("button", name="Tide chords, bar 3", exact=True).click()
        expect(page.locator("#part-title")).to_have_text("Tide chords")
        page.locator("#pitch").select_option("66")
        page.locator("#start").select_option(index=2)
        page.locator("#duration").select_option("3")
        page.locator("#velocity").fill("91")
        page.locator("#velocity").press("Tab")
        page.locator("#tempo").fill("112")
        page.locator("#tempo").press("Tab")
        page.get_by_role("button", name="Mute Harbor bass", exact=True).click()
        for action in ("add", "remove", "undo", "undo"):
            page.locator("#" + action).click()

        save_download(page, "#mmp", directory / "edited.mmp")
        save_download(page, "#commands", directory / "edited.jsonl")
        requests = check(directory / "edited.mmp", directory / "edited.jsonl")
        assert any(n["key"] == 66 and n["pos"] == 408 and n["len"] == 36 and n["vol"] == 91
                   for n in requests[2]["notes"])
        root = ET.parse(directory / "edited.mmp").getroot()
        assert root.find("head").get("bpm") == "112"
        bass = root.find(".//track[@name='Harbor bass']")
        assert bass.get("muted") == "1"

        save_download(page, "#render", directory / "edited.wav")
        with wave.open(str(directory / "edited.wav")) as audio:
            assert audio.getframerate() == 44100 and audio.getnchannels() == 2
            assert audio.getnframes() == round(16 * 60 / 112 * 44100)
        assert peak(directory / "edited.wav") > 2000
        if size["width"] > 1000:
            page.reload()
            mutes = page.locator(".mute")
            for mute in mutes.all():
                mute.click()
            output = directory / "isolated.wav"
            save_download(page, "#render", output)
            assert peak(output) == 0
            for mute in mutes.all():
                mute.click()
                save_download(page, "#render", output)
                assert peak(output) > 100
                mute.click()
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        assert not errors, errors
        print(f'{size["width"]} × {size["height"]}: playback, edits, undo and three exports verified.')
    finally:
        page.close()


if __name__ == "__main__":
    with sync_playwright() as playwright, tempfile.TemporaryDirectory() as work:
        browser = playwright.chromium.launch(executable_path=os.environ.get("CHROMIUM_EXECUTABLE"))
        try:
            for width, height in [(1440, 1000), (390, 844)]:
                exercise(browser, {"width": width, "height": height}, Path(work))
        finally:
            browser.close()
