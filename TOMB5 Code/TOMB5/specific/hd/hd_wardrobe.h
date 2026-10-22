/*
 * Modified 2026 by ChrisChanSan.
 *
 * The role table of the wardrobe (which slot means which model) and the
 * composite it serves are carried over from TROne (Copyright 2026
 * Todesengel, GPLv3) - see hd_lara.h and THIRD-PARTY.md.
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

// The wardrobe: the list of every outfit body Lara can wear, and what each
// one takes of the rest of her (hands, braid, holsters, rig, sheen, skin).
//
// The list is a scan of the installs and the active mods (24 OUTFIT_* files
// in the IV-VI bundle's 4\ITEM, 78 in the I-III bundle's 1\ITEM); the
// built-in table in hd_wardrobe.cpp only adds measured properties of known
// files. Four programs use it - the engine draws out of it, ModelProbe prints
// it, Randomize.exe rolls out of it and the GUI shows it - so it is one
// implementation, C-shaped outside, C++17 with <filesystem> inside, and free
// of the engine. Two separate copies of the list would drift apart, and the
// player would get another outfit than the one picked.
//
// Scan order; the first entry of a file name wins the name:
//
//   1. the IV-VI install, 4\ITEM and then 5\ITEM. OUTFIT_YOUNG* is skipped:
//      young Lara carries no weapon and the randomizer hands her one in every
//      level.
//   2. the I-III install, 1\ITEM. OUTFIT_HANDS_*, OUTFIT_HAIR and
//      OUTFIT_YOUNG* are skipped, and so is every name step 1 already found -
//      the IV-VI bundle ships its own copies of the fourteen TR1/TR2/TR3
//      outfits, with IV-VI page numbers and a higher vertex count
//      (OUTFIT_TR1_CLASSIC: 14,515 vertices there against 8,382 here), so
//      those copies need no alternate root and are the better model.
//   3. every active mod, its 5\ITEM and then its 4\ITEM. A mod's outfit is an
//      additional entry, never a replacement of a vanilla one of the same file
//      name; its pages live in that mod's page namespace (hd_pagecache.h) and
//      resolve through the chain mod, origin bundle, install. The young filter
//      does not apply to a mod: outfit mods commonly put a grown-up body into
//      the young slot.
//
// The key survives a text line, a settings file and a log: the source and the
// file, separated by a slash.
//
//     46/OUTFIT_TR2_HOME                  the IV-VI install
//     13/OUTFIT_TRX_WETSUIT_1             the I-III install
//     mod/Outfits/AOD Wetsuit/OUTFIT_CLASSIC      a mod, by its folder
//
// A mod's key carries its folder because two mods may ship the same file name
// and both are then in the wardrobe at once. Forward slashes everywhere, so
// the key reads the same in a .hdm line, in the randomizer's settings file
// and on a command line. It is compared case-insensitively.
//
// The properties of an entry (hands, braid, holsters, rig, gloves, body sheen,
// skin sheen, skin pages) come from these tiers, highest first, and each is
// recorded so the log can say where every answer came from:
//
//   1. a list file's block, Mods\...\*.txt         HD_WARDROBE_FROM_LIST
//   2. the mod's outfit.txt, if it has one          HD_WARDROBE_FROM_MOD_TXT
//   3. the built-in table, keyed by FILE NAME       HD_WARDROBE_FROM_TABLE
//   4. the source bundle (head rule only)           HD_WARDROBE_FROM_ORIGIN
//   5. measured out of the .TRM itself              HD_WARDROBE_FROM_MEASURED
//   6. the defaults                                 HD_WARDROBE_FROM_DEFAULT
//
// A list file gives an outfit of an install - which has no folder of its own
// for an outfit.txt - or one body of a mod shipping many, its own settings. It
// is a .txt beside the mod folders (hd_mods' HDModsListCount finds them) and
// holds blocks of the shape
//
//     Outfit: TRX_Wetsuit_3
//     sheen=0.10
//     exponent=14
//
// and every key an outfit.txt takes is a key of a block. The name is matched
// against the entry's FILE stem without regard to case, to the OUTFIT_ prefix
// or to "_" against " ", and may carry a key prefix - "13/", "46/",
// "mod/outfits/legend/" - to pin one source where the same stem is in the
// wardrobe twice. A block sets only the keys it has; everything else keeps
// the value of the tier below.
//
// Only the skin pages have a measured tier; everything else is a judgement
// about a picture, which is why the table exists (geometry cannot tell skin
// from latex). The defaults suit a randomized run: hands, braid and holsters
// on, because she carries a gun in every level and every HD outfit wears the
// TR5 braid.

// The .TRM stem, the display name and the key are all bounded, so an entry is
// a plain struct with no ownership and a caller may keep a pointer to it for
// as long as it does not rescan.
#define HD_WARDROBE_KEY		192
#define HD_WARDROBE_NAME	64
#define HD_WARDROBE_FILE	64
#define HD_WARDROBE_SKIN	3

// Where an entry came from. The numbers are written into logs and into the
// randomizer's own source list, so they do not move.
#define HD_WARDROBE_SOURCE_46	0	// Tomb Raider IV-VI Remastered
#define HD_WARDROBE_SOURCE_13	1	// Tomb Raider I-III Remastered
#define HD_WARDROBE_SOURCE_MOD	2	// an active mod

// Where one property came from - see the note above.
#define HD_WARDROBE_FROM_DEFAULT	0
#define HD_WARDROBE_FROM_MEASURED	1
#define HD_WARDROBE_FROM_TABLE		2
#define HD_WARDROBE_FROM_MOD_TXT	3
/* The bundle the body came out of. Not a measurement of the file and not a
 * judgement about a picture, but what the source of an entry says about it:
 * a body of the Tomb Raider I-III bundle, or of a mod whose mod.txt names
 * that bundle with origin=, wears no hair. Only the head rule has it. */
