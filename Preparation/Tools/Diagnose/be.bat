@echo off
rem be - build the engine.
rem
rem Runs the shader guard first. The HD shaders have no runtime compiler
rem behind them: their bytecode is checked in as .inc byte arrays beside the
rem .hlsl they came from, and only bshader.bat regenerates it. Without a
rem check, a changed .hlsl and a stale .inc can ship together with nothing in
rem the tree to say so - the source and the picture would disagree silently.
rem
rem Every .inc carries the MD5 of its .hlsl on its last line (bshader.bat,
rem :stamp) and this compares them. A content stamp, not a file date, because
rem a fresh clone gives every file the same date and a date guard would then
rem either cry wolf or say nothing at all.
rem
rem The MD5 is taken over bytes, so the checkout has to be byte-identical
rem everywhere; git's core.autocrlf=true checks HLSL out with carriage
rem returns on some machines and not others, which made the stamp mismatch on
rem a shader that was perfectly in step. .gitattributes now pins
rem **/hd_shader/*.hlsl and *.inc to eol=lf so the hash means the same thing
rem on every machine.
rem
rem A shader that is out of step is rebuilt on the spot rather than reported:
rem a build that quietly draws the wrong thing is the failure this exists to
rem stop.
call :shaderguard
if errorlevel 1 exit /b 1

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0..\..\.."

rem winmain.cpp writes the first line of every -log run, "PCtomb5 built
rem <__DATE__ __TIME__>", and those two macros are fixed when that one file is
rem compiled. In an incremental build it is not recompiled: change a shader,
rem or a file under specific\hd, and winmain.obj is still the old one, so the
rem log stamps a build that never happened.
rem
rem Deleting its object file before msbuild runs forces a fresh compile every
rem time, at the cost of one file and about a second of build time.
rem setupdlg.cpp carries the same __DATE__ in the launcher's window and goes
rem with it.
del /q "TOMB5 Code\Win32\Release\winmain.obj" 2>nul
del /q "TOMB5 Code\Win32\Release\setupdlg.obj" 2>nul

msbuild "TOMB5 Code\tomb5.vcxproj" /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
exit /b %errorlevel%

:shaderguard
setlocal enabledelayedexpansion
set "SHADERDIR=%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_shader"
set "STALE="

call :onestamp hd_room hd_room_vs.inc
call :onestamp hd_room hd_room_ps.inc
call :onestamp hd_model hd_model_vs.inc
call :onestamp hd_model hd_model_ps.inc

if not defined STALE (
	endlocal
	exit /b 0
)

echo be: regenerating shaders -!STALE! out of step with the HLSL
call "%~dp0bshader.bat"
if errorlevel 1 (
	echo be: bshader failed - the build would ship stale shader bytecode.
	endlocal
	exit /b 1
)

set "STALE="
call :onestamp hd_room hd_room_vs.inc
call :onestamp hd_room hd_room_ps.inc
call :onestamp hd_model hd_model_vs.inc
call :onestamp hd_model hd_model_ps.inc

if defined STALE (
	echo be: shaders STILL out of step after bshader -!STALE!
	endlocal
	exit /b 1
)

endlocal
exit /b 0

:onestamp
rem %1 the .hlsl stem, %2 the .inc beside it. Adds to STALE when they differ.
set "H="
set "S="
for /f "skip=1 delims=" %%H in ('certutil -hashfile "%SHADERDIR%\%~1.hlsl" MD5') do if not defined H set "H=%%H"
set "H=!H: =!"
for /f "tokens=3" %%S in ('findstr /b /c:"// bshader-stamp " "%SHADERDIR%\%~2" 2^>nul') do set "S=%%S"
if not "!H!"=="!S!" set "STALE=!STALE! %~2"
exit /b 0
