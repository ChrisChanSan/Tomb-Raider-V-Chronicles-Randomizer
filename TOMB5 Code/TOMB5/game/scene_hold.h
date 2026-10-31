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

/* A scene that holds Lara leaves her life and air as they were.
 *
 * Two kinds of scene take her out of the player's hands: a cutscene
 * (GLOBAL_playing_cutseq, game\deltapak.cpp) and a flyby whose camera
 * carries the "disable Lara control" flag (bUseSpotCam with
 * bDisableLaraControl, game\spotcam.cpp). The world does not stop for
 * either: the item loop runs, the enemies keep her as their target, and
 * every place that hurts her writes her life as always. Core held her in
 * the flyby alone - it remembered her life and air when the flyby began and
 * put them back once a tick, but after her control had seen the tick's
 * blows, so a blow that took the rest of her life killed her first. A
 * cutscene remembered nothing: the blows stood, and if they had taken her
 * life she died on the first tick after it.
 *
 * The rule: a scene the player cannot act in must not cost her what the
 * player could not defend. The enemies keep running, so the world does not
 * freeze; it is her life and air that are held, not they. When a hold
 * begins, her life and air are remembered; while it lasts they are put back
 * before the item loop, before her control and where the flyby put them
 * back; when it ends they are put back once more, before her control runs
 * again. What was remembered is what was: a life of 0 or less at the start
 * stays so - no scene brings her back - and once her own death has begun
 * (her death count runs) nothing is put back: a dead Lara stays dead.
 *
 * A restore cannot undo what a blow decides in the tick it lands: one that
 * takes her whole life, a place that sets her life to 0 or below, one that
 * forces her into a death or sets her on fire. Those are refused while a
 * scene holds her; the enemy keeps its own animation and misses. The
 * threshold is her maximum life, not today's numbers - Balance.txt can give
 * any attack any value. A smaller blow lands and is put back.
 *
 * No engine header here: ModernTest asserts on it. */

// Her maximum life: setup.cpp gives it at a level's start, LaraCheat and the
// medipacks cap at it. A blow of this much takes all of it.
#define SCENE_HOLD_WHOLE_LIFE	1000

// What holds her (SceneHoldKind).
#define SCENE_HOLD_NONE		0
#define SCENE_HOLD_CUTSEQ	1
#define SCENE_HOLD_FLYBY	2

// What one look at the scene found (SceneHoldSync).
#define SCENE_HOLD_KEPT		0	// nothing began or ended
#define SCENE_HOLD_BEGAN	1
#define SCENE_HOLD_ENDED	2

struct SCENE_HOLD
{
	int kind;		// what holds her now; SCENE_HOLD_NONE when free
	int last;		// what held her last, kept after the end
	short life;		// her life when the hold began
	short air;		// her air when the hold began
	long ticks;		// the ticks the hold lasted
	long restored;	// the times her life was put back
	long refused;	// the blows refused
};

/* What holds her: a cutscene playing, or a flyby that took her control. The
 * cutscene first - a flyby frozen under a cutscene holds her as well. */
inline int SceneHoldKind(long playingCutseq, long useSpotCam,
	long noLaraControl)
{
	if (playingCutseq)
		return SCENE_HOLD_CUTSEQ;

	return useSpotCam && noLaraControl ? SCENE_HOLD_FLYBY : SCENE_HOLD_NONE;
}

// The HOLDLOG word of a kind.
inline const char* SceneHoldName(int kind)
{
	if (kind == SCENE_HOLD_CUTSEQ)
		return "cutseq";

	return kind == SCENE_HOLD_FLYBY ? "flyby" : "none";
}

/* Puts back what the hold remembered, unless nothing holds her or her death
 * has begun (`dying`). Returns whether her life changed, and counts it. */
inline bool SceneHoldPut(SCENE_HOLD* h, short* life, short* air, bool dying)
{
	bool changed;

	if (h->kind == SCENE_HOLD_NONE || dying)
		return false;

	changed = *life != h->life;
	*life = h->life;
	*air = h->air;

	if (changed)
		h->restored++;

	return changed;
}

/* One look at what holds her now (`kind`, from SceneHoldKind): a hold
 * begins and remembers her life and air; a running one puts them back and,
 * on a tick's first look (`tick`), counts the tick; one whose scene is gone
 * puts them back a last time and ends. */
inline int SceneHoldSync(SCENE_HOLD* h, int kind, short* life, short* air,
	bool tick, bool dying)
{
	if (h->kind == SCENE_HOLD_NONE)
	{
		if (kind == SCENE_HOLD_NONE)
			return SCENE_HOLD_KEPT;

		h->kind = kind;
		h->last = kind;
		h->life = *life;
		h->air = *air;
		h->ticks = tick ? 1 : 0;
		h->restored = 0;
		h->refused = 0;
		return SCENE_HOLD_BEGAN;
	}

	SceneHoldPut(h, life, air, dying);
	h->last = h->kind;

	if (kind == SCENE_HOLD_NONE)
	{
		h->kind = SCENE_HOLD_NONE;
		return SCENE_HOLD_ENDED;
	}

	h->kind = kind;

	if (tick)
		h->ticks++;

	return SCENE_HOLD_KEPT;
}

/* Whether a blow is refused: a scene holds her and the blow takes her whole
 * life. A place that sets her life to 0 or below, forces her into a death
 * or sets her on fire asks with SCENE_HOLD_WHOLE_LIFE. */
inline bool SceneHoldRefusesBlow(int kind, long damage)
{
	return kind != SCENE_HOLD_NONE && damage >= SCENE_HOLD_WHOLE_LIFE;
}
