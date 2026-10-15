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

/* The visibility path's level tables - see hd_vis.h. Built once per level
 * out of the .TRG the renderer's build has open and the classic side
 * hd_level.cpp hands in; no Direct3D and no engine state, so RoomProbe and
 * TrgProbe link the same code. */

#include "hd_vis.h"
#include "../../vendor/trforge/src/trf_remaster/Trg.h"

#include <math.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <unordered_map>

/* The numbers, measured over the fifteen levels with TrgProbe -vistable:
 *
 *   reach 128      HD_COVER_REACH. Of the 42,971 wall faces a room's own
 *                  art covers wholly within 512 units, 91.4 per cent are
 *                  covered wholly within 32, 95.8 within 128 and 97.7
 *                  within 256: the art of a wall stands off its classic
 *                  plane, and 128 is the stand rule's reach
 *   step 64        HD_COVER_STEP
 *   bare 0.5       a free choice: a face half bare or more is an opening
 *   guestTris 8    the count TROne's TR2 visibility prototype names a
 *                  guest by (see hd_vis.h); here 2,671 (holder, group)
 *                  pairs have a triangle in the holder's inside, 2,377
 *                  eight or more, and the 294 under eight are stray
 *                  triangles
 *   guestRule 2    art reaching out of its own room, into a room under
 *                  the sky or a linked one - see the guests below
 *   overhang 128   HD_OVERHANG_SLACK: the relief of a wall stands within
 *                  64 units of its room's box, a facade or a pier a
 *                  sector past it; JOBY2's room 24 has the slab of its
 *                  ceiling 32 units over its box, inside room 31
 *   behind 512     half a sector: the middle of the wall ring behind the
 *                  face, which the room behind shares with the holder
 *   artShare 0.5   a free choice: 2,483 of the 2,709 rooms have their
 *                  walls asked; the rest have no group, an empty or a
 *                  hollow one, and every face of theirs would be bare
 *   wideBare 0.3   JOBY2's broken pillar beside room 124's portal to
 *                  room 99 is between 0.3 and 0.4 bare: at 0.3 its portal
 *                  widens and room 99 is listed through the break; the wide
 *                  portals of the fifteen levels are 53 at 0.5, 55 at 0.4,
 *                  64 at 0.3, 82 at 0.2 and 113 at 0.1 */
HDVisParams HDVisDefaults()
{
	HDVisParams p;

	p.reach = HD_COVER_REACH;
	p.step = HD_COVER_STEP;
	p.bare = 0.5;
	p.guestTris = 8;
	p.behind = 512.0;
	p.artShare = 0.5;
	p.guestRule = 2;
	p.overhang = 128.0;
	p.wideBare = 0.3;
	return p;
}

namespace
{
	// One holder's guest while the triangles are counted.
	struct GuestAcc
	{
		int tris = 0;
		float lo[3] = { 1e30f, 1e30f, 1e30f };
		float hi[3] = { -1e30f, -1e30f, -1e30f };
	};

	long long CellKey(long sx, long sz)
	{
		return ((long long)(sx + 1048576) << 21) | (long long)(sz + 1048576);
	}

	// Floor division by a sector, for coordinates either side of zero.
	long SectorOf(double v)
	{
		return (long)floor(v / 1024.0);
	}

	// The unit normal (b - a) x (c - a) of a face's first half: it points
	// away from the side the face is seen from (hd_cover.h).
	bool FaceNormal(const HDCoverFace& f, double n[3])
	{
		const double* a = f.v[0];
		const double* b = f.v[1];
		const double* c = f.v[2];
		const double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
		const double e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };

		n[0] = e1[1] * e2[2] - e1[2] * e2[1];
		n[1] = e1[2] * e2[0] - e1[0] * e2[2];
		n[2] = e1[0] * e2[1] - e1[1] * e2[0];

