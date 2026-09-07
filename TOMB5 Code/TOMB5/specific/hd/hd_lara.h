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
 * WITHDRAWN IN FIX ROUND F (F2) AND BACK IN FIX ROUND I (I2). What was wrong
 * in round D was not the number but two things around it:
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
 * 2. And with round D's PACK it did not settle even applied to both halves.
 *    ModelProbe -braidsim, sixty steps in the bind pose, the busiest link's
 *    travel per step at the end: the classic root read 0, the root at -33
 *    still read 1 and never reached 0. At -33 the chain hangs near enough to
 *    her neck for the NECK SPHERE and a PACK SPHERE to push it in turn, and
 *    two spheres pushing a point in turn make a small limit cycle rather than
 *    a rest.
 *
 * FIX ROUND I (I2) ASKED THE SAME QUESTION AGAIN, because reason 2 was about
 * the pack being SPHERES and the pack is a box now. A box pushes a point out
 * of one flat face along one axis and leaves it there; there is no curved
 * surface for the neck sphere to slide it back down. Measured, the same
 * -braidsim, sixty steps, both torso anchors:
 *
 *     regime                          step 1  step 10  step 30  last  in pack
 *     classic: root -48, 6 spheres        14        1        0     0       38
 *     round D as meant: -33, spheres      34        4        1     1       41
 *     round F/G: root -48, spheres        14        1        0     0       38
 *     round I: root -48, the BOX          45       10        0     0        7
 *     round I: root -33, the BOX          63       18        0     0       12
 *
 * Both box lines reach zero and stay there, so the reason the root was
 * withdrawn is gone; and "in pack" is the finding itself - the braid stood 38
 * units inside the pack with the spheres and stands 7 with the box.
 *
 * So the root is -33 whenever the HD wardrobe is drawing and -48 otherwise,
 * written in BOTH halves of HairControl out of ONE flag that is read once per
 * frame (game\hair.cpp, `pack.on`), which is the shape round D's mistake asks
 * for. Under Lara's own animations (ModelProbe -braidanim, ANIM_BREATH) the
 * near root costs a little liveliness and no jumps: the tip's worst travel in
 * a frame goes from 10 units to 14 and its mean from 1.04 to 1.41, with no
 * frame over 40 in either. */
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

/* THE SAME PACK AS A BOX, WHICH IS WHAT IT IS (#123, fix round I, I1).
 *
 * The user's finding after round G: the braid still lies IN the pack. Round G
 * had already written down why and left it as a decision - "the braid lies in
 * the pack, not on it - round D's own measurement is a slab, and two spheres
 * touch it on the centre line only". A sphere of radius 36 tangent to a flat
 * rear face touches that face at ONE point; a hand's width to either side its
 * surface has already curved 10 units forward, and the plait is 35 units wide.
 * So the braid sinks in beside the centre line however many spheres are
 * stacked, and the pack has to be a box.
 *
 * MEASURED, ModelProbe -braid, "the backpack as a BOX": the node 7 vertices
 * more than 30 units behind the torso joint, in the torso node's own frame -
 * the same point set round D took its spheres from, now as six numbers rather
 * than three:
 *
 *     outfit                x            y            z
 *     OUTFIT_CLASSIC       -57..59     -197..-68     -92..-30
 *     OUTFIT_RUSSIA        -60..62     -197..-47     -92..-30
 *     OUTFIT_VCI           -58..57     -197..-68     -92..-30
 *     OUTFIT_TR3_ANTARC    -59..60     -197..-48     -92..-30
 *     OUTFIT_TR3_NEVADA    -58..60     -197..-67     -92..-30
 *     OUTFIT_TR3_COAST     -58..60     -197..-67     -92..-30
 *     OUTFIT_TR3_CATSUIT   -58..60     -197..-67     -92..-30
 *
 * ONE BOX FOR ALL SEVEN, and the table is why: the depth is the SAME number in
 * every one of them (-92 to -30), the width agrees to three units, and the top
 * to none at all. The bottom is the only column that moves, and the two that
 * move it - the Russia coat and the Antarctica parka - are not deeper packs
 * but longer COATS: the rule "node 7 and more than 30 behind the joint" picks
 * up their back skirt as well. The box takes the pack's own bottom, -68, and
 * leaves the coat tail to the torso sphere, which for the Russia outfit is
 * nailed to radius 94 and covers it.
 *
 * The diving suit is left out here as it was left out of the spheres: what
 * stands behind ITS torso is two thrusters, it wears a helmet and no braid.
 *
 * THE MARGIN IS THE PLAIT'S OWN RADIUS. HairControl moves POINTS and the braid
 * is a solid; a point pushed exactly onto the pack's surface leaves half the
 * plait inside it. Measured over HAIR_OG (ModelProbe -braid, "HAIR_OG
 * thickness"): the radius about each bone's own axis is 17.5, 17.5, 15.0,
 * 12.8, 10.6 and 9.0 - the plait tapers to the tip. hairs[0][i] is the joint
 * BETWEEN bone i-1 and bone i, so the radius that belongs to it is the thicker
 * of the two, and the tip takes the last bone's.
 *
 *     point   1     2     3     4     5     6
 *     radius  18    18    15    13    11    9
 */
