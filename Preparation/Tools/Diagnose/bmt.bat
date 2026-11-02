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
rem
rem ModernLayout.cpp is the one part that does include the engine's
rem types.h, with global\disk_layout.h: the level file's and the save's
rem layouts, asserted at compile time and converted at run time.
rem
rem specific\high_pool.cpp, the walk of -high, is engine-free like
rem modern_move.cpp; on this 32-bit build its walk refuses, which is one
rem of the checks.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:ModernTest.exe ModernTest.cpp ModernLayout.cpp "..\..\..\TOMB5 Code\TOMB5\game\modern_move.cpp" "..\..\..\TOMB5 Code\TOMB5\game\modern_camera.cpp" "..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_vis.cpp" "..\..\..\TOMB5 Code\TOMB5\specific\high_pool.cpp"
