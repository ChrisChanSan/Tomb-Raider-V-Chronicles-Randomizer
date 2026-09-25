/*
 * Modified 2026 by ChrisChanSan.
 *
 * Where the layout of a face's track file and the deform of a face by its
 * joints come from is written at the top of hd_trl.h.
 *
 * TOMB5 is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version. See the LICENSE file at the root of this source tree.
 */
#include "hd_trl.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	const unsigned long long kMatrixBytes = 48;

	/* ------------------------------------------------- 3x4 arithmetic --
	 *
	 * Twelve doubles, row-major: (r00 r01 r02 tx), (r10 r11 r12 ty),
	 * (r20 r21 r22 tz) - the layout of the files, carried in double because
	 * a chain of forty columns composed in float drifts. */

	void identity(double* m)
	{
		std::memset(m, 0, sizeof(double) * 12);
		m[0] = m[5] = m[10] = 1.0;
	}

	void from_floats(const float* in, double* out)
	{
		for (int i = 0; i < 12; i++)
			out[i] = (double)in[i];
	}

	// out = a * b, as two affine maps: b first, then a.
	void multiply(const double* a, const double* b, double* out)
	{
		double r[12];

		for (int row = 0; row < 3; row++)
		{
			for (int col = 0; col < 4; col++)
				r[row * 4 + col] = a[row * 4 + 0] * b[0 * 4 + col] +
					a[row * 4 + 1] * b[1 * 4 + col] +
					a[row * 4 + 2] * b[2 * 4 + col];

			r[row * 4 + 3] += a[row * 4 + 3];
		}

		std::memcpy(out, r, sizeof(r));
	}

	/* The general inverse. Not the transpose: the columns carry a scale -
	 * LARA_HEAD's composed bind has determinants from 1.000 to 1.044, and
	 * the skins of LARA_HEAD_98.TRL run from 0.958 to 1.229 (LipsTest prints
	 * both) - and a rotation's inverse would leave the bind's scale in every
	 * frame. False for a matrix that cannot be inverted. */
	bool invert(const double* m, double* out)
	{
		const double a = m[0], b = m[1], c = m[2];
		const double d = m[4], e = m[5], f = m[6];
		const double g = m[8], h = m[9], k = m[10];

		const double c00 = e * k - f * h;
		const double c01 = f * g - d * k;
		const double c02 = d * h - e * g;
		const double det = a * c00 + b * c01 + c * c02;

		if (!(std::fabs(det) > 1e-12))
			return false;

		const double s = 1.0 / det;
		double r[12];

		r[0] = c00 * s;
		r[1] = (c * h - b * k) * s;
		r[2] = (b * f - c * e) * s;
		r[4] = c01 * s;
		r[5] = (a * k - c * g) * s;
		r[6] = (c * d - a * f) * s;
		r[8] = c02 * s;
		r[9] = (b * g - a * h) * s;
		r[10] = (a * e - b * d) * s;

		for (int row = 0; row < 3; row++)
			r[row * 4 + 3] = -(r[row * 4 + 0] * m[3] + r[row * 4 + 1] * m[7] +
				r[row * 4 + 2] * m[11]);

		std::memcpy(out, r, sizeof(r));
		return true;
	}

	/* ------------------------------------------------------ the axes --
	 *
	 * The vertices of a rigged face stand in (x, -z, y) of its rig: the
	 * centre of each joint's own vertices lies 8 to 35 units from that joint
	 * read so, in twelve of the thirteen rigged faces of 5\ITEM and
	 * LARA_HEAD of 4\ITEM (LARA_HEAD 18.1, against 63.4 under the next best
	 * order). The track files stand in the same axes for ten of them, in
	 * others for two, and in none that fits the thirteenth. Measured on
	 * frame 0 of each face's block, composed down the parents, against the
	 * rig's own joints, both in the vertices' axes - LipsTest prints it:
	 *
	 *   - the ten: 0.0002 units at most in LARA_HEAD, ADMIRAL_HEAD,
	 *     DEMON_HEAD and the two PATRICK faces; 0.49 in BLUE_GUARD_HEAD, 0.69
	 *     and 0.71 in the two VON_CROY faces, 0.80 in PIERRE_HEAD, and 3.46 in
	 *     LARSON_HEAD, whose frame 0 is not its rig's bind at all (2.96 on
	 *     average) - which is why the bind is frame 0 and never the rig's
	 *     inverse binds.
	 *   - SERGIE_HEAD: its track stands in the rig's own (x, y, z), so the
	 *     vertices see it through (x, -z, y); 0.0002 units.
	 *   - HANGMAN_HEAD: its track stands in (x, z, -y) of the rig, so the
	 *     vertices see it through (x, -y, -z); 0.0005 units over its five
	 *     joints.
	 *   - ZIP_HEAD: its track composes onto its rig under (z, y, -x), but the
	 *     best axis order still leaves the centres of its joints' vertices
	 *     188 units from the joints. No cutscene draws ZIP, so the face is
	 *     refused rather than guessed.
	 *
	 * Keyed on the face's file name, which is what the remaster names the
	 * files by. */
	struct AxisOfFace
	{
		const char* stem;
		int axis;
	};

	const AxisOfFace g_axis[] =
	{
		{ "SERGIE_HEAD", HD_LIPS_AXIS_X_NZ_Y },
		{ "HANGMAN_HEAD", HD_LIPS_AXIS_X_NY_NZ },
		{ "ZIP_HEAD", HD_LIPS_AXIS_REFUSED },
	};

	bool same_name(const char* a, const char* b)
	{
		for (; *a && *b; a++, b++)
			if (std::toupper((unsigned char)*a) != std::toupper((unsigned char)*b))
				return false;

		return !*a && !*b;
	}

	/* The axis as a matrix: row r of the vertices' axes takes `sign` times
	 * axis `from` of the track's. */
	void axis_matrix(int axis, double* a)
	{
		std::memset(a, 0, sizeof(double) * 9);

		switch (axis)
		{
		case HD_LIPS_AXIS_X_NZ_Y:
			a[0] = 1.0;
			a[5] = -1.0;
			a[7] = 1.0;
			break;

		case HD_LIPS_AXIS_X_NY_NZ:
			a[0] = 1.0;
			a[4] = -1.0;
			a[8] = -1.0;
			break;

		default:
			a[0] = a[4] = a[8] = 1.0;
			break;
		}
	}

	unsigned char pack_normal(double v)
	{
		const double scaled = v * 128.0 + 127.0;

		if (!(scaled > 0.0))
			return 0;

		if (scaled >= 255.0)
			return 255;

		return (unsigned char)(int)(scaled + 0.5);
	}
}

