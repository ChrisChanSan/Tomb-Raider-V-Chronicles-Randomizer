@echo off
rem bmenu - build MenuTest (#161, round 161b1, step 1).
rem
rem vcvars32, like bit.bat and bmt.bat and for the same reason: the tool
rem includes the ENGINE'S OWN game\menu_nav.h and game\key_scheme.h rather than
rem copies, and the engine is a Win32 build. Both are `int` and `long`
rem arithmetic, the same widths in either target on Windows, so this is belt
rem and braces - but the tool exists to say what the ENGINE does.
rem
rem Only this one file on the command line: both headers are headers of inline
rem functions with no engine include at all, which is what lets them be checked
rem with a compiler and nothing else. The drawing half (game\menu_frame.cpp)
rem needs font_height and a device and is not testable here - by design, the
rem arithmetic was put in its own file so that it would be. The keyboard's
rem scan codes are numbers in key_scheme.h for the same reason (no dinput.h);
rem specific\input.cpp static_asserts each one against its DIK_ name.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:MenuTest.exe MenuTest.cpp
