/*
 * RootProbe - the ROOT OFFSET of an animation, and what the sixty hertz
 * in-between frame draws it as.
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
 * Why it exists. Every stored frame of a TR5 animation carries a bounding box
 * (six shorts) and then the ROOT OFFSET (three shorts, frm[6..8]) which the
 * draw applies AFTER the item's own rotation - game\draw.cpp,
 * DrawAnimatingItem, and game\delstuff.cpp for Lara. The vertical travel of a
 * ladder climb is carried INSIDE those offsets, and the engine moves
 * item->pos by the same amount at the moment the animation wraps (the
 * animation command TranslateItem, game\laramisc.cpp AnimateLara), so that at
 * thirty hertz the DRAWN root - pos plus the rotated offset - is continuous
 * across the wrap.
 *
 * At sixty hertz the two halves of the interpolation disagree on exactly that
 * one frame: order 2 has blended item->pos half way across the wrap while
 * the neighbour rule refuses the pose blend (the frame stepped back to the
 * animation's own start) and the draw falls back to the CURRENT frame, whose
 * offset is the first frame's rather than the last frame's. Half the step is
 * then missing from the picture for one frame.
 *
 * This tool measures exactly that gap and is the proof a fix for it is
 * checked against. It prints, for every animation named on the line:
 *
 *   - the animation table row: rate, frame size, the frame window, where it
 *     continues (anim and FRAME - the field AnimCmd does not read), velocity,
 *     and every animation command including type 1, TranslateItem, which is
 *     the one that moves the root at the wrap;
 *   - every KEY frame's root offset;
 *   - and the WRAP REPLAY: three logic ticks before and after the wrap, the
 *     drawn root at thirty hertz, and the in-between frames of a sixty hertz
 *     run both as the engine draws them today and as InterpRootKey /
 *     InterpRootLerp (game\interpolate_math.h - the engine's own header, not
 *     a copy) make them. The tween frame the rule really reaches is marked
 *     `<-- refused`, and it is only reached where the blend lands somewhere
 *     other than that frame's own offset - the test the engine makes in
 *     InterpRootNow and the thing root_fix counts;
 *   - and for that frame, the -log line the engine will print, in all three
 *     axes. Computing this by hand from the key table beside it is
 *     error-prone: the previous tick usually sits BETWEEN two keys, so its
 *     offset is a blend of the pair and not either key. The tool does that
 *     arithmetic here so the printed line can be compared directly against
 *     the engine's own log.
 *
 * The NARROWED rule appears as a third row beside the other two - the blend
 * that keeps only as much of the offset step as the tick's own POSITION step
 * pays for (InterpRootNarrow, same header) - and the -sweep mode walks every
 * wrap and every state change of Lara's in one level and says of each one
 * whether the file pays for its offset step in full, in part or not at all.
 *
 * Usage:
 *   RootProbe <level.trc> [anim ...]        default: 96 161 164 165 168 169 173
 *   RootProbe <level.trc> -grab <a> <b> [y] the drawn root across a change
 *                                           from animation a to animation b;
 *                                           the position step defaults to a's
 *                                           own TranslateItem where the change
 *                                           is its wrap, and `y` overrides it
 *   RootProbe <level.trc> -sweep [least]    every transition of Lara's whose
 *                                           offset step is `least` units or
 *                                           more on an axis (default 8)
 *
 * Build (64 bit): broot.bat next to this file. See Preparation/Tools/README.md.
 */
#include "core/Tr5Level.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"

static std::wstring toW(const char* s)
{
	std::wstring w;

	while (*s)
		w += (wchar_t)(unsigned char)*s++;

	return w;
}

/* The 40 byte animation entry of a TR4/TR5 level file, which is ANIM_STRUCT
 * (TOMB5\global\types.h) with the frame POINTER still a byte offset. */
struct AnimRow
{
	uint32_t frameOffset = 0;
	int rate = 1, size = 0;
	int state = 0;
	long velocity = 0, acceleration = 0;
	int frameStart = 0, frameEnd = 0;
	int nextAnim = 0, nextFrame = 0;
	int numChanges = 0, changeIndex = 0;
	int numCommands = 0, commandIndex = 0;
};

static bool readAnim(const Tr5Level& l, int index, AnimRow& z)
{
	size_t at = 0;
	int32_t n = 0;

	if (!l.debugAnimBlock(at, n) || index < 0 || index >= n)
		return false;

	const std::vector<uint8_t>& b = l.debugBytes();

	if (at + (size_t)(index + 1) * 40 > b.size())
		return false;

	const uint8_t* e = b.data() + at + (size_t)index * 40;
	int16_t s = 0;
	uint16_t u = 0;
	int32_t v = 0;

	std::memcpy(&z.frameOffset, e + 0, 4);
	z.rate = e[4];
	z.size = e[5];
	std::memcpy(&u, e + 6, 2);	z.state = u;
	std::memcpy(&v, e + 8, 4);	z.velocity = v;
	std::memcpy(&v, e + 12, 4);	z.acceleration = v;
	std::memcpy(&s, e + 24, 2);	z.frameStart = s;
	std::memcpy(&s, e + 26, 2);	z.frameEnd = s;
	std::memcpy(&u, e + 28, 2);	z.nextAnim = u;
	std::memcpy(&u, e + 30, 2);	z.nextFrame = u;
	std::memcpy(&u, e + 32, 2);	z.numChanges = u;
	std::memcpy(&u, e + 34, 2);	z.changeIndex = u;
	std::memcpy(&u, e + 36, 2);	z.numCommands = u;
	std::memcpy(&u, e + 38, 2);	z.commandIndex = u;

	return true;
}

