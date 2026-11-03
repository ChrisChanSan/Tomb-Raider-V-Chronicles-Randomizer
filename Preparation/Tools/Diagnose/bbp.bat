@echo off
rem bbp - build BinkProbe (x64 only).
rem
rem The tool is compiled against the headers of the minimal FFmpeg in
rem vendor\ffmpeg-min\include and linked against NO FFmpeg import library:
rem it loads avutil, avcodec and avformat at run time from the folder it is
rem given (vendor\ffmpeg-min\bin), the way the engine is to load them. Only
rem Windows' bcrypt.lib is linked, for the MD5 of the frames.
rem
rem Run: BinkProbe.exe "<repo>\vendor\ffmpeg-min\bin" <film.bik> [-frames]
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W3 /I "%~dp0..\..\..\vendor\ffmpeg-min\include" /Fe:BinkProbe.exe BinkProbe.cpp
