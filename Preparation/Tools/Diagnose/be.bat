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

rem The whole engine is compiled every time, never only what msbuild takes
rem for changed. msbuild goes by file dates: a source file put back with a
rem date older than its object file - a copy that keeps the date, an archive
rem unpacked over the tree - is passed over although its content is new, and
rem the link then joins object files of two states of one header. Such a
rem build compiles, links and can die at once: a struct that grew in the
rem object file that fills it and not in the one that holds it on its stack.
rem The full build takes about half a minute and leaves no such mix possible.
rem
rem It also keeps the first line of every -log run true: winmain.cpp writes
rem "PCtomb5 built <__DATE__ __TIME__>" there and setupdlg.cpp the same date
rem in the launcher's window, and every build compiles both afresh.

msbuild "TOMB5 Code\tomb5.vcxproj" /t:Rebuild /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
exit /b %errorlevel%

:shaderguard
setlocal enabledelayedexpansion
set "SHADERDIR=%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_shader"
set "STALE="

call :onestamp hd_room hd_room_vs.inc
call :onestamp hd_room hd_room_ps.inc
call :onestamp hd_room hd_room_gloss_ps.inc
call :onestamp hd_room hd_room_water_vs.inc
call :onestamp hd_room hd_room_water_ps.inc
call :onestamp hd_room hd_room_water_depth_ps.inc
call :onestamp hd_room hd_room_water_view_ps.inc
call :onestamp hd_room hd_room_water_under_ps.inc
call :onestamp hd_room hd_room_water_foot_ps.inc
call :onestamp hd_room hd_room_water_river_ps.inc
call :onestamp hd_room hd_room_water_foam_ps.inc
call :onestamp hd_room hd_room_water_foam_depth_ps.inc
call :onestamp hd_room hd_room_card_vs.inc
call :onestamp hd_room hd_room_card_ps.inc
call :onestamp hd_wet hd_wet_ps.inc
call :onestamp hd_wet hd_wet_debug_ps.inc
call :onestamp hd_model hd_model_vs.inc
call :onestamp hd_model hd_model_ps.inc
call :onestamp hd_glow hd_glow_ps.inc
call :incstamp hd_room hd_glow_ps.inc
call :onestamp hd_shadow hd_shadow_cast_vs.inc
call :onestamp hd_shadow hd_shadow_cast_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive2_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_model_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_mirror_ps.inc
call :onestamp hd_spring hd_spring_column_ps.inc
call :onestamp hd_spring hd_spring_bubble_ps.inc

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
call :onestamp hd_room hd_room_gloss_ps.inc
call :onestamp hd_room hd_room_water_vs.inc
call :onestamp hd_room hd_room_water_ps.inc
call :onestamp hd_room hd_room_water_depth_ps.inc
call :onestamp hd_room hd_room_water_view_ps.inc
call :onestamp hd_room hd_room_water_under_ps.inc
call :onestamp hd_room hd_room_water_foot_ps.inc
call :onestamp hd_room hd_room_water_river_ps.inc
call :onestamp hd_room hd_room_water_foam_ps.inc
call :onestamp hd_room hd_room_water_foam_depth_ps.inc
call :onestamp hd_room hd_room_card_vs.inc
call :onestamp hd_room hd_room_card_ps.inc
call :onestamp hd_wet hd_wet_ps.inc
call :onestamp hd_wet hd_wet_debug_ps.inc
call :onestamp hd_model hd_model_vs.inc
call :onestamp hd_model hd_model_ps.inc
call :onestamp hd_glow hd_glow_ps.inc
call :incstamp hd_room hd_glow_ps.inc
call :onestamp hd_shadow hd_shadow_cast_vs.inc
call :onestamp hd_shadow hd_shadow_cast_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive2_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_model_ps.inc
call :onestamp hd_shadow_receive hd_shadow_receive_mirror_ps.inc
call :onestamp hd_spring hd_spring_column_ps.inc
call :onestamp hd_spring hd_spring_bubble_ps.inc

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

:incstamp
rem %1 an .hlsl stem the shader of %2 includes, %2 the .inc. The include's
rem MD5 stands on the .inc's line "// bshader-include <stem> <MD5>"
rem (bshader.bat, :incstamp), so an edit to the included file alone - the
rem glow shader calls hd_room.hlsl's CausticOf - is caught as well.
set "H="
set "S="
set "G=%SHADERDIR%\%~1.hlsl"
set "F=%SHADERDIR%\%~2"
set "P=// bshader-include %~1 "
for /f "skip=1 delims=" %%H in ('certutil -hashfile "!G!" MD5') do (
	if not defined H set "H=%%H"
)
set "H=!H: =!"
for /f "tokens=4" %%S in ('findstr /b /c:"!P!" "!F!" 2^>nul') do set "S=%%S"
if not "!H!"=="!S!" set "STALE=!STALE! %~2"
exit /b 0
