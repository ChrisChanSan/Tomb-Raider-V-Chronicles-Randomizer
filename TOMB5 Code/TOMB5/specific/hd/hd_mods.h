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

// THE MODS FOLDER (#153, round M1).
//
// A folder "Mods" beside the executable, in the remaster's own drop-in
// layout: Mods\<Category>\<Mod>\{4,5}\{ITEM,TEX,DATA}. Nothing of it is ever
// shipped by this project and nothing is copied anywhere - it is the player's
// own data, read where it lies, exactly like the HD art of #123 and the HQ
// sound bank.
//
// WHAT COUNTS AS A MOD. A folder directly under Mods, or one level deeper
// (Category\Mod), that has a "4" or a "5" subfolder or a mod.txt. A folder
// whose name begins with "_" is disabled, and everything under it with it -
// which is how a player turns one off without deleting it and gets vanilla
// back. The order is alphabetical by the relative path, compared as
// case-insensitive ASCII, and that order is the priority: the first active
// mod that has a file wins.
//
// WHAT IT ANSWERS. Two questions and nothing else:
//
//   - where is <NAME>.TRM   (HDModsFindModel)
//   - where is page <n>.DDS for a model that came out of mod i
//     (HDModsFindPage)
//
// plus the two sound answers, which are independent of the HD path because
// they are audio: the bank a mod ships (HDModsBank) and a loose NNNN.wav
// anywhere under it (HDModsWav) - and, since #159, the loose .txt files that
// lie BESIDE the mod folders rather than inside one (HDModsListCount). This
// walk is the only one that knows which folder is a mod, so it is the only
// one that can tell a list from a mod's own outfit.txt.
//
// A MOD NEVER TOUCHES A PAGE OF A MODEL IT DID NOT SUPPLY. That is the whole
// safety rule of this round and it is the user's: a page asked for by a
// vanilla model resolves exactly as it did before this file existed, so
// corruption of the vanilla look is impossible by construction rather than by
// care. The mechanism is the page namespace in hd_pagecache.h - a mod's 6111
// and the install's 6111 are two different cache slots.
//
// Engine-free like hd_install: built as C++17 with <filesystem>, plain
// C-shaped on the outside, so the diagnostic tools link the very same file
// the engine does.

// The mod count this build can carry. Tied to HD_PAGE_MOD_MAX in
// hd_pagecache.h, which is the real limit: every mod owns one page namespace.
#define HD_MODS_MAX	64

/* Where the Mods folder is. Null or "" means "Mods" in the process's current
 * directory, which for the game is the folder the executable sits in. Has to
 * be called BEFORE the first scan to have any effect - the diagnostic tools
 * do, the engine does not. */
void HDModsFolder(const char* path);

/* Scans, once per session. Every other call here does this first, so it does
 * not matter whether the HD path or the sound path asks first - they see the
 * same answer, which is the point of having one scanner. */
void HDModsScan();

int HDModsCount();
const char* HDModsPath(int index);	// the mod folder itself, absolute
const char* HDModsName(int index);	// its path relative to Mods
/* The game folder of the Tomb Raider I-III bundle whose page numbers this
 * mod's models use - mod.txt's "origin=tr1|tr2|tr3". 0 when it says nothing,
 * and then the mod's pages fall straight through to the IV-VI install. */
int HDModsOrigin(int index);

struct HDModShips
{
	int models;	// .TRM files under 5\ITEM and 4\ITEM, outfits included
	int outfits;	// of those, OUTFIT_*.TRM - a wardrobe entry each (M2)
	int pages;	// .DDS files under 5\TEX and 4\TEX
	int bank;	// 1 = it ships 5\DATA\MAIN.SFX
	int wavs;	// loose NNNN.wav, at any depth
};

void HDModsShipsOf(int index, HDModShips* out);

/* THE OUTFITS A MOD SHIPS, BY NAME (#153, round M2). The wardrobe reads this
 * list; HDModsFindModel still refuses OUTFIT_*.TRM, and deliberately, because
 * a mod's outfit is an ADDITIONAL wardrobe entry and never a replacement of
 * the install's file of the same name - the player picks it, the level does
 * not stumble into it. 5\ITEM before 4\ITEM, and a name that is in both is
 * listed once, out of 5. */
