// BalancePruef - Pruefstand fuer game\balance.cpp (Aufgabe #108). Uebersetzt
// die echte Quelldatei mit und liefert nur die Symbole nach, die sie sonst aus
// der Engine zieht: weapons[], objects[], gfLevelId, Log(). Legt im
// Arbeitsverzeichnis kurzzeitig eine Balance.txt an und raeumt sie wieder weg.
//
// Bauen (32 Bit, im Ordner Bittenichtbitdefender):
//   set E=..\..\Tomb5 Native win 11 fix\TOMB5 Code\TOMB5
//   cl /nologo /EHsc /W3 /I "%E%" BalancePruef.cpp "%E%\game\balance.cpp"
//       /Fe:BalancePruef.exe
//
// Geprueft wird: Vorlage anlegen und wieder einlesen (sie muss die
// Voreinstellungen exakt reproduzieren - die Vorlage wird aus derselben
// Tabelle erzeugt, aus der die Werte kommen), eine verbogene Datei (BOM,
// Klein/Grossschreibung, Tabs, Kommentar hinter dem Wert, unbekannte Zeile,
// Zeile ohne Gleichheitszeichen, Wert der keine Zahl ist, letzte Zeile ohne
// Umbruch, Werte ueber und unter der Grenze), die Waffenzuweisung fuer frueh
// und spaet, die Lebenspunkte je Gegner samt Prozentzeile obendrauf, und der
// Gegnerschaden bei 0/25/100/1000 Prozent samt Ueberlaufdeckel.
#include "tomb5/pch.h"
#include "game/balance.h"
#include "game/larafire.h"
#include "game/gameflow.h"
#include "game/objects.h"

WEAPON_INFO weapons[9];
OBJECT_INFO objects[NUMBER_OBJECTS];
uchar gfLevelId = 0;
void Log(const char* s, ...) { (void)s; }

static int fehler = 0;

static void pruefe(const char* was, long ist, long soll)
{
	if (ist == soll)
		printf("    ok    %-24s = %ld\n", was, ist);
	else
	{
		printf("    FEHLER %-24s = %ld, erwartet %ld\n", was, ist, soll);
		fehler++;
	}
}

// Die Gegner so aufstellen, wie setup.cpp es tut: intelligent, mit ihren
// Originalwerten. Nur die paar, die der Test anfasst.
static void gegnerAufstellen()
{
	for (int i = 0; i < NUMBER_OBJECTS; i++)
	{
		objects[i].hit_points = -16384;
		objects[i].intelligent = 0;
	}

	struct { int slot; short lp; } vanilla[] =
	{
		{ BLUE_GUARD, 24 }, { DOG, 18 }, { SAS, 800 }, { ROMAN_GOD, 300 },
		{ TWOGUN, 90 }, { IMP, 12 }, { SKELETON, 100 }
	};

	for (int i = 0; i < sizeof(vanilla) / sizeof(vanilla[0]); i++)
	{
		objects[vanilla[i].slot].hit_points = vanilla[i].lp;
		objects[vanilla[i].slot].intelligent = 1;
	}

	// Lara und die Fackel: Lebenspunkte, aber nicht intelligent. Die duerfen
	// die Prozentzeile nie anfassen.
	objects[LARA].hit_points = 1000;
	objects[FLARE_ITEM].hit_points = 256;
}

