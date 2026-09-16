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
#include "../global/types.h"

/* TIME INTERPOLATION (#148, order 2) - THE ROOT AND THE CAMERA.
 *
 * Order 1 split the clock: S_DumpScreen waits one 1/60 s tick instead of two
 * for DrawPhaseGame's own present, and ControlPhase's accumulator - `for
 * (framecount += nframes; framecount > 0; framecount -= 2)` - spends two of
 * them per logic step, so the simulation keeps its thirty ticks a second
 * while sixty frames are presented. Every second frame was therefore a
 * DUPLICATE of the one before it. This file is what fills those frames in.
 *
 * WHAT IT DOES, in one sentence: it remembers where everything stood before
 * the last logic tick, and on the in-between frame it writes the HALFWAY
 * point into the live structures, lets the whole renderer draw from them
 * exactly as it always has, and then puts the original bytes back.
 *
 * THREE CALL SITES, AND THAT IS THE WHOLE SEAM:
 *
 *   InterpCapturePrev()   game\control.cpp, the first statement inside the
 *                         tick body, before GlobalCounter++. It is the state
 *                         BEFORE the tick that is about to run. When one
 *                         ControlPhase runs several ticks (a hitch, a long
 *                         load) each one overwrites the last, so what stands
 *                         at draw time is the state before the FINAL tick -
 *                         which is the only neighbour the drawn state has.
 *
 *   InterpBeginFrame(a)   game\draw.cpp, the first line of DrawPhaseGame.
 *                         Writes the interpolated values into items[],
 *                         effects[], hairs[][] and camera, and keeps a copy
 *                         of every value it overwrote.
 *
 *   InterpEndFrame()      game\draw.cpp, after S_AnimateTextures. Puts the
 *                         copies back, byte for byte.
 *
 * Nothing downstream knows. DrawRooms, InsertRoom, phd_PutPolygons, the
 * bucket sorter, the shadows, the lighting, HDFrameRoom, HDObjectNode and
 * HDLaraDraw all read the same globals they read today and get the
 * interpolated ones - which is why the HD renderer follows this order with
 * not one line changed in specific\hd\ (see the plan, 0.3: the HD seam takes
 * its matrices off the engine's own stack).
 *
 * WHAT IS INTERPOLATED HERE, AND WHAT IS NOT. This order moves the ROOT of a
 * thing - where it stands and which way it faces - and the CAMERA. The POSE
 * (which animation frame each limb wears) still steps at thirty; that is
 * order 4, and it has its own machinery waiting for it in draw.cpp's second
 * matrix stack.
 *
 * ORDER 5 THEN TOOK THE PARTICLES, in the two ways they divide into. The ones
 * that carry a VELOCITY - sparks, fire and smoke sparks, rain, snow, the
 * underwater dust, blood, drips, debris and the shockwave's radius - need no
 * snapshot at all, because the velocity IS the step the last tick took and
 * the previous position is a subtraction (InterpTrailLong,
 * game\interpolate_math.h; InterpParticleAlpha below). The ones that carry
 * only a POSITION - gunshells, rats, bats, spiders, bubbles and the rope's
 * mesh segments - joined this file's snapshot, where they cost 16.7 KB and not
 * one line of their own drawing code. Only the lightning is still drawn at
 * thirty, and it is drawn at thirty on purpose: its segments are re-rolled
 * from scratch on every tick, so there is nothing between two of them to
 * interpolate.
 *
 * ALPHA COSTS NO CLOCK. After ControlPhase returns, framecount is -1 when a
 * tick ran and 0 when none did, so the two states of the sixty-hertz cadence
 * tell themselves apart with one comparison and cannot drift. See
 * InterpAlphaNow().
 *
 * fps_mode 0 TAKES EVERY BRANCH OUT. InterpAlphaNow returns 1, and at 1 both
 * InterpBeginFrame and InterpEndFrame return before they write anything at
 * all - no snapshot is applied, no phd_LookAt is issued, not one byte of the
 * game's state is touched. That is proof 6 of the plan (K11): the thirty
 * hertz picture stays byte-identical to the build before this order.
 *
 * ORDER 3 ADDED TWO THINGS TO THIS FILE, and both are about the frame that
 * carries no logic tick rather than about the blend:
 *
 *   InterpIsTweenFrame()   the one criterion every per-frame counter in the
 *                          build now asks before it steps. Its own note is
 *                          below.
 *
 *   THE LOGIC RNG IS ROLLED BACK over a tween frame. The plan's safety
 *   argument (0.4) said the draw and the logic have separate generators -
 *   GetRandomDraw on rand_2, GetRandomControl on rand_1 - so extra frames
 *   cannot desynchronise the game. That is not true of this engine: the
 *   DRAW path calls GetRandomControl in at least five places (the star
 *   twinkle, four thousand draws a frame; the floor lasers' water pulse;
 *   the enemy muzzle flash; the steel door's lens flare; the cutscene
 *   speech head). At sixty each of them would consume the LOGIC stream
 *   twice per tick and the simulation would drift apart between the two
 *   frame rates. So InterpBeginFrame remembers rand_1 on a tween frame and
 *   InterpEndFrame puts it back, which makes the consumption per logic tick
 *   exactly what it is at thirty - one frame's worth - whatever any drawing
 *   code does. Proof 5 of the plan, a byte-identical savegame from the same
 *   inputs at both rates, rests on this. */

