@echo off
rem bmap - build MapProbe: which .TRM the install assigns to a level's slots,
rem and, with -hdm, what the .hdm sidecar of a randomized level decides, line
rem by line.
rem
rem It links the engine's own binding, marker reader and wardrobe - no copy of
rem any of them - so what this tool says is what the game will do. hd_lara.cpp
rem comes with the wardrobe's own name tables, and the eight hd_model calls in
rem its HDLaraLevelBuild are the reason hd_model.cpp and the chain under it
rem (render, light, page cache, dxt5, scan, the BC7 decoder and the vendored
rem .TRG reader) are on this line as well: the linker resolves them even
rem though this tool never opens a device and never calls a single one.
rem hd_wardrobe.cpp and hd_classic.cpp are on the line for the same reason -
rem without them the wardrobe calls are unresolved externals, and hd_trl.cpp
rem because hd_model.cpp keeps the speaking faces' lips. hd_install.cpp
rem goes with the wardrobe, which asks it where the remasters are installed;
rem MapProbe.cpp carries the two registry-override globals that file reads,
rem the same pair ModelProbe, SfxProbe and TrgProbe carry.
rem
rem Every path here is the %~dp0 form, like the other build scripts, so
rem MapProbe.exe lands beside this file from a fresh clone. Only the path to
rem vcvars stays absolute.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:MapProbe.exe MapProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_bind.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_mods.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_markers.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_lara.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_wardrobe.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_model.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_classic.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_render.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_vis.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_light.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_pagecache.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_dxt5.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_scan.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_remaster\Trg.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_core\trf_file.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trm.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trl.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" user32.lib advapi32.lib d3d9.lib
