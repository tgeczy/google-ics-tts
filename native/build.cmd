@echo off
rem Builds addon\engine\x64\ics_hts.dll and addon\engine\x86\ics_hts.dll with the
rem VS 2022 Build Tools.  /MT links the C runtime in, so no VC++ redistributable is
rem needed; x86 is for 32-bit NVDA (2023.x and older, including Windows 7).
setlocal
cd /d "%~dp0"
call :build x64 vcvars64.bat || exit /b 1
call :build x86 vcvars32.bat || exit /b 1
exit /b 0

:build
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\%2" >nul || exit /b 1
if not exist obj\%1 mkdir obj\%1
if not exist ..\addon\engine\%1 mkdir ..\addon\engine\%1
cl /nologo /O2 /TC /MT /W3 /D_CRT_SECURE_NO_WARNINGS /I. /Ihts_engine_API /Foobj\%1\ ^
   ics_hts.c ics_front.c ics_convert.c hts_engine_API\HTS_audio.c hts_engine_API\HTS_engine.c hts_engine_API\HTS_gstream.c ^
   hts_engine_API\HTS_label.c hts_engine_API\HTS_misc.c hts_engine_API\HTS_model.c ^
   hts_engine_API\HTS_pstream.c hts_engine_API\HTS_sstream.c hts_engine_API\HTS_vocoder.c ^
   /LD /Fe:..\addon\engine\%1\ics_hts.dll /link /NOLOGO winmm.lib || exit /b 1
del ..\addon\engine\%1\ics_hts.exp ..\addon\engine\%1\ics_hts.lib 2>nul
echo built ..\addon\engine\%1\ics_hts.dll
exit /b 0
