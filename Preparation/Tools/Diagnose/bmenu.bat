@echo off
rem bmenu - build MenuTest.
rem
rem vcvars32, like bit.bat and bmt.bat and for the same reason: the tool
rem includes the engine's own game\menu_nav.h, game\key_scheme.h,
rem game\pad_scheme.h and specific\hd\hd_shadow.h rather than copies, and the
rem engine is a Win32 build. What the tool uses of them is `int` and `long`
rem arithmetic, the same widths in either target on Windows, so this is belt
rem and braces - but the tool exists to say what the engine does.
rem
rem Only this one file on the command line: all four headers are headers of
rem inline functions with no engine include at all, which is what lets them be checked
rem with a compiler and nothing else. The drawing half (game\menu_frame.cpp)
rem needs font_height and a device and is not testable here - by design, the
rem arithmetic was put in its own file so that it would be. The keyboard's
rem scan codes are numbers in key_scheme.h for the same reason (no dinput.h);
rem specific\input.cpp static_asserts each one against its DIK_ name.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:MenuTest.exe MenuTest.cpp
