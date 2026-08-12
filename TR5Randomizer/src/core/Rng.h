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
#pragma once
#include <cstdint>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// Deterministischer Zufallsgenerator fuer den Randomizer.
//
// Warum nicht std::mt19937 mit std::shuffle/std::uniform_int_distribution?
// Weil der Seed dann NICHT reproduzierbar waere, und genau das ist die
// Anforderung: meldet ein Nutzer einen Fehler zu Seed X, muss derselbe Seed
// hier exakt dasselbe Level erzeugen. std::mt19937 selbst ist zwar im Standard
// bitgenau festgelegt, aber std::uniform_int_distribution und std::shuffle
// sind es ausdruecklich NICHT - deren Verteilungs- bzw. Mischalgorithmus ist
// der Implementierung ueberlassen. Derselbe Seed liefert damit je nach
// Compiler, Standardbibliothek oder sogar deren Version ein anderes Ergebnis.
// Ein Nutzerbericht "Seed 12345 ist unspielbar" waere dann nicht nachstellbar,
// sobald der Build mit einem anderen Toolset erzeugt wurde.
//
// Deshalb: eigener Generator (xoshiro256** mit splitmix64-Initialisierung,
// beide Public Domain von Blackman/Vigna bzw. Steele et al.), eigene
// verzerrungsfreie Bereichsreduktion und eigenes Fisher-Yates. Alles hier ist
// exakt festgelegt, damit ein Seed ueber Compiler- und Versionsgrenzen hinweg
// dasselbe Ergebnis liefert.
//----------------------------------------------------------------------------
class Rng
{
public:
    explicit Rng(uint64_t seed) { reseed(seed); }

    void reseed(uint64_t seed);

    // Rohes 64-Bit-Wort.
    uint64_t next();

    // Gleichverteilt in [0, bound). bound == 0 liefert 0.
    // Verwendet Lemires verzerrungsfreies Verfahren mit Verwerfen - kein
    // "next() % bound", das haette bei nicht-Zweierpotenzen eine messbare
    // Schlagseite zu kleinen Werten.
    uint64_t below(uint64_t bound);

    // Fisher-Yates, absteigend. Bewusst hier ausgeschrieben statt std::shuffle.
    template <typename T>
    void shuffle(std::vector<T>& v)
    {
        if (v.size() < 2) return;
        for (size_t i = v.size() - 1; i > 0; --i)
        {
            size_t j = static_cast<size_t>(below(static_cast<uint64_t>(i) + 1));
            if (i != j)
            {
                T tmp = v[i];
                v[i] = v[j];
                v[j] = tmp;
            }
        }
    }

    // Ein Element auswaehlen. Der Aufrufer stellt sicher, dass v nicht leer ist.
    template <typename T>
    const T& pick(const std::vector<T>& v)
    {
        return v[static_cast<size_t>(below(static_cast<uint64_t>(v.size())))];
    }

private:
    uint64_t m_s[4];
};

//----------------------------------------------------------------------------
// Seed-Darstellung fuer die Oberflaeche.
//
// Ein Seed wird dem Nutzer als Text gezeigt und von ihm als Text wieder
// eingegeben - das ist der Weg, auf dem eine Fehlermeldung zurueckkommt. Rein
// numerisch waere das eine 20-stellige Zahl, die sich niemand fehlerfrei
// abtippt. Darum Base32 mit einem Alphabet ohne die Verwechslungspaare 0/O und
// 1/I/L: 13 Zeichen, gross-/kleinschreibungsunabhaengig lesbar.
//
// Jeder Seed-Text ist gueltig: laesst er sich nicht als Base32 lesen, wird er
// stattdessen als Zeichenkette gehasht. Damit kann ein Nutzer auch einfach
// "Weihnachten" eintippen und bekommt reproduzierbar dasselbe Level.
//----------------------------------------------------------------------------
namespace SeedText
{
    std::string  format(uint64_t seed);           // 64 Bit -> lesbarer Text
    uint64_t     parse(const std::string& text);  // Text -> 64 Bit
    uint64_t     random();                        // frischer Seed aus der Systemzeit
}
