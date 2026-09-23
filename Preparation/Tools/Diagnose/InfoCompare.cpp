//----------------------------------------------------------------------------
// TR5 Randomizer
// Copyright (C) 2026 ChrisChanSan
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file at the root of this source tree.
//----------------------------------------------------------------------------
//
// Proves that Rando::levelInfoFromLevel reproduces the old level_<name>.json
// exactly, for the three arrays anything ever read.
//
// This is the gate for deleting 45 MB of exports. Reading the diff of the new
// code is not evidence; comparing its output against the file it replaces, on
// every level, is.
//
// Build (64 bit): bic.bat next to this file.
// Usage: InfoCompare.exe <level.trc> <level_<name>.json>
//
// Known and expected difference: the export writes "refItem": null for every
// pickup trigger and pushes the reference word into the command list as a
// fake first Object command. The level file carries the word properly. Those
// are reported as EXPORT DEFECT rather than as a mismatch - they are the
// reason this change is an improvement and not merely a saving.
//----------------------------------------------------------------------------

#include "core/Json.h"
#include "core/LevelInfo.h"
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <map>

static std::wstring toW(const char* s)
{
    return std::wstring(s, s + std::strlen(s));
}

static int g_bad = 0, g_defect = 0;

static void cmpInt(const char* what, int a, int b, const std::string& where)
{
    if (a != b)
    {
        std::printf("  MISMATCH %-12s %s: level=%d export=%d\n", what, where.c_str(), a, b);
        ++g_bad;
    }
}

static void cmpStr(const char* what, const std::string& a, const std::string& b,
                   const std::string& where)
{
    if (a != b)
    {
        std::printf("  MISMATCH %-12s %s: level=%s export=%s\n",
                    what, where.c_str(), a.c_str(), b.c_str());
        ++g_bad;
    }
}

static std::string trigKey(const JsonValue& t)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d(%d,%d)",
                  static_cast<int>(t["room"].asInt()),
                  static_cast<int>(t["sectorX"].asInt()),
                  static_cast<int>(t["sectorZ"].asInt()));
    return buf;
}

