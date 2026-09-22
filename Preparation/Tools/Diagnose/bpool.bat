@echo off
rem bpool - build PoolProbe, the address space proof.
rem
rem The name follows be (the engine), bmodel (ModelProbe) and broom
rem (RoomProbe): b + what the tool looks at.
rem
rem x86, not x64, and that is the whole point of the tool: the engine is a
rem 32 bit process with LARGEADDRESSAWARE and the wall it runs into is four
rem gigabytes of address space. Built for x64 this tool would measure a wall
rem that is not there.
rem
rem And /LARGEADDRESSAWARE, which the linker does not set on its own for an
rem x86 executable: without it this tool would measure a two gigabyte address
rem space and the engine lives in a four gigabyte one (tomb5.vcxproj,
rem LargeAddressAware). Measured: 1995 MiB free without the switch and 3986
rem with it, in the same process on the same machine - a tool that measures
rem the wrong wall is worse than no tool.
rem
rem It links the engine's own source - no copy of it here:
rem   hd_memory.cpp   the GlobalMemoryStatusEx call, the VirtualQuery walk and
rem                   HDRedressDecide, which is the rule -grid tabulates
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /O2 /std:c++17 /W3 /Fe:PoolProbe.exe PoolProbe.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\specific\hd\hd_memory.cpp" user32.lib psapi.lib d3d9.lib /link /LARGEADDRESSAWARE
