@echo off
rem be64 - build the engine as a 64-bit program.
rem
rem The same engine as be.bat, compiled for x64: release64\PCtomb5.exe,
rem the shipping name itself (#339; the package writes it PCTomb5.exe).
rem Since #324 this is the build that ships. The 32-bit one of be.bat is
rem release\PCtomb5_x86.exe, built on request to compare against - its own
rem folder and its own name, so neither build overwrites the other.
rem
rem The shader guard is be.bat's own ("be guard"): one list of shaders for
rem both builds, and a stale .inc is rebuilt here exactly as there.
rem
rem The whole engine is compiled every time, for the reason be.bat gives:
rem a build by file dates can join object files of two states of one
rem header. It also gives the 64-bit EXE its own build time, the first line
rem of a -log run ("PCtomb5 built <date time>").
rem
rem The compiler is the gate of the pointer conversions: a pointer cut to
rem 32 bits or a 32-bit value made a pointer (C4311, C4302, C4312), a size_t
rem cut (C4267) and a printf format that does not fit its argument (C4477)
rem are errors in tomb5.vcxproj, not warnings - in the 32-bit
rem configurations as well, which have none of them.
rem
rem "be64 test" builds the same engine as a test build: TOMB5_TEST_BUILD
rem (tomb5.vcxproj, property Tomb5Test) compiles the recorder in (-record,
rem -replay, -liveclock), -rngtrace and STATELOG, into
rem releasetest\PCtomb5_x64t.exe - a folder and a name of its own, so a
rem copy into the game folder never passes for the shipping EXE. A property
rem of the Release|x64 configuration, not a configuration of its own: one
rem would have to repeat every per-file setting of the project. The test
rem build records; basan.bat's, a test build as well, replays. The plain
rem "be64" is the shipping build and knows none of these switches.
call "%~dp0be.bat" guard
if errorlevel 1 exit /b 1

set "OUT=release64"
set "TEST="
if /i "%~1"=="test" (
	set "OUT=releasetest"
	set "TEST=/p:Tomb5Test=true"
)

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0..\..\.."

msbuild "TOMB5 Code\tomb5.vcxproj" /t:Rebuild /p:Configuration=Release /p:Platform=x64 %TEST% /v:minimal /nologo
if errorlevel 1 exit /b 1

rem The films: this build plays them through the three FFmpeg DLLs of
rem vendor\ffmpeg-min\bin, which it loads at run time from its own folder
rem (specific\fmv_av.cpp) - so they go beside the EXE, and a copy of the
rem folder takes them along. Without them the game runs without films.
copy /Y "vendor\ffmpeg-min\bin\*.dll" "%OUT%\" >nul
if errorlevel 1 (
	echo be64: the FFmpeg DLLs could not be copied to %OUT%\
	exit /b 1
)
echo   FFmpeg DLLs copied to %OUT%\

rem The C++ runtime (#324): the engine links it as DLLs (/MD), and the
rem three it imports - vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll -
rem go beside the EXE from the compiler's own redist folder, the one
rem vcvars64 names in VCToolsRedistDir. The folder's version is read, not
rem written down here; a DLL that is not there stops the build.
set "CRT="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC14?.CRT") do set "CRT=%%~fD"
if not defined CRT (
	echo be64: no Microsoft.VC14?.CRT folder in "%VCToolsRedistDir%x64"
	exit /b 1
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
	if not exist "%CRT%\%%F" (
		echo be64: %%F not found in "%CRT%"
		exit /b 1
	)
	copy /Y "%CRT%\%%F" "%OUT%\" >nul
	if errorlevel 1 (
		echo be64: %%F could not be copied to %OUT%\
		exit /b 1
	)
)
echo   C++ runtime DLLs copied to %OUT%\ from %CRT%
exit /b 0
