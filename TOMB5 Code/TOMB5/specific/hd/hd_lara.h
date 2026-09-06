/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the Lara composite - the body is an OUTFIT_ model, the face a
 * separate rigid LARA_HEAD, the braid a six bone HAIR_OG, the hands come from
 * the HAND_ model of the slot whose mesh sits at the hand node and the worn
 * weapons from that slot's HOLSTER_ model - are carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, host\render\game_renderer.cpp:
 * lara_flush 1547-1679, lara_build_models 2462-2604, lara_load 2619-2800,
 * draw_hair 1498-1546, and games\tr5\glue\hostglue_file5.cpp:
 * RegisterLaraWardrobe 1856-1922). The names of the parts are TROne's; the
 * SECTION table under them is measured here, because TROne's TR5 glue has
 * ANDY and JOBY the wrong way round (it gives the Russia outfit to the Irish
 * levels and calls the Russian ones young Lara) - see the note in the .cpp.
 * The renderer under all of it is new: TROne is OpenGL 3.3, this is Direct3D
 * 9 through hd_model. See THIRD-PARTY.md.
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

#include "hd_model.h"

// LARA IN HD - THE COMPOSITE (#123, stage S4-1).
//
// Every other object in this game is ONE slot and ONE .TRM. Lara is not. The
// engine draws her out of up to five classic slots at once - the skin from
// LARA_SKIN, the seams from LARA_SKIN_JOINTS, the hands from whichever weapon
// slot the mesh swap put there, the thigh straps from LARA_HOLSTERS* and the
// braid from HAIR - and the remaster does the same thing with a wardrobe of
// separate models. This module is the wardrobe: which files a level needs,
// what each of them covers, and which of them the draw seam has to put on the
// card for the frame it is looking at.
//
// THIS FILE IS THE WARDROBE AND NOT THE SEAM. It knows nothing about this
// engine and is built into ModelProbe as well, which is how the pictures in
// the report were made. What reads lara_matrices, lara.mesh_ptrs, hairs[0]
// and LaraNodeAmbient and turns them into the calls below is
// specific\hd\hd_laradraw.cpp (stage S4-2), and what calls THAT is
// specific\drawlara.cpp.
//
// THE TWO FACTS THE WHOLE MODULE RESTS ON, both measured over the remaster's
// 4\ITEM (ModelProbe -name <NAME> -nodraw, the per node vertex counts):
//
//   1. Every OUTFIT_ model skins to nodes 0..14 - the classic 15 node Lara
//      mesh tree in LM_ order, hips, thighs, shins, feet, torso, arms, hands,
//      head. Not one of them names a sixteenth. So the composite's skeleton
//      IS Lara's classic skeleton and the joint matrices the game already has
//      (lara_matrices, 15 of them) are the joint matrices the shader wants.
//      The one exception is OUTFIT_SUBSUIT, which has no feet: the diving
//      suit ends in flippers that are part of the shins.
//   2. Each further part covers exactly the nodes it belongs on:
//      LARA_HEAD is a FACIAL RIG (bones up to 31, which are not mesh tree
//      nodes at all) and therefore rides RIGIDLY on node 14; HAIR_OG has six
//      bones, which is exactly TR5's six braid links; HOLSTER_PISTOL and
//      HOLSTER_UZI sit on 1 and 4 (the two thighs), HOLSTER_DEAGLE on 4
//      alone (the big gun hangs on the right), HOLSTER_SHOTGUN / _HK /
//      _GRAPPLE on 7 (the torso - the weapon on the back); a HAND_ model
//      covers 9 and 10 on the right and 12 and 13 on the left, and
//      HAND_BARE_REST adds the two upper arm stubs 8 and 11.
//
// THE "REVOLVER" IS A DESERT EAGLE (#123, fix round C, user finding C1). The
// slot kept its Tomb Raider IV name - REVOLVER_ANIM, LARA_REVOLVER_LASER,
// LARA_HOLSTERS_REVOLVER - and the weapon in it changed, exactly as
// CROSSBOW_ANIM became the grappling gun. The remaster ships both guns side by
// side in 4\ITEM and they are two different models, measured with ModelProbe:
// HAND_BARE_REVOLVER has a cylinder and a 1703 vertex HOLSTER_REVOLVER on node
// 4, HAND_BARE_DEAGLE is the slab-sided automatic with a 1270 vertex
// HOLSTER_DEAGLE on the SAME node 4. TR5 draws the Desert Eagle, so every
// part of the revolver slot takes the DEAGLE file: hand, laser hand, holster.
//
// WHAT IS NOT HERE, ON PURPOSE. Young Lara stays classic - the user's
// decision (plan section 9). OUTFIT_YOUNG, LARA_YOUNG_HEAD and HAIR_PIGTAIL
// all exist in 4\ITEM and TROne solved the two-pigtail flush for TR4, so the
// day that decision changes the work is small; today the Irish levels simply
// get HD_LARA_OUTFIT_NONE and a reason in the log.

