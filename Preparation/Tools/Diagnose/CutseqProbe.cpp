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

/* CutseqProbe - what the tracks of a cutseq.bin do between two frames.
 * Build with bcp.bat.
 *
 *   CutseqProbe [cutseq.bin]                  every cutscene and the totals
 *   CutseqProbe [cutseq.bin] -frames n a b    cutscene n, frames a..b: the
 *                                             decoded eye, target and roots
 *   CutseqProbe [cutseq.bin] -cutlog n        the CUTLOG lines a run of
 *                                             cutscene n at fps_mode 1 writes
 *   CutseqProbe [cutseq.bin] -actors [n]      every cutscene's audio track
 *                                             and its actors' object slots
 *                                             and node counts, or n's alone
 *
 * The file is read in place and never written. Every cutscene is decoded with
 * the engine's own decoder (game\cutseq_decode.h, the templates DecodeAnim
 * and InitPackNodes forward to, instantiated with the structs of
 * global\types.h) and measured with the engine's own blend rules
 * (game\interpolate_math.h): the camera's per-frame steps and the cuts at
 * INTERP_CUTSEQ_SNAP, the actors' root steps and the jumps above
 * INTERP_CUTSEQ_ROOT_SNAP, and the node axes that turn exactly half a turn in
 * one frame (InterpHalfTurn10). A frame here is the index DecodeAnim is
 * called with; the engine draws it while GLOBAL_cutseq_frame reads one more,
 * and CUTLOG's frame= is this index.
 *
 * The layout of the file (specific\alexstuff.cpp, aReadCutData): 2048 bytes
 * of header, slot n (1..44) an offset and a length at n * 8, the eight bytes
 * of slot 0 the text "(C) DEL!". Each cutscene starts with a NEW_CUTSCENE,
 * its actors and its camera are node tables at the offsets it names. */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX		// std::min and std::max below, not the windows.h macros
#define _USE_MATH_DEFINES
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>
#include <limits.h>
#include <mmeapi.h>
#include <mmreg.h>
#include <d3d9.h>
#include <dinput.h>
#include <dsound.h>
#include <vector>
#include <algorithm>

#include "../../../TOMB5 Code/TOMB5/global/types.h"
#include "../../../TOMB5 Code/TOMB5/game/cutseq_decode.h"
#include "../../../TOMB5 Code/TOMB5/game/interpolate_math.h"

/* The game folder's own cutseq.bin, read in place and never written. The
 * game folder lies beside the repository's Preparation tree, so the file
 * is looked for from the EXE's folder first and the working folder second;
 * the first argument names another file. */
static const char* DEFAULT_REL =
	"..\\..\\..\\Tomb5 Steam Version\\data\\cutseq.bin";

static const char* default_path(char* path, size_t size)
{
	char* exe = 0;

	if (!_get_pgmptr(&exe) && exe)
	{
		const char* cut = strrchr(exe, '\\');
		const size_t dir = cut ? (size_t)(cut - exe) + 1 : 0;

		if (dir && dir + strlen(DEFAULT_REL) < size)
		{
			memcpy(path, exe, dir);
			strcpy_s(path + dir, size - dir, DEFAULT_REL);
			FILE* f = fopen(path, "rb");

			if (f)
			{
				fclose(f);
				return path;
			}
		}
	}

	strcpy_s(path, size, DEFAULT_REL);
	return path;
}

static const int CUT_COUNT = 44;

/* The names of cutseq_control_routines (game\deltapak.cpp), slot by slot. */
static const char* CUT_NAME[CUT_COUNT + 1] =
{
	"-", "stealth3", "stealth3", "stealth3", "stealth3", "joby6", "andy5", "andrea3b",
	"andrea3", "andy4b", "andy4", "richcut4", "joby10", "joby9", "andy3", "joby5",
	"andrea2", "andrea1", "joby4", "andy2", "hamgate", "andy1", "joby3", "richcut3",
	"richcut1", "joby2", "richcut2", "cranecut", "special1", "special2", "special3",
	"special4", "joby8", "andy6", "andypew", "andy7", "cossack", "andy9", "andy8", "andy10",
	"joby7", "andrea4", "monk2", "swampy", "andy11"
};

struct Actor
{
	std::vector<PACKNODE> nodes;
	int count;		// the nodes DecodeAnim walks
	bool decoded;
};

