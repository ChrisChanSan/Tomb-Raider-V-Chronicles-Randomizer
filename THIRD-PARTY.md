# Third-party code in this repository

This project is GPLv3 (see [LICENSE](LICENSE)). It builds on other people's
work, and this page says whose, under what terms, and where the full text of
each licence lives in this tree. Nothing here is a summary you should rely on
instead of the licence itself — every entry points at the authoritative text.
TOMB5, tomb3, TROne and this project are all under the GPLv3, one and the same
[licence text](LICENSE), which the repository holds as `LICENSE` and the release
package as `Licenses/GPLv3.txt`.

## The engine

**TOMB5** — Troye / Trxyebeep and contributors,
<https://github.com/Trxyebeep/TOMB5> — **GPLv3**.
Everything under `TOMB5 Code/TOMB5/` is a modified copy of that decompilation.
What was changed and why is recorded area by area in
[TOMB5 Code/MODIFICATIONS.md](TOMB5%20Code/MODIFICATIONS.md), which is also the
dated notice of modification GPLv3 section 5(a) asks for.
Full text: `TOMB5 Code/LICENSE`.

**tomb3** — Troye / Trxyebeep, <https://github.com/Trxyebeep/tomb3> — **GPLv3**.
The control code of four Tomb Raider III enemies (T-Rex, raptor,
Compsognathus, vulture) was taken over directly rather than rewritten, so that
its origin stays visible. Each of those files names its source in its own
header; the whole arrangement is set out in section 2.3 of MODIFICATIONS.md,
"Tomb Raider III enemies".
Same licence as this project, which is what allows the combination.

**TROne** (GPLv3) by Todesengel1931 —
<https://github.com/todesengel1931>.
The HD art read at run time carries code over from TROne, and the modern
control scheme carries its design and its test expectations over.
TROne is under the same licence as this project, which is what allows the
combination. TROne-main was read in three downloads, of 2026-09-05, 2026-09-13
and 2026-10-05; the last is the reference copy, and every citation names the
copy it was read in. What was taken, file by file:

| Here | From TROne | What |
|---|---|---|
| `TOMB5 Code/TOMB5/specific/hd/hd_install.cpp/.h` | `host/fs/discovery.cpp` | Steam discovery: the registry lookup, the `libraryfolders.vdf` reader in both of its shapes, the "recognise by content, never by folder name" rule, and the wording of the diagnostics. The IV-VI branch is taken, and so is the I-III branch - the same table row, `tomb123.exe` beside subfolders 1..3 with `DATA` and `TEX` - because the four Tomb Raider III guests the randomizer imports have their HD models in that bundle. The rest of TROne's discovery (the classic installs, the per-game level counts, the PIX / TRACKS / FMV folders) is not taken. |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp/.h` | `host/render/game_renderer.cpp`, `hd_load` and `hd_draw_room` (TROne-main, copy of 2026-10-05) | The shape of the level build and of the draw: one buffer for the whole level, a range table per group, group *i* is room *i*, groups past the room count are the sky dome, three passes per room in the order opaque / alpha / additive, and the additive pass blended `ONE, ONE` with the depth write off. Also the rule that a vertex whose texture layer is 0 falls back to the level's first page instead of drawing nothing. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl` | the same file's GLSL `k_hd_vert` / `k_hd_frag` (TROne-main, copy of 2026-10-05) | Taken: the alpha test living in the alpha pass **only** (an opaque tile's alpha is a gloss mask, and testing on it blanks whole floors), and the additive pass weighting its colour by the texture's alpha - both TROne's own design. Not taken: the lighting rule. That the room light is the **sum** of the vertex's two colour triples, each byte over 255, with the pixel `texel * light`, is this project's own count: on the 107,118 classic room vertices a `.TRG` vertex of the same room stands on, the sum orders them as the classic prelight does better than the product or either triple (`TrgProbe -shadeorder`, the origin ledger below). TROne's sum rests on TRForge's reading of the Remaster's shaders and carries a room-shadow scale on the first triple; this shader has no such scale. Translated to HLSL for shader model 3; not a line of the GLSL is copied. |
| `TOMB5 Code/TOMB5/specific/hd/hd_pagecache.cpp/.h` | `host/render/hd_pages.cpp`, `upload2` / `fill_missing_tiles` | Where a page is looked for when it is not in its own game's `TEX` folder — the sibling games next, `WHITE.DDS` last — and the rule that a page that cannot be found is painted **white** and never black. The upload itself is new: TROne hands the driver BC7 blocks in one texture array, and Direct3D 9 has neither. |
| `TOMB5 Code/TOMB5/game/draw.cpp`, `DrawStaticObjects` | `host/render/game_renderer.cpp`, the baked-mesh rule in `draw_mesh` (TROne-main, copy of 2026-10-05) | The rule itself, not the code: a static mesh the remaster baked into the room art must not be drawn a second time (TROne's "double lantern" bug report). TROne suppresses by mesh pointer and needs an exception for a moveable that claims the same pointer; our seam skips one room's own `MESH_INFO` list, for which that exception cannot arise. |
| `TOMB5 Code/TOMB5/specific/hd/hd_model.cpp` | `host/render/game_renderer.cpp`: `build_skinned_model`, `bind_pose_of_slot`, the model and head loops of `hd_load` (TROne-main, copy of 2026-10-05) | The rules of the object path: one `.TRM` replaces the **whole** classic model; a vertex's three bone indices are nodes of the classic mesh tree; the bind pose is that tree's accumulated offsets with the rotations left out, walked with the same push and pop; a `<NAME>_HEAD.TRM` is drawn **rigidly** on the node nearest the face geometry's own centre (measured, not hard-coded); a part's three index runs are the three passes, additive blended `ONE, ONE` with the depth write off; and a node the caller never gave a matrix for gets the last one it did give rather than collapsing to the origin (`fill_unseen_joints`). The renderer under them is new: TROne is OpenGL 3.3 with vertex arrays and texture arrays, this is Direct3D 9 with a vertex declaration, one texture per page and compiled bytecode. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_model.hlsl` | the same file's GLSL `k_trm_vert` / `k_trm_frag` (TROne-main, copy of 2026-10-05) | The skinning rule — three bones and three weights per vertex, the bind offset off before the node matrix goes on, the normal through the same matrix's 3×3 part — and the alpha test in the alpha pass **only** (TROne's golden-skull report). Translated to HLSL for shader model 3, with the bind offset folded into the matrix on the processor instead of subtracted per vertex; not a line of the GLSL is copied. The lighting is **not** TROne's: its item light in its copy of 2026-09-05 was a flat ambient blown into a six-face cube plus four point lights of its own, while this engine has the classic path's own per-object light list, distance fog and fog bulbs. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp/.h` | `host/render/game_renderer.cpp`: the item bracket of `item_flush` and `draw_mesh`, `fill_unseen_joints`, `model.covered = ~0u` in `build_skinned_model` and the swap-key rule beside `item_flush` (TROne-main, copy of 2026-10-05) | The **routing rules**, not the code: an item is drawn either wholly HD or wholly classic and never half of each; a node the model has no geometry for is drawn by NOBODY, not even classically (TROne tried the classic fallback there and took it out again after user reports of classic patches on HD bodies); a swapped mesh on a covered node loses to the model, so the model draws what the remaster drew; and a node the walk never reached keeps the last matrix it saw rather than collapsing to the origin. The seam itself is ours - TROne is a module beside a foreign engine and reads its brackets out of a stream of draw calls, this one rides on `DrawAnimatingItem`'s own node walk - and so are the skeleton test, the `mesh_bits` node ranges and the whole of the lighting. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp`, the inventory ring (`HDInvBegin` / `HDInvNode` / `HDInvEnd`) | `games/tr5/glue/hostglue_2d5.cpp`, `phd_PutPolygonsPickup`, and the `TRONE_LIGHT_EXACT_NODES` flag of `host/api/trone_host_api.h` (TROne-main, copy of 2026-10-05) | The **idea**, not the code, and only half of it was kept: that the classic ring's off-centre projection - it moves the projection centre to each item's place on the screen - can be folded into the ITEM for the length of one draw, so that the HD path keeps one projection for the whole frame; and that the ring's light collapses to the ambient plus one sun taken from the ring's own brightness. TROne folds it into the item's own matrix as a **shear** of two rows against the third; this engine does **not**, because its shader takes the vertex normal through those same matrices and renormalises it, so a shear lights the ring by screen position (measured on the shader: 28 % too dark in the middle of the inventory row, 16 % too bright on the ammo row). The offset goes into the projection matrix instead, per draw call (`HDModelOffCentre`, `hd_model.cpp`), where the classic path has it and where a normal cannot reach it. The arithmetic was derived from TR5's own projection line (`aTransformLightClipMesh`, `specific/output.cpp`) and is written for this engine's matrices, not for TROne's float stack; the derivation stands in full over `HDInvNode` and over `HDModelOffCentre`. TROne also needs a flag to make the ring draw **exactly** the nodes it was given, because its default invents a matrix for an unseen node; ours takes `item->mesh_bits` as a mask and needs none. Not taken: TROne's scale term `f_mpersp/persp`, which corrects for a host projection built from the window's real width - TR5 computes `f_persp` from a 4:3 implied width whatever the window is, and the HD projection here is built from that very number, so the factor is exactly one (measured: `ModelProbe -ringcheck`, the bounding rectangle at the same **size** on 4:3 and on 16:9). |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp`, the water marks | `host/render/game_renderer.cpp`, `hd_load` and `hd_water_mark` (TROne-main, copy of 2026-10-05) | The **road**, not the code: the remaster's geometry carries no water flag, so the classic level's own water vertices are matched to it by room-local position through a hash of the packed int16 x/y/z, per room, once at level build - and which group owns which vertex is recovered by walking the group's index runs. TROne marks a vertex from the classic `RoomVertexMove` flag and from the room's underwater bit; ours marks it from `ROOMLET::nWVtx` and from the engine's own shimmer predicate, and carries three bits rather than one. |

| `TOMB5 Code/TOMB5/specific/hd/hd_lara.cpp/.h`, and `hd_wardrobe.cpp/.h` with it | `host/render/game_renderer.cpp`: `lara_flush`, `lara_build_models`, `lara_load`, `draw_hair`, and `games/tr5/glue/hostglue_file5.cpp`: `RegisterLaraWardrobe` (TROne-main, copy of 2026-10-05) | The **composite itself**: Lara is not one model but an outfit for the body, a separate `LARA_HEAD` drawn rigidly on the head node, a `HAIR_OG` braid on its own six-bone chain, a `HAND_` model chosen by the slot whose mesh sits at the hand node, and a `HOLSTER_` model for a worn weapon; an empty holster draws nothing because the straps are in the outfit; and the whole wardrobe lives in the shared `4\ITEM` folder, not in the running game's. The role table (which slot means which model) is TROne's, name for name. What is **not** TROne's: the section table - its TR5 glue of its copy of 2026-09-05 had `ANDY` and `JOBY` the wrong way round and would put the Russia outfit on Ireland - and the outfit test, which here reads the engine's own `LaraDrawType` and its own braid predicate rather than the level's file name. Also this project's own is the split hand rule: TROne drew one hand model for both hands in its copy of 2026-09-05 (TROne-main, copy of 2026-10-05, has a split of its own), which loses TR5's flare, torch and crowbar frames, so a frame whose two hands differ draws both models masked to one arm each. The role table and the search that finds those models live in `hd_wardrobe.cpp/.h`, which is why that file stands in this row rather than in one of its own. |

| `TOMB5 Code/TOMB5/specific/hd/hd_laradraw.cpp/.h` | `host/render/game_renderer.cpp`: `draw_hair` (its copy of 2026-09-05; in TROne-main, copy of 2026-10-05, `draw_hair` skins the links its glues build the same way) | The **braid rule**: `HAIR_OG` has one bone per link, so all six links get a frame of their own built from the link's absolute position and its two angles - where TR5 itself builds a matrix for links 1, 3 and 5 only and draws 0, 2 and 4 as skinned connectors out of stashed vertices. Everything else in that file is this engine's own: the LMX-to-LM turn of `lara_matrices`, the per-node ambient out of `LaraNodeAmbient`, the hand-slot pointer comparison, the first-person mask and the mirror. |

| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl`, the wind and the water UV | `host/render/game_renderer.cpp`: `k_hd_vert`'s `hd.water_effect` block (its copy of 2026-09-05) and `k_hd_frag`'s caustic (TROne-main, copy of 2026-10-05) | The **wave shape and its scale**: two sines of the position taken across each other, about 24 classic units of travel and a wavelength of roughly one and a half blocks, plus a UV displacement of a few texels driven by the same phase; the vertical swell on the water marks; and the per-pixel caustic - `sin(x) * sin(z)` plus a half-weight diagonal, all three on the world position and on the game clock, at TROne's own scale of 0.018 per unit; the vertical swell is TROne's design of its copy of 2026-09-05. The caustic's strength, 0.32, is this project's own, matched to two reference shots of one pool (TROne-main, copy of 2026-10-05, gives its caustic 0.12). Translated to HLSL; the weight the wave rides on is the `.TRG`'s own `TrgVertex::sway`, which TROne does not read. Not taken: TROne's water flag, which is a room bit, where ours is the classic path's own water marks (the water marks row above). TR5 has a brightness term of its own, the `WaterTable` shimmer, reproduced exactly, and a second brightness term would count it twice; but that shimmer is computed per vertex, and a remaster water surface is a few large quads, so on its own it reaches the screen as a slow blotch - which is the same thing TROne's own comment reports about TR4 water. So the shimmer is untouched, parity and all, and the caustic is added as a zero mean modulation that carries no average brightness of its own. |
| `TOMB5 Code/TOMB5/specific/hd/hd_fontmatch.cpp/.h`, `hd_font.cpp/.h` and the branch in `game/text.cpp` | `host/render/game_renderer.cpp`, the block its own code marks `hdtext` (`hdtext_find` and `hdtext_arm`), and the glyph draw beside it. **No line numbers**, on purpose: no line range for it could be checked against a copy of TROne that can be read here, so the block is named by its own marker instead — the reasoning stands over `hd_fontmatch.h` | The **method**, not the code: find a classic glyph in the remaster's `FONT<n>_OG.DDS` by sliding its ink mask over the atlas and scoring **intersection over union**, refuse anything under **0.94** because a similar character is the wrong character, take the `FONT<n>.DDS` beside the winner as the picture to draw from, and relocate the classic UV to the hit while adding **one texel of margin to image and screen in the same ratio**, so that the letter neither moves nor grows. Also theirs: the alpha ramp threshold 96 for what counts as ink, and the observation that the atlas of TR5 carries the classic glyph bitmaps unchanged. What is **not** taken and is not needed: their sprite scan and their "is this an alphabet" gate - the 106 glyph rectangles are known here (`game/chardef.inc`), so there is nothing to discover and nothing to guard against. New here: the per-glyph margin (a uniform one puts a slice of the neighbouring tick beside the apostrophe - measured), the seeding of that measurement from the classic page, the tpage sentinel that carries a quad to a texture outside `Textures[]`, the mip chain with its LOD bias, and the whole Direct3D 9 side. Their alpha-test change is not needed either: this engine's alpha reference is already 0. |

| `TOMB5 Code/TOMB5/specific/polyinsert.cpp`, the mip LOD bias of the HD font quad | `host/render/game_renderer.cpp` (TROne-main, copy of 2026-09-13; Copyright 2026 Todesengel, GPLv3), where the same bias stands with the same reason in TROne's own words | The **number and the reason for it**: a mip LOD bias of **-0.7** on the glyph quad, so that a font atlas minified by a D3D9 sampler keeps its edge instead of blurring into the next mip. The minification this project spends it on is its own measurement (1.8 at 1080p, 2.7 at 720p, 1.35 at 1440p), and so is where the bias lands at each of those (level 0.14 at 1080p, 0 at 1440p). |

| `TOMB5 Code/TOMB5/game/modern_move.cpp/.h`, `game/modern_camera.cpp/.h`, `game/modern_state.cpp` and `Preparation/Tools/Diagnose/ModernTest.cpp` | `tests/test_controller.cpp`, `host/input/` and `docs/CONTROLS.md` (TROne-main, copy of 2026-09-13) | The modern (analog-stick) control scheme. The **design** is TROne's and is re-derived here rather than copied: the stick's direction is measured against a held reference heading and turned into the classic engine's own tank bits, so that the game logic underneath never learns that anything changed. What was taken **verbatim** is the one thing worth having verbatim - the 32 expectations of TROne's own test suite (28 `modern.*`, 4 `combatmode.*`), rescaled to this engine's units by `S()`. Not taken: TROne's code. `game/modern_move.h` carries the origin block - which TROne version, and that the taboo table of 2026-09-16 found none of the set's 32 constants derived from a Remaster binary. |

The HD files above name Todesengel's copyright in their own headers, beside
our modification notice, as GPLv3 section 5(a) asks. In the last two rows the
notice sits in the file that carries the taking —
`specific/polyinsert.cpp`, `game/modern_move.h` and
`Preparation/Tools/Diagnose/ModernTest.cpp` — and the files named beside them
are this project's own code written to that design. Anything further will be
entered here as it arrives.

Not taken, and named here so the absence is on the record rather than an
oversight: TROne's `hd.light_stage` (an A/B switch that replaces the baked room
light with a flat ambient - a debugging pole for a renderer whose classic path
is a fallback; ours has the classic path itself), and its HD sky dome (this
engine draws a level's `.TRG` sky group with its own code, and where a level
has none, TR5's procedural sky under the HD walls). The distance fog, the fog
bulbs and the rooms' dynamic lights in `hd_room.hlsl` are transcriptions of
`specific/drawroom.cpp`; TROne-main (copy of 2026-10-05) has a fog model, fog
bulbs and room lights of its own (`set_fog_model`, `set_fog_bulbs`,
`set_room_lights`), and none of them is used here.

## Libraries compiled into the binaries

| Library | Licence | Full text in this tree |
|---|---|---|
| **Dear ImGui** — ocornut, <https://github.com/ocornut/imgui> | MIT | `TR5Randomizer/external/imgui/LICENSE.txt` |
| **zlib** — Jean-loup Gailly and Mark Adler | zlib licence | notice at the top of `TOMB5 Code/TOMB5/tomb5/libs/zlib/zlib.h` |
| **stb_vorbis** — Sean Barrett, <https://github.com/nothings/stb> | MIT **or** public domain, your choice | notice at the end of `TOMB5 Code/TOMB5/tomb5/libs/stb/stb_vorbis.c` |
| **stb_image** — Sean Barrett, <https://github.com/nothings/stb> | MIT **or** public domain, your choice | notice at the end of `TR5Randomizer/src/libs/stb/stb_image.h` |
| **TRForge** — TRForge contributors | MIT | `TOMB5 Code/TOMB5/vendor/trforge/LICENSE` |

Two of those libraries carry work by further authors, whose own notices sit
in the tracked files themselves and travel with every copy of this tree.
**zlib**'s `contrib/` tree — none of it compiled here, all of it distributed —
carries Gilles Vollant, Even Rouault, Mathias Svensson and Info-ZIP
(`contrib/minizip/`), Borland (`contrib/delphi/ZLib.pas`) and Henrik Ravn
(`contrib/dotzlib/`). **Dear ImGui** embeds two fonts that ARE compiled into
`TR5Randomizer.exe`: ProggyClean and ProggyForever, both MIT, by Tristan
Grimmer and, for the second, Disco Hello — their notices stand inline in
`imgui_draw.cpp`, and `BuildRelease.ps1` ships `external/` whole, so they
reach the recipient with the binary.

**TRForge** is vendored, not linked against: five files are copied into
`TOMB5 Code/TOMB5/vendor/trforge/src/` — the reader for the remasters'
`LEVELn.TRG` geometry and the whole-file helpers it needs — and compiled into
`PCTomb5.exe`. On the day of the copy, 2026-09-05, all five were byte for byte
TRForge's as TROne's own copy held them; since then three comment passages of
`trf_remaster/Trg.h` have been left out of this copy, each of which told in
words what the Remaster's renderer does with a `.TRG` field or with its point
lights. `vendor/trforge/VERSION.txt` names the source, the date, the SHA-256 of
every file then and of `Trg.h` now, the passages left out, and what was
deliberately **not** taken. The first three lines of a TRForge module carry a
hidden signature, so those files are never reformatted and never carry our
modification notice; anything we need differently is written in our own
wrappers under `TOMB5/specific/hd/`. `vendor/trforge/NOTICE.md` is the part of
TRForge's own notice that applies.

The sentences left inside those files are TRForge's own statements about
TRForge's own research, not this project's claims. The facts this project takes
from `Trg.h` are the field layout of the vertex - u and v as the fourth bytes of
the two colour triples, the layer counted from one, the normal biased by 127,
the sway weight over 32767 - and each is counted here on the player's own
`.TRG` files: `TrgProbe -uvbytes` (the corner order of 3,157,175 pairs against
1,465,989 for u, 3,354,530 against 1,340,180 for v, an even split for the
crossed reading), `TrgProbe -normals` (53.9 % of the triangle corners within 10
degrees of their plane under the bias, 0.5 % as signed bytes; the sway 0..32767
in thirteen levels), and the origin ledger below.

One TRForge file could **not** be vendored and was used as a description
instead: `trf_remaster/ItemModels.h` / `.cpp`, which includes TRForge's own
level library and would drag it in with it. What it gave us is the **format**
of a `.TRM` object model — the header, the part table, the meaning of the word
that is zero in every model but a head, the trick that finds a head's geometry
behind its facial rig, and the 24 byte vertex with its three bones and three
weights. `TOMB5/specific/hd/hd_trm.cpp` is written against that description,
with two exceptions its own header names: the facial-rig rescan and the
small byte reader beside it follow TRForge's code closely. That is lawful
under the MIT licence, and TRForge's notice travels with this tree in
`vendor/trforge/LICENSE`. Every number in its comments was measured again
here over the 566 models of the remaster's game 5.

The same file gave one more thing: the rule by which a slot
finds its model at all. TRForge's `bind_level_items` matches a slot on the
ENGINE'S OWN SYMBOL for it - `ID_` and `O_` prefixes off - and lets the level's
`.MAP` override where it names something, having measured that keying on the
English words found 60 of 211 `.MAP` keys while keying on the symbol found 197.
That rule holds across all five games, which is what lets
`TOMB5/specific/hd/hd_bind.cpp` name the four Tomb Raider III guests -
`TREX`, `VULTURE`, `COMPY`, `RAPTOR` - without a Tomb Raider I-III installation
to look in. The symbols themselves are TRForge's generated TR3 table
(`trf_level/ObjectNames.generated.inc`, slots 287, 29, 34 and 288); the four
names are written out in our own table, and nothing of TRForge's is copied for
it.
The credit is TRForge's all the same.

**What a `DUMMY` line in a `.MAP` means.**
`trf_remaster/HdMesh.cpp` (the note beside `items_dummy`, in TRForge's own
source, which is not vendored here) has it measured and written out - `DUMMY.TRM` is a real file with
no parts in it, 240 of the 2,940 bindings across both installs point at it, and
the remaster is saying *draw nothing here as an object*, "sometimes because it
has baked the thing into the room mesh instead, which is where the lanterns of
Rome live". TRForge names the consequence too: read the empty model, count it
among the ones you could not draw, and let the 1996 object be drawn instead,
and the lantern stands twice, one inside the other. That is exactly what this
engine draws without the rule, and
`TOMB5/specific/hd/hd_object.cpp` acts on TRForge's answer: a slot the level's
own `.MAP` calls `DUMMY` is drawn by nobody. The finding is TRForge's; the
seam, the per-item room test and the measurement that confirmed it over
ANDREA1's items are ours.

**The `.TEX` sidecar, and what an HD page is.** Two more TRForge files that
could **not** be vendored and were used as a description instead — both pull in TRForge's own asset and level
libraries, so neither is in `vendor/trforge`, which holds only what this tree
compiles. `trf_remaster/Remaster.h` writes down the format of `LEVELn.TEX` - a
page count, that many `.DDS` numbers, then one `uint16` per object texture of
the level saying which of them paints that tile - and `trf_remaster/HdPages.h`
writes down the thing the number really names: not the tile's rectangle but the
whole 64 x 64 CELL it sits in, repainted at eight times the size, with the
measurement behind it (of 17,164 tiles with HD art in the shipped games not one
crosses a cell boundary, and of 4,335 occupied cells not one wants two files).
`TOMB5/specific/hd/hd_scan.cpp`, `HDScanLevelTextures`, is written against that
description and not copied from it; the cell rule was measured again here over
five TR5 levels (0 cells wanting two files in every one) before anything was
drawn with it. The format and the finding are TRForge's; the reader, the guard
against a table that does not fit the level, and the use of it - the HD
pictures of the classic water animation frames - are ours.

**The `EXTRA` prefix a pickup's own model sits under.** TRForge's
`bind_level_items` (`trf_remaster/ItemModels.cpp`, the same file used as a description above) already showed the general shape -
a slot the `.MAP` is silent about tries the bare engine symbol next. TROne's
host code goes one step further for exactly the models that
fallback still lands on nothing for: TROne's `host/render/game_renderer.cpp`
tries `EXTRA<game>_<SYMBOL>` and then `EXTRA_<SYMBOL>` in front of the slot's
own name, and excludes an "Effect" category - `GUN_FLASH` among them - on its
own stated ground that such a slot is drawn bracketed inside another entity's
mesh walk rather than as a level item of its own (Copyright 2026 Todesengel,
GPLv3; not vendored, used as a description only, for the same reason as the
two TRForge files above - both pull in a foreign renderer). That ground is
TROne's, for TROne's renderer: measured against THIS engine the flash never
reaches the per-item path at all, which `MODIFICATIONS.md` writes up in area
2.4, "HD models for the pickups: the EXTRA prefix, a mod's own inventory model,
and lying on the floor" - the slot stays excluded either way.
`TOMB5/specific/hd/hd_bind.cpp`'s `bind_against` is
written against that description, not copied from it: the order the three
prefixes are tried in, and the rule that this project's own hand table is
asked BEFORE any of them, are this project's reading of it. The `EXTRA4_` tier
- for `COMPASS_ITEM` and `CROSSBOW_AMMO1/2_ITEM`, TR4 survivors TROne itself
can never reach because it always runs as its own game number - and the
measured table of which of the 471 slots resolves to which file on the
installed bundle are new, in the same entry of `MODIFICATIONS.md`.

### discord-rpc — shipped in the source tree, not in the binaries

| Library | Licence | Full text in this tree |
|---|---|---|
| **discord-rpc** — Discord, Inc., <https://github.com/discord/discord-rpc> | MIT | `TOMB5 Code/TOMB5/tomb5/libs/discordRPC/LICENSE` |

discord-rpc arrived from upstream TOMB5 as two prebuilt static libraries and
two headers, with no licence file. It is Troye's Discord Rich Presence, and
**this fork switches it off**: `TOMB5 Code/TOMB5/tomb5/tomb5.cpp` opens with
`#define TOMB5_DISCORD_RPC 0`, and every call site sits inside `#if
TOMB5_DISCORD_RPC`. The shipped `PCTomb5.exe` accordingly contains no
occurrence of the string `Discord` at all.

The notice still has to travel, because the two `.lib` files and their headers
are part of the source tree this project distributes — shipping someone's MIT
code without its notice is a violation whether or not a compiler used it. The
licence file above was added for that reason and is the project's own verbatim
copy of the upstream text.

**The two `.lib` files stay exactly as upstream shipped them**, and that is a
deliberate decision of this project's author: they are upstream
TOMB5's material, they are switched off here rather than removed, and they are
kept for completeness of what was inherited. Nothing in this fork links a
symbol out of them.

One thing about those prebuilt libraries this page has to say:
`discord-rpcd.lib` was built with **RapidJSON** statically inside it, so
a copy of this tree carries RapidJSON's object code as well. RapidJSON is
MIT-licensed, and its own notice reads: *"Tencent is pleased to support the
open source community by making RapidJSON available. Copyright (C) 2015 THL
A29 Limited, a Tencent company, and Milo Yip. All rights reserved."* The
upstream project also notes that RapidJSON's source includes `msinttypes`
(BSD, Alexander Chemeris) and a copy of the JSON checker test suite (JSON
License) — neither of which is used by anything here. The full terms are
RapidJSON's own, at <https://github.com/Tencent/rapidjson>; this project
vendors no RapidJSON source of its own, and names the holder here so that the
notice travels with the binary that contains the code.

The release package carries all of these in its `Licenses\` folder;
`Preparation/Tools/BuildRelease.ps1` puts them there.

## The origin ledger of the HD path

Every rule, constant, table, procedure and format fact of the HD path that came
by way of TROne or TRForge, one entry each: what it is; where it stands there
(by function, section or file, and the copy it was read in); its class; and this
project's own evidence for it. The classes: DATA (a fact about the player's files,
countable on them), PICTURE (a fact seen in the running game's pictures or video),
CLASSIC (the classic engine's own code), OWN (TROne's own design, under the GPLv3),
THIRD (another project's code under its licence), PLACE (where a file is). "Free
choice" says that nothing of ours decides the question and this project chose. The
counts are reproduced by the tool switch named; `MODIFICATIONS.md`, section 2.4,
entry "Where the HD rules come from", carries the tables.

1. **The `.TRG` container** - header, light cubes, groups, parts of 44 bytes with
   three index runs that tile the index list, the 20-byte vertex. TRForge,
   `trf_remaster/Trg.h` and `Trg.cpp`, vendored (copy of 2026-09-05). DATA.
   Ours: `read_trg` refuses any file whose sizes do not end where the file ends;
   all fifteen levels read (`TrgProbe -all`).
2. **Group i is room i.** TRForge, `Trg.h`. DATA. Ours: 107,118 of the 126,170
   classic room vertices of the fifteen levels have a `.TRG` vertex of the same
   group at exactly their position (`TrgProbe -shadeorder`).
3. **u and v are the fourth bytes of the two colour triples** (+15, +19). TRForge,
   `Trg.h`, which named them from the Remaster's shaders (that prose is left out
   of our copy). DATA. Ours: on 15,999 classic faces with `.TRG` vertices inside,
   the classic u orders vertex pairs as +15 does in 3,157,175 pairs and the other
   way in 1,465,989, the classic v as +19 in 3,354,530 against 1,340,180, the
   crossed reading splits even (`TrgProbe -uvbytes`).
4. **The layer byte counts from one.** TRForge, `Trg.h` (249 of 273 faces). DATA.
   Ours: every vertex of the fifteen files has a layer from 1 to its file's
   texture count (`TrgProbe -uvbytes`); none names layer 0 (`TrgProbe -layer0`).
5. **The normal is biased by 127.** TRForge, `Trg.h`. DATA. Ours: 53.9 % of
   45,170,724 triangle corners lie within 10 degrees of their triangle's normal
   read so, 0.5 % as signed bytes (`TrgProbe -normals`); for the `.TRM` normal
   38.1 % against 0.6 % over 565 files of `5\ITEM` (`TrgProbe -trmnormals`).
6. **The sway weight is over 32767.** TRForge, `Trg.h`. DATA. Ours: 0..32767 in
   thirteen levels, never below 0, none in RICH1 and RICHCUT2
   (`TrgProbe -normals`).
7. **The room light is the SUM of the two colour triples.** TROne, `k_hd_frag`
   (TROne-main, copy of 2026-10-05), resting on TRForge's reading of the
   Remaster's shaders. DATA. Ours: on the 107,118 paired vertices the sum orders
   them as the classic prelight does with a rank correlation of 0.553, the
   product 0.460, `colour1` 0.397, `colour2` 0.527; of the sampled pairs sum and
   product put in opposite order the prelight sides with the sum in 121,606 and
   with the product in 56,907; better than the product in 14 of 15 levels
   (`TrgProbe -shadeorder`). The rule rests on these rank counts, which no scale
   on a triple changes. Picture: RoomProbe's RICH2 X-ray view against the
   Remaster recording of the same place, the corridor wall's and floor's mean
   brightness at 1.13 and 1.21 of the recording's under the sum (0.20 and 0.26
   under the product, `colour2` alone 0.61 and 0.48); it shows the shipped scale
   is in the recording's range, and with two regions of one frame it cannot
   tell the sum from a scaled other rule.
8. **The `.TRM` layout** - 44-byte part, 24- or 30-byte vertex, three bones and
   three weights, the zero word and the head geometry behind a facial rig.
   TRForge, `trf_remaster/ItemModels.h`, as a description (TROne-main's vendored
   copy). DATA. Ours: counted over the 566 files of `5\ITEM` (`hd_trm.h`).
9. **The facial-rig rescan and the byte reader beside it.** TRForge,
   `ItemModels.cpp`. THIRD (MIT code followed closely, credited in `hd_trm.cpp`).
   Ours: 565 of the 566 files of `5\ITEM` load through it, the heads among them,
   and the one refused is `DUMMY.TRM`, which has no geometry
   (`TrgProbe -trmnormals`).
10. **A rig joint stands at -R^T t of its inverse bind matrix.** TROne, its
    vendored `ItemModels.h` (copy of 2026-10-05). DATA (TROne's measurement over
    54 heads). Ours: LipsTest puts the frame-0 joints of ten faces on the rig's
    joints 0.0002 to 3.46 units apart (`hd_trl.cpp`).
11. **A slot finds its model by the engine's own symbol, the `.MAP` overriding.**
    TRForge, `bind_level_items`. DATA. Ours: the table of all 471 slots against
    the installed bundle (`MODIFICATIONS.md` 2.4, the EXTRA prefix entry).
12. **The `EXTRA<game>_` and `EXTRA_` prefixes, and the "Effect" slots left
    out.** TROne, `hd_load`'s prefix pass. DATA and OWN. Ours: the same 471-slot
    table; `GUN_FLASH` measured never to reach the per-item path.
13. **A `DUMMY` model means "draw nothing".** TRForge, `HdMesh.cpp` (TROne-main's
    vendored copy). DATA. Ours: ANDREA1's map calls 14 of its slots `DUMMY`
    (`MapProbe <5> ANDREA1.TRC`, column dummy). The count of the art's own
    triangles inside each such item's box (1250, 730 and 5001 against at most
    18 elsewhere, `hd_object.cpp`) was made with a probe that is not in this
    tree: that part of the evidence is missing here.
14. **The `.TEX` sidecar and the 64 x 64 cell an HD page repaints.** TRForge,
    `trf_remaster/Remaster.h` and `HdPages.h`, as a description. DATA. Ours:
    over five TR5 levels no cell wants two files.
15. **The level build's shape and the draw rule** - one buffer, a range table
    per group, groups past the rooms are the sky, opaque / alpha / additive with
    the additive pass `ONE, ONE` and no depth write. TROne, `hd_load` and
    `hd_draw_room`. OWN. Ours: the groups, sky groups, ranges and page changes of
    the fifteen levels (`TrgProbe -all -classic`), and RoomProbe's pictures of
    the engine's own build.
16. **A vertex whose layer is 0 takes the level's first page.** TROne,
    `hd_load`. OWN. Ours: no vertex of the fifteen levels carries layer 0
    (`TrgProbe -layer0`), so it never fires on them.
17. **The alpha test lives in the alpha pass only.** TROne, `k_hd_frag` and
    `k_trm_frag`. OWN. Ours: the opaque pages' alpha measured as a mask
    (`hd_pagecache.cpp`, 0 % against 51 to 95 % for the cut-out sheets).
18. **The additive pass weighted by the texture's alpha** (rooms). TROne,
    `k_hd_frag`. OWN; free choice to keep it.
19. **The water-mark road** - classic water vertices matched to `.TRG` vertices
    by a hash of the packed int16 position, the owning group read from the index
    runs. TROne, `hd_load` and `hd_water_mark`. OWN. Ours: the pairing count of
    entry 2.
20. **The water's wave, swell, UV wobble and caustic shapes**, the caustic's
    scale 0.018 and six texels of wobble. TROne, `k_hd_vert` and `k_hd_frag`
    (the swell and the wobble from its copy of 2026-09-05). OWN; free choice to
    keep TROne's numbers. The caustic's strength 0.32 is ours, matched to two
    reference shots of one pool (PICTURE; TROne gives 0.12).
21. **Material 16's ripple held to the part's own UV span, nothing moving in
    the vertex.** TROne, its own material 16 water rule. OWN. Ours: `sway` is 0
    on 27 of the 28 parts; 23 of 28 live in a sub-rectangle of their page.
22. **Which of material 16's param[0] and param[2] is the scale and which the
    rate.** TROne's prose names the order, read off the Remaster's shaders.
    DATA for what is known: both move with the area the same way (0.1 to 5.0 and
    0.035 to 0.5, `TrgProbe -mat 16`). The order itself is a free choice; ours
    coincides with TROne's and is not taken as evidence (`hd_render.cpp`,
    `MaterialRipple`).
23. **Statics baked into the art are not drawn again** (the double lantern).
    TROne, `draw_mesh`. OWN. Ours: `RoomProbe -statictable` over the fifteen
    levels.
24. **The object path** - one `.TRM` replaces the whole model, bone numbers are
    mesh-tree nodes, the bind pose is the tree's offsets, a head rides rigidly on
    the node nearest its centre, an uncovered node is drawn by nobody, an unseen
    node keeps the last matrix. TROne, `build_skinned_model`,
    `bind_pose_of_slot`, `hd_load`'s head loop, `fill_unseen_joints`. OWN and
    CLASSIC. Ours: `ModelProbe -all` (the skeleton drift), `ModelProbe -nodes`.
25. **The item bracket and the swap rule** - wholly HD or wholly classic; a swap
    mesh on a covered node loses to the model. TROne, `item_flush` and
    `draw_mesh`. OWN. Ours: `ModelProbe -name MAFIA` (the swap files).
26. **The ring's off-centre projection folded into the draw, its light as
    ambient and one sun.** TROne, `games/tr5/glue/hostglue_2d5.cpp`,
    `phd_PutPolygonsPickup` (copy of 2026-10-05). OWN (the
    idea; the arithmetic is TR5's). Ours: `ModelProbe -ringcheck`, `-ringlight`.
27. **Lara's composite and the role table.** TROne, `lara_flush`, `lara_load`,
    `RegisterLaraWardrobe`. OWN. Ours: the section table (`ModelProbe -lara`).
28. **The braid, one frame per link.** TROne, `draw_hair`, and the classic
    `game/hair.cpp`. OWN and CLASSIC. Ours: `braidcheck` in `hd_laradraw.cpp`.
29. **The HD font's template match** - ink at alpha 96, intersection over union,
    0.94, a texel of margin in the same ratio, the prefilter 0.85 to 1.20.
    TROne, `hdtext_find` and `hdtext_arm`. OWN. Ours: `FontProbe -atlas` over the
    106 `chardef.inc` glyphs.
30. **The glyph quad's mip LOD bias of -0.7.** TROne, `k_ui_frag`. OWN. Ours:
    the minification 1.8, 2.7 and 1.35 measured on this engine's output.
31. **The glow's screen composition, near fade and `|N.V|` term.** TROne, its
    rules for its glow material. OWN. Ours: RoomProbe on ANDREA2 room 131.
32. **Where a page is looked for: its own game, the sibling games, then
    `WHITE.DDS`; a missing page is white, never black.** TROne,
    `host/render/hd_pages.cpp` (`upload_shared`, `upload2`). OWN and PLACE.
    Ours: RICH3 names three pages only `4\TEX` has; `WHITE.DDS` is in `4\TEX`
    only.
33. **The Steam discovery, by content and not by folder name.** TROne,
    `host/fs/discovery.cpp`. PLACE. Ours: `TrgProbe -find`.
34. **The `.TRL` track layout of the faces.** TROne, its porting notes for TR4
    and its mimic measurement. DATA. Ours: `LipsTest` over 45 files of `5\ITEM`.
35. **The modern control's design, its 32 test expectations and six default
    constants.** TROne, `tests/test_controller.cpp`, `host/input/` and
    `docs/CONTROLS.md` (TROne-main, copy of 2026-09-13). OWN. Ours:
    `ModernTest`; nine constants re-measured in play, the Remaster's timings
    from video (PICTURE).
36. **Material 1's scroll in param.xy.** Read so by TROne as well. DATA. Ours:
    calibrated against the classic UVROT tile in RICH2; 5 to 65 parts a level.
37. **Materials 13 and 15, the flipbook and the books.** Read the same way by
    TROne. DATA. Ours: the four-phase cycles and the thirty-page loops counted
    in `hd_render.cpp`.
38. **`param[0]` of materials 6, 7 and 8 is the cut-out threshold.** Read the
    same way by TROne (TROne-main, copy of 2026-10-05, its rules for the
    cut-out materials), whose reading rests on the Remaster's shaders. DATA.
    Ours: 0.5 in 42 of the 44 per-level tuples of the three materials over the
    fifteen levels, the other two ANDY1's 0.503 on material 6 and ANDY2's 0.12
    on material 8 (`TrgProbe -all -mat 6`, `-mat 7`, `-mat 8`); every other
    material's `param[0]` is measurably something else - an exponent on 3 and
    17, page numbers on 13 and 14, a preset on the additive material 2
    (`hd_render.cpp`, `MaterialAlphaRef`); and the pages the three wear are
    half hole or more where the opaque pages' alpha is a mask (`hd_pagecache.cpp`,
    the per-page measure).
39. **On a `.TRM` the material, not the index run, decides the cut, and
    material 0's alpha stays a mask.** TROne, `docs/SHADER_RULES.md` (copy of
    2026-09-05), on its own data. DATA and OWN. Ours: of the three cut-out
    materials only 6 and 8 occur in the `.TRM` files of `4\ITEM` and
    `5\ITEM`, both with 0.5 in `param[0]` in every part, and only 6 in the
    opaque run; the page's share of texels at exactly 0 among those below the
    reference (`HDPageCacheSlotZeroPermille`, `hd_model_cut.h`; ModelProbe's
    page lines) puts the cut-out sheets at 325 per mille and up and the pistol
    grips at 4, and material 0's page 9000 of the Roman statue at 715, which
    the material rule keeps whole.

Removed in the same round, so that nothing of the HD path rests on a reading of
the Remaster's programs: the spare scale on the first colour triple (TROne's
room-shadow factor, always 1 here); the sibling names `<n>_1024.DDS` and
`<n>_2048.DDS` tried for ids 8000..8499 (35 pages ship that way, all in the
I-III bundle's `1\TEX`, all beside a plain `<n>.DDS`); and the passages of
`Trg.h` that told in words what the Remaster's renderer does with a vertex and
with the point lights. The engine manual's HD chapter points here.

## Credited, but not contained here

trview (chreden), TRmod (b122251), TR Viewer (E. Popov) and Tomb Raider
Chronicles itself are named in the credits because the work would not exist
without them. None of their code is in this repository.

## The pictures in this repository

Five of them are artwork created by the author with an AI image generator
(ChatGPT) for this project: `docs/banner.jpg`,
`TR5Randomizer/src/gui/Background.jpg`, `TOMB5 Code/setup.bmp`,
`TOMB5 Code/icon.ico` and `TR5Randomizer/src/gui/TR5Randomizer.ico`. No
third-party picture was a source for any of them. It is said here because the
files themselves do not carry the content credentials such a tool writes.

The screenshots under `docs/` show the game running, so they necessarily show
Core Design's level geometry and textures, and the ones taken in HD mode show
art that is read at run time from the player's own *Tomb Raider IV-VI
Remastered* installation. None of that art is included here.

## No game data

No playable Tomb Raider content ships with this project — no levels, no
scripts, no sounds, no music, no videos, no language files, no textures, no
models. The randomizer reads what it needs from the player's own installation
at run time, and the four Tomb Raider III enemies are read from a Tomb Raider
III installation the same way. Without those installations, the affected
features simply do not appear.

Two things are derived from the original levels, and both are worth naming
rather than glossing over.

**In the release: `data\zones_*.json`, 15 files, about 1.2 MB.** Per level a
list of sectors with the floor height and headroom the randomizer needs to
know where an item may legally sit, plus this project's own curation on top
of it — key constraints, fixed items, coupled triggers, which enemy types
belong in that level. The curation is ours; the sector coordinates and
heights are measurements of Core's level design.

**And in the repository as well:** the same 15 zone files and the
two checksum tables beside them are baked, zlib-packed, into the tracked
source file `TR5Randomizer/src/core/EmbeddedData.cpp` (about 7,200 lines,
0.75 MB), generated by `Preparation/Tools/EmbedData.ps1`, so that the
randomizer carries its data built in and the build needs no generator. A file
in `data\` beside the program still wins at run time. That copy is the same
derived material as the release's.

**In the repository, not in the release: `Preparation/Level Infos/*.json`,
16 files, about 21 MB.** The fuller survey the zone files are generated
from: rooms, items, triggers and the sector grid, keyed to each `.TRC` by
SHA-256. They do not ship: the randomizer reads what it needs straight from
the level file, and they stay in the repository, where the build needs them.

Neither set contains a texture, a mesh, a sound or a script. Neither can be
rendered or played, and neither can reconstruct a level file. They are a
survey of geometry, not a copy of it. But they ARE derived from the game's
level design, so calling this project entirely free of Tomb Raider data
would be overstating it.
