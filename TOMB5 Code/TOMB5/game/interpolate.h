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

/* Time interpolation: 60 frames a second drawn from a 30 Hz simulation.
 *
 * S_DumpScreen waits one 1/60 s tick per present and ControlPhase spends two
 * per logic step, so every second frame carries no logic tick. On that frame
 * this module writes the halfway point between the previous and the current
 * logic state into the live structures (items[], effects[], hairs[][],
 * camera, the position-only particles and, in a cutscene, the actors' node
 * tables through game\cutseq_interp.cpp), lets the unchanged renderer draw
 * from them, and puts the original bytes back afterwards:
 *
 *   InterpCapturePrev()  control.cpp, first statement of the tick body: the
 *                        state before the tick. Several ticks in one
 *                        ControlPhase overwrite each other, so the draw sees
 *                        the state before the final tick.
 *   InterpBeginFrame(a)  draw.cpp, first line of DrawPhaseGame.
 *   InterpEndFrame()     draw.cpp, after S_AnimateTextures.
 *
 * Particles with a velocity are walked back by that velocity instead
 * (InterpTrailLong); lightning is re-rolled each tick and stays at 30 Hz.
 * The logic RNG (rand_1) is restored after a tween frame, a blended frame
 * and every frame of fps_mode 2's display cadence, because the draw path
 * calls GetRandomControl (star twinkle, floor lasers, muzzle flash, lens
 * flare, speech head); without that the simulation would diverge between the
 * frame rates. GetHeight's globals (trigger_index, tiltxoff, tiltyoff,
 * OnObject, height_type), which the shadows, the footprints and the rain set
 * in the frame, go back with it. At fps_mode 0 nothing here writes a byte. */

// The state before the last logic tick. Called once per tick.
void InterpCapturePrev();

/* The blend factor of the frame about to be drawn: 1 for the live state, 0.5
 * for the frame between two logic ticks. After ControlPhase framecount is -1
 * when a tick ran and 0 when none did, so the cadence needs no clock of its
 * own. At fps_mode 2 (display-bound) the frames do not land on the ticks and
 * the factor comes from InterpTickFraction instead. */
float InterpAlphaNow();

/* How far past the last logic tick the clock stands, as a fraction of a logic
 * step (1/30 s), clamped to [0,1]: the blend factor of fps_mode 2. Built from
 * ControlPhase's accumulator and TIME_SubTick (specific\time.cpp); the
 * arithmetic is InterpRefreshAlpha in interpolate_math.h, where InterpTest can
 * check it without a game. Not called at fps_mode 0 or 1. */
float InterpTickFraction();

void InterpBeginFrame(float alpha);
void InterpEndFrame();

/* Drops the previous state wherever game time jumped rather than flowed: a
 * level start, a level being freed, a savegame being read, and the return
 * from every menu that stops the world. The next frame draws the live state;
 * the tick after it resumes the interpolation. `why` is one short word for
 * the log and is never null. */
void InterpInvalidate(const char* why);

/* The blend factor of the frame being drawn now, 1 while nothing is
 * interpolated. For the log lines of other modules (HDLOG) only. */
float InterpFrameAlpha();

/* The room that contains the interpolated eye, used to seed the draw's
 * portal walk.
 *
 * On a tween frame the eye is blended but camera.pos.room_number is the logic
 * camera's room. Once the blended eye has crossed a portal plane, GetRoomBounds
 * (draw.cpp) skips that portal (the sign test on p->normal) or SetRoomBounds
 * drops it (tooNear == 4), and the room behind it vanishes for one frame,
 * leaving the sky in a hole with the portal's straight edge. The chase camera
 * crosses portal planes between ticks often; a first person eye rarely does.
 *
 * On a blended camera frame this returns GetFloor(x, y, z, &room) walked from
 * the logic room, the same call camera.cpp uses each tick. Nothing is written
 * back. On every other frame it returns `logicRoom` unchanged.
 *
 * On a blended frame whose eye that room does not hold, the drawn-eye rule
 * (ModernCameraDrawnEye, game\modern_camera.h) has already chosen the eye
 * and its room in InterpBeginFrame, and this returns that room. */
