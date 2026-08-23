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

#define BALANCE_DATEI	"Balance.txt"

BALANCE_INFO balance;

/*
 * Eine Zeile der Einstellungsdatei. Diese Tabelle ist die einzige Quelle der
 * Wahrheit: Voreinstellung, erlaubter Bereich, der Text der Vorlage und - bei
 * Lebenspunkten - der Objektplatz stehen alle hier. Die Vorlage wird daraus
 * erzeugt, sie kann also nicht von den Voreinstellungen abweichen.
 *
 * name == 0 heisst: keine Einstellung, nur der Vorlagentext in kopf.
 * slot >= 0 heisst: der Wert gehoert in objects[slot].hit_points.
 */
struct BALANCE_ZEILE
{
	const char* name;
	long* wert;
	long standard;
	long min;
	long max;
	short slot;
	const char* kopf;
	const char* notiz;
};

#define TRENNER(text)	{ 0, 0, 0, 0, 0, -1, text, 0 }
#define WAFFE(n, f, s, hinweis)		{ n, &balance.f, s, 0, 127, -1, 0, hinweis }
#define LEBEN(n, f, s, platz)		{ n, &balance.f, s, 1, 32000, platz, 0, 0 }
#define SCHADEN(n, f, s, hinweis)	{ n, &balance.f, s, 0, 32000, -1, 0, hinweis }

