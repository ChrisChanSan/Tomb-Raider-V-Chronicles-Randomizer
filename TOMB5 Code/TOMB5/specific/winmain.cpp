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

LRESULT CALLBACK WinMainWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static long mouseX, mouseY, mouseB;
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
		break;

	case WM_ACTIVATE:

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
		PostQuitMessage(0);
		break;

	case WM_COMMAND:
		Log("WM_COMMAND");
		WinProcessCommands(wParam & 0xFFFF);
		break;

	case WM_MOUSEMOVE:
		mouseX = GET_X_LPARAM(lParam);
		mouseY = GET_Y_LPARAM(lParam);
		mouseB = wParam;
		break;
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void WinClose()
{
	Log(__FUNCTION__);
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
