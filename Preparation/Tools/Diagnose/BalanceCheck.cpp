// BalanceCheck - test rig for game\balance.cpp (task #108). Compiles the
// real source file along and only supplies the symbols it otherwise pulls
// from the engine: weapons[], objects[], gfLevelId, Log(). Briefly creates
// a Balance.txt in the working directory and cleans it up again.
//
// Build (32 bit), from the .bat next to this file:
//   set E=..\..\Tomb5 Native win 11 fix\TOMB5 Code\TOMB5
//   cl /nologo /EHsc /W3 /I "%E%" BalanceCheck.cpp "%E%\game\balance.cpp"
//       /Fe:BalanceCheck.exe
//
// What is checked: create the template and read it back (it must reproduce
// the defaults exactly - the template is generated from the same table the
// values come from), a bent file (BOM, lower/upper case, tabs, a comment
// behind the value, an unknown line, a line without an equals sign, a
// value that is no number, a last line without a newline, values above and
// below the limit), the weapon assignment for early and late, the hit
// points per enemy including the percentage line on top, and the enemy
// damage at 0/25/100/1000 percent including the overflow cap.
#include "tomb5/pch.h"
#include "game/balance.h"
#include "game/larafire.h"
#include "game/gameflow.h"
#include "game/objects.h"

WEAPON_INFO weapons[9];
OBJECT_INFO objects[NUMBER_OBJECTS];
uchar gfLevelId = 0;
void Log(const char* s, ...) { (void)s; }

static int error = 0;

static void check(const char* what, long is, long expected)
{
	if (is == expected)
		printf("    ok    %-24s = %ld\n", what, is);
	else
	{
		printf("    ERROR %-24s = %ld, expected %ld\n", what, is, expected);
		error++;
	}
}

// Set up the enemies the way setup.cpp does: intelligent, with their
// original values. Only the few the test touches.
static void setUpEnemies()
{
	for (int i = 0; i < NUMBER_OBJECTS; i++)
	{
		objects[i].hit_points = -16384;
		objects[i].intelligent = 0;
	}

	struct { int slot; short hp; } vanilla[] =
	{
		{ BLUE_GUARD, 24 }, { DOG, 18 }, { SAS, 800 }, { ROMAN_GOD, 300 },
		{ TWOGUN, 90 }, { IMP, 12 }, { SKELETON, 100 }
	};

	for (int i = 0; i < sizeof(vanilla) / sizeof(vanilla[0]); i++)
	{
		objects[vanilla[i].slot].hit_points = vanilla[i].hp;
		objects[vanilla[i].slot].intelligent = 1;
	}

	// Lara and the flare: hit points, but not intelligent. The percentage
	// line must never touch them.
	objects[LARA].hit_points = 1000;
	objects[FLARE_ITEM].hit_points = 256;
}

