@echo off
rem bsd - build SaveDiff.
rem
rem vcvars32 and not vcvars64 like every other tool here, and that is not a
rem detail: SaveDiff takes the savegame's layout from the engine's own
rem global\types.h with offsetof. The structs are #pragma pack(1) but they
rem carry pointers, and a pointer is FOUR bytes in the engine, which is a
rem Win32 build. Built for x64 every offset past Lara.spaz_effect would be
rem wrong and the tool would name the wrong field with a straight face.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /Fe:SaveDiff.exe SaveDiff.cpp
