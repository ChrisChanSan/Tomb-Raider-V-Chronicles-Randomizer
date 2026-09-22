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
#include "secretreward.h"
#include "objects.h"
#include "gameflow.h"
#include "../specific/function_stubs.h"

#define SECRET_REWARD_FILE	"SecretRewards.txt"

/*
 * What a level awards for finding every secret in it.
 *
 * This used to be a table compiled into the game (newinv2.cpp), which was
 * fine while it was one fixed reward. The randomizer rolls a set of four to
 * eight items, drawn from weapons, ammunition and medipacks, and a player
 * may want to set it himself. Neither can reach a table inside the
 * executable.
 *
 * SCRIPT.DAT would have been the other candidate and is deliberately not
 * used: its layout is Core's, fixed and undocumented, and writing our own
 * fields into it would cost the compatibility that lets any vanilla
 * SCRIPT.DAT still work. Balance.txt already set the precedent for a plain
 * file beside the executable, and this follows it line for line: a missing
 * file is written from the defaults, a file that lacks lines is completed
 * after reading, and the player's own numbers survive that.
 *
 * What may be a reward is the list kRewardNames below and nothing else -
 * that list is the whole permission, so there is only ever one place to
 * change it. Anything else in the file is refused by name in the log rather
 * than silently dropped.
 */

struct RewardName
{
	const char* name;
	short object_number;
};

/* Everything the reward may contain, and nothing else. This is the guard -
 * a name or a number outside this list is refused.
 *
 * Left out on purpose: pistols and their ammo (Lara always has them), the
 * whole crossbow family, lasersight, binoculars and silencer, and both
 * flares. Some would cause trouble, the rest are simply pointless as a
 * prize. What is left is what a player is actually pleased to find.
 *
 * Keys and puzzle items were never in it: handing out a key the level did
 * not intend opens a door out of order, and in a randomized run that can
 * break the way through entirely. */
static const RewardName kRewardNames[] =
{
	{ "Uzi",              UZI_ITEM            },
	{ "Uzi_Ammo",         UZI_AMMO_ITEM       },
	{ "Shotgun",          SHOTGUN_ITEM        },
	{ "Shotgun_Ammo",     SHOTGUN_AMMO1_ITEM  },
	{ "Shotgun_Wideshot", SHOTGUN_AMMO2_ITEM  },
	{ "HK",               HK_ITEM             },
	{ "HK_Ammo",          HK_AMMO_ITEM        },
	{ "Revolver",         REVOLVER_ITEM       },
	{ "Revolver_Ammo",    REVOLVER_AMMO_ITEM  },
	{ "Big_Medipack",     BIGMEDI_ITEM        },
	{ "Small_Medipack",   SMALLMEDI_ITEM      },
	{ 0,                  0                   }
};

struct RewardLevel
{
	const char* name;
	long id;
};

/* Only the fourteen levels that are really played. The title screen and the
 * three development levels have no secrets and get no line. */
static const RewardLevel kRewardLevels[] =
{
	{ "Streets_of_Rome",      LVL5_STREETS_OF_ROME      },
	{ "Trajan_Markets",       LVL5_TRAJAN_MARKETS       },
	{ "Colosseum",            LVL5_COLOSSEUM            },
	{ "Base",                 LVL5_BASE                 },
	{ "Submarine",            LVL5_SUBMARINE            },
	{ "Deepsea_Dive",         LVL5_DEEPSEA_DIVE         },
	{ "Sinking_Submarine",    LVL5_SINKING_SUBMARINE    },
	{ "Gallows_Tree",         LVL5_GALLOWS_TREE         },
	{ "Labyrinth",            LVL5_LABYRINTH            },
	{ "Old_Mill",             LVL5_OLD_MILL             },
	{ "Thirteenth_Floor",     LVL5_THIRTEENTH_FLOOR     },
	{ "Escape_with_the_Iris", LVL5_ESCAPE_WITH_THE_IRIS },
	{ "Security_Breach",      LVL5_SECURITY_BREACH      },
	{ "Red_Alert",            LVL5_RED_ALERT            },
	{ 0,                      0                         }
};

/* The reward as the game shipped it - and what a fresh file is written
 * from, so a player who deletes his own gets the original back. */
static const SECRET_REWARD_ITEM kDefaultReward[] =
{
	{ BIGMEDI_ITEM,       1 },
	{ SMALLMEDI_ITEM,     2 },
	{ REVOLVER_AMMO_ITEM, 1 },
	{ UZI_AMMO_ITEM,      1 }
};
#define DEFAULT_COUNT	(sizeof(kDefaultReward) / sizeof(kDefaultReward[0]))

static SECRET_REWARD_ITEM rewardTable[NUM_TR5_LEVELS][SECRET_REWARD_MAX];
static long rewardCount[NUM_TR5_LEVELS];
static long rewardFound[NUM_TR5_LEVELS];

