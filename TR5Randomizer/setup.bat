@echo off
REM Opens the engine's setup dialog - resolution, renderer, controls.
REM Put this next to PCTomb5.exe; the release package does that for you.
cd /d "%~dp0"
start "" "PCTomb5.exe" -setup
