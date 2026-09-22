@echo off
rem Builds HeightCheck - headroom for every enemy model in a level.
rem Randomizer.cpp comes along for Rando::enemyKinds(): the enemy list must
rem not be a second, drifting copy.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:HeightCheck.exe HeightCheck.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Randomizer.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr3Source.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\LevelInfo.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Json.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Rng.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" /link bcrypt.lib
