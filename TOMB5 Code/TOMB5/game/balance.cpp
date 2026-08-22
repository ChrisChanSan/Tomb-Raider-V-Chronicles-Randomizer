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
#include "../specific/function_stubs.h"

#define BALANCE_DATEI	"Balance.txt"

BALANCE_INFO balance;

/*
 * Die Originalwerte. Waffenschaden steht in larafire.cpp in weapons[9], die
 * Werte fuer Revolver und HK werden dort allerdings zur Laufzeit ueberschrieben
 * (setup.cpp je Level, lara1gun.cpp je Schuss) - deshalb stehen sie hier
 * einzeln und werden an genau diesen Stellen wieder eingesetzt.
 */
static const BALANCE_INFO balanceOriginal =
{
	1,		/* Pistolen */
	1,		/* Uzi */
	3,		/* Schrotflinte */
	5,		/* Armbrust */
	3,		/* Fackel */
	6,		/* Revolver frueh */
	15,		/* Revolver spaet */
	3,		/* HK im Stand */
	1,		/* HK in Bewegung */
	100,	/* Gegnerleben */
	100,	/* Gegnerschaden */
	65		/* Kulissenreichweite */
};

struct BALANCE_ZEILE
{
	const char* name;
	long* wert;
	long min;
	long max;
};

static const BALANCE_ZEILE balanceZeilen[] =
{
	{ "Pistolen",			&balance.pistolen,			0, 127 },
	{ "Uzi",				&balance.uzi,				0, 127 },
	{ "Schrotflinte",		&balance.schrotflinte,		0, 127 },
	{ "Armbrust",			&balance.armbrust,			0, 127 },
	{ "Fackel",				&balance.fackel,			0, 127 },
	{ "RevolverFrueh",		&balance.revolverFrueh,		0, 127 },
	{ "RevolverSpaet",		&balance.revolverSpaet,		0, 127 },
	{ "HK_Stand",			&balance.hkStand,			0, 127 },
	{ "HK_Lauf",			&balance.hkLauf,			0, 127 },
	{ "GegnerLeben",		&balance.gegnerLeben,		1, 1000 },
	{ "GegnerSchaden",		&balance.gegnerSchaden,		0, 1000 },
	{ "KulissenReichweite",	&balance.kulissenReichweite,	10, 400 }
};

#define BALANCE_ANZAHL	(sizeof(balanceZeilen) / sizeof(balanceZeilen[0]))

/*
 * Die Datei, die beim ersten Start angelegt wird. Sie ist die eigentliche
 * Bedienungsanleitung der Aufgabe - wer sie in Notepad oeffnet, soll ohne
 * Nachfrage wissen, was jede Zeile tut und was der Originalwert war.
 */