		const double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);

		if (len <= 0.0)
			return false;

		for (int k = 0; k < 3; k++)
			n[k] /= len;

		return true;
	}

	short ToShort(double v)
	{
		if (v > 32767.0)
			return 32767;

		if (v < -32768.0)
			return -32768;

		return (short)(v < 0.0 ? v - 0.5 : v + 0.5);
	}

	bool InBox(const HDVisRoom& r, const double p[3])
	{
		return p[0] >= (double)r.x &&
			p[0] <= (double)(r.x + ((long)r.xs << 10)) &&
			p[2] >= (double)r.z &&
			p[2] <= (double)(r.z + ((long)r.zs << 10)) &&
			p[1] >= (double)r.top && p[1] <= (double)r.bottom;
	}

	// Two rooms of one flipmap pair stand at one place: never each other's.
	bool Twins(const HDVisRoom* rooms, int a, int b)
	{
		return a == b || rooms[a].twin == b || rooms[b].twin == a;
	}

	/* A rectangle in a portal's plane: `u` and `w` are the two axes that are
	 * not the plane's. */
	struct PlaneRect
	{
		double u0, u1, w0, w1;
	};

	bool Touches(const PlaneRect& a, const PlaneRect& b, double slack)
	{
		return a.u0 <= b.u1 + slack && b.u0 <= a.u1 + slack &&
			a.w0 <= b.w1 + slack && b.w0 <= a.w1 + slack;
	}
}

