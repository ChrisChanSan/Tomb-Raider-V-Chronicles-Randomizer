@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender"
set R=C:\Cloude AI\Tomb5 Native win 11 fix\TR5Randomizer\src
set Z=C:\Cloude AI\Tomb5 Native win 11 fix\TOMB5 Code\TOMB5\tomb5\libs\zlib
cl /nologo /EHsc /O2 /std:c++17 /w /I "%R%" /I "%Z%" /Fe:BauWerk.exe BauWerk.cpp "%R%\core\Tr5Level.cpp" "%R%\core\ApplyPatch.cpp" "%R%\core\Json.cpp" "%R%\core\Tr5Transfer.cpp" "%R%\core\Tr5Tr3Import.cpp" "%R%\core\Tr3Source.cpp" "%Z%\adler32.c" "%Z%\compress.c" "%Z%\crc32.c" "%Z%\deflate.c" "%Z%\inffast.c" "%Z%\inflate.c" "%Z%\inftrees.c" "%Z%\trees.c" "%Z%\uncompr.c" "%Z%\zutil.c"
