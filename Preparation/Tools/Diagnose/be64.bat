@echo off
rem be64 - build the engine as a 64-bit program.
rem
rem The same engine as be.bat, compiled for x64: release64\PCtomb5_x64.exe
rem beside release\PCtomb5.exe, never in its place - the two builds have
rem their own output folders, so neither overwrites the other. The 32-bit
rem build stays the one that ships; this one is checked against it.
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
call "%~dp0be.bat" guard
if errorlevel 1 exit /b 1

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0..\..\.."

msbuild "TOMB5 Code\tomb5.vcxproj" /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal /nologo
exit /b %errorlevel%