// The state before the last logic tick. Called once per tick.
void InterpCapturePrev();

/* What the frame about to be drawn should be drawn at: 1 for the live state,
 * 0.5 for the frame that falls between two logic ticks. Read at the top of
 * DrawPhaseGame and handed straight to InterpBeginFrame - it stands as its
 * own function so that the rule is one line and can be read where the caller
 * is.
 *
 * AT fps_mode 2 IT IS NOT A HALF (#148, order 6). Bound to the display's
 * refresh the frames do not land on the ticks, so the factor comes from the
 * clock instead - InterpTickFraction below - and walks the whole interval
 * from 0 to 1 across every logic step. */
float InterpAlphaNow();

/* HOW FAR PAST THE LAST LOGIC TICK THE CLOCK STANDS, as a fraction of a logic
 * step (1/30 s), clamped to [0,1] (#148, order 6).
 *
 * The display-bound frame rate's blend factor, and the only reading in the
 * engine that is finer than a 1/60 s tick. It is built from ControlPhase's
 * accumulator and the remainder in the engine's own clock (TIME_SubTick,
 * specific\time.cpp) and needs no accumulator of its own; the derivation is
 * written out over InterpRefreshAlpha in game\interpolate_math.h, where the
 * arithmetic lives so that InterpTest can check it without a game.
 *
 * Nothing calls it at fps_mode 0 or 1. */
float InterpTickFraction();

void InterpBeginFrame(float alpha);
void InterpEndFrame();

/* THE PREVIOUS STATE IS NOT A NEIGHBOUR OF THIS ONE. Called wherever game
 * time jumped rather than flowed: a level start, a level being freed, a
 * savegame being read, and the return from every menu that stops the world
 * (the inventory, the load/save screen, the pause menu). The next frame after
 * one of these draws the live state, and the tick after it starts the
 * interpolation again.
 *
 * `why` is one short word and reaches the log; it is never null. */
void InterpInvalidate(const char* why);

/* What the frame BEING DRAWN right now is drawn at - 1 while nothing is
 * interpolated. For the log lines of other modules (HDLOG); it is not part of
 * the mechanism. */
float InterpFrameAlpha();

