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
#include "Tr5Level.h"

#include <cstdio>
#include <cstring>

namespace
{
    // Alle Mehrbytewerte im TR-Format sind Little Endian, genau wie x86 - ein
    // memcpy genuegt also. Trotzdem ueber Hilfsfunktionen, damit jede Lesestelle
    // die Bereichspruefung mitbekommt: eine beschaedigte Groessenangabe im
    // Kopfteil darf uns nicht ueber das Dateiende hinauslaufen lassen.
    bool readI32(const std::vector<uint8_t>& b, size_t& pos, int32_t& out)
    {
        if (pos + 4 > b.size()) return false;
        std::memcpy(&out, b.data() + pos, 4);
        pos += 4;
        return true;
    }

    bool readI16(const std::vector<uint8_t>& b, size_t& pos, int16_t& out)
    {
        if (pos + 2 > b.size()) return false;
        std::memcpy(&out, b.data() + pos, 2);
        pos += 2;
        return true;
    }

    bool skip(const std::vector<uint8_t>& b, size_t& pos, int64_t n)
    {
        if (n < 0) return false;
        if (pos + static_cast<size_t>(n) > b.size()) return false;
        pos += static_cast<size_t>(n);
        return true;
    }
}

bool Tr5Level::load(const std::wstring& path, std::string& error)
{
    m_bytes.clear();
    m_mainOffset = m_mainSize = 0;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f)
    {
        error = "Leveldatei laesst sich nicht oeffnen";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); error = "Leveldatei ist leer"; return false; }

    m_bytes.resize(static_cast<size_t>(n));
    size_t got = std::fread(m_bytes.data(), 1, m_bytes.size(), f);
    std::fclose(f);
    if (got != m_bytes.size()) { error = "Leveldatei nicht vollstaendig lesbar"; return false; }

    if (!readHeader(error)) return false;
    return findItems(error);
}

//----------------------------------------------------------------------------
// Laeuft den Hauptblock von vorn ab, bis die Item-Anzahl erreicht ist.
//
// Es gibt keinen Index und keine Marke, an der die Items stehen - man muss
// jeden vorangehenden Abschnitt exakt so ueberspringen, wie die Engine ihn
// liest. Ein einziges falsches Byte verschiebt alles Weitere, deshalb ist die
// Reihenfolge unten 1:1 aus LoadRooms/LoadObjects/LoadSprites/LoadCameras/
// LoadSoundEffects/LoadBoxes/LoadAnimatedTextures/LoadTextureInfos in
// specific/file.cpp uebernommen, und die Strukturgroessen stammen aus einem
// sizeof()-Test gegen die Header der Engine, nicht aus einer Schaetzung.
//----------------------------------------------------------------------------
namespace
{
    // Groessen, vom Compiler gegen global/types.h ermittelt.
    constexpr int SZ_ANIM = 40;   // ANIM_STRUCT
    constexpr int SZ_CHANGE = 6;    // CHANGE_STRUCT
    constexpr int SZ_RANGE = 8;    // RANGE_STRUCT
    constexpr int SZ_SPRITE = 16;   // PHDSPRITESTRUCT
    constexpr int SZ_OBJVEC = 16;   // OBJECT_VECTOR
    constexpr int SZ_SPOTCAM = 40;   // SPOTCAM
    constexpr int SZ_BOX = 8;    // BOX_INFO
    constexpr int SZ_TEXINFO = 40;   // PHDTEXTURESTRUCT

    // Je Eintrag gelesene Bytes in den Schleifen, die Feld fuer Feld lesen.
    constexpr int SZ_OBJECTINFO = 20;   // slot4 + nmeshes2 + meshidx2 + bone4 + frame4 + anim2 + pad2
    constexpr int SZ_STATICINFO = 32;   // slot4 + mesh2 + 12 + 12 + flags2
    constexpr int SZ_SPRITESLOT = 8;    // slot4 + 2 shorts (beide Zweige gleich lang)
}

