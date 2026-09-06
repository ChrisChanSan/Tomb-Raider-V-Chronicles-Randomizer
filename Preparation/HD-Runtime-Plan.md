# HD at run time (#123, way B) - implementation plan

> **Where this stands.** Stages S0 to S8 are BUILT, on branch `hd-runtime`.
> Since then the work has been play-test fix rounds rather than new stages:
> **A** (rooms - flipmaps, cut-outs, glow, fog bulbs, sway), **A2** (the
> cut-out rule by material, and the fog bulb cone), **B** (objects - the level
> map, baked lanterns, mesh swaps, Pierre's head, cutscene actors), **C**
> (Lara - the Desert Eagle, the VCI holsters, the gloves, the braid root, the
> Tomb Raider III bird, the search order, the documentation), **I** (Lara -
> the backpack as a box, the braid's anchor, a cap over the first person
> collar, and a material per texture page), **J** (the neck cut re-shaped as a
> cylinder about the neck axis) and, latest, **K** (the cut widened to reach
> the collar, that cap withdrawn for the body's own INNER SKIN, and Lara's
> skin pages shining like skin so that the wrist seam is one material).
>
> This file is the PLAN and is left as it was written, so that what was
> intended can still be read against what was built. What was actually built,
> and every place the build departed from the plan, is recorded in
> `TOMB5 Code\MODIFICATIONS.md` - stages S0..S8 and then one section per fix
> round. Where the two disagree, MODIFICATIONS.md is the one that was measured.

Written 2026-09-05 from a read-only survey of TROne (GPLv3,
`C:\Cloude AI\Todesengel\TROne-main`, the user's chosen base), its vendored
TRForge readers (MIT), our engine and a fresh Steam copy of the Remaster.
Every number is measured over the real files unless marked "estimated".
The user's decision: way B - the engine draws the Remaster's rooms from
the `.TRG`, objects and Lara as `.TRM` models with skinning, BC7 pages,
Remaster light, water - all read at run time from the user's Remaster
installation, file present -> HD, otherwise vanilla. Way A (repainting
the classic pages, `HD-Patch-Plan.md`) is superseded by this decision.

Nothing is taken from the Remaster's binaries. TROne code is GPLv3 and
may be carried over with its author's copyright notice; TRForge is MIT
and is copied verbatim.


## 0. Three findings that shape everything

1. **Our seam is much narrower than TROne's.** TROne needs a 54-slot C
   ABI because host and game logic are separate binaries. Its TR5 glue
   shows exactly where it taps the logic, and those are two functions
   that exist unchanged here: `InsertRoom(ROOM_INFO*)`
   (`specific\drawroom.cpp:63`) and `phd_PutPolygons(short*, long)`
   (`specific\output.cpp:1569`). Plus the frame bracket
   (`S_InitialisePolyList` `:1416` / `S_OutputPolyList` `:1451`) and the
   level bracket (`file.cpp:1583 LoadLevel` / `:409 FreeLevel`). Six
   hooks, no more.
2. **A whole TROne problem class does not exist here.** Its "orange
   seams" (`set_room_frac`) come from an int32 matrix ABI; we read
   `aMXPtr` (`specific\3dmath.cpp:51`) as float. Its second read of the
   classic level (`register_room/mesh/model`) is unnecessary: `room[]`,
   `meshes[]` (real `MESH_DATA*` after `ProcessMeshData`,
   `drawroom.cpp:740`), `objects[]` and `bones[]` are already in memory,
   in the randomized state. That saves the whole `trf_level` vendor
   branch.
3. **The most expensive item is in no TROne document.** Our engine
   computes per room vertex (`drawroom.cpp:305-685`,
   `aRoomletTransformLight`): dynamic lights with falloff, shimmer and
   mist from `WaterTable`, distance fog, fog bulbs, underwater wibble.
   If the HD path draws the room, none of that runs - a torch would not
   light the wall, the user's `tomb5.fog` / `distance_fog` / `shimmer`
   switches would be dead in HD rooms. TROne never had this problem (its
   classic path is a fallback nobody looks at). For us it is parity
   work and a stage of its own (S2b).


## 1. The seam

| # | hook | file:line | what |
|---|---|---|---|
| 1 | level load | `file.cpp:1654` (after `LoadTextureInfos`) | `HDLevelLoad(stem)`: TRG/TRM/pages, slot -> TRM, bake map |
| 2 | level free | `file.cpp:409` | `HDLevelUnload()` |
| 3 | room draw | `game\draw.cpp:1027` (and `:1349` from `PrintRooms`) | `if (!HDDrawRoom(i, r)) InsertRoom(r);` |
| 4 | mesh draw | `output.cpp:1584` (after `aSetViewMatrix`) | `if (HDPutPolygons(objptr)) return;` - item bracket, Lara composite, bake suppression |
| 5 | frame start | `output.cpp:1446` | `HDBeginFrame()` |
| 6 | frame end | `output.cpp:1473` (before `DrawBuckets`) | `HDFlush()` |
| 7 | projection | `3dmath.cpp:724/76/707` | HD projection matrix |
| 8 | device | `dxshell.cpp:88` + reset path | HD resources |

`phd_PutPolygons_I` and `DrawStaticObjects` both call `phd_PutPolygons`,
so one seam covers interpolated items and statics.

**Carried over from TROne** (`host\render\game_renderer.cpp`, same data
model: same `MESH_DATA*` keys, same bone tree, same swap-mesh pairs):
`bind_pose_of_slot` (1752-1805), `build_skinned_model` (1810-1914),
`draw_skinned` (1300-1387), `fill_unseen_joints`/`fill_lara_joints`
(1391-1438), `item_flush` (1442-1480), the `draw_mesh` routing
(3568-3903 - bracket rules, shared pointers, node claims, baked meshes,
swap keys, the Lara branch: the part nobody wants to reinvent),
`lara_flush`/`draw_hair` (1498-1675), `lara_load`/`lara_build_models`
(2386-2886), `hd_load` structure (1919-2256), `hd_draw_room`/`hd_draw_sky`
(2259-2385), `hd_pages.cpp` reading (50-195), the GLSL shaders
(135-236, 318-409) translated to HLSL. About 2,100 lines: ~1,100 near
verbatim, ~1,000 as structure over a new D3D9 base. `SHADER_RULES.md`
states the host shaders were written against that document only and
contain no Aspyr line - a GPL-to-GPL derivation is allowed.


## 2. D3D9 instead of OpenGL 3.3

- **Our renderer is fixed-function with pre-transformed vertices**
  (`FVF = XYZRHW|DIFFUSE|SPECULAR|TEX2`, `DrawPrimitiveUP`, no shader
  call anywhere, no d3dx). HD geometry needs a second draw path sharing
  only the Z buffer and back buffer.
- **The Z buffer matches exactly.** The classic path writes
  `sz = f_a + f_b_orig / z_view` (`polyinsert.cpp`, `3dmath.cpp:718-721`);
  that is precisely a D3D projection matrix (`_33 = f_a`,
  `_43 = f_b_orig`, `_34 = 1`, half-pixel offset). No depth conflict, no
  sorting heuristic. HD rooms and items draw immediately in `DrawRooms`;
  classic buckets and the sort list draw afterwards, Z-tested against
  them. The mid-frame Z clear (#127, `output.cpp:1493`) stays.
- **State bracket** around every HD block (shaders, declaration,
  streams, `D3DRS_CLIPPING = TRUE`) and an `HDRestoreStates()` back to
  FVF, clipping off, blend off, Z write on - the first source of
  garbage if forgotten.
- **BC7.** D3D9 has none. Measured per TR5 level: 95 (JOBY4) to 220
  (TITLE) pages, 349,588 bytes each (512x512, 6 mips). Options for the
  220-page case: A8R8G8B8 all mips = 293 MB VRAM, lossless; DXT5
  transcode = 73 MB with banding; DXT1 = 37 MB, visibly worse.
  **Recommendation: A8R8G8B8 in `D3DPOOL_DEFAULT` with a reused
  system-memory staging surface** - VRAM, not address space; device
  loss means re-reading (~2 s). DXT5 stays as a switch. Decode time is
  a measurement (77 M texels through `specific\bc7.cpp`, a thread pool
  over the cores expected to bring it under a second; the load bar
  already runs in its own thread). `BC7DecodeDDS` needs a mip parameter
  (~40 lines).
- **No texture arrays - and it does not matter, measured.** Over
  1,590,768 triangles in RICH1 and ANDREA1: mixed-page triangles 0;
  pages per room max 24/27, median 4/7; the index stream is not sorted
  by page (32,291 changes, 1,859 mergeable ranges after a stable bucket
  sort at load). Result: one D3D9 texture per page, one draw per (room,
  pass, page), ~7.6 draws per room, ~190 per frame typical, ~600 peak.
  No atlas, no bilinear bleeding, no UV rebasing.
- **Buffers.** Max 1,853,159 vertices (TITLE), 5,471,013 indices
  (ANDREA2). Vertex packed as on disk, 20 bytes (SHORT4 pos+sway, UBYTE4
  normal+layer, two D3DCOLOR carrying colour+u / colour+v): 37 MB VB,
  22 MB IB (32-bit indices), both DEFAULT|WRITEONLY. The byte normal is
  decoded as `(n-127)/128` (signed read costs a third of the page
  matches, `SHADER_RULES.md:82-87`).
- **Skinning.** 3 bones + 3 weights per vertex; palette 32 joints x 3
  vec4 = 96 constants; highest bone index over 554 readable TR5 TRMs is
  28. TROne's 32 `uBind` vectors fold into the matrices on the CPU
  (`M' = M * translate(-bind)`). **`vs_3_0`/`ps_3_0`** (GeForce 6, 2004).
  CPU skinning rejected: rooms need the GPU path anyway, two lighting
  models are worse than one.
- **Shaders without d3dx:** `fxc.exe` from the Windows SDK as a custom
  build step, bytecode embedded as `.inc` arrays. No runtime
  dependency.
- **Room shader:** `light = L * roomshade + C`, `colour = texel * light`
  (addition, not product - the mistake costs a factor 2.65); pass 1
  alpha-tested `clip(a - 0.5)`, pass 0 not (opaque tiles carry a gloss
  mask in alpha), pass 2 additive `float4(lit*a, a)` with ONE/ONE and no
  Z write. Plus the parity uniforms from finding 3: up to 8 dynamic
  lights (selection already made by `aBuildRoomletLights`), distance
  fog, shimmer via a 64-entry table, underwater wibble, fog bulbs.
  Estimated +150-200 HLSL lines. **TRM shader:** skinning + ambient cube
  + up to 4 point lights with `att = (1024/6)^2 / d^2`, capped at 1.
- **Groups and rooms.** Group i = room i, proven over all 15 levels
  (group count = room count + 1 sky dome, except JOBY3/JOBY5 = equal,
  TITLE + 2). Rule: ignore groups >= `number_rooms`. TR5 draws its sky
  procedurally, so the sky stays classic and HD rooms overdraw it.
  Flip rooms are their own indices with their own groups; portal
  culling stays entirely classic - the HD path gets a finished list of
  visible rooms.


## 3. x64 - required or a choice?

Peak for the worst level (ANDREA2/TITLE), option A: address space
~180 MB while loading (TRG file 56 MB, parsed vertices/indices 59 MB,
staging, TRMs, arena) and ~60 MB in play; VRAM ~380 MB. Far from 2 GB,
as long as the transient buffers live sequentially, not at once.

**x64 is not a prerequisite for #123. `LARGEADDRESSAWARE` is
recommended as belt, not braces:** a linker switch (off by default for
x86, unset in `tomb5.vcxproj`) plus a check of the two known signed
comparisons of pointers (`savegame.cpp` `creature->enemy < 0`,
`alexstuff.cpp` `(long)pR->door & 1`) among 59 pointer-to-long casts.
**x64 (#143) stays a separate undertaking:** `binkw32.dll` and
`discord-rpc.lib` are 32-bit, and the x64 configurations in the project
have no link dependencies. FMV on x64 would be TROne's road - Theora
from `5\FMV\HD\*.OGV` via libogg/libtheora - a project of its own.


## 4. Stages - each ends with a picture the user judges

- **S0 - loaders and foundation.** Vendor `trf_remaster\Trg.*`,
  `ItemModels.*` (without `bind_level_items`), the
  `find_install`/`load_object_map` parts of `Remaster.*`, `trf_core`
  file I/O - verbatim, MIT, acrostic headers untouched. Not `Dds.cpp`
  (we have `bc7.cpp`), not `trf_level`. New: `specific\hd\hd_install.cpp`
  (Steam discovery for IV-VI + override), `hd_bind.cpp` (slot -> TRM
  from `objects[]`, `<LEVEL>.MAP` and a name table; the MAP lists only
  deviations - ANDREA1.MAP has 41 lines - enemy slots resolve by slot
  name, `SWAT.TRM`, `HITMAN.TRM`, `DOG.TRM` all exist). Mip parameter in
  `bc7.cpp`. fxc step, LAA switch, the two signed-pointer checks.
  Result: a `-log` line "HD: ANDREA1.TRG v3 · 243 groups · 179 pages ·
  955148 vertices · 35 TRM bound · 0 missing". **2 tasks.**
- **S1 - rooms, static.** `hd_render.cpp` (device objects, VB/IB,
  declaration, page textures, state bracket, projection),
  `hd_pagecache.cpp` (BC7 over the mip chain, DEFAULT upload, thread
  pool, load bar steps), `hd_room.vs/.ps`, triangle sort by (group,
  pass, page), hook at `draw.cpp:1027`, fallback per room when the
  group is missing **or has zero indices**, baked statics suppressed by
  TROne's "double lantern" rule (`game_renderer.cpp:3722-3736`: a baked
  pointer passes only while exactly the moveable that claims it is
  streaming). Result: ANDREA1 in HD geometry with baked light - the
  biggest visual jump of the whole plan. **4 tasks.**
- **S2 - light and parity.** S2a: `hd.light_stage` and the sky rule.
  S2b: dynamic lights, fog bulbs, distance fog, shimmer, underwater
  wibble as shader constants - the formulas from `drawroom.cpp:305-685`,
  per pixel instead of per vertex. Optional beyond TROne: sample
  `Trg::light_cubes` at the object position for a real six-direction
  ambient cube. Result: a torch lights the HD wall again. **2 tasks.**
- **S3 - objects.** The bracket/routing block carried over, `_HEAD.TRM`
  rigid at the head node (measured, not hard-coded), HLSL skinning,
  bind folding, per-mesh fallback. Result: enemies, doors, switches,
  vehicles in HD; `hd.objects = false` returns to classic. **3 tasks.**
- **S4 - Lara.** `lara_load`/`lara_build_models`/`lara_flush`/
  `draw_hair` carried over; skin joints suppressed (`laraskin.cpp`,
  `output.cpp:1314-1416`) while the HD composite draws; braid from
  `HAIR_OG.TRM` (six bones = TR5's six links) with the straight links'
  matrices rebuilt from `hairs[0][n]`. TR3 outfits (#122): the wardrobe
  has `OUTFIT_TR3_ANTARC/NEVADA/COAST/CATSUIT/GYM` - proposed rule: a
  chosen TR3 outfit maps to its TRM, no match -> that Lara classic
  (the Jungle->COAST, London->CATSUIT mapping is a measurement by
  picture). Young Lara: `OUTFIT_YOUNG*.TRM`, `LARA_YOUNG_HEAD.TRM`,
  `HAIR_PIGTAIL.TRM` exist; TROne solved the twin-braid flush for TR4.
  Mirror Lara (`draw.cpp:983`): negative determinant flips winding;
  `CULL_NONE` makes it harmless, show it in the proof. **3 tasks
  (+0.5 young Lara).**
- **S5 - water.** TROne's `water_effect` is its own code (vertical bob,
  UV wobble, per-pixel caustics from world position); the water vertex
  marking is simpler here - straight from the roomlets'
  `nWaterVerts`/`nShoreVerts`. **1 task.**
- **S6 - budget, missing tiles.** `hd.texture_budget_mb`: over budget
  drops mip 0 (256 instead of 512) rather than pages;
  `hd.texture_format` rgba/dxt5; `fill_missing_tiles` -> `TEX\WHITE.DDS`
  (the Remaster's slot 0), not black. No real streaming - everything
  resolves at level load. **1.5 tasks.**
- **S7 - randomizer cases.** Cross enemies in vanilla slots resolve by
  slot name automatically. The new slots 461-470 need a hand table of
  nine names (`BLUE_GUARD2 -> BLUE_GUARD_JOBY`, `ROMAN_GOD2 ->
  ROMAN_GOD_ANDREA_HAMMER`, `CYBORG -> HITMAN`, the MESHSWAP_CROSS_*
  sources) - each a measurement by picture. Level-specific models
  imported from a donor (`ANIMATING10 = ANIMATING10_ANDREA1`) need the
  donor's `.MAP` - cleanest with a marker written by the randomizer
  (pattern: `data[2]` for foreign speech heads); without it, that slot
  classic. TR3 guests: no TRM -> classic, automatic.
  `Make_Room_Geometry` rooms (#137): the TRG has nothing there either
  -> zero indices -> classic, automatic. `clearWallFaces`/`makePortal`/
  `addViewWindow`: the HD art still shows the closed wall - the gravest
  case; either a "touched rooms" bit mask written by the randomizer
  (those rooms classic) or a heuristic on portal counts against a
  vanilla table. `moveRoom`: dx/dz move only the origin, TRG vertices
  are room-local and follow; dy moves the vertices -> that room
  classic. `setRoomWater`: flags only, read at run time. Moved statics:
  no randomizer function changes `MESH_INFO` positions (to be confirmed
  by the user). Level order: outfit hangs on the section and
  `gfFilenameWad` stays vanilla - the TRG name is always right.
  **2 tasks (+1 for the marker).**

  **S7-1 is built (the randomizer half). What it writes, and what S7-2
  therefore has to read:**

  1. **The outfit**, in `data[3]` of the level header - the block the
     engine already reads for `LaraDrawType`, `WeatherType` and the
     speech-head flag. Measured free first: all seventeen shipped `.trc`
     files carry 0 in `data[3]` through `data[15]`. The value is the
     engine's own `HD_LARA_OUTFIT_*`, so it goes into `HDLaraOutfitFor`'s
     `tr3Outfit` argument unchanged: 0 none, 5 `TR3_ANTARC`
     (`tr3_antarctica`), 6 `TR3_NEVADA` (`tr3_nevada`), 7 `TR3_COAST`
     (`tr3_jungle`), 8 `TR3_CATSUIT` (`tr3_london`). The table is
     `Patch::hdOutfitId` in `core\ApplyPatch.cpp`, and it is the only
     one - if the enum is renumbered, that function goes with it. An
     outfit with no HD twin gets 0 and stays classic.
  2. **The touched rooms and the donor slots**, in a sidecar
     `data\<LEVEL>.hdm` beside the `.trc`. They cannot go in the header:
     a room bit field needs up to 254 bits and a donor list is
     unbounded. Plain text, one fact per line, first line the version:

     ```
     hdm 1
     outfit 6
     room 34
     room 36
     donor 386 Andrea1 386
     donor 462 Andrea1 405
     ```

     `room` lines ascending, `donor` lines ascending by slot, so the same
     seed gives the same file byte for byte. A donor line names the slot
     in THIS level, the donor's level stem (or `TR3` for a guest out of
     Tomb Raider III - then the I-III Remaster has the model), and the
     slot it was taken FROM, which differs whenever a mesh store travels
     into an enemy's own slot. Written on EVERY run for EVERY level, even
     when there is nothing to say; a missing file means "no markers", so
     an empty file is what keeps an old one from going on talking.

     Recorded in `Tr5Level` itself, at the six functions that change a
     room's shape - `clearWallFaces`, `addViewWindow`, `makePortal`,
     `makeVerticalPortal`, `buildRoomGeometry`, `moveRoom` with `dy` -
     so no caller can forget one. `Make_Room_Water`, `Make_Climb`,
     triggers and items mark nothing, and `moveRoom` with `dx`/`dz` only
     marks nothing either.

     Two kinds of slot are deliberately left out of the file, and
     `Patch::writeHdMarkers` is the only place that decides it: Lara's
     own block (0..30), because her HD body comes from the outfit path
     and a donor line there would drop her back to classic exactly where
     `data[3]` had just got it right; and 460..470, because the engine's
     hand table above already names those, measured by picture.
  3. **Nothing else.** `hd_render.cpp` already falls back to classic for
     a room with an empty group, which covers `Make_Room_Geometry` by
     itself - the mark only says it out loud.

  The grammar and a reference reader (about forty lines) are
  `Tools\Diagnose\HdmDump.cpp`, which also prints `data[3]` with its
  meaning and holds the two halves against each other. **S7-2 owes:**
  reading both, rooms classic, the donor `.MAP` lookup, `data[3]` into
  `HDLaraOutfitFor`, `ROMAN_GOD` without a MAP entry, and the I-III
  Remaster for the `TR3` donors.

  **S7-2 IS BUILT.** All six of those, plus one thing the brief did not
  ask for and the spiel test found: the young-Lara test took the Irish
  LEVEL RANGE, and the engine's own body assembly takes the range AND
  the script flag, which the randomizer clears. Every randomized ANDY
  level was therefore drawn classic with a grown Lara in it. Details and
  the proof tables in `TOMB5 Code\MODIFICATIONS.md`, stage S7-2; the
  reader is `specific\hd\hd_markers.cpp` and the offline proof is
  `MapProbe -hdm`. Still open for a later round: the eleven hand-table
  pairings and the `ROMAN_GOD` default are measurements BY PICTURE, and
  the four Tomb Raider III guest names are an inference from TRForge's
  naming rule - nothing here has a Tomb Raider I-III installation to
  hold them against.
- **S8 - first person and binoculars.** First person does not hide
  Lara; the HD body may show inside the head -> zero `mesh_bits` like
  the binoculars do. The binoculars draw the real mesh in the classic
  sort path above HD rooms; the mid-frame Z clear must keep working.
  **1 task.**

  **S8 IS BUILT**, and zero `mesh_bits` was the wrong answer: it would
  take her arms away, which is the one thing #126 kept on purpose. The
  measurement says what really shows - the neck stump, which hangs on
  the TORSO node and survives the head mask, at 18.6 units from the eye
  - and the fix is two things on her body's draw alone: everything
  within a 60-unit RADIUS of the eye discarded in the shader, and her
  body drawn one-sided while the view runs. The second half was not in
  the brief and the pictures insisted on it: the HD path draws double
  sided, the classic engine culls in software, and this is the only
  camera that ever stands inside a body. A cut at fixed DEPTH was tried
  first and rejected by picture - it lies flat across her chest as soon
  as she looks down. The binoculars needed nothing at all: `mesh_bits =
  0` keeps `DrawLara` from being called, so the composite is never
  pushed. Details and the table in `TOMB5 Code\MODIFICATIONS.md`, stage
  S8; the proof is `ModelProbe -neck` and `-firstperson`. Left over: the
  diving helmet rides on the torso node and cannot be masked or cut.
  UPDATE, fix round J: the 60-unit sphere is GONE. It took the stump and
  sixty units of her chest with it, forced the cap of fix round I to be
  as wide as her whole torso and cut a jagged edge into her wrist. The
  cut is a CYLINDER about the neck axis now, in the bind pose, on the
  torso node alone - 30 units down it and 30 off it - which is the shape
  of the thing that has to go and which reaches no arm vertex of any
  outfit at any candidate size.


## 5. What TROne cannot do - and what of it hits us

Fog bulbs: TROne left them out; our engine has them working per vertex
with a user switch -> S2b, mandatory. Young Lara, credits, special
features, the 23 stubs: not our problem, our engine is the real port.
`phd_PutPolygonsSpcEnvmap`/`SpcXLU` (catsuit gloss, translucency):
TROne draws them plain; an HD model has neither - documented deviation
or an envmap term in the TRM shader. `hd.texture_budget_mb`,
`fill_missing_tiles`, `hd.objects` exist only in TROne's config template,
read nowhere. `no_hair`, `debug_flow`, `flow_effect`, `debug_full_cover`
from the user's screenshot do not exist in this TROne copy (vendored
TRForge stand `030ed51`, 2026-08-31) - the user's TROne is newer; take
the newer copy before carrying anything over, or solved problems get
ported twice.


## 6. Proofs per stage

S0: `TrgProbe` (groups/pages/vertices/ranges per level), `MapProbe`
(slot -> TRM with a "missing" list); `hd.enable = 0` -> frame
byte-identical to today (`DXSaveScreen`). S1: `TrgProbe --dump-room`
PNG of room UVs; ANDREA1 alley HD vs vanilla from the same viewpoint;
`HDLOG room=N group=N tris=… ranges=… fallback=0/1`. S2: a torch in a
dark room with and without S2b; `tomb5.fog`/`distance_fog` behave the
same in HD and classic rooms. S3: `TrmProbe` (model to OBJ/PNG); a
SWAT, a door, a switch; `hd.objects = 0` = today's picture. S4: Lara in
Rome/Russia/VCI, a TR3 outfit, the braid, a mirror room; `hd.lara = 0`.
S5: a JOBY water surface two frames apart. S6: TITLE with 220 pages
loads on a 2 GB card; log names budget and actual. S7: `SlotList`
extended for a randomized level; a run with cross enemies, a secret
list and a `Make_Room_Geometry` room; the log names every room kept
classic and why. S8: first person and binoculars in HD; one frame after
the binoculars, no mask residue. **The user's play test is the
acceptance of every stage.**


## 7. Effort, honestly

The information is complete: formats measured, render rules written in
TROne's own words, and - most important - the hard logic exists as
GPLv3 code we may take: the bracket rules for shared mesh pointers, the
swap-mesh rule, the Lara composite, the bind pose. That is the work
TROne spent weeks on with user reports ("double lantern", "Von Croy
without a head", "orange seams", "thick braid"). We do not repeat it.

What is not transferable is the renderer itself: OpenGL 3.3 with
texture arrays, VAOs, GLSL and runtime shader compilation against D3D9
with a fixed-function neighbour, vertex declarations, HLSL bytecode and
no BC7 - that is new. Plus the parity work of finding 3.

| stage | tasks | carried over (lines) | new (lines, estimated) |
|---|---|---|---|
| S0 loaders | 2 | ~800 verbatim | ~400 |
| S1 rooms | 4 | ~600 structure | ~900 |
| S2 light + parity | 2 | ~50 | ~350 |
| S3 objects | 3 | ~700 | ~400 |
| S4 Lara | 3 (+0.5) | ~700 | ~250 |
| S5 water | 1 | ~80 | ~80 |
| S6 budget | 1.5 | - | ~200 |
| S7 randomizer | 2 (+1) | ~50 | ~300 |
| S8 first person | 1 | - | ~100 |
| **total** | **~20** | **~2,980** | **~2,980** |

For scale: `specificfx.cpp` has 6,586 lines, `bc7.cpp` + `hdscreen.cpp`
1,106. About 3,000 new lines over 20 tasks is two to three of this
project's previous work blocks - not a weekend, not a research project.
After **S1 alone** (6 tasks, ~1,500 lines) the user sees HD rooms with
Remaster light: the lion's share of the visual gain.

Measurements, not numbers I have: BC7 decode time of `bc7.cpp` (decides
whether threading is mandatory; S0); draw calls and frame time in the
densest room (ANDREA2 has a room of 381,709 triangles; S1); whether
`r->y` is 0 for every TR5 room (decides whether the TRG y axis needs no
extra term; the 90.2 % vertex match says yes, the S1 picture says it
finally).


## 8. Licence and provenance

TROne (GPLv3): carried-over files keep the TROne author's copyright
notice plus our "Modified 2026" header; `THIRD-PARTY.md` gets a section,
`MODIFICATIONS.md` the change notice GPLv3 §5(a) asks for. TRForge
(MIT): verbatim, own `vendor\trforge\` with LICENSE and VERSION.txt,
acrostic header lines untouched. Inherited taboo list: no TRX code, no
TRMix/TR2X_alt, **nothing from tomb5.dll/tomb456.exe** (no code, no
tables, no disassembly, no Ghidra output), no leaked Core sources, no
verbatim Aspyr GLSL (our HLSL is written against `SHADER_RULES.md` and
TROne's own GLSL only), no TombEngine code. Asset rule as today: no
Remaster data in the repository or the package - not the TRG, TRM, DDS,
nor extracted name tables; the S7 hand table holds names only.


## 9. Decisions for the user

Before S0:
1. Where the HD data comes from: (a) Remaster install path from
   Steam discovery plus an override (recommended, nothing copied) or
   (b) a drop-in folder like `screens\HD` (consistent, but 428 MB of
   TRG plus the used pages to copy).
2. Texture format: A8R8G8B8 in the DEFAULT pool (293 MB VRAM, lossless,
   reload on device loss - recommended) or DXT5 (73 MB, banding) as the
   default; the other as a switch.
3. Set `LARGEADDRESSAWARE` now (one switch, two signed-pointer checks -
   recommended); x64 (#143) stays separate.
Before S2:
4. Parity work S2b right after S1, before S3 (recommended) - without it
   HD rooms look static: no torch light, no distance fog, no shimmer,
   no fog bulbs.
Before S4:
5. TR3 outfits: HD Lara from `OUTFIT_TR3_*` (proposed) or HD Lara off
   while a TR3 outfit is active?
6. Young Lara in HD (models exist) or classic as in TROne?
Before S7:
7. May the randomizer write markers into the level - which rooms a
   secret list changed geometrically, and from which donor a
   level-specific model came? Without markers only the conservative
   variant remains (affected rooms and slots classic, by heuristics).
8. Moved statics: confirm that no randomizer function moves
   `MESH_INFO` positions, or name the place.
In general:
9. Clarify the TROne stand: the newer copy with `flow_effect` etc.
   should be the source of the carry-over.