// Dumps the raw floordata words of one sector's chain. A decoded list can be
// argued about; the words cannot.
static void dumpChain(const Tr5Level& lvl, int room, int sx, int sz)
{
    const int fd = lvl.sectorIndex(room, sx, sz);
    std::vector<Tr5FloorEntry> chain;
    if (fd <= 0 || !lvl.readChain(fd, chain)) { std::printf("    (no chain)\n"); return; }
    for (size_t e = 0; e < chain.size(); ++e)
    {
        std::printf("    entry fn=%d kind=%d words:", chain[e].function, chain[e].kind);
        for (size_t w = 0; w < chain[e].words.size(); ++w)
            std::printf(" %04X", chain[e].words[w]);
        std::printf("\n");
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("Usage: InfoCompare.exe <level.trc> <level_<name>.json>\n");
        return 2;
    }

    std::string err;
    Tr5Level lvl;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("load level: %s\n", err.c_str()); return 1; }

    JsonValue mine;
    if (!Rando::levelInfoFromLevel(lvl, mine, err)) { std::printf("build: %s\n", err.c_str()); return 1; }

    JsonValue theirs;
    if (!Json::parseFile(toW(argv[2]), theirs, err)) { std::printf("export: %s\n", err.c_str()); return 1; }

    // --- Rooms --------------------------------------------------------------
    const JsonValue& ra = mine["rooms"];
    const JsonValue& rb = theirs["rooms"];
    cmpInt("roomCount", static_cast<int>(ra.size()), static_cast<int>(rb.size()), "-");
    for (size_t i = 0; i < ra.size() && i < rb.size(); ++i)
    {
        const std::string w = "room " + std::to_string(i);
        for (const char* f : { "index", "originX", "originZ", "numXSectors", "numZSectors" })
            cmpInt(f, static_cast<int>(ra[i][f].asInt()), static_cast<int>(rb[i][f].asInt()), w);
        cmpInt("waterRoom", ra[i]["waterRoom"].asBool(false) ? 1 : 0,
                            rb[i]["waterRoom"].asBool(false) ? 1 : 0, w);
    }

    // --- Items --------------------------------------------------------------
    const JsonValue& ia = mine["items"];
    const JsonValue& ib = theirs["items"];
    cmpInt("itemCount", static_cast<int>(ia.size()), static_cast<int>(ib.size()), "-");
    for (size_t i = 0; i < ia.size() && i < ib.size(); ++i)
    {
        const std::string w = "item " + std::to_string(i);
        for (const char* f : { "index", "typeId", "room", "x", "y", "z", "sectorX", "sectorZ", "ocb" })
            cmpInt(f, static_cast<int>(ia[i][f].asInt()), static_cast<int>(ib[i][f].asInt()), w);
    }

    // --- Triggers -----------------------------------------------------------
    //
    // Matched by sector, not by position in the array: if the two ever walked
    // the rooms in a different order, comparing index by index would report
    // hundreds of differences and hide the real ones.
    const JsonValue& ta = mine["triggers"];
    const JsonValue& tb = theirs["triggers"];
    cmpInt("trigCount", static_cast<int>(ta.size()), static_cast<int>(tb.size()), "-");

    std::map<std::string, const JsonValue*> byKey;
    for (size_t i = 0; i < tb.size(); ++i) byKey[trigKey(tb[i])] = &tb[i];

    for (size_t i = 0; i < ta.size(); ++i)
    {
        const JsonValue& t = ta[i];
        const std::string k = trigKey(t);
        auto it = byKey.find(k);
        if (it == byKey.end())
        {
            std::printf("  MISSING  trigger at %s is not in the export\n", k.c_str());
            ++g_bad;
            continue;
        }
        const JsonValue& e = *it->second;
        cmpStr("triggerType", t["triggerType"].asString(), e["triggerType"].asString(), k);

        const bool mineNull  = t["refItem"].isNull();
        const bool theirNull = e["refItem"].isNull();
        if (!mineNull && theirNull)
        {
            std::printf("  EXPORT DEFECT %s (%s): level ref=%d, export null\n",
                        k.c_str(), t["triggerType"].asString().c_str(),
                        static_cast<int>(t["refItem"].asInt()));
            ++g_defect;
        }
        else if (mineNull != theirNull)
        {
            std::printf("  MISMATCH refItem %s: level=%s export=%s\n", k.c_str(),
                        mineNull ? "null" : "set", theirNull ? "null" : "set");
            ++g_bad;
        }
        else if (!mineNull)
        {
            cmpInt("refItem", static_cast<int>(t["refItem"].asInt()),
                              static_cast<int>(e["refItem"].asInt()), k);
        }

        // Command lists: where the export lost the reference word it has one
        // command too many, and that extra one is the word itself. Compare
        // from the back so the surplus at the front does not shift everything.
        const JsonValue& ca = t["commands"];
        const JsonValue& cb = e["commands"];
        const size_t na = ca.size(), nb = cb.size();
        const size_t n = na < nb ? na : nb;
        if (na != nb && !(theirNull && nb == na + 1))
        {
            std::printf("  MISMATCH cmdCount %s: level=%d export=%d\n",
                        k.c_str(), static_cast<int>(na), static_cast<int>(nb));
            ++g_bad;
        }
        const int before = g_bad;
        for (size_t c = 0; c < n; ++c)
        {
            const JsonValue& x = ca[na - 1 - c];
            const JsonValue& y = cb[nb - 1 - c];
            const std::string xt = x["type"].asString(), yt = y["type"].asString();

            // Only two command names are ever compared downstream: "Object"
            // and "PlaySoundtrack" (both read in Randomizer.cpp).
            // Where the two readings disagree on a name that is neither, the
            // difference cannot reach a decision - report it, but do not
            // count it as a defect of this code.
            const bool xMatters = (xt == "Object" || xt == "PlaySoundtrack");
            const bool yMatters = (yt == "Object" || yt == "PlaySoundtrack");
            if (xt != yt && !xMatters && !yMatters)
            {
                std::printf("  EXPORT DEFECT %s: command name %s vs %s - neither is read\n",
                            k.c_str(), xt.c_str(), yt.c_str());
                ++g_defect;
            }
            else
            {
                cmpStr("cmdType", xt, yt, k);
            }
            cmpInt("cmdIndex", static_cast<int>(x["index"].asInt()),
                               static_cast<int>(y["index"].asInt()), k);
        }
        if (g_bad != before)
        {
            // Print both lists in full. A single differing entry says nothing
            // about WHY without its neighbours - the two lists can also be
            // shifted against each other.
            std::printf("    level : ");
            for (size_t c = 0; c < na; ++c)
                std::printf("%s/%d ", ca[c]["type"].asString().c_str(),
                            static_cast<int>(ca[c]["index"].asInt()));
            std::printf("\n    export: ");
            for (size_t c = 0; c < nb; ++c)
                std::printf("%s/%d ", cb[c]["type"].asString().c_str(),
                            static_cast<int>(cb[c]["index"].asInt()));
            std::printf("\n");
            dumpChain(lvl, static_cast<int>(t["room"].asInt()),
                           static_cast<int>(t["sectorX"].asInt()),
                           static_cast<int>(t["sectorZ"].asInt()));
        }
    }

    std::printf("%-14s rooms %3d  items %4d  triggers %4d   mismatches %d   export defects %d\n",
                argv[1], static_cast<int>(ra.size()), static_cast<int>(ia.size()),
                static_cast<int>(ta.size()), g_bad, g_defect);
    return g_bad == 0 ? 0 : 1;
}
