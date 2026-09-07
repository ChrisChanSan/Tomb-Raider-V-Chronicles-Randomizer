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

Measured again on **2026-09-07**, after order 1 of "Sixty frames a second"
(section 5 below): **113** files changed, **62** entirely new - **175** in all,
**161** of them carrying the notice. The two additions are
`specific/profile.cpp` and `specific/profile.h`. One file that had been changed
by an earlier stage and never given the notice - `game/hitman.h` - was given it
with this change, which is why the notice count grows by three and not by two.

Fourteen of the 175 do not carry it, and none of them should - the ten below
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
switches with `SetCullCW` / `SetCullCCW` around the reflected draw.

*Corrected in fix round C.* This paragraph used to end by claiming that TR5's
mirror "does not even produce a negative determinant" and is a rotation. It is
not, and the sentence was wrong. `game/mirror.cpp` negates `x_rot` and `y_rot`,
adds half a turn to `z_rot` and then calls `CalcLaraMatrices(2)`, whose flag-2
branch scales the matrix by −1 on **all three** axes
(`game/delstuff.cpp:124`, `ScaleCurrentMatrix`). Three sign changes are a
determinant of −1, and the composition works out to exactly the reflection in
the mirror's z plane — which is the whole point, and which is why the classic
engine has to turn its own test round for the length of that draw. So the
winding really does flip, and "the HD path culls nothing" is not a lucky
coincidence but the actual reason the reflection comes out whole.

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

**The mirror needs nothing, and here is why.** TR5 mirrors Lara by *moving*
her: `game/mirror.cpp` reflects the z position in the plane, negates `x_rot`
and `y_rot`, adds half a turn to `z_rot`, and `CalcLaraMatrices(2)` then scales
the matrix by −1 on all three axes. That is a determinant of **−1** — a
reflection, as a mirror image must be — so every triangle's winding flips, and
the classic engine turns its own software backface test round with `SetCullCW`
/ `SetCullCCW` for the length of that draw. The HD path needs no such switch
for one reason only: its state bracket sets `D3DCULL_NONE` and everything it
draws is double sided, so a flipped winding is invisible to it. The hair is
mirrored in world space by that same function, so the braid follows for free.

*Corrected in fix round C.* An earlier wording gave the reason the other way
round — that TR5's mirror was "a rotation" and produced no negative
determinant. It does produce one, measured above, and the one frame where that
matters is first person: there, and only there, Lara's body is drawn with a
back-face cull (stage S8), so the reflected draw now culls the **other** side.
Everywhere else nothing changes, because nothing else culls.

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

#### Fix round A - the rooms (#123)

The first play-through of the whole HD path produced a list of faults. These
are the five of them that live in the room renderer. Each was measured against
the remaster's own files or the classic path before anything was changed, and
each carries the numbers that decided it. **No new source file**: the round
changes `game/control.cpp`, `specific/hd/hd_level.{h,cpp}`,
`specific/hd/hd_render.{h,cpp}`, `specific/hd/hd_pagecache.{h,cpp}`,
`specific/hd/hd_light.cpp` and `specific/hd/hd_shader/hd_room.hlsl` (with its
two generated `.inc` files), plus the diagnostic tool `RoomProbe`.

**A1 - a flipped room kept the art it no longer had.** `FlipMap`
(`game/control.cpp:2634`) does not move a room, it swaps the CONTENTS of two
`ROOM_INFO` records and leaves their numbers alone. Stage S1-2 drew room *i*
out of .TRG group *i*, so after a flip the wall that had gone was still
standing as an HD texture - the reported bug in the player's own words. The
identity is now a table, `g_room_group`, swapped in the same statement as the
records (`HDFlipRoom`, called from `FlipMap` itself), and everything decided
per room reads through it: whether the room is HD at all (which also decides
whether its static meshes are suppressed), which group's bounding box the
dynamic lights are culled against, and which group is drawn. It follows a
loaded save for free, because the engine restores a flip by calling `FlipMap`
once per set flip (`game/savegame.cpp:684`). The water marks need no thought:
they are built before any flip, on every room including the alternates, so
they already sit on the right group. **The proof:** the two halves of a pair
carry different art at the same place - ANDY1 179 has 450 triangles and its
partner 215 has 2,754; ANDREA1 200 has a barred window on a wall where its
partner 203 has none, and `RoomProbe` draws both from one camera. With `-log`
the level writes one `HDLOG flippair` line per pair and one `HDLOG flip` line
per swap.

**A2 - the kelp and the trees were solid rectangles.** The alpha test lived in
the alpha pass only, which is the measured rule for the pages that pass
usually gets. The remaster puts its vegetation in the OPAQUE pass instead:
material 8, 23,040 triangles of it in JOBY4 and 260,756 in ANDY1. Counting
texels below alpha 128 on the top mip settles it per page rather than per
material: JOBY4's kelp pages 3198 and 3257 are 52 % and 95 % hole, ANDY1's
tree pages 3268-3272 are 62-87 %, and every ordinary page - material 0's
walls, material 16's light shafts, material 2's glow - is 0 %. A page with no
hole cannot lose a pixel to a test it always passes, so the test is switched
on for the opaque pass exactly on the pages that have something to cut. The
threshold is the remaster's own: material 6 and material 8 carry 0.5 in
`param[0]` in every level. **The proof:** ANDY1 room 202, the same camera,
background 2.9 % -> 15.9 % and the picture goes from sheets of leaf texture
with hard edges to leaf silhouettes with sky between them.

**A3 - the light shafts were white surfaces.** The classic path splits a room
colour at 128 into a diffuse that stops at 255 and a specular that carries the
rest, and for its additive draw type it switches the specular off
(`specific/polyinsert.cpp:72`). So a classic additive polygon is `texel * 1`
at the very most; ours went on multiplying to `texel * 2` and blended it
ONE/ONE. Measured over the remaster's own vertices, channels of
colour1+colour2 above 1.0 in the additive pass: RICH1 **56.3 %**, mean 1.26,
max 2.00, of which material 2 alone is 81.6 %; ANDREA1 10.9 %, material 14
41.6 %. And the pages carry no weight to hold it back - JOBY4's shaft page
3255 is a flat grey with alpha 255 over the whole sheet. The ceiling is now
applied in the additive pass and only there (`gPass.y`), so the opaque pass
keeps the deliberate deviation stage S2 measured and argued for. **The
proof:** RICH1 room 50, where every vertex of the glow carries the full 2.0 -
the solid white blob becomes a glow with the grating, the floor and the far
panel visible through it again.

**A4 - the fog bulb was a sphere with an outline.** The chord through the
sphere is the exact classic formula and exact is the trouble: `2*sqrt(r^2 -
d^2)` has an infinite derivative at `d = r`, so the intensity stands up
vertically at the silhouette. The classic path has the identical formula and
does not show it, because it evaluates it once per VERTEX and lets the
rasteriser smear the cusp over a whole polygon. A per-pixel version cannot
borrow that, and rebuilding it would give up the one thing per-pixel is better
at, so the chord is weighted with a `smoothstep` over its own range instead:
one at the centre, zero with zero slope at the rim. `hd_light.cpp` carries the
same three lines behind a `soften` flag, so `RoomProbe -paritycheck` runs the
classic formula and the shader formula side by side and still says **PASS**
(worst contribution 0.999 of 255, budget 2); the deviation appears where it
belongs, in the final-brightness row (229.1 -> 239.0 on a white texel under a
4096-unit bulb). One quirk goes with it: the branch that returned one density
for a bulb behind the camera now returns zero, which is what the classic path
truncates it to anyway.

**A6 - the lamp post swayed.** `TrgVertex::sway` is a weight, not permission,
and stage S5 read it as permission. Counted over all 15 levels by the material
of the part that names them, the vertices with a non-zero sway are

| moves | | must not move | |
|---|---|---|---|
| material 8 | 416,153 | material 0 | 160,620 |
| material 1 | 92,896 | material 14 | 2,016 |
| material 7 | 46,611 | material 3 | 867 |
| | | material 16 | 594 |
| | | material 6 | 222 |

and the parts themselves say which column is which. **8 and 7** are the wind
material under two numbers — `param` is (alphaRef 0.5, strength, scale, scale)
in both, their pages are the cut-out sheets A2 is about, and no level carries
both (ANDREA3 has only 7, JOBY4 only 8; TITLE, which is a montage of every
level, has both). **1** is the flow material, the UV scroll of a river or a
waterfall, and its bend is the water's own — JOBY5's sub pens are 63,198 of
those 92,896, so taking it away would have caused the *other* reported fault,
the one about missing water animation. Everything else is ordinary geometry,
glass, glow and light shafts; in ANDREA1, the level the lamp post stands in,
material 0 alone is **61,208 of the level's 84,408** swaying vertices. The
weight is now taken away from every vertex that no part of material 1, 7 or 8
names, once, at level build, so the shader keeps its one multiplication.
ANDREA1 loses 63,610 of 84,408 and ANDREA2 63,022 of 89,141; ANDY3, JOBY2,
JOBY4 and RICH3 lose nothing at all, because there only the water and the
plants ever swayed. `RoomProbe` prints the split as an `A6:` line and the
level's log line carries `sway_parts=` and `sway_dropped=`.

*Measured and NOT changed, and why:*

- **A5, light that stops at a room border.** The HD path's dynamic lights are
  a line-for-line copy of `aBuildRoomletLights` (`specific/drawroom.cpp:1017`)
  and do cross a portal: the cull is the room's box grown by the falloff, and
  the falloff test inside the shader is a real distance test. What the picture
  shows is a hard-edged rectangle of unlit floor beside a lit one, which is a
  border in the BAKED art - either two rooms the remaster lit differently, or,
  more likely, an HD room next to one drawn classic. The player's own log
  names every one of those: 4 in RICH2, 9 in JOBY4, 10 in ANDY1, 14 in
  ANDREA3, all with `reason=empty_group` - rooms the .TRG has a group for that
  draws nothing. Closing that seam means lighting a classic room out of the
  .TRG's light cubes, which is a stage of its own and not a fix.
- **"the light rays are static".** Materials 2, 14, 15 and 16 carry four
  `param` floats each (material 16: `(0.1, 1.0, 0.035, 0)` in JOBY4,
  `(0.8, 0.7, 0.2, 0)` in JOBY5), and nothing in the .TRG says what they mean.
  Reading the remaster's own shaders is out of bounds for this project, so
  they are measured, written down, and not guessed at.
- **A7, the material 13 flipbook** (RICH1 7 parts, RICH2 3, JOBY5 3) and
  **A8, static meshes that cannot be shot to pieces**, are untouched in this
  round. A8's lead, for whoever picks it up: RICH2 carries five parts of
  exactly 2,226 triangles each whose material numbers are `0x8100`, `0x8200`,
  `0x8300`, `0x8400` and `0x8500` - bit 15 set and a counter in the high byte,
  which no other level does.

---

#### Fix round B - the objects (#123)

The other half of the same play-through. **No new source file**: the round
changes `game/deltapak.cpp`, `game/draw.cpp`'s neighbours only through the
seam it already had, `game/hitman.{h,cpp}`, `specific/hd/hd_bind.{h,cpp}`,
`specific/hd/hd_model.{h,cpp}`, `specific/hd/hd_object.cpp`,
`specific/hd/hd_pagecache.{h,cpp}` and `specific/hd/hd_shader/hd_model.hlsl`
(with its two generated `.inc` files), plus the diagnostic tool `ModelProbe`.

**B0 - the level's own .MAP was never in hand when the models were built.**
This is the root of two of the reports and it was found by reading the
player's own log against the tool. `hd_scan` reads `<LEVEL>.MAP` during the
level scan and frees it again at the end of that scan, and nothing read it
back - so `HDModelLevelBegin` bound every slot by the third and last of
`hd_bind.h`'s three rules, the slot's own name. The log says it plainly:
ANDREA1 built `loaded=46 dummy=0 missing=107` where the scan of the same level
over the same files had reported 55 bound and 14 DUMMY, and 18 of those 46
came out of **game 4** - `SAS.TRM`, `DOOR_TYPE1.TRM` and the rest of the names
TR5 inherited from TR4, drawn in Rome. `ANIMATING10` asked for
`ANIMATING10.TRM`, which is in no ITEM folder, while ANDREA1's map says
`ANIMATING10_ANDREA1`. The level's map is now loaded in `HDObjectLevelBuild`
(`HDMapLoadForLevel`) and freed in `HDObjectLevelEnd`. Donor slots are
untouched by it: the sidecar resolves them first and hands the name over
directly.

**B1 - statics and animatings drawn twice, vanilla and HD in one place.**
Stage S1-2 suppressed a room's own `MESH_INFO` list and argued that TROne's
case - a thing baked into the .TRG that a moveable also carries - could not
arise here. It does, and the user photographed it: ANDREA1's lamp post and the
lantern on its wall arm, each standing twice, slightly apart. **What a DUMMY
line in the .MAP means** is the answer, and it is Todesengel's own measurement
before it is ours (`vendor/trforge/src/trf_remaster/HdMesh.cpp`, the note at
`items_dummy`): `DUMMY.TRM` is a real file with no parts in it, 240 of the
2,940 bindings across both installs point at it, and the remaster is saying
*draw nothing here as an object* - "sometimes because it has baked the thing
into the room mesh instead, which is where the lanterns of Rome live". Since
B0 the engine never saw a single DUMMY line, so those objects fell through to
the classic mesh and stood inside their own HD copy.

Measured before changing anything, with a scratch tool that reads the classic
level and the .TRG together and counts the remaster's triangles inside each
object's own bounding box: in ANDREA1 every DUMMY slot that has items has the
art standing in it and no other slot does. The wall lantern (ANIMATING12,
eight items) carries 1,045 to 1,332 triangles in a box of 633x417x132 units -
169 per million cubic units; the alcove (SEARCH_OBJECT3) 44, the lamp post
(ANIMATING11) 59, the arch (ANIMATING1_MIP) 30, the waterfall (WATERFALL1) 12.
The highest of any slot the map does NOT call DUMMY is 4.0, and the big ones
sit at 0.01 to 0.04 - the room's own floor and wall inside an enemy's box.
`RoomProbe` draws both objects out of the .TRG alone, with nothing else in the
scene: ANDREA1 room 116 has the two lamp posts and room 2 the wall arm.

So a slot the LEVEL'S OWN map calls DUMMY is drawn by nobody, and a model that
reads but carries no triangle counts as the same statement. The test is per
ITEM and not per slot - only a room that really draws HD has the baked copy,
so a room the sidecar marked classic keeps its lamp post the old way - and it
sits ahead of the object path's own guard, because the second copy comes out
of the .TRG whether `hd_objects` is on or off. A DONOR slot is never baked:
its model came out of another level file and this level's art has never seen
it. The log carries `baked_slots=` per level, `reason=baked` per slot and
`baked=` per frame.

**B2 - transparency on objects.** Half of it was B1: the lantern glass the
player saw as solid white is the CLASSIC lamp standing in front of the HD one,
and the .TRG's own glass is see-through (the `RoomProbe` picture shows the
background through it). The other half was a measurement that said **no**. Fix
round A switched the alpha test on in the opaque pass for the pages whose
alpha really is a coverage mask (A2), and the obvious next step was to do the
same for models. It is wrong: `ROMAN_GOD_ANDREA_SWORD`'s three body pages
measure **64.1, 48.7 and 56.4 percent** of texels below alpha 128, and with
the test on, half the statue is discarded - the picture is unambiguous.
PIERRE and BLUE_GUARD share a page at 26.2 percent. An ITEM page's alpha is
not a coverage mask, and no threshold separates the two cases because a solid
statue sits ABOVE a leaf sheet. The models keep the remaster's own rule, which
is TROne's too: the alpha test runs in the alpha pass and nowhere else.
`hd_pagecache` gained `HDPageCacheSlotCutoutPermille` so the number can be
looked at, and `ModelProbe` prints it per page.

What DID change is the additive ceiling, A3's line in the model shader as
well: the classic path builds its colour in bytes and drops a channel over
255, while this shader carries the 128 split as a multiply of up to two. With
the engine's own neutral ambient of 128 the additive pass already draws at
1.004, and a lit object well above that. `gPass.y` is 1 in the additive pass
and 0 in the other two, so the opaque pass keeps stage S2's deliberate
deviation to the bit.

**B3 - the Mafia's empty fist and the cyborg without damage.** Stage S3-2
ignored mesh swaps on the argument that a .TRM is one whole model and carries
no second geometry for a node. The argument was wrong, and 5\ITEM says so:

- `MAFIA2_SWAP.TRM`. `MAFIA2.TRM` has his pistols in the shoulder holsters and
  empty fists, which is his INITIAL state - `meshswap_meshbits` 0x2400, the
  hands swapped out of MESHSWAP2 (`game/mafia2.cpp:37`). When he draws them
  the engine writes 0x80 instead, the TORSO swap, and `MAFIA2_SWAP.TRM` is the
  same man with the guns in his hands and the holsters empty. Two pictures,
  one bit - and the user's screenshot is the first one while he aims.
- `HITMAN_<n+1>.TRM`, eleven of them. The file numbers are
  5,6,7,8,11,12,13,14,15,16,20 and the cyborg's panel nodes are
  4,5,6,7,10,11,12,13,14,15,19 (`hitman_nodes`, `game/hitman.cpp`): the same
  eleven, off by one, 11 of 11. They are CUMULATIVE bodies, not single panels
  - along the opening order 16,15,14,7,6,13,8,5,11,12,20 their vertex counts
  climb 4115, 4099, 4310, 4301, 4489, 4734, 4972, 5381, 5469, 5555, 5548
  against the closed body's 4035 - so the right file is the one for the panel
  opened LAST, which `item_flags[0]` counts and `hitman_nodes` orders.

Both are ordinary models on pseudo slots: the same list, the same page cache,
the same skeleton, the same bind pose, and one entry in the frame list gets a
`drawSlot` instead of its own. Everything else keeps stage S3-2's answer,
which is measured too: the soldiers' hands need no second file because
`MAFIA.TRM` already holds the pistol in the hand, and so do SWAT, SWAT_PLUS
and the guards. `hitman_nodes` moved from a `static` in `hitman.cpp` into
`hitman.h` rather than being copied - one array, one truth.

**B4 - Pierre's neck and chest in his death animation.** A `<NAME>_HEAD.TRM`
carries a facial rig, so its bone numbers mean nothing to the mesh tree and
stage S3-1 nailed the whole file rigidly to the head node. Measured per bone
over all twelve head files of game 5: the file is not a face.
`PIERRE_HEAD.TRM` spans y -376..-177 while `PIERRE.TRM`'s whole body ends at
-286 and the head node sits at -342 - it carries head, NECK and a piece of
collar, reaching **165 units below the joint it was nailed to**. Turn that
node, which is exactly what a death animation does, and the neck and the
collar swing with the skull and tear a hole; the picture shows the head as an
open bowl with the collar black underneath.

The rig has a shape. Eight of the twelve files use both base bones 0 and 1,
and in every one of the eight one of them is the body of the head (2,305 to
7,817 vertices) and the other a small strip (16 to 469). In seven of those
eight the small one reaches BELOW the big one - PIERRE 85 units, LARSON 88,
VON_CROY_ELDER 42, BLUE_GUARD 32, ADMIRAL and VON_CROY_SENIOR 16 - and that
strip is the neck. The eighth is PATRICK, whose 16-vertex bone sits INSIDE the
head's own range and is no neck at all, which the test catches. The other four
name one base bone and have no neck to split off. So that bone now hangs off
the head node's PARENT, with the file's own weights kept so the seam blends
rather than tears; everything else stays on the head. It needs the classic
mesh TREE, which `bind_pose_of` now returns beside the bind pose for nothing -
the frame it carries belongs to exactly one node and the push/pop stack
carries that number along. **The proof:** `ModelProbe -slot 47 -head -pose 8
40 55`, before and after - the hollow bowl and the black collar become a neck
that stays in the collar; the rest pose is 3 pixels of 268,800 different.

**B5 - cutscenes took the vanilla models.** A cutscene does not go through
`DrawAnimatingItem`. `DrawCutSeqActors` (`game/deltapak.cpp:3167`) plays the
actors back from packed nodes with its own frame buffer and its own light, so
the object seam of stage S3-2 never saw them - and every actor but Lara, who
IS drawn by `DrawLara` and is HD, kept his 1999 mesh. The same seam sits there
now, in the same shape: the node walk is untouched and hands its MATRIX to the
HD path instead of its mesh, with `duff_item` - the light stand-in the
function already fills - carrying the slot number and the mesh bits. **Actor 1
with the see-through fade stays classic**: `cut_seethrough` fades him with
`phd_PutPolygons_seethrough` and an HD model has no such pass, so a fade that
jumped to solid would be worse than a classic model.

**B6 - the object gap in the log.** `hd=29 classic=108 of 137 loaded slots`
against a tool that counted 57 bound models had nothing to read, because a
slot that never reached the card got no line at all - only the level's totals.
Every slot in the list now writes one, with the reason (`baked`,
`model_missing`, `skeleton_nodes`, `skeleton_drift`, `no_geometry`), and the
level line carries `baked=` beside `hd=` and `classic=`, because a baked slot
is neither. The two remaining differences are arithmetic and named here: the
engine counts `objects[slot].loaded && nmeshes > 0` and leaves Lara's 24 slots
out, where the tool counts every moveable slot in the file (ANDREA1: 162 in
the file, 24 of them Lara, 137 loaded); and `hd` counts slots, not models, so
a slot that binds a model which then fails the skeleton test counts as
classic. With B0 fixed the numbers move: MapProbe over the player's own levels
says 55 bound and 14 DUMMY for ANDREA1, 87 and 3 for RICH1, 68 and 17 for
JOBY3.

