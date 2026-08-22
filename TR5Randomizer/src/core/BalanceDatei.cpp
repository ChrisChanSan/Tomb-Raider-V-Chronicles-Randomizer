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
#include "BalanceDatei.h"
#include "DataFolder.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace
{
    struct Zeile
    {
        const char* name;
        int BalanceDatei::Werte::* feld;
        const char* notiz;
    };

    // Reihenfolge wie in der Vorlage der Engine, damit eine neu geschriebene
    // Datei genauso aussieht wie eine vom Spiel erzeugte.
    const Zeile kZeilen[] =
    {
        { "WaffenSchaden", &BalanceDatei::Werte::waffenSchaden,
          "  auf alle Waffen Laras. 0 macht sie wirkungslos" },
        { "GegnerLeben",   &BalanceDatei::Werte::gegnerLeben,
          "  50 = halb so zaeh, 200 = doppelt so zaeh" },
        { "GegnerSchaden", &BalanceDatei::Werte::gegnerSchaden,
          "  0 macht alle Gegner harmlos" },
    };

    std::wstring dateiPfad(const std::wstring& spielOrdner)
    {
        return PathUtil::join(spielOrdner, L"Balance.txt");
    }

    bool lesenRoh(const std::wstring& pfad, std::string& inhalt)
    {
        FILE* f = _wfopen(pfad.c_str(), L"rb");
        if (!f) return false;

        char puffer[4096];
        size_t n;
        inhalt.clear();
        while ((n = std::fread(puffer, 1, sizeof(puffer), f)) > 0)
            inhalt.append(puffer, n);

        std::fclose(f);
        return true;
    }

    // Zeigt die Zeile auf diese Einstellung? Vor dem = darf Luft stehen,
    // Gross/Kleinschreibung ist egal - genauso liest die Engine.
    bool zeileGehoert(const std::string& zeile, const char* name)
    {
        size_t i = zeile.find_first_not_of(" \t");
        if (i == std::string::npos) return false;
        if (zeile[i] == ';' || zeile[i] == '#') return false;

        const size_t len = std::strlen(name);
        if (zeile.size() - i < len) return false;

        for (size_t k = 0; k < len; k++)
        {
            char a = zeile[i + k], b = name[k];
            if (a >= 'A' && a <= 'Z') a = char(a + 32);
            if (b >= 'A' && b <= 'Z') b = char(b + 32);
            if (a != b) return false;
        }

        size_t j = zeile.find_first_not_of(" \t", i + len);
        return j != std::string::npos && zeile[j] == '=';
    }

    std::string baueZeile(const Zeile& z, int wert)
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%-22s = %-6d ; (Original 100)%s",
                      z.name, wert, z.notiz);
        return buf;
    }
}

namespace BalanceDatei
{

bool schreiben(const std::wstring& spielOrdner, const Werte& w, std::string* fehlerText)
{
    if (spielOrdner.empty())
    {
        if (fehlerText) *fehlerText = "Kein Spielordner gewaehlt.";
        return false;
    }

    const std::wstring pfad = dateiPfad(spielOrdner);

    std::string alt;
    const bool hatDatei = lesenRoh(pfad, alt);

    // In Zeilen zerlegen, Zeilenende je Zeile merken. Die Datei der Engine ist
    // CRLF; wer sie in einem anderen Editor speichert, kann LF hinterlassen -
    // beides bleibt erhalten, damit unser Schreiben keine Spuren hinterlaesst.
    std::vector<std::string> zeilen;
    std::vector<std::string> enden;

    for (size_t i = 0; i < alt.size();)
    {
        size_t e = i;
        while (e < alt.size() && alt[e] != '\r' && alt[e] != '\n') e++;

        zeilen.push_back(alt.substr(i, e - i));

        std::string ende;
        if (e < alt.size() && alt[e] == '\r') { ende += '\r'; e++; }
        if (e < alt.size() && alt[e] == '\n') { ende += '\n'; e++; }
        enden.push_back(ende);

        i = e;
    }

    const char* umbruch = "\r\n";
    for (size_t i = 0; i < enden.size(); i++)
        if (enden[i] == "\n") { umbruch = "\n"; break; }

    for (const Zeile& z : kZeilen)
    {
        const std::string neu = baueZeile(z, w.*(z.feld));

        bool ersetzt = false;
        for (size_t i = 0; i < zeilen.size(); i++)
        {
            if (!zeileGehoert(zeilen[i], z.name)) continue;
            zeilen[i] = neu;
            ersetzt = true;
            break;      // die Engine nimmt ebenfalls den ersten Treffer
        }

        if (ersetzt) continue;

        // Fehlt die Zeile, kommt sie ans Ende. Beim naechsten Start sortiert
        // das Spiel die Datei ohnehin wieder in ihre gewohnte Form.
        if (!zeilen.empty() && enden.back().empty())
            enden.back() = umbruch;

        zeilen.push_back(neu);
        enden.push_back(umbruch);
    }

    // Eine ganz neue Datei bekommt einen Kopf, damit klar ist, woher sie
    // kommt und dass das Spiel den Rest nachtraegt.
    std::string aus;
    if (!hatDatei)
    {
        aus += "; Balance.txt - vom TR5 Randomizer angelegt.";
        aus += umbruch;
        aus += "; Beim naechsten Start traegt das Spiel hier alle weiteren";
        aus += umbruch;
        aus += "; Einstellungen nach: Schaden je Waffe, Lebenspunkte je Gegner,";
        aus += umbruch;
        aus += "; Schaden je Angriff, Levelgefahren.";
        aus += umbruch;
        aus += umbruch;
    }

    for (size_t i = 0; i < zeilen.size(); i++)
    {
        aus += zeilen[i];
        aus += enden[i];
    }

    FILE* f = _wfopen(pfad.c_str(), L"wb");
    if (!f)
    {
        if (fehlerText) *fehlerText = "Balance.txt laesst sich nicht schreiben.";
        return false;
    }

    const bool ok = aus.empty() || std::fwrite(aus.data(), 1, aus.size(), f) == aus.size();
    std::fclose(f);

    if (!ok && fehlerText) *fehlerText = "Balance.txt konnte nicht vollstaendig geschrieben werden.";
    return ok;
}

Werte lesen(const std::wstring& spielOrdner)
{
    Werte w;
    if (spielOrdner.empty()) return w;

    std::string inhalt;
    if (!lesenRoh(dateiPfad(spielOrdner), inhalt)) return w;

    for (size_t i = 0; i < inhalt.size();)
    {
        size_t e = i;
        while (e < inhalt.size() && inhalt[e] != '\r' && inhalt[e] != '\n') e++;
        const std::string zeile = inhalt.substr(i, e - i);
        while (e < inhalt.size() && (inhalt[e] == '\r' || inhalt[e] == '\n')) e++;
        i = e;

        for (const Zeile& z : kZeilen)
        {
            if (!zeileGehoert(zeile, z.name)) continue;

            const size_t g = zeile.find('=');
            if (g == std::string::npos) break;

            w.*(z.feld) = std::atoi(zeile.c_str() + g + 1);
            break;
        }
    }

    return w;
}

}
