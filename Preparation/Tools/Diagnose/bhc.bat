@echo off
rem Builds HeightCheck - headroom for every enemy model in a level.
rem Randomizer.cpp comes along for Rando::enemyKinds(): the enemy list must
rem not be a second, drifting copy.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /Fe:HeightCheck.exe HeightCheck.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Randomizer.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr3Source.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\LevelInfo.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Json.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Rng.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" /link bcrypt.lib
