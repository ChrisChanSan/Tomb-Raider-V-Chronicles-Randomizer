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

/* LipsTest - the lips of the cutscene faces, checked without a game or a
 * device. Build with blips.bat.
 *
 *     LipsTest [<obj folder> [<block folder>]]
 *
 * It compiles the engine's own specific\hd\hd_trm.cpp (the head reader),
 * specific\hd\hd_trl.cpp (the track reader, the map and the deform) and
 * specific\hd\hd_install.cpp (where the remaster is) beside itself, so there
 * is no twin to keep in step, and reads the installed remaster's 4\ITEM and
 * 5\ITEM through the same path the engine resolves - read only, the .TRM and
 * .TRL files and nothing else.
 *
 * Where the deform comes from is written at the top of hd_trl.h; the map is
 * the face's own header (hd_trm.h, HDTrmModel::rigBind).
 *
 * What it checks:
 *
 *   1. the map of every rigged face of 5\ITEM and LARA_HEAD: how many joints
 *      its columns drive, which joint is the head, which axis order - and
 *      ZIP_HEAD refused.
 *   2. the bind comes back: frame 0 of a face's own block deformed through
 *      the map returns every vertex to itself and every normal byte to
 *      within one.
 *   3. three wide-open frames against their numbers: the largest distance a
 *      vertex moves, the jaw's turn and the chin's travel, and the throat -
 *      Lara's vertices weighted to her body keep that share where the file
 *      puts it.
 *   4. with an <obj folder>: the deformed positions against an independent
 *      implementation of the same arithmetic, written out as .OBJ files named
 *      <FACE>_<track>_bind.obj, _mid<frame>.obj and _open<frame>.obj, to
 *      within 0.01 units a vertex. Without the folder the check is skipped
 *      and says so.
 *   5. the frame rule over every <FACE>_HEAD_<track>.TRL of 5\ITEM.
 *   6. the end of a clip: ten frames of ease, then the bind.
 *   7. the loop between lines: where each face's block lies in its .TRM and
 *      where it loops; the block read from there against its frame 0 and,
 *      with a <block folder>, against <FACE>_block.TRL exports of the same
 *      block written by an independent reader, frame by frame; the loop
 *      mask - the columns outside it stand still over the loop, the
 *      vertices on no masked joint stay bit for bit over every loop frame,
 *      a full mask is no mask - and the blinks the loop starts.
 *   8. the firing face: the ease in and out, the blend from the loop's frame,
 *      and the firing frame's mouth against its numbers.
 *   9. a face of another geometry on the same rig: which faces HDLipsSameRig
 *      takes for one rig and which it refuses, a copy refused for one byte
 *      of each part it compares; the folder a mod's face speaks from
 *      (HDLipsModRig, HDLipsTrackFiles - what make_lips decides by) with
 *      LARA_YOUNG_HEAD standing in for a mod's face: game 5's ITEM without
 *      track files of its own, its own folder with them, beside an install
 *      face of another rig or none; and the tracks of two cutscenes Lara
 *      acts in, read out of the game's data\cutseq.bin - cutscene 8 (track
 *      102) and 32 (track 120) - opened in that folder and driving
 *      LARA_YOUNG_HEAD's mouth, whose geometry is not LARA_HEAD's, while
 *      the bind returns it to itself.
 *
 * Besides the checks it prints what the comments in hd_trl.cpp and hd_trl.h
 * quote: how far each face's composed bind stands from its rig, the scale the
 * joints carry, how far a plain turn would leave the normals, and how far each
 * clip's last frame stands from the bind.
 *
 * The last line is "<n> checks, <m> failed".
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_trm.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_trl.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_install.h"

/* hd_install.cpp reads the two registry overrides out of these, which the
 * engine defines in tomb5\tomb5.cpp; a tool that links it brings its own pair,
 * as ModelProbe does. Left empty, so the search runs the way it does for a
 * player who never set the registry value. */
char tomb5_hd_install[260];
char tomb5_hd_install_tr3[260];

static int g_fail = 0;
static int g_checks = 0;

static void check(bool ok, const char* format, ...)
{
	va_list args;

	g_checks++;

	if (ok)
		return;

	g_fail++;
	printf("FAIL ");
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	printf("\n");
}

static std::string g_root5;	// <bundle>\5
static std::string g_bundle;

/* ------------------------------------------------------------ a face -- */

struct Face
{
	std::string stem;
	std::string from;	// "4" or "5"
	HDTrmModel trm;
	HDLipsMap map;
	bool loaded = false;
	bool mapped = false;
	const char* why = "";
	std::vector<float> position;
	std::vector<unsigned char> normal;
	std::vector<double> skins;
};

static std::map<std::string, Face*> g_faces;

static Face* face(const std::string& stem)
{
	std::map<std::string, Face*>::iterator at = g_faces.find(stem);

	if (at != g_faces.end())
		return at->second;

	Face* f = new Face();

	f->stem = stem;
	memset(&f->trm, 0, sizeof(f->trm));
	g_faces[stem] = f;

	/* Game 5 first, then its sibling game 4 - the order the engine's model
	 * search takes (find_model, hd_model.cpp). */
	const char* games[2] = { "5", "4" };

	for (int g = 0; g < 2 && !f->loaded; g++)
	{
		const std::string path = g_bundle + "\\" + games[g] + "\\ITEM\\" + stem + ".TRM";
		const char* why = "";

		if (HDTrmExists(path.c_str()) && HDTrmLoad(path.c_str(), &f->trm, &why))
		{
			f->loaded = true;
			f->from = games[g];
		}
	}

	if (!f->loaded)
		return f;

	f->mapped = HDLipsMapBuild(&f->trm, stem.c_str(), &f->map, &f->why);

	f->position.resize((size_t)f->trm.vertices * 3);
	f->normal.resize((size_t)f->trm.vertices * 3);

	for (int i = 0; i < f->trm.vertices; i++)
	{
		f->position[(size_t)i * 3 + 0] = f->trm.vertex[i].x;
		f->position[(size_t)i * 3 + 1] = f->trm.vertex[i].y;
		f->position[(size_t)i * 3 + 2] = f->trm.vertex[i].z;
		f->normal[(size_t)i * 3 + 0] = f->trm.vertex[i].nx;
		f->normal[(size_t)i * 3 + 1] = f->trm.vertex[i].ny;
		f->normal[(size_t)i * 3 + 2] = f->trm.vertex[i].nz;
	}

	f->skins.resize((size_t)(f->trm.rigJoints > 0 ? f->trm.rigJoints : 1) * 12);
	return f;
}

struct Result
{
	std::vector<float> position;
	std::vector<unsigned char> normal;
	double farthest = 0;
	double mean = 0;
	int overOne = 0;
};

// One frame of a face through the engine's own map and deform.
static void deform(Face* f, const float* frame, double amount, Result& out)
{
	const int n = f->trm.vertices;

	out.position.assign((size_t)n * 3, 0.0f);
	out.normal.assign((size_t)n * 3, 0);

	HDLipsSkins(&f->map, frame, amount, &f->skins[0]);
	out.farthest = HDLipsDeform(&f->skins[0], f->map.rigJoints, n,
		&f->position[0], &f->normal[0], f->trm.ownBone, f->trm.ownWeight,
		&out.position[0], &out.normal[0]);

	double sum = 0;

	out.overOne = 0;

	for (int i = 0; i < n; i++)
	{
		const double dx = out.position[(size_t)i * 3 + 0] - f->position[(size_t)i * 3 + 0];
		const double dy = out.position[(size_t)i * 3 + 1] - f->position[(size_t)i * 3 + 1];
		const double dz = out.position[(size_t)i * 3 + 2] - f->position[(size_t)i * 3 + 2];
		const double d = sqrt(dx * dx + dy * dy + dz * dz);

		sum += d;

		if (d > 1.0)
			out.overOne++;
	}

	out.mean = n ? sum / n : 0.0;
}

static std::string trl_path(const std::string& stem, int track)
{
	char name[128];

	snprintf(name, sizeof(name), "%s_%d.TRL", stem.c_str(), track);
	return g_root5 + "\\ITEM\\" + name;
}

// A frame of a track file, or empty.
static std::vector<float> read_frame(Face* f, int track, int frame, int* frames = 0)
{
	HDTrlClip clip;
	const char* why = "";
	std::vector<float> out;

	if (!HDTrlOpen(trl_path(f->stem, track).c_str(), f->map.columns, &clip, &why))
		return out;

	out.resize((size_t)f->map.columns * 12);

	if (frames)
		*frames = clip.frames;

	if (!HDTrlReadFrame(&clip, frame, &out[0]))
		out.clear();

	HDTrlClose(&clip);
	return out;
}

/* The turn of a 3x4 block with its scale taken out, in degrees. */
static double turn_degrees(const double* m)
{
	const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
		m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
	const double s = cbrt(fabs(det));

	if (s <= 0)
		return 0;

	double c = ((m[0] + m[5] + m[10]) / s - 1.0) / 2.0;

	c = c < -1 ? -1 : c > 1 ? 1 : c;
	return acos(c) * 180.0 / 3.14159265358979323846;
}

