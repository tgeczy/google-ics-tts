// engine.cpp -- see engine.h. The Speak loop follows the conventions the
// TGSpeechBox, Panthera and outSPOKEN SAPI voices settled on.
#include "engine.h"

#include <sperror.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "paths.h"
#include "text.h"

namespace ics {

LONG g_objects = 0;

static const ULONG kChunkBytes = 4096;
static const ULONG kTailMs = 50;

Engine::Engine() { InterlockedIncrement(&g_objects); }

Engine::~Engine()
{
	if (m_token)
		m_token->Release();
	InterlockedDecrement(&g_objects);
}

STDMETHODIMP Engine::QueryInterface(REFIID iid, void **out)
{
	if (!out)
		return E_POINTER;
	*out = nullptr;
	if (iid == IID_IUnknown || iid == IID_ISpTTSEngine)
		*out = static_cast<ISpTTSEngine *>(this);
	else if (iid == IID_ISpObjectWithToken)
		*out = static_cast<ISpObjectWithToken *>(this);
	else
		return E_NOINTERFACE;
	AddRef();
	return S_OK;
}

STDMETHODIMP_(ULONG) Engine::AddRef() { return InterlockedIncrement(&m_refs); }

STDMETHODIMP_(ULONG) Engine::Release()
{
	ULONG n = InterlockedDecrement(&m_refs);
	if (!n)
		delete this;
	return n;
}

STDMETHODIMP Engine::SetObjectToken(ISpObjectToken *token)
{
	if (!token)
		return E_INVALIDARG;
	try {
		std::string id = "en-us";
		ISpDataKey *attrs = nullptr;
		if (SUCCEEDED(token->OpenKey(L"Attributes", &attrs)) && attrs) {
			wchar_t *value = nullptr;
			if (SUCCEEDED(attrs->GetStringValue(L"IcsVoice", &value)) && value) {
				id = utf8(value);
				CoTaskMemFree(value);
			}
			attrs->Release();
		}
		const VoiceDef *voice = find_voice_def(id.c_str());
		if (!voice)
			return E_INVALIDARG;
		// A voice whose data is missing must refuse to load: otherwise it is
		// silent everywhere while still answering bookmarks.
		Runtime *runtime = Runtime::get(*voice, nullptr);
		if (!runtime)
			return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
		if (m_token)
			m_token->Release();
		m_token = token;
		m_token->AddRef();
		m_runtime = runtime;
		m_settings = load_settings();
		return S_OK;
	} catch (...) {
		return E_FAIL;
	}
}

STDMETHODIMP Engine::GetObjectToken(ISpObjectToken **token)
{
	if (!token)
		return E_POINTER;
	*token = m_token;
	if (m_token)
		m_token->AddRef();
	return m_token ? S_OK : S_FALSE;
}

STDMETHODIMP Engine::GetOutputFormat(const GUID *, const WAVEFORMATEX *, GUID *format_id, WAVEFORMATEX **format)
{
	if (!format_id || !format)
		return E_POINTER;
	// Any other format GUID registers fine and then plays silence.
	*format_id = SPDFID_WaveFormatEx;
	WAVEFORMATEX *f = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
	if (!f)
		return E_OUTOFMEMORY;
	DWORD rate = m_runtime ? (DWORD)m_runtime->sample_rate() : 16000;
	f->wFormatTag = WAVE_FORMAT_PCM;
	f->nChannels = 1;
	f->nSamplesPerSec = rate;
	f->wBitsPerSample = 16;
	f->nBlockAlign = 2;
	f->nAvgBytesPerSec = rate * 2;
	f->cbSize = 0;
	*format = f;
	return S_OK;
}

STDMETHODIMP Engine::Speak(DWORD, REFGUID, const WAVEFORMATEX *, const SPVTEXTFRAG *frags, ISpTTSEngineSite *site)
{
	if (!frags || !site)
		return E_INVALIDARG;
	if (!m_runtime)
		return SPERR_UNINITIALIZED;
	try {
		return speak(frags, site);
	} catch (...) {
		return E_FAIL;  // never let an exception reach the host
	}
}

namespace {

// Abort, or skip (acknowledged; nothing is skipped part-way), ends the Speak call.
bool stopping(ISpTTSEngineSite *site)
{
	DWORD actions = site->GetActions();
	if (actions & SPVES_SKIP) {
		site->CompleteSkip(0);
		return true;
	}
	return (actions & SPVES_ABORT) != 0;
}

void fire_bookmark(ISpTTSEngineSite *site, ULONGLONG offset, const std::wstring &name)
{
	// SAPI copies the string; wParam is the numeric id NVDA's sapi5 driver reads.
	SPEVENT ev = {};
	ev.eEventId = SPEI_TTS_BOOKMARK;
	ev.elParamType = SPET_LPARAM_IS_STRING;
	ev.ullAudioStreamOffset = offset;
	ev.wParam = (WPARAM)_wtol(name.c_str());
	ev.lParam = (LPARAM)name.c_str();
	site->AddEvents(&ev, 1);
}

void fire_sentence(ISpTTSEngineSite *site, ULONGLONG offset, ULONG source_pos, ULONG length)
{
	SPEVENT ev = {};
	ev.eEventId = SPEI_SENTENCE_BOUNDARY;
	ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
	ev.ullAudioStreamOffset = offset;
	ev.lParam = (LPARAM)source_pos;
	ev.wParam = (WPARAM)length;
	site->AddEvents(&ev, 1);
}

// Writes all of data, advancing *written by what the site accepted.
// Returns S_FALSE when the host asked to stop.
HRESULT write_audio(ISpTTSEngineSite *site, const BYTE *data, ULONG bytes, ULONGLONG *written)
{
	ULONG pos = 0;
	while (pos < bytes) {
		if (stopping(site))
			return S_FALSE;
		ULONG chunk = std::min(bytes - pos, kChunkBytes);
		ULONG got = 0;
		HRESULT hr = site->Write(data + pos, chunk, &got);
		if (FAILED(hr))
			return hr;
		if (got == 0) {
			Sleep(5);  // buffer full on Win7-era hosts: breathe and retry
			continue;
		}
		pos += got;
		*written += got;
	}
	return S_OK;
}

double speed_for_rate(long rate)
{
	rate = std::max(-10L, std::min(10L, rate));
	// 0 is normal; +10 is about six times, like NVDA's rate boost; -10 is half.
	return rate >= 0 ? std::pow(6.0, rate / 10.0) : std::pow(2.0, rate / 10.0);
}

}  // namespace

// Synthesis runs on a worker so this thread can keep polling the site: a long
// sentence takes a good fraction of a second, and an abort must not wait for it.
Engine::Synth Engine::synthesize(const std::string &text, const SynthParams &p, std::vector<short> &audio,
                                 ISpTTSEngineSite *site)
{
	bool ok = false;
	std::thread worker([&]() {
		try {
			ok = m_runtime->synthesize(text, p, audio);
		} catch (...) {
			ok = false;
		}
	});
	bool stopped = false;
	while (WaitForSingleObject(worker.native_handle(), 10) == WAIT_TIMEOUT) {
		// Keep stopping: the engine clears its flag when a synthesis starts.
		if (stopped || stopping(site)) {
			stopped = true;
			m_runtime->stop();
		}
	}
	worker.join();
	if (stopped || stopping(site))
		return Synth::Stopped;
	return ok ? Synth::Done : Synth::Failed;
}

HRESULT Engine::speak(const SPVTEXTFRAG *frags, ISpTTSEngineSite *site)
{
	std::lock_guard<std::mutex> lock(m_speak_lock);
	std::vector<Batch> batches = batch_fragments(frags);
	ULONGLONG written = 0;
	std::vector<short> audio;
	const int rate_hz = m_runtime->sample_rate();

	for (size_t bi = 0; bi < batches.size(); bi++) {
		const Batch &b = batches[bi];
		if (stopping(site))
			return S_OK;
		if (b.kind == Batch::Silence) {
			std::vector<BYTE> zeros((size_t)b.silence_ms * rate_hz / 1000 * 2);
			HRESULT hr = write_audio(site, zeros.data(), (ULONG)zeros.size(), &written);
			if (hr != S_OK)
				return SUCCEEDED(hr) ? S_OK : hr;
			continue;
		}
		size_t mark = 0;
		if (b.text.empty()) {
			for (; mark < b.marks.size(); mark++)
				fire_bookmark(site, written, b.marks[mark].name);
			continue;
		}
		// With shortened pauses, a piece that more speech follows (another
		// sentence, or the next rate or pitch run) keeps a mid-phrase ending,
		// as the NVDA driver does.
		bool more_text = false;
		for (size_t j = bi + 1; j < batches.size() && !more_text; j++)
			more_text = batches[j].kind != Batch::Silence && !batches[j].text.empty();
		std::vector<Sentence> sentences = split_sentences(b.text);
		for (size_t si = 0; si < sentences.size(); si++) {
			const Sentence &s = sentences[si];
			long site_rate = 0;
			if (FAILED(site->GetRate(&site_rate)))
				site_rate = 0;
			USHORT site_volume = 100;
			if (FAILED(site->GetVolume(&site_volume)) || site_volume > 100)
				site_volume = 100;
			// The XML rate adds to the base rate and the XML volume scales the base volume.
			SynthParams p;
			p.speed = speed_for_rate(site_rate + (b.has_state ? b.state.RateAdj : 0));
			ULONG state_volume = b.has_state ? std::min(b.state.Volume, 100UL) : 100;
			p.volume = state_volume * site_volume / 10000.0;
			long pitch = b.has_state ? std::max(-10L, std::min(10L, b.state.PitchAdj.MiddleAdj)) : 0;
			p.half_tone = pitch * 1.2;
			p.shorten_pauses = m_settings.shorten_pauses;
			p.continued = p.shorten_pauses && (more_text || si + 1 < sentences.size());

			fire_sentence(site, written, b.source_offset(s.begin), (ULONG)(s.end - s.begin));
			std::string text = utf8(b.text.substr(s.begin, s.end - s.begin));
			switch (synthesize(text, p, audio, site)) {
			case Synth::Stopped:
				return S_OK;
			case Synth::Failed:
				return E_FAIL;
			case Synth::Done:
				break;
			}

			// Marks inside this sentence fire when the audio reaches their
			// proportional position, before that audio is written.
			const ULONG total = (ULONG)audio.size() * 2;
			const BYTE *data = reinterpret_cast<const BYTE *>(audio.data());
			ULONG done = 0;
			const size_t span = std::max<size_t>(1, s.end - s.begin);
			while (mark < b.marks.size() && b.marks[mark].pos < s.end) {
				size_t into = b.marks[mark].pos > s.begin ? b.marks[mark].pos - s.begin : 0;
				ULONG at = (ULONG)((ULONGLONG)into * total / span) & ~1u;
				if (at > done) {
					HRESULT hr = write_audio(site, data + done, at - done, &written);
					if (hr != S_OK)
						return SUCCEEDED(hr) ? S_OK : hr;
					done = at;
				}
				fire_bookmark(site, written, b.marks[mark].name);
				mark++;
			}
			HRESULT hr = write_audio(site, data + done, total - done, &written);
			if (hr != S_OK)
				return SUCCEEDED(hr) ? S_OK : hr;
		}
		for (; mark < b.marks.size(); mark++)
			fire_bookmark(site, written, b.marks[mark].name);
	}
	// Some hosts stop the device as soon as Speak returns; pad so the last
	// sound is not clipped.
	std::vector<BYTE> tail((size_t)kTailMs * rate_hz / 1000 * 2);
	write_audio(site, tail.data(), (ULONG)tail.size(), &written);
	return S_OK;
}

}  // namespace ics
