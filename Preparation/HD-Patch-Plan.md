# HD patch (#123) - implementation plan

Written 2026-09-05 from a read-only survey of this repository, the TRForge
library (MIT, `C:\Cloude AI\Todesengel\TRForge-remastered`) and a fresh
Steam copy of Tomb Raider IV-VI Remastered. Every number below was measured;
the file:line anchors are today's. Nothing has been built yet - this is the
plan the user decides on.

The goal: paint the Remaster's art into the classic Chronicles levels so
that the native TOMB5 port (x86, D3D9) draws it, as an option of the
randomizer, reading the user's Remaster installation at run time. No
Remaster data ever enters the repository or the package. Nothing is taken
from the Remaster's binaries (tomb5.dll, tomb456.exe) - only file formats
as facts.


## 0. Five findings the plan rests on

1. **Our loader cannot take 512x512 pages - and does not need to.**
   `LoadTextures` walks the pages with a fixed stride of 256x256 texels
   (`TOMB5 Code\TOMB5\specific\file.cpp:538-581`), and `CreateTexturePage`
   samples the source as 256 wide (`specific\texture.cpp:76, 105, 122`).
   The UVs, however, are already page-size independent: `LoadTextureInfos`
   normalises to 0..1 (`file.cpp:1174-1193`) and `AdjustUV` reads the
   half-texel inset from `Textures[tpage].width` (`file.cpp:213-214`).
2. **TRForge cannot write 512x512 pages into a level either.**
   `TextilePixels = 256*256` is fixed (`trf_level\TRTypes.h:437`) and
   `replace_texture_page` refuses anything else
   (`trf_editor\TexturePalette.cpp:88-92`). **The HD patch is a repaint of
   the level's own 256x256 pages - better painted, not sharper.** The GUI
   text has to say so.
3. **The page budget is large, not tight.** Vanilla levels carry 6..17
   pages (RT+OT+BT from the header). Our port has `Textures[256]`
   (`texture.cpp:25`), checked at `file.cpp:457`. TRForge's
   `pages_the_engine_will_take(TR5) = 32` was read from the Remaster's
   tomb5.dll and does not apply to us. About 235 entries are free; the
   real limit is texture memory (256 KB per page, ~512 KB with the managed
   copy). Side finding: the check at `file.cpp:457` does not count the
   loader's own three pages (shine, font, sky) nor the title's two logo
   pages - an off-by-3 (title: 5) that is unreachable in practice.
