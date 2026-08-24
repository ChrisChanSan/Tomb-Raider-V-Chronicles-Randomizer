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

    // Je Eintrag gelesene Bytes in den Schleifen, die Feld fuer Feld read.
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
            "an Offset %zu (n=%d, mainOffset=%zu, end=%zu, file=%zu). "
            "Passt die Datei zum erwarteten TR5-Format?",
            where, p, n, m_mainOffset, m_mainOffset + m_mainSize, m_bytes.size());
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
    r.headerOffset = offset;

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
    shiftSound(24, itemsEnd);

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

bool Tr5Level::addAiObject(const Tr5AiObject& in, std::string& error)
{
    // Der AI-Block liegt direkt hinter den Items: int32 Anzahl, dann
    // AIOBJECT-Eintraege zu je 24 Byte (LoadAIInfo, specific/file.cpp:1209).
    // Einfuegen am Blockende, dieselbe Mechanik wie addItem: alles dahinter
    // im Hauptblock (Cutscene-Daten, Sound) rueckt mit.
    const size_t aiCountAt = m_itemCountOffset + 4 + static_cast<size_t>(m_itemCount) * 24;
    if (aiCountAt + 4 > m_bytes.size()) { error = "AI-Block liegt ausserhalb der Datei"; return false; }

    int32_t nAi = 0;
    std::memcpy(&nAi, m_bytes.data() + aiCountAt, 4);
    if (nAi < 0 || aiCountAt + 4 + static_cast<size_t>(nAi) * 24 > m_bytes.size())
    { error = "AI-Block unplausibel"; return false; }

    const size_t einfuegenAn = aiCountAt + 4 + static_cast<size_t>(nAi) * 24;
    m_bytes.insert(m_bytes.begin() + static_cast<ptrdiff_t>(einfuegenAn), 24, 0);
    shiftSound(24, einfuegenAn);

    ++nAi;
    std::memcpy(m_bytes.data() + aiCountAt, &nAi, 4);

    uint8_t* p = m_bytes.data() + einfuegenAn;
    std::memcpy(p + 0, &in.objectNumber, 2);
    std::memcpy(p + 2, &in.roomNumber, 2);
    std::memcpy(p + 4, &in.x, 4);
    std::memcpy(p + 8, &in.y, 4);
    std::memcpy(p + 12, &in.z, 4);
    std::memcpy(p + 16, &in.ocb, 2);
    std::memcpy(p + 18, &in.flags, 2);
    std::memcpy(p + 20, &in.yRot, 2);
    std::memcpy(p + 22, &in.boxNumber, 2);

    m_mainSize += 24;
    const int32_t newSize = static_cast<int32_t>(m_mainSize);
    std::memcpy(m_bytes.data() + m_mainOffset - 8, &newSize, 4);
    std::memcpy(m_bytes.data() + m_mainOffset - 4, &newSize, 4);

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
        e.function = w & 0x1F;
        e.art = (w >> 8) & 0x3F;
        e.words.push_back(w);
        const bool last = (w & 0x8000) != 0;

        if (e.function == FN_TRIGGER)
        {
            if (i >= limit) return false;
            e.words.push_back(word(i++));         // Timer, OneShot, Maske

            bool more = true;
            if (hasRefWord(e.art))
            {
                if (i >= limit) return false;
                const uint16_t r = word(i++);
                e.words.push_back(r);
                more = (r & 0x8000) == 0;
            }
            while (more)
            {
                if (i >= limit) return false;
                const uint16_t c = word(i++);
                e.words.push_back(c);
                const int akt = cmdType(c);
                if (akt == 1 /*Kamera*/ || akt == 12 /*Flyby*/)
                {
                    if (i >= limit) return false;
                    e.words.push_back(word(i++));
                }
                if (c & 0x8000) break;
            }
        }
        else
        {
            for (int k = 0; k < extraWords(e.function); ++k)
            {
                if (i >= limit) return false;
                e.words.push_back(word(i++));
            }
        }

        out.push_back(e);
        if (last) break;
    }
    return true;
}

