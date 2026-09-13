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
#include "ApplyPatch.h"
#include "Tr3Source.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <map>
#include <utility>

namespace
{
    std::string msg(const char* f, int a, int b = 0)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), f, a, b);
        return std::string(buf);
    }

    //------------------------------------------------------------------------
    // #147 - the flipmap partner of a room.
    //
    // FlipMap() (game/control.cpp:2634) swaps the two ROOM_INFO structs
    // wholesale and then hands the base room its own item list back. So the
    // ITEMS stay where they are, but the FLOORDATA is exchanged: after the
    // flip the base room index reads the alternate's sector chains. A
    // trigger that exists in only one of the two variants is dead for the
    // rest of the flip - and nothing forces a flip to ever be turned back
    // (joby5 group 0 is switched on by the intro cutscene and has no
    // FLIPOFF anywhere in the level).
    //
    // altRoom points from the BASE room to its alternate; the alternate
    // itself carries -1, so the way back has to be searched. Both
    // directions are covered because it costs nothing - today's placement
    // only ever names base rooms (the zone export lists no alternates).
    //------------------------------------------------------------------------
    int flipPartner(const Tr5Level& level, int room)
    {
        const std::vector<Tr5Room>& rooms = level.rooms();
        if (room < 0 || room >= static_cast<int>(rooms.size())) return -1;
        if (rooms[static_cast<size_t>(room)].altRoom >= 0)
            return rooms[static_cast<size_t>(room)].altRoom;
        for (const Tr5Room& r : rooms)
            if (r.altRoom == room) return r.index;
        return -1;
    }

    // Lowest and highest floor corner of a sector. false means: wall,
    // sector does not exist, or the chain is unreadable - in every one of
    // those cases the caller has NO height, not a wrong one.
    bool sectorFloorRange(const Tr5Level& level, int room, int sx, int sz, long& lo, long& hi)
    {
        bool any = false;
        for (int cx = 0; cx <= 1024; cx += 1024)
            for (int cz = 0; cz <= 1024; cz += 1024)
            {
                long y = 0;
                if (!level.cornerFloor(room, sx, sz, cx, cz, y)) return false;
                if (!any) { lo = hi = y; any = true; }
                else { if (y < lo) lo = y; if (y > hi) hi = y; }
            }
        return any;
    }

    // How far the partner tile's floor may sit away from the base tile's
    // before the copy is refused. One sector height is the measured room:
    // in joby5 - the level the whole thing was found in - all nine cases
    // of the user's run come out at exactly dy = 0, and a flip that lifts
    // a tile by more than a full sector no longer shows the same place.
    const long kFlipFloorTolerance = 1024;

    // May the flipmap partner take a second copy of a trigger on this
    // sector? nullptr means yes, otherwise the obstacle in plain words.
    //
    // One place for both users - the new enemy waker (triggerAdds) and the
    // travelling pickup trigger (triggerMoves) ask the identical question,
    // and two copies of it would be two chances to answer it differently.
    const char* flipTileTakesTrigger(const Tr5Level& level, int room, int twin, int sx, int sz)
    {
        const std::vector<Tr5Room>& rooms = level.rooms();
        if (room < 0 || room >= static_cast<int>(rooms.size())) return "base room does not exist";
        if (twin < 0 || twin >= static_cast<int>(rooms.size())) return "partner room does not exist";

        // Copied, not referenced: the caller writes to the level right
        // after this, and a reference into level.rooms() across that is a
        // trap waiting for the day the rooms are reparsed.
        const Tr5Room a = rooms[static_cast<size_t>(room)];
        const Tr5Room b = rooms[static_cast<size_t>(twin)];

        long bLo = 0, bHi = 0, tLo = 0, tHi = 0;
        std::vector<Tr5FloorEntry> twinChain;
        const int twinIdx = level.sectorIndex(twin, sx, sz);

        // Same sector index only means the same place while both rooms
        // start at the same world corner. Vanilla flip pairs always do, but
        // that is a measurement, not a law.
        if (a.originX != b.originX || a.originZ != b.originZ)
            return "partner room starts at another world corner";
        if (twinIdx < 0)
            return "partner room has no such sector";
        if (!sectorFloorRange(level, twin, sx, sz, tLo, tHi))
            return "wall";
        if (!sectorFloorRange(level, room, sx, sz, bLo, bHi))
            return "base tile has no readable floor";
        if (std::labs(tLo - bLo) > kFlipFloorTolerance ||
            std::labs(tHi - bHi) > kFlipFloorTolerance)
            return "height";
        if (!level.readChain(twinIdx, twinChain))
            return "partner floordata unreadable";
        return Tr5Level::canAppendTrigger(twinChain);
    }

    // Does this sector carry a trigger at all? Used only to report the
    // one-sided removal of a travelling trigger - see the moves loop.
    bool sectorHasTrigger(const Tr5Level& level, int room, int sx, int sz)
    {
        const int idx = level.sectorIndex(room, sx, sz);
        if (idx <= 0) return false;
        std::vector<Tr5FloorEntry> chain;
        if (!level.readChain(idx, chain)) return false;
        for (const Tr5FloorEntry& e : chain)
            if (e.function == 4 /* FN_TRIGGER */) return true;
        return false;
    }
}

long Patch::shuffleMusicTriggers(Tr5Level& level, uint64_t seed)
{
    // GLOBAL (user wish): no level-internal permutation of the existing
    // tracks, but ONE seed permutation over a fixed catalog (below) that
    // every level applies the same way - jingles thus travel across level
    // borders (rich3 sounds in joby2), yet the same track sounds the same
    // everywhere. And the CLI needs no collection phase over foreign levels
    // it does not even know.
    //
    // The atmospheres stay their own pot, because the engine treats them
    // differently (TriggerNormalCDTrack, control.cpp: 117/118/121/123..130
    // switch the running backdrop, everything else are one-shot jingles
    // with a cd_flags lock) - a mood change stays a mood change.
    const auto isAtmo = [](int t)
    {
        return t == 117 || t == 118 || t == 121 || (t >= 123 && t <= 130);
    };

    // The jingle pot is NOT the whole track space, but only what some
    // trigger in the game actually fires - measured with MusicCheck over
    // all 15 vanilla levels (2026-08-22, union of the TO_CD tracks, minus
    // the atmospheres). That keeps cutscene dialogue, title music, credits
    // lead-in, FMV-adjacent tracks and the tutorial voice lines (TL_*, they
    // only run via flipeffects anyway) out: a jingle trigger never plays a
    // 70-second dialogue (heard and requested by the user). The few voice
    // lines triggered inside levels (1, 26, 42, 47, 48, 52, 54, 59, 67, 79,
    // 90, 100) are in - those are level content, not cutscenes.
    static const int kJingles[] =
    {
        1, 8, 10, 11, 14, 15, 16, 17, 18, 21, 24, 25, 26, 27, 28, 29, 31, 32,
        33, 34, 36, 37, 38, 40, 41, 42, 44, 45, 47, 48, 49, 52, 53, 54, 55,
        57, 59, 61, 62, 63, 65, 66, 67, 70, 72, 74, 75, 78, 79, 80, 88, 90,
        100, 104, 111, 112
    };

    std::vector<int> atmo, jingle(std::begin(kJingles), std::end(kJingles));
    for (int t = 0; t < 136; ++t)
        if (isAtmo(t))
            atmo.push_back(t);

    std::mt19937_64 rng(seed ^ 0x54524B4D5553ull);      // "TRKMUS"

    std::map<int, int> mapping;
    const auto shufflePot = [&](std::vector<int>& pot)
    {
        std::vector<int> target = pot;
        std::shuffle(target.begin(), target.end(), rng);
        for (size_t i = 0; i < pot.size(); ++i)
            mapping[pot[i]] = target[i];
    };
    shufflePot(atmo);
    shufflePot(jingle);

    return level.musicTriggers(&mapping, nullptr);
}

void Patch::markTriggerSafeSectors(const Tr5Level& level, RandoInput& in)
{
    // Pierre combat set check: if the level carries slot 47 only as the
    // CUTSCENE variant (death anim +12 there does NOT have state 5 -
    // Andrea2, Trajan finale), the pool must never place him here. The
    // slot stays untouched, the cutscene needs it.
    {
        Tr5Level::MoveableInfo mi;
        if (level.moveableInfo(47, mi) && mi.animIndex >= 0)
        {
            size_t animAt; int32_t nAnims;
            if (level.debugAnimBlock(animAt, nAnims) && mi.animIndex + 12 < nAnims)
            {
                uint16_t state = 0;
                std::memcpy(&state,
                            level.debugBytes().data() + animAt + (static_cast<size_t>(mi.animIndex) + 12) * 40 + 6, 2);
                if (state != 5)
                    in.forbiddenTypes.insert(47);
            }
        }
    }

    // Which model slots does the level carry at all? Item abundance may
    // only scatter what can be drawn - a pickup without a model in the
    // level would be nothing but an invisible stumbling block.
    {
        const std::vector<int> slots = level.moveableSlots();
        in.presentSlots.insert(slots.begin(), slots.end());
    }

    // Weather (#144). Both numbers come out of the file for the same reason
    // the slots above do: the level itself is the only honest source, and a
    // typed table would drift the moment a level is edited.
    in.weatherRooms = level.weatherRooms();
    in.vanillaWeather = level.weatherType();

    // Collect the slope entries of all placement sectors - ALWAYS, even if
    // no trigger pass is needed below: every item placement needs them for
    // height and walkability (RandoInput::slopes).
    {
        SlopeInfo si;
        int baseY = 0;
        for (const ZoneSector& s : in.sectors)
        {
            const int kind = level.floorSlope(s.room, s.sectorX, s.sectorZ, si, baseY);
            if (kind == 0) continue;                         // flat: no entry needed
            if (kind < 0) { si = SlopeInfo(); si.typeWord = kSlopeUnreadable; }
            in.slopes[secKey(s.room, s.sectorX, s.sectorZ)] = si;
        }
    }

    // Headroom for enemies (#141, RandoInput::sectorCeiling / typeHeight).
    // Also ALWAYS, for the same reason as the slopes above: this is the one
    // place where the real level file and the placement input are both at
    // hand, and both halves of the height rule have to come out of files.
    {
        for (const ZoneSector& s : in.sectors)
        {
            long c = 0;
            if (level.sectorCeilingLow(s.room, s.sectorX, s.sectorZ, c))
                in.sectorCeiling[secKey(s.room, s.sectorX, s.sectorZ)] = c;
        }

        // The existing items' sectors as well, and not only the placement
        // pool: the enemy SWAP happens at Core's own positions, and those
        // need not lie in the pool at all (Andrea1's statue in room 222 does
        // not). Without them the rule would silently pass exactly the places
        // it was written for.
        for (const LevelItem& it : in.items)
        {
            const long long k = secKey(it.room, it.sectorX, it.sectorZ);
            if (in.sectorCeiling.count(k)) continue;
            long c = 0;
            if (level.sectorCeilingLow(it.room, it.sectorX, it.sectorZ, c))
                in.sectorCeiling[k] = c;
        }

        // The rest pose of every model the level carries. Enemies are what
        // this is for, but filtering here would need a second copy of the
        // kind list - the placement asks by type anyway.
        for (int slot : level.moveableSlots())
        {
            int16_t b[6] = {};
            if (!level.frameBounds(slot, b)) continue;
            const int h = b[3] - b[2];                   // maxY - minY, y grows downward
            if (h > 0) in.typeHeight[slot] = h;
        }
    }

    if (in.coupledTriggers.empty() && in.enemyTypes.empty()) return;

    std::vector<Tr5FloorEntry> chain;
    for (const ZoneSector& s : in.sectors)
    {
        const int idx = level.sectorIndex(s.room, s.sectorX, s.sectorZ);
        if (idx < 0) continue;
        if (!level.readChain(idx, chain)) continue;          // unreadable: hands off
        if (Tr5Level::canAppendTrigger(chain) != nullptr) continue;
        in.triggerSafeSectors.insert(secKey(s.room, s.sectorX, s.sectorZ));
    }
}

