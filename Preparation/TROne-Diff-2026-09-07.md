# TROne diff, stand 2026-09-06 (branch abnahme-D) against our copy of 2026-09-05

Information only. Nothing in this document has been built or adopted. Two
read-only surveys, both taken on 2026-09-07 against Todesengel's TROne tree
`Todesengel/Für neuen diff/TROne-abnahme-D` and our earlier copy
`Todesengel/TROne-main Aktuell`, and against our own tree at `hd-runtime`
commit 80df417. TROne's own state file says the surveyed branch carries
thirteen merged rounds with only one verifier verdict in, so every TROne
measurement below is a claim with a method, not an accepted result.

Licence frame for everything below: TROne is GPLv3 (Copyright 2026
Todesengel), `vendor/trforge` is MIT. Our tree is GPLv3. Adopting logic
from TROne means an attribution note in the file header and in
THIRD-PARTY.md, as `specific/hd/hd_object.h` already does. Nothing here
comes from the Remaster binaries; only shipped data files (`.MAP`, `.TRM`,
`.DDS`) were measured.

## Decisions this document leaves open

1. **Material 16.** Our reading: additive whole-surface sheet, weight in
   `param[2]`. TROne's reading: the still-water surface, `param[0..2]` =
   noise scale / distortion / time scale, 110 parts in 27 levels, never
   sharing a vertex with another material. Same numbers, incompatible
   meanings. The single most consequential disagreement; needs a
   measurement of our own before either side is trusted (see Part A, 4.2).
2. **Material 1 scroll scale.** TROne slows room flow by 0.12 but uses 1.0
   for the sky and argues against 0.12 in its own sky notes. Our 1:1 reading
   is cross-checked against RICH2's classic UV rotate and stays (Part A, 4.1).
3. **Pickups in HD.** Small round: a generic `EXTRA5_`/`EXTRA_`/`EXTRA4_`
   prefix step in `hd_bind.cpp` after the `.MAP` and the hand table; the
   hand table stays because it is the only way to the Joby Desert Eagle
   (Part B, 1). Spin is unmeasured on both sides.
4. **Inventory ring in HD.** Medium round: same object seam, four call sites
   in `DrawInventoryItemMe`, off-centre projection folded into the item
   matrix as a shear, ambient scalar light, node mask = `mesh_bits`
   (Part B, 2).
5. **Font in HD.** Medium round, lowest risk: `FONT0_OG.DDS` (256, B8G8R8A8)
   template-matched against the 106 `CharDef` rectangles, `FONT0.DDS`
   (2048, BC7) as a separate texture, one branch in `DrawChar`, alpha blend
   instead of alpha test, `nTextures-2` invariant untouched (Part B, 3).
6. **Small adoptions** worth a fix round on their own: sky-dome clamp
   sampler, `.MAP` DUMMY positive family list with MIP exemption, classic
   alpha test `ALPHAREF 0 / NOTEQUAL`, `kAttrUwDark` for TR5 underwater
   rooms (Part A, 3, items 1 to 5).
7. **Medium adoptions**: water table as a testable unit with its two
   deviations, per-vertex water bits by position match, ambient cube per
   floor square with the `x_size`/`y_size` swap trap, per-light falloff
   models, `LIGHT_SHADOW` spheres, torch on the HD room headroom-only, fog
   fact table, deferred room additive pass (Part A, 3, items 6 to 13).
8. **Large**: TROne's 60 fps is record-and-replay of the draw list with the
   camera delta recovered from a static room transform. Ours is
   interpolation of game state and was built independently; two ideas
   (room-transform delta, `set_room_frac` sub-unit precision) are portable
   on their own. Tile-hash HD for custom levels; head chosen per outfit
   bundle; braid anchor points and backpack drape sphere (Part A, 3, items
   14 to 17).

Two corrections to our own documents were made on 2026-09-07 together with
this file: `60FPS-Plan.md` section 0.5 (TROne now has 60 fps, our work
predates it and carries nothing over) and `MODIFICATIONS.md` fix round B
(material compare whole, superseded by fix round H).

---

# Part A: renderer, formats, licence

## Scope and method