long Tr5Level::musicTriggers(const std::map<int, int>* mapping, std::set<int>* found)
{
    // Der Lauf stuetzt sich bewusst auf readChain: die kennt jede
    // Wortverteilung (Referenzwort der Schalter/Key/Pickup-Arten,
    // Zusatzwoerter von Kamera und Flyby, extraWords der uebrigen
    // Funktionen). Die Dateiposition eines Wortes ergibt sich exakt aus
    // Kettenanfang + aufsummierten Eintragslaengen, weil readChain streng
    // linear liest.
    std::set<int> ketten;
    std::vector<Tr5FloorEntry> eintraege;
    long zahl = 0;

    for (size_t rm = 0; rm < m_rooms.size(); ++rm)
    {
        const Tr5Room& r = m_rooms[rm];

        for (int sx = 0; sx < r.numXSectors; ++sx)
        {
            for (int sz = 0; sz < r.numZSectors; ++sz)
            {
                const int start = sectorIndex(static_cast<int>(rm), sx, sz);
                if (start <= 0 || !ketten.insert(start).second) continue;
                if (!readChain(start, eintraege)) continue;   // unlesbar: Finger weg

                size_t pos = static_cast<size_t>(start);
                for (const Tr5FloorEntry& e : eintraege)
                {
                    if (e.function == FN_TRIGGER)
                    {
                        // Aktionsworte beginnen hinter Kopf + Setup (+ Referenzwort).
                        size_t k = hasRefWord(e.art) ? 3 : 2;
                        for (; k < e.words.size(); ++k)
                        {
                            const uint16_t c = e.words[k];
                            const int akt = cmdType(c);

                            if (akt == 1 || akt == 12)      // Kamera/Flyby: Zusatzwort
                            {
                                ++k;
                                continue;
                            }

                            if (akt != 8)                   // TO_CD
                                continue;

                            const int track = c & 0x3FF;

                            if (found)
                            {
                                found->insert(track);
                                ++zahl;
                            }

                            if (mapping)
                            {
                                const auto it = mapping->find(track);
                                if (it != mapping->end() && it->second != track)
                                {
                                    const uint16_t neu = static_cast<uint16_t>((c & 0xFC00) | (it->second & 0x3FF));
                                    std::memcpy(m_bytes.data() + m_floorAt + (pos + k) * 2, &neu, 2);
                                    ++zahl;
                                }
                            }
                        }
                    }

                    pos += e.words.size();
                }
            }
        }
    }

    return zahl;
}

int Tr5Level::floorSlope(int room, int sx, int sz, SlopeInfo& si, int& baseY) const
{
    si = SlopeInfo();
    baseY = 0;

    if (room < 0 || room >= static_cast<int>(m_rooms.size())) return -1;
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors) return -1;

    // floor-Byte des Sektors: Vorzeichenbyte an +5 des 8-Byte-Eintrags
    // (FLOOR_INFO in global/types.h: index, fx/box/stopper, pit_room, floor,
    // sky_room, ceiling). GetHeight beginnt mit genau diesem Wert << 8.
    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    baseY = static_cast<int8_t>(m_bytes[at + 5]) << 8;

    const int idx = sectorIndex(room, sx, sz);
    if (idx < 0) return -1;
    if (idx == 0) return 0;                         // keine Floordata = eben

    std::vector<Tr5FloorEntry> chain;
    if (!readChain(idx, chain)) return -1;          // unlesbar: Finger weg

    for (const Tr5FloorEntry& e : chain)
    {
        const bool boden = (e.function == FN_TILT || e.function == 7 ||
                            e.function == 8 || (e.function >= 11 && e.function <= 14));
        if (!boden) continue;
        if (e.words.size() < 2) return -1;        // Eintrag ohne Datenwort: kaputt
        si.typeWord = e.words[0];
        si.dataWord = e.words[1];
        return 1;
    }
    return 0;                                       // nur Portale/Trigger/...: eben
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
        if (e.function == FN_TRIGGER)     return "Sektor hat schon einen Trigger";
        if (e.function == FN_LAVA)        return "Sektor hat einen Lava-Eintrag";
        if (e.function == FN_CLIMB)       return "Sektor hat einen Climb-Eintrag";
        if (e.function == FN_MONKEY)      return "Sektor hat einen Monkey-Eintrag";
        if (e.function == FN_TRIGTRIGGER) return "Sektor hat einen TrigTrigger-Eintrag";
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
        std::vector<uint16_t> w = chain[n].words;
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
    shiftSound(raw.size(), at);      // der Sound-Block liegt noch weiter hinten

    // Und die BLOCKKARTE gleich mit. Mesh-, Anim-, Frame- und Objektbloecke
    // liegen im Hauptblock ebenfalls hinter der Floordata; ohne diese
    // Korrektur zeigen sie nach dem ersten angehaengten Trigger daneben.
    // Lange unbemerkt geblieben, weil niemand nach einem Floordata-Anhang
    // noch in diese Bloecke gelesen hat - erst die Anim-Pruefung tat es und
    // meldete prompt, dass Laras Animationen "fehlen" (#53).
    auto blockNachziehen = [&](Block& b) {
        if (b.countAt >= at) b.countAt += raw.size();
        if (b.dataAt  >= at) b.dataAt  += raw.size();
    };
    blockNachziehen(m_meshData);  blockNachziehen(m_meshPtrs);
    blockNachziehen(m_anims);     blockNachziehen(m_changes);
    blockNachziehen(m_ranges);    blockNachziehen(m_commands);
    blockNachziehen(m_bones);     blockNachziehen(m_frames);
    blockNachziehen(m_objects);   blockNachziehen(m_sprites);
    blockNachziehen(m_texinfos);

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
        if (from[k].function == FN_TRIGGER) { ++found; at = k; }
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

