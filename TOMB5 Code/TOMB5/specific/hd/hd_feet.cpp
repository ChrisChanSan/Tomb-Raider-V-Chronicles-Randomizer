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
#include "../../game/objects.h"	// LARA - the one emitter that is switched
#include "hd_level.h"			// HDEnabled - the gate this all lives in
#include "hd_laradraw.h"		// HDLaraDrawActive - Lara drawn HD or not

/* The header names the two ids by number so that its pure rule compiles in a
 * test without the engine; these hold the numbers to game\sound.h. */
static_assert(HD_FEET_ID_STEPS == SFX_LARA_FEET, "hd_feet.h: id 0 moved");
static_assert(HD_FEET_ID_LAND == SFX_LARA_LAND, "hd_feet.h: id 4 moved");

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

	short g_ownSteps = -1;		// the level's own entries for ids 0 and 4
	short g_ownLand = -1;
	short g_bareSteps = -1;		// the spare infos, or -1
	short g_bareLand = -1;
	short g_bootSteps = -1;
	short g_bootLand = -1;

	int g_takes;
	int g_landTakes;
	bool g_bare;
	bool g_saidQuiet;		// the floor layer's own line, once per spell
	char g_source[160];
	char g_whyNot[160];
	char g_ordinary[160];		// where the ordinary set's id 0 came from

	/* The decoded size of each of the level's own samples, noted by
	 * LoadSamples' loop. Only the footsteps' entries are ever read. */
	long g_pcm[MAX_SAMPLES];

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

	int takes_of(const SAMPLE_INFO& info)
	{
		int takes = (info.flags >> 2) & 0xF;

		if (takes < 1)
			takes = 1;

		return takes > kMaxTakes ? kMaxTakes : takes;
	}

	void no_set(const char* why)
	{
		snprintf(g_whyNot, sizeof(g_whyNot), "%s", why);
		Log("SFXLOG footsteps: no bare set - %s", g_whyNot);
	}

	/* Whether the ordinary chain plays something else than the unmodded bank
	 * for one of the level's samples. 1 it does, 0 it does not; -1 when the
	 * chain has no recording for it and -2 when the unmodded bank has none to
	 * measure it against.
	 *
	 * The chain first, and the order cannot change the verdict: with no mod
	 * bank both answers come out of the same read buffer, and there they are
	 * the same bytes by construction. Where they differ - a mod's bank, or a
	 * loose .wav, which is a buffer of its own - there is no sharing. */
	int chain_differs(long sample)
	{
		long chainBytes = 0, plainBytes = 0;
		SfxRiffParts cp, pp;
		const char* chain = HQSfxPayload(sample, 0, &chainBytes, &cp);

		if (!chain)
			return -1;

		const char* plain = HQSfxPayloadBoots(sample, 0, &plainBytes, &pp);

		if (!plain)
			return -2;

		return chainBytes != plainBytes ||
			memcmp(chain, plain, (size_t)chainBytes) != 0;
	}

	/* The unmodded boots for one id: its takes out of the first unmodded
	 * bank into the slots from `slot` up, held to the length rule the loop
	 * holds every bank sample to. All or nothing - a set with a hole in it
	 * would play the ordinary recording on some steps and not on others.
	 * Returns how many slots it filled. */
	int boots_into(const SAMPLE_INFO& own, int slot, int next)
	{
		const int takes = takes_of(own);

		if (slot + takes > MAX_SAMPLES)
			return 0;

		for (int i = 0; i < takes; i++)
		{
			const long s = own.number + i;
			long bytes = 0;
			SfxRiffParts parts;

			if (s < 0 || s >= next)
				return 0;

			char* riff = HQSfxPayloadBoots(s, g_pcm[s], &bytes, &parts);

			if (!riff || !take_into(slot + i, riff, parts))
				return 0;
		}

		return takes;
	}
}