bool Tr5Level::findItems(std::string& error)
{
    size_t p = m_mainOffset;
    const size_t end = m_mainOffset + m_mainSize;

    auto i32 = [&](int32_t& out) -> bool {
        if (p + 4 > end) return false;
        std::memcpy(&out, m_bytes.data() + p, 4);
        p += 4;
        return true;
    };
    // Ueberspringen mit Bereichspruefung. count kommt aus der Datei und darf
    // niemals ungeprueft als Laenge benutzt werden - eine beschaedigte oder
    // absichtlich manipulierte Zahl wuerde sonst weit hinter das Dateiende
    // zeigen.
    auto skipN = [&](int32_t count, int elemSize) -> bool {
        if (count < 0) return false;
        const uint64_t n = static_cast<uint64_t>(count) * static_cast<uint64_t>(elemSize);
        if (p + n > end) return false;
        p += static_cast<size_t>(n);
        return true;
    };

    int32_t n = 0;
    const char* where = "Anfang";

#define STEP(what, expr) do { where = what; if (!(expr)) goto broken; } while (0)

    STEP("unbenutztes Feld", skipN(1, 4));
    STEP("Raumanzahl", i32(n));
    m_roomCount = n;
    m_rooms.clear();
    m_roomMismatch = 0;
    for (int i = 0; i < m_roomCount; ++i)
    {
        int32_t roomSize = 0;
        STEP("Raum-Marke", skipN(1, 4));            // "XELA"
        STEP("Raumgroesse", i32(roomSize));
        if (roomSize < 0 || p + static_cast<size_t>(roomSize) > end) { where = "Raumdaten"; goto broken; }
        parseRoom(p, static_cast<size_t>(roomSize), i);
        p += static_cast<size_t>(roomSize);
    }
    m_floorCountOffset = p;
    STEP("Floordata", i32(n));
    m_floorCount = n;
    m_floorAt = p;
    STEP("Floordata", skipN(n, 2));

    // Blockkarte fuer den Modelltransfer nebenbei mitschreiben. merk() haelt
    // fest, wo die Anzahl steht und wo die Daten beginnen - NACHDEM i32 die
    // Anzahl gelesen hat, zeigt p genau auf das erste Datenbyte.
    auto merk = [&](Block& b, int32_t anzahl) { b.countAt = p - 4; b.dataAt = p; b.count = anzahl; };

    STEP("Mesh-Daten", i32(n));   merk(m_meshData, n);  STEP("Mesh-Daten", skipN(n, 2));
    STEP("Mesh-Zeiger", i32(n));  merk(m_meshPtrs, n);  STEP("Mesh-Zeiger", skipN(n, 4));
    STEP("Animationen", i32(n));  merk(m_anims, n);     STEP("Animationen", skipN(n, SZ_ANIM));
    STEP("Zustandswechsel", i32(n)); merk(m_changes, n); STEP("Zustandswechsel", skipN(n, SZ_CHANGE));
    STEP("Bereiche", i32(n));     merk(m_ranges, n);    STEP("Bereiche", skipN(n, SZ_RANGE));
    STEP("Anim-Befehle", i32(n)); merk(m_commands, n);  STEP("Anim-Befehle", skipN(n, 2));
    STEP("Knochen", i32(n));      merk(m_bones, n);     STEP("Knochen", skipN(n, 4));
    STEP("Frames", i32(n));       merk(m_frames, n);    STEP("Frames", skipN(n, 2));
    STEP("Objekte", i32(n));      merk(m_objects, n);   STEP("Objekte", skipN(n, SZ_OBJECTINFO));
    STEP("Statics", i32(n));      STEP("Statics", skipN(n, SZ_STATICINFO));

    STEP("SPR-Marke", skipN(1, 4));
    STEP("Sprites", i32(n));      merk(m_sprites, n);   STEP("Sprites", skipN(n, SZ_SPRITE));
    STEP("Sprite-Slots", i32(n)); STEP("Sprite-Slots", skipN(n, SZ_SPRITESLOT));

    STEP("Kameras", i32(n));      STEP("Kameras", skipN(n, SZ_OBJVEC));
    STEP("Spotcams", i32(n));     STEP("Spotcams", skipN(n, SZ_SPOTCAM));
    STEP("Soundquellen", i32(n)); STEP("Soundquellen", skipN(n, SZ_OBJVEC));

    {
        int32_t numBoxes = 0;
        STEP("Boxen", i32(numBoxes));
        STEP("Boxen", skipN(numBoxes, SZ_BOX));
        STEP("Overlaps", i32(n)); STEP("Overlaps", skipN(n, 2));
        // 2 Durchlaeufe zu je 4 + 1 Zonen, alle numBoxes shorts lang.
        STEP("Zonen", skipN(numBoxes, 2 * 10));
    }

    STEP("Animierte Texturen", i32(n)); STEP("Animierte Texturen", skipN(n, 2));
    // Ein einzelnes Byte: nAnimUVRanges am Ende von LoadAnimatedTextures.
    // Leicht zu uebersehen, weil es als ReadChar zwischen lauter Bloecken
    // steht - und genau dieses eine Byte hat beim ersten Versuch alles
    // Nachfolgende um eine Stelle verschoben.
    STEP("Anim-UV-Anzahl", skipN(1, 1));
    STEP("TEX-Marke", skipN(1, 4));
    STEP("Texturinfos", i32(n)); merk(m_texinfos, n); STEP("Texturinfos", skipN(n, SZ_TEXINFO));

    // Angekommen: hier steht die Item-Anzahl.
    m_itemCountOffset = p;
    STEP("Item-Anzahl", i32(m_itemCount));
    STEP("Item-Daten", skipN(m_itemCount, 24));
    // Der Sound-Block schliesst sich an. Er darf fehlschlagen, ohne dass das
    // ganze Level als unlesbar gilt - gebraucht wird er nur, wenn Samples
    // uebernommen werden sollen.
    findSound();
    return true;

#undef STEP

broken:
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "Hauptblock laesst sich nicht durchlaufen - gescheitert bei \"%s\" "
            "an Offset %zu. Passt die Datei zum erwarteten TR5-Format?",
            where, p);
        error = buf;
        m_itemCount = 0;
        m_itemCountOffset = 0;
        return false;
    }
}

