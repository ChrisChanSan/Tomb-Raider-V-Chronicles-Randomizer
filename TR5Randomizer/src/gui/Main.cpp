/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
//----------------------------------------------------------------------------
// TR5 Randomizer - Oberflaeche.
//
// Eigener Code auf Dear ImGui (MIT, Omar Cornut) mit dem Direct3D-11-Backend.
// Der Aufbau ist bewusst schlicht: ein Fenster, vier Schritte von oben nach
// unten. Wer das Werkzeug zum ersten Mal startet, soll ohne Anleitung
// durchkommen.
//
// Alles Fachliche liegt in core/ und ist ohne Oberflaeche lauffaehig - siehe
// Randomize.cpp, das dieselben Schritte auf der Kommandozeile durchfuehrt.
// Diese Datei enthaelt deshalb keine Spiellogik, nur Bedienung.
//----------------------------------------------------------------------------
#include "core/ApplyPatch.h"
#include "core/BuildId.h"
#include "core/Config.h"
#include "core/DataFolder.h"
#include "core/Json.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/ScriptDat.h"
#include "core/Tr5Level.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

//----------------------------------------------------------------------------
// D3D11-Geruest. Nichts Besonderes - genau so viel, wie ImGui zum Zeichnen
// braucht.
//----------------------------------------------------------------------------
namespace
{
    ID3D11Device*           g_dev = nullptr;
    ID3D11DeviceContext*    g_ctx = nullptr;
    IDXGISwapChain*         g_swap = nullptr;
    bool                    g_fensterGezogen = false;   // WM_EXITSIZEMOVE gesehen
    ID3D11RenderTargetView* g_rtv = nullptr;

    void createRTV()
    {
        ID3D11Texture2D* back = nullptr;
        g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
        if (back) { g_dev->CreateRenderTargetView(back, nullptr, &g_rtv); back->Release(); }
    }

    void releaseRTV() { if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; } }

    bool createDevice(HWND hwnd)
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Numerator = 60;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL fl;
        const D3D_FEATURE_LEVEL want[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                   want, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
        // Ohne brauchbare Grafikkarte auf den Software-Rasterizer ausweichen -
        // die Oberflaeche ist ein paar Rechtecke, das reicht dafuer allemal.
        if (hr == DXGI_ERROR_UNSUPPORTED)
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                               want, 2, D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, &fl, &g_ctx);
        if (FAILED(hr)) return false;

        createRTV();
        return true;
    }

    void destroyDevice()
    {
        releaseRTV();
        if (g_swap) { g_swap->Release(); g_swap = nullptr; }
        if (g_ctx) { g_ctx->Release();  g_ctx = nullptr; }
        if (g_dev) { g_dev->Release();  g_dev = nullptr; }
    }

    LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

        switch (msg)
        {
        case WM_SIZE:
            if (g_dev && wp != SIZE_MINIMIZED)
            {
                releaseRTV();
                g_swap->ResizeBuffers(0, (UINT)LOWORD(lp), (UINT)HIWORD(lp), DXGI_FORMAT_UNKNOWN, 0);
                createRTV();
            }
            // Maximieren/Wiederherstellen kommt ohne WM_EXITSIZEMOVE - auch
            // das soll die gespeicherte Groesse aktualisieren.
            if (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) g_fensterGezogen = true;
            return 0;
        case WM_EXITSIZEMOVE:
            // Der Nutzer hat das Fenster gezogen - die Hauptschleife schreibt
            // daraufhin die Einstellungsdatei (Nutzerwunsch: Groesse merken).
            g_fensterGezogen = true;
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;   // Alt-Menue unterdruecken
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Ordnerauswahl ueber die Windows-eigene Dialogbox.
    bool pickFolder(HWND owner, std::wstring& out)
    {
        bool ok = false;
        IFileOpenDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg))))
        {
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dlg->SetTitle(L"Ordner der Tomb Raider Chronicles-Installation waehlen");
            if (SUCCEEDED(dlg->Show(owner)))
            {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item)) && item)
                {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p)
                    {
                        out = p;
                        CoTaskMemFree(p);
                        ok = true;
                    }
                    item->Release();
                }
            }
            dlg->Release();
        }
        return ok;
    }
}

//----------------------------------------------------------------------------
// Zustand der Anwendung
//----------------------------------------------------------------------------
namespace
{
    // Nicht spielbare Dateien. Sie gehoeren zu einer vollstaendigen
    // Installation und werden bei der Pruefsummenkontrolle mitgeprueft, aber
    // sie zu randomisieren hat keinen Sinn: Title ist der Titelbildschirm -
    // dort etwas zu veraendern kann nur schaden -, del und richcut2 sind reine
    // Cutscene-Level ohne Spielinhalt (richcut2 hat zwei Items, del zehn,
    // davon kein einziges aufnehmbares).
    bool isRandomizable(const std::string& fileName)
    {
        return fileName != "Title.trc" && fileName != "del.trc" && fileName != "richcut2.trc";
    }

    struct LevelRow
    {
        std::string name;
        LevelState  state = LevelState::Missing;
        bool        enabled = true;
        std::string result;
    };

    struct App
    {
        Config                   cfg;
        DataFolder               data;
        std::vector<LevelRow>    levels;
        std::string              status;
        bool                     statusIsError = false;
        bool                     sourceOk = false;
        bool                     payloadOk = false;   // Pruefsummenliste geladen?
        std::string              payloadError;
        char                     seedBuf[64] = {};
        bool                     busy = false;

        // Es wird direkt in der Installation des Nutzers gearbeitet. Die
        // Originale liegen daneben in data\Backup und sind ab dann der
        // Massstab - siehe ensureBackup().
        std::wstring dataDir() const
        {
            const std::wstring sub = PathUtil::join(cfg.sourceDir, L"data");
            return PathUtil::dirExists(sub) ? sub : cfg.sourceDir;
        }
        std::wstring backupDir() const { return PathUtil::join(dataDir(), L"Backup"); }
        std::wstring payloadDir() const { return PathUtil::join(PathUtil::exeDir(), L"data"); }

        bool haveBackup = false;
        int  skippedNonPlayable = 0;
        std::string scriptNote;      // was SCRIPT.DAT beim letzten Lauf ergab

        // Ein Lauf laeuft nicht am Stueck durch, sondern schrittweise: pro Bild
        // ein Level. Sonst friert das Fenster fuer die Dauer aller 15 Level
        // ein, Windows meldet "reagiert nicht", und der Nutzer haelt es fuer
        // einen Absturz - obwohl alles laeuft. Der Fortschrittsbalken braucht
        // dieselbe Aufteilung, sonst haette er nichts anzuzeigen.
        enum class Phase { Idle, Copy, Levels, Finish, Debug };
        Phase phase = Phase::Idle;
        size_t stepIndex = 0;
        int    doneCount = 0, failCount = 0, plannedCount = 0;
        std::string phaseText;

        float progress() const
        {
            if (phase == Phase::Idle) return 0.0f;
            if (phase == Phase::Copy) return 0.0f;
            if (plannedCount <= 0) return 1.0f;
            return static_cast<float>(doneCount + failCount) / static_cast<float>(plannedCount);
        }

        // Was soll ueberhaupt randomisiert werden?
        bool optOtherPickups = true;
        bool optKeyItems = true;
        // Zusaetzliche Gegner ins Level setzen. Welche Typen ein Level vertraegt,
        // steht in der Kuration (ENEMY_TYPES in ZoneAnalyse.ps1) - ist sie leer,
        // bleibt der Haken folgenlos.
        bool optEnemies = false;
        int  optEnemyCount = 6;

        // Vorhandene Gegner untereinander tauschen.
        bool optEnemySwap = false;
        // Gegnerpool-Stufe: 0 nur Levelpool, 1 Cross-Mix (bis zu drei fremde
        // Typen), 2 Allsanity (der ganze Pool, jede Rolle wuerfelt frei).
        // Wirkt nur zusammen mit optEnemySwap oder optEnemies.
        int optEnemyPool = 0;
        // Zufaellige Outfits (Lara_Andrea/Lara_Joby/Lara_Rich) je Abschnitt.
        bool optOutfits = false;
        // Volles Waffenset in jedem Level ist STANDARD (Nutzerentscheidung,
        // kein Haken): opt.allWeapons wird beim Lauf fest auf true gesetzt.
        // Deshalb auch kein Seed-Bit dafuer - eine Konstante unterscheidet
        // keine Spiele.

        // Secrets (#53) - PLATZHALTER, noch ohne Wirkung. 0 Aus, 1 normaler
        // Item-Pool, 2 Glitched-Liste, 3 Custom-Nutzerliste. Bekommt sein
        // Seed-Bit erst, wenn die Logik existiert - ein Platzhalter, der den
        // Seed schon veraendert, waere spaeter nicht rueckwaerts vergleichbar.
        int optSecrets = 0;

        // Weitere PLATZHALTER (Nutzerwunsch 2026-08-14), alle ohne Wirkung,
        // ohne Seed-Bit und ohne Persistenz, bis die jeweilige Logik steht.
        int  optItemMenge = 0;          // 0 Vanilla Anzahl, 1 mehr, 2 viel mehr (#36)
        bool phTexRgb = false;          // Farbwuerfel ueber Vanilla-Texturen
        bool phTexMix = false;          // Texturen leveluebergreifend mixen
        bool phTexRemaster = false;     // Remaster-Texturen patchen
        bool phRoomKulissen = false;    // Kulissenbereiche einbinden
        bool phRoomNachbar = false;     // gesperrte Nachbarraeume oeffnen
        bool phJoby4NoWater = false;    // Deepsea Dive ohne Wasser
        bool phLevelAbschnitte = false; // Abschnitte (Rom/Russland/...) tauschen
        bool phLevelReihenfolge = false;// alle 13 Level durchwuerfeln
        bool phSpiegeln = false;        // GF_MIRROR je Level
        bool phMusikSwitch = false;     // Audiotrack je Level wuerfeln
        bool phMusikTrigger = false;    // Musik-/Sound-Trigger-IDs wuerfeln
        bool phOutfitTr1 = false;       // klassisches TR1-Outfit im Pool
        bool phOutfitTr2 = false;       // TR2-Outfits im Pool
        bool phOutfitTr3 = false;       // TR3-Outfits im Pool

