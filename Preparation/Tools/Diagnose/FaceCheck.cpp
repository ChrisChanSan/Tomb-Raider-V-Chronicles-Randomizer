// FaceCheck - the drawn faces of a room, and which of them cover a sector.
//
// Occasion (#136): a new Make_Portal opens the passage in the floordata and
// registers the rooms with each other, but you still cannot SEE through - the
// wall is additionally a textured quad in the room geometry, and the portal
// underneath does not hide it.
//
// The user's remark that put this on rails: no editor of the old days could
// ever put a texture ON a portal, "that is why they were always something
// special". So a genuine portal sector carries NO face at all, and making a
// new one see-through means taking the face away rather than inventing a
// transparent texture.
//
// Usage: FaceCheck <level.trc> <room> [sx sz]
//        without a sector: every face of the room
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "core/Tr5Level.h"

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (const char* p = s; *p; ++p) w.push_back((wchar_t)(unsigned char)*p);
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: FaceCheck <level.trc> <room> [sx sz]\n");
        return 1;
    }

    Tr5Level lvl;
    std::string error;
    if (!lvl.load(toW(argv[1]), error))
    { std::printf("not loadable: %s\n", error.c_str()); return 1; }

    const int r = std::atoi(argv[2]);
    const bool bySector = (argc >= 5);
    const int sx = bySector ? std::atoi(argv[3]) : -1;
    const int sz = bySector ? std::atoi(argv[4]) : -1;

    const std::vector<Tr5Room>& rooms = lvl.rooms();
    if (r < 0 || r >= (int)rooms.size())
    { std::printf("room %d: out of range (0..%zu)\n", r, rooms.size() - 1); return 1; }
    const Tr5Room& rm = rooms[(size_t)r];

    // When nothing comes back, say WHY. roomFaces gives up quietly on a
    // room whose geometry offsets carry the odd "none" mark or whose
    // roomlet count looks wrong, and a silent empty list reads exactly like
    // a room that truly has no faces.
    {
        unsigned n = 0, ro = 0, vo = 0, fo = 0; std::string herr;
        lvl.headerField(r, 168, n, herr); lvl.headerField(r, 172, ro, herr);
        lvl.headerField(r, 176, vo, herr); lvl.headerField(r, 180, fo, herr);
        std::printf("          nRoomlets %d  pRoomlets 0x%X  pRmVtx 0x%X  pRmFace 0x%X\n",
                    (int)n, ro, vo, fo);
    }

    std::vector<Tr5Level::FaceInfo> faces;
    if (!lvl.roomFaces(r, faces, error))
    { std::printf("room %d: %s\n", r, error.c_str()); return 1; }

    std::printf("room %3d: %dx%d sectors  x %d..%d  z %d..%d  -  %zu faces\n",
                r, rm.numXSectors, rm.numZSectors,
                rm.originX, rm.originX + rm.numXSectors * 1024,
                rm.originZ, rm.originZ + rm.numZSectors * 1024,
                faces.size());

    // The sector's own square of the world, so a face can be judged by where
    // it lies rather than by which roomlet happens to hold it.
    const long x0 = rm.originX + (long)sx * 1024, x1 = x0 + 1024;
    const long z0 = rm.originZ + (long)sz * 1024, z1 = z0 + 1024;
    if (bySector)
        std::printf("sector (x%d,z%d) covers x %ld..%ld  z %ld..%ld\n", sx, sz, x0, x1, z0, z1);

    size_t shown = 0;
    for (size_t i = 0; i < faces.size(); ++i)
    {
        const Tr5Level::FaceInfo& f = faces[i];
        const int n = f.quad ? 4 : 3;

        if (bySector)
        {
            bool inside = true;
            for (int k = 0; k < n; ++k)
                if (f.v[k][0] < x0 || f.v[k][0] > x1 || f.v[k][2] < z0 || f.v[k][2] > z1)
                { inside = false; break; }
            if (!inside) continue;
        }

        std::printf("  roomlet %2d  %s  tex %4d%s  fx 0x%04X  at 0x%zX  corners",
                    f.roomlet, f.quad ? "quad" : "tri ", f.texInfo,
                    f.doubleSided ? " 2sided" : "       ", f.effects, f.texWordAt);
        for (int k = 0; k < n; ++k)
            std::printf("  (%ld,%ld,%ld)", f.v[k][0], f.v[k][1], f.v[k][2]);
        std::printf("\n");
        ++shown;
    }

    if (bySector && !shown)
        std::printf("  no face covers that sector - which is what a real portal looks like\n");
    return 0;
}