/* The head of one key frame - six shorts of bounding box and then the three
 * of the root offset - at exactly the address GetFrames computes:
 * frame_ptr + size * key. The engine's own InterpRootKey indexes [6..8] of
 * this, so it is handed the frame and not the offset. */
static void keyFrame(const Tr5Level& l, const AnimRow& a, int key, short out[9])
{
	const long long at = (long long)(a.frameOffset / 2) + (long long)key * a.size;

	for (int i = 0; i < 9; i++)
		out[i] = l.debugReadFrameShort(at + i);
}

/* The offset one game frame is drawn with, by the engine's own arithmetic:
 * GetFrames picks the key pair and shortens the rate on the last interval,
 * InterpKeyAlpha turns (frac, rate) into vanilla's factor, and InterpRootKey
 * blends the pair with it. game\draw.cpp GetRootAt is this function with the
 * level file replaced by anims[]. */
static bool frameRoot(const Tr5Level& l, const AnimRow& a, int frameNumber, long out[3])
{
	if (a.rate < 1 || a.size < 9)
		return false;

	const long f = (long)frameNumber - (long)a.frameStart;

	if (f < 0)
		return false;

	const long key = f / a.rate;
	const long frac = f % a.rate;
	const long rate = InterpKeyRate(a.rate, key, a.frameEnd);
	short frm0[9], frm1[9];

	keyFrame(l, a, (int)key, frm0);

	if (frac)
		keyFrame(l, a, (int)key + 1, frm1);
	else
		std::memcpy(frm1, frm0, sizeof(frm0));

	/* InterpKeyAlpha over the shortened rate is exactly the `blend`
	 * GetFramesNow hands the draw, so the engine and this tool put the same
	 * number into the same function. */
	InterpRootKey(frm0, frm1, frac, InterpKeyAlpha(frac, rate), out);

	return true;
}

static void printCommands(const Tr5Level& l, const AnimRow& a, long trans[3], bool& haveTrans)
{
	size_t cmdAt = 0;
	int32_t nCmd = 0;

	haveTrans = false;
	trans[0] = trans[1] = trans[2] = 0;

	if (!a.numCommands || !l.debugCommandBlock(cmdAt, nCmd))
		return;

	const std::vector<uint8_t>& b = l.debugBytes();
	size_t p = cmdAt + (size_t)a.commandIndex * 2;

	auto r16 = [&](size_t at) -> int16_t
	{
		int16_t v = 0;
		std::memcpy(&v, b.data() + at, 2);
		return v;
	};

	for (int k = 0; k < a.numCommands; k++)
	{
		if (p + 2 > cmdAt + (size_t)nCmd * 2)
		{
			std::printf("    commands run out of the block\n");
			return;
		}

		const int typ = (int)(uint16_t)r16(p);
		p += 2;

		switch (typ)
		{
		case 1:
			std::printf("    cmd 1 TranslateItem  x %d  y %d  z %d   [at the wrap]\n",
				r16(p), r16(p + 2), r16(p + 4));
			trans[0] = r16(p);
			trans[1] = r16(p + 2);
			trans[2] = r16(p + 4);
			haveTrans = true;
			p += 6;
			break;

		case 2:
			std::printf("    cmd 2 JumpVelocity   fall %d  speed %d\n", r16(p), r16(p + 2));
			p += 4;
			break;

		case 3:
			std::printf("    cmd 3 EmptyHands\n");
			break;

		case 4:
			std::printf("    cmd 4 KillItem\n");
			break;

		case 5:
			std::printf("    cmd 5 PlaySound      frame %d  sfx %d\n", r16(p), r16(p + 2) & 0x3FFF);
			p += 4;
			break;

		case 6:
			std::printf("    cmd 6 FlipEffect     frame %d  no %d\n", r16(p), r16(p + 2) & 0x3FFF);
			p += 4;
			break;

		default:
			std::printf("    unknown command type %d - rest skipped\n", typ);
			return;
		}
	}
}

/* The TranslateItem of one animation, without printing anything - the sweep
 * and the -grab default both need the number and not the table. It is the
 * command that runs at the WRAP (AnimateLara, game\laramisc.cpp: the command
 * block is walked only where frame_number has passed frame_end), so a state
 * change never sees it. */