struct Scene
{
	NEW_CUTSCENE* cut;
	int frames;
	PACKNODE cam[2];
	Actor actor[INTERP_CUTSEQ_ACTORS];
};

/* The cutscene's data copied into a buffer of its own with slack behind it:
 * the track reader takes four bytes at a time. */
static char* SceneData(const std::vector<char>& file, int n, long* size)
{
	const long offset = *(const long*)&file[n * 8];

	*size = *(const long*)&file[n * 8 + 4];

	if (offset <= 0 || *size <= 0 || offset + *size > (long)file.size())
		return 0;

	char* data = (char*)calloc(*size + 16, 1);

	memcpy(data, &file[offset], *size);
	return data;
}

/* init_cutseq_actors, without the game: the node tables, the two cutscenes
 * whose length or audio it overrides, and the camera's two nodes. */
static bool OpenScene(char* data, int n, Scene* s)
{
	s->cut = (NEW_CUTSCENE*)data;

	if (s->cut->numactors < 0 || s->cut->numactors > INTERP_CUTSEQ_ACTORS)
		return false;

	if (n == 26)
		s->cut->numframes = 1978;

	s->frames = s->cut->numframes;

	for (int i = 0; i < s->cut->numactors; i++)
	{
		Actor* a = &s->actor[i];
		const int pda = s->cut->actor_data[i].nodes + 1;

		a->count = i ? pda : 16;	// do_new_cutscene_camera decodes 16 for Lara
		a->nodes.assign(std::max(pda, a->count), PACKNODE());
		a->decoded = i || s->cut->actor_data[0].objslot != NO_ITEM;

		if (a->decoded)
			CutseqInitPackNodes((NODELOADHEADER*)&data[s->cut->actor_data[i].offset],
				a->nodes.data(), &data[s->cut->actor_data[i].offset], pda);
	}

	memset(s->cam, 0, sizeof(s->cam));
	CutseqInitPackNodes((NODELOADHEADER*)&data[s->cut->camera_offset], s->cam,
		&data[s->cut->camera_offset], 2);
	return true;
}

/* do_new_cutscene_camera's decode for one frame, and the eye and target in
 * the track's own axes (cutrot and the origin turn and move both alike, so
 * the steps between frames are the same). */
static void DecodeFrame(Scene* s, long f, long eye[3], long target[3])
{
	CutseqDecodeAnim(s->cam, 2, f, 0xFFFF);
	target[0] = s->cam[0].xrot_run << 1;
	target[1] = s->cam[0].yrot_run << 1;
	target[2] = s->cam[0].zrot_run << 1;
	eye[0] = s->cam[1].xrot_run << 1;
	eye[1] = s->cam[1].yrot_run << 1;
	eye[2] = s->cam[1].zrot_run << 1;

	for (int i = 0; i < s->cut->numactors; i++)
		if (s->actor[i].decoded)
			CutseqDecodeAnim(s->actor[i].nodes.data(), s->actor[i].count, f, 0x3FF);
}

static void Triples(const Actor* a, short* out)
{
	for (int k = 0; k < a->count; k++)
	{
		out[3 * k] = a->nodes[k].xrot_run;
		out[3 * k + 1] = a->nodes[k].yrot_run;
		out[3 * k + 2] = a->nodes[k].zrot_run;
	}
}

// One step between two frames, for the lists of the largest steps under a rule.
struct FastStep
{
	long step; int scene; long frame; int actor;
};

struct Totals
{
	long scenes, frames, steps;
	long cuts, cutScenes;
	long camMidJumps;		// cuts at or under INTERP_CAM_SNAP
	long rootJumps, rootJumpsNoCut, rootJumpsRaw;
	long axisSteps, halfAxes, halfNodes, halfMulti, nearHalf, quarter;
	long keysWide;
	long detour10, detour30, flipLike, detourWide;	// see Detour
	long laraTicks, laraLag32;			// Lara's blended ticks, and those with a braid lag over 32
	double detourMax;
	int maxActorNodes, maxSceneNodes, maxActors;
	long residualMin, residualMax;
	long maxCutEye, maxCutTarget; int maxCutScene; long maxCutFrame;
	std::vector<long> eyeSteps, targetSteps, camSteps;
	std::vector<FastStep> rootSteps;	// root steps under INTERP_CUTSEQ_ROOT_SNAP
	long rootMax;
	long rootJumpsUnder, rootJumpsUnderNoCut, rootMotionOver, rootAloneMin, rootMotionMax;
	int cutFree[CUT_COUNT + 1];
	int nCutFree;
};