bool Tr5Level::copyTrigger(int fromRoom, int fromSx, int fromSz,
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

    // Wie bei moveTrigger: genau ein Triggereintrag. Zwei waeren mehrdeutig,
    // keiner ein Bedienfehler.
    size_t found = 0, at = 0;
    for (size_t k = 0; k < from.size(); ++k)
        if (from[k].function == FN_TRIGGER) { ++found; at = k; }
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

    // Nur die Zielkette waechst. Die Quelle bleibt Wort fuer Wort, wie sie
    // war - das ist der ganze Unterschied zu moveTrigger.
    to.push_back(from[at]);

    uint16_t newTo = 0;
    if (!appendChain(to, newTo, error)) return false;

    setSectorIndex(m_rooms[static_cast<size_t>(toRoom)], toSx, toSz, newTo);
    return true;
}

bool Tr5Level::addTrigger(int room, int sx, int sz, int art, int timer, bool oneShot,
                          int mask, const std::vector<Tr5TriggerCmd>& cmds,
                          std::string& error, int refItem)
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
    e.function = FN_TRIGGER;
    e.art = art;
    // Wort 0: Funktion in Bit 0-4, Art in Bit 8-13. Bit 15 vergibt baueKette.
    e.words.push_back(static_cast<uint16_t>((art << 8) | FN_TRIGGER));
    // Wort 1: Timer, OneShot, Codebits.
    e.words.push_back(static_cast<uint16_t>((timer & 0xFF) |
                                              (oneShot ? 0x100 : 0) |
                                              ((mask & 0x1F) << 9)));

    // SWITCH, KEY und PICKUP schieben ein Referenzwort vor die Befehlsliste:
    // das Item, das bedient bzw. aufgenommen werden muss. Die Engine liest es
    // dreimal gleich (control.cpp:2251/2266/2274, "value = *(data++) & 0x3FF")
    // und wertet es NICHT als Befehl.
    //
    // Frueher stand hier eine Absage - so ein Trigger ist eben keiner, den man
    // frei hinstellt, sondern einer, der an ein vorhandenes Objekt gebunden
    // ist. Mit den Secret-Listen (#53) gibt es diesen Fall jetzt: der Nutzer
    // setzt ein Schluesselloch und dazu den Key-Trigger, der es mit einer Tuer
    // verbindet. Wer refItem mitgibt, sagt damit ausdruecklich, an welches
    // Objekt der Trigger gebunden ist.
    if (hasRefWord(art))
    {
        if (refItem < 0)
        {
            error = "Triggerart braucht ein Referenzwort (refItem) - so nicht anlegbar";
            return false;
        }
        e.words.push_back(static_cast<uint16_t>(refItem & 0x3FF));
    }
    else if (refItem >= 0)
    {
        error = "Diese Triggerart kennt kein Referenzwort - refItem waere wirkungslos";
        return false;
    }

    for (size_t i = 0; i < cmds.size(); ++i)
    {
        uint16_t c = static_cast<uint16_t>(((cmds[i].type & 0xF) << 10) | (cmds[i].param & 0x3FF));
        if (i + 1 == cmds.size()) c |= 0x8000;       // Endebit auf den letzten
        e.words.push_back(c);

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
// Einen Wandsektor zum Portal machen (#53, andy3-Doppeltuer).
//
// Was ein Portalsektor ist, wurde mit SektorPruef an den funktionierenden
// Nachbarn in andy3 nachgemessen (Raum 34 Spalte x0, Raum 36 Spalte x4):
//
//   - Boden/Decke tragen die Values des UEBERLAPPTEN begehbaren Sektors des
//     Zielraums. Die Portalspalten zweier Raeume liegen naemlich uebereinander:
//     34(0,z) deckt dieselbe Weltflaeche wie 36(3,z). Von dort kommen auch
//     pit_room und sky_room.
//   - Das Boxwort ist bei allen Portalsektoren fx=6, box=2047 (keine Box,
//     0x7FF6) - Portale gehoeren keiner KI-Box.
//   - Die Kette endet mit DOOR -> Zielraum; Neigungen davor bleiben
//     (36(4,4) vanilla: [TILT][SPLIT4][DOOR]).
//
// Das SICHTFENSTER (32-Byte-Portaleintrag der Raumliste) wird hier NICHT
// angelegt - es muss schon existieren. Bei andy3 hat Core beide Fenster
// fertig eingebaut und nur die Sektoren versiegelt; genau dieser Fall wird
// geprueft, und sein Fehlen ist ein harter Fehler statt eines schwarzen
// Lochs im Bild.
//----------------------------------------------------------------------------
bool Tr5Level::makePortal(int room, int sx, int sz, int targetRoom, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "Raum " + std::to_string(room) + " gibt es nicht"; return false; }
    if (targetRoom < 0 || targetRoom >= static_cast<int>(m_rooms.size()))
    { error = "Zielraum " + std::to_string(targetRoom) + " gibt es nicht"; return false; }

    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    const Tr5Room& z = m_rooms[static_cast<size_t>(targetRoom)];
    if (!r.sectorsOffset || !z.sectorsOffset)
    { error = "Sektorfeld nicht aufgegangen"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "Sektor ausserhalb des Raums"; return false; }

    // Sichtfenster-Vorbedingung: der Zielraum muss in der Portalliste stehen.
    bool fenster = false;
    for (int p : r.portalRooms) if (p == targetRoom) { fenster = true; break; }
    if (!fenster)
    {
        error = "Raum " + std::to_string(room) + " hat kein Sichtfenster nach " +
                std::to_string(targetRoom) + " - das 32-Byte-Portal fehlt, ohne es "
                "waere die Oeffnung ein schwarzes Loch";
        return false;
    }

    // Ueberlappten Sektor des Zielraums finden: gleiche Weltflaeche.
    const int weltX = r.originX + sx * 1024;
    const int weltZ = r.originZ + sz * 1024;
    const int osx = (weltX - z.originX) / 1024;
    const int osz = (weltZ - z.originZ) / 1024;
    if (osx < 0 || osx >= z.numXSectors || osz < 0 || osz >= z.numZSectors)
    { error = "Zielraum ueberlappt den Sektor nicht"; return false; }

    const size_t eigenAt   = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    const size_t druebenAt = z.sectorsOffset + (static_cast<size_t>(osx) * z.numZSectors + osz) * 8;
    if (eigenAt + 8 > m_bytes.size() || druebenAt + 8 > m_bytes.size())
    { error = "Sektor liegt ausserhalb der Datei"; return false; }

    // Der ueberlappte Sektor muss walkable sein - eine Wand als Vorlage
    // hiesse, das Portal fuehrte ins Massiv.
    if (static_cast<int8_t>(m_bytes[druebenAt + 5]) == -127)
    { error = "der ueberlappte Sektor im Zielraum ist selbst eine Wand"; return false; }

    // Bestehende Kette read; Geometrie (TILT/ROOF/SPLITs/NOCOLs) bleibt.
    uint16_t alt;
    std::memcpy(&alt, m_bytes.data() + eigenAt, 2);
    std::vector<Tr5FloorEntry> kette;
    if (alt && !readChain(alt, kette))
    { error = "Floordata-Kette nicht lesbar"; return false; }

    std::vector<Tr5FloorEntry> neu;
    for (const Tr5FloorEntry& e : kette)
    {
        if (e.function == 1)                              // schon ein Portal?
        {
            if (e.words.size() >= 2 && e.words[1] == static_cast<uint16_t>(targetRoom))
                return true;                              // genau dieses: fertig
            error = "Sektor hat schon ein Portal in einen anderen Raum";
            return false;
        }
        if (e.function == 2 || e.function == 3 ||
            (e.function >= 7 && e.function <= 18))
            neu.push_back(e);
        // alles andere (Trigger, Climb, ...) hat auf einem Portalsektor
        // nichts verloren und faellt weg - vanilla tragen sie so etwas nie.
    }

    Tr5FloorEntry tuer;
    tuer.function = 1;
    tuer.words.push_back(0x0001);
    tuer.words.push_back(static_cast<uint16_t>(targetRoom));
    neu.push_back(tuer);

    uint16_t idx = 0;
    if (!appendChain(neu, idx, error)) return false;
    setSectorIndex(r, sx, sz, idx);

    // Boxwort: fx=6, box=2047 (keine), stopper=0 - wie alle Portalsektoren.
    const uint16_t boxwort = 0x7FF6;
    std::memcpy(m_bytes.data() + eigenAt + 2, &boxwort, 2);

    // pit/floor/sky/ceiling vom ueberlappten Sektor.
    std::memcpy(m_bytes.data() + eigenAt + 4, m_bytes.data() + druebenAt + 4, 4);
    return true;
}

