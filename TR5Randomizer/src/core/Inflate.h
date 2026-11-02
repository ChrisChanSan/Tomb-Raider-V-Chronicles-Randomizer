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
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

//----------------------------------------------------------------------------
// The randomizer's own deflate reader and stored-block writer, in place of
// zlib. Written for this project from the two format descriptions, RFC 1950
// (the zlib wrapper) and RFC 1951 (deflate); no code of zlib is in here.
//
// READS one zlib stream: the two header bytes (method 8, window up to
// 32 KB, the check bits), then stored, fixed-Huffman and dynamic-Huffman
// blocks until the final one, then the Adler-32 trailer, which must match
// the bytes written. That is every stream a .TRC can hold - the original
// levels' texture blocks and the stored ones written below.
//
// REFUSES, with a short message and nothing read or written out of bounds:
// a header that is not deflate or fails its check bits, a preset
// dictionary, block type 3, a stored block whose length check fails, a code
// that is over-subscribed or incomplete (bar the single one-bit code the
// format allows), a missing end-of-block code, the length and distance
// symbols the format reserves, a distance reaching back further than the
// window or than the bytes written so far, more output than the caller's
// room, input that ends early, and a wrong Adler-32.
//
// No allocation beyond the caller's output, no platform call. The 32 KB
// window is the output itself: the whole block is unpacked into one buffer
// and a distance reads back inside it.
//
// WRITES stored blocks only (the "no compression" block type of RFC 1951)
// inside the same zlib wrapper. Every deflate reader takes them, the
// engine's zlib included; the bytes are the raw bytes plus five per 65535
// and six for the wrapper.
//----------------------------------------------------------------------------
namespace Inflate
{
    // Adler-32 (RFC 1950) of n bytes, continuing from 'a' (1 starts a sum).
    uint32_t adler32(const uint8_t* p, size_t n, uint32_t a = 1);

    // CRC-32 with the reflected polynomial 0xEDB88320, as a PNG chunk
    // carries it, continuing from 'c' (0 starts one). The randomizer does
    // not need it; the diagnostic tools' picture writers do.
    uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0);

    // Unpacks the zlib stream in[0..inLen) into out[0..outCap). Returns
    // nullptr on success, with outLen the bytes written and, when 'used' is
    // given, the bytes of input the stream took (trailer included). On
    // failure returns a short message; out then holds whatever came before.
    const char* zlib(const uint8_t* in, size_t inLen, uint8_t* out,
                     size_t outCap, size_t& outLen, size_t* used = nullptr);

    // The same, for a caller that knows the unpacked size: out is resized
    // to 'expected', and exactly that many bytes must come out.
    bool zlibExact(const uint8_t* in, size_t inLen, size_t expected,
                   std::vector<uint8_t>& out, std::string& error);

    // Appends raw[0..n) to 'out' as a zlib stream of stored blocks: header
    // 0x78 0x01, blocks of at most 65535 bytes with LEN and NLEN, the final
    // bit on the last one, the Adler-32 big-endian at the end.
    void stored(const uint8_t* raw, size_t n, std::vector<uint8_t>& out);

    // Length of the stream stored() writes for n raw bytes.
    size_t storedSize(size_t n);
}
