// paths.cpp -- see paths.h.
#include "paths.h"

#include <shlobj.h>

#include <algorithm>
#include <cwctype>

namespace ics {

const VoiceDef *find_voice_def(const char *id)
{
	for (const VoiceDef &v : kVoices)
		if (strcmp(v.id, id) == 0)
			return &v;
	return nullptr;
}

std::wstring module_dir()
{
	HMODULE self = nullptr;
	GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&module_dir), &self);
	wchar_t path[MAX_PATH * 2];
	DWORD n = GetModuleFileNameW(self, path, MAX_PATH * 2);
	std::wstring s(path, n);
	size_t slash = s.find_last_of(L'\\');
	return slash == std::wstring::npos ? s : s.substr(0, slash);
}

static std::wstring known_folder(REFKNOWNFOLDERID id)
{
	PWSTR p = nullptr;
	std::wstring out;
	if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p)
		out = p;
	CoTaskMemFree(p);
	return out;
}

std::wstring user_voices_dir()
{
	std::wstring base = known_folder(FOLDERID_RoamingAppData);
	return base.empty() ? base : base + L"\\" + kSharedName + L"\\voices";
}

std::wstring common_voices_dir()
{
	std::wstring base = known_folder(FOLDERID_ProgramData);
	return base.empty() ? base : base + L"\\" + kSharedName + L"\\voices";
}

std::wstring nvda_voices_dir()
{
	std::wstring base = known_folder(FOLDERID_RoamingAppData);
	return base.empty() ? base : base + L"\\nvda\\googleIcsTts\\voices";
}

static bool writable(const std::wstring &dir)
{
	if (dir.empty())
		return false;
	SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
	std::wstring probe = dir + L"\\.write-test";
	HANDLE f = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (f == INVALID_HANDLE_VALUE)
		return false;
	CloseHandle(f);
	return true;
}

std::wstring import_dir()
{
	std::wstring common = common_voices_dir();
	return writable(common) ? common : user_voices_dir();
}

static void registry_dirs(HKEY root, std::vector<std::wstring> &out)
{
	for (REGSAM view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
		HKEY key;
		if (RegOpenKeyExW(root, kRegistryKey, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
			continue;
		wchar_t value[MAX_PATH * 2];
		DWORD size = sizeof value, type = 0;
		if (RegQueryValueExW(key, L"DataPath", nullptr, &type, reinterpret_cast<BYTE *>(value), &size) == ERROR_SUCCESS
				&& (type == REG_SZ || type == REG_EXPAND_SZ) && size > sizeof(wchar_t)) {
			value[size / sizeof(wchar_t) - 1] = L'\0';
			wchar_t expanded[MAX_PATH * 2];
			if (ExpandEnvironmentStringsW(value, expanded, MAX_PATH * 2)) {
				// The folder that holds the voices, or the one above it.
				out.push_back(expanded);
				out.push_back(std::wstring(expanded) + L"\\voices");
			}
		}
		RegCloseKey(key);
	}
}

std::vector<std::wstring> voice_search_dirs()
{
	std::vector<std::wstring> dirs;
	registry_dirs(HKEY_CURRENT_USER, dirs);
	dirs.push_back(user_voices_dir());
	dirs.push_back(nvda_voices_dir());
	registry_dirs(HKEY_LOCAL_MACHINE, dirs);
	dirs.push_back(common_voices_dir());
	dirs.push_back(module_dir());
	std::vector<std::wstring> unique;
	for (const std::wstring &d : dirs) {
		if (d.empty())
			continue;
		std::wstring key = d;
		std::transform(key.begin(), key.end(), key.begin(), towlower);
		bool seen = false;
		for (const std::wstring &u : unique) {
			std::wstring k2 = u;
			std::transform(k2.begin(), k2.end(), k2.begin(), towlower);
			seen |= k2 == key;
		}
		if (!seen)
			unique.push_back(d);
	}
	return unique;
}

static bool is_file(const std::wstring &path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring find_voice_file(const VoiceDef &voice)
{
	for (const std::wstring &d : voice_search_dirs()) {
		std::wstring p = d + L"\\" + voice.file;
		if (is_file(p))
			return p;
	}
	return std::wstring();
}

std::wstring dictionary_path()
{
	std::wstring dir = module_dir();
	if (is_file(dir + L"\\cmudict.dict"))
		return dir + L"\\cmudict.dict";
	size_t slash = dir.find_last_of(L'\\');
	if (slash != std::wstring::npos && is_file(dir.substr(0, slash) + L"\\cmudict.dict"))
		return dir.substr(0, slash) + L"\\cmudict.dict";
	return std::wstring();
}

std::string utf8(const std::wstring &w)
{
	if (w.empty())
		return std::string();
	int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s((size_t)n, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
	return s;
}

std::wstring wide(const std::string &s)
{
	if (s.empty())
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
	std::wstring w((size_t)n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
	return w;
}

}  // namespace ics