static long Percentile(std::vector<long>& v, double p)
{
	if (v.empty())
		return 0;

	std::sort(v.begin(), v.end());
	size_t k = (size_t)(p * (double)(v.size() - 1) + 0.5);
	return v[k];
}

static double TurnDegrees(const float* a, const float* b)
{
	double c = InterpPoseTurnCos(a, b);

	c = c > 1.0 ? 1.0 : c < -1.0 ? -1.0 : c;
	return acos(c) * 180.0 / M_PI;
}

/* How far the per-axis blend of one node at 0.5 strays from the shortest
 * rotation between its two ticks: the turn from the previous frame to the
 * blend plus the turn from the blend to the current one, less the turn
 * between the two. Zero when the blend lies on the shortest arc; an Euler
 * flip in the track (a small rotation written as large angles) shows as a
 * large detour on a small turn. */
static void Detour(const short* p, const short* c, Totals* t)
{
	short a[3], b[3], m[3];
	float ma[9], mb[9], mm[9];

	for (int k = 0; k < 3; k++)
	{
		a[k] = (short)((p[k] & 0x3FF) << 6);
		b[k] = (short)((c[k] & 0x3FF) << 6);
		m[k] = (short)((InterpLerpAngle10(p[k], c[k], 0.5F) & 0x3FF) << 6);
	}

	if (a[0] == b[0] && a[1] == b[1] && a[2] == b[2])
		return;

	InterpPoseRot(a, ma);
	InterpPoseRot(b, mb);
	InterpPoseRot(m, mm);

	const double whole = TurnDegrees(ma, mb);
	const double detour = TurnDegrees(ma, mm) + TurnDegrees(mm, mb) - whole;

	if (detour > t->detourMax)
		t->detourMax = detour;

	if (detour > 10.0)
		t->detour10++;

	if (detour > 30.0)
	{
		long wide = 0;

		for (int k = 0; k < 3; k++)
			wide = std::max(wide, labs(((((long)c[k] - (long)p[k]) + 512) & 0x3FF) - 512));

		t->detour30++;

		if (whole < 45.0)
			t->flipLike++;

		if (wide >= 384)
			t->detourWide++;
	}
}

