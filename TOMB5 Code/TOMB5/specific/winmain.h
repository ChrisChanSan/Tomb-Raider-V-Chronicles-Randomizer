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
