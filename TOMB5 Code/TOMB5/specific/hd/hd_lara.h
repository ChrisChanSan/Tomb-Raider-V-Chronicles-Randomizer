/*
 * Modified 2026 by ChrisChanSan.
 *
 * The rules of the Lara composite - the body is an OUTFIT_ model, the face a
 * separate LARA_HEAD, the braid a six bone HAIR_OG, the hands come from the
 * HAND_ model of the slot whose mesh sits at the hand node and the worn
 * weapons from that slot's HOLSTER_ model - are carried over from TROne
 * (Copyright 2026 Todesengel, GPLv3, TROne-main (copy of 2026-10-06, commit
 * 75c4c843), host\render\game_renderer.cpp: lara_flush, lara_build_models,
 * lara_load, draw_hair, and games\tr5\glue\hostglue_file5.cpp:
 * RegisterLaraWardrobe), TROne's own design. The names of the parts are
 * TROne's; the SECTION table under them is measured here (ModelProbe -lara),
 * because TROne's TR5 glue of its copy of 2026-09-05 had ANDY and JOBY the
 * wrong way round (it gave the Russia outfit to the Irish levels and called
 * the Russian ones young Lara; fixed in TROne-main, copy of 2026-10-06,
 * commit 75c4c843) - see the note in the .cpp. The renderer under all of it
 * is new: TROne is OpenGL 3.3, this is Direct3D 9 through hd_model. See
 * THIRD-PARTY.md.
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
#include "hd_softbody.h"

// Lara in HD: the composite.
//
// Every other object in this game is one slot and one .TRM. Lara is not: the
// engine draws her out of up to five classic slots at once - the skin from
// LARA_SKIN, the seams from LARA_SKIN_JOINTS, the hands from whichever weapon
// slot the mesh swap put there, the thigh straps from LARA_HOLSTERS* and the
// braid from HAIR - and the install ships the same as a wardrobe of separate
// models (4\ITEM). This module is the wardrobe: which files a level needs,
// what each of them covers, and which of them the draw seam puts on the card
// for a frame.
//
// This file is the wardrobe and not the seam. It knows nothing about the
// engine and is built into ModelProbe as well. What reads lara_matrices,
// lara.mesh_ptrs, hairs[0] and LaraNodeAmbient and turns them into the calls
// below is specific\hd\hd_laradraw.cpp, called from specific\drawlara.cpp.
//
// Two facts the module rests on, both measured over the remaster's 4\ITEM
// (ModelProbe -name <NAME> -nodraw, the per node vertex counts):
//
//   1. Every OUTFIT_ model skins to nodes 0..14 - the classic 15 node Lara
//      mesh tree in LM_ order, hips, thighs, shins, feet, torso, arms, hands,
//      head. Not one of them names a sixteenth. So the composite's skeleton
//      IS Lara's classic skeleton and the joint matrices the game already has
//      (lara_matrices, 15 of them) are the joint matrices the shader wants.
//      The one exception is OUTFIT_SUBSUIT, which has no feet: the diving
//      suit ends in flippers that are part of the shins.
//   2. Each further part covers exactly the nodes it belongs on:
//      LARA_HEAD is a facial rig (bones up to 31, which are not mesh tree
//      nodes at all) and therefore rides RIGIDLY on node 14; HAIR_OG has six
//      bones, which is exactly TR5's six braid links; HOLSTER_PISTOL and
//      HOLSTER_UZI sit on 1 and 4 (the two thighs), HOLSTER_DEAGLE on 4
//      alone (the big gun hangs on the right), HOLSTER_SHOTGUN / _HK /
//      _GRAPPLE on 7 (the torso - the weapon on the back); a HAND_ model
//      covers 9 and 10 on the right and 12 and 13 on the left, and
//      HAND_BARE_REST adds the two upper arm stubs 8 and 11.
//
// The "revolver" is a Desert Eagle. The slot kept its Tomb Raider IV name - REVOLVER_ANIM, LARA_REVOLVER_LASER,
// LARA_HOLSTERS_REVOLVER - and the weapon in it changed, exactly as
// CROSSBOW_ANIM became the grappling gun. The remaster ships both guns side by
// side in 4\ITEM and they are two different models, measured with ModelProbe:
// HAND_BARE_REVOLVER has a cylinder and a 1703 vertex HOLSTER_REVOLVER on node
// 4, HAND_BARE_DEAGLE is the slab-sided automatic with a 1270 vertex
// HOLSTER_DEAGLE on the SAME node 4. TR5 draws the Desert Eagle, so every
// part of the revolver slot takes the DEAGLE file: hand, laser hand, holster.
//
// Young Lara stays classic on purpose. OUTFIT_YOUNG, LARA_YOUNG_HEAD and
// HAIR_PIGTAIL exist in 4\ITEM and TROne draws the two pigtails for TR4, but
// the child body is not dressed here: it gets HD_LARA_OUTFIT_NONE and a
// reason in the log.

#define HD_LARA_NODES		15	// the classic mesh tree, LM_HIPS .. LM_HEAD
#define HD_LARA_HAIR_NODES	6	// TR5's six braid links

/* Where the braid hangs from, in the head node's own frame and in classic
 * units. game\hair.cpp (HairControl) walks the mesh tree to the head bone and
 * translates by exactly this before it writes hairs[0][0], the root of the
 * simulation - up a little, a touch to her left, and 48 units behind the
 * joint, which is the back of the skull. (Young Lara's two pigtails and the
 * second braid use two other triples; neither reaches the HD wardrobe, which
 * has no young body.)
 *
 * It is spelled out here because two things outside hair.cpp have to agree
 * with it: the seam, which reads hairs[0][n] and needs to know what the first
 * of them means, and ModelProbe, which has no simulation and builds its studio
 * braid from this point. */
