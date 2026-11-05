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
#include "crashseam.h"		// the CRASH line and the minidump
#include "devicewindow.h"	// the style of the two display states
#include "hd/hd_mods.h"		// the mods folder's log sink
#include "hd/hd_wardrobe.h"	// and the wardrobe scan's
#include "hd/hd_level.h"		// HDFramesPresented - the hang watch's pulse
#include "zrange_math.h"		// ZFAR_UNITS - the far plane
#include "../game/spotcam.h"	// CurrentFov - kept over a new window size
#include "replay.h"		// -record and -replay
#include "high_pool.h"		// -high
#include "pool_guard.h"		// the Asan build's report
#include "profile.h"		// ProfileNoteParked - the park's length

WINAPP App;
long resChangeCounter;
// ROOMLIST's window events (game\draw.cpp), set below with -log only.
extern volatile long RoomListWindowEvents;

// The first -log line's end: a test build (the recorder's) names itself.
#ifdef TOMB5_TEST_BUILD
#define WIN_BUILD_KIND	" test build"
#else
#define WIN_BUILD_KIND	""
#endif

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

#ifdef TOMB5_TEST_BUILD

// -record <file> and -replay <file> (specific\replay.cpp): the switch as a
// whole word behind '-' or '/', then the file, in quotes when it holds a
// blank. Read by hand like -log and before WinProcessCommandLine, which
// writes the command line over in capitals. 1 with the file in `out`.
static bool WinCheckPathFlag(LPSTR cmd, const char* word, char* out,
	size_t size)
{
	const size_t n = strlen(word);
	size_t len, i, j;

	out[0] = 0;

	if (!cmd)
		return 0;

	len = strlen(cmd);

	for (i = 1; i + n <= len; i++)
	{
		if (_strnicmp(cmd + i, word, n) || (cmd[i - 1] != '-' &&
			cmd[i - 1] != '/') || (i > 1 && cmd[i - 2] != ' '))
			continue;

		if (i + n < len && cmd[i + n] != ' ')
			continue;

		for (j = i + n; j < len && cmd[j] == ' '; j++);

		const char stop = cmd[j] == '"' ? '"' : ' ';
		size_t k = 0;

		if (stop == '"')
			j++;

		for (; j < len && cmd[j] != stop && k + 1 < size; j++)
			out[k++] = cmd[j];

		out[k] = 0;
		return k != 0;
	}

	return 0;
}

#endif	// TOMB5_TEST_BUILD

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

// The windowed state's helpers, below beside the window procedure.
static void WinRenderSize();
static void WinStorePlace();
static void WinMouseTake();		// the mouse is the game's again
static void WinHangWatchStart();	// the hang seam's watch (the hang block)
static void WinHangCapture(const char* when);
static void WinStepWindow(int dir, const char* key);

