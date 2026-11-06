@echo off
set R=%~dp0..\..
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
REM The manifest and the version block (src\Tools.rc). rc writes
REM Randomize.res into the current directory, hence after the cd above.
rc /nologo /D RANDOMIZE /fo "Randomize.res" "%R%\TR5Randomizer\src\Tools.rc"
REM /guard:cf (compiler and linker): Control Flow Guard, the check Windows makes
REM before an indirect call. /CETCOMPAT marks the EXE as fit for the processor's
REM shadow stack. ASLR, DEP and the high-entropy address space are the linker's
REM defaults and stay on.
REM /W4 /sdl /permissive-: the fourth warning level, the security
REM checks (the unsafe CRT calls are errors) and the standard's rules
REM instead of the compiler's old leniency. No /w anywhere on the line:
REM cl takes an option for the whole line, wherever it stands, and a /w
REM would silence every warning of every file.
cl /nologo /EHsc /O2 /MD /guard:cf /std:c++17 /W4 /sdl /permissive- /I "%R%\TR5Randomizer\src" /Fe:Randomize.exe "%R%\TR5Randomizer\src\Randomize.cpp" "%R%\TR5Randomizer\src\core\Randomizer.cpp" "%R%\TR5Randomizer\src\core\Json.cpp" "%R%\TR5Randomizer\src\core\Rng.cpp" "%R%\TR5Randomizer\src\core\ApplyPatch.cpp" "%R%\TR5Randomizer\src\core\Tr5Level.cpp" "%R%\TR5Randomizer\src\core\Inflate.cpp" "%R%\TR5Randomizer\src\core\Tr5Transfer.cpp" "%R%\TR5Randomizer\src\core\Tr5Tr3Import.cpp" "%R%\TR5Randomizer\src\core\Tr3Source.cpp" "%R%\TR5Randomizer\src\core\ScriptDat.cpp" "%R%\TR5Randomizer\src\core\SecretList.cpp" "%R%\TR5Randomizer\src\core\SecretReward.cpp" "%R%\TR5Randomizer\src\core\ShareCode.cpp" "%R%\TR5Randomizer\src\core\LevelFix.cpp" "%R%\TR5Randomizer\src\core\LevelInfo.cpp" "%R%\TR5Randomizer\src\core\DataFolder.cpp" "%R%\TR5Randomizer\src\core\DataFile.cpp" "Randomize.res" /link /guard:cf /CETCOMPAT bcrypt.lib
if errorlevel 1 exit /b 1

REM The C++ runtime: /MD links it as DLLs, and the three the program
REM imports - vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll - go
REM beside it from the compiler's own redist folder (VCToolsRedistDir of
REM vcvars64), the same set be64.bat puts beside the engine.
set "CRT="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC14?.CRT") do set "CRT=%%~fD"
if not defined CRT (
	echo b1: no Microsoft.VC14?.CRT folder in "%VCToolsRedistDir%x64"
	exit /b 1
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
	if not exist "%CRT%\%%F" (
		echo b1: %%F not found in "%CRT%"
		exit /b 1
	)
	copy /Y "%CRT%\%%F" "%~dp0" >nul
	if errorlevel 1 (
		echo b1: %%F could not be copied to "%~dp0"
		exit /b 1
	)
)
echo   C++ runtime DLLs copied to %~dp0 from %CRT%
exit /b 0