static bool readTranslate(const Tr5Level& l, const AnimRow& a, long trans[3])
{
	size_t cmdAt = 0;
	int32_t nCmd = 0;

	trans[0] = trans[1] = trans[2] = 0;

	if (!a.numCommands || !l.debugCommandBlock(cmdAt, nCmd))
		return false;

	const std::vector<uint8_t>& b = l.debugBytes();
	size_t p = cmdAt + (size_t)a.commandIndex * 2;

	auto r16 = [&](size_t at) -> int16_t
	{
		int16_t v = 0;
		std::memcpy(&v, b.data() + at, 2);
		return v;
	};

	for (int k = 0; k < a.numCommands && p + 2 <= cmdAt + (size_t)nCmd * 2; k++)
	{
		const int typ = (int)(uint16_t)r16(p);
		p += 2;

		if (typ == 1)
		{
			trans[0] = r16(p);
			trans[1] = r16(p + 2);
			trans[2] = r16(p + 4);
			return true;
		}
		else if (typ == 2 || typ == 5 || typ == 6)
			p += 4;
		else if (typ != 3 && typ != 4)
			break;
	}

	return false;
}

/* Where a state change really goes. The change block is three shorts per
 * entry - the goal state, how many ranges, the first range - and the range
 * block four: the frame window it is allowed in, and the animation and FRAME
 * it jumps to (game\control.cpp, GetChange). Without this, "the grab goes to
 * the climb stance" is a guess about which animation number that is. */
static void printChanges(const Tr5Level& l, const AnimRow& a)
{
	size_t chAt = 0, rgAt = 0;
	int32_t nCh = 0, nRg = 0;

	if (!a.numChanges || !l.debugChangeBlock(chAt, nCh) || !l.debugRangeBlock(rgAt, nRg))
		return;

	const std::vector<uint8_t>& b = l.debugBytes();

	auto r16 = [&](size_t at) -> int16_t
	{
		int16_t v = 0;
		std::memcpy(&v, b.data() + at, 2);
		return v;
	};

	std::printf("  state changes:\n");

	for (int c = 0; c < a.numChanges; c++)
	{
		const size_t p = chAt + (size_t)(a.changeIndex + c) * 6;

		if (a.changeIndex + c >= nCh)
			return;

		const int goal = r16(p);
		const int ranges = r16(p + 2);
		const int first = r16(p + 4);

		for (int r = 0; r < ranges; r++)
		{
			const size_t q = rgAt + (size_t)(first + r) * 8;

			if (first + r >= nRg)
				return;

			std::printf("    -> state %-4d frames %d..%d  becomes anim %d frame %d\n",
				goal, r16(q), r16(q + 2), r16(q + 4), r16(q + 6));
		}
	}
}

static void printAnim(const Tr5Level& l, int index, long trans[3], bool& haveTrans)
{
	AnimRow a;

	haveTrans = false;

	if (!readAnim(l, index, a))
	{
		std::printf("\nanim %d: not in this level's table.\n", index);
		return;
	}

	const int frames = a.frameEnd - a.frameStart + 1;
	int keys = (a.frameEnd - a.frameStart) / a.rate + 1;

	if ((a.frameEnd - a.frameStart) % a.rate)
		keys++;

	std::printf("\n=== anim %d ===\n", index);
	std::printf("  state %d  rate %d  size %d shorts  frames %d..%d (%d)  keys %d\n",
		a.state, a.rate, a.size, a.frameStart, a.frameEnd, frames, keys);
	std::printf("  continues with anim %d at frame %d   velocity %ld accel %ld\n",
		a.nextAnim, a.nextFrame, a.velocity, a.acceleration);
	std::printf("  changes %d@%d  commands %d@%d\n",
		a.numChanges, a.changeIndex, a.numCommands, a.commandIndex);

	printCommands(l, a, trans, haveTrans);
	printChanges(l, a);

	std::printf("  key   frame   root offset x/y/z\n");

	for (int k = 0; k < keys; k++)
	{
		short r[9];

		keyFrame(l, a, k, r);
		std::printf("  %-4d  %-6d  %6d %6d %6d\n",
			k, a.frameStart + k * a.rate, r[6], r[7], r[8]);
	}
}

/* The wrap, tick by tick and frame by frame.
 *
 * `posY` is a model of item->pos.y_pos: it does not move while the animation
 * runs (lara_col_climbing puts back what it took out on the continuing path)
 * and it takes the TranslateItem command's y at the wrap, which is what
 * AnimateLara does. The drawn root is pos plus the frame's own offset, and
 * the question this table answers is whether it is continuous. */
