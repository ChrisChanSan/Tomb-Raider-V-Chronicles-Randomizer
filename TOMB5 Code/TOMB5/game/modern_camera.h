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

/* THE MODERN CAMERA - THE ORBIT, AND NOTHING BUT ARITHMETIC (#161, round
 * 161a2, step 1; the WORLD orbit, the speed slider's mapping and the wall's
 * new meaning, round 161a2b step 1).
 *
 * The modern camera is not a new camera. It is our own chase camera with its
 * angle taken from a state instead of from Lara alone:
 *
 *     yaw       = world_yaw - a direction in the WORLD, which her turning does
 *                 not move; the engine's slot gets world_yaw - lara.y_rot
 *     elevation = offset_pitch
 *     distance  = camera.target_distance, exactly as today
 *
 * so every wall, every room change, every damping step and every snap of
 * game\camera.cpp stays what it was. What this file owns is the two angles
 * the orbit remembers between ticks and the three rules that move them; the
 * hunks that read them are game\camera.cpp.
 *
 * THE MODEL IS THE USER'S, from his own recordings of 2026-09-17, and it is
 * quoted here in his words because every number below is answerable to one of
 * these four sentences:
 *
 *   1 "Es ist, als waere die Kamera eine Kugel um Lara herum, auf der ich sie
 *      mit dem rechten Stick vollstaendig um Lara herum bewegen kann, auf
 *      allen Achsen 360 Grad. Lara ist immer im Zentrum der Kugel."
 *   2 "Die Kamera richtet sich auch nicht selbstaendig wieder hinter Lara, sie
 *      behaelt immer ihre Position, an der ich sie auf der Kugel verschoben
 *      habe - ausser sie wird durch eine Wand beeinflusst."   (standing)
 *   3 "Die Kamera bewegt sich ganz leicht und super smooth wieder hinter Lara.
 *      [...] Das ist fast nicht zu merken. Ausser eine Wand kommt, dann geht
 *      sie mit dem Winkel der Wand schneller zum Ursprung hinter Lara."
 *                                                              (moving)
 *   4 "ist Lara zu nah, wird sie komplett ausgeblendet"         (at a wall)
 *
 * So: the stick moves the camera (1), a stick at rest keeps it (2), running
 * lets it fall back behind her so slowly that it is hard to notice (3), and a
 * wall that clips the eye makes that fall faster while it lasts (3 again).
 *
 * ROUND 161a2b CORRECTS WHAT "KEEPS IT" MEANS, and the correction is the
 * user's own. Asked whether the camera should stay where it is in the WORLD
 * when Lara turns, and come back behind her only slowly, he answered "Ja,
 * hoert sich gut an" - and his first description of the Remaster, which the
 * round 161a2 brief mis-translated, reads "Die Kamera richtet sich auch nicht
 * selbstaendig wieder hinter Lara" and "richtet sie sich ganz langsam nach.
 * Das ist fast nicht zu merken."
 *
 * Round 161a2 remembered an OFFSET from her heading, so every degree she
 * turned turned the view with her. The play test measured it: a turn of 62
 * degrees was followed by the eye inside 20 ticks and overshot to 80 (M5 of
 * the round's brief), and his spin video shows the whole view wheeling round
 * with her where the Remaster's stands still. The state is therefore a WORLD
 * yaw now, and the offset the engine wants is derived from it on every tick.
 * The return is unchanged, and it is the only thing that brings the camera
 * home.
 *
 * TROne's orbit holds a world angle too, and that is where the resemblance
 * stops: theirs never returns behind the player at all, ours falls home at
 * MODERN_CAMERA_RETURN whenever she is moving and the thumb is off the stick.
 * Nothing of theirs is copied here; the only thing this file has in common
 * with it is the idea that a camera angle is a state and not a derivation.
 *
 * IT INCLUDES NO ENGINE HEADER, for the same reason game\modern_move.h does
 * not: Preparation\Tools\Diagnose\ModernTest.cpp compiles THIS FILE - not a
 * copy of it - and asserts on it. A feel number that nobody can re-measure is
 * a number nobody can change.
 *
 * ANGLES are the engine's own 16-bit units (65536 is a full turn, 0 is +Z),
 * the STICK is our own +-128 (specific\dxshell.cpp scales XInput by 256) and
 * a TICK is one logic step, 1/30 s (game\control.cpp:261 counts the loop down
 * in halves of a 60th).
 *
 * WHAT IS NOT HERE. The seam is game\camera.cpp and always was - round 161a2's
 * steps 2 to 5 built it. Round 161a2b's step 2 changes how a wall is resolved
 * there (only the angle we asked for, clipped by mgLOS at any distance, so the
 * DISTANCE shrinks instead of the angle jumping), and hands this file her
 * heading; step 5 wires the speed slider below to the options page, to the
 * registry and to MODERNLOG. */

