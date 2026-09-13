/*
 * Modified 2026 by ChrisChanSan.
 *
 * The eight rows of the table below are this project's own measurements, made
 * over rounds of #123; the composite they describe is carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3) - see hd_lara.h and THIRD-PARTY.md.
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
#include "hd_wardrobe.h"

#include "hd_install.h"
#include "hd_mods.h"
#include "hd_trm.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
	/* ------------------------------------------------- the override table -- */

	/* WHAT A PARTICULAR FILE IS, where somebody has looked at the picture.
	 *
	 * These eight rows are the wardrobe as it stood before this round - the
	 * table that used to be kOutfit in hd_lara.cpp - and every column of every
	 * one of them is a measurement or a judgement made over a picture in the
	 * rounds of #123. They are keyed by FILE NAME, which is also the honest
	 * warning about them: the measurement was made on the INSTALL'S copy of
	 * that file. A mod that replaces the name inherits the row, because a mod
	 * that replaces OUTFIT_CLASSIC is nearly always a reskin of the Rome body
	 * and wants exactly these answers - and where it is not, its outfit.txt
	 * says so and wins (see resolve_properties below).
	 *
	 * `hands`, `braid` and `holsters` say whether the body takes the rest of
	 * the wardrobe. The diving suit takes none of it: its own nodes 10 and 13
	 * carry 1352 vertices each - the gauntlets ARE the suit - it wears a
	 * helmet instead of a braid, and DrawLara__5 draws no holsters at all.
	 *
	 * `gloves` picks the HAND_GLOVES_ file instead of the HAND_BARE_ one
	 * (#123, fix round C, user finding C2: "OUTFIT_RUSSIA wears gloves"). An
	 * outfit model covers all fifteen nodes including the two hand nodes, but
	 * what it carries THERE is the sleeve - the hand itself is the separate
	 * HAND_ model, which is the whole reason the remaster ships three sets of
	 * them.
	 *
	 * `wearsRig` is the separate HOLSTER RIG - the belt, the two pouches and
	 * the two thigh straps cut out of the London catsuit by page (#123, fix
	 * round D, D4; the rule corrected in fix round F, F1).
	 *
	 *   THE MEASUREMENT ROUND D MADE ITS RULE. Triangles on nodes 1 and 4 by
	 *   texture page, over the eight wardrobe bodies - does the body carry a
	 *   rig of its OWN?
	 *
	 *     OUTFIT_CLASSIC      8017: 1090/1090   + 8018: 56/56    yes
	 *     OUTFIT_RUSSIA       8017: 1106/1114   + 8018: 56/56    yes
	 *     OUTFIT_TR3_COAST    8205:  354/354                     yes
	 *     OUTFIT_TR3_CATSUIT  8205:  354/354                     yes
	 *     OUTFIT_VCI          -                                  NO
	 *     OUTFIT_TR3_ANTARC   -                                  NO
	 *     OUTFIT_TR3_NEVADA   -                                  NO
	 *     OUTFIT_SUBSUIT      -                          holsters none anyway
	 *
	 *   AND THE RULE WAS WRONG ON THE PICTURE. "No rig of its own, so wear the
	 *   London one" put black leather straps over the Antarctica parka: the
	 *   belt disappears inside the padded jacket, the two pouches stand off
	 *   the hip at the width the London catsuit's thin thighs want, and the
	 *   straps cut through the coat (user finding F1 of the play test of
	 *   2026-09-06). Nevada is the same body shape and the same picture.
	 *   OUTFIT_VCI has no rig because vanilla TR5 hangs pistols on that
	 *   catsuit and the remaster left the pouches to the holster file; the
	 *   other two have none because in TR3 those outfits never wore a thigh
	 *   rig at all. Only the first of the three is a hole. So this is a TABLE
	 *   and not a rule derived from geometry - and an entry the scan finds
	 *   gets `false` until somebody has looked at its picture.
	 *
	 * `sheen` and `sheenPower` are the body's Blinn-Phong strength and
	 * exponent (#123, fix round D, D2). TR5's own lighting has no specular
	 * term at all, so every HD outfit came out matt beside the remaster's own
	 * pictures. The obvious source would have been the pages' ALPHA channel;
	 * measured with DdsProbe -alpha over the wardrobe's pages it is flat
	 * opaque (mean 254.6..255.0, not one texel below 128 on any of them), so
	 * there is no mask to read and the material has to be named:
	 *
	 *   latex     0.45 / 26   the two catsuits, the reference look
	 *   rubber    0.30 / 22   the diving suit
	 *   cloth     0.10 / 14   the Rome top and the rest
	 *
	 * `skin` names the pages of the BODY that are Lara's own skin (#123, fix
	 * round K, K2), so that a bare forearm and the hand model over it shine
	 * with ONE material instead of two. Measured with ModelProbe -handlight
	 * and -neck: 8001 is her skin in the three game 5 bodies, and the four
	 * Tomb Raider III guests carry theirs on 8201 (the neck) and 8202 (the
	 * arms) - the same two pages in all four, because it is the same Lara
	 * underneath. OUTFIT_SUBSUIT has none ON PURPOSE: the diving suit shows no
	 * skin anywhere and its throat page 8047 is the SUIT, so naming it would
	 * make the whole suit matt. That row is also why the measured tier cannot
	 * replace this table - see resolve_properties.
	 *
	 * `feet` is for round M3 and is unknown in every row today: which sample
	 * set a body walks on is a decision about sound, and the barefoot set does
	 * not exist yet. */
	struct Row
	{
		const char* file;
		bool hands;
		bool braid;
		bool holsters;
		bool gloves;
		bool wearsRig;
		float sheen;
		float sheenPower;
		int skin[HD_WARDROBE_SKIN];
		int feet;
	};

	/*                            hands  braid  holst  glove  rig                   */
	const Row kRow[] =
	{
		{ "OUTFIT_CLASSIC",     true,  true,  true,  false, false, 0.10f, 14.0f,
			{ 8001, 0, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_RUSSIA",      true,  true,  true,  true,  false, 0.12f, 16.0f,
			{ 8001, 0, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_SUBSUIT",     false, false, false, false, false, 0.30f, 22.0f,
			{ 0, 0, 0 },    HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_VCI",         true,  true,  true,  false, true,  0.45f, 26.0f,
			{ 8001, 0, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_TR3_ANTARC",  true,  true,  true,  false, false, 0.10f, 14.0f,
			{ 8201, 8202, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_TR3_NEVADA",  true,  true,  true,  false, false, 0.08f, 12.0f,
			{ 8201, 8202, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_TR3_COAST",   true,  true,  true,  false, false, 0.08f, 12.0f,
			{ 8201, 8202, 0 }, HD_WARDROBE_FEET_UNKNOWN },
		{ "OUTFIT_TR3_CATSUIT", true,  true,  true,  false, false, 0.45f, 26.0f,
			{ 8201, 8202, 0 }, HD_WARDROBE_FEET_UNKNOWN }
	};

	/* THE DEFAULTS, for a file nobody has looked at. They are not "nothing":
	 * a randomized run puts a gun in Lara's hand in every level, so a body
	 * that takes no holsters would hang its pistols in the air, and the user's
	 * decision is that EVERY HD outfit wears the TR5 braid. `gloves` is the
	 * one that defaults the other way: a bare hand under a sleeve reads as a
	 * long sleeve, a gloved hand under a bare arm reads as a fault. */
	const bool kDefaultHands = true;
	const bool kDefaultBraid = true;
	const bool kDefaultHolsters = true;
	const bool kDefaultGloves = false;
	const bool kDefaultRig = false;
	const float kDefaultSheen = 0.10f;
	const float kDefaultSheenPower = 14.0f;

	/* ------------------------------------------------------------ state -- */

	struct Entry
	{
		HDWardrobeEntry out;
		std::string path;
		std::string sort;	// the key, lower cased
	};

	bool scanned = false;
	std::string want46, want13;
	std::string used46, used13;

	std::vector<Entry> entries;
	std::vector<std::string> refused;

	std::vector<std::string> notes;
	size_t notes_flushed = 0;
	void (*note_sink)(const char*) = 0;

	void note(const char* format, ...)
	{
		char line[512];
		va_list args;

		va_start(args, format);
		std::vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		notes.push_back(line);

		if (note_sink)
			while (notes_flushed < notes.size())
				note_sink(notes[notes_flushed++].c_str());
	}

	/* ------------------------------------------------------- small tools -- */

	std::string lower(const std::string& in)
	{
		std::string out = in;

		for (size_t i = 0; i < out.size(); i++)
		{
			if (out[i] >= 'A' && out[i] <= 'Z')
				out[i] = (char)(out[i] - 'A' + 'a');
			else if (out[i] == '\\')
				out[i] = '/';
		}

		return out;
	}

	std::string upper(const std::string& in)
	{
		std::string out = in;

		for (size_t i = 0; i < out.size(); i++)
			if (out[i] >= 'a' && out[i] <= 'z')
				out[i] = (char)(out[i] - 'a' + 'A');

		return out;
	}

	std::string join(const std::string& a, const std::string& b)
	{
		if (a.empty())
			return b;

		if (!a.empty() && (a[a.size() - 1] == '\\' || a[a.size() - 1] == '/'))
			return a + b;

		return a + "\\" + b;
	}

	std::string parent_of(const std::string& path)
	{
		std::string out = path;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		const size_t cut = out.find_last_of("\\/");

		return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
	}

	std::string leaf_of(const std::string& path)
	{
		std::string out = path;

		while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
			out.erase(out.size() - 1);

		const size_t cut = out.find_last_of("\\/");

		return cut == std::string::npos ? out : out.substr(cut + 1);
	}

	std::string trim(const std::string& in)
	{
		size_t a = 0, b = in.size();

		while (a < b && (in[a] == ' ' || in[a] == '\t' || in[a] == '\r'))
			a++;

		while (b > a && (in[b - 1] == ' ' || in[b - 1] == '\t' || in[b - 1] == '\r'))
			b--;

		return in.substr(a, b - a);
	}

	void put(char* out, int size, const std::string& in)
	{
		std::snprintf(out, (size_t)size, "%s", in.c_str());
	}

	/* THE DISPLAY NAME out of the file name. "OUTFIT_" comes off, the
	 * underscores become spaces, and a word is Title Cased UNLESS it is one
	 * the eye reads as an abbreviation - one with a digit in it (TR2, TRX,
	 * 1) or one of three letters or fewer (VCI, FMV, HK). So OUTFIT_TR2_HOME
	 * reads "TR2 Home" and OUTFIT_TRX_WETSUIT_1 reads "TRX Wetsuit 1". */
	std::string display_of(const std::string& file)
	{
		std::string stem = file;

		if (stem.size() > 7 && upper(stem).compare(0, 7, "OUTFIT_") == 0)
			stem = stem.substr(7);

		std::string out;
		size_t at = 0;

		while (at < stem.size())
		{
			size_t end = stem.find('_', at);

			if (end == std::string::npos)
				end = stem.size();

			std::string word = stem.substr(at, end - at);
			bool abbreviation = word.size() <= 3;

			for (size_t i = 0; i < word.size(); i++)
				if (word[i] >= '0' && word[i] <= '9')
					abbreviation = true;

			if (!abbreviation)
				word = upper(word.substr(0, 1)) + lower(word.substr(1));
			else
				word = upper(word);

			if (!out.empty())
				out += " ";

			out += word;
			at = end + 1;
		}

		return out.empty() ? file : out;
	}

	/* WHICH NAMES THE SCAN PASSES OVER, and why each of them.
	 *
	 * OUTFIT_YOUNG* - young Lara's bodies. A randomized run hands her a
	 * weapon in every level and a child body cannot carry one (the user's
	 * decision). It does NOT apply to a mod: three of the four outfit mods
	 * the user runs are grown-up bodies that happen to sit in the young slot,
	 * and that is the whole point of a mod.
	 *
	 * OUTFIT_HANDS_* and OUTFIT_HAIR - the I-III bundle's names for what the
	 * IV-VI bundle calls HAND_BARE_* and HAIR_OG. They are PARTS of the
	 * composite, not bodies: 15 nodes' worth of Lara is not in them. */
	bool skipped(const std::string& file, int source, const char** why)
	{
		const std::string up = upper(file);

		if (source != HD_WARDROBE_SOURCE_MOD && up.compare(0, 12, "OUTFIT_YOUNG") == 0)
		{
			*why = "young Lara carries no weapon - the user's decision";
			return true;
		}

		if (up.compare(0, 13, "OUTFIT_HANDS_") == 0)
		{
			*why = "a pair of hands and not a body";
			return true;
		}

		if (up == "OUTFIT_HAIR")
		{
			*why = "a braid and not a body";
			return true;
		}

		return false;
	}

	/* ------------------------------------------------------- outfit.txt -- */

	/* A MOD MAY SAY WHAT ITS OUTFIT IS (#153, round M2): <mod>\outfit.txt,
	 * one key=value per line, "#" starts a comment - exactly the shape of
	 * mod.txt one folder up, so a modder learns one format and not two. It
	 * applies to every OUTFIT_*.TRM that mod ships, which is one in all four
	 * of the outfit mods the user runs.
	 *
	 *   name=Sola's wetsuit      what the wardrobe window calls it
	 *   hands=0|1                does it take the separate HAND_ models
	 *   gloves=0|1               ... the gloved ones
	 *   braid=0|1                does it wear the TR5 braid
	 *   holsters=0|1             does it wear thigh holsters
	 *   rig=0|1                  ... and the pouches they hang in
	 *   sheen=0.45               the body's specular strength ...
	 *   exponent=26              ... and its Blinn-Phong exponent
	 *   skin=8001,8201           which of its pages are Lara's own skin
	 *   footsteps=bare|boots     round M3's business; read and logged now
	 *
	 * An unknown key is a note and never a fault, for the same reason mod.txt
	 * does it that way: a mod written for a later round has to stay loadable
	 * by this one. */
	struct ModTxt
	{
		bool has = false;
		std::string name;
		int hands = -1, gloves = -1, braid = -1, holsters = -1, rig = -1;
		float sheen = -1.0f, exponent = -1.0f;
		bool hasSkin = false;
		int skin[HD_WARDROBE_SKIN] = { 0, 0, 0 };
		int feet = HD_WARDROBE_FEET_UNKNOWN;
	};

	int as_bool(const std::string& value)
	{
		if (value == "1" || value == "yes" || value == "true" || value == "on")
			return 1;

		if (value == "0" || value == "no" || value == "false" || value == "off")
			return 0;

		return -1;
	}

	void read_outfit_txt(int mod, ModTxt& out)
	{
		const std::string path = join(HDModsPath(mod), "outfit.txt");
		std::FILE* file = std::fopen(path.c_str(), "rb");

		if (!file)
			return;

		out.has = true;

		char raw[512];

		while (std::fgets(raw, sizeof(raw), file))
		{
			std::string line = raw;
			const size_t hash = line.find('#');

			if (hash != std::string::npos)
				line.erase(hash);

			while (!line.empty() && (line[line.size() - 1] == '\n' ||
				line[line.size() - 1] == '\r'))
				line.erase(line.size() - 1);

			line = trim(line);

			if (line.empty())
				continue;

			const size_t eq = line.find('=');

			if (eq == std::string::npos)
			{
				note("MODLOG mod%d %s: outfit.txt line \"%s\" has no = - ignored",
					mod, HDModsName(mod), line.c_str());
				continue;
			}

			const std::string key = lower(trim(line.substr(0, eq)));
			const std::string value = trim(line.substr(eq + 1));
			const std::string low = lower(value);

			if (key == "name")
				out.name = value;
			else if (key == "hands")
				out.hands = as_bool(low);
			else if (key == "gloves")
				out.gloves = as_bool(low);
			else if (key == "braid")
				out.braid = as_bool(low);
			else if (key == "holsters")
				out.holsters = as_bool(low);
			else if (key == "rig")
				out.rig = as_bool(low);
			else if (key == "sheen")
				out.sheen = (float)std::atof(value.c_str());
			else if (key == "exponent")
				out.exponent = (float)std::atof(value.c_str());
			else if (key == "footsteps")
			{
				if (low == "bare")
					out.feet = HD_WARDROBE_FEET_BARE;
				else if (low == "boots")
					out.feet = HD_WARDROBE_FEET_BOOTS;
				else
					note("MODLOG mod%d %s: outfit.txt footsteps=%s is neither "
						"bare nor boots - ignored", mod, HDModsName(mod),
						value.c_str());
			}
			else if (key == "skin")
			{
				int at = 0;
				std::string one;

				out.hasSkin = true;

				for (size_t i = 0; i <= value.size(); i++)
				{
					if (i == value.size() || value[i] == ',')
					{
						one = trim(one);

						if (!one.empty() && at < HD_WARDROBE_SKIN)
							out.skin[at++] = std::atoi(one.c_str());

						one.clear();
					}
					else
						one.push_back(value[i]);
				}
			}
			else
				note("MODLOG mod%d %s: outfit.txt key \"%s\" is not one this "
					"build knows - ignored", mod, HDModsName(mod), key.c_str());
		}

		std::fclose(file);
	}

	/* ------------------------------------------------------ the property -- */

	const Row* row_of(const std::string& file)
	{
		const std::string up = upper(file);

		for (size_t i = 0; i < sizeof(kRow) / sizeof(kRow[0]); i++)
			if (up == kRow[i].file)
				return &kRow[i];

		return 0;
	}

	/* THE FOUR TIERS, and the one thing they cannot do.
	 *
	 * Tier 3, "measured out of the file", exists for exactly one property -
	 * which of the body's pages are Lara's own skin - and it is NOT applied
	 * here, because it needs the .TRM read against a level's own bind pose
	 * (hd_lara.cpp's measure_neck_skin, the -neck machinery: the page whose
	 * torso-node vertices inside the neck cylinder span the greatest depth
	 * down the neck axis). hd_lara hands it back through
	 * HDWardrobeSkinMeasured once it has one.
	 *
	 * WHAT THAT RULE ANSWERS, checked against all eight rows of the table
	 * (ModelProbe -lara ANDREA1 -outfit <x> -nodraw, the "her throat" note):
	 *
	 *     OUTFIT_CLASSIC      8001   the table says 8001            agrees
	 *     OUTFIT_RUSSIA       8001   the table says 8001            agrees
	 *     OUTFIT_VCI          8001   the table says 8001            agrees
	 *     OUTFIT_TR3_ANTARC   8201   the table says 8201 and 8202   the first
	 *     OUTFIT_TR3_NEVADA   8201   the table says 8201 and 8202   the first
	 *     OUTFIT_TR3_COAST    8201   the table says 8201 and 8202   the first
	 *     OUTFIT_TR3_CATSUIT  8201   the table says 8201 and 8202   the first
	 *     OUTFIT_SUBSUIT      8047   the table says NOTHING         disagrees
	 *
	 * Seven of the eight, the rule finds the table's FIRST page and finds it
	 * exactly. The two disagreements are both about the same limit and both
	 * are real:
	 *
	 *   - the four Tomb Raider III guests carry a SECOND skin page, 8202,
	 *     which is their bare ARMS. A measurement taken at the neck cannot
	 *     reach an arm, and no measurement anywhere can: the same page number
	 *     is bare skin under the Antarctica parka and a latex sleeve under
	 *     the London catsuit. Geometry cannot tell skin from latex - which is
	 *     the sentence this whole table exists for.
	 *   - the diving suit HAS a throat page, 8047, and it is the HELMET. The
	 *     rule is not wrong about the file; it is right about the file and
	 *     wrong about Lara. Naming it would hand the skin material to the
	 *     whole suit.
	 *
	 * So the measured tier gives a NEW entry exactly one page - its throat,
	 * which is the page the first person neck lid wears and the page a bare
	 * neck needs - and everything beyond that is a picture somebody has to
	 * look at. Which is what the table above is for, and what a mod's
	 * outfit.txt is for. */
	void resolve_properties(Entry& e, const ModTxt& txt)
	{
		HDWardrobeEntry& o = e.out;
		const Row* row = row_of(o.file);

		o.hands = kDefaultHands;
		o.braid = kDefaultBraid;
		o.holsters = kDefaultHolsters;
		o.gloves = kDefaultGloves;
		o.wearsRig = kDefaultRig;
		o.sheen = kDefaultSheen;
		o.sheenPower = kDefaultSheenPower;
		o.skin[0] = o.skin[1] = o.skin[2] = 0;
		o.feet = HD_WARDROBE_FEET_UNKNOWN;

		o.fromFlags = HD_WARDROBE_FROM_DEFAULT;
		o.fromSheen = HD_WARDROBE_FROM_DEFAULT;
		o.fromSkin = HD_WARDROBE_FROM_DEFAULT;
		o.fromFeet = HD_WARDROBE_FROM_DEFAULT;

		if (row)
		{
			o.hands = row->hands;
			o.braid = row->braid;
			o.holsters = row->holsters;
			o.gloves = row->gloves;
			o.wearsRig = row->wearsRig;
			o.sheen = row->sheen;
			o.sheenPower = row->sheenPower;
			o.feet = row->feet;

			for (int i = 0; i < HD_WARDROBE_SKIN; i++)
				o.skin[i] = row->skin[i];

			o.fromFlags = HD_WARDROBE_FROM_TABLE;
			o.fromSheen = HD_WARDROBE_FROM_TABLE;
			o.fromFeet = row->feet != HD_WARDROBE_FEET_UNKNOWN
				? HD_WARDROBE_FROM_TABLE : HD_WARDROBE_FROM_DEFAULT;

			/* The diving suit's empty list is a DECISION and not a gap, so
			 * the table owns that property even when the list is empty - it
			 * is what keeps the measured tier from making the suit matt. */
			o.fromSkin = HD_WARDROBE_FROM_TABLE;
		}

		if (!txt.has)
			return;

		if (txt.hands >= 0) { o.hands = txt.hands != 0; o.fromFlags = HD_WARDROBE_FROM_MOD_TXT; }
		if (txt.braid >= 0) { o.braid = txt.braid != 0; o.fromFlags = HD_WARDROBE_FROM_MOD_TXT; }
		if (txt.holsters >= 0) { o.holsters = txt.holsters != 0; o.fromFlags = HD_WARDROBE_FROM_MOD_TXT; }
		if (txt.gloves >= 0) { o.gloves = txt.gloves != 0; o.fromFlags = HD_WARDROBE_FROM_MOD_TXT; }
		if (txt.rig >= 0) { o.wearsRig = txt.rig != 0; o.fromFlags = HD_WARDROBE_FROM_MOD_TXT; }

		if (txt.sheen >= 0.0f) { o.sheen = txt.sheen; o.fromSheen = HD_WARDROBE_FROM_MOD_TXT; }
		if (txt.exponent > 0.0f) { o.sheenPower = txt.exponent; o.fromSheen = HD_WARDROBE_FROM_MOD_TXT; }

		if (txt.hasSkin)
		{
			for (int i = 0; i < HD_WARDROBE_SKIN; i++)
				o.skin[i] = txt.skin[i];

			o.fromSkin = HD_WARDROBE_FROM_MOD_TXT;
		}

		if (txt.feet != HD_WARDROBE_FEET_UNKNOWN)
		{
			o.feet = txt.feet;
			o.fromFeet = HD_WARDROBE_FROM_MOD_TXT;
		}
	}

	/* ------------------------------------------------------- the folders -- */

	// "<bundle>\5" -> "<bundle>\<game>". The installs are named by their game
	// folder everywhere in this project (hd_install.h), and the sibling is
	// where the wardrobe's files are: 4\ITEM in the IV-VI bundle, 1\ITEM in
	// the I-III one.
	std::string game_folder(const std::string& root, int game)
	{
		char digit[4];

		std::snprintf(digit, sizeof(digit), "%d", game);

		return join(parent_of(root), digit);
	}

	bool add_one(const std::string& path, const std::string& file, int source,
		int mod, const std::string& key, const std::string& name)
	{
		Entry e;

		std::memset(&e.out, 0, sizeof(e.out));

		put(e.out.key, HD_WARDROBE_KEY, key);
		put(e.out.name, HD_WARDROBE_NAME, name);
		put(e.out.file, HD_WARDROBE_FILE, upper(file));
		e.out.source = source;
		e.out.mod = mod;
		e.path = path;
		e.sort = lower(e.out.key);

		entries.push_back(e);
		return true;
	}

	bool have_key(const std::string& key)
	{
		const std::string want = lower(key);

		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].sort == want)
				return true;

		return false;
	}

	bool have_file(const std::string& file, int source)
	{
		const std::string want = upper(file);

		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].out.source == source && want == entries[i].out.file)
				return true;

		return false;
	}

	/* THE .TRM ITSELF DECIDES WHETHER IT IS LISTED. Two files are refused and
	 * each for a measured reason:
	 *
	 *   - one this reader cannot get past at all. There is nothing to wear.
	 *   - one laid out with THIRTY byte vertices whose 24 byte twin is in the
	 *     same folder. Both describe the same body; the wide one carries its
	 *     texture coordinates as floats and this engine's vertex holds them
	 *     as bytes, so wearing it would round nearly every coordinate by up
	 *     to half a step of a 256 step axis (measured over
	 *     OUTFIT_TRX_WETSUIT_1_HD: 14,208 of 14,262 coordinates lie off that
	 *     grid, and off every other grid from 1/256 to 1/4096). The twin is
	 *     the same Lara without the rounding. Measured over both bundles, all
	 *     2,923 .TRM files: exactly thirty are laid out that way and every one
	 *     of them is an OUTFIT_TRX_*_HD.TRM with its twin beside it - so this
	 *     rule takes nothing off the list that the list has not got twice.
	 *     A wide file with NO twin is listed and worn, with the rounding in
	 *     the log, because a rounded picture beats no picture. */
	bool readable(const std::string& path, const std::string& file,
		const std::string& folder, std::string& why, int* vertexBytes)
	{
		HDTrmInfo info;
		const char* trouble = "";

		*vertexBytes = 0;

		if (!HDTrmProbe(path.c_str(), &info, &trouble))
		{
			why = trouble;
			return false;
		}

		*vertexBytes = info.vertexBytes;

		if (info.vertexBytes != 30)
			return true;

		const std::string up = upper(file);

		if (up.size() > 3 && up.compare(up.size() - 3, 3, "_HD") == 0)
		{
			const std::string twin = up.substr(0, up.size() - 3);

			if (HDTrmExists(join(folder, twin + ".TRM").c_str()))
			{
				why = "30 byte vertices, and " + twin + " is the same body "
					"with texture coordinates this engine's vertex can hold";
				return false;
			}
		}

		return true;
	}

	void add_from_install(const std::string& root, int game, int source)
	{
		const std::string folder = join(game_folder(root, game), "ITEM");
		std::error_code ec;
		fs::directory_iterator it(fs::u8path(folder), ec), end;

		if (ec)
			return;

		std::vector<std::string> leaves;

		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;

			if (!it->is_regular_file(ec) || ec)
				continue;

			leaves.push_back(it->path().filename().string());
		}

		// The file system's own order is not a promise; the wardrobe's is.
		std::sort(leaves.begin(), leaves.end(),
			[](const std::string& a, const std::string& b)
			{ return lower(a) < lower(b); });

		for (size_t i = 0; i < leaves.size(); i++)
		{
			const std::string& leaf = leaves[i];
			const std::string low = lower(leaf);

			if (low.size() <= 4 || low.compare(low.size() - 4, 4, ".trm") != 0)
				continue;

			if (low.compare(0, 7, "outfit_") != 0)
				continue;

			const std::string file = upper(leaf.substr(0, leaf.size() - 4));
			const char* why = "";

			if (skipped(file, source, &why))
			{
				refused.push_back(std::string(source == HD_WARDROBE_SOURCE_46
					? "46/" : "13/") + file + " - " + why);
				continue;
			}

			/* THE IV-VI BUNDLE'S COPY WINS THE NAME. It ships its own
			 * OUTFIT_TR1_* / TR2_* / TR3_* files with IV-VI page numbers, so
			 * they need no alternate root at all, and they are the better
			 * model: OUTFIT_TR1_CLASSIC has 14,515 vertices there against
			 * 8,382 in the I-III bundle. Measured over both folders. */
			if (source == HD_WARDROBE_SOURCE_13 && have_file(file, HD_WARDROBE_SOURCE_46))
			{
				refused.push_back("13/" + file + " - the IV-VI install has the "
					"same name and its copy carries that bundle's own pages");
				continue;
			}

			const std::string path = join(folder, leaf);
			std::string trouble;
			int wide = 0;

			if (!readable(path, file, folder, trouble, &wide))
			{
				refused.push_back(std::string(source == HD_WARDROBE_SOURCE_46
					? "46/" : "13/") + file + " - " + trouble);
				continue;
			}

			const std::string key = std::string(source == HD_WARDROBE_SOURCE_46
				? "46/" : "13/") + file;

			if (have_key(key))
				continue;

			add_one(path, file, source, -1, key, display_of(file));

			if (wide == 30)
				note("HDLOG wardrobe %s has 30 byte vertices and no twin - "
					"its texture coordinates are rounded onto the byte grid",
					key.c_str());
		}
	}

	void add_from_mods()
	{
		for (int mod = 0; mod < HDModsCount(); mod++)
		{
			const int count = HDModsOutfitCount(mod);

			if (!count)
				continue;

			ModTxt txt;

			read_outfit_txt(mod, txt);

			for (int at = 0; at < count; at++)
			{
				const std::string file = HDModsOutfitName(mod, at);
				const std::string path = HDModsOutfitPath(mod, at);
				const std::string name = lower(HDModsName(mod));
				const std::string key = "mod/" + name + "/" + file;
				const char* why = "";

				if (skipped(file, HD_WARDROBE_SOURCE_MOD, &why))
				{
					refused.push_back(key + " - " + why);
					continue;
				}

				std::string trouble;
				int wide = 0;

				if (!readable(path, file, parent_of(path), trouble, &wide))
				{
					refused.push_back(key + " - " + trouble);
					continue;
				}

				if (have_key(key))
					continue;

				/* The mod's own folder is the name a player knows it by;
				 * outfit.txt's name= overrides it, and a mod that ships more
				 * than one outfit gets the file's name after the folder's so
				 * that its two entries can be told apart in the window. */
				std::string display = leaf_of(HDModsName(mod));

				if (!txt.name.empty())
					display = txt.name;
				else if (count > 1)
					display += " - " + display_of(file);

				const size_t before = entries.size();

				add_one(path, file, HD_WARDROBE_SOURCE_MOD, mod, key, display);
				resolve_properties(entries[before], txt);

				if (wide == 30)
					note("HDLOG wardrobe %s has 30 byte vertices - its texture "
						"coordinates are rounded onto the byte grid",
						key.c_str());
			}
		}
	}

	void scan()
	{
		scanned = true;
		entries.clear();
		refused.clear();

		used46 = !want46.empty() ? want46 : HDInstallRoot();
		used13 = !want13.empty() ? want13 : HDInstallRootTr3();

		/* 4\ITEM BEFORE 5\ITEM, which is the other way round from the model
		 * resolver, and on purpose: every outfit of the IV-VI bundle lives in
		 * 4\ITEM and 5\ITEM has none at all (measured: 21 upper case plus the
		 * three the remaster spells in lower case, against 0). 5 is asked all
		 * the same, because a name that turns up there one day is a TR5 outfit
		 * and belongs at the head of the list. */
		if (!used46.empty())
		{
			add_from_install(used46, 4, HD_WARDROBE_SOURCE_46);
			add_from_install(used46, 5, HD_WARDROBE_SOURCE_46);
		}

		if (!used13.empty())
			add_from_install(used13, 1, HD_WARDROBE_SOURCE_13);

		// The two installs' entries carry no mod.txt, so their properties are
		// resolved in one pass here; a mod's are resolved as it is added,
		// because its outfit.txt is read once per mod.
		ModTxt none;

		for (size_t i = 0; i < entries.size(); i++)
			resolve_properties(entries[i], none);

		add_from_mods();

		int mine = 0, guest = 0, modded = 0;

		for (size_t i = 0; i < entries.size(); i++)
			switch (entries[i].out.source)
			{
			case HD_WARDROBE_SOURCE_46: mine++; break;
			case HD_WARDROBE_SOURCE_13: guest++; break;
			default: modded++; break;
			}

		note("HDLOG wardrobe scan: %d entries - %d from the IV-VI install, "
			"%d from the I-III install, %d from mods; %d files passed over",
			(int)entries.size(), mine, guest, modded, (int)refused.size());
	}

	void need_scan()
	{
		if (!scanned)
			scan();
	}
}

