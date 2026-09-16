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

// THE WARDROBE (#153, round M2).
//
// Until this round Lara's wardrobe was eight rows of a table in hd_lara.cpp,
// and every one of them was a file somebody had opened, counted and written
// down. The installs hold far more than eight - 24 OUTFIT_* files in the
// IV-VI bundle's 4\ITEM and 78 in the I-III bundle's 1\ITEM - and a mod may
// bring another. So the LIST is a scan now, and the table is what it still
// is: measurements about particular files, which the scan wears where it has
// them.
//
// ONE IMPLEMENTATION, four callers. The engine draws out of it, ModelProbe
// prints it, Randomize.exe rolls out of it and the GUI shows it - so it is
// built the way hd_install and hd_mods are: plain C-shaped on the outside,
// C++17 with <filesystem> inside, and NOTHING of the engine in it. A list
// that two programs work out separately is a list that will one day disagree
// with itself, and the wardrobe is the one place where that would show up as
// "the outfit I picked is not the outfit I got".
//
// WHAT IS SCANNED, in this order - the order IS the priority, because the
// first entry of a file name wins the name:
//
//   1. the IV-VI install, 4\ITEM and then 5\ITEM. OUTFIT_YOUNG* is skipped:
//      young Lara carries no weapon and the randomizer hands her one in every
//      level (the user's decision).
//   2. the I-III install, 1\ITEM. OUTFIT_HANDS_*, OUTFIT_HAIR and
//      OUTFIT_YOUNG* are skipped, and so is every name step 1 already found -
//      the IV-VI bundle ships its OWN copies of the fourteen TR1/TR2/TR3
//      outfits, with IV-VI page numbers and a higher vertex count
//      (OUTFIT_TR1_CLASSIC: 14,515 vertices there against 8,382 here), so
//      those copies need no alternate root and are the better model.
//   3. every active mod, its 5\ITEM and then its 4\ITEM. A mod's outfit is an
//      ADDITIONAL entry and never a replacement of a vanilla one of the same
//      file name; its pages live in that mod's page namespace (hd_pagecache.h)
//      and resolve through the M1 chain - the mod, its origin bundle, the
//      install. The young filter does NOT apply to a mod: three of the four
//      outfit mods the user runs are grown-up bodies sitting in the young
//      slot, and refusing them would refuse the very thing the round is for.
//
// THE KEY is what survives a text line, a settings file and a log: the source
// and the file, separated by a slash.
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
// THE PROPERTIES of an entry - does it take the hands, the braid, the
// holsters, the holster rig, is it gloved, how does it shine, how does her
// skin shine, which of its pages are Lara's own skin - come from five places,
// in this order, and every one of them is recorded so the log can say WHERE
// each answer came from:
//
//   1. a list file's block, Mods\...\*.txt          HD_WARDROBE_FROM_LIST
//   2. the mod's outfit.txt, if it has one          HD_WARDROBE_FROM_MOD_TXT
//   3. the built-in table, keyed by FILE NAME       HD_WARDROBE_FROM_TABLE
//   4. measured out of the .TRM itself              HD_WARDROBE_FROM_MEASURED
//   5. the defaults                                 HD_WARDROBE_FROM_DEFAULT
//
// THE LIST FILE (#159) is what an outfit of an INSTALL has instead of an
// outfit.txt. A mod is a folder and can carry one; the 52 outfits of the two
// bundles have no folder of their own, and a mod that ships seventeen bodies
// under one outfit.txt has no way to say anything about ONE of them. So a
// .txt beside the mod folders - hd_mods' HDModsListCount finds them - holds
// blocks of the shape
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
// about a picture, which is exactly why the table exists (hd_lara.cpp's own
// note: geometry cannot tell skin from latex). The defaults are the ones the
// randomizer needs: hands, braid and holsters ON, because she carries a gun
// in every level of a randomized run and the TR5 braid is what the user wants
// on every HD outfit.

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
/* THE BUNDLE THE BODY CAME OUT OF (#156). A fifth tier, and it sits between
 * the default and the table: it is not a measurement of the file and not a
 * judgement about a picture, it is the one thing the SOURCE of an entry says
 * about it - a body of the Tomb Raider I-III bundle, or of a mod whose
 * mod.txt names that bundle with origin=, wears no hair. Only the head rule
 * below has it. */
#define HD_WARDROBE_FROM_ORIGIN		4
/* A BLOCK OF A LIST FILE (#159). The top tier: a player who writes a line
 * about one particular outfit has said the last word about it, over a mod's
 * own outfit.txt and over this file's table alike. */
#define HD_WARDROBE_FROM_LIST		5

/* WHOSE HEAD THIS BODY WEARS (#156).
 *
 * WHAT WAS MEASURED, because the rule is nothing but the measurement. Every
 * outfit of the IV-VI bundle carries LARA'S WHOLE HEAD on its head node -
 * OUTFIT_CLASSIC 6,860 vertices and 9,485 triangles there, 9,483 of them on
 * page 8002, which is the HAIR; OUTFIT_TR2_HOME 6,830 the same way. The
 * separate LARA_HEAD.TRM that rides rigidly on that node is the FACE and the
 * front of the skull, and its back is open (a picture of it alone looks
 * through the back of her head at the inside of her own face): the two are
 * each other's halves by the remaster's own design.
 *
 * A body of the Tomb Raider I-III bundle is not built that way. Its head node
 * carries HEADGEAR and nothing else - the diving hood and goggles of
 * OUTFIT_TRX_WETSUIT_* (1,987 vertices, 998 triangles, all on the suit's own
 * page), the ice hood, the Greek headband - or, in six of the thirty, near
 * enough nothing at all (OUTFIT_TRX_BIKER_*: 10 vertices, 0 triangles). Not
 * one of the thirty wears page 8002. So the hair is simply not in the file,
 * the open back of LARA_HEAD stands in the picture, and what the player sees
 * is a bald skull with a face on it and the braid hanging off it. That is the
 * user's report of 2026-09-15 and his three photographs.
 *
 * A mod built out of such a file inherits it, and the whole outfit folder the
 * user runs is built out of them.
 *
 * THE RULE IS THEREFORE ONE BIT AND ONE PART: whether the body's own head
 * node is drawn, and whether TR5's head - node 14 of the install's own
 * OUTFIT_CLASSIC - is drawn over it. Three combinations are worth having and
 * they are these three. */
