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
#include "setupdlg.h"
#include "../../resource.h"
#include "registry.h"
#include "winmain.h"
#include "../game/gameflow.h"

bool start_setup = 0;
bool fmvs_disabled = 0;

static long nDDDevice;
static long nD3DDevice;
static bool Filter = 1;
static bool TextLow;

/* The choices the dialog opens with: what SaveSetup stored under System,
 * read without creating or writing anything (REG_PeekLong), and the
 * dialog's own default wherever nothing is stored - the primary monitor's
 * adapter, 640x480, Windowed, Bilinear on, the other three boxes off. A
 * stored adapter or resolution that no longer names an entry of the
 * lists DXGetInfo built is the default too. -1 is "not stored". */
static long stored_dd = -1;
static long stored_mode = -1;
static long stored_window = -1;
static bool NoFMV;
static bool NoSound;
static bool radios_set;

static void LoadStoredChoices()
{
	ulong v;

	stored_dd = REG_PeekLong("System", "DD", v) ? (long)v : -1;
	stored_mode = REG_PeekLong("System", "VMode", v) ? (long)v : -1;
	stored_window = REG_PeekLong("System", "Window", v) ? (v != 0) : -1;

	if (REG_PeekLong("System", "Filter", v))
		Filter = v != 0;

	if (REG_PeekLong("System", "TextLow", v))
		TextLow = v != 0;

	if (REG_PeekLong("System", "NoFMV", v))
		NoFMV = v != 0;

	if (REG_PeekLong("System", "DisableSound", v))
		NoSound = v != 0;

	if (stored_dd >= App.DXInfo.nDDInfo)
		stored_dd = -1;

	radios_set = 0;
}

static void InitResolution(HWND dlg, HWND hwnd, bool resetvms)
{
	DXD3DDEVICE* device;
	DXDISPLAYMODE* dm;
	long w, h, n;
	char buffer[40];

	n = 0;

	if (resetvms)
	{
		/* The stored resolution is an entry of the stored adapter's list,
		 * so it is taken on that adapter only; another adapter, or none
		 * stored, opens at 640x480, and a list without that at its first
		 * entry. */
		const long adapter = stored_dd >= 0 ? stored_dd : App.DXInfo.nDD;
		const long want = nDDDevice == adapter ? stored_mode : -1;
		long pick = -1, vga = -1;

		SendMessage(hwnd, CB_RESETCONTENT, 0, 0);
		device = &App.DXInfo.DDInfo[nDDDevice].D3DDevices[nD3DDevice];

		for (int i = 0; i < device->nDisplayModes; i++)
		{
			dm = &device->DisplayModes[i];
			w = dm->w;
			h = dm->h;

			wsprintf(buffer, "%dx%d", w, h);
			SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)buffer);
			SendMessage(hwnd, CB_SETITEMDATA, n, i);

			if (w == 640 && h == 480)
				vga = n;

			if (i == want)
				pick = n;

			n++;
		}

		if (pick < 0)
			pick = vga;

		if (pick < 0 && n > 0)
			pick = 0;

		if (pick >= 0)
			SendMessage(hwnd, CB_SETCURSEL, pick, 0);

		/* Fullscreen or Windowed: the stored choice the first time, the
		 * one on screen when the adapter changes, Windowed where nothing is
		 * stored - and Fullscreen on an adapter that cannot render into a
		 * window. Both radio buttons are set, so that exactly one is. */
		const bool can = App.DXInfo.DDInfo[nDDDevice].bCanRenderWindowed != 0;
		bool windowed;

		if (!can)
			windowed = 0;
		else if (radios_set)
			windowed = SendMessage(GetDlgItem(dlg, IDC_WINDOWED), BM_GETCHECK,
				0, 0) == BST_CHECKED;
		else
			windowed = stored_window != 0;

		EnableWindow(GetDlgItem(dlg, IDC_WINDOWED), can);
		SendMessage(GetDlgItem(dlg, IDC_WINDOWED), BM_SETCHECK, windowed, 0);
		SendMessage(GetDlgItem(dlg, IDC_FULLSCREEN), BM_SETCHECK, !windowed,
			0);
		radios_set = 1;
	}

	SendMessage(GetDlgItem(dlg, IDC_BILINEAR), BM_SETCHECK, Filter, 0);

	EnableWindow(GetDlgItem(dlg, IDC_LOW_QUALITY_TEX), 1);
	SendMessage(GetDlgItem(dlg, IDC_LOW_QUALITY_TEX), BM_SETCHECK, TextLow, 0);
}

static void InitD3DDevice(HWND dlg, HWND hwnd)
{
	DXDIRECTDRAWINFO* ddraw;

	ddraw = &App.DXInfo.DDInfo[nDDDevice];

	// A label, not a list. The D3D9 enumeration yields exactly ONE device per
	// adapter, so the combo box that used to stand here could offer a single
	// entry - a choice that is none. It now states which renderer is running,
	// which is all the box ever really told anybody. (Before the port it held
	// "software emulation" and "hardware" and the code picked index 1 blind;
	// with one device that pointed past the array and left the box - and
	// everything InitResolution derives from it - empty.)
	nD3DDevice = 0;
	SetWindowText(hwnd, ddraw->nD3DDevices > 0 ? ddraw->D3DDevices[0].About : "");
	InitResolution(dlg, GetDlgItem(dlg, IDC_RESOLUTION), 1);
}