short InterpEyeRoom(short logicRoom);

/* The camera's room at the previous logic tick, or -1 while there is no
 * previous state. For the ROOMFLASH log line (draw.cpp) only. */
short InterpPrevCamRoom();

/* True on a blended frame whose drawn eye no room holds and no point of the
 * chord between the two tick eyes could replace (the rule's NOSKY):
 * SkyDrawPhase then draws no sky, so what the short walk leaves undrawn is
 * the clear colour, black, as it is indoors. False on every other frame and
 * always at fps_mode 0. */
bool InterpSkyWithheld();

/* The zoom's field of view on the frame being drawn.
 *
 * BinocularCamera (game\camera.cpp) sets the field of view once a tick from
 * BinocularRange, so at 165 Hz five or six frames would share each zoom step
 * and then jump. On a blended frame whose two ticks both zoom
 * (ModernCameraZoomTweens, game\modern_camera.h) InterpBeginFrame sets the
 * field of view between the one the last tick left and the one before it,
 * at the frame's alpha, and SkyDrawPhase (game\draw.cpp) sets its two
 * through InterpZoomFOV: GAME_FOV less `factor` times the range, the range
 * blended between the two ticks at the same alpha. On every other frame,
 * and always at fps_mode 0, InterpZoomFOV is AlterFOV with the tick's own
 * value, the classic call of SkyDrawPhase. InterpEndFrame puts back the
 * field of view the classic code would have left, so the next tick finds
 * phd_persp and the floats beside it as at 30 Hz. Everything AlterFOV
 * feeds follows the blended value the way it follows the tick's: phd_persp,
 * f_persp, f_mpersp and their inverses, which the classic and the HD
 * paths project with.
 *
 * InterpZoomSetting is true while an AlterFOV call sets a blended value;
 * ALTERFOV (specific\3dmath.cpp) prints it. */
enum { INTERP_ZOOM_SKY = 1, INTERP_ZOOM_VIEW = 7 };

void InterpZoomFOV(long factor);
bool InterpZoomSetting();

/* CAMLOG's draw line for a frame the drawn-eye rule acted on (game\draw.cpp,
 * DrawRooms, after the walk and the sky). Writes nothing without -log or on a
 * frame the rule left alone. */
void InterpLogEyeFix(unsigned int frame, short walkRoom, long rooms, long outsideLogic, long sky);

/* True when the frame being drawn carries no logic tick. Every counter that
 * steps once per drawn frame and measures time (fades, weather, flashes,
 * corpse fades) steps only when this is false, so it lasts as long in
 * wall-clock seconds at 60 as at 30.
 *
 * It is not "alpha < 1": a camera cut or a frame after an invalidation is
 * drawn live and still carries no tick, so the latch follows the cadence, not
 * the snapshot. It stands from the top of the frame through its present,
 * and a logic tick lowers it at its first statement (InterpCapturePrev): a
 * tick is never a tween frame, so the poison ramp the tick's own camera
 * steps (phd_GenerateW2V) steps once per tick at every rate. S_DumpScreen
 * clears it on every 30 Hz present (InterpNoteClassicPresent), so a modal
 * loop entered after a tween frame does not inherit `true` and the load and
 * pause menus' wait for DoFade == 2 ends. Always false at fps_mode 0. */
bool InterpIsTweenFrame();

/* The latch of the frame drawn last, which a tick does not lower. For the
 * counters that sit before DrawPhaseGame in a loop and so read the frame
 * before them across the tick that ran since: gfLegendTime (DoLevel), and
 * the sky's lightning, the credits roll and cutseq_busy_timeout (DoTitle
 * walks all three before its DrawPhaseGame). Reading the lowered latch there
 * would step them on the tick and again on the frame that carries it. After
 * DrawPhaseGame and before the tick it equals InterpIsTweenFrame(). Always
 * false at fps_mode 0. */
bool InterpDrawnFrameWasTween();