#define HD_LARA_NODES		15	// the classic mesh tree, LM_HIPS .. LM_HEAD
#define HD_LARA_HAIR_NODES	6	// TR5's six braid links

/* WHERE THE BRAID HANGS FROM, in the HEAD node's own frame and in classic
 * units. game\hair.cpp:397 walks the mesh tree to the head bone and then
 * translates by exactly this before it writes hairs[0][0], the root of the
 * simulation - up a little, a touch to her left, and 48 units behind the
 * joint, which is the back of the skull. (Young Lara's two pigtails and the
 * second braid use two other triples in the same three lines; neither reaches
 * the HD wardrobe, which has no young body.)
 *
 * It is spelled out here because two things outside hair.cpp have to agree
 * with it: the seam, which reads hairs[0][n] and needs to know what the first
 * of them MEANS, and ModelProbe, which has no simulation and builds its studio
 * braid from this point (#123, fix round C, user finding C4 - "the braid does
 * not grip the head properly"). */
#define HD_LARA_BRAID_ROOT_X	(-4)
#define HD_LARA_BRAID_ROOT_Y	(-4)
#define HD_LARA_BRAID_ROOT_Z	(-48)

/* THE SAME POINT FOR AN HD LARA (#123, fix round D, D3a).
 *
 * Fix round C measured the gap and left it: the braid hangs 15.2 units clear
 * of the skull, on every outfit, and TROne has it too. The cause is that the
 * two braids are built differently. The CLASSIC one draws its first link as a
 * SKINNED connector whose top ring is stashed vertices of the HEAD mesh
 * (game\hair.cpp, GetCorrectStashPoints), so it starts on the skull whatever
 * the root is; the HD one has a rigid HAIR_OG link 0 that starts at the root
 * and reaches only 1.7 units back out of it.
 *
 * The measurement, ModelProbe -braid, and it is the same for all eight bodies
 * because the skull is one mesh:
 *
 *     head joint            1 / -247 / -12
 *     hair.cpp root        -3 / -251 / -60      = the joint plus (-4,-4,-48)
 *     nearest skull vertex -0.7 / -251.2 / -43.3
 *     from the root to it   2.3 / -0.2 / +16.7  - 16.8 units, and the
 *                                                 direction is +z alone
 *     link 0 reaches back                 1.7
 *     the gap that stays open            15.2
 *
 * So the correction is 15 units of +z and nothing else: the root moves from
 * 48 units behind the head joint to 33, and link 0 then overlaps the skull by
 * 1.7 units instead of hanging 15.2 short of it.
 *
 * WITHDRAWN IN FIX ROUND F (F2). THE ENGINE DOES NOT USE THIS NUMBER; it is
 * kept because the measurement above is right and because ModelProbe
 * -braidsim runs it as one of its regimes. What was wrong was moving the root
 * at all:
 *
 * 1. It was written into one of HairControl's two frame branches. The
 *    function has a half for an interpolated frame and a half for a key
 *    frame, and round D changed the second only, so the root of the
 *    simulation jumped between -48 and -33 from frame to frame. hairs[0][0]
 *    seeds the whole chain and every link's velocity is how far it moved
 *    since the last frame, so a root that moves 15 units and back feeds the
 *    braid 15 units of velocity for ever. That is the user's finding F2 -
 *    "it glitches about restlessly" - and his two video frames, one with the
 *    braid thrown out long and the next with it tucked short against her
 *    back.
 *
 * 2. And it does not settle even applied to both halves. ModelProbe
 *    -braidsim, sixty steps in the bind pose, the busiest link's travel per
 *    step at the end: the classic root reads 0, the root at -33 still reads
 *    1 and never reaches 0. At -33 the chain hangs near enough to her neck
 *    for the neck sphere and the pack sphere to push it in turn, and the two
 *    pushes make a small limit cycle rather than a rest.
 *
 * The 15.2 unit gap is therefore accepted for now, which is what the user
 * chose: the gap is quiet and the fix was not. Anybody reopening this should
 * leave the root where the simulation is happy and move the DRAWN braid
 * instead - the seam in hd_laradraw.cpp builds its own matrices out of
 * hairs[0][n] and could put link 0's 15 units in there without the physics
 * ever seeing them. */
#define HD_LARA_BRAID_ROOT_Z_HD	(-33)