/* ------------------------------------------------------------ the file -- */

bool HDTrlFrames(unsigned int word, unsigned long long bytes, int columns,
	int* frames, bool* lowWord)
{
	if (frames)
		*frames = 0;
	if (lowWord)
		*lowWord = false;

	if (columns <= 0 || bytes < 4)
		return false;

	const unsigned long long rest = bytes - 4;
	const unsigned long long frame = (unsigned long long)columns * kMatrixBytes;
	const unsigned int low = word & 0xFFFFu;

	if (word && rest == (unsigned long long)word * frame)
	{
		if (frames)
			*frames = (int)word;
		return true;
	}

	if (low && rest == (unsigned long long)low * frame)
	{
		if (frames)
			*frames = (int)low;
		if (lowWord)
			*lowWord = true;
		return true;
	}

	return false;
}

bool HDTrlOpen(const char* path, int columns, HDTrlClip* out, const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	if (!out)
	{
		*why = "no clip to open into";
		return false;
	}

	std::memset(out, 0, sizeof(*out));

	std::FILE* file = path && *path ? std::fopen(path, "rb") : 0;

	if (!file)
	{
		*why = "no file";
		return false;
	}

	unsigned int word = 0;
	long bytes = -1;

	if (std::fseek(file, 0, SEEK_END) == 0)
		bytes = std::ftell(file);

	if (bytes < 4 || std::fseek(file, 0, SEEK_SET) != 0 ||
		std::fread(&word, 4, 1, file) != 1)
	{
		std::fclose(file);
		*why = "too short";
		return false;
	}

	int frames = 0;
	bool low = false;

	if (!HDTrlFrames(word, (unsigned long long)bytes, columns, &frames, &low))
	{
		std::fclose(file);
		*why = "columns";
		return false;
	}

	out->file = file;
	out->frames = frames;
	out->columns = columns;
	out->word = word;
	out->lowWord = low;
	return true;
}

