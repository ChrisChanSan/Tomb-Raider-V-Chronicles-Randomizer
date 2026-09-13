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

#include "sfxbank.h"		// SfxRiffParts - where HQSfxPayload found the chunks

struct SAMPLE_INFO;

// Optional high quality sound bank: a mod's own, the player's audio\MAIN.SFX,
// or the installed remaster's, in that order (#153, round M1). See hqsfx.cpp
// for the chain, the format and why nothing of it is ever shipped.

// Opens the first bank of the chain that parses, and its tables with it. Safe
// to call repeatedly; the file is opened once and kept open. Returns false
// when there is no usable bank anywhere, in which case every other call here
// does nothing.
bool HQSfxOpen();

void HQSfxClose();

// Builds the bridge between this level's sample numbering and the bank's
// global one. Call once per level, right after the level's sound map has
// been read and before the samples are handed to DXCreateSample*.
void HQSfxBuildMap(const short* levelLut, long levelLutEntries,
	const SAMPLE_INFO* levelInfos, long levelInfoCount);

void HQSfxDropMap();

/* Returns the bank's RIFF for one of this level's samples - or a mod's loose
 * .wav for it - or null when the sample has no counterpart. pcmBytes22k is
 * the level sample's own decoded size and is used as a plausibility check;
 * see hqsfx.cpp.
 *
 * *parts, when asked for, says where the format block and the audio lie in
 * the returned buffer (#153, round M1, second fix round). ASK FOR IT rather
 * than assuming the format block sits at offset 20: a wav whose fmt chunk is
 * not the first one passes the gate here - correctly, that is a legal file
 * an editor may well write - and the caller that read offset 20 found two
 * bytes of a JUNK header there, silently fell back to the level's own sample,
 * and left this file's counters claiming a substitution nobody heard.
 *
 * THE CONTRACT THAT COSTS: everything this function returns has passed
 * SfxRiffCheck, so a caller using *parts CANNOT reject it afterwards. That is
 * what makes HQSfxSubstituted() and HQSfxOverlaid() true rather than hopeful,
 * and it is why they are counted here. */
char* HQSfxPayload(long levelSample, long pcmBytes22k, long* riffBytes,
	SfxRiffParts* parts = 0);

/* ----------------------------------------------- the footsteps -- */

/* THE REMASTER'S BAREFOOT SET (#153, round M3) is sound id 417 of the bank
 * and of no level: five recordings at 22050 Hz, 69 to 134 ms, which the user
 * listened to and identified. The bank's own entry for them is the footstep
 * entry with the random pitch turned off - measured, id 0 is volume 191,
 * radius 10, pitch 12, and id 417 is volume 191, radius 10, pitch 0.
 *
 * Not an enum: it is a number in a FILE, like every other id in this bridge. */
#define HQ_SFX_BARE_FEET	417

/* The boots' payload for one of the LEVEL's samples: the first UNMODDED bank
 * of the chain and no loose .wav overlay, so that a sound mod which replaces
 * the footsteps cannot take the BOOTS away from every outfit at once. Only
 * ever called for the samples behind sound ids 0 and 4. Null means "keep the
 * level's own", which is Core's boots and already right. */
char* HQSfxPayloadBoots(long levelSample, long pcmBytes22k, long* riffBytes,
	SfxRiffParts* parts = 0);

/* WHICH MOD'S LOOSE .wav ANSWERS ONE OF THIS LEVEL'S SAMPLES (#153, round M3,
 * the review after the fix round), by name, for a log line - "" when the bank
 * itself answers it. It asks the same overlay HQSfxPayload asks, through the
 * same bridge, and loads nothing; a mod that ships <NNNN>.wav and no bank at
 * all is a footstep mod exactly as a mod's bank is, and the bare set has to be
 * able to say so. Valid until the next call. */
const char* HQSfxWavSource(long levelSample);

/* The unmodded bank's own sample numbers for one sound id, and one of those
 * samples. The barefoot set is reached this way and not through the level
 * bridge, because no level has sound id 417 at all. `samples` takes at most
 * `max`; the return is how many were written. The buffer HQSfxBankSample
 * hands back is the bank's own and lives until the next call on it. */
int HQSfxBankTakes(int soundId, long* samples, int max);
char* HQSfxBankSample(long sample, long* riffBytes, SfxRiffParts* parts = 0);

// Whether the bank being PLAYED came out of a mod - see hd_feet.cpp.
bool HQSfxFromMod();

// Which bank that is, for the log. Never null; "" when there is none.
const char* HQSfxPlainSource();

// How many samples of the current level were answered from the bank.
long HQSfxSubstituted();

// Of those, how many a mod's own loose .wav supplied (#153, round M1).
long HQSfxOverlaid();

/* And how many were samples a mod's BANK had replaced with a recording of a
 * different length - the ones the plausibility rule would have refused if the
 * unmodded bank had not confirmed the pairing. See hqsfx.cpp. */
long HQSfxReplaced();

// Which of the three banks was opened, in words, for the log line that says
// where this level's samples came from. Empty when none was.
const char* HQSfxSource();

// Whether there is a bank at all - a loose .wav needs one for its bridge.
bool HQSfxHaveBank();
