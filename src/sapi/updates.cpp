// updates.cpp -- see updates.h.
#include "updates.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <regex>
#include <vector>

#include "http.h"
#include "json.h"
#include "paths.h"
#include "version.h"

namespace ics {

static const wchar_t kReleasesApi[] = L"https://api.github.com/repos/tgeczy/google-ics-tts/releases?per_page=20";
static const wchar_t kReleasesPage[] = L"https://github.com/tgeczy/google-ics-tts/releases";
static const std::wregex kInstaller(L"^googleIcsTts-sapi-(\\d[\\d.]*?)\\.exe$", std::regex::icase);

const wchar_t *installed_version() { return ICS_VERSION; }

static std::vector<long> parts(const std::wstring &v)
{
	std::vector<long> out;
	size_t i = 0;
	while (i < v.size() && !iswdigit(v[i]))
		i++;
	while (i < v.size() && iswdigit(v[i])) {
		wchar_t *end = nullptr;
		out.push_back(wcstol(v.c_str() + i, &end, 10));
		i = end - v.c_str();
		if (i < v.size() && v[i] == L'.' && i + 1 < v.size() && iswdigit(v[i + 1]))
			i++;
		else
			break;
	}
	return out;
}

bool is_newer(const std::wstring &latest, const std::wstring &installed)
{
	std::vector<long> a = parts(latest), b = parts(installed);
	if (a.empty() || b.empty())
		return false;
	size_t width = std::max(a.size(), b.size());
	a.resize(width);
	b.resize(width);
	return a > b;
}

bool newest_installer(const std::string &text, Release &out)
{
	Json root;
	if (!parse_json(text, root))
		return false;
	std::vector<const Json *> releases;
	if (root.type == Json::Array)
		for (const Json &r : root.items)
			releases.push_back(&r);
	else if (root.type == Json::Object)
		releases.push_back(&root);
	bool found = false;
	for (const Json *r : releases) {
		if ((*r)["draft"].truthy() || (*r)["prerelease"].truthy())
			continue;
		for (const Json &asset : (*r)["assets"].items) {
			std::wstring name = wide(asset["name"].string);
			std::wstring url = wide(asset["browser_download_url"].string);
			std::wsmatch m;
			if (url.empty() || !std::regex_match(name, m, kInstaller))
				continue;
			std::wstring version = m[1].str();
			if (!found || is_newer(version, out.version)) {
				found = true;
				out.version = version;
				out.url = url;
				std::wstring page = wide((*r)["html_url"].string);
				out.page = page.empty() ? kReleasesPage : page;
				std::string digest = asset["digest"].string;
				out.sha256.clear();
				if (digest.size() > 7 && _strnicmp(digest.c_str(), "sha256:", 7) == 0)
					for (size_t k = 7; k < digest.size(); k++)
						out.sha256 += (char)tolower((unsigned char)digest[k]);
			}
			break;
		}
	}
	return found;
}

bool latest_release(Release &out, std::wstring *error)
{
	Http http;
	std::string body;
	if (!http.get_text(kReleasesApi, body, L"application/vnd.github+json")) {
		if (error)
			*error = http.error();
		return false;
	}
	if (!newest_installer(body, out)) {
		if (error)
			*error = L"No release offers the SAPI voice yet.";
		return false;
	}
	return true;
}

std::wstring fetch_installer(const Release &r, const Progress &progress, const Cancelled &cancelled,
                             std::wstring *error)
{
	wchar_t temp[MAX_PATH + 1];
	DWORD n = GetTempPathW(MAX_PATH + 1, temp);
	std::wstring path = std::wstring(temp, n) + L"googleIcsTts-sapi-" + r.version + L".exe";
	DeleteFileW(path.c_str());  // a stale or half-finished copy goes first

	Http http;
	std::vector<char> data;
	HttpReply reply;
	bool ok = http.get(r.url, 0, 0, [&](const char *p, size_t k, const HttpReply &rep) {
		data.insert(data.end(), p, p + k);
		if (progress && rep.content_length)
			progress((int)(data.size() * 100 / rep.content_length),
				L"Downloaded " + std::to_wstring(data.size() / 1024) + L" KB");
		return !(cancelled && cancelled());
	}, reply);
	if (cancelled && cancelled())
		return std::wstring();
	if (!ok) {
		if (error)
			*error = http.error();
		return std::wstring();
	}
	if (!r.sha256.empty() && sha256_hex(data.data(), data.size()) != r.sha256) {
		if (error)
			*error = L"The download does not match the release's checksum.";
		return std::wstring();
	}
	HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	DWORD written = 0;
	bool saved = f != INVALID_HANDLE_VALUE && WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr)
		&& written == data.size();
	if (f != INVALID_HANDLE_VALUE)
		CloseHandle(f);
	if (!saved) {
		DeleteFileW(path.c_str());
		if (error)
			*error = L"Could not save the installer to " + path + L".";
		return std::wstring();
	}
	return path;
}

}  // namespace ics
