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
#include "ShareCode.h"
#include "BuildId.h"
#include "Rng.h"

#include <cstddef>
#include <cstdio>
#include <vector>

namespace
{
    const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    const char kFormat = 'A';
    const size_t kMaxSeedText = 63;   // the GUI's field holds 63 bytes

    // Crockford's digit value, with the look-alikes folded in; -1 if the
    // character is no digit at all.
    int digitValue(char ch)
    {
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (ch == 'O') ch = '0';
        if (ch == 'I' || ch == 'L') ch = '1';
        for (int i = 0; i < 32; ++i)
            if (kAlphabet[i] == ch) return i;
        return -1;
    }

    unsigned crc10(const std::vector<uint8_t>& bytes)
    {
        unsigned crc = 0;
        for (uint8_t b : bytes)
        {
            crc ^= static_cast<unsigned>(b) << 2;
            for (int k = 0; k < 8; ++k)
            {
                crc <<= 1;
                if (crc & 0x400u) crc ^= 0x633u;   // x^10 + polynomial 0x233
            }
        }
        return crc & 0x3FFu;
    }

    void putVarint(std::vector<uint8_t>& out, uint32_t v)
    {
        while (v >= 0x80u)
        {
            out.push_back(static_cast<uint8_t>((v & 0x7Fu) | 0x80u));
            v >>= 7;
        }
        out.push_back(static_cast<uint8_t>(v));
    }

    void putText(std::vector<uint8_t>& out, const std::string& s)
    {
        putVarint(out, static_cast<uint32_t>(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    }

    // Reads the payload front to back; any read past the end sets bad.
    struct Reader
    {
        const std::vector<uint8_t>& b;
        size_t pos = 0;
        bool bad = false;

        explicit Reader(const std::vector<uint8_t>& bytes) : b(bytes) {}

        uint8_t byte()
        {
            if (pos >= b.size()) { bad = true; return 0; }
            return b[pos++];
        }
        uint32_t varint()
        {
            uint32_t v = 0;
            for (int shift = 0; shift < 35; shift += 7)
            {
                const uint8_t x = byte();
                if (bad) return 0;
                v |= static_cast<uint32_t>(x & 0x7Fu) << shift;
                if (!(x & 0x80u)) return v;
            }
            bad = true;
            return 0;
        }
        std::string text()
        {
            const uint32_t n = varint();
            if (bad || n > b.size() - pos) { bad = true; return std::string(); }
            std::string s(b.begin() + static_cast<std::ptrdiff_t>(pos),
                          b.begin() + static_cast<std::ptrdiff_t>(pos + n));
            pos += n;
            return s;
        }
    };

    // A text that would break a line of TR5Randomizer.cfg.
    bool breaksLine(const std::string& s)
    {
        for (char ch : s)
            if (ch == '\r' || ch == '\n' || ch == '\0') return true;
        return false;
    }

    std::string buildText(int id)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0.%d", id);
        return buf;
    }
}

namespace ShareCode
{
    std::string make(const Config& c)
    {
        return makeForBuild(c, RANDO_BUILD_ID);
    }