        // Die Auswahl geht in den Seed ein. Sonst erzeugt derselbe Seed mit
        // anderen Haken ein anderes Spiel, und ein Fehlerbericht "Seed X ist
        // unspielbar" waere nicht nachstellbar - also genau das, was die
        // Seed-Mechanik verhindern soll. Der Nutzer sieht deshalb unten auch
        // den wirksamen Wert, nicht nur den eingegebenen Text.
        uint64_t effectiveSeed() const
        {
            uint64_t flags = 0;
            if (optOtherPickups) flags |= 1;
            if (optKeyItems)     flags |= 2;
            if (optEnemies)          flags |= 4;
            if (optEnemyPool == 1)   flags |= 8;    // Cross-Mix
            if (optEnemySwap)        flags |= 16;
            if (optOutfits)          flags |= 32;
            if (optEnemyPool == 2)   flags |= 64;   // Allsanity
            // Bit 64 war kurz "Alle Waffen ueberall" - seit die Standard
            // sind, traegt es jetzt Allsanity.
            // Die Anzahl gehoert genauso in den Seed wie die Haken: mit 3 und
            // mit 12 Gegnern ist es nicht dasselbe Spiel.
            flags |= static_cast<uint64_t>(optEnemyCount & 0xFF) << 8;
            // Dasselbe fuer die Item-Menge (#36).
            flags |= static_cast<uint64_t>(optItemMenge & 0x3) << 16;

            // FNV-1a ueber Seed-Text und Auswahl, damit beides gleichwertig
            // eingeht - eine blosse XOR-Verknuepfung haette bei benachbarten
            // Flags aehnliche Ergebnisse geliefert.
            uint64_t h = SeedText::parse(seedBuf);
            for (int i = 0; i < 8; ++i)
            {
                h ^= (flags >> (i * 8)) & 0xFF;
                h *= 0x100000001B3ull;
            }
            return h;
        }

        // --- Debugwerkzeug: Sektoren mit Markern befuellen ---
        //
        // Beantwortet die Frage, die keine Zahl beantwortet: liegt die Grenze an
        // der RICHTIGEN Stelle? Der Blick ins Level findet Fehler, die eine
        // Statistik nicht zeigt - so wurde in rich2 das Leck vom Erdgeschoss in
        // den Aufzugsschacht gefunden.
        bool dbgOpen = false;
        // Fenster "Gegnerarten" (#95): welche Arten darf der Pool ziehen?
        // Gehalten wird die ABWAHL - siehe Config::gegnerAus.
        bool artenOpen = false;
        std::set<int> optGegnerAus;
        // Fenster "Installations-Pfade" (#102).
        bool pfadeOpen = false;
        bool dbgUseFill = true;
        int  dbgFillType = 223;          // Pickup Item4 - im Spiel selten, faellt auf
        std::vector<std::string> dbgLog;

        std::wstring debugDir() const { return PathUtil::join(PathUtil::exeDir(), L"Debug Level"); }
    };

// Wo liegt die gesuchte Datei wirklich?
//
// Der Nutzer waehlt im Dateidialog den SPIELORDNER - das ist das Natuerliche,
// und genau das hat er getan. Die Leveldateien liegen aber im Unterordner
// "data". Statt ihn mit einem Hinweistext zu belehren, sucht das Werkzeug an
// beiden Stellen. Zurueck kommt der Ordner, der die Datei wirklich enthaelt,
// oder leer.
//
// Gross- und Kleinschreibung ist unter Windows egal, "Data" und "DATA" also
// mit abgedeckt.
static std::wstring loeseOrdner(const std::wstring& gewaehlt, const wchar_t* datei)
{
    if (gewaehlt.empty()) return std::wstring();
    if (PathUtil::fileExists(PathUtil::join(gewaehlt, datei))) return gewaehlt;
    const std::wstring unten = PathUtil::join(gewaehlt, L"data");
    if (PathUtil::fileExists(PathUtil::join(unten, datei))) return unten;
    return std::wstring();
}

// Die aufgeloesten Ordner, wie sie an Kern und Patchweg gehen.
static std::wstring tr3Ordner(const App& a)  { return loeseOrdner(a.cfg.tr3Dir, L"CRASH.TR2"); }
static std::wstring goldOrdner(const App& a) { return loeseOrdner(a.cfg.tr3GoldDir, L"chunnel.TR2"); }

// Zwischenlager fuer die aus Tomb Raider 3 entnommenen Pakete (#103).
//
// Liegt neben dem Programm. Die Dateien entstehen auf dem Rechner dessen, der
// die Spiele besitzt, und bleiben dort - lizenzrechtlich unbedenklich. NIE
// MITAUSLIEFERN: der Ordner bekommt dafuer eine Warnung als Textdatei, damit
// es beim Packen auffaellt.

static std::wstring lagerDir(const App& a)
{
    if (tr3Ordner(a).empty()) return std::wstring();     // ohne Quelle kein Lager
    const std::wstring dir = PathUtil::join(PathUtil::exeDir(), L"Import-Export");
    if (!PathUtil::ensureDir(dir)) return std::wstring();

    const std::wstring hinweis = PathUtil::join(dir, L"NICHT MITLIEFERN.txt");
    if (!PathUtil::fileExists(hinweis))
    {
        FILE* h = nullptr;
        if (_wfopen_s(&h, hinweis.c_str(), L"wb") == 0 && h)
        {
            std::fputs(
                "Dieser Ordner ist ein Zwischenlager.\r\n\r\n"
                "Darin liegen Modelle, Texturen und Klaenge, die das Programm aus\r\n"
                "DEINER Installation von Tomb Raider 3 entnommen hat. Sie sparen\r\n"
                "Zeit: ohne sie wird bei jedem Level neu entnommen.\r\n\r\n"
                "WEITERGEBEN DARFST DU DIESEN ORDNER NICHT. Er enthaelt Daten aus\r\n"
                "einem Spiel, das jeder selbst besitzen muss. Beim Packen einer\r\n"
                "Weitergabe gehoert er heraus.\r\n\r\n"
                "Loeschen ist jederzeit gefahrlos - es wird dann eben neu entnommen.\r\n", h);
            std::fclose(h);
        }
    }
    return dir;
}



    void setStatus(App& a, const std::string& s, bool error = false)
    {
        a.status = s;
        a.statusIsError = error;
    }

    void inspectSource(App& a)
    {
        a.levels.clear();
        a.sourceOk = false;

        // Ohne geladene Pruefsummenliste hat eine Pruefung keinen Sinn - sie
        // wuerde eine leere Liste durchwinken. Deshalb hier abbrechen und den
        // eigentlichen Grund nennen, statt ihn beim Ordnerwaehlen zu verlieren.
        if (!a.payloadOk) { setStatus(a, a.payloadError, true); return; }

        if (a.cfg.sourceDir.empty()) { setStatus(a, "Noch kein Spielordner gewaehlt."); return; }

        // Existiert schon ein Backup, ist DAS der Massstab - nicht data\, denn
        // dort koennen bereits randomisierte Level liegen. Sonst wuerde beim
        // zweiten Durchlauf ein randomisiertes Level als "Original" gelten und
        // das echte Original waere fuer immer weg.
        a.haveBackup = PathUtil::dirExists(a.backupDir());
        const std::wstring master = a.haveBackup ? a.backupDir() : a.dataDir();

        auto st = a.data.inspect(master);
        int skipped = 0;
        for (const LevelStatus& s : st)
        {
            // Nicht spielbare Dateien werden geprueft, aber nicht angeboten.
            if (!isRandomizable(s.name)) { ++skipped; continue; }

            LevelRow r;
            r.name = s.name;
            r.state = s.state;
            r.enabled = (s.state == LevelState::Vanilla);
            a.levels.push_back(r);
        }
        a.skippedNonPlayable = skipped;

        // Geprueft wird weiterhin die vollstaendige Liste - eine Installation,
        // der Title.trc fehlt, ist keine gueltige Quelle, auch wenn wir die
        // Datei nicht anfassen.
        std::string why;
        a.sourceOk = DataFolder::isUsableSource(st, why);

        if (a.sourceOk)
            setStatus(a, a.haveBackup
                ? "Installation bereit. Originale liegen in data\\Backup."
                : "Installation geprueft - alle Level unveraendert. Beim ersten "
                  "Randomisieren wird data\\Backup angelegt.");
        else
            setStatus(a, (a.haveBackup ? "Das Backup ist beschaedigt: " : "") + why, true);
    }

