// runtime.h -- the loaded voice and dictionary, cached per process.
//
// SAPI creates and destroys engine objects freely (every voice enumeration
// makes one), so loading per object would reload the voice each time; the
// runtime is loaded once per voice and kept until the DLL unloads.
#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "ics_hts.h"
#include "voices.h"

namespace ics {

struct SynthParams {
	double speed = 1.0;      // 0.5 .. 6
	double half_tone = 0.0;  // pitch shift in semitones
	double volume = 1.0;     // 0 .. 1
	bool shorten_pauses = true;
	bool continued = false;  // an unpunctuated end continues rather than ends the sentence
};

class Runtime {
public:
	// Cached per voice; nullptr with *error set when the voice or dictionary is missing.
	static Runtime *get(const VoiceDef &voice, std::wstring *error);

	// One sentence (UTF-8) to 16-bit samples; false on failure. Thread-safe.
	bool synthesize(const std::string &text, const SynthParams &p, std::vector<short> &out);
	// Ends a synthesize() running on another thread early (it then returns false).
	void stop() { ics_stop(m_engine); }
	int sample_rate() const { return m_sample_rate; }

private:
	IcsEngine *m_engine = nullptr;
	IcsLexicon *m_lexicon = nullptr;
	int m_sample_rate = 16000;
	int m_fperiod = 80;
	std::mutex m_lock;
};

}  // namespace ics
