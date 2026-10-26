"""Check GitHub for a newer release of the add-on (after Panthera's updates.py).

Only ever when somebody presses the button: nothing runs on a timer or at
start-up, so the add-on never tells a server when its owner sits down. Never
on a secure screen, where NVDA runs as SYSTEM; the caller hides the button
there, and this module refuses as well.

The version comes from the add-on asset's file name, not the release tag, so
a release can carry other files (the SAPI installer) without ever announcing
an add-on update that isn't there. Drafts and prereleases are skipped.
"""
from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
import urllib.request

RELEASES_API = "https://api.github.com/repos/tgeczy/google-ics-tts/releases?per_page=20"
RELEASES_PAGE = "https://github.com/tgeczy/google-ics-tts/releases"
ADDON_ASSET = re.compile(r"^googleIcsTts-\d[\d.]*\.nvda-addon$", re.I)
USER_AGENT = "google-ics-tts-addon"
_VERSION = re.compile(r"(\d+(?:\.\d+)*)")


def parse_version(text):
	"""A tuple of ints, or None if text has no version."""
	found = _VERSION.search(str(text or ""))
	if not found:
		return None
	return tuple(int(part) for part in found.group(1).split("."))


def is_newer(latest, installed) -> bool:
	"""Strictly higher, padding with zeros (2.1 > 2.0.9, 2.0 == 2.0.0); unparseable is False."""
	a, b = parse_version(latest), parse_version(installed)
	if not a or not b:
		return False
	width = max(len(a), len(b))
	return a + (0,) * (width - len(a)) > b + (0,) * (width - len(b))


def installed_version():
	try:
		import addonHandler

		addon = addonHandler.getCodeAddon()
		if addon and addon.version:
			return addon.version
	except Exception:
		pass
	manifest = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "manifest.ini")
	try:
		with open(manifest, encoding="utf-8") as f:
			for line in f:
				if line.strip().startswith("version"):
					return line.split("=", 1)[1].strip()
	except (OSError, IndexError):
		pass
	return None


def is_secure_screen() -> bool:
	try:
		import globalVars

		return bool(globalVars.appArgs.secure)
	except (ImportError, AttributeError):
		return False


def newest_addon(releases):
	"""(version, release page, asset url, sha256 or None) of the newest add-on asset, or Nones."""
	best = None
	for release in releases or []:
		if release.get("draft") or release.get("prerelease"):
			continue
		for asset in release.get("assets") or []:
			name = asset.get("name") or ""
			url = asset.get("browser_download_url")
			if not url or not ADDON_ASSET.match(name):
				continue
			version = parse_version(name)
			digest = asset.get("digest") or ""
			sha = digest[7:].lower() if digest.lower().startswith("sha256:") else None
			if best is None or version > best[0]:
				best = (version, release.get("html_url") or RELEASES_PAGE, url, sha)
			break
	if best is None:
		return None, None, None, None
	return (".".join(str(n) for n in best[0]),) + best[1:]


def latest_release(timeout=10, opener=None):
	"""(version, page, asset url, sha256) -- or (None, reason, None, None)."""
	if is_secure_screen():
		return None, "not while NVDA is on a secure screen", None, None
	try:
		if opener is None:
			def opener(url):
				request = urllib.request.Request(url, headers={
					"User-Agent": USER_AGENT, "Accept": "application/vnd.github+json"})
				with urllib.request.urlopen(request, timeout=timeout) as r:
					return r.read()

		payload = json.loads(opener(RELEASES_API).decode("utf-8"))
	except Exception as e:
		return None, str(e) or e.__class__.__name__, None, None
	if isinstance(payload, dict):
		payload = [payload]
	result = newest_addon(payload)
	if result[0] is None:
		return None, "no release offers the add-on yet", None, None
	return result


def fetch(url: str, sha256: str | None = None, timeout=60) -> str:
	"""Download into %TEMP% and return the path; a stale file of the same name goes first."""
	if is_secure_screen():
		raise RuntimeError("not while NVDA is on a secure screen")
	name = url.rsplit("/", 1)[-1]
	if not ADDON_ASSET.match(name):
		name = "googleIcsTts-update.nvda-addon"
	path = os.path.join(tempfile.gettempdir(), name)
	try:
		os.remove(path)
	except OSError:
		pass
	request = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
	with urllib.request.urlopen(request, timeout=timeout) as r:
		data = r.read()
	if sha256 and hashlib.sha256(data).hexdigest() != sha256:
		raise ValueError("the download does not match the release's checksum")
	with open(path, "wb") as f:
		f.write(data)
	return path
