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

static void InitResolution(HWND dlg, HWND hwnd, bool resetvms)
{
	DXD3DDEVICE* device;
	DXDISPLAYMODE* dm;
	long w, h, n;
	char buffer[40];

	n = 0;

	if (resetvms)
	{
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
				SendMessage(hwnd, CB_SETCURSEL, n, 0);

			n++;
		}
	}

	if (App.DXInfo.DDInfo[nDDDevice].bCanRenderWindowed)
	{
		EnableWindow(GetDlgItem(dlg, IDC_WINDOWED), 1);
		SendMessage(GetDlgItem(dlg, IDC_WINDOWED), BM_SETCHECK, 1, 0);
	}
	else
	{
		EnableWindow(GetDlgItem(dlg, IDC_WINDOWED), 0);
		SendMessage(GetDlgItem(dlg, IDC_WINDOWED), BM_SETCHECK, 0, 0);
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
	nDDDevice = App.DXInfo.nDD;
	SendMessage(hwnd, CB_SETCURSEL, nDDDevice, 0);
	InitD3DDevice(dlg, GetDlgItem(dlg, IDC_D3D));
}

static void InitDialogBox(HWND hwnd)
{
	// No sound device list any more: XAudio2 always plays through the system's
	// default endpoint, and the selection in that box was read by nothing.
	// What is left of the sound settings is the Disable switch, and that needs
	// no preparation.
	InitDDDevice(hwnd, GetDlgItem(hwnd, IDC_GRAPHICS_ADAPTER));

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
