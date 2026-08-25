@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender"
cl /nologo /EHsc /O2 /std:c++17 /w /I "C:\Cloude AI\Tomb5 Native win 11 fix\TR5Randomizer\src" /Fe:ScriptProbe.exe ScriptProbe.cpp "C:\Cloude AI\Tomb5 Native win 11 fix\TR5Randomizer\src\core\ScriptDat.cpp"