#define HD_LARA_PACK_BOX_X		(60)	// half width, |x| <= this
#define HD_LARA_PACK_BOX_Y_TOP		(-197)
#define HD_LARA_PACK_BOX_Y_BOTTOM	(-68)
#define HD_LARA_PACK_BOX_Z_BACK		(-92)	// the face the braid lies on
#define HD_LARA_PACK_BOX_Z_FRONT	(-30)

/* The six numbers above, as an initialiser - hair.cpp and ModelProbe both
 * want them and neither may own them. */
#define HD_LARA_BRAID_RADII	{ 18, 18, 15, 13, 11, 9 }

/* HOW MUCH THE REAR FACE IS PREFERRED when the link is pushed out along the
 * smallest penetration. It is the face the braid belongs on: everything else
 * is a way INTO her back or under the pack, and a tie between two faces that
 * goes the wrong way is a plait that flips through the body for one frame.
 * The bias is a divisor on the rear face's own depth, so a rear push wins
 * unless another face is more than twice as close.
 *
 * MEASURED AND HONEST: over the three animations ModelProbe -braidanim runs -
 * breathing on the spot, the run loop and the walk loop - variant h ("the BOX,
 * no rear preference") comes out IDENTICAL to variant f in every column. The
 * braid approaches this pack from behind and the rear face is the nearest one
 * anyway, so the preference never fires there. It is kept because the tool
 * never bends Lara far forward and the faces it steers away from lead into her
 * back; a value of 1 would be the same picture in everything measured. */
#define HD_LARA_PACK_BOX_BIAS	(2)

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

