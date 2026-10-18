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
 *   wideBare 0.3   JOBY2's broken pillar beside the portal of rooms 123
 *                  and 124 is between 0.3 and 0.4 bare on room 124's side:
 *                  at 0.3 that portal widens - from room 123 by the record
 *                  leading back (the wides below) - and room 99 is listed
 *                  through the break (431..748 at 1,280); the wide portals
 *                  of the fifteen levels by their own faces are 53 at 0.5,
 *                  55 at 0.4, 64 at 0.3, 82 at 0.2 and 113 at 0.1, and 55
 *                  more at 0.3 by the record leading back
 *   edge 2/2560    of the 181 rooms the owner's walk lost for one frame,
 *                  walked again at the frame's eye, 155 were refused with
 *                  an empty window, 71 of the 102 that could be measured
 *                  standing 2 pixels or less off it at 2,560 (49 touching
 *                  it); 2 pixels lists 81 of the 181 there again, 4 and 8
 *                  only 85 and 90; with the wides read from both records
 *                  85, against 11 with the edge at 0 */
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
	p.edge = 2.0 / 2560.0;
	p.staticReach = 32.0;
	p.staticStep = HD_COVER_STEP;
	p.staticShare = 0.5;
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
	t.enclosedBy.assign((size_t)t.rooms, -1);
	t.enclosedTris.assign((size_t)t.rooms, 0);

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

	/* The rooms standing inside another (HDVisClassicRoomEnclosed): per
	 * room with an inside, the groups of the rooms linked with it whose
	 * whole box holds that inside, and below the triangles of each one's
	 * art in it, every triangle counted. */
	std::vector<std::vector<int> > around(n);
	std::unordered_map<long long, int> enclosed;

	for (int h = 0; h < roomCount; h++)
	{
		const HDVisRoom& in = rooms[h];

		if (in.xs < 3 || in.zs < 3)
			continue;

		const long x0 = in.x + 1024, x1 = in.x + ((long)(in.xs - 1) << 10);
		const long z0 = in.z + 1024, z1 = in.z + ((long)(in.zs - 1) << 10);

		for (int g = 0; g < groups; g++)
		{
			const HDVisRoom& big = rooms[g];

			if (Twins(rooms, g, h) || !linked(h, g) || big.x > x0 ||
				big.x + ((long)big.xs << 10) < x1 || big.z > z0 ||
				big.z + ((long)big.zs << 10) < z1 || big.top > in.top ||
				big.bottom < in.bottom)
				continue;

			around[(size_t)h].push_back(g);
		}
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

					for (size_t i = 0; cell != cells.end() &&
						i < cell->second.size(); i++)
					{
						const int h = cell->second[i];
						const HDVisRoom& host = rooms[h];
						const std::vector<int>& ring = around[(size_t)h];

						if (std::find(ring.begin(), ring.end(), g) ==
							ring.end() ||
							cw[0] <= (double)(host.x + 1024) ||
							cw[0] >= (double)(host.x +
								((long)(host.xs - 1) << 10)) ||
							cw[2] <= (double)(host.z + 1024) ||
							cw[2] >= (double)(host.z +
								((long)(host.zs - 1) << 10)) ||
							cw[1] < (double)host.top ||
							cw[1] > (double)host.bottom)
							continue;

						enclosed[(long long)h * 65536 + g]++;
					}
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

	// The room each room stands inside: the most triangles, then the
	// lowest group.
	for (const std::pair<const long long, int>& one : enclosed)
	{
		const size_t h = (size_t)(one.first / 65536);
		const int g = (int)(one.first % 65536);

		if (one.second < params.guestTris ||
			one.second < t.enclosedTris[h] ||
			(one.second == t.enclosedTris[h] && g > t.enclosedBy[h]))
			continue;

		t.enclosedBy[h] = g;
		t.enclosedTris[h] = one.second;
	}

	for (int r = 0; r < roomCount; r++)
		if (t.enclosedBy[(size_t)r] >= 0)
			t.roomsEnclosed++;

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
	 * the art wider than itself.
	 *
	 * A portal is two records, one in each room, and the hole the art
	 * leaves round it is one hole: a record takes in the bare faces of the
	 * record that leads back (the same plane, the rectangles touching) when
	 * that one is wide, so the walk widens the window from either side.
	 * Only the faces of one side may be bare - JOBY2's broken pillar beside
	 * the portal of rooms 123 and 124 leaves room 124's faces bare and not
	 * room 123's - and from room 123 the break shows room 124 and room 99
	 * behind it. Only the faces: the two records of one portal need not
	 * span the same rectangle. Per portal record, world: the union, the
	 * portal, the bare faces taken; axis -1 for one off the axes. */
	const int pc = portals && portalCount > 0 ? portalCount : 0;
	std::vector<PlaneRect> grown((size_t)pc), plain((size_t)pc);
	std::vector<PlaneRect> faceOnly((size_t)pc);
	std::vector<double> planeAt((size_t)pc, 0.0);
	std::vector<int> axisOf((size_t)pc, -1);

	for (int r = 0; r < roomCount && portals; r++)
	{
		const HDVisRoom& own = rooms[r];

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
			PlaneRect only = { 1e30, -1e30, 1e30, -1e30 };
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
						only.u0 = std::min(only.u0, bare[k].u0);
						only.u1 = std::max(only.u1, bare[k].u1);
						only.w0 = std::min(only.w0, bare[k].w0);
						only.w1 = std::max(only.w1, bare[k].w1);
						grew = true;
					}
			}

			const double ou = origin[ua], ow = origin[wa];

			axisOf[(size_t)at] = axis;
			planeAt[(size_t)at] = plane + origin[axis];
			grown[(size_t)at] = { u.u0 + ou, u.u1 + ou, u.w0 + ow, u.w1 + ow };
			faceOnly[(size_t)at] = { only.u0 + ou, only.u1 + ou, only.w0 + ow,
				only.w1 + ow };
			plain[(size_t)at] = { portalRect.u0 + ou, portalRect.u1 + ou,
				portalRect.w0 + ow, portalRect.w1 + ow };
		}
	}

	const auto passes = [&](const PlaneRect& u, const PlaneRect& own) {
		return own.u0 - u.u0 >= params.step || u.u1 - own.u1 >= params.step ||
			own.w0 - u.w0 >= params.step || u.w1 - own.w1 >= params.step;
	};

	for (int r = 0; r < roomCount && portals; r++)
	{
		const HDVisRoom& own = rooms[r];

		t.wideFirst[(size_t)r] = (int)t.wides.size();

		for (int i = 0; i < own.portalCount; i++)
		{
			const int at = own.firstPortal + i;

			if (at < 0 || at >= pc || axisOf[(size_t)at] < 0)
				continue;

			const HDVisPortal& p = portals[at];
			const int axis = axisOf[(size_t)at];
			const int ua = (axis + 1) % 3;
			const int wa = (axis + 2) % 3;
			const PlaneRect mine = grown[(size_t)at];
			PlaneRect u = mine;
			const int s = p.to;

			for (int j = 0; s >= 0 && s < roomCount &&
				j < rooms[s].portalCount; j++)
			{
				const int back = rooms[s].firstPortal + j;

				if (back < 0 || back >= pc || portals[back].to != r ||
					axisOf[(size_t)back] != axis ||
					fabs(planeAt[(size_t)back] - planeAt[(size_t)at]) > 2.0 ||
					!Touches(plain[(size_t)back], plain[(size_t)at], 2.0))
					continue;

				const PlaneRect& o = faceOnly[(size_t)back];

				if (!passes(grown[(size_t)back], plain[(size_t)back]))
					continue;

				u.u0 = std::min(u.u0, o.u0);
				u.u1 = std::max(u.u1, o.u1);
				u.w0 = std::min(u.w0, o.w0);
				u.w1 = std::max(u.w1, o.w1);
			}

			if (!passes(u, plain[(size_t)at]))
				continue;

			if (!passes(mine, plain[(size_t)at]))
				t.wideMirrored++;

			HDVisWide wide;
			const double ou = (double)(ua == 0 ? own.x : ua == 1 ? own.y :
				own.z);
			const double ow = (double)(wa == 0 ? own.x : wa == 1 ? own.y :
				own.z);
			const double cu[4] = { u.u0 - ou, u.u1 - ou, u.u1 - ou, u.u0 - ou };
			const double cwv[4] = { u.w0 - ow, u.w0 - ow, u.w1 - ow,
				u.w1 - ow };

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

void HDVisStaticCovers(const trf::remaster::Trg& trg, const HDVisRoom* rooms,
	int roomCount, const HDVisStatic* statics, int staticCount,
	const HDCoverFace* faces, int faceCount, const HDVisParams& params,
	HDVisTables* out)
{
	HDVisTables& t = *out;
	const int count = statics && staticCount > 0 ? staticCount : 0;

	t.staticGroup.assign((size_t)count, -1);
	t.staticShare.assign((size_t)count, 0.0f);
	t.staticsHeld = 0;

	if (!count || !faces || faceCount <= 0 || !rooms || roomCount <= 0)
		return;

	const int groups = (int)trg.groups.size() < roomCount ?
		(int)trg.groups.size() : roomCount;

	/* The statics their own room's art holds (HDStaticCellsHold): the
	 * 256-unit cells of the box that hold a vertex of the room's group,
	 * room local, as the renderer's build notes them - per room, its
	 * group's cells once. */
	std::vector<unsigned char> ask((size_t)count, 0);
	std::unordered_map<int, std::vector<long long> > roomCells;

	for (int s = 0; s < count; s++)
	{
		const HDVisStatic& one = statics[s];

		if (one.faceCount <= 0 || one.room < 0 || one.room >= groups)
			continue;

		std::vector<long long>& cells = roomCells[one.room];
		const std::vector<trf::remaster::TrgPart>& parts =
			trg.groups[(size_t)one.room].parts;

		const bool fresh = cells.empty();

		for (size_t q = 0; fresh && q < parts.size(); q++)
			for (int pass = 0; pass < 3; pass++)
			{
				const trf::remaster::TrgPart& part = parts[q];
				const trf::remaster::TrgPart::Pass& run = part.pass[pass];

				for (unsigned at = 0; at < run.count; at++)
				{
					const unsigned i = run.start + at;

					if (i >= trg.indices.size() ||
						trg.indices[i] >= trg.vertices.size())
						break;

					const trf::remaster::TrgVertex& tv =
						trg.vertices[trg.indices[i]];

					cells.push_back((((long long)(tv.x >> 8) + 4096) << 26) |
						(((long long)(tv.y >> 8) + 4096) << 13) |
						((long long)(tv.z >> 8) + 4096));
				}
			}

		if (fresh)
		{
			std::sort(cells.begin(), cells.end());
			cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
			cells.push_back(-1);	// sorted: the end of a room's list
		}

		int held = 0;

		for (const long long key : cells)
		{
			bool in = key >= 0;

			for (int k = 0; k < 3 && in; k++)
			{
				const float p = (float)(((key >> (26 - 13 * k)) & 8191) -
					4096) * 256.0f;

				in = p + 255.0f >= one.mins[k] && p <= one.maxs[k];
			}

			held += in ? 1 : 0;
		}

		int boxCells = 1;

		for (int k = 0; k < 3; k++)
			boxCells *= ((int)one.maxs[k] >> 8) - ((int)one.mins[k] >> 8) + 1;

		ask[(size_t)s] = HDStaticCellsHold(held, boxCells) ? 0 : 1;
	}

	/* Each static asked: its box in world units, grown by the reach, and
	 * the groups whose room's box holds its centre - the art of the room it
	 * stands in, its own room's or another's. */
	const double reach = params.staticReach;
	std::vector<double> box((size_t)count * 6);
	std::vector<std::vector<int> > near((size_t)groups);

	for (int s = 0; s < count; s++)
	{
		double* b = &box[(size_t)s * 6];
		const HDVisStatic& one = statics[s];

		b[0] = b[1] = b[2] = 1e30;
		b[3] = b[4] = b[5] = -1e30;

		if (!ask[(size_t)s])
			continue;

		for (int f = one.firstFace; f < one.firstFace + one.faceCount &&
			f >= 0 && f < faceCount; f++)
			for (int c = 0; c < faces[f].corners; c++)
				for (int k = 0; k < 3; k++)
				{
					b[k] = faces[f].v[c][k] < b[k] ? faces[f].v[c][k] : b[k];
					b[3 + k] = faces[f].v[c][k] > b[3 + k] ?
						faces[f].v[c][k] : b[3 + k];
				}

		if (b[0] > b[3])
			continue;

		const double centre[3] = { (b[0] + b[3]) / 2.0, (b[1] + b[4]) / 2.0,
			(b[2] + b[5]) / 2.0 };

		for (int k = 0; k < 3; k++)
		{
			b[k] -= reach;
			b[3 + k] += reach;
		}

		for (int g = 0; g < groups; g++)
		{
			const HDVisRoom& r = rooms[g];

			if (centre[0] >= (double)r.x &&
				centre[0] <= (double)(r.x + ((long)r.xs << 10)) &&
				centre[2] >= (double)r.z &&
				centre[2] <= (double)(r.z + ((long)r.zs << 10)) &&
				centre[1] >= (double)r.top && centre[1] <= (double)r.bottom)
				near[(size_t)g].push_back(s);
		}
	}

	/* Per static and group, the group's triangles whose box meets the
	 * static's. */
	std::vector<std::unordered_map<int, HDCoverSet> > sets((size_t)count);

	for (int g = 0; g < groups; g++)
	{
		if (near[(size_t)g].empty())
			continue;

		const HDVisRoom& own = rooms[g];
		const float origin[3] = { (float)own.x, (float)own.y, (float)own.z };

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
					double lo[3] = { 1e30, 1e30, 1e30 };
					double hi[3] = { -1e30, -1e30, -1e30 };

					for (int c = 0; c < 3; c++)
					{
						const trf::remaster::TrgVertex& tv =
							trg.vertices[trg.indices[first + (unsigned)c]];

						v[c][0] = (float)tv.x;
						v[c][1] = (float)tv.y;
						v[c][2] = (float)tv.z;

						for (int k = 0; k < 3; k++)
						{
							const double w = (double)v[c][k] + origin[k];

							lo[k] = w < lo[k] ? w : lo[k];
							hi[k] = w > hi[k] ? w : hi[k];
						}
					}

					for (const int s : near[(size_t)g])
					{
						const double* b = &box[(size_t)s * 6];
						HDCoverTri tri;

						if (hi[0] < b[0] || lo[0] > b[3] || hi[1] < b[1] ||
							lo[1] > b[4] || hi[2] < b[2] || lo[2] > b[5])
							continue;

						if (HDCoverMakeTri(v, origin, g, &tri))
							sets[(size_t)s][g].tris.push_back(tri);
					}
				}
			}
	}

	/* The share per static and group: the static's samples a triangle of
	 * the group's covers, facing either way. The most first, then the
	 * lowest group. */
	for (int s = 0; s < count; s++)
	{
		const HDVisStatic& one = statics[s];
		std::vector<int> order;

		for (const auto& kv : sets[(size_t)s])
			order.push_back(kv.first);

		std::sort(order.begin(), order.end());

		for (const int g : order)
		{
			HDCoverSet& set = sets[(size_t)s][g];
			std::vector<int> mark(set.tris.size(), 0);
			int stamp = 0;
			long long samples = 0, covered = 0;

			set.build();

			for (int f = one.firstFace; f < one.firstFace + one.faceCount &&
				f >= 0 && f < faceCount; f++)
				samples += HDCoverSamples(faces[f], params.staticStep,
					[&](const double* p, const double* nrm)
					{
						if (set.hits(p, nrm, reach, 0, mark, ++stamp))
							covered++;
					});

			const float share = samples ?
				(float)((double)covered / (double)samples) : 0.0f;

			if (share > t.staticShare[(size_t)s])
			{
				t.staticShare[(size_t)s] = share;
				t.staticGroup[(size_t)s] = g;
			}
		}

		if (t.staticGroup[(size_t)s] >= 0 &&
			(double)t.staticShare[(size_t)s] >= params.staticShare)
			t.staticsHeld++;
	}
}

