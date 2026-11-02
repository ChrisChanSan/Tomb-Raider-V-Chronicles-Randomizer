@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
set R=%~dp0..\..\..\TR5Randomizer\src
cl /nologo /EHsc /O2 /std:c++17 /w /I "%R%" /Fe:LevelBuild.exe LevelBuild.cpp "%R%\core\Tr5Level.cpp" "%R%\core\Inflate.cpp" "%R%\core\ApplyPatch.cpp" "%R%\core\Json.cpp" "%R%\core\Tr5Transfer.cpp" "%R%\core\Tr5Tr3Import.cpp" "%R%\core\Tr3Source.cpp"
