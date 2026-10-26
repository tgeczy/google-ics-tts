"""Check which release the SAPI update check picks (build/sapi/x64/import_probe.exe).

Like the NVDA add-on's: the newest non-draft, non-prerelease release that has a
googleIcsTts-sapi-X.Y.Z.exe, by the version in that file name; the digest is
GitHub's "sha256:" asset digest. Live mode also asks GitHub itself.

Usage: python tests/test_sapi_updates.py [--live]
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROBE = os.path.join(ROOT, "build", "sapi", "x64", "import_probe.exe")
BASE = "https://github.com/tgeczy/google-ics-tts/releases/download/"


def asset(name, digest=None):
    a = {"name": name, "browser_download_url": BASE + name, "uploader": {"login": "x"}}
    if digest:
        a["digest"] = digest
    return a


def release(tag, assets, draft=False, prerelease=False):
    return {"tag_name": tag, "html_url": "https://github.com/tgeczy/google-ics-tts/releases/tag/" + tag,
            "draft": draft, "prerelease": prerelease, "assets": assets, "body": "café \"quoted\" \\ ☃"}


CASES = [
    ("newest by file name", [
        release("v1.2.0", [asset("googleIcsTts-1.2.0.nvda-addon")]),  # add-on only: not a SAPI update
        release("v1.1.0", [asset("googleIcsTts-1.1.0.nvda-addon"),
                           asset("googleIcsTts-sapi-1.1.0.exe", "sha256:" + "AB" * 32)]),
        release("v1.0.10", [asset("googleIcsTts-sapi-1.0.10.exe")]),
    ], "1.1.0 " + BASE + "googleIcsTts-sapi-1.1.0.exe", "ab" * 32),
    ("skips drafts and prereleases", [
        release("v9.0.0", [asset("googleIcsTts-sapi-9.0.0.exe")], draft=True),
        release("v8.0.0", [asset("googleIcsTts-sapi-8.0.0.exe")], prerelease=True),
        release("v0.9.0", [asset("googleIcsTts-sapi-0.9.0.exe")]),
    ], "0.9.0 " + BASE + "googleIcsTts-sapi-0.9.0.exe", "-"),
    ("numeric, not text, order", [
        release("v0.9.0", [asset("googleIcsTts-sapi-0.9.0.exe")]),
        release("v0.10.0", [asset("googleIcsTts-sapi-0.10.0.exe")]),
    ], "0.10.0 ", None),
    ("nothing to offer", [release("v1.0", [asset("googleIcsTts-1.0.nvda-addon")])], None, None),
]


def run(payload):
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8") as f:
        f.write(payload)
    try:
        out = subprocess.run([PROBE, "releases", f.name], capture_output=True, timeout=30)
    finally:
        os.remove(f.name)
    return out.stdout.decode("utf-8", "replace").strip()


def main(argv):
    failed = 0
    for label, releases, want, sha in CASES:
        got = run(json.dumps(releases, ensure_ascii=False, indent=1))
        if want is None:
            ok = got == "none"
        else:
            ok = got.startswith(want) and (sha is None or got.split()[3] == sha)
        print("%s: %s" % (label, "ok" if ok else "FAIL: " + got))
        failed += not ok
    if "--live" in argv:
        import urllib.request
        req = urllib.request.Request("https://api.github.com/repos/tgeczy/google-ics-tts/releases?per_page=20",
                                     headers={"User-Agent": "google-ics-tts-test"})
        with urllib.request.urlopen(req, timeout=30) as r:
            got = run(r.read().decode("utf-8"))
        print("live GitHub: %s" % got)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