static void ProbeScene(char* data, int n, Totals* t, std::vector<FastStep>* fast)
{
	Scene s;

	if (!OpenScene(data, n, &s))
	{
		printf("cutseq %2d %-9s unreadable header\n", n, CUT_NAME[n]);
		return;
	}

	int sceneNodes = 0;

	for (int i = 0; i < s.cut->numactors; i++)
	{
		if (!s.actor[i].decoded)
			continue;

		sceneNodes += s.actor[i].count;
		t->maxActorNodes = std::max(t->maxActorNodes, s.actor[i].count);

		for (int k = 1; k < s.actor[i].count; k++)
			if (s.actor[i].nodes[k].xkey > 0x3FF || s.actor[i].nodes[k].ykey > 0x3FF ||
				s.actor[i].nodes[k].zkey > 0x3FF || s.actor[i].nodes[k].xkey < 0 ||
				s.actor[i].nodes[k].ykey < 0 || s.actor[i].nodes[k].zkey < 0)
				t->keysWide++;
	}

	t->maxSceneNodes = std::max(t->maxSceneNodes, sceneNodes);
	t->maxActors = std::max(t->maxActors, (int)s.cut->numactors);

	long eye[3], target[3], prevEye[3], prevTarget[3];
	std::vector<short> prev[INTERP_CUTSEQ_ACTORS], cur[INTERP_CUTSEQ_ACTORS];
	std::vector<long> eyeSteps, targetSteps;
	std::vector<long> rootHist[INTERP_CUTSEQ_ACTORS];	// each actor's root step per frame
	std::vector<char> camCut(s.frames, 0);			// the camera cut at this frame
	long cuts = 0, rootJumps = 0, halfAxes = 0, halfNodes = 0, axisSteps = 0;
	long maxEye = 0, maxTarget = 0;
	char cutList[4096] = "";
	char rootList[2048] = "";
	char halfList[2048] = "";

	for (int i = 0; i < s.cut->numactors; i++)
	{
		prev[i].assign(3 * s.actor[i].count, 0);
		cur[i].assign(3 * s.actor[i].count, 0);
		rootHist[i].assign(s.frames, 0);
	}

	for (long f = 0; f < s.frames; f++)
	{
		DecodeFrame(&s, f, eye, target);

		for (int i = 0; i < s.cut->numactors; i++)
			if (s.actor[i].decoded)
				Triples(&s.actor[i], cur[i].data());

		if (f > 0)
		{
			long es, ts;
			const bool cut = InterpCutseqCameraCut(prevEye, eye, prevTarget, target, &es, &ts);

			t->steps++;
			eyeSteps.push_back(es);
			targetSteps.push_back(ts);
			t->eyeSteps.push_back(es);
			t->targetSteps.push_back(ts);
			t->camSteps.push_back(std::max(es, ts));
			maxEye = std::max(maxEye, es);
			maxTarget = std::max(maxTarget, ts);

			if (cut)
			{
				cuts++;
				camCut[f] = 1;

				if (!(es > INTERP_CAM_SNAP || ts > INTERP_CAM_SNAP))
					t->camMidJumps++;

				if (std::max(es, ts) > std::max(t->maxCutEye, t->maxCutTarget))
				{
					t->maxCutEye = es;
					t->maxCutTarget = ts;
					t->maxCutScene = n;
					t->maxCutFrame = f;
				}

				char one[48];
				sprintf(one, " %ld(%ld/%ld)", f, es, ts);

				if (strlen(cutList) + strlen(one) < sizeof(cutList) - 8)
					strcat(cutList, one);
			}
			else
			{
				FastStep fs = { std::max(es, ts), n, f, -1 };
				fast->push_back(fs);
			}

			for (int i = 0; i < s.cut->numactors; i++)
			{
				if (!s.actor[i].decoded)
					continue;

				std::vector<short> out(cur[i].size());
				long rootStep;
				int ht, hn;
				const bool blended = InterpCutseqBlend(prev[i].data(), cur[i].data(),
					s.actor[i].count, 0.5F, out.data(), &rootStep, &ht, &hn);

				t->rootMax = std::max(t->rootMax, rootStep);
				rootHist[i][f] = rootStep;

				if (blended)
				{
					FastStep rs = { rootStep, n, f, i };
					t->rootSteps.push_back(rs);

					/* Lara's braid is stepped once per tick from the tick's
					 * pose, so on the frame between two ticks it stands
					 * behind her blended root by half the root's step. */
					if (!i && !cut)
					{
						t->laraTicks++;

						if (InterpCutseqRootStep(prev[0].data(), out.data()) > 32)
							t->laraLag32++;
					}
				}

				if (!blended)
				{
					rootJumps++;

					if (!cut)
						t->rootJumpsNoCut++;

					char one[48];
					sprintf(one, " a%d@%ld(%ld)%s", i, f, rootStep, cut ? "" : "!");

					if (strlen(rootList) + strlen(one) < sizeof(rootList) - 8)
						strcat(rootList, one);
				}

				if (InterpManhattan(cur[i][0] - prev[i][0], cur[i][1] - prev[i][1],
					cur[i][2] - prev[i][2]) > INTERP_CUTSEQ_ROOT_SNAP)
					t->rootJumpsRaw++;

				for (int k = 1; k < s.actor[i].count; k++)
				{
					int axes = 0;

					for (int a = 0; a < 3; a++)
					{
						const short p = prev[i][3 * k + a];
						const short c = cur[i][3 * k + a];
						const long d = ((((long)c - (long)p) + 512) & 0x3FF) - 512;

						axisSteps++;

						if (InterpHalfTurn10(p, c))
							axes++;
						else if (labs(d) >= 384)
							t->nearHalf++;
						else if (labs(d) >= 256)
							t->quarter++;
					}

					if (!axes)
						Detour(&prev[i][3 * k], &cur[i][3 * k], t);

					if (axes)
					{
						halfAxes += axes;
						halfNodes++;

						if (axes > 1)
							t->halfMulti++;

						char one[48];
						sprintf(one, " a%d.n%d@%ld/%d", i, k, f, axes);

						if (strlen(halfList) + strlen(one) < sizeof(halfList) - 8)
							strcat(halfList, one);
					}
				}
			}
		}

		memcpy(prevEye, eye, sizeof(eye));
		memcpy(prevTarget, target, sizeof(target));

		for (int i = 0; i < s.cut->numactors; i++)
			prev[i] = cur[i];
	}

	/* A root step is a jump when it stands alone - more than four times
	 * either neighbour - and motion otherwise. Counted on both sides of
	 * INTERP_CUTSEQ_ROOT_SNAP: a jump under it is blended as a slide, a
	 * motion over it is drawn at the tick's values. */
	for (int i = 0; i < s.cut->numactors; i++)
	{
		for (long f = 1; f < s.frames; f++)
		{
			const long st = rootHist[i][f];
			const long before = rootHist[i][f - 1];
			const long after = f + 1 < s.frames ? rootHist[i][f + 1] : 0;

			if (st <= 256)
				continue;

			const bool alone = st > 4 * before && st > 4 * after;

			if (alone && st <= INTERP_CUTSEQ_ROOT_SNAP)
			{
				t->rootJumpsUnder++;

				if (!camCut[f])
					t->rootJumpsUnderNoCut++;
			}
			else if (!alone && st > INTERP_CUTSEQ_ROOT_SNAP)
				t->rootMotionOver++;

			if (alone)
				t->rootAloneMin = std::min(t->rootAloneMin, st);
			else
				t->rootMotionMax = std::max(t->rootMotionMax, st);
		}
	}

	/* What the decoder left of each camera track after the last frame: a
	 * decoder in step with the file ends within a few words of the end. */
	for (int k = 0; k < 2; k++)
	{
		const long left[3] = { (long)s.cam[k].decode_x.length, (long)s.cam[k].decode_y.length,
			(long)s.cam[k].decode_z.length };

		for (int a = 0; a < 3; a++)
		{
			t->residualMin = std::min(t->residualMin, left[a]);
			t->residualMax = std::max(t->residualMax, left[a]);
		}
	}

	char nodeList[128] = "";

	for (int i = 0; i < s.cut->numactors; i++)
	{
		char one[16];
		sprintf(one, "%s%d", i ? "," : "", s.actor[i].decoded ? s.actor[i].count : 0);
		strcat(nodeList, one);
	}

	printf("cutseq %2d %-9s frames=%-5d actors=%d nodes=%-24s eye max=%ld p99=%ld "
		"target max=%ld p99=%ld cuts=%ld root_jumps=%ld halfturns=%ld/%ld\n",
		n, CUT_NAME[n], s.frames, (int)s.cut->numactors, nodeList, maxEye,
		Percentile(eyeSteps, 0.99), maxTarget, Percentile(targetSteps, 0.99), cuts, rootJumps,
		halfAxes, halfNodes);

	if (cuts)
		printf("    cuts (frame(eye/target)):%s\n", cutList);

	if (rootJumps)
		printf("    root jumps (actor@frame(step), ! without a camera cut):%s\n", rootList);

	if (halfNodes)
		printf("    half turns (actor.node@frame/axes):%s\n", halfList);

	t->scenes++;
	t->frames += s.frames;
	t->cuts += cuts;
	t->rootJumps += rootJumps;
	t->halfAxes += halfAxes;
	t->halfNodes += halfNodes;
	t->axisSteps += axisSteps;

	if (cuts)
		t->cutScenes++;
	else
		t->cutFree[t->nCutFree++] = n;
}