/* THE CAMERA CODE'S OWN NUMBERS, MEASURED BEFORE ANYTHING WAS WRITTEN, because
 * an orbit that invents limits the classic camera already has is an orbit that
 * fights it. Every row was read out of the tree, not assumed:
 *
 *   elevation clamp        +-15470 (+-84.98 deg)  ChaseCamera, camera.cpp:1012
 *   resting elevation      -1820 (-10 deg, above) ChaseCamera, camera.cpp:1006
 *   elevation damping      (target - actual) >> 3 UpdateCameraElevation, :857
 *   distance               1536                   camera.cpp:258 and :602
 *   wall resolution        the wanted angle, else the nearest of the four
 *                          world cardinals with line of sight; mgLOS push 200,
 *                          CameraCollisionBounds push 384
 *                                                 ChaseCamera, camera.cpp:1043
 *   eye damping            pos += (ideal - pos) / speed      MoveCamera
 *   look camera pitch      364 per tick (2 deg), head -6370..+5460
 *                                                 LookUpDown, lara.cpp
 *   near clip plane        20 world units         InitWindow(...,20,20480,...),
 *                                                 specific\gamemain.cpp:261
 *   Lara's own radius      100..102               lara.cpp:1470 and :1475
 *   camera cut thresholds  Manhattan > 1024 on the eye and on the view
 *                          direction              INTERP_CAM_SNAP,
 *                                                 game\interpolate_math.h:187
 *
 * THE LAST ROW IS A CONSTRAINT ON THIS FILE and it was checked, not hoped
 * (the round's rule: the 60 fps cut detection must not fire on the orbit's own
 * steps). At the DEFAULT speed the yaw moves 1092 units, 6 degrees, in one
 * tick; the ideal eye sits at 1536 * cos(10 deg) = 1512 units, so it travels
 * 2 * 1512 * sin(3 deg) = 158 units, at worst 224 measured the Manhattan way.
 * The pitch moves 3 degrees, which lifts the ideal by at most 1536 * sin(3
 * deg) = 80 units, and the engine's own >> 3 damping turns that into 10. The
 * two together are 304 against a threshold of 1024 - a margin of 3.4.
 *
 * ROUND 161a2b'S SPEED SLIDER DOUBLES THAT STEP at its top end, so the margin
 * stops being an argument and becomes a measurement: at speed 100 the yaw
 * moves 2184 units a tick, 12 degrees, and the pitch 6, and
 * Preparation\Tools\Diagnose\ModernTest.cpp sweeps the whole sphere at BOTH
 * ends of the slider (camera_camcut) against INTERP_CAM_SNAP itself.
 *
 * AND THAT MARGIN IS THE WHOLE OF IT. This paragraph used to add "and the real
 * eye moves less again, because MoveCamera only ever walks a fraction of the
 * way to the ideal", counting the damping as reserve on top. Step 2 measured
 * that and it is wrong: "pos += (ideal - pos) / speed" is a first order lag,
 * and a lag fed a constant VELOCITY settles at that same velocity. It is
 * smaller for the first few ticks of a push and equal for as long as the push
 * lasts, which is exactly the case this row is about. The sentence is struck
 * rather than softened (the round's rule for a refuted assumption), and step
 * 2's own swept test in ModernTest.cpp answers the same question the honest
 * way: 309 Manhattan over the whole sphere, against INTERP_CAM_SNAP's 1024.
 *
 * The one thing that CAN trip the detection at the wall is the classic
 * quadrant snap, which jumps the ideal by a whole 90 degrees; that is
 * vanilla's and it is there at Tank today. Round 161a2 absorbed it into the
 * offset and made it worse; round 161a2b's step 2 stops PRODUCING it in Modern
 * instead (item 3), and the orbit never added to it either way. */