//----------------------------------------------------------------------------
// The height reader for models that are not in the level yet (#141).
//
// Deliberately no table of numbers anywhere: the donor file is opened and
// the model measured, exactly as the import will read it a moment later. The
// two caches are what makes that affordable - a full run asks thirteen
// levels about the same handful of donors, and the guests from Tomb Raider 3
// come out of the package cache the import fills anyway (#103).
//----------------------------------------------------------------------------
std::function<bool(const char*, const char*, int, int&)>
Patch::modelHeightFn(LevelSourceFn levelSource, void* levelSourceUser,
                     const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                     const std::wstring& cacheDir)
{
    // Shared by every level of the run. A donor that could not be opened is
    // remembered as a null entry, so it is not retried thirteen times.
    struct Cache
    {
        std::map<std::string, std::shared_ptr<Tr5Level>> donors;
        std::map<int, int> guests;          // guest slot -> height, -1 = unreachable
    };
    std::shared_ptr<Cache> cache = std::make_shared<Cache>();
    const std::wstring d3 = tr3Dir, dg = tr3GoldDir, dc = cacheDir;

    return [cache, levelSource, levelSourceUser, d3, dg, dc]
           (const char* fromLevel, const char* tr3, int slot, int& height) -> bool
    {
        height = 0;

        // --- A guest from Tomb Raider 3 -------------------------------------
        if (tr3 && *tr3)
        {
            std::map<int, int>::const_iterator c = cache->guests.find(slot);
            if (c != cache->guests.end())
            { if (c->second < 0) return false; height = c->second; return true; }

            const Tr3Source::DinoSource* d = nullptr;
            static const std::vector<Tr3Source::DinoSource> all = Tr3Source::dinos();
            for (const Tr3Source::DinoSource& k : all) if (k.slot == slot) { d = &k; break; }

            int h = -1;
            if (d && !d3.empty())
            {
                std::vector<uint8_t> model, sounds;
                std::string err;
                int16_t b[6] = {};
                if (Tr3Source::fetchCached(*d, d3, dg, dc, model, sounds, err) &&
                    Tr3Source::packageFrameBounds(model, b) && b[3] > b[2])
                    h = b[3] - b[2];
            }
            cache->guests[slot] = h;
            if (h < 0) return false;
            height = h;
            return true;
        }

        // --- A donor level of our own ---------------------------------------
        if (!fromLevel || !*fromLevel || !levelSource) return false;

        const std::string key(fromLevel);
        std::map<std::string, std::shared_ptr<Tr5Level>>::const_iterator f = cache->donors.find(key);
        if (f == cache->donors.end())
        {
            std::shared_ptr<Tr5Level> lvl;
            const std::wstring path = levelSource(key, levelSourceUser);
            if (!path.empty())
            {
                std::shared_ptr<Tr5Level> tmp = std::make_shared<Tr5Level>();
                std::string err;
                if (tmp->load(path, err)) lvl = tmp;
            }
            f = cache->donors.insert(std::make_pair(key, lvl)).first;
        }
        if (!f->second) return false;

        int16_t b[6] = {};
        if (!f->second->frameBounds(slot, b)) return false;
        if (b[3] <= b[2]) return false;                  // stub without a measure
        height = b[3] - b[2];
        return true;
    };
}