//----------------------------------------------------------------------------
// Einen Sektor kletterbar machen (#53, Make_Climb).
//
// Ein CLIMB-Eintrag ist EIN Wort: Funktion 6 in Bit 0-4, die kletterbaren
// Wandseiten in Bit 8-11. Die Engine prueft in _TestTriggers
// (control.cpp:2189): quad = (yRot + 0x2000) / 0x4000, Bit = 1 << (quad+8) -
// 0 Grad -> 0x100, 90 -> 0x200, 180 -> 0x400, 270 -> 0x800. Der Winkel ist
// Laras BLICKrichtung auf die Wand, dieselbe Konvention wie bei den
// Wandgriffen.
//
// Die KETTENORDNUNG ist Vorschrift, nicht Geschmack: _TestTriggers liest ab
// trigger_index der Reihe nach [LAVA] -> [CLIMB] -> Rest. Ein CLIMB hinter
// dem Trigger wuerde als Triggerbefehl fehlgedeutet, einer vor der LAVA
// liesse die LAVA ungelesen. Deshalb wird die Kette neu zusammengesetzt:
// Geometrie, dann LAVA, dann CLIMB, dann alles Uebrige in alter Folge.
//
// Wiederholbar: vorhandene CLIMB-Bits werden ge-ODER-t, ein zweiter Aufruf
// mit denselben Bits aendert nichts.
//----------------------------------------------------------------------------
bool Tr5Level::makeClimb(int room, int sx, int sz, int dirBits, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "Raum " + std::to_string(room) + " gibt es nicht"; return false; }
    const Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.sectorsOffset) { error = "Sektorfeld nicht aufgegangen"; return false; }
    if (sx < 0 || sx >= r.numXSectors || sz < 0 || sz >= r.numZSectors)
    { error = "Sektor ausserhalb des Raums"; return false; }
    if ((dirBits & ~0x0F00) || !(dirBits & 0x0F00))
    { error = "Kletterbits muessen in 0x100..0x800 liegen"; return false; }

    const size_t at = r.sectorsOffset + (static_cast<size_t>(sx) * r.numZSectors + sz) * 8;
    uint16_t alt;
    std::memcpy(&alt, m_bytes.data() + at, 2);

    std::vector<Tr5FloorEntry> kette;
    if (alt && !readChain(alt, kette))
    { error = "Floordata-Kette nicht lesbar"; return false; }

    std::vector<Tr5FloorEntry> geo, lava, rest;
    Tr5FloorEntry* climb = nullptr;
    Tr5FloorEntry climbAlt;
    for (Tr5FloorEntry& e : kette)
    {
        if (e.function == 6)      { climbAlt = e; climb = &climbAlt; }
        else if (e.function == 5) lava.push_back(e);
        else if (e.function == 1 || e.function == 2 || e.function == 3 ||
                 (e.function >= 7 && e.function <= 18))
            geo.push_back(e);
        else
            rest.push_back(e);
    }

    if (climb)
    {
        if ((climb->words[0] & 0x0F00 & dirBits) == static_cast<uint16_t>(dirBits))
            return true;                              // Bits stehen schon
        climb->words[0] |= static_cast<uint16_t>(dirBits);
    }
    else
    {
        climbAlt = Tr5FloorEntry();
        climbAlt.function = 6;
        climbAlt.words.push_back(static_cast<uint16_t>(0x0006 | dirBits));
        climb = &climbAlt;
    }

    std::vector<Tr5FloorEntry> neu;
    for (const Tr5FloorEntry& e : geo)  neu.push_back(e);
    for (const Tr5FloorEntry& e : lava) neu.push_back(e);
    neu.push_back(*climb);
    for (const Tr5FloorEntry& e : rest) neu.push_back(e);

    uint16_t idx = 0;
    if (!appendChain(neu, idx, error)) return false;
    setSectorIndex(r, sx, sz, idx);
    return true;
}

