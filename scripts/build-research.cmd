@echo off
setlocal
cd /d "%~dp0.."
if "%~1"=="" exit /b 2
if not exist "%~1\" exit /b 2
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 3
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%I"
if not defined VS_PATH exit /b 3
call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x86 >nul
if errorlevel 1 exit /b 3
set "CL="
set "_CL_="
set "LINK="
set "_LINK_="
cl /nologo /EHsc /std:c++17 /O2 /MT /W4 /WX /Fe:"%~1\pvz-naruto-research.exe" /Fo:"%~1\research-cli.obj" src\native\research_cli.cpp
exit /b %errorlevel%