bool Patch::apply(Tr5Level& level, const JsonValue& patch, PatchReport& report, std::string& error,
                  LevelSourceFn levelSource, void* levelSourceUser,
                  const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                  const std::wstring& cacheDir)
{
    report = PatchReport();

    if (patch.has("itemRemoves"))
    {
        // A clear statement instead of silent ignorance: whoever sets the
        // field expects an effect. Removing would shift all subsequent item
        // indices and leave the triggers' "Object N" references dangling -
        // the level would be broken without any visible error message.
        error = "The patch contains itemRemoves. Removing items shifts all "
                "subsequent indices and destroys the trigger mapping - "
                "this is deliberately unsupported.";
        return false;
    }

    if (patch.has("triggerRemoves"))
    {
        // The same statement as for itemRemoves, for the same reason: a
        // trigger nobody fires anymore is a door that never opens. Moving
        // yes, removing no.
        error = "The patch contains triggerRemoves. Removing triggers is "
                "deliberately unsupported - only moving (triggerMoves).";
        return false;
    }

    // --- Move triggers ---------------------------------------------------
    //
    // Deliberately before the item changes: that way the floordata is fixed
    // before anything is written to the items. The order is not strictly
    // necessary - appendChain adjusts the item offset itself - but it makes
    // the flow unambiguous when reading back.
    const JsonValue& moves = patch["triggerMoves"];
    for (size_t i = 0; i < moves.size(); ++i)
    {
        const JsonValue& m = moves[i];
        const int fRoom = static_cast<int>(m["fromRoom"].asInt(-1));
        const int fSx   = static_cast<int>(m["fromSectorX"].asInt(-1));
        const int fSz   = static_cast<int>(m["fromSectorZ"].asInt(-1));
        const int mRoom = static_cast<int>(m["toRoom"].asInt(-1));
        const int mSx   = static_cast<int>(m["toSectorX"].asInt(-1));
        const int mSz   = static_cast<int>(m["toSectorZ"].asInt(-1));

        std::string moveErr;
        if (!level.moveTrigger(fRoom, fSx, fSz, mRoom, mSx, mSz, moveErr))
        {
            // Abort hard, do not warn: if the item travels but the trigger
            // does not, the key lies elsewhere and its effect stays behind
            // at the old spot. A half-applied patch would be worse than
            // none.
            error = "triggerMoves[" + std::to_string(i) + "]: " + moveErr;
            return false;
        }
        ++report.triggersMoved;

        // --- The travelling trigger into the flipmap partner too (#147b) ---
        //
        // The same reasoning as for the enemy waker below, only sharper: a
        // waker that never fires costs one sleeping enemy, a KEY whose
        // pickup trigger lies in the wrong variant of the room costs the
        // key. The trigger is copied out of its NEW home, so the partner
        // gets exactly the entry that now lies there - reference word,
        // commands and all.
        //
        // Not fatal on failure, and for the same reason as with the waker:
        // the move itself has succeeded, so the trigger works in the
        // unflipped variant. Aborting the whole patch over a walled partner
        // tile would throw away a level that is in itself correct. The skip
        // is counted and named.
        const int mTwin = flipPartner(level, mRoom);
        if (mTwin >= 0)
        {
            const char* why = flipTileTakesTrigger(level, mRoom, mTwin, mSx, mSz);

            std::string mirErr;
            if (!why && !level.copyTrigger(mRoom, mSx, mSz, mTwin, mSx, mSz, mirErr))
                why = mirErr.c_str();

            if (why)
            {
                ++report.triggersMoveMirrorSkipped;
                char nb[192];
                std::snprintf(nb, sizeof(nb),
                              "flipmap: moved trigger %d,%d,%d NOT mirrored into room %d - %s.",
                              mRoom, mSx, mSz, mTwin, why);
                report.warnings.push_back(nb);
            }
            else
            {
                ++report.triggersMoveMirrored;
                char nb[192];
                std::snprintf(nb, sizeof(nb),
                              "flipmap: moved trigger now stands on %d,%d,%d AND in partner room %d.",
                              mRoom, mSx, mSz, mTwin);
                report.warnings.push_back(nb);
            }
        }

        // The other side of the move is deliberately NOT mirrored: the
        // trigger is taken out of the source sector only, even when that
        // room has a partner.
        //
        // Measured over all fifteen vanilla levels (FlipProbe, 2026-09-04):
        // not one PICKUP trigger that lies in a room with a flipmap partner
        // has a word-identical twin on the partner's same sector. The only
        // pickup that exists in both variants at all is Andy2's item 29
        // (rooms 30/113, sector 4,5), and the two entries differ - the base
        // one carries an Object command, the alternate one does not. So
        // there is no "second copy of ours" to take away; deleting anything
        // there would delete a DIFFERENT vanilla trigger, which is exactly
        // what this file refuses to do a few lines above (triggerRemoves).
        //
        // Today's single coupled item cannot even reach the case: Andrea1's
        // key 107 hangs on a pickup trigger in room 192, and room 192 has no
        // partner. If the curation ever adds one that does, the run says so
        // here instead of leaving it to be discovered in game.
        const int fTwin = flipPartner(level, fRoom);
        if (fTwin >= 0 && sectorHasTrigger(level, fTwin, fSx, fSz))
        {
            char nb[224];
            std::snprintf(nb, sizeof(nb),
                          "flipmap: the trigger left %d,%d,%d, but partner room %d carries one on "
                          "the same sector - it stays (vanilla data is never removed).",
                          fRoom, fSx, fSz, fTwin);
            report.warnings.push_back(nb);
        }
    }

    if (report.triggersMoveMirrored > 0 || report.triggersMoveMirrorSkipped > 0)
    {
        char nb[160];
        std::snprintf(nb, sizeof(nb),
                      "flipmap: %d moved triggers mirrored into the partner room, %d skipped (wall/height).",
                      report.triggersMoveMirrored, report.triggersMoveMirrorSkipped);
        report.warnings.push_back(nb);
    }

    // Donor levels are needed by outfit AND soundAdds and are several
    // megabytes - each is loaded at most once.
    std::map<std::string, Tr5Level> sources;
    auto source = [&](const std::string& name) -> Tr5Level* {
        auto it = sources.find(name);
        if (it != sources.end()) return &it->second;
        if (!levelSource) return nullptr;
        const std::wstring path = levelSource(name, levelSourceUser);
        std::string loadErr;
        Tr5Level q;
        if (path.empty() || !q.load(path, loadErr)) return nullptr;
        return &sources.emplace(name, std::move(q)).first->second;
    };

    // --- Outfit ----------------------------------------------------------
    //
    // Slots 8/9/10 (LARA_SKIN, LARA_SKIN_JOINTS, LARA_SCREAM) from the
    // donor, plus its LaraType byte. NEVER slot 0 - it carries the level's
    // animations (experienced in game: missing switch and crawl
    // animations). The donor rule comes from the weapon work and has been
    // played in all 13 levels.
    //
    // There is no half outfit: if one of the three slots fails, the patch
    // aborts BEFORE saving - the level then remains the untouched copy from
    // the backup.
    const JsonValue& outfit = patch["outfit"];

    // #123 S7: the HD outfit marker starts at "none" for every run that
    // decides an outfit at all, and the guest branch far below sets it.
    // Guarded by has("outfit") for the same reason the weather is: apply()
    // runs a SECOND time for the secret list, and that patch carries no
    // outfit - without the guard the second pass would wipe the marker the
    // first one had just written.
    //
    // The one case this does not reach is a run whose roll left the level
    // its OWN outfit, because then the randomizer puts no "outfit" into the
    // patch at all. That is harmless as long as the source file is a
    // vanilla one, where data[3] is 0 - and the checksum gate exists to
    // make sure it is.
    if (patch.has("outfit")) level.setHdOutfit(0);

    /* AND THE WARDROBE KEY BESIDE IT (#153, round M2). The roll writes the
       key of whatever it chose - a scanned entry, one of the three vanilla
       outfits or one of the four guests - and it goes into the sidecar as
       `wear`. For the seven that have a header byte as well, the file then
       says the same thing twice and the engine reads the two against each
       other; for an entry out of the scan it is the only place the choice can
       be written at all.

       Guarded and cleared exactly like the marker above, and for the same
       reason: apply() runs a second time for the secret list. */
    if (patch.has("outfit")) level.hdSetWear(std::string());

    if (outfit.has("wear")) level.hdSetWear(outfit["wear"].asString());

    if (outfit.has("fromLevel"))
    {
        const std::string from = outfit["fromLevel"].asString();
        Tr5Level* donor = source(from);
        if (!donor)
        {
            report.warnings.push_back("outfit: donor level " + from + " not loadable - outfit stays.");
        }
        else
        {
            // Skin, joints, scream head - and MANDATORY entourage:
            //
            // Hair (30): the hair model belongs to the head. joby2's Lara
            // wears a cap, her hair object is built for it; a foreign braid
            // on this head spikes through the body (seen in game).
            //
            // Holsters (13-16): with adult Lara, DrawLara__1 draws the two
            // thighs ADDITIONALLY from the holster objects
            // (drawlara.cpp:227ff). The rich and andy levels do not even
            // have these objects - objects[].mesh_index is 0 then, and a
            // foreign mesh lands on the left thigh, slightly offset (seen
            // in game). The catsuit path (DrawLara__4) needs no holsters,
            // which is why rich2 has none to offer - for it the list simply
            // ends after the mandatory slots.
            std::vector<int> slots = { 8, 9, 10, 30 };
            for (int s : { 13, 14, 15, 16 })
            {
                Tr5Level::MoveableInfo mi;
                if (donor->moveableInfo(s, mi)) slots.push_back(s);
            }

            // Speech heads (17-20): in dialogue cutscenes the engine swaps
            // Lara's head for LARA_SPEECH_HEAD1-4 (deltapak.cpp,
            // handle_lara_chatting) - they carry the level's VANILLA face
            // (seen in game: catsuit with a knit cap in joby5). If the
            // donor has its own, they travel along like the holsters. rich2
            // has NONE - the VCI levels know no dialogue cutscenes; then
            // the target gets the foreign-heads flag (data[2] in the level
            // header) and the engine skips the swap.
            bool talkHeadsPresent = true;
            for (int s : { 17, 18, 19, 20 })
            {
                Tr5Level::MoveableInfo mi;
                if (!donor->moveableInfo(s, mi)) { talkHeadsPresent = false; break; }
            }
            if (talkHeadsPresent)
                for (int s : { 17, 18, 19, 20 }) slots.push_back(s);

            for (int slot : slots)
            {
                std::string oErr;
                if (!level.copyMoveableFrom(*donor, slot, oErr))
                {
                    error = "outfit slot " + std::to_string(slot) + " from " + from + ": " + oErr;
                    return false;
                }
                level.hdMarkDonor(slot, slot, from);
            }

            // The LaraType byte is our outfit marker for the engine:
            // hair.cpp and laraskin.cpp key the braid special-handling
            // (anchoring AND the weld table of the braid base) off it -
            // Core had both hanging off the level name, because in vanilla
            // the outfit never crosses a section. Without the marker,
            // rich1's braid stuck to the cap's head vertex numbers (seen in
            // game). The decision is made in the randomizer ("laraType" in
            // the patch, see there: 2 = Joby outfit everywhere, 5 = Andrea
            // outfit in a Russia level); the fallback for hand-written
            // patches without the field remains the old Joby rule.
            if (outfit.has("laraType"))
                level.setLaraType(static_cast<int>(outfit["laraType"].asInt(donor->laraType())));
            else
                level.setLaraType(from == "joby2" ? 2 : donor->laraType());
            level.setForeignTalkHeads(talkHeadsPresent ? 0 : 1);
            report.outfitFrom = from;
        }
    }

    // --- An outfit from Tomb Raider 1-3 (#122) -----------------------------
    //
    // Beside the vanilla path above this one is small, and deliberately so:
    // ONLY the skin (slot 8) is replaced, and even there the HEAD stays the
    // level's own.
    //
    // That is not tidiness, it is necessary. TR5 rewrites
    // lara.mesh_ptrs[LM_HEAD] at RUNTIME - the scream head in
    // larafire.cpp:405, the speech heads out of the cutscene pak - so a
    // foreign head pops out mid-game the moment Lara screams. And the braid
    // takes its anchor point AND its collision radius from the bounding
    // sphere of whatever mesh sits there (hair.cpp:245). Keeping the head
    // keeps six heads and the braid right by itself, and joints, holsters
    // and speech heads never have to be touched at all.
    //
    // The seams of the joint shell are the one thing that does need saying,
    // because TR5's joints were built for TR5's body. Which ones draw
    // nothing and which get a flat colour stands in the OutfitSource - it
    // was judged in game, outfit by outfit.
    if (outfit.has("tr3"))
    {
        const int LARA_SKIN = 8, LARA_SKIN_JOINTS = 9, LM_HEAD = 14;

        const std::string id = outfit["tr3"].asString();
        static const std::vector<Tr3Source::OutfitSource> all = Tr3Source::outfits();
        const Tr3Source::OutfitSource* o = 0;
        for (size_t k = 0; k < all.size(); ++k) if (id == all[k].id) { o = &all[k]; break; }
        if (!o) { error = "no outfit known by the name " + id; return false; }

        std::vector<uint8_t> model;
        std::string f;
        if (!Tr3Source::fetchOutfitCached(*o, tr3Dir, tr3GoldDir, cacheDir, model, f))
        { error = f; return false; }

        // APPEND textures, never shift - the texture page rule holds here as
        // it does for the guests.
        std::map<int,int> texMap;
        if (!level.addTexturesFromTr3(model, texMap, f))
        { error = std::string(o->name) + " (textures): " + f; return false; }
        if (Tr3Source::remapTextures(model, texMap) < 0)
        { error = std::string(o->name) + ": faces not readable"; return false; }

        const std::vector<int> keep(1, LM_HEAD);
        if (!level.addMoveableFromTr3(model, LARA_SKIN, -1, f, &keep))
        { error = std::string(o->name) + " (model): " + f; return false; }

        // Seam n sits in mesh n+1 of the joint shell (laraskin.cpp:
        // jointMeshNum = i + 1). A seam that cannot be treated is a warning,
        // not an abort: the outfit is already in and looks right, one seam
        // wearing a foreign texture is not worth throwing the level away.
        for (size_t k = 0; k < o->dropSeams.size(); ++k)
            if (!level.clearMeshFaces(LARA_SKIN_JOINTS, o->dropSeams[k] + 1, f))
                report.warnings.push_back("outfit: seam " + std::to_string(o->dropSeams[k]) +
                                          " not dropped - " + f);

        for (size_t k = 0; k < o->seamColors.size(); ++k)
        {
            const Tr3Source::OutfitSource::SeamColor& sc = o->seamColors[k];
            int used = 0, gr = 0, gg = 0, gb = 0;
            if (!level.paintMeshFlat(LARA_SKIN_JOINTS, sc.seam + 1, sc.r, sc.g, sc.b,
                                     used, gr, gg, gb, f))
                report.warnings.push_back("outfit: seam " + std::to_string(sc.seam) +
                                          " not painted - " + f);
        }

        // The LaraType byte is deliberately NOT touched. It steers the
        // braid's special handling, and the braid hangs on the head - which
        // is the level's own. Whatever was right before is still right.
        //
        // But data[3] IS written (#123, stage S7), and it exists because of
        // exactly that: LaraType says LARA_PONYTAIL here, and it says the
        // same for the Rome body in a Russia level. The engine's HD path
        // cannot tell the two apart from the file and stays classic for
        // both; this byte is the one thing that can tell it. Nothing else
        // reads it, so a level whose outfit has no HD twin gets 0 and stays
        // classic - which is what it would have done anyway.
        //
        // No donor line for slot 8: Lara's HD body is chosen by the outfit
        // path, never by a slot name (see Patch::writeHdMarkers).
        report.hdOutfit = hdOutfitId(id);
        if (!level.setHdOutfit(report.hdOutfit))
        { error = std::string(o->name) + ": the level header could not be written."; return false; }

        report.outfitFrom = o->name;
        report.warnings.push_back(std::string("outfit fetched from Tomb Raider 3: ") + o->name);
    }

    // Retrofit holsters where the outfit brings none. The catsuit donor
    // rich2 does not have slots 13-16 (its draw path never needed them);
    // since the engine draws them on the catsuit too, they should exist
    // everywhere. Only missing slots are filled - existing ones (say,
    // Andrea1's own) stay.
    if (outfit.has("holstersFrom"))
    {
        const std::string from = outfit["holstersFrom"].asString();
        Tr5Level* donor = source(from);
        if (!donor)
            report.warnings.push_back("holsters: donor level " + from + " not loadable.");
        else
            for (int s : { 13, 14, 15, 16 })
            {
                Tr5Level::MoveableInfo mi;
                if (level.moveableInfo(s, mi)) continue;       // already has it
                if (!donor->moveableInfo(s, mi)) continue;   // not even the donor has it
                std::string oErr;
                if (!level.copyMoveableFrom(*donor, s, oErr))
                {
                    error = "holster slot " + std::to_string(s) + " from " + from + ": " + oErr;
                    return false;
                }
                level.hdMarkDonor(s, s, from);
            }
    }

    // --- Weather (#144) ---------------------------------------------------
    //
    // Two bytes in the header block between the texture blocks and the main
    // block, right beside the LaraType byte written above - which is why it
    // stands here and not somewhere among the item work.
    //
    // Only ever touched when the field is really there. apply() runs a
    // SECOND time for the secret list, and that patch carries no weather;
    // without the has() check the second pass would read a missing field as
    // 0 and quietly dry out every level. Writing the same value twice would
    // be harmless in itself - this is idempotent - but the report would
    // then lie about what the level used to have.
    if (patch.has("weather"))
    {
        const int want = static_cast<int>(patch["weather"].asInt(0));
        if (want < 0 || want > 2)
        {
            error = "weather: " + std::to_string(want) +
                    " is not a weather type - the engine knows 0 (dry), 1 (rain) and 2 (snow).";
            return false;
        }
        report.weatherFrom = level.weatherType();
        if (!level.setWeatherType(want))
        {
            error = "weather: the level header could not be written.";
            return false;
        }
        report.weatherTo = want;
    }

    // --- Weapons everywhere -----------------------------------------------
    //
    // Copy list from the randomizer (fully resolved, rules there). Runs
    // AFTER the outfit: the outfit donor's holster slots 13-16 are supposed
    // to stay, the weapon entries for them are "only what is missing" and
    // are then skipped here.
    //
    // The error picture is deliberately split: a donor that cannot be
    // LOADED is only a warning, as with outfit and sounds (the level stays
    // playable, it just lacks weapons). A FAILED copy, however, aborts like
    // a half outfit - then it is unclear what state the appended blocks are
    // in, and nothing like that gets saved.
    // weaponAdds and moveableAdds are the same operation - both are
    // resolved copy lists {slot, fromLevel, replace} from the randomizer,
    // they merely count separately (weapons resp. cross enemies).
    // Fetches a guest from Tomb Raider 3 and installs it (#92).
    //
    // Everything happens here and now on the user's machine: extract,
    // convert, install. None of it lies around prepared, because prepared
    // packages would be third-party game data.
    auto buildTr3Guest = [&](Tr5Level& lvl, int slot, PatchReport& rep, std::string& err) -> bool
    {
        const Tr3Source::DinoSource* d = nullptr;
        static const std::vector<Tr3Source::DinoSource> all = Tr3Source::dinos();
        for (const Tr3Source::DinoSource& k : all) if (k.slot == slot) { d = &k; break; }
        if (!d) { err = "no guest known for slot " + std::to_string(slot); return false; }

        // Via the cache (#103): extract once instead of per level.
        std::vector<uint8_t> model, sounds;
        std::string f;
        if (!Tr3Source::fetchCached(*d, tr3Dir, tr3GoldDir, cacheDir, model, sounds, f))
        { err = f; return false; }

        // 1. Sounds
        std::vector<std::pair<int,int>> mapping;
        if (!lvl.addSoundsFromTr3(sounds, d->soundBase, mapping, f, &d->volumes))
        { err = std::string(d->name) + " (installing sounds): " + f; return false; }

        // 2. Rewrite the SFX numbers of the anim commands. If a number
        //    remains unresolved, ABORT - it would hit a foreign vanilla
        //    sound in the target (with the Compy, 243 turned into a hefty
        //    SFX_GENERIC_HEAVY_FEET, and finding that cost hours).
        std::vector<int> unresolved;
        if (!Tr3Source::remapSfx(model, mapping, d->muted, 411, unresolved, f))
        { err = std::string(d->name) + " (remapping SFX): " + f; return false; }
        if (!unresolved.empty())
        {
            err = std::string(d->name) + ": SFX number without a mapping (" +
                  std::to_string(unresolved[0]) + ") - it would hit a foreign sound";
            return false;
        }

        // 3. APPEND textures, never shift - otherwise the level collapses
        //    (user requirement, see the texture page rule).
        std::map<int,int> texMap;
        if (!lvl.addTexturesFromTr3(model, texMap, f))
        { err = std::string(d->name) + " (textures): " + f; return false; }

        // And point the faces at them. Without this step they keep the TR3
        // numbers and grab arbitrarily into the target's texinfos.
        if (Tr3Source::remapTextures(model, texMap) < 0)
        { err = std::string(d->name) + ": faces not readable"; return false; }

        // 4. Model. texBase -1, because the faces already point at the new
        //    texinfos.
        if (!lvl.addMoveableFromTr3(model, slot, -1, f))
        { err = std::string(d->name) + " (model): " + f; return false; }

        // #123 S7: the Remaster has no TR5 model for this slot at all, and
        // the engine is told where it really comes from - the guests have
        // their own HD models in the Tomb Raider I-III Remaster.
        lvl.hdMarkDonor(slot, slot, "TR3");

        rep.warnings.push_back(std::string("guest fetched from Tomb Raider 3: ") + d->name +
                               " on slot " + std::to_string(slot));
        return true;
    };

    auto copySlots = [&](const JsonValue& list, const char* what, int& counter) -> bool
    {
        if (list.size() > 0 && !levelSource)
        {
            report.warnings.push_back(std::to_string(list.size()) + " " + what +
                                      " entries skipped - no access to the source levels.");
            return true;
        }
        for (size_t i = 0; i < list.size(); ++i)
        {
            const JsonValue& w = list[i];
            const int slot = static_cast<int>(w["slot"].asInt(-1));

            // --- Guest from Tomb Raider 3 (#92) -----------------------------
            //
            // No donor level: everything is fetched NOW from the user's
            // installation. That is exactly what this path is for - we ship
            // no game data, so nothing may be prepared.
            //
            // The order is a rule, not taste (see the recipe):
            // SOUNDS -> TEXTURES -> MODEL. Only then is it fixed what the
            // anim commands and the faces must point at, and both sit in
            // the model package, which is installed last.
            if (w.has("fromTr3"))
            {
                if (!buildTr3Guest(level, slot, report, error)) return false;
                ++counter;
                continue;
            }

            const std::string from = w["fromLevel"].asString();
            const bool replace = w["replace"].asBool(false);
            // Diverging target slot (fallback slots of the cross enemy mix:
            // the donor's MESHSWAP1 405 lands in the ENEMY'S slot, since
            // #111 401-404 resp. 462-467), and whether only the meshes
            // travel (mesh stores without their own animations).
            const int to = static_cast<int>(w["toSlot"].asInt(-1));
            const int targetSlot = (to < 0) ? slot : to;
            const bool meshesOnly = w["onlyMeshes"].asBool(false);
            // Explicit anim slice length (Imp: the death anim lies beyond
            // the object boundary in the global table, see Tr5Level.h).
            const int animCount = static_cast<int>(w["animCount"].asInt(-1));

            Tr5Level::MoveableInfo mi;
            if (!replace && level.moveableInfo(targetSlot, mi)) continue;   // already has it

            Tr5Level* donor = source(from);
            if (!donor)
            {
                report.warnings.push_back(std::string(what) + ": donor level " + from +
                                          " not loadable - its slots are missing.");
                continue;
            }

            std::string wErr;
            if (!level.copyMoveableFrom(*donor, slot, wErr, to, meshesOnly, animCount))
            {
                error = std::string(what) + " slot " + std::to_string(slot) + " from " + from + ": " + wErr;
                return false;
            }
            // #123 S7: BOTH slot numbers. A mesh store travels into the
            // enemy's own slot (MESHSWAP1 405 -> 462..467), and only the
            // slot it came FROM says what the model actually is.
            level.hdMarkDonor(targetSlot, slot, from);
            ++counter;
        }
        return true;
    };
    if (!copySlots(patch["weaponAdds"], "weapon", report.weaponsCopied)) return false;
    if (!copySlots(patch["moveableAdds"], "cross-enemy", report.moveablesCopied)) return false;

    // As soon as SWAT (33) and a guard lie in the level together, the guard
    // runs on SWAT's anim set via the engine alias and fires there via
    // state 3 - whose anim (+2) is dead heritage in Core's rich3 data with
    // the wrong stride (reads the target anim's 34-word frames in 36s: the
    // model disappears from keyframe 1, the shadow smears, no shot sound).
    // Repair: +2 adopts frame data, rate and shot command of the healthy
    // guard fire anim (+2 of its own set) in the same level. Any guard set
    // works as the source: the classic one on 37 or the joby guard on 460
    // (BLUE_GUARD2, #105) - both fire via state 3, their sets carry the
    // anim healthy.
    {
        Tr5Level::MoveableInfo swat, guard;
        if (level.moveableInfo(33, swat) && swat.animIndex >= 0)
        {
            int guardSlot = -1;
            if (level.moveableInfo(37, guard) && guard.animIndex >= 0)
                guardSlot = 37;
            else if (level.moveableInfo(460, guard) && guard.animIndex >= 0)
                guardSlot = 460;

            if (guardSlot > 0)
            {
                std::string rErr;
                if (!level.adoptAnimData(33, 2, guardSlot, 2, rErr))
                    report.warnings.push_back("SWAT fire anim not repairable: " + rErr);
            }
        }
    }

    // --- Take over sound samples ------------------------------------------
    const JsonValue& sadds = patch["soundAdds"];
    if (sadds.size() > 0 && !levelSource)
    {
        report.warnings.push_back(msg("%d soundAdds skipped - no access to the source levels.",
                                      static_cast<int>(sadds.size())));
    }
    else
    {
        for (size_t i = 0; i < sadds.size(); ++i)
        {
            const JsonValue& s = sadds[i];
            const std::string from = s["fromLevel"].asString();
            const int map = static_cast<int>(s["map"].asInt(-1));
            const int fromMap = static_cast<int>(s["fromMap"].asInt(-1));

            if (level.soundInfoIndex(map) >= 0) continue;      // already present

            Tr5Level* q = source(from);
            if (!q)
            {
                report.warnings.push_back("soundAdds: source level " + from + " not loadable - sound missing.");
                continue;
            }

            std::string sErr;
            if (!level.copySoundFrom(*q, fromMap, map, sErr))
            {
                // No abort: a missing sound does not break the level,
                // unlike a missing trigger.
                report.warnings.push_back("soundAdds: map " + std::to_string(map) + " - " + sErr);
                continue;
            }
            ++report.soundsCopied;
        }
    }

    // --- New triggers ----------------------------------------------------
    //
    // After the moves but before the items: that way the whole floordata is
    // fixed before anything is written to the items. It is not mandatory -
    // appendChain adjusts the item offset itself - but it keeps the flow
    // unambiguous when reading back.
    // --- Lara anims from donor levels (#53) ------------------------------
    //
    // {"anim": 437, "fromLevel": "joby2"} implants a single Lara anim with
    // frames, commands and sounds. Needed for ANIM_KEYCARD: the slot exists
    // everywhere, only joby2 has real image data. A donor that cannot be
    // loaded is only a warning (the engine then falls back to the ordinary
    // key anim at Key Hole 8); a FAILED install aborts.
    const JsonValue& lanims = patch["laraAnimAdds"];
    if (lanims.size() > 0 && !levelSource)
        report.warnings.push_back(std::to_string(lanims.size()) +
                                  " Lara anim entries skipped - no access to the source levels.");
    else for (size_t i = 0; i < lanims.size(); ++i)
    {
        const JsonValue& la = lanims[i];
        const std::string from = la["fromLevel"].asString();
        Tr5Level* donor = source(from);
        if (!donor)
        {
            report.warnings.push_back("Lara anim: donor level " + from + " not loadable.");
            continue;
        }
        std::string aErr;
        if (!level.copyLaraAnimFrom(*donor, static_cast<int>(la["anim"].asInt(-1)), aErr))
        {
            error = "laraAnimAdds[" + std::to_string(i) + "]: " + aErr;
            return false;
        }
    }

    // --- Water rooms (#53) -----------------------------------------------
    //
    // {"room": N, "on": true|false} sets resp. clears ROOM_UNDERWATER. A
    // hard error like portal and climbing wall: a half-switched room would
    // be worse than none.
    const JsonValue& water = patch["roomWater"];
    for (size_t i = 0; i < water.size(); ++i)
    {
        const JsonValue& w = water[i];
        std::string wErr;
        if (!level.setRoomWater(static_cast<int>(w["room"].asInt(-1)), w["on"].asBool(true), wErr))
        {
            error = "roomWater[" + std::to_string(i) + "]: " + wErr;
            return false;
        }
    }

    // --- Climbing walls (#53) --------------------------------------------
    //
    // {"room","sectorX","sectorZ","dirBits"}: the sector becomes climbable.
    // dirBits per the engine convention (control.cpp:2189), computed by the
    // list reader from @Angle. Hard error like the portal - nobody would
    // ever find half a climbing wall.
    const JsonValue& climbs = patch["climbAdds"];
    for (size_t i = 0; i < climbs.size(); ++i)
    {
        const JsonValue& c = climbs[i];
        std::string cErr;
        if (!level.makeClimb(static_cast<int>(c["room"].asInt(-1)),
                             static_cast<int>(c["sectorX"].asInt(-1)),
                             static_cast<int>(c["sectorZ"].asInt(-1)),
                             static_cast<int>(c["dirBits"].asInt(0)), cErr))
        {
            error = "climbAdds[" + std::to_string(i) + "]: " + cErr;
            return false;
        }
    }

    // --- Room moves (#136) -----------------------------------------------
    //
    // BEFORE the portals, deliberately: a window takes its height from the
    // two rooms as they stand, so the room has to be where it belongs first.
    // Doing it the other way round would leave the window describing a hole
    // that has since moved.
    const JsonValue& roomMoves = patch["roomMoves"];
    for (size_t i = 0; i < roomMoves.size(); ++i)
    {
        const JsonValue& mv = roomMoves[i];
        std::string mErr;
        if (!level.moveRoom(static_cast<int>(mv["room"].asInt(-1)),
                            static_cast<long>(mv["dx"].asInt(0)),
                            static_cast<long>(mv["dy"].asInt(0)),
                            static_cast<long>(mv["dz"].asInt(0)), mErr))
        {
            error = "roomMoves[" + std::to_string(i) + "]: " + mErr;
            return false;
        }
    }

    // --- Portals (#53) ---------------------------------------------------
    //
    // One entry opens BOTH sides of a sealed wall: the named sector of
    // roomA becomes a portal to roomB and vice versa. The view windows (the
    // room list's 32-byte portals) must already exist - makePortal checks
    // that and otherwise aborts hard, because a walkable opening without a
    // window would be a black hole in the picture.
    const JsonValue& portals = patch["portalAdds"];
    for (size_t i = 0; i < portals.size(); ++i)
    {
        const JsonValue& p = portals[i];
        const int rA = static_cast<int>(p["roomA"].asInt(-1));
        const int rB = static_cast<int>(p["roomB"].asInt(-1));
        const int xA = static_cast<int>(p["sxA"].asInt(-1));
        const int zA = static_cast<int>(p["szA"].asInt(-1));
        const int xB = static_cast<int>(p["sxB"].asInt(-1));
        const int zB = static_cast<int>(p["szB"].asInt(-1));
        const int kind = static_cast<int>(p["kind"].asInt(0));
        std::string pErr;
        bool ok;
        if (kind == 0)
        {
            ok = level.makePortal(rA, xA, zA, rB, xB, zB, pErr) &&
                 level.makePortal(rB, xB, zB, rA, xA, zA, pErr);
        }
        else
        {
            // A opens at its ceiling (kind 1) or at its floor (kind 2); B is
            // the mirror image, so the flag flips for the second call.
            const bool aCeiling = (kind == 1);
            ok = level.makeVerticalPortal(rA, xA, zA, rB, aCeiling, pErr) &&
                 level.makeVerticalPortal(rB, xB, zB, rA, !aCeiling, pErr);
        }
        if (!ok)
        {
            error = "portalAdds[" + std::to_string(i) + "]: " + pErr;
            return false;
        }

        // #136: the passage is open, but the WALL is a drawn face of its own
        // and would still stand in the way. Both sides are treated - where a
        // sector has no wall, and that is what most portals look like,
        // nothing happens and nothing is reported.
        if (p["clear"].asInt(0))
        {
            int n = 0;
            if (!level.clearWallFaces(rA, xA, zA, n, pErr) ||
                !level.clearWallFaces(rB, xB, zB, n, pErr))
            { error = "portalAdds[" + std::to_string(i) + "] @Clear: " + pErr; return false; }
        }

        const int lwRoom = static_cast<int>(p["likeWallRoom"].asInt(-1));
        if (lwRoom >= 0)
        {
            const int lwx = static_cast<int>(p["likeWallSx"].asInt(-1));
            const int lwz = static_cast<int>(p["likeWallSz"].asInt(-1));
            int n = 0;
            if (!level.copyWallTexture(lwRoom, lwx, lwz, rA, xA, zA, n, pErr) ||
                !level.copyWallTexture(lwRoom, lwx, lwz, rB, xB, zB, n, pErr))
            { error = "portalAdds[" + std::to_string(i) + "] @LikeWall: " + pErr; return false; }
        }
    }

    // --- Room geometry (#137) --------------------------------------------
    //
    // Last of the room work, and that order is the whole point. It runs
    // AFTER roomMoves, so the sector bytes already carry the shift and the
    // floor and ceiling heights read out right. It runs AFTER portalAdds,
    // so the view windows exist and the walls are not built across them -
    // and so @Clear, which only ever meets a room that still has no faces,
    // finds nothing to take away, exactly as it does today.
    //
    // All the lines for one room are gathered into one call: the first that
    // names like= decides where the textures come from, and every line that
    // sets a texture becomes a rule, applied in the order written.
    const JsonValue& geoms = patch["roomGeometry"];
    if (geoms.size() > 0)
    {
        std::vector<int> order;
        std::map<int, int> likeOf;
        std::map<int, std::vector<Tr5Level::RoomTexRule>> rulesOf;

        for (size_t i = 0; i < geoms.size(); ++i)
        {
            const JsonValue& g = geoms[i];
            const int room = static_cast<int>(g["room"].asInt(-1));
            if (room < 0)
            { error = "roomGeometry[" + std::to_string(i) + "]: no room"; return false; }

            if (likeOf.find(room) == likeOf.end())
            { order.push_back(room); likeOf[room] = -1; }

            const int like = static_cast<int>(g["like"].asInt(-1));
            if (like >= 0) likeOf[room] = like;

            Tr5Level::RoomTexRule rule;
            rule.sx       = static_cast<int>(g["sx"].asInt(-1));
            rule.sz       = static_cast<int>(g["sz"].asInt(-1));
            rule.band     = static_cast<int>(g["band"].asInt(-1));
            rule.floorTex = static_cast<int>(g["floor"].asInt(-1));
            rule.ceilTex  = static_cast<int>(g["ceiling"].asInt(-1));
            rule.wallTex  = static_cast<int>(g["wall"].asInt(-1));
            rule.floorTri = static_cast<int>(g["floorTri"].asInt(-1));
            rule.ceilTri  = static_cast<int>(g["ceilingTri"].asInt(-1));
            rule.wallTri  = static_cast<int>(g["wallTri"].asInt(-1));
            // Six fields now: a texinfo carries the shape it was cut for, so
            // quads and triangles are named separately. Any one of them makes
            // the line a rule.
            if (rule.floorTex >= 0 || rule.ceilTex >= 0 || rule.wallTex >= 0 ||
                rule.floorTri >= 0 || rule.ceilTri >= 0 || rule.wallTri >= 0)
                rulesOf[room].push_back(rule);
        }

        for (int room : order)
        {
            std::string gErr;
            if (!level.buildRoomGeometry(room, likeOf[room], rulesOf[room], gErr))
            { error = "roomGeometry room " + std::to_string(room) + ": " + gErr; return false; }
        }
    }

    const JsonValue& tadds = patch["triggerAdds"];

    // Compensate the offset between planning and file (#107): the planning
    // numbers new items from the LEVEL JSON's item count (itemBase), but
    // the itemAdds append to the REAL file. If the source file carries more
    // items than the JSON (the weapon base was created after the JSON
    // export), every waker pointed at the item TWO BEFORE its enemy, and
    // the last enemies stood without a trigger, motionless forever
    // (Andrea1 seed 1: JSON 151, file 153 - seen in trview).
    // Only TO_OBJECT targets >= itemBase are shifted: everything below are
    // vanilla indices, and those do not shift, because the weapon base also
    // appends its extra items only at the end.
    const int itemBase = static_cast<int>(patch["itemBase"].asInt(-1));
    const int itemDelta = (itemBase >= 0) ? (level.itemCount() - itemBase) : 0;
    if (itemDelta != 0 && tadds.size() > 0)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "trigger targets shifted by %+d: the level JSON knows %d items, "
                      "the source file carries %d. JSON export and base level drift apart.",
                      itemDelta, itemBase, level.itemCount());
        report.warnings.push_back(buf);
    }

    for (size_t i = 0; i < tadds.size(); ++i)
    {
        const JsonValue& t = tadds[i];

        std::vector<Tr5TriggerCmd> cmds;
        const JsonValue& cl = t["commands"];
        for (size_t c = 0; c < cl.size(); ++c)
        {
            Tr5TriggerCmd cmd;
            cmd.type = static_cast<int>(cl[c]["type"].asInt(0));
            cmd.param = static_cast<int>(cl[c]["param"].asInt(0));
            if (cmd.type == 0 && itemBase >= 0 && cmd.param >= itemBase)
                cmd.param += itemDelta;                      // TO_OBJECT onto a NEW item
            cmds.push_back(cmd);
        }

        std::string addErr;
        // Defaults: kind 0 (TRIGGER), full mask, oneShot set. That is the
        // form an enemy trigger needs - whoever wants something else must
        // deviate explicitly.
        const int tRoom    = static_cast<int>(t["room"].asInt(-1));
        const int tSx      = static_cast<int>(t["sectorX"].asInt(-1));
        const int tSz      = static_cast<int>(t["sectorZ"].asInt(-1));
        const int tKind    = static_cast<int>(t["kind"].asInt(0));
        const int tTimer   = static_cast<int>(t["timer"].asInt(0));
        const bool tOne    = t["oneShot"].asInt(1) != 0;
        const int tMask    = static_cast<int>(t["mask"].asInt(31));
        const int tRefItem = static_cast<int>(t["refItem"].asInt(-1));

        if (!level.addTrigger(tRoom, tSx, tSz, tKind, tTimer, tOne, tMask,
                              cmds, addErr, tRefItem))
        {
            // Abort hard, as with the move: an enemy without its trigger
            // stands motionless in the level forever, and nobody later
            // recognizes that as an error - it just looks broken.
            error = "triggerAdds[" + std::to_string(i) + "]: " + addErr;
            return false;
        }
        ++report.triggersAdded;

        // --- The same trigger into the flipmap partner room (#147) --------
        //
        // Only for the entries that ask for it. An enemy waker may exist
        // twice without doing any harm: it wakes the same item, and the
        // item itself does not travel with the flip anyway (FlipMap hands
        // the base room its item list back). A curated trigger from a
        // secret list carries no mirrorFlip - it is bound to one object at
        // one place, and duplicating it is a decision nobody made.
        //
        // A failure here is deliberately NOT fatal: the base trigger is
        // already written, so the enemy works in the unflipped variant.
        // The skip goes into the notes instead.
        if (t["mirrorFlip"].asInt(0) != 0)
        {
            const int twin = flipPartner(level, tRoom);
            if (twin >= 0)
            {
                const char* why = flipTileTakesTrigger(level, tRoom, twin, tSx, tSz);

                std::string mirErr;
                if (!why && !level.addTrigger(twin, tSx, tSz, tKind, tTimer, tOne,
                                              tMask, cmds, mirErr, tRefItem))
                    why = mirErr.c_str();

                if (why)
                {
                    ++report.triggersMirrorSkipped;
                    char nb[192];
                    std::snprintf(nb, sizeof(nb),
                                  "flipmap: trigger %d,%d,%d NOT mirrored into room %d - %s.",
                                  tRoom, tSx, tSz, twin, why);
                    report.warnings.push_back(nb);
                }
                else
                    ++report.triggersMirrored;
            }
        }
    }

    if (report.triggersMirrored > 0 || report.triggersMirrorSkipped > 0)
    {
        char nb[160];
        std::snprintf(nb, sizeof(nb),
                      "flipmap: %d triggers mirrored into the partner room, %d skipped (wall/height).",
                      report.triggersMirrored, report.triggersMirrorSkipped);
        report.warnings.push_back(nb);
    }

    // --- Edits -----------------------------------------------------------
    const JsonValue& edits = patch["itemEdits"];
    for (size_t i = 0; i < edits.size(); ++i)
    {
        const JsonValue& e = edits[i];
        const int index = static_cast<int>(e["index"].asInt(-1));

        Tr5Item it{};
        if (index < 0 || !level.getItem(index, it))
        {
            report.warnings.push_back(msg("itemEdits[%d]: item %d does not exist in the level - skipped.",
                                          static_cast<int>(i), index));
            continue;
        }

        // Take over only the fields that are set.
        //
        // "typeId" swaps the enemy kind. The index stays what it is - which
        // is why all the triggers' "Object N" references stay valid and the
        // enemy still wakes up at the same spot. That is exactly why the
        // swap is harmless at all, while a removal never would be.
        if (e.has("typeId")) it.objectNumber = static_cast<int16_t>(e["typeId"].asInt());
        if (e.has("room"))   it.roomNumber = static_cast<int16_t>(e["room"].asInt());
        if (e.has("x"))      it.x = static_cast<int32_t>(e["x"].asInt());
        if (e.has("y"))      it.y = static_cast<int32_t>(e["y"].asInt());
        if (e.has("z"))      it.z = static_cast<int32_t>(e["z"].asInt());

        // "yAuto" also when moving (#53): the same computation as for the
        // additions further down. Needed by producers that only know room
        // and sector - the user's secret lists state exactly that.
        if (e["yAuto"].asBool(false) &&
            it.roomNumber >= 0 && static_cast<size_t>(it.roomNumber) < level.rooms().size())
        {
            const Tr5Room& rm = level.rooms()[static_cast<size_t>(it.roomNumber)];
            SlopeInfo si;
            int baseY = 0;
            const int kind = level.floorSlope(it.roomNumber,
                                               (it.x - rm.originX) / 1024,
                                               (it.z - rm.originZ) / 1024, si, baseY);
            if (kind == 0) it.y = static_cast<int32_t>(baseY);
            else if (kind > 0) it.y = static_cast<int32_t>(floorPosAt(si, baseY, it.x & 1023, it.z & 1023).y);
        }
        // Offset against the computed floor position. Needed for stacked
        // objects: a double door made of two door models, the upper one one
        // block above the lower. y grows DOWNWARDS in TR, so putting an
        // object higher means subtracting - the list therefore writes a
        // height above the floor, and the reader flips the sign.
        if (e.has("xOff")) it.x += static_cast<int32_t>(e["xOff"].asInt(0));
        if (e.has("zOff")) it.z += static_cast<int32_t>(e["zOff"].asInt(0));
        if (e.has("yOff")) it.y += static_cast<int32_t>(e["yOff"].asInt(0));
        if (e.has("angle"))  it.yRot = static_cast<int16_t>(e["angle"].asInt());
        if (e.has("ocb"))
        {
            // "ocb" sets the lower six bits - that is the value
            // PickUpCollision and InitialisePickup evaluate as the OCB.
            //
            // Bits 0x40 AND 0x80 are preserved. PickUpCollision evaluates
            // both together (game/pickup.cpp:989):
            //
            //     if (!(item->trigger_flags & 0xC0))
            //         KillItem(item_number);
            //     else { item->item_flags[3] = 1;
            //            item->flags |= IFL_TRIGGERED;
            //            item->status = ITEM_INVISIBLE; }
            //
            // Without one of these bits the picked-up item is simply
            // killed. With them it stays as ITEM_INVISIBLE and gets
            // item_flags[3] = 1 - and those are exactly the two conditions
            // PickupTrigger (pickup.cpp:158) requires for a pickup trigger
            // to fire at all.
            //
            // 0x80 additionally marks a regenerating pickup
            // (game/init.cpp:492). If we cleared it, several ammo piles
            // would quietly stop refilling - Andrea1 items 66 and 78,
            // Andrea2 item 69, joby2 item 8.
            //
            // 0x40 used to be described here as "a pure editor artifact, no
            // code path reads it" and was cleared. That was wrong and muted
            // the coupled trigger move of Andrea1 item 107: the item
            // arrived at its new spot without 0x40, was killed on pickup,
            // and the trigger that traveled along never found the condition
            // to wake dog and flame emitter. It was visible nowhere,
            // because every tool displays the OCB masked to 0x3F - item 107
            // shows as "OCB 0" everywhere, raw it is 0x0040.
            //
            // Cross-check: all seven items pinned via a pickup trigger
            // carry this bit (Andrea1 107 and 94, Andrea3 68, andy1 8,
            // Andy2 29, rich1 14, rich3 75).
            //
            // 0x100 (mesh invisible), on the other hand, is deliberately
            // cleared: a moved pickup should be visible at its new spot.
            const uint16_t keep = static_cast<uint16_t>(it.triggerFlags) & 0x00C0u;
            const uint16_t low = static_cast<uint16_t>(e["ocb"].asInt()) & 0x003Fu;
            it.triggerFlags = static_cast<int16_t>(keep | low);
        }
        if (e.has("flags"))  it.flags = static_cast<int16_t>(e["flags"].asInt());

        // "antitriggerImmune" sets IFL_ANTITRIGGER_ONESHOT (0x80) in the
        // flag word. An antitrigger pointing at an ACTIVE intelligent enemy
        // kills it without detour: hit_points = -16384, DisableBaddieAI,
        // KillItem (control.cpp:2422) - no dying, no fading, it is simply
        // gone. In vanilla that is intentional, because the enemy is
        // supposed to be long done at that point; after a type swap that
        // assumption no longer holds and the new enemy vanishes mid-fight
        // (seen in game: Andrea1 item 34).
        //
        // Core's own brake against that is this bit: if it is set,
        // TestTriggers leaves the item alone (control.cpp:2378) - meant as
        // "antitriggered once, never again". We set it from the start, then
        // the antitrigger bounces off and the enemy fights and dies
        // normally. This acts per ITEM, not per trigger: if several enemies
        // share one antitrigger (Andy2 items 150-154), the others stay
        // untouched. The bit is not set on a single one of the 2094 items
        // across all 15 vanilla levels (measured) and is not touched by
        // InitialiseItem, so it survives from the file.
        if (e["antitriggerImmune"].asBool(false))
            it.flags = static_cast<int16_t>(static_cast<uint16_t>(it.flags) | 0x0080u);

        if (!level.setItem(index, it))
        {
            error = msg("item %d could not be written.", index);
            return false;
        }
        ++report.edited;
    }

    // --- Additions --------------------------------------------------------
    const JsonValue& adds = patch["itemAdds"];
    for (size_t i = 0; i < adds.size(); ++i)
    {
        const JsonValue& a = adds[i];

        Tr5Item it{};
        it.objectNumber = static_cast<int16_t>(a["typeId"].asInt());
        it.roomNumber = static_cast<int16_t>(a["room"].asInt());
        it.x = static_cast<int32_t>(a["x"].asInt());
        it.y = static_cast<int32_t>(a["y"].asInt());
        it.z = static_cast<int32_t>(a["z"].asInt());

        // "yAuto": compute the true floor height at this spot instead of
        // taking it from the caller (#61). On a slope the sector base value
        // is the HIGHEST corner - an item in the middle of the sector would
        // hang up to half a sector in the air (#86). The same computation
        // the engine uses to find the floor: floorSlope + floorPosAt.
        //
        // Needed by producers that have no slope data (the wall test script
        // in PowerShell). Without the field everything stays as before -
        // the randomizer computes its y itself and does not set it.
        if (a["yAuto"].asBool(false) &&
            it.roomNumber >= 0 && static_cast<size_t>(it.roomNumber) < level.rooms().size())
        {
            const Tr5Room& rm = level.rooms()[static_cast<size_t>(it.roomNumber)];
            const int sx = (it.x - rm.originX) / 1024;
            const int sz = (it.z - rm.originZ) / 1024;
            SlopeInfo si;
            int baseY = 0;
            const int kind = level.floorSlope(it.roomNumber, sx, sz, si, baseY);
            if (kind == 0)
                it.y = static_cast<int32_t>(baseY);          // flat
            else if (kind > 0)
            {
                const FloorPos pos = floorPosAt(si, baseY, it.x & 1023, it.z & 1023);
                it.y = static_cast<int32_t>(pos.y);
            }
            // kind < 0 (chain unreadable): the provided y stands.
        }


        // As with the edits: offset against the computed floor position, so
        // objects can be stacked (double door).
        if (a.has("xOff")) it.x += static_cast<int32_t>(a["xOff"].asInt(0));
        if (a.has("zOff")) it.z += static_cast<int32_t>(a["zOff"].asInt(0));
        if (a.has("yOff")) it.y += static_cast<int32_t>(a["yOff"].asInt(0));
        it.yRot = static_cast<int16_t>(a["angle"].asInt(0));
        it.triggerFlags = static_cast<int16_t>(a["ocb"].asInt(0));
        it.flags = static_cast<int16_t>(a["flags"].asInt(0));
        // shade: -1 means dynamic lighting. 0 would be pitch black, and
        // nobody ever finds a pitch-black pickup.
        it.shade = static_cast<int16_t>(a["shade"].asInt(-1));

        std::string addErr;
        if (!level.addItem(it, addErr))
        {
            error = "itemAdds: " + addErr;
            return false;
        }
        ++report.added;
    }

    // --- AI objects (flee markers) ----------------------------------------
    //
    // AFTER the itemAdds: addAiObject computes the AI block's position from
    // the current item count. Larson/Pierre thus get an AI_AMBUSH on the
    // spawn sector for every instance - the 40-HP flight finds its target
    // and despawns there instead of endlessly running after Lara.
    const JsonValue& aiAdds = patch["aiAdds"];
    for (size_t i = 0; i < aiAdds.size(); ++i)
    {
        const JsonValue& a = aiAdds[i];

        Tr5AiObject ai{};
        ai.objectNumber = static_cast<int16_t>(a["slot"].asInt());
        ai.roomNumber = static_cast<int16_t>(a["room"].asInt());
        ai.x = static_cast<int32_t>(a["x"].asInt());
        ai.y = static_cast<int32_t>(a["y"].asInt());
        ai.z = static_cast<int32_t>(a["z"].asInt());
        ai.ocb = static_cast<int16_t>(a["ocb"].asInt(0));

        std::string aiErr;
        if (!level.addAiObject(ai, aiErr))
        {
            error = "aiAdds: " + aiErr;
            return false;
        }
    }

    // --- Backfill Lara's interaction anims (#53) --------------------------
    //
    // Lara's anim table is the same length in every level, but the IMAGE
    // DATA is level-specific. Where a level never needs a movement, Core
    // leaves the entry as a stub (FrameSize 0) - what plays is a frozen
    // pose or a far too short movement. It hit the user twice: the keycard
    // in joby3 (22 seconds frozen) and a switch in andy3 ("does a switch
    // animation, just not the right one - far too fast"). The second is
    // worse than ugly: Lara finishes before the OBJECT animation ends,
    // leaves the sector, and the trigger never engages.
    //
    // Hence here, AFTER all object changes: look up which interactions even
    // occur in the finished level, and fetch the corresponding anim from a
    // donor level if it is missing.
    //
    // The donor order comes from measuring all 13 levels with LaraAnim:
    // joby4 carries almost everything, andy1 almost the rest. The first
    // donor that REALLY carries the anim is taken - verified on the loaded
    // donor, not believed.
    {
        struct Need { int typeFrom, typeTo; int ocbFrom, ocbTo; int anim; const char* what; };
        static const Need kNeeds[] = {
            // Switches 266..271: the OCB picks Lara's movement (switch.cpp:246ff)
            { 266, 271,  0,  0,  64, "switch" },
            { 266, 271,  1,  2, 325, "switch (hidden)" },
            { 266, 271,  3,  3, 413, "switch (small)" },
            { 266, 271,  4,  4, 196, "switch (generator)" },
            { 266, 271,  5,  7, 197, "switch (one-handed)" },
            { 266, 271,  8, 63, 470, "switch (airlock)" },
            { 274, 274,  8, 63, 470, "airlock switch" },
            // Keyholes and puzzle holes
            { 258, 264,  0, 63, 131, "keyhole" },
            // Key Hole 8 takes the card-swipe gesture IF the level has its
            // data (pickup.cpp queries the FrameSize). Hence request it
            // here - then it has it, and the gesture matches the object.
            // ANIM_USEKEY stays necessary regardless: it is the fallback.
            { 265, 265,  0, 63, 437, "keyhole 8 (card)" },
            { 265, 265,  0, 63, 131, "keyhole 8 (fallback)" },
            { 236, 243,  0, 63, 134, "puzzle hole" },
            // Trapdoors and push block
            { 109, 110,  0, 63, 317, "floor trapdoor" },
            { 111, 112,  0, 63, 318, "ceiling trapdoor" },
            { 137, 141,  0, 63, 316, "push block" },
        };

        // Donor order, measured with LaraAnim across all 13 levels.
        static const char* kDonors[] = { "joby4", "andy1", "Andy3", "joby3", "Andrea3", "joby5", "Andy2", "joby2", "rich2" };

        // These movements hang off no object but off Lara's controls - they
        // must exist in EVERY level. Found by the user: the crawl roll
        // (duck + sprint, lara.cpp:3288 sets anim 218) is a stub in rich1,
        // and already in vanilla - Lara is pushed forward without anything
        // moving. Measured with LaraAnim: rich1 is the ONLY level with
        // FrameSize 0 there.
        static const int kAlways[] = {
            218,   // AS_DUCKROLL - crawl roll
            421,   // ANIM_CRAWLJUMP - crawl jump
            // Handling a flare. Found by the user once the rich levels had
            // flares at all, and measured with LaraAnim: all four are stubs
            // (size 0) in rich1, rich2 and rich3, and sound in Andrea1 and
            // joby4. Core left them out because those levels carry no flare
            // - which is exactly what we changed.
            189,   // ANIM_THROWFLARE  - throwing one away
            204,   // ANIM_PICKUPF     - picking one off the floor
            206,   // ANIM_PICKUPF_UW  - the same under water
            312,   // ANIM_DUCKPICKUPF - the same while crouched
        };
        std::set<int> needed(std::begin(kAlways), std::end(kAlways));
        for (int i = 0; i < level.itemCount(); ++i)
        {
            Tr5Item it;
            if (!level.getItem(i, it)) continue;
            const int type = it.objectNumber;
            const int ocb = it.triggerFlags & 0x3F;
            for (const Need& b : kNeeds)
                if (type >= b.typeFrom && type <= b.typeTo && ocb >= b.ocbFrom && ocb <= b.ocbTo)
                    needed.insert(b.anim);
        }

        // Animations are CHAINS. An entry points at its successor via
        // next_animation, and a movement usually consists of several links:
        // the crawl roll in rich1 runs 218 -> 219 -> 220 -> 222. In such a
        // case Core stubs the WHOLE chain (in rich1, 218 through 221 point
        // at the same frame pointer). Repairing only the first link is
        // therefore not enough: Lara starts the roll and falls back into
        // the crawl pose after six frames - exactly what the user saw. The
        // need is therefore expanded along next_animation before anything
        // is fetched.
        {
            std::set<int> open = needed;
            while (!open.empty())
            {
                const int a = *open.begin();
                open.erase(open.begin());
                const int next = level.laraAnimNext(a);
                // Self-reference ends the chain (222 -> 222 is the usual
                // hold at the end), as does an unknown value.
                if (next < 0 || next == a) continue;
                if (needed.insert(next).second) open.insert(next);
            }
        }

        for (int animNr : needed)
        {
            if (level.laraAnimUsable(animNr)) continue;
            if (!levelSource)
            {
                report.warnings.push_back("Lara anim " + std::to_string(animNr) +
                                          " is missing and no access to the donor levels.");
                continue;
            }
            bool fetched = false;
            for (const char* sp : kDonors)
            {
                Tr5Level* q = source(sp);
                if (!q || !q->laraAnimUsable(animNr)) continue;
                std::string aErr;
                if (!level.copyLaraAnimFrom(*q, animNr, aErr)) continue;
                report.warnings.push_back("Lara anim " + std::to_string(animNr) +
                                          " backfilled from " + sp + ".");
                fetched = true;
                break;
            }
            if (!fetched)
                report.warnings.push_back("Lara anim " + std::to_string(animNr) +
                                          " is missing and no donor has it - the interaction stays broken.");
        }
    }

    // Close the texture workshop (#104): all page imports of this run are
    // now packed ONCE and put into the file. save() refuses to write a
    // dirty open workshop - forgetting therefore shows up loudly, not as a
    // broken level.
    if (!level.texWorkshopClose(error))
    {
        error = "texture workshop: " + error;
        return false;
    }

    return true;
}

