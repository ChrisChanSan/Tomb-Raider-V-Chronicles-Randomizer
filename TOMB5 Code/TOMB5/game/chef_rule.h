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
 * where the chef holds the knife. His reach is his own, measured on that
 * fist (below).
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

/* The brawler's reach - measured on his own fist, not the chef's numbers.
 *
 * His strike is the chef's: animation +2 of his set, state 3, 46 frames
 * at one a tick. He swings it with his own hand, mesh 13, an empty fist
 * whose sphere has radius 51 where the chef's knife has 138. BoneCheck
 * -reach poses that sphere on every frame the way GetSpheres does (the
 * sphere the touch bits come from), on his meshes from joby2's slot 439
 * and the chef's frames from joby3's slot 87 - and the same on slot 471
 * of a randomized level. Ahead of his pivot the fist is 310 at the
 * first frame, pulls back to 188 (frame 12), slashes across his front -
 * 344 on frame 15 at 1.6 degrees right of his facing, 345 on frame 16 at
 * 22.1 degrees, between 381 and 515 above his floor - swings out to the
 * right and comes slowly back, 302 to 353 ahead from frame 29 on. The
 * chef's knife reaches 473 on frame 16. Standing, the fist is already
 * 310 to 367 ahead: the chef's guard pose holds the knife out.
 *
 * The hit window is the slash, frames 15 and 16, the two in which the
 * fist crosses his front at its reach; the swing's sound plays on 16.
 * Lara is struck when her pivot lies within the fist's front plus her
 * radius (100, coll->radius in LaraAboveWater), within the cone of 22.1
 * degrees either side of the arm, and her body - her frame's box, 768 to
 * 781 high on her feet, 347 crouched - overlaps the fist's band: crouched
 * she ducks under the slash. The way back from frame 29 is no strike: the
 * arm returns to guard about 10 units a frame.
 *
 * The swing begins (and his walk ends) when CreatureAIInfo's distance is
 * below BRAWLER_SWING: from 50 units ahead of him (pivot_length) to where
 * Lara will be after 14 frames at her speed. A swing begun at a pivot
 * distance d is checked on frame 15 sixteen ticks later. Standing, Lara
 * is still at d and the AI saw d - 50. Walking back (10 a frame, her
 * animation 40) she is 160 further, and the AI saw d + 140 - 50. The
 * largest distance that lands on both is 445 - 50 - 10 * (16 - 14) = 375;
 * walking back is the row that binds it. Running (47 a frame) the AI sees
 * her 658 further: he does not swing at her, and she outruns his walk
 * (19 a frame). */
#define BRAWLER_STRIKE_ANIM		2		// his set's state 3
#define BRAWLER_HIT_FIRST		15		// frames of the strike, 0 its first
#define BRAWLER_HIT_LAST		16
#define BRAWLER_FIST_FRONT		345		// ahead of his pivot, frames 15..16
#define BRAWLER_LARA_RADIUS		100
#define BRAWLER_REACH			445		// the fist's front plus her radius
#define BRAWLER_CONE			4029	// 22.1 degrees, 65536 a turn
#define BRAWLER_FIST_TOP		515		// above his floor, frames 15..16
#define BRAWLER_FIST_BOTTOM		381
#define BRAWLER_SWING			375		// the AI's distance, not squared

// The table's inputs: the AI's pivot_length (setup.cpp) and look-ahead
// (CreatureAIInfo), Lara's walk back (animation 40's speed) and the mesh
// his line of sight leaves from, his torso.
#define BRAWLER_AI_PIVOT		50
#define BRAWLER_AI_AHEAD		14
#define BRAWLER_LARA_BACK		10
#define BRAWLER_TORSO_MESH		7

// The squared AI distance below which he swings (state 4) and his walk
// ends (state 5) - his own; the chef keeps vanilla's 682 (chef.cpp).
inline long BrawlerSwingSquared()
{
	return (long)BRAWLER_SWING * BRAWLER_SWING;
}

/* Whether his fist reaches Lara on frame `frame` of the strike (0 its
 * first): `dist2` the square of the horizontal distance between their
 * pivots, `angle` her direction off his arm's - his facing turned by the
 * torso's twist - with 65536 a turn, `dy` her y minus his (y grows
 * downward), `top` the top of her frame's box (GetBestFrame, negative).
 * Her body spans dy + top .. dy against the fist's -515 .. -381. The line
 * of sight is the caller's. */
inline bool BrawlerFistReaches(int frame, long long dist2, int angle, long dy,
	long top)
{
	if (frame < BRAWLER_HIT_FIRST || frame > BRAWLER_HIT_LAST)
		return false;

	if (dist2 > (long long)BRAWLER_REACH * BRAWLER_REACH)
		return false;

	if (angle > BRAWLER_CONE || angle < -BRAWLER_CONE)
		return false;

	return dy >= -BRAWLER_FIST_TOP && dy + top <= -BRAWLER_FIST_BOTTOM;
}
