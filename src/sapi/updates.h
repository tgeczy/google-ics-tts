// updates.h -- check GitHub for a newer SAPI installer (after the NVDA
// add-on's _updates.py). Only when the user presses the button; never on a
// timer or at start-up.
//
// The version comes from the installer's file name, googleIcsTts-sapi-X.Y.Z.exe,
// so a release that only updates the NVDA add-on never offers a SAPI update.
// Drafts and prereleases are skipped.
#pragma once

#include <string>

#include "importer.h"

namespace ics {

struct Release {
	std::wstring version;  // "0.9.0"
	std::wstring page;     // the release's web page
	std::wstring url;      // the installer
	std::string sha256;    // from GitHub's asset digest; may be empty
};

const wchar_t *installed_version();
bool is_newer(const std::wstring &latest, const std::wstring &installed);

// The newest release with a SAPI installer. false with *error set when GitHub
// could not be asked or has no installer yet.
bool latest_release(Release &out, std::wstring *error);
// From a releases API reply (for tests).
bool newest_installer(const std::string &json, Release &out);

// Downloads the installer into %TEMP% and checks its SHA-256; returns the path,
// or empty with *error set.
std::wstring fetch_installer(const Release &r, const Progress &progress, const Cancelled &cancelled,
                             std::wstring *error);

}  // namespace ics
