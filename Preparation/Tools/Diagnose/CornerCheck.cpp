/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */

// CornerCheck - proof for Tr5Level::cornerFloor / cornerCeiling.
//
// The two functions compute a sector's corner height from the floordata, as a
// port of the engine's GetHeight / GetCeiling with exactly one substitution
// (see the comment above cornerFloor in Tr5Level.cpp). This tool measures them
// against the only independent witness there is: the faces Core DREW into the
// vanilla levels. Every horizontal face that covers exactly one sector square
// must sit, corner for corner, on the computed floor or ceiling.
//
// Usage:
//   CornerCheck <level.trc> <room> [sx]   - one room (or one column), verbose
//   CornerCheck <level.trc> all           - every room of the level
//   CornerCheck sweep <level.trc> ...     - several levels, totals only
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

struct Stats
{
    long faces = 0;          // horizontal faces on exactly one sector
    long floorFaces = 0, floorOk = 0;
    long ceilFaces = 0, ceilOk = 0;
    long degenFaces = 0, degenOk = 0;   // sectors without headroom (floor <= ceiling)
    long cornerChecked = 0, cornerOk = 0;   // single corner values, headroom sectors only
    // Which branch of the formula a face actually exercised (headroom only):
    // flat, TILT, floor split, ROOF, ceiling split. hit[] counts the exact ones.
    long branch[5] = {0,0,0,0,0}, branchOk[5] = {0,0,0,0,0};
    long skippedVertical = 0, skippedOdd = 0, noHeight = 0;
    long corners = 0, notMul256 = 0;   // substitution check over ALL sectors
    long mismatches = 0;
};

