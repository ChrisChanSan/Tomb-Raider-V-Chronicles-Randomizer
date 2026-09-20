@echo off
rem bmenu - build MenuTest (#161, round 161b1, step 1).
rem
rem vcvars32, like bit.bat and bmt.bat and for the same reason: the tool
rem includes the ENGINE'S OWN game\menu_nav.h rather than a copy, and the
rem engine is a Win32 build. The header is `int` arithmetic, the same width in
rem either target on Windows, so this is belt and braces - but the tool exists
rem to say what the ENGINE does.
rem
rem Only this one file on the command line: menu_nav.h is a header of inline
rem functions with no engine include at all, which is what lets it be checked
rem with a compiler and nothing else. The drawing half (game\menu_frame.cpp)
rem needs font_height and a device and is not testable here - by design, the
rem arithmetic was put in its own file so that it would be.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:MenuTest.exe MenuTest.cpp
