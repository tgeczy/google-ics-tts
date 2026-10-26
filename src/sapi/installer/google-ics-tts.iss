; google-ics-tts.iss -- installer for the SAPI 5 voice (Inno Setup 6).
; Built by src\sapi\build.cmd installer, which passes /DAppVersion.
;
; Both engine DLLs are registered (32-bit programs need the x86 one); each
; adds its voice tokens only if they are missing, so an upgrade only
; re-registers the COM class. Uninstalling unregisters both, which removes
; the tokens from both registry views.
;
; The voices live in %ProgramData%\Google ICS TTS\voices, which every account
; and the sign-in screen see. Users may write there and to the settings key, so
; the settings app can import voices and save settings without elevation.

#ifndef AppVersion
  #error Pass /DAppVersion=x.y.z (src\sapi\build.cmd installer does).
#endif

#define Build "..\..\..\build"
#define Settings "Google TTS (Ice Cream Sandwich) voice settings"

[Setup]
AppId={{D912E041-1E4B-432E-87C3-4DEF1AAFE896}
AppName=Google TTS (Ice Cream Sandwich) for SAPI 5
AppVersion={#AppVersion}
AppVerName=Google TTS (Ice Cream Sandwich) for SAPI 5, version {#AppVersion}
AppPublisher=tgeczy
AppPublisherURL=https://github.com/tgeczy/google-ics-tts
AppSupportURL=https://github.com/tgeczy/google-ics-tts/issues
AppUpdatesURL=https://github.com/tgeczy/google-ics-tts/releases
VersionInfoVersion={#AppVersion}
DefaultDirName={autopf}\Google ICS TTS
DefaultGroupName=Google TTS (Ice Cream Sandwich)
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x86compatible x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=6.1sp1
LicenseFile=..\..\..\LICENSE
OutputDir={#Build}
OutputBaseFilename=googleIcsTts-sapi-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=Google TTS (Ice Cream Sandwich) for SAPI 5
UninstallDisplayIcon={app}\IcsTtsSettings.exe
; A screen reader using the voice keeps the DLL loaded: replace it at the next
; restart rather than closing the screen reader.
CloseApplications=no
RestartIfNeededByRun=no

[Dirs]
Name: "{commonappdata}\Google ICS TTS"; Permissions: users-modify
Name: "{commonappdata}\Google ICS TTS\voices"; Permissions: users-modify

[Files]
Source: "{#Build}\sapi\x64\ics_sapi.dll"; DestDir: "{app}\x64"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit; Check: Is64BitInstallMode
Source: "{#Build}\sapi\x86\ics_sapi.dll"; DestDir: "{app}\x86"; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "{#Build}\sapi\x86\IcsTtsSettings.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\data\cmudict.dict"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\..\..\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE.txt"
Source: "NOTICES.txt"; DestDir: "{app}\licenses"
Source: "..\..\addon\licenses\hts_engine_API.txt"; DestDir: "{app}\licenses"
Source: "..\..\addon\licenses\cmudict.txt"; DestDir: "{app}\licenses"
Source: "..\..\native\third_party\miniz\LICENSE"; DestDir: "{app}\licenses"; DestName: "miniz.txt"

[Registry]
Root: HKLM64; Subkey: "Software\Google ICS TTS"; Permissions: users-modify; Flags: uninsdeletekey; Check: IsWin64
Root: HKLM32; Subkey: "Software\Google ICS TTS"; Permissions: users-modify; Flags: uninsdeletekey

[Icons]
Name: "{group}\{#Settings}"; Filename: "{app}\IcsTtsSettings.exe"
Name: "{group}\Uninstall Google TTS (Ice Cream Sandwich)"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\IcsTtsSettings.exe"; Parameters: "/download"; Description: "Download the voices from SourceForge now (about 6 MB)"; Flags: postinstall nowait skipifsilent; Check: VoicesMissing
Filename: "{app}\IcsTtsSettings.exe"; Description: "Open the voice settings"; Flags: postinstall nowait skipifsilent unchecked; Check: not VoicesMissing

[UninstallDelete]
; The converted voices; they can be imported again at any time.
Type: filesandordirs; Name: "{commonappdata}\Google ICS TTS"

[Code]
function VoicesMissing: Boolean;
var
  Dir: String;
begin
  Dir := ExpandConstant('{commonappdata}\Google ICS TTS\voices\');
  Result := not FileExists(Dir + 'ics_en_us.htsvoice') and not FileExists(Dir + 'ics_en_gb.htsvoice');
end;