//----------------------------------------------------------------------------
// Aufbau eines Raum-Blobs, aus den Kopf-Offsets abgeleitet:
//
//   [0 .. 207]                 tr5_room_header, feste 208 Byte
//   208 + start_sd             Sektorfeld, numX * numZ * 8 Byte
//   danach                     uint16 Portalanzahl, dann je 32 Byte Portal
//   danach                     uint16 Statics-Anzahl
//   208 + end_portal           Statics-Feld
//
// Die Portalliste hat keinen eigenen Offset im Kopf - sie liegt zwischen
// Sektoren und Statics. Daraus folgt eine Gleichung, die aufgehen MUSS:
//
//   208 + end_portal == Sektorende + 2 + Portalanzahl * 32 + 2
//
// Das letzte "+ 2" ist die Statics-Anzahl, die end_portal bereits
// ueberspringt - sie steht zusaetzlich im Kopf bei +50. Ohne dieses eine Feld
// ging die Rechnung um genau zwei Byte daneben, und zwar in JEDEM Raum; erst
// an einem echten Blob nachgemessen wurde es sichtbar. An Andrea1 mit allen
// 242 Raeumen bestaetigt.
//
// Genau die wird hier geprueft. Geht sie nicht auf, ist die Annahme ueber den
// Aufbau falsch, und dann sollen die Portale lieber leer bleiben als falsch
// sein - ein erfundener Nachbar wuerde die Suche nach einem abgeschiedenen
// Raum genau dort in die Irre fuehren, wo sie stimmen muss.
//----------------------------------------------------------------------------
void Tr5Level::parseRoom(size_t offset, size_t size, int index)
{
    constexpr size_t HDR = 208;
    constexpr size_t OFS_START_SD = 8, OFS_END_PORTAL = 16;
    constexpr size_t OFS_X = 20, OFS_Z = 28, OFS_YBOTTOM = 32;
    constexpr size_t OFS_NUM_Z = 40, OFS_NUM_X = 42;
    constexpr size_t OFS_ALT = 76, OFS_FLAGS = 78;
    constexpr size_t SECTOR = 8, PORTAL = 32;

    Tr5Room r;
    r.index = index;
    if (size < HDR) { m_rooms.push_back(r); ++m_roomMismatch; return; }

    const uint8_t* b = m_bytes.data() + offset;
    auto u16 = [&](size_t o) { uint16_t v; std::memcpy(&v, b + o, 2); return v; };
    auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; };
    auto i32r = [&](size_t o) { int32_t v; std::memcpy(&v, b + o, 4); return v; };

    r.originX = i32r(OFS_X);
    r.originZ = i32r(OFS_Z);
    r.yBottom = i32r(OFS_YBOTTOM);
    r.numZSectors = u16(OFS_NUM_Z);
    r.numXSectors = u16(OFS_NUM_X);
    const uint16_t alt = u16(OFS_ALT);
    r.altRoom = (alt == 0xFFFF) ? -1 : static_cast<int>(alt);
    r.flags = u16(OFS_FLAGS);

    const size_t sectorsAt = HDR + u32(OFS_START_SD);
    const size_t sectorsEnd = sectorsAt + static_cast<size_t>(r.numXSectors) * r.numZSectors * SECTOR;
    const size_t staticsAt = HDR + u32(OFS_END_PORTAL);

    if (sectorsEnd + 2 > size || staticsAt > size || staticsAt < sectorsEnd + 2)
    {
        m_rooms.push_back(r);
        ++m_roomMismatch;
        return;
    }

    const uint16_t numPortals = u16(sectorsEnd);
    if (sectorsEnd + 2 + static_cast<size_t>(numPortals) * PORTAL + 2 != staticsAt)
    {
        m_rooms.push_back(r);
        ++m_roomMismatch;
        return;
    }

    r.portalRooms.reserve(numPortals);
    for (uint16_t i = 0; i < numPortals; ++i)
        r.portalRooms.push_back(static_cast<int>(u16(sectorsEnd + 2 + static_cast<size_t>(i) * PORTAL)));

    // Erst hier setzen: bis zu diesem Punkt kann der Raum noch als nicht
    // aufgegangen abgebrochen werden, und ein Offset auf einen unklaren Raum
    // waere schlimmer als gar keiner.
    r.sectorsOffset = offset + sectorsAt;

    m_rooms.push_back(r);
}

bool Tr5Level::getItem(int index, Tr5Item& out) const
{
    if (index < 0 || index >= m_itemCount) return false;
    const uint8_t* p = m_bytes.data() + m_itemCountOffset + 4 + static_cast<size_t>(index) * 24;
    std::memcpy(&out.objectNumber, p + 0, 2);
    std::memcpy(&out.roomNumber, p + 2, 2);
    std::memcpy(&out.x, p + 4, 4);
    std::memcpy(&out.y, p + 8, 4);
    std::memcpy(&out.z, p + 12, 4);
    std::memcpy(&out.yRot, p + 16, 2);
    std::memcpy(&out.shade, p + 18, 2);
    std::memcpy(&out.triggerFlags, p + 20, 2);
    std::memcpy(&out.flags, p + 22, 2);
    return true;
}

bool Tr5Level::addItem(const Tr5Item& in, std::string& error)
{
    if (m_itemCount >= MAX_ITEMS && !m_allowOverLimit)
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
            "Level hat bereits %d Items - die Engine laedt hoechstens %d.",
            m_itemCount, MAX_ITEMS);
        error = buf;
        return false;
    }

    // 24 Byte hinter dem letzten Item einfuegen. Alles danach im Hauptblock
    // (AI-Infos, Cutscene-Daten) rueckt mit; das ist unkritisch, weil dort
    // alles sequentiell gelesen wird und niemand absolute Offsets haelt.
    const size_t itemsEnd = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (itemsEnd > m_bytes.size()) { error = "Item-Bereich liegt ausserhalb der Datei"; return false; }

    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(itemsEnd), 24, 0);
    // Hinter den Items liegt der Sound-Block - seine Offsets wandern mit.
    verschiebeSound(24, itemsEnd);

    ++m_itemCount;
    std::memcpy(m_bytes.data() + m_itemCountOffset, &m_itemCount, 4);

    // Der Hauptblock ist um 24 Byte gewachsen. Seine Groesse steht doppelt im
    // Kopf davor - als size und als compressedSize, die bei einem
    // unkomprimierten Block gleich sind. Beide muessen mit, sonst liest die
    // Engine den Block zu kurz und alles Weitere kippt.
    m_mainSize += 24;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    return setItem(m_itemCount - 1, in);
}

bool Tr5Level::setItem(int index, const Tr5Item& in)
{
    if (index < 0 || index >= m_itemCount) return false;
    uint8_t* p = m_bytes.data() + m_itemCountOffset + 4 + static_cast<size_t>(index) * 24;
    std::memcpy(p + 0, &in.objectNumber, 2);
    std::memcpy(p + 2, &in.roomNumber, 2);
    std::memcpy(p + 4, &in.x, 4);
    std::memcpy(p + 8, &in.y, 4);
    std::memcpy(p + 12, &in.z, 4);
    std::memcpy(p + 16, &in.yRot, 2);
    std::memcpy(p + 18, &in.shade, 2);
    std::memcpy(p + 20, &in.triggerFlags, 2);
    std::memcpy(p + 22, &in.flags, 2);
    return true;
}

//----------------------------------------------------------------------------
// Floordata
//
// Das Format ist vollstaendig in "Tomb5 Rando vorbereitung\Trigger\Floordata
// und Trigger.md" hergeleitet, mit Zeilenangaben in die Engine. Hier nur, was
// zum Mitlesen des Codes noetig ist.
//----------------------------------------------------------------------------
namespace
{
    // global/types.h, enum floor_types
    enum { FN_DOOR = 1, FN_TILT = 2, FN_ROOF = 3, FN_TRIGGER = 4,
           FN_LAVA = 5, FN_CLIMB = 6, FN_MONKEY = 19, FN_TRIGTRIGGER = 20 };