/* EVERY CONSTANT OF THE ORBIT, WITH WHERE IT COMES FROM.
 *
 * Three origins appear below and they are not the same thing. "The camera
 * code's own" is a number read out of game\camera.cpp and reused so that the
 * orbit cannot disagree with the camera it steers. "The brief's" is a number
 * the user's model fixes directly. "Measure ourselves" is a feel number: it
 * carries the measurement plan and a MODERNLOG field (step 5), because the
 * play test is the only instrument that can read it.
 *
 * ModernCameraDefaults() below is the only thing that reads them. */
enum modern_camera_constants
{
	/* MEASURE, and the same answer as the left stick's MODERN_DEAD: nothing.
	 * The zone is applied before the axes reach us and it is the round's own
	 * now, not the hardware's: specific\input.cpp puts the RAW right stick
	 * through ModernStickDeadZone (game\modern_move.h, item 6) at
	 * MODERN_DEAD_RIGHT, or at MODERN_DEAD_RIGHT_DINPUT on the DirectInput
	 * path, where this user's stick rests off centre. A second zone on top of
	 * that would only eat travel.
	 * THIS ROW WAS WRONG UNTIL ROUND 161a2b AND THE CORRECTION IS THE POINT:
	 * it used to cite the hardware's LEFT stick zones (7849 of 32767, 150 of
	 * 1000) while the right stick's own DirectInput zone was 500 of 1000 -
	 * half the travel - which is what the play test then measured (M2).
	 * The plan if the user reports the camera drifting on its own: MODERNLOG
	 * prints rs=, and the number to raise is the radial zone in
	 * game\modern_move.h, not this one - this one is a second chance nobody
	 * needs while that one is honest. */
	MODERN_CAMERA_DEAD = 0,

	/* THE SPEED SLIDER'S TWO ENDS (round 161a2b, item 5), and the number the
	 * whole feel of the orbit hangs on. "Der rechte Stick muss noch schneller
	 * werden, der ist viel zu langsam. Am besten ein Slider wie die SFX und
	 * Musik." - so the rate is not one constant any more but a row on the
	 * Controls page, 0 to 100, and these four numbers are its ends, its top
	 * and its default.
	 *
	 * EACH END ANSWERS ONE DIRECTION OF THAT SENTENCE. 60 degrees a second is
	 * a third of what round 161a2 shipped, so somebody who finds the camera
	 * too quick has somewhere to go; 360 a second is twice it, a full turn in
	 * one second, and it is deliberately PAST what he asked for rather than
	 * short of it - his own Remaster video turns a full circle in 2.85 to
	 * 2.95 s, about 125 deg/s (M1), and a slider whose top is the reference is
	 * a slider with no room in it. In our units at 30 ticks a second, 60 deg/s
	 * is 364.09 units a tick and 360 deg/s is 2184.53, so 364 and 2184.
	 *
	 * THE MAPPING IS LINEAR BETWEEN THEM, which is what puts the default of 40
	 * at exactly 364 + 40 * 1820 / 100 = 1092: the rate round 161a2 shipped,
	 * so a player who never touches the row keeps the camera he already has.
	 * The pitch is half the yaw at every setting, as it always was - half a
	 * turn of elevation while the yaw goes round once, which is the whole
	 * travel the clamp below allows and a little over.
	 *
	 * Between rest and full deflection the STICK is proportional whatever the
	 * slider says: the axis IS the rate, which is how every camera stick since
	 * about 2005 has read, and it keeps the arithmetic one multiply the test
	 * can check. ModernCameraSpeedRates() is the only thing that reads these
	 * four, and it is checked at 0, 40 and 100. */
	MODERN_CAMERA_YAW_RATE_MIN = 364,	// speed 0:   60 deg/s (364.09 at 30 Hz)
	MODERN_CAMERA_YAW_RATE_MAX = 2184,	// speed 100: 360 deg/s (2184.53)
	MODERN_CAMERA_SPEED_FULL = 100,		// the slider's own top, like SFX and Music
	MODERN_CAMERA_SPEED_DEFAULT = 40,	// = 1092, round 161a2's own rate exactly