namespace
{
	std::vector<HDVisRoom> g_in_rooms;
	std::vector<HDCoverFace> g_in_faces;
	std::vector<HDVisPortal> g_in_portals;
	std::vector<int> g_in_links;
	std::vector<HDVisStatic> g_in_statics;
	std::vector<HDCoverFace> g_in_static_faces;
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

void HDVisLevelStatics(const HDVisStatic* statics, int staticCount,
	const HDCoverFace* faces, int faceCount)
{
	g_in_statics.clear();
	g_in_static_faces.clear();

	if (statics && staticCount > 0)
		g_in_statics.assign(statics, statics + staticCount);

	if (faces && faceCount > 0)
		g_in_static_faces.assign(faces, faces + faceCount);
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

	if (g_built && !g_in_statics.empty())
	{
		const auto started = std::chrono::steady_clock::now();

		HDVisStaticCovers(trg, &g_in_rooms[0], (int)g_in_rooms.size(),
			&g_in_statics[0], (int)g_in_statics.size(),
			g_in_static_faces.empty() ? 0 : &g_in_static_faces[0],
			(int)g_in_static_faces.size(), HDVisDefaults(), &g_tables);
		g_tables.staticMs = std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - started).count();
	}

	// The inputs were only for the build.
	std::vector<HDCoverFace>().swap(g_in_faces);
	std::vector<HDCoverFace>().swap(g_in_static_faces);
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
	std::vector<HDVisStatic>().swap(g_in_statics);
	std::vector<HDCoverFace>().swap(g_in_static_faces);
}

const HDVisTables* HDVisLevelTables()
{
	return g_built ? &g_tables : 0;
}
