@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TOMB5 Code\TOMB5\specific" /I "%~dp0..\..\..\TR5Randomizer\src" /DHDSCREEN_NO_ENGINE /Fe:DdsProbe.exe DdsProbe.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Inflate.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hdscreen.cpp"