	/* THE CAMERA CODE'S OWN AT THE BOTTOM, THE USER'S OWN AT THE TOP.
	 *
	 * ChaseCamera clamps camera.actual_elevation to +-15470 on every tick
	 * (camera.cpp:1012-1015), which is 84.98 degrees - "as far as you like,
	 * but never over the pole". That is what the camera BELOW her still uses,
	 * and asking for the engine's own number means the orbit asks for nothing
	 * the camera would silently take back from it. 478 ticks of the play test
	 * were spent down there and showed nothing, so nothing is taken away.
	 *
	 * THE TOP CAP IS CUT (round 161a2b, item 4a), and his drawing cuts it:
	 * "Wir muessen unsere Kugel auch noch ein bisschen einschraenken. Wenn ich
	 * direkt ueber Lara bin ist es sehr buggy, sie aendert sehr schnell ihre
	 * Richtung" - with a sketch of the sphere whose top cap is crossed out.
	 * -10923 is 60 degrees above her, where the ideal eye's HORIZONTAL radius
	 * is still 1536 * cos(60) = 768, half a sector. Over that cap the radius
	 * collapses towards nothing, the eye ends up nearly on her own axis, and
	 * everything measured from the vector eye -> target loses its direction
	 * with it - her movement reference above all, which then read her own
	 * running direction and turned her 3277 units every tick for as long as
	 * the stick was off centre (M3 of the round's brief). The real cure is
	 * item 4b, which takes that reference from the orbit instead; this cap is
	 * the second half of it, and 768 is the same half sector game\camera.cpp
	 * had already picked for MODERN_CAM_ANGLE_RADIUS when it asked the same
	 * question about the same radius.
	 *
	 * WHAT THE CAP COSTS, named here rather than discovered later: the water's
	 * pointer is this elevation minus pitch_home (ModernCameraSwimPitch), so
	 * the steepest DIVE a modern swimmer can ask for falls from 13650 (75
	 * degrees) to 9103 (50). game\modern_move.h carries that arithmetic and
	 * now says so; if the play test finds diving shallow, the water is where
	 * that is answered and not the camera. */
	MODERN_CAMERA_PITCH_MIN = -10923,	// 60 deg above her: radius 1536*cos(60) = 768
	MODERN_CAMERA_PITCH_MAX = 15470,	// below her: ChaseCamera's own clamp, camera.cpp:1012

	/* THE CAMERA CODE'S OWN as well: where the chase camera sits when nobody
	 * has touched it. ChaseCamera opens with "if (!camera.target_elevation)
	 * camera.target_elevation = -1820" (camera.cpp:1006), ten degrees above
	 * her and looking down, and that is where TR5 has put the camera since
	 * 1999. The orbit's pitch IS that elevation - it is not an offset from it
	 * - so this is what a reset seats it at. Zero would not be "no opinion",
	 * it would be dead level, ten degrees below the game's own camera, and it
	 * would land exactly on the value that line treats as "unset". */
	MODERN_CAMERA_PITCH_HOME = -1820,

	/* MEASURE, the "fast nicht zu merken" of observation 3. The offset falls
	 * towards zero by this fraction of itself per tick, out of
	 * MODERN_CAMERA_RETURN_ONE - the engine's own idiom (UpdateCameraElevation
	 * does the same thing with a >> 3) but written as a fraction rather than a
	 * shift, because a shift can only halve and the number the brief asks for
	 * is between two shifts: a 90 degree offset is to halve in about four
	 * seconds of running, and 4 s is 120 ticks. 1 - 2^(-1/120) = 0.0057595,
	 * times 65536 is 377.46, so 377 - which gives a half life of 120.1 ticks,
	 * 4.005 s. A shift of 7 would have been 2.95 s and a shift of 8, 5.9 s.
	 * The plan: MODERNLOG prints cam_off and cam_ret every tick, so the play
	 * test reads the real curve and this number moves in halves and doubles
	 * (188 = 8 s, 754 = 2 s) until the user says it is right. */
	MODERN_CAMERA_RETURN = 377,
	MODERN_CAMERA_RETURN_ONE = 65536,

