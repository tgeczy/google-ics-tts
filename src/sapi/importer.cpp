// importer.cpp -- see importer.h. Mirrors the NVDA add-on's _importer.py:
// the same checks, the same messages and the same progress steps.
#include "importer.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

#include "http.h"
#include "ics_convert.h"
#include "miniz.h"
#include "paths.h"
#include "voices.h"

namespace ics {

static const wchar_t kSourceForgeUrl[] =
	L"https://downloads.sourceforge.net/project/touchpad/gapps-ics-20120429-signed.zip";
static const size_t kBlock = 1 << 20;

namespace {

struct Failed {
	std::wstring message;
};
struct Stopped {};

using Voices = std::map<std::string, std::vector<unsigned char>>;

void check(const Cancelled &cancelled)
{
	if (cancelled && cancelled())
		throw Stopped();
}

// A random-access byte source for miniz.
struct Source {
	virtual ~Source() {}
	virtual bool read(mz_uint64 offset, void *buf, size_t n) = 0;
	mz_uint64 size = 0;
	std::wstring error;
};

size_t source_read(void *opaque, mz_uint64 offset, void *buf, size_t n)
{
	return static_cast<Source *>(opaque)->read(offset, buf, n) ? n : 0;
}

struct FileSource : Source {
	HANDLE file = INVALID_HANDLE_VALUE;
	explicit FileSource(const std::wstring &path)
	{
		file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		LARGE_INTEGER s;
		if (file != INVALID_HANDLE_VALUE && GetFileSizeEx(file, &s))
			size = (mz_uint64)s.QuadPart;
	}
	~FileSource() override
	{
		if (file != INVALID_HANDLE_VALUE)
			CloseHandle(file);
	}
	bool read(mz_uint64 offset, void *buf, size_t n) override
	{
		LARGE_INTEGER at;
		at.QuadPart = (LONGLONG)offset;
		if (!SetFilePointerEx(file, at, nullptr, FILE_BEGIN))
			return false;
		char *p = static_cast<char *>(buf);
		while (n) {
			DWORD got = 0;
			if (!ReadFile(file, p, (DWORD)std::min<size_t>(n, 1u << 30), &got, nullptr) || !got)
				return false;
			p += got;
			n -= got;
		}
		return true;
	}
};

// The remote zip, fetched in 1 MB blocks with range requests.
struct RangeSource : Source {
	Http &http;
	std::wstring url;
	const Progress &progress;
	const Cancelled &cancelled;
	std::vector<char> block;
	mz_uint64 block_start = 0;
	mz_uint64 fetched = 0;
	mz_uint64 expected = 4500000;

	RangeSource(Http &h, const std::wstring &u, mz_uint64 total, const Progress &p, const Cancelled &c)
		: http(h), url(u), progress(p), cancelled(c)
	{
		size = total;
	}

	bool fetch(mz_uint64 start, size_t length)
	{
		if (cancelled && cancelled())
			return false;
		length = (size_t)std::min<mz_uint64>(length, size - start);
		block.clear();
		HttpReply reply;
		bool ok = http.get(url, start, length, [&](const char *data, size_t n, const HttpReply &r) {
			if (r.status != 206)
				return false;
			block.insert(block.end(), data, data + n);
			return true;
		}, reply);
		if (!ok) {
			error = http.error();
			return false;
		}
		if (reply.status != 206 || block.size() != length) {
			error = L"The download server stopped answering partial requests.";
			return false;
		}
		block_start = start;
		fetched += block.size();
		if (progress)
			progress((int)std::min<mz_uint64>(60, 10 + fetched * 50 / std::max<mz_uint64>(1, expected)),
				L"Downloaded " + std::to_wstring(fetched / 1024) + L" KB");
		return true;
	}