bool HDTrlReadFrame(HDTrlClip* clip, int frame, float* out)
{
	if (!clip || !clip->file || clip->frames <= 0 || !out)
		return false;

	if (frame < 0)
		frame = 0;

	if (frame >= clip->frames)
		frame = clip->frames - 1;

	const size_t bytes = (size_t)clip->columns * (size_t)kMatrixBytes;
	const long at = 4 + (long)frame * (long)bytes;

	if (std::fseek(clip->file, at, SEEK_SET) != 0)
		return false;

	return std::fread(out, 1, bytes, clip->file) == bytes;
}

void HDTrlClose(HDTrlClip* clip)
{
	if (!clip)
		return;

	if (clip->file)
		std::fclose(clip->file);

	std::memset(clip, 0, sizeof(*clip));
}

/* ------------------------------------------------------------- the map -- */

const char* HDLipsAxisName(int axis)
{
	switch (axis)
	{
	case HD_LIPS_AXIS_IDENTITY: return "identity";
	case HD_LIPS_AXIS_X_NZ_Y: return "(x,-z,y)";
	case HD_LIPS_AXIS_X_NY_NZ: return "(x,-y,-z)";
	default: return "-";
	}
}

bool HDLipsMapBuild(const HDTrmModel* trm, const char* stem, HDLipsMap* out,
	const char** why)
{
	static const char* nothing = "";
	const char* dummy = nothing;

	if (!why)
		why = &dummy;
	*why = nothing;

	if (!out)
		return false;

	std::memset(out, 0, sizeof(*out));
	out->headJoint = -1;
	out->axis = HD_LIPS_AXIS_REFUSED;

	if (!trm || trm->rigJoints <= 0 || !trm->rigJoint || !trm->rigBind)
	{
		*why = "the file carries no rig";
		return false;
	}

	out->rigJoints = trm->rigJoints;

	if (!trm->blockBind || !trm->blockParent || !trm->blockColumn ||
		trm->blockColumns <= 0 || trm->blockNodes <= 0)
	{
		*why = "the file carries no joint block";
		return false;
	}

	if (trm->blockColumns > HD_LIPS_MAX_COLUMNS)
	{
		*why = "the block has more columns than a face is given";
		return false;
	}

	const int columns = trm->blockColumns;
	const int nodes = trm->blockNodes;

	out->columns = columns;

	/* Which rig joint each column drives, and which column is its parent.
	 * A node below the rig's joint count IS that joint (hd_trm.h). */
	for (int c = 0; c < columns; c++)
	{
		const int node = trm->blockColumn[c];

		if (node < 0 || node >= nodes)
		{
			*why = "a column names a node the header does not have";
			return false;
		}

		out->joint[c] = node < trm->rigJoints ? node : -1;
		out->parent[c] = -1;

		const int up = trm->blockParent[node];

		for (int p = 0; p < columns; p++)
			if (p != c && trm->blockColumn[p] == up)
			{
				out->parent[c] = p;
				break;
			}

		if (out->joint[c] >= 0)
			out->driven++;
	}

	/* Parents first. A column's depth is the number of columns above it; a
	 * chain longer than the column count is a loop. */
	int depth[HD_LIPS_MAX_COLUMNS];
	int deepest = 0;

	for (int c = 0; c < columns; c++)
	{
		int d = 0;

		for (int at = out->parent[c]; at >= 0; at = out->parent[at])
			if (++d > columns)
			{
				*why = "the columns' parents form a loop";
				return false;
			}

		depth[c] = d;

		if (d > deepest)
			deepest = d;
	}

	int placed = 0;

	for (int d = 0; d <= deepest; d++)
		for (int c = 0; c < columns; c++)
			if (depth[c] == d)
				out->order[placed++] = c;

	/* The head joint: the rig joint the driven columns hang off where their
	 * parent is no column - LARA_HEAD's joint 4, BLUE_GUARD_HEAD's, DEMON_
	 * HEAD's and the VON_CROY faces' joint 1, joint 0 in ADMIRAL_HEAD,
	 * PATRICK_*, PIERRE_HEAD, LARSON_HEAD and SERGIE_HEAD, HANGMAN_HEAD's
	 * joint 2. The most named one; the lowest on a tie. */
	{
		int votes[256];

		std::memset(votes, 0, sizeof(votes));

		for (int c = 0; c < columns; c++)
		{
			if (out->joint[c] < 0 || out->parent[c] >= 0)
				continue;

			const int up = trm->blockParent[trm->blockColumn[c]];

			if (up >= 0 && up < trm->rigJoints && up < 256)
				votes[up]++;
		}

		for (int j = 0; j < trm->rigJoints && j < 256; j++)
			if (votes[j] && (out->headJoint < 0 || votes[j] > votes[out->headJoint]))
				out->headJoint = j;
	}

	if (out->headJoint < 0)
	{
		*why = "no driven column hangs off a joint of the rig";
		return false;
	}

	out->axis = HD_LIPS_AXIS_IDENTITY;

	for (size_t i = 0; i < sizeof(g_axis) / sizeof(g_axis[0]); i++)
		if (stem && same_name(stem, g_axis[i].stem))
			out->axis = g_axis[i].axis;

	if (out->axis == HD_LIPS_AXIS_REFUSED)
	{
		*why = "its vertices match no axis order of its rig";
		return false;
	}

	/* The head joint in the vertices' axes, (x, -z, y) of the rig's own:
	 * LARA_HEAD's joint 4 is (1, -12, 247) in the rig and (1, -247, -12)
	 * among the vertices. */
	{
		const float* r = trm->rigJoint + out->headJoint * 3;

		out->head[0] = (double)r[0];
		out->head[1] = -(double)r[2];
		out->head[2] = (double)r[1];
	}

	/* The bind: frame 0 of the block, composed, and inverted. */
	double bind[HD_LIPS_MAX_COLUMNS * 12];

	HDLipsCompose(out, trm->blockBind, bind);

	for (int c = 0; c < columns; c++)
		if (!invert(bind + c * 12, out->bindInverse[c]))
		{
			*why = "a column's bind cannot be inverted";
			out->axis = HD_LIPS_AXIS_REFUSED;
			return false;
		}

	return true;
}

