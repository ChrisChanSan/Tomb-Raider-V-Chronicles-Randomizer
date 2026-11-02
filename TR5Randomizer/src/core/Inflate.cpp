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
#include "Inflate.h"

#include <cstring>

//----------------------------------------------------------------------------
// What the format says, and how this file follows it - see Inflate.h for
// what it reads and refuses.
//
// Bits are taken from the low end of each byte first. A Huffman code is
// packed from its most significant bit on, so a code read bit by bit
// arrives most significant bit first; every other field arrives least
// significant bit first.
//
// The codes are canonical: the lengths alone define them. Shorter codes
// come first, and within one length the symbols run in ascending order.
// Decoding takes a table of 2^FAST bits for the short codes (every code of
// a fixed block and nearly all of a dynamic one) and walks the lengths one
// bit at a time for the rest.
//----------------------------------------------------------------------------
namespace
{
    constexpr int MAXBITS = 15;      // longest code the format allows
    constexpr int FAST = 10;         // bits looked up at once
    constexpr int NLIT = 288;        // literal/length symbols incl. 286-287
    constexpr int NDIST = 32;        // distance symbols incl. 30-31

    // The length and distance bases are arithmetic, not data: within a
    // group of four (lengths) or two (distances) symbols the extra bits stay
    // the same, then grow by one, and each base is the previous base plus
    // the range of the previous symbol's extra bits. Lengths start at 3 and
    // symbol 285 is the lone exception, 258 with no extra bits; distances
    // start at 1 and the last one, 24577 + 8191, reaches exactly 32768.
    struct Tables
    {
        uint16_t lenBase[29];
        uint8_t  lenExtra[29];
        uint16_t distBase[30];
        uint8_t  distExtra[30];

        constexpr Tables() : lenBase(), lenExtra(), distBase(), distExtra()
        {
            int base = 3;
            for (int i = 0; i < 28; ++i)
            {
                lenExtra[i] = static_cast<uint8_t>(i < 8 ? 0 : (i - 4) / 4);
                lenBase[i] = static_cast<uint16_t>(base);
                base += 1 << lenExtra[i];
            }
            lenBase[28] = 258;
            lenExtra[28] = 0;
            base = 1;
            for (int i = 0; i < 30; ++i)
            {
                distExtra[i] = static_cast<uint8_t>(i < 4 ? 0 : (i - 2) / 2);
                distBase[i] = static_cast<uint16_t>(base);
                base += 1 << distExtra[i];
            }
        }
    };
    constexpr Tables kT;

