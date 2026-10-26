// sapi_probe.cpp -- drive the SAPI voice through ISpVoice the way screen
// readers do, and report what a test needs to check.
//
//   sapi_probe <out.wav> <voice id: en-us|en-gb> [xml text]
//
// 1. Speaks the text into <out.wav> (16 kHz mono) to check the audio.
// 2. Speaks it again to the sound card at volume 0, asynchronously, and prints
//    every bookmark and sentence event with its audio offset: screen readers
//    pace on these (NVDA's sapi5 driver waits for each bookmark).
// 3. Times an abort (SPF_PURGEBEFORESPEAK) in the middle of a long sentence.
#include <windows.h>
#include <sapi.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using clock_type = std::chrono::steady_clock;

static void check(HRESULT hr, const char *what)
{
	if (FAILED(hr)) {
		fprintf(stderr, "%s failed: 0x%08lx\n", what, (unsigned long)hr);
		exit(1);
	}
}

static long long ms_since(clock_type::time_point t)
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(clock_type::now() - t).count();
}

static ISpObjectToken *find_voice(const wchar_t *id)
{
	ISpObjectTokenCategory *cat = nullptr;
	check(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL, IID_ISpObjectTokenCategory,
		(void **)&cat), "token category");
	check(cat->SetId(SPCAT_VOICES, FALSE), "SetId");
	IEnumSpObjectTokens *e = nullptr;
	std::wstring want = std::wstring(L"IcsVoice=") + id;
	check(cat->EnumTokens(want.c_str(), nullptr, &e), "EnumTokens");
	ULONG n = 0;
	e->GetCount(&n);
	ISpObjectToken *tok = nullptr;
	if (n)
		e->Next(1, &tok, nullptr);
	e->Release();
	cat->Release();
	return tok;
}

static ISpVoice *make_voice(ISpObjectToken *tok)
{
	ISpVoice *voice = nullptr;
	check(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void **)&voice), "SpVoice");
	check(voice->SetVoice(tok), "SetVoice");
	return voice;
}

static void print_event(const SPEVENT &ev, ULONG bytes_per_sec)
{
	double t = (double)ev.ullAudioStreamOffset / bytes_per_sec;
	if (ev.eEventId == SPEI_TTS_BOOKMARK)
		printf("  %7.3fs bookmark %lld \"%ls\"\n", t, (long long)ev.wParam, (const wchar_t *)ev.lParam);
	else if (ev.eEventId == SPEI_SENTENCE_BOUNDARY)
		printf("  %7.3fs sentence at char %lld, length %lld\n", t, (long long)ev.lParam, (long long)ev.wParam);
	else if (ev.eEventId == SPEI_END_INPUT_STREAM)
		printf("  %7.3fs end of stream\n", t);
	if (ev.elParamType == SPET_LPARAM_IS_STRING || ev.elParamType == SPET_LPARAM_IS_TOKEN
			|| ev.elParamType == SPET_LPARAM_IS_OBJECT || ev.elParamType == SPET_LPARAM_IS_POINTER)
		CoTaskMemFree((void *)ev.lParam);
}

int wmain(int argc, wchar_t **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: sapi_probe out.wav en-us|en-gb [xml text]\n");
		return 2;
	}
	CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	ISpObjectToken *tok = find_voice(argv[2]);
	if (!tok) {
		fprintf(stderr, "voice %ls is not registered\n", argv[2]);
		return 1;
	}
	std::wstring text = argc > 3 ? argv[3] :
		L"<bookmark mark=\"1\"/>Hello, this is the SAPI voice. <bookmark mark=\"2\"/>Twenty six people "
		L"<bookmark mark=\"3\"/>played <rate absspeed=\"8\">very quickly</rate> and <pitch absmiddle=\"6\">"
		L"higher</pitch>.<silence msec=\"300\"/><spell>NVDA</spell><bookmark mark=\"4\"/>";

	// 1. To a WAV file.
	{
		ISpVoice *voice = make_voice(tok);
		WAVEFORMATEX fmt = {WAVE_FORMAT_PCM, 1, 16000, 32000, 2, 16, 0};
		ISpStream *stream = nullptr;
		check(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream, (void **)&stream), "SpStream");
		check(stream->BindToFile(argv[1], SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx, &fmt, 0), "BindToFile");
		check(voice->SetOutput(stream, TRUE), "SetOutput");
		auto t0 = clock_type::now();
		check(voice->Speak(text.c_str(), SPF_IS_XML, nullptr), "Speak to file");
		printf("file: spoken in %lld ms\n", ms_since(t0));
		stream->Close();
		stream->Release();
		voice->Release();
	}

	// 2. To the sound card, silently, collecting events as a screen reader does.
	{
		ISpVoice *voice = make_voice(tok);
		voice->SetVolume(0);
		ULONGLONG interest = SPFEI(SPEI_TTS_BOOKMARK) | SPFEI(SPEI_SENTENCE_BOUNDARY) | SPFEI(SPEI_END_INPUT_STREAM);
		check(voice->SetInterest(interest, interest), "SetInterest");
		check(voice->SetNotifyWin32Event(), "SetNotifyWin32Event");
		HANDLE notify = voice->GetNotifyEventHandle();
		auto t0 = clock_type::now();
		check(voice->Speak(text.c_str(), SPF_IS_XML | SPF_ASYNC, nullptr), "Speak to audio");
		printf("events (offsets in the audio):\n");
		bool done = false;
		long long first_mark_ms = -1;
		while (!done && ms_since(t0) < 30000) {
			WaitForSingleObject(notify, 200);
			SPEVENT ev;
			ULONG got = 0;
			while (voice->GetEvents(1, &ev, &got) == S_OK && got) {
				if (ev.eEventId == SPEI_TTS_BOOKMARK && first_mark_ms < 0)
					first_mark_ms = ms_since(t0);
				done |= ev.eEventId == SPEI_END_INPUT_STREAM;
				print_event(ev, 32000);
			}
		}
		printf("first bookmark after %lld ms; done after %lld ms\n", first_mark_ms, ms_since(t0));
		voice->Release();
	}

	// 3. Abort in the middle of a long sentence.
	{
		ISpVoice *voice = make_voice(tok);
		voice->SetVolume(0);
		voice->Speak(L"This is a long sentence that keeps going so that it can be interrupted in the middle "
			L"of being spoken, to see how quickly the voice stops talking when it is told to.", SPF_ASYNC, nullptr);
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		auto t0 = clock_type::now();
		voice->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
		voice->WaitUntilDone(5000);
		printf("abort: stopped %lld ms after the purge\n", ms_since(t0));
		voice->Release();
	}
	tok->Release();
	CoUninitialize();
	return 0;
}
