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

**TROne** — Todesengel — **GPLv3**.
The HD-at-run-time work (#123) carries code over from TROne, which is the same
licence as this project and is what allows the combination. What was taken so
far, file by file:

| Here | From TROne | What |
|---|---|---|
| `TOMB5 Code/TOMB5/specific/hd/hd_install.cpp` | `host/fs/discovery.cpp` | Steam discovery: the registry lookup, the `libraryfolders.vdf` reader in both of its shapes, the "recognise by content, never by folder name" rule, and the wording of the diagnostics. Only the IV-VI branch was taken. |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp` | `host/render/game_renderer.cpp`, `hd_load` (1919-2256) and `hd_draw_room` (2259-2312) | The shape of the level build and of the draw: one buffer for the whole level, a range table per group, group *i* is room *i*, groups past the room count are the sky dome, three passes per room in the order opaque / alpha / additive, and the additive pass blended `ONE, ONE` with the depth write off. Also the rule that a vertex whose texture layer is 0 falls back to the level's first page instead of drawing nothing. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_room.hlsl` | the same file's GLSL `k_hd_vert` / `k_hd_frag` (135-236) | The lighting rule — room light is the **sum** `colour1 * roomShadow + colour2`, each byte over 255, and the pixel is `texel * light` — the alpha test living in the alpha pass **only** (an opaque tile's alpha is a gloss mask, and testing on it blanks whole floors), and the additive pass weighting its colour by the texture's alpha. Translated to HLSL for shader model 3; not a line of the GLSL is copied. |
| `TOMB5 Code/TOMB5/specific/hd/hd_pagecache.cpp` | `host/render/hd_pages.cpp`, `upload2` / `fill_missing_tiles` | Where a page is looked for when it is not in its own game's `TEX` folder — the sibling games next, `WHITE.DDS` last — and the rule that a page that cannot be found is painted **white** and never black. The upload itself is new: TROne hands the driver BC7 blocks in one texture array, and Direct3D 9 has neither. |
| `TOMB5 Code/TOMB5/game/draw.cpp`, `DrawStaticObjects` | `host/render/game_renderer.cpp`, the baked-mesh rule (3722-3736) | The rule itself, not the code: a static mesh the remaster baked into the room art must not be drawn a second time (TROne's "double lantern" bug report). TROne suppresses by mesh pointer and needs an exception for a moveable that claims the same pointer; our seam skips one room's own `MESH_INFO` list, for which that exception cannot arise. |
| `TOMB5 Code/TOMB5/specific/hd/hd_model.cpp` | `host/render/game_renderer.cpp`: `build_skinned_model` (1807-1913), `bind_pose_of_slot` (1752-1805), the model and head loops of `hd_load` (1978-2100) | The rules of the object path: one `.TRM` replaces the **whole** classic model; a vertex's three bone indices are nodes of the classic mesh tree; the bind pose is that tree's accumulated offsets with the rotations left out, walked with the same push and pop; a `<NAME>_HEAD.TRM` is drawn **rigidly** on the node nearest the face geometry's own centre (measured, not hard-coded); a part's three index runs are the three passes, additive blended `ONE, ONE` with the depth write off; and a node the caller never gave a matrix for gets the last one it did give rather than collapsing to the origin (`fill_unseen_joints`, 1389). The renderer under them is new: TROne is OpenGL 3.3 with vertex arrays and texture arrays, this is Direct3D 9 with a vertex declaration, one texture per page and compiled bytecode. |
| `TOMB5 Code/TOMB5/specific/hd/hd_shader/hd_model.hlsl` | the same file's GLSL `k_trm_vert` / `k_trm_frag` (317-405) | The skinning rule — three bones and three weights per vertex, the bind offset off before the node matrix goes on, the normal through the same matrix's 3×3 part — and the alpha test in the alpha pass **only** (TROne's golden-skull report). Translated to HLSL for shader model 3, with the bind offset folded into the matrix on the processor instead of subtracted per vertex; not a line of the GLSL is copied. The lighting is **not** TROne's: its item light is a flat ambient blown into a six-face cube plus four point lights of its own invention, while this engine has the classic path's own per-object light list, distance fog and fog bulbs. |
| `TOMB5 Code/TOMB5/specific/hd/hd_render.cpp`, the water marks | `host/render/game_renderer.cpp` (2113-2188) | The **road**, not the code: the remaster's geometry carries no water flag, so the classic level's own water vertices are matched to it by room-local position through a hash of the packed int16 x/y/z, per room, once at level build - and which group owns which vertex is recovered by walking the group's index runs. TROne marks a vertex from the classic `RoomVertexMove` flag and from the room's underwater bit; ours marks it from `ROOMLET::nWVtx` and from the engine's own shimmer predicate, and carries three bits rather than one. |

Those files name Todesengel's copyright in their own headers, beside our
modification notice, as GPLv3 section 5(a) asks. More of TROne is expected in
the later stages of that work (the mesh-bracket logic, the Lara composite);
each will be entered here as it arrives.

Not taken, and named here so the absence is on the record rather than an
oversight: TROne's `hd.light_stage` (an A/B switch that replaces the baked room
light with a flat ambient - a debugging pole for a renderer whose classic path
is a fallback; ours has the classic path itself), its `hd.water_effect` (an
invented wave and a per-pixel caustic, where this engine has Core's own
`WaterTable` and reproduces that instead), and its HD sky dome (TR5 draws its
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

**TRForge** is vendored, not linked against: five files are copied **byte for
byte** into `TOMB5 Code/TOMB5/vendor/trforge/src/` — the reader for the
remasters' `LEVELn.TRG` geometry and the whole-file helpers it needs — and
compiled into `PCTomb5.exe`. `vendor/trforge/VERSION.txt` names the source, the
date, the SHA-256 of every file and what was deliberately **not** taken. The
first three lines of a TRForge module carry a hidden signature, so those files
are never reformatted and never carry our modification notice; anything we need
differently is written in our own wrappers under `TOMB5/specific/hd/`.
`vendor/trforge/NOTICE.md` is the part of TRForge's own notice that applies.

One TRForge file could **not** be vendored and was used as a description
instead: `trf_remaster/ItemModels.h` / `.cpp`, which includes TRForge's own
level library and would drag it in with it. What it gave us is the **format**
of a `.TRM` object model — the header, the part table, the meaning of the word
that is zero in every model but a head, the trick that finds a head's geometry
behind its facial rig, and the 24 byte vertex with its three bones and three
weights. `TOMB5/specific/hd/hd_trm.cpp` is written against that description
rather than copied from it, and says so in its header; every number in its
comments was measured again here over the 566 models of the remaster's game 5.
The credit is TRForge's all the same.

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

The release package carries all of these in its `Licenses\` folder;
`Preparation/Tools/BuildRelease.ps1` puts them there.

## Credited, but not contained here

trview (chreden), TRmod (b122251), TR Viewer (E. Popov) and Tomb Raider
Chronicles itself are named in the credits because the work would not exist
without them. None of their code is in this repository.

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
