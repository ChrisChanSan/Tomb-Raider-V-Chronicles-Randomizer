/*
 * SfxProbe - which sound bank the engine would open for a game folder, what
 * the bridge between a level's sample numbers and that bank's looks like, and
 * which loose .wav files a mod puts over it.
 *
 * Modified 2026 by ChrisChanSan.
 *
 * THE POINT OF THIS TOOL (#153, round M1). The HQ sound bank used to be one
 * file in one place - audio\MAIN.SFX, put there by hand - and "is it there"
 * was the whole of the question. Round M1 made it a chain of three, gave a
 * mod the right to put its own recordings over whichever of them was opened,
 * and hung a plausibility rule on each substitution. None of that is visible
 * in the game: a sound that quietly kept Core's version sounds exactly like a
 * sound nobody tried to replace.
 *
 * So this tool walks the same chain the engine walks, through the same
 * parser - specific\sfxbank.cpp, linked here, not copied - and says out loud
 * what the engine would do silently.
 *
 * THE SAME CHAIN LITERALLY, not the same chain by agreement: the order of the
 * candidates comes out of SfxBankChainLink in that shared file, which is the
 * call HQSfxOpen makes as well. The first cut of this tool kept its own copy
 * of the order, and a tool's copy of a rule is what drifts - ModelProbe's
 * page rule had drifted exactly that way and this round had to fix it. There
 * is nothing left here to keep in step by hand.
 *
 * USAGE
 *
 *   SfxProbe <game folder> [options]
 *
 *     <game folder>  the folder PCtomb5.exe runs in: the one with audio\,
 *                    data\ and Mods\ in it.
 *
 *   Options:
 *     -mods <folder>   the Mods folder, when it is not <game folder>\Mods.
 *                      "-mods none" scans none at all, which is what the
 *                      engine sees on a machine without the folder.
 *     -install <path>  the remaster's game 5 folder, instead of asking the
 *                      registry and Steam for it. This is the third link of
 *                      the chain - the bank read where it lies, which is what
 *                      makes the hand copy into audio\ unnecessary.
 *     -id <n>          print the bridge for that sound id as well. Repeatable.
 *                      Without any, 0 (LARA_FEET) and 4 (the landing) are
 *                      printed, which are the two the footstep mods replace.
 *     -all-ids         every sound id the chosen bank names, as a table.
 *     -wavs            the loose .wav overlay alone, without the bank tables.
 *     -samples         push every sample of the chosen bank (and of the
 *                      ruler, when there is one) through SfxRiffCheck - the
 *                      engine's own gate - and count what it refuses. A bank
 *                      the engine would silently fall back from on some of
 *                      its sounds shows up here and nowhere else. It also
 *                      counts the samples whose format block is not the first
 *                      chunk; see below.
 *
 * WHERE THE FORMAT BLOCK LIES is printed for every accepted file, because it
 * is not always +20. The gate WALKS the chunks, so a wav with a JUNK block in
 * front of fmt - the RF64 headroom Reaper, Pro Tools and libsndfile all write
 * - passes it, and so does one with a LIST holding the title. The engine used
 * to read the format tag at a flat offset 20, find two bytes of that first
 * block's header there, and drop the file without a word while its own log
 * said the mod had supplied the sound. It takes the offset from this same
 * check now (#153, round M1, second fix round), so a "layout" line that does
 * not say +20 marks a file that only plays on a build from that round on.
 *
 * Everything goes to stdout, so a run can be pasted into a report as it
 * stands.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../../TOMB5 Code/TOMB5/specific/sfxbank.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_install.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_mods.h"

/* hd_install.cpp reads the two registry overrides out of these, which the
 * engine defines in tomb5\tomb5.cpp. A tool that links hd_install.cpp has to
 * bring its own pair - TrgProbe does the same. Left empty, so the search runs
 * exactly as it does for a player who never set the registry value. */
char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];

static void ProbeModsLine(const char* line)
{
	printf("  %s\n", line);
}

