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
combination. TROne-main was read in four downloads: 2026-09-05, 2026-09-13,
2026-10-05 and 2026-10-06. On 2026-10-06 TROne restarted its public history at
commit 75c4c843 with a tree cleaned under the same origin rule as this project,
so the three earlier states are no longer published; a citation that names one
of them records what was taken from it, and every citation of the reference
copy (2026-10-06, commit 75c4c843) was checked against that state on
2026-10-07. Where the cleaned state differs from what was taken, the row says
so. Since its round K32, TROne's own input code carries this project's modern
movement core, orbit camera and tests, named as such in its `docs/LEGAL.md` and
`docs/CONTROLS.md`; the modern-control rows below describe what this project
took from TROne's state of 2026-09-13, before that. What was taken, file by
file:

| Here | From TROne | What |
|---|---|---|
| `TOMB5 Code/TOMB5/specific/hd/hd_install.cpp/.h` | `host/fs/discovery.cpp` | Steam discovery: the registry lookup, the `libraryfolders.vdf` reader in both of its shapes, the "recognise by content, never by folder name" rule, and the wording of the diagnostics. The IV-VI branch is taken, and so is the I-III branch - the same table row, `tomb123.exe` beside subfolders 1..3 with `DATA` and `TEX` - because the four Tomb Raider III guests the randomizer imports have their HD models in that bundle. The rest of TROne's discovery (the classic installs, the per-game level counts, the PIX / TRACKS / FMV folders) is not taken. |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp/.h` | `host/render/game_renderer.cpp`, `hd_load` and `hd_draw_room` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The shape of the level build and of the draw: one buffer for the whole level, a range table per group, group *i* is room *i*, groups past the room count are the sky dome, three passes per room in the order opaque / alpha / additive, and the additive pass blended `ONE, ONE` with the depth write off. Also the rule that a vertex whose texture layer is 0 falls back to the level's first page instead of drawing nothing. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl` | the same file's GLSL `k_hd_vert` / `k_hd_frag` (TROne-main, copy of 2026-10-06, commit 75c4c843) | Taken: the alpha test living in the alpha pass **only** (an opaque tile's alpha is a gloss mask, and testing on it blanks whole floors), and the additive pass weighting its colour by the texture's alpha - both TROne's own design. Not taken: the lighting rule. That the room light is the **sum** of the vertex's two colour triples, each byte over 255, with the pixel `texel * light`, is this project's own count: on the 107,118 classic room vertices a `.TRG` vertex of the same room stands on, the sum orders them as the classic prelight does better than the product or either triple (`TrgProbe -shadeorder`, the origin ledger below). In its copy of 2026-10-05 TROne's sum rested on TRForge's reading of the Remaster's shaders; in 75c4c843 it rests on TROne's own count over its `.TRG` files, and the room-shadow scale on the first triple is marked its own design; this shader has no such scale. Translated to HLSL for shader model 3; not a line of the GLSL is copied. |
| `TOMB5 Code/TOMB5/specific/hd/hd_pagecache.cpp/.h` | `host/render/hd_pages.cpp`, `upload2` / `fill_missing_tiles` | Where a page is looked for when it is not in its own game's `TEX` folder — the sibling games next, `WHITE.DDS` last — and the rule that a page that cannot be found is painted **white** and never black. The upload itself is new: TROne hands the driver BC7 blocks in one texture array, and Direct3D 9 has neither. |
| `TOMB5 Code/TOMB5/game/draw.cpp`, `DrawStaticObjects` | `host/render/game_renderer.cpp`, the baked-mesh rule in `draw_mesh` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The rule itself, not the code: a static mesh the remaster baked into the room art must not be drawn a second time (TROne's "double lantern" bug report). TROne suppresses by mesh pointer and needs an exception for a moveable that claims the same pointer; our seam skips one room's own `MESH_INFO` list, for which that exception cannot arise. |
| `TOMB5 Code/TOMB5/specific/hd/hd_model.cpp` | `host/render/game_renderer.cpp`: `build_skinned_model`, `bind_pose_of_slot`, the model and head loops of `hd_load` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The rules of the object path: one `.TRM` replaces the **whole** classic model; a vertex's three bone indices are nodes of the classic mesh tree; the bind pose is that tree's accumulated offsets with the rotations left out, walked with the same push and pop; a `<NAME>_HEAD.TRM` is drawn on the node nearest the face geometry's own centre (measured, not hard-coded) - **rigidly** here, where 75c4c843 splits the neck onto the parent node by default and keeps the rigid draw as the fallback; a part's three index runs are the three passes, additive blended `ONE, ONE` with the depth write off; and a node the caller never gave a matrix for gets the last one it did give rather than collapsing to the origin (`fill_unseen_joints`). The renderer under them is new: TROne is OpenGL 3.3 with vertex arrays and texture arrays, this is Direct3D 9 with a vertex declaration, one texture per page and compiled bytecode. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_model.hlsl` | the same file's GLSL `k_trm_vert` / `k_trm_frag` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The skinning rule — three bones and three weights per vertex, the bind offset off before the node matrix goes on, the normal through the same matrix's 3×3 part — and the alpha test in the alpha pass **only** (TROne's golden-skull report). Translated to HLSL for shader model 3, with the bind offset folded into the matrix on the processor instead of subtracted per vertex; not a line of the GLSL is copied. The lighting is **not** TROne's: its item light in its copy of 2026-09-05 was a flat ambient blown into a six-face cube plus four point lights of its own (in 75c4c843 the cube carries direction from the `.TRG` light cubes), while this engine has the classic path's own per-object light list, distance fog and fog bulbs. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp/.h` | `host/render/game_renderer.cpp`: the item bracket of `item_flush` and `draw_mesh`, `fill_unseen_joints`, `model.covered = ~0u` in `build_skinned_model` and the swap-key rule beside `item_flush` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The **routing rules**, not the code: an item is drawn either wholly HD or wholly classic and never half of each; a node the model has no geometry for is drawn by NOBODY, not even classically (TROne tried the classic fallback there and took it out again after user reports of classic patches on HD bodies); a swapped mesh on a covered node loses to the model, so the model draws what the remaster drew; and a node the walk never reached keeps the last matrix it saw rather than collapsing to the origin. The seam itself is ours - TROne is a module beside a foreign engine and reads its brackets out of a stream of draw calls, this one rides on `DrawAnimatingItem`'s own node walk - and so are the skeleton test, the `mesh_bits` node ranges and the whole of the lighting. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp`, the inventory ring (`HDInvBegin` / `HDInvNode` / `HDInvEnd`) | `games/tr5/glue/hostglue_2d5.cpp`, `phd_PutPolygonsPickup`, and the `TRONE_LIGHT_EXACT_NODES` flag of `host/api/trone_host_api.h` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The **idea**, not the code, and only half of it was kept: that the classic ring's off-centre projection - it moves the projection centre to each item's place on the screen - can be folded into the ITEM for the length of one draw, so that the HD path keeps one projection for the whole frame; and that the ring's light collapses to the ambient plus one sun taken from the ring's own brightness. TROne folds it into the item's own matrix as a **shear** of two rows against the third; this engine does **not**, because its shader takes the vertex normal through those same matrices and renormalises it, so a shear lights the ring by screen position (measured on the shader: 28 % too dark in the middle of the inventory row, 16 % too bright on the ammo row). The offset goes into the projection matrix instead, per draw call (`HDModelOffCentre`, `hd_model.cpp`), where the classic path has it and where a normal cannot reach it. The arithmetic was derived from TR5's own projection line (`aTransformLightClipMesh`, `specific/output.cpp`) and is written for this engine's matrices, not for TROne's float stack; the derivation stands in full over `HDInvNode` and over `HDModelOffCentre`. TROne also needs a flag to make the ring draw **exactly** the nodes it was given, because its default invents a matrix for an unseen node; ours takes `item->mesh_bits` as a mask and needs none. Not taken: TROne's scale term `f_mpersp/persp`, which corrects for a host projection built from the window's real width - TR5 computes `f_persp` from a 4:3 implied width whatever the window is, and the HD projection here is built from that very number, so the factor is exactly one (measured: `ModelProbe -ringcheck`, the bounding rectangle at the same **size** on 4:3 and on 16:9). |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp`, the water marks | `host/render/game_renderer.cpp`, `hd_load` and `hd_water_mark` (TROne-main, copy of 2026-10-05) | The **road**, not the code: the remaster's geometry carries no water flag, so the classic level's own water vertices are matched to it by room-local position through a hash of the packed int16 x/y/z, per room, once at level build - and which group owns which vertex is recovered by walking the group's index runs. In its copy of 2026-10-05 TROne marked a vertex from the classic `RoomVertexMove` flag and from the room's underwater bit, one bit; in 75c4c843 it classes water, shore and underwater-dark from the engine's own split, keeps the hash for shore corners and matches water surfaces as horizontal planes. Ours marks it from `ROOMLET::nWVtx` and from the engine's own shimmer predicate, and carries three bits rather than one. |

| `TOMB5 Code/TOMB5/specific/hd/hd_lara.cpp/.h`, and `hd_wardrobe.cpp/.h` with it | `host/render/game_renderer.cpp`: `lara_flush`, `lara_build_models`, `lara_load`, `draw_hair`, and `games/tr5/glue/hostglue_file5.cpp`: `RegisterLaraWardrobe` (TROne-main, copy of 2026-10-06, commit 75c4c843) | The **composite itself**: Lara is not one model but an outfit for the body, a separate `LARA_HEAD` drawn rigidly on the head node (75c4c843 splits the neck onto the parent node by default), a `HAIR_OG` braid on its own six-bone chain, a `HAND_` model chosen by the slot whose mesh sits at the hand node, and a `HOLSTER_` model for a worn weapon; an empty holster draws nothing because the straps are in the outfit; and the whole wardrobe lives in the shared `4\ITEM` folder, not in the running game's. The role table (which slot means which model) is TROne's, name for name. What is **not** TROne's: the section table - its TR5 glue of its copy of 2026-09-05 had `ANDY` and `JOBY` the wrong way round and would put the Russia outfit on Ireland (fixed in 75c4c843, which goes by `GF_YOUNGLARA` and `LaraDrawType` and records that the old mapping stood on its head) - and the outfit test, which here reads the engine's own `LaraDrawType` and its own braid predicate rather than the level's file name. Also this project's own is the split hand rule: TROne drew one hand model for both hands in its copy of 2026-09-05 (TROne-main, copy of 2026-10-06, commit 75c4c843, has a split of its own), which loses TR5's flare, torch and crowbar frames, so a frame whose two hands differ draws both models masked to one arm each. The role table and the search that finds those models live in `hd_wardrobe.cpp/.h`, which is why that file stands in this row rather than in one of its own. |

| `TOMB5 Code/TOMB5/specific/hd/hd_laradraw.cpp/.h` | `host/render/game_renderer.cpp`: `draw_hair` (its copy of 2026-09-05; in TROne-main, copy of 2026-10-06, commit 75c4c843, `draw_hair` skins the links its glues build the same way) | The **braid rule**: `HAIR_OG` has one bone per link, so all six links get a frame of their own built from the link's absolute position and its two angles - where TR5 itself builds a matrix for links 1, 3 and 5 only and draws 0, 2 and 4 as skinned connectors out of stashed vertices. Everything else in that file is this engine's own: the LMX-to-LM turn of `lara_matrices`, the per-node ambient out of `LaraNodeAmbient`, the hand-slot pointer comparison, the first-person mask and the mirror. |

| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl`, the wind | `host/render/game_renderer.cpp`: `k_hd_vert`'s `hd.water_effect` block (its copy of 2026-09-05) | The **wind's shape and its scale**: two sines of the position taken across each other, about 24 classic units of travel and a wavelength of about 1.4 blocks - the shape that block gave TROne's water in that copy, applied here to the `.TRG`'s own `TrgVertex::sway` weight, which TROne does not read. Translated to HLSL. The water itself - its wave, swell, UV wobble and caustic - is this project's own design (the origin ledger below, entries 40 to 86) and takes no shape or number from TROne; the water shapes this row credited to TROne before are recorded in `MODIFICATIONS.md`, entry "Water in the HD rooms". Not taken: TROne's water flag, a room bit in its copy of 2026-10-05 (75c4c843 classes water and shore from the engine's own split), where ours is the classic path's own water marks (the water marks row above) and the classic floor data's water squares. One observation stays TROne's: TR5's own brightness term, the `WaterTable` shimmer, is computed per vertex, and a remaster water surface is a few large quads, so on its own it reaches the screen as a slow blotch - which TROne's own comment reported about TR4 water in its copy of 2026-09-05 (in 75c4c843 its basin note says the same of a ripple). That is why a per-pixel term is added at all: the shimmer is untouched, parity and all, and the caustic on top of it is a zero mean modulation that carries no average brightness of its own. |
| `TOMB5 Code/TOMB5/specific/hd/hd_fontmatch.cpp/.h`, `hd_font.cpp/.h` and the branch in `game/text.cpp` | `host/render/game_renderer.cpp`, the block its own code marks `hdtext` (`hdtext_find` and `hdtext_arm`), and the glyph draw beside it. **No line numbers**, on purpose: no line range for it could be checked against a copy of TROne that can be read here, so the block is named by its own marker instead — the reasoning stands over `hd_fontmatch.h` | The **method**, not the code: find a classic glyph in the remaster's `FONT<n>_OG.DDS` by sliding its ink mask over the atlas and scoring **intersection over union**, refuse anything under **0.94** because a similar character is the wrong character, take the `FONT<n>.DDS` beside the winner as the picture to draw from, and relocate the classic UV to the hit while adding **one texel of margin to image and screen in the same ratio**, so that the letter neither moves nor grows. Also theirs: the alpha ramp threshold 96 for what counts as ink, and the observation that the atlas of TR5 carries the classic glyph bitmaps unchanged. What is **not** taken and is not needed: their sprite scan and their "is this an alphabet" gate - the 106 glyph rectangles are known here (`game/chardef.inc`), so there is nothing to discover and nothing to guard against. New here: the per-glyph margin (a uniform one puts a slice of the neighbouring tick beside the apostrophe - measured), the seeding of that measurement from the classic page, the tpage sentinel that carries a quad to a texture outside `Textures[]`, the mip chain with its LOD bias, and the whole Direct3D 9 side. Their alpha-test change is not needed either: this engine's alpha reference is already 0. |

| `TOMB5 Code/TOMB5/specific/polyinsert.cpp`, the mip LOD bias of the HD font quad | `host/render/game_renderer.cpp` (TROne-main, copy of 2026-09-13; Copyright 2026 Todesengel, GPLv3), where the same bias stands with the same reason in TROne's own words | The **number and the reason for it**: a mip LOD bias of **-0.7** on the glyph quad, so that a font atlas minified by a D3D9 sampler keeps its edge instead of blurring into the next mip. The minification this project spends it on is its own measurement (1.8 at 1080p, 2.7 at 720p, 1.35 at 1440p), and so is where the bias lands at each of those (level 0.14 at 1080p, 0 at 1440p). |

| `TOMB5 Code/TOMB5/game/modern_move.cpp/.h`, `game/modern_camera.cpp/.h`, `game/modern_state.cpp` and `Preparation/Tools/Diagnose/ModernTest.cpp` | `tests/test_controller.cpp`, `host/input/` and `docs/CONTROLS.md` (TROne-main, copy of 2026-09-13) | The modern (analog-stick) control scheme. The **design** is TROne's and is re-derived here rather than copied: the stick's direction is measured against a held reference heading and turned into the classic engine's own tank bits, so that the game logic underneath never learns that anything changed. What was taken **verbatim** is the one thing worth having verbatim - the 32 expectations of TROne's own test suite (28 `modern.*`, 4 `combatmode.*`), rescaled to this engine's units by `S()`. Not taken: TROne's code. `game/modern_move.h` carries the origin block - which TROne version, and that the taboo table of 2026-09-16 found none of the set's 32 constants derived from a Remaster binary. Since its round K32 the flow runs the other way as well: TROne's `host/input/trone_modern_core.h` is this file's core taken over under the GPL, and its orbit is this project's camera (TROne-main, copy of 2026-10-06, commit 75c4c843, `docs/CONTROLS.md` 4.7 and 4.8, `docs/LEGAL.md`); the 32 expectations cited here stand by name in its suite there (31 `modern.*` now, three new). |

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
`specific/drawroom.cpp`; TROne-main (copy of 2026-10-06, commit 75c4c843) has a
fog model, fog bulbs and room lights of its own (`set_fog_model`,
`set_fog_bulbs`, `set_room_lights`), and none of them is used here.

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
   (TROne-main, copy of 2026-10-05, resting on TRForge's reading of the
   Remaster's shaders; in 75c4c843 on TROne's own count). DATA. Ours: on the
   107,118 paired vertices the sum orders them as the classic prelight does
   with a rank correlation of 0.553, the
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
    vendored `ItemModels.h` (copy of 2026-10-06, commit 75c4c843). DATA (TROne's
    measurement over 54 heads). Ours: LipsTest puts the frame-0 joints of ten
    faces on the rig's joints 0.0002 to 3.46 units apart (`hd_trl.cpp`).
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
20. **The wind's shape** - two sines of the position taken across each other,
    about 24 units of travel, a wavelength of about 1.4 blocks.
    TROne, `k_hd_vert`'s water block (copy of 2026-09-05), where the same
    shape moved TROne's water. OWN; free choice to keep TROne's numbers for
    the wind. The water's wave, swell, UV wobble and caustic, which this entry
    credited to TROne before (`MODIFICATIONS.md`, "Water in the HD rooms"),
    are this project's own design: entries 40 to 86.
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
    `phd_PutPolygonsPickup` (copy of 2026-10-06, commit 75c4c843). OWN (the
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
    `docs/CONTROLS.md` (TROne-main, copy of 2026-09-13; since its round K32
    TROne carries this project's core and orbit, see the TROne section above).
    OWN. Ours: `ModernTest`; nine constants re-measured in play, the Remaster's
    timings from video (PICTURE).
36. **Material 1's scroll in param.xy.** Read so by TROne as well. DATA. Ours:
    calibrated against the classic UVROT tile in RICH2; 5 to 65 parts a level.
37. **Materials 13 and 15, the flipbook and the books.** Read the same way by
    TROne. DATA. Ours: the four-phase cycles and the thirty-page loops counted
    in `hd_render.cpp`.
38. **`param[0]` of materials 6, 7 and 8 is the cut-out threshold.** Read the
    same way by TROne (TROne-main, copy of 2026-10-05, its rules for the
    cut-out materials, there resting on the Remaster's shaders; in 75c4c843 on
    its own count, with material 8 cut at a fixed 0.5). DATA.
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

**This project's own water.** Entries 40 to 86 came by way of neither TROne nor
TRForge: the water sheets, their wave and their caustic are this project's own
design out of textbook graphics (sums of sines, Schlick's fresnel, Blinn-Phong,
exponential absorption, foam by shore distance, refraction by Snell's law, a
copy of the frame and a depth texture read back) and its own counts. In these
entries OWN is this project's own design - a free choice, a count on our data, or
tuned by eye on reference pictures supplied for this project (not in the
repository) - DATA a measurement on the player's files with the tool switch
that repeats it, and CLASSIC the classic engine's own code, as above (entry 71,
the game's own ripples). `MODIFICATIONS.md`, entry "The water sheets: this project's own
water surface", carries the tables.

40. **The water square.** A square of a room that is not under water whose room
    below (`FLOOR_INFO::pit_room`) is an underwater room; its surface the
    square's own floor, clicks times 256; in a room drawn HD it draws its
    sheet whatever flat art the Remaster lays over it (its own water art
    there is hidden, entry 77; the rest of its art stays on top). OWN, free
    choice. Count on our data: 577 places in the fifteen levels, all
    of them drawing, and 122 more squares of the second rooms of flipmap pairs
    (`RoomProbe -hdwater 1 -nodraw`).
41. **The depth walk** - down through further water portals to the first floor
    that is not one, at most six rooms; a wall or a floor at or above the
    surface is depth 0. OWN, free choice.
42. **The body join** - squares four-connected at one world height across
    rooms; a flipmap twin joins the body of the square at its place. OWN, free
    choice. Count: 81 bodies, no square held by two rooms that are no flipmap
    pair, 122 squares of a twin (`RoomProbe -hdwater 1 -nodraw`, `twins=`).
43. **The class rule** - sea when a room of the body carries `ROOM_OUTSIDE` or
    `ROOM_NOT_INSIDE`; channel when indoors, 16 squares or more and twice the
    area over the perimeter under 2; basin otherwise. OWN; the thresholds a
    count on our data: ANDREA2's sewer (44 squares, 1.83) a channel, its
    fountain (36, 2.25) a basin, JOBY2's water along the submarine a sea.
44. **The wave rows of the three classes** - four wavelengths, heights and
    directions each (the first four of the sheet's eight, entry 66, and the
    rows of the Remaster's marked water), the calm factor, a channel's current
    of 180 units a second, the swell's share and taper. OWN, tuned by eye on
    reference pictures supplied for this project.
45. **Deep-water dispersion**, omega = calm * sqrt(g k) with g = 4300 units
    a second squared: 9.81 m/s^2 at 2.3 mm a unit, Lara's 768 units standing
    for 1.77 m. OWN, free choice of the scale.
46. **The palette** of the four sections - shallow, deep, highlight and foam
    colours. OWN, tuned by eye on reference pictures supplied for this
    project; to be judged in the game.
47. **The body light** - the squares' rooms' mean colour1 + colour2 over their
    light samples, weighted by squares, times 1.8, never under 0.30 nor over
    1.5. OWN; count on our data: the bodies' means run 0.09 to 0.55 a channel
    over the bodies of ANDREA2 and JOBY2 (`RoomProbe -hdwater 1 -nodraw`, the
    `WATERLOG` body line's `raw=`); gain and floor chosen on the pictures, the
    cap of 1.5 a free choice, set by eye on RoomProbe pictures.
48. **The light directions** - indoors 55 degrees up at azimuth 40, out of
    doors 22 degrees up at azimuth 200. OWN, free choice: TR5 has no sun; the
    low open-air light follows the JOBY2 dome's bright horizon
    (`RoomProbe -sky -skyonly`).
49. **The fresnel and the reflection colours** - Schlick with F0 = 0.02 (water,
    n = 1.33); indoors the body light times 0.65 at the horizon and 0.25
    overhead, plus or minus 40 % by a slow pattern and a finer second one
    taking 0.4 of it, under the sky plus or minus a quarter by the slow pattern
    alone (the indoor numbers a free choice, set by eye on RoomProbe strips
    of ANDREA2's fountain and room 76); a broad lobe of the
    highlight colour, (R.L)^8 at 0.35; Blinn-Phong exponent 90 for a basin and
    a sea, 70 for a channel; strength 0.8 indoors, 0.5 out of doors. OWN, tuned
    by eye on reference pictures supplied for this project.
50. **The sky table** - one method for every row: `RoomProbe -sky -skyonly
    -black` at 960 x 540 from 1024 0 1024 in JOBY2's room 42, ANDY1's 132 and
    ANDY3's 17; the horizon the mean of the rows 6 to 40 pixels above the
    picture's middle toward four directions (yaw 0, 90, 180, 270), overhead
    the mean of the dome straight up (pitch -85) where it covers the view.
    JOBY2 horizon 152, 154, 171, overhead 96, 93, 116 (the dome covers all of
    it); ANDY1 and ANDY3, one dusk dome, horizon 113, 78, 67 and overhead 11,
    19, 32 (its clouds, 41 % of the view). DATA.
51. **The absorption lengths** - 4000 units for a basin, 1500 for a channel,
    900 for a sea - and the Snell window from below (its numbers in entry
    85). OWN, tuned by eye; a clear basin and a murky channel a project
    decision of 2026-10-07.
52. **The foam's band and strengths** - a 300-unit band without the depth
    texture, strengths 0, 0.7 and 0.85, whitecaps 0.6 above half the wave
    height, the blotch pattern 1.5 times finer for a channel and a sea; the
    foam's shape and motion in entries 68 to 70. OWN, tuned by eye; the finer
    grain a project decision of 2026-10-07.
53. **The caustic's shape** - one minus half the absolute sum of two
    domain-warped smoothed-triangle waves, to the sixth power, 0.0224 radians
    a unit (a cell of 280); 0.30 on a pool floor fading out over 2048 units
    down, 0.18 (0.6 of that) on the Remaster's marked water. OWN; the scale and
    the two strengths a free choice, set by eye on RoomProbe pictures.
54. **The caustic's normalisation** - its mean 0.2310 and spread 0.2964,
    divided out. OWN: a count on the curve itself, 400,000 samples.
55. **The caustic's footprint fade** - full up to 10 units a pixel, gone at 20:
    a bright line is a median 25.5 units across at half its height. OWN: the
    width a count on the curve itself, 400 lines along x and z sampled every
    half unit; the fade a free choice. The glow shader fades it the same way.
56. **The wave's footprint fade and bent crests** - a row fades by
    2 - 1.3 times its phase step to the next pixel, along its own direction
    (entry 84); the position is pushed about by a quarter of the longest
    wavelength along a slow pattern. OWN, free choice.
57. **The sheet's grid and bytes** - 5 x 5 vertices a square; the depth over
    32 and the distance to the nearest square without water within three
    squares over 16 in colour1's red and green; the depth averaged over the
    squares that meet at a vertex. OWN, free choice.
58. **The wobble and the swell on the Remaster's marked water** - 4/512 page a
    unit of slope, and the class's share of the wave height, tapered by the
    shore box. OWN, free choice; the wobble set by eye on RoomProbe pictures.
59. **The override table and `hd_water.txt`** - five compiled rows pinning the
    classes the rule gives at ANDREA2 144, 194 and 60 and JOBY2 42 and 143; a
    row of the file `<LEVEL> <room> <class|-|none>` with `tint`, `waves`,
    `foam` and `bend` (entry 86), applied after the table to every body its
    room has water in;
    `none` draws no sheet over those bodies, which keep their class. OWN, free
    choice; the pins a count on our data (`RoomProbe -hdwater`).
60. **The sheet's distance fog** - the water loses the share of its colour
    that the fog takes of the body's light, col * (1 - fog / light), the light
    the mean of the body's three channels or 1 for a body that reflects the
    sky, so it goes black where a room lit like it does. OWN, free choice.
61. **The refraction by the slope** - the floor read at a place moved by the
    wave's slope times the absorption length times 1 - 1 / 1.33 times 1 - T,
    which for a short path is the small-angle refraction of a ray through a
    tilted surface and stops growing where the floor fades; carried to the
    screen by solving the pixel's footprint (`ddx`, `ddy` of the world
    position); at most 256 units and 4 % of the screen; from below at the full
    slope. OWN: the strength derived from water's index, the two bounds a free
    choice, set by eye on RoomProbe pictures.
62. **The absorption per channel** - the channels absorbed in the ratio of
    ln(shallow / deep) of the section's palette, their mean the class's one
    over the absorption length, a channel the palette does not darken at a
    sixteenth of the mean; the floor dimmed by exp(-path * sigma) per channel,
    the scattered colour as before. OWN: the rule a free choice, the ratios a
    count on the palette (Rome 1.31 / 0.82 / 0.87, Russia 1.32 / 0.94 / 0.74,
    Ireland 1.24 / 0.84 / 0.92, VCI 1.31 / 1.03 / 0.66 of the mean).
63. **The depth mask** - the bent place refused, and the pixel's own place
    read, where the frame's depth there is nearer than the sheet. OWN, free
    choice (`RoomProbe -waterdepthview 3` shows the refused pixels).
64. **The water path out of the depth** - the view depth between the sheet
    and what was drawn behind it, times the eye distance over the sheet's view
    depth; the depth straight down that path times the view's vertical
    share; the projection undone as z = b / (a - d). OWN: geometry of the
    projection this engine already uses; `RoomProbe -waterdepthview 1|2`
    repeats it (view 2 is mid grey where it agrees with the floor data).
65. **The frame's copy and the depth texture** - a copy of the render target
    by `StretchRect` before the blend phase, and an `INTZ` texture as the
    depth-stencil where the adapter offers the format - standard Direct3D 9
    techniques of general graphics knowledge. OWN: where in the frame the
    copy is taken and when the texture is bound are this project's own
    choices.
66. **The sheet's spectrum** - eight rows a class: the four of entry 44 and
    three at the geometric mean of two neighbours' wavelengths and one at the
    shortest over 1.5 (moved two units off a whole fraction of another row),
    each 0.8 of its neighbours' mean steepness high, directions at least 0.15
    radians from every other row's; sine and cosine as Wave2's curve. OWN,
    free choice.
67. **The envelope and the phase form** - each row's height times
    1 - e + e (0.5 + 0.5 sin(2 pi t / P + i golden)), where the depth e
    is 0.30 for a basin, 0.35 for a channel and 0.50 for a sea and the periods P
    of the eight rows are 8.0, 13.7, 10.3, 15.0, 9.1, 11.9, 14.3 and 12.6
    seconds; the phase omega t plus the row's start (0 for the first four,
    multiples of the golden angle for the rest) wrapped in double precision on
    the processor. OWN, free choice, set by eye on RoomProbe strips.
68. **The foam's density and reach** - at most 0.75, a soft threshold that runs
    from only the peaks of the pattern at the outer edge to nearly all of it at
    the bank; the reach times 0.55 to 1.0 by a swash of 4.5 seconds whose phase
    runs along the bank, plus 0.35 of the wave's height there, over the
    class's band from the classic shore in both variants (the depth texture's
    own share is the waterline, entry 76). OWN, free choice, set by eye on
    RoomProbe pictures.
69. **The foam's motion and variety** - the coarse layer drifting at a third
    of the fine one's speed, a slow third layer giving each region of blotches
    its own phase for a threshold that swings over 5 seconds; a smooth field of
    two Wave2 layers (periods about 6,700 and 8,900 units) moving the pattern by
    up to 400 units and shifting the swash - moving it and not turning it: a
    blend toward a turned copy of world-sized coordinates multiplied the
    pattern's frequency where half done and drew a comb along the banks. OWN,
    free choice; a field rather than a number per square, which would draw a
    seam at every square's edge.
70. **The trailing whitecaps** - a cap where the eight rows stand above the
    class's share of their whole height now, and, at 0.7 and broken up by the
    slow layer, where the two longest rows stood half a second earlier. OWN,
    free choice.
71. **The game's own ripples** - `ripples[]` (`game/tomb4fx.cpp`), 128
    `RIPPLE_STRUCT` (`global/types.h`), set up by `SetupRipple` and grown by
    `UpdateSplashes` (`game/effect2.cpp`): Lara in the water and drips with
    flags 0x10, bubbles at the surface, rats and shells with 0x02, raindrops
    with 0, the underwater blood of `TriggerUnderwaterBlood` with 0x20; `init`
    counts up by 4 a tick (8 with flags 2) while `size` grows by 4 (2). CLASSIC:
    the sheet draws a ring where the classic engine drew a flat sprite
    (`S_DrawSplashes`, `specific/specificfx.cpp`), for the ripples with 0x10 or
    0x02 and without 0x20; the rain and the blood keep the sprite.
72. **The ring** - started where a ripple slot is new (or taken into a live
    ring, entry 79), aged by what its `init` has counted, then three and a
    half seconds on the frame's clock; a front running out at 150 units a
    second from half the ripple's first size, a train of waves 90 units long
    gone two wavelengths behind it, a slope of 0.5 times the first size over
    128 (at most 1) times the square of the life left and over the square
    root of 1 + d / 256; the eight youngest within 512 units of a sheet draw's
    squares and 256 of its surface. OWN, free choice, set by eye on RoomProbe
    pictures (`RoomProbe -ripple`) against the play test of 2026-10-08 (too
    restless, too quick, fading too soon).
73. **The ripple row of `hd_water.txt`** - `ripple <x> <z> <size> <seconds>`
    on any row: a ring at that world place every so many seconds, its phase
    from the place, at the surface of the row's room's square under it. OWN,
    free choice; for a fountain where the game makes no ripple of its own
    (ANDREA2 194: no item in it or in room 120 below, `ItemCheck`). The
    compiled row of entry 59 for ANDREA2 194 carries one (x 55,808, z 32,768,
    the middle of the basin's six squares; size 80, 0.7 seconds, a free
    choice set by eye in the game with a row of the file), with the sheets
    alone; a row of the file with a ripple for the same room takes its
    place. The body a source stands in keeps it as its spring (entry 163).
74. **The clip** - with the depth texture and the camera above the water, a
    sheet pixel is not drawn (`clip()`) where the frame lies less than 8 units
    under the surface at rest (the swell's lift handed from `WaterVS` and taken
    back), so a deck, a rim or a bank at the surface's own height shows itself.
    OWN, free choice; 8 against the depth's own error, measured: one value to
    a sixteenth of a unit over JOBY2 66's flat pool floor at 2560 x 1440
    (`RoomProbe -waterdepthview`), about 1.2 units at the far plane by the
    24-bit arithmetic, and the Remaster's planes 2 to 9 units off the classic
    height.
75. **The refraction guard** - with the depth texture, from above, the bent
    place is refused where the frame there lies within 96 units of the surface
    at rest or above it, and the move is the one the shorter of the two paths
    (here or there) gives: 1 - T of the shorter over 1 - T of the pixel's own.
    OWN; 96 rests on a measurement of the classic data: a swimming rat stands
    50 units under the surface (`game/rat.cpp`) and its mesh reaches 10 below
    its place (`ModelOffset` on the classic ANDREA2.TRC, slot RAT, y -84 to
    10), so its belly lies 60 down, under the 48 the guard took before (set by
    eye on JOBY2's pier posts and the floe); `-waterdepthview 3` shows the
    refused pixels.
76. **The waterline** - with the depth texture, foam where the frame lies
    within 24 units under the surface at rest, gone at 48, broken by the foam
    pattern (a soft threshold 0.25 to 0.6), at the class's foam strength;
    held to 0.25 by a bare bank and left out round Lara (entries 88, 89). OWN,
    free choice, set by eye on RoomProbe pictures.
77. **The art over a sheet** - a draw call wholly of the Remaster's own water
    art (horizontal triangles of material 16 or water-marked at every corner),
    within 32 units of a sheet's surface over a square whose sheet draws, is
    hidden unless an `hd_water.txt` row says `art keep`. OWN, free choice; the
    32 is the slack of the flat-art question (`HDRenderGroupFlatArt`). Count on
    our data: 2048 triangles in ANDREA2, 2384 in ANDY2, 15906 in JOBY5, none
    elsewhere (`RoomProbe -hdwater 1 -nodraw`, WATERLOG `art_hidden`).
    "Over a square" means over one of the squares the call's own art
    triangles cover (`HDRange::artCells`); the call's box is only the first
    test, because a call with art on both sides of a sheet's square has a box
    over it too (JOBY5's deck, room 4). A correction of this rule, OWN, the
    same free choice. Count on our data with the game's water marks
    (`RoomProbe -hdwater 1 -nodraw -watermarks`): JOBY5 15906 triangles and
    one call kept (WATERLOG `art_kept`), 15942 by the box alone; the other
    fourteen levels the same either way. The river's flow-material calls of
    entry 106 take the same test with their own squares
    (`HDRange::flowCells`), the same free choice; count on our data: ANDY3's
    five such calls, 612 triangles, all over their own squares, so every
    level's counts stay as they were.
78. **The motion clock** - every motion of the HD path runs on the drawn
    state's time, the tick clock less the part of a tick the blended pose
    stands behind it (`hd_book.h`, `HDBookClock`), so it moves on every frame
    at sixty frames a second and more. A rule of this project.
79. **One ring a body** - a new ripple of the same kind (a body's, flags
    0x10, or a small one's) within 192 units of a live ring and 256 of its
    height goes into that ring: taken as its own while the ring is younger
    than 0.6 s, a restart from the new ripple when it is older. OWN, free
    choice; the kinds a count on the game's own callers (`WadeSplash` sets a
    ripple every four ticks, flags 16 standing and 18 moving).
80. **The foam's body** - the foam's cover times the pattern taken as a
    height of up to 24 units; its rise toward the class's light, the screen
    gradient against the screen step of one unit toward the light, lights it
    with 0.55 + 0.45 N.L to first order over what a flat layer gets, never
    under 0.3; 0.65 where the layer is thinnest, and a rim 1 - 1.4 c (1 - c)
    where its cover c is half. OWN, free choice, set by eye on RoomProbe
    pictures (relief tried at 8, 24 and 48); a diffuse term and a height
    field's gradient are textbook shading.
81. **The breaking crest's lift** - on a body with whitecaps the vertex rises
    by up to a quarter of the whole wave height more where the wave stands
    above the whitecaps' threshold, reached a quarter of the height above
    it. OWN, free choice.
82. **The caustic projected by the face** - the caustic of entry 53 read in
    the plane of the face normal's major axis: x and z on a floor, z and y or
    x and y on a wall, the normal from the screen derivatives of the
    position; one sample, the planes never blended; the footprint fade of
    entry 55 by the pixel's step in all three axes. OWN: the triplanar
    projection of texturing in its one-sample form, a standard technique of
    general graphics knowledge; one sample a count on the slots (a second
    costs about 20 of the 512). The gloss variant keeps the horizontal plane.
83. **The body at eye level** - the waves' slope times the class's gain
    (basin 3, channel 2, sea 1) where the view runs within about 9 degrees of
    the surface (|V.y| 0.15), not at all from 37 degrees down (0.6), linear
    between; the rings added after it. OWN, free choice, set by eye on
    RoomProbe pictures; the view from above, which the play test of
    2026-10-08 judged good in ANDREA1 and ANDREA2, keeps its look.
84. **The rows' fade along their own direction** - each row's phase step to
    the next pixel right and the next pixel down, its wave vector against
    the world step of one pixel each way, that step the inverse of the
    projection's derivative at the vertex; carried to the pixel per vertex.
    OWN: the fade's thresholds those of entry 56; the per-direction measure
    is the sampling bound of a sine, general graphics knowledge.
85. **The view from below** - the Snell window's edge 0.61 to 0.71 of N.V
    (round the critical angle's 0.66), alpha 0.35 of the shallow colour
    outside with the bent frame showing through the rest, 0.1 inside: a tenth
    of the reflection colour and nine tenths of the bent frame. OWN, free
    choice, set by eye on RoomProbe pictures and on the reference pictures of
    the play test of 2026-10-08 (the surface from below opaque and milky);
    before it, 0.75 of the deep colour outside and 0.2 inside.
86. **The `bend` key of `hd_water.txt`** - the refraction's strength of entry
    61 times `bend` (0 to 4, 1 without the key), per row. OWN, free choice;
    1 by default, so the look from above stays as the play test found it.
87. **The bank foam's patches** - a field worked out per vertex: three layers
    of cells, each the product of two Wave2 along axes turned by 0.35, 1.25
    and 2.20 radians, periods 3,800, 5,400 and 7,400 units, each times a
    clock of 26.3, 33.7 and 45.9 seconds whose phase turns once, twice and
    three times over the variety field of entry 69 and a quarter turn with
    the next layer's cell; their sum over 1.5. A patch where it stands over
    0.05; the band of entry 68 reaches 1.5 times the square root of the
    excess over 0.4. A channel's patches drift at 0.3 of its current. OWN,
    free choice, set by eye against the reference pictures supplied for this
    project (foam on parts of a bank, in lobes of different sizes); count on
    the arithmetic (a script over eight bank lines of 24,000 units for ten
    minutes): foam on 0.45 of a bank's length, a patch 320 / 864 / 2,080
    units long and living 11 / 25 / 68 seconds at the tenth, half and ninth
    tenth. Cells of products of sines and a threshold are general graphics
    knowledge.
88. **The faint line where no patch is** - with the depth texture the
    waterline of entry 76 held to 0.25 within the band of a bank and outside
    a patch, 0.75 elsewhere; without it a line over the band's innermost 24
    units at 0.25 of foam, broken by the pattern. OWN, free choice.
89. **Lara's gap in the waterline** - no waterline within 320 units of Lara's
    place, back in full 64 further out, while she swims at the surface or
    wades (`lara.water_status`); her rings stay. A rule of this project: a
    wet line round her body read as white paint on her (the play test of
    2026-10-08); 320 and 64 free choices that cover her 768 units.
90. **The waterfall's foot** - every WATERFALL1 to 3 and WATERFALLSS1 and 2
    object (not FISHTANK), its classic mesh placed as `DrawAnimatingItem`
    places it (the item's place and turn, the frame's root offset); its foot
    the vertices within 64 units of the lowest, an object no taller than that
    a scrolling face; over a sheet where a foot point lies over a square that
    draws one with the surface at most 256 units over the lowest vertex or
    1,024 under it. OWN, free choice; count on our data: 31 of the fifteen
    levels' 46 waterfall objects over a sheet, every one with a gap of 0
    (RoomProbe `-hdwater 1 -nodraw`, the `fall=` lines).
91. **The art's falling water as the foot** - where the room is drawn HD, the
    lowest vertices (within 64 units) of a flow part (material 1) more than
    512 units tall that lie within a square of the classic foot and over the
    same sheet take its place. OWN, free choice; DATA for the reason: ANDREA2
    room 231's curtains land about 450 units from the classic object's foot
    (`TrgProbe -parts 231`, the `foot=` lines with and without the art), and
    its slot WATERFALL1 is DUMMY in the level's `.MAP`
    (`MapProbe -map ANDREA2 -v`), so the art draws that fall.
92. **The curtain and its footprint** - the objects over one body whose foot
    points come within 512 units of each other's are one curtain; its points
    in order along their principal axis, gathered every 512 units, a corner
    at the mean of every step that holds points: the footprint, a polyline;
    a segment over a step without points is a gap where no water lands and
    draws no foam (entry 117; it bridged the gap at 0.85 of the foam until
    the fifth play test of 2026-10-09); the reach 614 units plus 0.3 of the
    longest wet stretch, held to 614 to 1,229 (0.6 to 1.2 squares). OWN,
    free choice, set by eye on RoomProbe pictures; one curtain for the
    objects of one foot is the owner's ask of the play test of 2026-10-09
    (ANDREA2 room 231's two discs a strand read as two crowns turning
    alike).
93. **The foot's foam** - a second draw of the sheet's triangles (`FootPS`),
    up to eight segments a draw: per segment the pixel's distance to it, its
    way along it and its side; the reach times 0.8 plus 0.2 of entry 87's
    patch field and 0.85 plus 0.3 of a slow lobe field; the pattern carried
    outward at v0 sqrt(1 - x) (v0 220 units a second, x the distance over
    the reach) by reading it at the way a mark has come, 2R (1 - sqrt(1 -
    x)), less v0 t; across the line at 0.75 of its scale along it; the two
    sides moved 260 units apart over 160 units either side of the line; the
    bank foam's two layers (entry 68), the fine one at 0.55 of the speed,
    its dissolve, its density with the threshold along (1 - x) to the power
    0.75, its body (entry 80); the segment's strength times the body's foam
    scale; past a segment's end the way beyond it over that end's head
    (entry 117, 1 round); nothing from below; no turn. OWN, free choices, set by eye on
    RoomProbe pictures; the push and stretch away from the curtain tuned by
    eye on reference pictures supplied for this project (the owner's sketch
    of a foot from above, a photograph of a real waterfall, as a look and
    no file); a pattern carried along a flow by the way it has come is
    general graphics knowledge.
94. **The foot's rings** - a source every 512 units along the footprint's
    wet stretches (at least one, at the middles of equal parts), each a
    ripple source of entry 72's ring, size 64, re-armed every 0.4 to 0.8
    seconds by a hash of its place. OWN, free choice.
95. **The drops at the foot** - `TriggerSmallSplash` (`game/tomb4fx.cpp`), the
    engine's own splash of a rat or a shell landing in water, on the logic
    tick beside `AnimateWaterfalls`. CLASSIC: the effect and its sparks are
    the classic engine's, unchanged; OWN: two drops a tick at every source
    within eight squares of the camera and sixteen a tick at most, free
    choices. What makes the splash of ANDREA1's fountain is no effect of the
    engine but the remaster's own art, a spray part of material 1
    (`TrgProbe -parts 59`, DATA); it is not copied.
96. **No stretch of a waterfall that stops short** - a rule of this project:
    a fall whose bottom stopped short of the surface would be stretched down
    to it; none does (entry 90's count), and the art cannot be stretched
    without rewriting its vertex buffer. What reads as short in ANDREA2 room
    231, the curtain's light fading toward its foot (measured on the additive
    pass: half its peak about 290 units over the water, a tenth 62), is met
    by entries 93 to 95 instead.
97. **The `foot` key of `hd_water.txt`** - `foot <x1> <z1> <x2> <z2>
    <reach>` (64 to 2,048) a curtain of entries 92 to 95 along that line on
    the surface under its middle, `foot off` no objects' curtains in the
    row's bodies; `swirl <x> <z> <r>` reads as a foot of length 0 and
    `swirl off` as `foot off` (the level line's `old=`). OWN, free choice.
98. **A water square's surface at its water's top** - the surface of entry
    40's square is the highest corner of the water room's ceiling over it,
    not the dry square's floor byte, which a split sector raises to its
    highest corner. DATA: the classic floor data (RoomProbe `-hdwater 1
    -nodraw`, `lowered=`; RICH2's two corner plates 512 over the water,
    ANDY3's 29 split squares of the pond's island and room 12); OWN: taking
    the ceiling's highest corner as the surface, the height
    `GetWaterHeight` breaks Lara's head out at, and keeping a split square
    whole under the level's raised triangle, the owner's ask of the play
    test of 2026-10-09.
99. **No water square over an island or a rim** - a square whose dry floor
    stands more than a click (256) over that top at every corner is none.
    OWN, free choice of the click; count on our data: ANDY2's 16 squares of
    rooms 57, 136, 158 and 159 stand 1,024 over their water, every other
    level none (`dropped=`).
100. **The corner heights out of the slope words** - the engine's own
    `GetHeight` and `GetCeiling` (`game/control.cpp`), evaluated at a
    square's corners with the substitution of the randomizer's
    `Tr5Level::cornerFloor` and `cornerCeiling`; the floor's entry as
    `GetHeight` walks a chain, the ceiling's as `GetMinimumCeiling` reads
    it. CLASSIC: the arithmetic is the engine's; OWN: the substitution is
    this project's. Count on our data: 55,760 of 55,760 squares of the
    fifteen levels agree with `Tr5Level` corner for corner (RoomProbe
    `water corners:`).
101. **The creatures' mark** - the HD objects' solid draw marks the stencil's
    bit 4 under every pixel it draws, and the sheet's depth variant draws a
    range twice, split by that bit: the marked pixels without the waterline
    of entry 76. A rule of this project: the wet line on a swimming rat
    read as Lara's did (the play test of 2026-10-09); the stencil split is
    general graphics knowledge, chosen because it costs the depth variant
    no slot where a list read in a loop would take it over 512.
102. **The river class** - a body whose water names a `TO_SINK` trigger is
    a river; its flow the mean way from its places toward their sinks, its
    speed the mean of 120 units a second per strength. DATA: the sinks of
    the classic floor data and camera table (`flow=`, `sinks=`; ANDY3's two
    rivers only); CLASSIC: 120 units a second is `LaraWaterCurrent`'s
    arithmetic (`game/laraswim.cpp`, 4 units a tick at 30 ticks a second);
    OWN: the class and the mean.
103. **The river's rows** - eight rows 1.11 to 1.25 radians off the flow
    (crests 2 to 3 times as long along the current as across), heights at
    most 1.2 units on 300, carried at the body's speed held to 720 units a
    second, no bank foam, no whitecaps. OWN, free choice, set by eye on
    RoomProbe strips; the dispersion with a current is entry 45's.
104. **The river's streaks** - `RiverPS`: two sets of ridges across the
    flow about 410 and 170 units apart, wandering slowly along it, carried
    1.4 times as fast as the rows, broken by a slow field whose threshold
    swings over 5 seconds, at most half of the foam's cover, lit as entry
    80's foam. OWN, free choice, set by eye on RoomProbe pictures; ridges of
    a periodic curve are general graphics knowledge.
105. **The foam where a river ends** - the bank foam's pattern of entry 68
    laid across a river over 768 units upstream of where its squares end
    along the flow (colour1's blue, the way to the first point with no
    water), carried into the end at 0.3 of the speed, dense at the end,
    nothing at the upper end. OWN, free choice, tuned by eye on reference
    pictures supplied for this project (a weir's foam half to one square
    deep). Its pattern is now entry 116's; the reach and the speed stay.
106. **The remaster's flowing surface hidden over a river** - a draw call
    wholly of flat material 1 triangles within 32 units of a river's
    surface over its squares is left out, as entry 77 leaves out the still
    water. OWN rule; DATA for the reason: ANDY3's rooms 38, 41, 42 and 43
    carry one each, 90 to 108 triangles three units over the water,
    scrolled at 2.5 page units a second (`RoomProbe -animtex`, `TrgProbe
    -parts`), 378 triangles in all.
107. **The `river` class and the `flow` key of `hd_water.txt`** - a row may
    force the class `river`; `flow <dx> <dz> <speed>` (the way -1 to 1 each,
    not both 0, the speed 0 to 4,000 units a second) sets a body's current
    in place of its sinks'; a river with neither runs along its long axis at
    240 units a second. OWN, free choice.
108. **The extinction under the water** - with the eye in a water room of a
    level with sheets, every pixel of `RoomPS`, `GlowPS` (no scatter: a
    glow is dimmed only), `ModelPS` and the sheet's `HD_WATER_UNDER` variant
    goes col exp(-L sigma_rgb) + scatter (1 - exp(-L sigma_s)), L the eye
    ray's length in the water (the whole distance under the surface, d D / h
    over it), sigma_rgb the body's per-channel absorption of entry 62,
    sigma_s its one over the absorption length (entry 51), both times 0.3
    (0.35 until the fifth play test of 2026-10-09), the scatter its shallow
    colour (entries 46 and 47), held by entry 118, times 1 + 0.6 of the
    ray's upward cosine; L less entry 118's clear zone. OWN: the textbook
    extinction and in-scattering of a participating medium, general
    graphics knowledge; the 0.3 and the 0.6 free choices, tuned by eye on
    RoomProbe pictures and on reference pictures supplied for this project
    (the owner's picture of the flat haze, a photograph under water, as a
    look and no file).
109. **Which body an eye under the water looks through** - an underwater
    room takes the body most of the sheet squares over it belong to, a room
    further down the body of the room over it (six rooms down), a water room
    no sheet lies over the level's largest body, with no surface over it
    (entry 121). OWN, free choice; count on our data: the level line's
    `under=` and `under_fallback=` (ANDREA2 19 and 20, JOBY2 9 and 2, JOBY4
    2 and 132; RoomProbe `-hdwater 1 -nodraw`).
110. **What the extinction takes the place of** - under the water the
    distance fog (its start moved past any view) and the fog bulbs (none
    that frame); the gloss variant, with no slots left, is not drawn under
    the water (entry 119). DATA
    for the reason: ANDREA2's sixteen fog bulbs in its water rooms, colour
    0.81 1.00 0.87, radius 1,472 to 5,152 - the flat pale haze of the play
    test of 2026-10-09 (the rooms' headers, `nFogBulbs` and `fogbulb`, read
    with the randomizer's `Tr5Level`); OWN: the replacement.
111. **The flood under a sheet** - in a room whose body draws its sheet and
    hides the remaster's art, RoomVS's flood lays no swell, UV wobble or
    surface caustic on any vertex (`gSwell.z`, set by `set_room_rows`); the
    colour shimmer is untouched. OWN, a rule of this project; DATA for the
    reason: ANDY3's pond has no remaster water art in its classic planes (0
    `.TRG` triangles, entry 77's measurement), so every flooded vertex there
    is the bank's, and JOBY5 room 4's deck squares are dry floor at the
    water's height (RoomProbe pictures at three clock times; the squares
    read with the randomizer's `Tr5Level`).
112. **The waterline where a shape meets the water** - the depth variant's
    line weighed by the change of the frame's depth under the surface from
    pixel to pixel (`fwidth`), whole from 6 units a pixel, its fall from 24
    to 48 units a straight ramp. OWN, free choice, set by eye on RoomProbe
    pictures; a screen-space derivative is general graphics knowledge.
113. **The creatures' mark's log line and its foam-free pass** - one
    WATERLOG line a level with `-log` saying whether the frame marked and
    why not, whether the mark and the sheets used the INTZ surface, the
    objects, the sheet draws and the split draws; the marked pass of a range
    draws no foam (`gWaterFoam.w` and the whitecaps 0). OWN, a rule of this
    project; RoomProbe `-twophase` draws the two phases in two brackets as
    the game does.
114. **A sink in the water** - a sink of the camera table named by a
    `TO_SINK` command that lies in a body's water (inside one of its
    squares, at or under its surface, above the floor) pulls the body: the
    mean way from the squares that name it to the sink, the speed of its
    strength; counted only where those squares number at least a quarter of
    the body's own (`kRiverCover`). OWN rule, free choice between the
    counts; DATA: the sinks, their places and strengths and the squares that
    name them (the camera table and the classic floor data; RoomProbe
    `-hdwater 1 -nodraw`, `lying=` and `refused=`: ANDY3's river of rooms 38
    and 41 at 1.0, its sea of rooms 10 to 17 at 0.05).
115. **The river's dashes** - `RiverPS` breaks each streak into dashes a few
    hundred units long carried with the flow, their phase turned by the way
    across. OWN, free choice, set by eye on RoomProbe strips.
116. **The foam blob where a river ends** - three layers of cells about 830,
    360 and 150 units across along axes turned by 0.6, 2.1 and 1.3 radians,
    the finer warped by the coarser, carried into the end, the reach
    swinging across the river and with the coarsest cells, densest at the
    end; replaces entry 105's lattice. OWN, free choice, set by eye on
    RoomProbe pictures; the weir's photograph supplied for this project as a
    look and no file.
117. **The gap between two strands** - a step of a curtain's footprint
    without foot points draws no foam (the seg line's `wet=0.00`); in a
    curtain with such a gap every wet stretch ends in a head: past its last
    point the zone reaches half its reach along the line, beside a gap no
    more than 0.4 of the gap's length over the reach, so two heads reach at
    most 0.92 of the gap at the outline's widest swing; a corner alone
    between gaps is a segment of 2 units with those heads; a curtain
    without a gap keeps round ends. OWN, a rule of this project, the half
    a reach a free choice by the owner's sketch of the foot from above
    (two separate ovals with open water between them; reference pictures
    supplied for this project, as a look and no file), the 0.4 a free
    choice from the count of FootPS's spread (1.15 at the most).
118. **The clear zone and the held scatter under the water** - entry 108's
    path less 2,048 units (two squares, the third-person camera's reach),
    never under 0, in `RoomPS`, `GlowPS`, the model pixel shader and the
    `HD_WATER_UNDER` variant (c145); the scattered colour's brightest
    channel held to 0.45. OWN: a medium clear near the eye and a bound on
    the in-scattered light are general graphics knowledge; 2,048 and 0.45
    free choices, set by eye on RoomProbe pictures of JOBY2's flooded base
    and ANDREA2's corridor; DATA for the reason: the scattered colour of
    the body of JOBY2's room 7, 0.55, 0.85, 0.94 under a light of 1.45, took
    0.7 of the mix at three squares (RoomProbe `-underwater` with the
    randomized level's floor data).
119. **No gloss variant under the water** - while the frame's eye is under
    the water the gloss variant's faces draw with `RoomPS`, without the
    sheen, inside the extinction. OWN, a rule of this project; DATA for the
    reason: at the owner's view of JOBY2's flooded base the walls, crates
    and floor plates the gloss variant drew kept their colour against the
    faded rest (RoomProbe `-underwater`, `-noreflect`, `-glossfield`).
120. **Lara's wet nodes in water a sheet lies over** - they take the
    ambient of the dry room that sheet lies in (the room most of the sheet
    squares over the water room lie in, walked down with it), standing in
    a dry room the dry answer; in water no sheet lies over and without
    sheets the classic `LaraNodeAmbient[1]`. OWN, a rule of this project;
    DATA for the reason: ANDREA1's pool room 26 has the ambient 16, 16, 32
    under room 25's 27, 32, 41 (`ROOM_INFO`'s ambient read with the
    randomizer's `Tr5Level`) - the lilac the owner saw on Lara under the
    surface (ModelProbe `-shade r,g,b`).
121. **A water room with no surface, and the deep look** - a water room no
    sheet lies over sees every point in the water (no surface depth; with
    the largest body's surface, which may lie under the eye, the scene
    above the eye's height kept its colour along a hard line); a level
    whose fallback rooms outnumber its rooms under a sheet more than four
    times takes, in all its water rooms, the sea class's absorption and
    the section's palette at a light of 1 (`under_deep=`). OWN, free
    choices; count on our data: JOBY4 2 and 132, the next RICH2 at 1.33
    and ANDY3 at 1.22 (RoomProbe `-hdwater 1 -nodraw`).
122. **The rats' mark** - a swimming rat, which the classic path draws
    between the frame's two brackets and outside the creatures' mark, is
    marked once more before the sheets with its own mesh and matrix,
    pulled toward the eye by 1/512 of its distance (the stand rule's
    fraction) so that it passes against its own depth. A rule of this
    project; DATA for the reason: ANDY1's pool rats are drawn by `DrawRats`
    through `phd_PutPolygons` into the classic buckets, while the mark
    brackets the HD objects alone (the owner's mark line counts 8 objects,
    no rat among them; RoomProbe `-creature -nomark` shows the line on an
    unmarked stand-in, `-latemark` the same picture as the bracket's mark).
123. **Small rings after the others** - a sheet draw takes Lara's, the
    falls' and the rows' rings first and a small ring (a bubble, a shell, a
    rat) after them, each group youngest first. A rule of this project;
    count on our data: a rat ripples one tick in sixteen
    (`game/rat.cpp`) and swims 480 units a second, so eight rats start about
    15 rings a second against the 8 a draw can show.
124. **No dry copy beside a sheet** - in a room whose water a sheet draws,
    a restored classic water face over a square with no water room below
    is left out where the art draws flat in its plane there: a second copy
    of the art's floor (the covering rule's case). A rule of this project;
    DATA: JOBY5 room 4, 30 faces in the plane at 256, 25 over water room
    144 and 5 over none, the deck squares 3,2, 3,3, 4,3, 5,2 and 5,3 - the
    5 the engine restored (the classic level read with the randomizer's
    `Tr5Level`; RoomProbe `-classicroom 4 -classicplane 256` shows the
    fight, `-classicwet` its end).
125. **The mist on the art's footprint** - a WATERFALLMIST emitter whose
    mist line's middle lies within a square of a wet stretch of a curtain
    the art's falling water placed is moved onto the nearest point of it.
    OWN, free choice; DATA: ANDREA2 room 231's two emitters lay their mist
    at x 56,064 (`game/effects.cpp` `WaterFall` on the classic items) and
    the art's strands land at x 55,518 to 55,591 (the WATERLOG `seg=`
    lines), moved by -503, 19 and -473, 52 (RoomProbe `-hdwater 1`,
    `mist=`).
126. **A river carried as one** - the river class's rows travel at the
    current with little travel of their own (calm 0.2 instead of 0.8), stand
    at a channel's steepness and keep half its highlight. OWN, free choice,
    set by eye on RoomProbe strips; DATA: on ANDY3's river from its bank the
    mean change of its pixels from 3 to 3.1 seconds rose from 3.2 to 5.6,
    and the brightest stayed under 128 of 255 (RoomProbe, the river's pixels
    found with `-waterdepthview 1`).
127. **The river's sheen and its moving caustic** - long thin lines along
    the flow drawn lighter (a share of the highlight's colour) and darker
    (through alpha) than the water instead of white dashes, and the caustic
    laid on the floor seen through a river at the floor point under the
    pixel, carried with the current, while the floor's own standing caustic
    is switched off there. OWN, general graphics knowledge, the look of a
    fast stream seen from its bank, tuned by eye on reference pictures
    supplied for this project; DATA: the brightest of the river's pixels
    outside its end's foam fell from 108 to 77 of 255 (RoomProbe).
128. **The streams by table** - a compiled table of river rows (level,
    water room, way, speed) applied after the override rows and before the
    file, and a row's room naming the water room under a square where the
    room holds no square of its own. OWN; the ways are the arrows of
    reference pictures supplied for this project, placed in the level as
    entry 132 says; DATA: ANDY3's squares over water rooms 66, 70 and 67
    are bodies of their own, 4, 3 and 1 squares, 256 units deep (the
    randomizer's `Tr5Level`; RoomProbe `-hdwater 1 -nodraw`); 400 units a
    second a free choice.
129. **The deep look's own light** - in a level that takes the deep look,
    the scattered light under the water is the section's deep colour, dimmed
    by the eye's depth under the highest top of the level's water rooms
    (exp(-depth / 30,000), at least 0.25), with a reach of 0.4 and an upward
    gain of 0.8. OWN, free choice, set by eye on RoomProbe pictures of
    JOBY4; DATA: its water rooms reach from y -1,280 to 19,200 (the
    randomizer's `Tr5Level`, `ROOM_INFO`); the far wall's mean colour at the
    start cavern went from 63, 99, 105 to 15, 33, 42 (RoomProbe
    `-underwater`).
130. **The night's light on the water** - a section of the palette marked
    night (Ireland: ANDY1 to ANDY3) lights its water by the moon: a body's
    colours take the luma of its rooms' light times a cool tint of luma 1
    (0.90, 1.01, 1.22), its highlight and foam the same tint, under the sky
    the highlight at 0.4 and the foam at 0.6 of the day's; the floor and
    wall caustic RoomPS lays is off at night, and a river's drifting caustic
    is the floor's luma in the moon's tint at half strength. OWN, a rule of
    this project; which levels are night is DATA, a project decision on how
    those levels look in game; the tint and the shares free choices, set by
    eye on RoomProbe pictures of ANDY1 to ANDY3; DATA: on ANDY3's river and
    stream the gold pixels (hue 15 to 55 degrees, saturation over 0.35)
    fell from 18.8 to 8.3 % and from 13.0 to 8.8 % of the water's pixels,
    what is left the beds' own colour (RoomProbe, the sheet's pixels of
    `-waterdepthview 1`; the caustic alone taken off removes the lattice and
    leaves the hue).
131. **The relief's light on a river** - RiverPS lights the eight rows'
    slope against the class light's way along the surface, lighter toward
    it and darker away, with no Fresnel and no reflection in it, so a
    river's crests read as streaks along the flow at every angle; the
    sheen's lighter lines take the highlight's colour for the same reason.
    OWN, general graphics knowledge (a surface's slope lit by a light, as
    any shaded relief), the gain of 7 set by eye on RoomProbe strips; DATA:
    the waves' share of the picture (the mean change a test row `waves 0`
    makes) on ANDY3's stream of room 66 seen from above rose from 0.77 to
    5.95 of 255, at eye level from 1.48 to 5.68, the brightest river pixel
    outside the end's foam 113 of 255 (RoomProbe, 960 by 540).
132. **The streams' ways placed by the editor camera** - a reference
    picture's arrow is placed in the level through the camera the editor
    shows with it: RoomProbe draws the same view from the editor's
    position, yaw and pitch (the editor's yaw is RoomProbe's, its pitch
    counts the other way); without them, the view is the RoomProbe camera
    whose open sky and trigger squares match the picture. OWN, with the
    reference pictures supplied for this project as the measure; DATA:
    ANDY3's stream of room 66 runs -z and that of room 70 -x (the table had
    them the other way), room 67's +x; the floors under them are flat and
    give no way, their water rooms join river, streams and room 96 in one
    chain down to the east (the randomizer's `Tr5Level`).
133. **The lift: a body's water up to its bank** - a body's sheet stands a
    number of units over the level's water height (the table
    `kWaterLift`, the key `lift` of `hd_water.txt`); everything the sheet's
    pixels decide from the depth under it follows its grid, and what the
    game places at the water's own height keeps that height. OWN, a rule
    of this project; the pond of ANDY3 at 96 a free choice between the
    bank's lowest point and its median, set by eye on RoomProbe pictures,
    with a reference picture supplied for this project as the look (the
    water meeting the grass, no strip of the bank between); DATA: the
    Remaster's ground over the pond's 224 shore points against the classic
    floor (a tool of this round on the classic `.TRC` and the `.TRG`): the
    bank a slope that leaves the water a third of a square inside the water
    squares, 15 to 443 units over the water at the squares' edge, median
    145, 129 on the commonest bank; no other body of the fifteen levels has
    such a slope (walls a click or more high, or land flat at the water);
    the bank's strip in a view over the pond 25,674 pixels at the level's
    height, 8,742 at 96 (RoomProbe at 960 by 540, `-waterdepthview 4`).
    Kept in the code and the key, applied to no body since 2026-10-10: the
    pond stands at the level's own height again, its row in the table
    `kWaterBank` (entry 137).
134. **The edge held to the land** - a grid point of a lifted sheet that
    touches land lower than the lift takes the land's height there, the
    classic floor's corners interpolated at the point, so the sheet's edge
    never stands in the air over a bank that meets the water low. OWN, a
    rule of this project; DATA: where the classic land at the pond's edge
    lies under a click, the Remaster's ground there stands 5 under it to
    17 over it at 8 in 10 of 152 points, 1 over it at the median; 79 of the
    pond's 208 edge points lie under a lift of 100. Superseded by entry
    135: the edge's fall to the land, 96 units over a quarter of a square
    at 38 grid points of the pond, read in the game as water standing over
    its bank with a trench between the two; no grid point is held now,
    and since 2026-10-10 no body is lifted (entry 137).
135. **The level surface and its skirt** - a lifted body's sheet stands
    level at every grid point, and where the land beside it lies under
    that level the same surface runs on over the land, square by square up
    to three squares out, over land squares whose lowest classic corner
    stands less than the lift and 16 units over the water's top; drawn
    with the depth test against the room art, it is hidden wherever the
    Remaster's ground rises through it, and the waterline runs along that
    line. OWN, general physics (the free surface of still water is level;
    the bank climbs out of it), with a sketch supplied for this project as
    the measure; the reach of 3 and the 16 units chosen on DATA: the
    classic land squares round ANDY3's pond have their corners at the
    water's top or a click and more over it, and the land under 96 reaches
    three squares out (17 squares); the Remaster's ground crosses 96 from
    840 units inside the water squares to 1,472 outside, at the edge at the
    median, outside at 76 of 221 points; along the drawn surface's outer
    edge it lies under the surface at 5 of 528 points, all at the two sides
    where a wall stands; it stands 5 units under the classic land at nine in
    ten of the 152 low edge points (a tool of this round on the classic
    `.TRC` and the `.TRG`); the classic land beside the water squares of the
    fifteen levels stands at the water's top, a click and more over it, or
    under it, never between (the same tool). Kept in the code, applied to no
    body since 2026-10-10 (no body is lifted, so none has a skirt: the sweep
    `skirt=0` on all 56; entry 137).
136. **The shallows** - the rows of a lifted body keep the share of their
    slope, of the bank foam and of the whitecaps that the depth under the
    pixel allows, the depth over 2.56 times the rows' whole height now; a
    body without a lift keeps its rows at every depth. OWN, general physics:
    a shallow-water wave breaks where its height passes 0.78 of the depth
    (the textbook breaking index of a solitary wave), so its amplitude may
    reach 0.39 of the depth; that the classes' other bodies keep the look
    accepted in the game is a project decision. DATA: the pond's open water
    rises and falls -64.5 to 70.6 units at a vertex (rms 25.8) with the
    rows' slope 0.161 at the median at every point up to its edge before
    (the class table's arithmetic over 120 seconds); the white band at the
    pond's south edge seen from above 857, 494 and 766 pixels at 1, 3 and 5
    seconds before, 0 after (RoomProbe at 960 by 540). Kept in the code,
    applied to no body since 2026-10-10: no body is lifted (entry 137).
137. **The pond at the level's height, the clip a body's own** - ANDY3's
    pond stands at the level's water height (lift 0), and how far under a
    sheet's surface at rest the frame may lie and still be left to itself
    is a property of the body: 8 units for every body as before
    (`kWaterClip`), 0 for the pond (the table `kWaterBank`, the key `clip`
    of `hd_water.txt`), so the pond's sheet ends where the depth test ends
    it, on the line where the Remaster's ground comes out of the water.
    OWN, a rule of this project; the pond at the level's height as the
    reference pictures supplied for this project show its water (the WHAT:
    the old heights, rounder corners); DATA: the dark band between the
    pond's land and its water at the level's height is the sheet's own
    clip - over the pebble beach east of the lower lobe the depth under the
    surface runs 0.25, 2, 4, 6 and 7.8 units across the band and 9.8 where
    the water begins (RoomProbe `-waterdepthview 4` against `2`, which
    clips), 18,956 of 960 by 540 pixels in that view; every sheet square of
    the pond lies at the classic water face's height (24 of 24 at 11008);
    the Remaster bakes the ground under its water dark (the baked light of
    the art's top triangle every 32 units over the pond, luma 67 at the
    median under the water, 130 over it inside the squares, 96 on the land
    beside; a tool of this round on the `.TRG`); no flat triangle of the
    pond's squares' own groups lies within one unit over to nine under the
    surface (the same tool, all fifteen levels: such flats lie in ANDY2's
    52, 158 and 159, ANDY3's stream 66 and twelve bodies of JOBY5); the
    same clip of 0 for every body would change 12 of the 15 test cameras
    (the waterline would start at the walls' own contact).
138. **A body's own share of the swell** - a body's sheet takes a share
    of its class's swell at its vertices (1 for every body, the table
    `kWaterBank` and the key `swell` of `hd_water.txt`), the pond 1 as
    well since the owner's tenth play test, his choice on the look (the
    waves take a little from the edge now and then; 0 in the ninth round
    laid its surface at rest, the waves in its pixels). OWN, a rule of
    this project; DATA: the class's swell tapers to nothing only at the
    classic shore, while the pond's ground meets the water at a median 256
    units inside it and round the island up to 1,539 (the art's 0-line
    sampled every 32 units, 1,143 points); with the clip at 0 and the
    swell whole, twelve RoomProbe views of the pond at 1, 3 and 5 seconds
    still showed up to 17,165 pixels of ground under the water with no
    water over them (the depth views: the sheet's swell and clip against
    the ground at rest), with the sheet at rest none.
139. **Short-crested rows in a fan** - every row of a sheet's eight runs
    along its own crest under an envelope (`CrestEnv`, `WaveSum8`): a
    piece two of the row's wavelengths long, at most 1,536 units
    (`kCrestReach`), 1.8 times the row's height in its middle and 0.2
    between two (depth 0.8), its phase the golden angle times the row,
    drifting along the crest by 0.05 turns a second; the slope takes the
    envelope's rise. The rows ranked by wavelength lie at 0, 0.25,
    -0.375, 0.5, -0.625, 0.75, -0.875 and 1.0 times the class's width
    round the body's own direction (`kWaterFan`; basin 1.5, channel, sea
    and river 1.2), no two mirrored and no two close, modulo pi as well.
    The key `crest` of `hd_water.txt` (0 the straight rows). OWN, a
    design of this project from general graphics knowledge: a sum of a
    few plane waves crossing at fixed angles shows a lattice, and a finite
    crest length and a spread of directions are the standard way out; the
    lengths, depth, widths and drift are free choices, set by eye on the
    round's RoomProbe pictures against the lattice measure. DATA: the
    lattice is the light on the surface - with every body's refraction
    off it stays, with the rows' heights 0 it is gone; the surface light
    of six views holds 15.7 to 30.5 % of its power in its six strongest
    frequencies against 6.1 to 12.7 % with random directions; after this
    design the owner's four views 22.6, 10.1, 19.2 and 15.8 % fall to
    18.5, 5.4, 11.3 and 11.0 % (RoomProbe pictures at 1, 3 and 5 seconds,
    a script of this round). The pieces stand at no fixed step: the place
    along the crest is bent by two slow curves of Wave2's kind, 0.15 of a
    turn deep at 0.37 of the piece's frequency along the crest and 0.3
    deep at 0.61 across it (`HD_CREST_*`), so a piece runs from 0.75 to
    1.5 of its mean length and the next crests' pieces stand out of line;
    the envelope's rise is exact with both. OWN, the same design extended
    (a domain warp of the envelope's coordinate); the scales and depths
    are free choices, set by eye on RoomProbe pictures of the owner's near
    views. DATA: at 0.3 along, pieces stretched to three times their mean
    drew long streaks on ANDY3's pond (the bridge's view 23.0 % against
    18.5 %); with the four shortest rows straight the lattice came back
    (the canal close, 45 degrees down: 15.1 % against 11.1 %). The pieces
    are placed by a hash since entry 142: the envelope, its two slow
    curves and the drift along the crest are gone; the fan stays.
140. **The foam in a draw of its own** - the sheet's foam (the bank foam,
    its patches, the waterline, the whitecaps and the foam's body) is
    drawn in a second premultiplied draw of the same triangles over the
    sheet drawn without it (`HD_WATER_FOAM`, `draw_water_sheet`), for the
    bodies that have foam. OWN, a rule of this project: the blend ONE /
    INVSRCALPHA makes the water times 1 - foam plus the foam times foam
    the same as the two draws, up to the render target's rounding. DATA:
    the depth variant of the sheet's pixel shader stood at 511 of 512
    slots; split by class it fell to 334 only for the classes without
    foam, and stood at 487 and 511 for the channel and the sea (fxc /O3
    on a scratch copy); with the foam drawn apart it stands at 334, the
    foam's draw at 272 (293 with the crests).
141. **The crests' second bend and the glints** - `WaveBend` pushes the
    place the rows are read at a second time, along a pattern 1.75 of the
    longest wavelength wide, by 0.2 of it; the highlight's strength is
    multiplied by a field of cells about 1.4 of the shortest row's
    wavelength, 0.25 to 1.75 with a mean of 1; both times the body's
    `crest`. OWN, a design of this project from general graphics
    knowledge (a domain warp at the pattern's own scale; a highlight
    broken by a fine field over its strength, not the normal); the scales
    and amounts are free choices, set by eye on the round's RoomProbe
    pictures. DATA: on the canal at eye level the lattice measure stood
    at 13.6 % with the pieces and the fan alone and at 11.3 % with the
    glints (mean of 1, 3 and 5 seconds; 19.2 % before). The glints are
    taken out again: a product of two periodic curves along the world
    axes, the field drew a regular chequerboard of bright dots wherever
    the eye came close (reference pictures supplied for this project,
    not in the repository);
    the dots' period and direction in those pictures match the cells'
    (within 10 % and 3 degrees), and the measure's gain was partly its own
    subtraction, the cells standing in both pictures it compares. The
    second bend stays. Both of the sheet's pushes - the slow one and the
    second - give way to one push along a value noise of the cells' hash
    of entry 142 (`WaveBend`): cells 1.5 of the longest wavelength wide,
    along that row's direction, by up to 0.3 of its wavelength times the
    body's `crest`, drifting 0.031 and -0.023 cells a second; RiverPS
    takes none, RoomVS's `WaveSum` keeps its own slow push. OWN, the same
    design with a non-periodic field (value noise: a hash at a grid's
    corners blended smoothly, a common graphics idiom); the scale, push
    and drift are free choices, set by eye on the round's RoomProbe
    pictures. DATA: with every push out (a probe build of `aed63ba`) the
    owner's swimming view shows the pieces standing in rows of dashes -
    the periodic pushes were the swirls and squeezes, and their periods
    brought them back.
142. **The crests placed by a hash** - each of a sheet's eight rows is
    cut into cells three crests across (from trough to trough, counted
    by the crests' own number, which the processor keeps fixed as a
    crest travels by handing each row the whole cells its phase has run,
    `kCrestAcross`, `kCrestOffset`) and three of its wavelengths along
    (`crestLen`, at most `kCrestReach`), every other cell across moved
    along by half a cell; a hash of a cell's two numbers (`CellHash`:
    the fractional parts of two sums of products of the indices with
    multipliers n / 4096, crossed) gives its piece a height of 0.4 to 1
    times `kCrestGain` (6), a length of 1.5 to 3 wavelengths at a place
    in the cell, a lean of up to about 15 degrees and a shift of up to
    0.1 of a turn; the piece is the row's curve under two smoothstep
    windows, along the piece and across the cell, with the shear under
    the same windows, the slope exact (`CrestRows`). The phases and
    heights of the rows go to the shaders four to a register (c156 -
    c159, vertex c175 - c178). The river, the underside, the views'
    variant and the foam's draw take the pieces without the lean and the
    shift; `WaterVS`'s swell takes them at a gain of 1
    (`kCrestSwellGain`), never deeper than the straight rows. OWN,
    a design of this project from general graphics knowledge:
    randomness from a hash of whole cell numbers (a common graphics idiom)
    in place of periodic curves, short wave trains (a wave packet's
    finite extent) in place of endless rows; the cell's size, the
    lengths, heights, lean, shift and gain are free choices, set by eye
    on the round's RoomProbe pictures, the gain a third over the 4.49
    that keeps a row's mean square (0.52 x 0.258 x 0.371, the heights'
    and the two windows' mean squares). DATA: the hash counted on 128 x
    128 cells at three places up to (4000, 6000): even over 64 bins (chi
    square 41 to 82 on 63 degrees of freedom), neighbours correlated by
    under 0.03, a flat spectrum (largest bin 8 to 14 times the mean); a
    sine hash of the same cells kept an eighth of its values distinct. The
    lattice the owner saw (reference pictures supplied for this project,
    not in the repository) is the beat of the long rows - the difference
    of two wave vectors, 337, 411 and 851 units for the channel's 311,
    230 and 420 -: its cells in his picture, counted by autocorrelation,
    188 and 160 pixels, about 330 and 280 units at his distance. Cells
    one crest across drew round bumps, a pitted sheet; a sixth of the
    cells empty left flat stretches with single dimples (RoomProbe
    pictures of the owner's swimming view, a script of this round). The
    swell at the gain of 6 sank ANDY3's pond under the ground beside the
    island: 37,416 hole pixels in one of the twelve views of the holes
    count against 9,237 at most at 1.
143. **The splash and the tank's spray as cards** - a splash the classic
    `SetupSplash` makes over a water sheet, and a spark of a fish tank's
    spray, are drawn as this water's own: two rings on the sheet at the
    classic middle and outer rings' fronts (strengths 1.0 and 0.6 of the
    classic life), a crown of 12 + 16 cards standing at the classic
    columns' heights (0.7 to 1.0, at 0.85 to 1.15 of the time, 24 to 160
    units wide, leaning up to 0.3 radians) and 24 drops under the column's
    own gravity (7200 units/s^2, 0.5 to 1.1 of its first speed); a tank
    spark's card with the classic spark's own motion and a share of 0.35
    of its strength over 64; quads facing the eye, a window of a page cut
    round, soft over 64 units against the frame's depth, premultiplied
    with the body's foam colour times 1.5 (`CardVS`, `CardPS`,
    `hd_splash_math.h`). OWN, a design of this project from general
    graphics knowledge (camera-facing particle cards, soft against the
    depth buffer, the motion stepped from the classic struct's own
    numbers); the counts, shares, widths, lean, the soft depth and the
    light's 1.5 are free choices, set by eye on the round's RoomProbe
    pictures. DATA: the remaster's spray pages 3242 and 3243 and its drops
    page 3160 (5\TEX of the player's installation, read through the page
    cache like every page); the classic numbers counted at the engine's
    own code (`UpdateSplashes`, `TriggerFishtankSpray`, `UpdateSparks`).
144. **The tank's film** - the floor a breaking fish tank floods gets a
    basin body of the sheets of its own over the floor lasers' box,
    at rest 8 units over the floor, drawn with the sheets' shaders and
    cross-faded with the blend factor by a strength out of the tank's and
    the lasers' state: (4096 - `item_flags[1]`) / 1024 while the tank
    runs, the lasers' count over 64 after it. Its depth byte, in the
    sheets' 32-unit steps, is 0 (8 over 32, rounded down): only the
    variant without the depth texture reads it. OWN: the film, its
    8 units and the rise's 1024 are free choices, set by eye on the
    round's RoomProbe pictures; the timings are the engine's own (the
    tank's 105 ticks of a 64th, the lasers' 64-tick countdown, counted at
    `ControlFishtank` and `ControlFloorLasers`). DATA: RICH2's two tanks
    and the floor lasers of room 23 (4 x 4 squares), read from the
    classic level by RoomProbe.
145. **The film's height, rim and clip** - the film's surface stands
    `kFilmRise`, 16 units, times its strength over its rest, through the
    sheets' crest lift in `WaterVS` (tapering in over the basin's 512
    units from the edge); its edge takes a channel's bank foam (strength
    0.7, band 300 units, `kWaterClass`) times its strength; its clip is 0
    instead of the bodies' 8. OWN: the height's 16 units are a free
    choice, set by eye on the round's RoomProbe pictures; the rim reuses
    the channel row's own numbers (entry 52's foam rule); the clip 0 is a
    count on our data: with the clip 8 the film in RICH2 room 23 lost a
    band in front of the tank's plinth (RoomProbe `-tank 33 1.5`, 960 x
    540, the film at rest: 73,132 pixels against 84,559 with 0).

160. **The water's life's models** - `BOTTLE`, `WET_CLOTH`,
    `CLOTH_RICH2_RICH1` and `ANIMATING3_ANDY1` out of `5\ITEM`, `FISH2`
    out of `4\ITEM`, found by name through the engine's model resolver
    (`hd_life_rules.h`, `kHDLifeKinds`). DATA: the player's own files,
    read at level load; their boxes give the floating heights 80, 16, 16,
    326 and 70 units and the reaches across the water (`RoomProbe -life`
    prints both). Nothing of their contents is in this repository.
161. **Which water takes life, how much, and what kinds** - ANDREA2's
    channel bodies of 16 squares or more, ANDY3's rivers of three squares
    or more and its bodies of eight, ANDY1's bodies of eight; the kinds'
    weights; one float for three sheet squares with water under them
    (a floor at the surface counts none), twelve a body at most,
    thirty-two drawn a frame at most, nearest the eye first (`kHDLifeLevels`,
    `HDLifeTakes`, `HDLifeCount`). OWN, a design of this project; the
    numbers are free choices, set by eye on the round's RoomProbe
    pictures. DATA: the bodies, their squares and classes are the water
    sheets' own (`RoomProbe -hdwater 1 -life`, the `LIFELOG body=` lines).
162. **The drafts and the floating poses** - a bottle and a barrel laid on
    their side, the cloths flat; half a bottle under the surface, 0.7 of
    a cloth, 0.45 of a barrel; two barrels a body at most. OWN, free
    choices, set by eye on the round's RoomProbe pictures.
163. **The drift** - a float moves with its body's current (the sheets'
    `flow` and `speed`) plus a wander of 60 units a second whose way comes
    from a hash of the float and the clock's 4-second step, blended over
    the step; it turns toward its way at 40 degrees a second; it stops
    where a step would bring it nearer to a bank than its model's reach
    (half the box's diagonal seen from above, 128 at least) and slides
    along it; held by a bank it tries again after 6 to 9 seconds. OWN, a
    design of this project; the numbers are free choices, set by eye on
    the round's RoomProbe pictures. DATA: `ANIMATING3_ANDY1` on its side
    reaches 242 units and poked into a bank's planks at a margin of 128. In
    a body with a spring (entry 73) a current straight away from it is
    added: 40 units a second at the spring, falling off linearly to nothing
    1536 units out, none within 16 (`HDLifeSpringPush`). OWN, free choices,
    set by eye on RoomProbe `-life -lifeall` pictures of ANDREA2 194's basin
    over 20 seconds; over twelve seeds its floats' mean distance from the
    spring goes from 1055 to 1127 units in 20 seconds, to 911 without it.
164. **The river's conveyor** - on a body of class river a float that
    reaches a quarter square past the middle of the last row downstream,
    or that a bank holds, comes back with a kind rolled again on the
    upstream row (the squares whose middle lies within half a square of
    the first row's middle), on one of them deep enough for that kind -
    any square of the body that is, when none upstream is. OWN, a design
    of this project; free choices.
165. **The bob, the fit and the depth** - a float rises and sinks on a sine of the
    clock (2.5 to 4 seconds, a phase of its own) by a quarter of its
    floating height, never more than its body's sheet rises and never more
    than its draft leaves over that rise, and tilts 3 and 2 degrees on the
    same curve; a kind floats on a body only where its draft reaches
    deeper than the sheet falls (`HDLifeBobAmp`, `HDLifeFits`). OWN, a
    design of this project. DATA: the rise is counted from the water's own
    class table - the swell share times the eight rows' heights, a quarter
    more where the class has whitecaps (`HDRenderWaterBodySquares`): 0
    units in a basin, 5.9 in a channel, 3.0 in a river, 121.3 in a sea;
    the waterlines of `RoomProbe -life` at 1, 3 and 5 seconds keep every
    float's bottom, held level, under that rise - the tilt is left out
    and lifts a corner by up to its reach x 3 degrees (`HDLifeTiltDrop`,
    an upper bound by Cauchy-Schwarz: 12.7 units for the barrel). The
    depth: a float keeps to squares at least as deep as its lowest point
    (its draft, its bob and that tilt drop; `HDLifeNeed`) where it is
    placed, where it drifts and where it comes back; a square whose floor
    lies at the surface is water to none. OWN, a design of this project.
    DATA: ANDY3's shallows, room 34, has 23 of its 24 sheet squares with
    the floor at the surface (the sheets' `depth`, printed per square by
    a copy of `RoomProbe -life`), where a barrel stood in the floor.
166. **The school** - 4 to 8 `FISH2` on a ring of 150 to 300 units, half
    as wide as long, half a square under the surface, rising and sinking
    64 units, a 6 degree yaw wobble; its middle cruises at 150 units a
    second and keeps 387 units from water less than 768 deep; Lara within
    1024 units sends it to the deep square farthest from her at twice the
    speed for 4 seconds. OWN, a design of this project; the counts,
    speeds and times are free choices. DATA: 768 and 387 from `FISH2`'s
    box (173 long, 70 high): its lowest point lies 611 under the surface.
167. **The drips** - squares of a body whose room is not open air have a
    ceiling; four or more of them give one drip for eight, two to six,
    each at a period of 3 to 9 seconds; a drip rings the sheet within six
    squares of Lara and at most once in two seconds a body. Its ring is
    written into the game's ripples with the fields `SetupRipple` writes
    (flags 0x10 and 1, size, life 48 and up to 15 more, init 1) but its
    life's spread from the pool's hash. OWN, a design of this project;
    the counts, periods and reach are free choices. CLASSIC: the fields
    (`game\effect2.cpp`, `SetupRipple`), the ring size 32 and up to 7 more
    of the engine's own drip (`game\joby.cpp`), the eight rings a sheet
    draw shows (`hd_render.cpp`, `kRingsPerDraw`).
168. **The life's light** - a model takes its body's light, the one its
    sheet's colours are lit by, times 255 as its classic ambient, a fish
    half of it, with no light list and the game's sun factor 0.75. OWN, a
    free choice set by eye on the round's RoomProbe pictures: at the full
    light `FISH2`'s silver reads as white glints through the sheet.
169. **The pool's random and its hash** - the places are rolled at level
    load, and again when the HD level is rebuilt after the device was
    recreated, with `GetRandomDraw`, never `GetRandomControl`; a float's own
    numbers come from three steps of the same generator over its number,
    a second input mixed in times `0x9E3779B1`. CLASSIC: the generator's
    constants (`0x41C64E6D`, 12345, bits 10 up;
    `specific\function_stubs.cpp`). OWN, general knowledge: `0x9E3779B1`
    is 2^32 over the golden ratio, the usual multiplier that spreads
    neighbouring inputs. OWN, free choices: the second inputs' constants
    (101, 211, 307, 401, 503, 601, 7919 and a school's 7).
170. **The projected shadow's arrival** - where the shadow starts to run its
    strength is multiplied by 3f^2 - 2f^3 of f = t / 0.25 game seconds, and
    every blob that gives way to it is drawn meanwhile with its alpha byte
    times 1 minus that (`hd_shadow.h`, `HDShadowPresence`,
    `HDShadowArrive`, `HDShadowBlobAlpha`). OWN, a design of this project:
    a cross-fade on the game clock the soft motion uses; the curve is the
    one the room shader softens a fog bulb's rim with; 0.25 seconds is a
    free choice (7.5 frames at 30 a second, half the time constant of the
    lights' blending), not yet seen in the game. DATA: in ModelProbe
    (`-shadowpresence`, ANDREA1 room 0) the darkening at 1/60 s steps is
    the presence within the 8-bit rounding, and the picture at 0.25
    seconds is the picture without the switch, bit for bit.
171. **The depth terms a block below the feet** - `HD_SHADOW_FULL` 576 and
    `HD_SHADOW_END` 1088 below her lowest node, `HD_SHADOW_OTHERS_FULL`
    1024 and `HD_SHADOW_OTHERS_END` 1280 behind a creature's place along
    the light, the place's depth kept beside the caster's in the second
    map's green channel (`hd_shadow.hlsl`, `CastPS`; `hd_object.cpp`,
    `castPlace`). OWN, a design of this project: a creature's term
    measured from its feet as hers is from her lowest node. DATA: her
    lowest node stands 58.6 units above her soles (ModelProbe
    `-shadowroom`), rounded up to 64 on half a block and a block; counted
    on the thirteen levels' classic faces with ModelProbe `-shadowsurvey`
    (23,063 floor sectors as her place, rays along the fixed light, every
    face behind the first that the terms still weigh, measured as the
    engine measures): the weight laid through a wall fell by 88 per cent
    and through a floor by 52 against 1024 / 2048 (a creature's by 80 and
    40 against 1536 / 2560 per tap), the first faces below her feet lost
    27 per cent; the creatures' heights from the rest pose's box
    (`-surveyslots`, `Tr5Level::frameBounds`).
172. **The bulbs' haze given back** - every receive variant writes the
    factor in alpha and the fog bulbs' haze times 1 minus it in colour, and
    the receive blends ONE / SRCALPHA, so a pixel is the surface times the
    factor plus the haze (`hd_shadow_receive.hlsl`, `ReceiveOut`). OWN, a
    design of this project out of the room and model shaders' own
    composition (texel times light plus the bulbs' colour); the bulb term
    is the room shader's `BulbIntensity`, copied. DATA: in a bulb in front
    of her shadow (ModelProbe `-bulb`, ANDREA1 room 0) the shadowed floor
    kept 0.575 of its brightness before and keeps 0.934 now; without a
    bulb every picture of the earlier rounds' sets is unchanged.
173. **The survey of the shadow behind the first face** - ModelProbe
    `-shadowsurvey`: places on every floor sector, three columns of the
    body, a ray every 32 units of height along the fixed light,
    Moeller-Trumbore against the level's own room faces, the first face as
    the receiver and every weighted face behind it counted, through a
    floor when the first face's normal is within 60 degrees of vertical,
    the terms measured as the engine measures them. OWN, a procedure of
    this project from general geometry; the 0.1 weight called visible
    (0.034 of a pixel's colour at the fixed light's darkness), the
    creature's 760 units, the 32-unit step and the 64 and 256 that leave
    the floor at the feet out of the first faces are free choices.

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
