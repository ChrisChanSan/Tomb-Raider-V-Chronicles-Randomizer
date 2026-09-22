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

// GUI language switch. The sources carry the ENGLISH original strings;
// with German selected, T() returns the translation from the table in
// Language.cpp. Unknown strings come back unchanged - a gap thus shows up
// in the picture immediately instead of breaking the program.
//
// The default is ENGLISH; the switch sits next to the Debug button and
// lands in TR5Randomizer.cfg.

extern bool gGermanUi;

const char* T(const char* en);
