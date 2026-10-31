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

/* The classic side of the visibility path's tables (hd_vis.h) out of a .TRC,
 * as hd_level.cpp's BuildRoomVis hands it in out of ROOM_INFO: per room its
 * origin (header field 24 is its y), its sectors, its box in y (field 36 is
 * the highest ceiling), its flags, its flipmap twin and the map that swaps
 * the two (header byte 53, FlipNumber), its classic faces in
 * world units, its portal records room-local and its floor links (the rooms
 * its floor squares name above and below). RoomProbe and TrgProbe share
 * it. */

#include <string>
#include <vector>
#include "core/Tr5Level.h"
#include "../../../TOMB5 Code/TOMB5/specific/hd/hd_vis.h"

inline bool VisInputFromLevel(const Tr5Level& level,
	std::vector<HDVisRoom>& rooms, std::vector<HDCoverFace>& faces,
	std::vector<HDVisPortal>& portals, std::vector<int>& links)
{
	std::string error;

	rooms.clear();
	faces.clear();
	portals.clear();
	links.clear();

	for (int r = 0; r < level.roomCount(); r++)
	{
		const Tr5Room& rm = level.rooms()[(size_t)r];
		HDVisRoom one;
		unsigned yOrigin = 0, yTop = 0;
		std::vector<Tr5Level::FaceInfo> list;
		std::vector<Tr5Level::PortalRecord> doors;

		level.headerField(r, 24, yOrigin, error);
		level.headerField(r, 36, yTop, error);
		one.x = rm.originX;
		one.y = (long)(int)yOrigin;
		one.z = rm.originZ;
		one.xs = rm.numXSectors;
		one.zs = rm.numZSectors;
		one.top = (long)(int)yTop;
		one.bottom = rm.yBottom;
		one.flags = (int)rm.flags;
		one.twin = rm.altRoom;

		// The flipmap of a room naming a twin: FlipNumber, header byte 53.
		unsigned word = 0;

		one.flip = rm.altRoom >= 0 && level.headerField(r, 52, word, error) ?
			(int)((word >> 8) & 0xFF) : -1;
		one.firstFace = (int)faces.size();
		one.faceCount = 0;
		one.firstPortal = (int)portals.size();
		one.portalCount = 0;
		one.firstLink = (int)links.size();
		one.linkCount = 0;

		if (level.roomFaces(r, list, error))
			for (const Tr5Level::FaceInfo& face : list)
			{
				HDCoverFace f;

				f.corners = face.quad ? 4 : 3;
				f.doubleSided = face.doubleSided ? 1 : 0;

				for (int c = 0; c < 4; c++)
					for (int k = 0; k < 3; k++)
						f.v[c][k] = (double)face.v[c < f.corners ? c : 0][k];

				faces.push_back(f);
				one.faceCount++;
			}

		if (level.portalRecords(r, doors, error))
			for (const Tr5Level::PortalRecord& d : doors)
			{
				HDVisPortal p;

				p.to = d.toRoom;
				p.n[0] = (short)d.nx;
				p.n[1] = (short)d.ny;
				p.n[2] = (short)d.nz;

				for (int c = 0; c < 4; c++)
					for (int k = 0; k < 3; k++)
						p.v[c][k] = (short)d.v[c][k];

				portals.push_back(p);
				one.portalCount++;
			}

		std::vector<Tr5Level::SectorInfo> squares;

		if (level.sectorInfo(r, squares, error))
			for (const Tr5Level::SectorInfo& s : squares)
				for (const int to : { s.roomAbove, s.roomBelow })
					if (to >= 0 && to < level.roomCount())
					{
						links.push_back(to);
						one.linkCount++;
					}

		rooms.push_back(one);
	}

	return !rooms.empty();
}
