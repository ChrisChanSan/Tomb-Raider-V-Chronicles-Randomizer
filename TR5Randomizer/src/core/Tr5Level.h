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

private:
    std::vector<uint8_t> m_bytes;
    int32_t              m_version = 0;
    int16_t              m_rtPages = 0, m_otPages = 0, m_btPages = 0;
    size_t               m_mainOffset = 0;   // Beginn der Hauptblock-Nutzdaten
    size_t               m_mainSize = 0;

    // Leseposition beim Kopfteil.
    bool readHeader(std::string& error);
};
