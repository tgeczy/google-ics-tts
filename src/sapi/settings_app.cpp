// settings_app.cpp -- the SAPI voice's settings app: get the voices (download,
// import a file, or copy the NVDA add-on's), point at a folder of your own,
// shorten pauses, check for updates.
//
//   IcsTtsSettings.exe [/download]   /download starts the download at once
//                                    (the installer's "download now" option)
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "importer.h"
#include "paths.h"
#include "resource.h"
#include "settings.h"
#include "updates.h"
#include "voices.h"

using namespace ics;

namespace {

const UINT WM_APP_PROGRESS = WM_APP + 1;  // wParam percent, lParam new std::wstring
const UINT WM_APP_DONE = WM_APP + 2;      // lParam new std::function<void()>, run here
const wchar_t kTitle[] = L"Google TTS (Ice Cream Sandwich)";

using Continuation = std::function<void()>;

struct App {
	HWND dlg = nullptr;
	std::thread worker;
	std::atomic<bool> cancel{false};
	bool busy = false;
	bool close_when_done = false;
	std::wstring saved_folder;
} app;

HWND item(int id) { return GetDlgItem(app.dlg, id); }

std::wstring text_of(int id)
{
	int n = GetWindowTextLengthW(item(id));
	std::wstring s((size_t)n + 1, L'\0');
	GetWindowTextW(item(id), &s[0], n + 1);
	s.resize((size_t)n);
	return s;
}

void set_status(const std::wstring &s) { SetDlgItemTextW(app.dlg, IDC_STATUS, s.c_str()); }

int message(const std::wstring &text, UINT flags = MB_ICONINFORMATION)
{
	return MessageBoxW(app.dlg, text.c_str(), kTitle, flags);
}

bool is_file(const std::wstring &path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool is_dir(const std::wstring &path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool nvda_has_voices()
{
	for (const VoiceDef &v : kVoices)
		if (is_file(nvda_voices_dir() + L"\\" + v.file))
			return true;
	return false;
}

void set_busy(bool busy)
{
	app.busy = busy;
	for (int id : {IDC_DOWNLOAD, IDC_IMPORT, IDC_COPY_NVDA, IDC_BROWSE, IDC_CLEAR_FOLDER, IDC_FOLDER, IDC_UPDATES})
		EnableWindow(item(id), !busy);
	EnableWindow(item(IDC_STOP), busy);
	if (!busy)
		EnableWindow(item(IDC_COPY_NVDA), nvda_has_voices());
	if (!busy && GetFocus() == nullptr)
		SetFocus(item(IDCANCEL));
}

void refresh_voices()
{
	std::wstring text;
	for (const VoiceDef &v : kVoices) {
		std::wstring file = find_voice_file(v);
		text += v.name;
		if (file.empty()) {
			text += L": not installed";
		} else {
			std::wstring dir = file.substr(0, file.find_last_of(L'\\'));
			text += L": installed in " + dir;
			if (_wcsicmp(dir.c_str(), nvda_voices_dir().c_str()) == 0)
				text += L" (the NVDA add-on's folder: only your account sees it there)";
		}
		text += L"\r\n";
	}
	SetDlgItemTextW(app.dlg, IDC_VOICES, text.c_str());
	if (!app.busy)
		EnableWindow(item(IDC_COPY_NVDA), nvda_has_voices());
}

// Runs work on a worker thread; what it returns runs back on this thread.
void run_task(const wchar_t *starting, std::function<Continuation()> work)
{
	if (app.busy)
		return;
	if (app.worker.joinable())
		app.worker.join();
	app.cancel = false;
	set_busy(true);
	set_status(starting);
	SendDlgItemMessageW(app.dlg, IDC_PROGRESS, PBM_SETPOS, 0, 0);
	HWND dlg = app.dlg;
	app.worker = std::thread([dlg, work]() {
		CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
		Continuation done;
		try {
			done = work();
		} catch (...) {
			done = []() { message(L"Something went wrong.", MB_ICONERROR); };
		}
		CoUninitialize();
		PostMessageW(dlg, WM_APP_DONE, 0, (LPARAM) new Continuation(std::move(done)));
	});
}

Progress progress_to_dialog()
{
	HWND dlg = app.dlg;
	return [dlg](int percent, const std::wstring &text) {
		PostMessageW(dlg, WM_APP_PROGRESS, (WPARAM)percent, (LPARAM) new std::wstring(text));
	};
}

bool cancelled() { return app.cancel.load(); }

std::wstring voice_names(const std::vector<std::string> &ids)
{
	std::wstring out;
	for (size_t i = 0; i < ids.size(); i++) {
		const VoiceDef *v = find_voice_def(ids[i].c_str());
		if (i)
			out += i + 1 == ids.size() ? L" and " : L", ";
		out += v ? v->name : wide(ids[i]);
	}
	return out;
}

Continuation report_import(const ImportResult &r, const std::wstring &dest)
{
	return [r, dest]() {
		refresh_voices();
		SendDlgItemMessageW(app.dlg, IDC_PROGRESS, PBM_SETPOS, r.ok ? 100 : 0, 0);
		if (r.cancelled) {
			set_status(L"Stopped.");
		} else if (r.ok) {
			std::wstring done = L"Installed " + voice_names(r.voices) + L" in " + dest + L".";
			set_status(done);
			message(done + L"\n\nThe voices are ready in any program that uses SAPI 5.");
		} else {
			set_status(r.error);
			message(r.error, MB_ICONERROR);
		}
	};
}

void start_download()
{
	run_task(L"Contacting SourceForge", []() {
		std::wstring dest = import_dir();
		ImportResult r = import_download(dest, progress_to_dialog(), cancelled);
		return report_import(r, dest);
	});
}

void start_import()
{
	wchar_t file[MAX_PATH * 2] = L"";
	OPENFILENAMEW ofn = {sizeof ofn};
	ofn.hwndOwner = app.dlg;
	ofn.lpstrFilter = L"Google TTS or gapps package (*.apk;*.zip)\0*.apk;*.zip\0All files\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH * 2;
	ofn.lpstrTitle = L"Choose GoogleTTS.apk from Android 4.0, or an Ice Cream Sandwich gapps package";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_EXPLORER;
	if (!GetOpenFileNameW(&ofn))
		return;
	std::wstring path = file;
	run_task(L"Opening the file", [path]() {
		std::wstring dest = import_dir();
		ImportResult r = import_file(path, dest, progress_to_dialog(), cancelled);
		return report_import(r, dest);
	});
}

void copy_from_nvda()
{
	std::wstring dest = import_dir();
	SHCreateDirectoryExW(nullptr, dest.c_str(), nullptr);
	std::vector<std::string> copied;
	for (const VoiceDef &v : kVoices) {
		std::wstring from = nvda_voices_dir() + L"\\" + v.file;
		if (!is_file(from))
			continue;
		std::wstring to = dest + L"\\" + v.file;
		if (_wcsicmp(from.c_str(), to.c_str()) == 0 || CopyFileW(from.c_str(), to.c_str(), FALSE))
			copied.push_back(v.id);
	}
	refresh_voices();
	if (copied.empty()) {
		set_status(L"Could not copy the voices to " + dest + L".");
		message(L"Could not copy the voices to " + dest + L".", MB_ICONERROR);
		return;
	}
	std::wstring done = L"Copied " + voice_names(copied) + L" from the NVDA add-on to " + dest + L".";
	set_status(done);
	message(done);
}

void save_folder(const std::wstring &folder)
{
	if (!folder.empty() && !is_dir(folder)) {
		message(L"There is no folder " + folder + L".", MB_ICONWARNING);
		SetDlgItemTextW(app.dlg, IDC_FOLDER, app.saved_folder.c_str());
		return;
	}
	if (folder == app.saved_folder)
		return;
	save_data_path(folder);
	app.saved_folder = folder;
	SetDlgItemTextW(app.dlg, IDC_FOLDER, folder.c_str());
	refresh_voices();
	set_status(folder.empty() ? L"No folder of your own is used." : L"Voices are also looked for in " + folder + L".");
}

void browse_folder()
{
	IFileOpenDialog *pick = nullptr;
	if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pick))))
		return;
	DWORD options = 0;
	pick->GetOptions(&options);
	pick->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
	pick->SetTitle(L"Choose the folder that holds ics_en_us.htsvoice or ics_en_gb.htsvoice");
	IShellItem *chosen = nullptr;
	if (SUCCEEDED(pick->Show(app.dlg)) && SUCCEEDED(pick->GetResult(&chosen))) {
		PWSTR path = nullptr;
		if (SUCCEEDED(chosen->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
			save_folder(path);
			CoTaskMemFree(path);
		}
		chosen->Release();
	}
	pick->Release();
}