int HDModsOutfitCount(int index);
const char* HDModsOutfitName(int index, int at);	// the .TRM stem, upper case
const char* HDModsOutfitPath(int index, int at);	// absolute
int HDModsOutfitGame(int index, int at);		// 5 or 4

/* THE LOOSE LISTS (#159). A .txt file that lies BESIDE the mod folders -
 * directly in Mods, or directly in a category folder - and not INSIDE a mod.
 * The wardrobe reads them (hd_wardrobe.h): they are where an outfit that has
 * no mod folder, and therefore no outfit.txt of its own, is given the same
 * properties a mod's outfit.txt gives one.
 *
 * This file only finds them and says nothing about what is in them. The order
 * is the mods' own - the lower-cased path - because a later list overrides an
 * earlier one and that order has to be written down rather than the file
 * system's. A name beginning with "_" is switched off here too. */
int HDModsListCount();
const char* HDModsListPath(int at);	// absolute

/* THE MODEL RESOLVER. The active mods in order, each 5\ITEM then 4\ITEM.
 * Writes the path into `buffer` and returns it; an empty string means no
 * active mod has that name and the caller resolves as it always did.
 *
 * OUTFIT_*.TRM IS STILL REFUSED HERE, and now for a second reason (round M2).
 * It was refused in round M1 because Lara's wardrobe was a hand-written table
 * and a stranger's outfit would have been dressed by somebody else's
 * measurements. It stays refused because the wardrobe is now a SCAN
 * (hd_wardrobe.h) in which a mod's outfit is an ENTRY OF ITS OWN beside the
 * install's file of the same name: the player picks it, and a level that asks
 * for OUTFIT_CLASSIC by name goes on getting the install's OUTFIT_CLASSIC.
 * The list of what a mod ships is HDModsOutfitCount and its two neighbours
 * below.
 *
 * `modIndex` gets the mod that answered, or -1.
 *
 * `wantMod` names one mod and refuses every other, which is what a caller
 * that has already committed to a namespace needs - a face looked up for a
 * body that came out of mod 2 may not be answered by mod 0. -1, the default,
 * asks them all in order. The note in the log is made where the answer is
 * ACCEPTED, so what the log claims is what the caller got. */
const char* HDModsFindModel(const char* name, int* modIndex, char* buffer, int size,
	int wantMod = -1);

/* THE PAGE RESOLVER, first tier: mod `modIndex`'s own 5\TEX, then its 4\TEX.
 * Empty when the mod does not ship that page - the caller then tries the
 * mod's origin bundle and finally the install, which is where a mod that
 * ships only models finds its art. */
const char* HDModsFindPage(int modIndex, int page, char* buffer, int size);

/* The sound bank of a mod - its 5\DATA\MAIN.SFX and only that one. 4\DATA is
 * Tomb Raider IV's bank and its numbering is a different one; taking it for
 * a TR5 level would pair the bridge with the wrong recordings. Empty when
 * this mod ships none. */
const char* HDModsBank(int index);

/* A loose replacement sample: <number>.wav, written with exactly four digits,
 * anywhere under an active mod at any depth - so a modder's own "GlobeRaider
 * Files\0000.wav" works where it lies. First active mod wins per number.
 * Empty when no mod has that number. */
const char* HDModsWav(int number, int* modIndex, char* buffer, int size);

/* WHERE THE NOTES GO. The engine hands Log()'s one-string form in once, at
 * startup; the diagnostic tools hand in their own printer. Notes made before
 * a sink was installed are flushed to it on the spot, so the order of the two
 * calls does not matter. Without a sink they are simply kept and can be read
 * with the two calls below. */
void HDModsLogSink(void (*sink)(const char* line));

int HDModsNoteCount();
const char* HDModsNote(int at);

// Forgets everything, so the next call scans again. Tools and tests only.
void HDModsForget();
