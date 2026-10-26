// http.cpp -- see http.h.
#include "http.h"

#include <bcrypt.h>

#include <vector>

namespace ics {

static const wchar_t kUserAgent[] = L"google-ics-tts (SAPI voice settings)";
static const DWORD kTimeoutMs = 30000;

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

Http::Http()
{
	m_session = WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
		WINHTTP_NO_PROXY_BYPASS, 0);
	if (!m_session)
		return;
	WinHttpSetTimeouts(m_session, kTimeoutMs, kTimeoutMs, kTimeoutMs, kTimeoutMs);
	// Windows 7 offers only TLS 1.0 unless asked (and KB3140245 is installed);
	// older systems refuse the TLS 1.3 flag, so fall back to 1.2.
	DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
	if (!WinHttpSetOption(m_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
		protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
		WinHttpSetOption(m_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
	}
}

Http::~Http()
{
	if (m_session)
		WinHttpCloseHandle(m_session);
}

bool Http::fail(const std::wstring &what, DWORD code)
{
	wchar_t *text = nullptr;
	FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS, GetModuleHandleW(L"winhttp.dll"), code, 0,
		reinterpret_cast<wchar_t *>(&text), 0, nullptr);
	m_error = what;
	if (text) {
		std::wstring t = text;
		while (!t.empty() && (t.back() == L'\n' || t.back() == L'\r' || t.back() == L' ' || t.back() == L'.'))
			t.pop_back();
		m_error += L": " + t;
		LocalFree(text);
	} else {
		m_error += L" (error " + std::to_wstring(code) + L")";
	}
	return false;
}

namespace {

struct Handle {
	HINTERNET h = nullptr;
	~Handle()
	{
		if (h)
			WinHttpCloseHandle(h);
	}
};

std::wstring query_string(HINTERNET req, DWORD what)
{
	DWORD size = 0;
	WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
	if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size)
		return std::wstring();
	std::wstring s(size / sizeof(wchar_t), L'\0');
	if (!WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, &s[0], &size, WINHTTP_NO_HEADER_INDEX))
		return std::wstring();
	s.resize(size / sizeof(wchar_t));
	return s;
}

}  // namespace

bool Http::get(const std::wstring &url, ULONGLONG start, ULONGLONG length, const HttpSink &sink, HttpReply &reply)
{
	reply = HttpReply();
	m_error.clear();
	if (!m_session)
		return fail(L"Could not start WinHTTP", GetLastError());
	URL_COMPONENTSW parts = {sizeof parts};
	wchar_t host[256], path[4096];
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 4096;
	wchar_t extra[2048];
	parts.lpszExtraInfo = extra;
	parts.dwExtraInfoLength = 2048;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
		return fail(L"Bad address " + url, GetLastError());
	std::wstring object = std::wstring(path) + extra;

	Handle connection, request;
	connection.h = WinHttpConnect(m_session, host, parts.nPort, 0);
	if (!connection.h)
		return fail(L"Could not reach " + std::wstring(host), GetLastError());
	DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	request.h = WinHttpOpenRequest(connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!request.h)
		return fail(L"Could not reach " + std::wstring(host), GetLastError());
	std::wstring headers;
	if (length)
		headers += L"Range: bytes=" + std::to_wstring(start) + L"-" + std::to_wstring(start + length - 1) + L"\r\n";
	if (!m_accept.empty())
		headers += L"Accept: " + m_accept + L"\r\n";
	if (!WinHttpSendRequest(request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
			headers.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
			|| !WinHttpReceiveResponse(request.h, nullptr))
		return fail(L"Could not reach " + std::wstring(host), GetLastError());

	DWORD status = 0, size = sizeof status;
	WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
		&status, &size, WINHTTP_NO_HEADER_INDEX);
	reply.status = status;
	reply.content_range = query_string(request.h, WINHTTP_QUERY_CONTENT_RANGE);
	std::wstring len = query_string(request.h, WINHTTP_QUERY_CONTENT_LENGTH);
	reply.content_length = len.empty() ? 0 : _wcstoui64(len.c_str(), nullptr, 10);
	DWORD url_size = 0;
	WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, nullptr, &url_size);
	if (url_size) {
		std::wstring final_url(url_size / sizeof(wchar_t), L'\0');
		if (WinHttpQueryOption(request.h, WINHTTP_OPTION_URL, &final_url[0], &url_size)) {
			final_url.resize(wcslen(final_url.c_str()));
			reply.final_url = final_url;
		}
	}
	if (reply.final_url.empty())
		reply.final_url = url;
	if (status >= 400) {
		m_error = L"The server answered " + std::to_wstring(status) + L" for " + std::wstring(host);
		return false;
	}

	std::vector<char> buf(1 << 16);
	for (;;) {
		DWORD got = 0;
		if (!WinHttpReadData(request.h, buf.data(), (DWORD)buf.size(), &got))
			return fail(L"The download from " + std::wstring(host) + L" broke off", GetLastError());
		if (!got)
			break;
		if (!sink(buf.data(), got, reply))
			break;
	}
	return true;
}

bool Http::get_text(const std::wstring &url, std::string &body, const wchar_t *accept)
{
	body.clear();
	m_accept = accept ? accept : L"";
	HttpReply reply;
	bool ok = get(url, 0, 0, [&](const char *data, size_t n, const HttpReply &) {
		body.append(data, n);
		return body.size() < (16u << 20);
	}, reply);
	m_accept.clear();
	return ok;
}

std::string sha256_hex(const void *data, size_t n)
{
	// CreateHash/HashData rather than BCryptHash, which needs Windows 10.
	std::string out;
	BCRYPT_ALG_HANDLE alg = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	unsigned char digest[32];
	if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
		return out;
	if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
		const unsigned char *p = static_cast<const unsigned char *>(data);
		bool ok = true;
		while (n && ok) {
			ULONG chunk = n > (1u << 30) ? (1u << 30) : (ULONG)n;
			ok = BCryptHashData(hash, const_cast<PUCHAR>(p), chunk, 0) == 0;
			p += chunk;
			n -= chunk;
		}
		if (ok && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
			static const char hex[] = "0123456789abcdef";
			for (unsigned char c : digest) {
				out += hex[c >> 4];
				out += hex[c & 15];
			}
		}
		BCryptDestroyHash(hash);
	}
	BCryptCloseAlgorithmProvider(alg, 0);
	return out;
}

}  // namespace ics