/* THE ROOM THE INTERPOLATED EYE REALLY STANDS IN (#123, fix round T2, T2-1).
 *
 * WHAT THE PLAY TEST SAW. ANDREA1, the alley off the Piazza dell'Opera, sixty
 * frames a second, third person: for ONE frame the whole lower room is gone
 * and the painted city panorama stands where the pavement and the foot of the
 * brick wall should be, with the hole's edges dead straight along the
 * vertical portal line. At thirty it never happens, and in first person at
 * sixty it never happens either.
 *
 * WHY. On an in-between frame the EYE is blended (InterpBeginFrame, the
 * camera block) and the view matrix is rebuilt from it - but the portal walk
 * was started from `camera.pos.room_number`, which is the LOGIC camera's
 * room and steps only once per tick. Two lines of GetRoomBounds
 * (game\draw.cpp) then throw the neighbour away:
 *
 *   `if (x && (x ^ p->normal[0]) < 0) SetRoomBounds(...) else continue;`
 *       - the portal is only followed while the eye is on THIS room's side of
 *         its plane. Half a step past it, the sign flips and the portal, the
 *         room behind it and everything beyond are skipped for that frame.
 *   `if (tooNear == 4) return;` (SetRoomBounds)
 *       - and if the eye is past the plane by more than the portal is wide,
 *         all four corners come out at z <= 0 in view space and the portal is
 *         dropped there as well.
 *
 * What is left is the near room drawn alone, with the sky behind the hole
 * where the far room was - which is exactly the photograph, and exactly why
 * the hole's edge is the portal's own straight line.
 *
 * BOTH OF THE PLAYER'S CONTROLS FOLLOW FROM THAT. At fps_mode 0 no frame is
 * ever blended, so the eye and the room can never disagree. In first person
 * the eye rides on Lara's head inside her own room and hardly ever crosses a
 * portal plane between two ticks; the chase camera swings around corners in
 * whole sectors and crosses them all the time.
 *
 * WHAT THIS ANSWERS. On a frame whose camera really was blended, the room
 * that CONTAINS the interpolated eye - `GetFloor(x, y, z, &room)` walked from
 * the logic room, which is the engine's own answer to that question and the
 * very call camera.cpp makes at every tick (game\camera.cpp:1424, :2103,
 * :2146, :2296, :2654). Nothing is written back: camera.pos.room_number keeps
 * the logic room, and only the DRAW's portal walk is seeded from this one.
 *
 * On every other frame - fps_mode 0, the frame that carries the tick, a
 * cutscene, a camera cut, a level start - it returns `logicRoom` unchanged
 * and not one byte of the picture moves. */
short InterpEyeRoom(short logicRoom);

/* Which room the camera stood in at the PREVIOUS logic tick, or -1 while
 * there is no previous state. For ROOMFLASH's line (game\draw.cpp) and for
 * nothing else - it is a reading, not a part of the mechanism. */
short InterpPrevCamRoom();

/* THE ONE CRITERION OF ORDER 3 (#148).
 *
 * True when the frame being drawn carries NO logic tick - the in-between
 * frame of the sixty hertz cadence. Every counter in the build that steps
 * once per DRAWN frame and is meant to measure TIME asks this and steps only
 * when it is false, so that a fade, a raindrop, a flash and a corpse take
 * exactly as long in wall-clock seconds at sixty as they do at thirty.
 * "Everything must feel like vanilla, only more frames" is the whole rule of
 * this order, and this is the whole of its mechanism.
 *
 * WHAT IT IS NOT: it is not "alpha < 1". A cutscene frame is drawn live and
 * still carries no tick; a frame whose previous state was invalidated is
 * drawn live and still carries no tick. Both must stop the counters, or a
 * fade would run at double speed through every cutscene. The latch is
 * therefore set from the cadence itself and not from whether the snapshot
 * was applied.
 *
 * ITS LIFETIME IS THE WHOLE GAME-LOOP ITERATION, not just the draw. Two of
 * the counters (gfLegendTime in DoLevel, cutseq_busy_timeout in
 * handle_cutseq_triggering) sit in the main loop on either side of
 * DrawPhaseGame, so the latch is set at the top of DrawPhaseGame and STAYS
 * SET until the next frame sets it again. The alternation is what matters
 * and it holds on both sides of the call.
 *
 * AND IT IS FALSE IN EVERY MODAL LOOP. The inventory, the load/save screen,
 * the death menu and the load screen's fade loop present at two ticks and
 * step their own counters once per present, exactly as they did at thirty -
 * so S_DumpScreen calls InterpNoteClassicPresent() whenever the present it
 * just made was a thirty hertz one, which puts the latch down. Without that,
 * a modal loop entered right after a tween frame would inherit a `true` that
 * nothing inside it could ever clear, and RenderLoadPic's `while (DoFade !=
 * 2)` would never end.
 *
 * At fps_mode 0 it is false always and every guard in the build is inert. */
