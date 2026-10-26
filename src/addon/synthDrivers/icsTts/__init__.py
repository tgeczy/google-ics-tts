from __future__ import annotations

import os
import queue
import threading
from collections import OrderedDict

import config
import nvwave
import synthDriverHandler
from autoSettingsUtils.driverSetting import BooleanDriverSetting
from logHandler import log
from speech.commands import BreakCommand, IndexCommand, PitchCommand, RateCommand, VolumeCommand
from synthDriverHandler import VoiceInfo, synthDoneSpeaking, synthIndexReached

from . import _paths
from ._engine import HtsEngine, split_sentences


#: Carries an index callback; feeding zero bytes to WASAPI is not relied on.
_MARKER = b"\0" * 4


def _outputDevice():
	"""NVDA 2025.1 moved this setting from [speech] to [audio]."""
	try:
		return config.conf["audio"]["outputDevice"]
	except KeyError:
		return config.conf["speech"]["outputDevice"]


def _clamp(v: int) -> int:
	return max(0, min(100, v))


class SynthDriver(synthDriverHandler.SynthDriver):
	name = "icsTts"
	description = "Google TTS (Ice Cream Sandwich)"

	supportedSettings = (
		synthDriverHandler.SynthDriver.VoiceSetting(),
		synthDriverHandler.SynthDriver.RateSetting(),
		synthDriverHandler.SynthDriver.RateBoostSetting(),
		synthDriverHandler.SynthDriver.PitchSetting(),
		synthDriverHandler.SynthDriver.VolumeSetting(),
		BooleanDriverSetting(
			"shortenPauses",
			"Shorten pau&ses",
			defaultVal=True,
			availableInSettingsRing=True,
			displayName="Shorten pauses",
		),
	)
	supportedCommands = {
		IndexCommand,
		BreakCommand,
		PitchCommand,
		RateCommand,
		VolumeCommand,
	}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}

	@classmethod
	def check(cls):
		# Listed even before voices are imported, so choosing it can offer the importer.
		return os.path.isfile(_paths.DLL_PATH) and os.path.isfile(_paths.DICT_PATH)

	def __init__(self):
		super().__init__()
		self._rate = 50
		self._rateBoost = False
		self._shortenPauses = True
		self._pitch = 50
		self._volume = 100
		self._generation = 0
		self._playing = False
		voices = _paths.installed_voices()
		if not voices:
			import wx
			from . import _gui

			wx.CallAfter(_gui.prompt_missing_voices)
			raise RuntimeError("no Ice Cream Sandwich voices are imported")
		self._voice = next(iter(voices))
		self._engine = self._loadEngine(self._voice)
		self._player = nvwave.WavePlayer(
			channels=1,
			samplesPerSec=self._engine.sample_rate,
			bitsPerSample=16,
			outputDevice=_outputDevice(),
		)
		self._queue: queue.Queue = queue.Queue()
		self._thread = threading.Thread(target=self._bgLoop, daemon=True, name="icsTts.BgThread")
		self._thread.start()

	def terminate(self):
		self.cancel()
		self._queue.put(None)
		self._thread.join(timeout=3)
		self._engine.shutdown()
		self._player.close()

	def speak(self, speechSequence):
		rate, pitch, volume = self._rate, self._pitch, self._volume
		segments = []
		for item in speechSequence:
			if isinstance(item, str):
				# NVDA sends name, role and description as separate strings; one
				# utterance avoids a pause and a sentence ending between each.
				prev = segments[-1] if segments else None
				if prev and prev[0] == "text" and prev[2:] == (rate, pitch, volume):
					segments[-1] = ("text", prev[1] + " " + item, rate, pitch, volume)
				else:
					segments.append(("text", item, rate, pitch, volume))
			elif isinstance(item, IndexCommand):
				segments.append(("index", item.index))
			elif isinstance(item, BreakCommand):
				segments.append(("break", item.time))
			elif isinstance(item, PitchCommand):
				pitch = self._pitch if item.isDefault else _clamp(self._pitch + item.offset)
			elif isinstance(item, RateCommand):
				rate = self._rate if item.isDefault else _clamp(self._rate + item.offset)
			elif isinstance(item, VolumeCommand):
				volume = self._volume if item.isDefault else _clamp(self._volume + item.offset)
		self._queue.put((self._generation, segments))

	def cancel(self):
		self._generation += 1
		try:
			while True:
				self._queue.get_nowait()
		except queue.Empty:
			pass
		self._engine.stop()
		if self._playing:
			self._playing = False
			self._player.stop()

	def pause(self, switch):
		self._player.pause(switch)

	def _bgLoop(self):
		while True:
			item = self._queue.get()
			if item is None:
				break
			gen, segments = item
			try:
				self._speakSegments(gen, segments)
			except Exception:
				log.error("icsTts: speech failed", exc_info=True)

	def _feed(self, gen: int, pcm: bytes, onDone=None):
		if gen != self._generation:
			return
		self._playing = True
		self._player.feed(pcm, onDone=onDone)

	def _speakSegments(self, gen: int, segments: list):
		lastText = max((i for i, seg in enumerate(segments) if seg[0] == "text"), default=-1)
		for segIndex, seg in enumerate(segments):
			if gen != self._generation:
				return
			kind = seg[0]
			if kind == "index":
				index = seg[1]
				self._feed(gen, _MARKER, onDone=lambda i=index: synthIndexReached.notify(synth=self, index=i))
			elif kind == "break":
				self._feed(gen, b"\0\0" * int(self._engine.sample_rate * seg[1] / 1000))
			else:
				_, text, rate, pitch, volume = seg
				sentences = split_sentences(text)
				for n, sentence in enumerate(sentences):
					if gen != self._generation:
						return
					pcm = self._engine.synthesize(
						sentence, self._rateToEngine(rate, self._rateBoost), self._pitchToEngine(pitch),
						volume / 100.0, cancelled=lambda: gen != self._generation,
						shorten_pauses=self._shortenPauses,
						continued=self._shortenPauses and (segIndex < lastText or n < len(sentences) - 1))
					if pcm:
						self._feed(gen, pcm)
		if gen != self._generation:
			return
		self._player.idle()
		self._playing = False
		if gen == self._generation:
			synthDoneSpeaking.notify(synth=self)

	@staticmethod
	def _loadEngine(voiceId: str) -> HtsEngine:
		accent = _paths.VOICES[voiceId][3]
		engine = HtsEngine(_paths.DLL_PATH, _paths.find_voice(voiceId), _paths.DICT_PATH, accent)
		engine.start()
		return engine

	def _get_voice(self) -> str:
		return self._voice

	def _set_voice(self, value: str):
		if value == self._voice or not _paths.find_voice(value):
			return
		self.cancel()
		old = self._engine
		self._engine = self._loadEngine(value)
		self._voice = value
		old.shutdown()

	def _getAvailableVoices(self) -> OrderedDict[str, VoiceInfo]:
		installed = _paths.installed_voices()
		return OrderedDict(
			(k, VoiceInfo(k, name, language=lang))
			for k, (name, lang, _, _) in _paths.VOICES.items() if k in installed)

	def _get_rate(self) -> int:
		return self._rate

	def _set_rate(self, value: int):
		self._rate = value

	def _get_rateBoost(self) -> bool:
		return self._rateBoost

	def _set_rateBoost(self, value: bool):
		self._rateBoost = bool(value)

	def _get_shortenPauses(self) -> bool:
		return self._shortenPauses

	def _set_shortenPauses(self, value: bool):
		self._shortenPauses = bool(value)

	def _get_pitch(self) -> int:
		return self._pitch

	def _set_pitch(self, value: int):
		self._pitch = value

	def _get_volume(self) -> int:
		return self._volume

	def _set_volume(self, value: int):
		self._volume = value

	@staticmethod
	def _rateToEngine(nvdaRate: int, boost: bool) -> float:
		"""NVDA 0-100 to a speed factor: 0 -> 0.5x, 50 -> 1x, 100 -> 2x; boost triples it."""
		speed = 2.0 ** ((nvdaRate - 50) / 50.0)
		return speed * 3.0 if boost else speed

	@staticmethod
	def _pitchToEngine(nvdaPitch: int) -> float:
		"""NVDA 0-100 to a pitch multiplier: 50 -> 1.0, one octave either way."""
		return 2.0 ** ((nvdaPitch - 50) / 50.0)