    std::string makeForBuild(const Config& c, int buildId)
    {
        std::vector<uint8_t> p;
        uint8_t f1 = 0, f2 = 0;
        if (c.optOtherPickups)  f1 |= 0x01;
        if (c.optKeyItems)      f1 |= 0x02;
        if (c.optEnemies)       f1 |= 0x04;
        if (c.optEnemySwap)     f1 |= 0x08;
        if (c.optOutfits)       f1 |= 0x10;
        if (c.optOutfitsTr3)    f1 |= 0x20;
        if (c.secretRewards)    f1 |= 0x40;
        if (c.optMusicSwitch)   f1 |= 0x80;
        if (c.optMusicTrigger)  f2 |= 0x01;
        if (c.optLevelSections) f2 |= 0x02;
        if (c.optWeather)       f2 |= 0x04;
        if (c.balWeaponsRandom)     f2 |= 0x08;
        if (c.balEnemyHealthRandom) f2 |= 0x10;
        if (c.balEnemyDamageRandom) f2 |= 0x20;
        p.push_back(f1);
        p.push_back(f2);
        p.push_back(static_cast<uint8_t>(c.enemyCount));
        p.push_back(static_cast<uint8_t>(c.enemyPool));
        p.push_back(static_cast<uint8_t>(c.itemAmount));
        p.push_back(static_cast<uint8_t>(c.secrets));
        p.push_back(static_cast<uint8_t>(c.optLevelOrder));
        p.push_back(static_cast<uint8_t>(c.balWeapons));
        p.push_back(static_cast<uint8_t>(c.balEnemyHealth));
        p.push_back(static_cast<uint8_t>(c.balEnemyDamage));
        putText(p, c.seedText);

        putVarint(p, static_cast<uint32_t>(c.enemiesDisabled.size()));
        int prev = 0;
        for (int t : c.enemiesDisabled)
        {
            putVarint(p, static_cast<uint32_t>(t - prev));
            prev = t;
        }
        putText(p, c.secretAuthor);
        putVarint(p, static_cast<uint32_t>(c.outfitsDisabled.size()));
        for (const std::string& id : c.outfitsDisabled) putText(p, id);

        // The digits: payload bits from the top, zero bits after the end.
        std::string digits;
        uint32_t acc = 0;
        int bits = 0;
        for (uint8_t b : p)
        {
            acc = (acc << 8) | b;
            bits += 8;
            while (bits >= 5)
            {
                bits -= 5;
                digits.push_back(kAlphabet[(acc >> bits) & 31u]);
            }
            acc &= (1u << bits) - 1u;
        }
        if (bits > 0) digits.push_back(kAlphabet[(acc << (5 - bits)) & 31u]);

        std::vector<uint8_t> checked;
        checked.push_back(static_cast<uint8_t>(kFormat));
        checked.push_back(static_cast<uint8_t>(buildId));
        checked.insert(checked.end(), p.begin(), p.end());
        const unsigned crc = crc10(checked);

        std::string out = "TR5-";
        out.push_back(kFormat);
        out.push_back(kAlphabet[(buildId >> 5) & 31]);
        out.push_back(kAlphabet[buildId & 31]);
        for (size_t i = 0; i < digits.size(); i += 5)
        {
            out.push_back('-');
            out += digits.substr(i, 5);
        }
        out.push_back('-');
        out.push_back(kAlphabet[(crc >> 5) & 31u]);
        out.push_back(kAlphabet[crc & 31u]);
        return out;
    }