/* The frame being drawn wrote the blended state into the live one (the
 * snapshot was applied): items, the camera, Lara's aim and the cutscene's
 * actors stand between two ticks until InterpEndFrame puts them back. Not
 * the tween latch: at fps_mode 2 the frame that carries the tick is drawn
 * blended too, and at 60 Hz there the other one as well, at an alpha of 0.5
 * or more (live only when the clock clamps it at 1). What the draw builds
 * from this state and the logic reads - Lara's joint set and her wet nodes
 * (game\draw.cpp, LaraJointsBuild) - is built again from the live state
 * when it is true. Always false at fps_mode 0. */
bool InterpFrameBlended();

/* RNGLOG, one line per level with -log: the frames, the frames that gave
 * the generator's state and GetHeight's globals back and the draws they
 * undid, the frames that kept them and the draws they left in the stream.
 * Called from LaraJointsLevelEnd (game\draw.cpp). */
void InterpRngLevelEnd();

/* A present that is not a 60 Hz game frame's own. Called from S_DumpScreen
 * and S_DumpScreenFrame; clears the tween latch. */
void InterpNoteClassicPresent();

/* Sets the tween latch for a modal loop that presents at the frame rate (the
 * inventory ring at fps_mode 1 and 2): the ring states once per frame, before
 * drawing, whether this frame carries its logic step, so the guarded counters
 * keep their 30 Hz pace inside it. Same assignment as the top of
 * InterpBeginFrame. Not reached at fps_mode 0, where the ring presents at two
 * ticks and goes through InterpNoteClassicPresent. */
void InterpNoteMenuFrame(bool tween);

/* The logic tick counter (g_tickSerial, stepped in InterpCapturePrev at every
 * frame rate). The title menu (gameflow.cpp) uses it to ask "has a tick run
 * since I last read the debounced input?": a menu walked once per present
 * while dbinput is written once per tick would read every other press twice. */
unsigned long InterpTickSerial();

/* The pose blend: the time factor for this item's limbs on this frame, and
 * the animation frame it wore at the previous tick. Returns 0 (draw the pose
 * live) when the frame is not a tween frame, the snapshot was not applied,
 * the item is not an items[] entry, or its slot has no previous state,
 * changed hands or snapped. The neighbour rule (same animation, frame stepped
 * by exactly one) is tested where the frames are: GetFramesAt (draw.cpp) and
 * InterpPoseAt (interpolate_math.h).
 *
 * The snap verdict is recorded in InterpBeginFrame before item->pos is
 * overwritten; measured here, the distance would read half its real value
 * and a teleport could pass. */
float InterpPoseTime(const ITEM_INFO* item, short* prevAnim, short* prevFrame);

/* INTERPLOG's pose counters: an item drawn with its pose blended in time, one
 * drawn with the current tick's pose because the neighbour rule refused the
 * blend, and one
 * whose blended pose went through the HD path. Counted only with -log. */
void InterpNotePose(const ITEM_INFO* item);
void InterpNotePoseSkip();
void InterpNotePoseHD();
/* A rate 1 pair whose two poses are too far apart for an element-wise matrix
 * blend. */
void InterpNotePoseFar();
/* A tween frame on which Lara's pose was blended by the backwards rule
 * (InterpPoseBack, the ledge hang's lowering); once per drawn frame. */
void InterpNotePoseBack();