static void InitDDDevice(HWND dlg, HWND hwnd)
{
	D3DADAPTER_IDENTIFIER9* id;
	char buffer[256];

	SendMessage(hwnd, CB_RESETCONTENT, 0, 0);

	for (int i = 0; i < App.DXInfo.nDDInfo; i++)
	{
		id = &App.DXInfo.DDInfo[i].DDIdentifier;
		wsprintf(buffer, "%s - %s (%d.%d.%02d.%04d)%s", id->Description, id->Driver,
			HIWORD(id->DriverVersion.HighPart), LOWORD(id->DriverVersion.HighPart),
			HIWORD(id->DriverVersion.LowPart), LOWORD(id->DriverVersion.LowPart),
			App.DXInfo.DDInfo[i].MonitorLabel);
		SendMessage(hwnd, CB_ADDSTRING, 0, (LPARAM)buffer);
	}

	// Originally defaulted to the last enumerated device (meaningful in the old world of
	// "software emulation" vs "hardware" DirectDraw devices). Each entry is now one physical
	// monitor's D3D9 adapter, so default to whichever one DXGetInfo already determined is the
	// actual Windows primary monitor instead, so the setup dialog (and a fresh registry with
	// no saved choice) don't default to launching fullscreen on the wrong screen.
	// The stored adapter where there is one (LoadStoredChoices).
	nDDDevice = stored_dd >= 0 ? stored_dd : App.DXInfo.nDD;
	SendMessage(hwnd, CB_SETCURSEL, nDDDevice, 0);
	InitD3DDevice(dlg, GetDlgItem(dlg, IDC_D3D));
}

static void InitDialogBox(HWND hwnd)
{
	// No sound device list any more: XAudio2 always plays through the system's
	// default endpoint, and the selection in that box was read by nothing.
	// What is left of the sound settings is the Disable switch.
	LoadStoredChoices();
	InitDDDevice(hwnd, GetDlgItem(hwnd, IDC_GRAPHICS_ADAPTER));
	SendMessage(GetDlgItem(hwnd, IDC_DISABLE_FMV), BM_SETCHECK, NoFMV, 0);
	SendMessage(GetDlgItem(hwnd, IDC_DISABLE_SOUND), BM_SETCHECK, NoSound, 0);

	// The corner shows the build date rather than a fixed version string
	// such as "tomb5 V4.0": the build date says something true and needs nobody to
	// remember to raise it, and it is the same stamp the log writes, so a
	// screenshot of this dialog and a log.txt can be matched up.
	{
		char buffer[64];

		wsprintf(buffer, "PCtomb5 build %s", __DATE__);
		SetDlgItemText(hwnd, IDC_VERSION, buffer);
	}
}

static BOOL CALLBACK SetupDlgProc(HWND dlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_INITDIALOG)
	{
		InitDialogBox(dlg);
		return 1;
	}

	if (message == WM_COMMAND)
	{
		switch ((ushort)wParam)
		{
		case IDOK:
			SaveSetup(dlg);
			EndDialog(dlg, 1);
			return 1;

		case IDCANCEL:
			EndDialog(dlg, 0);
			return 1;

		case IDC_GRAPHICS_ADAPTER:

			if (HIWORD(wParam) == CBN_SELCHANGE)
			{
				nDDDevice = SendMessage(GetDlgItem(dlg, IDC_GRAPHICS_ADAPTER), CB_GETCURSEL, 0, 0);
				InitD3DDevice(dlg, GetDlgItem(dlg, IDC_D3D));
			}

			break;

		case IDC_BILINEAR:

			if (HIWORD(wParam) == BN_CLICKED)
			{
				if (SendMessage(GetDlgItem(dlg, IDC_BILINEAR), BM_GETCHECK, 0, 0))
					Filter = 1;
				else
					Filter = 0;

				InitResolution(dlg, GetDlgItem(dlg, IDC_RESOLUTION), 0);
			}

			break;

		case IDC_LOW_QUALITY_TEX:

			if (HIWORD(wParam) == BN_CLICKED)
			{
				if (SendMessage(GetDlgItem(dlg, IDC_LOW_QUALITY_TEX), BM_GETCHECK, 0, 0))
					TextLow = 1;
				else
					TextLow = 0;

				InitResolution(dlg, GetDlgItem(dlg, IDC_RESOLUTION), 0);
			}

			break;

		}
	}

	return 0;
}

bool SetupDialog()
{
	long ret;

	ShowCursor(1);
	ret = DialogBox(App.hInstance, MAKEINTRESOURCE(IDD_SETUPDIALOG), 0, SetupDlgProc);
	ShowCursor(0);

	if (ret == -1)
	{
		MessageBox(0, "Unable To Initialise Dialog", "", 0);
		return 0;
	}

	if (ret)
		return 1;

	return 0;
}
