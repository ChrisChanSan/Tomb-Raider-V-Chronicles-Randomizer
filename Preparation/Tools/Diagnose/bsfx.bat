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
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /Fe:SfxProbe.exe SfxProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\sfxbank.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_mods.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" /link advapi32.lib
