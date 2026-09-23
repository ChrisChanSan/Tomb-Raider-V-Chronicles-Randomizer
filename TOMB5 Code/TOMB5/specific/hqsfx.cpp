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
#include "../tomb5/pch.h"
#include "hqsfx.h"
#include "sfxbank.h"
#include "hd/hd_mods.h"
#include "hd/hd_markers.h"	// this run's own imported sound ids
#include "function_stubs.h"

// Optional high quality sounds.
//
// Tomb Raider Chronicles has no MAIN.SFX - that was Tomb Raider 4. From
// Chronicles on, Core moved the samples into the level files, so every .TRC
// carries its own bank of MS-ADPCM at 22050 Hz. The remaster went the other
// way and consolidated everything back into one global MAIN.SFX of plain PCM
// at 44100 Hz.
//
// Measured against the original samples, that file is not the old material
// resampled: run a 22050 sample up to 44100 and back down and almost nothing
// is left over, while the remaster's samples keep about twenty times as much
// residue, and their spectrum runs through 11025 Hz without the cliff a
// resampled signal always shows. Same recordings, same levels, but with the
// bandwidth the 2000 release threw away. Three of the 463 are resampled after
// all; they simply come out sounding like today.
//
// If such a bank is there, this reads the samples from it instead of the ones
// inside the level. If none is, nothing here happens and the game plays
// Core's samples as it always has. The file is the player's own data and is
// never shipped - exactly like the HQ music in audio.cpp.
//
// Where the bank comes from. Three places, in this order:
//
//   1. the first active mod that ships 5\DATA\MAIN.SFX - a sound mod is a
//      whole bank with a handful of its samples replaced, which is how the
//      remaster's own drop-in mods are built;
//   2. audio\MAIN.SFX, the copy the player used to have to make by hand;
//   3. the installed remaster's own 5\DATA\MAIN.SFX, read where it lies
//      through HDInstallRoot().
//
// The third is new and it is what makes the hand copy unnecessary from now
// on. Nothing is copied anywhere by any of the three.
//
// That order is not written here any more: it is SfxBankChainLink in
// sfxbank.cpp, so that SfxProbe walks the engine's chain instead of a copy
// of it. What this file still owns is what the engine does with each
// candidate - which one is played from, which one becomes the ruler, and
// the bridge.
//
// When the first one answers, the second is still opened - as a ruler and
// not as a source. A sound mod is a whole bank with a handful of its samples
// swapped for other recordings, and the plausibility rule further down is a
// test of the pairing that only works while both sides hold the same sound.
// The unmodded bank is what keeps that test possible; see the long note at
// the rule itself.
//
// The file format itself is read by sfxbank.cpp, which the SfxProbe
// diagnostic tool links as well - one parser, so what the tool prints is what
// the engine did.
//
// The bridge between the two numbering schemes is the sound id. A level knows
// its samples as 0..135 or so, the bank knows them as 0..472, and neither
// order has anything to do with the other. But both sides index their LUT by
// the same sound id, so id -> SAMPLE_INFO -> sample number can be walked on
// both sides and the results paired. Core's SAMPLE_INFO values are identical
// in every level (checked across three of them: 72 shared ids, no
// difference), so the bridge does not shift from level to level.
//
// What is deliberately not done: the level's own tables stay in place and
// only the audio data is swapped. That keeps the sounds the randomizer
// imports from Tomb Raider 3 working - they have no counterpart here and
// simply keep their original samples - and it means MAX_SAMPLES and the
// on-disk LUT length are not touched at all.

/* The largest loose .wav a mod may replace a sample with. Not a format rule -
 * a guard, so that a mistyped file name cannot make the engine read a video
 * into memory. Four megabytes is about forty-seven seconds of the mono 16 bit
 * 44100 the bank itself holds, which is longer than any sound effect in the
 * game by an order of magnitude. */
#define HQ_WAV_CAP	(4 * 1024 * 1024)

static SfxBank* hq_bank;
static bool hq_tried;
static bool hq_ready;

static char hq_source[300];	// what was opened, for the log and file.cpp