    bool apply(const std::string& code, Config& c, std::string& err)
    {
        // Upper case, without the separators.
        std::string s;
        for (char ch : code)
        {
            if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') continue;
            s.push_back((ch >= 'a' && ch <= 'z') ? static_cast<char>(ch - 'a' + 'A') : ch);
        }
        if (s.empty()) { err = "no code given"; return false; }
        if (s.compare(0, 3, "TR5") != 0)
        { err = "not a share code: it does not start with TR5-"; return false; }
        s.erase(0, 3);
        // Format letter, two build digits, at least one payload digit, two
        // check digits.
        if (s.size() < 6) { err = "the code is too short - part of it is missing"; return false; }

        const char format = s[0];
        std::vector<int> v;
        for (size_t i = 1; i < s.size(); ++i)
        {
            const int d = digitValue(s[i]);
            if (d < 0)
            {
                err = std::string("the character '") + s[i] + "' is not part of a code";
                return false;
            }
            v.push_back(d);
        }
        const int build = v[0] * 32 + v[1];
        const unsigned check = static_cast<unsigned>(v[v.size() - 2] * 32 + v[v.size() - 1]);
        const size_t nDigits = v.size() - 4;

        std::vector<uint8_t> p;
        uint32_t acc = 0;
        int bits = 0;
        for (size_t i = 0; i < nDigits; ++i)
        {
            acc = (acc << 5) | static_cast<uint32_t>(v[2 + i]);
            bits += 5;
            if (bits >= 8)
            {
                bits -= 8;
                p.push_back(static_cast<uint8_t>((acc >> bits) & 0xFFu));
                acc &= (1u << bits) - 1u;
            }
        }
        // A whole digit of padding, or padding bits that are not zero, is no
        // code this program writes.
        const bool padOk = bits < 5 && acc == 0;

        std::vector<uint8_t> checked;
        checked.push_back(static_cast<uint8_t>(format));
        checked.push_back(static_cast<uint8_t>(build & 0xFF));
        checked.insert(checked.end(), p.begin(), p.end());
        if (!padOk || build > 255 || crc10(checked) != check)
        {
            err = "the code does not check out - a character is mistyped or "
                  "missing";
            return false;
        }
        if (format != kFormat)
        {
            err = std::string("this code is of format ") + format +
                  "; this program reads format " + kFormat;
            return false;
        }
        if (build != RANDO_BUILD_ID)
        {
            err = "this code is from build " + buildText(build) + "; this is " +
                  buildText(RANDO_BUILD_ID) + " - settings may differ";
            return false;
        }

        Reader r(p);
        Config n = c;
        const uint8_t f1 = r.byte(), f2 = r.byte();
        n.optOtherPickups  = (f1 & 0x01) != 0;
        n.optKeyItems      = (f1 & 0x02) != 0;
        n.optEnemies       = (f1 & 0x04) != 0;
        n.optEnemySwap     = (f1 & 0x08) != 0;
        n.optOutfits       = (f1 & 0x10) != 0;
        n.optOutfitsTr3    = (f1 & 0x20) != 0;
        n.secretRewards    = (f1 & 0x40) != 0;
        n.optMusicSwitch   = (f1 & 0x80) != 0;
        n.optMusicTrigger  = (f2 & 0x01) != 0;
        n.optLevelSections = (f2 & 0x02) != 0;
        n.optWeather       = (f2 & 0x04) != 0;
        n.balWeaponsRandom     = (f2 & 0x08) != 0;
        n.balEnemyHealthRandom = (f2 & 0x10) != 0;
        n.balEnemyDamageRandom = (f2 & 0x20) != 0;
        n.enemyCount     = r.byte();
        n.enemyPool      = r.byte();
        n.itemAmount     = r.byte();
        n.secrets        = r.byte();
        n.optLevelOrder  = r.byte();
        n.balWeapons     = r.byte();
        n.balEnemyHealth = r.byte();
        n.balEnemyDamage = r.byte();
        n.seedText = r.text();

        n.enemiesDisabled.clear();
        const uint32_t kinds = r.varint();
        int prev = 0;
        for (uint32_t i = 0; i < kinds && !r.bad; ++i)
        {
            const uint32_t d = r.varint();
            if (d == 0 || d > 0xFFFFu) { r.bad = true; break; }
            prev += static_cast<int>(d);
            n.enemiesDisabled.insert(prev);
        }
        n.secretAuthor = r.text();
        n.outfitsDisabled.clear();
        const uint32_t outfits = r.varint();
        for (uint32_t i = 0; i < outfits && !r.bad; ++i)
        {
            const std::string id = r.text();
            if (id.empty() || id.find_first_of(", \r\n") != std::string::npos)
            { r.bad = true; break; }
            n.outfitsDisabled.insert(id);
        }

        const bool inRange =
            (f2 & 0xC0) == 0 &&
            n.enemyCount >= 1 && n.enemyCount <= 20 &&
            n.enemyPool <= 2 && n.itemAmount <= 2 && n.secrets <= 3 &&
            n.optLevelOrder <= 2 && n.balWeapons <= 200 &&
            n.balEnemyHealth >= 50 && n.balEnemyHealth <= 200 &&
            n.balEnemyDamage >= 50 && n.balEnemyDamage <= 200 &&
            n.seedText.size() <= kMaxSeedText &&
            !breaksLine(n.seedText) && !breaksLine(n.secretAuthor) &&
            n.outfitsDisabled.size() == outfits;
        if (r.bad || r.pos != p.size() || !inRange)
        {
            err = "the code checks out but its content cannot be read - it "
                  "was not written by this program";
            return false;
        }
        c = n;
        return true;
    }

