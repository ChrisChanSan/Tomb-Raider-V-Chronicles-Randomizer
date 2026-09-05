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

Measured again on **2026-09-04**, after the optional HD loading screens were
added: still 107 files changed, **18** entirely new - **125** in all, 123 of
them carrying the notice. The four additions are `specific/bc7.cpp`,
`specific/bc7.h`, `specific/hdscreen.cpp` and `specific/hdscreen.h`.

Measured again on **2026-09-04**, after the title menu learned to draw the HD
wordmark: **108** files changed, still 18 entirely new - **126** in all, 124 of
them carrying the notice. Nothing was added; `specific/specificfx.h` is the one
file that had never been touched before and now is.

Measured again on **2026-09-05**, after stage S0 of "HD art from the Remaster"
(section 4 below): **110** files changed, **36** entirely new - **146** in all,
136 of them carrying the notice. Two of the newly changed files
(`specific/registry.h`, `tomb5/tomb5.h`) had never been touched before and were
given their notice with this change. The 18 additions are the ten
`specific/hd/hd_*` files and eight under `vendor/trforge/`.

Measured again on **2026-09-05**, after stages S1 to S4-1 of the same section:
**112** files changed, **54** entirely new - **166** in all, **152** of them
carrying the notice. The additions since the count above are the rest of
`specific/hd/` (`hd_lara`, `hd_level`, `hd_light`, `hd_model`, `hd_object`,
`hd_pagecache`, `hd_render` and their headers) and the four generated shader
files under `specific/hd/hd_shader/`. Two headers that had been changed by an
earlier stage and never given the notice - `specific/3dmath.h` and
`specific/drawroom.h` - were given it with this change.

Fourteen of the 166 do not carry it, and none of them should - the ten below
plus the four `specific/hd/hd_shader/*.inc`, which are compiler output
(`fxc` bytecode as a C array) regenerated from the `.hlsl` beside them and
carrying no source of ours at all:
`tomb5/libs/stb/stb_vorbis.c` and `tomb5/libs/discordRPC/LICENSE` are other
people's files that we do not modify at all - stb_vorbis carries its own
licence at the end of the file, and the second one IS a licence. Putting our
notice on them would claim an authorship we do not have. The other eight are
`vendor/trforge/` - five source files copied byte for byte from TRForge (MIT),
its `LICENSE`, its `NOTICE.md` and our `VERSION.txt` that records where they
came from. The five sources carry a hidden signature in their first three
lines; writing anything into them, our notice included, would destroy it, and
we change nothing in them, so there is nothing to notice.

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

