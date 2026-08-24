// BalanceSchreibPruef - prueft die Schnittstelle des Randomizer zu Balance.txt
// (#108): schreibt er wirklich NUR die drei Prozentzeilen und laesst alles
// andere Zeichen fuer Zeichen stehen?
//
// Bauen (64 Bit, im Ordner Bittenichtbitdefender):
//   set R=..\..\Tomb5 Native win 11 fix\TR5Randomizer\src
//   cl /nologo /EHsc /std:c++17 /I "%R%" BalanceSchreibPruef.cpp
//       "%R%\core\BalanceFile.cpp" "%R%\core\DataFolder.cpp"
//       /Fe:BalanceSchreibPruef.exe
#include "core/BalanceFile.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <string>
#include <windows.h>

static int error = 0;

static void pruefe(const char* was, bool ok, const char* zusatz = "")
{
    if (ok)
        std::printf("    ok    %s %s\n", was, zusatz);
    else
    {
        std::printf("    FEHLER %s %s\n", was, zusatz);
        error++;
    }
}

static void pruefeZahl(const char* was, int ist, int soll)
{
    if (ist == soll)
        std::printf("    ok    %-32s = %d\n", was, ist);
    else
    {
        std::printf("    FEHLER %-32s = %d, erwartet %d\n", was, ist, soll);
        error++;
    }
}

static std::wstring ordner;

static void schreibeDatei(const std::string& inhalt)
{
    const std::wstring p = PathUtil::join(ordner, L"Balance.txt");
    FILE* f = _wfopen(p.c_str(), L"wb");
    std::fwrite(inhalt.data(), 1, inhalt.size(), f);
    std::fclose(f);
}

static std::string liesDatei()
{
    const std::wstring p = PathUtil::join(ordner, L"Balance.txt");
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return std::string();
    std::string s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}

