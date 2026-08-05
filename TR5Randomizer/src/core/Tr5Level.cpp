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

    return readHeader(error);
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