/* The neighbour rule refuses the pose, never the root.
 *
 * A stored frame carries the root offset in frm[6..8]. A climb carries its
 * vertical travel there (animation 161 walks -375 to -879 over 59 frames) and
 * the wrap moves item->pos by TranslateItem(0, -512, 0), so at 30 Hz the drawn
 * root is -879 before the wrap and -887 after. On the tween frame item->pos is
 * blended half way while the neighbour rule refuses the pose (the frame
 * stepped back to the
 * start) and the draw uses the current frame's offset: -631, a drop of 248
 * units once per climb cycle (measured with RootProbe on JOBY5).
 *
 * So on a refused tween frame the root offset is still blended in time, from
 * the previous tick's offset to this one's; the pose stays the current
 * tick's. The blend starts at the previous offset only as far as this tick's
 * own position step pays for it on each axis (InterpRootNarrow,
 * interpolate_math.h): a crawl ending in a stand, where item->pos does not
 * move, keeps the new pose at its own height. InterpRootNow hands the draw a
 * result only where it differs from the frame's own offset: the climb wrap
 * (248), the entry into a climb down (111), the ladder grab animation 172
 * with TranslateItem(0, -190, 0) (86), and a few units at other animation
 * changes. INTERPLOG's root_fix counts those frames, not the refusals (about
 * twenty per drawn frame in an ordinary room).
 *
 * GetFramesNow (draw.cpp) returns 0 both for "on a stored pose" and for
 * "refused by the neighbour rule", and its callers branch on that value, so
 * the refusal travels in g_poseOk[] (cleared per slot every drawn frame)
 * with the previous offset in g_rootPrev[]. `prev` is the offset the
 * previous tick drew, or null where a draw keeps its own frame's offset
 * (Lara's hit reaction, the first person still body). `prev` is in the
 * previous tick's frame of reference; where turn180_effect fired on the
 * refused tick (the underwater roll) it is half a turn behind and drew the
 * root up to 140 units off. InterpRootNow turns it into this tick's frame
 * first (InterpRootFlip). `turned` says the previous tick drew its frame
 * turned by half a turn itself (InterpKeyFrameTurn, the frame an early
 * turn180_effect fires on), so `prev` is half a turn behind in the same way;
 * the two turns cancel where both apply. Set on every call, never
 * accumulated: the draw asks two or three times a frame. */
void InterpRootRefused(const ITEM_INFO* item, const long prev[3], bool turned = false);

/* The root offset this draw should translate by, or false with `out`
 * untouched: every frame that is not a refused tween frame for this item
 * (so every frame at fps_mode 0), and every refused frame whose blend equals
 * the offset the frame already carries. The caller then draws as before.
 *
 * `frm`, `frac` and `blend` are what GetFramesNow left, so the current offset
 * is computed exactly as the draw would: frm[0] alone when frac is zero
 * (frm[1] may then point past the level's last frame and is not read), the
 * key pair at `blend` otherwise. On true the caller translates by `out` on
 * both matrix stacks; InterpolateMatrix then gives back exactly `out` and the
 * pose stays this tick's. A frame of Lara's bridged across the animation
 * change after an early half turn (draw.cpp LaraBridgeFrames) is a refused
 * frame whose pair is the bridge in both slots; the bridge carries the
 * current tick's own offset, so the offset comes out as it does where the
 * refused frame draws that tick's frame. */
bool InterpRootNow(const ITEM_INFO* item, short* const frm[], long frac, float blend,
	long out[3]);

/* The weapon arms' own animation frame at the previous tick.
 *
 * With a weapon drawn, Lara's arms (and with the HK in state 0..4 also torso
 * and head) are driven by the weapon slot's animation through lara.right_arm /
 * lara.left_arm (delstuff.cpp), not by her body animation, so the body's key
 * frame blend does not reach them. Unblended, the weapon stands still for a
 * frame and then jumps once per tick while the world glides.
 *
 * The weapon animations declare mixed rates (ANDREA1: PISTOLS_ANIM 26 at rate
 * 1, two at 2, two at 4; HK_ANIM 13 / 2 / 15; REVOLVER_ANIM 20 / 1 / 9;
 * FLARE_ANIM one at rate 3), but the arm chain indexes by frame number,
 * `frame_base[size * (frame_number - frame_base) + 9]`, so it reads one stored
 * frame per tick whatever the rate. The tween frame therefore wants the two
 * stored frames at the time fraction t, not the body's key frame factor.
 *
 * The draw walks the arm chain twice from the shoulder the body blend placed,
 * once with the previous frame into a scratch of three matrices and once with
 * the current one, and blends the two element-wise. Returns false (arms drawn
 * unblended) at fps_mode 0, on a frame that carries the tick or is drawn live,
 * without a previous tick, or when the arm's animation or frame_base changed
 * or its frame number moved by more than one (a pistol arm counts down as
 * well as up, so -1, 0 and +1 are neighbours). */
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