	bool read(mz_uint64 offset, void *buf, size_t n) override
	{
		char *p = static_cast<char *>(buf);
		while (n) {
			if (offset < block_start || offset >= block_start + block.size())
				if (!fetch(offset, std::max(n, kBlock)))
					return false;
			size_t off = (size_t)(offset - block_start);
			size_t take = std::min(n, block.size() - off);
			memcpy(p, block.data() + off, take);
			p += take;
			offset += take;
			n -= take;
		}
		return true;
	}
};

struct Zip {
	mz_zip_archive z;
	std::vector<unsigned char> memory;  // for an archive held in memory
	bool open = false;
	Zip() { mz_zip_zero_struct(&z); }
	~Zip()
	{
		if (open)
			mz_zip_reader_end(&z);
	}
	bool init(Source &src)
	{
		z.m_pRead = source_read;
		z.m_pIO_opaque = &src;
		open = mz_zip_reader_init(&z, src.size, 0) != 0;
		return open;
	}
	bool init(std::vector<unsigned char> &&bytes)
	{
		memory = std::move(bytes);
		open = mz_zip_reader_init_mem(&z, memory.data(), memory.size(), 0) != 0;
		return open;
	}
	int find(const char *name) { return mz_zip_reader_locate_file(&z, name, nullptr, 0); }
	bool extract(int index, std::vector<unsigned char> &out)
	{
		size_t n = 0;
		void *p = mz_zip_reader_extract_to_heap(&z, (mz_uint)index, &n, 0);
		if (!p)
			return false;
		out.assign(static_cast<unsigned char *>(p), static_cast<unsigned char *>(p) + n);
		mz_free(p);
		return true;
	}
};

const wchar_t kNoVoices[] =
	L"This file has no Google TTS voices. Choose GoogleTTS.apk from Android 4.0, "
	L"or an Ice Cream Sandwich gapps package that contains it.";

// Index of GoogleTTS.apk in a gapps zip; -1 when the zip is the APK itself.
int find_apk(Zip &zip)
{
	if (zip.find(kVoices[0].asset) >= 0)
		return -1;
	mz_uint count = mz_zip_reader_get_num_files(&zip.z);
	for (mz_uint i = 0; i < count; i++) {
		char name[512];
		mz_zip_reader_get_filename(&zip.z, i, name, sizeof name);
		std::string lower = name;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
		const std::string tail = "googletts.apk";
		if (lower.size() >= tail.size() && lower.compare(lower.size() - tail.size(), tail.size(), tail) == 0)
			return (int)i;
	}
	throw Failed{kNoVoices};
}

Voices voices_from_zip(Zip &zip, const Progress &progress, const Cancelled &cancelled, const Source *src)
{
	int apk_index = find_apk(zip);
	Zip inner;
	Zip *apk = &zip;
	if (apk_index >= 0) {
		if (progress)
			progress(30, L"Reading GoogleTTS.apk");
		std::vector<unsigned char> bytes;
		if (!zip.extract(apk_index, bytes)) {
			check(cancelled);
			throw Failed{src && !src->error.empty() ? src->error : std::wstring(L"Could not unpack GoogleTTS.apk.")};
		}
		if (!inner.init(std::move(bytes)))
			throw Failed{L"GoogleTTS.apk in this package is damaged."};
		apk = &inner;
	}
	check(cancelled);
	Voices voices;
	bool wrong = false;
	for (const VoiceDef &v : kVoices) {
		int index = apk->find(v.asset);
		if (index < 0)
			continue;
		std::vector<unsigned char> data;
		if (!apk->extract(index, data))
			throw Failed{L"Could not unpack the voices from GoogleTTS.apk."};
		if (sha256_hex(data.data(), data.size()) == v.sha256)
			voices[v.id] = std::move(data);
		else
			wrong = true;
	}
	if (voices.empty()) {
		if (wrong)
			throw Failed{L"This Google TTS is not the Android 4.0 (Ice Cream Sandwich) version; "
			             L"its voices are in a different format."};
		throw Failed{L"No Ice Cream Sandwich voices were found in this file."};
	}
	return voices;
}

std::vector<std::string> install(const Voices &voices, const std::wstring &dest, const Progress &progress)
{
	int err = SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);
	if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS)
		throw Failed{L"Could not create the voices folder " + dest + L"."};
	std::vector<std::string> done;
	int n = 0;
	for (const auto &entry : voices) {
		const VoiceDef *v = find_voice_def(entry.first.c_str());
		if (progress)
			progress(70 + 25 * n++ / (int)voices.size(), std::wstring(L"Converting ") + v->name);
		std::wstring target = dest + L"\\" + v->file;
		std::wstring tmp = target + L".tmp";
		int rc = icsc_convert(entry.second.data(), entry.second.size(), utf8(tmp).c_str(), "voice");
		if (rc != ICSC_OK) {
			DeleteFileW(tmp.c_str());
			throw Failed{std::wstring(L"Could not convert ") + v->name + L" (error " + std::to_wstring(rc) + L")."};
		}
		if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			DeleteFileW(tmp.c_str());
			throw Failed{L"Could not write " + target + L"."};
		}
		done.push_back(entry.first);
	}
	return done;
}

