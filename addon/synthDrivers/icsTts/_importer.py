"""Import the Ice Cream Sandwich voices from a GoogleTTS.apk or a gapps package.

The voices are Google's and are never shipped with the add-on. They are read
from the user's own file, or from the ICS gapps package on SourceForge, and
checked against the known SHA-256 of the Android 4.0 voices before they are
converted (ics_convert_voice in ics_hts.dll) into the voices folder.

Downloading reads only what is needed: the gapps zip's directory from its
end, then GoogleTTS.apk (about 4 MB of the 52 MB package), using HTTP range
requests; servers that ignore ranges get a plain full download.
"""
from __future__ import annotations

import ctypes
import hashlib
import io
import os
import tempfile
import urllib.request
import zipfile

from . import _paths

SOURCEFORGE_URL = "https://downloads.sourceforge.net/project/touchpad/gapps-ics-20120429-signed.zip"
USER_AGENT = "google-ics-tts (NVDA add-on)"
#: voice id -> (path inside the APK, SHA-256 of the Android 4.0 voice)
VOICE_ASSETS = {
	"en-us": ("assets/eng-usa/voice", "d24123f5c6d7c5f418868b7dcea86cc02407da67fad2191b1f74cda2f0b54214"),
	"en-gb": ("assets/eng-gbr/voice", "d2a13610e8c705b81fab4d4093f30bac0ac86175158912e890b49e814ab1881f"),
}
_BLOCK = 1 << 20
_TIMEOUT = 30


class ImportFailed(Exception):
	"""Carries a message meant for the user."""


class Cancelled(Exception):
	pass


def _check(cancelled):
	if cancelled and cancelled():
		raise Cancelled()


def _find_apk(zf: zipfile.ZipFile) -> str | None:
	names = zf.namelist()
	if any(n == VOICE_ASSETS["en-us"][0] for n in names):
		return None
	apks = [n for n in names if n.lower().endswith("googletts.apk")]
	if not apks:
		raise ImportFailed(
			"This file has no Google TTS voices. Choose GoogleTTS.apk from Android 4.0, "
			"or an Ice Cream Sandwich gapps package that contains it.")
	return apks[0]


def voices_from_zip(zf: zipfile.ZipFile, progress=None, cancelled=None) -> dict[str, bytes]:
	"""voice id -> the verified Flint voice bytes, from an APK or a gapps zip."""
	apk_name = _find_apk(zf)
	if apk_name:
		if progress:
			progress(30, "Reading GoogleTTS.apk")
		apk = zipfile.ZipFile(io.BytesIO(zf.read(apk_name)))
	else:
		apk = zf
	_check(cancelled)
	voices, wrong = {}, []
	for voice_id, (member, digest) in VOICE_ASSETS.items():
		try:
			data = apk.read(member)
		except KeyError:
			continue
		if hashlib.sha256(data).hexdigest() == digest:
			voices[voice_id] = data
		else:
			wrong.append(_paths.VOICES[voice_id][0])
	if not voices:
		if wrong:
			raise ImportFailed(
				"This Google TTS is not the Android 4.0 (Ice Cream Sandwich) version; "
				"its voices are in a different format.")
		raise ImportFailed("No Ice Cream Sandwich voices were found in this file.")
	return voices


def _converter():
	dll = ctypes.CDLL(_paths.DLL_PATH)
	dll.ics_convert_voice.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p]
	return dll.ics_convert_voice


