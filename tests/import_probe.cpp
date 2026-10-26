// import_probe.cpp -- run the SAPI side's native importer from the command
// line, for tests/test_sapi_import.py.
//
//   import_probe file <apk or zip> <dest folder>
//   import_probe download <dest folder>
//   import_probe releases <releases api json file>   (the update check's choice)
//
// Prints each progress step as "progress <percent> <message>", then
// "ok <voice ids>", "cancelled", or "error <message>"; exits 0 only on ok.
#include <windows.h>

#include <cstdio>
#include <string>

#include "importer.h"
#include "paths.h"
#include "updates.h"

using namespace ics;

int wmain(int argc, wchar_t **argv)
{
	SetConsoleOutputCP(CP_UTF8);
	if (argc < 3) {
		fprintf(stderr, "usage: import_probe file <apk or zip> <dest> | download <dest>\n");
		return 2;
	}
	std::wstring mode = argv[1];
	if (mode == L"releases") {
		FILE *f = _wfopen(argv[2], L"rb");
		if (!f)
			return 2;
		std::string json;
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0)
			json.append(buf, n);
		fclose(f);
		Release r;
		if (!newest_installer(json, r)) {
			printf("none\n");
			return 1;
		}
		printf("%s %s %s %s newer=%d\n", utf8(r.version).c_str(), utf8(r.url).c_str(), utf8(r.page).c_str(),
			r.sha256.empty() ? "-" : r.sha256.c_str(), is_newer(r.version, installed_version()) ? 1 : 0);
		return 0;
	}
	int last = -1;
	Progress progress = [&](int percent, const std::wstring &text) {
		if (percent < last)
			printf("progress went backwards: %d after %d\n", percent, last);
		last = percent;
		printf("progress %d %s\n", percent, utf8(text).c_str());
		fflush(stdout);
	};
	Cancelled never = []() { return false; };
	ImportResult r;
	if (mode == L"file" && argc >= 4)
		r = import_file(argv[2], argv[3], progress, never);
	else if (mode == L"download")
		r = import_download(argv[2], progress, never);
	else
		return 2;
	if (r.ok) {
		printf("ok");
		for (const std::string &v : r.voices)
			printf(" %s", v.c_str());
		printf("\n");
		return 0;
	}
	if (r.cancelled) {
		printf("cancelled\n");
		return 1;
	}
	printf("error %s\n", utf8(r.error).c_str());
	return 1;
}
