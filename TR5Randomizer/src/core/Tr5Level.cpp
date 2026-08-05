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
    STEP("Floordata", i32(n));  STEP("Floordata", skipN(n, 2));

    STEP("Mesh-Daten", i32(n));   STEP("Mesh-Daten", skipN(n, 2));
    STEP("Mesh-Zeiger", i32(n));  STEP("Mesh-Zeiger", skipN(n, 4));
    STEP("Animationen", i32(n));  STEP("Animationen", skipN(n, SZ_ANIM));
    STEP("Zustandswechsel", i32(n)); STEP("Zustandswechsel", skipN(n, SZ_CHANGE));
    STEP("Bereiche", i32(n));     STEP("Bereiche", skipN(n, SZ_RANGE));
    STEP("Anim-Befehle", i32(n)); STEP("Anim-Befehle", skipN(n, 2));
    STEP("Knochen", i32(n));      STEP("Knochen", skipN(n, 4));
    STEP("Frames", i32(n));       STEP("Frames", skipN(n, 2));
    STEP("Objekte", i32(n));      STEP("Objekte", skipN(n, SZ_OBJECTINFO));
    STEP("Statics", i32(n));      STEP("Statics", skipN(n, SZ_STATICINFO));

    STEP("SPR-Marke", skipN(1, 4));
    STEP("Sprites", i32(n));      STEP("Sprites", skipN(n, SZ_SPRITE));
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
    STEP("Texturinfos", i32(n)); STEP("Texturinfos", skipN(n, SZ_TEXINFO));

    // Angekommen: hier steht die Item-Anzahl.
    m_itemCountOffset = p;
    STEP("Item-Anzahl", i32(m_itemCount));
    STEP("Item-Daten", skipN(m_itemCount, 24));
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
    if (m_itemCount >= MAX_ITEMS)
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
