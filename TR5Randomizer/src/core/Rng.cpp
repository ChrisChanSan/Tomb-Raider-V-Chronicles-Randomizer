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
#include "Rng.h"

#include <chrono>
#include <cstring>

namespace
{
    // splitmix64 - streut einen einzelnen 64-Bit-Seed auf den 256-Bit-Zustand
    // von xoshiro. Ohne diesen Schritt liefert ein Seed wie 1 oder 0 eine
    // erkennbar schlechte erste Zahlenfolge, weil xoshiro einen gut verteilten
    // Anfangszustand voraussetzt.
    uint64_t splitmix64(uint64_t& x)
    {
        x += 0x9E3779B97F4A7C15ull;
        uint64_t z = x;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    inline uint64_t rotl(uint64_t x, int k)
    {
        return (x << k) | (x >> (64 - k));
    }
}

void Rng::reseed(uint64_t seed)
{
    uint64_t x = seed;
    for (int i = 0; i < 4; ++i)
        m_s[i] = splitmix64(x);
}

uint64_t Rng::next()
{
    // xoshiro256**
    const uint64_t result = rotl(m_s[1] * 5, 7) * 9;
    const uint64_t t = m_s[1] << 17;

    m_s[2] ^= m_s[0];
    m_s[3] ^= m_s[1];
    m_s[1] ^= m_s[2];
    m_s[0] ^= m_s[3];
    m_s[2] ^= t;
    m_s[3] = rotl(m_s[3], 45);

    return result;
}

uint64_t Rng::below(uint64_t bound)
{
    if (bound == 0) return 0;

    // Lemire: multipliziere in die doppelte Breite und verwirf den unteren
    // Randbereich, der die Gleichverteilung verletzen wuerde. Das Verwerfen
    // greift so selten, dass die Schleife praktisch nie zweimal laeuft - aber
    // sie muss da sein, sonst waeren kleine Values bevorzugt.
#if defined(_MSC_VER) && defined(_M_X64)
    unsigned __int64 hi;
    uint64_t lo = _umul128(next(), bound, &hi);
    if (lo < bound)
    {
        uint64_t threshold = (0ull - bound) % bound;
        while (lo < threshold)
        {
            lo = _umul128(next(), bound, &hi);
        }
    }
    return hi;
#else
    // Portabler Weg ohne 128-Bit-Multiplikation: Ablehnungsverfahren auf
    // Basis der groessten durch bound teilbaren Schranke. Liefert dieselbe
    // Verteilung, aber NICHT dieselbe Zahlenfolge wie der Zweig oben - der
    // Build muss deshalb bei einer Zielplattform bleiben, sonst waeren Seeds
    // zwischen 32- und 64-Bit-Build nicht vergleichbar. Wir liefern x86.
    const uint64_t limit = 0xFFFFFFFFFFFFFFFFull - (0xFFFFFFFFFFFFFFFFull % bound) - 1;
    uint64_t r;
    do { r = next(); } while (r > limit);
    return r % bound;
#endif
}

//----------------------------------------------------------------------------

namespace
{
    // Ohne 0/O und 1/I/L - die Zeichen, die beim Abtippen aus einem Screenshot
    // oder einer Chatnachricht regelmaessig verwechselt werden.
    const char* SEED_ALPHABET = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";  // 31 Zeichen
    const int   SEED_BASE = 31;
    const int   SEED_LEN = 13;   // 31^13 > 2^64
}

std::string SeedText::format(uint64_t seed)
{
    std::string out(SEED_LEN, SEED_ALPHABET[0]);
    uint64_t v = seed;
    for (int i = SEED_LEN - 1; i >= 0; --i)
    {
        out[i] = SEED_ALPHABET[v % SEED_BASE];
        v /= SEED_BASE;
    }
    return out;
}

uint64_t SeedText::parse(const std::string& text)
{
    // Erst versuchen, den Text als unser Base31-Format zu read. Trennzeichen
    // und Leerraum werden ignoriert, Kleinschreibung akzeptiert.
    std::string clean;
    bool decodable = true;
    for (char c : text)
    {
        if (c == ' ' || c == '-' || c == '_' || c == '\t') continue;
        char u = (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
        const char* p = std::strchr(SEED_ALPHABET, u);
        if (!p || u == '\0') { decodable = false; break; }
        clean.push_back(u);
    }

    if (decodable && !clean.empty() && clean.size() <= SEED_LEN)
    {
        uint64_t v = 0;
        for (char c : clean)
        {
            const char* p = std::strchr(SEED_ALPHABET, c);
            v = v * SEED_BASE + static_cast<uint64_t>(p - SEED_ALPHABET);
        }
        return v;
    }

    // Sonst: den Text als Zeichenkette hashen (FNV-1a, 64 Bit), damit jede
    // Eingabe zu einem gueltigen und reproduzierbaren Seed fuehrt.
    uint64_t h = 0xCBF29CE484222325ull;
    for (unsigned char c : text)
    {
        h ^= c;
        h *= 0x100000001B3ull;
    }
    return h;
}

uint64_t SeedText::random()
{
    // Nur fuer den Wuerfeln-Knopf. Der erzeugte Wert wird sofort als Text
    // angezeigt und ist damit ab da genauso reproduzierbar wie jeder andere.
    const uint64_t t = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    uint64_t x = t ^ 0xA5A5A5A5DEADBEEFull;
    return splitmix64(x);
}
