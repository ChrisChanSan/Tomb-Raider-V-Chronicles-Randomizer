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
#include "../../tomb5/pch.h"
#include "hd_feet.h"
#include "../hqsfx.h"
#include "../dxsound.h"
#include "../function_stubs.h"
#include "../../game/sound.h"
#include "hd_level.h"			// HDEnabled - the gate this all lives in

namespace
{
	/* The two sound ids this file owns, by the names game\sound.h gives them.
	 * They are not written out as numbers anywhere below, because a number in
	 * the source is a number nobody can grep for. */
	const int kStepsId = SFX_LARA_FEET;
	const int kLandId = SFX_LARA_LAND;

	// At most this many recordings in a set - the bank's own barefoot entry
	// has five and a SAMPLE_INFO's count field holds fifteen.
	const int kMaxTakes = 15;

	short* g_lut;			// the level's own tables, game_malloc'ed
	SAMPLE_INFO* g_infos;
	int g_infoCount;		// how many entries the level file carried

	short g_ownSteps = -1;		// what sample_lut held before this ran
	short g_ownLand = -1;
	short g_bareSteps = -1;		// the spare infos, or -1
	short g_bareLand = -1;

	int g_takes;
	int g_landTakes;
	bool g_bare;
	bool g_saidQuiet;		// the floor layer's own line, once per bare spell
	char g_source[160];
	char g_whyNot[160];

	/* One recording into one DS_Buffers slot. The payload has been through
	 * SfxRiffCheck, so it is mono 16 bit PCM with a data chunk inside the
	 * buffer and there is nothing left here to judge - that is the contract
	 * in hqsfx.h, and it is the same one LoadSamples rests on. */
	bool take_into(int slot, const char* riff, const SfxRiffParts& parts)
	{
		if (!riff || slot < 0 || slot >= MAX_SAMPLES)
			return false;

		return DXCreateSample(slot, (LPWAVEFORMATEX)(riff + parts.fmtAt),
			(LPVOID)(riff + parts.dataAt), (ulong)parts.dataBytes) != 0;
	}

	/* A spare SAMPLE_INFO, out of the level's own entry for that id.
	 *
	 * Everything about how a footstep is played stays the level's - the
	 * volume, the hearing radius, the volume randomness and the play mode -
	 * because those describe the game and not the recording. Two fields do
	 * not: the sample number and the count are what was really loaded, and
	 * the pitch is handed in, because the bank ships its barefoot entry at 0
	 * where the footstep entry is 12 (measured, and every bank checked
	 * agrees). The remaster turned the random pitch off for those five
	 * recordings and that is a statement about them. */
	void fill_info(SAMPLE_INFO& out, const SAMPLE_INFO& from, int first,
		int takes, int pitch)
	{
		out = from;
		out.number = (short)first;
		out.pitch = (char)pitch;
		out.flags = (short)((from.flags & ~0x3C) | ((takes & 0xF) << 2));
	}

	void no_set(const char* why)
	{
		snprintf(g_whyNot, sizeof(g_whyNot), "%s", why);
		Log("SFXLOG footsteps: no bare set - %s", g_whyNot);
	}
}

void HDFeetLevelEnd()
{
	g_lut = 0;
	g_infos = 0;
	g_infoCount = 0;
	g_ownSteps = -1;
	g_ownLand = -1;
	g_bareSteps = -1;
	g_bareLand = -1;
	g_takes = 0;
	g_landTakes = 0;
	g_bare = false;
	g_saidQuiet = false;
	g_source[0] = 0;
	g_whyNot[0] = 0;
}