static int DumpFrames(const std::vector<char>& file, int n, long a, long b)
{
	long size;
	char* data = SceneData(file, n, &size);
	Scene s;

	if (!data || !OpenScene(data, n, &s))
	{
		printf("cutseq %d: not in the file\n", n);
		return 1;
	}

	long eye[3], target[3];

	printf("cutseq %d %s frames=%d org=(%ld,%ld,%ld) actors=%d; eye and target in the track's "
		"axes before cutrot and the origin, roots at three times node 0\n", n, CUT_NAME[n],
		s.frames, s.cut->orgx, s.cut->orgy, s.cut->orgz, (int)s.cut->numactors);

	for (long f = 0; f < s.frames && f <= b; f++)
	{
		DecodeFrame(&s, f, eye, target);

		if (f < a)
			continue;

		printf("f=%ld eye=(%ld,%ld,%ld) target=(%ld,%ld,%ld)", f, eye[0], eye[1], eye[2],
			target[0], target[1], target[2]);

		for (int i = 0; i < s.cut->numactors; i++)
			if (s.actor[i].decoded)
				printf(" a%d=(%d,%d,%d)", i, 3 * s.actor[i].nodes[0].xrot_run,
					3 * s.actor[i].nodes[0].yrot_run, 3 * s.actor[i].nodes[0].zrot_run);

		printf("\n");
	}

	free(data);
	return 0;
}

