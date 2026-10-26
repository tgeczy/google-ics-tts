"""Where imported voices live, and where they are looked for.

Voices are kept outside the add-on, which NVDA deletes and recreates on every
update: <NVDA config>\\googleIcsTts\\voices. The SAPI voice may also point at
a shared folder through the registry or %ProgramData%; those are searched
too, so a voice imported for one is found by the other. The add-on's own
engine folder comes last, for builds that ship voices.
"""
from __future__ import annotations

import os
import struct

PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
ADDON_DIR = os.path.dirname(os.path.dirname(PACKAGE_DIR))
ENGINE_DIR = os.path.join(ADDON_DIR, "engine")
DLL_PATH = os.path.join(ENGINE_DIR, "x64" if struct.calcsize("P") == 8 else "x86", "ics_hts.dll")
DICT_PATH = os.path.join(ENGINE_DIR, "cmudict.dict")

DATA_DIRNAME = "googleIcsTts"
SHARED_NAME = "Google ICS TTS"
REGISTRY_KEY = r"Software\Google ICS TTS"

#: voice id -> (display name, language, file name, front-end accent)
VOICES = {
	"en-us": ("English US (Female)", "en_US", "ics_en_us.htsvoice", "us"),
	"en-gb": ("English GB (Male, RP)", "en_GB", "ics_en_gb.htsvoice", "gb"),
}


def config_base() -> str:
	"""NVDA's configuration folder.

	appArgs.configPath can be relative (it is "." in some secure-screen and
	helper processes); then the folder is the one this add-on is installed in:
	<config>\\addons\\<add-on>\\synthDrivers\\icsTts.
	"""
	try:
		import globalVars

		path = globalVars.appArgs.configPath
		if path and os.path.isabs(path):
			return path
	except (ImportError, AttributeError):
		pass
	return os.path.dirname(os.path.dirname(ADDON_DIR))


def user_voices_dir() -> str:
	return os.path.join(config_base(), DATA_DIRNAME, "voices")


def _registry_dirs() -> list[str]:
	try:
		import winreg
	except ImportError:
		return []
	dirs = []
	for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
		for view in (winreg.KEY_WOW64_64KEY, winreg.KEY_WOW64_32KEY):
			try:
				with winreg.OpenKey(hive, REGISTRY_KEY, 0, winreg.KEY_READ | view) as key:
					value, _ = winreg.QueryValueEx(key, "DataPath")
			except OSError:
				continue
			if isinstance(value, str) and value:
				dirs.append(os.path.join(os.path.expandvars(value), "voices"))
	return dirs


def search_dirs() -> list[str]:
	dirs = [user_voices_dir()] + _registry_dirs()
	program_data = os.environ.get("ProgramData")
	if program_data:
		dirs.append(os.path.join(program_data, SHARED_NAME, "voices"))
	dirs.append(ENGINE_DIR)
	seen, out = set(), []
	for d in dirs:
		key = os.path.normcase(os.path.abspath(d))
		if key not in seen:
			seen.add(key)
			out.append(d)
	return out


def find_voice(voice_id: str) -> str | None:
	name = VOICES[voice_id][2]
	for d in search_dirs():
		path = os.path.join(d, name)
		if os.path.isfile(path):
			return path
	return None


def installed_voices() -> dict[str, str]:
	"""voice id -> .htsvoice path, in VOICES order."""
	found = {}
	for voice_id in VOICES:
		path = find_voice(voice_id)
		if path:
			found[voice_id] = path
	return found