/* ----------------------------------------------------------- the calls -- */

void HDWardrobeFolders(const char* root46, const char* root13)
{
	want46 = root46 ? root46 : "";
	want13 = root13 ? root13 : "";
}

void HDWardrobeScan()
{
	need_scan();
}

void HDWardrobeForget()
{
	scanned = false;
	entries.clear();
	refused.clear();
	notes.clear();
	notes_flushed = 0;
}

int HDWardrobeCount()
{
	need_scan();
	return (int)entries.size();
}

const HDWardrobeEntry* HDWardrobeAt(int index)
{
	need_scan();

	return index >= 0 && index < (int)entries.size() ? &entries[(size_t)index].out : 0;
}

int HDWardrobeFind(const char* key)
{
	need_scan();

	if (!key || !*key)
		return -1;

	const std::string want = lower(key);

	for (size_t i = 0; i < entries.size(); i++)
		if (entries[i].sort == want)
			return (int)i;

	return -1;
}

int HDWardrobeFindFile(const char* file, int source)
{
	need_scan();

	if (!file || !*file)
		return -1;

	const std::string want = upper(file);

	for (size_t i = 0; i < entries.size(); i++)
		if (entries[i].out.source == source && want == entries[i].out.file)
			return (int)i;

	return -1;
}

const char* HDWardrobePath(int index)
{
	need_scan();

	return index >= 0 && index < (int)entries.size()
		? entries[(size_t)index].path.c_str() : "";
}

int HDWardrobeRefusedCount()
{
	need_scan();
	return (int)refused.size();
}

const char* HDWardrobeRefused(int at)
{
	need_scan();

	return at >= 0 && at < (int)refused.size() ? refused[(size_t)at].c_str() : "";
}

bool HDWardrobeSkinMeasured(int index, int page)
{
	need_scan();

	if (index < 0 || index >= (int)entries.size() || page <= 0)
		return false;

	HDWardrobeEntry& o = entries[(size_t)index].out;

	if (o.fromSkin != HD_WARDROBE_FROM_DEFAULT)
		return false;

	o.skin[0] = page;
	o.skin[1] = 0;
	o.skin[2] = 0;
	o.fromSkin = HD_WARDROBE_FROM_MEASURED;

	return true;
}

void HDWardrobeLogSink(void (*sink)(const char* line))
{
	note_sink = sink;

	if (!sink)
		return;

	while (notes_flushed < notes.size())
		sink(notes[notes_flushed++].c_str());
}

int HDWardrobeNoteCount()
{
	return (int)notes.size();
}

const char* HDWardrobeNote(int at)
{
	return at >= 0 && at < (int)notes.size() ? notes[(size_t)at].c_str() : "";
}