/* The CUTLOG lines a run at fps_mode 1 writes for cutscene n (game\
 * cutseq_interp.cpp), frame for frame: one frame between two ticks per tick,
 * the scene's first tick drawn live (why=open), a camera cut drawing that
 * frame live before any actor is looked at, and otherwise one line per actor
 * whose root jumps and one per actor with a node kept for a half turn. The
 * close line counts the frames 0 .. numframes - 1 only; the game plays on
 * through its fade-out on the last frame, which adds ticks and, on a
 * repeated frame, blended frames that change nothing. */
static int SimulateLog(const std::vector<char>& file, int n)
{
	long size;
	char* data = SceneData(file, n, &size);
	Scene s;

	if (!data || !OpenScene(data, n, &s))
	{
		printf("cutseq %d: not in the file\n", n);
		return 1;
	}

	int nodes = 0;

	for (int i = 0; i < s.cut->numactors; i++)
		nodes += s.actor[i].decoded ? s.actor[i].count : 0;

	printf("CUTLOG open cutseq=%d frames=%d actors=%d nodes=%d fps_mode=1\n", n, s.frames,
		(int)s.cut->numactors, nodes);

	long eye[3], target[3], prevEye[3], prevTarget[3];
	std::vector<short> prev[INTERP_CUTSEQ_ACTORS], cur[INTERP_CUTSEQ_ACTORS];
	int blended = 0, live = 0, cuts = 0, actorCuts = 0, halfTurns = 0;
	long hairLag = 0;

	for (int i = 0; i < s.cut->numactors; i++)
	{
		prev[i].assign(3 * s.actor[i].count, 0);
		cur[i].assign(3 * s.actor[i].count, 0);
	}

	for (long f = 0; f < s.frames; f++)
	{
		DecodeFrame(&s, f, eye, target);

		for (int i = 0; i < s.cut->numactors; i++)
			if (s.actor[i].decoded)
				Triples(&s.actor[i], cur[i].data());

		live++;		// the frame that carries the tick

		if (!f)
		{
			live++;
			printf("CUTLOG cut cutseq=%d frame=%ld why=open eye=-1 target=-1 actor=-1 node=-1 "
				"step=-1\n", n, f);
		}
		else
		{
			long es, ts;

			if (InterpCutseqCameraCut(prevEye, eye, prevTarget, target, &es, &ts))
			{
				live++;
				cuts++;
				printf("CUTLOG cut cutseq=%d frame=%ld why=camera eye=%ld target=%ld actor=-1 "
					"node=-1 step=-1\n", n, f, es, ts);
			}
			else
			{
				blended++;

				for (int i = 0; i < s.cut->numactors; i++)
				{
					if (!s.actor[i].decoded)
						continue;

					std::vector<short> out(cur[i].size());
					long rootStep;
					int ht, hn;

					if (!InterpCutseqBlend(prev[i].data(), cur[i].data(), s.actor[i].count, 0.5F,
						out.data(), &rootStep, &ht, &hn))
					{
						actorCuts++;
						printf("CUTLOG cut cutseq=%d frame=%ld why=actor eye=-1 target=-1 actor=%d "
							"node=0 step=%ld\n", n, f, i, rootStep);
					}
					else
					{
						if (ht)
						{
							halfTurns += ht;
							printf("CUTLOG cut cutseq=%d frame=%ld why=halfturn eye=-1 target=-1 "
								"actor=%d node=%d step=%d\n", n, f, i, hn, ht);
						}

						if (!i)
							hairLag = std::max(hairLag, InterpCutseqRootStep(prev[0].data(),
								out.data()));
					}
				}
			}
		}

		memcpy(prevEye, eye, sizeof(eye));
		memcpy(prevTarget, target, sizeof(target));

		for (int i = 0; i < s.cut->numactors; i++)
			prev[i] = cur[i];
	}

	printf("CUTLOG close cutseq=%d frames=%d ticks=%d blended=%d live=%d cuts=%d actor_cuts=%d "
		"halfturns=%d hair=%d eye_rooms=- hair_lag=%ld\n", n, blended + live, s.frames, blended,
		live, cuts, actorCuts, halfTurns, s.frames, hairLag);
	free(data);
	return 0;
}