    // SCRIPT.DAT: den Ireland-Leveln das Kind-Lara-Bit nehmen.
    //
    // Ohne das laeuft Lara in andy1, Andy2 und andy3 als Kind herum und kann
    // ueberhaupt keine Waffe tragen - GF_YOUNGLARA schaltet an acht Stellen im
    // Motor das Waffensystem ab. Nutzervorgabe: die Andy-Level muessen immer
    // Waffen haben, sonst steht Lara einem Two Gun mit leeren Haenden gegenueber.
    //
    // Bedingungslos und wiederholbar: ein schon geloeschtes Bit noch einmal zu
    // loeschen aendert nichts, und der Randomizer laeuft mehrfach ueber
    // dieselbe Installation.
    //
    // Vor dem ersten Schreiben wird das Original daneben gesichert - dieselbe
    // Zusage wie bei den Leveln: nichts geht unwiederbringlich verloren.
    bool doScriptDat(App& a, std::string& err, std::string& info)
    {
        const std::wstring skript = PathUtil::join(a.dataDir(), L"SCRIPT.DAT");
        if (!PathUtil::fileExists(skript))
        {
            info = "SCRIPT.DAT nicht gefunden - die Andy-Level bleiben Kind-Lara.";
            return true;                      // kein Abbruchgrund
        }

        const std::wstring sicherung = PathUtil::join(a.backupDir(), L"SCRIPT.DAT");
        if (PathUtil::dirExists(a.backupDir()) && !PathUtil::fileExists(sicherung))
            CopyFileW(skript.c_str(), sicherung.c_str(), TRUE);

        // Immer aus der Sicherung lesen, wenn es sie gibt: sonst arbeitet ein
        // zweiter Lauf auf dem Ergebnis des ersten.
        ScriptDat sd;
        const std::wstring quelle = PathUtil::fileExists(sicherung) ? sicherung : skript;
        if (!sd.load(quelle, err)) return false;

        // Die Eingriffe selbst stehen in ScriptDat - dasselbe ruft die CLI
        // auf, damit beide Wege nicht auseinanderlaufen.
        ScriptDat::Eingriffe e;
        sd.randomizerEingriffe(e);

        if (e.levelFehlend > 0)
        {
            info = "SCRIPT.DAT: " + std::to_string(e.levelFehlend) + " Ireland-Level nicht im Skript gefunden.";
            return true;
        }

        if (!sd.save(skript, err)) return false;

        info = e.flagsGeaendert > 0
            ? "SCRIPT.DAT: Kind-Lara in " + std::to_string(e.flagsGeaendert) + " Ireland-Leveln abgeschaltet."
            : "SCRIPT.DAT: Kind-Lara war bereits abgeschaltet.";
        if (!e.keyFehler.empty())   info += "  KEY8 fuer joby3 nicht uebernommen: " + e.keyFehler;
        else if (e.keyKopiert)      info += "  KEY8 fuer joby3 aus joby2 uebernommen.";
        return true;
    }

