@echo off
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo Visual Studio Installer not found. & exit /b 1)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%I"
if not defined VS_PATH (echo x86 C++ Build Tools not found. & exit /b 1)
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x86 >nul
if errorlevel 1 exit /b 1
if not exist scratch\native-check mkdir scratch\native-check
cl /nologo /EHsc /std:c++17 /W4 /WX /Fe:scratch\native-check\fixture.exe /Fo:scratch\native-check\fixture.obj tests\native_data_array.cpp
if errorlevel 1 exit /b 1
scratch\native-check\fixture.exe
if errorlevel 1 exit /b 1
cl /nologo /EHsc /std:c++17 /W4 /WX /Fe:scratch\native-check\snapshot.exe /Fo:scratch\native-check\snapshot.obj tests\native_candidate_snapshot.cpp
if errorlevel 1 exit /b 1
scratch\native-check\snapshot.exe
exit /b %errorlevel%
