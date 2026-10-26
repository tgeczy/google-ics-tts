// runtime.cpp -- see runtime.h. The synthesis maths matches the NVDA add-on's
// _engine.py, so both speak alike.
#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "paths.h"

namespace ics {

static const double kPostfilter = 0.8;
static const double kMaxDurationFactor = 1.6;
static const int kMinFramePeriod = 16;

Runtime *Runtime::get(const VoiceDef &voice, std::wstring *error)
{
	static std::mutex cache_lock;
	static std::map<std::string, Runtime *> cache;
	std::lock_guard<std::mutex> guard(cache_lock);
	auto it = cache.find(voice.id);
	if (it != cache.end())
		return it->second;
	std::wstring file = find_voice_file(voice);
	if (file.empty()) {
		if (error)
			*error = L"The voice has not been imported; open Google TTS (Ice Cream Sandwich) settings.";
		return nullptr;
	}
	std::wstring dict = dictionary_path();
	if (dict.empty()) {
		if (error)
			*error = L"cmudict.dict is missing beside the voice DLL.";
		return nullptr;
	}
	IcsLexicon *lexicon = ics_lexicon_open(utf8(dict).c_str(), voice.accent);
	IcsEngine *engine = lexicon ? ics_open(utf8(file).c_str()) : nullptr;
	if (!engine) {
		if (lexicon)
			ics_lexicon_close(lexicon);
		if (error)
			*error = L"The voice file could not be loaded: " + file;
		return nullptr;
	}
	Runtime *r = new Runtime;
	r->m_engine = engine;
	r->m_lexicon = lexicon;
	r->m_sample_rate = ics_sample_rate(engine);
	r->m_fperiod = ics_fperiod(engine);
	cache[voice.id] = r;
	return r;
}

bool Runtime::synthesize(const std::string &text, const SynthParams &p, std::vector<short> &out)
{
	out.clear();
	double speed = std::max(0.1, p.speed);
	double dur_factor = std::min(speed, kMaxDurationFactor);
	int fperiod = std::max(kMinFramePeriod, (int)std::lround(m_fperiod * dur_factor / speed));
	int edge_ms = p.shorten_pauses ? 0 : 50;
	int pause_ms = p.shorten_pauses ? 100 : 250;
	if (speed > 1.0)
		pause_ms = (int)(pause_ms / speed);

	std::lock_guard<std::mutex> guard(m_lock);
	int n = ics_speak(m_engine, m_lexicon, text.c_str(), p.continued ? 0 : 1, dur_factor, fperiod,
		p.half_tone, kPostfilter, edge_ms, pause_ms);
	if (n < 0)
		return false;
	if (n == 0)
		return true;
	out.resize((size_t)n);
	ics_get_samples(m_engine, out.data(), p.volume);
	if (p.shorten_pauses) {
		int start = 0, end = n;
		ics_trim_range(m_engine, 1.0, &start, &end);  // at full volume, so volume 0 keeps its timing
		out.erase(out.begin() + std::min(end, n), out.end());
		out.erase(out.begin(), out.begin() + std::min(start, (int)out.size()));
	}
	return true;
}

}  // namespace ics
