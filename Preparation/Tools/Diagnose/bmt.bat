@echo off
rem bmt - build ModernTest (#161, round 161a, step 2).
rem
rem vcvars32, like bit.bat and for the same reason: the tool compiles the
rem ENGINE'S OWN game\modern_move.cpp beside itself rather than a copy, and
rem the engine is a Win32 build. The translation is `short` and `long`
rem arithmetic, both the same width in either target on Windows, so this is
rem belt and braces - but the tool exists to say what the ENGINE does.
rem
rem The second source is the whole trick of this test: nothing is duplicated,
rem so nothing can drift. modern_move.cpp includes no engine header at all,
rem which is why it links here with a compiler and nothing else.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:ModernTest.exe ModernTest.cpp "..\..\..\TOMB5 Code\TOMB5\game\modern_move.cpp"
