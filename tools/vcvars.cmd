@echo off
rem Sets up the MSVC build environment for the caller:
rem   call tools\vcvars.cmd vcvars64.bat   (or vcvars32.bat)
rem Visual Studio is found with vswhere, so any 2017-or-later edition or the
rem Build Tools work, wherever they are installed.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
	echo vswhere.exe was not found: install Visual Studio or the Build Tools with the C++ workload.
	exit /b 1
)
set VSDIR=
for /f "usebackq delims=" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (
	echo No Visual Studio with the C++ tools was found.
	exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\%1" >nul
