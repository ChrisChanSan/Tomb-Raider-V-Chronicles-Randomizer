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
#include "../tomb5/pch.h"
#include "winmain.h"
#include "dxshell.h"
#include "function_stubs.h"
#include "output.h"
#include "../game/text.h"
#include "lighting.h"
#include "function_table.h"
#include "d3dmatrix.h"
#include "3dmath.h"
#include "audio.h"
#include "registry.h"
#include "../game/gameflow.h"
#include "texture.h"
#include "dxsound.h"
#include "gamemain.h"
#include "file.h"
#include "fmv.h"
#include "setupdlg.h"
#include "../tomb5/tomb5.h"	// load_tomb5_present_settings
#include "hd/hd_mods.h"		// the mods folder's log sink
#include "hd/hd_wardrobe.h"	// and the wardrobe scan's

WINAPP App;
long resChangeCounter;

static COMMANDLINES commandlines[] =
{
	{ "SETUP", 0, &CLSetup },
	{ "NOFMV", 0, &CLNoFMV }
};

// -log: switch the log on for this run.
//
// Deliberately does not go through the commandlines table above. That
// table is worked off inside WinProcessCommandLine, and by then the run
// check and the gameflow load have already run and logged - exactly the
// startup lines a diagnosis needs most would be missing. It is therefore
// read by hand as the first thing WinMain does.
//
// Matched as a whole word, unlike the table entries, which use a plain
// strstr: a stray "log" inside some path would otherwise silently turn on
// per-frame logging, which is the one thing this gate exists to prevent.
static bool WinCheckLogFlag(LPSTR cmd)
{
	long len, i;

	if (!cmd)
		return 0;

	len = (long)strlen(cmd);

	for (i = 0; i + 3 <= len; i++)
	{
		// Through unsigned char: a path with umlauts in it hands toupper a
		// negative value otherwise, which it is not defined for.
		if (toupper((uchar)cmd[i])     != 'L' ||
			toupper((uchar)cmd[i + 1]) != 'O' ||
			toupper((uchar)cmd[i + 2]) != 'G')
			continue;

		// What stands in front of it: start of line, a space or a switch
		// character - not a letter of a longer word.
		if (i > 0 && cmd[i - 1] != ' ' && cmd[i - 1] != '-' && cmd[i - 1] != '/')
			continue;

		// And what follows: end of line or a space.
		if (i + 3 < len && cmd[i + 3] != ' ')
			continue;

		return 1;
	}

	return 0;
}

void ClearSurfaces()
{
	D3DRECT r;

	r.x1 = App.dx.rViewport.left;
	r.y1 = App.dx.rViewport.top;
	r.y2 = App.dx.rViewport.top + App.dx.rViewport.bottom;
	r.x2 = App.dx.rViewport.left + App.dx.rViewport.right;

	DXAttempt(App.dx.lpD3DDevice->Clear(1, &r, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0F, 0));
	S_DumpScreen();
}

bool WinRunCheck(LPSTR WindowName, LPSTR ClassName, HANDLE* mutex)
{
	HWND window;

	Log(__FUNCTION__);
	*mutex = CreateMutex(0, 1, WindowName);

	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		window = FindWindow(ClassName, WindowName);

		if (window)
		{
			SendMessage(window, WM_ACTIVATE, WA_ACTIVE, 0);
			SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			SetWindowPos(window, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		}

		return 1;
	}

	return 0;
}

float WinFrameRate()
{
	double t, time_now;
	static float fps;
	static long time, counter;
	static char first_time;

	if (!(first_time & 1))
	{
		first_time |= 1;
		time = clock();
	}

	counter++;

	if (counter == 10)
	{
		time_now = clock();
		t = (time_now - time) / (double)CLOCKS_PER_SEC;
		time = (long)time_now;
		fps = float(counter / t);
		counter = 0;
	}

	App.fps = fps;
	return fps;
}

