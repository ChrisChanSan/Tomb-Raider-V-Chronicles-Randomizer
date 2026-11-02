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
cl /nologo /EHsc /O2 /guard:cf /std:c++17 /I "%R%\TR5Randomizer\src" /I "%R%\TR5Randomizer\external\imgui" /I "%R%\TR5Randomizer\external\imgui\backends" /Fe:TR5Randomizer.exe "%R%\TR5Randomizer\src\gui\Main.cpp" "%R%\TR5Randomizer\src\gui\Language.cpp" "%R%\TR5Randomizer\src\core\Randomizer.cpp" "%R%\TR5Randomizer\src\core\Json.cpp" "%R%\TR5Randomizer\src\core\Rng.cpp" "%R%\TR5Randomizer\src\core\ApplyPatch.cpp" "%R%\TR5Randomizer\src\core\Tr5Level.cpp" "%R%\TR5Randomizer\src\core\Inflate.cpp" /w "%R%\TR5Randomizer\src\core\Tr5Transfer.cpp" "%R%\TR5Randomizer\src\core\Tr5Tr3Import.cpp" "%R%\TR5Randomizer\src\core\Tr3Source.cpp" "%R%\TR5Randomizer\src\core\Config.cpp" "%R%\TR5Randomizer\src\core\BalanceFile.cpp" "%R%\TR5Randomizer\src\core\LevelFix.cpp" "%R%\TR5Randomizer\src\core\LevelInfo.cpp" "%R%\TR5Randomizer\src\core\DataFolder.cpp" "%R%\TR5Randomizer\src\core\DataFile.cpp" "%R%\TR5Randomizer\src\core\EmbeddedData.cpp" "%R%\TR5Randomizer\src\core\ScriptDat.cpp" "%R%\TR5Randomizer\src\core\SecretList.cpp" "%R%\TR5Randomizer\src\core\SecretReward.cpp" "%R%\TR5Randomizer\external\imgui\imgui.cpp" "%R%\TR5Randomizer\external\imgui\imgui_draw.cpp" "%R%\TR5Randomizer\external\imgui\imgui_tables.cpp" "%R%\TR5Randomizer\external\imgui\imgui_widgets.cpp" "%R%\TR5Randomizer\external\imgui\backends\imgui_impl_dx11.cpp" "%R%\TR5Randomizer\external\imgui\backends\imgui_impl_win32.cpp" "TR5Randomizer.res" /link /guard:cf /CETCOMPAT /SUBSYSTEM:WINDOWS d3d11.lib dxgi.lib d3dcompiler.lib user32.lib gdi32.lib shell32.lib ole32.lib bcrypt.lib