#define HD_LARA_BRAID_ROOT_X	(-4)
#define HD_LARA_BRAID_ROOT_Y	(-4)
#define HD_LARA_BRAID_ROOT_Z	(-48)

/* The same point for an HD Lara.
 *
 * From the classic root the HD braid hangs 15.2 units clear of the skull, on
 * every outfit (TROne shows the same gap). The two braids are built
 * differently: the classic one draws its first link as a skinned connector
 * whose top ring is stashed vertices of the head mesh (game\hair.cpp,
 * GetCorrectStashPoints), so it starts on the skull whatever the root is; the
 * HD one has a rigid HAIR_OG link 0 that starts at the root and reaches only
 * 1.7 units back out of it.
 *
 * Measured with ModelProbe -braid, the same for all eight bodies because the
 * skull is one mesh:
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
 * The root is -33 whenever the HD wardrobe draws and -48 otherwise, written
 * in both halves of HairControl (the interpolated frame and the key frame)
 * out of one flag read once per frame (game\hair.cpp, `pack.on`).
 * hairs[0][0] seeds the whole chain and every link's velocity is how far it
 * moved since the last frame, so a root that differed between the two halves
 * would jump 15 units and back and feed the braid 15 units of velocity for
 * ever - a restless braid, thrown out long on one frame and tucked short
 * against her back on the next.
 *
 * The near root settles only against a box-shaped pack. At -33 the chain
 * hangs near enough to her neck for the neck sphere and a pack sphere to push
 * it in turn, and two spheres pushing a point in turn make a small limit cycle
 * rather than a rest; a box pushes a point out of one flat face along one axis
 * and leaves it there. ModelProbe -braidsim, sixty steps in the bind pose,
 * both torso anchors - the busiest link's travel per step, and how deep the
 * braid ends up inside the pack:
 *
 *     regime                          step 1  step 10  step 30  last  in pack
 *     classic: root -48, 6 spheres        14        1        0     0       38
 *     root -33, pack as spheres           34        4        1     1       41
 *     root -48, pack as spheres           14        1        0     0       38
 *     root -48, pack as a box             45       10        0     0        7
 *     root -33, pack as a box             63       18        0     0       12
 *
 * Under Lara's own animations (ModelProbe -braidanim, ANIM_BREATH) the
 * near root costs a little liveliness and no jumps: the tip's worst travel in
 * a frame goes from 10 units to 14 and its mean from 1.04 to 1.41, with no
 * frame over 40 in either. */
#define HD_LARA_BRAID_ROOT_Z_HD	(-33)

/* The backpack as two collision spheres. The engine uses the box below; these
 * remain as the comparison ModelProbe measures against.
 *
 * The classic HairControl pushes the braid out of five spheres, all on classic
 * Lara meshes - hips, torso, head, the two upper arms. The HD body's pack
 * stands 92 units behind the torso joint, where the classic torso sphere does
 * not reach, so without a pack collider the HD braid passes through it.
 *
 * Measured with ModelProbe -braid over the wardrobe, in the torso node's own
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
 * ModelProbe -braidsim, sixty steps in the bind pose: without the pack the
 * braid comes to rest 15 units inside it, with the two spheres 9, and the
 * chain settles either way. Any pack collider is placed in both halves of
 * HairControl; a pack that comes and goes from frame to frame is a push that
 * comes and goes with it. */
#define HD_LARA_PACK_X		(0)
#define HD_LARA_PACK_Y_TOP	(-165)
#define HD_LARA_PACK_Y_BOTTOM	(-100)
#define HD_LARA_PACK_Z		(-52)
#define HD_LARA_PACK_R		(36)

/* The same pack as a box, which is its real shape.
 *
 * A sphere of radius 36 tangent to the flat rear face touches that face at one
 * point; a hand's width to either side its surface has already curved 10 units
 * forward, and the plait is 35 units wide. So with spheres the braid sinks
 * into the pack beside the centre line however many are stacked.
 *
 * Measured with ModelProbe -braid, "the backpack as a BOX": the node 7
 * vertices more than 30 units behind the torso joint, in the torso node's own
 * frame - the same point set as the spheres above, as six numbers:
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
 * One box serves all seven, and the table is why: the depth is the SAME number in
 * every one of them (-92 to -30), the width agrees to three units, and the top
 * to none at all. The bottom is the only column that moves, and the two that
 * move it - the Russia coat and the Antarctica parka - are not deeper packs
 * but longer COATS: the rule "node 7 and more than 30 behind the joint" picks
 * up their back skirt as well. The box takes the pack's own bottom, -68, and
 * leaves the coat tail to the torso sphere, which for the Russia outfit is
 * nailed to radius 94 and covers it.
 *
 * The diving suit is left out as it is left out of the spheres: what stands
 * behind its torso is two thrusters, and it wears a helmet and no braid.
 *
 * The margin is the plait's own radius. HairControl moves POINTS and the braid
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

/* How much the rear face is preferred when the link is pushed out along the
 * smallest penetration. It is the face the braid belongs on: everything else
 * is a way into her back or under the pack, and a tie between two faces that
 * goes the wrong way is a plait that flips through the body for one frame.
 * The bias is a divisor on the rear face's own depth, so a rear push wins
 * unless another face is more than twice as close.
 *
 * Over the three animations ModelProbe -braidanim runs - breathing on the
 * spot, the run loop and the walk loop - the box with no rear preference gives
 * identical results in every column: the braid approaches from behind and the
 * rear face is the nearest one anyway. The preference is for poses the tool
 * does not reach, with Lara bent far forward, where the other faces lead into
 * her back; a value of 1 gives the same picture in everything measured. */
#define HD_LARA_PACK_BOX_BIAS	(2)

