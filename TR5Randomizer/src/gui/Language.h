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

// Sprache der Oberflaeche (#128). Die Quelltexte tragen die deutschen
// Original-Strings; T() liefert bei englischer Sprache die Uebersetzung aus
// der Tabelle in Language.cpp. Unbekannte Strings kommen unveraendert zurueck -
// eine Luecke faellt damit sofort im Bild auf, statt das Programm zu brechen.
//
// Vorgabe ist ENGLISCH (Nutzerentscheid 2026-08-24); der Schalter sitzt neben
// dem Debug-Knopf und landet in der TR5Randomizer.cfg.

extern bool gGermanUi;

const char* T(const char* de);