// One face, judged. Returns nothing; fills st and prints mismatches.
static void checkFace(const Tr5Level& lvl, const Tr5Room& rm, int room,
                      const Tr5Level::FaceInfo& f, Stats& st,
                      const char* levelName, bool verbose)
{
    const int n = f.quad ? 4 : 3;

    // Sector-corner aligned?
    for (int k = 0; k < n; ++k)
        if (((f.v[k][0] - rm.originX) % 1024) != 0 || ((f.v[k][2] - rm.originZ) % 1024) != 0)
        { ++st.skippedOdd; return; }

    // Distinct xz positions: a wall face has fewer than n of them.
    int distinct = 0;
    for (int k = 0; k < n; ++k)
    {
        bool seen = false;
        for (int j = 0; j < k; ++j)
            if (f.v[j][0] == f.v[k][0] && f.v[j][2] == f.v[k][2]) seen = true;
        if (!seen) ++distinct;
    }
    if (distinct != n) { ++st.skippedVertical; return; }

    long minx = f.v[0][0], maxx = minx, minz = f.v[0][2], maxz = minz;
    for (int k = 1; k < n; ++k)
    {
        if (f.v[k][0] < minx) minx = f.v[k][0];
        if (f.v[k][0] > maxx) maxx = f.v[k][0];
        if (f.v[k][2] < minz) minz = f.v[k][2];
        if (f.v[k][2] > maxz) maxz = f.v[k][2];
    }
    if (maxx - minx != 1024 || maxz - minz != 1024) { ++st.skippedOdd; return; }

    const int sx = (int)((minx - rm.originX) / 1024);
    const int sz = (int)((minz - rm.originZ) / 1024);
    if (sx < 0 || sx >= rm.numXSectors || sz < 0 || sz >= rm.numZSectors)
    { ++st.skippedOdd; return; }

    ++st.faces;

    long fy[4], cy[4];
    bool haveF = true, haveC = true;
    for (int k = 0; k < n; ++k)
    {
        const int cx = (int)(f.v[k][0] - minx);
        const int cz = (int)(f.v[k][2] - minz);
        if (!lvl.cornerFloor(room, sx, sz, cx, cz, fy[k]))   haveF = false;
        if (!lvl.cornerCeiling(room, sx, sz, cx, cz, cy[k])) haveC = false;
    }
    if (!haveF && !haveC)
    {
        ++st.noHeight;
        std::printf("  NO HEIGHT %s room %d sector (%d,%d): a face on a wall sector\n",
                    levelName, room, sx, sz);
        return;
    }

    // Floor or ceiling? Decided by distance, not by matching - otherwise a
    // wrong floor value that happens to equal the ceiling would count as a
    // hit. y grows downward, the floor is the LARGER value.
    double avgY = 0, avgF = 0, avgC = 0;
    for (int k = 0; k < n; ++k) { avgY += (double)f.v[k][1]; avgF += (double)fy[k]; avgC += (double)cy[k]; }
    avgY /= n; avgF /= n; avgC /= n;

    bool isFloor;
    if (!haveC) isFloor = true;
    else if (!haveF) isFloor = false;
    else isFloor = std::fabs(avgY - avgF) <= std::fabs(avgY - avgC);

    const long* want = isFloor ? fy : cy;
    bool ok = true;
    for (int k = 0; k < n; ++k) if (f.v[k][1] != want[k]) ok = false;

    // A sector whose floor is not BELOW its ceiling (y grows downward) has no
    // headroom at all - Lara can never be in it. Core drew faces into some of
    // those anyway, and they follow no floordata; counted apart.
    bool degenerate = false;
    if (haveF && haveC)
        for (int k = 0; k < n; ++k) if (fy[k] <= cy[k]) degenerate = true;

    if (degenerate) { ++st.degenFaces; if (ok) ++st.degenOk; }
    else
    {
        if (isFloor) { ++st.floorFaces; if (ok) ++st.floorOk; }
        else         { ++st.ceilFaces;  if (ok) ++st.ceilOk;  }
        for (int k = 0; k < n; ++k)
        { ++st.cornerChecked; if (f.v[k][1] == want[k]) ++st.cornerOk; }

        // Which branch did this face exercise? Taken from the chain, so a
        // 100% hit rate cannot come from nothing but flat sectors.
        int br = 0;
        std::vector<Tr5FloorEntry> chain;
        const int cidx = lvl.sectorIndex(room, sx, sz);
        if (cidx > 0 && lvl.readChain(cidx, chain))
            for (size_t c = 0; c < chain.size(); ++c)
            {
                const int fn = chain[c].function;
                if (isFloor && fn == 2) br = 1;
                else if (isFloor && (fn == 7 || fn == 8 || (fn >= 11 && fn <= 14))) br = 2;
                else if (!isFloor && fn == 3) br = 3;
                else if (!isFloor && (fn == 9 || fn == 10 || (fn >= 15 && fn <= 18))) br = 4;
            }
        ++st.branch[br]; if (ok) ++st.branchOk[br];
    }

    if (!ok)
    {
        ++st.mismatches;
        std::printf("  MISMATCH %s room %d sector (%d,%d) %s %s tex %d  noheadroom %d  chain",
                    levelName, room, sx, sz, f.quad ? "quad" : "tri ",
                    isFloor ? "floor" : "ceiling", f.texInfo, degenerate ? 1 : 0);
        {
            std::vector<Tr5FloorEntry> chain;
            const int idx = lvl.sectorIndex(room, sx, sz);
            if (idx > 0 && lvl.readChain(idx, chain))
                for (size_t c = 0; c < chain.size(); ++c)
                {
                    std::printf(" [fn%d", chain[c].function);
                    for (size_t k = 0; k < chain[c].words.size(); ++k)
                        std::printf(" %04X", chain[c].words[k]);
                    std::printf("]");
                }
            else if (idx == 0) std::printf(" (none)");
            std::printf("\n");
        }
        for (int k = 0; k < n; ++k)
            std::printf("      corner (%ld,%ld) drawn y %ld   computed %ld%s\n",
                        f.v[k][0] - minx, f.v[k][2] - minz, f.v[k][1], want[k],
                        f.v[k][1] == want[k] ? "" : "   <-- differs");
    }
    else if (verbose)
    {
        std::printf("  ok  room %d sector (%d,%d) %s %-7s :", room, sx, sz,
                    f.quad ? "quad" : "tri ", isFloor ? "floor" : "ceiling");
        for (int k = 0; k < n; ++k)
            std::printf("  (%ld,%ld)=%ld", f.v[k][0] - minx, f.v[k][2] - minz, f.v[k][1]);
        std::printf("\n");
    }
}