/* Why the drawn braid gets a roll of its own.
 *
 * The simulation itself is calm (HAIRLOG: over 924 frames where the root did
 * not move a unit, the tip moved less than two on 728, and long standing
 * stretches settle to exactly zero). What trembles is the angle.
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
 * Measured with ModelProbe -braidanim over ANDREA2's own animation table, 110
 * frames of ANIM_BREATH (Lara standing at a door):
 *
 *     variant                      tip travel per frame   worst yaw jump
 *     the classic body alone       1 unit, never more     180 degrees on 18
 *     the HD body, no roll         1 unit, never more     180 degrees, frames
 *
 * The braid stands still to within a unit and spins half a turn on one frame
 * in six. That is the tremble.
 *
 * The cure has to leave the direction alone - the drawn bone's own +z IS the
 * link direction, and hd_laradraw's braidcheck proves that stepping one bone
 * length along it lands on the next link. A rotation about that same +z moves
 * nothing at all, so the roll is free: after RotY and RotX comes a RotZ that
 * takes the noise back out, and the braid's plait then keeps facing the way
 * Lara faces instead of the way two integers rounded.
 *
 * Which angle to put there instead: ModelProbe -braidanim runs the candidates
 * over three animations and counts the worst one-frame turn of any bone, in
 * degrees, over the worst of the five physics variants:
 *
 *     rule                                    standing   running   walking
 *     none - the raw yaw                       180        7         7
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
 * The fifth rule is the one used. One reference for all six bones - a rigid plait
 * cannot hide two neighbours twisting against each other, and the classic
 * path's skinned connectors are exactly how TR5 hid it - and the reference is
 * the azimuth of the WHOLE braid, root to tip, because that vector is six
 * bones long where a link is one and its horizontal part is ten to thirty
 * times better conditioned. "Towards her facing" fails the walk for the same
 * reason the raw yaw fails standing: it is blended per link by a weight that
 * jumps when the link crosses the band, and a blend of two angles far apart
 * jumps with it.
 *
 * When it follows: `flat` is the horizontal span of the whole braid, out of a
 * chain 336 units long. Below NEAR the reference does not move at all - a
 * braid hanging plumb has nothing to say about its own azimuth - above FAR it
 * follows completely, and between the two it follows part of the way, so
 * there is no threshold the picture can cross in one frame. */
#define HD_LARA_BRAID_ROLL_NEAR	16
#define HD_LARA_BRAID_ROLL_FAR	48

/* The pseudo slots. hd_model stores its models in a map keyed by slot number,
 * and a wardrobe part has no slot of its own - LARA_SKIN is one slot and the
 * composite is twenty models. So each part gets a number of its own, far above
 * anything the engine hands out (the highest engine slot is 471; 32767 is the
 * ceiling of ITEM_INFO::object_number), and the wardrobe rides through
 * hd_model's normal machinery: the same page cache, the same skinning, the
 * same draw call. */
#define HD_LARA_SLOT_BASE	0x5A00

/* A second block of the same kind for the install's own hands beside a mod's
 * gun: one slot per part, holding the file of the same name out of the
 * install where a mod's hand model wears a foreign bundle's skin. See
 * HDLaraPartTwinSlot and the note in hd_lara.cpp. */
#define HD_LARA_TWIN_SLOT_BASE	0x5B00

/* The mirrored hand weights, and where the bar sits.
 *
 * A hand file is drawn once and mirrored, and an exporter that mirrors the
 * geometry without the bone names leaves vertices of one hand carrying weight
 * on the other. hd_trm.h has the rule; this is the one number in it, and it is
 * a measurement rather than a taste.
 *
 * Measured over every hand file of the IV-VI install and of three gun mods.
 * The install's are all in 4\ITEM - games 5 and 6 hold none at all - and
 * there are 94: thirty HAND_BARE_, thirty HAND_GLOVES_, thirty HAND_XRAY_ and
 * the four HANDS_BARE_. The mods (HK USP, IMI Uzis, Spas-12) ship seven hand
 * files between them. Over all 101, a crossed vertex's share on the far hand
 * is one of exactly two things:
 *
 *   4 per mille    ONE weight unit out of 255 - 3.92, which the share's own
 *                  arithmetic rounds to 4 - and nothing else, in every one of
 *                  the 6,677 crossed vertices of the install's own files
 *                  (5,884 of them, out of 485,491 vertices) and of the Uzi,
 *                  Spas-12 and gloved-pistol mods (793). That is the
 *                  exporter's rounding and it is not a mirroring error.
 *   371..500       the 46 crossed vertices of the HK USP mod's
 *                  HAND_BARE_PISTOL, four of them exactly 500 - half of the
 *                  vertex on the wrong hand. That is the sliver of one hand
 *                  seen stretched to the other. They are the only 46 in the
 *                  whole set.
 *
 * Nothing at all lies between the two, so the bar is an eighth: 31 times the
 * largest share the remaster's own files carry and a third of the smallest the
 * mod's do.
 *
 * What the fold costs, measured with ModelProbe -handfold, which skins every
 * vertex twice through the same node matrices the card is given and takes the
 * distance between the two answers.
 *
 * Which hand parts a run loads is decided by the LEVEL's weapon slots, so the
 * sweep walks all thirteen classic levels and covers all ELEVEN hand parts of
 * the table in hd_lara.cpp. Four of them - REST, FLARE, TORCH, CROWBAR - have
 * no crossed vertex at all and never move. Folding the other seven at a bar of
 * 0 moves them, largest of the seven:
 *
 *   0.56 units   with both forearms turned 50 degrees out. DEAGLE and
 *                DEAGLE_LASER;
 *                PISTOL is 0.51, UZI 0.52, SHOTGUN, HK and GRAPPLE 0.53.
 *   0.84         with both upper arms straight out sideways (8 90 0, 11 -90 0),
 *                again DEAGLE and DEAGLE_LASER; the others 0.80 to 0.81.
 *   0.97         with the upper arms out 90 AND the forearms folded 90 on top
 *                of that - every one of the seven, to the hundredth.
 *   2.00         UZI, in a contortion no animation reaches (8 120 60,
 *                9 120 0 and their mirrors).
 *
 * Small - and pointless, because one weight unit on the far hand cannot make a
 * sliver.
 *
 * With the bar the remaster's own files come out of the reader byte for byte as
 * they went in - all 94 of them: not one carries a crossed vertex above the bar
 * and not one carries a vertex whose weights miss 255 - which is a guarantee
 * rather than a measurement, and only a file with the real defect is touched at
 * all. */
