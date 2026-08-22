// BalanceSchreibPruef - prueft die Schnittstelle des Randomizer zu Balance.txt
// (#108): schreibt er wirklich NUR die drei Prozentzeilen und laesst alles
// andere Zeichen fuer Zeichen stehen?
//
// Bauen (64 Bit, im Ordner Bittenichtbitdefender):
//   set R=..\..\Tomb5 Native win 11 fix\TR5Randomizer\src
//   cl /nologo /EHsc /std:c++17 /I "%R%" BalanceSchreibPruef.cpp
//       "%R%\core\BalanceDatei.cpp" "%R%\core\DataFolder.cpp"
//       /Fe:BalanceSchreibPruef.exe
#include "core/BalanceDatei.h"
#include "core/DataFolder.h"

#include <cstdio>
#include <string>
#include <windows.h>

static int fehler = 0;

static void pruefe(const char* was, bool ok, const char* zusatz = "")
{
    if (ok)
        std::printf("    ok    %s %s\n", was, zusatz);
    else
    {
        std::printf("    FEHLER %s %s\n", was, zusatz);
        fehler++;
    }
}

static void pruefeZahl(const char* was, int ist, int soll)
{
    if (ist == soll)
        std::printf("    ok    %-32s = %d\n", was, ist);
    else
    {
        std::printf("    FEHLER %-32s = %d, erwartet %d\n", was, ist, soll);
        fehler++;
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
        BalanceDatei::Werte w;
        w.waffenSchaden = 150; w.gegnerLeben = 75; w.gegnerSchaden = 125;
        std::string err;
        pruefe("schreiben meldet Erfolg", BalanceDatei::schreiben(ordner, w, &err), err.c_str());

        const std::string t = liesDatei();
        pruefe("WaffenSchaden steht drin", t.find("WaffenSchaden") != std::string::npos);
        pruefe("GegnerLeben steht drin", t.find("GegnerLeben") != std::string::npos);
        pruefe("GegnerSchaden steht drin", t.find("GegnerSchaden") != std::string::npos);
        pruefe("Kopfzeile erklaert die Herkunft", t.find("TR5 Randomizer") != std::string::npos);

        const BalanceDatei::Werte z = BalanceDatei::lesen(ordner);
        pruefeZahl("gelesen waffenSchaden", z.waffenSchaden, 150);
        pruefeZahl("gelesen gegnerLeben", z.gegnerLeben, 75);
        pruefeZahl("gelesen gegnerSchaden", z.gegnerSchaden, 125);
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

        BalanceDatei::Werte w;
        w.waffenSchaden = 200; w.gegnerLeben = 50; w.gegnerSchaden = 175;
        BalanceDatei::schreiben(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Pistolen = 42 unangetastet", t.find("Pistolen               = 42") != std::string::npos);
        pruefe("LP_Hund = 7 unangetastet", t.find("LP_Hund                = 7") != std::string::npos);
        pruefe("S_Skelett_Hieb = 5 unangetastet", t.find("S_Skelett_Hieb         = 5") != std::string::npos);
        pruefe("KulissenReichweite unangetastet", t.find("KulissenReichweite     = 65") != std::string::npos);
        pruefe("Kopfkommentar steht noch da", t.find("; Kopf, den der Spieler") != std::string::npos);
        pruefe("Schlusskommentar steht noch da", t.find("; Schlusskommentar") != std::string::npos);
        pruefe("keine Zeile dazugekommen", t.find("TR5 Randomizer") == std::string::npos);

        const BalanceDatei::Werte z = BalanceDatei::lesen(ordner);
        pruefeZahl("neu waffenSchaden", z.waffenSchaden, 200);
        pruefeZahl("neu gegnerLeben", z.gegnerLeben, 50);
        pruefeZahl("neu gegnerSchaden", z.gegnerSchaden, 175);

        // Zeilenzahl muss gleich geblieben sein.
        auto zaehle = [](const std::string& s)
        { int n = 0; for (char c : s) if (c == '\n') n++; return n; };
        pruefeZahl("Zeilenzahl unveraendert", zaehle(t), zaehle(vorher));
    }

    std::printf("--- 3) Alte Datei ohne die drei Zeilen: sie kommen ans Ende\n");
    {
        schreibeDatei("Pistolen = 1\r\nLP_Hund = 18\r\n");
        BalanceDatei::Werte w;
        w.waffenSchaden = 120; w.gegnerLeben = 90; w.gegnerSchaden = 110;
        BalanceDatei::schreiben(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Pistolen steht noch da", t.find("Pistolen = 1") != std::string::npos);
        pruefe("LP_Hund steht noch da", t.find("LP_Hund = 18") != std::string::npos);
        const BalanceDatei::Werte z = BalanceDatei::lesen(ordner);
        pruefeZahl("nachgetragen waffenSchaden", z.waffenSchaden, 120);
        pruefeZahl("nachgetragen gegnerSchaden", z.gegnerSchaden, 110);
    }

    std::printf("--- 4) Klein/Grossschreibung, Tabs, letzte Zeile ohne Umbruch\n");
    {
        schreibeDatei("gegnerleben\t=\t42\r\nPistolen = 1");
        BalanceDatei::Werte w;
        w.waffenSchaden = 100; w.gegnerLeben = 60; w.gegnerSchaden = 100;
        BalanceDatei::schreiben(ordner, w, nullptr);

        const std::string t = liesDatei();
        const BalanceDatei::Werte z = BalanceDatei::lesen(ordner);
        pruefeZahl("kleingeschriebene Zeile ersetzt", z.gegnerLeben, 60);
        pruefe("nur eine GegnerLeben-Zeile", t.find("egnerLeben") == t.rfind("egnerLeben"));
        pruefe("Pistolen ohne Umbruch ueberlebt", t.find("Pistolen = 1") != std::string::npos);
    }

    std::printf("--- 5) Auskommentierte Zeile gilt nicht als Treffer\n");
    {
        schreibeDatei("; GegnerLeben = 999\r\nPistolen = 1\r\n");
        BalanceDatei::Werte w;
        w.gegnerLeben = 80;
        BalanceDatei::schreiben(ordner, w, nullptr);

        const std::string t = liesDatei();
        pruefe("Kommentar unveraendert", t.find("; GegnerLeben = 999") != std::string::npos);
        pruefeZahl("echte Zeile angehaengt", BalanceDatei::lesen(ordner).gegnerLeben, 80);
    }

    std::printf("--- 6) Ohne Spielordner passiert nichts\n");
    {
        std::string err;
        BalanceDatei::Werte w;
        pruefe("schreiben meldet Fehler", !BalanceDatei::schreiben(L"", w, &err));
        pruefe("mit Begruendung", !err.empty(), err.c_str());
        pruefeZahl("lesen liefert 100", BalanceDatei::lesen(L"").gegnerLeben, 100);
    }

    DeleteFileW(PathUtil::join(ordner, L"Balance.txt").c_str());
    RemoveDirectoryW(ordner.c_str());

    std::printf("\n%s (%d Abweichungen)\n",
                fehler ? "FEHLGESCHLAGEN" : "ALLES IN ORDNUNG", fehler);
    return fehler ? 1 : 0;
}