void HDFeetLevelBuild(short* lut, SAMPLE_INFO* infos, int infoCount, int spare,
	int next)
{
	int loaded = 0;
	int landLoaded = 0;
	char why[160];

	HDFeetLevelEnd();

	/* The HD gate: everything this file adds is behind it, so a player with
	 * hd_build 0 hears exactly what he heard before - a sound mod's own
	 * footsteps included. Not the draw: the set is loaded into the sample
	 * memory at the level's load and stays whatever the HD/SD switch draws
	 * (hd_switch.h). Silent: an unasked-for line every level for a
	 * feature that is switched off is noise, and the option's own state is
	 * in the log already. */
	if (!HDEnabled())
		return;

	if (!lut || !infos || infoCount <= 0 || spare < 2)
	{
		no_set("this level's sound map has no room for a second entry");
		return;
	}

	g_lut = lut;
	g_infos = infos;
	g_infoCount = infoCount;
	g_ownSteps = lut[kStepsId];
	g_ownLand = lut[kLandId];

	if (g_ownSteps < 0 || g_ownSteps >= infoCount)
	{
		no_set("this level has no footstep sound at all");
		return;
	}

	if (!HQSfxHaveBank())
	{
		no_set("there is no sound bank to take a second set from");
		return;
	}

	const SAMPLE_INFO own = infos[g_ownSteps];
	int ownTakes = (own.flags >> 2) & 0xF;

	if (ownTakes < 1)
		ownTakes = 1;

	if (ownTakes > kMaxTakes)
		ownTakes = kMaxTakes;

	/* The slot room, checked before a single sample is decoded. MAX_SAMPLES is
	 * 450 and the fullest classic level (RICH2) carries 148, so five more fit
	 * with 297 to spare; a randomized level with all four Tomb Raider III
	 * guests adds 21 and still fits. This is here for the level that does
	 * not, and it is the one thing that can take the toggle away from a
	 * player who has everything else. */
	if (next < 0 || next + ownTakes + 1 > MAX_SAMPLES)
	{
		snprintf(why, sizeof(why),
			"this level's %d samples leave no room for a second set (the "
			"engine holds %d)", next, MAX_SAMPLES);
		no_set(why);
		return;
	}

	/* Where the bare recordings come from, in priority order:
	 *
	 * 1. What the ordinary chain plays for sound id 0 - a mod's bank, or a
	 *    mod's loose .wav over it - but only when a mod's bank really is the
	 *    one being played and that bank really replaces sound id 0. A sound
	 *    mod that replaces the footsteps is a barefoot mod; this is where it
	 *    belongs, and it is the only place it is still heard now that the
	 *    boots take the unmodded bank.
	 *
	 * 2. Otherwise the remaster's own barefoot set: sound id HQ_SFX_BARE_FEET
	 *    of the bank, which no level carries - so it is reached by bank sample
	 *    number and not through the level bridge every other sound uses.
	 *
	 * "Really replaces" needs its own check because `HQSfxFromMod()` only says
	 * that a mod's bank is being played, and most sound mods are not about
	 * footsteps at all - a door mod, a weapon mod, a voice mod. Such a bank
	 * carries the install's own id 0 byte for byte, so taking the bare set on
	 * that signal alone would hand the boots' own recordings back under the
	 * name "bare" while the toggle did nothing. The two payloads are compared
	 * here instead. The unmodded bank is the ruler and reading both at once is
	 * safe - each bank owns its own read buffer (specific\sfxbank.cpp,
	 * SfxBankSample).
	 *
	 * The comparison reads the ordinary chain (bank, then the loose overlay
	 * over it), not a mod's bank on its own: a footstep mod that ships loose
	 * 0000..0003.wav and no bank of its own would otherwise reach neither set,
	 * since the boots take the unmodded bank by design and a bank-only check
	 * never looks at the loose file. Reading the chain answers both kinds with
	 * one rule. The loose .wav is exactly the file such a mod ships: SfxProbe's
	 * own line reads "a mod's <NNNN>.wav replaces BANK sample NNNN", and
	 * 0000..0003 and 0010 are the boots. */
	bool useMod = false;
	const char* modWhy = "";
	/* A copy and not the pointer: HQSfxWavSource hands back a buffer of its own
	 * that the next call on it overwrites, and this name is read again further
	 * down, after the recordings have been loaded through that same file. */
	char modWho[160];

	{
		long modBytes = 0, plainBytes = 0;
		SfxRiffParts mp, pp;
		/* The chain first, and the order cannot change the verdict: with no mod
		 * bank both answers come out of the same read buffer, and there they are
		 * the same bytes by construction. Where they differ - a mod's bank, or a
		 * loose .wav, which is a buffer of its own - there is no sharing. */
		const char* chain = HQSfxPayload(own.number, 0, &modBytes, &mp);
		const char* plain = HQSfxPayloadBoots(own.number, 0, &plainBytes, &pp);
		/* Which kind of mod it is, for the log alone - the rule above has
		 * already decided. A loose .wav wins inside the chain, so when there is
		 * one it is the name to print even if a mod's bank is played as well. */
		const char* wav = HQSfxWavSource(own.number);
		const bool overlaid = *wav != 0;

		snprintf(modWho, sizeof(modWho), "%s", overlaid ? wav : HQSfxSource());

		if (!chain)
			modWhy = "the chain has no sound id 0 at all";
		else if (!plain)
		{
			useMod = true;
			modWhy = "there is no unmodded bank to compare it against";
		}
		else if (modBytes != plainBytes ||
			memcmp(chain, plain, (size_t)modBytes))
		{
			useMod = true;
			modWhy = overlaid ? "its own loose .wav stands on sound id 0"
				: "its own recordings stand on sound id 0";
		}
		else
			modWhy = "it carries the install's own sound id 0, byte for byte";

		// Said only where there is a mod to say it of. With none, the chain and
		// the ruler are the same bank and the answer is not news.
		if (HQSfxFromMod() || overlaid)
			Log("SFXLOG footsteps: %s %s - %s", modWho,
				useMod ? "supplies the bare set" : "is not a footstep mod",
				modWhy);

		/* And what the boots do with it, said once per level where the answer
		 * is decided. The two ids are loaded through the unmodded bank whatever
		 * a mod says, so a mod that really does replace the footsteps is
		 * bypassed for as long as the outfit wears boots - it is the bare half
		 * it supplies. Without this line a player who installs a footstep mod
		 * and hears the install's boots has nothing in the log to read. */
		if (useMod)
			Log("SFXLOG footsteps: %s's own sound ids %d and %d are bypassed "
				"while the outfit wears boots - the boots are %s's, and the "
				"mod supplies the bare set instead", modWho, kStepsId, kLandId,
				*HQSfxPlainSource() ? HQSfxPlainSource() : "the level's own");
	}

	const char* where = useMod ? modWho : HQSfxPlainSource();
	int pitch = own.pitch;

	if (useMod)
	{
		for (int i = 0; i < ownTakes; i++)
		{
			long bytes = 0;
			SfxRiffParts parts;
			char* riff = HQSfxPayload(own.number + i, 0, &bytes, &parts);

			if (!riff || !take_into(next + loaded, riff, parts))
				break;

			loaded++;
		}

		/* And its landing, when it brought one: a landing recording carries
		 * several times the RMS of the matching footstep (4.4x measured on a
		 * mod that supplies one), which is what marks sound id 4 as one. */
		if (loaded && g_ownLand >= 0 && g_ownLand < infoCount)
		{
			long bytes = 0;
			SfxRiffParts parts;
			char* riff = HQSfxPayload(infos[g_ownLand].number, 0, &bytes, &parts);

			if (riff && take_into(next + loaded, riff, parts))
			{
				landLoaded = 1;
				loaded++;
			}
		}
	}
	else
	{
		long banked[kMaxTakes];
		const int have = HQSfxBankTakes(HQ_SFX_BARE_FEET, banked, kMaxTakes);

		if (!have)
		{
			snprintf(why, sizeof(why),
				"%s has no sound id %d - the remaster's barefoot recordings "
				"are not in it", *where ? where : "the bank", HQ_SFX_BARE_FEET);
			no_set(why);
			return;
		}

		/* The bank's own pitch for those five, read off the file rather than
		 * written down here: 0, where the footstep entry beside it is 12. */
		pitch = 0;

		for (int i = 0; i < have && next + loaded + 1 < MAX_SAMPLES; i++)
		{
			long bytes = 0;
			SfxRiffParts parts;
			char* riff = HQSfxBankSample(banked[i], &bytes, &parts);

			if (riff && take_into(next + loaded, riff, parts))
				loaded++;
		}
	}

	if (!loaded)
	{
		no_set("none of the barefoot recordings could be read");
		return;
	}

	g_takes = loaded - landLoaded;
	g_bareSteps = (short)infoCount;
	fill_info(infos[g_bareSteps], own, next, g_takes, pitch);

	if (landLoaded)
	{
		g_bareLand = (short)(infoCount + 1);
		fill_info(infos[g_bareLand], infos[g_ownLand], next + g_takes, 1,
			infos[g_ownLand].pitch);
	}
	else if (g_ownLand >= 0 && g_ownLand < infoCount)
	{
		/* The landing draws from the same recordings: none of the bank's
		 * five barefoot takes stands out as a landing (hd_feet.h). So sound
		 * id 4 gets the same takes as sound id 0, with the level's own id 4
		 * volume and radius - the boots' thud does not fit a bare foot. */
		g_bareLand = (short)(infoCount + 1);
		fill_info(infos[g_bareLand], infos[g_ownLand], next, g_takes, pitch);
	}

	g_landTakes = landLoaded ? 1 : (g_bareLand >= 0 ? g_takes : 0);

	snprintf(g_source, sizeof(g_source), "%s", *where ? where : "the bank");

	Log("SFXLOG footsteps: boots = this level's own ids %d and %d through %s; "
		"bare = %d step%s and %s, from %s, in slots %d..%d, infos %d and %d "
		"(volume %d radius %d pitch %d -> %d)",
		kStepsId, kLandId,
		*HQSfxPlainSource() ? HQSfxPlainSource() : "the level's own samples",
		g_takes, g_takes == 1 ? "" : "s",
		landLoaded ? "a landing of its own"
			: g_bareLand >= 0 ? "the landing drawn from the same recordings (no separate one in the install)"
			: "the boots' own landing (this level has no id 4 entry)",
		g_source, next, next + loaded - 1, (int)g_bareSteps, (int)g_bareLand,
		(int)own.volume, (int)own.radius, (int)own.pitch, pitch);

	/* And the floor layer, said once where the set is built. It is the other
	 * half of a step and it is not in this level's sound map twice, so it
	 * cannot be bent the way ids 0 and 4 are - it is passed over instead, at
	 * its one caller. */
	Log("SFXLOG footsteps: the floor layer (this level's own ids %d..%d, played "
		"by AddFootprint on the same frame as id %d) is passed over while she "
		"is barefoot - those recordings are a boot on a surface",
		SFX_FOOTSTEPS_MUD, SFX_FOOTSTEPS_METAL, kStepsId);
}