    // The order in which a dynamic block lists the code length code's own
    // lengths (RFC 1951 3.2.7).
    constexpr uint8_t kOrder[19] =
        { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    struct Huff
    {
        uint16_t count[MAXBITS + 1];  // codes per length
        uint16_t symbol[NLIT];        // symbols by code order
        uint16_t fast[1 << FAST];     // symbol << 4 | length, 0 = slow path
    };

    // Builds a canonical code from n lengths. A code must not be
    // over-subscribed; an incomplete one is accepted only where the format
    // needs it - no code at all, or one single code of one bit (a block
    // with one distance) - and only when 'single' says the table may be so.
    const char* build(Huff& h, const uint8_t* len, int n, bool single)
    {
        std::memset(h.count, 0, sizeof(h.count));
        for (int s = 0; s < n; ++s) ++h.count[len[s]];
        h.count[0] = 0;

        int left = 1;
        int used = 0;
        for (int l = 1; l <= MAXBITS; ++l)
        {
            left <<= 1;
            left -= h.count[l];
            used += h.count[l];
            if (left < 0) return "over-subscribed code";
        }
        if (left > 0 && !(single && (used == 0 ||
                                     (used == 1 && h.count[1] == 1))))
            return "incomplete code";

        uint16_t offs[MAXBITS + 2];
        offs[1] = 0;
        for (int l = 1; l <= MAXBITS; ++l)
            offs[l + 1] = static_cast<uint16_t>(offs[l] + h.count[l]);

        // First code of each length (RFC 1951 3.2.2), for the fast table.
        uint32_t next[MAXBITS + 1];
        uint32_t code = 0;
        next[0] = 0;
        for (int l = 1; l <= MAXBITS; ++l)
        {
            code = (code + h.count[l - 1]) << 1;
            next[l] = code;
        }

        std::memset(h.fast, 0, sizeof(h.fast));
        for (int s = 0; s < n; ++s)
        {
            const int l = len[s];
            if (l == 0) continue;
            h.symbol[offs[l]++] = static_cast<uint16_t>(s);
            const uint32_t c = next[l]++;
            if (l > FAST) continue;
            // The code reversed, because the bit buffer holds the first
            // bit lowest; every index whose low l bits match takes it.
            uint32_t r = 0;
            for (int b = 0; b < l; ++b) r |= ((c >> b) & 1u) << (l - 1 - b);
            for (uint32_t k = r; k < (1u << FAST); k += 1u << l)
                h.fast[k] = static_cast<uint16_t>((s << 4) | l);
        }
        return nullptr;
    }

    struct Reader
    {
        const uint8_t* in;
        size_t inLen;
        size_t pos = 0;           // next input byte not yet in 'bits'
        uint64_t bits = 0;        // pending bits, first one lowest
        int count = 0;            // how many of them

        void fill()
        {
            while (count <= 56 && pos < inLen)
            {
                bits |= static_cast<uint64_t>(in[pos++]) << count;
                count += 8;
            }
        }

        // n <= 32. False when the input ends first.
        bool get(int n, uint32_t& v)
        {
            if (count < n) fill();
            if (count < n) return false;
            v = static_cast<uint32_t>(bits & ((1ull << n) - 1));
            bits >>= n;
            count -= n;
            return true;
        }

        void align()
        {
            bits >>= (count & 7);
            count &= ~7;
        }

        // Bytes of input taken so far, whole bytes still in 'bits' not
        // counted.
        size_t consumed() const { return pos - static_cast<size_t>(count / 8); }

        // One symbol; -1 when no code matches or the input ends.
        int decode(const Huff& h)
        {
            if (count < MAXBITS) fill();
            const uint16_t e = h.fast[bits & ((1u << FAST) - 1)];
            if (e != 0 && (e & 15) <= count)
            {
                bits >>= (e & 15);
                count -= (e & 15);
                return e >> 4;
            }
            int code = 0, first = 0, index = 0;
            for (int l = 1; l <= MAXBITS; ++l)
            {
                if (count == 0) return -1;
                code |= static_cast<int>(bits & 1);
                bits >>= 1;
                --count;
                const int c = h.count[l];
                if (code - first < c) return h.symbol[index + code - first];
                index += c;
                first = (first + c) << 1;
                code <<= 1;
            }
            return -1;
        }
    };

    // The body of one Huffman block, up to and including its end code.
    const char* codes(Reader& r, const Huff& lit, const Huff& dist,
                      uint8_t* out, size_t cap, size_t& at, size_t window)
    {
        for (;;)
        {
            const int sym = r.decode(lit);
            if (sym < 0) return "invalid literal/length code";
            if (sym < 256)
            {
                if (at >= cap) return "output larger than the room for it";
                out[at++] = static_cast<uint8_t>(sym);
                continue;
            }
            if (sym == 256) return nullptr;
            if (sym > 285) return "invalid literal/length code";

            uint32_t extra = 0;
            const int li = sym - 257;
            if (!r.get(kT.lenExtra[li], extra)) return "input ends early";
            const size_t length = kT.lenBase[li] + extra;

            const int ds = r.decode(dist);
            if (ds < 0 || ds > 29) return "invalid distance code";
            if (!r.get(kT.distExtra[ds], extra)) return "input ends early";
            const size_t distance = kT.distBase[ds] + extra;

            if (distance > at || distance > window)
                return "distance reaches back too far";
            if (length > cap - at) return "output larger than the room for it";

            uint8_t* d = out + at;
            const uint8_t* s = d - distance;
            if (distance >= length)
                std::memcpy(d, s, length);
            else
                for (size_t i = 0; i < length; ++i) d[i] = s[i];
            at += length;
        }
    }

    const char* stored(Reader& r, uint8_t* out, size_t cap, size_t& at)
    {
        r.align();
        uint32_t len = 0, nlen = 0;
        if (!r.get(16, len) || !r.get(16, nlen)) return "input ends early";
        if ((len ^ 0xFFFFu) != nlen) return "stored block length check fails";
        if (len > cap - at) return "output larger than the room for it";
        // Whole bytes already in the bit buffer first, then straight from
        // the input.
        while (len > 0 && r.count >= 8)
        {
            out[at++] = static_cast<uint8_t>(r.bits & 0xFF);
            r.bits >>= 8;
            r.count -= 8;
            --len;
        }
        if (len > r.inLen - r.pos) return "input ends early";
        std::memcpy(out + at, r.in + r.pos, len);
        r.pos += len;
        at += len;
        return nullptr;
    }

    const char* dynamicTables(Reader& r, Huff& lit, Huff& dist)
    {
        uint32_t hlit = 0, hdist = 0, hclen = 0;
        if (!r.get(5, hlit) || !r.get(5, hdist) || !r.get(4, hclen))
            return "input ends early";
        hlit += 257;
        hdist += 1;
        hclen += 4;
        if (hlit > 286 || hdist > 30) return "too many length or distance codes";

        uint8_t len[NLIT + NDIST];
        std::memset(len, 0, sizeof(len));
        for (uint32_t i = 0; i < hclen; ++i)
        {
            uint32_t v = 0;
            if (!r.get(3, v)) return "input ends early";
            len[kOrder[i]] = static_cast<uint8_t>(v);
        }
        Huff cl;
        if (const char* e = build(cl, len, 19, false))
            return e[0] == 'o' ? "over-subscribed code length code"
                               : "incomplete code length code";

        // The lengths of both codes, one run: 0-15 literally, 16 repeats
        // the previous length 3-6 times, 17 and 18 write 3-10 and 11-138
        // zeros. A repeat may cross from the one code into the other.
        const uint32_t total = hlit + hdist;
        uint32_t i = 0;
        std::memset(len, 0, sizeof(len));
        while (i < total)
        {
            const int sym = r.decode(cl);
            if (sym < 0) return "invalid code length code";
            if (sym < 16) { len[i++] = static_cast<uint8_t>(sym); continue; }
            uint32_t rep = 0, v = 0;
            uint8_t value = 0;
            if (sym == 16)
            {
                if (i == 0) return "repeat with no length before it";
                value = len[i - 1];
                if (!r.get(2, v)) return "input ends early";
                rep = 3 + v;
            }
            else if (sym == 17)
            {
                if (!r.get(3, v)) return "input ends early";
                rep = 3 + v;
            }
            else
            {
                if (!r.get(7, v)) return "input ends early";
                rep = 11 + v;
            }
            if (rep > total - i) return "repeat runs past the code lengths";
            while (rep--) len[i++] = value;
        }
        if (len[256] == 0) return "no end-of-block code";

        if (const char* e = build(lit, len, static_cast<int>(hlit), true))
            return e[0] == 'o' ? "over-subscribed literal/length code"
                               : "incomplete literal/length code";
        if (const char* e = build(dist, len + hlit, static_cast<int>(hdist), true))
            return e[0] == 'o' ? "over-subscribed distance code"
                               : "incomplete distance code";
        return nullptr;
    }

    // The fixed code of RFC 1951 3.2.6: literals 0-143 eight bits, 144-255
    // nine, 256-279 seven, 280-287 eight; all 30 distances (and the two
    // reserved ones) five bits.
    void fixedTables(Huff& lit, Huff& dist)
    {
        uint8_t len[NLIT];
        int s = 0;
        for (; s < 144; ++s) len[s] = 8;
        for (; s < 256; ++s) len[s] = 9;
        for (; s < 280; ++s) len[s] = 7;
        for (; s < NLIT; ++s) len[s] = 8;
        build(lit, len, NLIT, false);
        for (s = 0; s < NDIST; ++s) len[s] = 5;
        build(dist, len, NDIST, false);
    }
}

uint32_t Inflate::adler32(const uint8_t* p, size_t n, uint32_t a)
{
    // 65521 is the largest prime below 2^16. 5552 bytes is the longest run
    // after which s2 still fits 32 bits without a reduction:
    // 255 * n * (n + 1) / 2 + (n + 1) * 65520 < 2^32 holds up to 5552.
    uint32_t s1 = a & 0xFFFF, s2 = a >> 16;
    while (n > 0)
    {
        size_t k = n < 5552 ? n : 5552;
        n -= k;
        while (k--)
        {
            s1 += *p++;
            s2 += s1;
        }
        s1 %= 65521;
        s2 %= 65521;
    }
    return (s2 << 16) | s1;
}

namespace
{
    struct CrcTable
    {
        uint32_t t[256];
        constexpr CrcTable() : t()
        {
            for (uint32_t i = 0; i < 256; ++i)
            {
                uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
        }
    };
    constexpr CrcTable kCrc;
}

uint32_t Inflate::crc32(const uint8_t* p, size_t n, uint32_t c)
{
    c = ~c;
    while (n--) c = kCrc.t[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

const char* Inflate::zlib(const uint8_t* in, size_t inLen, uint8_t* out,
                          size_t outCap, size_t& outLen, size_t* used)
{
    outLen = 0;
    if (inLen < 2) return "input ends early";

    // RFC 1950: CMF (method 8 = deflate, window 2^(CINFO+8)), FLG (check
    // bits making CMF*256+FLG a multiple of 31, the preset dictionary bit).
    const uint32_t cmf = in[0], flg = in[1];
    if ((cmf & 15) != 8) return "not a deflate stream";
    if ((cmf >> 4) > 7) return "window larger than 32 KB";
    if ((cmf * 256 + flg) % 31 != 0) return "header check fails";
    if (flg & 0x20) return "preset dictionary not supported";
    const size_t window = static_cast<size_t>(1) << ((cmf >> 4) + 8);

    Reader r;
    r.in = in;
    r.inLen = inLen;
    r.pos = 2;

    Huff lit, dist;
    size_t at = 0;
    for (;;)
    {
        uint32_t last = 0, type = 0;
        if (!r.get(1, last) || !r.get(2, type)) { outLen = at; return "input ends early"; }

        const char* e = nullptr;
        if (type == 0)
            e = stored(r, out, outCap, at);
        else if (type == 1)
        {
            fixedTables(lit, dist);
            e = codes(r, lit, dist, out, outCap, at, window);
        }
        else if (type == 2)
        {
            e = dynamicTables(r, lit, dist);
            if (!e) e = codes(r, lit, dist, out, outCap, at, window);
        }
        else
            e = "invalid block type";

        outLen = at;
        if (e) return e;
        if (last) break;
    }

    // The trailer: Adler-32 of the output, most significant byte first.
    r.align();
    uint32_t want = 0;
    for (int i = 0; i < 4; ++i)
    {
        uint32_t b = 0;
        if (!r.get(8, b)) return "input ends early";
        want = (want << 8) | b;
    }
    if (adler32(out, at) != want) return "Adler-32 does not match";
    if (used) *used = r.consumed();
    return nullptr;
}

bool Inflate::zlibExact(const uint8_t* in, size_t inLen, size_t expected,
                        std::vector<uint8_t>& out, std::string& error)
{
    out.resize(expected);
    size_t got = 0;
    // A one-byte spare lets an over-long stream be told from an exact one.
    uint8_t spare = 0;
    const char* e = zlib(in, inLen, expected ? out.data() : &spare,
                         expected, got);
    if (e) { error = e; return false; }
    if (got != expected) { error = "fewer bytes than the header says"; return false; }
    return true;
}

size_t Inflate::storedSize(size_t n)
{
    const size_t blocks = n == 0 ? 1 : (n + 65534) / 65535;
    return 2 + n + 5 * blocks + 4;
}

size_t Inflate::storedTo(const uint8_t* raw, size_t n, uint8_t* out)
{
    uint8_t* o = out;
    *o++ = 0x78;              // deflate, 32 KB window
    *o++ = 0x01;              // check bits: 0x7801 = 31 * 991, no dictionary
    size_t p = 0;
    do
    {
        const size_t k = n - p < 65535 ? n - p : 65535;
        *o++ = p + k == n ? 1 : 0;            // final bit, type 00
        *o++ = static_cast<uint8_t>(k & 0xFF);
        *o++ = static_cast<uint8_t>(k >> 8);
        *o++ = static_cast<uint8_t>(~k & 0xFF);
        *o++ = static_cast<uint8_t>((~k >> 8) & 0xFF);
        if (k) std::memcpy(o, raw + p, k);
        o += k;
        p += k;
    } while (p < n);
    const uint32_t a = adler32(raw, n);
    for (int i = 3; i >= 0; --i) *o++ = static_cast<uint8_t>(a >> (8 * i));
    return static_cast<size_t>(o - out);
}

void Inflate::stored(const uint8_t* raw, size_t n, std::vector<uint8_t>& out)
{
    const size_t at = out.size();
    out.resize(at + storedSize(n));
    storedTo(raw, n, out.data() + at);
}