    // SWITCH (2), KEY (3) und PICKUP (4) schieben ein Referenzwort vor die
    // Befehlsliste: das Item, das bedient bzw. aufgenommen werden muss. Alle
    // drei liest die Engine gleich - game/control.cpp:2251, :2266, :2274, jedes
    // Mal "value = *(data++) & 0x3FF". Das Wort ist KEIN Befehl.
    bool hasRefWord(int art) { return art == 2 || art == 3 || art == 4; }

    // Zusatzwoerter eines Eintrags, der kein Trigger ist: genau die Faelle, in
    // denen GetHeight (control.cpp:977ff) ein "data++" macht.
    int extraWords(int fn)
    {
        return (fn == FN_DOOR || fn == FN_TILT || fn == FN_ROOF || (fn >= 7 && fn <= 18)) ? 1 : 0;
    }

    // Befehlsart: 4 Bit, nicht 5. Zweimal unabhaengig belegt - "switch
    // ((trigger & 0x3FFF) >> 10)" in _TestTriggers (control.cpp:2350) und
    // "(trigger & 0x3C00) != (TO_OBJECT << 10)" in GetHeight (:1025). Bit 15
    // ist das Endebit, Bit 14 wertet die Engine gar nicht aus.
    int cmdType(uint16_t c) { return (c & 0x3C00) >> 10; }
}

int Tr5Level::sectorIndex(int room, int sx, int sz) const
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return -1;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (r.sectorsOffset == 0) return -1;
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return -1;

    // Das Sektorfeld ist x-major: x * numZSectors + z.
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    if (at + 2 > m_bytes.size()) return -1;
    uint16_t v;
    std::memcpy(&v, m_bytes.data() + at, 2);
    return static_cast<int>(v);
}

void Tr5Level::setSectorIndex(const Tr5Room& r, int sx, int sz, uint16_t index)
{
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    std::memcpy(m_bytes.data() + at, &index, 2);
}

bool Tr5Level::readChain(int index, std::vector<Tr5FloorEntry>& out) const
{
    out.clear();
    if (index <= 0) return index == 0;              // 0 = keine Floordata

    auto word = [&](size_t i) {
        uint16_t v;
        std::memcpy(&v, m_bytes.data() + m_floorAt + i * 2, 2);
        return v;
    };
    const size_t limit = static_cast<size_t>(m_floorCount);
    size_t i = static_cast<size_t>(index);

    for (;;)
    {
        if (i >= limit) return false;

        Tr5FloorEntry e;
        const uint16_t w = word(i++);
        e.funktion = w & 0x1F;
        e.art = (w >> 8) & 0x3F;
        e.woerter.push_back(w);
        const bool last = (w & 0x8000) != 0;

        if (e.funktion == FN_TRIGGER)
        {
            if (i >= limit) return false;
            e.woerter.push_back(word(i++));         // Timer, OneShot, Maske

            bool more = true;
            if (hasRefWord(e.art))
            {
                if (i >= limit) return false;
                const uint16_t r = word(i++);
                e.woerter.push_back(r);
                more = (r & 0x8000) == 0;
            }
            while (more)
            {
                if (i >= limit) return false;
                const uint16_t c = word(i++);
                e.woerter.push_back(c);
                const int akt = cmdType(c);
                if (akt == 1 /*Kamera*/ || akt == 12 /*Flyby*/)
                {
                    if (i >= limit) return false;
                    e.woerter.push_back(word(i++));
                }
                if (c & 0x8000) break;
            }
        }
        else
        {
            for (int k = 0; k < extraWords(e.funktion); ++k)
            {
                if (i >= limit) return false;
                e.woerter.push_back(word(i++));
            }
        }

        out.push_back(e);
        if (last) break;
    }
    return true;
}

const char* Tr5Level::canAppendTrigger(const std::vector<Tr5FloorEntry>& chain)
{
    // _TestTriggers bekommt von GetHeight einen Zeiger und ueberspringt ab dort
    // GENAU vier Eintragsarten - Lava, Climb, Monkey, TrigTrigger
    // (control.cpp:2151-2195) - und liest dann bedingungslos einen Triggerkopf
    // (:2197). GetHeight setzt den Zeiger auf den ERSTEN Eintrag aus {Trigger,
    // Climb, Monkey, TrigTrigger} (1014-1053), Lava sogar bedingungslos neu.
    //
    // Steht eines dieser fuenf schon in der Kette, zeigt der Zeiger dorthin -
    // und alles, was zwischen ihm und unserem angehaengten Trigger liegt, wird
    // als Triggerkopf fehlgedeutet. Portale und Neigungen allein sind harmlos:
    // die frisst GetHeight beim Laufen, ohne den Zeiger zu setzen.
    for (const Tr5FloorEntry& e : chain)
    {
        if (e.funktion == FN_TRIGGER)     return "Sektor hat schon einen Trigger";
        if (e.funktion == FN_LAVA)        return "Sektor hat einen Lava-Eintrag";
        if (e.funktion == FN_CLIMB)       return "Sektor hat einen Climb-Eintrag";
        if (e.funktion == FN_MONKEY)      return "Sektor hat einen Monkey-Eintrag";
        if (e.funktion == FN_TRIGTRIGGER) return "Sektor hat einen TrigTrigger-Eintrag";
    }
    return nullptr;
}