/* THE FIRST PERSON HEAD CUT (#123, stage S8; a sphere then, a cylinder in
 * fix rounds J to L, a plane in fix round S; ONE BONE WEIGHT since fix round
 * U, U1).
 *
 * EVERY FRAGMENT WHOSE HEAD WEIGHT REACHES HD_LARA_HEAD_CUT_WEIGHT IS
 * DISCARDED while the first person view runs. That is the whole rule. There
 * is no shape, no plane, no radius, no near zone, no node mask and nothing
 * that depends on where the camera is or on what the animation is doing: a
 * vertex's three bone weights are written in the .TRM file and are the same
 * number in every pose, at every angle, in every level.
 *
 * WHY IT HAD TO STOP BEING A SHAPE, and this is the fourth answer to the same
 * question, so the argument is worth keeping whole:
 *
 *   stage S8   a SPHERE about the eye. It took the stump and a bite out of
 *              her chest with it, and it chewed a wrist brought up to the
 *              camera.
 *   rounds J-L a CYLINDER about the neck axis in the BIND POSE, with a node
 *              mask to keep it off the arms. Three play tests running came
 *              back with the collar still standing, because a neck vertex is
 *              skinned to two nodes at once - the one under her chin is 55 %
 *              torso and 45 % head, measured - so the patch of skin a bind
 *              pose shape covers MOVES as the head turns.
 *   round S    a PLANE carried by the head node. It cannot be dodged by a
 *              turn about the neck axis, which is what it was chosen for -
 *              but it TILTS with the head, and a plane through the head joint
 *              tilted forty degrees runs through her chest and her shoulders.
 *              The play test: "the cutting away has got worse - in animations
 *              Lara is partly rendered away over her whole body", at thirty
 *              hertz and at sixty, with the HDLOG line counting four, five and
 *              nine of the fifteen joints on the discarded side where two -
 *              the head and nothing else - is the healthy reading.
 *
 * A WEIGHT HAS NEITHER FREEDOM. It is not measured in space at all.
 *
 * WHERE THE THRESHOLD COMES FROM (ModelProbe -headw, over the eight wardrobe
 * bodies against ANDREA1's Lara skeleton). `drop` is how many vertices the
 * threshold takes, `r>s8` the widest of them below the chin - the collar
 * question - and `top` where the highest surviving vertex within a neck's own
 * radius then stands, in units down the neck axis from the head joint:
 *
 *   cut     CLASSIC          RUSSIA           VCI              TR3 (all four)
 *           drop r>s8 top    drop r>s8 top    drop r>s8 top    drop  r>s8 top
 *   any     6860 57.3 25.0   5236 28.3 10.5   8367 71.2 25.0   9622 100.3 26
 *   0.05    6729 24.5 25.0   5141 25.0 10.5   8198 71.2 25.0    6729 24.5 26
 *   0.10    6711 24.5 18.7   5093 24.5 10.5   8162 71.2 18.7    6711 24.5 19
 *   0.15    6709 24.5 18.7   5062 24.5 10.5   8153 71.2 11.6    6709 24.5 19
 *   0.25    6689 24.5 11.6   5006 24.5 10.5   8136 71.2 11.6    6689 24.5 12
 *   0.50    6645 24.5 -1.6   4818 24.5 -1.6   8086 71.2 -1.6    6638 24.5  0
 *
 * TWO THINGS IN THAT TABLE FIX THE NUMBER.
 *
 *   ANY WEIGHT AT ALL IS TOO MUCH. The four TR3 guest bodies (#122) carry a
 *   whisper of head weight - under 0.05 - over the WHOLE body, down to the
 *   feet: 9,622 vertices against the 6,729 that are really her head, the
 *   deepest of them 688 units down the axis. Take "any" and three quarters of
 *   a TR3 Lara disappears. The wardrobe's own bodies say the same thing more
 *   quietly: at "any" the widest thing taken below the chin is 57 units out
 *   on OUTFIT_CLASSIC, which is her shoulder.
 *
 *   HALF IS TOO LITTLE. At 0.50 the highest surviving vertex inside a neck's
 *   radius stands 1.6 units ABOVE the head joint on four of the bodies - a
 *   ring of neck around the camera, which is the tube the play test
 *   photographed.
 *
 * Between 0.05 and 0.35 every one of the eight bodies reads the same 24.5 (25
 * on the Russia coat) for the widest thing taken below the chin, which is the
 * neck's own radius and no collar. 0.15 is taken out of that plateau: three
 * times clear of the TR3 conversion's noise, and the one threshold at which
 * the rim below is a SINGLE texture page on all eight bodies. */
#define HD_LARA_HEAD_CUT_WEIGHT	0.15f	// of 1: a vertex the head owns this much of goes

/* WHAT SURVIVES THE CUT, AND WHY THE VCI COLLAR RING DOES NOT. The catsuit's
 * collar ring stands at radius 70 and is skinned to the HEAD outright, so it
 * goes with the head at every threshold in the table - it turns with her head
 * in the third person, which is what it is for, and in the first person there
 * is nothing left for it to sit on. The Russia coat's own collar is torso
 * skinned, carries no head weight anywhere, and stays.
 *
 * THE DIVING SUIT (OUTFIT_SUBSUIT) IS OUT OF THIS, as it has been since fix
 * round L: its helmet sits on the TORSO node and carries no head weight, so
 * the cut does not touch it. The user's decision stands - the suit neither
 * gets work of its own nor constrains the cut - and what it does is measured
 * and reported. What it does is nothing visible: the camera sits INSIDE the
 * closed helmet, first person culls back faces, and a closed surface seen
 * from within is back facing everywhere. */