/* THE BACKPACK, AS A COLLISION SPHERE (#123, fix round D, D3b).
 *
 * The user's finding: the HD braid goes through the HD backpack. The classic
 * HairControl pushes the braid out of five spheres and all five are CLASSIC
 * Lara meshes - hips, torso, head, the two upper arms. The HD body is a
 * different one, and its pack stands 92 units behind the torso joint where
 * the classic torso sphere reaches nowhere near.
 *
 * Measured with ModelProbe -braid over the wardrobe, in the TORSO NODE's own
 * frame (the pack is every node 7 vertex more than 30 units behind the torso
 * joint - nothing else of a torso is back there):
 *
 *     outfit              verts   centre x/y/z        radius
 *     OUTFIT_CLASSIC       2327    1 / -133 / -61       72
 *     OUTFIT_RUSSIA        2340    1 / -122 / -61       81
 *     OUTFIT_VCI           2277   -0 / -133 / -61       71
 *     OUTFIT_TR3_ANTARC    1468    1 / -122 / -61       83
 *     OUTFIT_TR3_NEVADA    1319    1 / -132 / -61       75
 *     OUTFIT_TR3_COAST     1199    1 / -132 / -61       74
 *     OUTFIT_TR3_CATSUIT   1174    1 / -132 / -61       74
 *
 * One sphere round that box would be a poor pack: it is a flat slab, 130
 * units tall and 62 deep, and a single sphere of radius 75 would hold the
 * braid 44 units clear of the back it is supposed to lie on. So the pack gets
 * TWO, stacked, each tangent to the slab's rear face.
 *
 * The rear face and the height come off the same measurement, taken down the
 * MIDDLE of the back (|x| <= 30, which is the strip a braid can touch) and
 * identical in all seven: y from -197 to -68, z from -92 to -30. Two spheres
 * of radius 36 whose surfaces touch z = -88 therefore sit at z = -52, and at
 * y -165 and -100 they cover y -201 to -64 between them - the slab, with 4
 * units to spare at either end.
 *
 * The diving suit is not in the list on purpose: it measures 165, because
 * what sits behind ITS torso is the two thrusters and not a pack, and it
 * wears a helmet rather than a braid, so no braid ever meets them.
 *
 * WHAT IT IS WORTH, MEASURED (#123, fix round F, F2 - round D said "this is
 * the one change no offline picture can prove", and a picture cannot, but the
 * simulation can be run on its own). ModelProbe -braidsim, sixty steps in the
 * bind pose: without the pack the braid comes to rest 15 units inside it,
 * with the pack 9, and the chain settles either way. The pack does its work
 * and costs no calm. It has to be placed in BOTH halves of HairControl,
 * though; round D put it in one, and a pack that comes and goes from frame to
 * frame is a push that comes and goes with it. */
#define HD_LARA_PACK_X		(0)
#define HD_LARA_PACK_Y_TOP	(-165)
#define HD_LARA_PACK_Y_BOTTOM	(-100)
#define HD_LARA_PACK_Z		(-52)
#define HD_LARA_PACK_R		(36)