#define HD_WARDROBE_HEAD_OUTFIT		0	// its own head, as the file has it
#define HD_WARDROBE_HEAD_TR5		1	// its own head dropped, TR5's instead
#define HD_WARDROBE_HEAD_HAIR		2	// its own head AND TR5's hair over it

/* THE FOOTSTEPS an outfit walks on (#153, round M2). Parsed and logged now,
 * acted on in round M3 - the barefoot sample set is that round's work. A
 * value of HD_WARDROBE_FEET_UNKNOWN means nobody has said, which is what
 * every entry is until a mod's outfit.txt or the table names one. */
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
	/* What the wardrobe SAYS about this body's hands - and nothing reads it
	 * since #153, round M2b: the wardrobe wears the bare hands in every level
	 * with every outfit (the user's decision), so hd_lara.cpp's takes_gloves
	 * answers false for everything. Still parsed, still printed, still logged,
	 * because a line that reports a file has to go on reporting what the file
	 * says. See the table's own note in hd_wardrobe.cpp. */
	bool gloves;
	bool wearsRig;			// the holster rig cut out of the London catsuit

	float sheen;			// the body's Blinn-Phong strength ...
	float sheenPower;		// ... and exponent

	/* AND LARA'S OWN SKIN'S (#158). The body's sheen is what the CLOTHES are
	 * made of; this is what SHE is made of - her face, the lid on her neck,
	 * her bare hands and the pages of the body named in `skin` below, which
	 * are drawn with one material so that the wrist seam is not two. It is a
	 * property of the outfit because an outfit decides how much of her is
	 * showing and in what light: a wetsuit wants a wet skin and a parka does
	 * not. Nothing but a list block or an outfit.txt sets it - the eight
	 * measured rows of the table never had the column - so without a line it
	 * is the material table's own `skin` pair, which hd_lara.cpp carries as
	 * kSheenSkin. */
	float skinSheen;
	float skinSheenPower;

	int skin[HD_WARDROBE_SKIN];	// pages that are Lara's own skin; 0 ends
	int feet;			// HD_WARDROBE_FEET_*
	int head;			// HD_WARDROBE_HEAD_* (#156)

	// Where each of those came from, one HD_WARDROBE_FROM_* each.
	int fromFlags;			// hands / braid / holsters / gloves / rig
	int fromSheen;
	int fromSkinSheen;		// #158
	int fromSkin;
	int fromFeet;
	int fromHead;
};

/* The word outfit.txt writes for a head rule, and the word the log prints:
 * "tr5", "hair" or "outfit". Never null. */
const char* HDWardrobeHeadWord(int head);

/* And the short phrase the wardrobe window shows in a row: "TR5 head",
 * "TR5 hair" or "own head". Never null. */
const char* HDWardrobeHeadPhrase(int head);

/* Where that answer came from, as a clause for a log line ("its mod.txt says
 * origin=tr1", "outfit.txt", "the default"). Never null.
 *
 * THE PHRASE FORM, and it is the one that belongs in PROSE: a sentence of the
 * log and every row of the F4 wardrobe window. */
const char* HDWardrobeFromWord(int from);

/* The same answer as ONE WORD for a column of a table or a field of a log
 * line - "default", "measured", "table", "outfit.txt", "origin", "list".
 * Never null, and never an index into a caller's own array: two of those
 * existed until #159 and one of them was already a row short of the tiers
 * this header defines. Add a tier here and every reader says its name.
 *
 * THE FIELD FORM, and it belongs where the characters are counted: the
 * `key=value (tag)` fields of the level build line and the columns of
 * ModelProbe's tables. One window, one form - the F4 rows all use the phrase
 * above, because a player reading "the table" over "table" in two rows of one
 * window has to ask whether they mean the same tier. */
const char* HDWardrobeFromTag(int from);

/* WHERE TO LOOK. Null or "" means "ask hd_install", which is what the engine
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

/* A FILE THE SCAN LOOKED AT AND DID NOT LIST, with the reason - so that a
 * missing outfit is a line in the log and not a mystery. The _HD twins of the
 * I-III bundle are all in here; see hd_wardrobe.cpp for the measurement. */
int HDWardrobeRefusedCount();
const char* HDWardrobeRefused(int at);

/* THE MEASURED TIER, which needs a reader and a bind pose and therefore
 * cannot live in here (this file must build without hd_trm and without the
 * engine, so the randomizer can link it). hd_lara.cpp measures the throat page
 * off the model it has just read and hands it back; the entry takes it only
 * where neither outfit.txt nor the table named a skin page. Returns true when
 * it was taken. */
bool HDWardrobeSkinMeasured(int index, int page);

// Notes, exactly as hd_mods keeps them: a sink for the engine, a list for the
// tools, and notes made before a sink was installed are flushed to it.
void HDWardrobeLogSink(void (*sink)(const char* line));
int HDWardrobeNoteCount();
const char* HDWardrobeNote(int at);