void WinProcessCommands(long cmd)
{
	if (cmd == KA_ALTENTER)
	{
		/* A thread parked at the start of its scene (WM_ACTIVATE) never ends
		 * the scene the hold below waits for: the key waits for the wake. */
		if (App.fmv || !G_dxinfo->DDInfo[G_dxinfo->nDD].bCanRenderWindowed ||
			LevelLoadingThread.active || App.dx.WaitAtBeginScene)
			return;

		Log("KA_ALTENTER");
		WinMouseTake();

		// The window's place as it is now, for the way back.
		WinStorePlace();

		Log("HangGameThread");
		while (App.dx.InScene) {};
		App.dx.WaitAtBeginScene = 1;
		while (!App.dx.InScene) {};
		SuspendThread(MainThread.handle);
		Log("Game Thread Suspended");

		DXToggleFullScreen();
		WinRenderSize();
		ResumeThread(MainThread.handle);
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
		const char* key = (cmd == KA_ALTP) ? "plus" : "minus";

		if (LevelLoadingThread.active || App.fmv)
			return;

		Log("Change Video Mode");

		/* Full screen is the monitor at its desktop's size (DXCreate), so
		 * a step has nothing to change there and changes nothing: no new
		 * device, only the size caption, which shows the size it is.
		 *   DXLOG window keys <plus|minus> fullscreen kept=<WxH> */
		if (App.dx.Flags & DXF_FULLSCREEN)
			Log("DXLOG window keys %s fullscreen kept=%lux%lu", key,
				App.dx.dwRenderWidth, App.dx.dwRenderHeight);
		else
			WinStepWindow(cmd == KA_ALTP ? 1 : -1, key);

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
			cmd[i] = (CHAR)toupper(cmd[i]);
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
				l = (ulong)strlen(pCommand);

				for (int j = 0; (ulong)j < l; j++, pCommand++)
				{
					if (*pCommand != '=')
						continue;

					p = pCommand + 1;
					l = (ulong)strlen(p);

					for (j = 0; (ulong)j < l; j++, p++)
					{
						if (*p != ' ')
							break;
					}

					last = p;
					l = (ulong)strlen(last);

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
 * the first; a window held by its caption or frame, or activated by a click
 * outside the picture, lets the cursor go until a click inside takes it back.
 * ShowCursor and ClipCursor are only ever called on this thread,
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
static long size_moving;		// between WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE
static long mouse_free;		// let go at the frame; a click inside takes it

// Alt+Enter is play: the cursor is the game's again.
static void WinMouseTake()
{
	mouse_free = 0;
}

/* Seats or frees the cursor to match the rule, on this thread. Called on every
 * message that can change a term, and on the game thread's WM_APP_MOUSE.
 *
 * ShowCursor keeps a counter, and the cursor shows while it is 0 or more. The
 * capture lowers it until it is below 0 and counts how many calls that took;
 * the release raises it by exactly that many, so it ends where it was found -
 * a full screen that had it at -1 stays hidden. The clip is set again on every
 * call while captured, because a move or a resize changes the client area.
 * While the player moves or sizes the window by its caption or frame the clip
 * is off: one that followed the client would push the cursor into it, and
 * Windows would move the window after the cursor. */
static void WinMouseCapture()
{
	const LONG want = mouse_want;
	const long modern = (want & MOUSE_WANT_MODERN) ? 1 : 0;
	const long fmv = (want & MOUSE_WANT_FMV) ? 1 : 0;
	const long on = (mouse_active && modern && !fmv && !mouse_closing &&
		!size_moving && !mouse_free && App.hWnd) ? 1 : 0;

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

	if (LogFam_MOUSELOG && gLogEnabled)
		Log("MOUSELOG capture=%s active=%ld modern=%ld fmv=%ld free=%ld",
			on ? "on" : "off", mouse_active, modern, fmv, mouse_free);
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

	// Let go: neither a button nor a move counts until a click in the
	// picture takes the mouse back (the click itself is not a shot).
	if (mouse_free)
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

/* The windowed state's messages (WinMainWndProc).
 *
 * The window is an ordinary overlapped window (devicewindow.h); the
 * engine's part is the back buffer, which takes the client's size when a
 * drag of the frame ends (WM_EXITSIZEMOVE) and at once on a WM_SIZE outside
 * a drag - maximize, restore, snap. During a drag the last picture
 * stretches into the client; a minimized window resizes nothing. A new
 * size needs the game thread held at the start of its scene, as Alt+Enter
 * holds it. Where it cannot be held - the game thread not set up yet, or
 * parked while the window is inactive, a film or a level load running, or
 * no start of a scene within two seconds - the size waits, and a timer
 * tries again four times a second. */
#define WIN_RESIZE_TIMER	1

static long game_parked;		// WM_ACTIVATE has the game thread suspended

/* The hang seam's watch. HangGameThread found the game thread away from
 * its frame wait for two seconds. A slow machine's HD build keeps it away
 * for longer, so the watch, a thread of its own, gives it thirty seconds
 * more and asks once a second: a frame presented since, the thread parked
 * after all, the window closing, or a level loading (sixty seconds then)
 * end the watch; a thread that shows none of that is stuck, and the seam
 * (crashseam.h) reads where it stands and writes the dump. Once per
 * process: a second Alt+Tab adds nothing, and the close path captures
 * only what the watch did not. */
static volatile long hang_watching;
static volatile long hang_captured;

static void WinHangCapture(const char* when)
{
	if (InterlockedExchange(&hang_captured, 1))
		return;

	CrashSeamHang(MainThread.handle, (DWORD)MainThread.address, when);
}

static unsigned int __stdcall WinHangWatch(void*)
{
	const long frames = HDFramesPresented();
	const DWORD since = GetTickCount();

	for (;;)
	{
		Sleep(1000);

		const DWORD waited = GetTickCount() - since;

		if (hang_captured || game_parked || MainThread.ended ||
			!MainThread.active)
			break;

		if (HDFramesPresented() != frames)
		{
			Log("HANG watch: the game thread went on after %lu ms",
				(unsigned long)waited);
			break;
		}

		if (waited >= (g_level_loading ? 60000UL : 30000UL))
		{
			WinHangCapture("focus");
			break;
		}
	}

	hang_watching = 0;
	return 0;
}

static void WinHangWatchStart()
{
	if (InterlockedExchange(&hang_watching, 1))
		return;

	const uintptr_t h = _beginthreadex(0, 0, WinHangWatch, 0, 0, 0);

	if (h)
		CloseHandle((HANDLE)h);
	else
		hang_watching = 0;
}
static long resize_waiting;		// a new size waits for WIN_RESIZE_TIMER

// The game thread held at the start of its scene, two seconds at most.
static bool WinHoldGame()
{
	DWORD start = GetTickCount();

	Log("HangGameThread");

	while (App.dx.InScene)
	{
		if (GetTickCount() - start > 2000)
			return false;
	}

	App.dx.WaitAtBeginScene = 1;
	start = GetTickCount();

	while (!App.dx.InScene)
	{
		if (GetTickCount() - start > 2000)
		{
			App.dx.WaitAtBeginScene = 0;
			return false;
		}
	}

	SuspendThread(MainThread.handle);
	Log("Game Thread Suspended");
	return true;
}

static void WinFreeGame()
{
	ResumeThread(MainThread.handle);
	App.dx.WaitAtBeginScene = 0;
	Log("Game Thread Resumed");
}

/* The park, its wake, and a lost device given back.
 *
 * WM_ACTIVATE parks the game thread when the window loses the activation:
 * held at the start of its scene and suspended, as WinHoldGame holds it,
 * so that a game in the background runs no logic and draws nothing. The
 * park has one partner, WinWakeGame, and every road back leads to it:
 * WA_ACTIVE and WA_CLICKACTIVE; WM_SETFOCUS and WM_ACTIVATEAPP with the
 * window in front again; WM_CLOSE; and WIN_PARK_TIMER, which asks four
 * times a second while the thread is parked whether the window is the
 * foreground window and not minimised. The timer is the road back that
 * needs no message at all: the dump of a game frozen behind Ctrl+Alt+Del
 * shows the park on WA_INACTIVE and, in the ten seconds after it, no
 * WM_ACTIVATE and no WM_SETFOCUS, only a mouse move over the window.
 *
 * A lost device (dxshell.cpp, DXShowFrame) is given back by this thread,
 * which made it. WM_APP_DEVICE starts WIN_DEVICE_TIMER, and four times a
 * second WinDeviceRestore asks whether the player is in the window again -
 * the thread not parked, the window in front and not minimised, no film,
 * no level loading - and whether the device can be given back
 * (TestCooperativeLevel); then the game thread is held, the device made
 * anew (DXRestoreDevice) and the thread let go. The first tick comes a
 * quarter of a second after the loss, by when the secure desktop's
 * WA_INACTIVE has parked the thread.
 *   WINLOG activate state=<inactive|active|click> minimized=<0|1>
 *     front=<ours|other|none> parked=<0|1> fmv=<0|1> closing=<0|1>
 *     device=<ok|lost> - every WM_ACTIVATE
 *   WINLOG activateapp active=<0|1> front= parked= - every WM_ACTIVATEAPP
 *   WINLOG focus <set|kill> front= parked= - every WM_SETFOCUS, WM_KILLFOCUS
 *   WINLOG park result=<parked|stuck|already|early> - every WA_INACTIVE
 *   WINLOG wake by=<activate|focus|activateapp|front|close> parked_ms=<n>
 *     device=<ok|lost>
 *   WINLOG front <ours|other|none> parked_ms=<n> - while parked, each time
 *     another window, or none, is in front
 *   WINLOG device waits why=<setup|parked|front|film|loading|lost|hold>
 *     coop=<ok|lost|notreset|internal|other> - once per reason
 *   WINLOG device restore coop=<...> ok=<0|1> - before the new device's
 *     render states; ok=0 is a device that could not be made */
#define WIN_PARK_TIMER		2
#define WIN_DEVICE_TIMER	3

static DWORD park_since;		// GetTickCount at the park
static long park_front = -1;		// the window in front the timer saw last
static const char* device_waits;	// what the restore last waited for

static const char* const kWinFront[3] = { "ours", "other", "none" };

// Whose window is in front: 0 ours, 1 another's, 2 none - another desktop
// has the screen, or no window is active.
static long WinFront()
{
	const HWND front = GetForegroundWindow();

	return !front ? 2 : front == App.hWnd ? 0 : 1;
}

// The park's one partner: the thread resumed, the timer gone. True when a
// parked thread was woken.
static bool WinWakeGame(const char* by)
{
	if (!game_parked)
		return false;

	KillTimer(App.hWnd, WIN_PARK_TIMER);
	// The pause line's `parked=`, handed over while the thread still sleeps.
	ProfileNoteParked(GetTickCount() - park_since);
	ResumeThread(MainThread.handle);
	App.dx.WaitAtBeginScene = 0;
	game_parked = 0;
	Log("WINLOG wake by=%s parked_ms=%lu device=%s", by,
		(unsigned long)(GetTickCount() - park_since),
		DXDeviceLost ? "lost" : "ok");
	return true;
}

// A road back that brings no WA_ACTIVE: the window in front and not
// minimised is the player back in it - the test WM_SIZE takes for the
// mouse's term. True when a parked thread was woken.
static bool WinWakeIfFront(const char* by)
{
	if (!game_parked || WinFront() != 0 || IsIconic(App.hWnd) ||
		!WinWakeGame(by))
		return false;

	Log("Game Thread Resumed");
	return true;
}

static void WinDeviceRestore()
{
	const char* why = 0;

	if (!DXDeviceLost || mouse_closing)
	{
		KillTimer(App.hWnd, WIN_DEVICE_TIMER);
		device_waits = 0;
		return;
	}

	const HRESULT coop = DXDeviceCoop();

	// A new size or Alt+Enter made a new device meanwhile.
	if (coop == D3D_OK)
	{
		InterlockedExchange(&DXDeviceLost, 0);
		KillTimer(App.hWnd, WIN_DEVICE_TIMER);
		device_waits = 0;
		Log("WINLOG device restore coop=ok ok=1");
		return;
	}

	if (!App.SetupComplete)
		why = "setup";
	else if (game_parked)
		why = "parked";
	else if (WinFront() != 0 || IsIconic(App.hWnd))
		why = "front";		// another window, another desktop, minimised
	else if (App.fmv)
		why = "film";
	else if (LevelLoadingThread.active || g_level_loading)
		why = "loading";
	else if (coop == D3DERR_DEVICELOST)
		why = "lost";		// the device cannot be given back yet
	else if (!WinHoldGame())
		why = "hold";

	if (why)
	{
		if (!device_waits || strcmp(device_waits, why))
			Log("WINLOG device waits why=%s coop=%s", why, DXCoopName(coop));

		device_waits = why;
		SetTimer(App.hWnd, WIN_DEVICE_TIMER, 250, 0);
		return;
	}

	KillTimer(App.hWnd, WIN_DEVICE_TIMER);
	device_waits = 0;

	const long made = DXRestoreDevice();

	Log("WINLOG device restore coop=%s ok=%ld", DXCoopName(coop), made);
	WinRenderSize();
	WinFreeGame();
	WinMouseCapture();
}

// The WINLOG activate line (the block above).
static void WinLogActivate(WPARAM wParam, bool closing)
{
	if (!gLogEnabled)
		return;

	const long state = (long)(wParam & 0xFFFF);

	Log("WINLOG activate state=%s minimized=%ld front=%s parked=%ld fmv=%ld "
		"closing=%ld device=%s", state == WA_INACTIVE ? "inactive" :
		state == WA_CLICKACTIVE ? "click" : "active",
		HIWORD(wParam) ? 1L : 0L, kWinFront[WinFront()], game_parked,
		App.fmv ? 1L : 0L, closing ? 1L : 0L, DXDeviceLost ? "lost" : "ok");
}

/* The projection and the 2D layout for the back buffer's size, after every
 * new device of Alt+Enter, a key or a resize: the render states, the
 * projection's window (InitWindow), whose field of view is worked out from
 * the client's height, with the field of view that was set kept, the font's
 * scale and the matrices. The game thread is held. */
static void WinRenderSize()
{
	const short fov = CurrentFov;

	HWInitialise();
	InitWindow(0, 0, App.dx.dwRenderWidth, App.dx.dwRenderHeight,
		ZNEAR_UNITS, ZFAR_UNITS, 80, App.dx.dwRenderWidth,
		App.dx.dwRenderHeight);

	if (fov)
		AlterFOV(fov);

	InitFont();
	S_InitD3DMatrix();
	aSetViewMatrix();
}

/* The window's place remembered (DXWindowPlace) and written to the registry
 * (REG_WriteWindowPlace): when a drag or a resize ends, on maximize and
 * restore, before Alt+Enter leaves the windowed state and when the window
 * is closed. A maximized window keeps the client it had before, which is
 * where a restore puts it back.
 *   DXLOG window stored client=<l,t,r,b in screen coordinates>
 *   maximized=<0|1> */
static void WinStorePlace()
{
	if (!App.dx.lpD3DDevice || DXWindowBusy ||
		(App.dx.Flags & DXF_FULLSCREEN) || IsIconic(App.hWnd))
		return;

	const long zoomed = IsZoomed(App.hWnd) ? 1 : 0;

	if (!zoomed)
		DXWindowPlace.client = DeviceWindowClient(App.hWnd);

	DXWindowPlace.maximized = zoomed;
	DXWindowPlace.have = 1;
	REG_WriteWindowPlace(DXWindowPlace.client.left, DXWindowPlace.client.top,
		WPWidth(DXWindowPlace.client), WPHeight(DXWindowPlace.client), zoomed);
	Log("DXLOG window stored client=%ld,%ld,%ld,%ld maximized=%ld",
		DXWindowPlace.client.left, DXWindowPlace.client.top,
		DXWindowPlace.client.right, DXWindowPlace.client.bottom, zoomed);
}

/* A new size that has to wait (see above).
 *   DXLOG window resize <why> waits fmv=<0|1> loading=<0|1> parked=<0|1>
 *   ready=<0|1> - once, when the waiting starts */
static void WinResizeLater(const char* why)
{
	if (!resize_waiting)
		Log("DXLOG window resize %s waits fmv=%ld loading=%ld parked=%ld "
			"ready=%ld", why, (long)(App.fmv != 0),
			(long)(LevelLoadingThread.active != 0), game_parked,
			(long)(App.SetupComplete != 0));

	resize_waiting = 1;
	SetTimer(App.hWnd, WIN_RESIZE_TIMER, 250, 0);
}

/* The back buffer to the client's size when the two differ (rule above):
 * the game thread held, the device made anew (DXResizeToClient), the
 * projection and the layout for the new size, the mouse's clip on the new
 * client. A key's step logs the window line of a resolution step after
 * it. */
static void WinClientChanged(const char* why)
{
	if (!App.dx.lpD3DDevice || DXWindowBusy ||
		(App.dx.Flags & DXF_FULLSCREEN) || IsIconic(App.hWnd))
		return;

	const WPRect now = DeviceWindowClient(App.hWnd);

	if (WPWidth(now) <= 0 || WPHeight(now) <= 0)
		return;

	if ((ulong)WPWidth(now) == App.dx.dwRenderWidth &&
		(ulong)WPHeight(now) == App.dx.dwRenderHeight)
	{
		if (resize_waiting)
			KillTimer(App.hWnd, WIN_RESIZE_TIMER);

		resize_waiting = 0;
		return;
	}

	if (!App.SetupComplete || game_parked || App.fmv ||
		LevelLoadingThread.active || !WinHoldGame())
	{
		WinResizeLater(why);
		return;
	}

	if (resize_waiting)
		KillTimer(App.hWnd, WIN_RESIZE_TIMER);

	resize_waiting = 0;
	DXResizeToClient(why);
	WinRenderSize();
	WinFreeGame();
	WinMouseCapture();

	if (!strcmp(why, "keys"))
		DXLogWindow("mode");
}

/* Alt+Plus and Alt+Minus in the windowed state: the client steps from its
 * own size to the next larger or smaller entry of the mode list
 * (WinPlaceStep), the largest it may take being what the work area of the
 * window's monitor holds; the window, restored first when maximized, keeps
 * its centre and stays inside that work area (WinPlaceAround). The entry
 * becomes the resolution the setup stores.
 *   DXLOG window keys <plus|minus> mode=<entry> <WxH> client=<WxH>;
 *   mode=-1 <WxH> for the largest client the work area holds, past the
 *   last entry that fits; mode=none client=<WxH> when there is no step */
static void WinStepWindow(int dir, const char* key)
{
	DXD3DDEVICE* device =
		&G_dxinfo->DDInfo[G_dxinfo->nDD].D3DDevices[G_dxinfo->nD3D];
	const long count = device->nDisplayModes;
	WPRect work;
	RECT viewport, screen;
	long w, h;

	if (IsIconic(App.hWnd) || count <= 0 ||
		!DeviceWindowWorkOf(MonitorFromWindow(App.hWnd,
		MONITOR_DEFAULTTONEAREST), &work))
		return;

	const WPRect frame = DeviceWindowFrame(
		(DWORD)GetWindowLong(App.hWnd, GWL_STYLE));
	const WPRect now = DeviceWindowClient(App.hWnd);
	WPMode* modes = (WPMode*)malloc(count * sizeof(WPMode));

	if (!modes)
		return;

	for (long i = 0; i < count; i++)
	{
		modes[i].w = device->DisplayModes[i].w;
		modes[i].h = device->DisplayModes[i].h;
		modes[i].bpp = device->DisplayModes[i].bpp;
	}

	const long maxW = WPWidth(work) - WPWidth(frame);
	const long maxH = WPHeight(work) - WPHeight(frame);
	const long next = WinPlaceStep(modes, count, App.DXInfo.nDisplayMode,
		WPWidth(now), WPHeight(now), maxW, maxH, dir);

	if (next >= 0)
	{
		WinPlaceFit(modes[next].w, modes[next].h, frame, work, &w, &h);
		App.DXInfo.nDisplayMode = next;
	}
	else if (dir > 0 && (WPWidth(now) < maxW || WPHeight(now) < maxH))
	{
		// Past the last entry that fits lies the largest client the work
		// area holds, as Maximize gives it; the list's entry stays.
		w = maxW;
		h = maxH;
	}
	else
	{
		Log("DXLOG window keys %s mode=none client=%ldx%ld", key,
			WPWidth(now), WPHeight(now));
		free(modes);
		return;
	}
	InterlockedIncrement(&DXWindowBusy);

	if (IsZoomed(App.hWnd))
		ShowWindow(App.hWnd, SW_RESTORE);

	DeviceWindowPlaceClient(App.hWnd, WinPlaceAround(now, w, h, frame, work),
		&viewport, &screen);
	InterlockedDecrement(&DXWindowBusy);
	Log("DXLOG window keys %s mode=%ld %ldx%ld client=%ldx%ld", key, next,
		next >= 0 ? modes[next].w : w, next >= 0 ? modes[next].h : h, w, h);
	free(modes);
	WinClientChanged("keys");
	WinStorePlace();
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

		// Maximize, restore and snap take the new size at once; a drag
		// waits for its end, a minimize changes nothing.
		if (wParam != SIZE_MINIMIZED && !size_moving && !DXWindowBusy)
		{
			WinClientChanged("size");
			WinStorePlace();
		}

		break;

	case WM_ENTERSIZEMOVE:
		size_moving = 1;
		WinMouseCapture();	// lets the cursor go for the drag
		break;

	case WM_EXITSIZEMOVE:
		size_moving = 0;
		WinClientChanged("drag");
		WinStorePlace();
		mouse_free = 1;		// the player has the window; a click inside takes it
		WinMouseCapture();
		break;

	case WM_TIMER:

		if (wParam == WIN_RESIZE_TIMER)
		{
			KillTimer(hwnd, WIN_RESIZE_TIMER);

			/* The wait stays marked across the retry: WinResizeLater logs its
			 * line once per wait, and WinClientChanged clears the mark when the
			 * size is taken or no longer differs. */
			if (!size_moving)
				WinClientChanged("later");

			return 0;
		}

		// The park's road back without a message (the park block).
		if (wParam == WIN_PARK_TIMER)
		{
			const long front = WinFront();

			if (!game_parked)
			{
				KillTimer(hwnd, WIN_PARK_TIMER);
				return 0;
			}

			if (front != park_front)
			{
				park_front = front;
				Log("WINLOG front %s parked_ms=%lu", kWinFront[front],
					(unsigned long)(GetTickCount() - park_since));
			}

			// No message has said so; the mouse's term follows the wake.
			if (WinWakeIfFront("front"))
				WinMouseActive(1);

			return 0;
		}

		if (wParam == WIN_DEVICE_TIMER)
		{
			WinDeviceRestore();
			return 0;
		}

		break;

	case WM_APP_DEVICE:

		// The game thread's present found the device lost (dxshell.cpp); the
		// restore is tried from the timer's first tick on (the park block).
		if (!mouse_closing)
			SetTimer(hwnd, WIN_DEVICE_TIMER, 250, 0);

		return 0;

	case WM_GETMINMAXINFO:

		// The windowed state's smallest client (windowplace.h); full
		// screen has no frame to drag.
		if (!(App.dx.Flags & DXF_FULLSCREEN))
		{
			MINMAXINFO* mmi = (MINMAXINFO*)lParam;
			long w, h;

			WinPlaceMinWindow(DeviceWindowFrame(
				(DWORD)GetWindowLong(hwnd, GWL_STYLE)), &w, &h);
			mmi->ptMinTrackSize.x = w;
			mmi->ptMinTrackSize.y = h;
			return 0;
		}

		break;

	case WM_SYSCOMMAND:

		/* SC_KEYMENU is the keyboard's way into the system menu: Alt alone,
		 * F10, Alt+Space. Alt is one of the game's keys, and with a system
		 * menu on the window a tap of it would put the window into menu
		 * mode and hand the next keys to that menu; so Alt alone and F10
		 * (lParam 0) are swallowed. Alt+Space (lParam ' ') opens the menu,
		 * as the mouse does at the caption; Alt+F4 is SC_CLOSE. */
		if ((wParam & 0xFFF0) == SC_KEYMENU && lParam != ' ')
			return 0;

		break;

	case WM_LBUTTONDOWN:
	case WM_RBUTTONDOWN:
	case WM_MBUTTONDOWN:

		// A click in the picture takes the mouse back after it was let go.
		if (mouse_free)
		{
			mouse_free = 0;
			WinMouseCapture();
			return 0;
		}

		break;

	case WM_INPUT:
		WinMouseInput((HRAWINPUT)lParam);
		break;			// DefWindowProc still has to see it, to free the input

	case WM_APP_MOUSE:
		WinMouseCapture();
		return 0;

	case WM_SETFOCUS:
		WinMouseActive(IsIconic(hwnd) ? 0 : 1);
		Log("WINLOG focus set front=%s parked=%ld", kWinFront[WinFront()],
			game_parked);
		WinWakeIfFront("focus");
		break;

	case WM_KILLFOCUS:
		WinMouseActive(0);
		Log("WINLOG focus kill front=%s parked=%ld", kWinFront[WinFront()],
			game_parked);
		break;

	case WM_ACTIVATEAPP:
		Log("WINLOG activateapp active=%ld front=%s parked=%ld",
			wParam ? 1L : 0L, kWinFront[WinFront()], game_parked);

		if (wParam)
			WinWakeIfFront("activateapp");

		break;

	case WM_ACTIVATE:

		/* Before everything else in this case: the FMV guard below returns
		 * early, and a window that goes inactive during a video must still
		 * give up its buttons. */
		/* A click that activates the window outside the picture - the
		 * caption, the frame, a button - lets the cursor go; a click inside,
		 * or a return by the keyboard, takes it. */
		if ((wParam & 0xFFFF) == WA_CLICKACTIVE)
		{
			POINT pt;

			GetCursorPos(&pt);
			mouse_free = (SendMessage(hwnd, WM_NCHITTEST, 0,
				MAKELPARAM(pt.x, pt.y)) != HTCLIENT) ? 1 : 0;
		}
		else if ((wParam & 0xFFFF) == WA_ACTIVE)
			mouse_free = 0;

		WinMouseActive((wParam & 0xFFFF) != WA_INACTIVE && !HIWORD(wParam));
		WinLogActivate(wParam, closing);

		if (!closing)
		{
			/* The wake comes before the film's guard: a thread the park held
			 * at the first frame of a film that began during its wait would
			 * otherwise wait for a WA_ACTIVE that the guard swallows. */
			if (App.fmv && (wParam & 0xFFFF) != WA_INACTIVE &&
				WinWakeGame("activate"))
				Log("Game Thread Resumed");

			if (App.fmv)
				return 0;

			switch (wParam & 0xFFFF)
			{
			case WA_INACTIVE:
				Log("WM_INACTIVE");

				// ROOMLIST writes the whole room list on the next frame.
				if (gLogEnabled)
					InterlockedOr(&RoomListWindowEvents, 1);

				/* Parked already, a second WA_INACTIVE without a WA_ACTIVE
				 * between them: the thread stands at the start of its scene,
				 * and the hold below would wait two seconds for an end of the
				 * scene that cannot come and then call the thread stuck. */
				if (game_parked)
					Log("WINLOG park result=already");
				else if (App.SetupComplete)
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
						SuspendThread(MainThread.handle);
						game_parked = 1;
						park_since = GetTickCount();
						park_front = -1;
						SetTimer(hwnd, WIN_PARK_TIMER, 250, 0);
						Log("Game Thread Suspended");
						Log("WINLOG park result=parked");
					}
					else
					{
						App.dx.WaitAtBeginScene = 0;
						Log("Game Thread appeared stuck, skipping suspend to avoid a deadlock");
						Log("WINLOG park result=stuck");
						WinHangWatchStart();
					}
				}
				else
					Log("WINLOG park result=early");

				return 0;

			case WA_ACTIVE:
			case WA_CLICKACTIVE:
				Log("WM_ACTIVE");

				// ROOMLIST writes the whole room list on the next frame.
				if (gLogEnabled)
					InterlockedOr(&RoomListWindowEvents, 2);

				if (WinWakeGame("activate"))
					Log("Game Thread Resumed");

				return 0;
			}
		}

		break;

	case WM_CLOSE:
		closing = 1;
		WinStorePlace();
		WinMouseRelease();
		KillTimer(hwnd, WIN_DEVICE_TIMER);	// no new device for a closing game

		/* A window closed from the taskbar is inactive, and an inactive
		 * window has the game thread parked (WM_ACTIVATE). It is woken here
		 * so that it sees the end and leaves; otherwise the wait at the end
		 * of WinMain never ends, and the music plays on in a process that
		 * only the task manager can close. */
		if (WinWakeGame("close"))
			Log("Game Thread Resumed for the close");

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
	ReplayClose();		// -record's file closed, -replay's end said
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
		Log("Released %s @ %x - RefCnt = %d", "Keyboard", LogAddr(G_dxptr->Keyboard), G_dxptr->Keyboard->Release());
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
		Log("Released %s @ %x - RefCnt = %d", "DirectInput", LogAddr(G_dxptr->lpDirectInput), G_dxptr->lpDirectInput->Release());
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
	const ulong style = DeviceWindowStyle(App.hWnd, fullscreen);

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

// The crash seam's writer (crashseam.h): its line into the -log file.
static void CrashLogLine(const char* line)
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
	// A test build (TOMB5_TEST_BUILD, the recorder's) says so after the
	// time; the replay file's stamp below stays without it.
	if (gLogEnabled)
		Log("Logging enabled by -log. PCtomb5 built " __DATE__ " " __TIME__
			WIN_BUILD_KIND);

#ifdef TOMB5_TEST_BUILD
	/* -record <file> or -replay <file>: the same inputs played again
	 * (specific\replay.cpp). Opened before anything reads a device; the
	 * file's header carries this build's stamp. -liveclock, read as -high
	 * is, blends a replay's frames by this machine's clock; without -replay
	 * it changes nothing. */
	{
		char replayPath[MAX_PATH];
		const bool liveClock = SwitchWordGiven(lpCmdLine, "liveclock");

		if (WinCheckPathFlag(lpCmdLine, "record", replayPath,
			sizeof(replayPath)))
			ReplayOpen(REPLAY_RECORD, replayPath,
				"PCtomb5 built " __DATE__ " " __TIME__, liveClock);
		else if (WinCheckPathFlag(lpCmdLine, "replay", replayPath,
			sizeof(replayPath)))
			ReplayOpen(REPLAY_PLAY, replayPath,
				"PCtomb5 built " __DATE__ " " __TIME__, liveClock);
	}

	/* -rngtrace <from> <to>, read as -high is, two tick numbers after it:
	 * the logic generator's callers in that window of ticks, with -log
	 * (function_stubs.h). Refused with one line otherwise. */
	{
		long rtFrom, rtTo;
		const int rt = RngTraceSwitch(lpCmdLine, &rtFrom, &rtTo);

		if (rt > 0 && RngTraceArm(rtFrom, rtTo))
			Log("RNGTRACE window from=%ld to=%ld", rtFrom, rtTo);
		else if (rt > 0)
			Log("RNGTRACE refused: no memory for the window");
		else if (rt < 0)
			Log("RNGTRACE refused: -rngtrace takes two tick numbers, "
				"from <= to, 0..65535");
	}
#endif	// TOMB5_TEST_BUILD: a shipping EXE does not know the four words

	/* The crash seam, as early as the log: an exception nobody catches
	 * writes its CRASH line into the log (with -log) or into crash.txt
	 * (without) and a minidump beside it - crashseam.h. This thread is the
	 * window's, "main"; the game thread is named where it starts. */
	CrashSeamInstall(CrashLogLine, !gLogEnabled, "PCtomb5");
	AsanSeamInstall(CrashLogLine);	// the Asan build only (pool_guard.h)

#ifdef TOMB5_TEST_BUILD
	/* -high (high_pool.h): every free region below 4 GB reserved before the
	 * pool and the first level are allocated. A test build's only (#338),
	 * with its HIGHLOG lines: a shipping EXE does not know the word. */
	if (HighSwitchGiven(lpCmdLine))
	{
		HighReserve high;

		if (HighReserveBelow4G(&high))
			Log("HIGHLOG walk reserved=%ld bytes=%llu held=%llu left=%llu "
				"failed=%ld", high.regions, high.bytes, high.held, high.left,
				high.failed);
		else
			Log("HIGHLOG refused: a 32-bit build has nothing above 4 GB");
	}
#endif	// TOMB5_TEST_BUILD: nor -high

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
			LogClose();
			return 0;
		}

		LoadSettings();
	}

	// Apply the saved language choice: LoadGameflow ran BEFORE LoadSettings
	// and, like the original, took the first loadable language file - only
	// now has the registry been read ("Language", -1 = automatic).
	if (LanguageChoice >= 0 && LanguageChoice != (long)Gameflow->Language)
		SwitchLanguage(LanguageChoice);

	// Where the window was left, for the windowed state (DXCreate).
	{
		long left, top, w, h, maximized;

		if (REG_ReadWindowPlace(&left, &top, &w, &h, &maximized))
		{
			DXWindowPlace.client = WPMake(left, top, w, h);
			DXWindowPlace.maximized = maximized;
			DXWindowPlace.have = 1;
		}
	}

#ifdef _WIN64
	// The game's binkw32.dll is 32-bit code, which a 64-bit process cannot
	// load: the x64 build plays the films through three FFmpeg DLLs beside
	// the EXE (specificmv_av.cpp), loaded here. They are optional -
	// without them it plays no film and goes on at once, with an FMVLOG
	// line instead of the message box below at every start.
	if (!fmvs_disabled && !LoadBinkStuff())
		fmvs_disabled = 1;
#else
	if (!fmvs_disabled)
	{
		if (!LoadBinkStuff())
		{
			MessageBox(0, "Failed to load Bink, disabling FMVs.", "Tomb Raider V", 0);
			fmvs_disabled = 1;
		}
	}
#endif

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

	/* Shown as DXCreate left it, maximized when the remembered place was:
	 * the size is the back buffer's, so a shortcut's "run maximized" or
	 * "minimized" (nShowCmd) may not change it behind the device. */
	UpdateWindow(App.hWnd);
	InterlockedIncrement(&DXWindowBusy);
	ShowWindow(App.hWnd, SW_SHOW);
	InterlockedDecrement(&DXWindowBusy);
	DXLogWindow("start");

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
	MainThread.handle = (HANDLE)_beginthreadex(0, 0, GameMain, 0, 0, (unsigned int*)&MainThread.address);
	CrashSeamGameThread((DWORD)MainThread.address);
	WinProcMsg();
	MainThread.ended = 1;

	/* The game thread leaves its loop within a frame or two. One that
	 * does not - stuck in a driver call, or never woken - must not hold
	 * the process: after five seconds the process ends without the
	 * teardown, and Windows frees what is left. A spin here would burn a
	 * core while it waits. */
	{
		const DWORD since = GetTickCount();

		while (MainThread.active)
		{
			if (GetTickCount() - since > 5000)
			{
				Log("Game thread did not end in five seconds: "
					"the process ends");

				/* Where it stands, before the process goes: the hang seam's
				 * line and dump - unless a level is loading, which keeps the
				 * thread from its end for longer than that on purpose. */
				if (g_level_loading)
					Log("HANG watch: a level is loading, no dump");
				else
					WinHangCapture("close");

				ExitProcess(0);
			}

			Sleep(1);
		}
	}

	WinClose();
	LogClose();		// after ReplayClose's end line (function_stubs.cpp)
	desktop = GetDesktopWindow();
	hdc = GetDC(desktop);
	devmode.dmSize = sizeof(DEVMODE);
	devmode.dmBitsPerPel = App.Desktopbpp;
	ReleaseDC(desktop, hdc);
	devmode.dmFields = DM_BITSPERPEL;
	ChangeDisplaySettings(&devmode, 0);
	return 0;
}
