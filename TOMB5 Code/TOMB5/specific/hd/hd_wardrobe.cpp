/*
 * Modified 2026 by ChrisChanSan.
 *
 * The wardrobe: scans the installs and the active mods for Lara's outfit
 * bodies and resolves what each one takes of the rest of her. Shared by the
 * engine, ModelProbe, Randomize.exe and the GUI; see hd_wardrobe.h.
 *
 * The eight rows of the table below are this project's own measurements; the
 * composite they describe is carried over from TROne (Copyright 2026
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
#include "hd_wardrobe.h"

#include "hd_install.h"
#include "hd_install_names.h"	// the root's code page, the leaves' test
#include "hd_mods.h"
#include "hd_trm.h"

#include <algorithm>
#include <chrono>
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
	// Wall clock, for the scan's own timing line.
	double clock_ms()
	{
		return std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	/* ------------------------------------------------- the override table -- */

	/* The measured properties of particular files.
	 *
	 * Every column of these eight rows is a measurement on the install's copy
	 * of the file or a judgement made over its picture. They are keyed by
	 * FILE NAME, so a mod that replaces the name inherits the row: a mod that
	 * replaces OUTFIT_CLASSIC is nearly always a reskin of the Rome body and
	 * wants exactly these answers, and where it is not, its outfit.txt wins
	 * (see resolve_properties below).
	 *
	 * `hands`, `braid` and `holsters` say whether the body takes the rest of
	 * the wardrobe. The diving suit takes none of it: its own nodes 10 and 13
	 * carry 1352 vertices each - the gauntlets are the suit - it wears a
	 * helmet instead of a braid, and DrawLara__5 draws no holsters at all.
	 *
	 * `gloves` would pick the HAND_GLOVES_ file over the HAND_BARE_ one (an
	 * outfit model carries only the sleeve on its two hand nodes; the hand is
	 * the separate HAND_ model). The draw ignores it: the bare hands are worn
	 * in every level with every outfit, so hd_lara.cpp's takes_gloves answers
	 * false for everything. OUTFIT_RUSSIA's row still says true because the
	 * outfit is gloved, and the flag is still parsed, printed and logged so
	 * that a wardrobe line reports what was measured. takes_gloves is the one
	 * line that would switch gloves back on.
	 *
	 * `wearsRig` is the separate holster rig - the belt, the two pouches and
	 * the two thigh straps cut out of the London catsuit by page. Triangles on
	 * nodes 1 and 4 by texture page - does the body carry a rig of its own?
	 *
	 *     OUTFIT_CLASSIC      8017: 1090/1090   + 8018: 56/56    yes
	 *     OUTFIT_RUSSIA       8017: 1106/1114   + 8018: 56/56    yes
	 *     OUTFIT_TR3_COAST    8205:  354/354                     yes
	 *     OUTFIT_TR3_CATSUIT  8205:  354/354                     yes
	 *     OUTFIT_VCI          -                                  no
	 *     OUTFIT_TR3_ANTARC   -                                  no
	 *     OUTFIT_TR3_NEVADA   -                                  no
	 *     OUTFIT_SUBSUIT      -                          holsters none anyway
	 *
	 *   "No rig of its own, so wear the London one" is wrong on the picture:
	 *   on the Antarctica parka the belt disappears inside the padded jacket,
	 *   the pouches stand off the hip at the width of the catsuit's thin
	 *   thighs, and the straps cut through the coat; Nevada is the same body
	 *   shape. OUTFIT_VCI has no rig because vanilla TR5 hangs pistols on that
	 *   catsuit and the remaster left the pouches to the holster file; the
	 *   other two never wore a thigh rig in TR3. Only the first is a hole. So
	 *   this is a table and not a rule derived from geometry, and an entry the
	 *   scan finds gets `false` until somebody has looked at its picture.
	 *
	 * `sheen` and `sheenPower` are the body's Blinn-Phong strength and
	 * exponent. TR5's own lighting has no specular term, so every HD outfit
	 * would come out matt. The pages' alpha channel carries no mask (DdsProbe
	 * -alpha over the wardrobe's pages: mean 254.6..255.0, not one texel below
	 * 128), so the material is named:
	 *
	 *   latex     0.45 / 26   the two catsuits, the reference look
	 *   rubber    0.30 / 22   the diving suit
	 *   cloth     0.10 / 14   the Rome top and the rest
	 *
	 * `skin` names the pages of the body that are Lara's own skin, so that a
	 * bare forearm and the hand model over it shine with one material instead
	 * of two. Measured with ModelProbe -handlight and -neck: 8001 is her skin
	 * in the three game 5 bodies, and the four Tomb Raider III guests carry
	 * theirs on 8201 (the neck) and 8202 (the arms) - the same two pages in
	 * all four. OUTFIT_SUBSUIT has none on purpose: the diving suit shows no
	 * skin and its throat page 8047 is the suit, so naming it would make the
	 * whole suit matt. That row is also why the measured tier cannot replace
	 * this table - see resolve_properties.
	 *
	 * `feet` is unknown in every row: no barefoot sample set exists yet. */
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
	/*                                                 ^ not read by the draw
	 *                                                   - see above            */
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

	/* The defaults, for a file nobody has looked at. A randomized run puts a
	 * gun in Lara's hand in every level, so a body without holsters would hang
	 * its pistols in the air; and every HD outfit wears the TR5 braid.
	 * `gloves` defaults the other way: a bare hand under a sleeve reads as a
	 * long sleeve, a gloved hand under a bare arm reads as a fault. */
	const bool kDefaultHands = true;
	const bool kDefaultBraid = true;
	const bool kDefaultHolsters = true;
	const bool kDefaultGloves = false;
	const bool kDefaultRig = false;
	const float kDefaultSheen = 0.10f;
	const float kDefaultSheenPower = 14.0f;

	/* Her skin's default: the `skin` row of the material table in hd_lara.cpp
	 * - a soft wide sheen, never a point - which that file carries as
	 * kSheenSkin and falls back to when there is no scan entry at all. The two
	 * numbers are written in both places on purpose: this file has to answer
	 * without the engine and that one has to draw without a scan. The table
	 * above has no skin-sheen column, so under a list block or an outfit.txt
	 * comes this pair directly. */
	const float kDefaultSkinSheen = 0.12f;
	const float kDefaultSkinSheenPower = 10.0f;

	/* ------------------------------------------------------------ state -- */

	struct Entry
	{
		HDWardrobeEntry out;
		std::string path;
		std::string sort;	// the key, lower cased
		// How many list blocks have named this entry, so that the
		// second one can say in its own line that it is overriding a first.
		int named = 0;
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

	/* The display name out of the file name. "OUTFIT_" comes off, the
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

	/* Which names the scan passes over, and why.
	 *
	 * OUTFIT_YOUNG* - young Lara's bodies. A randomized run hands her a
	 * weapon in every level and a child body does not carry one. It does not
	 * apply to a mod: outfit mods commonly put a grown-up body into the young
	 * slot.
	 *
	 * OUTFIT_HANDS_* and OUTFIT_HAIR - the I-III bundle's names for what the
	 * IV-VI bundle calls HAND_BARE_* and HAIR_OG. They are PARTS of the
	 * composite, not bodies: 15 nodes' worth of Lara is not in them. */
	bool skipped(const std::string& file, int source, const char** why)
	{
		const std::string up = upper(file);

		if (source != HD_WARDROBE_SOURCE_MOD && up.compare(0, 12, "OUTFIT_YOUNG") == 0)
		{
			*why = "young Lara's body carries no weapon, and a randomized run hands her one";
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

	/* What a mod says about its outfit: <mod>\outfit.txt, one key=value per
	 * line, "#" starts a comment - the shape of mod.txt one folder up, so a
	 * modder learns one format. It applies to every OUTFIT_*.TRM the mod
	 * ships.
	 *
	 *   name=Sola's wetsuit      what the wardrobe window calls it; a list
	 *                            block's name= overrides it, as it does every
	 *                            other key
	 *   hands=0|1                does it take the separate HAND_ models
	 *   gloves=0|1               ... the gloved ones. Read and ignored: the
	 *                            wardrobe wears bare hands everywhere
	 *                            (hd_lara.cpp, takes_gloves); still parsed and
	 *                            shown so the report of the file stays whole
	 *   braid=0|1                does it wear the TR5 braid
	 *   holsters=0|1             does it wear thigh holsters
	 *   rig=0|1                  ... and the pouches they hang in
	 *   sheen=0.45               the body's specular strength ...
	 *   exponent=26              ... and its Blinn-Phong exponent
	 *   skin_sheen=0.12          and Lara's own skin's - her face, the lid on
	 *   skin_exponent=10         her neck, her bare hands and the pages named
	 *                            by skin= below. 0..2 and 1..128; a value
	 *                            outside that is a note and the default stands
	 *   skin=8001,8201           which of its pages are Lara's own skin
	 *   footsteps=bare|boots     read and logged; no footstep sound uses it yet
	 *   head=tr5|hair|outfit     whose head this body wears - see
	 *                            HD_WARDROBE_HEAD_* in the header. The one
	 *                            override of the origin rule: a mod built out
	 *                            of a Tomb Raider I-III body writes it when
	 *                            its own head is finished and wants keeping
	 *
	 * An unknown key is a note and never a fault, as in mod.txt: a mod
	 * written for a later version has to stay loadable by this one.
	 *
	 * A list file's block takes the same keys, so this struct and take_key
	 * below are shared by both readers and the two cannot drift apart. */
	struct ModTxt
	{
		bool has = false;
		std::string name;
		int hands = -1, gloves = -1, braid = -1, holsters = -1, rig = -1;
		float sheen = -1.0f, exponent = -1.0f;
		float skinSheen = -1.0f, skinExponent = -1.0f;
		bool hasSkin = false;
		int skin[HD_WARDROBE_SKIN] = { 0, 0, 0 };
		int feet = HD_WARDROBE_FEET_UNKNOWN;
		int head = -1;		// -1 is "the file did not say"
		// The keys this one really took, for the line a list block logs.
		std::string said;
	};

	int as_bool(const std::string& value)
	{
		if (value == "1" || value == "yes" || value == "true" || value == "on")
			return 1;

		if (value == "0" || value == "no" || value == "false" || value == "off")
			return 0;

		return -1;
	}

	// The keys a file really took, so that a list block's own log line can say
	// what it changed rather than leaving it to be diffed.
	void took(ModTxt& out, const char* key)
	{
		if (!out.said.empty())
			out.said += " ";

		out.said += key;
	}

	/* A yes or a no a player wrote. As with the numbers below, a value this
	 * build cannot read is a note and the tier below stands, and the key is
	 * not taken, because a block's own line may not name a key whose value
	 * never arrived. Without this check, `braid=maybe` or `hands=2` would be
	 * logged as taken and silently change nothing (as_bool answers -1 and
	 * apply_txt skips it). The vocabulary is narrow on purpose, so the note
	 * spells it out: a player who writes `ja` or `y` learns the words from
	 * the log. */
	bool take_flag(const char* key, const std::string& value,
		const std::string& low, const char* keeps, const std::string& where,
		int* out)
	{
		const int f = as_bool(low);

		if (f < 0)
		{
			note("MODLOG %s: %s=%s is neither yes nor no (1, yes, true, on / "
				"0, no, false, off) - ignored, %s", where.c_str(), key,
				value.c_str(), keeps);
			return false;
		}

		*out = f;
		return true;
	}

	/* A number a player wrote, and the range a picture survives. All four
	 * numeric keys - sheen, exponent, skin_sheen, skin_exponent - come through
	 * here.
	 *
	 * Without the range, `sheen=14 / exponent=0.10` (the two values
	 * transposed, the typo this format invites) draws her top, shorts,
	 * holsters and hair pure white, and `sheen=1e40` draws them solid black;
	 * `sheen=-1` and `exponent=0` would be dropped by apply_txt while the
	 * block's line named them as set. 0..2 and 1..128 hold every value the
	 * known files write: the built-in table tops out at 0.45/26, common outfit
	 * mods write 1/50.
	 *
	 * Out of range is a note and the tier below stands, and the key is not
	 * taken. So is a value that is no number at all: `sheen=shiny` would
	 * otherwise read as 0.0, matt cloth. */
	bool take_number(const char* key, const std::string& value, float lo,
		float hi, const char* range, const char* keeps,
		const std::string& where, float* out)
	{
		const char* text = value.c_str();
		char* end = 0;
		const float f = (float)std::strtod(text, &end);

		if (end == text || *end)
		{
			note("MODLOG %s: %s=%s is not a number - ignored, %s",
				where.c_str(), key, text, keeps);
			return false;
		}

		// Written as a refusal rather than as `f < lo || f > hi` so that a
		// value that is neither - a NaN out of "nan" - is refused as well.
		if (!(f >= lo && f <= hi))
		{
			note("MODLOG %s: %s=%s is outside %s - ignored, %s",
				where.c_str(), key, text, range, keeps);
			return false;
		}

		*out = f;
		return true;
	}

	/* One key of one line. A mod's outfit.txt and a block of a list file take
	 * the same keys - the list file exists so that an outfit without a mod
	 * folder can be given them - so they are parsed here and nowhere else. `where` is the clause
	 * the notes put after MODLOG: "mod3 Outfits\Legend outfit.txt", or
	 * "outfit_1-5.txt, Outfit TR2_Swim". */
	void take_key(const std::string& key, const std::string& value,
		const std::string& where, ModTxt& out)
	{
		const std::string low = lower(value);

		if (key == "name")
		{
			/* What the wardrobe window calls this body. Applied in apply_txt
			 * with every other key, so a list block renames an install's
			 * outfit just as an outfit.txt renames a mod's.
			 *
			 * An empty name is a note and not a name: `name=` with nothing
			 * (or only spaces) after it would leave a row without a title
			 * while the block's line named a value that never arrived. The
			 * name it had stands. */
			if (value.empty())
			{
				note("MODLOG %s: name= says nothing - ignored, the row keeps "
					"the name it had", where.c_str());
				return;
			}

			/* The field is bounded, so a longer name is cut, and the note
			 * says so. It quotes what it kept and not what it was given:
			 * note() cuts at 512 bytes, and echoing a 900 character name in
			 * full would cut off the note's own reason. */
			if (value.size() >= HD_WARDROBE_NAME)
				note("MODLOG %s: name= is %d characters - the window shows the "
					"first %d, \"%s\"", where.c_str(), (int)value.size(),
					HD_WARDROBE_NAME - 1,
					value.substr(0, HD_WARDROBE_NAME - 1).c_str());

			out.name = value;
			took(out, "name");
		}
		else if (key == "hands")
		{
			if (take_flag("hands", value, low, "she keeps the hands she had",
				where, &out.hands))
				took(out, "hands");
		}
		else if (key == "gloves")
		{
			if (take_flag("gloves", value, low, "and it changes nothing here "
				"either", where, &out.gloves))
				took(out, "gloves");

			/* Said rather than passed over in silence, because a modder who
			 * writes the line and sees no gloves is owed the reason. Written
			 * whatever the value was, valid or not. */
			note("MODLOG %s: gloves=%s is ignored - the wardrobe wears "
				"bare hands everywhere",
				where.c_str(), value.c_str());
		}
		else if (key == "braid")
		{
			if (take_flag("braid", value, low, "she keeps the hair she had",
				where, &out.braid))
				took(out, "braid");
		}
		else if (key == "holsters")
		{
			if (take_flag("holsters", value, low,
				"she keeps the holsters she had", where, &out.holsters))
				took(out, "holsters");
		}
		else if (key == "rig")
		{
			if (take_flag("rig", value, low, "she keeps the rig she had",
				where, &out.rig))
				took(out, "rig");
		}
		/* What the cloth is made of, in the same two ranges her skin gets
		 * below: a strength above 2 washes the material white in any light and
		 * an exponent below 1 is not a highlight at all. */
		else if (key == "sheen")
		{
			if (take_number("sheen", value, 0.0f, 2.0f, "0..2",
				"her clothes keep the shine they had", where, &out.sheen))
				took(out, "sheen");
		}
		else if (key == "exponent")
		{
			if (take_number("exponent", value, 1.0f, 128.0f, "1..128",
				"her clothes keep the shine they had", where, &out.exponent))
				took(out, "exponent");
		}
		/* The skin's own glow. Out of range is a note and the tier below
		 * stands - the same rule head= and footsteps= get for a word they do
		 * not know. */
		else if (key == "skin_sheen")
		{
			if (take_number("skin_sheen", value, 0.0f, 2.0f, "0..2",
				"her skin keeps the glow it had", where, &out.skinSheen))
				took(out, "skin_sheen");
		}
		else if (key == "skin_exponent")
		{
			if (take_number("skin_exponent", value, 1.0f, 128.0f, "1..128",
				"her skin keeps the glow it had", where, &out.skinExponent))
				took(out, "skin_exponent");
		}
		else if (key == "head")
		{
			/* An unknown value is a note and the default stands - the same
			 * rule an unknown key gets: a mod written for a later version has
			 * to stay loadable by this one. */
			if (low == "tr5")
				out.head = HD_WARDROBE_HEAD_TR5;
			else if (low == "hair")
				out.head = HD_WARDROBE_HEAD_HAIR;
			else if (low == "outfit" || low == "own")
				out.head = HD_WARDROBE_HEAD_OUTFIT;
			else
			{
				note("MODLOG %s: head=%s is none of tr5, hair or outfit - "
					"ignored", where.c_str(), value.c_str());
				return;
			}

			took(out, "head");
		}
		else if (key == "footsteps")
		{
			if (low == "bare")
				out.feet = HD_WARDROBE_FEET_BARE;
			else if (low == "boots")
				out.feet = HD_WARDROBE_FEET_BOOTS;
			else
			{
				note("MODLOG %s: footsteps=%s is neither bare nor boots - "
					"ignored", where.c_str(), value.c_str());
				return;
			}

			took(out, "footsteps");
		}
		else if (key == "skin")
		{
			/* Which pages are her own skin - parsed first, taken after. If the
			 * key were taken before its value was read, an empty `skin=` (a
			 * list file's commented `# skin=` line, uncommented) would copy an
			 * empty array over the tier below and silently wipe the outfit's
			 * skin pages - on OUTFIT_TR3_CATSUIT the catsuit's material would
			 * then show at her neckline.
			 *
			 * Each piece is read with strtol, not atoi: atoi answers 0 for
			 * "abc", and 0 terminates this array. A piece that is not a whole
			 * page number above zero is a note. If no page survives, the line
			 * is refused and the pages the entry had stand; there is no
			 * spelling for "this body has no skin pages". */
			int page[HD_WARDROBE_SKIN] = { 0, 0, 0 };
			int at = 0;
			std::string one;

			for (size_t i = 0; i <= value.size(); i++)
			{
				if (i != value.size() && value[i] != ',')
				{
					one.push_back(value[i]);
					continue;
				}

				one = trim(one);

				if (!one.empty())
				{
					const char* text = one.c_str();
					char* end = 0;
					const long n = std::strtol(text, &end, 10);

					if (end == text || *end || n <= 0)
						note("MODLOG %s: skin=%s names \"%s\", which is no "
							"page number - ignored", where.c_str(),
							value.c_str(), one.c_str());
					else if (at < HD_WARDROBE_SKIN)
						page[at++] = (int)n;
					else
						note("MODLOG %s: skin=%s names more than %d pages - "
							"\"%s\" and what follows are ignored",
							where.c_str(), value.c_str(), HD_WARDROBE_SKIN,
							one.c_str());
				}

				one.clear();
			}

			if (!at)
			{
				note("MODLOG %s: skin=%s names no page number - ignored, her "
					"skin pages stay as they were", where.c_str(),
					value.c_str());
				return;
			}

			for (int i = 0; i < HD_WARDROBE_SKIN; i++)
				out.skin[i] = page[i];

			out.hasSkin = true;
			took(out, "skin");
		}
		else
			note("MODLOG %s: key \"%s\" is not one this build knows - ignored",
				where.c_str(), key.c_str());
	}

	/* One whole line, however long the player made it. Both readers below use
	 * it, because both hand what they read to take_key. A fixed buffer with
	 * fgets would cut a long line in two and parse its head as a whole
	 * key=value: a `sheen=` of 900 zeros and then `.25` would read as 0.0,
	 * in range, accepted - a matt body out of a line that asked for 0.25.
	 *
	 * A NUL byte ends the line's text and the rest of that line is skipped,
	 * so that a file saved as UTF-16 stays a set of unreadable lines rather
	 * than becoming half-readable. Returns false only when there was nothing
	 * left at all. */
	bool read_line(std::FILE* file, std::string& line)
	{
		bool nul = false;
		int c;

		line.clear();

		while ((c = std::fgetc(file)) != EOF)
		{
			if (c == '\n')
				return true;

			if (!c)
				nul = true;
			else if (!nul)
				line.push_back((char)c);
		}

		return !line.empty();
	}

	void read_outfit_txt(int mod, ModTxt& out)
	{
		const std::string path = join(HDModsPath(mod), "outfit.txt");
		std::FILE* file = std::fopen(path.c_str(), "rb");

		if (!file)
			return;

		/* The clause every note of this file carries. It is passed in because
		 * take_key serves two kinds of file and only the caller knows which
		 * one it is reading. */
		char where[256];

		std::snprintf(where, sizeof(where), "mod%d %s outfit.txt", mod,
			HDModsName(mod));

		out.has = true;

		std::string line;

		while (read_line(file, line))
		{
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
				note("MODLOG %s: line \"%s\" has no = - ignored", where,
					line.c_str());
				continue;
			}

			take_key(lower(trim(line.substr(0, eq))), trim(line.substr(eq + 1)),
				where, out);
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

	/* A file's own answers, laid over what the tiers below had.
	 *
	 * `from` says which tier this is - a mod's outfit.txt or a block of a list
	 * file - and the rule is the same either way: a key the file does not have
	 * changes nothing, so a block of two lines changes two answers and leaves
	 * the rest where the scan, the table and the measurement put them. One
	 * function for both callers, because the rank only holds if every tier
	 * writes the same fields in the same order. */
	void apply_txt(HDWardrobeEntry& o, const ModTxt& txt, int from)
	{
		if (!txt.has)
			return;

		/* The row's title, applied here like every other key so that a list
		 * block's name= overrides a mod's. An empty one never reaches this
		 * point; take_key refuses it, so no file can blank a row.
		 *
		 * The name carries no from-field on purpose: it is itself the label
		 * of the F4 row and the second column of ModelProbe's table, so a
		 * tier word after it would only displace what a player reads. The
		 * block's own log line already names `name` among the keys it set. */
		if (!txt.name.empty())
			put(o.name, HD_WARDROBE_NAME, txt.name);

		if (txt.hands >= 0) { o.hands = txt.hands != 0; o.fromFlags = from; }
		if (txt.braid >= 0) { o.braid = txt.braid != 0; o.fromFlags = from; }
		if (txt.holsters >= 0) { o.holsters = txt.holsters != 0; o.fromFlags = from; }
		if (txt.gloves >= 0) { o.gloves = txt.gloves != 0; o.fromFlags = from; }
		if (txt.rig >= 0) { o.wearsRig = txt.rig != 0; o.fromFlags = from; }

		/* The four numbers. Every test here only asks "did the file say it":
		 * the sentinel is -1, and a value that reaches this point has already
		 * passed take_number's range, so no test below can drop a value the
		 * block's own log line named as taken. */
		if (txt.sheen >= 0.0f) { o.sheen = txt.sheen; o.fromSheen = from; }
		if (txt.exponent > 0.0f) { o.sheenPower = txt.exponent; o.fromSheen = from; }

		// Lara's own skin.
		if (txt.skinSheen >= 0.0f) { o.skinSheen = txt.skinSheen; o.fromSkinSheen = from; }
		if (txt.skinExponent > 0.0f) { o.skinSheenPower = txt.skinExponent; o.fromSkinSheen = from; }

		if (txt.hasSkin)
		{
			for (int i = 0; i < HD_WARDROBE_SKIN; i++)
				o.skin[i] = txt.skin[i];

			o.fromSkin = from;
		}

		if (txt.feet != HD_WARDROBE_FEET_UNKNOWN)
		{
			o.feet = txt.feet;
			o.fromFeet = from;
		}

		if (txt.head >= 0)
		{
			o.head = txt.head;
			o.fromHead = from;
		}
	}

	/* Resolves an entry's properties: defaults, then the head rule, then the
	 * table, then the mod's outfit.txt. List blocks are laid over later.
	 *
	 * The measured tier exists for exactly one property - which of the body's
	 * pages are Lara's own skin - and it is not applied here, because it needs
	 * the .TRM read against a level's own bind pose (hd_lara.cpp's
	 * measure_neck_skin, the -neck machinery: the page whose torso-node
	 * vertices inside the neck cylinder span the greatest depth down the neck
	 * axis). hd_lara hands it back through HDWardrobeSkinMeasured.
	 *
	 * What that rule answers against all eight rows of the table (ModelProbe
	 * -lara ANDREA1 -outfit <x> -nodraw, the "her throat" note):
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
	 * In seven of the eight the rule finds the table's first page exactly.
	 * The disagreements show its limit:
	 *
	 *   - the four Tomb Raider III guests carry a second skin page, 8202,
	 *     their bare arms. A measurement at the neck cannot reach an arm, and
	 *     no measurement can: the same page number is bare skin under the
	 *     Antarctica parka and a latex sleeve under the London catsuit.
	 *     Geometry cannot tell skin from latex.
	 *   - the diving suit has a throat page, 8047, and it is the helmet.
	 *     Naming it would hand the skin material to the whole suit.
	 *
	 * So the measured tier gives a new entry exactly one page - its throat,
	 * the page the first person neck lid wears and a bare neck needs - and
	 * everything beyond that is for the table or a mod's outfit.txt. */
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
		o.skinSheen = kDefaultSkinSheen;
		o.skinSheenPower = kDefaultSkinSheenPower;
		o.skin[0] = o.skin[1] = o.skin[2] = 0;
		o.feet = HD_WARDROBE_FEET_UNKNOWN;

		o.fromFlags = HD_WARDROBE_FROM_DEFAULT;
		o.fromSheen = HD_WARDROBE_FROM_DEFAULT;
		o.fromSkinSheen = HD_WARDROBE_FROM_DEFAULT;
		o.fromSkin = HD_WARDROBE_FROM_DEFAULT;
		o.fromFeet = HD_WARDROBE_FROM_DEFAULT;

		/* The head rule. It asks one question - which bundle was this body
		 * drawn for - of the two things that answer it without a picture: the
		 * source of an install's entry, and mod.txt's origin= line for a mod.
		 * A body of the Tomb Raider I-III bundle carries no hair on its head
		 * node (the measurement is in the header) and therefore takes TR5's
		 * head; everything else keeps its own.
		 *
		 * A mod without a mod.txt keeps its own head, whatever its file is
		 * called. There is no name rule and no geometry rule on purpose:
		 * "OUTFIT_TR2_HOME" is the name of a IV-VI body with a full head and
		 * also the name a mod gives a I-III conversion, and counting vertices
		 * to tell a bald head from a hooded one is an unreliable guess. The
		 * mod says origin= or head=, and the log says which applied. */
		o.head = HD_WARDROBE_HEAD_OUTFIT;
		o.fromHead = HD_WARDROBE_FROM_DEFAULT;

		if (o.source == HD_WARDROBE_SOURCE_13 ||
			(o.source == HD_WARDROBE_SOURCE_MOD && HDModsOrigin(o.mod) > 0))
		{
			o.head = HD_WARDROBE_HEAD_TR5;
			o.fromHead = HD_WARDROBE_FROM_ORIGIN;
		}

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

			/* The diving suit's empty list is a decision and not a gap, so
			 * the table owns that property even when the list is empty - it
			 * is what keeps the measured tier from making the suit matt. */
			o.fromSkin = HD_WARDROBE_FROM_TABLE;
		}

		apply_txt(o, txt, HD_WARDROBE_FROM_MOD_TXT);
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

	/* The .TRM itself decides whether it is listed. Two kinds of file are
	 * refused:
	 *
	 *   - one this reader cannot get past at all. There is nothing to wear.
	 *   - one laid out with thirty byte vertices whose 24 byte twin is in the
	 *     same folder. Both describe the same body; the wide one carries its
	 *     texture coordinates as floats and this engine's vertex holds them
	 *     as bytes, so wearing it would round nearly every coordinate by up
	 *     to half a step of a 256 step axis (measured over
	 *     OUTFIT_TRX_WETSUIT_1_HD: 14,208 of 14,262 coordinates lie off that
	 *     grid, and off every other grid from 1/256 to 1/4096). The twin is
	 *     the same Lara without the rounding. Over both bundles, all 2,923
	 *     .TRM files: exactly thirty are laid out that way and every one is
	 *     an OUTFIT_TRX_*_HD.TRM with its twin beside it, so this rule takes
	 *     nothing off the list that the list has not got twice. A wide file
	 *     with no twin is listed and worn, with the rounding in the log.
	 *
	 * It reads the whole file to look at the header, on purpose. HDTrmProbe
	 * wants only the counts, and the scan puts 28.8 MiB (read_kib=29525)
	 * through it once a session for the 112 headers it looks at. Reading only
	 * a prefix is not worth it:
	 *
	 *   - the header needs everything up to the vertex block, and the index
	 *     list is inside that. For an outfit of 14,000 vertices and 70,000
	 *     indices the prefix is 140 KiB of a 480 KiB file.
	 *   - it would add a second length inside hd_trm's parser: every bounds
	 *     check tests against the file length, and a prefix read would make
	 *     "how much is in the buffer" and "how long is the file" two numbers
	 *     in the reader a downloaded mod's .TRM goes through.
	 *   - the whole scan takes 24 to 36 ms, once per session (HDLOG wardrobe
	 *     scan, ms=). */
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
		/* The root is spelt in the system code page, as the installation
		 * search hands it on, so it is read back in that code page. A UTF-8
		 * reading (u8path) throws on such a spelling as soon as it holds a
		 * letter beyond ASCII - an U with umlaut in a library name - and
		 * nothing above this walk would catch it. */
		const std::string folder = join(game_folder(root, game), "ITEM");
		std::error_code ec;
		fs::directory_iterator it(fs::path(HDNameWide(folder, CP_ACP)), ec),
			end;

		if (ec)
			return;

		std::vector<std::string> leaves;

		for (; it != end; it.increment(ec))
		{
			if (ec)
				break;

			if (!it->is_regular_file(ec) || ec)
				continue;

			// The narrow file calls below need the name in the code page; a
			// name it cannot spell is named among the refused, not thrown on.
			const std::wstring name = it->path().filename().native();
			std::string leaf;

			if (!HDNameFits(name, CP_ACP, leaf))
			{
				refused.push_back(std::string(source == HD_WARDROBE_SOURCE_46
					? "46/" : "13/") + HDNameShown(name) + " - its name has "
					"letters the system code page cannot spell");
				continue;
			}

			leaves.push_back(leaf);
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

			/* The IV-VI bundle's copy wins the name. It ships its own
			 * OUTFIT_TR1_* / TR2_* / TR3_* files with IV-VI page numbers, so
			 * they need no alternate root at all, and they are the better
			 * model: OUTFIT_TR1_CLASSIC has 14,515 vertices there against
			 * 8,382 in the I-III bundle. */
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

				/* The mod's own folder is the name a player knows it by, and a
				 * mod that ships more than one outfit gets the file's name
				 * after the folder's so that its two entries can be told apart
				 * in the window.
				 *
				 * The outfit.txt's own name= is applied in apply_txt with
				 * every other key that file sets, and a list block's name=
				 * lands over it. A mod that names itself is called what it
				 * says, whether it ships one body or seventeen. */
				std::string display = leaf_of(HDModsName(mod));

				if (count > 1)
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

	/* ------------------------------------------------------- the lists -- */

	/* The name a block names, split into what has to match.
	 *
	 * A name may carry a key prefix to pin one source - "13/TRX_Wetsuit_3",
	 * "46/Classic", "mod/outfits/legend/Black_1" - because the same .TRM stem
	 * can be in the wardrobe more than once (a mod may ship an OUTFIT_CLASSIC
	 * beside the install's). Without a prefix the block applies to every
	 * entry with that stem, install and mods alike, so one line about a body
	 * also covers the mod that reskinned it.
	 *
	 * The stem itself is matched the way a player writes it and not the way
	 * the file system spells it: case does not count, "OUTFIT_" may be left
	 * off, and a space is an underscore. So "TR2_Swim", "tr2 swim" and
	 * "OUTFIT_TR2_SWIM" are one name. */
	void split_name(const std::string& name, std::string& prefix, std::string& stem)
	{
		// lower() turns "\" into "/" as well, so a key typed with backslashes
		// lands on the same entry as one typed with forward ones.
		const std::string low = lower(name);
		const size_t cut = low.find_last_of('/');
		std::string rest = name;

		prefix.clear();

		if (cut != std::string::npos)
		{
			prefix = low.substr(0, cut + 1);
			rest = name.substr(cut + 1);
		}

		std::string up = upper(trim(rest));

		for (size_t i = 0; i < up.size(); i++)
			if (up[i] == ' ')
				up[i] = '_';

		if (up.compare(0, 7, "OUTFIT_") != 0)
			up = "OUTFIT_" + up;

		stem = up;
	}

	/* One block, applied. Every entry the name reaches takes it, and the log
	 * line says which entries they were and what the block set on them, so an
	 * unexpected picture is one grep away from the line that made it.
	 *
	 * A block that reaches nothing is a note and not a fault: a list written
	 * on a machine with both bundles installed may be read on one with a
	 * single bundle, and the names that are not there must not refuse the
	 * whole file. */
	bool apply_block(const std::string& leaf, const std::string& name,
		const std::string& prefix, const std::string& stem, const ModTxt& txt)
	{
		std::string hit;
		int matched = 0;
		int again = 0;

		for (size_t i = 0; i < entries.size(); i++)
		{
			if (stem != entries[i].out.file)
				continue;

			if (!prefix.empty() &&
				entries[i].sort.compare(0, prefix.size(), prefix) != 0)
				continue;

			apply_txt(entries[i].out, txt, HD_WARDROBE_FROM_LIST);
			matched++;

			if (entries[i].named++)
				again++;

			// The key list is bounded like every other note in this file; the
			// count stands whether or not the names fit.
			if (hit.size() < 300)
				hit += std::string(hit.empty() ? "" : ", ") + entries[i].out.key;
			else if (hit[hit.size() - 1] != '.')
				hit += ", ...";
		}

		if (!matched)
		{
			note("HDLOG wardrobe list %s: Outfit %s -> no such outfit",
				leaf.c_str(), name.c_str());
			return false;
		}

		note("HDLOG wardrobe list %s: Outfit %s -> %s: %s%s", leaf.c_str(),
			name.c_str(), hit.c_str(),
			txt.said.empty() ? "nothing - the block has no key this build knows"
				: txt.said.c_str(),
			again ? " (an earlier block named it too, and this one wins for "
				"these keys)" : "");
		return true;
	}

	/* A block ends, and the file's tally moves with it. Three things close a
	 * block - an empty line, the next "Outfit:" line and the end of the file -
	 * so the counting sits here and not at three call sites.
	 *
	 * It counts what apply_block returned, never the text of its notes:
	 * note() cuts at 512 bytes, so a note about a very long block name loses
	 * its "-> no such outfit" tail and a text search would miscount it. */
	void close_block(const std::string& leaf, const std::string& name,
		const std::string& prefix, const std::string& stem, ModTxt& open,
		int* hit, int* gone)
	{
		if (!open.has)
			return;

		if (apply_block(leaf, name, prefix, stem, open))
			(*hit)++;
		else
			(*gone)++;

		open = ModTxt();
	}

	/* A list file, read line by line:
	 *
	 *   "#..."            a comment; it closes nothing, so a comment under an
	 *                     "Outfit:" line stays inside the block
	 *   "Outfit: <name>"  opens a block and closes the one before it
	 *   "key=value"       belongs to the open block
	 *   an empty line     closes the block
	 *
	 * A key outside a block, a block with no name and a line with no "=" are
	 * each a note and the file goes on being read: one bad line may not cost
	 * the other blocks. */
	void read_list(const std::string& path, int* blocks, int* hit, int* gone)
	{
		std::FILE* file = std::fopen(path.c_str(), "rb");
		const std::string leaf = leaf_of(path);

		if (!file)
		{
			note("HDLOG wardrobe list %s: cannot be opened - ignored",
				path.c_str());
			return;
		}

		ModTxt open;
		std::string name, prefix, stem, line;
		char where[256];
		bool first = true;

		while (read_line(file, line))
		{
			/* A byte order mark off the front of the first line. A player
			 * writes this file in an editor, and Windows Notepad's "UTF-8"
			 * puts three bytes there; on an "Outfit:" line they would cost
			 * the block for a reason invisible in the file. */
			if (first)
			{
				first = false;

				if (line.size() >= 3 && (unsigned char)line[0] == 0xEF &&
					(unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
					line.erase(0, 3);
			}

			while (!line.empty() && (line[line.size() - 1] == '\n' ||
				line[line.size() - 1] == '\r'))
				line.erase(line.size() - 1);

			const size_t hash = line.find('#');
			/* A whole-line comment is not an empty line. Cutting the comment
			 * off first and then testing for empty would let a comment under
			 * an "Outfit:" line close the block it belongs to, and every key
			 * after it would stand outside one. */
			const bool comment = hash != std::string::npos &&
				trim(line.substr(0, hash)).empty();

			if (hash != std::string::npos)
				line.erase(hash);

			line = trim(line);

			if (comment)
				continue;

			if (line.empty())
			{
				close_block(leaf, name, prefix, stem, open, hit, gone);
				continue;
			}

			if (line.size() >= 7 && lower(line).compare(0, 7, "outfit:") == 0)
			{
				close_block(leaf, name, prefix, stem, open, hit, gone);
				name = trim(line.substr(7));

				if (name.empty())
				{
					note("HDLOG wardrobe list %s: an \"Outfit:\" line with no "
						"name - the block under it is ignored", leaf.c_str());
					continue;
				}

				split_name(name, prefix, stem);
				open.has = true;
				(*blocks)++;
				std::snprintf(where, sizeof(where), "%s, Outfit %s",
					leaf.c_str(), name.c_str());
				continue;
			}

			const size_t eq = line.find('=');

			if (eq == std::string::npos)
			{
				note("HDLOG wardrobe list %s: line \"%s\" has no = - ignored",
					leaf.c_str(), line.c_str());
				continue;
			}

			if (!open.has)
			{
				note("HDLOG wardrobe list %s: \"%s\" stands outside a block - "
					"ignored; a key belongs under an \"Outfit:\" line",
					leaf.c_str(), line.c_str());
				continue;
			}

			take_key(lower(trim(line.substr(0, eq))), trim(line.substr(eq + 1)),
				where, open);
		}

		close_block(leaf, name, prefix, stem, open, hit, gone);
		std::fclose(file);
	}

	/* Every list, in path order, after everything else. After, because this
	 * is the top tier: an entry has to have its mod's outfit.txt, its table
	 * row and its defaults on it before a block may overwrite any of them. In
	 * path order - hd_mods sorts them by their lower-cased full path, so the
	 * folder decides before the file name does - because two files may name
	 * one outfit and the later one wins. */
	void apply_lists(int* files, int* blocks, int* matched, int* missed)
	{
		*files = HDModsListCount();
		*blocks = 0;
		*matched = 0;
		*missed = 0;

		for (int i = 0; i < *files; i++)
		{
			const std::string path = HDModsListPath(i);
			int mine = 0, hit = 0, gone = 0;

			/* The file's own line counts the blocks it opened and, of those,
			 * the ones that reached an entry - counted by close_block from
			 * what apply_block returned. */
			read_list(path, &mine, &hit, &gone);

			note("HDLOG wardrobe list %s: %d blocks, %d matched, %d unmatched",
				leaf_of(path).c_str(), mine, hit, gone);

			*blocks += mine;
			*matched += hit;
			*missed += gone;
		}
	}

	void scan()
	{
		/* What the scan costs, as counters in its log line: the files it
		 * opened, the bytes it read and the models it loaded with geometry.
		 * The last is zero - the scan probes headers only, and the geometry
		 * of the one outfit that is worn is read by hd_model's own pass one.
		 * The scan runs once per session; a second level build finds
		 * `scanned` set and adds none of it. */
		double opens0 = 0, loads0 = 0, bytes0 = 0;

		HDTrmCounters(&opens0, &loads0, &bytes0);

		const double begun = clock_ms();

		scanned = true;
		entries.clear();
		refused.clear();

		used46 = !want46.empty() ? want46 : HDInstallRoot();
		used13 = !want13.empty() ? want13 : HDInstallRootTr3();

		/* 4\ITEM before 5\ITEM, the other way round from the model resolver:
		 * every outfit of the IV-VI bundle lives in 4\ITEM and 5\ITEM has none
		 * (21 upper case plus three spelled in lower case, against 0). 5 is
		 * asked all the same, because a name that turns up there is a TR5
		 * outfit. */
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

		/* The lists last, because they are the top tier: a block
		 * may only overwrite what the scan, a mod's outfit.txt, the table and
		 * the defaults have already put on an entry. */
		int listFiles = 0, listBlocks = 0, listHit = 0, listMissed = 0;

		apply_lists(&listFiles, &listBlocks, &listHit, &listMissed);

		int mine = 0, guest = 0, modded = 0;
		int tr5Head = 0, hairHead = 0, ownHead = 0;

		for (size_t i = 0; i < entries.size(); i++)
		{
			switch (entries[i].out.source)
			{
			case HD_WARDROBE_SOURCE_46: mine++; break;
			case HD_WARDROBE_SOURCE_13: guest++; break;
			default: modded++; break;
			}

			// The head rule tallied over the whole list, for the scan line.
			switch (entries[i].out.head)
			{
			case HD_WARDROBE_HEAD_TR5: tr5Head++; break;
			case HD_WARDROBE_HEAD_HAIR: hairHead++; break;
			default: ownHead++; break;
			}
		}

		double opens1 = 0, loads1 = 0, bytes1 = 0;

		HDTrmCounters(&opens1, &loads1, &bytes1);

		note("HDLOG wardrobe scan: %d entries - %d from the IV-VI install, "
			"%d from the I-III install, %d from mods; %d files passed over; "
			"lists=%d blocks=%d matched=%d unmatched=%d; "
			"heads tr5=%d hair=%d outfit=%d; "
			"ms=%d probed=%d read_kib=%.0f geometry=%d",
			(int)entries.size(), mine, guest, modded, (int)refused.size(),
			listFiles, listBlocks, listHit, listMissed,
			tr5Head, hairHead, ownHead,
			(int)(clock_ms() - begun + 0.5), (int)(opens1 - opens0),
			(bytes1 - bytes0) / 1024.0, (int)(loads1 - loads0));
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
	// The lists are hd_mods' own list and are dropped with its scan; nothing
	// of them is kept here past the entries they were applied to.
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

const char* HDWardrobeHeadWord(int head)
{
	switch (head)
	{
	case HD_WARDROBE_HEAD_TR5: return "tr5";
	case HD_WARDROBE_HEAD_HAIR: return "hair";
	default: return "outfit";
	}
}

const char* HDWardrobeHeadPhrase(int head)
{
	switch (head)
	{
	case HD_WARDROBE_HEAD_TR5: return "TR5 head";
	case HD_WARDROBE_HEAD_HAIR: return "TR5 hair";
	default: return "own head";
	}
}

const char* HDWardrobeFromWord(int from)
{
	switch (from)
	{
	case HD_WARDROBE_FROM_MEASURED: return "measured";
	case HD_WARDROBE_FROM_TABLE: return "the table";
	case HD_WARDROBE_FROM_MOD_TXT: return "outfit.txt";
	case HD_WARDROBE_FROM_ORIGIN: return "origin";
	case HD_WARDROBE_FROM_LIST: return "the list";
	default: return "the default";
	}
}

/* The one-word form, for a column of a table or a field of a log line.
 *
 * A switch and not an array on purpose: an array indexed by the from-number
 * reads past its end when a tier is added and the array is not. A tier added
 * here is a word here, and any other number answers "?". */
const char* HDWardrobeFromTag(int from)
{
	switch (from)
	{
	case HD_WARDROBE_FROM_DEFAULT: return "default";
	case HD_WARDROBE_FROM_MEASURED: return "measured";
	case HD_WARDROBE_FROM_TABLE: return "table";
	case HD_WARDROBE_FROM_MOD_TXT: return "outfit.txt";
	case HD_WARDROBE_FROM_ORIGIN: return "origin";
	case HD_WARDROBE_FROM_LIST: return "list";
	default: return "?";
	}
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