//----------------------------------------------------------------------------
// HD markers (#123, stage S7). What they are and why, see ApplyPatch.h.
//----------------------------------------------------------------------------
int Patch::hdOutfitId(const std::string& wardrobeId)
{
    // The engine's HD_LARA_OUTFIT_* numbers, written out rather than
    // included: this file builds without the engine (the diagnostic tools
    // use it), exactly as hd_lara.cpp writes out the LARA_* numbers for the
    // same reason. The enum they belong to is
    //   0 NONE, 1 CLASSIC, 2 RUSSIA, 3 SUBSUIT, 4 VCI,
    //   5 TR3_ANTARC, 6 TR3_NEVADA, 7 TR3_COAST, 8 TR3_CATSUIT.
    struct Row { const char* id; int hd; };
    static const Row kTable[] =
    {
        { "tr3_london",     8 },   // TR3_CATSUIT
        { "tr3_jungle",     7 },   // TR3_COAST
        { "tr3_antarctica", 5 },   // TR3_ANTARC
        { "tr3_nevada",     6 }    // TR3_NEVADA
    };

    for (const Row& r : kTable)
        if (wardrobeId == r.id) return r.hd;

    // An outfit the engine has no HD twin for. 0 means "no marker", and the
    // engine then dresses her the way it would have without stage S7 - the
    // level's own classic body. A right classic body beats a wrong HD one.
    return 0;
}