#define HD_LARA_HAND_FOLD_BAR	125

/* The two sides, as hd_trm's fold wants them: the right arm's bones first,
 * because it is the side at positive x in Lara's own frame and that is what
 * breaks a vertex weighted exactly half and half. */
const HDTrmMirrorPair* HDLaraHandMirror(int* pairs);

/* The bar itself, settable for a comparison picture without the fold.
 * ModelProbe -nofold sets it past a thousand, which no share can reach, and
 * every hand part is then built from the file exactly as it stands. The engine
 * never calls this; the default is HD_LARA_HAND_FOLD_BAR and it is read at
 * HDLaraPlan. */
void HDLaraHandFoldBar(int perMille);

/* The same switch for the twin. Off makes a hand part ask for no companion at
 * all: a mod's hands in a mod's own skin. The engine never calls this either. */
void HDLaraHandsFromInstall(bool on);

/* The same switch for the head rule. One of the HD_WARDROBE_HEAD_* makes
 * every outfit wear that rule whatever the wardrobe decided, and -1 - the
 * default, and what the engine always leaves it at - gives the wardrobe back
 * its say. ModelProbe -head drives it, so that the picture without the rule
 * and the picture of a rule no outfit.txt names yet both come out of the very
 * build the game runs. Read at HDLaraPlan and at every draw, so it has to be
 * set before the level build it is meant for. */
void HDLaraHeadForce(int head);

/* The install's hands under a mod's gun: the companion slot of a hand part,
 * or -1 for a part that is not a hand. Nothing is ever loaded
 * into it unless the part's own model came out of a mod whose skin pages are
 * another bundle's - hd_model decides that, because only it has the file open
 * and the pages resolved. */
int HDLaraPartTwinSlot(int part);

/* The first person head cut.
 *
 * Every fragment whose head weight reaches HD_LARA_HEAD_CUT_WEIGHT is
 * discarded while the first person view runs. That is the whole rule. There
 * is no shape, no plane, no radius, no node mask and nothing that depends on
 * where the camera is or on what the animation is doing: a vertex's three bone
 * weights are written in the .TRM file and are the same in every pose.
 *
 * Why no shape in space works:
 *
 *   a sphere about the eye takes a bite out of her chest and chews a wrist
 *              brought up to the camera;
 *   a cylinder about the bind pose neck axis leaves the collar standing,
 *              because a neck vertex is skinned to two nodes at once - the
 *              one under her chin is 55 % torso and 45 % head, measured - so
 *              the patch of skin a bind pose shape covers moves as the head
 *              turns;
 *   a plane carried by the head node cannot be dodged by a turn about the
 *              neck axis, but it tilts with the head, and tilted forty
 *              degrees it runs through her chest and shoulders (HDLOG then
 *              counts four to nine of the fifteen joints on the discarded
 *              side, where two is the healthy reading).
 *
 * A weight is not measured in space at all.
 *
 * Where the threshold comes from (ModelProbe -headw, over the eight wardrobe
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
 * Two things in that table fix the number.
 *
 *   Any weight at all is too much. The four TR3 guest bodies carry a
 *   whisper of head weight - under 0.05 - over the WHOLE body, down to the
 *   feet: 9,622 vertices against the 6,729 that are really her head, the
 *   deepest of them 688 units down the axis. Take "any" and three quarters of
 *   a TR3 Lara disappears. The wardrobe's own bodies say the same thing more
 *   quietly: at "any" the widest thing taken below the chin is 57 units out
 *   on OUTFIT_CLASSIC, which is her shoulder.
 *
 *   Half is too little. At 0.50 the highest surviving vertex inside a neck's
 *   radius stands 1.6 units above the head joint on four of the bodies - a
 *   ring of neck, a tube, around the camera.
 *
 * Between 0.05 and 0.35 every one of the eight bodies reads the same 24.5 (25
 * on the Russia coat) for the widest thing taken below the chin, which is the
 * neck's own radius and no collar. 0.15 is taken out of that plateau: three
 * times clear of the TR3 conversion's noise, and the one threshold at which
 * the rim below is a SINGLE texture page on all eight bodies. */
#define HD_LARA_HEAD_CUT_WEIGHT	0.15f	// of 1: a vertex the head owns this much of goes

/* What survives the cut, and why the VCI collar ring does not. The catsuit's
 * collar ring stands at radius 70 and is skinned to the head outright, so it
 * goes with the head at every threshold in the table - it turns with her head
 * in the third person, which is what it is for, and in the first person there
 * is nothing left for it to sit on. The Russia coat's own collar is torso
 * skinned, carries no head weight anywhere, and stays.
 *
 * The diving suit (OUTFIT_SUBSUIT) is outside this and gets no handling of
 * its own: its helmet sits on the torso node and carries no head weight, so
 * the cut does not touch it. Nothing of it shows: the camera sits inside the
 * closed helmet, first person culls back faces, and a closed surface seen
 * from within is back facing everywhere. */