    // Ein Level randomisieren. Genau dieselben Schritte wie Randomize.cpp.
    bool doLevel(App& a, LevelRow& row, uint64_t seed, std::string& err)
    {
        const std::string base = row.name.substr(0, row.name.find_last_of('.'));
        const std::wstring wbase = PathUtil::widen(base);

        JsonValue zone, info;
        if (!Json::parseFile(PathUtil::join(a.payloadDir(), L"zones_" + wbase + L".json"), zone, err)) return false;
        if (!Json::parseFile(PathUtil::join(a.payloadDir(), L"level_" + wbase + L".json"), info, err)) return false;

        RandoInput in;
        if (!Rando::buildInput(zone, info, in, err)) return false;

        // Das Level wird VOR der Platzierung geoeffnet, nicht erst zum Patchen.
        // Grund ist der gekoppelte Triggerzug: welche Sektoren einen Trigger
        // aufnehmen koennen, steht nur in der Floordata, und die Platzierung
        // muss es wissen, bevor sie waehlt - sonst faellt es erst beim Patchen
        // auf, und dann ist der Lauf schon halb gelaufen.
        Tr5Level lvl;
        const std::wstring target = PathUtil::join(a.dataDir(), PathUtil::widen(row.name));
        if (!lvl.load(target, err)) return false;
        Patch::markTriggerSafeSectors(lvl, in);

        RandoOptions opt;
        opt.shuffleKeyItems = a.optKeyItems;
        opt.shuffleOtherPickups = a.optOtherPickups;
        opt.addEnemies = a.optEnemies;
        opt.enemiesPerLevel = a.optEnemyCount;
        opt.swapEnemies = a.optEnemySwap;
        opt.crossEnemies = (a.optEnemyPool == 1);
        opt.allsanity    = (a.optEnemyPool == 2);
        opt.gesperrteGegner = a.optGegnerAus;
        opt.tr3Dir     = a.cfg.tr3Dir;
        opt.tr3GoldDir = a.cfg.tr3GoldDir;
        opt.randomOutfits = a.optOutfits;
        opt.allWeapons = true;              // Standard, siehe App-Kommentar
        // Item-Abundance: die drei Stufen der Oberflaeche als Stueckzahl.
        // Freie Ablageflaechen und das Item-Limit deckeln sie ohnehin.
        opt.extraPickups = (a.optItemMenge == 1) ? 12 : (a.optItemMenge == 2) ? 30 : 0;

        JsonValue patch;
        RandoReport rep;
        if (!Rando::run(in, opt, seed, patch, rep)) { err = rep.error; return false; }

        // Quell-Level fuer soundAdds: immer das Backup, nie data\ - dort koennen
        // schon randomisierte Level liegen, und aus denen Samples zu holen waere
        // vom Seed nicht mehr nachstellbar.
        static std::wstring soundQuelle;
        soundQuelle = a.haveBackup ? a.backupDir() : a.dataDir();
        auto quelle = [](const std::string& name, void*) -> std::wstring {
            return PathUtil::join(soundQuelle, PathUtil::widen(name) + L".trc");
        };

        PatchReport pr;
        // Die Fremdinstallationen durchreichen: nur damit kann apply() die
        // Gaeste aus Tomb Raider 3 zur Laufzeit holen (#92). Fehlen sie, hat
        // der Randomizer sie gar nicht erst in den Pool genommen.
        if (!Patch::apply(lvl, patch, pr, err, quelle, nullptr,
                          tr3Ordner(a), goldOrdner(a), lagerDir(a))) return false;
        for (const std::string& w : pr.warnings) rep.notes.push_back(w);
        if (!lvl.save(target, err)) return false;

        char buf[200];
        int len = std::snprintf(buf, sizeof(buf), "%d bewegt, %d neu", pr.edited, pr.added);
        if (rep.enemiesAdded > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Gegner", rep.enemiesAdded);
        if (pr.triggersMoved > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Trigger mit", pr.triggersMoved);
        if (pr.weaponsCopied > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Waffenslots", pr.weaponsCopied);
        if (rep.weaponsScattered > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Waffen gestreut", rep.weaponsScattered);
        if (rep.crossTypesUsed > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Cross-Typen", rep.crossTypesUsed);
        if (!pr.outfitFrom.empty())
            std::snprintf(buf + len, sizeof(buf) - len, ", Outfit %s", pr.outfitFrom.c_str());
        row.result = buf;
        return true;
    }

    // Ein Debug-Level bauen: statt Items zu verschieben, wird jeder gueltige
    // Sektor mit einem Marker belegt. Grundlage ist immer das ORIGINAL - liegt
    // ein Backup vor, kommt die Datei von dort, sonst aus data\. Ein bereits
    // randomisiertes Level als Vorlage waere zwar geometrisch gleich, aber die
    // Itemliste im Ergebnis waere nicht die, die der Nutzer erwartet.
    //
    // Geschrieben wird ausschliesslich nach "Debug Level" neben der EXE. Diese
    // Dateien sind nicht spielbar (mehrere tausend Items), und in data\ haetten
    // sie deshalb nichts verloren.
    bool doDebugLevel(App& a, LevelRow& row, std::string& err)
    {
        const std::string base = row.name.substr(0, row.name.find_last_of('.'));
        const std::wstring wbase = PathUtil::widen(base);

        JsonValue zone, info;
        if (!Json::parseFile(PathUtil::join(a.payloadDir(), L"zones_" + wbase + L".json"), zone, err)) return false;
        if (!Json::parseFile(PathUtil::join(a.payloadDir(), L"level_" + wbase + L".json"), info, err)) return false;

        RandoInput in;
        if (!Rando::buildInput(zone, info, in, err)) return false;

        JsonValue patch;
        std::vector<MarkerCount> perType;
        int fillSectors = 0;
        if (!Rando::buildMarkerPatch(in, a.dbgUseFill ? a.dbgFillType : 0, patch, perType, fillSectors))
        {
            err = "Marker konnten nicht erzeugt werden.";
            return false;
        }

        const std::wstring src = PathUtil::join(
            a.haveBackup ? a.backupDir() : a.dataDir(), PathUtil::widen(row.name));

        Tr5Level lvl;
        if (!lvl.load(src, err)) return false;
        lvl.allowMoreThanMaxItems(true);   // hier ausdruecklich gewollt, siehe Tr5Level.h

        PatchReport pr;
        if (!Patch::apply(lvl, patch, pr, err)) return false;

        if (!PathUtil::ensureDir(a.debugDir())) { err = "Ordner \"Debug Level\" liess sich nicht anlegen."; return false; }
        if (!lvl.save(PathUtil::join(a.debugDir(), PathUtil::widen(row.name)), err)) return false;

        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d Marker", pr.added);
        row.result = buf;

        std::snprintf(buf, sizeof(buf), "%s: %d Sektoren gesamt%s",
                      row.name.c_str(), fillSectors,
                      a.dbgUseFill ? "" : " (Fuellebene aus)");
        a.dbgLog.push_back(buf);
        for (const MarkerCount& m : perType)
        {
            std::snprintf(buf, sizeof(buf), "    Typ %d: %d Sektoren in %d Raeumen",
                          m.typeId, m.sectors, m.rooms);
            a.dbgLog.push_back(buf);
            // Ein Typ ohne einen einzigen erlaubten Sektor ist kein Randfall,
            // sondern ein Kurationsfehler - der Lauf wuerde spaeter daran
            // scheitern, und hier faellt es sofort auf.
            if (m.sectors == 0) a.dbgLog.push_back("    ^ KEIN erlaubter Sektor!");
        }
        return true;
    }

    void startDebugMarkers(App& a)
    {
        a.dbgLog.clear();
        a.plannedCount = 0;
        for (LevelRow& r : a.levels)
        {
            r.result.clear();
            if (r.enabled) ++a.plannedCount;
        }
        a.doneCount = a.failCount = 0;
        a.stepIndex = 0;
        a.phase = App::Phase::Debug;
        setStatus(a, "Debug-Level werden gebaut...");
    }

    // Legt data\Backup an, falls es fehlt. Das darf NUR passieren, wenn die
    // Level in data\ nachweislich unveraendert sind - ein Backup aus bereits
    // randomisierten Dateien waere unbemerkt wertlos und das Original
    // unwiederbringlich verloren. Deshalb ist die Pruefsummenpruefung hier
    // keine Bequemlichkeit, sondern die Absicherung.
    bool ensureBackup(App& a, std::string& err)
    {
        if (PathUtil::dirExists(a.backupDir())) return true;

        auto st = a.data.inspect(a.dataDir());
        std::string why;
        if (!DataFolder::isUsableSource(st, why))
        {
            err = "Es gibt noch kein Backup, und die Level in data sind nicht im "
                  "Originalzustand: " + why + " Bitte ueber Steam die Dateien "
                  "pruefen lassen und erneut versuchen.";
            return false;
        }

        int copied = 0;
        if (!a.data.copyLevels(a.dataDir(), a.backupDir(), err, &copied)) return false;
        a.haveBackup = true;
        return true;
    }

    // Startet einen Lauf. Die Arbeit selbst passiert in advanceRun(), damit die
    // Oberflaeche zwischendurch zeichnen kann.
    void startRandomize(App& a)
    {
        a.plannedCount = 0;
        for (LevelRow& r : a.levels)
        {
            r.result.clear();
            if (r.enabled) ++a.plannedCount;
        }
        a.doneCount = a.failCount = 0;
        a.stepIndex = 0;
        a.phase = App::Phase::Copy;
        a.phaseText = "Originale werden zurueckgespielt...";
        setStatus(a, "Lauf gestartet.");
    }

    void advanceRun(App& a)
    {
        switch (a.phase)
        {
        case App::Phase::Copy:
        {
            // Immer aus dem Backup kopieren, nie auf ein schon randomisiertes
            // Level aufsetzen - sonst waere das Ergebnis aus dem Seed nicht
            // mehr nachstellbar, und genau dafuer gibt es den Seed.
            std::string err;
            if (!ensureBackup(a, err)) { setStatus(a, err, true); a.phase = App::Phase::Idle; return; }

            int copied = 0;
            if (!a.data.copyLevels(a.backupDir(), a.dataDir(), err, &copied))
            {
                setStatus(a, "Kopieren fehlgeschlagen: " + err, true);
                a.phase = App::Phase::Idle;
                return;
            }
            // SCRIPT.DAT genau hier: einmal je Lauf, bevor das erste Level
            // angefasst wird. Scheitert es, ist das kein Grund abzubrechen -
            // die Level sind davon unabhaengig.
            {
                std::string sErr, sInfo;
                if (!doScriptDat(a, sErr, sInfo))
                    a.scriptNote = "SCRIPT.DAT: " + sErr;
                else
                    a.scriptNote = sInfo;
            }

            a.phase = App::Phase::Levels;
            a.stepIndex = 0;
            return;
        }

        case App::Phase::Levels:
        {
            // Nicht bearbeitbare Zeilen ueberspringen, aber pro Bild nur einen
            // echten Schritt tun.
            while (a.stepIndex < a.levels.size() && !a.levels[a.stepIndex].enabled)
            {
                a.levels[a.stepIndex].result = "uebersprungen";
                ++a.stepIndex;
            }
            if (a.stepIndex >= a.levels.size()) { a.phase = App::Phase::Finish; return; }

            LevelRow& r = a.levels[a.stepIndex];
            a.phaseText = r.name;

            std::string e;
            if (doLevel(a, r, a.effectiveSeed(), e)) ++a.doneCount;
            else { ++a.failCount; r.result = "FEHLER: " + e; }

            ++a.stepIndex;
            return;
        }

        case App::Phase::Debug:
        {
            while (a.stepIndex < a.levels.size() && !a.levels[a.stepIndex].enabled)
            {
                a.levels[a.stepIndex].result = "uebersprungen";
                ++a.stepIndex;
            }
            if (a.stepIndex >= a.levels.size())
            {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              "%d Debug-Level in \"Debug Level\" abgelegt%s. Nur fuer trview - "
                              "mit so vielen Items startet das Spiel nicht.",
                              a.doneCount, a.failCount ? " - mit Fehlern, siehe Liste" : "");
                setStatus(a, buf, a.failCount > 0);
                a.phaseText.clear();
                a.phase = App::Phase::Idle;
                return;
            }

            LevelRow& r = a.levels[a.stepIndex];
            a.phaseText = r.name;

            std::string e;
            if (doDebugLevel(a, r, e)) ++a.doneCount;
            else { ++a.failCount; r.result = "FEHLER: " + e; a.dbgLog.push_back(r.name + ": FEHLER: " + e); }

            ++a.stepIndex;
            return;
        }

        case App::Phase::Finish:
        {
            a.cfg.seedText = a.seedBuf;
            a.cfg.save();

            char buf[192];
            std::snprintf(buf, sizeof(buf), "%d Level randomisiert%s. Du kannst das Spiel jetzt starten.",
                          a.doneCount, a.failCount ? " - mit Fehlern, siehe Liste" : "");
            setStatus(a, buf, a.failCount > 0);
            a.phaseText.clear();
            a.phase = App::Phase::Idle;
            return;
        }

        default:
            return;
        }
    }

    void doRestore(App& a)
    {
        std::string err;
        if (!PathUtil::dirExists(a.backupDir()))
        {
            setStatus(a, "Es gibt kein Backup - da wurde noch nie randomisiert, "
                         "die Level sind also bereits im Originalzustand.", true);
            return;
        }

        int copied = 0;
        if (!a.data.copyLevels(a.backupDir(), a.dataDir(), err, &copied))
            setStatus(a, "Zuruecksetzen fehlgeschlagen: " + err, true);
        else
        {
            for (LevelRow& r : a.levels) r.result.clear();
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%d Level auf den Originalzustand zurueckgesetzt.", copied);
            setStatus(a, buf);
        }
    }
}

//----------------------------------------------------------------------------
// Debugfenster: Sektoren mit Markern befuellen.
//
// Was hier entsteht, ist kein Level, sondern ein Bild: auf jedem Sektor, auf
// dem etwas liegen darf, liegt ein Marker. Der Fuelltyp zeigt die Flaeche fuer
// gewoehnliche Ausruestung, darueber liegt je Schluesseltyp dessen engerer
// Bereich - beide UEBERLAPPEN sich. Ein Sektor mit nur dem Fuelltyp erlaubt
// also ausschliesslich Ausruestung, ein Sektor mit beidem beides.
//
// Geprueft wird mit derselben Funktion, die auch platziert
// (Rando::sectorAllowedFor). Genau darum geht es: was hier zu sehen ist, ist
// das, was der Randomizer wirklich tut - nicht eine zweite Beschreibung davon.
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// Ist die Fremdinstallation hinter einer Gegnerart nutzbar? (#102)
//
// Geprueft wird nicht der Ordner, sondern die Datei, die WIRKLICH gebraucht
// wird - ein Ordner kann existieren und trotzdem leer sein. CRASH.TR2 traegt
// T-Rex, Raptor und Compy, chunnel.TR2 den Geier; MAIN.SFX liefert in beiden
// Faellen die Klaenge.
//----------------------------------------------------------------------------
static bool dinoQuelleDa(const App& a, const char* quelle)
{
    if (!quelle) return true;                 // steckt in TR5 selbst

    if (std::strcmp(quelle, "tr3") == 0)
        return !tr3Ordner(a).empty()
            && !loeseOrdner(a.cfg.tr3Dir, L"MAIN.SFX").empty();

    // Der Geier braucht BEIDE Installationen: sein Modell steckt in chunnel.TR2
    // von The Lost Artifact, seine Klaenge kommen aber aus der MAIN.SFX von
    // Tomb Raider 3. Die beiden Dateien sind verschieden - Gold bringt eine
    // eigene mit (9,8 MB gegen 11,6 MB), und die abgehoerten Wave-Nummern
    // beziehen sich auf die von TR3.
    if (std::strcmp(quelle, "tr3gold") == 0)
        return !goldOrdner(a).empty()
            && !loeseOrdner(a.cfg.tr3Dir, L"MAIN.SFX").empty();

    return false;
}

//----------------------------------------------------------------------------
// Fenster "Installations-Pfade" (#102).
//
// Die Dinosaurier stammen aus Tomb Raider 3 und The Lost Artifact. Wir
// liefern KEINE Spieldaten mit - wer sie haben will, muss die Spiele
// besitzen. Der Lauf holt Modell, Texturen und Klaenge zur Laufzeit von dort.
//
// Ohne gueltigen Pfad bleibt die jeweilige Art im Fenster "Gegnerarten"
// ausgegraut, statt mitten im Lauf zu scheitern.
//----------------------------------------------------------------------------
static void drawPfadeWindow(App& a, HWND hwnd)
{
    if (!a.pfadeOpen) return;

    ImGui::SetNextWindowSize(ImVec2(620, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Installations-Pfade", &a.pfadeOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Die Dinosaurier stammen aus anderen Spielen. Dieses Werkzeug bringt "
        "keine Spieldaten mit - gib die Ordner an, dann holt der Lauf sich "
        "Modelle, Texturen und Klaenge von dort. Ohne Pfad bleiben die "
        "betroffenen Gegnerarten ausgegraut, sonst laeuft alles wie gewohnt.");
    ImGui::Spacing();

    struct Eintrag { const char* titel; const char* datei; const char* wofuer;
                     std::wstring* pfad; const char* quelle; };
    Eintrag liste[] = {
        { "Tomb Raider 5 (Chronicles)", nullptr, "die Level, die randomisiert werden",
          &a.cfg.sourceDir, nullptr },
        { "Tomb Raider 3",             "CRASH.TR2", "T-Rex, Raptor und Compy",
          &a.cfg.tr3Dir, "tr3" },
        { "Tomb Raider 3: The Lost Artifact", "chunnel.TR2", "der Geier",
          &a.cfg.tr3GoldDir, "tr3gold" },
    };

    for (Eintrag& e : liste)
    {
        ImGui::PushID(e.titel);
        ImGui::SeparatorText(e.titel);
        ImGui::TextDisabled("fuer %s", e.wofuer);

        const bool da = e.quelle ? dinoQuelleDa(a, e.quelle) : a.sourceOk;
        ImGui::TextWrapped("%s", e.pfad->empty() ? "(nicht gewaehlt)"
                                                 : PathUtil::narrow(*e.pfad).c_str());

        if (ImGui::Button("Ordner waehlen..."))
        {
            std::wstring picked;
            if (pickFolder(hwnd, picked))
            {
                *e.pfad = picked;
                a.cfg.save();
                if (!e.quelle) inspectSource(a);
            }
        }
        if (!e.pfad->empty())
        {
            ImGui::SameLine();
            if (ImGui::Button("Leeren")) { e.pfad->clear(); a.cfg.save(); }
        }
        ImGui::SameLine();
        if (da) ImGui::TextDisabled("in Ordnung");
        else if (e.datei) ImGui::TextDisabled("%s nicht gefunden", e.datei);
        else ImGui::TextDisabled("noch nicht geprueft");

        ImGui::PopID();
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Gesucht wird jeweils der data-Ordner der Installation.");
    ImGui::End();
}

//----------------------------------------------------------------------------
// Fenster "Gegnerarten" (#95): welche Arten darf der Pool ueberhaupt ziehen?
//
// Gut fuer den Spieler, der seine Lieblingsgegner haeufiger sehen will, und
// gut zum Pruefen einer einzelnen Art - alles abwaehlen bis auf eine, und der
// Lauf setzt nur noch die.
//
// Gehalten und gespeichert wird die ABWAHL, nicht die Auswahl. Eine Art, die
// spaeter dazukommt, ist dadurch automatisch an und faellt nicht stumm durch
// eine alte Einstellungsdatei.
//
// Der Haken beschneidet AUSSCHLIESSLICH die Wahl aus dem Pool. Was eine
// gewaehlte Art technisch nachzieht - ihren Anim-Basistyp, Pflichtbegleiter
// wie den Geschosskern der Hydra, die MESHSWAP-Haelften - sieht ihn nie.
// Sonst stuende ein Gegner ohne seinen Animationssatz im Level.
//----------------------------------------------------------------------------
static void drawGegnerartenWindow(App& a)
{
    if (!a.artenOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Gegnerarten", &a.artenOpen))
    {
        ImGui::End();
        return;
    }

    const std::vector<Gegnerart> arten = Rando::gegnerarten();

    ImGui::TextWrapped(
        "Welche Arten darf der Zufall ziehen? Der Haken wirkt auf den Tausch "
        "und auf zusaetzliche Gegner - Gegner, die im Level ohnehin stehen und "
        "nicht getauscht werden, bleiben davon unberuehrt.");
    ImGui::Spacing();
    // Manche Arten teilen sich unter der Haube einen Animationssatz oder
    // brauchen ein unsichtbares Hilfsobjekt (der Geschosskern der Hydra etwa).
    // Das ist Technik, keine Spielerentscheidung - der Lauf holt es selbst, egal
    // wie die Haken stehen. Hier steht nur, DASS es passiert, damit sich niemand
    // fragt, warum ein abgewaehlter Wachmann trotzdem in der Datei auftaucht.
    ImGui::TextDisabled("Was eine Art technisch braucht, laedt der Randomizer von selbst.");
    ImGui::Spacing();

    if (ImGui::Button("Alle an"))  a.optGegnerAus.clear();
    ImGui::SameLine();
    if (ImGui::Button("Alle aus"))
        for (const Gegnerart& g : arten) a.optGegnerAus.insert(g.typeId);
    ImGui::SameLine();
    ImGui::TextDisabled("%d von %d an",
                        static_cast<int>(arten.size() - a.optGegnerAus.size()),
                        static_cast<int>(arten.size()));

    ImGui::Separator();
    ImGui::BeginChild("##artenliste", ImVec2(0, -78), true);
    for (const Gegnerart& g : arten)
    {
        // Arten aus einem anderen Spiel bleiben ausgegraut, solange der Pfad
        // dorthin fehlt (#102). Der Haken bleibt dabei erhalten - traegt der
        // Nutzer den Pfad spaeter nach, ist seine Auswahl noch da.
        const bool da = dinoQuelleDa(a, g.quelle);
        ImGui::BeginDisabled(!da);

        bool an = !a.optGegnerAus.count(g.typeId) && da;
        char label[96];
        std::snprintf(label, sizeof(label), "%s##art%d", g.name, g.typeId);
        if (ImGui::Checkbox(label, &an))
        {
            if (an) a.optGegnerAus.erase(g.typeId);
            else    a.optGegnerAus.insert(g.typeId);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", g.typeId);
        ImGui::EndDisabled();

        if (!da)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("- braucht %s",
                std::strcmp(g.quelle, "tr3gold") == 0 ? "The Lost Artifact" : "Tomb Raider 3");
        }
    }
    ImGui::EndChild();

    // Alles abgewaehlt heisst nicht "keine Gegner": der Cross-Pool faellt dann
    // leer aus, und jedes Level wuerfelt nur noch aus seinen einheimischen
    // Arten. Das ist ein gueltiger Zustand, aber keiner, den man versehentlich
    // haben will - deshalb steht es hier und wird nicht stillschweigend getan.
    if (a.optGegnerAus.size() >= arten.size())
        ImGui::TextWrapped("Alle Arten abgewaehlt: jedes Level bleibt bei seinen "
                           "eigenen Gegnern, fremde kommen keine dazu.");
    else if (!a.optGegnerAus.empty())
        ImGui::TextWrapped("Waehlst du sehr viele ab, stehen manchen Leveln kaum "
                           "noch fremde Arten zur Verfuegung - sie bleiben dann "
                           "naeher am Original.");

    ImGui::End();
}

static void drawDebugWindow(App& a)
{
    if (!a.dbgOpen) return;

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug: Sektoren-Marker", &a.dbgOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Baut zu jedem oben angehakten Level eine Kopie, in der jeder erlaubte "
        "Sektor einen Marker traegt. Zum Ansehen in trview - mit mehreren tausend "
        "Items startet das Spiel diese Dateien NICHT. Deine Installation bleibt "
        "unberuehrt, geschrieben wird nur nach \"Debug Level\" neben dem Programm.");

    ImGui::Separator();
    ImGui::Checkbox("Fuellebene: jeden gueltigen Sektor markieren", &a.dbgUseFill);
    ImGui::BeginDisabled(!a.dbgUseFill);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt("Typ der Fuellebene", &a.dbgFillType);
    if (a.dbgFillType < 0) a.dbgFillType = 0;
    ImGui::EndDisabled();
    ImGui::TextDisabled("223 = Pickup Item4. Im Spiel selten, faellt in der Itemliste sofort auf.");

    const bool busy = (a.phase != App::Phase::Idle);
    ImGui::Separator();
    ImGui::BeginDisabled(!a.sourceOk || busy);
    if (ImGui::Button("Debug-Level bauen", ImVec2(200, 30))) startDebugMarkers(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!PathUtil::dirExists(a.debugDir()));
    if (ImGui::Button("Ordner oeffnen", ImVec2(150, 30)))
        ShellExecuteW(nullptr, L"open", a.debugDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::EndDisabled();

    if (!a.sourceOk)
        ImGui::TextDisabled("(erst oben eine Installation waehlen)");

    ImGui::Separator();
    ImGui::TextUnformatted("Ergebnis je Typ:");
    if (ImGui::BeginChild("dbglog", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        for (const std::string& l : a.dbgLog)
        {
            if (l.find("KEIN erlaubter") != std::string::npos)
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", l.c_str());
            else if (l.find("FEHLER") != std::string::npos)
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", l.c_str());
            else
                ImGui::TextUnformatted(l.c_str());
        }
        // Waehrend des Laufs mitscrollen, sonst sieht man immer nur den Anfang.
        if (a.phase == App::Phase::Debug) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
}

//----------------------------------------------------------------------------
// Oberflaeche
//----------------------------------------------------------------------------
static void drawUi(App& a, HWND hwnd)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("TR5 Randomizer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextUnformatted("TR5 Randomizer");
    ImGui::SameLine();
    ImGui::TextDisabled("Build %s", RANDO_BUILD_STRING);

    // Rechts oben, weil es kein Bedienschritt ist: die vier nummerierten
    // Abschnitte darunter sind der Weg durchs Programm, das hier steht daneben.
    {
        const float w = 110.0f;
        // GetContentRegionMax ist im Docking-Zweig (imconfig mit
        // IMGUI_DISABLE_OBSOLETE_FUNCTIONS) nicht mehr da; Cursor-X am
        // Zeilenanfang plus verfuegbare Breite ergibt denselben rechten Rand.
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - w);
        if (ImGui::Button("Debug", ImVec2(w, 0))) ImGui::OpenPopup("##debugmenu");
        if (ImGui::BeginPopup("##debugmenu"))
        {
            if (ImGui::MenuItem("Sektoren-Marker...")) a.dbgOpen = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Ordner \"Debug Level\" oeffnen",
                                nullptr, false, PathUtil::dirExists(a.debugDir())))
                ShellExecuteW(nullptr, L"open", a.debugDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::EndPopup();
        }
    }
    ImGui::Separator();

    // --- 1. Spielordner ---
    ImGui::SeparatorText("1. Deine Installation");
    ImGui::TextWrapped("Waehle den Ordner deiner Tomb Raider Chronicles-Installation. "
                       "Die Level werden direkt dort randomisiert; die Originale sichert "
                       "das Programm vorher nach data\\Backup.");

    std::string shown = a.cfg.sourceDir.empty() ? std::string("(nicht gewaehlt)")
                                                : PathUtil::narrow(a.cfg.sourceDir);
    ImGui::TextWrapped("%s", shown.c_str());

    if (ImGui::Button("Ordner waehlen..."))
    {
        std::wstring picked;
        if (pickFolder(hwnd, picked))
        {
            a.cfg.sourceDir = picked;
            a.cfg.save();
            inspectSource(a);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Erneut pruefen")) inspectSource(a);
    ImGui::SameLine();
    if (ImGui::Button("Installations-Pfade...")) a.pfadeOpen = true;
    // Die Schwebehilfe der Optionsliste steht hier noch nicht zur Verfuegung -
    // sie wird erst weiter unten angelegt. Der eigene Tooltip tut es auch.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Optional: Ordner von Tomb Raider 3 und The Lost Artifact.\n"
                          "Nur damit lassen sich die Dinosaurier verwenden -\n"
                          "wir liefern keine Spieldaten mit.");
    {
        const int fehlt = (dinoQuelleDa(a, "tr3") ? 0 : 1) + (dinoQuelleDa(a, "tr3gold") ? 0 : 1);
        ImGui::SameLine();
        if (fehlt == 0) ImGui::TextDisabled("TR3 + LA gefunden");
        else            ImGui::TextDisabled("%d ohne Pfad", fehlt);
    }

    // --- 2. Seed ---
    ImGui::SeparatorText("2. Seed");
    ImGui::TextWrapped("Derselbe Seed erzeugt immer dasselbe Spiel. Jede Eingabe ist gueltig - "
                       "auch ein Wort.");
    ImGui::SetNextItemWidth(280);
    ImGui::InputText("##seed", a.seedBuf, sizeof(a.seedBuf));
    ImGui::SameLine();
    if (ImGui::Button("Wuerfeln"))
    {
        const std::string s = SeedText::format(SeedText::random());
        std::snprintf(a.seedBuf, sizeof(a.seedBuf), "%s", s.c_str());
    }
    ImGui::TextDisabled("wirksam: 0x%016llX", (unsigned long long)a.effectiveSeed());

    // --- 3. Was soll gewuerfelt werden - thematische Bereiche ------------

    // Die Erklaerungen haengen als schwebende Box an der Checkbox: kurz mit
    // der Maus darueber verweilen blendet sie ein, wegbewegen blendet sie
    // aus (Nutzerwunsch - die Liste bleibt kompakt, die Texte gehen nicht
    // verloren). AllowWhenDisabled, damit auch der ausgegraute
    // Cross-Gegnermix seine Erklaerung zeigt.
    auto hoverHilfe = [](const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // --- Items: eigener Bereich (Nutzerwunsch) ---------------------------
    ImGui::SeparatorText("3. Items");

    ImGui::Checkbox("Ausruestung (Waffen, Munition, Medipacks)", &a.optOtherPickups);
    // Item-Abundance (#36): zusaetzliche Verbrauchsgueter streuen. Weniger als
    // Vanilla gibt es bewusst nicht - Items entfernen wuerde alle folgenden
    // Indizes verschieben und jeden "Object N"-Verweis der Trigger brechen.
    ImGui::SameLine();
    ImGui::RadioButton("Vanilla Anzahl", &a.optItemMenge, 0);
    hoverHilfe("Das Level behaelt genau so viel Ausruestung, wie Core vorgesehen "
               "hat - nur an anderen Stellen.");
    ImGui::SameLine();
    ImGui::RadioButton("Mehr Items", &a.optItemMenge, 1);
    hoverHilfe("Streut bis zu zwoelf zusaetzliche Verbrauchsgueter pro Level. "
               "Gelegt wird nur, was das Level ohnehin kennt - Munition, "
               "Medipacks, Fackeln -, gewichtet nach Haeufigkeit: Andy2 (Kind-Lara, "
               "keine Waffen) bekommt also Medipacks, ein Waffenlevel Munition. "
               "Schluessel, Puzzleteile und Secrets bleiben aussen vor.");
    ImGui::SameLine();
    ImGui::RadioButton("Viel mehr Items", &a.optItemMenge, 2);
    hoverHilfe("Wie \"Mehr Items\", aber bis zu dreissig pro Level. Die Grenze "
               "ist erreicht, wenn die freien Ablageflaechen ausgehen oder das "
               "Item-Limit der Engine (512) naeher rueckt - beides deckelt die "
               "Zahl von selbst.");

    ImGui::Checkbox("Schluessel & Puzzles", &a.optKeyItems);

    // Kein Haken mehr: volles Waffenset ist Standard (Nutzerentscheidung).
    ImGui::TextDisabled("Alle Waffen ueberall sind Standard und immer dabei.");
    hoverHilfe("Jedes Level bekommt bei jedem Lauf das volle Waffenset: Pistolen, "
               "Uzi, Schrotflinte, Desert Eagle (ersetzt den Revolver, in den "
               "joby-Leveln auch im Namen) und HK samt Munition. Ein Level behaelt "
               "seine eigene funktionierende Waffe; joby4 bleibt vanilla, und "
               "rich2 bekommt keine Pistolen (sein Pistolenslot traegt "
               "Sequenzmaterial von Core). Der Seed streut zusaetzlich einige der "
               "neuen Waffen samt Munition als Pickups ins Level. Die Modelle "
               "kommen zur Laufzeit aus den eigenen Leveldateien des Spielers.");

    if (!a.optOtherPickups && !a.optKeyItems && !a.optEnemies && !a.optEnemySwap && !a.optOutfits)
        ImGui::TextColored(ImVec4(1, .5f, .3f, 1),
                           "Keine Wuerfel-Option gewaehlt - die Level bekommen nur das Standard-Waffenset.");

    // --- Outfits: eigener Bereich (Nutzerwunsch) -------------------------
    ImGui::SeparatorText("Outfits");

    ImGui::Checkbox("Zufaellige Outfits", &a.optOutfits);
    hoverHilfe("Jeder Abschnitt bekommt zufaellig Lara_Andrea, Lara_Joby oder "
               "Lara_Rich (Catsuit). Uebertragen werden nur Haut und Kopf (Slots "
               "8/9/10) samt Outfit-Byte - nie Slot 0, der traegt die Animationen. "
               "joby4 behaelt den Taucheranzug; die andy-Level bekommen immer eine "
               "erwachsene Lara (Kind-Lara kann keine Waffen tragen).");

    // Platzhalter: klassische Outfits als zusaetzliche Poolauswahl.
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("TR1 Outfit", &a.phOutfitTr1);
    hoverHilfe("Kommt noch: das klassische TR1-Outfit als zusaetzliche Wahl im "
               "Outfit-Pool. Braucht ein passendes TR5-Modell - Spieldaten "
               "anderer Spiele liefern wir nie mit, das Modell muss also "
               "eigens gebaut oder vom Spieler beigesteuert werden.");
    ImGui::SameLine();
    ImGui::Checkbox("TR2 Outfits", &a.phOutfitTr2);
    hoverHilfe("Kommt noch: die TR2-Outfits (u.a. Bomberjacke, Wetsuit) als "
               "zusaetzliche Poolauswahl - gleiche Voraussetzung wie TR1.");
    ImGui::SameLine();
    ImGui::Checkbox("TR3 Outfits", &a.phOutfitTr3);
    hoverHilfe("Kommt noch: die TR3-Outfits (u.a. Dschungel, Antarktis) als "
               "zusaetzliche Poolauswahl - gleiche Voraussetzung wie TR1.");
    ImGui::EndDisabled();

    // --- Gegner: eigener Bereich (Nutzerwunsch) --------------------------
    ImGui::SeparatorText("Gegner");

    ImGui::Checkbox("Zusaetzliche Gegner ins Level setzen", &a.optEnemies);
    hoverHilfe("Jeder bekommt einen eigenen Trigger, etwa drei Sektoren vor sich "
               "auf Laras Weg - nie auf seinem eigenen Feld, sonst spawnt er in ihr. "
               "Welche Gegnertypen ein Level vertraegt, ist handkuriert; ist die "
               "Liste leer, bleibt der Haken folgenlos.");
    ImGui::Indent();
    ImGui::BeginDisabled(!a.optEnemies);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt("pro Level", &a.optEnemyCount, 1, 20);
    ImGui::EndDisabled();
    ImGui::Unindent();

    // Frueher "Vanilla-Gegnermix" - der Name klang wie die Poolstufe "Nur
    // Levelpool" und stiftete Verwirrung (Nutzerfrage). Der Haken sagt, OB
    // vorhandene Gegner die Rollen tauschen; die Poolwahl darunter sagt,
    // WORAUS Tausch und Zusatzgegner wuerfeln.
    ImGui::Checkbox("Vorhandene Gegner tauschen", &a.optEnemySwap);
    hoverHilfe("Die Gegner, die im Level ohnehin stehen, tauschen die Rollen. "
               "Nur die Art wechselt, der Item-Index bleibt - jeder Gegner wacht "
               "also weiter dort auf, wo sein Trigger es vorsieht. Anzahl und "
               "Verteilung bleiben gleich, gescriptete Gegner bleiben unberuehrt. "
               "Woraus die neuen Rollen kommen, entscheidet der Gegnerpool unten.");

    // Woraus wuerfeln Tausch und Zusatzgegner? Drei Stufen. Ausgegraut,
    // solange keine der beiden Aktionen aktiv ist - ohne sie hat die Wahl
    // keine Wirkung.
    ImGui::BeginDisabled(!a.optEnemies && !a.optEnemySwap);
    ImGui::TextUnformatted("Gegnerpool:");
    ImGui::SameLine();
    ImGui::RadioButton("Nur Levelpool", &a.optEnemyPool, 0);
    hoverHilfe("Getauscht und gesetzt wird nur, was das Level selbst kennt "
               "(die handkuratierte Liste je Level).");
    ImGui::SameLine();
    ImGui::RadioButton("Cross-Mix", &a.optEnemyPool, 1);
    hoverHilfe("Bis zu DREI fremde Typen aus anderen Leveln kommen in den Pool - "
               "SWAT, Guards, Mafia, Larson, Pierre, Loewe, Gladiator, Statue, "
               "Maze Monster, Imp und mehr. Beim Tausch geht etwa jede vierte "
               "Rolle an einen davon, das Level bleibt erkennbar. Modelle und "
               "Sounds wandern zur Laufzeit aus den eigenen Leveldateien mit.");
    ImGui::SameLine();
    ImGui::RadioButton("Allsanity", &a.optEnemyPool, 2);
    hoverHilfe("Der KOMPLETTE Gegnerpool steht jedem Level zur Verfuegung, und "
               "beim Tausch wuerfelt jede Rolle frei aus allem - maximale "
               "Auswahl. Uebernommen werden trotzdem nur die Typen, die der "
               "Seed wirklich einsetzt; der Texturseiten-Haushalt bleibt "
               "gemessen deutlich unter der 256er-Grenze. Gescriptete Gegner "
               "bleiben in jedem Modus unberuehrt.");

    // Feinauswahl in einem eigenen Fenster: die Liste ist zu lang fuer die
    // Hauptseite und der Nutzer fasst sie selten an.
    if (ImGui::Button("Gegnerarten...")) a.artenOpen = true;
    hoverHilfe("Welche Gegnerarten der Zufall ueberhaupt ziehen darf. Gut, um "
               "Lieblingsgegner haeufiger zu sehen - oder um eine einzelne Art "
               "zu pruefen: alles abwaehlen bis auf eine.");
    if (!a.optGegnerAus.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%d abgewaehlt", static_cast<int>(a.optGegnerAus.size()));
    }
    ImGui::EndDisabled();

    // --- Platzhalter-Bereiche (Nutzerwunsch, alle noch ohne Wirkung) -----
    //
    // Sichtbar, damit die Oberflaeche ihre endgueltige Form zeigt; ausgegraut,
    // bis die jeweilige Logik existiert. Erklaerungen als Schwebehilfe.
    ImGui::SeparatorText("Texturen (in Vorbereitung)");
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("RGB Vanilla Texturen", &a.phTexRgb);
    hoverHilfe("Kommt noch: Farbwuerfel ueber die Vanilla-Texturseiten - der "
               "Seed verschiebt die Farbtoene, rein kosmetisch.");
    ImGui::SameLine();
    ImGui::Checkbox("Leveluebergreifend mixen", &a.phTexMix);
    hoverHilfe("Kommt noch: Texturseiten zwischen den Leveln tauschen - Rom in "
               "Russland, U-Boot in Irland.");
    ImGui::SameLine();
    ImGui::Checkbox("Remaster-Texturen patchen", &a.phTexRemaster);
    hoverHilfe("Kommt noch: Remaster-Texturen (sofern der Spieler sie hat) in "
               "die klassischen Level patchen.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Raeume (in Vorbereitung)");
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Kulissen einbinden", &a.phRoomKulissen);
    hoverHilfe("Kommt noch: reine Kulissenbereiche ins Spiel einbinden, damit "
               "dort Items und Gegner liegen koennen.");
    ImGui::SameLine();
    ImGui::Checkbox("Nachbar-Raeume begehbar machen", &a.phRoomNachbar);
    hoverHilfe("Kommt noch: angrenzende, vanilla gesperrte Raeume oeffnen - "
               "mehr Flaeche fuer die Platzierung.");
    ImGui::SameLine();
    ImGui::Checkbox("joby4 NoWater", &a.phJoby4NoWater);
    hoverHilfe("Kommt noch: Deepsea Dive ohne Wasser - damit waere joby4 "
               "endlich voll randomisierbar statt vanilla.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Level (in Vorbereitung)");
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Levelabschnitte switchen", &a.phLevelAbschnitte);
    hoverHilfe("Kommt noch: die vier Abschnitte (Rom, Russland, Irland, VCI) "
               "in zufaelliger Reihenfolge spielen - SCRIPT.DAT-Arbeit.");
    ImGui::SameLine();
    ImGui::Checkbox("Level-Reihenfolge switchen", &a.phLevelReihenfolge);
    hoverHilfe("Kommt noch: die 13 Level komplett durchwuerfeln. Achtung "
               "Levelketten: Ausruestung und Story-Uebergaben haengen an der "
               "Reihenfolge (z.B. die Brechstange von Andrea2 nach Andrea3).");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Global (in Vorbereitung)");
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Level spiegeln", &a.phSpiegeln);
    hoverHilfe("Kommt noch: Level gespiegelt spielen. Die Engine kennt dafuer "
               "schon ein Flag (GF_MIRROR in SCRIPT.DAT) - wie gut Cores "
               "Spiegelmodus ausserhalb seines einen Einsatzes traegt, muss "
               "der Test zeigen.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Musik (in Vorbereitung)");
    ImGui::BeginDisabled(true);
    ImGui::Checkbox("Hintergrundmusik switchen", &a.phMusikSwitch);
    hoverHilfe("Kommt noch: der Audiotrack je Level wird gewuerfelt - er steht "
               "als Byte im CMD_LEVEL-Eintrag der SCRIPT.DAT.");
    ImGui::SameLine();
    ImGui::Checkbox("Trigger-Sound/Musik-IDs switchen", &a.phMusikTrigger);
    hoverHilfe("Kommt noch: Musik- und Sound-Trigger in der Floordata bekommen "
               "zufaellige IDs - Jingles und Stimmungen wechseln die Plaetze.");
    ImGui::EndDisabled();

    // --- Secrets (#53) - Platzhalter ohne Wirkung ------------------------
    //
    // Die Auswahl ist schon sichtbar, damit die Oberflaeche ihre endgueltige
    // Form zeigt; ausgegraut, bis die Sektorlisten und die Platzierungslogik
    // existieren. Die Erklaerungen haengen wie oben als Schwebehilfe dran.
    ImGui::SeparatorText("Secrets (in Vorbereitung)");
    ImGui::BeginDisabled(true);

    ImGui::RadioButton("Aus", &a.optSecrets, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Im normalen Item-Pool", &a.optSecrets, 1);
    hoverHilfe("Die Secrets wandern als gewoehnliche Pickups in den Wuerfelpool "
               "und koennen ueberall dort liegen, wo auch Ausruestung liegen darf.");

    ImGui::RadioButton("Glitched Secrets", &a.optSecrets, 2);
    hoverHilfe("Handverlesene Liste von Raeumen und Sektoren, die nur mit "
               "Glitches erreichbar sind. Die Liste wird von Hand gepflegt "
               "und kommt noch.");

    ImGui::RadioButton("Custom-Nutzerliste", &a.optSecrets, 3);
    hoverHilfe("Von Nutzern eingereichte, handverlesene Sektorlisten. Jede "
               "Liste traegt den Namen ihres Autors samt Kennzeichnung "
               "Glitched oder Non-Glitched, z.B. Lucifer[Glitched]. Die "
               "Dateien kommen in den Ordner SecretLists neben dem Programm; "
               "das Format wird mit der ersten echten Liste festgelegt.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("##secretliste", "(keine Listen gefunden)"))
        ImGui::EndCombo();

    ImGui::EndDisabled();

    // --- 4. Level ---
    ImGui::SeparatorText("4. Level");
    if (a.skippedNonPlayable > 0)
        ImGui::TextDisabled("%d nicht spielbare Dateien (Title, del, richcut2) werden geprueft, aber nicht angetastet.",
                            a.skippedNonPlayable);
    // Feste Hoehe nach Levelanzahl statt Scrollleiste (Nutzerwunsch): ohne
    // ScrollY und ohne Hoehenvorgabe waechst die Tabelle genau auf ihre
    // Zeilen - bei 13 spielbaren Leveln ist das ueberschaubar.
    if (ImGui::BeginTable("levels", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Zustand", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Ergebnis");
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < a.levels.size(); ++i)
        {
            LevelRow& r = a.levels[i];
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            const bool selectable = (r.state == LevelState::Vanilla);
            ImGui::BeginDisabled(!selectable);
            ImGui::Checkbox(r.name.c_str(), &r.enabled);
            ImGui::EndDisabled();
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            switch (r.state)
            {
            case LevelState::Vanilla:  ImGui::TextUnformatted("original"); break;
            case LevelState::Modified: ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "veraendert"); break;
            case LevelState::Missing:  ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "fehlt"); break;
            default:                   ImGui::TextUnformatted("unbekannt"); break;
            }

            ImGui::TableSetColumnIndex(2);
            if (!r.result.empty())
            {
                if (r.result.rfind("FEHLER", 0) == 0) ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "%s", r.result.c_str());
                else                                   ImGui::TextUnformatted(r.result.c_str());
            }
        }
        ImGui::EndTable();
    }

    // --- Fortschritt ---
    // Steht zwischen Levelliste und den Knoepfen, weil dort hingesehen wird,
    // sobald man auf "Randomisieren" gedrueckt hat.
    const bool busy = (a.phase != App::Phase::Idle);
    if (busy)
    {
        char overlay[96];
        if (a.phase == App::Phase::Copy)
            std::snprintf(overlay, sizeof(overlay), "Originale werden zurueckgespielt...");
        else
            std::snprintf(overlay, sizeof(overlay), "%s  (%d von %d)",
                          a.phaseText.c_str(), a.doneCount + a.failCount, a.plannedCount);
        ImGui::ProgressBar(a.progress(), ImVec2(-FLT_MIN, 22), overlay);
    }
    else
    {
        // Platz freihalten, damit die Knoepfe nicht springen, sobald ein Lauf
        // startet - ein huepfendes Fenster wirkt kaputt.
        ImGui::Dummy(ImVec2(0, 22));
    }

    // --- 5. Los ---
    ImGui::SeparatorText("5. Los geht's");
    ImGui::BeginDisabled(!a.sourceOk || busy);
    if (ImGui::Button("Randomisieren", ImVec2(160, 32))) startRandomize(a);
    ImGui::SameLine();
    if (ImGui::Button("Auf Original zuruecksetzen", ImVec2(220, 32))) doRestore(a);
    ImGui::EndDisabled();

    if (!a.sourceOk && !busy)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(erst eine unveraenderte Installation waehlen)");
    }

    ImGui::Separator();
    if (!a.status.empty())
    {
        if (a.statusIsError) ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "%s", a.status.c_str());
        else                 ImGui::TextWrapped("%s", a.status.c_str());
    }

    ImGui::End();

    // Nach dem Hauptfenster, damit es darueber liegt - das Hauptfenster fuellt
    // den ganzen Bildschirm und traegt NoBringToFrontOnFocus.
    drawDebugWindow(a);
    drawGegnerartenWindow(a);
    drawPfadeWindow(a, hwnd);
}

//----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr, nullptr, nullptr, nullptr,
                    L"TR5RandomizerWnd", nullptr };
    RegisterClassExW(&wc);

    // Fenstergroesse aus der Einstellungsdatei; beim ersten Start eine
    // grosszuegige Vorgabe (Nutzerwunsch - das alte 820x780 war zu klein).
    // Untergrenzen fangen eine von Hand kaputteditierte Datei ab.
    Config vorab;
    vorab.load();
    const int fensterW = (vorab.winW >= 500) ? vorab.winW : 1000;
    const int fensterH = (vorab.winH >= 400) ? vorab.winH : 920;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TR5 Randomizer", WS_OVERLAPPEDWINDOW,
                              100, 100, fensterW, fensterH, nullptr, nullptr, hInst, nullptr);

    if (!createDevice(hwnd))
    {
        MessageBoxW(nullptr, L"Direct3D 11 laesst sich auf diesem Rechner nicht starten.",
                    L"TR5 Randomizer", MB_ICONSTOP);
        DestroyWindow(hwnd);
        UnregisterClassW(wc.lpszClassName, hInst);
        CoUninitialize();
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // keine Fensterlage-Datei hinterlassen

    // Unterfenster (z.B. der Sektoren-Marker) sollen sich aus dem Hauptfenster
    // herausziehen lassen, auch auf einen zweiten Bildschirm (Nutzerwunsch).
    // Dafuer der Docking-Zweig von Dear ImGui mit Viewports: sobald ein
    // Fenster den Rand verlaesst, bekommt es ein eigenes Betriebssystem-
    // Fenster. WindowRounding 0 und volle Deckkraft, weil freistehende
    // Fenster keine runden, durchscheinenden Ecken haben koennen.
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    App app;
    app.cfg.load();
    std::snprintf(app.seedBuf, sizeof(app.seedBuf), "%s",
                  app.cfg.seedText.empty() ? SeedText::format(SeedText::random()).c_str()
                                           : app.cfg.seedText.c_str());

    // Gespeicherte Auswahl wiederherstellen - der Nutzer soll seine Haken
    // beim naechsten Start wiederfinden.
    app.optOtherPickups = app.cfg.optOtherPickups;
    app.optKeyItems     = app.cfg.optKeyItems;
    app.optEnemies      = app.cfg.optEnemies;
    app.optEnemyCount   = app.cfg.enemyCount;
    app.optEnemySwap    = app.cfg.optEnemySwap;
    app.optEnemyPool    = app.cfg.enemyPool;
    app.optGegnerAus    = app.cfg.gegnerAus;
    app.optOutfits      = app.cfg.optOutfits;
    app.optItemMenge    = app.cfg.itemMenge;
    app.optSecrets      = app.cfg.secrets;

    // Alles Merkenswerte in die Einstellungsdatei schreiben: Fenstergroesse,
    // Haken, Seed. Laeuft beim Ziehen des Fensters, bei jeder geaenderten
    // Auswahl und beim Beenden - die Datei ist winzig, das darf oft passieren.
    auto merkeEinstellungen = [&app](HWND fenster)
    {
        RECT r;
        if (GetWindowRect(fenster, &r) && r.right > r.left && r.bottom > r.top)
        {
            app.cfg.winW = r.right - r.left;
            app.cfg.winH = r.bottom - r.top;
        }
        app.cfg.optOtherPickups = app.optOtherPickups;
        app.cfg.optKeyItems     = app.optKeyItems;
        app.cfg.optEnemies      = app.optEnemies;
        app.cfg.enemyCount      = app.optEnemyCount;
        app.cfg.optEnemySwap    = app.optEnemySwap;
        app.cfg.enemyPool       = app.optEnemyPool;
        app.cfg.gegnerAus       = app.optGegnerAus;
        app.cfg.optOutfits      = app.optOutfits;
        app.cfg.itemMenge       = app.optItemMenge;
        app.cfg.secrets         = app.optSecrets;
        app.cfg.seedText        = app.seedBuf;
        app.cfg.save();
    };

    std::string err;
    const std::wstring csv = PathUtil::join(app.payloadDir(), L"VanillaLevelChecksums.csv");
    app.payloadOk = app.data.loadChecksums(csv, err);
    if (!app.payloadOk)
    {
        // Haeufigster Fall: die EXE wurde aus dem Build-Ordner gestartet statt
        // aus dem ausgelieferten Paket. Deshalb den erwarteten Pfad nennen -
        // "Datei nicht gefunden" allein hilft niemandem weiter.
        app.payloadError = "Der Ordner \"data\" neben dem Programm fehlt oder ist unvollstaendig ("
                         + err + "). Erwartet wird: " + PathUtil::narrow(csv);
    }
    inspectSource(app);

    bool running = true;
    while (running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        drawUi(app, hwnd);

        // Ein Arbeitsschritt pro Bild, NACH dem Zeichnen. So sieht der Nutzer
        // erst den neuen Balkenstand und dann passiert der naechste Schritt -
        // andersherum bliebe der letzte Schritt unsichtbar.
        advanceRun(app);

        // Einstellungen festhalten: nach dem Ziehen des Fensters und immer,
        // wenn sich eine Auswahl gegenueber der Datei geaendert hat.
        if (g_fensterGezogen)
        {
            g_fensterGezogen = false;
            merkeEinstellungen(hwnd);
        }
        else if (app.cfg.optOtherPickups != app.optOtherPickups ||
                 app.cfg.optKeyItems     != app.optKeyItems ||
                 app.cfg.optEnemies      != app.optEnemies ||
                 app.cfg.enemyCount      != app.optEnemyCount ||
                 app.cfg.optEnemySwap    != app.optEnemySwap ||
                 app.cfg.enemyPool       != app.optEnemyPool ||
                 app.cfg.gegnerAus       != app.optGegnerAus ||
                 app.cfg.optOutfits      != app.optOutfits ||
                 app.cfg.itemMenge       != app.optItemMenge ||
                 app.cfg.secrets         != app.optSecrets)
        {
            merkeEinstellungen(hwnd);
        }

        ImGui::Render();
        const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Herausgezogene Fenster leben in eigenen Betriebssystem-Fenstern mit
        // eigener Swapchain - die zeichnet und praesentiert dieser Aufruf.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_swap->Present(1, 0);
    }

    merkeEinstellungen(hwnd);   // letzter Stand, auch Groesse und Seed

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
