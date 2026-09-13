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
// TR5 Randomizer - the GUI.
//
// Own code on Dear ImGui (MIT, Omar Cornut) with the Direct3D 11 backend.
// The layout is deliberately plain: one window, numbered steps from top to
// bottom. Whoever starts the tool for the first time should get through
// without a manual.
//
// Everything substantial lives in core/ and runs without a GUI - see
// Randomize.cpp, which performs the same steps on the command line. This
// file therefore contains no game logic, only operation.
//----------------------------------------------------------------------------
#include "core/ApplyPatch.h"
#include "core/BalanceFile.h"
#include "core/LevelFix.h"
#include "core/BuildId.h"
#include "core/Config.h"
#include "core/DataFolder.h"
#include "core/EmbeddedData.h"
#include "core/Json.h"
#include "core/LevelInfo.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/ScriptDat.h"
#include "core/SecretList.h"
#include "core/SecretReward.h"
#include "core/Tr5Level.h"
#include "core/Tr3Source.h"
// The engine's own two scans (#153, round M2) - see Randomize.cpp.
#include "../../TOMB5 Code/TOMB5/specific/hd/hd_mods.h"
#include "../../TOMB5 Code/TOMB5/specific/hd/hd_wardrobe.h"

char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];

#include "Language.h"

#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "backends/imgui_impl_win32.h"

// stb_image (Public Domain, Sean Barrett) - only for the embedded background
// image. Shipped under libs/stb so the source package compiles without any
// further procurement.
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
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

//----------------------------------------------------------------------------
// D3D11 scaffolding. Nothing special - exactly as much as ImGui needs to
// draw.
//----------------------------------------------------------------------------
namespace
{
    ID3D11Device*           g_dev = nullptr;
    ID3D11DeviceContext*    g_ctx = nullptr;
    IDXGISwapChain*         g_swap = nullptr;
    bool                    g_windowDragged = false;   // WM_EXITSIZEMOVE seen
    ID3D11RenderTargetView* g_rtv = nullptr;

    // Background image: the user's banner, as JPEG inside the EXE
    // (TR5Randomizer.rc, IDR_BACKGROUND). Unpacked once at startup, then
    // drawn at window size every frame.
    ID3D11ShaderResourceView* g_background = nullptr;
    int                       g_bgWidth = 0, g_bgHeight = 0;

    void loadBackground()
    {
        HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(201), reinterpret_cast<LPCWSTR>(RT_RCDATA));
        if (!res) return;

        const DWORD size = SizeofResource(nullptr, res);
        HGLOBAL loaded = LoadResource(nullptr, res);
        if (!loaded || !size) return;

        const void* bytes = LockResource(loaded);
        if (!bytes) return;

        int channels = 0;
        stbi_uc* pixel = stbi_load_from_memory(static_cast<const stbi_uc*>(bytes),
                                               static_cast<int>(size),
                                               &g_bgWidth, &g_bgHeight, &channels, 4);
        if (!pixel) return;

        D3D11_TEXTURE2D_DESC td{};
        td.Width = g_bgWidth;
        td.Height = g_bgHeight;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = pixel;
        sd.SysMemPitch = g_bgWidth * 4;

