// settings.h -- SAPI voice settings, in the registry under Software\Google ICS TTS.
// HKCU wins over HKLM; HKLM is what the sign-in screen (SYSTEM) sees. HKLM is
// written in both registry views, so the 32- and 64-bit voices agree; the
// installer lets users write that key, so the settings app needs no elevation.
#pragma once

#include <string>

namespace ics {

struct Settings {
	bool shorten_pauses = true;
};

Settings load_settings();
// Writes HKCU, and HKLM when allowed (returns false if HKLM could not be written).
bool save_settings(const Settings &s);

// DataPath: a folder of the user's own that holds the voices (or a "voices"
// folder inside it). Empty when unset; saving an empty path removes it.
std::wstring load_data_path();
bool save_data_path(const std::wstring &path);

}  // namespace ics