**B7 - cosmetic.** `HDModelDrawHead` was called for every entry of every
frame and ended in a map lookup that found nothing, for a thing five slots in
a level own; the frame list carries the answer now, decided once at level
build. And a note that read `? (slot 23041)` names the model instead: 23041 is
`HD_LARA_SLOT_BASE + 1`, a pseudo slot `HDSlotName` cannot know, and a pseudo
slot IS its model.

*Measured and NOT changed, and why:*

- **21 static meshes of ANDREA1 are in NO .TRG group at all** - rooms 121 and
  221 above all - so the room seam suppresses them and they are simply gone.
  They are not the doubling bug and not its fix; whoever picks this up should
  start at ANDREA1 room 221, whose nine statics of slots 40 and 42 the .TRG
  has nothing for. The same measurement shows why a wider rule would be wrong:
  another 13 are baked into a NEIGHBOUR's group (room 136's static sits in
  group 154 with 1,925 triangles), and suppressing those in their own room is
  right.
- **A DUMMY slot with no baked geometry.** Six of ANDREA1's DUMMY items -
  ANIMATING2 in rooms 8 and 22, ANIMATING3, 4, 5 and 6 - have no remaster
  triangles inside their box either, so nothing is drawn where the classic
  mesh used to be. That is the remaster's own picture, and the alternative is
  the double the round is about.

---

#### Fix round A2 - what fix round A broke, and the fog at the edge (#123)

The play test after fix round A brought back three pictures of one fault and
one of another. **No new source file**: the round changes
`specific/hd/hd_render.{h,cpp}`, `specific/hd/hd_light.cpp`,
`specific/hd/hd_level.cpp` and `specific/hd/hd_shader/hd_room.hlsl` (with its
generated `.inc` file), plus the diagnostic tool `RoomProbe`.

**R1 - a Roman shop front as a bare box, a wall missing under the Caffe
Recco's awning, black holes in the crane yard's snow. All three are A2.** The
cut-out alpha test of fix round A was switched on per PAGE, out of a measured
share of texels below alpha 128, on the argument that an ordinary opaque
page's alpha channel is a flat 255 and a test on it changes nothing. The
argument is wrong, and it was measured over a handful of pages rather than
over the game: an opaque page's alpha channel is a MASK - gloss, wetness,
whatever the remaster's own shader reads it as - and the mask is mostly dark.

*What the flag was really catching*, per level, page number and share of the
top mip below alpha 128, held against the .TRG material that draws it:

| level | page | hole | material | what it is |
|---|---|---|---|---|
| ANDREA1 | 201 | 95 % | 3, opaque | stucco facade with columns and windows |
| ANDREA1 | 211 | 96 % | 3, opaque | the same facade, other sheet |
| ANDREA1 | 207 | 78 % | 3, opaque | facade, 2,206 triangles over 6 groups |
| ANDREA1 | 180 | 35 % | 0, opaque | ordinary wall |
| JOBY2 | 480 | 99 % | 3, opaque | the snow and ice of the crane yard |
| JOBY2 | 441 | 61 % | 3, opaque | the same yard |
| JOBY3 | 3219 | 47 % | 17, opaque | bulkhead panels |
| JOBY3 | 3220 | 71 % | 17, opaque | bulkhead panels |
| ANDY1 | 3268 | 74 % | 3, opaque | dirt and gravel ground - NOT a tree |
| ANDY1 | 3264 | 51 % | 8, opaque | leaves, and really a cut-out |
| JOBY4 | 3198 | 40 % | 8, opaque | kelp, and really a cut-out |

So the test threw away 95 % of a Roman facade, 99 % of the yard's snow and
half of a submarine's bulkheads. Fix round A's own note named ANDY1's pages
3268-3272 "the Irish tree pages"; they are the level's ground and gravel, and
its vegetation is material 8 on pages 3264, 3267 and 3165.

**What decides it now: the material, and the material alone.** Measured over
the 15 .TRG files, every distinct `(material, param)` tuple of every part:

| material | `param[0]` | passes | opaque triangles |
|---|---|---|---|
| 6 | 0.500 (once 0.503) | opaque only | 513,647 |
| 7 | 0.500 in all 7 tuples | opaque only | 88,726 |
| 8 | 0.500 in 10 of 11 tuples | opaque, one alpha | 1,308,742 |
| 3 | 1, 2, 4, 26, 32, 35, 39, 56, 69, 78, 107, 109, 110, 141, 161, 191 | opaque only | 148,352 |
| 17 | 22, 25, 36, 38, 63, 70, 72 | opaque only | 17,453 |
| 2 | 0.5, 1.0, 1.5 | ADDITIVE in all 20 tuples | 0 |
| 13, 14 | page numbers | mixed | - |

Materials 6, 7 and 8 are the three whose `param[0]` is an alpha reference;
every other material's `param[0]` is measurably something else - a specular
exponent, a page number, an intensity - and material 2's 0.5 never meets an
alpha test because material 2 only ever draws additive. `MaterialAlphaRef`
(`hd_render.cpp`) answers those three with the file's own threshold and
everything else with zero, the answer travels with the draw call as part of
its sort key, and `gPass.x` carries it to `clip(texel.a - gPass.x)`. Zero
throws nothing away, because an alpha is never below zero - so the alpha pass
(0.5, the classic `D3DRS_ALPHAREF` 128) and the additive pass are unchanged to
the bit, and ANDY1's one part at 0.503 now gets 0.503 instead of 0.5.

**The proof, in pictures out of `RoomProbe`,** the same camera before and
after: ANDREA1 group 167, a facade with a hole through to the sky and only the
window frames left, comes back whole (75 % of the frame differs); JOBY2 group
149, a snow floor with every other slab missing, comes back whole (10 %);
JOBY3 group 30 and JOBY5 group 30, ceilings and bulkheads eaten away, come
back whole (15 % and 21 %). And the benefit of fix round A stays: JOBY4 group
69's kelp is unchanged to the pixel, ANDY1 group 195's leaf silhouettes differ
from the pre-A picture by 39.7 % of the frame and from the post-A picture by
0.17 %, which is the gravel that stopped being clipped. Per level `RoomProbe`
now prints `A2: N opaque cut-out part(s)` and the page list with each page's
hole share, so the two measurements can be held against each other; the level
log line gained `cutout_parts=`.

**R2 - in first person a fog bulb at the edge of the screen jumps away.** Not
what it looked like. `BulbIntensity` opens with the classic path's own line
(`specific/drawroom.cpp:632`)

    stash.z + rad > 0 && |stash.x| - rad < |stash.z| && |stash.y| - rad < |stash.z|

which is a CULL, not a rule: a 45 degree cone about the view axis, widened by
the bulb's radius, thrown in front of the expensive arithmetic. It is Core's
and it predates the HD path; stage S2b carried it over with the rest of the
function, and fix round A did not touch it. Core could afford a hard edge on
it because the game shipped at 4:3, where the frame reaches 40 degrees to the
side. `AlterFOV` (`specific/3dmath.cpp:77`) builds the perspective from
`height * 320/240` and uses that value across as well, so 16:9 turns the
nominal 80 into about 96 degrees wide - 48 to the side - and #134's first
person view runs at 90 to 100 nominal, which is 107 to 118 across, 53 to 59 to
the side. Past 45 the cone cuts INTO the frame, and a per-pixel shader draws
that cut as what it is: a straight vertical line with fog on one side and none
on the other, moving with the camera.

A cull whose job is to skip pixels that are not drawn has nothing to do in a
pixel shader. What is left of it is the half that is exact rather than
conservative - a sphere whose every point lies behind the camera plane meets
no ray leaving the eye forwards - so the shader now tests `P.z + rad <= 0` on
the BULB. That also settles the camera-inside-the-bulb case by itself: inside
means `|P| < rad`, and then `P.z >= -|P| > -rad`. **The proof:** JOBY2 group
149 at `-fov 100` with a bulb 3,000 units ahead and 3,700 to the side, drawn
by `RoomProbe` before and after - a red patch with a straight edge sliced off
its side becomes a round soft-edged patch.

**And `kRim` said 1.0 where its own note said 0.35.** Fix round A's rim
weight, the smoothstep that takes the infinite derivative out of the chord at
the bulb's silhouette, is documented in the shader as 0.35 with a measurement
behind it - "at `t >= kRim` this function answers what it always answered, to
the bit" - and shipped as `1.0f`. With 1.0 that promise holds at the single
ray through the exact centre and nowhere else, and every bulb in the game came
out about half as thick as the classic path draws it. The number the note
measured is the one that stands, in the shader and in `hd_light.cpp`'s twin.

**Parity.** `hd_light.cpp`'s `BulbIntensity` keeps the classic answer to the
bit when `soften` is false and follows the shader when it is true, which is
the shape fix round A gave it; the cull is now part of that split. The
verdict of `-paritycheck` is computed on the light contribution BEFORE the
bulbs (`HDLightParity` reads `out->f[]`, which is written above the bulb
loop), and it is unchanged: 0.000 of 255, PASS, with and without a bulb. The
bulb deviation shows in the line below it, the final brightness on a white
texel, and it grows where the cull used to fire - with a deliberately dense
test bulb, from 45.6 to 150.5 of 255. That is the deviation being named, not
hidden: the classic path culls fog at 45 degrees off axis and we do not.

*Measured and NOT changed, and why:*

- **A1 was suspected and is innocent.** `RoomProbe` never calls `HDFlipRoom` -
  it draws a group, not a room - so the three R1 pictures it reproduces cannot
  come from the flip table, and switching the cut-out test off alone makes all
  three identical to the fixed build, to the pixel. ANDREA1 groups 200 and
  203, the pair fix round A proved itself on, draw identically before and
  after this round.
- **A3, A6 and the sway numbers are untouched.** `RoomProbe` reports the same
  A6 counters per level as before the round (ANDREA1: 29 moving parts, 20,798
  vertices keeping their weight, 63,610 losing it), and RICH1 group 12 and
  ANDREA3 group 40 - glow and material 7 - draw identically.
- **The per-page cut-out measurement stays, as a measurement.**
  `HDPageCacheSlotCutout` and `HDPageCacheSlotCutoutPermille` no longer decide
  anything, and they are kept and printed because holding "this page's alpha
  cuts" against "this part is a cut-out material" is exactly the comparison
  that would have caught the fault before the play test did.