/* WHY THE DRAWN BRAID GETS A ROLL OF ITS OWN (#123, fix round G, G1).
 *
 * The user's finding after round F: "the braid still trembles". Round F had
 * proved the SIMULATION calm and it is - the play test's own HAIRLOG says so
 * on 924 frames where the root did not move a unit, the tip moved less than
 * two on 728 of them and the two long standing stretches settle to exactly
 * zero and stay there. What nobody had measured was the ANGLE.
 *
 * game\hair.cpp gives link n-1 the spherical angles of the vector to link n:
 *
 *     (hair - 1)->pos.y_rot = phd_atan(dz, dx)
 *     (hair - 1)->pos.x_rot = -phd_atan(phd_sqrt(dz*dz + dx*dx), dy)
 *
 * and dx, dy, dz are differences of LONGS. For a braid hanging straight down
 * dy is the whole bone, 56, and dx and dz are single digits - so the PITCH is
 * a solid -90 degrees and the YAW is not an angle at all but the rounding of
 * two small integers. One unit of change in dx with dx and dz near zero
 * swings it by ninety degrees or more.
 *
 * TR5 could live with that. Its braid is three rigid meshes and three SKINNED
 * connectors, and GetCorrectStashPoints exists precisely to renumber the
 * connector's vertex ring when two neighbouring links disagree by a quarter
 * turn - Core knew the yaw jumps and hid it. The HD braid is six rigid bones
 * of one plaited model (hd_laradraw.cpp, braid_matrices, out of TROne's
 * draw_hair), and a rigid plait turned about its own axis is visible.
 *
 * MEASURED, ModelProbe -braidanim over ANDREA2's own animation table, 110
 * frames of ANIM_BREATH - Lara standing at a door, which is where the user's
 * two video frames come from:
 *
 *     variant                      tip travel per frame   worst yaw jump
 *     the classic body alone       1 unit, never more     180 degrees on 18
 *     round F as shipped           1 unit, never more     180 degrees, frames
 *
 * The braid stands still to within a unit and spins half a turn on one frame
 * in six. That is the tremble.
 *
 * THE CURE has to leave the direction alone - the drawn bone's own +z IS the
 * link direction, and hd_laradraw's braidcheck proves that stepping one bone
 * length along it lands on the next link. A rotation about that same +z moves
 * nothing at all, so the roll is free: after RotY and RotX comes a RotZ that
 * takes the noise back out, and the braid's plait then keeps facing the way
 * Lara faces instead of the way two integers rounded.
 *
 * WHICH ANGLE TO PUT THERE INSTEAD was measured, not guessed: ModelProbe
 * -braidanim runs five candidates over the same three animations and counts
 * the worst one-frame turn of any bone, in degrees, over the worst of the
 * five physics variants:
 *
 *     rule                                    standing   running   walking
 *     none - the raw yaw, as round F ships     180        7         7
 *     towards her facing, faded per link         0      167       179
 *     the whole chain's azimuth, every link     45        3         1
 *     the same, faded per link                  45        8         2
 *     the chain's azimuth, HELD while vertical   0        3         1
 *     her facing, every link, always             0        0         0
 *
 * The last line is not the winner it looks: Lara never turns in the tool, so
 * a rule that says "whatever she is facing" cannot move at all there and the
 * measurement is empty. It would also roll the plait against its own curve
 * whenever the braid is thrown out sideways, which is the one case where the
 * yaw is a real angle.
 *
 * The last one is what ships. ONE reference for all six bones - a rigid plait
 * cannot hide two neighbours twisting against each other, and the classic
 * path's skinned connectors are exactly how TR5 hid it - and the reference is
 * the azimuth of the WHOLE braid, root to tip, because that vector is six
 * bones long where a link is one and its horizontal part is ten to thirty
 * times better conditioned. "Towards her facing" fails the walk for the same
 * reason the raw yaw fails standing: it is blended per link by a weight that
 * jumps when the link crosses the band, and a blend of two angles far apart
 * jumps with it.
 *
 * WHEN IT FOLLOWS. `flat` is the horizontal span of the whole braid, out of a
 * chain 336 units long. Below NEAR the reference does not move at all - a
 * braid hanging plumb has nothing to say about its own azimuth - above FAR it
 * follows completely, and between the two it follows part of the way, so
 * there is no threshold the picture can cross in one frame. */
#define HD_LARA_BRAID_ROLL_NEAR	16
#define HD_LARA_BRAID_ROLL_FAR	48

/* THE PSEUDO SLOTS. hd_model stores its models in a map keyed by slot number,
 * and a wardrobe part has no slot of its own - LARA_SKIN is one slot and the
 * composite is twenty models. So each part gets a number of its own, far above
 * anything the engine will ever hand out (471 today, 32767 is the ceiling of
 * ITEM_INFO::object_number), and the wardrobe rides through hd_model's normal
 * machinery: the same page cache, the same skinning, the same draw call. */
#define HD_LARA_SLOT_BASE	0x5A00

/* THE FIRST PERSON NEAR CUT (#123, stage S8): a RADIUS around the eye, in
 * classic units. Every fragment of Lara's BODY nearer to the eye than this is
 * discarded while the first person view runs; her hands, her holsters and the
 * weapon on her back are separate models and are never cut. The shader end of
 * it is hd_model.hlsl, ModelPS.
 *
 * WHY THERE HAS TO BE ONE. The classic path hides the head MESH; an HD body is
 * one skinned surface, and the neck stump under the head belongs to the TORSO
 * node, so masking node 14 leaves it standing in the camera (user finding F3).
 *
 * WHY A SPHERE AND NOT A DEPTH. A cut at a fixed view DEPTH is a plane across
 * the view, and the moment she looks down that plane lies across her chest:
 * measured at 72 units of depth it took her collarbone with the stump and one
 * saw the floor through the hole. A cut at a fixed DISTANCE cannot do that,
 * because the two are separated in distance from every angle at once.
 *
 * WHERE 60 COMES FROM. ModelProbe -neck over all eight outfits, against the
 * eye the first person camera really uses: the head joint of the level's own
 * Lara skeleton (ANDREA1: 1 / -247 / -12), 16 units up and up to
 * FP_EYE_OFFSET = 48 units forward (game\camera.cpp). The forward step is what
 * mgLOS allows, so BOTH ends of it have to hold - 48 in the open, 0 with a
 * wall in front of her, which is the case that puts the stump nearest:
 *
 *                          eye 48 forward          eye at the joint
 *     outfit             stump  shoulder chest   stump shoulder chest
 *     OUTFIT_CLASSIC      41.9   79.8   74.6      18.6   73.5   73.6
 *     OUTFIT_RUSSIA       40.4   77.8   73.5      18.6   71.6   81.9
 *     OUTFIT_VCI          37.4   78.3   74.6      18.6   71.9   73.5
 *     OUTFIT_SUBSUIT      32.7   74.7   87.8      22.2   68.9   99.7
 *     OUTFIT_TR3_ANTARC   41.9   78.2   74.5      19.1   71.7   78.2
 *     OUTFIT_TR3_NEVADA   41.9   79.0   74.6      19.1   72.7   81.2
 *     OUTFIT_TR3_COAST    41.9   79.0   76.3      19.1   72.7   75.3
 *     OUTFIT_TR3_CATSUIT  41.9   78.6   74.6      19.1   72.4   81.3
 *
 * "stump" is the nearest torso-node vertex above the shoulder line, "shoulder"
 * the nearest of nodes 8 and 11, "chest" the nearest torso-node vertex below
 * that line. The nearest thing that must SURVIVE, over all sixteen columns, is
 * 68.9; 60 leaves it 8.9 units of margin, which is what the animation needs -
 * a shrug moves the shoulder relative to the head, the table does not. The
 * number is not per outfit and needs no hand work for a new one: the skeleton
 * it is measured against is the engine's own and every wardrobe body is built
 * on it.
 *
 * IT DOES NOT WORK ALONE. Cutting a closed body open shows the body's inside,
 * because the HD path draws every model double sided - so the cut is paired
 * with HD_MODEL_CULL_BACK on the same draw. hd_model.h has that half. */
