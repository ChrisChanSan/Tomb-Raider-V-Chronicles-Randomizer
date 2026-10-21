/*
 * TR5 Randomizer
 * Copyright (C) 2026 ChrisChanSan
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 */
//----------------------------------------------------------------------------
// Extraction from a Tomb Raider 3 level.
//
// This project ships no game data: whoever wants the dinosaurs must own
// Tomb Raider 3, and the run fetches them itself from that installation on
// the local machine. A standalone tool would mean distributing finished
// packages containing third-party game data, so this reader lives in the
// core. The other direction - inserting into a TR5 level - is
// Tr5Level::addMoveableFromTr3, addSoundsFromTr3 and addTexturesFromTr3.
//
// The tools TR3Fetch and TR3Sound only call these functions. There is no
// second reader: the format bridge (single-axis rotations 10 bit -> 12 bit,
// jump table with GLOBAL anim indices, faces with the TR5 effect word,
// colored faces onto a real texture) is correct in exactly one place.
//----------------------------------------------------------------------------
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Tr3Source
{
    // Fetches model, bones, animations, frames and textures of one object
    // from a TR3 level and stores them in the package format (version 2, the
    // same one Tr5Level::addMoveableFromTr3 and addTexturesFromTr3 expect).
    //
    // All format conversions happen here - afterwards the package is
    // TR5-ready. Verified along the way: every mesh must end byte-exactly,
    // no frame pointer and no jump target may leave its own slice. If any
    // check fails, nothing is delivered.
    // Optional treatment of an imported model. Both were needed for a Lara
    // outfit and mean nothing to the animals, so both default to off.
    //
    // recolors: a flat colour of TR3's is written to the page as a different
    // one. Source and target in full, so nothing has to be guessed about
    // which of the flat colours is meant. Wanted because TR3's skin and
    // TR5's differ by ONE 5 bit step in red - 224 falls on level 28 and
    // returns as 231, TR5 sits on 27 and returns as 222 - and that step is
    // the visible seam between an imported arm and TR5's own head.
    //
    // shinyWord: the effects word behind every TEXTURED face. Bit 1 switches
    // the TR4/TR5 environment map on, bits 2..6 are its strength
    // (specific/output.cpp). 0x0042 is what TR5's own catsuit wears in
    // rich1. Textured only, and that is exactly right: a TR1-3 model wears
    // its outfit as textures and its SKIN as flat colours, so the suit shines
    // and the skin stays matt without naming a single face.
    struct Recolor { int sr, sg, sb, dr, dg, db; };
    struct Rgb { int r, g, b; };
    struct Options
    {
        int shinyWord;
        std::vector<Recolor> recolors;

        // Extra solid blocks on the flat colour page that NO face points at.
        // They are there so a tone is AVAILABLE to paint a seam with later:
        // Tr5Level::paintMeshFlat looks for an evenly coloured entry and can
        // only find what the level holds. Without them the Antarctica hips
        // asked for 115,107,90 and got 90,107,90 - green, with the red gone.
        // The page is a 16 by 16 grid of 256 slots and a model uses a handful.
        std::vector<Rgb> extraColors;

        Options() : shinyWord(0) {}
    };

    bool fetchMoveable(const std::wstring& levelPath,
                      int objectId,
                      std::vector<uint8_t>& package,
                      std::string& error,
                      const Options* opt = 0);

    // One sound per entry, with explicitly given wave numbers from MAIN.SFX.
    // Several waves are variants of the same sound.
    //
    // The wave numbers are DELIBERATELY passed in from outside instead of
    // resolved from the level tables: for the Gold levels, the route via
    // SoundMap/SoundDetails/SampleIndices demonstrably yields wrong waves,
    // and the self-calibration does not notice. The numbers come from the
    // TRsound tool and were manually auditioned.
    struct SoundRequest { int tr3Id; std::vector<int> waves; };

    // Cuts the requested waves out of MAIN.SFX and stores them in the sound
    // package format (the same one addSoundsFromTr3 expects).
    //
    // CAUTION: The Lost Artifact ships its OWN MAIN.SFX, which differs from
    // the Tomb Raider 3 one (9820544 vs 11646918 bytes). The auditioned wave
    // numbers of the vulture refer to the TR3 file - which file is meant is
    // the caller's decision, not this function's.
    bool fetchSounds(const std::wstring& sfxPath,
                     const std::vector<SoundRequest>& requests,
                     std::vector<uint8_t>& package,
                     std::string& error);

    // An outfit from Tomb Raider 1-3, fully described.
    //
    // Same idea as DinoSource below: ONE place that knows everything about a
    // donor, so no detail can rot in a call line. Every number here was
    // measured, not guessed - how, stands with the field.
    struct OutfitSource
    {
        const char* id;          // stable key for config, patch and cache
        const char* name;        // label shown in the UI
        const char* source;      // "tr3" or "tr3gold": which installation
        const char* file;        // level name within it
        int         object;      // NOT 0 - in TR1-3 object 0 is a placeholder.
                                 // The real model is "Lara true appearance".

        // Brings TR3's flat skin onto TR5's 5 bit level. The two tones look
        // identical as numbers and are not: 224 falls on level 28 and comes
        // back as 231, TR5's own arm sits on 27 and comes back as 222. That
        // one step is a visible seam wherever imported skin meets the head
        // kept from the target level.
        std::vector<Recolor> recolors;

        // The TR4/TR5 environment map on every textured face. 0x0042 is what
        // TR5's own catsuit wears in rich1, measured there.
        int shinyWord;

        // Seams of TR5's joint shell that draw nothing for this outfit.
        // Numbering as in laraskin.cpp's SkinJoints; the mesh is seam+1.
        std::vector<int> dropSeams;

        // Seams painted in one flat colour instead of wearing TR5's own
        // texture, because that texture belongs to a body that is no longer
        // there.
        //
        // Every tone named here is put on the flat colour page as well, so
        // the painter finds it exactly rather than settling for the nearest
        // thing the level happens to contain.
        struct SeamColor { int seam, r, g, b; };
        std::vector<SeamColor> seamColors;
    };

    // Every outfit that can be fetched, in the order shown in the UI.
    std::vector<OutfitSource> outfits();

    // Like fetchCached below, for an outfit: no sounds, and the treatment
    // from the OutfitSource is applied on the way.
    bool fetchOutfitCached(const OutfitSource& o,
                           const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                           const std::wstring& cacheDir,
                           std::vector<uint8_t>& model,
                           std::string& error);

    // A guest from Tomb Raider 3, fully described.
    //
    // This is the ONLY truth about the four dinosaurs - randomizer and patch
    // path both read from here, so no detail can rot in a call line or
    // drift out of step between the two paths.
    struct DinoSource
    {
        int         slot;        // target slot in TR5 - occupied in NO level
        const char* name;
        const char* source;      // "tr3" or "tr3gold": which installation
        const char* file;       // level name within it
        int         object;      // object number in that file
        int         soundBase;  // first target slot in the soundmap
        std::vector<SoundRequest> sounds;
        std::vector<int>         muted;   // numbers deliberately left behind
        std::vector<int>         volumes;    // percent per sound, in package order
    };

    // The four guests. Sound slots are distributed collision-free and were
    // tested together in the "Jurassic Raider" level: vulture 420-423,
    // Compy 430-434, Raptor 438-444, T-Rex 445-449, muted ones on 411.
    std::vector<DinoSource> dinos();

    // Which MAIN.SFX applies to this guest? CAUTION, not obvious: the
    // vulture lives in The Lost Artifact, but its sounds are in the Tomb
    // Raider 3 MAIN.SFX. Gold ships its own, which differs (9820544 vs
    // 11646918 bytes) - the auditioned wave numbers refer to the TR3 one.
    std::wstring sfxPath(const DinoSource& d,
                         const std::wstring& tr3Dir, const std::wstring& tr3GoldDir);

    // And where is its level?
    std::wstring levelPath(const DinoSource& d,
                           const std::wstring& tr3Dir, const std::wstring& tr3GoldDir);

    // Fetches a guest's model and sounds - with a CACHE.
    //
    // Extraction costs around eleven seconds per call: it re-reads the TR3
    // level (3.3 MB) and MAIN.SFX (11.6 MB). Across thirteen levels the same
    // T-Rex would be extracted thirteen times. Once is enough.
    //
    // If "cacheDir" is given, the finished packages are stored there and
    // reused next time. That is fine license-wise: the files come into being
    // on the machine of the person who owns the games, and they stay there.
    // This folder is never shipped - it carries a warning text file so that
    // this stands out even while packaging.
    //
    // A stale entry is recognized by size and modification time of the
    // source file. Whoever reinstalls or patches their game automatically
    // gets a fresh extraction instead of a silent error.
    bool fetchCached(const DinoSource& d,
                      const std::wstring& tr3Dir, const std::wstring& tr3GoldDir,
                      const std::wstring& cacheDir,
                      std::vector<uint8_t>& model, std::vector<uint8_t>& sounds,
                      std::string& error);

    // The rest-pose bounding box of a model package - minX, maxX, minY,
    // maxY, minZ, maxZ, the same six int16 a TR5 frame starts with.
    //
    // The package's frame block starts with exactly the frame the installed
    // moveable will point at: addMoveableFromTr3 writes the block's start as
    // the object's frameOffset. So the first six words ARE the rest pose,
    // and a guest's height can be measured before it is installed anywhere.
    bool packageFrameBounds(const std::vector<uint8_t>& package, int16_t out[6]);

    // Redirects the faces' texture words to the new texinfos. Must run AFTER
    // Tr5Level::addTexturesFromTr3 (which provides the mapping) and BEFORE
    // addMoveableFromTr3. Returns the number of redirected words, -1 on
    // error.
    int remapTextures(std::vector<uint8_t>& package, const std::map<int,int>& mapping);

    // Rewrites the SFX numbers of the anim commands in the model package.
    //
    // "mapping" maps TR3 number to target slot. Numbers that are called but
    // appear in neither list end up in "unresolved" - the caller MUST abort
    // then. A TR3 number left in place hits some random vanilla sound in the
    // target; with the Compsognathus, 243 turned into
    // SFX_GENERIC_HEAVY_FEET, and finding that cost hours.
    //
    // "muted" names numbers that deliberately do not come along: they are
    // pointed at an empty soundmap slot, where the engine bails out instead
    // of playing something foreign.
    bool remapSfx(std::vector<uint8_t>& package,
                    const std::vector<std::pair<int,int>>& mapping,
                    const std::vector<int>& muted,
                    int muteSlot,
                    std::vector<int>& unresolved,
                    std::string& error);

    // Whoever points us at a Tomb Raider 3 installation hands over
    // whichever folder they happened to open - the game folder or its
    // data\ subfolder. Both are accepted: the folder itself is tried
    // first, then data\ inside it. Returns the folder that really holds
    // the file, or empty if neither does. Case does not matter on
    // Windows, so "Data" and "DATA" are covered too.
    //
    // This lives here, and not in the GUI where it was written, because
    // the command line needs exactly the same answer. It had its own
    // behaviour - none - and failed with "TR3 level not readable" on a
    // path the GUI accepts without a word.
    std::wstring resolveFolder(const std::wstring& chosen, const wchar_t* file);
    std::wstring resolveTr3(const std::wstring& chosen);       // CRASH.TR2
    // chunnel.TR2 - also in DATA\LA or LA under the chosen folder, the
    // Remastered layout, so its "3" folder serves the Gold row too.
    std::wstring resolveTr3Gold(const std::wstring& chosen);

    // The Tomb Raider 3 MAIN.SFX for a RESOLVED tr3 folder (the one holding
    // CRASH.TR2), or empty: beside the level, then in data\ inside it, then
    // in the sibling SFX\ of a folder named DATA (the Remastered layout,
    // <bundle>\3\SFX\MAIN.SFX). sfxPath, the GUI's presence test and the
    // command line all ask this one function.
    std::wstring resolveBank(const std::wstring& tr3Dir);
}