//----------------------------------------------------------------------------
// Das Wasserbit eines Raums setzen oder loeschen (#53, Make_Room_Water /
// Make_Room_Nowater).
//
// ROOM_UNDERWATER ist Bit 0 der Raumflags (global/types.h:281), im Raumkopf
// bei +78. Der Motor haengt 44 Stellen daran: Laras Schwimmzustand, Kamera,
// Sound, KI-Wege, Feuer. Es umzulegen ist damit ein echter Eingriff und
// keine Optik - der Raum wird wirklich zum Wasserraum.
//
// Wiederholbar: steht das Bit schon wie gewuenscht, passiert nichts.
//----------------------------------------------------------------------------
bool Tr5Level::setRoomWater(int room, bool anAus, std::string& error)
{
    if (room < 0 || room >= static_cast<int>(m_rooms.size()))
    { error = "Raum " + std::to_string(room) + " gibt es nicht"; return false; }

    Tr5Room& r = m_rooms[static_cast<size_t>(room)];
    if (!r.headerOffset) { error = "Raumkopf nicht aufgegangen"; return false; }

    constexpr size_t OFS_FLAGS = 78;
    const size_t at = r.headerOffset + OFS_FLAGS;
    if (at + 2 > m_bytes.size()) { error = "Raumkopf liegt ausserhalb der Datei"; return false; }

    uint16_t f;
    std::memcpy(&f, m_bytes.data() + at, 2);
    const uint16_t neu = anAus ? static_cast<uint16_t>(f | 0x0001)
                               : static_cast<uint16_t>(f & ~0x0001);
    if (neu == f) return true;                    // steht schon so

    std::memcpy(m_bytes.data() + at, &neu, 2);
    r.flags = neu;
    return true;
}

