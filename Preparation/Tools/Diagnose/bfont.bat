@echo off
rem bfont - build FontProbe.
rem
rem The tool compiles the engine's own font matcher and the engine's own BC7
rem decoder, plus the two include files that carry the glyph table and the
rem accent table, so that what it prints is what the engine finds. The include
rem paths are therefore the engine's specific\ and game\ folders, and the
rem randomizer's src\ for core\Inflate.cpp, which unpacks the level file's
rem texture blocks and packs the PNG.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TOMB5 Code\TOMB5\specific" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\game" /I "%~dp0..\..\..\TR5Randomizer\src" /Fe:FontProbe.exe FontProbe.cpp "%~dp0..\..\..\TR5Randomizer\src\core\Inflate.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_fontmatch.cpp"
