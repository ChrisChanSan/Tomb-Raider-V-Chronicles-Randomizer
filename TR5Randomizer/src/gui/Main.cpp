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
#include "core/BalanceDatei.h"
#include "core/BuildId.h"
#include "core/Config.h"
#include "core/DataFolder.h"
#include "core/Json.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/ScriptDat.h"
#include "core/SecretListe.h"
#include "core/Tr5Level.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

// stb_image (Public Domain, Sean Barrett) - nur fuer das eingebettete
// Hintergrundbild. Mitgeliefert unter libs/stb, damit das Quellpaket ohne
// weitere Beschaffung uebersetzbar bleibt.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "libs/stb/stb_image.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <set>
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

    // Hintergrundbild: das Banner des Nutzers, als JPEG in der EXE
    // (TR5Randomizer.rc, IDR_HINTERGRUND). Einmal beim Start entpackt, dann
    // je Bild in Fenstergroesse gezeichnet.
    ID3D11ShaderResourceView* g_hintergrund = nullptr;
    int                       g_hgBreite = 0, g_hgHoehe = 0;

    void ladeHintergrund()
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(201), reinterpret_cast<LPCWSTR>(RT_RCDATA));
        if (!res) return;

        const DWORD groesse = SizeofResource(nullptr, res);
        HGLOBAL geladen = LoadResource(nullptr, res);
        if (!geladen || !groesse) return;

        const void* daten = LockResource(geladen);
        if (!daten) return;

        int kanaele = 0;
        stbi_uc* pixel = stbi_load_from_memory(static_cast<const stbi_uc*>(daten),
                                               static_cast<int>(groesse),
                                               &g_hgBreite, &g_hgHoehe, &kanaele, 4);
        if (!pixel) return;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = g_hgBreite;
        td.Height = g_hgHoehe;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = pixel;
        sd.SysMemPitch = g_hgBreite * 4;

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex)) && tex)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
            vd.Format = td.Format;
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = 1;
            g_dev->CreateShaderResourceView(tex, &vd, &g_hintergrund);
            tex->Release();
        }

        stbi_image_free(pixel);
    }

    void gibHintergrundFrei()
    {
        if (g_hintergrund) { g_hintergrund->Release(); g_hintergrund = nullptr; }
    }

    // Das Bild fuellt das Fenster immer ganz aus und behaelt sein
    // Seitenverhaeltnis: die knappere Achse bestimmt den Massstab, die andere
    // laeuft mittig ueber den Rand hinaus. Ein Verzerren beim Ziehen des
    // Fensters waere sofort sichtbar, ein Beschnitt faellt nicht auf.
    //
    // Gezeichnet wird sehr dunkel getoent - der Hintergrund soll das Bild
    // erkennen lassen, ohne dass Text darauf schwer zu lesen wird.
    void zeichneHintergrund()
    {
        if (!g_hintergrund || g_hgBreite <= 0 || g_hgHoehe <= 0) return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float fw = vp->WorkSize.x, fh = vp->WorkSize.y;
        if (fw <= 0.0f || fh <= 0.0f) return;

        const float massstab = (fw / static_cast<float>(g_hgBreite) >
                                fh / static_cast<float>(g_hgHoehe))
                             ? fw / static_cast<float>(g_hgBreite)
                             : fh / static_cast<float>(g_hgHoehe);

        const float bw = g_hgBreite * massstab;
        const float bh = g_hgHoehe * massstab;
        const ImVec2 links(vp->WorkPos.x + (fw - bw) * 0.5f,
                           vp->WorkPos.y + (fh - bh) * 0.5f);
        const ImVec2 rechts(links.x + bw, links.y + bh);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddImage(reinterpret_cast<ImTextureID>(g_hintergrund), links, rechts,
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 46));
    }

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

        // Die SCRIPT.DAT liegt bei einer echten Installation im SPIELORDNER
        // neben der EXE, NICHT in data\ - dort liegen nur die Level. Weil
        // dataDir() aber auf data\ zeigt, sobald es den Ordner gibt, suchte
        // doScriptDat sie jahrelang an der falschen Stelle, fand sie nicht
        // und stieg still aus ("SCRIPT.DAT nicht gefunden"). Damit lief der
        // ganze Skript-Schritt bei keinem einzigen GUI-Lauf: kein
        // Kind-Lara-Bit, kein KEY8 fuer joby3, kein Pistolenentzug fuer
        // rich2. Vom Nutzer gefunden - die Pistolen kamen trotz Fix weiter
        // aus rich1 mit, und seine SCRIPT.DAT war neun Tage alt.
        //
        // Gesucht wird deshalb an beiden Stellen, Spielordner zuerst.
        std::wstring scriptPath() const
        {
            const std::wstring imSpiel = PathUtil::join(cfg.sourceDir, L"SCRIPT.DAT");
            if (PathUtil::fileExists(imSpiel)) return imSpiel;
            return PathUtil::join(dataDir(), L"SCRIPT.DAT");
        }
        std::wstring payloadDir() const { return PathUtil::join(PathUtil::exeDir(), L"data"); }

        // Balance.txt liegt neben der PCtomb5.exe, also im SPIELORDNER - aus
        // demselben Grund wie die SCRIPT.DAT und mit derselben Falle: hat der
        // Nutzer den data-Ordner gewaehlt, ist der Spielordner eine Ebene
        // hoeher. Anders als bei der SCRIPT.DAT hilft "wo liegt sie schon"
        // nicht weiter, weil wir sie auch anlegen koennen muessen - deshalb
        // wird an der EXE festgemacht.
        std::wstring spielOrdner() const
        {
            if (cfg.sourceDir.empty()) return std::wstring();
            if (PathUtil::fileExists(PathUtil::join(cfg.sourceDir, L"PCtomb5.exe")))
                return cfg.sourceDir;

            const size_t schnitt = cfg.sourceDir.find_last_of(L"\\/");
            if (schnitt != std::wstring::npos)
            {
                const std::wstring eltern = cfg.sourceDir.substr(0, schnitt);
                if (PathUtil::fileExists(PathUtil::join(eltern, L"PCtomb5.exe")))
                    return eltern;
            }

            return cfg.sourceDir;
        }

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

        // Secrets (#53): 0 Vanilla, 1 normaler Item-Pool, 2 Glitchless-Liste,
        // 3 Glitched-Liste. Die Listen selbst liest SecretListe.cpp aus
        // data\secrets; angewandt werden sie als zweiter Patch-Durchgang in
        // doLevel. Kein Seed-Bit: die Listen sind deterministisch, derselbe
        // Seed bleibt mit und ohne Secrets vergleichbar.
        int optSecrets = 0;

        // Gefundene Secret-Listen (data\secrets) und der gewaehlte Autor.
        std::vector<SecretQuelle> secretQuellen;
        std::string secretAutor;

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
        int  optLevelFolge = 0;         // 0 aus, 1 innerhalb der Abschnitte (Stufe 2), 2 frei ueber alles (Stufe 3)
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

        // Regler fuer Balance.txt (#108). Als Faktor gehalten, weil die
        // Oberflaeche "1.25x" zeigt; in der Datei stehen Prozentzahlen.
        float balWaffen        = 1.0f;
        float balGegnerLeben   = 1.0f;
        float balGegnerSchaden = 1.0f;

        // Je Regler: Wert aus dem Seed wuerfeln statt von Hand einstellen.
        // Der Regler zeigt dann, was gefallen ist, laesst sich aber nicht
        // mehr ziehen.
        bool balWaffenZufall        = false;
        bool balGegnerLebenZufall   = false;
        bool balGegnerSchadenZufall = false;

        std::string balHinweis;   // letzte Rueckmeldung unter den Reglern
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

        // Secret-Listen einsammeln - hier statt beim Start, damit ein frisch
        // dazugelegter Autor ohne Neustart auftaucht (inspectSource laeuft bei
        // jedem Ordnerwechsel und nach jedem Lauf).
        a.secretQuellen = secretQuellenSuchen(PathUtil::join(a.payloadDir(), L"secrets"));

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
        const std::wstring skript = a.scriptPath();
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

        // Hintergrundmusik: erst NACH den Pflichteingriffen wuerfeln, und nur
        // wenn der Haken steht. Weil oben immer aus der Sicherung gelesen
        // wird, stellt ein Lauf OHNE Haken die Originalmusik wieder her.
        int musikLevel = 0;
        if (a.phMusikSwitch)
        {
            std::string mErr;
            if (!sd.musikWuerfeln(a.effectiveSeed(), musikLevel, mErr))
                a.scriptNote = "Hintergrundmusik: " + mErr;
        }

        // Levelabschnitte: als LETZTER Eingriff, damit alle Namens-Lookups
        // davor auf dem bekannten Stand liefen (zerlege laeuft danach neu).
        std::string abschnitte, levelFolge;
        std::vector<std::string> ketten;
        // Bei "frei ueber alles" ist der Abschnitts-Wuerfel bedeutungslos - und
        // er darf auch nicht laufen, sonst startet der freie Wuerfel von einer
        // anderen Ausgangsfolge und derselbe Seed ergaebe je nach Haken zwei
        // verschiedene Spiele.
        if (a.phLevelAbschnitte && a.optLevelFolge != 2)
        {
            std::string aErr;
            if (!sd.abschnitteWuerfeln(a.effectiveSeed(), abschnitte, aErr))
                a.scriptNote = "Levelabschnitte: " + aErr;
        }
        if (a.optLevelFolge > 0)
        {
            std::string lErr;
            if (!sd.levelWuerfeln(a.effectiveSeed(), a.optLevelFolge == 2, levelFolge, lErr))
                a.scriptNote = "Level-Reihenfolge: " + lErr;
        }
        // Ketten immer auf die JETZIGE Reihenfolge pruefen - in Vanilla-Folge
        // passiert nichts, nach einem Wuerfel reicht das Skript nach.
        if (a.phLevelAbschnitte || a.optLevelFolge > 0)
        {
            std::string kErr;
            // ERST aufraeumen, DANN geben: die Wegnahmen muessen im Block vor den
            // Gaben stehen, damit die Engine sie in der dritten Phase
            // (setup.cpp) nach den Wegnahmen anwendet.
            if (!sd.inventarAufraeumen(ketten, kErr))
                a.scriptNote = "Inventar aufraeumen: " + kErr;
            else if (!sd.kettenAufloesen(ketten, kErr))
                a.scriptNote = "Levelketten: " + kErr;
        }

        if (!sd.save(skript, err)) return false;

        info = e.flagsGeaendert > 0
            ? "SCRIPT.DAT: Kind-Lara in " + std::to_string(e.flagsGeaendert) + " Ireland-Leveln abgeschaltet."
            : "SCRIPT.DAT: Kind-Lara war bereits abgeschaltet.";
        if (!e.keyFehler.empty())   info += "  KEY8 fuer joby3 nicht uebernommen: " + e.keyFehler;
        else if (e.keyKopiert)      info += "  KEY8 fuer joby3 aus joby2 uebernommen.";
        if (!e.pistolenFehler.empty()) info += "  rich2-Pistolen nicht entzogen: " + e.pistolenFehler;
        else if (e.pistolenWeg > 0)    info += "  rich2: Pistolen abgenommen.";
        if (e.joby3Waffen > 0)         info += "  joby3: " + std::to_string(e.joby3Waffen) + " Waffen-Wegnahmen entfernt.";
        if (a.phMusikSwitch && musikLevel > 0)
            info += "  Hintergrundmusik: " + std::to_string(musikLevel) + " Level neu vertont.";
        if (a.phLevelAbschnitte && !abschnitte.empty())
            info += "  Abschnitte: " + abschnitte + ".";
        if (a.optLevelFolge > 0 && !levelFolge.empty())
            info += "  Reihenfolge: " + levelFolge + ".";
        for (const std::string& k : ketten)
            info += "  " + k + ".";
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

        // Secrets in den Pool (#53, Art 1): die Handkuration pinnt jedes
        // Secret ueber fixedItems fest. Genau diese Pins nimmt der Modus
        // heraus - Typ 223 wuerfelt dann als gewoehnliches Pickup mit. Nur
        // die Secret-Pins fallen, alle anderen Handkurationen bleiben.
        if (a.optSecrets == 1)
        {
            std::set<int> secrets;
            for (const LevelItem& it : in.items)
                if (it.typeId == 223) secrets.insert(it.index);
            in.fixedItems.erase(
                std::remove_if(in.fixedItems.begin(), in.fixedItems.end(),
                               [&](int i) { return secrets.count(i) != 0; }),
                in.fixedItems.end());
        }

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

        // Secrets aus einer Nutzerliste (#53, Art 2/3): ZWEITER Durchgang mit
        // derselben apply-Maschinerie, gegen den echten Stand nach dem ersten.
        // Ohne "itemBase" im Patch verschiebt apply nichts - die Neu<k>-
        // Verweise der Liste zeigen exakt auf die Items, die dieser Patch
        // anlegt. Eine fehlende Liste ist kein Abbruch (das Level ist ja
        // fertig), ein GESCHEITERTER Patch schon - halb gesetzte Secrets
        // faende niemand wieder.
        int secretsGesetzt = 0, beiwerk = 0;
        if (a.optSecrets >= 2)
        {
            const char* kat = (a.optSecrets == 2) ? "Glitchless" : "Glitched";
            const SecretQuelle* liste = nullptr;
            for (const SecretQuelle& q : a.secretQuellen)
                if (q.kategorie == kat && q.autor == a.secretAutor) { liste = &q; break; }

            if (!liste)
                rep.notes.push_back(std::string("Secrets: keine ") + kat + "-Liste gewaehlt - Level bleibt vanilla.");
            else
            {
                JsonValue sPatch;
                std::vector<std::string> sWarn;
                std::string sErr;
                int sZahl = 0;
                if (!secretPatchBauen(liste->pfad, base, lvl, sPatch, sZahl, sWarn, sErr))
                { err = "Secret-Liste: " + sErr; return false; }
                for (const std::string& w : sWarn) rep.notes.push_back("Secrets: " + w);

                PatchReport spr;
                if (!Patch::apply(lvl, sPatch, spr, err, quelle, nullptr,
                                  tr3Ordner(a), goldOrdner(a), lagerDir(a)))
                { err = "Secret-Patch: " + err; return false; }
                for (const std::string& w : spr.warnings) rep.notes.push_back("Secrets: " + w);
                // NUR die umgezogenen Secrets zaehlen. edited+added des
                // Reports enthaelt das Beiwerk (Schluessel, Schalter,
                // Umtypungen) - die Statuszeile meldete damit "6 Secrets" in
                // einem Level mit dreien (vom Nutzer gesehen).
                secretsGesetzt = sZahl;
                beiwerk = (spr.edited + spr.added) - sZahl;
            }
        }
        // Musik-Trigger: als letzter Pass auf dem fertigen Level, direkt vor
        // dem Speichern - so erwischt er auch Trigger, die erst die
        // Secret-Liste gesetzt hat.
        if (a.phMusikTrigger)
        {
            const long mw = Patch::musikTriggerWuerfeln(lvl, seed);
            if (mw > 0)
                rep.notes.push_back("Musik-Trigger: " + std::to_string(mw) + " Woerter neu verteilt");
        }

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
        if (secretsGesetzt > 0 || beiwerk > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d Secrets (+%d Beiwerk)", secretsGesetzt, beiwerk);
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
    //------------------------------------------------------------------------
    // Die angehakten Balance-Regler aus dem Seed wuerfeln (#108).
    //
    // Gewuerfelt wird in Fuenferschritten von 0.50x bis 2.00x - auch der
    // Waffenschaden, obwohl sein Regler bis 0.00x reicht. Ein Seed, der Laras
    // Waffen auf ein Zehntel stellt, waere nicht schwer, sondern unspielbar;
    // 0.00x gehoert deshalb der Handeinstellung.
    //
    // ALLE DREI werden immer geworfen, auch die nicht angehakten. Sonst haenge
    // der Wurf des Waffenschadens davon ab, ob daneben die Gegnerhaken
    // gesetzt sind - derselbe Seed gaebe unterschiedliche Werte, je nachdem
    // was sonst noch angekreuzt ist.
    //------------------------------------------------------------------------
    void balanceWuerfeln(App& a)
    {
        if (!a.balWaffenZufall && !a.balGegnerLebenZufall && !a.balGegnerSchadenZufall)
            return;

        Rng r(a.effectiveSeed() ^ 0x42414C414E434521ull);   // "BALANCE!" - eigener Strom

        auto wurf = [&r]() { return (50 + static_cast<int>(r.below(31)) * 5) / 100.0f; };

        const float waffen = wurf();
        const float leben  = wurf();
        const float schaden = wurf();

        if (a.balWaffenZufall)        a.balWaffen        = waffen;
        if (a.balGegnerLebenZufall)   a.balGegnerLeben   = leben;
        if (a.balGegnerSchadenZufall) a.balGegnerSchaden = schaden;
    }

    // Die Reglerstellung in die Balance.txt der Installation schreiben.
    void balanceSchreibenNach(App& a)
    {
        BalanceDatei::Werte w;
        w.waffenSchaden = static_cast<int>(a.balWaffen * 100.0f + 0.5f);
        w.gegnerLeben   = static_cast<int>(a.balGegnerLeben * 100.0f + 0.5f);
        w.gegnerSchaden = static_cast<int>(a.balGegnerSchaden * 100.0f + 0.5f);

        std::string fehler;
        if (BalanceDatei::schreiben(a.spielOrdner(), w, &fehler))
            a.balHinweis = "Balance.txt geschrieben - gilt ab dem naechsten Spielstart.";
        else
            a.balHinweis = fehler;
    }

    //------------------------------------------------------------------------
    // spoiler.txt neben dem Programm schreiben.
    //
    // Frueher stand das je Level in einer dritten Tabellenspalte. Das kostete
    // die halbe Fensterbreite fuer eine Zeile Text, die man einmal liest -
    // und die Zeile war zu kurz fuer alles, was man wirklich wissen will. In
    // der Datei ist Platz fuer den Seed, alle Einstellungen und das, was die
    // SCRIPT.DAT abbekommen hat.
    //------------------------------------------------------------------------
    bool schreibeSpoiler(App& a)
    {
        const std::wstring pfad = PathUtil::join(PathUtil::exeDir(), L"spoiler.txt");
        FILE* f = nullptr;
        if (_wfopen_s(&f, pfad.c_str(), L"wb") != 0 || !f) return false;

        auto zeile = [f](const char* s) { std::fprintf(f, "%s\r\n", s); };

        std::fprintf(f, "TR5 Randomizer - spoiler.txt\r\n");
        std::fprintf(f, "Build %s\r\n", RANDO_BUILD_STRING);
        zeile("");
        std::fprintf(f, "Seed:      %s\r\n", a.seedBuf);
        std::fprintf(f, "wirksam:   0x%016llX\r\n", (unsigned long long)a.effectiveSeed());
        zeile("");

        zeile("EINSTELLUNGEN");
        std::fprintf(f, "  Ausruestung:        %s\r\n", a.optOtherPickups ? "gewuerfelt" : "aus");
        std::fprintf(f, "  Itemmenge:          %s\r\n",
                     a.optItemMenge == 0 ? "Vanilla" : a.optItemMenge == 1 ? "mehr" : "viel mehr");
        std::fprintf(f, "  Schluessel/Puzzles: %s\r\n", a.optKeyItems ? "gewuerfelt" : "aus");
        std::fprintf(f, "  Outfits:            %s\r\n", a.optOutfits ? "gewuerfelt" : "aus");
        std::fprintf(f, "  Zusatzgegner:       %s\r\n",
                     a.optEnemies ? (std::to_string(a.optEnemyCount) + " pro Level").c_str() : "aus");
        std::fprintf(f, "  Gegner tauschen:    %s\r\n", a.optEnemySwap ? "an" : "aus");
        std::fprintf(f, "  Gegnerpool:         %s\r\n",
                     a.optEnemyPool == 0 ? "nur Levelpool"
                                         : a.optEnemyPool == 1 ? "Cross-Mix" : "Allsanity");
        if (!a.optGegnerAus.empty())
            std::fprintf(f, "  abgewaehlte Arten:  %d\r\n", (int)a.optGegnerAus.size());
        std::fprintf(f, "  Secrets:            %s%s%s\r\n",
                     a.optSecrets == 0 ? "Vanilla" : a.optSecrets == 1 ? "im Item-Pool"
                                                   : a.optSecrets == 2 ? "Glitchless-Liste"
                                                                       : "Glitched-Liste",
                     a.optSecrets >= 2 && !a.secretAutor.empty() ? ", Autor " : "",
                     a.optSecrets >= 2 && !a.secretAutor.empty() ? a.secretAutor.c_str() : "");
        std::fprintf(f, "  Levelabschnitte:    %s\r\n", a.phLevelAbschnitte ? "getauscht" : "aus");
        std::fprintf(f, "  Levelreihenfolge:   %s\r\n",
                     a.optLevelFolge == 0 ? "aus"
                                          : a.optLevelFolge == 1 ? "innerhalb der Abschnitte"
                                                                 : "frei ueber alles");
        std::fprintf(f, "  Hintergrundmusik:   %s\r\n", a.phMusikSwitch ? "getauscht" : "aus");
        std::fprintf(f, "  Trigger-Musik:      %s\r\n", a.phMusikTrigger ? "getauscht" : "aus");
        std::fprintf(f, "  Waffenschaden:      %d %%%s\r\n",
                     (int)(a.balWaffen * 100.0f + 0.5f), a.balWaffenZufall ? " (gewuerfelt)" : "");
        std::fprintf(f, "  Gegnerleben:        %d %%%s\r\n",
                     (int)(a.balGegnerLeben * 100.0f + 0.5f), a.balGegnerLebenZufall ? " (gewuerfelt)" : "");
        std::fprintf(f, "  Gegnerschaden:      %d %%%s\r\n",
                     (int)(a.balGegnerSchaden * 100.0f + 0.5f), a.balGegnerSchadenZufall ? " (gewuerfelt)" : "");
        zeile("");

        if (!a.scriptNote.empty())
        {
            zeile("SCRIPT.DAT");
            // Die Notiz ist ein langer Satz mit zwei Leerzeichen als Trenner -
            // je Teil eine Zeile liest sich deutlich besser.
            const std::string& s = a.scriptNote;
            size_t i = 0;
            while (i < s.size())
            {
                size_t e = s.find("  ", i);
                if (e == std::string::npos) e = s.size();
                std::string teil = s.substr(i, e - i);
                while (!teil.empty() && teil.front() == ' ') teil.erase(teil.begin());
                if (!teil.empty()) std::fprintf(f, "  %s\r\n", teil.c_str());
                i = (e == s.size()) ? e : e + 2;
            }
            zeile("");
        }

        zeile("LEVEL");
        for (const LevelRow& r : a.levels)
        {
            const char* zustand = r.state == LevelState::Vanilla  ? "original"
                                : r.state == LevelState::Modified ? "veraendert"
                                : r.state == LevelState::Missing  ? "fehlt" : "unbekannt";
            std::fprintf(f, "  %-16s %-11s %s\r\n", r.name.c_str(), zustand,
                         r.result.empty() ? "-" : r.result.c_str());
        }
        zeile("");
        std::fprintf(f, "%d Level randomisiert, %d Fehler.\r\n", a.doneCount, a.failCount);

        std::fclose(f);
        return true;
    }

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

        // Angehakte Regler neu aus dem Seed werfen und die Stellung in die
        // Installation schreiben (#108). Die Regler schreiben schon beim
        // Loslassen; hier geht es um den Wurf und darum, dass ein Lauf in
        // einem neu gewaehlten Spielordner nicht ohne Balance.txt dasteht.
        balanceWuerfeln(a);
        balanceSchreibenNach(a);

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

            const bool spoiler = schreibeSpoiler(a);

            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "%d Level randomisiert%s. %s Du kannst das Spiel jetzt starten.",
                          a.doneCount, a.failCount ? " - mit Fehlern, siehe spoiler.txt" : "",
                          spoiler ? "Einzelheiten stehen in spoiler.txt neben dem Programm."
                                  : "spoiler.txt liess sich nicht schreiben.");
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
    // Angehakte Balance-Regler zeigen immer, was der AKTUELLE Seed gibt -
    // deshalb je Bild neu geworfen statt nur beim Lauf. Der Wurf ist eine
    // Handvoll Rechenschritte und beruehrt keine Datei; geschrieben wird er
    // erst beim Anhaken, beim Zuruecksetzen und beim Lauf.
    balanceWuerfeln(a);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Durchsichtig, damit das Hintergrundbild ueberhaupt zu sehen ist - es
    // liegt auf der BackgroundDrawList, also HINTER den Fenstern. Die
    // Lesbarkeit kommt von der dunklen Grundfarbe und der schwachen Toenung
    // des Bildes, nicht von einer Flaeche darueber.
    ImGui::SetNextWindowBgAlpha(0.0f);
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

    // --- Regler fuer Balance.txt (#108) ----------------------------------
    //
    // Geschrieben wird erst, wenn der Regler LOSGELASSEN wird - waehrend des
    // Ziehens laeuft die Schleife mit 60 Bildern je Sekunde, das waeren 60
    // Schreibvorgaenge in die Installation des Nutzers je Sekunde.
    //
    // Angefasst werden nur die drei Prozentzeilen. Der Schaden je Waffe, die
    // Lebenspunkte je Gegner und der Schaden je Angriff bleiben stehen, damit
    // Handarbeit in der Datei eine Reglerbewegung ueberlebt.
    auto balanceSchreiben = [&a]() { balanceSchreibenNach(a); };

    auto balanceRegler = [&](const char* beschriftung, float* wert, bool* zufall,
                             float von, float bis, const char* hilfe)
    {
        ImGui::BeginDisabled(*zufall);
        ImGui::SetNextItemWidth(220.0f);
        ImGui::SliderFloat(beschriftung, wert, von, bis, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        hoverHilfe(hilfe);

        if (ImGui::IsItemDeactivatedAfterEdit())
            balanceSchreiben();

        ImGui::SameLine();
        ImGui::TextDisabled("(%d %%)", static_cast<int>(*wert * 100.0f + 0.5f));

        // Eigene Kennung je Haken, damit ImGui drei "Zufall" unterscheiden
        // kann - der sichtbare Text bleibt fuer alle derselbe.
        char kennung[64];
        std::snprintf(kennung, sizeof(kennung), "Zufall##%s", beschriftung);

        ImGui::SameLine();
        if (ImGui::Checkbox(kennung, zufall))
        {
            // Sofort werfen, damit der Regler nicht auf dem alten Wert
            // stehen bleibt und der Nutzer gleich sieht, was der Seed gibt.
            balanceWuerfeln(a);
            balanceSchreiben();
        }
        hoverHilfe("Statt von Hand: der Seed wuerfelt den Wert aus, in "
                   "Fuenferschritten zwischen 0.50x und 2.00x. Derselbe Seed "
                   "gibt immer denselben Wert, und zwar unabhaengig davon, ob "
                   "die anderen beiden Haken gesetzt sind.\n\n"
                   "Der Regler zeigt sofort, was der aktuelle Seed gibt. In die "
                   "Balance.txt wandert der Wurf beim Anhaken und beim naechsten "
                   "Lauf.\n\n"
                   "Auch der Waffenschaden wird nur ab 0.50x geworfen - ein Seed, "
                   "der Laras Waffen auf ein Zehntel stellt, waere nicht schwer, "
                   "sondern unspielbar. 0.00x bleibt der Handeinstellung.");
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

    balanceRegler("Waffenschaden", &a.balWaffen, &a.balWaffenZufall, 0.0f, 2.0f,
                  "Wirkt auf alle Waffen Laras zugleich - Pistolen, Uzi, "
                  "Schrotflinte, Revolver, HK, Grappling Gun und Fackel. "
                  "0.00x macht sie wirkungslos, 2.00x verdoppelt ihren Schaden.\n\n"
                  "Geschrieben wird die Zeile WaffenSchaden in Balance.txt neben "
                  "der PCtomb5.exe. Wer dort einzelne Waffen von Hand eingestellt "
                  "hat, behaelt sie: der Regler wirkt zusaetzlich obendrauf.");

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

    balanceRegler("Gegnerleben", &a.balGegnerLeben, &a.balGegnerLebenZufall, 0.5f, 2.0f,
                  "Wie zaeh alle Gegner sind. 0.50x halbiert ihre Lebenspunkte, "
                  "2.00x verdoppelt sie.\n\n"
                  "Geschrieben wird die Zeile GegnerLeben in Balance.txt neben der "
                  "PCtomb5.exe. Wer dort einzelne Gegner von Hand eingestellt hat, "
                  "behaelt sie: der Regler wirkt zusaetzlich obendrauf.");

    balanceRegler("Gegnerschaden", &a.balGegnerSchaden, &a.balGegnerSchadenZufall, 0.5f, 2.0f,
                  "Wie hart alle Gegner zuschlagen. Fallen, Stuerze, Feuer, Gift "
                  "und Ertrinken bleiben immer so hart wie im Original, ebenso die "
                  "fest eingebauten Levelgefahren wie das Deckengeschuetz.\n\n"
                  "Geschrieben wird die Zeile GegnerSchaden in Balance.txt neben "
                  "der PCtomb5.exe.");

    if (ImGui::Button("Balance zuruecksetzen"))
    {
        a.balWaffenZufall = a.balGegnerLebenZufall = a.balGegnerSchadenZufall = false;
        a.balWaffen = a.balGegnerLeben = a.balGegnerSchaden = 1.0f;
        balanceSchreiben();
    }
    hoverHilfe("Nimmt die Zufallshaken zurueck, setzt die drei Regler auf 1.00x "
               "und traegt das in Balance.txt ein. "
               "Von Hand eingestellte Einzelwerte in der Datei bleiben erhalten - "
               "wer die auch zurueck will, loescht die Balance.txt; das Spiel legt "
               "sie beim naechsten Start neu und vollstaendig an.");

    if (!a.balHinweis.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%s", a.balHinweis.c_str());
    }

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

    ImGui::SeparatorText("Level");
    ImGui::BeginDisabled(a.optLevelFolge == 2);	// bei "frei ueber alles" bedeutungslos und bewusst ohne Wirkung
    ImGui::Checkbox("Levelabschnitte switchen", &a.phLevelAbschnitte);
    ImGui::EndDisabled();
    hoverHilfe("Die vier Story-Abschnitte (Rom, Russland, Irland, VCI) in "
               "zufaelliger Reihenfolge spielen. Innerhalb eines Abschnitts "
               "bleibt die Reihenfolge - alle Schluesselketten bleiben gueltig, "
               "der Abspann kommt nach dem letzten Abschnitt. BRAUCHT die "
               "Engine ab 2026-08-22 (Levelidentitaet aus dem Dateinamen); "
               "alte Spielstaende passen nicht zur neuen Reihenfolge.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    const char* folgeNamen[] = { "Level-Reihenfolge: aus", "innerhalb der Abschnitte", "frei ueber alles" };
    ImGui::Combo("##levelfolge", &a.optLevelFolge, folgeNamen, 3);
    hoverHilfe("Innerhalb: Rom/Russland/Irland/VCI bleiben Bloecke, nur die Level "
               "darin tauschen. Frei: alle 13 Level ueber alle Abschnitte hinweg "
               "(z.B. rich1 -> Andrea2 -> joby5 -> rich2). Die Cutscene richcut2 "
               "bleibt immer an rich2. Bei jedem Levelstart werden Keys/Puzzles/"
               "Pickups und die Grappling Gun (nur rich3) zurueckgesetzt - Waffen, "
               "Munition, Medis und Leuchtfackeln bleiben. Was ein Level aus einem "
               "anderen braucht (Key 8 joby3, Key 7 joby5, Catapult Andy2, "
               "Brechstange Andrea3), gibt das Skript immer - samt Modell und Namen. "
               "Kuratiert in Levelketten.h.");

    ImGui::SeparatorText("Musik");
    ImGui::Checkbox("Hintergrundmusik switchen", &a.phMusikSwitch);
    hoverHilfe("Die Levelmusik wird unter den spielbaren Leveln vertauscht - "
               "das Audio-Byte im CMD_LEVEL-Eintrag der SCRIPT.DAT. Cutscene-"
               "Level behalten ihre Musik. Gewuerfelt wird bei jedem Lauf neu "
               "aus der Sicherung heraus; Haken weg = naechster Lauf stellt "
               "die Originalmusik wieder her.");
    ImGui::SameLine();
    ImGui::Checkbox("Trigger-Sound/Musik-IDs switchen", &a.phMusikTrigger);
    hoverHilfe("Die Musik-Trigger in den Leveln (TO_CD) bekommen vertauschte "
               "Tracks - GLOBAL ueber alle Level: nur Tracks, die irgendwo im Spiel "
               "ein Trigger ausloest (keine Cutscene-Dialoge, kein FMV-Ton), "
               "und derselbe Track klingt in jedem Level gleich. "
               "Atmosphaeren tauschen weiter nur unter sich, damit ein "
               "Stimmungswechsel ein Stimmungswechsel bleibt. Die fest "
               "einprogrammierte Cutscene- und Tutorial-Ausloesung ist "
               "nicht betroffen.");

    // --- Secrets (#53) ---------------------------------------------------
    //
    // Vier Arten: Vanilla (aus), normaler Pool, und zwei Nutzerlisten-
    // Kategorien mit Autoren-Dropdown. Die Listen liegen als
    // Autor_Kategorie.txt in data\secrets neben dem Programm; gelesen
    // werden sie von SecretListe.cpp - dieselbe Grammatik liest auch
    // SecretPlan.ps1 in der Kuration.
    ImGui::SeparatorText("Secrets");

    ImGui::RadioButton("Vanilla", &a.optSecrets, 0);
    hoverHilfe("Die Secrets bleiben, wo Core sie versteckt hat.");
    ImGui::SameLine();
    ImGui::RadioButton("Im normalen Item-Pool", &a.optSecrets, 1);
    hoverHilfe("Die Secrets wandern als gewoehnliche Pickups in den Wuerfelpool "
               "und koennen ueberall dort liegen, wo auch Ausruestung liegen darf.");

    ImGui::RadioButton("Glitchless-Liste", &a.optSecrets, 2);
    hoverHilfe("Handverlesene Verstecke eines Autors, ohne Glitches erreichbar. "
               "Eine Liste kann je Level auch Zusatzobjekte setzen - Schluessel, "
               "Schalter, Tueren, sogar neue Portale.");
    ImGui::SameLine();
    ImGui::RadioButton("Glitched-Liste", &a.optSecrets, 3);
    hoverHilfe("Wie die Glitchless-Liste, aber die Verstecke duerfen Glitches "
               "voraussetzen.");

    if (a.optSecrets >= 2)
    {
        const char* kat = (a.optSecrets == 2) ? "Glitchless" : "Glitched";
        std::vector<const SecretQuelle*> passend;
        for (const SecretQuelle& q : a.secretQuellen)
            if (q.kategorie == kat) passend.push_back(&q);

        ImGui::SetNextItemWidth(220.0f);
        const char* anzeige = "(keine Listen gefunden)";
        for (const SecretQuelle* q : passend)
            if (q->autor == a.secretAutor) anzeige = q->autor.c_str();
        if (!passend.empty() && anzeige[0] == '(')
        { a.secretAutor = passend[0]->autor; anzeige = passend[0]->autor.c_str(); }

        if (ImGui::BeginCombo("Autor", anzeige))
        {
            for (const SecretQuelle* q : passend)
                if (ImGui::Selectable(q->autor.c_str(), q->autor == a.secretAutor))
                    a.secretAutor = q->autor;
            ImGui::EndCombo();
        }
        if (passend.empty())
            ImGui::TextDisabled("Keine %s-Liste in data\secrets gefunden - die Level bleiben vanilla.", kat);
    }

    // --- 4. Level ---
    ImGui::SeparatorText("4. Level");
    if (a.skippedNonPlayable > 0)
        ImGui::TextDisabled("%d nicht spielbare Dateien (Title, del, richcut2) werden geprueft, aber nicht angetastet.",
                            a.skippedNonPlayable);
    // Feste Hoehe nach Levelanzahl statt Scrollleiste (Nutzerwunsch): ohne
    // ScrollY und ohne Hoehenvorgabe waechst die Tabelle genau auf ihre
    // Zeilen - bei 13 spielbaren Leveln ist das ueberschaubar.
    // Zwei Spalten statt drei: was ein Lauf im einzelnen getan hat, steht seit
    // dem Spoiler in spoiler.txt. Die dritte Spalte kostete die halbe
    // Fensterbreite fuer eine Zeile Text, die man einmal liest.
    if (ImGui::BeginTable("levels", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Zustand", ImGuiTableColumnFlags_WidthFixed, 150);
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

            // Solange kein Lauf gelaufen ist, steht hier der Dateizustand;
            // danach das Ergebnis dieses Laufs. Fehler bleiben also sichtbar,
            // ohne dass die Tabelle eine Textspalte braucht - die Einzelheiten
            // stehen in spoiler.txt.
            ImGui::TableSetColumnIndex(1);
            if (r.result.empty())
            {
                switch (r.state)
                {
                case LevelState::Vanilla:  ImGui::TextUnformatted("original"); break;
                case LevelState::Modified: ImGui::TextColored(ImVec4(1, .5f, .3f, 1), "veraendert"); break;
                case LevelState::Missing:  ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "fehlt"); break;
                default:                   ImGui::TextUnformatted("unbekannt"); break;
                }
            }
            else if (r.result.rfind("FEHLER", 0) == 0)
            {
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "Fehler");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", r.result.c_str());
            }
            else if (r.result == "uebersprungen")
            {
                ImGui::TextDisabled("uebersprungen");
            }
            else
            {
                ImGui::TextColored(ImVec4(.5f, .9f, .5f, 1), "fertig");
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", r.result.c_str());
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

    // Programmsymbol aus den Ressourcen (TR5Randomizer.rc, Logo des Nutzers).
    // LoadIconW nimmt aus der ICO-Datei je nach Feld die passende Groesse:
    // hIcon das grosse (Alt+Tab, Fenstereigenschaften), hIconSm das kleine
    // (Titelleiste, Taskleiste). Ohne diese beiden Zeilen zeigt Windows das
    // Standardsymbol, selbst wenn die Ressource in der EXE liegt.
    HICON symbolGross = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON symbolKlein = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, symbolGross, nullptr, nullptr, nullptr,
                    L"TR5RandomizerWnd", symbolKlein };
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
    ladeHintergrund();

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
    app.secretAutor     = app.cfg.secretAutor;
    app.phMusikSwitch     = app.cfg.optMusikSwitch;
    app.phMusikTrigger    = app.cfg.optMusikTrigger;
    app.phLevelAbschnitte = app.cfg.optLevelAbschnitte;
    app.optLevelFolge     = app.cfg.optLevelFolge;

    // Balance.txt (#108): steht sie schon in der Installation, hat SIE recht
    // und nicht unsere cfg - der Nutzer kann sie in Notepad geaendert haben,
    // und dann sollen die Regler zeigen, was wirklich gilt. Nur wenn dort
    // nichts steht, gilt die zuletzt gemerkte Reglerstellung.
    {
        app.balWaffenZufall        = app.cfg.balWaffenZufall;
        app.balGegnerLebenZufall   = app.cfg.balGegnerLebenZufall;
        app.balGegnerSchadenZufall = app.cfg.balGegnerSchadenZufall;

        const BalanceDatei::Werte da = BalanceDatei::lesen(app.spielOrdner());
        const bool vorhanden = PathUtil::fileExists(
            PathUtil::join(app.spielOrdner(), L"Balance.txt"));

        app.balWaffen        = (vorhanden ? da.waffenSchaden : app.cfg.balWaffen)        / 100.0f;
        app.balGegnerLeben   = (vorhanden ? da.gegnerLeben   : app.cfg.balGegnerLeben)   / 100.0f;
        app.balGegnerSchaden = (vorhanden ? da.gegnerSchaden : app.cfg.balGegnerSchaden) / 100.0f;

        // Von Hand weiter aufgedreht als die Regler reichen? Dann klemmt der
        // Regler auf seine Bahn - geschrieben wird aber erst, wenn der Nutzer
        // ihn anfasst, die Datei bleibt bis dahin unangetastet.
        auto klemme = [](float& v, float von, float bis)
        { if (v < von) v = von; if (v > bis) v = bis; };

        klemme(app.balWaffen, 0.0f, 2.0f);
        klemme(app.balGegnerLeben, 0.5f, 2.0f);
        klemme(app.balGegnerSchaden, 0.5f, 2.0f);
    }

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
        app.cfg.secretAutor     = app.secretAutor;
        app.cfg.optMusikSwitch     = app.phMusikSwitch;
        app.cfg.optMusikTrigger    = app.phMusikTrigger;
        app.cfg.optLevelAbschnitte = app.phLevelAbschnitte;
        app.cfg.optLevelFolge      = app.optLevelFolge;
        app.cfg.balWaffen        = static_cast<int>(app.balWaffen * 100.0f + 0.5f);
        app.cfg.balGegnerLeben   = static_cast<int>(app.balGegnerLeben * 100.0f + 0.5f);
        app.cfg.balGegnerSchaden = static_cast<int>(app.balGegnerSchaden * 100.0f + 0.5f);
        app.cfg.balWaffenZufall        = app.balWaffenZufall;
        app.cfg.balGegnerLebenZufall   = app.balGegnerLebenZufall;
        app.cfg.balGegnerSchadenZufall = app.balGegnerSchadenZufall;
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
        zeichneHintergrund();

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
                 app.cfg.secrets         != app.optSecrets ||
                 app.cfg.optMusikSwitch     != app.phMusikSwitch ||
                 app.cfg.optMusikTrigger    != app.phMusikTrigger ||
                 app.cfg.optLevelAbschnitte != app.phLevelAbschnitte ||
                 app.cfg.optLevelFolge      != app.optLevelFolge ||
                 app.cfg.balWaffen        != static_cast<int>(app.balWaffen * 100.0f + 0.5f) ||
                 app.cfg.balGegnerLeben   != static_cast<int>(app.balGegnerLeben * 100.0f + 0.5f) ||
                 app.cfg.balGegnerSchaden != static_cast<int>(app.balGegnerSchaden * 100.0f + 0.5f) ||
                 app.cfg.balWaffenZufall        != app.balWaffenZufall ||
                 app.cfg.balGegnerLebenZufall   != app.balGegnerLebenZufall ||
                 app.cfg.balGegnerSchadenZufall != app.balGegnerSchadenZufall ||
                 app.cfg.secretAutor     != app.secretAutor)
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
    gibHintergrundFrei();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