bool HDVisBuild(const trf::remaster::Trg& trg, const HDVisRoom* rooms,
	int roomCount, const HDCoverFace* faces, int faceCount,
	const HDVisPortal* portals, int portalCount, const int* links,
	int linkCount, const HDVisParams& params, HDVisTables* out)
{
	const auto started = std::chrono::steady_clock::now();
	HDVisTables& t = *out;

	t = HDVisTables();
	t.rooms = roomCount > 0 ? roomCount : 0;
	t.guestFirst.assign((size_t)t.rooms, 0);
	t.guestCount.assign((size_t)t.rooms, 0);
	t.openFirst.assign((size_t)t.rooms, 0);
	t.openCount.assign((size_t)t.rooms, 0);
	t.wideFirst.assign((size_t)t.rooms, 0);
	t.wideCount.assign((size_t)t.rooms, 0);
	t.faceBare.assign((size_t)(faceCount > 0 ? faceCount : 0), -1.0f);

	if (!rooms || roomCount <= 0)
		return false;

	const int groups = (int)trg.groups.size() < roomCount ?
		(int)trg.groups.size() : roomCount;

	/* Which rooms are linked: a portal either way or a floor link, closed
	 * over the flipmap twins - a and b are linked when a or a's twin has a
	 * link with b or b's twin. Rooms that stand in one another with no link
	 * - JOBY2's room 24 in rooms 30, 31 and 33 - are never seen together in
	 * the classic game, whose walk only follows links, and drawn together
	 * their art fills the same space: an opening that listed room 24 would
	 * draw its marble over the stairwell's art at ten stair cameras
	 * (5,316,516 pixels at 2560x1440). The openings keep a room only when
	 * it is linked with the room that lists it.
	 *
	 * The guests (`guestRule` 2): a group's art counts for a holder only
	 * where it reaches out of its own room - farther than `overhang` past
	 * the room's whole box - and a guest of a room not under the sky must
	 * be linked with it. Every triangle in the holder's inside counted
	 * (rule 0, linked guests only) brings JOBY2's room 24 into rooms 30 and
	 * 31 by the slabs of its ceiling and walls; the art reaching out of its
	 * room alone (rule 1) brings room 92's beam into ANDREA3's room 63, an
	 * enclosed room it has no link with (400,730 pixels at one camera of
	 * its courtyard), while ANDREA2's brick tower of room 222 stands over
	 * room 232, a courtyard under the sky it has no link with either, and
	 * is seen from it. */
	const size_t n = (size_t)roomCount;
	std::vector<unsigned char> link(n * n, 0);
	// Per room, the room of its flipmap pair the portals lead to.
	std::vector<int> mainOf(n, 0);

	for (int r = 0; r < roomCount; r++)
		mainOf[(size_t)r] = r;

	for (int r = 0; r < roomCount; r++)
		if (rooms[r].twin >= 0 && rooms[r].twin < roomCount)
			mainOf[(size_t)rooms[r].twin] = r;

	for (int r = 0; r < roomCount; r++)
	{
		for (int i = 0; portals && i < rooms[r].portalCount; i++)
		{
			const int at = rooms[r].firstPortal + i;
			const int to = at >= 0 && at < portalCount ? portals[at].to : -1;

			if (to >= 0 && to < roomCount)
				link[(size_t)r * n + to] = link[(size_t)to * n + r] = 1;
		}

		for (int i = 0; links && i < rooms[r].linkCount; i++)
		{
			const int at = rooms[r].firstLink + i;
			const int to = at >= 0 && at < linkCount ? links[at] : -1;

			if (to >= 0 && to < roomCount)
				link[(size_t)r * n + to] = link[(size_t)to * n + r] = 1;
		}
	}

	const auto linked = [&](int a, int b)
	{
		const int ta = rooms[a].twin >= 0 && rooms[a].twin < roomCount ?
			rooms[a].twin : a;
		const int tb = rooms[b].twin >= 0 && rooms[b].twin < roomCount ?
			rooms[b].twin : b;

		return link[(size_t)a * n + b] || link[(size_t)ta * n + b] ||
			link[(size_t)a * n + tb] || link[(size_t)ta * n + tb];
	};

	/* The rooms' insides on the sector grid: TR5's own inside of a room,
	 * its grid less the ring of wall sectors (the box InsertRoom's items
	 * must fit, game\draw.cpp), which a neighbour's wall art never enters
	 * and a room standing in it does. */
	std::unordered_map<long long, std::vector<int> > cells;

	for (int r = 0; r < roomCount; r++)
	{
		const HDVisRoom& room = rooms[r];

		if (room.xs < 3 || room.zs < 3)
			continue;

		const long sx = SectorOf((double)room.x);
		const long sz = SectorOf((double)room.z);

		for (long x = sx + 1; x < sx + room.xs - 1; x++)
			for (long z = sz + 1; z < sz + room.zs - 1; z++)
				cells[CellKey(x, z)].push_back(r);
	}

	std::unordered_map<long long, GuestAcc> acc;
	std::vector<std::vector<HDVisOpening> > open((size_t)roomCount);

	for (int g = 0; g < groups; g++)
	{
		const HDVisRoom& own = rooms[g];
		const float origin[3] = { (float)own.x, (float)own.y, (float)own.z };
		HDCoverSet set;

		for (const trf::remaster::TrgPart& part : trg.groups[(size_t)g].parts)
			for (int pass = 0; pass < 2; pass++)
			{
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned at = 0; at + 2 < run.count; at += 3)
				{
					const unsigned first = run.start + at;

					if (first + 2 >= trg.indices.size())
						break;

					float v[3][3];

					for (int c = 0; c < 3; c++)
					{
						const trf::remaster::TrgVertex& tv =
							trg.vertices[trg.indices[first + (unsigned)c]];

						v[c][0] = (float)tv.x;
						v[c][1] = (float)tv.y;
						v[c][2] = (float)tv.z;
					}

					const double cw[3] =
					{
						(v[0][0] + v[1][0] + v[2][0]) / 3.0 + own.x,
						(v[0][1] + v[1][1] + v[2][1]) / 3.0 + own.y,
						(v[0][2] + v[1][2] + v[2][2]) / 3.0 + own.z
					};
					const auto cell = cells.find(CellKey(SectorOf(cw[0]),
						SectorOf(cw[2])));
					// Under `guestRule` 1 and 2, the art reaching out of
					// its own room alone (see the link table above).
					const double slack = (double)params.overhang;
					const double x1 = (double)(own.x + ((long)own.xs << 10));
					const double z1 = (double)(own.z + ((long)own.zs << 10));
					const bool inOwn =
						cw[0] >= (double)own.x - slack && cw[0] <= x1 + slack &&
						cw[2] >= (double)own.z - slack && cw[2] <= z1 + slack &&
						cw[1] >= (double)own.top - slack &&
						cw[1] <= (double)own.bottom + slack;

					if (cell != cells.end() &&
						!(params.guestRule >= 1 && inOwn))
						for (const int h : cell->second)
						{
							const HDVisRoom& host = rooms[h];

							if (Twins(rooms, g, h) ||
								cw[0] <= (double)(host.x + 1024) ||
								cw[0] >= (double)(host.x +
									((long)(host.xs - 1) << 10)) ||
								cw[2] <= (double)(host.z + 1024) ||
								cw[2] >= (double)(host.z +
									((long)(host.zs - 1) << 10)) ||
								cw[1] < (double)host.top ||
								cw[1] > (double)host.bottom)
								continue;

							GuestAcc& a = acc[(long long)h * 65536 + g];

							a.tris++;

							for (int c = 0; c < 3; c++)
								for (int k = 0; k < 3; k++)
								{
									const float w = v[c][k] + origin[k];

									if (w < a.lo[k]) a.lo[k] = w;
									if (w > a.hi[k]) a.hi[k] = w;
								}
						}

					HDCoverTri tri;

					if (own.faceCount > 0 && HDCoverMakeTri(v, origin, g, &tri))
						set.tris.push_back(tri);
				}
			}

		if (own.faceCount <= 0 || set.tris.empty() || !faces)
			continue;

		/* The room's walls against its own art: the vertical classic faces
		 * (a normal with no y), sampled as the stand rule samples a face,
		 * a sample covered by art facing the way the face does within
		 * `reach` of it. */
		std::vector<HDCoverFace> walls;
		std::vector<int> wallAt;

		for (int f = own.firstFace; f < own.firstFace + own.faceCount &&
			f < faceCount; f++)
		{
			double n[3];

			if (FaceNormal(faces[f], n) && fabs(n[1]) < 0.5)
			{
				walls.push_back(faces[f]);
				wallAt.push_back(f);
			}
		}

		if (walls.empty())
			continue;

		set.build();

		const HDCoverCount cover = HDCoverFaces(&walls[0], (int)walls.size(),
			set, params.reach, params.step, 1);

		if (!cover.samples || (double)cover.samplesCovered <
			params.artShare * (double)cover.samples)
			continue;

		t.artRooms++;
		t.samples += cover.samples;
		t.samplesCovered += cover.samplesCovered;

		for (size_t w = 0; w < walls.size(); w++)
		{
			const double bare = 1.0 - cover.faceShare[w];
			const int f = wallAt[w];

			t.faceBare[(size_t)f] = (float)bare;
			t.wallFaces++;

			if (bare < params.bare)
				continue;

			t.bareFaces++;

			double n[3], c[3] = { 0.0, 0.0, 0.0 };
			const HDCoverFace& face = faces[f];

			FaceNormal(face, n);

			for (int k = 0; k < face.corners; k++)
				for (int a = 0; a < 3; a++)
					c[a] += face.v[k][a] / face.corners;

			const double p[3] = { c[0] + n[0] * params.behind,
				c[1] + n[1] * params.behind, c[2] + n[2] * params.behind };
			HDVisOpening one;
			int found = 0;

			one.face = f - own.firstFace;
			one.bare = (float)bare;

			for (int k = 0; k < 3; k++)
				one.n[k] = ToShort(-n[k] * 1024.0);

			for (int k = 0; k < 4; k++)
			{
				const double* q = face.v[k < face.corners ? k : 0];

				one.v[k][0] = ToShort(q[0] - (double)own.x);
				one.v[k][1] = ToShort(q[1] - (double)own.y);
				one.v[k][2] = ToShort(q[2] - (double)own.z);
			}

			/* Of a flipmap pair only the room the portals lead to is in the
			 * world - the one whose twin names the other; it draws whichever
			 * state the map is in. The other room stands at the same place,
			 * and listed it would draw its state beside the one the map
			 * shows (JOBY2's room 53 behind a bare face of room 89, its
			 * twin: both states of the crane hall at once). An opening names
			 * the pair by its main room. */
			const size_t before = open[(size_t)g].size();

			for (int s = 0; s < roomCount; s++)
			{
				if (Twins(rooms, g, s) || !InBox(rooms[s], p))
					continue;

				if (!linked(g, s))
				{
					t.openingsUnlinked++;
					continue;
				}

				const int at = mainOf[(size_t)s];
				bool again = Twins(rooms, g, at);

				for (size_t k = before;
					k < open[(size_t)g].size() && !again; k++)
					again = open[(size_t)g][k].behind == at;

				if (at != s)
					t.openingsTwin++;

				if (again)
					continue;

				one.behind = at;
				open[(size_t)g].push_back(one);
				found++;
			}

			if (!found)
				t.voidFaces++;
		}
	}

	// The guests, holder by holder, the group's order within one.
	std::vector<std::pair<long long, GuestAcc> > pairs(acc.begin(), acc.end());

	std::sort(pairs.begin(), pairs.end(),
		[](const std::pair<long long, GuestAcc>& a,
			const std::pair<long long, GuestAcc>& b)
		{ return a.first < b.first; });

	for (const std::pair<long long, GuestAcc>& one : pairs)
	{
		const int h = (int)(one.first / 65536);
		HDVisGuest guest;

		if (one.second.tris < params.guestTris)
			continue;

		if ((params.guestRule == 0 || (params.guestRule == 2 &&
			!(rooms[h].flags & 8))) && !linked(h, (int)(one.first % 65536)))
		{
			t.guestsUnlinked++;
			continue;
		}

		guest.group = (int)(one.first % 65536);
		guest.tris = one.second.tris;

		for (int k = 0; k < 3; k++)
		{
			guest.lo[k] = one.second.lo[k];
			guest.hi[k] = one.second.hi[k];
		}

		if (!t.guestCount[(size_t)h])
			t.guestFirst[(size_t)h] = (int)t.guests.size();

		t.guestCount[(size_t)h]++;
		t.guests.push_back(guest);
		t.guestPairs++;
		t.guestTris += guest.tris;
	}

	for (int r = 0; r < roomCount; r++)
	{
		t.openFirst[(size_t)r] = (int)t.openings.size();
		t.openCount[(size_t)r] = (int)open[(size_t)r].size();
		t.openings.insert(t.openings.end(), open[(size_t)r].begin(),
			open[(size_t)r].end());
	}

	/* The wides: per portal of an axis plane, the room's bare faces in that
	 * plane that touch the portal or a face already taken, unioned with
	 * the portal's own rectangle. A portal whose union passes its own
	 * rectangle by a sample's spacing or more on a side has an opening in
	 * the art wider than itself. */
	for (int r = 0; r < roomCount && portals; r++)
	{
		const HDVisRoom& own = rooms[r];

		t.wideFirst[(size_t)r] = (int)t.wides.size();

		for (int i = 0; i < own.portalCount; i++)
		{
			const int at = own.firstPortal + i;

			if (at < 0 || at >= portalCount)
				continue;

			const HDVisPortal& p = portals[at];
			int axis = -1, axes = 0;

			for (int k = 0; k < 3; k++)
				if (p.n[k])
				{
					axis = k;
					axes++;
				}

			if (axes != 1)
				continue;

			const int ua = (axis + 1) % 3;
			const int wa = (axis + 2) % 3;
			const double plane = (double)p.v[0][axis];
			PlaneRect u = { 1e30, -1e30, 1e30, -1e30 };

			for (int k = 0; k < 4; k++)
			{
				u.u0 = std::min(u.u0, (double)p.v[k][ua]);
				u.u1 = std::max(u.u1, (double)p.v[k][ua]);
				u.w0 = std::min(u.w0, (double)p.v[k][wa]);
				u.w1 = std::max(u.w1, (double)p.v[k][wa]);
			}

			const PlaneRect portalRect = u;
			const double origin[3] = { (double)own.x, (double)own.y,
				(double)own.z };
			std::vector<PlaneRect> bare;

			// Every bare wall face of the room, with a room behind or not.
			for (int f = own.firstFace; faces &&
				f < own.firstFace + own.faceCount && f < faceCount; f++)
			{
				if (t.faceBare[(size_t)f] < (float)params.wideBare)
					continue;

				const HDCoverFace& face = faces[f];
				bool inPlane = true;
				PlaneRect q = { 1e30, -1e30, 1e30, -1e30 };

				for (int c = 0; c < face.corners; c++)
				{
					const double l[3] = { face.v[c][0] - origin[0],
						face.v[c][1] - origin[1], face.v[c][2] - origin[2] };

					inPlane = inPlane && fabs(l[axis] - plane) <= 2.0;
					q.u0 = std::min(q.u0, l[ua]);
					q.u1 = std::max(q.u1, l[ua]);
					q.w0 = std::min(q.w0, l[wa]);
					q.w1 = std::max(q.w1, l[wa]);
				}

				if (inPlane)
					bare.push_back(q);
			}

			std::vector<unsigned char> taken(bare.size(), 0);
			bool grew = true;

			while (grew)
			{
				grew = false;

				for (size_t k = 0; k < bare.size(); k++)
					if (!taken[k] && Touches(u, bare[k], 2.0))
					{
						taken[k] = 1;
						u.u0 = std::min(u.u0, bare[k].u0);
						u.u1 = std::max(u.u1, bare[k].u1);
						u.w0 = std::min(u.w0, bare[k].w0);
						u.w1 = std::max(u.w1, bare[k].w1);
						grew = true;
					}
			}

			if (portalRect.u0 - u.u0 < params.step &&
				u.u1 - portalRect.u1 < params.step &&
				portalRect.w0 - u.w0 < params.step &&
				u.w1 - portalRect.w1 < params.step)
				continue;

			HDVisWide wide;
			const double cu[4] = { u.u0, u.u1, u.u1, u.u0 };
			const double cwv[4] = { u.w0, u.w0, u.w1, u.w1 };

			wide.portal = i;

			for (int c = 0; c < 4; c++)
			{
				wide.v[c][axis] = p.v[0][axis];
				wide.v[c][ua] = ToShort(cu[c]);
				wide.v[c][wa] = ToShort(cwv[c]);
			}

			t.wides.push_back(wide);
			t.wideCount[(size_t)r]++;
			t.widePortals++;
		}
	}

	t.ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - started).count();
	return true;
}