void HDLipsCompose(const HDLipsMap* map, const float* frame, double* global)
{
	if (!map || !frame || !global)
		return;

	for (int i = 0; i < map->columns; i++)
	{
		const int c = map->order[i];
		double local[12];

		from_floats(frame + c * 12, local);

		if (map->parent[c] >= 0)
			multiply(global + map->parent[c] * 12, local, global + c * 12);
		else
			std::memcpy(global + c * 12, local, sizeof(local));
	}
}

void HDLipsSkins(const HDLipsMap* map, const float* frame, double amount,
	double* skins)
{
	if (!map || !skins)
		return;

	for (int j = 0; j < map->rigJoints; j++)
		identity(skins + j * 12);

	if (!frame || map->columns <= 0)
		return;

	double global[HD_LIPS_MAX_COLUMNS * 12];
	double a[9];

	HDLipsCompose(map, frame, global);
	axis_matrix(map->axis, a);

	const double* h = map->head;

	for (int c = 0; c < map->columns; c++)
	{
		const int j = map->joint[c];

		if (j < 0 || j >= map->rigJoints)
			continue;

		double d[12];

		multiply(global + c * 12, map->bindInverse[c], d);

		/* A * D * A^T, then about the head joint:
		 * T(h) * X * T(-h) keeps X's turn and moves its translation to
		 * h + t - R h. */
		double r[9], t[3];

		for (int row = 0; row < 3; row++)
		{
			for (int col = 0; col < 3; col++)
			{
				double sum = 0.0;

				for (int k = 0; k < 3; k++)
					for (int l = 0; l < 3; l++)
						sum += a[row * 3 + k] * d[k * 4 + l] * a[col * 3 + l];

				r[row * 3 + col] = sum;
			}

			t[row] = a[row * 3 + 0] * d[3] + a[row * 3 + 1] * d[7] +
				a[row * 3 + 2] * d[11];
		}

		double* s = skins + j * 12;

		for (int row = 0; row < 3; row++)
		{
			for (int col = 0; col < 3; col++)
				s[row * 4 + col] = r[row * 3 + col];

			s[row * 4 + 3] = h[row] + t[row] - (r[row * 3 + 0] * h[0] +
				r[row * 3 + 1] * h[1] + r[row * 3 + 2] * h[2]);
		}

		/* The ease: the identity plus `amount` of the way to the frame.
		 * Linear in the matrix, so every vertex moves on the straight line
		 * between where the frame puts it and the bind. */
		if (amount != 1.0)
		{
			double one[12];

			identity(one);

			for (int i = 0; i < 12; i++)
				s[i] = one[i] + amount * (s[i] - one[i]);
		}
	}
}