/* THE LID THAT CLOSES IT (#123, fix round S, S1; rebuilt on the RIM in fix
 * round U, U1).
 *
 * A FAN OVER THE BODY'S OWN RIM, and every word of that is the design.
 *
 * THE RIM is the set of vertices that do NOT reach the threshold but share a
 * triangle with one that does: the ring of skin the cut leaves standing, and
 * the exact boundary of the hole it opens. build_neck_cap finds it in the
 * .TRM while the file is still a file, sorts it by angle about the neck axis,
 * and builds a fan from the middle out to it.
 *
 * ITS RIM VERTICES ARE COPIES OF THE BODY'S, down to the three bones and the
 * three weights. That is what makes the seal exact and permanent: the lid's
 * edge is skinned by the same sum over the same joints as the skin it meets,
 * so the two land on the same point in EVERY pose, to the bit. There is no
 * margin, no overlap and no gap to leave, and no animation can open one -
 * which is the failure of every lid before it, all of which were rigid on one
 * node while the skin they met was not.
 *
 * ITS COLOUR IS THE RIM'S OWN, AND IT INVENTS NO TEXEL ANYWHERE (U1b). Each
 * spoke of the fan wears the texture coordinate of the rim vertex it hangs
 * from, unchanged all the way in, and the fan wears the page most of the rim
 * wears - so the lid is the top of whatever the outfit really has there: skin
 * on OUTFIT_CLASSIC (page 8001), the coat on OUTFIT_RUSSIA (8042), the suit on
 * OUTFIT_SUBSUIT (8047), the TR3 guests' own bodies (8201). The MIDDLE is a
 * copy per spoke for that reason - one point, one normal, one skinning, one
 * texel each - so a triangle of the lid spans two texels that are neighbours
 * on her skin and samples nothing between them.
 *
 * THE FIRST CUT OF THIS ROUND GAVE THE MIDDLE THE AVERAGE of the rim's texels,
 * and the play test came back with "a dark point in the middle of the cap,
 * running dark towards it like a cone" - on the four TR3 guest bodies only.
 * Measured (ModelProbe -headw, the rim's texels at the shipping threshold):
 * every TR5 page has the neck as ONE COLUMN six texels wide (u 235..241 on
 * 8001, u 119..125 on 8042, u 16..40 on 8047), and the average of a column is
 * in the column. The TR3 guests' page has it as TWO BANDS (u 60..170, one at
 * v 198..202 and one at v 154..158) and the average lands in the gap between
 * them. Picking a better single texel would only make the lottery smaller,
 * because the interior interpolates towards whatever the middle wears; not
 * inventing one is the only rule with nothing left to measure.
 *
 * Fix round S's one measured THROAT texel was a better answer than round I's
 * "whatever page the rim happens to sit on", and this is better than both: it
 * is not one colour at all, and it joins the skin at the seam because it IS
 * the skin at the seam.
 *
 * THE RINGS. The fan is subdivided towards its middle until it carries at
 * least HD_LARA_NECK_CAP_SEGMENTS triangles. That is NOT about the outline -
 * the outline is the body's own rim and cannot be improved by adding points
 * to it, only spoilt - it is about the LIGHT: the HD path lights per vertex
 * (hd_model.hlsl, ModelVS), and a fan of one middle and twenty rim points
 * shades as twenty flat wedges. The subdivision is radial, so the boundary is
 * untouched.
 *
 * THE OUTLIER GUARD. A rim vertex further than HD_LARA_NECK_CAP_RMAX off the
 * neck axis is not part of a neck and is dropped before the sort - a patch of
 * head-weighted geometry somewhere else on the body would otherwise put a
 * spoke of the fan across her chest. Measured: at 0.15 the widest rim vertex
 * over all eight bodies is 27.6 (the diving suit's collar), so the guard
 * never fires on the wardrobe as it stands. It is there for the ninth body.
 *
 * WHY A LID AT ALL, and the answer is the user's two photographs of fix round
 * S's test build with no cut of any kind: looking down into the open neck in
 * a dive roll one sees the undersides of her own collar and straps, and on a
 * landing the camera leaves the head and the nape fills the picture. Both are
 * reached THROUGH the open neck, and both stop existing when the hole is
 * closed. */
#define HD_LARA_NECK_CAP_SEGMENTS 48	// triangles in the fan, at least
#define HD_LARA_NECK_CAP_RMAX	34.0	// a neck and never a collar: a rim stops here

/* WHERE THE THROAT'S OWN TEXEL IS (#123, fix round K, K1; the inner skin it
 * was measured for is WITHDRAWN in fix round S, S1(4), and the lid stopped
 * needing it in fix round U, U1).
 *
 * WHAT THE INNER SKIN WAS. Round K drew the whole body a SECOND time in first
 * person, keeping exactly the faces the first draw culls, so that a ray
 * through the neck hole met the far wall of her own chest rather than the
 * room. It wore ONE texel and a turned normal, because the inside of a catsuit
 * is black and "black inside it still" was the user's finding and not the fix.
 *
 * WHY IT IS GONE. The LID closes the hole, and the second draw was measured
 * out rather than argued out. ModelProbe -firstperson, forty pictures: five
 * outfits at three pitches and both eyes the game uses, and on top of that the
 * head turned forty degrees each way, tilted thirty each way, and the torso
 * bent twenty-five each way and turned thirty - every one of them IDENTICAL TO
 * THE PIXEL with the inner draw and without it. Against that it cost a second
 * pass over 27,000 triangles per first person frame and one real fault: a
 * strap or a flap is a single layer of triangles whose two sides sit at the
 * same depth to the bit, and the inner draw won that tie, which is the torn
 * edges and missing shoulder pieces of fix round R.
 *
 * WHAT THE MEASUREMENT IS STILL FOR. It is no longer the lid's colour - the
 * lid wears the rim's own texture since fix round U - but it is still the one
 * line of the build log that says WHICH PAGE of an outfit is Lara's throat,
 * which is how a lid that comes out on the coat rather than on her skin can be
 * told from a lid that came out wrong. The rule, the two rules it replaced and
 * the measurement that killed them are at measure_neck_skin in hd_lara.cpp. */
