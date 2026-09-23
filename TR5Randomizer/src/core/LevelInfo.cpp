//----------------------------------------------------------------------------
// TR5 Randomizer
// Copyright (C) 2026 ChrisChanSan
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version. See the LICENSE file at the root of this source tree.
//----------------------------------------------------------------------------

#include "LevelInfo.h"

#include <cstdio>

namespace
{
    // The names the old export wrote, taken verbatim from its own tables so
    // the strings the consuming code compares against stay the same ones.
    // Anything outside the table was written as "Unknown" there, and is here.
    const char* triggerTypeName(int n)
    {
        static const char* names[] = {
            "Trigger", "Pad", "Switch", "Key", "Pickup",
            "HeavyTrigger", "Antipad", "Combat", "Dummy", "AntiTrigger",
            "HeavySwitch", "HeavyAntiTrigger", "Monkey", "Skeleton",
            "Tightrope", "Crawl", "Climb" };
        if (n < 0 || n >= static_cast<int>(sizeof(names) / sizeof(names[0]))) return "Unknown";
        return names[n];
    }

    const char* commandTypeName(int n)
    {
        static const char* names[] = {
            "Object", "Camera", "UnderwaterCurrent", "FlipMap",
            "FlipOn", "FlipOff", "LookAtItem", "EndLevel", "PlaySoundtrack",
            "Flipeffect", "SecretFound", "ClearBodies", "Flyby", "Cutscene" };
        if (n < 0 || n >= static_cast<int>(sizeof(names) / sizeof(names[0]))) return "Unknown";
        return names[n];
    }

    // global/types.h, enum floor_types.
    const int FN_TRIGGER = 4;

    // Item index and trigger command parameter are both the low 10 bits -
    // game/control.cpp reads every one of them as "& 0x3FF".
    const int LOW10 = 0x3FF;
}