static std::string Join(const std::string& a, const char* b)
{
	std::string out = a;

	while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
		out.erase(out.size() - 1);

	out += '\\';
	out += b;
	return out;
}

/* One candidate of the chain, tried and reported exactly as HQSfxOpen tries
 * it. Returns the bank when it opened, and the caller keeps the first one. */
static SfxBank* Try(const char* path, const char* label, bool keep,
	std::string* chosen)
{
	const char* why = "";

	if (!path || !*path)
	{
		printf("  %-28s  -  there is none to look at\n", label);
		return 0;
	}

	SfxBank* bank = SfxBankOpen(path, &why);

	if (!bank)
	{
		printf("  %-28s  NOT opened: %s\n", label, why);
		printf("  %-28s     %s\n", "", path);
		return 0;
	}

	printf("  %-28s  opened: %d samples in %d sound ids, %d infos, "
		"largest %ld bytes\n", label, SfxBankSamples(bank),
		SfxBankIdsUsed(bank), SfxBankInfos(bank), SfxBankLargest(bank));
	printf("  %-28s     %s\n", "", path);

	if (!keep)
	{
		printf("  %-28s     (would not be reached - an earlier link answered)\n", "");
		SfxBankClose(bank);
		return 0;
	}

	*chosen = label;
	return bank;
}

// The two numbers of a RIFF a comparison needs, or 0 when it is not one.
static long dataOf(const char* riff, long bytes)
{
	long dataBytes = 0;

	return SfxRiffCheck(riff, bytes, &dataBytes, 0, 0) ? dataBytes : 0;
}

static long rateOf(const char* riff, long bytes)
{
	long byteRate = 0;

	return SfxRiffCheck(riff, bytes, 0, &byteRate, 0) ? byteRate : 0;
}

// The format of one RIFF, in words, for a table.
static void RiffLine(const char* riff, long bytes, const char* lead)
{
	long dataBytes = 0, byteRate = 0;
	const char* why = "";

	if (!SfxRiffCheck(riff, bytes, &dataBytes, &byteRate, &why))
	{
		printf("%s%ld bytes - REFUSED: %s\n", lead, bytes, why);
		return;
	}

	printf("%s%ld bytes, %ld of them audio, %ld B/s -> %.3f s\n", lead, bytes,
		dataBytes, byteRate, (double)dataBytes / (double)byteRate);
}

/* WHERE THE ENGINE WILL READ IT (#153, round M1, second fix round).
 *
 * The gate walks the chunks, so it accepts a file whose fmt block is not the
 * first one - a JUNK block for RF64 headroom, or a LIST with the title, both
 * of which editors write as a matter of course. LoadSamples() used to read
 * the format tag at a fixed offset 20 and so quietly threw such a file away
 * after this tool had called it accepted. It takes these offsets now, out of
 * the same check, which is why they are worth printing: a line that does not
 * say +20 is a file the old build would have dropped in silence. */
