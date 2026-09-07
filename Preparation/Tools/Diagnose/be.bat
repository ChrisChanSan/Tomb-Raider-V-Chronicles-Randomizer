@echo off
rem be - build the engine.
rem
rem THE SHADER GUARD (#123, fix round G, G2) runs first. The HD shaders have no
rem runtime compiler behind them: their bytecode is checked in as .inc byte
rem arrays beside the .hlsl they came from, and only bshader.bat regenerates
rem it. Round R3b changed hd_room.hlsl, never ran bshader, and shipped a build
rem whose source and whose picture disagreed - nothing in the tree could say
rem so, and it took a play test and two rounds to find.
rem
rem So every .inc carries the MD5 of its .hlsl on its last line (bshader.bat,
rem :stamp) and this compares them. A CONTENT stamp and not a file date,
rem because a fresh clone gives every file the same date and a date guard
rem would then either cry wolf or say nothing at all. A shader that is out of
rem step is rebuilt on the spot rather than reported - a build that quietly
rem draws the wrong thing is the failure this exists to stop, and asking the
rem user to type one more command is not a safer answer than doing it.
call :shaderguard
if errorlevel 1 exit /b 1

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0..\..\.."

rem THE BUILD STAMP (#123, fix round U, U6). winmain.cpp writes the first line
rem of every -log run, "PCtomb5 built <__DATE__ __TIME__>", and those two
rem macros are fixed when THAT ONE FILE is compiled. In an incremental build it
rem is not compiled: change a shader, or a file in specific\hd, and winmain.obj
rem is still yesterday's, so the log stamps a build that never happened. A play
rem test read 18:34 off a build made at 19:49, which is exactly the kind of
rem thing that costs a round of "but I ran the new one".
rem
rem So its object file goes before msbuild does, and the linker gets a freshly
rem compiled stamp every time. One file, a second of build time. setupdlg.cpp
rem carries the same __DATE__ in the launcher's window and goes with it.
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
