// WinCheck - holds Tr5Level::wallWindow (#137) against the 32-byte view
// windows Core actually shipped.
//
//   WinCheck <folder with the vanilla .trc>
//
// For every one-sector wall window of the campaign levels it works out which
// sector pair the window sits on, asks wallWindow for the corner-exact
// opening, and compares. The two height bytes of the portal square are
// measured alongside, because that is what the old build wrote.
//
// Where the two differ, the DIRECTION matters: a window only clips
// (SetRoomBounds, draw.cpp:1687), so a window larger than the opening costs
// nothing and a smaller one blacks part of the neighbouring room out.
//
// Diagnostic scaffolding, not part of the shipped tools.
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::wstring wide(const std::string& s)
{
    std::wstring w;
    for (char c : s) w += static_cast<wchar_t>(static_cast<unsigned char>(c));
    return w;
}

int main(int argc, char** argv)
{
    if (argc < 2) { printf("WinCheck <folder with the vanilla .trc>\n"); return 1; }
    const std::string dir = argv[1];

    static const char* LEVELS[] = { "Andrea1", "Andrea2", "Andrea3", "Andy2", "andy1",
                                    "andy3", "joby2", "joby3", "joby4", "joby5",
                                    "rich1", "rich2", "rich3" };

    long nWall = 0, nRect = 0, nTrap = 0, nLost = 0, nDeclined = 0;
    long okExact = 0, badExact = 0, okBytes = 0, badBytes = 0;
    long coreBigger = 0, coreSmaller = 0, coreMixed = 0;
    int shown = 0;

    for (const char* name : LEVELS)
    {
        Tr5Level lvl;
        std::string err;
        const std::string path = dir + "\\" + name + ".trc";
        if (!lvl.load(wide(path), err))
        { printf("%s: %s\n", path.c_str(), err.c_str()); return 1; }

        for (int R = 0; R < static_cast<int>(lvl.rooms().size()); ++R)
        {
            std::vector<Tr5Level::PortalRecord> recs;
            if (!lvl.portalRecords(R, recs, err)) continue;
            std::vector<Tr5Level::SectorInfo> secs;
            if (!lvl.sectorInfo(R, secs, err)) continue;

            const Tr5Room& r = lvl.rooms()[static_cast<size_t>(R)];

            for (const Tr5Level::PortalRecord& p : recs)
            {
                if (p.ny != 0) continue;                       // floor / ceiling

                int lo[3] = { p.v[0][0], p.v[0][1], p.v[0][2] };
                int hi[3] = { lo[0], lo[1], lo[2] };
                for (int k = 1; k < 4; ++k)
                    for (int c = 0; c < 3; ++c)
                    {
                        if (p.v[k][c] < lo[c]) lo[c] = p.v[k][c];
                        if (p.v[k][c] > hi[c]) hi[c] = p.v[k][c];
                    }

                const bool planeX = (hi[0] - lo[0]) <= 2;
                const bool planeZ = (hi[2] - lo[2]) <= 2;
                if (planeX == planeZ) continue;                // odd window
                const int span = planeX ? (hi[2] - lo[2]) : (hi[0] - lo[0]);
                if (span != 1024) continue;                    // wider than a sector
                ++nWall;

                int ys[4]; for (int k = 0; k < 4; ++k) ys[k] = p.v[k][1];
                int distinct = 0;
                for (int k = 0; k < 4; ++k)
                {
                    bool seen = false;
                    for (int q = 0; q < k; ++q) if (ys[q] == ys[k]) seen = true;
                    if (!seen) ++distinct;
                }
                if (distinct > 2) { ++nTrap; continue; }
                ++nRect;

                // Which sector does the window sit on? It lies on the INNER
                // edge of the portal square, one unit in front of the plane
                // where the normal is positive.
                int sx = 0, sz = 0;
                if (planeX)
                {
                    if (p.nx > 0) sx = (lo[0] + 1) / 1024 - 1;
                    else          sx = lo[0] / 1024;
                    sz = lo[2] / 1024;
                }
                else
                {
                    if (p.nz > 0) sz = (lo[2] + 1) / 1024 - 1;
                    else          sz = lo[2] / 1024;
                    sx = lo[0] / 1024;
                }
                if (sx < 0 || sz < 0 || sx >= r.numXSectors || sz >= r.numZSectors)
                { ++nLost; continue; }

                const Tr5Level::SectorInfo* sec = nullptr;
                for (const Tr5Level::SectorInfo& s : secs)
                    if (s.sx == sx && s.sz == sz) sec = &s;
                if (!sec || sec->doorTo != p.toRoom) { ++nLost; continue; }

                long wc = 0, wf = 0;
                if (!lvl.wallWindow(R, sx, sz, p.toRoom, wc, wf)) { ++nDeclined; continue; }

                const long top = lo[1], bot = hi[1];
                if (top == wc && bot == wf) ++okExact; else ++badExact;

                const long bTop = static_cast<long>(sec->ceiling) * 256;
                const long bBot = static_cast<long>(sec->floor) * 256;
                if (top == bTop && bot == bBot) ++okBytes; else ++badBytes;

                if (top != wc || bot != wf)
                {
                    const bool bigger  = (top <= wc && bot >= wf);
                    const bool smaller = (top >= wc && bot <= wf);
                    if (bigger) ++coreBigger;
                    else if (smaller) ++coreSmaller;
                    else ++coreMixed;
                    if (!bigger && shown < 12)
                    {
                        ++shown;
                        printf("  %s room %3d (x%d,z%d) -> %3d   core %ld..%ld   "
                               "wallWindow %ld..%ld   bytes %ld..%ld\n",
                               name, R, sx, sz, p.toRoom, top, bot, wc, wf, bTop, bBot);
                    }
                }
            }
        }
    }

    printf("\n%7ld  one-sector wall windows\n", nWall);
    printf("%7ld    rectangle\n", nRect);
    printf("%7ld    trapezoid\n", nTrap);
    printf("%7ld    sector pair not locatable\n", nLost);
    printf("%7ld    wallWindow declined\n", nDeclined);
    printf("%7ld  == wallWindow      (%.1f %%)\n", okExact,
           (okExact + badExact) ? 100.0 * okExact / (okExact + badExact) : 0.0);
    printf("%7ld  != wallWindow\n", badExact);
    printf("%7ld  == the two height bytes (%.1f %%)\n", okBytes,
           (okBytes + badBytes) ? 100.0 * okBytes / (okBytes + badBytes) : 0.0);
    printf("%7ld  != the two height bytes\n", badBytes);
    printf("\n  of the %ld differences: %ld core LARGER, %ld core SMALLER, %ld mixed\n",
           badExact, coreBigger, coreSmaller, coreMixed);
    return 0;
}