bool InterpIsTweenFrame();

/* A present that is not a sixty hertz game frame's own. Called from
 * S_DumpScreen and S_DumpScreenFrame - see the note above. */
void InterpNoteClassicPresent();

/* A MODAL LOOP THAT PRESENTS AT THE FRAME RATE (#154).
 *
 * Round 154 took the inventory ring off its two ticks a present, so its
 * frames no longer come through InterpNoteClassicPresent above and nothing
 * else would ever raise the latch for them: every counter order 3 guards
 * would run at the present rate again, inside the one loop the guards were
 * written to protect.
 *
 * So the ring says for itself which of its frames carries its logic step,
 * once per frame and before it draws anything - the same door DrawPhaseGame
 * uses, which is the assignment to g_tween at the top of InterpBeginFrame,
 * given a name so that a second caller cannot drift from the first.
 *
 * It is called only at fps_mode 1 and 2. At fps_mode 0 the ring keeps its two
 * ticks, comes through InterpNoteClassicPresent exactly as it always did, and
 * this is not reached. */
void InterpNoteMenuFrame(bool tween);

/* THE LOGIC TICK COUNTER, AS A READING (#148, order 6; #154).
 *
 * g_tickSerial steps once per logic tick, in InterpCapturePrev, at every
 * frame rate - the note over it in game\interpolate.cpp says why the display-
 * bound mode needs it. It is exported for the title menu (game\gameflow.cpp),
 * which asks a question of the same shape and must not answer it with a clock
 * of its own: "has a tick run since the last time I read the debounced
 * input?" A menu walked once per present while dbinput is written once per
 * tick reads every other press twice, which is what order 1b found. */
unsigned long InterpTickSerial();

/* ORDER 4 - THE POSE (#148).
 *
 * Order 2 moves where a thing STANDS between two logic ticks. This is the
 * half that moves its LIMBS, and the whole of what this file contributes to
 * it is one question asked from the draw:
 *
 *   InterpPoseTime(item, &prevAnim, &prevFrame)
 *
 * "How far between the two ticks is this frame, and what animation frame did
 * this item wear before the last one?" It returns 0 - do not blend the pose -
 * whenever there is any doubt at all:
 *
 *   this frame is not a tween frame, or the snapshot was not applied (a
 *   cutscene, a level start, a return from the inventory, fps_mode 0);
 *   the item is not an items[] entry;
 *   the slot has no previous state, changed hands, or SNAPPED - the root is
 *   being drawn live, so the pose is drawn live with it.
 *
 * and otherwise the time alpha, a half. The animation NUMBER changing and the
 * frame number not stepping by exactly one are rule K6 and are tested where
 * the frames are - GetFramesAt (game\draw.cpp) and InterpPoseAt
 * (game\interpolate_math.h).
 *
 * WHY THE SNAP TEST IS TAKEN FROM THE SNAPSHOT AND NOT RE-RUN HERE: by the
 * time the draw asks, InterpBeginFrame has already written the blended
 * position into item->pos, so the distance between the previous state and
 * "now" reads half of what it really was and a teleport could pass the test.
 * The verdict is therefore recorded in InterpBeginFrame's own walk, before a
 * byte is overwritten. */
float InterpPoseTime(const ITEM_INFO* item, short* prevAnim, short* prevFrame);

/* The three counters INTERPLOG's pose fields are made of: one item drawn with
 * its pose blended in time, one drawn with the pose of the current tick
 * because K6 refused the blend, and one whose blended pose went through the
 * HD path's interpolated branch. Counted only with -log. */
void InterpNotePose(const ITEM_INFO* item);
void InterpNotePoseSkip();
void InterpNotePoseHD();
/* And the fourth (#123, fix round S, S2): a rate 1 pair whose two poses are
 * too far apart for an element-wise matrix blend to survive. */
void InterpNotePoseFar();