static const char* balanceVorlage =
	"; ---------------------------------------------------------------\r\n"
	";  Balance.txt - Feineinstellung fuer Tomb Raider Chronicles\r\n"
	";  Teil des TR5 Randomizer. Aendern, speichern, Spiel neu starten.\r\n"
	"; ---------------------------------------------------------------\r\n"
	";  Aufbau:  Name = Zahl\r\n"
	";  Zeilen, die mit ; oder # beginnen, sind Kommentare.\r\n"
	";  Fehlt eine Zeile, gilt der Originalwert aus dem Klammerhinweis.\r\n"
	";  Wird die Datei geloescht, legt das Spiel sie neu an.\r\n"
	"\r\n"
	"\r\n"
	"; === Schaden von Laras Waffen ===================================\r\n"
	";  Treffer je Schuss, 0 bis 127. Lara selbst hat 1000 Lebenspunkte,\r\n"
	";  ein einfacher Wachmann 24. Die Schrotflinte zaehlt je Kugel des\r\n"
	";  Schrotschusses, sie wirkt also staerker als die Zahl vermuten laesst.\r\n"
	"\r\n"
	"Pistolen        = 1     ; (Original 1)\r\n"
	"Uzi             = 1     ; (Original 1)\r\n"
	"Schrotflinte    = 3     ; (Original 3)  je Schrotkugel\r\n"
	"Armbrust        = 5     ; (Original 5)  im Spiel die Grappling Gun\r\n"
	"Fackel          = 3     ; (Original 3)\r\n"
	"\r\n"
	";  Der Revolver hat zwei Werte: das Spiel setzt ihn in jedem Level neu,\r\n"
	";  schwach in den Rom-Levels, stark ab dem U-Boot.\r\n"
	"RevolverFrueh   = 6     ; (Original 6)  bis einschliesslich Kolosseum\r\n"
	"RevolverSpaet   = 15    ; (Original 15) alle spaeteren Level\r\n"
	"\r\n"
	";  Die HK hat ebenfalls zwei Werte: gezielt aus dem Stand trifft sie\r\n"
	";  haerter als aus der Bewegung.\r\n"
	"HK_Stand        = 3     ; (Original 3)\r\n"
	"HK_Lauf         = 1     ; (Original 1)\r\n"
	"\r\n"
	"\r\n"
	"; === Gegner =====================================================\r\n"
	";  Prozentwerte. 100 laesst alles wie im Original.\r\n"
	"\r\n"
	";  Lebenspunkte aller Gegner. 50 = halb so zaeh, 200 = doppelt so zaeh.\r\n"
	"GegnerLeben     = 100   ; (Original 100), erlaubt 1 bis 1000\r\n"
	"\r\n"
	";  Schaden, den Gegner an Lara austeilen. 0 macht sie voellig harmlos.\r\n"
	";  Fallen, Stuerze, Feuer, Gift und Ertrinken bleiben unberuehrt.\r\n"
	"GegnerSchaden   = 100   ; (Original 100), erlaubt 0 bis 1000\r\n"
	"\r\n"
	"\r\n"
	"; === Klang ======================================================\r\n"
	";  Hoerweite der ortsfesten Levelkulisse in Prozent: Maschinen, Feuer,\r\n"
	";  tropfendes Wasser. Das Original liess sie durch halbe Level dringen,\r\n"
	";  der Randomizer hegt sie auf 65 Prozent ein. 100 stellt das Original\r\n"
	";  wieder her. Gegner und Lara behalten immer ihre volle Reichweite.\r\n"
	"KulissenReichweite = 65 ; (Randomizer 65, Original 100), erlaubt 10 bis 400\r\n";

static void BalanceVorlageSchreiben()
{
	HANDLE datei;
	DWORD geschrieben;

	datei = CreateFile(BALANCE_DATEI, GENERIC_WRITE, 0, 0, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, 0);

	if (datei == INVALID_HANDLE_VALUE)
		return;

	WriteFile(datei, balanceVorlage, (DWORD)strlen(balanceVorlage), &geschrieben, 0);
	CloseHandle(datei);
	Log("Balance.txt neu angelegt");
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
		if (!BalanceNameGleich(name, balanceZeilen[i].name))
			continue;

		if (zahl < balanceZeilen[i].min)
			zahl = balanceZeilen[i].min;
		else if (zahl > balanceZeilen[i].max)
			zahl = balanceZeilen[i].max;

		*balanceZeilen[i].wert = zahl;
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

	balance = balanceOriginal;

	datei = CreateFile(BALANCE_DATEI, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (datei == INVALID_HANDLE_VALUE)
	{
		BalanceVorlageSchreiben();
		return;
	}

	groesse = GetFileSize(datei, 0);

	if (groesse == INVALID_FILE_SIZE || groesse > 0x10000)
		groesse = 0x10000;

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

	Log("Balance.txt gelesen: Gegnerleben %d%%, Gegnerschaden %d%%, Kulisse %d%%",
		balance.gegnerLeben, balance.gegnerSchaden, balance.kulissenReichweite);
}

/*
 * Wird je Levelstart aufgerufen, weil setup.cpp den Revolver dort ohnehin neu
 * setzt. Die HK bleibt aussen vor, die stellt lara1gun.cpp je Schuss selbst.
 */
void BalanceWaffenSetzen()
{
	weapons[WEAPON_PISTOLS].damage = (char)balance.pistolen;
	weapons[WEAPON_UZI].damage = (char)balance.uzi;
	weapons[WEAPON_SHOTGUN].damage = (char)balance.schrotflinte;
	weapons[WEAPON_CROSSBOW].damage = (char)balance.armbrust;
	weapons[WEAPON_TORCH].damage = (char)balance.fackel;
	weapons[WEAPON_REVOLVER].damage =
		(char)(gfLevelId > LVL5_COLOSSEUM ? balance.revolverSpaet : balance.revolverFrueh);
}

/*
 * Schaden eines Gegners an Lara. Absichtlich nur fuer Gegner - Fallen,
 * Stuerze, Feuer, Gift und Ertrinken laufen an dieser Stelle vorbei und
 * bleiben so hart wie im Original.
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
