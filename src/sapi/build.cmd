@echo off
rem Builds the SAPI voice into build\sapi:
rem   x64\ics_sapi.dll, x86\ics_sapi.dll   the engine, one per architecture
rem   x86\IcsTtsSettings.exe              the settings app (runs on both)
rem   x64,x86\sapi_probe.exe, x64\import_probe.exe   for the tests
rem   ..\googleIcsTts-sapi-<version>.exe  the installer, with "installer"
rem with Visual Studio or its Build Tools. The C engine (src\native) is linked
rem in statically and the C runtime with it (/MT), so nothing else is needed.
rem The version is the add-on's, from src\addon\manifest.ini.
setlocal
cd /d "%~dp0"
for /f "tokens=2 delims== " %%v in ('findstr /b /c:"version" ..\addon\manifest.ini') do set VER=%%v
if not defined VER (echo no version in src\addon\manifest.ini & exit /b 1)
if not exist obj mkdir obj
set VC=%VER:.=,%,0
> obj\version.h (
	echo #define ICS_VERSION_A "%VER%"
	echo #define ICS_VERSION L"%VER%"
	echo #define ICS_VERSION_COMMA %VC%
)
call :build x64 vcvars64.bat || exit /b 1
call :build x86 vcvars32.bat || exit /b 1
if /i "%1"=="installer" call :installer || exit /b 1
exit /b 0

:build
setlocal
call ..\..\tools\vcvars.cmd %2 || exit /b 1
set OUT=..\..\build\sapi\%1
if not exist obj\%1 mkdir obj\%1
if not exist %OUT% mkdir %OUT%
set NATIVE=..\native
set MINIZ=%NATIVE%\third_party\miniz
set CFLAGS=/nologo /O2 /MT /W3 /EHsc /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX /DICS_EXPORT= /D_CRT_SECURE_NO_WARNINGS ^
   /I. /Iobj /I%NATIVE% /I%NATIVE%\hts_engine_API /I%MINIZ%
set HTS=%NATIVE%\hts_engine_API\HTS_audio.c %NATIVE%\hts_engine_API\HTS_engine.c %NATIVE%\hts_engine_API\HTS_gstream.c ^
   %NATIVE%\hts_engine_API\HTS_label.c %NATIVE%\hts_engine_API\HTS_misc.c %NATIVE%\hts_engine_API\HTS_model.c ^
   %NATIVE%\hts_engine_API\HTS_pstream.c %NATIVE%\hts_engine_API\HTS_sstream.c %NATIVE%\hts_engine_API\HTS_vocoder.c

cl %CFLAGS% /Foobj\%1\ engine.cpp paths.cpp runtime.cpp sapi_main.cpp settings.cpp text.cpp ^
   %NATIVE%\ics_hts.c %NATIVE%\ics_front.c %NATIVE%\ics_convert.c %NATIVE%\ics_util.c %HTS% ^
   /LD /Fe:%OUT%\ics_sapi.dll /link /NOLOGO /DEF:ics_sapi.def ^
   sapi.lib ole32.lib oleaut32.lib advapi32.lib shell32.lib winmm.lib || exit /b 1
del %OUT%\ics_sapi.exp %OUT%\ics_sapi.lib 2>nul
echo built %OUT%\ics_sapi.dll

rem The probe that tests\test_sapi.py drives.
cl /nologo /O2 /MT /W3 /EHsc /DUNICODE /D_UNICODE /Foobj\%1\ ..\..\tests\sapi_probe.cpp ^
   /Fe:%OUT%\sapi_probe.exe /link /NOLOGO ole32.lib sapi.lib || exit /b 1
echo built %OUT%\sapi_probe.exe

rem The importer, the update check and the settings app; one 32-bit app serves both.
set IMPORT=importer.cpp http.cpp json.cpp updates.cpp paths.cpp settings.cpp ^
   %NATIVE%\ics_convert.c %NATIVE%\ics_util.c %MINIZ%\miniz.c
set IMPORT_LIBS=winhttp.lib bcrypt.lib advapi32.lib shell32.lib ole32.lib user32.lib
if not exist obj\%1\app mkdir obj\%1\app
if "%1"=="x64" (
	cl %CFLAGS% /Foobj\%1\app\ ..\..\tests\import_probe.cpp %IMPORT% ^
	   /Fe:%OUT%\import_probe.exe /link /NOLOGO %IMPORT_LIBS% || exit /b 1
	echo built %OUT%\import_probe.exe
)
if "%1"=="x86" (
	rc /nologo /i obj /fo obj\%1\settings_app.res settings_app.rc || exit /b 1
	cl %CFLAGS% /Foobj\%1\app\ settings_app.cpp %IMPORT% obj\%1\settings_app.res ^
	   /Fe:%OUT%\IcsTtsSettings.exe /link /NOLOGO /SUBSYSTEM:WINDOWS ^
	   %IMPORT_LIBS% comctl32.lib comdlg32.lib || exit /b 1
	echo built %OUT%\IcsTtsSettings.exe
)
exit /b 0

:installer
set ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe
if not exist "%ISCC%" (echo Inno Setup 6 is needed for the installer & exit /b 1)
"%ISCC%" /Q /DAppVersion=%VER% installer\google-ics-tts.iss || exit /b 1
echo built ..\..\build\googleIcsTts-sapi-%VER%.exe
exit /b 0