/* The lid that closes the cut: a fan over the body's own rim.
 *
 * The rim is the set of vertices that do NOT reach the threshold but share a
 * triangle with one that does: the ring of skin the cut leaves standing, and
 * the exact boundary of the hole it opens. build_neck_cap finds it in the
 * .TRM while the file is still a file, sorts it by angle about the neck axis,
 * and builds a fan from the middle out to it.
 *
 * Its rim vertices are copies of the body's, down to the three bones and the
 * three weights. That is what makes the seal exact and permanent: the lid's
 * edge is skinned by the same sum over the same joints as the skin it meets,
 * so the two land on the same point in every pose, to the bit. There is no
 * margin, no overlap and no gap, and no animation can open one - which a lid
 * rigid on one node cannot promise, because the skin it meets is only partly
 * head.
 *
 * Its colour is the rim's own, and it invents no texel anywhere. Each
 * spoke of the fan wears the texture coordinate of the rim vertex it hangs
 * from, unchanged all the way in, and the fan wears the page most of the rim
 * wears - so the lid is the top of whatever the outfit really has there: skin
 * on OUTFIT_CLASSIC (page 8001), the coat on OUTFIT_RUSSIA (8042), the suit on
 * OUTFIT_SUBSUIT (8047), the TR3 guests' own bodies (8201). The middle is a
 * copy per spoke for that reason - one point, one normal, one skinning, one
 * texel each - so a triangle of the lid spans two texels that are neighbours
 * on her skin and samples nothing between them.
 *
 * A single middle texel, such as the average of the rim's, fails: on the TR3
 * guest bodies it shows as a dark point in the middle of the lid, darkening
 * towards it like a cone. Measured (ModelProbe -headw, the rim's texels at the
 * threshold used): every TR5 page has the neck as one column six texels wide
 * (u 235..241 on 8001, u 119..125 on 8042, u 16..40 on 8047), and the average
 * of a column is in the column. The TR3 guests' page has it as two bands (u
 * 60..170, one at v 198..202 and one at v 154..158) and the average lands in
 * the gap between them. Any single texel is a lottery, because the interior
 * interpolates towards whatever the middle wears; not inventing one is the
 * only rule with nothing left to measure.
 *
 * The rings. The fan is subdivided towards its middle until it carries at
 * least HD_LARA_NECK_CAP_SEGMENTS triangles. That is NOT about the outline -
 * the outline is the body's own rim and cannot be improved by adding points
 * to it, only spoilt - it is about the LIGHT: the HD path lights per vertex
 * (hd_model.hlsl, ModelVS), and a fan of one middle and twenty rim points
 * shades as twenty flat wedges. The subdivision is radial, so the boundary is
 * untouched.
 *
 * The outlier guard. A rim vertex further than HD_LARA_NECK_CAP_RMAX off the
 * neck axis is not part of a neck and is dropped before the sort - a patch of
 * head-weighted geometry somewhere else on the body would otherwise put a
 * spoke of the fan across her chest. Measured: at 0.15 the widest rim vertex
 * over all eight bodies is 27.6 (the diving suit's collar), so the guard
 * never fires on the wardrobe as it stands. It is there for a further body.
 *
 * Why a lid at all: looking down into the open neck in a dive roll one sees
 * the undersides of her own collar and straps, and on a landing the camera
 * leaves the head and the nape fills the picture. Both are seen through the
 * open neck, and both stop existing when the hole is closed. */
#define HD_LARA_NECK_CAP_SEGMENTS 48	// triangles in the fan, at least
#define HD_LARA_NECK_CAP_RMAX	34.0	// a neck and never a collar: a rim stops here

/* Where the throat's own texel is.
 *
 * This measurement does not colour anything - the lid wears the rim's own
 * texture - but it is the one line of the build log that says which page of an
 * outfit is Lara's throat, which tells a lid that comes out on the coat rather
 * than on her skin from a lid that came out wrong. The rule and the
 * measurement behind it are at measure_neck_skin in hd_lara.cpp.
 *
 * The first person view draws no second, inside-out pass of the body behind
 * the neck hole: with the lid in place, ModelProbe -firstperson over forty
 * pictures (five outfits, three pitches, both eyes the game uses, head turned
 * and tilted, torso bent and turned) is identical to the pixel with and
 * without it. Such a pass would cost a second draw of 27,000 triangles per
 * frame and would win the depth tie on a strap or flap - a single layer of
 * triangles whose two sides sit at the same depth - tearing its edges. */
#define HD_LARA_NECK_SKIN_R	30.0	// off the axis: the stump and no shoulder
#define HD_LARA_NECK_SKIN_S	30.0	// down it: where the shoulders join
#define HD_LARA_NECK_SKIN_MIN	24	// vertices before a page may be the neck

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

/* Where each node's matrix and mesh bit stand in the engine's LMX_ order,
 * the order DrawLara walks her skin in: the hips, the legs and the torso
 * where the tree has them, the head moved up to 8 and the arms one further
 * on. game\delstuff.h spells the same table for the engine as
 * lara_lmx_of_node, and the seam asserts at compile time that the two are
 * one table; this one is here for a caller with no engine behind it. */
constexpr int HD_LARA_LMX_OF_NODE[HD_LARA_NODES] =
{
	0,			// hips
	1, 2, 3,		// the left leg
	4, 5, 6,		// the right leg
	7,			// torso
	9, 10, 11,		// the right arm
	12, 13, 14,		// the left arm
	8			// head
};

/* The nodes Lara's mesh_bits show, bit n for node n of HDLaraNode.
 *
 * The engine's own test, the one DrawLara__1 puts each skin mesh under
 * (specific\drawlara.cpp): `mesh_bits >> 16 & 1 << (15 - i)` with i the
 * LMX_ index. Her fifteen bits sit in the high half and count backwards,
 * so the hips are bit 31, a thigh bit 30 or 27 and the head bit 23; the
 * low half belongs to the classic joint pieces, which an HD body does not
 * have. -1, the bits of nearly every frame, shows all fifteen; the
 * cutscenes that hide her legs write 0x81FF01FF, which shows the hips, the
 * torso, the head and both arms. */
unsigned int HDLaraNodesShown(unsigned long meshBits);