static const BALANCE_ZEILE balanceZeilen[] =
{
	TRENNER(
		"; ---------------------------------------------------------------\r\n"
		";  Balance.txt - Feineinstellung fuer Tomb Raider Chronicles\r\n"
		";  Teil des TR5 Randomizer. Aendern, speichern, Spiel neu starten.\r\n"
		"; ---------------------------------------------------------------\r\n"
		";  Aufbau:  Name = Zahl\r\n"
		";  Zeilen, die mit ; oder # beginnen, sind Kommentare.\r\n"
		";  Fehlt eine Zeile, gilt der Originalwert in der Klammer.\r\n"
		";  Wird die Datei geloescht, legt das Spiel sie neu an.\r\n"
		";  Kommen mit einer neuen Fassung Einstellungen dazu, traegt das\r\n"
		";  Spiel sie hier nach und behaelt deine Zahlen. Eigene Kommentare\r\n"
		";  gehen dabei verloren.\r\n"
		"\r\n"
		"\r\n"
		"; === Schaden von Laras Waffen ===================================\r\n"
		";  Treffer je Schuss, 0 bis 127. Lara hat 1000 Lebenspunkte, ein\r\n"
		";  einfacher Wachmann 24. Die Schrotflinte zaehlt je Schrotkugel,\r\n"
		";  sie wirkt also staerker als die Zahl vermuten laesst.\r\n"
		"\r\n"),

	WAFFE("Pistolen",		pistolen,		1,	0),
	WAFFE("Uzi",			uzi,			1,	0),
	WAFFE("Schrotflinte",	schrotflinte,	3,	"  je Schrotkugel"),
	WAFFE("Armbrust",		armbrust,		5,	"  im Spiel die Grappling Gun"),
	WAFFE("Fackel",			fackel,			3,	0),

	TRENNER(
		"\r\n"
		";  Der Revolver hat zwei Werte: das Spiel setzt ihn in jedem Level\r\n"
		";  neu, schwach in den Rom-Levels, stark ab dem U-Boot.\r\n"),

	WAFFE("RevolverFrueh",	revolverFrueh,	6,	"  bis einschliesslich Kolosseum"),
	WAFFE("RevolverSpaet",	revolverSpaet,	15,	"  alle spaeteren Level"),

	TRENNER(
		"\r\n"
		";  Die HK hat ebenfalls zwei Werte: gezielt aus dem Stand trifft\r\n"
		";  sie haerter als aus der Bewegung.\r\n"),

	WAFFE("HK_Stand",		hkStand,		3,	0),
	WAFFE("HK_Lauf",		hkLauf,			1,	0),

	TRENNER(
		"\r\n"
		"\r\n"
		"; === Lebenspunkte der Gegner ====================================\r\n"
		";  Ein Wert je Gegner, absolut wie im Original. Zum Vergleich:\r\n"
		";  Lara hat 1000, eine Pistolenkugel macht 1 Schaden.\r\n"
		"\r\n"),

	LEBEN("LP_WachmannBlau",		lpWachmannBlau,		24,		BLUE_GUARD),
	LEBEN("LP_WachmannJoby",		lpWachmannJoby,		24,		BLUE_GUARD2),
	LEBEN("LP_Swat",				lpSwat,				24,		SWAT),
	LEBEN("LP_SwatPlus",			lpSwatPlus,			24,		SWAT_PLUS),
	LEBEN("LP_Mafioso",				lpMafioso,			24,		MAFIA),
	LEBEN("LP_Mafia2",				lpMafia2,			26,		MAFIA2),
	LEBEN("LP_Wissenschaftler",		lpWissenschaftler,	24,		SCIENTIST),
	LEBEN("LP_Kranfuehrer",			lpKranfuehrer,		24,		CRANE_GUY),
	LEBEN("LP_Hitman",				lpHitman,			50,		HITMAN),
	LEBEN("LP_Scharfschuetze",		lpScharfschuetze,	35,		SNIPER),
	LEBEN("LP_Koch",				lpKoch,				60,		CHEF),
	LEBEN("LP_Pierre",				lpPierre,			60,		PIERRE),
	LEBEN("LP_Larson",				lpLarson,			60,		LARSON),
	LEBEN("LP_TwoGun",				lpTwoGun,			90,		TWOGUN),
	LEBEN("LP_Hund",				lpHund,				18,		DOG),
	LEBEN("LP_Husky",				lpHusky,			24,		HUSKIE),
	LEBEN("LP_Loewe",				lpLoewe,			40,		LION),
	LEBEN("LP_Kraehe",				lpKraehe,			10,		CROW),
	LEBEN("LP_TRex",				lpTRex,				800,	SAS),
	LEBEN("LP_Raptor",				lpRaptor,			90,		REAPER),
	LEBEN("LP_Compy",				lpCompy,			10,		SAILOR),
	LEBEN("LP_MiniUBoot",			lpMiniUBoot,		100,	ATTACK_SUB),
	LEBEN("LP_Hydra",				lpHydra,			30,		HYDRA),
	LEBEN("LP_Kobold",				lpKobold,			12,		IMP),
	LEBEN("LP_Irrlicht",			lpIrrlicht,			256,	WILLOWISP),
	LEBEN("LP_Labyrinthmonster",	lpLabyrinthmonster,	120,	MAZE_MONSTER),
	LEBEN("LP_Sumpfmonster",		lpSumpfmonster,		100,	GREEN_TEETH),
	LEBEN("LP_Skelett",				lpSkelett,			100,	SKELETON),
	LEBEN("LP_Gladiator",			lpGladiator,		20,		GLADIATOR),
	LEBEN("LP_RoemischerGott",		lpRoemischerGott,	300,	ROMAN_GOD),
	LEBEN("LP_RoemischerGott2",		lpRoemischerGott2,	300,	ROMAN_GOD2),

	TRENNER(
		"\r\n"
		"\r\n"
		"; === Schaden der Gegner an Lara =================================\r\n"
		";  Ein Wert je Angriff. Gegner mit mehreren Angriffen haben\r\n"
		";  mehrere Zeilen - im Original sind die absichtlich verschieden\r\n"
		";  stark, der Sprung eines Hundes tut mehr weh als sein Biss.\r\n"
		"\r\n"
		";  Schusswaffen der menschlichen Gegner:\r\n"),

	SCHADEN("S_Soldat_Frueh",		sSoldatFrueh,		30,		"  Wachmann/SWAT/Mafioso, bis Old Mill"),
	SCHADEN("S_Soldat_Spaet",		sSoldatSpaet,		60,		"  dieselben ab Old Mill - doppelt so hart"),
	SCHADEN("S_Soldat_Kurz",		sSoldatKurz,		10,		"  ihr schwaecherer zweiter Schuss"),
	SCHADEN("S_Hitman_Schuss",		sHitmanSchuss,		12,		0),
	SCHADEN("S_Mafia2_Schuss",		sMafia2Schuss,		35,		0),
	SCHADEN("S_Pierre_Schuss",		sPierreSchuss,		20,		"  je Pistole, er schiesst mit zweien"),
	SCHADEN("S_Larson_Schuss",		sLarsonSchuss,		20,		0),
	SCHADEN("S_Scharfschuetze",		sScharfschuetzeSchuss, 100,	"  ein Treffer ist ein Zehntel von Lara"),

	TRENNER(
		"\r\n"
		";  Tiere:\r\n"),

	SCHADEN("S_Hund_Biss",			sHundBiss,			30,		0),
	SCHADEN("S_Hund_Sprung",		sHundSprung,		80,		0),
	SCHADEN("S_Hund_Nachsetzen",	sHundNachsetzen,	50,		0),
	SCHADEN("S_Husky_Biss",			sHuskyBiss,			20,		0),
	SCHADEN("S_Husky_Sprung",		sHuskySprung,		10,		0),
	SCHADEN("S_Loewe_Sprung",		sLoeweSprung,		200,	0),
	SCHADEN("S_Loewe_Tatze",		sLoeweTatze,		60,		0),
	SCHADEN("S_Kraehe",				sKraeheHieb,		20,		0),
	SCHADEN("S_Fledermaeuse",		sFledermaeuse,		2,		"  je Fledermaus und Treffer"),
	SCHADEN("S_Spinnen",			sSpinnen,			3,		"  je Spinne und Treffer"),
	SCHADEN("S_TRex_Tritt",			sTRexTritt,			1,		"  wenn er steht"),
	SCHADEN("S_TRex_TrittLauf",		sTRexTrittLauf,		10,		"  wenn er laeuft"),
	SCHADEN("S_TRex_Biss",			sTRexBiss,			10000,	"  toedlich gemeint, Lara hat 1000"),
	SCHADEN("S_Raptor_Biss",		sRaptorBiss,		100,	0),
	SCHADEN("S_Raptor_Sprung",		sRaptorSprung,		100,	0),
	SCHADEN("S_Compy_Biss",			sCompyBiss,			90,		0),

	TRENNER(
		"\r\n"
		";  Uebernatuerliches und schweres Geraet:\r\n"),

	SCHADEN("S_Koch_Messer",		sKochMesser,		150,	"  Nahkaempfer im Pool seit #124 - war 80"),
	SCHADEN("S_Skelett_Hieb",		sSkelettHieb,		400,	0),
	SCHADEN("S_Gladiator_Hieb",		sGladiatorHieb,		120,	0),
	SCHADEN("S_Gott_Hieb",			sGottHieb,			40,		"  Roemischer Gott im Nahkampf"),
	SCHADEN("S_Gott_Blitz",			sGottBlitz,			200,	"  sein Fernangriff"),
	SCHADEN("S_Hydra_Strahl",		sHydraStrahl,		120,	0),
	SCHADEN("S_Kobold_Wurf",		sKoboldWurf,		3,		0),
	SCHADEN("S_Labyrinthmonster",	sLabyrinthmonster,	150,	0),
	SCHADEN("S_Sumpfmonster",		sSumpfmonster,		100,	0),
	SCHADEN("S_TwoGun_Plasma",		sTwoGunPlasma,		250,	0),
	SCHADEN("S_Torpedo",			sTorpedo,			200,	"  vom Mini-U-Boot"),

	TRENNER(
		"\r\n"
		"\r\n"
		"; === Levelgefahren ==============================================\r\n"
		";  Das sind keine Gegner, sondern fest eingebaute Maschinen. Sie\r\n"
		";  tauchen im Rando nirgends anders auf, und die Prozentzeile\r\n"
		";  GegnerSchaden weiter unten laesst sie absichtlich in Ruhe.\r\n"
		"\r\n"),

	SCHADEN("S_Autogun",			sAutogun,			20,		"  Deckengeschuetz"),
	SCHADEN("S_Gunship",			sGunship,			20,		"  Kampfhubschrauber"),
	SCHADEN("S_Iris",				sIris,				12,		"  je Bilddurchlauf, solange Lara zu nah steht"),
	SCHADEN("S_Area51Laser",		sArea51Laser,		100,	0),
	SCHADEN("S_Stromkabel",			sStromkabel,		10,		"  die Kabel im U-Boot-Stuetzpunkt"),

	TRENNER(
		"\r\n"
		"\r\n"
		"; === Alles auf einmal ===========================================\r\n"
		";  Prozentwerte. Sie wirken ZUSAETZLICH auf die Einzelwerte oben,\r\n"
		";  damit man das ganze Spiel mit einer Zahl haerter oder weicher\r\n"
		";  stellen kann, ohne die Feinabstimmung anzufassen. 100 laesst\r\n"
		";  alles wie eingetragen. Die Levelgefahren bleiben unberuehrt.\r\n"
		"\r\n"
		";  Diese drei Zeilen stellt auch der Randomizer ueber seine Regler\r\n"
		";  ein. Alles andere in dieser Datei fasst er nicht an.\r\n"
		"\r\n"),

	{ "WaffenSchaden",	&balance.waffenSchaden,	100, 0, 1000, -1, 0,
		"  auf alle Waffen Laras. 0 macht sie wirkungslos" },
	{ "GegnerLeben",	&balance.gegnerLeben,	100, 1, 1000, -1, 0,
		"  50 = halb so zaeh, 200 = doppelt so zaeh" },
	{ "GegnerSchaden",	&balance.gegnerSchaden,	100, 0, 1000, -1, 0,
		"  0 macht alle Gegner harmlos" },

	TRENNER(
		"\r\n"
		"\r\n"
		"; === Klang ======================================================\r\n"
		";  Hoerweite der ortsfesten Levelkulisse in Prozent: Maschinen,\r\n"
		";  Feuer, tropfendes Wasser. Das Original liess sie durch halbe\r\n"
		";  Level dringen, der Randomizer hegt sie ein. 100 stellt das\r\n"
		";  Original wieder her. Gegner und Lara behalten immer ihre volle\r\n"
		";  Reichweite.\r\n"
		"\r\n"),

	{ "KulissenReichweite", &balance.kulissenReichweite, 65, 10, 400, -1, 0,
		"  Vanilla waere 100, der Randomizer haelt 65" },

	TRENNER(
		"\r\n"
		";  Fallen, Stuerze, Feuer, Gift und Ertrinken sind hier bewusst\r\n"
		";  nicht einstellbar. Die bleiben immer so hart wie im Original,\r\n"
		";  auch wenn du die Gegner harmlos stellst.\r\n")
};

