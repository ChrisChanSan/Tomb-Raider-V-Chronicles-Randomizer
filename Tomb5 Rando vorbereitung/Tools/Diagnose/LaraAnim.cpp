// LaraAnim - welche von Laras Animationen ein Level WIRKLICH traegt.
//
// Warum es das gibt: Laras Anim-Tabelle ist in jedem TR5-Level gleich lang,
// die BILDDATEN sind aber levelspezifisch. Wo ein Level eine Animation nie
// braucht, laesst Core den Eintrag stehen und zeigt mit seinem Frame-Zeiger
// auf fremde Daten - abgespielt wird dann Unsinn (eingefrorene Pose, falsche
// Bewegung, falsche Laenge). Aufgefallen an ANIM_KEYCARD (437) in joby3 und
// am Schalter in andy3.
//
// Die Probe ist eine Platzrechnung, keine Vermutung:
//   Keyframes = (frame_end - frame_base) / rate + 1
//   gebraucht = Keyframes * FrameSize * 2 Byte
//   vorhanden = naechstgroesserer Frame-Zeiger IRGENDEINER Anim - eigener
// Passt das Gebrauchte nicht in das Vorhandene, koennen die Daten dieser
// Animation dort nicht liegen.
//
// Mehrere Anims duerfen sich EINEN Zeiger teilen, wenn sie dieselbe
// frame_base haben - dann sind es Varianten desselben Bewegungsblocks
// (andy3: 468/469/470 starten alle auf Frame 8190). Gezaehlt wird deshalb
// der Platz fuer die LAENGSTE Anim der Gruppe.
//
// Aufruf: LaraAnim <level.trc> [level.trc ...]        Uebersicht je Level
//         LaraAnim -alle <level.trc> [...]            jede Anim einzeln
//         LaraAnim -anim <nr> <level.trc> [...]       eine Anim im Vergleich
#include "core/Tr5Level.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static std::wstring zuW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

// Die Animationen, die der Motor bei Objektinteraktionen auswaehlt. Ohne
// echte Daten sieht Lara nicht nur falsch aus - die Laenge stimmt dann auch
// nicht, und sie kann den Sektor verlassen, bevor der Trigger greift.
struct AnimName { int nr; const char* name; };
static const AnimName kWichtig[] = {
    {  63, "SWITCHOFF" },     {  64, "SWITCHON" },      { 131, "USEKEY" },
    { 134, "USEPUZZLE" },     { 135, "PICKUP" },        { 195, "GENERATORSW_OFF" },
    { 196, "GENERATORSW_ON" },{ 197, "ONEHANDPUSHSW" }, { 204, "PICKUPF" },
    { 291, "DUCKPICKUP" },    { 312, "DUCKPICKUPF" },   { 313, "PUSHDOOR" },
    { 314, "PULLDOOR" },      { 315, "KICKDOOR" },      { 316, "BLOCKSWITCH" },
    { 317, "LIFTTRAP" },      { 318, "PULLTRAP" },      { 319, "TURNSWITCHC" },
    { 320, "TURNSWITCHA" },   { 322, "COGSWITCHS" },    { 324, "RAILSWITCH" },
    { 325, "HIDDENPICKUP" },  { 403, "CROWBARDOOR" },   { 412, "TWODOOR" },
    { 413, "SMALLSWITCH" },   { 414, "LEAPSWITCH" },    { 415, "WATERSWITCH" },
    { 416, "WATERDOOR" },     { 419, "CROWBAR" },       { 420, "CROWSWITCH" },
    { 424, "PLINTHHI" },      { 425, "PLINTHLO" },      { 437, "KEYCARD" },
    { 460, "CROWDOVE" },      { 470, "AIRLOCKSW" },     { 471, "JOBYCROW" },
};

struct AnimInfo
{
    uint32_t frameOfs = 0;
    int base = 0, ende = 0, rate = 0, groesse = 0;
    long long gebraucht = 0, vorhanden = 0;
    bool passt = false;
};