static void printWrap(const Tr5Level& l, int index, int before, int after)
{
	AnimRow a;

	if (!readAnim(l, index, a))
		return;

	AnimRow nxt;

	if (!readAnim(l, a.nextAnim, nxt))
		return;

	/* Read the commands again, silently, for the translate alone. */
	long trans[3] = { 0, 0, 0 };
	const bool haveTrans = readTranslate(l, a, trans);

	/* A one frame animation has no three ticks before its wrap - anim 172,
	 * the single frame the ladder grab passes through, is one of them. */
	if (before > a.frameEnd - a.frameStart + 1)
		before = a.frameEnd - a.frameStart + 1;

	if (after > nxt.frameEnd - a.nextFrame + 1)
		after = nxt.frameEnd - a.nextFrame + 1;

	std::printf("\n--- the wrap of anim %d -> anim %d frame %d ---\n",
		index, a.nextAnim, a.nextFrame);
	std::printf("TranslateItem at the wrap: %s(%ld, %ld, %ld)\n",
		haveTrans ? "" : "none ", trans[0], trans[1], trans[2]);

	/* `step` is the position step taken to REACH this tick, in the item's own
	 * y_rot frame - which is the frame TranslateItem writes its three shorts
	 * in (game\control.cpp), so the command's numbers go in unturned. It is
	 * zero on every tick but the wrap, and it is what the narrowed rule
	 * weighs the offset step against. */
	struct Tick { int anim; int frame; long pos; long off[3]; long drawn; long step[3]; };
	std::vector<Tick> ticks;

	long posY = 0;

	for (int i = before; i >= 1; i--)
	{
		Tick t;

		t.anim = index;
		t.frame = a.frameEnd - i + 1;
		t.pos = posY;
		t.step[0] = t.step[1] = t.step[2] = 0;

		if (!frameRoot(l, a, t.frame, t.off))
			return;

		t.drawn = t.pos + t.off[1];
		ticks.push_back(t);
	}

	/* The wrap itself: AnimateLara runs the command, then takes the next
	 * animation and its frame. */
	if (haveTrans)
		posY += trans[1];

	for (int i = 0; i < after; i++)
	{
		Tick t;

		t.anim = a.nextAnim;
		t.frame = a.nextFrame + i;
		t.pos = posY;
		t.step[0] = (!i && haveTrans) ? trans[0] : 0;
		t.step[1] = (!i && haveTrans) ? trans[1] : 0;
		t.step[2] = (!i && haveTrans) ? trans[2] : 0;

		if (!frameRoot(l, nxt, t.frame, t.off))
			return;

		t.drawn = t.pos + t.off[1];
		ticks.push_back(t);
	}

	std::printf("\n  what           anim  frame   pos.y   off.y   drawn y   step\n");
	std::printf("  (a tween frame whose frame steps by one is ACCEPTED by rule K6 and its\n"
		"   root is already blended in time by the pose pair; the today/fixed rows are\n"
		"   what the REFUSED frame - marked <-- below - draws before and after #157)\n");

	long lastDrawn = 0;
	bool have = false;

	for (size_t i = 0; i < ticks.size(); i++)
	{
		const Tick& t = ticks[i];

		if (i)
		{
			/* The in-between frame, at a half. */
			const Tick& p = ticks[i - 1];
			const long posT = InterpLerpLong(p.pos, t.pos, 0.5F);
			const bool refused = p.anim != t.anim || t.frame != p.frame + 1;
			long fix[3], start[3], narrow[3];

			InterpRootLerp(p.off, t.off, 0.5F, fix);

			/* The narrowed rule's own blend, from the narrowed start offset -
			 * the engine's own InterpRootNarrow out of interpolate_math.h. */
			InterpRootNarrow(p.off, t.off, t.step, start);
			InterpRootLerp(start, t.off, 0.5F, narrow);

			/* The rule only reaches the draw where the blend lands somewhere
			 * other than the frame's own offset - the engine tests exactly
			 * this in InterpRootNow, and root_fix counts exactly these. */
			const bool fires = refused &&
				(fix[0] != t.off[0] || fix[1] != t.off[1] || fix[2] != t.off[2]);
			const bool nfires = refused &&
				(narrow[0] != t.off[0] || narrow[1] != t.off[1] || narrow[2] != t.off[2]);

			/* Two different markers, because a row that carries neither
			 * rule's blend can mean two different things: the frame was
			 * accepted by the neighbour rule, so neither rule is ever asked,
			 * or the rule was asked and landed on the frame's own offset.
			 * The numbers on an ACCEPTED row still differ (they are the
			 * arithmetic of a blend nobody performs), and a reader who takes
			 * the shared marker for "the two agree" reads them as a
			 * difference. So an accepted row says so in its own words and
			 * only a refused one speaks of the rule at all. */
			const char* const fixMark = !refused ? "   (accepted frame - not drawn)"
				: (fires ? "" : "   (rule does not fire)");
			const char* const narMark = !refused ? "   (accepted frame - not drawn)"
				: (nfires ? "" : "   (rule does not fire)");

			std::printf("  tween  today   %-4d  %-6d  %6ld  %6ld  %8ld  %+ld%s\n",
				t.anim, t.frame, posT, t.off[1], posT + t.off[1],
				posT + t.off[1] - lastDrawn, refused ? "   <-- refused" : "");
			std::printf("  tween  fixed   %-4d  %-6d  %6ld  %6ld  %8ld  %+ld%s\n",
				t.anim, t.frame, posT, fix[1], posT + fix[1],
				posT + fix[1] - lastDrawn, fixMark);
			std::printf("  tween  narrow  %-4d  %-6d  %6ld  %6ld  %8ld  %+ld%s\n",
				t.anim, t.frame, posT, narrow[1], posT + narrow[1],
				posT + narrow[1] - lastDrawn, narMark);

			if (fires || nfires)
				/* d_off and both verdicts are spelled out explicitly rather
				 * than left to be inferred from other columns, because a
				 * table column that has to be inferred is a table column
				 * that gets read wrong. The worst= pair is what the tween
				 * frames really cost: the blend runs over the whole of
				 * alpha, and at alpha towards 0 today draws prev while the
				 * narrowed rule stands at cur, so the largest difference of
				 * the two pictures over the drawn frames is the UNPAID part
				 * of d_off, axis by axis. */
				std::printf("    step=(%ld,%ld,%ld) kept=(%ld,%ld,%ld)"
					"  fixed=(%ld,%ld,%ld) narrow=(%ld,%ld,%ld)%s"
					"  d_off=(%ld,%ld,%ld) today=%s narrowed=%s"
					" worst=(%ld,%ld,%ld)\n",
					t.step[0], t.step[1], t.step[2],
					labs(t.off[0] - start[0]), labs(t.off[1] - start[1]),
					labs(t.off[2] - start[2]),
					fix[0], fix[1], fix[2], narrow[0], narrow[1], narrow[2],
					(fix[0] == narrow[0] && fix[1] == narrow[1] && fix[2] == narrow[2])
						? "  IDENTICAL" : "",
					t.off[0] - p.off[0], t.off[1] - p.off[1], t.off[2] - p.off[2],
					fires ? "blends" : "stands down",
					nfires ? "blends" : "stands down",
					labs(start[0] - p.off[0]), labs(start[1] - p.off[1]),
					labs(start[2] - p.off[2]));

			if (nfires)
			{
				/* And the line the engine prints for this frame, in full and
				 * in all three axes, so it can be compared directly against
				 * the engine's own log. pos_y is absolute in the level, so
				 * only its step from the tick before the wrap is knowable
				 * here; Y below stands for that tick's y. */
				std::printf("  the -log line this frame produces (Y = pos.y of the tick above):\n"
					"  INTERPLOG root lara anim %d->%d frame %d->%d"
					" offset_prev=(%ld,%ld,%ld) offset_cur=(%ld,%ld,%ld)"
					" drawn=(%ld,%ld,%ld) pos_y=Y%+ld"
					" step=(%ld,%ld,%ld) kept=(%ld,%ld,%ld)\n",
					p.anim, t.anim, p.frame, t.frame,
					p.off[0], p.off[1], p.off[2],
					t.off[0], t.off[1], t.off[2],
					narrow[0], narrow[1], narrow[2], posT,
					t.step[0], t.step[1], t.step[2],
					labs(t.off[0] - start[0]), labs(t.off[1] - start[1]),
					labs(t.off[2] - start[2]));
			}
		}

		std::printf("  tick  %s  %-4d  %-6d  %6ld  %6ld  %8ld  %s%ld\n",
			(i && ticks[i - 1].anim != t.anim) ? "(wrap)" : "        ",
			t.anim, t.frame, t.pos, t.off[1], t.drawn,
			have ? "" : " ", have ? t.drawn - lastDrawn : 0);

		lastDrawn = t.drawn;
		have = true;
	}
}

