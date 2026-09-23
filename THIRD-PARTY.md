# Third-party code in this repository

This project is GPLv3 (see [LICENSE](LICENSE)). It builds on other people's
work, and this page says whose, under what terms, and where the full text of
each licence lives in this tree. Nothing here is a summary you should rely on
instead of the licence itself — every entry points at the authoritative text.

## The engine

**TOMB5** — Troye / Trxyebeep and contributors,
<https://github.com/Trxyebeep/TOMB5> — **GPLv3**.
Everything under `TOMB5 Code/TOMB5/` is a modified copy of that decompilation.
What was changed and why is recorded file by file in
[TOMB5 Code/MODIFICATIONS.md](TOMB5%20Code/MODIFICATIONS.md), which is also the
dated notice of modification GPLv3 section 5(a) asks for.
Full text: `TOMB5 Code/LICENSE`.

**tomb3** — Troye / Trxyebeep, <https://github.com/Trxyebeep/tomb3> — **GPLv3**.
The control code of four Tomb Raider III enemies (T-Rex, raptor,
Compsognathus, vulture) was taken over directly rather than rewritten, so that
its origin stays visible. Each of those files names its source in its own
header; the whole arrangement is set out in section 3 of MODIFICATIONS.md.
Same licence as this project, which is what allows the combination.