/* INTERPLOG's `arms=` counters: a frame on which an arm was blended in time,
 * and one on which InterpArmPrev refused. Latched per drawn frame, because
 * CalcLaraMatrices runs two or three times in one. Counted only with -log.
 * Called after the weapon is known (delstuff.cpp, after `gun`): lara.right_arm
 * and lara.left_arm keep their last values while her hands are empty, so empty
 * hands, a flare and a torch are counted in neither column. */
void InterpNoteArm(bool blended);

/* ARMLOG: one line per drawn frame with a weapon in her hands - the logic
 * tick and its alpha, the path that built her matrices, the body's blend,
 * both arms' animation frames beside the previous tick's, whether the arm
 * blend ran, the nine aim angles, and the view-space positions of the hips
 * and of shoulder, elbow and hand of both arms from lara_matrices (the table
 * the HD body is skinned from). For arm shake too fast to see in a video.
 *
 * CalcLaraMatrices notes its path (flag 0, the view set),
 * Rich_CalcLaraMatrices_Interpolated notes the arms, and DrawLara's caller
 * writes the line. A frame built by Rich_CalcLaraMatrices_Normal shows
 * ok=-1/-1. -log only; nothing here writes into the game. */
void InterpArmLogPath(long frac, float blend, bool still, bool hd);
void InterpArmLogNote(int gun, bool okR, bool okL, float alphaR, float alphaL,
	short prevR, short prevL);
void InterpArmLogFrame();

/* How much of each bone the pose blend leaves, for INTERPLOG.
 *
 * The element-wise matrix blend is affine in its factor, so a tween frame
 * inside one animation is the exact midpoint of its two ticks (ModelProbe
 * -tween60 over LARA animations 0..473 of ANDREA1, the 337 stored ones, all
 * fifteen nodes: worst offset 0.00) and no node can land outside the two
 * poses. It can shorten a bone: it walks the chord, not the arc, so a bone
 * whose world frame turns D comes out cos(D/2) of its length. Worst over the
 * table 0.34 on animation 205 (a pair the far-pose rule refuses); rolls and
 * the sprint dive lose at most a ninth.
 *
 * On every tween frame each of Lara's bones is measured against its skeleton
 * length and the worst of the second is kept with its animation and frame; a
 * rigid pose reads 1.00. `matrices` is lara_matrices (twelve floats per node,
 * world position in M03/M13/M23, LMX_ order - each node is read through
 * lara_lmx_of_node), `bone` the object's bone table. -log only. */
void InterpNoteLaraStretch(const float* matrices, const long* bone, int nodes,
	int animNumber, int frameNumber);

/* KEYFARLOG, one line per tick on which Lara's pose stands on a far key pair
 * (InterpKeyFarOf; InterpKeyFarDrawn, interpolate_math.h) and is drawn in
 * place of vanilla's key frame blend: from the pair's first key frame
 * (`hold`, mode=hold) or from a key frame built between the two (mode=lerp).
 * View set only (flag 0), and once per tick however many frames are drawn in
 * it. `frame0` and `frame1` are the pair GetFramesNow left; `nodes` and
 * `bone` are Lara's mesh count and bone table, for the node that turns
 * furthest in the drawn frame and its turn (node=, turn=). -log only. */
void InterpNoteKeyFar(const ITEM_INFO* item, const short* frame0, const short* frame1, bool hold,
	int nodes, const long* bone);

/* KEYSEAMLOG, one line per tick on which Lara's pose stands on a seam in the
 * data (InterpKeySeamAt, interpolate_math.h): a key pair far in the drawn
 * frame that the table leaves unmarked (InterpKeyFarOf), because a limb
 * turns more than 170 degrees both on its own and in the drawn frame, and
 * that vanilla's blend draws. View set only (flag 0), once per tick.
 * `pair` is the key pair `frame0` and `frame1` hold (draw.cpp LaraKeyOf);
 * `nodes` and `bone` are Lara's mesh count and bone table. Called on every
 * blended frame of a pair that is not far; without -log it returns at once.
 * -log only. */
