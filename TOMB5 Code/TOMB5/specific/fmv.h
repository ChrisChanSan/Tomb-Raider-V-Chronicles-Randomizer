#pragma once
#include "../global/types.h"

bool LoadBinkStuff();
void FreeBinkStuff();
void FmvDeviceLost();
void ShowBinkFrame();
long PlayFmv(long num);
long PlayFmvNow(long num, long u);
