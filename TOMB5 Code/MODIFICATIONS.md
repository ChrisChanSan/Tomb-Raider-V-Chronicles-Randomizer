# Modifications to TOMB5

**This is a modified version of TOMB5** (the PC Tomb Raider Chronicles
decompilation project by Troye/Trxyebeep and contributors,
<https://github.com/Trxyebeep/TOMB5>).

Modified between **2026-07-30 and 2026-08-30** (ongoing) for the
*TR5 Randomizer* project. The upstream code as we received it is preserved as
the **first commit of this repository** (no hash given here on purpose; it changes
whenever the history is rewritten for privacy scrubbing, the content does
not), so every change we made is visible as a diff against it.

Verified on 2026-08-13 against a freshly downloaded copy of the upstream
repository: 40 files were changed at that point, and of the 214 source files
we had not touched, 212 were byte-identical while the remaining two differed
only in line endings. The base is therefore exactly that upstream version.

Re-verified on **2026-08-24** against another fresh upstream download
(473 files): our baseline differs from current upstream in exactly three
files — one upstream commit made after our copy (`setup.cpp`: Two Gun gains
`undead = 1`) and two small local fixes that predate this repository and are
documented in their files (`dxsound.cpp` LOCSOFTWARE, `function_stubs.cpp`
DO_LOG). Measured again on **2026-08-30**: 107 files differ from upstream and
12 are entirely new, none was deleted - 119 in all. 117 of them carry the
modification notice at the top, which
`grep -rl "Modified 2026 by ChrisChanSan" TOMB5/` lists. The project file
`tomb5.vcxproj` carries the same notice as an XML comment.

Measured again on **2026-09-02**, after the optional HQ sound bank was
added: still 107 files changed, but **14** are entirely new - **121** in
all, 119 of them carrying the notice. The two additions are
`specific/hqsfx.cpp` and `specific/hqsfx.h`.

Exactly two of the 121 do not carry it, and neither should:
`tomb5/libs/stb/stb_vorbis.c` and `tomb5/libs/discordRPC/LICENSE` are other
people's files that we do not modify at all - stb_vorbis carries its own
licence at the end of the file, and the second one IS a licence. Putting our
notice on them would claim an authorship we do not have.

`game/trex.h`, `game/raptor.h` and `game/compy.h` carry the notice AND name
tomb3 as their origin further down (section 3 below); `game/newinv2.h` had
been missed entirely and was given its notice on 2026-08-30.

This modified version remains licensed under the **GNU General Public License
version 3**, the same licence as the original. See `LICENSE`. The complete
corresponding source is distributed alongside any binary we release.
Every file we changed under `TOMB5/` carries a modification notice at the
top, as required by
GPLv3 section 5(a).

Original credits are unchanged and remain with the upstream project: Troye
(main programmer, project leader), ChocolateFan, thatguyfrombb, Raildex. The
work below is layered on top of theirs.

---

## What was changed, and why

The modifications fall into two clearly separable groups. The first would be
useful to anyone who owns Tomb Raider Chronicles and has nothing to do with
randomizing; the second exists only to support the Randomizer.

### 1. Native Windows 11 port (no dgVoodoo2 wrapper)

The original renderer uses DirectDraw 4 and Direct3D 3 immediate mode. On
Windows 11 `IDirect3D3::CreateDevice` fails outright — for the hardware device
*and* for software emulation — so the game cannot create a rendering device at
all without a wrapper. These files were ported to Direct3D 9, following the
approach TR2Main used for Tomb Raider II:

| File | Change |
|---|---|
| `specific/dxshell.cpp`, `.h` | Device, surfaces, present, enumeration, cooperative level → D3D9 |
| `specific/output.cpp`, `.h` | Render/present path |
| `specific/function_table.cpp`, `.h` | One-time render state setup |
| `specific/texture.cpp`, `.h` | Texture upload → `CreateTexture`/`LockRect` |
| `specific/drawroom.cpp` | Bump-mapped room rendering |
| `specific/polyinsert.cpp` | `HWR_DrawSortList` submission only; the clip/subdivide maths is untouched |
| `specific/specificfx.cpp`, `drawbars.cpp` | Effects and HUD bars |
| `specific/3dmath.cpp`, `d3dmatrix.cpp` | Transform state constants |
| `specific/LoadSave.cpp`, `.h` | Save thumbnail capture — D3D9 back buffers are not directly lockable |
| `specific/fmv.cpp` | FMV drawn through a D3D9 texture instead of GDI `StretchBlt` |
| `specific/file.cpp` | One stray texture call, plus level-load bounds checks |
| `global/types.h`, `tomb5/pch.h` | The `DXPTR` struct and includes |

Audio was moved from DirectSound to XAudio2 in `specific/dxsound.cpp`; music
streaming in `specific/audio.cpp` was deliberately left on the original path.

**Optional HQ sounds** (`specific/hqsfx.cpp` and `.h`, both new). Chronicles
has no `MAIN.SFX` - that was TR4. From TR5 on, Core moved the samples into the
level files, so every `.TRC` carries its own bank of MS-ADPCM at 22050 Hz. The
TR4-6 Remaster went back to one global `MAIN.SFX` of plain PCM at 44100 Hz,
and that file is not the old material resampled: run a 22050 sample up to
44100 and back down again and almost nothing is left over, while the
remaster's samples keep about twenty times as much residue, and their spectrum
runs through 11025 Hz without the cliff a resampled signal always shows. If
the player puts that file into `audio\`, the engine takes the samples from it;
without it nothing changes at all.

The bridge between a level's own sample numbers and the bank's global ones
runs over the sound ids, which both sides index the same way. Only the audio
data is swapped - the level's own tables stay in charge, so the sounds the
randomizer imports from Tomb Raider III keep their original samples and
`MAX_SAMPLES` is not touched. Playback needed three changes in
`specific/dxsound.cpp`: a source sample rate per voice (checked, not assumed),
`DSAdjustPitch` reduced to the plain ratio it had always been computing, and
`MaxFrequencyRatio` lowered from 8.0 to 4.5 - XAudio2 caps
`MaxFrequencyRatio * source rate` at 200000, so at 8.0 no voice could ever
have been switched to 44100 Hz. Two unrelated holes were closed on the way:
`LoadSamples` read samples into a fixed buffer with no length check, and
`DXCreateSample` did not check its `malloc`. None of the audio ships - it is
the player's own data, exactly like the HQ music.

Also in this group:

- **Controller support.** `DXUpdateJoystick` was a stub returning zeros despite
  a complete remapping screen already existing in the game. It now polls XInput
  and falls back to DirectInput for controllers that are not XInput devices.
  `specific/input.cpp` gained bindable Pause/Save/Load and medipack slots and a
  fix to `Key()`, which shifted joystick button indices by the wrong amount and
  was therefore dead code.
- **Robustness fixes**, all the same bug class — fixed-size arrays filled from
  level-file counts with no bounds check: the savegame buffer
  (`game/savegame.cpp`, `.h`), the texture page array, `items[]`, spot cameras,
  animating-texture ranges, the sound buffer pool, `BuildOutsideTable`'s stack
  array in `game/setup.cpp`, and the per-frame queue in `game/items.cpp`.
  Upstream TR2's original 1996 code already had a guard in its equivalent of
  `WriteSG`; TR5 either never had one or lost it in decompilation.
- **`D3DCREATE_MULTITHREADED`** in device creation. `LoadTextures` runs on the
  background level-loading thread and calls `CreateTexture`/`LockRect` on the
  same device the main thread is presenting with. That is undefined behaviour
  per the D3D9 documentation and caused intermittent hangs during level loads.
- **Fullscreen** is now borderless windowed. Real exclusive fullscreen never
  recovered from device loss on the test system: `TestCooperativeLevel` stayed
  at `D3DERR_DEVICELOST` forever after alt-tabbing back in.
- **Joystick initialisation moved off the startup path** (`specific/dxshell.cpp`).
  `IDirectInput8::EnumDevices` can take ten seconds when a controller is paired
  over Bluetooth but attached over USB — DirectInput waits for the absent paired
  device. Measured and reproduced in a standalone program, so this is a
  DirectInput timeout rather than a fault in this code, but it left the game on
  a black screen, so the enumeration now runs on its own thread.
- **`Log()` gained millisecond timestamps and an `fflush`** in
  `specific/function_stubs.cpp`. Without the flush the log was routinely empty
  after a crash, which made it useless exactly when it was needed.

### 2. Randomizer-specific changes

- `game/gameflow.cpp` — a Level Select entry in the main menu, reusing the
  level-select screen the game already had behind `PlayAnyLevel`.
- `game/newinv2.cpp`, `game/savegame.cpp` — a TR2-style secret bonus.
- `game/setup.cpp` — one enemy's hit points raised as a balancing decision.
- `game/init.cpp`, `game/items.cpp` — headroom and guards for levels that are
  denser than the originals after randomization.
- `game/control.cpp`, `game/spotcam.cpp` — cutscene and flyby skip via
  controller.
- `tomb5/tomb5.cpp` — **Discord Rich Presence switched off** behind
  `TOMB5_DISCORD_RPC`. The application id in that call belongs to the upstream
  project and is not ours to use, and the bundled `discord-rpc.lib` is built
  against the dynamic C runtime, which prevented linking the executable
  statically. The code is left in place and one `#define` re-enables it.
- `tomb5.vcxproj` — output path, static C runtime (`/MT`) so the executable
  runs without the Visual C++ redistributable, and the Discord library removed.
- `game/laramisc.cpp` — the child-Lara torso swap in `LaraInitialiseMeshes` is
  now tied to `GF_YOUNGLARA` as well as to the level range. Upstream it depended
  on the level name alone, so clearing the flag in `SCRIPT.DAT` to play the
  Ireland levels with the adult Lara left her with the child's chest and a head
  floating above a torso that was too small. With the flag set — that is, with an
  unmodified `SCRIPT.DAT` — nothing changes.
- `game/hair.cpp` — the special braid anchoring for the Russia levels (torso
  sphere shifted by −11/−16/+25, fixed radius 94, upstream's own comment:
  "repositioned to avoid floating hair") now also engages when
  `LaraDrawType == LARA_BUNHEAD`. That anchoring belongs to the Russia OUTFIT
  (beanie, bomber jacket), not to the level — upstream could conflate the two
  because the outfit never leaves its section in vanilla. The randomizer marks
  the Russia outfit with LaraType 2 (`LARA_BUNHEAD`), a value no vanilla level
  file uses and no other code path branches on (the draw switch treats it like
  `LARA_NORMAL`). Without this, the braid pierces the body when the Russia
  outfit is worn elsewhere. The level half of the condition is additionally
  narrowed to `LARA_NORMAL`, so a foreign outfit brought INTO a Russia level
  gets its own standard anchoring there. Vanilla files never carry LaraType 2
  and vanilla Russia levels are `LARA_NORMAL`, so nothing changes for them.
- `game/laraskin.cpp` — the braid-root welding table is chosen per OUTFIT, not
  per level. `HairSkinVertNums[5]` lists the head-mesh vertex numbers of the
  Russia head (beanie), `[0]` those of the standard head; upstream selects `[5]`
  purely by level range. The selection now follows the same rule as the
  anchoring in `game/hair.cpp`: Russia level with `LARA_NORMAL` (unmodified
  vanilla file), or `LARA_BUNHEAD` anywhere. Without this, the braid root
  welds to the wrong head vertices whenever an outfit crosses sections — the
  visible result is the braid collapsing into a spike (the anchoring fix above
  is correct but was not sufficient on its own).
- `global/types.h`, `specific/drawlara.cpp` — new draw-type value
  `LARA_PONYTAIL` (LaraType byte 5 in the level file), drawn exactly like
  `LARA_NORMAL`. It exists for one case: the standard outfit worn IN a Russia
  level. There `LARA_NORMAL` would wrongly satisfy the "unmodified Russia
  level" half of the two conditions above, so the randomizer writes a value
  that is not `LARA_NORMAL` but draws like it. No vanilla file carries byte 5.
- `specific/drawlara.cpp` — `DrawLara__4` (catsuit) now draws the thigh
  holsters exactly like `DrawLara__1`, but only when the holster objects
  (slots 13–16) are actually loaded. Upstream never drew them on the catsuit —
  the catsuit levels do not load those objects, so there was nothing to draw.
  The randomizer can now copy them in, and then they should appear. The
  `loaded` guard keeps vanilla behaviour identical and also prevents drawing a
  stray mesh through `objects[].mesh_index == 0` — the exact artefact seen in
  `DrawLara__1` when holster objects are missing.
- `specific/drawroom.cpp` — the young-Lara left-hand mesh correction in
  `ProcessMeshData` (rewriting triangle 30 of `LARA_SKIN`'s left hand) is now
  tied to `GF_YOUNGLARA` as well as to the Ireland level range. Upstream keys it
  on the level alone, so playing those levels with the adult Lara (flag cleared
  in `SCRIPT.DAT`) silently bent one triangle of the adult hand. Same pattern
  and rationale as the `laramisc.cpp` torso fix below; with the flag set — an
  unmodified `SCRIPT.DAT` — nothing changes.
- `specific/file.cpp`, `specific/drawbars.cpp`/`.h`, `game/gameflow.cpp`,
  `game/control.cpp` — **two independent level-start freeze fixes**, found
  when randomized levels grew past anything the engine was ever tested
  with. (1) `aInit()` (room vertex buffers) and `aMakeCutsceneResident()`
  (cutscene textures) ran on the LOADING thread while the main thread
  presented the loading bar — the same two-threads-one-device class as the
  2026-07-31 loading hang; both now run on the main thread after the
  loader joins. (2) The loading bar's completion test
  (`loadbar_maxpos <= loadbar_steps` with steps growing by `100/maxpos`
  per step and `maxpos = texture pages + 20`) becomes mathematically
  unreachable at roughly 75+ texture pages, leaving the catch-up loop
  spinning forever with a full bar — unreachable in vanilla (9–17 pages),
  hit immediately by 79/82-page randomized levels. `S_FinishLoadBar()`
  forces the bar complete once the loader has joined, and the catch-up
  loop is bounded. Cheap seam logs remain around the load hand-off and
  the level-start milestones for future diagnosis.
- `game/objects.h`, `game/setup.cpp` — **fallback slots for the randomizer's
  cross-level enemy mesh swaps**. `MESHSWAP1` holds, per level, the swap
  meshes of that level's OWN enemies (people-family gun hands 10/13, Imp's
  alternate head 10, Two Gun's source mesh 2, Gladiator's and the Roman
  God's full damage sets); an imported enemy needs the set of a level that
  actually carries him, and that cannot be placed into `MESHSWAP1` without
  breaking the target's native users. The pool enemies' wiring in
  `BaddyObjects` now goes through `MeshSwapSource()`, which prefers a
  designated fallback slot and otherwise behaves exactly like upstream.
  The fallback ids are `SMASH_OBJECT5..8` (`MESHSWAP_CROSS_*` in
  objects.h) — loaded by no vanilla level and referenced by the engine
  only through `item->object_number` range checks, so without items of
  those types they are inert mesh warehouses; in unmodified levels the
  fallback is never loaded and behaviour is byte-identical to upstream.
  Grouping is by SWAP-set donor, which can differ from the model donor:
  rich1 (model source for the Guard and Two Gun) carries no `MESHSWAP1`
  at all — its enemies never set their swap bits in vanilla.
- `game/draw.cpp` — **distance models (MIP/LOD) switched off for enemies**.
  `DrawAnimatingItem` took the slot *after* the enemy blindly, as soon as
  anything at all was loaded there. That is safe in an original level,
  where level and enemies were built together — but the randomizer places
  enemies into levels whose MIP slot holds something entirely unrelated,
  and the enemy would then be drawn as that foreign model, using its own
  bones and frame. Measured across all 13 levels: 51 such combinations are
  reachable. What is lost is almost nothing — MIPs are barely used in this
  game. There are exactly seven genuine cases (SWAT, SWAT+, Guard and Two
  Gun in the three "rich" levels), all with the same mesh count as the
  full model, i.e. practically the same silhouette at five sectors'
  distance. `intelligent` separates cleanly: every enemy with `object_mip`
  sets it, doors and bridges (mip 2048–4096) do not and keep their
  distance models. This also removes the need to reserve a `_MIP` twin for
  new enemy slots — they are now added one per enemy.
- `game/pickup.cpp` — **keyhole state was lost across save/load** (an
  original-game bug, reproduced in unmodified Chronicles): inserting a key
  ran `item->flags &= IFL_TRIGGERED`, which clears *every* flag bit unless
  that one was already set — and a keyhole starts at flags 0 in the level
  file. The savegame writer only stores an item when one of
  `CODEBITS`/`INVISIBLE`/`TRIGGERED` is set (`savegame.cpp`), so the
  keyhole dropped out of the save entirely and came back as "unused",
  while the key itself was already gone (inventory *is* saved). The door
  could then never open, because `KeyTrigger` requires `ITEM_ACTIVE`.
  Saving during the key-insert animation and reloading reproduces it every
  time. This is the only `&=` of its kind in the engine — the other 26
  sites use `|=`, and the puzzle hole (which uses `|=`) survives save/load
  correctly for exactly this reason. Now assigns `IFL_TRIGGERED`, which
  keeps the original "clear the other bits" intent and only guarantees the
  bit is actually set.
- `game/objects.h`, `specific/file.cpp` — **the on-disk slot boundary is
  pinned to 460** (`SLOT_FILE_BOUNDARY`). The level file format encodes
  static-object sprite slots as `460 + static number`, 460 being the value
  `NUMBER_OBJECTS` had when the original levels were built; `LoadSprites`
  used the enum constant for that split, which silently breaks every
  existing level the moment new moveable slots are appended to the enum.
  The loader now uses the pinned constant, and `LoadObjects` skips (and
  logs) moveable records whose slot this build does not know instead of
  writing past the end of `objects[]`. With the enum unchanged both are
  no-ops: 460 == `NUMBER_OBJECTS`, and no vanilla level carries unknown
  slots. This is groundwork for engine-only extra slots (randomizer:
  additional enemy types beyond the original 460).
- `global/types.h`, `game/control.cpp`/`control.h`, `game/items.cpp` —
  **`MAX_ITEMS` doubled from 256 to 512**: the densest levels (rich2 at 248,
  joby3 at 244) left almost no room for the randomizer's extra enemies and
  scattered weapons. `items[]` is allocated from the `game_malloc` pool with
  this constant and the free list is parametric; triggers address items
  through a 10-bit field (`items[trigger & 0x3FF]`), so 512 stays fully
  reachable. The `ItemNewRooms` deferred queue (and its kill flag `0x8000`,
  which tolerates indices up to `0x7FFF`) now derives from `MAX_ITEMS` too,
  including the extern declaration in `control.h`. The savegame buffer grows
  to 28980 bytes because the per-item section scales with the item count —
  saves from older builds are cleanly rejected by the existing short-read
  detection, exactly as with the first buffer resize.
- `global/types.h`, `game/larafire.cpp` — **`MAX_LOT` raised from 5 to 16**:
  upstream allowed only five AI enemies to be active at once; the sixth
  triggered enemy evicted the one farthest from the camera (`EnableBaddieAI`
  freezes it as `ITEM_INVISIBLE`). With the randomizer placing 10–20 extra
  enemies per level, frozen bystanders were routine. The slot pool is fully
  parametric (`lot.cpp` allocates per slot `CREATURE_INFO` plus
  `num_boxes × BOX_NODE` from the `game_malloc` pool — roughly 25 KB per
  slot in the largest level; the pool itself is raised from 15 to 20 MB,
  not for the slots but because a randomized level carries the imported
  enemies' meshes, animations and frames and so runs several times the size
  of a vanilla one), savegames store creature state per item and
  re-acquire slots on load, so no compatibility impact. One genuine hidden
  assumption surfaced in the audit and is fixed alongside: Lara's target
  acquisition (`larafire.cpp`) collected up to `MAX_LOT` targets plus a
  terminator into fixed `TargetList[8]`/`LastTargets[8]` arrays — fine for 5
  slots, a buffer overflow from 8 active enemies up. Both arrays and their
  loop bounds now derive from the new `MAX_TARGETS (MAX_LOT + 1)`. The
  eviction path remains as fallback for the 17th enemy.
- `game/setup.cpp` — two hit-point balance changes for the randomizer's
  cross-level enemy pool, both explicit user calls from playtesting: the
  Maze Monster drops from 4000 to 120 (upstream built him effectively
  unkillable — in his home level he is a chase set piece, not a combat
  target; as a pool enemy he must die, and 120 keeps him the toughest
  regular), and Two Gun rises from 60 to 90 (24 in vanilla). Vanilla
  behaviour changes only in the sense that both are now reasonably
  killable/durable wherever they appear.
- `game/pierre.cpp` — Larson's corpse now fades out like every other enemy.
  When his death animation reaches its last frame the routine already probes
  his start sector for a heavy trigger; it now also sets `after_death = 1`,
  which is all the engine needs to fade the body and remove it (`draw.cpp:742`).
  Upstream never does this for him: his death animation carries no `KillItem`
  animation command — a dog's does — because the only place his body is ever
  removed is Trajan's Markets, where a cutscene takes over at exactly that frame
  and hides him. Everywhere else he never dies in the first place, since he
  flees below 40 hit points. Once an enemy pool can place him in any level that
  stops being true, and without this he would stand there dead for good: his
  death animation is a 207-frame set piece for the camera and ends upright, not
  fallen. Trajan's Markets is unaffected — the cutscene hides him in the same
  instant, so the fade never becomes visible.
- `game/deltapak.cpp`, `game/lara.cpp/.h`, `specific/file.cpp` — cutscene mesh
  swaps no longer outlive the cutscene, and dialog head swaps respect foreign
  outfits. Cutscenes graft vanilla-outfit meshes onto Lara at runtime:
  `handle_lara_chatting` swaps her head against `LARA_SPEECH_HEAD1-4` during
  dialog passages, the joby5 suit-exit swaps hips/legs. The swap-back only
  happens when the respective table runs past its terminator — skipping the
  cutscene mid-passage (or a passage reaching the final frame) left the last
  swap state installed into normal gameplay until the next weapon draw reset
  the head (seen in game: catsuit Lara wearing joby5's knitted beanie, and
  with the yellow suit legs). This is a Core bug that vanilla masks because
  there the grafted meshes match the level outfit. Fix in two parts:
  `CutseqLaraMeshesBack()` resets all 15 `lara.mesh_ptrs` from slot 0
  (primed with the current skin by `LaraInitialiseMeshes`, child torso from
  `ANIMATING6_MIP` as there) at the single spot in
  `handle_cutseq_triggering` where every cutscene teardown passes (right
  beside Core's own holster restore); and `handle_lara_chatting` skips the
  speech-head install when the new `GfForeignSpeechHeads` flag is set. That
  flag comes from `data[2]` of the 32-byte block before the main level chunk
  (`LoadLevel`, next to LaraType/WeatherType) — all 16 vanilla files carry 0
  there (measured), so unpatched levels behave byte-identically. The
  randomizer sets it when an outfit donor has no speech heads of its own
  (rich2/catsuit: the VCI levels know no dialog cutscenes) and otherwise
  imports the donor's speech heads so dialog mouths keep animating. Known
  corner: a weapon/torch held in hand across a cutscene is reset to the skin
  hand too; the weapon state machine reinstalls on the next draw.
- `game/control.cpp` — the cutscene skipper no longer fires during the
  cutscene START phase, and the controller skip (Cross) is edge-triggered.
  The skip check ran for any `cutseq_trig != 0`; entering a cutscene trigger
  while firing meant Cross (= the fire button on controllers) was still held
  when the pre-start fade finished, so `do_cutseq_skipper_stuff()` aborted
  the scene BEFORE `Load_and_Init_Cutseq` ever ran. The played-flag
  (`SetCutPlayed`, set inside `Load_and_Init_Cutseq`) was therefore never
  written, Lara still stood on the floor trigger, and the scene was
  re-requested the next frame — an endless approach/abort loop, Alt+F4
  required (seen in game). Now the skip requires `cutseq_trig == 2` (the
  actual playing state) and Cross must be freshly pressed (edge), so a
  fire button still held from gameplay never insta-skips the scene that
  just started; Escape keeps its original held behaviour.
- `game/pierre.cpp` — Larson/Pierre's 40-HP flee only engages when a
  matching escape marker actually exists. Their shared control clamps hit
  points at 40 and switches to `ai_bits = AMBUSH`, sending them to an
  `AIAmbush` object with matching OCB in the same zone, where they despawn
  (`reached_goal`). Core places that marker in every vanilla level that
  needs it; as pool enemies they can land in levels without one — then
  `FindAITargetObject` never finds a target, they chase Lara around
  "fleeing" forever, and the HP clamp makes them immortal (seen in game:
  joby2 item 36). The new `PierreCanFlee()` runs the same
  type/OCB/zone match as `FindAITargetObject` before the clamp: with a
  reachable marker everything behaves exactly as vanilla; without one the
  clamp is skipped and they fight to a normal death (both their death
  animations carry their own `KillItem` command — Pierre's natively,
  Larson's via the earlier after_death addition, so the corpses clean up).
- `game/pierre.cpp` — Pierre's death animation can no longer restart itself.
  His `+12` death animation jumps back into a *living* animation when it
  ends, because Core never uses his generic death: in vanilla he dies by
  cutscene. Outside that script the control routine saw a living state
  again the next tick and re-installed the death animation — an endless
  death loop (seen in game). The routine now holds the animation one frame
  short of its end and sets `after_death = 1`, so the body settles and
  fades like any other corpse. Vanilla is unaffected, since the loop can
  only be reached where a generic Pierre death happens at all.
- `game/sas.cpp` — the soldier family reads its animation set from its own
  object entry. `SasControl` picked the vault and special animations from a
  globally chosen base (SWAT's set if loaded, otherwise the Guard's), which
  is correct in vanilla because no level ever loads two members of the
  family at once. With a mixed enemy pool they do coexist, and the loser of
  that global pick indexed into the other one's animations. `index` now
  comes from `objects[items[item_number].object_number].anim_index`, which
  is the very set `InitialiseObjects` already aliased for that type
  (`setup.cpp:1052/1112-1115/1145-1148`) — identical in single-family
  levels, correct in mixed ones.
- `game/objects.h`, `game/setup.cpp`, `game/hitman.cpp`, `game/lot.cpp`,
  `game/balance.cpp`/`.h` — **the VCI cyborg as a pool enemy, on a slot of
  his own (`CYBORG`, 469)**. Slot 67 is not an enemy in the ordinary sense:
  Core marks it `undead`, so no weapon can reduce its hit points at all
  (`larafire.cpp` returns before the damage), and its only two deaths are
  Red Alert's own — the gas, keyed off a room flag that means something
  entirely different in the other fifteen levels, and the water short
  circuit, whose animation chain carries no `KillItem` at all. That last
  one is deliberate there: the electrocuted body has to stay lying in the
  corridor and keep the water lethal, which is the puzzle. Dropped into a
  random level the same three properties are a bug each — an enemy nothing
  can hurt, an enemy that dies of nothing, and a corpse that never fades
  and holds its AI slot for the rest of the level. So slot 67 is left
  byte-for-byte as it was and the pool gets a second slot with the same
  model and the same control routine: mortal, `save_mesh` for its eleven
  damage panels, its own hit-point and damage lines in `Balance.txt`, and a
  death of its own — the electrocution animations, all panels torn open,
  and after five seconds `CreatureDie` plus `after_death`, so the body
  fades and the slot is released. `HitmanControl` reached for
  `objects[HITMAN].anim_index` in twelve places and now reads
  `item->object_number`, the same correction the second Roman god needed
  (`romangod.cpp`, `#84`). Two further slots come with it:
  `MESHSWAP_CROSS_HITMAN` (468) as the cyborg's swap warehouse, and
  `MESHSWAP_CROSS_CRANEGUY` (470), which closes the last raw `MESHSWAP1`
  read left in the soldier family. Also: the cyborg is excluded from
  `GetCarriedItems`, so he never swallows a pickup lying beside him.
- `game/control.cpp` — **a reset enemy goes back to its own animation set.**
  `ResetGuards` (the body-bag trigger) wrote one globally chosen animation
  index into every thinking item in the level — SWAT's if loaded, else the
  mini sub's, else the Guard's. In the four vanilla levels that carry a body
  bag that is almost harmless, because what stands in them is soldier family
  and shares that set; with a mixed enemy pool it hands a hydra or a
  dinosaur an absolute animation number out of a foreign slot, i.e. the
  wrong frame data over the wrong bone tree. Each item now takes
  `objects[item->object_number].anim_index`. Two vanilla resets change, both
  of them broken before: in Andy2 no anchor is loaded at all, so the chain
  fell through and gave the skeletons animation 0 — *Lara's* first animation
  — and in joby3 Mafia 2, who carries 58 animations of his own, got the
  Guard's. The stub soldiers resolve to exactly the same number as before,
  because `setup.cpp` already aliases their `anim_index` onto that same
  anchor. Two Gun keeps his `+6` (his `+0` is a walk); the chef, the maze
  monster and the will-o'-wisp asked for their own set already and now need
  no branch. The forced rest state is left as Core wrote it.

- `game/balance.cpp`, `.h` (new) — a plain-text settings file, `Balance.txt`,
  read once at startup from the folder the game runs in. It carries an
  individual value for each of Lara's weapons, for the hit points of each
  enemy, for each separate enemy attack, and for each fixed level hazard, plus
  the audible range of the stationary level ambience and two percentages that
  scale all enemy values at once. If the file is absent the game writes it out
  with the original values and an explanation of every line, so it doubles as
  its own documentation; if a single line is absent or unreadable, that
  setting keeps its original value. Every value is clamped on read.
  One table in `balance.cpp` is the single source of truth: it holds the
  default, the allowed range, the explanatory text and — for hit points — the
  object slot. The template file is generated from that same table, so it
  cannot drift from the defaults the code actually uses.
  The wiring sits where the engine really decides these numbers rather than in
  one patched-over table, because three of the values in `weapons[9]` are dead:
  `game/setup.cpp` calls `BalanceSetWeapons` at level start (the point where
  vanilla already re-set the revolver's damage per level) and
  `BalanceSetEnemies` once in `InitialiseObjects`, after the three setup
  routines have written the originals. That function writes the per-enemy hit
  points and then applies the percentage to everything intelligent and
  damageable, so Lara and the flare keep theirs; it also logs a warning if a
  value in `setup.cpp` no longer matches the table, which is the one way the
  two could silently diverge. `game/lara1gun.cpp` takes the HK's two values,
  which vanilla rewrites on every shot. `game/sound.cpp` reads the ambience
  range that used to be a compile-time constant. `game/hitman.cpp`,
  `mafia2.cpp`, `pierre.cpp`, `sniper.cpp` and `sas.cpp` pass their own gunshot
  damage into `ShotLara` instead of a literal.
  The per-attack values are applied through `EnemyDamage()` at the 28 places
  in 20 enemy files that subtract from Lara's hit points, which is also where
  the global percentage is applied. Falls, fire, poison, drowning and the trap
  code in `traps.cpp`, `effect2.cpp`, `tomb4fx.cpp`, `control.cpp` and
  `camera.cpp` deliberately bypass all of it and stay exactly as hard as the
  original. Five fixed level hazards — the autogun, the gunship, the Iris, the
  Area 51 laser and the electrical cables in the submarine pen — are settable
  individually but are *not* enemies and are therefore left out of the
  percentage: they never move between levels, so scaling them with the enemy
  pool would be meaningless.
- `game/laramisc.cpp` - the DOZY fly cheat is now a TOGGLE. Vanilla only had a
  way in; the only way out was the Walk key inside `LaraCheat`, which nobody
  discovers on their own and which leaves a tester stuck in flight. The key
  combination now reacts to the rising EDGE and switches the mode off again if
  it is already on; the Walk exit stays. Both exits share one function,
  `LaraCheatEnd`, which also sets `current_anim_state`/`goal_anim_state` -
  upstream left them on the swim-cheat state for one frame.

- `game/balance.cpp` also carries a `WeaponDamage` percentage next to the two
  enemy ones, applied in `BalanceSetWeapons` and in `lara1gun.cpp` through
  `WeaponDamage()`, capped at 127 because the engine holds weapon damage in a
  `char`. It exists so the randomizer can offer a single slider for all of
  Lara's weapons without overwriting per-weapon values a player set by hand.

- `game/romangod.cpp` — the Roman God's armour used to burst at fixed hit
  point counts (225/150/75 out of 300). Those are now computed as three
  quarters, one half and one quarter of the object's own maximum, so the
  stages still line up when `Balance.txt` scales enemy hit points. With the
  original hit points the numbers are identical to vanilla.

- `specific/function_stubs.cpp`, `specific/winmain.cpp` — **`-log`**.
  Upstream compiled `Log()` out entirely, so a player who ran into
  something could never produce a log; the only way was a special build.
  `Log()` is compiled in here but returns on its first line unless the game
  was started with `-log`: without the switch nothing is opened, nothing is
  formatted and no `log.txt` appears. The switch is read by hand as the
  first thing `WinMain` does rather than through the `commandlines` table,
  because the run check and the gameflow load have already logged by the
  time that table is worked off — and those startup lines are the ones a
  diagnosis needs most. It is matched as a whole word, so a stray "log"
  inside a path cannot silently turn on per-frame logging.

- `game/draw.cpp`, `game/lot.cpp`, `game/sound.cpp`, `game/sound.h`,
  `specific/output.cpp` —
  diagnostic seams behind that switch: `SFXLOG` (which sample a sound
  really played, and which anim command asked for it), `CLIPLOG`,
  `SKIPLOG`, `MESHLOG`, `SLOTLOG` and `CLIPDETAIL` (why an enemy is not
  drawn — bounds check, status branch, slot allocation or mesh discard).
  They are throttled per item and per 30 frames, and the ones that have to
  compute something before they can log test the switch at the call site,
  not just inside `Log()`.

- `specific/file.cpp`, `game/door.cpp` — **a door wrote through the previous
  level's AI slots.** `FreeLevel` sets the `game_malloc` pool back to its
  start, which is the moment every global pointer into it goes stale: the
  address from the level before now points into the middle of the level
  about to be loaded. `baddie_slots` is read again before it is
  reallocated, because `InitialiseLOTarray` runs *after* `S_LoadLevelFile`
  while `ShutThatDoor` — called by `InitialiseDoor`, i.e. during loading —
  walks all `MAX_LOT` slots and writes to each. `FreeLevel` now clears the
  pointer where it dies, and `ShutThatDoor` only runs that loop while the
  array belongs to the current level; skipping it during loading loses
  nothing, since it drops the path target of creatures that are already
  thinking and at load time none are. Upstream bug, not one of ours, but it
  only became fatal with levels dense enough for the stray writes to hit
  live data.

- `specific/file.cpp` — `LoadItems` validates the slot and room number of
  every item as it reads them and stops with a message box instead of a
  wild access, in the same manner as the existing `MAX_ITEMS` check. Behind
  `-log` it also writes `ITEMLOG`, one line per item before its
  `initialise`, so a crash during loading names the item, and `ITEMWATCH`,
  a scan after each `initialise` that reports both an item whose room
  number became impossible and the item being initialised when it happened.
  The scan is quadratic and therefore gated at the call site, and it
  switches itself off after the first find.

- `specific/setupdlg.cpp`, `specific/registry.cpp`, `specific/dxshell.cpp`,
  `game/sound.cpp`, `tomb5.rc` — **the setup dialog now only offers what the
  engine still honours.** Three of its controls had quietly died with the
  port: the 3D device list, because D3D9 enumerates exactly one device per
  adapter; the texture format list, for the same reason; and the sound device
  list, whose selection was never read at all, since XAudio2 always plays
  through the system's default endpoint. A dialog offering a choice that is
  silently ignored is worse than one that does not offer it, so the two lists
  are gone and the device becomes a label naming the renderer.
  - The registry keeps five indices into those lists (`DD`, `D3D`, `VMode`,
    `TFormat`, `DS`) and `LoadTextures` walks three of them in one
    expression. Nothing checked them, although they can outlive the machine
    that wrote them — a saved `DD=2` reads past the adapter list once a
    monitor is unplugged, and any `TFormat` above 0 from an older DirectX 3
    build points past a one-element array. They are now clamped on read and
    written back as 0 where the control is gone.
  - `SOUND_Init` used to set `sound_active` unconditionally, so a machine
    without an audio device would have turned sound "on" over a null XAudio2
    and crashed on the first effect. It never showed because the dialog forced
    "Disable" whenever the DirectSound enumeration found nothing — a dead list
    holding up a live engine by accident. Sound is now switched on only by
    `DXDSCreate`, on success, and the voice creation checks the engine
    pointer.
  - With that, `DirectSoundEnumerate` was the last real DirectSound call, so
    **`Dsound.lib` leaves the linker line** (`dsound.h` stays for
    `DSBPLAY_LOOPING`). `Ddraw.lib` has to remain: the Bink FMV path still
    creates a DirectDraw surface.
  - The resolution list holds one entry per RESOLUTION now, not per mode.
    `EnumAdapterModes` reports each resolution once per refresh rate the
    monitor can drive it at, so a 165 Hz screen produced seven identical
    "2560x1440" lines - the dialog only ever printed width and height. They
    were seven names for one thing: the rate is never used, because device
    creation deliberately leaves `FullScreen_RefreshRateInHz` at 0 so the
    driver keeps the display at its current rate. (The original engine
    duplicated the same way for a different reason - its DirectDraw
    enumeration ran over several colour depths.) A saved `VMode` from before
    this therefore points at a different entry; it is clamped, and re-picking
    the resolution once settles it.
  - The corner of the dialog said "tomb5 V4.0"; it now carries the build date,
    the same stamp the log writes, so a screenshot and a `log.txt` can be
    matched up.

- `game/gameflow.h`, `game/gameflow.cpp`, `game/tower.cpp`, `game/objects.cpp`,
  `game/switch.cpp`, `game/footprnt.cpp`, `game/health.cpp`, `game/traps.cpp` — **level identity
  separated from level slot.** Upstream has one number, `gfCurrentLevel`, and
  uses it for two different questions: which entry of `SCRIPT.DAT` is being
  played (order, savegame, level names) and WHICH LEVEL this is (the places
  where the engine does something only Old Mill or only Red Alert needs). In
  vanilla the two are the same number. As soon as the randomizer shuffles the
  order they are not, and every such place fired in the wrong level - the
  Iris hazard, the gunship, the Area 51 laser, a shatter sound table indexed
  per level, footprints, the puzzle-hole special case. `gfLevelId` is the new
  identity, taken from the level's file name at load time; every level-specific
  branch now asks it, while `gfCurrentLevel` keeps its slot meaning.

- `game/chef.cpp`, `game/skeleton.cpp`, `game/swampy.cpp` — **three enemies
  that could not be killed.** All three have hit points and take damage, but
  their control functions never looked at `hit_points`, so there was no way out
  of their states. In vanilla it never shows: the chef's level takes Lara's
  weapons away beforehand, the swamp witch only wakes when the level is as good
  as over, and the skeletons' level hands Lara nothing to shoot them with. The
  randomizer scatters weapons and moves the items those assumptions rest on.
  The chef borrows the soldier family's gunshot death - his skeleton is
  bone-identical with theirs and his own death animation is a one-frame stub;
  the other two shred (see the next entry), having no death animation at all.

- `game/imp.cpp`, `game/willwisp.cpp` — **shredding death instead of a death
  animation.** The TR4 grenade-launcher death lives on in the code
  (`CreatureDie(item, 1)` -> `ExplodingDeath2`) and TR5 uses it only for the
  Roman god. The scraps are the enemy's OWN meshes thrown as `BODY_PART`
  effects, so no extra model is needed, and `CreatureDie` also does everything
  a death animation would: `KillItem`, dropped loot, AI off, the invisible flag
  for the savegame. For the maze monster it additionally replaces a stopgap -
  Core never finished that death, the lying pose sinks into the floor.

- `game/dog.cpp`, `game/huskie.cpp`, `game/lion.cpp`, `game/bat.cpp`,
  `game/spider.cpp`, `game/people.cpp`, `game/twogun.cpp`, `game/gladiatr.cpp`,
  `game/joby.cpp`, `game/hydra.cpp`, `game/minisub.cpp`, `game/autogun.cpp`,
  `game/tower.cpp`, `game/chef.cpp`, `game/skeleton.cpp`, `game/swampy.cpp` —
  **every damage figure and every enemy's hit points read from `Balance.txt`.**
  Upstream writes them as literals at the point of use. They are now named
  fields (`balance.dmgDogBite`, `balance.dmgHydraBeam`, …) fed by the new
  `game/balance.cpp`/`game/balance.h`, which reads a plain text file beside the executable,
  writes a commented template when it is missing and fills in settings a newer
  version has added. Three percentage lines scale everything at once. With no
  file present, the values are this build's defaults.

- `game/box.cpp` — **a dropped wall pickup becomes an ordinary one.** OCB 2 and
  9 mark the pickups Lara pulls off a wall; the manner demands a wall behind
  the item and her angle onto it. When an enemy carries such an item and dies,
  `DropBaddyPickups` puts it on open floor, where that condition can never be
  met again - the item is lost for good (seen in the randomizer: an Imp carried
  a crowbar away). The low OCB bits are cleared on the drop, so it lands as a
  normal floor pickup. The pickup trigger's own bits are untouched.

- `game/moveblok.cpp` — **a pushable block may roll over pickups.** TR5 refuses
  the push as soon as `GetCollidedObjects` reports anything at all on the target
  tile, a medipack included. TR1-4 had no such general test and the block simply
  rolled over pickups. With items relocated, a stray medipack could block the
  only push path to a puzzle. The test now asks what is in the way: pickups let
  the block through and are there again once it moves on; enemies, statues and a
  second block still stop it.

- `specific/gamemain.cpp`, `specific/gamemain.h`, `specific/LoadSave.h`, `game/savegame.h` —
  **savegames live in a `savegame\` subfolder**, and any `savegame.N` still
  lying loose in the game folder is moved there once at startup (never
  overwriting). Two guards came with it: the number of bytes the read actually
  returned is kept, so a save written before the struct grew is recognised
  before it is applied, and **the checksum is computed the moment the file is
  read** instead of after the fade and level reload. The game loop keeps running
  behind the menu and increments the level timer, so a load during Lara's death
  animation failed with "corrupted savegame" on a file that was provably read in
  full - and worked after a restart, when no frames lay in between.

- `tomb5/troyestuff.cpp`, `tomb5/troyestuff.h`, `specific/input.h`, `specific/dxshell.h`,
  `specific/output.h`, `specific/texture.h`, `specific/drawbars.h`,
  `specific/function_table.h`, `specific/dxsound.h`, `specific/audio.h`,
  `game/camera.h`, `game/lara.h`, `game/laramisc.h`, `resource.h` — **the
  in-game option pages.** The tomb5 options grew a second page and, when the
  first-person view is switched on, a third one for it alone; a page only joins
  the cycle when it has something to say. The rows use stepped values rather
  than free sliders where a step is plainly visible, the way the sound quality
  row already behaved. The joystick pages carry the vocabulary of the active
  input path, so a controller is described the way it actually reports itself.
  The headers carry the declarations these pages need.

- `specific/alexstuff.cpp`, `specific/alexstuff.h`, `tomb5/troyestuff.cpp` — **a credits screen.**
  GPLv3 courtesy and a project requirement: the names without which this would
  not exist belong in the game, not only in a readme. It is a screen of its
  own, deliberately not a page of the options - as a page it turned up unasked
  while paging through the settings, and the paging arrows are drawn at a fixed
  row that landed across a line of names. Groups are separated by colour rather
  than blank lines, because the screen holds fifteen rows and four separators
  would push the tool credits off it. The running state moved out of a function
  static so an abort lets the next roll start from the bottom again.

- `game/text.cpp` — **a glyph outside the font is skipped instead of read past
  the table.** `CharDef` holds 106 entries plus the accent codes 128..173;
  everything beyond that - the kana indices of the Japanese PSX language file
  reach to 181 - indexed the table unchecked. What the font can show is shown.

- `game/effects.cpp` — the second guard slot (see the `#105` entry above) is
  the last anchor of the same alias chain used elsewhere, so an enemy woken by
  a flipeffect finds its animation set too. In the same file, the ambient
  scenery sound sources are marked as such while they are created: the range
  brake and the stacking guard in `sound.cpp` hang on that mark.

- `game/delstuff.cpp`, `game/delstuff.h` — `SkinJointUsable` records, per joint, whether the
  seam pairing found anything to sew. A joint that matched no vertex at all
  cannot be computed, and drawing it anyway can only put a lump of geometry
  somewhere it does not belong; the three joint loops in `specific/drawlara.cpp`
  ask this before they draw.

- `specific/function_stubs.h` — `gLogEnabled`. `Log()` writes nothing unless
  the game was started with `-log`. Diagnostic seams that have to build
  something before they can log - walking a list, formatting a name - test it
  themselves at the call site, because the gate inside `Log()` only saves the
  writing, not the preparing.

- `game/secretreward.cpp`, `game/secretreward.h` — what finding every secret in a level awards is
  read from `SecretRewards.txt` beside the executable, keyed by level position
  rather than by file name, because the level order is itself rolled. The
  randomizer can write that file; a player can edit it. Only weapons,
  ammunition and medipacks are allowed as rewards - handing out a key or a
  puzzle item would open a door out of order.

- `specific/audio.cpp`, `tomb5/libs/stb/stb_vorbis.c` — **HQ soundtracks in
  OGG.** If a file named after the track number lies in `audio\HQ`, it is
  played instead of the original; without those files nothing changes. The
  decoder is **stb_vorbis by Sean Barrett**, vendored unchanged and in the
  public domain (its own header carries the notice), so it adds no license
  obligation of its own. No music is shipped with this project - the player
  supplies their own files.

### 3. Code taken from tomb3 (four Tomb Raider III enemies)

Four enemies from Tomb Raider III can appear in randomized levels: the
T-Rex, the raptor, the Compsognathus and the vulture. **Their control code
is not ours.** It comes from **tomb3**, Troye's decompilation of Tomb
Raider III, <https://github.com/Trxyebeep/tomb3>, and is under the GNU GPL
v3 — the same license as TOMB5 and as this project, which is what makes
combining them possible in the first place.

- `game/trex.cpp`, `game/trex.h`, `game/raptor.cpp`, `game/raptor.h`,
  `game/compy.cpp`, `game/compy.h` — new
  files, taken
  over from tomb3's files of the same name. The code was deliberately
  **taken over directly rather than rewritten**: a rewrite would obscure
  where it came from. Each file's header names its origin and lists every
  deviation from the tomb3 original with a reason.
- `game/crow.cpp` — not new, but extended: TR5 kept only the crow branch of
  tomb3's `bird.cpp`, so the vulture's animation offsets and its separate
  death handling were added back for the imported bird.

The AI substructure made this possible: `CreatureActive`, `CreatureAIInfo`,
`GetCreatureMood`, `CreatureMood`, `CreatureTurn` and `CreatureAnimation`
exist word for word in both engines.

**No game data from Tomb Raider III is shipped with this project.** The
models, animations and sounds are read at runtime from the player's own
Tomb Raider III installation; without it, those enemies simply do not
appear.

---

## Building this yourself

You do not need to trust our binary. Either:

- build from this tree directly — open `tomb5.sln`, configuration `Release`,
  platform `x86`; the result is `release/PCtomb5.exe`; or
- start from upstream <https://github.com/Trxyebeep/TOMB5>, and apply our
  changes on top. Every change is a commit in this repository, so
  `git diff "$(git rev-list --max-parents=0 HEAD)" HEAD -- "TOMB5 Code"`
  is the complete change set. That asks git for the repository's first
  commit - the unmodified decompilation - rather than naming a hash, which
  is deliberate: this history has been rewritten before, and a written-down
  hash does not survive that.

Unlike upstream, this build produces a **single self-contained executable**.
Upstream ships `PCTomb5.exe` plus `tomb5.dll`, where the DLL carries the game
code and does its work from `DllMain`; that is why upstream updates can ship as
"dll only". This fork builds the Win32 target as an ordinary application with
its own `WinMain`, so there is no DLL.

**No game data is included, here or in any release we make.** Tomb Raider
Chronicles itself remains the property of its rights holders; you need your own
copy of the game.