**Optional HD loading screens** (`specific/bc7.cpp`, `specific/bc7.h`,
`specific/hdscreen.cpp`, `specific/hdscreen.h`, all new; call sites in
`specific/file.cpp` and `specific/LoadSave.cpp`). Chronicles keeps its 21
loading pictures in `screens\SCREENS.STR`: raw 640x480 A1R5G5B5, 614400 bytes
each, no header, picture n at offset n * 614400. The remaster ships the same
pictures redrawn at 1920x1080 as BC7 `.DDS`, one per level and named after the
level. If the player copies those into `screens\HD\`, the engine uses them; if
the folder is not there, nothing changes. As with the sounds and the music,
none of it ships with this project.

Direct3D 9 predates BC7 by about a decade and cannot sample it, so `bc7.cpp`
unpacks the blocks on the CPU and hands over plain 32 bit pixels. That decoder
is written here against the format definition - no third-party code was taken
in, and `THIRD-PARTY.md` therefore gained no entry. Its partition and anchor
tables check each other at run time (`BC7SelfTest`): every partition must
start in subset 0, must really use all of its subsets, and the anchor listed
for subset n must be a pixel that partition actually assigns to subset n. The
engine refuses the HD path outright if that check ever fails.
`Preparation\Tools\Diagnose\DdsProbe.cpp` compiles the same `bc7.cpp` and
writes a `.PNG`, so the decoder is provable without starting the game.

The pixels go into one square power-of-two `A8R8G8B8` texture, 2048 for a
1920x1080 picture, with the picture in the top left corner - the engine counts
texture coordinates in 256ths and 1920 and 1080 out of 2048 come out as
exactly 240 and 135, so the addressing stays lossless. The 640x480 pictures are
stretched onto the window and always were, distortion included; the HD ones are
16:9 and are **fitted** instead, so on a 16:9 window they land pixel for pixel
and on anything else they get black bars rather than a squeeze. Those bars are
painted with a `Clear`, since nothing clears the frame before a loading screen
goes down. `S_DrawTile` gained a filter argument for this - point sampling is
right for a 640x480 picture blown up to the window and wrong for one that is
already at the window's resolution.

Which picture belongs to which level was **measured, not assumed**, and the
answer is that the original code was already right. `S_LoadLevelFile(num)` gets
`num` from `DoLevel(gf[3], ...)`, and `gf[3]` is the file name index out of
`CMD_LEVEL`, not the slot. The randomizer permutes only the script offset
table, never the file name table nor the indices pointing into it, so `num` is
the level's identity in vanilla and in a shuffled campaign alike - checked
block by block against a vanilla `SCRIPT.DAT` and a shuffled one. `num + 2` is
therefore the correct picture in both, and the HD file name is taken from the
same place, `gfFilenameWad[gfFilenameOffset[num]]`. Changing this to
`gfIdFromSlot[num]` would look like a repair and would in fact index a slot
table with an identity; there is a comment in `file.cpp` saying so.

The logo picture is the one exception to naming by level. Picture 0 is the
game's logo screen, and the remaster drew that one per language, so it is the
only place that picks its HD file by `Gameflow->Language`: `LOGO_DE`, `LOGO_FR`,
`LOGO_US`, and `LOGO_EU` for English and for every language the remaster did
not draw a logo for. Those four files carry an alpha channel and are composed
over black on the way into the texture.

It is also the one picture the engine **builds** rather than loads
(`HDScreenComposeLogo` in `hdscreen.cpp`, `LoadScreenHDLogo` in
`LoadSave.cpp`). The remaster draws the wordmark alone on a transparent frame
and the title picture without any lettering, so the two only ever appeared one
after the other; the wordmark is now cut out of the `LOGO_` file and blended
over `TITLE.DDS` at load time, horizontally centred with the top edge of the
lettering one seventysecond of the picture height down. Whichever of the two
files is missing simply drops out, so a half-filled `screens\HD\` still works.
Two bounding boxes are involved, and mixing them up is the trap: what has to be
**drawn** is every pixel with any alpha at all, because the lettering sits in a
soft black halo reaching about 20 pixels past it on every side, but what has to
be **placed** is only the lettering, alpha above 128. The blend premultiplies
before it interpolates - the files store straight alpha, so interpolating the
colour would drag the black of the transparent pixels into the letter edges and
leave a seam. All of this was measured, and none of it needed the game:
`DdsProbe -logo <title> <logo> <out.png>` compiles the same `hdscreen.cpp`
(with `HDSCREEN_NO_ENGINE`, which is why that one file is built without the
precompiled header) and writes the finished screen as a PNG, reporting the
boxes it used. Against the composite the user built by hand in an image editor,
the mark comes out the same size to within 0.6 % - his is 1020 pixels wide at
every ink threshold from 2 % to 20 % of the peak, the logo file's is 1020, so
he moved it and did not scale it - and its top edge lands on the same row, 15.
The only deliberate difference is that his sits 30 pixels right of centre and
this one is centred.

**The title menu draws the same wordmark** (`LoadTitleLogoHD` and
`SuperShowLogo` in `specific/specificfx.cpp`, `HDScreenLogoSize` and
`HDScreenCutLogo` in `specific/hdscreen.cpp`, call site in `specific/file.cpp`).
Core's menu logo is `data\logo512.pak`: 512x256 pixels of 24 bit colour with
magenta standing in for transparent, cut into two 256x256 pages and drawn as
two quads filling a rectangle 2:1 wide, centred and hanging from the top edge.
The language logos `grlogo.pak` and `frlogo.pak` only ever existed on the
respective CDs, so on the Steam edition German and French fall back to the
English one. If the player has `screens\HD\LOGO_<language>.DDS`, the mark out
of that file is used instead - and German, French and the US emblem then
finally appear in the menu at all.

The mark is cut out with the same alpha box the loading screen uses, resampled
with the same premultiplied bilinear tap, and put into **one** texture page,
at most 1024x512 and a power of two, with its alpha intact; the page is
addressed with texture coordinates, so the unused part of it is never drawn.
The menu then draws one quad instead of two, into exactly the rectangle the pak
logo filled, scaled to fit it without distortion and hugging the top edge: the
three wordmarks are far wider than 2:1 and come out as a band across the upper
part of that rectangle, the US emblem is nearly 2:1 and fills it. The draw type
is the pak logo's own (`polyinsert.cpp` case 1 - alpha blending on,
`SRCALPHA`/`INVSRCALPHA`, alpha test against 0), which the pak logo only ever
exercised at alpha 0 or 255 but which blends the HD mark's soft edges properly.

The new page **takes the place of** the pak logo's left half rather than being
added to the array. That is not tidiness: every page in the title level is
addressed as a distance from the end of `Textures[]` - the font is
`nTextures-2`, the sky `nTextures-1`, the logo `nTextures-5` and `-4` - so the
count has to stay what it was. The pak is still decoded first and stays the
fallback, which is why a missing, unreadable or empty HD file costs nothing.
`DdsProbe -crop <logo.dds> <out.png>` builds the same page out of the same two
routines and writes it as a PNG over a checkerboard, so the page can be
inspected without starting the game.

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

- `global/types.h`, `game/newinv2.cpp`, `game/setup.cpp` — **keys are counted,
  not flagged.** `LARA_INFO::keyitems` was a 16-bit field with one bit per
  `KEY_ITEM1..8`, while puzzle items had always been counters. Picking up a
  key ORed the bit in and the pickup was removed either way, so a second key
  of the same type was silently destroyed - vanilla joby2 hands out two Key 7
  for two separate locks, and a randomized layout can put both of them in
  front of the first door. The field is now an eight-byte counter array read
  and written exactly like `puzzleitems`: a lock consumes one copy, a script
  `TAKEOBJ` clears the slot, the inventory ring shows the number from two
  copies upwards. This changes `sizeof(SAVEGAME_INFO)`, which is serialized
  raw; savegames written before it are refused by the existing size and
  checksum check in `sgRestoreGame()` rather than decoded from a shifted
  layout.

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

### 4. HD art from the Remaster, read at run time (#123)

Stage **S0** of that work — the foundation. It draws no pixel differently; it
finds the installed *Tomb Raider IV-VI Remastered*, reads what that installation
has for the level being loaded, writes the numbers into the log and frees
everything again. The whole path is behind the registry setting `hd_enable`,
which is **0 out of the box**: with it off, `HDEnabled()` returns before any
folder is searched or any file is opened, and the two calls in `file.cpp` are
the only way into the new code at all.

New, under `TOMB5/specific/hd/`:

- `hd_install.cpp` / `.h` — finds the installation. The registry value
  `hd_install` first (same key as every `tomb5.*` setting:
  `HKCU\Software\Core Design\Tomb Raider Chronicles\tomb5`), then every Steam
  library. **Carried over from TROne (GPLv3, Todesengel)** — see
  `THIRD-PARTY.md`; the file names his copyright in its own header.
- `hd_bind.cpp` / `.h` — which `.TRM` belongs to which slot: the level's own
  `LEVELn.MAP` first, then a hand table for the slots this project added beyond
  the original 460, then the slot's own name. The slot name table is generated
  from `game/objects.h` and a `static_assert` in `hd_level.cpp` catches it
  drifting away from the enum.
- `hd_trm.cpp` / `.h` — the header of a `.TRM`, including the trick that gets
  past the facial rig the head models carry. Written against TRForge's format
  description, not copied from it (its own file pulls in a level library we do
  not want).
- `hd_scan.cpp` / `.h` — reads the `.TRG`, counts groups, pages, vertices,
  indices and draw ranges, binds the slots, and times our BC7 decoder over every
  page of the level. Engine-free on purpose: the diagnostic tools `TrgProbe` and
  `MapProbe` compile the same file, so what they measure is what the game
  measures.
- `hd_level.cpp` / `.h` — the level bracket and the `HDLOG` line.

New, under `TOMB5/vendor/trforge/` — **verbatim TRForge (MIT)**, five source
files plus `LICENSE`, `NOTICE.md` and `VERSION.txt`. Those five do **not** carry
our modification notice and must not: they are copied byte for byte, their first
three lines carry a hidden signature, and we change nothing in them.

Changed:

- `specific/bc7.cpp` / `.h` — `BC7DecodeDDSMip` and `BC7DDSMipCount`, so a page
  can be decoded at any level of its mip chain. `BC7DecodeDDS` is now that call
  with mip 0 and behaves exactly as before.
- `specific/file.cpp` — `HDLevelLoad` after `LoadTextureInfos` (the first moment
  rooms, meshes and slots are all in memory, in their randomized state) and
  `HDLevelUnload` in `FreeLevel`.
- `specific/registry.cpp` / `.h` — `REG_ReadString` / `REG_WriteString`, for the
  one setting that is a path and not a number.
- `global/types.h`, `tomb5/tomb5.cpp` / `.h` — the `hd_enable` switch and the
  `hd_install` path.
- `tomb5.vcxproj` — the new files, `/std:c++17` **per file** for the ones that
  use `<filesystem>` (the engine keeps its own default), and
  **`/LARGEADDRESSAWARE`** on both Win32 configurations.

`LARGEADDRESSAWARE` needs the code to survive an address above 2 GB, so the two
places where a pointer meets a signed comparison were measured with this
compiler rather than argued about:

- `game/savegame.cpp` — `if (creature->enemy < 0)` compares an `ITEM_INFO*` with
  the null pointer constant, which MSVC compiles as an **unsigned** comparison:
  false for every address there is, `0x90000000` included. The guard has done
  nothing since the cast the original C had was lost in the decompilation, and
  the save writes `enemy - malloc_buffer`, so a null enemy already comes back as
  0. It was **removed**, with the reason written where it stood — because
  "repairing" it to `(long)creature->enemy < 0` would be true for every address
  above 2 GB and would blank every enemy pointer in a reloaded save.
- `specific/alexstuff.cpp` — `(long)pR->door & 1` is a check that a `short*` is
  not odd. `& 1` reads bit 0 whatever the sign, and on Win32 a `long` is exactly
  as wide as a pointer, so nothing is truncated. Left as it is, with the
  measurement noted.

A sweep of every `(long)` / `(ulong)` cast in the engine found no other one
whose operand is an address and whose result meets a comparison, a division or a
shift. `specific/file.cpp`'s `(long)meshes[lp] / 2` is a division, but what it
divides is a file offset the loader has just read, not an address.

#### Stage S1-1 — the HD draw path itself, built but not yet hooked up

The engine gained a **second renderer**. It is compiled into `PCTomb5.exe` and
**nothing calls it**: a rebuild after adding it reported *0 of 3991 functions
compiled*, which is the plainest possible statement that the game's picture is
unchanged. Hanging it into `InsertRoom` is stage S1-2.

Where the classic path is fixed-function with vertices it has transformed,
clipped and lit itself (`FVF = XYZRHW|DIFFUSE|SPECULAR|TEX2`, `DrawPrimitiveUP`,
no shader anywhere), this one is `vs_3_0` / `ps_3_0` with its own vertex
declaration, its own buffers and one texture per remaster page. The two share
the back buffer and the depth buffer and nothing else.

- `specific/hd/hd_render.cpp` / `.h` — the renderer. One vertex buffer and one
  32-bit index buffer for the whole level; every group's triangles bucketed by
  `(pass, page)` with a **stable** sort into ranges, one range being one draw
  call; `HDRenderFrameBegin` / `HDRenderFrameEnd` as a state bracket around
  every HD block. The structure comes from TROne (`THIRD-PARTY.md`).
- `specific/hd/hd_pagecache.cpp` / `.h` — the level's `.DDS` pages, BC7 decoded
  on the processor over a thread pool and uploaded as **A8R8G8B8**, whole mip
  chain, lossless as the user asked. `D3DPOOL_DEFAULT` through one reused
  system-memory staging texture, which is not a preference but a measurement:
  for TITLE's 220 pages the peak commit of the process is **270 MiB** in
  `DEFAULT` against **545 MiB** in `MANAGED`, and the engine is a 32-bit
  process. The pages are decoded in **byte-budgeted batches**, because 2391 of
  the 2406 pages are 512x512 and decode to 1.4 MiB while eleven are 4096x4096
  and decode to 85 MiB each — a batch counted in pages rather than bytes would
  occasionally ask for one and a half gigabytes.
- `specific/hd/hd_shader/hd_room.hlsl` plus the checked-in bytecode
  `hd_room_vs.inc` / `hd_room_ps.inc` — **no shader compiler is part of the
  build**, so this project still builds with nothing but the C++ toolset.
  `Preparation\Tools\Diagnose\bshader.bat` regenerates the two `.inc` files
  with `fxc` from the Windows SDK; the call is written at the top of the
  `.hlsl`, and GPLv3 is why the source sits next to the bytecode.

**Why one depth buffer serves both paths.** The classic path writes its own
depth, `sz = f_a - f_boo * rhw` with `rhw = 2048 / z_view`
(`polyinsert.cpp:434`, `drawroom.cpp:379`), which is `f_a - f_b / z_view`. A
Direct3D projection matrix with `_33 = f_a`, `_43 = -f_b`, `_34 = 1`, `_44 = 0`
produces exactly that after the w division. `HDRenderSelfTest` compares the two
over 50 view distances spread across the reciprocal of z; the worst difference
is **8.9e-08**. The device's side of it was read back rather than assumed:
`D3DZB_TRUE` (the engine sets it in `function_table.cpp:107`), `ZFUNC`
`LESSEQUAL` (nothing in the engine ever sets it — that is the Direct3D
default), and the viewport at `MinZ 0 / MaxZ 1` (the engine never calls
`SetViewport` at all). All three agree with what the matrix assumes.

- `tomb5.vcxproj` — the two new `.cpp` files with `/std:c++17` per file and no
  precompiled header, the new headers, and the `.hlsl` as a `None` entry so it
  is visible in the solution without becoming a build step.

New diagnostic tool `Preparation/Tools/Diagnose/RoomProbe.cpp` (`broom.bat`):
a hidden window, a real Direct3D 9 device and a render target, so one room can
be drawn from a given camera into a `.PNG` **without starting the game**. It
links the engine's own sources, so what it shows is what the engine draws. It
also prints the depth table above and verifies the state bracket by reading 58
device states before and after an HD block — a check that immediately earned
its keep by reporting that `SetStreamSource(0, NULL, 0, 0)` leaves the old
stride readable, which is Direct3D's behaviour and not ours.

#### Stage S1-2 — the seam: the engine draws the HD rooms

Six lines of engine change and one new engine-facing module,
`specific/hd/hd_level.cpp`. With `hd_enable` at 0 every one of them returns on
its first line and the frame is what it was.

**The frame order, and why it is that order.** The classic path does not draw
in `InsertRoom` either: it fills texture buckets and a sort list there and draws
them later, in `S_OutputPolyList`. So an HD room is not drawn where it is met.
`InsertRoom` (`specific/drawroom.cpp:63`) writes the room number and the matrix
that is on the stack at that moment — view * world for this room, the one
`aSetViewMatrix()` has just handed the device — into a frame list and returns,
skipping its roomlets; `S_OutputPolyList` (`specific/output.cpp`) draws the
whole list **immediately before `DrawBuckets()`**, and `S_InitialisePolyList`
empties it beside the buckets and the sort list.

That place is the only one that works for all three of the depth clears and
draws in a TR5 frame:

1. `S_InitialisePolyList` clears colour **and depth** (#127).
2. `SkyDrawPhase` — inside `DrawRooms`, *before* the room loop — draws the sky
   at once and with `D3DRS_ZWRITEENABLE 0` (`specificfx.cpp`, `OutputSky`), so
   the sky owns no depth and HD walls paint over it exactly as the classic room
   polygons did.
3. **the HD rooms** — into an empty depth buffer, over the sky.
4. `DrawBuckets` — Lara, items, the statics of every classic room — depth
   tested against those walls. The test is exact and needs no bias: the HD
   projection matrix writes the same `sz` for the same point in space that the
   classic path writes (stage S1-1, worst difference 8.9e-08).
5. `DrawSortList` — the see-through polygons, over both.
6. the second depth clear, the one #127 kept, which separates the world from
   the binocular mask, the pickup display and the fade. It is *after* all of
   the above and keeps working unchanged.

**The world matrix, proved in pixels.** `SetD3DMatrix` (`d3dmatrix.cpp`) turns
the engine's 3x4 row-major matrix into a `D3DMATRIX`, and the HD path draws with
that as its world matrix, an identity view and the projection built from the
live `f_persp` / `f_centerx` / `f_centery` / `f_a` / `f_b`. Live, because
`AlterFOV` moves `f_persp` inside a frame — `SkyDrawPhase` does it twice while
the binoculars are up — so the projection is taken when the rooms are
collected, not when they are drawn.

`hd_level.cpp`'s `MatrixCheck` runs the engine's own three transform lines
(`drawroom.cpp`, `aRoomletTransformLight`) and the matrix that really goes to
the card over up to 128 of the room's own vertices, once per level, and writes
the worst difference in pixels into the log. Measured on ANDREA1 out of
`RoomProbe -matrixcheck`: room 12 **0.00017 px** in x, **0.00009 px** in y,
depth difference **0**; room 69 0.00017 / 0.00010 / 0. The vertices for it are
kept aside while the vertex buffer is filled (128 per group, evenly spread,
192 KiB for a whole level), because the `.TRG` is freed at the end of the build
and the vertex buffer is `WRITEONLY`.

**Where the build happens, and on which thread.** `HDLevelLoad` stays on the
load thread and only reads and measures. The Direct3D objects — vertex buffer,
index buffer, page textures — are made by `HDLevelBuild`, called from
`S_LoadLevelFile` beside `aInit()`, **on the main thread**, for the same reason
`aInit()` was moved out of the load thread: two threads on one device froze this
game twice. `HDLevelUnload` (in `FreeLevel`, which runs on the load thread)
therefore makes no Direct3D call at all; the previous level's objects are
released at the top of the next `HDLevelBuild`.

**The room decision is made once per level, not per frame.** A room is HD when
its group exists in the `.TRG` and has at least one index. Everything else stays
classic on its own: the four rooms of ANDREA1 whose group is empty, every room
above the group count, and — without anything being written for it — a room the
randomizer built from scratch. One `HDLOG` line per room records the decision
and its reason.

**Static meshes.** The remaster bakes a level's static objects into the room
art, so `DrawStaticObjects` returns at once for an HD room; drawing them again
is TROne's "double lantern" (`game_renderer.cpp:3722-3736`). TROne suppresses by
mesh pointer and needs an exception for a moveable that claims the same
pointer — our seam is narrower and needs none: this skips one **room's**
`MESH_INFO` list, a moveable sharing the mesh comes through `DrawAnimatingItem`
and is untouched, and a room that stayed classic keeps every static it had.
Counted from the `.TRG` groups and the `.TRC` room headers: ANDREA1 255 of 255
statics suppressed in 238 HD rooms, JOBY3 244 of 244 in 195, RICH1 261 of 261 in
164 — in all three the rooms that stay classic happen to hold no static at all.

**The device.** `DXToggleFullScreen` and `DXChangeVideoMode` call `DXCreate`,
which calls `DXClose`, which **releases the device and makes a new one** — so
Alt+Enter and the resolution keys are a device *replacement*, not a reset. Every
HD object lives on the old device, so `DXClose` releases them first
(`HDLevelDeviceLost`) and the rooms fall back to classic until the next level
starts. The rarely reached `Reset()` path in `DXShowFrame` gets the same call
for a different reason: `Reset` refuses outright while any `D3DPOOL_DEFAULT`
object is alive, and all of the HD path's are.

**The options row.** *Options -> tomb5 options*, the FIRST page, new last row
**HD textures** (on/off, `PAGE0_NUM` 12 -> 13), written to the registry by the
page's own `save_new_tomb5_settings()` like every neighbouring row. Those rows
are hard-coded English literals in `tomb5/troyestuff.cpp`, not language-table
entries, so nothing in `Language.cpp` changes. It takes effect at the next level
start, and the code enforces that rather than promising it: the frame path tests
a flag that only `HDLevelBuild` sets.

#### Stage S2 — light and parity: the HD room joins the engine's own effects

Stage S1 drew the remaster's rooms with the remaster's baked light and nothing
else. Everything the engine computes per room vertex *on top of* the baked
light — `specific/drawroom.cpp`, `aRoomletTransformLight`, lines 305-685 — was
missing, so a torch did not light an HD wall, the distance fog stopped at the
door and three option switches were dead in HD rooms. Stage S2 puts all of it
back, as shader arithmetic.

**S2a — two questions the plan asked, answered.**

*TROne's `hd.light_stage`: not adopted.* It is not a factor and not an
ambient blend. It is an A/B switch with three values
(`host/render/game_renderer.cpp:486`): for rooms, stage 0 throws the baked
`.TRG` light away and substitutes a flat `vec3(0.75)`, stage 1 and above use
the measured sum, and stage 2 adds point lights to *objects* only — which is
our stage S3, not this one. The flat pole is a debugging aid for a renderer
whose classic path is a fallback nobody looks at; ours is the real port, so the
comparison that matters is against the classic path, and `hd_enable` already
gives it. Nothing carried over.

*The sky rule: unchanged, and now verified rather than assumed.* Groups at or
above the classic room count stay out of the room table, so TR5 keeps drawing
its sky procedurally and the HD walls paint over it. Measured with
`RoomProbe -groups`: the last group of every level is an origin-centred dome
drawn entirely in the **alpha** pass and spanning the full int16 range —
ANDREA1 group 242, x -17250..17250, y -3956..3956, 1580 alpha triangles; RICH1
group 165 the same; JOBY4 group 137, ±4098. Real geometry, deliberately not
drawn. TROne draws its dome instead (`hd_draw_sky`, camera-anchored, depth
off), because its games have no procedural sky; that is a picture the user can
ask for later, not a bug here.

**S2b — the parity list.** Every contribution the classic path makes to a room
vertex colour, and where it now lives:

| contribution | classic | in HD | switched by |
|---|---|---|---|
| dynamic lights (torch, flare, muzzle flash, explosions) | `drawroom.cpp:415-436`, per vertex | pixel shader, up to 8 per room | — |
| WaterTable choppy displacement | `:357-362`, per vertex | vertex shader, on the marked vertices | — |
| shimmer / mist / reflection | `:451-517`, per vertex | vertex shader, selector per room | `tomb5.shimmer` |
| underwater darkening | `:477-487` | vertex shader | — |
| Red Alert gas rooms | `:503-517` | vertex shader | `tomb5.shimmer` |
| underwater screen wobble | `:381-387`, per vertex | vertex shader, clip-space offset | — |
| distance fog | `:519-524` | pixel shader | `tomb5.distance_fog` (3..30 blocks) |
| the 128 overbright split | `:526-545` | pixel shader | — |
| fog bulbs | `:547-676` | pixel shader, up to 6 per frame | `tomb5.fog` |

Each one hangs off the very global the classic path reads, in one place
(`specific/hd/hd_level.cpp`, `FillFrameLighting` and `FillRoomLighting`), so a
switch cannot be on in one path and off in the other. `tomb5.fog` is the
clearest case: it does not reach the shader at all — it gates
`aBuildFogBulbList` in `game/draw.cpp:1011`, so with fog off the active bulb
count is zero and the HD rooms lose their bulbs in the same frame the classic
ones do.

**The bridge between two brightness scales, and the one deliberate
deviation.** The classic path builds a colour in 0..255 and then splits it: up
to 128 it doubles the value into the diffuse colour, above 128 it pins the
diffuse at 255 and puts the excess, halved, into the **specular** colour, which
the fixed function pipeline adds after the texture modulation
(`D3DRS_SPECULARENABLE` is on — `function_table.cpp:104` — except for the
additive draw type, `polyinsert.cpp:72`). So a classic room surface covers a
brightness of 0..2, but the *multiply* on the texture never passes 1 and the
rest arrives as an add. The remaster's baked light covers the same 0..2
(`colour1 + colour2`, TRForge over 144 `.TRG` files) as a straight multiply,
and stage S1 drew it that way.

Below 128 the two are the same line, and that is where the bridge is built:

    f = light * 127.5        light = f * 2 / 255

Every contribution is computed in the classic path's own units, in the same
order, and converted back with that one line. **The split itself is not
reproduced**, and that is the single place stage S2 leaves the classic path.
The split is a hardware workaround — a way to fit a brightness of 2 into two
bytes on a fixed function pipeline — and a shader has floats. Following it
would have dulled every bright surface the remaster bakes: measured with
`RoomProbe -paritycheck`, 20 of 384 sampled light channels in ANDREA1 room 19
are above 1, 111 of 384 in room 69, and RICH1 room 12 reaches the full 2.0.
Applying the split to those would turn `texel × 1.5` into `texel × 1 + 0.25` —
a visibly flatter room, and a silent change to the picture stage S1 was already
judged on. The fog bulbs are the exception that keeps the split, because for
them it is not a workaround: a bulb is haze *between* the eye and the surface,
so its density has to darken the multiply and its colour has to be added on
top, or a bulb in front of a black wall would not glow.

**Proved, not asserted.** `specific/hd/hd_light.cpp` holds two transcriptions
of one vertex colour — one of `aRoomletTransformLight` with its longs and its
truncations, one of the HLSL — and `RoomProbe -paritycheck` runs both over 128
of a room's own vertices. It prints two numbers. The first is the
**contribution**, in the classic path's own 0..255 before its split; measured
on ANDREA1 room 19: nothing 0.000, one torch 0.997, distance fog 0.961, one fog
bulb 0.000, underwater 0.000, shimmer 0.000, all of them together 0.997 —
against a budget of 2, and the residue is the classic path truncating its float
sum into a `long`. The second is the **final** brightness on a white texel,
where the split shows: 73 of 255 with no contribution at all, 578 with the
torch. That number is the deviation above, quantified rather than claimed.

**What is per pixel and what is per vertex.** The dynamic lights, the fog
bulbs and the distance fog are smooth functions of a position and are computed
per pixel — a classic roomlet is a handful of vertices and a torch on a long
wall was a Gouraud smear. The WaterTable effects stay per vertex, because the
table is indexed by the position quantised to 64 x 64 x 128 units and per pixel
would paint visible blocks where the classic path interpolates. One consequence
is named here rather than found later: an HD room has far more vertices than a
classic roomlet, so the water pattern's wavelength is finer than the classic
one. Same table, same counter, same amplitude — a denser sampling lattice.

**The water marks.** The `.TRG` marks nothing: no bit in a remaster vertex says
"water surface" or "this one shimmers". The classic path knows both — a roomlet
keeps its water vertices first (`ROOMLET::nWVtx`) and the shimmer predicate
walks the neighbouring rooms through the portals (`drawroom.cpp:141-320`). So
the answer is computed once, at level build, on the classic vertices
(`aRoomVertexWaterMarks`, new, three lines of predicate and the guards that
keep it cheap) and carried to the HD vertex at the same room-local position
through a hash — the same road TROne takes for its own water flag
(`game_renderer.cpp:2127-2188`). Three bits per vertex, in a fifth vertex
element of four bytes; the vertex grows from 20 to 24. **Which** of the three
the shader looks at is decided per frame, out of `r->flags`, because a room can
flood while a level runs and its geometry cannot change.

**Registers, and the one trap in them.** The two shaders are 262 (vs_3_0) and
313 (ps_3_0) instruction slots, against the 512 shader model 3 guarantees; the
device's actual figure is read at start-up and refused below 320 rather than
letting `CreatePixelShader` fail with an HRESULT. `ps_3_0` has **no relative
addressing of constant registers** — only `vs_3_0` has — so the light loop is
unrolled against a compile-time maximum with the run-time count as a dynamic
`if`. Written as a plain loop, fxc emulates `gLights[i]` with 32 `cmp`
instructions per access: measured, about 140 instructions per light per pixel.
The fog bulb loop stays dynamic because its body is long and its array is
short. The full register table lives in `hd_room.hlsl` beside the code, and
`hd_render.cpp` is its only writer.

#### Stage S3-1 — the HD object models: read, skinned, and proved by picture

The remaster does not repaint an object's classic tiles. It throws the mesh
away and draws a model of its own — one `.TRM` file per slot in `<game>\ITEM`,
with its own art. Stage S3-1 reads those models, puts them on the card and
skins them; **the engine does not call any of it yet**, so the game's picture
is unchanged. The seam in `DrawAnimatingItem` is stage S3-2.

**What a `.TRM` is** (`specific/hd/hd_trm.cpp`, extended from the stage S0
header reader). The format is TRForge's measurement (MIT, `ItemModels.h`); the
byte offsets were measured again here over the 566 models of game 5. A 24 byte
vertex: three floats of position, a byte normal biased by 127, a 1-based
texture layer, **three** bone indices, **three** weights, and the two texture
coordinates tucked into the fourth byte of the bone triple and of the weight
triple. What the re-measurement says, because the object path is built on it:

- the three weights sum to exactly 255 in **all** 1,260,496 vertices, and
  174,896 of them (13.9 %) carry more than one — a two-weight reader tears
  every elbow;
- the highest bone index a weighted vertex names is **28** (GREEN_TEETH), which
  is what lets the joint palette be 32. The only two files past that,
  `PIERRE_HEAD` and `LARSON_HEAD` at 40, are facial-rig heads whose numbers
  belong to the rig and which are drawn rigidly on one node anyway;
- the layer is never 0 and never past the texture list — 1-based and always
  valid;
- **not one** of the 1,561,395 triangles spans two pages. One page per draw
  call, exactly as the `.TRG` measurement found for rooms;
- a model has at most three parts, and a part's three passes are contiguous:
  `opaqueEnd == start + opaque` and `alphaEnd == opaqueEnd + alpha`, in every
  part of every file.

**The node question, and why it is not a mapping.** A `.TRM` vertex's three
bone indices are indices into the **classic mesh tree of the slot** — so TRM
node *n* is classic mesh *n*, and there is no permutation to discover. What a
slot needs instead is a **coverage list**: which of its `nmeshes` nodes the
model has geometry for. `hd_model.cpp` counts the weighted vertices per node
and `ModelProbe -nodes` prints that beside the classic bind pose, so the claim
is measured per slot and not assumed. The identity was confirmed on SWAT: the
classic mesh tree of slot 33 puts the head node at y −256 and the feet at
y +425, and the model's own bounding box runs y −331 (helmet) to y +478 (boot
sole) — same units, same axis, same origin.

**The bind pose, folded on the processor.** A `.TRM` vertex sits in the model's
bind pose, so the node's own bind offset has to come off before the node matrix
goes on. TROne subtracts it in the shader with a second constant array of 32
vectors; here the two fold into one matrix, `M' = M` with
`t' = t − bind · R`, which costs three dot products per node per object and
saves 32 registers and three vector subtractions **per vertex**. The bind pose
itself is the mesh tree's accumulated offsets with every rotation left out —
the same walk, push and pop included, that `DrawAnimatingItem` makes.

