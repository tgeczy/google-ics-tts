from __future__ import annotations

import ctypes
import math
import re
import threading

ICS_POSTFILTER = 0.8
#: (edge silence, longest pause) in ms; ICS itself used 50 ms edges.
PAUSES_NORMAL = (50, 250)
PAUSES_SHORT = (0, 100)
#: Speed beyond this comes from shorter frames rather than shorter states,
#: so fast speech doesn't hit the one-frame-per-state floor.
MAX_DURATION_FACTOR = 1.6
MIN_FRAME_PERIOD = 16
_SENTENCE_END = re.compile(r"(?<=[.!?])\s+")
ACCENTS = {"us": 0, "gb": 1}


class EngineError(Exception):
	pass


def split_sentences(text: str) -> list[str]:
	return [s for s in _SENTENCE_END.split(text) if s.strip()]


class HtsEngine:
	"""ics_hts.dll: the ICS front end and hts_engine API, voice and dictionary loaded once."""

	def __init__(self, dll_path: str, voice_path: str, dict_path: str, accent: str = "us"):
		self._lock = threading.Lock()
		self._dll_path = dll_path
		self._voice_path = voice_path
		self._dict_path = dict_path
		self._accent = accent
		self._dll = None
		self._handle = None
		self._lexicon = None
		self.sample_rate = 16000
		self._fperiod = 80
		self.postfilter = ICS_POSTFILTER
		self.noise_gain = 1.0
		self.postfilter_voiced_only = False

	def start(self):
		dll = ctypes.CDLL(self._dll_path)
		dll.ics_open.argtypes = [ctypes.c_char_p]
		dll.ics_open.restype = ctypes.c_void_p
		dll.ics_close.argtypes = [ctypes.c_void_p]
		dll.ics_lexicon_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
		dll.ics_lexicon_open.restype = ctypes.c_void_p
		dll.ics_lexicon_close.argtypes = [ctypes.c_void_p]
		dll.ics_sample_rate.argtypes = [ctypes.c_void_p]
		dll.ics_fperiod.argtypes = [ctypes.c_void_p]
		dll.ics_set_vocoder.argtypes = [ctypes.c_double, ctypes.c_int]
		dll.ics_stop.argtypes = [ctypes.c_void_p]
		dll.ics_speak.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
			ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double, ctypes.c_int, ctypes.c_int]
		dll.ics_get_samples.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_short), ctypes.c_double]
		dll.ics_trim_range.argtypes = [ctypes.c_void_p, ctypes.c_double,
			ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
		lexicon = dll.ics_lexicon_open(self._dict_path.encode("utf-8"), ACCENTS[self._accent])
		if not lexicon:
			raise EngineError("could not load dictionary %s" % self._dict_path)
		handle = dll.ics_open(self._voice_path.encode("utf-8"))
		if not handle:
			dll.ics_lexicon_close(lexicon)
			raise EngineError("could not load voice %s" % self._voice_path)
		self._dll = dll
		self._lexicon = lexicon
		self._handle = handle
		self.sample_rate = dll.ics_sample_rate(handle)
		self._fperiod = dll.ics_fperiod(handle)

	def shutdown(self):
		self.stop()
		with self._lock:
			if self._handle:
				self._dll.ics_close(self._handle)
				self._handle = None
			if self._lexicon:
				self._dll.ics_lexicon_close(self._lexicon)
				self._lexicon = None

	def stop(self):
		"""Abort a synthesis in progress (safe from any thread)."""
		if self._handle:
			self._dll.ics_stop(self._handle)

	def synthesize(self, text: str, speed: float = 1.0, pitch: float = 1.0, volume: float = 1.0,
			cancelled=None, shorten_pauses: bool = False, continued: bool = False) -> bytes:
		dur_factor = min(speed, MAX_DURATION_FACTOR)
		fperiod = max(MIN_FRAME_PERIOD, int(round(self._fperiod * dur_factor / speed)))
		edge_ms, pause_ms = PAUSES_SHORT if shorten_pauses else PAUSES_NORMAL
		if speed > 1.0:
			pause_ms = int(pause_ms / speed)
		half_tone = 12.0 * math.log2(max(0.1, pitch))
		with self._lock:
			if not self._handle or (cancelled and cancelled()):
				return b""
			self._dll.ics_set_vocoder(self.noise_gain, int(self.postfilter_voiced_only))
			nsamples = self._dll.ics_speak(self._handle, self._lexicon, text.encode("utf-8", "replace"),
				0 if continued else 1, dur_factor, fperiod, half_tone, self.postfilter, edge_ms, pause_ms)
			if nsamples <= 0:
				return b""
			samples = (ctypes.c_short * nsamples)()
			self._dll.ics_get_samples(self._handle, samples, volume)
			start, end = 0, nsamples
			if shorten_pauses:
				a, b = ctypes.c_int(), ctypes.c_int()
				self._dll.ics_trim_range(self._handle, volume, ctypes.byref(a), ctypes.byref(b))
				start, end = a.value, b.value
		return bytes(samples)[start * 2:end * 2]
