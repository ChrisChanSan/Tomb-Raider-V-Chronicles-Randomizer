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

/* The two who run the chef's control (game\chef.cpp): the chef and the
 * brawler.
 *
 * The brawler is the soldier of JOBY2's last cutscene - Core built him as a
 * cutscene actor (ANIMATING12_MIP, one placeholder animation, no control)
 * and drew him twice there, two actors on one mesh list. His bone tree is
 * the chef's to the last offset, so the randomizer puts his meshes and the
 * chef's animations together on a slot of his own (BRAWLER, game\objects.h)
 * and he fights with the chef's control - with his fist, his own hand mesh
 * where the chef holds the knife.
 *
 * No engine header here: ModernTest asserts on it. */

// The slots the rule names (game\objects.h); setup.cpp checks that they agree.
#define CHEF_RULE_CHEF			87
#define CHEF_RULE_BRAWLER		471

/* Whether the control leaves state 1 at once and hunts Lara.
 *
 * State 1 is the chef's work at the cutting board, and in vanilla he only
 * leaves it when Lara comes close; joby3's kitchen is built for that moment.
 * OCB bit 0 makes a chef hunt instead - the randomizer writes it for every
 * chef it places, and the vanilla chef (joby3 item 186, OCB 0) keeps his
 * vegetables. The brawler has no kitchen anywhere, so he hunts whatever his
 * OCB says: the hunt is his slot's rule, not a number an item carries. */
inline bool ChefHunts(int object, int ocb)
{
	return object == CHEF_RULE_BRAWLER || (ocb & 1) != 0;
}

/* The brawler's animation base, from the anim_index his record carries
 * (-1 when it carries none), the chef's slot being loaded and the chef's
 * anim_index.
 *
 * His animations are the chef's set. The randomizer copies that set into
 * his own slot together with his meshes, so his record always carries it
 * and the anchor resolves to his own slot - in a level without a chef as in
 * one with the vanilla chef beside him. Only a record without animations
 * borrows the chef's slot, the way the stub soldiers borrow the guard's
 * (setup.cpp); with no chef loaded either it stays what it is. */
inline int BrawlerAnimBase(int own, bool chefLoaded, int chefBase)
{
	if (own >= 0)
		return own;

	return chefLoaded && chefBase >= 0 ? chefBase : own;
}