def install(voices: dict[str, bytes], dest: str | None = None, progress=None) -> list[str]:
	"""Convert into the voices folder; returns the voice ids installed."""
	dest = dest or _paths.user_voices_dir()
	os.makedirs(dest, exist_ok=True)
	convert = _converter()
	done = []
	for n, (voice_id, data) in enumerate(voices.items()):
		name = _paths.VOICES[voice_id][2]
		if progress:
			progress(70 + 25 * n // len(voices), "Converting %s" % _paths.VOICES[voice_id][0])
		target = os.path.join(dest, name)
		tmp = target + ".tmp"
		rc = convert(data, len(data), tmp.encode("mbcs"), b"voice")
		if rc != 0:
			try:
				os.remove(tmp)
			except OSError:
				pass
			raise ImportFailed("Could not convert %s (error %d)." % (_paths.VOICES[voice_id][0], rc))
		os.replace(tmp, target)
		done.append(voice_id)
	return done


def import_file(path: str, progress=None, cancelled=None, dest: str | None = None) -> list[str]:
	if progress:
		progress(10, "Opening %s" % os.path.basename(path))
	try:
		with zipfile.ZipFile(path) as zf:
			voices = voices_from_zip(zf, progress, cancelled)
	except (zipfile.BadZipFile, OSError) as e:
		raise ImportFailed("Could not read %s: %s" % (os.path.basename(path), e))
	_check(cancelled)
	return install(voices, dest, progress)


class _HttpRangeFile(io.RawIOBase):
	"""A seekable, read-only view of a remote file, fetched in blocks."""

	def __init__(self, url: str, size: int, progress, cancelled):
		super().__init__()
		self._url, self._size, self._pos = url, size, 0
		self._buf, self._buf_start = b"", 0
		self._progress, self._cancelled = progress, cancelled
		self.fetched = 0
		self.expected = 4_500_000

	def readable(self):
		return True

	def seekable(self):
		return True

	def tell(self):
		return self._pos

	def seek(self, offset, whence=io.SEEK_SET):
		base = {io.SEEK_SET: 0, io.SEEK_CUR: self._pos, io.SEEK_END: self._size}[whence]
		self._pos = max(0, base + offset)
		return self._pos

	def _fetch(self, start: int, length: int):
		_check(self._cancelled)
		end = min(self._size, start + length) - 1
		req = urllib.request.Request(self._url, headers={"User-Agent": USER_AGENT, "Range": "bytes=%d-%d" % (start, end)})
		with urllib.request.urlopen(req, timeout=_TIMEOUT) as r:
			if r.status != 206:
				raise ImportFailed("The download server stopped answering partial requests.")
			self._buf = r.read()
		self._buf_start = start
		self.fetched += len(self._buf)
		if self._progress:
			self._progress(min(60, 10 + self.fetched * 50 // max(1, self.expected)), "Downloaded %d KB" % (self.fetched // 1024))

	def readinto(self, b):
		if self._pos >= self._size:
			return 0
		n = min(len(b), self._size - self._pos)
		if not (self._buf_start <= self._pos and self._pos + n <= self._buf_start + len(self._buf)):
			self._fetch(self._pos, max(n, _BLOCK))
		off = self._pos - self._buf_start
		data = self._buf[off:off + n]
		b[:len(data)] = data
		self._pos += len(data)
		return len(data)


def download(progress=None, cancelled=None, dest: str | None = None, url: str = SOURCEFORGE_URL) -> list[str]:
	"""Fetch the voices from the ICS gapps package and install them."""
	if progress:
		progress(5, "Contacting SourceForge")
	try:
		req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT, "Range": "bytes=0-0"})
		with urllib.request.urlopen(req, timeout=_TIMEOUT) as r:
			final_url = r.geturl()
			content_range = r.headers.get("Content-Range", "")
			ranged = r.status == 206 and "/" in content_range
			if ranged:
				size = int(content_range.rsplit("/", 1)[1])
				r.read()
		_check(cancelled)
		if ranged:
			remote = _HttpRangeFile(final_url, size, progress, cancelled)
			with zipfile.ZipFile(io.BufferedReader(remote, buffer_size=1 << 16)) as zf:
				apk = _find_apk(zf)
				if apk:
					remote.expected = remote.fetched + zf.getinfo(apk).compress_size
				voices = voices_from_zip(zf, None, cancelled)
			if progress:
				progress(65, "Checking the voices")
		else:
			voices = _download_whole(final_url, progress, cancelled)
	except (OSError, ValueError, zipfile.BadZipFile) as e:
		raise ImportFailed("The download failed: %s" % e)
	_check(cancelled)
	return install(voices, dest, progress)


def _download_whole(url: str, progress, cancelled) -> dict[str, bytes]:
	req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
	with tempfile.TemporaryFile() as tmp:
		with urllib.request.urlopen(req, timeout=_TIMEOUT) as r:
			total = int(r.headers.get("Content-Length") or 0)
			got = 0
			while True:
				_check(cancelled)
				chunk = r.read(1 << 16)
				if not chunk:
					break
				tmp.write(chunk)
				got += len(chunk)
				if progress and total:
					progress(5 + got * 55 // total, "Downloaded %d of %d MB" % (got >> 20, total >> 20))
		tmp.seek(0)
		with zipfile.ZipFile(tmp) as zf:
			return voices_from_zip(zf, progress, cancelled)