/* The unmodded bank beside it, opened only when the chosen one came out of
 * a mod. It is not played from - it is the ruler the bridge is measured
 * with. Why that is needed is written out in full at the bridgeOk test in HQSfxPayload; the
 * short of it is that a sound mod's whole purpose is to put a different
 * recording in, and the length rule below cannot tell a different
 * recording from a wrong pairing. */
static SfxBank* hq_ref;
static char hq_ref_source[300];

/* Whether the bank being played came out of a mod. HQSfxOpen knew it and
 * threw it away; the footsteps need it, because "the boots keep the
 * install's own recordings" means "the first unmodded bank of the chain"
 * and that is hq_ref when this is true and hq_bank itself when it is not. */
static bool hq_from_mod;

static long* hq_map;		// level sample number -> bank sample number
static long* hq_ref_map;	// the same through the unmodded bank's own LUT
static long hq_substituted;
static long hq_overlaid;	// of those, answered by a mod's loose .wav
static long hq_replaced;	// of those, a sample the mod's bank replaced

static char* hq_wav;		// the loose .wav in hand, if any
static long hq_wav_bytes;

/* The switch is tested before the line is built. Log() tests gLogEnabled
 * itself, but a caller that runs its own snprintf first still pays for
 * building the whole text - one per bank, one per mod .wav, one per refused
 * pairing - on every level load even with no -log on the command line. Each
 * pair sits in `if (gLogEnabled)` instead, the shape the other seams use.
 * With -log the text is unchanged. */
static void HQSfxLog(const char* line)
{
	Log("%s", line);
}

/* One candidate of the chain, tried and reported. `into` is hq_bank for the
 * one that will be played from and hq_ref for the ruler beside it; `label` is
 * what the log and file.cpp call it afterwards. Every candidate earns a line
 * whether it opened or not - a player whose bank is quietly not the one he
 * thinks it is has no other way of finding out. */
static bool HQSfxTry(const char* path, const char* label, SfxBank** into,
	char* source, int sourceSize)
{
	const char* why = "";
	char line[512];

	if (!path || !*path)
	{
		if (gLogEnabled)
		{
			snprintf(line, sizeof(line),
				"MODLOG bank %s - there is none to look at", label);
			HQSfxLog(line);
		}
		return false;
	}

	*into = SfxBankOpen(path, &why);

	if (!*into)
	{
		if (gLogEnabled)
		{
			snprintf(line, sizeof(line), "MODLOG bank %s (%s) - not opened: %s",
				label, path, why);
			HQSfxLog(line);
		}
		return false;
	}

	snprintf(source, (size_t)sourceSize, "%s", label);
	if (gLogEnabled)
	{
		snprintf(line, sizeof(line),
			"MODLOG bank %s (%s) - opened, %d samples in %d sound ids, %d infos, "
			"largest %ld bytes", label, path, SfxBankSamples(*into),
			SfxBankIdsUsed(*into), SfxBankInfos(*into), SfxBankLargest(*into));
		HQSfxLog(line);
	}
	return true;
}

