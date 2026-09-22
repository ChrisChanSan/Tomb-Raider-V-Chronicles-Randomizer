@echo off
rem bmt - build ModernTest.
rem
rem vcvars32, like bit.bat and for the same reason: the tool compiles the
rem engine's own game\modern_move.cpp beside itself rather than a copy, and
rem the engine is a Win32 build. The translation is `short` and `long`
rem arithmetic, both the same width in either target on Windows, so this is
rem belt and braces - but the tool exists to say what the engine does.
rem
rem The engine sources on the command line are the whole trick of this test:
rem nothing is duplicated, so nothing can drift. Neither modern_move.cpp nor
rem modern_camera.cpp (the orbit) includes an engine header at all, which is
rem why they link here with a compiler and nothing else.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:ModernTest.exe ModernTest.cpp "..\..\..\TOMB5 Code\TOMB5\game\modern_move.cpp" "..\..\..\TOMB5 Code\TOMB5\game\modern_camera.cpp"