// Alle Anims eines Levels vermessen. Liefert false, wenn Lara fehlt.
static bool vermessen(const Tr5Level& lvl, std::vector<AnimInfo>& raus, int& basis)
{
    Tr5Level::MoveableInfo mv;
    if (!lvl.moveableInfo(0, mv) || mv.animIndex < 0) return false;
    basis = mv.animIndex;

    size_t animAt = 0; int32_t nAnims = 0;
    if (!lvl.debugAnimBlock(animAt, nAnims)) return false;
    const std::vector<uint8_t>& b = lvl.debugBytes();
    auto r16 = [&](size_t at) { uint16_t v; std::memcpy(&v, b.data() + at, 2); return v; };
    auto r32 = [&](size_t at) { uint32_t v; std::memcpy(&v, b.data() + at, 4); return v; };

    // Alle Frame-Zeiger einsammeln, um den naechstgroesseren zu finden.
    std::vector<uint32_t> zeiger;
    for (int32_t i = 0; i < nAnims; ++i) zeiger.push_back(r32(animAt + static_cast<size_t>(i) * 40));

    size_t objAt = 0, frAt = 0; int32_t objN = 0, frN = 0;
    lvl.debugModelBlocks(objAt, objN, frAt, frN);
    const uint32_t frameEndeGesamt = static_cast<uint32_t>(frN) * 2;

    raus.clear();
    for (int32_t i = 0; i < nAnims; ++i)
    {
        const size_t o = animAt + static_cast<size_t>(i) * 40;
        AnimInfo a;
        a.frameOfs = r32(o);
        a.rate     = b[o + 4];
        a.groesse  = b[o + 5];
        a.base     = r16(o + 24);
        a.ende     = r16(o + 26);

        const int spanne = a.ende - a.base;
        const int keyframes = (a.rate > 0 && spanne >= 0) ? (spanne / a.rate + 1) : 1;
        a.gebraucht = static_cast<long long>(keyframes) * a.groesse * 2;

        uint32_t naechster = frameEndeGesamt;
        for (uint32_t z : zeiger) if (z > a.frameOfs && z < naechster) naechster = z;
        a.vorhanden = static_cast<long long>(naechster) - a.frameOfs;
        // ENTSCHEIDEND: FrameSize 0 heisst, die Anim hat ueberhaupt keine
        // eigenen Bilddaten. Der Motor rechnet frame_ptr[n * 0] und zeigt
        // ewig dasselbe Bild - genau die eingefrorene Pose, die in joby3 an
        // der Keycard 22 Sekunden stand. Cores Stub-Kennzeichen.
        a.passt = (a.groesse > 0) && (a.gebraucht <= a.vorhanden);
        raus.push_back(a);
    }
    return true;
}

int main(int argc, char** argv)
{
    bool alle = false; int einzeln = -1; int erstes = 1;
    while (erstes < argc && argv[erstes][0] == '-')
    {
        if (!std::strcmp(argv[erstes], "-alle")) { alle = true; ++erstes; }
        else if (!std::strcmp(argv[erstes], "-anim") && erstes + 1 < argc)
        { einzeln = std::atoi(argv[erstes + 1]); erstes += 2; }
        else break;
    }
    if (erstes >= argc)
    {
        std::printf("Aufruf: LaraAnim [-alle] [-anim <nr>] <level.trc> [level.trc ...]\n");
        return 1;
    }

    for (int f = erstes; f < argc; ++f)
    {
        Tr5Level lvl; std::string error;
        if (!lvl.load(zuW(argv[f]), error)) { std::printf("%s: %s\n", argv[f], error.c_str()); continue; }

        std::vector<AnimInfo> a; int basis = 0;
        if (!vermessen(lvl, a, basis)) { std::printf("%s: keine Lara\n", argv[f]); continue; }

        const char* kurz = std::strrchr(argv[f], '\\');
        if (!kurz) kurz = std::strrchr(argv[f], '/');
        kurz = kurz ? kurz + 1 : argv[f];

        if (einzeln >= 0)
        {
            const size_t i = static_cast<size_t>(basis + einzeln);
            if (i >= a.size()) { std::printf("%-16s Anim %d ausserhalb\n", kurz, einzeln); continue; }
            std::printf("%-16s Anim %3d: Ofs %-8u Frames %5d..%-5d Rate %-2d Groesse %-3d  "
                        "braucht %5lld / hat %6lld  %s\n",
                        kurz, einzeln, a[i].frameOfs, a[i].base, a[i].ende, a[i].rate, a[i].groesse,
                        a[i].gebraucht, a[i].vorhanden, a[i].groesse == 0 ? "STUB" : (a[i].passt ? "OK" : "ZU ENG"));
            continue;
        }

        std::printf("\n=== %s  (%zu Anims ab %d) ===\n", kurz, a.size(), basis);
        int fehlend = 0;
        for (const AnimName& w : kWichtig)
        {
            const size_t i = static_cast<size_t>(basis + w.nr);
            if (i >= a.size())
            {
                std::printf("  %-16s (%3d) FEHLT - Tabelle zu kurz\n", w.name, w.nr);
                ++fehlend;
                continue;
            }
            if (a[i].groesse == 0)
            {
                std::printf("  %-16s (%3d) STUB - FrameSize 0, keine eigenen Bilddaten\n", w.name, w.nr);
                ++fehlend;
            }
            else if (!a[i].passt)
            {
                std::printf("  %-16s (%3d) ZU ENG - braucht %lld Byte, hat %lld\n",
                            w.name, w.nr, a[i].gebraucht, a[i].vorhanden);
                ++fehlend;
            }
            else if (alle)
            {
                std::printf("  %-16s (%3d) ok  (%lld/%lld Byte)\n",
                            w.name, w.nr, a[i].gebraucht, a[i].vorhanden);
            }
        }
        if (!fehlend) std::printf("  alle %zu Interaktions-Anims tragen echte Daten\n",
                                  sizeof(kWichtig) / sizeof(kWichtig[0]));
    }
    return 0;
}
