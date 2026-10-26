"""Check the C front end (ics_hts.dll) against the Python reference front end.

Compares labels for every CMUdict word and a set of sentences, in both
accents. Usage: python tools/test_front.py [--quick]
"""
import ctypes
import os
import random
import struct
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DICT = os.path.join(ROOT, "data", "cmudict.dict")
DLL = os.path.join(ROOT, "addon", "engine", "x64" if struct.calcsize("P") == 8 else "x86", "ics_hts.dll")
REF_DIR = os.path.join(ROOT, "tools", "reference")

pkg = types.ModuleType("icsref")
pkg.__path__ = [REF_DIR]
sys.modules["icsref"] = pkg
from icsref import _icsfront as ref  # noqa: E402

SENTENCES = [
    "Hello, this is the Android Ice Cream Sandwich voice, speaking again after fourteen years.",
    "Welcome to NVDA. Desktop, list, Recycle Bin, one of twelve.",
    "Run dialog Type the name of a program, folder, document, or Internet resource, and Windows will open it for you.",
    "open colon,", "Left paren, right paren, semicolon, caret.", "It’s what's there; isn't it? Yes!",
    "Please tap the top step, Peter. Twenty six people played twelve tracks.",
    "The year 2026 had 365 days and 1234567890123 is a long number, 007.",
    "USB, NASA, DON'T, I'm, a, I, x, Q.", "blorptastic frobnicator zzyzx qwrtp", "aalborg d'artagnan",
    "  , , leading commas.", "a,,b", "!!!", "", "   ", "été café naïve",
    "dialog catalog talk call dog frog thought more car care near sure bird water father hot start very fire hour ask",
    "what of from because", "3rd 21st 100% $5", "can cause cos the a an i_pronoun",
]


def c_lib():
    dll = ctypes.CDLL(DLL)
    dll.ics_lexicon_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
    dll.ics_lexicon_open.restype = ctypes.c_void_p
    dll.ics_labels.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]
    dll.ics_free.argtypes = [ctypes.c_void_p]
    return dll


def c_labels(dll, lex, text, final):
    out = ctypes.c_void_p()
    n = dll.ics_labels(lex, text.encode("utf-8"), final, ctypes.byref(out))
    if n < 0:
        raise RuntimeError("ics_labels failed")
    s = ctypes.string_at(out.value).decode("ascii") if out.value else ""
    dll.ics_free(out)
    return s.split("\n") if s else []


def main():
    quick = "--quick" in sys.argv
    dll = c_lib()
    words = list(ref.Lexicon(DICT).entries)
    if quick:
        words = random.Random(1).sample(words, 3000)
    rng = random.Random(7)
    oov = ["".join(rng.choice("abcdefghijklmnopqrstuvwxyz") for _ in range(rng.randint(3, 11))) for _ in range(2000)]
    failures = 0
    for accent, code in (("us", 0), ("gb", 1)):
        pylex = ref.Lexicon(DICT, accent)
        clex = dll.ics_lexicon_open(DICT.encode("mbcs"), code)
        checked = 0
        cases = [(t, f) for t in SENTENCES for f in ("s", None)] + [(w, "s") for w in words + oov]
        for text, final in cases:
            want = ref.text_to_labels(pylex, text, final)
            got = c_labels(dll, clex, text, 1 if final == "s" else 0)
            checked += 1
            if want != got:
                failures += 1
                if failures <= 8:
                    print("MISMATCH [%s] %r" % (accent, text))
                    for w, g in zip(want + [""] * len(got), got + [""] * len(want)):
                        if w != g:
                            print("   py:", w)
                            print("    c:", g)
                            break
        print("%s: %d cases checked" % (accent, checked))
    print("FAILURES:", failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