**TROne** (GPLv3) by Todesengel1931 —
<https://github.com/todesengel1931>.
The HD-at-run-time work (#123) carries code over from TROne, and the modern
control scheme (#161) carries its design and its test expectations over.
TROne is under the same licence as this project, which is what allows the
combination. What was taken so far, file by file:

| Here | From TROne | What |
|---|---|---|
| `TOMB5 Code/TOMB5/specific/hd/hd_install.cpp/.h` | `host/fs/discovery.cpp` | Steam discovery: the registry lookup, the `libraryfolders.vdf` reader in both of its shapes, the "recognise by content, never by folder name" rule, and the wording of the diagnostics. Only the IV-VI branch was taken until #123 stage S7-2; since then the I-III branch is taken as well - the same table row, `tomb123.exe` beside subfolders 1..3 with `DATA` and `TEX` - because the four Tomb Raider III guests the randomizer imports have their HD models in that bundle. The rest of TROne's discovery (the classic installs, the per-game level counts, the PIX / TRACKS / FMV folders) is still not taken. |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp/.h` | `host/render/game_renderer.cpp`, `hd_load` (1919-2256) and `hd_draw_room` (2259-2312) | The shape of the level build and of the draw: one buffer for the whole level, a range table per group, group *i* is room *i*, groups past the room count are the sky dome, three passes per room in the order opaque / alpha / additive, and the additive pass blended `ONE, ONE` with the depth write off. Also the rule that a vertex whose texture layer is 0 falls back to the level's first page instead of drawing nothing. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl` | the same file's GLSL `k_hd_vert` / `k_hd_frag` (135-236) | The lighting rule — room light is the **sum** of the vertex's two colour triples, each byte over 255, and the pixel is `texel * light`; the same sum is TRForge's own MIT reading of the format (`trf_remaster/HdMesh.cpp`, `colour_of`), which is the route this project relies on. The shader carries a spare scale on the first triple that this build always sets to 1 and nothing computes. Also taken: the alpha test living in the alpha pass **only** (an opaque tile's alpha is a gloss mask, and testing on it blanks whole floors), and the additive pass weighting its colour by the texture's alpha. Translated to HLSL for shader model 3; not a line of the GLSL is copied. |
| `TOMB5 Code/TOMB5/specific/hd/hd_pagecache.cpp/.h` | `host/render/hd_pages.cpp`, `upload2` / `fill_missing_tiles` | Where a page is looked for when it is not in its own game's `TEX` folder — the sibling games next, `WHITE.DDS` last — and the rule that a page that cannot be found is painted **white** and never black. The upload itself is new: TROne hands the driver BC7 blocks in one texture array, and Direct3D 9 has neither. |
| `TOMB5 Code/TOMB5/game/draw.cpp`, `DrawStaticObjects` | `host/render/game_renderer.cpp`, the baked-mesh rule (3722-3736) | The rule itself, not the code: a static mesh the remaster baked into the room art must not be drawn a second time (TROne's "double lantern" bug report). TROne suppresses by mesh pointer and needs an exception for a moveable that claims the same pointer; our seam skips one room's own `MESH_INFO` list, for which that exception cannot arise. |
| `TOMB5 Code/TOMB5/specific/hd/hd_model.cpp` | `host/render/game_renderer.cpp`: `build_skinned_model` (1807-1913), `bind_pose_of_slot` (1752-1805), the model and head loops of `hd_load` (1978-2100) | The rules of the object path: one `.TRM` replaces the **whole** classic model; a vertex's three bone indices are nodes of the classic mesh tree; the bind pose is that tree's accumulated offsets with the rotations left out, walked with the same push and pop; a `<NAME>_HEAD.TRM` is drawn **rigidly** on the node nearest the face geometry's own centre (measured, not hard-coded); a part's three index runs are the three passes, additive blended `ONE, ONE` with the depth write off; and a node the caller never gave a matrix for gets the last one it did give rather than collapsing to the origin (`fill_unseen_joints`, 1389). The renderer under them is new: TROne is OpenGL 3.3 with vertex arrays and texture arrays, this is Direct3D 9 with a vertex declaration, one texture per page and compiled bytecode. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_model.hlsl` | the same file's GLSL `k_trm_vert` / `k_trm_frag` (317-405) | The skinning rule — three bones and three weights per vertex, the bind offset off before the node matrix goes on, the normal through the same matrix's 3×3 part — and the alpha test in the alpha pass **only** (TROne's golden-skull report). Translated to HLSL for shader model 3, with the bind offset folded into the matrix on the processor instead of subtracted per vertex; not a line of the GLSL is copied. The lighting is **not** TROne's: its item light is a flat ambient blown into a six-face cube plus four point lights of its own, while this engine has the classic path's own per-object light list, distance fog and fog bulbs. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp/.h` | `host/render/game_renderer.cpp`: the item bracket (1440-1520), `fill_unseen_joints` (1391), `model.covered = ~0u` (1825-1830) and the swap-key rule (3800-3812, 4170-4180) | The **routing rules**, not the code: an item is drawn either wholly HD or wholly classic and never half of each; a node the model has no geometry for is drawn by NOBODY, not even classically (TROne tried the classic fallback there and took it out again after user reports of classic patches on HD bodies); a swapped mesh on a covered node loses to the model, so the model draws what the remaster drew; and a node the walk never reached keeps the last matrix it saw rather than collapsing to the origin. The seam itself is ours - TROne is a module beside a foreign engine and reads its brackets out of a stream of draw calls, this one rides on `DrawAnimatingItem`'s own node walk - and so are the skeleton test, the `mesh_bits` node ranges and the whole of the lighting. |
| `TOMB5 Code/TOMB5/specific/hd/hd_object.cpp`, the inventory ring (`HDInvBegin` / `HDInvNode` / `HDInvEnd`) | `games/tr5/glue/hostglue_2d5.cpp` (467-523), and the `TRONE_LIGHT_EXACT_NODES` semantics of `host/api/trone_host_api.h` (154-169) | The **idea**, not the code, and only half of it was kept: that the classic ring's off-centre projection - it moves the projection centre to each item's place on the screen - can be folded into the ITEM for the length of one draw, so that the HD path keeps one projection for the whole frame; and that the ring's light collapses to the ambient plus one sun taken from the ring's own brightness. TROne folds it into the item's own matrix as a **shear** of two rows against the third; this engine does **not**, because its shader takes the vertex normal through those same matrices and renormalises it, so a shear lights the ring by screen position (measured on the shader: 28 % too dark in the middle of the inventory row, 16 % too bright on the ammo row). The offset goes into the projection matrix instead, per draw call (`HDModelOffCentre`, `hd_model.cpp`), where the classic path has it and where a normal cannot reach it. The arithmetic was derived from TR5's own projection line (`aTransformLightClipMesh`, `specific/output.cpp`) and is written for this engine's matrices, not for TROne's float stack; the derivation stands in full over `HDInvNode` and over `HDModelOffCentre`. TROne also needs a flag to make the ring draw **exactly** the nodes it was given, because its default invents a matrix for an unseen node; ours has taken `item->mesh_bits` as a mask since stage S3-2 and needs none. Not taken: TROne's scale term `f_mpersp/persp`, which corrects for a host projection built from the window's real width - TR5 computes `f_persp` from a 4:3 implied width whatever the window is, and the HD projection here is built from that very number, so the factor is exactly one (measured: `ModelProbe -ringcheck`, the bounding rectangle at the same **size** on 4:3 and on 16:9). |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp`, the water marks | `host/render/game_renderer.cpp` (2113-2188) | The **road**, not the code: the remaster's geometry carries no water flag, so the classic level's own water vertices are matched to it by room-local position through a hash of the packed int16 x/y/z, per room, once at level build - and which group owns which vertex is recovered by walking the group's index runs. TROne marks a vertex from the classic `RoomVertexMove` flag and from the room's underwater bit; ours marks it from `ROOMLET::nWVtx` and from the engine's own shimmer predicate, and carries three bits rather than one. |

| `TOMB5 Code/TOMB5/specific/hd/hd_lara.cpp/.h`, and `hd_wardrobe.cpp/.h` with it | `host/render/game_renderer.cpp`: `lara_flush` (1547-1679), `lara_build_models` (2462-2604), `lara_load` (2619-2800), `draw_hair` (1498-1546), and `games/tr5/glue/hostglue_file5.cpp`: `RegisterLaraWardrobe` (1856-1922) | The **composite itself**: Lara is not one model but an outfit for the body, a separate `LARA_HEAD` drawn rigidly on the head node, a `HAIR_OG` braid on its own six-bone chain, a `HAND_` model chosen by the slot whose mesh sits at the hand node, and a `HOLSTER_` model for a worn weapon; an empty holster draws nothing because the straps are in the outfit; and the whole wardrobe lives in the shared `4\ITEM` folder, not in the running game's. The role table (which slot means which model) is TROne's, name for name. What is **not** TROne's: the section table - its TR5 glue has `ANDY` and `JOBY` the wrong way round and would put the Russia outfit on Ireland - and the outfit test, which here reads the engine's own `LaraDrawType` and its own braid predicate rather than the level's file name. Also new is the split hand rule: TROne draws one hand model for both hands, which loses TR5's flare, torch and crowbar frames, so a frame whose two hands differ draws both models masked to one arm each. The role table and the search that finds those models live in `hd_wardrobe.cpp/.h`, which is why that file stands in this row rather than in one of its own. |

| `TOMB5 Code/TOMB5/specific/hd/hd_laradraw.cpp/.h` | `host/render/game_renderer.cpp`: `draw_hair` (1498-1546) | The **braid rule**: `HAIR_OG` has one bone per link, so all six links get a frame of their own built from the link's absolute position and its two angles - where TR5 itself builds a matrix for links 1, 3 and 5 only and draws 0, 2 and 4 as skinned connectors out of stashed vertices. Everything else in that file is this engine's own: the LMX-to-LM turn of `lara_matrices`, the per-node ambient out of `LaraNodeAmbient`, the hand-slot pointer comparison, the first-person mask (#126) and the mirror. |

| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl`, the wind and the water UV | `host/render/game_renderer.cpp`: `k_hd_vert`'s `hd.water_effect` block (154-176) and `k_hd_frag`'s caustic (211-224) | The **wave shape and its scale**: two sines of the position taken across each other, about 24 classic units of travel and a wavelength of roughly one and a half blocks, plus a UV displacement of a few texels driven by the same phase. Half of it is not taken and the reason is in the file: TROne's water flag is a room bit, ours is the classic path's own water marks (stage S2), and TROne's per-pixel caustic is left out because TR5 has one already - the `WaterTable` shimmer, reproduced exactly in stage S2 - so a second one would count it twice. Translated to HLSL; the weight the wave rides on is the `.TRG`'s own `TrgVertex::sway`, which TROne does not read. **Fix round R3b takes the other half as well**: the vertical swell on the water marks, and the per-pixel caustic - `sin(x) * sin(z)` plus a half-weight diagonal, all three on the world position and on the game clock, at TROne's own scale of 0.018 per unit and strength 0.32. The refusal above stands for the BRIGHTNESS and was wrong about the MOTION: the `WaterTable` shimmer is computed per vertex, and a remaster water surface is a few large quads, so what reached the screen was a slow blotch - which is the same thing TROne's own comment reports about TR4 water. So the shimmer is untouched, parity and all, and the caustic is added as a zero mean modulation that carries no average brightness of its own. |
| `TOMB5 Code/TOMB5/specific/hd/hd_fontmatch.cpp/.h`, `hd_font.cpp/.h` and the branch in `game/text.cpp` | `host/render/game_renderer.cpp`, the block its own code marks `hdtext` (`hdtext_find` and `hdtext_arm`), and the glyph draw beside it. **No line numbers**, on purpose: the ranges this row once carried fit no copy of TROne that can be read here, so the block is named by its own marker instead — the reasoning stands over `hd_fontmatch.h` | The **method**, not the code: find a classic glyph in the remaster's `FONT<n>_OG.DDS` by sliding its ink mask over the atlas and scoring **intersection over union**, refuse anything under **0.94** because a similar character is the wrong character, take the `FONT<n>.DDS` beside the winner as the picture to draw from, and relocate the classic UV to the hit while adding **one texel of margin to image and screen in the same ratio**, so that the letter neither moves nor grows. Also theirs: the alpha ramp threshold 96 for what counts as ink, and the observation that the atlas of TR5 carries the classic glyph bitmaps unchanged. What is **not** taken and is not needed: their sprite scan and their "is this an alphabet" gate - the 106 glyph rectangles are known here (`game/chardef.inc`), so there is nothing to discover and nothing to guard against. New here: the per-glyph margin (a uniform one puts a slice of the neighbouring tick beside the apostrophe - measured), the seeding of that measurement from the classic page, the tpage sentinel that carries a quad to a texture outside `Textures[]`, the mip chain with its LOD bias, and the whole Direct3D 9 side. Their alpha-test change is not needed either: this engine's alpha reference is already 0. |

| `TOMB5 Code/TOMB5/specific/polyinsert.cpp`, the mip LOD bias of the HD font quad | `host/render/game_renderer.cpp` of the TROne version of 2026-09-13 (Copyright 2026 Todesengel, GPLv3), where the same bias stands with the same reason in TROne's own words | The **number and the reason for it**: a mip LOD bias of **-0.7** on the glyph quad, so that a font atlas minified by a D3D9 sampler keeps its edge instead of blurring into the next mip. The minification this project spends it on is its own measurement (1.8 at 1080p, 2.7 at 720p, 1.35 at 1440p), and so is where the bias lands at each of those (level 0.14 at 1080p, 0 at 1440p). |

| `TOMB5 Code/TOMB5/game/modern_move.cpp/.h`, `game/modern_camera.cpp/.h`, `game/modern_state.cpp` and `Preparation/Tools/Diagnose/ModernTest.cpp` | `tests/test_controller.cpp`, `host/input/` and `docs/CONTROLS.md` of the TROne version of 2026-09-13 | The modern (analog-stick) control scheme (#161). The **design** is TROne's and is re-derived here rather than copied: the stick's direction is measured against a held reference heading and turned into the classic engine's own tank bits, so that the game logic underneath never learns that anything changed. What was taken **verbatim** is the one thing worth having verbatim - the 32 expectations of TROne's own test suite (28 `modern.*`, 4 `combatmode.*`), rescaled to this engine's units by `S()`. Not taken: TROne's code. `game/modern_move.h` carries the origin block - which TROne version, and that the taboo table of 2026-09-16 found none of the set's 32 constants derived from a Remaster binary. |

The HD files above name Todesengel's copyright in their own headers, beside
our modification notice, as GPLv3 section 5(a) asks. In the two rows added in
audit round A1 the notice sits in the file that carries the taking —
`specific/polyinsert.cpp`, `game/modern_move.h` and
`Preparation/Tools/Diagnose/ModernTest.cpp` — and the files named beside them
are this project's own code written to that design. Anything further will be
entered here as it arrives.

Not taken, and named here so the absence is on the record rather than an
oversight: TROne's `hd.light_stage` (an A/B switch that replaces the baked room
light with a flat ambient - a debugging pole for a renderer whose classic path
is a fallback; ours has the classic path itself), the **caustic** half of its
`hd.water_effect` (see the row above), and its HD sky dome (TR5 draws its
sky procedurally and the HD walls paint over it). TROne has no fog of any kind
- its `set_fog` is a logged stub and it parses `Trg::fog_boxes` only to drop
them - so the distance fog and the fog bulbs in `hd_room.hlsl` have no
counterpart there and are transcriptions of `specific/drawroom.cpp`.

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

**TRForge** is vendored, not linked against: five files are copied **byte for
byte** into `TOMB5 Code/TOMB5/vendor/trforge/src/` — the reader for the
remasters' `LEVELn.TRG` geometry and the whole-file helpers it needs — and
compiled into `PCTomb5.exe`. `vendor/trforge/VERSION.txt` names the source, the
date, the SHA-256 of every file and what was deliberately **not** taken. The
first three lines of a TRForge module carry a hidden signature, so those files
are never reformatted and never carry our modification notice; anything we need
differently is written in our own wrappers under `TOMB5/specific/hd/`.
`vendor/trforge/NOTICE.md` is the part of TRForge's own notice that applies.

Because those five files are **byte-verbatim** copies, the sentences inside
them are TRForge's own statements about TRForge's own research, not this
project's claims — including the two places in `trf_remaster/Trg.h` where
TRForge writes down how it settled a format question. This project does not
repeat that route. The two facts it takes from that header are the
bias-127 vertex normal and the `1/32767` sway weight, and both are
re-measured here on the player's own `.TRG` files (`specific/hd/hd_render.h`,
`HDStats::swayMax`, and the counts in `MODIFICATIONS.md`).

One TRForge file could **not** be vendored and was used as a description
instead: `trf_remaster/ItemModels.h` / `.cpp`, which includes TRForge's own
level library and would drag it in with it. What it gave us is the **format**
of a `.TRM` object model — the header, the part table, the meaning of the word
that is zero in every model but a head, the trick that finds a head's geometry
behind its facial rig, and the 24 byte vertex with its three bones and three
weights. `TOMB5/specific/hd/hd_trm.cpp` is written against that description,
with two exceptions its own header now names: the facial-rig rescan and the
small byte reader beside it follow TRForge's code closely. That is lawful
under the MIT licence, and TRForge's notice travels with this tree in
`vendor/trforge/LICENSE`. Until audit round A1 this page and that header both
said "rather than copied" of the whole file, which was true of the rest of it
and not of those two passages. Every number in its comments was measured again
here over the 566 models of the remaster's game 5.

The same file gave one more thing, in stage S7-2: the rule by which a slot
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

And once more in fix round B of #123: **what a `DUMMY` line in a `.MAP` means.**
`trf_remaster/HdMesh.cpp` (the note beside `items_dummy`, in TRForge's own
source, which is not vendored here) has it measured and written out - `DUMMY.TRM` is a real file with
no parts in it, 240 of the 2,940 bindings across both installs point at it, and
the remaster is saying *draw nothing here as an object*, "sometimes because it
has baked the thing into the room mesh instead, which is where the lanterns of
Rome live". TRForge names the consequence too: read the empty model, count it
among the ones you could not draw, and let the 1996 object be drawn instead,
and the lantern stands twice, one inside the other. That is exactly the bug
this project's own play test reported, and
`TOMB5/specific/hd/hd_object.cpp` acts on TRForge's answer: a slot the level's
own `.MAP` calls `DUMMY` is drawn by nobody. The finding is TRForge's; the
seam, the per-item room test and the measurement that confirmed it over
ANDREA1's items are ours.

And a third time in fix round N of #123: **the `.TEX` sidecar, and what an HD
page is.** Two more TRForge files that could **not** be vendored and were used
as a description instead — both pull in TRForge's own asset and level
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

And a fourth time in fix round O of #123: **the `EXTRA` prefix a pickup's own
model sits under.** TRForge's `bind_level_items` (`trf_remaster/ItemModels.cpp`,
the same file used as a description above) already showed the general shape -
a slot the `.MAP` is silent about tries the bare engine symbol next. The
remaster's own host code goes one step further for exactly the models that
fallback still lands on nothing for: TROne's `host/render/game_renderer.cpp`
tries `EXTRA<game>_<SYMBOL>` and then `EXTRA_<SYMBOL>` in front of the slot's
own name, and excludes an "Effect" category - `GUN_FLASH` among them - on its
own stated ground that such a slot is drawn bracketed inside another entity's
mesh walk rather than as a level item of its own (Copyright 2026 Todesengel,
GPLv3; not vendored, used as a description only, for the same reason as the
two TRForge files above - both pull in a foreign renderer). That ground is
TROne's, for TROne's renderer: measured against THIS engine the flash never
reaches the per-item path at all, which `MODIFICATIONS.md`'s fix round O entry
writes up - the slot stays excluded either way.
`TOMB5/specific/hd/hd_bind.cpp`'s `bind_against` is
written against that description, not copied from it: the order the three
prefixes are tried in, and the rule that this project's own hand table is
asked BEFORE any of them, are this project's reading of it. The `EXTRA4_` tier
- for `COMPASS_ITEM` and `CROSSBOW_AMMO1/2_ITEM`, TR4 survivors TROne itself
can never reach because it always runs as its own game number - and the
measured table of which of the 471 slots resolves to which file on the
installed bundle are new, in `MODIFICATIONS.md`'s fix round O entry.

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
deliberate decision of this project's author (2026-09-21): they are upstream
TOMB5's material, they are switched off here rather than removed, and they are
kept for completeness of what was inherited. Nothing in this fork links a
symbol out of them.

One thing an audit of those prebuilt libraries turned up and this page has to
say: `discord-rpcd.lib` was built with **RapidJSON** statically inside it, so
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
files themselves no longer carry the content credentials such a tool writes.

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

**And in the repository as well, since #142:** the same 15 zone files and the
two checksum tables beside them are baked, zlib-packed, into the tracked
source file `TR5Randomizer/src/core/EmbeddedData.cpp` (about 7,200 lines,
0.75 MB), generated by `Preparation/Tools/EmbedData.ps1`, so that the
randomizer carries its data built in and the build needs no generator. A file
in `data\` beside the program still wins at run time. That copy is the same
derived material as the release's, and this paragraph named it release-only
until audit round A1 corrected it.

**In the repository, not in the release: `Preparation/Level Infos/*.json`,
16 files, about 21 MB.** The fuller survey the zone files are generated
from: rooms, items, triggers and the sector grid, keyed to each `.TRC` by
SHA-256. Until 2026-08-30 a 45 MB copy of these shipped as
`data\level_*.json`; the randomizer now reads what it needs straight from
the level file, so they stayed behind in the repository, where the build
still needs them.

Neither set contains a texture, a mesh, a sound or a script. Neither can be
rendered or played, and neither can reconstruct a level file. They are a
survey of geometry, not a copy of it. But they ARE derived from the game's
level design, so calling this project entirely free of Tomb Raider data
would be overstating it.