**The shader** (`hd_shader/hd_model.hlsl`, `vs_3_0` / `ps_3_0`, 65 and 295
instruction slots). The vertex on the card **is** the vertex on disk, all 24
bytes of it: a declaration of `FLOAT3` + `UBYTE4` + `UBYTE4` + `UBYTE4N` reads
the remaster's bytes where they lie, and the unnormalised/normalised pair is
what makes `u` arrive as 0..255 and `v` as 0..1. 32 joints × 3 registers at
c8, one colour per node at c104, the projection at c0 — 136 of 256. The pixel
half carries the **same** registers as the room shader, for the same things
(`gPass` c4, `gFog` c16, `gCounts` c17, `gLights` c20, `gBulbs` c40), because a
Direct3D 9 constant register belongs to the device and not to a shader: the
distance fog and the fog bulbs a frame sets for the rooms are the ones an
object reads. One trap is written into the file next to the declaration: the
model's **vertex** shader does not read `gPass`, so fxc puts two of its own
`def` constants in c4 and c5 there — writing `gPass` to the vertex constant
file would overwrite a literal, silently. `hd_model.cpp` sets it through
`SetPixelShaderConstantF` alone.

**Object light.** One colour per node in the classic path's 0..255 space —
what `CalculateObjectLighting` works out for the object — interpolated by the
same three weights as the position, so it crosses a joint smoothly. The
per-pixel dynamic-light loop of the room shader is there but the engine will
pass a count of zero: `InitDynamicLighting` folds the engine's dynamics into
the object's own light list already, so the node colour has the torch in it,
and running the loop as well would light it twice. Which of the two S3-2 uses
is one count, not two code paths.

**Two shared pieces, so that there is one of each.** The page cache grew an
`HDPageCacheAppend`: a level's models name pages the rooms already wear, and
appending keeps one texture and one slot number per page. The state bracket
moved out of `HDRenderFrameBegin` into `HDRenderStateSave` /
`HDRenderStateRestore`, which the object path opens too — there is one HD path
and there must be one list of states to save, or a forgotten one costs an
evening.

**What it measures** (`ModelProbe`, `Preparation\Tools\Diagnose`, built with
`bmodel.bat`; it links the engine's own sources, so a picture out of it is a
picture out of the engine). Over a whole level: ANDREA1 52 models, 341 of 364
nodes covered, 6.8 MiB of vertex and index buffers, 12 ms to read the files and
152 ms to build them; JOBY3 66 models, 454 of 462 nodes, 5.0 MiB; RICH3 68
models, 528 of 535 nodes, 5.3 MiB. The randomizer's own slots resolve on their
own: `CYBORG` (469) to `HITMAN` and `BLUE_GUARD2` (460) to `BLUE_GUARD_JOBY`,
through the hand table in `hd_bind.cpp`.

**Three findings for stage S3-2, from those tables.** First, an uncovered node
is **not** automatically a classic fallback: in nearly every door slot it is
node 0, the invisible root a TR5 door hangs from, and drawing the classic mesh
there would put a door leaf inside the HD one. Second, `PIERRE` has no head
geometry on node 8 **because** its face is the separate `PIERRE_HEAD.TRM`,
which this stage binds to exactly that node — measured, as the bind-pose node
nearest the face's own centre, not hard-coded. Third, a randomized level can
hand a slot a classic mesh tree with **more** nodes than the remaster's model
has: ANDREA1's `SAS` carries 25 and `SAS.TRM` covers 15. Those three are why
`hd_model.cpp` reports coverage and leaves the decision to its caller, where
TROne marks every node of a valid model as covered outright
(`game_renderer.cpp:1826`) after its own users reported classic patches drawn
over HD bodies.

---

#### Stage S3-2 — the seam for objects: enemies, doors and switches in HD

Stage S3-1 built the models and drew none of them. This stage is the seam that
lets the engine draw them, in `game/draw.cpp`, `DrawAnimatingItem`, at the four
places where a node's classic mesh used to go into the texture buckets.

