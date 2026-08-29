@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /EHsc /W3 /I "%~dp0..\..\..\TOMB5 Code\TOMB5" /Fe:BalanceCheck.exe BalanceCheck.cpp "%~dp0..\..\..\TOMB5 Code\TOMB5\game\balance.cpp"
