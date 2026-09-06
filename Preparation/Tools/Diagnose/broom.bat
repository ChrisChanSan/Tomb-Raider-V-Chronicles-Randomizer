@echo off
rem broom - build RoomProbe, the HD room renderer's picture proof.
rem
rem The name follows bmap (MapProbe) and btrg (TrgProbe): b + what the tool
rem looks at. brp was taken by RoomCheck long before this.
rem
rem x86, NOT x64 like its neighbours, and on purpose: the tool measures the
rem peak COMMIT of the process for the two texture pools, and the engine that
rem has to live with that number is a 32 bit process. On x64 the measurement
rem comes out fine either way and means nothing.
rem
rem It links the engine's own HD sources - no copy of them anywhere - so a
rem picture out of this tool is a picture out of the engine's renderer:
rem   hd_render.cpp     the renderer under test
rem   hd_light.cpp      the two parity formulas, on the processor
rem   hd_pagecache.cpp  BC7 -> A8R8G8B8 pages, the texture budget
rem   hd_dxt5.cpp       the DXT5 encoder of the format switch
rem   hd_scan / hd_bind / hd_trm   HDScanPagePath and what it needs
rem   bc7.cpp           the decoder
rem   Trg.cpp           the vendored .TRG reader
rem   Tr5Level.cpp      the classic level, for the H1 water survey
rem   zlib              only for the PNG's deflate
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /I "%~dp0..\..\..\TR5Randomizer\src" /Fe:RoomProbe.exe RoomProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_render.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_light.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_pagecache.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_dxt5.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_scan.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_bind.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trm.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_remaster\Trg.cpp" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_core\trf_file.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" user32.lib
