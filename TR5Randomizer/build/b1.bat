@echo off
set R=%~dp0..\..
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
REM The icon, the manifest and the version block (src\Tools.rc). rc writes
REM Randomize.res into the current directory, hence after the cd above.
rc /nologo /D RANDOMIZE /fo "Randomize.res" "%R%\TR5Randomizer\src\Tools.rc"
REM /guard:cf (compiler and linker): Control Flow Guard, the check Windows makes
REM before an indirect call. /CETCOMPAT marks the EXE as fit for the processor's
REM shadow stack. ASLR, DEP and the high-entropy address space are the linker's
REM defaults and stay on.
cl /nologo /EHsc /O2 /guard:cf /std:c++17 /I "%R%\TR5Randomizer\src" /Fe:Randomize.exe "%R%\TR5Randomizer\src\Randomize.cpp" "%R%\TR5Randomizer\src\core\Randomizer.cpp" "%R%\TR5Randomizer\src\core\Json.cpp" "%R%\TR5Randomizer\src\core\Rng.cpp" "%R%\TR5Randomizer\src\core\ApplyPatch.cpp" "%R%\TR5Randomizer\src\core\Tr5Level.cpp" /w /I "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib" "%R%\TR5Randomizer\src\core\Tr5Transfer.cpp" "%R%\TR5Randomizer\src\core\Tr5Tr3Import.cpp" "%R%\TR5Randomizer\src\core\Tr3Source.cpp" "%R%\TR5Randomizer\src\core\ScriptDat.cpp" "%R%\TR5Randomizer\src\core\SecretList.cpp" "%R%\TR5Randomizer\src\core\SecretReward.cpp" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%R%\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" "%R%\TR5Randomizer\src\core\LevelFix.cpp" "%R%\TR5Randomizer\src\core\LevelInfo.cpp" "%R%\TR5Randomizer\src\core\DataFolder.cpp" "%R%\TR5Randomizer\src\core\DataFile.cpp" "%R%\TR5Randomizer\src\core\EmbeddedData.cpp" "Randomize.res" /link /guard:cf /CETCOMPAT bcrypt.lib