static double det3(const double* m)
{
	return m[0] * (m[5] * m[10] - m[6] * m[9]) -
		m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
}

// a * b of two 3x4 blocks.
static void mul34(const double* a, const double* b, double* out)
{
	for (int r = 0; r < 3; r++)
	{
		for (int c = 0; c < 4; c++)
			out[r * 4 + c] = a[r * 4 + 0] * b[0 * 4 + c] + a[r * 4 + 1] * b[1 * 4 + c] +
				a[r * 4 + 2] * b[2 * 4 + c];

		out[r * 4 + 3] += a[r * 4 + 3];
	}
}

/* ------------------------------------------------------- the sections -- */

static void section_map()
{
	printf("\n-- 1. the map ------------------------------------------------------\n");

	struct Want
	{
		const char* stem;
		int driven;
		int rig;
		int head;
		const char* axis;
	};

	const Want want[] =
	{
		{ "LARA_HEAD", 26, 33, 4, "identity" },
		{ "ADMIRAL_HEAD", 25, 27, 0, "identity" },
		{ "BLUE_GUARD_HEAD", 26, 28, 1, "identity" },
		{ "DEMON_HEAD", 26, 28, 1, "identity" },
		{ "HANGMAN_HEAD", 5, 8, 2, "(x,-y,-z)" },
		{ "LARSON_HEAD", 26, 41, 0, "identity" },
		{ "PATRICK_BROWN_HEAD", 26, 28, 0, "identity" },
		{ "PATRICK_WHITE_HEAD", 26, 28, 0, "identity" },
		{ "PIERRE_HEAD", 26, 41, 0, "identity" },
		{ "SERGIE_HEAD", 25, 27, 0, "(x,-z,y)" },
		{ "VON_CROY_ELDER_HEAD", 26, 28, 1, "identity" },
		{ "VON_CROY_SENIOR_HEAD", 26, 29, 1, "identity" },
	};

	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
	{
		Face* f = face(want[i].stem);

		check(f->loaded, "%s does not load", want[i].stem);

		if (!f->loaded)
			continue;

		check(f->mapped, "%s is refused: %s", want[i].stem, f->why);
		check(f->map.driven == want[i].driven && f->map.rigJoints == want[i].rig,
			"%s drives %d of %d joints, %d of %d wanted", want[i].stem,
			f->map.driven, f->map.rigJoints, want[i].driven, want[i].rig);
		check(f->map.headJoint == want[i].head, "%s's head joint is %d, %d wanted",
			want[i].stem, f->map.headJoint, want[i].head);
		check(!strcmp(HDLipsAxisName(f->map.axis), want[i].axis),
			"%s's axis is %s, %s wanted", want[i].stem,
			HDLipsAxisName(f->map.axis), want[i].axis);

		/* How far the composed bind of each driven column stands from the
		 * rig's own joint, both in the vertices' axes. A measurement for the
		 * comments, not a check: LARSON_HEAD's frame 0 is not its rig. */
		double worst = 0, sum = 0;
		int count = 0;

		if (f->mapped)
		{
			double bind[HD_LIPS_MAX_COLUMNS * 12];
			const int ax = f->map.axis;

			HDLipsCompose(&f->map, f->trm.blockBind, bind);

			for (int c = 0; c < f->map.columns; c++)
			{
				const int j = f->map.joint[c];

				if (j < 0)
					continue;

				const double* t = bind + c * 12;
				double v[3] = { t[3], t[7], t[11] };

				// The axis, as HDLipsSkins applies it.
				if (ax == HD_LIPS_AXIS_X_NZ_Y)
				{
					const double y = v[1], z = v[2];

					v[1] = -z;
					v[2] = y;
				}
				else if (ax == HD_LIPS_AXIS_X_NY_NZ)
				{
					v[1] = -v[1];
					v[2] = -v[2];
				}

				const float* r = f->trm.rigJoint + j * 3;
				const double joint[3] = { r[0], -r[2], r[1] };
				const double dx = f->map.head[0] + v[0] - joint[0];
				const double dy = f->map.head[1] + v[1] - joint[1];
				const double dz = f->map.head[2] + v[2] - joint[2];
				const double d = sqrt(dx * dx + dy * dy + dz * dz);

				worst = d > worst ? d : worst;
				sum += d;
				count++;
			}
		}

		printf("%-22s from %s  rig %2d  columns %2d  driven %2d  head joint %d at "
			"(%.0f, %.0f, %.0f)  axis %-9s  bind to rig: mean %.4f max %.4f units\n",
			want[i].stem, f->from.c_str(), f->map.rigJoints, f->map.columns,
			f->map.driven, f->map.headJoint, f->map.head[0], f->map.head[1],
			f->map.head[2], HDLipsAxisName(f->map.axis), count ? sum / count : 0.0, worst);
	}

	Face* zip = face("ZIP_HEAD");

	check(zip->loaded && !zip->mapped, "ZIP_HEAD is not refused");
	printf("%-22s refused: %s (driven %d of %d)\n", "ZIP_HEAD", zip->why,
		zip->map.driven, zip->map.rigJoints);

	Face* lara = face("LARA_HEAD");

	check(fabs(lara->map.head[0] - 1) < 1 && fabs(lara->map.head[1] + 247) < 1 &&
		fabs(lara->map.head[2] + 12) < 1,
		"LARA_HEAD's head joint stands at (%.1f, %.1f, %.1f), (1, -247, -12) wanted",
		lara->map.head[0], lara->map.head[1], lara->map.head[2]);
}

static void section_bind()
{
	printf("\n-- 2. the bind comes back -----------------------------------------\n");

	const char* stems[] =
	{
		"LARA_HEAD", "PIERRE_HEAD", "HANGMAN_HEAD", "LARSON_HEAD", "ADMIRAL_HEAD",
		"SERGIE_HEAD", "DEMON_HEAD", "PATRICK_WHITE_HEAD", "PATRICK_BROWN_HEAD",
		"BLUE_GUARD_HEAD", "VON_CROY_ELDER_HEAD", "VON_CROY_SENIOR_HEAD"
	};

	for (size_t s = 0; s < sizeof(stems) / sizeof(stems[0]); s++)
	{
		Face* f = face(stems[s]);

		if (!f->mapped)
			continue;

		Result r;
		int normalOff = 0;

		deform(f, f->trm.blockBind, 1.0, r);

		for (size_t i = 0; i < r.normal.size(); i++)
		{
			const int d = abs((int)r.normal[i] - (int)f->normal[i]);

			normalOff = d > normalOff ? d : normalOff;
		}

		printf("%-22s frame 0 of the block: farthest vertex %.2e units, "
			"normal bytes off by at most %d\n", stems[s], r.farthest, normalOff);

		check(r.farthest < 1e-3, "%s's bind moves a vertex %.6f units", stems[s],
			r.farthest);
		check(normalOff <= 1, "%s's bind moves a normal byte by %d", stems[s],
			normalOff);
	}
}

