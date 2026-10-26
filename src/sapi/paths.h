// paths.h -- where voices are found (the same search as the NVDA add-on's
// _paths.py, so a voice imported for either is found by both).
#pragma once

#include <string>
#include <vector>

#include "voices.h"

namespace ics {

std::wstring module_dir();                 // folder of the DLL or exe this code is in
std::wstring user_voices_dir();            // %APPDATA%\Google ICS TTS\voices
std::wstring common_voices_dir();          // %ProgramData%\Google ICS TTS\voices
std::wstring nvda_voices_dir();            // where the NVDA add-on imports by default
// Where the settings app imports: the shared folder, which every account and
// the sign-in screen see (the installer lets users write there), else the user's.
std::wstring import_dir();
std::vector<std::wstring> voice_search_dirs();
std::wstring find_voice_file(const VoiceDef &voice);  // empty if not installed
std::wstring dictionary_path();            // cmudict.dict beside the module, or one level up

std::string utf8(const std::wstring &w);
std::wstring wide(const std::string &s);

}  // namespace ics