#define HD_LARA_FIRST_PERSON_NEAR_CUT	60.0f

/* The classic Lara mesh tree in LM_ order, spelled out so that the tables
 * below and a reader of them do not have to look it up. These are the numbers
 * a .TRM vertex names in its three bone bytes. */
enum HDLaraNode
{
	HD_LARA_HIPS = 0,
	HD_LARA_LTHIGH, HD_LARA_LSHIN, HD_LARA_LFOOT,
	HD_LARA_RTHIGH, HD_LARA_RSHIN, HD_LARA_RFOOT,
	HD_LARA_TORSO,
	HD_LARA_RINARM, HD_LARA_ROUTARM, HD_LARA_RHAND,
	HD_LARA_LINARM, HD_LARA_LOUTARM, HD_LARA_LHAND,
	HD_LARA_HEAD_NODE
};

// Which body Lara wears. The section table that picks one is in the .cpp.
enum HDLaraOutfit
{
	HD_LARA_OUTFIT_NONE = 0,	// stay classic - and the log says why
	HD_LARA_OUTFIT_CLASSIC,		// Rome: shorts and a green top
	HD_LARA_OUTFIT_RUSSIA,		// the Russian section: coat and cap
	HD_LARA_OUTFIT_SUBSUIT,		// the diving suit of Deepsea Dive
	HD_LARA_OUTFIT_VCI,		// the black catsuit of the VCI levels
	HD_LARA_OUTFIT_TR3_ANTARC,	// the four guests the randomizer rolls (#122)
	HD_LARA_OUTFIT_TR3_NEVADA,
	HD_LARA_OUTFIT_TR3_COAST,
	HD_LARA_OUTFIT_TR3_CATSUIT,
	HD_LARA_OUTFIT_COUNT
};

// The parts of the composite. Their order is the order they are drawn in.
enum HDLaraPart
{
	HD_LARA_PART_BODY = 0,		// the OUTFIT_ model, nodes 0..14
	HD_LARA_PART_FACE,		// LARA_HEAD, rigid on node 14
	HD_LARA_PART_BRAID,		// HAIR_OG, its own six node chain

	HD_LARA_PART_HAND_REST,		// nothing in the hands
	HD_LARA_PART_HAND_PISTOL,
	HD_LARA_PART_HAND_UZI,
	HD_LARA_PART_HAND_SHOTGUN,
	HD_LARA_PART_HAND_HK,
	HD_LARA_PART_HAND_DEAGLE,	// TR5's "revolver" slot is a Desert Eagle
	HD_LARA_PART_HAND_DEAGLE_LASER,
	HD_LARA_PART_HAND_GRAPPLE,	// TR5's "crossbow" slot is the grappling gun
	HD_LARA_PART_HAND_GRAPPLE_LASER,
	HD_LARA_PART_HAND_FLARE,	// left hand only
	HD_LARA_PART_HAND_TORCH,	// left hand only
	HD_LARA_PART_HAND_CROWBAR,	// right hand only

	HD_LARA_PART_HOLSTER_PISTOL,	// both thighs
	HD_LARA_PART_HOLSTER_UZI,	// both thighs
	HD_LARA_PART_HOLSTER_DEAGLE,	// the right thigh alone

