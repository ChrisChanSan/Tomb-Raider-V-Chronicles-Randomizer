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
#include "core/Tr5Level.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
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

        std::wstring gameDir() const { return PathUtil::join(PathUtil::exeDir(), L"Game"); }
        std::wstring dataDir() const { return PathUtil::join(gameDir(), L"data"); }
        std::wstring payloadDir() const { return PathUtil::join(PathUtil::exeDir(), L"data"); }
    };

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

        const std::wstring dir = PathUtil::join(a.cfg.sourceDir, L"data");
        const std::wstring use = PathUtil::dirExists(dir) ? dir : a.cfg.sourceDir;

        auto st = a.data.inspect(use);
        for (const LevelStatus& s : st)
        {
            LevelRow r;
            r.name = s.name;
            r.state = s.state;
            r.enabled = (s.state == LevelState::Vanilla);
            a.levels.push_back(r);
        }

        std::string why;
        a.sourceOk = DataFolder::isUsableSource(st, why);
        setStatus(a, a.sourceOk ? "Installation geprueft - alle Level unveraendert." : why, !a.sourceOk);
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

        JsonValue patch;
        RandoReport rep;
        if (!Rando::run(in, RandoOptions(), seed, patch, rep)) { err = rep.error; return false; }

        Tr5Level lvl;
        const std::wstring target = PathUtil::join(a.dataDir(), PathUtil::widen(row.name));
        if (!lvl.load(target, err)) return false;

        PatchReport pr;
        if (!Patch::apply(lvl, patch, pr, err)) return false;
        if (!lvl.save(target, err)) return false;

        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d bewegt, %d neu", pr.edited, pr.added);
        row.result = buf;
        return true;
    }

    void doRandomize(App& a)
    {
        // Immer aus dem Original kopieren, nie auf ein schon randomisiertes
        // Level aufsetzen - sonst waere das Ergebnis aus dem Seed nicht mehr
        // nachstellbar, und genau dafuer gibt es den Seed.
        std::string err;
        const std::wstring dir = PathUtil::join(a.cfg.sourceDir, L"data");
        const std::wstring use = PathUtil::dirExists(dir) ? dir : a.cfg.sourceDir;

        int copied = 0;
        if (!a.data.copyLevels(use, a.dataDir(), err, &copied))
        {
            setStatus(a, "Kopieren fehlgeschlagen: " + err, true);
            return;
        }

        const uint64_t seed = SeedText::parse(a.seedBuf);
        int done = 0, failed = 0;
        for (LevelRow& r : a.levels)
        {
            r.result.clear();
            if (!r.enabled) { r.result = "uebersprungen"; continue; }
            std::string e;
            if (doLevel(a, r, seed, e)) { ++done; }
            else { ++failed; r.result = "FEHLER: " + e; }
        }

        a.cfg.seedText = a.seedBuf;
        a.cfg.save();

        char buf[192];
        std::snprintf(buf, sizeof(buf), "%d Level randomisiert%s. Spielordner: Game\\",
                      done, failed ? " - mit Fehlern, siehe Liste" : "");
        setStatus(a, buf, failed > 0);
    }

    void doRestore(App& a)
    {
        std::string err;
        const std::wstring dir = PathUtil::join(a.cfg.sourceDir, L"data");
        const std::wstring use = PathUtil::dirExists(dir) ? dir : a.cfg.sourceDir;

        int copied = 0;
        if (!a.data.copyLevels(use, a.dataDir(), err, &copied))
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
    ImGui::Separator();

    // --- 1. Spielordner ---
    ImGui::SeparatorText("1. Deine Installation");
    ImGui::TextWrapped("Waehle den Ordner deiner Tomb Raider Chronicles-Installation. "
                       "Er wird nur gelesen - randomisiert wird in einer eigenen Kopie.");

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
    ImGui::TextDisabled("intern: 0x%016llX", (unsigned long long)SeedText::parse(a.seedBuf));

    // --- 3. Level ---
    ImGui::SeparatorText("3. Level");
    if (ImGui::BeginTable("levels", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 260)))
    {
        ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Zustand", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Ergebnis");
        ImGui::TableSetupScrollFreeze(0, 1);
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

    // --- 4. Los ---
    ImGui::SeparatorText("4. Los geht's");
    ImGui::BeginDisabled(!a.sourceOk);
    if (ImGui::Button("Randomisieren", ImVec2(160, 32))) doRandomize(a);
    ImGui::SameLine();
    if (ImGui::Button("Auf Original zuruecksetzen", ImVec2(220, 32))) doRestore(a);
    ImGui::EndDisabled();

    if (!a.sourceOk)
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
}

//----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, nullptr, nullptr, nullptr, nullptr,
                    L"TR5RandomizerWnd", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TR5 Randomizer", WS_OVERLAPPEDWINDOW,
                              100, 100, 820, 780, nullptr, nullptr, hInst, nullptr);

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
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    App app;
    app.cfg.load();
    std::snprintf(app.seedBuf, sizeof(app.seedBuf), "%s",
                  app.cfg.seedText.empty() ? SeedText::format(SeedText::random()).c_str()
                                           : app.cfg.seedText.c_str());

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

        ImGui::Render();
        const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