**The matrices are the game's own, not a rebuild.** An HD skinned model needs
one matrix per node, model space to view space. `DrawAnimatingItem` already
computes exactly those, on the engine's float matrix stack, with the real
animation, the real frame interpolation, the real `gar_RotYXZsuperpack` and the
real mesh-tree flags. So the seam records `aMXPtr` where `phd_PutPolygons`
would have been called and changes nothing else. In the interpolated branch it
records the matrix `phd_PutPolygons_I` would have drawn with — inside the same
`phd_PushMatrix()` / `InterpolateMatrix()` / `phd_PopMatrix()` — and not the
one before the blend.

Five places changed in that function, and the classic path is untouched in
every one of them when the object is not HD:

| Line | What |
|---|---|
| after `phd_RotYXZ` | `const bool hd = HDObjectBegin(item, obj);` — one array lookup |
| the distance-model branch | now `if (!hd && obj->object_mip && ...)`: an HD object never switches to its MIP slot |
| node 0, both branches | `if (hd) HDObjectNode(0, ...); else if (item->mesh_bits & 1) ...` |
| the node loop, both branches | `if (hd) HDObjectNode(i + 1, ...); else if (bit & item->mesh_bits) ...` |
| after the loop | `if (hd) HDObjectEnd();` — outside `if (clip)`, so the bracket closes on every road out |

The rest is `specific/hd/hd_object.cpp`, which is to objects what `hd_level.cpp`
is to rooms: engine-facing, and the only file in `specific\hd` besides that one
that includes an engine header.

**Does the model fit the skeleton in the slot?** It has to be asked, because
the randomizer puts foreign creatures into slots: ANDREA1's `SAS` holds a
twenty-five-node mesh tree and `SAS.TRM` was drawn for fifteen. Two tests, per
slot, at level build:

1. **The node count.** A weighted vertex naming node *n* where the slot has
   fewer than *n* nodes settles it. `FIREROPE` fails this in eleven of the
   fourteen levels: TR5's slot is a one-piece rope, the model comes out of
   `4\ITEM` where the slot had seventeen.
2. **The bind drift**, for a foreign model with the right node count. For every
   pair of nodes that carry geometry, the distance between their vertex centres
   in the model against the distance between their bind positions in the slot's
   own mesh tree, with each mesh's own RMS size subtracted.

The second one took two attempts and the first attempt is worth recording. It
measured each node's vertex centre against that node's bind position — and in
**vanilla** ANDREA1, where nothing can be mismatched at all, it condemned every
door, every switch and the Rome hammer: `DOOR_TYPE1` came out 748 units
"wrong". What it was really measuring is where the modeller put the origin,
which is the model's business and not the skeleton's. The pair form has neither
problem: a translation of the whole model changes no distance, and taking the
meshes' own size off removes the long-bone bias that made the hammer look
broken (a long mesh's centre sits half a bone-length from its joint).

Measured with `ModelProbe -all` over all fourteen randomized levels and their
vanilla twins:

| | mean drift |
|---|---|
| worst model that **belongs** where it stands | 239 (`ANIMATING3`, JOBY3/JOBY5) |
| then 214, 192, 185, 184, 179, 172, 161, 157 | the rest of the field |
| best model that does **not** | 308 (`ANIMATING1`, RICH2) |
| then 330, 375, 428, 466, 484, 522, 687, 690, 807 | up to `SAS` at 807 |

The line is at **256**, inside that gap and nearer the accepted end on purpose:
a good model refused costs a classic-looking door, a bad one accepted costs a
body torn across the room. A model with one node has no pair and is never
refused on drift — rightly, since any one-node model fits any one-node slot.

**Mesh swaps and `mesh_bits`.** Two different things that both hide or replace
geometry per node, and they get different answers.

`item->meshswap_meshbits` picks the **second** mesh of a node's pair — filled at
level load from `MESHSWAP1`/`MESHSWAP2` or, for the randomizer's cross-level
enemies, from the `MESHSWAP_CROSS_*` donor slots (`game/setup.cpp`,
`MeshSwapSource`). Measured, every site in the engine:

| Slot | Nodes whose second mesh is filled | From |
|---|---|---|
| `BLUE_GUARD`, `SWAT`, `SWAT_PLUS`, `MAFIA`, `CRANE_GUY`, `BLUE_GUARD2` (460) | 10, 13 | the matching `MESHSWAP_CROSS_*` donor, else `MESHSWAP1` |
| `SCIENTIST` | 10, 13 | `MESHSWAP1` raw |
| `MAFIA2` | 7, 10, 13 | `MESHSWAP2` raw |
| `TWOGUN` | 10 | `MESHSWAP_CROSS_TWOGUN`, source mesh 2 |
| `IMP` | 10 | `MESHSWAP_CROSS_IMP` |
| `HITMAN` | 0-19 | `MESHSWAP1` raw |
| `CYBORG` (469) | 0-19 | `MESHSWAP_CROSS_HITMAN` |
| `GLADIATOR` | 0-15 | `MESHSWAP_CROSS_GLADIATOR` |
| `ROMAN_GOD`, `ROMAN_GOD2` (461) | 0-16 | their own `MESHSWAP_CROSS_*` |
| `BURNING_ROOTS` | 0-27 | `ANIMATING16_MIP` |

and at run time the bits are set by: every soldier at birth (`0x2400`, nodes 10
and 13, `game/init.cpp`), the Cyborg one panel per eleventh of his health over
eleven of the twenty (`game/hitman.cpp`), Mafia2 `0x2400` then `0x80`
(`game/mafia2.cpp`), the Roman gods one bit per weapon (`game/romangod.cpp`),
the SAS node 10 while he throws (`game/sas.cpp`), the Imp node 10
(`game/imp.cpp`), and `-1` — the WHOLE body out of the swap set — for a
gladiator with trigger flag 1 and for one of the search objects. Two things
follow. It is always per NODE: no slot has all its meshes replaced by another
slot's, so "the HD model of the source" never has to be looked up. And even the
`-1` cases take their swap set from their own family's donor, so the source
model and the slot's model are the same file anyway. A `.TRM` is one whole model with no
second geometry for a node, so **the swap is ignored and the model draws what
the remaster drew** — which for a guard is the guard holding his weapon, as the
`ModelProbe` picture of `SWAT` shows. TROne tried drawing the swap mesh
classically over the HD body and took it out again after user reports of
classic patches on HD bodies (`game_renderer.cpp:1825-1830`, where `covered` is
forced to `~0u` for exactly this). What is really lost is the Cyborg's panels
opening; it is counted per level in the log as `swap_ignored`.

`item->mesh_bits` hides whole nodes and **is** honoured. An index buffer cannot
skip a triangle at draw time, so each model's triangles are sorted by
`(page, node)` instead of by page alone and every range carries its node — the
node that carries the most weight over the triangle's three corners. At draw
time consecutive visible ranges of the same page are merged again, so with
`mesh_bits` at `-1`, which is nearly always, the draw-call count is exactly the
one stage S3-1 had: `SWAT` builds 16 ranges and draws 2. `ModelProbe -meshbits`
proves it — `0xBFFF` on the SWAT takes his head off and leaves the rest whole.

**The light is per vertex, because the classic path is per vertex.** The
alternative — one colour per node — was rejected by reading the code:
`aTransformLightClipMesh` (`specific/output.cpp`) runs the light formula with
the vertex's own normal, and a flat colour per node would draw a chest whose
front and back are equally bright. So what goes to the card is the light LIST,
and the shading happens in the vertex shader with the remaster's own normals.
Three things make that exact rather than an approximation:

- the engine's per-node light vectors are in that node's model space and its
  normals with them; a skinned normal arrives in view space, and a node matrix
  is a rotation without scale, so `dot(n, L)` is the same number in either
  space and one list serves every node;
- the ambient really is per object, not per node: the shadow lights that lower
  it read `light->Range`, which `CreateLightList` measures at the ITEM;
- what remains per node in the classic path — a point light's direction and its
  falloff, measured from the node's world position — is handed over as the
  light's **position** instead and worked out from the surface point, which is
  finer than the classic answer everywhere and identical at the node itself.

The activity flags, the eight-frame fades and the range culling are **not**
redone: `Active` and the faded colours are the engine's own, computed in
`CreateLightList` a moment earlier inside `CalculateObjectLighting`, which the
seam still calls. Registers, vertex side: `c136-c159` twelve lights of two
registers, `c160` the count and the sun's own factor (0.75 in the game, doubled
in the inventory — the engine's own quirk). The pixel side is untouched and
shares `c16` fog, `c17` counts and `c40` bulbs with the room shader, so one
frame's constants serve both; an object writes `c17.x = 0`, which switches off
the room shader's per-pixel dynamic-light loop, because for an object those
lights are already in the vertex list where the classic formula puts them. The
vertex shader is 358 of the 512 instruction slots `vs_3_0` guarantees.

**Where they are drawn, and why there.** In `HDFrameDraw`, inside the bracket
the rooms already opened, after the rooms and before `DrawBuckets`. The depth
buffer is shared and exact (stage S1-1's `HDRenderSelfTest`: both paths write
`a - b/z` for the same point in space), so an opaque HD guard and an opaque
classic crate sort themselves out by depth in either order — which covers every
opaque pixel and needs no rule at all. Only the additive pass, which writes no
depth, is order-sensitive, and the note in the manual says so. Drawing objects
inside the room loop instead would swap shader, declaration and stream twice
per room, for nothing.

**Two proofs in the log**, on the first HD object of every level, with `-log`:

```
HDLOG itemcheck level=ANDREA1 slot=33(SWAT) nodes=15 used=15 maxdx=... maxdy=...
HDLOG lightcheck level=ANDREA1 slot=33 classic=r,g,b hd=r,g,b diff=... engine=1sun/2point/0spot
```

`itemcheck` puts each node's origin on the screen twice — once through the
classic transform with the live `f_persp` and centre, once through the
projection matrix the card is handed — and prints the worst disagreement in
pixels; it is the object twin of stage S1-2's `matrixcheck`. `lightcheck` runs
the classic formula on the engine's own `SunLights`/`PointLights`/`SpotLights`
and the HD one on the view-space list that was uploaded, on the same
non-axis-aligned normal, and prints both. That second one exists because the
space conversion is the one part of the object path that cannot be seen in a
picture, only measured.

**One engine global was un-`static`ed**: `aW2VMx` in `specific/3dmath.cpp`, the
float twin of the long `w2v_matrix` that has always been exported. The object
path needs the float one to put a light's world position into view space; the
long matrix carries its rotation in fourteen bits, which is four digits where a
light direction wants seven.

**The switch.** A new registry value `hd_objects`, `1` by default, in the same
key as every other setting. No options row: it is not a matter of taste but a
way back if one level's objects ever go wrong, and it does nothing at all while
`hd_enable` is off. Both guards are the first line of `HDObjectBegin`, so with
either off not one byte of this stage is entered.

**Still classic, by plan.** Lara and her whole composite (slots 0-30) — stage
S4. Objects whose slot has no model, whose model does not fit the skeleton, or
whose draw routine is not `DrawAnimatingItem` (the scaled spikes, the three
kinds of laser, the darts, the lift doors, the portal door, the gas cloud, the
flare in flight, a thrown weapon — fourteen assignments of a routine of their
own in `game/setup.cpp`). Per level that leaves ANDREA1 drawing 50 of
its 52 models in HD, JOBY3 65 of 66 and RICH3 65 of 68.

#### The frame clock, and the fog bulb maximum

Two small things that came out of reading a real play session's log.

**`FPSLOG` — the frame rate, measured.** A new seam in `DXShowFrame`
(`specific/dxshell.cpp`), one line directly after
`IDirect3DDevice9::Present`. That function is the whole engine's only
present — `S_DumpScreen` (the game frame, the inventory, the title menu),
`S_DumpScreenFrame` (the loading bar, the credits) and `fmv.cpp` all end
there — so one call counts every frame the player sees, exactly once, and
counts it including the wait and the driver time. Measuring inside
`S_OutputPolyList` would have missed the loading bar and the movies;
measuring in the main loop would have counted message pumps that drew
nothing.

The counting lives in `specific/hd/hd_level.cpp` beside the other frame-path
diagnostics (`HDFramePresented`). With `-log` it writes one line per second
of wall clock — frames presented, shortest / longest / average gap, how many
gaps missed the 30 Hz budget, and the heaviest HD room, object and draw-call
counts of that second — and one summary per level, flushed at the top of
`HDLevelBuild` for the level before and in `HDLevelDeviceLost` so the level
the player quits from gets one too. Both are on the main thread, after the
load thread has joined.

Two counters and not one, because the logic is a fixed 30 Hz built on a 60 Hz
tick: `slow` is every gap over the 33.3 ms budget, which half a healthy
second reaches on jitter alone, and `late` is every gap over 41.7 ms, which
jitter cannot reach because a missed slot costs a whole tick. Without `-log`
the first line of `HDFramePresented` returns — no clock read, no counter,
nothing allocated anywhere in the path.

**`HD_MAX_BULBS` 6 → 8**, in `hd_render.h` and in both pixel shaders, which
share one frame's constant upload and must agree. Registers `c40-c55`
(sixteen, two per bulb); `c52-c55` were free in both. The pixel shaders grow
from 310 to 319 (room) and 295 to 304 (model) of the 512 instruction slots
`ps_3_0` guarantees — about 4.5 slots per bulb, because `ps_3_0` has no
relative addressing of constants and `fxc` emulates the array access with a
chain of `cmp`.

The number is measured out of all sixteen level files: a level holds up to
17 fog bulbs (ANDREA2, and TITLE has 17 as well) and one room up to 5
(ANDY3); sweeping every camera position, up to **16** of a level's bulbs fall
inside the 20480-unit circle `aBuildFogBulbList` keeps around the camera at
once (16 ANDREA2, 14 RICH2, 13 ANDY1, 11 TITLE, 10 RICH1). So 8 is not the
ceiling — it is where the title screen stopped visibly losing one. The
engine's own ceiling is 16, and that is not a coincidence:
`RoomletFogBulbs[16]` in `drawroom.cpp` is filled with no bound check at all,
so 16 is also where the classic path itself would start writing past its
array. Raising this cap to 16 would cost 355 / 340 slots and `c40-c71`, both
still well inside the 512 and 224 `ps_3_0` guarantees.

The **`clipped` line goes with it**: it used to reset its counters every
frame, so the comment saying "once per level" produced 173 identical lines
out of one title screen. It now counts for the whole level — one line at the
first frame that clips, naming the room and how many lights and bulbs were
offered, and one total at the level's end. Both `Fill…Lighting` functions
count what they turn away instead of stopping at the cap, so the numbers say
*how many* and not just *that*.

Proved with `RoomProbe` — the same room, the same camera, six harmless bulbs
in both runs, and a seventh and eighth that only the raised cap can reach:
they show up as a red and a blue glow that are not in the six-bulb picture,
and a ninth is reported dropped at the new maximum.

---

#### Stage S4-1 — Lara's composite: the wardrobe loaded, measured, and drawn without the game

New: `specific/hd/hd_lara.cpp` and `specific/hd/hd_lara.h`. **The engine calls
neither.** `hd_lara.cpp` compiles into the executable and nothing in the game
references it, so the frame is unchanged, exactly as `hd_model.cpp` stood after
stage S3-1. The seam in `DrawLara` is stage S4-2.

**Why Lara needs a module of her own.** Every other creature is one slot and
one `.TRM`, which is why `hd_object.cpp` can decide per slot and be done. Lara
is drawn out of five slots in one pass (`specific/drawlara.cpp`): the skinned
body from `LARA_SKIN`, the fourteen seam pieces from `LARA_SKIN_JOINTS`, the
hands from whichever weapon slot the mesh swap put at `LM_RHAND` / `LM_LHAND`,
the thigh straps from `LHolster` and `lara.holster`, the weapon on the back
from `lara.back_gun`, and the braid from `HAIR` through `DrawHair`. The
remaster answers with a wardrobe of separate models, and which of them to draw
changes from frame to frame. So the module has two halves: a level half that
loads the wardrobe, and a frame half that answers "which parts, on which
nodes" from the slot numbers `DrawLara` already holds.

**The measurements it is built on**, all taken with `ModelProbe -name <NAME>
-nodraw` over the remaster's `4\ITEM` (the whole wardrobe lives there, not in
`5\ITEM` — the same shared-folder rule TROne found):

