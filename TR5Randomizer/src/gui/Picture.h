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
#include <vector>

// Reads a picture file (JPEG, PNG, any format Windows can decode) through
// the Windows Imaging Component, a system interface every Windows carries:
// no decoder of foreign code is compiled in. The result is 8-bit RGBA, rows
// top-down, width * 4 bytes per row, alpha 255 where the file has none -
// exactly what DXGI_FORMAT_R8G8B8A8_UNORM takes.
//
// COM must be initialised on the calling thread (the GUI does it first
// thing in wWinMain). False on any failure - a missing file, an unreadable
// one, one that is no picture, a size over 16384 - and then the three
// outputs are left as they were.
bool loadPictureRgba(const std::wstring& path,
                     std::vector<unsigned char>& rgba,
                     int& width, int& height);