void InterpNoteKeySeam(const ITEM_INFO* item, long pair, const short* frame0, const short* frame1,
	int nodes, const long* bone);

/* TURNLOG, one line per tick on which Lara's pose is drawn from key frames
 * turned by half a turn (InterpKeyFrameTurn): the frame turn180_effect fires
 * on belongs to the yaw before the effect (INTERP_TURN_EARLY). View set only,
 * once per tick. `pair` is the first key of the pair the frame lies on.
 * -log only. */
void InterpNoteTurn(const ITEM_INFO* item, long flip, long pair);

/* A tween frame on which Lara's pose is bridged across the animation change
 * after an early half turn (draw.cpp LaraBridgeFrames), from `from`, the pose
 * the flip tick drew, to `to`, the current tick's; `longHips` says the hips
 * take the longer way round (InterpBridgeLong). Counted once per drawn frame
 * for INTERPLOG's `bridge=`, and BRIDGELOG writes one line per bridged tick,
 * on its first bridge frame. -log only. */
void InterpNoteBridge(const ITEM_INFO* item, short prevAnim, short prevFrame, const short* from,
	const short* to, bool longHips);

/* Where turn180_effect fires in animation `animNumber` and what kind of
 * frame it fires on (InterpTurnKind, interpolate_math.h): returns the kind,
 * with the frame in `flip` and the first key of the pair around it in `pair`
 * (both -1 when the animation fires none).
 *
 * The animation's commands are read once and the answer kept in a small
 * table indexed by the animation number, so a draw pays a lookup;
 * InterpInvalidate empties the table, and FreeLevel invalidates, so no
 * answer outlives the level whose animation table it was read from. The
 * first turn180_effect command counts; no animation of Lara's carries two. */
int InterpTurnOf(short animNumber, long* flip, long* pair);

/* Whether key pair `pair` (key `pair` to key `pair` + 1) of Lara's animation
 * `animNumber` is far: the drawn rotation of some node turns more than 90
 * degrees between the two keys (InterpKeyFarDrawn, interpolate_math.h), and
 * the pair is no seam in the data (InterpKeySeamAt), which is left to
 * vanilla's blend. `nodes` and `bone` are Lara's mesh count and bone table.
 *
 * The animation's pairs are decided together on its first call
 * (InterpKeyFarFill) and kept as one bit per pair in a table indexed by the
 * animation number, like InterpTurnOf's, so a draw pays a lookup;
 * InterpInvalidate empties it with the level. The table holds Lara's
 * animations alone, whose bone table is the one passed. False for a pair
 * outside the animation or past INTERP_KEY_FAR_PAIRS. */
bool InterpKeyFarOf(short animNumber, long pair, int nodes, const long* bone);

/* The blend factor for velocity-carrying particles (sparks, rain, drips...):
 * the frame's alpha, or 1 on a live frame (fps_mode 0, the frame that
 * carries the tick, a camera cut, a level start, a return from a menu), where
 * every call site keeps vanilla's own expression. It is `g_applied ? g_alpha
 * : 1`, tied to the snapshot rather than the cadence, so a frame that draws
 * the items live draws the particles live too. The particle is walked
 * backwards from where the tick left it (InterpTrailLong,
 * interpolate_math.h), so it shows the same instant as the bodies around it. */
float InterpParticleAlpha();

/* INTERPLOG's `particles=` and `particle_kinds=`: each drawing routine
 * reports once, after its loop, how many particles it really moved. A kind
 * missing from the bitmask in a scene that shows it marks a draw site whose
 * guard never fires. Counted only with -log. */
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

/* TICKLOG: what the tween-guarded counters stepped this second. A 30 and a
 * 60 fps run must print the same line. Counted only with -log. */
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
