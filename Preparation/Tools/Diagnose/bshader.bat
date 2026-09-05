@echo off
rem bshader - compile the HD shaders with fxc and write the .inc byte arrays.
rem
rem The engine has no runtime shader compiler and no d3dx, so the bytecode is
rem checked in next to its HLSL source (GPLv3: the source has to be there).
rem This script is the only thing that regenerates it - run it after every
rem change to a .hlsl, then rebuild with be.bat.
setlocal enabledelayedexpansion

set "SHADERDIR=%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_shader"

set "FXC="
call :find "C:\Program Files (x86)\Windows Kits\10\bin"
call :find "C:\Program Files\Windows Kits\10\bin"

if not defined FXC (
	echo bshader: no fxc.exe found in any Windows Kits 10 bin folder.
	echo          Install the Windows SDK, or point FXC at it by hand.
	exit /b 1
)

echo bshader: using "%FXC%"

"%FXC%" /nologo /T vs_3_0 /E RoomVS /O3 /Vn kHDRoomVS /Fh "%SHADERDIR%\hd_room_vs.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

"%FXC%" /nologo /T ps_3_0 /E RoomPS /O3 /Vn kHDRoomPS /Fh "%SHADERDIR%\hd_room_ps.inc" "%SHADERDIR%\hd_room.hlsl"
if errorlevel 1 exit /b 1

for %%F in ("%SHADERDIR%\hd_room_vs.inc" "%SHADERDIR%\hd_room_ps.inc") do echo bshader: %%~nxF %%~zF bytes of C source
exit /b 0

:find
rem Newest SDK first - /o-n sorts the version folders descending by name.
if not exist "%~1" exit /b 0
for /f "delims=" %%D in ('dir /b /ad /o-n "%~1" 2^>nul') do (
	if not defined FXC if exist "%~1\%%D\x86\fxc.exe" set "FXC=%~1\%%D\x86\fxc.exe"
)
exit /b 0