#define BALANCE_ANZAHL	(sizeof(balanceZeilen) / sizeof(balanceZeilen[0]))

static void BalanceSchreibe(HANDLE datei, const char* text)
{
	DWORD geschrieben;
	WriteFile(datei, text, (DWORD)strlen(text), &geschrieben, 0);
}

/* Welche Zeilen beim Lesen tatsaechlich in der Datei standen. */
static char balanceGefunden[BALANCE_ANZAHL];

/*
 * Die Datei wird aus der Tabelle erzeugt, damit Text und Voreinstellungen
 * nicht auseinanderlaufen koennen. Geschrieben werden die aktuellen Werte -
 * bei einer neuen Datei sind das die Originalwerte, beim Nachtragen die des
 * Spielers.
 */
static void BalanceDateiSchreiben(long neu)
{
	HANDLE datei;
	char zeile[256];

	zeile[sizeof(zeile) - 1] = 0;

	datei = CreateFile(BALANCE_DATEI, GENERIC_WRITE, 0, 0,
		neu ? CREATE_NEW : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);

	if (datei == INVALID_HANDLE_VALUE)
		return;

	for (int i = 0; i < BALANCE_ANZAHL; i++)
	{
		if (balanceZeilen[i].kopf)
			BalanceSchreibe(datei, balanceZeilen[i].kopf);

		if (!balanceZeilen[i].name)
			continue;

		_snprintf(zeile, sizeof(zeile) - 1, "%-22s = %-6ld ; (Original %ld)%s\r\n",
			balanceZeilen[i].name, *balanceZeilen[i].wert, balanceZeilen[i].standard,
			balanceZeilen[i].notiz ? balanceZeilen[i].notiz : "");

		BalanceSchreibe(datei, zeile);
	}

	CloseHandle(datei);
	Log(neu ? "Balance.txt neu angelegt" : "Balance.txt um neue Zeilen ergaenzt");
}