/* Every cutscene's actors as the file names them: the object slot each
 * actor is drawn from (actor_data[i].objslot) and its node count, with the
 * audio track the cutscene plays, which the HD faces speak. Actor 0 is Lara,
 * -1 (NO_ITEM) where she is not in it. The file's own slots, before
 * stealth3_start (game\deltapak.cpp) puts the slot of the guard beside her
 * on actor 1 of the four stealth3 cutscenes. With `only` above 0, that
 * cutscene alone. */
static int ListActors(const std::vector<char>& file, int only)
{
	printf("cutseq name      frames audio actors  actor:slot/nodes ...\n");

	for (int n = 1; n <= CUT_COUNT; n++)
	{
		if (only > 0 && n != only)
			continue;

		long size;
		char* data = SceneData(file, n, &size);

		if (!data)
		{
			printf("%6d %-9s not in the file\n", n, CUT_NAME[n]);
			continue;
		}

		const NEW_CUTSCENE* cut = (const NEW_CUTSCENE*)data;
		const int actors = cut->numactors;

		printf("%6d %-9s %6d %5ld %6d ", n, CUT_NAME[n], cut->numframes,
			cut->audio_track, actors);

		for (int i = 0; i < actors && i < 10; i++)
			printf(" %d:%d/%d", i, cut->actor_data[i].objslot,
				cut->actor_data[i].nodes);

		printf("\n");
		free(data);
	}

	return 0;
}