/* Which body Lara wears. The section table that picks one is in the .cpp.
 *
 * These nine are the level-facts path and nothing more. They are the outfits
 * HDLaraOutfitFor can reach out of what the level file says, and their numbers
 * are written into level headers (data[3]) and into .hdm files, so they never
 * move. The wardrobe is bigger than they are and is a scan (hd_wardrobe.h):
 * any entry of it can be worn, by the player's own choice in the registry
 * (hd_laradraw.cpp, hd_outfit), and such an outfit is named
 * HD_LARA_OUTFIT_WEAR + its scan index. */
enum HDLaraOutfit
{
	HD_LARA_OUTFIT_NONE = 0,	// stay classic - and the log says why
	HD_LARA_OUTFIT_CLASSIC,		// Rome: shorts and a green top
	HD_LARA_OUTFIT_RUSSIA,		// the Russian section: coat and cap
	HD_LARA_OUTFIT_SUBSUIT,		// the diving suit of Deepsea Dive
	HD_LARA_OUTFIT_VCI,		// the black catsuit of the VCI levels
	HD_LARA_OUTFIT_TR3_ANTARC,	// the four TR3 guests the randomizer rolls
	HD_LARA_OUTFIT_TR3_NEVADA,
	HD_LARA_OUTFIT_TR3_COAST,
	HD_LARA_OUTFIT_TR3_CATSUIT,
	HD_LARA_OUTFIT_COUNT
};

/* An outfit out of the scan: this plus its index in
 * hd_wardrobe. Far above HD_LARA_OUTFIT_COUNT so that the two kinds cannot be
 * confused, and far below the end of an int. Every call below that takes an
 * `outfit` takes one of these as well. */
#define HD_LARA_OUTFIT_WEAR	1000

inline int HDLaraOutfitOfWear(int wardrobeIndex)
{
	return wardrobeIndex >= 0 ? HD_LARA_OUTFIT_WEAR + wardrobeIndex : HD_LARA_OUTFIT_NONE;
}

// The scan index behind such a number, or -1 for one of the nine above.
inline int HDLaraWearOfOutfit(int outfit)
{
	return outfit >= HD_LARA_OUTFIT_WEAR ? outfit - HD_LARA_OUTFIT_WEAR : -1;
}

// The parts of the composite. Their order is the order they are drawn in.
enum HDLaraPart
{
	HD_LARA_PART_BODY = 0,		// the OUTFIT_ model, nodes 0..14

	/* TR5's own head, borrowed for a body that has none: node 14 of the
	 * install's OUTFIT_CLASSIC, cut out by node (HDModelSlot::onlyNode) and
	 * skinned to node 14 exactly as the body's own head node is.
	 *
	 * What it is, measured: 6,860 vertices and 9,485 triangles, 9,483 of them
	 * on page 8002 - which is HAIR. Two triangles of skin at the nape and
	 * nothing else. The head node of a IV-VI outfit IS her hair, and the
	 * separate LARA_HEAD that rides on the same node is the face and the
	 * front of the skull, whose back is open; the two are each other's halves.
	 *
	 * A body of the Tomb Raider I-III bundle carries headgear on that node
	 * and no hair at all, so its Lara comes out with the open back of
	 * LARA_HEAD in the picture - a bald skull with a face on it. This part is
	 * the missing half, and hd_wardrobe.h's head rule says who wears it. */
	HD_LARA_PART_HEAD,

	/* LARA_HEAD on node 14, and its throat on node 7.
	 *
	 * The file is more than a face: it reaches 27.8 units below the head
	 * joint, 541 of its 5,913 vertices lie below that joint and 102 of them
	 * more than 10 below - a strip of throat that runs down into the body's
	 * own neck. Its facial rig carries a skeleton as well, and the strip is
	 * weighted to it: of the rig's 33 joints, joint 4 stands on the bind of
	 * node 14, joint 1 on the bind of node 7, the torso, at (-1, -49, 11), and
	 * joints 3 and 6 at a thigh and at the hips. Those three stand 199.3 to
	 * 270.8 units from the head joint, beyond the limit hd_model's neck rule
	 * draws (HDModelSlot::neckNode), and 171 vertices carry weight on them.
	 * They hang on node 7 with the file's own weights and the rest of the face
	 * on node 14, so the throat bends where the file says it bends.
	 *
	 * The file's weights match the body's own: the 66 throat vertices that
	 * lie on OUTFIT_CLASSIC's neck (within one unit in the bind pose) carry a
	 * head weight 0.02 from the neck vertex under them on average (0.06 on
	 * OUTFIT_VCI), and the two skins stay together when the head pitches 70
	 * degrees back - 0.8 units apart on average and 4.0 at most, 1.5 and 9.5
	 * on OUTFIT_VCI - where the whole face rigid on node 14 parts from the
	 * neck by 15.1 on average and 34.6 at most, and folds out under the jaw
	 * as a flap.
	 *
	 * Taking the weight of every vertex below the head joint from the body's
	 * nearest neck vertex instead is the rule that does not hold: 387 of
	 * those vertices are the jaw and the chin, on average 25 units from the
	 * nearest vertex of the neck, and that rule hands them 0.46 to 0.61 of
	 * the head on average (OUTFIT_VCI to OUTFIT_RUSSIA) - the jaw tears open
	 * at 60 and 70 degrees and in the crouch.
	 *
	 * First person does not draw the face at all, so the neck cut and its lid
	 * are untouched by this. */
	HD_LARA_PART_FACE,
	HD_LARA_PART_BRAID,		// HAIR_OG, its own six node chain

	/* Not a file: a fan built on the rim the head cut leaves, skinned exactly
	 * as that rim is. Drawn in first person only. */
	HD_LARA_PART_NECK_CAP,