	/* A fraction of a number can be zero, and a decay that can step by zero
	 * never arrives: under 174 units of offset, 377/65536 of it floors to
	 * nothing and the camera would sit one degree off her back for ever.
	 * Four units a tick is 0.02 degrees, 0.66 degrees a second - well under
	 * anything an eye can follow - and it closes that last degree in 44
	 * ticks. It is the floor of the step, not a step of its own: above 174
	 * the fraction is what moves the camera. */
	MODERN_CAMERA_RETURN_MIN = 4,

	/* MEASURE, the second half of observation 3: "dann geht sie mit dem
	 * Winkel der Wand SCHNELLER zum Ursprung hinter Lara". A factor and not a
	 * second rate, so that "faster than the return" is structural - it cannot
	 * come apart when the return above is tuned, and the test can prove the
	 * relation rather than compare two numbers that happen to differ. Eight
	 * times 377 is a half life of 14.7 ticks, half a second: fast enough to
	 * read as the wall taking the camera, slow enough to still be a movement
	 * and not a cut.
	 *
	 * WHAT A WALL IS HAS CHANGED UNDER IT (round 161a2b, item 3) and the
	 * factor has not. `wall` used to mean "geometry turned the angle we asked
	 * for"; it now means "the eye was CLIPPED this tick" - the camera keeps
	 * the angle the player dialled and loses DISTANCE instead, which is what
	 * the user's own Remaster video shows. It is still the one signal that
	 * makes the return run fast, and it is still a wall that sets it. */
	MODERN_CAMERA_WALL_FACTOR = 8,

	/* MEASURE, observation 4: "ist Lara zu nah, wird sie komplett
	 * ausgeblendet" - and the brief's origin for it is "the distance at which
	 * the classic camera's own near clip cuts her". Both halves of that are
	 * in the tree: the near plane is 20 world units (InitWindow(0, 0, w, h,
	 * 20, 20480, 80, ...) in specific\gamemain.cpp:261 and winmain.cpp:277),
	 * and her own body is a cylinder of radius 102 (coll->radius in
	 * game\lara.cpp:1470, the standing and running value). 20 + 102 = 122 is
	 * therefore the exact distance from the eye to her centre at which the
	 * near plane stops being in front of her and starts being inside her.
	 * The plan: MODERNLOG prints hide= and the measured distance, and if the
	 * play test sees her sliced open before she vanishes this rises towards
	 * 384 - the camera collision's own push, the closest the camera is ever
	 * meant to sit to anything - in steps of 64. */
	MODERN_CAMERA_HIDE_DIST = 122,

	/* THE CAMERA CODE'S OWN, again, and a coin flip taken out of our hands.
	 * Which way a pushed stick moves the view is taste in every other engine,
	 * but ours has already answered it once: the classic look camera turns
	 * IN_FORWARD into "look up" (LookUpDown in game\lara.cpp - head_x_rot -=
	 * 364, and a negative x_rot is up). Looking up from an orbit means the
	 * camera drops BELOW her, which is a POSITIVE elevation here, and our
	 * stick reads forward-negative - so the default subtracts the y axis.
	 * The switch exists because half the world calls that inverted, and
	 * because flipping it must not need a rebuild of anything but this line.
	 */
	MODERN_CAMERA_INVERT_PITCH = 0,

