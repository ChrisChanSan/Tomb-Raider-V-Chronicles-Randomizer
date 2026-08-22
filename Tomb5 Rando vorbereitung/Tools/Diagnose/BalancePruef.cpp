// BalancePruef - Pruefstand fuer game\balance.cpp (Aufgabe #108). Uebersetzt
// die echte Quelldatei mit und liefert nur die drei Symbole nach, die sie
// sonst aus der Engine zieht: weapons[], gfLevelId, Log(). Legt im
// Arbeitsverzeichnis kurzzeitig eine Balance.txt an und raeumt sie wieder weg.
//
// Bauen (32 Bit, im Ordner Bittenichtbitdefender):
//   set E=..\..\Tomb5 Native win 11 fix\TOMB5 Code\TOMB5
//   cl /nologo /EHsc /W3 /I "%E%" BalancePruef.cpp "%E%\game\balance.cpp"
//       /Fe:BalancePruef.exe
//
// Geprueft wird: Vorlage anlegen, Vorlage wieder einlesen, verbogene Datei
// (BOM, Klein/Grossschreibung, Tabs, Kommentar hinter dem Wert, unbekannte
// Zeile, Zeile ohne Gleichheitszeichen, Wert der keine Zahl ist, letzte Zeile
// ohne Umbruch, Werte ueber und unter der Grenze), die Waffenzuweisung fuer
// frueh und spaet, und der Gegnerschaden bei 0/25/100/1000 Prozent samt
// Ueberlaufdeckel.
#include "tomb5/pch.h"
#include "game/balance.h"
#include "game/larafire.h"
#include "game/gameflow.h"

WEAPON_INFO weapons[9];
uchar gfLevelId = 0;
void Log(const char* s, ...) { (void)s; }

static void zeige(const char* was)
{
	printf("%-9s pist=%ld uzi=%ld schrot=%ld armbr=%ld fackel=%ld | revF=%ld revS=%ld hkSt=%ld hkLa=%ld | leben=%ld schaden=%ld kulisse=%ld\n",
		was, balance.pistolen, balance.uzi, balance.schrotflinte, balance.armbrust, balance.fackel,
		balance.revolverFrueh, balance.revolverSpaet, balance.hkStand, balance.hkLauf,
		balance.gegnerLeben, balance.gegnerSchaden, balance.kulissenReichweite);
}

int main()
{
	DeleteFileA("Balance.txt");

	printf("--- 1) Datei fehlt: Vorlage muss entstehen, Originalwerte gelten\n");
	BalanceLaden();
	zeige("frisch");
	printf("    Balance.txt vorhanden: %s\n",
		GetFileAttributesA("Balance.txt") == INVALID_FILE_ATTRIBUTES ? "NEIN" : "ja");

	printf("--- 2) Die eben geschriebene Vorlage einlesen: muss identisch sein\n");
	BalanceLaden();
	zeige("vorlage");

	printf("--- 3) Verbogene Datei: BOM, Klein/Gross, Tabs, Muell, Grenzwerte\n");
	{
		HANDLE h = CreateFileA("Balance.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
		const char* txt =
			"\xEF\xBB\xBF; Kommentar mit Umlaut \xC3\xA4\xC3\xB6\xC3\xBC\r\n"
			"# noch ein Kommentar\r\n"
			"\r\n"
			"   pistolen   =   7   ; klein geschrieben, viel Luft\r\n"
			"UZI\t=\t99\r\n"
			"Schrotflinte = 500\r\n"			// ueber max -> 127
			"Armbrust = -4\r\n"				// unter min -> 0
			"GegnerLeben=250\r\n"
			"GegnerSchaden = 0\r\n"
			"KulissenReichweite = 1\r\n"		// unter min -> 10
			"UnbekannteZeile = 5\r\n"
			"kaputte zeile ohne gleichheitszeichen\r\n"
			"HK_Stand = abc\r\n"			// keine Zahl -> Original 3
			"Fackel = 12 ; Kommentar hinter dem Wert\r\n"
			"RevolverSpaet = 40";			// letzte Zeile ohne Umbruch
		DWORD n;
		WriteFile(h, txt, (DWORD)strlen(txt), &n, 0);
		CloseHandle(h);
	}
	BalanceLaden();
	zeige("verbogen");
	printf("    erwartet: pist=7 uzi=99 schrot=127 armbr=0 fackel=12 revS=40 hkSt=3 leben=250 schaden=0 kulisse=10\n");

	printf("--- 4) Waffen setzen, Revolver frueh (gfLevelId 0) und spaet\n");
	BalanceWaffenSetzen();
	printf("    frueh: revolver=%d pistolen=%d uzi=%d schrot=%d armbrust=%d fackel=%d\n",
		weapons[WEAPON_REVOLVER].damage, weapons[WEAPON_PISTOLS].damage, weapons[WEAPON_UZI].damage,
		weapons[WEAPON_SHOTGUN].damage, weapons[WEAPON_CROSSBOW].damage, weapons[WEAPON_TORCH].damage);
	gfLevelId = LVL5_DEEPSEA_DIVE;
	BalanceWaffenSetzen();
	printf("    spaet: revolver=%d (erwartet 40)\n", weapons[WEAPON_REVOLVER].damage);

	printf("--- 5) GegnerSchaden\n");
	printf("      0%%: aus 80 wird %d, aus 1 wird %d\n", GegnerSchaden(80), GegnerSchaden(1));
	balance.gegnerSchaden = 25;
	printf("     25%%: aus 80 wird %d, aus 1 wird %d (nie 0)\n", GegnerSchaden(80), GegnerSchaden(1));
	balance.gegnerSchaden = 100;
	printf("    100%%: aus 80 wird %d, aus 10000 wird %d\n", GegnerSchaden(80), GegnerSchaden(10000));
	balance.gegnerSchaden = 1000;
	printf("   1000%%: aus 80 wird %d, aus 10000 wird %d (Deckel 32767)\n", GegnerSchaden(80), GegnerSchaden(10000));

	DeleteFileA("Balance.txt");
	return 0;
}