int main(int argc, char** argv)
{
	char defaultPath[1024];
	const char* path = default_path(defaultPath, sizeof(defaultPath));
	int argi = 1;

	if (argi < argc && strcmp(argv[argi], "-frames") &&
		strcmp(argv[argi], "-cutlog") && strcmp(argv[argi], "-actors"))
		path = argv[argi++];

	FILE* fp = fopen(path, "rb");

	if (!fp)
	{
		printf("cannot open %s\n", path);
		return 1;
	}

	fseek(fp, 0, SEEK_END);
	std::vector<char> file((size_t)ftell(fp));
	fseek(fp, 0, SEEK_SET);

	if (fread(file.data(), 1, file.size(), fp) != file.size())
	{
		fclose(fp);
		printf("cannot read %s\n", path);
		return 1;
	}

	fclose(fp);

	if (file.size() < 2048 || memcmp(file.data(), "(C) DEL!", 8))
	{
		printf("%s: no cutseq.bin header\n", path);
		return 1;
	}

	if (argi + 3 < argc && !strcmp(argv[argi], "-frames"))
		return DumpFrames(file, atoi(argv[argi + 1]), atol(argv[argi + 2]), atol(argv[argi + 3]));

	if (argi + 1 < argc && !strcmp(argv[argi], "-cutlog"))
		return SimulateLog(file, atoi(argv[argi + 1]));

	if (argi < argc && !strcmp(argv[argi], "-actors"))
		return ListActors(file, argi + 1 < argc ? atoi(argv[argi + 1]) : 0);

	printf("%s: %zu bytes\n", path, file.size());

	Totals t = {};
	std::vector<FastStep> fast;

	t.residualMin = LONG_MAX;
	t.rootAloneMin = LONG_MAX;
	t.residualMax = LONG_MIN;

	for (int n = 1; n <= CUT_COUNT; n++)
	{
		long size;
		char* data = SceneData(file, n, &size);

		if (!data)
		{
			printf("cutseq %2d %-9s not in the file\n", n, CUT_NAME[n]);
			continue;
		}

		ProbeScene(data, n, &t, &fast);
		free(data);
	}

	std::sort(fast.begin(), fast.end(), [](const FastStep& x, const FastStep& y)
		{ return x.step > y.step; });
	std::sort(t.rootSteps.begin(), t.rootSteps.end(), [](const FastStep& x, const FastStep& y)
		{ return x.step > y.step; });

	char freeList[256] = "";

	for (int k = 0; k < t.nCutFree; k++)
	{
		char one[8];
		sprintf(one, "%s%d", k ? "," : "", t.cutFree[k]);
		strcat(freeList, one);
	}

	printf("\nTOTAL cutscenes=%ld frames=%ld steps=%ld\n", t.scenes, t.frames, t.steps);
	printf("TOTAL camera: cuts=%ld (%.2f%% of steps) in %ld cutscenes, without a cut: %s\n",
		t.cuts, t.steps ? 100.0 * t.cuts / t.steps : 0.0, t.cutScenes, freeList);
	printf("TOTAL camera: cuts at or under INTERP_CAM_SNAP (%ld), which that rule would blend: %ld\n",
		INTERP_CAM_SNAP, t.camMidJumps);
	printf("TOTAL camera: largest cut cutseq %d frame %ld eye=%ld target=%ld\n", t.maxCutScene,
		t.maxCutFrame, t.maxCutEye, t.maxCutTarget);
	printf("TOTAL camera: p99 eye=%ld target=%ld larger-of-the-two=%ld; p50 larger=%ld\n",
		Percentile(t.eyeSteps, 0.99), Percentile(t.targetSteps, 0.99),
		Percentile(t.camSteps, 0.99), Percentile(t.camSteps, 0.50));
	printf("TOTAL camera: largest steps under the snap:");

	for (size_t k = 0; k < fast.size() && k < 8; k++)
		printf(" %d@%ld(%ld)", fast[k].scene, fast[k].frame, fast[k].step);

	printf("\nTOTAL camera tracks: words left after the last frame %ld..%ld\n", t.residualMin,
		t.residualMax);
	printf("TOTAL actors: root jumps above %ld (drawn units)=%ld, of them without a camera cut=%ld; "
		"above %ld in node units=%ld; largest root step=%ld\n", INTERP_CUTSEQ_ROOT_SNAP,
		t.rootJumps, t.rootJumpsNoCut, INTERP_CUTSEQ_ROOT_SNAP, t.rootJumpsRaw, t.rootMax);
	printf("TOTAL actors: root steps above 256 standing alone (a jump) under the snap=%ld "
		"(without a camera cut=%ld), with a neighbour (motion) over it=%ld; smallest jump=%ld, "
		"largest motion=%ld\n", t.rootJumpsUnder, t.rootJumpsUnderNoCut, t.rootMotionOver,
		t.rootAloneMin, t.rootMotionMax);
	printf("TOTAL actors: largest root steps under the snap (cutseq@frame/actor(step)):");

	for (size_t k = 0; k < t.rootSteps.size() && k < 8; k++)
		printf(" %d@%ld/a%d(%ld)", t.rootSteps[k].scene, t.rootSteps[k].frame,
			t.rootSteps[k].actor, t.rootSteps[k].step);

	printf("\n");
	printf("TOTAL actors: node-axis steps=%ld exact half turns=%ld on %ld node steps "
		"(%ld with more than one axis); steps of 384..511=%ld, 256..383=%ld\n",
		t.axisSteps, t.halfAxes, t.halfNodes, t.halfMulti, t.nearHalf, t.quarter);
	printf("TOTAL actors: at most %d actors, %d nodes per actor, %d nodes per cutscene; "
		"angle keys outside 0..1023=%ld\n", t.maxActors, t.maxActorNodes, t.maxSceneNodes,
		t.keysWide);
	printf("TOTAL actors: node steps whose per-axis blend strays from the shortest arc by more "
		"than 10 degrees=%ld, 30 degrees=%ld (on a turn under 45 degrees=%ld, with an axis step of 384 or more=%ld); largest=%.1f\n",
		t.detour10, t.detour30, t.flipLike, t.detourWide, t.detourMax);
	printf("TOTAL Lara: blended ticks=%ld, of them with the braid more than 32 units behind her "
		"root on the frame between=%ld\n", t.laraTicks, t.laraLag32);
	return 0;
}