bool Patch::writeHdMarkers(const Tr5Level& level, const std::wstring& levelPath,
                           std::string& error, int* roomsOut, int* donorsOut,
                           int* cutsOut)
{
    if (roomsOut) *roomsOut = 0;
    if (donorsOut) *donorsOut = 0;
    if (cutsOut) *cutsOut = 0;

    // Lara's own block and the slots this project added past vanilla. Both
    // numbers are the engine's (hd_bind.cpp: lara_block_last = 30, and the
    // hand table covers BLUE_GUARD2 460 .. MESHSWAP_CROSS_CRANEGUY 470).
    const int kLaraBlockLast = 30;
    const int kHandTableFirst = 460, kHandTableLast = 470;

    // <path>.trc -> <path>.hdm. The extension is cut only when the dot
    // belongs to the file name and not to a folder above it.
    std::wstring out = levelPath;
    const size_t slash = out.find_last_of(L"/\\");
    const size_t dot = out.find_last_of(L'.');
    if (dot != std::wstring::npos && (slash == std::wstring::npos || dot > slash))
        out.erase(dot);
    out += L".hdm";

    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"wb") != 0 || !f)
    {
        error = "HD markers: cannot write the sidecar file";
        return false;
    }

    // THE VERSION IS THE LOWEST ONE THAT CAN SAY WHAT IS IN THE FILE
    // (#123, fix round Z). `cut` lines were added in version 2, so a file
    // that carries one declares 2 and an engine older than fix round Z
    // refuses the whole of it - which is the right answer, because that
    // engine would draw a closed HD wall across an opening it cannot cut.
    // A file with no cut line is a valid version 1 file to the last byte,
    // so it says 1 and an older engine goes on reading it. The user runs
    // intermediate builds out of release\ beside finished levels; a format
    // number is no reason to break the ones that still work.
    //
    // Deterministic all the same: the same seed writes the same lines, so
    // it writes the same version.
    const std::set<Tr5Level::HdCut>& cuts = level.hdCuts();

    // "wb", and every line ended with a single \n: the file has to come out
    // byte for byte the same for the same seed, and text mode would make
    // that depend on the platform's line ending. A reader has to tolerate a
    // stray \r anyway, since a user may have opened it in an editor.
    std::fprintf(f, "hdm %d\n", cuts.empty() ? 1 : 2);

    // Straight out of the level header, not out of a report - so the byte
    // the engine reads and the line a person reads cannot say two different
    // things. No line at all means 0, "no Tomb Raider III outfit".
    const int outfit = level.hdOutfit();
    if (outfit > 0) std::fprintf(f, "outfit %d\n", outfit);

    /* THE WARDROBE KEY (#153, round M2), the rest of the line and no quoting:
       a mod's key holds its folder name and may carry spaces. Written after
       the number so that the two facts about the same roll stand together,
       and only when the run rolled an outfit at all - a file with no `wear`
       line means "the level's own facts decide", which is what every file
       written before this round means and is read as.

       The version stays 2. An engine older than this round does not know the
       word, ignores the line and draws what it drew before, which is the
       whole reason the key went into a line and not into a new version. */
    if (!level.hdWear().empty())
        std::fprintf(f, "wear %s\n", level.hdWear().c_str());

    // Ascending, because a std::set is. The same seed therefore gives the
    // same file, which is part of the proof that a run is reproducible.
    for (int room : level.hdTouchedRooms())
    {
        std::fprintf(f, "room %d\n", room);
        if (roomsOut) ++*roomsOut;
    }

    // The cut boxes, ascending by their six numbers - a std::set again, and
    // for the same reason. Written after the `room` lines so that a person
    // reading the file meets the whole-room facts first; the engine's reader
    // does not care about the order of any of it.
    for (const Tr5Level::HdCut& c : cuts)
    {
        std::fprintf(f, "cut %d %d %d %d %d %d\n",
                     c.lo[0], c.lo[1], c.lo[2], c.hi[0], c.hi[1], c.hi[2]);
        if (cutsOut) ++*cutsOut;
    }

    std::vector<Tr5Level::HdDonor> donors = level.hdDonors();
    std::sort(donors.begin(), donors.end(),
              [](const Tr5Level::HdDonor& a, const Tr5Level::HdDonor& b)
              { return a.slot < b.slot; });

    for (const Tr5Level::HdDonor& d : donors)
    {
        if (d.slot <= kLaraBlockLast) continue;
        if (d.slot >= kHandTableFirst && d.slot <= kHandTableLast) continue;
        std::fprintf(f, "donor %d %s %d\n", d.slot, d.level.c_str(), d.fromSlot);
        if (donorsOut) ++*donorsOut;
    }

    const bool ok = (std::fclose(f) == 0);
    if (!ok) error = "HD markers: the sidecar file could not be closed";
    return ok;
}