bool HQSfxOpen()
{
	char line[512];

	if (hq_tried)
		return hq_ready;

	hq_tried = true;

	/* The mods are scanned here as well as at the first HD level build - one
	 * scanner with one cached answer, so whichever path gets there first, the
	 * two see the same list. Sound does not care whether hd_enable is on. */
	HDModsScan();

	bool fromMod = false;

	hq_from_mod = false;

	/* The whole chain in one loop, in the order SfxBankChainLink gives it -
	 * one order, walked by the engine and by SfxProbe alike, so the tool
	 * cannot drift from what the engine does. What this loop owns is which
	 * of the two banks a link fills.
	 *
	 * Which of the two: with no mod bank in hand the link is the bank
	 * itself and the loop stops at the first that opens; with one, the next
	 * plain link is the ruler - opened, never played from - and the loop
	 * stops at the first of those that opens. A player with no mods
	 * therefore sees exactly the same chain as without this rule.
	 *
	 * The break stands before SfxBankChainLink because its last link asks
	 * HDInstallRoot(), and that is a walk through the registry and every
	 * Steam library. Nothing may pay for it that does not need the answer. */
	for (int link = 0; ; link++)
	{
		char path[512], label[512];
		int mod = -1;

		if (hq_bank && (!fromMod || hq_ref))
			break;		// nothing left to want

		if (!SfxBankChainLink(link, 0, 0, path, sizeof(path), label,
			sizeof(label), &mod))
			break;		// past the end of the chain

		/* A mod's bank is never the ruler. The ruler exists to measure the
		 * bridge against a recording no mod has touched, so once a mod's
		 * bank is the one being played the remaining mod links are passed
		 * over in silence. */
		if (hq_bank && mod >= 0)
			continue;

		SfxBank** into = hq_bank ? &hq_ref : &hq_bank;
		char* source = hq_bank ? hq_ref_source : hq_source;
		const int size = hq_bank ? (int)sizeof(hq_ref_source) : (int)sizeof(hq_source);

		if (HQSfxTry(path, label, into, source, size) && mod >= 0)
		{
			fromMod = true;
			hq_from_mod = true;
		}
	}

	if (fromMod && hq_ref)
	{
		if (gLogEnabled)
		{
			snprintf(line, sizeof(line),
				"MODLOG bank ruler: %s, %d samples - a mod's bank is played and "
				"this one measures the bridge", hq_ref_source,
				SfxBankSamples(hq_ref));
			HQSfxLog(line);
		}
	}
	else if (fromMod)
		HQSfxLog("MODLOG bank ruler: none - a mod's bank is played and there is "
			"no unmodded one to measure the bridge against, so a sample the "
			"mod made longer keeps the level's own");

	if (!hq_bank)
	{
		/* No bank means no bridge. A loose .wav is named by a bank sample
		 * number, and without a bank there is nothing that turns a level's
		 * own sample number into one. Said once, because a player who
		 * dropped a sound mod in and hears nothing deserves the reason
		 * rather than silence. */
		for (int i = 0; i < HDModsCount(); i++)
		{
			HDModShips ships;

			HDModsShipsOf(i, &ships);

			if (!ships.wavs)
				continue;

			HQSfxLog("MODLOG wav overlay needs a bank - none of the three was "
				"usable, so the level's own samples play");
			break;
		}

		Log("HQ SFX: no bank anywhere, using the level's own samples");
		return false;
	}

	hq_ready = true;
	Log("HQ SFX: %s accepted - %d sounds, %d infos, largest %ld bytes",
		hq_source, SfxBankSamples(hq_bank), SfxBankInfos(hq_bank),
		SfxBankLargest(hq_bank));
	return true;
}

void HQSfxClose()
{
	SfxBankClose(hq_bank);
	SfxBankClose(hq_ref);
	hq_bank = 0;
	hq_ref = 0;
	hq_source[0] = 0;
	hq_ref_source[0] = 0;
	hq_from_mod = false;
	hq_ready = false;
	HQSfxDropMap();
}

void HQSfxDropMap()
{
	free(hq_map);
	free(hq_ref_map);
	hq_map = 0;
	hq_ref_map = 0;
	hq_substituted = 0;
	hq_overlaid = 0;
	hq_replaced = 0;
	free(hq_wav);
	hq_wav = 0;
	hq_wav_bytes = 0;
}

/* The bridge through one bank. A level knows its samples as 0..135 or so and
 * a bank knows them as 0..472; both index their LUT by the same sound id, so
 * id -> SAMPLE_INFO -> sample number walks on both sides and the results
 * pair. Built through the bank's own lookup table, which is why the ruler
 * gets its own map rather than sharing the played bank's numbers: the two
 * banks have different sample counts (measured: 463 against 473) and nothing
 * says a number means the same sound in both. The sound id does. */
