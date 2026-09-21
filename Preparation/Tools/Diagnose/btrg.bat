@echo off
rem btrg - build TrgProbe: what a Remaster level's .TRG contains and what its
rem pages cost to decode (#123, stage S0).
rem
rem It links the engine's own scan, binding and .TRM reader together with the
rem vendored TRForge .TRG reader - no copy of any of them - so the numbers the
rem tool prints are the numbers the engine would work with.
rem
rem AUDIT ROUND A1 (#172): every path here used to be this machine's own
rem absolute one, and the script cd'd into a folder outside the tree, so a
rem fresh clone could not run it. It is now the %~dp0 form of the other 43
rem scripts (broom.bat and bmodel.bat are the models) and TrgProbe.exe lands
rem beside this file the way theirs do. Only the path to vcvars stays absolute.
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /w /I "%~dp0..\..\..\TR5Randomizer\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src" /I "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib" /Fe:TrgProbe.exe TrgProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_scan.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_install.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_bind.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_mods.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_trm.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\bc7.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_remaster\Trg.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\vendor\trforge\src\trf_core\trf_file.c" "%~dp0..\..\..\TR5Randomizer\src\core\Tr5Level.cpp" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\adler32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\compress.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\crc32.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\deflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inffast.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inflate.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\inftrees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\trees.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\uncompr.c" "%~dp0..\..\..\TOMB5 Code\TOMB5\tomb5\libs\zlib\zutil.c" /link advapi32.lib