/* ORDER 4'S ROOT - K6 REFUSES THE POSE, NEVER THE ROOT (#157).
 *
 * WHAT THE PLAY TEST PHOTOGRAPHED. JOBY5, a ladder, sixty frames a second,
 * three consecutive captures: the same pose family from the same camera, and
 * the MIDDLE frame has Lara a quarter of a sector lower than the two either
 * side of it. Measured off the level file (Preparation\Tools\Diagnose,
 * RootProbe): 248 units, once per climb cycle, and the same thing with the
 * sign reversed climbing down.
 *
 * WHY. A stored frame carries the ROOT OFFSET in frm[6..8] and the draw
 * applies it after the item's own rotation. A climb carries its whole
 * vertical travel in those offsets - animation 161 walks from -375 to -879
 * over its 59 frames - and the engine puts item->pos where the offsets had
 * taken her at the wrap, with the animation command TranslateItem(0, -512, 0).
 * At thirty hertz the DRAWN root, pos plus the offset, is therefore the same
 * number on both sides of the wrap: -879 before it, -887 after.
 *
 * Sixty hertz breaks that agreement on one frame, and it takes both halves of
 * the interpolation to do it. Order 2 has blended item->pos half way across
 * the wrap - 512 units is not a teleport and must not snap - while rule K6
 * refuses the POSE, because the frame stepped back to the animation's own
 * start and there is no neighbouring pose to blend with, so the draw falls
 * back to the CURRENT frame, whose offset is the first frame's. Half of
 * pos's step is there and none of the offset's: -631 where both neighbours
 * drew -879 and -887.
 *
 * THE RULE IS THE TITLE OF THIS NOTE. On a tween frame whose pose K6 refused,
 * the draw still blends the ROOT OFFSET in TIME - between what the previous
 * tick drew and what this one draws - and the two halves then add up to the
 * whole step. The pose stays the current tick's exactly as before, which is
 * the one frame of thirty hertz K6 was always allowed to keep; with the root
 * blended it is finally the invisible one its own comment promises.
 *
 * WHERE IT FIRES AND WHERE IT CANNOT, and this is TESTED and not assumed:
 * InterpRootNow blends, and then hands the draw the result only where it is
 * not the offset the frame already carries. So the rule reaches only the
 * frames whose picture was already wrong - the climb wrap (248 units), the
 * entry into a climb down (111), the ladder grab's one frame animation 172
 * with its TranslateItem(0, -190, 0) (86), and a handful of units wherever an
 * animation change moves the root. Every other refused frame - and refusals
 * are the common case, because an item that is standing still has not
 * stepped its frame either - is handed straight back to the line that has
 * always stood at the draw site.
 *
 * THAT IS ALSO WHAT root_fix COUNTS in the INTERPLOG second: frames this rule
 * moved, not frames it was asked about. The two differ by three orders of
 * magnitude - the user's own log carries twenty-odd refusals per drawn frame
 * in an ordinary room - and it is the first number his play test will read.
 *
 * HOW THE TWO SIDES TALK. GetFramesNow (game\draw.cpp) returns 0 both for
 * "this frame sits on a stored pose" and for "K6 refused", and it must keep
 * doing so: the call sites branch on that value and a third one would send a
 * refused frame down the interpolated path with a pair it has no factor for.
 * So the refusal is a SIDE CHANNEL, and the channel is g_poseOk[] - the array
 * InterpBeginFrame already clears for every slot at the top of every drawn
 * frame. It carries a third value now, "refused, and the previous tick's
 * offset is in g_rootPrev[]", which makes the latch per item and per frame
 * with no counter, no serial and no new per-frame work. A slot that snapped,
 * a frame that carries the tick and every frame at fps_mode 0 all read 0
 * there and never reach a line of this.
 *
 * `prev` is the offset the PREVIOUS tick drew - anims[prevAnim] at prevFrame,
 * by that tick's own key pair and factor - or null for "nothing was refused
 * for this item", which is what the two draws that must keep their own
 * frame's offset say: Lara's hit reaction, which wears a frame picked by hand
 * out of a spaz animation, and the first person still body of fix round U. */
void InterpRootRefused(const ITEM_INFO* item, const long prev[3]);

