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

// WHAT LARA WALKS ON (#153, round M3).
//
// An outfit says boots or bare feet (HDWardrobeEntry::feet, and the player may
// overrule it in the F4 changer), and this is the half of that answer which is
// audible. It owns nothing about the wardrobe and nothing about the menu: it
// owns the level's two sets of footstep recordings and the two entries of the
// sound map that choose between them.
//
// A STEP IS TWO LAYERS, NOT ONE (corrected in the fix round of M3 - the round
// first claimed there was no material branch, and the grep it rested on could
// not have found one, because the branch does not use these names).
//
//   THE FOOT   sound id 0 through an ANIMATION COMMAND - game\control.cpp
//              reads `num = cmd[1] & 0x3FFF` out of the animation and calls
//              SoundEffect(num, ...). Measured on ANDREA1's LARA slot: 148
//              frames carry `PlaySound sfx 0`, and they are 33 to 41 ms long.
//
//   THE FLOOR  game\footprnt.cpp AddFootprint plays
//              `footsounds[floor->fx] + SFX_FOOTSTEPS_MUD` - sound ids 288 to
//              294 - for every sector whose fx is not 5, 6 or 11. It is
//              effect_routines[32], and Lara's own animations carry
//              `FlipEffect no 32` on 121 of those same 148 frames (measured
//              with AnimCmd; three more call it straight out of game\lara.cpp).
//              Those recordings are the long half of a step: ANDREA1's id 288
//              is 274 ms where its id 0 is 41.
//
// So the switch is the two ids AND the floor layer. Bending sound id 0 and
// sound id 4 puts the bank's barefoot recording under her foot; leaving the
// floor layer alone would put a BOOT on the surface under it on the same
// frame, which is the sound the layer is - a boot on mud, on metal, on wood.
// While she is barefoot AddFootprint asks HDFeetSurfaceQuiet() and passes it
// over. The footPRINT itself is a picture and is not touched.
//
// THE TWO SETS.
//
//   BOOTS  the level's own samples for ids 0 and 4, loaded from the first
//          UNMODDED bank of the chain (HQSfxPayloadBoots) and NOT from a mod's
//          bank or a mod's loose .wav. That is the user's decision: a sound mod
//          that replaces the footsteps replaces them for every outfit at once,
//          and his Underworld bare footsteps made Lara barefoot in boots, in a
//          wetsuit and in a parka alike. With the outfit choosing between two
//          sets, the two have to BE two.
//
//   BARE   extra DS_Buffers slots after the level's own samples. What goes in
//          them is what the ORDINARY chain would have played for ids 0 and 4 -
//          so a barefoot sound mod is heard exactly where it belongs - and,
//          when the chain has nothing of its own to say, the remaster's own
//          barefoot recordings: sound id HQ_SFX_BARE_FEET of the bank, which
//          no level carries.
//
//          THE ORDINARY CHAIN IS BOTH KINDS OF MOD (the review after the fix
//          round of M3). A mod's BANK is one way to replace the footsteps and
//          a loose 0000..0003.wav beside it is the other - "a mod's <NNNN>.wav
//          replaces bank sample NNNN", which for the footsteps is exactly
//          those four and 0010. The test here is not "is a mod's bank being
//          played" but "does the chain's sound id 0 differ from the unmodded
//          bank's", so both kinds answer it and a mod that is about doors or
//          voices answers it with no.
//
// AND THE LANDING, MEASURED. The boots have four steps of 33 to 41 ms and one
// landing of 175 ms - a landing is 4.7 times a step and 2.4 times its peak. Of
// the bank's five barefoot recordings (106 / 86 / 134 / 69 / 113 ms) none
// stands out that way: the longest is 1.4 times the mean of the other four and
// the loudest by RMS is 1.25 times theirs, where the boots' landing is 4.7 and
// 1.4. So ALL FIVE ARE STEPS, the landing keeps the boots' recording, and the
// log says so. (The user's Underworld mod does distinguish its landing - 4.4
// times the RMS of its steps - and when that mod is the source its id 4 is the
// bare landing.)
//
// THE INFOS. The level's sound map is game_malloc'ed and exactly num_infos
// long, so file.cpp asks for two more and this fills them: one for the bare
// steps and one for the bare landing. Their fields are the LEVEL's own id 0
// and id 4 entries - how loud, how far and how the engine rolls between takes
// is the level's business - with two exceptions, both measured: the sample
// number and count are what was really loaded, and the PITCH comes from the
// bank's own barefoot entry, which is 0 where id 0 is 12. The remaster ships
// those five recordings with the random pitch turned off and that is a
// statement about them, not about footsteps in general.
//
// NOTHING IS ALLOCATED AFTER THE LEVEL IS UP. Everything here is decided while
// LoadSamples runs; the switch afterwards writes two shorts into sample_lut.

struct SAMPLE_INFO;

/* How many spare SAMPLE_INFO entries LoadSamples allocates past the level's
 * own for this file: one for the bare steps and one for the bare landing. */
#define HD_FEET_SPARE_INFOS	2

/* Called from LoadSamples once the level's own samples are decoded. `lut` and
 * `infos` are the level's own tables, `infoCount` how many entries were read
 * and `spare` how many entries beyond that the caller allocated (two). `next`
 * is the first free DS_Buffers slot, which is the level's num_samples.
 *
 * Loads the bare set into the slots from `next` up, fills the spare infos and
 * writes its own log lines. Safe with no bank at all - then there is no bare
 * set and HDFeetAvailable() answers false. */
void HDFeetLevelBuild(short* lut, SAMPLE_INFO* infos, int infoCount, int spare,
	int next);

// Thrown away with the level. The tables themselves are game_malloc's.
void HDFeetLevelEnd();

// Is there a barefoot set in this level at all? False means the F4 row says
// "unavailable" and the log says why.
bool HDFeetAvailable();

// Why not, in words, for the menu and the log. "" when it is available.
const char* HDFeetWhyNot();

/* Bend sound ids 0 and 4 to the bare set or back to the level's own. Costs two
 * shorts, may be called at any time and is idempotent. Does nothing at all
 * when there is no bare set. Returns what is in force afterwards. */
bool HDFeetBare(bool bare);
bool HDFeetIsBare();

/* Whether the FLOOR layer is to be passed over - see the two layers above.
 * True only while a bare set is in force, so a player with hd_enable off, with
 * boots on, or in a level that has no second set hears exactly what he heard
 * before. Says so in the log the first time in each bare spell, which is the
 * seam a play test reads; the caller is game\footprnt.cpp and nothing else. */
bool HDFeetSurfaceQuiet();

// What the level's two sets hold, for the log line and for a tool.
int HDFeetBareTakes();		// recordings behind sound id 0 when bare
int HDFeetBareLandTakes();	// ... and behind sound id 4; 0 keeps the boots'
const char* HDFeetBareSource();	// where they came from. Never null.
