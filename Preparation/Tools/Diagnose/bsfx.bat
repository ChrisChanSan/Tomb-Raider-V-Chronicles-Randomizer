@echo off
rem bsfx - build SfxProbe, the sound overlay's proof (#153, round M1).
rem
rem The name follows bmodel (ModelProbe), broom (RoomProbe) and btrg
rem (TrgProbe): b + what the tool looks at.
rem
rem x64, unlike bmodel: this one touches no Direct3D and no engine memory
rem numbers, only files, so there is nothing for it to be 32 bit about.
rem
rem It links the engine's own sources - no copy of them anywhere - so what
rem the tool prints is what the engine does:
rem   sfxbank.cpp       the MAIN.SFX parser and the RIFF plausibility rule
rem   hd_mods.cpp       the Mods folder - the scan, the banks, the loose wavs
rem   hd_install.cpp    where the installed remaster is, for the third link
rem   hd_markers.cpp    the .hdm sidecar, for -level (#153, round M3)
rem   Tr5Level.cpp      the randomizer's level reader, for the same switch
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:SfxProbe.exe SfxProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\sfxbank.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_mods.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_markers.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" /link advapi32.lib
