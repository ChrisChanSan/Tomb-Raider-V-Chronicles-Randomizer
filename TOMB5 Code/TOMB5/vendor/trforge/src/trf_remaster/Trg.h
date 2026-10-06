/* TRForge - LEVELn.TRG, the geometry the remasters actually draw.
 *
 * This is the file the whole HD side hangs on. The level's own rooms give the
 * remaster its collision and its gameplay; the .TRG gives it the picture -
 * every room's mesh, rebuilt and lit, with the static meshes baked into it and
 * a sky dome on the end. Delete it and the level still plays, in the classic
 * look, which is what TRForge has been offering until now.
 *
 * THE CONTAINER, measured over all 144 files of both installs - 2.25 GB, and
 * every byte of every file accounted for:
 *
 *     'TRG' | u8 version = 3 | i32 sectors
 *     sectors * 24                    a light cube per floor square, 6 x RGBA8
 *     float sun_direction[3]
 *     float sun_colour[3]
 *     u16 volumes_total | u8 fog_boxes | u8 box_volumes
 *     fog_boxes   * 40
 *     box_volumes * 28
 *     i32 groups
 *       per group:  i32 parts
 *                   parts * 44        one draw call each
 *                   u32 flags
 *                   i32 volumes
 *                   volumes * 28      point lights, most likely
 *     u16 textures | u16 0 | textures * u16      HD page numbers, padded to 4
 *     u32 indices | u32 vertices
 *     indices  * u32
 *     vertices * 20
 *
 * A part is `u32 material; float param[4];` then three (start, count) pairs -
 * the opaque, alpha and additive passes. They tile [0, indices) with no gap
 * and no overlap in all 144 files, and c == a+b and e == c+d in 29306 of 29306.
 *
 * WHY THESE SIZES AND NOT OTHERS. "Every byte is used" would be weak on its
 * own, because 99% of the bytes sit in three fields whose length the file
 * states. So each size was tried against its neighbours, and a wrong one has
 * to miss the end of the file:
 *
 *     vertex 16, 18, 22, 24, 28, 32 ............ 0 of 144 reach the end
 *     sector 12, 16, 20, 22, 23, 25, 26, 28 .... 0 of 144
 *     part   36, 40, 42, 43, 45, 46, 48, 52 .... 0 of 144
 *     the sizes above ......................... 144 of 144
 *
 * THE VERTEX is 20 bytes. (How a renderer uses each field is left out of
 * this copy; the layout is the file's.)
 *
 *     +0  i16 x, y, z     room local, classic units, 1:1
 *     +6  i16 sway        wind and water weight
 *     +8  u8  nx, ny, nz  normal, normalize(n - 127) - BIASED
 *     +11 u8  layer       which of the file's textures, COUNTED FROM ONE -
 *                         see HdMesh, where reading it from zero gave 0 of
 *                         273 faces the page the .TEX names and reading it
 *                         from one gave 249
 *     +12 u8  r, g, b     first colour
 *     +15 u8  u
 *     +16 u8  r, g, b     second colour
 *     +19 u8  v
 *
 * Group i is room i, in room order: 570359 of 632393 classic room vertices
 * appear verbatim in the matching group (90.2%), against 10.7% for the group
 * next door. The extra group at the end, where there is one, is the sky dome.
 *
 * What TRForge does NOT do with this yet is write it. Everything it does not
 * understand - the light cubes, the fog boxes, the volumes - is kept as the
 * bytes it was read as, so that a writer can put them back untouched when
 * there is one.
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace trf::remaster
{
    struct TrgPart
    {
        uint32_t material = 0;
        float    param[4] = {};
        /* Opaque, alpha, additive - each a run of indices. */
        struct Pass { uint32_t start = 0, count = 0; } pass[3];
    };

    struct TrgVertex
    {
        int16_t x = 0, y = 0, z = 0;
        int16_t sway = 0;
        uint8_t nx = 127, ny = 127, nz = 127;

        float normal_x() const { return ((float)nx - 127.0f) / 128.0f; }
        float normal_y() const { return ((float)ny - 127.0f) / 128.0f; }
        float normal_z() const { return ((float)nz - 127.0f) / 128.0f; }
        uint8_t layer = 0;      /* one-based into Trg::textures */
        uint8_t colour1[3] = {};
        uint8_t u = 0;
        uint8_t colour2[3] = {};
        uint8_t v = 0;
    };

    struct TrgGroup
    {
        std::vector<TrgPart> parts;
        uint32_t flags = 0;
        /* THE ROOM'S POINT LIGHTS, 28 bytes each, and now decoded.
         *
         *     float x, y, z      where it is, in world units
         *     float r, g, b      its colour, unnormalised - a hall sconce in
         *                        ASSAULT is (10, 7.72, 5.18)
         *     u32   kind         1 or 3 in every file there is
         *
         * (How a renderer lights an object with these is left out of this
         * copy.)
         *
         * ASSAULT's room 9 carries six of these at y = -1390, which is where
         * the sconces hang on its walls, and their warm colour is the colour
         * those sconces throw. Kept as bytes as well as read, because only
         * the first 28 are understood and a round trip has to give back what
         * it was handed. */
        struct PointLight
        {
            float x = 0, y = 0, z = 0;
            float r = 0, g = 0, b = 0;
            uint32_t kind = 0;
        };
        PointLight light(size_t at) const
        {
            PointLight out;
            if (at >= volumes.size()) return out;
            std::memcpy(&out, volumes[at].data(), 28);
            return out;
        }

        std::vector<std::array<uint8_t, 28>> volumes;
    };

    struct Trg
    {
        uint8_t version = 0;
        /* One 6 x RGBA8 cube per floor square of the whole level, in room
         * order. The count equals the sum of every room's sectors - checked
         * against the level in 64 of 64. */
        std::vector<std::array<uint8_t, 24>> light_cubes;
        float sun_direction[3] = {};
        float sun_colour[3] = {};
        std::vector<std::array<uint8_t, 40>> fog_boxes;
        std::vector<std::array<uint8_t, 28>> box_volumes;

        std::vector<TrgGroup> groups;      /* one per room, plus the sky dome */
        std::vector<uint16_t> textures;    /* .DDS numbers in <game>\TEX      */
        std::vector<uint32_t> indices;
        std::vector<TrgVertex> vertices;

        bool valid() const { return version != 0; }
        size_t triangles() const { return indices.size() / 3; }
    };

    /* Reads one .TRG. Refuses rather than guesses: version 3, the parts must
     * tile the index list, every index must be inside the vertex list, and the
     * file must end exactly where the vertices do. */
    bool read_trg(const std::string& path, Trg& out, std::string& error);

    /* The one number worth having without reading tens of megabytes: how many
     * floor squares the .TRG was built for. It sits at offset 4, and holding
     * it against the level's own count is the cheapest way to notice that the
     * two have drifted apart - which is what happens the moment somebody adds
     * or resizes a room and keeps the .TRG. Measured: they agree in 144 of
     * 144 untouched levels. */
    bool trg_floor_squares(const std::string& path, uint32_t& out, std::string& error);
}