4. **The checksum gate is no obstacle in the GUI.** `inspectSource`
   hashes `Backup\`, not the working copy (`gui\Main.cpp:671-674`);
   `Phase::Copy` restores `data\` from Backup (`Main.cpp:1365-1379`) and
   `doLevel` loads from `data\` (`Main.cpp:848-850`). An HD step placed
   at the start of `doLevel`, before `lvl.load`, is never seen by the
   gate. No second checksum list, no structural check, no stamp needed.
   Only the CLI hashes its input itself (`Randomize.cpp:342-388`); there
   the HD step runs inside the run, after the gate, on a working copy.
5. **TRForge and this project arrived at the same dangerous rule
   independently.** `TextureInserter::finish` inserts pages at
   `rt + ot`, shifts every stored tile number at or above the insert
   point, drags sprites along and keeps the 32- and 16-bit blocks in
   step (`trf_assets\TextureAtlas.cpp:694-774`) - line for line our
   Phase B (`core\Tr5Transfer.cpp:993-1063`).


## 1. Architecture - recommendation: a seam of our own, built into the randomizer

One C++17-clean header in our tree is the only thing that sees TRForge:

```
TR5Randomizer\src\core\HdPatch.h      our language, our error texts
TR5Randomizer\src\core\HdPatch.cpp    the one file that includes TRForge
```

About three functions: `available(remasterDir)`, `levelHasPartner(name)`,
`apply(levelPath, remasterDir, cacheDir, Options, Report&, error)`.
Whether the library behind it is a static lib, a stack of `.cpp` in
`b2.bat` or a second process becomes a build question that can be
changed later without touching our code.

Build it as: TRForge vendored unchanged under
`TR5Randomizer\external\trforge\` (LICENSE and NOTICE alongside; the
acrostic header lines of ten files stay verbatim), compiled by a **second
`cl` line** in `b1.bat`/`b2.bat` with `/std:c++20` for the library only.
Our code stays C++17; MSVC links both. No CMake in the build path. The
same `HdPatch.cpp` built into `Preparation\Tools\Diagnose\HdBake.cpp` is
the standalone exporter for free - for measurements and regressions.

Two build traps, known now: TRForge bundles zlib 1.2.11 and its own
`stb_image` implementation; we have zlib 1.2.12 in the engine tree and
`STB_IMAGE_IMPLEMENTATION` in `gui\Main.cpp`. Do not compile TRForge's
copies; point its include paths at ours. TRForge's CMake uses `/MT`; the
`cl` line keeps the default `/MD` for both halves.

Licence: MIT inside GPLv3 is fine. `THIRD-PARTY.md` gets a TRForge entry
under "Libraries compiled into the binaries"; `BuildRelease.ps1` places
LICENSE and NOTICE into the package's `Licenses\` the way it does for
imgui.

| | (A) standalone exporter | (B) inside the randomizer | (C) seam + library |
|---|---|---|---|
| one-time build work | CMake, second artefact, release script | two `cl` lines, ~70 TUs | as B plus one header |
| checksum gate | exported files would need a second list | gate never sees them | same |
| user experience | "run the exporter first" | one checkbox and one path, like TR3 | same |
| robustness | two artefacts must match | one binary | one binary plus a tool from the same core |

The user's stated preference (2026-09-05) is the native module - (B),
built as (C).


## 2. Order, checksum gate, page budget

Order in the GUI: `ensureBackup` -> `copyLevels(Backup -> data\)` ->
**HD step on `data\<level>.trc`** -> `doLevel: lvl.load` -> LevelFix,
`Rando::run`, `Patch::apply`. The HD step belongs at the start of
`doLevel`, before `lvl.load(target)` (`Main.cpp:848`), because TRForge
rewrites the file and `tiles_line_up` needs the classic copy to be
vanilla at that moment.

Checksums: nothing to do (finding 4). A stamp in the header's spare
words is still worth setting as a diagnostic - `data[3] = 1` meaning
"this file carries HD art", because a second bake over translucent
texels drifts (`trf_remaster\HdPages.h:180-189`). The engine reads only
`data[0..2]`; TRForge preserves the block; LevelDiff already prints it.

Page budget, measured: ceiling 252 (title 250) minus 6..17 vanilla
pages; stage 1 adds no page; stages 1b and 2 take a `page_budget`;
cross enemies and TR3 guests add their own through Phase B, which
counts and aborts at 256 (`Tr5Transfer.cpp:1027`). Proposed own
ceiling `kHdPageCeiling = 48` per level including vanilla pages - to be
measured with allsanity + cross + HD by reading the header bytes of all
13 output levels. Side effect to expect: Phase B reuses pages by
`memcmp`; after a repaint fewer donor pages match, so a few more pages
get appended than before.


## 3. Engine side

For stages 1 and 2: **nothing.** Page count, page size, UVs, memory,
D3D9 format, room and object drawing all stay - the patch refills the
existing 256x256 pages and only appends records. Two facts to know: the
renderer uses the 32-bit block only (`dxshell.cpp:551-562`,
`file.cpp:476-504`); `Bucket[30]` is a sort buffer, not a per-page
array (`drawroom.cpp:1231-1282`), so more pages cost bucket switches,
never an overflow.

512x512 pages would be a separate undertaking (three sites: page side
measured from block size in `LoadTextures`, a source width in
`CreateTexturePage`, and the mallocs) - pointless without a producer,
which TRForge is not. A cheap pre-measurement exists: upscale a vanilla
level's pages to 512, adjust block and count, leave the UVs, start it.


## 4. Stages

**Stage 1 - rooms and statics (the TRG way).** `describe`,
`tiles_line_up`, `improve_textures_with_trg`, `read_trg`,
`paint_pages_from_trg` with `with_statics = true`,
`leave_animated_alone = false` (TRForge measured that for TR4/5 the
animated cells match better - median distance 1.5-3.4 against 5-10,
`HdPages.h:120-132`; its own CLI leaves the default on, one more reason
to write our own caller), `keep_detail = 0.4` (user decision). Statics
are baked into the Remaster's room mesh, so they come with this stage.
**No new record, no new page, no byte in the main block** - the stage
whose rooms can be proven byte-identical.

**Stage 1b - split contested tiles (optional, budgeted).**
`copy_contested_statics` first, then `split_contested_tiles` with our
ceiling. From here the main block grows: texinfos appended, faces
repointed, static records added, pages at `rt + ot`. The sentence to
prove: no existing texinfo index moves - `LevelFix::buildRoomGeometry`
and the secret lists name texinfo numbers. Static slots are a real
limit: 70 (`TrgTiles.h:169`), indexed unchecked in the engine.

**Stage 2 - single-mesh objects and root meshes.** `bake_objects_hd`
with a slot allow-list. Articulated objects cannot be done, for a
measurable reason: their meshes each sit in their own origin while the
Remaster model keeps all bones in one space (`HdObjects.h:18-26`);
Lara and enemies in HD would be TRM models, another format, another
project. Two exclusions we must set ourselves: slot 0 (LARA - the TR3
outfit seam reads skin tones from her pages, `Tr5Level.h:307-328`) and
the cross-enemy and MESHSWAP slots (their meshes get replaced).

Untouched, correctly: sprites (own rectangles, tile numbers dragged
along), shine/font/sky (third texture block), the HD loading screens and
title wordmark (already built, same pattern). Without a Remaster path:
checkbox greyed, run byte-identical to a build without the feature.
`del.trc` has no Remaster partner and stays vanilla; `Title.trc` and
`richcut2.trc` have partners and could take HD although they are not
randomized - user decision.


## 5. Proofs per stage

**5.0 The gate proof: lossless round trip.** `HdBake -roundtrip` over
all 16 levels (load + save, no HD art), then `LevelDiff` against
Backup. Expectation: 0 differing bytes in every level; the texture
blocks even verbatim, because TRForge reuses the raw zlib stream when
nothing is dirty (`LevelWriter.cpp:364-373, 406`). If this proof fails,
the plan stops here. Watch the sound samples behind the main block.

**5.1** Option off: old build vs new build, same seed and switches,
`cmp` over all 13 outputs, identical - after every work package.

**5.2** Positive control: stage 1 - main block byte-identical, page
counts equal, 32- and 16-bit blocks different, `data[16]` equal but for
the stamp, samples equal. Stages 1b/2 - `RoomCheck`, `ItemCheck`,
`FloorCheck`, `SectorCheck` equal before/after; `HeightCheck` 0 over all
13 outputs; `TexRec` shows no existing texinfo record moved.

**5.3** `Preparation\Tools\Diagnose\HdBake.cpp` + `bhd.bat`, built from
the same `core\HdPatch.cpp`: a table per level - partner, tiles_line_up,
tiles, `.TEX` share, faces served before/after, pages before/after,
texels, ms, MB.

**5.4** PNG sight check: all pages before and after, the only check
that answers "does it look right" (a picture painted into plaster is
invisible to numbers).

**5.5** The user's play test: one seed, all 13 levels, with `-log`;
"Created N Texture Pages" in log.txt, water and waterfalls (animated
tiles), pictures on walls (`keep_detail`), doors and switches (stage
2), load times.


## 6. Risks, with the measured sizes

Per Remaster level: `.TRG` 1.6 MB (RICHCUT2) to 58.4 MB (ANDREA2); 73
to 121 HD pages (24 to 40 MB on disk, 73 to 121 MB decoded if all at
once); 1266 to 3007 tiles, of which the dead `.TEX` paints 7 to 26 %.
The classic texinfo counts equal the `.TEX` tile counts in all 15
levels - `tiles_line_up` has good odds, but the records themselves are
ours to measure.

1. Round-trip fidelity (5.0) - decides everything.
2. Texinfo indices from stage 1b on - a moved index textures the
   built geometry and secret portals silently wrong.
3. `keep_detail` - the Remaster rearranged rooms; too high paints
   nothing, too low turns pictures into plaster. Decided by looking.
4. Animated tiles - measured right for TR5 by TRForge's yardstick, not
   by our eye; water is what a player notices first.
5. Contested tiles - a page is shared; without stage 1b the rest stays
   visibly wrong. The report's `tiles_extra_wanted` is the honest count.
6. Time and memory per level - unmeasured; peak roughly 200 MB. The
   randomizer builds x64 (`b1/b2.bat` call vcvars64), so the 2 GB limit
   is the engine's problem only - one more reason to bake in the
   randomizer, never in the engine. `HdBake` must print the time; more
   than about 30 s per level argues for a stamped cache.
7. A second bake drifts - our order (always restored from Backup)
   protects, the `data[3]` stamp makes it checkable.
8. Cache location - `Import-Export\` beside the EXE with its warning
   note is the model; a stamp of size and time of `.TRC`, `.TRG`,
   `.TEX` plus an HdPatch version and the options, like
   `Tr3Source::fetchCached`.
9. Legacy: `Andrea1HD Remaster.trc` in `data\Backup` is inert but is
   foreign game data in the folder that counts as pristine. Remove.


## 7. Work packages - one agent, one proof, then a halt

- **A0 - bind the library, prove the round trip.** Vendor TRForge,
  second `cl` line, zlib/stb dedup, `HdPatch` with `roundtrip()` only,
  `HdBake` + `bhd.bat`. Proof: LevelDiff 0 bytes over 16 levels. The
  gate.
- **A1 - stage 1, CLI tool only.** Rooms and statics, no split, no
  objects; table and PNGs. Proof: main block identical, texture blocks
  different, page count equal; a hand-copied level starts and looks
  right. **First time the user judges the look.**
- **A2 - wire GUI and CLI.** Remaster path in `Config` beside `tr3Dir`,
  path dialog line, `phTexRemaster` live and out of the "in preparation"
  box, call at the start of `doLevel`, stamped cache, spoiler line,
  `-hd` in the CLI, persistence. Proof: off = byte-identical to the
  previous build; on = 13 levels HD and randomized, HeightCheck 0, play
  test.
- **A3 - page budget and split (optional).** Our ceiling; measurement
  run with allsanity + cross + HD. Proof: no texinfo moved, pages under
  the ceiling, static IDs under 70, room/item/floor checks unchanged,
  engine loads all 13.
- **A4 - stage 2, objects (optional).** Allow-list without slot 0 and
  without the cross/MESHSWAP slots. Proof: TR3 outfit run still works
  (skin seam), page PNGs, play test at doors and switches.
- **A5 - 512x512 pages in the engine.** A separate undertaking, after
  the pre-measurement in section 3.
- **A6 - housekeeping.** The off-by-3 at `file.cpp:457`; the legacy
  file out of `data\Backup`.


## 8. Decisions for the user

1. Architecture: seam at `core\HdPatch.h`, built as a second `cl` line
   (recommended, matches the stated preference) - or a standalone
   exporter with CMake?
2. Wording: the feature paints better, it does not sharpen. Say so in
   the GUI ("paint the Remaster's art into the classic pages")?
3. `keep_detail` (default 0.4): keep, raise, or 0 - only after the A1
   PNGs.
4. Animated tiles: paint them (TRForge's TR4/5 measurement) or leave
   them (TRForge's default)? Recommendation: paint, then look.
5. `kHdPageCeiling` per level: 48 proposed - higher, lower, or no stage
   1b at all?
6. Stages 1b and 2 at all? Stage 1 alone leaves the main block
   untouched - the lowest-risk form of the feature.
7. `Title.trc` and `richcut2.trc` in HD too, although not randomized?
8. Cache in `Import-Export\HD\` with a stamp, or bake every time if the
   measured time allows?
9. Set the `data[3]` stamp?
10. Slot allow-list for stage 2: scenery, doors, switches, trapdoors,
    pickups; never slot 0, never the cross/MESHSWAP slots.
