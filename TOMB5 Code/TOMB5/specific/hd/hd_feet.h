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

// What Lara walks on.
//
// An outfit says boots or bare feet (HDWardrobeEntry::feet, and the player may
// overrule it in the F4 changer), and this is the half of that answer which is
// audible. It owns nothing about the wardrobe and nothing about the menu: it
// owns the level's footstep sets and the choice between them at every step.
//
// A step is two layers, not one - the branch does not surface under either of
// their names, so a plain grep for one sound id misses the other:
//
//   the foot    sound id 0 through an animation command - game\control.cpp
//               reads `num = cmd[1] & 0x3FFF` out of the animation and calls
//               SoundEffect(num, ...). Measured on ANDREA1's Lara slot: 148
//               frames carry `PlaySound sfx 0`, and they are 33 to 41 ms long.
//
//   the floor   game\footprnt.cpp AddFootprint plays
//               `footsounds[floor->fx] + SFX_FOOTSTEPS_MUD` - sound ids 288 to
//               294 - for every sector whose fx is not 5, 6 or 11. It is
//               effect_routines[32], and Lara's own animations carry
//               `FlipEffect no 32` on 121 of those same 148 frames (measured
//               with AnimCmd; three more call it straight out of game\lara.cpp).
//               Those recordings are the long half of a step: ANDREA1's id 288
//               is 274 ms where its id 0 is 41.
//
// So the switch is the two ids and the floor layer. A barefoot recording under
// her foot and a boot on the surface under it on the same frame would be both
// at once, so while she is drawn barefoot AddFootprint asks
// HDFeetSurfaceQuiet() and passes the floor layer over. The footprint itself
// is a picture and is not touched. Sound id 17 (the wet foot) is not switched:
// there is one dry barefoot set and no wet one.
//
// Who walks. Ids 0 and 4 are not Lara's alone: measured with AnimCmd, a guard
// (BLUE_GUARD, BLUE_GUARD2, SWAT_PLUS_MIP on the guard's animation), MAFIA2,
// HITMAN and CYBORG play them from their own animations, and the shotgun's
// weapon item carries id 0 as well. They all keep the level's own
// entry whatever Lara wears; only a step of Lara's is ever redirected. The
// caller is SfxCommandCaller (game\sound.h), which AnimateItem and
// AnimateLara set around every sound of an animation command. Her weapon
// item - the shotgun, HK or crossbow she holds, an item of its own that
// AnimateItem animates and names by its object number - is hers: a step it
// plays is chosen as her own (HDFeetCallerOf below).
//
// The three sets, all in memory from the level's start:
//
//   ordinary  the level's own entries for ids 0 and 4, loaded through the
//             ordinary chain like every other sample: a mod's loose .wav, a
//             mod's bank, else the unmodded bank, else the level's own
//             recording. This is what the classic draw plays, and what every
//             emitter but Lara plays in either draw. A sound mod's footsteps
//             are heard here, exactly as without the HD build.
//
//   bare      what the HD draw plays under a barefoot outfit. When the
//             ordinary chain's id 0 is a mod's own recording (a footstep
//             mod), that IS the bare set: the two spare entries point at the
//             level's own samples and nothing is loaded twice. Otherwise the
//             remaster's own barefoot recordings, sound id HQ_SFX_BARE_FEET
//             of the bank, which no level carries - in DS_Buffers slots after
//             the level's own.
//
//   boots     what the HD draw plays under an outfit with boots: the first
//             unmodded bank's ids 0 and 4. Loaded only when the ordinary
//             chain's footsteps differ from the unmodded bank's, because
//             otherwise the ordinary set already is those boots and the choice
//             falls through to it.
//
// The test for "a footstep mod" is not "is a mod's bank being played" but
// "does the chain's recording differ from the unmodded bank's, byte for
// byte": a mod's bank is one way to replace the footsteps and a loose
// 0000..0003.wav (0010 for the landing) is the other, and a mod that is about
// doors or voices carries the install's own footsteps and answers no.
//
// The landing, measured on the bank: the boots have four steps of 33 to 41 ms
// and one landing of 175 ms - 4.7 times a step and 2.4 times its peak. Of the
// bank's five barefoot recordings (106 / 86 / 134 / 69 / 113 ms) none stands
// out that way, so the bare id 4 draws from the same five takes as the bare
// id 0, with the level's own id 4 volume and radius.
//
// The infos. The level's sound map is game_malloc'ed and exactly num_infos
// long, so file.cpp asks for four more and this fills them: bare steps, bare
// landing, unmodded boots' steps, unmodded boots' landing. Their fields are
// the level's own id 0 and id 4 entries - how loud, how far and how the
// engine rolls between takes is the level's business - except the sample
// number and count, which are what was really loaded, and the bare set's
// pitch, which comes from the bank's own barefoot entry (0 where id 0 is 12).
//
// The choice is made at every step, in SoundEffect, by HDFeetStepInfo below:
// the sound map itself is never bent. So the HD/SD switch changes the next
// step and the F4 changer the next step after it closes, and a step already
// sounding finishes as it began.
//
// Nothing is allocated after the level is up. Everything here is loaded while
// LoadSamples runs; the outfit's answer afterwards is one flag.

