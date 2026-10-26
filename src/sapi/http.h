// http.h -- just enough HTTPS for the importer and the update check (WinHTTP,
// so it uses the system's proxy settings and certificate store), plus SHA-256.
#pragma once

#include <windows.h>
#include <winhttp.h>

#include <functional>
#include <string>

namespace ics {

struct HttpReply {
	DWORD status = 0;
	std::wstring final_url;      // after redirects
	std::wstring content_range;  // "bytes 0-0/52428800" for a 206
	ULONGLONG content_length = 0;
};

// Body bytes as they arrive; return false to stop reading (not an error).
using HttpSink = std::function<bool(const char *data, size_t n, const HttpReply &reply)>;

class Http {
public:
	Http();
	~Http();
	// GET, following redirects. length 0 means no Range header; otherwise
	// bytes [start, start + length). false with error() set on failure.
	bool get(const std::wstring &url, ULONGLONG start, ULONGLONG length, const HttpSink &sink, HttpReply &reply);
	// The whole body as bytes; for small replies like the releases API.
	bool get_text(const std::wstring &url, std::string &body, const wchar_t *accept = nullptr);
	const std::wstring &error() const { return m_error; }

private:
	bool fail(const std::wstring &what, DWORD code);
	HINTERNET m_session = nullptr;
	std::wstring m_error;
	std::wstring m_accept;
};

std::string sha256_hex(const void *data, size_t n);

}  // namespace ics