bool HDFeetAvailable()
{
	return g_bareSteps >= 0;
}

const char* HDFeetWhyNot()
{
	return g_whyNot;
}

bool HDFeetBare(bool bare)
{
	if (!HDFeetAvailable() || !g_lut)
		return false;

	/* Two shorts, and that is the whole switch. No reload, no allocation and
	 * nothing that cares which thread asks: both sets are already in
	 * DS_Buffers and both entries are already in the level's own sound map. */
	g_lut[kStepsId] = bare ? g_bareSteps : g_ownSteps;

	if (g_bareLand >= 0)
		g_lut[kLandId] = bare ? g_bareLand : g_ownLand;

	if (g_bare != bare)
		g_saidQuiet = false;

	g_bare = bare;
	return g_bare;
}

bool HDFeetSurfaceQuiet()
{
	if (!g_bare)
		return false;

	if (!g_saidQuiet)
	{
		g_saidQuiet = true;
		Log("SFXLOG footsteps: the floor layer is passed over from here on - "
			"she is walking barefoot on %d recording%s from %s", g_takes,
			g_takes == 1 ? "" : "s", g_source);
	}

	return true;
}

bool HDFeetIsBare()
{
	return g_bare;
}

int HDFeetBareTakes()
{
	return g_takes;
}

int HDFeetBareLandTakes()
{
	return g_landTakes;
}

const char* HDFeetBareSource()
{
	return g_source;
}