void WinProcMsg()
{
	MSG msg;

	Log(__FUNCTION__);

	do
	{
		GetMessage(&msg, 0, 0, 0);

		if (!TranslateAccelerator(App.hWnd, App.hAccel, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	while (!MainThread.ended && msg.message != WM_QUIT);
}

void WinProcessCommands(long cmd)
{
	DXDISPLAYMODE* dm;
	long odm;

	if (cmd == KA_ALTENTER)
	{
		if (App.fmv || !G_dxinfo->DDInfo[G_dxinfo->nDD].bCanRenderWindowed || LevelLoadingThread.active)
			return;

		Log("KA_ALTENTER");

		Log("HangGameThread");
		while (App.dx.InScene) {};
		App.dx.WaitAtBeginScene = 1;
		while (!App.dx.InScene) {};
		SuspendThread((HANDLE)MainThread.handle);
		Log("Game Thread Suspended");

		DXToggleFullScreen();
		HWInitialise();
		S_InitD3DMatrix();
		aSetViewMatrix();
		ResumeThread((HANDLE)MainThread.handle);
		App.dx.WaitAtBeginScene = 0;
		Log("Game Thread Resumed");

		if (App.dx.Flags & DXF_FULLSCREEN)
		{
			SetCursor(0);
			ShowCursor(0);
		}
		else
		{
			SetCursor(LoadCursor(0, IDC_ARROW));
			ShowCursor(1);
		}
	}
	else if (cmd == KA_ALTP || cmd == KA_ALTM)
	{
		if (LevelLoadingThread.active || App.fmv)
			return;

		Log("Change Video Mode");

		Log("HangGameThread");
		while (App.dx.InScene) {};
		App.dx.WaitAtBeginScene = 1;
		while (!App.dx.InScene) {};
		SuspendThread((HANDLE)MainThread.handle);
		Log("Game Thread Suspended");

		odm = App.DXInfo.nDisplayMode;

		if (cmd == KA_ALTP)
		{
			App.DXInfo.nDisplayMode++;

			if (App.DXInfo.nDisplayMode >= G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].nDisplayModes)
				App.DXInfo.nDisplayMode = G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].nDisplayModes - 1;

			dm = G_dxinfo->DDInfo[App.DXInfo.nDD].D3DDevices[App.DXInfo.nD3D].DisplayModes;

			while (dm[odm].bpp != dm[App.DXInfo.nDisplayMode].bpp)
			{
				App.DXInfo.nDisplayMode++;

				if (App.DXInfo.nDisplayMode >= G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].nDisplayModes)
				{
					App.DXInfo.nDisplayMode = odm;
					break;
				}
			}
		}
		else
		{
			App.DXInfo.nDisplayMode--;

			if (App.DXInfo.nDisplayMode < 0)
				App.DXInfo.nDisplayMode = 0;

			dm = G_dxinfo->DDInfo[App.DXInfo.nDD].D3DDevices[App.DXInfo.nD3D].DisplayModes;

			while (dm[odm].bpp != dm[App.DXInfo.nDisplayMode].bpp)
			{
				App.DXInfo.nDisplayMode--;

				if (App.DXInfo.nDisplayMode < 0)
				{
					App.DXInfo.nDisplayMode = odm;
					break;
				}
			}
		}

		if (odm != App.DXInfo.nDisplayMode)
		{
			if (!DXChangeVideoMode())
			{
				App.DXInfo.nDisplayMode = odm;
				DXChangeVideoMode();
			}

			HWInitialise();
			InitWindow(0, 0, App.dx.dwRenderWidth, App.dx.dwRenderHeight, 20, 20480, 80, App.dx.dwRenderWidth, App.dx.dwRenderHeight);
			InitFont();
			S_InitD3DMatrix();
			aSetViewMatrix();
		}

		ResumeThread((HANDLE)MainThread.handle);
		App.dx.WaitAtBeginScene = 0;
		Log("Game Thread Resumed");
		/* Four seconds of the resolution caption, counted in logic ticks,
		 * which are thirty a second at every frame rate, so this is
		 * vanilla's own 120 again and needs no rate baked into it - see the
		 * note beside the countdown in S_OutputPolyList. */
		resChangeCounter = 120;
	}
}

void CLSetup(char* cmd)
{
	Log("CLSetup");

	if (cmd)
		start_setup = 0;
	else
		start_setup = 1;
}

void CLNoFMV(char* cmd)
{
	Log("CLNoFMV");

	if (cmd)
		fmvs_disabled = 0;
	else
		fmvs_disabled = 1;
}

