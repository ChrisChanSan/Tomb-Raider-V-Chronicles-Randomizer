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
#include <string>

//----------------------------------------------------------------------------
// Balance.txt - the engine's tuning file that lives next to PCtomb5.exe.
//
// The randomizer touches EXCLUSIVELY the three percentage lines that its
// sliders drive. Everything else - per-weapon damage, per-enemy hit points,
// per-attack damage, the level hazards - stays untouched, so that the
// player's hand tuning survives a round of shuffling. That is exactly what
// the percentage lines in the engine are for: they apply on top of the
// individual values.
//
// If the file is missing, a short one with just these three lines is
// written; the game fills in everything else on its own next start.
//----------------------------------------------------------------------------
namespace BalanceFile
{
    // All three values in percent. 100 = unchanged.
    struct Values
    {
        int weaponDamage = 100;
        int enemyHealth   = 100;
        int enemyDamage = 100;
    };

    // Set the three lines in <gameDir>\Balance.txt. Existing lines are
    // replaced in place, missing ones appended at the end, and the rest of
    // the file stays character for character as it was.
    //
    // Returns false only on a genuine write error; errorText then carries
    // the cause.
    bool write(const std::wstring& gameDir, const Values& w,
                   std::string* errorText = nullptr);

    // What the file currently says. If it or a line in it is missing, 100
    // applies - the same assumption the engine makes.
    Values read(const std::wstring& gameDir);
}