* Every `OUTFIT_` model skins to nodes **0..14** and names no sixteenth, so the
  composite's skeleton is Lara's classic mesh tree and the fifteen matrices in
  `lara_matrices` are the joint palette. `OUTFIT_SUBSUIT` is the one exception
  and covers thirteen: the diving suit has no feet, it ends in flippers that
  belong to the shins.
* `LARA_HEAD.TRM` covers 31 nodes — it carries a **facial rig**, whose bone
  numbers are not mesh-tree nodes at all — so it rides rigidly on node 14, the
  same treatment a creature's `<NAME>_HEAD.TRM` gets. `hd_model` already had
  the rigid path; it only needed a way in, which is the new `rigidNode` field
  of `HDModelSlot`.
* `HAIR_OG.TRM` has six bones. The classic `HAIR` object's own mesh tree is a
  straight chain at z = 0, 56, 112, 168, 224, 280 in every adult level — six
  links, the exact spacing of the model. Young Lara's `HAIR` is the same six
  nodes at half the spacing (27 units), which is the pigtails.
* A `HAND_` model covers 9 and 10 on the right and 12 and 13 on the left;
  `HAND_BARE_REST` adds the two upper-arm stubs 8 and 11. Three of them are
  one-sided: `HAND_BARE_FLARE` and `HAND_BARE_TORCH` cover 12 and 13 only,
  `HAND_BARE_CROWBAR` 9 and 10 only.
* `HOLSTER_PISTOL` and `HOLSTER_UZI` cover nodes 1 and 4, `HOLSTER_REVOLVER`
  node 4 alone, and `HOLSTER_SHOTGUN` / `_HK` / `_GRAPPLE` node 7 — the two
  thighs and the torso, which is where `DrawLara` puts the classic ones.