/* The same table for a change from one animation to another - the ladder
 * grab. There is no TranslateItem here; whatever moves item->pos is the
 * collision code, and that amount is the caller's to supply. */
static void printChange(const Tr5Level& l, int from, int to, const long step[3])
{
	AnimRow a, b;

	if (!readAnim(l, from, a) || !readAnim(l, to, b))
		return;

	long off0[3], off1[3];

	if (!frameRoot(l, a, a.frameEnd, off0) || !frameRoot(l, b, b.frameStart, off1))
		return;

	std::printf("\n--- the change anim %d (frame %d, its last) -> anim %d (frame %d) ---\n",
		from, a.frameEnd, to, b.frameStart);
	std::printf("  assumed pos step of the change (item frame): (%ld,%ld,%ld)\n",
		step[0], step[1], step[2]);

	const long posStep = step[1];
	const long drawn0 = off0[1];
	const long drawn1 = posStep + off1[1];
	const long posT = InterpLerpLong(0, posStep, 0.5F);
	long fix[3], start[3], narrow[3];

	InterpRootLerp(off0, off1, 0.5F, fix);
	InterpRootNarrow(off0, off1, step, start);
	InterpRootLerp(start, off1, 0.5F, narrow);

	std::printf("  tick  N      pos.y %6d  off.y %6ld  drawn %8ld\n", 0, off0[1], drawn0);
	std::printf("  tween today  pos.y %6ld  off.y %6ld  drawn %8ld\n", posT, off1[1], posT + off1[1]);
	std::printf("  tween fixed  pos.y %6ld  off.y %6ld  drawn %8ld\n", posT, fix[1], posT + fix[1]);
	std::printf("  tween narrow pos.y %6ld  off.y %6ld  drawn %8ld\n", posT, narrow[1], posT + narrow[1]);
	std::printf("  tick  N+1    pos.y %6ld  off.y %6ld  drawn %8ld\n", posStep, off1[1], drawn1);
	std::printf("  offsets x/z: last (%ld,%ld) -> first (%ld,%ld)\n",
		off0[0], off0[2], off1[0], off1[2]);
	/* Same three additions as the wrap table above: d_off, which of the two
	 * rules blends at all, and what the difference of the two pictures grows
	 * to over the tween frames. */
	std::printf("  step=(%ld,%ld,%ld) kept=(%ld,%ld,%ld)  fixed=(%ld,%ld,%ld) narrow=(%ld,%ld,%ld)%s"
		"  d_off=(%ld,%ld,%ld) today=%s narrowed=%s worst=(%ld,%ld,%ld)\n",
		step[0], step[1], step[2],
		labs(off1[0] - start[0]), labs(off1[1] - start[1]), labs(off1[2] - start[2]),
		fix[0], fix[1], fix[2], narrow[0], narrow[1], narrow[2],
		(fix[0] == narrow[0] && fix[1] == narrow[1] && fix[2] == narrow[2]) ? "  IDENTICAL" : "",
		off1[0] - off0[0], off1[1] - off0[1], off1[2] - off0[2],
		(fix[0] != off1[0] || fix[1] != off1[1] || fix[2] != off1[2]) ? "blends" : "stands down",
		(narrow[0] != off1[0] || narrow[1] != off1[1] || narrow[2] != off1[2]) ? "blends" : "stands down",
		labs(start[0] - off0[0]), labs(start[1] - off0[1]), labs(start[2] - off0[2]));

	if (narrow[0] != off1[0] || narrow[1] != off1[1] || narrow[2] != off1[2])
		std::printf("  the -log line this frame produces (Y = pos.y of tick N):\n"
			"  INTERPLOG root lara anim %d->%d frame %d->%d"
			" offset_prev=(%ld,%ld,%ld) offset_cur=(%ld,%ld,%ld)"
			" drawn=(%ld,%ld,%ld) pos_y=Y%+ld step=(%ld,%ld,%ld) kept=(%ld,%ld,%ld)\n",
			from, to, a.frameEnd, b.frameStart,
			off0[0], off0[1], off0[2], off1[0], off1[1], off1[2],
			narrow[0], narrow[1], narrow[2], posT,
			step[0], step[1], step[2],
			labs(off1[0] - start[0]), labs(off1[1] - start[1]), labs(off1[2] - start[2]));
	else
		std::printf("  the rule does not fire here: the blend is the frame's own offset\n");
}

