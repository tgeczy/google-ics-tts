"""The voices dialog (Tools menu), the missing-voices prompt, and update checks."""
from __future__ import annotations

import os
import threading

import gui
import synthDriverHandler
import ui
import wx
from gui import guiHelper
from logHandler import log

from . import _importer, _paths, _updates

TITLE = "Google TTS (Ice Cream Sandwich)"
SYNTH_NAME = "icsTts"
_prompt_active = False


def _secure() -> bool:
	return _updates.is_secure_screen()


def _voice_summary() -> str:
	found = _paths.installed_voices()
	lines = []
	for voice_id, (name, _lang, _file, _accent) in _paths.VOICES.items():
		lines.append("%s: %s" % (name, "installed" if voice_id in found else "not installed"))
	return "\n".join(lines)


def open_voices_dialog():
	"""Open (or focus) the voices dialog; on a secure screen, only explain."""
	if _secure():
		gui.messageBox(
			"Voices can only be imported from your own Windows session, not from a secure screen.\n\n"
			+ _voice_summary(), TITLE, wx.OK | wx.ICON_INFORMATION)
		return
	if VoicesDialog.instance:
		VoicesDialog.instance.Raise()
		return
	gui.mainFrame.prePopup()
	VoicesDialog(gui.mainFrame).Show()
	gui.mainFrame.postPopup()


def prompt_missing_voices():
	"""Offer the importer when the synth was chosen with no voices imported."""
	global _prompt_active
	if _prompt_active or _secure() or VoicesDialog.instance:
		return
	_prompt_active = True
	try:
		answer = gui.messageBox(
			"No Ice Cream Sandwich voices are imported yet, so NVDA kept your current synthesizer.\n\n"
			"The voices belong to Google and are not included. Open the voice importer to download them "
			"from SourceForge or read them from your own Android 4.0 GoogleTTS.apk?\n\n"
			"You can also open it later from NVDA menu, Tools, %s voices." % TITLE,
			TITLE, wx.YES_NO | wx.ICON_INFORMATION)
		if answer == wx.YES:
			open_voices_dialog()
	finally:
		_prompt_active = False


