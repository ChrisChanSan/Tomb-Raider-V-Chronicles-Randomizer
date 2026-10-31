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

/* A Zip call: Zip talks to Lara over her headset in the VCI levels. Its
 * item has the slot the data name EXPANDING_PLATFORM, whose control is
 * ControlZipController (game\twogun.cpp), and its OCB is the audio track of
 * the call. The call runs in three stages, kept in item_flags[0]:
 *
 *   0  it takes Lara's control;
 *   1  it waits until she stands (AS_STOP in ANIM_BREATH), puts her into
 *      the headset pose (ANIM_ZIP_IN, AS_INTO_ZIP) and starts the track;
 *   2  it waits for the track: heard on the first tick it plays
 *      (item_flags[1]), over on the first tick after that it does not;
 *      then her goal is AS_OUT_ZIP, her control comes back and the item
 *      is killed.
 *
 * Her three zip states run lara_as_null with no collision: in the pose she
 * does nothing of her own, and the pose (ANIM_ZIP) loops until its one
 * state change, goal AS_OUT_ZIP, which only the end of the call sets.
 *
 * A skip did not reach the call. It ends the flyby that carries the call
 * in a few ticks and leaves the track playing, so the call held her in the
 * pose until the track ran out; and if she was still coming to a stop when
 * the flyby ended, the call posed her wherever she next stood still. A
 * skip now ends the call with the scene.
 *
 * A call can hold her with no scene at all: one that comes without a
 * flyby, and one that outlives the flyby it came with - its track the
 * longer, or the flyby broken off by Look. No skipper runs there, so
 * nothing but the track's end let her go. Such a call now ends on the
 * skip as well, through the same end (ZipSkipStarts below).
 *
 * Pure, with no engine header (skip_latch.h is pure too), so that
 * Preparation\Tools\Diagnose\ModernTest.cpp includes this file itself. */

#include "skip_latch.h"

/* Lara's zip states, the values of game\lara_states.h (twogun.cpp asserts
 * that they agree). */
enum
{
	ZIP_AS_INTO = 130,	// AS_INTO_ZIP: her hand on its way up
	ZIP_AS_POSE = 131,	// AS_ZIP: her hand at her ear
	ZIP_AS_OUT = 132	// AS_OUT_ZIP: her hand coming down
};

// The stages, item_flags[0].
enum
{
	ZIP_STAGE_TAKE,		// takes Lara's control
	ZIP_STAGE_POSE,		// waits for her to stand, poses her, plays
	ZIP_STAGE_LISTEN	// waits for the track to end
};

// What a tick of a call does, and how a call ends.
enum
{
	ZIP_WAIT,			// nothing this tick
	ZIP_STARTED,		// stage 0: her control taken
	ZIP_POSING,			// stage 1: she stands; the pose and the track
	ZIP_HEARD,			// stage 2: the track plays
	ZIP_ENDED,			// stage 2: the track is over
	ZIP_ENDED_BY_SKIP,	// the scene that carries the call was skipped
	ZIP_ENDED_NO_TRACK	// stage 2: the track did not start
};

/* One tick of a call in `stage`. `heard`: its track played on an earlier
 * tick (item_flags[1]); `stands`: Lara is in AS_STOP with ANIM_BREATH;
 * `playing`: the call's track is the music and its stream runs
 * (S_CDTrackPlaying, specific\audio.cpp).
 *
 * The tick that poses her starts the track, and S_CDPlay has made it the
 * music before it returns, so the first tick of stage 2 knows. A track not
 * playing then - no music device, its file missing, another track started
 * over it - will not play, since nothing starts it again, and the call
 * ends there instead of holding her for good. A track that plays ends the
 * call on the first tick it no longer does: at its end the stream goes
 * back to the level's atmosphere by itself. A stage outside 0 to 2 does
 * nothing, as before. */
inline int ZipCallTick(int stage, int heard, int stands, int playing)
{
	if (stage == ZIP_STAGE_TAKE)
		return ZIP_STARTED;

	if (stage == ZIP_STAGE_POSE)
		return stands ? ZIP_POSING : ZIP_WAIT;

	if (stage != ZIP_STAGE_LISTEN)
		return ZIP_WAIT;

	if (!heard)
		return playing ? ZIP_HEARD : ZIP_ENDED_NO_TRACK;

	return playing ? ZIP_WAIT : ZIP_ENDED;
}

// What the end of a call does with Lara.
enum
{
	ZIP_POSE_KEEP,		// leaves her as she is
	ZIP_POSE_GOAL_OUT,	// goal AS_OUT_ZIP, whatever she is doing
	ZIP_POSE_OUT,		// in the pose: goal AS_OUT_ZIP, her hand comes down
	ZIP_POSE_STAND		// her hand still going up: straight to standing
};

