"""Check the SAPI side's native importer (build/sapi/x64/import_probe.exe).

Each source must give the same .htsvoice files, byte for byte, as the Python
converter (tools/flint2htsvoice.py), with progress that only ever climbs; a
file that is not a zip, and a zip without the voices, must fail with a
message for the user. --download also fetches from SourceForge.

Usage: python tests/test_sapi_import.py <GoogleTTS.apk> [<gapps zip>] [--download]
"""
import hashlib
import os
import subprocess
import sys
import tempfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "build", "sapi", "x64", "import_probe.exe")
EXPECTED = {
    "ics_en_us.htsvoice": "a9bf107c3f6837b5f92fab6a06b6832f2a1c00a7e0fca245d50b63534862c149",
    "ics_en_gb.htsvoice": "5c3a34e6096165b70572376d90abe598b0700d4183ad8045fd5fc1b1c7ce3264",
}


def probe(*args):
    out = subprocess.run([PROBE] + list(args), capture_output=True, timeout=300)
    return out.returncode, out.stdout.decode("utf-8", "replace")


def check_import(label, args, dest):
    code, text = probe(*args)
    problems = []
    lines = text.splitlines()
    if code or not lines or not lines[-1].startswith("ok"):
        return ["failed: " + (lines[-1] if lines else "no output")]
    if any("backwards" in line for line in lines):
        problems.append("progress went backwards")
    if set(lines[-1].split()[1:]) != {"en-us", "en-gb"}:
        problems.append("voices " + lines[-1])
    for name, digest in EXPECTED.items():
        path = os.path.join(dest, name)
        if not os.path.isfile(path):
            problems.append(name + " missing")
            continue
        with open(path, "rb") as f:
            if hashlib.sha256(f.read()).hexdigest() != digest:
                problems.append(name + " differs from the Python converter")
    if any(n.endswith(".tmp") for n in os.listdir(dest)):
        problems.append("a .tmp file was left behind")
    return problems


def check_failure(label, path, dest, words):
    code, text = probe("file", path, dest)
    last = text.splitlines()[-1] if text.strip() else ""
    if code == 0 or not last.startswith("error") or not all(w in last for w in words):
        return ["expected an error about %s, got %r" % (" ".join(words), last)]
    if os.path.isdir(dest) and os.listdir(dest):
        return ["files were written for a failed import"]
    return []


def main(argv):
    paths = [a for a in argv if not a.startswith("--")]
    if not paths:
        print(__doc__)
        return 2
    cases = [("APK", ["file", paths[0]])]
    if len(paths) > 1:
        cases.append(("gapps zip", ["file", paths[1]]))
    if "--download" in argv:
        cases.append(("SourceForge download", ["download"]))
    failed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for n, (label, args) in enumerate(cases):
            dest = os.path.join(tmp, "import %d éè" % n)  # a non-ASCII folder on purpose
            problems = check_import(label, args + [dest], dest)
            print("%s: %s" % (label, "ok" if not problems else "FAIL: " + "; ".join(problems)))
            failed += bool(problems)

        not_zip = os.path.join(tmp, "not a zip.apk")
        with open(not_zip, "wb") as f:
            f.write(b"this is not a zip file" * 100)
        empty_zip = os.path.join(tmp, "other.zip")
        with zipfile.ZipFile(empty_zip, "w") as z:
            z.writestr("readme.txt", "nothing to see")
        for label, path, words in (("not a zip", not_zip, ["not a zip"]),
                                   ("zip without voices", empty_zip, ["no Google TTS voices"])):
            problems = check_failure(label, path, os.path.join(tmp, "fail " + label), words)
            print("%s: %s" % (label, "ok" if not problems else "FAIL: " + "; ".join(problems)))
            failed += bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