/* THE OFFSET THIS DRAW SHOULD TRANSLATE BY, or false and `out` untouched -
 * which is every frame that is not a refused tween frame for this item, and
 * therefore every frame at fps_mode 0, and also every refused frame whose
 * blend comes out at the offset that frame already carries. The caller then
 * draws the line it has always drawn, and nothing of this round is in the
 * picture.
 *
 * `frm` is the pair GetFramesNow left, `frac` what it returned and `blend`
 * the factor it set, so that the current tick's own offset is worked out
 * exactly as the draw would have drawn it: frm[0] alone when frac is zero -
 * and frm[1] is not read then, because at frac zero it may point past the
 * last frame of the level - and the key pair at `blend` otherwise.
 *
 * On true the caller translates by `out` on BOTH matrix stacks. The two
 * stacks carry the same translation and differ only in the node rotations
 * hung off it, so InterpolateMatrix gives back exactly `out` and the pose is
 * the one this tick wears, untouched. */
bool InterpRootNow(const ITEM_INFO* item, short* const frm[], long frac, float blend,
	long out[3]);

/* ORDER 4'S OTHER HALF - THE ARM'S OWN ANIMATION FRAME (#148; #123, fix round U)
 *
 * WHAT THIS ORDER LEFT OUT, AND WHAT IT COST. Lara's body pose is blended by
 * the key frame pair (GetFramesAt), but four of her fifteen nodes do not come
 * out of her own animation at all: with a weapon in her hands the ARMS are
 * driven by the weapon slot's animation, read through lara.right_arm /
 * lara.left_arm, and with the HK in state 0..4 the TORSO and the HEAD are
 * read from it as well (game\delstuff.cpp). Order 4 wrote those frames RAW -
 * the note that stood over InterpLaraAim said so in as many words - because
 * the arm chain leaves the second matrix stack behind at the shoulder and its
 * frames would have to be blended by the body's factor, which is not theirs.
 *
 * At thirty that is invisible: everything steps once per tick together. At
 * sixty it is the whole of what the play test of 2026-09-07 photographed -
 * the world and the camera glide, and the weapon in the hands, which in first
 * person is most of the picture, stands still and then jumps once per logic
 * tick. Measured off the user's own video: the background moved 1.87 and 1.88
 * units per frame in turn, and the weapon area moved 0.7 to 1.2 five frames
 * running and then 3.2 to 3.6 on the sixth.
 *
 * THE FACTOR IS THE FIX, AND THE ARM CHAIN IS ALREADY RATE 1 WHATEVER THE
 * TABLE SAYS. Measured (AnimMap over ANDREA1, the six weapon slots): the
 * weapon animations are NOT all rate 1 - PISTOLS_ANIM has 26 at rate 1, two
 * at 2 and two at 4, HK_ANIM has 13, two and FIFTEEN, REVOLVER_ANIM 20, one
 * and nine, and FLARE_ANIM even carries one at rate 3. It makes no difference
 * here, and the reason is Core's own: the arm chain indexes the frame data by
 * the FRAME number and not by the key,
 *
 *     lara.right_arm.frame_base[size * (frame_number - frame_base) + 9]
 *
 * so it reads a stored frame per game frame however the rate is declared. The
 * pose the last tick drew and the pose this tick draws are therefore two
 * whole stored frames, and what the tween frame wants is the two of them at
 * the time fraction t. That is not the body's key frame factor - which is why
 * this is a second question with its own answer.
 *
 * HOW THE DRAW USES IT (game\delstuff.cpp). The arm chain hangs off the
 * shoulder the BODY's blend has already placed, and from there it is drawn
 * from one matrix stack. So it is walked TWICE from that same shoulder - once
 * with the previous tick's arm frame into a scratch of three matrices, once
 * with this tick's into the output - and the two are blended element by
 * element with the factor below, exactly as InterpolateMatrix blends the
 * body's. Two walks of three nodes, on the tween frame only.
 *
 * This is the question the draw asks, and it answers false - the arms are
 * drawn exactly as they are today - whenever there is any doubt: fps_mode 0,
 * a frame that carries the tick, a frame drawn live, no previous tick, the
 * arm's ANIMATION changed, its frame_base moved, or its frame number moved by
 * more than one (rule K6, the arm's own form of it: a pistol arm counts down
 * as well as up, so a step of -1, 0 and +1 are all neighbours and anything
 * else is not). */
