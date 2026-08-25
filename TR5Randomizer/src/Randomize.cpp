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
// End-to-end: from a seed to a finished randomized level file.
//
// Usage:
//   Randomize.exe <zones.json> <levelinfo.json> <source.trc> <target.trc> [seed]
//
// The GUI drives the same steps; this program is the proof that the chain is
// complete, and it stays around for checking individual levels.
//----------------------------------------------------------------------------
#include "core/ApplyPatch.h"
#include "core/Json.h"
#include "core/Randomizer.h"
#include "core/Rng.h"
#include "core/ScriptDat.h"
#include "core/SecretList.h"
#include "core/Tr5Level.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("Usage: Randomize.exe <zones.json> <levelinfo.json> <source.trc> <target.trc> [seed] [switches...]\n");
        std::printf("  number:   count of extra enemies, 0 = none (default)\n");
        std::printf("  only:     leave items untouched - for enemy/outfit tests\n");
        std::printf("  outfit:   random outfit (Lara_Andrea/Joby/Rich)\n");
        std::printf("  weapons:  import the full weapon set (donors Andrea3/joby2/rich1)\n");
        std::printf("  mix:      swap the existing enemies among each other\n");
        std::printf("  cross:    extend the enemy pool by up to three foreign types (with mix/number)\n");
        std::printf("  allsanity: the whole enemy pool, every role rolls freely (with mix/number)\n");
        std::printf("  itemsN:   scatter N additional consumables (e.g. items20)\n");
        std::printf("  script=P: also treat the SCRIPT.DAT at P - young Lara out of the\n");
        std::printf("            Ireland levels and KEY8 for joby3 from joby2. Repeatable:\n");
        std::printf("            a second run over the same file changes nothing more.\n");
        std::printf("  music=background|trigger|both:\n");
        std::printf("            background rolls the level music in SCRIPT.DAT\n");
        std::printf("            (needs script=; NOT repeatable - every run rolls the\n");
        std::printf("            file anew). trigger sends the floordata's TO_CD tracks\n");
        std::printf("            through a GLOBAL permutation of the triggered tracks:\n");
        std::printf("            atmospheres among themselves, everything else likewise.\n");
        std::printf("  order=sections|levels|both|free:\n");
        std::printf("            sections permutes the four story sections, levels the\n");
        std::printf("            order WITHIN the sections, free all 13 levels across\n");
        std::printf("            everything (level chains from LevelChains.h are resolved\n");
        std::printf("            via GIVEOBJ). Needs script= and the engine with gfLevelId;\n");
        std::printf("            NOT repeatable.\n");
        return 2;
    }

    const std::string seedText = (argc > 5) ? argv[5] : "Weihnachten";
    int enemies = 0, extraItems = 0;
    bool enemiesOnly = false, outfits = false, weapons = false, mix = false, cross = false, allsanity = false;
    bool notes = false;
    std::set<int> enemiesDisabled;         // deselection, see argument enemiesoff=
    // Foreign installations for the guests from TR3 (#92). Without them the
    // four kinds drop silently out of the pool - exactly as for a user
    // without the games.
    std::wstring tr3Dir, tr3GoldDir;
    // Also treat SCRIPT.DAT - the same changes as the GUI.
    std::wstring scriptDat;
    // Secret list (#53): second patch pass from an author file.
    std::wstring secretList;
    // Music: "background" rolls the audio byte of the CMD_LEVEL entries
    // (needs script=), "trigger" the TO_CD tracks in the floordata, "both"
    // both. CAUTION background: not repeatable - every run with script=
    // rolls the file anew.
    bool musicBackground = false, musicTriggers = false;
    // Permute the level sections (needs script= and the engine with gfLevelId).
    bool sections = false;
    int  levelOrder = 0;            // 1 within the sections, 2 free across everything
    for (int i = 6; i < argc; ++i)
    {
        const std::string s = argv[i];
        if (s == "only")        enemiesOnly = true;
        else if (s == "notes") notes = true;   // print the report notes (diagnosis)
        else if (s == "outfit") outfits = true;
        else if (s == "weapons") weapons = true;
        else if (s == "mix")    mix = true;
        else if (s == "cross")  cross = true;
        else if (s == "allsanity") allsanity = true;
        else if (s.rfind("tr3=", 0) == 0)     tr3Dir     = toW(s.c_str() + 4);
        else if (s.rfind("tr3gold=", 0) == 0) tr3GoldDir = toW(s.c_str() + 8);
        else if (s.rfind("script=", 0) == 0)  scriptDat  = toW(s.c_str() + 7);
        else if (s.rfind("secrets=", 0) == 0) secretList = toW(s.c_str() + 8);
        else if (s == "music=background") musicBackground = true;
        else if (s == "music=trigger")    musicTriggers = true;
        else if (s == "music=both")       { musicBackground = true; musicTriggers = true; }
        else if (s == "order=sections") sections = true;
        else if (s == "order=levels")   levelOrder = 1;
        else if (s == "order=both")     { sections = true; levelOrder = 1; }
        else if (s == "order=free")     levelOrder = 2;
        // "enemiesoff=47,49" - the same deselection as the "Enemy types"
        // window in the GUI (#95), so the filter can be tested from scripts
        // too.
        else if (s.rfind("enemiesoff=", 0) == 0)
        {
            size_t a2 = 11;
            while (a2 <= s.size())
            {
                size_t e = s.find(',', a2);
                if (e == std::string::npos) e = s.size();
                std::string piece = s.substr(a2, e - a2);
                if (!piece.empty())
                {
                    int t = std::atoi(piece.c_str());
                    if (t > 0) enemiesDisabled.insert(t);
                }
                a2 = e + 1;
            }
        }
        // "itemsN" or "items=N" - must stand BEFORE the number fallback, or
        // atoi swallows the word as 0 and overwrites the enemy count.
        else if (s.rfind("items", 0) == 0)
        {
            const char* p = s.c_str() + 5;
            if (*p == '=') ++p;
            extraItems = std::atoi(p);
        }
        else                    enemies = std::atoi(argv[i]);
    }
    const uint64_t seed = SeedText::parse(seedText);

    // --- Treat SCRIPT.DAT along (switch script=) --------------------------
    //
    // The same changes as the GUI, from the same source: young Lara out of
    // the Ireland levels and the KEY8 entry for joby3. The GUI makes itself
    // a backup first and always reads from it; here there is no backup
    // folder, so the file is edited in place. That is harmless because both
    // changes are repeatable - an already cleared bit stays cleared, an
    // already present object command is not inserted a second time.
    //
    // Happens before the level work: if the script goes wrong, nothing has
    // been written yet.
    if (!scriptDat.empty())
    {
        ScriptDat sd;
        std::string sErr;
        if (!sd.load(scriptDat, sErr)) { std::printf("SCRIPT.DAT: %s\n", sErr.c_str()); return 1; }

        ScriptDat::Changes e;
        sd.randomizerChanges(e);

        if (e.levelsMissing > 0)
            std::printf("SCRIPT.DAT  %d Ireland levels not in the script - nothing changed.\n", e.levelsMissing);
        else
        {
            if (musicBackground)
            {
                int mg = 0;
                if (sd.shuffleMusic(seed, mg, sErr))
                    std::printf("SCRIPT.DAT  background music: %d levels rescored\n", mg);
                else
                    std::printf("SCRIPT.DAT  background music: %s\n", sErr.c_str());
            }

            if (sections)
            {
                std::string order;
                if (sd.shuffleSections(seed, order, sErr))
                    std::printf("SCRIPT.DAT  sections: %s\n", order.c_str());
                else
                    std::printf("SCRIPT.DAT  sections: %s\n", sErr.c_str());
            }

            if (levelOrder)
            {
                std::string order;
                if (sd.shuffleLevels(seed, levelOrder == 2, order, sErr))
                    std::printf("SCRIPT.DAT  order: %s\n", order.c_str());
                else
                    std::printf("SCRIPT.DAT  order: %s\n", sErr.c_str());
            }

            if (sections || levelOrder)
            {
                // FIRST clean up, THEN give - see Main.cpp/setup.cpp.
                std::vector<std::string> chains;
                if (!sd.cleanupInventory(chains, sErr))
                    std::printf("SCRIPT.DAT  inventory cleanup: %s\n", sErr.c_str());
                else if (!sd.resolveChains(chains, sErr))
                    std::printf("SCRIPT.DAT  LevelChains: %s\n", sErr.c_str());
                for (const std::string& k : chains)
                    std::printf("SCRIPT.DAT  %s\n", k.c_str());
            }

            if (!sd.save(scriptDat, sErr)) { std::printf("SCRIPT.DAT: %s\n", sErr.c_str()); return 1; }
            std::printf("SCRIPT.DAT  young Lara: %d levels changed; KEY8 for joby3: %s\n",
                        e.flagsChanged,
                        !e.keyError.empty() ? e.keyError.c_str()
                                             : (e.keyCopied ? "inserted" : "was already there"));
            std::printf("SCRIPT.DAT  rich2 pistols: %s\n",
                        !e.pistolError.empty() ? e.pistolError.c_str()
                                                  : (e.pistolsRemoved > 0 ? "taken away" : "was already so"));

            if (e.joby3Weapons > 0)
                std::printf("SCRIPT.DAT  joby3: %d weapon takeaways removed\n", e.joby3Weapons);
        }
    }

    std::string err;

    JsonValue zone, info;
    if (!Json::parseFile(toW(argv[1]), zone, err)) { std::printf("zone export: %s\n", err.c_str()); return 1; }
    if (!Json::parseFile(toW(argv[2]), info, err)) { std::printf("level info: %s\n", err.c_str()); return 1; }

    RandoInput in;
    if (!Rando::buildInput(zone, info, in, err)) { std::printf("input: %s\n", err.c_str()); return 1; }

    // Open the level BEFORE placement, not only for patching: if a trigger
    // travels along, placement must know which sectors can take it, and that
    // stands only in the floordata.
    Tr5Level lvl;
    if (!lvl.load(toW(argv[3]), err)) { std::printf("load level: %s\n", err.c_str()); return 1; }
    const int itemsBefore = lvl.itemCount();
    Patch::markTriggerSafeSectors(lvl, in);

    RandoOptions opt;
    opt.addEnemies = enemies > 0;
    opt.enemiesPerLevel = enemies;
    opt.swapEnemies = enemies > 0;     // combined in the CLI, enough for tests
    opt.randomOutfits = outfits;
    opt.allWeapons = weapons;
    opt.swapEnemies = mix;
    opt.crossEnemies = cross;
    opt.allsanity = allsanity;
    opt.disabledEnemies = enemiesDisabled;
    opt.tr3Dir     = tr3Dir;
    opt.tr3GoldDir = tr3GoldDir;
    opt.extraPickups = extraItems;
    if (enemiesOnly) { opt.shuffleKeyItems = false; opt.shuffleOtherPickups = false; }

    JsonValue patch;
    RandoReport rep;
    if (!Rando::run(in, opt, seed, patch, rep))
    {
        std::printf("placement failed: %s\n", rep.error.c_str());
        return 1;
    }

    // Source levels for soundAdds: they lie next to the source file, which
    // is passed in here anyway.
    static std::wstring folder = toW(argv[3]);
    {
        const size_t cut = folder.find_last_of(L"\\/");
        folder = (cut == std::wstring::npos) ? L"." : folder.substr(0, cut);
    }
    auto source = [](const std::string& name, void*) -> std::wstring {
        std::wstring w;
        for (char c : name) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
        return folder + L"\\" + w + L".trc";
    };

    PatchReport pr;
    // Cache next to the source files (#103): extract once instead of per
    // level. Without a TR3 path there is nothing to cache.
    const std::wstring cache = tr3Dir.empty() ? std::wstring() : folder + L"\\Import-Export";
    if (!Patch::apply(lvl, patch, pr, err, source, nullptr, tr3Dir, tr3GoldDir, cache)) { std::printf("patch: %s\n", err.c_str()); return 1; }
    for (const std::string& w : pr.warnings) std::printf("  note: %s\n", w.c_str());

    // Secrets from a user list: second pass against the state after the
    // first patch - exactly as in the GUI (doLevel).
    if (!secretList.empty())
    {
        std::string levelName = zone["level"].asString("");
        JsonValue sPatch;
        std::vector<std::string> sWarn;
        int sCount = 0;
        if (!buildSecretPatch(secretList, levelName, lvl, sPatch, sCount, sWarn, err))
        { std::printf("secret list: %s\n", err.c_str()); return 1; }
        for (const std::string& w : sWarn) std::printf("  secrets: %s\n", w.c_str());

        PatchReport spr;
        if (!Patch::apply(lvl, sPatch, spr, err, source, nullptr, tr3Dir, tr3GoldDir, cache))
        { std::printf("secret patch: %s\n", err.c_str()); return 1; }
        for (const std::string& w : spr.warnings) std::printf("  secrets: %s\n", w.c_str());
        std::printf("  Secrets     %d relocated, %d extras (%d triggers, %zu portals)\n",
                    sCount, (spr.edited + spr.added) - sCount,
                    spr.triggersAdded, sPatch["portalAdds"].size());
    }


    if (notes)
        for (const std::string& n : rep.notes) std::printf("  note: %s\n", n.c_str());

    if (musicTriggers)
    {
        const long mw = Patch::shuffleMusicTriggers(lvl, seed);
        std::printf("  Music trig. %ld words redistributed\n", mw);
    }

    if (!lvl.save(toW(argv[4]), err)) { std::printf("save: %s\n", err.c_str()); return 1; }

    std::printf("Level %s\n", zone["level"].asString("?").c_str());
    std::printf("  Seed        %s  (0x%016llX)\n", seedText.c_str(), (unsigned long long)seed);
    std::printf("  Placement   keys %d, other %d, pinned %d, injected %d\n",
                rep.movedKeyItems, rep.movedOtherItems, rep.pinnedItems, rep.injected);
    std::printf("  Patch       %d edited, %d added\n", pr.edited, pr.added);
    if (rep.enemiesAdded > 0 || pr.triggersAdded > 0 || pr.triggersMoved > 0)
        std::printf("  Triggers    %d enemies placed, %d triggers new, %d moved\n",
                    rep.enemiesAdded, pr.triggersAdded, pr.triggersMoved);
    if (rep.enemiesSwapped > 0 || pr.soundsCopied > 0)
        std::printf("  Enemies     %d swapped, %d samples copied\n",
                    rep.enemiesSwapped, pr.soundsCopied);
    if (!pr.outfitFrom.empty())
        std::printf("  Outfit      Lara from %s\n", pr.outfitFrom.c_str());
    if (pr.weaponsCopied > 0 || rep.weaponsScattered > 0)
        std::printf("  Weapons     %d slots copied, %d items scattered\n",
                    pr.weaponsCopied, rep.weaponsScattered);
    if (rep.extraPickups > 0)
        std::printf("  Abundance   %d additional consumables scattered\n", rep.extraPickups);
    if (rep.crossTypesUsed > 0)
        std::printf("  Cross       %d foreign enemy types, %d slots copied\n",
                    rep.crossTypesUsed, pr.moveablesCopied);
    std::printf("  Items       %d -> %d\n", itemsBefore, lvl.itemCount());

    // Cross-check: reload the result and verify that the changes really
    // stand in the file and the structure still adds up. Without this step
    // one would only know that saving did not crash.
    Tr5Level check;
    if (!check.load(toW(argv[4]), err))
    {
        std::printf("  [FAIL] result is no longer readable: %s\n", err.c_str());
        return 1;
    }
    if (check.itemCount() != lvl.itemCount())
    {
        std::printf("  [FAIL] item count after reload: %d instead of %d\n", check.itemCount(), lvl.itemCount());
        return 1;
    }

    const JsonValue& edits = patch["itemEdits"];
    int verified = 0, wrong = 0;
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const int idx = static_cast<int>(edits[i]["index"].asInt());
        Tr5Item it{};
        if (!check.getItem(idx, it)) { ++wrong; continue; }

        // An enemy swap carries only index and typeId - no position, no
        // OCB. The position comparison below would check it against zeros
        // and always report. It gets its own check.
        if (!edits[i].has("x"))
        {
            if (edits[i].has("typeId") &&
                it.objectNumber != static_cast<int16_t>(edits[i]["typeId"].asInt()))
                ++wrong;
            else
                ++verified;
            continue;
        }

        if (it.roomNumber != static_cast<int16_t>(edits[i]["room"].asInt()) ||
            it.x != static_cast<int32_t>(edits[i]["x"].asInt()) ||
            it.y != static_cast<int32_t>(edits[i]["y"].asInt()) ||
            it.z != static_cast<int32_t>(edits[i]["z"].asInt()) ||
            (it.triggerFlags & 0x3F) != 0)
        {
            ++wrong;
            continue;
        }
        ++verified;
    }
    std::printf("  Cross-check %d positions confirmed, %d deviating\n", verified, wrong);

    return wrong ? 1 : 0;
}
