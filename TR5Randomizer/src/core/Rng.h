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
// Deterministic random number generator for the randomizer.
//
// Why not std::mt19937 with std::shuffle/std::uniform_int_distribution?
// Because the seed would then NOT be reproducible, and that is precisely the
// requirement: if a user reports a bug for seed X, the same seed must
// produce exactly the same level here. std::mt19937 itself is specified
// bit-exactly by the standard, but std::uniform_int_distribution and
// std::shuffle explicitly are NOT - their distribution and shuffling
// algorithms are left to the implementation. The same seed thus yields a
// different result depending on compiler, standard library or even its
// version. A user report "seed 12345 is unwinnable" would then not be
// reproducible as soon as the build was made with a different toolset.
//
// Hence: our own generator (xoshiro256** with splitmix64 initialization,
// both Public Domain by Blackman/Vigna and Steele et al.), our own bias-free
// range reduction and our own Fisher-Yates. Everything here is pinned down
// exactly, so one seed gives the same result across compiler and version
// boundaries.
//----------------------------------------------------------------------------
class Rng
{
public:
    explicit Rng(uint64_t seed) { reseed(seed); }

    void reseed(uint64_t seed);

    // Raw 64-bit word.
    uint64_t next();

    // Uniform in [0, bound). bound == 0 yields 0.
    // Uses Lemire's bias-free rejection method - no "next() % bound", which
    // would have a measurable lean towards small values for non-powers of
    // two.
    uint64_t below(uint64_t bound);

    // Fisher-Yates, descending. Deliberately spelled out here instead of
    // std::shuffle.
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

    // Pick one element. The caller ensures v is not empty.
    template <typename T>
    const T& pick(const std::vector<T>& v)
    {
        return v[static_cast<size_t>(below(static_cast<uint64_t>(v.size())))];
    }

private:
    uint64_t m_s[4];
};

//----------------------------------------------------------------------------
// Seed representation for the UI.
//
// A seed is shown to the user as text and entered by them as text - that is
// the path a bug report travels back on. Purely numeric it would be a
// 20-digit number nobody can retype without mistakes. Hence Base32 with an
// alphabet that avoids the look-alike pairs 0/O and 1/I/L: 13 characters,
// readable regardless of case.
//
// Every seed text is valid: if it cannot be read as Base32, it is hashed as
// a string instead. That way a user can simply type "Christmas" and
// reproducibly gets the same level.
//----------------------------------------------------------------------------
namespace SeedText
{
    std::string  format(uint64_t seed);           // 64 bits -> readable text
    uint64_t     parse(const std::string& text);  // text -> 64 bits
    uint64_t     random();                        // fresh seed from system time
}
