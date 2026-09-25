@echo off
rem blips - build LipsTest.
rem
rem vcvars32, like bmt.bat and for the same reason: the tool compiles the
rem engine's own sources beside itself rather than a copy, and the engine is a
rem Win32 build. Three of them, and no device:
rem   hd_trm.cpp      the head reader - it includes hd_trm.h and the standard
rem                   headers and nothing else
rem   hd_trl.cpp      the track reader, the map and the deform - the same
rem   hd_install.cpp  where the remaster is, read the way the engine reads it
rem                   (it needs advapi32 for the registry and Steam's path)
rem
rem Run LipsTest.exe with no arguments, or with the folder of .OBJ files the
rem fourth section compares against.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /D_CRT_SECURE_NO_WARNINGS /Fe:LipsTest.exe LipsTest.cpp "..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trm.cpp" "..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trl.cpp" "..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" advapi32.lib user32.lib
