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
#pragma once
#include <set>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Settings that persist between two starts.
//
// Deliberately a text file next to the EXE and not the registry: the tool is
// meant to be portable - copy the folder, it runs. The user can also look
// inside and fix things by hand if need be, which in case of a path problem
// is a lot friendlier than a registry editor.
//----------------------------------------------------------------------------
struct Config
{
    std::wstring sourceDir;   // data folder of the user's Chronicles install
    std::string  seedText;    // last used seed, as text

    // Third-party installations for the Tomb Raider 3 enemies (#102).
    //
    // We ship NO game data - whoever wants the dinosaurs must own the games.
    // The run fetches model, textures and sounds from them at runtime;
    // without a path the affected types stay grayed out in the UI.
    //
    //   tr3Dir     data folder of Tomb Raider 3 - CRASH.TR2 carries T-Rex,
    //              Raptor and Compsognathus, MAIN.SFX the matching sounds.
    //   tr3GoldDir data folder of The Lost Artifact - chunnel.TR2 carries
    //              the vulture.
    std::wstring tr3Dir;
    std::wstring tr3GoldDir;

    // Window size at last exit or after the last resize. 0 means "never
    // saved yet" - then the default size applies.
    int winW = 0, winH = 0;

    // UI language (#128). Default is ENGLISH (user decision 2026-08-24);
    // the translation table lives in gui\Language.cpp.
    bool germanUi = false;

    // The shuffle options from section 3, so the user finds their choices
    // again on the next start. Defaults identical to those of the UI - if a
    // key is missing from the file, nothing changes.
    bool optOtherPickups = true;
    bool optKeyItems     = true;
    bool optEnemies      = false;
    int  enemyCount      = 6;
    bool optEnemySwap    = false;
    int  enemyPool       = 0;     // 0 level pool, 1 cross mix, 2 allsanity
    // Deselected enemy types (#95). The DESELECTION is what gets saved, so
    // a type added later is not silently dropped by old files.
    std::set<int> enemiesDisabled;
    bool optOutfits      = false;
    bool optOutfitsTr3   = false;   // guest outfits from Tomb Raider 3 in the pool

    // The wardrobe: which outfits the roll may NOT draw. Held is the
    // DESELECTION, as with enemiesDisabled - an outfit added later is then
    // automatically on and does not fall through an old settings file.
    std::set<std::string> outfitsDisabled;
    /* And the SELECTION of the entries the wardrobe scan found (#153, round
       M2), which is held the other way round because the scan's content
       depends on what is installed - see RandoOptions::outfitsOn. Same
       comma-separated shape, same rule about an id nobody recognises: it is
       kept, because it may name a mod that is out of the folder today. */
    std::set<std::string> outfitsEnabled;
    int  itemAmount       = 0;     // 0 vanilla, 1 more, 2 a lot more (#36)
    int  secrets         = 0;     // secrets (#53): 0 vanilla, 1 pool, 2/3 list
    bool secretRewards   = false;  // roll SecretRewards.txt beside the game (#53)
    std::string secretAuthor;      // chosen list author (kinds 2/3)
    bool optMusicSwitch  = false; // shuffle level music in SCRIPT.DAT
    bool optMusicTrigger = false; // shuffle the floordata's TO_CD tracks
    bool optLevelSections = false;  // permute the four story sections
    int  optLevelOrder   = 0;     // 0 off, 1 within the sections, 2 free across everything
    bool optWeather      = false; // roll dry/rain/snow per level (#144)

    // The three Balance.txt sliders (#108), in percent. Only the slider
    // position is stored here; the truth lives in Balance.txt next to
    // PCtomb5.exe and is read from there at startup.
    int  balWeapons        = 100;
    int  balEnemyHealth   = 100;
    int  balEnemyDamage = 100;
    bool balWeaponsRandom        = false;
    bool balEnemyHealthRandom   = false;
    bool balEnemyDamageRandom = false;

    // Reads/writes <exe folder>\TR5Randomizer.cfg. If the file is missing,
    // everything stays at its default - that is not an error, it is the
    // first start.
    void load();
    bool save() const;

    static std::wstring path();
};
