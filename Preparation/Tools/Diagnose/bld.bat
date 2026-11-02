@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /Fe:LevelDiff.exe LevelDiff.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Inflate.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\DataFolder.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Transfer.cpp"