enum
{
	INTERP_ARM_RIGHT = 0,
	INTERP_ARM_LEFT = 1
};

struct InterpArmPose
{
	short* frame_base;
	short anim_number;
	short frame_number;
};

bool InterpArmPrev(int which, InterpArmPose* prev, float* alpha);

/* The two counters INTERPLOG's `arms=` field is made of - a frame on which an
 * arm really was blended in time, and one on which the rule above refused.
 * Latched per drawn frame, because CalcLaraMatrices runs two or three times
 * in one. Counted only with -log.
 *
 * ASKED WHERE THE WEAPON IS KNOWN and not where the rule is (game\delstuff.cpp,
 * after `gun`; corrected in U7). The rule reads lara.right_arm / lara.left_arm,
 * and those fields keep their last values while her hands are empty - nothing
 * clears them - so an unarmed Lara satisfies it on every frame while the draw
 * takes the WEAPON_NONE branch and blends no arm at all. Empty hands, a flare
 * and a torch are therefore not counted in either column. */
void InterpNoteArm(bool blended);

/* ARMLOG (#148, fix round Y): ONE LINE PER DRAWN FRAME WITH A WEAPON IN HER
 * HANDS. The user's play test sees the right arm shake with the shotgun and
 * the HK at sixty, too fast for any one video frame to show - so the arms
 * are written down as numbers, frame by frame: the logic tick the frame
 * belongs to and its alpha, which path built her matrices, the body's blend,
 * both arms' animation frames beside the previous tick's, whether the arm
 * blend of fix round U ran, the nine aim angles, and the view-space
 * positions of the hips and of shoulder, elbow and hand of BOTH arms out of
 * lara_matrices - the very table the HD body is skinned from.
 *
 * Three calls in the draw's own order: CalcLaraMatrices notes the path it
 * took (flag 0, the view set), Rich_CalcLaraMatrices_Interpolated notes what
 * it did with the arms, and DrawLara's caller writes the line once both are
 * known. A frame built by Rich_CalcLaraMatrices_Normal never reaches the
 * second call, which the line shows as ok=-1/-1. -log only, and nothing
 * here writes into the game. */
void InterpArmLogPath(long frac, float blend, bool still, bool hd);
void InterpArmLogNote(int gun, bool okR, bool okL, float alphaR, float alphaL,
	short prevR, short prevL);
void InterpArmLogFrame();

/* HOW MUCH OF A BONE THE POSE BLEND LEAVES (#123, fix round U, U2; #148).
 *
 * WHY THIS SEAM EXISTS. A play test at sixty photographed one frame of a dive
 * roll with a leg stretched fully the wrong way, curled up on the frames
 * either side of it, and faultless at thirty. Fix round S guessed a rate 1
 * key frame pair too far apart to blend (S2), and the counter it added -
 * pose_far - read 0.0 for the whole of the next session: the guess measured
 * zero, so it was not the answer.
 *
 * WHAT WAS MEASURED INSTEAD (ModelProbe -tween60 over the whole LARA animation
 * range 0..473 of ANDREA1 - the 337 of them this level really stores, the
 * other 137 being the level-specific stubs - every drawn frame, all fifteen
 * nodes): the element-wise blend is
 * AFFINE in its factor, so a tween frame inside one animation is the exact
 * midpoint of the two ticks either side of it - `worst off 0.00` over the
 * whole table. A node therefore CANNOT come out beyond the two poses it
 * stands between. What the blend can do is SHORTEN a bone: it walks the chord
 * of the arc, not the arc, so a bone whose world frame turns D between the
 * two key frames comes out cos(D/2) of its own length. Over the whole table
 * the worst is 0.34 - on animation 205, which is the one pair S2 already
 * refuses - and the rolls and the sprint dive lose at most a ninth.
 *
 * SO THE PICTURE IS NOT IN THE PAIR, and this is the seam that will name it
 * in the GAME rather than in a table: on every tween frame it measures each
 * of Lara's bones against its own length in the skeleton and keeps the worst
 * of the second, with the animation and the frame it happened on. A rigid
 * pose reads 1.00. A frame the play test can point at reads whatever the
 * shrink really was, and the answer stops being a guess.
 *
 * `matrices` is lara_matrices (twelve floats per node, world position in
 * M03/M13/M23) and `bone` the object's own bone table. -log only. */
