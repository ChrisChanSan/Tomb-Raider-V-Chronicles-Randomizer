//----------------------------------------------------------------------------
// TR3Setz - setzt ein mit TR3Hol geholtes Moveable in ein TR5-Level.
//
// Aufruf: TR3Setz <ziel.trc> <package.bin> <zielSlot> <ausgabe.trc> [texinfo]
//
// texinfo (Vorgabe 0): auf diesen vorhandenen Texinfo zeigen danach ALLE
// Faces. Es wird keine Texturseite angefasst - siehe Tr5Tr3Import.cpp.
//----------------------------------------------------------------------------
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

static std::wstring toW(const char* s)
{
    std::wstring w;
    for (; *s; ++s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*s)));
    return w;
}

// Schreibt die SFX-Nummern der Anim-Kommandos im Modellpaket um.
//
// Das Kommandowort 5 (PlaySound) fuehrt zwei Folgeworte: Frame und dann die
// SFX-Nummer, deren obere Bits (12-15) Ausfuehrungsbedingungen tragen (nur
// an Land, nur im Wasser) und stehen bleiben MUESSEN.
//
// Ohne diesen Schritt ruft der Gegner weiter seine TR3-Nummern auf, die im
// Ziel andere oder gar keine Klaenge sind. Dasselbe Vergessen bei der
// Sprungtabelle war der Absturz in #88 - deshalb hier ausdruecklich.
static int remapSfx(std::vector<uint8_t>& package, const std::vector<std::pair<int,int>>& mapping,
                      std::vector<int>& unresolved, std::vector<std::pair<int,int>>& genutzt,
                      std::vector<std::pair<int,int>>& arten)
{
    auto g32 = [&](size_t p) { uint32_t v; std::memcpy(&v, &package[p], 4); return v; };
    auto g16 = [&](size_t p) { uint16_t v; std::memcpy(&v, &package[p], 2); return v; };

    // Kopf des Modellpakets ueberspringen, um an den Kommandoblock zu kommen.
    const uint32_t nMeshes = g32(12), nAnims = g32(16), nFrameW = g32(20);
    const uint32_t nBoneW = g32(24), nSC = g32(28), nAD = g32(32), nCmd = g32(36);
    size_t p = 48;
    for (uint32_t i = 0; i < nMeshes; ++i) { const uint32_t l = g32(p); p += 4 + l; }
    p += size_t(nBoneW) * 4 + size_t(nFrameW) * 2 + size_t(nAnims) * 40
       + size_t(nSC) * 6 + size_t(nAD) * 8;
    const size_t cmdAt = p;
    if (cmdAt + size_t(nCmd) * 2 > package.size()) return -1;

    // Nur die Kommandos anfassen, auf die die Animationen DIESES Objekts
    // zeigen. Das Paket fuehrt den ganzen Kommandoblock des Quelllevels mit
    // (die Anims verweisen mit globalen Indizes hinein) - wer stumpf ueber
    // alles laeuft, sieht auch die Klaenge von Raptoren, Tueren und Lara und
    // meldet Dutzende "fehlender" Zuordnungen, die niemanden interessieren.
    // Je Anim stehen Anzahl bei +36 und Startindex bei +38.
    const size_t animAt = cmdAt - size_t(nAD) * 8 - size_t(nSC) * 6 - size_t(nAnims) * 40;

    int getauscht = 0;
    for (uint32_t a = 0; a < nAnims; ++a)
    {
        const size_t ao = animAt + size_t(a) * 40;
        const uint16_t anzahl = g16(ao + 36);
        if (!anzahl) continue;
        const uint16_t start = g16(ao + 38);

    // ANZAHL ZAEHLT KOMMANDOS, NICHT WORTE.
    //
    // Hier stand "i < start + anzahl" mit i als WORTindex - und weil ein
    // Kommando je nach Art 1 bis 4 Worte belegt, brach die Schleife viel zu
    // frueh ab. Sie sah nur die ersten paar Kommandos einer Animation, der
    // Rest blieb unangetastet UND unbemerkt: die Meldung "ohne Zuordnung"
    // kann nur melden, was sie ueberhaupt gelesen hat.
    //
    // Aufgefallen ist es am Compsognathus. Seine Laufanimation ruft den
    // Schritt mehrfach; die erste Fundstelle wurde umgebogen, die spaeteren
    // behielten ihre TR3-Nummer 243 - und 243 ist in TR5
    // SFX_GENERIC_HEAVY_FEET, ein wuchtiger schwerer Schritt. Genau den hat
    // der Nutzer gehoert, waehrend alle Messungen an Modell, Zuordnung,
    // Soundmap und Wave-Daten sauber blieben. Sichtbar wurde es erst durch
    // das Klangprotokoll in der Engine.
    uint32_t i = start;
    for (uint16_t k = 0; k < anzahl && i < nCmd; ++k)
    {
        const uint16_t art = g16(cmdAt + size_t(i) * 2);
        ++i;
        int folge = 0;
        switch (art)
        {
        case 1: folge = 3; break;    // SetPosition
        case 2: folge = 2; break;    // JumpVelocity
        case 5: folge = 2; break;    // PlaySound: Frame + SFX
        case 6: folge = 2; break;    // Flipeffect
        default: folge = 0; break;   // EmptyHands, KillItem, Unbekanntes
        }

        // Kommandoarten mitzaehlen. Ein Flipeffect (6) traegt eine Nummer,
        // die in TR3 und TR5 VERSCHIEDENE Wirkungen hat und in TR5 durchaus
        // einen Klang ausloesen kann - er wird hier NICHT umgebogen. Wer nur
        // die PlaySounds betrachtet, uebersieht das.
        arten.push_back(std::make_pair((int)art, (int)a));

        if (art == 5 && i + 1 < nCmd)
        {
            const size_t at = cmdAt + size_t(i + 1) * 2;
            const uint16_t w = g16(at);
            const int nr = w & 0x0FFF;
            bool zugeordnet = false;
            for (const auto& z : mapping)
                if (z.first == nr)
                {
                    const uint16_t neu = static_cast<uint16_t>((w & 0xF000) | (z.second & 0x0FFF));
                    std::memcpy(&package[at], &neu, 2);
                    ++getauscht;
                    zugeordnet = true;
                    // Mitzaehlen, WELCHE ID wie oft und in welcher Animation
                    // gerufen wird. Eine blosse Gesamtzahl sagt nicht, ob der
                    // gesuchte Klang ueberhaupt vorkommt - beim Compy hielt
                    // ich den Laufklang faelschlich fuer belegt, weil die
                    // Summe stimmte.
                    genutzt.push_back(std::make_pair(nr, (int)a));
                    break;
                }

            // Nicht zugeordnete Nummern MELDEN. Sie behalten sonst still
            // ihre TR3-Nummer und treffen im Ziel irgendeinen fremden Klang -
            // genau die Sorte lautloser Fehler, die dieses Projekt schon
            // mehrfach Zeit gekostet hat.
            if (!zugeordnet)
            {
                bool schonGemeldet = false;
                for (int o : unresolved) if (o == nr) { schonGemeldet = true; break; }
                if (!schonGemeldet) unresolved.push_back(nr);
            }
        }
        i += folge;
    }
    }
    return getauscht;
}

