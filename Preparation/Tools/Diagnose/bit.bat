@echo off
rem bit - build InterpTest.
rem
rem vcvars32, like bsd.bat and for a related reason: the tool includes the
rem engine's own game\interpolate_math.h and checks arithmetic on `short` and
rem `long`. Both are the same width in either target on Windows, so this is
rem belt and braces rather than a requirement - but the tool exists to say
rem what the ENGINE does, and the engine is a Win32 build.
rem
rem Run: InterpTest [path of the game's data\ANDREA1.TRC]. Lara's key frames
rem and bone table are read from that file at run time; without a path it is
rem looked for in the game folder beside the repository, and without the file
rem the eleven check blocks that need it fail, each with "no ANDREA1.TRC".
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:InterpTest.exe InterpTest.cpp