static void section_open()
{
	printf("\n-- 3. wide open ----------------------------------------------------\n");

	struct Want
	{
		const char* stem;
		int track;
		int frame;
		double farthest;
	};

	const Want want[] =
	{
		{ "LARA_HEAD", 98, 556, 7.19 },
		{ "PIERRE_HEAD", 108, 1107, 8.18 },
		{ "LARSON_HEAD", 102, 978, 12.03 },
	};

	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
	{
		Face* f = face(want[i].stem);
		const std::vector<float> frame = read_frame(f, want[i].track, want[i].frame);

		check(!frame.empty(), "%s_%d.TRL frame %d does not read", want[i].stem,
			want[i].track, want[i].frame);

		if (frame.empty())
			continue;

		Result r;

		deform(f, &frame[0], 1.0, r);

		/* The jaw: the driven column that hangs off the head and carries the
		 * most weight; the chin: the driven column that hangs off the jaw. */
		std::vector<double> weight((size_t)f->map.rigJoints, 0.0);

		for (int v = 0; v < f->trm.vertices; v++)
			for (int k = 0; k < 3; k++)
				if (f->trm.ownBone[v * 3 + k] < f->map.rigJoints)
					weight[f->trm.ownBone[v * 3 + k]] += f->trm.ownWeight[v * 3 + k];

		int jaw = -1;

		for (int c = 0; c < f->map.columns; c++)
			if (f->map.joint[c] >= 0 && f->map.parent[c] < 0 &&
				(jaw < 0 || weight[f->map.joint[c]] > weight[f->map.joint[jaw]]))
				jaw = c;

		double global[HD_LIPS_MAX_COLUMNS * 12], bind[HD_LIPS_MAX_COLUMNS * 12];

		HDLipsCompose(&f->map, &frame[0], global);
		HDLipsCompose(&f->map, f->trm.blockBind, bind);

		double d[12];

		mul34(global + jaw * 12, f->map.bindInverse[jaw], d);

		double chin = 0;

		for (int c = 0; c < f->map.columns; c++)
			if (f->map.joint[c] >= 0 && f->map.parent[c] == jaw)
			{
				const double dx = global[c * 12 + 3] - bind[c * 12 + 3];
				const double dy = global[c * 12 + 7] - bind[c * 12 + 7];
				const double dz = global[c * 12 + 11] - bind[c * 12 + 11];

				chin = std::max(chin, sqrt(dx * dx + dy * dy + dz * dz));
			}

		printf("%s_%d.TRL frame %d: farthest %.2f units (%.2f wanted), mean %.2f, "
			"%d vertices past 1 unit; jaw joint %d turns %.1f degrees, chin travels "
			"%.2f units\n", want[i].stem, want[i].track, want[i].frame, r.farthest,
			want[i].farthest, r.mean, r.overOne, f->map.joint[jaw], turn_degrees(d),
			chin);

		check(fabs(r.farthest - want[i].farthest) <= 0.05,
			"%s_%d.TRL frame %d moves a vertex %.3f units, %.2f wanted",
			want[i].stem, want[i].track, want[i].frame, r.farthest, want[i].farthest);

		if (!strcmp(want[i].stem, "LARA_HEAD"))
		{
			check(f->map.joint[jaw] == 18, "LARA_HEAD's jaw is joint %d, 18 wanted",
				f->map.joint[jaw]);
			check(fabs(turn_degrees(d) - 10.3) <= 0.05,
				"LARA_HEAD's jaw turns %.2f degrees, 10.3 wanted", turn_degrees(d));
			check(fabs(chin - 4.30) <= 0.05, "LARA_HEAD's chin travels %.3f, 4.30 wanted",
				chin);
			check(fabs(r.mean - 1.68) <= 0.01 && abs(r.overOne - 2198) <= 2,
				"LARA_HEAD's mean %.3f and %d past a unit, 1.68 and 2198 wanted",
				r.mean, r.overOne);

			/* The throat. Every joint no column drives keeps the identity,
			 * a vertex weighted only to such joints does not move by a bit,
			 * and the vertices that carry weight on her BODY joints (not the
			 * head, not a driven one) move by their face share alone. */
			bool identities = true;

			for (int j = 0; j < f->map.rigJoints; j++)
			{
				bool driven = false;

				for (int c = 0; c < f->map.columns; c++)
					driven = driven || f->map.joint[c] == j;

				if (driven)
					continue;

				const double* s = &f->skins[(size_t)j * 12];
				const double one[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };

				identities = identities && !memcmp(s, one, sizeof(one));
			}

			check(identities, "a joint no column drives does not keep the identity");

			int still = 0, stillMoved = 0, body = 0, bodyOverOne = 0;
			double bodyFarthest = 0, bodyShareOff = 0;

			for (int v = 0; v < f->trm.vertices; v++)
			{
				bool onDriven = false, onBody = false;

				for (int k = 0; k < 3; k++)
				{
					const int j = f->trm.ownBone[v * 3 + k];

					if (!f->trm.ownWeight[v * 3 + k])
						continue;

					bool driven = false;

					for (int c = 0; c < f->map.columns; c++)
						driven = driven || f->map.joint[c] == j;

					onDriven = onDriven || driven;
					onBody = onBody || (!driven && j != f->map.headJoint);
				}

				const float* p = &f->position[(size_t)v * 3];
				const float* q = &r.position[(size_t)v * 3];
				const double dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
				const double moved = sqrt(dx * dx + dy * dy + dz * dz);

				if (!onDriven)
				{
					still++;

					if (memcmp(p, q, sizeof(float) * 3) != 0)
						stillMoved++;
				}

				if (!onBody)
					continue;

				body++;
				bodyFarthest = std::max(bodyFarthest, moved);

				if (moved > 1.0)
					bodyOverOne++;

				/* The face share alone: sum over the driven bones of
				 * weight/255 * (S p - p). */
				double e[3] = { 0, 0, 0 };

				for (int k = 0; k < 3; k++)
				{
					const int j = f->trm.ownBone[v * 3 + k];
					const double w = f->trm.ownWeight[v * 3 + k] / 255.0;
					const double* s = &f->skins[(size_t)j * 12];

					for (int row = 0; row < 3; row++)
						e[row] += w * (s[row * 4 + 0] * p[0] + s[row * 4 + 1] * p[1] +
							s[row * 4 + 2] * p[2] + s[row * 4 + 3] - p[row]);
				}

				bodyShareOff = std::max(bodyShareOff, std::max(fabs(e[0] - dx),
					std::max(fabs(e[1] - dy), fabs(e[2] - dz))));
			}

			printf("LARA_HEAD throat: %d vertices carry body weight, %d of them move "
				"more than a unit, the farthest %.2f; %d vertices on no driven joint, "
				"%d of them moved; face share against the move %.1e units\n",
				body, bodyOverOne, bodyFarthest, still, stillMoved, bodyShareOff);

			check(body == 176, "%d vertices carry body weight, 176 wanted", body);
			check(bodyOverOne == 61 && fabs(bodyFarthest - 6.81) <= 0.05,
				"%d throat vertices move past a unit, farthest %.3f; 61 and 6.81 wanted",
				bodyOverOne, bodyFarthest);
			check(stillMoved == 0, "%d vertices on no driven joint moved", stillMoved);
			check(bodyShareOff < 1e-3, "the throat moves %.2e units beyond its face "
				"share", bodyShareOff);
		}
	}

	/* What the comments quote: the scale the joints carry and how far a plain
	 * turn of the normals would be off, over every frame of LARA_HEAD_98. */
	Face* f = face("LARA_HEAD");
	int frames = 0;

	read_frame(f, 98, 0, &frames);

	HDTrlClip clip;
	const char* why = "";
	double bindDetLo = 1e9, bindDetHi = 0, detLo = 1e9, detHi = 0, plainOff = 0;
	int plainFrame = -1;

	{
		double bind[HD_LIPS_MAX_COLUMNS * 12];

		HDLipsCompose(&f->map, f->trm.blockBind, bind);

		for (int c = 0; c < f->map.columns; c++)
		{
			bindDetLo = std::min(bindDetLo, det3(bind + c * 12));
			bindDetHi = std::max(bindDetHi, det3(bind + c * 12));
		}
	}

	if (HDTrlOpen(trl_path("LARA_HEAD", 98).c_str(), f->map.columns, &clip, &why))
	{
		std::vector<float> frame((size_t)f->map.columns * 12);
		Result r;

		for (int at = 0; at < clip.frames; at++)
		{
			HDTrlReadFrame(&clip, at, &frame[0]);
			deform(f, &frame[0], 1.0, r);

			for (int j = 0; j < f->map.rigJoints; j++)
			{
				const double det = det3(&f->skins[(size_t)j * 12]);

				detLo = std::min(detLo, det);
				detHi = std::max(detHi, det);
			}

			for (int v = 0; v < f->trm.vertices; v++)
			{
				double m[12] = { 0 };

				for (int k = 0; k < 3; k++)
				{
					const double w = f->trm.ownWeight[v * 3 + k] / 255.0;
					const double* s = &f->skins[(size_t)f->trm.ownBone[v * 3 + k] * 12];

					for (int e = 0; e < 12; e++)
						m[e] += w * s[e];
				}

				const double n[3] =
				{
					(f->normal[(size_t)v * 3 + 0] - 127.0) / 128.0,
					(f->normal[(size_t)v * 3 + 1] - 127.0) / 128.0,
					(f->normal[(size_t)v * 3 + 2] - 127.0) / 128.0
				};
				const double plain[3] =
				{
					m[0] * n[0] + m[1] * n[1] + m[2] * n[2],
					m[4] * n[0] + m[5] * n[1] + m[6] * n[2],
					m[8] * n[0] + m[9] * n[1] + m[10] * n[2]
				};
				const double engine[3] =
				{
					(r.normal[(size_t)v * 3 + 0] - 127.0) / 128.0,
					(r.normal[(size_t)v * 3 + 1] - 127.0) / 128.0,
					(r.normal[(size_t)v * 3 + 2] - 127.0) / 128.0
				};
				const double lp = sqrt(plain[0] * plain[0] + plain[1] * plain[1] + plain[2] * plain[2]);
				const double le = sqrt(engine[0] * engine[0] + engine[1] * engine[1] + engine[2] * engine[2]);

				if (lp <= 0 || le <= 0)
					continue;

				double c = (plain[0] * engine[0] + plain[1] * engine[1] + plain[2] * engine[2]) / (lp * le);

				c = c < -1 ? -1 : c > 1 ? 1 : c;

				const double off = acos(c) * 180.0 / 3.14159265358979323846;

				if (off > plainOff)
				{
					plainOff = off;
					plainFrame = at;
				}
			}
		}

		HDTrlClose(&clip);
	}

	printf("LARA_HEAD: the composed bind's determinants %.3f..%.3f; over the %d frames "
		"of LARA_HEAD_98.TRL the skins' %.3f..%.3f, and a plain turn of the normals "
		"is up to %.1f degrees off the cofactor's (frame %d)\n", bindDetLo, bindDetHi,
		frames, detLo, detHi, plainOff, plainFrame);
}