bool Rando::levelInfoFromLevel(const Tr5Level& lvl, JsonValue& out, std::string& error)
{
    error.clear();
    out = JsonValue::makeObject();

    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (rooms.empty()) { error = "level contains no rooms"; return false; }

    // --- Rooms --------------------------------------------------------------
    JsonValue roomArr = JsonValue::makeArray();
    for (size_t i = 0; i < rooms.size(); ++i)
    {
        const Tr5Room& r = rooms[i];
        JsonValue o = JsonValue::makeObject();
        o.set("index",        JsonValue::makeInt(r.index));
        o.set("originX",      JsonValue::makeInt(r.originX));
        o.set("originZ",      JsonValue::makeInt(r.originZ));
        o.set("numXSectors",  JsonValue::makeInt(r.numXSectors));
        o.set("numZSectors",  JsonValue::makeInt(r.numZSectors));
        // Bit 0 of the room flags is "under water" (Tr5Room::flags).
        o.set("waterRoom",    JsonValue::makeBool((r.flags & 1u) != 0));
        roomArr.push(o);
    }
    out.set("rooms", roomArr);

    // --- Items --------------------------------------------------------------
    //
    // sectorX/sectorZ are not stored anywhere; they are the item's position
    // relative to its room origin, in sectors. The same arithmetic is already
    // in production in Patch::apply (ApplyPatch.cpp) - do not invent a
    // second one.
    JsonValue itemArr = JsonValue::makeArray();
    const int nItems = lvl.itemCount();
    for (int i = 0; i < nItems; ++i)
    {
        Tr5Item it;
        if (!lvl.getItem(i, it)) { error = "item " + std::to_string(i) + " unreadable"; return false; }

        int sectorX = 0, sectorZ = 0;
        if (it.roomNumber >= 0 && it.roomNumber < static_cast<int>(rooms.size()))
        {
            const Tr5Room& r = rooms[static_cast<size_t>(it.roomNumber)];
            sectorX = (it.x - r.originX) / 1024;
            sectorZ = (it.z - r.originZ) / 1024;
        }

        JsonValue o = JsonValue::makeObject();
        o.set("index",   JsonValue::makeInt(i));
        o.set("typeId",  JsonValue::makeInt(it.objectNumber));
        o.set("room",    JsonValue::makeInt(it.roomNumber));
        o.set("x",       JsonValue::makeInt(it.x));
        o.set("y",       JsonValue::makeInt(it.y));
        o.set("z",       JsonValue::makeInt(it.z));
        // OCB is the low 6 bits of the raw trigger_flags field - same mask as
        // Patch::apply (ApplyPatch.cpp) and main (Randomize.cpp).
        o.set("ocb",     JsonValue::makeInt(it.triggerFlags & 0x3F));
        o.set("flags",   JsonValue::makeInt(static_cast<uint16_t>(it.flags)));
        o.set("sectorX", JsonValue::makeInt(sectorX));
        o.set("sectorZ", JsonValue::makeInt(sectorZ));
        itemArr.push(o);
    }
    out.set("items", itemArr);

    // --- Triggers -----------------------------------------------------------
    //
    // Walked room by room, sector by sector, in the sector field's own order
    // (x-major, see Tr5Level::sectorIndex) so the list comes out in the same
    // order the export had.
    //
    // refItem is the reference word of a Switch/Key/Pickup trigger - the item
    // that has to be operated or collected. The old export got this wrong:
    // all eleven pickup triggers across the campaign carried "refItem": null
    // there and pushed the word into the command list as a fake Object
    // command instead, so Randomizer.cpp had to guess it back from the
    // trigger's own sector. Here the word is simply read, which is why that
    // guess can go: Tr5Level::readChain already separates the reference word
    // from the commands, exactly as the engine does.
    JsonValue trigArr = JsonValue::makeArray();
    for (size_t ri = 0; ri < rooms.size(); ++ri)
    {
        const Tr5Room& r = rooms[ri];
        if (r.sectorsOffset == 0) continue;

        for (int sx = 0; sx < r.numXSectors; ++sx)
        for (int sz = 0; sz < r.numZSectors; ++sz)
        {
            const int fd = lvl.sectorIndex(r.index, sx, sz);
            if (fd <= 0) continue;

            std::vector<Tr5FloorEntry> chain;
            if (!lvl.readChain(fd, chain)) continue;   // damaged chain: skip, as the export did

            for (size_t e = 0; e < chain.size(); ++e)
            {
                const Tr5FloorEntry& ent = chain[e];
                if (ent.function != FN_TRIGGER) continue;
                if (ent.words.size() < 2) continue;

                JsonValue t = JsonValue::makeObject();
                t.set("triggerType", JsonValue::makeString(triggerTypeName(ent.kind)));
                t.set("room",    JsonValue::makeInt(r.index));
                t.set("sectorX", JsonValue::makeInt(sx));
                t.set("sectorZ", JsonValue::makeInt(sz));

                // words[0] header, words[1] setup, then the reference word if
                // this kind has one, then the commands.
                size_t w = 2;
                if (Tr5Floor::hasRefWord(ent.kind) && ent.words.size() > w)
                    t.set("refItem", JsonValue::makeInt(ent.words[w++] & LOW10));
                else
                    t.set("refItem", JsonValue());     // null, as the export wrote it

                JsonValue cmds = JsonValue::makeArray();
                for (; w < ent.words.size(); ++w)
                {
                    const uint16_t c = ent.words[w];
                    const int act = Tr5Floor::cmdType(c);

                    JsonValue cmd = JsonValue::makeObject();
                    cmd.set("type",  JsonValue::makeString(commandTypeName(act)));
                    cmd.set("index", JsonValue::makeInt(c & LOW10));
                    cmds.push(cmd);

                    // Camera (1) and flyby (12) carry one extra word that is
                    // not a command of its own - readChain stored it right
                    // behind, so step over it here.
                    if (act == 1 || act == 12) ++w;
                    if (c & 0x8000) break;
                }
                t.set("commands", cmds);
                trigArr.push(t);
            }
        }
    }
    out.set("triggers", trigArr);
    return true;
}
