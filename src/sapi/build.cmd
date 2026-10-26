@echo off
rem Builds the SAPI voice into build\sapi\x64 and build\sapi\x86 with Visual
rem Studio or its Build Tools. The C engine (src\native) is linked in statically and the C
rem runtime with it (/MT), so the DLLs need nothing else installed.
setlocal
cd /d "%~dp0"
call :build x64 vcvars64.bat || exit /b 1
call :build x86 vcvars32.bat || exit /b 1
exit /b 0

:build
setlocal
call ..\..\tools\vcvars.cmd %2 || exit /b 1
set OUT=..\..\build\sapi\%1
if not exist obj\%1 mkdir obj\%1
if not exist %OUT% mkdir %OUT%
set NATIVE=..\native
cl /nologo /O2 /MT /W3 /EHsc /std:c++17 /DUNICODE /D_UNICODE /DNOMINMAX /DICS_EXPORT= /D_CRT_SECURE_NO_WARNINGS ^
   /I%NATIVE% /I%NATIVE%\hts_engine_API /Foobj\%1\ ^
   engine.cpp paths.cpp runtime.cpp sapi_main.cpp settings.cpp text.cpp ^
   %NATIVE%\ics_hts.c %NATIVE%\ics_front.c %NATIVE%\ics_convert.c %NATIVE%\ics_util.c ^
   %NATIVE%\hts_engine_API\HTS_audio.c %NATIVE%\hts_engine_API\HTS_engine.c %NATIVE%\hts_engine_API\HTS_gstream.c ^
   %NATIVE%\hts_engine_API\HTS_label.c %NATIVE%\hts_engine_API\HTS_misc.c %NATIVE%\hts_engine_API\HTS_model.c ^
   %NATIVE%\hts_engine_API\HTS_pstream.c %NATIVE%\hts_engine_API\HTS_sstream.c %NATIVE%\hts_engine_API\HTS_vocoder.c ^
   /LD /Fe:%OUT%\ics_sapi.dll /link /NOLOGO /DEF:ics_sapi.def ^
   sapi.lib ole32.lib oleaut32.lib advapi32.lib shell32.lib winmm.lib || exit /b 1
del %OUT%\ics_sapi.exp %OUT%\ics_sapi.lib 2>nul
echo built %OUT%\ics_sapi.dll
rem The probe that tests\test_sapi.py drives.
cl /nologo /O2 /MT /W3 /EHsc /DUNICODE /D_UNICODE /Foobj\%1\ ..\..\tests\sapi_probe.cpp ^
   /Fe:%OUT%\sapi_probe.exe /link /NOLOGO ole32.lib sapi.lib || exit /b 1
echo built %OUT%\sapi_probe.exe
exit /b 0
