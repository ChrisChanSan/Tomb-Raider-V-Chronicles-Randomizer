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
 *     LipsTest [<obj folder>]
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

int main(int argc, char** argv)
{
	const char* objFolder = argc > 1 ? argv[1] : 0;

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

	for (std::map<std::string, Face*>::iterator at = g_faces.begin(); at != g_faces.end(); ++at)
	{
		HDTrmFree(&at->second->trm);
		delete at->second;
	}

	printf("\n%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