    bool sameSettings(const Config& a, const Config& b)
    {
        return a.seedText == b.seedText &&
               a.optOtherPickups == b.optOtherPickups &&
               a.optKeyItems == b.optKeyItems &&
               a.optEnemies == b.optEnemies &&
               a.enemyCount == b.enemyCount &&
               a.optEnemySwap == b.optEnemySwap &&
               a.enemyPool == b.enemyPool &&
               a.enemiesDisabled == b.enemiesDisabled &&
               a.optOutfits == b.optOutfits &&
               a.optOutfitsTr3 == b.optOutfitsTr3 &&
               a.outfitsDisabled == b.outfitsDisabled &&
               a.itemAmount == b.itemAmount &&
               a.secrets == b.secrets &&
               a.secretRewards == b.secretRewards &&
               a.secretAuthor == b.secretAuthor &&
               a.optMusicSwitch == b.optMusicSwitch &&
               a.optMusicTrigger == b.optMusicTrigger &&
               a.optLevelSections == b.optLevelSections &&
               a.optLevelOrder == b.optLevelOrder &&
               a.optWeather == b.optWeather &&
               a.balWeapons == b.balWeapons &&
               a.balEnemyHealth == b.balEnemyHealth &&
               a.balEnemyDamage == b.balEnemyDamage &&
               a.balWeaponsRandom == b.balWeaponsRandom &&
               a.balEnemyHealthRandom == b.balEnemyHealthRandom &&
               a.balEnemyDamageRandom == b.balEnemyDamageRandom;
    }

    // The selection goes into the seed. Otherwise the same seed with
    // different checkboxes produces a different game, and a bug report
    // "seed X is unplayable" would not be reproducible - exactly what the
    // seed mechanism is supposed to prevent. That is also why the GUI shows
    // the effective value, not just the entered text.
    uint64_t effectiveSeed(const Config& c)
    {
        uint64_t flags = 0;
        if (c.optOtherPickups)    flags |= 1;
        if (c.optKeyItems)        flags |= 2;
        if (c.optEnemies)         flags |= 4;
        if (c.enemyPool == 1)     flags |= 8;    // cross mix
        if (c.optEnemySwap)       flags |= 16;
        if (c.optOutfits)         flags |= 32;
        if (c.optOutfitsTr3)      flags |= 128;
        if (c.enemyPool == 2)     flags |= 64;   // allsanity
        // The count belongs in the seed just like the checkboxes: with 3
        // and with 12 enemies it is not the same game.
        flags |= static_cast<uint64_t>(c.enemyCount & 0xFF) << 8;
        // The same for the item amount.
        flags |= static_cast<uint64_t>(c.itemAmount & 0x3) << 16;
        // Weather. Bits 0-7 are the checkboxes above, 8-15 the enemy
        // count, 16-17 the item amount - so 18 is the next free one.
        // Whoever adds an option next takes bit 19.
        if (c.optWeather) flags |= 1ull << 18;

        // FNV-1a over seed text and selection, so both enter equally - a
        // mere XOR would have produced similar results for neighboring
        // flags.
        uint64_t h = SeedText::parse(c.seedText);
        for (int i = 0; i < 8; ++i)
        {
            h ^= (flags >> (i * 8)) & 0xFF;
            h *= 0x100000001B3ull;
        }

        // The wardrobe belongs in the seed just as the checkboxes do: the
        // same seed with only the catsuit left on is not the same game as
        // with everything. A std::set walks in sorted order, so the hash
        // does not depend on the order things were clicked.
        for (const std::string& id : c.outfitsDisabled)
            for (size_t k = 0; k < id.size(); ++k)
            {
                h ^= static_cast<unsigned char>(id[k]);
                h *= 0x100000001B3ull;
            }
        return h;
    }
}
