"""Keypress-to-first-PCM latency of the icsTts driver under NVDA fakes.

Uses the NVDA fake modules from a panthera-speech checkout (its
panthera/tests/conftest.py), which model WavePlayer stream-restart cost and
synthDoneSpeaking pacing.  Reports PCM reaching the simulated player, not sound.

    python tools/nvda_latency.py --fakes <panthera-speech>/panthera/tests/conftest.py
"""
import argparse
import importlib
import importlib.util
import os
import sys
import tempfile
import time
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fakes", required=True)
    ap.add_argument("--interval-ms", type=float, default=150)
    ap.add_argument("--text", default="hello world")
    a = ap.parse_args()

    pkg = types.ModuleType("synthDrivers")
    pkg.__path__ = [os.path.join(ROOT, "src", "addon", "synthDrivers")]
    sys.modules["synthDrivers"] = pkg
    os.environ["PANTHERA_TEST_CONFIG"] = os.path.join(tempfile.gettempdir(), "icsTts-test-config")
    spec = importlib.util.spec_from_file_location("nvda_fakes", a.fakes)
    fakes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fakes)

    player_cls = fakes.FakeWavePlayer
    first_feed = {}
    orig_feed = player_cls.feed

    def feed(self, data, size=None, onDone=None):
        if data and "t" not in first_feed:
            first_feed["t"] = time.perf_counter()
        return orig_feed(self, data, size, onDone)

    player_cls.feed = feed
    sdh = sys.modules["synthDriverHandler"]
    if not hasattr(sdh.SynthDriver, "RateBoostSetting"):
        ds = sys.modules["autoSettingsUtils.driverSetting"]
        sdh.SynthDriver.RateBoostSetting = classmethod(
            lambda cls: ds.BooleanDriverSetting("rateBoost", "Rate boos&t"))

    t0 = time.perf_counter()
    mod = importlib.import_module("synthDrivers.icsTts")
    synth = mod.SynthDriver()
    print("driver init %.0f ms" % ((time.perf_counter() - t0) * 1000))

    def run(label, items, wait_done):
        lat = []
        synth.cancel()
        for text in items:
            first_feed.clear()
            sdh.synthDoneSpeaking.arm()
            t = time.perf_counter()
            if not wait_done:
                synth.cancel()
            synth.speak([text])
            while "t" not in first_feed and time.perf_counter() - t < 5:
                time.sleep(0.001)
            if "t" in first_feed:
                lat.append((first_feed["t"] - t) * 1000)
            if wait_done:
                sdh.synthDoneSpeaking.wait(5)
            else:
                time.sleep(max(0.0, a.interval_ms / 1000 - (time.perf_counter() - t)))
        lat.sort()
        print("%-34s n=%d  median %.0f ms  worst %.0f ms" % (label, len(lat), lat[len(lat) // 2], lat[-1]))

    letters = list("thequickbrownfox")
    run("typed letters (wait for done)", letters, True)
    run("letters every %d ms (interrupting)" % a.interval_ms, letters, False)
    run("phrase", [a.text] * 5, True)
    p = synth._player
    print("player feeds %d, stops %d, stream startups %d" % (p.fed, p.stops, p.startups))
    synth.terminate()


if __name__ == "__main__":
    main()