struct SAMPLE_INFO;

/* How many spare SAMPLE_INFO entries LoadSamples allocates past the level's
 * own for this file: bare steps, bare landing, unmodded boots' steps and
 * unmodded boots' landing. */
#define HD_FEET_SPARE_INFOS	4

/* The two sound ids this file switches, SFX_LARA_FEET and SFX_LARA_LAND of
 * game\sound.h - written out here so that the rule below compiles without
 * the engine's headers, and checked against them in hd_feet.cpp. */
#define HD_FEET_ID_STEPS	0
#define HD_FEET_ID_LAND		4

/* The spare entries of one level, by sound map index, -1 where a set is not
 * there. */
struct HDFeetSets
{
	int bareSteps;
	int bareLand;
	int bootSteps;
	int bootLand;
};

/* The step's choice, pure: which sound map entry sound id `sfx` plays.
 *
 *   mapped   the level's own entry for it (sample_lut[sfx]), the ordinary set
 *   caller   who plays it: SfxCommandCaller as HDFeetCallerOf sees it, an
 *            object number or -1
 *   lara     Lara's object number (LARA)
 *   drawnHD  whether Lara is drawn HD at this step (HDLaraDrawActive)
 *   bare     whether her HD outfit walks barefoot (HDFeetIsBare)
 *
 * Every emitter but Lara, every id but 0 and 4, and every step while she is
 * drawn classic keep `mapped`. Drawn HD, she walks on the bare set or on the
 * unmodded boots, and on `mapped` wherever the set she wants is not there. */
inline int HDFeetPick(int sfx, int mapped, long caller, long lara,
	bool drawnHD, bool bare, const HDFeetSets& sets)
{
	if (caller < 0 || caller != lara || !drawnHD || mapped < 0)
		return mapped;

	int pick;

	if (sfx == HD_FEET_ID_STEPS)
		pick = bare ? sets.bareSteps : sets.bootSteps;
	else if (sfx == HD_FEET_ID_LAND)
		pick = bare ? sets.bareLand : sets.bootLand;
	else
		return mapped;

	return pick >= 0 ? pick : mapped;
}

/* Who a step's caller is, pure: the caller itself, except that the item at
 * lara.weapon_item is Lara. It is asked by the item's index in items[] and
 * never by its object number, so a weapon item that is not hers stays what it
 * is; -1 (NO_ITEM) on either side is nobody's.
 *
 *   caller      SfxCommandCaller, an object number or -1
 *   lara        Lara's object number (LARA)
 *   item        the index of the item whose animation plays the sound, or -1
 *   weaponItem  lara.weapon_item at that moment, or -1 */