/* The call's own end, the track over, keeps its line: goal AS_OUT_ZIP,
 * whatever she does. An end that a skip or a missing track cuts takes her
 * out of the pose she is in: from AS_ZIP through AS_OUT_ZIP, her hand
 * coming down as at the call's own end; from AS_INTO_ZIP, the pose never
 * reached, straight back to standing; in any other state - not posed yet,
 * or on her way out already - she is left alone. */
inline int ZipPoseEnd(int why, int state)
{
	if (why == ZIP_ENDED)
		return ZIP_POSE_GOAL_OUT;

	if (state == ZIP_AS_POSE)
		return ZIP_POSE_OUT;

	if (state == ZIP_AS_INTO)
		return ZIP_POSE_STAND;

	return ZIP_POSE_KEEP;
}

// What the end of a call does with the music.
enum
{
	ZIP_MUSIC_KEEP,			// leaves it as it is
	ZIP_MUSIC_ATMOSPHERE,	// back to the level's atmosphere
	ZIP_MUSIC_STOP			// silence: the level has no atmosphere
};

/* `own`: the music is still the call's track (XATrack names it) - a skip
 * cut it short, or its file is missing and nothing plays. The end then
 * puts the music where the stream puts it at a track's end (its EOF tail,
 * specific\audio.cpp): the level's atmosphere, looped, or silence where
 * the level has none. A call whose track is over or never started leaves
 * the music alone: what plays then is not the call's. */
inline int ZipEndMusic(int own, int atmosphere)
{
	if (!own)
		return ZIP_MUSIC_KEEP;

	return atmosphere ? ZIP_MUSIC_ATMOSPHERE : ZIP_MUSIC_STOP;
}

// The word of a step in the ZIPLOG line (game\twogun.cpp).
inline const char* ZipCallWord(int step)
{
	switch (step)
	{
	case ZIP_STARTED:
		return "started";

	case ZIP_POSING:
		return "posing";

	case ZIP_HEARD:
		return "heard";

	case ZIP_ENDED:
		return "ended";

	case ZIP_ENDED_BY_SKIP:
		return "ended by skip";

	case ZIP_ENDED_NO_TRACK:
		return "ended without track";
	}

	return "waiting";
}

/* The skip of a call without a scene. With the Cutscene skipper on, a call
 * that holds Lara while no flyby and no cutscene runs ends on Escape held
 * or on a fresh press of the pad's confirm button, as a skipped scene ends
 * it (ZipCallsEndBySkip, game\twogun.cpp, asked once a tick by
 * game\control.cpp). */

// How a skip without a scene fired, the ZIPSKIP word.
enum
{
	ZIP_SKIP_NONE,
	ZIP_SKIP_ESCAPE,	// Escape held
	ZIP_SKIP_PAD		// a fresh press of the pad's confirm button
};

/* Whether the skip without a scene ends the call this tick, and how. `on`:
 * the Cutscene skipper is on and this is not the title; `stage`: the
 * call's stage; `taken`: her control is taken (bDisableLaraControl);
 * `step`: what the call's own tick does now (ZipCallTick); `scene`: a
 * flyby or a cutscene runs; `escape`: Escape is held; `fresh`: the confirm
 * button went down on this reading (SkipConfirmEdge).
 *
 * The call holds her once it has posed her, and before that while her
 * control is taken and she comes to a stand; stage 0, the tick that takes
 * it, holds nothing yet. Under a scene the scene's skip ends the call at
 * the scene's end, so this one asks nothing there: one press never skips
 * twice. On the tick the call ends by itself - its track over, or none -
 * that end is the end. With both, Escape is the word. */
inline int ZipSkipStarts(int on, int stage, int taken, int step, int scene,
	int escape, int fresh)
{
	if (!on || scene)
		return ZIP_SKIP_NONE;

	if (stage != ZIP_STAGE_LISTEN && !(stage == ZIP_STAGE_POSE && taken))
		return ZIP_SKIP_NONE;

	if (step == ZIP_ENDED || step == ZIP_ENDED_NO_TRACK)
		return ZIP_SKIP_NONE;

	if (escape)
		return ZIP_SKIP_ESCAPE;

	return fresh ? ZIP_SKIP_PAD : ZIP_SKIP_NONE;
}

/* The latch the skip engages (skip_latch.h): the confirm button's whenever
 * its fresh press was there, Escape held beside it or not - play comes
 * back on the next tick, and the button is Action there. Escape takes
 * none: it is no play role, and the ring opens on a fresh press of it
 * only. 0: no latch. */
inline long ZipSkipLatchKind(int fresh)
{
	return fresh ? SKIP_LATCH_ZIP : 0;
}

// The word of a skip in the ZIPSKIP line (game\twogun.cpp).
inline const char* ZipSkipWord(int how)
{
	if (how == ZIP_SKIP_ESCAPE)
		return "escape";

	return how == ZIP_SKIP_PAD ? "pad" : "none";
}
