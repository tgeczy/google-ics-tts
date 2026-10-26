// voices.h -- the two Ice Cream Sandwich voices and where their data lives.
#pragma once

#include <windows.h>

namespace ics {

// {CDDA2DBC-A776-4A5A-8E40-D2DF233899CD}
static const CLSID CLSID_IcsEngine =
	{0xcdda2dbc, 0xa776, 0x4a5a, {0x8e, 0x40, 0xd2, 0xdf, 0x23, 0x38, 0x99, 0xcd}};

struct VoiceDef {
	const char *id;            // "en-us": the NVDA add-on's id, and the token's IcsVoice attribute
	const wchar_t *token;      // key under ...\Speech\Voices\Tokens
	const wchar_t *name;       // unique display name (clients key voice lists by it)
	const wchar_t *language;   // SAPI LCID, hex
	const wchar_t *gender;
	const wchar_t *file;       // converted voice file name
	int accent;                // ICSF_ACCENT_US / ICSF_ACCENT_GB
	const char *asset;         // path inside GoogleTTS.apk
	const char *sha256;        // of the Android 4.0 voice in the APK
};

static const VoiceDef kVoices[] = {
	{"en-us", L"GoogleIcsTts_enUS", L"Google TTS (Ice Cream Sandwich) English US", L"409", L"Female",
	 L"ics_en_us.htsvoice", 0, "assets/eng-usa/voice",
	 "d24123f5c6d7c5f418868b7dcea86cc02407da67fad2191b1f74cda2f0b54214"},
	{"en-gb", L"GoogleIcsTts_enGB", L"Google TTS (Ice Cream Sandwich) English GB", L"809", L"Male",
	 L"ics_en_gb.htsvoice", 1, "assets/eng-gbr/voice",
	 "d2a13610e8c705b81fab4d4093f30bac0ac86175158912e890b49e814ab1881f"},
};
static const int kVoiceCount = sizeof kVoices / sizeof kVoices[0];

const VoiceDef *find_voice_def(const char *id);

static const wchar_t kRegistryKey[] = L"Software\\Google ICS TTS";
static const wchar_t kSharedName[] = L"Google ICS TTS";

}  // namespace ics