	HD_LARA_PART_HAND_REST,		// nothing in the hands
	HD_LARA_PART_HAND_PISTOL,
	HD_LARA_PART_HAND_UZI,
	HD_LARA_PART_HAND_SHOTGUN,
	HD_LARA_PART_HAND_HK,
	HD_LARA_PART_HAND_DEAGLE,	// TR5's "revolver" slot is a Desert Eagle
	HD_LARA_PART_HAND_DEAGLE_LASER,
	HD_LARA_PART_HAND_GRAPPLE,	// TR5's "crossbow" slot is the grappling gun, with
								// or without the sight bit
	HD_LARA_PART_HAND_FLARE,	// left hand only
	HD_LARA_PART_HAND_TORCH,	// left hand only
	HD_LARA_PART_HAND_CROWBAR,	// right hand only

	HD_LARA_PART_HOLSTER_PISTOL,	// both thighs
	HD_LARA_PART_HOLSTER_UZI,	// both thighs
	HD_LARA_PART_HOLSTER_DEAGLE,	// the right thigh alone

	/* The pouches the guns sit in.
	 *
	 * A HOLSTER_ file is the weapon and nothing else - HOLSTER_PISTOL.TRM is
	 * two pistols, 4268 vertices, and no leather anywhere (ModelProbe -name
	 * HOLSTER_PISTOL, a picture of two guns hanging in mid air). The pouch,
	 * the thigh strap and the hip belt they hang from are built into the
	 * OUTFIT model, one page of it: 8017 in OUTFIT_CLASSIC and OUTFIT_RUSSIA,
	 * 8205 in OUTFIT_TR3_COAST and OUTFIT_TR3_CATSUIT.
	 *
	 * Three of the eight wardrobe bodies have none - OUTFIT_VCI,
	 * OUTFIT_TR3_ANTARC and OUTFIT_TR3_NEVADA - because vanilla TR5 gives
	 * catsuit Lara no pistols and the two TR3 guests wear none in their own
	 * game either. The randomizer hands her a gun in every level, and
	 * without a rig the guns hang in the air beside her thigh.
	 *
	 * This part is the missing rig, cut out of OUTFIT_TR3_CATSUIT by page
	 * (HDModelSlot::onlyPage). The London catsuit is the donor rather than
	 * the Rome one because its rig is black leather over a black leg, which
	 * is what the VCI catsuit is; OUTFIT_CLASSIC's is brown suede over bare
	 * skin.
	 *
	 * Who wears it is a table and not a rule: OUTFIT_VCI and nobody else.
	 * On the Antarctica parka and the Nevada vest the straps clip through
	 * the coat - those two never wore a thigh rig in TR3 and their geometry
	 * is finished without one. hd_lara.cpp's
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

/* What the level hands over. The two bind poses are the engine's own: the
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

	/* Which of Lara's slots the level actually loaded, so that the wardrobe
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
	/* Of those, models that are not a part at all - the install's own hands
	 * loaded beside a mod's gun. Counted with the rest in every field below - loaded, vertices, indices, ranges, pages and both
	 * byte counts, because it is a model on the card like any other - and
	 * named here so that a line saying "19 on the card" out of 18 parts is
	 * not a puzzle. */
	int twins;
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

/* The frame: what the seam in DrawLara knows about this one frame, in the
 * engine's own terms - slot numbers, exactly the ones DrawLara already has in
 * its hands. HDLaraFrameParts turns them into the list of parts to draw.
 *
 * rightHandSlot / leftHandSlot: which slot the mesh at LM_RHAND / LM_LHAND
 * comes from. The engine keeps that in lara.mesh_ptrs[] as a POINTER, so the
 * seam has to compare it against meshes[objects[s].mesh_index + 2 * LM_xHAND]
 * for the handful of candidate slots - which is cheap and is what the classic
 * path itself does in larafire.cpp.
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
// A short human name for the log ("the VCI catsuit"), never 0. For a scanned
// outfit it is the wardrobe's display name ("TR2 Home", "AOD Wetsuit").
const char* HDLaraOutfitLabel(int outfit);
/* The wardrobe key of whatever this number stands for ("46/OUTFIT_VCI"), or
 * "" when this machine's scan has no entry for it. Never
 * null. The nine have one as well, which is what lets the player's chosen key
 * and a level header's data[3] be compared as the same kind of thing. */
const char* HDLaraOutfitKey(int outfit);
// An outfit from a name: the .TRM stem, the bare tail ("VCI", "TR3_COAST"),
// the randomizer's own guest id ("tr3_london") or a wardrobe key
// ("46/OUTFIT_TR2_HOME"). HD_LARA_OUTFIT_NONE when the name is none of them.
int HDLaraOutfitFromName(const char* name);

/* Which outfit this level wears, out of the engine's own facts and nothing
 * else. The caller passes what it has:
 *
 *   laraDrawType  LaraDrawType, i.e. LARA_NORMAL .. LARA_PONYTAIL. It comes
 *                 from data[0] of the level header and the randomizer writes
 *                 it, so it is the outfit's own word and not the level's.
 *   russiaCap     the engine's own predicate for "she is wearing the cap":
 *                 (gfLevelId in the Russia range && LaraDrawType ==
 *                 LARA_NORMAL) || LaraDrawType == LARA_BUNHEAD. That exact
 *                 expression already stands three times in game\hair.cpp and
 *                 once in game\laraskin.cpp, and it is the only thing in this
 *                 engine that knows the difference - a vanilla Russia level
 *                 carries LaraType 0, the same byte Rome carries. Passing the
 *                 engine's own test in rather than rebuilding it here is what
 *                 keeps the two from drifting apart.
 *   youngLara     (gfLevelFlags & GF_YOUNGLARA) || LaraDrawType ==
 *                 LARA_YOUNG. Either and Lara stays classic.
 *
 *                 The Irish level range is deliberately not in that test.
 *                 The engine builds the child body at exactly two places -
 *                 LaraInitialiseMeshes in game\laramisc.cpp and its twin in
 *                 game\deltapak.cpp - and both ask the range and the flag.
 *                 The randomizer clears GF_YOUNGLARA out of SCRIPT.DAT for
 *                 all three Irish levels (core\ScriptDat.cpp) and replaces
 *                 Lara's skin with an adult's, because young Lara can carry
 *                 no weapon; the range alone would call a grown Lara a child
 *                 and draw every Irish level classic. The flag and the
 *                 LaraType byte both flip with the change.
 *   tr3Outfit     one of the four HD_LARA_OUTFIT_TR3_* when the randomizer
 *                 dressed this level in a Tomb Raider III outfit, otherwise
 *                 HD_LARA_OUTFIT_NONE. It is data[3] of the level header,
 *                 passed in raw: the randomizer writes the
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

/* Whose head the outfit that is on wears: one of the HD_WARDROBE_HEAD_* of
 * hd_wardrobe.h. Settled at level build out of the wardrobe entry and asked
 * for by the draw, which turns it into two bits and nothing more - whether the
 * body's own head node is drawn, and whether HD_LARA_PART_HEAD is. */
int HDLaraHeadMode();

/* The node mask the body draws with: every node but the head node when that
 * mode is `tr5`, and every node otherwise. The caller
 * ANDs it into the frame's own mask. Settled at level build, so the draw does
 * not ask the wardrobe anything. */
unsigned int HDLaraBodyMask();

bool HDLaraHasPart(int part);
/* The install's own hands beside a mod's gun. True when
 * hd_model really loaded the twin for this part - which it does only for a mod
 * hand painted with another bundle's skin. The caller draws it with the SAME
 * matrices, the same node mask and the same light as the part itself; it is
 * the same hand in the same pose, wearing the install's own skin pages. */
bool HDLaraHasPartTwin(int part);
// Whether the level asked for the part at all - see HDLaraLevel::slots. A part
// that was never planned is not missing; the level simply has no slot for it.
bool HDLaraPartPlanned(int part);
// Bit per classic node the part covers, as hd_model measured it. 0 for a part
// that is not on the card.
unsigned int HDLaraPartCoverage(int part);

/* The first person head cut, in the only terms it has.
 *
 * `node` comes back as the node whose weight decides - Lara's head, node 14 -
 * and `bar` as how much of that weight sends a fragment away. Neither is a
 * fact about the level, the pose or the camera: they are the two numbers the
 * shader needs, and there is no third. No bind pose is needed; a weight is
 * written in the file.
 *
 * False when there is no HD body to cut - no wardrobe, or the outfit was never
 * built - and then the caller draws with no cut at all, because half a cut is
 * worse than none. */
bool HDLaraHeadCut(int* node, float* bar);

/* The sheen of one part.
 *
 * The strength and the Blinn-Phong exponent HDModelLighting wants, for the
 * outfit that is on. Both 0 when the part does not shine, and then the shader
 * takes the branch that costs nothing.
 *
 * The numbers are a material table and not a measurement, and hd_lara.cpp says
 * so at the table itself: there is no gloss mask in the art to read (measured
 * with DdsProbe -alpha - every wardrobe page is a flat 240..255 alpha), so
 * what a surface is made of has to be written down. They are logged per part
 * at level build so that a picture and a log line can be put side by side. */
void HDLaraPartSheen(int part, float* strength, float* power);

/* What is in the hand, as opposed to the hand itself.
 *
 * A HAND_ model is two things on two texture pages - the hand and the weapon -
 * and one sheen for the draw makes either the skin metal or the gun matt. So
 * HDLaraPartSheen answers with the hand's material (skin, or leather for a
 * gloved outfit) and this one with the weapon's; HDLaraLevelBuild hangs the
 * second on the pages the rest hands have not got, through
 * HDModelSetPageSheen. Both 0 for a part that is not a hand. */
void HDLaraHeldSheen(int part, float* strength, float* power);

/* Lara's skin itself, out of the same material table: what her skin shines
 * like as against the outfit she wears. The neck lid asks for it, whose whole
 * business is to be the top of her neck, and so do the hands, which makes the
 * wrist seam one material instead of two. One function so that the number is
 * written down once. */
void HDLaraSkinSheen(float* strength, float* power);

/* Which pages of the body wear it: true for a
 * .DDS page the outfit that is on names as Lara's own skin, which is what
 * makes the wrist seam one material instead of two. The table and the
 * measurement behind it are at kOutfit in hd_lara.cpp. */
bool HDLaraSkinPage(int page);

/* What to draw this frame, in draw order, after the body and the face. Returns
 * how many entries were written (at most 6: two hands, two thigh holsters, the
 * back weapon and one spare).
 *
 * The rule is TROne's with one addition. TROne picks ONE hand model -
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

/* The soft motion of the chest: the two regions of the outfit that is on.
 *
 * Found once per level build out of the body model's own vertices, while the
 * file is still a file (hd_softbody.h, HDSoftFindRegion, has the rule). Never
 * null: with no body built the region is not ok and its reason is
 * "no_body", and an outfit the finder refused carries the reason it gave. The
 * region is found whatever the option says, so the log can tell an outfit
 * that cannot carry the motion from one that has it switched off. */
const HDSoftRegion* HDLaraSoftRegion();

/* Whether a part's draws carry the motion: every part skinned on the body's
 * skeleton in the body's own bind space - the body, TR5's borrowed head, the
 * face with its throat on the torso, the neck lid and the holster rig - so
 * that a layer that lies on the skin moves with it and no seam opens between
 * the two. Never a part whose vertices live in a space of their own and ride
 * a node rigidly: the hands with what they hold, the thigh holsters, the
 * weapon on the back and the braid. A short name for the log comes back in
 * `name` when it is not null. */
bool HDLaraPartSoft(int part, const char** name = 0);

// --- the log ---------------------------------------------------------------

int HDLaraNoteCount();
const char* HDLaraNote(int at);
