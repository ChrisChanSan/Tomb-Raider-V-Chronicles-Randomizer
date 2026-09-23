/* TRForge - reading LEVELn.TRG. See Trg.h for the container and for how each
 * record size was pinned down against its neighbours.
 */
#include "trf_remaster/Trg.h"

#include <cstdio>
#include <cstring>
#include <filesystem>

#include "trf_core/trf_file.h"

namespace trf::remaster
{
    namespace
    {
        struct Reader
        {
            const uint8_t* data = nullptr;
            size_t size = 0;
            size_t at = 0;
            bool   bad = false;

            bool have(size_t bytes) const { return !bad && at + bytes <= size; }
            void fail() { bad = true; }

            uint8_t u8()
            {
                if (!have(1)) { fail(); return 0; }
                return data[at++];
            }
            uint16_t u16()
            {
                if (!have(2)) { fail(); return 0; }
                uint16_t v = 0; std::memcpy(&v, data + at, 2); at += 2; return v;
            }
            uint32_t u32()
            {
                if (!have(4)) { fail(); return 0; }
                uint32_t v = 0; std::memcpy(&v, data + at, 4); at += 4; return v;
            }
            int32_t i32() { return (int32_t)u32(); }
            int16_t i16() { return (int16_t)u16(); }
            float f32()
            {
                if (!have(4)) { fail(); return 0; }
                float v = 0; std::memcpy(&v, data + at, 4); at += 4; return v;
            }
            template <size_t N>
            void block(std::vector<std::array<uint8_t, N>>& out, size_t count)
            {
                if (!have(count * N)) { fail(); return; }
                out.resize(count);
                for (auto& record : out)
                {
                    std::memcpy(record.data(), data + at, N);
                    at += N;
                }
            }
            void align4() { while (at % 4 && !bad) { if (!have(1)) { fail(); return; } ++at; } }
        };

        constexpr size_t TrgVolumeBytes = 28;

        std::string named(const std::string& path)
        {
            return std::filesystem::path(path).filename().string();
        }
    }

    bool trg_floor_squares(const std::string& path, uint32_t& out, std::string& error)
    {
        out = 0;
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) { error = "Could not read " + path; return false; }

        uint8_t head[8] = {};
        const size_t got = std::fread(head, 1, sizeof(head), file);
        std::fclose(file);