	/* THE ONE SWITCH LEFT, and it is a switch for the same reason A1's five
	 * are: it turns off exactly one rule, which is how a play test bisects a
	 * complaint instead of describing it.
	 *   pitch_return  the pitch falls back to the resting elevation the way
	 *                 the yaw falls back behind her. It ships OFF: the user
	 *                 described the yaw returning and said nothing at all
	 *                 about the height, and observation 2 says what he did not
	 *                 ask for stays where he put it. The capability is real
	 *                 and testable with an explicit cfg; it is simply not what
	 *                 ships until he asks for it.
	 *
	 * MODERN_CAMERA_FOLLOW_WALL AND MODERN_CAMERA_WALL_DEAD ARE GONE, and they
	 * are this round's refuted assumption (rule 5: reverted, not left in).
	 * Round 161a2 took the displacement geometry forced on the camera INTO the
	 * offset, reasoning that the next tick would then ask for an angle nothing
	 * blocks. What it really wrote into the state was the classic QUADRANT
	 * SNAP: ChaseCamera answers a blocked angle with one of the four world
	 * cardinals, so the offset took that cardinal, the next tick asked for the
	 * cardinal plus one stick step, was blocked again, and the stick was
	 * swallowed. Measured in the play test (M3): in 290 of 855 ticks with the
	 * right stick fully pushed the angle did not advance or ran AGAINST the
	 * stick, 177 of them at -400 units or worse, in 14 episodes of 8 to 37
	 * ticks. "bei uns ist die Kamera in so einem Fall nicht zu bewegen." The
	 * cure is to stop producing the snap at all - only the angle we asked for,
	 * clipped by mgLOS at any distance, which is item 3 in game\camera.cpp -
	 * and not to absorb it here. */
	MODERN_CAMERA_PITCH_RETURN = 0,

	/* AN ANIMATION ABOUT-TURN TAKES THE CAMERA WITH IT (round 161a3, item 5,
	 * the user's observation 6). "Im Remaster dreht sich die Kamera automatisch
	 * mit Lara mit und richtet sich hinter ihr wieder aus. Das ist nicht immer
	 * die gleiche Richtung und auch nicht immer der gleiche Winkel, das ist
	 * mehr dynamisch als statisch. Bei uns bleibt die Kamera in jedem Fall
	 * immer da, wo sie war. Das wird immer dann ein Problem, wenn Lara nach
	 * einer Drehung springen will: dann ist der relative Winkel zu Lara
	 * invertiert und sie springt in eine falsche Richtung."
	 *
	 * IT IS ITEM 1's OWN RULE THAT PRODUCES IT, and that is why the answer is
	 * here and not a retreat from item 1: the world yaw does not move when she
	 * turns, which is exactly right while the STICK is steering her - and an
	 * ANIMATION that flips her 180 degrees in one tick is not steering at all.
	 * M6 counted 25 such flips in one session, every one with the modern turn
	 * innocent (`yawd=0`): the ground roll (state 45 -> 23), the jump twist
	 * (3 -> 3) and the underwater roll (66 -> 66). After each, cam_off went from
	 * about 0 to about 180 - the camera in front of her face - and the slow
	 * return took it back by under 30 degrees in 40 ticks.
	 *
	 * THE THRESHOLD IS 150 DEGREES IN ONE TICK. The three flips are all 180;
	 * the modern turn's own biggest step is 30 (MODERN_TURN_RATE) and the
	 * classic automaton's is 8, so anything over 150 in a single tick is an
	 * animation and nothing else. 65536 * 150 / 360 = 27306.67, so 27307.
	 *
	 * AND IT IS PAID OUT OVER 20 TICKS, 0.67 s, which is the middle of the
	 * 0.6 to 0.75 s his own Remaster videos take to sit behind her again
	 * (anim_rem_roll.png, anim_rem_saltos.png). It is a "measure ourselves"
	 * number: MODERNLOG's `carry=` prints what is still pending every tick, so
	 * the curve can be read back and this moved in fives.
	 *
	 * THE PROFILE IS EASED and not linear, for the reason the eye can see: a
	 * linear swing starts and stops at full speed, which reads as two cuts with
	 * a pan between them. 3u^2 - 2u^3 starts and ends at nothing, and its
	 * steepest tick is 1.5 times the average - 2456 units at the half turn,
	 * which at the orbit's own radius moves the eye by about 360 units, a third
	 * of INTERP_CAM_SNAP, so the interpolator never reads the swing as a cut.
	 * MODERN_CAMERA_CARRY_ONE is the fixed point the profile is computed in:
	 * 4096ths, small enough that total * profile stays inside a long at any
	 * carry_ticks a setting could hold, exact at both ends. */
	MODERN_CAMERA_CARRY_FLIP = 27307,	// 150 deg in one tick: an animation, never a turn
	MODERN_CAMERA_CARRY_TICKS = 20,		// 0.67 s at 30 Hz - the Remaster's own 0.6 to 0.75
	MODERN_CAMERA_CARRY_ONE = 4096,		// the eased profile's fixed point