void WinProcessCommandLine(LPSTR cmd)
{
	COMMANDLINES* command;
	char* pCommand;
	char* p;
	char* last;
	ulong l;
	long num;
	char parameter[20];

	Log(__FUNCTION__);

	num = sizeof(commandlines) / sizeof(commandlines[0]);

	for (int i = 0; i < num; i++)
	{
		command = &commandlines[i];
		command->code((char*)"_INIT");
	}

	for (int i = 0; (ulong)i < strlen(cmd); i++)
	{
		if (toupper(cmd[i]))
			cmd[i] = toupper(cmd[i]);
	}

	for (int i = 0; i < num; i++)
	{
		command = &commandlines[i];
		memset(parameter, 0, sizeof(parameter));
		pCommand = strstr(cmd, command->command);

		if (pCommand)
		{
			if (command->needs_parameter)
			{
				p = 0;
				l = strlen(pCommand);

				for (int j = 0; (ulong)j < l; j++, pCommand++)
				{
					if (*pCommand != '=')
						continue;

					p = pCommand + 1;
					l = strlen(p);

					for (j = 0; (ulong)j < l; j++, p++)
					{
						if (*p != ' ')
							break;
					}

					last = p;
					l = strlen(last);

					for (j = 0; (ulong)j < l; j++, last++)
					{
						if (*last == ' ')
							break;
					}

					strncpy(parameter, p, j);
					break;
				}

				command->code(parameter);
			}
			else
				command->code(0);
		}
	}
}

/* The mouse: Raw Input, read here and handed to the game thread.
 *
 * Two threads meet in this block. The window procedure runs on the main
 * thread, inside WinProcMsg's GetMessage loop, which does nothing else once
 * the game thread is started - so WM_INPUT is handled as it arrives, at the
 * mouse's own report rate and independent of the frame rate. The game thread
 * (GameMain) takes what has gathered once per S_UpdateInput
 * (specific\input.cpp), which is once per logic tick in play.
 *
 * No lock: the counts and the buttons are LONGs changed only through the
 * Interlocked functions, so a take and an add cannot lose a count between
 * them. A lock would also be a hazard of its own here: WM_ACTIVATE suspends
 * the game thread with SuspendThread, and a game thread suspended while
 * holding a lock would leave this thread waiting on it for good.
 *
 * The cursor: captured - hidden and confined to the client area - only while
 * the window is active, the control type is Modern and no FMV plays. The game
 * thread reports the last two (WinMouseWant); the window's own messages report
 * the first. ShowCursor and ClipCursor are only ever called on this thread,
 * because ShowCursor's counter belongs to the thread that owns the window.
 * While the cursor is not captured, the moves are dropped where they arrive;
 * the buttons still count, as a keyboard key does. */
#define WM_APP_MOUSE		(WM_APP + 1)	// the game thread's half of the rule changed
#define MOUSE_WANT_MODERN	1
#define MOUSE_WANT_FMV		2

static volatile LONG mouse_dx;		// counts gathered since the last take
static volatile LONG mouse_dy;
static volatile LONG mouse_held;	// the five buttons as they stand, bit n = button n
static volatile LONG mouse_pressed;	// every button that went down since the last take
static volatile LONG mouse_want;	// MOUSE_WANT_*, written by the game thread
static volatile LONG mouse_captured;	// read by the game thread for MOUSELOG

// This thread's alone.
static long mouse_active;		// the window is the active one and not minimised
static long mouse_closing;		// WM_CLOSE or WinClose: nothing captures any more
static long mouse_hidden;		// how many ShowCursor(FALSE) calls the capture made
static long mouse_abs_seeded;		// an absolute device has given a first position
static long mouse_abs_x, mouse_abs_y;

/* Seats or frees the cursor to match the rule, on this thread. Called on every
 * message that can change a term, and on the game thread's WM_APP_MOUSE.
 *
 * ShowCursor keeps a counter, and the cursor shows while it is 0 or more. The
 * capture lowers it until it is below 0 and counts how many calls that took;
 * the release raises it by exactly that many, so it ends where it was found -
 * a full screen that had it at -1 stays hidden. The clip is set again on every
 * call while captured, because a move or a resize changes the client area. */