#define HD_LARA_NECK_SKIN_R	30.0	// off the axis: the stump and no shoulder
#define HD_LARA_NECK_SKIN_S	30.0	// down it: where the shoulders join
#define HD_LARA_NECK_SKIN_MIN	24	// vertices before a page may be the neck

/* THE TWO LIDS THAT WERE THERE BEFORE (#123, fix round I, I3; re-aimed in fix
 * round J, J1; WITHDRAWN in fix round K, K1(b); a flat disc in fix round S,
 * S1; the RIM fan above since fix round U).
 *
 * Rounds I and J closed the hole with a DOME of 32 spokes and four rings, cut
 * from the outfit's own collar ring and sunk into the chest, and the play test
 * sent it back: with the eye on the head joint - where the first person camera
 * sits whenever a wall is in front of Lara - it was a GREAT FLAT
 * SKIN-COLOURED PLATE across the whole view, and on OUTFIT_RUSSIA it wore the
 * BACKPACK STRAPS, because the majority page of that outfit's collar ring is
 * 8018. It was as wide as her torso because the cut it closed was.
 *
 * Round S's was a flat DISC in the cut's plane, a neck wide and rigid on the
 * head node, wearing the one throat texel. It was the right size and the right
 * colour, and it went with the plane that defined it: a disc rigid on the head
 * meets skin that is only part head, so the two part company as she moves.
 * Fix round U's lid is not a shape at all - it is a copy of the rim. */

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

	/* Not a file: a fan built on the RIM the head cut leaves, skinned exactly
	 * as that rim is (#123, fix round S, S1; rebuilt in fix round U, U1).
	 * Drawn in first person only. */
	HD_LARA_PART_NECK_CAP,

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

/* THE FIRST PERSON HEAD CUT, IN THE ONLY TERMS IT HAS (#123, fix round U,
 * U1).
 *
 * `node` comes back as the node whose weight decides - Lara's head, node 14 -
 * and `bar` as how much of that weight sends a fragment away. Neither is a
 * fact about the level, the pose or the camera: they are the two numbers the
 * shader needs, and the whole point of the round is that there is no third.
 *
 * FALSE when there is no HD body to cut - no wardrobe, or the outfit was never
 * built - and then the caller draws with no cut at all, because half a cut is
 * worse than none.
 *
 * There is no bind pose in it any more. Fix round S's plane needed one (the
 * neck axis and a depth down it), and every shape before that needed one too;
 * a weight is written in the file. */
bool HDLaraHeadCut(int* node, float* bar);

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

/* WHAT IS IN THE HAND, as opposed to the hand itself (#123, fix round I, I4).
 *
 * A HAND_ model is two things on two texture pages - the hand and the weapon -
 * and one sheen for the draw makes either the skin metal or the gun matt. So
 * HDLaraPartSheen answers with the HAND's material (skin, or leather for a
 * gloved outfit) and this one with the WEAPON's; HDLaraLevelBuild hangs the
 * second on the pages the rest hands have not got, through
 * HDModelSetPageSheen. Both 0 for a part that is not a hand. */
void HDLaraHeldSheen(int part, float* strength, float* power);

/* AND LARA'S SKIN ITSELF (#123, fix round K, K1; this note corrected in fix
 * round U, U5), out of the same material table: what her SKIN shines like as
 * against the outfit she wears. It was written for the inner skin draw, which
 * fix round S withdrew; what asks for it now is the neck LID, whose whole
 * business is to be the top of her neck, and the hands, which is what made
 * the wrist seam one material instead of two. One function so that the number
 * is written down once. */
void HDLaraSkinSheen(float* strength, float* power);

/* AND WHICH PAGES OF THE BODY WEAR IT (#123, fix round K, K2): true for a
 * .DDS page the outfit that is on names as Lara's own skin, which is what
 * makes the wrist seam one material instead of two. The table and the
 * measurement behind it are at kOutfit in hd_lara.cpp. */
bool HDLaraSkinPage(int page);

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