static void writeFile(const char* txt)
{
	HANDLE h = CreateFileA("Balance.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	DWORD n;
	WriteFile(h, txt, (DWORD)strlen(txt), &n, 0);
	CloseHandle(h);
}

int main()
{
	DeleteFileA("Balance.txt");

	printf("--- 1) File missing: the template arises, the original values apply\n");
	BalanceLoad();
	printf("    Balance.txt present: %s\n",
		GetFileAttributesA("Balance.txt") == INVALID_FILE_ATTRIBUTES ? "NO" : "yes");
	check("Pistols", balance.pistols, 1);
	check("RevolverLate", balance.revolverLate, 15);
	check("HP_TRex", balance.hpTRex, 800);
	check("DMG_Skeleton_Strike", balance.dmgSkeletonStrike, 400);
	check("DMG_Soldier_Late", balance.dmgSoldierLate, 60);
	check("DMG_Iris", balance.dmgIris, 12);
	check("SceneryRange", balance.sceneryRange, 65);

	printf("--- 2) Read the just-written template back: must be identical\n");
	{
		BALANCE_INFO before = balance;
		BalanceLoad();
		if (memcmp(&before, &balance, sizeof(BALANCE_INFO)) == 0)
			printf("    ok    the template reproduces all %d bytes\n", (int)sizeof(BALANCE_INFO));
		else
		{
			printf("    ERROR the template deviates from the defaults\n");
			error++;
		}
	}

	printf("--- 3) Bent file: BOM, lower/upper case, tabs, garbage, limit values\n");
	writeFile(
		"\xEF\xBB\xBF; comment with umlauts \xC3\xA4\xC3\xB6\xC3\xBC\r\n"
		"# another comment\r\n"
		"\r\n"
		"   pistols   =   7   ; lower case, lots of air\r\n"
		"UZI\t=\t99\r\n"
		"Shotgun = 500\r\n"			// above max -> 127
		"Crossbow = -4\r\n"			// below min -> 0
		"HP_Dog = 5\r\n"
		"HP_TRex = 99999\r\n"			// above max -> 32000
		"HP_RomanGod = 0\r\n"			// below min -> 1
		"DMG_Dog_Jump = 500\r\n"
		"DMG_Autogun = 1\r\n"
		"EnemyHealth=200\r\n"
		"EnemyDamage = 0\r\n"
		"SceneryRange = 1\r\n"			// below min -> 10
		"UnknownLine = 5\r\n"
		"broken line without an equals sign\r\n"
		"HK_Standing = abc\r\n"			// no number -> original 3
		"Torch = 12 ; comment behind the value\r\n"
		"RevolverLate = 40");			// last line without a newline
	BalanceLoad();
	check("Pistols", balance.pistols, 7);
	check("Uzi", balance.uzi, 99);
	check("Shotgun", balance.shotgun, 127);
	check("Crossbow", balance.crossbow, 0);
	check("Torch", balance.torch, 12);
	check("HK_Standing", balance.hkStanding, 3);
	check("RevolverLate", balance.revolverLate, 40);
	check("HP_Dog", balance.hpDog, 5);
	check("HP_TRex", balance.hpTRex, 32000);
	check("HP_RomanGod", balance.hpRomanGod, 1);
	check("DMG_Dog_Jump", balance.dmgDogJump, 500);
	check("DMG_Autogun", balance.dmgAutogun, 1);
	check("EnemyHealth", balance.enemyHealth, 200);
	check("SceneryRange", balance.sceneryRange, 10);

	printf("--- 4) Set the weapons, revolver early (gfLevelId 0) and late\n");
	BalanceSetWeapons();
	check("weapons revolver early", weapons[WEAPON_REVOLVER].damage, 6);
	check("weapons pistols", weapons[WEAPON_PISTOLS].damage, 7);
	check("weapons shotgun", weapons[WEAPON_SHOTGUN].damage, 127);
	gfLevelId = LVL5_DEEPSEA_DIVE;
	BalanceSetWeapons();
	check("weapons revolver late", weapons[WEAPON_REVOLVER].damage, 40);

	printf("--- 5) Hit points: own value, then 200 percent on top\n");
	setUpEnemies();
	BalanceSetEnemies();
	check("dog 5 x 200%", objects[DOG].hit_points, 10);
	check("TRex 32000 capped", objects[SAS].hit_points, 32000);
	check("god 1 x 200%", objects[ROMAN_GOD].hit_points, 2);
	check("guard 24 x 200%", objects[BLUE_GUARD].hit_points, 48);
	check("Lara untouched", objects[LARA].hit_points, 1000);
	check("flare untouched", objects[FLARE_ITEM].hit_points, 256);

	printf("--- 6) Hit points at 100 percent: only the own values\n");
	balance.enemyHealth = 100;
	setUpEnemies();
	BalanceSetEnemies();
	check("dog", objects[DOG].hit_points, 5);
	check("TwoGun unchanged", objects[TWOGUN].hit_points, 90);

	printf("--- 7) Enemy damage\n");
	balance.enemyDamage = 0;
	check("0% of 80", EnemyDamage(80), 0);
	balance.enemyDamage = 25;
	check("25% of 80", EnemyDamage(80), 20);
	check("25% of 1 (never 0)", EnemyDamage(1), 1);
	balance.enemyDamage = 100;
	check("100% of 10000", EnemyDamage(10000), 10000);
	balance.enemyDamage = 1000;
	check("1000% of 10000 capped", EnemyDamage(10000), 32767);

	printf("--- 7b) WeaponDamage percentage line (the randomizer's dial)\n");
	writeFile(
		"Pistols = 10\r\n"
		"Shotgun = 3\r\n"
		"HK_Standing = 3\r\n"
		"RevolverEarly = 6\r\n"
		"WeaponDamage = 200\r\n");
	BalanceLoad();
	gfLevelId = 0;
	BalanceSetWeapons();
	check("200% pistols 10 -> 20", weapons[WEAPON_PISTOLS].damage, 20);
	check("200% revolver 6 -> 12", weapons[WEAPON_REVOLVER].damage, 12);
	check("200% HK via the helper", WeaponDamage(balance.hkStanding), 6);
	balance.weaponDamage = 50;
	BalanceSetWeapons();
	check("50% pistols 10 -> 5", weapons[WEAPON_PISTOLS].damage, 5);
	check("50% shotgun 3 -> 1", weapons[WEAPON_SHOTGUN].damage, 1);
	check("50% of 1 never becomes 0", WeaponDamage(1), 1);
	balance.weaponDamage = 0;
	check("0% makes it ineffective", WeaponDamage(10), 0);
	balance.weaponDamage = 1000;
	check("1000% capped at 127", WeaponDamage(100), 127);

	printf("--- 8) Old file from an earlier version: append the missing lines,\n");
	printf("       but keep the player's numbers\n");
	writeFile(
		"; this is how the file looked in the first version\r\n"
		"Pistols = 42\r\n"
		"EnemyDamage = 55\r\n"
		"SceneryRange = 80\r\n");
	BalanceLoad();
	check("Pistols kept", balance.pistols, 42);
	check("EnemyDamage kept", balance.enemyDamage, 55);
	check("SceneryRange kept", balance.sceneryRange, 80);
	check("HP_TRex appended", balance.hpTRex, 800);
	check("DMG_God_Lightning appended", balance.dmgGodLightning, 200);
	{
		// Now the file must be complete: reading it again may not append
		// anything anymore and may not change anything anymore.
		BALANCE_INFO before = balance;
		BalanceLoad();
		if (memcmp(&before, &balance, sizeof(BALANCE_INFO)) == 0)
			printf("    ok    the completed file reads back unchanged\n");
		else
		{
			printf("    ERROR the completed file delivers different values\n");
			error++;
		}
	}

	DeleteFileA("Balance.txt");
	printf("\n%s (%d deviations)\n", error ? "FAILED" : "ALL FINE", error);
	return error ? 1 : 0;
}
