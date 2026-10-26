"""Tools menu entry for the Google TTS (Ice Cream Sandwich) voices dialog."""
import globalPluginHandler
import gui
import wx

from synthDrivers.icsTts import _gui


class GlobalPlugin(globalPluginHandler.GlobalPlugin):
	def __init__(self):
		super().__init__()
		self._item = None
		wx.CallAfter(self._add_menu_item)

	def _add_menu_item(self):
		tools = gui.mainFrame.sysTrayIcon.toolsMenu
		self._item = tools.Append(wx.ID_ANY, "%s &voices..." % _gui.TITLE)
		gui.mainFrame.sysTrayIcon.Bind(wx.EVT_MENU, lambda evt: _gui.open_voices_dialog(), self._item)

	def terminate(self):
		if self._item is not None:
			try:
				gui.mainFrame.sysTrayIcon.toolsMenu.Remove(self._item)
			except (RuntimeError, AttributeError):
				pass
			self._item = None
		super().terminate()