/* ==========================================================================
 * THE SWEEP - EVERY TRANSITION OF LARA'S IN ONE LEVEL, AND WHAT PAYS FOR IT.
 * ==========================================================================
 *
 * The narrowed rule blends the offset step only as far as the tick's own
 * POSITION step pays for it, so this table lists every transition with an
 * offset step and the position step beside it. Two kinds reach the draw:
 *
 *   WRAP    frame_number passed frame_end: AnimateLara runs the command block
 *           first, so a TranslateItem here IS the position step, and it is
 *           already written in the y_rot frame the rule compares in.
 *   CHANGE  GetChange swapped the animation before that test, so NO command
 *           runs and the level file knows of no position step at all. What
 *           really moves her is the state routine - the vault, the ledge grab,
 *           the pushblock snap, the climb shifts - which writes item->pos by
 *           hand. The ENGINE measures that step and this table cannot: a
 *           CHANGE row reads "not at all (file)" and means "nothing in the
 *           file pays for it", never "nothing pays for it".
 *
 * A change is allowed over a whole frame range, so the offset step depends on
 * which frame it fired at; the row prints the frame of the range that gives
 * the LARGEST step, the worst case that could occur in play. */
static void sweep(const Tr5Level& l, int first, int last, long least)
{
	size_t chAt = 0, rgAt = 0;
	int32_t nCh = 0, nRg = 0;
	const bool haveChanges = l.debugChangeBlock(chAt, nCh) && l.debugRangeBlock(rgAt, nRg);
	const std::vector<uint8_t>& b = l.debugBytes();

	auto r16 = [&](size_t at) -> int16_t
	{
		int16_t v = 0;
		std::memcpy(&v, b.data() + at, 2);
		return v;
	};

	std::printf("\n=== sweep: animations %d..%d, offset steps of %ld units or more ===\n",
		first, last, least);
	std::printf("kind    from            to             d off (x,y,z)      step (x,y,z)      "
		"kept (x,y,z)      today  narrow  verdict\n");

	/* `beyond` is the invariant this table proves, counted here rather than
	 * left for a reader to derive from the per-row columns: the claim is
	 * that the narrowed rule blends never MORE than today's and never in
	 * another direction.
	 *
	 * It is not counted as "blends where today does not", which would be a
	 * DEAD counter here: a row only enters the sweep when some axis moves by
	 * `least` units or more, so today's blend at a half always lands off
	 * `cur` and the reverse case cannot arise whatever the rule does
	 * (measured against a mutated InterpRootNarrow, it stayed 0). What is
	 * counted instead is the invariant itself, axis by axis: the narrowed
	 * draw must lie ON THE SEGMENT from `cur` to today's draw - same side,
	 * no further - which has "blends where today stands down" inside it as
	 * the case where that segment is a point. */
	long rows = 0, full = 0, part = 0, none = 0, stops = 0, beyond = 0;

	for (int i = first; i <= last; i++)
	{
		AnimRow a;

		if (!readAnim(l, i, a) || a.rate < 1 || a.size < 9)
			continue;

		for (int kind = 0; kind < 2; kind++)
		{
			const int entries = kind ? (haveChanges ? a.numChanges : 0) : 1;

			for (int c = 0; c < entries; c++)
			{
				int to = 0, toFrame = 0, lo = 0, hi = 0, ranges = 0, firstRange = 0;
				long step[3] = { 0, 0, 0 };

				if (!kind)
				{
					to = a.nextAnim;
					toFrame = a.nextFrame;
					lo = hi = a.frameEnd;
					readTranslate(l, a, step);
				}
				else
				{
					if (a.changeIndex + c >= nCh)
						break;

					ranges = r16(chAt + (size_t)(a.changeIndex + c) * 6 + 2);
					firstRange = r16(chAt + (size_t)(a.changeIndex + c) * 6 + 4);
				}

				for (int r = 0; r < (kind ? ranges : 1); r++)
				{
					AnimRow bnx;

					if (kind)
					{
						if (firstRange + r >= nRg)
							break;

						const size_t q = rgAt + (size_t)(firstRange + r) * 8;

						lo = r16(q);
						hi = r16(q + 2);
						to = r16(q + 4);
						toFrame = r16(q + 6);
					}

					if (!readAnim(l, to, bnx) || bnx.rate < 1 || bnx.size < 9)
						continue;

					long cur[3];

					if (!frameRoot(l, bnx, toFrame, cur))
						continue;

					/* The frame of the range that gives the biggest step. */
					long prev[3] = { 0, 0, 0 }, best = -1;
					int bestFrame = lo;

					for (int f = lo; f <= hi && f - lo < 256; f++)
					{
						long o[3];

						if (!frameRoot(l, a, f, o))
							continue;

						const long m = labs(cur[0] - o[0]) > labs(cur[1] - o[1])
							? (labs(cur[0] - o[0]) > labs(cur[2] - o[2])
								? labs(cur[0] - o[0]) : labs(cur[2] - o[2]))
							: (labs(cur[1] - o[1]) > labs(cur[2] - o[2])
								? labs(cur[1] - o[1]) : labs(cur[2] - o[2]));

						if (m > best)
						{
							best = m;
							bestFrame = f;
							prev[0] = o[0]; prev[1] = o[1]; prev[2] = o[2];
						}
					}

					if (best < least)
						continue;

					long start[3], today[3], narrow[3], kept[3];

					InterpRootLerp(prev, cur, 0.5F, today);
					InterpRootNarrow(prev, cur, step, start);
					InterpRootLerp(start, cur, 0.5F, narrow);

					bool anyPaid = false, allPaid = true;

					for (int k = 0; k < 3; k++)
					{
						kept[k] = labs(cur[k] - start[k]);

						if (kept[k])
							anyPaid = true;

						if (kept[k] != labs(cur[k] - prev[k]))
							allPaid = false;
					}

					const bool firesToday = today[0] != cur[0] || today[1] != cur[1] ||
						today[2] != cur[2];
					const bool firesNarrow = narrow[0] != cur[0] || narrow[1] != cur[1] ||
						narrow[2] != cur[2];

					rows++;

					if (allPaid)
						full++;
					else if (anyPaid)
						part++;
					else
						none++;

					if (firesToday && !firesNarrow)
						stops++;

					for (int k = 0; k < 3; k++)
					{
						const long t = today[k] - cur[k];
						const long n = narrow[k] - cur[k];

						if ((t < 0 && n > 0) || (t > 0 && n < 0) ||
							(!t && n) || labs(n) > labs(t))
						{
							beyond++;
							break;
						}
					}

					std::printf("%-6s  %4d f%-8d %4d f%-8d (%5ld,%5ld,%5ld)  "
						"(%5ld,%5ld,%5ld)  (%5ld,%5ld,%5ld)  %-5s  %-6s  %s\n",
						kind ? "change" : "wrap", i, bestFrame, to, toFrame,
						cur[0] - prev[0], cur[1] - prev[1], cur[2] - prev[2],
						step[0], step[1], step[2],
						kept[0], kept[1], kept[2],
						firesToday ? "fires" : "-", firesNarrow ? "fires" : "-",
						allPaid ? "paid in full"
							: (anyPaid ? "paid in part"
								: (kind ? "not at all (file)" : "not at all")));
				}
			}
		}
	}

	std::printf("\n%ld transitions with a step of %ld or more: %ld paid in full, %ld in part, "
		"%ld not at all;\n%ld of them blend today and do not blend under the narrowed rule,\n"
		"and in %ld the narrowed draw leaves the segment from the frame's own offset to\n"
		"today's draw - further, or the other way (this one must read 0: it is the round's\n"
		"invariant, that the rule blends never more than today and never in another direction).\n",
		rows, least, full, part, none, stops, beyond);
}