/* ---------------------------------------------------- what is shown -- */

int HDLipsSourceOf(int frame, int frames, int* clipFrame, double* amount)
{
	if (clipFrame)
		*clipFrame = 0;
	if (amount)
		*amount = 0.0;

	if (frames <= 0)
		return HD_LIPS_SOURCE_BIND;

	if (frame < 0)
		frame = 0;

	if (frame < frames)
	{
		if (clipFrame)
			*clipFrame = frame;
		if (amount)
			*amount = 1.0;
		return HD_LIPS_SOURCE_TRL;
	}

	const int past = frame - (frames - 1);

	if (past >= HD_LIPS_EASE_FRAMES)
		return HD_LIPS_SOURCE_BIND;

	if (clipFrame)
		*clipFrame = frames - 1;
	if (amount)
		*amount = (double)(HD_LIPS_EASE_FRAMES - past) / (double)HD_LIPS_EASE_FRAMES;

	return HD_LIPS_SOURCE_EASE;
}

const char* HDLipsSourceName(int source)
{
	switch (source)
	{
	case HD_LIPS_SOURCE_TRL: return "trl";
	case HD_LIPS_SOURCE_EASE: return "ease";
	default: return "bind";
	}
}

/* ----------------------------------------------------------- the deform -- */

double HDLipsDeform(const double* skins, int rigJoints, int vertices,
	const float* position, const unsigned char* normal,
	const unsigned char* bone, const unsigned char* weight,
	float* outPosition, unsigned char* outNormal)
{
	if (!skins || rigJoints <= 0 || vertices <= 0 || !position || !normal ||
		!bone || !weight || !outPosition || !outNormal)
		return 0.0;

	/* Which skins move at all. A vertex weighted only to joints that do not
	 * - the head, the body, a joint no column drives - is copied, so the
	 * bind comes back bit for bit and the throat's share on the body stays
	 * where the file put it. */
	static const int kFlags = 256;
	bool moves[kFlags];
	double one[12];

	identity(one);

	for (int j = 0; j < kFlags; j++)
		moves[j] = j < rigJoints &&
			std::memcmp(skins + j * 12, one, sizeof(one)) != 0;

	double farthest = 0.0;

	for (int i = 0; i < vertices; i++)
	{
		const float* p = position + i * 3;
		const unsigned char* n = normal + i * 3;
		const unsigned char* b = bone + i * 3;
		const unsigned char* w = weight + i * 3;
		float* op = outPosition + i * 3;
		unsigned char* on = outNormal + i * 3;

		if (!(w[0] && moves[b[0]]) && !(w[1] && moves[b[1]]) &&
			!(w[2] && moves[b[2]]))
		{
			op[0] = p[0];
			op[1] = p[1];
			op[2] = p[2];
			on[0] = n[0];
			on[1] = n[1];
			on[2] = n[2];
			continue;
		}

		double m[12];

		std::memset(m, 0, sizeof(m));

		for (int k = 0; k < 3; k++)
		{
			if (!w[k])
				continue;

			const double share = (double)w[k] / 255.0;
			const double* s = (int)b[k] < rigJoints ? skins + b[k] * 12 : one;

			for (int e = 0; e < 12; e++)
				m[e] += share * s[e];
		}

		const double x = (double)p[0], y = (double)p[1], z = (double)p[2];
		double q[3];

		for (int row = 0; row < 3; row++)
			q[row] = m[row * 4 + 0] * x + m[row * 4 + 1] * y +
				m[row * 4 + 2] * z + m[row * 4 + 3];

		const double dx = q[0] - x, dy = q[1] - y, dz = q[2] - z;
		const double moved = std::sqrt(dx * dx + dy * dy + dz * dz);

		if (moved > farthest)
			farthest = moved;

		op[0] = (float)q[0];
		op[1] = (float)q[1];
		op[2] = (float)q[2];

		/* The normal through the cofactor matrix - the rows of M's 3x3 are
		 * a, b and c, and the cofactor's rows are b x c, c x a and a x b -
		 * brought back to the normal's own length, so a normal the file
		 * stores shorter than one stays that short. */
		const double nx = ((double)n[0] - 127.0) / 128.0;
		const double ny = ((double)n[1] - 127.0) / 128.0;
		const double nz = ((double)n[2] - 127.0) / 128.0;
		const double* a = m;
		const double* bb = m + 4;
		const double* c = m + 8;

		const double bc[3] =
		{
			bb[1] * c[2] - bb[2] * c[1],
			bb[2] * c[0] - bb[0] * c[2],
			bb[0] * c[1] - bb[1] * c[0]
		};
		const double ca[3] =
		{
			c[1] * a[2] - c[2] * a[1],
			c[2] * a[0] - c[0] * a[2],
			c[0] * a[1] - c[1] * a[0]
		};
		const double ab[3] =
		{
			a[1] * bb[2] - a[2] * bb[1],
			a[2] * bb[0] - a[0] * bb[2],
			a[0] * bb[1] - a[1] * bb[0]
		};

		double tx = bc[0] * nx + bc[1] * ny + bc[2] * nz;
		double ty = ca[0] * nx + ca[1] * ny + ca[2] * nz;
		double tz = ab[0] * nx + ab[1] * ny + ab[2] * nz;

		const double before = std::sqrt(nx * nx + ny * ny + nz * nz);
		const double after = std::sqrt(tx * tx + ty * ty + tz * tz);

		if (after > 0.0)
		{
			tx *= before / after;
			ty *= before / after;
			tz *= before / after;
		}

		on[0] = pack_normal(tx);
		on[1] = pack_normal(ty);
		on[2] = pack_normal(tz);
	}

	return farthest;
}