void InterpNoteLaraStretch(const float* matrices, const long* bone, int nodes,
	int animNumber, int frameNumber);

/* ORDER 5 - THE PARTICLES (#148).
 *
 * The one number a drawing routine needs in order to put a spark, a raindrop
 * or a drip where it stood BETWEEN the last two logic ticks: the same alpha
 * everything else on this frame is drawn at, or 1 when the frame is a live
 * one (fps_mode 0, the frame that carries the tick, a cutscene, a level
 * start, every return from a menu). At 1 every call site keeps vanilla's own
 * expression untouched, which is what makes proof 6 - a byte-identical 30 Hz
 * frame - still true after this order.
 *
 * It is `g_applied ? g_alpha : 1`, i.e. it is tied to what the SNAPSHOT did
 * rather than to the cadence, and that is deliberate: a frame that draws the
 * items live must draw the particles live too, or a cutscene would have the
 * sparks half a step behind the body throwing them.
 *
 * The arithmetic that goes with it is InterpTrailLong (game\interpolate_math.h),
 * where the reasoning about the time axis is written out: a particle is walked
 * BACKWARDS from where the tick left it by the step it just took, never
 * forwards, so that it shows the same instant as the bodies around it. */
float InterpParticleAlpha();

/* WHAT WAS INTERPOLATED THIS FRAME, BY KIND (INTERPLOG's `particles=` and
 * `particle_kinds=`). Each drawing routine counts what it really moved and
 * says so once, after its loop - so the line reads "how many particles the
 * player's eye saw move on the in-between frames", not "how many exist".
 *
 * The bitmask is what makes the line diagnostic rather than decorative: a
 * kind that is missing from it in a scene that plainly has that kind on
 * screen is a draw site whose guard never fires, and there is no other way
 * to see that from the picture - it simply looks like thirty hertz.
 *
 * Counted only with -log. */
enum InterpParticleKind
{
	INTERP_PART_SPARK = 0,		// spark[1024]        - S_DrawSparks
	INTERP_PART_FIRE,		// fire_spark[20]     - S_DrawFireSparks
	INTERP_PART_SMOKE,		// smoke_spark[32]    - S_DrawSmokeSparks
	INTERP_PART_BLOOD,		// blood[32]          - DrawBlood
	INTERP_PART_DRIP,		// Drips[32]          - DrawDrips
	INTERP_PART_DEBRIS,		// debris[256]        - DrawDebris
	INTERP_PART_RAIN,		// Rain[1024]         - DoRain
	INTERP_PART_SNOW,		// Snow[1024]         - DoSnow
	INTERP_PART_DUST,		// uwdust[256]        - DoUwEffect
	INTERP_PART_SHOCKWAVE,		// ShockWaves[16]     - DrawShockwaves
	INTERP_PART_SHELL,		// Gunshells[24]      - the snapshot
	INTERP_PART_SWARM,		// Rats/Bats/Spiders  - the snapshot
	INTERP_PART_BUBBLE,		// Bubbles[40]        - the snapshot
	INTERP_PART_ROPE,		// RopeList[8]        - the snapshot
	INTERP_PART_COUNT
};

void InterpNoteParticles(int kind, int n);

/* WHAT STEPPED THIS SECOND (TICKLOG). The counters of order 3 report
 * themselves so that "identical in wall-clock time" is a measured number
 * rather than a claim: the same TICKLOG line must come out of a thirty and a
 * sixty run. Counted only with -log. */
enum InterpStepKind
{
	INTERP_STEP_FADE = 0,		// Fade()          - the screen fade
	INTERP_STEP_SCREENFADE,		// DoScreenFade()  - death and level change
	INTERP_STEP_WEATHER,		// DoRain / DoSnow / DoUwEffect
	INTERP_STEP_LEGEND,		// gfLegendTime    - the level caption
	INTERP_STEP_AFTERDEATH,		// item->after_death - the corpse fade
	INTERP_STEP_COUNT
};

void InterpNoteStep(int kind);
