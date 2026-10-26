// settings.cpp -- see settings.h.
#include "settings.h"

#include <windows.h>

#include "voices.h"

namespace ics {

static bool read_dword(HKEY root, const wchar_t *name, DWORD &out)
{
	DWORD size = sizeof out;
	return RegGetValueW(root, kRegistryKey, name, RRF_RT_REG_DWORD, nullptr, &out, &size) == ERROR_SUCCESS;
}

static bool write_dword(HKEY root, const wchar_t *name, DWORD value)
{
	HKEY key;
	if (RegCreateKeyExW(root, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return false;
	bool ok = RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof value)
		== ERROR_SUCCESS;
	RegCloseKey(key);
	return ok;
}

Settings load_settings()
{
	Settings s;
	DWORD v;
	if (read_dword(HKEY_CURRENT_USER, L"ShortenPauses", v) || read_dword(HKEY_LOCAL_MACHINE, L"ShortenPauses", v))
		s.shorten_pauses = v != 0;
	return s;
}

bool save_settings(const Settings &s)
{
	write_dword(HKEY_CURRENT_USER, L"ShortenPauses", s.shorten_pauses ? 1 : 0);
	return write_dword(HKEY_LOCAL_MACHINE, L"ShortenPauses", s.shorten_pauses ? 1 : 0);
}

}  // namespace ics
