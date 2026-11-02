@echo off
rem basan - build the 64-bit engine under AddressSanitizer.
rem
rem The Asan|x64 configuration of tomb5.vcxproj: the engine of be64.bat
rem with /fsanitize=address, into releaseasan\PCtomb5_asan.exe. For test
rem runs only - it never goes into a package (Preparation\Verification.md,
rem section 9, says how to run it and how to read its report).
rem
rem The configuration is built with the newest toolset on the machine
rem (v145), not with the v142 of the other four: the v142 sanitizer runtime
rem stops at its own start on Windows 11 ("ASan init calls itself!") before
rem a line of the engine runs. v145 links the runtime as a DLL,
rem clang_rt.asan_dynamic-x86_64.dll, which the EXE needs beside it; this
rem copies it from the toolset into releaseasan\. It is the compiler's, not
rem the project's: it stays out of every package as well.
rem
rem The game_malloc pool is poisoned between its blocks in this build only
rem (specific\pool_guard.h); the other builds' pools are what they were.
rem
rem The shader guard and the full build as in be.bat and be64.bat.
call "%~dp0be.bat" guard
if errorlevel 1 exit /b 1

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0..\..\.."

msbuild "TOMB5 Code\tomb5.vcxproj" /t:Rebuild /p:Configuration=Asan /p:Platform=x64 /v:minimal /nologo
if errorlevel 1 exit /b 1

copy /Y "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" "releaseasan\" >nul
if errorlevel 1 (
	echo basan: clang_rt.asan_dynamic-x86_64.dll not found in %VCToolsInstallDir%bin\Hostx64\x64
	exit /b 1
)
echo   sanitizer runtime copied to releaseasan\
exit /b 0
