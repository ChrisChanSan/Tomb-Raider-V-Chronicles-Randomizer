/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#pragma once
#include "../global/types.h"

void ClearSurfaces();
bool WinRunCheck(LPSTR WindowName, LPSTR ClassName, HANDLE* mutex);
float WinFrameRate();
void WinProcMsg();
void WinProcessCommands(long cmd);
void CLSetup(char* cmd);
void CLNoFMV(char* cmd);
void WinProcessCommandLine(LPSTR cmd);
LRESULT CALLBACK WinMainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void WinClose();
bool WinRegisterWindow(HINSTANCE hinstance);
bool WinCreateWindow();
void WinSetStyle(bool fullscreen, ulong& set);
void WinMouseWant(long modern, long fmv);		// the game thread's half of the capture rule
void WinMouseTake(long* dx, long* dy, long* buttons);	// the counts and buttons since the last take
long WinMouseCaptured();				// is the cursor hidden and confined now
int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nShowCmd);

extern WINAPP App;
extern long resChangeCounter;