static void WinMouseCapture()
{
	const LONG want = mouse_want;
	const long modern = (want & MOUSE_WANT_MODERN) ? 1 : 0;
	const long fmv = (want & MOUSE_WANT_FMV) ? 1 : 0;
	const long on = (mouse_active && modern && !fmv && !mouse_closing && App.hWnd) ? 1 : 0;

	if (on)
	{
		RECT rc;

		GetClientRect(App.hWnd, &rc);
		MapWindowPoints(App.hWnd, 0, (POINT*)&rc, 2);
		ClipCursor(&rc);
	}

	if (on == mouse_captured)
		return;

	if (on)
	{
		// Bounded: a counter that some other code has raised far above 0
		// would otherwise keep this loop going.
		do
			mouse_hidden++;
		while (ShowCursor(FALSE) >= 0 && mouse_hidden < 16);
	}
	else
	{
		ClipCursor(0);

		for (; mouse_hidden > 0; mouse_hidden--)
			ShowCursor(TRUE);
	}

	InterlockedExchange(&mouse_captured, on);

	// A capture starts from nothing: counts gathered before it would jolt the
	// camera on its first tick.
	InterlockedExchange(&mouse_dx, 0);
	InterlockedExchange(&mouse_dy, 0);
	mouse_abs_seeded = 0;

	if (gLogEnabled)
		Log("MOUSELOG capture=%s active=%ld modern=%ld fmv=%ld", on ? "on" : "off", mouse_active, modern, fmv);
}

// The window's own term. Losing it also lets go of every button: their up
// messages go to whichever window has the focus now.
static void WinMouseActive(long active)
{
	mouse_active = active;

	if (!active)
	{
		InterlockedExchange(&mouse_held, 0);
		InterlockedExchange(&mouse_pressed, 0);
	}

	WinMouseCapture();
}

/* One WM_INPUT. Buttons first, whether captured or not; then the move, only
 * while captured.
 *
 * A mouse reports relative counts. A tablet, a touch screen or a remote
 * desktop reports an absolute position instead, 0..65535 across the screen
 * or the virtual desktop; that is turned into pixels and then into a
 * difference against the previous position, which is as near to a count as
 * such a device has. The first position after a capture only seeds it. */