static long* HQSfxMapThrough(SfxBank* bank, const short* levelLut,
	long levelLutEntries, const SAMPLE_INFO* levelInfos, long levelInfoCount,
	long* paired, int bit = -1, long* refused = 0, long* outside = 0)
{
	long sfx, li, v, n, lvar;
	long* map;

	*paired = 0;

	if (!bank)
		return 0;

	map = (long*)malloc(MAX_SAMPLES * sizeof(long));

	if (!map)
		return 0;

	for (v = 0; v < MAX_SAMPLES; v++)
		map[v] = -1;

	for (sfx = 0; sfx < levelLutEntries; sfx++)
	{
		li = levelLut[sfx];

		const int hi = SfxBankLut(bank, (int)sfx);

		if (li < 0 || li >= levelInfoCount || hi < 0)
			continue;

		/* An id this run imported is not the remaster's sound. The
		 * randomizer's Tomb Raider III guests take over free slots of the
		 * TR5 soundmap, and free in a level is not free in the bank: the
		 * vulture's 420..423 are RICH1's VCI scientist voice lines there,
		 * and the length rule alone lets 422 through - the level's 604 ms
		 * against the bank's 624, well inside the fifth the rule allows -
		 * so the bird would die saying something in English. (The other
		 * three are refused by length: 322, 425 and 406 ms against 624,
		 * 624 and 1037; measured with SfxProbe -level on a randomized
		 * ANDREA1.)
		 *
		 * A length cannot tell a wrong pairing from a different recording
		 * and a name can, so the run writes the ids it imported into its
		 * own sidecar and they are passed over here. Ids the run moved
		 * between TR5 levels are not in that list and keep their pairing:
		 * those really are the remaster's own sounds. */
		if (HDMarkersSoundImported((int)sfx))
		{
			if (refused && *refused < 32)
			{
				char line[160];

				if (gLogEnabled)
				{
					snprintf(line, sizeof(line),
						"SFXLOG bridge: id %d is a randomizer import - the level's "
						"own sample plays", (int)sfx);
					HQSfxLog(line);
				}
			}

			if (refused)
				(*refused)++;

			continue;
		}

		/* The safety net. The bank says which level files use each sound; a
		 * pairing the mask excludes is either a sound the run moved here -
		 * which is real and keeps its pairing - or an id nobody has
		 * declared. Either way it is worth a line, and it is the line that
		 * would find a wrongly paired sound without anybody reporting it. */
		if (bit >= 0 && outside && !((SfxBankMask(bank, (int)sfx) >> bit) & 1))
		{
			/* Counted first and printed second: the total the summary line
			 * reports must not saturate at the 32-line print cap below, or a
			 * reader could not tell 32 occurrences from 200 - which is the
			 * one thing a safety net is for. */
			(*outside)++;

			if (*outside <= 32)
			{
				char line[192];

				if (gLogEnabled)
				{
					snprintf(line, sizeof(line),
						"SFXLOG bridge: id %d is paired, but the bank says no level "
						"of ours uses it (mask 0x%08x%08x, this level is bit %d) - a "
						"sound the run moved here, or one nobody has declared",
						(int)sfx,
						(unsigned int)((unsigned long long)SfxBankMask(bank, (int)sfx) >> 32),
						(unsigned int)((unsigned long long)SfxBankMask(bank, (int)sfx) & 0xFFFFFFFFu),
						bit);
					HQSfxLog(line);
				}
			}
		}

		// The variation count sits in bits 2..5 of flags, the same place
		// sound.cpp reads it from when it rolls which of a sound's takes to
		// play. Pair only as many as both sides actually have.
		lvar = (levelInfos[li].flags >> 2) & 0xF;

		if (!lvar)
			lvar = 1;

		const long hvar = SfxBankInfoVariations(bank, hi);

		n = lvar < hvar ? lvar : hvar;

		for (v = 0; v < n; v++)
		{
			const long ls = levelInfos[li].number + v;
			const long hs = SfxBankInfoNumber(bank, hi) + v;

			if (ls < 0 || ls >= MAX_SAMPLES || hs < 0 || hs >= SfxBankSamples(bank))
				continue;

			map[ls] = hs;
			(*paired)++;
		}
	}

	return map;
}

