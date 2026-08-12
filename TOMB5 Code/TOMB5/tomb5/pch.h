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
// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#define WIN32_LEAN_AND_MEAN 
#define _USE_MATH_DEFINES
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <windowsx.h>
#include <stdint.h>
#include <stdio.h>
#include <mmeapi.h>
#include <d3d9.h>
#include <process.h>
#include <dinput.h>
#include <dsound.h>
#include <xaudio2.h>
#include <cmath>
#include <time.h>
#include <mmreg.h>
#include <mmiscapi.h>
#include <msacm.h>

#include "../../resource.h"

#endif //PCH_H