static void schreibeDatei(const char* txt)
{
	HANDLE h = CreateFileA("Balance.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
	DWORD n;
	WriteFile(h, txt, (DWORD)strlen(txt), &n, 0);
	CloseHandle(h);
}

int main()
{
	DeleteFileA("Balance.txt");

	printf("--- 1) Datei fehlt: Vorlage entsteht, Originalwerte gelten\n");
	BalanceLaden();
	printf("    Balance.txt vorhanden: %s\n",
		GetFileAttributesA("Balance.txt") == INVALID_FILE_ATTRIBUTES ? "NEIN" : "ja");
	pruefe("Pistolen", balance.pistolen, 1);
	pruefe("RevolverSpaet", balance.revolverSpaet, 15);
	pruefe("LP_TRex", balance.lpTRex, 800);
	pruefe("S_Skelett_Hieb", balance.sSkelettHieb, 400);
	pruefe("S_Soldat_Spaet", balance.sSoldatSpaet, 60);
	pruefe("S_Iris", balance.sIris, 12);
	pruefe("KulissenReichweite", balance.kulissenReichweite, 65);

	printf("--- 2) Die eben geschriebene Vorlage einlesen: muss identisch sein\n");
	{
		BALANCE_INFO vorher = balance;
		BalanceLaden();
		if (memcmp(&vorher, &balance, sizeof(BALANCE_INFO)) == 0)
			printf("    ok    Vorlage reproduziert alle %d Bytes\n", (int)sizeof(BALANCE_INFO));
		else
		{
			printf("    FEHLER Vorlage weicht von den Voreinstellungen ab\n");
			fehler++;
		}
	}

	printf("--- 3) Verbogene Datei: BOM, Klein/Gross, Tabs, Muell, Grenzwerte\n");
	schreibeDatei(
		"\xEF\xBB\xBF; Kommentar mit Umlaut \xC3\xA4\xC3\xB6\xC3\xBC\r\n"
		"# noch ein Kommentar\r\n"
		"\r\n"
		"   pistolen   =   7   ; klein geschrieben, viel Luft\r\n"
		"UZI\t=\t99\r\n"
		"Schrotflinte = 500\r\n"			// ueber max -> 127
		"Armbrust = -4\r\n"				// unter min -> 0
		"LP_Hund = 5\r\n"
		"LP_TRex = 99999\r\n"			// ueber max -> 32000
		"LP_RoemischerGott = 0\r\n"		// unter min -> 1
		"S_Hund_Sprung = 500\r\n"
		"S_Autogun = 1\r\n"
		"GegnerLeben=200\r\n"
		"GegnerSchaden = 0\r\n"
		"KulissenReichweite = 1\r\n"		// unter min -> 10
		"UnbekannteZeile = 5\r\n"
		"kaputte zeile ohne gleichheitszeichen\r\n"
		"HK_Stand = abc\r\n"			// keine Zahl -> Original 3
		"Fackel = 12 ; Kommentar hinter dem Wert\r\n"
		"RevolverSpaet = 40");			// letzte Zeile ohne Umbruch
	BalanceLaden();
	pruefe("Pistolen", balance.pistolen, 7);
	pruefe("Uzi", balance.uzi, 99);
	pruefe("Schrotflinte", balance.schrotflinte, 127);
	pruefe("Armbrust", balance.armbrust, 0);
	pruefe("Fackel", balance.fackel, 12);
	pruefe("HK_Stand", balance.hkStand, 3);
	pruefe("RevolverSpaet", balance.revolverSpaet, 40);
	pruefe("LP_Hund", balance.lpHund, 5);
	pruefe("LP_TRex", balance.lpTRex, 32000);
	pruefe("LP_RoemischerGott", balance.lpRoemischerGott, 1);
	pruefe("S_Hund_Sprung", balance.sHundSprung, 500);
	pruefe("S_Autogun", balance.sAutogun, 1);
	pruefe("GegnerLeben", balance.gegnerLeben, 200);
	pruefe("KulissenReichweite", balance.kulissenReichweite, 10);

	printf("--- 4) Waffen setzen, Revolver frueh (gfLevelId 0) und spaet\n");
	BalanceWaffenSetzen();
	pruefe("weapons Revolver frueh", weapons[WEAPON_REVOLVER].damage, 6);
	pruefe("weapons Pistolen", weapons[WEAPON_PISTOLS].damage, 7);
	pruefe("weapons Schrotflinte", weapons[WEAPON_SHOTGUN].damage, 127);
	gfLevelId = LVL5_DEEPSEA_DIVE;
	BalanceWaffenSetzen();
	pruefe("weapons Revolver spaet", weapons[WEAPON_REVOLVER].damage, 40);

	printf("--- 5) Lebenspunkte: eigener Wert, dann 200 Prozent obendrauf\n");
	gegnerAufstellen();
	BalanceGegnerSetzen();
	pruefe("Hund 5 x 200%", objects[DOG].hit_points, 10);
	pruefe("TRex 32000 gedeckelt", objects[SAS].hit_points, 32000);
	pruefe("Gott 1 x 200%", objects[ROMAN_GOD].hit_points, 2);
	pruefe("Wachmann 24 x 200%", objects[BLUE_GUARD].hit_points, 48);
	pruefe("Lara unberuehrt", objects[LARA].hit_points, 1000);
	pruefe("Fackel unberuehrt", objects[FLARE_ITEM].hit_points, 256);

	printf("--- 6) Lebenspunkte bei 100 Prozent: nur die eigenen Werte\n");
	balance.gegnerLeben = 100;
	gegnerAufstellen();
	BalanceGegnerSetzen();
	pruefe("Hund", objects[DOG].hit_points, 5);
	pruefe("TwoGun unveraendert", objects[TWOGUN].hit_points, 90);

	printf("--- 7) Gegnerschaden\n");
	balance.gegnerSchaden = 0;
	pruefe("0% aus 80", GegnerSchaden(80), 0);
	balance.gegnerSchaden = 25;
	pruefe("25% aus 80", GegnerSchaden(80), 20);
	pruefe("25% aus 1 (nie 0)", GegnerSchaden(1), 1);
	balance.gegnerSchaden = 100;
	pruefe("100% aus 10000", GegnerSchaden(10000), 10000);
	balance.gegnerSchaden = 1000;
	pruefe("1000% aus 10000 gedeckelt", GegnerSchaden(10000), 32767);

	DeleteFileA("Balance.txt");
	printf("\n%s (%d Abweichungen)\n", fehler ? "FEHLGESCHLAGEN" : "ALLES IN ORDNUNG", fehler);
	return fehler ? 1 : 0;
}