void HQSfxBuildMap(const short* levelLut, long levelLutEntries,
	const SAMPLE_INFO* levelInfos, long levelInfoCount)
{
	long paired = 0, refPaired = 0;

	HQSfxDropMap();

	if (!hq_ready || !levelLut || !levelInfos || levelInfoCount <= 0)
		return;

	if (levelLutEntries > SFX_LUT_ENTRIES)
		levelLutEntries = SFX_LUT_ENTRIES;

	/* Which level the bank believes this is, for the safety net below. Asked
	 * of the bank that is played, because that is the one whose pairings
	 * are being made. */
	const int bit = SfxBankLevelBit(hq_bank, levelLut, levelLutEntries);
	long refused = 0, outside = 0;

	hq_map = HQSfxMapThrough(hq_bank, levelLut, levelLutEntries, levelInfos,
		levelInfoCount, &paired, bit, &refused, &outside);

	if (!hq_map)
		return;

	hq_ref_map = HQSfxMapThrough(hq_ref, levelLut, levelLutEntries, levelInfos,
		levelInfoCount, &refPaired);

	Log("HQ SFX: %d of this level's samples have a counterpart in the bank", paired);

	if (HDMarkersSoundImportCount() || outside || bit < 0)
		Log("SFXLOG bridge: this level is the bank's level bit %d; the run declared %d imported sound id%s, %ld pairing%s refused, %ld more that no level of ours claims", bit, HDMarkersSoundImportCount(),
			HDMarkersSoundImportCount() == 1 ? "" : "s", refused,
			refused == 1 ? "" : "s", outside);

	if (hq_ref_map)
	{
		char line[256];

		if (gLogEnabled)
		{
			snprintf(line, sizeof(line),
				"MODLOG bridge: %d pairs through the mod's bank, %d through the "
				"ruler", paired, refPaired);
			HQSfxLog(line);
		}
	}
}

/* A mod's own recording for bank sample `g`.
 *
 * Reached only after the bank's own sample for this level sample has passed
 * every check below - and that is the whole of the length question. The bank
 * check exists to catch a wrong pairing: the randomizer moves sounds around
 * inside a level, so "level sample 17 is bank sample 42" can be false, and
 * the same recording at twice the rate being about twice as long is what says
 * so. A mod's file is a different recording and its length says nothing at
 * all about the pairing - measured: the five Underworld bare footsteps run
 * 0.30 s where Core's are 0.038 s, eight times longer, and a length rule
 * would refuse every one of them while the pairing is perfectly correct.
 *
 * So the bank verifies the bridge and the mod supplies the audio. What is
 * still checked here is the file itself: a RIFF WAVE, uncompressed PCM, mono,
 * 16 bit, with a data chunk inside the file - exactly what the bank's own
 * samples have to be, because that is what DXCreateSample hands to XAudio2.
 * Anything else keeps the bank's sample and says why. */
static char* HQSfxWavFor(long g, long* riffBytes, SfxRiffParts* parts)
{
	char path[512];
	char line[640];
	const char* why = "";
	int mod = -1;
	long dataBytes = 0, byteRate = 0;
	SfxRiffParts where;

	if (!*HDModsWav((int)g, &mod, path, sizeof(path)))
		return 0;

	free(hq_wav);
	hq_wav = SfxWavLoad(path, HQ_WAV_CAP, &hq_wav_bytes, &why);

	if (!hq_wav)
	{
		if (gLogEnabled)
		{
			snprintf(line, sizeof(line), "MODLOG wav %04ld refused - %s (%s)",
				g, why, path);
			HQSfxLog(line);
		}
		return 0;
	}

	if (!SfxRiffCheck(hq_wav, hq_wav_bytes, &dataBytes, &byteRate, &why, &where))
	{
		if (gLogEnabled)
		{
			snprintf(line, sizeof(line), "MODLOG wav %04ld refused - %s (%s)",
				g, why, path);
			HQSfxLog(line);
		}
		free(hq_wav);
		hq_wav = 0;
		hq_wav_bytes = 0;
		return 0;
	}

	/* The format block's offset is part of the answer. A wav exported by an
	 * editor often carries a JUNK or a LIST block in front of fmt, so a
	 * caller that assumes a fixed offset 20 for the format tag would read
	 * the middle of that first block's header instead - accepting the file
	 * here and discarding it there. What is found here is what gets handed
	 * on. */
	if (gLogEnabled)
	{
		snprintf(line, sizeof(line),
			"MODLOG wav %04ld <- mod%d %s, %ld bytes, %ld of them audio, %ld B/s, "
			"format block at +%ld",
			g, mod, HDModsName(mod), hq_wav_bytes, dataBytes, byteRate, where.fmtAt);
		HQSfxLog(line);
	}

	hq_overlaid++;

	if (riffBytes)
		*riffBytes = hq_wav_bytes;

	if (parts)
		*parts = where;

	return hq_wav;
}