void HDFeetNoteSample(int levelSample, long pcmBytes22k)
{
	if (levelSample >= 0 && levelSample < MAX_SAMPLES)
		g_pcm[levelSample] = pcmBytes22k;
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
	g_bootSteps = -1;
	g_bootLand = -1;
	g_takes = 0;
	g_landTakes = 0;
	g_bare = false;
	g_saidQuiet = false;
	g_source[0] = 0;
	g_whyNot[0] = 0;
	g_ordinary[0] = 0;
}

void HDFeetLevelBuild(short* lut, SAMPLE_INFO* infos, int infoCount, int spare,
	int next)
{
	int loaded = 0;
	char why[160];

	HDFeetLevelEnd();

	/* The HD gate: everything this file adds is behind it, so a player with
	 * hd_build 0 hears exactly the classic engine - a sound mod's own
	 * footsteps included. Not the draw: the sets are loaded at the level's
	 * load and the draw chooses between them at every step. Silent: an
	 * unasked-for line every level for a feature that is switched off is
	 * noise, and the option's own state is in the log already. */
	if (!HDEnabled())
		return;

	if (!lut || !infos || infoCount <= 0 || spare < HD_FEET_SPARE_INFOS)
	{
		no_set("this level's sound map has no room for a second entry");
		return;
	}

	g_lut = lut;
	g_infos = infos;
	g_infoCount = infoCount;
	g_ownSteps = lut[kStepsId];
	g_ownLand = lut[kLandId];

	if (g_ownLand >= infoCount)
		g_ownLand = -1;

	snprintf(g_ordinary, sizeof(g_ordinary), "%s",
		HQSfxHaveBank() ? HQSfxSource() : "the level's own");

	if (g_ownSteps < 0 || g_ownSteps >= infoCount)
	{
		snprintf(g_ordinary, sizeof(g_ordinary), "none");
		no_set("this level has no footstep sound at all");
		return;
	}

	if (!HQSfxHaveBank())
	{
		no_set("there is no sound bank to take a second set from");
		return;
	}

	const SAMPLE_INFO own = infos[g_ownSteps];
	const int ownTakes = takes_of(own);

	/* The slot room, checked before a single sample is decoded. MAX_SAMPLES is
	 * 450 and the fullest classic level (RICH2) carries 148, so the bare set
	 * and the unmodded boots fit with 280 to spare; a randomized level with
	 * all four Tomb Raider III guests adds 21 and still fits. This is here for
	 * the level that does not, and it is the one thing that can take the
	 * toggle away from a player who has everything else. */
	if (next < 0 || next + ownTakes + 1 > MAX_SAMPLES)
	{
		snprintf(why, sizeof(why),
			"this level's %d samples leave no room for a second set (the "
			"engine holds %d)", next, MAX_SAMPLES);
		no_set(why);
		return;
	}

	/* Is the ordinary set a footstep mod's? Two questions with one test.
	 *
	 * The bare set asks it of the first take of id 0, as it always has: a
	 * sound mod that replaces the footsteps is a barefoot mod, so when the
	 * chain's id 0 is a mod's own recording the bare set IS the ordinary set.
	 *
	 * The boots ask it of every take of ids 0 and 4: where any one of them is
	 * not the unmodded bank's, the ordinary set is not the boots, and the
	 * boots are loaded on their own.
	 *
	 * "Is a mod's bank being played" would not do for either. Most sound mods
	 * are not about footsteps at all - a door mod, a weapon mod, a voice mod -
	 * and such a bank carries the install's own id 0 byte for byte, so the
	 * payloads are compared instead. The comparison reads the ordinary chain
	 * (bank, then the loose overlay over it), so a footstep mod that ships
	 * loose 0000..0003.wav and no bank of its own answers it as well:
	 * SfxProbe's own line reads "a mod's <NNNN>.wav replaces BANK sample
	 * NNNN", and 0000..0003 and 0010 are the boots. */
	bool useMod = false;
	const char* modWhy = "";
	/* A copy and not the pointer: HQSfxWavSource hands back a buffer of its own
	 * that the next call on it overwrites. */
	char modWho[160];

	{
		const int first = chain_differs(own.number);
		/* Which kind of mod it is, for the log alone - the rule has already
		 * decided. A loose .wav wins inside the chain, so when there is one
		 * it is the name to print even if a mod's bank is played as well. */
		const char* wav = HQSfxWavSource(own.number);
		const bool overlaid = *wav != 0;

		snprintf(modWho, sizeof(modWho), "%s", overlaid ? wav : HQSfxSource());
		snprintf(g_ordinary, sizeof(g_ordinary), "%s", overlaid ? wav
			: first == -1 ? "the level's own" : HQSfxSource());

		if (first > 0)
		{
			useMod = true;
			modWhy = overlaid ? "its own loose .wav stands on sound id 0"
				: "its own recordings stand on sound id 0";
		}
		else if (first == -2)
		{
			useMod = true;
			modWhy = "there is no unmodded bank to compare it against";
		}
		else if (first == -1)
			modWhy = "the chain has no sound id 0 at all";
		else
			modWhy = "it carries the install's own sound id 0, byte for byte";

		// Said only where there is a mod to say it of. With none, the chain and
		// the ruler are the same bank and the answer is not news.
		if (HQSfxFromMod() || overlaid)
			Log("SFXLOG footsteps: %s %s - %s", modWho,
				useMod ? "supplies the bare set" : "is not a footstep mod",
				modWhy);
	}

	const char* where = useMod ? modWho : HQSfxPlainSource();
	int pitch = own.pitch;

	if (useMod)
	{
		/* The mod's recordings are the level's own samples already - the
		 * ordinary chain put them there - so the bare entries point at them
		 * and nothing is loaded twice. Its landing is the level's own id 4,
		 * which is the mod's landing where it brought one. */
		g_takes = ownTakes;
		g_bareSteps = (short)infoCount;
		fill_info(infos[g_bareSteps], own, own.number, ownTakes, own.pitch);

		if (g_ownLand >= 0)
		{
			g_bareLand = (short)(infoCount + 1);
			infos[g_bareLand] = infos[g_ownLand];
			g_landTakes = takes_of(infos[g_ownLand]);
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

		if (!loaded)
		{
			no_set("none of the barefoot recordings could be read");
			return;
		}

		g_takes = loaded;
		g_bareSteps = (short)infoCount;
		fill_info(infos[g_bareSteps], own, next, g_takes, pitch);

		/* The landing draws from the same recordings: none of the bank's
		 * five barefoot takes stands out as a landing (hd_feet.h). So sound
		 * id 4 gets the same takes as sound id 0, with the level's own id 4
		 * volume and radius - the boots' thud does not fit a bare foot. */
		if (g_ownLand >= 0)
		{
			g_bareLand = (short)(infoCount + 1);
			fill_info(infos[g_bareLand], infos[g_ownLand], next, g_takes,
				pitch);
			g_landTakes = g_takes;
		}
	}

	snprintf(g_source, sizeof(g_source), "%s", *where ? where : "the bank");

	/* The unmodded boots, when the ordinary set is not them. Every take of
	 * both ids is compared, so a mod that replaces only the landing, or only
	 * some of the steps, still leaves the HD draw's boots whole. */
	bool footMod = false;

	for (int i = 0; i < ownTakes && !footMod; i++)
		footMod = chain_differs(own.number + i) > 0;

	if (g_ownLand >= 0)
	{
		const SAMPLE_INFO& land = infos[g_ownLand];

		for (int i = 0; i < takes_of(land) && !footMod; i++)
			footMod = chain_differs(land.number + i) > 0;
	}

	int bootsLoaded = 0;

	if (footMod)
	{
		const int first = next + loaded;
		const int steps = boots_into(own, first, next);

		if (steps)
		{
			g_bootSteps = (short)(infoCount + 2);
			fill_info(infos[g_bootSteps], own, first, steps, own.pitch);
			bootsLoaded = steps;

			if (g_ownLand >= 0)
			{
				const SAMPLE_INFO& land = infos[g_ownLand];
				const int lands = boots_into(land, first + steps, next);

				if (lands)
				{
					g_bootLand = (short)(infoCount + 3);
					fill_info(infos[g_bootLand], land, first + steps, lands,
						land.pitch);
					bootsLoaded += lands;
				}
			}
		}

		Log("SFXLOG footsteps: %s's own sound ids %d and %d are the ordinary "
			"set - the classic draw and every other emitter play them; Lara "
			"drawn HD in boots walks on %s's unmodded boots (%s)", modWho,
			kStepsId, kLandId,
			*HQSfxPlainSource() ? HQSfxPlainSource() : "the bank",
			!steps ? "not loaded - the ordinary set stands in"
				: g_bootLand >= 0 || g_ownLand < 0 ? "loaded"
				: "steps loaded, the landing stays the ordinary one");
	}

	Log("SFXLOG footsteps: the sets - ordinary = this level's own ids %d and "
		"%d through %s; bare = %d step%s and %s, from %s, %s, infos %d and %d "
		"(volume %d radius %d pitch %d -> %d); boots = %s",
		kStepsId, kLandId, g_ordinary, g_takes, g_takes == 1 ? "" : "s",
		g_bareLand < 0 ? "the ordinary landing (this level has no id 4 entry)"
			: useMod ? "the ordinary landing"
			: "the landing drawn from the same recordings",
		g_source, useMod ? "the ordinary samples themselves" : "in new slots",
		(int)g_bareSteps, (int)g_bareLand, (int)own.volume, (int)own.radius,
		(int)own.pitch, pitch,
		g_bootSteps >= 0 ? "the unmodded bank's, in new slots"
			: "the ordinary set (it is the unmodded bank's)");

	if (loaded || bootsLoaded)
		Log("SFXLOG footsteps: slots %d..%d hold %d bare and %d boot "
			"recording%s", next, next + loaded + bootsLoaded - 1, loaded,
			bootsLoaded, loaded + bootsLoaded == 1 ? "" : "s");

	/* And the floor layer, said once where the sets are built. It is the
	 * other half of a step and it is not in this level's sound map twice, so
	 * it is passed over at its one caller instead of being switched. */
	Log("SFXLOG footsteps: the floor layer (this level's own ids %d..%d, played "
		"by AddFootprint on the same frame as id %d) is passed over while she "
		"is barefoot - those recordings are a boot on a surface - and only "
		"while she is drawn HD",
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

	/* One flag, and that is the whole switch. No reload, no allocation, no
	 * write into the sound map: every set is already in DS_Buffers and every
	 * entry already in the level's own table, and the next step asks. */
	if (g_bare != bare)
		g_saidQuiet = false;

	g_bare = bare;
	return g_bare;
}

int HDFeetStepInfo(int sfx, int mapped, long caller)
{
	if ((sfx != kStepsId && sfx != kLandId) || !g_lut)
		return mapped;

	const HDFeetSets sets = { g_bareSteps, g_bareLand, g_bootSteps,
		g_bootLand };

	return HDFeetPick(sfx, mapped, caller, LARA, HDLaraDrawActive(), g_bare,
		sets);
}

const char* HDFeetInfoName(int info)
{
	if (info < 0 || !g_lut)
		return "";

	if (info == g_bareSteps || info == g_bareLand)
		return "bare";

	if (info == g_bootSteps || info == g_bootLand)
		return "boots";

	return "";
}

bool HDFeetSurfaceQuiet()
{
	if (!HDFeetQuietRule(HDLaraDrawActive(), g_bare, HDFeetAvailable()))
	{
		// The next quiet spell - after F12 or the changer - says so again.
		g_saidQuiet = false;
		return false;
	}

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