static void WinMouseInput(HRAWINPUT handle)
{
	RAWINPUT raw;
	UINT size = sizeof(raw);
	LONG held, pressed;
	long dx, dy;

	if (GetRawInputData(handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1)
		return;

	if (raw.header.dwType != RIM_TYPEMOUSE)
		return;

	held = mouse_held;
	pressed = 0;

	// RI_MOUSE_BUTTON_1_DOWN .. RI_MOUSE_BUTTON_5_UP are 0x001 .. 0x200: button
	// n goes down on bit 2n and up on bit 2n + 1, in the order left, right,
	// middle, X1, X2 - the order of KEY_MOUSE_* (game\key_scheme.h).
	for (long n = 0; n < 5; n++)
	{
		const USHORT down = (USHORT)(1 << (2 * n));
		const USHORT up = (USHORT)(1 << (2 * n + 1));

		if (raw.data.mouse.usButtonFlags & down)
		{
			held |= 1L << n;
			pressed |= 1L << n;
		}

		if (raw.data.mouse.usButtonFlags & up)
			held &= ~(1L << n);
	}

	InterlockedExchange(&mouse_held, held);

	if (pressed)
		InterlockedOr(&mouse_pressed, pressed);

	if (!mouse_captured)
		return;

	if (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)
	{
		const bool virt = (raw.data.mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
		const long w = GetSystemMetrics(virt ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
		const long h = GetSystemMetrics(virt ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
		const long x = (long)(((__int64)raw.data.mouse.lLastX * w) >> 16);
		const long y = (long)(((__int64)raw.data.mouse.lLastY * h) >> 16);

		dx = mouse_abs_seeded ? x - mouse_abs_x : 0;
		dy = mouse_abs_seeded ? y - mouse_abs_y : 0;
		mouse_abs_x = x;
		mouse_abs_y = y;
		mouse_abs_seeded = 1;
	}
	else
	{
		dx = raw.data.mouse.lLastX;
		dy = raw.data.mouse.lLastY;
	}

	if (dx)
		InterlockedExchangeAdd(&mouse_dx, dx);

	if (dy)
		InterlockedExchangeAdd(&mouse_dy, dy);
}

/* The game thread's two terms, reported on every S_UpdateInput. Only a change
 * wakes the window thread, so the menus, the FMV player and play all reach it
 * the moment the control type is switched or a video starts or ends. */
void WinMouseWant(long modern, long fmv)
{
	const LONG want = (modern ? MOUSE_WANT_MODERN : 0) | (fmv ? MOUSE_WANT_FMV : 0);

	if (InterlockedExchange(&mouse_want, want) != want && App.hWnd)
		PostMessage(App.hWnd, WM_APP_MOUSE, 0, 0);
}

/* What gathered since the last take, for the game thread: the counts, and the
 * buttons down now or pressed in between - so a click that went down and up
 * between two ticks still counts for one. */
void WinMouseTake(long* dx, long* dy, long* buttons)
{
	*dx = InterlockedExchange(&mouse_dx, 0);
	*dy = InterlockedExchange(&mouse_dy, 0);
	*buttons = InterlockedExchange(&mouse_pressed, 0) | mouse_held;
}

long WinMouseCaptured()
{
	return mouse_captured;
}

// Frees the cursor for good: the window is closing.
static void WinMouseRelease()
{
	mouse_closing = 1;
	WinMouseCapture();
}

LRESULT CALLBACK WinMainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static bool closing;

	switch (uMsg)
	{
	case WM_CREATE:
		resChangeCounter = 0;
		Log("WM_CREATE");
		break;

	case WM_MOVE:
		Log("WM_MOVE");
		DXMove((short)lParam, short((lParam >> 16) & 0xFFFF));
		WinMouseCapture();	// the clip follows the client area
		break;

	case WM_SIZE:

		/* The window's term is taken afresh here as well: a window restored
		 * from the taskbar gets its WM_ACTIVATE while it is still minimised,
		 * which says inactive, and a later WM_SETFOCUS is not certain, so the
		 * restore itself is what says it is the active window again. */
		WinMouseActive((GetForegroundWindow() == hwnd && !IsIconic(hwnd)) ? 1 : 0);
		break;

	case WM_INPUT:
		WinMouseInput((HRAWINPUT)lParam);
		break;			// DefWindowProc still has to see it, to free the input

	case WM_APP_MOUSE:
		WinMouseCapture();
		return 0;

	case WM_SETFOCUS:
		WinMouseActive(IsIconic(hwnd) ? 0 : 1);
		break;

	case WM_KILLFOCUS:
		WinMouseActive(0);
		break;

	case WM_ACTIVATE:

		/* Before everything else in this case: the FMV guard below returns
		 * early, and a window that goes inactive during a video must still
		 * give up its buttons. */
		WinMouseActive((wParam & 0xFFFF) != WA_INACTIVE && !HIWORD(wParam));

		if (!closing)
		{
			if (App.fmv)
				return 0;

			switch (wParam & 0xFFFF)
			{
			case WA_INACTIVE:
				Log("WM_INACTIVE");

				if (App.SetupComplete)
				{
					// Bounded wait: the game thread can occasionally stall for
					// several seconds inside the graphics driver's Present()
					// call (e.g. stuck in nvd3dum during the level-load
					// progress bar, unrelated to focus changes). This handler
					// runs synchronously on the window procedure, so an
					// unbounded spin here while the game thread never reaches
					// a safe point would freeze the whole application with no
					// way to recover short of killing the process. Give it a
					// couple of seconds, then leave the game thread running
					// unpaused rather than deadlock the app over a driver
					// hiccup.
					DWORD waitStart = GetTickCount();
					bool safeToSuspend = true;

					Log("HangGameThread");

					while (App.dx.InScene)
					{
						if (GetTickCount() - waitStart > 2000)
						{
							safeToSuspend = false;
							break;
						}
					}

					if (safeToSuspend)
					{
						App.dx.WaitAtBeginScene = 1;
						waitStart = GetTickCount();

						while (!App.dx.InScene)
						{
							if (GetTickCount() - waitStart > 2000)
							{
								safeToSuspend = false;
								break;
							}
						}
					}

					if (safeToSuspend)
					{
						SuspendThread((HANDLE)MainThread.handle);
						Log("Game Thread Suspended");
					}
					else
					{
						App.dx.WaitAtBeginScene = 0;
						Log("Game Thread appeared stuck, skipping suspend to avoid a deadlock");
					}
				}

				return 0;

			case WA_ACTIVE:
			case WA_CLICKACTIVE:
				Log("WM_ACTIVE");

				if (App.SetupComplete)
				{
					ResumeThread((HANDLE)MainThread.handle);
					App.dx.WaitAtBeginScene = 0;
					Log("Game Thread Resumed");
				}

				return 0;
			}
		}

		break;

	case WM_CLOSE:
		closing = 1;
		WinMouseRelease();
		PostQuitMessage(0);
		break;

	case WM_COMMAND:
		Log("WM_COMMAND");
		WinProcessCommands(wParam & 0xFFFF);
		break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void WinClose()
{
	Log(__FUNCTION__);
	WinMouseRelease();	// the clip is the whole system's, not only this window's
	SaveSettings();
	CloseHandle(App.mutex);
	DXFreeInfo(&App.DXInfo);
	DestroyAcceleratorTable(App.hAccel);
	DXClose();
	FreeBinkStuff();

	if (!G_dxptr)
		return;

	DXAttempt(G_dxptr->Keyboard->Unacquire());

	if (G_dxptr->Keyboard)
	{
		Log("Released %s @ %x - RefCnt = %d", "Keyboard", G_dxptr->Keyboard, G_dxptr->Keyboard->Release());
		G_dxptr->Keyboard = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "Keyboard");

	// Catch up with the enumeration thread first: it still uses
	// lpDirectInput. Quit the game within the first seconds and it would
	// otherwise keep running while the interface is released here.
	DXWaitJoystickInit();

	if (G_dxptr->lpDirectInput)
	{
		Log("Released %s @ %x - RefCnt = %d", "DirectInput", G_dxptr->lpDirectInput, G_dxptr->lpDirectInput->Release());
		G_dxptr->lpDirectInput = 0;
	}
	else
		Log("%s Attempt To Release NULL Ptr", "DirectInput");
}

bool WinRegisterWindow(HINSTANCE hinstance)
{
	App.hInstance = hinstance;
	App.WindowClass.hIcon = 0;
	App.WindowClass.lpszMenuName = 0;
	App.WindowClass.lpszClassName = "MainGameWindow";
	App.WindowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	App.WindowClass.hInstance = hinstance;
	App.WindowClass.style = CS_VREDRAW | CS_HREDRAW;
	App.WindowClass.lpfnWndProc = WinMainWndProc;
	App.WindowClass.cbClsExtra = 0;
	App.WindowClass.cbWndExtra = 0;
	App.WindowClass.hCursor = LoadCursor(0, IDC_ARROW);

	if (!RegisterClass(&App.WindowClass))
		return 0;

	return 1;
}

bool WinCreateWindow()
{
	App.hWnd = CreateWindowEx(WS_EX_APPWINDOW, "MainGameWindow", "Tomb Raider Chronicles", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		0, 0, App.hInstance, 0);

	if (!App.hWnd)
		return 0;

	/* The mouse as Raw Input: usage page 1 (generic desktop), usage 2 (mouse),
	 * delivered to this window as WM_INPUT while it is in the foreground. No
	 * flags: RIDEV_NOLEGACY would take the ordinary mouse messages away from
	 * the setup dialog and from the title bar, and RIDEV_INPUTSINK would
	 * deliver clicks meant for other windows. A failure leaves the mouse
	 * inert and the game otherwise as it is. */
	{
		RAWINPUTDEVICE rid;

		rid.usUsagePage = 1;
		rid.usUsage = 2;
		rid.dwFlags = 0;
		rid.hwndTarget = App.hWnd;

		if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
			Log("Unable To Register The Mouse For Raw Input (%lu)", GetLastError());
	}

	return 1;
}

void WinSetStyle(bool fullscreen, ulong& set)
{
	ulong style;

	style = GetWindowLong(App.hWnd, GWL_STYLE);

	if (fullscreen)
		style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
	else
		style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;

	style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX | WS_SYSMENU);
	SetWindowLong(App.hWnd, GWL_STYLE, style);

	if (set)
		set = style;
}

// One MODLOG line. Log() takes a format, so the note goes through %s
// rather than as the format itself - a mod folder called "100%" would
// otherwise be read as a conversion.
static void ModsLogLine(const char* line)
{
	Log("%s", line);
}

int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nShowCmd)
{
	DXDISPLAYMODE* dm;
	HWND desktop;
	HDC hdc;
	DEVMODE devmode;

	// Before everything else: the run check and the gameflow load below
	// both log, and the regular command line parser only runs after them.
	// Reading the flag here is what makes "start with -log, reproduce, send
	// log.txt" cover the startup as well.
	gLogEnabled = WinCheckLogFlag(lpCmdLine);

	// The build date identifies the executable in a log somebody sends in.
	// The seed and the curation stamp belong here too, but the engine does
	// not know them yet - they arrive with the level data, so the seams
	// that need them have to print them themselves.
	if (gLogEnabled)
		Log("Logging enabled by -log. PCtomb5 built " __DATE__ " " __TIME__);

	/* Where the mods folder's notes go: hd_mods.cpp is engine-free - the
	 * diagnostic tools link the very same file - so it does not know Log()
	 * and keeps its notes until somebody says where to put them. Installed
	 * here, before anything can scan, so that every MODLOG line lands in
	 * the log in the order it was made. */
	HDModsLogSink(ModsLogLine);

	// The wardrobe scan keeps its notes the same way and for the same
	// reason. One sink, because both land in the same log.
	HDWardrobeLogSink(ModsLogLine);

	start_setup = 0;
	App.mmx = 0;
	App.SetupComplete = 0;
	App.AutoTarget = 0;

	if (WinRunCheck((char*)"Tomb Raider Chronicles", (char*)"MainGameWindow", &App.mutex))
		return 0;

	LoadGameflow();
	WinProcessCommandLine(lpCmdLine);

	if (!WinRegisterWindow(hInstance))
	{
		Log("Unable To Register Window Class");
		return 0;
	}

	if (!WinCreateWindow())
	{
		Log("Unable To Create Window");
		return 0;
	}

	ShowWindow(App.hWnd, SW_HIDE);
	UpdateWindow(App.hWnd);

	DXGetInfo(&App.DXInfo, App.hWnd);

	if (start_setup || !LoadSettings())
	{
		if (!SetupDialog())
		{
			free(gfScriptFile);
			free(gfLanguageFile);
			WinClose();
			return 0;
		}

		LoadSettings();
	}

	// Apply the saved language choice: LoadGameflow ran BEFORE LoadSettings
	// and, like the original, took the first loadable language file - only
	// now has the registry been read ("Language", -1 = automatic).
	if (LanguageChoice >= 0 && LanguageChoice != (long)Gameflow->Language)
		SwitchLanguage(LanguageChoice);

	if (!fmvs_disabled)
	{
		if (!LoadBinkStuff())
		{
			MessageBox(0, "Failed to load Bink, disabling FMVs.", "Tomb Raider V", 0);
			fmvs_disabled = 1;
		}
	}

	SetWindowPos(App.hWnd, 0, App.dx.rScreen.left, App.dx.rScreen.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
	desktop = GetDesktopWindow();
	hdc = GetDC(desktop);
	App.Desktopbpp = GetDeviceCaps(hdc, BITSPIXEL);
	ReleaseDC(desktop, hdc);
	App.dx.WaitAtBeginScene = 0;
	App.dx.InScene = 0;
	App.fmv = 0;
	dm = &G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D].DisplayModes[G_dxinfo->nDisplayMode];

	/* The present interval is a device-creation parameter, so the VSync
	 * option has to be known one line before the device is made - and
	 * init_tomb5_stuff, which reads all the others, runs on the game thread
	 * that is only started at the bottom of this function. */
	load_tomb5_present_settings();

	if (!DXCreate(dm->w, dm->h, dm->bpp, App.StartFlags, &App.dx, App.hWnd, WS_OVERLAPPEDWINDOW))
	{
		MessageBox(0, SCRIPT_TEXT(TXT_Failed_To_Setup_DirectX), "Tomb Raider", 0);
		return 0;
	}

	WinSetStyle(G_dxptr->Flags & DXF_FULLSCREEN, G_dxptr->WindowStyle);

	UpdateWindow(App.hWnd);
	ShowWindow(App.hWnd, nShowCmd);

	if (App.dx.Flags & DXF_FULLSCREEN)
	{
		SetCursor(0);
		ShowCursor(0);
	}

	DXInitInput(App.hWnd, App.hInstance);
	App.hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR));

	if (!App.SoundDisabled)
	{
		DXDSCreate();
		ACMInit();
	}

	MainThread.active = 1;
	MainThread.ended = 0;
	MainThread.handle = _beginthreadex(0, 0, GameMain, 0, 0, (unsigned int*)&MainThread.address);
	WinProcMsg();
	MainThread.ended = 1;
	while (MainThread.active) {};

	WinClose();
	desktop = GetDesktopWindow();
	hdc = GetDC(desktop);
	devmode.dmSize = sizeof(DEVMODE);
	devmode.dmBitsPerPel = App.Desktopbpp;
	ReleaseDC(desktop, hdc);
	devmode.dmFields = DM_BITSPERPEL;
	ChangeDisplaySettings(&devmode, 0);
	return 0;
}
