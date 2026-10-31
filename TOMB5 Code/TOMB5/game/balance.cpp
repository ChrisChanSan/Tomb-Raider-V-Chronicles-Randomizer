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
#include "balance.h"
#include "larafire.h"
#include "gameflow.h"
#include "objects.h"
#include "../specific/function_stubs.h"

#define BALANCE_FILE	"Balance.txt"

BALANCE_INFO balance;

/*
 * One line of the tuning file. This table is the single source of truth:
 * default, allowed range, the template text and - for hit points - the
 * object slot all live here. The template is generated from it, so it can
 * never disagree with the defaults.
 *
 * name == 0 means: no setting, only the template text in header.
 * slot >= 0 means: the value belongs into objects[slot].hit_points.
 */
struct BALANCE_LINE
{
	const char* name;
	long* value;
	long standard;   /* what the game uses when the line is missing */
	long vanilla;    /* what Core shipped - the self-check and the comment */
	long min;
	long max;
	short slot;
	const char* header;
	const char* note;
};

/*
 * s = the standard this build ships with. It is what a missing line falls
 *     back to and what a fresh Balance.txt is written from.
 * v = what Core shipped. Only two things read it: the comment in the
 *     generated file, so a player can see how far the defaults moved, and
 *     the self-check in BalanceSetEnemies, which compares against
 *     setup.cpp. Putting the current standard there instead would make
 *     that check fire for every enemy retuned away from Core's value and
 *     drown the one case it exists for.
 */
#define SEPARATOR(text)	{ 0, 0, 0, 0, 0, 0, -1, text, 0 }
#define WEAPON(n, f, s, v, hint)	{ n, &balance.f, s, v, 0, 127, -1, 0, hint }
#define HEALTH(n, f, s, v, place)	{ n, &balance.f, s, v, 1, 32000, place, 0, 0 }
#define DAMAGE(n, f, s, v, hint)	{ n, &balance.f, s, v, 0, 32000, -1, 0, hint }