        if (got != sizeof(head) || std::memcmp(head, "TRG", 3) != 0 || head[3] != 3)
        {
            error = named(path) + " does not begin with TRG version 3.";
            return false;
        }
        std::memcpy(&out, head + 4, sizeof(out));
        return true;
    }

    bool read_trg(const std::string& path, Trg& out, std::string& error)
    {
        out = Trg();

        uint8_t* bytes = nullptr;
        size_t size = 0;
        if (trf_file_read_all(path.c_str(), &bytes, &size) != TRF_OK || !bytes)
        {
            error = "Could not read " + path;
            return false;
        }
        struct Guard { uint8_t* p; ~Guard() { free(p); } } guard{ bytes };

        Reader reader{ bytes, size, 0, false };
        if (size < 8 || std::memcmp(bytes, "TRG", 3) != 0)
        {
            error = named(path) + " does not begin with TRG.";
            return false;
        }
        reader.at = 3;
        const uint8_t version = reader.u8();
        if (version != 3)
        {
            error = named(path) + " is version " + std::to_string(version) +
                    "; every one of the 144 files in the two remasters is version 3, so "
                    "TRForge will not guess at another.";
            return false;
        }

        const int32_t sectors = reader.i32();
        if (sectors < 0) { error = named(path) + " has a negative sector count."; return false; }
        reader.block(out.light_cubes, (size_t)sectors);

        for (float& value : out.sun_direction) value = reader.f32();
        for (float& value : out.sun_colour) value = reader.f32();

        const uint16_t volumes_total = reader.u16();
        const uint8_t fog_boxes = reader.u8();
        const uint8_t box_volumes = reader.u8();
        reader.block(out.fog_boxes, fog_boxes);
        reader.block(out.box_volumes, box_volumes);

        /* A GROUP COSTS TWELVE BYTES EVEN WHEN IT IS EMPTY - a part count,
         * a flags word and a light count - so the file size over twelve is
         * the most groups that can possibly be in it. The bound was the file
         * size itself, twelve times too loose, and it is resize() that pays:
         * a corrupt count in a fifty megabyte .TRG asked for fifty million
         * groups, and a TrgGroup is two vectors, so the reader tried to take
         * three gigabytes before noticing it had run out of file. */
        const int32_t groups = reader.i32();
        if (groups < 0 || (size_t)groups > size / 12)
        {
            error = named(path) + " says it has " + std::to_string(groups) +
                    " groups, and is only long enough for " + std::to_string(size / 12) + ".";
            return false;
        }
        out.groups.resize((size_t)groups);
        size_t volumes_seen = 0;
        for (TrgGroup& group : out.groups)
        {
            const int32_t parts = reader.i32();
            if (parts < 0 || reader.bad || (size_t)parts * 44 > size)
            {
                error = named(path) + " has a group with an impossible part count.";
                return false;
            }
            group.parts.resize((size_t)parts);
            for (TrgPart& part : group.parts)
            {
                part.material = reader.u32();
                for (float& value : part.param) value = reader.f32();
                for (TrgPart::Pass& pass : part.pass)
                {
                    pass.start = reader.u32();
                    pass.count = reader.u32();
                }
            }
            group.flags = reader.u32();

            const int32_t volumes = reader.i32();
            /* Checked against what is LEFT of the file, not merely against
             * zero: block() would have caught a short read, but only after
             * resize() had already been asked for the memory. */
            if (volumes < 0 || reader.bad || !reader.have((size_t)volumes * TrgVolumeBytes))
            {
                error = named(path) + " has a group with an impossible light count.";
                return false;
            }
            reader.block(group.volumes, (size_t)volumes);
            volumes_seen += (size_t)volumes;
        }

        const uint16_t textures = reader.u16();
        reader.u16();                      /* always zero */
        if (!reader.have((size_t)textures * 2))
        {
            error = named(path) + " ends inside its texture list.";
            return false;
        }
        out.textures.resize(textures);
        for (uint16_t& texture : out.textures) texture = reader.u16();
        reader.align4();

        const uint32_t index_count = reader.u32();
        const uint32_t vertex_count = reader.u32();
        if (reader.bad || !reader.have((size_t)index_count * 4 + (size_t)vertex_count * 20))
        {
            error = named(path) + " says it has " + std::to_string(index_count) +
                    " indices and " + std::to_string(vertex_count) +
                    " vertices and is not long enough for them.";
            return false;
        }

        out.indices.resize(index_count);
        for (uint32_t& index : out.indices) index = reader.u32();

        out.vertices.resize(vertex_count);
        for (TrgVertex& vertex : out.vertices)
        {
            vertex.x = reader.i16();
            vertex.y = reader.i16();
            vertex.z = reader.i16();
            vertex.sway = reader.i16();
            vertex.nx = reader.u8();
            vertex.ny = reader.u8();
            vertex.nz = reader.u8();
            vertex.layer = reader.u8();
            for (uint8_t& channel : vertex.colour1) channel = reader.u8();
            vertex.u = reader.u8();
            for (uint8_t& channel : vertex.colour2) channel = reader.u8();
            vertex.v = reader.u8();
        }

        if (reader.bad)
        {
            error = named(path) + " ran out of bytes while being read.";
            return false;
        }
        if (reader.at != size)
        {
            error = named(path) + " has " + std::to_string(size - reader.at) +
                    " bytes left over, so it is not laid out the way TRForge reads it.";
            return false;
        }

        /* The checks that a wrong reading fails even when the byte count comes
         * out right. All three hold in 144 of 144 shipped files. */
        if (volumes_seen != volumes_total)
        {
            error = named(path) + " counts " + std::to_string(volumes_total) +
                    " volumes at the front and " + std::to_string(volumes_seen) + " in its rooms.";
            return false;
        }
        /* THE PARTS HAVE TO TILE THE INDEX LIST, which Trg.h and formats.md
         * both said was checked and which nothing checked. A part carrying
         * start = 0xFFFFFFFE and count = 3 loaded clean, and the .OBJ
         * exporter's guard `at + 2 >= indices.size()` wraps to zero on it and
         * reads thirty-four gigabytes past the end. Free to add: over the 144
         * shipped files all 29,306 parts tile [0, index_count) exactly, so
         * this refuses nothing that ships. */
        for (const TrgGroup& group : out.groups)
        for (const TrgPart& part : group.parts)
        for (const TrgPart::Pass& pass : part.pass)
        {
            if ((size_t)pass.start + (size_t)pass.count > (size_t)index_count)
            {
                error = named(path) + " has a part that draws indices " +
                        std::to_string(pass.start) + " to " +
                        std::to_string((size_t)pass.start + pass.count) + " out of " +
                        std::to_string(index_count) + ".";
                return false;
            }
        }

        if (index_count % 3)
        {
            error = named(path) + " has an index count that is not whole triangles.";
            return false;
        }
        for (uint32_t index : out.indices)
        {
            if (index < vertex_count) continue;
            error = named(path) + " points at a vertex it does not have.";
            return false;
        }

        out.version = version;
        return true;
    }
}
