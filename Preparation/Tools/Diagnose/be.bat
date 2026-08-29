@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0..\..\.."
msbuild "TOMB5 Code\tomb5.vcxproj" /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
