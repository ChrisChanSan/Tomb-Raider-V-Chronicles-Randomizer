/*
 * ModelProbe - draw one HD object model with the engine's own renderer and
 * write a PNG, and print how its nodes line up with the classic mesh tree.
 *
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
 *
 * specific\hd\hd_model.cpp loads the remaster's .TRM models and skins them on
 * the card; no code path in the game called it yet. A skinning path nobody
 * has seen a picture out of is not a path, it is a hope - so this tool gives
 * it a hidden window, a real Direct3D 9 device, a render target and a
 * camera, and puts what comes back into a .PNG. Same source files as the
 * engine links, no copy.
 *
 * It answers three questions:
 *
 *   - does the model read? Counts, parts, pages, and the vertex layout with
 *     its three bones and three weights.
 *   - does it skin? -pose turns one node of the classic mesh tree and
 *     everything below it. A model that comes back with a turned head and a
 *     body that stayed put has been skinned; one that turns as a lump has
 *     not, and one that tears at the neck has the wrong bind pose.
 *   - does it fit the classic slot? A .TRM vertex names three bones, and
 *     those numbers are indices into the classic mesh tree - so TRM node n is
 *     classic mesh n and the question is not "which node is which" but "which
 *     of the slot's nodes does the model actually cover". -nodes prints that
 *     for one slot and -all for every bound slot of a level: a node with no
 *     geometry draws nothing at all, neither the model nor the classic mesh
 *     (see hd_model.h), so on a body it is a hole unless a separate face
 *     fills it.
 *
 * USAGE
 *
 *   ModelProbe <5-folder> [what] [options]
 *
 *     <5-folder>  the remaster's game 5 folder - the one with DATA, ITEM, TEX
 *
 *   What to look at (one of):
 *     -name <NAME>        the model <5>\ITEM\<NAME>.TRM, with no level behind
 *                         it: bind pose only, node table without a classic
 *                         column. The sibling 4\ITEM is searched too.
 *     -slot <n>           the model bound to slot n of the level, which needs
 *                         -level
 *     -all                every slot of the level, as a table, then stop
 *     -withrooms <LEVEL>  load that level's room pages into the cache first.
 *                         The models are then appended to it, exactly as
 *                         HDLevelBuild does in the game, so "pages added" is
 *                         the marginal cost of the models rather than their
 *                         cost in isolation - a page a room and a model both
 *                         wear is paid for once. Prints the level's total
 *                         page bytes as well.
 *     -lara <LEVEL>       Lara's HD composite. Loads the wardrobe that level
 *                         wears - the outfit, the face, the braid, the hands
 *                         and the holsters - against the level's own Lara
 *                         skeleton, and draws the lot in bind pose. Short
 *                         for -level <LEVEL> plus the mode.
 *
 *   Lara only:
 *     -outfit <NAME>      wear this one instead of the level's own.
 *                         OUTFIT_VCI, VCI, TR3_COAST, the randomizer's own
 *                         guest id (tr3_london, tr3_jungle, tr3_antarctica,
 *                         tr3_nevada) and any wardrobe key -
 *                         46/OUTFIT_TR2_HOME, 13/OUTFIT_TRX_WETSUIT_1,
 *                         mod/outfits/aod wetsuit/OUTFIT_CLASSIC. -wardrobe
 *                         prints the keys.
 *     -wardrobe           the wardrobe scan, and then stop: every outfit
 *                         this machine can dress her in, with
 *                         the key to name it by, where the file came from,
 *                         the five flags and where each of them was decided,
 *                         the material and the skin pages - and under it
 *                         every OUTFIT_ file the scan passed over, with the
 *                         reason. Wants -tr3 for the second install and -mods
 *                         for the player's own folder; without them it prints
 *                         what a machine with only the IV-VI remaster has.
 *     -guns <what>        pistols, uzi, shotgun, hk, revolver,
 *                         revolver_laser, grapple, grapple_laser, crowbar or
 *                         none - which slot the meshes at the hand nodes come
 *                         from, exactly as the game's mesh swap sets it.
 *     -holsters full|empty|uzi|revolver
 *                         full = LARA_HOLSTERS_PISTOLS on both thighs (the
 *                         guns are away), empty = LARA_HOLSTERS (they are in
 *                         her hands), uzi and revolver the other two slots the
 *                         engine keeps. -guns implies empty - unless this
 *                         switch is on the line, and then it wins: -guns
 *                         revolver -holsters revolver is the contradictory
 *                         frame where the gun is in her hand and
 *                         in the holster at once, which the seam has to
 *                         resolve to an empty thigh.
 *     -flare / -torch     the LEFT hand alone, as laraflar.cpp / flmtorch.cpp
 *                         set it.
 *     -back <what>        shotgun, hk or grapple on her back.
 *     -softtest <x> <y> <z>
 *                         the chest's secondary motion at a fixed offset, in
 *                         the torso's own axes (y down, z forward), the same
 *                         on both sides, through the engine's own draw
 *                         constants and to the parts the engine hands them
 *                         to. Every -lara run prints the outfit's regions as
 *                         the `softbody:` lines, with or without it.
 *     -shadowmap          the projected shadow's map of this pose, through
 *                         the engine's own box, cast matrix, target and cast
 *                         shaders (hd_shadow.h, hd_render.cpp, hd_model.cpp),
 *                         written as <out>_shadowmap_<outfit>.png: white is
 *                         nothing cast, grey the depth along the light,
 *                         darker nearer the light, stretched over the depths
 *                         cast. u runs along the box's right axis, the
 *                         picture's top is its up axis.
 *     -shadowdir <x> <y> <z>
 *                         the light's travel direction in her model space (y
 *                         down), held to the slant limit as the game holds
 *                         it; 0.35 1 0.2 without the switch.
 *     -shadowcast         run the cast pass inside the picture's own bracket,
 *                         after the bracket opens and before the models, where
 *                         the game runs it; the picture is written with
 *                         _cast in its name and must be the picture without
 *                         the switch, byte for byte.
 *     -shadowdevice [<frames>]
 *                         the map on this device: the formats it answers, the
 *                         device's reference count before, while and after the
 *                         map lives, the map made, released, the device
 *                         Reset and the map made again - the same map before
 *                         and after - and the cast pass's time against a
 *                         2560 x 1440 target, a mean over <frames> (300)
 *                         frames each waited for with an event query. Takes
 *                         the pages managed, which a Reset needs. The rooms'
 *                         vertex and index buffers (-withrooms) live in
 *                         D3DPOOL_DEFAULT, which the game drops with the whole
 *                         HD path at a device loss and builds again with the
 *                         level (hd_level.cpp, HDLevelDeviceLost): with rooms
 *                         the two Resets are left out, and a line says so.
 *                         With -shadowreceive and a room,
 *                         the device's whole state is compared before and
 *                         after the models' pass of the first picture.
 *     -shadowroom <room> <x> <z>
 *                         one room group of the level under her feet, with
 *                         -withrooms <LEVEL>: she stands at (x, z), room
 *                         local units, on the classic level's floor there
 *                         (the sector's four corners interpolated), her
 *                         soles on it. Prints the room's floor per sector, to
 *                         find a step or a wall by. The room's solid art is
 *                         drawn before her and its glass and glows after, as
 *                         HDFrameDraw and HDFrameDrawGlow do; the picture's
 *                         name gets _room<n>.
 *     -shadowreceive      the cast pass and the receive pass in that
 *                         picture, where HDFrameDraw runs them, at the full
 *                         strength of one dominant light: _recv. The models
 *                         take the maps too, after their solid draw, as
 *                         HDFrameDraw's models' pass has it: her parts the
 *                         second map, a creature hers, an item without a
 *                         blob both (hd_shadow.h, HDShadowReceiverTakes).
 *                         A model's additive pass is drawn after the room's
 *                         glass and glows, in their bracket, after the
 *                         models' pass - where HDFrameDrawGlow draws it.
 *     -shadownomodels     the same with the models' pass left out:
 *                         _nomodels, which must be the picture without the
 *                         models' pass, byte for byte.
 *     -shadowcount        the same with each model's pixels marked in the
 *                         stencil by the draw that leaves it visible (the
 *                         solid passes alone), and then counted: per model
 *                         the pixels visible and those of them the models'
 *                         pass reached - equal for every model drawn again,
 *                         the depth identity - and the pixels it reached on
 *                         no model, which must be 0. The picture is the
 *                         mask instead, _mask: red a model's pixel, yellow
 *                         one the models' pass reached, green one it reached
 *                         off every model.
 *     -shadowalong <units>
 *                         the second box moved that many units against the
 *                         light (negative: with it), so the creatures stand
 *                         that much nearer its far (near) side - a creature
 *                         crossing the box's side along the light, in view.
 *     -shadowpresence <seconds>
 *                         the receive at the strength the projected shadow
 *                         has that many game seconds after it starts to run
 *                         (hd_shadow.h, HDShadowPresence: none at 0, whole
 *                         from FADE_IN on); _recv as without it. The blob the
 *                         game fades out meanwhile is classic and not drawn.
 *     -shadowfog <units>  the distance fog from that many units on, as the
 *                         game's frame hands it to the rooms and the models
 *                         (tomb5.distance_fog times 1024); with -bulb the
 *                         picture of a shadow in fog
 *     -shadowwith <room,room,...>
 *                         further room groups beside the -shadowroom group,
 *                         each moved by its room's origin against the seed
 *                         room's: drawn solid, again in the receive (all of
 *                         them, as the groups a frame's reach holds) and in
 *                         the glass and glows - for -shadowtime's cost of a
 *                         frame with many groups in the reach
 *     -shadowunseen       the -shadowcreature models cast into the second
 *                         map and drawn in no pass - neither in the picture
 *                         nor in the models' pass - as an item the picture
 *                         clipped away (hd_object.h, HDObjectCastUnseen):
 *                         its shadow on the floor, no body. _unseen.
 *     -shadowmirror       the mirror of the -shadowroom group (RICH2 rooms
 *                         134 and 135): the classic rooms' boxes handed in
 *                         before the level build, so the build finds it;
 *                         her reflection and the creatures' drawn into the
 *                         copy behind the glass, clipped to it, as
 *                         game\mirror.cpp draws them. With -shadowreceive the
 *                         copy takes the maps through the mirror's blocks
 *                         (hd_shadow.h, HDShadowMirrorView; the receive's
 *                         MirrorReceivePS) before the group's ordinary
 *                         receive, and the reflections in the models' pass,
 *                         as hd_level.cpp and hd_object.cpp draw them.
 *                         _mirror.
 *     -shadowmirrorview   the same, the camera reflected in the glass: it sees
 *                         the room where the picture without it sees the copy,
 *                         mirror-inverted, so the two pictures' factors can
 *                         be compared pixel for pixel. _mview.
 *     -shadownomirror     the same with the mirror's blocks left out: the copy
 *                         takes the ordinary receive and the reflections
 *                         nothing. _nomirror.
 *     -shadowmirrormask   the receive with the mirror, and in place of the
 *                         picture the copy's pixels - those the mirror's
 *                         draw left - white on black, counted. _copymask.
 *     -shadowempty       the same with nothing cast into the map: _empty,
 *                         which must be the picture without the passes, byte
 *                         for byte.
 *     -shadownoguard      the same with the stencil guard switched off inside
 *                         the pass: _noguard, which differs from _recv
 *                         exactly where a pixel would be darkened twice.
 *     -shadowtime [<frames>]
 *                         the two passes' time on that room against a 2560 x
 *                         1440 target, a mean over <frames> (300) frames each
 *                         waited for with an event query; take -size 2560
 *                         1440 so the target is framed as the picture. With
 *                         -shadowcreature a third time: her cast pass, the
 *                         second map's cast pass and the two-map receive;
 *                         a fourth with the models' solid draw after them
 *                         and a fifth with the models' pass after that.
 *     -shadowcreature <slot> <x> <z> [<yaw>]
 *                         an HD model of this level's slot (a number or a
 *                         name) stood beside her in its first animation
 *                         frame, turned by <yaw> degrees: on the room's floor
 *                         at (x, z) room local with -shadowroom, at (x, z) of
 *                         her own space on her soles' height without it. Up to
 *                         eight, with -shadowitem. Drawn in the picture after
 *                         her (its separate face too), and with -shadowcast
 *                         or -shadowreceive drawn into the second map by the
 *                         engine's own cast draw and target right after her
 *                         map, where the game draws it - when its place lies
 *                         inside the second box grown by OTHERS_MARGIN
 *                         (hd_shadow.h, HDShadowBoxHolds), which the run
 *                         prints; with -shadowreceive the room then takes
 *                         both maps through the two-map receive shader, as
 *                         the game's frame with the second map drawn. The
 *                         picture's name gets _c<n>.
 *     -shadowitem <slot> <x> <z> [<yaw>]
 *                         the same for an HD item without a blob - a door, a
 *                         crate, a pickup: placed and drawn the same way,
 *                         cast into no map, and in the models' pass it takes
 *                         both. A slot the pocket rule takes is drawn
 *                         one-sided, as HDObjectEnd draws it
 *                         (hd_object_rules.h).
 *     -itemanim <anim> <key>
 *                         the -shadowcreature and -shadowitem models in that
 *                         keyframe of their own animation (numbered from the
 *                         slot's first) instead of the first frame, with
 *                         that keyframe's own offset.
 *     -itempush <e>       their node matrices scaled by 1 + e about the eye:
 *                         every pixel stays where it is and only its depth
 *                         moves. Two runs at +e and -e differ exactly where
 *                         an item and another surface tie in depth - the
 *                         measure of a fight.
 *     -nopocket           the pocket rule left off: the before picture.
 *     -itemcull none|back the side of every placed model drawn, whatever
 *                         the pocket rule says.
 *     -shadowothers       the second map of this pose and these creatures,
 *                         written as <out>_shadowothers_<outfit>_c<n>.png,
 *                         2048 texels a side, grey as -shadowmap's. With
 *                         -shadowdevice the second map's life is checked as
 *                         well: made and released alone, the device's
 *                         reference count, released with hers, the device
 *                         Reset and the map made again, the same bit for bit.
 *     -shadowsurvey <classic .TRC>
 *                         no picture: where the projected shadow lands behind
 *                         another face along the light, counted on the
 *                         classic level's own faces (Tr5Level::roomFaces) and
 *                         floors, then stop. Lara (768 units tall) and then a
 *                         creature (760) stand on each sector of the level's
 *                         floor in turn, or at the -surveyplace places; three
 *                         columns of her body (her middle and 96 units to
 *                         either side across the light) send a ray along the
 *                         fixed light (HDShadowFallback) from every 32 units of
 *                         their height. The first face a ray meets takes the
 *                         shadow as it should; every face behind it that the
 *                         receive's terms still give a weight (her height
 *                         term from her lowest node, 58.6 units above her
 *                         soles, a creature's distance term from its place,
 *                         the facing term)
 *                         takes it through that face - through a floor when
 *                         the first face is a floor or a ceiling, through a
 *                         wall otherwise. Prints per level the places with
 *                         such a face, the strongest, the rooms the faces lie
 *                         in (joined to the place's room by a door or not),
 *                         and the first faces' depth below her feet; with
 *                         -surveyout <file> one line per place and ray.
 *     -surveyplace <room> <x> <z>
 *                         one place, room local, repeatable
 *     -surveyrooms <r,r,...>
 *                         every sector of those rooms only
 *     -surveyslots <n,n,...>
 *                         how tall those slots stand in the level, from the
 *                         rest pose's box
 *     -surveyterms <full> <end> <ofull> <oend>
 *                         the survey's terms with other numbers than the
 *                         table's FULL, END, OTHERS_FULL and OTHERS_END, so a
 *                         cut is weighed before it is built
 *     -lara-nodes         the node table: every classic
 *                         node 0..14 against every source, and which .TRM
 *                         part covers it.
 *     -lara-pose <node> <yaw> <pitch>
 *                         the same as -pose, spelled for the composite.
 *     -mirror             draw through a view matrix of NEGATIVE determinant,
 *                         which is what a mirror room does to the winding.
 *     -firstperson        draw her the way the seam draws her while the first
 *                         person camera is on: the head node masked
 *                         off the body, no LARA_HEAD and no braid - all three
 *                         sit where the camera does. The camera moves with it:
 *                         it goes where game/camera.cpp
 *                         puts the eye - the head joint of this level's own
 *                         skeleton, 16 units up and 48 forward - and looks
 *                         along +z, which is the way she faces. -pitch is
 *                         then the look-DOWN angle and defaults to 60; -yaw
 *                         turns the head. The near cut below is on by
 *                         default, because that is what the game does.
 *     -fpforward <units>  how far the eye sits in front of the head joint;
 *                         48 is FP_EYE_OFFSET, what the game uses when the
 *                         sight line is clear, and 0 is what it falls back to
 *                         with a wall in the way - the case that puts the
 *                         neck nearest the camera.
 *     -cull none|cw|ccw|back
 *                         which side of her body's triangles is drawn. The
 *                         HD path draws every model double sided; with the
 *                         camera inside her that shows her own inside, so
 *                         first person culls. Default: back with
 *                         -firstperson, none otherwise. With -slot the side
 *                         of that model drawn, none without the switch.
 *     -headcut <w>        the first person head cut, on her body alone: every
 *                         fragment whose weight on the head node reaches w is
 *                         discarded. A negative w draws
 *                         the picture from before the cut existed. The default with -firstperson is
 *                         the engine's own HD_LARA_HEAD_CUT_WEIGHT.
 *     -nocap              draw first person without the lid that closes the
 *                         hole left by the head cut - the picture before the lid existed.
 *     -eye <x> <y> <z>    move the eye off the head joint, which is what a
 *                         landing does: the camera leaves
 *                         the skull and the nape stands in front of it.
 *     -headw              the head's own weight, per outfit: how much of each body the head node really owns,
 *                         scanned over eight thresholds, with the RIM each one
 *                         leaves - its vertices, how far off the neck axis and
 *                         how far down it they stand, which page they wear and
 *                         how much head weight the worst of them still
 *                         carries. It is the measurement the cut's threshold
 *                         and the lid's shape both come out of.
 *     -handfold [<permille>]
 *                         the mirrored hand weights, and
 *                         what folding them back costs. A hand file is drawn
 *                         once and mirrored, and an exporter that mirrors the
 *                         geometry without the bone names leaves vertices of
 *                         one hand weighted on the other - invisible in the
 *                         bind pose and a sliver of skin hanging in the gap
 *                         the moment the arms move apart. One row per hand
 *                         part: how many vertices name both hands, how many
 *                         the bar folds, how many are weighted exactly half
 *                         and half, how many do not sum to 255, the worst far
 *                         share, and the largest and mean DISPLACEMENT the
 *                         fold causes in the pose -lara-pose asks for - the
 *                         same vertex skinned twice through the same node
 *                         matrices the card is given. The fold is the
 *                         engine's own call, so what this prints is what the
 *                         game does. Without a number the bar is the engine's
 *                         HD_LARA_HAND_FOLD_BAR; 0 measures every crossed
 *                         vertex, which is how the bar was chosen. Needs the
 *                         camera, so it does not combine with -nodraw - and
 *                         it wants a pose with the hands APART, e.g.
 *                         -lara-pose 9 50 40 -lara-pose 12 -50 40.
 *     -nofold             build the hands from the file exactly as it stands:
 *                         the picture from before the hand fold existed, sliver and all.
 *     -nohandmix          draw a mod's hand model whole, in the mod's own
 *                         skin, instead of putting the install's hands under
 *                         its gun - the other half of the
 *                         before picture.
 *     -lara-head tr5|hair|outfit
 *                         whose head this body wears, whatever the
 *                         wardrobe decided for it. tr5 = its own head node is
 *                         not drawn and node 14 of the install's
 *                         OUTFIT_CLASSIC - Lara's hair - stands in its place;
 *                         hair = its own head AND that hair over it, for a
 *                         body whose hat or hood is worth keeping; outfit =
 *                         its own head alone, the classic head swap.
 *                         Without the switch the wardrobe
 *                         answers, exactly as it does in the game: tr5 for
 *                         every outfit of the Tomb Raider I-III install and
 *                         for every mod whose mod.txt says origin=, outfit
 *                         for the rest, and a mod's own outfit.txt head= over
 *                         either. -wardrobe prints the column. (The plain
 *                         -head is a different switch and older: it draws a
 *                         separate <NAME>_HEAD.TRM beside a -name model.)
 *     -tween60 [<n> ...]  replay those animations at sixty hertz: every drawn frame, tick and
 *                         tween alike, with every node's origin in her own
 *                         frame and how far it stands from the midpoint of
 *                         its own two neighbours. A frame
 *                         showing "a leg the wrong way round" is an
 *                         outlier here or it is not in the pose at all. With
 *                         no number it takes the rolls and the sprint dive.
 *     -poserisk           what the sixty hertz pose blend has to survive:
 *                         every adjacent
 *                         key frame pair of every Lara animation, all fifteen
 *                         nodes, with the relative TURN of the two node
 *                         frames in degrees - which is what an element-wise
 *                         matrix blend shrinks by cos(turn/2) - and the
 *                         largest per-axis packed difference in ten bit units
 *                         after the wrap. The rate 1 animations are listed
 *                         apart: those are the pairs vanilla never blended at
 *                         all and the sixty hertz path does.
 *     -neck               the measurements those numbers come from: for every
 *                         outfit, how many torso-node vertices sit above the
 *                         shoulder line and how near the nearest of them, of
 *                         the shoulders and of the chest come to the eye - at
 *                         both eye positions the game uses; then the neck as a
 *                         tube and as a cylinder, with the (S, R) scan that
 *                         says what a candidate cut takes, how much collar it
 *                         leaves standing, whether it can reach an arm and
 *                         what it leaves in the camera; then the neck in two
 *                         unit bins with the head-weight column; then what a candidate cut leaves, which is the
 *                         only one of them that models the shader exactly -
 *                         the weighted node push and the range mask, so a
 *                         head-skinned collar vertex is counted where the
 *                         engine really puts it; and last the stump BY PAGE,
 *                         which is where the inner skin's own texel is chosen
 *                         from.
 *     -braidsim [steps]   the braid's own physics, run offline. HairControl's
 *                         chain loop transcribed
 *                         line for line and driven with this level's Lara
 *                         skeleton, her collision spheres and the HD pack, in
 *                         the bind pose. One line per regime: how far
 *                         the busiest link still travels per step after 1,
 *                         10, 30 and `steps` steps, and how deep a link still
 *                         stands inside the backpack - measured against the
 *                         pack box whether or not the regime
 *                         has it, so the shipped spheres say how far into the
 *                         pack the braid may go. A settled braid reads
 *                         zero and stays there; the regimes that read
 *                         anything else are the ones the game had. Default
 *                         60 steps, which is a second of game time.
 *     -classicroot        draw the studio braid from the classic root, which
 *                         is the picture before the root moved to its HD
 *                         position: the plait
 *                         starts 15 units clear of the hair knot. The default
 *                         is the engine's own HD root.
 *     -braidanim [<n>]    the same chain under a real animation. The bind pose has no anchor motion at
 *                         all; this reads the level's own animation table,
 *                         decodes the frames Lara really wears and rebuilds
 *                         all eight collision spheres and the braid root from
 *                         them once per game frame, the game's own frame
 *                         blending included. One row per variant - the
 *                         classic body alone, the pack as it ships,
 *                         and three ways of softening it - counting the
 *                         tip's travel per frame in the four buckets its log
 *                         was measured in. Repeat the switch
 *                         for more animations; with no number it runs 103
 *                         (breathing on the spot), 0 (the run loop) and 1
 *                         (the walk loop). The step count comes from
 *                         -braidsim, or 120 without it.
 *     -nobraidfloor       take the floor clamp out of -braidanim, which is
 *                         how the bind-pose simulation ran before the clamp
 *                         existed.
 *     -laracheck          the two proofs of the Lara seam, as tables, and the
 *                         offline twins of what the engine logs as HDLOG
 *                         laracheck and HDLOG braidcheck on its first HD Lara
 *                         frame: every joint's origin put on the screen twice
 *                         (classic transform against the card's matrix), and
 *                         the braid's chain rule - a link's own frame stepped
 *                         one bone offset has to land on the next link, which
 *                         is how HairControl derives the positions in the
 *                         first place.
 *
 *   The mods and the other bundle:
 *     -mods <folder>      the Mods folder to scan. The very
 *                         resolver the engine uses, not a copy of it: a mod's
 *                         .TRM replaces the install's file of the same name,
 *                         and the pages that model asks for are looked for in
 *                         the mod first, then in the bundle its mod.txt names
 *                         with origin=, then in the install. Without the
 *                         switch no Mods folder is scanned at all and every
 *                         answer is the one this tool gives for the install alone.
 *     -tr3 <3-folder>     the Tomb Raider I-III Remastered game 3 folder,
 *                         which is what a mod's origin= reaches into - and
 *                         what the four Tomb Raider III guests need. Sets it
 *                         on both modules, as hd_object.cpp does.
 *     -classicpose <game> <MODEL_NAME>
 *                         the pose the engine will apply,
 *                         and then stop. Reads the classic object that model
 *                         name stands for in Tomb Raider <game> - 1, 2 or 3 -
 *                         out of the level files of the I-III install, and
 *                         prints the level it came from, the mesh count, the
 *                         bones, frame 0 and the matrix each node gets. Wants
 *                         -tr3 for the root (its parent is the bundle). The
 *                         same reader the engine uses, not a copy of it.
 *                         What the pose does to a particular .TRM is printed
 *                         by -slot <n> -inventory, which is the path that
 *                         really applies it.
 *     -noclassicpose      -inventory without that pose - the file as it lies
 *                         in the mod, which is what the ring drew before
 *                         the pose was applied. The switch exists so that what the pose
 *                         buys stays a measurement: the box, the fit, the
 *                         overflow and the floor figure can be had both ways
 *                         out of the same build.
 *
 *   The level:
 *     -level <LEVEL>      stem, e.g. ANDREA1. The classic level is read from
 *                         <5-folder>\DATA\<LEVEL>.TRC and the binding from
 *                         <LEVEL>.MAP beside it.
 *     -classic <path>     a classic .TRC by path instead - a Steam install's
 *                         own data, or a randomized level.
 *
 *   The picture:
 *     -size <w> <h>       render target, default 1024 1024
 *     -fov <deg>          default 80, the engine's own
 *     -yaw <deg>          camera around the model, default 180 (from the front)
 *     -pitch <deg>        default 10, looking slightly down
 *     -dist <units>       camera distance; default frames the bounding box
 *     -under <s_r,s_g,s_b,s_s,c_r,c_g,c_b,up,depth>
 *                         the eye under the water with these constants
 *                         (hd_render.cpp, "under the water"; RoomProbe
 *                         -underwater prints a body's): the picture as the
 *                         model shader fades it with the distance
 *     -invrot <x> <y> <z> the ring's own presentation: the
 *                         three angles the item's row of
 *                         inventry_objects_list carries, raw engine units
 *                         (0x4000 is 90 degrees, decimal or 0x), put on the
 *                         model exactly as DrawThreeDeeObject2D's
 *                         phd_RotYXZ puts them. Without it a picture shows
 *                         the item's OWN frame, which is not what the
 *                         player sees - the Uzi row is 0x4000 0x6000
 *                         0x4000 and the pistols' 0x4000 0xAD4C 0xBD40.
 *                         The turn goes in front of EVERY node's matrix,
 *                         so the whole object turns about the model
 *                         origin; for an inventory model, which is one
 *                         node, that is the root and nothing else. The
 *                         camera frames the
 *                         turned box - without that,
 *                         a model whose box is off-centre swung out of the
 *                         picture and the .PNG came out empty.
 *     -axisview           the model turned so that its own vertex cloud's
 *                         principal plane faces the camera: axis 0 onto x,
 *                         axis 1 onto y, the flattest onto z. The picture
 *                         that answers "what SHAPE is this file", which is
 *                         the question the ring's fit rests on. Combines
 *                         with -invrot (this one goes first).
 *     -rigid              load a -name model the way the ring loads a mod's
 *                         <BASE>_OPTION: ONE node, every vertex rigid on it.
 *                         A file whose geometry sits on nodes other than 0
 *                         draws nothing at all without this, because a bare
 *                         -name run has no mesh tree to place those nodes -
 *                         which is what an empty picture of UZI_OPTION means.
 *     -inventory          with -slot: draw the mod's own <BASE>_OPTION model
 *                         in the slot's place, turned and scaled onto the box
 *                         of the EXTRA model it replaces, exactly as the
 *                         inventory ring and the pickup display do it.
 *                         Prints both boxes and the scale. Wants
 *                         -mods; without a mod that ships one it says so and
 *                         draws the slot's own model.
 *     -itemcheck          the matrix proof of the object path, as a table:
 *                         every node's origin put on the screen twice, once
 *                         by the classic transform and once by the matrix the
 *                         card is handed. The engine runs the same check on
 *                         its own matrix stack and logs HDLOG itemcheck; this
 *                         one runs it without the game.
 *     -ringcheck          the proof of the inventory ring's off-centre
 *                         projection, in two halves.
 *                         The place: the model's bounding rectangle put on the
 *                         screen twice at every place the ring puts an item -
 *                         once by the classic off-centre projection
 *                         (f_centerx/f_centery moved to the item, as
 *                         phd_PutPolygonsPickup does) and once through the
 *                         engine's own HDModelOffCentreMatrix, which is what
 *                         hd_object.cpp hands the card per ring item. Both the
 *                         place and the size have to agree, at 4:3 and at
 *                         16:9; the budget is 1 pixel. The light: the ring's
 *                         sun term over 26 sample normals, on the matrix the
 *                         engine hands the card and on a sheared matrix (a
 *                         constant pixel offset put in as a shear) - the first must not move at all,
 *                         and the second is the shear error, kept as a
 *                         number. Needs the camera, so it does not combine
 *                         with -nodraw - same as -itemcheck and -laracheck.
 *     -pickupcheck        the proof of the pickup display - the little
 *                         turning object in the corner
 *                         after Lara picks something up - in three halves.
 *                         The place: the bounding rectangle put on the
 *                         screen twice at the place S_DrawPickup puts it
 *                         (x = winwidth - GetFixedScale(80) + PickupX,
 *                         y = winheight - GetFixedScale(75)), at 1920x1080
 *                         and 2560x1440, at rotation 0 and 90 degrees, and
 *                         with the slide-in both fully in and fully out.
 *                         Budget 1 px, on the place and on the size. The
 *                         light: the same object rendered twice - once
 *                         about the screen centre, once about the display's
 *                         corner - and compared where the two overlap after
 *                         the whole known pixel shift, with the display's
 *                         own light (ambient 8/8/8, one sun of 0x7F7F7F,
 *                         sun scale 0.75 and not the ring's 2.0). The
 *                         bracket: every watched device state read before
 *                         and after the corner draw. Needs the camera, so
 *                         it does not combine with -nodraw.
 *     -ring <x> <y>       draw the model where the ring would put it, by the
 *                         engine's own road: the matrices are untouched and
 *                         the offset goes into the projection. The picture of
 *                         what -ringcheck counts; with -ringlight it is also
 *                         the end-to-end proof that the ring's light does not
 *                         depend on where the item stands. Combines with
 *                         -ringcheck.
 *     -ringlight <bright> light it the way the ring lights it and nothing
 *                         else: an ambient of 8/8/8 and ONE sun of that
 *                         brightness, direction (0, -50, -100) normalised,
 *                         DOUBLED - which is all phd_PutPolygonsPickup sets
 *                         up and all HDInvEnd hands the model. The ring's own
 *                         numbers are 32 (an item off to the side), 160 (the
 *                         selected one) and 127 (the pickup display).
 *     -meshbits <mask>    hide nodes exactly as item->mesh_bits does in the
 *                         game; 0xBFFF takes a SWAT's head off. Decimal or
 *                         0x. Default: every node drawn.
 *     -larabits <bits>    with -lara: hide her nodes exactly as the game's
 *                         lara_item->mesh_bits does, read by the engine's own
 *                         test (hd_lara.h, HDLaraNodesShown) - 0x81FF01FF is
 *                         the word of the two cutscenes that hide her legs
 *                         inside the diving suit. Decimal or 0x.
 *     -pose <node> <yaw> <pitch>
 *                         turn that node of the mesh tree and everything
 *                         below it. Repeat the switch for more nodes.
 *     -head               draw the separate HD face as well, if there is one
 *     -lips <FILE.TRL> <frame>
 *                         a speaking face at one frame of its track file,
 *                         by the engine's own road: the face opens the track
 *                         the file's name gives (PIERRE_HEAD_108.TRL is track
 *                         108) where the game opens it, and the draw deforms
 *                         the face into a vertex buffer of its own. On -slot
 *                         with -head the camera frames the face alone; on
 *                         -lara it is her face. A frame past the clip's end is
 *                         the ease and then the bind, as in the game. Prints
 *                         what the draw did - the clip, the ease or the bind,
 *                         and the farthest vertex from the bind.
 *     -lips <FILE.TRL> <a>:<b>:<step>
 *                         every step-th frame from a to b, tiled into one
 *                         sheet, left to right and top to bottom; -size is
 *                         one tile.
 *     -loop <frame>       the loop between lines at one frame, by the
 *                         engine's own road: the face opens its own block
 *                         (HD_LIPS_TRACK_LOOP) and the draw deforms it
 *                         through the loop mask. For a face whose block
 *                         loops - Lara's, LARSON_HEAD on -slot with -head;
 *                         any other face draws as built and says so.
 *     -loop <a>:<b>:<step>
 *                         every step-th frame of the loop, as a sheet.
 *     -fire <on>[:<off>]  the firing face (HD_LIPS_TRACK_FIRE) as the game
 *                         shows it <on> ticks after the classic head swap
 *                         came on - 0 is the tick itself, a third of the
 *                         way - and, with <off>, that many ticks after it
 *                         went off again. It blends from the -loop frame
 *                         (one frame, default 0); fully off it is the loop.
 *     -onlypage <n>       with -name: build only the triangles that wear that
 *                         .DDS page, and load only that page. This is the cut the wardrobe uses to take the
 *                         thigh holster rig out of OUTFIT_TR3_CATSUIT
 *                         (page 8205) without its legs, and the switch is how
 *                         one looks at what the cut kept.
 *     -look head|wrist    aim the camera at one JOINT of the composite instead
 *                         of at the middle of the whole figure: the head, for
 *                         the braid against the skull, or the RIGHT WRIST, for
 *                         the seam between the body's own forearm and the
 *                         separate HAND_ model.
 *     -light <x y z r g b range>
 *                         one POINT light in VIEW space (the camera is at the
 *                         origin looking down +z), colour 0..255 and the
 *                         range its falloff reaches zero at. Up to 12.
 *     -bulb <x y z radius density [r g b]>
 *                         one fog bulb in VIEW space, like -light (+z is in
 *                         front of the camera); density is the engine's own
 *                         bulb->d, colour 0..1 and white without it. Up to
 *                         HD_MAX_BULBS, repeat the switch. The model is drawn
 *                         through the engine's own model shader with the
 *                         bulb in the frame's lighting block, exactly as
 *                         HDFrameDraw hands it the room's bulbs. With -shade 0
 *                         and no light the picture IS the bulb's own term,
 *                         intensity x colour / 255, wherever the model covers
 *                         a pixel.
 *     -shade <0..255>     the flat node AMBIENT, default 128; r,g,b gives
 *                         Lara's nodes a coloured one (a room's ambient,
 *                         as hd_laradraw.cpp hands her nodes), the others
 *                         take r
 *     -out <prefix>       output path prefix, default .\modelprobe
 *
 *   Everything else:
 *     -nodes              print the node table (also printed by -all)
 *     -bindcheck          the skeleton test taken apart (PrintBindCheck):
 *                         per node the classic bind, the classic mesh's
 *                         centre and the .TRM's, their boxes, the drift's
 *                         pairs against the joints and against the meshes,
 *                         the model's own twin triangles, and every
 *                         animation keyframe by keyframe - its box and
 *                         offset, each node's turn and how far the classic
 *                         and the .TRM centre of each node travel. With -all
 *                         one `bindcheck` line per bound slot.
 *     -bones              every bone the .TRM weights, numbered as the file
 *                         numbers it - the vertices it leads, their box,
 *                         the vertices alone on it and their centre - with
 *                         the slot's classic node of that number beside it,
 *                         or that the bone lies past the slot's node count.
 *     -model <NAME>       with -slot: that .TRM on the slot instead of the
 *                         level's own binding - a candidate held against a
 *                         classic tree.
 *     -crossdrift         with -all: every model on every other slot of the
 *                         level with its node count, by both drifts and the
 *                         fit - the foreign trees the skeleton test refuses.
 *     -slidecensus        with -all: per slot how far its animations carry
 *                         it, whether a node turns, whether a keyframe box
 *                         leaves the rest box or folds flat, and the pocket
 *                         rule's answer by both walks; `open` is the walk
 *                         that follows every link, into other objects'
 *                         animations, and a slot either walk takes is
 *                         listed even when its own animations do not move.
 *     -alpha              with -slot: the glass rule taken apart - per
 *                         node the classic faces, how many carry effects
 *                         bit 0 or a texture of draw type 1 or 2, and
 *                         whether the node is see-through; per part, run
 *                         and node of the .TRM its material, parameters,
 *                         triangles and what the HD path draws them as
 *                         (opaque, alpha test, additive, glass), how many
 *                         glass ranges are one-sided, how many additive
 *                         ranges copy the glass and are left out, the
 *                         part's opacity and the factor the glass is
 *                         added with, and at full light the classic's
 *                         see-through texels' luma band (tenth, half,
 *                         nine tenths) beside the glass's (its texels'
 *                         luma times alpha times the opacity) and the
 *                         factor the classic's band would hold it to. With
 *                         -all one `alpha` line per bound slot with a
 *                         see-through node or a glass range: the census,
 *                         the bands, opacity, factor and band at its end.
 *     -noglass            the glass rule left off: the before picture, for
 *                         -slot and for the -shadowitem models alike.
 *     -animframe <anim> <key>
 *                         with -slot: the picture in that keyframe of one of
 *                         the slot's own animations, on the bind pose's
 *                         camera.
 *     -classicdots        with -slot: every vertex of the slot's classic
 *                         meshes as a cyan cross over the picture, posed by
 *                         the node matrices the .TRM was drawn with, and per
 *                         node how many land on a pixel the model covered -
 *                         the HD model held against the classic one in any
 *                         pose (-animframe), with no classic draw needed.
 *     -pagelist           every slot of the page cache after the build:
 *                         its key as the cache holds it,
 *                         where the .DDS was read from, the size it was
 *                         decoded to and its bytes. One line each and a
 *                         total, so that two runs can be compared with diff
 *                         - which is how "what does a second outfit cost in
 *                         the cache" is answered by a set difference instead
 *                         of by a guess. The cache never frees a page
 *                         (hd_pagecache.cpp), so the union of two such lists
 *                         is what an in-level outfit change leaves behind.
 *     -threads <n>        page decoding threads (HDPoolRun), 0 = the
 *                         machine's own count, at most 16; default 8
 *     -managed            pages in D3DPOOL_MANAGED instead of D3DPOOL_DEFAULT
 *     -nodraw             load and measure only - no window, no picture
 *
 *   The floor a pickup lies on:
 *     -floor              with -slot: draw the slot the way the level draws
 *                         it on the ground - the mod's own <BASE>_OPTION
 *                         model where there is one, fitted onto the item's
 *                         box, and the slot's own EXTRA model otherwise -
 *                         with the floor rule applied, and put the CLASSIC
 *                         MESH's own lowest plane over the picture: a green
 *                         outline of the model's own footprint, and a small
 *                         square of that plane right under the model's
 *                         DEEPEST point, which is the one place the model can
 *                         touch it and the one the eye can read (the big
 *                         outline's near edge stands well in front of that
 *                         point at a low camera, so a model resting exactly
 *                         on the plane still shows a gap there). The three
 *                         numbers are printed beside it: the classic mesh's
 *                         deepest point, the model's, and the lift that
 *                         brings the second onto the first. Take it with a
 *                         low camera (-pitch 4 reads well; -pitch 55 shows a
 *                         crossed pair from above).
 *                         In the item's own animation frame, which is what "the way the level draws it"
 *                         means: the frame's rotations are read out of the
 *                         .TRC and put on every node, so a slot whose frame
 *                         stands the model on its head is drawn standing on
 *                         its head. The item's own world rotation is a yaw
 *                         and is left out; -pose, -invrot and -axisview are
 *                         left out of a floor picture too - the item's frame
 *                         is not a presentation, it is where the thing lies.
 *                         Every other switch's picture is the bind pose.
 *                         The camera frames the lift as well:
 *                         the box it is built from is stretched upward by
 *                         the lift, because a model that comes up 126 units
 *                         (RICH3's crossbow ammunition) otherwise walks out of
 *                         the top of the very picture that wants it. It is
 *                         stretched by the same amount with -nofloorlift, so
 *                         the two pictures share one camera.
 *     -nofloorlift        the same picture with the lift left off - what the
 *                         floor showed before the lift was added, so that what the
 *                         rule buys is a comparison and not a memory. Implies
 *                         -floor. Same camera as the picture with it, so the
 *                         two can be measured against each other in pixels.
 *     -floorcheck         every pickup slot of the level as a table, and then
 *                         stop: the classic mesh's deepest point, the HD
 *                         model's, the sink and the lift, each of them in the
 *                         frame the level draws the item in - and under every
 *                         row the same three in the bind pose, with the node
 *                         0 axis that says how far apart the two frames are.
 *                         The fitted <BASE>_OPTION models come with
 *                         their own fit's answer as well, the eight box corners
 *                         through the fit. Loads every slot the way -all
 *                         does; add -all for -all's table as well. Wants
 *                         -mods to see a mod's models, and wants -level:
 *                         without the level's own .MAP the table describes
 *                         generic-prefix bindings and not the ones the engine
 *                         builds (RICH1's BINOCULARS_ITEM is one row that
 *                         differs).
 *
 * Everything outside the pixels goes to stdout, so a run can be pasted into a
 * report as it stands.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <d3d9.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

#include "zlib.h"

#include "core/Tr5Level.h"

/* By path, not through a /I of specific\: that folder holds the engine's own
 * time.h, and putting it on the include path makes <chrono> pick it up
 * instead of the CRT's. */
/* The time interpolation's own arithmetic.
 * InterpTest includes this same header; -tween60 below
 * replays a whole animation through it, so the tool and the engine
 * cannot answer the question differently. It pulls in no engine header
 * of its own. */
#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"

#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_render.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_pagecache.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_model.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_lara.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_bind.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_mods.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_wardrobe.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_classic.h"

/* hd_install.cpp reads the two registry overrides out of these, which the
 * engine defines in tomb5\tomb5.cpp. A tool that links hd_install.cpp has to
 * bring its own pair - SfxProbe and TrgProbe do the same. Left empty, so the
 * search runs exactly as it does for a player who never set the registry
 * value; this tool is handed its folders on the command line in any case. */
char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_trm.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_object_rules.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_model_cut.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_trl.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_shadow.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_scan.h"
#include "../../../TOMB5 Code/TOMB5/specific/bc7.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "psapi.lib")

/* ------------------------------------------------------------------ PNG -- */
/* The same writer RoomProbe and DdsProbe use: 24 bit truecolour, filter 0 on
 * every scanline, deflate through the zlib the engine already links. */

static void PutBE(unsigned char* p, unsigned long v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void PngChunk(FILE* f, const char* tag, const unsigned char* data, unsigned long len)
{
	unsigned char head[8];
	unsigned long crc;

	PutBE(head, len);
	memcpy(head + 4, tag, 4);
	fwrite(head, 1, 8, f);

	if (len)
		fwrite(data, 1, len, f);

	crc = crc32(0, (const Bytef*)tag, 4);

	if (len)
		crc = crc32(crc, (const Bytef*)data, len);

	PutBE(head, crc);
	fwrite(head, 1, 4, f);
}

/* ------------------------------------------ What is drawn over the picture --
 *
 * The floor a pickup lies on. A render of a gun lying on the ground is
 * not a proof of the floor rule on its own: the eye cannot see where the floor
 * IS. So the classic mesh's own lowest point is drawn into the picture as a
 * square in that plane, projected with the same arithmetic the renderer uses -
 * the classic persp * x / z, which -ringcheck holds to one pixel against the
 * projection matrix the card is given.
 *
 * Into the PIXELS and not through the device: a line list would want a shader,
 * a vertex declaration and a state bracket of its own, all of which this tool
 * would then have to prove it puts back. Lines drawn into the readback buffer
 * touch nothing the model's own draw used.
 *
 * The list is filled by the run and emptied by ShotWrite. */
struct OverlayLine
{
	double x0, y0, x1, y1;
	unsigned long colour;
};

static std::vector<OverlayLine> g_overlay;

/* -classicdots: the classic mesh's vertices over the picture, each where
 * the node matrices the .TRM was drawn with put it. ShotWrite counts the
 * ones that land on a pixel the model covered before it draws the crosses,
 * so the number is about the renderer's picture and not about the marks. */
struct OverlayDot
{
	double x, y;
	int node;
};

static std::vector<OverlayDot> g_dots;
static bool g_dotsAsked = false;

/* -bg r,g,b: the colour every picture is cleared to, and the colour the
 * background count looks for. Magenta unless asked (see the shot's own
 * note); a dark one is what an additive pass is measured against, because
 * magenta plus a glow saturates to white whatever the glow is. */
static unsigned long g_clear = 0x00FF00FFu;

static void OverlayClear()
{
	g_overlay.clear();
}

/* One point of the model's own frame onto the screen, through `view` (the
 * model-to-view matrix, row vectors with the translation in row 3) and the
 * classic projection. false for a point behind the camera. */
static bool OverlayProject(const float* view, double persp, long w, long h,
	double x, double y, double z, double* sx, double* sy)
{
	const double vx = x * view[0] + y * view[4] + z * view[8] + view[12];
	const double vy = x * view[1] + y * view[5] + z * view[9] + view[13];
	const double vz = x * view[2] + y * view[6] + z * view[10] + view[14];

	if (vz < 20.0)
		return false;

	*sx = vx * persp / vz + (double)w * 0.5;
	*sy = vy * persp / vz + (double)h * 0.5;
	return true;
}

/* A rectangle in the plane y = `at`, centred on (cx, cz), as four projected
 * edges.
 *
 * A narrow strip and not a big square. A square as wide as the model is deep
 * puts its near edge a long way down the picture and its far edge through the
 * model, and the eye then reads the model against whichever line it happens to
 * meet - which says nothing at all. Keeping the depth small puts the two edges
 * within a few pixels of each other at a low camera, and the strip reads as
 * what it is: the height the classic mesh ends at. */
static void OverlayPlane(const float* view, double persp, long w, long h,
	double cx, double cz, double at, double halfX, double halfZ,
	unsigned long colour)
{
	double px[4], py[4];
	const double corner[4][2] =
	{
		{ cx - halfX, cz - halfZ }, { cx + halfX, cz - halfZ },
		{ cx + halfX, cz + halfZ }, { cx - halfX, cz + halfZ }
	};

	for (int i = 0; i < 4; i++)
		if (!OverlayProject(view, persp, w, h, corner[i][0], at, corner[i][1],
			&px[i], &py[i]))
			return;

	for (int i = 0; i < 4; i++)
	{
		OverlayLine line;

		line.x0 = px[i];
		line.y0 = py[i];
		line.x1 = px[(i + 1) & 3];
		line.y1 = py[(i + 1) & 3];
		line.colour = colour;
		g_overlay.push_back(line);
	}
}

// A straight run of pixels, clipped, one sample per longest-axis step.
static void OverlayStroke(unsigned long* px, long w, long h,
	const OverlayLine& line)
{
	const double dx = line.x1 - line.x0;
	const double dy = line.y1 - line.y0;
	const double span = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
	const long steps = (long)(span + 0.5);

	for (long i = 0; i <= steps; i++)
	{
		const double t = steps ? (double)i / (double)steps : 0.0;
		const long x = (long)(line.x0 + dx * t + 0.5);
		const long y = (long)(line.y0 + dy * t + 0.5);

		if (x >= 0 && x < w && y >= 0 && y < h)
			px[y * w + x] = line.colour | 0xFF000000u;
	}
}

static bool WritePng(const char* path, const unsigned long* px, long w, long h)
{
	FILE* f = fopen(path, "wb");

	if (!f)
		return false;

	static const unsigned char signature[8] =
		{ 137, 'P', 'N', 'G', 13, 10, 26, 10 };

	fwrite(signature, 1, 8, f);

	unsigned char ihdr[13];

	PutBE(ihdr, (unsigned long)w);
	PutBE(ihdr + 4, (unsigned long)h);
	ihdr[8] = 8;
	ihdr[9] = 2;			// truecolour
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	PngChunk(f, "IHDR", ihdr, 13);

	const size_t raw_size = (size_t)h * (1 + (size_t)w * 3);
	unsigned char* raw = (unsigned char*)malloc(raw_size);

	if (!raw)
	{
		fclose(f);
		return false;
	}

	unsigned char* at = raw;

	for (long y = 0; y < h; y++)
	{
		*at++ = 0;

		for (long x = 0; x < w; x++)
		{
			const unsigned long c = px[y * w + x];

			*at++ = (unsigned char)((c >> 16) & 255);
			*at++ = (unsigned char)((c >> 8) & 255);
			*at++ = (unsigned char)(c & 255);
		}
	}

	uLongf packed = (uLongf)compressBound((uLong)raw_size);
	unsigned char* out = (unsigned char*)malloc(packed);
	bool ok = false;

	if (out && compress2(out, &packed, raw, (uLong)raw_size, 9) == Z_OK)
	{
		PngChunk(f, "IDAT", out, packed);
		PngChunk(f, "IEND", 0, 0);
		ok = true;
	}

	free(out);
	free(raw);
	fclose(f);
	return ok;
}

/* --------------------------------------------------------------- camera -- */

/* The engine's own view convention (3dmath.cpp): x right, y DOWN, z forward,
 * a point goes through as a ROW vector. yaw 0 looks along +Z and grows
 * towards +X; a positive pitch looks downwards. Same function as RoomProbe's,
 * because a picture out of the two tools has to be comparable. */
static void LookAt(double ex, double ey, double ez, double yawDeg, double pitchDeg,
	float* out)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const double yaw = yawDeg * d2r;
	const double pitch = pitchDeg * d2r;

	const double sy = sin(yaw), cy = cos(yaw);
	const double sp = sin(pitch), cp = cos(pitch);

	const double f[3] = { sy * cp, sp, cy * cp };
	const double r[3] = { cy, 0.0, -sy };
	const double d[3] =
	{
		f[1] * r[2] - f[2] * r[1],
		f[2] * r[0] - f[0] * r[2],
		f[0] * r[1] - f[1] * r[0]
	};

	out[0] = (float)r[0];	out[1] = (float)d[0];	out[2] = (float)f[0];	out[3] = 0;
	out[4] = (float)r[1];	out[5] = (float)d[1];	out[6] = (float)f[1];	out[7] = 0;
	out[8] = (float)r[2];	out[9] = (float)d[2];	out[10] = (float)f[2];	out[11] = 0;
	out[12] = (float)-(ex * r[0] + ey * r[1] + ez * r[2]);
	out[13] = (float)-(ex * d[0] + ey * d[1] + ez * d[2]);
	out[14] = (float)-(ex * f[0] + ey * f[1] + ez * f[2]);
	out[15] = 1;
}

/* AlterFOV (3dmath.cpp) without the engine: the field of view is
 * measured against a 4:3 width whatever the window really is. */
static double EnginePersp(long height, double fovDeg)
{
	const double d2r = 3.14159265358979323846 / 180.0;
	const long fovWidth = height * 320 / 240;
	const double half = fovDeg * 0.5 * d2r;

	return (double)(fovWidth / 2) * cos(half) / sin(half);
}

/* -------------------------------------------------------------- matrices -- */

/* Row vector convention, the same one D3DMATRIX and the engine use:
 * out = a * b, and a point goes through as p * m. */
static void MatMul(const float* a, const float* b, float* out)
{
	float tmp[16];

	for (int row = 0; row < 4; row++)
		for (int col = 0; col < 4; col++)
		{
			float sum = 0;

			for (int k = 0; k < 4; k++)
				sum += a[row * 4 + k] * b[k * 4 + col];

			tmp[row * 4 + col] = sum;
		}

	memcpy(out, tmp, sizeof(tmp));
}

static void MatIdentity(float* m)
{
	memset(m, 0, 16 * sizeof(float));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void MatTranslate(float x, float y, float z, float* m)
{
	MatIdentity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
}

static void MatRotY(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[0] = c;  m[2] = -s;
	m[8] = s;  m[10] = c;
}

static void MatRotX(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[5] = c;  m[6] = s;
	m[9] = -s; m[10] = c;
}

/* ------------------------------------------------------- the ring (P) -- */

/* The road the engine did not take.
 *
 * The classic inventory ring projects each item about the item's own place on
 * the screen: phd_PutPolygonsPickup (specific\output.cpp) moves f_centerx /
 * f_centery to X / Y for the length of one mesh. The HD path builds ONE
 * projection per frame, about the screen centre, so the difference
 *
 *     kx = (X - centreX) / persp,  ky = (Y - centreY) / persp
 *
 * has to be put somewhere. It can be put into the ITEM'S MATRIX as a shear -
 * vx' = vx + kx * vz, which survives the projection's division by vz as a
 * constant pixel offset. This function is
 * that shear, and it is kept for ONE purpose: -ringcheck measures what it does
 * to the LIGHT, which is why the engine does not do it. hd_model.hlsl skins
 * the vertex NORMAL through the same matrix and renormalises, so a sheared
 * matrix tilts and rescales every normal of the item.
 *
 * The engine puts the offset in the PROJECTION instead (hd_model.cpp,
 * HDModelOffCentreMatrix), where the classic path has it and where a normal
 * cannot reach it.
 *
 * `m` is a row-major 4x4 in the row-vector convention (p * m), where the
 * engine's own matrix row i is the matrix's COLUMN i - hence the stride of
 * four. */
static void RingShear(float* m, double kx, double ky)
{
	for (int col = 0; col < 4; col++)
	{
		const double z = (double)m[col * 4 + 2];

		m[col * 4 + 0] = (float)((double)m[col * 4 + 0] + kx * z);
		m[col * 4 + 1] = (float)((double)m[col * 4 + 1] + ky * z);
	}
}

/* GetFixedScale (specific\output.cpp) for a window the tool only imagines. */
static long RingFixedScale(long unit, long w, long h)
{
	const long x = (w > 640) ? (long)(((double)w * unit) / 640.0 + 0.5) : unit;
	const long y = (h > 480) ? (long)(((double)h * unit) / 480.0 + 0.5) : unit;

	return x < y ? x : y;
}

/* ------------------------------------------------------------------ mem -- */

static double PeakCommitMiB()
{
	PROCESS_MEMORY_COUNTERS_EX counters;

	memset(&counters, 0, sizeof(counters));
	counters.cb = sizeof(counters);

	if (!GetProcessMemoryInfo(GetCurrentProcess(),
		(PROCESS_MEMORY_COUNTERS*)&counters, sizeof(counters)))
		return 0.0;

	return (double)counters.PeakPagefileUsage / 1048576.0;
}

/* ------------------------------------------------------------------ d3d -- */

static HWND g_window = 0;
static IDirect3D9* g_d3d = 0;
static IDirect3DDevice9* g_device = 0;
// The parameters the device was made with, for -shadowdevice's Reset.
static D3DPRESENT_PARAMETERS g_present;

static bool MakeDevice()
{
	WNDCLASSA cls;
	D3DPRESENT_PARAMETERS pp;

	memset(&cls, 0, sizeof(cls));
	cls.lpfnWndProc = DefWindowProcA;
	cls.hInstance = GetModuleHandleA(0);
	cls.lpszClassName = "ModelProbeWindow";
	RegisterClassA(&cls);

	// Never shown: WS_POPUP without WS_VISIBLE, one pixel off screen.
	g_window = CreateWindowA("ModelProbeWindow", "ModelProbe", WS_POPUP,
		-32, -32, 16, 16, 0, 0, cls.hInstance, 0);

	if (!g_window)
	{
		printf("ModelProbe: CreateWindow failed\n");
		return false;
	}

	g_d3d = Direct3DCreate9(D3D_SDK_VERSION);

	if (!g_d3d)
	{
		printf("ModelProbe: Direct3DCreate9 failed\n");
		return false;
	}

	memset(&pp, 0, sizeof(pp));
	pp.Windowed = TRUE;
	pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	pp.BackBufferFormat = D3DFMT_X8R8G8B8;
	pp.BackBufferWidth = 16;
	pp.BackBufferHeight = 16;
	pp.hDeviceWindow = g_window;
	pp.EnableAutoDepthStencil = FALSE;
	g_present = pp;

	if (FAILED(g_d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_window,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &g_device)))
	{
		printf("ModelProbe: CreateDevice(HAL, hardware vertex processing) failed\n");
		return false;
	}

	return true;
}

static void DropDevice()
{
	if (g_device) { g_device->Release(); g_device = 0; }
	if (g_d3d) { g_d3d->Release(); g_d3d = 0; }
	if (g_window) { DestroyWindow(g_window); g_window = 0; }
}

struct Shot
{
	IDirect3DTexture9* target = 0;
	IDirect3DSurface9* colour = 0;
	IDirect3DSurface9* depth = 0;
	IDirect3DSurface9* readback = 0;
	IDirect3DSurface9* oldColour = 0;
	long w = 0, h = 0;
};

static bool ShotBegin(Shot& shot, long w, long h)
{
	shot.w = w;
	shot.h = h;

	if (FAILED(g_device->CreateTexture((UINT)w, (UINT)h, 1, D3DUSAGE_RENDERTARGET,
		D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &shot.target, 0)))
	{
		printf("ModelProbe: CreateTexture(render target %ldx%ld) failed\n", w, h);
		return false;
	}

	if (FAILED(shot.target->GetSurfaceLevel(0, &shot.colour)))
		return false;

	if (FAILED(g_device->CreateDepthStencilSurface((UINT)w, (UINT)h, D3DFMT_D24S8,
		D3DMULTISAMPLE_NONE, 0, TRUE, &shot.depth, 0)))
	{
		printf("ModelProbe: CreateDepthStencilSurface failed\n");
		return false;
	}

	if (FAILED(g_device->CreateOffscreenPlainSurface((UINT)w, (UINT)h,
		D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &shot.readback, 0)))
	{
		printf("ModelProbe: CreateOffscreenPlainSurface failed\n");
		return false;
	}

	g_device->GetRenderTarget(0, &shot.oldColour);
	g_device->SetRenderTarget(0, shot.colour);
	g_device->SetDepthStencilSurface(shot.depth);

	/* MAGENTA, not black: a model that draws nothing and a model that draws
	 * black are the same picture on a black background, and the first is a
	 * bug while the second is a dark corner. */
	g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
		(D3DCOLOR)(0xFF000000u | g_clear), 1.0f, 0);

	return true;
}

static bool ShotWrite(Shot& shot, const char* path)
{
	D3DLOCKED_RECT rect;

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)))
	{
		printf("ModelProbe: GetRenderTargetData failed\n");
		return false;
	}

	if (FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
	{
		printf("ModelProbe: locking the readback surface failed\n");
		return false;
	}

	unsigned long* pixels = (unsigned long*)malloc((size_t)shot.w * (size_t)shot.h * 4);

	if (!pixels)
	{
		shot.readback->UnlockRect();
		return false;
	}

	long background = 0;
	double sum = 0;

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src =
			(const unsigned long*)((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
		{
			const unsigned long c = src[x] | 0xFF000000u;

			pixels[y * shot.w + x] = c;

			if ((c & 0x00FFFFFFu) == g_clear)
				background++;

			sum += (double)((c >> 16) & 255) + (double)((c >> 8) & 255) + (double)(c & 255);
		}
	}

	shot.readback->UnlockRect();

	/* -classicdots: a vertex is on the model when its pixel or one of the
	 * eight around it is not the background - a vertex on the outline
	 * rounds to either side of it. Counted per node, so a node the .TRM
	 * leaves empty or puts elsewhere shows as that node's own number. */
	if (g_dotsAsked)
	{
		int on[33] = { 0 }, all[33] = { 0 }, off = 0;

		for (size_t i = 0; i < g_dots.size(); i++)
		{
			const long x = (long)(g_dots[i].x + 0.5);
			const long y = (long)(g_dots[i].y + 0.5);
			const int n = g_dots[i].node < 32 ? g_dots[i].node : 32;
			bool hit = false;

			if (x < 0 || x >= shot.w || y < 0 || y >= shot.h)
			{
				off++;
				continue;
			}

			for (long dy = -1; dy <= 1 && !hit; dy++)
				for (long dx = -1; dx <= 1 && !hit; dx++)
				{
					const long px = x + dx, py = y + dy;

					hit = px >= 0 && px < shot.w && py >= 0 && py < shot.h &&
						(pixels[py * shot.w + px] & 0x00FFFFFFu) != g_clear;
				}

			all[n]++;
			on[n] += hit ? 1 : 0;
		}

		int onSum = 0, allSum = 0;

		for (int n = 0; n <= 32; n++)
		{
			onSum += on[n];
			allSum += all[n];

			if (all[n])
				printf("  classic dots node %2d: %4d of %4d on the model\n", n,
					on[n], all[n]);
		}

		printf("  classic dots: %d of %d on the model (%.1f%%), %d off the "
			"picture\n", onSum, allSum,
			allSum ? 100.0 * (double)onSum / (double)allSum : 0.0, off);
		g_dots.clear();
	}

	/* And the floor over it. After the background count above and
	 * before the file: the percentages beside every picture this tool writes
	 * are about what the RENDERER drew, and lines counted into them would
	 * change a number other proofs already compare. */
	for (size_t i = 0; i < g_overlay.size(); i++)
		OverlayStroke(pixels, shot.w, shot.h, g_overlay[i]);

	const double texels = (double)shot.w * (double)shot.h;
	const bool ok = WritePng(path, pixels, shot.w, shot.h);

	free(pixels);

	printf("  %-58s %5.1f%% background, mean brightness %.1f/255\n",
		path, 100.0 * (double)background / texels, sum / (texels * 3.0));

	if (!ok)
		printf("ModelProbe: writing %s failed\n", path);

	return ok;
}

/* The picture's pixels, with no file written.
 * ShotWrite measures a mean over the whole target, which answers nothing when
 * the two pictures being compared have the object in DIFFERENT places;
 * -pickupcheck compares them pixel by pixel after the whole known shift, so it
 * needs the pixels themselves. */
static bool ShotGrab(Shot& shot, std::vector<unsigned long>& out)
{
	D3DLOCKED_RECT rect;

	out.clear();

	if (FAILED(g_device->GetRenderTargetData(shot.colour, shot.readback)))
		return false;

	if (FAILED(shot.readback->LockRect(&rect, 0, D3DLOCK_READONLY)))
		return false;

	out.resize((size_t)shot.w * (size_t)shot.h);

	for (long y = 0; y < shot.h; y++)
	{
		const unsigned long* src = (const unsigned long*)
			((const unsigned char*)rect.pBits + (size_t)y * rect.Pitch);

		for (long x = 0; x < shot.w; x++)
			out[(size_t)y * (size_t)shot.w + (size_t)x] = src[x] | 0xFF000000u;
	}

	shot.readback->UnlockRect();
	return true;
}

/* ------------------------------------------------------- state bracket -- */
/*
 * The same check RoomProbe makes of the room path's bracket,
 * brought here for a bracket that is new: the pickup display
 * opens one in the MIDDLE of a classic frame - after the frame's own HD block
 * has been drawn and closed, between an InitialiseSortList and the DrawSortList
 * that follows it - so anything it leaves behind is handed straight to the
 * classic path with no frame boundary in between to hide it.
 *
 * The list is deliberately WIDER than the one hd_render saves, so that a state
 * the HD path changes and forgets to save is caught here rather than in the
 * game.
 */
static const D3DRENDERSTATETYPE kWatchedStates[] =
{
	D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_FILLMODE,
	D3DRS_SHADEMODE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC,
	D3DRS_DITHERENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
	D3DRS_BLENDOP, D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_SPECULARENABLE,
	D3DRS_LIGHTING, D3DRS_COLORVERTEX, D3DRS_AMBIENT, D3DRS_CLIPPING,
	D3DRS_STENCILENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_COLORWRITEENABLE,
	D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRGBWRITEENABLE,
	D3DRS_MULTISAMPLEANTIALIAS, D3DRS_TEXTUREFACTOR, D3DRS_WRAP0,
	D3DRS_NORMALIZENORMALS, D3DRS_LOCALVIEWER, D3DRS_DEPTHBIAS,
	D3DRS_SLOPESCALEDEPTHBIAS
};

static const D3DSAMPLERSTATETYPE kWatchedSamplers[] =
{
	D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_ADDRESSW, D3DSAMP_BORDERCOLOR,
	D3DSAMP_MAGFILTER, D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER,
	D3DSAMP_MIPMAPLODBIAS, D3DSAMP_MAXMIPLEVEL, D3DSAMP_MAXANISOTROPY,
	D3DSAMP_SRGBTEXTURE
};

static const D3DTEXTURESTAGESTATETYPE kWatchedStages[] =
{
	D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
	D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2, D3DTSS_TEXCOORDINDEX
};

static const int kWatchedStateCount =
	(int)(sizeof(kWatchedStates) / sizeof(kWatchedStates[0]));
static const int kWatchedSamplerCount =
	(int)(sizeof(kWatchedSamplers) / sizeof(kWatchedSamplers[0]));
static const int kWatchedStageCount =
	(int)(sizeof(kWatchedStages) / sizeof(kWatchedStages[0]));

struct StateShot
{
	DWORD rs[kWatchedStateCount];
	DWORD samp[kWatchedSamplerCount];
	DWORD stage[kWatchedStageCount];
	RECT scissor;
	BOOL scissorOk;
	DWORD fvf;
	void* vs;
	void* ps;
	void* decl;
	void* stream;
	void* indices;
	void* texture;
	UINT offset, stride;
};

static void StateTake(StateShot& shot)
{
	memset(&shot, 0, sizeof(shot));

	for (int i = 0; i < kWatchedStateCount; i++)
		g_device->GetRenderState(kWatchedStates[i], &shot.rs[i]);
	for (int i = 0; i < kWatchedSamplerCount; i++)
		g_device->GetSamplerState(0, kWatchedSamplers[i], &shot.samp[i]);
	for (int i = 0; i < kWatchedStageCount; i++)
		g_device->GetTextureStageState(0, kWatchedStages[i], &shot.stage[i]);

	shot.scissorOk = SUCCEEDED(g_device->GetScissorRect(&shot.scissor));
	g_device->GetFVF(&shot.fvf);

	// The Get* calls add a reference; the pointer is taken as an identity and
	// the reference given straight back, so the measurement cannot change what
	// it measures.
	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* stream = 0;
	IDirect3DIndexBuffer9* indices = 0;
	IDirect3DBaseTexture9* texture = 0;

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetStreamSource(0, &stream, &shot.offset, &shot.stride);
	g_device->GetIndices(&indices);
	g_device->GetTexture(0, &texture);

	shot.vs = vs; shot.ps = ps; shot.decl = decl;
	shot.stream = stream; shot.indices = indices; shot.texture = texture;

	if (vs) vs->Release();
	if (ps) ps->Release();
	if (decl) decl->Release();
	if (stream) stream->Release();
	if (indices) indices->Release();
	if (texture) texture->Release();
}

static int StateDiff(const StateShot& before, const StateShot& after)
{
	int broken = 0;

	for (int i = 0; i < kWatchedStateCount; i++)
		if (before.rs[i] != after.rs[i])
		{
			printf("  BROKEN render state %d: %lu -> %lu\n",
				(int)kWatchedStates[i], before.rs[i], after.rs[i]);
			broken++;
		}

	for (int i = 0; i < kWatchedSamplerCount; i++)
		if (before.samp[i] != after.samp[i])
		{
			printf("  BROKEN sampler state %d: %lu -> %lu\n",
				(int)kWatchedSamplers[i], before.samp[i], after.samp[i]);
			broken++;
		}

	for (int i = 0; i < kWatchedStageCount; i++)
		if (before.stage[i] != after.stage[i])
		{
			printf("  BROKEN texture stage state %d: %lu -> %lu\n",
				(int)kWatchedStages[i], before.stage[i], after.stage[i]);
			broken++;
		}

	if (before.scissorOk != after.scissorOk ||
		(before.scissorOk && memcmp(&before.scissor, &after.scissor, sizeof(RECT))))
	{
		printf("  BROKEN scissor rectangle\n");
		broken++;
	}

	if (before.fvf != after.fvf) { printf("  BROKEN FVF\n"); broken++; }
	if (before.vs != after.vs) { printf("  BROKEN vertex shader\n"); broken++; }
	if (before.ps != after.ps) { printf("  BROKEN pixel shader\n"); broken++; }
	if (before.decl != after.decl) { printf("  BROKEN vertex declaration\n"); broken++; }
	if (before.stream != after.stream) { printf("  BROKEN stream 0\n"); broken++; }
	if (before.indices != after.indices) { printf("  BROKEN index buffer\n"); broken++; }
	if (before.texture != after.texture) { printf("  BROKEN texture 0\n"); broken++; }
	if (before.offset != after.offset) { printf("  BROKEN stream offset\n"); broken++; }

	/* NOT the stride, and RoomProbe's own note says why: after
	 * SetStreamSource(0, NULL, 0, 0) the runtime still answers the last stride
	 * it was given and there is no way to put the old one back. The stride of
	 * an unbound stream is read by nobody. */
	return broken;
}

static void ShotEnd(Shot& shot)
{
	if (shot.oldColour)
	{
		g_device->SetRenderTarget(0, shot.oldColour);
		shot.oldColour->Release();
		shot.oldColour = 0;
	}

	g_device->SetDepthStencilSurface(0);

	if (shot.readback) { shot.readback->Release(); shot.readback = 0; }
	if (shot.depth) { shot.depth->Release(); shot.depth = 0; }
	if (shot.colour) { shot.colour->Release(); shot.colour = 0; }
	if (shot.target) { shot.target->Release(); shot.target = 0; }
}

/* ------------------------------------------------------- the classic side -- */

/* The bind pose and the hierarchy of one slot, straight out of the .TRC.
 *
 * A TR5 mesh tree is four longs per node after the first - flags, x, y, z -
 * and the flags carry the matrix stack: bit 0 pops, bit 1 pushes, and the
 * push happens BEFORE the translation, so what is pushed is the parent's
 * frame (game\draw.cpp, DrawAnimatingItem). The bind pose is that walk with
 * every rotation left out, which is TROne's bind_pose_of_slot (TROne-main,
 * copy of 2026-10-06, commit 75c4c843, host\render\game_renderer.cpp) and
 * the very thing the .TRM's vertices are measured against.
 */
struct ClassicSlot
{
	int slot = -1;
	int nodes = 0;
	float bind[32][3] = { { 0, 0, 0 } };
	int parent[32] = { 0 };
	long flags[32] = { 0 };
	long delta[32][3] = { { 0, 0, 0 } };
	bool ok = false;
};

static bool ReadClassicSlot(const Tr5Level& level, int slot, ClassicSlot& out)
{
	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(slot, info))
		return false;

	out.slot = slot;
	out.nodes = info.nmeshes < 32 ? info.nmeshes : 32;
	out.parent[0] = -1;
	out.flags[0] = 0;

	float current[3] = { 0, 0, 0 };
	float stack[32][3];
	int parentStack[32];
	int top = 0;
	int parent = 0;

	memset(out.bind, 0, sizeof(out.bind));

	for (int node = 1; node < out.nodes; node++)
	{
		const long long at = (long long)info.boneIndex + (long long)(node - 1) * 4;
		const long flags = level.debugReadBoneLong(at + 0);
		const float x = (float)level.debugReadBoneLong(at + 1);
		const float y = (float)level.debugReadBoneLong(at + 2);
		const float z = (float)level.debugReadBoneLong(at + 3);

		out.flags[node] = flags;
		out.delta[node][0] = (long)x;
		out.delta[node][1] = (long)y;
		out.delta[node][2] = (long)z;

		if ((flags & 1) && top > 0)
		{
			--top;
			memcpy(current, stack[top], sizeof(current));
			parent = parentStack[top];
		}

		if (flags & 2)
		{
			memcpy(stack[top], current, sizeof(current));
			parentStack[top] = parent;

			if (top < 31)
				++top;
		}

		current[0] += x;
		current[1] += y;
		current[2] += z;

		memcpy(out.bind[node], current, sizeof(current));
		out.parent[node] = parent;
		parent = node;
	}

	out.ok = true;
	return true;
}

/* The classic mesh's own deepest point, in the slot's frame.
 *
 * The reference the floor rule measures an HD pickup model against: a pickup
 * is drawn at its animation frame's own offset, so where the ground meets it
 * is wherever the level's own mesh ends. y is DOWN, so deepest is the LARGEST
 * y, and each node's mesh is taken where the mesh tree puts it - a two-mesh
 * pickup measured on node 0 alone would be measured against half of itself.
 * The frame offset is left out on purpose: it reaches the HD model through the
 * same matrix, so it cancels in the difference the rule is about.
 *
 * The engine reads the very same quantity out of the box ProcessMeshData took
 * at level load (MESH_DATA::bbox[4]); this walks the .TRC because a tool has
 * no engine behind it. The TR5 mesh is x, y, z, radius, flags, a vertex-count
 * word whose LOW BYTE is the count, and then three shorts per vertex - the
 * same reading as the engine's loader and as the randomizer's own walker,
 * which agree with each other. */
static bool ClassicMeshLowest(const Tr5Level& level, const ClassicSlot& here,
	const float* pose, float* out, int* meshesSeen)
{
	Tr5Level::MoveableInfo info;

	if (meshesSeen)
		*meshesSeen = 0;

	if (!level.moveableInfo(here.slot, info))
		return false;

	const std::vector<uint8_t>& bytes = level.debugBytes();
	bool any = false;
	float deepest = 0.0f;

	for (int node = 0; node < here.nodes; node++)
	{
		size_t at = 0;

		if (!level.debugMeshAt(info.meshIndex + node, at) ||
			at + 12 > bytes.size())
			continue;

		int16_t vertsWord = 0;

		memcpy(&vertsWord, &bytes[at + 10], 2);

		const int verts = vertsWord & 0xFF;

		if (!verts || at + 12 + (size_t)verts * 6 > bytes.size())
			continue;

		/* Through the frame the level draws this node in, which
		 * is why all three coordinates are read and not the y alone: a frame
		 * that turns node 0 - six of ANDREA3's eighteen pickup slots do, and
		 * UZI_ITEM's turns it upside down - sends a mesh's lowest point
		 * somewhere its own y cannot say. Without a pose the frame turns
		 * nothing and the node's bind offset is the whole of its placement,
		 * which is the number this tool printed before. */
		const float* m = pose ? pose + (size_t)node * 12 : 0;

		for (int v = 0; v < verts; v++)
		{
			int16_t xyz[3] = { 0, 0, 0 };

			memcpy(xyz, &bytes[at + 12 + (size_t)v * 6], 6);

			const float inSlot = m
				? m[4] * (float)xyz[0] + m[5] * (float)xyz[1] +
					m[6] * (float)xyz[2] + m[7]
				: (float)xyz[1] + here.bind[node][1];

			if (!any || inSlot > deepest)
				deepest = inSlot;

			any = true;
		}

		if (meshesSeen)
			(*meshesSeen)++;
	}

	if (any && out)
		*out = deepest;

	return any;
}

/* The frame the level draws a slot in, out of the .TRC.
 *
 * The engine's own walk is HDFramePose, which this tool links - so the frame a
 * -floor picture is drawn in is the frame the engine measured in, and not a
 * copy of the rule. All this does is hand it the two things it wants out of
 * the level file: the object's first frame as words (frameOffset is a BYTE
 * offset into the frame block, so it halves into a short index, the same
 * reading -braidanim does) and the mesh tree as the engine's four longs a
 * node.
 *
 * The answer is twelve floats a node - the engine's 3x4, a point is R * p + t
 * - relative to the item's origin, the frame's own offset left out. Empty when
 * the level has no such moveable, and a caller with an empty pose measures and
 * draws the bind pose, which is what this tool drew before it read the item's own animation frame. */
static int ReadFramePose(const Tr5Level& level, const ClassicSlot& here,
	std::vector<float>& out)
{
	Tr5Level::MoveableInfo info;

	out.clear();

	if (here.slot < 0 || here.nodes < 1 || !level.moveableInfo(here.slot, info))
		return 0;

	/* The words the walk can read at most: nine of box and offset, then two
	 * per node - a single-axis rotation takes one word and a three-axis one
	 * takes two, so twice the node count is the bound. */
	std::vector<short> frame((size_t)(9 + 2 * here.nodes), 0);

	for (size_t i = 0; i < frame.size(); i++)
		frame[i] = level.debugReadFrameShort(
			(long long)(info.frameOffset / 2) + (long long)i);

	std::vector<long> tree((size_t)(here.nodes > 1 ? (here.nodes - 1) * 4 : 1), 0);

	for (int node = 1; node < here.nodes; node++)
	{
		tree[(size_t)(node - 1) * 4 + 0] = here.flags[node];
		tree[(size_t)(node - 1) * 4 + 1] = here.delta[node][0];
		tree[(size_t)(node - 1) * 4 + 2] = here.delta[node][1];
		tree[(size_t)(node - 1) * 4 + 3] = here.delta[node][2];
	}

	out.assign((size_t)here.nodes * 12, 0.0f);

	return HDFramePose(&frame[0], here.nodes > 1 ? &tree[0] : 0, here.nodes,
		&out[0]);
}

/* The engine's 3x4 into the row-major 4x4 this tool multiplies with - the very
 * transposition store_matrices does in hd_object.cpp, because the engine's
 * matrix takes a point as a COLUMN and everything past the shader takes it as
 * a row. */
static void PoseToRowMajor(const float* pose, float* out)
{
	out[0] = pose[0];  out[1] = pose[4];  out[2] = pose[8];  out[3] = 0.0f;
	out[4] = pose[1];  out[5] = pose[5];  out[6] = pose[9];  out[7] = 0.0f;
	out[8] = pose[2];  out[9] = pose[6];  out[10] = pose[10]; out[11] = 0.0f;
	out[12] = pose[3]; out[13] = pose[7]; out[14] = pose[11]; out[15] = 1.0f;
}

/* The lift, straight up in the world - the engine's own
 * move, on the same matrices and in the same space.
 *
 * The node matrices are model space to VIEW space, so a displacement in the
 * world has to be turned into view space first; world up is (0, -1, 0)
 * because y is DOWN, and the view matrix's row 1 is the image of world y, so
 * that is minus that row. The 3x3 is untouched - nothing is scaled, nothing
 * turned, nothing moved sideways.
 *
 * At the view end of the chain, which is what makes it "up" in the picture
 * whatever is prepended in front of the model afterwards. Written
 * onto the fit's own translation row in one branch and
 * onto the node matrix's y row in the other, so -invrot displaced the model
 * in the TURNED frame and a slot whose animation frame stands the model on
 * its head was pushed further into the ground. */
static void LiftNodes(float* matrices, int nodes, float lift, const float* view)
{
	const float up[3] = { -view[4], -view[5], -view[6] };

	for (int node = 0; node < nodes; node++)
	{
		float* m = matrices + (size_t)node * 16;

		m[12] += lift * up[0];
		m[13] += lift * up[1];
		m[14] += lift * up[2];
	}
}

/* The node matrices, model space to VIEW space - what the engine's matrix
 * stack holds when it calls phd_PutPolygons for each mesh, and what
 * HDModelDraw wants. `turn` is one rotation per node in degrees (yaw, pitch),
 * applied where -pose asked for it; a node's rotation reaches every node
 * below it because the stack carries it down.
 *
 * `pose` is ReadFramePose's answer, and with it the walk
 * below is not done at all: the frame's own matrices already carry every
 * translation AND every rotation of the item's animation frame, which is the
 * whole of what -floor has to draw. -pose's turns are a presentation of the
 * model and are left out of that picture on purpose; null is the bind pose,
 * which is every other switch's picture and unchanged. */
static void BuildNodeMatrices(const ClassicSlot& classic, const float* view,
	const double turn[32][2], float* out /* nodes * 16 */,
	const float* pose = 0)
{
	float stack[32][16];
	float current[16];
	int top = 0;

	float rotation[16], tmp[16], step[16];

	if (pose)
	{
		for (int node = 0; node < classic.nodes; node++)
		{
			float own[16];

			PoseToRowMajor(pose + (size_t)node * 12, own);
			MatMul(own, view, out + (size_t)node * 16);
		}

		return;
	}

	MatIdentity(rotation);

	if (turn && (turn[0][0] != 0.0 || turn[0][1] != 0.0))
	{
		MatRotY(turn[0][0], tmp);
		MatRotX(turn[0][1], step);
		MatMul(tmp, step, rotation);
	}

	MatMul(rotation, view, current);
	memcpy(out, current, sizeof(current));

	for (int node = 1; node < classic.nodes; node++)
	{
		if ((classic.flags[node] & 1) && top > 0)
			memcpy(current, stack[--top], sizeof(current));

		if (classic.flags[node] & 2)
		{
			memcpy(stack[top], current, sizeof(current));

			if (top < 31)
				++top;
		}

		MatTranslate((float)classic.delta[node][0], (float)classic.delta[node][1],
			(float)classic.delta[node][2], step);
		MatMul(step, current, tmp);
		memcpy(current, tmp, sizeof(current));

		if (turn && (turn[node][0] != 0.0 || turn[node][1] != 0.0))
		{
			float yawM[16], pitchM[16];

			MatRotY(turn[node][0], yawM);
			MatRotX(turn[node][1], pitchM);
			MatMul(yawM, pitchM, rotation);
			MatMul(rotation, current, tmp);
			memcpy(current, tmp, sizeof(current));
		}

		memcpy(out + node * 16, current, sizeof(current));
	}
}

/* -------------------------------------------------------------- the pages -- */

static bool Exists(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");

	if (!f)
		return false;

	fclose(f);
	return true;
}

static std::string ParentOf(const std::string& path)
{
	std::string out = path;

	while (!out.empty() && (out[out.size() - 1] == '\\' || out[out.size() - 1] == '/'))
		out.erase(out.size() - 1);

	const size_t cut = out.find_last_of("\\/");

	return cut == std::string::npos ? std::string(".") : out.substr(0, cut);
}

/* Whether a page has a .DDS of its own anywhere in the bundle. The page cache
 * puts WHITE.DDS in its place when it has none, which is a picture and not an
 * error - but for the table the two have to be told apart. */
/* Where a page really came from, asked of the cache that loaded it rather
 * than worked out again here. A copy of the 5-then-4 rule with a root and a
 * page number would go out of step: it would know nothing of the other
 * bundle's namespace and nothing of the mods', so it would report every page of a Tomb Raider III guest
 * and every page of a mod's model as missing while the engine had them on the
 * card. The cache answers for the key it was handed, which is the only thing
 * that can be right for all three namespaces.
 */
static const char* PageFrom(int page)
{
	const int slot = HDPageCacheSlotOf(page);

	return slot < 0 ? "-"
		: HDPageCacheOriginText(HDPageCacheSlotOrigin(slot));
}

static bool PageThere(int page)
{
	const int slot = HDPageCacheSlotOf(page);

	return slot >= 0 && HDPageCacheSlotOrigin(slot) > 0;
}

/* ------------------------------------------------------------------ main -- */

static std::wstring ToW(const std::string& s)
{
	std::wstring w;

	for (size_t i = 0; i < s.size(); i++)
		w += (wchar_t)(unsigned char)s[i];

	return w;
}

static void PrintNodeTable(int slot, const ClassicSlot* classic, const char* name)
{
	printf("\n-- nodes: %s, slot %d ----------------------------------------\n",
		name ? name : "?", slot);
	printf("  %-5s %-9s %-24s %-9s %s\n", "node", "TRM vtx", "classic bind (x y z)",
		"parent", "");
	printf("  ----- --------- ------------------------ --------- ------------------\n");

	const int nodes = HDModelNodes(slot);
	int covered = 0;

	for (int node = 0; node < nodes; node++)
	{
		const int vertices = HDModelNodeVertices(slot, node);
		const bool has = HDModelNodeCovered(slot, node);

		if (has)
			covered++;

		char bind[64] = "-";
		char parent[16] = "-";

		if (classic && classic->ok && node < classic->nodes)
		{
			sprintf(bind, "%7.0f %7.0f %7.0f", classic->bind[node][0],
				classic->bind[node][1], classic->bind[node][2]);

			if (node)
				sprintf(parent, "%d", classic->parent[node]);
			else
				sprintf(parent, "root");
		}

		printf("  %-5d %-9d %-24s %-9s %s\n", node, vertices, bind, parent,
			has ? "" : "NO GEOMETRY - nothing is drawn there");
	}

	printf("  %d of %d nodes covered by the model, %d draw nothing\n",
		covered, nodes, nodes - covered);

	if (HDModelHasHead(slot))
		printf("  a separate HD face rides rigidly on node %d\n", HDModelHeadNode(slot));
}

/* ------------------------------------------------------- the neck stump -- */
/* Where the first person near cut comes from.
 *
 * The classic path hides Lara's head mesh in first person, and for a classic
 * body that is the whole of it - LM_HEAD is head and neck in one piece. An HD
 * body is a single skinned surface: masking node 14 takes the head off, and
 * the neck stump below it stays, because those triangles are weighted to the
 * torso. That is what stands in the camera when she looks down. The diving suit puts its whole helmet on the torso node in the same
 * way.
 *
 * This table measures how near that geometry comes to the eye - for every
 * outfit, not only the one the level wears, because the cut is one number for
 * all of them. The eye is the game's: the head joint of THIS level's Lara
 * skeleton, 16 units up and 48 or 0 units forward (game/camera.cpp,
 * FirstPersonCamera - mgLOS decides which). The cut is a RADIUS around that
 * point, so what matters is DISTANCE and not depth: it has to sit above every
 * stump figure and below every shoulder and chest figure in the table.
 */
static std::string TrmPath(const std::string& root5, const char* stem)
{
	const std::string leaf = std::string("\\ITEM\\") + stem + ".TRM";

	if (Exists(root5 + leaf))
		return root5 + leaf;

	const std::string bundle = ParentOf(root5);

	if (Exists(bundle + "\\4" + leaf))
		return bundle + "\\4" + leaf;

	if (Exists(bundle + "\\6" + leaf))
		return bundle + "\\6" + leaf;

	return std::string();
}

/* ------------------------------------------------ the skeleton, taken apart */
/* One animation of a slot, as the .TRC holds it: the byte offset of its first
 * keyframe in the frame block, the keyframe stride in words, the rate and the
 * frame range. `count` comes back as the number of animations the slot owns -
 * up to the next slot's first one, or the end of the table. */
struct SlotAnim
{
	uint32_t frameOffset = 0;
	int rate = 1, size = 0, start = 0, end = 0;
};

static bool ReadSlotAnim(const Tr5Level& level, int slot, int anim,
	SlotAnim& out, int* count)
{
	Tr5Level::MoveableInfo info;
	size_t at = 0;
	int32_t total = 0;

	if (!level.moveableInfo(slot, info) || info.animIndex < 0 ||
		!level.debugAnimBlock(at, total))
		return false;

	int next = total;
	const std::vector<int> slots = level.moveableSlots();

	for (size_t i = 0; i < slots.size(); i++)
	{
		Tr5Level::MoveableInfo other;

		if (level.moveableInfo(slots[i], other) &&
			other.animIndex > info.animIndex && other.animIndex < next)
			next = other.animIndex;
	}

	if (count)
		*count = next - info.animIndex;

	const int index = info.animIndex + anim;
	const std::vector<uint8_t>& bytes = level.debugBytes();

	if (anim < 0 || index >= next ||
		at + (size_t)(index + 1) * 40 > bytes.size())
		return false;

	const uint8_t* e = bytes.data() + at + (size_t)index * 40;
	int16_t s = 0;

	memcpy(&out.frameOffset, e + 0, 4);
	out.rate = e[4] ? e[4] : 1;
	out.size = e[5];
	memcpy(&s, e + 24, 2);
	out.start = s;
	memcpy(&s, e + 26, 2);
	out.end = s;

	return out.size > 0 && out.end >= out.start;
}

/* A keyframe of one of the slot's animations through the engine's own walk,
 * HDFramePose - twelve floats a node, the frame's offset left out as
 * ReadFramePose leaves it out. */
static int ReadAnimPose(const Tr5Level& level, const ClassicSlot& here,
	int anim, int key, std::vector<float>& out)
{
	SlotAnim a;

	out.clear();

	if (!ReadSlotAnim(level, here.slot, anim, a, 0) || key < 0)
		return 0;

	std::vector<short> frame((size_t)(9 + 2 * here.nodes), 0);
	const long long first = (long long)(a.frameOffset / 2) +
		(long long)key * a.size;

	for (size_t i = 0; i < frame.size(); i++)
		frame[i] = level.debugReadFrameShort(first + (long long)i);

	std::vector<long> tree((size_t)(here.nodes > 1 ? (here.nodes - 1) * 4 : 1),
		0);

	for (int node = 1; node < here.nodes; node++)
	{
		tree[(size_t)(node - 1) * 4 + 0] = here.flags[node];
		tree[(size_t)(node - 1) * 4 + 1] = here.delta[node][0];
		tree[(size_t)(node - 1) * 4 + 2] = here.delta[node][1];
		tree[(size_t)(node - 1) * 4 + 3] = here.delta[node][2];
	}

	out.assign((size_t)here.nodes * 12, 0.0f);

	return HDFramePose(&frame[0], here.nodes > 1 ? &tree[0] : 0, here.nodes,
		&out[0]);
}

/* The classic mesh of one node, its vertices in the slot's frame at the bind
 * pose (the mesh's own coordinates plus the node's bind offset). */
static int ClassicNodeVertices(const Tr5Level& level, const ClassicSlot& here,
	int node, std::vector<float>& out)
{
	Tr5Level::MoveableInfo info;
	const std::vector<uint8_t>& bytes = level.debugBytes();
	size_t at = 0;

	out.clear();

	if (!level.moveableInfo(here.slot, info) ||
		!level.debugMeshAt(info.meshIndex + node, at) || at + 12 > bytes.size())
		return 0;

	int16_t word = 0;

	memcpy(&word, &bytes[at + 10], 2);

	const int verts = word & 0xFF;

	if (at + 12 + (size_t)verts * 6 > bytes.size())
		return 0;

	for (int v = 0; v < verts; v++)
	{
		int16_t xyz[3];

		memcpy(xyz, &bytes[at + 12 + (size_t)v * 6], 6);

		for (int k = 0; k < 3; k++)
			out.push_back((float)xyz[k] + here.bind[node][k]);
	}

	return verts;
}

/* The skeleton fit as hd_object.cpp asks it (skeleton_fit_of): the model's
 * own single-weight centres (HDModelNodeCentre, the engine's numbers)
 * against the classic meshes' centres in the bind pose, read out of the
 * .TRC instead of mesh_vtxbuf. -1 when no node can be measured. */
static float ToolFitOf(const Tr5Level& level, const ClassicSlot& here,
	int slot)
{
	const int nodes = here.nodes < 32 ? here.nodes : 32;
	float trm[32 * 3], mesh[32 * 3];
	int trmCount[32], meshCount[32];

	for (int node = 0; node < nodes; node++)
	{
		std::vector<float> cv;

		trmCount[node] = HDModelNodeCentre(slot, node, trm + node * 3);
		meshCount[node] = ClassicNodeVertices(level, here, node, cv);

		double sum[3] = { 0, 0, 0 };

		for (int v = 0; v < meshCount[node]; v++)
			for (int k = 0; k < 3; k++)
				sum[k] += cv[(size_t)v * 3 + k];

		for (int k = 0; k < 3; k++)
			mesh[node * 3 + k] = meshCount[node]
				? (float)(sum[k] / meshCount[node]) : 0.0f;
	}

	return HDSkeletonFitWorst(trm, trmCount, mesh, meshCount, nodes);
}

/* The pocket as hd_object.cpp asks it (pocket_of): the same walk over the
 * same words, the animations, changes and ranges read out of the .TRC -
 * an animation entry is 40 bytes, a change 6, a range 8. */
static bool ToolPocketOf(const Tr5Level& level, int slot, bool open = false)
{
	Tr5Level::MoveableInfo info;
	size_t animAt = 0, changeAt = 0, rangeAt = 0;
	int32_t anims = 0, changeCount = 0, rangeCount = 0;

	if (!level.moveableInfo(slot, info) || info.animIndex < 0 ||
		!level.debugAnimBlock(animAt, anims))
		return false;

	level.debugChangeBlock(changeAt, changeCount);
	level.debugRangeBlock(rangeAt, rangeCount);

	const std::vector<uint8_t>& bytes = level.debugBytes();
	std::map<int, std::vector<short> > cache;

	auto word = [&](size_t at) -> short
	{
		short s = 0;

		if (at + 2 <= bytes.size())
			memcpy(&s, &bytes[at], 2);

		return s;
	};

	auto frames = [&](int anim, const short** keyframes, int* keys,
		int* size) -> bool
	{
		if (anim < 0 || anim >= anims)
			return false;

		const size_t e = animAt + (size_t)anim * 40;
		uint32_t offset = 0;

		memcpy(&offset, &bytes[e], 4);

		const int rate = bytes[e + 4];
		const int base = word(e + 24), end = word(e + 26);

		*size = bytes[e + 5];
		*keys = rate > 0 && end >= base ? (end - base) / rate + 1 : 0;

		if (*keys < 1 || *size < 1)
			return false;

		std::vector<short>& own = cache[anim];

		if (own.empty())
			for (int i = 0; i < *keys * *size; i++)
				own.push_back(level.debugReadFrameShort(
					(long long)(offset / 2) + i));

		*keyframes = &own[0];
		return true;
	};

	auto links = [&](int anim, int* out, int most) -> int
	{
		const size_t e = animAt + (size_t)anim * 40;
		int count = 0;

		if (count < most)
			out[count++] = word(e + 28);

		const int changesOf = word(e + 32), first = word(e + 34);

		for (int c = 0; c < changesOf && first + c < changeCount; c++)
		{
			const size_t ce = changeAt + (size_t)(first + c) * 6;
			const int ranges = word(ce + 2), firstRange = word(ce + 4);

			for (int r = 0; r < ranges && count < most &&
				firstRange + r < rangeCount; r++)
				out[count++] = word(rangeAt + (size_t)(firstRange + r) * 8 + 4);
		}

		return count;
	};

	/* The slot's own run ends where the next object's begins, as the
	 * engine's walk ends it (hd_object.cpp, pocket_of); `open` follows
	 * every link instead - the walk before that bound. */
	std::vector<int> starts;

	for (int other : level.moveableSlots())
	{
		Tr5Level::MoveableInfo them;

		if (other != slot && level.moveableInfo(other, them) &&
			them.animIndex >= 0)
			starts.push_back(them.animIndex);
	}

	const int end = open || starts.empty() ? -1
		: HDPocketAnimEnd(info.animIndex, &starts[0], (int)starts.size());

	return HDSlotSlidesIntoPocket(info.animIndex, end, frames, links);
}

/* The faces of one node's classic mesh, read out of the .TRC as the engine
 * reads them out of mesh_vtxbuf: per face its effects word and its
 * texture's draw type. The mesh is the header (x, y, z, radius, flags and
 * the vertex count word), the vertices, the normals or the lights, the
 * quads at twelve bytes and the triangles at ten, the texture word and the
 * effects word last. */
struct ToolFace
{
	bool quad;
	int effects;
	int drawtype;
};

static int ToolNodeFaces(const Tr5Level& level, int slot, int node,
	std::vector<ToolFace>& out)
{
	Tr5Level::MoveableInfo info;
	const std::vector<uint8_t>& bytes = level.debugBytes();
	size_t at = 0, texAt = 0;
	int32_t texCount = 0;

	out.clear();

	if (!level.moveableInfo(slot, info) || node >= info.nmeshes ||
		!level.debugMeshAt(info.meshIndex + node, at) ||
		!level.debugTexinfoBlock(texAt, texCount) || at + 12 > bytes.size())
		return 0;

	auto word = [&](size_t p) -> int
	{
		short s = 0;

		if (p + 2 <= bytes.size())
			memcpy(&s, &bytes[p], 2);

		return s;
	};

	const int verts = word(at + 10) & 0xFF;
	size_t p = at + 12 + (size_t)verts * 6;
	const int normals = word(p);

	p += 2 + (size_t)verts * (normals >= 0 ? 6 : 2);

	for (int kind = 0; kind < 2; kind++)
	{
		const int faces = word(p);
		const int corners = kind == 0 ? 4 : 3;

		p += 2;

		for (int f = 0; f < faces && p + corners * 2 + 4 <= bytes.size(); f++)
		{
			const int tex = word(p + corners * 2) & 0x7FFF;
			ToolFace face;

			face.quad = kind == 0;
			face.effects = word(p + corners * 2 + 2) & 0xFFFF;
			face.drawtype = tex < texCount
				? (word(texAt + (size_t)tex * 40) & 0xFFFF) : -1;
			out.push_back(face);
			p += (size_t)corners * 2 + 4;
		}
	}

	return (int)out.size();
}

/* The nodes whose classic mesh has a see-through face, a bit per node - the
 * engine's see_through_nodes_of (hd_object.cpp) over the .TRC. */
static unsigned int ToolSeeThroughNodes(const Tr5Level& level, int slot,
	int nodes)
{
	unsigned int mask = 0;
	std::vector<ToolFace> faces;

	for (int node = 0; node < nodes && node < 32; node++)
	{
		ToolNodeFaces(level, slot, node, faces);

		for (const ToolFace& face : faces)
			mask = HDSeeThroughNodes(mask, node,
				HDClassicFaceSeeThrough(face.effects, face.drawtype));
	}

	return mask;
}

/* A band of what a glass adds at full light: how many texels, and the luma
 * at their tenth, half and nine tenths. */
struct ToolBand
{
	int texels = 0;
	float p10 = 0, p50 = 0, p90 = 0;
};

static ToolBand ToolBandOf(std::vector<float>& v)
{
	ToolBand band;

	if (v.empty())
		return band;

	std::sort(v.begin(), v.end());
	band.texels = (int)v.size();
	band.p10 = v[(size_t)(0.1 * (v.size() - 1))];
	band.p50 = v[(size_t)(0.5 * (v.size() - 1))];
	band.p90 = v[(size_t)(0.9 * (v.size() - 1))];
	return band;
}

/* Every texel centre of a page `w` x `h` inside the triangle (ax, ay),
 * (bx, by), (cx, cy) in texel units; the nearest texel when none is. */
template <typename Take>
static void ToolTexelsOf(float ax, float ay, float bx, float by, float cx,
	float cy, long w, long h, Take take)
{
	const float x0 = (std::min)(ax, (std::min)(bx, cx));
	const float x1 = (std::max)(ax, (std::max)(bx, cx));
	const float y0 = (std::min)(ay, (std::min)(by, cy));
	const float y1 = (std::max)(ay, (std::max)(by, cy));
	const float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
	long found = 0;

	for (long y = y0 < 0 ? 0 : (long)y0; y <= (long)y1 && y < h; y++)
		for (long x = x0 < 0 ? 0 : (long)x0; x <= (long)x1 && x < w; x++)
		{
			const float px = x + 0.5f, py = y + 0.5f;
			const float e0 = (bx - ax) * (py - ay) - (px - ax) * (by - ay);
			const float e1 = (cx - bx) * (py - by) - (px - bx) * (cy - by);
			const float e2 = (ax - cx) * (py - cy) - (px - cx) * (ay - cy);

			if (area >= 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0)
				: (e0 <= 0 && e1 <= 0 && e2 <= 0))
			{
				take(x, y);
				found++;
			}
		}

	if (!found)
	{
		long x = (long)((ax + bx + cx) / 3), y = (long)((ay + by + cy) / 3);

		take(x < 0 ? 0 : x >= w ? w - 1 : x, y < 0 ? 0 : y >= h ? h - 1 : y);
	}
}

/* What the classic adds through the see-through faces of the nodes in
 * `see` at full light: each face's texels on the level's 32-bit page, their
 * luma - the classic adds the texel times its light, ONE / ONE, and reads no
 * alpha for it (specific/polyinsert.cpp, case 2). */
static ToolBand ToolClassicBand(const Tr5Level& level, int slot, int nodes,
	unsigned int see)
{
	const std::vector<uint8_t>& bytes = level.debugBytes();
	std::vector<float> lumas;
	ToolBand none;

	if (!see || bytes.size() < 18)
		return none;

	static const Tr5Level* s_level = 0;
	static std::vector<unsigned char> s_tex32;

	if (s_level != &level)
	{
		unsigned int sizes[2];

		memcpy(sizes, &bytes[10], 8);
		s_tex32.assign(sizes[0], 0);

		uLongf length = sizes[0];

		if (18 + (size_t)sizes[1] > bytes.size() || uncompress(s_tex32.data(),
			&length, &bytes[18], sizes[1]) != Z_OK)
			s_tex32.clear();

		s_level = &level;
	}

	const int pages = (int)(s_tex32.size() / 0x40000);
	Tr5Level::MoveableInfo info;
	size_t texAt = 0;
	int32_t texCount = 0;

	if (!pages || !level.moveableInfo(slot, info) ||
		!level.debugTexinfoBlock(texAt, texCount))
		return none;

	auto word = [&](size_t p) -> int
	{
		short s = 0;

		if (p + 2 <= bytes.size())
			memcpy(&s, &bytes[p], 2);

		return s;
	};

	for (int node = 0; node < nodes && node < 32 && node < info.nmeshes;
		node++)
	{
		size_t at = 0;

		if (!(see & (1u << node)) ||
			!level.debugMeshAt(info.meshIndex + node, at))
			continue;

		const int verts = word(at + 10) & 0xFF;
		size_t p = at + 12 + (size_t)verts * 6;
		const int normals = word(p);

		p += 2 + (size_t)verts * (normals >= 0 ? 6 : 2);

		for (int kind = 0; kind < 2; kind++)
		{
			const int faces = word(p);
			const int corners = kind == 0 ? 4 : 3;

			p += 2;

			for (int f = 0; f < faces && p + corners * 2 + 4 <= bytes.size();
				f++)
			{
				const int tex = word(p + corners * 2) & 0x7FFF;
				const int effects = word(p + corners * 2 + 2) & 0xFFFF;

				p += (size_t)corners * 2 + 4;

				if (tex >= texCount)
					continue;

				const size_t info40 = texAt + (size_t)tex * 40;
				const int drawtype = word(info40) & 0xFFFF;
				const int page = word(info40 + 2) & 0x7FFF;

				if (!HDClassicFaceSeeThrough(effects, drawtype) ||
					page >= pages)
					continue;

				float uv[4][2];

				for (int k = 0; k < 4; k++)
				{
					const size_t corner = info40 + 6 + 4 * (size_t)k;

					uv[k][0] = (float)((word(corner) & 0xFFFF) >> 8);
					uv[k][1] = (float)((word(corner + 2) & 0xFFFF) >> 8);
				}

				auto take = [&](long x, long y)
				{
					const unsigned char* t = &s_tex32[(size_t)page * 0x40000 +
						(size_t)y * 1024 + (size_t)x * 4];

					lumas.push_back(0.299f * t[2] + 0.587f * t[1] +
						0.114f * t[0]);
				};

				ToolTexelsOf(uv[0][0], uv[0][1], uv[1][0], uv[1][1], uv[2][0],
					uv[2][1], 256, 256, take);

				if (corners == 4)
					ToolTexelsOf(uv[0][0], uv[0][1], uv[2][0], uv[2][1],
						uv[3][0], uv[3][1], 256, 256, take);
			}
		}
	}

	return ToolBandOf(lumas);
}

/* What the HD glass of a model adds at full light, per texel of its glass
 * triangles on the nodes in `see`: the page's luma times its alpha, as the
 * glass step adds it, times the part's opacity when `weighed`. The pages
 * are read out of the install's TEX folder, or a sibling game's. */
static ToolBand ToolGlassBand(const HDTrmModel& trm, const std::string& root5,
	unsigned int see, bool weighed)
{
	std::map<int, std::vector<unsigned long>> pixels;
	std::map<int, std::pair<long, long>> shape;
	std::vector<float> adds;

	auto page_of = [&](int page) -> const std::vector<unsigned long>&
	{
		if (pixels.count(page))
			return pixels[page];

		std::vector<unsigned long>& px = pixels[page];
		const std::string bundle = ParentOf(root5);
		const std::string roots[3] = { root5, bundle + "\\4", bundle + "\\6" };

		for (const std::string& root : roots)
		{
			char path[1024];

			HDScanPagePath(root.c_str(), page, path, sizeof(path));

			FILE* f = fopen(path, "rb");

			if (!f)
				continue;

			std::vector<unsigned char> file;

			fseek(f, 0, SEEK_END);
			file.resize((size_t)ftell(f));
			fseek(f, 0, SEEK_SET);
			fread(file.data(), 1, file.size(), f);
			fclose(f);

			long w = 0, h = 0;
			const char* why = 0;
			unsigned long* decoded = BC7DecodeDDS(file.data(),
				(unsigned long)file.size(), 8192, &w, &h, &why);

			if (decoded)
			{
				px.assign(decoded, decoded + (size_t)w * h);
				shape[page] = std::make_pair(w, h);
				free(decoded);
			}

			break;
		}

		return px;
	};

	for (int part = 0; part < trm.parts; part++)
	{
		const HDTrmPart& p = trm.part[part];

		if (p.material != HD_TRM_MATERIAL_GLASS)
			continue;

		const float opacity = weighed
			? HDGlassOpacity(p.param[HD_TRM_PARAM_OPACITY]) : 1.0f;

		for (unsigned int i = 0; i + 2 < p.alpha; i += 3)
		{
			const unsigned int at = p.opaqueEnd + i;

			if (at + 2 >= (unsigned int)trm.indices)
				break;

			int weight[256] = { 0 };
			int best = 0;

			for (int c = 0; c < 3; c++)
			{
				const HDTrmVertex& v = trm.vertex[trm.index[at + c]];

				weight[v.bone[0]] += v.weight[0];
				weight[v.bone[1]] += v.weight[1];
				weight[v.bone[2]] += v.weight[2];
			}

			for (int b = 1; b < 256; b++)
				if (weight[b] > weight[best])
					best = b;

			if (!HDTrmTriangleIsGlass(HD_TRM_RUN_ALPHA, p.material, best, see))
				continue;

			const HDTrmVertex& a = trm.vertex[trm.index[at]];
			const HDTrmVertex& b = trm.vertex[trm.index[at + 1]];
			const HDTrmVertex& c = trm.vertex[trm.index[at + 2]];

			if (a.layer < 1 || a.layer > trm.textures)
				continue;

			const int page = (int)trm.texture[a.layer - 1];
			const std::vector<unsigned long>& px = page_of(page);

			if (px.empty())
				continue;

			const long w = shape[page].first, h = shape[page].second;

			ToolTexelsOf(a.u / 255.0f * w, a.v / 255.0f * h,
				b.u / 255.0f * w, b.v / 255.0f * h,
				c.u / 255.0f * w, c.v / 255.0f * h, w, h,
				[&](long x, long y)
				{
					const unsigned long t = px[(size_t)y * w + x];
					const float luma = 0.299f * ((t >> 16) & 255) +
						0.587f * ((t >> 8) & 255) + 0.114f * (t & 255);

					adds.push_back(luma * (float)(t >> 24) / 255.0f *
						opacity);
				});
		}
	}

	return ToolBandOf(adds);
}

/* -alpha: a slot's model taken apart for the glass rule. Per node of the
 * classic tree its faces - how many, how many see-through by the effects
 * word, by the draw type - and per run of every .TRM part on every node the
 * part's material and parameters, the triangles and what the HD path draws
 * them as: opaque, alpha test, additive, or glass (hd_object_rules.h,
 * HDTrmTriangleIsGlass). A triangle's node is the bone with the most
 * weight over its three corners. `brief` prints the one line the -all
 * table takes. */
static void PrintAlpha(const Tr5Level& level, const std::string& root5,
	int slot, int nodes, bool brief)
{
	const unsigned int see = ToolSeeThroughNodes(level, slot, nodes);
	const char* name = HDModelName(slot);
	int blended = 0;
	const int glass = HDModelGlassRanges(slot, &blended);

	/* The classic's band and the glass's, what the glass is added times
	 * and what the classic's band would hold it to on top
	 * (hd_object_rules.h, HDGlassBandFactor): read only for a slot whose
	 * glass is drawn. */
	HDModelGlassCount count;
	ToolBand classicBand, glassBand;
	float bandFactor = 1.0f;

	HDModelGlassCounts(slot, &count);

	if (blended)
	{
		HDTrmModel bandTrm;
		const char* bandWhy = 0;
		const std::string bandPath = name ? TrmPath(root5, name)
			: std::string();

		memset(&bandTrm, 0, sizeof(bandTrm));
		classicBand = ToolClassicBand(level, slot, nodes, see);

		if (!bandPath.empty() &&
			HDTrmLoad(bandPath.c_str(), &bandTrm, &bandWhy))
		{
			// Weighed as the draw weighs it: by the opacity where the
			// model's drawn glass is added times it (HDGlassWeight).
			glassBand = ToolGlassBand(bandTrm, root5, see,
				count.factor < 1.0f);
			HDTrmFree(&bandTrm);
		}

		bandFactor = HDGlassBandFactor(classicBand.p90, glassBand.p90);
	}

	if (brief)
	{
		if (see || glass)
			printf("  alpha slot=%d name=%s model=%s see_through=0x%X "
				"glass_ranges=%d drawn_as_glass=%d classic=%.1f/%.1f/%.1f "
				"glass=%.1f/%.1f/%.1f opacity=%.3f factor=%.3f band=%.3f "
				"oneside=%d copies=%d/%d\n", slot,
				HDSlotName(slot) ? HDSlotName(slot) : "?", name ? name : "-",
				see, glass, blended, classicBand.p10, classicBand.p50,
				classicBand.p90, glassBand.p10, glassBand.p50, glassBand.p90,
				count.opacity, count.factor, bandFactor, count.sided,
				count.dropped, count.copies);
		return;
	}

	printf("\n-- alpha: %s, slot %d ---------------------------------\n",
		name ? name : "-", slot);
	printf("  node  faces  effects-bit-0  drawtype-1  drawtype-2  "
		"see-through\n");

	for (int node = 0; node < nodes && node < 32; node++)
	{
		std::vector<ToolFace> faces;
		int effect = 0, tested = 0, added = 0;

		ToolNodeFaces(level, slot, node, faces);

		for (const ToolFace& face : faces)
		{
			effect += (face.effects & 1) ? 1 : 0;
			tested += face.drawtype == 1 ? 1 : 0;
			added += face.drawtype == 2 ? 1 : 0;
		}

		printf("  %4d  %5d  %13d  %10d  %10d  %s\n", node, (int)faces.size(),
			effect, tested, added, (see & (1u << node)) ? "yes" : "no");
	}

	const std::string path = name ? TrmPath(root5, name) : std::string();
	HDTrmModel trm;
	const char* why = 0;

	memset(&trm, 0, sizeof(trm));

	if (path.empty() || !HDTrmLoad(path.c_str(), &trm, &why))
	{
		printf("  no .TRM to read (%s)\n", why ? why : "no file");
		return;
	}

	static const char* kRun[3] = { "opaque", "alpha", "additive" };

	printf("  part  material  param                       run       node  "
		"triangles  drawn as\n");

	for (int part = 0; part < trm.parts; part++)
	{
		const HDTrmPart& p = trm.part[part];
		const unsigned int start[3] = { p.start, p.opaqueEnd, p.alphaEnd };
		const unsigned int count[3] = { p.opaque, p.alpha, p.additive };

		for (int run = 0; run < 3; run++)
		{
			std::map<int, int> perNode;

			for (unsigned int i = 0; i + 2 < count[run]; i += 3)
			{
				const unsigned int at = start[run] + i;

				if (at + 2 >= (unsigned int)trm.indices)
					break;

				int weight[256] = { 0 };
				int best = 0;

				for (int c = 0; c < 3; c++)
				{
					const HDTrmVertex& v = trm.vertex[trm.index[at + c]];

					weight[v.bone[0]] += v.weight[0];
					weight[v.bone[1]] += v.weight[1];
					weight[v.bone[2]] += v.weight[2];
				}

				for (int b = 1; b < 256; b++)
					if (weight[b] > weight[best])
						best = b;

				perNode[best]++;
			}

			for (const std::pair<const int, int>& n : perNode)
			{
				const char* as = run == HD_TRM_RUN_ADDITIVE ? "additive"
					: HDTrmTriangleIsGlass(run, p.material, n.first, see)
						? "glass (blended)"
					: run == HD_TRM_RUN_ALPHA ? "alpha test"
					: HDTrmMaterialCuts(p.material) ? "opaque, cut material"
					: "opaque";

				printf("  %4d  %8u  %6.3g %6.3g %6.3g %6.3g  %-8s  %4d  %9d"
					"  %s\n",
					part, p.material, p.param[0], p.param[1], p.param[2],
					p.param[3], kRun[run], n.first, n.second, as);
			}
		}
	}

	printf("  glass ranges %d, drawn as glass %d\n", glass, blended);
	printf("  drawn as glass: %d one-sided; additive copies of the glass %d, "
		"left out %d; opacity %.3f, added times %.3f\n", count.sided,
		count.copies, count.dropped, count.opacity, count.factor);
	printf("  at full light, luma: classic see-through texels %d, tenth "
		"%.1f half %.1f nine tenths %.1f; the glass %d, tenth %.1f half %.1f "
		"nine tenths %.1f; the classic's band holds it to %.3f\n",
		classicBand.texels, classicBand.p10, classicBand.p50, classicBand.p90,
		glassBand.texels, glassBand.p10, glassBand.p50, glassBand.p90,
		bandFactor);
	HDTrmFree(&trm);
}

/* -bindcheck: the skeleton test of hd_model.cpp taken apart for one slot.
 *
 * Per node the classic bind offset and parent, the centre and spread of the
 * classic mesh in the bind pose, and the same two numbers of the .TRM's
 * single-weight vertices of that node - the very working set the bind drift
 * is computed from - with their difference. Per pair of nodes the drift's own
 * term. Then every animation the slot owns, keyframe by keyframe: how far
 * each node's origin has moved and turned against the animation's first
 * keyframe, which names the node an animation opens. */
static void PrintBindCheck(const Tr5Level& level, const std::string& root5,
	int slot, const ClassicSlot& here, bool brief)
{
	const char* name = HDModelName(slot);
	const std::string path = name ? TrmPath(root5, name) : std::string();
	HDTrmModel trm;
	const char* why = 0;

	memset(&trm, 0, sizeof(trm));
	if (!brief)
		printf("\n-- bindcheck: %s, slot %d ------------------------------\n",
			name ? name : "-", slot);

	if (path.empty() || !HDTrmLoad(path.c_str(), &trm, &why))
	{
		printf("  no .TRM to read (%s)\n", why ? why : "no file");
		return;
	}

	const int nodes = here.nodes < 32 ? here.nodes : 32;
	double hc[32][3] = { { 0 } }, hs[32] = { 0 };
	int hn[32] = { 0 };

	/* The box of the vertices each node LEADS (its largest weight), beside
	 * the classic mesh's box: the place measured the second way, by box
	 * centres rather than by vertex means. */
	float blo[32][3], bhi[32][3];
	bool bhas[32] = { false };

	for (int v = 0; v < trm.vertices; v++)
	{
		const HDTrmVertex& in = trm.vertex[v];
		int lead = 0;

		for (int w = 1; w < 3; w++)
			if (in.weight[w] > in.weight[lead])
				lead = w;

		const int ln = in.bone[lead];
		const float at[3] = { in.x, in.y, in.z };

		if (ln < 32 && in.weight[lead])
		{
			for (int k = 0; k < 3; k++)
			{
				if (!bhas[ln] || at[k] < blo[ln][k]) blo[ln][k] = at[k];
				if (!bhas[ln] || at[k] > bhi[ln][k]) bhi[ln][k] = at[k];
			}

			bhas[ln] = true;
		}

		if (in.weight[0] != 255 || in.bone[0] >= 32)
			continue;

		const int n = in.bone[0];

		hn[n]++;
		hc[n][0] += in.x;
		hc[n][1] += in.y;
		hc[n][2] += in.z;
		hs[n] += (double)in.x * in.x + (double)in.y * in.y +
			(double)in.z * in.z;
	}

	double cc[32][3] = { { 0 } }, cs[32] = { 0 };
	int cn[32] = { 0 };
	double boxWorst = -1;

	if (!brief)
		printf("  %-4s %-6s %-21s %-21s %6s %-21s %6s %5s %-17s %5s\n", "node",
			"parent", "classic bind", "classic centre", "spread",
			"TRM centre", "spread", "vtx", "TRM - classic", "cls");

	for (int node = 0; node < nodes; node++)
	{
		std::vector<float> cv;
		const int count = ClassicNodeVertices(level, here, node, cv);
		double sq = 0;

		for (int v = 0; v < count; v++)
			for (int k = 0; k < 3; k++)
			{
				cc[node][k] += cv[(size_t)v * 3 + k];
				sq += (double)cv[(size_t)v * 3 + k] * cv[(size_t)v * 3 + k];
			}

		cn[node] = count;

		if (count && bhas[node])
		{
			double d = 0;
			float clo[3], chi[3];

			for (int k = 0; k < 3; k++)
			{
				float lo = cv[(size_t)k], hi = cv[(size_t)k];

				for (int v = 1; v < count; v++)
				{
					const float x = cv[(size_t)v * 3 + k];

					lo = x < lo ? x : lo;
					hi = x > hi ? x : hi;
				}

				const double e = (blo[node][k] + bhi[node][k]) * 0.5 -
					(lo + hi) * 0.5;

				clo[k] = lo;
				chi[k] = hi;
				d += e * e;
			}

			boxWorst = sqrt(d) > boxWorst ? sqrt(d) : boxWorst;

			if (!brief)
				printf("  %-4d boxes: classic x %.0f..%.0f y %.0f..%.0f z "
					"%.0f..%.0f  TRM x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f\n",
					node, clo[0], chi[0], clo[1], chi[1], clo[2], chi[2],
					blo[node][0], bhi[node][0], blo[node][1], bhi[node][1],
					blo[node][2], bhi[node][2]);
		}

		if (count)
		{
			for (int k = 0; k < 3; k++)
				cc[node][k] /= count;

			const double l = cc[node][0] * cc[node][0] +
				cc[node][1] * cc[node][1] + cc[node][2] * cc[node][2];

			cs[node] = sqrt(sq / count - l > 0 ? sq / count - l : 0);
		}

		double tc[3] = { 0, 0, 0 }, ts = 0;

		if (hn[node])
		{
			for (int k = 0; k < 3; k++)
				tc[k] = hc[node][k] / hn[node];

			const double l = tc[0] * tc[0] + tc[1] * tc[1] + tc[2] * tc[2];

			const double e2 = hs[node] / hn[node] - l;

			ts = sqrt(e2 > 0 ? e2 : 0);
		}

		char parent[8];

		sprintf(parent, node ? "%d" : "root", here.parent[node]);

		if (!brief)
			printf("  %-4d %-6s %6.0f %6.0f %6.0f  %6.0f %6.0f %6.0f  %6.0f "
				"%6.0f %6.0f %6.0f  %6.0f %5d %5.0f %5.0f %5.0f %5d\n", node,
				parent, here.bind[node][0], here.bind[node][1],
				here.bind[node][2], cc[node][0], cc[node][1], cc[node][2],
				cs[node], tc[0], tc[1], tc[2], ts, hn[node],
				tc[0] - cc[node][0], tc[1] - cc[node][1], tc[2] - cc[node][2],
				cn[node]);

		for (int k = 0; k < 3; k++)
			hc[node][k] = tc[k];

		hs[node] = ts;
	}

	if (!brief)
		printf("\n  the drift's pairs (nodes with 16 or more single-weight "
			"vertices): against the bind joints, as the engine measures, and\n"
			"  against the classic meshes' own centres in the bind pose\n"
			"  %-5s %9s %9s %9s %7s %7s %7s %7s %7s\n", "pair", "TRM dist",
			"bind dist", "mesh dist", "spread", "spread", "joint", "mesh",
			"raw");

	double sum = 0, sumMesh = 0, sumRaw = 0;
	double worst = 0, worstMesh = 0, worstRaw = 0;
	int pairs = 0;

	for (int a = 0; a < nodes; a++)
		for (int b = a + 1; b < nodes; b++)
		{
			if (hn[a] < 16 || hn[b] < 16)
				continue;

			double m = 0, d = 0, c = 0;

			for (int k = 0; k < 3; k++)
			{
				m += (hc[a][k] - hc[b][k]) * (hc[a][k] - hc[b][k]);
				d += (here.bind[a][k] - here.bind[b][k]) *
					(here.bind[a][k] - here.bind[b][k]);
				c += (cc[a][k] - cc[b][k]) * (cc[a][k] - cc[b][k]);
			}

			double term = fabs(sqrt(m) - sqrt(d)) - hs[a] - hs[b];
			double mesh = fabs(sqrt(m) - sqrt(c)) - hs[a] - hs[b];
			const double raw = fabs(sqrt(m) - sqrt(c));

			term = term < 0 ? 0 : term;
			mesh = mesh < 0 ? 0 : mesh;
			// A node the classic slot draws nothing on has no centre.
			if (!cn[a] || !cn[b])
				mesh = 0;

			sum += term;
			sumMesh += mesh;
			sumRaw += raw;
			worst = term > worst ? term : worst;
			worstMesh = mesh > worstMesh ? mesh : worstMesh;
			worstRaw = raw > worstRaw ? raw : worstRaw;
			pairs++;

			if (!brief)
				printf("  %d-%-3d %9.0f %9.0f %9.0f %7.0f %7.0f %7.0f %7.0f "
					"%7.0f\n", a, b, sqrt(m), sqrt(d), sqrt(c), hs[a], hs[b],
					term, mesh, raw);
		}

	/* The fit: per node, how far the .TRM's centre stands from the classic
	 * mesh's own centre in the bind pose - no pair, no spread, the place
	 * itself, since that is where the bind fold puts both. `rel` is the
	 * same distance over the classic mesh's spread. */
	double fitSum = 0, fitWorst = 0, relWorst = 0;
	int fitNodes = 0;

	for (int node = 0; node < nodes; node++)
	{
		if (hn[node] < 16 || !cn[node])
			continue;

		const double dx = hc[node][0] - cc[node][0];
		const double dy = hc[node][1] - cc[node][1];
		const double dz = hc[node][2] - cc[node][2];
		const double fit = sqrt(dx * dx + dy * dy + dz * dz);
		const double rel = fit / (cs[node] > 1 ? cs[node] : 1);

		fitSum += fit;
		fitWorst = fit > fitWorst ? fit : fitWorst;
		relWorst = rel > relWorst ? rel : relWorst;
		fitNodes++;
	}

	printf("  bindcheck slot=%d name=%s model=%s pairs=%d joint=%.0f/%.0f "
		"mesh=%.0f/%.0f raw=%.0f/%.0f fit=%.0f/%.0f rel=%.2f nodes=%d "
		"box=%.0f\n",
		slot, HDSlotName(slot) ? HDSlotName(slot) : "?", name, pairs,
		pairs ? sum / pairs : 0.0, worst, pairs ? sumMesh / pairs : 0.0,
		worstMesh, pairs ? sumRaw / pairs : 0.0, worstRaw,
		fitNodes ? fitSum / fitNodes : 0.0, fitWorst, relWorst, fitNodes,
		boxWorst);

	if (brief)
	{
		HDTrmFree(&trm);
		return;
	}

	/* The model's own twins: pairs of triangles whose three corners stand
	 * at the same three places (within half a unit), told apart by whether
	 * the two face the same way or opposite ways, with their area. Two such
	 * triangles drawn without culling meet at one depth everywhere. */
	{
		const int tris = trm.indices / 3;
		int same = 0, opposite = 0;
		double sameArea = 0, oppositeArea = 0;
		std::vector<std::array<float, 9> > at((size_t)tris);
		std::vector<int> nodeOf((size_t)tris);

		for (int t = 0; t < tris; t++)
			for (int c = 0; c < 3; c++)
			{
				const HDTrmVertex& v = trm.vertex[trm.index[t * 3 + c]];

				at[(size_t)t][c * 3 + 0] = v.x;
				at[(size_t)t][c * 3 + 1] = v.y;
				at[(size_t)t][c * 3 + 2] = v.z;
				nodeOf[(size_t)t] = v.bone[0];
			}

		for (int a = 0; a < tris; a++)
			for (int b = a + 1; b < tris; b++)
			{
				int match[3] = { -1, -1, -1 };
				bool all = true;

				for (int i = 0; i < 3 && all; i++)
				{
					all = false;

					for (int j = 0; j < 3; j++)
						if (fabs(at[a][i * 3] - at[b][j * 3]) < 0.5f &&
							fabs(at[a][i * 3 + 1] - at[b][j * 3 + 1]) < 0.5f &&
							fabs(at[a][i * 3 + 2] - at[b][j * 3 + 2]) < 0.5f)
						{
							match[i] = j;
							all = true;
							break;
						}
				}

				if (!all)
					continue;

				// Same winding when the corner map is a rotation.
				const bool rotation = (match[1] - match[0] + 3) % 3 == 1;
				const double ux = at[a][3] - at[a][0], uy = at[a][4] - at[a][1],
					uz = at[a][5] - at[a][2];
				const double vx = at[a][6] - at[a][0], vy = at[a][7] - at[a][1],
					vz = at[a][8] - at[a][2];
				const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz,
					cz = ux * vy - uy * vx;
				const double area = 0.5 * sqrt(cx * cx + cy * cy + cz * cz);

				if (rotation)
				{
					same++;
					sameArea += area;
				}
				else
				{
					opposite++;
					oppositeArea += area;
				}
			}

		printf("\n  twins: %d triangle pairs at the same places facing "
			"the same way (%.0f square units), %d facing opposite ways "
			"(%.0f), of %d triangles\n", same, sameArea, opposite,
			oppositeArea, tris);
	}

	int anims = 0;
	SlotAnim first;

	ReadSlotAnim(level, slot, 0, first, &anims);
	printf("\n  animations: %d\n", anims);

	for (int anim = 0; anim < anims; anim++)
	{
		SlotAnim a;

		if (!ReadSlotAnim(level, slot, anim, a, 0))
			continue;

		const int keys = (a.end - a.start) / a.rate + 1;
		std::vector<float> rest, pose;

		ReadAnimPose(level, here, anim, 0, rest);
		printf("  anim %d: frames %d..%d, rate %d, %d keyframes\n", anim,
			a.start, a.end, a.rate, keys);

		for (int key = 0; key < keys; key++)
		{
			if (!ReadAnimPose(level, here, anim, key, pose) || rest.empty())
				continue;

			/* The keyframe's own offset, words 6..8 - the part of a motion
			 * the node matrices above leave out (a door that slides). */
			const long long word = (long long)(a.frameOffset / 2) +
				(long long)key * a.size;

			printf("    key %-3d box %d..%d %d..%d %d..%d off %5d %5d %5d", key,
				level.debugReadFrameShort(word + 0),
				level.debugReadFrameShort(word + 1),
				level.debugReadFrameShort(word + 2),
				level.debugReadFrameShort(word + 3),
				level.debugReadFrameShort(word + 4),
				level.debugReadFrameShort(word + 5),
				level.debugReadFrameShort(word + 6),
				level.debugReadFrameShort(word + 7),
				level.debugReadFrameShort(word + 8));

			/* Each node's turn against the slot's rest (its first
			 * animation's first keyframe), beside the turn within this
			 * animation printed below. */
			{
				std::vector<float> home;

				if (ReadAnimPose(level, here, 0, 0, home))
					for (int node = 0; node < nodes; node++)
					{
						const float* p = &pose[(size_t)node * 12];
						const float* h = &home[(size_t)node * 12];
						double tr = 0;

						for (int i = 0; i < 3; i++)
							for (int j = 0; j < 3; j++)
								tr += h[i * 4 + j] * p[i * 4 + j];

						double c = (tr - 1.0) * 0.5;

						c = c > 1 ? 1 : c < -1 ? -1 : c;
						printf(" r%d %.0f", node,
							acos(c) * 180.0 / 3.14159265358979);
					}
			}

			for (int node = 0; node < nodes; node++)
			{
				const float* p = &pose[(size_t)node * 12];
				const float* r = &rest[(size_t)node * 12];
				const double dx = p[3] - r[3], dy = p[7] - r[7],
					dz = p[11] - r[11];
				// The angle between the two frames, from the trace of
				// R_rest^T * R_pose.
				double tr = 0;

				for (int i = 0; i < 3; i++)
					for (int j = 0; j < 3; j++)
						tr += r[i * 4 + j] * p[i * 4 + j];

				double c = (tr - 1.0) * 0.5;

				c = c > 1 ? 1 : c < -1 ? -1 : c;
				printf("  n%d %4.0f/%5.1fdeg", node,
					sqrt(dx * dx + dy * dy + dz * dz),
					acos(c) * 180.0 / 3.14159265358979);

				/* Where the two meshes of the node travel: the classic
				 * mesh's centre and the .TRM's, each in node space (the
				 * bind offset off), through this keyframe and the first -
				 * c and t the two travels, g the gap between the two
				 * centres in this keyframe. The bind fold puts a .TRM
				 * vertex through the same matrix, so a model that moves
				 * the right part has c = t. */
				if (!cn[node] || hn[node] < 16)
					continue;

				double ct[3], tt[3], cr[3], tr0[3];

				for (int i = 0; i < 3; i++)
				{
					const double cl[3] = { cc[node][0] - here.bind[node][0],
						cc[node][1] - here.bind[node][1],
						cc[node][2] - here.bind[node][2] };
					const double tl[3] = { hc[node][0] - here.bind[node][0],
						hc[node][1] - here.bind[node][1],
						hc[node][2] - here.bind[node][2] };

					ct[i] = p[i * 4 + 3];
					tt[i] = p[i * 4 + 3];
					cr[i] = r[i * 4 + 3];
					tr0[i] = r[i * 4 + 3];

					for (int j = 0; j < 3; j++)
					{
						ct[i] += p[i * 4 + j] * cl[j];
						tt[i] += p[i * 4 + j] * tl[j];
						cr[i] += r[i * 4 + j] * cl[j];
						tr0[i] += r[i * 4 + j] * tl[j];
					}
				}

				double cm = 0, tm = 0, gap = 0;

				for (int i = 0; i < 3; i++)
				{
					cm += (ct[i] - cr[i]) * (ct[i] - cr[i]);
					tm += (tt[i] - tr0[i]) * (tt[i] - tr0[i]);
					gap += (ct[i] - tt[i]) * (ct[i] - tt[i]);
				}

				printf(" c%.0f t%.0f g%.0f", sqrt(cm), sqrt(tm), sqrt(gap));
			}

			printf("\n");
		}
	}

	HDTrmFree(&trm);
}

/* -bones: every bone the .TRM weights, numbered as the file numbers it, with
 * the slot's classic node of the same number beside it where the slot has
 * one. Per bone the vertices it leads (its largest weight), their box and
 * the box's centre, and the vertices that ride on it alone with their
 * centre - the place the bind drift and the skeleton fit measure. A bone at
 * or past the slot's node count is marked: the engine has no matrix of its
 * own for it and refuses the model (hd_object.cpp, the skeleton test). The
 * classic box is the node's mesh in the bind pose, and `offset` is how far
 * the two box centres stand apart. */
static void PrintBoneTable(const Tr5Level* level, const std::string& root5,
	int slot, const ClassicSlot* here)
{
	const char* name = HDModelName(slot);
	const std::string path = name ? TrmPath(root5, name) : std::string();
	HDTrmModel trm;
	const char* why = 0;

	memset(&trm, 0, sizeof(trm));
	printf("\n-- bones: %s, slot %d --------------------------------\n",
		name ? name : "-", slot);

	if (path.empty() || !HDTrmLoad(path.c_str(), &trm, &why))
	{
		printf("  no .TRM to read (%s)\n", why ? why : "no file");
		return;
	}

	int lead[256] = { 0 }, single[256] = { 0 };
	float lo[256][3], hi[256][3];
	double sum[256][3] = { { 0 } };

	for (int v = 0; v < trm.vertices; v++)
	{
		const HDTrmVertex& in = trm.vertex[v];
		int w = 0;

		for (int k = 1; k < 3; k++)
			if (in.weight[k] > in.weight[w])
				w = k;

		const int b = in.bone[w];
		const float at[3] = { in.x, in.y, in.z };

		if (!in.weight[w])
			continue;

		for (int k = 0; k < 3; k++)
		{
			if (!lead[b] || at[k] < lo[b][k]) lo[b][k] = at[k];
			if (!lead[b] || at[k] > hi[b][k]) hi[b][k] = at[k];
		}

		lead[b]++;

		if (in.weight[0] == 255)
		{
			single[in.bone[0]]++;

			for (int k = 0; k < 3; k++)
				sum[in.bone[0]][k] += at[k];
		}
	}

	const int nodes = here ? here->nodes : 0;

	printf("  %-4s %6s %-30s %-18s %6s %-18s %s\n", "bone", "lead",
		"lead box (x y z)", "box centre", "alone", "alone centre",
		"classic node");

	for (int b = 0; b < 256; b++)
	{
		if (!lead[b])
			continue;

		float c[3];

		for (int k = 0; k < 3; k++)
			c[k] = (lo[b][k] + hi[b][k]) * 0.5f;

		char box[64], centre[48], alone[48], node[96];

		sprintf(box, "%.0f..%.0f %.0f..%.0f %.0f..%.0f", lo[b][0], hi[b][0],
			lo[b][1], hi[b][1], lo[b][2], hi[b][2]);
		sprintf(centre, "%.0f %.0f %.0f", c[0], c[1], c[2]);

		if (single[b])
			sprintf(alone, "%.0f %.0f %.0f", sum[b][0] / single[b],
				sum[b][1] / single[b], sum[b][2] / single[b]);
		else
			strcpy(alone, "-");

		if (!here)
			strcpy(node, "-");
		else if (b >= nodes)
			sprintf(node, "none - past the slot's %d nodes", nodes);
		else
		{
			std::vector<float> cv;
			const int count = level
				? ClassicNodeVertices(*level, *here, b, cv) : 0;

			if (!count)
				sprintf(node, "%d: no classic mesh", b);
			else
			{
				float clo[3], chi[3];

				for (int k = 0; k < 3; k++)
				{
					clo[k] = chi[k] = cv[(size_t)k];

					for (int v = 1; v < count; v++)
					{
						const float x = cv[(size_t)v * 3 + k];

						clo[k] = x < clo[k] ? x : clo[k];
						chi[k] = x > chi[k] ? x : chi[k];
					}
				}

				double d = 0;

				for (int k = 0; k < 3; k++)
				{
					const double e = c[k] - (clo[k] + chi[k]) * 0.5;

					d += e * e;
				}

				sprintf(node,
					"%d: %.0f..%.0f %.0f..%.0f %.0f..%.0f offset %.0f", b,
					clo[0], chi[0], clo[1], chi[1], clo[2], chi[2], sqrt(d));
			}
		}

		printf("  %-4d %6d %-30s %-18s %6d %-18s %s\n", b, lead[b], box, centre,
			single[b], alone, node);
	}

	HDTrmFree(&trm);
}

/* ------------------------------------------------------- the braid root -- */
/* The braid does not grip the head properly for every outfit (Todesengel's
 * models included).
 *
 * The three things that have to meet, all in the classic bind pose, which is
 * the pose every .TRM vertex is written in:
 *
 *   1. The root the game hangs the braid from. HairControl (game\hair.cpp) walks the
 *      mesh tree to the head bone and translates by (-4, -4, -48) in its own
 *      frame; the result is hairs[0][0], and the seam puts HAIR_OG's bone 0
 *      there. In the bind pose the head frame is axis aligned with the world,
 *      so the root is simply the head joint plus that triple.
 *   2. The head the braid comes out of - the outfit's own node 14 geometry and
 *      the separate LARA_HEAD that rides rigidly on it, which is where the
 *      hair and the tie are modelled.
 *   3. HAIR_OG's FIRST LINK, whose bone 0 sits at the model's origin: its
 *      vertices say how far the link reaches BACKWARDS out of that origin -
 *      negative z, since the braid's bind chain runs down +z (0, 56, 112 ..).
 *
 * The braid grips when link 0's backward reach covers the distance from the
 * root to the head's surface. This table prints all three per outfit and the
 * distance that is left over, which is the number a constant would have to
 * close - and which is 0 or less when nothing needs closing. */
static void PrintBraidTable(const std::string& root5, const ClassicSlot& body)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\nthe braid root needs this level's Lara tree, which is not "
			"readable here.\n");
		return;
	}

	const double hx = body.bind[HD_LARA_HEAD_NODE][0];
	const double hy = body.bind[HD_LARA_HEAD_NODE][1];
	const double hz = body.bind[HD_LARA_HEAD_NODE][2];
	const double rx = hx + HD_LARA_BRAID_ROOT_X;
	const double ry = hy + HD_LARA_BRAID_ROOT_Y;
	const double rz = hz + HD_LARA_BRAID_ROOT_Z;

	printf("\n-- the braid root, per outfit --------------------------------\n");
	printf("head joint %.0f/%.0f/%.0f, hair.cpp root %.0f/%.0f/%.0f "
		"(offset %d/%d/%d)\n", hx, hy, hz, rx, ry, rz,
		HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y, HD_LARA_BRAID_ROOT_Z);

	/* HAIR_OG's first link first - it is the same file for every outfit. */
	double reach = 0.0;
	int linkVerts = 0;
	{
		HDTrmModel og;
		const char* why = "";
		const std::string path = TrmPath(root5, "HAIR_OG");

		if (path.empty() || !HDTrmLoad(path.c_str(), &og, &why))
			printf("HAIR_OG not read - %s\n",
				path.empty() ? "no .TRM in 5 or 4" : why);
		else
		{
			double lo = 1.0e9, hi = -1.0e9;
			/* How thick the plait is. The push-out of
			 * a box needs a margin, and the margin is the braid's own radius:
			 * a link's axis is its bone's +z, so the radius is the furthest a
			 * vertex of that link stands from that axis. Measured per link and
			 * over the whole model, because the plait tapers. */
			double linkR[HD_LARA_HAIR_NODES];
			int linkN[HD_LARA_HAIR_NODES];
			double allR = 0.0;

			for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
			{
				linkR[n] = 0.0;
				linkN[n] = 0;
			}

			for (int i = 0; i < og.vertices; i++)
			{
				const HDTrmVertex& v = og.vertex[i];
				int bone = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					bone = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					bone = v.bone[2];

				const double r = sqrt((double)v.x * v.x + (double)v.y * v.y);

				if (r > allR)
					allR = r;

				if (bone >= 0 && bone < HD_LARA_HAIR_NODES)
				{
					linkN[bone]++;

					if (r > linkR[bone])
						linkR[bone] = r;
				}

				if (bone)
					continue;

				linkVerts++;

				if (v.z < lo) lo = v.z;
				if (v.z > hi) hi = v.z;
			}

			reach = linkVerts ? -lo : 0.0;
			printf("HAIR_OG link 0: %d vertices, z %.1f .. %.1f - it reaches "
				"%.1f units back out of its own origin\n",
				linkVerts, lo, hi, reach);
			printf("HAIR_OG thickness: radius about the bone axis");

			for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
				printf(" %d:%.1f(%d)", n, linkR[n], linkN[n]);

			printf(", worst %.1f\n", allR);
			HDTrmFree(&og);
		}
	}

	printf("\n%-19s %-10s %8s %8s %8s\n", "outfit", "part", "nearest",
		"reach", "left");
	printf("%-19s %-10s %8s %8s %8s\n", "-------------------", "----------",
		"--------", "--------", "--------");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);

		if (!stem)
			continue;

		/* The outfit's own node 14 and the separate face, in that order. The
		 * face is a facial RIG - its bones are not mesh tree nodes - so every
		 * one of its vertices counts, and it rides on the head joint. */
		for (int which = 0; which < 2; which++)
		{
			const char* name = which ? "LARA_HEAD" : stem;
			HDTrmModel model;
			const char* why = "";
			const std::string path = TrmPath(root5, name);

			if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			{
				printf("%-19s %-10s not read - %s\n", stem, name,
					path.empty() ? "no .TRM in 5 or 4" : why);
				continue;
			}

			double nearest = 1.0e9;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];

				if (!which)
				{
					int node = v.bone[0];

					if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
						node = v.bone[1];
					else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
						node = v.bone[2];

					if (node != HD_LARA_HEAD_NODE)
						continue;
				}

				const double dx = v.x - rx;
				const double dy = v.y - ry;
				const double dz = v.z - rz;
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				if (d < nearest)
					nearest = d;
			}

			if (nearest > 1.0e8)
				printf("%-19s %-10s %8s %8.1f %8s\n", stem,
					which ? "face" : "body", "-", reach, "-");
			else
				printf("%-19s %-10s %8.1f %8.1f %8.1f\n", stem,
					which ? "face" : "body", nearest, reach, nearest - reach);

			HDTrmFree(&model);
		}
	}

	printf("\n\"nearest\" is the distance from the root to the nearest vertex "
		"of that part,\n\"reach\" how far link 0 of HAIR_OG comes back out of "
		"the root towards it, and\n\"left\" the gap that stays open. A "
		"NEGATIVE left means the link is already\ninside the head and nothing "
		"needs moving.\n");

	/* --- Where the hair knot is ------------------------------------------ */
	/* The braid starts
	 * behind the skull beside the pack instead of coming out of the knot when the anchor is measured against the wrong surface: measuring
	 * the gap against the skull, which is the outfit's own
	 * node 14; the knot is on LARA_HEAD, and the two are not the same surface.
	 * So the head file is measured on its own: its box, its rearmost point,
	 * and the nearest of its vertices BEHIND the head joint - which is the
	 * knot, since nothing else of a face is back there. */
	{
		HDTrmModel head;
		const char* why = "";
		const std::string path = TrmPath(root5, "LARA_HEAD");

		if (path.empty() || !HDTrmLoad(path.c_str(), &head, &why))
			printf("\nLARA_HEAD not read - %s\n",
				path.empty() ? "no .TRM in 5 or 4" : why);
		else
		{
			double lo[3] = { 1.0e9, 1.0e9, 1.0e9 };
			double hi[3] = { -1.0e9, -1.0e9, -1.0e9 };
			double best = 1.0e9, bestAt[3] = { 0, 0, 0 };
			double rear = 1.0e9, rearAt[3] = { 0, 0, 0 };
			int behind = 0;

			for (int i = 0; i < head.vertices; i++)
			{
				const HDTrmVertex& v = head.vertex[i];
				const double p[3] = { (double)v.x, (double)v.y, (double)v.z };

				for (int a = 0; a < 3; a++)
				{
					if (p[a] < lo[a]) lo[a] = p[a];
					if (p[a] > hi[a]) hi[a] = p[a];
				}

				if (p[2] < rear)
				{
					rear = p[2];
					rearAt[0] = p[0]; rearAt[1] = p[1]; rearAt[2] = p[2];
				}

				// Behind the head JOINT is where the knot and the tie are.
				if (p[2] >= hz)
					continue;

				behind++;

				const double d = sqrt((p[0] - rx) * (p[0] - rx) +
					(p[1] - ry) * (p[1] - ry) + (p[2] - rz) * (p[2] - rz));

				if (d < best)
				{
					best = d;
					bestAt[0] = p[0]; bestAt[1] = p[1]; bestAt[2] = p[2];
				}
			}

			printf("\nLARA_HEAD box   x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f "
				"(%d vertices, %d of them behind the joint)\n",
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], head.vertices, behind);
			printf("rearmost vertex %.1f/%.1f/%.1f - the back of the knot\n",
				rearAt[0], rearAt[1], rearAt[2]);
			printf("nearest vertex behind the joint %.1f/%.1f/%.1f, %.1f units "
				"from the root\n", bestAt[0], bestAt[1], bestAt[2], best);
			printf("the root would have to move %.1f in z to sit on it "
				"(root_z %d -> %d)\n", bestAt[2] - rz, HD_LARA_BRAID_ROOT_Z,
				(int)(HD_LARA_BRAID_ROOT_Z + (bestAt[2] - rz) + 0.5));
			HDTrmFree(&head);
		}
	}

	/* --- The backpack ------------------------------------------------------ */
	/* The classic HairControl collides the braid with five spheres, all of
	 * them CLASSIC Lara meshes: hips, torso, head and the two upper arms. An
	 * HD outfit is a different body and its pack stands further out than the
	 * classic torso sphere reaches, so the HD braid walks through it. What
	 * follows is the number an added sphere needs: the outfit's own pack, in
	 * the TORSO NODE'S frame, which is the frame hair.cpp is in when it
	 * places sphere[1].
	 *
	 * "The pack" is defined geometrically rather than by texture page, so
	 * that the rule does not depend on how one outfit's artist split his
	 * materials: the node 7 vertices that sit more than 30 units BEHIND the
	 * torso joint. Nothing else of a torso does. */
	printf("\n-- the backpack, per outfit (torso joint %.0f/%.0f/%.0f) ------\n",
		body.bind[7][0], body.bind[7][1], body.bind[7][2]);
	printf("%-19s %7s %24s %8s\n", "outfit", "verts",
		"centre in the torso frame", "radius");
	printf("%-19s %7s %24s %8s\n", "-------------------", "-------",
		"------------------------", "--------");

	/* The pack as a box. Measurement shows the
	 * pack is a slab and that two spheres of radius 36 touch it on the centre
	 * line alone, so the braid lies in the pack instead of on it. A box needs
	 * six numbers and the spheres gave three, so they are taken here - over the
	 * whole pack point set and, separately, over the middle strip a braid can
	 * actually reach (|x| <= 30). */
	double boxLo[3] = { 1.0e9, 1.0e9, 1.0e9 };
	double boxHi[3] = { -1.0e9, -1.0e9, -1.0e9 };
	double stripLo[3] = { 1.0e9, 1.0e9, 1.0e9 };
	double stripHi[3] = { -1.0e9, -1.0e9, -1.0e9 };
	int boxOutfits = 0;
	char boxRows[HD_LARA_OUTFIT_COUNT][192];

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);

		if (!stem)
			continue;

		HDTrmModel model;
		const char* why = "";
		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		double lo[3] = { 1.0e9, 1.0e9, 1.0e9 };
		double hi[3] = { -1.0e9, -1.0e9, -1.0e9 };
		double slo[3] = { 1.0e9, 1.0e9, 1.0e9 };
		double shi[3] = { -1.0e9, -1.0e9, -1.0e9 };
		int count = 0;

		for (int pass = 0; pass < 2; pass++)
		{
			double radius = 0.0;
			const double cx = 0.5 * (lo[0] + hi[0]);
			const double cy = 0.5 * (lo[1] + hi[1]);
			const double cz = 0.5 * (lo[2] + hi[2]);

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != 7)
					continue;

				const double lx = v.x - body.bind[7][0];
				const double ly = v.y - body.bind[7][1];
				const double lz = v.z - body.bind[7][2];

				if (lz > -30.0)
					continue;

				if (!pass)
				{
					count++;

					if (lx < lo[0]) lo[0] = lx;
					if (ly < lo[1]) lo[1] = ly;
					if (lz < lo[2]) lo[2] = lz;
					if (lx > hi[0]) hi[0] = lx;
					if (ly > hi[1]) hi[1] = ly;
					if (lz > hi[2]) hi[2] = lz;

					// The middle strip, which is the width a braid can touch.
					if (lx >= -30.0 && lx <= 30.0)
					{
						if (lx < slo[0]) slo[0] = lx;
						if (ly < slo[1]) slo[1] = ly;
						if (lz < slo[2]) slo[2] = lz;
						if (lx > shi[0]) shi[0] = lx;
						if (ly > shi[1]) shi[1] = ly;
						if (lz > shi[2]) shi[2] = lz;
					}

					continue;
				}

				const double d = sqrt((lx - cx) * (lx - cx) +
					(ly - cy) * (ly - cy) + (lz - cz) * (lz - cz));

				if (d > radius)
					radius = d;
			}

			if (!pass)
			{
				if (!count)
					break;

				continue;
			}

			printf("%-19s %7d %8.0f %7.0f %7.0f %8.0f\n", stem, count,
				cx, cy, cz, radius);
		}

		if (!count)
			printf("%-19s %7s %24s %8s\n", stem, "-", "no pack", "-");
		else if (outfit != HD_LARA_OUTFIT_SUBSUIT)
		{
			/* The diving suit is not in the box (nor in the sphere pack
			 * either): what sits behind its
			 * torso is two thrusters, it wears a helmet and no braid, and its
			 * numbers are half as deep again as any pack's. */
			sprintf(boxRows[boxOutfits], "%-19s %7d  %6.0f..%-6.0f %6.0f..%-6.0f "
				"%6.0f..%-6.0f  %6.0f..%-6.0f %6.0f..%-6.0f", stem, count,
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2],
				slo[1], shi[1], slo[2], shi[2]);
			boxOutfits++;

			for (int a = 0; a < 3; a++)
			{
				if (lo[a] < boxLo[a]) boxLo[a] = lo[a];
				if (hi[a] > boxHi[a]) boxHi[a] = hi[a];
				if (slo[a] < stripLo[a]) stripLo[a] = slo[a];
				if (shi[a] > stripHi[a]) stripHi[a] = shi[a];
			}
		}

		HDTrmFree(&model);
	}

	printf("\nthe sphere HairControl adds for an HD Lara is placed from the "
		"torso node\nwith these offsets; HD_LARA_PACK_* in hd_lara.h carries "
		"the numbers.\n");

	/* --- The same pack as a box ------------------------------------------- */
	printf("\n-- the backpack as a BOX, in the torso frame -----------------\n");
	printf("%-19s %7s  %-13s %-13s %-13s  %-13s %-13s\n", "outfit", "verts",
		"x", "y", "z", "y (|x|<=30)", "z (|x|<=30)");
	printf("------------------- -------  ------------- ------------- "
		"-------------  ------------- -------------\n");

	for (int i = 0; i < boxOutfits; i++)
		printf("%s\n", boxRows[i]);

	if (boxOutfits)
	{
		printf("\nthe union over the %d packs: x %.0f..%.0f  y %.0f..%.0f  "
			"z %.0f..%.0f\n", boxOutfits, boxLo[0], boxHi[0], boxLo[1],
			boxHi[1], boxLo[2], boxHi[2]);
		printf("the union over the middle strip: y %.0f..%.0f  z %.0f..%.0f\n",
			stripLo[1], stripHi[1], stripLo[2], stripHi[2]);
		printf("HD_LARA_PACK_BOX_* in hd_lara.h carries one box for all of "
			"them; the braid is\npushed out of it along the smallest "
			"penetration, the rear face preferred.\n");
	}
}

/* ---------------------------------------- the neck as a cylinder -------- */
/*
 * Why a third neck table.
 *
 * A cut of the neck by a sphere around the eye, closed with a dome whose rim
 * is the body's own outline at s = 50, is wrong in both halves: the dome is a plate
 * the size of her whole chest, it pokes out through the collar, its middle is
 * a black smear, and the strips between its spokes are long slivers. The
 * cause is in the two numbers this table replaces: at s = 50 the body is no
 * longer a neck but a CHEST, so "the vertex furthest off the axis in each
 * sector" is a shoulder and the rim is the outline of her whole torso.
 *
 * A sphere around the eye is the wrong SHAPE for a neck. The neck is a tube
 * about the neck axis and nothing else is, so the cut this table measures is
 * a CYLINDER about that axis - depth S down it and radius R off it - which
 * takes the stump and cannot reach a chest, a shoulder or a forearm however
 * the eye moves. What comes out of it:
 *
 *   1. THE GRID. Node 7 vertices by depth and by radius. A tube reads as a
 *      narrow band of r that walks slowly outwards; where the row spreads
 *      across every column is where the shoulders join.
 *   2. THE RING. At the depth the cut ends, the innermost node 7 vertex in
 *      each of 32 sectors - the neck WALL, which is what the cap has to be
 *      sewn to - with its spread, so that "is this ring round?" is a number.
 *   3. THE SCAN. For a grid of (S, R): how much of the stump the cut takes,
 *      whether it reaches an arm, and how near the eye the nearest SURVIVING
 *      vertex is - which is the whole point of the cut and the thing that
 *      must come out large.
 */
static void PrintNeckCylinder(const std::string& root5, const ClassicSlot& body)
{
	const double kForward[2] = { 48.0, 0.0 };
	const int kSectors = 32;
	const double kPi = 3.14159265358979323846;

	printf("\n-- the neck as a CYLINDER, per outfit ------------------------\n");
	printf("node 7 vertices by depth s down the axis and radius r off it.\n"
		"A neck is a narrow band of r; the row that fills every column is\n"
		"where the shoulders join and the neck stops being one.\n\n");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		/* Two axes across it, the same pair the maker builds its sectors
		 * from, so a sector number here is the sector number there. */
		double u[3] = { axis[1], -axis[0], 0.0 };
		const double uLen = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);

		for (int i = 0; i < 3; i++)
			u[i] /= uLen > 1.0e-6 ? uLen : 1.0;

		const double w[3] =
		{
			axis[1] * u[2] - axis[2] * u[1],
			axis[2] * u[0] - axis[0] * u[2],
			axis[0] * u[1] - axis[1] * u[0]
		};

		/* --- the grid ---------------------------------------------------- */
		static const double kEdge[9] =
		{ 0, 10, 15, 20, 25, 30, 40, 60, 1.0e9 };

		printf("%s\n", stem);
		printf("%6s %6s %6s %6s %6s %6s %6s %6s %6s   %8s %8s\n", "s",
			"<10", "10-15", "15-20", "20-25", "25-30", "30-40", "40-60",
			"60+", "r min", "r max");

		for (int step = -2; step <= 14; step++)
		{
			const double at = step * 5.0;
			int bin[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			double lo = 1.0e9, hi = -1.0e9;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_TORSO)
					continue;

				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

				if (s < at - 2.5 || s >= at + 2.5)
					continue;

				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);

				for (int b = 0; b < 8; b++)
					if (r >= kEdge[b] && r < kEdge[b + 1])
					{
						bin[b]++;
						break;
					}

				if (r < lo) lo = r;
				if (r > hi) hi = r;
			}

			if (lo > 1.0e8)
				continue;

			printf("%6.0f %6d %6d %6d %6d %6d %6d %6d %6d   %8.1f %8.1f\n", at,
				bin[0], bin[1], bin[2], bin[3], bin[4], bin[5], bin[6], bin[7],
				lo, hi);
		}

		/* --- the ring at four candidate depths --------------------------- */
		printf("  the WALL ring (the innermost node 7 vertex in each of %d "
			"sectors):\n", kSectors);
		printf("  %6s %7s %8s %8s %8s %8s\n", "s", "sectors", "r min",
			"r mean", "r max", "gaps");

		for (int cand = 0; cand < 4; cand++)
		{
			const double at = 30.0 + cand * 5.0;
			double ring[32];
			int filled = 0;
			double lo = 1.0e9, hi = -1.0e9, sum = 0.0;

			for (int k = 0; k < kSectors; k++)
				ring[k] = 1.0e9;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_TORSO)
					continue;

				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

				if (s < at - 4.0 || s > at + 4.0)
					continue;

				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);
				const double a = atan2(off[0] * w[0] + off[1] * w[1] +
					off[2] * w[2], off[0] * u[0] + off[1] * u[1] +
					off[2] * u[2]);
				int k = (int)((a + kPi) * kSectors / (2.0 * kPi));

				if (k < 0) k = 0;
				if (k >= kSectors) k = kSectors - 1;

				if (r < ring[k])
					ring[k] = r;
			}

			for (int k = 0; k < kSectors; k++)
				if (ring[k] < 1.0e8)
				{
					filled++;
					sum += ring[k];

					if (ring[k] < lo) lo = ring[k];
					if (ring[k] > hi) hi = ring[k];
				}

			if (!filled)
				continue;

			printf("  %6.0f %7d %8.1f %8.1f %8.1f %8d\n", at, filled, lo,
				sum / filled, hi, kSectors - filled);
		}

		HDTrmFree(&model);
		printf("\n");
	}

	/* --- the scan --------------------------------------------------------- */
	printf("-- what a cylinder cut (S down, R off) leaves -----------------\n");
	printf("`stump` is how many node 7 vertices above the shoulder line the "
		"cut takes\nand `left` how many it leaves; `collar` is how many of "
		"those survivors sit in\nthe neck band s < 30, which is the COLLAR "
		"RING the first person view shows;\n`arm` is the nearest arm vertex "
		"the cylinder reaches (- is none); `nearest`\nis the nearest SURVIVING "
		"vertex of any node but the masked head, from the\ntwo eyes - the "
		"number the cut exists to raise.\n\n");
	printf("%-19s %4s %4s %7s %7s %7s %7s   %8s %8s\n", "outfit", "S", "R",
		"stump", "left", "collar", "arm", "eye 48", "eye 0");
	printf("------------------- ---- ---- ------- ------- ------- -------   "
		"-------- --------\n");

	const double shoulder =
		0.5 * (body.bind[HD_LARA_RINARM][1] + body.bind[HD_LARA_LINARM][1]);

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		static const double kS[3] = { 30.0, 40.0, 50.0 };
		static const double kR[3] = { 30.0, 34.0, 38.0 };

		for (int a = 0; a < 3; a++)
			for (int b = 0; b < 3; b++)
			{
				int cut = 0, left = 0, collar = 0;
				double arm = 1.0e9;
				double nearest[2] = { 1.0e9, 1.0e9 };

				for (int i = 0; i < model.vertices; i++)
				{
					const HDTrmVertex& v = model.vertex[i];
					int node = v.bone[0];

					if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
						node = v.bone[1];
					else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
						node = v.bone[2];

					if (node == HD_LARA_HEAD_NODE)
						continue;	// masked in first person anyway

					const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
					const double s = d[0] * axis[0] + d[1] * axis[1] +
						d[2] * axis[2];
					const double off[3] =
					{
						d[0] - s * axis[0], d[1] - s * axis[1],
						d[2] - s * axis[2]
					};
					const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
						off[2] * off[2]);
					const bool inside = s <= kS[a] && r <= kR[b];
					const bool stump = node == HD_LARA_TORSO && v.y < shoulder;

					if (stump)
					{
						if (inside)
							cut++;
						else
							left++;
					}

					/* The collar ring: what the cut
					 * leaves standing in the neck band itself. The shoulders
					 * join at s = 30 in every outfit - the tube table above -
					 * so a survivor above that line and near the axis is a
					 * collar and not a chest. */
					if (!inside && node == HD_LARA_TORSO && s >= 0.0 &&
						s < 30.0 && r < 60.0)
						collar++;

					if (inside && node != HD_LARA_TORSO)
					{
						const double dd = sqrt(off[0] * off[0] + off[1] *
							off[1] + off[2] * off[2]);

						if (dd < arm)
							arm = dd;
					}

					if (inside)
						continue;

					for (int e = 0; e < 2; e++)
					{
						const double ex = H[0];
						const double ey = H[1] - 16.0;
						const double ez = H[2] + kForward[e];
						const double dd = sqrt((v.x - ex) * (v.x - ex) +
							(v.y - ey) * (v.y - ey) + (v.z - ez) * (v.z - ez));

						if (dd < nearest[e])
							nearest[e] = dd;
					}
				}

				char armText[16] = "-";

				if (arm < 1.0e8)
					sprintf(armText, "%.1f", arm);

				printf("%-19s %4.0f %4.0f %7d %7d %7d %7s   %8.1f %8.1f\n",
					(a || b) ? "" : stem, kS[a], kR[b], cut, left, collar,
					armText, nearest[0], nearest[1]);
			}

		HDTrmFree(&model);
	}
}

/* One number as a table cell, so that a column which prints "-" when there is
 * nothing to print does not need a scratch buffer at every call site. Four
 * rotating buffers, which is more than any one printf below asks for. */
/* The cylinder cut that shipped before the plane cut, as two plain numbers.
 * The engine cuts with one plane now and hd_lara.h has no
 * HD_LARA_NECK_CUT constants any more, but the tables below were the argument
 * that led to the plane and they are kept exactly as they read: a table that
 * silently changes its own reference line is worse than no table. The plane
 * has its own table at the end of -neck. */
static const double kRoundLCutS = 40.0;
static const double kRoundLCutR = 34.0;

static const char* Num(double v)
{
	static char buf[4][24];
	static int at = 0;

	at = (at + 1) & 3;
	sprintf(buf[at], "%.1f", v);

	return buf[at];
}

/* Where the collar that survived the cylinder cut actually sits.
 *
 * One cylinder leaves 94 collar vertices standing on
 * OUTFIT_RUSSIA and 110 on the
 * parka: one cylinder cannot take a collar of
 * radius 31 and leave shoulders of radius 67 at what the five-unit bins call
 * the same depth. The collar has to go anyway, so
 * this is the measurement that says what shape does it.
 *
 * Two tables. The first is the neck in two-unit bins instead of five, with the
 * survivors of the shipped cylinder counted separately - it is the picture of
 * a collar as a function of depth, and it shows that the ring lives in a band
 * a few units deep and never as wide as a shoulder. The second walks candidate
 * cones - wide at the head joint, tapering to the shipped radius where the
 * shoulders arrive - and asks each of them the four questions that matter:
 * how much collar is left, how much chest it takes (the thing a wider cut
 * ruins), whether it can reach an arm, and what it leaves nearest the eye.
 */
static void PrintNeckCandidates(const std::string& root5,
	const ClassicSlot& body);

static void PrintCollarShape(const std::string& root5, const ClassicSlot& body)
{
	const double kForward[2] = { 48.0, 0.0 };
	const double shoulder =
		0.5 * (body.bind[HD_LARA_RINARM][1] + body.bind[HD_LARA_LINARM][1]);

	printf("\n-- the collar in TWO unit bins -------------------------------\n");
	printf("Neck vertices - torso or head, whichever carries the most of the\n"
		"vertex - by depth s down the neck axis. `all` is how many sit in the\n"
		"bin and `r max` the widest of them; `>R` counts those the shipped\n"
		"cylinder (R = %.0f) leaves standing, `wide` the widest of THOSE and\n"
		"`head` how many of them carry ANY head weight. That last column is\n"
		"the answer: what stands around the neck hole is the head\n"
		"to torso BLEND, which no cut could touch while its node mask named\n"
		"the torso alone.\n\n", kRoundLCutR);

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		printf("%s\n", stem);
		printf("%6s %6s %8s   %6s %8s %8s %6s\n", "s", "all", "r max", ">R",
			"wide", "wide<60");

		for (int step = -3; step <= 20; step++)
		{
			const double at = step * 2.0;
			int all = 0, over = 0, head = 0;
			double hi = -1.0e9, wide = -1.0e9, wideNear = -1.0e9;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_TORSO && node != HD_LARA_HEAD_NODE)
					continue;

				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] +
					d[2] * axis[2];

				if (s < at - 1.0 || s >= at + 1.0)
					continue;

				bool anyHead = false;

				for (int b = 0; b < 3; b++)
					if (v.weight[b] && v.bone[b] == HD_LARA_HEAD_NODE)
						anyHead = true;

				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);

				all++;

				if (r > hi)
					hi = r;

				if (r > kRoundLCutR)
				{
					over++;

					if (anyHead)
						head++;

					if (r > wide)
						wide = r;

					if (r < 60.0 && r > wideNear)
						wideNear = r;
				}
			}

			if (!all)
				continue;

			printf("%6.0f %6d %8.1f   %6d %8s %8s %6d\n", at, all, hi, over,
				over ? Num(wide) : "-", wideNear > -1.0e8 ? Num(wideNear) : "-",
				head);
		}

		printf("\n");
		HDTrmFree(&model);
	}

	/* --- the candidate scan ----------------------------------------------- */
	PrintNeckCandidates(root5, body);
}

/* What a candidate cut really leaves in the camera.
 *
 * A table of this measurement that leaves out two rules of the engine is
 * blind in one eye and lets a collar survive a cut that ought to take it.
 * The two rules:
 *
 *   1. A vertex is cut only if all of its weight sits on nodes the cut may
 *      touch. The shader adds (1 - cuttable) * 1000 to the keep value, and
 *      `cuttable` is the vertex's own weights summed over the masked nodes -
 *      so a vertex half on the torso and half on the head is pushed five
 *      hundred units out of any cut and survives everything. The neck ring is
 *      exactly such a blend.
 *   2. A head-skinned vertex is still drawn in first person. What first person
 *      masks is the node of a range, and a range's node is the heaviest node
 *      over its triangles; the collar's triangles are heaviest on the torso
 *      and carry head-skinned corners along with them. So "the head node is
 *      masked" does not hide them, and a measurement that skipped
 *      every head-majority vertex could not see them.
 *
 * This one models both: it works out each triangle's range node the way
 * hd_model.cpp does, calls a vertex drawn when a triangle that survives the
 * first person mask uses it, and cuts a drawn vertex only when its whole
 * weight is on the candidate's own node mask.
 */
static void PrintNeckCandidates(const std::string& root5, const ClassicSlot& body)
{
	struct Cand
	{
		unsigned int nodes;	// which nodes the cut may touch
		double top, s0, s1;	// the cone: top radius, taper start, taper end
		double near_;		// the zone about the eye, 0 = none
		const char* what;
	};

	static const unsigned int kTorso = 1u << HD_LARA_TORSO;
	static const unsigned int kHead = 1u << HD_LARA_HEAD_NODE;

	static const Cand kCand[] =
	{
		{ kTorso,         34, 0, 0,  0, "the torso alone, as it ships" },
		{ kTorso | kHead, 34, 0, 0,  0, "+ the head node" },
		{ kTorso | kHead, 34, 0, 0, 30, "+ a near zone of 30" },
		{ kTorso | kHead, 34, 0, 0, 45, "+ a near zone of 45" },
		{ kTorso | kHead, 34, 0, 0, 60, "+ a near zone of 60" },
		{ kTorso | kHead, 44, 20, 30, 45, "+ a 44 wide cone" },
		{ kTorso | kHead, 56, 20, 30, 45, "+ a 56 wide cone" }
	};

	const double kForward[2] = { 48.0, 0.0 };
	const double shoulder =
		0.5 * (body.bind[HD_LARA_RINARM][1] + body.bind[HD_LARA_LINARM][1]);

	printf("\n-- what a candidate cut leaves -------------------------------\n");
	printf("Every vertex a first person frame really DRAWS - the triangles\n"
		"whose range node is not the head - and cut only where its whole\n"
		"weight lies on the cut's own nodes, which is what the shader does.\n"
		"`ring` is what stands IN THE CAMERA: survivors within 60 units of\n"
		"the eye on the joint, which is the wall case and the one a player\n"
		"photographed. `chest` is what the cut costs - drawn vertices below\n"
		"the shoulder line (y >= %.0f) that it takes, and it must stay 0. The\n"
		"two eye columns are the nearest survivor from the eye 48 forward and\n"
		"from the eye on the joint. A line under a row names the nearest one\n"
		"at the joint by its bones and weights, which is how a survivor is\n"
		"told from a chest the cut must leave alone.\n\n", shoulder);
	printf("%-19s %-22s %7s %7s %7s   %8s %8s\n", "outfit", "cut", "cut",
		"ring", "chest", "eye 48", "eye 0");
	printf("------------------- ---------------------- ------- ------- "
		"-------   -------- --------\n");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		/* Which vertices a first person frame draws. hd_model.cpp gives a
		 * triangle to the node with the most weight over its three corners
		 * and hides a whole range by that node; so a vertex is drawn when any
		 * triangle that is not the head's uses it. */
		std::vector<unsigned char> drawn((size_t)model.vertices, 0);

		for (int i = 0; i + 2 < model.indices; i += 3)
		{
			double weight[32] = { 0 };
			int best = 0;

			for (int c = 0; c < 3; c++)
			{
				const HDTrmVertex& v = model.vertex[model.index[i + c]];

				for (int b = 0; b < 3; b++)
					weight[v.bone[b] & 31] += (double)v.weight[b];
			}

			for (int n = 1; n < 32; n++)
				if (weight[n] > weight[best])
					best = n;

			if (best == HD_LARA_HEAD_NODE)
				continue;	// the range first person hides

			for (int c = 0; c < 3; c++)
				drawn[model.index[i + c]] = 1;
		}

		for (int c = 0; c < (int)(sizeof(kCand) / sizeof(kCand[0])); c++)
		{
			int cut = 0, ring = 0, chest = 0;
			double arm = 1.0e9;
			double nearest[2] = { 1.0e9, 1.0e9 };
			int who[2] = { -1, -1 };	// which vertex the eye column names

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];

				if (!drawn[i])
					continue;

				/* The shader's own arithmetic, and not a simplification of
				 * it. A vertex goes when
				 *
				 *     keep + (1 - cuttable) * 1000 < 0
				 *
				 * with `cuttable` the vertex's weights summed over the nodes
				 * the cut may touch. It is a WEIGHT and not a test, so a
				 * vertex that is 254/255 on the torso is pushed four units
				 * out of the cut and not a thousand - which matters, because
				 * the .TRM is full of single-unit strays (the vertex under
				 * the chin is bones 7/14/5 at 140/114/1, and the 1 is a
				 * shin). */
				double total = 0.0, on = 0.0;

				for (int b = 0; b < 3; b++)
				{
					total += (double)v.weight[b];

					if (kCand[c].nodes >> (v.bone[b] & 31) & 1u)
						on += (double)v.weight[b];
				}

				const double push = total > 0.0
					? (1.0 - on / total) * 1000.0 : 1000.0;
				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);
				const double span = kCand[c].s1 - kCand[c].s0;
				double t = span > 1.0e-6 ? (s - kCand[c].s0) / span : 1.0;

				if (t < 0.0) t = 0.0;
				if (t > 1.0) t = 1.0;

				const double wide = kCand[c].top +
					(kRoundLCutR - kCand[c].top) * t;
				const double cone =
					s - kRoundLCutS > r - wide
						? s - kRoundLCutS : r - wide;

				/* The near zone follows the camera, so it is worked out once
				 * per eye - which is the whole point of it living in view
				 * space in the shader. Everything else about the cut is the
				 * same at both. */
				double eye[2];
				bool gone[2];

				for (int e = 0; e < 2; e++)
				{
					const double ex = H[0];
					const double ey = H[1] - 16.0;
					const double ez = H[2] + kForward[e];

					eye[e] = sqrt((v.x - ex) * (v.x - ex) +
						(v.y - ey) * (v.y - ey) + (v.z - ez) * (v.z - ez));

					const double near_ = eye[e] - kCand[c].near_;
					const double keep = kCand[c].near_ > 0.0 && near_ < cone
						? near_ : cone;

					gone[e] = keep + push < 0.0;
				}

				if (gone[1])
				{
					cut++;

					if (v.y >= shoulder)
						chest++;

					int node = v.bone[0];

					if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
						node = v.bone[1];
					else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
						node = v.bone[2];

					if (node != HD_LARA_TORSO && node != HD_LARA_HEAD_NODE &&
						r < arm)
						arm = r;
				}
				else if (eye[1] < 60.0)
					ring++;		// standing in the camera at the wall eye

				for (int e = 0; e < 2; e++)
					if (!gone[e] && eye[e] < nearest[e])
					{
						nearest[e] = eye[e];
						who[e] = i;
					}
			}

			printf("%-19s %-22s %7d %7d %7d   %8.1f %8.1f\n", c ? "" : stem,
				kCand[c].what, cut, ring, chest, nearest[0], nearest[1]);

			/* What the nearest survivor is, and it is the whole reason this
			 * table exists: a number alone cannot say whether the thing at the
			 * eye is a neck the cut should have taken or a chest it must
			 * leave. Its bones and weights say which, and its depth and radius
			 * say whether the cut even reached it. */
			if (who[1] >= 0)
			{
				const HDTrmVertex& v = model.vertex[who[1]];
				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double sd = d[0] * axis[0] + d[1] * axis[1] +
					d[2] * axis[2];
				const double off[3] =
				{
					d[0] - sd * axis[0], d[1] - sd * axis[1],
					d[2] - sd * axis[2]
				};

				printf("%-19s %-22s   nearest at the joint: bones %d/%d/%d "
					"weights %d/%d/%d, s %.1f r %.1f\n", "", "",
					v.bone[0], v.bone[1], v.bone[2], v.weight[0], v.weight[1],
					v.weight[2], sd, sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]));
			}

			if (arm < 1.0e8)
				printf("%-19s %-22s an ARM vertex is inside it, %.1f off the "
					"axis\n", "", "", arm);
		}

		HDTrmFree(&model);
	}
}

/* Which page is the neck.
 *
 * The inner skin wears one texel and it has to be Lara's throat, so the maker
 * in hd_lara.cpp has to pick a page out of the stump - and the first rule it
 * was given (the topmost ring) answered 8002, which is her HAIR. This is the
 * table that says so: the node 7 vertices inside the cut's own radius,
 * counted by texture page and by how far down the neck axis they sit.
 */

static void PrintNeckPages(const std::string& root5, const ClassicSlot& body)
{
	printf("\n-- the neck stump BY PAGE, per outfit -------------------------\n");
	printf("node 7 vertices within %.0f of the neck axis, by the page they\n"
		"wear. The inner skin's colour is taken from one of these, so the\n"
		"page that is the THROAT has to be the one the rule picks.\n\n",
		(double)HD_LARA_NECK_SKIN_R);

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		struct Row { int page; int count; double lo, hi, sum;
			double rlo, rhi, rsum; };
		std::vector<Row> rows;

		for (int i = 0; i < model.vertices; i++)
		{
			const HDTrmVertex& v = model.vertex[i];
			int node = v.bone[0];

			if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
				node = v.bone[1];
			else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
				node = v.bone[2];

			if (node != HD_LARA_TORSO)
				continue;

			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
			const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

			if (s > kRoundLCutS)
				continue;

			const double off[3] =
			{
				d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
			};
			const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
				off[2] * off[2]);

			if (r > HD_LARA_NECK_SKIN_R)
				continue;

			const int page = HDTrmPageOf(&model, &v);
			size_t at = 0;

			for (; at < rows.size(); at++)
				if (rows[at].page == page)
					break;

			if (at == rows.size())
			{
				Row one = { page, 0, 1.0e9, -1.0e9, 0.0, 1.0e9, -1.0e9, 0.0 };

				rows.push_back(one);
			}

			rows[at].count++;
			rows[at].sum += s;
			rows[at].rsum += r;

			if (s < rows[at].lo) rows[at].lo = s;
			if (s > rows[at].hi) rows[at].hi = s;
			if (r < rows[at].rlo) rows[at].rlo = r;
			if (r > rows[at].rhi) rows[at].rhi = r;
		}

		printf("%-19s %6s %7s %7s %7s %7s   %7s %7s %7s\n", stem, "page",
			"verts", "s min", "s mean", "s max", "r min", "r mean", "r max");

		for (size_t i = 0; i < rows.size(); i++)
			printf("%-19s %6d %7d %7.1f %7.1f %7.1f   %7.1f %7.1f %7.1f\n", "",
				rows[i].page, rows[i].count, rows[i].lo,
				rows[i].sum / rows[i].count, rows[i].hi, rows[i].rlo,
				rows[i].rsum / rows[i].count, rows[i].rhi);

		HDTrmFree(&model);
	}
}

/* Why a hand does not shine like the arm it is on.
 *
 * The hands react differently to a room's intensity: in some rooms the hand
 * is brighter than the forearm, in others darker. A
 * difference in the texture would be the same everywhere, so it is in the light.
 *
 * What is the same, read off hd_laradraw.cpp and not guessed: the body and the
 * hand parts are pushed with the same nodeLight array, the same light list,
 * the same sun factor and the same joint matrices. HDObjectPush copies all
 * four per draw and changes none of them. So the ambient and the diffuse at a
 * point on the wrist are identical to the bit whichever model owns it.
 *
 * What differs is the sheen, and it is the whole of it. The body carries the outfit's material and the hand
 * carries the skin's, and
 * the forearm the hand meets is the body. The Blinn-Phong term the vertex
 * shader adds is, where the room's own light list is empty - an ordinary room
 * lit by its baked vertex colours, which is most of the game:
 *
 *     spec = light * strength * pow(N.V, power) / 255
 *
 * `light` is the room's ambient. So the difference between the two materials
 * is multiplied by the room's intensity - bright in a bright room, gone in a
 * dark one, and its sign follows the outfit.
 *
 * This table is the measurement: which texture pages the body wears on the
 * arm and hand nodes, which pages the HAND_ model wears, and what each of
 * them shines like.
 */
static void PrintHandLight(const std::string& root5, int outfit, bool oldSkin)
{
	static const int kArmNodes[6] =
	{
		HD_LARA_RINARM, HD_LARA_ROUTARM, HD_LARA_RHAND,
		HD_LARA_LINARM, HD_LARA_LOUTARM, HD_LARA_LHAND
	};

	printf("\n-- the wrist seam: what shines how ---------------------------\n");
	printf("`body` is the OUTFIT model's own pages on nodes 8..13 - the arms\n"
		"and the sleeve or skin at the hand - and `hand` the pages of the\n"
		"HAND_ model that is drawn over them. `sheen` is the material each\n"
		"gets, and `white` the grey levels the highlight adds at N.V = 1 in a\n"
		"room whose ambient is 255, which is what the seam shows.\n\n");
	printf("%-19s %6s %7s %7s %7s %7s   %11s %7s\n", "outfit", "page",
		"arm", "at hand", "torso", "where", "sheen", "white");
	printf("------------------- ------ ------- ------- ------- -------   "
		"----------- -------\n");

	{
		const char* stem = HDLaraOutfitModel(outfit);
		const char* handStem = HDLaraPartModel(outfit, HD_LARA_PART_HAND_REST);
		bool named = false;

		if (!stem)
			return;

		for (int which = 0; which < 2; which++)
		{
			const char* file = which ? handStem : stem;
			HDTrmModel model;
			const char* why = "";

			if (!file)
				continue;

			const std::string path = TrmPath(root5, file);

			if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
				continue;

			struct Row { int page; int count; int hands; int torso; };
			std::vector<Row> rows;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];
				bool arm = false;

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				for (int k = 0; k < 6; k++)
					if (node == kArmNodes[k])
						arm = true;

				if (!arm && node != HD_LARA_TORSO)
					continue;

				const int page = HDTrmPageOf(&model, &v);
				size_t at = 0;

				for (; at < rows.size(); at++)
					if (rows[at].page == page)
						break;

				if (at == rows.size())
				{
					Row one = { page, 0, 0, 0 };

					rows.push_back(one);
				}

				if (arm)
					rows[at].count++;

				if (node == HD_LARA_RHAND || node == HD_LARA_LHAND)
					rows[at].hands++;

				if (node == HD_LARA_TORSO)
					rows[at].torso++;
			}

			/* The sheen the engine's own tables give it: the outfit's for the
			 * body, the hand's for the hand - and the SKIN's
			 * for whichever page of the body is Lara's own skin. */
			float bodyS = 0.0f, bodyP = 1.0f, handS = 0.0f, handP = 1.0f;
			float skinS = 0.0f, skinP = 1.0f;

			HDLaraPartSheen(HD_LARA_PART_BODY, &bodyS, &bodyP);
			HDLaraPartSheen(HD_LARA_PART_HAND_REST, &handS, &handP);
			HDLaraSkinSheen(&skinS, &skinP);

			for (size_t i = 0; i < rows.size(); i++)
			{
				const bool skin = !which && !oldSkin &&
					HDLaraSkinPage(rows[i].page);
				const float s = which ? handS : (skin ? skinS : bodyS);
				const float p = which ? handP : (skin ? skinP : bodyP);

				if (!rows[i].count)
					continue;

				printf("%-19s %6d %7d %7d %7d %7s   %5.2f / %-3.0f %7.1f\n",
					named ? "" : stem, rows[i].page, rows[i].count,
					rows[i].hands, rows[i].torso,
					which ? "hand" : (skin ? "SKIN" : "body"), s, p,
					s * 255.0f);
				named = true;
			}

			HDTrmFree(&model);
		}
	}

	/* The seam itself, as one number. The body's page with the most geometry
	 * on the hand nodes is the surface the HAND model continues, so the two
	 * whites beside each other are what a pixel row across the wrist sees. */
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";
		const std::string path = stem ? TrmPath(root5, stem) : std::string();

		if (!path.empty() && HDTrmLoad(path.c_str(), &model, &why))
		{
			std::vector<int> page, count;

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_RHAND && node != HD_LARA_LHAND)
					continue;

				const int at = HDTrmPageOf(&model, &v);
				size_t k = 0;

				for (; k < page.size(); k++)
					if (page[k] == at)
						break;

				if (k == page.size())
				{
					page.push_back(at);
					count.push_back(0);
				}

				count[k]++;
			}

			int best = -1, most = 0;

			for (size_t k = 0; k < page.size(); k++)
				if (count[k] > most)
				{
					most = count[k];
					best = page[k];
				}

			if (best >= 0)
			{
				float bodyS = 0.0f, bodyP = 1.0f, handS = 0.0f, handP = 1.0f;
				float skinS = 0.0f, skinP = 1.0f;

				HDLaraPartSheen(HD_LARA_PART_BODY, &bodyS, &bodyP);
				HDLaraPartSheen(HD_LARA_PART_HAND_REST, &handS, &handP);
				HDLaraSkinSheen(&skinS, &skinP);

				if (!oldSkin && HDLaraSkinPage(best))
					bodyS = skinS;

				printf("\nTHE SEAM: the body meets the hand on page %d (%d "
					"vertices at the two hand\nnodes). It adds %.1f grey "
					"levels there and the hand adds %.1f - a difference of "
					"%.1f,\nmultiplied by the room's ambient.\n", best, most,
					bodyS * 255.0f, handS * 255.0f,
					(handS - bodyS) * 255.0f);
			}

			HDTrmFree(&model);
		}
	}

	printf("\nA page marked SKIN is Lara's own skin on the BODY model, and it\n"
		"takes the skin material - the same one the HAND model\n"
		"has. Where the two used to differ, the difference was multiplied\n"
		"by the room's ambient, which is the wrist seam the table measures.\n");
}

/* ------------------------------------------ the head's own weight ------- */
/* What the first person cut really wants to throw away.
 *
 * Every shape cut before this one - a sphere about the eye, a cylinder about the neck axis, a plane carried by the
 * head node - is a shape in space, and each was found wanting for the same reason: a skinned vertex moves, so a rule written
 * in space takes a different patch of her skin in every pose. A plane fixed to the head
 * was the worst of them, because it tilts with the head
 * and then slices through her chest and her shoulders in some animations,
 * partly rendering Lara away over her whole body (the HDLOG line reads
 * drop= 4, 5 and 9 where 2 is healthy).
 *
 * The one thing about a vertex that does not move is its bone weights. They
 * are written in the file, they are the same in every animation, at every
 * camera angle and in every pose. So this table measures the wardrobe by
 * weight rather than by position:
 *
 *   head>0    vertices carrying ANY weight on node 14
 *   the bins  how that weight is distributed - a neck vertex is part head and
 *             part torso, and the bins say whether there is a gap to put a
 *             threshold in
 *   the RIM   the vertices with NO head weight that share a triangle with one
 *             that has some. That ring is the boundary the discard leaves
 *             behind, it is skinned to the TORSO alone, and it therefore sits
 *             still in the torso's frame in every pose - which is what lets a
 *             lid rigid on the torso close it without a gap for good.
 *   the sectors  the rim seen as 48 wedges about the neck axis: how many are
 *             empty (a fan needs every one filled), and how far the radius and
 *             the depth wander over them, which says whether a flat disc is
 *             honest or a fan has to follow the rim's own shape.
 *
 * s is the depth down the neck axis from the head joint, growing towards the
 * torso, exactly as the -neck table's bins are; r is the distance off that
 * axis. The last column is the widest r of anything discarded, which is the
 * question "does the threshold eat the COLLAR" - the VCI catsuit's collar ring
 * stands at r = 70. */
static void PrintHeadWeight(const std::string& root5, const ClassicSlot& body)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\n-headw needs the level's Lara skeleton and this one has "
			"none.\n");
		return;
	}

	const double H[3] =
	{
		body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
		body.bind[HD_LARA_HEAD_NODE][2]
	};
	const double T[3] =
	{
		body.bind[HD_LARA_TORSO][0], body.bind[HD_LARA_TORSO][1],
		body.bind[HD_LARA_TORSO][2]
	};

	double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
	double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);

	if (len < 1.0)
	{
		printf("\n-headw: the head and the torso joint sit on top of each "
			"other.\n");
		return;
	}

	for (int i = 0; i < 3; i++)
		axis[i] /= len;

	/* The same two cross axes build_neck_cap uses, so that the sector numbers
	 * here and the fan there are the same wedges. */
	double u[3] = { axis[1], -axis[0], 0.0 };
	const double uLen = sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);

	for (int i = 0; i < 3 && uLen > 1.0e-6; i++)
		u[i] /= uLen;

	const double w[3] =
	{
		axis[1] * u[2] - axis[2] * u[1],
		axis[2] * u[0] - axis[0] * u[2],
		axis[0] * u[1] - axis[1] * u[0]
	};

	const int kSectors = HD_LARA_NECK_CAP_SEGMENTS;

	/* The thresholds the table is scanned at. `any` is one part in 255 - a
	 * vertex the file gives the head ANY share of at all - and 1.00 is a
	 * vertex the head owns outright. What lies between them is the neck, and
	 * the point of the scan is to find out whether there is a threshold that
	 * takes the whole tube and leaves the collar. */
	const int kCuts = 8;
	const double kCut[kCuts] =
		{ 1.0 / 255.0, 0.05, 0.10, 0.15, 0.20, 0.25, 0.35, 0.50 };
	const char* const kCutName[kCuts] =
		{ "any", "0.05", "0.10", "0.15", "0.20", "0.25", "0.35", "0.50" };

	printf("\n-- the head's own weight, per outfit -------------------------\n");
	printf("head joint %.0f/%.0f/%.0f, the axis runs to the torso joint "
		"%.0f/%.0f/%.0f\ns grows down that axis from the head joint, r is "
		"the distance off it\n\n"
		"cut     the threshold: a vertex whose head weight REACHES it is "
		"discarded\ndrop    how many of them there are\ns max   the deepest of "
		"them - how far down her body the cut goes\nr>s8    the widest of "
		"them below s = 8, which is the COLLAR question\npage    the .DDS "
		"page most of the rim wears; rimW its worst head weight\n"
		"rim     the vertices with NO head weight in a triangle with one that "
		"has some\nsectors how many of the %d wedges about the neck axis the "
		"rim fills\n\n", H[0], H[1], H[2], T[0], T[1], T[2], kSectors);

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
		{
			printf("%-19s not read - %s\n", stem,
				path.empty() ? "no .TRM in 5 or 4" : why);
			continue;
		}

		std::vector<unsigned char> hw((size_t)model.vertices, 0);
		std::vector<float> vs((size_t)model.vertices, 0.0f);
		std::vector<float> vr((size_t)model.vertices, 0.0f);
		std::vector<float> va((size_t)model.vertices, 0.0f);
		int any = 0;

		for (int i = 0; i < model.vertices; i++)
		{
			const HDTrmVertex& v = model.vertex[i];
			int sum = 0;

			for (int k = 0; k < 3; k++)
				if (v.bone[k] == HD_LARA_HEAD_NODE)
					sum += v.weight[k];

			hw[(size_t)i] = (unsigned char)(sum > 255 ? 255 : sum);

			if (sum)
				any++;

			const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
			const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];
			const double off[3] =
			{
				d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
			};
			const double x = off[0] * u[0] + off[1] * u[1] + off[2] * u[2];
			const double y = off[0] * w[0] + off[1] * w[1] + off[2] * w[2];
			double a = atan2(y, x);

			if (a < 0.0)
				a += 6.283185307179586;

			vs[(size_t)i] = (float)s;
			vr[(size_t)i] = (float)sqrt(x * x + y * y);
			va[(size_t)i] = (float)a;
		}

		printf("%s: %d vertices, %d carry head weight\n", stem,
			model.vertices, any);
		printf("  %-4s %7s %7s %7s   %5s %13s %13s %5s %5s %7s\n", "cut",
			"drop", "s max", "r>s8", "rim", "rim s", "rim r", "page",
			"rimW", "sectors");
		printf("  ---- ------- ------- -------   ----- ------------- "
			"------------- ----- ----- -------\n");

		for (int c = 0; c < kCuts; c++)
		{
			const int bar = (int)(kCut[c] * 255.0 + 0.5);
			int drop = 0;
			double sMax = 0.0, rMax = 0.0;

			for (int i = 0; i < model.vertices; i++)
				if ((int)hw[(size_t)i] >= bar)
				{
					drop++;

					if (vs[(size_t)i] > sMax) sMax = vs[(size_t)i];

					/* The widest discarded vertex BELOW THE CHIN, which is
					 * the collar question. Above s = 8 it is her jaw and her
					 * skull, and both are meant to go. */
					if (vs[(size_t)i] >= 8.0f && vr[(size_t)i] > rMax)
						rMax = vr[(size_t)i];
				}

			std::vector<unsigned char> rim((size_t)model.vertices, 0);

			for (int t = 0; t + 2 < model.indices; t += 3)
			{
				const int tri[3] =
					{ model.index[t + 0], model.index[t + 1], model.index[t + 2] };
				bool cut = false;

				for (int k = 0; k < 3; k++)
					if (tri[k] < model.vertices && (int)hw[(size_t)tri[k]] >= bar)
						cut = true;

				if (!cut)
					continue;

				for (int k = 0; k < 3; k++)
					if (tri[k] < model.vertices && (int)hw[(size_t)tri[k]] < bar)
						rim[(size_t)tri[k]] = 1;
			}

			int rimCount = 0, filled = 0, rimW = 0;
			double sLo = 1.0e9, sHi = -1.0e9, rLo = 1.0e9, rHi = -1.0e9;
			std::vector<unsigned char> sector((size_t)kSectors, 0);
			std::vector<int> rimPage, rimPageCount;

			for (int i = 0; i < model.vertices; i++)
			{
				if (!rim[(size_t)i])
					continue;

				rimCount++;

				if ((int)hw[(size_t)i] > rimW)
					rimW = hw[(size_t)i];

				const int page = HDTrmPageOf(&model, &model.vertex[i]);
				size_t at = 0;

				while (at < rimPage.size() && rimPage[at] != page)
					at++;

				if (at == rimPage.size())
				{
					rimPage.push_back(page);
					rimPageCount.push_back(0);
				}

				rimPageCount[at]++;

				if (vs[(size_t)i] < sLo) sLo = vs[(size_t)i];
				if (vs[(size_t)i] > sHi) sHi = vs[(size_t)i];
				if (vr[(size_t)i] < rLo) rLo = vr[(size_t)i];
				if (vr[(size_t)i] > rHi) rHi = vr[(size_t)i];

				int k = (int)((double)va[(size_t)i] * (double)kSectors /
					6.283185307179586);

				if (k < 0) k = 0;
				if (k >= kSectors) k = kSectors - 1;

				sector[(size_t)k] = 1;
			}

			for (int k = 0; k < kSectors; k++)
				filled += sector[(size_t)k];

			int bestPage = -1, bestCount = 0;

			for (size_t k = 0; k < rimPage.size(); k++)
				if (rimPageCount[k] > bestCount)
				{
					bestCount = rimPageCount[k];
					bestPage = rimPage[k];
				}

			printf("  %-4s %7d %7.1f %7.1f   %5d %6.1f..%-6.1f %6.1f..%-6.1f "
				"%5d %5.2f %3d/%d\n", kCutName[c], drop, sMax, rMax, rimCount,
				rimCount ? sLo : 0.0, rimCount ? sHi : 0.0,
				rimCount ? rLo : 0.0, rimCount ? rHi : 0.0, bestPage,
				(double)rimW / 255.0, filled, kSectors);

			if (rimPage.size() > 1)
			{
				printf("       the rim spans %d pages:", (int)rimPage.size());

				for (size_t k = 0; k < rimPage.size(); k++)
					printf(" %d(%d)", rimPage[k], rimPageCount[k]);

				printf("\n");
			}

			/* The rim's texels at the shipping threshold. The lid wears the
			 * rim's own texture, and its middle has
			 * to be given a texel of its own because it belongs to no rim
			 * vertex. Whatever rule picks that texel is the one thing on the
			 * lid that is not copied from the body, so it is the one thing
			 * that can be wrong - and it is
			 * wrong on the four TR3 guest bodies: a dark point in the middle
			 * of the cap, running dark towards it like a cone, while the TR5
			 * outfits are clean. This prints what the rule is working with. */
			if (fabs(kCut[c] - (double)HD_LARA_HEAD_CUT_WEIGHT) < 1.0e-6)
			{
				int uLo = 255, uHi = 0, vLo = 255, vHi = 0;
				int sumU = 0, sumV = 0, on = 0;

				printf("       the rim's texels on page %d:", bestPage);

				for (int i = 0; i < model.vertices; i++)
				{
					if (!rim[(size_t)i] ||
						HDTrmPageOf(&model, &model.vertex[i]) != bestPage)
						continue;

					const int su = model.vertex[i].u;
					const int sv = model.vertex[i].v;

					printf(" %d/%d", su, sv);

					if (su < uLo) uLo = su;
					if (su > uHi) uHi = su;
					if (sv < vLo) vLo = sv;
					if (sv > vHi) vHi = sv;

					sumU += su;
					sumV += sv;
					on++;
				}

				printf("\n       u %d..%d, v %d..%d, the average is %d/%d - "
					"the texel the middle WORE when it was one averaged vertex: on the "
					"TR5 pages it lands inside the neck's own column, on the "
					"TR3 page 8201 in the gap between the two bands, which was "
					"the dark point. Now the middle is `spokes` copies, "
					"each wearing its own rim vertex's texel, and no texel is "
					"averaged at all\n", uLo, uHi, vLo, vHi,
					on ? sumU / on : 0, on ? sumV / on : 0);
			}
		}

		/* What survives nearest the head, which is the other half of the
		 * question: a threshold that leaves a ring of neck standing above the
		 * lid leaves a visible tube of neck poking through it. The nearest surviving
		 * vertex INSIDE the neck's own radius says where the skin really
		 * stops. */
		for (int c = 0; c < kCuts; c++)
		{
			const int bar = (int)(kCut[c] * 255.0 + 0.5);
			double topNeck = 1.0e9;
			int narrow = 0;

			for (int i = 0; i < model.vertices; i++)
			{
				if ((int)hw[(size_t)i] >= bar ||
					vr[(size_t)i] > (float)kRoundLCutR)
					continue;

				narrow++;

				if (vs[(size_t)i] < topNeck)
					topNeck = vs[(size_t)i];
			}

			printf("  %-4s the highest surviving vertex within r %.0f stands "
				"at s %.1f (%d of them)\n", kCutName[c], kRoundLCutR,
				narrow ? topNeck : 0.0, narrow);
		}

		printf("\n");
		HDTrmFree(&model);
	}

	printf("A RIM vertex carries NO head weight at all, so it is skinned to "
		"the torso\nand its neighbours alone: it stands still in the torso's "
		"frame in every pose,\nwhich is why the lid hangs on the TORSO node "
		"and the cut goes by weight.\n");
}

static void PrintNeckTable(const std::string& root5, const ClassicSlot& body)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\n-neck needs the level's Lara skeleton and this one has none.\n");
		return;
	}

	/* The two eyes. game/camera.cpp asks mgLOS whether it may put the eye
	 * FP_EYE_OFFSET = 48 units in front of the head joint, and takes 0 when
	 * something is in the way - a wall, a ledge, a doorway. Both are ordinary
	 * play, and the cut has to hold at both, so both are measured. The
	 * shoulder line is the two inner-arm joints of the level's own mesh tree,
	 * which is where a body stops being neck. */
	const double kForward[2] = { 48.0, 0.0 };
	const double shoulder =
		0.5 * (body.bind[HD_LARA_RINARM][1] + body.bind[HD_LARA_LINARM][1]);

	printf("\n-- the neck stump, per outfit --------------------------------\n");
	printf("head joint %.0f/%.0f/%.0f, eye 16 above it and 48 or 0 in "
		"front\nshoulder line y %.0f (nodes %d and %d)\n\n",
		body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
		body.bind[HD_LARA_HEAD_NODE][2], shoulder, HD_LARA_RINARM,
		HD_LARA_LINARM);
	printf("%-19s %7s   %-24s   %-24s\n", "outfit", "stump",
		"eye 48 forward", "eye at the joint");
	printf("%-19s %7s   %7s %7s %7s   %7s %7s %7s\n", "", "verts",
		"stump", "shldr", "chest", "stump", "shldr", "chest");
	printf("------------------- -------   ------- ------- -------   "
		"------- ------- -------\n");

	double keepMin = 1.0e9, stumpMax = 0.0;

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
		{
			printf("%-19s not read - %s\n", stem,
				path.empty() ? "no .TRM in 5 or 4" : why);
			continue;
		}

		int stumpVerts = 0;
		double nearest[2][3];  // near is a windows.h macro

		for (int e = 0; e < 2; e++)
			nearest[e][0] = nearest[e][1] = nearest[e][2] = 1.0e9;

		for (int i = 0; i < model.vertices; i++)
		{
			const HDTrmVertex& v = model.vertex[i];

			/* The node a vertex BELONGS to is the one with the biggest of its
			 * three weights - the same rule the range builder uses, and the
			 * one that decides which node's bit hides it. */
			int node = v.bone[0];

			if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
				node = v.bone[1];
			else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
				node = v.bone[2];

			int kind = -1;

			if (node == HD_LARA_RINARM || node == HD_LARA_LINARM)
				kind = 1;
			else if (node == HD_LARA_TORSO)
				kind = v.y < shoulder ? 0 : 2;

			if (kind < 0)
				continue;

			if (!kind)
				stumpVerts++;

			for (int e = 0; e < 2; e++)
			{
				const double dx = v.x - body.bind[HD_LARA_HEAD_NODE][0];
				const double dy = v.y - (body.bind[HD_LARA_HEAD_NODE][1] - 16.0);
				const double dz = v.z -
					(body.bind[HD_LARA_HEAD_NODE][2] + kForward[e]);
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				if (d < nearest[e][kind])
					nearest[e][kind] = d;
			}
		}

		printf("%-19s %7d   %7.1f %7.1f %7.1f   %7.1f %7.1f %7.1f\n", stem,
			stumpVerts, nearest[0][0], nearest[0][1], nearest[0][2],
			nearest[1][0], nearest[1][1], nearest[1][2]);

		for (int e = 0; e < 2; e++)
		{
			if (nearest[e][0] > stumpMax && nearest[e][0] < 1.0e8)
				stumpMax = nearest[e][0];

			for (int k = 1; k < 3; k++)
				if (nearest[e][k] < keepMin)
					keepMin = nearest[e][k];
		}

		HDTrmFree(&model);
	}

	printf("\nthe nearest stump vertex of any outfit at either eye is %.1f "
		"units away;\nthe nearest shoulder or chest vertex - the geometry that "
		"must SURVIVE -\nis %.1f. A radius between the two separates them from "
		"every angle.\n", stumpMax, keepMin);
	printf("(a cut at a fixed DEPTH cannot do this: measured at 72 units of "
		"depth it\ntook the collarbone with the stump and opened a window "
		"through her chest.)\n");
	printf("THIS TABLE IS HISTORY: the cut is no longer a "
		"sphere\naround the eye at all - a sphere large enough to take the "
		"stump also takes\nsixty units of her chest with it. It is a CYLINDER "
		"about the neck now, and\nthe two tables below are the ones it rests "
		"on.\n");

	/* --- What the cut leaves open ------------------------------------------
	 *
	 * The cut leaves a hole at the collar and one looks
	 * into the body - the inside of her chest and the backpack straps behind
	 * it. So the neck has to be closed below the cut, and the first question
	 * is what shape the thing being closed is.
	 *
	 * The neck AXIS is the line from the torso joint to the head joint; `s` is
	 * how far down it a vertex sits (0 at the head joint, negative towards the
	 * torso) and `r` how far it stands off the axis. A tube reads as a narrow
	 * band of r at every s; where the band bursts open is where the neck stops
	 * being a neck and becomes shoulders.
	 *
	 * The cap this measurement is for is a DOME sunk into that tube, so what it
	 * needs is the tube's radius at the depth the dome sits at, and the
	 * distance of that depth from both eyes - which has to clear the 60 unit
	 * cut, or the dome would be cut away with the neck it is replacing. */
	printf("\n-- the neck as a tube, per outfit ----------------------------\n");
	printf("s is the depth down the torso-joint..head-joint axis from the head "
		"joint,\nr the distance off that axis. Only node 7 (the torso) counts, "
		"which is\nwhat the cut leaves standing.\n\n");
	printf("%-19s %5s %6s %8s %8s %8s %8s %8s\n", "outfit", "s", "verts",
		"r min", "r mean", "r max", "eye 48", "eye 0");
	printf("------------------- ----- ------ -------- -------- -------- "
		"-------- --------\n");

	for (int outfit = HD_LARA_OUTFIT_NONE + 1; outfit < HD_LARA_OUTFIT_COUNT;
		outfit++)
	{
		const char* stem = HDLaraOutfitModel(outfit);
		HDTrmModel model;
		const char* why = "";

		if (!stem || outfit == HD_LARA_OUTFIT_SUBSUIT)
			continue;

		const std::string path = TrmPath(root5, stem);

		if (path.empty() || !HDTrmLoad(path.c_str(), &model, &why))
			continue;

		const double H[3] =
		{
			body.bind[HD_LARA_HEAD_NODE][0], body.bind[HD_LARA_HEAD_NODE][1],
			body.bind[HD_LARA_HEAD_NODE][2]
		};
		const double T[3] = { body.bind[7][0], body.bind[7][1], body.bind[7][2] };
		double axis[3] = { T[0] - H[0], T[1] - H[1], T[2] - H[2] };
		const double len = sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
			axis[2] * axis[2]);

		for (int i = 0; i < 3; i++)
			axis[i] /= len > 1.0e-6 ? len : 1.0;

		for (int step = 0; step <= 10; step++)
		{
			const double at = step * 10.0;	// down the axis from the head joint
			double lo = 1.0e9, hi = -1.0e9, sum = 0.0;
			int count = 0;
			double eye[2] = { 1.0e9, 1.0e9 };

			for (int i = 0; i < model.vertices; i++)
			{
				const HDTrmVertex& v = model.vertex[i];
				int node = v.bone[0];

				if (v.weight[1] > v.weight[0] && v.weight[1] >= v.weight[2])
					node = v.bone[1];
				else if (v.weight[2] > v.weight[0] && v.weight[2] > v.weight[1])
					node = v.bone[2];

				if (node != HD_LARA_TORSO)
					continue;

				const double d[3] = { v.x - H[0], v.y - H[1], v.z - H[2] };
				const double s = d[0] * axis[0] + d[1] * axis[1] + d[2] * axis[2];

				if (s < at - 5.0 || s > at + 5.0)
					continue;

				const double off[3] =
				{
					d[0] - s * axis[0], d[1] - s * axis[1], d[2] - s * axis[2]
				};
				const double r = sqrt(off[0] * off[0] + off[1] * off[1] +
					off[2] * off[2]);

				count++;
				sum += r;

				if (r < lo) lo = r;
				if (r > hi) hi = r;

				for (int e = 0; e < 2; e++)
				{
					const double ex = H[0];
					const double ey = H[1] - 16.0;
					const double ez = H[2] + kForward[e];
					const double dd = sqrt((v.x - ex) * (v.x - ex) +
						(v.y - ey) * (v.y - ey) + (v.z - ez) * (v.z - ez));

					if (dd < eye[e])
						eye[e] = dd;
				}
			}

			if (!count)
				continue;

			printf("%-19s %5.0f %6d %8.1f %8.1f %8.1f %8.1f %8.1f\n",
				step ? "" : stem, at, count, lo, sum / count, hi, eye[0],
				eye[1]);
		}

		HDTrmFree(&model);
	}

	printf("\nthe dome that closes the neck sits at the depth where the tube "
		"stops being one:\nthe row above the one that bursts open.\n");

	PrintNeckCylinder(root5, body);
	PrintCollarShape(root5, body);
	PrintNeckPages(root5, body);
}

/* ------------------------------------------------- the braid's physics -- */
/*
 * The braid, run offline.
 *
 * The HD braid glitches about restlessly: it can be long and thrown out to
 * the side in one frame and short and
 * tucked against her back in the next. Two changes went into
 * game\hair.cpp - the root moved 15 units forward for an HD Lara, and the HD
 * backpack got two collision spheres - and the second of those is
 * the one change no offline picture can prove. A picture cannot, but the
 * simulation can be run on its own, and that is what this does.
 *
 * What is transcribed, and what is not. The loop below is HairControl's, line
 * for line: integrate three quarters of the last velocity, add the ten units
 * of gravity, push the link out of every sphere, point the link above it at
 * where it landed, and put it back at exactly one bone length along that
 * direction, the new velocity being how far it actually moved. Positions are
 * longs, as they are in the game, and the push-out keeps the game's integer
 * division, because that quantisation is the only thing that can leave a
 * chain trembling when everything else has come to rest.
 *
 * Three of the game's terms are left out and none of them can hide a
 * tremble: the WIND (an outdoor room only, and it is a translation of the
 * whole chain, not a force between links), the WATER LINE (an upward clamp)
 * and the FLOOR (a downward clamp). A braid that will not settle standing
 * still in a dry indoor room will not settle anywhere.
 *
 * The pose is the bind pose - Lara standing, every joint at rest - the same
 * pose she stands still in at a door
 * while the braid moves anyway.
 *
 * What comes out is one line per regime: how far the furthest link still
 * travels per step after 10, 30 and `steps` steps. A settled chain goes to
 * zero and stays there. The last column is the deepest a link still sits
 * inside a pack sphere, which is what the pack was added for.
 */
struct BraidSphere
{
	long x, y, z, r;
};

struct BraidRegime
{
	const char* label;
	/* What the two halves of HairControl do. The KEY frame is the half that
	 * runs when the animation is not interpolating - the `else` half, the one
	 * that moved the root - and the TWEEN frame the `if (frac)` half it left
	 * alone. `period` is how often a key frame comes round: 1 is "every
	 * frame is a key frame", 4 is one key frame in four, which is what an
	 * ordinary Lara animation with an interpolation rate of 4 does. */
	long keyRootZ, tweenRootZ;
	int keySpheres, tweenSpheres;
	int period;
	// Does this regime place the pack as a box?
	bool keyBox, tweenBox;
};

/* phd_sqrt is an integer square root; long arithmetic all the way is what
 * the game does, so the tool does it too. */
static long BraidSqrt(long long v)
{
	if (v <= 0)
		return 0;

	long r = (long)sqrt((double)v);

	while ((long long)r * r > v)
		r--;

	while ((long long)(r + 1) * (r + 1) <= v)
		r++;

	return r;
}

/* ---------------------------------------------- the pack, as a box -------
 *
 * The tool's twin of game\hair.cpp's HdPackFrame / HdPackPush
 * - the same six faces, the same per-link margin and the same
 * preference for the rear one, in doubles instead of the engine's floats
 * because the tool has no fixed-point matrix stack to match.
 *
 * `on` false is a variant without the box at all, which is what the old
 * regimes are measured as.
 */
struct BraidBox
{
	bool on;
	double rot[9];		// the torso frame's axes, rot[3 * row + col]
	double org[3];		// the torso joint, in the world
	long zBack;		// the rear face, so a variant can move it
	int bias;		// how much that face is preferred
};

static const long kBraidLinkR[HD_LARA_HAIR_NODES] = HD_LARA_BRAID_RADII;

static void BraidBoxIdentity(BraidBox& box, double x, double y, double z)
{
	box.on = false;

	for (int i = 0; i < 9; i++)
		box.rot[i] = (i % 4) ? 0.0 : 1.0;

	box.org[0] = x;
	box.org[1] = y;
	box.org[2] = z;
	box.zBack = HD_LARA_PACK_BOX_Z_BACK;
	box.bias = HD_LARA_PACK_BOX_BIAS;
}

/* How far into the box the point stands, 0 when it is outside. The same
 * measurement the push uses, so "how deep" and "was it pushed" cannot
 * disagree. */
static long BraidBoxDepth(const BraidBox& box, const long p[3], int link,
	int* faceOut, double* moveOut)
{
	if (link < 1 || link > 6)
		return 0;

	const double dx = (double)p[0] - box.org[0];
	const double dy = (double)p[1] - box.org[1];
	const double dz = (double)p[2] - box.org[2];

	const double lx = dx * box.rot[0] + dy * box.rot[3] + dz * box.rot[6];
	const double ly = dx * box.rot[1] + dy * box.rot[4] + dz * box.rot[7];
	const double lz = dx * box.rot[2] + dy * box.rot[5] + dz * box.rot[8];

	const double r = (double)kBraidLinkR[link - 1];
	const double loX = -(double)HD_LARA_PACK_BOX_X - r;
	const double hiX = (double)HD_LARA_PACK_BOX_X + r;
	const double loY = (double)HD_LARA_PACK_BOX_Y_TOP - r;
	const double hiY = (double)HD_LARA_PACK_BOX_Y_BOTTOM + r;
	const double loZ = (double)box.zBack - r;
	const double hiZ = (double)HD_LARA_PACK_BOX_Z_FRONT + r;

	if (lx <= loX || lx >= hiX || ly <= loY || ly >= hiY ||
		lz <= loZ || lz >= hiZ)
		return 0;

	const double out[6] =
	{
		lx - loX, hiX - lx,
		ly - loY, hiY - ly,
		lz - loZ, hiZ - lz
	};

	int face = 0;
	double best = out[0];

	for (int i = 1; i < 6; i++)
	{
		const double weigh = i == 4 ? out[i] / (double)box.bias : out[i];
		const double against = face == 4 ? best / (double)box.bias : best;

		if (weigh < against)
		{
			best = out[i];
			face = i;
		}
	}

	if (faceOut)
		*faceOut = face;

	if (moveOut)
	{
		moveOut[0] = moveOut[1] = moveOut[2] = 0.0;
		moveOut[face >> 1] = (face & 1) ? best : -best;
	}

	return (long)(best + 0.5);
}

static bool BraidBoxPush(const BraidBox& box, long p[3], int link)
{
	int face = 0;
	double move[3];

	if (!box.on || !BraidBoxDepth(box, p, link, &face, move))
		return false;

	p[0] += (long)(move[0] * box.rot[0] + move[1] * box.rot[1] +
		move[2] * box.rot[2]);
	p[1] += (long)(move[0] * box.rot[3] + move[1] * box.rot[4] +
		move[2] * box.rot[5]);
	p[2] += (long)(move[0] * box.rot[6] + move[1] * box.rot[7] +
		move[2] * box.rot[8]);

	return true;
}

/* One step of HairControl's chain loop. `links` is hairs[0][0..6] - the root
 * and the six moving points - and `vel` their velocities. */
static long BraidStep(long pos[7][3], long vel[7][3], const long root[3],
	const long length[7], const BraidSphere* sphere, int spheres,
	const BraidBox& box, long* deepest, int* packHits)
{
	long moved = 0;

	if (packHits)
		*packHits = 0;

	pos[0][0] = root[0];
	pos[0][1] = root[1];
	pos[0][2] = root[2];

	for (int i = 1; i < 7; i++)
	{
		const long was[3] = { pos[i][0], pos[i][1], pos[i][2] };

		pos[i][0] += 3 * vel[i][0] / 4;
		pos[i][1] += 3 * vel[i][1] / 4;
		pos[i][2] += 3 * vel[i][2] / 4;

		pos[i][1] += 10;	// gravity; no water line and no floor indoors

		for (int j = 0; j < spheres; j++)
		{
			const long x = pos[i][0] - sphere[j].x;
			const long y = pos[i][1] - sphere[j].y;
			const long z = pos[i][2] - sphere[j].z;
			long long d = (long long)x * x + (long long)y * y +
				(long long)z * z;

			if (d >= (long long)sphere[j].r * sphere[j].r)
				continue;

			long dist = BraidSqrt(d);

			if (!dist)
				dist = 1;

			pos[i][0] = sphere[j].x + x * sphere[j].r / dist;
			pos[i][1] = sphere[j].y + y * sphere[j].r / dist;
			pos[i][2] = sphere[j].z + z * sphere[j].r / dist;

			/* The twin of HAIRLOG's own packhits field in the game: how
			 * many links the pack itself had to push this step. */
			if (j >= 6 && packHits)
				(*packHits)++;
		}

		// ... and out of the box, last, exactly as the game does it.
		if (BraidBoxPush(box, pos[i], i) && packHits)
			(*packHits)++;

		/* The chain rule. The game writes the angles of the link ABOVE and
		 * then rebuilds this one from them, one bone offset along its own z;
		 * the angles are the spherical angles of the vector it just walked,
		 * so the rebuilt point is the previous one plus one bone length in
		 * that direction. (game\hair.cpp: phd_atan(dz, dx) and
		 * -phd_atan(dist, dy), then phd_RotYXZ and phd_TranslateRel.) */
		const double dx = (double)(pos[i][0] - pos[i - 1][0]);
		const double dy = (double)(pos[i][1] - pos[i - 1][1]);
		const double dz = (double)(pos[i][2] - pos[i - 1][2]);
		double len = sqrt(dx * dx + dy * dy + dz * dz);

		if (len < 1.0e-6)
			len = 1.0;

		const double step = (double)length[i] / len;

		pos[i][0] = pos[i - 1][0] + (long)floor(dx * step + 0.5);
		pos[i][1] = pos[i - 1][1] + (long)floor(dy * step + 0.5);
		pos[i][2] = pos[i - 1][2] + (long)floor(dz * step + 0.5);

		vel[i][0] = pos[i][0] - was[0];
		vel[i][1] = pos[i][1] - was[1];
		vel[i][2] = pos[i][2] - was[2];

		const long d2 = labs(vel[i][0]) + labs(vel[i][1]) + labs(vel[i][2]);

		if (d2 > moved)
			moved = d2;
	}

	/* How deep a link stands inside the pack. Measured against the box,
	 * whether or not the box was switched on for this
	 * step - so a regime without it says how far the braid walks into the
	 * backpack it cannot feel. */
	if (deepest)
	{
		*deepest = 0;

		for (int i = 1; i < 7; i++)
		{
			const long in = BraidBoxDepth(box, pos[i], i, 0, 0);

			if (in > *deepest)
				*deepest = in;
		}
	}

	return moved;
}

/* Where the simulation leaves the braid, so that the picture can be the
 * simulation's and not a studio pose.
 *
 * A braid drawn as a chain of the tool's own invention -
 * turned out of its bind direction to point downwards and a little
 * backwards - is fine for proving that six bones move independently and
 * useless for the one question that matters: does the plait lie on the
 * pack or in it. -braidrest names one of the regimes below, runs it in the
 * bind pose, and hands the settled positions to the draw, which then builds
 * the six matrices exactly as the engine's braid_matrices does.
 *
 * A global rather than a return value because the sim is a printing function
 * and the picture is built six hundred lines further down; the alternative was
 * to thread an out-parameter through both, which buys nothing. */
static long g_braidRest[7][3];
static bool g_braidRestHave = false;
static int g_braidRestWant = -1;

static void PrintBraidSim(const Tr5Level& level, int bodySlot,
	const ClassicSlot& body, const ClassicSlot& hairTree, bool haveHair,
	int steps)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
	{
		printf("\n-braidsim needs the level's Lara skeleton and this one has "
			"none.\n");
		return;
	}

	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(bodySlot, info))
	{
		printf("\n-braidsim: %s has no moveable entry.\n", HDSlotName(bodySlot));
		return;
	}

	/* The five classic spheres plus the neck one, exactly as HairControl
	 * builds them: a mesh's own centre and radius, in the frame of its node.
	 * In the bind pose the node frames carry no rotation, so a node's origin
	 * is ClassicSlot::bind and the mesh offset is a plain addition. The three
	 * hand-tuned constants (-9 and +25 on the torso, -2 on the head) are
	 * Core's own and come along. */
	static const int kNode[5] = { 0, 7, 14, 8, 11 };	// hips torso head arms
	static const int kInto[5] = { 0, 1, 2, 3, 4 };	// which sphere each fills
	BraidSphere sphere[8];
	int16_t mesh[5][6];

	for (int i = 0; i < 5; i++)
		if (!level.debugMeshHeader(info.meshIndex + kNode[i], mesh[i]))
		{
			printf("\n-braidsim: mesh %d of %s is not readable.\n", kNode[i],
				HDSlotName(bodySlot));
			return;
		}

	/* The torso sphere has two settings and they are not the same size, so
	 * both are run. HairControl anchors the braid differently for the Russia
	 * outfit - the cap and the bomber jacket - than for every other: the
	 * sphere moves 11 left, 16 up and its radius is nailed to 94 instead of
	 * the mesh's own. `anchor` 0 is the ordinary one, 1 the Russia one. */
	long torsoR[2] = { 0, 94 };
	long torsoOff[2][3] = { { -9, 0, 25 }, { -11, -16, 25 } };

	printf("\n-- the braid, simulated in the bind pose ---------------------\n");

	static const char* kSphereName[8] =
	{
		"0 hips", "1 torso", "2 head", "3 r upper arm", "4 l upper arm",
		"5 neck", "6 pack top", "7 pack bottom"
	};

	static const char* kAnchorName[2] =
	{
		"the ordinary torso anchor", "the Russia / bunhead torso anchor"
	};

	long length[7];

	for (int i = 1; i < 7; i++)
	{
		const int n = i < 6 ? i : 5;

		length[i] = haveHair && n < hairTree.nodes
			? (long)(hairTree.bind[n][2] - hairTree.bind[n - 1][2])
			: 56;
	}

	printf("\nlink lengths %ld %ld %ld %ld %ld %ld, %d steps per regime\n",
		length[1], length[2], length[3], length[4], length[5], length[6],
		steps);

	/* The regimes.
	 *
	 * The two "mixed" lines are the ones that matter: an earlier engine
	 * version put both of its root/pack changes into the else half of
	 * HairControl - the half
	 * that runs on a frame the animation does not interpolate - and left the
	 * interpolating half untouched. The engine takes one branch or the other
	 * from frame to frame, so what the braid really saw was a root that
	 * jumped between two places and a backpack that came and went, which is
	 * what these two lines run. */
	static const BraidRegime kRegime[8] =
	{
		{ "classic: root -48, 6 spheres",  HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, false, false },
		{ "mixed: -33 + pack, 1 step in 4", HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z,    8, 6, 4, false, false },
		{ "mixed: -33 + pack, every other", HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z,    8, 6, 2, false, false },
		{ "root -33, two pack spheres",    HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z_HD, 8, 8, 1, false, false },
		{ "root -48, two pack spheres",    HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    8, 8, 1, false, false },
		{ "root -48, no pack at all",      HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, false, false },
		/* The pack as a box, at the classic root and at
		 * the root an earlier engine version withdrew to. The second line asks whether that
		 * root's limit cycle - the neck sphere and a pack sphere pushing
		 * in turn - still happens with a box, which does not push the same way. */
		{ "root -48, the pack as a BOX",   HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_BRAID_ROOT_Z,    6, 6, 1, true, true },
		{ "root -33, the pack as a BOX",   HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_BRAID_ROOT_Z_HD, 6, 6, 1, true, true }
	};

	char rest[8][256];

	for (int anchor = 0; anchor < 2; anchor++)
	{

	for (int i = 0; i < 5; i++)
	{
		const int n = kNode[i];
		long ox = mesh[i][0], oy = mesh[i][1], oz = mesh[i][2];
		long r = mesh[i][3];

		if (n == 7)
		{
			ox += torsoOff[anchor][0];
			oy += torsoOff[anchor][1];
			oz += torsoOff[anchor][2];

			if (torsoR[anchor])
				r = torsoR[anchor];
		}
		else if (n == 14)
			ox -= 2;
		else if (n == 8 || n == 11)
			r = 4 * r / 3;

		sphere[kInto[i]].x = (long)body.bind[n][0] + ox;
		sphere[kInto[i]].y = (long)body.bind[n][1] + oy;
		sphere[kInto[i]].z = (long)body.bind[n][2] + oz;
		sphere[kInto[i]].r = r;
	}

	sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;
	sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
	sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
	sphere[5].r = 5 * sphere[2].r / 4;

	sphere[6].x = (long)body.bind[7][0] + HD_LARA_PACK_X;
	sphere[6].y = (long)body.bind[7][1] + HD_LARA_PACK_Y_TOP;
	sphere[6].z = (long)body.bind[7][2] + HD_LARA_PACK_Z;
	sphere[6].r = HD_LARA_PACK_R;

	sphere[7].x = (long)body.bind[7][0] + HD_LARA_PACK_X;
	sphere[7].y = (long)body.bind[7][1] + HD_LARA_PACK_Y_BOTTOM;
	sphere[7].z = (long)body.bind[7][2] + HD_LARA_PACK_Z;
	sphere[7].r = HD_LARA_PACK_R;

	printf("\n=== %s ===\n", kAnchorName[anchor]);
	printf("%-16s %7s %7s %7s %8s\n", "sphere", "x", "y", "z", "radius");

	for (int i = 0; i < 8; i++)
		printf("%-16s %7ld %7ld %7ld %8ld\n", kSphereName[i], sphere[i].x,
			sphere[i].y, sphere[i].z, sphere[i].r);

	printf("\n%-34s %8s %8s %8s %8s %9s %8s\n", "regime", "step 1", "step 10",
		"step 30", "last", "in a pack", "packhits");
	printf("%-34s %8s %8s %8s %8s %9s %8s\n",
		"----------------------------------",
		"--------", "--------", "--------", "--------", "---------",
		"--------");

	for (int r = 0; r < 8; r++)
	{
		long pos[7][3], vel[7][3] = { { 0, 0, 0 } };
		long deepest = 0, worstDeepest = 0;
		BraidBox box;

		/* The box sits in the torso node's frame, and in the bind pose that
		 * frame carries no rotation at all - so the identity plus the torso
		 * joint is the whole of it here. */
		BraidBoxIdentity(box, body.bind[7][0], body.bind[7][1], body.bind[7][2]);
		long at1 = 0, at10 = 0, at30 = 0, last = 0;
		int hits = 0, worstHits = 0;

		memset(vel, 0, sizeof(vel));

		/* The seed is the game's own first frame: first_hair hangs the chain
		 * straight down from the root, every velocity zero. */
		const long rootBase[3] =
		{
			(long)body.bind[HD_LARA_HEAD_NODE][0] + HD_LARA_BRAID_ROOT_X,
			(long)body.bind[HD_LARA_HEAD_NODE][1] + HD_LARA_BRAID_ROOT_Y,
			(long)body.bind[HD_LARA_HEAD_NODE][2]
		};

		long down = 0;

		for (int i = 0; i < 7; i++)
		{
			pos[i][0] = rootBase[0];
			pos[i][1] = rootBase[1] + down;
			pos[i][2] = rootBase[2] + kRegime[r].keyRootZ;

			if (i < 6)
				down += length[i + 1];
		}

		for (int s = 1; s <= steps; s++)
		{
			/* A key frame every `period` steps, the tween half in between -
			 * which is exactly how the engine alternates between the two
			 * halves of HairControl. */
			const bool key = (s % kRegime[r].period) == 0;
			long root[3] =
			{
				rootBase[0], rootBase[1],
				rootBase[2] + (key ? kRegime[r].keyRootZ
					: kRegime[r].tweenRootZ)
			};
			const int n = key ? kRegime[r].keySpheres
				: kRegime[r].tweenSpheres;

			box.on = key ? kRegime[r].keyBox : kRegime[r].tweenBox;

			last = BraidStep(pos, vel, root, length, sphere, n, box, &deepest,
				&hits);

			if (s > steps - 10 && hits > worstHits)
				worstHits = hits;

			if (s == 1) at1 = last;
			if (s == 10) at10 = last;
			if (s == 30) at30 = last;

			if (s > steps - 10 && deepest > worstDeepest)
				worstDeepest = deepest;
		}

		printf("%-34s %8ld %8ld %8ld %8ld %9ld %8d\n", kRegime[r].label, at1,
			at10, at30, last, worstDeepest, worstHits);

		/* The ordinary anchor's settled chain, for the picture. */
		if (!anchor && r == g_braidRestWant)
		{
			memcpy(g_braidRest, pos, sizeof(g_braidRest));
			g_braidRestHave = true;
		}

		/* Where the six links have come to rest. A regime that settles still
		 * has to settle in the right PLACE - a pack sphere that shoves the
		 * braid out sideways would read zero here and look wrong in the
		 * game. */
		strcpy(&rest[r][0], "");

		for (int i = 1; i < 7; i++)
			sprintf(&rest[r][strlen(rest[r])], " %ld/%ld/%ld", pos[i][0],
				pos[i][1], pos[i][2]);
	}

	printf("\nwhere the six links stand at the end:\n");

	for (int r = 0; r < 8; r++)
		printf("  %-34s%s\n", kRegime[r].label, rest[r]);

	}

	printf("\nthe four numbers are how far the busiest link still moves in one "
		"step\n(|dx|+|dy|+|dz|, in classic units); a settled braid reads 0 and "
		"stays there.\n\"in a pack\" is the deepest any link stands inside the "
		"pack BOX over the last\nten steps, measured whether or not that "
		"regime switched the box on - so a\nregime without it says how far "
		"the braid walks into the backpack.\n");
	printf("\"packhits\" is the twin of the game's own HAIRLOG field: how many "
		"links the\npack pushed out in one step.\n");
	printf("no wind, no water line and no floor: a braid that will not settle "
		"standing\nstill in a dry indoor room will not settle anywhere.\n");
}

/* --------------------------------------- the braid under an animation -- */
/*
 * The same simulation, driven by a real animation.
 *
 * Running the chain in the bind pose alone finds it settles, yet the braid
 * still moves in play: a bind pose has no anchor
 * motion at all, and in the game every one of the eight collision spheres
 * rides on Lara's own skeleton. Even standing at a door she breathes -
 * ANIM_BREATH moves her chest and her head a unit or two a frame - and the
 * moment she walks the whole rig travels.
 *
 * So this reads the level's OWN animation table, decodes the frames Lara
 * really wears, and rebuilds HairControl's node walk out of them once per
 * game frame: the hips out of the frame's own offset and node 0's rotation,
 * the torso by bone 7, the two upper arms by bones 8 and 11, the head by bone
 * 14, and the braid root 48 units behind the skull - the same order, the same
 * skips through the packed rotation stream (game\draw.cpp,
 * gar_RotYXZsuperpack), the same three hand-tuned offsets. Frames between two
 * key frames are blended frac/rate, which is what InterpolateMatrix does to
 * the matrix, and the positions are truncated to longs afterwards, which is
 * what `(long)aMXPtr[M03]` does.
 *
 * What comes out is the twin of the game's own HAIRLOG: how far the TIP moved
 * this frame and how far the ROOT moved, in the same four buckets its
 * log is counted in (under 2, 2 to 10, 10 to 40, over 40 units). One
 * row per variant, so the pack's cost is a number and not an opinion.
 *
 * Still left out, for the same reason as the bind-pose simulation: the wind (an
 * outdoor room, and a translation of the whole chain), and the water line. The
 * FLOOR is in, because with a real anchor at a real height it matters - a
 * braid 336 units long hanging from a head 251 units up reaches through the
 * ground she stands on, and the game's clamp (snap y to the floor, put x and z
 * back where they were) is itself a discontinuity that could shake a chain.
 * -braidfloor takes it out again for a comparison.
 */

static void MatRotZ(double deg, float* m)
{
	const double a = deg * 3.14159265358979323846 / 180.0;
	const float c = (float)cos(a), s = (float)sin(a);

	MatIdentity(m);
	m[0] = c;  m[1] = s;
	m[4] = -s; m[5] = c;
}

/* A TR rotation word is 65536 units to the circle. */
static double BraidDeg(int16_t angle)
{
	return (double)angle * 360.0 / 65536.0;
}

/* game\lara_states.h, ANIM_BREATH: Lara standing still and breathing. */
#define ANIM_BREATH_NUMBER	103

struct BraidAnimRow
{
	uint32_t frameOffset = 0;
	int rate = 1, size = 0, start = 0, end = 0;
	long velocity = 0;
};

static bool BraidReadAnim(const Tr5Level& level, int index, BraidAnimRow& out)
{
	size_t at = 0;
	int32_t n = 0;

	if (!level.debugAnimBlock(at, n) || index < 0 || index >= n)
		return false;

	const std::vector<uint8_t>& bytes = level.debugBytes();

	if (at + (size_t)(index + 1) * 40 > bytes.size())
		return false;

	const uint8_t* e = bytes.data() + at + (size_t)index * 40;
	int16_t s = 0;
	int32_t l = 0;

	memcpy(&out.frameOffset, e + 0, 4);
	out.rate = e[4];
	out.size = e[5];
	memcpy(&l, e + 8, 4);
	out.velocity = l;
	memcpy(&s, e + 24, 2);
	out.start = s;
	memcpy(&s, e + 26, 2);
	out.end = s;

	return out.rate > 0 && out.size > 0 && out.end >= out.start;
}

/* The packed rotation stream of one frame, and a cursor into it. One word when
 * the top two bits name a single axis, two when they are zero and three ten
 * bit angles follow - game\draw.cpp, gar_RotYXZsuperpack. */
struct BraidRotStream
{
	const Tr5Level* level;
	long long at;
};

static void BraidRotSkip(BraidRotStream& r, int skip)
{
	while (skip-- > 0)
	{
		const uint16_t w = (uint16_t)r.level->debugReadFrameShort(r.at);

		r.at += (w & 0xC000) ? 1 : 2;
	}
}

/* Applies the node's rotation to `m` and steps the cursor. `m` is row major
 * and a row vector's, which is the transpose of the engine's - so a local
 * rotation multiplies from the LEFT, and phd_RotYXZ's Y then X then Z order
 * comes out in that order here too. */
static void BraidRotApply(BraidRotStream& r, float* m)
{
	const uint16_t w = (uint16_t)r.level->debugReadFrameShort(r.at);
	float step[16], tmp[16];

	if ((w >> 14) == 0)
	{
		const uint16_t w2 = (uint16_t)r.level->debugReadFrameShort(r.at + 1);
		const uint32_t packed = ((uint32_t)w << 16) | (uint32_t)w2;
		const int16_t ay = (int16_t)(((packed >> 10) & 0x3FF) << 6);
		const int16_t ax = (int16_t)(((packed >> 20) & 0x3FF) << 6);
		const int16_t az = (int16_t)((packed & 0x3FF) << 6);

		if (ay)
		{
			MatRotY(BraidDeg(ay), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		if (ax)
		{
			MatRotX(BraidDeg(ax), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		if (az)
		{
			MatRotZ(BraidDeg(az), step);
			MatMul(step, m, tmp);
			memcpy(m, tmp, sizeof(tmp));
		}

		r.at += 2;
		return;
	}

	const int16_t a = (int16_t)((w & 0xFFF) << 4);

	if ((w >> 14) == 1)
		MatRotX(BraidDeg(a), step);
	else if ((w >> 14) == 2)
		MatRotY(BraidDeg(a), step);
	else
		MatRotZ(BraidDeg(a), step);

	MatMul(step, m, tmp);
	memcpy(m, tmp, sizeof(tmp));
	r.at += 1;
}

static void BraidTranslate(float* m, double x, double y, double z)
{
	float step[16], tmp[16];

	MatTranslate((float)x, (float)y, (float)z, step);
	MatMul(step, m, tmp);
	memcpy(m, tmp, sizeof(tmp));
}

/* The nine points HairControl reads off the mesh tree, in one key frame:
 * sphere 0..4 and 6..7 as the function builds them, plus the braid root. The
 * neck (sphere 5) is derived from two of the others and is left to the caller,
 * because the game derives it from the TRUNCATED longs. */
struct BraidFramePoints
{
	double sphere[8][3];
	double root[3];
	/* The torso node's whole frame. A sphere needs a
	 * centre and a box needs an orientation, so the pack's frame is carried
	 * out of here as it stands - row major, the tool's own convention, which
	 * is the transpose of the engine's. */
	double torso[16];
};

static void BraidOrigin(const float* m, double* out)
{
	out[0] = m[12];
	out[1] = m[13];
	out[2] = m[14];
}

/* `rootZ` is which of the two braid roots this pose hangs the chain from - the
 * classic -48, or the -33 an earlier engine version tried and withdrew. It is a
 * parameter because the question "does the near root
 * settle now that the pack is a box" cannot be asked without it. */
static void BraidPoseAt(const Tr5Level& level, const ClassicSlot& body,
	const int16_t mesh[5][6], const BraidAnimRow& a, int keyframe,
	const long torsoOff[3], long rootZ, BraidFramePoints& out)
{
	const long long base = (long long)(a.frameOffset / 2) +
		(long long)keyframe * a.size;

	BraidRotStream r;
	float m[16], push[16];

	r.level = &level;
	r.at = base + 9;

	MatIdentity(m);
	BraidTranslate(m, level.debugReadFrameShort(base + 6),
		level.debugReadFrameShort(base + 7),
		level.debugReadFrameShort(base + 8));

	BraidRotApply(r, m);			// node 0, the hips

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[0][0], mesh[0][1], mesh[0][2]);
	BraidOrigin(push, out.sphere[0]);

	BraidTranslate(m, body.delta[7][0], body.delta[7][1], body.delta[7][2]);
	BraidRotSkip(r, 6);			// the six leg nodes
	BraidRotApply(r, m);			// node 7, the torso
	/* lara.torso_?_rot would go here; they are zero for a Lara who is not
	 * aiming. */

	// The pack's own frame, before any mesh offset - game\hair.cpp's
	// HdPackFrame stands exactly here.
	for (int i = 0; i < 16; i++)
		out.torso[i] = (double)m[i];

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[1][0] + torsoOff[0], mesh[1][1] + torsoOff[1],
		mesh[1][2] + torsoOff[2]);
	BraidOrigin(push, out.sphere[1]);

	// The HD pack, in the torso node's own frame - game\hair.cpp, HdPackSpheres
	memcpy(push, m, sizeof(push));
	BraidTranslate(push, HD_LARA_PACK_X, HD_LARA_PACK_Y_TOP, HD_LARA_PACK_Z);
	BraidOrigin(push, out.sphere[6]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, HD_LARA_PACK_X, HD_LARA_PACK_Y_BOTTOM, HD_LARA_PACK_Z);
	BraidOrigin(push, out.sphere[7]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, body.delta[8][0], body.delta[8][1], body.delta[8][2]);
	BraidRotApply(r, push);			// node 8, the right upper arm
	BraidTranslate(push, mesh[3][0], mesh[3][1], mesh[3][2]);
	BraidOrigin(push, out.sphere[3]);

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, body.delta[11][0], body.delta[11][1], body.delta[11][2]);
	BraidRotSkip(r, 2);			// the right forearm and hand
	BraidRotApply(r, push);			// node 11, the left upper arm
	BraidTranslate(push, mesh[4][0], mesh[4][1], mesh[4][2]);
	BraidOrigin(push, out.sphere[4]);

	BraidTranslate(m, body.delta[14][0], body.delta[14][1], body.delta[14][2]);
	BraidRotSkip(r, 2);			// the left forearm and hand
	BraidRotApply(r, m);			// node 14, the head
	/* lara.head_?_rot would go here and are zero for the same reason. */

	memcpy(push, m, sizeof(push));
	BraidTranslate(push, mesh[2][0] - 2, mesh[2][1], mesh[2][2]);
	BraidOrigin(push, out.sphere[2]);

	BraidTranslate(m, HD_LARA_BRAID_ROOT_X, HD_LARA_BRAID_ROOT_Y,
		(double)rootZ);
	BraidOrigin(m, out.root);
}

/* The tool's BraidBox out of a pose's torso frame. The tool's matrices are row
 * major and a row vector's - world[c] = sum_r local[r] * m[4r + c] - and the
 * engine's are the transpose of that, so the box's own R[r][c] is m[4c + r].
 * The identity comes out of both roads as the identity, which is the check
 * that matters. */
static void BraidBoxFromFrame(BraidBox& box, const double* m, double travelZ)
{
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			box.rot[3 * r + c] = m[4 * c + r];

	box.org[0] = m[12];
	box.org[1] = m[13];
	box.org[2] = m[14] + travelZ;
}

/* What the pack may do to a link. `spheres` 6 leaves it out of the loop
 * altogether; the rest is measured against it. */
struct BraidAnimVariant
{
	const char* label;
	int spheres;		// 6 = the classic body alone, 8 = with the pack
	long packR;		// the pack's radius
	long packZ;		// where its centre sits behind the torso joint
	bool packDamps;		// a pack push-out kills the link's velocity
	// The box instead of spheres, and which root it hangs from.
	bool box;
	long rootZ;
	int bias;		// how much the box prefers its rear face
};

/* One step of the chain with a moving anchor. The same loop BraidStep runs,
 * plus the game's floor clamp, and it reports how far the tip travelled. */
static long BraidAnimStep(long pos[7][3], long vel[7][3], const long root[3],
	const long length[7], const BraidSphere* sphere, const BraidAnimVariant& v,
	const BraidBox& box, long floorY, bool useFloor, int* packHits,
	long* deepest)
{
	const long tipWas[3] = { pos[6][0], pos[6][1], pos[6][2] };

	*packHits = 0;

	pos[0][0] = root[0];
	pos[0][1] = root[1];
	pos[0][2] = root[2];

	for (int i = 1; i < 7; i++)
	{
		const long was[3] = { pos[i][0], pos[i][1], pos[i][2] };
		const long before[3] = { pos[i][0], pos[i][1], pos[i][2] };
		bool packPushed = false;

		pos[i][0] += 3 * vel[i][0] / 4;
		pos[i][1] += 3 * vel[i][1] / 4;
		pos[i][2] += 3 * vel[i][2] / 4;

		pos[i][1] += 10;	// gravity

		/* The floor, as game\hair.cpp clamps it: x and z go back where they
		 * were and y snaps to the ground, but only within one click. */
		if (useFloor && pos[i][1] > floorY)
		{
			pos[i][0] = before[0];

			if (pos[i][1] - floorY <= 256)
				pos[i][1] = floorY;

			pos[i][2] = before[2];
		}

		for (int j = 0; j < v.spheres; j++)
		{
			const long x = pos[i][0] - sphere[j].x;
			const long y = pos[i][1] - sphere[j].y;
			const long z = pos[i][2] - sphere[j].z;
			const long long d = (long long)x * x + (long long)y * y +
				(long long)z * z;

			if (d >= (long long)sphere[j].r * sphere[j].r)
				continue;

			long dist = BraidSqrt(d);

			if (!dist)
				dist = 1;

			pos[i][0] = sphere[j].x + x * sphere[j].r / dist;
			pos[i][1] = sphere[j].y + y * sphere[j].r / dist;
			pos[i][2] = sphere[j].z + z * sphere[j].r / dist;

			if (j >= 6)
			{
				(*packHits)++;
				packPushed = true;
			}
		}

		// ... and out of the box, last, exactly as the game does it.
		if (BraidBoxPush(box, pos[i], i))
		{
			(*packHits)++;
			packPushed = true;
		}

		const double dx = (double)(pos[i][0] - pos[i - 1][0]);
		const double dy = (double)(pos[i][1] - pos[i - 1][1]);
		const double dz = (double)(pos[i][2] - pos[i - 1][2]);
		double len = sqrt(dx * dx + dy * dy + dz * dz);

		if (len < 1.0e-6)
			len = 1.0;

		const double step = (double)length[i] / len;

		pos[i][0] = pos[i - 1][0] + (long)floor(dx * step + 0.5);
		pos[i][1] = pos[i - 1][1] + (long)floor(dy * step + 0.5);
		pos[i][2] = pos[i - 1][2] + (long)floor(dz * step + 0.5);

		if (v.packDamps && packPushed)
		{
			vel[i][0] = 0;
			vel[i][1] = 0;
			vel[i][2] = 0;
		}
		else
		{
			vel[i][0] = pos[i][0] - was[0];
			vel[i][1] = pos[i][1] - was[1];
			vel[i][2] = pos[i][2] - was[2];
		}
	}

	/* How deep a link stands in the pack box, measured
	 * whether or not this variant switched the box on. */
	if (deepest)
	{
		*deepest = 0;

		for (int i = 1; i < 7; i++)
		{
			const long in = BraidBoxDepth(box, pos[i], i, 0, 0);

			if (in > *deepest)
				*deepest = in;
		}
	}

	return labs(pos[6][0] - tipWas[0]) + labs(pos[6][1] - tipWas[1]) +
		labs(pos[6][2] - tipWas[2]);
}

/* The two angles HairControl writes for a link, in degrees.
 *
 * game\hair.cpp gives link n-1 the spherical angles of the vector from it to
 * link n - phd_atan(dz, dx) around y and -phd_atan(sqrt(dz^2+dx^2), dy) around
 * x - and hd_laradraw.cpp's braid_matrices builds the DRAWN bone out of those
 * two. The positions they are taken from are longs, so for a braid hanging
 * straight down dx and dz are single digits and the yaw is whatever the
 * rounding of two of them says: the pitch is stable at -90 and the yaw is not
 * an angle at all but noise. Whether that noise reaches the picture is what
 * the dyaw column below measures.
 */
static void BraidAngles(const long pos[7][3], double* yaw, double* pitch)
{
	for (int i = 0; i < 6; i++)
	{
		const double dx = (double)(pos[i + 1][0] - pos[i][0]);
		const double dy = (double)(pos[i + 1][1] - pos[i][1]);
		const double dz = (double)(pos[i + 1][2] - pos[i][2]);
		const double flat = sqrt(dx * dx + dz * dz);

		yaw[i] = atan2(dx, dz) * 180.0 / 3.14159265358979323846;
		pitch[i] = -atan2(dy, flat) * 180.0 / 3.14159265358979323846;
	}
}

/* What the drawn bone is turned by about its own axis, under four rules.
 *
 * A rotation about the bone's own +z moves no vertex out of the braid and
 * lands the next bone in exactly the same place, so the roll is free and the
 * only question is which value of it does not tremble. `eff` comes back as the
 * azimuth the picture really shows, in degrees, so a rule is judged by how far
 * that moves from frame to frame. Lara faces +z in the tool, so her own facing
 * is 0.
 *
 *   NONE    the original rule: whatever phd_atan made of two
 *           single-digit integers.
 *   FACING  every link pulled towards her facing, by a weight that fades out
 *           as the link leaves the vertical.
 *   CHAIN   every link turned to the azimuth of the WHOLE braid, root to tip.
 *           That vector is 336 units long where a link is 56, so its own
 *           horizontal part is ten to thirty times better conditioned, and one
 *           reference for all six bones means they can never twist against
 *           each other - which is the thing a rigid six bone plait cannot
 *           hide.
 *   CHAINW  the same, faded out per link like FACING.
 */
enum
{
	BRAID_ROLL_NONE = 0,
	BRAID_ROLL_FACING,
	BRAID_ROLL_CHAIN,
	BRAID_ROLL_CHAINW,
	BRAID_ROLL_LOCK,
	BRAID_ROLL_FACING_ALL,
	BRAID_ROLL_COUNT
};

static const char* const kBraidRollName[BRAID_ROLL_COUNT] =
{
	"none - the raw yaw",
	"towards her facing, faded per link",
	"the whole chain's azimuth, every link",
	"the whole chain's azimuth, faded per link",
	"the chain's azimuth, held while it is vertical",
	"her facing, every link, always"
};

/* The held reference of BRAID_ROLL_LOCK. The chain's own azimuth is a good
 * angle while the braid has a horizontal part and noise while it hangs
 * straight; so it is followed when it is worth following and simply held
 * where it is when it is not. One value, and the only state in the whole
 * rule. */
static double g_braidLock = 0.0;
static bool g_braidLockSeen = false;

#define BRAID_LOCK_NEAR	16.0
#define BRAID_LOCK_FAR	48.0

static double BraidWrap(double a)
{
	while (a > 180.0)
		a -= 360.0;

	while (a < -180.0)
		a += 360.0;

	return a;
}

static void BraidRollFix(const long pos[7][3], const double* yaw, double* eff,
	int rule)
{
	const double chainX = (double)(pos[6][0] - pos[0][0]);
	const double chainZ = (double)(pos[6][2] - pos[0][2]);
	double reference = 0.0;

	if (rule == BRAID_ROLL_CHAIN || rule == BRAID_ROLL_CHAINW ||
		rule == BRAID_ROLL_LOCK)
		reference = atan2(chainX, chainZ) * 180.0 / 3.14159265358979323846;

	if (rule == BRAID_ROLL_LOCK)
	{
		const double flat = sqrt(chainX * chainX + chainZ * chainZ);
		double weight;

		if (flat <= BRAID_LOCK_NEAR)
			weight = 0.0;
		else if (flat >= BRAID_LOCK_FAR)
			weight = 1.0;
		else
			weight = (flat - BRAID_LOCK_NEAR) /
				(BRAID_LOCK_FAR - BRAID_LOCK_NEAR);

		if (!g_braidLockSeen)
		{
			g_braidLock = reference;
			g_braidLockSeen = true;
		}
		else
			g_braidLock += BraidWrap(reference - g_braidLock) * weight;

		reference = g_braidLock;
	}

	for (int i = 0; i < 6; i++)
	{
		if (rule == BRAID_ROLL_NONE)
		{
			eff[i] = yaw[i];
			continue;
		}

		if (rule == BRAID_ROLL_CHAIN || rule == BRAID_ROLL_LOCK)
		{
			eff[i] = reference;
			continue;
		}

		if (rule == BRAID_ROLL_FACING_ALL)
		{
			eff[i] = 0.0;	// her facing, and she faces +z in the tool
			continue;
		}

		const double dx = (double)(pos[i + 1][0] - pos[i][0]);
		const double dz = (double)(pos[i + 1][2] - pos[i][2]);
		const double flat = sqrt(dx * dx + dz * dz);
		double weight;

		if (flat <= (double)HD_LARA_BRAID_ROLL_NEAR)
			weight = 1.0;
		else if (flat >= (double)HD_LARA_BRAID_ROLL_FAR)
			weight = 0.0;
		else
			weight = ((double)HD_LARA_BRAID_ROLL_FAR - flat) /
				(double)(HD_LARA_BRAID_ROLL_FAR - HD_LARA_BRAID_ROLL_NEAR);

		eff[i] = yaw[i] + BraidWrap(reference - yaw[i]) * weight;

		if (rule == BRAID_ROLL_FACING)
			reference = eff[i];
	}
}

static double BraidAngleStep(double a, double b)
{
	double d = a - b;

	while (d > 180.0)
		d -= 360.0;

	while (d < -180.0)
		d += 360.0;

	return fabs(d);
}

static void BraidBucket(long step, int* into)
{
	if (step < 2)
		into[0]++;
	else if (step < 10)
		into[1]++;
	else if (step < 40)
		into[2]++;
	else
		into[3]++;
}

/* ------------------------------------------------- the pose risk -----------
 *
 * What the sixty hertz interpolation path really blends, and what a matrix
 * blend does to a big turn.
 *
 * A dive roll at sixty
 * hertz can show one in-between frame with a leg fully extended the other way
 * round, tucked before it and tucked after it. The obvious reading is a packed
 * ten bit angle interpolated the long way round. It is not that: the sixty hertz path hands
 * GetFramesAt's key frame pair and a factor to InitInterpolateAlpha, and the
 * blend itself is vanilla's own InterpolateMatrix, which lerps the two 3x4
 * matrices element by element and never touches an angle (game\draw.cpp,
 * game\delstuff.cpp Rich_CalcLaraMatrices_Interpolated).
 *
 * An element-wise lerp of two rotations is never a flip - it is a shrink. At
 * a relative angle D the blend at a half is the short-way bisector scaled by
 * cos(D/2) in the plane across the axis, so at D = 180 degrees the plane
 * collapses to nothing: the bone's own frame goes singular, its length in that
 * plane goes to zero, and whatever hangs off it lands somewhere that is not on
 * either of the two poses. That is what a leg pointing the wrong way looks
 * like, and it is a fact about the size of the turn between the two key frames
 * and nothing else.
 *
 * So the question is which pairs the sixty hertz path blends that vanilla never did. At
 * thirty hertz the blend runs between key frames k and k+1 at a quarter, a
 * half or three quarters - and only when the animation's rate is more than
 * one, because with rate 1 every frame is a key frame and vanilla's `frac` is
 * always zero. The sixty hertz path blends the rate 1 animations too: their two ticks are
 * two adjacent stored frames and the midpoint is that pair at a half. Those
 * pairs are a whole game frame apart and are the new exposure.
 *
 * This walks every animation of the Lara slot and measures both readings of
 * every adjacent key frame pair, per node:
 *
 *   turn    the relative rotation angle of the two node matrices, in degrees.
 *           This is the number the matrix blend shrinks by cos(turn/2), so
 *           `>90` costs a quarter of the bone's length at the midpoint and
 *           `>150` costs nine tenths of it.
 *   axis    the largest per-axis difference of the packed rotations, in ten
 *           bit units and wrapped into -512..511. Over 512 is impossible after the wrap, so
 *           a column of zeroes rules out a wrap-related cause entirely.
 */

struct PoseRiskRot
{
	float m[16];
	int a[3];		// the three packed angles, 16 bit
};

static void PoseRiskRead(const Tr5Level& level, long long& at, PoseRiskRot& out)
{
	const uint16_t w = (uint16_t)level.debugReadFrameShort(at);

	MatIdentity(out.m);
	out.a[0] = out.a[1] = out.a[2] = 0;

	if ((w >> 14) == 0)
	{
		const uint16_t w2 = (uint16_t)level.debugReadFrameShort(at + 1);
		const uint32_t packed = ((uint32_t)w << 16) | (uint32_t)w2;

		out.a[0] = (int16_t)(((packed >> 20) & 0x3FF) << 6);	// x
		out.a[1] = (int16_t)(((packed >> 10) & 0x3FF) << 6);	// y
		out.a[2] = (int16_t)((packed & 0x3FF) << 6);		// z
	}
	else
	{
		const int16_t a = (int16_t)((w & 0xFFF) << 4);

		out.a[(w >> 14) == 1 ? 0 : (w >> 14) == 2 ? 1 : 2] = a;
	}

	BraidRotStream r;
	r.level = &level;
	r.at = at;
	BraidRotApply(r, out.m);
	at = r.at;
}

/* The relative rotation of two node frames, in degrees. Both are pure
 * rotations, so A^T B is one too and its trace names the angle. */
static double PoseRiskTurn(const float* A, const float* B)
{
	double t = 0.0;

	for (int i = 0; i < 3; i++)
		for (int k = 0; k < 3; k++)
			t += (double)A[k * 4 + i] * (double)B[k * 4 + i] * (i == k ? 1.0 : 0.0);

	/* trace(A^T B) = sum over i of (column i of A) . (column i of B) */
	t = 0.0;

	for (int i = 0; i < 3; i++)
	{
		double d = 0.0;

		for (int k = 0; k < 3; k++)
			d += (double)A[i * 4 + k] * (double)B[i * 4 + k];

		t += d;
	}

	double c = (t - 1.0) * 0.5;

	if (c > 1.0) c = 1.0;
	if (c < -1.0) c = -1.0;

	return acos(c) * 180.0 / 3.14159265358979323846;
}

static void PrintPoseRisk(const Tr5Level& level, const char* stem)
{
	Tr5Level::MoveableInfo lara;

	if (!level.moveableInfo(0 /* LARA */, lara) || lara.animIndex < 0)
	{
		printf("\n-poserisk: this level has no LARA slot.\n");
		return;
	}

	/* How many animations are Lara's: up to the next moveable's first one. */
	size_t block = 0;
	int32_t total = 0;

	if (!level.debugAnimBlock(block, total))
	{
		printf("\n-poserisk: this level has no animation block.\n");
		return;
	}

	int last = total;
	const std::vector<int> slots = level.moveableSlots();

	for (size_t i = 0; i < slots.size(); i++)
	{
		Tr5Level::MoveableInfo one;

		if (!level.moveableInfo(slots[i], one) || one.animIndex <= lara.animIndex)
			continue;

		if (one.animIndex < last)
			last = one.animIndex;
	}

	const int count = last - lara.animIndex;

	printf("\n-- what a matrix blend does to a big turn -------------------\n");
	printf("%s: LARA has %d animations, table index %d..%d\n", stem, count,
		lara.animIndex, last - 1);
	printf("Every adjacent KEY FRAME pair of every animation, all 15 nodes.\n"
		"`turn` is the relative rotation of the two node frames in degrees -\n"
		"the number an element-wise matrix blend shrinks by cos(turn/2) - and\n"
		"`axis` the largest per-axis packed difference in TEN BIT units,\n"
		"wrapped into -512..511. rate 1 animations are the ones vanilla never\n"
		"blended at all and the sixty hertz pose blend does.\n\n");

	printf("  anim rate frames keys  worst turn  >90  >150  worst axis  >512\n");

	int rate1 = 0, rate1Risky = 0, anyRisky = 0;
	double worstAll = 0.0;
	int worstAnim = -1, worstNode = -1, worstKey = -1;
	int axisOver = 0;

	struct Row { int anim, rate, keys; double turn; int over90, over150, axis; };
	std::vector<Row> rows;

	for (int i = 0; i < count; i++)
	{
		BraidAnimRow a;

		if (!BraidReadAnim(level, lara.animIndex + i, a))
			continue;

		const int frames = a.end - a.start + 1;
		int keys = (a.end - a.start) / a.rate + 1;

		if ((a.end - a.start) % a.rate)
			keys++;

		if (keys < 2)
			continue;

		Row row;

		row.anim = i;
		row.rate = a.rate;
		row.keys = keys;
		row.turn = 0.0;
		row.over90 = 0;
		row.over150 = 0;
		row.axis = 0;

		std::vector<PoseRiskRot> prev(HD_LARA_NODES), now(HD_LARA_NODES);

		for (int k = 0; k < keys; k++)
		{
			long long at = (long long)(a.frameOffset / 2) +
				(long long)k * a.size + 9;

			for (int n = 0; n < HD_LARA_NODES; n++)
				PoseRiskRead(level, at, now[n]);

			if (k > 0)
				for (int n = 0; n < HD_LARA_NODES; n++)
				{
					const double turn = PoseRiskTurn(prev[n].m, now[n].m);

					if (turn > row.turn)
						row.turn = turn;

					if (turn > 90.0)
						row.over90++;

					if (turn > 150.0)
						row.over150++;

					if (turn > worstAll)
					{
						worstAll = turn;
						worstAnim = i;
						worstNode = n;
						worstKey = k;
					}

					for (int ax = 0; ax < 3; ax++)
					{
						int d = (now[n].a[ax] - prev[n].a[ax]) >> 6;

						while (d > 512) d -= 1024;
						while (d < -512) d += 1024;

						if (d < 0)
							d = -d;

						if (d > row.axis)
							row.axis = d;
					}
				}

			prev.swap(now);
		}

		if (a.rate == 1)
			rate1++;

		if (row.over90)
		{
			anyRisky++;

			if (a.rate == 1)
				rate1Risky++;
		}

		if (row.axis > 512)
			axisOver++;

		rows.push_back(row);

		(void)frames;
	}

	/* The worst thirty, and every rate 1 animation that carries a big turn -
	 * those are the frames the sixty hertz blend newly touches. */
	for (size_t pass = 0; pass < 2; pass++)
	{
		if (pass == 1)
			printf("\n  ... and EVERY rate 1 animation, which vanilla never "
				"blended at all:\n");

		int shown = 0;

		for (size_t i = 0; i < rows.size() && shown < 30; i++)
		{
			const Row& r = rows[i];

			if (pass == 0 && r.turn < 120.0)
				continue;

			if (pass == 1 && r.rate != 1)
				continue;

			printf("  %4d %4d %6d %4d  %10.1f %4d  %4d  %10d  %4d\n",
				r.anim, r.rate, r.keys * r.rate, r.keys, r.turn, r.over90,
				r.over150, r.axis, r.axis > 512 ? 1 : 0);
			shown++;
		}

		if (!shown)
			printf("  (none)\n");
	}

	printf("\n%d animations read, %d of them rate 1.\n", (int)rows.size(), rate1);
	printf("%d carry a key frame pair whose node turns more than 90 degrees; "
		"%d of those %s rate 1.\n", anyRisky, rate1Risky,
		rate1Risky == 1 ? "is" : "are");
	printf("worst turn %.1f degrees - anim %d, node %d, between key %d and "
		"%d.\n", worstAll, worstAnim, worstNode, worstKey - 1, worstKey);
	printf("animations whose per-axis packed difference exceeds 512 after the "
		"wrap: %d (a wrap cannot produce one, so this must be 0).\n", axisOver);
}


/* ======================================================================
 * The sixty hertz replay - one animation, every drawn frame.
 * ======================================================================
 *
 * What this is for. At sixty a dive roll can show one
 * frame with a leg stretched fully the wrong way, with the leg curled up on
 * the frames either side of it; at thirty the same roll is faultless. An
 * earlier guess assumed a rate 1 key frame pair too far apart to blend and put
 * a refusal in for it - and the counter it added, INTERPLOG's pose_far, read
 * 0.0 across a whole play session. A guess that measures zero is a
 * guess disproved, so this walks the animation offline instead and prints
 * where every node really stands on every drawn frame.
 *
 * What it reproduces, line for line:
 *
 *   the pose at a tick   GetFrames (game\draw.cpp): key = f / rate, and the
 *                        factor InterpKeyAlpha(f % rate, InterpKeyRate(...)).
 *   the pose between two InterpPoseAt (game\interpolate_math.h) at t = 0.5,
 *                        which is the midpoint of the two poses the ticks
 *                        either side of it draw - the whole of the sixty hertz path.
 *   the pose itself      the two key frames' chains built separately and
 *                        blended element by element per node, which is what
 *                        Rich_CalcLaraMatrices_Interpolated does through
 *                        InterpolateMatrix. That blend is the one thing in
 *                        the picture that can put a bone somewhere neither
 *                        of its two ends is, so it is reproduced exactly and
 *                        not approximated by a rotation blend.
 *
 * What it prints. Per drawn frame, every node's origin in her own frame - the
 * hips at the origin - and, for each, how far it stands from the midpoint of
 * the two frames either side of it, as a fraction of how far those two are
 * apart. A node moving smoothly reads near 0; a frame with the leg
 * curled, then straight the wrong way, then curled again cannot read near 0,
 * because that is the definition of an outlier.
 * `-tween60` names the animations; with none it takes the sprint dive and
 * the rolls. */

struct Tween60Chain
{
	float m[32][16];
};

/* One key frame's whole chain, in Lara's own frame - no item position and no
 * item rotation, because the question is about the pose and those two are
 * handled elsewhere and interpolate perfectly well. The walk is
 * Rich_CalcLaraMatrices's: the frame's own translation and the hips'
 * rotation, then per node pop, push, the bone offset and the node's rotation
 * off the same packed stream. */
static void Tween60Build(const Tr5Level& level, const ClassicSlot& body,
	const BraidAnimRow& a, int key, Tween60Chain& out)
{
	const long long base = (long long)(a.frameOffset / 2) + (long long)key * a.size;

	BraidRotStream r;
	float stack[32][16], cur[16];
	int top = 0;

	r.level = &level;
	r.at = base + 9;

	MatIdentity(cur);
	BraidTranslate(cur, level.debugReadFrameShort(base + 6),
		level.debugReadFrameShort(base + 7),
		level.debugReadFrameShort(base + 8));
	BraidRotApply(r, cur);
	memcpy(out.m[0], cur, sizeof(cur));

	for (int node = 1; node < body.nodes; node++)
	{
		if ((body.flags[node] & 1) && top > 0)
			memcpy(cur, stack[--top], sizeof(cur));

		if (body.flags[node] & 2)
		{
			memcpy(stack[top], cur, sizeof(cur));

			if (top < 31)
				++top;
		}

		BraidTranslate(cur, body.delta[node][0], body.delta[node][1],
			body.delta[node][2]);
		BraidRotApply(r, cur);
		memcpy(out.m[node], cur, sizeof(cur));
	}
}

/* The engine's own blend, expression for expression - the three cases of
 * InterpBlendFloat (game\interpolate_math.h) over the twelve elements
 * aInterpolateMatrix walks. The tool's matrices are the transpose of the
 * engine's, so the twelve are the 3x3 in rows 0..2 and the translation in
 * row 3; which twelve they are does not matter to an element-wise blend. */
static float Tween60BlendOne(float a, float b, float alpha)
{
	if (alpha == 0.5F)
		return (a + b) * 0.5F;

	if (alpha > 0.5F)
		return b - (b - a) * (1.0F - alpha);

	return a + (b - a) * alpha;
}

static void Tween60Blend(const Tween60Chain& A, const Tween60Chain& B,
	float alpha, int nodes, Tween60Chain& out)
{
	static const int kElem[12] = { 0, 1, 2, 4, 5, 6, 8, 9, 10, 12, 13, 14 };

	for (int n = 0; n < nodes; n++)
	{
		memcpy(out.m[n], A.m[n], sizeof(A.m[n]));

		for (int i = 0; i < 12; i++)
			out.m[n][kElem[i]] = Tween60BlendOne(A.m[n][kElem[i]],
				B.m[n][kElem[i]], alpha);
	}
}

/* game\draw.cpp, GetFrames: which key frame pair a TICK wears and at what
 * factor. Returns the key; `alpha` is 0 when the frame sits on a stored one. */
static int Tween60Tick(const BraidAnimRow& a, int frame, float* alpha)
{
	const int f = frame - a.start;
	const int key = f / a.rate;
	const int frac = f % a.rate;
	int rate = a.rate;
	const int num = rate * (f / rate + 1);

	/* Vanilla's own comparison, mismatch included: `num` is counted from the
	 * animation's start and `frame_end` from the level's. */
	if (num > a.end)
		rate = rate + a.end - num;

	*alpha = frac ? InterpKeyAlpha(frac, rate) : 0.0F;

	return key;
}

static const char* const kTween60Node[15] =
{
	"hips", "thigh R", "calf R", "foot R", "thigh L", "calf L", "foot L",
	"torso", "uarm R", "larm R", "hand R", "uarm L", "larm L", "hand L",
	"head"
};

static void PrintTween60(const Tr5Level& level, const ClassicSlot& body,
	const int* want, int wantCount)
{
	if (!body.ok || body.nodes < 15)
	{
		printf("\n-tween60 needs the level's own Lara skeleton and this one "
			"has none.\n");
		return;
	}

	Tr5Level::MoveableInfo lara;

	if (!level.moveableInfo(0 /* LARA */, lara) || lara.animIndex < 0)
	{
		printf("\n-tween60: this level has no LARA slot.\n");
		return;
	}

	printf("\n-- one animation at sixty, frame by frame --------------------\n");
	printf("A TICK frame is GetFrames; the frame between two ticks is\n"
		"InterpPoseAt at t = 0.5, blended element-wise per node exactly as\n"
		"InterpolateMatrix blends them. Per TWEEN frame:\n\n"
		"  pair    the key frame pair it blends and the factor. `>N` marks a\n"
		"          key past the ones this animation stores - the partial last\n"
		"          segment, where the pair reaches into whatever frame data\n"
		"          follows in the block.\n"
		"  off     how far the worst node stands from the MIDPOINT of the two\n"
		"          TICKS either side of it, over how far those two are apart.\n"
		"          An element-wise blend is affine in the factor, so a tween\n"
		"          inside one segment must read 0.00; anything else is a tween\n"
		"          that is NOT between the frames it stands between, which is\n"
		"          what a leg pointing the wrong way is.\n"
		"  legs    the hip-to-foot distance, right and left, on the tween over\n"
		"          the mean of the two ticks. The chord of an arc is shorter\n"
		"          than the arc, so a big turn reads under 1 - that is the\n"
		"          shrink, and vanilla has it at thirty as well.\n\n");

	double worstOff = 0.0;
	int worstAnim = -1, worstFrame = -1, worstNode = -1;
	double worstLeg = 1.0;
	int legAnim = -1, legFrame = -1;
	int reached = 0;
	int played = 0, stubs = 0;

	for (int w = 0; w < wantCount; w++)
	{
		BraidAnimRow a;
		const int index = lara.animIndex + want[w];

		if (!BraidReadAnim(level, index, a))
		{
			printf("anim %d: not readable (a stub in this level, or past the "
				"table)\n", want[w]);
			stubs++;
			continue;
		}

		played++;

		const int keys = (a.end - a.start) / a.rate + 1;

		/* One chain per stored key, and one past them. The one past is not a
		 * mistake: an animation whose frame count is not a whole number of
		 * segments has a last frame whose pair reaches beyond its own stored
		 * frames, and the engine reads it - so the tool reads it too, out of
		 * the same block, rather than pretending the animation stops on a
		 * segment boundary. */
		std::vector<Tween60Chain> key((size_t)keys + 2);

		for (int k = 0; k <= keys + 1; k++)
			Tween60Build(level, body, a, k, key[(size_t)k]);

		printf("anim %d: frames %d..%d, rate %d, size %d, %d stored keys%s\n",
			want[w], a.start, a.end, a.rate, a.size, keys,
			(a.end - a.start) % a.rate ? "  (a PARTIAL last segment)" : "");

		bool said = false;

		for (int f = a.start + 1; f <= a.end; f++)
		{
			long tkey = 0;
			float b = 0.0F;

			if (!InterpPoseAt(f - 1, f, a.start, a.rate, a.end, 0.5F, &tkey, &b))
				continue;

			float ap = 0.0F, ac = 0.0F;
			const int kp = Tween60Tick(a, f - 1, &ap);
			const int kc = Tween60Tick(a, f, &ac);

			Tween60Chain P, C, T;

			Tween60Blend(key[(size_t)kp], key[(size_t)kp + 1], ap, body.nodes, P);
			Tween60Blend(key[(size_t)kc], key[(size_t)kc + 1], ac, body.nodes, C);
			Tween60Blend(key[(size_t)tkey], key[(size_t)tkey + 1], b, body.nodes, T);

			double off = 0.0;
			int offNode = -1;

			for (int n = 0; n < 15; n++)
			{
				double span = 0.0, e = 0.0;

				for (int c = 0; c < 3; c++)
				{
					const double mid = ((double)P.m[n][12 + c] +
						(double)C.m[n][12 + c]) * 0.5;
					const double s = (double)C.m[n][12 + c] -
						(double)P.m[n][12 + c];
					const double dd = (double)T.m[n][12 + c] - mid;

					span += s * s;
					e += dd * dd;
				}

				span = sqrt(span);
				e = sqrt(e);

				const double here = span > 1.0 ? e / span : e;

				if (here > off)
				{
					off = here;
					offNode = n;
				}
			}

			double leg[2] = { 1.0, 1.0 };

			for (int side = 0; side < 2; side++)
			{
				const int foot = side ? 6 : 3;
				double lp = 0.0, lc = 0.0, lt = 0.0;

				for (int c = 0; c < 3; c++)
				{
					const double dp = (double)P.m[foot][12 + c] - (double)P.m[0][12 + c];
					const double dc = (double)C.m[foot][12 + c] - (double)C.m[0][12 + c];
					const double dt = (double)T.m[foot][12 + c] - (double)T.m[0][12 + c];

					lp += dp * dp;
					lc += dc * dc;
					lt += dt * dt;
				}

				const double mean = (sqrt(lp) + sqrt(lc)) * 0.5;

				leg[side] = mean > 1.0 ? sqrt(lt) / mean : 1.0;
			}

			const bool past = (long)(tkey + 1) > (long)(keys - 1);

			if (off > 0.01 || leg[0] < 0.9 || leg[1] < 0.9 || past)
			{
				printf("  %5d.5  pair key%ld/%ld%s a%.3f   off %.2f (%s)   "
					"legs %.2f / %.2f\n", f - 1, tkey, tkey + 1,
					past ? " >stored" : "", (double)b, off,
					offNode >= 0 ? kTween60Node[offNode] : "-",
					leg[0], leg[1]);
				said = true;
			}

			if (past)
				reached++;

			if (off > worstOff)
			{
				worstOff = off;
				worstAnim = want[w];
				worstFrame = f - 1;
				worstNode = offNode;
			}

			for (int side = 0; side < 2; side++)
				if (leg[side] < worstLeg)
				{
					worstLeg = leg[side];
					legAnim = want[w];
					legFrame = f - 1;
				}
		}

		if (!said)
			printf("  every tween frame is exactly the midpoint of its two "
				"ticks, and no leg loses a tenth of its length.\n");
	}

	printf("\nworst off: %.2f", worstOff);

	if (worstAnim >= 0)
		printf(" - anim %d, between frames %d and %d, %s", worstAnim,
			worstFrame, worstFrame + 1,
			worstNode >= 0 ? kTween60Node[worstNode] : "-");

	printf("\nworst leg: %.2f of its own length", worstLeg);

	if (legAnim >= 0)
		printf(" - anim %d, between frames %d and %d", legAnim, legFrame,
			legFrame + 1);

	printf("\ntween frames whose pair reaches past the animation's own stored "
		"keys: %d\n", reached);

	/* And how many animations that was really. Lara's anims
	 * are level-specific and a level stubs the ones it does not use, so a run
	 * over the whole range 0..473 replays fewer than 474 of them - and a
	 * verdict quoted as "over all 474" says more than the run did. The two
	 * numbers stand here so that the next quotation can be right. */
	printf("animations asked for: %d - %d replayed, %d stubbed in this "
		"level\n", wantCount, played, stubs);
}


static void PrintBraidAnim(const Tr5Level& level, int bodySlot,
	const ClassicSlot& body, const ClassicSlot& hairTree, bool haveHair,
	int steps, const int* anims, int animCount, bool useFloor)
{
	if (!body.ok || body.nodes <= HD_LARA_HEAD_NODE)
		return;

	Tr5Level::MoveableInfo info;

	if (!level.moveableInfo(bodySlot, info))
		return;

	Tr5Level::MoveableInfo laraInfo;

	if (!level.moveableInfo(0 /* LARA */, laraInfo))
	{
		printf("\n-braidanim: this level has no LARA entry, so no animations.\n");
		return;
	}

	static const int kNode[5] = { 0, 7, 14, 8, 11 };
	int16_t mesh[5][6];

	for (int i = 0; i < 5; i++)
		if (!level.debugMeshHeader(info.meshIndex + kNode[i], mesh[i]))
		{
			printf("\n-braidanim: mesh %d of %s is not readable.\n", kNode[i],
				HDSlotName(bodySlot));
			return;
		}

	long length[7];

	for (int i = 1; i < 7; i++)
	{
		const int n = i < 6 ? i : 5;

		length[i] = haveHair && n < hairTree.nodes
			? (long)(hairTree.bind[n][2] - hairTree.bind[n - 1][2])
			: 56;
	}

	/* f, g and h are the box variants. f is what ships - the box
	 * at the classic root; g asks whether the box with the root moved
	 * up into the hair knot settles too; h is the same box with no preference for its rear
	 * face, which is the measurement HD_LARA_PACK_BOX_BIAS rests on. */
	static const int kVariants = 8;
	static const BraidAnimVariant kVariant[kVariants] =
	{
		{ "a  the classic body alone (6 spheres)", 6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "b  two pack spheres (r36 at z-52)",     8, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "c  the pack damps instead of feeding",  8, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, true,  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "d  a thinner pack, same rear face",     8, 24, -64, false,
		  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "e  thinner still, same rear face",      8, 18, -70, false,
		  false, HD_LARA_BRAID_ROOT_Z, 1 },
		{ "f  the BOX, root -48",                  6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z,
		  HD_LARA_PACK_BOX_BIAS },
		{ "g  the BOX, root -33",                  6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z_HD,
		  HD_LARA_PACK_BOX_BIAS },
		{ "h  the BOX, no rear preference",        6, HD_LARA_PACK_R,
		  HD_LARA_PACK_Z, false, true,  HD_LARA_BRAID_ROOT_Z, 1 }
	};

	static const long kTorsoOff[3] = { -9, 0, 25 };

	printf("\n-- the braid, driven by Lara's own animations ----------------\n");
	printf("%d frames per animation, the floor is %s\n", steps,
		useFloor ? "in (y = her feet, as the game clamps it)" : "OUT");
	printf("the four counts are the TIP's travel in one frame, in the same "
		"buckets\nthe play test's HAIRLOG was counted in: under 2, 2 to 10, "
		"10 to 40, over 40.\n");

	for (int ai = 0; ai < animCount; ai++)
	{
		BraidAnimRow a;

		if (!BraidReadAnim(level, laraInfo.animIndex + anims[ai], a))
		{
			printf("\nanim %d: not in this level's table.\n", anims[ai]);
			continue;
		}

		const int frames = a.end - a.start + 1;
		const int keyframes = (a.end - a.start) / a.rate + 1;
		const long move = a.velocity >> 16;

		printf("\n=== anim %d: %d frames, rate %d, frame size %d, %d key "
			"frames, forward %ld/frame ===\n", anims[ai], frames, a.rate,
			a.size, keyframes, move);
		/* The roll rules change no position, so they are the same question
		 * under every physics variant - but the yaw they have to survive is
		 * NOT, because it is read off single-digit integers and those differ.
		 * So the worst of the five is what is reported. */
		double rollWorst[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
		int rollOver[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };

		printf("%-38s %6s %6s %6s %6s %6s %7s %6s %8s %7s %7s %7s\n",
			"variant", "tip<2", "2-10", "10-40", ">40", "worst", "meantip",
			"inpack", "packhits", "short", "dyaw", "dyaw>30");
		printf("%-38s %6s %6s %6s %6s %6s %7s %6s %8s %7s %7s %7s\n",
			"--------------------------------------", "------", "------",
			"------", "------", "------", "-------", "------", "--------",
			"-------", "-------", "-------");

		int rootBucket[4] = { 0, 0, 0, 0 };
		long rootWorst = 0;

		for (int vi = 0; vi < kVariants; vi++)
		{
			const BraidAnimVariant& v = kVariant[vi];
			BraidBox box;
			BraidSphere sphere[8];
			long pos[7][3], vel[7][3];
			int bucket[4] = { 0, 0, 0, 0 };
			long worst = 0, sum = 0, worstDeep = 0;
			int worstHits = 0;
			bool seeded = false;
			long lastRoot[3] = { 0, 0, 0 };
			double yaw[6], pitch[6], lastYaw[6];
			double eff[BRAID_ROLL_COUNT][6], lastEff[BRAID_ROLL_COUNT][6];
			double worstYaw = 0;
			double worstRule[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
			int yawJumps = 0;
			int ruleJumps[BRAID_ROLL_COUNT] = { 0, 0, 0, 0, 0, 0 };
			long shortest = 1 << 30;

			memset(vel, 0, sizeof(vel));
			memset(pos, 0, sizeof(pos));
			memset(lastYaw, 0, sizeof(lastYaw));
			memset(lastEff, 0, sizeof(lastEff));
			memset(eff, 0, sizeof(eff));
			g_braidLockSeen = false;
			BraidBoxIdentity(box, 0.0, 0.0, 0.0);
			box.on = v.box;
			box.bias = v.bias;

			for (int s = 0; s < steps; s++)
			{
				const int frame = s % frames;
				const int key = frame / a.rate;
				const int frac = frame % a.rate;
				BraidFramePoints A, B;

				BraidPoseAt(level, body, mesh, a, key, kTorsoOff, v.rootZ, A);

				double w = 0.0;

				if (frac && key + 1 < keyframes)
				{
					BraidPoseAt(level, body, mesh, a, key + 1, kTorsoOff,
						v.rootZ, B);
					w = (double)frac / (double)a.rate;
				}
				else
					B = A;

				/* Where the item itself has got to. An animation that carries
				 * Lara forward moves every sphere with her, and that is most
				 * of the anchor motion the game's own HAIRLOG counts. */
				const double travel = (double)move * (double)s;

				for (int j = 0; j < 8; j++)
				{
					sphere[j].x = (long)(A.sphere[j][0] +
						(B.sphere[j][0] - A.sphere[j][0]) * w);
					sphere[j].y = (long)(A.sphere[j][1] +
						(B.sphere[j][1] - A.sphere[j][1]) * w);
					sphere[j].z = (long)(A.sphere[j][2] +
						(B.sphere[j][2] - A.sphere[j][2]) * w + travel);
				}

				sphere[0].r = mesh[0][3];
				sphere[1].r = mesh[1][3];
				sphere[2].r = mesh[2][3];
				sphere[3].r = 4 * mesh[3][3] / 3;
				sphere[4].r = 4 * mesh[4][3] / 3;

				sphere[5].x = (2 * sphere[2].x + sphere[1].x) / 3;
				sphere[5].y = (2 * sphere[2].y + sphere[1].y) / 3;
				sphere[5].z = (2 * sphere[2].z + sphere[1].z) / 3;
				sphere[5].r = 5 * sphere[2].r / 4;

				sphere[6].r = v.packR;
				sphere[7].r = v.packR;
				sphere[6].z += v.packZ - HD_LARA_PACK_Z;
				sphere[7].z += v.packZ - HD_LARA_PACK_Z;

				/* The pack box's frame, blended
				 * between the two key frames component by component - which is
				 * what the engine's own InterpolateMatrix does to a matrix, so
				 * a slightly non-orthonormal blend is not an approximation
				 * here but the thing itself. */
				{
					double frame16[16];

					for (int i = 0; i < 16; i++)
						frame16[i] = A.torso[i] +
							(B.torso[i] - A.torso[i]) * w;

					BraidBoxFromFrame(box, frame16, travel);
				}

				const long root[3] =
				{
					(long)(A.root[0] + (B.root[0] - A.root[0]) * w),
					(long)(A.root[1] + (B.root[1] - A.root[1]) * w),
					(long)(A.root[2] + (B.root[2] - A.root[2]) * w + travel)
				};

				if (!seeded)
				{
					/* first_hair: the chain hangs straight down from the
					 * root, every velocity zero. */
					long down = 0;

					for (int i = 0; i < 7; i++)
					{
						pos[i][0] = root[0];
						pos[i][1] = root[1] + down;
						pos[i][2] = root[2];

						if (i < 6)
							down += length[i + 1];
					}

					seeded = true;
					lastRoot[0] = root[0];
					lastRoot[1] = root[1];
					lastRoot[2] = root[2];
					BraidAngles(pos, lastYaw, pitch);

					for (int r = 0; r < BRAID_ROLL_COUNT; r++)
						BraidRollFix(pos, lastYaw, lastEff[r], r);

					continue;
				}

				int hits = 0;
				long deep = 0;
				const long moved = BraidAnimStep(pos, vel, root, length,
					sphere, v, box, 0 /* her feet */, useFloor, &hits, &deep);

				BraidAngles(pos, yaw, pitch);

				for (int r = 0; r < BRAID_ROLL_COUNT; r++)
					BraidRollFix(pos, yaw, eff[r], r);

				/* The first ten steps are the seed settling, not the
				 * animation - the game has been running for hours by the
				 * time a player sees her. */
				if (s < 10)
				{
					lastRoot[0] = root[0];
					lastRoot[1] = root[1];
					lastRoot[2] = root[2];
					memcpy(lastYaw, yaw, sizeof(lastYaw));
					memcpy(lastEff, eff, sizeof(lastEff));
					continue;
				}

				double frameYaw = 0;

				for (int i = 0; i < 6; i++)
				{
					const double d = BraidAngleStep(yaw[i], lastYaw[i]);

					if (d > frameYaw)
						frameYaw = d;
				}

				for (int r = 0; r < BRAID_ROLL_COUNT; r++)
				{
					double worstHere = 0;

					for (int i = 0; i < 6; i++)
					{
						const double e = BraidAngleStep(eff[r][i],
							lastEff[r][i]);

						if (e > worstHere)
							worstHere = e;
					}

					if (worstHere > worstRule[r])
						worstRule[r] = worstHere;

					if (worstHere > 30.0)
						ruleJumps[r]++;
				}

				memcpy(lastYaw, yaw, sizeof(lastYaw));
				memcpy(lastEff, eff, sizeof(lastEff));

				if (frameYaw > worstYaw)
					worstYaw = frameYaw;

				if (frameYaw > 30.0)
					yawJumps++;

				const long span = (long)sqrt(
					(double)(pos[6][0] - pos[0][0]) * (pos[6][0] - pos[0][0]) +
					(double)(pos[6][1] - pos[0][1]) * (pos[6][1] - pos[0][1]) +
					(double)(pos[6][2] - pos[0][2]) * (pos[6][2] - pos[0][2]));

				if (span < shortest)
					shortest = span;

				BraidBucket(moved, bucket);
				sum += moved;

				if (moved > worst)
					worst = moved;

				if (deep > worstDeep)
					worstDeep = deep;

				if (hits > worstHits)
					worstHits = hits;

				if (!vi)
				{
					const long rs = labs(root[0] - lastRoot[0]) +
						labs(root[1] - lastRoot[1]) +
						labs(root[2] - lastRoot[2]);

					BraidBucket(rs, rootBucket);

					if (rs > rootWorst)
						rootWorst = rs;
				}

				lastRoot[0] = root[0];
				lastRoot[1] = root[1];
				lastRoot[2] = root[2];
			}

			const int counted = bucket[0] + bucket[1] + bucket[2] + bucket[3];

			printf("%-38s %6d %6d %6d %6d %6ld %7.2f %6ld %8d %7ld %7.0f "
				"%7d\n",
				v.label, bucket[0], bucket[1], bucket[2], bucket[3], worst,
				counted ? (double)sum / (double)counted : 0.0, worstDeep,
				worstHits, shortest, worstYaw, yawJumps);

			for (int r = 0; r < BRAID_ROLL_COUNT; r++)
			{
				if (worstRule[r] > rollWorst[r])
					rollWorst[r] = worstRule[r];

				if (ruleJumps[r] > rollOver[r])
					rollOver[r] = ruleJumps[r];
			}
		}

		printf("%-38s %6d %6d %6d %6d %6ld\n",
			"   (the ROOT's own travel, for scale)", rootBucket[0],
			rootBucket[1], rootBucket[2], rootBucket[3], rootWorst);

		printf("\n  %-46s %7s %8s\n",
			"the DRAWN bone's twist, per rule (worst variant)", "worst",
			"over 30");

		for (int r = 0; r < BRAID_ROLL_COUNT; r++)
			printf("  %-46s %7.0f %8d%s\n", kBraidRollName[r],
				rollWorst[r], rollOver[r],
				r == BRAID_ROLL_LOCK ? "   <- what ships" : "");
	}

	printf("\na tip that moves no further than its anchor is the braid "
		"following Lara;\nthe question this table answers is whether the two "
		"pack spheres add travel\nof their own, and how much.\n");
	printf("\"short\" is the shortest the chain ever got - root to tip, out of "
		"%ld when it\nhangs straight; anything much under that is the braid "
		"folded up on itself.\n\"dyaw\" is the worst one-frame change of any "
		"link's YAW in degrees and\n\"dyaw>30\" how many frames had one: that "
		"angle is what hd_laradraw turned the\nDRAWN bone by before the roll "
		"table, and for a braid hanging straight down it is\nread off two "
		"single-digit integers.\n",
		length[1] + length[2] + length[3] + length[4] + length[5] + length[6]);
	printf("\nthe roll table is the cure: a rotation about the bone's OWN axis "
		"moves no\nvertex and lands the next bone in exactly the same place, "
		"so the only\nquestion is which value of it does not tremble. Beware "
		"the last line - Lara\nnever turns in this tool, so \"her facing, "
		"always\" cannot move here and its\nzero is an empty one.\n");
}

/* ---------------------------------------------------- Lara's composite -- */
/* Everything the tool needs to know about one wardrobe picture;
 * a struct rather than eighteen arguments. */
/* -shadowpresence: the projected shadow's presence the receive is drawn
 * with (hd_shadow.h, HDShadowPresence of the switch's seconds), the strength
 * times it; exactly 1 without the switch. -shadowfog: the distance fog's
 * start, units (the frame's distanceFogStart); none without it. */
static float g_probe_presence = 1.0f;
static float g_probe_fog = 0.0f;

/* -shadowwith: further room groups drawn beside the -shadowroom group. */
static std::vector<int> g_probe_with;

/* -bulb: the fog bulbs of the picture, in view space, and how many. */
static HDFogBulb g_probe_bulbs[HD_MAX_BULBS];
static int g_probe_bulb_count = 0;

/* -under: the constants of an eye under the water, and whether given. */
static float g_probe_under[9];
static bool g_probe_under_set = false;

/* -under into the frame's lighting block: the eye under the water, world up
 * in view space out of the view's second row, and no fog bulbs, as the
 * engine's frame has it (hd_level.cpp, HDFrameDraw). Returns the bulbs the
 * frame keeps. */
static int PutUnder(HDFrameLighting& frame, const float* view, int bulbs)
{
	HDRenderWaterUnderProbe(g_probe_under_set ? g_probe_under : 0);

	if (!g_probe_under_set)
		return bulbs;

	const float len = sqrtf(view[4] * view[4] + view[5] * view[5] +
		view[6] * view[6]);

	frame.under = 1;
	frame.underRoom = -1;
	frame.underUp[0] = len > 0.0f ? -view[4] / len : 0.0f;
	frame.underUp[1] = len > 0.0f ? -view[5] / len : -1.0f;
	frame.underUp[2] = len > 0.0f ? -view[6] / len : 0.0f;
	frame.bulbCount = 0;
	printf("-under: world up in view space %.3f %.3f %.3f, depth %.0f\n",
		frame.underUp[0], frame.underUp[1], frame.underUp[2],
		g_probe_under[8]);
	return 0;
}

/* The bulbs into the frame's lighting block. HDRenderFrameBegin uploads them
 * and HDModelFrameBegin takes the count, the two calls the engine's frame
 * makes; the return value is that count. */
static int PutBulbs(HDFrameLighting& frame)
{
	frame.bulbs = g_probe_bulb_count ? g_probe_bulbs : 0;
	frame.bulbCount = g_probe_bulb_count;

	for (int i = 0; i < g_probe_bulb_count; i++)
		printf("bulb %d  : view space %.0f %.0f %.0f, radius %.0f, density "
			"%.4f, colour %.2f %.2f %.2f\n", i, g_probe_bulbs[i].x,
			g_probe_bulbs[i].y, g_probe_bulbs[i].z, g_probe_bulbs[i].rad,
			g_probe_bulbs[i].density, g_probe_bulbs[i].r, g_probe_bulbs[i].g,
			g_probe_bulbs[i].b);

	return g_probe_bulb_count;
}

/* One part's sheen into the lighting block. `on` is
 * false for -nosheen, which is the before picture. */
static void SetSheen(HDModelLighting& lighting, int part, bool on)
{
	float strength = 0.0f, power = 1.0f;

	HDLaraPartSheen(part, &strength, &power);

	lighting.specStrength = on ? strength : 0.0f;
	lighting.specPower = power;
}

/* ------------------------------------------------------------ the lips --
 *
 * -lips <FILE.TRL> <frame> draws a speaking face at one frame of its track
 * file by the engine's own road: HDModelLipsOpen, then the face's draw with
 * the track and the frame, which deforms the face into a vertex buffer of its
 * own (hd_model.cpp, lips_stream). -lips <FILE.TRL> <a>:<b>:<step> draws every
 * step-th frame from a to b and tiles the pictures into one sheet, left to
 * right and top to bottom. The file names the face and the track -
 * PIERRE_HEAD_108.TRL is track 108 of PIERRE_HEAD - and the engine looks for
 * it where it looks in the game. */
struct LipsAsk
{
	std::string file;		// as given
	std::string stem;		// the face the name says
	int track = -1;
	std::vector<int> frames;	// what the draw is handed, a key for -loop/-fire
	bool sheet = false;
	/* -loop and -fire (hd_model.h, HD_LIPS_TRACK_LOOP): the loop's frames the
	 * keys stand for, and the firing face's ticks on and off and its step. */
	std::vector<int> loopFrames;
	int fireOn = -1;
	int fireOff = 0;
	int fireStep = 0;
};

/* -loop and -fire into a LipsAsk: the loop's frames as HDModelLipsLoopKey
 * keys, or one HDModelLipsFireKey, the step taken the way the game takes it
 * (HDLipsFireStep: one tick on is the first step). A firing face stepped
 * back to 0 is the loop at its frame. */
static bool LipsLoopParse(const char* loop, const char* fire, LipsAsk& out)
{
	out = LipsAsk();

	std::vector<int> frames;
	int a = 0, b = 0, step = 0;

	if (loop && sscanf(loop, "%d:%d:%d", &a, &b, &step) == 3 && step > 0 && b >= a)
	{
		out.sheet = !fire;

		for (int f = a; f <= b; f += step)
			frames.push_back(f);
	}
	else
		frames.push_back(loop ? atoi(loop) : 0);

	out.file = std::string(loop ? "-loop " : "") + (loop ? loop : "") +
		(loop && fire ? " " : "") + (fire ? "-fire " : "") + (fire ? fire : "");
	out.loopFrames = frames;

	if (!fire)
	{
		out.track = HD_LIPS_TRACK_LOOP;

		for (size_t i = 0; i < frames.size(); i++)
			out.frames.push_back(HDModelLipsLoopKey(frames[i], 0));

		return true;
	}

	int on = 0, off = 0;

	if (sscanf(fire, "%d:%d", &on, &off) < 1 || on < 0 || off < 0)
		return false;

	out.fireOn = on;
	out.fireOff = off;
	out.fireStep = HDLipsFireStep(0, true, on + 1);

	if (off > 0)
		out.fireStep = HDLipsFireStep(out.fireStep, false, off);

	out.loopFrames.resize(1);
	out.track = out.fireStep > 0 ? HD_LIPS_TRACK_FIRE : HD_LIPS_TRACK_LOOP;
	out.frames.push_back(out.fireStep > 0
		? HDModelLipsFireKey(frames[0], out.fireStep)
		: HDModelLipsLoopKey(frames[0], 0));
	return true;
}

/* The picture's name for its lips: _lips<track>_<frame> for a track file,
 * _loop<frame> for the loop, _fire<on>[r<off>]_from<frame> for the firing
 * face; a sheet names its first frame, its last and its step. */
static std::string LipsName(const LipsAsk& lips, int track, bool sheet)
{
	char name[96] = "";

	if (track < 0 || lips.frames.empty())
		return name;

	if (lips.fireOn >= 0)
	{
		char off[16] = "";

		if (lips.fireOff > 0)
			sprintf(off, "r%d", lips.fireOff);

		sprintf(name, "_fire%d%s_from%d", lips.fireOn, off, lips.loopFrames[0]);
	}
	else if (track == HD_LIPS_TRACK_LOOP && sheet)
		sprintf(name, "_loop%d-%d-%d_sheet", lips.loopFrames.front(),
			lips.loopFrames.back(), lips.loopFrames.size() > 1
				? lips.loopFrames[1] - lips.loopFrames[0] : 1);
	else if (track == HD_LIPS_TRACK_LOOP)
		sprintf(name, "_loop%d", lips.loopFrames[0]);
	else if (sheet)
		sprintf(name, "_lips%d_%d-%d-%d_sheet", track, lips.frames.front(),
			lips.frames.back(), lips.frames.size() > 1
				? lips.frames[1] - lips.frames[0] : 1);
	else
		sprintf(name, "_lips%d_%d", track, lips.frames.front());

	return name;
}

static bool LipsParse(const char* file, const char* frames, LipsAsk& out)
{
	std::string name = file;
	const size_t slash = name.find_last_of("\\/");

	if (slash != std::string::npos)
		name = name.substr(slash + 1);

	if (name.size() > 4 && !_stricmp(name.c_str() + name.size() - 4, ".TRL"))
		name.erase(name.size() - 4);

	const size_t cut = name.find_last_of('_');

	if (cut == std::string::npos || cut + 1 >= name.size())
		return false;

	out.file = file;
	out.stem = name.substr(0, cut);
	out.track = atoi(name.c_str() + cut + 1);
	out.frames.clear();

	int a = 0, b = 0, step = 0;

	if (sscanf(frames, "%d:%d:%d", &a, &b, &step) == 3 && step > 0 && b >= a)
	{
		out.sheet = true;

		for (int f = a; f <= b; f += step)
			out.frames.push_back(f);
	}
	else
		out.frames.push_back(atoi(frames));

	return !out.frames.empty();
}

/* The face the engine keeps for a slot, out of its own level build lines. */
static std::string LipsFaceOf(int slot)
{
	char head[32];

	snprintf(head, sizeof(head), "slot=%d face=", slot);

	for (int i = 0; i < HDModelLipsLineCount(); i++)
		if (!strncmp(HDModelLipsLine(i), head, strlen(head)))
		{
			const char* face = HDModelLipsLine(i) + strlen(head);

			return std::string(face, strcspn(face, " "));
		}

	return std::string();
}

/* Opens the clip for the face of `slot` and says what happened. The track, or
 * -1 when the face draws as it was built. */
static int LipsOpen(const LipsAsk& lips, int slot, bool head)
{
	if (lips.track < 0)
		return -1;

	const std::string face = LipsFaceOf(slot);
	const int frames = HDModelLipsOpen(slot, head, lips.track);

	/* The loop and the firing face open the face's own block. */
	if (lips.track >= HD_LIPS_TRACK_LOOP)
	{
		printf("lips    : %s asks for the %s of the face of slot %d, %s - %s\n",
			lips.file.c_str(), lips.track == HD_LIPS_TRACK_FIRE ? "firing face"
				: "loop", slot, face.empty() ? "(no face that keeps lips)" : face.c_str(),
			frames > 0 ? "its block is open" : "no loop, the face draws as built");

		if (frames > 0)
			printf("          block of %d frames, loop of %d (%.1f s); drawing %d "
				"picture%s%s\n", frames, HDModelLipsLoop(slot, head),
				HDModelLipsLoop(slot, head) / 30.0, (int)lips.frames.size(),
				lips.frames.size() == 1 ? "" : "s", lips.sheet ? " as a sheet" : "");

		if (frames > 0 && lips.fireOn >= 0)
			printf("          fire key held %d tick%s, released %d: step %d of %d, "
				"from loop frame %d\n", lips.fireOn + 1, lips.fireOn ? "s" : "",
				lips.fireOff, lips.fireStep, HD_LIPS_EASE_IN_TICKS,
				lips.loopFrames[0]);

		return frames > 0 ? lips.track : -1;
	}

	printf("lips    : %s asks for track %d of %s; the face of slot %d is %s - %s\n",
		lips.file.c_str(), lips.track, lips.stem.c_str(), slot,
		face.empty() ? "(no face that keeps lips)" : face.c_str(),
		frames > 0 ? "the clip is open" : "no clip, the face draws as built");

	if (frames > 0)
		printf("          %d frames, %.1f s; drawing %d frame%s%s\n", frames,
			frames / 30.0, (int)lips.frames.size(),
			lips.frames.size() == 1 ? "" : "s", lips.sheet ? " as a sheet" : "");

	if (!face.empty() && _stricmp(face.c_str(), lips.stem.c_str()))
		printf("          NOTE: the file names %s and the slot's face is %s - "
			"the face's own track %d is what is drawn\n", lips.stem.c_str(),
			face.c_str(), lips.track);

	return frames > 0 ? lips.track : -1;
}

/* What the draws just did with the lips, out of the engine's own record. */
static void LipsSay()
{
	for (int i = 0; i < HDModelLipsEventCount(); i++)
	{
		const HDModelLipsEvent* e = HDModelLipsEventAt(i);

		/* A loop or firing key, taken apart (hd_model.h). */
		if (e->track == HD_LIPS_TRACK_LOOP)
			printf("  lips: %s slot %d, loop frame %d, %d past - %s, the farthest "
				"vertex %.2f units from the bind\n", e->head ? "face" : "model",
				e->slot, e->frame % HD_LIPS_KEY_STRIDE, e->frame / HD_LIPS_KEY_STRIDE,
				HDLipsSourceName(e->source), e->jaw);
		else if (e->track == HD_LIPS_TRACK_FIRE)
		{
			char from[32] = "in full";

			if (e->frame % HD_LIPS_KEY_STRIDE)
				sprintf(from, "from loop frame %d", e->frame % HD_LIPS_KEY_STRIDE - 1);
			else if (e->frame / HD_LIPS_KEY_STRIDE < HD_LIPS_EASE_IN_TICKS)
				sprintf(from, "from the bind");

			printf("  lips: %s slot %d, firing frame %d at step %d of %d %s - %s, the "
				"farthest vertex %.2f units from the bind\n",
				e->head ? "face" : "model", e->slot, HD_LIPS_FIRE_FRAME,
				e->frame / HD_LIPS_KEY_STRIDE, HD_LIPS_EASE_IN_TICKS, from,
				HDLipsSourceName(e->source), e->jaw);
		}
		else
			printf("  lips: %s slot %d, track %d frame %d - %s, the farthest vertex "
				"%.2f units from the bind\n", e->head ? "face" : "model", e->slot,
				e->track, e->frame, HDLipsSourceName(e->source), e->jaw);
	}

	HDModelLipsEventsClear();
}

/* One picture of a sheet into its place, tiles of w x h, `across` a row. */
static void LipsPaste(std::vector<unsigned long>& sheet, long sheetW,
	const std::vector<unsigned long>& tile, long w, long h, int at, int across)
{
	const long x0 = (long)(at % across) * w;
	const long y0 = (long)(at / across) * h;

	for (long y = 0; y < h; y++)
		memcpy(&sheet[(size_t)(y0 + y) * (size_t)sheetW + (size_t)x0],
			&tile[(size_t)y * (size_t)w], (size_t)w * 4);
}

// The sheet's path and its size, for `count` tiles.
static void LipsSheetSize(int count, long w, long h, int* across, long* sw, long* sh)
{
	int a = 1;

	while (a * a < count)
		a++;

	*across = a;
	*sw = w * a;
	*sh = h * ((count + a - 1) / a);
}

struct LaraOptions
{
	std::string root;
	std::string stem;
	std::string outfit;
	std::string guns;
	std::string holsters;		// "", "full", "empty", "uzi", "revolver"
	std::string look;		// "" = the whole figure, "head" = the head joint
	std::string back;
	std::string out;
	bool holstersFull;
	bool flare;
	bool torch;
	bool nodes;
	bool mirror;
	bool firstPerson;
	bool neck;
	/* The wardrobe measured by bone weight rather
	 * than by position - what carries node 14, and where the rim of that is. */
	bool headWeight;
	/* The mirrored hand weights, and what folding them back
	 * moves in the pose the command line asks for. -1 is off, otherwise the
	 * bar in per mille the fold is measured at. */
	int handFold;
	bool noFold;			// the picture before the hand fold existed
	bool noHandMix;			// the other half of that before picture
	/* -passes solid|additive: which of hd_model's passes Lara's picture
	 * draws - the opaque and alpha-tested runs, or the additive run alone.
	 * HD_PASS_ALL without the switch. A measurement of what each run adds,
	 * and nothing the game ever draws on its own. */
	unsigned int passes;
	/* -xray: the level's X-ray set planned beside the outfit and drawn in
	 * its place, as the composite draws her while lara.skelebob holds. */
	bool xray;
	/* The head rule forced for this picture - one of the
	 * HD_WARDROBE_HEAD_*, or -1 to let the wardrobe decide, which is what
	 * the engine always does. */
	int head;
	bool poseRisk;			// the sixty hertz pose blend risk scan
	/* The animations to replay at sixty, frame by
	 * frame, Lara-relative. Empty means the switch was not given. */
	std::vector<int> tween60;
	bool handLight;			// the wrist seam material measurement
	/* Put the outfit's own material back on Lara's
	 * skin pages, which is the picture before the hand took the skin's
	 * material - the forearm and
	 * the hand shaded by two different materials. */
	bool oldSkin;
	bool braid;			// the braid root table
	int braidSim;			// steps for the offline braid physics, 0 = off
	/* The same chain under a real animation. Empty =
	 * off; `braidFloor` is the game's own floor clamp, on by default. */
	std::vector<int> braidAnims;
	bool braidFloor;
	bool classicBraidRoot;		// the picture before the root moved to its HD position
	/* Draw one part of the composite and nothing else,
	 * so that a question about one part - "does the sheen reach the hands?" -
	 * is answered by a picture of that part and not by hunting for it in a
	 * picture of the whole of her. -1 draws them all. */
	int onlyPart;
	bool noSheen;			// the picture before the sheen split existed
	/* The chest's secondary motion at a fixed offset: x, y, z in the
	 * torso's own axes, units, the same on both sides, handed to every part
	 * the engine hands the motion to. Not set draws with none. */
	bool softTest;
	float softOffset[3];
	/* The projected shadow's map (hd_shadow.h): -shadowmap writes it as a
	 * grey picture, -shadowcast runs the cast pass inside the picture's own
	 * bracket where the game runs it, -shadowdevice runs the device checks.
	 * `shadowDir` is the light's travel direction in her model space (y
	 * down), held to the slant limit as the game holds it. */
	bool shadowMap;
	bool shadowCast;
	bool shadowDevice;
	float shadowDir[3];
	int shadowFrames;
	/* -shadowroom: the room group under her feet, at (x, z) room local;
	 * -shadowreceive runs the cast and the receive pass in the picture,
	 * -shadowempty with nothing cast, -shadownoguard without the stencil
	 * guard, -shadowtime times the two passes. */
	int shadowRoom;
	float shadowAt[2];
	bool shadowReceive;
	bool shadowEmpty;
	bool shadowNoGuard;
	int shadowTime;
	/* -shadowcreature: HD models of the level's other slots stood beside
	 * her, drawn in the picture and cast into the second map; -shadowitem
	 * the same without a blob, cast nowhere; up to eight of the two together.
	 * -shadowothers writes the second map as a grey picture. */
	struct Creature
	{
		std::string slot;	// a slot number or a slot name of the level
		float at[2];		// room local (x, z) with -shadowroom, else her own space
		float yaw;		// degrees about the vertical
		bool blob;		// a creature with a classic blob, or an item without
	};
	std::vector<Creature> creatures;
	bool shadowOthers;
	/* -itemanim <anim> <key>: the -shadowitem and -shadowcreature models
	 * in that keyframe of their own animation instead of the first frame;
	 * -itempush <e>: their node matrices scaled by 1 + e in view space,
	 * which keeps every pixel's place and moves only its depth - two runs
	 * with +e and -e differ exactly where a model and the room tie. */
	int itemAnim = -1;
	int itemKey = 0;
	double itemPush = 0.0;
	/* -nopocket: the pocket rule (hd_object_rules.h) left off - the
	 * before picture of a sliding door. */
	bool noPocket = false;
	// -noglass: the glass rule left off.
	bool noGlass = false;
	/* -itemcull none|back: the side of the -shadowitem models drawn, for
	 * every one of them; -1 leaves it to the pocket rule. */
	int itemCull = -1;
	/* -shadownomodels: the models' receive pass left out; -shadowcount: the
	 * pixels each model leaves visible and the models' pass reaches,
	 * counted through the stencil, and the models' mask written in place of
	 * the picture. */
	bool shadowNoModels;
	bool shadowCount;
	/* -shadowalong: the second box moved by this many units against the
	 * light, so the creatures stand that much nearer its far side. */
	float shadowAlong;
	/* -shadowunseen: the creatures cast into the second map and drawn in no
	 * pass, as an item the picture clipped away. -shadowmirror: the room's
	 * mirror - her reflection and the creatures' drawn into the copy, and with
	 * -shadowreceive the copy and the reflections taking the maps through the
	 * mirror's blocks; -shadowmirrorview: the camera reflected in the glass;
	 * -shadownomirror: the mirror's blocks left out. */
	bool shadowUnseen;
	bool shadowMirror;
	bool shadowMirrorView;
	bool shadowNoMirror;
	bool shadowMirrorMask;
	bool check;
	bool noDraw;
	// The ring's own presentation - see -invrot.
	short invRot[3];
	bool invRotSet;
	bool managed;
	int threads;
	long width, height;
	double fov, yaw, pitch, dist, shade;
	double shadeRgb[3];	// -shade r,g,b: Lara's nodes per channel
	/* The head cut (a cylinder, then a plane, then
	 * one bone weight, in that order of development): how much of the head node a
	 * fragment has to carry to be discarded, of 1. Negative is off, which is
	 * the picture before the cut existed. */
	double cutS;
	bool noCap;			// draw without the lid that closes the head cut
	double fpForward;		// FP_EYE_OFFSET; 0 is the eye against a wall
	/* The eye moved off the head joint, which is what
	 * a landing does - the camera leaves the head and the nape fills the
	 * picture. x/y/z in classic units. */
	double eyeOff[3];
	int cull;			// HDModelCull, on her body alone
	bool yawSet, pitchSet;		// did the caller name one?
	const HDObjLight* lights;
	int lightCount;
	const double (*turn)[2];
	// -meshbits, on the composite too.
	unsigned int nodeMask;
	// -lips, on her face.
	LipsAsk lips;
};

/* The engine's slot number for a name, out of the level that is loaded. Every
 * table in hd_lara speaks names, and this is the one place that turns them
 * back into the numbers the game keeps in LHolster, lara.holster and
 * lara.back_gun. */
/* The page cache, slot by slot - for -pagelist.
 *
 * Written as a sorted list of "key origin size bytes" so that two runs differ
 * only where the two builds really differ, and `diff` says which pages the
 * second one added. The size comes off the texture the cache made rather than
 * off the .DDS, because that is the memory the card holds: the cache decodes
 * to A8R8G8B8 with its mip chain and a page's cost is that chain, not the
 * compressed file.
 *
 * The cache never frees a page, so the union of the lists of outfit A and
 * outfit B is exactly what the cache holds after a change from A to B. */
static bool g_pagelist = false;

static void PrintPageList()
{
	struct Row { int key; int origin; int w, h, levels; double bytes; };
	std::vector<Row> rows;
	double total = 0.0;
	char label[32];

	for (int slot = 0; slot < HDPageCacheSlots(); slot++)
	{
		IDirect3DTexture9* tex = HDPageCacheSlotTexture(slot);
		Row row;

		row.key = HDPageCachePageOfSlot(slot);
		row.origin = HDPageCacheSlotOrigin(slot);
		row.w = row.h = row.levels = 0;
		row.bytes = 0.0;

		if (tex)
		{
			D3DSURFACE_DESC desc;

			row.levels = (int)tex->GetLevelCount();

			for (int level = 0; level < row.levels; level++)
				if (tex->GetLevelDesc((UINT)level, &desc) == D3D_OK)
				{
					if (!level)
					{
						row.w = (int)desc.Width;
						row.h = (int)desc.Height;
					}

					row.bytes += (double)desc.Width * desc.Height * 4.0;
				}
		}

		total += row.bytes;
		rows.push_back(row);
	}

	for (size_t a = 0; a + 1 < rows.size(); a++)
		for (size_t b = a + 1; b < rows.size(); b++)
			if (rows[b].key < rows[a].key)
			{
				const Row swap = rows[a];
				rows[a] = rows[b];
				rows[b] = swap;
			}

	printf("\n-- the page cache: %d slots, %.2f MiB ---------------------\n",
		(int)rows.size(), total / 1048576.0);

	for (size_t i = 0; i < rows.size(); i++)
	{
		const int mod = HDPageModIndex(rows[i].key);

		if (mod >= 0)
			sprintf(label, "mod%d:%d", mod, HDPageModNumber(rows[i].key));
		else if (rows[i].key >= HD_PAGE_ALT_BASE)
			sprintf(label, "TR3 %d", rows[i].key - HD_PAGE_ALT_BASE);
		else
			sprintf(label, "%d", rows[i].key);

		printf("page %-12s %-28s %5dx%-5d mips %2d  %8.3f MiB\n",
			label, HDPageCacheOriginText(rows[i].origin),
			rows[i].w, rows[i].h, rows[i].levels, rows[i].bytes / 1048576.0);
	}

	printf("page total %d slots %.3f MiB\n", (int)rows.size(), total / 1048576.0);
}

static int SlotByName(const Tr5Level& level, const char* want)
{
	const std::vector<int> slots = level.moveableSlots();

	for (size_t i = 0; i < slots.size(); i++)
	{
		const char* name = HDSlotName(slots[i]);

		if (name && !_stricmp(name, want))
			return slots[i];
	}

	return -1;
}

/* The same, WITHOUT asking the level. LHolster, lara.holster and lara.back_gun
 * are plain slot numbers out of the engine's own enum, and the engine writes
 * them from the INVENTORY: setup.cpp puts LARA_HOLSTERS_PISTOLS into both at
 * every level start whether or not the level ever loaded that object. A tool
 * that resolved them through the level's slot list would show an empty thigh
 * in the three VCI levels where the game shows a holster. */
static int SlotNumberByName(const char* want)
{
	for (int slot = 0; slot < HD_SLOT_COUNT; slot++)
	{
		const char* name = HDSlotName(slot);

		if (name && !_stricmp(name, want))
			return slot;
	}

	return -1;
}

/* The classic Lara mesh tree this level really carries. LARA_SKIN is the
 * skinned body the engine draws (laramisc.cpp copies its meshes into LARA at
 * level start), and LARA is the fallback for a level that has no skin - the
 * two trees are identical, which is why TROne accepts either. */
static bool LaraBodySlot(const Tr5Level& level, ClassicSlot& out, int* slotOut)
{
	static const char* kTry[2] = { "LARA_SKIN", "LARA" };

	for (int i = 0; i < 2; i++)
	{
		const int slot = SlotByName(level, kTry[i]);

		if (slot >= 0 && ReadClassicSlot(level, slot, out) &&
			out.nodes >= HD_LARA_NODES)
		{
			if (slotOut)
				*slotOut = slot;

			return true;
		}
	}

	return false;
}

/* A projected shadow's map off the card, the depth a texel - one of the
 * engine's own targets (hd_render.cpp, HDRenderShadowMap or
 * HDRenderShadowOthersMap), `size` texels a side, copied through a
 * system-memory surface of the target's own format. The second map is
 * G32R32F: its red channel goes to `out`, its green - each texel's caster's
 * place - to `places` when that is given. */
static bool ShadowMapRead(std::vector<float>& out, IDirect3DTexture9* map,
	int size, std::vector<float>* places = 0)
{
	IDirect3DSurface9* level = 0;
	IDirect3DSurface9* copy = 0;
	D3DSURFACE_DESC desc;
	bool ok = false;

	out.clear();

	if (places)
		places->clear();

	if (!map || FAILED(map->GetSurfaceLevel(0, &level)))
		return false;

	const bool two = SUCCEEDED(level->GetDesc(&desc)) &&
		desc.Format == D3DFMT_G32R32F;

	if (SUCCEEDED(g_device->CreateOffscreenPlainSurface(size, size,
		two ? D3DFMT_G32R32F : D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &copy, 0)) &&
		SUCCEEDED(g_device->GetRenderTargetData(level, copy)))
	{
		D3DLOCKED_RECT rect;

		if (SUCCEEDED(copy->LockRect(&rect, 0, D3DLOCK_READONLY)))
		{
			out.resize((size_t)size * size);

			if (places && two)
				places->resize((size_t)size * size);

			for (int y = 0; y < size; y++)
			{
				const float* row = (const float*)((const unsigned char*)
					rect.pBits + (size_t)y * rect.Pitch);

				for (int x = 0; x < size; x++)
				{
					out[(size_t)y * size + x] = row[two ? x * 2 : x];

					if (places && two)
						(*places)[(size_t)y * size + x] = row[x * 2 + 1];
				}
			}

			copy->UnlockRect();
			ok = true;
		}
	}

	if (copy)
		copy->Release();

	level->Release();
	return ok;
}

/* The map as a grey picture, and what is in it: white is the clear value 1
 * (nothing cast), grey the cast depth stretched over the depths that occur,
 * darker nearer the light. Prints the cast texels' count, their box in
 * texels and their depth range; the margins say whether the outline stands
 * inside the map with room to spare. `size` is the map's side and `depth`
 * the length of its box along the light, units. */
static bool ShadowMapWrite(const std::vector<float>& map, int size, float depth,
	const char* path)
{
	const int n = size;
	float lo = 1.0f, hi = 0.0f;
	int cast = 0, x0 = n, y0 = n, x1 = -1, y1 = -1, odd = 0;

	for (int y = 0; y < n; y++)
		for (int x = 0; x < n; x++)
		{
			const float v = map[(size_t)y * n + x];

			if (!(v == v) || v < 0.0f || v > 1.0f)
				odd++;

			if (!(v < 1.0f))
				continue;

			cast++;
			lo = v < lo ? v : lo;
			hi = v > hi ? v : hi;
			x0 = x < x0 ? x : x0;
			y0 = y < y0 ? y : y0;
			x1 = x > x1 ? x : x1;
			y1 = y > y1 ? y : y1;
		}

	std::vector<unsigned long> pixels((size_t)n * n, 0xFFFFFFFFu);
	const float span = hi > lo ? hi - lo : 1.0f;

	for (size_t i = 0; i < pixels.size(); i++)
		if (map[i] < 1.0f)
		{
			const unsigned long g = (unsigned long)(32.0f + 176.0f * (map[i] - lo) / span);

			pixels[i] = 0xFF000000u | (g << 16) | (g << 8) | g;
		}

	printf("  map: %d of %d texels cast (%.2f%%), texels x %d..%d y %d..%d, margins "
		"%d/%d/%d/%d (left/right/top/bottom), depth %.4f..%.4f (%.0f..%.0f units "
		"from the box's middle), %d texels not in 0..1\n", cast, n * n,
		100.0 * cast / ((double)n * n), x0, x1, y0, y1, x0, n - 1 - x1, y0, n - 1 - y1,
		lo, hi, (lo - 0.5) * depth, (hi - 0.5) * depth, odd);

	const bool ok = WritePng(path, &pixels[0], n, n);

	printf("  %-58s %s\n", path, ok ? "written" : "NOT written");
	return ok;
}

/* The stencil, read through occlusion queries: the pixels of the whole
 * target whose stencil value, masked with `mask`, equals `ref` - a quad over
 * the screen with the stencil test and nothing written. -1 without an
 * occlusion query. With `colour` the quad paints those pixels in it instead
 * (added, so two quads mix), and nothing is counted. */
static long StencilQuad(long width, long height, DWORD func, DWORD ref, DWORD mask,
	D3DCOLOR colour = 0)
{
	struct Vertex
	{
		float x, y, z, rhw;
		D3DCOLOR c;
	};

	const float w = (float)width - 0.5f, h = (float)height - 0.5f;
	const Vertex quad[4] =
	{
		{ -0.5f, -0.5f, 0.0f, 1.0f, colour }, { w, -0.5f, 0.0f, 1.0f, colour },
		{ -0.5f, h, 0.0f, 1.0f, colour }, { w, h, 0.0f, 1.0f, colour }
	};

	g_device->SetVertexShader(0);
	g_device->SetPixelShader(0);
	g_device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	g_device->SetTexture(0, 0);
	g_device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
	g_device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
	g_device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
	g_device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
	g_device->SetRenderState(D3DRS_FOGENABLE, FALSE);
	g_device->SetRenderState(D3DRS_LIGHTING, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, colour ? TRUE : FALSE);
	g_device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, colour ? 0xF : 0);
	g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
	g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
	g_device->SetRenderState(D3DRS_STENCILFUNC, func);
	g_device->SetRenderState(D3DRS_STENCILREF, ref);
	g_device->SetRenderState(D3DRS_STENCILMASK, mask);
	g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0);
	g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
	g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);

	long pixels = -1;

	if (colour)
		g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
	else
	{
		IDirect3DQuery9* query = 0;

		if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_OCCLUSION, &query)))
		{
			DWORD got = 0;

			query->Issue(D3DISSUE_BEGIN);
			g_device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
			query->Issue(D3DISSUE_END);

			while (query->GetData(&got, sizeof(got), D3DGETDATA_FLUSH) == S_FALSE)
				;

			pixels = (long)got;
			query->Release();
		}
	}

	g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
	g_device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	g_device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
	g_device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
	g_device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
	return pixels;
}

/* The device's state as a list of numbers, for "the same after as before":
 * the render target, the depth surface, the viewport, the scissor
 * rectangle, the shaders, the declaration, the FVF, stream 0, the indices,
 * the textures of stages 0 - 7, every render state from D3DRS_ZENABLE to
 * D3DRS_BLENDOPALPHA, the sampler states of stages 0 - 7, the 256 vertex and
 * the 224 pixel float constants. The references the Get calls add are
 * released at once - only the addresses are compared. `names` says what each
 * stretch of the list is. */
static void DeviceStateTake(std::vector<unsigned long long>& out,
	std::vector<std::string>* names = 0)
{
	out.clear();

	if (names)
		names->clear();

	const auto put = [&](unsigned long long v, const char* what)
	{
		out.push_back(v);

		if (names)
			names->push_back(what);
	};

	IDirect3DSurface9* surface = 0;

	if (SUCCEEDED(g_device->GetRenderTarget(0, &surface)) && surface)
	{
		put((unsigned long long)(size_t)surface, "render target");
		surface->Release();
	}
	else
		put(0, "render target");

	surface = 0;

	if (SUCCEEDED(g_device->GetDepthStencilSurface(&surface)) && surface)
	{
		put((unsigned long long)(size_t)surface, "depth surface");
		surface->Release();
	}
	else
		put(0, "depth surface");

	D3DVIEWPORT9 vp = {};
	RECT scissor = { 0, 0, 0, 0 };

	g_device->GetViewport(&vp);
	g_device->GetScissorRect(&scissor);
	put(vp.X, "viewport");
	put(vp.Y, "viewport");
	put(vp.Width, "viewport");
	put(vp.Height, "viewport");
	put((unsigned long long)scissor.left, "scissor");
	put((unsigned long long)scissor.top, "scissor");
	put((unsigned long long)scissor.right, "scissor");
	put((unsigned long long)scissor.bottom, "scissor");

	IDirect3DVertexShader9* vs = 0;
	IDirect3DPixelShader9* ps = 0;
	IDirect3DVertexDeclaration9* decl = 0;
	IDirect3DVertexBuffer9* vb = 0;
	IDirect3DIndexBuffer9* ib = 0;
	DWORD fvf = 0;
	UINT offset = 0, stride = 0;

	g_device->GetVertexShader(&vs);
	g_device->GetPixelShader(&ps);
	g_device->GetVertexDeclaration(&decl);
	g_device->GetFVF(&fvf);
	g_device->GetStreamSource(0, &vb, &offset, &stride);
	g_device->GetIndices(&ib);
	put((unsigned long long)(size_t)vs, "vertex shader");
	put((unsigned long long)(size_t)ps, "pixel shader");
	put((unsigned long long)(size_t)decl, "declaration");
	put(fvf, "fvf");
	put((unsigned long long)(size_t)vb, "stream 0");
	put(offset, "stream 0");
	put(stride, "stream 0");
	put((unsigned long long)(size_t)ib, "indices");

	if (vs) vs->Release();
	if (ps) ps->Release();
	if (decl) decl->Release();
	if (vb) vb->Release();
	if (ib) ib->Release();

	for (DWORD stage = 0; stage < 8; stage++)
	{
		IDirect3DBaseTexture9* texture = 0;

		g_device->GetTexture(stage, &texture);
		put((unsigned long long)(size_t)texture, "texture");

		if (texture)
			texture->Release();

		for (DWORD s = D3DSAMP_ADDRESSU; s <= D3DSAMP_DMAPOFFSET; s++)
		{
			DWORD v = 0;

			g_device->GetSamplerState(stage, (D3DSAMPLERSTATETYPE)s, &v);
			put(v, "sampler state");
		}
	}

	for (DWORD rs = D3DRS_ZENABLE; rs <= D3DRS_BLENDOPALPHA; rs++)
	{
		DWORD v = 0;

		if (SUCCEEDED(g_device->GetRenderState((D3DRENDERSTATETYPE)rs, &v)))
			put(((unsigned long long)rs << 32) | v, "render state");
	}

	float vertex[256 * 4], pixel[224 * 4];

	g_device->GetVertexShaderConstantF(0, vertex, 256);
	g_device->GetPixelShaderConstantF(0, pixel, 224);

	for (int i = 0; i < 256 * 4; i++)
	{
		unsigned int bits;

		memcpy(&bits, &vertex[i], 4);
		put(bits, "vertex constant");
	}

	for (int i = 0; i < 224 * 4; i++)
	{
		unsigned int bits;

		memcpy(&bits, &pixel[i], 4);
		put(bits, "pixel constant");
	}
}

/* -shadowcount's count and mask, after the picture's solid bracket: model 1
 * is her parts, 2 on the creatures and items in the order asked. */
static void CountModelPixels(long width, long height, int models, int drawnAgain)
{
	printf("shadowcount: %d model(s) drawn again by the models' pass\n", drawnAgain);

	for (int id = 1; id <= models && id < 64; id++)
	{
		const long visible = StencilQuad(width, height, D3DCMP_EQUAL, (DWORD)id << 2, 0xFC);
		const long reached = StencilQuad(width, height, D3DCMP_EQUAL, ((DWORD)id << 2) | 2, 0xFE);

		printf("  model %d (%s): %ld pixels visible, %ld reached by the models' pass%s\n", id,
			id == 1 ? "her parts" : "creature or item", visible, reached,
			reached == visible ? " - equal" : reached == 0 ? " - not drawn again"
			: " - DIFFER");
	}

	const long stray = StencilQuad(width, height, D3DCMP_EQUAL, 2, 0xFE);

	printf("  pixels the models' pass reached on no model: %ld%s\n", stray,
		stray == 0 ? "" : " - DEFECT");

	g_device->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
	StencilQuad(width, height, D3DCMP_NOTEQUAL, 0, 0xFC, D3DCOLOR_XRGB(255, 0, 0));
	StencilQuad(width, height, D3DCMP_EQUAL, 2, 2, D3DCOLOR_XRGB(0, 255, 0));
}

static int RunLara(const LaraOptions& o, const Tr5Level& level)
{
	/* --- the two skeletons the composite is folded against ------------- */
	ClassicSlot body, hair;
	int bodySlot = -1;

	if (!LaraBodySlot(level, body, &bodySlot))
	{
		printf("ModelProbe: this level has no Lara mesh tree - nothing to build\n");
		return 6;
	}

	printf("body    : %s, %d nodes\n", HDSlotName(bodySlot), body.nodes);

	const int hairSlot = SlotByName(level, "HAIR");
	bool haveHair = false;

	if (hairSlot >= 0 && ReadClassicSlot(level, hairSlot, hair) &&
		hair.nodes >= HD_LARA_HAIR_NODES)
	{
		haveHair = true;
		printf("braid   : HAIR, %d nodes, bind", hair.nodes);

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
			printf(" %.0f/%.0f/%.0f", hair.bind[n][0], hair.bind[n][1],
				hair.bind[n][2]);

		printf("\n");
	}
	else
		printf("braid   : this level has no usable HAIR tree - the braid stays classic\n");

	/* --- which outfit --------------------------------------------------- */
	int outfit = HD_LARA_OUTFIT_NONE;
	const char* why = "";

	if (!o.outfit.empty())
	{
		outfit = HDLaraOutfitFromName(o.outfit.c_str());

		if (outfit == HD_LARA_OUTFIT_NONE)
		{
			printf("ModelProbe: %s is no outfit this wardrobe knows\n",
				o.outfit.c_str());
			return 2;
		}

		why = "asked for on the command line";
	}
	else
	{
		/* The engine's own facts, read out of the level the way the engine
		 * reads them:
		 *
		 *   LaraDrawType = data[0] + LARA_NORMAL (specific\file.cpp), so the
		 *   header byte plus one.
		 *
		 *   "she wears the cap" is game\hair.cpp's own test - a Russia level
		 *   with LARA_NORMAL, or LARA_BUNHEAD anywhere. The Russia levels are
		 *   LVL5_BASE .. LVL5_SINKING_SUBMARINE, which are the four JOBY
		 *   files.
		 *
		 *   Young Lara is the byte and nothing else. Reading it as "an
		 *   ANDY file, or the byte" goes wrong: the engine builds the
		 *   child body only where the Irish range AND the script flag
		 *   GF_YOUNGLARA meet (game\laramisc.cpp, LaraInitialiseMeshes), the
		 *   randomizer clears that flag and puts an adult skin in the level,
		 *   and every randomized ANDY level would be reported classic
		 *   with a grown Lara standing in it. This tool reads the level FILE
		 *   and no SCRIPT.DAT, so the byte is the one fact it has - and it is
		 *   a true one: the randomizer writes the outfit byte and the flag in
		 *   the same breath. Vanilla ANDY1/2/3 carry 1, which is LARA_YOUNG.
		 *
		 *   The Tomb Raider III outfit is data[3] of the level header, passed
		 *   in raw, exactly as specific\hd\hd_laradraw.cpp passes it. 0 in
		 *   every vanilla file. */
		const int drawType = level.laraType() + 1;
		const bool joby = !_strnicmp(o.stem.c_str(), "JOBY", 4);
		const bool andy = !_strnicmp(o.stem.c_str(), "ANDY", 4);
		const bool cap = (joby && drawType == 1) || drawType == 3;

		outfit = HDLaraOutfitFor(drawType, cap, drawType == 2,
			level.hdOutfit(), &why);

		printf("header  : LaraType byte %d -> LaraDrawType %d, data[3] %d%s%s\n",
			level.laraType(), drawType, level.hdOutfit(),
			joby ? ", a Russia level" : "",
			andy ? ", an Ireland level" : "");
	}

	printf("outfit  : %s (%s) - %s\n",
		HDLaraOutfitModel(outfit) ? HDLaraOutfitModel(outfit) : "none",
		HDLaraOutfitLabel(outfit), why);

	if (outfit == HD_LARA_OUTFIT_NONE)
	{
		printf("\nnothing to draw - and that is the answer this level gives.\n");
		return 0;
	}

	/* --- plan, load, build ---------------------------------------------- */
	/* Which of Lara's slots this level really loaded - the same list the
	 * engine would build out of objects[n].loaded, and what keeps the
	 * wardrobe down to the weapons the level can actually hand her. */
	std::vector<int> present;

	{
		const std::vector<int> all = level.moveableSlots();

		for (size_t i = 0; i < all.size(); i++)
			if (HDSlotIsLara(all[i]))
				present.push_back(all[i]);
	}

	HDLaraLevel wardrobe;

	wardrobe.outfit = outfit;
	wardrobe.bodyBind = &body.bind[0][0];
	wardrobe.bodyNodes = HD_LARA_NODES;
	wardrobe.hairBind = haveHair ? &hair.bind[0][0] : 0;
	wardrobe.hairNodes = haveHair ? HD_LARA_HAIR_NODES : 0;
	wardrobe.slots = present.empty() ? 0 : &present[0];
	wardrobe.slotCount = (int)present.size();
	wardrobe.xray = o.xray;

	/* -xray: the squares of this level that can X-ray her, out of the
	 * randomizer's own reader of the floor data - the count the engine's own
	 * walk makes at the level build (hd_laradraw.cpp, skeleton_squares), so
	 * the two can be held side by side. A trigger is function 4 and its
	 * kind 13, SKELETON_T (global\types.h). */
	if (o.xray)
	{
		const std::vector<Tr5Room>& rooms = level.rooms();
		int squares = 0;
		std::string where;

		for (int r = 0; r < (int)rooms.size(); r++)
		{
			int here = 0;

			for (int sx = 0; sx < rooms[(size_t)r].numXSectors; sx++)
				for (int sz = 0; sz < rooms[(size_t)r].numZSectors; sz++)
				{
					std::vector<Tr5FloorEntry> chain;
					const int index = level.sectorIndex(r, sx, sz);

					if (index <= 0 || !level.readChain(index, chain))
						continue;

					for (const Tr5FloorEntry& e : chain)
						if (e.function == 4 && e.kind == 13)
						{
							here++;
							break;
						}
				}

			if (here)
			{
				char text[32];

				sprintf(text, "%s%d:%d", where.empty() ? "" : " ", r, here);
				where += text;
				squares += here;
			}
		}

		printf("xray    : %d squares carry a SKELETON trigger (room:squares "
			"%s)\n", squares, where.empty() ? "none" : where.c_str());
	}

	printf("slots   : %d of Lara's own slots loaded in this level\n",
		(int)present.size());

	/* -nofold: the hands built from the file exactly as it stands, which is
	 * the picture from before the hand fold existed. Set before the plan, because that is
	 * where the bar is read. */
	if (o.noFold)
	{
		HDLaraHandFoldBar(1001);
		printf("handfold: OFF (-nofold) - the mirrored weights are left where "
			"the file has them, which is the BEFORE picture\n");
	}

	if (o.noHandMix)
	{
		HDLaraHandsFromInstall(false);
		printf("handmix : OFF (-nohandmix) - a mod's hand model is drawn whole, "
			"in its own skin, which is the BEFORE picture\n");
	}

	/* -lara-head: the head rule this picture is drawn with. Before the
	 * plan as well, because that is where the parts are chosen - the engine's
	 * own switch, so "outfit" really is the picture from before the head rule existed. */
	if (o.head >= 0)
	{
		HDLaraHeadForce(o.head);
		printf("head    : forced to %s (-lara-head) - the wardrobe's own answer "
			"is overruled for this picture\n", HDWardrobeHeadWord(o.head));
	}

	HDModelSlot wanted[HD_LARA_PART_COUNT * 2];
	const int count = HDLaraPlan(&wardrobe, wanted, HD_LARA_PART_COUNT * 2);

	printf("wardrobe: %d parts planned\n", count);

	/* -shadowcreature: the level's slots stood beside her, planned the way
	 * the engine plans a slot of the level (its classic mesh tree as the
	 * bind pose and the parents for a separate face's neck), after her
	 * parts. Reserved, because every plan points into these. */
	std::vector<HDModelSlot> plan(wanted, wanted + count);
	std::vector<ClassicSlot> creatureTree;
	std::vector<int> creatureSlot;
	std::vector<size_t> creatureAsk;	// which -shadowcreature each one is

	creatureTree.reserve(o.creatures.size());

	for (size_t c = 0; c < o.creatures.size(); c++)
	{
		const std::string& name = o.creatures[c].slot;
		const int slot = isdigit((unsigned char)name[0]) ? atoi(name.c_str())
			: SlotByName(level, name.c_str());
		ClassicSlot tree;

		if (slot < 0 || !ReadClassicSlot(level, slot, tree) || tree.nodes < 1)
		{
			printf("creature: %s is no moveable of this level - left out\n", name.c_str());
			continue;
		}

		creatureTree.push_back(tree);
		creatureSlot.push_back(slot);
		creatureAsk.push_back(c);

		bool planned = false;

		for (size_t k = 0; k + 1 < creatureSlot.size(); k++)
			planned = planned || creatureSlot[k] == slot;

		if (planned)
			continue;

		HDModelSlot one;

		one.slot = slot;
		one.nodes = creatureTree.back().nodes;
		one.bind = &creatureTree.back().bind[0][0];
		one.parent = creatureTree.back().parent;
		one.name = 0;
		plan.push_back(one);
	}

	HDModelStats stats;
	const char* trouble = "";

	if (!HDModelLevelBegin(g_device, o.root.c_str(), plan.empty() ? wanted : &plan[0],
		(int)plan.size(), o.managed,
		o.threads, &stats, &trouble))
	{
		printf("ModelProbe: HDModelLevelBegin failed - %s\n", trouble);
		return 5;
	}

	// The glass of the placed items, as the main build marks it.
	for (int i = 0; i < (int)plan.size(); i++)
		HDModelSetGlassNodes(plan[i].slot, o.noGlass ? 0
			: ToolSeeThroughNodes(level, plan[i].slot, plan[i].nodes));

	HDLaraStats laraStats;
	const bool ok = HDLaraLevelBuild(&wardrobe, &laraStats);

	printf("\n-- what was loaded -------------------------------------------\n");
	printf("parts              %d wanted, %d on the card, %d missing%s",
		laraStats.wanted, laraStats.loaded, laraStats.missing,
		laraStats.twins ? "" : "\n");

	// The install's own hands beside a mod's gun are on the
	// card and are not parts, so the two numbers differ and this says why.
	if (laraStats.twins)
		printf(" (%d of them the install's own hands under a mod's gun)\n",
			laraStats.twins);
	printf("geometry           %d vertices, %d indices, %d draw calls\n",
		laraStats.vertices, laraStats.indices, laraStats.ranges);
	printf("buffers            %.1f KiB vertices + %.1f KiB indices = %.1f KiB\n",
		laraStats.vertexBytes / 1024.0, laraStats.indexBytes / 1024.0,
		(laraStats.vertexBytes + laraStats.indexBytes) / 1024.0);
	printf("pages              %d distinct in the cache, %.1f MiB, %.0f ms decode\n",
		stats.pages, stats.pageStats.bytes / 1048576.0, stats.pageStats.decodeMs);

	if (g_pagelist)
		PrintPageList();
	/* The phases of `building` - keys is
	 * the one that grew to twenty-five seconds in the game, and the three
	 * .TRM numbers say how much of the wardrobe a build really reads. */
	printf("time               %.0f ms reading the .TRM files, %.0f ms building"
		" (find %.0f, keys %.0f, pages %.0f, buffers %.0f) hash=%016llx"
		" hash_ms=%.0f\n",
		stats.readMs, stats.buildMs, stats.findMs, stats.keyMs,
		stats.pageMs, stats.bufferMs, stats.hash, stats.hashMs);
	printf("trm reads          %d files opened, %d loaded with geometry, %.0f KiB read\n",
		(int)stats.trmOpens, (int)stats.trmLoads, stats.trmBytes / 1024.0);
	printf("skeleton drift     %.0f mean over %d skinned parts, worst %.0f "
		"(the limit for an object is %.0f)\n",
		laraStats.driftMean, laraStats.driftParts, laraStats.driftWorst,
		(double)HD_SKELETON_DRIFT_LIMIT);

	for (int i = 0; i < HDModelNoteCount(); i++)
		printf("  note: %s\n", HDModelNote(i));

	// The engine's own lines for the faces' track files, word for word.
	for (int i = 0; i < HDModelLipsLineCount(); i++)
		printf("  HDLOG level=%s lips %s\n", o.stem.c_str(), HDModelLipsLine(i));

	for (int i = 0; i < HDLaraNoteCount(); i++)
		printf("  lara: %s\n", HDLaraNote(i));

	/* Part by part, because a mean drift hides the one part that does not
	 * belong - and because the coverage column is the answer to "which node
	 * does this file actually paint". */
	printf("\n%-26s %-28s %7s %7s %7s %s\n", "part", "model", "verts",
		"drift", "worst", "nodes");
	printf("-------------------------- ---------------------------- "
		"------- ------- ------- ------------------------\n");

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraPartModel(outfit, part) || !HDLaraPartPlanned(part))
			continue;

		const int pslot = HDLaraPartSlot(part);

		if (!HDLaraHasPart(part))
		{
			printf("%-26s %-28s %7s %7s %7s %s\n", HDLaraPartLabel(part),
				HDLaraPartModel(outfit, part), "-", "-", "-", "NOT LOADED");
			continue;
		}

		/* No number means there was no PAIR of nodes to measure - a part that
		 * covers one node, or the rigid face. The drift is a pairwise
		 * measurement and one node has no pair. */
		float mean = -1, worst = -1;
		char driftText[16] = "-", worstText[16] = "-";

		if (HDModelBindDrift(pslot, &mean, &worst))
		{
			sprintf(driftText, "%.0f", mean);
			sprintf(worstText, "%.0f", worst);
		}

		std::string cover;

		for (int node = 0; node < 32; node++)
			if (HDLaraPartCoverage(part) & (1u << node))
			{
				char text[8];

				sprintf(text, "%s%d", cover.empty() ? "" : ",", node);
				cover += text;
			}

		printf("%-26s %-28s %7d %7s %7s %s\n", HDLaraPartLabel(part),
			HDModelName(pslot), HDModelVertices(pslot), driftText, worstText,
			cover.c_str());
	}

	/* -xray: the X-ray set's parts in their own block, the same columns
	 * without the coverage, which the engine does not keep for them. */
	for (int part = 0; o.xray && part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraXrayPlanned(part))
			continue;

		const int xslot = HDLaraPartXraySlot(part);
		float mean = -1, worst = -1;
		char driftText[16] = "-", worstText[16] = "-";

		if (HDLaraHasXrayPart(part) && HDModelBindDrift(xslot, &mean, &worst))
		{
			sprintf(driftText, "%.0f", mean);
			sprintf(worstText, "%.0f", worst);
		}

		printf("%-26s %-28s %7d %7s %7s %s\n", HDLaraPartLabel(part),
			HDLaraPartXrayModel(part), HDLaraHasXrayPart(part)
			? HDModelVertices(xslot) : 0, driftText, worstText,
			HDLaraHasXrayPart(part) ? "x-ray" : "x-ray, NOT LOADED");
	}

	if (!ok)
	{
		printf("\nHDLaraLevelBuild says classic - see the notes above.\n");
		HDModelLevelEnd();
		return 6;
	}

	/* The chest's regions, out of the engine's own finder at the engine's
	 * own build: the fields of the engine's HDLOG softbody line with the
	 * finder's working numbers beside them. */
	{
		const HDSoftRegion* r = HDLaraSoftRegion();
		std::string parts;

		for (int part = 0; part < HD_LARA_PART_COUNT; part++)
		{
			const char* name = "-";

			if (HDLaraPartSoft(part, &name) && HDLaraHasPart(part))
				parts += (parts.empty() ? "" : ",") + std::string(name);
		}

		printf("\nsoftbody: outfit=%s ok=%d reason=%s apexL=(%.1f,%.1f,%.1f) "
			"apexR=(%.1f,%.1f,%.1f) radius=%.1f verts=%d/%d parts=%s\n",
			HDLaraOutfitModel(outfit) ? HDLaraOutfitModel(outfit) : "-", r->ok,
			r->reason ? r->reason : "-", r->apex[0][0], r->apex[0][1], r->apex[0][2],
			r->apex[1][0], r->apex[1][1], r->apex[1][2], r->radius, r->inside[0],
			r->inside[1], parts.empty() ? "-" : parts.c_str());
		printf("softbody: torso=%d height=%.1f prominence=%.2f up=(%.2f,%.2f,%.2f) "
			"side=(%.2f,%.2f,%.2f) fwd=(%.2f,%.2f,%.2f)\n", r->torsoVerts, r->height,
			r->prominence, r->up[0], r->up[1], r->up[2], r->side[0], r->side[1],
			r->side[2], r->fwd[0], r->fwd[1], r->fwd[2]);
	}

	/* --- the frame ------------------------------------------------------ */
	const int slotLara = SlotByName(level, "LARA");
	const int slotHolstersEmpty = SlotNumberByName("LARA_HOLSTERS");
	const int slotHolstersFull = SlotNumberByName("LARA_HOLSTERS_PISTOLS");
	const int slotFlare = SlotByName(level, "FLARE_ANIM");
	const int slotTorch = SlotByName(level, "TORCH_ANIM");

	static const struct { const char* word; const char* slot; } kGun[] =
	{
		{ "pistols",        "PISTOLS_ANIM" },
		{ "uzi",            "UZI_ANIM" },
		{ "shotgun",        "SHOTGUN_ANIM" },
		{ "hk",             "HK_ANIM" },
		{ "revolver",       "REVOLVER_ANIM" },
		{ "revolver_laser", "LARA_REVOLVER_LASER" },
		{ "grapple",        "CROSSBOW_ANIM" },
		{ "grapple_laser",  "LARA_CROSSBOW_LASER" },
		{ "crowbar",        "CROWBAR_ANIM" }
	};

	HDLaraFrame now;

	now.rightHandSlot = slotLara;
	now.leftHandSlot = slotLara;
	now.leftHolsterSlot = o.holstersFull ? slotHolstersFull : slotHolstersEmpty;

	/* -holsters names one of the engine's four holster slots by hand, so that
	 * a frame the engine can produce but the tool would never build on its own
	 * can be photographed - see the note further down. */
	if (!o.holsters.empty() && !_stricmp(o.holsters.c_str(), "uzi"))
		now.leftHolsterSlot = SlotNumberByName("LARA_HOLSTERS_UZIS");
	else if (!o.holsters.empty() && !_stricmp(o.holsters.c_str(), "revolver"))
		now.leftHolsterSlot = SlotNumberByName("LARA_HOLSTERS_REVOLVER");

	now.rightHolsterSlot = now.leftHolsterSlot;
	now.backGunSlot = -1;

	if (!o.guns.empty() && _stricmp(o.guns.c_str(), "none"))
	{
		int found = -1;

		for (int i = 0; i < (int)(sizeof(kGun) / sizeof(kGun[0])); i++)
			if (!_stricmp(o.guns.c_str(), kGun[i].word))
				found = SlotByName(level, kGun[i].slot);

		if (found < 0)
			printf("ModelProbe: -guns %s is not a weapon this level has\n",
				o.guns.c_str());
		else
		{
			now.rightHandSlot = found;
			now.leftHandSlot = found;

			/* The gun is in her hands, so the holsters are empty - which is
			 * what the engine does too (draw_pistol_meshes: LHolster and
			 * lara.holster both go back to LARA_HOLSTERS).
			 *
			 * Unless the caller said otherwise. A -holsters spelled out ON
			 * The command line beats this, because the engine's bookkeeping
			 * really can end up in the contradictory state and the seam's
			 * rule against it needs a way to be shown: -guns revolver
			 * -holsters revolver is the frame where the gun is
			 * in her hand and in the holster at once. */
			if (o.holsters.empty())
			{
				now.leftHolsterSlot = slotHolstersEmpty;
				now.rightHolsterSlot = slotHolstersEmpty;
			}
		}
	}

	if (o.flare && slotFlare >= 0)
		now.leftHandSlot = slotFlare;

	if (o.torch && slotTorch >= 0)
		now.leftHandSlot = slotTorch;

	if (!o.back.empty())
	{
		static const struct { const char* word; const char* slot; } kBack[] =
		{
			{ "shotgun", "SHOTGUN_ANIM" },
			{ "hk",      "HK_ANIM" },
			{ "grapple", "CROSSBOW_ANIM" }
		};

		for (int i = 0; i < (int)(sizeof(kBack) / sizeof(kBack[0])); i++)
			if (!_stricmp(o.back.c_str(), kBack[i].word))
				now.backGunSlot = SlotNumberByName(kBack[i].slot);
	}

	HDLaraDraw list[8];
	const int pieces = HDLaraFrameParts(&now, list, 8, o.xray);

	printf("\n-- this frame ------------------------------------------------\n");
	printf("hands   : right from %s, left from %s\n",
		HDSlotName(now.rightHandSlot) ? HDSlotName(now.rightHandSlot) : "-",
		HDSlotName(now.leftHandSlot) ? HDSlotName(now.leftHandSlot) : "-");
	printf("holsters: left %s, right %s\n",
		HDSlotName(now.leftHolsterSlot) ? HDSlotName(now.leftHolsterSlot) : "-",
		HDSlotName(now.rightHolsterSlot) ? HDSlotName(now.rightHolsterSlot) : "-");
	printf("back    : %s\n",
		HDSlotName(now.backGunSlot) ? HDSlotName(now.backGunSlot) : "nothing");
	/* -xray: the X-ray set in the outfit's place - its body, its hands and
	 * holsters out of their own block, and no face, rig or braid. */
	const bool xrayDraw = o.xray && HDLaraHasXrayPart(HD_LARA_PART_BODY);

	if (o.xray && !xrayDraw)
		printf("xray    : the X-ray set's body is not on the card - %s\n",
			HDLaraOutfitIsXray(outfit) ? "the outfit is the X-ray body and "
			"wears the set already" : "the outfit is drawn");

	printf("drawn   : %s%s%s", xrayDraw
		? HDLaraPartXrayModel(HD_LARA_PART_BODY) : HDLaraOutfitModel(outfit),
		!xrayDraw && HDLaraHasPart(HD_LARA_PART_FACE) ? " + LARA_HEAD" : "",
		!xrayDraw && HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG)
			? " + holster rig" : "");

	for (int i = 0; i < pieces; i++)
		printf(" + %s(%08x)", HDModelName(xrayDraw
			? HDLaraPartXraySlot(list[i].part) : HDLaraPartSlot(list[i].part)),
			list[i].nodeMask);

	if (!xrayDraw && HDLaraHasPart(HD_LARA_PART_BRAID))
		printf(" + HAIR_OG");

	printf("\n");

	/* --- the node table -------------------------------------------------- */
	if (o.nodes)
	{
		static const char* kNodeName[HD_LARA_NODES] =
		{
			"hips", "l thigh", "l shin", "l foot", "r thigh", "r shin",
			"r foot", "torso", "r inarm", "r outarm", "r hand", "l inarm",
			"l outarm", "l hand", "head"
		};

		/* The columns are the SOURCES a node's mesh can come from in the
		 * classic path: the skin, the weapon slot the mesh swap put at the
		 * hands, the holster slot at the thighs, the flare in the left hand,
		 * and the weapon on the back. The cell is the .TRM that covers the
		 * node when the source is that one. */
		struct Column { const char* head; int part; };

		Column column[6];
		int columns = 0;

		column[columns].head = "skin";
		column[columns++].part = HD_LARA_PART_BODY;
		column[columns].head = "face";
		column[columns++].part = HD_LARA_PART_FACE;

		/* The weapon column is the RIGHT hand's part - the one the game
		 * decides through WeaponObjectMesh - and the flare column the left
		 * hand's own model, which is the only one that can differ. */
		int weapon = HDLaraPartForSlot(now.rightHandSlot, HD_LARA_ROLE_HAND);

		if (weapon < 0)
			weapon = HD_LARA_PART_HAND_REST;

		int back = -1;

		for (int part = HD_LARA_PART_BACK_SHOTGUN;
			part <= HD_LARA_PART_BACK_GRAPPLE && back < 0; part++)
			if (HDLaraHasPart(part))
				back = part;

		column[columns].head = "weapon";
		column[columns++].part = weapon;
		column[columns].head = "holster";
		column[columns++].part = HD_LARA_PART_HOLSTER_PISTOL;
		column[columns].head = "flare";
		column[columns++].part = HD_LARA_PART_HAND_FLARE;
		column[columns].head = "back";
		column[columns++].part = back;

		printf("\n-- every Lara node against every source -----------------------\n");
		printf("%-5s %-9s %-24s %-4s %-6s", "node", "name",
			"classic bind (x y z)", "flag", "parent");

		for (int c = 0; c < columns; c++)
			printf(" %-26s", column[c].head);

		printf("\n");

		for (int node = 0; node < HD_LARA_NODES; node++)
		{
			printf("%-5d %-9s %7.0f %7.0f %7.0f %-4ld %-6d", node,
				kNodeName[node], body.bind[node][0], body.bind[node][1],
				body.bind[node][2], body.flags[node], body.parent[node]);

			for (int c = 0; c < columns; c++)
			{
				const int part = column[c].part;
				const bool has = HDLaraHasPart(part) &&
					(HDLaraPartCoverage(part) & (1u << node)) != 0;

				printf(" %-26s", has
					? HDModelName(HDLaraPartSlot(part))
					: (HDLaraHasPart(part) ? "-" : "(not loaded)"));
			}

			printf("\n");
		}

		printf("\nthe braid is its own six node chain and covers no body node.\n");
	}

	if (o.neck)
		PrintNeckTable(o.root, body);

	if (o.headWeight)
		PrintHeadWeight(o.root, body);

	if (o.poseRisk)
		PrintPoseRisk(level, "the level");

	if (!o.tween60.empty())
		PrintTween60(level, body, &o.tween60[0], (int)o.tween60.size());

	if (o.handLight)
		PrintHandLight(o.root, outfit, o.oldSkin);

	if (o.braid)
		PrintBraidTable(o.root, body);

	if (o.braidSim > 0)
		PrintBraidSim(level, bodySlot, body, hair, haveHair, o.braidSim);

	if (!o.braidAnims.empty())
		PrintBraidAnim(level, bodySlot, body, hair, haveHair,
			o.braidSim > 0 ? o.braidSim : 120, o.braidAnims.data(),
			(int)o.braidAnims.size(), o.braidFloor);

	if (o.noDraw)
	{
		HDModelLevelEnd();
		return 0;
	}

	/* --- the camera ------------------------------------------------------ */
	/* The head joint of this level's own Lara skeleton, which is where the
	 * first person camera hangs. Node 14 of the bind pose. */
	const float* const classic14 = body.bind[HD_LARA_HEAD_NODE];

	/* The composite's box is the union of every part's, because the outfit
	 * alone has an empty skull and a naked wrist. */
	float mins[3] = { 0, 0, 0 }, maxs[3] = { 0, 0, 0 };
	bool first = true;

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		if (!HDLaraHasPart(part) || part == HD_LARA_PART_BRAID)
			continue;

		float lo[3], hi[3];

		if (!HDModelBounds(HDLaraPartSlot(part), lo, hi))
			continue;

		for (int i = 0; i < 3; i++)
		{
			if (first || lo[i] < mins[i]) mins[i] = lo[i];
			if (first || hi[i] > maxs[i]) maxs[i] = hi[i];
		}

		first = false;
	}

	printf("\nbounds  : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	const double persp = EnginePersp(o.height, o.fov);

	/* -look head aims the camera at the head joint instead of the middle of
	 * the whole figure, which is the only way to get a picture of the
	 * braid against the back of the
	 * skull. Without it the camera frames hips to boots and the head sits off
	 * the top edge. */
	/* -look wrist does the same for the right hand joint. The seam this is
	 * about is one joint wide - the body's own
	 * forearm meets the separate HAND_ model there - and in a picture of the
	 * whole figure it is four pixels. The radius is the hand's own, not the
	 * body's, so the frame really is the wrist. */
	const bool lookHead = o.look == "head";
	const bool lookWrist = o.look == "wrist";
	const float* const classic10 = body.bind[HD_LARA_RHAND];
	const double cx = lookHead ? classic14[0]
		: lookWrist ? classic10[0] : 0.5 * (mins[0] + maxs[0]);
	const double cy = lookHead ? classic14[1]
		: lookWrist ? classic10[1] : 0.5 * (mins[1] + maxs[1]);
	const double cz = lookHead ? classic14[2]
		: lookWrist ? classic10[2] : 0.5 * (mins[2] + maxs[2]);
	const double radius = lookWrist ? 90.0 : 0.5 * sqrt(
		(maxs[0] - mins[0]) * (maxs[0] - mins[0]) +
		(maxs[1] - mins[1]) * (maxs[1] - mins[1]) +
		(maxs[2] - mins[2]) * (maxs[2] - mins[2]));

	double dist = o.dist;

	if (dist <= 0.0)
		dist = persp * radius / (0.45 * (double)o.height);

	const double d2r = 3.14159265358979323846 / 180.0;
	const double f[3] =
	{
		sin(o.yaw * d2r) * cos(o.pitch * d2r),
		sin(o.pitch * d2r),
		cos(o.yaw * d2r) * cos(o.pitch * d2r)
	};

	float view[16];

	/* The eye of the first person camera, where
	 * game/camera.cpp puts it and nowhere else:
	 *
	 *     GetLaraJointPos(&head, LMX_HEAD)   the head joint
	 *     eye.y = head.y - 16                sixteen units up
	 *     + FP_EYE_OFFSET (48) along yrot    forward, when mgLOS lets it
	 *
	 * The joint is the bind pose's node 14 here, and that is the same point:
	 * the joint is the origin of the head bone, and turning the head about it
	 * does not move it. So a bind-pose picture taken from this eye is the
	 * picture the game gives, minus the animation - and the neck stump the
	 * first person cut is about does not move with the animation either, because it
	 * hangs on the torso.
	 *
	 * Looking forward is yaw 0: a TR mesh faces +z, and the tool's own
	 * default of 180 is the camera standing in front of her. The pitch is the
	 * look-DOWN angle; the game clamps head_x_rot to 75 degrees and
	 * item->pos.x_rot can add to that while swimming, so 60 is a plain look
	 * at one's own feet and 75 the steepest a standing Lara reaches. */
	if (o.firstPerson)
	{
		const double ex = classic14[0] + o.eyeOff[0];
		const double ey = classic14[1] - 16.0 + o.eyeOff[1];
		const double ez = classic14[2] + o.fpForward + o.eyeOff[2];
		const double fpYaw = o.yawSet ? o.yaw : 0.0;
		const double fpPitch = o.pitchSet ? o.pitch : 60.0;

		LookAt(ex, ey, ez, fpYaw, fpPitch, view);

		printf("eye     : %.0f/%.0f/%.0f - the head joint %.0f/%.0f/%.0f, "
			"16 up and %.0f forward", ex, ey, ez, classic14[0], classic14[1],
			classic14[2], o.fpForward);

		/* And where -eye moved it from there. Naming the default derivation
		 * whatever the switches said would let a run with the eye taken out
		 * of the skull - the LANDING case - print the
		 * right coordinates under a description of a placement it was not
		 * using. */
		if (o.eyeOff[0] != 0.0 || o.eyeOff[1] != 0.0 || o.eyeOff[2] != 0.0)
			printf(", then moved by %.0f/%.0f/%.0f (-eye)", o.eyeOff[0],
				o.eyeOff[1], o.eyeOff[2]);

		printf("; yaw %.0f, pitch %.0f down\n", fpYaw, fpPitch);
	}
	else
		LookAt(cx - f[0] * dist, cy - f[1] * dist, cz - f[2] * dist, o.yaw,
			o.pitch, view);

	/* The mirror proof. A mirror room turns the world
	 * inside out, and a matrix that does that has a negative determinant:
	 * every triangle's winding flips, and a renderer that culls by winding
	 * loses the whole body. This one negates the view space x, which is the
	 * cheapest such matrix there is, and the picture that comes back says
	 * whether the HD path minds. It does not - hd_render's state bracket sets
	 * D3DCULL_NONE, the same thing the classic engine does globally
	 * (specific\function_table.cpp), and TR5's own mirror does not even
	 * need it: game\mirror.cpp mirrors Lara by MOVING her (z_pos = plane - z,
	 * the three angles negated), which is a rotation with determinant +1, and
	 * switches the SOFTWARE backface test with SetCullCW/SetCullCCW. */
	if (o.mirror)
	{
		float flip[16], mirrored[16];

		MatIdentity(flip);
		flip[0] = -1.0f;
		MatMul(view, flip, mirrored);
		memcpy(view, mirrored, sizeof(view));

		printf("mirror  : the view is flipped in x - determinant negative\n");
	}

	/* -shadowmirror: the mirror of the -shadowroom group, as the level build
	 * found it (hd_render.h, HDMirror), in this picture's world - the room's
	 * art is moved by -(x, z) of her stand point and the art is room local,
	 * so the plane and the copy's ends move by the room's origin and her
	 * stand point. `Refl` is the world's reflection in the glass, row vector
	 * on the left. -shadowmirrorview puts it in front of the view: the
	 * camera reflected in the glass, which sees the room where the picture
	 * without it sees the copy. */
	HDMirror mirror;
	bool mirrorOn = false;
	float mirrorPlane = 0.0f, mirrorEnds[2] = { 0.0f, 0.0f };
	float refl[16];

	memset(&mirror, 0, sizeof(mirror));
	MatIdentity(refl);

	if (o.shadowMirror && o.shadowRoom >= 0)
	{
		for (int m = 0; m < HDRenderMirrorCount(); m++)
		{
			HDMirror one;

			if (HDRenderMirrorInfo(m, &one) && one.group == o.shadowRoom)
			{
				mirror = one;
				mirrorOn = true;
			}
		}

		if (!mirrorOn || o.shadowRoom >= (int)level.rooms().size())
		{
			mirrorOn = false;
			printf("shadowmirror: room %d has no mirror here (%d in the level) - no mirror in "
				"the picture\n", o.shadowRoom, HDRenderMirrorCount());
		}
		else
		{
			const Tr5Room& rm = level.rooms()[(size_t)o.shadowRoom];
			const int k = 2 - mirror.axis;
			const float origin[3] = { (float)rm.originX, 0.0f, (float)rm.originZ };
			const float stand[3] = { o.shadowAt[0], 0.0f, o.shadowAt[1] };

			mirrorPlane = mirror.plane - origin[mirror.axis] - stand[mirror.axis];
			mirrorEnds[0] = mirror.mins[k] - origin[k] - stand[k];
			mirrorEnds[1] = mirror.maxs[k] - origin[k] - stand[k];
			refl[mirror.axis * 5] = -1.0f;
			refl[12 + mirror.axis] = 2.0f * mirrorPlane;

			printf("shadowmirror: room %d, the glass in %c = %.0f (%.1f in this picture), the "
				"copy at %s %c, its ends along the glass %.0f..%.0f here; %d pane, %d behind, "
				"%d reflected%s\n", o.shadowRoom, mirror.axis == 0 ? 'x' : 'z', mirror.plane,
				mirrorPlane, mirror.side < 0 ? "smaller" : "larger", mirror.axis == 0 ? 'x' : 'z',
				mirrorEnds[0], mirrorEnds[1], mirror.pane, mirror.behind, mirror.mirrored,
				o.shadowNoMirror ? " - its blocks left out (-shadownomirror)" : "");

			if (o.shadowMirrorView)
			{
				float mirrored[16];

				MatMul(refl, view, mirrored);
				memcpy(view, mirrored, sizeof(view));
				printf("shadowmirror: the camera reflected in the glass (-shadowmirrorview)\n");
			}

			/* Both sides of every room face: the reflected camera turns the
			 * winding round, and a culled face would then be the other one.
			 * Every mirror picture draws the same way, so the two views are
			 * pictures of the same faces. */
			HDRenderCullOpaque(0);
			printf("shadowmirror: the room's faces drawn double sided in every mirror picture\n");
		}
	}

	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)o.width * 0.5, (double)o.height * 0.5,
		a, b, &projection);

	printf("camera  : %.0f units away, yaw %.0f, pitch %.0f, persp %.1f at %ldx%ld\n",
		dist, o.yaw, o.pitch, persp, o.width, o.height);

	/* --- the joint matrices ----------------------------------------------- */
	/* The body: the very walk DrawAnimatingItem does, with the animation left
	 * out - which is the bind pose, and which is what lara_matrices holds
	 * when the game calls phd_PutPolygons for each of the fifteen. */
	float matrices[HD_LARA_NODES * 16];

	BuildNodeMatrices(body, view, o.turn, matrices);

	/* Where the joints really land in view space. The
	 * near zone is a radius about the camera measured in view space, so the
	 * one thing it rests on is that view space is CLASSIC UNITS with the
	 * camera at the origin. This line says so out loud for the two joints the
	 * cut is about; with -firstperson and the eye on the joint the head must
	 * read 16, which is exactly how far above it the eye sits. */
	{
		const float* h = &matrices[HD_LARA_HEAD_NODE * 16];
		const float* t = &matrices[HD_LARA_TORSO * 16];

		printf("viewspace: head joint %.0f/%.0f/%.0f (%.0f from the camera), "
			"torso joint %.0f/%.0f/%.0f (%.0f)\n", h[12], h[13], h[14],
			sqrt(h[12] * h[12] + h[13] * h[13] + h[14] * h[14]),
			t[12], t[13], t[14],
			sqrt(t[12] * t[12] + t[13] * t[13] + t[14] * t[14]));
	}

	/* The mirrored hand weights, and what folding them costs.
	 * The rule itself is the engine's - HDTrmFoldMirrored, the very call
	 * hd_model makes while it loads the same file - so what this prints is what
	 * the game does. What it adds is the price, which only a pose can say: the
	 * same vertex skinned twice through the node matrices built above, once
	 * with the file's own weights and once with the folded ones, and the
	 * distance between the two answers.
	 *
	 * It is the measurement the bar comes out of. In the bind pose the two
	 * hands sit beside each other and every weighting draws the same picture,
	 * so the pose has to have the arms APART - which is what
	 * "-lara-pose 9 50 40 -lara-pose 12 -50 40" is for. */
	if (o.handFold >= 0)
	{
		printf("\n-- the mirrored hand weights, bar %d per mille -------------\n",
			o.handFold);
		printf("%-26s %7s %7s %6s %5s %6s %6s %8s %8s\n", "model",
			"verts", "crossed", "folded", "tied", "renorm", "worst",
			"moved", "worst mv");

		for (int part = 0; part < HD_LARA_PART_COUNT; part++)
		{
			const char* model = HDLaraPartModel(outfit, part);

			if (!model || !HDLaraHasPart(part) ||
				HDLaraPartTwinSlot(part) < 0)
				continue;

			char path[512];
			int mod = -1;

			if (!HDModelFindFile(model, false, true, path, sizeof(path), &mod))
			{
				printf("%-26s %s\n", model, "no file - this build did not load it");
				continue;
			}

			HDTrmModel raw, folded;
			const char* trouble = "";

			if (!HDTrmLoad(path, &raw, &trouble))
			{
				printf("%-26s %s\n", model, trouble);
				continue;
			}

			if (!HDTrmLoad(path, &folded, &trouble))
			{
				HDTrmFree(&raw);
				printf("%-26s %s\n", model, trouble);
				continue;
			}

			int pairs = 0;
			const HDTrmMirrorPair* mirror = HDLaraHandMirror(&pairs);
			HDTrmFoldCounts counts;

			HDTrmFoldMirrored(&folded, mirror, pairs, o.handFold, &counts);

			/* The skinning, exactly as hd_model.hlsl does it: the weighted sum
			 * over the three bones of (p - bind[bone]) * M[bone]. The bind
			 * fold is in set_joint in the engine and written out here. */
			double moved = 0.0, worstMove = 0.0;
			int movedCount = 0;

			for (int v = 0; v < raw.vertices; v++)
			{
				double before[3] = { 0, 0, 0 }, after[3] = { 0, 0, 0 };
				bool differs = false;

				for (int b = 0; b < 3; b++)
					if (raw.vertex[v].bone[b] != folded.vertex[v].bone[b] ||
						raw.vertex[v].weight[b] != folded.vertex[v].weight[b])
						differs = true;

				if (!differs)
					continue;

				for (int which = 0; which < 2; which++)
				{
					const HDTrmVertex& in = which ? folded.vertex[v] : raw.vertex[v];
					double* out = which ? after : before;

					for (int b = 0; b < 3; b++)
					{
						const int node = in.bone[b];
						const double w = (double)in.weight[b] / 255.0;

						if (!in.weight[b] || node >= HD_LARA_NODES)
							continue;

						const float* m = &matrices[node * 16];
						const double px = in.x - body.bind[node][0];
						const double py = in.y - body.bind[node][1];
						const double pz = in.z - body.bind[node][2];

						for (int c = 0; c < 3; c++)
							out[c] += w * (px * m[0 * 4 + c] + py * m[1 * 4 + c] +
								pz * m[2 * 4 + c] + m[3 * 4 + c]);
					}
				}

				const double dx = after[0] - before[0];
				const double dy = after[1] - before[1];
				const double dz = after[2] - before[2];
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				moved += d;
				movedCount++;

				if (d > worstMove)
					worstMove = d;
			}

			printf("%-26s %7d %7d %6d %5d %6d %5d%s %8d %8.2f\n", model,
				raw.vertices, counts.crossed, counts.folded, counts.tied,
				counts.renormed, counts.worstPermille, "pm", movedCount,
				worstMove);

			if (movedCount)
				printf("%-26s   mean move %.2f units over the %d vertices the "
					"fold touched%s\n", "", moved / movedCount, movedCount,
					mod >= 0 ? ", out of a mod" : ", out of the install");

			HDTrmFree(&raw);
			HDTrmFree(&folded);
		}

		printf("(the pose is the one -lara-pose asked for; in the bind pose the "
			"two hands sit beside each other and nothing moves. `tied` is the "
			"crossed vertices whose two sides weigh exactly the same - they "
			"are part of `folded`, not an exception to it, and the only "
			"difference is that their far side is picked by the sign of their "
			"own x)\n");
	}

	/* The braid. In the game the six links come from hairs[0][n], which the
	 * simulation writes as ABSOLUTE world positions and angles - there is no
	 * mesh tree walk for the braid at all, which is why the classic HAIR
	 * object's own tree is a straight line along +z (measured: 0, 56, 112,
	 * 168, 224, 280 in every adult level, half that for young Lara's
	 * pigtails) and the .TRM is drawn in exactly that pose.
	 *
	 * A studio picture has no simulation, so the tool builds the chain the
	 * way HairControl would have it hang at rest: the whole braid turned out
	 * of its +z bind direction to point downwards and a little backwards, and
	 * each further link bent a few degrees more, so that the picture proves
	 * what it is meant to prove - the six bones move independently and the
	 * chain hangs together. The angles are the tool's, and only the tool's;
	 * the composite build takes the real ones out of hairs[0][n].
	 *
	 * The chain is built the way a chain is: link n's frame is a bend, then a
	 * step of one link's length along the previous link's own z, then the
	 * previous frame. */
	const double kBraidRest = -100.0;	// +z -> down and slightly behind her
	const double kBraidBend = -5.0;		// per link, so the six are visible

	float hairMatrices[HD_LARA_HAIR_NODES * 16];
	float chain[16];

	{
		float rest[16], anchor[16], base[16];

		/* The root is not the head joint.
		 * HairControl (game\hair.cpp) walks to the head bone and then translates by
		 * (-4, -4, -48) in the head's own frame before it writes hairs[0][0];
		 * that offset - up a little and 48 units behind the joint, which is
		 * the back of the skull - is where the braid hangs from. Only the two
		 * angles below are the tool's; the root is the game's own formula, so
		 * that a studio picture shows the same junction the game shows. */
		/* The root moved 15 units nearer the skull for an HD Lara, was
		 * withdrawn, then put back once the pack became a
		 * box - see HD_LARA_BRAID_ROOT_Z_HD for all three. This tool draws an
		 * HD Lara and nothing else, so the HD root is the default and
		 * -classicroot is the picture from before the HD root existed. */
		MatTranslate((float)HD_LARA_BRAID_ROOT_X, (float)HD_LARA_BRAID_ROOT_Y,
			(float)(o.classicBraidRoot ? HD_LARA_BRAID_ROOT_Z
				: HD_LARA_BRAID_ROOT_Z_HD), anchor);
		MatMul(anchor, &matrices[HD_LARA_HEAD_NODE * 16], base);

		MatRotX(kBraidRest, rest);
		MatMul(rest, base, chain);
	}

	memcpy(&hairMatrices[0], chain, sizeof(chain));

	for (int n = 1; n < HD_LARA_HAIR_NODES; n++)
	{
		const float length = haveHair
			? hair.bind[n][2] - hair.bind[n - 1][2]
			: 56.0f;

		float bend[16], step[16], tmp[16];

		MatRotX(kBraidBend, bend);
		MatTranslate(0.0f, 0.0f, length, step);
		MatMul(bend, step, tmp);
		MatMul(tmp, chain, &hairMatrices[n * 16]);
		memcpy(chain, &hairMatrices[n * 16], sizeof(chain));
	}

	/* The settled chain instead, when -braidrest asked for one. The six
	 * matrices are built the way specific\hd\
	 * hd_laradraw.cpp's braid_matrices builds them and not the way the studio
	 * chain above is: a translation to the link's own position, then the two
	 * angles HairControl derives from the chain, then the roll of the real-animation rule.
	 *
	 * The order is the engine's read through the transpose this file works in.
	 * phd_TranslateAbs, phd_RotY, phd_RotX, phd_RotZ builds M = W . Ry . Rx .
	 * Rz on a column vector; the same thing on a ROW vector is Rz then Rx then
	 * Ry then the view, because (A.B.C) transposed is C'.B'.A' - and each of
	 * MatRotY/X/Z here is already the transpose of the engine's own. */
	if (g_braidRestHave)
	{
		double yaw[6], pitch[6], eff[6];

		BraidAngles(g_braidRest, yaw, pitch);
		g_braidLockSeen = false;
		BraidRollFix(g_braidRest, yaw, eff, BRAID_ROLL_LOCK);

		printf("braid   : the SETTLED chain of regime %d, not a studio pose\n",
			g_braidRestWant);

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rz[16], rot[16], move[16], placed[16];
			float* out = &hairMatrices[n * 16];

			MatRotZ(eff[n] - yaw[n], rz);
			MatRotX(pitch[n], rx);
			MatRotY(yaw[n], ry);
			MatMul(rz, rx, rot);
			MatMul(rot, ry, rot);
			MatMul(rot, view, out);

			/* phd_TranslateAbs replaces the translation column and leaves the
			 * rotation where it is. */
			MatTranslate((float)g_braidRest[n][0], (float)g_braidRest[n][1],
				(float)g_braidRest[n][2], move);
			MatMul(move, view, placed);

			out[12] = placed[12];
			out[13] = placed[13];
			out[14] = placed[14];
		}
	}

	/* --- the two proofs of the seam ------------------------------------- */
	/* Offline twins of what specific\hd\hd_laradraw.cpp logs as HDLOG
	 * laracheck and HDLOG braidcheck on the first HD Lara frame of a level.
	 * The engine's run is the one that counts - it measures its own matrix
	 * stack and its own braid simulation; this one runs the same two
	 * questions with no game around it, which is what makes a wrong answer
	 * findable before the game is ever started. */
	if (o.check)
	{
		const float* pm = (const float*)&projection;
		double worstX = 0, worstY = 0;
		int used = 0, behind = 0;

		printf("\n-- laracheck: the joint origins, both roads --------------------\n");
		printf("%5s %-9s %12s %12s %12s %10s %10s %8s %8s\n", "node", "name",
			"view x", "view y", "view z", "classic x", "device x", "dx", "dy");

		static const char* kName[HD_LARA_NODES] =
		{
			"hips", "l thigh", "l shin", "l foot", "r thigh", "r shin",
			"r foot", "torso", "r inarm", "r outarm", "r hand", "l inarm",
			"l outarm", "l hand", "head"
		};

		for (int node = 0; node < HD_LARA_NODES; node++)
		{
			const float* m = &matrices[node * 16];
			const double vx = m[12], vy = m[13], vz = m[14];

			if (vz < 20.0)
			{
				behind++;
				continue;
			}

			const double zv = persp / vz;
			const double classicX = vx * zv + (double)o.width * 0.5;
			const double classicY = vy * zv + (double)o.height * 0.5;

			const double cxx = vx * pm[0] + vy * pm[4] + vz * pm[8] + pm[12];
			const double cyy = vx * pm[1] + vy * pm[5] + vz * pm[9] + pm[13];
			const double cww = vx * pm[3] + vy * pm[7] + vz * pm[11] + pm[15];

			if (cww <= 0.0)
			{
				behind++;
				continue;
			}

			const double deviceX = (cxx / cww + 1.0) * 0.5 * (double)o.width;
			const double deviceY = (1.0 - cyy / cww) * 0.5 * (double)o.height;
			const double dx = fabs(deviceX - classicX);
			const double dy = fabs(deviceY - classicY);

			if (dx > worstX) worstX = dx;
			if (dy > worstY) worstY = dy;

			used++;

			printf("%5d %-9s %12.1f %12.1f %12.1f %10.3f %10.3f %8.5f %8.5f\n",
				node, kName[node], vx, vy, vz, classicX, deviceX, dx, dy);
		}

		printf("\n%d joints compared, %d behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", used, behind, worstX, worstY);
		printf("%s\n", worstX < 1.0 && worstY < 1.0
			? "PASS - both roads put every joint origin in the same pixel"
			: "FAIL - the projection and the classic transform disagree");

		/* The braid's chain rule, which is the one thing about this seam that
		 * a still picture cannot show. TR5 builds a matrix for links 1, 3 and
		 * 5 only (game\hair.cpp, DrawHair) and draws 0, 2 and 4 as skinned
		 * connectors out of stashed vertices; HAIR_OG.TRM has six bones, so
		 * the seam has to build all six - and nothing in the game has ever
		 * used the angles of the three it adds.
		 *
		 * They are usable, and this is the proof. HairControl derives every
		 * link's POSITION by putting the link before it at its own position
		 * and angles - phd_SetTrans, then phd_RotYXZ(y_rot, x_rot, 0), which
		 * is phd_RotY followed by phd_RotX (specific\3dmath.cpp) - and
		 * stepping one bone offset along that frame. The seam builds the same
		 * frame with phd_TranslateAbs, phd_RotY, phd_RotX, in that order. So
		 * frame(n) stepped by the offset must land on position(n + 1).
		 *
		 * The chain below is built by the FIRST of those two rules and the
		 * frames by the SECOND, written apart, so that a swapped rotation
		 * order or a translation that accumulates instead of being replaced
		 * comes out as a gap and not as agreement. */
		printf("\n-- braidcheck: the six links' chain rule -----------------------\n");

		double yawChain[HD_LARA_HAIR_NODES];
		double pitchChain[HD_LARA_HAIR_NODES];
		float chainPos[HD_LARA_HAIR_NODES][3];
		float step[HD_LARA_HAIR_NODES][3];

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			// Nothing round or symmetric, so that no sign can cancel itself.
			yawChain[n] = 25.0 - 11.0 * n;
			pitchChain[n] = -97.0 - 6.0 * n;

			step[n][0] = 0.0f;
			step[n][1] = 0.0f;
			step[n][2] = haveHair && n + 1 < hair.nodes
				? hair.bind[n + 1][2] - hair.bind[n][2]
				: 56.0f;
		}

		chainPos[0][0] = 0.0f;
		chainPos[0][1] = 0.0f;
		chainPos[0][2] = 0.0f;

		for (int n = 0; n + 1 < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rot[16];

			MatRotY(yawChain[n], ry);
			MatRotX(pitchChain[n], rx);
			MatMul(ry, rx, rot);		// RotY then RotX - phd_RotYXZ(y, x, 0)

			for (int a = 0; a < 3; a++)
				chainPos[n + 1][a] = chainPos[n][a] +
					step[n][0] * rot[0 * 4 + a] +
					step[n][1] * rot[1 * 4 + a] +
					step[n][2] * rot[2 * 4 + a];
		}

		float frame[HD_LARA_HAIR_NODES][16];

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
		{
			float ry[16], rx[16], rot[16], move[16], placed[16];

			MatRotY(yawChain[n], ry);
			MatRotX(pitchChain[n], rx);
			MatMul(ry, rx, rot);
			MatMul(rot, view, frame[n]);

			/* phd_TranslateAbs REPLACES the translation column and leaves the
			 * rotation alone, so the frame's origin is the link's own view
			 * position and not the rotated camera's. */
			MatTranslate(chainPos[n][0], chainPos[n][1], chainPos[n][2], move);
			MatMul(move, view, placed);

			frame[n][12] = placed[12];
			frame[n][13] = placed[13];
			frame[n][14] = placed[14];
		}

		printf("%5s %8s %12s %12s %12s %10s\n", "link", "step",
			"stepped x", "stepped y", "stepped z", "gap");

		double worstGap = 0;

		for (int n = 0; n + 1 < HD_LARA_HAIR_NODES; n++)
		{
			const float* m = frame[n];
			const double px = step[n][0] * m[0] + step[n][1] * m[4] +
				step[n][2] * m[8] + m[12];
			const double py = step[n][0] * m[1] + step[n][1] * m[5] +
				step[n][2] * m[9] + m[13];
			const double pz = step[n][0] * m[2] + step[n][1] * m[6] +
				step[n][2] * m[10] + m[14];

			const double dx = px - frame[n + 1][12];
			const double dy = py - frame[n + 1][13];
			const double dz = pz - frame[n + 1][14];
			const double gap = sqrt(dx * dx + dy * dy + dz * dz);

			if (gap > worstGap)
				worstGap = gap;

			printf("%5d %8.0f %12.2f %12.2f %12.2f %10.5f\n", n,
				step[n][2], px, py, pz, gap);
		}

		printf("\nworst gap %.5f units over %d joints\n", worstGap,
			HD_LARA_HAIR_NODES - 1);
		printf("%s\n", worstGap < 0.01
			? "PASS - the six rebuilt frames are the chain HairControl walked"
			: "FAIL - a link's frame does not reach the next link");
	}

	float light[HD_LARA_NODES * 3];

	for (int i = 0; i < HD_LARA_NODES * 3; i++)
		light[i] = (float)o.shadeRgb[i % 3];

	/* The X-ray look - -xray, or an outfit that is the X-ray body: the set's
	 * tone on the light it is handed, the ambient and every -light alike, as
	 * specific\hd\hd_laradraw.cpp hands it (hd_lara.h, HDLaraXrayTone). */
	HDObjLight tonedLights[HD_MAX_OBJ_LIGHTS];
	const bool xrayLit = xrayDraw || HDLaraOutfitIsXray(outfit);

	if (xrayLit)
	{
		float level = 1.0f, hue[3] = { 1.0f, 1.0f, 1.0f };

		HDLaraXrayTone(&level, hue);

		for (int i = 0; i < HD_LARA_NODES * 3; i++)
			light[i] *= level * hue[i % 3];

		for (int i = 0; i < o.lightCount && i < HD_MAX_OBJ_LIGHTS; i++)
		{
			tonedLights[i] = o.lights[i];
			tonedLights[i].r *= level * hue[0];
			tonedLights[i].g *= level * hue[1];
			tonedLights[i].b *= level * hue[2];
		}

		printf("xray    : the set's tone %.2f/%.2f,%.2f,%.2f on its light, "
			"every part lit as the hull\n", level, hue[0], hue[1], hue[2]);
	}

	HDModelLighting lighting;

	lighting.lights = o.lightCount ? (xrayLit ? tonedLights : o.lights) : 0;
	lighting.lightCount = o.lightCount;
	lighting.sunScale = 0.75f;
	lighting.specStrength = 0.0f;
	lighting.specPower = 1.0f;

	/* The sheen, part by part, out of the same table
	 * the engine reads - so a picture from here is the picture the game
	 * draws. -nosheen sets every one of them to zero, which is the before
	 * picture. */
	printf("sheen   : ");

	for (int part = 0; part < HD_LARA_PART_COUNT; part++)
	{
		float st = 0.0f, pw = 1.0f, held = 0.0f, hp = 1.0f;

		HDLaraPartSheen(part, &st, &pw);
		HDLaraHeldSheen(part, &held, &hp);

		if (!HDLaraHasPart(part) || st <= 0.0f)
			continue;

		printf("%s %.2f/%.0f", HDLaraPartLabel(part), o.noSheen ? 0.0f : st, pw);

		/* And what is in the hand, on its own pages. */
		if (held > 0.0f && HDModelPageSheenCount(HDLaraPartSlot(part)))
			printf(" [held %.2f/%.0f on %d page(s)]", o.noSheen ? 0.0f : held,
				hp, HDModelPageSheenCount(HDLaraPartSlot(part)));

		printf("  ");
	}

	printf("%s\n", o.noSheen ? "  (-nosheen: all off)" : "");

	/* -onlypart draws one part and leaves the rest
	 * out, which is how a question about one part is answered by a picture
	 * of that part. A lambda rather than six copies of the same test. */
	const auto draws = [&o](int part) { return o.onlyPart < 0 || o.onlyPart == part; };

	/* -softtest: the chest's motion at a fixed offset, through the engine's
	 * own constants on this picture's torso matrix and to the parts the
	 * engine hands it to. Without it every draw gets none, as in the game
	 * with the option off. */
	HDModelSoft softConstants = {};

	if (o.softTest)
	{
		const float offset[2][3] =
		{
			{ o.softOffset[0], o.softOffset[1], o.softOffset[2] },
			{ o.softOffset[0], o.softOffset[1], o.softOffset[2] }
		};

		HDSoftConstants(HDLaraSoftRegion(), offset, matrices + HD_LARA_TORSO * 16,
			HD_LARA_TORSO, softConstants.constants);
		printf("softtest: (%.1f, %.1f, %.1f) in the torso's axes, both sides%s\n",
			o.softOffset[0], o.softOffset[1], o.softOffset[2],
			HDLaraSoftRegion()->ok ? "" : " - the region is not ok, nothing moves");
	}

	const auto soft = [&o, &softConstants, xrayDraw](int part)
		-> const HDModelSoft*
	{
		return o.softTest && !xrayDraw && HDLaraPartSoft(part)
			? &softConstants : 0;
	};

	/* -shadowmap, -shadowcast, -shadowdevice: the projected shadow's box for
	 * this pose, through the engine's own hd_shadow.h. The picture's view is
	 * the world-to-view, so her model space is the world, y down; her nodes
	 * go back into it out of the very matrices drawn, as hd_laradraw.cpp
	 * takes them out of the frame's. */
	const bool shadowAny = o.shadowMap || o.shadowCast || o.shadowDevice || o.shadowReceive ||
		o.shadowTime > 0 || o.shadowOthers;
	float shadowClip[16];
	HDShadowBox shadowBox;
	float R[9], t[3];

	memset(shadowClip, 0, sizeof(shadowClip));
	memset(&shadowBox, 0, sizeof(shadowBox));

	if (shadowAny)
	{
		float nodesW[HD_LARA_NODES * 3];

		for (int i = 0; i < 3; i++)
		{
			for (int j = 0; j < 3; j++)
				R[i * 3 + j] = view[j * 4 + i];

			t[i] = view[12 + i];
		}

		for (int n = 0; n < HD_LARA_NODES; n++)
			HDShadowViewToWorld(R, t, matrices + n * 16 + 12, nodesW + n * 3);

		float dir[3] = { o.shadowDir[0], o.shadowDir[1], o.shadowDir[2] };

		hd_shadow_unit(dir);

		const bool held = HDShadowLimit(dir);

		HDShadowBoxMake(&shadowBox, dir, nodesW, HD_LARA_NODES);
		HDShadowCastMatrix(&shadowBox, R, t, shadowClip);

		printf("shadow  : light (%.3f, %.3f, %.3f), %.1f degrees off straight down%s\n",
			dir[0], dir[1], dir[2], HDShadowSlant(dir),
			held ? " (held to the slant limit)" : "");
		printf("          box centre (%.1f, %.1f, %.1f), her nodes y %.1f (lowest) .. %.1f\n",
			shadowBox.centre[0], shadowBox.centre[1], shadowBox.centre[2], shadowBox.lowest,
			shadowBox.highest);
		printf("          map u along right (%.3f, %.3f, %.3f), picture top along up "
			"(%.3f, %.3f, %.3f)\n", shadowBox.right[0], shadowBox.right[1],
			shadowBox.right[2], shadowBox.up[0], shadowBox.up[1], shadowBox.up[2]);
	}

	/* Every part the engine marks for the map, with the mask hd_laradraw.cpp
	 * gives it: the frame's own (-meshbits), first person not taken off, the
	 * face and the braid in first person too, no neck lid. */
	const auto castParts = [&](int lipsT, int lipsF) -> int
	{
		const unsigned int full = o.nodeMask;
		int n = 0;

		if (draws(HD_LARA_PART_BODY) && HDModelCastDraw(HDLaraPartSlot(HD_LARA_PART_BODY),
			false, matrices, HD_LARA_NODES, full & HDLaraBodyMask(), -1, -1,
			soft(HD_LARA_PART_BODY)))
			n++;

		if (draws(HD_LARA_PART_HEAD) && HDLaraHasPart(HD_LARA_PART_HEAD) &&
			HDModelCastDraw(HDLaraPartSlot(HD_LARA_PART_HEAD), false, matrices,
				HD_LARA_NODES, full, -1, -1, soft(HD_LARA_PART_HEAD)))
			n++;

		if (draws(HD_LARA_PART_FACE) && HDModelCastDraw(HDLaraPartSlot(HD_LARA_PART_FACE),
			false, matrices, HD_LARA_NODES, full, lipsT, lipsF, soft(HD_LARA_PART_FACE)))
			n++;

		if (draws(HD_LARA_PART_HOLSTER_RIG) && HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG) &&
			HDModelCastDraw(HDLaraPartSlot(HD_LARA_PART_HOLSTER_RIG), false, matrices,
				HD_LARA_NODES, HDLaraPartCoverage(HD_LARA_PART_HOLSTER_RIG) & full, -1, -1,
				soft(HD_LARA_PART_HOLSTER_RIG)))
			n++;

		for (int i = 0; i < pieces; i++)
		{
			if (draws(list[i].part) && HDModelCastDraw(HDLaraPartSlot(list[i].part), false,
				matrices, HD_LARA_NODES, list[i].nodeMask & full))
				n++;

			if (draws(list[i].part) && HDLaraHasPartTwin(list[i].part) &&
				HDModelCastDraw(HDLaraPartTwinSlot(list[i].part), false, matrices,
					HD_LARA_NODES, list[i].nodeMask & full))
				n++;
		}

		if (draws(HD_LARA_PART_BRAID) && HDLaraHasPart(HD_LARA_PART_BRAID) &&
			HDModelCastDraw(HDLaraPartSlot(HD_LARA_PART_BRAID), false, hairMatrices,
				HD_LARA_HAIR_NODES, ~0u))
			n++;

		return n;
	};

	/* The cast pass exactly as hd_level.cpp runs it, inside an open bracket.
	 * -1 when the device refused the map or its shaders. */
	const auto shadowPass = [&](int lipsT, int lipsF) -> int
	{
		if (!HDRenderShadowTargetReady() || !HDModelCastReady() || !HDRenderShadowCastBegin())
			return -1;

		HDModelCastBegin(shadowClip);

		// -shadowempty: the map cleared and nothing cast into it.
		const int n = o.shadowEmpty ? 0 : castParts(lipsT, lipsF);

		HDModelCastEnd();
		HDRenderShadowCastEnd();
		return n;
	};

	int shadowCastDrawn = 0;

	/* -shadowroom: one room group of the level (-withrooms) under her feet.
	 * Her model space stays the world of this picture, y down; the room is
	 * moved so that its floor at (x, z), room local, meets her soles - the
	 * deepest point of her composite. The floor is the classic level's own,
	 * the four corners of that sector interpolated, so on a slope it is the
	 * slope's height there. */
	bool roomHas = false;
	D3DMATRIX roomWorld;
	float roomFloor = 0.0f;

	memset(&roomWorld, 0, sizeof(roomWorld));

	if (o.shadowRoom >= 0)
	{
		float gmin[3], gmax[3];
		const auto& rooms = level.rooms();

		if (o.shadowRoom >= HDRenderGroupCount() || o.shadowRoom >= (int)rooms.size() ||
			!HDRenderGroupBounds(o.shadowRoom, gmin, gmax))
			printf("shadowroom: room %d has no HD group here (-withrooms <LEVEL>?) - no "
				"room in the picture\n", o.shadowRoom);
		else
		{
			const Tr5Room& rm = rooms[(size_t)o.shadowRoom];
			const int sx = (int)floorf(o.shadowAt[0] / 1024.0f);
			const int sz = (int)floorf(o.shadowAt[1] / 1024.0f);
			const float fx = o.shadowAt[0] / 1024.0f - (float)sx;
			const float fz = o.shadowAt[1] / 1024.0f - (float)sz;
			long c00 = 0, c10 = 0, c01 = 0, c11 = 0;

			if (level.cornerFloor(o.shadowRoom, sx, sz, 0, 0, c00) &&
				level.cornerFloor(o.shadowRoom, sx, sz, 1024, 0, c10) &&
				level.cornerFloor(o.shadowRoom, sx, sz, 0, 1024, c01) &&
				level.cornerFloor(o.shadowRoom, sx, sz, 1024, 1024, c11))
			{
				roomFloor = (float)c00 * (1.0f - fx) * (1.0f - fz) + (float)c10 * fx * (1.0f - fz) +
					(float)c01 * (1.0f - fx) * fz + (float)c11 * fx * fz;
				roomHas = true;
			}
			else
				printf("shadowroom: (%.0f, %.0f) is not a floor of room %d - no room in "
					"the picture\n", o.shadowAt[0], o.shadowAt[1], o.shadowRoom);

			printf("shadowroom: room %d, %d x %d sectors, group box x %.0f..%.0f y %.0f..%.0f "
				"z %.0f..%.0f, origin x %ld z %ld\n", o.shadowRoom,
				rm.numXSectors, rm.numZSectors, gmin[0], gmax[0], gmin[1],
				gmax[1], gmin[2], gmax[2], (long)rm.originX,
				(long)rm.originZ);
			printf("            the floor of each sector, units (x across, z down; # a wall):\n");

			for (int z = 0; z < rm.numZSectors; z++)
			{
				printf("            z%2d ", z);

				for (int x = 0; x < rm.numXSectors; x++)
				{
					long y = 0;

					if (level.cornerFloor(o.shadowRoom, x, z, 0, 0, y))
						printf(" %6ld", y);
					else
						printf("      #");
				}

				printf("\n");
			}

			if (roomHas)
			{
				roomWorld._11 = roomWorld._22 = roomWorld._33 = roomWorld._44 = 1.0f;
				roomWorld._41 = -o.shadowAt[0];
				roomWorld._42 = maxs[1] - roomFloor;
				roomWorld._43 = -o.shadowAt[1];
				printf("            she stands at (%.0f, %.0f) on the floor at y %.1f; her soles "
					"at y %.1f of her own space\n", o.shadowAt[0], o.shadowAt[1], roomFloor,
					maxs[1]);
			}
		}
	}

	/* -shadowwith: each further group moved by its room's origin against the
	 * seed room's, as the level places them; y is the world's in both. */
	std::vector<std::pair<int, D3DMATRIX> > withRooms;

	for (size_t w = 0; roomHas && w < g_probe_with.size(); w++)
	{
		const int g = g_probe_with[w];
		float wmin[3], wmax[3];

		if (g < 0 || g >= HDRenderGroupCount() ||
			g >= (int)level.rooms().size() ||
			!HDRenderGroupBounds(g, wmin, wmax))
		{
			printf("shadowwith: room %d has no HD group here - left out\n", g);
			continue;
		}

		D3DMATRIX world = roomWorld;
		const Tr5Room& seed = level.rooms()[(size_t)o.shadowRoom];
		const Tr5Room& beside = level.rooms()[(size_t)g];

		world._41 += (float)(beside.originX - seed.originX);
		world._43 += (float)(beside.originZ - seed.originZ);
		withRooms.push_back(std::make_pair(g, world));
	}

	if (!withRooms.empty())
		printf("shadowwith: %d room groups beside room %d\n",
			(int)withRooms.size(),
			o.shadowRoom);

	/* -shadowcreature: each creature in its slot's first animation frame
	 * (ReadFramePose, the walk the engine measures with, plus the frame's own
	 * offset, which puts the item's origin at its feet), turned by its yaw
	 * and stood with that origin on the floor: the room's floor at its room
	 * local (x, z) under -shadowroom, her soles' height at (x, z) of her own
	 * space without it. Node matrices model space to view, as the engine's
	 * walk hands them to HDModelDraw. */
	struct Placed
	{
		int slot;
		int nodes;
		std::vector<float> matrices;
		float place[3];
		bool blob;	// -shadowcreature: cast into the second map; -shadowitem: not
		bool unseen;	// -shadowunseen: a creature cast and drawn in no pass
		int cull;	// the side drawn, HD_MODEL_CULL_*
	};
	std::vector<Placed> placed;

	for (size_t c = 0; c < creatureSlot.size(); c++)
	{
		const ClassicSlot& tree = creatureTree[c];
		const LaraOptions::Creature* ask = &o.creatures[creatureAsk[c]];
		float px = ask->at[0], py = maxs[1], pz = ask->at[1];

		if (roomHas)
		{
			const int sx = (int)floorf(ask->at[0] / 1024.0f);
			const int sz = (int)floorf(ask->at[1] / 1024.0f);
			const float fx = ask->at[0] / 1024.0f - (float)sx;
			const float fz = ask->at[1] / 1024.0f - (float)sz;
			long c00 = 0, c10 = 0, c01 = 0, c11 = 0;

			if (!level.cornerFloor(o.shadowRoom, sx, sz, 0, 0, c00) ||
				!level.cornerFloor(o.shadowRoom, sx, sz, 1024, 0, c10) ||
				!level.cornerFloor(o.shadowRoom, sx, sz, 0, 1024, c01) ||
				!level.cornerFloor(o.shadowRoom, sx, sz, 1024, 1024, c11))
			{
				printf("creature: (%.0f, %.0f) is not a floor of room %d - %s left out\n",
					ask->at[0], ask->at[1], o.shadowRoom, ask->slot.c_str());
				continue;
			}

			const float floorAt = (float)c00 * (1.0f - fx) * (1.0f - fz) +
				(float)c10 * fx * (1.0f - fz) + (float)c01 * (1.0f - fx) * fz +
				(float)c11 * fx * fz;

			px = ask->at[0] + roomWorld._41;
			py = floorAt + roomWorld._42;
			pz = ask->at[1] + roomWorld._43;
		}

		std::vector<float> pose;
		Tr5Level::MoveableInfo info;
		float offset[3] = { 0.0f, 0.0f, 0.0f };

		SlotAnim keyed;

		if (o.itemAnim >= 0 && ReadAnimPose(level, tree, o.itemAnim, o.itemKey,
			pose) && ReadSlotAnim(level, tree.slot, o.itemAnim, keyed, 0))
			for (int i = 0; i < 3; i++)
				offset[i] = (float)level.debugReadFrameShort(
					(long long)(keyed.frameOffset / 2) +
					(long long)o.itemKey * keyed.size + 6 + i);
		else if (ReadFramePose(level, tree, pose) &&
			level.moveableInfo(tree.slot, info))
			for (int i = 0; i < 3; i++)
				offset[i] = (float)level.debugReadFrameShort(
					(long long)(info.frameOffset / 2) + 6 + i);

		// Toff * RotY * Tpos * view: a row vector through the frame's offset,
		// the item's turn, its place, and the camera.
		float toff[16], rot[16], tpos[16], a[16], b[16], itemView[16];

		MatTranslate(offset[0], offset[1], offset[2], toff);
		MatRotY(ask->yaw, rot);
		MatTranslate(px, py, pz, tpos);
		MatMul(toff, rot, a);
		MatMul(a, tpos, b);
		MatMul(b, view, itemView);

		Placed one;

		one.slot = tree.slot;
		one.nodes = tree.nodes;
		one.matrices.assign((size_t)tree.nodes * 16, 0.0f);
		one.place[0] = px;
		one.place[1] = py;
		one.place[2] = pz;
		one.blob = ask->blob;
		one.unseen = ask->blob && o.shadowUnseen;
		one.cull = o.itemCull >= 0 ? o.itemCull : HD_MODEL_CULL_NONE;
		BuildNodeMatrices(tree, itemView, 0, &one.matrices[0], pose.empty() ? 0 : &pose[0]);

		/* The pocket, as HDObjectEnd draws a sliding door: one-sided. The
		 * picture holds no reflection of an item, so the outside mode. */
		const bool pocket = !o.noPocket && ToolPocketOf(level, tree.slot);

		if (o.itemCull < 0 && HDPocketSide(pocket, false) == HD_POCKET_OUTSIDE)
			one.cull = HD_MODEL_CULL_BACK;

		if (o.itemPush != 0.0)
			for (size_t m = 0; m < one.matrices.size(); m++)
				if (m % 4 != 3)
					one.matrices[m] *= (float)(1.0 + o.itemPush);
		placed.push_back(one);

		printf("%s: %s (slot %d, %d nodes%s) at (%.0f, %.0f, %.0f) of her space, turned "
			"%.0f, frame offset (%.0f, %.0f, %.0f), HD model %s%s\n",
			one.unseen ? "creature (unseen: cast, drawn in no pass)"
			: ask->blob ? "creature" : "item (no blob)",
			HDSlotName(tree.slot) ? HDSlotName(tree.slot) : "?", tree.slot, tree.nodes,
			pose.empty() ? ", bind pose" : "", px, py, pz, ask->yaw, offset[0], offset[1],
			offset[2], HDModelHas(tree.slot) ? "loaded" : "MISSING",
			pocket ? ", pocket" : "");
	}

	/* -shadowmirror: her reflection and the creatures', as game\mirror.cpp
	 * draws them - every node matrix through the world's reflection in the
	 * glass (node * view^-1 * Refl * view), which lands a reflected body
	 * point at the view position of its image in the copy - and the three
	 * planes the reflection is clipped with in view space, as
	 * MirrorClipPlanes gives them: the copy's side of the glass and the
	 * copy's two ends along it. */
	float reflMatrices[HD_LARA_NODES * 16];
	float reflHair[HD_LARA_HAIR_NODES * 16];
	std::vector<std::vector<float> > reflPlaced;
	float mirrorClip[12];
	float viewR[9], viewT[3];

	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			viewR[i * 3 + j] = view[j * 4 + i];

		viewT[i] = view[12 + i];
	}

	if (mirrorOn)
	{
		// view^-1: the transposed rotation and the translation taken back.
		float inv[16], a[16], q[16];

		MatIdentity(inv);

		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				inv[i * 4 + j] = view[j * 4 + i];

		for (int j = 0; j < 3; j++)
			inv[12 + j] = -(view[12] * inv[0 * 4 + j] + view[13] * inv[1 * 4 + j] +
				view[14] * inv[2 * 4 + j]);

		MatMul(inv, refl, a);
		MatMul(a, view, q);

		for (int n = 0; n < HD_LARA_NODES; n++)
			MatMul(matrices + n * 16, q, reflMatrices + n * 16);

		for (int n = 0; n < HD_LARA_HAIR_NODES; n++)
			MatMul(hairMatrices + n * 16, q, reflHair + n * 16);

		reflPlaced.resize(placed.size());

		for (size_t c = 0; c < placed.size(); c++)
		{
			reflPlaced[c].assign(placed[c].matrices.size(), 0.0f);

			for (int n = 0; n < placed[c].nodes; n++)
				MatMul(&placed[c].matrices[(size_t)n * 16], q, &reflPlaced[c][(size_t)n * 16]);
		}

		/* A world plane n.p + d >= 0 in view space: (R n, d - (R n).t). The
		 * copy's ends along the glass: k >= its low end, k <= its high end. */
		const int k = 2 - mirror.axis;
		const float world[2][4] = { { 0, 0, 0, -mirrorEnds[0] }, { 0, 0, 0, mirrorEnds[1] } };

		HDShadowMirrorPlane(viewR, viewT, mirror.axis, mirrorPlane, mirror.side, mirrorClip);

		for (int e = 0; e < 2; e++)
		{
			float n[3] = { 0.0f, 0.0f, 0.0f };
			float* out = mirrorClip + 4 * (e + 1);

			n[k] = e == 0 ? 1.0f : -1.0f;
			out[3] = world[e][3];

			for (int i = 0; i < 3; i++)
			{
				out[i] = viewR[i * 3 + 0] * n[0] + viewR[i * 3 + 1] * n[1] + viewR[i * 3 + 2] * n[2];
				out[3] -= out[i] * viewT[i];
			}
		}
	}

	/* The second map's box round hers and its cast matrix, as hd_laradraw.cpp
	 * makes them; which creatures stand inside it, as hd_object.cpp asks
	 * (HDShadowBoxHolds on the item's place with OTHERS_MARGIN). */
	HDShadowBox othersBox;
	float othersClip[16];

	memset(&othersBox, 0, sizeof(othersBox));
	memset(othersClip, 0, sizeof(othersClip));

	if (shadowAny && !placed.empty())
	{
		HDShadowBoxAround(&othersBox, &shadowBox, HD_SHADOW_OTHERS_BOX, HD_SHADOW_OTHERS_DEPTH,
			HD_SHADOW_OTHERS_MAP);

		/* -shadowalong: the second box moved along the light, which puts
		 * the creatures where they would stand that much nearer her far
		 * side (a negative move: her near side) - the picture of a creature
		 * crossing the box's side along the light, with the creature and
		 * its floor still in view. */
		if (o.shadowAlong != 0.0f)
		{
			for (int i = 0; i < 3; i++)
			{
				othersBox.centre[i] -= o.shadowAlong * othersBox.fwd[i];
				othersBox.mid[i] -= o.shadowAlong * othersBox.fwd[i];
			}

			printf("shadowalong: the second box moved %.0f units against the light\n",
				o.shadowAlong);
		}

		HDShadowCastMatrix(&othersBox, R, t, othersClip);

		for (size_t c = 0; c < placed.size(); c++)
		{
			float uvd[3];

			HDShadowMapUVD(&othersBox, placed[c].place, uvd);
			printf("creature: %s in the second box at u %.4f v %.4f depth %.4f - %s\n",
				HDSlotName(placed[c].slot) ? HDSlotName(placed[c].slot) : "?", uvd[0], uvd[1],
				uvd[2], !placed[c].blob ? "no blob, casts nothing"
				: HDShadowBoxHolds(&othersBox, placed[c].place, HD_SHADOW_OTHERS_MARGIN)
				? "casts"
				: "outside, casts nothing");
		}
	}

	/* The second map's cast pass as hd_level.cpp runs it (ShadowOthersPass):
	 * the creatures inside the second box, through the engine's cast draw,
	 * into the second target. 0 with none inside, and then nothing is drawn;
	 * -1 when the device refused the map or its shaders. */
	/* Whether the last othersPass drew the second map - the bracket opened,
	 * as hd_level.cpp's g_others_cast_now - which makes receivePass the
	 * two-map receive. */
	bool othersDrawn = false;

	const auto othersPass = [&]() -> int
	{
		int inside = 0;

		othersDrawn = false;

		for (size_t c = 0; c < placed.size(); c++)
			inside += placed[c].blob &&
				HDShadowBoxHolds(&othersBox, placed[c].place, HD_SHADOW_OTHERS_MARGIN) ? 1 : 0;

		if (!inside)
			return 0;

		if (!HDRenderShadowOthersReady() || !HDModelCastReady() ||
			!HDRenderShadowOthersCastBegin())
			return -1;

		othersDrawn = true;

		HDModelCastBegin(othersClip);

		int n = 0;

		for (size_t c = 0; c < placed.size(); c++)
		{
			if (!placed[c].blob ||
				!HDShadowBoxHolds(&othersBox, placed[c].place, HD_SHADOW_OTHERS_MARGIN))
				continue;

			/* The creature's place beside every texel of it, as
			 * HDObjectCastOthersFrame writes an item's. */
			float uvd[3];

			HDShadowMapUVD(&othersBox, placed[c].place, uvd);
			HDRenderShadowPlace(uvd[2]);

			if (HDModelCastDraw(placed[c].slot, false, &placed[c].matrices[0], placed[c].nodes,
				~0u, -1, -1, 0))
				n++;

			if (HDModelHasHead(placed[c].slot) && HDModelCastDraw(placed[c].slot, true,
				&placed[c].matrices[0], placed[c].nodes, ~0u, -1, -1, 0))
				n++;
		}

		HDModelCastEnd();
		HDRenderShadowCastEnd();
		return n;
	};

	/* The receive pass as hd_level.cpp runs it, on the one room: the
	 * constants out of this picture's box and view at the full strength of
	 * one dominant light - and, when othersPass has just drawn the second
	 * map, the two-map receive with the second box's constants, as
	 * hd_level.cpp chooses it. -shadownoguard takes the stencil guard away
	 * again inside the pass, for the count of pixels it saves from a second
	 * darkening. -1 when the receive could not begin. `mirrorCalls` comes back
	 * with the mirror's draw calls, -shadowmirror's. */
	int mirrorCalls = 0;

	const auto receivePass = [&]() -> int
	{
		float regs[HD_SHADOW_RECEIVE_COUNT * 4];
		float oregs[HD_SHADOW_RECEIVE2_COUNT * 4];

		HDShadowReceiveConstants(&shadowBox, R, t, HD_SHADOW_DARK *
			g_probe_presence, regs);

		if (othersDrawn)
			HDShadowReceiveOthersConstants(&othersBox, R, t, oregs);

		if (!(othersDrawn ? HDRenderShadowReceive2Begin(regs, oregs)
			: HDRenderShadowReceiveBegin(regs)))
			return -1;

		if (o.shadowNoGuard)
			g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

		/* -shadowmirror: the mirror's group first with the mirror's variant
		 * and blocks, as hd_level.cpp's receive pass draws it - unless
		 * -shadownomirror leaves the blocks out. */
		if (mirrorOn && !o.shadowNoMirror)
		{
			float mh[HD_SHADOW_RECEIVE_COUNT * 4], mo[HD_SHADOW_RECEIVE2_COUNT * 4];
			float glass[4], Rm[9], tm[3];

			HDShadowMirrorView(R, t, mirror.axis, mirrorPlane, Rm, tm);
			HDShadowReceiveConstants(&shadowBox, Rm, tm, HD_SHADOW_DARK *
				g_probe_presence, mh);

			if (othersDrawn)
				HDShadowReceiveOthersConstants(&othersBox, Rm, tm, mo);
			else
			{
				memset(mo, 0, sizeof(mo));
				HDShadowReceiveWithoutOthers(mo);
			}

			HDShadowMirrorPlane(R, t, mirror.axis, mirrorPlane, mirror.side, glass);

			if (!HDShadowMirrorFaces(R, t, mirror.axis, mirrorPlane, mirror.side))
				printf("shadowmirror: the camera stands behind the glass - no mirror's draw\n");
			else if (HDRenderShadowReceiveMirrorBegin(mh, mo, glass))
			{
				/* -shadowmirrormask: the pixels the mirror's draw leaves - the
				 * copy - marked in stencil bit 2 as well as the guard's bit 0. */
				if (o.shadowMirrorMask)
					g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 1 | 4);

				HDRenderRoomShadow(o.shadowRoom, &roomWorld, 0, HD_WINDOW_ALL);

				if (o.shadowMirrorMask)
					g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 1);

				HDRenderShadowReceiveMirrorEnd();
				mirrorCalls = HDRenderShadowReceiveCalls();
			}
			else
				printf("shadowmirror: the mirror's draw refused (variant %s, stencil %s)\n",
					HDRenderShadowReceiveMirrorReady() ? "ready" : "MISSING",
					HDRenderShadowReceiveStencil() ? "on" : "off");
		}

		HDRenderRoomShadow(o.shadowRoom, &roomWorld, 0, HD_WINDOW_ALL);

		for (size_t w = 0; w < withRooms.size(); w++)
			HDRenderRoomShadow(withRooms[w].first, &withRooms[w].second, 0,
				HD_WINDOW_ALL);

		const int n = HDRenderShadowReceiveCalls();

		HDRenderShadowReceiveEnd();
		return n;
	};

	// The room's solid art, as the room loop and the clear-floor loop draw it.
	const auto roomSolid = [&]()
	{
		HDRenderRoomMask(o.shadowRoom, &roomWorld, 0, HD_PASS_SOLID);

		if (HDRenderGroupHasClear(o.shadowRoom))
			HDRenderRoomMask(o.shadowRoom, &roomWorld, 0, HD_PASS_CLEAR);

		for (size_t w = 0; w < withRooms.size(); w++)
		{
			HDRenderRoomMask(withRooms[w].first, &withRooms[w].second, 0,
				HD_PASS_SOLID);

			if (HDRenderGroupHasClear(withRooms[w].first))
				HDRenderRoomMask(withRooms[w].first, &withRooms[w].second, 0,
					HD_PASS_CLEAR);
		}
	};

	int receiveCalls = 0;
	int othersCastDrawn = 0;

	/* --- draw -------------------------------------------------------------- */
	printf("\n-- the picture ------------------------------------------------\n");

	Shot shot;

	if (!ShotBegin(shot, o.width, o.height))
	{
		ShotEnd(shot);
		HDModelLevelEnd();
		return 7;
	}

	HDFrameLighting frame;
	float wibble[32];

	memset(&frame, 0, sizeof(frame));

	for (int i = 0; i < 32; i++)
		wibble[i] = 0.0f;

	frame.centerX = (float)o.width * 0.5f;
	frame.centerY = (float)o.height * 0.5f;
	frame.distanceFogStart = g_probe_fog > 0.0f ? g_probe_fog : 1.0e9f;
	frame.wibbleTable = wibble;

	const int bulbCount = PutUnder(frame, view, PutBulbs(frame));

	/* First person, the same three omissions the seam makes: the head
	 * node off the body, no separate face, no braid. All three sit exactly
	 * where the camera does. */
	/* -meshbits, on the composite as well. It reached the
	 * single-model path only, and the question "which node is that ring in
	 * front of the camera?" is answered by drawing one node and looking. */
	unsigned int bodyMask = o.nodeMask;

	if (o.firstPerson)
	{
		bodyMask &= ~(1u << HD_LARA_HEAD_NODE);
		printf("firstperson: head node %d masked, no LARA_HEAD, no braid\n",
			HD_LARA_HEAD_NODE);
	}

	if (o.nodeMask != ~0u)
		printf("meshbits: %08x - the composite is masked to those nodes\n",
			o.nodeMask);

	/* And the body's own nodes, which are every one of them unless the
	 * head rule has taken TR5's head instead of this body's - the engine's own
	 * HDLaraBodyMask, so this picture is the game's picture. Every other part
	 * keeps the frame's mask, exactly as specific\hd\hd_laradraw.cpp has it. */
	const unsigned int ownBody = bodyMask & HDLaraBodyMask();

	printf("head    : %s, %s; the body draws nodes %08x\n",
		HDWardrobeHeadWord(HDLaraHeadMode()),
		HDLaraHasPart(HD_LARA_PART_HEAD) ? "TR5's head is on the card"
			: "TR5's head is not loaded", ownBody);

	/* The head cut (a sphere, then a cylinder, then a
	 * plane, then one bone weight, in that order of development). The body carries it and nothing
	 * else, exactly as specific/hd/hd_laradraw.cpp pushes it: the hands, the
	 * holsters and the lid are their own models and must never be cut away.
	 *
	 * The two numbers come out of the engine's own HDLaraHeadCut, so this
	 * picture is the game's picture. Only the threshold may be overridden,
	 * which is what -headcut is for; a negative one draws the picture from
	 * before the cut existed, with no cut at all. */
	HDModelNeckCut neckCut = { 0 };
	int cutNode = HD_LARA_HEAD_NODE;
	float cutBar = 0.0f;
	const bool cutting = o.cutS >= 0.0 && HDLaraHeadCut(&cutNode, &cutBar);

	if (cutting)
	{
		neckCut.on = 1.0f;
		neckCut.node = (float)cutNode;
		neckCut.bar = (float)o.cutS;
	}

	printf("headcut : every fragment whose weight on node %d reaches %.2f, "
		"body alone%s%s\n", cutNode, o.cutS,
		cutting ? "" : " - off, this is the BEFORE picture",
		o.noCap ? "; the lid is OFF (-nocap)" : "");
	printf("cull    : %s\n", o.cull == HD_MODEL_CULL_CW ? "clockwise"
		: o.cull == HD_MODEL_CULL_CCW ? "counter-clockwise"
		: "none - both sides drawn, as every other HD model is");

	/* The sheen, part by part: the same table the
	 * engine reads, so this picture is the game's picture. */
	const bool sheenOn = !o.noSheen;

	/* -nosheen has to take the per page materials off as well,
	 * or the before picture would still carry the gun's highlight. */
	if (!sheenOn)
		for (int part = 0; part < HD_LARA_PART_COUNT; part++)
		{
			const int pslot = HDLaraPartSlot(part);

			if (!HDLaraHasPart(part))
				continue;

			for (int i = 0; i < HDModelPageCount(pslot); i++)
				HDModelSetPageSheen(pslot, HDModelPage(pslot, i), 0.0f, 1.0f);
		}

	/* -oldskin: her skin back under the outfit's material. The
	 * picture from before the two materials were split, and the only honest way to have one - the
	 * engine's own table is what the tool draws with, so that picture
	 * has to be made by undoing the split rather than by
	 * describing it. */
	if (o.oldSkin)
	{
		const int bodySlot = HDLaraPartSlot(HD_LARA_PART_BODY);
		float was = 0.0f, wasPower = 1.0f;
		int put = 0;

		HDLaraPartSheen(HD_LARA_PART_BODY, &was, &wasPower);

		for (int i = 0; i < HDModelPageCount(bodySlot); i++)
			if (HDLaraSkinPage(HDModelPage(bodySlot, i)))
			{
				HDModelSetPageSheen(bodySlot, HDModelPage(bodySlot, i), was,
					wasPower);
				put++;
			}

		printf("oldskin : %d skin page%s back on the outfit's %.2f/%.0f\n", put,
			put == 1 ? "" : "s", was, wasPower);
	}

	/* Her lips - see LipsAsk. One picture, or one per frame of a sheet, and
	 * nothing else changes between them. */
	const int lipsTrack = LipsOpen(o.lips, HDLaraPartSlot(HD_LARA_PART_FACE), false);
	const int pictures = lipsTrack >= 0 && o.lips.sheet ? (int)o.lips.frames.size() : 1;
	int across = 1;
	long sheetW = o.width, sheetH = o.height;
	std::vector<unsigned long> sheet, tile;

	if (pictures > 1)
	{
		LipsSheetSize(pictures, o.width, o.height, &across, &sheetW, &sheetH);
		sheet.assign((size_t)sheetW * (size_t)sheetH, 0xFF000000u);
	}

	/* The models' list once more, as the picture draws it: her parts with
	 * their masks, cuts, culls, lips and soft motion, then the creatures and
	 * the items with their separate faces. `receive` draws it through the
	 * models' receive draw (hd_model.h, HDModelReceiveDraw), each entry with
	 * the blocks of the maps its class takes (hd_shadow.h,
	 * HDShadowReceiverTakes: her parts LARA, a creature BLOB, an item
	 * OTHER), as hd_object.cpp's HDObjectShadowReceiveFrame draws the frame
	 * list; otherwise through HDModelDraw, for -shadowtime's frame. Returns
	 * the entries drawn. */
	float receiveHers[HD_SHADOW_RECEIVE_COUNT * 4];
	float receiveOthers[HD_SHADOW_RECEIVE2_COUNT * 4];
	// -shadowmirror: the mirror's two blocks, made with its reflected view.
	float mirrorHers[HD_SHADOW_RECEIVE_COUNT * 4];
	float mirrorOthers[HD_SHADOW_RECEIVE2_COUNT * 4];
	bool receiveHersDrawn = false;
	bool mirrorBlocks = false;

	/* `reflected`: a reflection, which takes through the mirror's blocks
	 * where they exist and nothing where they do not. */
	const auto receiveBlocks = [&](int receiver, bool reflected) -> bool
	{
		const int takes = HDShadowReceiverTakes(receiver, receiveHersDrawn, othersDrawn, reflected,
			reflected && mirrorBlocks);

		if (!takes)
			return false;

		float h[HD_SHADOW_RECEIVE_COUNT * 4], s[HD_SHADOW_RECEIVE2_COUNT * 4];

		memcpy(h, reflected ? mirrorHers : receiveHers, sizeof(h));
		memcpy(s, reflected ? mirrorOthers : receiveOthers, sizeof(s));

		if (!(takes & HD_SHADOW_TAKES_HERS))
			HDShadowReceiveWithoutHer(h);

		if (!(takes & HD_SHADOW_TAKES_OTHERS))
			HDShadowReceiveWithoutOthers(s);

		HDRenderShadowModelsBlocks(h, s);
		return true;
	};

	/* `reflected`: the same list as the mirror's reflections, on the
	 * reflected matrices (-shadowmirror), with the mirror's blocks. */
	const auto modelList = [&](bool receive, int lipsT, int lipsF, bool reflected) -> int
	{
		const float* const mats = reflected ? reflMatrices : matrices;
		const float* const hair = reflected ? reflHair : hairMatrices;

		const auto one = [&](int slot, bool head, const float* m, int nodes,
			const float* nodeLight, unsigned int mask, const HDModelLighting* lit,
			const HDModelNeckCut* cut, int cull, int lt, int lf, const HDModelSoft* sft) -> bool
		{
			if (receive)
				return HDModelReceiveDraw(slot, head, m, nodes, mask, cut, cull, lt, lf,
					head ? 0 : sft);

			return head ? HDModelDrawHead(slot, m, nodes, nodeLight, mask, lit, cut, cull, lt, lf)
				: HDModelDraw(slot, m, nodes, nodeLight, mask, lit, cut, cull, lt, lf, sft);
		};

		int n = 0;
		const HDModelNeckCut* bodyCut = cutting ? &neckCut : 0;

		if (!receive || receiveBlocks(HD_SHADOW_RECEIVER_LARA, reflected))
		{
			bool any = false;

			if (draws(HD_LARA_PART_BODY))
				any = one(xrayDraw ? HDLaraPartXraySlot(HD_LARA_PART_BODY)
					: HDLaraPartSlot(HD_LARA_PART_BODY), false, mats, HD_LARA_NODES,
					light, xrayDraw ? bodyMask : ownBody, &lighting, bodyCut,
					o.cull, -1, -1, soft(HD_LARA_PART_BODY)) || any;

			if (!xrayDraw && draws(HD_LARA_PART_HEAD) &&
				HDLaraHasPart(HD_LARA_PART_HEAD))
				any = one(HDLaraPartSlot(HD_LARA_PART_HEAD), false, mats, HD_LARA_NODES,
					light, bodyMask, &lighting, bodyCut, o.cull, -1, -1,
					soft(HD_LARA_PART_HEAD)) || any;

			if (!xrayDraw && o.firstPerson && cutting && !o.noCap &&
				draws(HD_LARA_PART_NECK_CAP) && HDLaraHasPart(HD_LARA_PART_NECK_CAP))
				any = one(HDLaraPartSlot(HD_LARA_PART_NECK_CAP), false, mats, HD_LARA_NODES,
					light, ~0u, &lighting, 0, HD_MODEL_CULL_NONE, -1, -1,
					soft(HD_LARA_PART_NECK_CAP)) || any;

			if (!xrayDraw && !o.firstPerson && draws(HD_LARA_PART_FACE))
				any = one(HDLaraPartSlot(HD_LARA_PART_FACE), false, mats, HD_LARA_NODES,
					light, bodyMask, &lighting, 0, HD_MODEL_CULL_NONE, lipsT, lipsF,
					soft(HD_LARA_PART_FACE)) || any;

			if (!xrayDraw && draws(HD_LARA_PART_HOLSTER_RIG) &&
				HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG))
				any = one(HDLaraPartSlot(HD_LARA_PART_HOLSTER_RIG), false, mats,
					HD_LARA_NODES, light, HDLaraPartCoverage(HD_LARA_PART_HOLSTER_RIG) & bodyMask,
					&lighting, 0, HD_MODEL_CULL_NONE, -1, -1,
					soft(HD_LARA_PART_HOLSTER_RIG)) || any;

			for (int i = 0; i < pieces; i++)
			{
				if (draws(list[i].part))
					any = one(xrayDraw ? HDLaraPartXraySlot(list[i].part)
						: HDLaraPartSlot(list[i].part), false, mats, HD_LARA_NODES,
						light, list[i].nodeMask & bodyMask, &lighting, 0, HD_MODEL_CULL_NONE,
						-1, -1, 0) || any;

				if (!xrayDraw && draws(list[i].part) &&
					HDLaraHasPartTwin(list[i].part))
					any = one(HDLaraPartTwinSlot(list[i].part), false, mats, HD_LARA_NODES,
						light, list[i].nodeMask & bodyMask, &lighting, 0, HD_MODEL_CULL_NONE,
						-1, -1, 0) || any;
			}

			if (!xrayDraw && !o.firstPerson && draws(HD_LARA_PART_BRAID) &&
				HDLaraHasPart(HD_LARA_PART_BRAID))
			{
				float hairLight[HD_LARA_HAIR_NODES * 3];

				for (int i = 0; i < HD_LARA_HAIR_NODES * 3; i++)
					hairLight[i] = (float)o.shade;

				any = one(HDLaraPartSlot(HD_LARA_PART_BRAID), false, hair,
					HD_LARA_HAIR_NODES, hairLight, ~0u, &lighting, 0, HD_MODEL_CULL_NONE, -1, -1,
					0) || any;
			}

			n += any ? 1 : 0;
		}

		for (size_t c = 0; c < placed.size(); c++)
		{
			// A creature the picture clipped away is drawn in no pass.
			if (placed[c].unseen)
				continue;

			if (receive && !receiveBlocks(placed[c].blob ? HD_SHADOW_RECEIVER_BLOB
				: HD_SHADOW_RECEIVER_OTHER, reflected))
				continue;

			const float* const cm = reflected ? &reflPlaced[c][0] : &placed[c].matrices[0];

			HDModelLighting itemLighting = lighting;
			std::vector<float> itemLight((size_t)placed[c].nodes * 3, (float)o.shade);

			itemLighting.specStrength = 0.0f;
			itemLighting.specPower = 1.0f;

			// A mirrored matrix turns the winding: the other side is front.
			const int side = !reflected ? placed[c].cull
				: placed[c].cull == HD_MODEL_CULL_CW ? HD_MODEL_CULL_CCW
				: placed[c].cull == HD_MODEL_CULL_CCW ? HD_MODEL_CULL_CW
				: HD_MODEL_CULL_NONE;
			bool any = one(placed[c].slot, false, cm, placed[c].nodes,
				&itemLight[0], ~0u, &itemLighting, 0, side, -1, -1, 0);

			if (HDModelHasHead(placed[c].slot))
				any = one(placed[c].slot, true, cm, placed[c].nodes,
					&itemLight[0], ~0u, &itemLighting, 0, HD_MODEL_CULL_NONE, -1, -1, 0) || any;

			n += any ? 1 : 0;
		}

		return n;
	};

	/* The models' receive pass as hd_level.cpp runs it (ShadowModelsPass):
	 * after the models' solid pass, in a frame whose rooms' receive ran,
	 * with the maps the rooms took - her map, and the second map when
	 * othersPass drew it. -shadownoguard takes the guard away inside it as
	 * well. -1 when the bracket could not begin; `modelCalls` comes back
	 * with its draw calls. */
	int modelCalls = 0;
	bool stateChecked = false;

	const auto modelsPass = [&](int lipsT, int lipsF) -> int
	{
		receiveHersDrawn = shadowCastDrawn >= 0;
		modelCalls = 0;

		HDShadowReceiveConstants(&shadowBox, R, t, HD_SHADOW_DARK *
			g_probe_presence,
			receiveHers);

		if (othersDrawn)
			HDShadowReceiveOthersConstants(&othersBox, R, t, receiveOthers);
		else
		{
			memset(receiveOthers, 0, sizeof(receiveOthers));
			HDShadowReceiveWithoutOthers(receiveOthers);
		}

		/* -shadowmirror: the mirror's blocks for the reflections, as
		 * hd_object.cpp makes them (HDShadowMirrorView) while the camera sees
		 * the glass from the room's side; -shadownomirror leaves them out and
		 * the reflections take nothing, as before they had any. */
		mirrorBlocks = mirrorOn && !o.shadowNoMirror &&
			HDShadowMirrorFaces(R, t, mirror.axis, mirrorPlane, mirror.side);

		if (mirrorBlocks)
		{
			float Rm[9], tm[3];

			HDShadowMirrorView(R, t, mirror.axis, mirrorPlane, Rm, tm);
			HDShadowReceiveConstants(&shadowBox, Rm, tm, HD_SHADOW_DARK *
				g_probe_presence,
				mirrorHers);

			if (othersDrawn)
				HDShadowReceiveOthersConstants(&othersBox, Rm, tm, mirrorOthers);
			else
			{
				memset(mirrorOthers, 0, sizeof(mirrorOthers));
				HDShadowReceiveWithoutOthers(mirrorOthers);
			}
		}

		/* -shadowdevice: the device's whole state before the pass and after
		 * it, compared - once, on the first picture. */
		std::vector<unsigned long long> before, after;
		std::vector<std::string> what;
		const bool compare = o.shadowDevice && !stateChecked;

		if (compare)
			DeviceStateTake(before, &what);

		if (!HDModelReceiveReady() || !HDRenderShadowModelsBegin(receiveHersDrawn, othersDrawn))
			return -1;

		if (o.shadowNoGuard)
			g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);

		HDModelReceiveBegin();

		int n = modelList(true, lipsT, lipsF, false);

		// The reflections, clipped to the copy, as hd_object.cpp draws them.
		if (mirrorOn)
		{
			HDModelClip(mirrorClip);
			n += modelList(true, lipsT, lipsF, true);
			HDModelClip(0);
		}

		modelCalls = HDModelReceiveCalls();
		HDModelReceiveEnd();
		HDRenderShadowModelsEnd();

		if (compare)
		{
			int differ = 0;

			DeviceStateTake(after);
			stateChecked = true;

			for (size_t i = 0; i < before.size() && i < after.size(); i++)
				if (before[i] != after[i])
				{
					if (differ < 8)
						printf("  state differs after the models' pass: %s (item %d)\n",
							what[i].c_str(), (int)i);

					differ++;
				}

			printf("shadowdevice: the device's state after the models' pass as before it, %d "
				"values over the targets, viewport, scissor, shaders, streams, textures, "
				"samplers, render states and constants - %s (%d differ, %d models drawn again)\n",
				(int)before.size(), differ == 0 && before.size() == after.size() ? "PASS" : "FAIL",
				differ, n);
		}

		return n;
	};

	/* -shadowcount: each model's pixels marked in the stencil's bits 2 to 7
	 * by the last draw that passes the depth test there - so the mark is the
	 * model the picture left visible - her parts as 1, the creatures and
	 * items from 2 on. 0 switches the marking off. Without the switch it
	 * sets nothing. */
	const auto tag = [&](int id)
	{
		if (!o.shadowCount)
			return;

		if (!id)
		{
			g_device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
			return;
		}

		g_device->SetRenderState(D3DRS_STENCILENABLE, TRUE);
		g_device->SetRenderState(D3DRS_TWOSIDEDSTENCILMODE, FALSE);
		g_device->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
		g_device->SetRenderState(D3DRS_STENCILREF, (DWORD)id << 2);
		g_device->SetRenderState(D3DRS_STENCILWRITEMASK, 0xFC);
		g_device->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
		g_device->SetRenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
		g_device->SetRenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
	};

	int modelsDrawn = 0;
	int drawn = 0;
	int calls = 0;

	for (int picture = 0; picture < pictures; picture++)
	{
		const int lipsFrame = lipsTrack >= 0 ? o.lips.frames[(size_t)picture] : -1;

		if (picture)
			g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
				(D3DCOLOR)(0xFF000000u | g_clear), 1.0f, 0);

		g_device->BeginScene();
		HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

		// -shadowroom: the room's solid art first, as HDFrameDraw's room loop.
		if (roomHas)
			roomSolid();

		/* -shadowcast: the cast pass where HDFrameDraw runs it, after the
		 * bracket has opened and before the models' own frame begins. */
		if (o.shadowCast || o.shadowReceive)
			shadowCastDrawn = shadowPass(lipsTrack, lipsFrame);

		// -shadowcreature: the second map's cast pass right after hers.
		if ((o.shadowCast || o.shadowReceive) && !placed.empty())
			othersCastDrawn = othersPass();

		// -shadowreceive: the receive pass right after it, as HDFrameDraw has it.
		if (o.shadowReceive && roomHas && shadowCastDrawn >= 0)
			receiveCalls = receivePass();

		/* -shadowcount draws the solid passes alone, so that no glow, which
		 * writes no depth, marks a pixel. A picture whose room takes the
		 * shadow draws them alone too, and the models' additive passes after
		 * the room's glass and glows (below), where HDFrameDrawGlow draws them
		 * (hd_level.cpp) - after the models' pass, which would darken a glow
		 * on a model it ran after. */
		const bool additiveAfter = o.shadowReceive && roomHas && !o.shadowCount;

		HDModelFrameBegin(&projection, bulbCount,
			o.shadowCount || additiveAfter ? HD_PASS_SOLID : o.passes);

		// Every model of the picture, through the passes HDModelFrameBegin set.
		const auto drawModels = [&]()
		{
		drawn = 0;

		tag(1);
		SetSheen(lighting, HD_LARA_PART_BODY, sheenOn);

		/* -xray: the X-ray body out of its own block, with its own sheen and
		 * its own node 14, as hd_laradraw.cpp draws it. */
		if (xrayDraw)
		{
			float xs = 0.0f, xp = 1.0f;

			HDLaraXraySheen(&xs, &xp);
			lighting.specStrength = sheenOn ? xs : 0.0f;
			lighting.specPower = xp;
		}

		if (draws(HD_LARA_PART_BODY) && HDModelDraw(xrayDraw
			? HDLaraPartXraySlot(HD_LARA_PART_BODY)
			: HDLaraPartSlot(HD_LARA_PART_BODY), matrices, HD_LARA_NODES,
			light, xrayDraw ? bodyMask : ownBody, &lighting,
			cutting ? &neckCut : 0, o.cull, -1, -1, soft(HD_LARA_PART_BODY)))
			drawn++;

		/* TR5's own head, in the same place and with the same rules the
		 * seam has: after the body and before the face, the frame's mask, the
		 * body's cut and the body's cull - see specific\hd\hd_laradraw.cpp. */
		if (!xrayDraw && draws(HD_LARA_PART_HEAD) &&
			HDLaraHasPart(HD_LARA_PART_HEAD))
		{
			SetSheen(lighting, HD_LARA_PART_HEAD, sheenOn);

			if (HDModelDraw(HDLaraPartSlot(HD_LARA_PART_HEAD), matrices,
				HD_LARA_NODES, light, bodyMask, &lighting,
				cutting ? &neckCut : 0, o.cull, -1, -1, soft(HD_LARA_PART_HEAD)))
				drawn++;
		}

		/* The lid, in the
		 * same place and with the same rules the seam has: no cut of its own (its
		 * rim vertices are copies of the body's and would sit exactly on the
		 * boundary), no cull, every node (its rim is skinned to whatever the
		 * body's rim is), and the skin's own material. -nocap leaves it out, which
		 * is the picture from before the lid existed. */
		if (!xrayDraw && o.firstPerson && cutting && !o.noCap &&
			draws(HD_LARA_PART_NECK_CAP) && HDLaraHasPart(HD_LARA_PART_NECK_CAP))
		{
			SetSheen(lighting, HD_LARA_PART_NECK_CAP, sheenOn);

			if (HDModelDraw(HDLaraPartSlot(HD_LARA_PART_NECK_CAP), matrices,
				HD_LARA_NODES, light, ~0u, &lighting, 0,
				HD_MODEL_CULL_NONE, -1, -1, soft(HD_LARA_PART_NECK_CAP)))
				drawn++;
		}

		SetSheen(lighting, HD_LARA_PART_FACE, sheenOn);

		// The face with her lips at the frame asked for, and -1 without -lips.
		if (!xrayDraw && !o.firstPerson && draws(HD_LARA_PART_FACE) &&
			HDModelDraw(HDLaraPartSlot(HD_LARA_PART_FACE), matrices, HD_LARA_NODES,
			light, bodyMask, &lighting, 0, HD_MODEL_CULL_NONE, lipsTrack, lipsFrame,
			soft(HD_LARA_PART_FACE)))
			drawn++;

		/* The holster rig, in the same place and with the
		 * same guard specific\hd\hd_laradraw.cpp puts it: before the pieces,
		 * because it belongs to the outfit and not to the weapon. */
		SetSheen(lighting, HD_LARA_PART_HOLSTER_RIG, sheenOn);

		if (!xrayDraw && draws(HD_LARA_PART_HOLSTER_RIG) &&
			HDLaraHasPart(HD_LARA_PART_HOLSTER_RIG) &&
			HDModelDraw(HDLaraPartSlot(HD_LARA_PART_HOLSTER_RIG), matrices,
				HD_LARA_NODES, light,
				HDLaraPartCoverage(HD_LARA_PART_HOLSTER_RIG) & bodyMask, &lighting, 0,
				HD_MODEL_CULL_NONE, -1, -1, soft(HD_LARA_PART_HOLSTER_RIG)))
			drawn++;

		for (int i = 0; i < pieces; i++)
		{
			SetSheen(lighting, list[i].part, sheenOn);

			if (draws(list[i].part) &&
				HDModelDraw(xrayDraw ? HDLaraPartXraySlot(list[i].part)
				: HDLaraPartSlot(list[i].part), matrices, HD_LARA_NODES,
				light, list[i].nodeMask & bodyMask, &lighting))
				drawn++;

			/* The install's own hands under a mod's gun, in the
			 * same place and with the same arguments the engine gives them - see
			 * hd_laradraw.cpp. */
			if (!xrayDraw && draws(list[i].part) &&
				HDLaraHasPartTwin(list[i].part) &&
				HDModelDraw(HDLaraPartTwinSlot(list[i].part), matrices,
				HD_LARA_NODES, light, list[i].nodeMask & bodyMask, &lighting))
				drawn++;
		}

		if (!xrayDraw && !o.firstPerson && draws(HD_LARA_PART_BRAID) &&
			HDLaraHasPart(HD_LARA_PART_BRAID))
		{
			float hairLight[HD_LARA_HAIR_NODES * 3];

			for (int i = 0; i < HD_LARA_HAIR_NODES * 3; i++)
				hairLight[i] = (float)o.shade;

			SetSheen(lighting, HD_LARA_PART_BRAID, sheenOn);

			if (HDModelDraw(HDLaraPartSlot(HD_LARA_PART_BRAID), hairMatrices,
				HD_LARA_HAIR_NODES, hairLight, ~0u, &lighting))
				drawn++;
		}

		/* -shadowcreature: the creatures after her, each with its separate
		 * face where its slot has one, in her light without her sheen - an
		 * item has none. */
		for (size_t c = 0; c < placed.size(); c++)
		{
			HDModelLighting itemLighting = lighting;
			std::vector<float> itemLight((size_t)placed[c].nodes * 3, (float)o.shade);

			itemLighting.specStrength = 0.0f;
			itemLighting.specPower = 1.0f;

			// -shadowunseen: cast into the second map, drawn in no pass.
			if (placed[c].unseen)
				continue;

			tag(2 + (int)c);

			if (HDModelDraw(placed[c].slot, &placed[c].matrices[0], placed[c].nodes,
				&itemLight[0], ~0u, &itemLighting, 0, placed[c].cull))
				drawn++;

			if (HDModelHasHead(placed[c].slot) && HDModelDrawHead(placed[c].slot,
				&placed[c].matrices[0], placed[c].nodes, &itemLight[0], ~0u, &itemLighting))
				drawn++;
		}

		/* -shadowmirror: the reflections into the copy, clipped to it, after
		 * the things they reflect - game\mirror.cpp draws them before the
		 * items, and the depth buffer sorts the two, which never overlap
		 * across the glass. -shadowcount marks them as one model more. */
		if (mirrorOn)
		{
			tag(2 + (int)placed.size());
			HDModelClip(mirrorClip);
			drawn += modelList(false, lipsTrack, lipsFrame, true);
			HDModelClip(0);
		}

		};

		drawModels();
		calls = HDModelDrawCalls();
		tag(0);

		/* -shadowreceive: the models take the maps after their solid pass,
		 * as HDFrameDraw has it, in a picture whose room took them;
		 * -shadownomodels leaves it out. */
		modelsDrawn = 0;

		if (o.shadowReceive && roomHas && receiveCalls >= 0 && shadowCastDrawn >= 0 &&
			!o.shadowNoModels)
			modelsDrawn = modelsPass(lipsTrack, lipsFrame);

		HDRenderFrameEnd();

		/* -shadowcount: per model, the pixels the picture left it visible
		 * (its mark) and those of them the models' pass reached (its mark
		 * and the guard's bit 1), and the pixels the pass reached on no
		 * model; then the mask in place of the picture: red a model's
		 * pixel, yellow one the pass reached, green one it reached off every
		 * model. Depth identity is reached == visible for every model drawn
		 * again, and 0 off them. */
		if (o.shadowCount && picture == 0)
			CountModelPixels(o.width, o.height, 1 + (int)placed.size() + (mirrorOn ? 1 : 0),
				modelsDrawn);

		/* -shadowmirrormask: the copy's pixels, which the mirror's draw left
		 * (stencil bit 2), white on black in place of the picture. */
		if (o.shadowMirrorMask && picture == 0)
		{
			g_device->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
			StencilQuad(o.width, o.height, D3DCMP_EQUAL, 4, 4, D3DCOLOR_XRGB(255, 255, 255));
			printf("shadowmirrormask: %ld pixels of the copy took the mirror's blocks\n",
				StencilQuad(o.width, o.height, D3DCMP_EQUAL, 4, 4));
		}

		/* And the room's glass and glows in a bracket of their own, as
		 * HDFrameDrawGlow draws them after the classic buckets. */
		if (roomHas && !o.shadowCount && !o.shadowMirrorMask)
		{
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
			HDRenderRoomMask(o.shadowRoom, &roomWorld, 0, HD_PASS_BLEND);

			for (size_t w = 0; w < withRooms.size(); w++)
				HDRenderRoomMask(withRooms[w].first, &withRooms[w].second, 0,
					HD_PASS_BLEND);

			HDRenderRoomMask(o.shadowRoom, &roomWorld, 0, HD_PASS_ADDITIVE);

			for (size_t w = 0; w < withRooms.size(); w++)
				HDRenderRoomMask(withRooms[w].first, &withRooms[w].second, 0,
					HD_PASS_ADDITIVE);

			// The models' glows after the room's, as HDFrameDrawGlow has them.
			if (additiveAfter)
			{
				const int solid = drawn;

				HDModelFrameBegin(&projection, bulbCount, HD_PASS_ADDITIVE);
				drawModels();
				drawn = solid;
			}

			HDRenderFrameEnd();
		}

		g_device->EndScene();

		LipsSay();

		if (pictures > 1 && ShotGrab(shot, tile))
			LipsPaste(sheet, sheetW, tile, o.width, o.height, picture, across);
	}

	printf("%d models drawn in %d draw calls\n", drawn, calls);

	char path[512];

	char cut[32] = "";

	if (o.firstPerson)
		sprintf(cut, "_p%d_f%d_w%d%s_%s",
			(int)(o.pitchSet ? o.pitch : 60.0),
			(int)o.fpForward, (int)(o.cutS * 100.0 + 0.5),
			o.noCap ? "_nocap" : "",
			o.cull == HD_MODEL_CULL_CW ? "cw"
			: o.cull == HD_MODEL_CULL_CCW ? "ccw" : "nocull");

	/* A picture with her lips at a frame says which, and a sheet says the
	 * first, the last and the step. */
	const std::string lipsName = LipsName(o.lips, lipsTrack, pictures > 1);

	sprintf(path, "%s_lara_%s%s%s%s%s%s%s.png", o.out.c_str(),
		HDLaraOutfitModel(outfit), o.firstPerson ? "_firstperson" : "", cut,
		o.mirror ? "_mirror" : "", lipsName.c_str(), o.shadowCast ? "_cast" : "",
		xrayDraw ? "_xray" : "");

	/* A picture with a room says which, and what the receive did to it. */
	if (roomHas)
	{
		const size_t at = strlen(path) - 4;
		char tail[64];

		sprintf(tail, "_room%d%s.png", o.shadowRoom, !o.shadowReceive ? ""
			: o.shadowCount ? "_mask" : o.shadowEmpty ? "_empty" : o.shadowNoGuard ? "_noguard"
			: o.shadowNoModels ? "_nomodels" : "_recv");
		path[at] = 0;
		strcat(path, tail);
	}

	// ... one with the room's mirror, which view and whether its blocks ran ...
	if (mirrorOn)
	{
		const size_t at = strlen(path) - 4;

		path[at] = 0;
		strcat(path, o.shadowMirrorView ? "_mview" : "_mirror");

		if (o.shadowNoMirror)
			strcat(path, "_nomirror");

		if (o.shadowMirrorMask)
			strcat(path, "_copymask");

		strcat(path, ".png");
	}

	// ... and one with creatures beside her, how many.
	if (!placed.empty())
	{
		const size_t at = strlen(path) - 4;
		char tail[32];

		sprintf(tail, "_c%d%s.png", (int)placed.size(), o.shadowUnseen ? "_unseen" : "");
		path[at] = 0;
		strcat(path, tail);

		if (o.shadowCast || o.shadowReceive)
			printf("shadowothers: %d models into the second map%s\n", othersCastDrawn,
				othersCastDrawn < 0 ? " - REFUSED, the map or its shaders"
				: othersCastDrawn == 0 ? " - none inside the second box, the map not drawn" : "");
	}

	if (o.shadowCast)
		printf("shadowcast: the cast pass ran inside the picture's bracket, %d models "
			"into the map%s\n", shadowCastDrawn,
			shadowCastDrawn < 0 ? " - REFUSED, the map or its shaders" : "");

	if (o.shadowReceive && roomHas)
	{
		printf("shadowreceive: %d models into the map%s, the room again in %d draw calls%s, "
			"stencil guard %s\n", shadowCastDrawn, o.shadowEmpty ? " (-shadowempty)" : "",
			receiveCalls, receiveCalls < 0 ? " - REFUSED" : "",
			o.shadowNoGuard ? "OFF (-shadownoguard)"
			: HDRenderShadowReceiveStencil() ? "on" : "off - no stencil");

		if (mirrorOn)
			printf("shadowmirror: the mirror's group again in %d draw calls through the "
				"mirror's blocks%s\n", mirrorCalls,
				o.shadowNoMirror ? " - left out (-shadownomirror)" : "");

		if (o.shadowNoModels)
			printf("shadowmodels: the models' pass left out (-shadownomodels)\n");
		else
			printf("shadowmodels: %d model(s) drawn again in %d draw calls%s, stencil guard %s\n",
				modelsDrawn, modelCalls, modelsDrawn < 0 ? " - REFUSED" : "",
				o.shadowNoGuard ? "OFF (-shadownoguard)"
				: HDRenderShadowModelsStencil() ? "on" : "off - no stencil");
	}

	if (pictures > 1)
	{
		const bool wrote = WritePng(path, &sheet[0], sheetW, sheetH);

		printf("  %-58s %d frames, %d across, %s\n", path, pictures, across,
			wrote ? "written" : "NOT written");
	}
	else
		ShotWrite(shot, path);

	ShotEnd(shot);

	/* The map alone, in a bracket of its own on the device's own target. */
	const auto mapRun = [&](std::vector<float>& map) -> int
	{
		g_device->BeginScene();
		HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

		const int n = shadowPass(-1, -1);

		HDRenderFrameEnd();
		g_device->EndScene();

		if (n < 0 || !ShadowMapRead(map, HDRenderShadowMap(), HD_SHADOW_MAP))
			return -1;

		return n;
	};

	if (o.shadowMap)
	{
		std::vector<float> map;

		printf("\n-- the projected shadow's map --------------------------------\n");

		const int n = mapRun(map);

		sprintf(path, "%s_shadowmap_%s%s.png", o.out.c_str(), HDLaraOutfitModel(outfit),
			o.firstPerson ? "_firstperson" : "");

		if (n < 0)
			printf("  the map was refused or not read back\n");
		else
		{
			printf("  %d models into the map, %d draw calls\n", n, HDModelCastCalls());
			ShadowMapWrite(map, HD_SHADOW_MAP, HD_SHADOW_DEPTH, path);
		}
	}

	/* The second map alone, in a bracket of its own: her pass and then the
	 * creatures', as HDFrameDraw runs them. -1 when it was refused, not
	 * drawn (no creature inside the second box) or not read back. */
	const auto othersMapRun = [&](std::vector<float>& map) -> int
	{
		g_device->BeginScene();
		HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

		shadowPass(-1, -1);

		const int n = othersPass();

		HDRenderFrameEnd();
		g_device->EndScene();

		if (n <= 0 || !ShadowMapRead(map, HDRenderShadowOthersMap(), HD_SHADOW_OTHERS_MAP))
			return -1;

		return n;
	};

	if (o.shadowOthers)
	{
		std::vector<float> map;

		printf("\n-- the second map ---------------------------------------------\n");

		const int n = placed.empty() ? -1 : othersMapRun(map);

		sprintf(path, "%s_shadowothers_%s_c%d.png", o.out.c_str(), HDLaraOutfitModel(outfit),
			(int)placed.size());

		if (n < 0)
			printf("  no creature drawn into it (-shadowcreature, inside the second box), "
				"or the map was refused or not read back\n");
		else
		{
			HDShadowTargetInfo info;

			HDRenderShadowOthersInfo(&info);
			printf("  %d models into the second map, %d draw calls; the target %s %dx%d, depth "
				"%s\n", n, HDModelCastCalls(), info.format, info.size, info.size, info.depth);
			ShadowMapWrite(map, HD_SHADOW_OTHERS_MAP, HD_SHADOW_OTHERS_DEPTH, path);
		}
	}

	if (o.shadowDevice)
	{
		printf("\n-- the projected shadow on this device ------------------------\n");

		/* The reference count: a made and released map leaves the device's
		 * count where it found it. */
		HDRenderShadowTargetRelease();
		g_device->AddRef();

		const ULONG before = g_device->Release();
		const bool made = HDRenderShadowTargetReady();

		g_device->AddRef();

		const ULONG during = g_device->Release();
		HDShadowTargetInfo info;

		HDRenderShadowTargetInfo(&info);
		printf("  target %s (%s): %s %dx%d, depth %s, cast shaders %s\n",
			made ? "made" : "REFUSED", info.reason, info.format, info.size, info.size,
			info.depth, HDModelCastReady() ? "made" : "MISSING");

		HDRenderShadowTargetRelease();
		g_device->AddRef();

		const ULONG after = g_device->Release();

		printf("  device references: %lu before, %lu with the map, %lu after - %s\n",
			before, during, after, after == before ? "PASS" : "FAIL");

		/* The map, the device Reset, the map again. With -withrooms the
		 * rooms' vertex and index buffers live in D3DPOOL_DEFAULT
		 * (hd_render.cpp, the level's buffers), and a Reset is refused while
		 * they do. At a device loss the game drops the whole HD path and
		 * builds the level again on the device it then has (hd_level.cpp,
		 * HDLevelDeviceLost, HDRenderShutdown); the probe appends its models'
		 * pages to the rooms' cache, which that would drop with them, so it
		 * leaves the two Resets out with rooms and says so. */
		const bool roomsHeld = HDRenderGroupCount() > 0;
		std::vector<float> first, second;
		const int castFirst = mapRun(first);

		HDRenderShadowTargetRelease();

		if (roomsHeld)
			printf("  the Resets left out: the rooms (-withrooms) hold "
				"D3DPOOL_DEFAULT "
				"buffers, which the game drops with the whole HD path at a "
					"device "
				"loss and builds again with the level; run without -withrooms "
					"for "
				"them\n");

		const HRESULT reset = roomsHeld ? E_ABORT : g_device->Reset(&g_present);

		if (!roomsHeld)
			printf("  map before the Reset: %d models; Reset 0x%08lx (%s)\n",
				castFirst, (unsigned long)reset, SUCCEEDED(reset) ? "ok" :
					"FAILED");

		const int castSecond = SUCCEEDED(reset) ? mapRun(second) : -1;
		const bool same = castFirst >= 0 && castSecond >= 0 &&
			first.size() == second.size() &&
			!memcmp(&first[0], &second[0], first.size() * sizeof(float));

		HDRenderShadowTargetInfo(&info);

		if (!roomsHeld)
			printf("  map after the Reset: %d models, target %s, %u state "
				"changes "
				"- %s\n", castSecond,
				info.state == HD_SHADOW_TARGET_MADE ? "made again" : "NOT made",
				(unsigned)info.events, same ? "PASS, the same map bit for bit"
				: "FAIL, the maps differ");

		/* The second map the same way: made alone and released alone with
		 * the device's count where it was; released with hers by
		 * HDRenderShadowTargetRelease, the device Reset, and made again with
		 * the creatures drawn into it the same, bit for bit. */
		{
			HDRenderShadowTargetRelease();
			g_device->AddRef();

			const ULONG obefore = g_device->Release();
			LARGE_INTEGER makeFrom, makeTo, makeRate;

			QueryPerformanceFrequency(&makeRate);
			QueryPerformanceCounter(&makeFrom);

			const bool omade = HDRenderShadowOthersReady();

			QueryPerformanceCounter(&makeTo);
			g_device->AddRef();

			const ULONG oduring = g_device->Release();
			HDShadowTargetInfo oinfo;

			HDRenderShadowOthersInfo(&oinfo);
			printf("  second target %s (%s): %s %dx%d, depth %s - her map %s meanwhile; made in "
				"%.3f ms\n", omade ? "made" : "REFUSED", oinfo.reason, oinfo.format, oinfo.size,
				oinfo.size, oinfo.depth, HDRenderShadowMap() ? "made" : "not made",
				(double)(makeTo.QuadPart - makeFrom.QuadPart) * 1000.0 / (double)makeRate.QuadPart);

			HDRenderShadowOthersRelease();
			g_device->AddRef();

			const ULONG oafter = g_device->Release();

			printf("  device references: %lu before, %lu with the second map, %lu after - %s\n",
				obefore, oduring, oafter, oafter == obefore ? "PASS" : "FAIL");

			std::vector<float> ofirst, osecond;
			const int ocastFirst = placed.empty() ? -1 : othersMapRun(ofirst);

			HDRenderShadowTargetInfo(&info);
			HDRenderShadowOthersInfo(&oinfo);

			const bool bothMade = info.state == HD_SHADOW_TARGET_MADE &&
				oinfo.state == HD_SHADOW_TARGET_MADE;

			HDRenderShadowTargetRelease();
			HDRenderShadowOthersInfo(&oinfo);

			const bool bothGone = !HDRenderShadowMap() && !HDRenderShadowOthersMap() &&
				oinfo.state == HD_SHADOW_TARGET_RELEASED;
			const HRESULT oreset = roomsHeld ? E_ABORT :
				g_device->Reset(&g_present);
			const int ocastSecond = SUCCEEDED(oreset) && !placed.empty()
				? othersMapRun(osecond) : -1;
			const bool osame = ocastFirst > 0 && ocastSecond > 0 &&
				ofirst.size() == osecond.size() &&
				!memcmp(&ofirst[0], &osecond[0], ofirst.size() * sizeof(float));

			HDRenderShadowOthersInfo(&oinfo);

			if (!roomsHeld)
			{
				printf("  second map before the Reset: %d models; both maps "
					"%s, both "
					"released by HDRenderShadowTargetRelease: %s; Reset "
						"0x%08lx (%s)\n",
					ocastFirst, bothMade ? "made" : "NOT both made",
					bothGone ? "PASS" : "FAIL", (unsigned long)oreset,
					SUCCEEDED(oreset) ? "ok" : "FAILED");
				printf("  second map after the Reset: %d models, target %s, "
					"%u state "
					"changes - %s\n", ocastSecond,
					oinfo.state == HD_SHADOW_TARGET_MADE ? "made again" :
						"NOT made",
					(unsigned)oinfo.events, placed.empty()
					? "no creature (-shadowcreature), no map to compare"
					: osame ? "PASS, the same map bit for bit" :
						"FAIL, the maps differ");
			}
			else
				printf("  second map: both maps %s, both released by "
					"HDRenderShadowTargetRelease: %s\n",
					bothMade ? "made" : "NOT both made", bothGone ? "PASS" :
						"FAIL");
		}

		/* The time: the bracket with the pass against the bracket alone, each
		 * frame waited for with an event query, so the card has finished what
		 * the frame sent before the clock stops. */
		Shot big;

		if (ShotBegin(big, 2560, 1440))
		{
			IDirect3DQuery9* query = 0;

			if (SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &query)))
			{
				LARGE_INTEGER rate;

				QueryPerformanceFrequency(&rate);

				double mean[2] = { 0.0, 0.0 };

				for (int with = 0; with < 2; with++)
				{
					double total = 0.0;

					for (int f = 0; f < o.shadowFrames + 10; f++)
					{
						LARGE_INTEGER from, to;

						QueryPerformanceCounter(&from);
						g_device->BeginScene();
						HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);

						if (with)
							shadowPass(-1, -1);

						HDRenderFrameEnd();
						g_device->EndScene();
						query->Issue(D3DISSUE_END);

						while (query->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE)
							;

						QueryPerformanceCounter(&to);

						// The first ten warm the card and the caches up.
						if (f >= 10)
							total += (double)(to.QuadPart - from.QuadPart) * 1000.0 /
								(double)rate.QuadPart;
					}

					mean[with] = total / o.shadowFrames;
				}

				printf("  2560x1440 target, %d frames each: bracket alone %.4f ms, with the "
					"cast pass %.4f ms - the pass %.4f ms a frame\n", o.shadowFrames,
					mean[0], mean[1], mean[1] - mean[0]);
				query->Release();
			}
			else
				printf("  no event query on this device - no time\n");
		}

		ShotEnd(big);
		HDRenderShadowTargetRelease();
	}

	/* -shadowtime: the two passes' time on the room of -shadowroom against a
	 * 2560 x 1440 target, the frame being the room's solid art alone against
	 * the room's solid art with the cast and the receive pass after it - and,
	 * with -shadowcreature, a third frame with her cast pass, the second
	 * map's cast pass of the creatures and the two-map receive. Each frame is
	 * waited for with an event query, so the card has finished what the frame
	 * sent before the clock stops; the first ten warm up. The picture's own
	 * projection is used, so -size 2560 1440 frames the time's target as the
	 * picture. */
	if (o.shadowTime > 0 && roomHas)
	{
		printf("\n-- the time of the two passes ---------------------------------\n");

		Shot big;
		IDirect3DQuery9* query = 0;

		if (ShotBegin(big, 2560, 1440) &&
			SUCCEEDED(g_device->CreateQuery(D3DQUERYTYPE_EVENT, &query)))
		{
			LARGE_INTEGER rate;
			double mean[5] = { 0.0, 0.0, 0.0, 0.0, 0.0 };
			int rcalls = 0, ocalls = 0, omodels = 0, mmodels = 0, mcalls = 0;
			const int kinds = placed.empty() ? 2 : 5;

			QueryPerformanceFrequency(&rate);

			for (int with = 0; with < kinds; with++)
			{
				double total = 0.0;

				for (int f = 0; f < o.shadowTime + 10; f++)
				{
					LARGE_INTEGER from, to;

					QueryPerformanceCounter(&from);
					g_device->BeginScene();
					HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
					roomSolid();
					othersDrawn = false;

					if (with == 1 && shadowPass(-1, -1) >= 0)
						rcalls = receivePass();

					if (with >= 2 && (shadowCastDrawn = shadowPass(-1, -1)) >= 0)
					{
						omodels = othersPass();
						ocalls = receivePass();
					}

					/* The models' solid pass, and in the last frame the
					 * models' receive pass after it. */
					if (with >= 3)
					{
						HDModelFrameBegin(&projection, bulbCount, HD_PASS_SOLID);
						modelList(false, -1, -1, false);

						if (with == 4)
						{
							mmodels = modelsPass(-1, -1);
							mcalls = modelCalls;
						}
					}

					HDRenderFrameEnd();
					g_device->EndScene();
					query->Issue(D3DISSUE_END);

					while (query->GetData(0, 0, D3DGETDATA_FLUSH) == S_FALSE)
						;

					QueryPerformanceCounter(&to);

					if (f >= 10)
						total += (double)(to.QuadPart - from.QuadPart) * 1000.0 /
							(double)rate.QuadPart;
				}

				mean[with] = total / o.shadowTime;
			}

			printf("  2560x1440 target, %d frames each: the room alone %.4f ms, with the cast "
				"and the receive pass %.4f ms - the two passes %.4f ms a frame (%d receive "
				"draw calls)\n", o.shadowTime, mean[0], mean[1], mean[1] - mean[0], rcalls);

			if (kinds == 5)
			{
				printf("  with %d creature(s) (%d models into the second map) and the two-map "
					"receive %.4f ms - the three passes %.4f ms a frame, %.4f ms above her two "
					"(%d receive draw calls)\n", (int)placed.size(), omodels, mean[2],
					mean[2] - mean[0], mean[2] - mean[1], ocalls);
				printf("  with the models drawn %.4f ms; with the models' pass after them %.4f ms "
					"- the models' pass %.4f ms a frame (%d models again, %d draw calls)\n",
					mean[3], mean[4], mean[4] - mean[3], mmodels, mcalls);
			}
		}
		else
			printf("  no 2560x1440 target or no event query on this device - no time\n");

		if (query)
			query->Release();

		ShotEnd(big);
	}

	HDModelLevelEnd();
	return 0;
}

/* -------------------------------------------------------- -shadowsurvey -- */

/* One triangle of the classic level's room faces, world units, y down. */
struct SurveyTri
{
	double v[3][3];
	double n[3];		// the unit normal of the side the room sees
	double mn[3], mx[3];
	int room;
	bool twoSided;
};

struct SurveyPlace
{
	int room;
	double x, z;		// room local
};

/* What the survey's terms use: the table's numbers unless -surveyterms gave
 * others. */
struct SurveyTerms
{
	double full, end, ofull, oend;
};

/* Moeller and Trumbore's ray-triangle test: the ray's parameter at the hit,
 * or -1. Both sides of the triangle are hit. */
static double SurveyHit(const SurveyTri& t, const double* o, const double* d)
{
	double e1[3], e2[3], p[3], q[3], s[3];

	for (int i = 0; i < 3; i++)
	{
		e1[i] = t.v[1][i] - t.v[0][i];
		e2[i] = t.v[2][i] - t.v[0][i];
		s[i] = o[i] - t.v[0][i];
	}

	p[0] = d[1] * e2[2] - d[2] * e2[1];
	p[1] = d[2] * e2[0] - d[0] * e2[2];
	p[2] = d[0] * e2[1] - d[1] * e2[0];

	const double det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];

	if (fabs(det) < 1e-9)
		return -1.0;

	const double inv = 1.0 / det;
	const double u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;

	if (u < 0.0 || u > 1.0)
		return -1.0;

	q[0] = s[1] * e1[2] - s[2] * e1[1];
	q[1] = s[2] * e1[0] - s[0] * e1[2];
	q[2] = s[0] * e1[1] - s[1] * e1[0];

	const double v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;

	if (v < 0.0 || u + v > 1.0)
		return -1.0;

	return (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
}

static double SurveySat(double f)
{
	return f > 0.0 ? (f < 1.0 ? f : 1.0) : 0.0;
}

// The key of the 1024-unit cell (x, z) of the world's x-z plane.
static long long SurveyCell(int x, int z)
{
	return ((long long)x << 32) ^ (long long)(unsigned int)z;
}

/* The survey's weight of a face behind the first: her height term for one
 * `below` units under her lowest node, as HDShadowHeightTerm measures, a
 * creature's distance term for one `behind` units along the light from the
 * creature's place, as the second map's lookup measures. */
static double SurveyTerm(const SurveyTerms& terms, bool lara, double below,
	double behind)
{
	return lara ? SurveySat((terms.end - below) / (terms.end - terms.full))
		: SurveySat((terms.oend - behind) / (terms.oend - terms.ofull));
}

/* The survey itself; see the usage block. Returns the process's exit code. */
static int ShadowSurvey(const std::string& trc,
	const std::vector<SurveyPlace>& asked,
	const std::vector<int>& onlyRooms, const std::vector<int>& slots,
		const SurveyTerms& terms,
	const std::string& outPath)
{
	Tr5Level level;
	std::string error;

	if (!level.load(ToW(trc), error))
	{
		printf("shadowsurvey: %s: %s\n", trc.c_str(), error.c_str());
		return 1;
	}

	// How tall the casters are: the rest pose's box (Tr5Level::frameBounds).
	for (size_t i = 0; i < slots.size(); i++)
	{
		int16_t b[6];

		if (level.frameBounds(slots[i], b))
			printf("shadowsurvey: slot %d stands %d units tall (y %d..%d), %d "
				"along a 30 degree "
				"light\n", slots[i], b[3] - b[2], b[2], b[3],
					(int)((b[3] - b[2]) / 0.8660254 + 0.5));
	}

	const std::vector<Tr5Room>& rooms = level.rooms();
	const int roomCount = (int)rooms.size();
	std::vector<char> alternate((size_t)roomCount, 0);

	/* A flipmap's second room stands where its first does, face on face:
	 * only the rooms the level shows without a flip take part. */
	for (int r = 0; r < roomCount; r++)
		if (rooms[(size_t)r].altRoom >= 0 &&
			rooms[(size_t)r].altRoom < roomCount)
			alternate[(size_t)rooms[(size_t)r].altRoom] = 1;

	std::vector<SurveyTri> tris;

	for (int r = 0; r < roomCount; r++)
	{
		if (alternate[(size_t)r])
			continue;

		std::vector<Tr5Level::FaceInfo> faces;

		if (!level.roomFaces(r, faces, error))
			continue;

		for (size_t f = 0; f < faces.size(); f++)
		{
			const Tr5Level::FaceInfo& fi = faces[f];
			const int split[2][3] = { { 0, 1, 2 }, { 0, 2, 3 } };

			for (int k = 0; k < (fi.quad ? 2 : 1); k++)
			{
				SurveyTri t;

				for (int c = 0; c < 3; c++)
					for (int i = 0; i < 3; i++)
						t.v[c][i] = (double)fi.v[split[k][c]][i];

				const double a[3] = { t.v[1][0] - t.v[0][0],
					t.v[1][1] - t.v[0][1],
					t.v[1][2] - t.v[0][2] };
				const double b[3] = { t.v[2][0] - t.v[0][0],
					t.v[2][1] - t.v[0][1],
					t.v[2][2] - t.v[0][2] };

				t.n[0] = a[1] * b[2] - a[2] * b[1];
				t.n[1] = a[2] * b[0] - a[0] * b[2];
				t.n[2] = a[0] * b[1] - a[1] * b[0];

				const double l = sqrt(t.n[0] * t.n[0] + t.n[1] * t.n[1] +
					t.n[2] * t.n[2]);

				if (!(l > 1e-6))
					continue;

				for (int i = 0; i < 3; i++)
				{
					t.n[i] /= l;
					t.mn[i] = t.v[0][i];
					t.mx[i] = t.v[0][i];

					for (int c = 1; c < 3; c++)
					{
						t.mn[i] = t.v[c][i] < t.mn[i] ? t.v[c][i] : t.mn[i];
						t.mx[i] = t.v[c][i] > t.mx[i] ? t.v[c][i] : t.mx[i];
					}
				}

				t.room = r;
				t.twoSided = fi.doubleSided;
				tris.push_back(t);
			}
		}
	}

	/* Which way round the level winds a face: the side a floor shows the
	 * room is up (y negative). Counted on the flat faces that lie at their
	 * sector's floor height, so the answer is the data's, not assumed. */
	int up = 0, down = 0;

	for (size_t i = 0; i < tris.size(); i++)
	{
		const SurveyTri& t = tris[i];

		if (fabs(t.n[1]) < 0.95)
			continue;

		const Tr5Room& rm = rooms[(size_t)t.room];
		const double cx = (t.v[0][0] + t.v[1][0] + t.v[2][0]) / 3.0;
		const double cy = (t.v[0][1] + t.v[1][1] + t.v[2][1]) / 3.0;
		const double cz = (t.v[0][2] + t.v[1][2] + t.v[2][2]) / 3.0;
		const int sx = (int)floor((cx - rm.originX) / 1024.0);
		const int sz = (int)floor((cz - rm.originZ) / 1024.0);
		long y = 0;

		if (!level.cornerFloor(t.room, sx, sz, 0, 0, y))
			continue;

		if (fabs(cy - (double)y) > 300.0)
			continue;

		(t.n[1] < 0.0 ? up : down)++;
	}

	const double wind = up >= down ? 1.0 : -1.0;

	for (size_t i = 0; i < tris.size(); i++)
		for (int k = 0; k < 3; k++)
			tris[i].n[k] *= wind;

	printf("shadowsurvey: %s, %d rooms (%d flip twins left out), %d "
		"triangles; the floors "
		"show the room the side %s winds (%d of %d flat floor faces)\n",
			trc.c_str(), roomCount,
		(int)std::count(alternate.begin(), alternate.end(), (char)1),
			(int)tris.size(),
		wind > 0 ? "the data" : "against the data", up >= down ? up : down, up +
			down);

	// The faces by the 1024-unit cells of the world's x-z plane they touch.
	std::map<long long, std::vector<int> > cells;

	for (size_t i = 0; i < tris.size(); i++)
	{
		const int x0 = (int)floor(tris[i].mn[0] / 1024.0),
			x1 = (int)floor(tris[i].mx[0] / 1024.0);
		const int z0 = (int)floor(tris[i].mn[2] / 1024.0),
			z1 = (int)floor(tris[i].mx[2] / 1024.0);

		for (int x = x0; x <= x1; x++)
			for (int z = z0; z <= z1; z++)
				cells[SurveyCell(x, z)].push_back((int)i);
	}

	// The places: those asked for, or every floor sector of the rooms.
	std::vector<SurveyPlace> places = asked;

	if (places.empty())
		for (int r = 0; r < roomCount; r++)
		{
			const Tr5Room& rm = rooms[(size_t)r];

			if (alternate[(size_t)r] || (rm.flags & 1))
				continue;

			if (!onlyRooms.empty() &&
				std::find(onlyRooms.begin(), onlyRooms.end(),
					r) == onlyRooms.end())
				continue;

			std::vector<Tr5Level::SectorInfo> sectors;

			if (!level.sectorInfo(r, sectors, error))
				continue;

			for (size_t s = 0; s < sectors.size(); s++)
			{
				if (sectors[s].roomBelow != 255)
					continue;

				SurveyPlace p = { r, sectors[s].sx * 1024.0 + 512.0,
					sectors[s].sz * 1024.0 + 512.0 };

				places.push_back(p);
			}
		}

	float fb[3];

	HDShadowFallback(fb);

	const double dir[3] = { fb[0], fb[1], fb[2] };
	const double hl = sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
	const double side[3] = { -dir[2] / hl, 0.0, dir[0] / hl };
	FILE* out = outPath.empty() ? 0 : fopen(outPath.c_str(), "w");
	std::vector<int> stamp(tris.size(), -1);
	int rayId = 0;

	// [caster][kind] - kind 0 through a wall, 1 through a floor.
	int placesAny[2][2] = { { 0, 0 }, { 0, 0 } }, placesSeen[2][2] = { { 0, 0 },
		{ 0, 0 } };
	long raysAny[2][2] = { { 0, 0 }, { 0, 0 } }, raysCured[2] = { 0, 0 },
		rays = 0;
	long belowFirst[24] = { 0 }, belowLeak[24] = { 0 };
	double firstW[2] = { 0.0, 0.0 }, leakW[2][2] = { { 0.0, 0.0 }, { 0.0,
		0.0 } };
	int counted = 0;

	struct Worst { double score; int room; double x, z, w; int kind,
		leakRoom; bool joined; };
	std::vector<Worst> worst;

	for (size_t pi = 0; pi < places.size(); pi++)
	{
		const SurveyPlace& pl = places[pi];

		if (pl.room < 0 || pl.room >= roomCount)
			continue;

		const Tr5Room& rm = rooms[(size_t)pl.room];
		const int sx = (int)floor(pl.x / 1024.0),
			sz = (int)floor(pl.z / 1024.0);
		long c00, c10, c01, c11;

		if (!level.cornerFloor(pl.room, sx, sz, 0, 0, c00) ||
			!level.cornerFloor(pl.room, sx, sz, 1024, 0, c10) ||
			!level.cornerFloor(pl.room, sx, sz, 0, 1024, c01) ||
			!level.cornerFloor(pl.room, sx, sz, 1024, 1024, c11))
			continue;

		const double fx = (pl.x - sx * 1024.0) / 1024.0, fz = (pl.z - sz *
			1024.0) / 1024.0;
		const double feet = c00 * (1 - fx) * (1 - fz) + c10 * fx * (1 - fz) +
			c01 * (1 - fx) * fz +
			c11 * fx * fz;
		const double wx = rm.originX + pl.x, wz = rm.originZ + pl.z;

		counted++;

		/* Her height term measures from her lowest node, not from her soles:
		 * 58.6 units above them, where -shadowroom prints her nodes' lowest
		 * y 398.0 and her soles at 456.6 of her own space. */
		const double node = feet - 58.6;

		for (int caster = 0; caster < 2; caster++)
		{
			/* Her standing height, three clicks of 256 (her rest pose's box
			 * with the HD body is 797, -shadowroom's bounds); a creature's, a
			 * free choice inside the 691 to 809 the man-sized blob carriers
			 * stand (-surveyslots). */
			const double height = caster == 0 ? 768.0 : 760.0;
			double maxW[2] = { 0.0, 0.0 };
			int rayLeak[2] = { 0, 0 };
			int maxRoom[2] = { -1, -1 };
			bool maxJoined[2] = { false, false };

			/* Three columns 96 units apart across the light, inside the
			 * half width of her rest pose's box (x -133..114 in
			 * -shadowroom's bounds); a ray every 32 units of height, a free
			 * choice: 24 a column over her 768. */
			for (int col = -1; col <= 1; col++)
				for (double h = 16.0; h <= height + 0.5; h += 32.0)
				{
					const double o[3] = { wx + side[0] * 96.0 * col, feet - h,
						wz + side[2] * 96.0 * col };
					/* A creature's distance term measures from its place,
					 * the point at its feet, as the second map's green
					 * channel holds it: a face `s` along the ray lies s -
					 * h * dir.y further along the light than the place (the
					 * columns stand across the light and add nothing). How
					 * far a ray is followed: until her height term or the
					 * creature's distance term has ended. */
					const double lift = h * dir[1];
					const double reach = caster == 0 ? (node +
						terms.end - o[1]) / dir[1] : terms.oend + lift;
					const double e[3] = { o[0] + dir[0] * reach, o[1] + dir[1] *
						reach,
						o[2] + dir[2] * reach };
					const int x0 = (int)floor((o[0] < e[0] ? o[0] :
						e[0]) / 1024.0) - 1;
					const int x1 = (int)floor((o[0] > e[0] ? o[0] :
						e[0]) / 1024.0) + 1;
					const int z0 = (int)floor((o[2] < e[2] ? o[2] :
						e[2]) / 1024.0) - 1;
					const int z1 = (int)floor((o[2] > e[2] ? o[2] :
						e[2]) / 1024.0) + 1;
					std::vector<std::pair<double, int> > hits;

					rayId++;
					rays += caster == 0 ? 1 : 0;

					for (int x = x0; x <= x1; x++)
						for (int z = z0; z <= z1; z++)
						{
							std::map<long long,
								std::vector<int> >::const_iterator it =
								cells.find(SurveyCell(x, z));

							if (it == cells.end())
								continue;

							for (size_t k = 0; k < it->second.size(); k++)
							{
								const int ti = it->second[k];

								if (stamp[(size_t)ti] == rayId)
									continue;

								stamp[(size_t)ti] = rayId;

								const double s = SurveyHit(tris[(size_t)ti], o,
									dir);

								if (s > 0.5 && s <= reach)
									hits.push_back(std::make_pair(s, ti));
							}
						}

					if (hits.empty())
						continue;

					std::sort(hits.begin(), hits.end());

					const SurveyTri& first = tris[(size_t)hits[0].second];
					const int kind = fabs(first.n[1]) >= 0.5 ? 1 : 0;
					const double firstBelow = o[1] + dir[1] *
						hits[0].first - feet;

					if (caster == 0 && firstBelow > 0.0)
						belowFirst[firstBelow / 128.0 < 23 ?
							(int)(firstBelow / 128.0) : 23]++;

					/* The first face's own weight, where it lies below her
					 * feet or behind the creature: the shadow a cut of the
					 * terms would take from a face that should have it. More
					 * than 64 below her soles and 256 behind a creature, free
					 * choices: the floor at the feet itself is left out. */
					{
						const double nl = -(first.n[0] * dir[0] + first.n[1] *
							dir[1] +
							first.n[2] * dir[2]);
						const double facing = SurveySat((first.twoSided ?
							fabs(nl) : nl) /
							HD_SHADOW_FACING);
						const double term = SurveyTerm(terms, caster == 0,
							firstBelow + feet - node, hits[0].first - lift);

						if (caster == 0 ? firstBelow > 64.0 :
							hits[0].first > 256.0)
							firstW[caster] += facing * term;
					}

					double rayMax = 0.0;
					int rayRoom = -1;

					// Two hits closer than 2 units are one face drawn twice.
					for (size_t k = 1; k < hits.size(); k++)
					{
						if (hits[k].first - hits[0].first < 2.0)
							continue;

						const SurveyTri& t = tris[(size_t)hits[k].second];
						const double nl = -(t.n[0] * dir[0] + t.n[1] * dir[1] +
							t.n[2] * dir[2]);
						const double facing = SurveySat((t.twoSided ? fabs(nl) :
							nl) /
							HD_SHADOW_FACING);
						const double y = o[1] + dir[1] * hits[k].first;
						const double term = SurveyTerm(terms, caster == 0,
							y - node, hits[k].first - lift);
						const double w = facing * term;

						if (!(w > 0.0))
							continue;

						if (caster == 0)
						{
							const double below = y - feet;

							belowLeak[below / 128.0 < 23 ? (below > 0 ?
								(int)(below / 128.0) : 0) : 23]++;
						}

						if (w > rayMax)
						{
							rayMax = w;
							rayRoom = t.room;
						}

						if (out)
							fprintf(out,
								"%s room %d (%.0f, %.0f) %s ray %d h %.0f: "
								"%s, face of room %d "
								"%.0f units along the light, %.0f below the "
									"feet, weight %.3f\n",
								trc.c_str(), pl.room, pl.x, pl.z, caster == 0 ?
									"lara" : "creature",
								col, h, kind ? "through a floor" :
									"through a wall", t.room,
								hits[k].first, y - feet, w);
					}

					if (rayMax > 0.0)
					{
						const bool joined = rayRoom == pl.room ||
							std::find(rm.portalRooms.begin(),
								rm.portalRooms.end(), rayRoom) !=
								rm.portalRooms.end();

						rayLeak[kind]++;
						raysAny[caster][kind]++;
						leakW[caster][kind] += rayMax;

						if (kind == 0 && !joined)
							raysCured[caster]++;

						if (rayMax > maxW[kind])
						{
							maxW[kind] = rayMax;
							maxRoom[kind] = rayRoom;
							maxJoined[kind] = joined;
						}
					}
				}

			for (int kind = 0; kind < 2; kind++)
			{
				if (rayLeak[kind])
					placesAny[caster][kind]++;

				/* 0.1 of the weight is 0.034 of a pixel's colour at the
				 * fixed light's DARK * LOW, about the least a floor shows. */
				if (maxW[kind] >= 0.1)
					placesSeen[caster][kind]++;

				if (caster == 0 && rayLeak[kind])
				{
					Worst wv = { maxW[kind] * rayLeak[kind], pl.room, pl.x,
						pl.z, maxW[kind], kind,
						maxRoom[kind], maxJoined[kind] };

					worst.push_back(wv);
				}
			}
		}
	}

	if (out)
		fclose(out);

	printf("shadowsurvey: terms full %.0f end %.0f, others full %.0f end "
		"%.0f; %d places, %ld "
		"rays of hers\n", terms.full, terms.end, terms.ofull, terms.oend,
			counted, rays);

	for (int caster = 0; caster < 2; caster++)
		printf("shadowsurvey: %s - through a wall: %d places (%d with a "
			"weight of 0.1 or "
			"more), %ld rays, %ld of them on a room not joined to hers by a "
				"door; through a "
			"floor: %d places (%d), %ld rays\n", caster == 0 ? "lara" :
				"creature",
			placesAny[caster][0], placesSeen[caster][0], raysAny[caster][0],
				raysCured[caster],
			placesAny[caster][1], placesSeen[caster][1], raysAny[caster][1]);

	for (int caster = 0; caster < 2; caster++)
		printf("shadowsurvey: %s - summed weight: the first faces below her "
			"feet (behind "
			"the creature by more than 256) %.1f, the faces behind them "
				"through a wall %.1f, "
			"through a floor %.1f\n", caster == 0 ? "lara" : "creature",
				firstW[caster],
			leakW[caster][0], leakW[caster][1]);

	printf("shadowsurvey: her first face below her feet, 128-unit bins:");

	for (int i = 0; i < 24; i++)
		printf(" %ld", belowFirst[i]);

	printf("\nshadowsurvey: a face behind it below her feet, 128-unit bins:");

	for (int i = 0; i < 24; i++)
		printf(" %ld", belowLeak[i]);

	printf("\n");

	std::sort(worst.begin(), worst.end(), [](const Worst& a, const Worst& b) {
		return a.score > b.score; });

	for (int kind = 0; kind < 2; kind++)
	{
		int shown = 0;

		for (size_t i = 0; i < worst.size() && shown < 3; i++)
		{
			if (worst[i].kind != kind)
				continue;

			printf("shadowsurvey: worst through a %s: room %d (%.0f, %.0f), "
				"weight %.3f, the face "
				"in room %d, %s\n", kind ? "floor" : "wall", worst[i].room,
					worst[i].x, worst[i].z,
				worst[i].w, worst[i].leakRoom, worst[i].joined ?
					"joined by a door" : "not joined");
			shown++;
		}
	}

	return 0;
}

/* The Mods folder's notes, on stdout. hd_mods.cpp is
 * engine-free and keeps its notes until somebody says where they go; the
 * engine sends them to Log(), this tool prints them. */
static void ProbeModsLine(const char* line)
{
	printf("  %s\n", line);
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		printf("usage: ModelProbe <5-folder> [-name <NAME> | -slot <n> | -all]"
			" [-level <LEVEL>] [options]\n"
			"       see the head of ModelProbe.cpp for the whole list\n");
		return 2;
	}

	/* -shadowsurvey runs on the classic level alone, before any device or
	 * install is asked for. */
	for (int i = 1; i < argc; i++)
	{
		if (_stricmp(argv[i], "-shadowsurvey") || i + 1 >= argc)
			continue;

		std::vector<SurveyPlace> places;
		std::vector<int> onlyRooms, slots;
		SurveyTerms terms = { HD_SHADOW_FULL, HD_SHADOW_END,
			HD_SHADOW_OTHERS_FULL,
			HD_SHADOW_OTHERS_END };
		std::string surveyOut;

		for (int k = 1; k < argc; k++)
		{
			if (!_stricmp(argv[k], "-surveyplace") && k + 3 < argc)
			{
				SurveyPlace p = { atoi(argv[k + 1]), atof(argv[k + 2]),
					atof(argv[k + 3]) };

				places.push_back(p);
				k += 3;
			}
			else if (!_stricmp(argv[k], "-surveyrooms") && k + 1 < argc)
			{
				for (const char* c = argv[++k]; *c; )
				{
					onlyRooms.push_back(atoi(c));

					while (*c && *c != ',')
						c++;

					if (*c == ',')
						c++;
				}
			}
			else if (!_stricmp(argv[k], "-surveyterms") && k + 4 < argc)
			{
				terms.full = atof(argv[k + 1]);
				terms.end = atof(argv[k + 2]);
				terms.ofull = atof(argv[k + 3]);
				terms.oend = atof(argv[k + 4]);
				k += 4;
			}
			else if (!_stricmp(argv[k], "-surveyslots") && k + 1 < argc)
			{
				for (const char* c = argv[++k]; *c; )
				{
					slots.push_back(atoi(c));

					while (*c && *c != ',')
						c++;

					if (*c == ',')
						c++;
				}
			}
			else if (!_stricmp(argv[k], "-surveyout") && k + 1 < argc)
				surveyOut = argv[++k];
		}

		return ShadowSurvey(argv[i + 1], places, onlyRooms, slots, terms,
			surveyOut);
	}

	std::string root = argv[1];
	std::string name, levelStem, classicPath, out = ".\\modelprobe";
	/* -withrooms: load that level's room pages first, so the model page count
	 * that comes out is the one the engine really pays - most of a level's
	 * model pages are pages its rooms already wear, and appending them to a
	 * cache that already holds the rooms is exactly what HDLevelBuild does. */
	std::string withRooms;
	/* The Mods folder and the other bundle. Both empty by
	 * default, which is exactly the behaviour this tool has without -mods.
	 */
	std::string modsFolder, tr3Root;
	// -wardrobe: print the wardrobe scan and stop.
	bool wardrobe = false;
	/* -classicpose: print the classic pose of one model name and stop.
	 * Zero game means the switch was not on the line. */
	int poseGame = 0;
	std::string poseName;
	// ... and -noclassicpose: the ring as it was before that pose existed.
	bool noClassicPose = false;
	int slot = -1;
	bool all = false, nodes = false, noDraw = false, managed = false, head = false;
	// -all's own table is printed only for a line that asked for -all;
	// -floorcheck turns `all` on for the load and prints a table of its own.
	bool allAsked = false;
	// -invrot, the ring's own x/y/z for this item.
	short invRot[3] = { 0, 0, 0 };
	bool invRotSet = false;
	// -axisview, the model face-on to its own plane.
	bool axisView = false;
	// ... and -rigid, a -name model loaded the way the ring loads a mod's.
	bool rigid = false;
	bool itemcheck = false;
	bool inventory = false;	// -inventory: a mod's own _OPTION model
	/* -floor: the floor rule. -floor draws the slot the way the level draws it
	 * on the ground, lift and all, with the classic mesh's own lowest plane
	 * over the picture; -floorcheck prints the numbers for every pickup slot
	 * of the level and draws nothing. */
	bool floorDraw = false;
	bool floorCheck = false;
	/* -bindcheck: the skeleton test taken apart (PrintBindCheck);
	 * -animframe <anim> <key>: the picture in that keyframe of one of the
	 * slot's own animations, numbered from the slot's first. */
	bool bindCheck = false, crossDrift = false, slideCensus = false;
	/* -bones: every bone of the .TRM against the slot's classic nodes;
	 * -model <NAME>: -slot's model is that .TRM instead of the level's own
	 * binding - a candidate held against a slot's classic tree. */
	bool bonesAsked = false;
	std::string modelAsked;
	// -alpha: the glass rule taken apart; -noglass: the rule left off.
	bool alphaCheck = false, noGlass = false;
	int itemAnim = -1, itemKey = 0;
	double itemPush = 0.0;
	bool noPocket = false;
	int itemCull = -1;
	int animAsk = -1, animKey = 0;
	/* ... and the before picture: the same draw with the lift left off, which
	 * is what the floor showed before the lift existed. The switch exists so that
	 * what the rule buys stays a measurement instead of a memory. */
	bool noFloorLift = false;
	/* The inventory ring. -ringcheck is the numeric proof
	 * of the shear, -ring <x> <y> draws the model where the ring would put it
	 * so that the same shear can be looked at. */
	bool ringcheck = false;
	/* The pickup display in the corner. The same two
	 * halves -ringcheck measures - the place and the light - asked at the one
	 * place the display stands, and a third one the ring never needed: the
	 * device state bracket, because the display opens one of its own in the
	 * middle of a classic frame. */
	bool pickupcheck = false;
	bool ringSet = false;
	bool ringLight = false;
	double ringX = 0.0, ringY = 0.0;

	/* Lara's HD composite. */
	int onlyPage = -1;		// -onlypage: -name with one page
	bool lara = false, laraNodes = false, mirror = false;
	bool laraFirstPerson = false, laraCheck = false, laraNeck = false;
	bool laraHeadWeight = false;	// -headw
	int laraHandFold = -1;		// -handfold
	bool laraNoFold = false;	// -nofold: the before picture
	bool laraNoHandMix = false;	// -nohandmix: and the other half of it
	bool laraXray = false;		// -xray: the X-ray look of the skeleton cheat
	unsigned int laraPasses = HD_PASS_ALL;	// -passes solid|additive
	int laraHead = -1;		// -lara-head: -1 = the wardrobe's own answer
	bool laraPoseRisk = false;	// -poserisk
	std::vector<int> laraTween60;	// -tween60
	bool laraHandLight = false;	// -look wrist: the wrist seam
	bool laraOldSkin = false;	// -oldskin: the before picture
	bool classicBraidRoot = false;	// -classicroot: the before picture
	int onlyPart = -1;		// -onlypart: one part alone
	bool noSheen = false;		// -nosheen: the before picture
	bool softTest = false;		// -softtest: the chest's motion at a fixed offset
	float softOffset[3] = { 0.0f, 0.0f, 0.0f };
	// -shadowmap, -shadowcast, -shadowdevice: the projected shadow's map.
	bool shadowMap = false, shadowCast = false, shadowDevice = false;
	float shadowDir[3] = { 0.35f, 1.0f, 0.2f };
	int shadowFrames = 300;
	// -shadowroom, -shadowreceive, -shadowempty, -shadownoguard, -shadowtime.
	int shadowRoom = -1;
	float shadowAt[2] = { 0.0f, 0.0f };
	bool shadowReceive = false, shadowEmpty = false, shadowNoGuard = false;
	int shadowTime = 0;
	// -shadowcreature, -shadowothers: the second map's casters and its picture.
	std::vector<LaraOptions::Creature> shadowCreatures;
	bool shadowOthers = false;
	// -shadowitem, -shadownomodels, -shadowcount: the models' receive.
	bool shadowNoModels = false, shadowCount = false;
	float shadowAlong = 0.0f;
	// -shadowunseen, -shadowmirror, -shadowmirrorview, -shadownomirror.
	bool shadowUnseen = false, shadowMirror = false, shadowMirrorView = false;
	bool shadowNoMirror = false, shadowMirrorMask = false;
	bool laraBraid = false;
	int laraBraidSim = 0;		// -braidsim: the braid's own physics
	/* -braidanim: the same chain under a real animation. */
	std::vector<int> laraBraidAnims;
	bool laraBraidFloor = true;
	/* -1e9 means "the engine's own constant when -firstperson is
	 * on", which is what makes the tool's default picture the game's picture;
	 * a number given on the line overrides it, and a negative one is the
	 * before picture. */
	double cutS = -1.0e9;
	bool noCap = false;		// -nocap: without the lid
	double fpForward = 48.0;
	double eyeOff[3] = { 0.0, 0.0, 0.0 };	// -eye
	int cullMode = -1;		// -1 = "what -firstperson would use"
	bool yawSet = false, pitchSet = false;
	std::string outfitName, gunsName, backName, holstersName, lookName;
	bool holstersFull = true, flare = false, torch = false;
	int threads = 8;
	long width = 1024, height = 1024;
	double fov = 80.0, yaw = 180.0, pitch = 10.0, dist = 0.0;
	double shade = 128.0;
	double shadeRgb[3] = { 128.0, 128.0, 128.0 };

	double turn[32][2];
	memset(turn, 0, sizeof(turn));

	HDObjLight lights[HD_MAX_OBJ_LIGHTS];
	int lightCount = 0;
	unsigned int nodeMask = ~0u;
	// -lips: a speaking face at a frame of its track. See LipsAsk.
	LipsAsk lips;
	// -loop and -fire: the loop between lines and the firing face.
	const char* loopAsk = 0;
	const char* fireAsk = 0;

	for (int i = 2; i < argc; i++)
	{
		if (!_stricmp(argv[i], "-name") && i + 1 < argc)
			name = argv[++i];
		else if (!_stricmp(argv[i], "-lips") && i + 2 < argc)
		{
			const char* file = argv[++i];
			const char* frames = argv[++i];

			if (!LipsParse(file, frames, lips))
				printf("ModelProbe: -lips %s %s names no <FACE>_<track>.TRL and no "
					"frame - ignored\n", file, frames);
		}
		else if (!_stricmp(argv[i], "-loop") && i + 1 < argc)
			loopAsk = argv[++i];
		else if (!_stricmp(argv[i], "-fire") && i + 1 < argc)
			fireAsk = argv[++i];
		else if (!_stricmp(argv[i], "-slot") && i + 1 < argc)
			slot = atoi(argv[++i]);
		else if (!_stricmp(argv[i], "-level") && i + 1 < argc)
			levelStem = argv[++i];
		else if (!_stricmp(argv[i], "-classic") && i + 1 < argc)
			classicPath = argv[++i];
		else if (!_stricmp(argv[i], "-wardrobe"))
			wardrobe = true;
		else if (!_stricmp(argv[i], "-mods") && i + 1 < argc)
			modsFolder = argv[++i];
		else if (!_stricmp(argv[i], "-tr3") && i + 1 < argc)
			tr3Root = argv[++i];
		// The pose the ring will apply, out of hd_classic.
		else if (!_stricmp(argv[i], "-classicpose") && i + 2 < argc)
		{
			poseGame = atoi(argv[++i]);
			poseName = argv[++i];
		}
		else if (!_stricmp(argv[i], "-noclassicpose"))
			noClassicPose = true;
		else if (!_stricmp(argv[i], "-withrooms") && i + 1 < argc)
			withRooms = argv[++i];
		else if (!_stricmp(argv[i], "-all"))
			all = allAsked = true;
		else if (!_stricmp(argv[i], "-lara") && i + 1 < argc)
		{
			lara = true;
			levelStem = argv[++i];
		}
		else if (!_stricmp(argv[i], "-outfit") && i + 1 < argc)
		{
			lara = true;
			outfitName = argv[++i];
		}
		else if (!_stricmp(argv[i], "-guns") && i + 1 < argc)
			gunsName = argv[++i];
		else if (!_stricmp(argv[i], "-back") && i + 1 < argc)
			backName = argv[++i];
		else if (!_stricmp(argv[i], "-holsters") && i + 1 < argc)
		{
			holstersName = argv[++i];
			holstersFull = !_stricmp(holstersName.c_str(), "full");
		}
		else if (!_stricmp(argv[i], "-look") && i + 1 < argc)
			lookName = argv[++i];
		else if (!_stricmp(argv[i], "-flare"))
			flare = true;
		else if (!_stricmp(argv[i], "-torch"))
			torch = true;
		else if (!_stricmp(argv[i], "-lara-nodes"))
		{
			lara = true;
			laraNodes = true;
		}
		else if (!_stricmp(argv[i], "-mirror"))
			mirror = true;
		else if (!_stricmp(argv[i], "-firstperson"))
		{
			lara = true;
			laraFirstPerson = true;
		}
		/* The head cut's threshold, of 1. Negative
		 * is no cut at all, which is the before picture. */
		else if (!_stricmp(argv[i], "-headcut") && i + 1 < argc)
			cutS = atof(argv[++i]);
		/* The picture with the hole and no lid. */
		else if (!_stricmp(argv[i], "-nocap"))
			noCap = true;
		else if (!_stricmp(argv[i], "-fpforward") && i + 1 < argc)
			fpForward = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-cull") && i + 1 < argc)
		{
			const char* what = argv[++i];

			cullMode = !_stricmp(what, "cw") ? HD_MODEL_CULL_CW
				: !_stricmp(what, "ccw") ? HD_MODEL_CULL_CCW
				: !_stricmp(what, "back") ? HD_MODEL_CULL_BACK
				: HD_MODEL_CULL_NONE;
		}
		else if (!_stricmp(argv[i], "-neck"))
		{
			lara = true;
			laraNeck = true;
		}
		/* The head's own weight, per outfit, and the
		 * rim it leaves - the measurement the weight cut is built on. */
		else if (!_stricmp(argv[i], "-headw"))
		{
			lara = true;
			laraHeadWeight = true;
		}
		/* The mirrored hand weights, and what folding them
		 * back costs in this pose. An optional bar in per mille; without one
		 * the engine's own HD_LARA_HAND_FOLD_BAR. */
		else if (!_stricmp(argv[i], "-handfold"))
		{
			lara = true;
			laraHandFold = HD_LARA_HAND_FOLD_BAR;

			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				laraHandFold = atoi(argv[++i]);
		}
		/* Build the hands from the file exactly as it stands,
		 * which is the picture from before the hand fold existed. A share of 1001 is one
		 * no vertex can reach. */
		else if (!_stricmp(argv[i], "-nofold"))
		{
			lara = true;
			laraNoFold = true;
		}
		/* A mod's hand model drawn whole, in the mod's own
		 * skin - the other half of that before picture. */
		else if (!_stricmp(argv[i], "-nohandmix"))
		{
			lara = true;
			laraNoHandMix = true;
		}
		/* The X-ray look: the level's X-ray set (hd_lara.cpp, kXray) planned
		 * and drawn in place of the outfit's parts, which is what the game
		 * draws while lara.skelebob holds in a level with a SKELETON
		 * trigger. */
		else if (!_stricmp(argv[i], "-xray"))
		{
			lara = true;
			laraXray = true;
		}
		/* The runs of Lara's picture, one at a time: what the opaque bones
		 * and what the additive hull of the X-ray body each put on the
		 * picture. */
		else if (!_stricmp(argv[i], "-passes") && i + 1 < argc)
		{
			const char* what = argv[++i];

			laraPasses = !_stricmp(what, "solid") ? HD_PASS_SOLID
				: !_stricmp(what, "additive") ? HD_PASS_ADDITIVE : HD_PASS_ALL;
		}
		else if (!_stricmp(argv[i], "-bg") && i + 1 < argc)
		{
			int r = 0, g = 0, b = 0;

			if (sscanf_s(argv[++i], "%d,%d,%d", &r, &g, &b) == 3)
				g_clear = (unsigned long)(((r & 255) << 16) | ((g & 255) << 8)
					| (b & 255));
			else
				printf("ModelProbe: -bg wants r,g,b - '%s' ignored\n", argv[i]);
		}
		/* The head rule this picture is drawn with, whatever the
		 * wardrobe says - "outfit" is the picture from before the head rule existed.
		 *
		 * Not "-head": this tool's -head means "draw the
		 * separate <NAME>_HEAD.TRM as well", so this switch takes the name its
		 * neighbours in the composite block have - -lara-nodes, -lara-pose. */
		else if (!_stricmp(argv[i], "-lara-head") && i + 1 < argc)
		{
			lara = true;
			i++;

			if (!_stricmp(argv[i], "tr5"))
				laraHead = HD_WARDROBE_HEAD_TR5;
			else if (!_stricmp(argv[i], "hair"))
				laraHead = HD_WARDROBE_HEAD_HAIR;
			else if (!_stricmp(argv[i], "outfit") || !_stricmp(argv[i], "own"))
				laraHead = HD_WARDROBE_HEAD_OUTFIT;
			else
				printf("ModelProbe: -lara-head %s is none of tr5, hair or "
					"outfit - ignored\n", argv[i]);
		}
		/* The eye off the head joint, for the frame
		 * a landing gives - the camera outside the skull, the nape in front
		 * of it. Three numbers, in classic units. */
		else if (!_stricmp(argv[i], "-eye") && i + 3 < argc)
		{
			eyeOff[0] = atof(argv[++i]);
			eyeOff[1] = atof(argv[++i]);
			eyeOff[2] = atof(argv[++i]);
		}
		/* Which key frame pairs a matrix
		 * blend cannot survive, and whether a packed angle ever wraps. */
		else if (!_stricmp(argv[i], "-poserisk"))
		{
			lara = true;
			laraPoseRisk = true;
		}
		/* Replay these animations at sixty, frame by
		 * frame, and say which drawn frame is an outlier between its own two
		 * neighbours. With no number it takes the sprint dive (230, 232, 240,
		 * 242, 308, 309), the duck roll (218, 219, 220, 247) and the standing
		 * roll (146, 147). */
		else if (!_stricmp(argv[i], "-tween60"))
		{
			lara = true;

			while (i + 1 < argc && (argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9'))
				laraTween60.push_back(atoi(argv[++i]));

			if (laraTween60.empty())
			{
				static const int kRolls[] =
					{ 146, 147, 218, 219, 220, 247, 230, 232, 240, 242, 308, 309 };

				for (size_t k = 0; k < sizeof(kRolls) / sizeof(kRolls[0]); k++)
					laraTween60.push_back(kRolls[k]);
			}
		}
		/* What the body and the hand shine like where
		 * the two meet, for the outfit this run wears. */
		else if (!_stricmp(argv[i], "-handlight"))
		{
			lara = true;
			laraHandLight = true;
		}
		/* The before picture - her skin shaded by the
		 * outfit's material again, as it was before the split. */
		else if (!_stricmp(argv[i], "-oldskin"))
		{
			lara = true;
			laraOldSkin = true;
		}
		else if (!_stricmp(argv[i], "-classicroot"))
			classicBraidRoot = true;
		/* The first person picture without the inner
		 * skin, which is what the cut and the culling leave on their own -
		 * a hole one sees the room through. */
		else if (!_stricmp(argv[i], "-onlypart") && i + 1 < argc)
		{
			lara = true;
			onlyPart = atoi(argv[++i]);
		}
		/* Draw the braid where the simulation leaves it
		 * rather than in a studio pose. The number is a -braidsim regime; with
		 * none it is 7, which is what the engine ships (the box, root -33).
		 * Regime 4 is the picture before the box existed - root -48 and the two
		 * pack spheres. */
		else if (!_stricmp(argv[i], "-braidrest"))
		{
			lara = true;
			g_braidRestWant = 7;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				g_braidRestWant = atoi(argv[++i]);

			if (g_braidRestWant < 0 || g_braidRestWant > 7)
				g_braidRestWant = 7;

			if (laraBraidSim <= 0)
				laraBraidSim = 60;
		}
		else if (!_stricmp(argv[i], "-nosheen"))
			noSheen = true;
		else if (!_stricmp(argv[i], "-softtest") && i + 3 < argc)
		{
			softTest = true;
			softOffset[0] = (float)atof(argv[++i]);
			softOffset[1] = (float)atof(argv[++i]);
			softOffset[2] = (float)atof(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-shadowmap"))
			shadowMap = true;
		else if (!_stricmp(argv[i], "-shadowcast"))
			shadowCast = true;
		else if (!_stricmp(argv[i], "-shadowdevice"))
		{
			shadowDevice = true;

			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shadowFrames = atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-shadowdir") && i + 3 < argc)
		{
			shadowDir[0] = (float)atof(argv[++i]);
			shadowDir[1] = (float)atof(argv[++i]);
			shadowDir[2] = (float)atof(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-shadowroom") && i + 3 < argc)
		{
			shadowRoom = atoi(argv[++i]);
			shadowAt[0] = (float)atof(argv[++i]);
			shadowAt[1] = (float)atof(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-shadowreceive"))
			shadowReceive = true;
		else if (!_stricmp(argv[i], "-shadowempty"))
			shadowReceive = shadowEmpty = true;
		else if (!_stricmp(argv[i], "-shadownoguard"))
			shadowReceive = shadowNoGuard = true;
		else if (!_stricmp(argv[i], "-shadowtime"))
		{
			shadowTime = 300;

			if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9')
				shadowTime = atoi(argv[++i]);
		}
		else if ((!_stricmp(argv[i], "-shadowcreature") || !_stricmp(argv[i], "-shadowitem")) &&
			i + 3 < argc)
		{
			LaraOptions::Creature c;

			c.blob = !_stricmp(argv[i], "-shadowcreature");
			c.slot = argv[++i];
			c.at[0] = (float)atof(argv[++i]);
			c.at[1] = (float)atof(argv[++i]);
			c.yaw = 0.0f;

			// An optional turn, a number (a minus sign included).
			if (i + 1 < argc && ((argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') ||
				(argv[i + 1][0] == '-' && argv[i + 1][1] >= '0' && argv[i + 1][1] <= '9')))
				c.yaw = (float)atof(argv[++i]);

			if (shadowCreatures.size() < 8)
				shadowCreatures.push_back(c);
			else
				printf("ModelProbe: -shadowcreature and -shadowitem take eight at most - %s "
					"left out\n", c.slot.c_str());
		}
		else if (!_stricmp(argv[i], "-shadowothers"))
			shadowOthers = true;
		else if (!_stricmp(argv[i], "-shadownomodels"))
			shadowReceive = shadowNoModels = true;
		else if (!_stricmp(argv[i], "-shadowcount"))
			shadowReceive = shadowCount = true;
		else if (!_stricmp(argv[i], "-shadowalong") && i + 1 < argc)
			shadowAlong = (float)atof(argv[++i]);
		else if (!_stricmp(argv[i], "-shadowpresence") && i + 1 < argc)
			g_probe_presence = HDShadowPresence((float)atof(argv[++i]));
		else if (!_stricmp(argv[i], "-shadowfog") && i + 1 < argc)
			g_probe_fog = (float)atof(argv[++i]);
		else if (!_stricmp(argv[i], "-shadowwith") && i + 1 < argc)
		{
			for (const char* c = argv[++i]; *c; )
			{
				g_probe_with.push_back(atoi(c));

				while (*c && *c != ',')
					c++;

				if (*c == ',')
					c++;
			}
		}
		else if (!_stricmp(argv[i], "-shadowunseen"))
			shadowUnseen = true;
		else if (!_stricmp(argv[i], "-shadowmirror"))
			shadowMirror = true;
		else if (!_stricmp(argv[i], "-shadowmirrorview"))
			shadowMirror = shadowMirrorView = true;
		else if (!_stricmp(argv[i], "-shadownomirror"))
			shadowMirror = shadowNoMirror = true;
		else if (!_stricmp(argv[i], "-shadowmirrormask"))
			shadowMirror = shadowMirrorMask = shadowReceive = true;
		else if (!_stricmp(argv[i], "-braid"))
		{
			lara = true;
			laraBraid = true;
		}
		/* The braid's own physics, run offline. The
		 * step count is optional - 60 is a second of game time. */
		else if (!_stricmp(argv[i], "-braidsim"))
		{
			lara = true;
			laraBraidSim = 60;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				laraBraidSim = atoi(argv[++i]);

			if (laraBraidSim < 1)
				laraBraidSim = 60;
		}
		/* The same chain driven by a real animation.
		 * Repeat the switch for more of them; with no number it runs the
		 * three a player spends most of the time in - breathing on the spot,
		 * the run loop and the walk loop. */
		else if (!_stricmp(argv[i], "-braidanim"))
		{
			lara = true;

			if (i + 1 < argc && argv[i + 1][0] != '-')
				laraBraidAnims.push_back(atoi(argv[++i]));
			else
			{
				laraBraidAnims.push_back(ANIM_BREATH_NUMBER);
				laraBraidAnims.push_back(0);	// the run loop
				laraBraidAnims.push_back(1);	// the walk loop
			}
		}
		else if (!_stricmp(argv[i], "-nobraidfloor"))
			laraBraidFloor = false;
		else if (!_stricmp(argv[i], "-laracheck"))
		{
			lara = true;
			laraCheck = true;
		}
		else if (!_stricmp(argv[i], "-nodes"))
			nodes = true;
		else if (!_stricmp(argv[i], "-bindcheck"))
			bindCheck = true;
		else if (!_stricmp(argv[i], "-bones"))
			bonesAsked = true;
		else if (!_stricmp(argv[i], "-model") && i + 1 < argc)
			modelAsked = argv[++i];
		else if (!_stricmp(argv[i], "-alpha"))
			alphaCheck = true;
		else if (!_stricmp(argv[i], "-noglass"))
			noGlass = true;
		else if (!_stricmp(argv[i], "-crossdrift"))
			crossDrift = true;
		else if (!_stricmp(argv[i], "-slidecensus"))
			slideCensus = true;
		else if (!_stricmp(argv[i], "-itemanim") && i + 2 < argc)
		{
			itemAnim = atoi(argv[++i]);
			itemKey = atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-itempush") && i + 1 < argc)
			itemPush = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-nopocket"))
			noPocket = true;
		else if (!_stricmp(argv[i], "-itemcull") && i + 1 < argc)
			itemCull = !_stricmp(argv[++i], "back") ? HD_MODEL_CULL_BACK
				: HD_MODEL_CULL_NONE;
		else if (!_stricmp(argv[i], "-animframe") && i + 2 < argc)
		{
			animAsk = atoi(argv[++i]);
			animKey = atoi(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-classicdots"))
			g_dotsAsked = true;
		/* Every slot of the page cache, so that the cost of a
		 * second outfit is a set difference and not a guess. */
		else if (!_stricmp(argv[i], "-pagelist"))
			g_pagelist = true;
		else if (!_stricmp(argv[i], "-nodraw"))
			noDraw = true;
		else if (!_stricmp(argv[i], "-managed"))
			managed = true;
		else if (!_stricmp(argv[i], "-onlypage") && i + 1 < argc)
			onlyPage = (int)strtol(argv[++i], 0, 0);
		else if (!_stricmp(argv[i], "-head"))
			head = true;
		/* A mod's own <BASE>_OPTION model in the ring's
		 * place, exactly as hd_object.cpp puts it there. */
		else if (!_stricmp(argv[i], "-inventory"))
			inventory = true;
		/* The floor rule, as a picture and as a table. -floorcheck
		 * wants every slot of the level loaded, which is what -all does - but
		 * not -all's own table, unless the line asked for that too. */
		else if (!_stricmp(argv[i], "-floor"))
			floorDraw = true;
		else if (!_stricmp(argv[i], "-floorcheck"))
			floorCheck = all = true;
		else if (!_stricmp(argv[i], "-nofloorlift"))
			noFloorLift = floorDraw = true;
		/* The ring's own three angles, so that a picture out
		 * of this tool is the picture the player sees. */
		else if (!_stricmp(argv[i], "-invrot") && i + 3 < argc)
		{
			invRot[0] = (short)strtol(argv[++i], 0, 0);
			invRot[1] = (short)strtol(argv[++i], 0, 0);
			invRot[2] = (short)strtol(argv[++i], 0, 0);
			invRotSet = true;
		}
		/* The model turned so that its own
		 * principal plane faces the camera. See the usage block. */
		else if (!_stricmp(argv[i], "-axisview"))
			axisView = true;
		// ... and -rigid. See the usage block.
		else if (!_stricmp(argv[i], "-rigid"))
			rigid = true;
		else if (!_stricmp(argv[i], "-itemcheck"))
			itemcheck = true;
		else if (!_stricmp(argv[i], "-ringcheck"))
			ringcheck = true;
		else if (!_stricmp(argv[i], "-pickupcheck"))
			pickupcheck = true;
		else if (!_stricmp(argv[i], "-ring") && i + 2 < argc)
		{
			ringX = atof(argv[++i]);
			ringY = atof(argv[++i]);
			ringSet = true;
		}
		else if (!_stricmp(argv[i], "-threads") && i + 1 < argc)
			threads = HDPoolThreads(atoi(argv[++i]));
		else if (!_stricmp(argv[i], "-out") && i + 1 < argc)
			out = argv[++i];
		else if (!_stricmp(argv[i], "-size") && i + 2 < argc)
		{
			width = atol(argv[++i]);
			height = atol(argv[++i]);
		}
		else if (!_stricmp(argv[i], "-fov") && i + 1 < argc)
			fov = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-yaw") && i + 1 < argc)
		{
			yaw = atof(argv[++i]);
			yawSet = true;
		}
		else if (!_stricmp(argv[i], "-pitch") && i + 1 < argc)
		{
			pitch = atof(argv[++i]);
			pitchSet = true;
		}
		else if (!_stricmp(argv[i], "-dist") && i + 1 < argc)
			dist = atof(argv[++i]);
		else if (!_stricmp(argv[i], "-under") && i + 1 < argc)
		{
			float* u = g_probe_under;

			g_probe_under_set = sscanf_s(argv[++i],
				"%f,%f,%f,%f,%f,%f,%f,%f,%f", &u[0], &u[1], &u[2], &u[3],
				&u[4], &u[5], &u[6], &u[7], &u[8]) == 9;

			if (!g_probe_under_set)
				printf("ModelProbe: -under wants nine numbers - '%s' "
					"ignored\n", argv[i]);
		}
		else if (!_stricmp(argv[i], "-shade") && i + 1 < argc)
		{
			double rgb[3];

			if (sscanf_s(argv[++i], "%lf,%lf,%lf", &rgb[0], &rgb[1],
				&rgb[2]) == 3)
			{
				shade = rgb[0];
				shadeRgb[0] = rgb[0];
				shadeRgb[1] = rgb[1];
				shadeRgb[2] = rgb[2];
			}
			else
				shade = shadeRgb[0] = shadeRgb[1] = shadeRgb[2] =
					atof(argv[i]);
		}
		else if (!_stricmp(argv[i], "-bulb") && i + 5 < argc)
		{
			const bool coloured = i + 8 < argc && argv[i + 6][0] != '-';

			if (g_probe_bulb_count < HD_MAX_BULBS)
			{
				HDFogBulb& bulb = g_probe_bulbs[g_probe_bulb_count++];

				bulb.x = (float)atof(argv[i + 1]);
				bulb.y = (float)atof(argv[i + 2]);
				bulb.z = (float)atof(argv[i + 3]);
				bulb.rad = (float)atof(argv[i + 4]);
				bulb.density = (float)atof(argv[i + 5]);
				bulb.r = coloured ? (float)atof(argv[i + 6]) : 1.0f;
				bulb.g = coloured ? (float)atof(argv[i + 7]) : 1.0f;
				bulb.b = coloured ? (float)atof(argv[i + 8]) : 1.0f;
			}
			else
				printf("ModelProbe: more than %d fog bulbs - the extra one is "
					"dropped\n", HD_MAX_BULBS);

			i += coloured ? 8 : 5;
		}
		else if ((!_stricmp(argv[i], "-pose") || !_stricmp(argv[i], "-lara-pose"))
			&& i + 3 < argc)
		{
			const int node = atoi(argv[++i]);
			const double y = atof(argv[++i]);
			const double p = atof(argv[++i]);

			if (node >= 0 && node < 32)
			{
				turn[node][0] = y;
				turn[node][1] = p;
			}
		}
		else if (!_stricmp(argv[i], "-meshbits") && i + 1 < argc)
			nodeMask = (unsigned int)strtoul(argv[++i], 0, 0);
		else if (!_stricmp(argv[i], "-larabits") && i + 1 < argc)
		{
			const unsigned long bits = strtoul(argv[++i], 0, 0);

			nodeMask = HDLaraNodesShown(bits);
			printf("larabits: mesh_bits %08lx shows nodes %04x of her 15\n",
				bits, nodeMask);
		}
		else if (!_stricmp(argv[i], "-light") && i + 7 < argc && lightCount < HD_MAX_OBJ_LIGHTS)
		{
			/* x y z r g b range - a POINT light in view space, which is what
			 * the object shader wants. The studio camera
			 * looks down -z at the model, so "0 0 -2000" is in front of it. */
			HDObjLight& light = lights[lightCount++];

			light.x = (float)atof(argv[++i]);
			light.y = (float)atof(argv[++i]);
			light.z = (float)atof(argv[++i]);
			light.kind = (float)HD_OBJLIGHT_POINT;
			light.r = (float)atof(argv[++i]);
			light.g = (float)atof(argv[++i]);
			light.b = (float)atof(argv[++i]);

			const float range = (float)atof(argv[++i]);

			light.invRange = range > 0.0f ? 1.0f / range : 0.0f;
		}
		else if (!_stricmp(argv[i], "-ringlight") && i + 1 < argc &&
			lightCount < HD_MAX_OBJ_LIGHTS)
		{
			/* The inventory ring's own light, which is
			 * the whole of it: an ambient of 8/8/8 and one sun of the ring's
			 * pcbright, direction (0, -50, -100) normalised, doubled - the
			 * three lines phd_PutPolygonsPickup sets up (specific\output.cpp)
			 * and the ones HDInvEnd hands the model. `bright` is the ring's
			 * own value: 32 for an item off to the side, 160 for the selected
			 * one, 127 for the pickup display. */
			const float bright = (float)atof(argv[++i]);
			HDObjLight& light = lights[lightCount++];

			light.x = 0.0f;
			light.y = -50.0f / 111.80339887f;	// sqrt(12500)
			light.z = -100.0f / 111.80339887f;
			light.kind = (float)HD_OBJLIGHT_SUN;
			light.r = bright;
			light.g = bright;
			light.b = bright;
			light.invRange = 0.0f;

			shade = 8.0;		// the ring's ambient
			ringLight = true;	// and its doubled sun
		}
		else
			printf("ModelProbe: ignoring %s\n", argv[i]);
	}

	/* -loop and -fire take the place of -lips: the face's own block instead of
	 * a track file. */
	if (loopAsk || fireAsk)
	{
		if (lips.track >= 0)
			printf("ModelProbe: -loop/-fire replace -lips %s\n", lips.file.c_str());

		if (!LipsLoopParse(loopAsk, fireAsk, lips))
			printf("ModelProbe: -fire %s is no <on>[:<off>] - ignored\n",
				fireAsk ? fireAsk : "");
	}

	if (name.empty() && slot < 0 && !all && !lara && !wardrobe && !poseGame)
	{
		printf("ModelProbe: nothing to look at - give -name, -slot, -all, "
			"-lara, -wardrobe or -classicpose\n");
		return 2;
	}

	printf("install : %s\n", root.c_str());

	/* The other bundle and the mods, in the same order and
	 * on the same two modules hd_object.cpp sets them: the alternate root
	 * before anything can ask for a page, the Mods folder before anything can
	 * ask for a model. */
	if (!tr3Root.empty())
	{
		HDModelTr3Root(tr3Root.c_str());
		HDPageCacheAltRoot(tr3Root.c_str());
		printf("tr1-3   : %s\n", tr3Root.c_str());
	}

	if (!modsFolder.empty())
	{
		HDModsFolder(modsFolder.c_str());
		HDModsLogSink(ProbeModsLine);
		HDModsScan();

		printf("mods    : %s, %d active\n", modsFolder.c_str(), HDModsCount());

		for (int m = 0; m < HDModsCount(); m++)
		{
			HDModShips ships;

			HDModsShipsOf(m, &ships);
			printf("  mod%-2d %-46s origin %-4s models %2d (%d outfit) "
				"pages %2d bank %s wavs %d\n", m, HDModsName(m),
				HDModsOrigin(m) ? (HDModsOrigin(m) == 1 ? "tr1"
					: HDModsOrigin(m) == 2 ? "tr2" : "tr3") : "-",
				ships.models, ships.outfits, ships.pages,
				ships.bank ? "yes" : "no", ships.wavs);
		}
	}

	/* The classic pose, and then stop. Needs neither the
	 * device nor a level: a mesh tree and a frame are level-file facts, which
	 * is what lets the engine, this tool and anything else say the same thing
	 * about the same object. -tr3 names the game 3 folder, so the bundle is
	 * its parent - the same step hd_model.cpp takes. */
	if (poseGame)
	{
		std::string bundle = tr3Root;

		while (!bundle.empty() && (bundle[bundle.size() - 1] == '\\' ||
			bundle[bundle.size() - 1] == '/'))
			bundle.erase(bundle.size() - 1);

		const size_t cut = bundle.find_last_of("\\/");

		bundle = cut == std::string::npos ? std::string(".") : bundle.substr(0, cut);

		const int id = HDClassicObjectId(poseName.c_str(), poseGame);
		HDClassicObject object;
		const char* why = "";

		printf("\n-- the classic pose: %s in game %d ----------------------\n",
			poseName.c_str(), poseGame);
		printf("bundle  : %s\n", bundle.c_str());

		if (!id)
		{
			printf("%s is no inventory model of game %d - the table in "
				"hd_classic.cpp has no row for it, so the ring would draw the "
				"file as it stands\n", poseName.c_str(), poseGame);
			return 1;
		}

		printf("object  : %d\n", id);

		if (!HDClassicRead(bundle.c_str(), poseGame, id, &object, &why))
		{
			printf("not read: %s\n", why);
			return 1;
		}

		printf("level   : %d\\DATA\\%s   (the first in name order that holds "
			"it)\n", object.game, object.level);
		printf("meshes  : %d\n", object.meshes);

		for (int n = 1; n < object.meshes; n++)
			printf("  bone %d: flags %d (%s%s) offset %d %d %d\n", n,
				object.bone[n].flags, (object.bone[n].flags & 1) ? "pop " : "",
				(object.bone[n].flags & 2) ? "push" : "", object.bone[n].x,
				object.bone[n].y, object.bone[n].z);

		printf("frame 0 : offset %d %d %d, bounds x %d..%d y %d..%d z %d..%d"
			"  = the posed box, %d x %d x %d\n",
			object.offset[0], object.offset[1], object.offset[2],
			object.bounds[0], object.bounds[1], object.bounds[2],
			object.bounds[3], object.bounds[4], object.bounds[5],
			object.bounds[1] - object.bounds[0],
			object.bounds[3] - object.bounds[2],
			object.bounds[5] - object.bounds[4]);

		for (int n = 0; n < object.meshes; n++)
		{
			float pivot[3], matrix[16];

			printf("  mesh %d: rot %4d %4d %4d of 1024 = %6.1f %6.1f %6.1f deg",
				n, object.rot[n][0], object.rot[n][1], object.rot[n][2],
				object.rot[n][0] * 360.0 / 1024.0,
				object.rot[n][1] * 360.0 / 1024.0,
				object.rot[n][2] * 360.0 / 1024.0);

			if (HDClassicBindPivot(&object, n, pivot))
				printf("   bind pivot %.0f %.0f %.0f", pivot[0], pivot[1], pivot[2]);

			printf("\n");

			if (!HDClassicNodeMatrix(&object, n, matrix))
				continue;

			for (int r = 0; r < 4; r++)
				printf("          %s %8.4f %8.4f %8.4f %8.4f\n",
					r == 3 ? "move" : "turn", matrix[r * 4 + 0],
					matrix[r * 4 + 1], matrix[r * 4 + 2], matrix[r * 4 + 3]);
		}

		printf("\nThe .TRM's vertices for node n are stored around that node's "
			"bind pivot;\nthe matrix is what the ring applies to them, before "
			"the box, the axes and the fit.\n");
		return 0;
	}

	/* The wardrobe scan. The folders are the ones on this command
	 * line and not the ones the registry names, so a measurement can be
	 * repeated on a copy - which is the whole reason this tool takes the
	 * install as its first argument. Set unconditionally, because -outfit
	 * takes a wardrobe key as well now. */
	HDWardrobeFolders(root.c_str(), tr3Root.empty() ? 0 : tr3Root.c_str());
	HDWardrobeLogSink(ProbeModsLine);

	/* The report itself needs neither the device nor the level: the scan is
	 * folders and .TRM headers, which is exactly what makes it the same
	 * answer in the engine, in this tool and in the randomizer. */
	if (wardrobe)
	{
		HDWardrobeScan();

		/* The from-words are hd_wardrobe's own - HDWardrobeFromTag.
		 * A local array of them stood here and it was one row short of the
		 * tiers the header defines, which is exactly the kind of
		 * drift a table of words in the caller invites. */
		printf("\n-- the wardrobe: %d outfits --------------------------------\n",
			HDWardrobeCount());
		printf("%-46s %-24s %-6s %-25s %-9s %-20s %-20s %-18s %s\n",
			"key", "name", "source", "hands braid hols glov rig", "flags",
			"sheen", "skin glow", "skin", "head");
		printf("%-46s %-24s %-6s %-25s %-9s %-20s %-20s %-18s %s\n",
			"----------------------------------------------",
			"------------------------", "------",
			"-------------------------", "---------", "--------------------",
			"--------------------", "------------------", "-------------------");

		for (int i = 0; i < HDWardrobeCount(); i++)
		{
			const HDWardrobeEntry* e = HDWardrobeAt(i);
			/* snprintf and not sprintf, and the buffers are wide enough for a
			 * number no file should hold. A sheen of 3e38 - a float's largest
			 * - is 42 characters before the "/14 (list)" is added, and
			 * sprintf into 48 bytes would give
			 * a table visibly corrupted and a write past the end of a stack
			 * array. The engine refuses such a value,
			 * so nothing hands it one; a tool that prints
			 * what it is given may not depend on that. */
			char flags[32], sheen[96], glow[96], skin[64], head[64];

			if (!e)
				continue;

			snprintf(flags, sizeof(flags), "%5d %5d %4d %4d %3d", (int)e->hands,
				(int)e->braid, (int)e->holsters, (int)e->gloves, (int)e->wearsRig);
			/* The tier goes in the sheen column as well: a list
			 * file may set it, and "0.10/14" alone cannot say whether the
			 * player's own line was read. */
			snprintf(sheen, sizeof(sheen), "%.2f/%.0f (%s)", e->sheen,
				e->sheenPower, HDWardrobeFromTag(e->fromSheen));
			// What her own skin is made of in this outfit.
			snprintf(glow, sizeof(glow), "%.2f/%.0f (%s)", e->skinSheen,
				e->skinSheenPower, HDWardrobeFromTag(e->fromSkinSheen));

			skin[0] = 0;

			for (int k = 0; k < HD_WARDROBE_SKIN && e->skin[k]; k++)
				snprintf(skin + strlen(skin), sizeof(skin) - strlen(skin),
					"%s%d", k ? "," : "", e->skin[k]);

			if (!skin[0])
				strcpy(skin, "-");

			snprintf(skin + strlen(skin), sizeof(skin) - strlen(skin), " (%s)",
				HDWardrobeFromTag(e->fromSkin));

			// Whose head this body wears, and who decided that.
			snprintf(head, sizeof(head), "%s (%s)", HDWardrobeHeadWord(e->head),
				HDWardrobeFromTag(e->fromHead));

			printf("%-46s %-24s %-6s %-25s %-9s %-20s %-20s %-18s %s\n", e->key,
				e->name,
				e->source == HD_WARDROBE_SOURCE_46 ? "46"
					: e->source == HD_WARDROBE_SOURCE_13 ? "13" : "mod",
				flags, HDWardrobeFromTag(e->fromFlags), sheen, glow, skin, head);
		}

		printf("\n-- passed over: %d files -----------------------------------\n",
			HDWardrobeRefusedCount());

		for (int i = 0; i < HDWardrobeRefusedCount(); i++)
			printf("  %s\n", HDWardrobeRefused(i));

		if (name.empty() && slot < 0 && !all && !lara)
			return 0;
	}

	/* --- the classic level, when there is one -------------------------- */
	Tr5Level level;
	bool haveLevel = false;

	if (!levelStem.empty() || !classicPath.empty())
	{
		const std::string path = classicPath.empty()
			? root + "\\DATA\\" + levelStem + ".TRC"
			: classicPath;
		std::string error;

		if (!level.load(ToW(path), error))
			printf("ModelProbe: %s is not loadable: %s\n", path.c_str(), error.c_str());
		else
		{
			haveLevel = true;
			printf("classic : %s\n", path.c_str());
		}

		if (!levelStem.empty())
		{
			const std::string map = root + "\\DATA\\" + levelStem + ".MAP";

			if (HDMapLoad(map.c_str()))
				printf("map     : %s, %d lines\n", map.c_str(), HDMapCount());
			else
				printf("map     : %s is not there - every slot binds by name\n",
					map.c_str());
		}
	}

	if ((all || slot >= 0 || lara) && !haveLevel)
	{
		printf("ModelProbe: -slot, -all and -lara need a level "
			"(-lara <LEVEL>, -level or -classic)\n");
		return 2;
	}

	/* --- the device and the two HD modules ----------------------------- */
	if (!MakeDevice())
		return 3;

	const char* why = "";

	if (!HDRenderInit(g_device, &why))
	{
		printf("ModelProbe: HDRenderInit failed - %s\n", why);
		DropDevice();
		return 4;
	}

	// The model builds' fingerprint is always on in the tool.
	HDModelBuildHash(1);

	if (!HDModelInit(g_device, &why))
	{
		printf("ModelProbe: HDModelInit failed - %s\n", why);
		HDRenderShutdown();
		DropDevice();
		return 4;
	}

	/* --- Lara's composite ------------------------------------------------ */
	if (lara)
	{
		LaraOptions o;

		o.root = root;
		o.stem = levelStem;
		o.outfit = outfitName;
		o.guns = gunsName;
		o.back = backName;
		o.out = out;
		o.holstersFull = holstersFull;
		o.holsters = holstersName;
		o.look = lookName;
		o.flare = flare;
		o.torch = torch;
		o.nodes = laraNodes;
		o.mirror = mirror;
		o.firstPerson = laraFirstPerson;
		o.classicBraidRoot = classicBraidRoot;
		o.onlyPart = onlyPart;
		o.noSheen = noSheen;
		o.softTest = softTest;
		o.softOffset[0] = softOffset[0];
		o.softOffset[1] = softOffset[1];
		o.softOffset[2] = softOffset[2];
		o.shadowMap = shadowMap;
		o.shadowCast = shadowCast;
		o.shadowDevice = shadowDevice;
		o.shadowDir[0] = shadowDir[0];
		o.shadowDir[1] = shadowDir[1];
		o.shadowDir[2] = shadowDir[2];
		o.shadowFrames = shadowFrames > 0 ? shadowFrames : 300;
		o.shadowRoom = shadowRoom;
		o.shadowAt[0] = shadowAt[0];
		o.shadowAt[1] = shadowAt[1];
		o.shadowReceive = shadowReceive;
		o.shadowEmpty = shadowEmpty;
		o.shadowNoGuard = shadowNoGuard;
		o.shadowTime = shadowTime;
		o.creatures = shadowCreatures;
		o.itemAnim = itemAnim;
		o.itemKey = itemKey;
		o.itemPush = itemPush;
		o.noPocket = noPocket;
		o.noGlass = noGlass;
		o.itemCull = itemCull;
		o.shadowOthers = shadowOthers;
		o.shadowNoModels = shadowNoModels;
		o.shadowCount = shadowCount;
		o.shadowAlong = shadowAlong;
		o.shadowUnseen = shadowUnseen;
		o.shadowMirror = shadowMirror;
		o.shadowMirrorView = shadowMirrorView;
		o.shadowNoMirror = shadowNoMirror;
		o.shadowMirrorMask = shadowMirrorMask;
		o.neck = laraNeck;
		o.headWeight = laraHeadWeight;
		o.handFold = laraHandFold;
		o.noFold = laraNoFold;
		o.noHandMix = laraNoHandMix;
		o.xray = laraXray;
		o.passes = laraPasses;
		o.head = laraHead;
		o.poseRisk = laraPoseRisk;
		o.tween60 = laraTween60;
		o.handLight = laraHandLight;
		o.oldSkin = laraOldSkin;
		o.braid = laraBraid;
		o.braidSim = laraBraidSim;
		o.braidAnims = laraBraidAnims;
		o.braidFloor = laraBraidFloor;
		o.check = laraCheck;
		o.invRot[0] = invRot[0];
		o.invRot[1] = invRot[1];
		o.invRot[2] = invRot[2];
		o.invRotSet = invRotSet;
		o.noDraw = noDraw;
		/* A device Reset refuses while a D3DPOOL_DEFAULT page lives, so the
		 * device check takes the pages managed. */
		o.managed = managed || shadowDevice;
		o.threads = threads;
		o.width = width;
		o.height = height;
		o.fov = fov;
		o.yaw = yaw;
		o.pitch = pitch;
		o.dist = dist;
		o.shade = shade;
		o.shadeRgb[0] = shadeRgb[0];
		o.shadeRgb[1] = shadeRgb[1];
		o.shadeRgb[2] = shadeRgb[2];
		o.cutS = cutS > -1.0e8 ? cutS
			: (laraFirstPerson ? (double)HD_LARA_HEAD_CUT_WEIGHT : -1.0);
		o.noCap = noCap;
		o.fpForward = fpForward;
		o.eyeOff[0] = eyeOff[0];
		o.eyeOff[1] = eyeOff[1];
		o.eyeOff[2] = eyeOff[2];
		o.cull = cullMode < 0
			? (laraFirstPerson ? HD_MODEL_CULL_BACK : HD_MODEL_CULL_NONE)
			: cullMode;
		o.yawSet = yawSet;
		o.pitchSet = pitchSet;
		o.lights = lights;
		o.lightCount = lightCount;
		o.turn = turn;
		o.nodeMask = nodeMask;
		o.lips = lips;

		/* -withrooms in -lara mode: the level's rooms first, as for a slot,
		 * so that -shadowroom has a room group to stand her in. With the
		 * classic level's room count, as RoomProbe's -rooms gives it, so a
		 * group is a room and not the sky. */
		if (!withRooms.empty())
		{
			HDRenderStats roomStats;
			const char* roomWhy = "";

			/* -shadowmirror: the classic rooms' boxes handed in first, as
			 * hd_level.cpp's BuildRoomBoxes does, so the level build finds
			 * the mirrors in the art (hd_render.cpp, FindMirrors). */
			static std::vector<HDRoomBox> boxes;

			if (shadowMirror)
			{
				std::string why;

				boxes.assign(level.rooms().size(), HDRoomBox());

				for (size_t r = 0; r < level.rooms().size(); r++)
				{
					const Tr5Room& rm = level.rooms()[r];
					HDRoomBox& b = boxes[r];
					unsigned yTop = 0, yOrigin = 0;

					level.headerField((int)r, 24, yOrigin, why);
					level.headerField((int)r, 36, yTop, why);
					b.mins[0] = 1024.0f;
					b.maxs[0] = (float)((rm.numXSectors - 1) * 1024);
					b.mins[1] = (float)((int)yTop - (int)yOrigin);
					b.maxs[1] = (float)(rm.yBottom - (int)yOrigin);
					b.mins[2] = 1024.0f;
					b.maxs[2] = (float)((rm.numZSectors - 1) * 1024);

					for (int axis = 0; axis < 3; axis++)
						if (b.maxs[axis] < b.mins[axis])
							b.maxs[axis] = b.mins[axis];

					b.originX = (float)rm.originX;
					b.originZ = (float)rm.originZ;
				}

				if (!boxes.empty())
					HDRenderSetRoomBoxes(&boxes[0], (int)boxes.size());
			}

			if (HDRenderLevelBegin(root.c_str(), withRooms.c_str(), (int)level.rooms().size(),
				o.managed,
				threads, &roomStats, &roomWhy))
				printf("rooms first        %s: %d group(s), %d page(s)\n", withRooms.c_str(),
					HDRenderGroupCount(), roomStats.pages.slots);
			else
				printf("rooms first        %s FAILED - %s\n", withRooms.c_str(), roomWhy);
		}

		const int result = RunLara(o, level);

		HDLaraLevelEnd();
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());
		return result;
	}

	/* --- which slots ---------------------------------------------------- */
	std::vector<HDModelSlot> wanted;
	std::vector<ClassicSlot> classic;
	std::vector<int> order;

	if (!name.empty())
	{
		/* A bare model, no level: 32 nodes, no bind pose, and the name
		 * handed straight to hd_model. The picture is the bind pose.
		 *
		 * Or one rigid node, with -rigid, which
		 * is how hd_object.cpp loads a mod's <BASE>_OPTION model for the ring:
		 * one node, every vertex rigid on it, because the file is another
		 * game's and stands in its own frame with no mesh tree of ours behind
		 * it. It is not a nicety - a file whose geometry sits on nodes 1 and 2
		 * and not on 0 (the IMI Uzis mod's UZI_OPTION is exactly that: 13723
		 * vertices on node 1 and 13726 on node 2) draws NOTHING at all through
		 * the 32-node path with no mesh tree to place those nodes, which is why
		 * a plain -name picture of it comes out empty. */
		ClassicSlot empty;

		empty.nodes = rigid ? 1 : 32;
		classic.push_back(empty);

		HDModelSlot one;

		one.slot = 0;
		one.nodes = rigid ? 1 : 32;
		one.rigidNode = rigid ? 0 : -1;
		one.bind = 0;
		one.name = name.c_str();
		one.onlyPage = onlyPage;	// -onlypage
		// The floor rule sweeps the kept vertices, every
		// model and not only the fitted ones - the reason is at
		// HDModelSlot::keepPoints.
		one.keepPoints = floorDraw;
		wanted.push_back(one);
		order.push_back(0);
		slot = 0;
	}
	else
	{
		const std::vector<int> slots = level.moveableSlots();

		classic.resize(slots.size());

		for (size_t i = 0; i < slots.size(); i++)
		{
			if (!all && slots[i] != slot)
				continue;

			ReadClassicSlot(level, slots[i], classic[i]);

			HDModelSlot one;

			one.slot = slots[i];
			one.nodes = classic[i].nodes;
			one.bind = &classic[i].bind[0][0];
			/* A pickup's vertex positions, kept until the
			 * floor rule has swept them. The engine asks for exactly this
			 * list, by exactly this rule (hd_object.cpp's slot plan). */
			one.keepPoints = HDSlotIsPickup(slots[i]);
			/* The mesh tree as well: a separate
			 * HD face's neck bone follows the head node's parent, and this
			 * tool has walked the tree for its own node table anyway. */
			one.parent = classic[i].parent;
			// -model: the asked slot takes that .TRM instead of its own.
			one.name = !modelAsked.empty() && slots[i] == slot
				? modelAsked.c_str() : 0;
			wanted.push_back(one);
			order.push_back((int)i);
		}

		/* What the inventory question costs. The engine asks
		 * it of every loaded slot at every level build and it walks the mods
		 * for a slot whose name ends in _ITEM - four names each - so it is
		 * exactly the shape of thing that can turn a level build into
		 * twenty-five seconds. The engine's own line says the same number; this
		 * is how it was measured before it was written down. */
		if (inventory)
		{
			LARGE_INTEGER freq, began, ended;
			int asked = 0, found = 0;
			char option[128];

			QueryPerformanceFrequency(&freq);
			QueryPerformanceCounter(&began);

			for (size_t i = 0; i < slots.size(); i++)
			{
				asked++;

				if (*HDBindModOption(slots[i], option, sizeof(option)))
					found++;
			}

			QueryPerformanceCounter(&ended);

			printf("inventory: the _OPTION question over %d slots found %d, in "
				"%.2f ms\n", asked, found,
				1000.0 * (double)(ended.QuadPart - began.QuadPart) /
					(double)freq.QuadPart);
		}

		if (wanted.empty())
		{
			printf("ModelProbe: slot %d is not in this level\n", slot);
			HDModelShutdown();
			HDRenderShutdown();
			DropDevice();
			return 5;
		}
	}

	/* A mod's own inventory model, planned exactly as
	 * hd_object.cpp plans it for the ring: a pseudo slot, one node, every
	 * vertex rigid on it, no bind pose - the file is another game's and stands
	 * in its own frame. -inventory then draws this instead of the slot's model,
	 * through HDModelRingFit, which is what the ring does. */
	std::string optionName;
	int optionSlot = -1;

	/* -floor asks for it too: the pickup lying on the
	 * ground draws a mod's <BASE>_OPTION model where there is one, so a
	 * picture of the floor has to have the same model on the card that the
	 * level would put there. */
	if ((inventory || floorDraw) && !all && slot >= 0)
	{
		char option[128];

		if (*HDBindModOption(slot, option, sizeof(option)))
		{
			optionName = option;
			/* A key for this tool's own model map and nothing else - the
			 * engine's HD_RING_SLOT_BASE is hd_object.cpp's, which this tool
			 * does not link. It only has to miss every slot of the level. */
			optionSlot = 0x7000 + slot;

			HDModelSlot one;

			one.slot = optionSlot;
			one.nodes = 1;
			one.name = optionName.c_str();
			one.rigidNode = 0;
			// ... and posed with the donor game's own frame 0, exactly
			// as hd_object.cpp asks for it - unless the run asked to see the
			// file as it lies.
			one.classicPose = !noClassicPose;
			// And its vertices kept, because the floor rule measures
			// the fitted model and a fit is only known once both are built.
			one.keepPoints = true;
			wanted.push_back(one);
		}
		else
			printf("inventory: no mod ships an _OPTION model for this slot - "
				"the ring and the floor draw the slot's own\n");
	}

	/* And the same plan for every pickup slot of the level, which is
	 * what -floorcheck needs: the floor draws a mod's <BASE>_OPTION model
	 * where there is one, so a table about the floor has to have those models
	 * on the card as well. Exactly hd_object.cpp's ring plan, slot for slot. */
	struct FloorOption { int slot; int model; };
	std::vector<FloorOption> floorOptions;
	std::vector<std::string> floorOptionNames;

	if (floorCheck)
	{
		// Reserved, because every `name` below points into this vector and a
		// reallocation would leave hd_model holding a dead pointer.
		floorOptionNames.reserve(wanted.size());

		const size_t asked = wanted.size();

		for (size_t i = 0; i < asked; i++)
		{
			char option[128];
			const int at = wanted[i].slot;

			if (!HDSlotIsPickup(at) ||
				!*HDBindModOption(at, option, sizeof(option)))
				continue;

			floorOptionNames.push_back(option);

			FloorOption plan;

			plan.slot = at;
			plan.model = 0x7000 + at;
			floorOptions.push_back(plan);

			HDModelSlot one;

			one.slot = plan.model;
			one.nodes = 1;
			one.name = floorOptionNames.back().c_str();
			one.rigidNode = 0;
			one.classicPose = !noClassicPose;
			one.keepPoints = true;
			wanted.push_back(one);
		}
	}

	/* --- the rooms first, if asked --------------------------------------- */
	double roomsBytes = 0;

	if (!withRooms.empty())
	{
		HDRenderStats roomStats;
		const char* roomWhy = "";

		if (HDRenderLevelBegin(root.c_str(), withRooms.c_str(), 0, managed,
			threads, &roomStats, &roomWhy))
		{
			roomsBytes = roomStats.pages.bytes;
			printf("rooms first        %s: %d page(s), %.1f MiB - the models "
				"are appended to that cache\n", withRooms.c_str(),
				roomStats.pages.slots, roomsBytes / 1048576.0);
		}
		else
			printf("rooms first        %s FAILED - %s\n", withRooms.c_str(), roomWhy);
	}

	/* --- load ----------------------------------------------------------- */
	HDModelStats stats;

	if (!HDModelLevelBegin(g_device, root.c_str(), &wanted[0], (int)wanted.size(),
		managed, threads, &stats, &why))
	{
		printf("ModelProbe: HDModelLevelBegin failed - %s\n", why);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 5;
	}

	/* The glass, as hd_object.cpp marks it at level build: every bound
	 * slot's material 14 alpha run on the nodes the classic draws
	 * see-through (hd_object_rules.h). -noglass draws the picture before
	 * the rule. */
	for (size_t i = 0; i < wanted.size(); i++)
		HDModelSetGlassNodes(wanted[i].slot, haveLevel && !noGlass
			? ToolSeeThroughNodes(level, wanted[i].slot, wanted[i].nodes) : 0);

	printf("\n-- what was loaded -------------------------------------------\n");
	printf("slots asked for   %d\n", stats.slots);
	printf("models on the card %d   (5: %d, 4: %d, mod: %d)   heads %d\n",
		stats.loaded, stats.fromGame5, stats.fromGame4, stats.fromMod,
		stats.heads);
	printf("not bound          lara %d, dummy %d, no name %d, missing %d, refused %d\n",
		stats.lara, stats.dummy, stats.noName, stats.missing, stats.refused);
	printf("nodes              %d over %d slots, %d covered, %d draw nothing\n",
		stats.nodesTotal, stats.fullSlots + stats.partialSlots, stats.nodesCovered,
		stats.nodesFallback);
	printf("slots              %d complete, %d partial\n",
		stats.fullSlots, stats.partialSlots);
	printf("geometry           %d vertices, %d indices, %d draw calls, "
		"%d mixed-page triangles\n",
		stats.vertices, stats.indices, stats.ranges, stats.mixedPageTriangles);
	printf("buffers            %.1f KiB vertices + %.1f KiB indices = %.1f KiB\n",
		stats.vertexBytes / 1024.0, stats.indexBytes / 1024.0,
		(stats.vertexBytes + stats.indexBytes) / 1024.0);
	printf("pages              %d added, %.1f MiB of texture, %.0f ms decode\n",
		stats.pages, stats.pageStats.bytes / 1048576.0, stats.pageStats.decodeMs);
	/* How many of the models' pages are cut-out
	 * sheets - a hole share over HD_CUTOUT_PERMILLE of the top mip. The
	 * opaque pass has to switch the alpha test on for exactly these, which is
	 * what hd_render has always done for the rooms. */
	printf("cut-out pages      %d of %d in the cache carry holes\n",
		HDPageCacheCutoutPages(), HDPageCacheSlots());

	/* Where each page came from. The key on the left is the
	 * cache's own - "mod0:6111" is a page out of mod 0's TEX folder and 6111
	 * on its own is the install's page of that number, two different slots -
	 * and the word after it is the folder the .DDS was really read from. It
	 * is the proof that a mod replaced the pages it brought and no others. */
	for (int s = 0; s < HDPageCacheSlots(); s++)
	{
		const int key = HDPageCachePageOfSlot(s);
		char label[32];

		if (HDPageModIndex(key) >= 0)
			snprintf(label, sizeof(label), "mod%d:%d", HDPageModIndex(key),
				HDPageModNumber(key));
		else if (key >= HD_PAGE_ALT_BASE)
			snprintf(label, sizeof(label), "TR3 %d", key - HD_PAGE_ALT_BASE);
		else
			snprintf(label, sizeof(label), "%d", key);

		/* The second number is the object path's own: of the texels below
		 * the reference, the share at exactly 0 - holes, where a gloss mask
		 * lies at a dim value that is not 0 (hd_model_cut.h). */
		printf("  page %-12s from %-7s holes %4.1f %%%s  at 0 %5.1f %% of them%s\n",
			label,
			HDPageCacheOriginText(HDPageCacheSlotOrigin(s)),
			HDPageCacheSlotCutoutPermille(s) / 10.0,
			HDPageCacheSlotCutout(s) ? "  <- counted as a cut-out sheet" : "",
			HDPageCacheSlotZeroPermille(s) / 10.0,
			HDPageCacheSlotHoles(s) ? "  <- holes: a part of material 6, 7 or 8 on it would be cut" : "");
	}
	printf("cut ranges         %d opaque ranges of %d models drawn with the alpha test\n",
		stats.cutRanges, stats.cutModels);
	/* The phases of `building` - keys is
	 * the one that grew to twenty-five seconds in the game, and the three
	 * .TRM numbers say how much of the wardrobe a build really reads. */
	printf("time               %.0f ms reading the .TRM files, %.0f ms building"
		" (find %.0f, keys %.0f, pages %.0f, buffers %.0f) hash=%016llx"
		" hash_ms=%.0f\n",
		stats.readMs, stats.buildMs, stats.findMs, stats.keyMs,
		stats.pageMs, stats.bufferMs, stats.hash, stats.hashMs);
	printf("trm reads          %d files opened, %d loaded with geometry, %.0f KiB read\n",
		(int)stats.trmOpens, (int)stats.trmLoads, stats.trmBytes / 1024.0);

	if (!withRooms.empty())
		printf("level total        %.1f MiB of pages on the card "
			"(%.1f rooms + %.1f models and wardrobe)\n",
			HDPageCacheBytes() / 1048576.0, roomsBytes / 1048576.0,
			(HDPageCacheBytes() - roomsBytes) / 1048576.0);

	for (int i = 0; i < HDModelNoteCount(); i++)
		printf("  note: %s\n", HDModelNote(i));

	// The engine's own lines for the faces' track files, word for word.
	for (int i = 0; i < HDModelLipsLineCount(); i++)
		printf("  HDLOG level=%s lips %s\n", levelStem.empty() ? "-" : levelStem.c_str(),
			HDModelLipsLine(i));

	/* --- the table ------------------------------------------------------ */
	if (allAsked)
	{
		/* The skeleton columns. `maxb` is the highest node
		 * a weighted vertex of the .TRM names and `drift` is how far the
		 * centre of a node's own vertices sits from that node in the SLOT's
		 * bind pose, mean over the covered nodes. The engine draws HD only
		 * when maxb is inside the slot's node count and the drift is under
		 * its limit - which is why the two numbers stand here beside the slot
		 * that is really loaded, randomised or not. */
		printf("\n-- every slot ------------------------------------------------\n");
		printf("%-5s %-24s %-7s %-24s %5s %5s %5s %5s %6s %5s %s\n", "slot",
			"slot name", "via", "model", "nodes", "maxb", "cover", "drift",
			"worst", "pages", "notes");
		printf("----- ------------------------ ------- ------------------------ "
			"----- ----- ----- ----- ------ ----- ------------------\n");

		int complete = 0, partial = 0, classicOnly = 0;
		const float kProbeDriftLimit = HD_SKELETON_DRIFT_LIMIT;
		int mismatchNodes = 0, mismatchDrift = 0, placedFit = 0;
		float worstGood = 0, bestBad = 1e9f;

		/* The bind step, per slot - map/hand/donor are
		 * not told apart from prefix5/prefix/prefix4/classic here because
		 * this tool never builds a DonorPlan (it has no .hdm sidecar to read
		 * for the 15 shipped, unrandomized levels); a donor slot instead
		 * shows up as whatever its OWN name resolves to, same as any other. */
		int viaMap = 0, viaHand = 0, viaPrefix5 = 0, viaPrefix = 0,
			viaPrefix4 = 0, viaClassic = 0;

		for (size_t i = 0; i < wanted.size(); i++)
		{
			const int here = wanted[i].slot;
			const char* slotName = HDSlotName(here);

			HDBindKind kind = HD_BIND_NONE;
			bool fromMap = false;
			HDBindVia via = HD_BIND_VIA_NONE;
			const char* asked = HDBindSlot(root.c_str(), here, &kind, &fromMap, &via);
			const char* viaStr;

			switch (via)
			{
				case HD_BIND_VIA_MAP:     viaStr = "map";     viaMap++;     break;
				case HD_BIND_VIA_HAND:    viaStr = "hand";    viaHand++;    break;
				case HD_BIND_VIA_PREFIX5: viaStr = "prefix5"; viaPrefix5++; break;
				case HD_BIND_VIA_PREFIX:  viaStr = "prefix";  viaPrefix++;  break;
				case HD_BIND_VIA_PREFIX4: viaStr = "prefix4"; viaPrefix4++; break;
				case HD_BIND_VIA_CLASSIC: viaStr = "classic"; viaClassic++; break;
				default:                  viaStr = "-";                    break;
			}

			if (!HDModelHas(here))
			{
				classicOnly++;
				printf("%-5d %-24s %-7s %-24s %5d %5s %5s %5s %6s %5s %s\n", here,
					slotName ? slotName : "?", viaStr,
					kind == HD_BIND_LARA ? "(Lara's composite)"
						: (kind == HD_BIND_DUMMY ? "(DUMMY on purpose)"
							: (asked ? asked : "?")),
					wanted[i].nodes, "-", "-", "-", "-", "-",
					kind == HD_BIND_MODEL ? "no .TRM - classic" : "");
				continue;
			}

			int covered = 0;
			std::string fallback;

			for (int node = 0; node < HDModelNodes(here); node++)
			{
				if (HDModelNodeCovered(here, node))
					covered++;
				else
				{
					char text[16];

					sprintf(text, "%s%d", fallback.empty() ? "" : ",", node);
					fallback += text;
				}
			}

			int missingPages = 0;

			for (int p = 0; p < HDModelPageCount(here); p++)
				if (!PageThere(HDModelPage(here, p)))
					missingPages++;

			if (covered >= HDModelNodes(here))
				complete++;
			else
				partial++;

			char note[128] = "";

			if (!fallback.empty())
				sprintf(note, "empty nodes %.60s", fallback.c_str());

			if (missingPages)
				sprintf(note + strlen(note), "%s%d page(s) WHITE",
					*note ? "; " : "", missingPages);

			if (HDModelHasHead(here))
				sprintf(note + strlen(note), "%shead on node %d",
					*note ? "; " : "", HDModelHeadNode(here));

			const int maxBone = HDModelMaxBone(here);
			float drift = -1, worst = -1;

			HDModelBindDrift(here, &drift, &worst);

			/* The engine's own verdict, spelled out here so that the table and
			 * the game cannot drift apart. */
			const bool badNodes = maxBone >= HDModelNodes(here);
			const ClassicSlot& tree = classic[(size_t)order[i]];
			const float fit = !badNodes && drift > kProbeDriftLimit &&
				tree.ok ? ToolFitOf(level, tree, here) : -1.0f;
			const HDSkeletonVerdict judged =
				HDSkeletonJudge(drift, fit, kProbeDriftLimit);
			const bool badDrift = !badNodes && judged == HD_SKELETON_DRIFTS;

			if (badNodes)
			{
				mismatchNodes++;
				sprintf(note + strlen(note), "%sSKELETON: names node %d of %d",
					*note ? "; " : "", maxBone, HDModelNodes(here));
			}
			else if (badDrift)
			{
				mismatchDrift++;
				sprintf(note + strlen(note), "%sSKELETON: drift",
					*note ? "; " : "");
			}
			else if (judged == HD_SKELETON_PLACED)
			{
				placedFit++;
				sprintf(note + strlen(note), "%sfit %.0f", *note ? "; " : "",
					fit);
			}

			if (drift >= 0)
			{
				if (badNodes || badDrift)
				{
					if (drift < bestBad)
						bestBad = drift;
				}
				else if (judged == HD_SKELETON_FITS && drift > worstGood)
					worstGood = drift;
			}

			char maxText[16], driftText[16], worstText[16];

			sprintf(maxText, "%d", maxBone);
			sprintf(driftText, drift >= 0 ? "%.0f" : "-", drift);
			sprintf(worstText, worst >= 0 ? "%.0f" : "-", worst);

			printf("%-5d %-24s %-7s %-24s %5d %5s %5d %5s %6s %5d %s\n", here,
				slotName ? slotName : "?", viaStr, HDModelName(here),
				HDModelNodes(here), maxText, covered, driftText, worstText,
				HDModelPageCount(here), note);
		}

		printf("\n%d slots complete, %d partial (an uncovered node draws "
			"nothing), %d classic only\n", complete, partial, classicOnly);
		printf("bind: %d map, %d hand, %d prefix5, %d prefix, %d prefix4, "
			"%d classic (of %d slots asked)\n",
			viaMap, viaHand, viaPrefix5, viaPrefix, viaPrefix4, viaClassic,
			(int)wanted.size());
		printf("skeleton: %d refused on the node count, %d on the drift; "
			"worst drift among the models that fit %.0f (limit %.0f)\n",
			mismatchNodes, mismatchDrift, worstGood, kProbeDriftLimit);

		if (bestBad < 1e8f)
			printf("          the smallest drift among the refused is %.0f\n",
				bestBad);

		// Only a level with a model the fit took prints the line.
		if (placedFit)
			printf("          %d over the drift limit taken on the fit "
				"(limit %.0f)\n", placedFit, (double)HD_FIT_LIMIT);

		/* -all -alpha: one line per bound slot that has a see-through
		 * classic node or a glass material range - the glass rule's census
		 * over a level. */
		if (alphaCheck)
		{
			printf("\n-- alpha, every bound slot with glass or "
				"see-through --\n");

			for (size_t i = 0; i < wanted.size(); i++)
				if (HDModelHas(wanted[i].slot))
					PrintAlpha(level, root, wanted[i].slot, wanted[i].nodes,
						true);
		}

		/* -all -bindcheck: one bindcheck line per bound slot, so the two
		 * drifts can be read side by side over a whole level. */
		if (bindCheck)
		{
			printf("\n-- bindcheck, every bound slot -------------------\n");

			for (size_t i = 0; i < wanted.size(); i++)
				if (HDModelHas(wanted[i].slot) &&
					classic[(size_t)order[i]].ok)
					PrintBindCheck(level, root, wanted[i].slot,
						classic[(size_t)order[i]], true);
		}

		/* -all -slidecensus: per slot, over every keyframe of every one of
		 * its animations against the slot's rest (its first animation's
		 * first keyframe): how far the frame's own offset travels, how far
		 * any node turns against the rest (`turn`) and whether any rotation
		 * word changes within an animation (`within`), and whether some
		 * keyframe's own box (the frame's first six words) has left the
		 * rest box altogether (`leaves`) or folded flat (`folds`). `pocket`
		 * is HDSlidesIntoPocket over every animation the slot holds in the
		 * file, `walk` the engine's walk over the animations it can reach
		 * (ToolPocketOf) - the rule hd_object.cpp draws by - and `open` the
		 * same walk following every link, into other objects' runs too. */
		if (slideCensus)
		{
			printf("\n-- slidecensus ------------------------------------\n");

			for (size_t i = 0; i < wanted.size(); i++)
			{
				const ClassicSlot& tree = classic[(size_t)order[i]];
				int anims = 0;
				SlotAnim a, a0;
				double travel = 0, turn = 0;
				bool leaves = false, folds = false, turnsWithin = false;
				std::vector<float> rest, pose;

				if (!tree.ok ||
					!ReadSlotAnim(level, tree.slot, 0, a0, &anims) ||
					!ReadAnimPose(level, tree, 0, 0, rest))
					continue;

				const long long home = (long long)(a0.frameOffset / 2);
				short restBox[9];

				for (int k = 0; k < 9; k++)
					restBox[k] = level.debugReadFrameShort(home + k);

				for (int anim = 0; anim < anims; anim++)
				{
					if (!ReadSlotAnim(level, tree.slot, anim, a, 0))
						continue;

					const long long first = (long long)(a.frameOffset / 2);
					const int keys = (a.end - a.start) / a.rate + 1;

					for (int key = 0; key < keys; key++)
					{
						const long long at = first + (long long)key * a.size;
						short box[9];
						double d = 0;

						for (int k = 0; k < 9; k++)
							box[k] = level.debugReadFrameShort(at + k);

						for (int k = 0; k < 3; k++)
							d += (double)(box[6 + k] - restBox[6 + k]) *
								(box[6 + k] - restBox[6 + k]);

						travel = sqrt(d) > travel ? sqrt(d) : travel;

						if (HDSlidBoxLeaves(restBox, box))
							leaves = true;

						if (HDSlidBoxFolds(restBox, box))
							folds = true;

						// The rotation words against the animation's first
						// keyframe's: a turn within the animation, exactly.
						for (int w = 9; w < a.size; w++)
							if (level.debugReadFrameShort(at + w) !=
								level.debugReadFrameShort(first + w))
								turnsWithin = true;

						if (!ReadAnimPose(level, tree, anim, key, pose))
							continue;

						for (int n = 0; n < tree.nodes; n++)
						{
							const float* p = &pose[(size_t)n * 12];
							const float* r = &rest[(size_t)n * 12];
							double tr = 0;

							for (int x = 0; x < 3; x++)
								for (int y = 0; y < 3; y++)
									tr += r[x * 4 + y] * p[x * 4 + y];

							double c = (tr - 1.0) * 0.5;

							c = c > 1 ? 1 : c < -1 ? -1 : c;
							turn = acos(c) * 180.0 / 3.14159265358979 > turn
								? acos(c) * 180.0 / 3.14159265358979 : turn;
						}
					}
				}

				/* A slot is listed when its own animations move it or
				 * when either walk takes it; `open` is the walk that
				 * follows every link, into other objects' animations. */
				const bool walk = ToolPocketOf(level, tree.slot);
				const bool open = ToolPocketOf(level, tree.slot, true);

				if (travel > 0 || leaves || walk || open)
					printf("  slide slot=%d name=%s anims=%d travel=%.0f "
						"turn=%.1f within=%d leaves=%d folds=%d pocket=%d "
						"walk=%d "
						"hd=%d open=%d\n", tree.slot,
						HDSlotName(tree.slot) ? HDSlotName(tree.slot) : "?",
						anims, travel, turn, turnsWithin ? 1 : 0,
						leaves ? 1 : 0, folds ? 1 : 0,
						HDSlidesIntoPocket(leaves, folds, turnsWithin) ? 1 : 0,
						walk ? 1 : 0, HDModelHas(tree.slot) ? 1 : 0,
						open ? 1 : 0);
			}
		}

		/* -all -crossdrift: every bound model against every OTHER slot of
		 * the level with the same node count - the foreign skeletons the
		 * skeleton test exists to refuse, measured by both drifts. */
		if (crossDrift)
		{
			printf("\n-- crossdrift, every model on every foreign tree --\n");

			for (size_t i = 0; i < wanted.size(); i++)
			{
				if (!HDModelHas(wanted[i].slot) ||
					HDModelMaxBone(wanted[i].slot) < 1)
					continue;

				for (size_t j = 0; j < wanted.size(); j++)
				{
					const ClassicSlot& other = classic[(size_t)order[j]];

					if (j == i || !other.ok ||
						other.nodes != HDModelNodes(wanted[i].slot))
						continue;

					printf("  on slot %d: ", other.slot);
					PrintBindCheck(level, root, wanted[i].slot, other, true);
				}
			}
		}
	}

	/* --- The floor rule, slot by slot ------------------------------------ */
	if (floorCheck)
	{
		/* The engine's own arithmetic, not a copy of it: HDModelFloorLift is
		 * the function hd_object.cpp calls at level build, and the reference
		 * it is handed - the classic mesh's deepest point - is read here out
		 * of the .TRC that the engine reads out of mesh_vtxbuf.
		 *
		 * Every pickup slot of the level, not the ones that move: an install
		 * file that reads zero is the proof that the rule is inert on vanilla
		 * data, and it is only a proof if it is printed.
		 *
		 * The fitted column is the option case - a mod's <BASE>_OPTION model
		 * turned onto the item's box - and it is measured twice on purpose:
		 * on the box corners through the fit, which is the older reading,
		 * and on the vertices, which is what the rule does now. */
		printf("\n-- the floor rule --------------------------------------------\n");
		printf("%-5s %-20s %-26s %8s %8s %8s %7s %s\n", "slot", "slot name",
			"model on the floor", "classic", "lowest", "sink", "lift",
			"how");
		printf("----- -------------------- -------------------------- "
			"-------- -------- -------- ------- -------------\n");

		int measured = 0, lifted = 0;
		/* What the sweep costs. The fitted case walks every kept vertex of
		 * the model - 27,449 of them on one posed pair of Uzis - and
		 * it happens once per slot at level build, so the number worth having
		 * is microseconds per model and not per frame. */
		LARGE_INTEGER freq, began, ended;
		double sweptUs = 0;
		int sweptVertices = 0;
		int sweptBlended = 0;

		QueryPerformanceFrequency(&freq);

		for (size_t i = 0; i < wanted.size(); i++)
		{
			const int at = wanted[i].slot;

			if (!HDSlotIsPickup(at) || !HDModelHas(at))
				continue;

			const ClassicSlot* shape = 0;

			for (size_t k = 0; k < order.size(); k++)
				if (wanted[k].slot == at)
					shape = &classic[(size_t)order[k]];

			/* The frame the level draws this slot in - the
			 * rule's own frame. The bind numbers are printed beside it,
			 * because the difference between the two is what the animation-frame
			 * reading changed, and a table that showed only one of
			 * them could not be checked against the other. */
			std::vector<float> framePose;
			const float* poseAt = 0;

			if (shape && ReadFramePose(level, *shape, framePose) > 0)
				poseAt = &framePose[0];

			float classicLowest = 0.0f, classicBind = 0.0f;

			if (!shape || !ClassicMeshLowest(level, *shape, poseAt, &classicLowest, 0))
			{
				printf("%-5d %-20s %-26s %8s %8s %8s %7s %s\n", at,
					HDSlotName(at) ? HDSlotName(at) : "?", HDModelName(at),
					"-", "-", "-", "-", "no classic mesh with vertices");
				continue;
			}

			/* The model the floor really draws. A mod that ships a
			 * <BASE>_OPTION model and no EXTRA_ name for the slot puts that
			 * on the ground, fitted onto the item's box - so the
			 * question is asked of the model that is drawn and not of the one
			 * the slot happens to be bound to. */
			int option = -1;

			for (size_t k = 0; k < floorOptions.size(); k++)
				if (floorOptions[k].slot == at && HDModelHas(floorOptions[k].model))
					option = floorOptions[k].model;

			ClassicMeshLowest(level, *shape, 0, &classicBind, 0);

			float lowest = 0.0f, lift = 0.0f, lowestBind = 0.0f;
			float fix[16];
			char frame[64];

			snprintf(frame, sizeof(frame), "node 0 y -> (%.2f %.2f %.2f)",
				poseAt ? poseAt[1] : 0.0f, poseAt ? poseAt[5] : 1.0f,
				poseAt ? poseAt[9] : 0.0f);

			if (option >= 0 && HDModelRingFit(option, at, fix))
			{
				QueryPerformanceCounter(&began);

				const bool ok = HDModelFloorLift(option, fix, poseAt,
					shape->nodes, classicLowest, &lowest, &lift);

				QueryPerformanceCounter(&ended);

				if (!ok)
					continue;

				sweptUs += 1.0e6 * (double)(ended.QuadPart - began.QuadPart) /
					(double)freq.QuadPart;

				int kept = 0, blended = 0;

				HDModelPointsInfo(option, &kept, &blended);
				sweptVertices += kept;
				sweptBlended += blended;

				HDModelFloorLift(option, fix, 0, shape->nodes, classicBind,
					&lowestBind, 0);

				/* And the box-corner answer beside it: the eight corners of
				 * the source's box through the same fit. It is here to be
				 * compared, because the difference between the two is
				 * what the vertex-level reading changed. */
				float srcMin[3], srcMax[3], corners = 0.0f;

				HDModelBounds(option, srcMin, srcMax);

				for (int c = 0; c < 8; c++)
				{
					const float p[3] =
					{
						(c & 1) ? srcMax[0] : srcMin[0],
						(c & 2) ? srcMax[1] : srcMin[1],
						(c & 4) ? srcMax[2] : srcMin[2]
					};
					const float y = fix[13] + p[0] * fix[1] + p[1] * fix[5] +
						p[2] * fix[9];

					if (!c || y > corners)
						corners = y;
				}

				measured++;

				if (lift > 0.0f)
					lifted++;

				printf("%-5d %-20s %-26s %8.1f %8.1f %8.1f %7.1f %s\n", at,
					HDSlotName(at) ? HDSlotName(at) : "?",
					HDModelName(option), classicLowest, lowest,
					lowest - classicLowest, lift, "fitted");
				printf("%-5s %-20s %-26s %8.1f %8.1f %8.1f %7s %s\n", "", "",
					"  (bind pose, not drawn)", classicBind, lowestBind,
					lowestBind - classicBind, "", frame);
				printf("%-5s %-20s %-26s %8s %8.1f %8.1f %7s %s\n", "", "",
					"  (by the box corners)", "", corners,
					corners - classicBind, "",
					"the bound, not the model, in the bind pose");
				continue;
			}

			QueryPerformanceCounter(&began);

			const bool ok = HDModelFloorLift(at, 0, poseAt, shape->nodes,
				classicLowest, &lowest, &lift);

			QueryPerformanceCounter(&ended);

			if (!ok)
			{
				printf("%-5d %-20s %-26s %8.1f %8s %8s %7s %s\n", at,
					HDSlotName(at) ? HDSlotName(at) : "?", HDModelName(at),
					classicLowest, "-", "-", "-",
					"the model kept no vertices to measure");
				continue;
			}

			sweptUs += 1.0e6 * (double)(ended.QuadPart - began.QuadPart) /
				(double)freq.QuadPart;

			{
				int kept = 0, blended = 0;

				HDModelPointsInfo(at, &kept, &blended);
				sweptVertices += kept;
				sweptBlended += blended;
			}

			HDModelFloorLift(at, 0, 0, shape->nodes, classicBind, &lowestBind, 0);

			measured++;

			if (lift > 0.0f)
				lifted++;

			printf("%-5d %-20s %-26s %8.1f %8.1f %8.1f %7.1f %s\n", at,
				HDSlotName(at) ? HDSlotName(at) : "?", HDModelName(at),
				classicLowest, lowest, lowest - classicLowest, lift,
				"as it stands");
			printf("%-5s %-20s %-26s %8.1f %8.1f %8.1f %7s %s\n", "", "",
				"  (bind pose, not drawn)", classicBind, lowestBind,
				lowestBind - classicBind, "", frame);
		}

		printf("\n%d pickup model(s) measured, %d lifted - y is DOWN, so a "
			"positive sink is a model standing IN the floor\n",
			measured, lifted);

		if (sweptVertices)
			printf("the vertex sweep: %d vertices through the frame in %.1f us,"
				" once per slot at level build - %d of them blended over more "
				"than one node\n", sweptVertices, sweptUs, sweptBlended);
	}

	if (all)
	{
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());
		return 0;
	}

	/* --- one model ------------------------------------------------------ */
	if (!HDModelHas(slot))
	{
		printf("\nModelProbe: slot %d has no HD model - nothing to draw\n", slot);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 6;
	}

	const ClassicSlot* here = 0;

	for (size_t i = 0; i < order.size(); i++)
		if (wanted[i].slot == slot)
			here = &classic[(size_t)order[i]];

	printf("\nmodel   : %s.TRM, %d vertices, %d indices, %d draw calls, %d page(s), "
		"%d cut\n",
		HDModelName(slot), HDModelVertices(slot), HDModelIndices(slot),
		HDModelRanges(slot), HDModelPageCount(slot), HDModelCutRanges(slot));

	for (int p = 0; p < HDModelPageCount(slot); p++)
	{
		const int key = HDModelPage(slot, p);
		const int mod = HDPageModIndex(key);

		if (mod >= 0)
			printf("          page mod%d:%-6d from %-7s%s\n", mod,
				HDPageModNumber(key), PageFrom(key),
				PageThere(key) ? "" : "  - no .DDS, WHITE instead");
		else if (key >= HD_PAGE_ALT_BASE)
			printf("          page TR3 %-6d from %-7s%s\n",
				key - HD_PAGE_ALT_BASE, PageFrom(key),
				PageThere(key) ? "" : "  - no .DDS, WHITE instead");
		else
			printf("          page %-11d from %-7s%s\n", key, PageFrom(key),
				PageThere(key) ? "" : "  - no .DDS, WHITE instead");
	}

	if (nodes || !all)
		PrintNodeTable(slot, here, HDModelName(slot));

	if (bindCheck && here)
		PrintBindCheck(level, root, slot, *here, false);

	if (bonesAsked)
		PrintBoneTable(&level, root, slot, here && here->ok ? here : 0);

	if (alphaCheck && here)
		PrintAlpha(level, root, slot, here->nodes, false);

	float mins[3], maxs[3];

	HDModelBounds(slot, mins, maxs);

	/* A separate HD face reaches past the body's box - the body has an empty
	 * collar and the face IS the head - so the camera has to frame both or it
	 * cuts the head off. */
	if (head && HDModelHasHead(slot))
	{
		float headMins[3], headMaxs[3];

		if (HDModelHeadBounds(slot, headMins, headMaxs))
			for (int i = 0; i < 3; i++)
			{
				if (headMins[i] < mins[i]) mins[i] = headMins[i];
				if (headMaxs[i] > maxs[i]) maxs[i] = headMaxs[i];
			}

		/* -lips looks at the face: the camera frames its box alone, in the
		 * body's bind pose. */
		if (lips.track >= 0 && HDModelHeadBounds(slot, headMins, headMaxs))
			for (int i = 0; i < 3; i++)
			{
				mins[i] = headMins[i];
				maxs[i] = headMaxs[i];
			}
	}

	printf("\nbounds  : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f\n",
		mins[0], maxs[0], mins[1], maxs[1], mins[2], maxs[2]);

	/* The cloud's own axes beside the box. A box says how
	 * much room the model needs; these say which way its shape runs, which
	 * is what the ring's fit has to know about a crossed pair of guns. */
	{
		float axes[9], extent[3];

		if (HDModelPrincipalAxes(slot, axes, extent))
		{
			printf("axes    : the vertex cloud's own, longest first\n");

			for (int k = 0; k < 3; k++)
				printf("          %d  (%+6.3f %+6.3f %+6.3f)  extent %7.1f\n",
					k, axes[k * 3 + 0], axes[k * 3 + 1], axes[k * 3 + 2],
					extent[k]);
		}
	}

	/* --- The frame the level draws in, before the camera ------------------
	 *
	 * Every other switch's picture is the bind pose, which is the frame a
	 * .TRM's vertices are measured against and the right frame to look at a
	 * model in. -floor is the one that claims to show what the level puts on
	 * the ground, and a version that only used the bind pose could not have
	 * shown that the engine's
	 * lift went the wrong way for six of ANDREA3's eighteen pickup slots.
	 *
	 * It goes in front of the camera as well, for the reason the ring turn
	 * does below: the frame moves the model about the item's origin, and a
	 * camera framed on the untouched box then looks at the wrong place - a
	 * picture taken without this fix had the model well above the floor plane and
	 * the plane itself half out of the frame. The box is the drawn vertices'
	 * own, through the fit and the frame, which is the same sweep the rule
	 * measures its lowest point with. */
	std::vector<float> framePose;
	const float* floorPose = 0;
	float floorFit[16];
	float floorDeepest[3] = { 0.0f, 0.0f, 0.0f };
	bool floorFitted = false;

	/* The pose is read wherever a floor number is printed and not only for
	 * -floor: -inventory prints the floor line too, and a number in the wrong
	 * frame is exactly the bug this reading fixes. Only the picture - the node
	 * matrices, the camera and the plane - waits for -floor, so every other
	 * switch's render is the bind pose it always was. */
	if (here->slot >= 0 && ReadFramePose(level, *here, framePose) > 0)
		floorPose = &framePose[0];

	if (floorDraw && floorPose)
	{
		float lo[3], hi[3];

		floorFitted = optionSlot >= 0 && HDModelHas(optionSlot) &&
			HDModelRingFit(optionSlot, slot, floorFit);

		printf("\nthe drawn frame: node 0's y axis points (%.3f %.3f %.3f) in "
			"the item's frame - (0 1 0) is a frame that turns nothing or turns "
			"only about y\n", floorPose[1], floorPose[5], floorPose[9]);

		if (HDModelFramedBox(floorFitted ? optionSlot : slot,
			floorFitted ? floorFit : 0, floorPose, here->nodes, lo, hi,
			floorDeepest))
		{
			printf("drawn   : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f - the "
				"model's own box where the frame puts it; its deepest point is "
				"at x %.0f z %.0f, where the floor plane is drawn\n",
				lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], floorDeepest[0],
				floorDeepest[2]);

			/* And the lift is part of what the camera has to see. The box above is the model before the
			 * rule moves it and the camera is built from it further down, so a
			 * model that is lifted far walks out of the top of the picture:
			 * RICH3's crossbow ammunition comes up 126.6 units, and without
			 * this a render of it shows the bottom third of a quiver with the whole
			 * top cut off - the one case where a picture is most wanted.
			 *
			 * The box is stretched upward by the lift, which is the smaller y
			 * here, so that the camera frames the union of the two positions:
			 * the plane stays at the classic mesh's own depth, the lifted
			 * model stands on it, and both are in the picture.
			 *
			 * Also when the lift is left off, because -floor and -nofloorlift
			 * are a pair and a pair is only readable on one camera: the pixel
			 * proof measures how much higher the silhouette
			 * stands in the one than in the other, and two different framings
			 * would make that difference meaningless.
			 *
			 * The lift is the same two calls the printer makes further down -
			 * one reads the classic mesh out of the .TRC, the other is the
			 * engine's own HDModelFloorLift - with the same arguments, and
			 * both are pure functions of the model, the fit and the pose. */
			float floorClassic = 0.0f, lowest = 0.0f, lift = 0.0f;

			if (ClassicMeshLowest(level, *here, floorPose, &floorClassic, 0) &&
				HDModelFloorLift(floorFitted ? optionSlot : slot,
					floorFitted ? floorFit : 0, floorPose, here->nodes,
					floorClassic, &lowest, &lift) && lift > 0.0f)
			{
				printf("          and %.0f units of room above it, which is "
					"where the lift puts the model - the camera frames both "
					"positions, so -floor and -nofloorlift share it\n", lift);
				lo[1] -= lift;
			}

			for (int c = 0; c < 3; c++)
			{
				mins[c] = lo[c];
				maxs[c] = hi[c];
			}
		}
		else
			printf("drawn   : the model kept no vertices - the camera frames "
				"the bind box and the picture is not the level's\n");
	}
	else if (floorDraw && here->slot >= 0)
		printf("\nthe drawn frame: the level has no frame for this slot - the "
			"picture and the numbers are the bind pose\n");

	if (noDraw)
	{
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 0;
	}

	/* --- the turn in front of the model, before the camera --------------- */

	/* Whatever goes in front of the model has to go in front of the camera too.
	 * Both turns below spin the model about
	 * the origin, and a model whose box is off-centre - UZI_OPTION's is
	 * x -131..16, y -82..171, z -87..72, so its middle stands 73 units off -
	 * then swings clean out of a frame that was computed from its untouched
	 * box. That is why a face-on render without this fix comes out 100 % magenta,
	 * and any conclusion that no rotation of the file makes an X
	 * has nothing under it. The box corners are carried through the turn here
	 * and the camera frames what will really be drawn. */
	float ringTurn[16];

	MatIdentity(ringTurn);

	if (axisView)
	{
		/* The model face-on to its own plane. The vertex cloud's axes, longest
		 * first, are sent onto x, y and z - so the plane the shape runs in
		 * lies in the screen and the direction it is flat in points at the
		 * camera (yaw 180 looks down -z, so +z is the camera's side). Row
		 * vectors: out[r][c] is the c-th axis's r-th element, which is the
		 * transpose of the axis matrix and therefore its inverse. */
		float axes[9], extent[3];

		if (HDModelPrincipalAxes(slot, axes, extent))
		{
			for (int r = 0; r < 3; r++)
				for (int c = 0; c < 3; c++)
					ringTurn[r * 4 + c] = axes[c * 3 + r];

			printf("axisview: the cloud's own axes sent onto x, y and z - the "
				"picture is face-on to the plane of %.1f x %.1f, %.1f thick\n",
				extent[0], extent[1], extent[2]);
		}
		else
			printf("axisview: this model has no axes to turn onto\n");
	}

	if (invRotSet)
	{
		/* The ring's own presentation (-invrot). The ring puts the item's three
		 * angles on the matrix stack with phd_RotYXZ - Y, then X, then Z, each
		 * post-multiplied into the column-vector stack - so in this tool's row
		 * vector matrices the model meets Z first, then X, then Y. Without this
		 * a picture is the item's OWN frame and says nothing about what the
		 * player sees. */
		float rx[16], ry[16], rz[16], t[16], all[16];

		MatRotX(invRot[0] * 360.0 / 65536.0, rx);
		MatRotY(invRot[1] * 360.0 / 65536.0, ry);
		MatRotZ(invRot[2] * 360.0 / 65536.0, rz);
		MatMul(rz, rx, t);
		MatMul(t, ry, all);
		MatMul(ringTurn, all, t);
		memcpy(ringTurn, t, sizeof(ringTurn));

		printf("ring rot: x %d y %d z %d (%.1f/%.1f/%.1f degrees)\n",
			invRot[0], invRot[1], invRot[2],
			invRot[0] * 360.0 / 65536.0, invRot[1] * 360.0 / 65536.0,
			invRot[2] * 360.0 / 65536.0);
	}

	/* And no presentation turn in a floor picture.
	 *
	 * -axisview and -invrot exist to look at a model from an angle a person
	 * chooses; the item's own frame is not a choice, it is where the thing
	 * lies. Mixing the two produces a bug of its own kind: the lift went onto a matrix with the ring turn in front of it, so
	 * with -invrot it displaced the model in the turned frame. With the turn
	 * forced out of a floor picture there is no such frame to be in. */
	if (floorDraw && floorPose && (axisView || invRotSet))
	{
		MatIdentity(ringTurn);

		printf("the drawn frame: -axisview / -invrot are left OUT of a -floor "
			"picture - the item's own frame is what it shows\n");
	}

	if ((axisView || invRotSet) && !(floorDraw && floorPose))
	{
		float lo[3], hi[3];

		for (int corner = 0; corner < 8; corner++)
		{
			const float v[3] =
			{
				(corner & 1) ? maxs[0] : mins[0],
				(corner & 2) ? maxs[1] : mins[1],
				(corner & 4) ? maxs[2] : mins[2]
			};

			for (int c = 0; c < 3; c++)
			{
				float d = ringTurn[3 * 4 + c];

				for (int r = 0; r < 3; r++)
					d += v[r] * ringTurn[r * 4 + c];

				if (!corner || d < lo[c]) lo[c] = d;
				if (!corner || d > hi[c]) hi[c] = d;
			}
		}

		printf("turned  : x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f - what the "
			"camera frames\n", lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);

		for (int c = 0; c < 3; c++)
		{
			mins[c] = lo[c];
			maxs[c] = hi[c];
		}
	}

	/* --- the camera ----------------------------------------------------- */
	const double persp = EnginePersp(height, fov);
	const double cx = 0.5 * (mins[0] + maxs[0]);
	const double cy = 0.5 * (mins[1] + maxs[1]);
	const double cz = 0.5 * (mins[2] + maxs[2]);
	const double radius = 0.5 * sqrt(
		(maxs[0] - mins[0]) * (maxs[0] - mins[0]) +
		(maxs[1] - mins[1]) * (maxs[1] - mins[1]) +
		(maxs[2] - mins[2]) * (maxs[2] - mins[2]));

	/* The classic projection puts a point at persp * x / z pixels off centre,
	 * so a model of the given radius fills 0.45 of the picture's height at
	 * this distance - close enough to look at, far enough that a turned limb
	 * still has room. */
	if (dist <= 0.0)
		dist = persp * radius / (0.45 * (double)height);

	const double d2r = 3.14159265358979323846 / 180.0;
	const double f[3] =
	{
		sin(yaw * d2r) * cos(pitch * d2r),
		sin(pitch * d2r),
		cos(yaw * d2r) * cos(pitch * d2r)
	};

	float view[16];

	LookAt(cx - f[0] * dist, cy - f[1] * dist, cz - f[2] * dist, yaw, pitch, view);

	D3DMATRIX projection;
	double a = 0, b = 0, boo = 0;

	HDRenderZConstants(20.0, 20480.0, &a, &b, &boo);
	HDRenderMakeProjection(persp, (double)width * 0.5, (double)height * 0.5,
		a, b, &projection);

	printf("camera  : %.0f units away, yaw %.0f, pitch %.0f, persp %.1f at %ldx%ld\n",
		dist, yaw, pitch, persp, width, height);

	/* --- the node matrices ---------------------------------------------- */
	ClassicSlot flat;

	if (!here)
	{
		flat.nodes = 32;
		flat.ok = true;
		here = &flat;
	}

	std::vector<float> matrices((size_t)here->nodes * 16);

	/* The frame the level draws in, where -floor asked for it: the frame's own
	 * matrices carry every translation and every rotation of the item's
	 * animation frame, so the bind-pose walk is not done at all. The pose itself was read before the camera, which had to frame
	 * it. */
	/* -animframe: a keyframe of one of the slot's own animations in place
	 * of the bind pose, on the bind pose's camera, so two keyframes are two
	 * pictures of one model from one place. */
	std::vector<float> animPose;

	if (animAsk >= 0 && here->slot >= 0 &&
		ReadAnimPose(level, *here, animAsk, animKey, animPose) > 0)
		printf("animframe: anim %d keyframe %d of slot %d\n", animAsk,
			animKey, here->slot);
	else if (animAsk >= 0)
		printf("animframe: anim %d keyframe %d not in the level - the "
			"bind pose is drawn\n", animAsk, animKey);

	BuildNodeMatrices(*here, view, turn, &matrices[0],
		!animPose.empty() ? &animPose[0] : floorDraw ? floorPose : 0);

	/* The matrix proof of the object path, without the
	 * game. A node matrix carries the node's origin into view space all by
	 * itself - its translation is that origin's view position - so the two
	 * roads to a pixel can be compared on it directly:
	 *
	 *   classic   sx = vx * f_persp / vz + centre, as aTransformLightClipMesh
	 *             computes it for every vertex of every mesh
	 *   device    the row vector through the projection matrix, the w
	 *             division, and the viewport this engine never sets - the
	 *             whole back buffer, MinZ 0, MaxZ 1
	 *
	 * The engine runs the identical comparison on the first HD item of every
	 * level, on the matrices its own stack produced, and writes HDLOG
	 * itemcheck. This is the same arithmetic on the same kind of matrices
	 * with no game around it. */
	if (itemcheck)
	{
		const float* pm = (const float*)&projection;
		double worstX = 0, worstY = 0;
		int used = 0, behind = 0;

		printf("\n-- itemcheck: the node origins, both roads ---------------------\n");
		printf("%5s %12s %12s %12s %10s %10s %8s %8s\n", "node",
			"view x", "view y", "view z", "classic x", "device x", "dx", "dy");

		for (int node = 0; node < here->nodes; node++)
		{
			const float* m = &matrices[(size_t)node * 16];
			const double vx = m[12], vy = m[13], vz = m[14];

			if (vz < 20.0)
			{
				behind++;
				continue;
			}

			const double zv = persp / vz;
			const double classicX = vx * zv + (double)width * 0.5;
			const double classicY = vy * zv + (double)height * 0.5;

			const double cxx = vx * pm[0] + vy * pm[4] + vz * pm[8] + pm[12];
			const double cyy = vx * pm[1] + vy * pm[5] + vz * pm[9] + pm[13];
			const double cww = vx * pm[3] + vy * pm[7] + vz * pm[11] + pm[15];

			if (cww <= 0.0)
			{
				behind++;
				continue;
			}

			const double deviceX = (cxx / cww + 1.0) * 0.5 * (double)width;
			const double deviceY = (1.0 - cyy / cww) * 0.5 * (double)height;
			const double dx = fabs(deviceX - classicX);
			const double dy = fabs(deviceY - classicY);

			if (dx > worstX) worstX = dx;
			if (dy > worstY) worstY = dy;

			used++;

			printf("%5d %12.1f %12.1f %12.1f %10.3f %10.3f %8.5f %8.5f\n",
				node, vx, vy, vz, classicX, deviceX, dx, dy);
		}

		printf("\n%d nodes compared, %d behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", used, behind, worstX, worstY);
		printf("%s\n", worstX < 1.0 && worstY < 1.0
			? "PASS - both roads put every node origin in the same pixel"
			: "FAIL - the projection and the classic transform disagree");
	}

	/* The ring proof, without the game.
	 * Two measurements; the second catches a projection that shears the
	 * item's matrix.
	 *
	 * One, the place. The model's bounding rectangle put on the screen twice
	 * at every place the ring puts an item:
	 *
	 *   classic  the eight corners through node 0's matrix into view space,
	 *            then sx = vx * persp / vz + X, sy = vy * persp / vz + Y -
	 *            the line aTransformLightClipMesh runs with f_centerx /
	 *            f_centery moved to the item's place (specific\output.cpp,
	 *            phd_PutPolygonsPickup)
	 *   engine   the SAME, untouched corners through the engine's own
	 *            off-centre projection - HDModelOffCentreMatrix, called here
	 *            and not copied - and the viewport, which is what
	 *            specific\hd\hd_object.cpp hands HDModelOffCentre for every
	 *            ring item
	 *
	 * Two, the light, the measurement that failed on the sheared-matrix
	 * approach. The
	 * ring's sun term over a sphere of sample normals, computed the way
	 * hd_model.hlsl computes it - the normal through the node's 3x3,
	 * renormalised, dotted with the ring's fixed direction - once on the
	 * matrix the engine hands the card and once on a sheared matrix (a
	 * constant pixel offset put in as a shear). The first column cannot move at all, because the
	 * engine's matrix is the classic one and a rotation leaves a unit normal
	 * a unit normal. The second is the shear's error kept as a number: it
	 * is what the ring really looked like before the offset moved into the
	 * projection, and a regression that put the shear back would make the
	 * first column read like the second.
	 *
	 * Both the position and the size of the rectangle have to agree: the ring
	 * must not be 4/3 too wide on a 16:9 window, a mistake TROne's own notes
	 * describe as "diary drawn buggy". It cannot happen
	 * here because TR5 computes f_persp from a 4:3 implied width whatever the
	 * window is (specific\3dmath.cpp, AlterFOV) and the HD projection is built
	 * from that very number - and the two aspect ratios below are that
	 * statement made into a measurement rather than an argument. */
	if (ringcheck)
	{
		struct RingAspect { const char* name; long w, h; };

		const RingAspect aspects[2] = { { "4:3", 640, 480 }, { "16:9", 1920, 1080 } };
		const float* m0 = &matrices[0];
		double corner[8][3];
		int c = 0;

		for (int xi = 0; xi < 2; xi++)
			for (int yi = 0; yi < 2; yi++)
				for (int zi = 0; zi < 2; zi++, c++)
				{
					const double mx = xi ? maxs[0] : mins[0];
					const double my = yi ? maxs[1] : mins[1];
					const double mz = zi ? maxs[2] : mins[2];

					corner[c][0] = mx * m0[0] + my * m0[4] + mz * m0[8] + m0[12];
					corner[c][1] = mx * m0[1] + my * m0[5] + mz * m0[9] + m0[13];
					corner[c][2] = mx * m0[2] + my * m0[6] + mz * m0[10] + m0[14];
				}

		/* The sample normals for the light half: the 26 directions of a
		 * 3x3x3 grid without its centre, normalised. Any spread of unit
		 * normals does - the point is that the engine's road leaves every
		 * one of them alone and the shear does not. */
		double normals[26][3];
		int normalCount = 0;

		for (int nx = -1; nx <= 1; nx++)
			for (int ny = -1; ny <= 1; ny++)
				for (int nz = -1; nz <= 1; nz++)
				{
					if (!nx && !ny && !nz)
						continue;

					const double len = sqrt((double)(nx * nx + ny * ny + nz * nz));

					normals[normalCount][0] = (double)nx / len;
					normals[normalCount][1] = (double)ny / len;
					normals[normalCount][2] = (double)nz / len;
					normalCount++;
				}

		// The ring's own sun, in view space: (0, -50, -100) over sqrt(12500),
		// which is what phd_PutPolygonsPickup sets up and HDInvEnd hands over.
		const double sun[3] = { 0.0, -50.0 / 111.80339887498948,
			-100.0 / 111.80339887498948 };

		printf("\n-- ringcheck: the ring's own off-centre projection -------------\n");
		printf("%6s %7s %7s %9s %9s %9s %9s %9s %9s %8s %8s\n", "aspect",
			"ring x", "ring y", "class w", "ring w", "class h", "ring h",
			"worst dx", "worst dy", "light", "if shear");

		double worstX = 0.0, worstY = 0.0, worstLight = 0.0, worstShearLight = 0.0;
		int places = 0, skipped = 0;

		for (int ai = 0; ai < 2; ai++)
		{
			const long w = aspects[ai].w;
			const long h = aspects[ai].h;
			const double p = EnginePersp(h, fov);
			const double centreX = (double)(w / 2);
			const double centreY = (double)(h / 2);
			/* OBJLIST_SPACING is phd_centerx >> 1 and the six visible slots
			 * sit at i = -3..2; the inventory row stands at GetFixedScale(84)
			 * and the ammo row at GetFixedScale(380) (game\newinv2.cpp,
			 * draw_current_object_list). */
			const long spacing = (long)(w / 2) >> 1;
			const long rows[2] = { RingFixedScale(84, w, h), RingFixedScale(380, w, h) };

			double za = 0, zb = 0, zboo = 0;
			D3DMATRIX ringProj;

			HDRenderZConstants(20.0, 20480.0, &za, &zb, &zboo);
			HDRenderMakeProjection(p, centreX, centreY, za, zb, &ringProj);

			for (int ri = 0; ri < 2; ri++)
				for (int i = -3; i <= 2; i++)
				{
					const double X = centreX + (double)(i * spacing);
					const double Y = (double)rows[ri];
					const double kx = (X - centreX) / p;
					const double ky = (Y - centreY) / p;

					/* The engine's own off-centre projection, out of the
					 * engine's own file - hd_model.cpp is linked into this
					 * tool, so this is HDModelOffCentre's arithmetic and not
					 * a copy of it. The node matrices stay untouched. */
					D3DMATRIX offProj;

					HDModelOffCentreMatrix(&ringProj, (float)kx, (float)ky,
						&offProj);

					const float* om = (const float*)&offProj;

					/* And the matrix the earlier approach sheared instead, for the
					 * light column below - never for the geometry. */
					float sheared[16];

					memcpy(sheared, m0, sizeof(sheared));
					RingShear(sheared, kx, ky);

					double cl = 1e30, cr = -1e30, ct = 1e30, cb = -1e30;
					double sl = 1e30, sr = -1e30, st = 1e30, sb = -1e30;
					bool behind = false;

					for (int k = 0; k < 8; k++)
					{
						const double vz = corner[k][2];

						if (vz < 20.0)
						{
							behind = true;
							break;
						}

						const double zv = p / vz;
						const double cx2 = corner[k][0] * zv + X;
						const double cy2 = corner[k][1] * zv + Y;

						if (cx2 < cl) cl = cx2;
						if (cx2 > cr) cr = cx2;
						if (cy2 < ct) ct = cy2;
						if (cy2 > cb) cb = cy2;

						/* The very same view-space corner - the UNTOUCHED
						 * matrix - through the engine's off-centre
						 * projection and the viewport. */
						const double sx = corner[k][0];
						const double sy = corner[k][1];
						const double sz = corner[k][2];

						const double px = sx * om[0] + sy * om[4] + sz * om[8] + om[12];
						const double py = sx * om[1] + sy * om[5] + sz * om[9] + om[13];
						const double pw = sx * om[3] + sy * om[7] + sz * om[11] + om[15];

						if (pw <= 0.0)
						{
							behind = true;
							break;
						}

						const double dx2 = (px / pw + 1.0) * 0.5 * (double)w;
						const double dy2 = (1.0 - py / pw) * 0.5 * (double)h;

						if (dx2 < sl) sl = dx2;
						if (dx2 > sr) sr = dx2;
						if (dy2 < st) st = dy2;
						if (dy2 > sb) sb = dy2;
					}

					if (behind)
					{
						skipped++;
						continue;
					}

					const double dl = fabs(sl - cl), dr = fabs(sr - cr);
					const double dt = fabs(st - ct), db = fabs(sb - cb);
					const double dxw = dl > dr ? dl : dr;
					const double dyw = dt > db ? dt : db;

					if (dxw > worstX) worstX = dxw;
					if (dyw > worstY) worstY = dyw;

					/* The light, the way hd_model.hlsl works it out: the
					 * normal through the node's 3x3, renormalised, dotted
					 * with the ring's sun and clamped at zero. Once on the
					 * matrix the engine hands the card (which is the classic
					 * one, untouched) and once on the sheared matrix the
					 * earlier approach handed it. */
					double plain = 0.0, shear = 0.0, classic = 0.0;

					for (int n = 0; n < normalCount; n++)
					{
						const double* nv = normals[n];
						const float* road[2] = { m0, sheared };
						double term[2] = { 0.0, 0.0 };

						for (int r = 0; r < 2; r++)
						{
							const float* mm = road[r];
							const double vx = nv[0] * mm[0] + nv[1] * mm[4] + nv[2] * mm[8];
							const double vy = nv[0] * mm[1] + nv[1] * mm[5] + nv[2] * mm[9];
							const double vz2 = nv[0] * mm[2] + nv[1] * mm[6] + nv[2] * mm[10];
							const double len = sqrt(vx * vx + vy * vy + vz2 * vz2);
							const double inv = len > 1e-12 ? 1.0 / len : 0.0;
							const double dot = (vx * sun[0] + vy * sun[1] + vz2 * sun[2]) * inv;

							term[r] = dot > 0.0 ? dot : 0.0;

							/* The classic path does NOT renormalise - it
							 * dots the model normal with the sun turned into
							 * model space, which is the same number as the
							 * transformed normal dotted with the sun
							 * (specific\output.cpp,
							 * aTransformLightClipMesh). For a rotation the
							 * two are equal anyway, and that equality is
							 * what the plain column is being checked
							 * against. */
							if (r == 0)
							{
								const double raw = vx * sun[0] + vy * sun[1] + vz2 * sun[2];

								classic += raw > 0.0 ? raw : 0.0;
							}
						}

						plain += term[0];
						shear += term[1];
					}

					plain /= (double)normalCount;
					shear /= (double)normalCount;
					classic /= (double)normalCount;

					const double dLight = fabs(plain - classic);
					const double dShear = classic > 1e-9
						? fabs(shear - classic) / classic : 0.0;

					if (dLight > worstLight) worstLight = dLight;
					if (dShear > worstShearLight) worstShearLight = dShear;

					places++;

					printf("%6s %7.0f %7.0f %9.2f %9.2f %9.2f %9.2f %9.5f %9.5f "
						"%+7.4f %+7.1f%%\n",
						aspects[ai].name, X, Y, cr - cl, sr - sl, cb - ct,
						sb - st, dxw, dyw, plain - classic,
						100.0 * (shear - classic) / (classic > 1e-9 ? classic : 1.0));
				}
		}

		printf("\n%d ring places compared, %d skipped behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", places, skipped,
			worstX, worstY);
		printf("the light: worst |engine - classic| %.9f over %d normals a "
			"place; a sheared node matrix would have been off by up to "
			"%.1f %%\n", worstLight, normalCount, 100.0 * worstShearLight);
		printf("%s\n", places && worstX <= 1.0 && worstY <= 1.0
			&& worstLight <= 1e-6
			? "PASS - the ring's own projection puts the bounding rectangle "
			  "where the off-centre projection puts it, at the same size, "
			  "and leaves the light alone"
			: "FAIL - the ring's projection and the classic off-centre "
			  "projection disagree");
	}

	/* -ring <x> <y> draws the model where the
	 * inventory ring would put it, by the engine's own road - the node
	 * matrices are left exactly as they are and the offset goes into the
	 * projection at the draw call below, which is what hd_object.cpp does per
	 * ring item. So the picture shows what the ring really shows, light
	 * included: -ringlight 160 at the middle of the ring and at the ammo row
	 * has to come out at the same brightness as with no -ring at all. */
	double ringKx = 0.0, ringKy = 0.0;

	if (ringSet)
	{
		ringKx = (ringX - (double)(width / 2)) / persp;
		ringKy = (ringY - (double)(height / 2)) / persp;

		printf("\nring    : item at %.0f, %.0f - off-centre kx %.6f, ky %.6f "
			"(the projection, not the matrices)\n",
			ringX, ringY, ringKx, ringKy);
	}

	std::vector<float> light((size_t)here->nodes * 3, (float)shade);

	HDModelLighting lighting;

	lighting.lights = lightCount ? lights : 0;
	lighting.lightCount = lightCount;
	/* 0.75 is the factor aTransformLightClipMesh gives a sun in the world and
	 * 2.0 the one it gives it in the inventory ("fucking shit", says the
	 * original comment) - so -ringlight sets the second. */
	lighting.sunScale = ringLight ? 2.0f : 0.75f;
	// The sheen is Lara's alone; an object gets none.
	lighting.specStrength = 0.0f;
	lighting.specPower = 1.0f;

	/* The face's lips - see LipsAsk - on the separate face, or on the model
	 * itself where the model is a face (-name LARA_HEAD -rigid). Not with the
	 * ring's model in the slot's place. */
	const bool lipsOnHead = head && HDModelHasHead(slot);
	const int lipsTrack = lips.track >= 0 && optionSlot < 0
		? LipsOpen(lips, slot, lipsOnHead) : -1;

	/* --- draw ------------------------------------------------------------ */
	printf("\n-- the picture ------------------------------------------------\n");

	Shot shot;

	if (!ShotBegin(shot, width, height))
	{
		ShotEnd(shot);
		HDModelShutdown();
		HDRenderShutdown();
		DropDevice();
		return 7;
	}

	HDFrameLighting frame;
	float wibble[32];

	memset(&frame, 0, sizeof(frame));

	for (int i = 0; i < 32; i++)
		wibble[i] = 0.0f;

	frame.centerX = (float)width * 0.5f;
	frame.centerY = (float)height * 0.5f;
	frame.distanceFogStart = 1.0e9f;	// no fog in a studio picture
	frame.wibbleTable = wibble;

	const int bulbCount = PutUnder(frame, view, PutBulbs(frame));

	g_device->BeginScene();
	HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
	HDModelFrameBegin(&projection, bulbCount);

	/* The inventory ring's own screen centre, exactly as
	 * hd_object.cpp hands it over per ring item. Zero without -ring, and then
	 * the call writes nothing at all. */
	HDModelOffCentre((float)ringKx, (float)ringKy);

	/* A mod's own inventory model, drawn the way the ring
	 * draws it: the correction in front of the item's own node matrix, one
	 * node, every node drawn. Same two calls hd_object.cpp makes. */
	bool drew = false;

	/* The floor plane the picture gets, in the slot's own frame, and
	 * whether it is known at all - a -name run has no level and so no classic
	 * mesh to stand on. */
	double floorPlaneAt = 0.0;
	bool floorPlaneKnown = false;

	if (optionSlot >= 0 && HDModelHas(optionSlot))
	{
		float fix[16], placed[16];

		/* The donor game's frame 0, first. The fit below
		 * measures the model the pose left behind, so the pose's own verdict
		 * belongs above it - and a run where it did not happen has to say so,
		 * or an unexplained box is a box nobody can explain. */
		{
			HDModelClassicPose pose;

			if (HDModelClassicPoseOf(optionSlot, &pose))
			{
				if (pose.applied)
					printf("\nclassic pose: %s posed with frame 0 of game %d "
						"object %d, out of %d\\DATA\\%s - %d nodes, box "
						"%.0fx%.0fx%.0f -> %.0fx%.0fx%.0f\n",
						HDModelName(optionSlot), pose.game, pose.object,
						pose.game, pose.level, pose.nodes, pose.before[0],
						pose.before[1], pose.before[2], pose.after[0],
						pose.after[1], pose.after[2]);
				/* And which object it was about. The reason does not
				 * name the game and the object - that would name them twice in
				 * the engine's own line - so the printer names them, here as
				 * there, and only where they are known. */
				else if (pose.game && pose.object)
					printf("\nclassic pose: NOT applied to %s - game %d object "
						"%d: %s. It is drawn as the file stands.\n",
						HDModelName(optionSlot), pose.game, pose.object,
						pose.why);
				else
					printf("\nclassic pose: NOT applied to %s - %s. It is drawn "
						"as the file stands.\n", HDModelName(optionSlot),
						pose.why);
			}
			/* And a run that took the pose off says so. -noclassicpose
			 * asks for no pose at all, so hd_model keeps no decision for this
			 * slot and the block above printed nothing - which left the two
			 * runs the switch exists to compare telling apart only by a box
			 * measurement further down. */
			else if (noClassicPose)
				printf("\nclassic pose: not asked for (-noclassicpose) - %s is "
					"drawn exactly as the file stands, with no classic pose on "
					"the ring.\n", HDModelName(optionSlot));
		}

		if (HDModelRingFit(optionSlot, slot, fix))
		{
			float srcMin[3], srcMax[3], dstMin[3], dstMax[3];

			HDModelBounds(optionSlot, srcMin, srcMax);
			HDModelBounds(slot, dstMin, dstMax);

			/* And how far it leaves that box. The engine's own figure, from the engine's own
			 * function - the scale matches the longest axes only, so the
			 * other two land where their proportions put them. */
			int outAxis = -1;
			const float over = HDModelRingFitOverflow(optionSlot, slot,
				&outAxis);
			/* The target's own axes and not x/y/z. The fit pairs the two models'
			 * shapes, so the axis handed back is one of the target's
			 * principal axes, longest first - the same three words the
			 * engine prints (hd_object.cpp, box_verdict). Naming it "z"
			 * sent a reader to the model's z extent, which is a number
			 * that cannot produce the ratio printed beside it. */
			static const char* kAxis[3] =
			{
				"its longest axis", "its middle axis", "its flattest axis"
			};

			printf("\ninventory: %s stands in for %s - its box "
				"%.0fx%.0fx%.0f against %.0fx%.0fx%.0f, scale %.2f, centre "
				"moved by %.0f/%.0f/%.0f, %.2fx of that box at its worst"
				", on %s\n", HDModelName(optionSlot),
				HDModelName(slot), srcMax[0] - srcMin[0], srcMax[1] - srcMin[1],
				srcMax[2] - srcMin[2], dstMax[0] - dstMin[0],
				dstMax[1] - dstMin[1], dstMax[2] - dstMin[2],
				sqrt(fix[0] * fix[0] + fix[1] * fix[1] + fix[2] * fix[2]),
				fix[12], fix[13], fix[14], over,
				outAxis >= 0 && outAxis < 3 ? kAxis[outAxis] : "?");

			/* And where it lies on the floor - the engine's own
			 * arithmetic, not a copy of it. The level pickup takes this model
			 * and the number is
			 * what it is lifted by, measured on the vertices through the fit
			 * against the classic mesh's own deepest point. */
			if (here && here->slot >= 0)
			{
				float classicLowest = 0.0f, lowest = 0.0f, lift = 0.0f;

				if (ClassicMeshLowest(level, *here, floorPose, &classicLowest, 0) &&
					HDModelFloorLift(optionSlot, fix, floorPose, here->nodes,
						classicLowest, &lowest, &lift))
				{
					printf("           the floor: the classic mesh reaches y "
						"%.1f, the fitted model y %.1f - %.1f units %s it, "
						"so the level pickup draws it %s\n", classicLowest,
						lowest, fabs(lowest - classicLowest),
						lowest > classicLowest ? "UNDER" : "above",
						lift > 0.0f ? "lifted" : "where the fit puts it");
					printf("           lowest point before %.1f, lift %.1f, "
						"lowest point after %.1f (the classic mesh's own is "
						"%.1f)%s\n", lowest, lift, lowest - lift, classicLowest,
						noFloorLift ? " - NOT APPLIED (-nofloorlift), this is "
							"the picture from before the rule" : "");

					if (floorDraw)
					{
						/* The lift goes straight up in the world, at the view
						 * end of the chain - LiftNodes, the same move the
						 * engine makes and in the same space. On the fit's own
						 * translation row, which
						 * is the item's frame, -invrot would displace the
						 * model in the turned frame, and for a slot whose
						 * animation frame stands the model on its head it would
						 * be the wrong direction altogether. */
						if (!noFloorLift)
							LiftNodes(&matrices[0], here->nodes, lift, view);

						floorPlaneAt = classicLowest;
						floorPlaneKnown = true;
					}
				}
				else
					printf("           the floor: not measured - the slot has "
						"no classic mesh with vertices, or the fitted model "
						"kept no points\n");
			}

			/* Where the fit really put it: the source's own
			 * axes and box corners through the fit, so a picture that looks
			 * wrong can be read as numbers. */
			{
				float sa[9], se[3];

				if (HDModelPrincipalAxes(optionSlot, sa, se))
				{
					printf("           the source's axes land at:\n");

					for (int k = 0; k < 3; k++)
					{
						float o[3];

						for (int c = 0; c < 3; c++)
						{
							o[c] = 0.0f;

							for (int r = 0; r < 3; r++)
								o[c] += sa[k * 3 + r] * fix[r * 4 + c];
						}

						const float n = sqrtf(o[0]*o[0] + o[1]*o[1] + o[2]*o[2]);

						printf("             %d ext %6.1f -> (%+6.3f %+6.3f %+6.3f)\n",
							k, se[k], o[0]/n, o[1]/n, o[2]/n);
					}
				}

				float lo[3], hi[3];

				for (int corner = 0; corner < 8; corner++)
				{
					const float v[3] =
					{
						(corner & 1) ? srcMax[0] : srcMin[0],
						(corner & 2) ? srcMax[1] : srcMin[1],
						(corner & 4) ? srcMax[2] : srcMin[2]
					};

					for (int c = 0; c < 3; c++)
					{
						float d = fix[3 * 4 + c];

						for (int r = 0; r < 3; r++)
							d += v[r] * fix[r * 4 + c];

						if (!corner || d < lo[c]) lo[c] = d;
						if (!corner || d > hi[c]) hi[c] = d;
					}
				}

				printf("           fitted box x %.0f..%.0f y %.0f..%.0f "
					"z %.0f..%.0f (%.0fx%.0fx%.0f)\n", lo[0], hi[0], lo[1],
					hi[1], lo[2], hi[2], hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2]);
			}

			float turned[16];

			MatMul(fix, ringTurn, turned);
			MatMul(turned, &matrices[0], placed);
			drew = HDModelDraw(optionSlot, placed, 1, &light[0], ~0u, &lighting);
		}
		else
			printf("\ninventory: %s has no bounding box to fit - the slot's "
				"own model is drawn\n", HDModelName(optionSlot));
	}

	/* And the slot's own model on the floor, which is the other half
	 * of the rule: no fit, no OPTION file, just the model the slot is bound to
	 * against the classic mesh it replaces. Both lowest points are taken in
	 * the frame the level draws the item in and the lift goes straight up in
	 * the world - the same two decisions hd_object.cpp makes, through the same
	 * function for the first and the same arithmetic for the second. */
	if (floorDraw && !drew && here && here->slot >= 0)
	{
		float classicLowest = 0.0f, lowest = 0.0f, lift = 0.0f;

		if (ClassicMeshLowest(level, *here, floorPose, &classicLowest, 0) &&
			HDModelFloorLift(slot, 0, floorPose, here->nodes, classicLowest,
				&lowest, &lift))
		{
			printf("\nthe floor: the classic mesh reaches y %.1f, %s y %.1f - "
				"%.1f units %s it\n", classicLowest, HDModelName(slot), lowest,
				fabs(lowest - classicLowest),
				lowest > classicLowest ? "UNDER" : "above");
			printf("           lowest point before %.1f, lift %.1f, lowest "
				"point after %.1f (the classic mesh's own is %.1f)%s\n",
				lowest, lift, lowest - lift, classicLowest,
				noFloorLift ? " - NOT APPLIED (-nofloorlift), this is the "
					"picture from before the rule" : "");

			floorPlaneAt = classicLowest;
			floorPlaneKnown = true;

			if (!noFloorLift)
				LiftNodes(&matrices[0], here->nodes, lift, view);
		}
		else
			printf("\nthe floor: not measured - the slot has no classic mesh "
				"with vertices in it\n");
	}

	/* The plane itself, over the picture: a square in the floor plane,
	 * as wide as the model's own box, so that a gun lying on the ground can be
	 * SEEN to be lying on it. It is drawn into the pixels after the render -
	 * see OverlayPlane - and never counted into the background percentage. */
	if (floorDraw && floorPlaneKnown)
	{
		/* The model's own footprint, and not a bigger square.
		 *
		 * A plane seen from a low camera is not a line: a point ON it but
		 * nearer the eye projects further down the picture than one at the
		 * same height further away, and the gap grows with the depth. So a
		 * square wider than the model puts edges where no part of the model
		 * can reach them, and the eye then reads "sunk" off perspective
		 * alone. Drawn at the model's own box, the near edge stands where the
		 * model's near face does and the far edge where its far face does, so
		 * the silhouette's lowest point meets the outline where it should.
		 * The two diagonals are there so that the quad reads as a surface. */
		OverlayClear();

		const double halfX = 0.5 * (maxs[0] - mins[0]);
		const double halfZ = 0.5 * (maxs[2] - mins[2]);
		const double cxm = 0.5 * (mins[0] + maxs[0]);
		const double czm = 0.5 * (mins[2] + maxs[2]);

		OverlayPlane(view, persp, width, height, cxm, czm, floorPlaneAt,
			halfX > 8.0 ? halfX : 8.0, halfZ > 8.0 ? halfZ : 8.0, 0x0000FF00u);
		OverlayPlane(view, persp, width, height, cxm, czm, floorPlaneAt,
			halfX > 8.0 ? halfX : 8.0, 0.0, 0x0000C000u);

		/* And a patch under the deepest point, which is
		 * the one place on that plane where the model can touch it.
		 *
		 * The quad above spans the model's whole footprint, and at a low
		 * camera its near edge stands well in front of the point the rule is
		 * about - so a model resting exactly on the plane still shows a gap
		 * there, and a picture without this patch reads as "floating"
		 * when the numbers said the contact was exact.
		 *
		 * What it can and cannot show. Where the deepest point is also the
		 * lowest thing on the screen - the crowbar's tip, the fitted pair of
		 * Uzis - the model's outline meets the patch and the contact is
		 * plain. Where it is not - a box whose far bottom corner is the
		 * deepest and whose near face projects lower - the patch crosses the
		 * silhouette, because the overlay is drawn into the pixels after the
		 * model and takes no part in the depth test. The picture is there to
		 * show the frame the item is drawn in and how far the lift moved it;
		 * that the lift lands exactly on the mesh is the numbers' work. */
		OverlayPlane(view, persp, width, height, floorDeepest[0],
			floorDeepest[2], floorPlaneAt, 24.0, 24.0, 0x0000FF00u);
	}

	if (!drew)
	{
		/* Every node's matrix and not one. A single float[16] handed to
		 * HDModelDraw with the slot's node count beside it would make the draw
		 * read past the end of a stack array for every node but the root and
		 * skinned that node's vertices with whatever was there. A model whose
		 * geometry sits on node 0 drew correctly and everything else vanished
		 * - which came within one measurement of reporting
		 * an MP5 mod (5478 vertices, all on node 1) as an invisible
		 * file. It was the tool that was blind, from putting the
		 * turn in front of the matrix.
		 *
		 * The turn goes in front of every node, so it turns the whole object
		 * about the model origin. A one-node inventory model gets exactly what
		 * it got before. */
		std::vector<float> own((size_t)here->nodes * 16);

		for (int node = 0; node < here->nodes; node++)
			MatMul(ringTurn, &matrices[(size_t)node * 16],
				&own[(size_t)node * 16]);

		/* A model that is itself a face (-name LARA_HEAD -rigid) takes the
		 * lips on its own draw; a body with a separate face leaves them to
		 * the face below. */
		drew = HDModelDraw(slot, &own[0], here->nodes,
			&light[0], nodeMask, &lighting, 0,
			cullMode >= 0 ? cullMode : HD_MODEL_CULL_NONE,
			lipsOnHead ? -1 : lipsTrack, lipsOnHead || lipsTrack < 0 ? -1 : lips.frames[0]);

		/* -classicdots: every vertex of the classic mesh of each node, in
		 * the node's own frame (the .TRC's shorts, the bind offset left
		 * out), through the same matrix the .TRM node was just drawn with -
		 * a node matrix takes a point of its node into view space. Where
		 * the model stands where the classic meshes stand, the dots lie on
		 * it in every keyframe; a part the model leaves out or puts
		 * elsewhere leaves its dots on the background. */
		if (g_dotsAsked && here->slot >= 0)
			for (int node = 0; node < here->nodes; node++)
			{
				std::vector<float> cv;
				const int verts = ClassicNodeVertices(level, *here, node, cv);
				const float* m = &own[(size_t)node * 16];

				for (int v = 0; v < verts; v++)
				{
					OverlayDot dot;
					const float* at = &cv[(size_t)v * 3];
					const double x = at[0] - here->bind[node][0];
					const double y = at[1] - here->bind[node][1];
					const double z = at[2] - here->bind[node][2];

					if (!OverlayProject(m, persp, width, height, x, y, z,
						&dot.x, &dot.y))
						continue;

					dot.node = node;
					g_dots.push_back(dot);

					OverlayLine line;

					line.colour = 0x0000FFFFu;
					line.x0 = dot.x - 2.0;
					line.x1 = dot.x + 2.0;
					line.y0 = line.y1 = dot.y;
					g_overlay.push_back(line);
					line.x0 = line.x1 = dot.x;
					line.y0 = dot.y - 2.0;
					line.y1 = dot.y + 2.0;
					g_overlay.push_back(line);
				}
			}
	}

	bool drewHead = false;

	if (head && optionSlot < 0)
		drewHead = HDModelDrawHead(slot, &matrices[0], here->nodes,
			&light[0], nodeMask, &lighting, 0, HD_MODEL_CULL_NONE,
			lipsOnHead ? lipsTrack : -1, lipsOnHead && lipsTrack >= 0 ? lips.frames[0] : -1);

	const int calls = HDModelDrawCalls();

	HDRenderFrameEnd();
	g_device->EndScene();

	printf("HDModelDraw returned %s after %d draw calls%s\n",
		drew ? "true" : "FALSE", calls,
		drewHead ? " (the separate face came along)" : "");

	LipsSay();

	char path[512];
	const std::string lipsName = LipsName(lips, lipsTrack, false);

	sprintf(path, "%s_%s%s.png", out.c_str(), HDModelName(slot), lipsName.c_str());
	ShotWrite(shot, path);

	/* And the sheet: the same body and face, one picture per frame, the
	 * plain draw of the two and nothing of the ring or the floor. */
	if (lipsTrack >= 0 && lips.sheet && lips.frames.size() > 1)
	{
		const int pictures = (int)lips.frames.size();
		int across = 1;
		long sheetW = width, sheetH = height;
		std::vector<unsigned long> sheet, tile;
		std::vector<float> own((size_t)here->nodes * 16);

		LipsSheetSize(pictures, width, height, &across, &sheetW, &sheetH);
		sheet.assign((size_t)sheetW * (size_t)sheetH, 0xFF000000u);

		for (int node = 0; node < here->nodes; node++)
			MatMul(ringTurn, &matrices[(size_t)node * 16], &own[(size_t)node * 16]);

		for (int picture = 0; picture < pictures; picture++)
		{
			const int lipsFrame = lips.frames[(size_t)picture];

			g_device->Clear(0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
				(D3DCOLOR)(0xFF000000u | g_clear), 1.0f, 0);
			g_device->BeginScene();
			HDRenderFrameBegin((const D3DMATRIX*)view, &projection, &frame);
			HDModelFrameBegin(&projection, bulbCount);

			HDModelDraw(slot, &own[0], here->nodes, &light[0], nodeMask, &lighting,
				0, HD_MODEL_CULL_NONE, lipsOnHead ? -1 : lipsTrack, lipsFrame);

			if (lipsOnHead)
				HDModelDrawHead(slot, &matrices[0], here->nodes, &light[0],
					nodeMask, &lighting, 0, HD_MODEL_CULL_NONE, lipsTrack,
					lipsFrame);

			HDRenderFrameEnd();
			g_device->EndScene();
			LipsSay();

			if (ShotGrab(shot, tile))
				LipsPaste(sheet, sheetW, tile, width, height, picture, across);
		}

		sprintf(path, "%s_%s%s.png", out.c_str(), HDModelName(slot),
			LipsName(lips, lipsTrack, true).c_str());

		const bool wrote = WritePng(path, &sheet[0], sheetW, sheetH);

		printf("  %-58s %d frames, %d across, %s\n", path, pictures, across,
			wrote ? "written" : "NOT written");
	}

	ShotEnd(shot);

	/* The pickup display, in three halves.
	 *
	 * The little turning object in the corner after Lara picks something up.
	 * It is the SECOND caller of DrawInventoryItemMe and it goes through the
	 * very same four call sites as the inventory ring, so the arithmetic under
	 * it is -ringcheck's - asked at the one place the display stands rather
	 * than at the ring's twenty-four:
	 *
	 *   x = phd_winwidth  - GetFixedScale(80) + PickupX
	 *   y = phd_winheight - GetFixedScale(75)
	 *
	 * (specific\output.cpp, S_DrawPickup; INVOBJ::yoff shifts y a little
	 * further and is per LEVEL, out of SCRIPT.DAT - the offset arithmetic is
	 * linear in y, so any y proves the same thing.) PickupX is the slide, from
	 * GetFixedScale(128) fully out to 0 fully in (game\health.cpp), and both
	 * ends are measured because the slide is exactly what moves the display
	 * about the screen.
	 *
	 * What is different from the ring, and it is one number: the sun's scale.
	 * aTransformLightClipMesh doubles a sun while InventoryActive and takes
	 * three quarters of it otherwise, and the display is drawn with
	 * InventoryActive at 0 - so the light half below uses 0.75 where
	 * -ringlight uses 2.0.
	 */
	if (pickupcheck)
	{
		struct DisplayWindow { const char* name; long w, h; };

		const DisplayWindow windows[2] =
		{
			{ "1920x1080", 1920, 1080 },
			{ "2560x1440", 2560, 1440 }
		};

		const double spins[2] = { 0.0, 90.0 };

		printf("\n-- pickupcheck: the place ---------------------------------------\n");
		printf("%10s %4s %6s %7s %7s %9s %9s %9s %9s %9s %9s\n", "window",
			"rot", "slide", "x", "y", "class w", "disp w", "class h", "disp h",
			"worst dx", "worst dy");

		double worstX = 0.0, worstY = 0.0;
		int places = 0, skipped = 0;

		for (int wi = 0; wi < 2; wi++)
		{
			const long w = windows[wi].w;
			const long h = windows[wi].h;
			const double pp = EnginePersp(h, fov);
			const double centreX = (double)(w / 2);
			const double centreY = (double)(h / 2);
			const double baseX = (double)(w - RingFixedScale(80, w, h));
			const double Y = (double)(h - RingFixedScale(75, w, h));
			const double slides[2] = { 0.0, (double)RingFixedScale(128, w, h) };

			double za = 0, zb = 0, zboo = 0;
			D3DMATRIX flat;

			HDRenderZConstants(20.0, 20480.0, &za, &zb, &zboo);
			HDRenderMakeProjection(pp, centreX, centreY, za, zb, &flat);

			for (int si = 0; si < 2; si++)
				for (int ri = 0; ri < 2; ri++)
				{
					/* The rotation is in the matrices, not in the offset:
					 * S_DrawPickup spins the object on
					 * (GnFrameCounter & 0x7F) << 9, which reaches
					 * DrawInventoryItemMe as item->pos.y_rot and goes onto
					 * the matrix stack before the node walk. Both paths get
					 * the very same matrices out of it - which is the point:
					 * a turned object must land in the same rectangle on
					 * both roads, not only a square-on one. */
					double spun[32][2];

					memcpy(spun, turn, sizeof(spun));
					spun[0][0] += spins[ri];

					std::vector<float> spunMat((size_t)here->nodes * 16);

					BuildNodeMatrices(*here, view, spun, &spunMat[0]);

					const float* m0 = &spunMat[0];
					const double X = baseX + slides[si];
					const double kx = (X - centreX) / pp;
					const double ky = (Y - centreY) / pp;

					D3DMATRIX offProj;

					HDModelOffCentreMatrix(&flat, (float)kx, (float)ky, &offProj);

					const float* om = (const float*)&offProj;

					double cl = 1e30, cr = -1e30, ct = 1e30, cb = -1e30;
					double dl = 1e30, dr = -1e30, dt = 1e30, db = -1e30;
					bool behind = false;

					for (int xi = 0; xi < 2 && !behind; xi++)
						for (int yi = 0; yi < 2 && !behind; yi++)
							for (int zi = 0; zi < 2 && !behind; zi++)
							{
								const double mx = xi ? maxs[0] : mins[0];
								const double my = yi ? maxs[1] : mins[1];
								const double mz = zi ? maxs[2] : mins[2];

								const double vx = mx * m0[0] + my * m0[4] + mz * m0[8] + m0[12];
								const double vy = mx * m0[1] + my * m0[5] + mz * m0[9] + m0[13];
								const double vz = mx * m0[2] + my * m0[6] + mz * m0[10] + m0[14];

								if (vz < 20.0)
								{
									behind = true;
									break;
								}

								/* The classic road: aTransformLightClipMesh
								 * with f_centerx / f_centery moved to the
								 * display's own x / y for the length of one
								 * mesh, which is what phd_PutPolygonsPickup
								 * does (specific\output.cpp). */
								const double zv = pp / vz;
								const double cx2 = vx * zv + X;
								const double cy2 = vy * zv + Y;

								if (cx2 < cl) cl = cx2;
								if (cx2 > cr) cr = cx2;
								if (cy2 < ct) ct = cy2;
								if (cy2 > cb) cb = cy2;

								/* The engine's road: the same untouched
								 * corner through HDModelOffCentreMatrix -
								 * the engine's own line, linked into this
								 * tool, not a copy of it - and the viewport.
								 */
								const double px = vx * om[0] + vy * om[4] + vz * om[8] + om[12];
								const double py = vx * om[1] + vy * om[5] + vz * om[9] + om[13];
								const double pw = vx * om[3] + vy * om[7] + vz * om[11] + om[15];

								if (pw <= 0.0)
								{
									behind = true;
									break;
								}

								const double dx2 = (px / pw + 1.0) * 0.5 * (double)w;
								const double dy2 = (1.0 - py / pw) * 0.5 * (double)h;

								if (dx2 < dl) dl = dx2;
								if (dx2 > dr) dr = dx2;
								if (dy2 < dt) dt = dy2;
								if (dy2 > db) db = dy2;
							}

					if (behind)
					{
						skipped++;
						continue;
					}

					const double e1 = fabs(dl - cl), e2 = fabs(dr - cr);
					const double e3 = fabs(dt - ct), e4 = fabs(db - cb);
					const double ex = e1 > e2 ? e1 : e2;
					const double ey = e3 > e4 ? e3 : e4;

					if (ex > worstX) worstX = ex;
					if (ey > worstY) worstY = ey;

					places++;

					printf("%10s %4.0f %6s %7.0f %7.0f %9.2f %9.2f %9.2f %9.2f "
						"%9.5f %9.5f\n",
						windows[wi].name, spins[ri], si ? "out" : "in", X, Y,
						cr - cl, dr - dl, cb - ct, db - dt, ex, ey);
				}
		}

		printf("\n%d places compared, %d skipped behind the near plane; "
			"worst dx %.5f px, worst dy %.5f px\n", places, skipped,
			worstX, worstY);

		/* --- The light, end to end ---------------------------------------
		 *
		 * The same object drawn twice at 1920x1080 with the display's own
		 * light - once about the screen CENTRE and once about the display's
		 * CORNER - and then compared where they overlap after the whole known
		 * pixel shift. X, Y and the screen centre are all whole pixels, and
		 * the off-centre projection is exactly X - centreX pixels, so the two
		 * pictures must be the SAME picture moved by (dx, dy): not "close in
		 * mean brightness" but equal pixel for pixel.
		 *
		 * The camera is pushed back for this half so that the object fits in
		 * the corner whole - a model that runs off the edge would have the two
		 * pictures overlap in only part of itself, and a mean over a subset
		 * proves less than a mean over the same set. */
		const long lw = 1920, lh = 1080;
		const double lp = EnginePersp(lh, fov);
		const double lcx = (double)(lw / 2), lcy = (double)(lh / 2);
		const double lx = (double)(lw - RingFixedScale(80, lw, lh));
		const double ly = (double)(lh - RingFixedScale(75, lw, lh));
		double ldist = lp * radius / (0.055 * (double)lh);

		float lview[16];
		D3DMATRIX lproj;

		LookAt(cx - f[0] * ldist, cy - f[1] * ldist, cz - f[2] * ldist,
			yaw, pitch, lview);
		HDRenderMakeProjection(lp, lcx, lcy, a, b, &lproj);

		std::vector<float> lmat((size_t)here->nodes * 16);

		BuildNodeMatrices(*here, lview, turn, &lmat[0]);

		/* The display's own light, which is phd_PutPolygonsPickup's: an
		 * ambient of 8/8/8, one sun of pcbright - and pcbright is 0x7F7F7F
		 * here, because S_DrawPickup passes `bright` 0 and
		 * DrawThreeDeeObject2D turns that into 0x7F7F7F - pointing along
		 * (0, -50, -100) over sqrt(12500). Scale 0.75 and not the ring's 2.0:
		 * the display is drawn with InventoryActive at 0. */
		HDObjLight pickupSun;

		pickupSun.x = 0.0f;
		pickupSun.y = (float)(-50.0 / sqrt(12500.0));
		pickupSun.z = (float)(-100.0 / sqrt(12500.0));
		pickupSun.kind = (float)HD_OBJLIGHT_SUN;
		pickupSun.r = 127.0f;
		pickupSun.g = 127.0f;
		pickupSun.b = 127.0f;
		pickupSun.invRange = 0.0f;

		HDModelLighting pickupLight;

		pickupLight.lights = &pickupSun;
		pickupLight.lightCount = 1;
		pickupLight.sunScale = 0.75f;
		pickupLight.specStrength = 0.0f;
		pickupLight.specPower = 1.0f;

		std::vector<float> pickupNodes((size_t)here->nodes * 3, 8.0f);

		HDFrameLighting pframe;
		float pwibble[32];

		memset(&pframe, 0, sizeof(pframe));

		for (int i = 0; i < 32; i++)
			pwibble[i] = 0.0f;

		pframe.centerX = (float)lcx;
		pframe.centerY = (float)lcy;
		pframe.distanceFogStart = 1.0e9f;
		pframe.wibbleTable = pwibble;

		std::vector<unsigned long> shots[2];
		StateShot before, after;
		int broken = -1;

		for (int pass = 0; pass < 2; pass++)
		{
			Shot ps;

			if (!ShotBegin(ps, lw, lh))
			{
				ShotEnd(ps);
				break;
			}

			const double pkx = pass ? (lx - lcx) / lp : 0.0;
			const double pky = pass ? (ly - lcy) / lp : 0.0;

			g_device->BeginScene();

			if (pass)
				StateTake(before);

			HDRenderFrameBegin((const D3DMATRIX*)lview, &lproj, &pframe);
			HDModelFrameBegin(&lproj, 0);
			HDModelOffCentre((float)pkx, (float)pky);
			HDModelDraw(slot, &lmat[0], here->nodes, &pickupNodes[0], nodeMask,
				&pickupLight);
			HDModelOffCentre(0.0f, 0.0f);
			HDRenderFrameEnd();

			if (pass)
			{
				StateTake(after);
				broken = StateDiff(before, after);
			}

			g_device->EndScene();
			ShotGrab(ps, shots[pass]);

			sprintf(path, "%s_pickup_%s_%s.png", out.c_str(),
				pass ? "corner" : "centre", HDModelName(slot));
			ShotWrite(ps, path);
			ShotEnd(ps);
		}

		printf("\n-- pickupcheck: the light ---------------------------------------\n");

		double meanCentre = 0.0, meanCorner = 0.0, diffPct = 0.0;
		long compared = 0, lost = 0, onlyA = 0, onlyB = 0, worstChannel = 0;
		/* How many pixels differ at all, and how many by more than a step or
		 * two - a worst value alone cannot tell one stray texel apart from a
		 * picture that is lit differently. */
		long moved = 0, noisy = 0;

		if (shots[0].size() && shots[1].size())
		{
			const long dx = (long)(lx - lcx);
			const long dy = (long)(ly - lcy);

			for (long y = 0; y < lh; y++)
				for (long x = 0; x < lw; x++)
				{
					const unsigned long b2 = shots[1][(size_t)y * lw + x];
					const bool bObj = (b2 & 0x00FFFFFFu) != 0x00FF00FFu;
					const long ax = x - dx, ay = y - dy;
					const bool inA = ax >= 0 && ax < lw && ay >= 0 && ay < lh;
					const unsigned long a2 = inA
						? shots[0][(size_t)ay * lw + ax] : 0xFFFF00FFu;
					const bool aObj = inA && (a2 & 0x00FFFFFFu) != 0x00FF00FFu;

					if (!bObj && !aObj)
						continue;

					if (bObj != aObj)
					{
						if (aObj) onlyA++; else onlyB++;

						if (!inA) lost++;
						continue;
					}

					compared++;
					meanCentre += (double)((a2 >> 16) & 255)
						+ (double)((a2 >> 8) & 255) + (double)(a2 & 255);
					meanCorner += (double)((b2 >> 16) & 255)
						+ (double)((b2 >> 8) & 255) + (double)(b2 & 255);

					long here = 0;

					for (int ch = 0; ch < 3; ch++)
					{
						const long d = (long)((a2 >> (ch * 8)) & 255)
							- (long)((b2 >> (ch * 8)) & 255);
						const long ad = d < 0 ? -d : d;

						if (ad > here)
							here = ad;
					}

					if (here > worstChannel)
						worstChannel = here;

					if (here)
						moved++;

					if (here > 2)
						noisy++;
				}

			if (compared)
			{
				meanCentre /= (double)compared * 3.0;
				meanCorner /= (double)compared * 3.0;
			}

			diffPct = meanCentre > 1e-9
				? 100.0 * fabs(meanCorner - meanCentre) / meanCentre : 0.0;

			printf("centre %.0f,%.0f against corner %.0f,%.0f - the whole shift "
				"is %ld,%ld px\n", lcx, lcy, lx, ly, dx, dy);
			printf("object pixels    %ld compared, %ld only in the centre "
				"picture, %ld only in the corner one (%ld of them off the "
				"target)\n", compared, onlyA, onlyB, lost);
			printf("mean brightness  centre %.4f, corner %.4f - %.4f %% apart\n",
				meanCentre, meanCorner, diffPct);
			printf("per pixel        %ld of %ld differ at all, %ld by more than "
				"2 of 255, worst single channel %ld/255\n",
				moved, compared, noisy, worstChannel);
		}
		else
			printf("the two pictures could not be read back\n");

		printf("\n-- pickupcheck: the state bracket -------------------------------\n");

		if (broken < 0)
			printf("the corner draw did not run\n");
		else
			printf("%d of %d watched states changed across the display's own "
				"bracket\n", broken,
				kWatchedStateCount + kWatchedSamplerCount + kWatchedStageCount + 8);

		/* The budgets, and the light's is not zero on purpose. The place is
		 * arithmetic and its budget is the same 1 px -ringcheck uses. The
		 * light is a RASTERISED picture: the two draws put the same triangles
		 * on the same pixel grid a whole number of pixels apart, but the
		 * projection reaches that offset through _31 = kx * _11 in single
		 * precision rather than through an integer move, so an edge texel can
		 * land a hundredth of a pixel further along and come back one step of
		 * 255 different. One step is the smallest difference a picture can
		 * have; what is being ruled out is a light that depends on WHERE the
		 * object stands, which the shear of the earlier projection had at 45 %. */
		const long edges = onlyA + onlyB;
		const bool pass = places && worstX <= 1.0 && worstY <= 1.0 &&
			compared > 0 && diffPct < 2.0 && noisy * 200 <= compared &&
			edges * 100 <= compared && broken == 0;

		printf("\nbudgets: place 1 px; light 2 %% of the mean, and at most "
			"0.5 %% of the object's pixels more than 2 of 255 apart; edge "
			"pixels 1 %% of the object; states 0\n");
		printf("%s\n", pass
			? "PASS - the display's projection puts the rectangle where the "
			  "classic off-centre projection puts it, at the same size; the "
			  "corner picture is the centre picture moved and not a different "
			  "one; and the bracket hands the classic path back the device it "
			  "was given"
			: "FAIL - see the three sections above");
	}

	printf("\npeak commit of the whole run: %.1f MiB\n", PeakCommitMiB());

	HDModelShutdown();
	HDRenderShutdown();
	DropDevice();
	return 0;
}