**Which outfit a level wears** is decided in `HDLaraOutfitFor` out of three
things the engine already knows, and out of nothing else. `LaraDrawType` (the
level header's outfit byte plus one) answers for the diving suit and the
catsuit, which have values of their own. It cannot answer for the cap: a
vanilla Russia level carries the same byte Rome does. So the caller passes in
the engine's **own** test for that — `(gfLevelId >= LVL5_BASE && gfLevelId <=
LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType ==
LARA_BUNHEAD`, the expression that already stands three times in
`game/hair.cpp` and once in `game/laraskin.cpp` — rather than a second copy of
it written here, which would be a thing that can drift. Young Lara is
recognised three ways over (the script flag, the Irish level range, the byte)
and stays classic by the user's decision. Read against all fifteen vanilla
level files the rules answer correctly fifteen times out of fifteen.

**TROne's TR5 glue has ANDY and JOBY the wrong way round** and the table here
deliberately does not follow it: `games/tr5/glue/hostglue_file5.cpp` gives
`OUTFIT_RUSSIA` to the `ANDY` files and treats the `JOBY` ones as young Lara,
where in this game — and in the level files, measured — `JOBY` is the Russian
submarine section and `ANDY` is Ireland. The part of that file that *was*
carried over is the wardrobe itself: which model plays which role.

**Only what the level can use is loaded.** `HDLaraLevel::slots` takes the list
of Lara's slots the level really loaded, and a weapon or holster model is
planned only when its slot is there. A weapon whose slot is missing cannot be
drawn by the classic path either — that path reads
`meshes[objects[slot].mesh_index]` — so nothing is lost. Measured over the
vanilla levels: the full wardrobe is 21 models, 3.5 MiB of buffers and 30 MiB
of pages; ANDREA1 needs 11 models and 20 MiB, JOBY5 16 and 27 MiB, RICH1 6 and
17 MiB, JOBY4 two models and 12 MiB. Build time 34 to 79 ms.

**The frame half** is `HDLaraFrameParts`, and it is TROne's rule with one
addition. TROne picks ONE hand model — the first of nodes 10 and 13 whose mesh
came from a non-body slot — and draws it whole
(`game_renderer.cpp:1594-1616`). That is right for every two-handed frame and
wrong for the three TR5 frames where the two hands differ: flare and torch are
in the left hand alone, the crowbar in the right hand alone. So when the two
sides disagree, both models are drawn and each is masked to its own arm through
the `mesh_bits` argument `hd_model` already has — 8, 9, 10 on the right, 11,
12, 13 on the left. When they agree, which is every other frame, it is one draw
with every node, exactly as before. The same split answers the holsters, whose
two slots the engine keeps apart as well (`LHolster` and `lara.holster`).

**An empty holster draws nothing extra**, and that is the picture and not a
gap: the straps are painted into every `OUTFIT_` model, which is why the
remaster ships no "empty holster" file. TROne reaches the same result by
registering the slot with an empty model name.

**One thing hd_model needed:** `HDModelSlot` gained `rigidNode` (default -1,
so every existing caller is unchanged) and `HDModelLevelBegin` passes it to
`build()`, which has taken a rigid node since stage S3-1 for the separate
faces. That is the whole engine-side change outside the new files.

**The proof, without the game.** `ModelProbe -lara <LEVEL>` builds the
composite against that level's own Lara mesh tree and draws it: body, face,
braid, hands, holsters, back weapon. `-outfit` wears another one, `-guns`
puts a weapon in her hands (and empties the holsters, as `draw_pistol_meshes`
does), `-flare` / `-torch` fill the left hand alone, `-lara-nodes` prints every
node 0..14 against every source with the `.TRM` that covers it, `-lara-pose`
turns a node, and `-mirror` draws through a view matrix of negative
determinant. Pictures made this way: the Rome Lara complete from the front and
the back (the braid, the backpack, the pistols in their holsters), the pistols
in her hands with the holsters empty, the diving suit with her face inside the
helmet, the VCI catsuit, an Antarctica outfit, the mirror, and a head turned
55° with the braid following it.

**The mirror is a non-event, and the picture says so.** A mirror room turns the
world inside out and a matrix that does that has a negative determinant, so
every triangle's winding flips. The HD path does not care: `hd_render`'s state
bracket sets `D3DCULL_NONE`, which is also what the classic engine does
globally (`specific/function_table.cpp:105`) — TR5 culls back faces in
*software*, through an `IsVisible` function pointer that `game/mirror.cpp`
switches with `SetCullCW` / `SetCullCCW`. And TR5's own mirror does not even
produce a negative determinant: it mirrors Lara by *moving* her (`z_pos = plane
- z`, the three angles negated), which is a rotation.

**Still open, and it is a randomizer change:** the four Tomb Raider III outfits
(#122). The randomizer rolls them and writes `LARA_PONYTAIL` into the outfit
byte when it does — the same value it writes for the Rome outfit inside a
Russia level — so the byte cannot say which guest she wears, or that she wears
one. `HDLaraOutfitFor` therefore takes the answer as an argument
(`tr3Outfit`), and the mapping is measured and in place:
`tr3_london` → `OUTFIT_TR3_CATSUIT`, `tr3_jungle` → `OUTFIT_TR3_COAST`,
`tr3_antarctica` → `OUTFIT_TR3_ANTARC`, `tr3_nevada` → `OUTFIT_TR3_NEVADA`.
What is missing is a marker byte in the level header; the 32-byte block behind
the texture pages already carries `LaraType` at `data[0]`, the weather at
`data[1]` and the foreign-speech-head flag at `data[2]`, and `data[3]` is the
obvious place. That is a randomizer change and belongs to a later stage.

---

#### Stage S4-2 — the seam for Lara: the composite drawn by the engine

New: `specific/hd/hd_laradraw.cpp` and `specific/hd/hd_laradraw.h` — the
engine-side half of stage S4-1. `hd_lara.cpp` stays engine-free (ModelProbe
links it), and this file is what reads TR5's own state and turns it into the
arguments that module and `hd_model` want.

**Three edits in `specific/drawlara.cpp`, one per draw variant.** Each sits
directly after `CalculateObjectLightingLara()`, which is the last thing both
roads need, and each is a single `if`:

* `DrawLara__1` (everything but the catsuit and the diving suit) and
  `DrawLara__4` (the catsuit) **leave the function**: they pop the one matrix
  they pushed, put the clipping window back exactly as their own tail does,
  and return. Everything they would have drawn — the skinned body, the
  fourteen seam pieces from `LARA_SKIN_JOINTS`, `DrawHair`, the two thigh
  holsters and the weapon on the back — is what the composite replaces.
* `DrawLara__5` (the diving suit) does **not** leave: its skin loop is skipped
  and the two `LARA_EXTRA_MESH1` thrusters below it still draw. They are a
  separate object on her torso rather than a node of her body, so a classic
  thruster on an HD suit is not the patchwork the object path refuses — and
  the wardrobe has no model for them.
* `DrawLara__6` (the skeleton cheat) is untouched and stays classic.

The `mesh_bits` argument differs per variant on purpose: `true` for
`DrawLara__1`, which reads them, `false` for the two that do not. A seam that
read them where the classic path does not would make the catsuit disappear in
the very cutscenes that hide the classic joints.

**The matrices are the engine's own.** `lara_matrices` already holds fifteen
3×4 matrices in view space, built this frame by `CalcLaraMatrices`. They are in
`LMX_` order and a `.TRM`'s bone numbers are mesh-tree nodes, which are `LM_`
order; the whole difference is the head, which is matrix 8 and node 14. One
table turns the one into the other and nothing else about the animation is
touched — the same frames, the same interpolation, the same
`gar_RotYXZsuperpack`.

**Which parts, per frame**, out of the state `DrawLara` already holds:
`LHolster`, `lara.holster` and `lara.back_gun` are slot numbers already. The
hands are not — the engine keeps them in `lara.mesh_ptrs` as **pointers** — so
the seam compares that pointer against
`meshes[objects[s].mesh_index + 2 * LM_xHAND]` for Lara's own loaded slots,
which is exactly what `game/larafire.cpp:331` does when it wants to know
whether the flare is still in her hand. At most a dozen comparisons, twice a
frame. No match means a mesh swap from outside the wardrobe, and the rest hands
are drawn: a hand and not a hole.

**The braid needed three matrices the game never builds.** `HAIR_OG` has one
bone per link, but TR5 puts a matrix only under links 1, 3 and 5 and draws 0, 2
and 4 as *skinned connectors* out of their neighbours' stashed vertices
(`game/hair.cpp`, `DrawHair`). The angles for the other three are there and
have simply never been used: `HairControl` writes `y_rot` and `x_rot` for every
one of `hairs[0][0..5]`. So all six are built the same way the three rigid ones
are — `phd_TranslateAbs`, `phd_RotY`, `phd_RotX` — from the base of the matrix
stack and **not** from `aW2VMx`, because `phd_GenerateW2V` puts the poison
shake on the stack's base and leaves `aW2VMx` alone; a braid built from the
latter would be the one part of her that stood still.

**The light** is the object path's, with one thing an object does not have. The
list of suns, points and spots goes to the vertex shader in view space through
`HDObjectLights`, the same conversion and the same twelve. The **ambient is per
node**: a body half in the water is lit by two rooms at once and the classic
path switches between them per node through `bLaraUnderWater`. The seam runs
`InitObjectLighting` once with that flag forced to "dry", which is the branch
that answers `LaraNodeAmbient[0]`, and reads the wet answer straight out of
`LaraNodeAmbient[1]` — a second call would push every dynamic light onto the
list twice.

**First person** (#126) drops the head node, the separate `LARA_HEAD` and the
braid, which is what the classic path drops. It does *not* have to copy the
reason the classic path hides the head through the draw and not through
`mesh_bits` — the neighbours pull their rim points out of the head mesh's stash
and lose them with it, the user's "third arm" — because an HD model has no
stash and no seam pieces: the head node is one range of triangles.

**The mirror needs nothing.** TR5 mirrors Lara by *moving* her
(`game/mirror.cpp`: the z position reflected in the plane, the angles negated,
`CalcLaraMatrices(2)` scaling by −1) and flips its own software backface test
with `SetCullCW` / `SetCullCCW` around the second draw. The HD state bracket
sets `D3DCULL_NONE`, so a negative determinant costs it nothing. The hair is
mirrored in world space by that same function, so the braid follows for free.

**No new registry key.** `hd_objects` covers Lara: she is a model like the
creatures and a level's models are one vertex buffer, so a switch of her own
would only be a second way to end up with half a picture.

**Where her parts are drawn.** On the object path's own frame list, through two
new calls in `hd_object.h` (`HDObjectLights`, `HDObjectPush`) and its bind-pose
walk (`HDObjectBindPose`) — one frame list, one light conversion, one bracket,
one line in the log. `game/draw.cpp` calls `DrawLara` before the `PrintObjects`
loop, so her parts go on the list first and come off it first, which is the
classic order.

**One behaviour changed outside the seam**, and it is the user's decision:
`HDLaraOutfitFor` now answers `HD_LARA_OUTFIT_NONE` for `LARA_PONYTAIL`. That
byte means two different things — the Rome body inside a Russia level, and
every Tomb Raider III guest (#122) — and nothing else in the level file
separates them. Reading it as the Rome outfit would dress every guest in
`OUTFIT_CLASSIC`. Such a level draws the classic Lara until the randomizer
writes a marker of its own; `HDLaraOutfitFor` has taken it as an argument since
stage S4-1. No vanilla level carries the byte, so a run without the wardrobe
switch loses nothing.

**Known losses, named rather than faked:** the catsuit's environment-map sheen
(`phd_PutPolygonsSpcEnvmap` is a second, view-dependent pass an HD model does
not have, and the remaster's `OUTFIT_VCI` carries its shading in its textures),
the diving suit's two thrusters staying classic, and the muzzle flash, the gun
shells and the flare in flight, which are drawn by their own routines outside
`DrawLara` and were never part of the composite. The inventory ring never calls
`DrawLara` at all.

**The proofs.** Two lines per level with `-log`, on the first HD Lara frame:
`HDLOG laracheck` puts each of the fifteen joint origins on the screen twice —
the classic transform against the matrix the card is handed — and `HDLOG
braidcheck` steps each braid link's own frame one bone along and measures how
far it misses the next link, which is the identity `HairControl` used to derive
those positions in the first place and the only part of this seam a still
picture cannot show. `ModelProbe -laracheck` runs both without the game:
**0.00000 px** over all fifteen joints and a worst chain gap of **0.00006
units** over the five braid joints. `ModelProbe -firstperson` and `-mirror`
draw the two pictures.

#### Stage S5 — what moves in an HD room

An HD room drawn by stages S1 to S4 was a photograph: the light was right, the
geometry was right, and nothing in it moved except the water vertices the
classic path itself displaces. This stage makes three things move, and all
three of them are the remaster's own data rather than an invention.

**What the classic engine does, measured first.** `S_AnimateTextures`
(`specific\output.cpp:864`) has two branches, and the level file's
`aranges`/`nAnimUVRanges` (`file.cpp:1102`) decide which one a range takes:

- a **sequence**: every five game frames the `TEXTURESTRUCT`s of a range are
  cyclically permuted, so a face keeps its texinfo number and shows the next
  tile of the range. Twelve of the fifteen levels carry one to ten such
  ranges, three to thirty-two tiles each; `TITLE`, `RICH3` and `DEL` have
  none.
- a **UV rotation**: the first `nAnimUVRanges` ranges scroll their tile
  downwards by 32 of 256 texels per cycle, at the speed the script's `UVROT`
  command sets. Over the whole vanilla `SCRIPT.DAT` that command appears in
  exactly **two** levels: `ANDY3` (speed 3) and `RICH2` (speed 8) - which is
  also why `LoadAnimatedTextures` patches `aranges[2]` to 2076 for `RICH2`
  alone.

**What the .TRG has, measured against it.** A `.TRG` part carries a material
number and four floats (`vendor/trforge/src/trf_remaster/Trg.h`), and over all
fifteen levels three of those materials matter here:

| material | meaning | where |
|---|---|---|
| 1 | UV scroll, `param.xy` in page units per second | 5 to 65 parts in 14 of 15 levels |
| 8 | wind, with the weight per vertex in `TrgVertex::sway` | ivy and banners in Rome, grass in Ireland, kelp in JOBY4 |
| 13 | a four frame page flipbook - `param[0..2]` name three further texture layers | 3 to 8 parts in JOBY2, JOBY3, JOBY5, RICH1, RICH2 |

The scroll and the classic UV rotation are the same thing, and `RICH2` is the
one level where both exist on the same wall: the classic tile moves 7.5 tile
heights a second there, the remaster's own scroll 4.0. Same axis, same sign,
the same order of magnitude - which is what says the unit is page-per-second
and not something else.

**What was implemented.**

1. **The scroll** (`hd_render.cpp`). A draw call is a run of triangles that
   name one page; from now on it is a run that names one page *and* one
   scroll, because the offset is a shader constant and a triangle cannot have
   half of one. The offset itself is computed on the processor and taken
   modulo one page, so a game running for an hour does not spend a float's
   mantissa on the integer part. The sampler goes to `WRAP` for those ranges
   and back to `CLAMP` after them - an HD page is one whole texture and not an
   atlas, so wrapping is safe, but a still surface whose UV sits exactly on 0
   or 1 filters differently under the two modes.
2. **The wind** (`hd_shader/hd_room.hlsl`). `TrgVertex::sway` was already on
   the card: it is the fourth `short` of the position, which the vertex
   declaration hands the shader as `POSITION.w`. No vertex format change, no
   second buffer. The weight is the remaster's, the wave is ours (the shape
   is TROne's `water_effect`); it moves the drawn position only, so the water
   hash, the surface test and everything stage S2's parity is measured
   against still read the position the `.TRG` stores.
3. **The water surface**. The classic path already moves its water vertices
   and brightens them, and stage S2 reproduces both exactly. What is left is
   the tile scroll, and where the remaster authored none, the marked vertices
   get a small wobble of their own UV instead - four texels of a 512 page, on
   the engine's `wibble` counter, dying with `tomb5.shimmer` because the
   selector does. **No caustic**: TR5 has one, it is the `WaterTable` shimmer
   of stage S2, and a second one would count it twice.

**The clock.** A speed needs a counter that runs forward, and the engine has
exactly one: `GlobalCounter` (`control.cpp:262`), stepped once per game update
in the same loop as `wibble`, standing still whenever the game stands still.
It is a `ushort` and turns over every 36 minutes, so what `hd_level.cpp` keeps
is the accumulated difference. No `timeGetTime`, no `steady_clock`: a frame
drawn twice while the game is paused draws the same picture.

**The switch.** A new registry value `hd_motion`, `1` by default, no options
row - the same shape and the same reason as `hd_objects`. `0` and an HD room
is the still photograph again; `hd_enable` off and it is not read at all.

**The named loss: material 13.** The remaster's own flipbook is counted and
not drawn. A range names one page, and a four frame animation names four; the
page would have to be chosen at draw time out of the part's parameters rather
than at build time out of the vertex layer. It is a handful of parts in five
levels - screens and monitors - and the log and `RoomProbe -animtex` say
exactly how many, per level and per room, so the size of the gap is a number
and not a shrug. The classic sequence ranges have no HD counterpart at all
where material 13 is absent, because the classic tile numbers and the HD page
numbers are two different name spaces with no bridge between them; that is the
same loss seen from the other side.

**The proofs.** `RoomProbe` gained `-time <seconds>` (which also sets the
wibble counter to the value the engine would have at that second, so two
`-time` values are two moments of the whole picture), `-water`, `-nomotion`
and `-animtex`. Pictures at two moments: the Rome courtyard fountain
(`ANDREA1` room 59) - the falling water scrolls and the banner on the left
wall sways, 7.3 % of the frame changes and nothing else does; `RICH2` room 83,
the shaft whose classic twin is the `UVROT` tile; `JOBY4` room 123, where the
kelp bends and the rock walls do not (1.9 % of the frame, all of it in the
columns the kelp occupies).

#### Stage S6 — the texture budget, the small card, and the missing page

Stage S1 put every page on the card exactly as the Remaster drew it, because
that is what the user chose: lossless, whatever it costs. This stage is what
happens when the card cannot pay. Two new registry values, both registry-only
and neither in the options menu, because neither is a taste:

| Value | Type | Default | Meaning |
|---|---|---|---|
| `hd_texture_budget_mb` | DWORD | `0` | The ceiling on the HD pages, in mebibytes. `0` = work it out from the card. |
| `hd_texture_format` | String | `rgba` | `rgba` = A8R8G8B8, lossless. `dxt5` = BC3, a quarter of the memory. |

**What a level actually costs**, measured over all fifteen, in mebibytes of
A8R8G8B8 on the card (`RoomProbe -nodraw` for the rooms, `ModelProbe -all
-withrooms` for the models, `ModelProbe -lara` for the wardrobe):

| Level | Rooms | Models | Lara | Total | Vertices | VB+IB |
|---|---:|---:|---:|---:|---:|---:|
| ANDREA1 | 279.9 | 61.3 | 25.3 | 366.5 | 955,148 | 32.9 |
| ANDREA2 | 331.9 | 64.0 | 28.0 | **423.9** | 1,814,770 | 62.4 |
| ANDREA3 | 251.9 | 57.3 | 28.0 | 337.2 | 1,095,516 | 39.7 |
| ANDY1 | 297.2 | 62.7 | — | 359.9 | 657,035 | 24.4 |
| ANDY2 | 295.9 | 34.7 | — | 330.6 | 554,726 | 22.0 |
| ANDY3 | 283.9 | 61.3 | — | 345.2 | 673,746 | 27.3 |
| JOBY2 | 251.9 | 62.7 | 26.7 | 341.3 | 1,082,551 | 42.1 |
| JOBY3 | 204.0 | 69.3 | — | 273.3 | 713,265 | 24.7 |
| JOBY4 | 167.9 | 10.7 | 12.0 | **190.6** | 269,655 | 10.7 |
| JOBY5 | 219.9 | 50.7 | — | 270.6 | 862,703 | 31.5 |
| RICH1 | 246.6 | 60.0 | 26.7 | 333.3 | 556,988 | 19.9 |
| RICH2 | 319.9 | 65.3 | 25.3 | 410.5 | 1,427,263 | 50.8 |
| RICH3 | 206.6 | 72.0 | 28.0 | 306.6 | 701,878 | 24.9 |
| RICHCUT2 | 194.6 | 0.0 | 26.7 | 221.3 | 44,268 | 1.3 |
| TITLE | 375.8 | 5.3 | 16.0 | 397.1 | 1,853,159 | 61.6 |

A dash is a level where Lara stays classic today — young Lara in the Irish
levels, and the two levels whose outfit byte is the ponytail the randomizer
also writes for a Tomb Raider III guest (stage S7's marker). The three columns
add up without overlap: a page number the rooms use and a page number a model
uses are different name spaces in the Remaster's own art, so nothing is paid
for twice — measured, not assumed (`-withrooms` appends the models to the
cache the rooms filled and reports what it really added).

**The automatic budget.** `DXCreateD3DDevice` asks `GetAvailableTextureMem()`
one instruction after the device exists — the only moment the answer means
what it says, because nothing has been loaded yet — and `hd_level.cpp` takes
off what else has to live on the card: the back buffer and depth buffer out of
the present parameters, `nTextures` classic pages of `App.TextureSize` squared
(measured 7 to 96 pages, 1.8 to 24.0 MiB, never near the 256 the array holds),
this level's HD vertex and index buffers, which are made *before* the pages,
and 64 MiB of slack for the driver. The floor is 16 MiB. For the title screen
the reserve is 155 MiB against 397 MiB of pages, so **any card offering 552 MiB
loads it whole**; for ANDREA2, the heaviest, the number is 595 MiB. A two
gigabyte card is nowhere near either.

**What happens over budget: mip 0 goes, pages never do.** A missing page is a
hole in a wall and reads as a rendering fault; a page that starts one level
down is a softer wall and nothing else, because the texture coordinates are
0..1 of whatever the texture turns out to be. So the pages are ordered by what
they cost, biggest first, and the biggest give up their top level until what is
left fits — repeatedly, if one pass is not enough, and never past a page's last
level. The sort is **stable**, which matters: 2391 of the Remaster's 2406 pages
are the same size, so almost every comparison is a tie, and an unstable sort
would pick a different set each run and make two screenshots of the same wall
disagree for no findable reason.

The whole decision is taken from the pages' 148-byte DDS headers before a
single block is decoded (`BC7DDSShape`, new in `bc7.cpp`). Stage S1 estimated a
page's cost as four times its file length; an estimate is not a thing to
promise a card. The estimate now equals the outcome to the mebibyte — TITLE
plans 375.8 MiB and loads 375.8 MiB.

**The format switch.** `hd_dxt5.cpp` is a BC3 encoder of our own, about 150
lines: the bounding box of each 4x4 block pulled in by a sixteenth at each end,
endpoints quantised to RGB565, indices chosen as the nearest of the four
palette entries in the colour *the card* will produce, with the classic 2:4:1
weighting; alpha always in the eight-value mode. No `d3dx` — this port depends
on `d3d9.dll` and nothing else. The file carries its own decoder as well, which
the engine never calls and the proof does: `RoomProbe -psnr` decodes a page,
compresses it, decodes it back and prints the peak signal to noise ratio, so
the quality is a number rather than an opinion. **Measured over sixteen pages
of ANDREA1: mean 40.28 dB, worst 35.41 dB, best 41.54 dB**, and the pages go
from 279.9 to 70.0 MiB — a quarter, exactly. It costs 1.46 s of compression on
eight cores, and it *lowers* the peak process memory (270 → 136 MiB) because
the pixels are let go on the worker thread as soon as the blocks exist.
`rgba` stays the default and the recommendation.

**The card's own limits, asked and not assumed.**

| The device says | What happens |
|---|---|
| no `vs_3_0` / `ps_3_0` | classic for the session, one line in the log |
| fewer than 320 pixel shader slots | classic for the session |
| no `UBYTE4` vertex element | classic for the session |
| no `A8R8G8B8` texture (`CheckDeviceFormat`) | classic for the session |
| no `DXT5` while `hd_texture_format = dxt5` | quietly back to `rgba`, one note |
| `MaxTextureWidth/Height` below a page | that page starts far enough down to fit, one note each |
| `MaxVertexIndex` = 65535 | see below |
| `CreateTexture` out of video memory mid-level | the **whole** level back to classic, naming the page |

That last one is a change of behaviour, not only a new message. Before, a page
that failed to upload left its slot empty, the draw calls naming it were
skipped, and the surfaces simply were not there — a level half in HD with holes
in it. Now an out-of-memory answer stops the load, `HDRenderLevelBegin` fails,
and `HDLevelBuild` puts the level on the classic path as a whole. The same
rule was extended to the models: `HDModelLevelBegin` used to note a page
failure and carry on building models whose draw calls named textureless slots,
which would have made the enemies *invisible* rather than classic.

**The 16 bit index buffer — checked, and honestly reported.** The plan asked
whether the index buffer could be split for a card without 32 bit indices. It
can be, in principle: every draw call here is one range, and a range can be
given 16 bit indices rebased on its own lowest vertex with the vertex stream
moved to that vertex (`SetStreamSource`'s byte offset, which is plainly inside
what such a card can address, unlike adding a base to the index). That path is
built and it runs today — RICHCUT2 has 44,268 vertices and uses it, and its
picture is byte-identical to the S5 build's.

What cannot be done is the *large* levels, and the reason is a measurement.
Cutting a range only helps while the pieces stay inside a 65536 vertex window,
and the floor under that is one triangle, which cannot be cut. The `.TRG`
numbers its vertices over the whole level rather than room by room, so the
three corners of one wall tile sit far apart: the widest single triangle
reaches over **235,507 vertices in JOBY4 and 1,391,696 in RICH2**, and only
RICHCUT2 (33,585) is under the line. So such a card draws RICHCUT2 in HD and
every other level classic, with the number in the log. Making the rest work
would mean a second vertex buffer layout compacted per room, for hardware that
has not shipped since about 2002; it is not built, and this paragraph is the
record of why.

**The missing page, and a bug found while checking it.** TROne's rule is that a
page which cannot be found is white and never black, and stage S1 followed it
by reaching for the Remaster's own `TEX\WHITE.DDS`. Measured now: **that file
exists in `4\TEX` only** — not in `5\TEX`, not in `6\TEX`. An installation
without the fourth game therefore had no white tile at all, the page ended with
no texture, its draw calls were skipped, and the wall was not white but *gone*.
The cache now makes four white texels itself when nothing can be found, so the
rule holds without any file on disk; `WHITE.DDS` is still preferred where it
is there. Proved with a bundle holding only `5` (a junction, nothing copied):
RICH3's three pages that live in `4\TEX` go from `3 failed` to `3 white, 0
failed`, the rooms load, and Lara's nineteen wardrobe parts — the whole
wardrobe lives in `4\ITEM` — report "is in no ITEM folder - classic", one line
each, and she draws the classic way.

**The level change holds one level, not two.** `HDLevelBuild` releases the
previous level's objects, buffers and pages at its top, before it builds
anything, and `HDLevelUnload` makes no Direct3D call at all (it runs on the
load thread). `RoomProbe -seq TITLE,ANDREA2,TITLE` walks the same road without
the game: free texture memory returns to its starting value after every level,
and TITLE's numbers repeat exactly the second time (3658 MiB free both times).
The process's own commit rises about 43 MiB per large level for the first five
and then stops — the C runtime keeping large freed blocks rather than a leak;
a small level (RICHCUT2, 1.3 MiB of buffers) shows 4 MiB of drift over four
builds, which is the same effect in proportion.

**New and changed proofs.** `RoomProbe` gained `-budget <MiB>`,
`-format rgba|dxt5`, `-psnr`, `-caps maxtex <n>`, `-caps noidx32`,
`-seq <A,B,C>` and `-vramhog`; `ModelProbe` gained `-withrooms <LEVEL>`. The
two `-caps` switches bend the caps `HDRenderInit` read from the device
(`HDRenderCapOverride`, which the engine never calls and for which there is no
registry value) — a fallback nobody can run is a fallback that does not work.
`-vramhog` fills the card with 64 MiB blocks until it refuses, which is the
only way to see the out-of-memory path: 1477 blocks taken, 0 MiB left, and the
level then refuses with `page 130: the card ran out of video memory after
19 MiB - the level goes back to classic`.

**Regression.** ANDREA1 rooms 12 and 69 and the three separate passes of room
19, and RICHCUT2 room 3 through the 16 bit path, are byte-identical to the
same pictures out of a build of the S5 commit. Nothing about a normal card's
picture changed in this stage.

#### Stage S7-2 — the engine reads what the randomizer marked

Stage S7-1 (`TR5Randomizer`) put three facts into the level that the Remaster's
art cannot know. This stage reads them. Everything here sits behind
`hd_enable` and changes nothing whatever in a vanilla installation.

**1. `data[3]` — which Tomb Raider III outfit Lara wears.**
`specific/file.cpp` reads the fourth word of the 32-byte header block into a
new `GfHdOutfit`, beside the three it already read, and `hd_laradraw.cpp` hands
it to `HDLaraOutfitFor` **unchanged**: the randomizer writes the engine's own
`HD_LARA_OUTFIT_*` numbers, so neither side translates. A `static_assert` in
`hd_level.cpp` holds the enum to 5..8. The engine accepts 5..8 and only those —
1..4 are the vanilla bodies, and letting one of them through would let a stale
byte overrule the level's own `LaraType`.

With the marker in hand the `LARA_PONYTAIL` byte means one thing again instead
of two, so a level that carries it *and* a `data[3]` of 0 now draws
`OUTFIT_CLASSIC` — the Rome body inside a Russia level — where it used to stay
classic. Measured over all seventeen shipped files: not one carries that byte,
so nothing vanilla moves.

**2. The young-Lara test was wrong, and it was the level range.** A spiel test
found every randomized ANDY level reported `young Lara stays classic` with a
grown Lara standing in it. The engine builds the child body at exactly two
places — `LaraInitialiseMeshes` in `game/laramisc.cpp` and its twin in
`game/deltapak.cpp` — and **both ask the Irish level range AND the script flag
`GF_YOUNGLARA` together**. The randomizer clears that flag out of `SCRIPT.DAT`
(`core/ScriptDat.cpp`), because young Lara can carry no weapon at all, and
replaces her skin with an adult's; the range on its own is therefore not
sufficient and never was. The test is now the flag or the `LARA_YOUNG` outfit
byte, both of which change when she changes.

Measured, seed 380: vanilla ANDY1/2/3 carry `data[0] = 1`, which is
`LARA_YOUNG`, and the flag; the randomized three carry 2, 0 and 2 —
`LARA_BUNHEAD`, `LARA_NORMAL`, `LARA_BUNHEAD` — and no flag. `ModelProbe -lara`
says `young Lara stays classic` for the vanilla file and `OUTFIT_RUSSIA` for
the randomized one. The same hole was in `ModelProbe` itself and is fixed there
too.

**3. The `.hdm` sidecar.** New `specific/hd/hd_markers.cpp` — engine-free, like
`hd_bind` and `hd_scan`, so the diagnostic tools run the very same reader. The
parse loop is sixty lines: `hdm 1` or the file is refused whole, a stray `\r`
tolerated, an unknown line ignored **and counted**, so that a newer randomizer
talking to an older engine is visible rather than silent, and a missing file
"no markers". `HDLevelLoad` reads it from the `.TRC`'s own path with the
extension swapped, so it is found wherever the level is.

A `room` line makes that room classic **even where the `.TRG` has a full group
for it** — a third reason beside `no_group` and `empty_group`, and the only one
that throws HD art away. It has to: when a run cuts a wall open, the Remaster's
art still shows the wall, and nothing else in the file gives it away.

A `donor` line names a slot whose model came out of another level file. The
Remaster resolves a slot by the name *this* level gives it, and `ANIMATING10`
is a different object in each of the fifteen — so `hd_object.cpp` asks
`HDBindDonorSlot`, which loads the **donor's** `LEVELn.MAP` (cached per donor
level: one file opened however many slots came from it) and answers with the
donor's own name for the source slot. A donor slot that resolves to nothing is
left out of the model list entirely rather than bound by its own name — the
slot is called `SAS` and holds a T-Rex, so `SAS.TRM` would be a soldier on a
dinosaur's skeleton.

**4. `ROMAN_GOD` without a map entry.** Measured over the shipped files: there
is no `ROMAN_GOD.TRM` at all. `5\ITEM` has `ROMAN_GOD_ANDREA_SWORD` and
`ROMAN_GOD_ANDREA_HAMMER`, and of the fifteen `.MAP` files exactly two mention
the slot — ANDREA2 says sword, ANDREA3 says hammer. Every other level that
carries the god therefore fell through to its own name, found nothing and drew
him classic, and the randomizer makes that the common case. The hand table now
carries a twelfth line, `ROMAN_GOD` → `ROMAN_GOD_ANDREA_SWORD`, and it is only
ever consulted **when the level's own `.MAP` is silent** — so ANDREA2 and
ANDREA3 keep their own answers, and no vanilla level changes at all, those two
being the only ones that carry the slot.

**5. The guests out of Tomb Raider III.** Their models are in the *other*
bundle, `Tomb Raider I-III Remastered`, under `3\ITEM`. Three things were
needed:

* `hd_install.cpp` looks for `tomb123.exe` with a subfolder `3` carrying `DATA`
  and `TEX`, in the **same walk** as the IV-VI bundle — they sit side by side in
  the same Steam library — with the registry override `hd_install_tr3`. Not
  finding it costs those four models and nothing else.
* `HDModelSlot::fromTr3` sends one slot's model lookup into that bundle and
  **only** into it (game 3, then the siblings 1 and 2), never into the IV-VI
  folders, and never the other way round.
* The page cache had to learn that page 12 of Tomb Raider III and page 12 of
  Tomb Raider V are two different pictures. A guest's pages are offered with
  `HD_PAGE_ALT_BASE` added; the cache keeps the number as it is and takes the
  base off again at the one place a number becomes a file name.

**The names of the four** had to be derived without an installation to look in,
because the user's copy may be modded and will be fetched fresh later. TRForge
measured that the Remaster names a model after the **engine's own symbol** for
the slot unless the level's `.MAP` says otherwise — keyed on the English words
it found 60 of 211 `.MAP` keys, keyed on the symbol 197 — and that rule holds
across all five games. TRForge's generated TR3 symbol table then gives the four:

| TR5 slot | Its TR5 name | TR3 slot | TR3 symbol | `.TRM` expected |
|---:|---|---:|---|---|
| 31 | `SAS` | 287 | `TREX` | `TREX.TRM` |
| 43 | `CROW` | 29 | `VULTURE` | `VULTURE.TRM` |
| 53 | `SAILOR` | 34 | `COMPY` | `COMPY.TRM` |
| 75 | `REAPER` | 288 | `RAPTOR` | `RAPTOR.TRM` |

A `static_assert` in `hd_level.cpp` holds those four slot numbers against the
enum. **The names are an inference, not a measurement** — nothing here has an
I-III installation — so the log says out loud which way it went:
`tr3_not_installed`, `tr3_model_missing`, or the model with its origin.

**The log.** Per level: `HDLOG level=… markers file=… rooms=N donors=N
outfit=N tr3_install=found/absent`, a `MISMATCH` line if `data[3]` and the
sidecar's `outfit` disagree (the header byte wins), one line per line the
reader had no rule for, `reason=marker` on the room lines it touched, and one
`HDLOG donor slot=… name=… from=LEVEL:SOURCE model=… hd=0/1 reason=…` per donor
slot. The Lara line gained `laratype=` and `data3=`, so a garbled header byte
is visible rather than merely ineffective.

**Proof without the game.** `MapProbe` gained `-hdm` and `-tr3`: it compiles
`hd_markers.cpp`, `hd_bind.cpp` and `hd_lara.cpp` and prints the engine's own
answer for every line of a real sidecar. Over a run of seed 380 with the
glitched secret list:

| Level | Rooms marked | Donors | What the table shows |
|---|---|---:|---|
| ANDREA3 | 1, 74 | 4 | the two rooms that list reshapes, and `LARSON` out of ANDREA1 |
| RICH3 | 46, 85, 86, 120, 121, 150 | 14 | exactly the six rooms of its three portals |
| JOBY3 | 10, 76 | 10 | `donor 203 KEY_ITEM8 <- joby2:203` resolves to **`KEY_ITEM8_1`** out of JOBY2's map, which JOBY3's own map does not name at all — the donor lookup doing the one thing it exists for |
| RICH1 | 129, 130 | 15 | `donor 43 CROW <- TR3:43` → `VULTURE`, and `data[3] = 5` → `OUTFIT_TR3_ANTARC` |

A vanilla level answers `no markers … no sidecar file`; a sidecar with `hdm 2`
is refused with its reason; a sidecar with CRLF line endings and an unknown
line is read correctly and the unknown line reported. `HdmDump`, the reference
reader the format was agreed against, gives the same answers on the same files.
`ModelProbe -lara` on the randomized RICH1 draws the Antarctica outfit out of
`data[3]` on that level's own Lara skeleton, mean drift 4 and worst 143 against
a limit of 256.

**What is not proved here.** Whether the Remaster really draws those four names
for the four guests, and whether a guest's skeleton fits — both need an I-III
installation, and the code path answers "classic", in the log, when it is not
there. The eleven hand-table pairings and the `ROMAN_GOD` default are still
measurements by picture, as they have been since S0.

---

**Re-measured after this stage** (2026-09-05), counting every source file under
`TOMB5 Code` (`.c`, `.cpp`, `.h`, `.hlsl`, `.inc`) plus the four licence and
notice files that came with the vendored code: **173** files changed or added,
**158** of them carrying the modification notice (stage S6 added two,
`specific/hd/hd_dxt5.cpp` and its header, and stage S7-2 two more,
`specific/hd/hd_markers.cpp` and its header).

Fifteen do not, and none of them should. Fourteen are the ones already named
above — `tomb5/libs/stb/stb_vorbis.c`, `tomb5/libs/discordRPC/LICENSE`, the
eight under `vendor/trforge/` and the four `specific/hd/hd_shader/*.inc`. The
fifteenth is `resource.h`, which the earlier count had missed: it is a
*Microsoft Visual C++ generated include file* and says so in its first line, so
a notice put into it would be wiped by the next use of the resource editor.

---

#### Stage S8 — first person, the binoculars, and the neck in the camera

The first person view (#126) hangs the camera in Lara's head joint. The classic
path answers that by leaving the head *mesh* out, and for a classic body that
is the whole of it: `LM_HEAD` is head **and** neck in one piece. An HD body is a
single skinned surface, and the neck stump under the head is weighted to the
**torso** — so masking node 14 takes the head off and leaves the stump standing
in the camera. The user photographed it: looking down, her neck and chest fill
the frame.

**Where the eye is, measured.** `FirstPersonCamera` (`game/camera.cpp`) reads
the head joint with `GetLaraJointPos(&head, LMX_HEAD)`, lifts it 16 units, and
asks `mgLOS` whether it may go `FP_EYE_OFFSET` = 48 units forward. Both answers
are ordinary play — 48 in the open, 0 with a wall, a ledge or a doorway in
front — so both were measured. `ModelProbe -neck` over all eight wardrobe
bodies, against ANDREA1's own Lara skeleton (head joint 1 / −247 / −12):

| outfit | stump verts | eye 48 forward: stump / shoulder / chest | eye at the joint: stump / shoulder / chest |
|---|---|---|---|
| `OUTFIT_CLASSIC` | 1281 | 41.9 / 79.8 / 74.6 | 18.6 / 73.5 / 73.6 |
| `OUTFIT_RUSSIA` | 1679 | 40.4 / 77.8 / 73.5 | 18.6 / 71.6 / 81.9 |
| `OUTFIT_VCI` | 1235 | 37.4 / 78.3 / 74.6 | 18.6 / 71.9 / 73.5 |
| `OUTFIT_SUBSUIT` | 2210 | 32.7 / 74.7 / 87.8 | 22.2 / 68.9 / 99.7 |
| `OUTFIT_TR3_ANTARC` | 2093 | 41.9 / 78.2 / 74.5 | 19.1 / 71.7 / 78.2 |
| `OUTFIT_TR3_NEVADA` | 937 | 41.9 / 79.0 / 74.6 | 19.1 / 72.7 / 81.2 |
| `OUTFIT_TR3_COAST` | 930 | 41.9 / 79.0 / 76.3 | 19.1 / 72.7 / 75.3 |
| `OUTFIT_TR3_CATSUIT` | 940 | 41.9 / 78.6 / 74.6 | 19.1 / 72.4 / 81.3 |

"stump" is the nearest torso-node vertex above the shoulder line, "shoulder"
the nearest of nodes 8 and 11, "chest" the nearest torso-node vertex below that
line. With the eye at the joint the stump reaches **18.6 units** — inside the
near plane, which is 20 (`InitWindow`, `gamemain.cpp`). The nearest thing that
must **survive**, over all sixteen columns, is **68.9**.

**The cut is a sphere, and that is the point.** The first shape tried was the
obvious one, a cut at a fixed view *depth*: `clip(viewPos.z - gNearCut)`. It
works looking straight ahead and fails looking down, because a depth is a plane
across the view, and at 75° down that plane lies flat across her chest. The
picture is in the report: at 72 units of depth the near cut took her collarbone
along with the stump and one saw the floor through the hole. A cut at a fixed
**distance** cannot do that — the stump is 18 to 42 units from the eye and the
nearest shoulder or chest vertex is 68.9, so a single radius separates them
from *every* angle at once. `HD_LARA_FIRST_PERSON_NEAR_CUT` is **60**: 8.9
units of margin below the nearest geometry that has to stay, which is what the
animation needs, since a shrug moves the shoulder relative to the head and the
table does not.

**And her body is drawn one-sided while the view runs.** This is the half the
brief did not ask for and the pictures insisted on. The HD path draws every
model double sided (`D3DCULL_NONE`, `hd_render.cpp:1395`), which is what TROne
does as well (`glDisable(GL_CULL_FACE)`) and what the classic engine *looks*
like it does — `specific/function_table.cpp:105` sets `D3DCULL_NONE` for the
whole session. It only looks like it: TR5 culls back faces in **software**, per
polygon, through the `IsVisible` function pointer, so the card never sees one.
Nobody had ever noticed the difference, because no camera stands inside an
object — except this one, which stands inside Lara. With the camera in her neck
the inside of her own chest is the entire picture, and cutting a hole in a
closed body only shows more of it. The winding of the wardrobe carries the
culling: over the eight bodies, 25,384 to 39,440 triangles each, at most 59 of
them have their geometric normal on the other side of the vertex normal
(0.06 % to 0.18 %). `D3DCULL_CCW` is the mode that keeps the outside — measured,
not assumed: from a normal camera the same picture with `CCW` and with `NONE`
has the identical background share and the identical mean brightness (162.1 of
255), while `CW` differs (160.1). Outside first person the culling is therefore
a no-op, and it is only ever asked for there.

**Both are per draw, and only her body asks.** `HDModelDraw` gained a `nearCut`
and a `cull` argument; the cut rides in `gPass.z`, the pixel-side constant
`hd_model.cpp` already wrote per pass, and the cull mode is set and put back
around the model's own draw calls. Every other model — every item, and Lara's
hands, holsters and back weapon, which are models of their own — gets 0 and
`HD_MODEL_CULL_NONE`, which is why the cut can sit close enough to take the
neck and still never take a hand that comes up to the camera. The register
table at the top of `hd_model.hlsl` names `gPass.z` accordingly; it is the one
component where the two shaders differ, and it is safe because `hd_model.cpp`
writes `c4` to the **pixel** constant file alone while the room shader reads
its own `gPass.z` in the **vertex** half.

**The binoculars and the laser sight never reach the seam.** Not by a check in
`hd_laradraw.cpp` — the engine does not get that far. `BinocularCamera` sets
`lara_item->mesh_bits = 0` (`game/camera.cpp:2185`) for the scope and the laser
sight both, and `game/draw.cpp:982` asks `if (lara_item->mesh_bits &&
!SCNoDrawLara)` before it calls `DrawLara` at all; leaving the scope puts
`mesh_bits` back to −1 in the same function (`:2173`). So nothing of her is
pushed while it is up, and there is nothing to leave behind when it comes down.
The scope's mask lies above the HD world by the order stage S1-2 chose:
`HDFrameDraw()` runs immediately before `DrawBuckets`, and the depth clear that
separates the world from mask, pickup display and fade (#127) comes after all
of it — `DrawBinoculars` is drawn after that clear and is untouched. `AlterFOV`
moves `f_persp` *inside* the frame while zooming, which is why S1-2 takes the
projection when the world is collected (`HDFrameCaptureProjection`) rather than
when it is drawn. The black frame on zooming out is the vanilla inheritance
#127 identified and is not touched here.

**The braid.** Not drawn in first person, and its simulation runs on
regardless: `HairControl` is called from `game/control.cpp:583`, in the game
loop, not from `DrawLara`. Switching back to third person therefore shows it
hanging where it belongs on the very next frame — the same as the classic path,
which skips `DrawHair` in exactly the same way (`specific/drawlara.cpp:214`).

**The log.** `HDLOG firstperson on|off nearcut=…` on the edge, once, when the
view is switched; `HDLOG binoculars lara_pushed=N mesh_bits=N` one frame after
the scope comes up, when the frame it describes was wholly inside the
binocular view, and `HDLOG binoculars off` when it goes down. The per-frame
Lara line carries `nearcut=` as well.

**The proof, without the game.** `ModelProbe -firstperson` now moves the camera
as well as the mask: it goes where `FirstPersonCamera` puts the eye — the head
joint of the level's own skeleton, 16 up and `-fpforward` units in front,
default 48 — and looks along +z, with `-pitch` as the look-down angle.
`-nearcut` and `-cull` switch the two halves for a before-and-after picture,
and `-neck` prints the table above with a PASS/FAIL against the constant.
Pictures made this way, at the eye against a wall (`-fpforward 0`), 60° down:
`OUTFIT_CLASSIC` and `OUTFIT_VCI` before — the inside of the shirt and of the
catsuit filling the frame — and after, where the collar is a rim seen from
above, her body is see-through where the camera is inside it, and her belt,
her legs and her boots are below; plus the pistols drawn and the view level,
where both hands and both pistols are whole, untouched by the cut.

*Left over, and named:* in the **diving suit** the helmet rides on the torso
node too. It cannot be masked with the head and it is far too big for a
60-unit cut, so looking down inside it shows the helmet from within. The
classic suit hides it because there the helmet is part of the head mesh.

This stage adds **no new source file**, so the file count above still stands:
it changes `hd_lara.h`, `hd_model.h`, `hd_model.cpp`, `hd_object.h`,
`hd_object.cpp`, `hd_laradraw.cpp` and `hd_shader/hd_model.hlsl` (plus its two
generated `.inc` files), all of which already carried the notice.

---

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