static const BALANCE_LINE balanceLines[] =
{
	SEPARATOR(
		"; ---------------------------------------------------------------\r\n"
		";  Balance.txt - fine tuning for Tomb Raider Chronicles\r\n"
		";  Part of the TR5 Randomizer. Edit, save, restart the game.\r\n"
		"; ---------------------------------------------------------------\r\n"
		";  Format:  Name = number\r\n"
		";  Lines starting with ; or # are comments.\r\n"
		";  If a line is missing, the default in brackets applies.\r\n"
		";  If the file is deleted, the game recreates it.\r\n"
		";  When a new version adds settings, the game fills them in here\r\n"
		";  and keeps your numbers. Your own comments are lost in the\r\n"
		";  process.\r\n"
		"\r\n"
		"\r\n"
		"; === Damage of Lara's weapons ===================================\r\n"
		";  Hits per shot, 0 to 127. Lara has 1000 hit points, a simple\r\n"
		";  guard 24. The shotgun counts per pellet, so it hits harder\r\n"
		";  than the number suggests.\r\n"
		"\r\n"),

	WEAPON("Pistols",		pistols,		1,	1,	0),
	WEAPON("Uzi",			uzi,			3,	1,	0),
	WEAPON("Shotgun",		shotgun,		4,	3,	"  per pellet"),
	WEAPON("Crossbow",		crossbow,		5,	5,	"  in game the Grappling Gun"),
	WEAPON("Torch",			torch,			3,	3,	0),

	SEPARATOR(
		"\r\n"
		";  The revolver has two values: the game resets it every level,\r\n"
		";  weak in the Rome levels, strong from the submarine on.\r\n"),

	WEAPON("RevolverEarly",	revolverEarly,	15,	6,	"  up to and including Colosseum"),
	WEAPON("RevolverLate",	revolverLate,	15,	15,	"  all later levels"),

	SEPARATOR(
		"\r\n"
		";  The HK also has two values: aimed from a standstill it hits\r\n"
		";  harder than fired while moving.\r\n"),

	WEAPON("HK_Standing",	hkStanding,		4,	3,	0),
	WEAPON("HK_Moving",		hkMoving,		2,	1,	0),

	SEPARATOR(
		"\r\n"
		"\r\n"
		"; === Enemy hit points ===========================================\r\n"
		";  One value per enemy, absolute as in the original. For scale:\r\n"
		";  Lara has 1000, one pistol bullet deals 1 damage.\r\n"
		"\r\n"),

	HEALTH("HP_GuardBlue",		hpGuardBlue,	35,	24,		BLUE_GUARD),
	HEALTH("HP_GuardJoby",		hpGuardJoby,	35,	24,		BLUE_GUARD2),
	HEALTH("HP_Swat",			hpSwat,			35,	24,		SWAT),
	HEALTH("HP_SwatPlus",		hpSwatPlus,		35,	24,		SWAT_PLUS),
	HEALTH("HP_Mafia",			hpMafia,		35,	24,		MAFIA),
	HEALTH("HP_Mafia2",			hpMafia2,		35,	26,		MAFIA2),
	HEALTH("HP_Scientist",		hpScientist,	35,	24,		SCIENTIST),
	HEALTH("HP_CraneGuy",		hpCraneGuy,		35,	24,		CRANE_GUY),
	HEALTH("HP_Hitman",			hpHitman,		50,	50,		HITMAN),
	HEALTH("HP_Cyborg",			hpCyborg,		180,	50,		CYBORG),
	HEALTH("HP_Sniper",			hpSniper,		35,	35,		SNIPER),
	HEALTH("HP_Chef",			hpChef,			60,	60,		CHEF),
	HEALTH("HP_Pierre",			hpPierre,		60,	60,		PIERRE),
	HEALTH("HP_Larson",			hpLarson,		60,	60,		LARSON),
	HEALTH("HP_TwoGun",			hpTwoGun,		90,	90,		TWOGUN),
	HEALTH("HP_Dog",			hpDog,			25,	18,		DOG),
	HEALTH("HP_Huskie",			hpHuskie,		25,	24,		HUSKIE),
	HEALTH("HP_Lion",			hpLion,			50,	40,		LION),
	HEALTH("HP_Crow",			hpCrow,			30,	10,		CROW),
	HEALTH("HP_Drone",			hpDrone,		60,	60,		DRONE),
	HEALTH("HP_TRex",			hpTRex,			350,	800,	SAS),
	HEALTH("HP_Raptor",			hpRaptor,		80,	90,		REAPER),
	HEALTH("HP_Compy",			hpCompy,		25,	10,		SAILOR),
	HEALTH("HP_MiniSub",		hpMiniSub,		100,	100,	ATTACK_SUB),
	HEALTH("HP_Hydra",			hpHydra,		45,	30,		HYDRA),
	HEALTH("HP_Imp",			hpImp,			40,	12,		IMP),
	HEALTH("HP_WillOWisp",		hpWillOWisp,	1000,	256,	WILLOWISP),
	HEALTH("HP_MazeMonster",	hpMazeMonster,	90,	120,	MAZE_MONSTER),
	HEALTH("HP_SwampMonster",	hpSwampMonster,	70,	100,	GREEN_TEETH),
	HEALTH("HP_Skeleton",		hpSkeleton,		35,	100,	SKELETON),
	HEALTH("HP_Gladiator",		hpGladiator,	60,	20,		GLADIATOR),
	HEALTH("HP_RomanGod",		hpRomanGod,		190,	300,	ROMAN_GOD),
	HEALTH("HP_RomanGod2",		hpRomanGod2,	190,	300,	ROMAN_GOD2),

	SEPARATOR(
		"\r\n"
		"\r\n"
		"; === Enemy damage against Lara ==================================\r\n"
		";  One value per attack. Enemies with several attacks have\r\n"
		";  several lines - in the original those differ on purpose, a\r\n"
		";  dog's jump hurts more than its bite.\r\n"
		"\r\n"
		";  Firearms of the human enemies:\r\n"),

	DAMAGE("DMG_Soldier_Early",	dmgSoldierEarly,	30,	30,		"  guard/SWAT/mafia, up to Old Mill"),
	DAMAGE("DMG_Soldier_Late",	dmgSoldierLate,		60,	60,		"  the same from Old Mill on - twice as hard"),
	DAMAGE("DMG_Soldier_Short",	dmgSoldierShort,	10,	10,		"  their weaker second shot"),
	DAMAGE("DMG_Hitman_Shot",	dmgHitmanShot,		15,	12,		0),
	/*
	 * The pool cyborg uses the same gun as the line above but has its own
	 * number, so the enemy the roll scatters everywhere can be tuned
	 * without touching Red Alert's scripted agent. There is no Core value
	 * to hold him against - the slot did not exist in the original game -
	 * so the vanilla column repeats the standard value of 15; Core's agent
	 * used 12.
	 */
	DAMAGE("DMG_Cyborg_Shot",	dmgCyborgShot,		50,	12,		"  the pool enemy on slot 469"),
	DAMAGE("DMG_Mafia2_Shot",	dmgMafia2Shot,		35,	35,		0),
	DAMAGE("DMG_Pierre_Shot",	dmgPierreShot,		20,	20,		"  per pistol, he fires two"),
	DAMAGE("DMG_Larson_Shot",	dmgLarsonShot,		20,	20,		0),
	DAMAGE("DMG_Sniper",		dmgSniperShot,		100,	100,	"  one hit is a tenth of Lara"),

	SEPARATOR(
		"\r\n"
		";  Animals:\r\n"),

	DAMAGE("DMG_Dog_Bite",		dmgDogBite,			40,	30,		0),
	DAMAGE("DMG_Dog_Jump",		dmgDogJump,			80,	80,		0),
	DAMAGE("DMG_Dog_Chase",		dmgDogChase,		50,	50,		0),
	DAMAGE("DMG_Huskie_Bite",	dmgHuskieBite,		40,	20,		0),
	DAMAGE("DMG_Huskie_Jump",	dmgHuskieJump,		80,	10,		0),
	DAMAGE("DMG_Lion_Jump",		dmgLionJump,		200,	200,	0),
	DAMAGE("DMG_Lion_Paw",		dmgLionPaw,			90,	60,		0),
	DAMAGE("DMG_Crow",			dmgCrowStrike,		40,	20,		0),
	DAMAGE("DMG_Bats",			dmgBats,			2,	2,		"  per bat and hit"),
	DAMAGE("DMG_Spiders",		dmgSpiders,			3,	3,		"  per spider and hit"),
	DAMAGE("DMG_TRex_Kick",		dmgTRexKick,		1,	1,		"  while it stands"),
	DAMAGE("DMG_TRex_KickRun",	dmgTRexKickRun,		10,	10,		"  while it runs"),
	DAMAGE("DMG_TRex_Bite",		dmgTRexBite,		10000,	10000,	"  meant to be lethal, Lara has 1000"),
	DAMAGE("DMG_Raptor_Bite",	dmgRaptorBite,		100,	100,	0),
	DAMAGE("DMG_Raptor_Jump",	dmgRaptorJump,		100,	100,	0),
	DAMAGE("DMG_Compy_Bite",	dmgCompyBite,		100,	90,		0),

	SEPARATOR(
		"\r\n"
		";  The supernatural and heavy machinery:\r\n"),

	DAMAGE("DMG_Chef_Knife",	dmgChefKnife,		200,	150,	"  melee fighter in the enemy pool - was 80"),
	DAMAGE("DMG_Drone",			dmgDrone,			5,	5,		"  per hit of the drone, half the attack helicopter's"),
	DAMAGE("DMG_Skeleton_Strike", dmgSkeletonStrike, 400,	400,	0),
	DAMAGE("DMG_Gladiator_Strike", dmgGladiatorStrike, 200,	120,	0),
	DAMAGE("DMG_God_Strike",	dmgGodStrike,		60,	40,		"  Roman god in melee"),
	DAMAGE("DMG_God_Lightning",	dmgGodLightning,	200,	200,	"  his ranged attack"),
	DAMAGE("DMG_Hydra_Beam",	dmgHydraBeam,		150,	120,	0),
	DAMAGE("DMG_Imp_Throw",		dmgImpThrow,		100,	3,		0),
	DAMAGE("DMG_MazeMonster",	dmgMazeMonster,		250,	150,	0),
	DAMAGE("DMG_SwampMonster",	dmgSwampMonster,	250,	100,	0),
	DAMAGE("DMG_TwoGun_Plasma",	dmgTwoGunPlasma,	250,	250,	0),
	DAMAGE("DMG_Torpedo",		dmgTorpedo,			300,	200,	"  from the mini submarine"),

	SEPARATOR(
		"\r\n"
		"\r\n"
		"; === Level hazards ==============================================\r\n"
		";  These are not enemies but built-in machinery. They appear\r\n"
		";  nowhere else in the rando, and the percentage line EnemyDamage\r\n"
		";  further down leaves them alone on purpose.\r\n"
		"\r\n"),

	DAMAGE("DMG_Autogun",		dmgAutogun,			20,	20,		"  ceiling gun"),
	DAMAGE("DMG_Gunship",		dmgGunship,			10,	20,		"  attack helicopter"),
	DAMAGE("DMG_Iris",			dmgIris,			12,	12,		"  per frame while Lara stands too close"),
	DAMAGE("DMG_Area51Laser",	dmgArea51Laser,		100,	100,	0),
	DAMAGE("DMG_PowerCable",	dmgPowerCable,		10,	10,		"  the cables in the submarine base"),

	SEPARATOR(
		"\r\n"
		"\r\n"
		"; === Everything at once =========================================\r\n"
		";  Percentages. They apply ON TOP of the individual values above,\r\n"
		";  so the whole game can be made harder or softer with one number\r\n"
		";  without touching the fine tuning. 100 leaves everything as\r\n"
		";  entered. The level hazards stay untouched.\r\n"
		"\r\n"
		";  These three lines are also set by the randomizer through its\r\n"
		";  sliders. It touches nothing else in this file.\r\n"
		"\r\n"),

	{ "WeaponDamage",	&balance.weaponDamage,	100, 100, 0, 1000, -1, 0,
		"  on all of Lara's weapons. 0 makes them harmless" },
	{ "EnemyHealth",	&balance.enemyHealth,	100, 100, 1, 1000, -1, 0,
		"  50 = half as tough, 200 = twice as tough" },
	{ "EnemyDamage",	&balance.enemyDamage,	100, 100, 0, 1000, -1, 0,
		"  0 makes all enemies harmless" },

	SEPARATOR(
		"\r\n"
		"\r\n"
		"; === Sound ======================================================\r\n"
		";  Audible range of the fixed level scenery in percent: machines,\r\n"
		";  fire, dripping water. The original let them carry through half\r\n"
		";  the level, the randomizer reins them in. 100 restores the\r\n"
		";  original. Enemies and Lara always keep their full range.\r\n"
		"\r\n"),

	{ "SceneryRange", &balance.sceneryRange, 65, 100, 10, 400, -1, 0,
		"  the randomizer reins the level scenery in" },

	SEPARATOR(
		"\r\n"
		";  Traps, falls, fire, poison and drowning are deliberately not\r\n"
		";  tunable here. They always stay as hard as in the original,\r\n"
		";  even if you make the enemies harmless.\r\n")
};

