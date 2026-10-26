// engine.h -- the SAPI 5 engine object (ISpTTSEngine + ISpObjectWithToken).
#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

#include <mutex>

#include "runtime.h"
#include "settings.h"

namespace ics {

extern LONG g_objects;

class Engine : public ISpTTSEngine, public ISpObjectWithToken {
public:
	Engine();
	virtual ~Engine();

	STDMETHODIMP QueryInterface(REFIID iid, void **out) override;
	STDMETHODIMP_(ULONG) AddRef() override;
	STDMETHODIMP_(ULONG) Release() override;

	STDMETHODIMP SetObjectToken(ISpObjectToken *token) override;
	STDMETHODIMP GetObjectToken(ISpObjectToken **token) override;

	STDMETHODIMP Speak(DWORD flags, REFGUID format_id, const WAVEFORMATEX *format,
	                   const SPVTEXTFRAG *frags, ISpTTSEngineSite *site) override;
	STDMETHODIMP GetOutputFormat(const GUID *target_id, const WAVEFORMATEX *target,
	                             GUID *format_id, WAVEFORMATEX **format) override;

private:
	enum class Synth { Done, Stopped, Failed };
	HRESULT speak(const SPVTEXTFRAG *frags, ISpTTSEngineSite *site);
	Synth synthesize(const std::string &text, const SynthParams &p, std::vector<short> &audio,
	                 ISpTTSEngineSite *site);

	LONG m_refs = 1;
	ISpObjectToken *m_token = nullptr;
	Runtime *m_runtime = nullptr;
	Settings m_settings;
	std::mutex m_speak_lock;
};

}  // namespace ics