char* HQSfxPayload(long levelSample, long pcmBytes22k, long* riffBytes,
	SfxRiffParts* parts)
{
	long g, dataLen = 0, bankBytes;
	const char* why = "";
	char* buffer;
	SfxRiffParts where;

	if (riffBytes)
		*riffBytes = 0;

	if (parts)
	{
		parts->fmtAt = 0;
		parts->fmtBytes = 0;
		parts->dataAt = 0;
		parts->dataBytes = 0;
		parts->byteRate = 0;
	}

	if (!hq_ready || !hq_map || levelSample < 0 || levelSample >= MAX_SAMPLES)
		return 0;

	g = hq_map[levelSample];

	if (g < 0 || g >= SfxBankSamples(hq_bank))
		return 0;

	buffer = (char*)SfxBankSample(hq_bank, (int)g, &bankBytes);

	if (!buffer)
	{
		Log("HQ SFX: sample %d could not be read, keeping the level's own", g);
		return 0;
	}

	/* The bank's sample, checked by the shared parser. A raw walk of the
	 * RIFF chunks here - `at += 8 + chunk` with no bound on `chunk` at all -
	 * plus a WAVEFORMATEX laid straight over offset 20 on the assumption
	 * that fmt comes first, is not safe once the bank can be a downloaded
	 * mod's 5\DATA\MAIN.SFX: SfxBankOpen validates only the outer chain of
	 * RIFFs, never the chunks inside one, so a garbage inner length would
	 * walk a pointer off the buffer while the level was loading.
	 *
	 * SfxRiffCheck is that walk done once, bounds-checked (see the guard
	 * there), and it already answers the format question and the data length
	 * together - the same call the loose .wav goes through, so a mod's bank
	 * and a mod's .wav are held to one rule rather than two. */
	if (!SfxRiffCheck(buffer, bankBytes, &dataLen, 0, &why, &where))
	{
		Log("HQ SFX: sample %d - %s, keeping the level's own", g, why);
		return 0;
	}

	// Plausibility. The pairing runs over the sound map, and the randomizer
	// is free to move sounds around inside a level, so a mismatched pair is
	// not impossible. Both sides hold the same recording, so at twice the
	// rate the bank's sample must be about twice as long; anything further
	// off than a fifth is not the same sound and keeps Core's version.
	// The arithmetic is SfxPairPlausible, so that SfxProbe judges a pairing
	// with this same rule and not with a copy of it.
	if (pcmBytes22k > 0)
	{
		const long expected = pcmBytes22k * 2;

		if (!SfxPairPlausible(dataLen, pcmBytes22k))
		{
			/* Unless a mod put a different recording there.
			 *
			 * The rule above is a test of the pairing, and it works because
			 * both sides hold the same sound. A mod's bank breaks that on
			 * purpose: measured, the five Underworld bare footsteps are
			 * 0.300 s where Core's are 0.038 s - eight times longer - so the
			 * rule above refuses precisely the five samples the mod exists
			 * to change, and the mod would do nothing while the pairing was
			 * right all along.
			 *
			 * So when a mod's bank is the one being played, the pairing is
			 * measured against the unmodded bank instead - the ruler opened
			 * beside it in HQSfxOpen, walked through its own sound id table
			 * so that the two banks' differing sample counts cannot shift
			 * anything. If the ruler's sample for this level sample is the
			 * right length, the bridge is right and the mod's recording is
			 * played whatever its length. If it is not, the level keeps its
			 * own sample exactly as before.
			 *
			 * Without a ruler - no mod bank, or no unmodded bank to be had -
			 * none of this runs and the rule above is the whole answer. */
			bool bridgeOk = false;
			long refLen = 0, refBytes = 0;

			if (hq_ref && hq_ref_map && hq_ref_map[levelSample] >= 0)
			{
				const long r = hq_ref_map[levelSample];
				const char* ref = SfxBankSample(hq_ref, (int)r, &refBytes);

				if (ref && SfxRiffCheck(ref, refBytes, &refLen, 0, 0) &&
					refLen >= expected - expected / 5 &&
					refLen <= expected + expected / 5)
					bridgeOk = true;
			}

			if (!bridgeOk)
			{
				Log("HQ SFX: sample %d is %d bytes where %d were expected - keeping the level's own",
					g, dataLen, expected);
				return 0;
			}

			hq_replaced++;

			{
				char line[256];

				if (gLogEnabled)
				{
					snprintf(line, sizeof(line),
						"MODLOG bank sample %ld is %ld bytes where the ruler has "
						"%ld - a recording the mod replaced, and the bridge holds",
						g, dataLen, refLen);
					HQSfxLog(line);
				}
			}
		}
	}

	hq_substituted++;

	/* The mod's own recording, if it brought one for this bank number. The
	 * bridge has just been proved by everything above; see HQSfxWavFor. */
	{
		long wavBytes = 0;
		SfxRiffParts wavWhere;
		char* wav = HQSfxWavFor(g, &wavBytes, &wavWhere);

		if (wav)
		{
			if (riffBytes)
				*riffBytes = wavBytes;

			if (parts)
				*parts = wavWhere;

			return wav;
		}
	}

	if (riffBytes)
		*riffBytes = bankBytes;

	/* Where the gate found this one's chunks, so that the caller hands the
	 * format block and the audio on from where they are. Both counters above
	 * were raised on the strength of this return, and the caller may not
	 * undo it - see the contract in hqsfx.h. */
	if (parts)
		*parts = where;

	return buffer;
}