//----------------------------------------------------------------------------
// Traegt Laras Animation <animNr> in diesem Level echte Bilddaten?
//
// Cores Stub-Kennzeichen ist die FrameSize null (hohes Byte des Feldes
// "interpolation", ANIM_STRUCT in global/types.h:645). Der Motor rechnet die
// Bildadresse als frame_ptr[n * FrameSize] - bei null steht sie fuer immer
// auf demselben Bild, und abgespielt wird eine eingefrorene Pose.
//
// Zweiter Fall: die Groesse stimmt, aber zwischen dem eigenen Frame-Zeiger
// und dem naechstgroesseren liegt weniger Platz, als die Keyframes brauchen.
// Dann gehoeren die Daten dort einer anderen Animation.
//
// Gefunden ueber ANIM_KEYCARD in joby3 (22 Sekunden erstarrt) und den
// Schalter in andy3 (falsche, viel zu kurze Bewegung). Siehe LaraAnim.
//----------------------------------------------------------------------------
bool Tr5Level::laraAnimUsable(int animNr) const
{
    MoveableInfo mv;
    if (!moveableInfo(0, mv) || mv.animIndex < 0) return false;

    const long long a = static_cast<long long>(mv.animIndex) + animNr;
    if (a < 0 || a >= m_anims.count) return false;

    const size_t o = m_anims.dataAt + static_cast<size_t>(a) * 40;
    const uint8_t groesse = m_bytes[o + 4 + 1];          // hohes Byte
    if (!groesse) return false;                          // Stub, Form 1

    const uint8_t rate = m_bytes[o + 4];
    uint16_t von, bis;
    std::memcpy(&von, m_bytes.data() + o + 24, 2);
    std::memcpy(&bis, m_bytes.data() + o + 26, 2);
    // Stub, Form 2: leerer Bildbereich. joby4 traegt seine AIRLOCKSW so -
    // FrameSize 10, aber Frames 0..0. Ein Spender mit dieser Form waere ein
    // Tausch von Stub gegen Stub.
    if (bis <= von) return false;
    const int keyframes = (rate > 0 && bis >= von) ? ((bis - von) / rate + 1) : 1;
    const long long gebraucht = static_cast<long long>(keyframes) * groesse * 2;

    uint32_t eigen;
    std::memcpy(&eigen, m_bytes.data() + o, 4);
    uint32_t naechster = static_cast<uint32_t>(m_frames.count) * 2;
    for (int32_t i = 0; i < m_anims.count; ++i)
    {
        uint32_t z;
        std::memcpy(&z, m_bytes.data() + m_anims.dataAt + static_cast<size_t>(i) * 40, 4);
        if (z > eigen && z < naechster) naechster = z;
    }
    return gebraucht <= static_cast<long long>(naechster) - eigen;
}