	/* THE POUCHES THE GUNS SIT IN (#123, fix round D, D4).
	 *
	 * A HOLSTER_ file is the WEAPON and nothing else - HOLSTER_PISTOL.TRM is
	 * two pistols, 4268 vertices, and no leather anywhere (ModelProbe -name
	 * HOLSTER_PISTOL, a picture of two guns hanging in mid air). The pouch,
	 * the thigh strap and the hip belt they hang from are built into the
	 * OUTFIT model, one page of it: 8017 in OUTFIT_CLASSIC and OUTFIT_RUSSIA,
	 * 8205 in OUTFIT_TR3_COAST and OUTFIT_TR3_CATSUIT.
	 *
	 * Three of the eight wardrobe bodies have none - OUTFIT_VCI,
	 * OUTFIT_TR3_ANTARC and OUTFIT_TR3_NEVADA - because vanilla TR5 gives
	 * catsuit Lara no pistols and the two TR3 guests wear none in their own
	 * game either. The randomizer hands her a gun in every level, so the
	 * guns hung in the air beside her thigh: the user's finding D4, with
	 * three photographs.
	 *
	 * This part is the missing rig, cut out of OUTFIT_TR3_CATSUIT by page
	 * (HDModelSlot::onlyPage). The London catsuit is the donor rather than
	 * the Rome one because its rig is black leather over a black leg, which
	 * is what the VCI catsuit is; OUTFIT_CLASSIC's is brown suede over bare
	 * skin.
	 *
	 * WHO WEARS IT is a table and not a rule (fix round F, F1): OUTFIT_VCI
	 * and nobody else. Round D gave it to all three bodies that carry no rig
	 * of their own, and on the Antarctica parka and the Nevada vest the
	 * straps clip through the coat - those two never wore a thigh rig in
	 * TR3 and their geometry is finished without one. hd_lara.cpp's
	 * Outfit::wearsRig carries the decision and the measurement behind it. */
	HD_LARA_PART_HOLSTER_RIG,

	HD_LARA_PART_BACK_SHOTGUN,	// the torso
	HD_LARA_PART_BACK_HK,
	HD_LARA_PART_BACK_GRAPPLE,

	HD_LARA_PART_COUNT
};

// What a classic slot is to the composite.
enum HDLaraRole
{
	HD_LARA_ROLE_NONE = 0,		// not one of Lara's slots
	HD_LARA_ROLE_BODY,		// LARA, LARA_SKIN
	HD_LARA_ROLE_HAND,		// a weapon/tool slot, or the body's rest hands
	HD_LARA_ROLE_HOLSTER,		// LARA_HOLSTERS*
	HD_LARA_ROLE_BACK,		// the same weapon slot seen as lara.back_gun
	HD_LARA_ROLE_HAIR,		// HAIR
	HD_LARA_ROLE_SUPPRESS		// drawn classic today, drawn by nobody in HD
};

/* WHAT THE LEVEL HANDS OVER. The two bind poses are the engine's own: the
 * accumulated offsets of the slot's mesh tree, three floats a node, in node
 * order - exactly what hd_object.cpp's bind_pose_of() produces for an
 * ordinary slot and what ModelProbe reads out of the .TRC. Without them a
 * model draws in its own bind pose and nowhere else. */
struct HDLaraLevel
{
	int outfit;			// one of HDLaraOutfit
	const float* bodyBind;		// HD_LARA_NODES * 3, from LARA_SKIN (or LARA)
	int bodyNodes;
	const float* hairBind;		// HD_LARA_HAIR_NODES * 3, from HAIR, or 0
	int hairNodes;			// 0 when the level has no braid

	/* WHICH OF LARA'S SLOTS THE LEVEL ACTUALLY LOADED, so that the wardrobe
	 * is not loaded whole every time. The full set is 21 models, 3.4 MiB of
	 * buffers and 30 MiB of texture pages, and most of it is weapons the
	 * level file has no slot for: the VCI levels ship no LARA_HOLSTERS_UZIS,
	 * the Rome ones no HK_ANIM. A weapon whose slot is not loaded cannot be
	 * drawn by the classic path either - the engine reads
	 * meshes[objects[slot].mesh_index] - so leaving its wardrobe model out
	 * costs nothing at all.
	 *
	 * `slots` is the list of loaded slot numbers (objects[n].loaded, the
	 * Lara block is enough) and `slotCount` its length. Pass 0 to plan the
	 * whole wardrobe, which is what a diagnostic run with no level wants. */
	const int* slots;
	int slotCount;
};

struct HDLaraStats
{
	int outfit;			// what was asked for
	int wanted;			// parts planned
	int loaded;			// parts on the card
	int missing;			// a name with no .TRM anywhere
	int vertices;
	int indices;
	int ranges;
	int pages;			// distinct .DDS pages the wardrobe added
	double vertexBytes;
	double indexBytes;
	double buildMs;