bool Tr5Level::appendChain(const std::vector<Tr5FloorEntry>& chain, uint16_t& outIndex,
                           std::string& error)
{
    if (chain.empty()) { outIndex = 0; return true; }   // Sektor ohne Floordata

    std::vector<uint16_t> words;
    for (size_t n = 0; n < chain.size(); ++n)
    {
        std::vector<uint16_t> w = chain[n].woerter;
        if (w.empty()) { error = "leerer Floordata-Eintrag"; return false; }
        w[0] &= 0x7FFF;                              // Endebit erst mal weg
        if (n + 1 == chain.size()) w[0] |= 0x8000;   // und nur beim letzten setzen
        words.insert(words.end(), w.begin(), w.end());
    }

    // FLOOR_INFO.index ist ein ushort (global/types.h). Was jenseits von 65535
    // liegt, kann kein Sektor mehr adressieren.
    const size_t newIndex = static_cast<size_t>(m_floorCount);
    if (newIndex + words.size() > 65535)
    {
        error = "Floordata wuerde 65535 Shorts ueberschreiten - FLOOR_INFO.index ist ushort";
        return false;
    }

    std::vector<uint8_t> raw;
    raw.reserve(words.size() * 2);
    for (uint16_t w : words) { raw.push_back(w & 0xFF); raw.push_back(w >> 8); }

    const size_t at = m_floorAt + static_cast<size_t>(m_floorCount) * 2;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(at), raw.begin(), raw.end());

    m_floorCount += static_cast<int32_t>(words.size());
    std::memcpy(m_bytes.data() + m_floorCountOffset, &m_floorCount, 4);

    // Der Hauptblock ist gewachsen; seine Groesse steht doppelt davor.
    m_mainSize += raw.size();
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

    // Und das hier ist der Fallstrick: die Items liegen HINTER der Floordata.
    // Wer den Block wachsen laesst, ohne diesen Offset nachzuziehen, schreibt
    // beim naechsten setItem/addItem mitten in fremde Daten. Die Sektorfelder
    // der Raeume liegen davor und bleiben, wo sie sind.
    m_itemCountOffset += raw.size();
    verschiebeSound(raw.size(), at);      // der Sound-Block liegt noch weiter hinten

    outIndex = static_cast<uint16_t>(newIndex);
    return true;
}

bool Tr5Level::moveTrigger(int fromRoom, int fromSx, int fromSz,
                           int toRoom, int toSx, int toSz, std::string& error)
{
    char buf[256];

    if (fromRoom == toRoom && fromSx == toSx && fromSz == toSz)
    { error = "Quelle und Ziel sind derselbe Sektor"; return false; }

    const int fromIdx = sectorIndex(fromRoom, fromSx, fromSz);
    const int toIdx = sectorIndex(toRoom, toSx, toSz);
    if (fromIdx < 0) { error = "Quellsektor gibt es nicht"; return false; }
    if (toIdx < 0) { error = "Zielsektor gibt es nicht"; return false; }

    std::vector<Tr5FloorEntry> from, to;
    if (!readChain(fromIdx, from)) { error = "Quellkette laeuft aus dem Block heraus"; return false; }
    if (!readChain(toIdx, to)) { error = "Zielkette laeuft aus dem Block heraus"; return false; }

    // Genau einen Triggereintrag erwarten. Zwei waeren mehrdeutig, keiner ein
    // Bedienfehler - beides lieber abbrechen als raten.
    size_t found = 0, at = 0;
    for (size_t k = 0; k < from.size(); ++k)
        if (from[k].funktion == FN_TRIGGER) { ++found; at = k; }
    if (found != 1)
    {
        std::snprintf(buf, sizeof(buf),
            "Quellsektor %d,%d,%d hat %zu Triggereintraege, erwartet genau einen",
            fromRoom, fromSx, fromSz, found);
        error = buf;
        return false;
    }

    if (const char* why = canAppendTrigger(to))
    {
        std::snprintf(buf, sizeof(buf), "Zielsektor %d,%d,%d ungeeignet: %s",
                      toRoom, toSx, toSz, why);
        error = buf;
        return false;
    }

    const Tr5FloorEntry trig = from[at];
    from.erase(from.begin() + static_cast<ptrdiff_t>(at));
    to.push_back(trig);

    // Reihenfolge mit Absicht: erst beide Ketten anhaengen, dann beide Sektoren
    // umhaengen. Sonst zeigte ein Sektor zwischenzeitlich auf eine Kette, die
    // noch nicht vollstaendig geschrieben ist.
    uint16_t newFrom = 0, newTo = 0;
    if (!appendChain(from, newFrom, error)) return false;
    if (!appendChain(to, newTo, error)) return false;

    setSectorIndex(m_rooms[static_cast<size_t>(fromRoom)], fromSx, fromSz, newFrom);
    setSectorIndex(m_rooms[static_cast<size_t>(toRoom)], toSx, toSz, newTo);
    return true;
}

bool Tr5Level::addTrigger(int room, int sx, int sz, int art, int timer, bool oneShot,
                          int mask, const std::vector<Tr5TriggerCmd>& cmds,
                          std::string& error)
{
    char buf[256];

    if (cmds.empty()) { error = "Trigger ohne Befehle waere wirkungslos"; return false; }

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0)
    {
        std::snprintf(buf, sizeof(buf), "Sektor %d,%d,%d gibt es nicht", room, sx, sz);
        error = buf;
        return false;
    }

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) { error = "Kette laeuft aus dem Block heraus"; return false; }

    if (const char* why = canAppendTrigger(chain))
    {
        std::snprintf(buf, sizeof(buf), "Sektor %d,%d,%d ungeeignet: %s", room, sx, sz, why);
        error = buf;
        return false;
    }

    Tr5FloorEntry e;
    e.funktion = FN_TRIGGER;
    e.art = art;
    // Wort 0: Funktion in Bit 0-4, Art in Bit 8-13. Bit 15 vergibt baueKette.
    e.woerter.push_back(static_cast<uint16_t>((art << 8) | FN_TRIGGER));
    // Wort 1: Timer, OneShot, Codebits.
    e.woerter.push_back(static_cast<uint16_t>((timer & 0xFF) |
                                              (oneShot ? 0x100 : 0) |
                                              ((mask & 0x1F) << 9)));

    // SWITCH, KEY und PICKUP braeuchten hier ein Referenzwort. Das waere kein
    // Trigger, den man "hinzufuegt", sondern einer, der an ein vorhandenes
    // Objekt gebunden ist - deshalb bewusst nicht unterstuetzt statt geraten.
    if (hasRefWord(art))
    {
        error = "Triggerart braucht ein Referenzwort - so nicht anlegbar";
        return false;
    }

    for (size_t i = 0; i < cmds.size(); ++i)
    {
        uint16_t c = static_cast<uint16_t>(((cmds[i].type & 0xF) << 10) | (cmds[i].param & 0x3FF));
        if (i + 1 == cmds.size()) c |= 0x8000;       // Endebit auf den letzten
        e.woerter.push_back(c);

        if (cmds[i].type == 1 || cmds[i].type == 12)
        {
            error = "Kamera- und Flyby-Befehle brauchen ein Zusatzwort - hier nicht vorgesehen";
            return false;
        }
    }

    chain.push_back(e);

    uint16_t neu = 0;
    if (!appendChain(chain, neu, error)) return false;
    setSectorIndex(m_rooms[static_cast<size_t>(room)], sx, sz, neu);
    return true;
}

