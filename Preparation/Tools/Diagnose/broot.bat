@echo off
rem broot - build RootProbe (#157).
rem
rem 64 bit like bam.bat: it reads a whole level file into memory through
rem Tr5Level and nothing in it cares which target the engine is. It includes
rem the engine's own game\interpolate_math.h - not a copy - so the wrap table
rem it prints is the arithmetic the engine really runs.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:RootProbe.exe RootProbe.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c"