Compared `C:\Cloude AI\Todesengel\TROne-main Aktuell\` (2026-09-05) against `C:\Cloude AI\Todesengel\Für neuen diff\TROne-abnahme-D\` (2026-09-06 23:34). Read `.claude\workflows\STAND-2026-09-06.md`, all of `docs\`, then `diff -rq`, then the large diffs. Nothing was written, built or committed. No binary from `tomb5.dll` / `tomb456.exe` / `tomb123.exe` was touched — only source and prose.

Headline: `host\render\game_renderer.cpp` grew from 4 782 to 11 270 lines; Host-API 10 → 20; `docs\SHADER_RULES.md` gained ~470 lines; six new docs; new host subsystems `host\photo\`, `host\mods\`, `host\custom\`, `host\script\`, `host\input\`. `vendor\trforge\` is byte-identical (no TRG/TRM format change).

---

# 1. What is new in TROne

## 1.1 The state file's own summary

`Für neuen diff\TROne-abnahme-D\.claude\workflows\STAND-2026-09-06.md` describes branch `abnahme-D` carrying 13 merge commits, only 1 of 13 verifier verdicts in: **Alpha, water+waves, fog, fountain basin, Z-buffer, light, photo mode, 60 fps, sky, HD text, voice, modding, custom levels**, Host-API v20, plus a prepared (not started) round E (mirrors, remaster water, TR5 fountain, atmosphere, 3D weather + footprints, braid shape/physics, cold breath, young Lara, Von Croy neck, TR1 fly, TR4 moreguns, weapon classes, projectile models, anim meshes HD, 3D pickups, coop).

**Caveat for you:** this tree is explicitly *unverified*. The STAND file says "Prüfung: 1 von 13 Urteilen da". Treat measurements as claims-with-method, not as accepted results.

## 1.2 Renderer / format facts (the part that matters to us)

### Materials TROne now reads out of `.TRG`

| material | TROne's reading | where |
|---|---|---|
| 1 | UV scroll ("flow"): fountains, waterfalls, canals, lava chutes; `param[0..1]` = velocity | `host\render\game_renderer.cpp:4532-4598`, shader `:371-386` |
| 8 | foliage cut-out; its **opaque** run is moved into the alpha pass | `:4946` (`alpha_tested_material`), `:4933-4960` |
| 16 | **still-water surface** (basins, canals, cisterns, lakes); `param[0..2]` = noise scale / distortion / time scale, `param[3]`=0 in all 110 parts | `:4600-4680`, shader `:478-510` |
| 22 | TR4/TR5 horizon panorama ring (one 4096×2048 page) | `:4991-4993`, `:5085-5152` |
| 0 | untouched — its DDS alpha is a sparkle/specular mask, discarding blanks floors | `:533-537`, `:847-854` |

Not implemented at all: **13 (flipbook), 14 (glass), 15 (haze), 2 (shafts/glow weights), 3/17 (sheen), 6/7 (cut-out and wind), bit 15 destructible statics.** TROne masks `material & 0xFF` everywhere, so a destructible copy (`0x80xx`) is classified by its low byte, but nothing is destructible.

### Sky (new)

`game_renderer.cpp:4976-5200`, `docs\SHADER_RULES.md` section "sky":
- Sky = every `.TRG` group with index ≥ classic room count (`:4568`, `:5028`).
- A part whose vertices are **degenerate** (all `colour1==colour2==0`, or all 255) is the painted picture/panorama and is drawn **unlit** (`light = 1.0`); parts with a real tint keep `light = c1 + c2` (`:5030-5068`, shader `:602-605`).
- TR4/TR5 panorama page is 4096×2048, cannot go into the uniform 512 array → own `GL_TEXTURE_2D`, `WRAP_S = REPEAT`, `WRAP_T = CLAMP_TO_EDGE` (`:5093-5152`).
- Dome tiling seam: half-texel clamp in the fragment (`:520-524`) is **not enough** — mip levels defeat it. They bind a separate **sampler object with `GL_CLAMP_TO_EDGE`** for the painted parts only (`:1256-1269`), leaving `GL_REPEAT` for the flow scroll.
- Sky scroll uses a **separate scale**: `kFlowScale = 0.12f` for rooms, `kSkyFlowScale = 1.0f` for the sky group (`:4555`, `:4566`).

### Water (rewritten, "water45")

`host\render\water_wibble.h:1-100` / `.cpp`:
- The wave is now the **measured D3D table**: `WaterTable[22][64]` rebuilt exactly like `init_water_table()`; `y += choppy[((wibble>>2)+rnd)&0x3F]`, colour `+= (shimmer + abs) << 3`.
- Water/shore classification is a **per-vertex bit** (`0x2000` water, `0x4000` shore), not a room flag, handed through `trone_room_vertex::attributes`; MeshEffect in bits 16-23 (`water_wibble.h:87-95`).
- Two documented deviations: `abs` read as **signed** (`water_wibble.h:56-63`), `choppy` **saturated** rather than wrapped (`water_wibble.cpp:63-71`).
- TR5 gets `kAttrUwDark = 0x0800` — every corner of an underwater room darkened by `-2*choppy` instead of TR4's shore shimmer (`game_renderer.cpp:359-364`).
- Their own per-pixel caustic survives but at **half strength (0.12 instead of 0.32)** and only on real surface pixels (`:606-616`).
- Underwater tint: `(0.5, 0.875, 1.0)` for the D3D family, `(0.55, 0.80, 0.92)` otherwise (`:648-652`).

### Fog (new)

`host\render\game_renderer_fog.inc:1-60` — measured, four branches: subtractive distance fog on the **light before texture modulate** (`c -= (z-S)*255/S`, black at `1.502*S`), D3D vertex fog `f = 1-(z-S)/4096`, **TR5 fog bulbs by ray/sphere chord**, and "not fogged": sprites, particles, fx, sky. Evaluated **per pixel** from `vViewPos` instead of per vertex, declared as an intentional improvement. Wired through `render->set_fog_model` / `render->set_fog_bulbs` (API v18).

### Light (Stage 2 completed)

`docs\SHADER_RULES.md` §12, `game_renderer.cpp:1164-1202`, shaders `:555-601` (room) and `:884-923` (objects):
- Ambient cube sampled **per object from the floor square** it stands on: `feld = basis[room] + sx*z_felder + sz`. The trap they document: classic `x_size` counts along world **Z** — modules must pass `(y_size, x_size)` (`register_room_sectors`).
- `hd.light_cube = 1` (default) uses the cube **for direction only**, divided by its own mean, so it scales the classic per-object shade and cannot overbrighten.
- `trone_light` now carries `r/g/b`, `radius` and a **falloff model** per light (`trone_light_model` 0..4: LINEAR / QUADRATIC / ROOM / REMASTER / SHADOW), because TR1..TR5 each attenuate differently.
- Contribution `colour * att * (31/255 + max(0, N·L))`, taken from tomb3 `S_CalculateLight`.
- New slot `render->set_room_lights` puts the torch/flare on the **HD room** too; room normal from `dFdx/dFdy` of the VIEW position; **headroom-only addition** `light += min(moving, max(0, 1-light))` so a saturated wall takes nothing.
- Regel 5 shadow maps are **not** done; instead the games' own `LIGHT_SHADOW` darkening spheres are implemented (`model == 4`, `:895-909`). Reason given: 63 of 118 `.TRG` carry no sun colour at all.

### Alpha (new rule)

`game_renderer.cpp:155-171` (classic) and `:533-537` / `:847-854` (HD/TRM): the D3D family sets `ALPHAREF 0` + `NOTEQUAL`, i.e. it discards exactly `a == 0`, and drawtype 1 keeps `SRCALPHA/INVSRCALPHA`. So the classic path now uses `uAlphaRef` (0.0) **plus** blend. HD path keeps a hard `a < 0.5` discard in pass 1 only. Registration-order fix: TR4 registers meshes before texinfos, so the pass split is redone in `level_end` (`resplit_mesh`, `:6689`, called `:8595`).

### Z-buffer / draw order

`game_renderer.cpp:1101-1127`: room-level **additive** pass deferred to the end of the 3D frame (after items and Lara), because the classic engines had a sorted poly list and the host was painting later rooms' opaque geometry over earlier rooms' shafts through portal openings. Switch `host.defer_transparent`.

### 60 fps (new — this is the big one for us)

`host\render\frame_interp.h:1-107`, `frame_interp.cpp`, `frame_record.h:1-78`:
- Logic stays 30 Hz. The render-table entries that draw are **recorded** (arguments only, no GL) and the recorded frame is **replayed N times** (`host.fps60_present`, default 2): once at `t = 0.5` blended against the previous tick, once at `t = 1` exactly as handed over.
- The camera delta is **recovered from a room transform**: a room is world-static, so `D = M_cur · M_prev⁻¹ = C_cur · C_prev⁻¹` — the room offset cancels. Previous transforms are re-expressed in the current camera frame (`P = D·M_prev`), so thresholds are meaningful; the camera is put back by `E(t) = blend(D⁻¹, I, t)`, giving exact endpoints at t=0 and t=1.
- Rotation = quaternion slerp, translation linear; sub-unit room precision preserved via `set_room_frac` × 256 (`frame_interp.h:52-58`).
- Jump thresholds `host.fps60_cam_jump 512 / _cam_turn 40 / _item_jump 512 / _item_turn 120` (`host\cfg\config.cpp:193-196`).
- Counter-proof switch `host.fps60_interp = false` keeps two presentations but blends nothing.
- Frames without 3D content (FMV, loading, stats) are replayed once.

### Lara / wardrobe

- **Head follows the outfit's bundle**, not the game: `lara_head_for_outfit()` (`game_renderer.cpp:5719`, used `:5928`). I–III outfits → `HEAD_IDLE` (whole skull incl. neck), IV–VI outfits → `LARA_HEAD` (face only, skull is on outfit bone 14), `OUTFIT_YOUNG*` → `LARA_YOUNG_HEAD`. This is the "back of the head missing" bug, measured over all 54 `OUTFIT_*.TRM`.
- **Partial-model guard** `lara_outfit_gap()` (`:5749`) rejects `OUTFIT_RUSSIA_EXTRA` (no legs) as a skin.
- **Braid**: anchor re-measured against the remaster models, not against the classic braid — target rest-pose point `(1,-238,-47)`, offset `(0,+9,-35)` from the head node for I–III; `(-3,-249,-50)` for IV–VI (`docs\PORTING_tr3.md`, "Zopf-Ansatz und Rucksack").
- **Backpack collision** is a measured *drape sphere* built from the outfit's own backpack box, exposed via `geom->lara_pack_sphere` (`game_renderer.cpp:5800` `measure_pack`, `:10840`); braid half-thickness taken from the hair model's root bead (OUTFIT_HAIR 14, HAIR_OG 10, HAIR_PIGTAIL 8), `bulge` 4 (`hd.hair_pack_bulge`).
- Hair joints come from the module's own per-node matrices (`draw_hair`, `:3237`); a second braid is flushed when a node index repeats (young Lara's pigtails).
- **Speech heads**: `register_model` flags **bit 2** marks a speech-head slot (TR4/TR5 slots 17..24) so the host routes the swap into the running bracket instead of painting a classic head over the HD one (`docs\HOST_API.md`, "Nachtrag der Sprachkopf-Runde"). No `.TRL` reading anywhere in the tree.

### `.MAP = DUMMY` (donor-map rule — strong candidate for us)

`docs\PORTING_tr5.md:780-880`, host side `game_renderer.cpp:1593` (`mesh_is_dummy_only`), `:1314`, `:4241`, `:4300-4371`:
`DUMMY.TRM` is a valid model with **no parts** — it is how the remaster says "this slot is switched off because I baked it into the room art". TROne now suppresses the classic draw for mesh pointers owned *exclusively* by such slots, with two measured safety rails: (a) a **positive family list** (`ANIMATING*`, `WATERFALL*`, `SEARCH_OBJECT*`, `SECURITY_SCREENS`, `MONITOR_SCREEN`, `COOKER_FLAME`, `STATUE_PLINTH`, `HORIZON`) because TR4 `ANGKOR1.MAP` sends `BRIDGE_FLAT` to DUMMY and that bridge is *not* in the `.TRG`; (b) `*_MIP` LOD slots are exempt. Switch `hd.map_dummy`. Counts: DUMMY appears TR1 10×, TR2 17×, TR3 14×, TR4 86×, TR5 97×.

### Custom levels: HD by tile hash

`host\custom\tile_hd.h:1-52` — a remastered HD texture is not a repainted *page*, it is one **64×64 cell** of a classic 256×256 page repainted 8× as a 512×512 DDS. Measured in `vendor\trforge\src\trf_remaster\HdPages.h`: of 17 164 HD tiles, **not one crosses a 64-pixel cell boundary**, and of 4 335 occupied cells not one wants two files. Three layers: retail tile cache (hash → DDS, written next to the exe), author package `customlevels\<pkg>\hd\`, classic upscale fallback. Hash is taken on the classic 5:5:5:1 form so host reader and module glue agree.

### Page wrap mode

`host\render\hd_pages.cpp`: room pages are now `GL_REPEAT` (needed for flow scroll), Lara/item pages stay `GL_CLAMP_TO_EDGE`, plus `upload_shared()` for the shared wardrobe folder.

### Non-rendering additions

Lua 5.4.4 scripting (`host\script\`, `docs\SCRIPTING.md`, `trone.enemies` / `trone.project` / net / HUD / ghost coop), photo mode (`host\photo\`, `docs\PHOTO.md`), mods & skins (`host\mods\`, `.trcs` classic-skin format, `docs\MODDING.md`), controller + modern control (`host\input\`, `docs\CONTROLS.md`), console polish + autocomplete + `weather` (`host\console\weather.cpp`), HD console font (`host\render\console_font_hi.inc`), voice tracks (`host\audio\voice_tracks.h`), cross-game weapons in all five games with classic art appended at runtime (`geom->append_texture_pages` / `append_object_textures`, API v13), movesets, localisation.

---

# 2. Overlap with our tree

Legend: **=** we have the same, **≠** we solved it differently, **–** we don't have it, **↑** TROne is better/more complete, **↓** we are ahead.

| Topic | Us | TROne | Verdict |
|---|---|---|---|
| TRG container / vertex 20 B / layer 1-based / normal bias 127 | same (`vendor\trforge` is our shared source) | unchanged | **=** |
| Material 1 UV scroll | `param.xy` **1:1 page units/s**, cross-checked against RICH2's classic `UVROT` (7.5 vs 4.0 tile heights/s) — `MODIFICATIONS.md:1979`, `:1985` | `param × 0.12` for rooms, `× 1.0` for sky (`:4555`, `:4566`) | **↓ us** — see §4 |
| Material 8 foliage | per-**page** cut-out measurement decides (hole-count on top mip), threshold from `param[0]` = 0.5 on mat 6 and 8 — `MODIFICATIONS.md:2539-2552` | material 8 only, hard `a < 0.5` — `:4946` | **↓ us** (we cover 6 and 7 too) |
| Material 7/8 wind (sway weight) | implemented, `sway` restricted to parts of material 1/7/8 (A6, `MODIFICATIONS.md:2585-2615`) | not implemented; only noted in comments | **↓ us** |
| Material 13 flipbook | implemented (R1, `MODIFICATIONS.md:3496`), `param[0..2]` = layers, `param[3]` = 0.15 s | **–** | **↓ us** |
| Material 14 glass | own blend phase, opacity from `param[1]`, no alpha test (H2, `MODIFICATIONS.md:4243-4270`) | **–** (glass falls under the hard 0.5 discard) | **↓ us** |
| Material 2/15/16 additive weights | per-material weight slot measured (2→`p1`, 14→`p1`, 15→`p2`, 16→`p2`) + `param[3]` breathing (`MODIFICATIONS.md:3169-3225`) | additive pass weighted only by `tex.a` (`:653-658`) | **↓ us** |
| Material 16 | "whole-surface sheets, the caustic net among them", additive weight `p2` | **still-water pool surface**, per-pixel UV ripple from `param[0..2]` | **conflict — §4** |
| Material 3/17 sheen | Blinn-Phong per side (`MODIFICATIONS.md:3460`, fix round D) | **–** | **↓ us** |
| Destructible statics (bit 15) | implemented, R4, instance counter in bits 8-14 (`MODIFICATIONS.md:3627-3680`) | **–**, only `& 0xFF` masking | **↓ us** |
| Sky group after rooms | same finding (band + cloud layers, pages 7000-7011, zero baked light → light 1) — `MODIFICATIONS.md:3234-3248` | same, plus TR1-TR3 dome case and the "all-255 degenerate" case | **= / ↑ TROne** on TR1-TR3 domes and the seam |
| Sky tile seam | not mentioned | half-texel clamp **+ CLAMP_TO_EDGE sampler object** (`:1256-1269`, `:5085-5152`) | **↑ TROne** |
| Flipmap | implemented, flip pairs recorded, restored on save load (`MODIFICATIONS.md:2519-2536`) | **–** (no flipmap anywhere in `host\`) | **↓ us** |
| Water surface | classic wave + brightness reproduced (S2), no second caustic | same `WaterTable`, but per-vertex water/shore bits `0x2000/0x4000` passed through the API, `kAttrUwDark` for TR5, caustic halved | **≈**, TROne's per-vertex classification is cleaner — §3 |
| "TRG has no water surfaces in many rooms, classic faces over it" | our finding | TROne matches HD vertices to classic animated-texture faces by room-local position (90.2 % verbatim) and to the water bits — `:4547-4554` | **↑ TROne** on the matching technique |
| Fog / TR5 bulbs | implemented, up to 6 bulbs/frame in the pixel shader, `c40` registers (`MODIFICATIONS.md:1212`, `:1606-1646`) | measured formulas in one includable `.inc`, up to N bulbs, per-pixel, chord clamped at eye and vertex | **≈**, TROne's `.inc` is better documented; we hit a shader-register ceiling they don't |
| Light cube per floor square | our HD path uses `aBuildRoomletLights` copy (`MODIFICATIONS.md:2620-2632`) | ambient cube sampled per object + `x_size/y_size` swap trap + direction-only default | **↑ TROne** on the cube; ours is ahead on dynamic room lights |
| Torch lighting the HD room | our A5 note says the seam is baked-art, not lighting | `set_room_lights` + headroom-only addition | **↑ TROne** |
| Non-euclidean rooms / portal window | Group M, portal window as scissor (`MODIFICATIONS.md:4961`) | **–** (only "defer transparent" ordering) | **↓ us** |
| Neck cut in FP / collar / cylinder neck | fix rounds J, K, L (`MODIFICATIONS.md:4526`, `:4652`, `:4812`) | **–** | **↓ us** |
| Braid | HAIR_OG 6 bones, roll around limb axis, 180° yaw flip from long differences; backpack box in the hair physics | anchor re-measured against `HEAD_IDLE`/`OUTFIT_*` band; **backpack drape sphere** fed to the module; hair joints come from the module's own node matrices, so no yaw reconstruction | **↑ TROne** on anchor + pack sphere; the yaw problem does not exist for them by construction |
| Lara head per outfit bundle | our wardrobe is `4\ITEM` + London rig for VCI | `lara_head_for_outfit()` picks the head from the **outfit's bundle** across all five games | **↑ TROne** if we ever allow I–III outfits |
| Actor faces | name rule + table, `.TRL` | `register_model` flags bit 2 = speech-head slot; no `.TRL` | **≠** — different mechanism, ours is data-driven |
| Donor `.MAP` = DUMMY | not covered in our docs | positive family list + MIP exemption | **↑ TROne** |
| 60 fps | interpolation, snapshot prev/cur, per-frame counters onto the tick, pose between ticks, VSync/refresh clock, particle trail | record-and-replay of the draw list, camera delta recovered from a room transform, slerp, 4 jump thresholds | **≠** — see §3; **our `60FPS-Plan.md:185-206` is now factually stale** |
| Custom levels HD | not covered | tile-hash (64×64 cell) three-layer resolution | **–**, **↑ TROne** |

---

# 3. What we could adopt (GPLv3, with attribution)

Everything below is GPL-3 TROne code by *Todesengel*; taking it means an attribution note and GPL-3 compatibility for our tree (our `HD-Runtime-Plan.md` §8 already anticipates this, and `MODIFICATIONS.md` already credits TROne's `water_effect` shape).

### Small (a day or less)

1. **Sky seam: CLAMP_TO_EDGE sampler object for the painted dome.**
   TROne: `game_renderer.cpp:1256-1269` + fragment clamp `:511-524`. Our counterpart: the sky band in room round 3a (`MODIFICATIONS.md:3234-3248`) — we set the sampler per draw range already, so this is one extra sampler state for the dome/panorama parts. Under D3D9 this is a `SetSamplerState(D3DSAMP_ADDRESSU/V, CLAMP)` around those calls. TROne measured a 13.6 → 2.8 luminance-dip improvement; worth checking whether we show the same hairlines on TR1-TR3 domes.

2. **`.MAP` `DUMMY` rule with the positive family list.**
   TROne: `docs\PORTING_tr5.md:780-880`, `game_renderer.cpp:1593`, `:4300-4371`. Our counterpart: whatever suppresses the classic draw when an HD model exists (fix round B seam, `MODIFICATIONS.md:1409`). The valuable part is not the code, it is the **measured list** and the two safety rails (BRIDGE_FLAT counter-example; `*_MIP` exemption). Cheap to port, removes double meshes (ANDREA1 lantern, WATERFALL1, shelves, roller door).

3. **Alpha threshold: `ALPHAREF 0 / NOTEQUAL` instead of `a < 0.5`.**
   TROne: `game_renderer.cpp:155-171`. Our counterpart: `MaterialAlphaRef` guard (`MODIFICATIONS.md:2889`) and the H2 glass finding. Their claim, worth verifying against our own data: TR1-TR4 texinfos carry **zero** soft alpha, so it is arithmetically identical there, and TR5's 8-bit alpha grates get a soft edge. Also fixes the classic TR4 sky being cut away by the 0.5 threshold.

4. **`kAttrUwDark` — TR5 darkens *every* corner of an underwater room by `-2 × choppy`** instead of running TR4's shore shimmer (`water_wibble.h:92-94`, shader `:359-364`). One line if our S2 parity code already has the table.

5. **Half-strength caustic confined to real surface pixels** (`game_renderer.cpp:606-616`) — we already refuse a second caustic; this is only a cross-check that our net is not on opaque floors.

### Medium (a few days)

6. **The measured water table as a standalone, testable unit.**
   TROne: `host\render\water_wibble.h` + `.cpp` + `tests\test_water_wibble.cpp` (values nailed down in a test). The two documented deviations — `abs` read **signed** (`water_wibble.h:56-63`) and `choppy` **saturated** (`water_wibble.cpp:63-71`) — are the interesting part; if our S2 wraps instead of saturating for schemes 9..20, our TR5 water looks different from the original. Cheap to check, and their unit test is directly reusable as a reference vector set.

7. **Per-vertex water/shore classification through the API (`0x2000` / `0x4000`, MeshEffect in bits 16-23) and the room-local position match to classic animated-texture faces.**
   TROne: `water_wibble.h:87-95`, `game_renderer.cpp:4547-4554`, second HD VBO filled at `level_end` (`:1220-1225`). Our counterpart: our own "TRG has no water surface in many rooms, classic faces on top" finding. Their 90.2 %-verbatim position match is a general tool — it is also how they tag lava and TR1-TR3 water. Would let us animate exactly the right HD triangles instead of guessing by room flag.

8. **Ambient cube sampled per floor square + the `x_size`/`y_size` trap.**
   TROne: `docs\SHADER_RULES.md` §12.1, `game_renderer.cpp:1164-1194`, `register_room_sectors`. The trap is the payoff: classic `ROOM_INFO.x_size` counts along world **Z**; passing it through unswapped reads the wrong (often all-zero) cube and silently falls back to grey. Their verification method — sum of squares over all rooms must equal the cube count in the file, 5 of 5 levels exact — is worth copying as an assertion. `hd.light_cube = 1` (direction only, divided by its own mean) is a genuinely good idea: it cannot overbrighten and keeps the game's own brightness logic 1:1.

9. **Per-light falloff model (`trone_light_model`).**
   TROne: `host\api\trone_host_api.h` (`TRONE_LIGHT_LINEAR/QUADRATIC/ROOM/REMASTER/SHADOW`), shader `:868-921`. Their measured table (TR1 `F²/(d²+F²)`; TR2 `1-d²/R²` dynamic, `F²/(d²+F²)` static; TR3 `1-d/R` with `R = falloff>>1`; TR4/TR5 `1-d/R` with `R = (falloff>>1)+(falloff>>3)`) is directly checkable against our own `lighting.cpp`. For a TR5-only port only the TR4/TR5 row matters, but the `31/255 + max(0, N·L)` split from tomb3's `S_CalculateLight` is a shape we may not have.

10. **`LIGHT_SHADOW` darkening spheres.**
    TROne: `docs\SHADER_RULES.md` §12.4, shader `:895-909`. The scale trap they document — `CreateLightList` stores `shadow << 3` and `SetupLight` reads `>> 3`, so the room record already holds the 0..255 strength — is exactly the kind of bug that produces "the shadow does nothing". Our counterpart is `aBuildRoomletLights`; check whether we carry `LIGHT_SHADOW` at all.

11. **Torch/flare on the HD room, headroom-only.**
    TROne: `render->set_room_lights` + `light += min(moving, max(0, 1-light))` (`:593-600`), face normal from `dFdx/dFdy` of the view position. Our A5 note explicitly declined to light HD rooms from dynamic lights; this is a cheaper answer than lighting classic rooms from the `.TRG` cubes.

12. **Fog as an includable, documented unit.**
    TROne: `host\render\game_renderer_fog.inc:1-60`. Not the code (HLSL vs GLSL) but the **fact table**: 128 = unity not 255; the subtraction goes on the light *before* modulate; sprites/particles/fx/sky are never fogged (`specular = 0xFF000000`, 115 sites in TOMB5); TR1-TR3 fog internally and must stay on mode 0. If our fog subtracts on the finished pixel or divides by 255, that is a one-line correctness fix.

13. **Deferred room additive pass.**
    TROne: `game_renderer.cpp:1101-1127`, switch `host.defer_transparent`. Our fix round E already reordered the frame (`MODIFICATIONS.md:3935`) — worth diffing the two orderings; TROne's specific claim is that a *later room's opaque* geometry paints over an *earlier room's* shafts through a portal opening, which is a symptom we may still have.

### Large (a week or more, and a design decision)

14. **60 fps by record-and-replay with camera-delta recovery.**
    TROne: `host\render\frame_interp.h:1-107` (the maths and its derivation), `frame_record.h:1-78` (which API entries are recorded and which are not). This is architecturally *different* from our snapshot approach and probably not worth swapping — but two ideas are portable independently:
    - **`D = M_cur · M_prev⁻¹` from any static room transform** to recover the camera delta and thereby distinguish a camera cut from an object teleport (`frame_interp.h:13-22`). Our plan derives `alpha` from the framecount parity and snaps on explicit rules (`60FPS-Plan.md:279`); the room-transform trick gives the same information without a snapshot of the camera.
    - **Sub-unit room precision through `set_room_frac` ×256** (`frame_interp.h:52-58`) — interpolating an integer room translation without it quantises to whole units.
    - Their counter-proof switch (`fps60_interp = false`: two presentations, no blending) is a clean A/B we should mirror.
    - **Action item regardless:** `Preparation\60FPS-Plan.md:185-206` ("TROne: no, it has no 60 FPS and nothing to carry over here") is now wrong and should be corrected, since it is our stated basis for *not* adding a GPL attribution for this work.

15. **Tile-hash HD for custom levels.**
    TROne: `host\custom\tile_hd.h:1-52`. The load-bearing measurement — HD art is per **64×64 cell**, no tile crosses a cell boundary in 17 164 tiles, no cell wants two files, hash taken on the classic 5:5:5:1 form so two different 5→8-bit wideners agree — is a format fact we do not have written down anywhere. Even if we never ship custom levels, this is the correct model of what an HD page *is*, and it explains why the `.TEX` indirection works.

16. **Lara head chosen by the outfit's bundle + partial-model guard.**
    TROne: `game_renderer.cpp:5719` / `:5749`, table of all 54 outfits in the `HOST_API.md` diff. Only relevant if we let I–III outfits into TR5; if we do, this is the difference between a working skin and a head with no back.

17. **Braid anchor + backpack drape sphere.**
    TROne: `docs\PORTING_tr3.md` "Zopf-Ansatz und Rucksack", `game_renderer.cpp:5800` (`measure_pack`), `:10840`. Two things we can use directly: (a) the **rest-pose target points** measured off `HEAD_IDLE`/`OUTFIT_*` (`(1,-238,-47)` for I–III, `(-3,-249,-50)` for IV–VI, against TOMB5's own `(-3,-251,-60)`); (b) the idea of deriving braid half-thickness from the hair model's own root bead (OUTFIT_HAIR 14, **HAIR_OG 10**, HAIR_PIGTAIL 8) instead of a constant. Also worth reading their debug-camera artefact note: a proof camera placed in `S_InitialisePolyList` put Lara's body on the *previous* frame's camera and the braid on the new one, which invalidated their earlier "braid inside the backpack" screenshots. If any of our braid evidence came from a debug camera, check where it is installed relative to `CalcLaraMatrices`.

---

# 4. Format facts where TROne contradicts our measurements

These are the places where the two trees read the same bytes differently. Each needs an adjudication before anything is adopted.

**4.1 Material 1 scroll scale — direct conflict.**
- Us: `param.xy` is **page units per second, 1:1**; cross-checked against RICH2, the one level where the classic `UVROT` and the remaster scroll sit on the same wall — classic 7.5 tile heights/s vs remaster 4.0, "same axis, same sign, same order of magnitude" (`MODIFICATIONS.md:1979-1987`).
- TROne: `kFlowScale = 0.12f` for room flow (`game_renderer.cpp:4555`) — an ~8× slowdown — while using `kSkyFlowScale = 1.0f` for the sky group (`:4566`), and their own sky section argues that going through 0.12 would be "five to fifteen times too slow to see" (`docs\SHADER_RULES.md`, sky Regel 3).
- **TROne is internally inconsistent here and our 1:1 reading is better evidenced.** Do not adopt the 0.12.

**4.2 Material 16 — same material, two incompatible meanings.**
- Us: "whole-surface sheets, the caustic net among them", classified with 2/14/15 as an **additive-weight** material, weight in `p2` (0.035..0.2); `p0`/`p1` "pair up with the page"; explicitly *not adopted*: `p1` (0.25, 0.7, 1.0) "looks like a speed but nothing says of what" (`MODIFICATIONS.md:3183`, `:3219`). Elsewhere we call mat 16 "light shafts" (`:2546`) and mat 16 in JOBY4 room 19 "the caustic net" (`:3157`).
- TROne: material 16 is the **still-water surface**, 110 parts in 27 levels across all five games, never sharing a vertex with any other material, `sway == 0` on all of them, `param[0..2]` = noise scale / distortion amount / time scale (`game_renderer.cpp:4600-4680`, `docs\SHADER_RULES.md` §12).
- Note the numbers agree even though the interpretations don't: TROne's ANDREA1 basin params `0.8 / 0.7 / 0.2 / 0` are exactly our JOBY5 material-16 tuple, and both of us measured `param[3] == 0` in every mat-16 part. Our "caustic net" in JOBY4 room 19 is plausibly TROne's pool surface seen from underneath.
- **Adjudication needed.** If TROne is right, our `p2` additive weight on material 16 is weighting a water surface as a glow, and their claim that `param[0..2]` come from the remaster's own uncompressed GLSL is checkable against our own material-2 weight evidence. This is the single most consequential disagreement in the two trees.

**4.3 Alpha-test material set.**
- Us: the alpha pass of all fifteen TR5 files carries materials 0, 6, 7, 8, 1, 22 and 14; cut-outs are 0/6/7/8 with the threshold in `param[0]` = 0.5 on **6 and 8**, and the decision is taken **per page** by counting texels below alpha 128, not per material (`MODIFICATIONS.md:2539-2552`, `:4252-4256`).
- TROne: `alpha_tested_material()` returns true for **material 8 only** (`game_renderer.cpp:4946`), threshold hard-coded 0.5, and mat 6/7 are never cut.
- **We are ahead**; do not narrow to their rule. Their measurement that mat-8 indices lie **entirely in the opaque run** (angkor1 2 511 159 opaque / 0 alpha, jungle 1 861 626 / 0, ANDREA1 558 942 / 0) is nonetheless a useful cross-check on our per-page decision.

**4.4 Material comparison: whole vs. low byte.**
- Us: originally compared `part.material` **whole**, on the reasoning that only low bytes 0, 3 and 14 ever appear with bit 15 (`MODIFICATIONS.md:2976-2980`) — then fix round H changed it to the low byte because a destructible copy matched nothing (`:4265-4269`).
- TROne: always `material & 0xFF` (`:4580`, `:4628`, `:4946`).
- **Agreed after our fix round H** — but note the two documents still contain our older, contradictory paragraph at `:2976`. Worth reconciling in our own doc.

**4.5 Alpha threshold semantics.**
- Us: `clip(texel.a - 0.5)`, plus a separate blend phase for material 14 with opacity from `param[1]`.
- TROne: the D3D family's own state is `ALPHAREF 0` + `NOTEQUAL` + blend on drawtype 1 — i.e. discard exactly `a == 0`, never `a < 0.5` (`game_renderer.cpp:155-171`).
- **Both can be right**: theirs is the classic renderer's state, ours is the HD path's. But if we also apply 0.5 in the *classic* path, TROne's measurement says we are cutting the TR4 sky and hardening TR5 grates.

**4.6 Sky vertex light.**
- Us: "its baked light is exactly zero on every vertex of every level" (`MODIFICATIONS.md:3236`).
- TROne: mostly zero, **but** TR2 `ICECAVE` and `FLOATING` domes and three TR5 `ANDREA1` layers are all-**255** in both triples (`game_renderer.cpp:4996-5002`), which through `c1+c2` gives 2.0, not 0. They therefore test for "degenerate" rather than "zero".
- **Not a contradiction for TR5 alone, but their rule is the safer one** if we ever cover TR1-TR3.

**4.7 The `x_size` / `y_size` swap.**
- TROne documents that classic `ROOM_INFO.x_size` counts squares along world **Z** and `y_size` along world **X**, and that reading them unswapped silently returns an unlit cube (TR1 LEVEL1: square 19 instead of 61, all 24 bytes zero → silent grey fallback) — `docs\SHADER_RULES.md` §12.1.
- We have no statement on this. If our per-square lookups (light cubes, sector probes) use the fields in the naive order, this is a silent wrong-answer bug, not a crash.

---

# 5. Licence / `LEGAL.md` changes

`diff docs\LEGAL.md` is 19 lines and contains exactly **one** substantive change:

- New vendor row: `vendor\lua\` — **Lua 5.4.4** (Lua.org / PUC-Rio), a verbatim copy of `src\` **minus** the two standalone mains `lua.c` / `luac.c` (TROne embeds the interpreter and builds no `lua.exe`), LF line endings as in the upstream tarball, **MIT**, with `LICENSE` beside it and the full text also at the end of `src\lua.h`.
- Added prose: Lua is MIT and therefore GPL-3 compatible; the script layer above it (`host\script\`) is TROne's own code under **GPL-3**, and the shipped example scripts (`scripts\*.lua`) likewise.

Unchanged: the project stays **GPL-3** (`COPYING` identical), `vendor\trforge\` MIT, `vendor\libogg` / `vendor\libtheora` Xiph-BSD, `host\external\stb_vorbis.c` used instead of libvorbis, and the standing rule that `tools\`, `tests\`, `docs\` are TROne's own GPL-3 text.

**Consequences for us:** nothing new. Adopting any of §3 remains a GPL-3 obligation with attribution to *Todesengel* / TROne; nothing in the D round adds a licence that would be incompatible with our tree, and the only new third-party dependency (Lua/MIT) is in a subsystem we would not take.

---

## Two things I would fix in our own docs regardless of adoption

1. `Preparation\60FPS-Plan.md:185-206` asserts, with citations, that TROne has no 60 fps and that therefore no GPL attribution is owed. That is no longer true as of 2026-09-06 (`host\render\frame_interp.*`, `frame_record.*`, `host.fps60*`). The citations themselves are still accurate — TROne's *logic* is still 30 Hz — but the conclusion needs rewording.
2. `TOMB5 Code\MODIFICATIONS.md:2976-2980` (material compared whole) and `:4265-4269` (material now read as its low byte) contradict each other; the later one is the built behaviour.


---

# Part B: pickups, inventory ring, font and HUD

# TROne vs. our TOMB5 HD path: pickups, inventory, font

All paths below are read-only findings. Nothing was changed.

Reference trees:
- TROne: `C:\Cloude AI\Todesengel\Für neuen diff\TROne-abnahme-D\`
- Ours: `C:\Cloude AI\TR Randomizer final steps\TOMB5 Code\TOMB5\`

Context from `.claude\workflows\STAND-2026-09-06.md`: this `abnahme-D` tree carries 13 unreviewed merge commits (incl. **HD-Text**), only 1 of 13 verdicts was in. A separate track **"Pickups-3D"** (`.claude\workflows\trone-E-arbeit.js:289-295`) is *prepared but not started*, so pickup spin/3D is explicitly still open on their side too.

---

## 1. Pickups in the level in HD

### Finding

TROne has **no hand table for pickups at all**. It has a *rule*, applied in two passes, plus the shipped `.MAP` files as the per-level table.

**Pass 1: the generic slot binding** (`vendor\trforge\src\trf_remaster\ItemModels.cpp:282-353`, `bind_level_items`). Per moveable slot of the loaded level, in this order:
1. the level's own `LEVELn.MAP` value for the slot's engine symbol (`ItemModels.cpp:320-332`),
2. the bare engine symbol with `ID_`/`O_` stripped (`:314-316`, `:333`),
3. the English slot words (`:337`), last resort.

Matching is "squashed": upper-cased, non-alphanumerics dropped (`:291-298`). The `.MAP` is a plain `KEY=VALUE`/CRLF text file (`vendor\trforge\src\trf_remaster\Remaster.h:126-133`, `load_object_map`); measured on the shipped data, e.g. `5\DATA\JOBY2.MAP` has `PUZZLE_ITEM1=PUZZLE_ITEM1_4`, `KEY_ITEM8=KEY_ITEM8_1`.

**Pass 2: the `EXTRA` prefix, and this is the piece we do not have** (`host\render\game_renderer.cpp:3847-3964`). For every slot *still unbound* after pass 1, TROne tries:
- `EXTRA<game>_<SYMBOL>` (i.e. `EXTRA5_...` when TR5 is running),
- `EXTRA_<SYMBOL>`,
- (TR2 only) an 11-entry spelling alias table, `game_renderer.cpp:3925-3945`.

`Effect` and `Lara` category slots are deliberately excluded (`:3893-3898`): `GUN_FLASH` is drawn *inside* Lara's mesh walk and an item bracket opened there would break her composite.

**The shared folder.** The models the whole bundle shares live in game "4" (IV-VI) / game "1" (I-III), not in `5\ITEM` (`game_renderer.cpp:3786-3811`); `read_item` looks in the running install first and in the shared folder second (`:3812-3833`). Their DDS pages likewise live only in the shared `TEX`, so the page array is uploaded with a fallback root: `hdpages::upload_shared` (`host\render\hd_pages.h:36-44`, called at `game_renderer.cpp:4218-4225`).

**`DUMMY`.** `.MAP` value `DUMMY` means "the remaster draws nothing here" (`game_renderer.cpp:4231-4243`), but TROne only obeys it for a *positive list* of room-decoration families (`:4267-4300`: `ANIMATING*`, `WATERFALL*`, `SEARCH_OBJECT*`, `SECURITY_SCREENS`, `MONITOR_SCREEN`, `COOKER_FLAME`, `STATUE_PLINTH`, `HORIZON`). `PICKUP_*`, `PUZZLE_*`, `DOOR_*`, `BRIDGE_*` keep their classic draw, measured after ANGKOR1's `BRIDGE_FLAT=DUMMY` deleted a walkable ledge. There is also a `_MIP` guard (`:4330-4360`).

**Level variants (Joby & co.).** Handled *only* through the `.MAP`. All 15 TR5 `.MAP` files were grepped: not one contains an `EXTRA*` line, and none names `REVOLVER_ITEM`. So TROne binds `REVOLVER_ITEM` to `5\ITEM\EXTRA5_REVOLVER_ITEM`, the TR4 six-shooter, and **never reaches `EXTRA5_REVOLVER_ITEM_JOBY`**. The `_JOBY`/`_RICH2` variant knowledge TROne *does* have is only for **speech heads / creature bodies**, where the name is progressively shortened by its last `_SEGMENT` with four measured brakes (`game_renderer.cpp:3996-4028`). That shortening runs for `<MODEL>_HEAD.TRM` lookups, not for item slot binding.

**Spin.** No, TROne does not rotate pickups. The host never touches rotation: `draw_mesh` converts the module's own classic matrix (`model_from_classic`) into the joint slot and the skinned model rides it (`game_renderer.cpp:9518-9537`, flush at `:3159-3218`). The ring/pickup-display spin `(GnFrameCounter & 0x7F) << 9` is the game's own, in `games\tr5\glue\hostglue_2d5.cpp:528-538` (`S_DrawPickup`), 1:1 from the original. World pickups therefore lie still in TROne exactly as classic. Making them spin like the remaster is precisely what the unstarted "Pickups-3D" track was written for (`trone-E-arbeit.js:291-293`, "gedreht wie im Remaster (Rotation um Y, gemessene Geschwindigkeit), Bodenabstand").

### The pickup slot to TRM table (measured against the installed bundle)

Resolved by TROne's rule (`EXTRA5_`, then `EXTRA_`, then plain name), across `5\ITEM` and `4\ITEM`:

| TR5 slot | TROne resolves to | note |
|---|---|---|
| `PISTOLS_ITEM` | `4\ITEM\EXTRA_PISTOLS_ITEM` | shared |
| `PISTOLS_AMMO_ITEM` | `4\ITEM\EXTRA_PISTOLS_AMMO_ITEM` | shared |
| `UZI_ITEM` | `4\ITEM\EXTRA_UZI_ITEM` | shared |
| `UZI_AMMO_ITEM` | `4\ITEM\EXTRA_UZI_AMMO_ITEM` | shared |
| `SHOTGUN_ITEM` | `4\ITEM\EXTRA_SHOTGUN_ITEM` | shared |
| `SHOTGUN_AMMO1_ITEM` | `4\ITEM\EXTRA_SHOTGUN_AMMO1_ITEM` | shared |
| `SHOTGUN_AMMO2_ITEM` | `4\ITEM\EXTRA_SHOTGUN_AMMO2_ITEM` | shared |
| `CROSSBOW_ITEM` (grapple) | `5\ITEM\EXTRA5_CROSSBOW_ITEM` | own game |
| `CROSSBOW_BOLT` | `5\ITEM\EXTRA5_CROSSBOW_BOLT` | own game |
| `HK_ITEM` | `5\ITEM\EXTRA5_HK_ITEM` | also `_RICH2`, `_RICH2_CUT` exist |
| `HK_AMMO_ITEM` | `5\ITEM\EXTRA5_HK_AMMO_ITEM` | |
| `REVOLVER_ITEM` | `5\ITEM\EXTRA5_REVOLVER_ITEM` | **six-shooter, not the Deagle** |
| `REVOLVER_AMMO_ITEM` | `5\ITEM\EXTRA5_REVOLVER_AMMO_ITEM` | |
| `BIGMEDI_ITEM` | `4\ITEM\EXTRA_BIGMEDI_ITEM` | shared |
| `SMALLMEDI_ITEM` | `4\ITEM\EXTRA_SMALLMEDI_ITEM` | shared |
| `LASERSIGHT_ITEM` | `4\ITEM\EXTRA_LASERSIGHT_ITEM` | shared |
| `BINOCULARS_ITEM` | `4\ITEM\EXTRA_BINOCULARS_ITEM` | `5\ITEM\EXTRA5_BINOCULARS_ITEM_RICH` also exists |
| `FLARE_ITEM` | `4\ITEM\EXTRA_FLARE_ITEM` | shared |
| `FLARE_INV_ITEM` | `4\ITEM\EXTRA_FLARE_INV_ITEM` | shared |
| `CROWBAR_ITEM` | `4\ITEM\EXTRA_CROWBAR_ITEM` | shared |
| `BURNING_TORCH_ITEM` | `4\ITEM\EXTRA_BURNING_TORCH_ITEM` | shared |
| `GUNSHELL` / `SHOTGUNSHELL` | `4\ITEM\EXTRA_GUNSHELL` / `EXTRA_SHOTGUNSHELL` | |
| `GUN_FLASH` | `4\ITEM\EXTRA_GUN_FLASH` exists | **skipped on purpose** (Effect category, `:3893-3898`) |
| `COMPASS_ITEM` | **nothing** | only `4\ITEM\EXTRA4_COMPASS_ITEM`; TROne running as game 5 never tries `EXTRA4_` |
| `CROSSBOW_AMMO1_ITEM` | **nothing** | only `4\ITEM\EXTRA4_CROSSBOW_AMMO1_ITEM` |
| `CROSSBOW_AMMO2_ITEM` | **nothing** | only `4\ITEM\EXTRA4_CROSSBOW_AMMO2_ITEM` |
| `SILENCER_ITEM` | **nothing** | no HD model in either folder |
| `MEMCARD_*` / `PC_*_INV_ITEM` | **nothing** | documented fallback, `docs\PORTING_tr5.md:2461-2464` |
| `PUZZLE_ITEM*` / `KEY_ITEM*` / `PICKUP_ITEM*` / `*_COMBO*` | only via `.MAP` | plain `PUZZLE_ITEM1.TRM` etc. **do not exist**; only numbered `PUZZLE_ITEM1_1..._9`, `KEY_ITEM7_1/_2`, `PICKUP_ITEM1_1..._5` |
| `BOTTLE`, `WET_CLOTH` | plain name hits | `5\ITEM\BOTTLE.TRM`, `WET_CLOTH.TRM` |
| `CLOTH`, `COSH`, `HAMMER_ITEM`, `EXAMINE1..3` | nothing (only `CLOTH_RICH2_RICH1`) | |

### What TROne has that our hand table does not

1. **The `EXTRA5_`/`EXTRA_` prefix rule as a generic pass.** We bind exactly two pickups by hand (`specific\hd\hd_bind.cpp:204-205`) and MODIFICATIONS.md:3006-3009 says outright the rest "is a round of its own". TROne gets ~22 pickup slots from one rule.
2. **The shared page fallback for those models' 8xxx pages** (`hd_pages.h:36-44`), though we already have the equivalent for free (see below).
3. **A `.MAP` `DUMMY` positive-list + `_MIP` guard** (`game_renderer.cpp:4267-4360`), which we do not have in that shape.

### What we have that TROne does not

- **The donor-map mechanism.** `HDBindDonorSlot` (`hd_bind.cpp:448-495`) reads the *donor level's* `.MAP` for a slot the randomizer imported, keyed off the `.hdm` sidecar. TROne has no equivalent: a moved `PUZZLE_ITEM1` in a level whose `.MAP` is silent goes classic there.
- **The hand table itself as an override when the `.MAP` is silent** (`hd_bind.cpp:339-388`). This is what gets us `EXTRA5_REVOLVER_ITEM_JOBY` (the Deagle), which TROne's rule structurally cannot reach. Our decision is documented in MODIFICATIONS.md:2988-3004 and is *better* than TROne's for the randomizer case.
- **`5\TEX` to `4\TEX` page fallback already built in** (`specific\hd\hd_pagecache.cpp:405-423`, "5, THEN 4, THEN NOTHING"), and **`5\ITEM` to `4\ITEM` model fallback** (`specific\hd\hd_model.cpp:216-290`). So `upload_shared` is already covered on our side.
- **Node mask = `item->mesh_bits`** already threaded through (`hd_object.cpp:1525`, `hd_model.h:345,439`), i.e. the `TRONE_LIGHT_EXACT_NODES` semantics without needing a flag.

### Effort for us: **small**

Add a third step to `bind_against` in `hd_bind.cpp:339-388`, after the `.MAP` and after the hand table, before the bare slot name: try `EXTRA5_<name>`, `EXTRA_<name>`, and, because TR5 slots inherited from TR4 exist only under `EXTRA4_`, `EXTRA4_<name>` (which fixes `COMPASS_ITEM` and both `CROSSBOW_AMMO*`, three slots TROne itself still loses). `find_model` (`hd_model.cpp:222-295`) and the page cache already resolve into `4\ITEM`/`4\TEX`, so nothing else changes. `GUN_FLASH` must stay excluded for the same reason TROne excludes it. Spin is a separate, larger question: nobody has measured the remaster's rotation speed yet.

---

## 2. Inventory in HD

### Finding: yes, and it works, through exactly the same seam as world objects, not a second render path

There is **no `DrawInventoryItem` hook and no new host API slot**. The ring goes:

`newinv2.cpp DrawThreeDeeObject2D` -> `DrawInventoryItemMe` -> `phd_PutPolygonsPickup` -> `host->render->draw_mesh`

The whole TR5 seam is `games\tr5\glue\hostglue_2d5.cpp:467-523`:
- the classic off-centre projection (`f_centerx/f_centery` moved to the ring's x/y) is turned into a **shear of the float matrix**: `fm[M00+i] = fm[M00+i]*scale + kx*fm[M20+i]`, same for row 1 (`:502-506`), linear, exact per vertex;
- `persp` is the *host's* projection constant `(w/2)/tan(fov/2)`, and `scale = f_mpersp/persp` corrects the classic 4:3-implied width so ring objects are not 4/3 too big on 16:9 (`:485-498`; this was the "diary drawn buggy" report);
- lighting collapses to the ambient scalar: `ctx.ambient = TroneAmbientFromRGB(color)` (`:478`), `pcbright` from the ring, one sun light, stage-1 model;
- **`ctx.flags = TRONE_LIGHT_EXACT_NODES`** (`:516-520`).

That last flag is the whole inventory-specific piece of the host. Semantics at `host\api\trone_host_api.h:154-169`: by default a node the item bracket never saw is treated as an accident and gets an invented matrix (Von Croy's nodes 7/18); the ring is the opposite, it *deliberately* leaves meshes out (`INVOBJ.meshbits`, the passport's pages, the globe's markers). With the bit set, `item_flush` draws **exactly the nodes that arrived**: `const uint32_t node_mask = exact ? g_item_seen : 0xFFFFFFFFu` (`game_renderer.cpp:3201-3210`), fed into `draw_skinned` and mirrored to the shader uniform `g_t_u_nodemask` (`:1329`). Bracket state at `:1336-1356`; the flag is latched in `draw_mesh` at `:9536-9537` and `:9586-9587`.

**Which models.** Level slots, not separate inventory slots. TR4/TR5's ring draws the *same* object slots the level loaded, so the models are the `EXTRA*_<SLOT>_ITEM` files of section 1. There is no `*_INV` naming in IV-VI. (TR1-III *do* have a separate `<SLOT>_OPTION` naming, handled by the same pass plus the TR2 alias table, `game_renderer.cpp:3925-3945`.)

**Matrices / light / background.**
- Matrices: the game's own `aMXPtr` float stack, sheared, converted to `trone_matrix` at `:508-514` (rotation x16384, translation truncated).
- Light: one ambient scalar; no room cube, no point lights in the ring.
- Background: `capture_backdrop` / `draw_backdrop` (`trone_host_api.h:612-617`), driven from `CreateMonoScreen`/`S_DisplayMonoScreen` (`hostglue_2d5.cpp:572-589`). Documented deviation: the host paints the frozen frame **untinted and not grey-scaled**, where TR5 originally grey-scaled it and tinted `0xFFFFFF80`.
- Depth: the z-buffer still holds the scene; accepted stage-1 deviation (`:462-466`).

**Injected cross-weapon ring rows** get a *named* model instead: `geom->register_item_model(slot, game, "NAME")` (API v16, `trone_host_api.h:478-494`), drawn **rigidly** with the same matrix copied into all 32 joints (`game_renderer.cpp:9383-9450`, note at `:9419-9428`: filling only joint 0 made both `MAGNUM_OPTION` models vanish, they carry nothing on bone 0). Important repair at `:9438-9448`: the branch must **fall through** when the model is absent, or the classic ring goes empty in classic mode.

**Proof they quote** (`docs\PORTING_tr5.md:2431-2464`): ANDREA3 + `moreguns`, ESC ring, log "17 extra item models bound - the inventory ring"; pistols, uzi, revolver, magazine in HD; `hd.enable=false` unchanged classic. `hd.debug_item=-2` measured slot 105 `seen=00000001` against 4 model nodes and slot 347 `seen=00000001` against 3, i.e. the mask really is needed. TR4's equivalent (`docs\PORTING_tr4.md:2120-2166`) has the sharper measurement: without the bit, slot 366 `SIXSHOOTER_ITEM`'s three undrawn nodes sat as a foreign lump inside the revolver's trigger guard, diff box 1872..1920 x 201..218.

### Our counterpart

Our HD object seam sits **only** in `game\draw.cpp:702` (`HDObjectBegin`), `:815/852/869/906` (`HDObjectNode`), `:924` (`HDObjectEnd`) and in `game\deltapak.cpp:3240-3315` for cutscene actors. `specific\hd\hd_laradraw.h:68-69` states it plainly: *"the inventory: newinv2.cpp draws its own ring of objects with DrawInventoryItem and never calls DrawLara."* Our ring is 100 % classic.

**But the machinery is already there**, and closer than it looks:

- `game\newinv2.cpp:943-1021` (`DrawInventoryItemMe`) is byte-for-byte the same node walk TROne's is, calling `phd_PutPolygonsPickup` (`specific\output.cpp:2230`) per node under `item->mesh_bits`.
- Our inventory loop calls `S_OutputPolyList()` at `game\newinv2.cpp:663`, and `S_OutputPolyList` **already calls `HDFrameDraw()` / `HDFrameDrawGlow()`** (`specific\output.cpp:1820, 1827`). So the HD bracket is entered every inventory frame today.
- `HDFrameDraw` explicitly tolerates zero rooms: `if (!g_frame_count && !objects_here) return;` (`specific\hd\hd_level.cpp:2220-2228`); a frame with objects and no rooms draws fine.
- The node mask is already `item->mesh_bits` (`hd_object.cpp:94, 1525`; `hd_model.h:345`), so `EXACT_NODES` needs no new concept.

**What would have to happen:**
1. An `HDInvBegin/HDInvNode/HDInvEnd` triple in `DrawInventoryItemMe` (or reuse `HDObject*` with a flag), matching the four `phd_PutPolygonsPickup` call sites at `newinv2.cpp:987, 992, 1016, 1021`.
2. **The off-centre problem.** `HDFrameDraw` builds *one* projection from `g_persp/g_centerx/g_centery` (`hd_level.cpp:2234-2235`), while the ring moves `f_centerx/f_centery` **per item** (`output.cpp:2268-2274`). TROne's answer is the shear at `hostglue_2d5.cpp:502-506`: fold `(x - centerx)/persp` and `(y - centery)/persp` into the item's world matrix rows instead of into the projection. That is the one non-obvious piece and it is worth copying the reasoning, not the code.
3. Light: `pcbright` to one ambient scalar. Note our `hd_object.cpp:434, 687, 731` already special-cases `InventoryActive` with a `sunScale` of 2.0 vs 0.75; someone already looked at this.
4. Model coverage: the ring's slots are the same level slots, so this rides entirely on section 1's binding fix. `MEMCARD_*`/`PC_*_INV_ITEM` have no `.TRM` at all and must stay classic (honest fallback, same as TROne).

### Effort for us: **medium**

The bracket, the four call sites and the light are small. The off-centre matrix and getting depth/ordering right against the classic ring polygons in the same frame is the real work. One concrete risk our tree has and TROne's does not: our ring presents at 30 while the HD path is per drawn frame. Worth checking that `HDObjectFrameReset` runs once per *drawn* inventory frame and not once per tick.

---

## 3. Font / HUD in HD

### Finding: yes, `FONT0.DDS` is used, and for TR5 it works essentially perfectly

The whole thing lives in the host, in the block marked `/* hdtext: */`, `host\render\game_renderer.cpp:7367-7870` + `:11211-11269`. **No module line and no host-API slot changed** for it.

**The files** (measured, `game_renderer.cpp:7369-7385`; headers verified on the installed bundle):
- `<bundle>\<n>\TEX\FONT0.DDS`: 2048x2048, DX10 header, DXGI format **98 = BC7_UNORM**, 6 mips, 5 591 188 B.
- `<bundle>\<n>\TEX\FONT0_OG.DDS`: 256x256, uncompressed **B8G8R8A8** (masks R `00FF0000`, G `0000FF00`, B `000000FF`, A `FF000000`; pitch 0x400), 262 272 B.
- Same pair for `FONT1`, and `FONT_JA*/CN*/KO*/TR3_*/TR6_*` (TR5 ships only `FONT0`/`FONT1`; the TR4 folder has the full set).

`FONT0_OG` and `FONT0` are the **same atlas**, cell for cell, at 256 and at 2048. Measured: the 166 HD ink boxes of TR5's `FONT0` sit a median 0...3 HD texels (< half a 256-texel) from their OG box x 8.

**It is NOT the classic font page.** The remaster re-typeset its atlas (rows in ASCII order); the classic page packs glyphs densely. Best overlap of a TR5 level page against `FONT0_OG` measured at **0.110**; a page swap is nonsense (`:7387-7393`).

**How the seam finds the glyphs anyway**, and it needs no glyph table from the remaster:
1. Every small registered sprite (2..32 px, >= 4 ink texels; `:7695-7725`) becomes a **template**: ink mask = alpha >= 96 (`:7447-7453`).
2. Each `FONT<n>_OG.DDS` is loaded, ink-masked, and given a summed-area table (`:7754-7771`).
3. `hdtext_find` (`:7587-7641`) slides the template over the atlas, pre-filtered by ink count in `[0.85*ink, 1.20*ink]`, scored **intersection over union**, budget-capped at 20 000 full comparisons, early-out at >= 0.999.
4. Accept threshold **0.94** (`:7795-7801`): "a similar character is the wrong character".
5. Sanity gate (`:7806-7839`): all hits must land on **one** classic page, that page must contribute **>= 24** hits **and >= half** its small sprites. Otherwise "no alphabet", log line, classic font.
6. The winning `_OG` path's sibling `FONT<n>.DDS` (the 2048) is uploaded as a 1-layer `GL_TEXTURE_2D_ARRAY`, natively BC7 when the driver has BPTC, CPU-decoded otherwise (`:7460-7564`).
7. `hdtext_arm` is idempotent and called from **both** `register_texture_pages` (`:8209-8211`) and `register_sprites` (`:8294-8299`) because modules report them in different orders.

**How a glyph is drawn** (`:11211-11269`): the *same classic UV arithmetic*, just relocated to the atlas hit: `u0 = (glyph.u0 - 0.5)/side`, `u1 = (glyph.u0 + w - 0.5)/side`, so the text stands on the same pixel at the same size, only sharp. One texel of margin is added on each side **to image and screen in the same ratio** (`:11249-11254`), so the slight HD overhang is not clipped and the glyph does not move or grow. `layer = -2.0` routes it to a second sampler; the UI fragment stage (`:734-757`) reads `sHdFont` with **LOD bias -0.7** and **alpha-blends** it; the classic `alpha < 0.5` discard would turn the antialiased edges back into stairs.

**How TR5's classic quads reach `draw_2d_sprite` at all**, the part directly transferable to us: `games\tr5\glue\hostglue_2d5.cpp:309-351` captures `CharDef`'s 106 glyph rectangles **before `InitFont` scales them in place** (static initializer, since `CharDef` is compile-time data). `games\tr5\glue\hostglue_file5.cpp:2548, 2597-2607` appends those 106 as extra host sprites behind the level's own and records `trone_tr5_glyph_sprite_base`. Then `AddQuadClippedSorted` (`hostglue_2d5.cpp:383-422`) recognises the glyph quad by `nPolyType == 4 && tex->tpage == nTextures-2`, matches `tex->u1 - 1/512` against `CharDef[i].u`, and issues `draw_2d_sprite(base + i, ...)`. The D3D blend `tex*color + specular` is folded per channel into one colour (`:364-381`).

**Status in TR5** (`docs\PORTING_tr5.md:2764-2772`): **running.** `FONT0.DDS` carries **88 of the 102** small sprites of the font page (ANDREA3 + title), **all 88 with score 1.000**: the remaster took TR5's glyph bitmaps over *pixel-identically* and only rearranged them. Menu, HUD lines, item names and subtitles all pass through `PrintString -> DrawChar -> AddQuadClippedSorted`. `FONT1_OG` (Cyrillic/Greek) is checked and correctly rejected: 22 hits, under half the page. (TR1/TR2 fail the gate honestly: their remaster ships a completely own typeface, `docs\PORTING_tr1.md:1754-1762`.)

Switches: `hd.text` (default true, hangs off `hd.enable`; `game_renderer.cpp:7937-7940`), `hd.debug_text` logs every template with its score and dumps the level pages as `hdtext_pages.raw`.

**HUD bars and PIX.** Bars are **not** HD: `hostglue_2d5.cpp:591-720` is `drawbars.cpp` 1:1, gouraud colour sets -> `TroneBarRect` -> one single `draw_2d_quad` call site (`:132`). No PIX art for bars. `PIX` is only used for full-screen backdrops: `display_picture` resolves `PIX\HD\<stem>.DDS`, then `PIX\<stem>.DDS`, then `PIX\OG\<stem>.DDS`, with `_EU/_US/_JA` flavours (`game_renderer.cpp:11110-11167`). HUD icons stay classic sprites.

### Our counterpart

Structurally we are in the *better* position, because we do not have the sprite-registration indirection: we own the draw call.

- `game\text.cpp:97`: `CHARDEF CharDef[106]`, the same table.
- `game\text.cpp:207-252`: `DrawChar` builds the quad, `u1 = def->u + 1/512`, `tpage = nTextures - 2`, `nPolyType = 4`, `AddQuadClippedSorted`. This one function is the entire seam.
- `game\text.cpp:496-506`: our existing `#116`-style glyph guard (MODIFICATIONS.md:854-857): CharDef has 106 entries plus accent codes 128..173, and the Japanese kana indices reach 181, so out-of-range glyphs are skipped rather than read past the table. That guard is *upstream* of any HD swap and stays as is.
- MODIFICATIONS.md:265-270, a hard constraint to respect: **every title page is addressed as a distance from the end of `Textures[]`** (`font = nTextures-2`, sky `-1`, logo `-5`/`-4`), so the page count must not change. An HD font must be a **separate texture**, never an added entry in `Textures[]`.
- We already have a BC7 decoder (`specific\bc7.cpp`, used at `hd_pagecache.cpp:581-589`, `hd_scan.cpp:160-167`) and a plain-DDS path, so both `FONT0.DDS` (BC7 2048) and `FONT0_OG.DDS` (uncompressed B8G8R8A8 256) are readable with what is in the tree.