//----------------------------------------------------------------------------
// Sound
//
// Aufbau, abgelesen an TOMB5s LoadSamples (specific/file.cpp:1232):
//
//   Im Hauptblock, direkt hinter den Items:
//     long  nAIObjs;  AIOBJECT aiObjs[nAIObjs];    (24 Byte je Eintrag)
//     short cinematic;
//     short sample_lut[450];      Map -> Index ins sample_infos, -1 = fehlt
//     long  num_infos;
//     SAMPLE_INFO sample_infos[num_infos];         (8 Byte: short number,
//                                                   uchar volume, char radius,
//                                                   randomness, pitch, short flags)
//     long  num_samples;
//     ... danach eine Indextabelle, die die Engine NIE liest ...
//
//   Hinter dem Hauptblock, bis Dateiende:
//     long  num_samples;          dasselbe noch einmal
//     je Sample: long uncompSize, long compSize, compSize Bytes
//
// Dass num_samples zweimal dasteht, ist kein Lesefehler: LoadSamples holt es
// erst mit ReadLong() aus dem gepufferten Hauptblock (:1252) und gleich darauf
// noch einmal per fread() direkt aus der Datei (:1261), womit der zweite Wert
// den ersten ueberschreibt. Wer nur einen der beiden nachzieht, baut eine
// Datei, die je nach Leser verschieden aussieht.
//----------------------------------------------------------------------------
namespace
{
    constexpr int SAMPLE_LUT_SIZE = 450;   // MAX_SAMPLES, global/types.h
    constexpr int SZ_SAMPLE_INFO = 8;
    constexpr int SZ_AIOBJECT = 24;

    // specific/dxsound.cpp schreibt DS_Buffers[num] ohne Bereichspruefung; die
    // Engine bricht darum ab 257 Samples mit einer Meldung ab (file.cpp:1282).
    constexpr int MAX_SAMPLES_PLAYABLE = 256;
}

bool Tr5Level::findSound()
{
    m_soundOk = false;
    if (m_itemCountOffset == 0) return false;

    size_t p = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (p + 4 > m_bytes.size()) return false;

    int32_t nAI = 0;
    std::memcpy(&nAI, m_bytes.data() + p, 4); p += 4;
    if (nAI < 0) return false;
    p += static_cast<size_t>(nAI) * SZ_AIOBJECT;
    p += 2;                                             // cinematic

    if (p + SAMPLE_LUT_SIZE * 2 + 4 > m_bytes.size()) return false;
    m_lutAt = p;
    m_numInfosAt = p + SAMPLE_LUT_SIZE * 2;
    std::memcpy(&m_nInfos, m_bytes.data() + m_numInfosAt, 4);
    if (m_nInfos <= 0) return false;

    m_infosAt = m_numInfosAt + 4;
    m_numSamplesInMainAt = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    if (m_numSamplesInMainAt + 4 > m_bytes.size()) return false;

    m_numSamplesAt = m_mainOffset + m_mainSize;
    if (m_numSamplesAt + 4 > m_bytes.size()) return false;
    std::memcpy(&m_nSamples, m_bytes.data() + m_numSamplesAt, 4);

    // Beide Fassungen muessen dasselbe sagen. Tun sie es nicht, sitzt einer der
    // Offsets falsch - dann lieber gar nichts anfassen.
    int32_t imBlock = 0;
    std::memcpy(&imBlock, m_bytes.data() + m_numSamplesInMainAt, 4);
    if (imBlock != m_nSamples) return false;

    m_soundOk = true;
    return true;
}

void Tr5Level::verschiebeSound(size_t bytes, size_t abOffset)
{
    if (!m_soundOk || bytes == 0) return;
    if (m_lutAt >= abOffset)              m_lutAt += bytes;
    if (m_numInfosAt >= abOffset)         m_numInfosAt += bytes;
    if (m_infosAt >= abOffset)            m_infosAt += bytes;
    if (m_numSamplesInMainAt >= abOffset) m_numSamplesInMainAt += bytes;
    if (m_numSamplesAt >= abOffset)       m_numSamplesAt += bytes;
}

int Tr5Level::soundInfoIndex(int map) const
{
    if (!m_soundOk || map < 0 || map >= SAMPLE_LUT_SIZE) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_lutAt + static_cast<size_t>(map) * 2, 2);
    return (v < 0 || v >= m_nInfos) ? -1 : static_cast<int>(v);
}