class VoicesDialog(wx.Dialog):
	instance = None

	def __init__(self, parent):
		super().__init__(parent, title="%s voices" % TITLE)
		VoicesDialog.instance = self
		self._busy = False
		self._cancel = threading.Event()
		main = wx.BoxSizer(wx.VERTICAL)
		helper = guiHelper.BoxSizerHelper(self, orientation=wx.VERTICAL)
		self.installed = helper.addLabeledControl(
			"Installed voices:", wx.TextCtrl, style=wx.TE_MULTILINE | wx.TE_READONLY, size=(420, 60))
		buttons = guiHelper.ButtonHelper(wx.VERTICAL)
		self.download = buttons.addButton(self, label="&Download the voices from SourceForge")
		self.from_file = buttons.addButton(self, label="&Import from a GoogleTTS.apk or gapps file...")
		self.folder = buttons.addButton(self, label="Open the voices &folder")
		self.updates = buttons.addButton(self, label="Check for &updates")
		helper.addItem(buttons)
		self.status = helper.addLabeledControl(
			"Status:", wx.TextCtrl, style=wx.TE_MULTILINE | wx.TE_READONLY, size=(420, 60))
		helper.addDialogDismissButtons(wx.CLOSE)
		main.Add(helper.sizer, border=guiHelper.BORDER_FOR_DIALOGS, flag=wx.ALL)
		self.SetSizer(main)
		main.Fit(self)
		self.download.Bind(wx.EVT_BUTTON, self._on_download)
		self.from_file.Bind(wx.EVT_BUTTON, self._on_file)
		self.folder.Bind(wx.EVT_BUTTON, self._on_folder)
		self.updates.Bind(wx.EVT_BUTTON, self._on_updates)
		self.Bind(wx.EVT_BUTTON, lambda evt: self.Close(), id=wx.ID_CLOSE)
		self.Bind(wx.EVT_CLOSE, self._on_close)
		self.EscapeId = wx.ID_CLOSE
		self._refresh()
		self.status.SetValue("Ready.")
		self.CentreOnScreen()
		(self.download if not _paths.installed_voices() else self.installed).SetFocus()

	def _refresh(self):
		self.installed.SetValue(_voice_summary())

	def _set_busy(self, busy: bool):
		self._busy = busy
		for button in (self.download, self.from_file, self.updates):
			button.Enable(not busy)

	def _progress(self, percent: int, message: str):
		wx.CallAfter(self._show_progress, percent, message)

	def _show_progress(self, percent: int, message: str):
		if not self:
			return
		self.status.SetValue("%d%%: %s" % (percent, message))
		milestone = percent // 25
		if milestone > self._milestone:
			self._milestone = milestone
			ui.message("%d percent" % (milestone * 25))

	def _run(self, work, done):
		"""work() on a worker thread; done(result, error) back on the GUI thread."""
		self._set_busy(True)
		self._cancel.clear()
		self._milestone = 0

		def target():
			result = error = None
			try:
				result = work()
			except Exception as e:
				error = e
			wx.CallAfter(self._finish, done, result, error)

		threading.Thread(target=target, daemon=True, name="googleIcsTts.import").start()

	def _finish(self, done, result, error):
		if not self:
			return
		self._set_busy(False)
		done(result, error)

	def _on_download(self, evt):
		self.status.SetValue("Contacting SourceForge...")
		self._run(lambda: _importer.download(self._progress, self._cancel.is_set), self._imported)

	def _on_file(self, evt):
		with wx.FileDialog(self, "Choose GoogleTTS.apk or an Ice Cream Sandwich gapps package",
				wildcard="Android packages (*.apk;*.zip)|*.apk;*.zip|All files (*.*)|*.*",
				style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as dlg:
			if dlg.ShowModal() != wx.ID_OK:
				return
			path = dlg.GetPath()
		self._run(lambda: _importer.import_file(path, self._progress, self._cancel.is_set), self._imported)

	def _imported(self, ids, error):
		if isinstance(error, _importer.Cancelled):
			self.status.SetValue("Cancelled.")
			return
		if error:
			if not isinstance(error, _importer.ImportFailed):
				log.error("googleIcsTts: import failed", exc_info=error)
			message = str(error) or error.__class__.__name__
			self.status.SetValue("Import failed: %s" % message)
			gui.messageBox(message, "%s: import failed" % TITLE, wx.OK | wx.ICON_ERROR, self)
			return
		self._refresh()
		names = ", ".join(_paths.VOICES[v][0] for v in ids)
		self.status.SetValue("Installed: %s." % names)
		current = synthDriverHandler.getSynth()
		if current and current.name == SYNTH_NAME:
			gui.messageBox("Installed: %s." % names, TITLE, wx.OK | wx.ICON_INFORMATION, self)
			return
		answer = gui.messageBox(
			"Installed: %s.\n\nSwitch NVDA to %s now?" % (names, TITLE), TITLE, wx.YES_NO | wx.ICON_QUESTION, self)
		if answer == wx.YES and not synthDriverHandler.setSynth(SYNTH_NAME):
			gui.messageBox("NVDA could not start %s; see the NVDA log." % TITLE, TITLE, wx.OK | wx.ICON_ERROR, self)

	def _on_folder(self, evt):
		folder = _paths.user_voices_dir()
		os.makedirs(folder, exist_ok=True)
		os.startfile(folder)

	def _on_updates(self, evt):
		self.status.SetValue("Checking for updates...")
		self._run(lambda: _updates.latest_release(), self._checked)

	def _checked(self, result, error):
		version, page, url, sha = result if result else (None, str(error), None, None)
		installed = _updates.installed_version()
		if version is None:
			self.status.SetValue("Could not check for updates: %s" % page)
			return
		if not _updates.is_newer(version, installed):
			self.status.SetValue("You have the newest version (%s)." % installed)
			ui.message("No update available")
			return
		self.status.SetValue("Version %s is available; you have %s." % (version, installed))
		answer = gui.messageBox(
			"Version %s of %s is available; you have %s.\n\nDownload and install it?" % (version, TITLE, installed),
			TITLE, wx.YES_NO | wx.ICON_QUESTION, self)
		if answer != wx.YES:
			return
		self.status.SetValue("Downloading version %s..." % version)
		self._run(lambda: _updates.fetch(url, sha), lambda path, err: self._fetched(path, err, page))

	def _fetched(self, path, error, page):
		if error:
			self.status.SetValue("The download failed: %s" % error)
			if gui.messageBox("The download failed: %s\n\nOpen the releases page instead?" % error,
					TITLE, wx.YES_NO | wx.ICON_ERROR, self) == wx.YES:
				os.startfile(page)
			return
		self.status.SetValue("Downloaded; NVDA will ask before installing.")
		try:
			from gui import addonGui

			wx.CallAfter(addonGui.handleRemoteAddonInstall, path)
		except (ImportError, AttributeError):
			os.startfile(path)

	def _on_close(self, evt):
		self._cancel.set()
		VoicesDialog.instance = None
		self.Destroy()