int main()
{
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    ordner = PathUtil::join(tmp, L"BalanceSchreibPruef");
    PathUtil::ensureDir(ordner);
    DeleteFileW(PathUtil::join(ordner, L"Balance.txt").c_str());

    std::printf("--- 1) Datei fehlt: kurze Datei mit den drei Zeilen entsteht\n");
    {
        BalanceFile::Values w;
        w.weaponDamage = 150; w.enemyHealth = 75; w.enemyDamage = 125;
        std::string err;
        pruefe("write meldet Erfolg", BalanceFile::write(ordner, w, &err), err.c_str());

        const std::string t = liesDatei();
        pruefe("WaffenSchaden steht drin", t.find("WaffenSchaden") != std::string::npos);
        pruefe("GegnerLeben steht drin", t.find("GegnerLeben") != std::string::npos);
        pruefe("GegnerSchaden steht drin", t.find("GegnerSchaden") != std::string::npos);
        pruefe("Kopfzeile erklaert die Herkunft", t.find("TR5 Randomizer") != std::string::npos);

        const BalanceFile::Values z = BalanceFile::read(ordner);
        pruefeZahl("gelesen weaponDamage", z.weaponDamage, 150);
        pruefeZahl("gelesen enemyHealth", z.enemyHealth, 75);
        pruefeZahl("gelesen enemyDamage", z.enemyDamage, 125);
    }

    std::printf("--- 2) Vollstaendige Datei: nur die drei Zeilen aendern sich\n");
    {
        const std::string vorher =
            "; Kopf, den der Spieler behalten soll\r\n"
            "Pistolen               = 42     ; (Original 1)\r\n"
            "LP_Hund                = 7      ; (Original 18)\r\n"
            "S_Skelett_Hieb         = 5      ; (Original 400)\r\n"
            "\r\n"
            "WaffenSchaden          = 100    ; (Original 100)  Hinweis\r\n"
            "GegnerLeben            = 100    ; (Original 100)  Hinweis\r\n"
            "GegnerSchaden          = 100    ; (Original 100)  Hinweis\r\n"
            "KulissenReichweite     = 65     ; (Original 65)\r\n"
            "; Schlusskommentar\r\n";
        schreibeDatei(vorher);

        BalanceFile::Values w;
        w.weaponDamage = 200; w.enemyHealth = 50; w.enemyDamage = 175;
        BalanceFile::write(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Pistolen = 42 unangetastet", t.find("Pistolen               = 42") != std::string::npos);
        pruefe("LP_Hund = 7 unangetastet", t.find("LP_Hund                = 7") != std::string::npos);
        pruefe("S_Skelett_Hieb = 5 unangetastet", t.find("S_Skelett_Hieb         = 5") != std::string::npos);
        pruefe("KulissenReichweite unangetastet", t.find("KulissenReichweite     = 65") != std::string::npos);
        pruefe("Kopfkommentar steht noch da", t.find("; Kopf, den der Spieler") != std::string::npos);
        pruefe("Schlusskommentar steht noch da", t.find("; Schlusskommentar") != std::string::npos);
        pruefe("keine Zeile dazugekommen", t.find("TR5 Randomizer") == std::string::npos);

        const BalanceFile::Values z = BalanceFile::read(ordner);
        pruefeZahl("neu weaponDamage", z.weaponDamage, 200);
        pruefeZahl("neu enemyHealth", z.enemyHealth, 50);
        pruefeZahl("neu enemyDamage", z.enemyDamage, 175);

        // Zeilenzahl muss gleich geblieben sein.
        auto zaehle = [](const std::string& s)
        { int n = 0; for (char c : s) if (c == '\n') n++; return n; };
        pruefeZahl("Zeilenzahl unveraendert", zaehle(t), zaehle(vorher));
    }

    std::printf("--- 3) Alte Datei ohne die drei Zeilen: sie kommen ans Ende\n");
    {
        schreibeDatei("Pistolen = 1\r\nLP_Hund = 18\r\n");
        BalanceFile::Values w;
        w.weaponDamage = 120; w.enemyHealth = 90; w.enemyDamage = 110;
        BalanceFile::write(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Pistolen steht noch da", t.find("Pistolen = 1") != std::string::npos);
        pruefe("LP_Hund steht noch da", t.find("LP_Hund = 18") != std::string::npos);
        const BalanceFile::Values z = BalanceFile::read(ordner);
        pruefeZahl("nachgetragen weaponDamage", z.weaponDamage, 120);
        pruefeZahl("nachgetragen enemyDamage", z.enemyDamage, 110);
    }

    std::printf("--- 4) Klein/Grossschreibung, Tabs, letzte Zeile ohne Umbruch\n");
    {
        schreibeDatei("gegnerleben\t=\t42\r\nPistolen = 1");
        BalanceFile::Values w;
        w.weaponDamage = 100; w.enemyHealth = 60; w.enemyDamage = 100;
        BalanceFile::write(ordner, w, nullptr);

        const std::string t = liesDatei();
        const BalanceFile::Values z = BalanceFile::read(ordner);
        pruefeZahl("kleingeschriebene Zeile ersetzt", z.enemyHealth, 60);
        pruefe("nur eine GegnerLeben-Zeile", t.find("egnerLeben") == t.rfind("egnerLeben"));
        pruefe("Pistolen ohne Umbruch ueberlebt", t.find("Pistolen = 1") != std::string::npos);
    }

    std::printf("--- 5) Auskommentierte Zeile gilt nicht als Treffer\n");
    {
        schreibeDatei("; GegnerLeben = 999\r\nPistolen = 1\r\n");
        BalanceFile::Values w;
        w.enemyHealth = 80;
        BalanceFile::write(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Kommentar unveraendert", t.find("; GegnerLeben = 999") != std::string::npos);
        pruefeZahl("echte Zeile angehaengt", BalanceFile::read(ordner).enemyHealth, 80);
    }

    std::printf("--- 6) Ohne Spielordner passiert nichts\n");
    {
        std::string err;
        BalanceFile::Values w;
        pruefe("write meldet Fehler", !BalanceFile::write(L"", w, &err));
        pruefe("mit Begruendung", !err.empty(), err.c_str());
        pruefeZahl("read liefert 100", BalanceFile::read(L"").enemyHealth, 100);
    }

    DeleteFileW(PathUtil::join(ordner, L"Balance.txt").c_str());
    RemoveDirectoryW(ordner.c_str());

    std::printf("\n%s (%d Abweichungen)\n",
                error ? "FEHLGESCHLAGEN" : "ALLES IN ORDNUNG", error);
    return error ? 1 : 0;
}