std::wstring base_name(const std::wstring &path)
{
	size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

template <typename F>
ImportResult run(F body)
{
	ImportResult r;
	try {
		r.voices = body();
		r.ok = true;
	} catch (const Failed &f) {
		r.error = f.message;
	} catch (const Stopped &) {
		r.cancelled = true;
	} catch (const std::bad_alloc &) {
		r.error = L"Not enough memory to import the voices.";
	}
	return r;
}

}  // namespace

ImportResult import_file(const std::wstring &path, const std::wstring &dest_dir, const Progress &progress,
                         const Cancelled &cancelled)
{
	return run([&]() {
		if (progress)
			progress(10, L"Opening " + base_name(path));
		FileSource src(path);
		if (src.file == INVALID_HANDLE_VALUE)
			throw Failed{L"Could not open " + base_name(path) + L"."};
		Zip zip;
		if (!zip.init(src))
			throw Failed{L"Could not read " + base_name(path) + L": it is not a zip or APK file."};
		Voices voices = voices_from_zip(zip, progress, cancelled, &src);
		check(cancelled);
		return install(voices, dest_dir, progress);
	});
}

ImportResult import_download(const std::wstring &dest_dir, const Progress &progress, const Cancelled &cancelled)
{
	return run([&]() {
		if (progress)
			progress(5, L"Contacting SourceForge");
		Http http;
		HttpReply probe;
		if (!http.get(kSourceForgeUrl, 0, 1, [](const char *, size_t, const HttpReply &r) { return r.status == 206; },
				probe))
			throw Failed{L"The download failed: " + http.error()};
		check(cancelled);
		Voices voices;
		size_t slash = probe.content_range.find(L'/');
		if (probe.status == 206 && slash != std::wstring::npos) {
			RangeSource src(http, probe.final_url, _wcstoui64(probe.content_range.c_str() + slash + 1, nullptr, 10),
				progress, cancelled);
			Zip zip;
			if (!zip.init(src)) {
				check(cancelled);
				throw Failed{L"The download failed: " + (src.error.empty() ? L"the package is damaged." : src.error)};
			}
			int apk = find_apk(zip);
			if (apk >= 0) {
				mz_zip_archive_file_stat st;
				if (mz_zip_reader_file_stat(&zip.z, (mz_uint)apk, &st))
					src.expected = src.fetched + st.m_comp_size;
			}
			voices = voices_from_zip(zip, nullptr, cancelled, &src);
			if (progress)
				progress(65, L"Checking the voices");
		} else {
			// The server ignores ranges: fetch the whole package (about 50 MB).
			std::vector<unsigned char> all;
			HttpReply reply;
			bool ok = http.get(probe.final_url, 0, 0, [&](const char *data, size_t n, const HttpReply &r) {
				all.insert(all.end(), data, data + n);
				if (progress && r.content_length)
					progress((int)(5 + all.size() * 55 / r.content_length), L"Downloaded "
						+ std::to_wstring(all.size() >> 20) + L" of " + std::to_wstring(r.content_length >> 20) + L" MB");
				return !(cancelled && cancelled());
			}, reply);
			check(cancelled);
			if (!ok)
				throw Failed{L"The download failed: " + http.error()};
			Zip zip;
			if (!zip.init(std::move(all)))
				throw Failed{L"The download failed: the package is damaged."};
			voices = voices_from_zip(zip, progress, cancelled, nullptr);
		}
		check(cancelled);
		return install(voices, dest_dir, progress);
	});
}

}  // namespace ics