#define HD_WARDROBE_FROM_ORIGIN		4
/* A block of a list file. The top tier: a line about one particular outfit
 * overrides both a mod's own outfit.txt and the built-in table. */
#define HD_WARDROBE_FROM_LIST		5

/* Whose head this body wears.
 *
 * Measured on the installs: every outfit of the IV-VI bundle carries Lara's
 * whole head on its head node - OUTFIT_CLASSIC 6,860 vertices and 9,485
 * triangles there, 9,483 of them on page 8002, the hair; OUTFIT_TR2_HOME
 * 6,830 the same way. The separate LARA_HEAD.TRM that rides rigidly on that
 * node is the face and the front of the skull, and its back is open: the two
 * are each other's halves.
 *
 * A body of the Tomb Raider I-III bundle carries only headgear on its head
 * node - the diving hood and goggles of OUTFIT_TRX_WETSUIT_* (1,987 vertices,
 * 998 triangles, all on the suit's own page), the ice hood, the Greek
 * headband - or, in six of the thirty, next to nothing (OUTFIT_TRX_BIKER_*:
 * 10 vertices, 0 triangles). None of the thirty uses page 8002. Without a
 * rule the open back of LARA_HEAD shows: a bald skull with a face on it and
 * the braid hanging off it. Mods built out of such files inherit this.
 *
 * The rule is one bit and one part: whether the body's own head node is
 * drawn, and whether TR5's head - node 14 of the install's own
 * OUTFIT_CLASSIC - is drawn over it. */
#define HD_WARDROBE_HEAD_OUTFIT		0	// its own head, as the file has it
#define HD_WARDROBE_HEAD_TR5		1	// its own head dropped, TR5's instead
#define HD_WARDROBE_HEAD_HAIR		2	// its own head AND TR5's hair over it

/* The footsteps an outfit walks on. Parsed and logged; no footstep sound
 * reads it yet. HD_WARDROBE_FEET_UNKNOWN means neither a mod's outfit.txt
 * nor the table names one. */
#define HD_WARDROBE_FEET_UNKNOWN	0
#define HD_WARDROBE_FEET_BARE		1
#define HD_WARDROBE_FEET_BOOTS		2

struct HDWardrobeEntry
{
	char key[HD_WARDROBE_KEY];	// "46/OUTFIT_TR2_HOME"
	char name[HD_WARDROBE_NAME];	// "TR2 Home"
	char file[HD_WARDROBE_FILE];	// "OUTFIT_TR2_HOME", upper case
	int source;			// HD_WARDROBE_SOURCE_*
	int mod;			// the mod's index, or -1

	// What the body takes of the rest of the wardrobe.
	bool hands;
	bool braid;
	bool holsters;
	/* What the wardrobe says about this body's hands. The draw ignores it:
	 * the bare hands are worn in every level with every outfit, so
	 * hd_lara.cpp's takes_gloves answers false for everything. Still parsed,
	 * printed and logged so that a report of the file stays complete. See
	 * the table's note in hd_wardrobe.cpp. */
	bool gloves;
	bool wearsRig;			// the holster rig cut out of the London catsuit

