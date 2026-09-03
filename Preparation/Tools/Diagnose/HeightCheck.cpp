// HeightCheck - does every enemy in a level have headroom for its model? (#141)
//
// The randomizer used to put enemies into rooms lower than the model, and the
// head then stuck through the ceiling (user find in game: rich1 item 259, a
// soldier statue of 1546 units under a ceiling 1024 above the floor). This
// tool is the independent witness for the rule that fixes it - it reads the
// FINISHED level file and knows nothing about the placement code:
//
//   rest-pose height = maxY - minY of the model's anim 0 / frame 0
//                      (Tr5Level::frameBounds)
//   clearance        = the item's own y minus the LOWEST ceiling of its
//                      sector (Tr5Level::sectorCeilingLow - the four corners
//                      out of the floordata, after the ceiling portals have
//                      been followed upward the way GetCeiling does)
//
// A violation is clearance < height. No safety margin: a 1024 room has to
// stay open to an 809 unit human, that is the user's decision.
//
// Both numbers come out of the level under test, so an imported foreign
// model is measured as it really arrived, not as some table claims.
//
// CALIBRATION: on the 15 VANILLA levels the tool must report 0 violations.
// Core's own placements are the yardstick - if the measure calls them
// wrong, the measure is wrong. It did, once: without the sky_room walk, ten
// vanilla enemies came out too tall for their spot.
//
// Usage:
//   HeightCheck <level.trc> [<level.trc> ...]   - enemy items, violations, totals
//   HeightCheck -v <level.trc> ...              - every enemy item, not only the violations
//   HeightCheck heights <level.trc> ...         - the rest-pose height of every enemy model present
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/Randomizer.h"
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

static std::string baseName(const char* path)
{
    std::string s(path);
    const size_t cut = s.find_last_of("/\\");
    if (cut != std::string::npos) s = s.substr(cut + 1);
    return s;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: HeightCheck <level.trc> [more levels...]\n");
        std::printf("       HeightCheck -v <level.trc> ...     every enemy item, not only violations\n");
        std::printf("       HeightCheck heights <level.trc> ... rest-pose height of every enemy model\n");
        return 2;
    }

    bool verbose = false, heightsOnly = false;
    int first = 1;
    if (std::strcmp(argv[1], "-v") == 0)          { verbose = true;     first = 2; }
    else if (std::strcmp(argv[1], "heights") == 0){ heightsOnly = true; first = 2; }

    // The enemy kinds come from the randomizer itself, so the tool cannot
    // drift away from the pool it is meant to check.
    std::map<int, std::string> enemyName;
    for (const EnemyKind& k : Rando::enemyKinds()) enemyName[k.typeId] = k.name;

    int grandViolations = 0, grandEnemies = 0, grandNoMeasure = 0, grandNoSector = 0;

    for (int a = first; a < argc; ++a)
    {
        Tr5Level lvl;
        std::string err;
        if (!lvl.load(toW(argv[a]), err))
        { std::printf("%-16s NOT LOADABLE: %s\n", baseName(argv[a]).c_str(), err.c_str()); return 1; }

        const std::string name = baseName(argv[a]);

        // Rest-pose height per model slot, straight out of this file.
        std::map<int, int> height;
        for (int slot : lvl.moveableSlots())
        {
            if (!enemyName.count(slot)) continue;
            int16_t b[6] = {};
            if (!lvl.frameBounds(slot, b)) continue;
            height[slot] = b[3] - b[2];             // maxY - minY
        }

        if (heightsOnly)
        {
            std::printf("%s\n", name.c_str());
            for (std::map<int, int>::const_iterator h = height.begin(); h != height.end(); ++h)
                std::printf("  slot %3d  %-24s height %5d\n",
                            h->first, enemyName[h->first].c_str(), h->second);
            continue;
        }

        const std::vector<Tr5Room>& rooms = lvl.rooms();

        int enemies = 0, violations = 0, noMeasure = 0, noSector = 0;
        std::map<int, int> perType;                 // violations per type

        for (int i = 0; i < lvl.itemCount(); ++i)
        {
            Tr5Item it;
            if (!lvl.getItem(i, it)) continue;
            const int t = it.objectNumber;
            if (!enemyName.count(t)) continue;
            ++enemies;

            std::map<int, int>::const_iterator h = height.find(t);
            if (h == height.end() || h->second <= 0)
            {
                ++noMeasure;
                std::printf("  %-14s item %3d  type %3d %-22s NO MEASURE (model missing or one-frame stub)\n",
                            name.c_str(), i, t, enemyName[t].c_str());
                continue;
            }

            const int room = it.roomNumber;
            if (room < 0 || room >= (int)rooms.size() || rooms[room].sectorsOffset == 0)
            { ++noSector; continue; }

            const Tr5Room& r = rooms[room];
            const int sx = (it.x - r.originX) / 1024;
            const int sz = (it.z - r.originZ) / 1024;
            if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
            { ++noSector; continue; }

            long ceil = 0;
            if (!lvl.sectorCeilingLow(room, sx, sz, ceil)) { ++noSector; continue; }

            const long clearance = (long)it.y - ceil;
            const bool bad = clearance < h->second;
            if (bad) { ++violations; ++perType[t]; }

            if (bad || verbose)
                std::printf("  %-14s item %3d  type %3d %-22s room %3d (%2d,%2d)  y %6d  ceil %6ld  clearance %5ld  needs %5d  %s\n",
                            name.c_str(), i, t, enemyName[t].c_str(), room, sx, sz,
                            it.y, ceil, clearance, h->second, bad ? "VIOLATION" : "ok");
        }

        std::printf("%-16s %4d enemies, %3d violations", name.c_str(), enemies, violations);
        if (noMeasure) std::printf(", %d without a measure", noMeasure);
        if (noSector)  std::printf(", %d without a sector", noSector);
        if (!perType.empty())
        {
            std::printf("  [");
            for (std::map<int, int>::const_iterator p = perType.begin(); p != perType.end(); ++p)
                std::printf("%s%d x type %d", p == perType.begin() ? "" : ", ", p->second, p->first);
            std::printf("]");
        }
        std::printf("\n");

        grandEnemies += enemies;
        grandViolations += violations;
        grandNoMeasure += noMeasure;
        grandNoSector += noSector;
    }

    if (!heightsOnly)
        std::printf("TOTAL            %4d enemies, %3d violations, %d without a measure, %d without a sector\n",
                    grandEnemies, grandViolations, grandNoMeasure, grandNoSector);
    return 0;
}
