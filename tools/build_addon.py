"""Package the NVDA add-on into build/googleIcsTts-<version>.nvda-addon.

No voices are packaged: they are imported from the user's own APK.
Build native/ first (native\\build.cmd).
"""
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

#: path in the package -> source path relative to the repository
FILES = {
    "manifest.ini": "addon/manifest.ini",
    "licenses/LICENSE.txt": "LICENSE",
    "licenses/NOTICES.txt": "addon/licenses/NOTICES.txt",
    "licenses/hts_engine_API.txt": "addon/licenses/hts_engine_API.txt",
    "licenses/cmudict.txt": "addon/licenses/cmudict.txt",
    "engine/x64/ics_hts.dll": "addon/engine/x64/ics_hts.dll",
    "engine/x86/ics_hts.dll": "addon/engine/x86/ics_hts.dll",
    "engine/cmudict.dict": "data/cmudict.dict",
    "synthDrivers/icsTts/__init__.py": "addon/synthDrivers/icsTts/__init__.py",
    "synthDrivers/icsTts/_engine.py": "addon/synthDrivers/icsTts/_engine.py",
    "synthDrivers/icsTts/_paths.py": "addon/synthDrivers/icsTts/_paths.py",
    "synthDrivers/icsTts/_importer.py": "addon/synthDrivers/icsTts/_importer.py",
    "synthDrivers/icsTts/_updates.py": "addon/synthDrivers/icsTts/_updates.py",
    "synthDrivers/icsTts/_gui.py": "addon/synthDrivers/icsTts/_gui.py",
    "globalPlugins/googleIcsTts.py": "addon/globalPlugins/googleIcsTts.py",
}


def main():
    manifest = open(os.path.join(ROOT, "addon", "manifest.ini"), encoding="utf-8").read()
    version = re.search(r"^version\s*=\s*(\S+)", manifest, re.M).group(1)
    missing = [src for src in FILES.values() if not os.path.isfile(os.path.join(ROOT, src))]
    if missing:
        sys.exit("missing: %s (run native\\build.cmd first)" % ", ".join(missing))
    out_dir = os.path.join(ROOT, "build")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, "googleIcsTts-%s.nvda-addon" % version)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for arc, src in FILES.items():
            z.write(os.path.join(ROOT, src), arc)
    print(out, os.path.getsize(out), "bytes")


if __name__ == "__main__":
    main()