	/* HOW MANY FRACTIONS OF THE RAY THE EYE IS PULLED IN ALONG (round 161a3,
	 * item 1, and the user's word 1: "Als waeren Stick, Kamera und Laras
	 * Position out of sync").
	 *
	 * Eight, because mgLOS already walks that same ray in eight
	 * (game\camera.cpp:1430, `(target->x - start->x) >> 3` and a loop of 8), so
	 * the clipped point it hands back always sits on an eighth and the pull-in
	 * that follows it measures in the same units instead of inventing a second
	 * grid. At the orbit's own radius of 1536 that is 192 units a step.
	 *
	 * THE ARITHMETIC IS HERE and the geometry is not: ModernCameraPullAxis
	 * below is the whole of the rule - one coordinate, one fraction - and
	 * game\camera.cpp's ModernCameraRayBounds asks the floor and the ceiling
	 * about the point it gets back. That split is what lets Preparation\Tools\
	 * Diagnose\ModernTest check the thing the yaw depends on, which is that the
	 * SAME fraction is applied to x and to z, so the point stays on the ray and
	 * the picture keeps showing the angle the movement is measured against. */
	MODERN_CAMERA_PULL_STEPS = 8,

	/* AND THE CLEARANCE THAT POINT IS ASKED FOR, which is the classic camera
	 * collision's own push and deliberately not a new number:
	 * CameraCollisionBounds is called with 384 from both orbit cameras
	 * (game\camera.cpp:1954, :2149) and 384 is what the engine has always meant
	 * by "the closest the eye may sit to a wall". Item 1 keeps the question and
	 * throws away the answer - the sector-grid snap - so the number that asks
	 * it stays exactly what it was. */
	MODERN_CAMERA_PULL_PUSH = 384,

	/* HOW FAR THE EYE MAY BE FROM HER AND STILL BE ORBITING HER AT ALL, the
	 * outer bound game\camera.cpp's ModernCameraLeadEye needs. Eight sectors is
	 * five times the orbit's own radius of 1536: past that the eye is not
	 * circling her, it is arriving from somewhere - a fixed camera just
	 * released, a cut, a room change - and turning it about her by a stick step
	 * would be turning the wrong thing. It also keeps the squares in that
	 * function inside a long with room to spare.
	 *
	 * IT IS NOT THE BOUND THE CUT DETECTION NEEDS: a rotation moves the eye by a
	 * chord that grows with the RADIUS, so an eye walking in from three or four
	 * sectors away would be turned further in one tick than INTERP_CAM_SNAP
	 * allows, in exactly the ticks the player is steering. That bound is
	 * camera.target_distance and it lives in the function, because it is the
	 * radius the ideal itself is built at in the same breath.
	 *
	 * IT LIVES IN THIS HEADER, and not beside the function it guards, for one
	 * reason the round's second review pass named: Preparation\Tools\Diagnose\
	 * ModernTest models ModernCameraLeadEye by hand - camera.cpp cannot be
	 * linked without a level - and while this number was a #define in that .cpp
	 * the model held a hand-copied 8192 that no compiler would ever compare
	 * against the tree. Here the test reads the tree's own number, so moving or
	 * deleting it breaks the build of the test rather than its silence. */
	MODERN_CAMERA_LEAD_MAX = 8192
};

/* The orbit's thresholds, in one place, filled by ModernCameraDefaults().
 * Every field is a long so that nothing in the update has to convert: the
 * angles are signed 16-bit values widened, the rates are fractions of
 * MODERN_CAMERA_RETURN_ONE and the flags are 0 or 1. */
