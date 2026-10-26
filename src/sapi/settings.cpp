// settings.cpp -- see settings.h.
#include "settings.h"

#include <windows.h>

#include "voices.h"

namespace ics {

static const REGSAM kViews[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};

static bool read_dword(HKEY root, const wchar_t *name, DWORD &out)
{
	DWORD size = sizeof out;
	return RegGetValueW(root, kRegistryKey, name, RRF_RT_REG_DWORD, nullptr, &out, &size) == ERROR_SUCCESS;
}

static bool open_for_write(HKEY root, REGSAM view, HKEY &key)
{
	return RegCreateKeyExW(root, kRegistryKey, 0, nullptr, 0, KEY_SET_VALUE | view, nullptr, &key, nullptr)
		== ERROR_SUCCESS;
}

// Writes (or with data null, deletes) a value in HKCU, then in both HKLM views.
static bool write_value(const wchar_t *name, DWORD type, const void *data, DWORD size)
{
	auto put = [&](HKEY key) {
		LONG rc = data ? RegSetValueExW(key, name, 0, type, static_cast<const BYTE *>(data), size)
		               : RegDeleteValueW(key, name);
		return rc == ERROR_SUCCESS || (!data && rc == ERROR_FILE_NOT_FOUND);
	};
	HKEY key;
	if (open_for_write(HKEY_CURRENT_USER, 0, key)) {
		put(key);
		RegCloseKey(key);
	}
	bool machine = true;
	for (REGSAM view : kViews) {
		if (!open_for_write(HKEY_LOCAL_MACHINE, view, key)) {
			machine = false;
			continue;
		}
		machine &= put(key);
		RegCloseKey(key);
	}
	return machine;
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
	DWORD v = s.shorten_pauses ? 1 : 0;
	return write_value(L"ShortenPauses", REG_DWORD, &v, sizeof v);
}

std::wstring load_data_path()
{
	wchar_t value[MAX_PATH * 2];
	DWORD size = sizeof value;
	if (RegGetValueW(HKEY_CURRENT_USER, kRegistryKey, L"DataPath", RRF_RT_REG_SZ | RRF_NOEXPAND, nullptr, value, &size)
			!= ERROR_SUCCESS)
		return std::wstring();
	return value;
}

bool save_data_path(const std::wstring &path)
{
	if (path.empty())
		return write_value(L"DataPath", REG_SZ, nullptr, 0);
	return write_value(L"DataPath", REG_SZ, path.c_str(), (DWORD)((path.size() + 1) * sizeof(wchar_t)));
}

}  // namespace ics
