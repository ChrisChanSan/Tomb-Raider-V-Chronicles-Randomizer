// ScriptCheck - reads the object commands (name and inventory pose) per
// level from a TR5 SCRIPT.DAT.
//
// Why this exists: name, y offset, size and rotation of a key, puzzle or
// pickup item do NOT stand in the level but in the level's script block
// (gameflow.cpp:371ff, "inventry_objects_list[n].objname = ..."). If such
// a model is imported into a level that lacks the matching command, it
// carries in the inventory the name and pose of the last level that set
// them - inventry_objects_list is never reset between levels.
//
// The parser in ScriptDat.cpp is no use for this: it stops at CMD_LEVEL.
//
// Usage: ScriptCheck <SCRIPT.DAT> [levelname ...]
//        without a level list, all levels.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static std::string lower(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// Opcodes from enum gf_commands (gameflow.h, CMD_FMV = 0x80). Payload byte
// lengths 1:1 from LoadGameflow (gameflow.cpp).
static int payload(unsigned char cmd)
{
    switch (cmd)
    {
    case 0x80: case 0x84: case 0x85: case 0x86: case 0x87: case 0x88:
    case 0x8B: case 0x8C: case 0x90: case 0x91: return 1;
    case 0x81:                                  return 5;   // LEVEL
    case 0x82: case 0x89: case 0x8A:            return 4;   // TITLE, LAYER1/2
    case 0x83:                                  return 0;   // ENDSEQ
    case 0x8D:                                  return 9;   // LENSFLARE
    case 0x8E:                                  return 5;   // MIRROR
    case 0x8F:                                  return 3;   // FOG
    case 0xD9: case 0xDA:                       return 2;   // GIVEOBJ/TAKEOBJ
    default:
        if (cmd >= 0x92 && cmd <= 0xD8) return 14;          // key/puzzle/pickup/examine/combos
        return -1;                                          // unknown: stop
    }
}