void check_updates()
{
	run_task(L"Asking GitHub for the newest release", []() -> Continuation {
		Release r;
		std::wstring error;
		if (!latest_release(r, &error))
			return [error]() {
				set_status(L"Could not check for updates: " + error);
				message(L"Could not check for updates: " + error, MB_ICONWARNING);
			};
		return [r]() {
			if (!is_newer(r.version, installed_version())) {
				std::wstring s = std::wstring(L"You have the newest version, ") + installed_version() + L".";
				set_status(s);
				message(s);
				return;
			}
			std::wstring ask = L"Version " + r.version + L" is available (you have " + installed_version()
				+ L").\n\nDownload and install it now?";
			if (message(ask, MB_ICONQUESTION | MB_YESNO) != IDYES) {
				set_status(L"Version " + r.version + L" is available: " + r.page);
				return;
			}
			run_task(L"Downloading the update", [r]() -> Continuation {
				std::wstring error;
				std::wstring path = fetch_installer(r, progress_to_dialog(), cancelled, &error);
				return [path, error]() {
					if (cancelled()) {
						set_status(L"Stopped.");
					} else if (path.empty()) {
						set_status(L"The update failed: " + error);
						message(L"The update failed: " + error, MB_ICONERROR);
					} else if ((INT_PTR)ShellExecuteW(app.dlg, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) {
						EndDialog(app.dlg, 0);  // the installer replaces this program
					} else {
						set_status(L"Could not start the installer " + path + L".");
						message(L"Could not start the installer " + path + L".", MB_ICONERROR);
					}
				};
			});
		};
	});
}

void close_dialog()
{
	if (app.busy) {
		app.cancel = true;
		app.close_when_done = true;
		set_status(L"Stopping...");
		return;
	}
	save_folder(text_of(IDC_FOLDER));
	EndDialog(app.dlg, 0);
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg) {
	case WM_INITDIALOG: {
		app.dlg = dlg;
		SendDlgItemMessageW(dlg, IDC_PROGRESS, PBM_SETRANGE32, 0, 100);
		CheckDlgButton(dlg, IDC_SHORTEN, load_settings().shorten_pauses ? BST_CHECKED : BST_UNCHECKED);
		app.saved_folder = load_data_path();
		SetDlgItemTextW(dlg, IDC_FOLDER, app.saved_folder.c_str());
		refresh_voices();
		bool any = false;
		for (const VoiceDef &v : kVoices)
			any |= !find_voice_file(v).empty();
		set_status(any ? L"Ready." : L"No voices yet: download them, or import GoogleTTS.apk from Android 4.0.");
		SetFocus(item(any ? IDCANCEL : IDC_DOWNLOAD));
		if (lp)
			start_download();
		return FALSE;
	}
	case WM_COMMAND:
		switch (LOWORD(wp)) {
		case IDC_DOWNLOAD:
			start_download();
			return TRUE;
		case IDC_IMPORT:
			start_import();
			return TRUE;
		case IDC_COPY_NVDA:
			copy_from_nvda();
			return TRUE;
		case IDC_BROWSE:
			browse_folder();
			return TRUE;
		case IDC_CLEAR_FOLDER:
			save_folder(L"");
			return TRUE;
		case IDC_FOLDER:
			if (HIWORD(wp) == EN_KILLFOCUS)
				save_folder(text_of(IDC_FOLDER));
			return TRUE;
		case IDC_SHORTEN:
			if (HIWORD(wp) == BN_CLICKED) {
				Settings s;
				s.shorten_pauses = IsDlgButtonChecked(dlg, IDC_SHORTEN) == BST_CHECKED;
				bool everyone = save_settings(s);
				set_status(std::wstring(s.shorten_pauses ? L"Pauses are shortened" : L"Pauses are not shortened")
					+ (everyone ? L"." : L" for your account."));
			}
			return TRUE;
		case IDC_UPDATES:
			check_updates();
			return TRUE;
		case IDC_STOP:
			app.cancel = true;
			set_status(L"Stopping...");
			return TRUE;
		case IDCANCEL:
			close_dialog();
			return TRUE;
		}
		break;
	case WM_APP_PROGRESS: {
		std::wstring *text = reinterpret_cast<std::wstring *>(lp);
		if (app.busy && !app.cancel) {
			SendDlgItemMessageW(dlg, IDC_PROGRESS, PBM_SETPOS, wp, 0);
			set_status(*text);
		}
		delete text;
		return TRUE;
	}
	case WM_APP_DONE: {
		Continuation *done = reinterpret_cast<Continuation *>(lp);
		if (app.worker.joinable())
			app.worker.join();
		set_busy(false);
		if (app.close_when_done) {
			delete done;
			EndDialog(dlg, 0);
			return TRUE;
		}
		if (*done)
			(*done)();
		delete done;
		return TRUE;
	}
	case WM_CLOSE:
		close_dialog();
		return TRUE;
	}
	return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmd, int)
{
	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
	InitCommonControlsEx(&icc);
	// One window: a second start brings the first one forward.
	HANDLE once = CreateMutexW(nullptr, TRUE, L"Local\\GoogleIcsTtsSettings");
	if (once && GetLastError() == ERROR_ALREADY_EXISTS) {
		HWND other = FindWindowW(L"#32770", L"Google TTS (Ice Cream Sandwich) voice settings");
		if (other)
			SetForegroundWindow(other);
		return 0;
	}
	bool download = cmd && wcsstr(cmd, L"/download") != nullptr;
	DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, dialog_proc, download ? 1 : 0);
	if (app.worker.joinable())
		app.worker.join();
	if (once)
		CloseHandle(once);
	CoUninitialize();
	return 0;
}