bool Tr5Level::copySoundFrom(const Tr5Level& src, int srcMap, int dstMap, std::string& error)
{
    char buf[256];

    if (!m_soundOk) { error = "Ziel-Level: Sound-Block nicht deutbar"; return false; }
    if (!src.m_soundOk) { error = "Quell-Level: Sound-Block nicht deutbar"; return false; }
    if (dstMap < 0 || dstMap >= SAMPLE_LUT_SIZE) { error = "Ziel-Map ausserhalb 0..449"; return false; }

    if (soundInfoIndex(dstMap) >= 0) return true;      // schon da, nichts zu tun

    const int erste = src.soundInfoIndex(srcMap);
    if (erste < 0)
    {
        std::snprintf(buf, sizeof(buf), "Quell-Level hat Map %d nicht", srcMap);
        error = buf;
        return false;
    }

    // Zufallsvarianten: N aufeinanderfolgende Infos, N in Bit 2-5 der Flags des
    // ersten - "(sample_infos[lut].flags >> 2) & 0xF", game/sound.cpp:70.
    // SAMPLE_INFO selbst steht in global/types.h:691.
    int16_t flags = 0;
    std::memcpy(&flags, src.m_bytes.data() + src.m_infosAt + static_cast<size_t>(erste) * SZ_SAMPLE_INFO + 6, 2);
    int varianten = (static_cast<int>(static_cast<uint16_t>(flags)) >> 2) & 0xF;
    if (varianten <= 0) varianten = 1;
    if (erste + varianten > src.m_nInfos) { error = "Variantenzahl passt nicht zur Quelle"; return false; }

    if (m_nSamples + varianten > MAX_SAMPLES_PLAYABLE)
    {
        std::snprintf(buf, sizeof(buf),
            "Level haette %d Samples - die Engine laedt hoechstens %d",
            m_nSamples + varianten, MAX_SAMPLES_PLAYABLE);
        error = buf;
        return false;
    }

    // Quellsamples heraussuchen, bevor irgendetwas geschrieben wird.
    std::vector<std::vector<uint8_t>> infos;
    std::vector<std::pair<int32_t, std::vector<uint8_t>>> samples;
    for (int k = 0; k < varianten; ++k)
    {
        const size_t o = src.m_infosAt + static_cast<size_t>(erste + k) * SZ_SAMPLE_INFO;
        int16_t nummer = 0;
        std::memcpy(&nummer, src.m_bytes.data() + o, 2);
        if (nummer < 0 || nummer >= src.m_nSamples) { error = "Info zeigt auf ein Sample, das es nicht gibt"; return false; }

        // Physisches Sample-Feld der Quelle ablaufen
        size_t q = src.m_numSamplesAt + 4;
        bool gefunden = false;
        for (int32_t i = 0; i < src.m_nSamples; ++i)
        {
            if (q + 8 > src.m_bytes.size()) break;
            int32_t uc = 0, cs = 0;
            std::memcpy(&uc, src.m_bytes.data() + q, 4);
            std::memcpy(&cs, src.m_bytes.data() + q + 4, 4);
            q += 8;
            if (cs < 0 || q + static_cast<size_t>(cs) > src.m_bytes.size()) break;
            if (i == nummer)
            {
                samples.push_back({ uc, std::vector<uint8_t>(src.m_bytes.begin() + q,
                                                             src.m_bytes.begin() + q + cs) });
                gefunden = true;
                break;
            }
            q += static_cast<size_t>(cs);
        }
        if (!gefunden) { error = "Sampledaten der Quelle nicht auffindbar"; return false; }

        std::vector<uint8_t> info(src.m_bytes.begin() + o, src.m_bytes.begin() + o + SZ_SAMPLE_INFO);
        const int16_t neuNummer = static_cast<int16_t>(m_nSamples + k);
        std::memcpy(info.data(), &neuNummer, 2);        // auf den neuen Platz zeigen
        infos.push_back(info);
    }

    // --- Infos einfuegen. Sie liegen IM Hauptblock, der waechst also. -----
    const size_t einfuegenAn = m_infosAt + static_cast<size_t>(m_nInfos) * SZ_SAMPLE_INFO;
    std::vector<uint8_t> roh;
    for (const std::vector<uint8_t>& i : infos) roh.insert(roh.end(), i.begin(), i.end());
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(einfuegenAn), roh.begin(), roh.end());

    const int16_t neueLut = static_cast<int16_t>(m_nInfos);
    std::memcpy(m_bytes.data() + m_lutAt + static_cast<size_t>(dstMap) * 2, &neueLut, 2);

    m_nInfos += varianten;
    std::memcpy(m_bytes.data() + m_numInfosAt, &m_nInfos, 4);

    // Alles hinter der Einfuegestelle ist gewandert.
    verschiebeSound(roh.size(), einfuegenAn);
    if (m_itemCountOffset >= einfuegenAn) m_itemCountOffset += roh.size();

    m_mainSize += roh.size();
    const int32_t neueGroesse = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &neueGroesse, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &neueGroesse, 4);

    m_nSamples += varianten;
    std::memcpy(m_bytes.data() + m_numSamplesInMainAt, &m_nSamples, 4);
    std::memcpy(m_bytes.data() + m_numSamplesAt, &m_nSamples, 4);

    // --- Sampledaten ans Dateiende ---------------------------------------
    for (const auto& s : samples)
    {
        const int32_t uc = s.first, cs = static_cast<int32_t>(s.second.size());
        const uint8_t* pu = reinterpret_cast<const uint8_t*>(&uc);
        const uint8_t* pc = reinterpret_cast<const uint8_t*>(&cs);
        m_bytes.insert(m_bytes.end(), pu, pu + 4);
        m_bytes.insert(m_bytes.end(), pc, pc + 4);
        m_bytes.insert(m_bytes.end(), s.second.begin(), s.second.end());
    }
    return true;
}

bool Tr5Level::reparse(std::string& error)
{
    // Nach einem Einfuegen sind ALLE gemerkten Offsets Vergangenheit. Statt
    // jeden einzeln nachzuziehen - und einen zu vergessen -, wird die Datei
    // schlicht neu gedeutet. Sie liegt im Speicher, das kostet nichts.
    if (!readHeader(error)) return false;
    return findItems(error);
}