static void section_obj(const char* folder)
{
	printf("\n-- 4. against an independent implementation ------------------------\n");

	if (!folder)
	{
		printf("skipped - no <obj folder> on the command line\n");
		return;
	}

	std::vector<std::string> names;

	for (const std::filesystem::directory_entry& e : std::filesystem::directory_iterator(folder))
		if (e.path().extension() == ".obj")
			names.push_back(e.path().filename().string());

	std::sort(names.begin(), names.end());

	int files = 0;

	for (size_t i = 0; i < names.size(); i++)
	{
		/* <FACE>_<track>_<tag>.obj, the tag bind, mid<frame> or open<frame>;
		 * anything else in the folder is not this check's. */
		const std::string name = names[i].substr(0, names[i].size() - 4);
		const size_t tagAt = name.find_last_of('_');

		if (tagAt == std::string::npos)
			continue;

		const std::string tag = name.substr(tagAt + 1);
		const std::string rest = name.substr(0, tagAt);
		const size_t trackAt = rest.find_last_of('_');

		if (trackAt == std::string::npos)
			continue;

		const std::string stem = rest.substr(0, trackAt);
		const int track = atoi(rest.c_str() + trackAt + 1);
		int frame = -1;

		if (tag == "bind")
			frame = -1;
		else if (tag.compare(0, 3, "mid") == 0 && tag.size() > 3 &&
			tag.find_first_not_of("0123456789", 3) == std::string::npos)
			frame = atoi(tag.c_str() + 3);
		else if (tag.compare(0, 4, "open") == 0 && tag.size() > 4 &&
			tag.find_first_not_of("0123456789", 4) == std::string::npos)
			frame = atoi(tag.c_str() + 4);
		else
			continue;

		Face* f = face(stem);

		if (!f->mapped)
		{
			check(false, "%s: the face %s is not mapped", names[i].c_str(), stem.c_str());
			continue;
		}

		std::vector<float> clip;

		if (frame >= 0)
		{
			clip = read_frame(f, track, frame);

			if (clip.empty())
			{
				check(false, "%s: %s_%d.TRL frame %d does not read", names[i].c_str(),
					stem.c_str(), track, frame);
				continue;
			}
		}

		Result r;

		deform(f, frame >= 0 ? &clip[0] : f->trm.blockBind, 1.0, r);

		FILE* file = fopen((std::string(folder) + "\\" + names[i]).c_str(), "r");
		char line[256];
		int v = 0;
		double worst = 0;

		while (file && fgets(line, sizeof(line), file))
		{
			double x, y, z;

			if (line[0] != 'v' || line[1] != ' ' || sscanf(line + 2, "%lf %lf %lf", &x, &y, &z) != 3)
				continue;

			if (v < f->trm.vertices)
			{
				const double dx = x - r.position[(size_t)v * 3 + 0];
				const double dy = y - r.position[(size_t)v * 3 + 1];
				const double dz = z - r.position[(size_t)v * 3 + 2];

				worst = std::max(worst, sqrt(dx * dx + dy * dy + dz * dz));
			}

			v++;
		}

		if (file)
			fclose(file);

		files++;
		printf("%-34s %5d vertices, farthest %.2f units from the bind, engine against "
			"the file at most %.4f units apart\n", names[i].c_str(), v, r.farthest, worst);

		check(v == f->trm.vertices, "%s holds %d vertices, the face %d", names[i].c_str(),
			v, f->trm.vertices);
		check(worst <= 0.01, "%s: a vertex lies %.4f units from the engine's",
			names[i].c_str(), worst);
	}

	check(files > 0, "no .obj file of the form <FACE>_<track>_<tag>.obj in %s", folder);
}

static void section_frames()
{
	printf("\n-- 5. the frame rule --------------------------------------------------\n");

	std::vector<std::string> names;

	for (const std::filesystem::directory_entry& e :
		std::filesystem::directory_iterator(g_root5 + "\\ITEM"))
	{
		const std::string n = e.path().filename().string();

		if (n.size() > 4 && !_stricmp(n.c_str() + n.size() - 4, ".TRL") &&
			n.find("_HEAD_") != std::string::npos)
			names.push_back(n);
	}

	std::sort(names.begin(), names.end());

	int opened = 0, low = 0, of40 = 0;
	double lastWorst = 0;
	std::string lastWorstName;
	std::vector<std::string> lowStems;

	for (size_t i = 0; i < names.size(); i++)
	{
		const std::string name = names[i].substr(0, names[i].size() - 4);
		const size_t at = name.find_last_of('_');
		const std::string stem = name.substr(0, at);
		const int track = atoi(name.c_str() + at + 1);
		Face* f = face(stem);
		HDTrlClip clip;
		const char* why = "";

		if (!f->loaded)
		{
			check(false, "%s: no face %s.TRM", names[i].c_str(), stem.c_str());
			continue;
		}

		const bool ok = HDTrlOpen(trl_path(stem, track).c_str(), f->trm.blockColumns,
			&clip, &why);

		check(ok, "%s does not open at %d columns: %s", names[i].c_str(),
			f->trm.blockColumns, why);

		if (!ok)
			continue;

		opened++;

		if (clip.lowWord)
		{
			low++;

			if (std::find(lowStems.begin(), lowStems.end(), stem) == lowStems.end())
				lowStems.push_back(stem);
		}

		if (clip.columns == 40)
			of40++;

		/* How far the clip's last frame leaves the face from its bind - the
		 * step the ease at the end of a clip smooths. */
		double last = -1;

		if (f->mapped)
		{
			std::vector<float> frame((size_t)clip.columns * 12);
			Result r;

			HDTrlReadFrame(&clip, clip.frames - 1, &frame[0]);
			deform(f, &frame[0], 1.0, r);
			last = r.farthest;

			if (last > lastWorst)
			{
				lastWorst = last;
				lastWorstName = names[i];
			}
		}

		printf("%-28s word 0x%08X  %4d frames%s  %2d columns  %6.1f s  last frame "
			"%s%.2f units from the bind\n", names[i].c_str(), clip.word, clip.frames,
			clip.lowWord ? " (low word)" : "           ", clip.columns,
			clip.frames / 30.0, last < 0 ? "(refused) " : "", last < 0 ? 0.0 : last);

		if (names[i] == "ADMIRAL_HEAD_113.TRL")
			check(clip.frames == 2196 && clip.lowWord,
				"ADMIRAL_HEAD_113.TRL reads %d frames, 2196 by the low word wanted",
				clip.frames);

		if (names[i] == "LARA_HEAD_98.TRL")
			check(clip.frames == 1292 && !clip.lowWord,
				"LARA_HEAD_98.TRL reads %d frames, 1292 by the whole word wanted",
				clip.frames);

		if (names[i] == "HANGMAN_HEAD_132.TRL")
			check(clip.frames == 3859 && clip.columns == 7,
				"HANGMAN_HEAD_132.TRL reads %d frames of %d columns, 3859 of 7 wanted",
				clip.frames, clip.columns);

		/* A frame read past the end is the last one. */
		if (names[i] == "LARA_HEAD_98.TRL")
		{
			std::vector<float> a((size_t)clip.columns * 12), b((size_t)clip.columns * 12);

			HDTrlReadFrame(&clip, clip.frames - 1, &a[0]);
			HDTrlReadFrame(&clip, clip.frames + 50, &b[0]);
			check(a == b, "a frame past the end is not the last frame");
		}

		HDTrlClose(&clip);

		/* And the wrong column count is refused. */
		if (names[i] == "LARA_HEAD_98.TRL")
		{
			check(!HDTrlOpen(trl_path(stem, track).c_str(), 39, &clip, &why) &&
				!strcmp(why, "columns"), "LARA_HEAD_98.TRL opens at 39 columns");
		}
	}

	std::string list;

	for (size_t i = 0; i < lowStems.size(); i++)
		list += (i ? ", " : "") + lowStems[i];

	printf("%d files, %d open, %d of 40 columns, %d need the low word (%s); the "
		"widest last frame is %s at %.2f units\n", (int)names.size(), opened, of40,
		low, list.c_str(), lastWorstName.c_str(), lastWorst);

	check(names.size() == 45 && opened == 45, "%d files, %d opened; 45 wanted",
		(int)names.size(), opened);
	check(of40 == 44, "%d files of 40 columns, 44 wanted", of40);
	check(low == 15, "%d files need the low word, 15 wanted", low);
	check(lowStems.size() == 5, "the low word is needed by %d faces, 5 wanted",
		(int)lowStems.size());
}

