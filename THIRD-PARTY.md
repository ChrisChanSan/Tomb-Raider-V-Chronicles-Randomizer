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
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl` | the same file's GLSL `k_hd_vert` / `k_hd_frag` (TROne-main, copy of 2026-10-06, commit 75c4c843) | Taken: the alpha test living in the alpha pass **only** (an opaque tile's alpha is a gloss mask, and testing on it blanks whole floors), and the additive pass weighting its colour by the texture's alpha - both TROne's own design. Not taken: the lighting rule. That the room light is the **sum** of the vertex's two colour triples, each byte over 255, with the pixel `texel * light`, is this project's own count: on the 107,118 classic room vertices a `.TRG` vertex of the same room stands on, the sum orders them as the classic prelight does better than the product or either triple (`TrgProbe -shadeorder`, the origin ledger below). In its copy 75c4c843 TROne rests the sum on its own count over its `.TRG` files and marks a room-shadow scale on the first triple as its own design; this shader has no such scale. Translated to HLSL for shader model 3; not a line of the GLSL is copied. |
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
TRForge's as TROne's own copy held them; since then three prose comment
passages of `trf_remaster/Trg.h` have been left out of this copy - over the
normal bytes, the vertex layout and the point lights; its code is unchanged.
`vendor/trforge/VERSION.txt` names the source, the date, the SHA-256 of
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

Every rule, constant, table, procedure and format fact of the HD path, one entry
each, with its origin: what it is; where it stands (by function, section or file,
and for a rule TROne or TRForge has too, where it stands there and the copy it was
read in); its class; and this project's own evidence for it. The classes: DATA (a
fact about the player's files, countable on them), PICTURE (a fact measured on this
engine's own pictures), CLASSIC (the classic engine's own code), OWN (this project's
own design: a free choice, a count on its data, or tuned by eye on its RoomProbe
pictures, in its play tests or on reference pictures supplied for this project, not
in the repository), THIRD (TROne's design or code under the GPLv3, or TRForge's under
the MIT licence, credited), PLACE (where a file is). "Free choice" says that nothing
of ours decides the question and this project chose. The counts are reproduced by the
tool switch named; `MODIFICATIONS.md`, section 2.4, entry "Where the HD rules come
from", carries the tables.

1. **The `.TRG` container** - header, light cubes, groups, parts of 44 bytes with
   three index runs that tile the index list, the 20-byte vertex. TRForge,
   `trf_remaster/Trg.h` and `Trg.cpp`, vendored (copy of 2026-09-05). DATA.
   Ours: `read_trg` refuses any file whose sizes do not end where the file ends;
   all fifteen levels read (`TrgProbe -all`).
2. **Group i is room i.** TRForge, `Trg.h`. DATA. Ours: 107,118 of the 126,170
   classic room vertices of the fifteen levels have a `.TRG` vertex of the same
   group at exactly their position (`TrgProbe -shadeorder`).
3. **u and v are the fourth bytes of the two colour triples** (+15, +19). TRForge,
   `Trg.h`, which names them (its prose passage over the vertex is left out of
   our copy). DATA. Ours: on 15,999 classic faces with `.TRG` vertices inside,
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
   (TROne-main, copy of 2026-10-05; in 75c4c843 on TROne's own count). DATA. Ours: on the
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
    out.** TROne, `hd_load`'s prefix pass. DATA and THIRD. Ours: the same 471-slot
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
    `hd_draw_room`. THIRD. Ours: the groups, sky groups, ranges and page changes of
    the fifteen levels (`TrgProbe -all -classic`), and RoomProbe's pictures of
    the engine's own build.
16. **A vertex whose layer is 0 takes the level's first page.** TROne,
    `hd_load`. THIRD. Ours: no vertex of the fifteen levels carries layer 0
    (`TrgProbe -layer0`), so it never fires on them.
17. **The alpha test lives in the alpha pass only.** TROne, `k_hd_frag` and
    `k_trm_frag`. THIRD. Ours: the opaque pages' alpha measured as a mask
    (`hd_pagecache.cpp`, 0 % against 51 to 95 % for the cut-out sheets).
18. **The additive pass weighted by the texture's alpha** (rooms). TROne,
    `k_hd_frag`. THIRD; free choice to keep it.
19. **The water-mark road** - classic water vertices matched to `.TRG` vertices
    by a hash of the packed int16 position, the owning group read from the index
    runs. TROne, `hd_load` and `hd_water_mark`. THIRD. Ours: the pairing count of
    entry 2.
20. **The wind's shape** - two sines of the position taken across each other,
    about 24 units of travel, a wavelength of about 1.4 blocks.
    TROne, `k_hd_vert`'s water block (copy of 2026-09-05), where the same
    shape moved TROne's water. THIRD; free choice to keep TROne's numbers for
    the wind. The water's wave, swell, UV wobble and caustic, which this entry
    credited to TROne before (`MODIFICATIONS.md`, "Water in the HD rooms"),
    are this project's own design: entries 40 to 86.
21. **Material 16's ripple held to the part's own UV span, nothing moving in
    the vertex.** TROne, its own material 16 water rule. THIRD. Ours: `sway` is 0
    on 27 of the 28 parts; 23 of 28 live in a sub-rectangle of their page.
22. **Material 16 marks the remaster's still water art; its four numbers are
    facts of the file.** DATA: 28 parts in six levels, `TrgProbe -mat 16` prints
    each part's numbers (`param[0]` 0.1 to 5.0 and `param[2]` 0.035 to 0.5, both
    moving with the area of the water). The ripple reads none of them: its scale,
    travel and rate are this project's own, entry 248 (`hd_render.cpp`,
    `HDWaterArt`).
23. **Statics baked into the art are not drawn again** (the double lantern).
    TROne, `draw_mesh`. THIRD. Ours: `RoomProbe -statictable` over the fifteen
    levels.
24. **The object path** - one `.TRM` replaces the whole model, bone numbers are
    mesh-tree nodes, the bind pose is the tree's offsets, a head rides rigidly on
    the node nearest its centre, an uncovered node is drawn by nobody, an unseen
    node keeps the last matrix. TROne, `build_skinned_model`,
    `bind_pose_of_slot`, `hd_load`'s head loop, `fill_unseen_joints`. THIRD and
    CLASSIC. Ours: `ModelProbe -all` (the skeleton drift), `ModelProbe -nodes`.
25. **The item bracket and the swap rule** - wholly HD or wholly classic; a swap
    mesh on a covered node loses to the model. TROne, `item_flush` and
    `draw_mesh`. THIRD. Ours: `ModelProbe -name MAFIA` (the swap files).
26. **The ring's off-centre projection folded into the draw, its light as
    ambient and one sun.** TROne, `games/tr5/glue/hostglue_2d5.cpp`,
    `phd_PutPolygonsPickup` (copy of 2026-10-06, commit 75c4c843). THIRD (the
    idea; the arithmetic is TR5's). Ours: `ModelProbe -ringcheck`, `-ringlight`.
27. **Lara's composite and the role table.** TROne, `lara_flush`, `lara_load`,
    `RegisterLaraWardrobe`. THIRD. Ours: the section table (`ModelProbe -lara`).
28. **The braid, one frame per link.** TROne, `draw_hair`, and the classic
    `game/hair.cpp`. THIRD and CLASSIC. Ours: `braidcheck` in `hd_laradraw.cpp`.
29. **The HD font's template match** - ink at alpha 96, intersection over union,
    0.94, a texel of margin in the same ratio, the prefilter 0.85 to 1.20.
    TROne, `hdtext_find` and `hdtext_arm`. THIRD. Ours: `FontProbe -atlas` over the
    106 `chardef.inc` glyphs.
30. **The glyph quad's mip LOD bias of -0.7.** TROne, `k_ui_frag`. THIRD. Ours:
    the minification 1.8, 2.7 and 1.35 measured on this engine's output.
31. **The glow's screen composition, near fade and `|N.V|` term.** TROne, its
    rules for its glow material. THIRD. Ours: RoomProbe on ANDREA2 room 131.
32. **Where a page is looked for: its own game, the sibling games, then
    `WHITE.DDS`; a missing page is white, never black.** TROne,
    `host/render/hd_pages.cpp` (`upload_shared`, `upload2`). THIRD and PLACE.
    Ours: RICH3 names three pages only `4\TEX` has; `WHITE.DDS` is in `4\TEX`
    only.
33. **The Steam discovery, by content and not by folder name.** TROne,
    `host/fs/discovery.cpp`. THIRD and PLACE. Ours: `TrgProbe -find`.
34. **The `.TRL` track layout of the faces.** TROne, its porting notes for TR4
    and its mimic measurement. DATA. Ours: `LipsTest` over 45 files of `5\ITEM`.
35. **The modern control's design, its 32 test expectations and six default
    constants.** TROne, `tests/test_controller.cpp`, `host/input/` and
    `docs/CONTROLS.md` (TROne-main, copy of 2026-09-13; since its round K32
    TROne carries this project's core and orbit, see the TROne section above).
    THIRD, reworked here almost throughout: of the 32 constants 16 stand as
    TROne's design has them, nine were re-measured in this project's own play,
    and the turn rate, the camera's return and the orbit rate's ends are this
    project's own choices (OWN). Ours: `ModernTest`.
36. **Material 1's scroll in param.xy.** Read so by TROne as well. DATA. Ours:
    calibrated against the classic UVROT tile in RICH2; 5 to 65 parts a level.
37. **Materials 13 and 15, the flipbook and the books.** Read the same way by
    TROne. DATA. Ours: the four-phase cycles and the thirty-page loops counted
    in `hd_render.cpp`.
38. **`param[0]` of materials 6, 7 and 8 is the cut-out threshold.** Read the
    same way by TROne (TROne-main, copy of 2026-10-05, its rules for the
    cut-out materials; in 75c4c843 on its own count, with material 8 cut at a
    fixed 0.5). DATA.
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
    2026-09-05), on its own data. DATA and THIRD. Ours: of the three cut-out
    materials only 6 and 8 occur in the `.TRM` files of `4\ITEM` and
    `5\ITEM`, both with 0.5 in `param[0]` in every part, and only 6 in the
    opaque run; the page's share of texels at exactly 0 among those below the
    reference (`HDPageCacheSlotZeroPermille`, `hd_model_cut.h`; ModelProbe's
    page lines) puts the cut-out sheets at 325 per mille and up and the pistol
    grips at 4, and material 0's page 9000 of the Roman statue at 715, which
    the material rule keeps whole.

Removed in the same round, so that every rule of the HD path rests on this
project's own count, is declared a free choice or is credited: the spare scale on
the first colour triple (TROne's room-shadow factor, always 1 here); the sibling
names `<n>_1024.DDS` and `<n>_2048.DDS` tried for ids 8000..8499 (35 pages ship
that way, all in the I-III bundle's `1\TEX`, all beside a plain `<n>.DDS`); and
two prose passages of `Trg.h`, over the vertex and over the point lights
(`vendor/trforge/VERSION.txt`). The engine manual's HD chapter points here.

**This project's own water.** Entries 40 to 86 came by way of neither TROne nor
TRForge: the water sheets, their wave and their caustic are this project's own
design out of textbook graphics (sums of sines, Schlick's fresnel, Blinn-Phong,
exponential absorption, foam by shore distance, refraction by Snell's law, a
copy of the frame and a depth texture read back) and its own counts; their
classes are the header's (entry 71, the game's own ripples, is CLASSIC).
`MODIFICATIONS.md`, entry "The water sheets: this project's own water surface",
carries the tables.

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
    restless, too quick, fading too soon). A ring that starts over 128 (a
    floating body's) keeps its size past the cap and is 4 times stronger:
    entry 193. A spring's rings (entry 198) go into the live rings as they
    are, without entry 79's merge, and take this strength.
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
    The compiled row's source is the springs' table's since #200 (entry
    198); a row of the file within 256 units of a spring rings its place.
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
    at sixty frames a second and more. A rule of this project (OWN).
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
    wades (`lara.water_status`); her rings stay. A rule of this project (OWN): a
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
96. **No stretch of a waterfall that stops short** - a rule of this project (OWN):
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
    of entry 76. A rule of this project (OWN): the wet line on a swimming rat
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
    rat) after them, each group youngest first. A rule of this project (OWN);
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
    A tank spark's card stands on its spark's point, its lower edge there,
    at a share of 0.8 of its strength over 64 (`kTankSprayShare`): OWN,
    the dive splash's middle crown ring's share, chosen after a count on
    our data (round its point at 0.35 the spray touched 14.9 % of the
    room 122 tank's view at +24 and 1 % of the room 23 tank's view,
    RoomProbe `-tank` with and without `-tankleave spray`). The burst of
    a breaking tank's glass - 40 spray cards and 24 drops leaving the
    glass within 0.2 s at sqrt(2 g d) times 0.6 to 1.0 under the water's
    depth d, up or down by up to 400 units/s, under g 4300 units/s^2, for
    at most 1 s (`HDTankBurst`) - OWN, Torricelli's law and the project's
    g (9.81 m/s^2 at 2.3 mm a unit); the counts, time, lift, life and
    sizes are free choices, set by eye on the round's RoomProbe pictures.
    The warm-up - each of the water's pixel shaders and the cards' pair
    drawing once into a 16 x 16 target of its own at the level's load
    (`HDRenderWaterWarmUp`) - OWN, a design of this project from general
    graphics knowledge (a shader readied by a draw before it is needed);
    the cause it answers is a count on our data (the first draw with
    `WaterVS` waited 42 to 73 ms, the pixel shaders under 1 ms). The
    cards' vertex shader also projects a shot tank's wet floor (entry
    192).
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
    The tank's water itself, the water a breaking tank pours out: the
    FISHTANK's classic mesh (69 vertices, 104 quads in RICH2, a flat top
    1,276 over the floor behind the glass and an apron reaching 866 units
    in front of it), read from the player's classic level at load, drawn
    with the cards' shaders as a see-through sheet (`HDTankDomeAt`,
    `HDTankDomePoint`, `HDTankDomeShare`): scaled in y by the tank's own
    `item_flags[1]` / 4096 between its ticks, the apron coming out of the
    glass over 0.25 s (866 units at the 3,310 units/s the water leaves the
    glass at), the whole in over 2 ticks and out over the 0.5 s before
    the 106th tick, a share of 0.15 where the eye looks through it and up
    to 0.65 where it looks along it, the remaster's spray page 3243
    stretched 384 units across and 2,048 down, running down at 1,660
    units/s (half the speed of a fall of the dome's height under g). OWN,
    a design of this project; the shares, the lengths and the fades are
    free choices, set by eye on the round's RoomProbe pictures. DATA: the
    mesh (the player's classic level) and page 3243 (5\TEX). After the
    film, and at the tank without one, the floor its water reaches stays
    wet: entries 190 to 192.
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
    After the owner's play test of 2026-10-11: floats keep apart, no nearer
    than 0.75 of their two reaches unless they part (`HDLifeClear`), so a
    current lays them side by side along a wall. OWN, a free choice set on
    RoomProbe series of ANDREA2's canal (two barrels side by side take 326
    of their 484). ANDREA2's canal gets a current, +z at 90 units a second,
    through a compiled flow row that keeps its class (`kWaterFlow`, its
    new `cls` column -1). OWN, a free choice: half the 180 its rows run at,
    one and a half times the wander. DATA: the way - a channel's rows run
    along its longer side, 15 squares along z against 7 along x, toward +z
    (`water_look`, `WaveSum8`), and past the +z end wall the water goes on
    under it (water room 133, SectorCheck on the classic `ANDREA2.TRC`);
    RoomProbe `-life`: 8 of the 12 floats lie along that wall by 180 seconds.
    In that round's fix pass: where the current holds a float square-on
    against a bank, within its margin and an eighth of a square of its
    square's edge, the float turns along the bank toward the nearest
    square of its row with a way on, at the current's speed
    (`HDLifeWayOn`); a row with no way on turns nothing. OWN, a design of
    this project; the eighth a free choice. DATA: the canal's side arm,
    rows z 43008 and 44032 five squares west of the main channel, held
    three of the 12 floats on its bank to 300 seconds; with the way on all
    12 lie along the end wall by 170 seconds (RoomProbe `-life`).
164. **The river's conveyor** - on a body of class river a float that
    reaches a quarter square past the middle of the last row downstream,
    or that a bank holds, comes back with a kind rolled again on the
    upstream row (the squares whose middle lies within half a square of
    the first row's middle), on one of them deep enough for that kind -
    any square of the body that is, when none upstream is. OWN, a design
    of this project; free choices. After the owner's play test of
    2026-10-11 the float neither appears nor vanishes in open water: it
    comes back at the inlet's slot point, 384 units past the first row's
    edge along the flow, its top 64 under the surface, rises over the first
    square, rides, dives over the last square into the outlet's slot point
    (as far past the last row's edge) and comes back only there; over the
    two end squares it rides straight with the current, and its light falls
    to a quarter over the dive (`HDLifeDiveShare`, `HDLifeOnRun`,
    `HDLifeComeBack`, `HDLifeDiveLightShare`). OWN, a design of this
    project; the reach, the depth, the runs and the light are free
    choices. DATA: past every end row of ANDY3's four rivers the classic
    level has a dry floor 256 to 768 units over the surface or a wall,
    with the water going on under it at six of the eight ends, 256 high at
    four and 1024 at two (SectorCheck on the classic `ANDY3.TRC`); 384 is
    more than the 242 a barrel reaches across the water.
    After the owner's second play test of 2026-10-11 a channel with a
    current and an inlet named in `kHDLifeInlets` is a conveyor the same
    way (`HDLifeConveyor`, `HDLifeInletRun`): its outlet is a river's, its
    floats come back at the inlet's slot point across the inlet's width,
    ride out of it the way the water comes in and rise over the first
    square; a float that drifts onto that square without coming out of
    the inlet floats on. OWN, a design of this project. DATA: ANDREA2's
    canal (SectorCheck on the classic `ANDREA2.TRC`): past its +z end the
    water room 133 goes on under the wall from the surface down; past its
    -z end and its side arm's west end no water at the surface's height;
    past its east bank at x 57344, z 43008 to 45056 the water rooms 58 and
    186 under a dry floor 256 over the water - the inlet.
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
    After the owner's play test of 2026-10-11: Lara within 1024 units of
    any fish, or a live splash of the game on the body's water, sends
    every fish on a dart of its own, away from the scare and turned by up
    to 25 degrees by a hash, at 2800 units a second falling off with a
    time of 1.5 seconds (3093 units in two), sliding along the deep water's
    edge; after 4 seconds the centre goes where the fish are and they swim
    back to the ring at 600 units a second; a new dart 1.5 seconds after
    the last at the earliest (`HDLifeDartSpeed`, `HDLifeDartWay`,
    `HDLifeSchoolTick`). OWN, a design of this project; the numbers are
    free choices. DATA: the school before it swam 110 units in the two
    seconds after Lara came and stopped at the first bank (RoomProbe
    `-lifelara`, ANDY3's pond and lake); the splashes are read from the
    game's `splashes[]` (`game\effect2.cpp`, `SetupSplash`), never written.
    A fish tank (`kHDLifeTanks`, `HDLifeAddTank`) holds a school of 8 by
    the same rules while its FISHTANK item has not been set off. OWN, the
    count a free choice. DATA: RICH2's tank, x 33792 to 34816, z 67584 to
    69632, y -3840 to -2816, where the classic room 23 and its flip 32
    differ (SectorCheck on the classic `RICH2.TRC`); the item's water,
    4096 at start (`game\init.cpp`, `InitialiseFishtank`).
    In that round's fix pass a fish turns to its way at 720 degrees a
    second, and its school's tail beat runs on a clock of its own, three
    times as fast in a flight (`HDLifeFishTurn`, `HDLifeSchool::beat`), so
    a dart turns no fish and jumps no wobble in one frame. OWN, free
    choices.
    After the owner's second play test of 2026-10-11 a school swims in a
    depth band (`HDLifeFishBand`): from 128 units under its sheet's rise
    down to `FISH2`'s reach, 87, over the floor of its body's shallowest
    deep square; calm, each fish rises and sinks 192 units round the
    band's middle (less in a narrower band) at 0.45 radians a second from
    a hashed phase (`HDLifeFishCalm`); a dart heads each fish for a hashed
    depth in the band, its way pitched 10 to 25 degrees toward it, level
    once there; back to its ring it climbs at most 346 units a second; the
    model is pitched to its way, at most 30 degrees, turned at 720 degrees
    a second. OWN, a design of this project; the numbers are free choices.
    DATA: 128 is `FISH2`'s half thickness (35) and the 44 its nose rises at
    30 degrees; a dart pitched straight at 25 degrees reached the edge of
    ANDY3's pond's band (809 high) within a third of a second (RoomProbe
    `-lifelara`). The tank is whole while its FISHTANK item is not active
    and its status is neither active nor deactivated, its water the full
    4096 (`HDLifeTankWhole`). CLASSIC: the states the game gives the item
    (`game\items.cpp`, `InitialiseItem`: `IFL_INVISIBLE` starts it
    invisible; `game\savegame.cpp`: a killed item loads deactivated with
    the 4096 `InitialiseItem` left). DATA: RICH2's item 33 carries flags
    0x100 in the classic level (RoomProbe `-lifetank game`), so it starts
    invisible, which the first rule took for a shot tank.
    After the owner's sixteenth play test of 2026-10-13, his third of the life
    (life round 4), a dart turns off the deep water's edge, the school
    regroups off it within 10 seconds, a still Lara scares a school once, a
    tank's school never flees and a big body's school roams: entries 186
    to 189. The first sentence's ring and the regroup "where the fish are"
    stand; the tank's school no longer follows "the same rules".
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
    draw shows (`hd_render.cpp`, `kRingsPerDraw`). After the owner's play
    test of 2026-10-11: a drip's ring is 96 and up to 15 more - three
    quarters of a wading Lara's or more (`game\effects.cpp`, 112 to 127),
    where the engine's drip size gave a quarter of her ring's strength -
    and it starts when its drop arrives: the drop leaves the ceiling over
    the drip (the classic sector's ceiling byte, through the rooms above)
    and falls as the game's own drip, its speed growing by 153 a tick
    (4300 units a second squared at 30 ticks, the project's g) and the
    drip moving that over 32. OWN, the sizes and the colour free choices.
    CLASSIC: the drop is the game's own drip, `Drips[]` (`game\tomb4fx.cpp`),
    moved by `UpdateDrips` and drawn by `DrawDrips` as a short streak, put
    into a free odd slot only (an even slot's drip that lands on a floor
    calls `TriggerDripSplash`); the drops page 3160 of the remaster's data
    is not used here - the cards take a splash and a spray, no single drop.
    A float whose floating height is 160 or more (the barrel) rings the
    water round it every 1 to 2 seconds (the hash of the float), 88 and up
    to 16 more at the top of its bob, through the same slot fill
    (`HDLifeFloatRings`). OWN, a design of this project, free choices;
    RoomProbe over 60 seconds: ANDY3's rings hold 9 of the 128 slots at
    once at the most, ANDY1's 5.
    In that round's fix pass the drop is no longer put into `Drips[]` - the
    free odd slot above held for its first build only: `GetFreeDrip`
    (`game\tomb4fx.cpp`) hands out the first free slot from `next_drip`
    on, so a slot the life held moved a wet Lara's next drip to another,
    and the game's random sequence followed the life. The pool holds its
    drops itself (`HDLifePool::falling`, at most 32, the size of the
    game's table) and draws each between two ticks (`HDLifeDropFall`).
    CLASSIC: the line's form, as `DrawDrips` (`specific\specificfx.cpp`)
    draws a drip - from its head to half a tick's move over it, the tail
    at half the head's colour, as wide as `GetFixedScale(1)`, through
    `ClipLine` and `AddPolyLine`. OWN, a free choice: the head's colour
    120, 160, 208, about one and a half times the middle of a wet Lara's
    drips (`TriggerLaraDrips`, 16 to 39 a channel, drawn four times that).
    After the owner's second play test of 2026-10-11 a barrel rings once a
    bob, at the bob's top, 2.5 to 4 seconds apart, at 240 and up to 15 more
    (the hash of the float and the bob's count), near the 255 a ripple
    slot's byte holds (`UpdateSplashes` grows a size only under 252; the
    sheets take the size they first see). OWN, a design of this
    project. DATA: a ring of the sheets starts at half its size, runs out
    at 150 units a second and is started again by the next of its kind
    within 192 units after 0.6 seconds (`hd_render.cpp`, the rings); the
    barrel reaches 163 to 179 from its middle across the water; RoomProbe
    `-liferings game`: at ANDY3's lake a barrel's ring of 88 to 104 every 1
    to 2 seconds changed 175 pixels at 1 second and none at 1.5, a wading
    Lara's of 120 at the same place 637; the ring of 240 to 255 changes 697
    and 609 there.
168. **The life's light** - a model takes its body's light, the one its
    sheet's colours are lit by, times 255 as its classic ambient, a fish
    half of it, with no light list and the game's sun factor 0.75. OWN, a
    free choice set by eye on the round's RoomProbe pictures: at the full
    light `FISH2`'s silver reads as white glints through the sheet. After
    the owner's play test of 2026-10-11: a river float's light and sun fall
    to a quarter over its dive into a slot (the push takes no alpha), and a
    fish tank's fish take the light of a body of light one (255). OWN, free
    choices.
169. **The pool's random and its hash** - the places are rolled at level
    load, and again when the HD level is rebuilt after the device was
    recreated, with `GetRandomDraw`, never `GetRandomControl`; a float's own
    numbers come from three steps of the same generator over its number,
    a second input mixed in times `0x9E3779B1`. CLASSIC: the generator's
    constants (`0x41C64E6D`, 12345, bits 10 up;
    `specific\function_stubs.cpp`). OWN, general knowledge: `0x9E3779B1`
    is 2^32 over the golden ratio, the usual multiplier that spreads
    neighbouring inputs. OWN, free choices: the second inputs' constants
    (101, 211, 307, 401, 503, 601, 7919 and a school's 7). After the
    owner's play test of 2026-10-11 the pool is kept when the HD level is
    built again for the same level and its bodies come out the same
    (`HDLifeSameBodies`), so nothing is rolled anew after a lost device;
    and three more second inputs: 811 and 821 a barrel's ring period and
    phase, 907 a fish's dart. OWN, a design of this project; free choices.
    So the first sentence's rolling again after a rebuild holds only when
    the bodies come out different; with the same bodies the pool is kept.
    After the owner's second play test of 2026-10-11: 1009 a fish's rise
    phase, 1103 and 1109 a dart's depth and pitch, and 811 with the bob's
    count a barrel ring's size (821 no longer used). OWN, free choices.
    After the owner's sixteenth play test of 2026-10-13, his third of the life
    (life round 4): 1201 the side a fish turns along an edge, 1301, 1303 and
    1307 a tank fish's line, period and phase, 1311 with a school's count of
    legs the tie between a roaming school's waypoints. OWN, free choices.
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
174. **The channel's highlight at 0.4** - entry 49's strength for the
    channel class, 0.8 until now (`hd_render.cpp`, `kWaterClass`). OWN,
    a free choice set by eye on RoomProbe pictures of ANDREA2's canal.
    DATA: scratch probes leaving one term out (`-waterdepthview 1` for the
    path): over the water that shows no floor the highlight lifted the
    median luma from 33 to 50, the reflection by 2; at 0.4 the median is
    45. Entry 126's river keeps its 0.4, now the channel's own and no
    longer half of it.
175. **A slope turned away takes the slope it mirrors** - the Fresnel term
    of a sheet pixel whose N.V is under 0 is taken at |N.V| (`hd_room.hlsl`,
    `WaterPS`). OWN, a design of this project: a facet hidden behind the
    crest in front of it stands for the visible one. DATA: in JOBY2's rain
    basin seen within 96 units of the surface 45 to 85 % of the water's
    pixels had N.V under 0 (a scratch probe marking them).
176. **The sheet held off an eye at the surface** - near the eye every
    vertex of a sheet keeps at least 32 units off the eye's height on the
    side the game puts the eye, a crest under an eye in the air cut down,
    a trough over an eye under the water held up, the hold fading out over
    1,536 units across (`hd_room.hlsl`, `HD_SWELL_GAP`, `HD_SWELL_EYE`);
    and the side a sheet is seen from is the eye's, in the air or under
    the water (c148.y, `gEye.w`). OWN, a design of this project; the
    distances free choices: the near plane, 20 units ahead at the game's
    80 degrees over a 4:3 width, reaches at most 24 units below or above
    the eye at any pitch. DATA: the eye of the play test's log (CAMLOG)
    held within 42 units of the surface, swept with RoomProbe.
177. **Lara's ambient along a swim** - in a water room without an air room
    her wet nodes keep the last air room's ambient; with the level's
    sheets built and while she is in the water her dry and wet colours
    follow the rules' over 0.25 s of game time, a share
    1 - e^(-dt / 0.25) a frame, and after she leaves it until they are
    within 0.5 of 255 (`hd_laradraw.cpp`). OWN, a design of this
    project; 0.25 s a free choice, the projected shadow's arrival's
    (entry 170), 0.5 half a step of the colour. DATA: in the water the
    classic path takes the rooms' ambients unfaded (`InitObjectLighting`,
    `lighting.cpp`); the ambients of `ROOM_INFO` (the randomizer's
    `Tr5Level`) and the air rooms of RoomProbe `-underwater` along the
    water rooms of six levels: the worst jump between neighbours 62 of
    luma, 16 after, 1.03 a frame at 60 frames a second.
178. **The water's look along a swim** - a water room no sheet lies over
    keeps the body of the last room the eye was in that has one, in place
    of entry 109's largest body while there is one; the extinction and the
    scattered light follow over 0.25 s of the game clock (`hd_render.cpp`,
    "along a swim"). OWN, a design of this project; free choices. DATA:
    the scattered light's luma (x 255) per water room, RoomProbe
    `-underwater`: the worst jump between neighbours 59, 4.4 after.
179. **A deeper bent place seen through its own path** - seen from above,
    the bent place of the copy (entry 61) is found before the water's
    colour, and one that lies deeper than the pixel's own and passes the
    guard (entry 75) is seen through the longer path: its extinction and its
    scattered light are the ones the floor's own pixels show
    (`hd_room.hlsl`, `WaterPS`, "the bent place first"). OWN, a design of
    this project. DATA: RoomProbe `-swimmer` (a box 30 units under the
    surface) at a logged camera over ANDREA1's pool room 32 with the
    level's three fog bulbs: 296 violet pixels at its rim, all of them
    where the read lay more than 96 units deeper (a scratch probe marking
    them), none without the bulbs or with `-waterbend 0`; 0 after.
180. **Fog bulbs of a water room under a sheet left out** - with sheets
    built, a fog bulb whose room is a water room a sheet lies over is not
    handed to the frame over the water either, as entry 110 leaves every
    bulb out under it (`hd_level.cpp`, `BulbUnderSheet`). OWN, a design of
    this project. DATA: the classic `.TRC` files' rooms: ANDREA1 has seven
    such bulbs, ANDREA2 sixteen, no other level any; the excess of a
    swimming Lara over the canal's water in a video of the game, 13, 18,
    15, stands in the ratio of the canal bulbs' colour 207, 255, 223;
    RoomProbe over the canal with and without them: 12.6 to 13.9, 17.5 to
    19.3, 16.3 to 18.0 over the whole frame.
181. **The world above bent a quarter outside the window** - seen from
    below, the move of entry 61 is whole inside the window of entry 85
    and a quarter outside it, over the window's edge (`hd_room.hlsl`,
    `WaterPS`, the under-water variant). OWN, a free choice set by eye on
    RoomProbe pictures. DATA: JOBY2's rain basin, the eye 8 to 32 units
    under the surface: the move stood at its 4 % bound over nearly the
    whole surface, a median of 31.7 pixels of 960 (a scratch probe drawing
    it); 8.9 after.
182. **The gloss variant's caustic projected by the face** - entry 82's
    projection in the gloss variant of `RoomPS` too, which kept the
    horizontal plane (`hd_room.hlsl`). OWN, entry 82's design. DATA: the
    riveted walls of JOBY2's rain basin are sheen ranges; under the
    waterline they showed vertical streaks with the eye over the water
    (gone with the caustic off, still there with the bend off): 9 and 13
    columns at two views, 4 and 6 after.
183. **The under look on the drawn eye's room** - the frame's under look
    (entry 110) asks the room the drawn eye stands in, `InterpEyeRoom`,
    not the logic room (`hd_level.cpp`). OWN, a design of this project.
    DATA: the camera's tick eyes of a logged ANDREA1 run: 11 of the 16
    frames a 60 Hz draw blends between the tick pairs that cross the
    surface, 32 of 48 at 120 Hz, drew the eye on the other side of the
    surface from the logic room.
184. **A channel's end foam** - a channel with a current of its own takes
    entry 116's blob at its downstream end: the way ahead along the
    current within the body's last square along it, RiverPS with the end
    alone, only in the rooms whose sheet lies within twice `kRiverEdge` of
    the end (`hd_render.cpp`, `WaterLook::hasEnd`, `SheetRange::endNear`).
    OWN, entry 105's and 116's design for a channel. DATA: ANDREA2's
    canal, its current from the flow row of room 60, runs on under its +z
    end wall into water room 133 (`SectorCheck`, classic `.TRC`).
185. **JOBY2's seas at half the foam** - the pinned rows of rooms 42 and
    143 and a row for room 7 (the rain basin) give foam 0.5
    (`hd_render.cpp`, `kWaterOverride`). OWN, a project decision of
    2026-10-12, chosen on RoomProbe pictures at foam 1, 0.5, 0.25 and 0.
186. **The edge in a flight** (life round 4) - at d units from the deep
    water's edge (`HDLifeEdgeDistance`, the nearest square less than 768 deep)
    the share of a dart's way that points away from it is at least (384 - d) /
    512, along the edge to the side the way leans to or, square on, a hash's
    side, turned at the fish's own 720 degrees a second; a fish whose way back
    to its ring the edge refuses rounds it the same way (`HDLifeEdgeWay`,
    `HDLifeTurnWay`, `HDLifeSchoolTick`). The school regroups where its whole
    ring keeps the stop, 428 off the edge, else at a deep square's middle; the
    rejoin runs faster than 600 units a second where a place lies farther than
    that reaches in the time left, and the flight ends within 10 seconds of
    its last dart; after it the heading turns to its cruise at 45 degrees a
    second. OWN, a design of this project; the numbers are free choices. DATA:
    RoomProbe `-lifeseries` with Lara at 12 bank places of ANDY3 and ANDY1:
    before, every fish of every flight stood 128 to 131 off the edge for 3.4
    to 6 seconds, and a flight's end moved every fish 250 to 500 units in one
    tick; after, none stays within 192 for 2 seconds and no fish moves more
    than 96 units a tick in a flight.
187. **A Lara who stands still** (life round 4) - a school she scared is not
    scared by her again until she has moved 256 units from where she stood at
    that dart, and that place is then forgotten; a splash scares it wherever
    she stands (`HDLifeSchool::seen`,
    `HDLifeSchoolTick`). OWN, a design of this project; the distance a free
    choice. DATA: with the edge turn the schools of ANDY1's pools darted 7 and
    12 times in 30 seconds and without end with Lara on a bank (RoomProbe
    `-lifeseries`).
188. **A tank's to and fro** (life round 4) - a tank's school never flees;
    each fish swims a line of its own along the tank's long side, the lines
    evenly over its width less the 128 stop each side in the order of a hash,
    on a cosine from 215 off one end to as far off the other, there and back
    in 20 to 30 seconds by a hash, turning about at each end at its own rate
    (`HDLifeTankBox`, `HDLifeTankAt`, `HDLifeAddTank`). OWN, a design of this
    project; the numbers are free choices. DATA: 215 is the stop and `FISH2`'s
    reach (87; its box 173 long, 26 wide); Lara before RICH2's tank lies
    within a square of its near fish, and the school there darted six times in
    30 seconds (RoomProbe `-lifeseries`).
189. **A big body's roam** (life round 4) - a school whose body's deep squares
    span more than 8 squares picks a waypoint, a deep square's middle 3 steps
    off at least, whose way runs over the most squares its centre never came
    to, then over those it came to longest ago, a hash deciding a tie, the
    first from the first square it comes to (`HDLifeSchool::been`), and swims
    there square by square on the shortest way over deep squares, a
    diagonal step only between two deep squares, at one `FISH2` length a
    second, its heading turned at 45 degrees a second (`HDLifeRoams`,
    `HDLifeRoamSteps`, `HDLifeRoamNext`, `HDLifeRoamPick`, `HDLifeRoamStep`).
    OWN, a design of this project; the numbers are free choices. DATA: ANDY3's
    deep-sea basin, 42 deep squares (a block of 8 by 3, two arms a square
    wide): its school's fish saw 4 to 9 of them in a minute over twelve rolls,
    7 to 17 with a waypoint by the hash alone, which sent the centre back along
    the way it came by, and 14 to 20 with this pick (24 to 32 and 32 to 39 in
    five minutes); ANDY3's pond and ANDY1's pools, 4 to 7 squares across, saw
    54 to 64 percent of theirs and keep their cruise (RoomProbe
    `-lifeseries`).
190. **Where a shot tank's water runs** - the classic squares under the
    dome's foot (the apron's footprint on the floor in the tank's frame,
    between the glass and the foot, as wide as the foot), and from there
    every flat square at the same height or one click (256 units) lower,
    across rooms by world square, never up a step, never down a drop,
    never behind the glass (`hd_render.cpp`, `build_wet_floors`). OWN, a
    design of this project: water on a floor runs level and down a step;
    the step of one click is the classic floor's own unit. DATA: RICH2's
    floors out of the classic level (`SectorCheck`): tank 182 reaches 50
    squares at y 0 of rooms 122, 121, 45, 46 and 113 (the raised blocks
    and stairs of 121 and 45 stay dry), tank 33 its plinth and the
    lowered area of room 23, 14 squares at two heights. The run keeps to
    the rooms of its tank's row and stops at a radius: entry 194. It runs
    over sloping squares as far as they lie level or fall: entry 204.
191. **The water's front over the floor** - a point of a 256-unit grid
    over those squares is wet once the front has passed its way from the
    glass (the shortest run over the grid, diagonals at their length, a
    step down at no cost), over a quarter square; the front comes out
    with the dome's apron (its 866 units in 0.25 s) and runs on at 321
    units/s, sqrt(g h) at the tank's film's 24 units; each point's way is
    pushed back by up to 384 units and its share of the mirror set
    between 0.5 and 1 out of two hashes of its place; once shot the floor
    stays wet, and a tank first seen shot is wet at once, as is every
    tank's floor from its break while `tomb5.hd_motion` is off - the run
    is motion, the wet the game's state (`HDWetFront`, `HDWetAt`,
    `HDRenderWaterWet`, `HDRenderWaterWetDraw`). OWN: the shallow-water
    wave speed is
    textbook physics with the project's g; the ramp, the push and the
    damp share are free choices, set by eye on the round's RoomProbe
    pictures. DATA: the furthest point of tank 182's floor lies 19,749
    units along the way (RoomProbe's `SPLASHLOG ... wet` line).
192. **The wet floor's look** - a layer 4 units over the floor, one draw a
    frame through the cards' vertex shader and its own pixel shader
    (`hd_wet.hlsl`, `WetPS`): the floor behind kept by one less 0.3 times
    the wet; Schlick's Fresnel with water's F0 0.02 weighting a mirror -
    the frame's copy where a ray reflected in the floor and marched in
    screen space (12 steps from 32 units, each 1.5 times the last) finds
    the frame, the copy's clip undone as Reinhard's curve x / (1 - x)
    held at 8, else the light field's colour times the polished walls'
    0.35 - and the light field's Blinn-Phong highlight at exponent 128
    with the textbook normalisation (n + 8) / 8, all held softly under the
    walls' cap of 0.75 and taking the distance fog as the cards do; it
    fades out where the frame lies more than 32 units under it. OWN, a
    design of this project from general graphics knowledge (a decal over
    the floor, Fresnel, a reflection in screen space, the inverse of a
    tone curve); the darkening, the exponent, the march and the 8 are
    free choices, set by eye on the round's RoomProbe pictures. DATA: the
    level's light field (the sheen's, `hd_gloss_field.h`) and the frame
    itself. The darkening and the mirror's weight stand a third higher
    (entry 195), the march halves its crossings and asks the depth to fit
    (entry 196), and the mirror is drawn after the rooms' glass and glows
    out of a copy of the frame there (entry 197).
193. **A floating body's ring** - a ring that starts over 128 units (the
    water's life's barrel, 240 to 255) keeps its size past the cap of
    128 and is 4 times stronger (`hd_render.cpp`, `kRingBody`;
    `hd_splash_math.h`, `HDRingStrength`). OWN, a free choice, set by
    eye on RoomProbe pictures of ANDY3's lake at night from the bank at
    700, 1,500 and 3,000 units: at 4 the ring reads from a wading Lara's
    camera at 1.5 and 2 s, at 2 it does not. A ring that starts at 128
    or less keeps entry 72's strength to the bit.
194. **A shot tank's rooms and radius** - a tank's water runs only over
    squares that lie inside the rooms its row names, or their flip twins,
    off those rooms' rings of walls and doors (`hd_render.cpp`,
    `kWetRooms`; a tank without a row keeps to its own room), and its
    front stops `kWetRadius`, 2,048 units, past the dome's foot along the
    water's way (`hd_splash_math.h`); a square stays while a point of it
    is wet with the front there, so the edge is the front's own, ramped
    and ragged. OWN: the rows follow the rooms named for the wet floor
    (RICH2 122, 121, 45, 46 for tank 182; 23 for tank 33); the radius is
    half the dome's foot, 3,186 units wide, taken up to the run's whole
    squares - a free choice on that ground. DATA: RICH2's floors out of
    the classic level (RoomProbe `-tankwetmap`): tank 182 keeps 16 of the
    50 squares (room 113 and corridor 46 past its mouth dry), tank 33 12
    of 14. The run also takes sloping squares (entry 204): tank 182 keeps
    22 squares of 55 found.
195. **The wet look's two strengths** - the darkening 0.4 (`kWetDark`)
    and a factor of 4/3 on the mirror's weight (`kWetMirror`, `WetPS`
    `gWetGain`). OWN, free choices, a third over the first look's 0.3 and
    1, set by eye on RoomProbe pictures of RICH2's two tanks so that the
    dark marble (a luma of 37 to 58 dry) still reads as wet stone.
196. **The mirror's march** - 16 steps from 32 units, each 1.3 times the
    last; a step that passes from in front of the frame's depth to behind
    it has its crossing halved 5 times and is the mirror's hit where the
    frame's depth there fits the ray's within 32 units and 2 per cent of
    the depth, fading out toward that tolerance; past it the ray went
    behind an edge and the march goes on (`hd_wet.hlsl`, `WetMarch`).
    OWN, a design of this project from general graphics knowledge (a
    screen-space march with a binary search and a thickness test); the
    numbers are set on RoomProbe's march debugger (`-tankmarch`) against
    a march of 8-unit steps halved ten times, which matches the scene
    seen from the eye mirrored in the floor: 93 to 98 per cent of the
    hits within 2 pixels of its, at RICH2's tank eyes. The march does not
    run where the art carries its own copy of the room under the floor
    (entry 203). Since the splash's fifth round: 22 steps, each 1.2 times
    the last (8,700 units), the crossing halved 8 times - a longer step
    passed a thin thing by, a machine's foot or a bench's rail, and took
    what lay behind it; set on the same debugger and on the true mirror
    at the owner's eyes of the corridor and a 10-degree fan round two of
    them: 0 to 2.2 per cent of the fine march's hits more than 2 pixels
    off where the game marches (0 to 3.5 at 16 steps of 1.3 halved 5
    times), a tighter tolerance or the first crossing taken behind an
    edge no better.
197. **The mirror's place in the frame** - the wet floor draws twice a
    frame: its darkening over the opaque frame, where the sheets' copy is
    taken first, and its mirror after the rooms' glass and glows and
    before the cards and the objects' glows, out of a copy of the frame
    there (`HDRenderWaterWetMirror`); under a tank's film the mirror gives
    up the film's strength. OWN, a design of this project: the order the
    pictures needed, measured with the debugger - the glows' triangles and
    what the glass shows reached no mirror out of the opaque frame's copy,
    and a mirror drawn after the cards lay over the dome and the spray
    (every wet pixel covered after the opaque copy at the burst of RICH2's
    tank 182 was the cards').
198. **The spring** (`hd_spring.h`, `hd_spring.cpp`) - a point under water
    with a hashed clock: a ring a period (0.7 s at the table's), each up to
    a fifth of a period early or late by hash, size 80 times the strength
    (at least 16), into the live rings unmerged, only within 6,144 units of
    the eye; a ripple row within 256 units rings the place instead. The
    compiled table's one row, ANDREA2 194 at x 55,808, z 32,768, floor
    2,560. OWN: the size and the period tuned by eye in the game with a
    file row, the place and the floor DATA (RoomProbe `-life`
    LIFESQUARES), the jitter and the reach free choices.
199. **The spring's bubbles** - three a second at strength 1 by hashed
    birth, 200 to 300 units a second, a sway of 8 to 20 units and a third
    across at 1.0 to 1.8 s, radius 5 to 12, one in three ringing the sheet
    at 48 to 63; premultiplied cards (`hd_spring.hlsl`, `BubblePS`) with a
    rim, a body and a highlight, soft over 32 units, the cards' fog, the
    under look's extinction and the sheet's absorption along the eye's path.
    OWN, free choices set by eye on the round's RoomProbe pictures; the
    speed from the textbook rise of small air bubbles (about 0.25 m/s);
    the sway's form, the radius range and the pop ring's size measured on
    the classic bubble (`game/tomb4fx.cpp`, `specific/specificfx.cpp`).
    With a crack (entry 206) a bubble is born on the crack's bottom at a
    hashed place in the slit, its spread and sway growing from nothing there
    to whole at the crack's depth: OWN, a free choice.
200. **The rising current** - a quad turned to the eye from the floor to
    the sheet, half-width 32 + 96 times the strength widening by a tenth of
    its height (a round jet's spread), drawn after the sheets as a multiply
    (`DESTCOLOR` / `INVSRCALPHA`, rgb w r, alpha w; r the frame's copy at a
    place bent by two cells of hashed value noise rising at 400 units a
    second over the copy at the pixel, floor 0.02, at most 2; w at most
    0.5), the bend at most 32 units at strength 1, `(1 - u^2)^2` across and
    1,200 over 1,200 plus the height; faded by the angle to its line and
    from 16 to 6 pixels of half-width. OWN, a design of this project from
    general graphics knowledge (refraction through the frame's copy, value
    noise); the numbers free choices set by eye on the round's RoomProbe
    pictures, the spread from the textbook round jet. Since `996e85a6`
    the noise's lattice values are a 64 x 64 table filled with the life's
    mixer (`HDSpringNoiseTexel`) and read with wrap and a linear filter -
    the column's first version took them from a published third-party shader
    hash without crediting it - that hash was 'Hash without Sine' by David
    Hoskins (2014), published under the MIT licence according to its own
    header, and it stands only in the commits de4cce39 to 58b3e8fb of the
    round's branch history - and no line of that hash is left;
    rgb and alpha are the share a, whole from half the weight up, the
    bend at most 96 units on cells 24 across and 96 along the line: OWN,
    set by eye on RoomProbe pictures from under the water. With a crack
    (entry 206) the column stands on the crack's bottom: OWN. The
    frame's copy is taken for a column too (`HDSpringFrameCopy`,
    `HDSpringColumnInFrame`): both ends more than a unit in front of
    the eye (`HDSpringColumnFront`, the draw's own test), a weight over
    0, the box of its ends widened by their half-widths on the screen.
    OWN, chosen on the owner's camera log at the fountain (no sheet's
    room in the frame, ticks 262 to 287) and a fan of 29 eyes round the
    column (RoomProbe, a measuring build).
201. **The file's spring row** - `spring <x> <z> <strength> [<seconds>]`
    in `hd_water.txt`: changes the table's spring of its level and room,
    takes it away at strength 0, else adds one; a row whose one key it is
    tunes no body; the sheet of the row's room, else the highest. OWN.
202. **The springs' placement rule** - a candidate square draws a sheet
    over 768 units or more, lies 3,072 units or more from Lara's start and
    from every portal through a wall that reaches into the water and has
    the body's squares on one side only, and under no static; a body that
    is no river and has no current, no spring of its own and six
    candidates or more holds one in two (the key `hd_springs` n: one in two
    times n, 0 none) by the hash of its level and first square, at a hashed
    candidate's middle, strength 0.5 to 1 by hash. OWN: 768 and six the
    life's deep water and school size (`hd_life_rules.h`), one in two chosen
    on the counts of the fifteen levels (RoomProbe `-springs`: five bodies
    fit, three springs; one in three gave one).
203. **The art's own mirror under a wet floor** - a wet square under
    whose floor the art carries a copy of the room upside down takes no
    march mirror, only the darkening (`hd_render.cpp`, `wet_copied`,
    `kWetCopyDepth`, `kWetCopyCells`; `WetPS` skips the march where the
    share is 0): in the column of the square, the art's 256-unit cells
    (`HDGroup::cells`) down to 2,048 units under the floor of every room
    with a flat floor there at that height, mirrored when a cell of any
    of them lies at the mirrored height over it; copied at 8 mirrored and
    a third of those under. OWN: the rule and its two numbers are free
    choices in the gap the count found. DATA: RICH2's corridor 46 (and
    its twin 75) carries such a copy for its whole length, 1,536 units
    deep (RoomProbe `-tankart`, every triangle of the .TRG: 24 to 874
    under each square, 40 to 100 per cent mirrored), and it reaches under
    room 122's two squares at its mouth; the corridor's 36 squares 12 to
    34 mirrored cells, room 122's two 15 and 16 (`-tankwetmap`, copy=).
    It reaches on under room 122's next column (58 and 74 triangles, 22
    mirrored), where no floor of the corridor lies: the rule asks only
    the art of the rooms whose floor the square is, and that column keeps
    the march. Every other square of either tank has none. A copied
    square loses the light field's stand-in for the march too and keeps
    the darkening and the lamps' highlight; at the corridor's mouth its
    floor is opaque, so the copy does not show there. The mirror marched there out
    of the frame showed the ceiling glows as yellow bands (1,446 to 5,194
    pixels at the eyes of his pictures, 0 after). Since the splash's sixth
    round a square counts as copied only where its floor is a clear floor
    as well (`wet_clear`, `HDGroup::clearFlats`), the one kind of floor a
    copy shows through: the corridor's mouth, opaque over its copy, marches
    again. The bands are the cove lights' true mirror, not a glow without
    depth (entry 215).
204. **The sloping floors under a wet floor** - the run crosses an edge
    where the next square's edge stands level or a click under the one
    it comes from at both corners (`hd_splash_math.h`, `HDWetPasses`),
    flat or sloping (`HDWetSlope`: a TILT, SPLIT1/2 or NOCOLF entry); a
    point lies on its square's own floor (`HDWetHeight`, the plane of its
    triangle as the engine's GetHeight divides a split), is dry where it
    stands over the edge the water came in by (`kWetSlopeRise`, 8 units,
    the corners' rounding), and the run goes on only across wet edges.
    OWN: water runs level and down, never up - the rule of entry 190
    carried onto slopes. DATA: the corner heights out of the classic
    floor data (`HDRenderWaterCorners`); RICH2 by tank 182 holds six
    splits round the raised blocks of rooms 45 and 121 (RoomProbe
    `-tankslopes`), each wet on its flat half now. The layer's cells are
    halved along the diagonal the square's crease follows (`HDWetCell`),
    so no triangle crosses the crease and the wet half is covered to it:
    room 45's three splits, creased from corner 1 to corner 2, had lost
    a half-cell on each of the crease's cells.
205. **No water's life over a menu's picture** - while a menu stands over
    a picture of the world (`MonoScreenOn`: the pause menu, the
    inventory, the load and save screens) the water's life is not put on
    the frame's object list (`hd_level.cpp`, `HDFrameDraw`). OWN, the
    engine's own frame order read: those menus draw no room, so nothing
    in the depth buffer hid RICH2's room 23 school, which was drawn over
    the picture at its true place behind the corridor's ceiling.
206. **The spring's crack** (`hd_spring.h`, `HDSpringCrackMake`) - a slit
    whose every choice is a hash of the spring's seed: a main line 256 to
    512 units long turned any way, bowed by up to 8 % and jagged by up to
    4 % at 14 steps, 32 to 96 wide, each side's half 0.6 to 1 of the
    profile sin(pi t)^0.6 and slid by up to 0.3 of a step; two to four
    branches a third of its length (0.8 to 1.2 of that), rooted between a
    fifth and four fifths of the way, the sides taking turns, leaning 20 to
    45 degrees (one lean a side, 5 either way more), a third of the side's
    half wide, closing in four steps, each a spike in the outline; the
    branches drawn again by the next round of the hash where the outline
    would cross itself (16 rounds); 64 to 128 deep; all scaled from half to
    whole by the strength and remade at 0.8, 0.64, 0.5 to lie on its body's
    deep squares. OWN: the sizes the request's, the rest free choices set by
    eye on RoomProbe pictures of ANDREA2's fountain against reference
    pictures supplied for this project; the hash is the life's mixer.
207. **The crack in the frame** (`hd_spring.cpp`, `HDSpringCrackFrameDraw`;
    `hd_spring.hlsl`, `HolePS`) - drawn at the start of the blend phase,
    before the copy and the sheets: a volume over the outline (8 units over
    the floor to 8 under the bottom, two fans and the edges' quads) marks
    with stencil bit 8 the pixels whose surface lies inside it by an odd
    count of faces in front of the frame's depth, the hole writes the
    bottom plane's depth along each pixel's ray there in black, the walls
    follow with the depth test; not drawn without a stencil, with the eye
    under the floor, at the near plane, past half the far plane's distance,
    off the screen or while its floor's room is off the frame's draw list.
    OWN, a design of this project from general graphics knowledge (stencil
    parity of a closed volume, a depth written by the pixel shader).
    The eye's test (`hd_spring.h`, `HDSpringCrackSeen`,
    `HDSpringCrackFloorFit`): the eye 8 units over the bottom, and over the
    outline's highest point or else over the plane of least squares through
    the outline's heights at the eye's own column. OWN, chosen on RoomProbe
    pictures of a fan of 42 eyes round ANDREA2's rule crack with the test
    removed. The eye's test asks no more for the bottom; for an eye
    under the bottom's plane (`HDSpringCrackBottom`) the hole writes
    the far plane's depth in black and the walls fill the slit, and the
    eye must stand over the level's own floor at its column
    (`HDSpringCrackGround`: of the `.TRG`'s floor read 4,096 round the
    outline, the height nearest the eye lies under it by the lift).
    OWN, chosen on that fan's rows under the bottom and three cameras of
    the owner's logs.
208. **The crack's walls** (`hd_spring.hlsl`, `CrackPS`) - the floor's page
    folded over the lip (a place d down the wall takes the floor's page d
    further in along the edge's inward normal, wrapped), the floor's light
    at the rim times the share 0.5 (1 - d / sqrt(d^2 + w^2)) of the slit's
    cross-section (w the width across from the edge) times a band from
    black at the bottom to whole at the crack's depth over it, then the
    rooms' distance fog and under-water look. OWN, the share a textbook
    view factor; the floor (its height, light, page and page map per
    outline point, out of the level's `.TRG` by the layout TRForge's reader
    documents) is DATA of the player's installation, read, never copied.
209. **The visibility path's approach** (`specific/hd/hd_vis.h`, `game/draw.cpp`
    `VisExtend`; `tomb5.hd_vis` 1) - the rooms of a frame are the portal
    walk from the eye's room, every listed room's art is drawn whole with no
    window cut and the depth buffer alone decides what hides what, and a group
    whose art stands in a listed room's box is listed with the part of that
    room's window its art's box covers. THIRD: TROne (GPLv3) by Todesengel1931
    - https://github.com/todesengel1931 - its TR2 visibility prototype
    (`host/render/hd_visibility_tr2.inc`, the copy handed over on 2026-10-14),
    read for the approach; no code was taken. Taken from it: the approach
    above and the guests' count of eight triangles (entry 210); the walk,
    the tables and the other rules below are this project's own. Its known
    failure, a room listed by its art box without passing the walk on, is
    what entry 213 answers. Since round 10 two depth pushes of the build stay
    on this path (entry 218); no art is cut to a window.
210. **The guests' rule** (`hd_vis.cpp`, `guestRule` 2) - a group's triangles
    count for a room when their centroid stands in the room's inside and more
    than 128 units outside the group's own room's box; eight such triangles
    make the group a guest, and a guest of a room not under the sky must be
    linked with it (a portal either way or a floor link, closed over the
    flipmap twins). THIRD for the count of eight: TROne (GPLv3) by
    Todesengel1931 - https://github.com/todesengel1931 - names a guest by
    eight or more triangles in a room's box (`host/render/
    hd_visibility_tr2.inc`, the guest table's comment); the centroid test,
    the 128 units and the link test OWN. DATA (TrgProbe `-vistable`,
    RoomProbe `-vis 1 -visguestrule`): 2,671 (holder, group) pairs have a triangle in the
    holder's inside, 2,377 eight or more; every such triangle counted lists
    JOBY2's room 24 in rooms 30, 31 and 33 by the slabs of its ceiling (32
    units over its box) and covers the stairwell's art with its marble at
    all ten of the owner's stair cameras; with no link test ANDREA3's room
    92 puts a beam into room 63 (400,730 pixels at one camera); 128 is
    `HD_OVERHANG_SLACK`; under the count of eight lie 294 pairs, stray
    triangles. Under the rule: 430 guests over the fifteen levels,
    175 more taken out by the link test.
211. **The openings** (`hd_vis.cpp`) - a classic wall face of a room whose
    own art covers half its wall samples or more is an opening when its art
    leaves half the face's samples or more bare (samples every 64 units, art
    facing the face's way within 128 units counting); the rooms whose box
    holds the point 512 units behind the face's middle are listed, linked
    ones only, by the face's corners as a portal's when the face faces the
    eye. OWN. DATA: of the 42,971 wall faces the art covers wholly within
    512 units, 91.4 per cent within 32, 95.8 within 128, 97.7 within 256;
    2,483 of 2,709 rooms have their walls asked; linked openings 575 over the
    fifteen levels, 413 taken out by the link test; half, 512 and the art
    share free choices. Since round 10 an opening names a flipmap pair by its
    main room (entry 219): 507 openings.
212. **The wide portals** (`hd_vis.cpp`) - a portal on an axis plane whose
    room has bare wall faces in that plane touching it (or touching a face
    already taken) is followed a second time with the rectangle round the
    portal and those faces, when it passes the portal's own by 64 units or
    more on a side; where that rectangle has no part inside the window the
    room is seen through but lands on the screen, the room behind is listed
    with it clamped to the screen alone. OWN. DATA: 53 portals of the 8,078
    in the fifteen levels; the same screen listing for every portal brought
    JOBY2's room 30 and its pipes into the court's wall (24,640 pixels at
    the court's camera). Since round 10 a face beside the portal counts at
    0.3 bare (entry 220): 64 portals.
213. **Passing on** (`game/draw.cpp`, `VisQueue`, `VisFollowPortal`) - a room
    a guest, an opening or a wide portal lists is queued as a portal queues
    it, with the part of the window it is seen in, so it walks on through its
    own portals and asks its own tables, its window growing by union as the
    classic one does. OWN.
214. **The art's copies counted** (RoomProbe `-tankcensus`) - every floor
    square of a level against the .TRG: the triangles 16 to 2,048 units
    under the floor, those whose centroid mirrored in the floor lies within
    32 units of one over it, and the floor's own faces by how the renderer
    draws them; copied at 8 mirrored and a third of those under. OWN, a
    measuring rule of this project (entry 203's numbers asked of
    triangles). DATA: RICH2 387 copied squares under a clear floor in 55
    rooms, RICHCUT2 5, TITLE 74, no other level; RICH2's tank rooms 121, 45
    and 23 carry none, room 122 one under an opaque floor. A square whose
    art floor lies off the classic floor is measured again about the flat
    art face nearest within 512 units (a free choice): RICH2 695 such
    squares, 50 copied about the art's floor, 2 under a clear floor in a
    room that has 28; over all levels no room gains a copy that shows.
215. **The cove lights in the wet floor's mirror** - the march keeps
    mirroring what the frame holds with depth; the yellow bands at RICH2's
    corridor mouth are the mirror of the corridor's cove lights, opaque art
    that writes its depth. OWN, a decision on a measurement: the additive
    pass alone draws nothing at those eyes (at two eyes of the owner's
    pictures it holds a few pixels, at most 28 of 255, away from the
    mouth's floor), the glass left as an alpha test
    changes no pixel, and of the bands' pixels 94 to 99 per cent lie within
    2 pixels of the fine march's hit (`-tankmarch`), on its surface, none
    showing what the later copy adds over the opaque frame.
216. **The models in the wet floor's mirror** (`hd_render.cpp`,
    `HDRenderWaterWetModels`; `hd_wet.hlsl`, `WetModelAt`) - the frame's
    object list drawn twice more into a target and an INTZ depth of the
    mirror's own: through the floor's reflection folded into the
    projection, and as the frame drew it into the target's alpha alone,
    both turned left to right to keep the winding; the march goes past a
    model's place, and a mirrored model on the floor's plane, under the
    floor and nearer than the march's hit is the mirror's. OWN, a design of
    this project from general graphics knowledge (a planar reflection
    through the projection matrix, a coverage mask); `game/mirror.cpp` is
    not drawn on. The plane - the height most drawn wet points stand at -
    and the 8 units of slack (a sloping corner's rounding, `kWetSlopeRise`)
    are free choices. DATA: the march's hits on a stand-in at Lara's place
    of the owner's pictures, 2,105 to 10,433 at 960 x 540. The place mask
    is written with both alpha blend factors zero over a clear 1, so it
    holds every pixel a model draws whatever alpha its shader returns (a
    gloss mask: on her body page a tenth of the texels under 64).
217. **The wet floor's mirrored models clipped** (`hd_render.cpp`,
    `HDRenderWaterWetModels`; `hd_splash_math.h`, `HDWetPlaneTo`) - five
    clip planes on the mirrored draw: above the floor's plane, and inside
    the box of the drawn wet points grown by one square, carried from the
    world through the view into the mirrored projection's clip space as
    the inverse matrix carries a plane. OWN, general graphics knowledge;
    the square of reach is a free choice (a model on the outermost wet
    square keeps what it reaches out with).
218. **The build's two depth pushes on the visibility path**
    (`hd_level.cpp`, `BuildRoomVis`, `DecidePairs`) - with `hd_vis` 1 the
    level still builds the intrusion list and the rooms' air, every piece is
    answered whole, and the renderer's hulls (a group whose art outside its
    own room lies on other groups' art over more than half of it) and
    covered faces (a room's classic face inside a larger one of the room it
    stands in, one plane within a unit) draw that art pushed back, so the
    copy of the room the art stands in is the one seen. OWN: this project's
    hull and covered-face rules (the #181 entry, rounds 6 and 7), kept. The
    covered face reads a classic datum - which room's classic face holds the
    other's, the holder round 8 found to be the floor's owner or the larger
    face at all 40 - but no classic routine chooses between two rooms'
    faces, so the rule is OWN, not CLASSIC/OWN; round 7's pairs carried no
    ledger class of their own to follow.
    DATA (RoomProbe `-vis 1`, its `hull:` and `covered:` lines): 13 hulls on
    nine levels and 40 covered faces on six, the old path's own; RICH2's
    shaft at the owner's picture 2 57,426 -> 5,402 pixels of eye-step fight,
    ANDREA1's room 212 from room 19 4,590 -> 2,836.
219. **A flipmap pair named by its main room** (`hd_vis.cpp`, the openings)
    - an opening whose room behind is the other room of a flipmap pair names
    the pair's main room, the one the portals lead to. CLASSIC: `FlipMap`
    swaps the two rooms' contents and the portals lead to the main room
    alone, so the other is never in the world. DATA (TrgProbe `-vistable`,
    `openings_twin=`): 68 such openings, ANDREA1 7, ANDY3 3, JOBY2 1, RICH1
    4, RICH2 16, RICH3 37; JOBY2's room 53 drew the crane hall's second
    state at the owner's picture 4 (32,590 pixels).
220. **A broken-out wall beside a portal** (`hd_vis.cpp`, `wideBare` 0.3) -
    a wall face beside a portal widens it from 0.3 of its samples bare, the
    openings keeping 0.5. OWN. DATA (RoomProbe `-vis 1 -viswidebare`): JOBY2's
    broken pillar beside the portal of rooms 123 and 124 lies between 0.3 and
    0.4 bare on room 124's side, and at 0.3 room 124's record of that portal
    widens; 53, 55, 64, 72, 82 and 113 wide portals at 0.5, 0.4, 0.3, 0.25,
    0.2 and 0.1. Alone it did not mend the owner's picture 3 (the walk from
    room 123 reads room 123's record; at the rule's own commit the fight
    stayed 20,290 pixels): entry 222 does, with this share.
221. **The window's edge** (`game/draw.cpp`, `VisFollowPortal`; `edge`
    2/2,560) - a portal whose projection meets the window it is seen through
    on an edge, or stops within 2 pixels of it at 2,560, lists the room
    behind with that strip. CLASSIC: `SetRoomBounds`' rectangle, one pixel
    out on every side, and its empty test; the tolerance OWN. DATA (the
    owner's ROOMFLASH frames walked again, RoomProbe `-visedge`): of 181
    rooms dropped for one frame 155 were refused with an empty window, 71
    of the 102 measurable 2 pixels or less off it; at 2 pixels 81 are
    listed again, at 4 85, at 8 90. With entry 222, 85 of the 181 at 2
    pixels, 11 with the edge at 0; drawn at those frames, the sky the dropped
    rooms leave shows at 6 of 155 frames against 8 without the edge and 9 on
    round 9's path.
222. **Both records of a portal take the art's hole round it** (`hd_vis.cpp`,
    the wides) - a portal record takes in the bare faces of the record that
    leads back (the same plane within 2 units, the rectangles touching) when
    that one is wide, so the walk widens the window from either room. OWN.
    CLASSIC fact read: a portal is stored twice, once in each room, and the
    two records need not span one rectangle - taking the other record's
    whole rectangle made 781 wide portals, its bare faces alone 55. DATA
    (TrgProbe `-vistable`, `wide_mirror=`): 119 wide portals, 55 of them by
    the record leading back (ANDREA1 7, ANDREA2 3, ANDY1 9, ANDY3 2, JOBY2 9,
    JOBY4 1, RICH1 9, RICH2 12, RICH3 1, TITLE 2); the owner's nine pictures
    of round 10 draw the old path's picture at 1,280 and at his 2,560.
223. **The room behind the models** (`hd_render.cpp`,
    `HDRenderWaterWetRoomCopy`; `hd_wet.hlsl`, `WetMarch` why 4) - the
    frame's colour copied before the objects are drawn, while the last
    frame's wet floor mirrored models; a reflected ray that went behind a
    model's place and met nothing after it takes that copy's colour at its
    first such place. OWN, a design of this project from general graphics
    knowledge (a screen-space reflection's hidden region filled from a
    layer without the occluder). DATA: RoomProbe `-wetlara` at the owner's
    eye and four round it, the floor pixels the march left to the stand-in
    because of her, 34,446 before and as without her after (probe pitch
    +4.3); at the pitch his CAMLOG eye looks, -4.3: 40,972 before, 1,173
    after. The copy: 0.016 ms of GPU at 2560 x 1440.
224. **The wet floor warmed before its first frame** (`hd_render.cpp`,
    `HDRenderWaterWarmUp`, `wet_warm_models`) - the wet floor's targets made
    at the frame's size and its pixel shader drawn once at the water's
    warm-up, and the level's first frame drawing its objects once through
    five clip planes that cut everything away. OWN, free choice of where;
    DATA: RoomProbe `-wetagain`, the first wet frame's draws 100 to 122 ms
    before and 0.6 to 1.4 after, the clipped draw alone 107 ms the first
    time and 0.13 to 0.24 without the planes.
225. **The flipped water** (`hd_render.cpp`, `HDRenderSetWaterSheets`,
    `body_shown`, `HDRenderWaterFlipRoom`) - a square over a room that is
    dry while its flipmap alternate is an underwater room is water while
    FlipMap has swapped the two, read in the alternate, its bodies apart
    and drawn only in that state. OWN, from the classic level's own
    flipmap pairs (DATA: the census of the 15 levels, five bodies in JOBY5
    and RICH3).
226. **The soft mirror** (`hd_render.cpp`, `wet_layer`, `kWetBlur`,
    `kWetBlurNear`; `hd_wet.hlsl`, `WetBlur`) - the wet floor's mirror drawn
    into a layer of its own, its alpha the blur's radius (the screen length
    of the march's last step, whole from 2,048 units from the eye on,
    shrinking nearer), blurred across and down over the covered pixels
    with a Gaussian of sigma r / 2, then added to the frame. OWN, a design
    of this project from general graphics knowledge (a separable blur of
    variable radius); the strength and the distance free choices by the
    metric. DATA: RoomProbe `-wetblur`/`-wetnomirror` at four band eyes,
    the mirror's variation along the reflected bands 1.276 sharp, 0.504
    built; +0.05 ms GPU at 2560 x 1440.
227. **The skipped flyby's last trigger test** (`game/spotcam.cpp`,
    `CalculateSpotCams`; `game/spotcam_math.h`, `SpotEndTestCamera`,
    `SpotSeqEndsShort`) - the cause: the skip jumps the spline after the
    frame's camera place is taken, so the end of a SNAPBACK sequence is
    tested at the camera before the last; the fix's rule: on such a frame
    the last camera's own place and room; and the census rule: first camera
    SNAPBACK without LOOP/TRACK, last camera SP_TESTTRIGGER. THIRD: TROne
    (GPLv3) by Todesengel1931 - https://github.com/todesengel1931 - the
    flyby skip fix in `games/tr5/src/game/spotcam.cpp` (`CalculateSpotCams`,
    its SNAPBACK branch) and the census in `games/tr5/glue/hostglue_input5.cpp`
    (the copy handed over on 2026-10-14), read for the cause and the rules;
    no code was taken, the form and the words are this project's own.
    CLASSIC: our own reading of the engine's frame order is the check
    (`MODIFICATIONS.md`, the flyby skip's entry). DATA (TrgProbe `-flyby`):
    64 sequences, 359 cameras, 58 test cameras, 11 sequences ending short,
    5 of them leaving a trigger unfired (RICH2 3, RICH3 6, ANDREA2 5,
    ANDREA3 5, RICH1 0).
228. **The flyby census and the FLYLOG seam** (TrgProbe `-flyby`,
    `FlybyCensus`; `game/spotcam.cpp`, `FlyLog`, `check_camera`;
    ModernTest `spotcam_end`) - the spotcam block found behind the fixed
    cameras by the level's own counts up to the "TEX" marker, the trigger
    of a test's place by the GetFloor door walk and the GetHeight descent,
    the frame-order model the rule is checked against, and one log line per
    heavy-trigger test with the arming camera, the place and the skip. OWN,
    this project's own tools on the classic engine's code (CLASSIC) and the
    player's files (DATA).
229. **The marble floors' page column** (`hd_gloss_pages.h`, the `mirror`
    column and class `A`; `hd_gloss_math.h`, `HDGlossMirror`, the
    `mirror`/`nomirror` word of `hd_gloss.txt`) - the floors that mirror,
    named by texture page: 35 pages. OWN, looked at by eye on the thirty
    largest floor pages of each level; DATA: RoomProbe `-marblecensus` -
    the footstep material names marble in three levels only and RICH2's
    marble is stone there, so the data alone does not tell polished stone.
    The lobby, the pool's surround and the title are in by a project
    decision of 2026-10-16.
230. **The mirror faces** (`hd_render.cpp`, the triangle loop and
    `marble_build`; `hd_splash_math.h`, `HDMarbleFace`, `HD_MARBLE_FLAT`) - a
    triangle of a room's opaque pass or clear floors on a page of the
    column, flat and facing up, outside the water rooms; the mirror's extent
    and its planes are those faces, never the classic squares. OWN, a
    design of this project; 0.95 the census's flat face. DATA: RoomProbe
    `-marble 1`, RICH2 42,228 faces at 67 heights, 180 in water rooms.
231. **The clear floors drawn opaque** (`hd_render.cpp`, `draw_pass`
    `opaque`, `HDRange::marbleOpaque`) - a clear floor of the column drawn
    with the blend off while the mirror is on, the art's mirrored copy under
    it hidden. OWN, a project decision of 2026-10-16 (the planar mirror
    takes the copies' place); DATA: RICH2 147 draw calls, TITLE 9.
232. **The planar pass** (`hd_render.cpp`, `HDRenderMarbleDraw`,
    `marble_note`, `marble_replay`; `hd_wet.hlsl`, `MarblePS`) - the frame's
    noted room, stand and static draws and its object list made again
    through the projection folded with the floor's reflection and turned
    left to right, one clip plane over the floor, into one target per plane,
    laid over the plane's faces through stencil bit 16. OWN, a design of
    this project from general graphics knowledge (a planar reflection); the
    composition shares the wet floor's Schlick term and cap, at the
    textbook F0 0.04 of polished stone and no darkening. DATA: GPU 0.14 to
    5 ms at 2560 x 1440 by the rooms drawn (the record's table).
233. **The plane order and cap** (`hd_splash_math.h`, `HDMarbleCoverage`,
    `HDMarblePlanesPick`; `hd_marble_planes`) - a frame's floor heights
    ordered by the share of the screen their faces cover, the higher first
    between equals, at most the cap (default 2). OWN, free choice; DATA: the
    cap's cost at 1, 2 and 3 planes at 27 eyes.
234. **The mirror's edges** (`hd_render.cpp`, `marble_make`,
    `g_marble_rows`, the flip's half pixel; bilinear reading) - the target
    read at 2 X + W - x plus a half, Direct3D 9's pixel centre, and drawn at
    two rows a row of the frame, read between them. OWN, from general
    graphics knowledge (the pixel-centre rule, supersampling); DATA: p7,
    the reflection 1.04 pixels back under the pipe, the reflected edge's RMS
    about its line 0.290 to 0.211 pixels.
235. **The wet floor keeps its points** (`hd_render.cpp`,
    `HDRenderMarbleDraw`; `MarblePS`'s mask mode) - the wet layer's points
    of the frame clear the marble's stencil bit where they are wet, so the
    water's mirror is the wet floor's alone. OWN, a project decision of
    2026-10-16; DATA: tank 182 has 16 of its 22 wet squares on marble, tank
    33 all 12.
236. **A shatterable static's piece by overlap and a pane's plane**
    (`hd_cover.h`, `HDStaticPiecePaired`, `HD_STATIC_PIECE_EDGE`) - a
    destructible piece of the art belongs to a static of slot 50 to 57
    when the two boxes overlap by more than 16 units along every axis the
    static has an extent on, and the static's plane stands within 16 units
    of the piece along an axis it has none on. OWN, the rule a free choice;
    16 chosen inside the measured gap. DATA (RoomProbe `-smashcensus`, the
    15 levels): true pairs overlap by 38 units or more, a pane stands 0 or
    1 unit off its glass (ANDREA1 rooms 198 and 201), a row's neighbours
    touch or overlap by 0 to 2 (JOBY2 room 57, RICH3 rooms 120, 169, 170,
    171); pairs ANDREA1 4 -> 6, JOBY2 7 -> 3, RICH3 44 -> 36.
237. **The smash census and the shot static's picture** (RoomProbe
    `-smashcensus`, `-smash`, `-smashpiece`, `probestatics::SmashCensus`,
    `SmashHide`) - every destructible piece, every shatterable static, the
    gap per axis and the pairing's answer, the pieces no static or several
    take; and the art after a static is shot, each pair written as
    `UpdateDestructibles` writes it, or with any piece hidden by its
    number. OWN, this project's own tools on the player's files (DATA)
    and the classic level's statics (CLASSIC).
238. **The deep look's sea surface from every water room** (`hd_water_under.h`,
    `HDWaterUnderDepth`; `hd_render.cpp`, `water_under_constants`) - in a
    level that takes the deep look a water room no sheet lies over counts
    its body's surface where it lies above the eye, the eye's depth under
    it the depth for the extinction, in place of the depth that puts every
    point in the water; an eye over that surface keeps the whole way. OWN,
    from the level's own water data (the body and the deep look's share);
    DATA: RoomProbe `-underwater`, the frame with and without room 19, his
    eye in room 6 0 to 116,979 surface pixels, his eye in room 123 and the
    other levels unchanged. ModernTest `water_under`.
239. **The sea seen from below, a study** (`hd_water_look.hlsl`, `LookPS`,
    `LookShaft`; `hd_render.cpp`, `HDRenderWaterProbeLook`; RoomProbe
    `-underlook`, `-underlookset`) - the sun's disc through the surface by
    the eye ray bent out through the normal of the body's own wave rows,
    the underside lit inside Snell's window, the shafts as the light's way
    followed back to the surface into a value noise of the cells' hash,
    two draws over the frame. OWN, a design of this project from general
    graphics knowledge (Snell's law, a marched light shaft) and the
    project's own wave and hash, aimed at reference pictures supplied for
    this project (not in the repository); the strengths, the disc's power, the cells,
    the reaches and the rim's width free choices. Probe-only; the game
    never draws it.
240. **The sky census** (RoomProbe `-skycensus`, `-skydump`, `ProbeSkyCensus`) -
    what the `.TRG`'s sky groups and the classic HORIZON mesh are made of, read
    part by part: the band a sphere of 4,100 (ANDREA3's 4,891) in 21 rings with
    v linear in its elevation, the cloud sheets flat squares of tiles of one
    page, each part's vertices one block in part order, the level's page table
    naming exactly the sky pages its parts use. OWN, this project's own tool on
    the player's files (DATA: the fifteen levels' `sky part`, `sky ring`, `sky
    pages` and `sky horizon` lines).
241. **The sky's alpha pass back to front** (`hd_render.cpp`, "The sky dome",
    `HDRenderSky`, `HDRenderSkyPartOrder`) - a sky group's alpha-pass ranges
    drawn in the `.TRG`'s part order, kept by each range's lowest vertex, with
    the depth test off among them. DATA: the part order is back to front in
    all thirteen sky levels and the vertex blocks keep it (`-skycensus`); the
    cause, the band's depth over its empty half, from the build's range sort
    and the blend's depth write. OWN, the painter's rule over that order;
    measured at the owner's JOBY2 eye: the clear pixels 105,937 to 0.
242. **The layers where the HD sky does not stand for them** (`game/draw.cpp`,
    `SkyDrawPhase`; `hd_level.cpp`, `HDFrameSkyCoversLayers`; `hd_render.cpp`,
    `HDRenderSkyCoversLayers`) - the classic flat layers left out only where a
    sky group holds a cloud sheet or an opaque-pass band. OWN, the rule; DATA:
    JOBY4's sky group is a band of one colour alone (page 7006, `-skycensus`,
    DdsProbe), its script's layers the sea's surface on the level's own sky
    page; TITLE's bands are in the opaque pass. Since 2026-10-17 a level whose
    water takes the deep look keeps its layers off as well while the water
    sheets are on (OWN, decided after the play test of that day): the surface
    seen from below is the HD picture and this project's own look.
243. **The HD page lent to the layers** (`hd_level.cpp`, `HDFrameSkyLayerPage`;
    `hd_render.cpp`, `HDRenderSkyLayerPage`; `game/draw.cpp`, `SkyDrawPhase`) -
    over an HD sky with a cloud sheet the layers draw the nearest sheet's page
    in the classic sky page's slot for their one `OutputSky`, on the flat
    layer's quads also with `PSX_skies` on. OWN, free choice of the sheet (the
    nearest, the last of the alpha pass in part order); DATA: the classic flat
    page is alpha 255 on every texel of ANDY2 and ANDY3 (the `.TRC`'s misc
    block), the PSX path's tiles are 65 x 65 windows of the `SKY_GRAPHICS`
    sprites on shared object pages (the `.TRC`'s sprite table), which a sheet
    page has no stand-in for; at the owner's ANDY3 eye 1,128 of 311,355 sky
    pixels stayed as the dome draws them with the flat page, 229,933 with the
    sheet's page, and the PSX tiles changed 150,748 (RoomProbe `-skylayer`,
    `-skylayerpage psx`).
244. **RoomProbe's layer twin and the volumetric clouds measured**
    (`hd_render.cpp`, `HDRenderSkyLayerProbe`, `HDRenderSkyCloudProbe`;
    `hd_sky.hlsl`, `LayerVS`, `LayerPS`, `CloudVS`, `CloudPS`; RoomProbe
    `-skylayer`, `-skylayerpage`, `-skycloud`, `-skygpu`) - the classic
    layer drawn as `DrawFlatSky` or `DrawPSXSky` (4 x 4 tiles a quad on the
    `SKY_GRAPHICS` sprites' windows) and the classic draw types 4 and 2 draw it;
    a cloud slab marched in 16 steps through a field of smoothly
    interpolated lattice values in two octaves, the values a hash of the
    cell's integer coordinates (two rounds of multiplying each axis by a
    linear function of another and folding back into 0..1), carried along x
    as `UpdateSky` carries a layer. OWN, a design of this project from
    general graphics knowledge; the steps, octaves, colours and fade free
    choices for the measurement, the octaves cut to two by the 512-slot
    budget (three took 519). Probe only; the game draws none of it.
245. **The visibility path's window on art standing in another room**
    (`hd_level.cpp`, `DecidePairs`) - with `hd_vis` 1 a piece of a room's
    art that stands inside another room (the intrusion list) gets the answer
    of `hd_vis` 0: inside the room's own window unless a classic face the
    pair claims is drawn this frame; the rest of the room's art stays whole.
    It replaces entry 218's 'every piece answered whole'; the pushes of 218
    stay. OWN: this project's pieces and their answer (the #181 entry,
    rounds 4 to 8), applied to the visibility path's list. DATA (RoomProbe
    `-vis 1 -viswin`, five candidates measured at the owner's places, camE
    and the owner set of 99): ANDREA1's brick block of room 93 at camE 8,957
    pixels changed against the old path down to 0 (7,112 of them the block),
    the owner's three ANDREA1 ghosts 4,853, 41,998 and 74,183 down to 0,
    JOBY2's bars by the pillar 9,339 down to 103; over the 99 cameras no
    view worse than round 10 by more than 500 pixels.
246. **The portal the eye stands at** (`game/draw.cpp`, `VisNearPortals`) -
    a portal whose four corners' box, grown by the art's reach of 128 units
    (`HDVisDefaults`, how far art stands off a classic face), holds the eye
    lists the room behind with the whole window of the room being walked.
    CLASSIC fact read: `SetRoomBounds` projects such a portal onto the
    screen's edges or behind the eye, and the side test skips a portal the
    eye has passed; the rule is OWN. DATA (the owner's ROOMFLASH frames of
    round 10 walked again, RoomProbe `-visnear`): JOBY3's room 76, JOBY5's 88
    with its children and RICH2's 55 listed at their frames' eyes, 4,029,896
    of the 4,127,491 pixels of sky those frames showed.
247. **RoomProbe `-viswin` and `-visnear`** (`Preparation/Tools/Diagnose/
    RoomProbe.cpp`) - the pieces' answer on the visibility path chosen per
    run (the engine's, every piece whole, the air pieces alone, every piece
    in the window, none, the whole group in the window) and the walk's copy
    of entry 246 with a switch to leave it out. OWN, this project's tool.
248. **The art's ripple from the water under it** (`hd_render.cpp`, `HDWaterArt`,
    `water_art_ripples`; `hd_splash_math.h`, `HDArtRipple`) - a material 16 part takes its
    ripple's scale, travel and rate from the body under it (the body's squares in the
    part's room under its box within 32 units of its height: the body's class, squares and
    row's `waves`) or, over no body, from the squares its own up-facing triangles cover
    and the class the data rule gives them with its room's open air; the length a fifth of
    the water's width and never under the class's longest wave row, the deep-water rate at
    that length at 0.35 of the class's calm, the travel a tenth of the part's UV span at
    the sea row's steepness and in proportion below it, never more. None of the file's
    numbers is read (entry 22). OWN: the rule, the fifth and the 0.35, free choices set on
    RoomProbe pictures of JOBY4's sea. DATA: 22 of the 28 parts lie over a body (RoomProbe
    `-hdwater 0 -nodraw`, the `WATERLOG ... art` lines). PICTURE (RoomProbe at 1280x720
    and 2560x1440, `-time 3`, against the tool of `5df75640`): under JOBY4's sea 11 to 37
    per cent of the pixels change, by 1.3 to 1.4 of 255 on average; JOBY5's sea 85 per
    cent by 5.5; JOBY3's kitchen pots 2.8 per cent by 24; ANDREA1's fountain 1.0 per cent
    by 11; with the sheets on, the views of ANDREA2's basin, JOBY5's room 49, RICH2's pool
    and TITLE do not change.
249. **The layers out under the deep look** (`hd_water_under.h`,
    `HDSkyLayersLeftOut`; `hd_render.cpp`, `HDRenderSkyCoversLayers`) - the
    classic flat layers left out under the HD sky in a level that takes the
    deep look with the sheets on (`hd_water` 1), whatever its sky holds,
    beside entry 242's sheet and opaque band; with `hd_water` 0 no look is
    drawn and the layers stay in, since the band's open pole would show the
    frame's clear as a dark disc overhead (2,576 pixels at tick 22,020,
    mean colour 10.5, 68.4, 64.8 against 23.2, 100.3, 103.6 just outside).
    OWN, the rule, at the owner's word that the SD surface must go;
    DATA: the deep look from the level's water data (`WATERLOG
    under_deep=1`: JOBY4, 132 water rooms no sheet lies over against 2;
    RICH3, 1 against 0, whose sheets leave its layers out already), JOBY4's
    band one colour alone (entry 242); at his picture's eye (tick 22,020) the
    PSX tiles had changed 433,045 pixels of 518,400. ModernTest `water_look`.
250. **The sea seen from below on the game's path** (`hd_render.cpp`,
    `HDRenderWaterLook`, `water_look_constants`, `water_look_draws`;
    `hd_level.cpp`, `HDFrameSkyLook`; `hd_water_look.hlsl`; `hd_water_under.h`,
    `HDWaterLookDepth`, `HDWaterLookKnobs`) - entry 239's look drawn by the
    game right after the sky, before the rooms, in a level that takes the
    deep look, with `hd_water` on and the eye under the water: its surface
    the water's top over the dive, its light the hue of the water's
    scattered colour, the waves' slope held to 1,500 / t of itself, the
    window's rim over 0.3 of cosine, inside the window's own span of 0.339
    (cosine 0.661 to 1). OWN, the design (entry 239) and its
    place; DATA: the top from the level's water rooms (JOBY4 -5,120, 867
    units over the remaster's art of the surface at -4,253), the colour
    from the deep look; the eleven numbers free choices set by eye at four
    eyes of the owner's dive for the look of a reference picture supplied
    for this project (not in the repository), not matched in brightness:
    the remaster's art and the band alone are brighter overhead than it
    (mean luma of the top third 55.2 at tick 22,020 against its 36.4), so
    the look adds 4.3 to 17.4 to the mean luma at those eyes (disc 0.2,
    shafts 8, underside 0.12); measured: 0 rock pixels touched at four eyes,
    0.885 to 0.912 ms at 2,560 x 1,440 (medians), nothing drawn with
    `hd_water` 0. ModernTest `water_look`.
251. **The clear filled with the water's colour** (`hd_render.cpp`,
    `water_look_draws`) - with the look, a third draw at a depth of 0.99999
    tested LESS adds the water's scattered colour where the depth buffer
    still holds its clear: the band's open poles past its last ring at 75
    degrees, no longer covered by the layers. OWN; DATA: the band's depth
    0.99998 (`HD_SKY_MINZ` 0.996 and the sphere at 4,100 units), at his
    picture's eye 2,576 pixels of the hole, the scattered colour 14, 32, 41
    against the band's 13, 35, 42.
252. **RoomProbe `-underlook`, `-underlookknobs`, `-undertick` and the sky
    line's `layers out`** (`Preparation/Tools/Diagnose/RoomProbe.cpp`) - the
    game's look drawn at the sky's place with the engine's numbers or
    others, the study kept under `-underlookset`, a walk of CAMLOG eyes
    whose rooms the portal walk lists and whose surface pixels the tool
    counts out of its own pictures (`ReadOwnPng`) - with the look's
    switches it stops unless `-sky` and `-rooms <n>` are given, without
    which the look is left out unseen -, and the layer rule's answer per
    level. OWN, this project's tool.
253. **Picture 218 and the clouds' texels** (RoomProbe `-skydump` of round 1,
    DdsProbe, the owner's log) - the picture matched to ANDREA2's tick
    25,031 (correlation 0.983); ANDREA2's opaque sheet 7109 and alpha sheet
    7101 at 13.4 units a texel, 2.15 to 6.29 and 3.00 to 8.76 screen pixels
    a texel across from 20 to 90 degrees up at his 2,560 x 1,440 (persp
    1,144); ANDREA1's 7107 near-black (2, 1, 12) and its horizon sheets at
    alpha 30 at the most. DATA; no rule changed.
254. **The vanishing surface classed over his dive** (RoomProbe `-undertick`
    on the 1,613 CAMLOG ticks of the owner's JOBY4 play of 2026-10-17) - the
    surface 0 at 34 ticks looking down at the dive's start, 2 under a
    seaweed card, and room 19 (the sea's surface art) missing from the
    portal walk at 164 ticks, reached only as a box guest of room 100: at
    the 106 of them looking up his build's surface art was 0 and the look
    now keeps 8,221 to 68,223 pixels. DATA; the walk agrees with all 61 of
    his `VISLOG` lines. No rule changed in the list: room 19's art still
    leaves the picture and comes back within a tick at the six edges of the
    three runs (mean added luma 13.60 to 4.55 at 21,547 / 21,548, 3.83 to
    12.25 at 21,577 / 21,578, 16.66 to 5.10 at 21,937 / 21,938).
255. **The wall of a claimed face** (`hd_level.cpp`, `DecidePairs` and
    `BuildVisWalls`; `hd_vis.h`, `HDVisFacesOneWall`, `HDVisWallLabels`) - with
    `hd_vis` 1 a piece of a room's art standing in the air of a room its own
    room has no link with is drawn whole only when a claimed face of its pair
    that the classic path draws this frame is one wall with the piece's own
    face; otherwise inside its room's window. One wall: the pair's faces joined
    face to face where one stands in the other's plane (every corner within one
    unit) or both stand upright and meet along an edge, round the corners a wall
    turns; made once per level. It narrows entry 245 and keeps `hd_vis` 0 as it
    was. OWN: the plane and edge tests and their use, read from the reason the
    pieces are answered per pair (the art of one wall reaches over the seams of
    that wall's faces, in its plane and round its corners). DATA (RoomProbe
    `-vis 1 -visplane`, `-visfaces`): ANDREA1's brick block of room 93 seen from
    the balcony of room 68, kept whole by the block's floor in sight while its
    roof and walls stood in room 101's space, 2,304 pixels at 1,280 x 720 down
    to 0; the plane alone also cut ANDREA3's wooden wall of room 8 at its corner
    (59,516 pixels at 2,560 x 1,440, black seams), which the wall keeps as
    before; over the sweep sets one view changes against round 11, ANDREA2 room
    115 at yaw 0 (405 pixels, room 70's sloped faces), and none else.
256. **The classic room the visibility path leaves out** (`hd_level.cpp`,
    `VisHidesClassicRoom`, asked by `HDFrameRoom`; `hd_vis.h`,
    `HDVisClassicRoomHidden`) - with `hd_vis` 1 a room drawn classic whose own
    group holds no art, none of whose classic faces any art covers (wholly or
    in part), and whose inside holds a guest of the visibility tables (a
    linked group's art, eight triangles or more) draws none of its faces.
    OWN, from the owner's decision on ANDY3's canopy (the art's trees replace
    the flat faces that stood in for them). DATA (TrgProbe `-classicart
    -perface`, `-vistable -visroom`; RoomProbe `-classicroom`): of the 21
    classic rooms with faces in the fifteen levels it takes ANDY3 88, 118 and
    122 and ANDY1 212, each a band of painted tree trunks and leaves round a
    forest's trees, all under the sky; at round 11's canopy place 219,836
    pixels change, 84,808 of them to sky, the rest to room 35's trees.
257. **RoomProbe `-visplane`, `-visfaces`, `-visportals` and `-walkdoors`**
    (`Preparation/Tools/Diagnose/RoomProbe.cpp`) - entry 255 on and off, a
    piece's claimed face with its corners under its decide line, a room's
    portal records with their world corners, and every closed door with the
    portals it shuts. OWN, this project's tool.
258. **The marble mirror's clip over the floor** (`hd_render.cpp`,
    `kMarbleClipOver`, `g_marble_clip`, `HDRenderMarbleDraw`) - the mirrored
    draw keeps what stands 4 units (the layer's lift, `kWetLift`) over the
    floor's plane, not what stands in it: at the plane the floor's own
    triangles were kept or cut by rounding and covered the reflection.
    OWN, from general graphics knowledge (a planar reflection's clip
    plane); DATA: the owner's log and videos, RICH2, the same eye with the
    clip in the floor and 4 units over it 60 to 34,574 pixels apart at his
    hall (room 12, ticks 15389 to 15412) and 61 to 9,755 at his corridor
    (room 46, ticks 12955 to 12975) at 960 x 540.
259. **Rising faces are no mirror** (`hd_render.cpp`, `marble_build`) - a
    face with a corner farther than the clip's offset from its plane
    leaves the mirror. DATA: RoomProbe `-marble 1`'s spread - every face
    of the eight levels lies with each corner within a unit of its plane
    or rises 15 units and more across itself (RICH2 561 of 42,228 counted
    with the copies in the level, 477 with them left out; ANDREA2 30,
    ANDREA3 4, RICH3 54, RICHCUT2 27); OWN, the limit the clip's.
260. **The art's copy under a polished floor left out** (`hd_render.cpp`,
    `marble_copies`, the triangle loop's skip; `kMarbleCopyNear`,
    `kMarbleCopyMargin`, `kMarbleCopyMirrored`) - while `hd_marble` is on,
    a group's triangles that are the room turned corner for corner about
    one of its mirror planes (each corner within 2 units, the same page),
    lie wholly under that plane's faces with a 64-unit margin, stand in no
    classic room's box and make a copy's square leave the level. OWN, the
    rule a project decision of 2026-10-16 (the copies dropped for the
    planar mirror); the square rule RoomProbe `-tankcensus`'s (8 mirrored,
    a third); 2 from the faces' spread (entry 259), 64 a free choice
    measured: without it two triangles beside RICH2's corridor showed as
    holes from room 121 (p9 562 pixels, v510 445), with it none. DATA: RICH2
    119,319 triangles; at the measurement's 28 eyes the pictures with and
    without the copies differ by at most 31 pixels (a crack between floor
    pieces the copy filled, see the record's Left).
261. **A mirror face wholly under its own room's floor is none**
    (`hd_render.cpp`, `marble_build`, `kMarbleBuriedIn`) - a face whose
    centroid, corners and edges' middles (these drawn an eighth of the way
    in) each lie under a face of its own group 16 units and more higher
    leaves the mirror: what is left of a copy. OWN, the rule and the
    eighth a free choice; DATA: RICH2 4,558 faces and ANDREA1 12 (squares
    under rooms 174 and 175's tops), no other level; the planes -22527 and
    1536 of the owner's two eyes gone. The centroid alone took ANDREA1
    room 175's visible floor at -5592 (63,266 pixels at A175b, 960 x 540).
262. **One floor, one height** (`hd_render.cpp`, `marble_build`,
    `kMarbleHeightJoin`) - heights within 2 units of the next are one
    plane, at the height most faces hold. DATA: entry 259's spread (two
    faces of one floor differ by at most 2), RICH2 33 to 24 heights, 1,834
    faces moved, the pool surround's -2816 and -2817; OWN, the rule.
263. **The cap re-judged** (`hd_marble_planes`, default 2, unchanged) - no
    hold of last frame's planes. DATA: 132 consecutive ticks of the owner's
    log (RICH2 rooms 120 and 126), the drawn set changing with the same
    planes in view 3 times, between second floors of 2 to 3 per cent of
    the screen, and a simulated hold (twice the cover to take a place)
    leaving 2 and losing 0.18 of a screen's cover; OWN, the decision.
264. **RICH2's banded marble in the mirror column** (`hd_gloss_pages.h`,
    3314 and 3315, class `A`) - OWN, the owner's decision of 2026-10-17;
    DATA: RoomProbe `-marblecensus`, three squares a page in rooms 46, 75
    and 128, two over a copy; at E190 4,342 pixels of the strip change.
265. **The marble mirror's warm-up** (`hd_render.cpp`,
    `HDRenderMarbleWarmUp`; `hd_level.cpp`, `BuildWaterSheets`) - once a
    level the mirror's target made and the plain, gloss and glow room
    shaders drawn once with the clip plane on, and the layer's pair once,
    as the water's warm-up does. OWN, after this project's water warm-up;
    DATA: the first marble draw at 2560 x 1440 at the owner's two eyes 42
    ms (median of five) before, 0.08 to 0.67 after; the load 27.7 to 48.6
    ms more over the sessions measured.
266. **RoomProbe's marble measurement** (`Preparation/Tools/Diagnose/
    RoomProbe.cpp`, `-marbleclip`, `-marblecopies`, the `marbleplane:`
    lines and the `marble:` line's spread, rising, copies, buried and
    joined counts) - OWN, this project's tool on the player's files (DATA).
267. **The room whose art is the sea's surface** (`hd_water_under.h`,
    `HDWaterSurfaceRoom`, `kHDWaterSurfaceReach`; `hd_level.cpp`,
    `SurfaceRoom`) - in a level that takes the deep look, the water room
    whose art reaches highest, when it reaches within 2,048 units of the
    water's top (the highest ceiling of a square of any water room, the
    deep look's own top). DATA: the classic squares and flags and the
    groups' art (RoomProbe `-surfaceroom`): JOBY4 room 19, a room of the
    sea floor (squares at 15,872 to 17,152), art up to -4,269 under a top
    of -5,120 (851), spanning x -6,608 to 55,136 and z 39,944 to 99,327;
    the next water room's art -1,280 (room 113, 3,840 under); the dry room
    1's art -4,352; RICH3's one water room 190 has its art at 0, its own
    floor, under a top of -4,864 (none). OWN, the rule; the 2,048 a free
    choice of two squares between 851 and 3,840.
268. **The sea's surface drawn at every frame** (`hd_level.cpp`,
    `HDFrameDraw`) - the surface room's art joins every frame of the HD
    path whose list leaves it out, with the whole screen as its window, at
    the first listed room's matrix moved by the two origins' difference
    (as `DecideStand`), its own lighting and the frame's extinction. OWN,
    the owner's rule of 2026-10-17 ("draw always") and the place, chosen
    over the reach walk (`draw.cpp`, beside `CarryArt`), which his dive's
    visibility path does not run; DATA: RoomProbe `-undertick` over 306
    eyes of his dive with and without it (164 ticks without room 19 on the
    list, the six edges, round 1's 40 eyes, every 40th tick).
269. **The surface room in the log and the probe** (`hd_level.cpp`, the
    `WATERLOG ... undergate` line's `surface=`; RoomProbe `-surfaceroom`,
    `-surfacealways`, `-undertick`'s `carried=`) - OWN, this project's
    log and tool on the player's files (DATA).
270. **The ten shatter slots** (`hd_cover.h`, `HDStaticShatterSlot`,
    `HD_STATIC_SHATTER_FIRST` 50, `HD_STATIC_SHATTER_LAST` 59) - a static of
    slot 50 to 59 can take a destructible piece. CLASSIC: the savegame
    writes and restores Flags bit 0 for exactly these slots
    (`savegame.cpp`), `ShatterSounds` holds ten per level, a shot takes 50
    to 57 (`control.cpp`), the gladiator's and the Roman god's blows 50 to
    59, the `XRAY_CONTROLLER` at OCB 333 any static at its own place
    (`objects.cpp`). DATA (the classic `.TRC` files): 16 statics of slots 58
    and 59, ANDREA2 2 (the window, slot 59, rooms 65 and 132), RICH1 2,
    RICH2 12; ANDREA2's item 109 is that controller, started by the same
    heavy trigger as flip 8.
271. **A flipmap twin's pieces left to its own state** (`hd_cover.h`,
    `HDStaticPieceInState`; `hd_level.cpp`, the pairing loop) - a static
    takes no piece of its room's flipmap twin, found as `flipped_room`
    names it. CLASSIC: FlipMap puts the twin in the room's place and sets
    the alternate's `flipped_room` to -1 (`control.cpp`). OWN, the rule.
    DATA (`-smashcensus`): ANDREA2's pieces 3 and 4, the same box in groups
    65 and 132, one pair each; no pair of any other level is lost.
272. **A static read where its list stands** (`hd_cover.h`,
    `HDStaticListRoom`; `hd_level.cpp`, `UpdateDestructibles`) - a pair
    reads its static in the room that holds the list it was built from,
    out of the group table `HDFlipRoom` swaps. CLASSIC: FlipMap's three
    `memcpy` lines swap two rooms' records with their `MESH_INFO` lists;
    `sgRestoreGame` flips first and restores the statics' flags after.
    OWN, the reading. DATA: the owner's logs of 2026-10-16 and 2026-10-17
    (the flip line in the flyby's tick, the glass sound a frame later, the
    flip right after each restored load).
273. **The smash census and the shot after a flip** (RoomProbe
    `-smashcensus`, `-statictable`, `-smash` with `-flip`,
    `probestatics::Twin`, `SmashHide`) - the ten slots, the twin's pieces
    marked ` twin`, and a shot named in the rooms as they stand after the
    flips, each pair reading its static where its list stands. OWN, this
    project's own tool on the player's files (DATA) and the classic
    level's statics and flip pairs (CLASSIC).
274. **The glass parts' texels and the three readings** (`MODIFICATIONS.md`,
    the windows' Round 2, "The bars' visibility"; nothing in the code) -
    the alpha run's material 14 parts with `param[1]` under 1 counted by
    the texels they cover (clear below 16, opaque 240 and above), and the
    present reading, reading B (opaque texels opaque) and reading C (clear
    texels opaque) drawn at one eye per page. OWN, the count and the
    readings, measured with scratch tools on the player's files (DATA),
    found through the engine's own resolvers: 74 parts, the clear texels
    on ANDREA2's page 3007 alone, the opaque ones on JOBY2's pages 513 and
    16073, RICH2's 3309 and RICH3's 10084.
275. **The gamma curve and the row's range** (`hd_gamma.hlsl`, `GammaPS`;
    `global/types.h`, `tomb5.gamma`) - the row's number n from 0 to 100,
    gamma = 2 ^ ((n - 50) / 50), output = input ^ (1 / gamma) per colour
    channel, the alpha written as 1. OWN, a free choice: the power law is
    the textbook form of a display's gamma; the middle at 50, the ends one
    octave each way (0.5 and 2) and every step the same factor 2 ^ (1/50)
    are this project's. PICTURE: RoomProbe `-gamma` over the six HD views
    at 0, 30, 70 and 100, every channel within one step of the curve taken
    on the processor; the power fitted back out of the two pictures
    1.3207 to 1.3238 at 30 (the curve's 1.3195), 0.7574 to 0.7580 at 70
    (0.7579), 0.4999 to 0.5002 at 100.
276. **No device gamma ramp** (`dxshell.cpp`, the gamma pass's comment) -
    `SetGammaRamp` acts on a full-screen exclusive swap chain only, general
    Direct3D 9 knowledge; this engine's devices are windowed in both of its
    modes (`DXCreate`, the border-less window, OWN since the port), so a
    ramp would act in neither. DATA, this machine's adapters measured by a
    scratch probe, read only: two outputs of one RTX 5080, both
    `D3DCAPS2_FULLSCREENGAMMA` 1 and `D3DCAPS2_CANCALIBRATEGAMMA` 0; a
    windowed device's `GetGammaRamp` is all zeros, no ramp in use; the
    player's `-log` of 2026-10-17 reads `DXCreate: Full Screen Mode
    (borderless windowed)` and `DXLOG window start ... windowed=1`.
    NOT MEASURED: no ramp was set, since setting one changes this machine's
    desktop; so a ramp's effect on an exclusive chain, and whether one set
    by a game outlives a crash or Alt+Tab, rest on the documentation and
    were not observed, and GDI's `GetDeviceGammaRamp` of the desktop failed
    (read 0).
277. **The pass's form** (`dxshell.cpp`, `GammaPass`, `GammaRestore`,
    `GammaRelease`) - the back buffer copied by `StretchRect` into a
    render-target texture of its size, drawn back over it as one
    pretransformed quad half a pixel up and left, point sampled, through a
    `ps_2_0` shader without a vertex shader; every render, sampler and
    stage state it sets read first and set back after, stream 0 and the
    vertex declaration included; the copy and the shader released with the
    device and before a reset - standard Direct3D 9 techniques of general
    graphics knowledge. OWN: its place in both of the engine's presents,
    after the last draw of every path - `DXShowFrame`, before the apostrophe
    screenshot, and the loading bar's held present while the HD build runs
    (`specific/drawbars.cpp`, `LoadBarPresentHeld`), the two sharing
    `DXGammaStart` before the present and `DXGammaFinish` after it - and the
    copy back after the present, which keeps the back buffer the engine's
    own picture between frames. CLASSIC: the inventory's and the pause
    menu's picture of the world is read out of the back buffer after the
    present (`CreateMonoScreen`, `specific/LoadSave.cpp`). PICTURE: a
    scratch harness built from the extracted block of `dxshell.cpp` on a
    windowed 2560 by 1440 device: the curve within one step at 0, 30, 49,
    70 and 100, sixteen states set back, the plain picture back in the
    back buffer after the present at each, the copy following a new size, no
    reference left on the device; 98 checks, 0 failed. The same harness on
    the block with the shared steps: the held present at 50 makes no object
    and writes one -log line, at 30, 70 and 100 `DXGammaStart`'s picture is
    the curve's within one step and the back buffer the plain picture after
    the held present's steps; 116 checks, 0 failed.
278. **Nothing at 50** (`dxshell.cpp`, `GAMMA_MIDDLE`, `DXGammaStart`) -
    at the middle no object is made and no pass runs; each present compares
    the row with the value the -log line last named, and the first present
    writes that line (`pass=off`, with `-log` only). OWN. PICTURE:
    RoomProbe at b35b3b96 and at this round's head, the fifteen parity
    cameras 15 of 15 (and 15 of 15 with `-gamma 50` given), the six HD
    views 6 of 6, the six classic views 6 of 6 identical.
279. **The cost** - the copy and the draw 0.0312, 0.0312 and 0.0313 ms, the
    copy back 0.0149 ms in each of three runs, medians of 200 rounds at 2560
    by 1440 on the GPU's clock (RoomProbe `-gamma 70 -gammatime 200`).
    DATA, measured on this machine's RTX 5080.
280. **The row** (`game/menu_frame.cpp`, `menu_rows_display`,
    `MenuNumGamma`, `MenuStepGamma`; `tomb5/tomb5.cpp`, the key `gamma`) -
    a slider third on the *Display* page under *Frame rate* and *VSync*,
    the picture's page; the volume rows' kind and so their keys and pad
    (`MenuSliderDir`), the mouse slider's step (`MenuSliderStep` over 0 to
    100), saved with the tomb5 settings; the label the literal `Gamma`,
    which needs no `CustomText` row; 50 on a first run, 50 for a missing
    key and for one above 100, as `dead_zone` and `mouse_sens` read. OWN,
    following the menu's own patterns. MenuTest's `display_gamma_row`
    checks it in the tree (44,272 checks before, 44,619 after).
281. **The seam and the tools** (`dxshell.cpp`, `DXLOG gamma`; RoomProbe
    `-gamma`, `-gammatime`, `GammaProbePass`) - one `-log` line at the first
    present and at each change, a refused step at its end once per device;
    the probe's pass written again in `GammaPass`'s order on the engine's
    own bytecode, the per-channel check against the curve and the power
    fitted back by least squares on the logarithms. OWN, this project's log
    and tools on the player's files (DATA). The dark place of the pictures,
    JOBY5 room 37, is the room of the player's `-log` of 2026-10-17 where
    `HDLOG laralights` reads `cur=0/0` with a dynamic light on in 423 lines:
    its mean 64.0 of 255 at 50, 86.5 at 70, 104.2 at 85 and 121.9 at 100.

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
