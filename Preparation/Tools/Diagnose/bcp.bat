@echo off
rem bcp - build CutseqProbe.
rem
rem vcvars32, like bsd.bat and for the same reason: the tool takes the
rem cutscene structs from the engine's own global\types.h, whose PACKNODE
rem carries three pointers, four bytes each in the engine's Win32 build. It
rem decodes with the engine's own game\cutseq_decode.h and measures with
rem game\interpolate_math.h, both headers with no engine include, so this one
rem file is the whole command line. /w as in bsd.bat: types.h is the engine's
rem header, written for the engine's warning level, not for /W4.
rem
rem Run CutseqProbe.exe with no arguments (it reads the game folder's
rem data\cutseq.bin in place), with the path of another cutseq.bin, and with
rem -frames <n> <a> <b> for the decoded camera and roots of frames a..b of
rem cutscene n.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul 2>&1
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /Fe:CutseqProbe.exe CutseqProbe.cpp
