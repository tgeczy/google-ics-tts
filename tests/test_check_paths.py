"""Check tools/check_paths.py finds machine paths and leaves everything else alone.

Usage: python tests/test_check_paths.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import check_paths  # noqa: E402

# (line, should it be reported); the example paths are what the test is for.
CASES = [
    (r'x = "C:\git\stuff"', True),  # path-check: ok
    (r"p = 'D:/data/x'", True),  # path-check: ok
    (r"cd /c/git/google-ics-tts", True),  # path-check: ok
    (r'open("/mnt/c/Users/x")', True),  # path-check: ok
    (r"C:\\Users\\someone\\AppData", True),  # path-check: ok
    (r"/home/alice/src", True),  # path-check: ok
    (r'"%s^%s-%s+%s=/B:%s:/C:%s:/D:%s:"', False),
    (r'call "C:\Program Files (x86)\VS\vcvars.bat"', False),
    (r"%APPDATA%\Google ICS TTS", False),
    (r'L"Software\\Microsoft\\Speech"', False),
    (r"https://github.com/tgeczy/google-ics-tts", False),
    (r'f = r"C:\temp\a"  # path-check: ok', False),
]


def main():
    patterns = check_paths.PATTERNS + check_paths.personal_patterns()
    failures = 0
    for line, expected in CASES:
        found = bool(list(check_paths.problems_in(line, patterns)))
        if found != expected:
            print("FAIL: %r %s" % (line, "missed" if expected else "reported"))
            failures += 1
    print("checked %d failures %d" % (len(CASES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