	// The composite against the classic skeleton, in classic units: the
	// worst and the mean of hd_model's pairwise bind drift over the parts
	// that carry more than one node. A part that belongs on this tree keeps
	// it small; see HD_SKELETON_DRIFT_LIMIT in hd_model.h.
	float driftWorst;
	float driftMean;
	int driftParts;
};

/* THE FRAME. What the seam in DrawLara knows about this one frame, in the
 * engine's own terms - slot numbers, exactly the ones DrawLara already has in
 * its hands. HDLaraFrameParts turns them into the list of parts to draw.
 *
 * rightHandSlot / leftHandSlot: which slot the mesh at LM_RHAND / LM_LHAND
 * comes from. The engine keeps that in lara.mesh_ptrs[] as a POINTER, so the
 * seam has to compare it against meshes[objects[s].mesh_index + 2 * LM_xHAND]
 * for the handful of candidate slots - which is cheap and is what the classic
 * path itself does in larafire.cpp:331.
 *
 * leftHolsterSlot / rightHolsterSlot: LHolster and lara.holster - the engine's
 * own two variables, already the slot number.
 *
 * backGunSlot: lara.back_gun, or 0 for none. */
struct HDLaraFrame
{
	int rightHandSlot;
	int leftHandSlot;
	int leftHolsterSlot;
	int rightHolsterSlot;
	int backGunSlot;
};

// One thing to draw: a part and the nodes it is responsible for this frame.
// `nodeMask` goes straight into HDModelDraw's mesh-bits argument, so the two
// hands of a crowbar frame - HAND_BARE_CROWBAR on the right, HAND_BARE_REST
// on the left - do not draw two left hands into one another.
struct HDLaraDraw
{
	int part;
	unsigned int nodeMask;
};

// --- what the level asks for -----------------------------------------------

// The .TRM stem of an outfit ("OUTFIT_VCI"), or 0.
const char* HDLaraOutfitModel(int outfit);
// A short human name for the log ("the VCI catsuit"), never 0.
const char* HDLaraOutfitLabel(int outfit);
// An outfit from a name: the .TRM stem, the bare tail ("VCI", "TR3_COAST") or
// the randomizer's own guest id ("tr3_london"). HD_LARA_OUTFIT_NONE when the
// name is none of them.
int HDLaraOutfitFromName(const char* name);

/* WHICH OUTFIT THIS LEVEL WEARS, out of the engine's own facts and nothing
 * else. The caller passes what it has:
 *
 *   laraDrawType  LaraDrawType, i.e. LARA_NORMAL .. LARA_PONYTAIL. It comes
 *                 from data[0] of the level header and the randomizer writes
 *                 it, so it is the outfit's own word and not the level's.
 *   russiaCap     the ENGINE'S OWN predicate for "she is wearing the cap":
 *                 (gfLevelId in the Russia range && LaraDrawType ==
 *                 LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD. That exact
 *                 expression already stands three times in game\hair.cpp and
 *                 once in game\laraskin.cpp, and it is the only thing in this
 *                 engine that knows the difference - a vanilla Russia level
 *                 carries LaraType 0, the same byte Rome carries. Passing the
 *                 engine's own test in rather than rebuilding it here is what
 *                 keeps the two from drifting apart.
 *   youngLara     (gfLevelFlags & GF_YOUNGLARA) || LaraDrawType ==
 *                 LARA_YOUNG. Either and Lara stays classic, by the user's
 *                 decision.
 *
 *                 THE IRISH LEVEL RANGE IS NOT IN THAT TEST, and it was
 *                 (#123, stage S7-2, the spiel test's F1). The engine builds
 *                 the child body at exactly two places - LaraInitialiseMeshes
 *                 in game\laramisc.cpp and its twin in game\deltapak.cpp -
 *                 and both ask the range AND THE FLAG. The randomizer clears
 *                 GF_YOUNGLARA out of SCRIPT.DAT for all three Irish levels
 *                 (core\ScriptDat.cpp) and replaces Lara's skin with an
 *                 adult's, because young Lara can carry no weapon; the range
 *                 alone therefore said "child" about a grown Lara and drew
 *                 every Irish level classic. The flag and the LaraType byte
 *                 both flip with the change and neither has that hole.
 *   tr3Outfit     one of the four HD_LARA_OUTFIT_TR3_* when the randomizer
 *                 dressed this level in a Tomb Raider III outfit, otherwise
 *                 HD_LARA_OUTFIT_NONE. It is data[3] of the level header
 *                 (#123, stage S7), passed in RAW: the randomizer writes the
 *                 engine's own numbers so that neither side translates.
 *                 Anything but 5..8 is ignored - see the .cpp. It exists
 *                 because the wardrobe writes the SAME outfit byte
 *                 (LARA_PONYTAIL) for a guest and for the Rome body in a
 *                 Russia level, and nothing else in the file tells the two
 *                 apart.
 *
 * *why comes back as a short reason for the log, never 0.
 */
