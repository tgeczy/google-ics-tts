"""Speak text with the add-on's engine code outside NVDA and save a WAV.

Usage: python ics_say.py out.wav "text" [--rate R] [--pitch P] [--postfilter B]
                                         [--noise G] [--pf-voiced-only]
"""
import argparse
import os
import struct
import sys
import time
import types
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PKG_DIR = os.path.join(ROOT, "addon", "synthDrivers", "icsTts")
ENGINE_DIR = os.path.join(ROOT, "addon", "engine")

# Load the add-on's modules without its NVDA-only __init__.
pkg = types.ModuleType("icsTts")
pkg.__path__ = [PKG_DIR]
sys.modules["icsTts"] = pkg
from icsTts._engine import HtsEngine  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("text")
    ap.add_argument("--rate", type=float, default=1.0)
    ap.add_argument("--pitch", type=float, default=1.0)
    ap.add_argument("--postfilter", type=float, default=0.8)
    ap.add_argument("--noise", type=float, default=1.0)
    ap.add_argument("--pf-voiced-only", action="store_true")
    ap.add_argument("--voice", choices=["us", "gb"], default="us")
    a = ap.parse_args()
    eng = HtsEngine(os.path.join(ENGINE_DIR, "x64" if struct.calcsize("P") == 8 else "x86", "ics_hts.dll"),
                    os.path.join(ENGINE_DIR, "ics_en_%s.htsvoice" % a.voice),
                    os.path.join(ENGINE_DIR, "cmudict.dict"), a.voice)
    t0 = time.perf_counter()
    eng.start()
    t1 = time.perf_counter()
    eng.postfilter = a.postfilter
    eng.noise_gain = a.noise
    eng.postfilter_voiced_only = a.pf_voiced_only
    pcm = eng.synthesize(a.text, a.rate, a.pitch)
    t2 = time.perf_counter()
    with wave.open(a.out, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(eng.sample_rate)
        w.writeframes(pcm)
    print("load %.2fs, synth %.3fs, audio %.2fs -> %s" % (
        t1 - t0, t2 - t1, len(pcm) / 2 / eng.sample_rate, a.out))
    eng.shutdown()


if __name__ == "__main__":
    main()