**What would have to happen:**
1. One-time at level build: load `<install>\5\TEX\FONT0_OG.DDS`, ink-mask it, and template-match the **106 `CharDef` rects on page `nTextures-2`** against it. We do not need TROne's sprite scan or the "is this an alphabet" gate at all: we *know* which rects are glyphs. That removes the most fragile half of their design.
2. Store 106 `(u0, v0, ok)` hits. Given TR5 measured 1.000 on 88 of 102, expect near-total coverage; anything below ~0.94 falls back to classic per glyph.
3. Load `FONT0.DDS` (2048 BC7). D3D9 cannot take BC7 natively, so either decode to A8R8G8B8 (**16 MiB** VRAM) or re-encode through our existing `DXT5Encode` (`hd_dxt5.h:41-46`, **4 MiB**, measurable quality cost; DXT5 on antialiased glyph edges is the bad case for that codec; A8R8G8B8 is the honest choice for one texture).
4. In `DrawChar`: when the glyph has an HD hit and HD text is on, set the HD texture, use the relocated UVs, add the one-texel margin **to quad and UV in the same ratio** (`game_renderer.cpp:11249-11254` is the exact arithmetic), and draw **alpha-blended, no alpha test**, otherwise the soft edges become stairs. Everything else (position, size, `FontShades` colour + specular) stays classic.
5. Bars/icons: leave classic, same as TROne.

