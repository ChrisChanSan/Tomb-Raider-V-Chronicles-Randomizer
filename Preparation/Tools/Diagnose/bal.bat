@echo off
rem bal - build AddrLine (x64), the resolver of -rngtrace's addresses.
rem
rem Links Windows' own dbghelp.dll, nothing else: no symbol server, the
rem search path is the folder of the EXE it is given (AddrLine.cpp says
rem how). An x64 tool reads the PDB of either engine build.
rem
rem Run: AddrLine.exe <exe> [-ret] <rva> [<rva> ...]
rem  or: AddrLine.exe <exe> [-ret] @<file of addresses>
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W4 /Fe:AddrLine.exe AddrLine.cpp
