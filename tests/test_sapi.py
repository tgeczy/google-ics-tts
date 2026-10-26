"""Check the registered SAPI voice through ISpVoice, as screen readers use it.

Runs build/sapi/<arch>/sapi_probe.exe (built by src/sapi/build.cmd) for each
voice in both architectures and checks:
  - the WAV has audio, at 16 kHz mono;
  - bookmarks 1..4 arrive in order, with wParam equal to the mark number;
  - event offsets never go backwards, and the stream ends where the WAV ends
    (the probe speaks at volume 0, so this also checks volume keeps timing);
  - bookmark 2 falls between the two sentences around it;
  - an abort mid-sentence stops within 100 ms.

The voice must be registered (regsvr32, both architectures) and imported.
Usage: python tests/test_sapi.py
"""
import array
import os
import re
import subprocess
import sys
import tempfile
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ABORT_LIMIT_MS = 100

EVENT = re.compile(r"^\s*([\d.]+)s (bookmark|sentence|end of stream)(.*)$")


def run_probe(arch, voice, wav):
    exe = os.path.join(ROOT, "build", "sapi", arch, "sapi_probe.exe")
    out = subprocess.run([exe, wav, voice], capture_output=True, text=True, timeout=60)
    if out.returncode:
        raise AssertionError("probe failed: " + (out.stderr or out.stdout).strip())
    return out.stdout


def check(arch, voice):
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        wav = os.path.join(tmp, "out.wav")
        text = run_probe(arch, voice, wav)
        with wave.open(wav) as w:
            rate, channels = w.getframerate(), w.getnchannels()
            samples = array.array("h", w.readframes(w.getnframes()))
    seconds = len(samples) / float(rate)
    if (rate, channels) != (16000, 1):
        problems.append("format %d Hz x%d" % (rate, channels))
    if not samples or max(abs(s) for s in samples) < 1000:
        problems.append("the WAV is silent")

    events = []
    for line in text.splitlines():
        m = EVENT.match(line)
        if m:
            events.append((float(m.group(1)), m.group(2), m.group(3).strip()))
    marks = [(t, rest) for t, kind, rest in events if kind == "bookmark"]
    names = [re.match(r'(-?\d+) "(.*)"', rest).groups() for _, rest in marks]
    if [n for _, n in names] != ["1", "2", "3", "4"]:
        problems.append("bookmarks %s" % [n for _, n in names])
    if any(w != n for w, n in names):
        problems.append("wParam differs from the mark: %s" % names)
    times = [t for t, _, _ in events]
    if times != sorted(times):
        problems.append("event offsets go backwards")
    if not events or events[-1][1] != "end of stream":
        problems.append("no end of stream event last")
    elif abs(events[-1][0] - seconds) > 0.06:
        problems.append("stream ends at %.3fs but the WAV is %.3fs" % (events[-1][0], seconds))
    sentences = [t for t, kind, _ in events if kind == "sentence"]
    if len(marks) >= 2 and len(sentences) >= 2 and not sentences[0] < marks[1][0] <= sentences[2 if len(sentences) > 2 else 1]:
        problems.append("bookmark 2 at %.3fs is outside its sentence" % marks[1][0])

    m = re.search(r"abort: stopped (\d+) ms", text)
    if not m:
        problems.append("no abort result")
    elif int(m.group(1)) > ABORT_LIMIT_MS:
        problems.append("abort took %s ms" % m.group(1))
    return problems, seconds


def main():
    failed = 0
    for arch in ("x64", "x86"):
        for voice in ("en-us", "en-gb"):
            try:
                problems, seconds = check(arch, voice)
            except Exception as e:  # a probe that cannot run is a failure too
                problems, seconds = [str(e)], 0.0
            status = "ok" if not problems else "FAIL: " + "; ".join(problems)
            print("%s %s (%.2fs of audio): %s" % (arch, voice, seconds, status))
            failed += bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