struct MODERN_CAMERA_CFG
{
	long dead;			// right stick deflection under which nothing happens
	long stick_full;	// full deflection in our units (MODERN_STICK_FULL)
	long yaw_rate;		// units of yaw per tick at full deflection
	long pitch_rate;	// units of pitch per tick at full deflection
	long pitch_min;		// the elevation clamp, the classic camera's own
	long pitch_max;
	long pitch_home;	// the elevation a reset seats her camera at
	long invert_pitch;	// 0 = stick forward looks up, as the look camera does
	long return_rate;	// per MODERN_CAMERA_RETURN_ONE of what is left, per tick
	long return_min;	// the smallest step, so the return really arrives
	long pitch_return;	// 1 = the pitch falls back to pitch_home as well
	long wall_factor;	// how much faster the return runs while a wall clips the eye
	long hide_dist;		// eye-to-Lara distance under which she is not drawn
	long carry_flip;	// heading change in one tick that counts as an animation about-turn
	long carry_ticks;	// how many ticks the swing that follows it is paid out over; 0 = off
};

/* Where the camera stands and what the last tick did to it. game\camera.cpp
 * owns the one instance and forgets it at a level start, a savegame load and a
 * scheme change, because a camera angle seated in the last level is no angle
 * at all.
 *
 * THE STATE IS THE WORLD YAW AND THE OFFSET IS DERIVED (round 161a2b, item 1),
 * rather than the state being the offset and her heading being subtracted from
 * it as it was in round 161a2. Both spellings of the same rule exist and this
 * one was chosen because it cannot DRIFT: subtracting a per-tick heading delta
 * accumulates every rounding and loses any tick the orbit does not run (a
 * flyby, a cutscene, the look key, the sniper cameras), where a world yaw minus
 * this tick's heading is exact on every tick whatever happened before it. It
 * also prints straight to MODERNLOG's cam_world (step 5) without arithmetic.
 *
 * SEATING. A reset leaves world_yaw at 0 and `seated` at 0, because a pure
 * function has no Lara to ask; the first update ties the world yaw to the
 * heading it is handed, keeping whatever offset_yaw holds at that moment (0
 * after a reset - behind her back). That is what lets a caller, and a test,
 * seat an OFFSET before the first tick and get a world angle out of it. */
struct MODERN_CAMERA_STATE
{
	short world_yaw;	// where the eye stands IN THE WORLD - the state proper
	short offset_yaw;	// derived: world_yaw - her heading, what the engine's slot wants
	short offset_pitch;	// and how high: the camera elevation itself, clamped
	long seated;		// 0 = world_yaw is not tied to a heading yet
	long wall;			// 1 = the eye was CLIPPED this tick, so the return ran fast
	long ret;			// the return step this tick, signed - MODERNLOG cam_ret
	long steering;		// 1 = the stick was out, so the return stood still

	/* THE SWING AN ANIMATION ABOUT-TURN OWES (round 161a3, item 5). `carry` is
	 * what is still pending, signed, and is MODERNLOG's `carry=`; the other two
	 * are how the eased profile knows where it is. All three are cleared
	 * together, by the last tick of the swing or by the stick cancelling it. */
	long carry;			// units of world yaw still to be paid out
	long carry_total;	// the whole swing, so the profile is measured from its start
	long carry_tick;	// how many of carry_ticks have been paid
};

void ModernCameraDefaults(MODERN_CAMERA_CFG* cfg);
void ModernCameraCarry(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state, long flip);
long ModernCameraCarryStep(const MODERN_CAMERA_CFG* cfg, long total, long tick);
long ModernCameraPullAxis(long target, long eye, long step, long steps);
void ModernCameraSpeedRates(long speed, long* yaw_rate, long* pitch_rate);
void ModernCameraReset(MODERN_CAMERA_STATE* state);
void ModernCameraUpdate(const MODERN_CAMERA_CFG* cfg, MODERN_CAMERA_STATE* state,
	long rs_x, long rs_y, long lara_heading, long lara_moving, long wall_clipped,
	short* out_yaw_offset, short* out_pitch);
long ModernCameraHidesLara(const MODERN_CAMERA_CFG* cfg, long distance);
short ModernCameraSwimPitch(const MODERN_CAMERA_CFG* cfg, long pitch);
long ModernCameraTickFollows(unsigned short now, unsigned short stamp);