/*
 * Nach dem Lesen: standen nicht alle Einstellungen in der Datei, wird sie neu
 * geschrieben - mit den Werten des Spielers, aber vollstaendig. Sonst wuerde
 * eine Datei aus einer aelteren Fassung des Spiels alle spaeter
 * hinzugekommenen Einstellungen fuer immer verstecken. Eigene Kommentare des
 * Spielers gehen dabei verloren, seine Zahlen nicht.
 */
static void BalanceNachtragen()
{
	long fehlt = 0;

	for (int i = 0; i < BALANCE_ANZAHL; i++)
	{
		if (balanceZeilen[i].name && !balanceGefunden[i])
		{
			Log("Balance.txt: %s fehlte, Originalwert %ld eingetragen",
				balanceZeilen[i].name, balanceZeilen[i].standard);
			fehlt++;
		}
	}

	if (fehlt)
		BalanceDateiSchreiben(0);
}

static int BalanceNameGleich(const char* a, const char* b)
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
 * Eine Zeile der Form "Name = Wert" auswerten. Alles hinter ; oder # ist
 * Kommentar, damit die Erklaerung hinter dem Wert stehen darf.
 */
static void BalanceZeileLesen(char* zeile)
{
	char* gleich;
	char* name;
	char* wert;
	char* ende;
	long zahl;

	for (char* p = zeile; *p; p++)
	{
		if (*p == ';' || *p == '#')
		{
			*p = 0;
			break;
		}
	}

	gleich = strchr(zeile, '=');

	if (!gleich)
		return;

	*gleich = 0;
	name = zeile;
	wert = gleich + 1;

	while (*name == ' ' || *name == '\t')
		name++;

	ende = name + strlen(name);

	while (ende > name && (ende[-1] == ' ' || ende[-1] == '\t'))
		ende--;

	*ende = 0;

	while (*wert == ' ' || *wert == '\t')
		wert++;

	if (!*name || (*wert != '-' && (*wert < '0' || *wert > '9')))
		return;

	zahl = atol(wert);

	for (int i = 0; i < BALANCE_ANZAHL; i++)
	{
		if (!balanceZeilen[i].name || !BalanceNameGleich(name, balanceZeilen[i].name))
			continue;

		if (zahl < balanceZeilen[i].min)
			zahl = balanceZeilen[i].min;
		else if (zahl > balanceZeilen[i].max)
			zahl = balanceZeilen[i].max;

		*balanceZeilen[i].wert = zahl;
		balanceGefunden[i] = 1;
		return;
	}

	Log("Balance.txt: unbekannte Zeile \"%s\"", name);
}