static void PartsLine(const char* riff, long bytes, const char* lead)
{
	SfxRiffParts parts;

	if (!SfxRiffCheck(riff, bytes, 0, 0, 0, &parts))
		return;

	printf("%sformat block at +%ld (%ld bytes), audio at +%ld%s\n", lead,
		parts.fmtAt, parts.fmtBytes, parts.dataAt,
		parts.fmtAt == 20 ? "" : "  <- fmt is not the first chunk");
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: SfxProbe <game folder> [-mods <folder>] "
			"[-install <5-folder>] [-id <n>] [-all-ids] [-wavs] [-samples]\n"
			"       see the head of SfxProbe.cpp for the whole list\n");
		return 2;
	}

	const std::string game = argv[1];
	std::string mods = Join(game, "Mods");
	std::string install;
	std::vector<int> ids;
	bool allIds = false, wavsOnly = false, samples = false;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-mods") && i + 1 < argc)
			mods = argv[++i];
		else if (!_stricmp(argv[i], "-install") && i + 1 < argc)
			install = argv[++i];
		else if (!_stricmp(argv[i], "-id") && i + 1 < argc)
			ids.push_back(atoi(argv[++i]));
		else if (!_stricmp(argv[i], "-all-ids"))
			allIds = true;
		else if (!_stricmp(argv[i], "-wavs"))
			wavsOnly = true;
		else if (!_stricmp(argv[i], "-samples"))
			samples = true;
		else
			printf("SfxProbe: ignoring %s\n", argv[i]);
	}

	if (ids.empty())
	{
		ids.push_back(0);	// LARA_FEET
		ids.push_back(4);	// the landing
	}

	printf("game    : %s\n", game.c_str());

	/* --- the mods ------------------------------------------------------- */
	printf("\n-- the mods folder -------------------------------------------\n");

	if (_stricmp(mods.c_str(), "none") == 0)
		printf("  none scanned - the switch says so\n");
	else
	{
		HDModsFolder(mods.c_str());
		HDModsLogSink(ProbeModsLine);
		HDModsScan();
	}

	/* --- the chain ------------------------------------------------------ */
	printf("\n-- the bank chain, in the order HQSfxOpen walks it ------------\n");

	/* The number is the link's POSITION in this machine's chain, not a fixed
	 * tier: with no mod bank installed, audio\MAIN.SFX is link 1 and not
	 * link 2. It moved with the shared order (#153, round M1, fix round) and
	 * it is worth saying, because an older run of this tool printed the tier
	 * and the two are easy to mistake for a changed chain. */
	printf("  (numbered by position - the first mod bank, if any, is 1)\n");

	SfxBank* bank = 0;
	std::string chosen;
	int chosenMod = -1;		// the mod whose bank answered, or -1
	std::vector<int> plainLinks;	// the links no mod owns, for the ruler

	/* THE ENGINE'S OWN CHAIN, not a copy of it (#153, round M1, fix round).
	 * This tool used to hold its own ordering - mods, then audio\, then the
	 * install - which is exactly the arrangement that let ModelProbe's page
	 * rule drift out of step with the cache until this round caught it. The
	 * order now comes from SfxBankChainLink in specific\sfxbank.cpp, the same
	 * call HQSfxOpen makes, so a later round that reorders the chain reorders
	 * what this tool prints with it. */
	for (int link = 0; ; link++)
	{
		char path[512], label[512], shown[560];
		int mod = -1;

		if (!SfxBankChainLink(link, game.c_str(),
			install.empty() ? 0 : install.c_str(),
			path, sizeof(path), label, sizeof(label), &mod))
			break;

		if (mod < 0)
			plainLinks.push_back(link);

		snprintf(shown, sizeof(shown), "%d. %.500s", link + 1, label);

		SfxBank* got = Try(path, shown, bank == 0, &chosen);

		if (got)
		{
			bank = got;
			chosenMod = mod;
		}
	}

	if (!bank)
	{
		printf("\nno bank anywhere - the level plays its own samples, and a\n"
			"loose .wav has no bridge to hang on either.\n");
		return 1;
	}

	printf("\nchosen  : %s\n", chosen.c_str());

	/* --- the ruler ------------------------------------------------------ *
	 *
	 * When the chosen bank is a mod's, the engine opens the first UNMODDED
	 * one beside it and measures the pairing against that instead - the
	 * whole of the reason is in hqsfx.cpp at the plausibility rule. This
	 * section is what that decision rests on, per sound id: how long the
	 * recording is in each of the two banks. A row where they differ is a
	 * sample the mod really replaced, and it is exactly the row the old rule
	 * would have thrown away. */
	SfxBank* ruler = 0;
	std::string rulerName;

	if (chosenMod >= 0)
	{
		/* The plain links of the same chain, in the same order - which is
		 * what the engine walks on for the ruler once a mod's bank has
		 * answered. Collected above rather than rebuilt here, so this half
		 * of the tool has no copy of the order either. */
		for (size_t k = 0; k < plainLinks.size() && !ruler; k++)
		{
			char path[512], label[512];
			const char* why = "";
			int mod = -1;

			if (!SfxBankChainLink(plainLinks[k], game.c_str(),
				install.empty() ? 0 : install.c_str(),
				path, sizeof(path), label, sizeof(label), &mod))
				continue;

			ruler = SfxBankOpen(path, &why);

			if (ruler)
				rulerName = label;
		}

		printf("ruler   : %s\n", ruler ? rulerName.c_str()
			: "none - a sample the mod made longer would keep the level's own");
	}

	/* --- every sample through the engine's own gate ---------------------- *
	 *
	 * WHY THIS EXISTS (#153, round M1, fix round). HQSfxPayload used to judge
	 * a bank's sample with a hand-rolled walk of the RIFF chunks that had no
	 * bound on the lengths it read; it asks SfxRiffCheck now, which is the
	 * same gate a loose .wav goes through and is bounds-checked. That is a
	 * STRICTER gate than what stood there, so it has to be shown that the
	 * player's real bank still passes it whole - a sound that quietly falls
	 * back to Core's sample is exactly the kind of regression nobody hears
	 * until much later. Every sample, not only the first take of each sound:
	 * the bridge can land on any of them. */
	if (samples)
	{
		int refused = 0;
		int notFirst = 0;

		printf("\n-- every sample through SfxRiffCheck, the engine's own gate ---\n");

		for (int s = 0; s < SfxBankSamples(bank); s++)
		{
			long bytes = 0;
			const char* riff = SfxBankSample(bank, s, &bytes);
			const char* why = "";
			SfxRiffParts parts;

			if (riff && SfxRiffCheck(riff, bytes, 0, 0, &why, &parts))
			{
				/* And where the gate found the format block. A sample whose
				 * fmt is not the first chunk is one the build before the
				 * second fix round would have accepted here and then thrown
				 * away in LoadSamples(), which read offset 20 flat. */
				if (parts.fmtAt != 20)
				{
					notFirst++;
					printf("  sample %-5d passes, but its format block is at "
						"+%ld, not +20\n", s, parts.fmtAt);
				}

				continue;
			}

			refused++;
			printf("  sample %-5d REFUSED: %s\n", s, riff ? why : "could not be read");
		}

		printf("  %d of %d samples pass, %d refused, %d with the format block "
			"somewhere other than +20\n", SfxBankSamples(bank) - refused,
			SfxBankSamples(bank), refused, notFirst);

		if (ruler)
		{
			int rulerRefused = 0;
			int rulerNotFirst = 0;

			for (int s = 0; s < SfxBankSamples(ruler); s++)
			{
				long bytes = 0;
				const char* riff = SfxBankSample(ruler, s, &bytes);
				const char* why = "";
				SfxRiffParts parts;

				if (riff && SfxRiffCheck(riff, bytes, 0, 0, &why, &parts))
				{
					if (parts.fmtAt != 20)
						rulerNotFirst++;

					continue;
				}

				rulerRefused++;
				printf("  ruler sample %-5d REFUSED: %s\n", s,
					riff ? why : "could not be read");
			}

			printf("  ruler: %d of %d pass, %d refused, %d with the format "
				"block somewhere other than +20\n",
				SfxBankSamples(ruler) - rulerRefused, SfxBankSamples(ruler),
				rulerRefused, rulerNotFirst);
		}
	}

	/* --- the bridge ----------------------------------------------------- */
	if (!wavsOnly)
	{
		printf("\n-- the bridge: sound id -> bank sample ------------------------\n");
		printf("  %-6s %-6s %-8s %-6s %s\n", "id", "info", "sample", "takes",
			"the first take");

		if (allIds)
		{
			ids.clear();

			for (int id = 0; id < SFX_LUT_ENTRIES; id++)
				if (SfxBankLut(bank, id) >= 0)
					ids.push_back(id);
		}

		for (size_t i = 0; i < ids.size(); i++)
		{
			const int id = ids[i];
			const int info = SfxBankLut(bank, id);

			if (info < 0)
			{
				printf("  %-6d %s\n", id, "- this bank has no such sound");
				continue;
			}

			const int first = SfxBankInfoNumber(bank, info);
			const int takes = SfxBankInfoVariations(bank, info);
			long bytes = 0;
			const char* riff = SfxBankSample(bank, first, &bytes);

			printf("  %-6d %-6d %-8d %-6d ", id, info, first, takes);

			if (riff)
				RiffLine(riff, bytes, "");
			else
				printf("could not be read\n");

			/* The same sound through the ruler's OWN lookup table, which is
			 * how the engine finds it: the two banks have different sample
			 * counts, so the number is not a common language and the sound id
			 * is. */
			if (ruler)
			{
				const int refInfo = SfxBankLut(ruler, id);
				long refBytes = 0, refData = 0, refRate = 0;
				const char* refRiff = refInfo >= 0
					? SfxBankSample(ruler, SfxBankInfoNumber(ruler, refInfo), &refBytes)
					: 0;

				if (!refRiff || !SfxRiffCheck(refRiff, refBytes, &refData, &refRate, 0))
					printf("  %-6s %-6s %-8s %-6s the ruler has no such sound - "
						"the pairing cannot be confirmed\n", "", "", "", "");
				else
				{
					const double mine = (double)dataOf(riff, bytes);
					const double rate = (double)rateOf(riff, bytes);
					const double secs = rate > 0 ? mine / rate : 0.0;
					const double refSecs = (double)refData / (double)refRate;

					printf("  %-6s %-6s %-8d %-6s ruler: %ld bytes -> %.3f s",
						"", "", SfxBankInfoNumber(ruler, refInfo), "",
						refData, refSecs);

					if (refData == dataOf(riff, bytes))
						printf("   same recording\n");
					else
						printf("   REPLACED (%.2fx) - the old rule alone would "
							"refuse it, the ruler lets it through\n",
							refSecs > 0 ? secs / refSecs : 0.0);
				}
			}
		}
	}

	/* --- the overlay ---------------------------------------------------- */
	printf("\n-- the loose .wav overlay ------------------------------------\n");
	printf("  a mod's <NNNN>.wav replaces BANK sample NNNN. The verdict is the\n"
		"  engine's own: SfxRiffCheck, the same call HQSfxPayload makes.\n");

	int found = 0;

	for (int number = 0; number < SfxBankSamples(bank); number++)
	{
		char path[512];
		int mod = -1;

		if (!*HDModsWav(number, &mod, path, sizeof(path)))
			continue;

		found++;

		long wavBytes = 0, wavData = 0, wavRate = 0;
		const char* why = "";
		char* wav = SfxWavLoad(path, 4 * 1024 * 1024, &wavBytes, &why);

		printf("\n  sample %04d  <- mod%d %s\n", number, mod, HDModsName(mod));
		printf("    file       %s\n", path);

		if (!wav)
		{
			printf("    verdict    REFUSED: %s\n", why);
			continue;
		}

		const bool ok = SfxRiffCheck(wav, wavBytes, &wavData, &wavRate, &why);

		if (!ok)
		{
			printf("    verdict    REFUSED: %s\n", why);
			free(wav);
			continue;
		}

		printf("    the wav    ");
		RiffLine(wav, wavBytes, "");
		PartsLine(wav, wavBytes, "    layout     ");

		long bankBytes = 0, bankData = 0, bankRate = 0;
		const char* riff = SfxBankSample(bank, number, &bankBytes);

		if (riff && SfxRiffCheck(riff, bankBytes, &bankData, &bankRate, 0))
		{
			printf("    the bank   ");
			RiffLine(riff, bankBytes, "");
			printf("    ratio      %.2fx as long as the bank's own recording\n",
				((double)wavData / (double)wavRate) /
				((double)bankData / (double)bankRate));
		}

		printf("    verdict    ACCEPTED - handed to the voice instead of the "
			"bank's\n");
		free(wav);
	}

	if (!found)
		printf("\n  no <NNNN>.wav under any active mod\n");

	SfxBankClose(ruler);
	SfxBankClose(bank);
	return 0;
}