static void section_ease()
{
	printf("\n-- 6. the end of a clip ---------------------------------------------\n");

	int clipFrame = -1;
	double amount = -1;

	check(HDLipsSourceOf(0, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_TRL &&
		clipFrame == 0 && amount == 1.0, "frame 0 is not the clip's frame 0");
	check(HDLipsSourceOf(-3, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_TRL &&
		clipFrame == 0, "a frame before 0 is not frame 0");
	check(HDLipsSourceOf(1291, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_TRL &&
		clipFrame == 1291 && amount == 1.0, "the last frame is not the clip's");
	check(HDLipsSourceOf(1292, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_EASE &&
		clipFrame == 1291 && amount == 0.9, "one past the end is not 0.9 of the last");
	check(HDLipsSourceOf(1300, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_EASE &&
		clipFrame == 1291 && fabs(amount - 0.1) < 1e-12, "nine past is not 0.1");
	check(HDLipsSourceOf(1301, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_BIND,
		"ten past the end is not the bind");
	check(HDLipsSourceOf(99999, 1292, &clipFrame, &amount) == HD_LIPS_SOURCE_BIND,
		"far past the end is not the bind");
	check(HDLipsSourceOf(0, 0, &clipFrame, &amount) == HD_LIPS_SOURCE_BIND,
		"a clip of no frames is not the bind");

	/* The ease moves every vertex on the line to the bind: the farthest
	 * vertex at k frames past the last is (10 - k) / 10 of the last frame's,
	 * and at amount 0 every skin is the identity. */
	Face* f = face("LARA_HEAD");
	int frames = 0;
	const std::vector<float> last = read_frame(f, 98, 100000, &frames);

	check(!last.empty(), "LARA_HEAD_98.TRL's last frame does not read");

	if (last.empty())
		return;

	Result full, r;

	deform(f, &last[0], 1.0, full);

	double worst = 0;

	for (int k = 1; k < HD_LIPS_EASE_FRAMES; k++)
	{
		HDLipsSourceOf(frames - 1 + k, frames, &clipFrame, &amount);
		deform(f, &last[0], amount, r);
		worst = std::max(worst, fabs(r.farthest - amount * full.farthest));
		printf("  %d past the last frame: amount %.1f, farthest %.4f units\n", k, amount,
			r.farthest);
	}

	check(worst < 1e-9 * (1.0 + full.farthest), "the ease leaves the line by %.2e",
		worst);

	deform(f, &last[0], 0.0, r);

	bool same = true;

	for (size_t i = 0; i < r.position.size(); i++)
		same = same && r.position[i] == f->position[i];

	for (size_t i = 0; i < r.normal.size(); i++)
		same = same && r.normal[i] == f->normal[i];

	check(same && r.farthest == 0.0, "amount 0 does not give the bind bit for bit");
	printf("LARA_HEAD_98.TRL's last frame (%d) moves a vertex %.2f units; at amount 0 "
		"the face is its bind %s\n", frames - 1, full.farthest,
		same ? "bit for bit" : "NOT bit for bit");
}

/* The .TRM a face was read from. */
static std::string trm_path(const Face* f)
{
	return g_bundle + "\\" + f->from + "\\ITEM\\" + f->stem + ".TRM";
}

/* Every frame of a face's block, read the way the engine reads it: out of the
 * .TRM from blockAt on. Empty when it does not open. */
static std::vector<float> block_frames(Face* f)
{
	HDTrlClip clip;
	const char* why = "";
	std::vector<float> out;

	if (!HDTrlOpenBlock(trm_path(f).c_str(), f->trm.blockAt, f->trm.blockFrames,
		f->trm.blockColumns, &clip, &why))
		return out;

	const size_t each = (size_t)f->trm.blockColumns * 12;

	out.resize(each * (size_t)f->trm.blockFrames);

	for (int k = 0; k < f->trm.blockFrames; k++)
		if (!HDTrlReadFrame(&clip, k, &out[each * (size_t)k]))
		{
			out.clear();
			break;
		}

	HDTrlClose(&clip);
	return out;
}

/* The lip gap of Lara's face: the mean height of the vertices whose heaviest
 * bone is a lower lip (16, 17) minus that of the upper lip's (14, 15), less
 * the same at the bind. Positive is open. */
static double lip_gap(const Face* f, const std::vector<float>& position)
{
	double up = 0, low = 0, upBind = 0, lowBind = 0;
	int ups = 0, lows = 0;

	for (int i = 0; i < f->trm.vertices; i++)
	{
		const unsigned char* b = f->trm.ownBone + i * 3;
		const unsigned char* w = f->trm.ownWeight + i * 3;
		int k = 0;

		if (w[1] > w[k]) k = 1;
		if (w[2] > w[k]) k = 2;

		if (b[k] == 14 || b[k] == 15)
		{
			up += position[(size_t)i * 3 + 1];
			upBind += f->position[(size_t)i * 3 + 1];
			ups++;
		}
		else if (b[k] == 16 || b[k] == 17)
		{
			low += position[(size_t)i * 3 + 1];
			lowBind += f->position[(size_t)i * 3 + 1];
			lows++;
		}
	}

	if (!ups || !lows)
		return 0;

	return (low / lows - up / ups) - (lowBind / lows - upBind / ups);
}

static void section_loop(const char* blockFolder)
{
	printf("\n-- 7. the loop between lines ----------------------------------------\n");

	struct Want
	{
		const char* stem;
		long at;		// 0: printed, not pinned
		int loop;
		int frames;
		int blinks[8];
		double farthest;	// the farthest vertex over the loop, 0: printed
	};

	const Want want[] =
	{
		{ "LARA_HEAD", 2264, 499, 508, { 92, 137, 198, 254, 314, 322, 420, 468 }, 6.681 },
		{ "LARA_CREEPY_HEAD", 2260, 499, 508, { 92, 137, 198, 254, 314, 322, 420, 468 }, 0 },
		{ "LARA_YOUNG_HEAD", 2264, 499, 508, { 92, 137, 198, 254, 314, 322, 420, 468 }, 0 },
		{ "LARSON_HEAD", 2648, 300, 301, { 71, 148, 229, -1, -1, -1, -1, -1 }, 4.405 },
	};

	if (!blockFolder)
		printf("  (no block folder given: the reads are not compared with an export)\n");

	std::vector<float> laraBlock;

	for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++)
	{
		const Want& w = want[i];
		Face* f = face(w.stem);

		check(f->loaded && f->mapped, "%s does not load and map: %s", w.stem, f->why);

		if (!f->loaded || !f->mapped)
			continue;

		const int columns = f->trm.blockColumns;
		const size_t each = (size_t)columns * 12;

		printf("%-18s from %s: block at %ld, %d frames, loop %d\n", w.stem,
			f->from.c_str(), f->trm.blockAt, f->trm.blockFrames, f->trm.blockLoop);

		if (w.at)
			check(f->trm.blockAt == w.at, "%s's block lies at %ld, %ld wanted", w.stem,
				f->trm.blockAt, w.at);

		check(f->trm.blockLoop == w.loop && f->trm.blockFrames == w.frames,
			"%s: loop %d of %d frames, %d of %d wanted", w.stem, f->trm.blockLoop,
			f->trm.blockFrames, w.loop, w.frames);

		const std::vector<float> all = block_frames(f);

		check(!all.empty(), "%s's block does not open at %ld", w.stem, f->trm.blockAt);

		if (all.empty())
			continue;

		check(!memcmp(&all[0], f->trm.blockBind, each * sizeof(float)),
			"%s's frame 0 read at %ld is not blockBind bit for bit", w.stem,
			f->trm.blockAt);

		/* The same block in the three faces of Lara. */
		if (i == 0)
			laraBlock = all;
		else if (!strncmp(w.stem, "LARA_", 5))
			check(all == laraBlock, "%s's block is not LARA_HEAD's", w.stem);

		/* Against the independent export, frame by frame. */
		if (blockFolder)
		{
			const std::string path = std::string(blockFolder) + "\\" + w.stem + "_block.TRL";
			HDTrlClip clip;
			const char* why = "";

			if (HDTrlOpen(path.c_str(), columns, &clip, &why))
			{
				std::vector<float> frame(each);
				int differ = 0;

				check(clip.frames == f->trm.blockFrames, "%s: the export holds %d "
					"frames, the block %d", w.stem, clip.frames, f->trm.blockFrames);

				for (int k = 0; k < clip.frames && k < f->trm.blockFrames; k++)
					if (!HDTrlReadFrame(&clip, k, &frame[0]) ||
						memcmp(&frame[0], &all[each * (size_t)k], each * sizeof(float)))
						differ++;

				check(!differ, "%s: %d frames differ from the export", w.stem, differ);
				printf("  against %s: %d frames, %d differ\n", path.c_str(), clip.frames,
					differ);
				HDTrlClose(&clip);
			}
			else
				printf("  (no export %s)\n", path.c_str());
		}

		const unsigned long long mask = HDLipsLoopMask(w.stem);
		const unsigned long long lids = HDLipsLidMask(w.stem);

		check(mask != 0 && lids != 0, "%s has no loop mask", w.stem);
		printf("  mask %llx, lids %llx\n", mask, lids);

		/* The columns outside the mask stand at frame 0 over the loop - the
		 * data behind playing Lara's brows with her eyes. LARSON's lips move
		 * in his loop and his mask leaves them out. */
		int outside = 0;

		for (int k = 1; k < f->trm.blockLoop; k++)
			for (int c = 0; c < columns; c++)
				if (!(mask >> c & 1) && memcmp(&all[each * (size_t)k + (size_t)c * 12],
					&all[(size_t)c * 12], 12 * sizeof(float)))
				{
					outside++;
					break;
				}

		if (!strncmp(w.stem, "LARA_", 5))
			check(!outside, "%s: %d loop frames move a column outside the mask",
				w.stem, outside);

		printf("  loop frames moving a column outside the mask: %d of %d\n", outside,
			f->trm.blockLoop - 1);

		/* The blinks. */
		HDTrlClip clip;
		const char* why = "";
		int start[16];
		float lid[16];
		int blinks = 0;

		if (HDTrlOpenBlock(trm_path(f).c_str(), f->trm.blockAt, f->trm.blockFrames,
			columns, &clip, &why))
		{
			blinks = HDLipsBlinks(&f->map, &clip, f->trm.blockLoop, lids, start, lid, 16);
			HDTrlClose(&clip);
		}

		int wanted = 0;
		bool same = true;

		while (wanted < 8 && w.blinks[wanted] >= 0)
			wanted++;

		same = blinks == wanted;

		for (int b = 0; b < blinks && same; b++)
			same = start[b] == w.blinks[b];

		check(same, "%s: %d blinks, %d wanted", w.stem, blinks, wanted);
		printf("  blinks:");

		for (int b = 0; b < blinks; b++)
			printf(" %d (%.1f)", start[b], lid[b]);

		printf("\n");

		/* The mask over every loop frame: a vertex that moves has weight on a
		 * joint a masked column drives, and none else moves by a bit, neither
		 * its position nor its normal. */
		bool onMasked[256];

		for (int j = 0; j < 256; j++)
			onMasked[j] = false;

		for (int c = 0; c < f->map.columns; c++)
			if ((mask >> c & 1) && f->map.joint[c] >= 0)
				onMasked[f->map.joint[c]] = true;

		std::vector<float> outPosition((size_t)f->trm.vertices * 3);
		std::vector<unsigned char> outNormal((size_t)f->trm.vertices * 3);
		int strays = 0, moved = 0;
		double farthest = 0;
		std::vector<bool> ever((size_t)f->trm.vertices, false);

		for (int k = 0; k < f->trm.blockLoop; k++)
		{
			HDLipsSkins(&f->map, &all[each * (size_t)k], 1.0, &f->skins[0], mask,
				f->trm.blockBind);

			const double far = HDLipsDeform(&f->skins[0], f->map.rigJoints,
				f->trm.vertices, &f->position[0], &f->normal[0], f->trm.ownBone,
				f->trm.ownWeight, &outPosition[0], &outNormal[0]);

			farthest = far > farthest ? far : farthest;

			for (int v = 0; v < f->trm.vertices; v++)
			{
				const size_t at = (size_t)v * 3;

				if (outPosition[at] == f->position[at] &&
					outPosition[at + 1] == f->position[at + 1] &&
					outPosition[at + 2] == f->position[at + 2] &&
					outNormal[at] == f->normal[at] && outNormal[at + 1] == f->normal[at + 1] &&
					outNormal[at + 2] == f->normal[at + 2])
					continue;

				const unsigned char* b = f->trm.ownBone + at;
				const unsigned char* wt = f->trm.ownWeight + at;

				if (!((wt[0] && onMasked[b[0]]) || (wt[1] && onMasked[b[1]]) ||
					(wt[2] && onMasked[b[2]])))
					strays++;

				if (!ever[(size_t)v])
				{
					ever[(size_t)v] = true;
					moved++;
				}
			}
		}

		check(!strays, "%s: %d vertices off the masked joints move", w.stem, strays);

		if (w.farthest > 0)
			check(fabs(farthest - w.farthest) < 0.0005, "%s: the farthest vertex "
				"of the loop moves %.4f units, %.3f wanted", w.stem, farthest,
				w.farthest);

		printf("  over %d loop frames: %d of %d vertices move, %d of them off the "
			"masked joints; the farthest %.4f units\n", f->trm.blockLoop, moved,
			f->trm.vertices, strays, farthest);

		/* A mask of every column is no mask, bit for bit, and so is a mask
		 * without a bind - at a blink and at the firing frame. */
		const unsigned long long every = columns >= 64 ? ~0ull : (1ull << columns) - 1;
		std::vector<double> plain(f->skins.size()), full(f->skins.size()),
			unbound(f->skins.size());
		const int probes[2] = { w.blinks[0] + 1, f->trm.blockFrames - 1 };

		for (int p = 0; p < 2; p++)
		{
			const float* frame = &all[each * (size_t)probes[p]];

			HDLipsSkins(&f->map, frame, 1.0, &plain[0]);
			HDLipsSkins(&f->map, frame, 1.0, &full[0], every, f->trm.blockBind);
			HDLipsSkins(&f->map, frame, 1.0, &unbound[0], mask, 0);

			check(plain == full && plain == unbound, "%s frame %d: a full mask or "
				"no bind is not the frame bit for bit", w.stem, probes[p]);
		}
	}

	/* Every other face: no loop. */
	const char* others[] =
	{
		"ADMIRAL_HEAD", "BLUE_GUARD_HEAD", "DEMON_HEAD", "HANGMAN_HEAD",
		"PATRICK_BROWN_HEAD", "PATRICK_WHITE_HEAD", "PIERRE_HEAD", "SERGIE_HEAD",
		"VON_CROY_ELDER_HEAD", "VON_CROY_SENIOR_HEAD", "ZIP_HEAD"
	};

	for (size_t i = 0; i < sizeof(others) / sizeof(others[0]); i++)
	{
		Face* f = face(others[i]);

		check(!HDLipsLoopMask(others[i]) && !HDLipsLidMask(others[i]),
			"%s has a loop mask", others[i]);

		if (f->loaded)
			printf("%-22s block of %d frames, loop %d, mask 0\n", others[i],
				f->trm.blockFrames, f->trm.blockLoop);
	}
}

static void section_fire()
{
	printf("\n-- 8. the firing face -----------------------------------------------\n");

	/* The ease in: a third a tick, from the tick the swap comes on. */
	check(HDLipsEaseIn(-1) == 0.0 && HDLipsEaseIn(0) == 1.0 / 3.0 &&
		HDLipsEaseIn(1) == 2.0 / 3.0 && HDLipsEaseIn(2) == 1.0 &&
		HDLipsEaseIn(3) == 1.0 && HDLipsEaseIn(1000) == 1.0,
		"the ease in is not 1/3, 2/3, 1");

	/* The steps: up a tick while on, down a tick while off, several ticks at
	 * once where the draws skipped some, and a release half way in going
	 * back from where it stood. */
	int s = 0;
	int seen[8];

	for (int t = 0; t < 4; t++)
		seen[t] = s = HDLipsFireStep(s, true, 1);

	for (int t = 4; t < 8; t++)
		seen[t] = s = HDLipsFireStep(s, false, 1);

	check(seen[0] == 1 && seen[1] == 2 && seen[2] == 3 && seen[3] == 3 &&
		seen[4] == 2 && seen[5] == 1 && seen[6] == 0 && seen[7] == 0,
		"the steps on and off are %d %d %d %d / %d %d %d %d", seen[0], seen[1],
		seen[2], seen[3], seen[4], seen[5], seen[6], seen[7]);
	check(HDLipsFireStep(0, true, 5) == 3 && HDLipsFireStep(3, false, 2) == 1 &&
		HDLipsFireStep(1, false, 1) == 0 && HDLipsFireStep(1, true, 1) == 2 &&
		HDLipsFireStep(2, true, 0) == 2, "several ticks at once step wrongly");

	for (int t = 0; t < 4; t++)
		check(HDLipsFireStep(0, true, t + 1) / (double)HD_LIPS_EASE_IN_TICKS ==
			HDLipsEaseIn(t), "step and ease in disagree at tick %d", t);

	Face* f = face("LARA_HEAD");

	if (!f->mapped)
		return;

	const std::vector<float> all = block_frames(f);
	const size_t each = (size_t)f->trm.blockColumns * 12;

	check(all.size() == each * 508, "LARA_HEAD's block does not read");

	if (all.size() != each * 508)
		return;

	/* The firing frame and the shout beside it, against the numbers measured
	 * on LARA_HEAD.TRM's block, frames 501 and 506: the lips apart, the jaw's
	 * turn (column 11) and the chin's. */
	struct Pose
	{
		int frame;
		double gap;
		double jaw;
	};

	const Pose poses[2] = { { HD_LIPS_FIRE_FRAME, 4.08, 0.0 }, { 506, 9.36, 10.89 } };
	Result full;

	for (int p = 0; p < 2; p++)
	{
		const float* frame = &all[each * (size_t)poses[p].frame];
		Result r;

		deform(f, frame, 1.0, r);

		const double gap = lip_gap(f, r.position);
		const double jaw = HDLipsTurn(&f->map, frame, 11);

		check(fabs(gap - poses[p].gap) < 0.01 && fabs(jaw - poses[p].jaw) < 0.01,
			"block frame %d: lips %.3f apart, jaw %.3f degrees; %.2f and %.2f "
			"wanted", poses[p].frame, gap, jaw, poses[p].gap, poses[p].jaw);
		printf("block frame %d: the lips %+.2f units apart, the jaw %.2f degrees, the "
			"farthest vertex %.4f units, %d past a unit\n", poses[p].frame, gap, jaw,
			r.farthest, r.overOne);

		if (!p)
			full = r;
	}

	check(fabs(full.farthest - 3.9736) < 0.0005, "the firing frame's farthest "
		"vertex moves %.4f units", full.farthest);

	/* The blend. From the bind it is HDLipsSkins's own ease bit for bit,
	 * every vertex on the straight line; at 0 it is the bind bit for bit.
	 * From the loop's frame 93 (the upper lids 25.3 degrees closed) at a
	 * third, the eyes stand between the two. */
	const float* fire = &all[each * HD_LIPS_FIRE_FRAME];
	std::vector<double> identity(f->skins.size()), to(f->skins.size()),
		eased(f->skins.size()), blended(f->skins.size());

	HDLipsSkins(&f->map, 0, 1.0, &identity[0]);
	HDLipsSkins(&f->map, fire, 1.0, &to[0]);

	double worst = 0;

	for (int t = 0; t <= HD_LIPS_EASE_IN_TICKS; t++)
	{
		const double amount = (double)t / HD_LIPS_EASE_IN_TICKS;

		HDLipsSkins(&f->map, fire, amount, &eased[0]);
		HDLipsSkinsBlend(&identity[0], &to[0], amount, f->map.rigJoints, &blended[0]);

		check(eased == blended, "the blend from the bind at %d/%d is not the ease",
			t, HD_LIPS_EASE_IN_TICKS);

		std::vector<float> position((size_t)f->trm.vertices * 3);
		std::vector<unsigned char> normal((size_t)f->trm.vertices * 3);
		const double far = HDLipsDeform(&blended[0], f->map.rigJoints, f->trm.vertices,
			&f->position[0], &f->normal[0], f->trm.ownBone, f->trm.ownWeight,
			&position[0], &normal[0]);

		worst = std::max(worst, fabs(far - amount * full.farthest));
		printf("  step %d: %.3f of the way, the farthest vertex %.4f units\n", t, amount,
			far);

		if (!t)
			check(position == f->position && normal == f->normal && far == 0.0,
				"step 0 is not the bind bit for bit");
	}

	check(worst < 1e-9 * (1.0 + full.farthest), "the blend leaves the line by %.2e",
		worst);

	const unsigned long long mask = HDLipsLoopMask("LARA_HEAD");
	std::vector<double> from(f->skins.size());

	HDLipsSkins(&f->map, &all[each * 93], 1.0, &from[0], mask, f->trm.blockBind);
	HDLipsSkinsBlend(&from[0], &to[0], 1.0 / 3.0, f->map.rigJoints, &blended[0]);

	/* The upper lid's turn, read back out of the blended skin of its joint
	 * (column 12 drives joint 19). */
	const int lidJoint = f->map.joint[12];
	const double lidFrom = HDLipsTurn(&f->map, &all[each * 93], 12);
	const double lidTo = HDLipsTurn(&f->map, fire, 12);
	double lidMid = 0;

	{
		const double* m = &blended[(size_t)lidJoint * 12];
		const double det = m[0] * (m[5] * m[10] - m[6] * m[9]) -
			m[1] * (m[4] * m[10] - m[6] * m[8]) + m[2] * (m[4] * m[9] - m[5] * m[8]);
		const double sc = cbrt(fabs(det));
		double c = ((m[0] + m[5] + m[10]) / sc - 1.0) / 2.0;

		c = c < -1 ? -1 : c > 1 ? 1 : c;
		lidMid = acos(c) * 180.0 / 3.14159265358979323846;
	}

	check(lidJoint == 19 && lidMid < std::max(lidFrom, lidTo) + 0.5 &&
		lidMid > std::min(lidFrom, lidTo) - 0.5, "a third of the way from frame 93 "
		"the upper lid turns %.2f degrees, between %.2f and %.2f wanted", lidMid,
		lidFrom, lidTo);
	printf("from loop frame 93 (upper lid %.2f degrees) a third of the way to the "
		"firing frame (%.2f): %.2f\n", lidFrom, lidTo, lidMid);
}

/* The game's own cutseq.bin, read in place and never written - the file
 * CutseqProbe reads by default. */
static const char* CUTSEQ_FILE =
	"C:/Cloude AI/TR Randomizer final steps/Tomb5 Steam Version/data/"
	"cutseq.bin";

/* Cutscene n's audio track and actor 0's object slot, out of cutseq.bin: a
 * table of offset and size (8 bytes a cutscene, by number), then each
 * cutscene's NEW_CUTSCENE (global\types.h) - numactors at 0, audio_track at
 * 16, actor_data at 24 with 8 bytes an actor and objslot at 4 of it. */
static bool cut_track(const std::vector<char>& file, int n, int* track,
	int* actor0)
{
	if (n <= 0 || file.size() < (size_t)n * 8 + 8)
		return false;

	int offset = 0;
	int size = 0;

	memcpy(&offset, &file[(size_t)n * 8], 4);
	memcpy(&size, &file[(size_t)n * 8 + 4], 4);

	if (offset <= 0 || size < 32 || (size_t)offset + size > file.size())
		return false;

	short actors = 0;
	short slot = -1;

	memcpy(&actors, &file[(size_t)offset], 2);
	memcpy(track, &file[(size_t)offset + 16], 4);
	memcpy(&slot, &file[(size_t)offset + 28], 2);
	*actor0 = actors > 0 ? slot : -1;
	return true;
}

/* One part of a face's rig changed by one byte: HDLipsSameRig must refuse
 * it. `part` 0 the inverse binds, 1 the block's parents, 2 its columns, 3
 * its frame 0. The copy shares nothing with the face. */
static bool same_after_one_byte(const HDTrmModel& trm, int part)
{
	const size_t joints = (size_t)trm.rigJoints * 12;
	std::vector<float> bind(trm.rigBind, trm.rigBind + joints);
	std::vector<int> parent(trm.blockParent, trm.blockParent + trm.blockNodes);
	std::vector<int> column(trm.blockColumn,
		trm.blockColumn + trm.blockColumns);
	std::vector<float> zero(trm.blockBind,
		trm.blockBind + (size_t)trm.blockColumns * 12);
	unsigned char* bytes = part == 0 ? (unsigned char*)&bind[5]
		: part == 1 ? (unsigned char*)&parent[7]
		: part == 2 ? (unsigned char*)&column[3]
		: (unsigned char*)&zero[17];

	bytes[0] ^= 1;

	HDTrmModel copy = trm;

	copy.rigBind = &bind[0];
	copy.blockParent = &parent[0];
	copy.blockColumn = &column[0];
	copy.blockBind = &zero[0];
	return HDLipsSameRig(&trm, &copy);
}

static void section_mod_rig()
{
	printf("\n-- 9. another geometry on the same rig ------------------\n");

	Face* lara = face("LARA_HEAD");
	Face* young = face("LARA_YOUNG_HEAD");
	Face* creepy = face("LARA_CREEPY_HEAD");
	Face* larson = face("LARSON_HEAD");
	Face* pierre = face("PIERRE_HEAD");

	if (!lara->loaded || !young->loaded || !creepy->loaded || !larson->loaded ||
		!pierre->loaded)
	{
		check(false, "the five faces of this section do not all load");
		return;
	}

	/* The three faces of Lara carry one rig and one block header; their
	 * geometry differs (5913, 12740 and 5409 vertices) and the creepy
	 * face's block lies four bytes earlier, which the rule does not read. */
	check(HDLipsSameRig(&lara->trm, &lara->trm),
		"LARA_HEAD is not its own rig");
	check(HDLipsSameRig(&lara->trm, &young->trm) &&
		HDLipsSameRig(&young->trm, &lara->trm),
		"LARA_YOUNG_HEAD is not taken for LARA_HEAD's rig");
	check(HDLipsSameRig(&lara->trm, &creepy->trm),
		"LARA_CREEPY_HEAD is not taken for LARA_HEAD's rig");
	check(!HDLipsSameRig(&lara->trm, &larson->trm) &&
		!HDLipsSameRig(&larson->trm, &pierre->trm),
		"LARSON_HEAD or PIERRE_HEAD is taken for another face's rig");

	for (int part = 0; part < 4; part++)
		check(!same_after_one_byte(lara->trm, part),
			"one byte changed in part %d of LARA_HEAD's rig is still the rig",
			part);

	HDTrmModel none;

	memset(&none, 0, sizeof(none));
	check(!HDLipsSameRig(&none, &none) && !HDLipsSameRig(&lara->trm, &none) &&
		!HDLipsSameRig(0, &lara->trm), "a face without a rig is taken");

	/* The folder a mod's face speaks from - HDLipsModRig and
	 * HDLipsTrackFiles, the two make_lips (hd_model.cpp) decides by.
	 * LARA_YOUNG_HEAD stands in for a mod's LARA_HEAD.TRM: another geometry
	 * on her rig. Its own folder is one with no track file of hers (a
	 * folder that is not there), one with game 4's (4\ITEM, a mod that
	 * ships its own), and the install's face beside it is LARA_HEAD,
	 * LARSON_HEAD (another rig) or none. */
	const std::string item5 = g_root5 + "\\ITEM";
	const std::string item4 = g_bundle + "\\4\\ITEM";
	const std::string bare = g_root5 + "\\no mod folder of this name";
	const std::string laraFile = trm_path(lara);
	const std::string larsonFile = trm_path(larson);
	const std::string missing = bare + "\\LARA_HEAD.TRM";
	int tracks5[64];
	const int count5 = HDLipsTrackFiles(item5.c_str(), "LARA_HEAD", tracks5,
		64);
	const int count4 = HDLipsTrackFiles(item4.c_str(), "LARA_HEAD", 0, 0);
	const int countBare = HDLipsTrackFiles(bare.c_str(), "LARA_HEAD", 0, 0);

	printf("track files of LARA_HEAD: %d in 5\\ITEM (%d to %d), %d in "
		"4\\ITEM, %d in a folder that is not there\n", count5,
		count5 > 0 ? tracks5[0] : 0, count5 > 0 ? tracks5[count5 - 1] : 0,
		count4, countBare);
	check(count5 == 18 && tracks5[0] == 96 && tracks5[17] == 135,
		"5\\ITEM holds %d track files of LARA_HEAD, 18 (96 to 135) wanted",
		count5);
	check(count4 == 24 && countBare == 0, "4\\ITEM holds %d track files of "
		"LARA_HEAD (24 wanted), a missing folder %d", count4, countBare);

	const char* folder = 0;
	int rig = HDLipsModRig(&young->trm, countBare > 0, laraFile.c_str(),
		bare.c_str(), item5.c_str(), &folder);
	const std::string spoken = folder ? folder : "";

	check(rig == HD_LIPS_RIG_INSTALL && spoken == item5 &&
		!strcmp(HDLipsRigName(rig), "install"), "a mod's face on her rig "
		"without track files: rig=%s, folder %s - install and 5\\ITEM "
		"wanted", HDLipsRigName(rig), spoken.c_str());

	rig = HDLipsModRig(&lara->trm, false, laraFile.c_str(), bare.c_str(),
		item5.c_str(), &folder);
	check(rig == HD_LIPS_RIG_INSTALL && folder && item5 == folder,
		"the install's own LARA_HEAD as a mod's face: rig=%s",
		HDLipsRigName(rig));

	rig = HDLipsModRig(&young->trm, count4 > 0, laraFile.c_str(),
		item4.c_str(), item5.c_str(), &folder);
	check(rig == HD_LIPS_RIG_OWN && folder && item4 == folder &&
		!strcmp(HDLipsRigName(rig), "own"), "a mod's face with track files "
		"of its own: rig=%s, folder %s - own and its own folder wanted",
		HDLipsRigName(rig), folder ? folder : "-");

	rig = HDLipsModRig(&young->trm, false, larsonFile.c_str(), bare.c_str(),
		item5.c_str(), &folder);
	check(rig == HD_LIPS_RIG_DIFFERS && folder && bare == folder &&
		!strcmp(HDLipsRigName(rig), "differs"), "a mod's face beside an "
		"install face of another rig: rig=%s - differs and its own folder "
		"wanted", HDLipsRigName(rig));

	rig = HDLipsModRig(&young->trm, false, "", bare.c_str(), item5.c_str(),
		&folder);

	const int rigMissing = HDLipsModRig(&young->trm, false, missing.c_str(),
		bare.c_str(), item5.c_str(), 0);

	check(rig == HD_LIPS_RIG_NONE && folder && bare == folder &&
		rigMissing == HD_LIPS_RIG_NONE &&
		!strcmp(HDLipsRigName(rig), "none") && !strcmp(HDLipsRigName(4), "-"),
		"a mod's face with no install face of its name: rig=%s and %s",
		HDLipsRigName(rig), HDLipsRigName(rigMissing));

	/* The two cutscenes, as the game's data\cutseq.bin names them: Lara is
	 * actor 0 (object slot 0) of cutscene 8 (ANDREA2) and of cutscene 32
	 * (JOBY5's start), and the audio track each plays is the track file her
	 * face speaks. The engine overrides the track of cutscene 4 alone
	 * (init_cutseq_actors, deltapak.cpp). The numbers wanted here are what
	 * the file reads; the tracks below are taken from the file. */
	struct Cut
	{
		int cut;
		int track;		// wanted from the file
		int frames;
		int frame;		// the frame of the widest mouth, every 10th
		double lara;		// the farthest vertex on LARA_HEAD
		double young;		// ... and on LARA_YOUNG_HEAD
	};
	static const Cut cuts[2] =
	{
		{ 8, 102, 1395, 70, 6.6087, 6.8420 },
		{ 32, 120, 3078, 1270, 7.1867, 7.1686 },
	};
	std::vector<char> cutseq;
	FILE* fp = fopen(CUTSEQ_FILE, "rb");

	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		cutseq.resize((size_t)ftell(fp));
		fseek(fp, 0, SEEK_SET);

		if (!cutseq.empty() &&
			fread(&cutseq[0], 1, cutseq.size(), fp) != cutseq.size())
			cutseq.clear();

		fclose(fp);
	}

	check(!cutseq.empty(), "%s does not read", CUTSEQ_FILE);

	Face* faces[2] = { lara, young };

	for (int i = 0; i < 2; i++)
	{
		int track = -1;
		int laraSlot = -1;

		cut_track(cutseq, cuts[i].cut, &track, &laraSlot);
		printf("cutscene %2d: audio track %d, actor 0 in slot %d\n",
			cuts[i].cut, track, laraSlot);
		check(track == cuts[i].track && laraSlot == 0, "cutscene %d: track "
			"%d and actor 0 in slot %d, track %d and Lara (0) wanted",
			cuts[i].cut, track, laraSlot, cuts[i].track);

		bool listed = false;

		for (int t = 0; t < count5 && t < 64; t++)
			listed = listed || tracks5[t] == track;

		check(listed, "cutscene %d: track %d is not among LARA_HEAD's "
			"track files in the folder the mod's face speaks from",
			cuts[i].cut, track);

		for (int k = 0; k < 2; k++)
		{
			Face* f = faces[k];

			if (!f->mapped)
			{
				check(false, "%s is not mapped: %s", f->stem.c_str(), f->why);
				continue;
			}

			/* LARA_HEAD's file in the folder HDLipsModRig gave, read at
			 * the face's columns - the path a mod's face on her rig takes. */
			HDTrlClip clip;
			const char* why = "";
			int frames = 0;
			char name[64];
			std::vector<float> frame((size_t)f->map.columns * 12);

			snprintf(name, sizeof(name), "\\LARA_HEAD_%d.TRL", track);

			if (HDTrlOpen((spoken + name).c_str(), f->map.columns, &clip,
				&why))
			{
				frames = clip.frames;

				if (!HDTrlReadFrame(&clip, cuts[i].frame, &frame[0]))
					frame.clear();

				HDTrlClose(&clip);
			}
			else
				frame.clear();

			check(!frame.empty() && frames == cuts[i].frames,
				"cutscene %d: LARA_HEAD_%d.TRL reads %d frames for %s, "
				"%d wanted", cuts[i].cut, track, frames,
				f->stem.c_str(), cuts[i].frames);

			if (frame.empty())
				continue;

			Result moved;
			Result rest;
			const double want = k ? cuts[i].young : cuts[i].lara;

			deform(f, &frame[0], 1.0, moved);
			deform(f, f->trm.blockBind, 1.0, rest);

			printf("cutscene %2d: LARA_HEAD_%d.TRL frame %d on %s - the "
				"farthest vertex %.4f units, %d past a unit; the bind "
				"%.2e\n", cuts[i].cut, track, cuts[i].frame,
				f->stem.c_str(), moved.farthest, moved.overOne, rest.farthest);
			check(fabs(moved.farthest - want) <= 0.001 && rest.farthest < 1e-3,
				"cutscene %d: %s moves %.4f units, %.4f wanted, and its "
				"bind %.2e", cuts[i].cut, f->stem.c_str(), moved.farthest,
				want, rest.farthest);
		}
	}
}

int main(int argc, char** argv)
{
	const char* objFolder = argc > 1 ? argv[1] : 0;
	const char* blockFolder = argc > 2 ? argv[2] : 0;

	g_root5 = HDInstallRoot();

	printf("LipsTest - the lips of the cutscene faces\n");
	printf("install: %s\n", g_root5.empty() ? "(none found)" : g_root5.c_str());

	check(!g_root5.empty(), "no Tomb Raider IV-VI Remastered found: %s",
		HDInstallDiagnostics());

	if (g_root5.empty())
	{
		printf("\n%d checks, %d failed\n", g_checks, g_fail);
		return 1;
	}

	g_bundle = g_root5.substr(0, g_root5.find_last_of("\\/"));

	section_map();
	section_bind();
	section_open();
	section_obj(objFolder);
	section_frames();
	section_ease();
	section_loop(blockFolder);
	section_fire();
	section_mod_rig();

	for (std::map<std::string, Face*>::iterator at = g_faces.begin(); at != g_faces.end(); ++at)
	{
		HDTrmFree(&at->second->trm);
		delete at->second;
	}

	printf("\n%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