/* ------------------------------------------------ the footsteps -- */

/* The first unmodded bank of the chain, which is the ruler when a mod's
 * bank is played and the played bank itself when none is. Null when there
 * is no bank at all. */
static SfxBank* HQSfxPlain()
{
	if (!hq_ready)
		return 0;

	return hq_from_mod ? hq_ref : hq_bank;
}

static long* HQSfxPlainMap()
{
	return hq_from_mod ? hq_ref_map : hq_map;
}

/* Whether the bank being played came out of a mod - which is what decides
 * where the barefoot set comes from, see hd_feet.cpp. */
bool HQSfxFromMod()
{
	return hq_ready && hq_from_mod;
}

const char* HQSfxPlainSource()
{
	if (!HQSfxPlain())
		return "";

	return hq_from_mod ? hq_ref_source : hq_source;
}

/* The boots' payload for one of the level's samples.
 *
 * HQSfxPayload with the mod taken out of it, for sound ids 0 and 4 alone:
 * the first unmodded bank of the chain, and no loose .wav overlay. The
 * footsteps need two independent sets because a sound mod which replaces
 * the footsteps replaces them for every outfit at once - the Underworld
 * bare-footstep mod, for example, would make Lara barefoot in boots, in a
 * wetsuit and in a parka alike. With the outfit deciding which of two sets
 * plays, the two have to be two, and the boots' half may not be the half a
 * mod has swapped.
 *
 * The plausibility rule is the one HQSfxPayload has always had: the unmodded
 * bank holds the same recording as the level, so at twice the rate it must
 * be about twice as long. No ruler is needed here - this is the ruler.
 *
 * Null means "keep the level's own sample", which is Core's boots and
 * therefore already the right answer. */
