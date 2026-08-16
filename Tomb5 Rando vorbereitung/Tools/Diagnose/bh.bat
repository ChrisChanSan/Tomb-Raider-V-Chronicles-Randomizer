@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Cloude AI\Two gun test\Bittenichtbitdefender"
cl /nologo /EHsc /O2 /std:c++17 /w /Fe:TR3Hol.exe TR3Hol.cpp
