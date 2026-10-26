"""Check that the C speech path matches the reference path sample for sample.

Reference: tools/reference front end -> labels -> ics_synth -> Python trim.
C path: HtsEngine.synthesize (ics_speak + ics_trim_range).

Usage: python tools/test_speak.py <us.htsvoice> [<gb.htsvoice>]
"""
import array
import ctypes
import math
import os
import re
import struct
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DICT = os.path.join(ROOT, "data", "cmudict.dict")
DLL = os.path.join(ROOT, "addon", "engine", "x64" if struct.calcsize("P") == 8 else "x86", "ics_hts.dll")

for name, path in (("icsref", os.path.join(ROOT, "tools", "reference")),
                   ("icsTts", os.path.join(ROOT, "addon", "synthDrivers", "icsTts"))):
    pkg = types.ModuleType(name)
    pkg.__path__ = [path]
    sys.modules[name] = pkg
from icsref import _icsfront as ref  # noqa: E402
from icsTts import _engine  # noqa: E402

TEXTS = [
    "Hello, this is the Android Ice Cream Sandwich voice, speaking again after fourteen years.",
    "Run dialog Type the name of a program, folder, document, or Internet resource.",
    "open colon,", "p", "Please tap the top step, Peter.", "Left paren, semicolon; caret: done!",
    "blorptastic NVDA 2026", "a", ",",
]


def reference(eng, lex, text, speed, pitch, shorten, continued):
    labels = ref.text_to_labels(lex, text, None if continued else "s")
    if len(labels) <= 2:
        return b""
    dur = min(speed, _engine.MAX_DURATION_FACTOR)
    fperiod = max(_engine.MIN_FRAME_PERIOD, int(round(eng._fperiod * dur / speed)))
    edge, pause = _engine.PAUSES_SHORT if shorten else _engine.PAUSES_NORMAL
    if speed > 1.0:
        pause = int(pause / speed)
    caps = []
    for i, lab in enumerate(labels):
        sil = re.match(r"\^[^-]*-sil\+", lab)
        caps.append(0 if not sil else max(1, edge) if i in (0, len(labels) - 1) else pause)
    dll = eng._dll
    dll.ics_synth.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_int),
                              ctypes.c_int, ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double]
    n = len(labels)
    ns = dll.ics_synth(eng._handle, (ctypes.c_char_p * n)(*[l.encode() for l in labels]),
                       (ctypes.c_int * n)(*caps), n, dur, fperiod, 12.0 * math.log2(pitch), eng.postfilter)
    if ns <= 0:
        return b""
    buf = (ctypes.c_short * ns)()
    dll.ics_get_samples(eng._handle, buf, 1.0)
    pcm = bytes(buf)
    if shorten:
        s = array.array("h", pcm)
        fr = eng.sample_rate // 200
        nf = len(s) // fr
        if nf >= 3:
            e = [sum(x * x for x in s[i * fr:(i + 1) * fr]) for i in range(nf)]
            th = max(e) * 0.002
            first = next(i for i, v in enumerate(e) if v > th)
            last = max(i for i, v in enumerate(e) if v > th)
            pcm = s[max(0, first - 1) * fr:min(nf, last + 2) * fr].tobytes()
    return pcm


def main():
    failures = checked = 0
    for voice, accent in zip(sys.argv[1:3], ("us", "gb")):
        eng = _engine.HtsEngine(DLL, voice, DICT, accent)
        eng.start()
        lex = ref.Lexicon(DICT, accent)
        for text in TEXTS:
            for speed, pitch in ((1.0, 1.0), (0.6, 1.3), (2.0, 0.8), (5.0, 1.0)):
                for shorten in (False, True):
                    for continued in (False, True):
                        want = reference(eng, lex, text, speed, pitch, shorten, continued)
                        got = eng.synthesize(text, speed, pitch, 1.0, shorten_pauses=shorten, continued=continued)
                        checked += 1
                        if want != got:
                            failures += 1
                            print("MISMATCH", accent, repr(text), speed, pitch, shorten, continued, len(want), len(got))
        eng.shutdown()
    print("checked", checked, "failures", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