void BalanceLaden()
{
	HANDLE datei;
	DWORD groesse;
	DWORD gelesen;
	char* puffer;
	char* zeile;

	for (int i = 0; i < BALANCE_ANZAHL; i++)
	{
		balanceGefunden[i] = 0;

		if (balanceZeilen[i].wert)
			*balanceZeilen[i].wert = balanceZeilen[i].standard;
	}

	datei = CreateFile(BALANCE_DATEI, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (datei == INVALID_HANDLE_VALUE)
	{
		BalanceDateiSchreiben(1);
		return;
	}

	groesse = GetFileSize(datei, 0);

	if (groesse == INVALID_FILE_SIZE || groesse > 0x20000)
		groesse = 0x20000;

	puffer = (char*)malloc(groesse + 1);

	if (!puffer)
	{
		CloseHandle(datei);
		return;
	}

	gelesen = 0;
	ReadFile(datei, puffer, groesse, &gelesen, 0);
	CloseHandle(datei);
	puffer[gelesen] = 0;

	zeile = puffer;

	/* Notepad speichert UTF-8 gern mit Vorzeichen - das ueberspringen. */
	if (gelesen >= 3 && (uchar)zeile[0] == 0xEF && (uchar)zeile[1] == 0xBB && (uchar)zeile[2] == 0xBF)
		zeile += 3;

	while (*zeile)
	{
		char* naechste = zeile;

		while (*naechste && *naechste != '\r' && *naechste != '\n')
			naechste++;

		if (*naechste)
		{
			*naechste = 0;
			naechste++;

			while (*naechste == '\r' || *naechste == '\n')
				naechste++;
		}

		BalanceZeileLesen(zeile);
		zeile = naechste;
	}

	free(puffer);
	BalanceNachtragen();

	Log("Balance.txt gelesen: Waffen %d%%, Gegnerleben %d%%, Gegnerschaden %d%%, Kulisse %d%%",
		balance.waffenSchaden, balance.gegnerLeben, balance.gegnerSchaden, balance.kulissenReichweite);
}

/*
 * Wird je Levelstart aufgerufen, weil setup.cpp den Revolver dort ohnehin neu
 * setzt. Die HK bleibt aussen vor, die stellt lara1gun.cpp je Schuss selbst.
 */
void BalanceWaffenSetzen()
{
	weapons[WEAPON_PISTOLS].damage = WaffenSchaden(balance.pistolen);
	weapons[WEAPON_UZI].damage = WaffenSchaden(balance.uzi);
	weapons[WEAPON_SHOTGUN].damage = WaffenSchaden(balance.schrotflinte);
	weapons[WEAPON_CROSSBOW].damage = WaffenSchaden(balance.armbrust);
	weapons[WEAPON_TORCH].damage = WaffenSchaden(balance.fackel);
	weapons[WEAPON_REVOLVER].damage =
		WaffenSchaden(gfLevelId > LVL5_COLOSSEUM ? balance.revolverSpaet : balance.revolverFrueh);
}

/*
 * Der Einzelwert einer Waffe mal der Prozentzeile. Der Deckel ist 127, weil
 * die Engine den Schaden in einem char haelt - ohne ihn wuerde eine starke
 * Waffe bei hohen Prozentwerten ins Negative kippen und den Gegner heilen.
 */
char WaffenSchaden(long basis)
{
	long wert;

	if (balance.waffenSchaden == 100)
		wert = basis;
	else
	{
		wert = basis * balance.waffenSchaden / 100;

		/* Abrunden darf eine Waffe nicht wirkungslos machen - das soll nur,
		   wer ausdruecklich 0 einstellt. */
		if (wert < 1 && basis > 0 && balance.waffenSchaden > 0)
			wert = 1;
	}

	if (wert > 127)
		wert = 127;

	return (char)wert;
}

/*
 * Lebenspunkte der Gegner. Wird aus InitialiseObjects gerufen, nachdem
 * BaddyObjects/ObjectObjects/TrapObjects die Originalwerte geschrieben haben.
 */
void BalanceGegnerSetzen()
{
	OBJECT_INFO* obj;

	/* Erst die einzelnen Werte aus der Datei. */
	for (int i = 0; i < BALANCE_ANZAHL; i++)
	{
		if (balanceZeilen[i].slot < 0)
			continue;

		obj = &objects[balanceZeilen[i].slot];

		/* hit_points <= 0 heisst: das Level laedt diesen Gegner nicht. */
		if (obj->hit_points <= 0)
			continue;

		/* Faellt an, wenn jemand setup.cpp aendert und die Tabelle vergisst. */
		if (obj->hit_points != balanceZeilen[i].standard)
			Log("Balance: %s steht in setup.cpp auf %d, die Tabelle kennt %d",
				balanceZeilen[i].name, obj->hit_points, balanceZeilen[i].standard);

		obj->hit_points = (short)*balanceZeilen[i].wert;
	}

	if (balance.gegnerLeben == 100)
		return;

	/*
	 * Dann die Prozentzeile auf alles Denkende. Absichtlich ueber alle
	 * Objekte statt ueber die Tabelle: so faellt auch ein Gegner darunter,
	 * der oben keine eigene Zeile hat. Lara haengt zwar auch an einem
	 * Objekteintrag, ist aber nicht intelligent, und Objekte ohne eigene
	 * Lebenspunkte stehen auf -16384.
	 */
	for (int lp = 0; lp < NUMBER_OBJECTS; lp++)
	{
		long punkte;

		obj = &objects[lp];

		if (!obj->intelligent || obj->hit_points <= 0)
			continue;

		punkte = obj->hit_points * balance.gegnerLeben / 100;

		if (punkte < 1)
			punkte = 1;
		else if (punkte > 32000)
			punkte = 32000;

		obj->hit_points = (short)punkte;
	}
}

/*
 * Schaden eines Gegners an Lara. Absichtlich nur fuer Gegner - Fallen,
 * Stuerze, Feuer, Gift, Ertrinken und die fest eingebauten Levelgefahren
 * laufen an dieser Stelle vorbei und bleiben so hart wie im Original.
 */
short GegnerSchaden(long schaden)
{
	long wert;

	if (balance.gegnerSchaden == 100)
		return (short)schaden;

	wert = schaden * balance.gegnerSchaden / 100;

	/* Abrunden darf einen Treffer nicht wirkungslos machen. */
	if (wert < 1 && schaden > 0 && balance.gegnerSchaden > 0)
		wert = 1;

	/* Der Rueckgabewert ist ein short. Ohne Deckel wuerde der Biss des
	   T-Rex (10000) bei hohen Prozentwerten ueberlaufen und Lara heilen. */
	if (wert > 32767)
		wert = 32767;

	return (short)wert;
}