inline long HDFeetCallerOf(long caller, long lara, long item, long weaponItem)
{
	return item >= 0 && item == weaponItem ? lara : caller;
}

/* The floor layer's rule, pure: passed over only while she is drawn HD, her
 * outfit walks barefoot and the level has a bare set to walk on. Its one
 * caller plays it for Lara alone, so there is no emitter to ask. */
inline bool HDFeetQuietRule(bool drawnHD, bool bare, bool haveBare)
{
	return drawnHD && bare && haveBare;
}

/* Called from LoadSamples once the level's own samples are decoded. `lut` and
 * `infos` are the level's own tables, `infoCount` how many entries were read
 * and `spare` how many entries beyond that the caller allocated. `next` is the
 * first free DS_Buffers slot, which is the level's num_samples.
 *
 * Loads what the sets need into the slots from `next` up, fills the spare
 * infos and writes its own log lines. Safe with no bank at all - then there
 * is no bare set and HDFeetAvailable() answers false. */
void HDFeetLevelBuild(short* lut, SAMPLE_INFO* infos, int infoCount, int spare,
	int next);

/* One of the level's own samples as LoadSamples' loop left it: its decoded
 * size, so that the unmodded boots are held to the same length rule the loop
 * holds every bank sample to, and whether the loop took the ordinary chain's
 * payload for it. A take the loop refused holds the level's own recording,
 * and a mod's recording that never reached the slot is no mod. Stores two
 * facts and decides nothing. */
void HDFeetNoteSample(int levelSample, long pcmBytes22k, bool chainTaken);

// Thrown away with the level. The tables themselves are game_malloc's.
void HDFeetLevelEnd();

/* The FEET line behind -log, once a level's outfit is known: what the
 * ordinary set is, whether the unmodded boots are loaded, the bare set's
 * entries and what the outfit wants. Silent in a level that built no sets. */
void HDFeetSay();

// Is there a barefoot set in this level at all? False means the F4 row says
// "unavailable" and the log says why.
bool HDFeetAvailable();

// Why not, in words, for the menu and the log. "" when it is available.
const char* HDFeetWhyNot();

/* What the outfit wants: barefoot or in boots. One flag, read at every step;
 * may be called at any time and is idempotent. Does nothing at all when there
 * is no bare set. Returns what is in force afterwards. */
bool HDFeetBare(bool bare);
bool HDFeetIsBare();

/* The sound map entry one sound plays - HDFeetPick with this level's sets,
 * Lara's object number, her draw at this moment and the caller as
 * HDFeetCallerOf sees it. SoundEffect asks it for every sound once the
 * level's own entry is known; anything but ids 0 and 4 comes back unchanged
 * at once. */
int HDFeetStepInfo(int sfx, int mapped, long caller);

/* The index in items[] of the item whose animation command plays the sound
 * at hand, and lara.weapon_item at that moment, both -1 for none: AnimateItem
 * hands them over around the sound as it sets SfxCommandCaller, and -1 and -1
 * after it. HDFeetStepInfo asks HDFeetCallerOf with them. */
void HDFeetStepItem(long item, long weaponItem);

/* For the SFXLOG line: "bare" or "boots" when `info` is one of this level's
 * spare entries, "" otherwise. */
const char* HDFeetInfoName(int info);

/* Whether the FLOOR layer is to be passed over - see the two layers above.
 * HDFeetQuietRule with this level's answer: true only while she is drawn HD
 * and barefoot on a bare set, so with the classic draw, with boots on, or in
 * a level that has no second set the player hears the level as it is.
 * Says so in the log the first time in each quiet spell; the caller is
 * game\footprnt.cpp and nothing else. */
bool HDFeetSurfaceQuiet();

// What the level's sets hold, for the log line and for a tool.
int HDFeetBareTakes();		// recordings behind sound id 0 when bare
int HDFeetBareLandTakes();	// ... and behind sound id 4; 0 only without an id 4
const char* HDFeetBareSource();	// where they came from. Never null.