static uint32_t paketVersion(const std::vector<uint8_t>& p)
{
    if (p.size() < 8) return 0;
    uint32_t v; std::memcpy(&v, p.data() + 4, 4); return v;
}

// Schreibt die Texturworte der Faces auf die neuen Texinfos um. Gleicher
// Ableser wie ueberall (Kopf 10 Byte, dann das Vertexwort; 0 Normalen heisst
// NORMALEN). Bit 15 des Texturworts ist ein Flag und bleibt stehen.
static int remapTextures(std::vector<uint8_t>& package, const std::map<int,int>& mapping)
{
    auto g32 = [&](size_t q) { uint32_t v; std::memcpy(&v, &package[q], 4); return v; };
    auto g16 = [&](size_t q) { uint16_t v; std::memcpy(&v, &package[q], 2); return v; };

    const uint32_t nMeshes = g32(12);
    size_t p = 48;
    int getauscht = 0;

    for (uint32_t i = 0; i < nMeshes; ++i)
    {
        if (p + 4 > package.size()) return -1;
        const uint32_t len = g32(p); p += 4;
        const size_t ende = p + len;
        size_t q = p + 10;
        if (q + 2 > ende) return -1;
        const int16_t vw = static_cast<int16_t>(g16(q)); q += 2;
        const int nv = vw & 0xFF;
        if (nv == 0) q += (size_t((vw >> 8) & 0xFF) * 6 + 1) * 2;
        else
        {
            q += size_t(nv) * 6;
            const int16_t nn = static_cast<int16_t>(g16(q)); q += 2;
            q += size_t(nv) * (nn >= 0 ? 6 : 2);
        }

        auto biege = [&](size_t at) {
            const uint16_t w = g16(at);
            std::map<int,int>::const_iterator f = mapping.find(w & 0x7FFF);
            if (f == mapping.end()) return;
            const uint16_t neu = static_cast<uint16_t>((w & 0x8000) | (f->second & 0x7FFF));
            std::memcpy(&package[at], &neu, 2);
            ++getauscht;
        };

        const int16_t nq = static_cast<int16_t>(g16(q)); q += 2;
        for (int k = 0; k < nq; ++k, q += 12) biege(q + 8);
        const int16_t nt = static_cast<int16_t>(g16(q)); q += 2;
        for (int k = 0; k < nt; ++k, q += 10) biege(q + 6);

        p = ende;
    }
    return getauscht;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::printf("Aufruf: TR3Setz <ziel.trc> <package.bin> <zielSlot> <ausgabe.trc> [texinfo] [ton.bin]\n");
        return 2;
    }
    const int slot = std::atoi(argv[3]);

    // Stellungsargumente 5 und 6, danach duerfen Schalter folgen (-basis,
    // -volumes). Ein mit '-' beginnendes Wort ist ein Schalter, kein Wert -
    // sonst zaehlte argc mit und der Tonpfad fiele lautlos auf NULL zurueck.
    auto stellung = [&](int i) -> const char* {
        return (argc > i && argv[i][0] != '-') ? argv[i] : 0; };
    const char* texArg   = stellung(5);
    const int   tex      = texArg ? std::atoi(texArg) : 0;
    const char* tonDatei = stellung(6);

    std::vector<uint8_t> package;
    {
        FILE* f = std::fopen(argv[2], "rb");
        if (!f) { std::printf("Paket nicht lesbar: %s\n", argv[2]); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        package.resize(static_cast<size_t>(n));
        const size_t g = std::fread(package.data(), 1, package.size(), f);
        std::fclose(f);
        if (g != package.size()) { std::printf("Paket unvollstaendig gelesen\n"); return 2; }
    }

    Tr5Level lvl;
    std::string err;
    if (!lvl.load(toW(argv[1]), err)) { std::printf("Level laden: %s\n", err.c_str()); return 2; }

    Tr5Level::MoveableInfo vorher;
    const bool hatte = lvl.moveableInfo(slot, vorher);
    std::printf("Ziel %s: %d Items, Slot %d %s\n", argv[1], lvl.itemCount(), slot,
                hatte ? "vorhanden (wird ersetzt)" : "frei (wird angehaengt)");

    // Sounds ZUERST: erst danach steht fest, auf welche Plaetze die
    // SFX-Nummern der Anim-Kommandos zeigen muessen - und die Kommandos
    // stecken im Modellpaket, das gleich eingebaut wird.
    if (tonDatei)
    {
        std::vector<uint8_t> ton;
        FILE* f = std::fopen(tonDatei, "rb");
        if (!f) { std::printf("Tonpaket nicht lesbar: %s\n", tonDatei); return 2; }
        std::fseek(f, 0, SEEK_END); const long n = std::ftell(f); std::fseek(f, 0, SEEK_SET);
        ton.resize(static_cast<size_t>(n));
        const size_t g = std::fread(ton.data(), 1, ton.size(), f);
        std::fclose(f);
        if (g != ton.size()) { std::printf("Tonpaket unvollstaendig\n"); return 2; }

        const int vorherSamples = lvl.soundSampleCount();
        std::vector<std::pair<int,int>> mapping;

        // 446..449: in allen 16 Leveln frei. Die Soundmap ist ein festes Feld
        // von 450 Plaetzen, ein hoher Platz kostet also kein Byte.
        // Lautstaerke je Klang in Paketreihenfolge (84 Angriff, 87 Bruellen,
        // 88 Fussstampfen, 89 Schnuppern, 255 Raptor-Todesschrei). Das
        // Bruellen soll rund 30 Prozent lauter sein - im Spiel gehoert,
        // Wunsch des Nutzers.
        // Alle im Spiel abgehoert und vom Nutzer eingestellt. ACHTUNG: der
        // Grundwert ist 191 und das Feld ein Byte - alles ueber 133 Prozent
        // landet an der Obergrenze 255. Bruellen und Sterben liegen dort,
        // ihre Prozentwerte sind also der Wunsch, nicht das Ergebnis.
        std::vector<int> volumes = { 100, 130, 150, 100, 140 };

        // -muted <id,...>: diese Nummern bewusst auf einen LEEREN Platz der
        // Soundmap biegen, statt sie stehen zu lassen.
        //
        // 411 ist in allen 16 TR5-Leveln unbelegt - mit MissMod -ton an jedem
        // einzelnen nachgemessen, nicht angenommen. Der erste Kandidat 405 war
        // in rich3 belegt; frei sind 411 bis 419. Der Platz liegt ausserhalb
        // der Bereiche, die wir fuer Importe vergeben (420-449).
        std::vector<int> muted;
        int muteSlot = 411;

        // 445..449: fuenf Plaetze am oberen Ende, in allen Leveln frei.
        int basis = 445;

        // Ab dem Raptor (#97) sind Basisplatz und Lautstaerken einstellbar -
        // vorher standen die T-Rex-Values fest im Werkzeug. Zwei Gegner teilen
        // sich den oberen Rand der Soundmap nicht mehr von allein:
        //   -basis N        erster Zielplatz
        //   -volumes a,b,c,..  Prozent je Klang in Paketreihenfolge
        for (int a = 5; a < argc; ++a)
        {
            if (std::strcmp(argv[a], "-muted") == 0 && a + 1 < argc)
            {
                const char* p = argv[++a];
                while (*p)
                {
                    muted.push_back(std::atoi(p));
                    while (*p && *p != ',') ++p;
                    if (*p == ',') ++p;
                }
            }
            else if (std::strcmp(argv[a], "-stummplatz") == 0 && a + 1 < argc)
                muteSlot = std::atoi(argv[++a]);
            else if (std::strcmp(argv[a], "-basis") == 0 && a + 1 < argc)
                basis = std::atoi(argv[++a]);
            else if (std::strcmp(argv[a], "-volumes") == 0 && a + 1 < argc)
            {
                volumes.clear();
                const char* p = argv[++a];
                while (*p)
                {
                    volumes.push_back(std::atoi(p));
                    while (*p && *p != ',') ++p;
                    if (*p == ',') ++p;
                }
            }
        }
        std::printf("  Zielplaetze ab %d, %d Lautstaerkeangaben\n",
                    basis, static_cast<int>(volumes.size()));

        if (!lvl.addSoundsFromTr3(ton, basis, mapping, err, &volumes))
        { std::printf("SOUNDS GESCHEITERT: %s\n", err.c_str()); return 1; }

        std::printf("  Sounds: %d -> %d Samples\n", vorherSamples, lvl.soundSampleCount());
        for (const auto& z : mapping)
            std::printf("    TR3-Sound %3d  ->  Map %3d\n", z.first, z.second);

        std::vector<int> unresolved;
        std::vector<std::pair<int,int>> genutzt;
        std::vector<std::pair<int,int>> arten;
        const int n2 = remapSfx(package, mapping, unresolved, genutzt, arten);

        // Je ID zeigen, wie oft und aus welcher Animation sie gerufen wird.
        // Eine blosse Gesamtzahl reicht nicht: beim Compy stimmte die Summe,
        // und trotzdem war unresolved, ob der Laufklang ueberhaupt vorkommt.
        for (const auto& z : mapping)
        {
            std::string wo;
            int mal = 0;
            for (const auto& g : genutzt)
                if (g.first == z.first)
                {
                    ++mal;
                    char puf[16];
                    std::snprintf(puf, sizeof(puf), "%s%d", wo.empty() ? "" : ",", g.second);
                    wo += puf;
                }
            std::printf("    ID %3d -> Map %3d : %dx%s%s\n", z.first, z.second, mal,
                        mal ? ", Anim " : " - WIRD NIE GERUFEN", wo.c_str());
        }

        // Alle Kommandoarten auflisten. PlaySound ist nicht die einzige, die
        // im Ziel hoerbar wird: ein Flipeffect traegt eine Nummer, die in TR3
        // und TR5 verschiedene Wirkungen hat, und wird NICHT umgebogen.
        {
            const char* name[8] = { "?", "SetPosition", "JumpVelocity", "EmptyHands",
                                    "KillItem", "PlaySound", "Flipeffect", "?" };
            for (int k = 1; k <= 6; ++k)
            {
                std::string wo;
                int mal = 0;
                for (const auto& s : arten)
                    if (s.first == k)
                    {
                        ++mal;
                        char puf[16];
                        std::snprintf(puf, sizeof(puf), "%s%d", wo.empty() ? "" : ",", s.second);
                        wo += puf;
                    }
                if (mal)
                    std::printf("    Kommando %d (%s): %dx, Anim %s\n", k, name[k], mal, wo.c_str());
            }
        }
        if (n2 < 0) { std::printf("Kommandoblock im Modellpaket nicht auffindbar\n"); return 1; }
        std::printf("  Anim-Kommandos: %d SFX-Nummern umgebogen\n", n2);
        // HART ABBRECHEN, nicht nur warnen.
        //
        // Eine stehen gebliebene TR3-Nummer trifft im Ziel irgendeinen
        // Vanilla-Klang. Das ist kein Schoenheitsfehler: beim Compsognathus
        // blieb 243 stehen und wurde zu SFX_GENERIC_HEAVY_FEET - ein wuchtiger
        // schwerer Schritt mitten zwischen den richtigen Klaengen. Die Suche
        // danach hat Stunden gekostet, weil alle Messungen an den Dateien
        // sauber waren und nur eine WARNUNG im Bauprotokoll stand, die man
        // ueberliest.
        //
        // Wo die Zielplaetze liegen, hilft dagegen nicht: sie liegen ohnehin
        // hoch (420-449) und koennen mit TR3-Nummern (bis etwa 256) nicht
        // kollidieren. Der Schutz muss an der Quelle greifen - jede gerufene
        // Nummer MUSS eine Zuordnung haben.
        //
        // Wer den Klang bewusst nicht mitnehmen will, biegt ihn auf einen
        // eigenen stillen Platz. Einfach stehenlassen ist keine Option mehr.
        // Bewusst stummgeschaltete Nummern: sie werden auf einen LEEREN Platz
        // der Soundmap gebogen. Die Engine steigt dort aus (sound.cpp: lut ==
        // -1 -> "Non present sample") und spielt nichts. Das kostet kein
        // Sample und trifft garantiert nichts Fremdes.
        //
        // Gebraucht fuer Klaenge, die wir aus dem Quellspiel nicht mitnehmen
        // wollen: TR3 143 "Falling" bei T-Rex und Raptor, TR3 56 (Laras
        // Schritt) beim Geier. Stehenlassen ist keine Option - 143 und 56 sind
        // in TR5 belegt (SFX_GENERIC_BODY_FALL, SFX_LARA_GRABFEET).
        if (!unresolved.empty() && !muted.empty())
        {
            std::vector<std::pair<int,int>> aufStumm;
            for (int o : unresolved)
                for (int s : muted)
                    if (o == s) aufStumm.push_back(std::make_pair(o, muteSlot));

            if (!aufStumm.empty())
            {
                std::vector<int> rest;
                std::vector<std::pair<int,int>> egal;
                remapSfx(package, aufStumm, rest, egal, egal);
                std::printf("  Stummgeschaltet auf leeren Platz %d:", muteSlot);
                for (const auto& z : aufStumm) std::printf(" %d", z.first);
                std::printf("\n");

                std::vector<int> uebrig;
                for (int o : unresolved)
                {
                    bool weg = false;
                    for (const auto& z : aufStumm) if (z.first == o) weg = true;
                    if (!weg) uebrig.push_back(o);
                }
                unresolved = uebrig;
            }
        }

        if (!unresolved.empty())
        {
            std::printf("\nABBRUCH: %zu SFX-Nummern OHNE Zuordnung. Sie wuerden ihre\n"
                        "TR3-Nummer behalten und im Ziel einen fremden Klang treffen:\n   ",
                        unresolved.size());
            for (int o : unresolved) std::printf("%d ", o);
            std::printf("\n\nNimm sie ins Klangpaket auf (TR3Ton -waves) oder biege sie\n"
                        "bewusst auf einen stillen Platz. Es wurde NICHTS geschrieben.\n");
            return 1;
        }
    }

    // Texturen VOR dem Modell: erst danach steht fest, auf welche Texinfos
    // die Faces zeigen muessen - und die Faces stecken im Modellpaket.
    // Dieselbe Reihenfolge wie bei den Sounds.
    int texBasis = tex;
    if (paketVersion(package) >= 2)
    {
        std::map<int,int> texMap;
        if (!lvl.addTexturesFromTr3(package, texMap, err))
        { std::printf("TEXTUREN GESCHEITERT: %s\n", err.c_str()); return 1; }
        std::printf("  Texturen: %s\n", err.c_str());

        const int n = remapTextures(package, texMap);
        if (n < 0) { std::printf("Meshes im Paket nicht lesbar\n"); return 1; }
        std::printf("  Faces: %d Texturworte umgebogen\n", n);
        texBasis = -1;                 // Faces stehen jetzt richtig, nicht ueberschreiben
    }

    if (!lvl.addMoveableFromTr3(package, slot, texBasis, err))
    { std::printf("EINBAU GESCHEITERT: %s\n", err.c_str()); return 1; }

    Tr5Level::MoveableInfo mi;
    if (!lvl.moveableInfo(slot, mi)) { std::printf("Slot %d nach dem Einbau nicht auffindbar\n", slot); return 1; }
    std::printf("  Slot %d: %d Meshes, meshIdx %d, bone %d, frameOffset %u, anim %d\n",
                slot, mi.nmeshes, mi.meshIndex, mi.boneIndex,
                static_cast<unsigned>(mi.frameOffset), mi.animIndex);

    // Gegenprobe: laufen die eingebauten Meshes durch den Ableser der Engine?
    int schlecht = 0;
    for (int i = 0; i < mi.nmeshes; ++i)
    {
        int nv, nn, q, t, bytes;
        if (!lvl.debugMeshWalk(mi.meshIndex + i, nv, nn, q, t, bytes)) ++schlecht;
    }
    std::printf("  Meshpruefung: %d von %d nicht lesbar\n", schlecht, mi.nmeshes);

    if (!lvl.save(toW(argv[4]), err)) { std::printf("Speichern: %s\n", err.c_str()); return 1; }
    std::printf("  Geschrieben: %s\n", argv[4]);
    return schlecht ? 1 : 0;
}
