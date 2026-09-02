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

struct SAMPLE_INFO;

// Optional high quality sound bank, read from audio\MAIN.SFX if the player
// put one there. See hqsfx.cpp for the format and for why nothing of it is
// ever shipped.

// Opens audio\MAIN.SFX and parses its tables. Safe to call repeatedly; the
// file is opened once and kept open. Returns false when there is no usable
// bank, in which case every other call here does nothing.
bool HQSfxOpen();

void HQSfxClose();

// Builds the bridge between this level's sample numbering and the bank's
// global one. Call once per level, right after the level's sound map has
// been read and before the samples are handed to DXCreateSample*.
void HQSfxBuildMap(const short* levelLut, long levelLutEntries,
	const SAMPLE_INFO* levelInfos, long levelInfoCount);

void HQSfxDropMap();

// Returns the bank's RIFF for one of this level's samples, or null when the
// sample has no counterpart. pcmBytes22k is the level sample's own decoded
// size and is used as a plausibility check - see hqsfx.cpp.
char* HQSfxPayload(long levelSample, long pcmBytes22k, long* riffBytes);

// How many samples of the current level were answered from the bank.
long HQSfxSubstituted();