namespace
{
	std::vector<HDVisRoom> g_in_rooms;
	std::vector<HDCoverFace> g_in_faces;
	std::vector<HDVisPortal> g_in_portals;
	std::vector<int> g_in_links;
	bool g_armed = false;
	HDVisTables g_tables;
	bool g_built = false;
}

void HDVisLevelInput(const HDVisRoom* rooms, int roomCount,
	const HDCoverFace* faces, int faceCount, const HDVisPortal* portals,
	int portalCount, const int* links, int linkCount)
{
	HDVisLevelClear();

	if (!rooms || roomCount <= 0)
		return;

	g_in_rooms.assign(rooms, rooms + roomCount);

	if (faces && faceCount > 0)
		g_in_faces.assign(faces, faces + faceCount);

	if (portals && portalCount > 0)
		g_in_portals.assign(portals, portals + portalCount);

	if (links && linkCount > 0)
		g_in_links.assign(links, links + linkCount);

	g_armed = true;
}

void HDVisLevelBuild(const trf::remaster::Trg& trg)
{
	if (!g_armed)
		return;

	g_armed = false;
	g_built = HDVisBuild(trg, &g_in_rooms[0], (int)g_in_rooms.size(),
		g_in_faces.empty() ? 0 : &g_in_faces[0], (int)g_in_faces.size(),
		g_in_portals.empty() ? 0 : &g_in_portals[0],
		(int)g_in_portals.size(), g_in_links.empty() ? 0 : &g_in_links[0],
		(int)g_in_links.size(), HDVisDefaults(), &g_tables);

	// The inputs were only for the build.
	std::vector<HDCoverFace>().swap(g_in_faces);
}

void HDVisLevelClear()
{
	g_armed = false;
	g_built = false;
	g_tables = HDVisTables();
	std::vector<HDVisRoom>().swap(g_in_rooms);
	std::vector<HDCoverFace>().swap(g_in_faces);
	std::vector<HDVisPortal>().swap(g_in_portals);
	std::vector<int>().swap(g_in_links);
}

const HDVisTables* HDVisLevelTables()
{
	return g_built ? &g_tables : 0;
}