	float sheen;			// the body's Blinn-Phong strength ...
	float sheenPower;		// ... and exponent

	/* Lara's own skin sheen. The body's sheen is the clothes; this is her
	 * face, the lid on her neck, her bare hands and the body pages named in
	 * `skin` below, drawn with one material so the wrist seam does not show.
	 * It belongs to the outfit because a wetsuit wants a wet skin and a parka
	 * does not. Only a list block or an outfit.txt sets it (the table has no
	 * such column); otherwise it is the material table's `skin` pair, which
	 * hd_lara.cpp carries as kSheenSkin. */
	float skinSheen;
	float skinSheenPower;

	int skin[HD_WARDROBE_SKIN];	// pages that are Lara's own skin; 0 ends
	int feet;			// HD_WARDROBE_FEET_*
	int head;			// HD_WARDROBE_HEAD_*

	// Where each of those came from, one HD_WARDROBE_FROM_* each.
	int fromFlags;			// hands / braid / holsters / gloves / rig
	int fromSheen;
	int fromSkinSheen;
	int fromSkin;
	int fromFeet;
	int fromHead;
};

/* The word outfit.txt writes for a head rule, and the word the log prints:
 * "tr5", "hair" or "outfit". Never null. */
const char* HDWardrobeHeadWord(int head);

/* The same head as a short phrase for prose: "TR5 head", "TR5 hair" or
 * "own head". Never null. Nothing reads it at present: the wardrobe window
 * prints the head word itself (hd_outfitmenu.cpp) and the log the word. */
const char* HDWardrobeHeadPhrase(int head);

/* Where that answer came from, as a clause for a log line ("its mod.txt says
 * origin=tr1", "outfit.txt", "the default"). Never null. The phrase form, for
 * prose: the sentences of the log. */
const char* HDWardrobeFromWord(int from);

/* The same answer as one word for a table column or a log field - "default",
 * "measured", "table", "outfit.txt", "origin", "list". Never null. Callers use
 * this instead of an array of their own, so a new tier gets its name in every
 * reader. For the `key=value (tag)` fields of the level build line, the
 * columns of ModelProbe's tables and the rows of the F4 wardrobe window, so
 * that one tier never reads two ways in one window. */
const char* HDWardrobeFromTag(int from);

/* Where to look. Null or "" means "ask hd_install", which is what the engine
 * wants; the diagnostic tools pass the folders on the command line so that a
 * measurement can be repeated on a copy. Has to be called before the first
 * scan to have any effect. The mods folder is hd_mods' own business -
 * HDModsFolder - and is not repeated here. */
void HDWardrobeFolders(const char* root46, const char* root13);

// Scans once per session, exactly like HDModsScan. Every call below does this
// first, so the order in which the callers ask does not matter.
void HDWardrobeScan();
void HDWardrobeForget();

int HDWardrobeCount();
const HDWardrobeEntry* HDWardrobeAt(int index);

// The entry with that key, or -1. Case-insensitive; a backslash reads as a
// forward slash, so a key a user typed by hand still lands.
int HDWardrobeFind(const char* key);

/* The entry for a .TRM stem out of ONE source - which is how the eight known
 * outfits, who are named by the engine's enum and not by a key, find their
 * scan entry. -1 when this machine has no such file. */
int HDWardrobeFindFile(const char* file, int source);

// The absolute path of an entry's .TRM. Never null; "" for a bad index.
const char* HDWardrobePath(int index);

/* The files the scan looked at and did not list, each with the reason, so
 * that a missing outfit is a line in the log. The _HD twins of the I-III
 * bundle are all in here; see hd_wardrobe.cpp for the measurement. */
int HDWardrobeRefusedCount();
const char* HDWardrobeRefused(int at);

/* The measured tier. It needs a reader and a bind pose and therefore cannot
 * live in here (this file builds without hd_trm and without the engine, so
 * the randomizer can link it). hd_lara.cpp measures the throat page
 * off the model it has just read and hands it back; the entry takes it only
 * where neither outfit.txt nor the table named a skin page. Returns true when
 * it was taken. */
bool HDWardrobeSkinMeasured(int index, int page);

// Notes, exactly as hd_mods keeps them: a sink for the engine, a list for the
// tools, and notes made before a sink was installed are flushed to it.
void HDWardrobeLogSink(void (*sink)(const char* line));
int HDWardrobeNoteCount();
const char* HDWardrobeNote(int at);