        ID3D11Texture2D* tex = nullptr;
        if (SUCCEEDED(g_dev->CreateTexture2D(&td, &sd, &tex)) && tex)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC vd{};
            vd.Format = td.Format;
            vd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = 1;
            g_dev->CreateShaderResourceView(tex, &vd, &g_background);
            tex->Release();
        }

        stbi_image_free(pixel);
    }

    void freeBackground()
    {
        if (g_background) { g_background->Release(); g_background = nullptr; }
    }

    // The image always fills the window completely and keeps its aspect
    // ratio: the tighter axis sets the scale, the other runs centered past
    // the edge. Distortion while dragging the window would be immediately
    // visible, a crop goes unnoticed.
    //
    // Drawn very darkly tinted - the background should let the image show
    // without making text on top of it hard to read.
    void drawBackground()
    {
        if (!g_background || g_bgWidth <= 0 || g_bgHeight <= 0) return;

        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float fw = vp->WorkSize.x, fh = vp->WorkSize.y;
        if (fw <= 0.0f || fh <= 0.0f) return;

        const float scale = (fw / static_cast<float>(g_bgWidth) >
                             fh / static_cast<float>(g_bgHeight))
                          ? fw / static_cast<float>(g_bgWidth)
                          : fh / static_cast<float>(g_bgHeight);

        const float bw = g_bgWidth * scale;
        const float bh = g_bgHeight * scale;
        const ImVec2 lt(vp->WorkPos.x + (fw - bw) * 0.5f,
                        vp->WorkPos.y + (fh - bh) * 0.5f);
        const ImVec2 rb(lt.x + bw, lt.y + bh);

        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        dl->AddImage(reinterpret_cast<ImTextureID>(g_background), lt, rb,
                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 70));
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
        // Without a usable graphics card, fall back to the software
        // rasterizer - the GUI is a couple of rectangles, that is plenty.
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
            // Maximize/restore comes without WM_EXITSIZEMOVE - that too
            // should update the saved size.
            if (wp == SIZE_MAXIMIZED || wp == SIZE_RESTORED) g_windowDragged = true;
            return 0;
        case WM_EXITSIZEMOVE:
            // The user dragged the window - the main loop then writes the
            // settings file (user wish: remember the size).
            g_windowDragged = true;
            return 0;
        case WM_GETMINMAXINFO:
            // The upper area stands in four columns. If the window shrinks
            // below this width, the boxes become so narrow that sliders and
            // labels no longer fit side by side - hence a hard lower bound
            // instead of a broken-looking GUI.
            {
                MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
                mm->ptMinTrackSize.x = 1180;
                mm->ptMinTrackSize.y = 780;
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xfff0) == SC_KEYMENU) return 0;   // suppress the Alt menu
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    // Folder selection via the Windows-native dialog box.
    bool pickFolder(HWND owner, std::wstring& out)
    {
        bool ok = false;
        IFileOpenDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg))))
        {
            DWORD opts = 0;
            dlg->GetOptions(&opts);
            dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dlg->SetTitle(gGermanUi
                ? L"Ordner der Tomb Raider Chronicles-Installation waehlen"
                : L"Choose the folder of your Tomb Raider Chronicles installation");
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
// Application state
//----------------------------------------------------------------------------
namespace
{
    // Non-playable files. They belong to a complete installation and are
    // included in the checksum check, but randomizing them makes no sense:
    // Title is the title screen - changing anything there can only do harm -,
    // del and richcut2 are pure cutscene levels without game content
    // (richcut2 has two items, del ten, not a single one of them pickable).
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
        bool                     payloadOk = false;   // checksum list loaded?
        std::string              payloadError;
        // Mixed curation stamps of the zone exports in data\ - empty = ok.
        std::string              zoneWarning;
        char                     seedBuf[64] = {};
        bool                     busy = false;

        // The tool works directly in the user's installation. The originals
        // lie next to it in data\Backup and are the yardstick from then on -
        // see ensureBackup().
        std::wstring dataDir() const
        {
            const std::wstring sub = PathUtil::join(cfg.sourceDir, L"data");
            return PathUtil::dirExists(sub) ? sub : cfg.sourceDir;
        }
        std::wstring backupDir() const { return PathUtil::join(dataDir(), L"Backup"); }

        // In a real installation, SCRIPT.DAT lies in the GAME FOLDER next to
        // the EXE, NOT in data\ - only the levels lie there. But because
        // dataDir() points at data\ as soon as that folder exists,
        // doScriptDat looked for it in the wrong place for ages, did not
        // find it and silently bailed out ("SCRIPT.DAT not found"). The
        // whole script step therefore never ran in a single GUI run: no
        // young-Lara bit, no KEY8 for joby3, no pistol removal for rich2.
        // Found by the user - the pistols kept coming along from rich1
        // despite the fix, and his SCRIPT.DAT was nine days old.
        //
        // Hence both places are searched, game folder first.
        // SCRIPT.DAT lies in the GAME folder, next to PCtomb5.exe - never in
        // data\, where only the levels are. Ask gameDir() for it, because
        // that is the one place that copes with the user having picked the
        // data folder: it walks one level up when PCtomb5.exe is not where
        // it was pointed.
        //
        // 2026-08-30: this used to search sourceDir and then dataDir(), and
        // with the data folder chosen BOTH of those are the same wrong
        // place. The whole script step then bailed out with "SCRIPT.DAT not
        // found" - no young-Lara bit, no Key 8 for joby3, no pistol removal
        // for rich2 - and the only trace was one line in spoiler.txt.
        // gameDir() already knew how to find it; scriptPath() just never
        // asked it.
        std::wstring scriptPath() const
        {
            const std::wstring inGame = PathUtil::join(gameDir(), L"SCRIPT.DAT");
            if (PathUtil::fileExists(inGame)) return inGame;
            const std::wstring chosen = PathUtil::join(cfg.sourceDir, L"SCRIPT.DAT");
            if (PathUtil::fileExists(chosen)) return chosen;
            return PathUtil::join(dataDir(), L"SCRIPT.DAT");
        }
        std::wstring payloadDir() const { return PathUtil::join(PathUtil::exeDir(), L"data"); }

        // Balance.txt lies next to PCtomb5.exe, i.e. in the GAME FOLDER -
        // for the same reason as SCRIPT.DAT and with the same trap: if the
        // user picked the data folder, the game folder is one level up.
        // Unlike SCRIPT.DAT, "where does it already lie" does not help here,
        // because we must also be able to CREATE it - so it is anchored at
        // the EXE.
        std::wstring gameDir() const
        {
            if (cfg.sourceDir.empty()) return std::wstring();
            if (PathUtil::fileExists(PathUtil::join(cfg.sourceDir, L"PCtomb5.exe")))
                return cfg.sourceDir;

            const size_t cut = cfg.sourceDir.find_last_of(L"\\/");
            if (cut != std::wstring::npos)
            {
                const std::wstring parent = cfg.sourceDir.substr(0, cut);
                if (PathUtil::fileExists(PathUtil::join(parent, L"PCtomb5.exe")))
                    return parent;
            }

            return cfg.sourceDir;
        }

        bool haveBackup = false;
        int  skippedNonPlayable = 0;
        std::string scriptNote;      // what SCRIPT.DAT yielded on the last run

        // A run does not go through in one piece but step by step: one level
        // per frame. Otherwise the window freezes for the duration of all 15
        // levels, Windows reports "not responding", and the user takes it
        // for a crash - although everything is running. The progress bar
        // needs the same split, or it would have nothing to show.
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

        // What should be randomized at all?
        bool optOtherPickups = true;
        bool optKeyItems = true;
        // Add extra enemies to the level. Which types a level can take is
        // curated (ENEMY_TYPES in ZoneAnalysis.ps1) - if the list is empty,
        // the checkbox has no effect.
        bool optEnemies = false;
        int  optEnemyCount = 6;

        // Swap the existing enemies among each other.
        bool optEnemySwap = false;
        // Enemy pool tier: 0 level pool only, 1 cross mix (up to three
        // foreign types), 2 allsanity (the whole pool, every role rolls
        // freely). Only effective together with optEnemySwap or optEnemies.
        int optEnemyPool = 0;
        // Random outfits (Lara_Andrea/Lara_Joby/Lara_Rich) per section.
        bool optOutfits = false;
        bool optOutfitsTr3 = false;     // guest outfits from Tomb Raider 3
        // The wardrobe (#122): which outfits the roll may NOT draw. Held is
        // the DESELECTION, as with the enemy types - an outfit added later is
        // then automatically on.
        std::set<std::string> optOutfitsOff;
        /* And the SELECTION for the entries the wardrobe scan found (#153,
           round M2). Empty by default: which of them exist depends on what
           is installed and which mods are in the folder, so "on unless said
           otherwise" would make a run change silently when a mod is added -
           and an old seed has to go on producing the same game. */
        std::set<std::string> optOutfitsOn;
        bool wardrobeOpen = false;
        // The full weapon set in every level is STANDARD (user decision, no
        // checkbox): opt.allWeapons is hard-set to true on every run. Hence
        // no seed bit for it either - a constant distinguishes no games.

        // Secrets (#53): 0 vanilla, 1 normal item pool, 2 glitchless list,
        // 3 glitched list. The lists themselves are read by SecretList.cpp
        // from data\secrets; they are applied as a second patch pass in
        // doLevel. No seed bit: the lists are deterministic, the same seed
        // stays comparable with and without secrets.
        int optSecrets = 0;
        // Roll what a level awards for every secret found (#53). Off by
        // default: it writes SecretRewards.txt beside the game, and a file
        // the player may have set himself is not something to overwrite
        // without being asked.
        bool optSecretRewards = false;

        // Found secret lists (data\secrets) and the chosen author.
        std::vector<SecretSource> secretSources;
        std::string secretAuthor;

        // Further PLACEHOLDERS (user wish 2026-08-14), all without effect,
        // without a seed bit and without persistence until the respective
        // logic exists.
        int  optItemAmount = 0;         // 0 vanilla amount, 1 more, 2 a lot more (#36)
        bool phTexRgb = false;          // color dice over vanilla textures
        bool phTexMix = false;          // mix textures across levels
        bool phTexRemaster = false;     // patch remaster textures
        bool phRoomBackdrops = false;   // include backdrop areas
        bool phRoomNeighbors = false;   // open locked neighboring rooms
        bool phJoby4NoWater = false;    // Deepsea Dive without water
        bool phLevelSections = false;   // shuffle sections (Rome/Russia/...)
        int  optLevelOrder = 0;         // 0 off, 1 within the sections (tier 2), 2 free across everything (tier 3)
        // Weather per level (#144): dry, rain or snow instead of what Core
        // set. Only the six levels that can show it take part - which ones
        // is read out of the level files, not listed here.
        bool optWeather = false;
        bool phMusicSwitch = false;     // roll the audio track per level
        bool phMusicTrigger = false;    // roll music/sound trigger IDs
        bool phOutfitTr1 = false;       // classic TR1 outfit in the pool
        bool phOutfitTr2 = false;       // TR2 outfits in the pool
        bool phOutfitTr3 = false;       // TR3 outfits in the pool

        // The selection goes into the seed. Otherwise the same seed with
        // different checkboxes produces a different game, and a bug report
        // "seed X is unplayable" would not be reproducible - exactly what
        // the seed mechanism is supposed to prevent. That is also why the
        // user sees the effective value below, not just the entered text.
        uint64_t effectiveSeed() const
        {
            uint64_t flags = 0;
            if (optOtherPickups) flags |= 1;
            if (optKeyItems)     flags |= 2;
            if (optEnemies)          flags |= 4;
            if (optEnemyPool == 1)   flags |= 8;    // cross mix
            if (optEnemySwap)        flags |= 16;
            if (optOutfits)          flags |= 32;
            if (optOutfitsTr3)       flags |= 128;
            if (optEnemyPool == 2)   flags |= 64;   // allsanity
            // Bit 64 was briefly "all weapons everywhere" - since those are
            // standard, it now carries allsanity.
            // The count belongs in the seed just like the checkboxes: with 3
            // and with 12 enemies it is not the same game.
            flags |= static_cast<uint64_t>(optEnemyCount & 0xFF) << 8;
            // The same for the item amount (#36).
            flags |= static_cast<uint64_t>(optItemAmount & 0x3) << 16;
            // Weather (#144). Bits 0-7 are the checkboxes above, 8-15 the
            // enemy count, 16-17 the item amount - so 18 is the next free
            // one. Whoever adds an option next takes bit 19.
            if (optWeather) flags |= 1ull << 18;

            // FNV-1a over seed text and selection, so both enter equally - a
            // mere XOR would have produced similar results for neighboring
            // flags.
            uint64_t h = SeedText::parse(seedBuf);
            for (int i = 0; i < 8; ++i)
            {
                h ^= (flags >> (i * 8)) & 0xFF;
                h *= 0x100000001B3ull;
            }

            // The wardrobe belongs in the seed just as the checkboxes do: the
            // same seed with only the catsuit left on is not the same game as
            // with everything. A std::set walks in sorted order, so the hash
            // does not depend on the order things were clicked.
            for (std::set<std::string>::const_iterator it = optOutfitsOff.begin();
                 it != optOutfitsOff.end(); ++it)
                for (size_t k = 0; k < it->size(); ++k)
                {
                    h ^= static_cast<unsigned char>((*it)[k]);
                    h *= 0x100000001B3ull;
                }

            /* The scan's own selection belongs in it just as much (#153,
               round M2), and it is the second set rather than more entries in
               the first because it is held the other way round - see
               optOutfitsOn. An EMPTY set changes nothing here, which is what
               makes a seed from before this round come out at the same number
               as long as nothing new is switched on. */
            for (std::set<std::string>::const_iterator it = optOutfitsOn.begin();
                 it != optOutfitsOn.end(); ++it)
                for (size_t k = 0; k < it->size(); ++k)
                {
                    h ^= static_cast<unsigned char>((*it)[k]);
                    h *= 0x100000001B3ull;
                }
            return h;
        }

        // --- Debug tool: fill sectors with markers ---
        //
        // Answers the question no number answers: does the boundary lie in
        // the RIGHT place? Looking into the level finds mistakes a statistic
        // does not show - that is how the leak from the ground floor into
        // the elevator shaft was found in rich2.
        bool dbgOpen = false;
        // Window "Enemy types" (#95): which kinds may the pool draw? What is
        // held is the DESELECTION - see Config::enemiesDisabled.
        bool kindsOpen = false;

        // Sliders for Balance.txt (#108). Held as a factor because the GUI
        // shows "1.25x"; the file holds percentages.
        float balWeapons        = 1.0f;
        float balEnemyHealth   = 1.0f;
        float balEnemyDamage = 1.0f;

        // Per slider: roll the value from the seed instead of setting it by
        // hand. The slider then shows what was rolled but can no longer be
        // dragged.
        bool balWeaponsRandom        = false;
        bool balEnemyHealthRandom   = false;
        bool balEnemyDamageRandom = false;

        std::string balNote;   // last feedback below the sliders

        // Measured intrinsic heights of the top-row boxes (#GUI). Needed to
        // bring all four columns to the same bottom edge: a box's height is
        // only known after it has been drawn, so it is remembered here from
        // one frame to the next. In the first frame all are 0 - the row then
        // stands uneven once, which nobody sees.
        struct BoxHeights
        {
            // Only boxes that are NEVER stretched - otherwise a feedback
            // loop arises, see drawUi.
            float install = 0, items = 0, enemies = 0, level = 0, music = 0;
        } heights;
        std::set<int> optEnemiesOff;
        // Window "Installation paths" (#102).
        bool pathsOpen = false;
        bool dbgUseFill = true;
        int  dbgFillType = 223;          // Pickup Item4 - rare in the game, stands out
        std::vector<std::string> dbgLog;

        std::wstring debugDir() const { return PathUtil::join(PathUtil::exeDir(), L"Debug Level"); }
    };

// Where does the wanted file really lie?
//
// The user picks the GAME FOLDER in the file dialog - that is the natural
// thing, and exactly what he did. The level files, however, lie in the
// subfolder "data". Instead of lecturing him with a hint, the tool searches
// both places. Returned is the folder that really contains the file, or
// empty.
//
// Case does not matter on Windows, so "Data" and "DATA" are covered too.
// Moved into Tr3Source (2026-08-30) so the command line gets the same
// answer - it had no such handling at all and refused paths this
// accepted.
static std::wstring resolveFolder(const std::wstring& chosen, const wchar_t* file)
{ return Tr3Source::resolveFolder(chosen, file); }

// The resolved folders as they go to the core and the patch path.
static std::wstring tr3Folder(const App& a)  { return resolveFolder(a.cfg.tr3Dir, L"CRASH.TR2"); }
static std::wstring goldFolder(const App& a) { return resolveFolder(a.cfg.tr3GoldDir, L"chunnel.TR2"); }

// Cache for the packages extracted from Tomb Raider 3 (#103).
//
// Lies next to the program. The files are created on the machine of whoever
// owns the games, and they stay there - unobjectionable license-wise. NEVER
// SHIP THIS FOLDER: it gets a warning text file so it stands out when
// packing.

static std::wstring cacheDir(const App& a)
{
    if (tr3Folder(a).empty()) return std::wstring();     // no source, no cache
    const std::wstring dir = PathUtil::join(PathUtil::exeDir(), L"Import-Export");
    if (!PathUtil::ensureDir(dir)) return std::wstring();

    const std::wstring note = PathUtil::join(dir, L"DO NOT REDISTRIBUTE.txt");
    if (!PathUtil::fileExists(note))
    {
        FILE* h = nullptr;
        if (_wfopen_s(&h, note.c_str(), L"wb") == 0 && h)
        {
            std::fputs(
                "This folder is a cache.\r\n\r\n"
                "It holds models, textures and sounds the program extracted from\r\n"
                "YOUR installation of Tomb Raider 3. They save time: without them,\r\n"
                "extraction happens anew for every level.\r\n\r\n"
                "YOU MUST NOT PASS THIS FOLDER ON. It contains data from a game\r\n"
                "everyone has to own themselves. When packing a redistribution,\r\n"
                "it goes out.\r\n\r\n"
                "Deleting it is safe at any time - it simply gets extracted again.\r\n", h);
            std::fclose(h);
        }
    }
    return dir;
}



/* THE MODS FOLDER FOLLOWS THE GAME FOLDER (#153, round M2).
 *
 * The engine looks for Mods beside its own executable; here the user names
 * the game folder, so Mods is beside that. Both scans are once-per-session by
 * design - the engine has no reason to run them twice - and this program lets
 * the user change the folder while it is open, so the two are forgotten when
 * it does. Nothing happens while the folder stays the same, which is every
 * frame but the first.
 *
 * The remasters themselves are hd_install's business and are found the way
 * the engine finds them: the registry value hd_install and Steam. */
static void wardrobeFollowsGameFolder(App& a)
{
    static std::wstring pointedAt;
    const std::wstring want = a.gameDir();

    if (want == pointedAt) return;

    pointedAt = want;
    HDModsFolder(PathUtil::narrow(PathUtil::join(want, L"Mods")).c_str());
    HDModsForget();
    HDWardrobeForget();
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

        // Without a loaded checksum list a check is pointless - it would
        // wave an empty list through. So abort here and name the real
        // reason instead of losing it during folder selection.
        if (!a.payloadOk) { setStatus(a, a.payloadError, true); return; }

        if (a.cfg.sourceDir.empty()) { setStatus(a, T("No game folder selected yet.")); return; }

        // If a backup already exists, THAT is the yardstick - not data\,
        // because already randomized levels can lie there. Otherwise a
        // second run would take a randomized level for the "original" and
        // the real original would be gone forever.
        a.haveBackup = PathUtil::dirExists(a.backupDir());
        const std::wstring master = a.haveBackup ? a.backupDir() : a.dataDir();

        auto st = a.data.inspect(master);
        int skipped = 0;
        for (const LevelStatus& s : st)
        {
            // Non-playable files are checked but not offered.
            if (!isRandomizable(s.name)) { ++skipped; continue; }

            LevelRow r;
            r.name = s.name;
            r.state = s.state;
            r.enabled = (s.state == LevelState::Vanilla);
            a.levels.push_back(r);
        }
        a.skippedNonPlayable = skipped;

        // Collect secret lists - here instead of at startup, so a freshly
        // added author appears without a restart. Careful with the
        // wording: inspectSource runs at startup, on a folder change and
        // on "Check again" - NOT after a run.
        a.secretSources = findSecretSources(PathUtil::join(a.payloadDir(), L"secrets"));

        // The complete list is still checked - an installation missing
        // Title.trc is no valid source, even though we never touch the file.
        std::string why;
        a.sourceOk = DataFolder::isUsableSource(st, why);

        if (a.sourceOk)
            setStatus(a, a.haveBackup
                ? T("Installation ready. The originals are in data\\Backup.")
                : T("Installation checked - all levels unmodified. data\\Backup "
                    "is created on the first randomize."));
        else
            setStatus(a, (a.haveBackup ? std::string(T("The backup is damaged: ")) : std::string()) + why, true);
    }

    // SCRIPT.DAT: take the young-Lara bit away from the Ireland levels.
    //
    // Without that, Lara runs around as a child in andy1, Andy2 and andy3
    // and cannot carry any weapon at all - GF_YOUNGLARA switches the weapon
    // system off in eight places in the engine. User requirement: the Andy
    // levels must always have weapons, or Lara faces a Two Gun with empty
    // hands.
    //
    // Unconditional and repeatable: clearing an already cleared bit changes
    // nothing, and the randomizer runs over the same installation several
    // times.
    //
    // Before the first write, the original is backed up next to it - the
    // same promise as with the levels: nothing is lost beyond recovery.
    bool doScriptDat(App& a, std::string& err, std::string& info)
    {
        const std::wstring script = a.scriptPath();
        if (!PathUtil::fileExists(script))
        {
            info = "SCRIPT.DAT not found - the Andy levels keep young Lara.";
            return true;                      // no reason to abort
        }

        const std::wstring backup = PathUtil::join(a.backupDir(), L"SCRIPT.DAT");
        if (PathUtil::dirExists(a.backupDir()) && !PathUtil::fileExists(backup))
            CopyFileW(script.c_str(), backup.c_str(), TRUE);

        // Always read from the backup if it exists: otherwise a second run
        // works on the result of the first.
        ScriptDat sd;
        const std::wstring source = PathUtil::fileExists(backup) ? backup : script;
        if (!sd.load(source, err)) return false;

        // The changes themselves live in ScriptDat - the CLI calls the same
        // code, so the two paths cannot drift apart.
        ScriptDat::Changes e;
        sd.randomizerChanges(e);

        if (e.levelsMissing > 0)
        {
            info = "SCRIPT.DAT: " + std::to_string(e.levelsMissing) + " Ireland levels not found in the script.";
            return true;
        }

        // Background music: roll only AFTER the mandatory edits, and only if
        // the box is checked. Because the read above always comes from the
        // backup, a run WITHOUT the box restores the original music.
        int musicLevels = 0;
        if (a.phMusicSwitch)
        {
            std::string mErr;
            if (!sd.shuffleMusic(a.effectiveSeed(), musicLevels, mErr))
                a.scriptNote = "Background music: " + mErr;
        }

        // Level sections: as the LAST edit, so all name lookups before it
        // ran on the known state (parse runs again afterwards).
        std::string sections, levelOrder;
        std::vector<std::string> chains;
        // With "free across everything" the section roll is meaningless -
        // and it must not run either, or the free roll would start from a
        // different base order and the same seed would yield two different
        // games depending on the checkbox.
        if (a.phLevelSections && a.optLevelOrder != 2)
        {
            std::string aErr;
            if (!sd.shuffleSections(a.effectiveSeed(), sections, aErr))
                a.scriptNote = "Level sections: " + aErr;
        }
        if (a.optLevelOrder > 0)
        {
            std::string lErr;
            if (!sd.shuffleLevels(a.effectiveSeed(), a.optLevelOrder == 2, levelOrder, lErr))
                a.scriptNote = "Level order: " + lErr;
        }
        // Always check the chains against the CURRENT order - in vanilla
        // order nothing happens, after a roll the script grants what is
        // missing.
        if (a.phLevelSections || a.optLevelOrder > 0)
        {
            std::string kErr;
            // FIRST clean up, THEN give: the takeaways must stand in the
            // block before the gifts, so the engine applies them in the
            // third phase (setup.cpp) after the takeaways.
            if (!sd.cleanupInventory(chains, kErr))
                a.scriptNote = "Inventory cleanup: " + kErr;
            else if (!sd.resolveChains(chains, kErr))
                a.scriptNote = "LevelChains: " + kErr;
        }

        if (!sd.save(script, err)) return false;

        info = e.flagsChanged > 0
            ? "SCRIPT.DAT: young Lara switched off in " + std::to_string(e.flagsChanged) + " Ireland levels."
            : "SCRIPT.DAT: young Lara was already switched off.";
        if (!e.keyError.empty())   info += "  KEY8 for joby3 not carried over: " + e.keyError;
        else if (e.keyCopied)      info += "  KEY8 for joby3 carried over from joby2.";
        if (!e.pistolError.empty()) info += "  rich2 pistols not removed: " + e.pistolError;
        else if (e.pistolsRemoved > 0)    info += "  rich2: pistols taken away.";
        if (e.joby3Weapons > 0)         info += "  joby3: " + std::to_string(e.joby3Weapons) + " weapon takeaways removed.";
        if (e.andrea1Uzi > 0)           info += "  Andrea1: Uzi takeaway removed.";
        if (a.phMusicSwitch && musicLevels > 0)
            info += "  Background music: " + std::to_string(musicLevels) + " levels rescored.";
        if (a.phLevelSections && !sections.empty())
            info += "  Sections: " + sections + ".";
        if (a.optLevelOrder > 0 && !levelOrder.empty())
            info += "  Order: " + levelOrder + ".";
        for (const std::string& k : chains)
            info += "  " + k + ".";
        return true;
    }

    // The zone export for one level. data\ beside the program wins, the copy
    // baked into the EXE is the floor - see EmbeddedData.h. Which of the two
    // it was lands in DataFile::log() and from there in spoiler.txt: a bug
    // report is worth little without knowing which curation actually ran.
    bool loadZones(const App& a, const std::string& base, JsonValue& zone, std::string& err)
    {
        std::string text;
        DataFile::Source src = DataFile::Source::External;
        if (!DataFile::read(a.payloadDir(), "zones_" + base + ".json", text, src, err)) return false;
        return Json::parseString(text, zone, err);
    }

    // Randomize one level. Exactly the same steps as Randomize.cpp.
    bool doLevel(App& a, LevelRow& row, uint64_t seed, std::string& err)
    {
        const std::string base = row.name.substr(0, row.name.find_last_of('.'));

        JsonValue zone;
        if (!loadZones(a, base, zone, err)) return false;

        // The level is opened BEFORE placement, not only for patching. Two
        // reasons now. The older one is the coupled trigger move: which
        // sectors can take a trigger stands only in the floordata, and
        // placement must know it before it chooses - otherwise it only shows
        // while patching, and by then the run is half done. The newer one is
        // that the level info itself comes from this file (LevelInfo.h), so
        // it has to be open before buildInput, not after.
        Tr5Level lvl;
        const std::wstring target = PathUtil::join(a.dataDir(), PathUtil::widen(row.name));
        if (!lvl.load(target, err)) return false;

        // Rooms, items and triggers, read out of the level instead of out of
        // a 3 MB level_<name>.json. Safe here because the caller restored
        // data\ from Backup first and only enabled levels whose checksum
        // matches vanilla - the info therefore describes the untouched level,
        // exactly as the old export did by construction.
        JsonValue info;
        if (!Rando::levelInfoFromLevel(lvl, info, err)) return false;

        RandoInput in;
        if (!Rando::buildInput(zone, info, in, err)) return false;

        // Repair Core's level bugs BEFORE anything is rolled (LevelFix.h).
        // Early, because placement reads the floordata: a trigger added only
        // afterwards would not have existed for the zone computation.
        // The report is created further down - collect until then.
        std::vector<std::string> fixNotes;
        {
            std::vector<int> freed;
            const int fixes = LevelFix::apply(lvl, base, fixNotes, &freed);
            if (fixes > 0)
                fixNotes.push_back(std::to_string(fixes) + " level fixes applied");

            // A rewritten pickup trigger no longer holds its item - but the
            // pin was decided from level_*.json, which still describes the
            // vanilla trigger. Same pattern as the secrets mode below.
            for (int i : freed)
                if (in.triggerPinned.erase(i))
                    fixNotes.push_back("item " + std::to_string(i) +
                                       " released - its pickup trigger was rewritten");
        }

        // Source levels for soundAdds and for the cross enemies' models:
        // always the backup, never data\ - already randomized levels can lie
        // there, and pulling samples from those would no longer be
        // reproducible from the seed. Defined BEFORE the placement, because
        // the headroom rule (#141) has to measure a cross enemy in its donor
        // file while it decides.
        static std::wstring soundSource;
        soundSource = a.haveBackup ? a.backupDir() : a.dataDir();
        auto source = [](const std::string& name, void*) -> std::wstring {
            return PathUtil::join(soundSource, PathUtil::widen(name) + L".trc");
        };

        Patch::markTriggerSafeSectors(lvl, in);
        in.modelHeight = Patch::modelHeightFn(source, nullptr,
                                              tr3Folder(a), goldFolder(a), cacheDir(a));

        // Secrets into the pool (#53, kind 1): hand curation pins every
        // secret via fixedItems. Exactly those pins are taken out in this
        // mode - type 223 then rolls as an ordinary pickup. Only the secret
        // pins fall, all other hand curations stay.
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
        opt.disabledEnemies = a.optEnemiesOff;
        opt.tr3Dir     = a.cfg.tr3Dir;
        opt.tr3GoldDir = a.cfg.tr3GoldDir;
        opt.randomOutfits = a.optOutfits;
        opt.tr3Outfits    = a.optOutfitsTr3;
        opt.outfitsOff    = a.optOutfitsOff;
        opt.outfitsOn     = a.optOutfitsOn;
        // The scan the roll draws from has to be the one for THIS game
        // folder (#153, round M2) - the user may have changed it since
        // the window was last open, or never have opened the window.
        wardrobeFollowsGameFolder(a);
        opt.allWeapons = true;              // standard, see the App comment
        // Item abundance: the GUI's three tiers as a count. Free placement
        // spots and the item limit cap them anyway.
        opt.extraPickups = (a.optItemAmount == 1) ? 12 : (a.optItemAmount == 2) ? 30 : 0;
        opt.randomWeather = a.optWeather;

        JsonValue patch;
        RandoReport rep;
        if (!Rando::run(in, opt, seed, patch, rep)) { err = rep.error; return false; }

        for (const std::string& n : fixNotes) rep.notes.push_back(n);

        PatchReport pr;
        // Pass the foreign installations through: only with them can apply()
        // fetch the guests from Tomb Raider 3 at runtime (#92). If they are
        // missing, the randomizer never put them into the pool.
        if (!Patch::apply(lvl, patch, pr, err, source, nullptr,
                          tr3Folder(a), goldFolder(a), cacheDir(a))) return false;
        for (const std::string& w : pr.warnings) rep.notes.push_back(w);

        // Secrets from a user list (#53, kinds 2/3): a SECOND pass with the
        // same apply machinery, against the real state after the first.
        // Without "itemBase" in the patch, apply shifts nothing - the
        // list's New<k> references point exactly at the items this patch
        // creates. A missing list is no abort (the level is done after
        // all), a FAILED patch is - nobody would ever find half-placed
        // secrets.
        int secretsPlaced = 0, extras = 0;
        if (a.optSecrets >= 2)
        {
            const char* cat = (a.optSecrets == 2) ? "Glitchless" : "Glitched";
            const SecretSource* list = nullptr;
            for (const SecretSource& q : a.secretSources)
                if (q.category == cat && q.author == a.secretAuthor) { list = &q; break; }

            if (!list)
                rep.notes.push_back(std::string("secrets: no ") + cat + " list chosen - level stays vanilla.");
            else
            {
                JsonValue sPatch;
                std::vector<std::string> sWarn;
                std::string sErr;
                int sCount = 0;
                if (!buildSecretPatch(list->path, base, lvl, sPatch, sCount, sWarn, sErr))
                { err = "secret list: " + sErr; return false; }
                for (const std::string& w : sWarn) rep.notes.push_back("secrets: " + w);

                PatchReport spr;
                if (!Patch::apply(lvl, sPatch, spr, err, source, nullptr,
                                  tr3Folder(a), goldFolder(a), cacheDir(a)))
                { err = "secret patch: " + err; return false; }
                for (const std::string& w : spr.warnings) rep.notes.push_back("secrets: " + w);
                // Count ONLY the relocated secrets. The report's
                // edited+added contains the extras (keys, switches,
                // retypings) - the status line reported "6 secrets" in a
                // level with three because of that (seen by the user).
                secretsPlaced = sCount;
                extras = (spr.edited + spr.added) - sCount;
            }
        }
        // Music triggers: as the last pass on the finished level, right
        // before saving - that way it also catches triggers the secret list
        // has just placed.
        if (a.phMusicTrigger)
        {
            const long mw = Patch::shuffleMusicTriggers(lvl, seed);
            if (mw > 0)
                rep.notes.push_back("music triggers: " + std::to_string(mw) + " words redistributed");
        }

        if (!lvl.save(target, err)) return false;

        // HD markers (#123, stage S7): the sidecar beside the finished
        // level. AFTER the save, and for EVERY level - one with nothing to
        // mark gets the version line alone, so a file left over from an
        // earlier run can never keep talking about rooms this run did not
        // touch.
        int hdRooms = 0, hdDonors = 0, hdCuts = 0;
        if (!Patch::writeHdMarkers(lvl, target, err, &hdRooms, &hdDonors, &hdCuts))
            return false;

        // 320 and not 200: with everything switched on the pieces below
        // already added up to just over 200, and snprintf returns the length
        // it WOULD have written - so "sizeof(buf) - len" turned negative and,
        // as a size_t, into a huge number. The weather line (#144) is the one
        // that pushed it over; the room is added here rather than juggling
        // the pieces. The HD markers (#123) got another 64.
        char buf[384];
        int len = std::snprintf(buf, sizeof(buf), "%d moved, %d new", pr.edited, pr.added);
        if (rep.enemiesAdded > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d enemies", rep.enemiesAdded);
        if (pr.triggersMoved > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d triggers along", pr.triggersMoved);
        if (pr.triggersMirrored > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d flipmap copies", pr.triggersMirrored);
        if (pr.triggersMoveMirrored > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d moved-trigger copies", pr.triggersMoveMirrored);
        if (pr.weaponsCopied > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d weapon slots", pr.weaponsCopied);
        if (rep.weaponsScattered > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d weapons scattered", rep.weaponsScattered);
        if (secretsPlaced > 0 || extras > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d secrets (+%d extras)", secretsPlaced, extras);
        if (rep.crossTypesUsed > 0)
            len += std::snprintf(buf + len, sizeof(buf) - len, ", %d cross types", rep.crossTypesUsed);
        if (!pr.outfitFrom.empty())
            len += std::snprintf(buf + len, sizeof(buf) - len, ", outfit %s", pr.outfitFrom.c_str());
        if (pr.weatherTo >= 0 && len >= 0 && len < static_cast<int>(sizeof(buf)))
        {
            static const char* kW[3] = { "dry", "rain", "snow" };
            const int was = (pr.weatherFrom >= 0 && pr.weatherFrom <= 2) ? pr.weatherFrom : 0;
            len += std::snprintf(buf + len, sizeof(buf) - static_cast<size_t>(len),
                                 ", weather: %s (vanilla %s)", kW[pr.weatherTo], kW[was]);
        }
        // The HD markers (#123), so the spoiler says what the sidecar beside
        // the level carries. Only when there is something to say - in a run
        // without secret lists and without a guest outfit that is nothing
        // but donor slots, and those are in the file for whoever wants them.
        if ((lvl.hdOutfit() > 0 || hdRooms > 0 || hdCuts > 0) &&
            len >= 0 && len < static_cast<int>(sizeof(buf)))
            std::snprintf(buf + len, sizeof(buf) - static_cast<size_t>(len),
                          ", HD: outfit %d, %d rooms, %d cuts, %d donors",
                          lvl.hdOutfit(), hdRooms, hdCuts, hdDonors);
        row.result = buf;
        return true;
    }

    // Build one debug level: instead of moving items, every valid sector is
    // covered with a marker. The basis is always the ORIGINAL - if a backup
    // exists, the file comes from there, else from data\. An already
    // randomized level as the template would be geometrically identical, but
    // the item list in the result would not be the one the user expects.
    //
    // Written exclusively to "Debug Level" next to the EXE. These files are
    // not playable (several thousand items), so they have no business in
    // data\.
    bool doDebugLevel(App& a, LevelRow& row, std::string& err)
    {
        const std::string base = row.name.substr(0, row.name.find_last_of('.'));

        JsonValue zone;
        if (!loadZones(a, base, zone, err)) return false;

        // The backup by preference, exactly as the comment above this function
        // demands: the markers must describe the untouched level. That matters
        // twice now, because the level info is read out of this same file.
        const std::wstring src = PathUtil::join(
            a.haveBackup ? a.backupDir() : a.dataDir(), PathUtil::widen(row.name));

        Tr5Level lvl;
        if (!lvl.load(src, err)) return false;
        lvl.allowMoreThanMaxItems(true);   // expressly wanted here, see Tr5Level.h

        JsonValue info;
        if (!Rando::levelInfoFromLevel(lvl, info, err)) return false;

        RandoInput in;
        if (!Rando::buildInput(zone, info, in, err)) return false;

        // The level is opened BEFORE the markers are built - exactly as in
        // the real run above, and for the same reason.
        //
        // markTriggerSafeSectors fills RandoInput::slopes from the
        // floordata. Without that map every sector answers "flat"
        // (Randomizer.cpp, floorAt: no entry means flat), so every rule
        // that hangs on the floor shape was blind here: the slide and
        // NoCol bans of #86 and, since 2026-08-29, the flatness rule for
        // the wall grabs. The debug level then showed sectors as valid
        // that the real placement rejects - which is the opposite of what
        // it is for.
        //
        // Found by the user in andy3: four markers on split sectors, his
        // example room 54, a diagonally split field with a marker on it.
        //
        // The level fixes belong here for the very same reason (second find,
        // the same evening): a rewritten pickup trigger changes the
        // floordata AND releases the item it used to pin. Without this the
        // debug level went on showing andy1's heart nailed down while the
        // real run had long let it go.
        {
            std::vector<std::string> dbgFixNotes;
            std::vector<int> dbgFreed;
            LevelFix::apply(lvl, base, dbgFixNotes, &dbgFreed);
            for (int i : dbgFreed) in.triggerPinned.erase(i);
        }

        Patch::markTriggerSafeSectors(lvl, in);

        JsonValue patch;
        std::vector<MarkerCount> perType;
        int fillSectors = 0;
        if (!Rando::buildMarkerPatch(in, a.dbgUseFill ? a.dbgFillType : 0, patch, perType, fillSectors))
        {
            err = "markers could not be generated.";
            return false;
        }

        PatchReport pr;
        if (!Patch::apply(lvl, patch, pr, err)) return false;

        if (!PathUtil::ensureDir(a.debugDir())) { err = "folder \"Debug Level\" could not be created."; return false; }
        if (!lvl.save(PathUtil::join(a.debugDir(), PathUtil::widen(row.name)), err)) return false;

        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d markers", pr.added);
        row.result = buf;

        std::snprintf(buf, sizeof(buf), "%s: %d sectors total%s",
                      row.name.c_str(), fillSectors,
                      a.dbgUseFill ? "" : " (fill layer off)");
        a.dbgLog.push_back(buf);
        for (const MarkerCount& m : perType)
        {
            std::snprintf(buf, sizeof(buf), "    type %d: %d sectors in %d rooms",
                          m.typeId, m.sectors, m.rooms);
            a.dbgLog.push_back(buf);
            // A type without a single allowed sector is no edge case but a
            // curation error - the run would fail on it later, and here it
            // stands out immediately.
            if (m.sectors == 0) a.dbgLog.push_back("    ^ NO allowed sector!");
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
        setStatus(a, T("Building debug levels..."));
    }

    // Creates data\Backup if it is missing. That may ONLY happen when the
    // levels in data\ are provably unmodified - a backup made from already
    // randomized files would be worthless without anyone noticing, and the
    // original lost beyond recovery. The checksum check here is therefore
    // not a convenience but the safeguard.
    bool ensureBackup(App& a, std::string& err)
    {
        if (PathUtil::dirExists(a.backupDir())) return true;

        auto st = a.data.inspect(a.dataDir());
        std::string why;
        if (!DataFolder::isUsableSource(st, why))
        {
            err = "There is no backup yet, and the levels in data are not in "
                  "their original state: " + why + " Please have Steam verify "
                  "the files and try again.";
            return false;
        }

        int copied = 0;
        if (!a.data.copyLevels(a.dataDir(), a.backupDir(), err, &copied)) return false;
        a.haveBackup = true;
        return true;
    }

    // Starts a run. The work itself happens in advanceRun(), so the GUI can
    // draw in between.
    //------------------------------------------------------------------------
    // Roll the checked balance sliders from the seed (#108).
    //
    // Rolled in steps of five from 0.50x to 2.00x - weapon damage too,
    // although its slider reaches down to 0.00x. A seed that puts Lara's
    // weapons at a tenth would not be hard, just unplayable; 0.00x therefore
    // belongs to manual tuning.
    //
    // ALL THREE are always rolled, the unchecked ones too. Otherwise the
    // weapon damage roll would depend on whether the enemy boxes next to it
    // are checked - the same seed would give different values depending on
    // what else is ticked.
    //------------------------------------------------------------------------
    void rollBalance(App& a)
    {
        if (!a.balWeaponsRandom && !a.balEnemyHealthRandom && !a.balEnemyDamageRandom)
            return;

        Rng r(a.effectiveSeed() ^ 0x42414C414E434521ull);   // "BALANCE!" - its own stream

        auto roll = [&r]() { return (50 + static_cast<int>(r.below(31)) * 5) / 100.0f; };

        const float weapons = roll();
        const float health  = roll();
        const float damage = roll();

        if (a.balWeaponsRandom)        a.balWeapons        = weapons;
        if (a.balEnemyHealthRandom)   a.balEnemyHealth   = health;
        if (a.balEnemyDamageRandom) a.balEnemyDamage = damage;
    }

    // Write the slider positions into the installation's Balance.txt.
    void writeBalanceOut(App& a)
    {
        BalanceFile::Values w;
        w.weaponDamage = static_cast<int>(a.balWeapons * 100.0f + 0.5f);
        w.enemyHealth   = static_cast<int>(a.balEnemyHealth * 100.0f + 0.5f);
        w.enemyDamage = static_cast<int>(a.balEnemyDamage * 100.0f + 0.5f);

        std::string error;
        if (BalanceFile::write(a.gameDir(), w, &error))
            a.balNote = T("Balance.txt written - takes effect on the next game start.");
        else
            a.balNote = error;
    }

    //------------------------------------------------------------------------
    // Write spoiler.txt next to the program.
    //
    // This used to live in a third table column per level. That cost half
    // the window width for one line of text read once - and the line was too
    // short for everything one really wants to know. The file has room for
    // the seed, all settings and what SCRIPT.DAT received.
    //------------------------------------------------------------------------
    bool writeSpoiler(App& a)
    {
        const std::wstring path = PathUtil::join(PathUtil::exeDir(), L"spoiler.txt");
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;

        auto line = [f](const char* s) { std::fprintf(f, "%s\r\n", s); };

        std::fprintf(f, "TR5 Randomizer %s - spoiler.txt\r\n", RANDO_VERSION_STRING);
        std::fprintf(f, "Build %s\r\n", RANDO_BUILD_STRING);
        line("");
        std::fprintf(f, "Seed:      %s\r\n", a.seedBuf);
        std::fprintf(f, "effective: 0x%016llX\r\n", (unsigned long long)a.effectiveSeed());
        line("");

        line("SETTINGS");
        std::fprintf(f, "  Equipment:          %s\r\n", a.optOtherPickups ? "shuffled" : "off");
        std::fprintf(f, "  Item amount:        %s\r\n",
                     a.optItemAmount == 0 ? "vanilla" : a.optItemAmount == 1 ? "more" : "a lot more");
        std::fprintf(f, "  Keys/puzzles:       %s\r\n", a.optKeyItems ? "shuffled" : "off");
        std::fprintf(f, "  Outfits:            %s%s\r\n",
                     a.optOutfits ? "shuffled" : "off",
                     (a.optOutfits && a.optOutfitsTr3) ? " (with Tomb Raider 3)" : "");
        std::fprintf(f, "  Extra enemies:      %s\r\n",
                     a.optEnemies ? (std::to_string(a.optEnemyCount) + " per level").c_str() : "off");
        std::fprintf(f, "  Enemy swap:         %s\r\n", a.optEnemySwap ? "on" : "off");
        std::fprintf(f, "  Enemy pool:         %s\r\n",
                     a.optEnemyPool == 0 ? "level pool only"
                                         : a.optEnemyPool == 1 ? "cross mix" : "allsanity");
        if (!a.optEnemiesOff.empty())
            std::fprintf(f, "  deselected kinds:   %d\r\n", (int)a.optEnemiesOff.size());
        std::fprintf(f, "  Secrets:            %s%s%s\r\n",
                     a.optSecrets == 0 ? "vanilla" : a.optSecrets == 1 ? "in the item pool"
                                                   : a.optSecrets == 2 ? "glitchless list"
                                                                       : "glitched list",
                     a.optSecrets >= 2 && !a.secretAuthor.empty() ? ", author " : "",
                     a.optSecrets >= 2 && !a.secretAuthor.empty() ? a.secretAuthor.c_str() : "");
        std::fprintf(f, "  Level sections:     %s\r\n", a.phLevelSections ? "shuffled" : "off");
        std::fprintf(f, "  Level order:        %s\r\n",
                     a.optLevelOrder == 0 ? "off"
                                          : a.optLevelOrder == 1 ? "within the sections"
                                                                 : "free across everything");
        std::fprintf(f, "  Weather:            %s\r\n", a.optWeather ? "shuffled" : "original");
        std::fprintf(f, "  Background music:   %s\r\n", a.phMusicSwitch ? "shuffled" : "off");
        std::fprintf(f, "  Trigger music:      %s\r\n", a.phMusicTrigger ? "shuffled" : "off");
        std::fprintf(f, "  Weapon damage:      %d %%%s\r\n",
                     (int)(a.balWeapons * 100.0f + 0.5f), a.balWeaponsRandom ? " (rolled)" : "");
        std::fprintf(f, "  Enemy health:       %d %%%s\r\n",
                     (int)(a.balEnemyHealth * 100.0f + 0.5f), a.balEnemyHealthRandom ? " (rolled)" : "");
        std::fprintf(f, "  Enemy damage:       %d %%%s\r\n",
                     (int)(a.balEnemyDamage * 100.0f + 0.5f), a.balEnemyDamageRandom ? " (rolled)" : "");
        line("");

        if (!a.scriptNote.empty())
        {
            line("SCRIPT.DAT");
            // The note is one long sentence with two spaces as separators -
            // one part per line reads much better.
            const std::string& s = a.scriptNote;
            size_t i = 0;
            while (i < s.size())
            {
                size_t e = s.find("  ", i);
                if (e == std::string::npos) e = s.size();
                std::string part = s.substr(i, e - i);
                while (!part.empty() && part.front() == ' ') part.erase(part.begin());
                if (!part.empty()) std::fprintf(f, "  %s\r\n", part.c_str());
                i = (e == s.size()) ? e : e + 2;
            }
            line("");
        }

        //--------------------------------------------------------------------
        // Where the precomputed data came from.
        //
        // Every zone export and the checksum list exist twice: baked into the
        // EXE, and possibly as a file in data\ that outranks it
        // (EmbeddedData.h). Two runs of the same seed give different results
        // if one of them found a hand-dropped export and the other did not -
        // and from the outside the two are indistinguishable. So the choice
        // goes on the record, one line per file. Repeated reads of the same
        // file collapse; the startup stamp check reads them all once before
        // the run does.
        //--------------------------------------------------------------------
        {
            line("DATA");
            std::set<std::string> seen;
            for (const std::string& l : DataFile::log())
                if (seen.insert(l).second) std::fprintf(f, "  %s\r\n", l.c_str());
            line("");
        }

        line("LEVELS");
        for (const LevelRow& r : a.levels)
        {
            const char* state = r.state == LevelState::Vanilla  ? "original"
                              : r.state == LevelState::Modified ? "modified"
                              : r.state == LevelState::Missing  ? "missing" : "unknown";
            std::fprintf(f, "  %-16s %-11s %s\r\n", r.name.c_str(), state,
                         r.result.empty() ? "-" : r.result.c_str());
        }
        line("");
        std::fprintf(f, "%d levels randomized, %d errors.\r\n", a.doneCount, a.failCount);

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

        // Roll the checked sliders anew from the seed and write the position
        // into the installation (#108). The sliders already write when
        // released; this is about the roll, and about a run in a freshly
        // chosen game folder not standing there without a Balance.txt.
        rollBalance(a);
        writeBalanceOut(a);

        a.phase = App::Phase::Copy;
        a.phaseText = "Restoring originals...";
        setStatus(a, T("Run started."));
    }

    void advanceRun(App& a)
    {
        switch (a.phase)
        {
        case App::Phase::Copy:
        {
            // Always copy from the backup, never build on an already
            // randomized level - otherwise the result would no longer be
            // reproducible from the seed, and that is exactly what the seed
            // exists for.
            std::string err;
            if (!ensureBackup(a, err)) { setStatus(a, err, true); a.phase = App::Phase::Idle; return; }

            int copied = 0;
            if (!a.data.copyLevels(a.backupDir(), a.dataDir(), err, &copied))
            {
                setStatus(a, T("Copy failed: ") + err, true);
                a.phase = App::Phase::Idle;
                return;
            }
            // SCRIPT.DAT exactly here: once per run, before the first level
            // is touched. If it fails, that is no reason to abort - the
            // levels are independent of it.
            {
                std::string sErr, sInfo;
                if (!doScriptDat(a, sErr, sInfo))
                    a.scriptNote = "SCRIPT.DAT: " + sErr;
                else
                    a.scriptNote = sInfo;
            }

            // And the secret rewards, for the same reason and in the same
            // place: one file for the whole run, keyed by level position
            // rather than by file name, because the level order is itself
            // rolled.
            if (a.optSecretRewards)
            {
                std::string rSum, rErr;
                if (!writeSecretRewards(a.gameDir(), SeedText::parse(a.seedBuf), rSum, rErr))
                    a.scriptNote += (a.scriptNote.empty() ? "" : "  |  ") + std::string("rewards: ") + rErr;
                else
                    a.scriptNote += (a.scriptNote.empty() ? "" : "  |  ") + rSum;
            }

            a.phase = App::Phase::Levels;
            a.stepIndex = 0;
            return;
        }

        case App::Phase::Levels:
        {
            // Skip non-editable rows, but do only one real step per frame.
            while (a.stepIndex < a.levels.size() && !a.levels[a.stepIndex].enabled)
            {
                a.levels[a.stepIndex].result = "skipped";
                ++a.stepIndex;
            }
            if (a.stepIndex >= a.levels.size()) { a.phase = App::Phase::Finish; return; }

            LevelRow& r = a.levels[a.stepIndex];
            a.phaseText = r.name;

            std::string e;
            if (doLevel(a, r, a.effectiveSeed(), e)) ++a.doneCount;
            else { ++a.failCount; r.result = "ERROR: " + e; }

            ++a.stepIndex;
            return;
        }

        case App::Phase::Debug:
        {
            while (a.stepIndex < a.levels.size() && !a.levels[a.stepIndex].enabled)
            {
                a.levels[a.stepIndex].result = "skipped";
                ++a.stepIndex;
            }
            if (a.stepIndex >= a.levels.size())
            {
                char buf[224];
                std::snprintf(buf, sizeof(buf),
                              T("%d debug levels written to \"Debug Level\"%s. For trview "
                                "only - the game will not start with this many items."),
                              a.doneCount, a.failCount ? T(" - with errors, see the list") : "");
                setStatus(a, buf, a.failCount > 0);
                a.phaseText.clear();
                a.phase = App::Phase::Idle;
                return;
            }

            LevelRow& r = a.levels[a.stepIndex];
            a.phaseText = r.name;

            std::string e;
            if (doDebugLevel(a, r, e)) ++a.doneCount;
            else { ++a.failCount; r.result = "ERROR: " + e; a.dbgLog.push_back(r.name + ": ERROR: " + e); }

            ++a.stepIndex;
            return;
        }

        case App::Phase::Finish:
        {
            a.cfg.seedText = a.seedBuf;
            a.cfg.save();

            const bool spoiler = writeSpoiler(a);

            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          T("%d levels randomized%s. %s You can start the game now."),
                          a.doneCount, a.failCount ? T(" - with errors, see spoiler.txt") : "",
                          spoiler ? T("Details are in spoiler.txt next to the program.")
                                  : T("spoiler.txt could not be written."));
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
            setStatus(a, T("There is no backup - nothing has ever been randomized, "
                           "so the levels are already in their original state."), true);
            return;
        }

        int copied = 0;
        if (!a.data.copyLevels(a.backupDir(), a.dataDir(), err, &copied))
            setStatus(a, T("Restore failed: ") + err, true);
        else
        {
            // The HD marker sidecars go with them (#123, stage S7). They
            // describe changes that no longer exist, and the engine reads a
            // missing file as "nothing to report" - which is exactly the
            // truth about a restored original. Levels are not RE-randomized
            // here, so nothing rewrites them; they have to be removed.
            // A file that is not there is not an error.
            for (const LevelRow& r : a.levels)
            {
                const size_t dot = r.name.find_last_of('.');
                const std::string stem = (dot == std::string::npos) ? r.name : r.name.substr(0, dot);
                DeleteFileW(PathUtil::join(a.dataDir(), PathUtil::widen(stem + ".hdm")).c_str());
            }

            for (LevelRow& r : a.levels) r.result.clear();
            char buf[128];
            std::snprintf(buf, sizeof(buf), T("%d levels restored to their original state."), copied);
            setStatus(a, buf);
        }
    }
}

//----------------------------------------------------------------------------
// Debug window: fill sectors with markers.
//
// What is produced here is not a level but a picture: on every sector where
// something may lie, a marker lies. The fill type shows the area for
// ordinary equipment; on top of it, per key type, lies its narrower area -
// the two OVERLAP. A sector with only the fill type thus allows equipment
// exclusively, a sector with both allows both.
//
// The check uses the same function that also places
// (Rando::sectorAllowedFor). That is the whole point: what is visible here
// is what the randomizer really does - not a second description of it.
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------
// Is the foreign installation behind an EnemyKind usable? (#102)
//
// Checked is not the folder but the file that is REALLY needed - a folder
// can exist and still be empty. CRASH.TR2 carries T-Rex, Raptor and Compy,
// chunnel.TR2 the vulture; MAIN.SFX supplies the sounds in both cases.
//----------------------------------------------------------------------------
static bool dinoSourcePresent(const App& a, const char* source)
{
    if (!source) return true;                 // lives in TR5 itself

    if (std::strcmp(source, "tr3") == 0)
        return !tr3Folder(a).empty()
            && !resolveFolder(a.cfg.tr3Dir, L"MAIN.SFX").empty();

    // The vulture needs BOTH installations: its model sits in chunnel.TR2 of
    // The Lost Artifact, but its sounds come from the MAIN.SFX of Tomb
    // Raider 3. The two files differ - Gold ships its own (9.8 MB against
    // 11.6 MB), and the audited wave numbers refer to TR3's.
    if (std::strcmp(source, "tr3gold") == 0)
        return !goldFolder(a).empty()
            && !resolveFolder(a.cfg.tr3Dir, L"MAIN.SFX").empty();

    return false;
}

//----------------------------------------------------------------------------
// Window "Installation paths" (#102).
//
// The dinosaurs come from Tomb Raider 3 and The Lost Artifact. We ship NO
// game data - whoever wants them must own the games. The run fetches model,
// textures and sounds from there at runtime.
//
// Without a valid path, the respective kind stays grayed out in the "Enemy
// types" window instead of failing mid-run.
//----------------------------------------------------------------------------
static void drawPathsWindow(App& a, HWND hwnd)
{
    if (!a.pathsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(620, 400), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(T("Installation paths")) + "###paths").c_str(), &a.pathsOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(T("The dinosaurs come from other games. This tool ships no game data - "
        "point it at those folders and the run fetches models, textures and "
        "sounds from there. Without a path the affected enemy types stay "
        "grayed out; everything else works as usual."));
    ImGui::Spacing();

    struct Entry { const char* title; const char* file; const char* forWhat;
                   std::wstring* path; const char* source; };
    Entry list[] = {
        { "Tomb Raider 5 (Chronicles)", nullptr, "the levels that get randomized",
          &a.cfg.sourceDir, nullptr },
        { "Tomb Raider 3",             "CRASH.TR2", "T-Rex, Raptor and Compy",
          &a.cfg.tr3Dir, "tr3" },
        { "Tomb Raider 3: The Lost Artifact", "chunnel.TR2", "the vulture",
          &a.cfg.tr3GoldDir, "tr3gold" },
    };

    for (Entry& e : list)
    {
        ImGui::PushID(e.title);
        ImGui::SeparatorText(e.title);
        ImGui::TextDisabled(T("for %s"), T(e.forWhat));

        const bool present = e.source ? dinoSourcePresent(a, e.source) : a.sourceOk;
        ImGui::TextWrapped(T("%s"), e.path->empty() ? T("(not selected)")
                                                    : PathUtil::narrow(*e.path).c_str());

        if (ImGui::Button(T("Choose folder...")))
        {
            std::wstring picked;
            if (pickFolder(hwnd, picked))
            {
                *e.path = picked;
                a.cfg.save();
                if (!e.source) inspectSource(a);
            }
        }
        if (!e.path->empty())
        {
            ImGui::SameLine();
            if (ImGui::Button(T("Clear"))) { e.path->clear(); a.cfg.save(); }
        }
        ImGui::SameLine();
        if (present) ImGui::TextDisabled(T("OK"));
        else if (e.file) ImGui::TextDisabled(T("%s not found"), e.file);
        else ImGui::TextDisabled(T("not checked yet"));

        ImGui::PopID();
    }

    ImGui::Spacing();
    // This used to read "In each case, choose the data folder" - which is
    // right for the two Tomb Raider 3 rows and WRONG for the first one.
    // Choosing data\ for Chronicles still finds the levels, so nothing
    // looks amiss, but SCRIPT.DAT is only looked for in the chosen folder
    // and one data\ below it - the whole script step then vanishes in
    // silence and shows up in the game as young Lara without weapons.
    ImGui::TextWrapped("%s", T("Either the game folder or its data folder - "
                              "each row takes both."));
    ImGui::End();
}

//----------------------------------------------------------------------------
// Window "Wardrobe" (#122): which outfits may the roll draw?
//
// Same idea as the enemy types window, and for the same two reasons: someone
// wants to see a favourite outfit more often, and a test wants exactly one -
// deselect everything but one and every level wears it.
//
// Held and saved is the DESELECTION. An outfit added later is thereby on by
// itself and does not fall silently through an old settings file.
//
// A guest outfit is greyed out while its game is not configured, or while
// the TR3 switch is off - but its checkbox STATE is kept either way, so a
// choice made now survives switching the source on later.
//----------------------------------------------------------------------------

static void drawWardrobeWindow(App& a)
{
    if (!a.wardrobeOpen) return;

    wardrobeFollowsGameFolder(a);

    ImGui::SetNextWindowSize(ImVec2(400, 340), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(T("Wardrobe")) + "###wardrobe").c_str(), &a.wardrobeOpen))
    {
        ImGui::End();
        return;
    }

    const std::vector<OutfitKind> kinds = Rando::outfitKinds();

    ImGui::TextWrapped(T("Which outfits may the random draw pick? A level whose own outfit "
        "is drawn simply keeps it - nothing is copied onto itself."));
    ImGui::Spacing();

    /* "All on" and "All off" act on the SEVEN, and on nothing the scan
       found (#153, round M2). A hundred outfits behind one button is not a
       choice anybody makes on purpose, and the whole point of the scanned
       entries being off by default is that they are picked one at a time. */
    if (ImGui::Button(T("All on")))  a.optOutfitsOff.clear();
    ImGui::SameLine();
    if (ImGui::Button(T("All off")))
        for (size_t k = 0; k < kinds.size(); ++k)
            if (!outfitIsScanned(kinds[k])) a.optOutfitsOff.insert(kinds[k].id);
    ImGui::SameLine();
    {
        int on = 0;
        for (size_t k = 0; k < kinds.size(); ++k)
        {
            if (outfitIsScanned(kinds[k]))
            {
                if (a.optOutfitsOn.count(kinds[k].id)) ++on;
                continue;
            }
            if (!a.optOutfitsOff.count(kinds[k].id)) ++on;
        }
        ImGui::TextDisabled(T("%d of %d on"), on, static_cast<int>(kinds.size()));
    }

    ImGui::Separator();
    ImGui::BeginChild("##outfitlist", ImVec2(0, -70), true);
    bool divided = false;
    for (size_t k = 0; k < kinds.size(); ++k)
    {
        /* THE TWO HALVES OF THE LIST (#153, round M2). Above the divider the
           seven the randomizer copies into the level file, held as a
           DESELECTION as they always were; below it everything the engine's
           own wardrobe scan found, held as a SELECTION and therefore off
           until somebody clicks it. The divider is drawn once, at the first
           scanned entry, so a machine with no remaster installed sees the
           window it saw before this round. */
        const bool scanned = outfitIsScanned(kinds[k]);

        if (scanned && !divided)
        {
            divided = true;
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextWrapped(T("From the Remasters and from the Mods folder. "
                "These are drawn by the engine itself - nothing is copied into "
                "the level file - and they need the HD art switched on."));
            ImGui::Spacing();
        }

        const bool guest   = !scanned && kinds[k].source[0] != 0;
        const bool present = scanned || !guest ||
                             (a.optOutfitsTr3 && !tr3Folder(a).empty());

        ImGui::BeginDisabled(!present);
        bool on = scanned ? a.optOutfitsOn.count(kinds[k].id) != 0
                          : (!a.optOutfitsOff.count(kinds[k].id) && present);
        char label[256];
        std::snprintf(label, sizeof(label), "%s##outfit%s", T(kinds[k].name), kinds[k].id);
        if (ImGui::Checkbox(label, &on))
        {
            if (scanned)
            {
                if (on) a.optOutfitsOn.insert(kinds[k].id);
                else    a.optOutfitsOn.erase(kinds[k].id);
            }
            else if (on) a.optOutfitsOff.erase(kinds[k].id);
            else         a.optOutfitsOff.insert(kinds[k].id);
        }
        ImGui::EndDisabled();

        if (guest && !present)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(T("- needs \"TR3 outfits\" and Tomb Raider 3"));
        }

        // The key, so that a run can be repeated from the command line and a
        // report line can be read back to an entry.
        if (scanned)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", kinds[k].id);
        }
    }
    ImGui::EndChild();

    // Everything off does not mean "no outfits": every level then simply
    // keeps its own. A valid state, but not one anyone wants by accident.
    {
        size_t fixed = 0;
        for (size_t k = 0; k < kinds.size(); ++k)
            if (!outfitIsScanned(kinds[k])) ++fixed;

        if (a.optOutfitsOff.size() >= fixed && a.optOutfitsOn.empty())
            ImGui::TextWrapped(T("Everything deselected: every level keeps its own outfit."));
    }

    ImGui::End();
}

//----------------------------------------------------------------------------
// Window "Enemy types" (#95): which kinds may the pool draw at all?
//
// Good for the player who wants to see favorite enemies more often, and good
// for testing a single kind - deselect everything but one, and the run only
// places that one.
//
// Held and saved is the DESELECTION, not the selection. A kind added later
// is thereby automatically on and does not fall silently through an old
// settings file.
//
// The checkbox trims EXCLUSIVELY the draw from the pool. Whatever a chosen
// kind pulls in technically - its anim base type, mandatory companions like
// the Hydra's projectile core, the MESHSWAP halves - never sees it.
// Otherwise an enemy would stand in the level without its animation set.
//----------------------------------------------------------------------------
static void drawEnemyKindsWindow(App& a)
{
    if (!a.kindsOpen) return;

    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(T("Enemy types")) + "###kinds").c_str(), &a.kindsOpen))
    {
        ImGui::End();
        return;
    }

    const std::vector<EnemyKind> kinds = Rando::enemyKinds();

    ImGui::TextWrapped(T("Which types may the random draw pick? The checkbox affects swapping "
        "and additional enemies - enemies that are in the level anyway and "
        "are not swapped remain untouched."));
    ImGui::Spacing();
    // Some kinds share an animation set under the hood or need an invisible
    // helper object (the Hydra's projectile core, say). That is technology,
    // not a player decision - the run fetches it itself no matter how the
    // boxes stand. This line only says THAT it happens, so nobody wonders
    // why a deselected guard still appears in the file.
    ImGui::TextDisabled(T("Whatever a type needs technically, the randomizer loads on its own."));
    ImGui::Spacing();

    if (ImGui::Button(T("All on")))  a.optEnemiesOff.clear();
    ImGui::SameLine();
    if (ImGui::Button(T("All off")))
        for (const EnemyKind& g : kinds) a.optEnemiesOff.insert(g.typeId);
    ImGui::SameLine();
    ImGui::TextDisabled(T("%d of %d on"),
                        static_cast<int>(kinds.size() - a.optEnemiesOff.size()),
                        static_cast<int>(kinds.size()));

    ImGui::Separator();
    ImGui::BeginChild("##kindlist", ImVec2(0, -78), true);
    for (const EnemyKind& g : kinds)
    {
        // Kinds from another game stay grayed out as long as the path to it
        // is missing (#102). The checkbox state is kept - if the user adds
        // the path later, his selection is still there.
        const bool present = dinoSourcePresent(a, g.source);
        ImGui::BeginDisabled(!present);

        bool on = !a.optEnemiesOff.count(g.typeId) && present;
        char label[96];
        std::snprintf(label, sizeof(label), "%s##kind%d", T(g.name), g.typeId);
        if (ImGui::Checkbox(label, &on))
        {
            if (on) a.optEnemiesOff.erase(g.typeId);
            else    a.optEnemiesOff.insert(g.typeId);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(T("(%d)"), g.typeId);
        ImGui::EndDisabled();

        if (!present)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(T("- needs %s"),
                std::strcmp(g.source, "tr3gold") == 0 ? "The Lost Artifact" : "Tomb Raider 3");
        }
    }
    ImGui::EndChild();

    // Everything deselected does not mean "no enemies": the cross pool then
    // comes up empty, and every level rolls only from its native kinds.
    // That is a valid state, but not one anyone wants by accident - hence
    // it is stated here rather than done silently.
    if (a.optEnemiesOff.size() >= kinds.size())
        ImGui::TextWrapped(T("All types deselected: every level keeps its own enemies, "
                             "no foreign ones are added."));
    else if (!a.optEnemiesOff.empty())
        ImGui::TextWrapped(T("If you deselect a lot, some levels have hardly any foreign "
                             "types left to draw from - they will stay closer to the original."));

    ImGui::End();
}

static void drawDebugWindow(App& a)
{
    if (!a.dbgOpen) return;

    ImGui::SetNextWindowSize(ImVec2(560, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin((std::string(T("Debug: sector markers")) + "###dbgmarker").c_str(), &a.dbgOpen))
    {
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(T("Builds a copy of every level checked above in which every allowed "
        "sector carries a marker. View them in trview - with several thousand "
        "items the game will NOT start these files. Your installation stays "
        "untouched; everything is written to \"Debug Level\" next to the program."));

    ImGui::Separator();
    ImGui::Checkbox(T("Fill layer: mark every valid sector"), &a.dbgUseFill);
    ImGui::BeginDisabled(!a.dbgUseFill);
    ImGui::SetNextItemWidth(120);
    ImGui::InputInt(T("Fill layer type"), &a.dbgFillType);
    if (a.dbgFillType < 0) a.dbgFillType = 0;
    ImGui::EndDisabled();
    ImGui::TextDisabled(T("223 = Pickup Item4. Rare in the game, stands out immediately in the item list."));

    const bool busy = (a.phase != App::Phase::Idle);
    ImGui::Separator();
    ImGui::BeginDisabled(!a.sourceOk || busy);
    if (ImGui::Button(T("Build debug levels"), ImVec2(200, 30))) startDebugMarkers(a);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!PathUtil::dirExists(a.debugDir()));
    if (ImGui::Button(T("Open folder"), ImVec2(150, 30)))
        ShellExecuteW(nullptr, L"open", a.debugDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::EndDisabled();

    if (!a.sourceOk)
        ImGui::TextDisabled(T("(choose an installation above first)"));

    ImGui::Separator();
    ImGui::TextUnformatted(T("Result per type:"));
    if (ImGui::BeginChild("dbglog", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        for (const std::string& l : a.dbgLog)
        {
            if (l.find("NO allowed") != std::string::npos)
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), T("%s"), l.c_str());
            else if (l.find("ERROR") != std::string::npos)
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), T("%s"), l.c_str());
            else
                ImGui::TextUnformatted(l.c_str());
        }
        // Scroll along during the run, or only the beginning is ever seen.
        if (a.phase == App::Phase::Debug) ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();

    ImGui::End();
}

//----------------------------------------------------------------------------
// The GUI
//----------------------------------------------------------------------------
static void drawUi(App& a, HWND hwnd)
{
    // Checked balance sliders always show what the CURRENT seed gives -
    // hence rolled anew every frame instead of only on a run. The roll is a
    // handful of arithmetic steps and touches no file; it is only written on
    // checking, on reset and on a run.
    rollBalance(a);

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    // Transparent, so the background image can be seen at all - it lies on
    // the BackgroundDrawList, i.e. BEHIND the windows. Legibility comes from
    // the dark base color and the weak tint of the image, not from a surface
    // above it.
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("TR5 Randomizer", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::TextUnformatted(T("TR5 Randomizer"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", RANDO_VERSION_STRING);
    ImGui::SameLine();
    ImGui::TextDisabled(T("Build %s"), RANDO_BUILD_STRING);

    // Top right, because it is no operating step: the numbered sections
    // below are the way through the program, this stands beside it.
    {
        const float w = 110.0f;
        // GetContentRegionMax is gone in the docking branch (imconfig with
        // IMGUI_DISABLE_OBSOLETE_FUNCTIONS); cursor X at line start plus
        // available width yields the same right edge.
        // Two buttons: language on the left (#128), Debug on the right.
        ImGui::SameLine(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x
                        - 2.0f * w - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(gGermanUi ? "Sprache: DE###language" : "Language: EN###language", ImVec2(w, 0)))
        {
            gGermanUi = !gGermanUi;
            a.cfg.germanUi = gGermanUi;
            a.cfg.save();
        }
        ImGui::SameLine();
        if (ImGui::Button(T("Debug"), ImVec2(w, 0))) ImGui::OpenPopup("##debugmenu");
        if (ImGui::BeginPopup("##debugmenu"))
        {
            if (ImGui::MenuItem(T("Sector markers..."))) a.dbgOpen = true;
            ImGui::Separator();
            if (ImGui::MenuItem(T("Open \"Debug Level\" folder"),
                                nullptr, false, PathUtil::dirExists(a.debugDir())))
                ShellExecuteW(nullptr, L"open", a.debugDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::EndPopup();
        }
    }
    ImGui::Separator();

    //------------------------------------------------------------------------
    // Layout of the upper area (user wish): four equally wide columns with
    // bordered boxes instead of one long column. Before, the separators ran
    // across the full window width and the content stood as a narrow band on
    // the far left - on a wide screen that was a lot of distance for little
    // content.
    //
    // The split is chosen so the four columns come out roughly equally
    // tall: installation+seed, items+outfits, enemies+preparation,
    // levels+music+secrets.
    //------------------------------------------------------------------------
    const float gapX = ImGui::GetStyle().ItemSpacing.x;
    const float columnWidth = (ImGui::GetContentRegionAvail().x - 3.0f * gapX) / 4.0f;

    // Each area gets its own color (user wish), so the boxes stand apart
    // from each other. The surface stays very weakly tinted - it should mark
    // the area and lift the text off the background image, not stand out
    // itself. Border and heading carry the same color more strongly.
    // height == 0 means self-growing. Only the boxes that stretch a column
    // to the common bottom edge get a fixed height.
    auto openBlock = [columnWidth](const char* id, const char* title, ImVec4 color,
                                   float height = 0.0f)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(color.x, color.y, color.z, 0.13f));
        ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(color.x, color.y, color.z, 0.75f));

        ImGuiChildFlags flags = ImGuiChildFlags_Borders;
        if (height <= 0.0f) flags |= ImGuiChildFlags_AutoResizeY;

        ImGui::BeginChild(id, ImVec2(columnWidth, height), flags);

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.x, color.y, color.z, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(color.x, color.y, color.z, 0.6f));
        ImGui::SeparatorText(T(title));
        ImGui::PopStyleColor(2);
    };
    // Closes a box and remembers its measured height.
    auto closeBlock = [](float* height = nullptr)
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        if (height) *height = ImGui::GetItemRectSize().y;
    };

    //------------------------------------------------------------------------
    // Common bottom edge of the top row.
    //
    // The enemy box sets the target height (user wish: "everything to the
    // size of the enemy block"). Stretched are the LAST boxes of the other
    // columns - seed, outfits, secrets - and via a FIXED height.
    //
    // What matters is what does NOT happen here: measured are exclusively
    // boxes that are themselves never stretched. A first attempt appended
    // the stretch as blank space inside the box and computed the intrinsic
    // height back as "measured minus blank" - that produced a feedback
    // loop, because ImGui always takes a self-growing child window's size
    // from the PREVIOUS frame. The computation thereby mixed two frames,
    // the error entered the target height, and the boxes grew frame by
    // frame into nothing (seen by the user). Fixed height for the
    // stretched, measure only the free ones - then nothing can feed back.
    //------------------------------------------------------------------------
    const float gapY = ImGui::GetStyle().ItemSpacing.y;
    const float targetHeight = a.heights.enemies;    // 0 in the very first frame

    auto remainingHeight = [targetHeight](float taken) -> float
    {
        if (targetHeight <= 0.0f) return 0.0f;  // nothing measured yet
        const float r = targetHeight - taken;
        // Too little room? Then better self-growing than cut off.
        return r > 60.0f ? r : 0.0f;
    };

    // The color chart in one place, so the areas match each other.
    const ImVec4 fInstall (0.45f, 0.70f, 1.00f, 1.0f);   // blue
    const ImVec4 fSeed    (0.40f, 0.85f, 0.80f, 1.0f);   // teal
    const ImVec4 fItems   (0.55f, 0.85f, 0.45f, 1.0f);   // green
    const ImVec4 fOutfits (0.80f, 0.55f, 0.95f, 1.0f);   // violet
    const ImVec4 fEnemies (1.00f, 0.50f, 0.40f, 1.0f);   // red
    const ImVec4 fPrep    (0.60f, 0.60f, 0.65f, 1.0f);   // gray, because inert
    const ImVec4 fLevel   (1.00f, 0.80f, 0.35f, 1.0f);   // gold
    const ImVec4 fMusic   (0.50f, 0.75f, 1.00f, 1.0f);   // light blue
    const ImVec4 fSecrets (1.00f, 0.60f, 0.80f, 1.0f);   // pink
    const ImVec4 fList    (0.75f, 0.78f, 0.85f, 1.0f);   // neutral
    const ImVec4 fStart   (0.55f, 0.90f, 0.60f, 1.0f);   // green

    // --- Column 1: installation and seed ---------------------------------
    ImGui::BeginGroup();

    // --- 1. Game folder ---
    // The curation-stamp warning at the very top and across all columns: it
    // affects the result of EVERY run, not a single area.
    if (!a.zoneWarning.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, .45f, .30f, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(1.0f, .45f, .30f, 0.85f));
        ImGui::BeginChild("k_zonewarning", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::TextColored(ImVec4(1, .55f, .35f, 1), T("Warning: outdated curation data"));
        ImGui::TextWrapped(T("%s"), a.zoneWarning.c_str());
        ImGui::TextDisabled(T("To fix: re-export ZoneAnalysis.ps1 for ALL levels into data\\."));
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
    }

    openBlock("k_install", "1. Your installation", fInstall);
    ImGui::TextWrapped(T("Choose the folder of your Tomb Raider Chronicles installation. "
                         "The levels are randomized right there; the program first backs up "
                         "the originals to data\\Backup."));

    std::string shown = a.cfg.sourceDir.empty() ? std::string(T("(not selected)"))
                                                : PathUtil::narrow(a.cfg.sourceDir);
    ImGui::TextWrapped(T("%s"), shown.c_str());

    // On the very first start there is nothing to check and nothing to
    // press: the window comes up with "(not selected)" and a player who
    // does not read section 1 sits in front of a program that appears to
    // do nothing. README.txt has been promising this prompt for a while.
    // Once only - after this the dialog is the button, not a greeting.
    static bool askedOnce = false;
    if (!askedOnce && a.cfg.sourceDir.empty())
    {
        askedOnce = true;
        a.pathsOpen = true;      // the window, not a bare folder dialog:
    }                            // it shows all three rows and what each is for


    // "Choose folder..." used to sit here as well and did exactly what the
    // first row of the paths window does. One place for paths - and that
    // window is where Tomb Raider 1, 2 and 4 will be added.
    if (ImGui::Button(T("Installation paths..."))) a.pathsOpen = true;
    ImGui::SameLine();
    if (ImGui::Button(T("Check again"))) inspectSource(a);
    // The option list's hover help is not available up here - it is only
    // created further down. A plain tooltip does the job.
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip(T("Optional: folders of Tomb Raider 3 and The Lost Artifact.\n"
                            "Only with these can the dinosaurs be used -\n"
                            "we ship no game data."));
    {
        const int missing = (dinoSourcePresent(a, "tr3") ? 0 : 1) + (dinoSourcePresent(a, "tr3gold") ? 0 : 1);
        ImGui::SameLine();
        if (missing == 0) ImGui::TextDisabled(T("TR3 + LA found"));
        else              ImGui::TextDisabled(T("%d without a path"), missing);
    }

    closeBlock(&a.heights.install);

    // --- 2. Seed ---
    openBlock("k_seed", "2. Seed", fSeed, remainingHeight(a.heights.install + gapY));
    ImGui::TextWrapped(T("The same seed always produces the same game. Any input is valid - "
                         "even a word."));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
    ImGui::InputText("##seed", a.seedBuf, sizeof(a.seedBuf));
    ImGui::SameLine();
    if (ImGui::Button(T("Roll")))
    {
        const std::string s = SeedText::format(SeedText::random());
        std::snprintf(a.seedBuf, sizeof(a.seedBuf), "%s", s.c_str());
    }
    ImGui::TextDisabled(T("effective: 0x%016llX"), (unsigned long long)a.effectiveSeed());
    closeBlock();

    ImGui::EndGroup();
    ImGui::SameLine();

    // --- 3. What should be rolled - thematic areas -----------------------

    // The explanations hang on the checkbox as a floating box: lingering
    // with the mouse fades them in, moving away fades them out (user wish -
    // the list stays compact, the texts are not lost). AllowWhenDisabled so
    // the grayed-out cross enemy mix shows its explanation too.
    auto hoverHelp = [](const char* text)
    {
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
            ImGui::TextUnformatted(T(text));
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    // --- Sliders for Balance.txt (#108) ----------------------------------
    //
    // Written only when the slider is RELEASED - while dragging, the loop
    // runs at 60 frames per second, which would be 60 writes into the user's
    // installation per second.
    //
    // Only the three percentage lines are touched. The damage per weapon,
    // the hit points per enemy and the damage per attack stay, so hand work
    // in the file survives a slider movement.
    auto writeBalance = [&a]() { writeBalanceOut(a); };

    auto balanceSlider = [&](const char* label, float* value, bool* random,
                             float from, float to, const char* help)
    {
        // Language (#128): translate once at the entrance, then the whole
        // slider works with the visible text.
        label = T(label);
        help = T(help);
        // Two lines instead of one: label with checkbox on top, the slider
        // below it across the full box width. In one line, slider, name,
        // percentage and checkbox no longer fit side by side in a quarter
        // column and were cut off on the right.
        char tag[64];
        std::snprintf(tag, sizeof(tag), "%s##%s", T("Random"), label);

        ImGui::TextUnformatted(label);
        hoverHelp(help);
        ImGui::SameLine();

        // Right-align the checkbox so the three sliders form a line among
        // themselves instead of a staircase.
        {
            const float width = ImGui::CalcTextSize(T("Random")).x
                              + ImGui::GetFrameHeight()
                              + ImGui::GetStyle().ItemInnerSpacing.x;
            const float target = ImGui::GetContentRegionAvail().x - width;
            if (target > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + target);
        }

        if (ImGui::Checkbox(tag, random))
        {
            // Roll immediately, so the slider does not stay on the old
            // value and the user sees right away what the seed gives.
            rollBalance(a);
            writeBalance();
        }
        hoverHelp("Instead of setting it by hand: the seed rolls the value, in "
                  "steps of five between 0.50x and 2.00x. The same seed always "
                  "gives the same value, no matter whether the other two boxes "
                  "are checked.\n\n"
                  "The slider immediately shows what the current seed gives. The "
                  "roll goes into Balance.txt when you check the box and on the "
                  "next run.\n\n"
                  "Weapon damage too is only rolled from 0.50x upward - a seed "
                  "that puts Lara's weapons at a tenth would not be hard, just "
                  "unplayable. 0.00x is reserved for manual tuning.");

        // The slider carries its value as its own label - a separate
        // percentage line next to it is no longer needed.
        char display[32];
        std::snprintf(display, sizeof(display), "%%.2fx  (%d %%%%)",
                      static_cast<int>(*value * 100.0f + 0.5f));

        char sliderId[64];
        std::snprintf(sliderId, sizeof(sliderId), "##slider_%s", label);

        ImGui::BeginDisabled(*random);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::SliderFloat(sliderId, value, from, to, display, ImGuiSliderFlags_AlwaysClamp);
        ImGui::EndDisabled();
        hoverHelp(help);

        if (ImGui::IsItemDeactivatedAfterEdit())
            writeBalance();
    };

    // --- Column 2: items and outfits -------------------------------------
    ImGui::BeginGroup();

    openBlock("k_items", "3. Items", fItems);

    ImGui::Checkbox(T("Equipment (weapons, ammo, medipacks)"), &a.optOtherPickups);
    // Item abundance (#36): scatter additional consumables. Less than
    // vanilla deliberately does not exist - removing items would shift all
    // following indices and break every "Object N" reference of the
    // triggers.
    ImGui::RadioButton(T("Vanilla amount"), &a.optItemAmount, 0);
    hoverHelp("The level keeps exactly as much equipment as Core intended - "
              "just in different places.");
    ImGui::SameLine();
    ImGui::RadioButton(T("More"), &a.optItemAmount, 1);
    hoverHelp("Scatters up to twelve additional consumables per level. Only "
              "what the level already knows is placed - ammo, medipacks, "
              "flares -, weighted by frequency: Andy2 (young Lara, no weapons) "
              "gets medipacks, a weapon level gets ammo. Keys, puzzle pieces "
              "and secrets are left out.");
    ImGui::SameLine();
    ImGui::RadioButton(T("A lot more"), &a.optItemAmount, 2);
    hoverHelp("Like \"More items\", but up to thirty per level. The limit is "
              "reached when the free placement spots run out or the engine's "
              "item limit (512) draws near - both cap the number on their own.");

    ImGui::Checkbox(T("Keys & puzzles"), &a.optKeyItems);

    // No checkbox anymore: the full weapon set is standard (user decision).
    ImGui::TextDisabled(T("All weapons everywhere is standard and always included."));
    hoverHelp("Every level gets the full weapon set on every run: pistols, Uzi, "
              "shotgun, Desert Eagle (replaces the revolver, in the joby levels "
              "by name too) and HK, ammo included. A level keeps its own "
              "working weapon; joby4 stays vanilla, and rich2 gets no pistols "
              "(its pistol slot carries sequence material from Core). The seed "
              "additionally scatters some of the new weapons with ammo into the "
              "level as pickups. The models come from the player's own level "
              "files at runtime.");

    if (!a.optOtherPickups && !a.optKeyItems && !a.optEnemies && !a.optEnemySwap && !a.optOutfits)
        ImGui::TextColored(ImVec4(1, .5f, .3f, 1), T("No shuffle option selected - the levels only get the standard weapon set."));

    balanceSlider("Weapon damage", &a.balWeapons, &a.balWeaponsRandom, 0.0f, 2.0f,
                  "Affects all of Lara's weapons at once - pistols, Uzi, shotgun, "
                  "revolver, HK, grappling gun and torch. 0.00x makes them "
                  "harmless, 2.00x doubles their damage.\n\n"
                  "This writes the WeaponDamage line in Balance.txt next to "
                  "PCtomb5.exe. If you tuned individual weapons there by hand, "
                  "they are kept: the slider applies on top.");

    closeBlock(&a.heights.items);

    openBlock("k_outfits", "4. Outfits", fOutfits, remainingHeight(a.heights.items + gapY));

    ImGui::Checkbox(T("Random outfits"), &a.optOutfits);
    hoverHelp("Every level randomly gets Lara_Andrea, Lara_Joby or Lara_Rich "
              "(catsuit). Only skin and head are carried over (slots 8/9/10) "
              "plus the outfit byte - never slot 0, which carries the "
              "animations. joby4 keeps the diving suit; the andy levels always "
              "get an adult Lara (young Lara cannot carry weapons).");

    // TR3 outfits are real (#122). They need the player's own Tomb Raider 3
    // installation - no game data of ours travels, the model is fetched at
    // runtime, exactly as with the four animals.
    const bool tr3Ready = !tr3Folder(a).empty();
    ImGui::BeginDisabled(!a.optOutfits || !tr3Ready);
    ImGui::Checkbox(T("TR3 outfits"), &a.optOutfitsTr3);
    ImGui::EndDisabled();
    hoverHelp(tr3Ready
        ? "Puts Lara's outfits from Tomb Raider 3 into the pool. Only the body "
          "travels - head, braid, holsters and the speech heads stay the "
          "level's own, which is why they keep working. The model is read from "
          "your own Tomb Raider 3 at runtime; nothing of it is shipped here."
        : "Needs your Tomb Raider 3 installation. Set it under "
          "\"Installation paths...\" in section 1 - the model is read from there "
          "data.");

    ImGui::BeginDisabled(!a.optOutfits);
    if (ImGui::Button(T("Wardrobe..."))) a.wardrobeOpen = true;
    ImGui::EndDisabled();
    hoverHelp("Which outfits the random draw may pick. Everything is on by "
              "default.");

    // Still placeholders: TR1 and TR2 would need a donor level each, and
    // those games' Lara is built differently again.
    ImGui::BeginDisabled(true);
    ImGui::Checkbox(T("TR1 outfit"), &a.phOutfitTr1);
    hoverHelp("Coming later: the classic TR1 outfit as an additional choice in "
              "the outfit pool.");
    ImGui::Checkbox(T("TR2 outfits"), &a.phOutfitTr2);
    hoverHelp("Coming later: the TR2 outfits (bomber jacket, wetsuit and more) "
              "as additional pool choices.");
    ImGui::EndDisabled();
    closeBlock();

    ImGui::EndGroup();
    ImGui::SameLine();

    // --- Column 3: enemies and the placeholders --------------------------
    ImGui::BeginGroup();

    openBlock("k_enemies", "5. Enemies", fEnemies);

    ImGui::Checkbox(T("Add extra enemies to the level"), &a.optEnemies);
    hoverHelp("Each one gets its own trigger, about three sectors ahead on "
              "Lara's path - never on its own tile, or it would spawn inside "
              "her. Which enemy types a level can take is hand-curated; if the "
              "list is empty, the checkbox has no effect.");
    ImGui::Indent();
    ImGui::BeginDisabled(!a.optEnemies);
    ImGui::SetNextItemWidth(160.0f);
    ImGui::SliderInt(T("per level"), &a.optEnemyCount, 1, 20);
    ImGui::EndDisabled();
    ImGui::Unindent();

    // Formerly "vanilla enemy mix" - the name sounded like the pool tier
    // "level pool only" and caused confusion (user question). The checkbox
    // says WHETHER existing enemies swap roles; the pool choice below says
    // WHAT swap and extra enemies roll from.
    ImGui::Checkbox(T("Swap existing enemies"), &a.optEnemySwap);
    hoverHelp("The enemies already in the level swap roles. Only the type "
              "changes, the item index stays - every enemy still wakes up "
              "where its trigger intends. Count and distribution stay the "
              "same, scripted enemies remain untouched. Where the new roles "
              "come from is decided by the enemy pool below.");

    // What do swap and extra enemies roll from? Three tiers. Grayed out as
    // long as neither action is active - without them the choice has no
    // effect.
    ImGui::BeginDisabled(!a.optEnemies && !a.optEnemySwap);
    ImGui::TextUnformatted(T("Enemy pool:"));
    ImGui::RadioButton(T("Level pool only"), &a.optEnemyPool, 0);
    hoverHelp("Only what the level itself knows is swapped and placed "
              "(the hand-curated list per level).");
    ImGui::RadioButton(T("Cross-Mix"), &a.optEnemyPool, 1);
    hoverHelp("Up to THREE foreign types from other levels join the pool - "
              "SWAT, guards, mafia, Larson, Pierre, lion, gladiator, statue, "
              "maze monster, imp and more. When swapping, roughly every fourth "
              "role goes to one of them, so the level stays recognizable. "
              "Models and sounds travel along from your own level files at "
              "runtime.");
    ImGui::RadioButton(T("Allsanity"), &a.optEnemyPool, 2);
    hoverHelp("The COMPLETE enemy pool is available to every level, and when "
              "swapping, every role rolls freely from everything - maximum "
              "variety. Still, only the types the seed actually uses are "
              "imported; the texture page budget stays, measured, well below "
              "the 256 limit. Scripted enemies remain untouched in every "
              "mode.");

    // Fine selection in its own window: the list is too long for the main
    // page and the user rarely touches it.
    if (ImGui::Button(T("Enemy types..."))) a.kindsOpen = true;
    hoverHelp("Which enemy types the random draw may pick at all. Good for "
              "seeing favorite enemies more often - or for testing a single "
              "type: deselect everything but one.");
    if (!a.optEnemiesOff.empty())
    {
        ImGui::SameLine();
        ImGui::TextDisabled(T("%d deselected"), static_cast<int>(a.optEnemiesOff.size()));
    }
    ImGui::EndDisabled();

    balanceSlider("Enemy health", &a.balEnemyHealth, &a.balEnemyHealthRandom, 0.5f, 2.0f,
                  "How tough all enemies are. 0.50x halves their hit points, "
                  "2.00x doubles them.\n\n"
                  "This writes the EnemyHealth line in Balance.txt next to "
                  "PCtomb5.exe. If you tuned individual enemies there by hand, "
                  "they are kept: the slider applies on top.");

    balanceSlider("Enemy damage", &a.balEnemyDamage, &a.balEnemyDamageRandom, 0.5f, 2.0f,
                  "How hard all enemies hit. Traps, falls, fire, poison and "
                  "drowning always stay as hard as in the original, as do the "
                  "built-in level hazards like the ceiling gun.\n\n"
                  "This writes the EnemyDamage line in Balance.txt next to "
                  "PCtomb5.exe.");

    if (ImGui::Button(T("Reset balance")))
    {
        a.balWeaponsRandom = a.balEnemyHealthRandom = a.balEnemyDamageRandom = false;
        a.balWeapons = a.balEnemyHealth = a.balEnemyDamage = 1.0f;
        writeBalance();
    }
    hoverHelp("Unchecks the random boxes, sets the three sliders to 1.00x and "
              "writes that to Balance.txt. Individual values tuned by hand in "
              "the file are kept - if you want those reset too, delete "
              "Balance.txt; the game recreates it completely on the next "
              "start.");

    if (!a.balNote.empty())
        ImGui::TextDisabled(T("%s"), a.balNote.c_str());
    closeBlock(&a.heights.enemies);


    ImGui::EndGroup();
    ImGui::SameLine();

    // --- Column 4: levels, music, secrets --------------------------------
    ImGui::BeginGroup();

    openBlock("k_level", "6. Levels", fLevel);
    ImGui::BeginDisabled(a.optLevelOrder == 2);	// meaningless with "free across everything", deliberately inert
    ImGui::Checkbox(T("Shuffle level sections"), &a.phLevelSections);
    ImGui::EndDisabled();
    hoverHelp("Play the four story sections (Rome, Russia, Ireland, VCI) in "
              "random order. Within a section the order stays - all key "
              "chains remain valid, the credits come after the last section. "
              "REQUIRES the engine from 2026-08-22 on (level identity from "
              "the file name); old savegames do not match the new order.");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const char* orderNames[] = { T("Level order: off"), T("within the sections"), T("free across everything") };
    ImGui::Combo("##levelorder", &a.optLevelOrder, orderNames, 3);
    hoverHelp("Within: Rome/Russia/Ireland/VCI stay blocks, only the levels "
              "inside them shuffle. Free: all 13 levels across all sections "
              "(e.g. rich1 -> Andrea2 -> joby5 -> rich2). The cutscene "
              "richcut2 always stays attached to rich2. On every level start, "
              "keys/puzzles/pickups and the grappling gun (rich3 only) are "
              "reset - weapons, ammo, medipacks and flares are kept. Whatever "
              "a level needs from another one - the Swipe Card for joby3 and "
              "the crowbar for Andrea3 - the script grants, "
              "model and name included. Curated in LevelChains.h.");

    ImGui::Checkbox(T("Shuffle weather"), &a.optWeather);
    hoverHelp("Every level with open sky gets dry weather, rain or snow "
              "rolled from the seed - the rainy Ireland levels can come up "
              "dry, the Russian base can get rain instead of snow. Six of "
              "the thirteen levels can show weather at all (andy1, Andy2, "
              "andy3, joby2, rich2, rich3); in the others the engine draws "
              "no drop in any room, so they keep what Core gave them. "
              "The spoiler lists what each level got.");

    closeBlock(&a.heights.level);

    openBlock("k_music", "7. Music", fMusic);
    ImGui::Checkbox(T("Shuffle background music"), &a.phMusicSwitch);
    hoverHelp("The level music is shuffled among the playable levels - the "
              "audio byte in the CMD_LEVEL entry of SCRIPT.DAT. Cutscene "
              "levels keep their music. Every run rolls anew from the backup; "
              "uncheck it and the next run restores the original music.");
    ImGui::Checkbox(T("Shuffle trigger sound/music IDs"), &a.phMusicTrigger);
    hoverHelp("The music triggers in the levels (TO_CD) get shuffled tracks - "
              "GLOBALLY across all levels: only tracks that some trigger in "
              "the game actually fires (no cutscene dialogue, no FMV audio), "
              "and the same track sounds the same in every level. Atmospheres "
              "still swap only among themselves, so a mood change stays a "
              "mood change. The hard-coded cutscene and tutorial triggering "
              "is not affected.");

    // --- Secrets (#53) ---------------------------------------------------
    //
    // Four kinds: vanilla (off), the normal pool, and two user-list
    // categories with an author dropdown. The lists lie as
    // Author_Category.txt in data\secrets next to the program; they are
    // read by SecretList.cpp - the same grammar is read by SecretPlan.ps1
    // during curation.
    closeBlock(&a.heights.music);

    openBlock("k_secrets", "8. Secrets", fSecrets,
              remainingHeight(a.heights.level + gapY + a.heights.music + gapY));

    ImGui::RadioButton(T("Vanilla"), &a.optSecrets, 0);
    hoverHelp("The secrets stay where Core hid them.");
    ImGui::RadioButton(T("In the normal item pool"), &a.optSecrets, 1);
    hoverHelp("The secrets go into the shuffle pool as ordinary pickups and "
              "can lie anywhere equipment is allowed to lie.");

    ImGui::RadioButton(T("Glitchless list"), &a.optSecrets, 2);
    hoverHelp("Hand-picked hiding spots by one author, reachable without "
              "glitches. A list can also place extra objects per level - "
              "keys, switches, doors, even new portals.");
    ImGui::RadioButton(T("Glitched list"), &a.optSecrets, 3);
    hoverHelp("Like the glitchless list, but the hiding spots may require "
              "glitches.");

    ImGui::Spacing();
    ImGui::Checkbox(T("Roll the reward for finding them all"), &a.optSecretRewards);
    hoverHelp("Finding every secret in a level awards items. This rolls what "
              "they are - four to eight, per level, from weapons, ammunition "
              "and medipacks; no keys or puzzle items. Written to "
              "SecretRewards.txt beside the game, where it can also be "
              "edited by hand. Left off, that file is not touched.");

    if (a.optSecrets >= 2)
    {
        const char* cat = (a.optSecrets == 2) ? "Glitchless" : "Glitched";
        std::vector<const SecretSource*> matching;
        for (const SecretSource& q : a.secretSources)
            if (q.category == cat) matching.push_back(&q);

        ImGui::SetNextItemWidth(220.0f);
        const char* display = T("(no lists found)");
        for (const SecretSource* q : matching)
            if (q->author == a.secretAuthor) display = q->author.c_str();
        if (!matching.empty() && display[0] == '(')
        { a.secretAuthor = matching[0]->author; display = matching[0]->author.c_str(); }

        if (ImGui::BeginCombo(T("Author"), display))
        {
            for (const SecretSource* q : matching)
                if (ImGui::Selectable(q->author.c_str(), q->author == a.secretAuthor))
                    a.secretAuthor = q->author;
            ImGui::EndCombo();
        }
        if (matching.empty())
            ImGui::TextDisabled(T("No %s list found in data\\secrets - the levels stay vanilla."), cat);
    }
    closeBlock();

    ImGui::EndGroup();

    //------------------------------------------------------------------------
    // 9. In preparation - its own row under column 1 (user wish).
    //
    // Used to stand under the enemy box. There it prevented the four columns
    // of the top row from reaching a common bottom edge: the column was
    // always the longest. As its own row below, it bothers nobody, and the
    // top row closes flush.
    //------------------------------------------------------------------------
    ImGui::Spacing();
    // --- Placeholders (user wish, all still inert) -----------------------
    //
    // Visible so the GUI shows its final shape; grayed out until the
    // respective logic exists. Explanations as hover help. Textures and
    // rooms stand together in one box - both are future work, and separate
    // they would be two very flat boxes.
    openBlock("k_preparation", "9. In preparation", fPrep);
    ImGui::BeginDisabled(true);

    ImGui::TextDisabled(T("Textures"));
    ImGui::Checkbox(T("RGB vanilla textures"), &a.phTexRgb);
    hoverHelp("Coming later: color dice over the vanilla texture pages - the "
              "seed shifts the hues, purely cosmetic.");
    ImGui::Checkbox(T("Mix across levels"), &a.phTexMix);
    hoverHelp("Coming later: swap texture pages between levels - Rome in "
              "Russia, the submarine in Ireland.");
    ImGui::Checkbox(T("Patch remaster textures"), &a.phTexRemaster);
    hoverHelp("Coming later: patch remaster textures (if the player owns "
              "them) into the classic levels.");

    ImGui::TextDisabled(T("Rooms"));
    ImGui::Checkbox(T("Include backdrop areas"), &a.phRoomBackdrops);
    hoverHelp("Coming later: bring pure backdrop areas into play so items "
              "and enemies can be placed there.");
    ImGui::Checkbox(T("Make neighboring rooms walkable"), &a.phRoomNeighbors);
    hoverHelp("Coming later: open adjacent rooms that are locked in vanilla - "
              "more area for placement.");
    ImGui::Checkbox(T("joby4 NoWater"), &a.phJoby4NoWater);
    hoverHelp("Coming later: Deepsea Dive without water - joby4 would "
              "finally be fully randomizable instead of vanilla.");

    ImGui::EndDisabled();
    closeBlock();

    //------------------------------------------------------------------------
    // 10. Levels - centered under the four columns (user wish).
    //
    // The table has been narrow since the spoiler. Left-aligned under a wide
    // window it would look lost; centered it stands as its own block and
    // leaves the background free left and right.
    //------------------------------------------------------------------------
    ImGui::Spacing();

    const float tableWidth = 340.0f;
    auto centered = [](float width)
    {
        const float free = ImGui::GetContentRegionAvail().x;
        if (free > width)
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (free - width) * 0.5f);
    };

    centered(tableWidth);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(fList.x, fList.y, fList.z, 0.13f));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(fList.x, fList.y, fList.z, 0.75f));
    ImGui::BeginChild("k_levellist", ImVec2(tableWidth, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fList.x, fList.y, fList.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(fList.x, fList.y, fList.z, 0.6f));
    ImGui::SeparatorText(T("10. Levels"));
    ImGui::PopStyleColor(2);
    if (a.skippedNonPlayable > 0)
        ImGui::TextWrapped(T("%d non-playable files (Title, del, richcut2) are checked, "
                             "but never touched."), a.skippedNonPlayable);
    // Fixed height by level count instead of a scrollbar (user wish):
    // without ScrollY and without a height, the table grows exactly to its
    // rows - manageable with 13 playable levels.
    // Two columns instead of three: what a run did in detail has been in
    // spoiler.txt since the spoiler. The third column cost half the window
    // width for one line of text read once.
    if (ImGui::BeginTable("levels", 2,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn(T("Level"), ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn(T("State"), ImGuiTableColumnFlags_WidthFixed, 150);
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

            // As long as no run has run, the file state stands here; after
            // that, the result of this run. Errors thus stay visible without
            // the table needing a text column - the details are in
            // spoiler.txt.
            ImGui::TableSetColumnIndex(1);
            if (r.result.empty())
            {
                switch (r.state)
                {
                case LevelState::Vanilla:  ImGui::TextUnformatted(T("original")); break;
                case LevelState::Modified: ImGui::TextColored(ImVec4(1, .5f, .3f, 1), T("modified")); break;
                case LevelState::Missing:  ImGui::TextColored(ImVec4(1, .4f, .4f, 1), T("missing")); break;
                default:                   ImGui::TextUnformatted(T("unknown")); break;
                }
            }
            else if (r.result.rfind("ERROR", 0) == 0)
            {
                ImGui::TextColored(ImVec4(1, .4f, .4f, 1), T("Error"));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip(T("%s"), r.result.c_str());
            }
            else if (r.result == "skipped")
            {
                ImGui::TextDisabled(T("skipped"));
            }
            else
            {
                ImGui::TextColored(ImVec4(.5f, .9f, .5f, 1), T("done"));
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip(T("%s"), r.result.c_str());
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    // --- 11. Let's go - likewise centered, under the level list ----------
    ImGui::Spacing();

    const float startWidth = 400.0f;
    const bool busy = (a.phase != App::Phase::Idle);

    centered(startWidth);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(fStart.x, fStart.y, fStart.z, 0.13f));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(fStart.x, fStart.y, fStart.z, 0.75f));
    ImGui::BeginChild("k_start", ImVec2(startWidth, 0.0f),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(fStart.x, fStart.y, fStart.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(fStart.x, fStart.y, fStart.z, 0.6f));
    ImGui::SeparatorText(T("11. Let's go"));
    ImGui::PopStyleColor(2);

    // The progress stands above the buttons, because that is exactly where
    // one looks once "Randomize" has been pressed.
    if (busy)
    {
        char overlay[96];
        if (a.phase == App::Phase::Copy)
            std::snprintf(overlay, sizeof(overlay), "%s", T("Restoring originals..."));
        else
            std::snprintf(overlay, sizeof(overlay), "%s  (%d of %d)",
                          a.phaseText.c_str(), a.doneCount + a.failCount, a.plannedCount);
        ImGui::ProgressBar(a.progress(), ImVec2(-FLT_MIN, 22), overlay);
    }
    else
    {
        // Keep the space, so the buttons do not jump as soon as a run
        // starts - a hopping window looks broken.
        ImGui::Dummy(ImVec2(0, 22));
    }

    ImGui::BeginDisabled(!a.sourceOk || busy);
    if (ImGui::Button(T("Randomize"), ImVec2(160, 32))) startRandomize(a);
    ImGui::SameLine();
    if (ImGui::Button(T("Restore original"), ImVec2(200, 32))) doRestore(a);
    ImGui::EndDisabled();

    if (!a.sourceOk && !busy)
        ImGui::TextDisabled(T("(choose an unmodified installation first)"));

    if (!a.status.empty())
    {
        ImGui::Separator();
        if (a.statusIsError) ImGui::TextColored(ImVec4(1, .5f, .3f, 1), T("%s"), a.status.c_str());
        else                 ImGui::TextWrapped(T("%s"), a.status.c_str());
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::End();

    // After the main window, so they lie above it - the main window fills
    // the whole screen and carries NoBringToFrontOnFocus.
    drawDebugWindow(a);
    drawEnemyKindsWindow(a);
    drawWardrobeWindow(a);
    drawPathsWindow(a, hwnd);
}

//----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Program icon from the resources (TR5Randomizer.rc, the user's logo).
    // LoadIconW takes the fitting size from the ICO file per field: hIcon
    // the large one (Alt-Tab, window properties), hIconSm the small one
    // (title bar, taskbar). Without these two lines, Windows shows the
    // default icon even though the resource lies in the EXE.
    HICON iconLarge = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON iconSmall = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);

    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, WndProc, 0, 0, hInst, iconLarge, nullptr, nullptr, nullptr,
                    L"TR5RandomizerWnd", iconSmall };
    RegisterClassExW(&wc);

    // Window size from the settings file; a generous default on first start
    // (user wish - the old 820x780 was too small). Lower bounds catch a
    // hand-mangled file.
    Config pre;
    pre.load();
    const int windowW = (pre.winW >= 500) ? pre.winW : 1000;
    const int windowH = (pre.winH >= 400) ? pre.winH : 920;
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"TR5 Randomizer", WS_OVERLAPPEDWINDOW,
                              100, 100, windowW, windowH, nullptr, nullptr, hInst, nullptr);

    if (!createDevice(hwnd))
    {
        MessageBoxW(nullptr, L"Direct3D 11 cannot start on this machine.",
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
    ImGui::GetIO().IniFilename = nullptr;   // leave no window-layout file behind

    // Sub-windows (the sector markers, say) should be draggable out of the
    // main window, even onto a second screen (user wish). Hence the docking
    // branch of Dear ImGui with viewports: as soon as a window leaves the
    // edge, it gets its own operating-system window. WindowRounding 0 and
    // full opacity, because free-standing windows cannot have round,
    // translucent corners.
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    loadBackground();

    App app;
    app.cfg.load();
    gGermanUi = app.cfg.germanUi;   // language (#128), default English
    std::snprintf(app.seedBuf, sizeof(app.seedBuf), "%s",
                  app.cfg.seedText.empty() ? SeedText::format(SeedText::random()).c_str()
                                           : app.cfg.seedText.c_str());

    // Restore the saved selection - the user should find his checkboxes
    // again on the next start.
    app.optOtherPickups = app.cfg.optOtherPickups;
    app.optKeyItems     = app.cfg.optKeyItems;
    app.optEnemies      = app.cfg.optEnemies;
    app.optEnemyCount   = app.cfg.enemyCount;
    app.optEnemySwap    = app.cfg.optEnemySwap;
    app.optEnemyPool    = app.cfg.enemyPool;
    app.optEnemiesOff   = app.cfg.enemiesDisabled;
    app.optOutfits      = app.cfg.optOutfits;
    app.optOutfitsTr3   = app.cfg.optOutfitsTr3;
    app.optOutfitsOff   = app.cfg.outfitsDisabled;
    app.optOutfitsOn    = app.cfg.outfitsEnabled;

    // Where the Mods folder is, before anything can scan (#153, round M2) -
    // see wardrobeFollowsGameFolder.
    wardrobeFollowsGameFolder(app);
    app.optItemAmount   = app.cfg.itemAmount;
    app.optSecrets      = app.cfg.secrets;
    app.optSecretRewards = app.cfg.secretRewards;
    app.secretAuthor     = app.cfg.secretAuthor;
    app.phMusicSwitch     = app.cfg.optMusicSwitch;
    app.phMusicTrigger    = app.cfg.optMusicTrigger;
    app.phLevelSections = app.cfg.optLevelSections;
    app.optLevelOrder     = app.cfg.optLevelOrder;
    app.optWeather        = app.cfg.optWeather;

    // Balance.txt (#108): if it already exists in the installation, IT is
    // right and not our cfg - the user may have changed it in Notepad, and
    // then the sliders should show what really applies. Only if nothing
    // stands there does the last remembered slider position apply.
    {
        app.balWeaponsRandom        = app.cfg.balWeaponsRandom;
        app.balEnemyHealthRandom   = app.cfg.balEnemyHealthRandom;
        app.balEnemyDamageRandom = app.cfg.balEnemyDamageRandom;

        const BalanceFile::Values vals = BalanceFile::read(app.gameDir());
        const bool present = PathUtil::fileExists(
            PathUtil::join(app.gameDir(), L"Balance.txt"));

        app.balWeapons        = (present ? vals.weaponDamage : app.cfg.balWeapons)        / 100.0f;
        app.balEnemyHealth   = (present ? vals.enemyHealth   : app.cfg.balEnemyHealth)   / 100.0f;
        app.balEnemyDamage = (present ? vals.enemyDamage : app.cfg.balEnemyDamage) / 100.0f;

        // Cranked further by hand than the sliders reach? Then the slider
        // clamps onto its track - but nothing is written until the user
        // touches it, the file stays untouched until then.
        auto clamp = [](float& v, float from, float to)
        { if (v < from) v = from; if (v > to) v = to; };

        clamp(app.balWeapons, 0.0f, 2.0f);
        clamp(app.balEnemyHealth, 0.5f, 2.0f);
        clamp(app.balEnemyDamage, 0.5f, 2.0f);
    }

    // Write everything worth remembering into the settings file: window
    // size, checkboxes, seed. Runs when the window is dragged, on every
    // changed selection and on exit - the file is tiny, that may happen
    // often.
    auto rememberSettings = [&app](HWND window)
    {
        RECT r;
        if (GetWindowRect(window, &r) && r.right > r.left && r.bottom > r.top)
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
        app.cfg.enemiesDisabled = app.optEnemiesOff;
        app.cfg.optOutfits      = app.optOutfits;
        app.cfg.optOutfitsTr3   = app.optOutfitsTr3;
        app.cfg.outfitsDisabled = app.optOutfitsOff;
        app.cfg.outfitsEnabled  = app.optOutfitsOn;
        app.cfg.itemAmount      = app.optItemAmount;
        app.cfg.secrets         = app.optSecrets;
        app.cfg.secretRewards   = app.optSecretRewards;
        app.cfg.secretAuthor     = app.secretAuthor;
        app.cfg.optMusicSwitch     = app.phMusicSwitch;
        app.cfg.optMusicTrigger    = app.phMusicTrigger;
        app.cfg.optLevelSections = app.phLevelSections;
        app.cfg.optLevelOrder      = app.optLevelOrder;
        app.cfg.optWeather         = app.optWeather;
        app.cfg.balWeapons        = static_cast<int>(app.balWeapons * 100.0f + 0.5f);
        app.cfg.balEnemyHealth   = static_cast<int>(app.balEnemyHealth * 100.0f + 0.5f);
        app.cfg.balEnemyDamage = static_cast<int>(app.balEnemyDamage * 100.0f + 0.5f);
        app.cfg.balWeaponsRandom        = app.balWeaponsRandom;
        app.cfg.balEnemyHealthRandom   = app.balEnemyHealthRandom;
        app.cfg.balEnemyDamageRandom = app.balEnemyDamageRandom;
        app.cfg.seedText        = app.seedBuf;
        app.cfg.save();
    };

    std::string err;
    // data\ beside the program wins, the baked copy is the floor
    // (EmbeddedData.h). This used to be the one thing that could not go
    // missing without the whole program being useless - which is precisely
    // what a copying accident produced once.
    {
        std::string csvText;
        DataFile::Source csvSrc = DataFile::Source::External;
        app.payloadOk = DataFile::read(app.payloadDir(), "VanillaLevelChecksums.csv", csvText, csvSrc, err)
                     && app.data.loadChecksumsText(csvText, err);
        if (!app.payloadOk)
        {
            // With a built-in copy this can now only mean a broken build or
            // a damaged file in data\ - hence say what was tried.
            app.payloadError = "The checksum list could not be read (" + err + "). Tried: "
                             + PathUtil::narrow(PathUtil::join(app.payloadDir(), L"VanillaLevelChecksums.csv"))
                             + ", then the copy built into the program.";
        }
    }

    //------------------------------------------------------------------------
    // Check the curation stamps of the zone exports.
    //
    // The randomizer never reads the curation tables from ZoneAnalysis.ps1,
    // only the zones_<level>.json generated from them. If the table is
    // changed and not regenerated, it silently keeps working with the old
    // curation - and the folder data\ is a build product, so there is no
    // commit that would make it visible either.
    //
    // Exactly that blew up on 2026-08-23: five exports dated from Aug
    // 13/14 and still carried the refuted global HK offset, while the
    // table had known it only for rich2 since Aug 19. The user had noticed
    // it in game as "only partially effective", without the cause being
    // visible.
    //
    // The check is not against the script - the player does not even have
    // it - but the exports AGAINST EACH OTHER. A mixed state means for
    // certain that someone regenerated only a part. A uniformly old state
    // does not stand out this way, but that is the rarer case, and when
    // packaging, BuildRelease.ps1 regenerates everything fresh anyway.
    //------------------------------------------------------------------------
    // Since the exports are baked into the EXE, the set to compare is no
    // longer "whatever lies in data\": it is every baked name plus anything
    // extra the folder holds, each one resolved by the same rule the run
    // uses. That keeps the check honest in the case it now has to cover -
    // fourteen baked exports and one hand-dropped file with an older stamp
    // would otherwise never be compared against each other.
    if (app.payloadOk)
    {
        std::map<std::string, std::vector<std::string>> stamps;

        std::vector<std::string> files = EmbeddedData::names();
        files.erase(std::remove_if(files.begin(), files.end(), [](const std::string& n) {
                        return n.rfind("zones_", 0) != 0;
                    }), files.end());
        {
            WIN32_FIND_DATAW fd{};
            const std::wstring pattern = PathUtil::join(app.payloadDir(), L"zones_*.json");
            HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                {
                    const std::string n = PathUtil::narrow(fd.cFileName);
                    if (std::find(files.begin(), files.end(), n) == files.end()) files.push_back(n);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
        }

        for (const std::string& file : files)
        {
            std::string text, jerr;
            DataFile::Source zsrc = DataFile::Source::External;
            if (!DataFile::read(app.payloadDir(), file, text, zsrc, jerr)) continue;

            JsonValue z;
            if (!Json::parseString(text, z, jerr)) continue;

            const std::string stamp = z["curationStamp"].asString();
            const std::string level = z["level"].asString();
            stamps[stamp.empty() ? std::string("(no stamp)") : stamp]
                .push_back(level.empty() ? file : level);
        }

        if (stamps.size() > 1)
        {
            std::string t = "The zone exports carry different curation stamps - "
                            "part of them was not regenerated after a change to "
                            "ZoneAnalysis.ps1. Affected:";
            for (const auto& kv : stamps)
            {
                t += "\n  " + kv.first + ":";
                for (const std::string& l : kv.second) t += " " + l;
            }
            app.zoneWarning = t;
        }
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
        drawBackground();

        drawUi(app, hwnd);

        // One work step per frame, AFTER drawing. That way the user first
        // sees the new bar state and then the next step happens - the other
        // way round, the last step would stay invisible.
        advanceRun(app);

        // Persist the settings: after dragging the window, and whenever a
        // selection changed against the file.
        if (g_windowDragged)
        {
            g_windowDragged = false;
            rememberSettings(hwnd);
        }
        else if (app.cfg.optOtherPickups != app.optOtherPickups ||
                 app.cfg.optKeyItems     != app.optKeyItems ||
                 app.cfg.optEnemies      != app.optEnemies ||
                 app.cfg.enemyCount      != app.optEnemyCount ||
                 app.cfg.optEnemySwap    != app.optEnemySwap ||
                 app.cfg.enemyPool       != app.optEnemyPool ||
                 app.cfg.enemiesDisabled != app.optEnemiesOff ||
                 app.cfg.optOutfits      != app.optOutfits ||
                 app.cfg.optOutfitsTr3   != app.optOutfitsTr3 ||
                 app.cfg.outfitsDisabled != app.optOutfitsOff ||
                 app.cfg.outfitsEnabled  != app.optOutfitsOn ||
                 app.cfg.itemAmount      != app.optItemAmount ||
                 app.cfg.secrets         != app.optSecrets ||
                 app.cfg.secretRewards   != app.optSecretRewards ||
                 app.cfg.optMusicSwitch     != app.phMusicSwitch ||
                 app.cfg.optMusicTrigger    != app.phMusicTrigger ||
                 app.cfg.optLevelSections != app.phLevelSections ||
                 app.cfg.optLevelOrder      != app.optLevelOrder ||
                 app.cfg.optWeather         != app.optWeather ||
                 app.cfg.balWeapons        != static_cast<int>(app.balWeapons * 100.0f + 0.5f) ||
                 app.cfg.balEnemyHealth   != static_cast<int>(app.balEnemyHealth * 100.0f + 0.5f) ||
                 app.cfg.balEnemyDamage != static_cast<int>(app.balEnemyDamage * 100.0f + 0.5f) ||
                 app.cfg.balWeaponsRandom        != app.balWeaponsRandom ||
                 app.cfg.balEnemyHealthRandom   != app.balEnemyHealthRandom ||
                 app.cfg.balEnemyDamageRandom != app.balEnemyDamageRandom ||
                 app.cfg.secretAuthor     != app.secretAuthor)
        {
            rememberSettings(hwnd);
        }

        ImGui::Render();
        const float clear[4] = { 0.10f, 0.11f, 0.13f, 1.0f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Dragged-out windows live in their own operating-system windows
        // with their own swapchain - this call draws and presents those.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        g_swap->Present(1, 0);
    }

    rememberSettings(hwnd);   // final state, size and seed included

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    freeBackground();
    destroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