#define BALANCE_COUNT	(sizeof(balanceLines) / sizeof(balanceLines[0]))

static void BalanceWriteText(HANDLE file, const char* text)
{
	DWORD written;
	WriteFile(file, text, (DWORD)strlen(text), &written, 0);
}

/* Which lines actually stood in the file while reading. */
static char balanceFound[BALANCE_COUNT];

/*
 * The file is generated from the table, so text and defaults can never drift
 * apart. The current values are written - for a new file those are this
 * build's standards, when backfilling they are the player's.
 */
static void BalanceWriteFile(long fresh)
{
	HANDLE file;
	char line[256];
	char from[32];

	line[sizeof(line) - 1] = 0;
	from[sizeof(from) - 1] = 0;

	file = CreateFile(BALANCE_FILE, GENERIC_WRITE, 0, 0,
		fresh ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

	if (file == INVALID_HANDLE_VALUE)
		return;

	for (int i = 0; i < BALANCE_COUNT; i++)
	{
		if (balanceLines[i].header)
			BalanceWriteText(file, balanceLines[i].header);

		if (!balanceLines[i].name)
			continue;

		/* Core's number is only worth printing where the standard moved away from it. */
		if (balanceLines[i].vanilla != balanceLines[i].standard)
			_snprintf(from, sizeof(from) - 1, ", vanilla %ld", balanceLines[i].vanilla);
		else
			from[0] = 0;

		_snprintf(line, sizeof(line) - 1, "%-22s = %-6ld ; (default %ld%s)%s\r\n",
			balanceLines[i].name, *balanceLines[i].value,
			balanceLines[i].standard, from,
			balanceLines[i].note ? balanceLines[i].note : "");

		BalanceWriteText(file, line);
	}

	CloseHandle(file);
	Log(fresh ? "Balance.txt newly created" : "Balance.txt extended by new lines");
}

/*
 * After reading: if not all settings stood in the file, it is rewritten -
 * with the player's values, but complete. Otherwise a file from an older
 * version of the game would hide all later additions forever. The player's
 * own comments are lost in the process, his numbers are not.
 */
static void BalanceBackfill()
{
	long missing = 0;

	for (int i = 0; i < BALANCE_COUNT; i++)
	{
		if (balanceLines[i].name && !balanceFound[i])
		{
			Log("Balance.txt: %s was missing, default %ld entered",
				balanceLines[i].name, balanceLines[i].standard);
			missing++;
		}
	}

	if (missing)
		BalanceWriteFile(0);
}

static int BalanceNameEquals(const char* a, const char* b)
{
	while (*a && *b)
	{
		char x = *a;
		char y = *b;

		if (x >= 'A' && x <= 'Z')
			x = (char)(x + 32);

		if (y >= 'A' && y <= 'Z')
			y = (char)(y + 32);

		if (x != y)
			return 0;

		a++;
		b++;
	}

	return *a == 0 && *b == 0;
}

/*
 * Evaluate one line of the form "Name = value". Everything after ; or # is a
 * comment, so the explanation may stand behind the value.
 */
static void BalanceReadLine(char* line)
{
	char* eq;
	char* name;
	char* value;
	char* end;
	long number;

	for (char* p = line; *p; p++)
	{
		if (*p == ';' || *p == '#')
		{
			*p = 0;
			break;
		}
	}

	eq = strchr(line, '=');

	if (!eq)
		return;

	*eq = 0;
	name = line;
	value = eq + 1;

	while (*name == ' ' || *name == '\t')
		name++;

	end = name + strlen(name);

	while (end > name && (end[-1] == ' ' || end[-1] == '\t'))
		end--;

	*end = 0;

	while (*value == ' ' || *value == '\t')
		value++;

	if (!*name || (*value != '-' && (*value < '0' || *value > '9')))
		return;

	number = atol(value);

	for (int i = 0; i < BALANCE_COUNT; i++)
	{
		if (!balanceLines[i].name || !BalanceNameEquals(name, balanceLines[i].name))
			continue;

		if (number < balanceLines[i].min)
			number = balanceLines[i].min;
		else if (number > balanceLines[i].max)
			number = balanceLines[i].max;

		*balanceLines[i].value = number;
		balanceFound[i] = 1;
		return;
	}

	Log("Balance.txt: unknown line \"%s\"", name);
}

void BalanceLoad()
{
	HANDLE file;
	DWORD size;
	DWORD bytesRead;
	char* buffer;
	char* line;

	for (int i = 0; i < BALANCE_COUNT; i++)
	{
		balanceFound[i] = 0;

		if (balanceLines[i].value)
			*balanceLines[i].value = balanceLines[i].standard;
	}

	file = CreateFile(BALANCE_FILE, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (file == INVALID_HANDLE_VALUE)
	{
		BalanceWriteFile(1);
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

		BalanceReadLine(line);
		line = next;
	}

	free(buffer);
	BalanceBackfill();

	Log("Balance.txt read: weapons %d%%, enemy health %d%%, enemy damage %d%%, scenery %d%%",
		balance.weaponDamage, balance.enemyHealth, balance.enemyDamage, balance.sceneryRange);
}

/*
 * Called at every level start, because setup.cpp resets the revolver there
 * anyway. The HK stays out - lara1gun.cpp sets it per shot itself.
 */
void BalanceSetWeapons()
{
	weapons[WEAPON_PISTOLS].damage = WeaponDamage(balance.pistols);
	weapons[WEAPON_UZI].damage = WeaponDamage(balance.uzi);
	weapons[WEAPON_SHOTGUN].damage = WeaponDamage(balance.shotgun);
	weapons[WEAPON_CROSSBOW].damage = WeaponDamage(balance.crossbow);
	weapons[WEAPON_TORCH].damage = WeaponDamage(balance.torch);
	weapons[WEAPON_REVOLVER].damage =
		WeaponDamage(gfLevelId > LVL5_COLOSSEUM ? balance.revolverLate : balance.revolverEarly);
}

/*
 * A weapon's individual value times the percentage line. The cap is 127
 * because the engine holds the damage in a char - without it a strong weapon
 * would flip negative at high percentages and heal the enemy.
 */
char WeaponDamage(long base)
{
	long value;

	if (balance.weaponDamage == 100)
		value = base;
	else
	{
		value = base * balance.weaponDamage / 100;

		/* Rounding down must not make a weapon harmless - only an explicit 0
		   setting may do that. */
		if (value < 1 && base > 0 && balance.weaponDamage > 0)
			value = 1;
	}

	if (value > 127)
		value = 127;

	return (char)value;
}

/*
 * Enemy hit points. Called from InitialiseObjects after BaddyObjects/
 * ObjectObjects/TrapObjects have written the original values.
 */
void BalanceSetEnemies()
{
	OBJECT_INFO* obj;

	/* First the individual values from the file. */
	for (int i = 0; i < BALANCE_COUNT; i++)
	{
		if (balanceLines[i].slot < 0)
			continue;

		obj = &objects[balanceLines[i].slot];

		/* hit_points <= 0 means: this level does not load this enemy. */
		if (obj->hit_points <= 0)
			continue;

		/* Trips when someone changes setup.cpp and forgets the table. */
		if (obj->hit_points != balanceLines[i].vanilla)
			Log("Balance: %s stands at %d in setup.cpp, the table knows %d",
				balanceLines[i].name, obj->hit_points, balanceLines[i].vanilla);

		obj->hit_points = (short)*balanceLines[i].value;
	}

	if (balance.enemyHealth == 100)
		return;

	/*
	 * Then the percentage line on everything thinking. Deliberately over all
	 * objects instead of over the table: that way an enemy without its own
	 * line above is covered too. Lara hangs on an object entry as well but
	 * is not intelligent, and objects without own hit points stand at
	 * -16384.
	 */
	for (int lp = 0; lp < NUMBER_OBJECTS; lp++)
	{
		long points;

		obj = &objects[lp];

		if (!obj->intelligent || obj->hit_points <= 0)
			continue;

		points = obj->hit_points * balance.enemyHealth / 100;

		if (points < 1)
			points = 1;
		else if (points > 32000)
			points = 32000;

		obj->hit_points = (short)points;
	}
}

/*
 * An enemy's damage against Lara. Deliberately enemies only - traps, falls,
 * fire, poison, drowning and the built-in level hazards bypass this spot and
 * stay as hard as in the original.
 */
short EnemyDamage(long damage)
{
	long value;

	if (balance.enemyDamage == 100)
		return (short)damage;

	value = damage * balance.enemyDamage / 100;

	/* Rounding down must not make a hit harmless. */
	if (value < 1 && damage > 0 && balance.enemyDamage > 0)
		value = 1;

	/* The return value is a short. Without the cap, the T-Rex bite (10000)
	   would overflow at high percentages and heal Lara. */
	if (value > 32767)
		value = 32767;

	return (short)value;
}
