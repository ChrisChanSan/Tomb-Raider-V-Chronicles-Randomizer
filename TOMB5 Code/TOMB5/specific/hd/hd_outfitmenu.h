/*
 * Modified 2026 by ChrisChanSan.
 *
 * Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
 * TR5 Randomizer project. The individual changes are recorded in the git
 * history of this source tree.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#pragma once

// THE OUTFIT CHANGER (#153, round M3).
//
// F4 in the game opens a paused menu of every outfit this machine can dress
// Lara in - the wardrobe scan of round M2, which is the installs' bodies and
// whatever the player's Mods folder adds - and the choice takes effect at once
// and is remembered for every level afterwards.
//
// WHOSE CHOICE IT IS. Round M2 briefly let the RANDOMIZER roll an outfit and
// wrote the key into the .hdm sidecar. That was withdrawn whole: a seed and its
// settings travel from one player to another and an installation does not, so a
// run rolled here could not be played there. An extra outfit is the PLAYER'S
// choice, it lives in his registry, and this is where he makes it.
//
// IT IS THE PAUSE MENU'S TWIN and deliberately so - CreateMonoScreen freezes
// the picture, S_UpdateInput with SetDebounce drives it, dbinput's IN_FORWARD /
// IN_BACK / IN_LEFT / IN_RIGHT / IN_SELECT / IN_DESELECT are the only inputs,
// SFX_MENU_CHOOSE and SFX_MENU_SELECT are the sounds, PrintString and
// font_height draw it and InventoryActive is held for the whole of it so that
// the press which closes the menu cannot also roll Lara (#115). A player who
// knows the pause menu knows this one, and a gamepad works in it because the
// bits it reads are the ones the pad already drives.
//
// WHAT IT IS NOT. It is not in the title screen, not in a cutscene, not while
// the binoculars or the laser sight are up and not inside the inventory or the
// pause menu - the caller in game\control.cpp asks all of that. And with
// hd_enable off, or with no installation found, F4 does nothing at all beyond
// one line in the log the first time it is pressed: everything this menu can
// change lives inside the HD path.

// Has the player just pressed F4, and may the menu open? Reads the key on its
// own edge, so holding it down opens the menu once. Call once per frame.
bool HDOutfitMenuWanted();

/* Opens it. Runs its own frame loop exactly as S_PauseMenu does and returns
 * when the player closes it. Everything it changes - the outfit, the
 * footsteps, the registry - is done before it returns. */
void HDOutfitMenu();

/* WHAT LARA WALKS ON IN THE LEVEL THAT HAS JUST BEEN BUILT (#153, round M3).
 * Asks which outfit she ended up in, what the wardrobe says about its feet
 * and what the player has said about that outfit, and bends the level's
 * sound map accordingly (hd_feet.h). One log line. Called at the end of
 * every HD level build and of every in-level re-dress. */
void HDOutfitMenuFollowFeet();

/* The footsteps choice for one wardrobe key, out of the registry, or -1 when
 * the player has never said. Read at every level build beside the outfit
 * itself; the level then bends its sound map (hd_feet.h). */
int HDOutfitMenuFeetFor(const char* key);

/* What the level build should do: true for bare feet. `why` gets a clause for
 * the log - the player's own choice, or the wardrobe's, or the default. */
bool HDOutfitMenuFeetWanted(const char* key, int wardrobeFeet, const char** why);