char* HQSfxPayloadBoots(long levelSample, long pcmBytes22k, long* riffBytes,
	SfxRiffParts* parts)
{
	SfxBank* bank = HQSfxPlain();
	long* map = HQSfxPlainMap();
	long dataLen = 0, bankBytes = 0;
	const char* why = "";
	SfxRiffParts where;

	if (riffBytes)
		*riffBytes = 0;

	if (!bank || !map || levelSample < 0 || levelSample >= MAX_SAMPLES)
		return 0;

	const long g = map[levelSample];

	if (g < 0 || g >= SfxBankSamples(bank))
		return 0;

	char* buffer = (char*)SfxBankSample(bank, (int)g, &bankBytes);

	if (!buffer || !SfxRiffCheck(buffer, bankBytes, &dataLen, 0, &why, &where))
		return 0;

	if (!SfxPairPlausible(dataLen, pcmBytes22k))
		return 0;

	hq_substituted++;

	if (riffBytes)
		*riffBytes = bankBytes;

	if (parts)
		*parts = where;

	return buffer;
}

/* Which mod's loose .wav answers one of this level's samples.
 *
 * The same lookup HQSfxWavFor makes and nothing more: this level's sample
 * through the bridge to a bank sample number, and that number asked of the
 * mods' own <NNNN>.wav files. It loads no audio and decides nothing - it is
 * here so that the barefoot set can name the mod that supplied it, because a
 * mod which ships loose .wav and no bank of its own is a footstep mod exactly
 * as a mod's bank is and the log may not call it "the install". */
const char* HQSfxWavSource(long levelSample)
{
	static char said[160];
	char path[512];
	int mod = -1;

	said[0] = 0;

	if (!hq_ready || !hq_map || levelSample < 0 || levelSample >= MAX_SAMPLES)
		return said;

	const long g = hq_map[levelSample];

	if (g < 0 || g >= SfxBankSamples(hq_bank))
		return said;

	if (!*HDModsWav((int)g, &mod, path, sizeof(path)))
		return said;

	snprintf(said, sizeof(said), "%s", HDModsName(mod));
	return said;
}

/* The unmodded bank's own takes for one sound id, by bank sample number.
 * This is how the barefoot set is reached: it is sound id HQ_SFX_BARE_FEET
 * of the remaster's own bank and no level has it, so it cannot be found
 * through the level bridge the way every other sound is. Returns how many
 * were written, 0 when this bank has no such id. */
int HQSfxBankTakes(int soundId, long* samples, int max)
{
	SfxBank* bank = HQSfxPlain();

	if (!bank || !samples || max <= 0)
		return 0;

	const int info = SfxBankLut(bank, soundId);

	if (info < 0)
		return 0;

	const int first = SfxBankInfoNumber(bank, info);
	int takes = SfxBankInfoVariations(bank, info);
	int written = 0;

	if (takes < 1)
		takes = 1;

	for (int i = 0; i < takes && written < max; i++)
	{
		const long s = first + i;

		if (s < 0 || s >= SfxBankSamples(bank))
			continue;

		samples[written++] = s;
	}

	return written;
}

/* One of those samples, checked by the same gate every payload goes
 * through. The buffer is the bank's own and is valid until the next call on
 * that bank, exactly as HQSfxPayload's is. */
char* HQSfxBankSample(long sample, long* riffBytes, SfxRiffParts* parts)
{
	SfxBank* bank = HQSfxPlain();
	long bytes = 0, dataLen = 0;
	const char* why = "";
	SfxRiffParts where;

	if (riffBytes)
		*riffBytes = 0;

	if (!bank || sample < 0 || sample >= SfxBankSamples(bank))
		return 0;

	char* buffer = (char*)SfxBankSample(bank, (int)sample, &bytes);

	if (!buffer || !SfxRiffCheck(buffer, bytes, &dataLen, 0, &why, &where))
	{
		Log("HQ SFX: bank sample %d - %s", (int)sample, why);
		return 0;
	}

	if (riffBytes)
		*riffBytes = bytes;

	if (parts)
		*parts = where;

	return buffer;
}

long HQSfxSubstituted()
{
	return hq_substituted;
}

long HQSfxOverlaid()
{
	return hq_overlaid;
}

long HQSfxReplaced()
{
	return hq_replaced;
}

const char* HQSfxSource()
{
	return hq_source;
}

bool HQSfxHaveBank()
{
	return hq_ready;
}
