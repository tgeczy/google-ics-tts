// importer.h -- the voice importer for the SAPI side (native twin of the NVDA
// add-on's _importer.py): an APK or gapps zip from disk, or the ICS gapps
// package on SourceForge read with HTTP range requests; each voice checked
// against the Android 4.0 SHA-256, then converted with ics_convert_voice.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ics {

// percent 0..100 and a short message; called on the worker thread.
using Progress = std::function<void(int percent, const std::wstring &message)>;
using Cancelled = std::function<bool()>;

struct ImportResult {
	bool ok = false;
	bool cancelled = false;
	std::vector<std::string> voices;  // voice ids installed: "en-us", "en-gb"
	std::wstring error;               // for the user when !ok
};

ImportResult import_file(const std::wstring &path, const std::wstring &dest_dir,
                         const Progress &progress, const Cancelled &cancelled);
ImportResult import_download(const std::wstring &dest_dir, const Progress &progress,
                             const Cancelled &cancelled);

}  // namespace ics
