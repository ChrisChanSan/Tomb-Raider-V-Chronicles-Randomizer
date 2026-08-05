#pragma once
#include <cstdint>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Eigener Leser und Schreiber fuer TR5-Leveldateien (.trc).
//
// Warum eigener Code und nicht der vorhandene aus TombItemEdit2026: dessen
// TR5-Parser (TRFile.cpp) stammt aus TRViewer von Popov Inc. und traegt keine
// Lizenzangabe. Unsere eigenen Ergaenzungen dort setzen darauf auf, lassen sich
// also nicht herausloesen. Fuer ein Werkzeug, das weitergegeben werden soll,
// ist das nicht tragbar - deshalb hier von Grund auf neu.
//
// Formatquelle: LoadLevel() und LoadTextures() in specific/file.cpp der Engine,
// die diesem Paket ohnehin im Quelltext beiliegt und unter derselben Lizenz
// steht (GPLv3). Die Engine ist die autoritative Beschreibung - sie liest diese
// Dateien seit 2000 korrekt.
//
// Dateiaufbau, aus LoadLevel/LoadTextures abgeleitet:
//
//   int32  version
//   int16  RTPages, OTPages, BTPages        Anzahl Texturseiten
//   -- Texturblock 1 (32 Bit) --
//   int32  size, compressedSize
//   byte[] Nutzdaten                        compressedSize bzw. size Bytes
//   -- Texturblock 2 (16 Bit) --
//   int32  size, compressedSize
//   byte[] Nutzdaten
//   int16  data[16]                         32 Bytes, u.a. LaraDrawType/Weather
//   -- Hauptblock --
//   int32  size, compressedSize
//   byte[] size Bytes                       UNKOMPRIMIERT, wortwoertlich
//
// Der Hauptblock ist der interessante Teil: darin liegen nacheinander Raeume,
// Objekte, Sprites, Kameras, Sounds, Boxen, animierte Texturen und
// Texturinfos, dann ein int32 mit der Item-Anzahl und danach die Items zu je
// 24 Bytes. Dass er unkomprimiert und wortwoertlich in der Datei steht, ist
// der Grund, warum sich Items ueberhaupt an Ort und Stelle aendern lassen,
// ohne die ganze Datei neu zu schreiben - compressedSize wird fuer diesen
// Block gelesen, aber nie benutzt.
//----------------------------------------------------------------------------

struct Tr5Item
{
    int16_t objectNumber = 0;
    int16_t roomNumber = 0;
    int32_t x = 0, y = 0, z = 0;
    int16_t yRot = 0;
    int16_t shade = 0;
    int16_t triggerFlags = 0;   // OCB in den unteren 6 Bit, siehe Randomizer.h
    int16_t flags = 0;
};

//----------------------------------------------------------------------------
// Ein Raum, soweit wir ihn brauchen. Der Raum-Blob wird nicht mehr am Stueck
// uebersprungen, sondern aufgeschluesselt - Portale, Nachbarn und Flags sind
// die Grundlage dafuer, einen wirklich abgeschiedenen Raum zu finden.
//----------------------------------------------------------------------------
struct Tr5Room
{
    int   index = 0;
    int   originX = 0, originZ = 0, yBottom = 0;
    int   numXSectors = 0, numZSectors = 0;
    int   altRoom = -1;                 // Flipmap-Partner, -1 = keiner
    unsigned flags = 0;                 // Bit 0x1 = unter Wasser
    std::vector<int> portalRooms;       // Zielraeume der Portalliste

    bool isAlternate() const { return altRoom >= 0; }
};

class Tr5Level
{
public:
    // Liest die Datei vollstaendig in den Speicher und ermittelt die Lage des
    // Hauptblocks. Bei Fehlschlag steht der Grund in error.
    bool load(const std::wstring& path, std::string& error);

    // Schreibt die - moeglicherweise geaenderte - Datei. Bewusst als eigener
    // Zielpfad: die Quelldatei bleibt unangetastet, bis der Aufrufer sie
    // ersetzen will.
    bool save(const std::wstring& path, std::string& error) const;

    int32_t version() const { return m_version; }
    size_t  fileSize() const { return m_bytes.size(); }

    // Absolute Position und Groesse des Hauptblocks in der Datei.
    size_t  mainBlockOffset() const { return m_mainOffset; }
    size_t  mainBlockSize() const { return m_mainSize; }

    // --- Items ---------------------------------------------------------
    // Absolute Position des int32 mit der Item-Anzahl; direkt dahinter
    // liegen die Items zu je 24 Byte.
    size_t  itemCountOffset() const { return m_itemCountOffset; }
    int32_t itemCount() const { return m_itemCount; }
    int     roomCount() const { return m_roomCount; }

    const std::vector<Tr5Room>& rooms() const { return m_rooms; }

    // Zaehlt, in wie vielen Raeumen die Portalliste NICHT aufging. Muss 0
    // sein - siehe die Rechnung in parseRoom().
    int     roomParseMismatches() const { return m_roomMismatch; }

    // Liest ein Item. index muss < itemCount() sein.
    bool    getItem(int index, Tr5Item& out) const;

    // Schreibt ein Item an derselben Stelle zurueck. Aendert nur die 24 Byte
    // dieses Datensatzes - Dateigroesse und alle anderen Offsets bleiben
    // unberuehrt, weshalb Aendern gefahrlos ist und Entfernen nicht.
    bool    setItem(int index, const Tr5Item& in);

    // Haengt ein Item hinten an. Das ist der einzige Weg, Items hinzuzufuegen:
    // anhaengen verschiebt keine bestehenden Indizes, und die "Object N"-
    // Verweise der Trigger bleiben damit gueltig. Ein Entfernen gibt es hier
    // bewusst NICHT - es wuerde alle nachfolgenden Indizes verschieben.
    bool    addItem(const Tr5Item& in, std::string& error);

    // Die Engine haelt items[] als festes Feld dieser Groesse (MAX_ITEMS in
    // global/types.h). Mehr Items laedt sie nicht - rich2 liegt mit 248 schon
    // dicht darunter, deshalb wird die Grenze hier geprueft und nicht gehofft.
    static constexpr int MAX_ITEMS = 256;

private:
    std::vector<uint8_t> m_bytes;
    int32_t              m_version = 0;
    int16_t              m_rtPages = 0, m_otPages = 0, m_btPages = 0;
    size_t               m_mainOffset = 0;   // Beginn der Hauptblock-Nutzdaten
    size_t               m_mainSize = 0;
    size_t               m_itemCountOffset = 0;
    int32_t              m_itemCount = 0;
    int                  m_roomCount = 0;
    std::vector<Tr5Room> m_rooms;
    int                  m_roomMismatch = 0;

    // Schluesselt einen Raum-Blob auf. offset/size zeigen auf den Blob im
    // Hauptblock, index ist die laufende Raumnummer.
    void parseRoom(size_t offset, size_t size, int index);

    bool readHeader(std::string& error);
    bool findItems(std::string& error);   // laeuft den Hauptblock ab
};