int HDLaraOutfitFor(int laraDrawType, bool russiaCap, bool youngLara,
	int tr3Outfit, const char** why);

// --- the parts -------------------------------------------------------------

// The pseudo slot a part lives in, for HDModelDraw and friends.
int HDLaraPartSlot(int part);
// The .TRM stem of a part for this outfit, or 0 when the outfit does not use
// it (the diving suit has no hands, no braid and no holsters of its own).
const char* HDLaraPartModel(int outfit, int part);
// A part's name for the log ("the right hand with the HK"), never 0.
const char* HDLaraPartLabel(int part);

/* The slots the parts hang off. Both take an ENGINE slot number and answer
 * out of hd_bind's name table, so the numbers stay in one place: a slot is a
 * hand slot because it is called PISTOLS_ANIM, not because it is 1. */
int HDLaraRoleOfSlot(int slot);
// The part a slot stands for in that role, or -1. HD_LARA_ROLE_BACK and
// HD_LARA_ROLE_HAND give different answers for the same weapon slot - the HK
// in the hands is HAND_BARE_HK, on the back HOLSTER_HK.
int HDLaraPartForSlot(int slot, int role);

// --- the level bracket -----------------------------------------------------

/* The parts this level needs, as HDModelSlot entries for HDModelLevelBegin.
 * The caller appends them to its own slot list and passes the lot in one go -
 * one page cache, one build, one measurement. Returns how many were written;
 * 0 when the outfit is HD_LARA_OUTFIT_NONE or there is no bind pose.
 *
 * The bind pose pointers are COPIED INTO the entries, not the data - so
 * `level` and the two arrays it points at have to outlive HDModelLevelBegin,
 * exactly as hd_object.cpp's own bind block does. */
int HDLaraPlan(const HDLaraLevel* level, HDModelSlot* out, int max);

/* After HDModelLevelBegin: work out what really arrived, measure the drift
 * against the classic skeleton and write the notes. False when the body is
 * not on the card, which is the one part the composite cannot do without -
 * Lara then stays classic and the reason is in the notes. */
bool HDLaraLevelBuild(const HDLaraLevel* level, HDLaraStats* stats);
void HDLaraLevelEnd();

bool HDLaraActive();
int HDLaraOutfitNow();
bool HDLaraHasPart(int part);
// Whether the level asked for the part at all - see HDLaraLevel::slots. A part
// that was never planned is not missing; the level simply has no slot for it.
bool HDLaraPartPlanned(int part);
// Bit per classic node the part covers, as hd_model measured it. 0 for a part
// that is not on the card.
unsigned int HDLaraPartCoverage(int part);

/* THE SHEEN OF ONE PART (#123, fix round D, D2).
 *
 * The strength and the Blinn-Phong exponent HDModelLighting wants, for the
 * outfit that is on. Both 0 when the part does not shine, and then the shader
 * takes the branch that costs nothing.
 *
 * The numbers are a MATERIAL table and not a measurement, and hd_lara.cpp says
 * so at the table itself: there is no gloss mask in the art to read (measured
 * with DdsProbe -alpha - every wardrobe page is a flat 240..255 alpha), so
 * what a surface is made of has to be written down. They are logged per part
 * at level build so that a picture and a log line can be put side by side. */
void HDLaraPartSheen(int part, float* strength, float* power);

/* WHAT TO DRAW THIS FRAME, in draw order, after the body and the face. Returns
 * how many entries were written (at most 6: two hands, two thigh holsters, the
 * back weapon and one spare).
 *
 * THE RULE, which is TROne's with one addition. TROne picks ONE hand model -
 * the first of nodes 10 and 13 whose mesh came from a non-body slot - and
 * draws it whole. That is right for every two-handed frame and wrong for the
 * three TR5 frames where the hands differ: a flare or a torch is in the LEFT
 * hand while the right is empty, and the crowbar is in the RIGHT hand while
 * the left is empty (measured: HAND_BARE_CROWBAR covers nodes 9 and 10 and
 * nothing else, HAND_BARE_FLARE 12 and 13 and nothing else). So when the two
 * sides disagree, both models are drawn and each is masked to its own arm -
 * 8, 9, 10 on the right, 11, 12, 13 on the left. When they agree, which is
 * every other frame, it is one draw with every node, exactly as before. */
int HDLaraFrameParts(const HDLaraFrame* now, HDLaraDraw* out, int max);

// --- the log ---------------------------------------------------------------

int HDLaraNoteCount();
const char* HDLaraNote(int at);
