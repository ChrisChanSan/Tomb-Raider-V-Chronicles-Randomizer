/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * The Steam discovery below is carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\fs\discovery.cpp) - the IV-VI
 * branch of it and nothing else. See THIRD-PARTY.md.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#pragma once

// WHERE THE HD ART COMES FROM (#123).
//
// Nothing of the remaster is shipped with this project and nothing is copied
// out of it - the user's decision for way (a) of the plan: find the installed
// "Tomb Raider IV-VI Remastered" and read it where it lies. The only thing
// this project stores is a path - two of them since stage S7-2, because the
// four Tomb Raider III guests the randomizer imports have their HD models in
// the OTHER bundle, "Tomb Raider I-III Remastered".
//
// Two sources, in this order:
//
//   1. The registry value  hd_install  under the game's own settings key
//      (HKCU\Software\<company>\tomb5, the same key every tomb5.* option
//      lives in). It may name the bundle folder itself - the one holding
//      tomb456.exe - or a folder containing it.
//   2. Steam. The Steam folder comes out of the registry (HKCU SteamPath,
//      HKLM InstallPath as the fallback), every further library out of
//      steamapps\libraryfolders.vdf, and each library's steamapps\common is
//      searched one level deep.
//
// Recognised by CONTENT, never by folder name: a tomb456.exe with a
// subfolder "5" that carries DATA and TEX. Steam names the same game
// differently per language and depot, and a folder somebody moved by hand is
// named whatever they liked. That rule and the code under it are TROne's.
//
// Everything here is plain C-shaped on purpose: the file that implements it
// is compiled as C++17 (<filesystem>), the engine around it is not.

// The path of the game 5 folder of the installed remaster - "<bundle>\5",
// with DATA, TEX and ITEM inside it. Empty string when nothing was found.
// Worked out once and remembered; call it as often as you like.
const char* HDInstallRoot();

/* The game 3 folder of the installed Tomb Raider I-III Remastered -
 * "<bundle>\3", with DATA, TEX and (normally) ITEM inside it. Empty string
 * when nothing was found, which is not a fault: it is needed for exactly one
 * thing, the HD models of the four Tomb Raider III guests the randomizer
 * imports (#92, T-Rex, vulture, Compsognathus, raptor), and without it those
 * four draw classic while everything else in the level is HD.
 *
 * Found the same way and in the SAME WALK as the IV-VI bundle: recognised by
 * content - a tomb123.exe with a subfolder "3" carrying DATA and TEX -
 * overridden by the registry value hd_install_tr3, and otherwise looked for
 * in every Steam library. TROne's discovery table names both bundles side by
 * side (host\fs\discovery.cpp), which is where the executable name comes
 * from. */
const char* HDInstallRootTr3();

// Why the search ended the way it did: which folders were looked at, what was
// rejected and for what reason. Never null, possibly empty. For the log.
const char* HDInstallDiagnostics();

// Forgets the answer, so the next HDInstallRoot() searches again. Only the
// setup dialog and the tests need this.
void HDInstallForget();

// The path stored in the registry, or an empty string. Public so the setup
// dialog can show it.
const char* HDInstallOverride();
// The same for the I-III bundle: registry value hd_install_tr3.
const char* HDInstallOverrideTr3();
