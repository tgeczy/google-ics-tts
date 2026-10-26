"""Keep machine-specific paths out of the public repo.

Absolute paths (drive letters, MSYS-style paths, home folders, an expanded
AppData folder) give away how this machine is laid out, and tests and tools
should be using paths relative to the repo anyway. The standard Program Files
and Windows folders are the same on every machine, so they are allowed.

This checks every file git would
commit -- tracked files and untracked files that are not ignored -- and
reports each line that has one, along with this machine's user name, computer
name or git e-mail address.

  python tools/check_paths.py              check the working tree
  python tools/check_paths.py --history    also check every commit and message
  python tools/check_paths.py --install-hook
                                           run the check before each commit

A line that really needs its path can say so with "path-check: ok".
Exit status is 1 when anything is found.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALLOW = "path-check: ok"

START = r"""(?:^|[\s"'`=(\[,<>|;])"""
SEP = r"(?:\\\\|\\|/)"
SYSTEM = r"(?!(?:Program Files|Windows)\b)"

# A path starts a token (so the HTS label format "/C:%s:/D:" does not count).
PATTERNS = [
    ("drive path", re.compile(START + r"[A-Za-z]:" + SEP + SYSTEM + r"[\w$%.(~-]", re.I)),
    ("MSYS/WSL path", re.compile(START + r"/(?:mnt/)?[a-z]/" + SYSTEM + r"\w", re.I)),
    ("home folder", re.compile(r"(?:\\\\|\\|/)(?:Users|home|Documents and Settings)(?:\\\\|\\|/)\w", re.I)),
    ("AppData folder", re.compile(r"(?<![%$])\bAppData(?:\\\\|\\|/)", re.I)),
]


def personal_patterns():
    """This machine's names, looked up now so they are never written down here."""
    found = []
    user = os.environ.get("USERNAME") or os.environ.get("USER")
    if user:
        sep = r"(?:\\\\|\\|/)"
        found.append(("user folder", re.compile(sep + re.escape(user) + sep, re.I)))
    host = os.environ.get("COMPUTERNAME")
    if host:
        found.append(("computer name", re.compile(r"\b" + re.escape(host) + r"\b", re.I)))
    try:
        email = subprocess.run(["git", "config", "user.email"], cwd=ROOT, capture_output=True,
                               text=True).stdout.strip()
    except OSError:
        email = ""
    if email:
        found.append(("e-mail address", re.compile(re.escape(email), re.I)))
    return found


def problems_in(text, patterns):
    for number, line in enumerate(text.splitlines(), 1):
        if ALLOW in line:
            continue
        for reason, pattern in patterns:
            if pattern.search(line):
                yield number, reason, line.strip()
                break


def git(*args):
    return subprocess.run(["git"] + list(args), cwd=ROOT, capture_output=True, check=True).stdout


def check_tree(patterns):
    count = 0
    names = git("ls-files", "-z", "--cached", "--others", "--exclude-standard").decode("utf-8").split("\0")
    for name in filter(None, names):
        path = os.path.join(ROOT, name)
        if not os.path.isfile(path):
            continue  # deleted but not yet staged
        with open(path, "rb") as f:
            data = f.read()
        if b"\0" in data[:8192]:
            continue  # binary
        for number, reason, line in problems_in(data.decode("utf-8", "replace"), patterns):
            print("%s:%d: %s: %s" % (name, number, reason, line[:160]))
            count += 1
    return count


def check_history(patterns):
    count = 0
    log = git("log", "--all", "-p", "--no-color", "--format=commit %h%n%B").decode("utf-8", "replace")
    commit = "?"
    for line in log.splitlines():
        if line.startswith("commit "):
            commit = line.split()[1]
            continue
        if line.startswith(("-", "---", "+++", "diff ", "index ", "@@")) and not line.startswith("+"):
            continue  # only what was added, and the messages
        for _, reason, text in problems_in(line, patterns):
            print("commit %s: %s: %s" % (commit, reason, text[:160]))
            count += 1
    return count


def install_hook():
    hooks = git("rev-parse", "--git-path", "hooks").decode().strip()
    path = os.path.join(ROOT, hooks, "pre-commit")
    with open(path, "w", newline="\n") as f:
        f.write("#!/bin/sh\n# Refuse commits that contain machine-specific paths.\n"
                "exec python tools/check_paths.py\n")
    print("installed %s" % os.path.relpath(path, ROOT))


def main(argv):
    if "--install-hook" in argv:
        install_hook()
        return 0
    patterns = PATTERNS + personal_patterns()
    count = check_tree(patterns)
    if "--history" in argv:
        count += check_history(patterns)
    if count:
        print("%d line(s) with machine-specific paths; use repo-relative paths, or mark a line "
              "that must keep one with \"%s\"." % (count, ALLOW))
        return 1
    print("no machine-specific paths found")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
