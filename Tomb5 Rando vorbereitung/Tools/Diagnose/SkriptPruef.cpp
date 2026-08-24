// SkriptPruef - liest die Objektbefehle (Name und Inventarpose) je Level aus
// einer TR5-SCRIPT.DAT.
//
// Warum es das gibt: Name, Y-Versatz, Groesse und Drehung eines Key-, Puzzle-
// oder Pickup-Items stehen NICHT im Level, sondern im Skriptblock des Levels
// (gameflow.cpp:371ff, "inventry_objects_list[n].objname = ..."). Wird ein
// solches Modell in ein Level importiert, das den passenden Befehl nicht hat,
// traegt es im Inventar Namen und Pose des zuletzt gesetzten Levels -
// inventry_objects_list wird zwischen Leveln nie zurueckgesetzt.
//
// Der Parser in ScriptDat.cpp taugt dafuer nicht: der bricht bei CMD_LEVEL ab.
//
// Aufruf: SkriptPruef <SCRIPT.DAT> [levelname ...]
//         ohne Levelliste alle Level.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

static std::string klein(std::string s)
{
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32;
    return s;
}

// Opcodes aus enum gf_commands (gameflow.h, CMD_FMV = 0x80). Nutzbytelaengen
// 1:1 aus LoadGameflow (gameflow.cpp).
static int nutzlast(unsigned char cmd)
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
        if (cmd >= 0x92 && cmd <= 0xD8) return 14;          // Key/Puzzle/Pickup/Examine/Combos
        return -1;                                          // unbekannt: anhalten
    }
}

static std::string befehlsname(unsigned char c)
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
    if (argc < 2) { std::printf("Aufruf: SkriptPruef <SCRIPT.DAT> [levelname ...]\n"); return 1; }

    bool rohModus = false;
    for (int a = 1; a < argc; ++a)
        if (std::strcmp(argv[a], "-roh") == 0) { rohModus = true; argv[a] = (char*)"-"; }

    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::printf("nicht lesbar: %s\n", argv[1]); return 1; }
    std::fseek(f, 0, SEEK_END); long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
    std::vector<unsigned char> d((size_t)n);
    if (std::fread(d.data(), 1, (size_t)n, f) != (size_t)n) { std::fclose(f); std::printf("unvollstaendig\n"); return 1; }
    std::fclose(f);

    // GAMEFLOW (global/types.h:615) ist 16 Byte, dann 40 Byte "[PCExtensions]".
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

    if (scrWad + ScriptLen > d.size()) { std::printf("Kopf passt nicht zur Datei\n"); return 1; }
    std::printf("%s: %u Level, %u Dateinamen, Skript-Wad %u Byte\n\n", argv[1], nLevels, nFileNames, ScriptLen);

    for (unsigned i = 0; i < nLevels; ++i)
    {
        const size_t start = scrWad + u16(scrOfsAt + 2ull * i);
        const size_t ende  = (i + 1 < nLevels) ? scrWad + u16(scrOfsAt + 2ull * (i + 1))
                                               : scrWad + ScriptLen;

        // Erst den Dateinamen holen: er steckt im CMD_LEVEL des Blocks.
        std::string file;
        for (size_t q = start; q < ende; )
        {
            const int len = nutzlast(d[q]);
            if (len < 0) break;
            if (d[q] == 0x81 && q + 4 < d.size() && d[q + 4] < nFileNames)
            {
                const size_t o = nameWadAt + u16(nameOfsAt + 2ull * d[q + 4]);
                if (o < d.size()) file = (const char*)&d[o];
                break;
            }
            q += 1 + len;
        }

        if (argc > 2 + (rohModus ? 1 : 0))
        {
            bool gewollt = false;
            for (int a = 2; a < argc; ++a)
                if (std::strcmp(argv[a],"-") && klein(file) == klein(argv[a])) { gewollt = true; break; }
            if (!gewollt) continue;
        }

        std::printf("Level %2u  %s\n", i, file.empty() ? "(ohne Dateinamen)" : file.c_str());
        int objekte = 0;
        for (size_t q = start; q < ende; )
        {
            const unsigned char cmd = d[q];
            const int len = nutzlast(cmd);
            if (len < 0) { std::printf("   ! unbekannter Befehl %u bei +%zu - Abbruch\n", cmd, q - start); break; }

            if (cmd >= 0x92 && cmd <= 0xD8 && q + 14 < d.size())
            {
                const unsigned char* g = &d[q + 1];
                std::printf("   %-16s Name=%-5u yoff=%-6d Groesse=%-6u yrot=%-6u xrot=%-6u zrot=%-6u Flags=0x%04X\n",
                            befehlsname(cmd).c_str(),
                            (unsigned)(g[0] | (g[1] << 8)),
                            (int)(short)(g[2] | (g[3] << 8)),
                            (unsigned)(g[4] | (g[5] << 8)),
                            (unsigned)(g[6] | (g[7] << 8)),
                            (unsigned)(g[8] | (g[9] << 8)),
                            (unsigned)(g[10] | (g[11] << 8)),
                            (unsigned)(g[12] | (g[13] << 8)));
                ++objekte;
            }
            q += 1 + len;
        }
        if (rohModus)
        {
            std::printf("   -- Befehlsfolge --\n");
            for (size_t q = start; q < ende; )
            {
                const int l = nutzlast(d[q]);
                const char* zusatz = "";
                char puffer[64];
                if (d[q] == 0x81) zusatz = "   <== CMD_LEVEL";
                else if ((d[q] == 0xD9 || d[q] == 0xDA) && q + 1 < d.size())
                {
                    // Nutzbyte 0 ist die INVENTAR-Objektnummer (enum
                    // invobj_types, newinv2.h:121): 0 = Uzi, 1 = Pistolen,
                    // 19 = Pistolenmunition. setup.cpp:122/160 wendet sie
                    // beim Levelstart an - GIVEOBJ legt ins Inventar,
                    // TAKEOBJ nimmt heraus (NailInvItem).
                    std::snprintf(puffer, sizeof(puffer), "   %s Inventarobjekt %u",
                                  d[q] == 0xD9 ? "gibt" : "NIMMT", d[q + 1]);
                    zusatz = puffer;
                }
                std::printf("   +%-4zu  %-3u  %s%s\n", q - start, d[q], befehlsname(d[q]).c_str(), zusatz);
                if (l < 0) break;
                q += 1 + l;
            }
        }
        if (!objekte) std::printf("   (kein einziger Objektbefehl)\n");
        std::printf("\n");
    }
    return 0;
}
