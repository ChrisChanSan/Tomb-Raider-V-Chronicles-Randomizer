@echo off
rem bmodel - build ModelProbe, the HD object path's picture proof.
rem
rem The name follows broom (RoomProbe), bmap (MapProbe) and btrg (TrgProbe):
rem b + what the tool looks at.
rem
rem x86, NOT x64 like most of its neighbours, and for the same reason broom is:
rem the engine that has to live with the memory numbers is a 32 bit process.
rem
rem It links the engine's own HD sources - no copy of them anywhere - so a
rem picture out of this tool is a picture out of the engine's renderer:
rem   hd_model.cpp      the object path under test
rem   hd_lara.cpp       Lara's composite - the wardrobe (S4-1)
rem   hd_render.cpp     the state bracket, the projection and the depth rule
rem   hd_light.cpp      the parity formulas hd_render needs
rem   hd_pagecache.cpp  BC7 -> A8R8G8B8 pages, shared by rooms and models
rem   hd_trm.cpp        the .TRM reader
rem   hd_scan / hd_bind slot -> model name, and where a page lives
rem   hd_classic.cpp  the donor game's own mesh tree and frame 0, out of
rem                   the classic level files of the I-III install
rem                   (#155) - -classicpose prints it
rem   hd_mods.cpp    the Mods folder - the resolver -mods drives (#153, M1)
rem   hd_wardrobe.cpp the outfit scan -wardrobe prints (#153, M2)
rem   hd_install.cpp  where the two remasters are, for that scan
rem   bc7.cpp           the decoder
rem   Trg.cpp           the vendored .TRG reader (hd_render needs it)
rem   Tr5Level.cpp      the randomizer's classic level reader - the mesh trees
rem   zlib              for the PNG's deflate
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:ModelProbe.exe ModelProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_model.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_lara.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_render.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_light.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_pagecache.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_dxt5.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_scan.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_bind.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_classic.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_mods.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_wardrobe.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trm.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_remaster\Trg.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_core\trf_file.c" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" user32.lib advapi32.lib
