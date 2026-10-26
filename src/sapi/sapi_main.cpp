// sapi_main.cpp -- COM entry points, class factory and registration.
//
// regsvr32 registers the COM class for this DLL's architecture (run both the
// System32 and SysWOW64 regsvr32 on 64-bit Windows) and adds the two voice
// tokens if they are missing. Existing tokens are never rewritten, so an
// upgrade keeps whatever is there. SAPI lists voices only from HKLM, so
// registering needs administrator rights.
#include <windows.h>
#include <olectl.h>
#include <sapi.h>

#include <new>
#include <string>

#include "engine.h"
#include "paths.h"
#include "voices.h"

using namespace ics;

HMODULE g_module = nullptr;

namespace {

class Factory : public IClassFactory {
	LONG m_refs = 1;

public:
	Factory() { InterlockedIncrement(&g_objects); }
	virtual ~Factory() { InterlockedDecrement(&g_objects); }

	STDMETHODIMP QueryInterface(REFIID iid, void **out) override
	{
		if (!out)
			return E_POINTER;
		*out = nullptr;
		if (iid != IID_IUnknown && iid != IID_IClassFactory)
			return E_NOINTERFACE;
		*out = static_cast<IClassFactory *>(this);
		AddRef();
		return S_OK;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_refs); }
	STDMETHODIMP_(ULONG) Release() override
	{
		ULONG n = InterlockedDecrement(&m_refs);
		if (!n)
			delete this;
		return n;
	}
	STDMETHODIMP CreateInstance(IUnknown *outer, REFIID iid, void **out) override
	{
		if (outer)
			return CLASS_E_NOAGGREGATION;
		Engine *e = new (std::nothrow) Engine;
		if (!e)
			return E_OUTOFMEMORY;
		HRESULT hr = e->QueryInterface(iid, out);
		e->Release();
		return hr;
	}
	STDMETHODIMP LockServer(BOOL lock) override
	{
		if (lock)
			InterlockedIncrement(&g_objects);
		else
			InterlockedDecrement(&g_objects);
		return S_OK;
	}
};

std::wstring clsid_string()
{
	wchar_t s[64];
	StringFromGUID2(CLSID_IcsEngine, s, 64);
	return s;
}

bool set_string(HKEY root, const std::wstring &path, const wchar_t *name, const std::wstring &value)
{
	HKEY key;
	if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
		return false;
	bool ok = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
		(DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
	RegCloseKey(key);
	return ok;
}

bool key_exists(HKEY root, const std::wstring &path)
{
	HKEY key;
	if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
		return false;
	RegCloseKey(key);
	return true;
}

const wchar_t kTokens[] = L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens\\";

HRESULT register_server(HKEY root)
{
	std::wstring cls = L"Software\\Classes\\CLSID\\" + clsid_string();
	wchar_t module[MAX_PATH * 2];
	GetModuleFileNameW(g_module, module, MAX_PATH * 2);
	if (!set_string(root, cls, nullptr, L"Google TTS (Ice Cream Sandwich)")
			|| !set_string(root, cls + L"\\InprocServer32", nullptr, module)
			|| !set_string(root, cls + L"\\InprocServer32", L"ThreadingModel", L"Both"))
		return SELFREG_E_CLASS;
	for (const VoiceDef &v : kVoices) {
		std::wstring tok = std::wstring(kTokens) + v.token;
		if (key_exists(root, tok))
			continue;  // an upgrade: leave the token as it is
		std::wstring attrs = tok + L"\\Attributes";
		set_string(root, tok, nullptr, v.name);
		set_string(root, tok, v.language, v.name);
		set_string(root, tok, L"CLSID", clsid_string());
		set_string(root, attrs, L"Name", v.name);
		set_string(root, attrs, L"Language", v.language);
		set_string(root, attrs, L"Gender", v.gender);
		set_string(root, attrs, L"Age", L"Adult");
		set_string(root, attrs, L"Vendor", L"Google");
		set_string(root, attrs, L"IcsVoice", wide(v.id));
	}
	return S_OK;
}

HRESULT unregister_server(HKEY root)
{
	for (const VoiceDef &v : kVoices)
		RegDeleteTreeW(root, (std::wstring(kTokens) + v.token).c_str());
	RegDeleteTreeW(root, (L"Software\\Classes\\CLSID\\" + clsid_string()).c_str());
	return S_OK;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		g_module = instance;
		DisableThreadLibraryCalls(instance);
	}
	return TRUE;
}

STDAPI DllCanUnloadNow()
{
	return g_objects ? S_FALSE : S_OK;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
	if (!out)
		return E_POINTER;
	*out = nullptr;
	if (clsid != CLSID_IcsEngine)
		return CLASS_E_CLASSNOTAVAILABLE;
	Factory *f = new (std::nothrow) Factory;
	if (!f)
		return E_OUTOFMEMORY;
	HRESULT hr = f->QueryInterface(iid, out);
	f->Release();
	return hr;
}

STDAPI DllRegisterServer() { return register_server(HKEY_LOCAL_MACHINE); }
STDAPI DllUnregisterServer() { return unregister_server(HKEY_LOCAL_MACHINE); }

