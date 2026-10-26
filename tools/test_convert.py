"""Check that the C converter (ics_convert_voice) and flint2htsvoice.py agree byte for byte.

Usage: python tools/test_convert.py <flint voice> [<flint voice> ...]
(the voice files are assets/eng-usa/voice and assets/eng-gbr/voice from the APK)
"""
import ctypes
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DLL = os.path.join(ROOT, "addon", "engine", "x64" if struct.calcsize("P") == 8 else "x86", "ics_hts.dll")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def main():
    dll = ctypes.CDLL(DLL)
    dll.ics_convert_voice.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p]
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        for i, voice in enumerate(sys.argv[1:]):
            py_out = os.path.join(tmp, "py%d.htsvoice" % i)
            c_out = os.path.join(tmp, "c%d.htsvoice" % i)
            subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "flint2htsvoice.py"), voice, py_out],
                           check=True, stdout=subprocess.DEVNULL)
            data = open(voice, "rb").read()
            rc = dll.ics_convert_voice(data, len(data), c_out.encode("mbcs"), os.path.basename(voice).encode())
            if rc != 0:
                print("C converter failed with", rc, "on", voice)
                failures += 1
                continue
            same = sha(py_out) == sha(c_out)
            print(voice, "python", sha(py_out)[:16], "c", sha(c_out)[:16], "MATCH" if same else "DIFFER")
            failures += not same
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