static const char* RewardNameOf(short object_number)
{
	for (int i = 0; kRewardNames[i].name; i++)
		if (kRewardNames[i].object_number == object_number)
			return kRewardNames[i].name;

	return 0;
}

/* Case-insensitive, and '-' reads as '_' so nobody is caught out by a dash. */
static int RewardNameEquals(const char* a, const char* b)
{
	while (*a && *b)
	{
		char ca = *a;
		char cb = *b;

		if (ca >= 'A' && ca <= 'Z') ca = char(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = char(cb + 32);
		if (ca == '-') ca = '_';
		if (cb == '-') cb = '_';

		if (ca != cb)
			return 0;

		a++;
		b++;
	}

	return !*a && !*b;
}

static void RewardSetDefaults()
{
	for (int lvl = 0; lvl < NUM_TR5_LEVELS; lvl++)
	{
		rewardCount[lvl] = 0;
		rewardFound[lvl] = 0;
	}

	for (int i = 0; kRewardLevels[i].name; i++)
	{
		const long lvl = kRewardLevels[i].id;

		for (int k = 0; k < (int)DEFAULT_COUNT; k++)
			rewardTable[lvl][k] = kDefaultReward[k];

		rewardCount[lvl] = (long)DEFAULT_COUNT;
	}
}

static void RewardWriteText(HANDLE file, const char* text)
{
	DWORD written;

	if (file != INVALID_HANDLE_VALUE)
		WriteFile(file, text, (DWORD)strlen(text), &written, 0);
}

static void RewardWriteFile(long fresh)
{
	HANDLE file;
	char line[512];
	char part[64];

	file = CreateFile(SECRET_REWARD_FILE, GENERIC_WRITE, 0, 0,
		fresh ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

	if (file == INVALID_HANDLE_VALUE)
		return;

	RewardWriteText(file,
		"; What finding EVERY secret in a level awards.\r\n"
		";\r\n"
		"; One line per level:\r\n"
		";     Level_Name = Item x Count, Item x Count, ...\r\n"
		";\r\n"
		"; The count may be left out and is then 1. Names are not case\r\n"
		"; sensitive and a dash reads like an underscore. A raw object\r\n"
		"; number works too, if you prefer.\r\n"
		";\r\n"
		"; Only the names listed below may be used. Anything else is\r\n"
		"; refused and said so in the log - keys and puzzle items above\r\n"
		"; all: handing out a key the level did not intend opens a door\r\n"
		"; out of order, and in a randomized run that can break the way\r\n"
		"; through entirely.\r\n"
		";\r\n"
		"; Names:\r\n");

	line[0] = 0;

	for (int i = 0; kRewardNames[i].name; i++)
	{
		if (!(i % 4))
		{
			if (line[0])
			{
				strcat(line, "\r\n");
				RewardWriteText(file, line);
			}

			strcpy(line, ";   ");
		}

		_snprintf(part, sizeof(part) - 1, "%-22s", kRewardNames[i].name);
		part[sizeof(part) - 1] = 0;
		strcat(line, part);
	}

	if (line[0])
	{
		strcat(line, "\r\n");
		RewardWriteText(file, line);
	}

	RewardWriteText(file, ";\r\n\r\n");

	for (int i = 0; kRewardLevels[i].name; i++)
	{
		const long lvl = kRewardLevels[i].id;

		_snprintf(line, sizeof(line) - 1, "%-22s = ", kRewardLevels[i].name);
		line[sizeof(line) - 1] = 0;

		if (!rewardCount[lvl])
			strcat(line, "none");

		for (long k = 0; k < rewardCount[lvl]; k++)
		{
			const char* nm = RewardNameOf(rewardTable[lvl][k].object_number);

			if (nm)
				_snprintf(part, sizeof(part) - 1, "%s%s x %d",
					k ? ", " : "", nm, rewardTable[lvl][k].count);
			else
				_snprintf(part, sizeof(part) - 1, "%s%d x %d",
					k ? ", " : "", rewardTable[lvl][k].object_number,
					rewardTable[lvl][k].count);

			part[sizeof(part) - 1] = 0;
			strcat(line, part);
		}

		strcat(line, "\r\n");
		RewardWriteText(file, line);
	}

	CloseHandle(file);
	Log(fresh ? "SecretRewards.txt newly created" : "SecretRewards.txt completed");
}

/* One "Item x Count" piece. Returns 0 if it is not usable. */
static int RewardReadItem(char* text, SECRET_REWARD_ITEM* out)
{
	char name[64];
	char* p;
	int n;
	long value;

	while (*text == ' ' || *text == '\t')
		text++;

	n = 0;

	while (*text && *text != 'x' && *text != 'X' && *text != ',' && n < 63)
		name[n++] = *text++;

	while (n && (name[n - 1] == ' ' || name[n - 1] == '\t'))
		n--;

	name[n] = 0;

	if (!n)
		return 0;

	out->count = 1;

	if (*text == 'x' || *text == 'X')
	{
		text++;
		out->count = (short)strtol(text, &p, 10);

		if (out->count < 1)
			out->count = 1;
	}

	/* A name first, a raw number if it is not one. */
	for (int i = 0; kRewardNames[i].name; i++)
		if (RewardNameEquals(name, kRewardNames[i].name))
		{
			out->object_number = kRewardNames[i].object_number;
			return 1;
		}

	value = strtol(name, &p, 10);

	if (p == name)
	{
		Log("SecretRewards.txt: \"%s\" is no item - skipped", name);
		return 0;
	}

	/* A raw number must still be one of the allowed items. The list above
	 * is the whole permission, so there is only ever one place to change
	 * what a reward may be. */
	for (int i = 0; kRewardNames[i].name; i++)
		if (kRewardNames[i].object_number == (short)value)
		{
			out->object_number = (short)value;
			return 1;
		}

	Log("SecretRewards.txt: object %ld is not allowed as a reward "
		"- see the list of names in the file", value);
	return 0;
}

static void RewardReadLine(char* line)
{
	char name[64];
	char* eq;
	char* p;
	int n;

	/* Comments and blank lines. */
	for (p = line; *p == ' ' || *p == '\t'; p++);

	if (!*p || *p == ';' || *p == '#')
		return;

	eq = strchr(line, '=');

	if (!eq)
		return;

	n = 0;

	for (p = line; p < eq && n < 63; p++)
		if (*p != ' ' && *p != '\t')
			name[n++] = *p;

	name[n] = 0;

	for (int i = 0; kRewardLevels[i].name; i++)
	{
		if (!RewardNameEquals(name, kRewardLevels[i].name))
			continue;

		const long lvl = kRewardLevels[i].id;
		long count = 0;
		char* item = eq + 1;

		rewardFound[lvl] = 1;

		/* "none" empties the line - a level that awards nothing. */
		for (p = item; *p == ' ' || *p == '\t'; p++);

		if (RewardNameEquals(p, "none"))
		{
			rewardCount[lvl] = 0;
			return;
		}

		while (*item && count < SECRET_REWARD_MAX)
		{
			char* comma = strchr(item, ',');

			if (comma)
				*comma = 0;

			if (RewardReadItem(item, &rewardTable[lvl][count]))
				count++;

			if (!comma)
				break;

			item = comma + 1;
		}

		rewardCount[lvl] = count;
		return;
	}

	Log("SecretRewards.txt: \"%s\" is no level - skipped", name);
}

void SecretRewardLoad()
{
	HANDLE file;
	DWORD size;
	DWORD bytesRead;
	char* buffer;
	char* line;
	long complete;

	RewardSetDefaults();

	file = CreateFile(SECRET_REWARD_FILE, GENERIC_READ, FILE_SHARE_READ, 0,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (file == INVALID_HANDLE_VALUE)
	{
		RewardWriteFile(1);
		return;
	}

	size = GetFileSize(file, 0);

	if (size == INVALID_FILE_SIZE || size > 0x20000)
		size = 0x20000;

	buffer = (char*)malloc(size + 1);

	if (!buffer)
	{
		CloseHandle(file);
		return;
	}

	bytesRead = 0;
	ReadFile(file, buffer, size, &bytesRead, 0);
	CloseHandle(file);
	buffer[bytesRead] = 0;

	line = buffer;

	/* Notepad likes to save UTF-8 with a signature - skip it. */
	if (bytesRead >= 3 && (uchar)line[0] == 0xEF && (uchar)line[1] == 0xBB && (uchar)line[2] == 0xBF)
		line += 3;

	while (*line)
	{
		char* next = line;

		while (*next && *next != '\r' && *next != '\n')
			next++;

		if (*next)
		{
			*next = 0;
			next++;

			while (*next == '\r' || *next == '\n')
				next++;
		}

		RewardReadLine(line);
		line = next;
	}

	free(buffer);

	/* A file from an older version would otherwise hide every level added
	 * later for good. The player's numbers are kept, his comments are not -
	 * the same bargain Balance.txt makes. */
	complete = 1;

	for (int i = 0; kRewardLevels[i].name; i++)
		if (!rewardFound[kRewardLevels[i].id])
			complete = 0;

	if (!complete)
		RewardWriteFile(0);
}

const SECRET_REWARD_ITEM* SecretRewardFor(long level, long* count)
{
	if (count)
		*count = 0;

	if (level < 0 || level >= NUM_TR5_LEVELS || !rewardCount[level])
		return 0;

	if (count)
		*count = rewardCount[level];

	return rewardTable[level];
}