### Effort for us: **medium**, but the lowest-risk of the three

Roughly: a font loader + template matcher (~200 lines, no new dependency), 106 entries of state, and one branch in `DrawChar`. It is self-contained, needs no change to the HD object/room bracket, and degrades per glyph. The one thing to be careful about is the `nTextures-2` page-count invariant.

---

## License note (all three)

- **TROne is GPLv3**, Copyright 2026 Todesengel; `host\`, `games\`, `tools\`, `docs\` are all GPL-3 per `docs\LEGAL.md:9-24`.
- **`vendor\trforge\` is MIT**: that includes `ItemModels.cpp`/`.h` (the `.TRM` format description, `bind_level_items`) and `Remaster.h` (the `.MAP` format). MIT is the easier provenance for the format knowledge.
- Our tree is GPLv3 too, so carrying logic across is licence-compatible **provided attribution is kept**. Our existing practice is already correct and should be continued: `specific\hd\hd_object.h:1-9` names TROne, the copyright, GPLv3, the exact `game_renderer.cpp` line ranges, and `THIRD-PARTY.md`; `hd_bind.cpp:8-12` names TRForge/MIT for the `.MAP` format and states the reader was *written against the description rather than copied*. Do the same for anything taken from the `hdtext` block or from `hostglue_2d5.cpp`.
- **Nothing here touches `tomb5.dll`/`tomb456.exe`.** Everything above is either GPL/MIT source or measurements of shipped data files (`.MAP`, `.TRM`, `.DDS`); the `.MAP` files are plain text and the `.TRM`/`.DDS` layouts come from TRForge's MIT description, not from disassembly.
