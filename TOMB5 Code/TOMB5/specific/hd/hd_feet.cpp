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

	/* ONE RECORDING INTO ONE DS_Buffers SLOT. The payload has been through
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

	/* A SPARE SAMPLE_INFO, out of the level's own entry for that id.
	 *
	 * Everything about how a footstep is PLAYED stays the level's - the
	 * volume, the hearing radius, the volume randomness and the play mode -
	 * because those describe the game and not the recording. Two fields do
	 * not: the sample number and the count are what was really loaded, and
	 * the PITCH is handed in, because the bank ships its barefoot entry at 0
	 * where the footstep entry is 12 (measured, and both of the user's banks
	 * agree). The remaster turned the random pitch off for those five
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

	/* THE HD GATE. Everything round M3 added is behind it, so a player with
	 * hd_enable off hears exactly what he heard before - a sound mod's own
	 * footsteps included, which is what round M1 gave him. Silent: an
	 * unasked-for line every level for a feature that is switched off is
	 * noise, and the option's own state is in the log already. */
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

	/* THE SLOT ROOM, asked before a single sample is decoded. MAX_SAMPLES is
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

	/* WHERE THE BARE RECORDINGS COME FROM, in the order this round decided.
	 *
	 * 1. What the ORDINARY chain plays for sound id 0 - a mod's bank, or a
	 *    mod's loose .wav over it - but only when a mod's bank really is the
	 *    one being played AND that bank REALLY REPLACES sound id 0. A sound
	 *    mod that replaces the footsteps is a BAREFOOT mod; this is where it
	 *    belongs, and it is the only place it is still heard now that the
	 *    boots take the unmodded bank.
	 *
	 * 2. Otherwise the remaster's own barefoot set: sound id HQ_SFX_BARE_FEET
	 *    of the bank, which NO LEVEL carries - so it is reached by bank sample
	 *    number and not through the level bridge every other sound uses.
	 *
	 * THE "REALLY REPLACES" IS THE FIX ROUND'S (M3). `HQSfxFromMod()` says
	 * only that A mod's bank is being played, and most sound mods are not
	 * about footsteps at all - a door mod, a weapon mod, a voice mod. Such a
	 * bank carries the install's own id 0 byte for byte, so taking the bare
	 * set through it handed the BOOTS' four recordings back under the name
	 * "bare": the toggle did nothing and the log said it had. The two payloads
	 * are compared here instead. The unmodded bank is the ruler and reading
	 * both at once is safe - each bank owns its own read buffer
	 * (specific\sfxbank.cpp, SfxBankSample). */
	const bool fromMod = HQSfxFromMod();
	bool useMod = false;
	const char* modWhy = "";

	if (fromMod)
	{
		long modBytes = 0, plainBytes = 0;
		SfxRiffParts mp, pp;
		const char* chain = HQSfxPayload(own.number, 0, &modBytes, &mp);
		const char* plain = HQSfxPayloadBoots(own.number, 0, &plainBytes, &pp);

		if (!chain)
			modWhy = "the mod's bank has no sound id 0 at all";
		else if (!plain)
		{
			useMod = true;
			modWhy = "there is no unmodded bank to compare it against";
		}
		else if (modBytes != plainBytes ||
			memcmp(chain, plain, (size_t)modBytes))
		{
			useMod = true;
			modWhy = "its own recordings stand on sound id 0";
		}
		else
			modWhy = "it carries the install's own sound id 0, byte for byte";

		Log("SFXLOG footsteps: %s %s - %s", HQSfxSource(),
			useMod ? "supplies the bare set" : "is not a footstep mod",
			modWhy);
	}

	const char* where = useMod ? HQSfxSource() : HQSfxPlainSource();
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

		/* AND ITS LANDING, when it brought one. The user's own mod does: its
		 * sound id 4 carries 4.4 times the RMS of its own steps, which is
		 * what a landing is. */
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

		/* THE BANK'S OWN PITCH for those five, read off the file rather than
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
	g_landTakes = landLoaded;
	g_bareSteps = (short)infoCount;
	fill_info(infos[g_bareSteps], own, next, g_takes, pitch);

	if (landLoaded)
	{
		g_bareLand = (short)(infoCount + 1);
		fill_info(infos[g_bareLand], infos[g_ownLand], next + g_takes, 1,
			infos[g_ownLand].pitch);
	}

	snprintf(g_source, sizeof(g_source), "%s", *where ? where : "the bank");

	Log("SFXLOG footsteps: boots = this level's own ids %d and %d through %s; "
		"bare = %d step%s and %s, from %s, in slots %d..%d, infos %d and %d "
		"(volume %d radius %d pitch %d -> %d)",
		kStepsId, kLandId,
		*HQSfxPlainSource() ? HQSfxPlainSource() : "the level's own samples",
		g_takes, g_takes == 1 ? "" : "s",
		g_landTakes ? "a landing of its own"
			: "the boots' own landing (none of the five stands out as one)",
		g_source, next, next + loaded - 1, (int)g_bareSteps, (int)g_bareLand,
		(int)own.volume, (int)own.radius, (int)own.pitch, pitch);

	/* AND THE FLOOR LAYER, said once where the set is built (#153, round M3,
	 * the fix round). It is the other half of a step and it is not in this
	 * level's sound map twice, so it cannot be bent the way ids 0 and 4 are -
	 * it is passed over instead, at its one caller. */
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

	/* TWO SHORTS, AND THAT IS THE WHOLE SWITCH. No reload, no allocation and
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
