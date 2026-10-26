@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
REM The manifest and the version block, from the randomizer's
REM src\Tools.rc. rc writes FixCheck.res into the current directory, hence
REM after the cd above.
rc /nologo /D FIXCHECK /fo "FixCheck.res" "%~dp0..\..\..\TR5Randomizer\src\Tools.rc"
REM /guard:cf (compiler and linker): Control Flow Guard, the check Windows makes
REM before an indirect call. /CETCOMPAT marks the EXE as fit for the processor's
REM shadow stack. ASLR, DEP and the high-entropy address space are the linker's
REM defaults and stay on.
cl /nologo /EHsc /O2 /guard:cf /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:FixCheck.exe FixCheck.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" "%~dp0..\..\..\TR5Randomizer\src\core\LevelFix.cpp" "FixCheck.res" /link /guard:cf /CETCOMPAT
REM The .res is only a step on the way into the EXE and no ignore rule covers
REM it in this folder; the build's own exit code is kept across the del.
set BUILT=%ERRORLEVEL%
del "FixCheck.res" 2>nul
exit /b %BUILT%
