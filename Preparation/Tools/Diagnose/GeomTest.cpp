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

// GeomTest - tries buildRoomGeometry (#137) on one room of one level and
// reports what it did. Diagnostic scaffolding, not part of the shipped tools.
//
//   GeomTest <level.trc> <room> [likeRoom] [out.trc]
#include "core/Tr5Level.h"

#include <cstdio>
#include <string>
#include <vector>

static std::wstring wide(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w += static_cast<wchar_t>(static_cast<unsigned char>(*p));
    return w;
}

static void report(Tr5Level& lvl, int room, const char* when)
{
    std::vector<Tr5Level::FaceInfo> f;
    std::string e;
    if (!lvl.roomFaces(room, f, e)) { printf("  %-7s roomFaces failed: %s\n", when, e.c_str()); return; }

    long lo[3] = { 1 << 30, 1 << 30, 1 << 30 }, hi[3] = { -(1 << 30), -(1 << 30), -(1 << 30) };
    int bad = 0, tex = -1, mixed = 0, dbl = 0, fx = 0, tris = 0;
    for (const Tr5Level::FaceInfo& q : f)
    {
        if (tex < 0) tex = q.texInfo; else if (tex != q.texInfo) mixed++;
        if (q.doubleSided) dbl++;
        if (q.effects) fx++;
        const int corners = q.quad ? 4 : 3;
        if (!q.quad) tris++;
        for (int k = 0; k < corners; ++k)
            for (int a = 0; a < 3; ++a)
            { if (q.v[k][a] < lo[a]) lo[a] = q.v[k][a]; if (q.v[k][a] > hi[a]) hi[a] = q.v[k][a]; }

        // The winding rule: cross(v1-v0, v2-v0) must point AWAY from the
        // inside, so its dot with the inward normal is negative. Without the
        // stored normal here, check only that the face is not degenerate.
        const long ax = q.v[1][0] - q.v[0][0], ay = q.v[1][1] - q.v[0][1], az = q.v[1][2] - q.v[0][2];
        const long bx = q.v[2][0] - q.v[0][0], by = q.v[2][1] - q.v[0][1], bz = q.v[2][2] - q.v[0][2];
        const long cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
        if (!cx && !cy && !cz) bad++;
    }

    printf("  %-7s %3zu faces (%d tri)", when, f.size(), tris);
    if (!f.empty())
        printf("   x %ld..%ld  y %ld..%ld  z %ld..%ld   texinfo %d%s   twoSided %d  fx %d  degenerate %d",
               lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], tex,
               mixed ? " (mixed)" : "", dbl, fx, bad);
    printf("\n");
}

int main(int argc, char** argv)
{
    if (argc < 3) { printf("GeomTest <level.trc> <room> [likeRoom] [out.trc]\n"); return 1; }

    const int room = atoi(argv[2]);
    const int like = (argc > 3) ? atoi(argv[3]) : -1;

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(wide(argv[1]), err)) { printf("load: %s\n", err.c_str()); return 1; }

    printf("%s room %d, like=%d\n", argv[1], room, like);
    report(lvl, room, "before");
    if (like >= 0) report(lvl, like, "source");

    std::vector<Tr5Level::RoomTexRule> rules;
    if (!lvl.buildRoomGeometry(room, like, rules, err))
    { printf("  buildRoomGeometry: %s\n", err.c_str()); return 1; }

    report(lvl, room, "after");

    // Second call must change nothing - Patch::apply runs twice.
    const size_t before = lvl.mainBlockSize();
    if (!lvl.buildRoomGeometry(room, like, rules, err))
    { printf("  second call: %s\n", err.c_str()); return 1; }
    printf("  repeat  main block %s (%zu)\n",
           (lvl.mainBlockSize() == before) ? "unchanged" : "CHANGED!", lvl.mainBlockSize());
    printf("  parse   %d room mismatches\n", lvl.roomParseMismatches());

    if (argc > 4)
    {
        if (!lvl.save(wide(argv[4]), err)) { printf("save: %s\n", err.c_str()); return 1; }
        printf("  wrote   %s\n", argv[4]);
    }
    return 0;
}