// The substitution check: every corner height must be an exact multiple of
// 256, over every sector of the room - faces or not.
static void checkMul256(const Tr5Level& lvl, const Tr5Room& rm, int room, Stats& st)
{
    static const int C[4][2] = { {0,0},{1024,0},{0,1024},{1024,1024} };
    for (int sx = 0; sx < rm.numXSectors; ++sx)
        for (int sz = 0; sz < rm.numZSectors; ++sz)
            for (int c = 0; c < 4; ++c)
            {
                long y;
                if (lvl.cornerFloor(room, sx, sz, C[c][0], C[c][1], y))
                { ++st.corners; if (y % 256) ++st.notMul256; }
                if (lvl.cornerCeiling(room, sx, sz, C[c][0], C[c][1], y))
                { ++st.corners; if (y % 256) ++st.notMul256; }
            }
}

static void doRoom(const Tr5Level& lvl, int room, int onlySx, Stats& st,
                   const char* levelName, bool verbose)
{
    const std::vector<Tr5Room>& rooms = lvl.rooms();
    const Tr5Room& rm = rooms[(size_t)room];
    std::vector<Tr5Level::FaceInfo> faces;
    std::string error;
    if (!lvl.roomFaces(room, faces, error)) return;

    checkMul256(lvl, rm, room, st);

    for (size_t i = 0; i < faces.size(); ++i)
    {
        if (onlySx >= 0)
        {
            const long x0 = rm.originX + (long)onlySx * 1024, x1 = x0 + 1024;
            bool inside = true;
            const int n = faces[i].quad ? 4 : 3;
            for (int k = 0; k < n; ++k)
                if (faces[i].v[k][0] < x0 || faces[i].v[k][0] > x1) inside = false;
            if (!inside) continue;
        }
        checkFace(lvl, rm, room, faces[i], st, levelName, verbose);
    }
}