bool Tr5Level::moveableInfo(int slotId, MoveableInfo& out) const
{
    for (int32_t i = 0; i < m_objects.count; ++i)
    {
        const size_t o = m_objects.dataAt + static_cast<size_t>(i) * 20;
        int32_t slot; std::memcpy(&slot, m_bytes.data() + o, 4);
        if (slot != slotId) continue;

        int16_t v16; uint16_t u16v; int32_t v32; uint32_t u32v;
        std::memcpy(&v16, m_bytes.data() + o + 4, 2);  out.nmeshes = v16;
        std::memcpy(&v16, m_bytes.data() + o + 6, 2);  out.meshIndex = v16;
        std::memcpy(&v32, m_bytes.data() + o + 8, 4);  out.boneIndex = v32;
        std::memcpy(&u32v, m_bytes.data() + o + 12, 4); out.frameOffset = u32v;
        std::memcpy(&u16v, m_bytes.data() + o + 16, 2);
        out.animIndex = (u16v == 0xFFFF) ? -1 : static_cast<int>(u16v);
        out.slot = slotId;
        return true;
    }
    return false;
}

std::vector<int> Tr5Level::moveableSlots() const
{
    std::vector<int> raus;
    for (int32_t i = 0; i < m_objects.count; ++i)
    {
        int32_t slot;
        std::memcpy(&slot, m_bytes.data() + m_objects.dataAt + static_cast<size_t>(i) * 20, 4);
        raus.push_back(slot);
    }
    return raus;
}

int32_t Tr5Level::debugReadBoneLong(long long i) const
{
    if (i < 0 || i >= m_bones.count) return 0;
    int32_t v;
    std::memcpy(&v, m_bytes.data() + m_bones.dataAt + static_cast<size_t>(i) * 4, 4);
    return v;
}

int16_t Tr5Level::debugReadFrameShort(long long i) const
{
    if (i < 0 || i >= m_frames.count) return 0;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_frames.dataAt + static_cast<size_t>(i) * 2, 2);
    return v;
}

bool Tr5Level::debugMeshHeader(int ptrIndex, int16_t out[6]) const
{
    if (ptrIndex < 0 || ptrIndex >= m_meshPtrs.count) return false;
    uint32_t ofs;
    std::memcpy(&ofs, m_bytes.data() + m_meshPtrs.dataAt + static_cast<size_t>(ptrIndex) * 4, 4);
    if (ofs + 12 > static_cast<uint32_t>(m_meshData.count) * 2) return false;
    std::memcpy(out, m_bytes.data() + m_meshData.dataAt + ofs, 12);
    return true;
}

//----------------------------------------------------------------------------
// LaraType: das Outfit-Byte. Es liegt in den 32 Bytes zwischen den
// Texturbloecken und dem Hauptblock (LaraType u16, WeatherType u16, Rest
// Polsterung) - genau der Bereich, den readHeader mit "+ 32" ueberspringt.
//----------------------------------------------------------------------------
int Tr5Level::laraType() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 32, 2);
    return v;
}

bool Tr5Level::setLaraType(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 32, &v, 2);
    return true;
}

bool Tr5Level::readHeader(std::string& error)
{
    size_t pos = 0;

    if (!readI32(m_bytes, pos, m_version)) { error = "Datei zu kurz fuer den Versionskopf"; return false; }
    if (!readI16(m_bytes, pos, m_rtPages) ||
        !readI16(m_bytes, pos, m_otPages) ||
        !readI16(m_bytes, pos, m_btPages))
    {
        error = "Datei zu kurz fuer die Texturseiten-Angaben";
        return false;
    }

    // DREI Texturbloecke, nicht zwei: 32 Bit, 16 Bit und ein Misc-Block
    // (Schrift und Himmel). An echten Dateien nachgerechnet, Andrea1 etwa:
    // 17 Seiten * 4 * 0x10000 = 4456448, dann 17 * 2 * 0x10000 = 2228224,
    // dann 3 * 4 * 0x10000 = 786432. Die Engine liest je nach Anzeigeformat
    // einen davon und ueberspringt die anderen.
    //
    // Ob die Nutzdaten komprimiert sind, steht nicht als Flag in der Datei -
    // die Engine entscheidet das ueber eine globale Variable. Statt zu raten,
    // werden beide Auslegungen probiert; gueltig ist die, bei der der
    // Hauptblock hinterher schluessig daliegt. Sein Erkennungszeichen ist
    // eindeutig: er ist unkomprimiert, und dann traegt er size == compressedSize.
    for (int mode = 0; mode < 2; ++mode)
    {
        const bool compressed = (mode == 0);
        size_t p = pos;
        bool ok = true;

        for (int block = 0; block < 3 && ok; ++block)
        {
            int32_t size = 0, compSize = 0;
            if (!readI32(m_bytes, p, size) || !readI32(m_bytes, p, compSize)) { ok = false; break; }
            if (size < 0 || compSize < 0) { ok = false; break; }
            ok = skip(m_bytes, p, compressed ? compSize : size);
        }
        if (!ok) continue;

        // short data[16] - LaraDrawType, WeatherType und weitere Levelschalter.
        if (!skip(m_bytes, p, 32)) continue;

        int32_t mainSize = 0, mainComp = 0;
        if (!readI32(m_bytes, p, mainSize) || !readI32(m_bytes, p, mainComp)) continue;
        if (mainSize <= 0 || mainSize != mainComp) continue;
        if (p + static_cast<size_t>(mainSize) > m_bytes.size()) continue;

        // Hinter dem Hauptblock folgen noch die Sound-Samples, die die Engine
        // direkt aus der Datei nachliest - ein Rest ist also normal und kein
        // Zeichen fuer eine Fehldeutung.
        m_mainOffset = p;
        m_mainSize = static_cast<size_t>(mainSize);
        return true;
    }

    error = "Kopfteil nicht deutbar - weder komprimierte noch unkomprimierte "
            "Texturbloecke ergeben einen passenden Hauptblock. Ist das wirklich "
            "eine TR5-Leveldatei?";
    return false;
}

bool Tr5Level::save(const std::wstring& path, std::string& error) const
{
    if (m_bytes.empty()) { error = "Kein Level geladen"; return false; }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
    {
        error = "Zieldatei laesst sich nicht schreiben";
        return false;
    }
    size_t put = std::fwrite(m_bytes.data(), 1, m_bytes.size(), f);
    std::fclose(f);
    if (put != m_bytes.size()) { error = "Zieldatei nicht vollstaendig geschrieben"; return false; }
    return true;
}