- **The material is compared whole, not by its low byte.** A destructible
  static carries its material with bit 15 set and a counter in bits 8 to 14
  (`0x8003` is RICH3's material 3). Masking would be more general and is not
  done: over the 15 files the only low bytes that ever appear with the high
  bit are 0, 3 and 14, and none of them is a cut-out material.

---

#### Fix round C - Lara, the speech heads and the documentation (#123)

The play test of 2026-09-05 again, this time everything that hangs on Lara.

**C1 - the Desert Eagle, everywhere.** TR5's "revolver" slot kept its Tomb
Raider IV name and changed its weapon, exactly as `CROSSBOW_ANIM` became the
grappling gun. The randomizer settles it outright: it copies JOBY2's slots 6,
12, 16, 347 and 348 into **every** level and replaces what is there
(`Randomizer.cpp`, `kWeaponRules`; the user's decision of 2026-08-23, "replace
the revolver with the Desert Eagle entirely"). The wardrobe was drawing Tomb
Raider IV's six-shooter. Measured with `ModelProbe -name`: the remaster ships
both guns side by side in `4\ITEM` and they are different models —
`HAND_BARE_REVOLVER` has a cylinder and a 1703-vertex `HOLSTER_REVOLVER` on
node 4, `HAND_BARE_DEAGLE` is the slab-sided automatic with a 1270-vertex
`HOLSTER_DEAGLE` on the **same** node 4, so the swap is a swap of names and
nothing else. Hand, laser hand and holster now take the DEAGLE files. The
pickup goes with them: no `.MAP` names `REVOLVER_ITEM` at all, so the slot fell
through to its own name and found nothing; the hand table binds it to
`EXTRA5_REVOLVER_ITEM_JOBY` (the plain `EXTRA5_REVOLVER_ITEM` is the
six-shooter — measured by picture) and the ammo to
`EXTRA5_REVOLVER_AMMO_ITEM`.

*Left named:* the rest of the pickup family — pistols, uzi, shotgun, HK,
grapple, medipack, flare, lasersight, binoculars, crowbar — is unbound for the
same reason and draws classic. The names are all in `4\ITEM` and `5\ITEM` under
`EXTRA`, `EXTRA4` and `EXTRA5`; it is a round of its own.

**C1b - and it is not in the holster at the same time.** The engine keeps
three separate variables for what Lara wears (`LHolster`, `lara.holster`,
`lara.back_gun`) and they are bookkeeping, not fact: every draw and undraw
animation writes them at its own frame, and the paths do not agree.
`lara2gun.cpp:70` (`draw_pistol_meshes`) clears `lara.holster` for every weapon
and `LHolster` for all but the revolver; `lara1gun.cpp:43`
(`draw_shotgun_meshes`) clears `lara.back_gun` and **neither** holster — and
`larafire.cpp:475` (`InitialiseNewWeapon`) sends the revolver down that second
path while the pistols go down the first. Rather than reach into a state
machine the classic path lives on too, the seam states the rule the picture
needs: **what one of the hands is holding is not drawn a second time on the
thigh or on the back** (`HDLaraFrameParts`, one table, one lookup a frame).
`ModelProbe -guns revolver -holsters revolver` builds the contradictory frame
on purpose and the part list comes out
`OUTFIT_CLASSIC + LARA_HEAD + HAND_BARE_DEAGLE + HAIR_OG` — the thigh empty.

**C3 - the VCI Lara's holsters.** The wardrobe planned a holster only when the
level had loaded the matching `LARA_HOLSTERS*` object. That is right for a
HAND part — a mesh can only sit at the hand node if its slot is loaded — and
wrong for everything WORN, because the three variables above hold slot numbers
written from the **inventory**: `setup.cpp:115` puts `LARA_HOLSTERS_PISTOLS` on
both thighs at every level start, whatever the level loaded. Measured against
the shipped files: the three VCI levels load no holster slot at all
(`ModelProbe -lara RICH1/2/3`: "holsters: left -, right -", 6, 7 and 9 parts
planned), because Core drew no thigh holsters on the catsuit and left the
objects out — and the randomizer hands that Lara pistols anyway. The worn parts
are planned by the OUTFIT now (11, 12 and 13 parts, three thigh models and
three back models more per level), so the holster follows the weapon she
carries rather than the slot the level shipped. It cannot invent one: a
takeaway of the pistols runs `NailInvItem`, which puts `LARA_HOLSTERS` back
into both variables.

**C2 - the Russia outfit wears gloves.** An outfit model covers all fifteen
nodes, but what it carries at the two hand nodes is the SLEEVE; the hand itself
is the separate `HAND_` model, which is why the remaster ships three sets of
them — `HAND_BARE_*`, `HAND_GLOVES_*` and `HAND_XRAY_*`. Stage S4-1 took the
bare set for everything and a bare hand came out of the Russia suit's glove
cuff. The outfit table gains a `gloves` column and the part table a gloved twin
per hand part; all twelve exist and load with a bind drift of 0
(`HAND_GLOVES_REST` 5008 vertices against the bare 3864, `HAND_GLOVES_HK` 7507
against 6363). Only `OUTFIT_RUSSIA` is set — nobody has reported a wrong hand
on any other, and each is a picture the user has to judge. A hand part with no
gloved twin falls back to the bare one and says so in the log; none does today.
`HAND_XRAY_*` belongs to a remaster feature this engine has not got and is
deliberately unused.

**C6 - the speech heads, measured and not built.** There is no speech-head draw
path and no overlay: a talking head is a MESH SWAP inside the normal draw.
`deltapak.cpp:2759` (`handle_lara_chatting`) writes
`lara.mesh_ptrs[LM_HEAD]` from one of the four `LARA_SPEECH_HEAD1..4` slots,
re-rolled every frame; `deltapak.cpp:2763` (`handle_actor_chatting`) does the
same one level down, setting the node's bit in `cutseq_meshswapbits` and
writing an `ACTOR*_SPEECH_HEAD*` mesh into the second pointer of that node's
mesh pair, which `DrawCutSeqActors` then draws instead of the first
(`:3226`, `:3260`). Four heads with different mouths, cycled at 30 Hz, is the
whole of TR5's lip sync.

Both ends of that swap are meshes, and the HD path has none: Lara's head comes
from `LARA_HEAD.TRM` and an actor's from his body model or his own
`<NAME>_HEAD.TRM`. The six speech-head slots have nothing to swap TO either —
not one `.MAP` names them and neither `4\ITEM` nor `5\ITEM` ships a
`LARA_SPEECH_HEAD1.TRM`. What the remaster does instead, measured over its own
files: one rigged head per speaker — `LARA_HEAD.TRM` has bones up to 31, which
are a FACIAL rig and not mesh tree nodes — and beside it a `.TRL` file **per
spoken line**: `LARA_HEAD_96.TRL` .. `LARA_HEAD_135.TRL`,
`ADMIRAL_HEAD_113.TRL`, `PIERRE_HEAD_108.TRL` and so on, 2.6 MiB apiece. That
is facial animation for a rig this engine already carries but does not animate,
in a format nothing here reads. Driving it is a piece of work of its own;
inventing a mouth movement instead would be inventing a look. So the seam
**measures** it: `speech_head_slot()` says which of the four slots the engine
has just put on her neck and the frame log carries it as `speechhead=`, so the
trigger is visible and a later round starts from a log rather than a guess.

**C4 - the braid root, measured.** `hair.cpp:397` walks the mesh tree to the
head bone and translates by `(-4, -4, -48)` in its own frame before writing
`hairs[0][0]`; that point, not the head joint, is what the seam puts
`HAIR_OG`'s bone 0 on. The classic braid hides the distance between that point
and the skull by SKINNING: `DrawHair` draws meshes 1, 3 and 5 rigidly at
`hairs[1]`, `[3]`, `[5]` and meshes 0, 2 and 4 as connectors stitched out of
stashed vertices — mesh 0 stretching from the head's own stash ring down to
link 1. An HD link is rigid and cannot stretch. `ModelProbe -braid` puts a
number on what is left over: the root sits **16.8 units** from the nearest
vertex of the outfit's head node and `HAIR_OG`'s link 0 reaches only **1.7
units** back out of its own origin, so **15.2 units** stay open — the same for
every outfit that wears a braid. TROne has it too, and carries a debug probe
for exactly this question (`hd.debug_hair_shift`, "bone b reads joint b+n"),
which is why the user sees it there as well.

No constant is applied. Every way of closing 15 units moves the gap somewhere
else — pulling the root up detaches link 0 from link 1, stretching the chain
breaks the chain rule the braid check proves — and which of those pictures is
the better one is the user's call, not a guess. What fix round C did build is
the ability to see it: `ModelProbe` seeds its studio braid at the game's own
root instead of at the head joint, `-look head` aims the camera at the
junction, and `-braid` prints the three numbers.

**C5 - the Tomb Raider III bird is the Gold one.** The randomizer takes guest
slot 43 out of THE LOST ARTIFACT's `chunnel.TR2`, not out of Tomb Raider III
itself (`Randomizer.cpp:1824`, donor `"tr3gold"`; `Config.h`: "chunnel.TR2
carries the vulture"). The remaster keeps the two apart under exactly that
rule: `3\ITEM` of a fresh Tomb Raider I-III Remastered holds `VULTURE.TRM` and
`VULTURE_CHUNNEL.TRM` side by side, and they are different creatures — a
feathered vulture with a red head (5511 vertices) and the Gold bird (2334).
The guest table now names `VULTURE_CHUNNEL`.

**C7 - the search order stops at 4.** A model or a page was looked for in
`5\`, then `4\`, then `6\`. Game 6 is Angel of Darkness: a different engine,
a different Lara, different objects, and a name that matched there would be a
wrong model rather than a missing one. The fallback is gone from
`hd_model.cpp` and from both page lookups in `hd_pagecache.cpp`, along with the
`HD_MODEL_ORIGIN_6` value and the `fromGame6` counters in the two logs and in
`ModelProbe` / `RoomProbe`. The other bundle is unchanged: `3 -> 1 -> 2` stays,
because Tomb Raider II ships extras of its own.

**C8 - the documentation.** The manual now explains why a big card reports
4095 MiB of texture memory (`GetAvailableTextureMem` answers in an unsigned 32
bit count of bytes, so 4096 MiB is the largest number that fits) and that this
costs nothing, since every card that hits the ceiling is nearly seven times
over what the heaviest level needs. The registry table was checked key by key
against the source and is complete: `hd_enable`, `hd_objects`, `hd_motion`,
`hd_texture_budget_mb`, `hd_install`, `hd_install_tr3`, `hd_texture_format` are
the seven the engine reads and the seven the manual lists. The two mirror
paragraphs above are corrected, and the correction turned up one real fault -
see there.

---

#### Room round 3a - the light shafts, the lamp cones and the sky (#123)

The play test's line about the light was two faults in one: *"Intensity of the
light far too bright and only static, light rays etc. generally static"*. Both
live in the ADDITIVE pass, and the sky - which the HD path had never drawn at
all - is the third thing in this round. **No new source file**: it changes
`specific/hd/hd_render.{h,cpp}`, `specific/hd/hd_level.{h,cpp}`,
`specific/hd/hd_shader/hd_room.hlsl` (with its two generated `.inc` files),
`game/draw.cpp` and the diagnostic tool `RoomProbe`.

**R1 - what the additive pass was doing.** It is blended ONE / ONE with the
depth write off, so overlapping parts SUM, and the remaster's light shafts and
lamp cones are fans of dozens of quads that all meet at the lamp. Its caustic
net over JOBY4's sea bed is one 192 x 192 sheet over the whole floor. Painted
as texel x baked light with no weight of its own, that saturates. Measured with
`RoomProbe`, on the additive pass alone, over the pixels it covers:

| scene | what it is | before | after |
|---|---|---|---|
| JOBY4 room 19 | the caustic net | 98.1 % mean, 86.9 % of it pure white | 6.9 %, nothing white |
| ANDY3 room 126 | a window glow | 72.5 %, 22.1 % white | 61.2 %, 19.0 % white (see below) |
| RICH2 room 25 | two wall lamps | 21.9 %, peak 100 % | 9.1 %, peak 94.9 % |
| RICH1 room 136 | a ray fan | 22.7 %, peak 100 % | 4.3 %, **peak 27.8 %** |
| ANDREA1 room 181 | hall shafts | 13.3 %, peak 100 % | 6.8 %, peak 55.7 % |

The player's own reference shots of a VCI corridor put the streaks at a
quarter of white with nothing saturated - measured off the picture, the
streaks stand 40 to 80 above a wall of 110 to 130, which is 16 to 31 % - and
that is where the ray fan now lands.

**What says the weight: the material, one slot at a time.** A `.TRG` part
carries a material and four floats, and the four MEAN DIFFERENT THINGS PER
MATERIAL. That is not a guess. Material 13 proves it: its `param[0..2]` are
texture LAYER numbers - they name the other three frames of its flipbook,
layers 123 to 126 of JOBY2, which are pages 3180 to 3183 - and its `param[3]`
is 0.15, the classic flipbook's own cadence of five frames at 30 Hz to within
a sixtieth of a second. So the question is never "which slot is the weight"
but "which slot is the weight of THIS material", and it was measured slot by
slot over the distinct (material, param) tuples of all fifteen levels:

| material | what it draws | the four slots | weight |
|---|---|---|---|
| 2 | shafts, lamp cones, window and candle glows | `p0` in {0.5, 1.0, 1.5} and `p2` in {0, 0.1, 0.4, 0.6, 0.8, 1.0, 2.0} move together and with the page - a preset; `p3` is a time (below) | `p1` |
| 14 | lit decals and panels | `p2` and `p3` are zero in all sixteen tuples, `p0` is an integer layer number (0, 12, 26, 32, 38, 45, 50, 61, 102, 139) | `p1`, and it never leaves 0..1 |
| 15 | haze | `p1` is 14 or 30 in every one of eleven tuples - a speed, not a weight | `p2`, 0.025 to 0.1 |
| 16 | whole-surface sheets, the caustic net among them | `p0` and `p1` pair up with the page | `p2`, 0.035 to 0.2, the same shape as 15's |

Everything else - materials 0, 1, 6, 7, 8, 13 and the destructible statics'
high-bit numbers - has no slot that behaves like a weight and draws exactly as
it did. Over the game 20 to 131 parts per level take a weight; the smallest is
0.025.

**Where the file gives no usable number the part keeps the old picture.** Two
of material 2's tuples carry `p1` = 0 - 2,100 triangles of window glow in ANDY1
and ANDY3 - and one carries 2.68, in five RICH1 rooms. Reading 0 as "off" would
delete geometry that measurably draws something; reading 2.68 as a weight would
brighten what is already too bright. Both fall through to 1, the same shape as
`MaterialAlphaRef`'s guard of fix round A2, and both are named losses: **ANDY3
room 126's window glow is still 61 % mean with a fifth of it white** (it moved
at all only because of the swing below), and RICH1's five bright ceiling lamps
are untouched.

**R2 - and the swing.** The reference shots are three seconds apart and the
streaks stand in different places. The only slot in the file that is
measurably a time is `param[3]`, and material 13's 0.15 seconds is the proof;
material 2 fills the same slot with 0.05 to 16 seconds - a candle's flicker at
one end and a sun shaft's slow drift at the other - and it is the only
additive material that fills it at all, 14, 15 and 16 carrying a hard zero
there in every tuple of every level. So the weight breathes a quarter either
side of itself, once per `param[3]` seconds, with a phase taken from the
surface point so that neighbouring parts of one fan are out of step and the
bright places travel along the shaft instead of the whole room pulsing at
once. The wave has the shape stage S5's wind already uses. Measured between
two moments 1.25 s apart, over the pixels the pass covers:

- RICH2's wall lamps, whose `param[3]` is 2.5 s: mean change 3.3 %, worst 33.7 %
- ANDREA1's hall shafts, whose `param[3]` is 16 s: mean change 0.3 %, worst 3.5 %

which is the reference's own behaviour - the corridor wanders, the hall barely
moves. Both die with `hd_motion` 0, exactly as stage S5's scroll and wind do.

*Not adopted:* material 15's `p1` (14 or 30) and material 16's `p1` (0.25, 0.7,
1.0) look like speeds as well, but nothing in the data says of WHAT, and
neither material carries a time slot. They stand still and are named here.

**R3 - the sky.** Every `.TRG` whose level has a sky ends in one group past the
last room - two in TITLE, none in JOBY3 and JOBY5 - and until now the HD path
kept them out and let the classic `SkyDrawPhase` draw its horizon mesh instead.
Measured, the group holds:

- **the band**: 1,280 triangles on a sphere of about four blocks' radius (21
  rings, radius 1060 at the poles and 4100 at the equator), wearing one
  4096 x 2048 panorama out of the page block 7000..7011 - ANDREA1's is Rome at
  sunset with St Peter's dome, ANDY's the Irish hills at dusk. Alpha 0 above
  the horizon line and opaque below it, so the alpha pass cuts the void away.
  **Its baked light is exactly zero on every vertex of every level**, because
  the remaster's own sky shader does not read it; through the room formula it
  would be black, so a sky part with no light anywhere is drawn at light 1 and
  its page is the whole picture.
- **the cloud layers**: one to three flat discs of up to 24,000 units' radius
  at fixed heights above the camera, material 1, which therefore already
  scroll on their own parameters (`p1` = -0.03 to 0.06 page units per second)
  since stage S5 - and which DO carry baked light, 0.26 to 2.00 over the game.

**No scaling and no rotation.** The sphere's four blocks sit far inside the far
plane of 20480; the cloud discs reach past it, which only cuts a disc where the
band already covers it. The group is centred on the origin, so it takes the
matrix the classic path builds for its own horizon - `phd_TranslateAbs(camera.pos)`,
nothing else - and rides on the camera exactly as the horizon did.

**The depth.** The discs hang in front of the band overhead and behind it at
the horizon, and both draw in the opaque and the alpha pass, so no draw order
gets both right. The sky is given its own DEPTH RANGE instead: the viewport's
`MinZ` is pushed to 0.996 for the length of the call, which folds the whole sky
into the last four thousandths of the depth buffer. Inside that slice the depth
test sorts the sky properly; outside it every pixel of the world is nearer,
because the classic depth of the far plane itself is only 0.995
(`SetupZRange(20, 20480)`: a - b/z = 0.99500) and nothing the engine draws ever
reaches the slice. The viewport is read back and put together again before the
call returns.

**Where it is drawn, and what it replaces.** In `SkyDrawPhase`, in the classic
horizon mesh's own place in that function - after the moon that was drawn a
moment earlier and before the stars below it, because none of the three writes
depth and the order in that function is the order on the screen. Drawn with the
rooms in `HDFrameDraw` instead, the dome would have painted over the moon.
Where the dome draws, the horizon mesh and the two flat colour layers are left
out, because the dome already contains them. What still runs is the weather:
the moon, the stars, and **the lightning flash**, which the game paints THROUGH
layer 1 in the lightning's own colour and which would have been lost with the
layer - so layer 1 survives the dome when `GF_LIGHTNING` is set, and only then.
Rain and snow were never part of this phase and are untouched.

Which levels have a dome, measured over the fifteen `.TRG` files by the pages
they wear:

| levels | sky groups |
|---|---|
| ANDREA1, ANDREA2, ANDREA3, ANDY1, ANDY2, ANDY3, JOBY2, JOBY4, RICH1, RICH2, RICH3, RICHCUT2 | 1, always the last group |
| TITLE | 2 |
| JOBY3, JOBY5 | none - the classic sky stays whole |

The player's own log confirms the room counts the engine hands in: RICH2 "175
HD rooms, 4 classic" is 179 rooms and its sky is group 179; JOBY4's "128 HD
rooms, 9 classic" is 137 and its sky group is 137; TITLE's "105 HD rooms, 11
classic" is 116 and its two sky groups are 116 and 117.

*Named and left:* the dome rides on the classic path's own two gates, because
it is drawn inside them - `outside`, which `GetRoomBounds` sets when a room
that sees the sky is in view, and `objects[HORIZON].loaded`, which
`SkyDrawPhase` returns on. A level with an HD sky group but no classic HORIZON
object would therefore draw no sky at all, which is exactly what it does today
and one gate more than the dome itself needs.

**What did not move.** The opaque and the alpha pass multiply by exactly one
and the sort key's two new fields are constant across them, so a surface cannot
change. Proven, not argued: thirteen scenes over eight levels - ANDREA1's
facades, roofs and lamp posts, JOBY2's snow yard, JOBY3's bulkheads, ANDY1's
trees, JOBY4's kelp and the four scenes above - rendered through the same
camera before and after, **26 of 26 opaque and alpha pictures byte for byte
identical**. `RoomProbe -paritycheck` on ANDREA1 room 19 still reports 0.000 of
255 on all three channels. The room pixel shader grew from 362 to 386
instruction slots and the vertex shader from 328 to 330, against the 512 shader
model 3 guarantees.

`RoomProbe` gained `-sky` and `-skyonly`, and prints one `R3a:` line with the
weighted parts, the smallest weight and the swinging parts; the engine writes
the same three numbers into its own `HDLOG ... motion=` line, plus one
`HDLOG ... sky groups=` line per level.

---

#### Fix round D - the actors, Lara's kit and the sheen (#123)

The play test of 2026-09-06: four findings, all of them about people rather
than rooms.

**D1 - the cutscene actors had no heads.** In the RICHCUT2 cutscene the man in
the suit and the man in the wheelchair stood in front of the fire without a
face, and the guard beside them wore a cap with nothing under it. Measured with
`ModelProbe`: `ANIMATING4_RICHCUT2` stops at y -234 with an open ring of 16
points at (0, -218, 6), radius 13, and not one vertex above it - the body
really is a collar, and the rule of stage S3-2 (a node the .TRM does not cover
is not drawn classic either) then leaves nothing there at all.

The cause is a naming one. A face is `<MODEL>_HEAD.TRM`, which answers for
`BLUE_GUARD`, `LARSON` and `PIERRE`, and cannot answer for a cutscene actor:
the remaster names his BODY after the level slot it fills
(`ANIMATING4_RICHCUT2`) while his FACE keeps the person's name
(`VON_CROY_SENIOR_HEAD`). Game 5 ships twelve `*_HEAD.TRM` and only three of
them are reachable by name.

Two rules are added in `hd_model.cpp` (`find_head_model`), both measured over
all 1,335 model names of the 4 and 5 folders:

- the **strip rule** - cut the last `_<QUALIFIER>` off and ask again, as long
  as one is left. It answers for 17 names and every one is right or harmless:
  `BLUE_GUARD_JOBY`, `_RICH2`, `_RICHCUT2` and the two `_MIP` twins,
  `PIERRE_SWAP`, `VON_CROY_BOOK/BOTH/EMPTY` of game 4, and eight `_MIP` models
  this engine never draws. Not one name gets a face belonging to somebody else.
- a **table** of two entries for the bodies that carry no part of the person's
  name at all. Each is measured - the body's open ring against the face's neck
  ring, both in the bind pose the .TRM files share:
  `ANIMATING4_RICHCUT2` gets `VON_CROY_SENIOR_HEAD` (7.4 units apart, next
  nearest face 11.4, and the two share texture page 9075, which no other pair
  in the level does) and `ANIMATING6_RICHCUT2` gets `VON_CROY_ELDER_HEAD` (2.7
  against 35.3).

A ring match alone is not proof and the round has the counter-example:
`ADMIRAL_HEAD`'s neck lands 1.3 units from a ring of `ANIMATING10_TITLE`, and
the picture shows that model is the top-hatted mourner of the title graveyard,
who has his own face and needs none. So each entry is confirmed by a picture as
well, and the three RICHCUT2 actors are in the report with their heads on.

A face is also no longer given a face of its own: without that guard the strip
rule cut `_HEAD` off Lara's own `LARA_HEAD` part, found `LARA_HEAD` again and
hung her face on itself, two sets of triangles fighting over the depth buffer.

`DrawCutSeqActors` writes one `HDLOG cutseq actor=` line per actor per
cutscene - slot, model, hd, head node, the mesh bits - so the next report of a
missing face can be read out of a log.

*Left named:* `ADMIRAL`, `DEMON`, `HANGMAN`, `PATRICK_BROWN`, `PATRICK_WHITE`,
`SERGIE` and `ZIP` have no body among the models the fifteen `.MAP` files name
that both fits their neck and lacks a head. `PATRICK_1` and `PATRICK_2` are
headless and are plainly the two Patrick faces' bodies, but the remaster
reaches them only through `SAILOR_MIP`, and this engine never draws an item's
MIP slot - so they stay classic and the question does not arise. And the mouths
do not move: the remaster animates a face with one `.TRL` file per spoken line,
a format nothing here reads.

**D4 - the VCI catsuit had no holsters.** Three photographs of catsuit Lara
with the guns hanging in mid air beside her thighs, no pouch and no strap,
while the log said `HOLSTER_PISTOL` was planned and drawn. Both are true:
`HOLSTER_PISTOL.TRM` is the two guns and nothing else - 4268 vertices, and the
picture out of `ModelProbe -name HOLSTER_PISTOL` is two pistols floating in
space. The pouch, the thigh strap and the hip belt they hang from are built
into the OUTFIT model, one page of it. Measured over the eight wardrobe bodies,
triangles on nodes 1 and 4 by texture page:

| outfit | thigh holster rig |
| --- | --- |
| `OUTFIT_CLASSIC` | page 8017: 1090/1090 triangles |
| `OUTFIT_RUSSIA` | page 8017: 1106/1114 |
| `OUTFIT_TR3_COAST` | page 8205: 354/354 |
| `OUTFIT_TR3_CATSUIT` | page 8205: 354/354 |
| `OUTFIT_VCI` | **none** |
| `OUTFIT_TR3_ANTARC` | **none** |
| `OUTFIT_TR3_NEVADA` | **none** |

Three of the eight have none, because vanilla TR5 gives catsuit Lara no
pistols and the two Tomb Raider III guests wear none in their own game either.
The randomizer hands her a gun in every level, so the guns hung in the air.

`HDModelSlot` gains `onlyPage`: build only the triangles that wear one .DDS
page, and load only that page. The cut is by page and not by triangle count on
purpose - a page is the artist's own division of the model into materials, and
the measurement says it is exact here: 8017 appears on nodes 0, 1, 4 (and 79
triangles of node 7 in the Russia coat) and nowhere else, 8205 on nodes 0, 1
and 4 alone, with no part of a leg in either. A new wardrobe part,
`HD_LARA_PART_HOLSTER_RIG`, is `OUTFIT_TR3_CATSUIT` cut to page 8205 and is
worn by an outfit that holsters and has no rig of its own. The London catsuit
is the donor rather than the Rome one because its rig is black leather over a
black leg, which is what the VCI catsuit is; `OUTFIT_CLASSIC`'s is brown suede
over bare skin.

It is drawn before the weapons and not with them, because it belongs to the
outfit: the pouches do not vanish when she draws her guns, exactly as the
built-in rig of `OUTFIT_CLASSIC` does not. `ModelProbe` gained `-onlypage <n>`,
and the picture out of it is the rig alone - a belt, two pouches and two thigh
straps, no legs.

**D3a - the braid hung 15 units clear of the skull.** Fix round C measured the
gap and left it; this round closes it. The two braids are built differently:
the classic one draws its first link as a SKINNED connector whose top ring is
stashed vertices of the head mesh, so it starts on the skull whatever the root
is, while the HD one has a rigid `HAIR_OG` link 0 that starts at the root and
reaches only 1.7 units back out of it. The measurement (`ModelProbe -braid`) is
the same for all eight bodies because the skull is one mesh: the head joint at
1/-247/-12, the root at -3/-251/-60, the nearest skull vertex at
-0.7/-251.2/-43.3 - 16.8 units away and the direction is +z alone. So the
correction is 15 units of +z and nothing else, and `HD_LARA_BRAID_ROOT_Z_HD` is
-33 where the classic one is -48. It is applied in `HairControl` and only when
the HD wardrobe is drawing, so the whole chain is seeded from the new point and
the links below follow instead of breaking at a seam.

**D3b - and it went through the backpack.** The classic `HairControl` pushes
the braid out of five spheres and all five are CLASSIC Lara meshes: hips,
torso, head, the two upper arms. An HD outfit is a different body and its pack
stands 92 units behind the torso joint, where the classic torso sphere reaches
nowhere near. Measured with `ModelProbe -braid` in the torso node's own frame -
the pack being every node 7 vertex more than 30 units behind the torso joint,
because nothing else of a torso is back there - it is the same slab in all
seven braided outfits: centre 1/-132/-61, radius 71 to 83, and down the middle
of the back (the strip a braid can touch) y from -197 to -68 and z from -92 to
-30. One sphere round that box would be a poor pack: it is flat, and a single
sphere of radius 75 would hold the braid 44 units clear of the back it is meant
to lie on. So the pack gets **two**, stacked, each tangent to the slab's rear
face: radius 36 at z -52, at y -165 and -100, covering y -201 to -64 between
them. Added in `HairControl` only while the HD wardrobe draws, and the
collision loop runs over six spheres or eight accordingly.

**D2 - everything was matt.** TR5 has no specular term of any kind:
`aTransformLightClipMesh` adds an ambient and a diffuse cosine and stops. The
remaster's own VCI catsuit and the Tomb Raider III London one read as LATEX
because they are lit with one, and the skin carries a soft sheen; ours had
none.

*The obvious source was measured and refused.* Fix round A2 found the room
pages' alpha channel to be a gloss mask, and TROne describes the item pages the
same way in so many words (`host/render/game_renderer.cpp:373`). It is not one
here. `DdsProbe` gained `-alpha`, which writes the alpha channel as a grey
picture and prints a histogram; over the wardrobe's pages 8001, 8008, 8016,
8018, 8027, 8028 and 8205 **100 % of the texels fall in the 240..255 bucket** -
a flat opaque channel with BC7 quantisation noise on it and no mask anywhere.
(The one page that is not flat is 8002, Lara's hair, at 15.2 % below 128 - and
that is coverage, not gloss.) There is nothing per texel to read.

So the material is named instead, in one table in `hd_lara.cpp` with the
numbers in it, and the numbers go into the log per part so that a picture the
user does not like can be traced to a line without opening the source: latex
0.45/26 for the two catsuits, rubber 0.30/22 for the diving suit, cloth
0.10/14 for the Rome top, skin 0.12/10, hair 0.14/18, leather 0.18/20 for the
holster rig, gunmetal 0.35/34 for a weapon. The term itself is Blinn-Phong in
the model VERTEX shader, beside the diffuse it belongs to - the classic path
lights an object per vertex and this is that light's second half, not a second
light - out of the object's own light list, with the same direction and the
same distance falloff the diffuse loop uses. When the list is empty, which is
an ordinary room whose light is all baked into the vertices, the eye is the
lamp, weighted by the light the vertex already has so that a catsuit in an
unlit corner stays dark. The highlight is added AFTER the texel and not
multiplied through it - a highlight on black latex is white - and never in the
additive pass.

One new vertex register, `c161` (`gSpec`: strength, exponent), written for
every draw so that Lara's gloss cannot be left on the next creature.
`HDModelLighting` carries the two numbers; **every object passes 0**, so the
creature path is unchanged to the bit. `ModelProbe` gained `-nosheen`, which is
the BEFORE picture.

*Proof.* `ModelProbe -laracheck` 15 of 15 joints at 0.00000 px and the braid
chain gap 0.00002 units, `-itemcheck` on the SWAT 0.00000 px,
`RoomProbe -paritycheck` worst 8.9e-08 - the room path is untouched. Pictures:
the three RICHCUT2 actors with their heads, the rig alone, catsuit Lara with
the guns in their pouches, the braid junction from the side before and after,
and the catsuit with and without the sheen.

---

#### Room round 3b - the flipbook, the water, the gloss and the statics (#123)

Four of the play test's lines at once, and all four turn on the same question:
what does the `.TRG` say, and what does it not say. **No new source file**: it
changes `specific/hd/hd_render.{h,cpp}`, `specific/hd/hd_level.{h,cpp}`,
`specific/hd/hd_shader/hd_room.hlsl` (with its two generated `.inc` files),
`game/draw.cpp`, `tomb5/tomb5.cpp`, `global/types.h` and the diagnostic tool
`RoomProbe`.

**R1 - the four-page flipbook, drawn at last.** Material 13 was counted and
not drawn from stage S5 onwards, "because a range names one page and the
animation names four". Fix round R3a found the bridge on its way past:
`param[0..2]` are not tile numbers in the classic name space, they are LAYER
numbers - indices into the file's own texture list, counted from one, exactly
like `TrgVertex::layer`. Measured over all fifteen levels, there are 27 such
parts in five levels:

| level | rooms | layers | pages | `param[3]` | what it is |
|---|---|---|---|---|---|
| JOBY2 | 58, 104 | 118..121 opaque | 3176..3179 | 9, 7, 11, 6 s | the camera feed on a bank of monitors |
| JOBY2 | 58, 104 | 123..126 additive | 3180..3183 | 0.15 s | the scan line glow over them |
| JOBY3 | 4 | 15..17 | 3228..3230 | 0.2 s | 1,728 triangles |
| JOBY3 | 49 | 87..89 | 3235..3237 | 0.05 s | the panels over the bunks |
| JOBY5 | 4, 5 | 57..59 | 3228..3230 | 0.2 s | the same as JOBY3's |
| JOBY5 | 49 | 115..117 | 3235..3237 | 0.05 s | |
| RICH1 | 7 rooms | 68, 69, 70, 57 | 3064..3067 | 0.2 s | |
| RICH2 | 11, 159 | 96..99 | 3064..3067 | 0.2 s | |
| RICH2 | 21 | 120..123 | 3310..3313 | 0.5 s | |

The sequence is (own layer, `param[0]`, `param[1]`, `param[2]`), and the parts
of one surface are the SAME cycle at four phases: JOBY2's four opaque parts
read 118 -> (119, 120, 121), 119 -> (120, 121, 118), 121 -> (118, 119, 120) and
120 -> (121, 118, 119), so the whole wall steps together. A repeat inside a
sequence is a ping-pong and is left as written - JOBY3's 15 -> (16, 17, 16) is
a three frame swing.

`param[3]` is seconds per frame, and the six to eleven seconds of JOBY2 are not
a misreading: the picture says what room 58 is. It is a VCI control room whose
wall is a bank of closed circuit monitors, its opaque flipbook is the camera
feed switching every six to eleven seconds, and its additive one is the scan
line glow at 0.15 s - the classic flipbook's own cadence of five frames at
30 Hz to within a sixtieth. Nothing else in the game reaches past 0.5 s.

A part's triangles are drawn from `flipSlot[frame]` with the frame taken from
the same monotonic game clock stage S5 uses, modulo the whole cycle so that an
hour of play never leaves the range of an `int`. **27 of 27 parts resolved, 0
kept their own page.** Pictures: JOBY2 room 58 at t = 0 and t = 12 s, the
monitors showing two different sets of feeds.

**R2 - why the water stood still, and it was not the shader.** The play test:
*"Water waves and animation missing in some sections"*. Measured out of the
player's own log of the complete build, over ten level loads:

| level | classic marks | HD vertices stamped | of them `choppy` |
|---|---|---|---|
| JOBY4 | 12 | 32 | 0 |
| RICH2 | 31 | 47 | 0 |
| ANDREA1 | 204 | 267 | 0 |
| ANDY1 | 155 | 218 | 0 |
| ANDREA2 | 564 | 809 | 0 |
| RICH1, RICHCUT2, TITLE | 0 | 0 | 0 |

Two findings in one table. `choppy` is `ROOMLET::nWVtx`, the roomlet's own
water vertex set, and it is **zero in every level of TR5** - the classic engine
never displaces a water vertex either, and what moves its water is the animated
texture range, which is R1 above. And the surface bit reaches only the corners:
stage S2 carries the classic answer to the HD vertex AT THE SAME POSITION, and
on a water surface that is the corners of the classic quads and nothing else.
The remaster remeshes a room, so a triangle whose three corners are all
unmarked gets weight zero over its whole area. Measured over the water rooms of
six levels, the busiest horizontal plane of a water room holds 1 to 15 times as
many HD vertices as the classic quad grid has corners - JOBY5 room 126 is 544
against 36 - and on JOBY2's room 67 the corner rule finds **11 of 448**, two and
a half percent.

So the surface bit is flooded along its own plane: per room and per height, the
axis aligned box of the marked corners, and every HD vertex of that room at that
height inside that box gets the bit. A box and not the whole plane, so a walkway
at the water's own height on the far side of the room stays dry; per height, so
two pools at two levels in one room stay apart. The classic answer for that bit
IS a decision about a plane - `drawroom.cpp:537` opens the branch with
`pos.y == CurrentRoomPtr->minfloor` - so the answer for a vertex the classic
mesh never had is the answer of the piece of plane it lies in.

On top of that mask, TROne's water effect: a vertical swell of 24 units and a
UV wobble of six texels in the vertex half, and a **per pixel** caustic in the
fragment half, computed from the WORLD position. Stage S5 refused the caustic -
"TR5 has one, the WaterTable shimmer, and a second would count it twice" - and
that refusal was right about the brightness and wrong about the motion: the
WaterTable term is per vertex, and on a surface of a few large quads what
reaches the screen is a slow blotch. TROne hit exactly this and says so in the
same words. So the WaterTable term stays where it is, untouched, parity and all,
and the caustic is added as a ZERO MEAN modulation of the multiply: it carries
no average brightness of its own, only the movement.

Calibrated against the player's two Remaster shots of the same VCI pool: over
the water region the two frames differ by 8.4 of 255 on average, with 17 % of
the pixels more than 16 apart against a mean of 34 - a swing of a third to a
half of the local value, which TROne's 0.32 either way meets.

**R3 - the marble does not shine, and the file says why.** The player's shots
show RICH2's marble throwing back the walls and the lamps where ours is matt.
Two obvious answers were measured first and both are wrong:

- **TROne has no reflection and no specular anywhere.** Grepped over the whole
  host tree and its docs: the only matches are a comment about the water UV
  wobble "distorting the reflection" and the note in `game_renderer.cpp:373`
  that an opaque page's DDS alpha is a *"sparkle/specular MASK, not coverage"*.
  `SHADER_RULES.md` - Todesengel's own measurement of the remaster's programs -
  lists six rules and none of them is a mirror.
- **The gloss mask of fix round A2 is not on the marble.** RICH2's floor rooms
  draw with pages 1010, 1032 and 1033, and all three are alpha 255 on all
  262,144 texels of the top mip (`DdsProbe -alpha`). A2's masked pages are
  stucco, snow, bulkheads and gravel; the marble is not among them.

What the file does say is a pair that behaves like a specular one:

| material | exponent | strength | triangles | levels |
|---|---|---|---|---|
| 3 | `param[0]`, an integer 1, 2, 4, 26, 32, 35, 39, 56, 69, 78, 107, 109, 110, 141, 161, 191 | `param[2]`, 0.333..1.0 | 165,897 | ANDREA1 (110 at 0.85 over 42 rooms), ANDREA2, ANDY1..3, JOBY2, TITLE |
| 17 | `param[0]`, 22, 25, 36, 38, 63, 70, 72 | `param[1]`, 0.05 or 0.1 | 17,453 | JOBY3, JOBY4, JOBY5 |

**And RICH1, RICH2 and RICH3 - the three VCI levels, the ones the marble is in -
carry neither material, not one triangle.** So the one surface the report is
about has no datum in the `.TRG` at all. It is named here and left matt rather
than given an invented one.

What the 214 glossy parts of the other twelve levels get is not a mirror -
nothing renders the room a second time - but the thing a polished floor does
that a matt one does not: it gets brighter at a grazing angle, Schlick's fifth
power of one minus N·V, times the material's own strength, applied to the
multiply so that what the surface lifts towards is its own lit colour and never
a colour of the shader's making. The obvious second half, Blinn's highlight
against the room's dynamic lights raised to the material's exponent, was
written, compiled and **measured**: inside a loop `ps_3_0` forces to be
unrolled eight times, one `pow` per light takes the room pixel shader from 319
to **636** instruction slots, past the 512 shader model 3 guarantees. So the
exponent is read, logged and unused, and that is said out loud rather than
hidden. Behind the registry key `hd_reflect`, default 1.

**R4 - the destructible statics.** *"Static meshes are not destructible."* Fix
round A left a lead: five parts of RICH2 whose material carries bit 15 and a
counter. Measured over all fifteen levels there are 69 such parts in six levels
and 32 rooms - ANDREA1 6, ANDREA2 5, JOBY2 3, RICH1 5, RICH2 14, RICH3 36 - and
the encoding comes apart cleanly: bit 15 set, bits 8..14 a counter 0..5, bits
0..7 an ordinary material number (0, 3 or 14, and nothing else in the game).

The counter is an INSTANCE index and not a damage stage, and the boxes prove
it: JOBY2's room 57 carries `0x8000`, `0x8100` and `0x8200` with 552 triangles
each, standing side by side at x 1024..2048, 2048..3072 and 3072..4096, same y,
same z, 25 units thick. RICH2's room 151 carries three 2,226-triangle copies at
three corners, RICH3's room 204 four. One number per copy is exactly what a
renderer needs to make ONE of them disappear.

So each (group, material) is one piece, its triangles are kept in draw calls of
their own - the sort key carries the id, so they never merge with the wall
behind them - and `hd_level.cpp` pairs a piece with the classic `MESH_INFO`
standing inside its box, out of the slots 50 to 57 that `control.cpp:1928`
calls shatterable. `MESH_INFO::Flags` bit 0, which `control.cpp:1935` clears
when Lara shoots one, then decides per frame. Picture: ANDREA1 room 198, the
barred shop window, with and without its grating.

**R5 - and the statics the `.TRG` has nothing for.** Fix round B left 21
statics of ANDREA1 named: they sit in no `.TRG` group, the room seam took them
away and put nothing in their place. Their rooms are not empty - group 121 has
27,252 triangles and group 221 has 2,803 - but the group covers only part of
the room: 121's own box is x 1024..2066 where the room reaches far past it.

So the seam is asked per static instead of per room. The renderer keeps, per
group, a sorted list of the 256-unit cells its own vertices fall in - a quarter
of a floor square on a side, packed ten bits per axis, built while the index
buffer is filled, about 25 bytes per thousand vertices - and answers whether it
draws anything inside a room-local box. A static's box is its own visibility
box turned by the mesh's `y_rot` through the engine's own sine table. Where the
answer is no, `DrawStaticObjects` draws that static the classic way inside an
HD room; every other one stays suppressed. A part's bounding box could not
answer this - one part of a Roman room spans the whole room - and walking the
triangles per static would be a million tests per lantern.

*Proof.* Twelve regression pictures - Rome facades, the flipmap pair, the Irish
trees, JOBY4's kelp, the fog bulb, JOBY3's bulkheads, RICH1's ray fan, RICH2's
wall lamps and marble, the sky dome, JOBY2's snow - rendered through the same
camera by the build before this round and by this one: **12 of 12 byte for byte
identical with `hd_reflect` off**, and with it on only the three rooms that
have a glossy part move at all, by at most 22 of 255. `RoomProbe -paritycheck`
on ANDREA1 room 19 reports 0.000 of 255 bare and 0.997 with a torch and fog,
both PASS, and `hd_motion 0` draws the picture fix round R3a drew, byte for
byte. The room pixel shader grew from 386 to **453** instruction slots and the
vertex shader from 330 to 333, against the 512 shader model 3 guarantees.

`RoomProbe` gained `-plane`, `-hasgeom`, `-shatter` and `-noreflect`, and
prints three `R3b:` lines - the flipbook, the water marks with the flooded
count, and the glossy parts - plus one line per destructible piece. The engine
writes `flipbook_pages`, `flipbook_dropped`, `flipbook_ranges`, `gloss_parts`
and `reflect` into its own `HDLOG ... motion=` line, `flipbook_calls` and
`gloss_calls` into the frame line, one `HDLOG ... statics classic=` line per
level and one `HDLOG static room=` line per static that changed sides.

---

#### Fix round G - the braid trembles in the drawing, and a guard on the shaders (#123)

The play test of the evening of 2026-09-06, one finding on Lara and one on the
build. **No new source file**: it changes `specific/hd/hd_lara.h`,
`specific/hd/hd_laradraw.{h,cpp}`, `game/hair.cpp`, the build scripts
`Preparation/Tools/Diagnose/be.bat` and `bshader.bat`, and the diagnostic tool
`ModelProbe`.

**G1 - "the braid still trembles."** Fix round F had proved the *simulation*
calm and it is. The play test's own log says so: 1,791 `HAIRLOG` lines out of
ANDREA2, and on the **924 frames where the root did not move by a single
unit**, the tip moved less than two units on 728 of them; the two long
stretches where she stands still settle to exactly 0 and stay there for fifty
frames on end. Of the 254 frames where the tip moved more than 40 units, 224
are frames where the *root* moved 10 units or more - that is Lara walking, and
the braid is meant to follow her.

What nobody had measured was the **angle**. `game/hair.cpp` gives link *n-1*
the spherical angles of the vector to link *n*:

```
(hair - 1)->pos.y_rot = phd_atan(dz, dx)
(hair - 1)->pos.x_rot = -phd_atan(phd_sqrt(dz * dz + dx * dx), dy)
```

and `dx`, `dy`, `dz` are differences of **longs**. For a braid hanging straight
down `dy` is the whole bone, 56, and `dx` and `dz` are single digits - so the
pitch is a solid -90 degrees and the yaw is not an angle at all but the
rounding of two small integers. One unit of change in `dx` swings it by ninety
degrees.

TR5 could live with that. Its braid is three rigid meshes and three **skinned**
connectors, and `GetCorrectStashPoints` exists precisely to renumber a
connector's vertex ring when two neighbouring links disagree by a quarter turn
- Core knew the yaw jumps and hid it. The HD braid is six **rigid** bones of
one plaited model, and a rigid plait turned about its own axis is visible.

`ModelProbe` gained **`-braidanim`**. Round F's `-braidsim` runs the chain in
the bind pose, which has no anchor motion at all; this reads the level's own
animation table, decodes the frames Lara really wears and rebuilds all eight
collision spheres and the braid root from them once per game frame - the same
node walk, the same skips through the packed rotation stream, the same frame
blending, the positions truncated to longs afterwards as `(long)aMXPtr[M03]`
does. The floor clamp is in, because with a real anchor at a real height a
braid 336 units long hanging from a head 251 units up reaches through the
ground. ANDREA2, 110 counted frames per animation, the tip's travel per frame
in the same four buckets the play test's log was counted in:

| animation | tip < 2 | 2-10 | 10-40 | > 40 | worst yaw jump | frames over 30 |
| --- | --- | --- | --- | --- | --- | --- |
| 103, breathing on the spot | 110 | 0 | 0 | 0 | **180** | **14** |
| 0, the run loop | 0 | 0 | 0 | 110 | 7 | 0 |
| 1, the walk loop | 0 | 1 | 109 | 0 | 7 | 0 |

**The braid stands still to within a unit and spins half a turn on one frame in
eight.** That is the tremble, and it is in the drawing and not in the physics.
The same table run with the pack spheres out, damped, thinner and thinner still
moves the tip by the same amount to two decimal places, so **no change was made
to the physics**: the pack of fix round D stays exactly as it is.

**The cure is free.** The drawn bone's own +z *is* the link direction, and
`braidcheck` proves that stepping one bone length along it lands on the next
link; a rotation about that same +z moves no vertex and lands the next bone in
the same place. So after `phd_RotY` and `phd_RotX` in `braid_matrices` comes a
`phd_RotZ`, and the only question is which value of it does not tremble.
`-braidanim` measures five candidates over the three animations, over the worst
of the five physics variants - the worst one-frame turn of any bone, in
degrees:

| rule | standing | running | walking |
| --- | --- | --- | --- |
| none - the raw yaw, as fix round F ships it | **180** | 7 | 7 |
| towards her facing, faded out per link | 0 | **167** | **179** |
| the whole chain's azimuth, every link | **45** | 3 | 1 |
| the same, faded out per link | **45** | 8 | 2 |
| the chain's azimuth, **held** while it is vertical | **0** | **3** | **1** |
| her facing, every link, always | 0 | 0 | 0 |

The last line is not the winner it looks: Lara never turns in the tool, so a
rule that says "whatever she is facing" cannot move there at all and its zero
is an empty one. The fifth is what ships, as `HDLaraBraidRollStep` /
`HDLaraBraidRolls`. **One** reference for all six bones - a rigid plait cannot
hide two neighbours twisting against each other - and the reference is the
azimuth of the **whole** braid, root to tip, because that vector is six bones
long where a link is one and its horizontal part is ten to thirty times better
conditioned. It is **held** while even that is noise: below 16 units of
horizontal span the reference does not move, above 48 it follows the braid
completely, and in between it follows part of the way, so there is no threshold
the picture can cross in one frame. "Towards her facing" fails the walk for the
same reason the raw yaw fails standing - a blend of two angles far apart jumps
when the weight jumps.

The rule holds a value from frame to frame, so it is advanced from
`HairControl`, which is the one place per frame where the chain is new; the
draw may run twice (the mirror) or not at all (first person) and only reads
what that worked out.

`HAIRLOG` gained four fields: **`rootstep`** and **`tipstep`** (how far the
root and the tip moved since the last frame - counting those by hand out of a
thousand lines is what this round had to do once), **`dyaw`** (the worst
one-frame change of any link's raw yaw, in degrees - the disease) and
**`droll`** (the same after the roll has taken the noise out - the cure, and
what the picture really shows; `-1` for the classic braid, which never had this
trouble).

*Still open, measured and not fixed:* the braid does not lie **on** the
backpack, it lies **in** it. Two spheres of radius 36 at z -52 in the torso
frame touch the slab's rear face on the centre line only; 19 units above the
top sphere's centre their surface is already 9 units in front of it, and the
simulated rest has links 1 and 2 at model z -61 and -69 where the slab runs
from -19 to -81. Round D's own measurement (y -197 to -68, z -92 to -30, |x| <=
30) is a **slab**, and pushing a link out of a slab needs the torso frame's
inverse in `HairControl` rather than two spheres. It is a decision, not a bug
fix, and it is written down here rather than guessed at.

**G2 - a guard on the shader bytecode.** Round R3b changed `hd_room.hlsl`,
never ran `bshader.bat`, and shipped a build whose source and whose picture
disagreed; nothing in the tree could say so and it took a play test and two
rounds to find. Every `.inc` now carries the **MD5 of the `.hlsl` it came
from** on its last line (`bshader.bat`, `:stamp`) and `be.bat` compares the two
before it builds. A **content** stamp and not a file date, because a fresh
clone gives every file the same date and a date guard would then either cry
wolf or say nothing at all; and a shader that is out of step is **rebuilt on
the spot** rather than reported, because a build that quietly draws the wrong
thing is the failure this exists to stop.

*Proof.* `ModelProbe -laracheck` 15 of 15 joints at 0.00000 px, PASS, and the
braid chain rule worst gap 0.00006 units over 5 joints, PASS - the roll is
about the bone's own axis, so that proof is untouched by it. `be.bat` on the
tree as it stands regenerates nothing; touching `hd_room.hlsl` makes it print
`be: regenerating shaders - hd_room_vs.inc hd_room_ps.inc out of step with the
HLSL` and rebuild them; putting the file back does it again, and all four
`.inc` files come back **byte for byte identical** to the checked-in bytecode
apart from the stamp line - which is also the proof that round E's own
regeneration was sound.

---

#### Fix round F - Lara: whose rig, and a braid that would not stand still (#123)

The play test of the morning of 2026-09-06, two findings, both of them about
what fix round D did to Lara. **No new source file**: it changes
`specific/hd/hd_lara.{h,cpp}`, `specific/hd/hd_laradraw.cpp`, `game/hair.cpp`
and the diagnostic tool `ModelProbe`.

**F1 - the holster rig was worn by outfits that did not want it.** Round D cut
the belt, the two pouches and the two thigh straps out of the London catsuit by
texture page and gave them to "any holstering outfit that has no rig of its
own". Measured, that rule reached three of the eight wardrobe bodies -
`ModelProbe -lara ... -guns pistols`, the `drawn:` line, before this round:

| outfit | drawn |
| --- | --- |
| `OUTFIT_CLASSIC` | body + `LARA_HEAD` + hand + `HAIR_OG` |
| `OUTFIT_RUSSIA` | body + `LARA_HEAD` + hand + `HAIR_OG` |
| `OUTFIT_VCI` | body + `LARA_HEAD` + **holster rig** + hand + `HAIR_OG` |
| `OUTFIT_TR3_ANTARC` | body + `LARA_HEAD` + **holster rig** + hand + `HAIR_OG` |
| `OUTFIT_TR3_NEVADA` | body + `LARA_HEAD` + **holster rig** + hand + `HAIR_OG` |
| `OUTFIT_TR3_COAST` | body + `LARA_HEAD` + hand + `HAIR_OG` |
| `OUTFIT_TR3_CATSUIT` | body + `LARA_HEAD` + hand + `HAIR_OG` |

Two of those three are wrong, and the user's photograph is one of them: black
leather straps over the orange Antarctica parka, the belt swallowed by the
padded jacket and the pouches standing off the hip at the width the London
catsuit's thin thighs want. Nevada is the same body and the same picture.

**A body without a rig is not a body asking for one.** The three are without
one for two different reasons. `OUTFIT_VCI` has none because vanilla TR5 hangs
pistols on that catsuit and the remaster left the pouches to the holster file -
that is a hole. Antarctica and Nevada have none because in TR3 those outfits
never wore a thigh rig at all: the geometry is finished as it stands and the
coat is modelled to sit flush - that is a design. So the rule is now a **table
and not a measurement**: `Outfit::wearsRig`, true for `OUTFIT_VCI` and nobody
else, with the page measurement kept beside it and the reason it is not the
rule. A wardrobe body added later gets a `false` and a `true` only after
somebody has looked at the picture.

Written down while measuring: the page cut keeps 207 vertices whose dominant
bone is node 14, left over from triangles the cut dropped, so the coverage mask
has bit 14 set. No surviving triangle uses them - `-onlypage 8205 -meshbits
0x4000` draws nothing at all - and the note is in the source so that a reader
of the `HDLOG` part line does not go looking for a piece of London on Lara's
head.

**F2 - the braid glitched about restlessly.** The user's words, with two frames
of video: in one the braid is long and thrown out to the side, in the next it
is short and tucked against her back. Round D had changed two things in
`HairControl`, the braid root and the two backpack collision spheres, and it
put **both of them into one of that function's two frame branches** - the
`else` half, which runs on a frame the animation does not interpolate. The
other half, `if (frac)`, kept the old root and six spheres. The engine takes
one branch or the other from frame to frame, so what the braid really saw was a
root that jumped between z -48 and z -33 and a backpack that came and went.
`hairs[0][0]` seeds the whole chain and every link's velocity is *how far it
moved since the last frame*, so a root that travels 15 units and back feeds the
braid 15 units of velocity for ever.

`ModelProbe` gained **`-braidsim`**, which transcribes `HairControl`'s chain
loop line for line - integer positions, the game's integer push-out, the same
rebuild from the angles - and runs it on this level's own Lara skeleton, her
collision spheres and the HD pack, in the bind pose, with no wind, no water
line and no floor. A braid that will not settle standing still in a dry indoor
room will not settle anywhere. Sixty steps, "how far the busiest link still
travels in one step" (ANDREA1, the ordinary torso anchor; the Russia anchor and
JOBY2, RICH1 and ANDY1 give the same verdict):

| regime | step 1 | step 10 | step 30 | step 60 | inside the pack |
| --- | --- | --- | --- | --- | --- |
| classic: root -48, 6 spheres | 0 | 0 | 0 | **0** | 15 |
| round D as built, 1 key frame in 4 | 12 | 2 | 1 | **3** | 13 |
| round D as built, every other frame | 12 | 4 | 2 | **2** | 13 |
| round D as it was meant: -33, pack always | 11 | 3 | 1 | **1** | 15 |
| round F: root -48, pack always | 5 | 0 | 0 | **0** | 9 |

So the tremble is measured, and the moved root does not settle even applied to
both halves: at -33 the chain hangs near enough to her neck for the neck sphere
and the pack sphere to push it in turn, and the two pushes make a small limit
cycle rather than a rest. **The root goes back to the classic -48 for every
adult Lara.** The 15.2 unit gap between the hairband and the first rigid link
comes back with it, and that is the user's own choice - the gap is quiet and
the fix was not. `HD_LARA_BRAID_ROOT_Z_HD` stays in the header with the
measurement, the refutation and the way to reopen it: move the DRAWN braid in
`hd_laradraw.cpp`, which builds its own matrices out of `hairs[0][n]`, and let
the physics never see it.

**The backpack stays, in both branches**, as `HdPackSpheres` - a function with
an `interpolate` switch rather than a block that can be forgotten in one place,
because that is exactly what happened. Round D said of it "this is the one
change no offline picture can prove"; a picture cannot, but the simulation can:
without the pack the braid comes to rest 15 units inside it, with the pack 9,
and the chain settles either way, the pack pushing two of the six links per
step while it rests.

`game/hair.cpp` writes one `HAIRLOG` line per braid per frame under `-log`:
`rootz` (which root the frame really used - it has to read the same number on
every line of a run; a line where it changes is the bug back again), the room
position of the root, how many spheres that frame tested (6 or 8, and it must
not alternate either), `packhits`, whether the HD wardrobe is drawing, and
where the tip ended up.

*Proof.* `ModelProbe -laracheck` 15 of 15 joints at 0.00000 px, PASS, and the
braid chain rule worst gap 0.00002 units over 5 joints, PASS. The `drawn:` line
now names the rig for `OUTFIT_VCI` and for no other outfit. Pictures: the
Antarctica and Nevada bodies before and after (3,451 and 11,257 pixels change,
all of them at the hip and the upper thigh), the VCI body before and after
(**0 pixels change** - the one outfit that should wear it is untouched), and
the rig alone out of its page.

#### Fix round E - rooms: the order of a frame, the glows, the shore (#123)

The play test of the evening of 2026-09-06, and the player's own thesis first:
*"the shaders have an alpha problem, everything BEHIND them is always slightly
wrong."* He is right about the symptom and it is not alpha - it is the order
the frame is drawn in. **No new source file**: this round changes
`specific/output.cpp`, `specific/hd/hd_level.{h,cpp}`, `hd_render.{h,cpp}`,
`hd_model.{h,cpp}`, `hd_object.{h,cpp}`, `hd_shader/hd_room.hlsl` and the
diagnostic tools `RoomProbe` and `TrgProbe`.

**E0 - what the three passes really do, measured on the device.** Read back
out of `hd_render.cpp` and confirmed against `RoomProbe`'s own state check:

| pass | blend | Z test | Z write | alpha |
| --- | --- | --- | --- | --- |
| 0 opaque | off | LESSEQUAL | **on** | `clip` at the material's own reference, 0 for all but materials 6, 7, 8 |
| 1 alpha | off | LESSEQUAL | **on** | `clip` at 0.5 - a test, not a blend |
| 2 additive | ONE / ONE | LESSEQUAL | **off** | none |

The model path is the same three. So two of the three write depth and sort
themselves out against anything else that writes depth, whatever order they are
drawn in; only the third does not - and it was drawn room-major, all three
passes of one room before the next, then the HD objects, then the classic
buckets. Any depth-writing draw that comes after a glow and stands behind it
therefore passes the depth test - the glow left no depth to fail against - and
paints over it.

**E6 - so the frame is drawn pass-major, and the glows last.** `HDFrameDraw`
is now everything of the HD path that writes depth, and a second call,
`HDFrameDrawGlow`, draws the additive pass of the same rooms and objects
**after `DrawBuckets`**:

    HDFrameDraw       every room's passes 0 and 1, then every HD object's
    DrawBuckets       the classic opaque world
    HDFrameDrawGlow   every room's pass 2, then every object's
    DrawSortList      TR5's own see-through polygons, last of all

which is the classic engine's own order and needs no rule of its own. The cost
is one more state bracket per frame and one more `prepare` per drawn room; the
room's lighting is worked out once and kept in the frame list, because
`FillRoomLighting` counts the lights it turns away and must not count them
twice.

*Proof.* `RoomProbe` gained `-with <group> <dx dy dz>`, which draws a
neighbouring room at its world offset, and `-oldorder`, which puts the
room-major order back - so both pictures come out of one build. RICH2 room 25
with room 23 behind it (the two classic origins differ by -1024, 0, +4096):
**11,667 pixels change** from one camera and **35,285** from another, every
one of them brighter with the new order, and the
difference image is exactly the two wall lamp cones, cut off along a straight
vertical line where room 23's geometry begins. That straight line is the
player's photograph. Ten single-room regression pictures are **byte for byte
identical** between the two orders, which is what the argument predicts: the
order can only matter where two rooms overlap on the screen.

**E9 - and the shader on disk was not the shader in the source.** Found while
rebuilding for E1: `hd_room_ps.inc`, the checked-in bytecode, does not compile
from the checked-in `hd_room.hlsl`. It is **636 instruction slots with eight
`pow`s in it** where the source gives **453 with none** - it is the Blinn
highlight that fix round R3b's own note says it *built, measured and rejected*
for taking the shader past the 512 that shader model 3 guarantees. R3b edited
the HLSL and did not run `bshader.bat` again, so the engine has been running
the rejected experiment ever since. Regenerated. Ten regression pictures and a
scene with a dynamic light are **byte for byte identical** across the swap -
the extra code was dead - so the cost was 183 instruction slots of portability
and nothing on the screen. The three other `.inc` files reproduce exactly.

**E1 - the pulsing was four times too strong.** Fix round R3a let the additive
weight breathe by a quarter either way. Measured on the player's own two shots
of the ANDREA church, the two candle glows move by **+7.8 and -9.3 percent of
the whole pixel** between two arbitrary moments of that sine. Measured on the
remaster's own two shots of the VCI corridor, three seconds apart, over the
shaft band on the far wall (108 columns): mean **124.6** against **130.1**, and
the column-to-column spread **14.3 %** against **16.5 %** of the mean. The
remaster's shafts **travel**; their level moves by 4.4 percent, camera drift
included. So the travelling wave stays and `HD_RAY_SWING` goes from 0.25 to
**0.06**. Measured back through `RoomProbe` on RICH2 room 25, eight seconds at
one second against the `-nomotion` still picture: the worst pixel moved **17.0
percent before and 5.4 after**, and the share of pixels moving more than 5
percent went from 0.82 to none.

**E8 - the shafts that were still white.** R3a let a material 2 weight outside
`(0, 1)` fall through to ONE and named the four tuples it did that to. The play
test found them. `TrgProbe` gained **`-mat [material]`**, which prints the
distinct `(material, param)` tuples of a level with their triangles per pass
and the groups they stand in - so the distribution R3a measured by hand is now
a measurement anybody can repeat. Over the fifteen files, material 2 has 36
tuples, 32 of them with `p1` in range, and grouped by the PRESET - the
`(param[0], param[2])` pair - the weight is all but constant inside a preset:

| preset | in-range weights | median |
| --- | --- | --- |
| (0.5, 0.0) | 0.10 | 0.10 |
| (0.5, 0.6) | 0.40 0.40 0.55 0.55 0.55 0.75 0.75 | **0.55** |
| (0.5, 0.8) | 0.40 0.40 0.40 | **0.40** |
| (0.5, 1.0) | 0.20 | 0.20 |
| (1.0, 0.1) | 0.05 x5, 0.20 x4 | 0.05 |
| (1.0, 2.0) | 0.20 0.20 | 0.20 |
| (1.5, 0.0) | 0.10 x4 | 0.10 |
| (1.5, 0.4) | 0.30 x5 | 0.30 |

The four tuples that carry no weight belong to two of those presets - ANDY1,
ANDY2 and ANDY3's window glows are (0.5, 0.8) and RICH1's lift shaft is
(0.5, 0.6) - so a part with no readable weight takes **its own preset's**
instead of full white, and a preset this game does not have takes 0.20, the
median of all 32, so that "no number" can never mean "full white" again.
Measured through `RoomProbe -all -black`, the additive pass alone over a black
ground, mean brightness as a share of white against the remaster's own 16 to
31 percent:

| scene | before | after |
| --- | --- | --- |
| RICH1 room 13, from the shaft floor | 36.3 %, 7.3 white | **21.4 %, 0.1** |
| RICH1 room 13, camera inside the fan | 73.4 %, 35.5 | 48.7 %, 7.5 |
| ANDY1 room 194 | 85.5 %, 11.8 | 51.2 %, 0.0 |
| ANDY2 room 34 | 69.1 %, 23.8 | 34.2 %, 0.0 |
| ANDY3 room 86 | 38.6 %, 0.2 | 17.8 %, 0.0 |
| ANDY3 room 150 | 26.9 %, 0.0 | 15.7 %, 0.0 |

Not one saturated pixel is left where a player stands. The second line is the
camera INSIDE the fan of quads, where every one of them overlaps every other
and a ONE / ONE sum saturates whatever weight it is given; that is stacking and
not weighting, the classic path has no answer to it either, and it is named
rather than tuned away. `-black` is new: an additive pass on the tool's magenta
ground starts every covered pixel at 170 of 255 and cannot be measured at all.

**E7 - the step in the wall at a room boundary is the remaster's own art.**
Three things could put it there and two of them are ours. The dynamic lights
are not: the HD path culls a light against the whole ROOM's box where the
classic path culls it against a ROOMLET's, with the same test and the same
falloff (`drawroom.cpp:1043` against `hd_level.cpp:1009`), so it offers a light
wherever the classic path does and more; the only way to lose one is the eight
light cap, and the log counts every light that hits it. The third is the art,
and `TrgProbe` gained **`-seam <groupA> <groupB> <dx> <dy> <dz>`** to measure
it: it pairs the vertices two rooms have at the same world position and reports
what each side's baked light says. Over four RICH2 portals:

| portal | shared positions | mean difference | worst |
| --- | --- | --- | --- |
| 25 / 23 | 14 | 0.174 | 0.438 |
| 23 / 5 | 14 | 0.209 | 0.553 |
| 23 / 24 | 18 | 0.203 | 0.461 |
| 23 / 27 | 18 | 0.234 | 0.628 |

on a 0..2 scale - up to 31 percent of full brightness at the same point in
space, on every portal measured. **Not one pair has a side above 1**, so the
deliberate deviation of stage S2 (no 128 split) does not widen it either: the
step is drawn exactly as the classic path would draw its own. What our code did
add on top of it was E6's cut, which puts a hard straight vertical edge exactly
at a portal - and that is what is fixed.

**E3 - and the wave that climbed the bank.** Fix round R3b floods the surface
mark into the box of the classic corners and lifts every marked vertex by up to
one and a half times `HD_WATER_SWELL` - 36 units, and 36 of them upwards,
because y counts downwards in this engine. The vertices at the water's own edge
carry the mark like every other, so the surface rose over the coping it should
meet: on the player's two shots of the Rome cistern a dark band stands exactly
where the lit coping is, it covers the coping and not the wall below it, and it
**moves** between the two shots (x 875..1055 in the first, 960..1130 in the
second), which is the travelling wave and not a texture.

Water goes flat where it meets the land. The flood box is kept per room and per
height now, handed to the vertex shader in `gWaterBox` (c15), and the swell
fades to nothing inside one square of its edge - one square because a TR5 water
quad is one square, and anything shorter would fade inside a single triangle
and read as a crease. *Proof:* `RoomProbe -noshore` puts the old swell back out
of the same build. ANDREA1 room 33 with a marked plane, against the same room
with the water still: the worst pixel moved **255 of 255 before and 111 after**,
while the number of pixels that move at all is unchanged (72,676 against
70,586) - the movement is kept, the tear at the edge is gone. In the picture
the marked patch is a lifted, torn hump with a hard dark rim before, and a flat
surface that meets the floor after.

**E2 - and why only the Irish water animated.** *"The right texture is missing,
only ANDY got the right one."* The answer is in the player's own log:

| level | HD vertices marked | surface bit | whole-room bit |
| --- | --- | --- | --- |
| JOBY4 | 32 | 32 | 0 |
| RICH2 | 47 | 47 | 0 |
| ANDY1 | 218 | 218 | 0 |
| ANDREA1 | 267 | 145 | 122 |
| ANDREA3 | 291 | 0 | 291 |
| ANDREA2 | 809 | 69 | 740 |

The classic path answers the water question on **two** bits - a plain water
room on one, a `ROOM_REFLECT + ROOM_REFLECT_CEILING` room on the other
(`drawroom.cpp:373`) - and fix round R3b flooded only the first, on the grounds
that the second "is not a plane at all". In the Rome levels the water is all on
the second bit, so nothing was flooded there and the surface kept the sliver of
marks the classic corners gave it. ANDY1's water is all on the first bit and
animates; Rome's stands still. Measured on the player's shot against the
remaster's recording of the same pool from the same place: the walkway beside
it matches to within 6 percent per channel (ours R101 G94 B54, the remaster's
R95 G87 B49), while the water is **achromatic in ours** (R33.7 G33.4 B32.7)
where the remaster's is green (R27.7 G30.5 B22.5) and its highlights reach 118
of 255 against our 85. Three readings were tried and disproved before this one:
it is not a flipbook (material 13 lives only in JOBY2, JOBY3, JOBY5, RICH1 and
RICH2 and its fifteen tuples are CCTV walls and monitors), not a second layer
of material 1 (its four floats are two scroll speeds and two times over all
fifteen files), and not the classic WaterTable shimmer washing the colour out
(measured: the shimmer raises the surface from R55.5 G51.9 B30.6 to R71.8 G65.8
B42.5 and its channel spread from 24.9 to 29.3 - it brightens, it does not
desaturate).

So **both** bits are flooded now, each into its own box per room and height,
and a vertex is marked only at a height and inside an extent the classic mesh
already marked - the same rule, twice. And with it a correction that had to
come with it: the whole-room bit says yes to almost every vertex of such a
room, walls and ceiling included, so the UV wobble and the swell were moving
the WALLS in Rome. The classic path displaces nothing but its own `nWVtx` set
and that set is empty in all of TR5. Geometry and UV now ride on the marked set
**at the surface plane** - the room's own `minfloor` - which in a first-bit room
is what the selector already meant; the colour term keeps the whole mark, so
the room still shimmers exactly as it did. *Proof:* `RoomProbe -roomplane`
marks a plane on the second bit and `-plane`/`-roomplane` now also set the
room's surface height and its selector, as the engine does. ANDREA1 room 33
with a second-bit plane: **5 marked HD vertices before, 64 after**, 59 of them
from the flood. Ten regression pictures byte for byte identical, because none
of them has water.

*Proof of the round.* Ten regression pictures - Rome facades and the missing
statics, the Irish gravel, ANDY3's window glow, JOBY2's pool room, JOBY4's kelp
and caustic, RICH1's ray fan, RICH2's wall lamps, RICH3, the sky dome -
rendered through the same camera before and after: **six of ten byte for byte
identical**, and the four that move are the four with a swinging additive part,
by at most 6 to 30 of 255, which is E1 and nothing else. `RoomProbe
-paritycheck` on ANDREA1 room 19, RICH2 room 25 and JOBY4 room 19: **0.000 of
255, PASS** on all three. The state bracket: 0 of 58 watched states changed.
The room pixel shader is **453** instruction slots (it was reported as 453 and
was really 636 - see E9) and the vertex shader 342 (it was 333), against the 512 shader
model 3 guarantees.

`RoomProbe` gained `-with`, `-oldorder`, `-black`, `-noshore` and `-roomplane`;
`TrgProbe` gained `-mat` and `-seam`. `HDRenderShoreOverride` exists for the
tool alone and the engine never calls it.

#### Fix round H - rooms: the water that was never there, the glass, the shafts (#123)

Four reports from the play test, and the first of them upended its own
diagnosis. All four were measured against the fifteen `.TRG` files and the
fifteen classic levels before a line was changed.

**H1 - the water surface the Remaster does not draw.** The player's picture of
a VCI pool shows dark, patternless blotches where the Remaster shows a moving
surface, and his own look in trview said why: *"the vanilla water faces carry a
wave texture, in HD the assignment for the face is missing"*. The reading that
followed from it - the `.TRG` gives those faces texture layer 0, and the
renderer's layer-0 fallback paints them with the level's first page - is
**wrong, and the file says so**: over all fifteen levels, `TrgProbe -layer0`
finds **0 of 12,247,000 vertices** on layer 0, and every level's
`fallbackPageTris` is 0. No face in the game is missing its page.

What is missing is the GEOMETRY. The classic engine draws a water surface as
ordinary room faces at the room's own `minfloor`, and the shimmer branch that
makes them move opens with `pos.y == CurrentRoomPtr->minfloor` in a room
carrying `ROOM_REFLECT` or `ROOM_MIST` (`drawroom.cpp:531-545`). Held against
the `.TRG` plane by plane and box by box - `TrgProbe -watergap`, and
`RoomProbe -watersurvey`, which asks the ENGINE's own gate - over the game's 93
reflecting rooms:

| level | reflecting rooms | classic faces in the plane | .TRG triangles there |
|---|---|---|---|
| JOBY2 | 5 | 87 | 54, and 0 in four of the five rooms |
| JOBY5 | 25 | 213 | 1,458, but 0 in thirteen rooms |
| ANDY3 | 23 | 223 | 0 |
| ANDREA2 | 13 | 102 | 214, 0 in ten rooms |
| ANDY1 | 6 | 30 | 45, 0 in five rooms |
| ANDREA1 | 3 | 33 | 0 |
| RICH3 | 4 | 32 | 198, 0 in two rooms |
| JOBY4 | 1 | 2 | 0 |
| ANDREA3, JOBY3, RICH2 | 15 | 71 | every plane covered |

JOBY2's room 66 is the pool of the player's picture: **24 classic faces of ONE
texinfo at y -1280, and 0 `.TRG` triangles in that plane**. The HD room draws
the pool FLOOR - a black-and-white marble chequerboard, page 489 - and nothing
over it, which is exactly what he photographed. There was no water there at
all, so no texture and no shader could have been wrong about it.

So where the level has a water surface and the `.TRG` has none, the level's
**own** face is drawn on top of the HD room, by the classic path, with its own
texinfo - and therefore its own animated texture range - its own light and the
shimmer the classic path has always given it. Nothing is invented: the surface
that comes back is the one the level ships.

The two tests a face has to pass are the engine's own. Every corner at the
room's `minfloor`, which is the classic height test; and every corner marked by
`aRoomVertexWaterMarks` as a shimmer-surface vertex, which is the classic
predicate with its portal walk and all. Whether the `.TRG` already covers that
piece of plane is asked of a new per-group table of horizontal art, kept by
1024-unit SECTOR and not by one box: JOBY2's room 66 draws 24 triangles at the
water's own height and all of them are in two corners of the room - the coping,
not the water - and a single box round them would span the whole room and
answer "the Remaster built this pool". The decision is then taken **per room**,
by majority, because half a pool is worse than either whole. Measured through
the engine's own gate: JOBY2 restores 87 faces in 5 rooms, JOBY5 86 in 8 of
30, ANDREA1 34 in 4; JOBY3 and RICH2 restore nothing at all, which is right -
the Remaster built those surfaces.

*Cost:* one classic roomlet transform per visible room that carries restored
water, and nothing at all for every other room in the game, which is most of
them. *Not done:* the water is the level's own art, not the Remaster's. The
Remaster's effect atlas was measured for a wave band to put on it instead
(`EFFECT.DDS`, rows 936..1008 and 1014..1060, full width, mean luminance 24 of
255) and it is a very dark modulation strip, not a surface texture; a page
cache for named sheets was therefore not built.

**H2 - glass drawn as a mask of itself.** The player: dark shards in every pane
of ANDREA2's glass wall where the Remaster shows clear glass. The alpha pass of
stages S1 and S2 is an alpha TEST and nothing else - `clip(texel.a - 0.5)`, the
surface drawn solid - which is right for a grating and wrong for glass. The
wall is one part, `TrgProbe -parts 132`: material `0x800E`, which is the
destructible bit 15 over material **14**, 2,146 triangles in the alpha pass on
page 3007, a sheet whose alpha is 80.6 % below 16 and whose glass is the rest,
at 0.5 to 0.75. At a test of 0.5 that pane comes out as a hard black-and-white
mask - the shards.

The alpha pass of all fifteen files carries five materials: 0, 6, 7 and 8, the
cut-outs, whose pages really do cut; 1 and 22, the sky's cloud layers and the
dome itself; and **14**, which is the glass - 4,292 triangles in ANDREA2,
15,904 in JOBY3, 19,028 in JOBY5, 2,510 in RICH2, 3,894 in RICH3, 432 in
RICHCUT2, 576 in TITLE. `param[1]` of a material 14 part is its opacity - the
same field fix round R3a reads as that material's ADDITIVE weight - and the
game's values are 0.1, 0.2333, 0.8417 and 1.0.

So material 14 in the alpha pass is a phase of its own now: `SRCALPHA` /
`INVSRCALPHA`, no depth write, no alpha test, drawn after everything opaque and
before the glows, in the same place fix round E6 put the additive pass and for
the same reason. **96 parts in eight levels, 96 draw calls**; seven levels have
none and are untouched. One more thing came out with it: `MaterialAddWeight`
and its neighbours compared `part.material` raw, so a destructible copy of a
material - bit 15 and a counter in bits 8 to 14 - matched nothing; the material
is now read as its low byte, which no material in any of the fifteen files
exceeds.

Pictures, ANDREA2 room 132 through the same camera, `-noblend` against the same
build: before, a black shard pattern in all 35 panes; after, the panes are
see-through and only their frames stand. The wall is also one of the round-3b
destructible pieces (`0x800E`, copy 0, rooms 65 and 132), so it still vanishes
when the game breaks it - all three of the engine's shatter paths, Lara's gun
(`control.cpp:1931`), the laser sight (`camera.cpp:966`) and the trigger
action 333 (`objects.cpp:755`), clear `MESH_INFO::Flags` bit 0, which is what
`UpdateDestructibles` reads.

**H3 - the great hall's shafts, down to a minimum.** The player, after round
R3a: ANDREA1's hall - the one with the gladiator statue, room 181 - is still
too strong. The hall and the VCI corridor are not the same thing in the file:

| | material | the four numbers |
|---|---|---|
| the hall | 2 | (1.5, 0.3, 0.4, **16.0**) |
| the corridor | 2 | (1.0, 0.2, 0.1, 1.0) |

Different preset - round R3a showed that `(param[0], param[2])` moves with the
page - and a different time. `param[3]` = 16 seconds is the slowest drift in
the whole game and belongs to exactly ONE tuple, carried by 34 parts in
ANDREA1 (12 groups), JOBY2 (21) and RICH3 (1): the sun shafts. The next slowest
in the game is 5 s, so the tuple is the rule and no room number has to be
written down. RICH2's and ANDREA2's shafts share the preset but not the time
(2.5 s and 3.07 s) and are untouched, which is why the tuple and not the preset
is the key.

Measured on room 181's own additive pass against black, same camera:

| | mean | peak |
|---|---|---|
| before | 6.8 % | 58.0 % of white |
| after | **1.7 %** | **15.7 %** |

against a streak reference of 16 to 31 % of white (round R3a) - faint, and
below it, which is what was asked for. RICH1's ray fan and RICH2's wall lamps
come out **byte for byte identical**.

**H4 - the ripple on the wall at the waterline.** The mark that says "this is
water" is carried from the classic mesh to the finer Remaster one BY POSITION -
the corner match and round R3b's plane flood both - and a wall standing at
exactly the water's height inside the pool's box answers yes to it. It then
gets the surface's UV wobble and its caustic, and on a wall triangle whose
other two corners are lower that is a sheared, darkened triangle standing on
the waterline. Measured through the engine's own flood: **34 of 257** marked
vertices in RICH2's room 125 and **205 of 374** in JOBY5's room 184 are not a
flat surface at all.

A water surface is flat, so its normal is vertical and a wall's is not. The
geometry half - the swell, the UV wobble, the caustic - now asks for that as
well; the COLOUR term keeps every one of those vertices, because the classic
path asks the height and not the normal (`drawroom.cpp:537`) and stage S2's
parity is measured against it. Costs three vertex shader instructions. Measured
before and after through the same camera in RICH2 room 125: 0.6 to 2.0 % of the
picture changes, all of it in one narrow strip along the junction, worst 33 of
255.

*Named as unfinished:* the player's own screenshot of the wedge was not
available here, so what is proven is the mechanism and the count, not that this
is the very triangle he saw.

**The regression.** `RoomProbe -paritycheck` on ANDREA1 room 19: **0.000 of
255, PASS**. ANDREA1 rooms 12 and 19, JOBY4 room 40, RICH1 rooms 13 and 136,
RICH2 rooms 25 and 26 and the ANDREA1 sky dome, rendered through the same
camera with and without each of the three overrides: **byte for byte
identical** except where the fix is meant to bite. `be.bat` regenerates the
shader bytecode from its own MD5 stamp and reports no error.

`RoomProbe` gained `-watersurvey`, `-flatart`, `-noblend` and `-noupright`, and
its picture line now carries the peak and the white share as well as the mean;
`TrgProbe` gained `-layer0`, `-flat`, `-parts`, `-surface` and `-watergap`;
`DdsProbe` gained `-rows` and `-cut`. `HDRenderBlendOverride` and
`HDRenderUprightOverride` exist for the tool alone and the engine never calls
them.

#### Fix round I - Lara: the backpack, the anchor, the open collar, the hands (#123)

Four reports, all about her and all about geometry the wardrobe does not ship.

**I1 - "the braid lies in the backpack."** Fix round G had already written this
one down as a decision rather than a fix: *"the braid lies in the pack, not on
it - round D's own measurement is a slab, and two spheres touch it on the
centre line only"*. The user asked for the measurement to be finished, and it
is. `ModelProbe -braid` now prints the pack as a **box** in the torso node's
own frame - the node 7 vertices more than 30 units behind the torso joint,
which is the same point set round D took its spheres from:

| outfit | x | y | z |
|---|---|---|---|
| `OUTFIT_CLASSIC` | -57..59 | -197..-68 | -92..-30 |
| `OUTFIT_RUSSIA` | -60..62 | -197..-47 | -92..-30 |
| `OUTFIT_VCI` | -58..57 | -197..-68 | -92..-30 |
| `OUTFIT_TR3_ANTARC` | -59..60 | -197..-48 | -92..-30 |
| `OUTFIT_TR3_NEVADA` | -58..60 | -197..-67 | -92..-30 |
| `OUTFIT_TR3_COAST` | -58..60 | -197..-67 | -92..-30 |
| `OUTFIT_TR3_CATSUIT` | -58..60 | -197..-67 | -92..-30 |

**One box serves all seven**, and the table is the argument: the depth is the
same number in every one of them, the width agrees to three units, and the top
to none at all. The only column that moves is the bottom, and the two that move
it are the Russia coat and the Antarctica parka - longer coats, not deeper
packs, because "node 7 and more than 30 behind the joint" picks up their back
skirt as well. The box takes the pack's own bottom, -68, and leaves the coat
tail to the torso sphere, which for the Russia outfit is nailed to radius 94
and covers it.

The two spheres are **withdrawn** and `HairControl` pushes the braid out of the
box instead, along whichever of the six faces is nearest, the rear one
preferred (a divisor of 2 on its own depth, so a link genuinely nearer another
face still leaves by it - and said honestly: over the three animations
`-braidanim` runs, "the box with no rear preference" comes out *identical* in
every column, because the braid meets this pack from behind and the rear face
is the nearest one anyway; the preference is kept for poses the tool never
strikes). The margin is the plait's own radius, measured over
`HAIR_OG`: 17.5, 17.5, 15.0, 12.8, 10.6 and 9.0 about each bone's axis, which
gives the six moving points 18, 18, 15, 13, 11 and 9. **Behind the HD guard in
both halves of `HairControl`**, out of one flag read once per frame, which is
the shape round D's mistake asks for.

*Proof, `ModelProbe -braidsim`, sixty steps in the bind pose, the deepest a
link stands inside that box:*

| regime | step 1 | step 10 | step 30 | last | in the pack |
|---|---|---|---|---|---|
| classic: root -48, 6 spheres | 14 | 1 | 0 | **0** | 38 |
| round F/G: root -48, two pack spheres | 14 | 1 | 0 | **0** | 38 |
| round I: root -48, the box | 45 | 10 | 0 | **0** | **7** |
| round I: root -33, the box | 63 | 18 | 0 | **0** | **12** |

The braid stood **38 units** inside the pack with the spheres and stands 7 with
the box, and both box lines still settle to zero and stay there. Under Lara's
own animations (`-braidanim`, `ANIM_BREATH`, the run loop and the walk loop)
the tip never moves more than 14 units in a frame and **no frame is over 40**,
which is the figure that was not allowed to rise.

**I2 - "the anchor is still wrong."** The braid began 15.2 units behind the
skull instead of coming out of the hair knot. Fix round D had measured exactly
that and moved the root from -48 to -33; fix round F withdrew it for two
reasons, and the second was *"it does not settle even applied to both halves -
at -33 the chain hangs near enough to her neck for the neck sphere and the pack
sphere to push it in turn"*. That reason was about the pack being **spheres**.
The table above asks the question again with a box, and the answer has changed:
the near root settles. So the root is -33 whenever the HD wardrobe draws and
-48 otherwise, written in **both** halves of `HairControl` out of the same one
flag as the box - which is what makes it impossible for the two to disagree,
the failure of round D. The picture is the whole point: side by side, the plait
now leaves the hair tie instead of hanging clear of it, and lies **on** the
pack rather than through it, which is what the Remaster's own screenshot shows.

**I3 - the open collar in first person.** Stage S8's near cut takes the neck
stump off her body, and the body is a hollow shell: what is left is a shell
with the neck bored out of it, and since S8 also culls back faces one looks
clean through her - *"the inner faces, the backpack straps"*. No file in the
wardrobe is that lid, and there could not be a general one, because its rim is
the outfit's own body at the height the cut reaches.

So the lid is **built**. `HDModelSlot::synth` is new: a maker is handed the
`.TRM` of another slot and fills an `HDTrmModel` of its own, which `hd_model`
then treats exactly like a file it read - same page cache, same skinning, same
bind pose, same draw. `HDLaraNeckCap` is the one maker there is.

*The measurement it stands on,* `ModelProbe -neck`, the torso node's vertices
against the axis from the torso joint to the head joint (`s` down it from the
head joint, `r` off it), `OUTFIT_VCI` and the other six the same to a few
units:

| s | verts | r min | r mean | r max | eye 48 | eye 0 |
|---|---|---|---|---|---|---|
| 0 | 18 | 7.2 | 12.6 | 20.1 | 50.8 | 18.6 |
| 20 | 34 | 12.4 | 19.5 | 24.5 | 37.4 | 35.5 |
| 30 | 426 | 14.9 | 40.3 | 76.1 | 49.0 | 44.9 |
| 40 | 484 | 17.1 | 47.1 | 81.9 | 54.9 | 55.0 |
| **50** | 256 | 20.3 | 54.4 | 82.5 | **63.9** | **65.0** |
| 60 | 547 | 9.9 | 64.3 | 80.4 | 69.2 | 76.0 |

Down to s = 20 the neck is a tube of eighteen to thirty vertices; at s = 30 it
bursts open into the shoulders. And the two eye columns say where the 60 unit
cut falls: it eats the tube and stops in the collar. **The rim therefore sits
at s = 50**, where every vertex is at least 61 units from either eye - so the
body's own surface at the seam survives the cut and the cap is sewn to
geometry that is still being drawn.

The cap is one vertex per spoke (the one furthest off the axis within half a
sector, the window widening where a chest's outline has no vertex at all), 32
spokes and 6 rings, **192 vertices and 352 triangles**, the radius falling off
as a quarter cosine and the middle sunk 24 units into the chest so that no part
of it is ever nearer the eye than the rim. Normals accumulated from the
triangles. Its rim positions are the outfit's own, unmoved, so the two surfaces
share an edge; the texture is the rim's own at the rim and the ring's average
in the middle. Drawn in first person only, on node 7, with **no near cut of its
own** - the cut is the thing it closes - and double sided.

*Proof:* `ModelProbe -firstperson -nocap` against the same without it, at 60
and 75 degrees down and at both eye positions, for `OUTFIT_CLASSIC` and
`OUTFIT_VCI`. Looking down with the eye at the joint the hole showed the
background and her own boots through her chest; it is closed. With the eye 48
forward, where there is no hole, the two pictures are identical. All eight
outfits build a cap (`the neck cap: 32 spokes, 6 rings, page N, k rim vertices
re-homed, 352 triangles` in the log).

**I4 - "the sheen is on the body and the head; the hands go empty."** Measured
first, with a new `ModelProbe -onlypart` that draws one part of the composite
and nothing else, each part rendered with the sheen on and off and the two
pictures differenced:

| part alone | drawn px | lit px | share | peak |
|---|---|---|---|---|
| the body (`OUTFIT_CLASSIC`) | 53,682 | 20,772 | 38.7 % | 13 |
| the face (`LARA_HEAD`) | 4,403 | 2,000 | 45.4 % | 16 |
| empty hands (`HAND_BARE_REST`) | 1,475 | 981 | **66.5 %** | 15 |
| the Desert Eagle hand | 2,224 | 1,396 | **62.8 %** | 36 |
| the pistol holsters | 1,782 | 721 | 40.5 % | 36 |

**The hands were never empty** - they are the most strongly lit parts of the
whole composite. What makes them read as matt is that a Blinn-Phong exponent of
34 on a hand-sized object is a highlight one or two pixels wide, beside a
catsuit whose whole chest lights up.

But the hunt found two things that really were wrong, and both are one number
having to describe two materials:

1. **A `HAND_` model is a hand *and* what is in it**, on two pages of one file.
   Measured: `HAND_BARE_REST` has 1 page, `HAND_BARE_DEAGLE` 2,
   `HAND_GLOVES_REST` 3, `HAND_GLOVES_DEAGLE` 4 - one extra page every time,
   whichever gun and whether or not she wears gloves. One sheen for the draw
   therefore made her skin metal. `HDModelSetPageSheen` names a material **per
   texture page**, which is already the unit a draw call is cut to, so it costs
   one constant write on the ranges that have one and nothing at all on every
   other model in the game. The rule that fills it is the measurement above:
   every page a weapon hand has that the *rest* hands have not got is the
   weapon.
2. **A gloved outfit shone like bare skin.** The hand's own material now
   follows the outfit - skin 0.12/10 bare, leather 0.18/20 gloved - which is
   the same rule the body has had since fix round D.

*Proof:* the `sheen:` line of `ModelProbe`, `OUTFIT_CLASSIC` against
`OUTFIT_RUSSIA`: `empty hands 0.12/10` becomes `0.18/20`, and every weapon hand
reads `0.12/10 [held 0.35/34 on 1 page(s)]` - the hand's material for the draw
and the weapon's on its own page. `HDLOG ... lara part=` carries `held=` and
`pagesheen=` per part.

**Nothing outside the HD path moved.** With HD off `HdPackFrame` returns false
at its first line and touches no matrix, `HdPackPush` returns false at its
first, the root reads the classic -48, and the sphere loop tests the same six
spheres it always did - `ModelProbe -braidsim`'s "classic: root -48, 6 spheres"
and "root -48, no pack at all" come out identical, position for position.
`ModelProbe -laracheck`: 15 of 15 joints at 0.00000 px, **PASS**, and the
braid's chain rule at 0.00006 units over five joints, **PASS**.

`ModelProbe` gained `-braidrest` (draw the braid where the *simulation* leaves
it, by regime, instead of in a studio pose), `-classicroot`, `-nocap` and
`-onlypart`; `-braid` prints the pack as a box and the plait's own radius,
`-neck` prints the neck as a tube, and `-braidsim` and `-braidanim` gained the
box regimes.


#### Fix round J: the neck as a cylinder, and the lid that fits it (#123)

The play test of fix round I: *"the neck cap is a start"* - and then three
photographs of what is wrong with it, plus a fourth of a jagged wrist.

**J1 - the cap was built at the wrong height, and everything followed from
that.** Fix round I put its rim at `s = 50` down the neck axis, because that
was the depth at which the body's own surface survived stage S8's sixty-unit
sphere. Measured again with `ModelProbe -neck`, which gained a third table for
it - the torso node's own vertices by depth `s` down the axis and radius `r`
off it, `OUTFIT_CLASSIC`, and the other seven read the same to a few units:

| s | r min | r max | what it is |
| ---: | ---: | ---: | --- |
| 0 | 7.9 | 17.3 | the stump, a tube of 6 vertices |
| 10 | 11.0 | 20.7 | still a tube, 8 |
| 20 | 12.4 | 22.3 | still a tube, 18 |
| 25 | 17.0 | 31.2 | the tube opening out, 24 |
| 30 | 14.9 | 67.0 | **burst** - 212 vertices, the shoulders have joined |

At `s = 50` the body is not a neck but a **chest**, so "the vertex furthest off
the axis in each of 32 sectors" was a **shoulder** and the rim was the outline
of her whole torso, `r` running from 20 to 82 in one ring. All three of the
user's findings are that one number:

1. **a flat skin-coloured plate at the collar, seen from outside.** The dome
   rounded each spoke towards the ring's *mean* radius, so a spoke whose rim
   sat at 20 was pushed out to 27 by the second ring - seven units through her
   own skin.
2. **black inside the neck.** The middle of the dome took the *average* of the
   32 rim texture coordinates. The average of a scattered set of points on an
   atlas page is a point in the middle of the page, and the middle of page 8310
   is empty. An averaged coordinate is a coordinate; it is not the average of
   the colours at them.
3. **"the polygons look random."** A rim that runs from 20 to 82 units off the
   axis is not a ring but a star, and the strips between its spokes are
   slivers.

**So the cut is a cylinder now and not a sphere.** The thing that has to go is
a neck; a neck is a tube about the neck axis; and *nothing else of her is
inside that tube*. The cut is therefore everything less than
`HD_LARA_NECK_CUT_S` = 30 units down the axis from the head joint and less than
`HD_LARA_NECK_CUT_R` = 30 units off it, worked out in the **bind pose** in the
vertex shader (`gNeck`, `gNeckAxis`, registers c162 and c163) so that it takes
the same patch of her skin in every frame and at every camera angle, and
carrying a **node mask** (`gNodeLight[].a`, the one component of that register
that was never light) so that only the torso can be clipped by it at all.

What that leaves standing, nearest the eye, over the two eye positions the
first person camera really uses - and 50 units is her own chest, which is where
the classic engine leaves it too:

| outfit | eye 48 | eye 0 |
| --- | ---: | ---: |
| OUTFIT_CLASSIC | 60.4 | 52.0 |
| OUTFIT_RUSSIA | 54.7 | 48.7 |
| OUTFIT_SUBSUIT | 32.7 | 33.5 |
| OUTFIT_VCI | 60.4 | 52.0 |
| OUTFIT_TR3_ANTARC | 48.0 | 43.4 |
| OUTFIT_TR3_NEVADA | 60.4 | 52.0 |
| OUTFIT_TR3_COAST | 60.4 | 52.0 |
| OUTFIT_TR3_CATSUIT | 60.4 | 52.0 |

**And the cap is rebuilt on the cut's own edge.** Its rim is, per sector, the
body's own vertex nearest the boundary of that cylinder - which is the mouth of
the neck - pulled back onto the boundary where the sector has no crossing at
all (the mouth is not a circle, and where it opens into a shoulder the nearest
surviving vertex can be fifty units off the axis). Three lines answer the three
findings: no ring of the dome is ever further off the axis than its own rim
vertex, so it cannot poke through the skin; the inner rings wear the texture
coordinates of **one real rim vertex** - the middlemost of them on the page -
rather than an average; and the ring is a ring because it is taken where the
neck is still a neck. 32 spokes, 4 rings, 224 triangles.

*Proof.* `ModelProbe -neck`, the new cylinder scan: over all eight outfits and
nine candidate `(S, R)` pairs - 72 combinations, every one of them larger than
what ships - the cylinder reaches **not one arm vertex**. The cap's own log
line now carries the ring it was built on and a self-check: `the rim r
20.4/26.4/30.0 s 27.8/29.9/30.0, 0 vertices outside their own spoke`
(`OUTFIT_CLASSIC`; it read `r 20.4/27.9/53.7` before the clamp). And the
pictures: `ModelProbe -firstperson` at 60, 75 and 90 degrees down with the eye
both 48 units forward and against a wall, and `-showcap` - a new switch that
draws the cap in an ordinary third-person picture, where a properly sunk cap is
**invisible**, which is the user's finding (a) answered by a photograph.

**J3 - the jagged wrist is gone with the sphere.** It was the sphere: a cut at
a fixed distance from the eye does not know a forearm from a neck, and a hand
brought up to the camera crossed it. There is now no cut anywhere in the HD
path that depends on where the eye is, and the cylinder is restricted to node 7
besides, so an arm vertex is never even tested. The measurement above is the
same one twice over.

**J2 - "the hands of the TR3 outfits do not shine."** Measured, and it is not
what the code does. The four Tomb Raider III outfits take the same
`HAND_BARE_*` models, on the same pages, with the same materials as the Rome
one - the `sheen:` line reads `empty hands 0.12/10` and `the Desert Eagle
0.12/10 [held 0.35/34 on 1 page(s)]` for every one of them. Rendered twice with
`-onlypart 9`, once with the sheen and once with `-nosheen`, and differenced:
`OUTFIT_CLASSIC` changes **65.6 %** of the drawn pixels by a mean of **15.60**,
`OUTFIT_TR3_NEVADA` **65.9 %** by **15.56**. The two are the same hand.

What the measurement *does* show is one real difference, and it is not in any
table: **`OUTFIT_TR3_ANTARC` carries its own gloves.** Vertices on the two hand
nodes, `ModelProbe -nodes`:

| outfit | node 10 | node 13 |
| --- | ---: | ---: |
| OUTFIT_CLASSIC | 126 | 140 |
| OUTFIT_VCI | 200 | 200 |
| OUTFIT_TR3_NEVADA | 58 | 64 |
| OUTFIT_TR3_COAST | 92 | 87 |
| OUTFIT_TR3_CATSUIT | 60 | 57 |
| **OUTFIT_TR3_ANTARC** | **462** | **487** |

Every other outfit has a cuff there and the separate `HAND_` model is the hand
one sees. The parka has a mitten - eight times the geometry - and *it* is what
one sees, drawn as part of the body and with the body's cloth material. Nothing
was changed for it: which of the two surfaces should shine, and how much, is a
picture the user has to judge, and this file's own rule since fix round F is
that a wardrobe table gets a new line only after somebody has looked at the
picture.

`ModelProbe` gained `-neckcut`, `-showcap` and a third `-neck` table (the
neck as a CYLINDER: an (s, r) grid of the torso's own vertices per outfit,
the ring at four candidate depths, and the (S, R) scan above).

#### Fix round K - the collar, the plate, and the wrist (#123)

The play test of fix round J, with photographs. Two findings, and the first of
them undoes the answer the two rounds before it gave.

**K1(a) - the collar ring is still there.** *"The cylinder leaves the catsuit's
collar ring standing; looking down one sees it as a dark ring with a patch of
skin in it."* It does, and the cause is one unit. Measured, `ModelProbe -neck`,
the cylinder table: the collar of `OUTFIT_CLASSIC` and `OUTFIT_VCI` reaches
**r = 31.2 at s = 25**, and the cut reached 30 - so the bottom edge of the
collar survived, as a sliver at the very mouth of the hole.

The scan gained a **collar** column for it: how many torso vertices the cut
leaves standing *above the shoulder line*, which is the ring in the picture.

| outfit | R = 30 | R = 34 | R = 38 |
| --- | ---: | ---: | ---: |
| OUTFIT_CLASSIC | 43 | **14** | 11 |
| OUTFIT_VCI | 35 | **14** | 11 |
| OUTFIT_RUSSIA | 129 | **94** | 66 |
| OUTFIT_TR3_ANTARC | 199 | **110** | 41 |
| OUTFIT_TR3_NEVADA | 4 | **3** | 3 |
| OUTFIT_TR3_COAST | 9 | **6** | 5 |
| OUTFIT_TR3_CATSUIT | 2 | **2** | 2 |
| OUTFIT_SUBSUIT (a helmet) | 148 | 135 | 106 |

The depth does not touch that column - a collar lives above the shoulders - but
it is what pushes the nearest surviving vertex away from the eye: over CLASSIC
and VCI, from 49.5 units to **60.4** at the eye 48 forward and from 50.1 to
56.3 at the eye on the joint, going from S = 30 to S = 40.

**The cut is 40 x 34 now, and wider was refused by picture.** Every outfit was
rendered at six cameras - the eye 48 units forward and on the joint, at 60, 75
and 90 degrees down - and compared by background share and mean grey level. At
40 x 34 the picture from the eye 48 forward, which is what the game gives
whenever the line of sight is clear, is **identical to the shipped one to a
tenth of a grey level in every outfit and at every pitch**. At R = 38 the
Antarctica parka already loses 0.9 of a level at 75 degrees and 2.6 at 90; at
60 x 55 her chest is visibly scooped out and one looks at the inside of her own
back, which is fix round J's finding (a) coming back. And at S = 50, R = 38 the
cylinder reaches an **arm** vertex on the diving suit (36.1 units) for the
first time in any scan - which the node mask stops anyway, and which is a
warning all the same. A collar that reaches 31 and shoulders that reach 67 at
the same depth cannot both be separated by one cylinder about the neck; what
makes the remainder harmless is K1(b).

**K1(b) - the cap is withdrawn, and her own skin closes the hole.** *"The cap
lies as a great flat skin-coloured plate over chest and collar, visible from
outside - a huge plate with the backpack straps on it - and inside it is still
black."* Both halves are reproduced exactly:

- the plate is the dome seen **with the eye on the head joint**, which is where
  the camera sits whenever a wall is in front of Lara. Rounds I and J only ever
  photographed it from the eye 48 units forward, where it is invisible.
- the straps are the page. The cap wore the majority page of the outfit's own
  collar ring, and for `OUTFIT_RUSSIA` that page is **8018, the backpack**
  (`OUTFIT_TR3_ANTARC` 8320, `OUTFIT_TR3_CATSUIT` 8310, and so on - eight
  outfits, eight lotteries).

So the built cap is gone - the part, its maker, the `HDModelSlot::synth`
machinery it was the only user of, and the tool's `-nocap` and `-showcap`. In
its place **the body is drawn a second time in first person**, keeping exactly
the faces the first draw culls, with the same cut, the same node lights, the
same light list, the same matrices and the same node mask, after it so that the
depth test hides everything a closed limb already covers. What a ray through
the hole meets is then the far wall of her own chest. It is a new cull mode
rather than a new parameter - `HD_MODEL_SKIN_CW` / `_CCW` in `hd_model.h`,
which the existing `cull` argument already carries all the way from
`hd_laradraw.cpp` - and the fragments wear one texel and a turned normal
(`gInnerSkin`, register c164 of `hd_model.hlsl`).

*Which texel*, and the rule took three tries:

| rule | answer | why it is wrong |
| --- | --- | --- |
| the topmost ring of the stump | 8002 | that is Lara's **hair**: the nape hairline sits on the torso node from s = -1.6 to 5.6 in every outfit |
| the majority page of the stump | 8042 / 8320 | a **coat** has more of the neck than a neck has - 276 vertices against 51 on the Russia body, 506 against 75 on the parka |
| the page whose column runs the whole height of the neck | 8001 / 8201 / 8047 | correct: a throat runs from the head joint to the shoulders because it *is* the neck; a collar, a zip and a hairline are bands across it |

and the coordinate on that page took a fourth: a real **vertex** nearest the
middle of the others - fix round J's own repair - sits on the *edge* of its
texture island, where the paint stops, and gave the four Tomb Raider III guests
`8201 at 117/169`, which reads **(63, 38, 21)**, a dark brown. The **centroid
of the biggest triangle** whose three corners are all on that page is inside
the paint by construction. What ships reads (201, 153, 115) for the three game
5 bodies, (193, 143, 104) for the Russia coat, (199, 146, 108) for the four
Tomb Raider III guests, and near black for the diving suit, which has no throat
on it at all.

*Proof:* `ModelProbe -firstperson` over `OUTFIT_CLASSIC`, `OUTFIT_VCI`,
`OUTFIT_RUSSIA`, `OUTFIT_TR3_ANTARC` and `OUTFIT_TR3_NEVADA` at both eyes and
three pitches - thirty pictures. The background share, which is the see-through
hole measured as a number, does not move by a tenth of a per cent under any
candidate cut; the same view without the inner skin (`-noinner`) is 43 % hole.

**K2 - the hands and the forearm are lit alike.** *"The hands react differently
to the room's intensity - in five rooms the hand is now brighter, now darker
than the forearm. A difference in the texture would be the same everywhere, so
it is light."*

Everything at that seam **is** identical except one term, and this is read off
the code rather than measured: `hd_laradraw.cpp` pushes the body and the hand
parts with the same `nodeLight` array, the same light list, the same sun factor
and the same matrices, and `HDObjectPush` copies all four per draw. What
differs is the **sheen**: fix round D gives the whole body the outfit's
material and fix round I gives the hand the skin's, and where a room's own
light list is empty - an ordinary room lit by its baked vertex colours, which
is most of the game - the shader's highlight is `light * strength *
pow(N.V, power) / 255`. The difference between two materials is therefore
**multiplied by the room's ambient**, which is precisely the reading the play
test made of it.

Measured as the grey levels the highlight adds at N.V = 1 in a room of 255
(`ModelProbe -handlight`, a new table: the body's pages on nodes 8..13 with the
hand's beside them):

| outfit | the arm wears | the hand wears | seam, before | after |
| --- | --- | --- | ---: | ---: |
| OUTFIT_CLASSIC | 8001, 25.5 | 8001, 30.6 | 5.1 | **0.0** |
| OUTFIT_TR3_NEVADA | 8202, 20.4 | 8001, 30.6 | 10.2 | **0.0** |
| OUTFIT_TR3_COAST | 8202, 20.4 | 8001, 30.6 | 10.2 | **0.0** |
| OUTFIT_TR3_CATSUIT | 8202, 114.8 | 8001, 30.6 | 84.1 | **0.0** |
| OUTFIT_TR3_ANTARC | 8320 parka, 25.5 | 8001, 30.6 | 5.1 | 5.1 |
| OUTFIT_VCI | 8027 latex, 114.8 | 8001, 30.6 | 84.1 | 84.1 |
| OUTFIT_RUSSIA | 8042 coat, 30.6 | 8001 glove, 45.9 | 15.3 | 15.3 |

The Rome case is the plainest of the seven: **the forearm and the hand are the
same texture page**, 8001, shaded by two different materials.

The fix is one line of the outfit table - which of the body's pages are Lara's
own **skin** - and the per-page material machinery fix round I already built.
Geometry cannot tell skin from latex, so it is a table, exactly as `wearsRig`
and the sheen itself are; two measurements decide each row, the page the arm
geometry wears and the page the throat column wears. 8001 is her skin on the
three game 5 bodies; the four Tomb Raider III guests carry theirs on 8201 (the
neck) and 8202 (the arms), the same two pages in all four because it is the
same Lara underneath; the diving suit has none, and naming its throat page
would make the whole suit matt.

The three rows that keep a difference keep it **on purpose**: a latex sleeve
meeting a bare hand, a parka mitten meeting one and a coat sleeve meeting a
leather glove are material boundaries in the art, not faults.

*Left over and named:* the four Tomb Raider III guests wear game 5 hands
(`HAND_BARE_*`, page 8001) on a Tomb Raider III body whose arms are page 8202 -
two different paintings of Lara's skin meeting at the wrist. That is a question
about **colour** and not about light, and no measurement this round could
separate it cleanly from the shape of the two models. It is a picture for the
user.

`ModelProbe` gained `-noinner` (the first person picture without the inner
skin, which is the hole), `-handlight` (the table above, with the seam as one
number), `-oldskin` (her skin back under the outfit's material - the BEFORE
picture), `-look wrist` and `-meshbits` on the composite; `-neck` gained the
collar column and a fourth table, the neck stump **by page**, which is where
the inner skin's texel is chosen from. `-nocap` and `-showcap` are gone with
the cap.

---

#### Fix round L - the neck was never cut at all (#123)

The play test of fix round K, with four first person photographs. Two findings,
and the first of them is the one rounds I, J and K each answered with the wrong
tool.

**L1 - "the collar is still there and it clips."** It is, on every outfit, and
the cause is not the shape of the cut. It is the **node mask**.

The shader's node rule is a weight and not a test (`hd_model.hlsl`, `ModelVS`):
a vertex is pushed

```
o.cut = keep + (1 - cuttable) * 1000
```

units out of the cut, where `cuttable` is that vertex's own skinning weights
summed over the nodes `HD_LARA_NECK_CUT_NODES` names. Until this round that was
the **torso alone**. And the vertex under Lara's chin is, measured
(`ModelProbe -neck`, the candidate table names it):

```
bones 7 / 14 / 5      weights 140 / 114 / 1      s -1.6   r 11.9
```

55 % torso, 45 % **head**, and one part in 255 of a shin. It was therefore
pushed 451 units out of a cut whose deepest point is 40 - and so was the whole
head-blended half of her neck. **No cylinder, no cone and no radius any round
has tried could ever have taken it.** Rounds I, J and K each measured the
collar's radius, each found a number that should have been enough, and each
came back from the play test with the ring still standing; the ring was never
inside the reach of the cut in the first place.

*And "the head node is masked in first person" does not hide it.* First person
clears node 14, but the mask works on **ranges**, and a range's node is the
heaviest node over its triangles (`hd_model.cpp`, `draw_model`). The collar's
triangles are heaviest on the **torso** and carry their head-skinned corners
with them, so they are drawn. That is also why round K's table could not see
the problem: it counted a vertex under the node that carries most of it and
threw every head-majority vertex away before counting.

**The mask is the torso AND the head now**, and that is the whole engine change
of this finding:

```c
#define HD_LARA_NECK_CUT_NODES  ((1u << HD_LARA_TORSO) | (1u << HD_LARA_HEAD_NODE))
```

It costs nothing - the head's own ranges are not drawn in first person anyway -
and it can no more reach an arm than the torso-only mask could.

Measured, `ModelProbe -neck`, "what a candidate cut leaves", which models the
shader's arithmetic exactly (the weighted push, and a vertex counted as drawn
when a triangle whose range node is not the head uses it). `near` is the
nearest surviving vertex from the eye **on the head joint** - the wall case,
and the one the user photographed - and `in view` how many survivors stand
within sixty units of it:

| outfit | near, round K | in view, K | near, round L | in view, L |
| --- | ---: | ---: | ---: | ---: |
| OUTFIT_CLASSIC | 18.6 | 202 | **45.1** | **78** |
| OUTFIT_RUSSIA | 18.6 | 462 | **43.7** | **119** |
| OUTFIT_VCI | 18.6 | 164 | **45.1** | **57** |
| OUTFIT_TR3_ANTARC | 18.6 | 972 | **43.5** | **222** |
| OUTFIT_TR3_NEVADA | 18.6 | 133 | **45.2** | **33** |
| OUTFIT_TR3_COAST | 18.6 | 193 | **45.2** | **56** |
| OUTFIT_TR3_CATSUIT | 18.6 | 293 | **45.2** | **81** |
| OUTFIT_SUBSUIT (a helmet, out of scope) | 18.0 | 171 | 36.4 | 119 |

and the `chest` column of that table - drawn vertices below the shoulder line
that the cut takes - is **zero in every row of it**, before and after.

**A cone was built, measured and refused.** Rounds I to K reported, honestly, a
limit: a collar that reaches 31 units off the neck axis and shoulders that
reach 67 at the same depth cannot both be cut by one cylinder. Fix round L
therefore built the cone that would do it - wide at the head joint, tapering to
34 where the shoulders arrive, one lerp in the vertex shader - and only then
found the node mask. With the head node in the mask a 44-unit cone buys **three
vertices** on `OUTFIT_CLASSIC` and a 56-unit one **five**, and the pictures are
the same to the eye at every pitch and both eyes. It is not shipped. The rows
stay in the tool's own table so that the next round does not build it again.

**L2 - "the inner skin sticks to the camera as huge surfaces."** The second
half of the same picture: with the eye on the head joint the camera stands
inside her chest, and what filled it was the collar of L1 at 18 units. With the
collar gone the nearest thing to the eye is 43.5 units - her own chest, at a
distance, which is what a camera inside a body is supposed to show, and which
the user's own wording allows ("a look into the body is skin in the distance").

What is added beside it is a **near zone about the eye**: a sphere in which
nothing of her body is drawn, on the same nodes the cut may touch.

```c
#define HD_LARA_NECK_NEAR   40.0f   // about the camera, in view space
```

It is the one part of the cut that lives in **view space**, and for the
opposite reason to the cylinder: what it exists to stop is a surface standing
on the *lens*, and how far a thing is from the lens is a fact about the camera
and not about her skin. In first person the camera *is* the eye, so it needs no
position of its own and follows the game's own choice between `FP_EYE_OFFSET`
48 units forward and the eye on the joint when `mgLOS` forbids the offset -
with nothing having to be told which happened. It rides in `gNeckNear.x`
(register c165) beside the cylinder, one `min` in `ModelVS`.

Forty is measured and not chosen. The renderer's near plane is at 20
(`InitWindow`, `gamemain.cpp`) and the nearest vertex that survives the
cylinder at either eye, over every outfit, is 43.5. So the near zone **changes
not one pixel** of any of the thirty pictures this round took - and that is the
point of it. Those pictures are taken in the bind pose, and the bind pose
cannot show what an animation does when Lara folds; this is the floor that says
no surface of hers may stand between the near plane and forty units, whatever
the animation. Sixty was measured too, and at sixty it starts taking geometry
the eye 48 forward is supposed to show.

**The proof, by picture.** Five outfits - Rome, VCI, Russia, the Antarctica
parka and the Nevada guest - at three pitches (60, 75 and 90 degrees down) and
both eyes the game uses, thirty pictures, each one against the picture the same
camera gave before this round:

| | eye 48 forward | eye on the joint |
| --- | --- | --- |
| pitch 60 | **0.00 % of pixels differ**, every outfit | 21.8 to 52.4 % - the collar going |
| pitch 75 | **0.00 %**, every outfit | 32.3 to 68.5 % |
| pitch 90 | 4.1 to 8.6 % - the stump, seen only straight down | 42.2 to 77.1 % |

and the share of the frame showing the **room behind her** rises by at most 1.7
points anywhere (worst: the VCI catsuit at 75 degrees, 21.1 % to 22.8 %), so
nothing was opened that one can see through. The **third person** picture is
**byte-identical** for all five outfits, built from the round K sources and the
round L sources in turn - the cut is off there and the shader's new `min` is a
no-op with the register at minus a million.

`ModelProbe` gained `-necknear <units>` (the near zone) and `-cutnodes <mask>`
(which nodes the cut may touch - `-cutnodes 0x80` is round K's picture and the
BEFORE of this round, and pointing it at one node at a time is how a surface
standing in the camera is traced to the node it hangs off). `-neck` gained two
tables: the neck in **two-unit bins** with a column counting how many survivors
carry head weight, and the **candidate** table above, which is the first one in
this work to model the shader's weighted push rather than a majority vote.

*Left over and named:* with the eye on the head joint and the view straight
down, a sliver of the room is still visible at the extreme corners under the
arms - about one percent of the frame in the worst outfit, the same order as
before this round. And the **diving suit** is out of this work by the user's
decision: it has no throat, its helmet sits on the torso node, and it neither
got a fix of its own nor was allowed to constrain the cut for the other seven
bodies.

#### Group M - non-euclidean rooms: the portal window as a scissor (#123)

The user, in trview and in the game, in ANDREA1 *The base*: **rooms that stand
inside one another in the world are both drawn in HD**. Room 39 (a storeroom
with a shelf and a floor grating) and room 53 (an air shaft) occupy the same
world coordinates, and through the door of one you can see the geometry of the
other coming through the wall. The depth buffer has nothing to say about it -
both rooms really are at that distance.

**What the classic path does that the HD path did not.** Every frame, TR5
walks its portals from the camera's room outwards (`game\draw.cpp`,
`GetRoomBounds` and `SetRoomBounds`) and leaves each reached room a rectangle
in `ROOM_INFO::left / top / right / bottom` - the projected union of the
portals it was reached through, widened by a pixel on each side, in
**backbuffer pixels** with both ends inclusive. `InsertRoom`
(`specific\drawroom.cpp`) copies those four into `clip_left..clip_bottom`, and
the classic roomlets are held against them.

Both facts about that window were **measured and not assumed**, and they are
what the fix rests on:

- **The units are backbuffer pixels, one to one.** `InitWindow`
  (`specific\3dmath.cpp:731`) is called with `App.dx.dwRenderWidth /
  dwRenderHeight` (`specific\gamemain.cpp:261`, `winmain.cpp:274`), and those
  two are the present parameters' `BackBufferWidth / Height`
  (`specific\dxshell.cpp:451`). There is no letterbox and no scale. The engine
  never calls `SetViewport` anywhere except in the HD sky, which restores it
  before it returns, so the viewport is the whole render target.
- **The classic clip is COARSER than a scissor.** `clip_left..clip_bottom`
  only build per-vertex flags; a polygon is *dropped* when every corner lies
  beyond the same edge (`specific\polyinsert.cpp:1213`, `c[v0] & c[v1] & …`)
  and is otherwise clipped against the **screen**, not the window
  (`specific\clipping.cpp`, `XYUVGClipper` uses `f_left..f_bottom`). So a
  polygon straddling the window's rim is drawn whole by the classic path. That
  is why the scissor rectangle carries **one pixel of margin** on every side:
  without it the HD path would be strictly stricter than the engine it copies,
  and the two would disagree along every portal rim.

**The change.** `HDFrameRoom` takes the four numbers at the seam it already
had - `InsertRoom`, one line, beside the matrix - and keeps them in the frame
list as a half-open rectangle. Taken **there** and not at draw time, because
they do not survive the frame: `PrintObjects` resets all four the moment it is
done with a room's items, long before `S_OutputPolyList` draws anything HD.
Both room phases then set `D3DRS_SCISSORTESTENABLE` and `SetScissorRect` per
room - the solid passes in `HDFrameDraw`, the glass and the glows in
`HDFrameDrawGlow`, because a lamp cone hanging in a portal has to be cut by
that portal like the wall beside it. A window that covers the whole backbuffer
- the camera's own room, and any room the walk could see all of - is drawn with
the test **off**, which is both cheaper and exactly what such a rectangle would
do; a window of no width or height means the room is not drawn at all.

**Objects get the window of their own room**, out of `item->room_number`, and
this is the one place where the HD path is deliberately *not* the classic path:
`PrintObjects` hands every item the whole screen
(`phd_left = 0 … phd_winxmax + 1`). The argument for doing it anyway is the
same one the portal walk is built on - anything of a far room outside its
window stands behind the near room's own geometry - and the argument for it
being safe is that both the HD room walls and the classic buckets write depth,
so the pixels a scissor takes here are pixels something else covers. **Lara is
never scissored**: everything pushed through `HDObjectPush` answers -1, and she
is in the camera's room whose window is the screen. An item in a room the walk
never reached answers -1 as well and is not cut either - inventing a window for
it would be a rule the engine has not got.

**The state bracket grew by one.** `D3DRS_SCISSORTESTENABLE` has been in
`hd_render.cpp`'s saved list since stage S3-1; the **rectangle** beside it is a
device state of its own with its own `Get/SetScissorRect` pair, and group M is
the first thing in the HD path that writes it. It is read back in
`HDRenderStateSave` and written back in `HDRenderStateRestore` like everything
else. `RoomProbe`'s bracket check counts **59** items now instead of 58, and
reports **0 changed**.

**Without the cap** (`D3DPRASTERCAPS_SCISSORTEST` - every card since about
2002 has it) `HDRenderScissor` and `HDRenderScissorOff` do nothing at all, and
the HD rooms draw whole: the old bug, not a black screen. The first `HDLOG`
line of a level says which of the two it is.

**The proof.** `be.bat` builds with 0 errors. `RoomProbe` cannot walk portals -
it has no engine around it - so the claim it *can* prove is the one the engine
depends on, that the renderer really draws nothing outside a rectangle handed
to it. New switch `-scissor <l,t,r,b>`: it calls `HDRenderScissor` with exactly
the numbers the engine's room path calls it with, draws the room, reads the
picture back and counts drawn pixels inside and outside the rectangle. ANDREA1
room 39, 800x600, the tool's own automatic camera:

| run | background left in the picture | drawn inside the rectangle | drawn outside it |
| --- | ---: | ---: | ---: |
| no `-scissor` at all | 71.2 % | — | — |
| `-scissor 0,0,800,600` | 71.2 % | 138 197 | **0** |
| `-scissor 200,150,600,450` | 91.3 % | 41 689 | **0** |

The middle row is the control that matters as much as the last: a rectangle
covering the frame leaves exactly the picture no rectangle at all leaves, so
the switch neither adds nor takes away anything by itself. The last row draws a
third of what the first one did, and every pixel of it is inside the window.

The state bracket reported **0 of 59 changed** in all three runs, and with
`hd_enable` off nothing in this group is reached at all: `HDFrameRoom` returns
at `GroupOfRoom` before it touches the window, and `HDFrameDraw` returns on an
empty list before the first scissor call.

**`HDLOG`, behind `-log`.** Once per level, at the first frame that draws an HD
room, one line saying whether the device has the cap and what the backbuffer
is, then one line per room with the window it got:

```
HDLOG level=ANDREA1 clip first frame - scissor test in force, backbuffer 1920x1080
HDLOG   room=39 group=39 clip=0,0,1920,1080 (the whole screen)
HDLOG   room=53 group=53 clip=812,391,1096,655
```

and after that, once a game second, the tally:

```
HDLOG clip rooms=11 full=1 scissored=10 empty=0
```

*Left over and named:* an item's window is its room's, and `item->room_number`
can lag by a frame while it crosses a portal. If a cut limb is ever seen, it
will be there, and the object half of this group is one line
(`entry.room = -1`) away from being taken back out on its own.

---

### 5. Sixty frames a second (#148)

The plan this follows is `Preparation/60FPS-Plan.md`, written from a read-only
survey of this tree. It runs in orders; **this section covers orders 1 and 1b**
- the clock split, the option, the measurement, one warm-up fix, and then what
a play test of all that found. There is no interpolation yet.

#### 5.1 What the 30 Hz cap actually was

One comparison. `Sync()` (`specific/time.cpp`) counts whole sixtieths of a
second - `frequency = pfq / 60`, and there is no thirty anywhere in that file.
`S_DumpScreen()` (`specific/output.cpp`) waited for **two** of those ticks
before it presented, and that busy wait was the entire cap.

Everything downstream was already built for a 60 Hz tick. `ControlPhase()`
(`game/control.cpp`) runs

```
for (framecount += nframes; framecount > 0; framecount -= 2)
```

where `nframes` is what `S_DumpScreen` returned, i.e. how many sixtieths the
frame really took, and a logic step costs **two** of them. Feeding it one per
frame therefore runs the game on every second call, entirely by itself:
sixty presents, thirty logic steps, and not one line of the simulation
touched.

#### 5.2 The change

`gPresentMinTicks` (`specific/output.cpp`, declared in `output.h`) replaces
the constant `2` in `S_DumpScreen`, and `DrawPhaseGame()` (`game/draw.cpp`)
lowers it to `1` around **its own** present when `tomb5.fps_mode` is 1, then
puts it straight back:

```
gPresentMinTicks = GameFrameMinTicks();		// was: tomb5.fps_mode == 1 ? 1 : 2
camera.number_frames = S_DumpScreen();
S_AnimateTextures(camera.number_frames);
ProfileFrameEnd();				// judged against the slot above
gPresentMinTicks = 2;
```

`GameFrameMinTicks()` (`specific/output.cpp`) is order 1b's correction and is
described in 5.8; order 1 had `tomb5.fps_mode == 1 ? 1 : 2` inline here, which
gave the title menu sixty as well and broke it.

The default staying at two is what keeps every modal loop at
thirty without touching any of them - the inventory (`game/newinv2.cpp`), the
load/save screen (`specific/LoadSave.cpp`), the death menu
(`game/control.cpp`) and the loading fade (`specific/output.cpp`) each step
their own animation counters once per present, and those counters were written
for a 30 Hz present.

`tomb5.fps_mode` is a `ulong` in `tomb5_options` (`global/types.h`), read and
written under the registry name `fps_mode`, `1` (60) by default; `0` is 30 and
`2` is reserved for the display's own refresh and reads back as `1`. The menu
row is *Frame rate* on page 1 of the tomb5 options
(`tomb5/troyestuff.cpp`), which grew from thirteen rows to fourteen - the
ceiling for that screen, as the comment over `PAGE0_NUM` now records.

`TargetPresentHz()` is the same decision as a number (30 or 60) for the two
places that need a rate rather than a tick count.

#### 5.3 What had to follow the rate

Only one thing in this order - risk **R15** of the plan. The resolution
caption after Alt+Plus counted down by `long(30 / App.fps)` once per drawn
frame, which truncates to **zero** above 30 fps: the caption would have stayed
on screen for ever. It now steps by `long(TargetPresentHz() / App.fps)` and
starts from `4 * TargetPresentHz()` instead of a bare `120`
(`specific/winmain.cpp`), so it is the same four seconds in both modes and
byte-for-byte the old line at `fps_mode 0`.

The other fourteen risks the plan lists - the screen fades, the rain and snow,
the corpse fade, the lightning, the pickup slide-in - **still count per drawn
frame** and therefore still run at double speed at `fps_mode 1`. They are
order 3 and are named here so that a play test knows what it is looking at.

#### 5.4 Proving the logic did not change

`FPSLOG` (`specific/hd/hd_level.cpp`) gained two fields: `fps=` is the target
the budget was taken from, and `logic_ticks=` is `GlobalCounter`'s difference
over that second - the number of logic steps the game really ran.
`GlobalCounter` is stepped once per tick in `ControlPhase` and nowhere else,
so a healthy 60 Hz second must read `frames=60 logic_ticks=30`. The two budget
constants became `FrameBudgetMs()` / `MissedSlotMs()` so that a 60 Hz second
is not measured against a 30 Hz slot.

`Preparation/Tools/Diagnose/SaveDiff.cpp` (`bsd.bat`) is the second proof and
the stronger one: two savegames, field by field, by name. The draw path has an
RNG of its own (`rand_2`, `GetRandomDraw`) that the simulation never reads, so
extra drawn frames cannot reach the game state - and a savegame is that state
written down. The tool takes every offset from the engine's own
`global/types.h` with `offsetof`, which is why it is built for Win32: the
structs are `#pragma pack(1)` but carry pointers. The recipe is in
`docs/Engine-Manual.md` under *Frame rate*.

#### 5.5 `PROFLOG` - where a frame's milliseconds go

New files `specific/profile.cpp` and `specific/profile.h`. `FPSLOG` says how
long a frame took; it cannot say why, and the two things the user measured -
the title screen falling to 23 frames a second, and about three seconds of
warm-up at the start of every level - are exactly the questions it cannot
answer.

Seventeen phases per frame, taken with `QueryPerformanceCounter`: `logic`
(`ControlPhase`, at the call site in `game/gameflow.cpp` because
`ControlPhase` has early returns), `gather`, `rooms` and `sky`, the HD path's
three shares of the gather (`hdroom` in `InsertRoom`, `hdobj` in
`HDObjectBegin` / `Node` / `End`, `hdlara` in `HDLaraDrawFrame`), `output`
with `hddraw`, `hdlight`, `hdobjdraw` and `hdglow` inside it, `buckets`,
`sort` and `sortdraw` as whole-frame totals, and the present split into `spin`
- the frame finished and waiting for its slot - and `flip`, `Present` itself.
(`sort` and `sortdraw` were one phase in order 1; 5.8 says why they are two.)

`buckets`, `sort` and `sortdraw` are whole-frame on purpose: all are called
from three or more places, and the callee is the only place that catches all
of them. That
is also why `SkyDrawPhase` got a phase of its own - it is the one place in the
frame that draws buckets and a sort list OUTSIDE `S_OutputPolyList`, so the
printed remainder is `outrest = output - hddraw - hdglow`, the two phases that
run inside the output and nowhere else, rather than a subtraction that would
read zero in every outdoor level.

One line per second with mean and worst per phase; one line for **every**
frame that missed its slot, with all its phases and the second of the level it
happened in; and one `first_frame` line per level, always written, which is
the line the warm-up question is about.

**The guard is one bool.** Every seam is `ProfBegin(...)` / `ProfEnd(...)`,
both of which are `if (gLogEnabled)` around a call. Without `-log` not one
clock is read, no accumulator moves and no string is built - the same rule
`HDFramePresented` follows, and the reason these seams stay in for good.

#### 5.6 The warm-up, and what is provable about it

`WarmUpLevel()` (`specific/file.cpp`), one line in the log:
`WARMUP pages=112 hd_pages=143 hd_buffers=94 ms=38.2`.

Nearly every texture in this engine is `D3DPOOL_MANAGED` - the classic pages
(`specific/texture.cpp`, `CreateTexturePage`), the HD pages when the budget
lets them be, and every HD model's vertex and index buffer
(`specific/hd/hd_model.cpp`, which says so in its own comment). A managed
resource lives in system memory until something **draws** with it; Direct3D
uploads it at that first bind. So the first time a wall, a texture or an enemy
comes into view, the frame that shows it pays for the upload - and a level
shows most of its art in its first seconds. That is a property of the code,
not a hypothesis.

`IDirect3DResource9::PreLoad` is Direct3D's own answer to exactly this. The
new pass calls it for every classic page, every HD page slot
(`HDPageCacheSlotTexture`) and every HD model buffer (`HDModelPreload`, new in
`hd_model.cpp/.h`), after the load thread has joined and the HD build is done,
with the loading bar still on screen. It draws nothing, touches no render
state and cannot change a pixel; for a resource that is already resident it
costs nothing, and for one in `D3DPOOL_DEFAULT` it is documented to be
ignored.

What it cannot fix is named rather than guessed at: the water mark tables, a
room's first light list, the driver's first use of a shader or a vertex
declaration, and Windows paging the process in. Those are for `PROFLOG`'s
`first_frame` and low-`sec=` `late` lines to point at from a real session.

#### 5.7 What `fps_mode 0` does

Nothing. Every branch this order added is inert at 30: `gPresentMinTicks` is 2
and `S_DumpScreen` reads `while (n < 2)`; `TargetPresentHz()` is 30, so the
caption's step is `long(30 / App.fps)` and its start is 120; `FrameBudgetMs()`
is `2 * 1000/60` and `MissedSlotMs()` that plus 1000/120, the two old
constants to the bit; and every profile seam is behind `gLogEnabled`. The only
thing that runs regardless is the warm-up, which by construction cannot change
what is drawn.

#### 5.8 Order 1b - what the play test of order 1 found

Five findings from one session at `fps_mode 1` with `-log`. The first is a
bug in the game, the other four are bugs in the measurement - which matters
just as much, because the whole of order 2 will be judged by these logs.

**The title menu skipped rows.** `DoTitle`'s loop (`game/gameflow.cpp`) is
`TitleOptions(); DrawPhaseGame(); ControlPhase(nframes);` - so the menu is
walked once per **present**, while the debounced input it walks with,
`dbinput`, is written once per **tick**, inside `ControlPhase`'s
`for (framecount += nframes; ...; framecount -= 2)` loop and nowhere else
(`specific/input.cpp`, `if (debounce) dbinput = inputBusy & (dbinput ^
inputBusy)`). At two ticks per present those two rates are the same and every
edge is read exactly once. At one, every second `ControlPhase` runs no tick at
all, `dbinput` keeps standing, and the next `TitleOptions` reads the same
press again: one tap of Down moved the cursor two rows, and *New Game* could
not be selected. Everything `TitleOptions` reaches has the same shape - the
level select, the whole options tree, the Tomb5 pages in
`tomb5/troyestuff.cpp` - because all of them read `dbinput` per call.

The fix is `GameFrameMinTicks()` (`specific/output.cpp`): sixty is for a
played frame, so it returns 2 whenever `gfLevelId == LVL5_TITLE` or
`InventoryActive`, and 1 only otherwise. Nothing else in the build needed a
guard, and that is a property of the code rather than a hope: `DoLevel` reads
no input at all outside `ControlPhase`, and every other menu - the inventory
(`game/newinv2.cpp`), the pause menu and the load/save screen
(`specific/LoadSave.cpp`), the death menu (`game/control.cpp`), the special
features - runs a modal loop of its own that calls `S_UpdateInput` once per
present and is therefore consistent at any rate. They also never reach
`DrawPhaseGame`, which is why they already kept their thirty.

**The late counter blamed the menus.** `FrameBudgetMs()`
(`specific/hd/hd_level.cpp`) and its twin in `profile.cpp` took the slot from
`tomb5.fps_mode`. But `fps_mode` is not what a given frame waited for: only
`DrawPhaseGame`'s own present ever drops to one tick, and after the fix above
not even that one in the title. Every menu second was therefore measured
against 16.7 ms and reported as thirty slow and up to thirty late frames -
`logic_ticks=0 hd_rooms=0 frames=30 late=30`, which is a *healthy* inventory.
Both now read `gPresentMinTicks` at the moment the frame is judged, and print
it as `budget=` so the line says what it was measured against. That is also
why `DrawPhaseGame` restores the two **after** `ProfileFrameEnd` rather than
before it.

**An alt-tab was a late frame.** A frame whose interval was 5516 ms while its
own work was 32 ms is not a slow frame; the main loop was somewhere else.
`PROFLOG` now counts those as `pauses` and never as `late`, and writes one
short line - `pause at=41s frame=5516.30 work=8.90 presents=1204` - instead of
a late line with seventeen meaningless phases. It recognises them by counting
presents: `ProfPresent()` in both present functions (`specific/output.cpp`)
means the profile knows how many pictures went out during an interval, and
anything above its own one is a modal loop drawing behind its back. The second
half of the rule, an interval that exceeds its own work by more than a second,
catches what presents nothing at all - an alt-tab, a window drag, a
breakpoint. A paused interval is also kept out of the second's `frame=`
mean and maximum, where a single five-second one would otherwise own both.

**The profiler's `logic` grew without end.** It read 3.4, 7.2, 11.0, 14.9 and
18.9 ms over five consecutive seconds of the title, and 28.5 ms after seven
seconds of play - larger than the whole frame it was supposedly part of. The
cause is one missing line. `PROF_LOGIC` is deliberately not cleared by
`ProfileFrameBegin`, because `ControlPhase` runs between the present and that
call and its milliseconds are already in the accumulator when it runs. It was
cleared **nowhere else either**, so it was a running total for the length of a
level. `ProfileFrameEnd` now clears it once it has reported it and once the
second's totals have taken it.

The definition it implements is written down in `profile.h` and in the manual,
because the number is easy to misread: the loop is draw, present, logic, so
the game step that follows a frame is counted into the *next* frame's line,
and `logic` is **not** part of `work` (which is `ProfileFrameBegin` to
`ProfileFrameEnd`). The invariant to check a log against is
`work + logic <= frame`.

**22 ms in the sort phase, in the title's first frame and never again.** Not
answered, but made answerable and made cheaper. `sort` was `SortPolyList` and
`DrawSortList` added together, and those two have completely different
explanations: the first is a quicksort, the second is vertex copying and
`DrawPrimitiveUP`. They are now two phases, `sort` and `sortdraw`, and the
line carries `sortn=`, how many entries the list held - so a slow sort can be
told from a merely long one without a second session.

And the one part of it that *is* provable from the source is fixed:
`SortBuffer` (2.5 MB), `SortList` (256 KB) and `Bucket` (30 x 2080 vertices,
2.4 MB) are plain statics, so they live in `.bss` and Windows hands them out
as demand-zero pages - roughly 1300 first-write faults, every one of them in
the first frame that draws anything. `PreTouchDrawBuffers()`
(`specific/polyinsert.cpp`) writes through all three from `WarmUpLevel()`,
where the loading bar is on screen. It cannot change a pixel: `InitBuckets`
and `InitialiseSortList` reset all three at the top of every frame, and
nothing reads a byte of any of them that was not written in the same frame
first.

**Two more per-drawn-frame counters found while reading, not fixed here.**
`gfLegendTime--` in `DoLevel` (`game/gameflow.cpp`) - the level-name caption
shows for half as long at sixty - and `cutseq_busy_timeout--` in
`handle_cutseq_triggering` (`game/deltapak.cpp`), the wind-up before a
cutscene. Both belong with the fourteen counters of 5.3 and are order 3; they
are recorded as R16 and R17 in the plan.

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