static std::string commandName(unsigned char c)
{
    char b[64];
    if (c >= 146 && c <= 157) { std::sprintf(b, "KEY%d",     c - 145); return b; }
    if (c >= 158 && c <= 169) { std::sprintf(b, "PUZZLE%d",  c - 157); return b; }
    if (c >= 170 && c <= 173) { std::sprintf(b, "PICKUP%d",  c - 169); return b; }
    if (c >= 174 && c <= 176) { std::sprintf(b, "EXAMINE%d", c - 173); return b; }
    if (c >= 177 && c <= 192) { std::sprintf(b, "KEYCOMBO%d_%d",     (c - 177) / 2 + 1, (c - 177) % 2 + 1); return b; }
    if (c >= 193 && c <= 208) { std::sprintf(b, "PUZZLECOMBO%d_%d",  (c - 193) / 2 + 1, (c - 193) % 2 + 1); return b; }
    if (c >= 209 && c <= 216) { std::sprintf(b, "PICKUPCOMBO%d_%d",  (c - 209) / 2 + 1, (c - 209) % 2 + 1); return b; }
    std::sprintf(b, "cmd%u", c);
    return b;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: ScriptCheck <SCRIPT.DAT> [levelname ...]\n"); return 1; }

    bool rawMode = false;
    for (int a = 1; a < argc; ++a)
        if (std::strcmp(argv[a], "-raw") == 0) { rawMode = true; argv[a] = (char*)"-"; }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("not readable: %s\n", argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> d((size_t)n);
    if (std::fread(d.data(), 1, (size_t)n, f) != (size_t)n) { std::fclose(f); std::printf("incomplete\n"); return 1; }
    std::fclose(f);

    // GAMEFLOW (global/types.h:615) is 16 bytes, then 40 bytes "[PCExtensions]".
    const unsigned nLevels    = d[9];
    const unsigned nFileNames = d[10];
    auto u16 = [&](size_t o) { return (unsigned)(d[o] | (d[o + 1] << 8)); };
    const unsigned FileNameLen = u16(12);
    const unsigned ScriptLen   = u16(14);

    size_t p = 16 + 40;
    const size_t nameOfsAt = p;   p += 2ull * nFileNames;
    const size_t nameWadAt = p;   p += FileNameLen;
    const size_t scrOfsAt  = p;   p += 2ull * nLevels;
    const size_t scrWad    = p;

    if (scrWad + ScriptLen > d.size()) { std::printf("header does not fit the file\n"); return 1; }
    std::printf("%s: %u levels, %u file names, script wad %u bytes\n\n", argv[1], nLevels, nFileNames, ScriptLen);

    for (unsigned i = 0; i < nLevels; ++i)
    {
        const size_t start = scrWad + u16(scrOfsAt + 2ull * i);
        const size_t end   = (i + 1 < nLevels) ? scrWad + u16(scrOfsAt + 2ull * (i + 1))
                                               : scrWad + ScriptLen;

        // Fetch the file name first: it sits in the block's CMD_LEVEL.
        std::string file;
        for (size_t q = start; q < end; )
        {
            const int len = payload(d[q]);
            if (len < 0) break;
            if (d[q] == 0x81 && q + 4 < d.size() && d[q + 4] < nFileNames)
            {
                const size_t o = nameWadAt + u16(nameOfsAt + 2ull * d[q + 4]);
                if (o < d.size()) file = (const char*)&d[o];
                break;
            }
            q += 1 + len;
        }

        if (argc > 2 + (rawMode ? 1 : 0))
        {
            bool wanted = false;
            for (int a = 2; a < argc; ++a)
                if (std::strcmp(argv[a],"-") && lower(file) == lower(argv[a])) { wanted = true; break; }
            if (!wanted) continue;
        }

        std::printf("level %2u  %s\n", i, file.empty() ? "(without a file name)" : file.c_str());
        int objects = 0;
        for (size_t q = start; q < end; )
        {
            const unsigned char cmd = d[q];
            const int len = payload(cmd);
            if (len < 0) { std::printf("   ! unknown command %u at +%zu - stopping\n", cmd, q - start); break; }

            if (cmd >= 0x92 && cmd <= 0xD8 && q + 14 < d.size())
            {
                const unsigned char* g = &d[q + 1];
                std::printf("   %-16s name=%-5u yoff=%-6d size=%-6u yrot=%-6u xrot=%-6u zrot=%-6u flags=0x%04X\n",
                            commandName(cmd).c_str(),
                            (unsigned)(g[0] | (g[1] << 8)),
                            (int)(short)(g[2] | (g[3] << 8)),
                            (unsigned)(g[4] | (g[5] << 8)),
                            (unsigned)(g[6] | (g[7] << 8)),
                            (unsigned)(g[8] | (g[9] << 8)),
                            (unsigned)(g[10] | (g[11] << 8)),
                            (unsigned)(g[12] | (g[13] << 8)));
                ++objects;
            }
            q += 1 + len;
        }
        if (rawMode)
        {
            std::printf("   -- command sequence --\n");
            for (size_t q = start; q < end; )
            {
                const int l = payload(d[q]);
                const char* extra = "";
                char buffer[64];
                if (d[q] == 0x81) extra = "   <== CMD_LEVEL";
                else if ((d[q] == 0xD9 || d[q] == 0xDA) && q + 1 < d.size())
                {
                    // Payload byte 0 is the INVENTORY object number (enum
                    // invobj_types, newinv2.h:121): 0 = Uzi, 1 = pistols,
                    // 19 = pistol ammo. setup.cpp:122/160 applies it at
                    // level start - GIVEOBJ puts into the inventory,
                    // TAKEOBJ takes out (NailInvItem).
                    std::snprintf(buffer, sizeof(buffer), "   %s inventory object %u",
                                  d[q] == 0xD9 ? "gives" : "TAKES", d[q + 1]);
                    extra = buffer;
                }
                std::printf("   +%-4zu  %-3u  %s%s\n", q - start, d[q], commandName(d[q]).c_str(), extra);
                if (l < 0) break;
                q += 1 + l;
            }
        }
        if (!objects) std::printf("   (not a single object command)\n");
        std::printf("\n");
    }
    return 0;
}