static void report(const char* what, const Stats& st)
{
    const long hf = st.floorOk, hc = st.ceilOk;
    std::printf("%-28s floor %ld/%ld  ceiling %ld/%ld  (%.4f%%)  corner values %ld/%ld   no headroom %ld/%ld   "
                "corners %ld, not*256 %ld   skipped: vertical %ld, odd %ld, wall %ld\n",
                what, hf, st.floorFaces, hc, st.ceilFaces,
                (st.floorFaces + st.ceilFaces) ? 100.0 * (hf + hc) / (st.floorFaces + st.ceilFaces) : 0.0,
                st.cornerOk, st.cornerChecked, st.degenOk, st.degenFaces,
                st.corners, st.notMul256, st.skippedVertical, st.skippedOdd, st.noHeight);
    std::printf("%-28s branches: flat %ld/%ld  tilt %ld/%ld  floorsplit %ld/%ld  roof %ld/%ld  ceilsplit %ld/%ld\n",
                "", st.branchOk[0], st.branch[0], st.branchOk[1], st.branch[1],
                st.branchOk[2], st.branch[2], st.branchOk[3], st.branch[3],
                st.branchOk[4], st.branch[4]);
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: CornerCheck <level.trc> <room|all> [sx]\n"
                    "       CornerCheck sweep <level.trc> ...\n");
        return 1;
    }

    if (std::strcmp(argv[1], "sweep") == 0)
    {
        Stats total;
        for (int a = 2; a < argc; ++a)
        {
            Tr5Level lvl; std::string error;
            if (!lvl.load(toW(argv[a]), error))
            { std::printf("%s: not loadable: %s\n", argv[a], error.c_str()); continue; }
            Stats st;
            const char* name = argv[a];
            for (const char* p = argv[a]; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;
            for (int r = 0; r < (int)lvl.rooms().size(); ++r)
                doRoom(lvl, r, -1, st, name, false);
            report(name, st);
            total.faces += st.faces; total.floorFaces += st.floorFaces; total.floorOk += st.floorOk;
            total.ceilFaces += st.ceilFaces; total.ceilOk += st.ceilOk;
            total.degenFaces += st.degenFaces; total.degenOk += st.degenOk;
            total.cornerChecked += st.cornerChecked; total.cornerOk += st.cornerOk;
            for (int b = 0; b < 5; ++b) { total.branch[b] += st.branch[b]; total.branchOk[b] += st.branchOk[b]; }
            total.skippedVertical += st.skippedVertical; total.skippedOdd += st.skippedOdd;
            total.noHeight += st.noHeight; total.corners += st.corners;
            total.notMul256 += st.notMul256; total.mismatches += st.mismatches;
        }
        std::printf("\n");
        report("TOTAL", total);
        return total.mismatches ? 2 : 0;
    }

    if (std::strcmp(argv[1], "map") == 0 && argc >= 4)
    {
        Tr5Level lvl; std::string error;
        if (!lvl.load(toW(argv[2]), error))
        { std::printf("not loadable: %s\n", error.c_str()); return 1; }
        const int room = std::atoi(argv[3]);
        const Tr5Room& rm = lvl.rooms()[(size_t)room];
        std::printf("room %d  %d x %d (sx x sz)  origin x %d z %d\n",
                    room, rm.numXSectors, rm.numZSectors, rm.originX, rm.originZ);
        for (int sx = 0; sx < rm.numXSectors; ++sx)
        {
            std::printf("sx %2d:", sx);
            for (int sz = 0; sz < rm.numZSectors; ++sz)
            {
                long fy = 0, cy = 0;
                const bool okF = lvl.cornerFloor(room, sx, sz, 0, 0, fy);
                const bool okC = lvl.cornerCeiling(room, sx, sz, 0, 0, cy);
                if (!okF && !okC) std::printf("   wall  ");
                else std::printf(" %5ld/%-5ld", fy, cy);
            }
            std::printf("\n");
        }
        return 0;
    }

    if (std::strcmp(argv[1], "sector") == 0 && argc >= 6)
    {
        Tr5Level lvl; std::string error;
        if (!lvl.load(toW(argv[2]), error))
        { std::printf("not loadable: %s\n", error.c_str()); return 1; }
        const int room = std::atoi(argv[3]), sx = std::atoi(argv[4]), sz = std::atoi(argv[5]);
        const Tr5Room& rm = lvl.rooms()[(size_t)room];
        std::printf("room %d origin x %d z %d  %dx%d sectors\n",
                    room, rm.originX, rm.originZ, rm.numXSectors, rm.numZSectors);
        const int idx = lvl.sectorIndex(room, sx, sz);
        std::printf("sector (%d,%d) fd index %d\n", sx, sz, idx);
        std::vector<Tr5FloorEntry> chain;
        if (idx > 0 && lvl.readChain(idx, chain))
            for (size_t i = 0; i < chain.size(); ++i)
            {
                std::printf("   entry fn %2d kind %2d :", chain[i].function, chain[i].kind);
                for (size_t k = 0; k < chain[i].words.size(); ++k) std::printf(" %04X", chain[i].words[k]);
                std::printf("\n");
            }
        static const int C[4][2] = { {0,0},{1024,0},{0,1024},{1024,1024} };
        for (int c = 0; c < 4; ++c)
        {
            long fy = 0, cy = 0;
            const bool okF = lvl.cornerFloor(room, sx, sz, C[c][0], C[c][1], fy);
            const bool okC = lvl.cornerCeiling(room, sx, sz, C[c][0], C[c][1], cy);
            std::printf("   corner (%4d,%4d)  floor %s%-8ld ceiling %s%ld\n",
                        C[c][0], C[c][1], okF ? "" : "WALL ", fy, okC ? "" : "WALL ", cy);
        }
        return 0;
    }

    if (argc < 3) { std::printf("usage: CornerCheck <level.trc> <room|all> [sx]\n"); return 1; }

    Tr5Level lvl; std::string error;
    if (!lvl.load(toW(argv[1]), error))
    { std::printf("not loadable: %s\n", error.c_str()); return 1; }

    const char* name = argv[1];
    for (const char* p = argv[1]; *p; ++p) if (*p == '\\' || *p == '/') name = p + 1;

    Stats st;
    if (std::strcmp(argv[2], "all") == 0)
    {
        for (int r = 0; r < (int)lvl.rooms().size(); ++r) doRoom(lvl, r, -1, st, name, false);
        report(name, st);
    }
    else
    {
        const int room = std::atoi(argv[2]);
        if (room < 0 || room >= (int)lvl.rooms().size()) { std::printf("room out of range\n"); return 1; }
        const int onlySx = (argc >= 4) ? std::atoi(argv[3]) : -1;
        doRoom(lvl, room, onlySx, st, name, true);
        report("room", st);
    }
    return st.mismatches ? 2 : 0;
}
