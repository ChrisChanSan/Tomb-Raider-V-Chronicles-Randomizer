@echo off
set R=%~dp0..\..
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
REM cd FIRST: rc writes TR5Randomizer.res into the CURRENT directory,
REM and the linker below looks for it beside the object files. With the
REM two lines the other way round the build only works when an old .res
REM happens to be lying in this folder already.
cd /d "%~dp0"
rc /nologo /fo "TR5Randomizer.res" "%R%\TR5Randomizer\src\gui\TR5Randomizer.rc"
REM /guard:cf (compiler and linker): Control Flow Guard, the check Windows makes
REM before an indirect call. /CETCOMPAT marks the EXE as fit for the processor's
REM shadow stack. ASLR, DEP and the high-entropy address space are the linker's
REM defaults and stay on.
REM /W4 /sdl /permissive-: the fourth warning level, the security
REM checks (the unsafe CRT calls are errors) and the standard's rules
REM instead of the compiler's old leniency. No /w anywhere on the line:
REM cl takes an option for the whole line, wherever it stands, and a /w
REM would silence every warning of every file.
cl /nologo /EHsc /O2 /MD /guard:cf /std:c++17 /W4 /sdl /permissive- /I "%R%\TR5Randomizer\src" /I "%R%\TR5Randomizer\external\imgui" /I "%R%\TR5Randomizer\external\imgui\backends" /Fe:TR5Randomizer.exe "%R%\TR5Randomizer\src\gui\Main.cpp" "%R%\TR5Randomizer\src\gui\Language.cpp" "%R%\TR5Randomizer\src\gui\Picture.cpp" "%R%\TR5Randomizer\src\core\Randomizer.cpp" "%R%\TR5Randomizer\src\core\Json.cpp" "%R%\TR5Randomizer\src\core\Rng.cpp" "%R%\TR5Randomizer\src\core\ApplyPatch.cpp" "%R%\TR5Randomizer\src\core\Tr5Level.cpp" "%R%\TR5Randomizer\src\core\Inflate.cpp" "%R%\TR5Randomizer\src\core\Tr5Transfer.cpp" "%R%\TR5Randomizer\src\core\Tr5Tr3Import.cpp" "%R%\TR5Randomizer\src\core\Tr3Source.cpp" "%R%\TR5Randomizer\src\core\Config.cpp" "%R%\TR5Randomizer\src\core\BalanceFile.cpp" "%R%\TR5Randomizer\src\core\LevelFix.cpp" "%R%\TR5Randomizer\src\core\LevelInfo.cpp" "%R%\TR5Randomizer\src\core\DataFolder.cpp" "%R%\TR5Randomizer\src\core\DataFile.cpp" "%R%\TR5Randomizer\src\core\ScriptDat.cpp" "%R%\TR5Randomizer\src\core\SecretList.cpp" "%R%\TR5Randomizer\src\core\SecretReward.cpp" "%R%\TR5Randomizer\external\imgui\imgui.cpp" "%R%\TR5Randomizer\external\imgui\imgui_draw.cpp" "%R%\TR5Randomizer\external\imgui\imgui_tables.cpp" "%R%\TR5Randomizer\external\imgui\imgui_widgets.cpp" "%R%\TR5Randomizer\external\imgui\backends\imgui_impl_dx11.cpp" "%R%\TR5Randomizer\external\imgui\backends\imgui_impl_win32.cpp" "TR5Randomizer.res" /link /guard:cf /CETCOMPAT /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib shell32.lib ole32.lib bcrypt.lib
if errorlevel 1 exit /b 1

REM The C++ runtime: /MD links it as DLLs, and the three the program
REM imports - vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll - go
REM beside it from the compiler's own redist folder (VCToolsRedistDir of
REM vcvars64), the same set be64.bat puts beside the engine.
set "CRT="
for /d %%D in ("%VCToolsRedistDir%x64\Microsoft.VC14?.CRT") do set "CRT=%%~fD"
if not defined CRT (
	echo b2: no Microsoft.VC14?.CRT folder in "%VCToolsRedistDir%x64"
	exit /b 1
)
for %%F in (vcruntime140.dll vcruntime140_1.dll msvcp140.dll) do (
	if not exist "%CRT%\%%F" (
		echo b2: %%F not found in "%CRT%"
		exit /b 1
	)
	copy /Y "%CRT%\%%F" "%~dp0" >nul
	if errorlevel 1 (
		echo b2: %%F could not be copied to "%~dp0"
		exit /b 1
	)
)
echo   C++ runtime DLLs copied to %~dp0 from %CRT%
exit /b 0