//----------------------------------------------------------------------------
// Auf welche Anim zeigt Laras Anim <animNr> mit next_animation?
//
// Gebraucht, um eine Bewegung als KETTE zu reparieren statt gliedweise: die
// Kriechrolle laeuft 218 -> 219 -> 220 -> 222, und Core stubbt solche
// Ketten am Stueck. Liefert -1, wenn es die Anim nicht gibt oder der
// Nachfolger ausserhalb von Laras Satz liegt.
//----------------------------------------------------------------------------
int Tr5Level::laraAnimNext(int animNr) const
{
    MoveableInfo mv;
    if (!moveableInfo(0, mv) || mv.animIndex < 0) return -1;

    const long long a = static_cast<long long>(mv.animIndex) + animNr;
    if (a < 0 || a >= m_anims.count) return -1;

    uint16_t weiter;
    std::memcpy(&weiter, m_bytes.data() + m_anims.dataAt + static_cast<size_t>(a) * 40 + 28, 2);

    const long long rel = static_cast<long long>(weiter) - mv.animIndex;
    if (rel < 0 || static_cast<long long>(mv.animIndex) + rel >= m_anims.count) return -1;
    return static_cast<int>(rel);
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

void Tr5Level::shiftSound(size_t bytes, size_t abOffset)
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

    // Zufallsvarianten: N aufeinanderfolgende SAMPLES, N in Bit 2-5 der
    // Flags des Infos - die Engine wuerfelt sample = number + Zufall(N)
    // (game/sound.cpp:280). Es ist also EIN Info mit N Samples, NICHT N
    // Infos: die alte Lesart hier kopierte die Samples der NACHBAR-Infos
    // (= fremde Klaenge) als "Varianten" mit - im Ziel wuerfelte die
    // Engine dann zwischen dem richtigen Klang und fremden, mit den Flags
    // des richtigen (im Spiel gehoert, #76: TwoGun spielt beim Laufen
    // zufaellig seinen Schuss statt der Schritte, Gladiator/Roman God
    // einen High-Pitch-Schrei beim Treffer - alles Mehrvarianten-Sounds).
    int16_t flags = 0;
    std::memcpy(&flags, src.m_bytes.data() + src.m_infosAt + static_cast<size_t>(erste) * SZ_SAMPLE_INFO + 6, 2);
    int varianten = (static_cast<int>(static_cast<uint16_t>(flags)) >> 2) & 0xF;
    if (varianten <= 0) varianten = 1;

    int16_t nummer = 0;
    std::memcpy(&nummer, src.m_bytes.data() + src.m_infosAt + static_cast<size_t>(erste) * SZ_SAMPLE_INFO, 2);
    if (nummer < 0 || nummer + varianten > src.m_nSamples)
    { error = "Info zeigt auf Samples, die es nicht gibt"; return false; }

    if (m_nSamples + varianten > MAX_SAMPLES_PLAYABLE)
    {
        std::snprintf(buf, sizeof(buf),
            "Level haette %d Samples - die Engine laedt hoechstens %d",
            m_nSamples + varianten, MAX_SAMPLES_PLAYABLE);
        error = buf;
        return false;
    }

    // Quellsamples nummer..nummer+varianten-1 in einem Durchlauf einsammeln,
    // bevor irgendetwas geschrieben wird.
    std::vector<std::vector<uint8_t>> infos;
    std::vector<std::pair<int32_t, std::vector<uint8_t>>> samples;
    {
        size_t q = src.m_numSamplesAt + 4;
        for (int32_t i = 0; i < src.m_nSamples && static_cast<int>(samples.size()) < varianten; ++i)
        {
            if (q + 8 > src.m_bytes.size()) break;
            int32_t uc = 0, cs = 0;
            std::memcpy(&uc, src.m_bytes.data() + q, 4);
            std::memcpy(&cs, src.m_bytes.data() + q + 4, 4);
            q += 8;
            if (cs < 0 || q + static_cast<size_t>(cs) > src.m_bytes.size()) break;
            if (i >= nummer)
                samples.push_back({ uc, std::vector<uint8_t>(src.m_bytes.begin() + q,
                                                             src.m_bytes.begin() + q + cs) });
            q += static_cast<size_t>(cs);
        }
        if (static_cast<int>(samples.size()) != varianten)
        { error = "Sampledaten der Quelle nicht auffindbar"; return false; }
    }

    {
        const size_t o = src.m_infosAt + static_cast<size_t>(erste) * SZ_SAMPLE_INFO;
        std::vector<uint8_t> info(src.m_bytes.begin() + o, src.m_bytes.begin() + o + SZ_SAMPLE_INFO);
        const int16_t neuNummer = static_cast<int16_t>(m_nSamples);
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

    m_nInfos += 1;      // EIN Info; die Varianten sind Samples, keine Infos
    std::memcpy(m_bytes.data() + m_numInfosAt, &m_nInfos, 4);

    // Alles hinter der Einfuegestelle ist gewandert.
    shiftSound(roh.size(), einfuegenAn);
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

bool Tr5Level::adoptAnimData(int dstSlot, int dstOffset, int srcSlot,
                                   int srcOffset, std::string& error)
{
    MoveableInfo dst, src;
    if (!moveableInfo(dstSlot, dst) || dst.animIndex < 0)
    { error = "Zielslot fehlt oder traegt keine Anims"; return false; }
    if (!moveableInfo(srcSlot, src) || src.animIndex < 0)
    { error = "Quellslot fehlt oder traegt keine Anims"; return false; }

    const long long d = static_cast<long long>(dst.animIndex) + dstOffset;
    const long long s = static_cast<long long>(src.animIndex) + srcOffset;
    if (d < 0 || d >= m_anims.count || s < 0 || s >= m_anims.count)
    { error = "Anim liegt ausserhalb der Tabelle"; return false; }

    uint8_t* dr = m_bytes.data() + m_anims.dataAt + static_cast<size_t>(d) * 40;
    const uint8_t* sr = m_bytes.data() + m_anims.dataAt + static_cast<size_t>(s) * 40;

    // Schablonengleichheit der Familiensaetze absichern: beide Anims muessen
    // denselben Frame-Bereich tragen, sonst passen Keyframe-Zaehlung und
    // Kommando-Framenummern nicht.
    uint16_t dVon, dBis, sVon, sBis;
    std::memcpy(&dVon, dr + 24, 2); std::memcpy(&dBis, dr + 26, 2);
    std::memcpy(&sVon, sr + 24, 2); std::memcpy(&sBis, sr + 26, 2);
    if (dVon != sVon || dBis != sBis)
    { error = "Frame-Bereiche der beiden Anims sind verschieden"; return false; }

    std::memcpy(dr, sr, 6);           // Frame-Zeiger, Rate, frame_size
    std::memcpy(dr + 8, sr + 8, 16);  // Geschwindigkeit/Beschleunigung
    std::memcpy(dr + 36, sr + 36, 4); // Kommandozahl + -offset (globaler Block)
    return true;
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

//----------------------------------------------------------------------------
// data[2] derselben 32 Bytes: "Sprechkoepfe passen nicht zum Outfit". Die
// Engine tauscht Laras Kopf in Dialog-Cutscenes gegen LARA_SPEECH_HEAD1-4
// (deltapak.cpp, handle_lara_chatting) - die tragen das Vanilla-Outfit des
// Levels. Kann der Outfit-Spender keine eigenen Sprechkoepfe liefern (rich2:
// die VCI-Level kennen keine Dialog-Cutscenes), laesst die Engine den Tausch
// bei gesetztem Flag aus. Vanilla steht hier in allen 16 Dateien 0.
//----------------------------------------------------------------------------
int Tr5Level::foreignTalkHeads() const
{
    if (m_mainOffset < 40) return -1;
    int16_t v;
    std::memcpy(&v, m_bytes.data() + m_mainOffset - 8 - 32 + 4, 2);
    return v;
}

bool Tr5Level::setForeignTalkHeads(int value)
{
    if (m_mainOffset < 40) return false;
    const int16_t v = static_cast<int16_t>(value);
    std::memcpy(m_bytes.data() + m_mainOffset - 8 - 32 + 4, &v, 2);
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

    // Texturwerkstatt (#104): solange sie schmutzig unresolved ist, tragen die
    // Texturbloecke in m_bytes den ALTEN Stand. Lieber volumes scheitern als
    // still ein Level mit veralteten Seiten write (die TR3Setz-Lehre:
    // eine Warnung liest niemand). Patch::apply schliesst am Ende selbst.
    if (m_texOpen && m_texDirty)
    {
        error = "Texturwerkstatt noch unresolved - texWorkshopClose() vor dem Speichern rufen";
        return false;
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f)
    {
        error = "Zieldatei laesst sich nicht write";
        return false;
    }
    size_t put = std::fwrite(m_bytes.data(), 1, m_bytes.size(), f);
    std::fclose(f);
    if (put != m_bytes.size()) { error = "Zieldatei nicht vollstaendig geschrieben"; return false; }
    return true;
}