/* Lara's own slice of the animation table. A TR5 moveable is 20 bytes with the
 * animation index a uint16 at +16 (the frame pointer is at +12); object 0 is
 * LARA, and her animations run up to the next moveable's first one. */
static bool laraAnims(const Tr5Level& l, int& first, int& last)
{
	size_t objAt = 0, frAt = 0;
	int32_t objN = 0, frN = 0;
	size_t animAt = 0;
	int32_t animN = 0;

	if (!l.debugModelBlocks(objAt, objN, frAt, frN) || !l.debugAnimBlock(animAt, animN))
		return false;

	const std::vector<uint8_t>& b = l.debugBytes();
	int best = -1;

	first = -1;
	last = animN - 1;

	for (int i = 0; i < objN; i++)
	{
		uint32_t id = 0;
		uint16_t an = 0;

		std::memcpy(&id, b.data() + objAt + (size_t)i * 20 + 0, 4);
		std::memcpy(&an, b.data() + objAt + (size_t)i * 20 + 16, 2);

		if (an == 0xFFFF)
			continue;

		if (!id)
			first = (int)an;
		else if (first >= 0 && (int)an > first && ((int)an < best || best < 0))
			best = (int)an;
	}

	if (first < 0)
		return false;

	if (best > 0)
		last = best - 1;

	return true;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::printf("usage: RootProbe <level.trc> [anim ...]\n"
			"       RootProbe <level.trc> -grab <from> <to> [posStepY]\n"
			"       RootProbe <level.trc> -sweep [leastStep]\n");
		return 2;
	}

	Tr5Level l;
	std::string e;

	if (!l.load(toW(argv[1]), e))
	{
		std::printf("not loadable: %s\n", e.c_str());
		return 1;
	}

	size_t at = 0;
	int32_t n = 0;

	l.debugAnimBlock(at, n);
	std::printf("%s: %d animations\n", argv[1], (int)n);

	if (argc >= 3 && !_stricmp(argv[2], "-sweep"))
	{
		const long least = argc >= 4 ? std::atol(argv[3]) : 8;
		int first = 0, last = 0;

		if (!laraAnims(l, first, last))
		{
			std::printf("no LARA moveable in this level\n");
			return 1;
		}

		sweep(l, first, last, least);

		return 0;
	}

	if (argc >= 4 && !_stricmp(argv[2], "-grab"))
	{
		const int from = std::atoi(argv[3]);
		const int to = argc >= 5 ? std::atoi(argv[4]) : 0;
		long trans[3] = { 0, 0, 0 };
		long step[3] = { 0, 0, 0 };
		bool haveTrans = false;
		AnimRow a;

		/* The step is the leaving animation's own TranslateItem where the
		 * change really is its wrap - anim 172, the one frame the ladder
		 * grab passes through, carries (0,-190,0). An explicit argument
		 * overrides it for a change that a state routine moves by hand,
		 * which the file cannot know. */
		if (readAnim(l, from, a) && a.nextAnim == to && readTranslate(l, a, trans))
		{
			step[0] = trans[0];
			step[1] = trans[1];
			step[2] = trans[2];
		}

		if (argc >= 6)
			step[1] = std::atol(argv[5]);

		printAnim(l, from, trans, haveTrans);
		printAnim(l, to, trans, haveTrans);
		printChange(l, from, to, step);

		return 0;
	}

	std::vector<int> anims;

	for (int i = 2; i < argc; i++)
		anims.push_back(std::atoi(argv[i]));

	if (anims.empty())
	{
		static const int kClimb[] = { 96, 161, 164, 165, 168, 169, 173 };

		for (int i = 0; i < (int)(sizeof(kClimb) / sizeof(kClimb[0])); i++)
			anims.push_back(kClimb[i]);
	}

	for (size_t i = 0; i < anims.size(); i++)
	{
		long trans[3];
		bool haveTrans = false;

		printAnim(l, anims[i], trans, haveTrans);
		printWrap(l, anims[i], 3, 3);
	}

	return 0;
}
