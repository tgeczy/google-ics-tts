// settings.h -- SAPI voice settings, in the registry under Software\Google ICS TTS.
// HKCU wins over HKLM; HKLM is what the sign-in screen (SYSTEM) sees.
#pragma once

namespace ics {

struct Settings {
	bool shorten_pauses = true;
};

Settings load_settings();
// Writes HKCU, and HKLM when allowed (returns false if HKLM could not be written).
bool save_settings(const Settings &s);

}  // namespace ics
