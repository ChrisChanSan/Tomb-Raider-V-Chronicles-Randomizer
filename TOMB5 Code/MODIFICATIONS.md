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

Measured again on **2026-09-07**, after orders 2, 3, 4 and 6 of the same
section: **115** files changed, **65** entirely new - **180** in all, **166**
of them carrying the notice. The three additions are `game/interpolate.cpp`,
`game/interpolate.h` and `game/interpolate_math.h`, which came with orders 2
and 4 and were never counted here. The two newly *changed* files are
`specific/time.cpp` and `specific/time.h`: order 6 is the first thing in this
project to touch the engine's clock at all, and they were given their notice
with it.

Fourteen of the 180 do not carry it, and none of them should - the ten below
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
walls, material 16's sheets (called light shafts here; fix round N found them
to be still WATER surfaces), material 2's glow - is 0 %. A page with no
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
  bit are 0, 3 and 14, and none of them is a cut-out material. (Superseded
  in fix round H: the material is now read as its low byte everywhere, see
  the glass paragraph there.)

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
against 6363). (The table holds **eleven** hand parts, not twelve - counted and
corrected in #153's round M2b, second fix round, below. All eleven do have a
gloved twin, and since that round none of them is worn: the wardrobe wears the
bare hands in every level.) Only `OUTFIT_RUSSIA` is set — nobody has reported a
wrong hand on any other, and each is a picture the user has to judge. A hand part with no
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
net over JOBY4's sea bed is one 192 x 192 sheet over the whole floor (fix round
N: it is not a caustic net at all - it is the sea's own surface, three sheets
of it 16 units apart, seen from below). Painted
as texel x baked light with no weight of its own, that saturates. Measured with
`RoomProbe`, on the additive pass alone, over the pixels it covers:

| scene | what it is | before | after |
|---|---|---|---|
| JOBY4 room 19 | the caustic net (fix round N: the SEA of Deepsea Dive seen from under it) | 98.1 % mean, 86.9 % of it pure white | 6.9 %, nothing white |
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
| 16 | whole-surface sheets, the caustic net among them | `p0` and `p1` pair up with the page | `p2`, 0.035 to 0.2, the same shape as 15's - **superseded in fix round N**: material 16 is the remaster's STILL WATER SURFACE and `p2` is a rate, not a weight |

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
**Superseded in fix round N for material 16:** what the data says of WHAT is
the AREA of the body of water each part covers, over the whole game, and `p1`
is the one of the three numbers that does NOT follow it - it is read as the
amplitude of a ripple now. Material 15 is untouched.

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

> The play test of this round came back with a torn collar and missing
> straps. The cut itself is not the cause - see **fix round R** at the end
> of section 5 (the inner skin's depth tie, and the camera). And the cylinder
> is gone: **fix round S** replaced the whole shape with ONE PLANE in the
> head's own frame and a lid, because a shape in the BIND POSE moves across a
> neck that is skinned to two nodes at once.

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

> The ITEM half of this group was withdrawn after the play test of
> 2026-09-07 - the classic path hands every item the whole screen. The
> rooms keep their windows. See **fix round R** at the end of section 5.

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
survey of this tree. It runs in orders; **this section covers all six of them
and the 1b that came out of a play test** - the clock split, the option, the
measurement, one warm-up fix, what a play test of all that found, the
interpolation of the root and the camera that fills the in-between frames in,
then every counter in the engine that was measuring time in drawn frames, then
the pose, so that a body's limbs move on the in-between frame and not only its
feet's position, then VSync and the rate the monitor sets, and finally the
particles - the sparks, the weather and everything else small that moves.

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

The other sixteen risks the plan lists - the screen fades, the rain and snow,
the corpse fade, the lightning, the pickup slide-in - counted per drawn frame
through orders 1, 1b and 2 and therefore ran at double speed at `fps_mode 1`.
**Order 3 is the one that fixed them; it is 5.10.**

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
written down. **That last sentence was not true when it was written**: five
places in the DRAW path call `GetRandomControl`, which is the simulation's
generator. Order 3 (5.10) makes it true by rolling `rand_1` back over an
in-between frame; until then this proof would have failed and the reason would
have been the proof's own premise. The tool takes every offset from the engine's own
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

#### 5.9 Order 2 - the root and the camera

Order 1 left every second frame a duplicate of the one before it: sixty
presents of thirty distinct pictures. This order fills them in. New files
`game/interpolate.cpp` and `game/interpolate.h`, plus
`game/interpolate_math.h` for the arithmetic alone.

**Three call sites, and that is the whole seam.**

| | where | what |
|---|---|---|
| capture | `game/control.cpp`, the first statement inside the tick body, before `GlobalCounter++` | `InterpCapturePrev()` - the state the tick is about to leave behind |
| apply | `game/draw.cpp`, the first line of `DrawPhaseGame` after `ProfileFrameBegin` | `InterpBeginFrame(InterpAlphaNow())` |
| restore | `game/draw.cpp`, after `S_AnimateTextures`, before `ProfileFrameEnd` | `InterpEndFrame()` |

Nothing downstream knows. `DrawRooms`, `InsertRoom`, `phd_PutPolygons`, the
bucket sorter, the shadows, the lighting, `HDFrameRoom`, `HDObjectNode` and
`HDLaraDraw` read the same globals they always read and get the interpolated
ones. **Not one line of `specific/hd/` changed for this order** except a log
field: the HD seam takes its matrices off the engine's own stack
(`InsertRoom` records `aMXPtr` as it stands, `HDObjectNode` records the matrix
the classic mesh would have used), so interpolating *before* the stack makes
HD follow for free.

`InterpCapturePrev` is the first statement of the tick body on purpose. When
one `ControlPhase` runs several ticks - a hitch, the two priming ticks of a
level start - each overwrites the last, and what stands at draw time is the
state before the **final** tick, which is the only neighbour the drawn state
has.

**Alpha needs no clock.** After `ControlPhase` returns, `framecount` is `-1`
when a tick ran and `0` when none did, so

```c
alpha = framecount < 0 ? 0.5f : 1.0f;   // and 1.0f whenever fps_mode != 1
```

is exact, cannot drift and costs one comparison. The drawn states are then
`T1, (T1+T2)/2, T2, (T2+T3)/2, ...` - even half steps, forever. A property
worth writing down falls out of the same walk: the frame immediately before a
`ControlPhase` **that runs a tick** is always a live one, so no logic tick
ever reads what a tween frame left behind - which matters because the draw
phase does leave `lara_matrices` and `lara_joint_matrices` behind and the
first-person and look cameras read them back. Only a *late* frame (one that
took two ticks or more) can break that, and then the head bone one camera
reads is half a step stale for one frame. Nothing in the simulation or the
savegame hangs on those matrices, and `HairControl` - the one piece of logic
that could have - builds its own stack from `lara_item->pos` instead, which
`InterpEndFrame` has already put back.

**The snapshot.** `items[512]` (position, three rotations, room, object,
anim, frame, valid), `effects[24]`, `hairs[2][7]` and the camera - 15.0 KB.
Beside it stands what a frame overwrote, so that the restore is exact: 10.7 KB
plus 1.1 KB of touched-index lists. **27 KB of statics in all**, no
allocation, no pointer into the `game_malloc` pool - the plan's 1.2 estimated
13 KB and had counted only the previous state, not the copy of the live one
that has to go back. `room`, `object`, `anim` and `frame` are never
interpolated; they are there so that a slot handed to a *different* object can
be told apart (otherwise a dead enemy's slot reused by a spawn would be drawn
sliding from the grave to the spawn point), and so that order 4 has the frame
pair it will need for the pose. The braid is not optional: `HairControl`
derives every link from the head, so a head that moves half a step while the
braid does not is a plait hanging in the air behind Lara's neck.

Only things that **moved** are touched. A level holds 512 item slots and a
handful of them travel in any tick; blending a value into itself is work with
no picture behind it and would have to be undone as well. It is also what
makes `INTERPLOG`'s `items=` mean "things that really moved" rather than "how
many slots exist".

**Where it deliberately snaps**, with the reason for each threshold in
`interpolate_math.h`:

| trigger | test |
|---|---|
| no previous state | first frame of a level, after a load, after a menu |
| a different object in the slot | `object_number` changed |
| teleport | `abs(dx)+abs(dy)+abs(dz) > 512` in one tick - `TranslateItem`, a flipmap move, the randomizer's own spawn placement. Nothing in TR5 crosses half a sector under its own power in 33 ms; a terminal fall is about 130 units |
| room jump | the same test across a room boundary; a portal *walk* is normal and interpolates |
| camera cut | `camera.type` **or** `camera.old_type` changed, `fixed_camera` changed, `bUseSpotCam` changed, or eye or target jumped more than 1024 |
| cut scene | `GLOBAL_playing_cutseq` - the whole frame, see below |

`camera.type` is tested together with `camera.old_type` because
`CalculateCamera` resets `type` to `CHASE_CAMERA` at its own end unless a
heavy camera's timer is running; `old_type` is what the view was actually
built as and is the half of the pair that moves when a fixed camera takes
over.

`camera.pos.room_number` is **never** interpolated. `DrawRooms` is handed it
and it drives the whole portal walk and every clip window; a half step across
a portal would flip the room list. The live room with the interpolated eye is
a mismatch of under 33 ms of camera travel - the same compromise TR1X makes.

**The camera is rebuilt, not merely moved.** Moving `camera.pos` does nothing
on its own: the whole world-to-view matrix, `CamPos`, `CamRot`, `D3DMView` and
the device's own view transform are built by `phd_LookAt`, once per logic
tick, at the end of whichever camera routine ran. So the interpolator calls
`phd_LookAt` again with the moved eye - and **a second time in
`InterpEndFrame` with the live eye**, which is the half that is easy to forget:
at sixty, every second `ControlPhase` runs no tick at all, so nothing else
would rebuild it, and the tween frame's matrix would be handed to the next
frame while that frame draws everything at its live position. The world half a
step ahead of the camera looking at it, on every second frame, for the whole
game.

The roll comes with it. Three cameras hand `phd_LookAt` a roll that is not
zero - first person rolls with the head bone, a flyby rolls along its track, a
cut scene rolls with the scene - and rebuilding the view without it would snap
a first-person frame upright and back every other frame. `phd_LookAt` now
records what it was handed (`CamRoll`, `specific/3dmath.cpp`), which is the one
place that catches all three because they share nothing else. In first person
the value is read through the camera's own new getter `GetFirstPersonRoll()`
instead, because `fpRoll` is private to `camera.cpp` and everything that
writes it lives there. It is blended the short way round like any other angle.

**The poison filter (R10 / K7).** `phd_GenerateW2V`, which `phd_LookAt` calls,
carries a piece of game state inside it:

```c
lara.poisoned += (lara.dpoisoned - lara.poisoned) >> 4;
```

an exponential ramp written when there was exactly one `phd_LookAt` per logic
tick. Two extra calls per tween frame would run it three times as often and
the green would come up three times as fast. `lara.poisoned` is saved and put
back around **both** calls, so the ramp still steps exactly once per tick, in
`CalculateCamera`'s own `phd_LookAt` and nowhere else. The engine already knew
this hazard - `specific/output.cpp` does the same save-and-zero around the load
screen's fade loop.

**The first-person filters are not touched (K3).** `fpVel`, `fpSpd`, `fpRel`
and `fpSmoothSlow[]` (#134) are tuned as per-logic-tick exponentials.
`FirstPersonCamera` stays inside `CalculateCamera`, at thirty, seeing exactly
one sample per tick - which the existing `FPHEAD` line still proves. Only its
**output** - `camera.pos`, `camera.target` and `fpRoll` - is interpolated.

**Cut scenes are snapped whole**, and that is a correctness requirement rather
than a preference: their camera is decoded from a frame index
(`game/deltapak.cpp`) and `frigup_lara()` moves Lara *from inside
`DrawPhaseGame` itself*. Blending the first would fight a decoder; restoring
after the second would undo what it did. `InterpBeginFrame` therefore returns
before it writes anything at all while `GLOBAL_playing_cutseq` is set.

**Where the previous state is discarded** - `InterpInvalidate(why)`, with the
reason reaching the log: a level start and a title start
(`game/gameflow.cpp`), `FreeLevel` (`specific/file.cpp`), a savegame being
read (`game/savegame.cpp`, beside `JustLoaded = 1`), and the return from each
of the four menus that stop the world inside `ControlPhase` - the inventory,
the save screen, the load screen and the pause menu.

**`INTERPLOG`**, one line per *logic* second (`GlobalCounter` steps thirty
times a second by construction in both modes and stands still while the
inventory is up, so the line needs no clock and is directly comparable between
a 30 and a 60 run):

```
INTERPLOG level=1 at=12s frames=60 tween=30 alpha=0.75 lara_step=15.8
  items=4.0 snaps=0 (teleport=0 roomjump=0 object=0 invalid=0) camcut=0 cutscene=0
```

`lara_step` is the proof: how far Lara moved **on screen** from one picture to
the next, averaged over the second. Over the same walk it must read half at
sixty what it reads at thirty - the same distance cut into twice as many
steps. It is measured on the position that was *drawn*, not the one the logic
holds, which is the difference between measuring the interpolation and
measuring the game. The counters run at `fps_mode 0` as well (that run is the
comparison), but only ever behind `gLogEnabled`. `HDLOG frame lara` gained
`interp=`, what that frame was drawn at, so the HD seam says for itself that
it saw the interpolated root.

**What `fps_mode 0` does: nothing, again.** `InterpAlphaNow` returns 1 on its
first test, and at 1 both `InterpBeginFrame` and `InterpEndFrame` return
before they write a byte - no snapshot applied, no `phd_LookAt` issued. The
only thing that runs is the frame counter for the log, and only with `-log`.

**Proved offline.** `Preparation/Tools/Diagnose/InterpTest.cpp` (build
`bit.bat`) **includes `game/interpolate_math.h` itself** rather than a copy of
it, so there is no twin to keep in step, and asserts 40 checks: the short-angle
lerp over a sweep of 16,256 angle pairs (direction and distance, with the 128
exact half-turns stepped over and asserted separately), the position lerp over
the whole coordinate range of a level, the snap thresholds against the
engine's own speeds, that a Manhattan sum is never smaller than the true
distance, and the cadence - `framecount` and the alpha rule run for a
simulated minute at both settings and through a hitch: 1800 logic ticks either
way, exactly half the frames tween at sixty, none at thirty, and every drawn
step exactly half a tick.

The reason the angle lerp gets a sweep rather than a spot check: a short
blended *the long way round* is invisible almost all the time and looks like a
rendering glitch when it is not - one enemy, facing roughly south, spinning
backwards for half a tick on every second frame. It has to be proven at the
wrap, and a play test will not put a body on the wrap when you want it to.

**Still at thirty after this order**, on purpose: the pose (order 4), sparks,
rain, snow, debris, blood, drips, bubbles, gunshells, rats, bats, spiders,
ropes, shockwaves and lightning (order 5), and the per-drawn-frame counters
R1-R17 of 5.3 (order 3) - fades, the weather simulation, the lightning
cadence, the level-name caption.

#### 5.10 Order 3 - the per-frame counters, put on the logic tick

Order 2 made the in-between frame show something. This order is about
everything in the engine that **counted** those frames as if they were units
of time. The user's rule for the whole of #148 is *everything must feel like
vanilla, only more frames*: a death fade, a rain shower, a level caption and a
corpse must last exactly as many seconds at sixty as they do at thirty.

**One criterion for all of it.** `InterpIsTweenFrame()`
(`game/interpolate.h`, `game/interpolate.cpp`) is true when the frame being
drawn carries no logic tick. Every counter below asks it and steps only when
it is false. At `fps_mode 0` it is false always, so every guard in the build
is inert and the thirty hertz behaviour is the old behaviour line for line.

Three details of that latch are worth having written down, because each of
them would have been a bug:

- **It is not `alpha < 1` as the snapshot sees it.** A cutscene frame and a
  frame with no previous state are drawn *live* by `InterpBeginFrame` and
  still carry no tick. The latch is therefore set from the cadence, above the
  early returns, or every fade through every cutscene would have run at double
  speed.
- **It lives for the whole loop iteration, not just the draw.** Two of the
  counters sit outside `DrawPhaseGame`: `gfLegendTime--` before it
  (`game/gameflow.cpp`) and `cutseq_busy_timeout--` after it
  (`game/deltapak.cpp`). The first reads the previous frame's latch, the
  second this frame's; both alternate, which is all the rule needs.
- **It is false in every modal loop.** `S_DumpScreen` calls
  `InterpNoteClassicPresent()` whenever the present it is about to make waited
  the full two ticks, and `S_DumpScreenFrame` calls it always. Without that, a
  modal loop entered straight after an in-between frame would inherit a `true`
  that nothing inside it could clear - and `RenderLoadPic`'s `do { ... } while
  (DoFade != 2)` (`specific/output.cpp`) would **never end**, because
  `DoScreenFade` is the only thing in the build that sets `DoFade` to 2.

**The counters, by the plan's numbers.**

| # | what | where | what it does now |
|---|---|---|---|
| R1 | `Fade()` | `game/tomb4fx.cpp` | the grey walks on a tick, the tile is drawn every frame |
| R2 | `DoScreenFade()` | `specific/specificfx.cpp` | nine steps on nine ticks; `a` is read before the step so the in-between frame draws the same grey |
| R3 | `FlashFader -= 2` | `specific/output.cpp` | `DrawFlash()` every frame, the walk on a tick |
| R4 | `FlashIt()` | `game/health.cpp` | returns the last state on an in-between frame |
| R5 | `DrawPickups()` | `game/health.cpp` | despite the name it draws nothing - it is the whole slide-in animation, so it returns early |
| R6 | `after_death` + `KillItem` | `game/draw.cpp`, `game/control.cpp` | moved out of the draw entirely - its own note below |
| R7 | the sky lightning machine | `game/draw.cpp` | strike, flicker and thunder delay all on the tick |
| R8 | rain and snow | `specific/specificfx.cpp` | the simulation loop on the tick, the draw loop every frame |
| R9 | `aWibble++` | `game/draw.cpp` (the `aUpdate()` call) | on the tick |
| R10 | the poison ramp | `specific/3dmath.cpp` | guarded at its source - see below |
| R11 | a spark letting go of its item | `specific/specificfx.cpp` | the random threshold is rolled on the tick |
| R12 | `GnFrameCounter++` | `specific/output.cpp` | on the tick in a game frame, per present in a modal loop |
| R13 | the modal loops | - | nothing to do: they keep two ticks (order 1) |
| R14 | `RegeneratePickups()` | `game/control.cpp` | moved from above `ControlPhase`'s loop into the tick body |
| R15 | `resChangeCounter` | `specific/output.cpp` | done in order 1 (5.3) |
| R16 | `gfLegendTime--` | `game/gameflow.cpp` | on the tick |
| R17 | `cutseq_busy_timeout--` | `game/deltapak.cpp` | on the tick |

**R6, and why it had to leave the draw path.** What stood in `PrintObjects`
(`game/draw.cpp`) was

```
if (item->after_death < 128 && item->after_death > 0 && !(wibble & 3))
        item->after_death++;

if (item->after_death == 128)
        KillItem(i);
```

Three things are wrong with it and only one of them is about the frame rate.

1. It counts per drawn frame, so at sixty every body would fade and be deleted
   in half the time.
2. `!(wibble & 3)` **is dead and always has been**. `ControlPhase` steps
   `wibble = (wibble + 4) & 0xFC`, so the low two bits are always zero and the
   test is always true. It was plainly meant to make the fade a quarter as
   fast - 128 steps would be seventeen seconds instead of the 4.3 they
   actually take. **It is left dead on purpose.** Repairing it would change
   how long every body in the game lies there, which is a game change and not
   a frame-rate change.
3. `KillItem` was being called from inside the walk of the room's own item
   list. For a **spawned** item (`item_num >= level_items`) `KillItem` ends
   with `item->next_item = next_item_free; next_item_free = item_num;`
   (`game/items.cpp`) - it hands the slot to the free list and rewrites the
   very link the loop is about to follow, so the walk leaves the room and
   wanders into the free list. That is a real latent bug and moving the kill
   onto the tick removes it.

The draw now only **marks** the bodies it drew (one byte per item slot) and
`StepAfterDeathFade()` (`game/draw.cpp`, called from `ControlPhase`) does the
counting and the killing. What deliberately does **not** change is that only
bodies in a room that was actually drawn age at all - a corpse in a room the
camera never sees is still lying there when the player comes back, exactly as
at thirty, which is why the tick does not simply sweep all 512 slots. The mark
is also wider than the old increment (`after_death > 0`, not `> 0 && < 128`),
because the second `if` above also fired on a body already at 128 and the
skeleton writes that field directly (`game/skeleton.cpp`).

**R10 was not finished in order 2.** Order 2 saved and restored `lara.poisoned`
around the interpolator's own two extra `phd_LookAt` calls, but `S_DrawPickup`
(`specific/output.cpp`) issues a `phd_LookAt` of its own once per drawn frame
whenever a pickup icon is on screen. The guard therefore moved into the ramp
itself (`aGenerateW2V` and `phd_GenerateW2V`, `specific/3dmath.cpp`), which
covers every caller there is. Core's own arithmetic is untouched, including the
fact that `phd_GenerateW2V` ends by calling `aGenerateW2V` and so steps the
ramp **twice** per `phd_LookAt`.

**Seven more counters the plan's list did not have.** The order also required a
sweep of everything reachable from the draw. It found:

| what | where | decision |
|---|---|---|
| `ammo_change_timer--` | `game/health.cpp` | guarded - the "ammo changed" caption |
| `PoisonFlag--` | `game/health.cpp` | guarded, though nothing in the build reads it - it is dead state |
| the F12 bump-mapping debounce (`c++`) | `specific/output.cpp` | guarded - a debug key, but it is a per-frame counter |
| the underwater dust simulation | `specific/specificfx.cpp` (`DoUwEffect`) | guarded - it is the third weather and behaves exactly like the rain |
| `print->Active--` | `game/footprnt.cpp` | guarded - footprints would have faded in half the time |
| the monitor screens' scan band | `specific/file.cpp` (`DoMonitorScreen`) | guarded - `pos` and `reset` both stepped per frame |
| the gas cloud's `yo` / `sTime` / `mTime` | `specific/specificfx.cpp` (`DrawGasCloud`) | the three steps guarded; the fog bulbs still trigger every frame |

Two more were found and deliberately **left**: the enemy muzzle flash's random
Z rotation (`DrawBaddieGunFlash`, `game/objects.cpp`) and the steel door's lens
flare colour (`DrawSteelDoorLensFlare`, `game/tower.cpp`). Both are per-frame
noise with no state behind them and no duration to get wrong; the rule the
sweep settled on is that **a counter that carries state or a cadence is
guarded, pure noise is not**. Lara's cutscene speech head
(`game/deltapak.cpp`) is the same case.

**And the one that would have moved the game itself.** The plan's safety
argument (0.4) was that the draw and the logic have separate generators -
`GetRandomDraw` on `rand_2`, `GetRandomControl` on `rand_1` - so extra frames
could not desynchronise the simulation. **That is not true of this engine.**
The draw calls `GetRandomControl` in five places, and the star twinkle
(`DrawStars`, `specific/specificfx.cpp`) alone takes **four thousand draws out
of the logic stream on every drawn frame**. At sixty, each of them would
consume the simulation's generator twice per tick and two runs of the same
level with the same inputs would drift apart between the frame rates.

So `InterpBeginFrame` remembers `rand_1` on an in-between frame and
`InterpEndFrame` puts it back (`GetRandomControlSeed()` is the new read-back
in `specific/function_stubs.cpp`; `SeedRandomControl` already existed). The
consumption per logic tick is then exactly what it is at thirty, whatever any
drawing code does. The star twinkle and the floor lasers' water pulse are
guarded on top of that, because both **write their result back** into
persistent state and would otherwise flicker at double speed.

**`TICKLOG`.** One line a second beside `INTERPLOG`, under `-log`:

```
TICKLOG level=3 at=41s fade=0 screenfade=0 weather=30 legend=0 after_death=4
```

The second is a *logic* second (`GlobalCounter`, thirty by construction in
both modes), so **the same line has to come out of a thirty hertz run and a
sixty hertz run of the same scene**. A number that doubles is a counter this
order missed; a number that halves is a guard that also stopped a tick frame.

**The offline proof.** `Preparation/Tools/Diagnose/InterpTest.cpp` (`bit.bat`)
grew a `counters` section: a simulated wall-clock second at both rates with the
engine's own latch rule, asserting that a guarded counter steps thirty times in
both, that the nine-step screen fade spans the same nine ticks (2n-1 presents
at sixty, because it begins and ends on a live frame), and that at `fps_mode 0`
not one frame is ever an in-between frame. It also states the rule's one
honest limit: a frame that ran long carries **two** logic ticks and can only
step a counter once, so a counter falls one behind per doubled frame and does
not catch up - which is what the thirty hertz engine does too, and is why the
guard is a frame test rather than a tick counter. 49 checks, 0 failed.

#### 5.11 Order 4 - the pose

Orders 2 and 3 moved where a thing **stands** and made every clock in the
engine count game steps. This order moves its **limbs**. Until it, a body at
sixty slid smoothly across the floor with its legs ticking over at thirty.

**The machine was already there and it interpolates the wrong axis.** An
animation stores every second, fourth, fifth ... frame - `anim->interpolation
& 0xFF` is the *frame rate*, its high byte the frame size - and `GetFrames`
(`game/draw.cpp`) hands the drawing code the two stored frames around the
current one. A whole second matrix stack (`IMstack`, `aIFMStack`, the `phd_*_I`
functions) then carries the second frame's transform in parallel with the
first's, and `InterpolateMatrix` blends the two. So the engine already blends
poses - **in key frames, not in time**. What it lacked was a factor that is not
one of three constants, and a *previous tick's frame* rather than a previous
key frame.

**One factor in place of four hard-wired branches.** The blend was
`IM_rate == 2 || (IM_frac == 2 && IM_rate == 4)` -> a half, `IM_frac == 1` ->
a quarter, otherwise three quarters, written out twelve times each in
`aInterpolateMatrix`, `InterpolateMatrix`, `InterpolateArmMatrix` and
`aInterpolateArmMatrix` - about two hundred lines for three numbers. They are
now one `IM_alpha` (and `IM_alpha8`, the same factor in eighths for the
integer stack), computed by `InterpKeyAlpha(frac, rate)` in
`game/interpolate_math.h`, and each of the four functions is a loop over
twelve elements.

That replacement had to be **exact**, because `fps_mode 0` must still produce
the frame the build produced before it (the plan's K11 and proof 6):

- the **float** stack keeps vanilla's three *expressions* rather than one
  algebraic rearrangement of them. `(a + b) * 0.5f` and `a + (b - a) * 0.5f`
  are the same number in arithmetic and can differ in the last bit in IEEE
  floats, and a matrix element that differs in its last bit is a frame that is
  not byte-identical;
- the **integer** stack keeps vanilla's *shifts*. `(-3) >> 2` is -1 where
  `(long)(-3 * 0.25f)` is 0, so the factor is applied as
  `a + ((d * alpha8) >> 3)` below a half and `b - ((d * (8 - alpha8)) >> 3)`
  above it - which *is* `a + (d >> 1)`, `a + (d >> 2)` and `b - (d >> 2)` for
  the three vanilla factors, for every value including the negative ones.

**And the factor is vanilla's own, not a better one.** `frac / (float)rate`
would be the "correct" fraction and would change the game. Measured by walking
the animation tables of all sixteen retail level files: **rates 1, 2, 3, 4, 5,
6, 9 and 10 all occur** - 3619, 3350, 35, 5399, 3187, 2, 2 and 120 animations.
At rate 5 vanilla draws the four frames of an interval at 0.25, 0.75, 0.75,
0.75 - the pose stands still for three frames and jumps - and `frac / rate`
would smooth that out. Over three thousand animations would then move
differently from the day the game shipped, which is a different game and not a
smoother one. So the classification stays, and what order 4 adds is a
**midpoint between two poses vanilla really draws**.

**The midpoint, and why it needs no third stack.** Both poses a tick apart lie
on the *same key frame pair*, so their midpoint does too:

```
same interval  pose(f-1) = lerp(K, K', ap), pose(f) = lerp(K, K', ac)
               -> midpoint = lerp(K, K', (ap + ac) / 2)
crossing one   f mod rate == 0, so pose(f) IS K'
               -> midpoint = lerp(K, K', (ap + 1) / 2)
```

`InterpPoseAt` (`game/interpolate_math.h`) is those four lines, `GetFramesAt`
(`game/draw.cpp`) turns them into the pair of frame pointers, and
`GetFramesNow` is what the draw sites call: `GetFrames` on a frame that
carries a tick and at `fps_mode 0`, the midpoint on the frame between two
ticks. The animation's shortened last interval (`GetFrames` reduces the rate
there, comparing a relative frame against an absolute `frame_end` - Core's own
mismatch, reproduced rather than repaired) is followed by `InterpKeyRate`.

The best of it falls out for free: **animations of rate 1, which never
interpolated at all in vanilla, now interpolate.** Their two ticks are two
adjacent stored frames and the midpoint is that pair blended at a half. 3619
of the game's animations are rate 1.

**Rule K6 - where the pose is not blended.** When the animation *number*
changed between the two ticks, or the frame number did not step by exactly one
(a loop wrapping to its own start, a jump, a frame written by hand, an item
standing still), there is no neighbouring pose to blend with. That frame is
drawn with the current tick's pose, which is exactly what thirty hertz shows,
and it lasts a sixtieth of a second. `InterpPoseTime`
(`game/interpolate.cpp`) refuses on top of that whenever the item's **root**
snapped - a teleport, a slot handed to another object, no previous state - so
that a thing drawn live is drawn live whole.

**One detail of that gate is load-bearing.** The snap verdict is recorded in
`InterpBeginFrame`'s own item walk, one line *before* it writes the blended
position over the live one. Asked later from the draw, the same test would
measure half the distance it should and a teleport could slip through it.

**The three feed points.**

- `DrawAnimatingItem` (`game/draw.cpp`) - every enemy, door, machine and
  pickup. `GetFramesNow` instead of `GetFrames`, `InitInterpolateAlpha`
  instead of `InitInterpolate`, and the `if (frac)` branch is now entered on
  in-between frames for animations that never entered it before.
- `CalcLaraMatrices` (`game/delstuff.cpp`) - Lara herself, and with her the
  HD composite, the classic body and the mirror, all of which read
  `lara_matrices`. `Rich_CalcLaraMatrices_Interpolated` takes a float alpha in
  place of `frac` and `rate`; the two integers only ever reached
  `InitInterpolate` anyway. She therefore comes through the *interpolated*
  branch on every in-between frame, which is the one thing this order had to
  achieve for her.
- **Lara's nine aim angles** - the torso twist, the head turn and the two
  arms - are blended by the **snapshot** rather than by the pose machine,
  because that is what they are: per-tick values that come from the player and
  not from the animation, written by `LookLeftRight` and the weapon code and
  read by both halves of `CalcLaraMatrices` through `phd_RotYXZ`. Eighteen
  shorts in `InterpCapturePrev` / `InterpBeginFrame` / `InterpEndFrame`, no
  draw code touched, and angles blend the short way round like every other
  angle in that file.

**`HairControl` needed nothing, and that was worth checking rather than
assuming.** It is called from inside `ControlPhase`'s tick loop
(`game/control.cpp`), so it never runs on an in-between frame at all. The
braid's *positions* are in order 2's snapshot and are already blended; `DrawHair`
(`game/hair.cpp`) builds its own matrices from those positions on every drawn
frame. The plait therefore follows the blended neck with no change here.

**The HD path followed with no change to `specific/hd/` either**, as the plan
said it would. `HDObjectNode`'s `interpolated` branch already does
`phd_PushMatrix(); InterpolateMatrix(); memcpy(...)` - the matrix
`phd_PutPolygons_I` would have drawn the classic mesh with - and it is now
entered on in-between frames as well. `INTERPLOG`'s `pose_hd` counts exactly
those, from `DrawAnimatingItem` where both facts are known.

**What is deliberately left at thirty, with its reason.** The **frame of the
weapon animation in Lara's hands**. After `aInterpolateArmMatrix` the arm
chain takes the rotation whole from the shoulder matrix and continues on a
single stack (`game/delstuff.cpp`); to blend the arm's own frames it would
need the second stack back, and then it would be blended by **the body's
factor** - the two stacks would be carrying two different key frame pairs at
one `IM_alpha`, which is arithmetically wrong, not merely awkward. What the
player actually steers - where the arms point - is blended, by the aim angles
above. The remaining piece is the few frames of a draw, holster or reload,
and it would cost a rebuild of that chain.

Also left alone: `GetBoundsAccurate` and `GetBestFrame`, which produce
*bounding boxes* for clip windows, shadows and lighting. They stay on
vanilla's own pair, which is the same approximation vanilla already makes on
every interpolated tick frame - and the sequence of boxes over a crossing is
`K, K, K'`, so nothing alternates.

**`INTERPLOG` grew four fields** - `pose_items`, `pose_skips`, `pose_hd`,
`lara_pose` - documented in `docs/Engine-Manual.md`.

**The offline proof.** `InterpTest.cpp` (`bit.bat`) grew two sections and
**198 checks pass, 0 failed**:

- `pose_factor` transcribes the four hard-wired branches as the *reference*
  and compares them against the header's own functions over 441 matrix
  element pairs (rotation values, world translations, the negative odd
  numbers that separate a shift from a cast) for every `(frac, rate)` the
  retail levels can produce - 336,471 animation frames' worth - and then over
  every pair up to rate 32. **Maximum difference: 0 units, on both stacks.**
- `pose_time` walks an animation at sixty hertz and prints where the pose
  stands in key frame units on every drawn frame. Rate 4 comes out as even
  eighths, rate 2 as even quarters, rate 1 as exact halves, and rate 5 as
  vanilla's own uneven steps with a midpoint between each pair. It asserts
  that the pose never walks backwards, never leaves its key frame pair, is
  vanilla's own value to the bit on every tick frame, follows the shortened
  last interval, and is refused by each of the six K6 cases.

#### 5.12 Order 6 - VSync, and the rate the monitor sets

Orders 1 to 4 gave the engine sixty pictures a second of a thirty hertz game.
This one hands the pacing to the display itself, and switches on the vsync the
port never had.

**Two options, one page.** `tomb5.vsync` (on by default) and a third position
for `tomb5.fps_mode`:

| `fps_mode` | the row says | what the frame waits for |
|---|---|---|
| 0 | `30` | two 1/60 s ticks, spinning - the original |
| 1 | `60` | one tick, spinning - the default since order 1 |
| 2 | `Display` | nothing of its own: `Present` waits for the panel |

They are one subject and they now have a page of their own, *Display*, third
in the tomb5 options cycle. Pages 0 and 1 were both at the fourteen-row
ceiling (`troyestuff.cpp`), so *Frame rate* moved off page 0 rather than being
squeezed in - and the row that offers *Display* belongs beside the row that
says whether the present waits, because without the second the first cannot be
honoured.

**VSync is a device-creation parameter and behaves like one.** D3D9 takes
`PresentationInterval` in `D3DPRESENT_PARAMETERS`, so it is read in `DXCreate`
(`specific/dxshell.cpp`) - at startup, on Alt+Enter and on a resolution
change; the device-lost path in `DXShowFrame` resets from the same
`PresentParams` and follows for free. `DXCreateD3DDevice` checks
`caps.PresentationIntervals` first and drops back to `IMMEDIATE` with a log
line rather than failing to create a device at all, and `DXVSyncActive()`
afterwards answers what the device REALLY got - which is what the frame rate
hangs on, never the option. The row therefore reads `on (restart)`: it is the
one setting in the build that is not instant, and saying so on the row is
better than a manual nobody has open.

One ordering problem came with it. `init_tomb5_stuff`, which reads every tomb5
option, runs on the game thread that `WinMain` starts *after* the first
`DXCreate`. So `load_tomb5_present_settings()` (`tomb5/tomb5.cpp`) reads that
one value ahead of time, through a new **read-only** registry helper,
`REG_PeekLong` - `OpenRegistry` uses `RegCreateKeyEx` and `init_tomb5_stuff`
decides from `REG_KeyWasCreated()` whether to write the whole set of defaults,
so a second reader that opened the key early would have silently taken a first
run's defaults away.

**The rate is bound by letting the present wait - nothing measures a rate and
spins against it.** `GameFrameMinTicks()` returns **0** for a display-bound
game frame, so `while (n < gPresentMinTicks)` is not entered and the loop is
paced by `Present`. At 144 Hz that means two frames out of three come back
from `Sync()` with no whole tick at all: `ControlPhase(0)` runs no logic step,
which is exactly right, and the frame is drawn from the clock instead. There
is **no uncapped mode** - the user ruled it out - and there is no path to one:
without a blocking present `ActiveFpsMode()` reports 1 and the sixty hertz
spin is back.

**The blend factor comes from the clock, and `(framecount + subTick + 1) / 2`
is the only mapping that never has to be clamped.** `specific/time.cpp` gained
`TIME_SubTick()`: how far past the last tick `Sync()` gave away the clock
stands, read WITHOUT consuming it and deliberately not capped at one.
`framecount` is ControlPhase's accumulator, which the loop leaves at -1 or 0.
Their sum is the time since the last logic step, continuously - when `Sync()`
consumes k ticks the one drops by k and the other rises by k - and the `+1`
is where the newest state sits in wall-clock time: the loop spends two ticks
per step but runs one as soon as the accumulator is positive, so the state
after step *k* is the newest there is from `S = 2k-1` to `S = 2k+1`, and
reading it as the world at `2k+1` is what makes the whole of that window an
interpolation rather than a guess. The factor then walks 0 -> 1 across every
logic step at any refresh rate, with no accumulator of its own and nothing to
drift. The derivation is written out over `InterpRefreshAlpha`
(`game/interpolate_math.h`), where the arithmetic lives so that `InterpTest`
can check it without a game.

It is therefore **not** 1.0 on the frame that carries a tick, which is what
the order asked for at first: it is 0 there and 1 where the next tick is due.
The clamp is for hitches only, and for the early returns inside `ControlPhase`
that can leave the accumulator positive - both draw the newest state, which is
what thirty hertz shows on a slow frame.

**The clock has to be read on the OTHER SIDE of the flip, and this is the
thing about the order that cannot be seen in a code review.** Every other
present in the engine reads `Sync()` first and flips afterwards, because there
the WAIT IS THE READ - `while (n < min) while (!Sync());` is the pacing and
the measurement in one. Here the wait is inside `Present`, and a `Sync()`
taken before it misses the whole of it: the ticks spent waiting land in the
next frame's count instead, one frame late, the factor asks for a state the
logic has not reached, and the picture stands still for a frame **at every
logic step** - a thirty hertz stutter on top of a 144 Hz picture. So
`S_DumpScreen` flips first and reads the clock afterwards in this one mode.
`PROF_SPIN` is empty there and `PROF_FLIP` carries the wait, which is the
honest place for it: the CPU is inside the driver rather than burning a core
in a loop of ours.

**`alpha < 1` stopped meaning "this frame carries no logic tick".** Order 3's
whole rule rests on that test and it is only equivalent to the cadence while
the cadence is two frames to a tick; at 144 Hz it is true on four frames out
of five, and every fade, raindrop, corpse and caption in the build would have
stopped. The ticks are counted instead - `g_tickSerial`, stepped in
`InterpCapturePrev` - and the drawn frame that finds a new one claims it, so
exactly one frame per logic tick steps the counters whatever the panel does.
`fps_mode 1` keeps the parity rule untouched, down to which of the two frames
carries the step.

**A hole in `R15` that only this mode falls into.** The resolution caption
counted drawn frames and scaled its step by `TargetPresentHz() / App.fps` - an
integer division of a target by a *measured* rate, so a measured rate a hair
above the target truncates the step to zero and the caption never leaves the
screen. With the panel pacing the frames that is every second reading. It
counts 120 **logic ticks** now, like every other duration since order 3, which
is also vanilla's own 120 frames at thirty.

**The watchdog, because a driver can lie.** A control panel set to force vsync
off leaves the device claiming the interval while `Present` returns at once,
and then nothing at all bounds the loop. `NoteDisplayBoundPresent` measures
the interval between display-bound presents: six hundred in a row under half
the panel's period is not jitter, it is an overridden interval, and the frame
rate falls back to the sixty hertz spin for the session with one line in the
log. An iconic window is not counted - `Present` on a minimised window can
return at once with nothing to compose against, and coming back from the
taskbar must not cost the player their setting.

**What the logs say.** `FPSLOG` gained `real_fps=`, the rate worked out from
the mean present interval rather than from `frames=` (which is a count over a
second that begins and ends on a frame boundary and is a frame out either
way); `fps=` is what the mode asks for, and on a healthy display-bound run the
two agree with the monitor. The frame budget is one function now,
`PresentSlotMs()` in `specific/output.cpp`, which both `FPSLOG` and `PROFLOG`
call - a third case had arrived, the frame whose slot is the panel's period
rather than a whole number of ticks, and the two logs must not be able to
disagree about it. `INTERPLOG`'s `alpha=` is a distribution now,
`min/mean/max`: bound to the display every frame has a factor of its own, and
their spread is what says the pacing is healthy - a maximum pinned at 1.00
with a mean near it is the clamp firing.

**The offline proof.** `InterpTest.cpp` (`bit.bat`) grew a `refresh` section
and **220 checks pass, 0 failed**. It models the engine's own loop - draw,
flip at the panel's vblank, read the clock after it, hand the whole ticks to
`ControlPhase` - with the vblanks on their own grid at their own phase, so
that the two clocks are as independent as a 144 Hz panel and a 1/60 s counter
really are. Ten simulated seconds at 60, 120 and 144 Hz each run **300 logic
steps** against 600, 1200 and 1440 frames; the factor is **never clamped**; no
frame draws what the one before it drew; exactly one frame per logic tick
carries the counters; and what is drawn tracks the wall clock with a spread of
under 1e-7 ticks, which is a float's precision and not drift. A 200 ms hitch
is caught up rather than dropped, less the two ticks vanilla's own
`if (nframes > 10)` cap discards.

**What it costs.** One 1/60 s tick more display latency than `fps_mode 1` -
which is what a factor that is never clamped costs, and why 60 remains the
default - and, on the other side, the busy-wait is gone: a core that spun
inside `while (!Sync());` for the rest of every frame's slot now waits inside
the driver instead.

**`fps_mode 0` and `vsync 0` are the engine as it was**, with one deliberate
exception: `R15` above, whose step is a logic tick rather than a rounded ratio
in every mode. At thirty frames a second that is vanilla's own 120 frames, and
it is the version that cannot leave the caption standing.

#### 5.13 Order 5 - the particles

Orders 2 and 4 gave every BODY in the game a place between two logic ticks.
This one gives it to everything else that moves: the sparks, the flames, the
smoke, the rain, the snow, the underwater dust, the blood, the drips, the
flying shards of a smashed object, the shockwave's expanding ring, the spent
shell casings, the bubbles, the rats, the bats, the spiders and a rope Lara is
hanging from.

The user's decision on it was one line - *yes, interpolate; everything as
vanilla, only more pictures* - and the plan's own recommendation had been the
other way (`Preparation/60FPS-Plan.md`, decision 6: leave them, and revisit
only if the user's eye finds them). The eye found them.

**The insight that makes it nearly free.** A particle does not need a
snapshot, because it already carries the thing a snapshot would have held.
Every particle simulation in this engine has the same two last statements:

```
sptr->Yvel += sptr->Gravity;      // whatever settles the velocity
sptr->x    += sptr->Xvel >> 5;    // ... and then the step it takes
```

The velocity is settled FIRST and the position moves by it SECOND, so the
stored velocity *is* the step the last tick took, and

```
previous position = current position - step
```

is exact rather than an estimate. Order 2 stores 15 KB of previous state for
512 items; order 5 covers a thousand sparks and two thousand raindrops with a
subtraction, no memory at all and nothing to restore afterwards.

**The time axis, which is the part that could have gone wrong silently.** A
body is drawn at `prev + (cur - prev) * alpha`, i.e. the world at
(tick - 1) + alpha. The obvious reading of "move the particle along a bit" -
`cur + vel * alpha` - would draw it at tick + alpha, **one whole logic step
ahead of everything around it**: sparks running out in front of the gun that
made them, rain falling through a floor that has not arrived yet. It would
also look perfectly smooth, which is why it is worth writing down. So a
particle is walked BACKWARDS from the state the game holds and never forwards,
and the arithmetic is one line (`InterpTrailLong`, `game/interpolate_math.h`):

```
InterpTrailLong(cur, step, alpha) = InterpLerpLong(cur - step, cur, alpha)
```

It is written as the snapshot's own lerp of a reconstructed `prev` and **not**
as `cur - step * (1 - alpha)`, which is the same line of algebra and not the
same integer: a C cast truncates towards zero, so the two forms disagree on
odd steps and a particle would sit a unit away from where the item machinery
would have put it. `InterpTest` sweeps every step a TR5 particle can carry, in
both signs, against both forms - the wrong one is out on **16,384 of the
40,965** pairs, which is what a rearrangement that "obviously cannot matter"
costs.

**Ten draw sites, and each keeps vanilla's own expression at thirty.** Every
one asks `InterpParticleAlpha()` once, before its loop, and takes no branch at
all when it answers 1:

| what | file | the step it walks back |
|---|---|---|
| `spark[1024]` | `specific/specificfx.cpp`, `S_DrawSparks` | `Xvel >> 5`, plus the outdoor wind |
| `fire_spark[20]` | `S_DrawFireSparks` | the same, on a short offset from the fire |
| `smoke_spark[32]` | `S_DrawSmokeSparks` | the same, the mirror copy counted once |
| `blood[32]` | `DrawBlood` | `Xvel >> 5` |
| `Drips[32]` | `DrawDrips` | `Yvel >> 5` and the wind, if the room is outside |
| `debris[256]` | `DrawDebris` | `Speed * sin(Dir) >> W2V_SHIFT`, `Yvel >> 4` |
| `Rain[1024]` | `DoRain` | `xv + 4 * SmokeWindX`, `yv << 3` |
| `Snow[1024]` | `DoSnow` | `xv`, `(yv >> 1) & 0xFC` - and nothing for a settled flake |
| `uwdust[256]` | `DoUwEffect` | `xv`, `(yv & ~7) >> 6` |
| `ShockWaves[16]` | `DrawShockwaves` | the RADIUS: `Speed` and `Speed >> 1` |

The sparks are the interesting case: two thirds of the array hangs off an item
or an effect and stores an OFFSET from a body that order 2 has already
blended. The walk back is applied to the finished WORLD position, which is the
same number as blending both halves - the anchor is added to either side
identically - and it also leaves the "let go of the thing you came off" code
above it writing the LIVE position, which is game state and must not have half
a step in it.

**And six things that have a place but no velocity joined the snapshot.**
Where the step is not recoverable, order 2's own machinery is cheaper than
guessing:

| what | why not the velocity |
|---|---|
| `Gunshells[24]` | a bounce negates the fall speed and writes the position back, so the stored speed is not the step that was taken - on the one tick anybody watches a shell for |
| `Rats[32]`, `Bats[64]`, `Spiders[64]` | the step is `sin(y_rot) * speed` and both the angle and the speed are rewritten *after* the position moves. They are small animals, not particles: `pos + speed`, the same shape as an `items[]` entry |
| `Bubbles[40]` | the drift is a sine of a rotation that has already advanced, and the impulse decays after it is spent |
| `RopeList[8]`, 24 links each | a rope is a constraint solve; there is no velocity in the drawn segments at all - and Lara HANGS on it, so if she is interpolated and the rope is not, her hands leave it by half a step on every second frame. The same argument as the braid in order 2, and the same answer |

Together they cost 16.7 KB of statics - 10.1 for the 184 movers with their
pointer table, 1.2 for the bubbles, 5.6 for the ropes, which takes the whole
snapshot to about 44 KB - and **not one line of their own drawing code**:
`DrawGunshells`, `DrawRats`, `DrawBats`, `DrawSpiders`, `DrawBubbles`
and `DrawRope` read the same globals they always read and find the blended
values in them. They carry order 2's snap rule as well - half a sector in one
tick is a teleport, not a step - and "was this slot alive on the last tick" is
their identity test, because a bubble slot is freed by writing its size to
zero and handed straight to the next bubble.

**What is deliberately left at thirty.**

- **The lightning.** `UpdateLightning` does not move its segments, it re-rolls
  them out of the random generator on every tick. A bolt's previous shape is
  not a neighbour of its current one, it is a different bolt, and blending
  them would draw a shape the game never draws at thirty - the one thing this
  order is not allowed to do.
- **A shard's tumble.** Where debris FLIES is interpolated; the way it SPINS
  is not. Its two rotations are unsigned bytes shifted up by eight to make an
  angle, so blending them as bytes would wrap the wrong way half the time, and
  a shard flying smoothly while spinning in thirty hertz steps is not
  something an eye can pick apart at the speed these things move.
- **Splashes and ripples.** Their radii grow like a shockwave's, but their
  vertical term steps its velocity on the same line as the position and their
  life is decremented in two places, so the walk back is not exact. They last
  a few tenths of a second on the surface of water. Named here rather than
  silently skipped.

**Two approximations, both named.** A snowflake's sideways drift is ramped
towards the wind by two units a tick after it has moved, and a dust mote's
sink is a quantised reading of a field that is stepped after the move. Both
are a unit or two out of a step of sixty-odd, on a sprite four pixels wide,
and both are written down where they are made rather than left for somebody to
find.

**`particles=` and `particle_kinds=` on the `INTERPLOG` line.** The first is
how many particles were drawn between two ticks per frame; the second is which
KINDS, one bit each. The mask is what makes the line diagnostic rather than
decorative: a draw site whose guard never fires looks exactly like thirty
hertz and there is no other way to see it. Standing in the rain with no `0040`
bit is a bug; no `2000` bit in a level with no rope in it is not.

**The proof.** `InterpTest`'s `particles` section, 14 checks: a falling
particle under gravity - the velocity changing every tick, which a fixed-speed
model would not catch - simulated tick by tick and drawn beside an ITEM whose
previous state really is remembered, over sixty frames at `fps_mode 1` and 720
at 144 Hz. **The two agree to the unit on every single frame**, the particle is
never drawn past the state the game holds, never before the state it came
from, and never a step backwards; at `fps_mode 2` the frame that finds a
factor of nought draws the previous state exactly. A factor of 1 is the
identity over the whole range, which is what keeps `fps_mode 0`
byte-identical, and a particle born on the last tick is drawn between its
birthplace and where that tick left it - never ahead of it.

#### 5.14 Round 154 (#154) - the ring and the title at the frame rate

"Sehr merklich, wenn das Spiel 60 hat und das Inventar 30" - the user, 13
September 2026. His whole-game log says it in numbers: `FPSLOG` reads
`frames=165 real_fps=165 budget=6.1 late=0` while he plays and
`frames=30 logic_ticks=0 budget=33.3` for every second the inventory is up,
and `level=TITLE frames=30 logic_ticks=30 real_fps=30.0` for the menu he
starts from. Order 1b put both of them there on purpose (5.8) and this round
takes them off it.

**Both now present at the rate the option asks for, and both keep their logic
at thirty.** Nothing else moved: the pause screen, the load and save screens,
the death menu, the F4 outfit screen and the loading fade still wait their two
ticks, `fps_mode 0` runs the instruction sequence it ran before, and the
randomizer is untouched.

##### The title: one tap, one row

The fault order 1b found is real and is still the whole of the problem.
`DoTitle`'s loop is `TitleOptions(); DrawPhaseGame(); ControlPhase(nframes);`,
so the menu is walked once per PRESENT while `dbinput` is written once per
TICK - at one tick a present every second `ControlPhase` runs no tick, the
edge keeps standing, and the next `TitleOptions` reads the same press again.

What 1b had not got to is that the cure need not cost the title its frame
rate. A menu only has to be told which of its calls is reading an edge for the
second time, and on that call its input is **shadowed**: `input` and `dbinput`
are put aside, zeroed for the length of the one call, and put back byte for
byte the moment it returns (`TitleInputShadow`, `game/gameflow.cpp`). The
question is asked of the engine's own tick counter and not of a clock of its
own - `g_tickSerial` steps once per logic tick at every frame rate and is now
readable as `InterpTickSerial()` - so it is right at `fps_mode 1`, at
`fps_mode 2` where four or five frames fall inside one tick, on the first
iteration after `DoTitle`'s priming `ControlPhase`, and through a hitch.
`GameFrameMinTicks()` then gives the title what it gives a played frame, and
the fly-by is drawn through `DrawPhaseGame` with order 2 underneath it, which
it always could have been.

**One counter behind the title needed the guard the rest of the build already
has**: `DoCredits`' roll (`StartPos--`, `specific/alexstuff.cpp`) is one pixel
a present and would have scrolled five times too fast on a 165 Hz panel. It
asks `InterpIsTweenFrame()` now, like every other counter of order 3. Nothing
else `TitleOptions` reaches is per-present: the level select, the whole
options tree and the Tomb5 pages all step on `dbinput`, which the shadow
covers, `JoyMenuStep` has a latch of its own, and the two direct `keymap`
reads in the key-mapping screen are idempotent.

**And the rule the round was briefed with was wrong, which is worth writing
down.** The brief said `dbinput`'s previous value is part of the next tick's
answer, so that zeroing it between ticks would make a held key a fresh press
every tick. `specific/input.cpp` says otherwise: the debounce is **two**
statements with the frame's own read between them - `if (debounce) dbinput =
inputBusy;` (`:821`, the PREVIOUS frame's), `inputBusy = linput;` (`:869`),
`if (debounce) dbinput = inputBusy & (dbinput ^ inputBusy);` (`:881`) - so
what comes out is the rising edge against the previous frame's INPUT and
`dbinput`'s own incoming value is discarded by the first line. The shadow is
restored all the same, and for a reason the code really has: `SetDebounce` is
cleared inside the FIRST tick of a `ControlPhase` call (`game/control.cpp`),
so a call that runs three ticks writes `dbinput` on one of them and a zero
left behind would swallow the edge for the other two. `InterpTest` runs both
builds side by side - three reads against one.

##### The ring: the frame rate outside, thirty inside

`S_CallInventory2` (`game/newinv2.cpp`) keeps its own modal loop and its own
present, and that present now asks for what a played frame asks for
(`InvFrameMinTicks()`; `gPresentMinTicks` is raised around THAT present only
and put straight back to two, exactly as `DrawPhaseGame` does it, so
`FPSLOG`'s `budget=` reads the ring's own slot). The loop body then runs its
LOGIC once per two ticks, through an accumulator of exactly `ControlPhase`'s
shape:

```c
const bool invStep = InvInterpBeginFrame();	// acc += ticks; step while acc > 0, acc -= 2
```

with one difference from `ControlPhase` and it is written out where it lives:
the ring's body DRAWS as it steps, so the step can run at most once per frame,
and the accumulator is clamped to one step's worth of credit. A hitch that
banks four steps would otherwise make the ring step on every frame until the
debt was paid - the ring running at the frame rate, which is the one thing
this round exists to stop.

On a frame with no step, nothing of the loop's state moves. The input is
shadowed (the same shadow as the title's, plus the six `go_` flags, which
`do_debounced_joystick_poo` rebuilds from `input` on every step frame and
which would otherwise still be standing from the step before), and every
stepping site inside the ring's reach asks the build's one criterion,
`InterpIsTweenFrame()` - which the ring raises for itself now that its present
no longer comes through `InterpNoteClassicPresent` (`InterpNoteMenuFrame`,
`game/interpolate.cpp`).

**What moves on a tween frame is the picture, and it is handed to the draw at
the READ rather than written into the ring.** Order 2 writes its blended
values into `items[]` and puts the bytes back afterwards, because in the game
the whole tick is over before `DrawPhaseGame` is entered. The ring is not
built that way: `draw_current_object_list` steps and draws in one pass - the
spin and the brightness ramp sit inside the very loop that draws each icon,
and the slide is stepped at the bottom of the same function, after the draw.
A value written before that body is stepped over by the body itself, and one
written after it is too late for the picture. So `game/inventory_interp.cpp`
keeps the snapshot and eight accessors answer it, each returning the live
value whenever there is nothing to blend.

**And it is the STEP frame that is blended, which reads backwards until the
order of the loop is written out.** In the game the draw comes before the
tick, so the drawn states are `T1, mid(T1,T2), T2` - the live frame first. In
the ring the step comes before the draw, and the same arrangement draws `C1,
mid(C0,C1), C2, mid(C1,C2)`: a picture walking backwards on every second
frame. The even, monotone half steps are the other way round. That is not a
preference - it is what `InterpRefreshAlpha`, order 6's own derivation,
returns for the ring's accumulator: 0.5 on the frame whose step ran, 1.0 on
the next. `InterpTest` runs the two arrangements on the same simulated spin:
**the briefed one walks backwards on 200 of 400 frames, the engine's on
none.**

**The table.** Every per-present counter and every moving field in the ring's
reach, and what happens to it on a frame that carries no step:

| what | where | on a tween frame |
|---|---|---|
| `S_UpdateInput`, `input`, `dbinput` | the loop body | not called; input shadowed to zero and restored |
| `go_left/right/up/down/select/deselect`, the `*_debounce` and `left/right_repeat` | `do_debounced_joystick_poo` | not called; the six flags shadowed and restored |
| `GameTimer++` | the loop body | held - it is the stats page's clock, `GameTimer / 30` |
| `UpdatePulseColour` (`PulseCnt`) | `game/text.cpp` | held |
| the four keymap cheats | the loop body | not run |
| `GnFrameCounter` | `S_DumpScreen` | held by the latch - the keypad's flash and the blinking text read it |
| the selected icon's spin (`yrot += 1022`), `spinback` | `draw_current_object_list` | **blended**, the short way round 65536 |
| the brightness ramp (`bright += 16` / `-= 16`) | `draw_current_object_list` | **blended** |
| the slide (`objlistmovement += 8192`), `curobjinlist`, the turn's sound | `draw_current_object_list`, bottom | held; the slide **blended** from the two previous generations, and a step in which `curobjinlist` changed is a JUMP and draws live. The row's LAYOUT takes `curobjinlist` from the same generation as the offset - see below |
| `combine_ring_fade_val`, `normal_ring_fade_val` and the hand-overs at their ends | `step_ring_fades` | held; the two fades **blended** |
| `ammo_selector_fade_val` | `fade_ammo_selector` | held; **blended** from the two previous generations |
| the ammo selector's own spin | `draw_ammo_selector` | **blended** |
| `examine_mode += 8` | `do_examine_mode` | held; **blended** (it is the zoom's own shade) |
| `stats_mode += 8` | `do_stats_mode` | held, not blended - nothing reads it as a value |
| `keypadpause--` and its six tones | `do_keypad_mode` | held, both halves inside one guard |
| `current_selected_option`, `menu_active`, `ringactive`, `ammo_active`, the keypad digits, every text and count | everywhere | held - discrete state is never blended |
| `Fade`, `FlashFader`, `DoScreenFade`, the F12 debounce, `resChangeCounter` | `S_OutputPolyList` | already guarded by order 3; the latch now comes from the ring |
| `LookCnt`, `med_hotkey_timer` | `S_UpdateInput` | step once per logic step, which is the rate they had |

**Two generations for what vanilla steps after its draw.** The spin, the
brightness, the two ring fades and the examine zoom are stepped BEFORE the
icon is drawn, so the value this frame draws is the new one and the pair is
(previous, live). The slide and the ammo selector's fade are stepped AFTER
their draw, so the value this frame draws is the one the last step produced
and their pair is the two previous generations - which keeps each of them
exactly one step behind the spin, the relation they already have at thirty.

**The slot and the offset are one pair, and getting that wrong was a real bug
in this round before the arithmetic caught it.** `curobjinlist` is discrete and
is never blended - but the step that puts `objlistmovement` back to 0 moves it
in the same breath, so a frame that drew the NEW slot with the OLD offset put
the whole row seven eighths of a slot to the side for one frame, at every turn
of the ring. The layout therefore takes both from one generation
(`InvDrawCur` / `InterpRingSlot`), and a slot left over from a list that has
since been rebuilt falls back to the live one. The option list under the row
keeps the live slot, which is not an oversight: at thirty
`draw_current_object_list` has already run its stepping block by the time
`handle_inventry_menu` reads `curobjinlist`, so the option text is one step
ahead of the row there too, and this keeps that relation rather than inventing
a new one.

**Cuts.** Opening or closing a mode, the ammo ring taking over, the combine
ring being built, the list being rebuilt by a combine, a separate or a weapon
hot key, the load/save screen, and opening and closing the ring itself: the snapshot is dropped and the next step frame draws live
(`InvInterpCut`, one word each into the log). A rebuilt list needs no call
site of its own - each slot's `invitem` travels in the snapshot, so a slot
that changed hands draws live by itself, which is the same job
`object_number` does in order 2's.

##### The log

`INVLOG`, new, `-log` only and behind `gLogEnabled` like every other seam:

```
INVLOG open level=0 why=open fps_mode=2 min_ticks=0 budget=6.1
INVLOG level=3 at=0s frames=165 steps=30 tween=135 alpha=0.09/0.95 budget=6.1 blend=spin,bright,slide cuts=0
INVLOG cut level=3 at=2s why=menu steps=64
INVLOG close level=3 frames=812 steps=148
```

The second is a LOGIC second - thirty ring steps - because the ring stops
`GlobalCounter` and there is no other clock in it. `blend=` is the same kind
of field as `INTERPLOG`'s `particle_kinds=`: a motion that is plainly on
screen and missing from the list is an accessor whose guard never fires, and
there is no other way to see that from the picture, because a field that never
blends simply looks like thirty. The counters run at `fps_mode 0` as well -
that run is the comparison, `frames=30 steps=30 tween=0` - and `INTERPLOG`
needs no new word for the title: its `level=` is `gfLevelId`, and the title
has one of its own.

##### The proof

`InterpTest` (`bit.bat`) grew `ring_cadence` and `title_shadow`; the arithmetic
they run is the engine's own, because `InterpRingStep`, `InterpRingAlpha`,
`InterpRingSlide` and `InterpRingSlot` live in `game/interpolate_math.h` beside
the rest of it.
**361 checks, 0 failed.** 600 frames at sixty run 300 steps with the clock
agreeing with the parity shortcut on every one; ten seconds at the user's own
165 Hz run 300 steps against 1650 frames with the factor never clamped and the
drawn instant never walking backwards; a 333 ms hitch does not leave the ring
stepping on every frame afterwards; the spin crosses the 65536 wrap four times
with no long-way frame; three whole turns of the ring never blend the slide's
reset halfway back and never draw the slot and the offset from two different
generations; and at `fps_mode 0` not one ring frame is ever blended.

**What only a play test can show.** That an inventory second really reads
`frames=165 logic_ticks=0 budget=6.1` with `INVLOG steps=30` beside it; that
the title's fly-by is blended through the normal path (`INTERPLOG` at the
title with `tween=` half the frames and no run of `camcut=`); that one tap of
Down still moves one row on every page of the options tree; and that the ring
looks right rather than merely measures right.

#### Fix round R - what the night's chain broke in first person (#123, #148)

Orders 1b to 6 and fix rounds K, L and M were written and committed in one
night and played the next afternoon: ANDREA1, first person, `Frame rate 60`,
VSync on, a 165 Hz panel, 2560x1440, HD on. Six findings came back, and four of
them are one fault seen from four sides.

**R1 - the way back to the title menu is a black screen with no input.** The
title draws (the log has its rooms, its draw calls, its braid and a steady
30.0 fps for fifteen seconds) and nothing reaches it.

The cause is a **parity**, and it is why the first title of a session was fine
and the one after a level was not. `ControlPhase`'s accumulator survives a
level change:

```
for (framecount += nframes; framecount > 0; framecount -= 2)
```

At sixty the game loop feeds it one tick a frame and leaves it at 0 and -1 by
turns. The title and the inventory keep **two** ticks a present at every
setting (order 1b, `GameFrameMinTicks`) - and at two, that loop returns
`framecount` to exactly where it started. From 0 it stays 0; from -1 it stays
-1, for as long as the title is up. `InterpAlphaNow` read `framecount < 0` and
answered **0.5 to every frame of the title**, so every frame was an in-between
frame and every counter order 3 guards stood still. One of them is
`DoScreenFade`, whose nine steps are the only thing that ever sets `DoFade` to
2: the fade-in from black never advanced, its full screen black quad was drawn
over the menu for ever, and `TitleOptions` - which zeroes `input` and `dbinput`
until `DoFade` reaches 2 - swallowed every key after the first choice. `DoLevel`
has always cleared `framecount`; `DoTitle` never did.

Both halves are fixed, because a picture must not hang on the parity of an
accumulator. `InterpAlphaNow` now asks `GameFrameMinTicks() >= 2` and returns
a flat 1 for a frame that is not a sixty hertz one - the same test the
display-bound branch beside it already made - and `DoTitle` clears
`framecount` as `DoLevel` does.

**And the first person camera no longer runs outside the played game.**
`FirstPersonActive()`, which the DRAW asks, already refused a flyby and a
cutscene. `FirstPersonEnabled()`, which decides whether the camera RUNS, did
not - and `DoTitle` calls `InitialiseCamera`, which ends in `CalculateCamera`,
at a moment when `bUseSpotCam` is still 0 because the flyby is triggered two
lines later. So one full pass of `FirstPersonCamera` ran on the title level's
Lara, moved the camera onto her head, took the field of view with `AlterFOV`
and left `fpFovSet` and `fpHeadHidden` standing - and nothing ever handed
either back, because from the next frame on the title is a flyby and
`control.cpp` calls `CalculateSpotCams` instead. One `FPHEAD` line and two
`ALTERFOV` lines in the user's log say so. The gate now names the title, the
flyby and the cutscene, so the camera and the draw answer the same question.

**R2 - the camera cut ten to seventeen times a second while Lara stood
still,** and it is the fault behind R3, R4 and R6 as well.

`INTERPLOG camcut=15` in a quiet room is not a camera cutting. It is the
**target measured as a place when it is a direction**. `FirstPersonCamera`
builds

```
camera.target = camera.pos + direction * 0x5100
```

- 20736 units down the line of sight, because the engine's camera has no field
for "which way is she looking". On a target that far away **one degree moves
the point 362 units** and order 2's absolute test snapped at 1024, so any turn
past about three degrees between two ticks read as a cut. An idle animation
turns her head by more than that.

The rule is now scale-aware (`InterpCamAimCut`, `game/interpolate_math.h`):
beyond `INTERP_CAM_FAR_TARGET` (4096) the target is pulled back onto a sphere
of `INTERP_CAM_DIR_RADIUS` (1024) about **its own eye** and the threshold is
applied to the change of DIRECTION alone - the eye's own travel has its own
test with the same threshold right beside it. At that radius the threshold is
a turn of 41 to 57 degrees in one thirtieth of a second, which is 1200 degrees
a second and more; a real cut, which is a new view and not a turn, measures
1448 at a right angle and 2048 at a half turn and is still caught. **The chase
camera is not touched**: its target sits on Lara, a sector and a half away,
under `INTERP_CAM_FAR_TARGET`, where the comparison is the absolute one order
2 shipped, to the bit.

**And a cut is now a cut of the WHOLE picture.** The test used to sit at the
bottom of `InterpBeginFrame`, after every item, effect, hair link, particle and
aim angle had already been overwritten with its halfway value - so a cut left
the camera live and the world half a step behind it. In first person, where the
eye hangs on Lara's own head, the whole level then jumped against her on every
cut frame. The test now runs at the TOP, beside the cutscene test it belongs
with, and a frame whose camera cuts is drawn live from end to end: one frame of
thirty hertz at the one moment the picture jumps anyway.

**R3 - "she drags four models along behind her", and the idle shudders worse
than it did before the interpolation.** This is R2 seen from the body. With the
camera live and the body at the half step on one frame in four, the whole of
Lara moves against the eye by half a step and back again - which the eye
integrates into copies. Nothing else was wrong with her pose: `CalcLaraMatrices`
goes through `GetFramesNow` like every other body, `lara_matrices` are built
once per frame and the HD composite - the body, the inner skin, the face, the
hands, the holsters and the weapon on her back - all read that one array. What
the log could not say is that they do, because `pose_hd` counted HD **items**
alone and in first person the only HD body that matters is hers; it now counts
her too.

**R4 - the neck hole is huge, its edges are torn, and shoulder pieces and
straps are missing.** Two things, and the first is R2 again: with the eye a
half step away from the body the near zone of fix round L - a sphere of 40
units about the camera, in view space - measures a distance the picture does
not have.

The second is the **inner skin's depth tie**. The HD path draws with
`D3DCMP_LESSEQUAL`, which is right for anything drawn once. Fix round K then
drew Lara's body a second time - the same vertices, the same matrices, the
other side of every triangle - to close the neck hole from the inside, and it
is drawn AFTER the outward one. On a closed limb that is harmless: the inward
face is behind the outward one and fails. **On a sheet it is not.** A strap, a
shoulder piece, a flap - one layer of triangles with nothing behind it - has
its inward and its outward face at the same depth to the bit, so the second
draw won the tie and the piece came out in the inner skin's one flat texel with
its normal turned round; and where the two depths differ by one unit of float
precision it did that per pixel, which is what "randomly cut-off polygons with
torn edges" looks like. The inner draw now runs with `D3DCMP_LESS` and is put
back to `LESSEQUAL` beside the cull mode: at a tie the outward face stands, and
through the neck - where nothing was written at all - the inner face still
passes, which is the whole job it has.

**And the near zone is now measured instead of argued about.** The cylinder is
in the bind pose and therefore takes the same patch of skin in every animation
- that is the whole reason fix round J chose it. The near zone is in VIEW
space, so what it takes depends on the pose, and fix round L measured it in the
bind pose alone (nearest survivor 43.5 to 45.2 units over the eight outfits,
which is why the radius is 40). "The hole reaches into her chest, looking down
along her body" is a question about a FOLDED pose and no picture can answer it.
So `HDLOG neckcut` now writes one line a second in first person with the
distance from the lens to the head joint, to the torso joint and to the nearest
joint of all fifteen, in view space, beside the radius in force. A torso
reading under 40 is the near zone eating her chest and the radius is wrong for
that animation; one well above it says the hole has another cause and the next
round may stop looking here.

**The lid of fix round J is not coming back.** It was withdrawn on purpose in
fix round K, with the play test's own picture ("a great flat plate"); the inner
skin is what closes the hole now. *(A lid did come back one round later - see
**fix round S**, S1 - but not that one: round J's was a DOME as wide as the
cylinder it closed and wearing whatever page its rim sat on, and round S's is a
flat disc a neck wide in the cut's own plane. The INNER SKIN is what went
instead, measured out over forty pictures, and R4's depth tie went with it.)*

**R5 - enemies partly or wholly missing in first person.** A soldier at a wall
not drawn at all while his muzzle flash, which the classic path draws, still
fired; a creature close to the camera visible only on one side of a straight
edge.

It is **group M's item half**, and the round's own note beside it says why
without following it: **the classic path hands every item the whole screen.**
`PrintObjects` (`game/draw.cpp`) sets `phd_left = 0`, `phd_top = 0`,
`phd_right = phd_winxmax + 1`, `phd_bottom = phd_winymax + 1` before its item
loop and puts them back afterwards; only the ROOM walk uses
`r->left..r->bottom`. Group M gave every HD item the portal window of its own
room instead, so an enemy in a neighbouring room - drawn whole by the classic
path - was cut to the doorway rectangle, and one whose window came out small
was not drawn at all.

The item half is withdrawn. `HDFrameScissorForRoom` now switches the scissor
test off for every item, which is the classic path's own answer, and takes a
count on the way past: `items_spared=` on the `HDLOG clip` line is how many
items the withdrawn rule WOULD have cut, so the guard-through-the-wall it was
built for can be judged by a number rather than by a memory. **The rooms keep
their windows** - that is where the bleeding really was, and nothing about it
changed.

**R6 - room seams that move with Lara's idle, and the void through them.**
Mostly R2 again: the camera live against a world half a step back moves every
seam in the picture, once every three or four frames, in time with whatever
animation is driving the head. What could not be answered from the log at all
is the question behind it - **were all the rooms, all the portal windows and
all the objects of one picture collected with the same camera?** - because at
sixty the view is rebuilt twice per in-between frame (the blended eye on the
way in, the live one on the way out) and nothing wrote down which.

`ViewSerial` (`specific/3dmath.cpp`) now steps once per `phd_LookAt`, the HD
path writes it down beside every room matrix and every object it collects, and
the `HDLOG clip` line reports the spread as `view=` and `view_obj=`. A healthy
frame prints one number for each and the two are equal; a range prints
`a..b SPLIT` and names the half of the frame that was collected late. It is a
counter and nothing branches on it.

**What this round did NOT find.** The "stale cut constants" theory for R5 is
disproved by the code and stays disproved: `HDObjectBegin` zeroes `entry.cut`
and sets `entry.cull` to `HD_MODEL_CULL_NONE` for every item, `draw_model`
writes `c162`, `c163`, `c164` and `c165` for **every** draw and not only for the
one that cuts, and `gPass.z` - the pixel shader's whole share of the cut - is
`cut && cut->on > 0` per pass per draw. No creature can inherit Lara's
cylinder. The checkerboard strip over a doorway and the black gap at a
brick reveal (the user's screenshots 3 and 4) are not explained by anything in
this round; the `view=` field is there to say whether one camera per picture is
the reason before the next round looks anywhere else.

**The guards.** `fps_mode 0` reaches none of it: `InterpAlphaNow` returns 1
before the new test, and every line of the camera rule lives past
`InterpBeginFrame`'s `alpha >= 1` return. `hd_enable 0` reaches none of the
three HD changes. `ViewSerial` is one increment with no branch on it. The one
change that is not behind either guard is `DoTitle` clearing `framecount`,
which is the same line `DoLevel` has always had.

**The proof.** `InterpTest`, two new sections, 255 checks and none failing:
`camera_aim` runs the OLD rule and the new one side by side over a swept
first person camera - the old one cuts on a three degree head turn, the new one
does not cut on any turn up to twenty degrees a tick in either axis with the
eye moving as well (over 500 cases), still cuts at sixty, ninety and a hundred
and eighty degrees, leaves a chase camera alone and still catches a target
jumping two sectors, and hands back a near target as its own offset to the
unit; `title_cadence` runs the title's own two-ticks-a-present accumulator from
both parities and shows the old rule making every frame of one of them a tween
frame, the new rule making none on either, and the logic tick count unchanged.


#### Fix round S - the neck as a plane with a lid, the pose the short way, and a creature's light (#123, #148)

Four orders, and the first of them is the user's own drawing: a Blender
screenshot of Lara's neck with a red line across it just under the chin, and
one sentence - *"can we not cut there and close the hole like a lid?"*

**S1 - the cut is ONE PLANE in the head's own frame, and a lid closes it.**

Rounds I, J, K and L each cut a shape defined in the BIND POSE - a sphere about
the eye, then a cylinder about the neck axis with a near zone and a node mask
beside it - and each of them came back from the play test with the collar
still standing or the neck still torn. The whole of that trouble is in one
word. A neck vertex is skinned to TWO nodes at once (the one under her chin is
55 % torso and 45 % head, measured in fix round L), so where a bind-pose shape
falls on her skin **moves with the animation**, and the node mask that kept the
shape off an arm kept it off half the neck as well.

A plane carried by the HEAD has neither freedom. What is above it is above it
in every pose and at every camera angle; there is no mask, no near zone and no
second shape; and a triangle that crosses it is cut **exactly at it**, per
pixel, because the pixel shader clips on the interpolated distance.

*The shape.* Four floats in VIEW space, built once a frame in
`hd_laradraw.cpp` out of the interpolated head node matrix: the neck axis
turned by the head's own rotation, and the head joint's view position stepped
`HD_LARA_NECK_PLANE_S` along it. The shader (`hd_model.hlsl`, `ModelVS`) is one
dot product on the finished skinned position - `o.cut = dot(view, plane.xyz) +
plane.w` - where it used to be a cylinder, a sphere and a weighted node sum.
`gNeckAxis` (c163), `gNeckNear` (c165) and the node mask in `gNodeLight[].a`
are gone with them.

*Where the plane sits, measured.* `ModelProbe -neck`, the two-unit bins: every
vertex of the body by depth `s` down the neck axis from the head joint.

| s | CLASSIC / NEVADA / COAST / CATSUIT | RUSSIA | TR3_ANTARC |
| ---: | --- | --- | --- |
| 4 | 389 vertices, widest 28.1 | 389 / 28.1 | 389 / 28.1 |
| 6 | 128 / 26.1 | 128 / 26.1 | 128 / 26.1 |
| **8** | **2 / 16.2** | **22 / 17.5** | **11 / 17.8** |
| 10 | 3 / 20.7 | 25 / 20.7 | 34 / 24.0 |
| 12 | 4 / 19.0 | 36 / 23.4 | 40 / 25.9 |
| 14 | 7 / 21.6 | 25 / 26.2 | 65 / 29.1 |

The hundreds of vertices at 4 and 6 are her **jaw**; the handful from 8 on are
the **neck**, a tube of radius 16 to 26. So the chin ends between 6 and 8 in
every body of the wardrobe and **the plane goes at twelve**: four units clear
of the jaw, well inside the tube, and above every collar there is. Deeper was
refused (at 20 the cut eats neck one can see from outside) and shallower was
refused (at 8 the lid stands 24 units from the eye when a wall holds the camera
on the head joint, and the renderer's near plane is at 20 - four units is not a
margin).

**And the collars stay.** That is the visible change and it is on purpose: the
Russia jacket opens out from s = 12, the parka from 14, and the VCI catsuit's
collar ring stands at radius 70 from s = 6 to 16. All three are below the plane
and all three are drawn, which is what a collar does when one looks down at it.
Every round before this one cut them away and every play test asked for them
back.

*The lid.* A disc **in the very same plane**, skinned rigidly to the head node
- which is what keeps it there: the plane is built from that matrix, so the two
cannot drift apart whatever the animation does. It is `HD_LARA_PART_NECK_CAP`,
made by `build_neck_cap` out of `HDModelSlot::synth` - the machinery fix round
K withdrew with round I's dome, back for a lid that is nothing like it. Its
radius is the widest the outfit's own neck really is in a three-unit band about
the plane, plus three, with the search stopped at 34 so that a **collar** can
never be taken for a neck:

| outfit | the neck measures | the disc |
| --- | ---: | ---: |
| OUTFIT_CLASSIC, VCI, TR3_NEVADA, TR3_COAST, TR3_CATSUIT | 21.6 | 24.6 |
| OUTFIT_RUSSIA | 26.2 | 29.2 |
| OUTFIT_TR3_ANTARC | 29.1 | 32.1 |
| OUTFIT_SUBSUIT (a helmet, out of scope) | 33.2 | 36.2 |

The band is three and not six because at six the search reaches s = 6, where
her jaw is, and the lid came out 28.5 wide on `OUTFIT_CLASSIC` - a third wider
than the hole it closes, which is the beginning of round I's plate. It wears
the **throat texel** fix round K measured (page 8001 at 240/73 for the three
game 5 bodies, 8201 at 114/151 for the four Tomb Raider III guests) and the
SKIN material the hands wear, because what it is is the top of her neck; its
normal points head-ward, towards the camera looking down at it; and it is drawn
with no cut of its own (it lies exactly IN the plane), no cull, and its own node
mask, because first person clears the head node from the body's mask and the
lid rides on that very node.

**S1(4) - and the INNER SKIN of fix round K is gone.** Round K drew the whole
body a second time, keeping the faces the first draw culls, so that a ray
through the neck hole met the far wall of her own chest instead of the room.
With a lid there it was measured out rather than argued out - `ModelProbe
-firstperson`, **forty pictures**: five outfits at three pitches and both eyes
the game uses, and on top of that the head turned forty degrees each way,
tilted thirty each way, and the torso bent twenty-five each way and turned
thirty. Every one of them is **identical to the pixel** with the inner draw and
without it.

| | with the lid | lid off (`-nocap`) | no lid and no inner skin |
| --- | ---: | ---: | ---: |
| CLASSIC, eye on the joint, pitch 60 | 34.0 % room | 43.3 % | 58.1 % |
| ... pitch 75 | 9.3 % | 20.8 % | 34.2 % |
| ... pitch 89 | 0.0 % | 1.3 % | 15.4 % |
| RUSSIA, pitch 75 | 1.3 % | 20.7 % | 21.0 % |
| TR3_ANTARC, pitch 89 | 0.0 % | 0.0 % | 11.3 % |

and `-noinner` moves not one of those numbers. Against that it cost a second
pass over 27,000 triangles every first person frame and one real fault, which
is **fix round R's own R4**: a strap, a shoulder piece or a flap is ONE layer
of triangles whose two sides sit at the same depth to the bit, and the inner
draw won that tie - the torn edges and missing straps of the play test. R
answered it with a `D3DCMP_LESS` for that one draw; taking the draw away
settles it for good and puts the whole HD path back on one depth rule. **R4's
depth-tie question is therefore closed, not carried forward.** What stays is
the measurement the inner skin brought: `measure_neck_skin` still runs, because
the lid wears its texel.

Gone with the draw: `HD_MODEL_SKIN_CW` / `_CCW` / `_INSIDE`,
`HDModelSetInnerSkin`, `HDModelInnerSkin`, `Model::innerPage*`, the shader's
`gInnerSkin` (c164) with its turned normal and its one texel, and the tool's
`-noinner`.

*Proof, by picture.* `ModelProbe -firstperson` over `OUTFIT_CLASSIC`,
`OUTFIT_VCI`, `OUTFIT_RUSSIA` and `OUTFIT_TR3_ANTARC` at 60, 75 and 89 degrees
down and at both eyes - twenty-four pictures, looked at and not only counted.
The Russia jacket straight down from the eye 48 forward comes out with its
collar, its epaulette, its button flap and its jumper whole and no hole
anywhere; the VCI catsuit comes out with its zip and its collar ring whole. The
one case that is worth naming is the WALL case at a shallow pitch: with the eye
on the head joint the camera stands inside her neck, the lid is 28 units below
it and its 24.6-unit disc fills the lower half of the frame. That is what
looking down one's own neck from inside one's own head gives, it is closed
(0.0 % room straight down), and it is the case the game reaches only when a
wall is directly in front of Lara.

*Log.* (Superseded by fix round U, U1, which cut the plane out altogether:
the two lines read `HDLOG firstperson on headcut=0.15` and `HDLOG headcut
cut=1 w=0.15 node=14 head=.. torso=.. min=..@node.. lid=1` now, and there is no
`drop=` column any more - no joint can fall on the wrong side of a bone
weight.) As fix round S shipped them:
`HDLOG firstperson on neckplane=12` on the edge, and once a second
`HDLOG neckcut cut=1 s=12 lens=.. head=.. torso=.. min=..@node.. drop=..` -
where `lens` is how far the plane stands from the eye along its own normal (a
reading under the near plane's 20 is the lid being clipped away) and `drop` how
many of the fifteen joints stand on the discarded side of it. Two - the head
and nothing else - is the healthy reading.

**S2 - the pose blend, and the pair a matrix blend cannot survive.**

*The finding.* A dive roll in the third person at sixty hertz, three captures:
tucked, ONE frame with a leg fully extended the other way round, tucked again.

*The first reading was wrong, and measuring it is what found the right one.*
The obvious explanation is a packed ten bit angle interpolated the LONG way
round - 1000 to 20 giving 510 instead of 1022. **Order 4 interpolates no angles
anywhere.** `GetFramesAt` hands the key frame PAIR and a factor to
`InitInterpolateAlpha`, and the blend is vanilla's own `InterpolateMatrix`,
which lerps the two 3x4 MATRICES element by element. An element-wise lerp of
two rotations always lands on the SHORT-way bisector, so the wrap cannot
happen; `InterpTest`'s new `pose_wrap` section runs the 1000-to-20 case through
that very blend on all three axes and in both directions and gets **1022 every
time**. Measured on the data as well - `ModelProbe -poserisk` over all 319 Lara
animations of ANDREA1: not one adjacent key frame pair has a per-axis packed
difference over 512 after the wrap, and a wrap cannot produce one.

*What an element-wise lerp does instead is SHRINK.* With a relative rotation of
D between the two node frames, the midpoint is the short-way bisector scaled by
`cos(D/2)` in the plane across the axis: 13 % of the bone's frame gone at 60
degrees, 29 % at 90, and at 180 the plane collapses to **nothing** - the frame
is singular, the bone has no length in it, and whatever hangs off it lands
somewhere that is on neither pose. That is a leg pointing the wrong way.

*So which pairs does order 4 blend that vanilla never did?* One word: rate. At
thirty hertz vanilla blends key frames k and k+1 at a quarter, a half or three
quarters - and only when the rate is more than one, because with rate 1 every
frame IS a key frame and `frac` is always zero. Order 4 blends the rate 1
animations too (its own note says so). For every other rate the tween frame
lands INSIDE the same segment the two tick frames are blended on, so it is no
worse than its own neighbours and refusing it would be a jump.

*Measured*, `ModelProbe -poserisk`, ANDREA1, the LARA slot's 474 table entries:

| | |
| --- | ---: |
| animations read | 319 |
| of them rate 1 | 23 |
| carrying a key frame pair that turns more than 90 degrees at some node | 34 |
| of those, rate 1 | **1** |
| worst turn of all | 179.9 degrees, animation 205, node 0 (the HIPS), key 0 to key 1 |

Twenty-two of the twenty-three rate 1 animations turn at most **52.7** degrees
between two adjacent frames at every one of the fifteen nodes - a shrink of ten
per cent, which is what sixty hertz is there to smooth. The twenty-third turns
her **root** half round in one game frame. That one frame is the picture.

*The fix* is rule K6 with one more entry: a rate 1 pair whose two poses turn
more than ninety degrees at any node is **not blended at all**, and the frame is
drawn exactly as thirty hertz draws it. `InterpPoseFar` (`interpolate_math.h`)
walks the two packed rotation streams in lockstep, builds each node's frame in
`phd_RotYXZ`'s own order and compares them by `trace(A^T B) = 1 + 2 cos`;
`GetFramesAt` (`game\draw.cpp`) asks it for `rate == 1` and returns 0 when the
answer is yes. Ninety degrees is the last turn whose midpoint keeps more than
seventy per cent of the frame; over ANDREA1 it refuses animation 205 and
nothing else. `INTERPLOG` gained `pose_far=`, beside `pose_skips=`.

**S3 - the three findings the review of fix round R left.**

**(a) The camera's shell.** Round R chose the far branch **per camera**:
`InterpCamAim` asked each of the two whether ITS target was beyond
`INTERP_CAM_FAR_TARGET`, so a target at 4095 was compared as a place, 4095
units from its eye, and the same target one unit further out as a direction,
1024 units from its eye - three thousand units of difference out of nothing at
all. It could not reach either camera the engine really has (a first person
target sits at 20736, a chase target at about 1500), but a rule with a cliff in
the middle of it is a bug waiting for the third camera. The branch is chosen
**once for the pair** now - far if EITHER length is beyond the shell - and both
offsets take the **same** scale, `INTERP_CAM_DIR_RADIUS` over the longer of the
two. One k and not one each, because two different k's would hide a target that
really did jump along its own ray. `InterpTest`'s new `camera_shell` section
walks a target out through the shell one unit at a time (500 steps, 0 cuts) and
jitters one on it for thirty ticks (0 cuts), and re-asserts round R's own two
answers.

**(b) R4, the inner skin's depth tie.** Closed by S1(4) above: the draw that
needed the tie broken is gone, and the HD path is back to one depth rule.

**(c) `DoTitle`'s `framecount = 0` is outside the interpolation's guards, and
this file should have said so.** It is one line in `game\gameflow.cpp` and it
runs at `fps_mode 0` as well as at 60. It is deliberate and it is harmless: it
mirrors the line `DoLevel` has always had, and what it can cost is at most half
a logic tick at the moment the title is entered - the accumulator is cleared
where `DoLevel` clears it and for the same reason. Fix round R's own guard
paragraph named it; this is it named as a **change that is not behind a guard**
rather than as a footnote. No code changed for it.

**S4 - are HD creatures darker than classic ones?**

*The finding.* The play test's video of the RICH levels: enemies far off in
dark rooms come out **pure black** and are lit only by their own muzzle flash,
while two shots of the same level show them lit.

*What the code says*, and it is read off the source rather than measured: an HD
item's ambient is `aAmbientR/G/B` **after the engine's own
`InitObjectLighting(current_item)`** - the same call, on the same item, that
`phd_PutPolygons` makes for a classic mesh - and its light list is the item's
own `il.pCurrentLights` converted to view space one for one. The level's own
`HDLOG lightcheck` has compared the two arithmetics on the first HD item of
every level since stage S3 and reads a difference of zero.

*What is measured offline.* `ModelProbe -name SWAT -shade N`, the same model at
six ambients, with the model's own share of the frame (16.6 %) separated from
the background:

| ambient | frame mean | the model's own mean | ratio |
| ---: | ---: | ---: | ---: |
| 8 | 142.2 | 2.5 | 0.32 |
| 16 | 142.6 | 4.9 | 0.31 |
| 32 | 143.5 | 10.4 | 0.32 |
| 64 | 145.1 | 20.0 | 0.31 |
| 128 | 148.4 | 39.9 | 0.31 |
| 255 | 154.8 | 78.4 | 0.31 |

The HD path's brightness is a **strictly linear** function of the ambient it is
handed, to three per cent over a factor of thirty-two. It neither clamps, nor
loses a term, nor applies a floor: a creature at ambient 8 comes out at 2.5 of
255 because the room really is that dark, which is the answer the classic path
gives too.

*What could still make an HD body darker, named and counted.* The classic list
holds **21** lights per item (`ITEM_INFO::il.CurrentLights[21]`) and the HD path
carries **12** (`HD_MAX_OBJ_LIGHTS`). Lights past the twelfth are dropped, and
they have been counted on the `HDLOG frame` line as `clipped=` since stage S3.
A dark room is not a room with thirteen lights in it, so this is almost
certainly not the video's cause - but it is the one term that can differ, and
it is a number rather than an opinion.

*And the question is now asked of the creature itself.* `HDLOG creaturelight`
writes one line a second of game time for up to six creatures of the frame -
only `objects[].intelligent` ones, which is the engine's own word for the things
that walk about and shoot:

```
HDLOG creaturelight level=.. slot=..(..) room=.. dist=.. classic=r,g,b
    hd=r,g,b diff=r,g,b lit=.. ambient=.. engine=..sun/..point/..spot
    hd_lights=..
```

`classic` is what `aTransformLightClipMesh` would give this item out of the
engine's own light arrays; `hd` is what the shader gets; `dist` is how far the
creature stands from the lens, because the whole report is about DISTANT
enemies. Two numbers that agree say the HD path is exactly as dark as the
classic one, and a black guard is Core's own room. **No engine change was made
for S4**, because nothing measurable yet says one is needed - the play test's
next log says whether that stands.

**The guards.** `fps_mode 0` reaches none of S2 or S3: `InterpPoseFar` is
called only from `GetFramesAt`, which is called only from `GetFramesNow` inside
`if (t > 0)`, and `InterpPoseTime` returns 0 before `g_applied` is ever set at
thirty hertz; the camera rule lives past `InterpBeginFrame`'s `alpha >= 1`
return, exactly where fix round R left it. `hd_enable 0` reaches none of S1:
every line of it is in `specific\hd`. `HDLOG creaturelight` and `pose_far` are
counters behind `-log` and nothing branches on them.

**The proof.** `be.bat` builds clean (full rebuild, 0 errors, only the
pre-existing warnings), the shader guard regenerated `hd_model_vs.inc` and
`hd_model_ps.inc` and left `hd_room_*.inc` byte-identical, `InterpTest` reads
**269 checks, 0 failed** including the two new sections, and the picture proof
was run here with `ModelProbe`: twenty-four first person pictures over four
outfits, sixteen more under a turned head and a bent torso, and the six
brightness renders of S4.


#### Fix round U - first person without head or neck, the arms on every frame, a body that can stand still (#123, #148)

The play test of fix round S in one line: *"almost perfect - the head and the
neck are gone in first person and the skin cap closes the neck; looking down
into the neck from above shows skin now and not the inside of me. Only: with
the TR3 outfits the cap texture has an artefact in the middle, a dark point
running dark towards it like a cone. With the TR5 outfits the cap is clean."*
Round U finishes that cap, and takes the three other findings the same session
left: the roll at sixty, the arms in first person, and an option to stop the
body moving at all while she stands still.

**U1 - the cut is a BONE WEIGHT and the lid is the body's own RIM.**

*The measurement that ended the search for a shape.* Four rounds cut a shape
in space and four rounds came back wrong: a SPHERE about the eye (stage S8)
took a bite out of her chest, a CYLINDER in the bind pose (rounds J to L) left
the collar standing because a neck vertex is skinned to two nodes at once, and
round S's PLANE carried by the head node **tilts with the head** - the play
test read *"the cutting away has got worse, in animations Lara is partly
rendered away over her whole body"*. A plane through the head joint tilted
forty degrees runs through her chest.

`ModelProbe -headw` measures the one thing no animation can move: how much of
each vertex the HEAD NODE owns, written in the `.TRM` beside the vertex.
`drop` is how many vertices a threshold takes, `r>s8` the widest of them below
the chin - the collar question - and `top` where the highest surviving vertex
within a neck's own radius then stands, in units down the neck axis:

| cut | CLASSIC drop / r>s8 / top | RUSSIA | VCI | TR3 (all four) |
| --- | ---: | ---: | ---: | ---: |
| any | 6860 / 57.3 / 25.0 | 5236 / 28.3 / 10.5 | 8367 / 71.2 / 25.0 | 9622 / 100.3 / 26.2 |
| 0.05 | 6729 / 24.5 / 25.0 | 5141 / 25.0 / 10.5 | 8198 / 71.2 / 25.0 | 6729 / 24.5 / 8.1 |
| 0.15 | 6709 / 24.5 / 18.7 | 5062 / 24.5 / 10.5 | 8153 / 71.2 / 11.6 | 6709 / 24.5 / 8.1 |
| 0.50 | 6645 / 24.5 / -1.6 | 4818 / 24.5 / -1.6 | 8086 / 71.2 / -1.6 | 6638 / 24.5 / 0.5 |

Two readings fix the number. **Any weight at all is too much:** the four TR3
guest bodies (#122) carry a whisper of head weight - under 0.05 - over the
whole body down to the feet, 9,622 vertices against the 6,729 that are really
her head and the deepest of them 688 units down the axis, so "any" makes three
quarters of a TR3 Lara disappear. **Half is too little:** at 0.50 the highest
surviving vertex inside a neck's radius stands 1.6 units ABOVE the head joint
on four of the bodies - a ring of neck around the camera, which is the tube the
play test photographed. Between 0.05 and 0.35 all eight bodies read the same
24.5 for the widest thing taken below the chin, which is the neck's own radius
and no collar. **0.15** is taken out of that plateau.

*The cut, in the shader.* `hd_lara.h`, `HD_LARA_HEAD_CUT_WEIGHT 0.15f`;
`HDLaraHeadCut` (`hd_lara.cpp`) hands the node and the threshold up;
`hd_laradraw.cpp` puts them in `HDModelNeckCut` with nothing to build per
frame at all; `hd_model.cpp` writes them to `c162`; and `hd_model.hlsl`'s
`ModelVS` adds the vertex's share of that node out of the three bones the
skinning already walks (`headWeight += abs(node - gNeckCut.x) < 0.5 ? weight :
0`) while `ModelPS` clips on the interpolated value. **Nothing is measured in
space any more**, so `HD_LARA_NECK_PLANE_S`, `HDLaraNeckPlane` and the plane's
four view-space numbers are gone, and with them the per-frame matrix work
round S needed.

*The lid is a fan over the body's own RIM* (`build_neck_cap`, `hd_lara.cpp`).
The RIM is the set of vertices that do NOT reach the threshold but share a
triangle with one that does - the exact edge of the hole. Every rim vertex is
copied whole: position, three bones, three weights and texture coordinate. That
is what makes the seal permanent: **the lid's edge is skinned by the same sum
over the same joints as the skin it meets**, so the two land on the same point
in every pose, to the bit, and no animation can open a gap. Every lid before
this one was rigid on one node while the skin it met was not.

| outfit | rim vertices | r off the axis | s down it | page | fan |
| --- | ---: | ---: | ---: | ---: | ---: |
| CLASSIC | 20 | 12.4 .. 22.7 | 18.7 .. 37.0 | 8001 | 60 triangles, 2 rings |
| RUSSIA | 23 | 13.2 .. 23.8 | 16.5 .. 34.3 | 8042 | 69, 2 |
| VCI | 20 | 10.7 .. 22.7 | 11.6 .. 37.0 | 8001 | 60, 2 |
| SUBSUIT | 32 | 13.0 .. 27.6 | 5.8 .. 23.1 | 8047 | 96, 2 |
| TR3 (all four) | 20 | 12.4 .. 22.8 | 18.8 .. 37.1 | 8201 | 60, 2 |

**U1b - and the dark point in the middle of the TR3 cap.** The first cut of
this round gave the fan's one shared middle vertex the AVERAGE of the rim's
texture coordinates. `ModelProbe -headw` prints the rim's texels at the
shipping threshold, and the answer is in them:

| outfit | page | u | v | the average |
| --- | ---: | ---: | ---: | ---: |
| CLASSIC | 8001 | 235 .. 241 | 70 .. 168 | 238 / 119 |
| RUSSIA | 8042 | 119 .. 125 | 38 .. 255 | 120 / 150 |
| SUBSUIT | 8047 | 16 .. 40 | 48 .. 84 | 28 / 68 |
| the four TR3 | 8201 | 60 .. 170 | 154 .. 202 | 112 / 178 |

On every TR5 page the neck is **one narrow column six texels wide**, and the
average of a column is in the column - skin. On the TR3 guests' page it is
**two separate bands**, one at v 198..202 and one at v 154..158, and the
average lands in the gap between them. That is the dark point exactly, and the
cone is the interpolation running into it.

*The fix invents no texture coordinate anywhere.* Each spoke of the fan wears
the texel of the rim vertex it hangs from, unchanged all the way in - the
middle included, which is why the middle is now a COPY PER SPOKE (same
position, same normal, same skinning, different texel) rather than one shared
vertex. A triangle of the lid therefore spans two texels that are neighbours on
her skin and samples nothing between them. Picking a *better* single texel
would only have made the lottery smaller: the interior interpolates from each
rim texel towards the middle's, so a rim at u 60 and a middle at u 112 sweeps
the fifty texels between them whatever is written there.

*Proof, by picture.* `ModelProbe -firstperson -pitch 89 -fpforward 0` over all
eight wardrobe bodies, looked at and not only counted: the TR3 cap comes out
one even skin tone with the dark point and the cone gone, and `OUTFIT_CLASSIC`
is unchanged but for a little less swirl. The two cases the play test named are
pictures as well - looking straight down into the neck (the roll), and the eye
moved off the head joint with `-eye 0 30 -70` and pitched down (the landing
after a dive): skin in both, no body interior, no nape, no hair.

**U2 - the roll at sixty, measured rather than guessed.**

*The finding.* At sixty a dive roll shows one frame with a leg stretched fully
the wrong way, curled up on the frames either side of it; at thirty the same
roll is faultless. Fix round S guessed a rate 1 key frame pair too far apart to
blend (S2) - and `INTERPLOG`'s `pose_far` read **0.0 for the whole of the next
session**. A guess that measures zero is a guess disproved.

*The measurement.* `ModelProbe -tween60` replays an animation at sixty offline
- `GetFrames` for the tick frames, `InterpPoseAt` at t = 0.5 for the frames
between them, and the two key frame chains built separately and blended element
by element per node, which is exactly what `Rich_CalcLaraMatrices_Interpolated`
does through `InterpolateMatrix`. Over **the whole LARA animation range 0..473
of ANDREA1 - the 337 of them this level really stores**, every drawn frame, all
fifteen nodes (the other 137 are the level-specific stubs, and the run now says
so itself: *animations asked for: 474 - 337 replayed, 137 stubbed in this
level*):

| | |
| --- | ---: |
| worst distance of a tween frame from the midpoint of its two ticks | **0.00** |
| worst bone shortening, whole table | 0.34 (animation 205, the pair S2 already refuses) |
| worst bone shortening over the rolls and the sprint dive | 0.89 |
| tween frames whose key pair reaches past the animation's own stored keys | 313, all of them reached by the two TICKS as well |

The element-wise blend is **affine in its factor**, so a tween frame inside one
animation is the exact arithmetic midpoint of the two ticks either side of it,
node by node. A limb therefore **cannot** come out beyond the two poses it
stands between. What the blend can do is shorten a bone - it walks the chord of
the arc and not the arc, so a bone whose world frame turns D comes out
cos(D/2) of its own length - and over the rolls and the sprint dive that is at
most a ninth.

*What changed.* Nothing in the pose path, because the measurement says there is
nothing there to change, and a fix on a disproved suspicion is what cost this
project two days once already. What was added is the seam that will name the
frame in the GAME rather than in a table: `InterpNoteLaraStretch`
(`interpolate.cpp`, called from `delstuff.cpp` on every tween frame of the view
set) measures each of Lara's bones against **its own length in the skeleton**
and keeps the worst of the second with the node, the animation and the frame it
happened on. `INTERPLOG` gained `stretch=0.87@node3(anim232 f5843)`. A rigid
pose reads 1.00; under about 0.85 is a limb one can see in the wrong place, and
the animation and frame beside it are then the pair to look at. S2 stays: it is
right for what it guards, and it guards animation 205.

**U3 - the arms on every drawn frame.**

*The finding, and it is the user's own measurement off a 60 Hz video of a still
first person frame:* the background moves 1.87 and 1.88 units per frame in
turn, and the hand and the HK move 0.7 to 1.2 for five frames and then 3.2 to
3.6 on the sixth. The same idle in the third person is even (0.15 / 0.17).
Maximum camera damping changes nothing; `INTERPLOG` reads `pose_hd=0.5` on
every frame, so her body IS getting the blended pose.

*What the code says.* Four of Lara's fifteen nodes do not come out of her
animation at all. With a weapon in her hands the ARMS are driven by the weapon
slot's animation through `lara.right_arm` / `lara.left_arm`, and with the HK in
state 0 to 4 the TORSO and the HEAD are read from it as well
(`Rich_CalcLaraMatrices_Interpolated`, `game\delstuff.cpp`). Order 4 wrote
those frames **raw** - the pistol, uzi and revolver branches walk the arm from
one stack with `gar_RotYXZsuperpack`, and the shotgun, HK and crossbow branch
feeds *the same current frame into both stacks* and blends a pose with itself.
Either way the arm's own animation frame is this tick's on every drawn frame,
with no blend in time at all. At thirty that is invisible; at sixty it is the
whole of what the video photographed.

*What was measured for it.* `AnimMap` over ANDREA1's six weapon slots: the
weapon animations are **not** all rate 1, which the note round U inherited
claimed - `PISTOLS_ANIM` has 26 at rate 1, two at 2 and two at 4, `HK_ANIM` has
13, two and **fifteen**, `REVOLVER_ANIM` 20, one and nine, and `FLARE_ANIM`
carries one at rate 3. It makes no difference, and the reason is Core's own:
the arm chain indexes the frame data by the FRAME number and not by the key, so
it reads one stored frame per game frame however the rate is declared. The two
ticks either side of a drawn frame are therefore two whole stored poses, and
the frame between them wants them at the time fraction t - which is not the
body's key frame factor. Also measured, for the record: `ANIM_BREATH` (103) is
rate 4 over 44 frames, `ANIM_STOP` (11) rate 2, `ANIM_RUN` (0) rate 1.

*The fix.* `InterpArmPrev` (`interpolate.cpp`) keeps each arm's animation
number, frame number and frame_base pointer at the tick and hands the previous
one back with the frame's time alpha. It answers **false** - and the arms are
drawn exactly as they were - at `fps_mode 0`, on a frame that carries a tick,
on a frame drawn live, with no previous tick, when the arm's animation changed,
when its `frame_base` moved, or when its frame number stepped by more than one
in either direction (a pistol arm counts down as well as up). The draw then
takes one of two shapes:

- the pistol, uzi and revolver branches walk the three arm nodes **twice** from
  the same shoulder - once with the previous tick's frame into a scratch of
  three matrices, once with this tick's into the output - and blend the two
  with `InterpBlendFloat`, the engine's own lerp. Two walks of three nodes, on
  the tween frame only.
- the shotgun, HK and crossbow branch already has both stacks in the weapon's
  own animation, so `InterpArmStack` (`game\draw.cpp`) collapses them to their
  blend at the BODY's factor - the joint the arm hangs from - and sets the
  ARM's factor. Stack one then walks the previous tick's frame and stack two
  this tick's, and every `InterpolateMatrix` below is the midpoint in time. The
  HK's torso and head take the same road, at the torso, so the whole of what
  the weapon animation drives is blended and not only the hands. With the HK
  that makes **two** collapses in one draw, and the second is not free: the
  torso collapse is walked apart again through the torso and the head, so the
  arm branch's own call takes the SHOULDER to its midpoint in time and hangs
  both arm chains off that one joint. That is the order wanted - the arm is
  built once, from the shoulder the drawn body really has - and the note over
  `InterpArmStack` says so since U7, where it used to claim the second call
  blended two stacks that were already equal.

`INTERPLOG` gained `arms=<blended>/<refused>/<tween frames>`. With a weapon in
her hands the first number should be the tween count and the second a handful a
second - one on each frame the weapon animation changes number. With **empty
hands, a flare or a torch both stay nought**, because the frame is not counted
at all: the counter is asked in the draw where the weapon is known, and not
from the arm rule, which reads `lara.right_arm` / `lara.left_arm` - fields that
keep their last values while her hands are empty and would answer *yes* to a
frame on which the draw blends no arm whatever (corrected in U7).

*And it is not first-person only, by design.* `InterpArmPrev` asks `g_applied`
and not the camera, so at 60 the arms interpolate in **third** person too -
they are the same four nodes whichever camera looks at her. The even third
person reading in the measurement above (0.15 / 0.17) cannot have come from
smoothing, because before this round there was none in either camera: it is
what a hand that small in the picture reads.

**U4 - a new option: First person view / Body, `animated` or `still`.**

*The user's words:* "mute the animations except the head movement". In first
person the camera hangs in her head joint, so every breath of her idle
animation moves the WHOLE PICTURE - and it is the one thing about the view that
cannot be avoided by looking somewhere else.

`still` holds her body on one stored frame while she is standing, crouching or
kneeling still on the ground, and nothing else. The states, and it is by STATE
and not by a list of animation numbers, because which animation she stands in
depends on what she is holding and what she did last: `AS_STOP` (standing, and
aiming while standing, which keeps the same state, so a rifle held on a target
stops breathing too), `AS_POSE`, `AS_DUCK` and `AS_ALL4S`, each with
`current_anim_state == goal_anim_state` - a state already on its way somewhere
else is not an idle.

What is NOT held: the head's own look angles are not in the frame at all and
the player turns and tilts the view exactly as before; the ARMS are driven by
the weapon slot, so drawing, aiming, firing and reloading are untouched - and
they keep U3's blend in time while the hold is on, which took a correction in
U7 to be true; and
every state that is not in that list - every step, jump, climb, pull, roll and
swim - is untouched. The held pose is the **key frame the idle began on**, so
it is a pose that really exists in the file, nothing about it can drift, and
switching INTO the hold cannot jump at all because it is the pose she was
already wearing. Leaving it costs at most what the idle travels in one cycle,
which is the breath, and it is spent on the frame she starts moving anyway. The
hold is dropped the moment the state stops being an idle one, the animation
changes underneath it, or the view leaves first person - so a savegame, a
cutscene or a death cannot leave her frozen.

It is `LaraBodyStillFrame` in `game\delstuff.cpp`, the held frame **in both
slots** with the factor 0 beside it, so both draw paths - HD and classic - wear
it; the classic first person path draws her body too (it leaves out the head
MESH), which is why this sits in `CalcLaraMatrices` and not in the HD seam. A
blend of a pose with itself is that pose, so the body is frozen whichever
branch runs, and the branch is left as it was: U4 first wrote `frac = 0`, which
sent the draw down `Rich_CalcLaraMatrices_Normal` - a function that reads
`lara.right_arm` / `lara.left_arm` **raw** in all six weapon branches and so
switched U3's arm blend back off in exactly the case U3 was built for. The
shadow takes the held frame's own bounds for the same reason. The
option is `tomb5.first_person_body_still` (`ego_body_still` in the registry),
default `animated`, row five of the "First person view" page under Camera
damping.

**U5 - what the review of fix round S found, in words only.** `hd_model.h`
carried a whole comment for `HDModelSetInnerSkin`, a function fix round S
deleted; `hd_model.cpp` gave the inner skin as the reason two draw ranges may
be merged; `hd_laradraw.cpp` and `hd_lara.h` still said the lid wears the
throat texel, which stopped being true in U1; `interpolate_math.h` said
`InterpCamAim` was written per camera *"so that the two sides of the comparison
cannot be reduced differently by accident"*, which is precisely the cliff fix
round S took out of it (nothing in the ENGINE calls that function any more -
`InterpTest` still does, and deleting it on the strength of that sentence would
break the 269-check test; the note says both since U7); `ModelProbe`'s
`-poserisk` summary read "1 of those **are** rate 1"; and three loop bodies in
`hd_object.cpp` stood one tab short of their own braces. All corrected; no
behaviour changed by any of them.

**U6 - an honest build stamp.** `winmain.cpp` writes the first line of every
`-log` run, `PCtomb5 built <__DATE__ __TIME__>`, and those two macros are fixed
when THAT ONE FILE is compiled. In an incremental build it is not: change a
shader, or a file in `specific\hd`, and `winmain.obj` is still yesterday's - a
play test read 18:34 off a build made at 19:49. `be.bat` now deletes
`winmain.obj` and `setupdlg.obj` before `msbuild`, so the linker gets a freshly
compiled stamp every time. Two builds one after the other read
`Sep  7 2026 23:05:47` and `Sep  7 2026 23:06:17`, and each matches its own
EXE's timestamp to the second.

**U7 - what the review of round U itself found.** One of the eight findings was
a real defect and the rest were sentences that had outlived their code.

*The defect (`game\delstuff.cpp`).* **U4 switched U3 back off.** The still body
forced `frac = 0`, and zero is the test that chooses
`Rich_CalcLaraMatrices_Normal` - the branch with no `InterpArmPrev`, no
`InterpArmStack` and six weapon cases that read `lara.right_arm` /
`lara.left_arm` raw. So *First person view / Body = still* at 60 Hz put the arms
back on the logic tick in precisely the scenario U3 was written for: standing
still in first person with the HK, where drawing, aiming, firing and reloading
all keep `AS_STOP` and the hold therefore stays armed the whole time. Both
documents promised the opposite. The fix keeps the interpolated branch
reachable - the held frame goes into **both** slots with the factor 0, which
freezes the body just as hard because a blend of a pose with itself is that
pose - and `frac` is only lifted to 1 on a frame that had no pair of its own.
`GLaraShadowframe` takes the held frame with it, which is what the
non-interpolated draw did.

*The counter that counted the wrong frames (`arms=`).* Corrected as described
under U3 above: asked in the draw where the weapon is known.

*The snapshot nobody invalidated.* `InterpInvalidate` cleared every previous
state by hand except `g_prevArm`, whose `base[]` is a `short*` **into the
level's frame data** - memory `FreeLevel` returns to the `game_malloc` pool and
the next level allocates over. That is the shape of the stale pool pointer that
crashed joby3; `g_prevArm.valid = 0` now stands beside `g_prevAim.valid = 0`.

*And four sentences.* `InterpArmStack`'s note claimed the second call blends two
stacks that are already equal (true for the shotgun and the crossbow, false for
the HK - see U3 above); `ModelProbe -headw` still printed the averaged texel as
*"the texel the MIDDLE wears"* although U1b had made the middle `spokes` copies
each wearing its own rim texel; `-eye` moved the eye and the run header still
described the default placement; `be.bat`'s build-stamp comment called itself
U5. All corrected. The `-tween60` verdict is now quoted as what it is - the
whole range 0..473, of which **ANDREA1 really stores 337**, the other 137 being
the level-specific stubs - and the run prints that split itself so the next
quotation cannot drift (`animations asked for: 474 - 337 replayed, 137 stubbed
in this level`). `Preparation\Tools\README.md` also still opened the
`InterpTest` row with the pre-U count of 198 checks while ending it with 269.

**The guards.** Everything in U2, U3 and U4 is behind the existing ones:
`InterpArmPrev` answers false at `fps_mode 0`, `InterpNoteLaraStretch` runs
only with `-log`, and the still body needs `tomb5.first_person` and
`tomb5.first_person_body_still` both on. U1 is inside `hd_enable`. The thirty
hertz picture and the classic path are what they were. The new option is the
one place where a picture changes at thirty as well - it is what the option is
for - and even there nothing is blended in TIME: the held pose reaches the
interpolated branch as a pair of identical frames, `InterpArmPrev` answers
false at `fps_mode 0`, and the arm branches are the ones vanilla itself takes
whenever `frac` is not zero.

**The proof.** `be.bat` builds clean (0 errors, only the pre-existing
`camera.cpp` warning), `InterpTest` reads **269 checks, 0 failed**, `ModelProbe`
rebuilt and run for all of the tables above, and the U1 pictures were looked at
in all eight outfits. After U7 the eight first person renders and the landing
render come back **byte for byte the same files** as the ones round U was
accepted on (`cmp`), which is the proof that U7's tool edits are words and not
pictures; `-tween60` over 0..473 reproduces `worst off 0.00`, `worst leg 0.34`
on animation 205 and 313 pairs past the stored keys; and the U6 stamp holds
over the round's own builds - `Sep  7 2026 23:46:59` with its EXE written at
23:47:01, and `Sep  7 2026 23:54:57` with its EXE at 23:54:59, each stamp its
own build's and neither the one before it.

---

#### Fix round T - the rooms: the window only where it is needed, the black courtyard, the sky's hard edge, the palm without its crown (#123)

The play test of 2026-09-07, on the group M portal windows: *"the room fix
really only had negative consequences. Everything over portals does not go
over its edges properly any more."* With a photograph: the corner of the
Piazza dell'Opera in ANDREA1, where the HD pier of room 57 stands out of that
room's own volume into the alley of room 2, and a **screen-vertical strip of
sky** runs down the edge of the brick wall where the pier should be. And from
ANDREA2's courtyard, three more: the doorways are **black rectangles**, the
sky has a **hard edge** when you look up, and the **palm** is wrong.

Four findings, four fixes, and every one of them a measurement first.

---

**T1 - the window belongs to a handful of rooms, not to all of them.**

*What group M did.* It scissored every HD room to the rectangle the classic
portal walk left in `r->left .. r->bottom`. Its own note says why that is
safe, and the note is where the mistake is: the classic clip is COARSER than a
scissor. `specific\polyinsert.cpp:1213` drops a polygon only when EVERY corner
of it lies beyond one edge of the window and otherwise draws it **whole**,
clipped against the screen; a scissor cuts it at the pixel. One pixel of
margin makes the two agree for a polygon that straddles the rim by a pixel. It
does nothing at all for a wall that straddles it by two hundred.

*And that is the ordinary case at a doorway.* A room's art in its BORDER RING
- the wall's own thickness, a reveal, a corner pier - stands on the camera's
side of the portal plane, and from a shallow angle it projects far outside the
portal's rectangle. Room 57's pier sits at world x 47104..48128, and the
portal it is seen through is at x 48127: the pier is in front of the window
that was computed for it.

*Measured, with `TrgProbe -roomspace` (new), which reads the classic `.TRC`
and the `.TRG` side by side.* From the camera the play test used, the portal
window of room 57 comes out as x 328..627 of a 960 pixel picture. Drawn with
it, room 57 keeps 328..627 and loses everything else - `RoomProbe -scissor`
draws exactly that, and the two pictures are `r57_pair.png`. Drawn without it,
the wall and the pier are whole.

*What the window IS for.* Rooms that stand inside one another. ANDREA1's room
53, an air shaft, has **45 of its 272 art cells more than one sector deep
inside room 207**, with no portal between the two; from inside 207 that art
comes through the wall and the depth buffer cannot help, because both rooms
really are at that distance. Rendered: room 207 alone against room 207 with
room 53's group drawn at its own offset differ in **35,520 pixels** in the
upper right corner (`bleed_pair.png`) - that is the bug group M was built for,
and it is real.

*Telling the two apart, and each step of it measured:*

| asked of | ANDREA1 rooms that come out "overlapping" |
| --- | ---: |
| the room boxes | 233 of 242 |
| the art's own 256-unit cells | 96 |
| ... and only where no portal explains it | 82 |
| ... and only past the wall's own sector | **2** |

The last line is the rule. A room's border ring is one sector thick, so two
rooms that merely share a wall reach one sector into each other by
construction - ANDREA1's 57 and 197 at the Piazza corner do exactly that, with
60 cells of wall art - and art that reaches PAST that sector is art that is
really standing inside the room. Over the fifteen files that leaves **69 rooms
of 2,709** with a window: ANDREA1 2, ANDREA2 16, ANDREA3 7, ANDY2 10, ANDY3 2,
JOBY2 3, JOBY3 2, JOBY5 2, RICH1 14, RICH2 11, and none at all in the other
five. ANDREA1's one pair is room 53's air shaft inside room 207; rooms 39 and
41 are NOT on the list.

*And inside those rooms, the overhang is still drawn whole.* A group's
triangles are sorted into two kinds while the index buffer is filled - inside
the room's own box, or outside it - and the two never share a draw call. The
window is applied to the first and switched off for the second, which is what
the classic path does with a polygon that straddles the rim.

**The box is the engine's own** (`game\draw.cpp:2248-2253`, the lines that ask
whether an item fits wholly inside its room): `r->x + 1024 .. r->x +
(y_size - 1) * 1024`, the same in z out of `x_size`, `maxceiling .. minfloor`.
**The slack is 128 units and it is measured**: a wall face sits exactly on the
box's edge and the remaster's relief a little past it, so with no slack 73 %
of ANDREA2 is "overhang" and the window means nothing. The distances come in
two clumps - one below 64 units (a million triangles of ANDREA2 alone: the
relief) and one past a whole sector (the facades, the piers) - and 128 sits
between them and takes ANDREA2 from 77.6 % to 4.5 %.

*The code.* `HDRoomBox` and `HDRenderSetRoomBoxes` (`specific\hd\hd_render.h`)
hand the boxes in before the build, on the same contract the water marks
already had; `BuildRoomBoxes` (`hd_level.cpp:942`) fills them out of
`ROOM_INFO`. `hd_render.cpp:2313` tags every triangle and `:712` carries the
tag in `HDRange`, last in the sort key so that a level built with NO boxes
sorts exactly as it did before this round. `HDRenderGroupCellsIn` and
`HDRenderGroupOverhangTriangles` (`hd_render.cpp:3893`, `:3925`) are the two
new questions. `BuildRoomWindows` (`hd_level.cpp:1262`) makes the list once
per level; `DrawRoomWindowed` (`hd_level.cpp:551`) draws a listed room in two
pieces and every other room in one, with the scissor test off.

*The cost.* The split adds draw calls: ANDREA1 1,855 -> 2,044 ranges over the
whole level (+10 %), ANDREA2 1,687 -> 1,861 (+10 %), JOBY2 1,553 -> 1,763
(+14 %), RICH2 1,586 -> 2,092 (+32 %). A frame draws about eleven rooms, so
that is single-figure draw calls per frame. **And it changes no picture**:
ANDREA1 rooms 39, 19 and 103 rendered with the boxes handed in and without
them come back **0 differing pixels** - the split regroups triangles, it does
not add or drop one.

---

**T2 - the black doorways of the ANDREA2 courtyard.**

*Measured.* The courtyard is a stack: room 124 on the ground with two palm
statics, room 125 with the colonnade above it. Room 124's `.TRG` group holds
**four triangles, and all four of them stand outside the room**. The engine's
test for "does the remaster have art for this room" was `tris` non-zero
(`hd_level.cpp`), so room 124 counted as HD, its classic roomlets were dropped
- and four triangles outside the room is nothing at all inside it. `RoomProbe`
draws room 124 in three ways: whole, 1 draw call; the inside half, **0 draw
calls**; the overhang half, 1. The picture is `mean 0.0 of 255` in every one
of them, which is the black rectangle of the play test.

*How many rooms are like that.* **Nine** in the whole game under the rule as it
ships - the one with `HD_OVERHANG_SLACK` in it - and they are named: ANDREA2
124, JOBY2 19 and 21, RICHCUT2 2, 3 and 4, TITLE 70, 101 and 103. `RoomProbe
-roombox <classic folder> -window inside` draws all nine at **0 draw calls**.
A tenth, ANDREA1's room 4, is hollow only with the slack taken away: its 216
triangles all stand within 128 units of the box edge, so the shipped rule
leaves that room HD (1 draw call). Nothing else in the fifteen files comes
close - the next-thinnest rooms have 3 to 31 triangles genuinely inside them.

*The fix.* A fourth reason for a room to stay classic, beside "no group", "an
empty group" and the randomizer's marker: **a HOLLOW group, one with no
triangle inside its own room** (`hd_level.cpp:2025`). The classic geometry of
such a room is the only geometry there is, and it is drawn. `HDLOG` says
`reason=hollow_group` and carries `overhang=` on every room line.

---

**T3 - the hard edge in the sky.**

*Measured, page by page.* ANDREA2's sky group is three parts: a flat cloud
sheet in the OPAQUE pass on page 7109, a second sheet in the ALPHA pass on
page 7101, and the painted dome, material 22, in the alpha pass on page 7003.
`DdsProbe -alpha` over the three:

| page | drawn in | alpha under 16 | alpha over 240 | mean |
| --- | --- | ---: | ---: | ---: |
| 7109 | opaque | 0.0 % | 100.0 % | 254.9 |
| 7101 | alpha | 42.0 % | 35.9 % | 122.7 |
| 7003 | alpha | 33.5 % | 59.2 % | 160.5 |

7109 is opaque and is drawn opaque, correctly. The other two carry a **soft
ramp with every value in between** - and the alpha pass is an alpha TEST at
0.5 (`hd_render.cpp`, `gPass.x`), made for the cut-out materials where a texel
is either wholly there or wholly gone. What is left of a feathered cloud after
a test at 0.5 is a hard-edged silhouette. `RoomProbe -skyonly` draws it:
jagged cloud outlines and a hard rim across the dome.

*The fix.* The SKY group's alpha pass blends instead of being cut
(`hd_render.cpp:3245`): source alpha against one minus it, alpha reference 0,
and **the depth write stays on**, unlike the glass of fix round H2 - the sky
sorts inside itself in its own depth slice (`HD_SKY_MINZ`), the dome stands in
front of the sheets at the horizon and behind them overhead, and taking the
depth write away would lose that order. Nothing outside the sky can be
touched: every pixel of the world is nearer than the slice. Only a group past
the last room takes this branch, and `HDRenderRoomMask` is the one place that
still knows the group's number.

*The picture.* The same camera before and after: **243,897 of 518,400 pixels
change**, the jagged outlines become feathered cloud, and the dome's rim is
gone (`sky_pair.png`). The state bracket over the sky call reports **0 of 59**
changed.

*Not adopted, and why.* TROne's own sky finding is a `CLAMP_TO_EDGE` sampler
for the painted dome (`game_renderer.cpp:1256-1269`), against a tiling seam.
We already clamp: `hd_render.cpp` sets `D3DTADDRESS_CLAMP` for every range
that does not scroll and `WRAP` only for the ones that do, and D3D9's CLAMP is
GL's `CLAMP_TO_EDGE`. So that part was in place before this round and the edge
was somewhere else.

---

**T4 - the palm with no crown.**

*Measured.* A TR5 palm is two statics: the trunk (slot 8) in the ground room
and the crown (slot 9) in the room above. Fix round R3b takes a static away
when the `.TRG` draws ANYTHING in its box, and over the fifteen files that
question is answered on very little evidence in a few places:

| art in the static's own box | statics |
| --- | ---: |
| nothing at all - drawn classic | 271 |
| more than nothing, under a twentieth | **44** |
| a twentieth or more | 2,286 |

ANDREA2's courtyard is in the middle row. Room 125's crowns catch **4 cells of
768** - the wall behind them, not a palm - and were suppressed with nothing in
the `.TRG` to put in their place, while room 124's trunks below them catch 0
and are drawn classic. A trunk with no fronds. The same level's room 162 crown
catches 313 of 768 and really is in the art. (`TrgProbe -statics` splits the
group's cells into an interior and a shell list and counts a static's box
against both, so a cell that both lists hold is counted twice - it reads 380
for that crown at `-slack 0` and 355 at `-slack 128`. The engine walks ONE
deduplicated list, which is the 313; the classification of all 2,601 statics
comes out the same either way.)

*The fix.* The question is now how MUCH, against the static's own box: art
must cover **a twentieth** of it before the classic draw is suppressed
(`hd_level.cpp:1462`, `HDRenderGroupCellsIn`). The threshold sits in the gap
the measurement found rather than through a crowd; the 44 statics of the
middle row go back to being drawn the classic way and the 2,286 do not move.
`HDLOG static ... classic cells=%d of %d` now says why for each of them.

*Not adopted.* TROne's `.MAP` `DUMMY` positive family list
(`docs\PORTING_tr5.md:780-880`) is about MOVEABLES that the remaster baked
into the room art, not about statics, and nothing measured in this round
touches it. It stays on the list of things to port with a measurement of its
own rather than being taken on trust.

---

**What this round did NOT change.** The classic path. The diff is three files
under `specific\hd` and two diagnostic tools, and `HDLevelBuild` returns at
`HDEnabled()` before it reaches any of it - with `hd_enable` off no room box
is built, no overlap list is made and no group is split. `be.bat` builds with
**0 errors**; `RoomProbe`'s state bracket reports **0 of 59** watched states
changed in every run of this round, room draws and sky draws alike.

**The tools.** `TrgProbe` gained `-roomspace` (the room boxes, the overlap
list, the overhang histogram by distance and by side, and the hollow groups),
`-room <n>` (one room in full: box, portals, vertical links, whose art stands
inside it), `-statics` / `-slot <n>` (every static mesh with its box and how
much art is in it), and `-share` / `-slack` / `-deep` to move the three
thresholds and see what they cost. `RoomProbe` gained `-roombox <folder>`,
which hands the renderer the same boxes the engine hands it, and `-window
all|inside|overhang`, which draws one half of a group - which is how "room
124's art is entirely outside room 124" was turned into a picture and a draw
call count.

#### Fix round T2 - the rooms again: the room that vanishes for one frame, the window only on the art that intrudes, and the tower that pops in (#123, #148)

The play test of 2026-09-08, at sixty frames a second in third person,
brought three things back from the same corner of Rome:

* **the sky flashes through for ONE frame.** ANDREA1, the alley off the
  Piazza dell'Opera, Lara in a jump at the wall: for a single frame the
  whole lower room is gone and the painted city panorama stands where
  the pavement and the foot of the brick wall should be, the hole's
  edges dead straight along the vertical portal line. *"Die Skybox kommt
  sporadisch fuer ein Frame ins Bild ... der Schnitt verlaeuft an der
  vertikalen Portallinie."* With two controls of his own: **at frame
  rate 30 it is gone**, and **in first person at sixty it never
  happens**.
* **a horizontal BLACK BAND** across ANDREA2's courtyard at the boundary
  of rooms 134 and 203, cutting the palm's trunk and the door lintels in
  half. At 30 "a little better, but still there".
* **a brick tower with a barred window** at the same corner of ANDREA1
  that is not in trview at all - pure remaster art - missing from a
  distance and popping into the picture as she walks up to it.

---

**T2-1 - the portal walk started in the wrong room on every in-between
frame.** (The whole of it, with the offline proof, is also written out
in `Preparation\60FPS-Plan.md` under *Order 2, the sixth thing*.)

*What was wrong.* On a tween frame of the sixty hertz cadence the EYE is
blended and the view matrix rebuilt from it (`game\interpolate.cpp`, the
camera block), while `DrawRooms` was handed `camera.pos.room_number` -
the LOGIC camera's room, which steps once per tick. Where the two
disagree, two pieces of arithmetic in the walk throw a neighbour away:

* `GetRoomBounds` (`game\draw.cpp:2562`): `if (x && (x ^ p->normal[0]) <
  0) SetRoomBounds(...) else continue;` follows a portal only while the
  eye is on this room's side of its plane.
* `SetRoomBounds` (`game\draw.cpp:2415`): `if (tooNear == 4) return;`
  drops a portal whose four corners all fall behind the eye.

Either one leaves the near room drawn alone with the sky behind the
hole, and the hole's edge is the portal's own straight line - which is
the photograph.

*And it explains both of the player's controls, which is what makes it
the right answer and not a plausible one.* At `fps_mode 0` no frame is
ever drawn from a blended eye, so the eye and the room can never
disagree. In first person the eye rides on Lara's head inside her own
room and hardly ever crosses a portal plane between two ticks; the chase
camera swings around corners in whole sectors and crosses them
constantly.

*Measured offline*, on ANDREA1's own portal records read out of the
shipped level with `TrgProbe -roomspace -room 2` and `-room 57`
(`InterpTest`, new section `portal walk`, 13 checks, 282 in the tool
altogether and 0 failed):

| | |
| --- | --- |
| room 2's portal to room 57 | world x 48128, normal -1/0/0 |
| the camera on two ticks | x 48000 (room 2) and x 48400 (room 57) |
| the blended eye between them | x 48200 |
| seeded with room 2 | the portal to 57 is **refused** |
| seeded with room 57 | the portal back to 2 IS followed |
| over the whole interval | refused from 32 % of the way across, 69 of 101 sampled positions |
| looking east from the blended eye | `tooNear == 4` - dropped by the second test as well |

*The fix.* `InterpEyeRoom` (`game\interpolate.h`, `interpolate.cpp`): on
a frame whose camera really was blended, the room that CONTAINS the
interpolated eye - `GetFloor(x, y, z, &room)` walked from the logic
room, which is the engine's own answer to that question and the very
call `game\camera.cpp` makes at every tick (`:1424`, `:2103`, `:2146`,
`:2296`, `:2654`). `DrawRooms` (`game\draw.cpp:1403`) seeds the walk
with it. Nothing is written back: `camera.pos.room_number` keeps the
logic room, because half a dozen callers outside the draw read it.

*What it deliberately does NOT move.* `outside` and `camera_underwater`
are still read from the LOGIC room. They are the frame's own flags
rather than a property of the room list, the live frames on either side
of a tween one read them from the logic room, and a tween frame that
took them from a room half a step away could put one frame of
underwater tint or one frame of weather into a run that has neither.
`outside` is widened by the walk in any case - `GetRoomBounds` sets it
from every `ROOM_OUTSIDE` room it reaches.

*And `g_applied` is the gate*, not the tween latch. A cutscene frame and
the first frame after a level start are tween frames of the cadence and
are drawn LIVE, with `camera.pos` untouched; asking `GetFloor` about a
live camera would answer with whatever room really holds it, which need
not be `camera.pos.room_number`, and the picture would change at thirty
frames a second too. `g_applied` is true only where the camera block
really ran.

*The note that said otherwise is corrected in place*
(`game\interpolate.cpp:1571`). It read *"a half step across a portal
would flip the room list and the picture with it"* - and the flip is the
RIGHT room list once the eye has crossed.

*The seam, and it stays in the build behind `-log`.* What this repairs
lasts one frame in sixty, and the per-frame HD log line is throttled to
one a second on purpose - so the frame that flashes is exactly the frame
the log does not write, which is why the play test's 41 `INTERPLOG`
lines from ANDREA1 could not say whether the sky came through.
`ROOMFLASH` (`game\draw.cpp`, `NoteRoomWalk`) keeps three frames of the
room list - three notes of 244 bytes - and names any room that is in the
list on the frame before and the frame after and not on the one in
between. That
shape is a hole whatever made it, and it needs no knowledge of portals
or cameras. Beside it `ROOMWALK` says once a second `corrections=N of M
tween frames`, so a run in that alley with `corrections=0` would mean
the fix never fired and the flash came from somewhere else - which is a
thing worth being able to tell apart.

---

**T2-2 - the window belongs to the intruding TRIANGLES, not to both
rooms.**

*What round T got half right.* It measured which pairs of rooms really
stand inside one another and left every other room alone - and then gave
the portal window to BOTH partners of a pair, and to the whole of each
partner's art. ANDREA2's courtyard is a stack: room 134 on the ground
with the palm and the doorways, room 203 with the colonnade above it.
The play test's own log says both are HOLDERS - `overlap room=134 holds
the art of room=143` and `overlap room=203 holds the art of room=248` -
so both were scissored, and their two rectangles meet along the
courtyard's own room boundary. That is the black band.

*The holder never needed a window at all.* Nothing of ITS art is
standing anywhere it should not be.

*First, the discrepancy the play test exposed, because it had to be
explained before the rule could be changed.* The engine counted MORE
overlapping pairs than the tool did on the same level: ANDREA1 4 pairs /
8 rooms in the log against `TrgProbe -roomspace -slack 128 -share 16
-deep 1024`'s ONE, ANDREA2 22/29 against 10/16. Cause, found and then
confirmed by a control run: **the two asked the cell test differently**.
`HDRenderGroupCellsIn` rejects a cell only when `p + 255 < mins`, i.e. it
counts a cell that merely TOUCHES the box; the tool counted a cell whose
own corner lay inside it. One 256-unit ring of difference. Run with the
box a ring larger (`-deep 768`) the tool reproduces the engine's four
ANDREA1 pairs exactly - 65/103, 93/101, 149/209, 207/53 - and 78 cells
against 45 in room 53's air shaft. Neither reading is wrong; they are
two different rules, and that is the bug.

*So the question is asked of a POINT, where the two cannot differ.* The
rule now is one sentence:

> a triangle of room b is windowed when its CENTROID stands inside the
> inside of some other room a - and nothing else in the game ever is.

`a`'s inside is TR5's own (`game\draw.cpp:2248-2253`, the lines that ask
whether an item fits wholly inside its room) pulled **one whole sector
further in** on each side, with `a` not next door to `b` by portal or by
a vertical floor link, and not its flipmap twin.

*Every part of that measured:*

| how far in the holder's box is pulled | ANDREA1 pairs | ANDREA2 pairs |
| --- | ---: | ---: |
| the plain inside (the wall's own sector counted) | 158 | 148 |
| half a sector further | 25 | 49 |
| **one sector further - what ships** | **7** | **26** |

At the plain inside, ANDREA1 comes out with `room 2 holds the art of
room 197` - the Piazza corner the play test of 2026-09-07 complained
about, two rooms that merely share a wall. A room's border ring is one
sector thick, so that is by construction, and one sector further in is
what tells the two apart.

*Tried and rejected, and it is worth writing down because it looks
right:* deciding the PAIR with the strict box and then windowing every
triangle of `b` inside `a`'s plain inside. It would have hidden the last
26 % of ANDREA1's 207/53 bleed - and it windows 7,250 triangles of room
103 inside room 65, which is that pair's SHARED WALL and is round T's
own bug coming back through the other door. What is left un-hidden is
art of the intruder standing within one sector of the holder's wall, and
there is no way to tell that from a shared wall with a box.

*No count threshold*, unlike round T's sixteen cells. That floor existed
because the answer was all-or-nothing for a whole room; now the answer
IS the triangle, so a stray one costs a stray one.

*What the whole game comes to:* **105 pairs, 81,261 triangles in 90
groups of 2,709** - 0.54 % of the game's 15,052,011 triangles, and the
only draw calls in it the scissor test is ever on for. ANDREA1 7 pairs /
1,067 triangles, ANDREA2 26 / 2,571, ANDREA3 11 / 9,989, ANDY1 3 / 789,
ANDY2 16 / 59,766, ANDY3 2 / 81, JOBY2 4 / 2,037, JOBY3 2 / 36, JOBY4
5 / 45, JOBY5 2 / 210, RICH1 18 / 3,245, RICH2 9 / 1,425, and nothing at
all in RICH3, RICHCUT2 and TITLE.

*The tool and the engine now answer with THE SAME LIST, and that is
checked rather than asserted.* `RoomProbe -roombox <classic folder>`
builds the intrusion list out of the `.TRC` and hands it to the engine's
own renderer, which prints what its build made of it; `TrgProbe
-roomspace` answers the same question out of the two files with no
renderer at all. Held against each other the two listings are
**identical, line for line** - ANDREA1's 7 pairs, ANDREA2's 26, ANDY2's
16 and RICH1's 18 - and the summary line agrees on all fifteen levels,
pairs, triangles and groups alike.

Getting there took two corrections OF THE TOOL, and both are worth
naming because either would have left a plausible-looking table that was
not the engine's:

* it averaged the three vertices in WORLD units with an integer divide
  where the engine averages them as floats in the group's own frame. The
  two then differed by one triangle in ANDREA1's 207/53 and seven in
  ANDREA2's 58/175 - a truncation against a box edge.
* it SUMMED the per-pair counts per group. A triangle can stand inside
  two holders at once, and ANDY2 is full of them: the sum read 70,934
  where the engine's own counter, which counts the triangle once however
  many boxes caught it, reads 59,766.

*The code.* `HDIntrusion` and `HDRenderSetIntrusions` /
`HDRenderIntrusionHits` / `HDRenderGroupIntrudingTriangles`
(`specific\hd\hd_render.h`) hand the boxes in before the build on the
same contract the room boxes already had. `BuildRoomIntrusions`
(`hd_level.cpp:1868`) makes the list out of `room[]` alone - boxes,
portals, floor links - which is why the tool can reproduce it;
`ReportRoomIntrusions` (`:1985`) reads the counts back afterwards and is
where `HDLOG level=%s overlap pairs=%d intruding tris=%d in %d groups`
comes from. `hd_render.cpp:2716` tags the triangle and `:925` carries
the tag in `HDRange`, last in the sort key so that a level built with NO
list sorts exactly as it did before group M. `DrawRoomWindowed`
(`hd_level.cpp:563`) draws the open half over the whole screen and the
intruding half inside the window.

*Three more things fell out of it and are fixed in the same breath:*

1. **The flag is per GROUP now** (`g_group_windowed`), and `HDFrameRoom`
   asks it of the group that is really drawing the room. Round T kept it
   per ROOM - so after a flipmap a windowed room's art could be drawn
   with no window at all and its twin's art cut to a rectangle that was
   never measured for it.
2. **An empty window no longer silences a whole room.** Group M skipped
   a room whose window came out with no width or height, because the
   window WAS the room; now only the intruding half is silenced, inside
   `DrawRoomWindowed`.
3. **The overhang keeps no draw calls of its own.** Round T sorted every
   group in the game into an inside half and an overhang half so that
   the two could be scissored differently; with the window a property of
   the triangle they are both drawn over the whole screen, so the split
   is gone and the draw calls come back. Measured, whole level, all
   passes:

   | level | no list at all | round T | **round T2** |
   | --- | ---: | ---: | ---: |
   | ANDREA1 | 1,859 | 2,044 | **1,874** |
   | ANDREA2 | 1,702 | 1,861 | **1,743** |
   | JOBY2 | 1,553 | 1,763 | **1,563** |
   | RICH2 | 1,586 | 2,092 | **1,602** |

   Round T cost 10 to 32 %; what is left is 15 to 41 draw calls per
   LEVEL, which is the intruding split's own cost and nothing else. Per
   frame, the ANDREA2 courtyard scene draws 29 calls where round T's
   rule drew 32. The overhang is still counted per triangle, so the
   hollow-room test of round T's T2 - a group with nothing at all inside
   its own room, which is what made ANDREA2's room 124 a black hole - is
   untouched.

*The pictures.* All from `RoomProbe` with the room boxes and the
intrusion list handed in exactly as the engine hands them, camera inside
ANDREA2's room 134 in the courtyard, rooms 134, 203, 143 and 248 drawn at
their own offsets; the "before" is round T's own code path, put back for
one run by `HDRenderSplitOverhang` and driven by the new `-tround
<group> <l,t,r,b>` - the two calls round T's `DrawRoomWindowed` made,
with the rectangle worked out from the ceiling portal's own corners
through the engine's own `SetRoomBounds` arithmetic.

* `palm_before.png` / `palm_after.png`: the whole upper band of the
  colonnade is missing along a dead straight horizontal edge, and the
  palm crown is seen floating through the gap. **58,026 of 518,400
  pixels** differ (`palm_diff.png` - a band whose bottom edge is exactly
  the scissor rectangle's top edge).
* `court_before.png` / `court_after.png`, a second camera: **75,655
  pixels**, background 51.3 % against 39.9 %.
* ANDREA1's 207/53, the nesting the window exists for, camera inside
  room 207: room 53's art drawn whole against room 207 alone differs in
  **27,694 pixels** - that bleed is real and is what group M was built
  for. Withhold the tagged triangles alone (`-window open`) and **20,601
  of those 27,694** go with them; the 7,093 that remain are room 53's
  own wall standing in room 207's border ring, which is the trade the
  rejected variant above was measured against.
* ANDREA1's room 57, the pier at the Piazza corner that started round T:
  group 57 is not on the list at all and is never windowed, so it draws
  whole.

Every `RoomProbe` run of this round reports **0 of 59** watched device
states changed.

---

**T2-3 - the tower: measured, and NOT fixed on a guess.**

*What was asked for* was a fix in one of two shapes: widen the S8 culling
to the group's own box, or add an HD neighbour rule for groups whose
overhang stands in the frustum. The measurement says neither applies,
and says so with numbers:

1. **The HD path has no visibility test of its own.** There is no view
   frustum anywhere in `specific\hd` - grepped for `frustum`, `cull` and
   `reject`, and every hit is BACK FACE culling on Lara's model - and the
   `dropped=` of its per-frame log line counts only the fixed frame
   array's overflow, which read **0** through the whole play test. So the
   rooms it draws are exactly the rooms `DrawRooms` hands it. There is
   nothing to widen.
2. **There is no remaster art hung far outside a room box at that
   corner.** `TrgProbe -near <x> <y> <z> <radius>` (new) holds every
   group's triangles against a sphere in world units and says how far
   each is from its OWN room's box. Within 8,000 units of the corner
   where ANDREA1's rooms 2, 57 and 197 meet (world 48128 / -3000 /
   37888), the worst any group reaches past its own room is **936 units**
   - under one sector, room 103's facade. Sampled again at 6,000 units
   around y -8000, -14000 and -20000 - the height a tower would stand at
   - the answer is 572 units and then nothing at all. The tower is
   ordinary art of some room, not something hung outside one, so a rule
   about overhang boxes would not reach it.

*What is left are two ordinary things, and they cannot be told apart
without the game:* the room the tower belongs to is not in `draw_rooms`
because the portal chain does not reach it from the alley (vanilla
behaviour, invisible in vanilla only because vanilla has no tower
there), or it is past the FAR PLANE, which `InitWindow` sets at 20480
units - twenty sectors - and which both paths project with
(`specific\gamemain.cpp:261`).

*So this round adds the line that will name it in one play test rather
than a change made on a suspicion.* `ROOMPOP` (`game\draw.cpp`,
`NoteRoomWalk`, `-log`): a room that JOINS the draw list while it is
more than eight sectors away, with the room number, the distance, the
eye and the far plane, at most four lines a second. If the tower appears
on a frame that also joins its room, it is the walk; if the room was
there all along, it is the far plane. `ROOMWALK`'s per-second line
carries the count as `far rooms joined=`.

---

**What this round did NOT change, and the one thing it does change
outside the HD path.** At `fps_mode 0` nothing moves at all, in either
path: `InterpAlphaNow` hands 1 in, `InterpBeginFrame` returns before it
writes anything, `g_applied` stays false and `InterpEyeRoom` therefore
returns its argument, so `DrawRooms` is line for line the function it
was. T2-2 is the HD path alone - `HDLevelBuild` returns at `HDEnabled()`
before it reaches `BuildRoomIntrusions`, so with `hd_enable` off no list
is built and no triangle is tagged. T2-1, on the other hand, is in the
walk BOTH paths share, so at sixty frames a second it corrects the
classic picture as well as the HD one - deliberately, because the flash
is the shared walk's and not the renderer's. The three new log lines are
all behind `gLogEnabled` and write nothing else. `be.bat` builds with
**0 errors**.

**The tools.** `InterpTest` gained the `portal walk` section (13 checks,
282 in all, 0 failed). `TrgProbe -roomspace` now answers the overlap
question the way the engine does, per triangle and per centroid, and
gained `-near <x> <y> <z> <radius>`. `RoomProbe -roombox` now hands the
intrusion list in as well as the room boxes and prints the engine's own
overlap listing for it, `-window` takes `all|open|intruding`,
`-clipgroup <g> <l,t,r,b>` puts a scissor round ONE group (which is what
a portal window really is, and what `-scissor` could not do), and
`-tround <g> <l,t,r,b>` draws one group the way fix round T drew it,
which is the "before" of every picture above.

#### Fix round T3 - the rooms a third time: the black room behind the torn wall, the tower of Trajan's Markets, and the seams that name the refused portal (#123, #148)

The play test of 2026-09-08 - the T2 build stamped 19:50:31, sixty frames a
second, third person - came back with two things and a bisect, and this round
is almost all measurement. Nothing in it changes a picture: the two engine
changes are both inside `NoteRoomWalk`, which returns at its first line
without `-log`.

* **JOBY2, the "broken wall".** An edge that is solid in the classic level is
  see-through in HD - the remaster's art has a torn-open wall there - and
  through the hole the room behind is **black**. *"Raeume die weiter als 1 weg
  sind bekommen ein schwarzes Portal."* Its history: under group M a black
  STRIPE along the tear, straight, the scissor edge of the room behind; since
  T2 the whole room behind, from some angles. And the player's own bisect,
  which is what makes this round possible: *"Der Bug in Joby2 war frueher
  anders. Mehr wandbasiert als portalbasiert."* - **the wall was see-through
  before any room fix existed.** This is not a regression of T2 or of group M.
* **ANDREA2, the tower.** *"Der Turm wird zu spaet geladen und ploppt im
  Sichtbereich auf."* Bisect: before T2 the tower was never drawn at all
  (*"Der Turm war nie da. Jetzt ist er erst gekommen"*) - T2-2, which stopped
  an empty window silencing a whole room, is what made it visible; now its
  room joins the draw list late.
* And the log itself: 54 `ROOMFLASH`, 371 `ROOMPOP`, 226 `ROOMWALK`, 265
  `INTERPLOG` lines over 22,421 lines of JOBY2.

---

**T3-1 - the seams. What refused the portal, and what the list is.**

*The tool first, because everything below is measured with it.* `RoomProbe
-walk` runs THE CLASSIC PORTAL WALK OFFLINE, out of the classic `.TRC` alone -
no device, no `.TRG`, no picture - with `GetRoomBounds` (`game\draw.cpp:2939`)
and `SetRoomBounds` (`:2795`) carried over line for line and every number
given a name. It takes an eye and a seed room and answers over a fan of view
directions, because the `ROOMFLASH` line has no camera angle in it. It
reproduces the play test's own lines: seeded in JOBY2 room 114 at the eye of
frame 1624, `40024/-3408/24107`, at yaw 272 pitch -10 it draws **114, 123, 124
and nothing else** - `rooms=3`, which is what the log says - and room 99, which
the log says went missing, is refused there with `empty [2559 0 2559 687]`. The
angle has to be named, and that is the whole point: at that one eye the list
runs from 1 room to 5 as the view turns, and the log line the tool is being
matched against carries no angle at all. That is what T3-1 adds to the seam.

*And the two engine seams, both behind `-log`, both dead without it.*

`ROOMFLASH` (`game\draw.cpp`, `NoteRoomWalk`) now carries the eye THE WALK
REALLY USED - `w2v_matrix[M03..M23]`, which on a tween frame is the blended
eye while `camera.pos` is half a step behind it, so the old line printed the
wrong one of the two - beside the logic camera, the target and its room, and
`camera.type / old_type / fixed_camera / number`. Then one line per missing
room saying WHAT DROPPED IT, worked out from the note itself (`WhyRoomMissing`)
and not from the walk, which is neither read nor slowed:

| answer | the line of `draw.cpp` it belongs to |
| --- | --- |
| `side` | `if (x && (x ^ p->normal[0]) < 0)`, `:3010` - the eye is on the far side of the portal's plane |
| `toonear` | `tooNear == 4`, `:2863` - all four corners behind the eye |
| `empty` | the rectangle came out with no width or no height after the clamp |
| `noportal` | no portal to it out of ANY listed room |

`SetRoomBounds`' fifth exit - the early out at its top, where the room's window
already covers what this portal would give it - is deliberately not among them:
it only ever fires for a room that is in the list ALREADY, and this line runs
on missing rooms alone. `RoomProbe -walk`, which judges every portal and not
only the missing ones, does report it, as `covered`.

For that the note keeps each listed room's own window as well - `PrintObjects`
(`:1087`) wipes all four at the end of the frame, so a frame judged one frame
later has to have kept them. It keeps `left..bottom`, the UNION of every window
a room was dequeued with, so the answer is the CONSERVATIVE one: it can say
`reached` where the walk said `empty`, never the other way about.

`ROOMLIST` says what the draw list IS. One line whenever its membership
changes, with `added=` and `removed=`; and every five seconds the whole list,
each room with its window rectangle, its distance and its `outside` flag -
**and its SHADOW: that room's portal neighbours that are NOT drawn.** That
shadow list is the line this round exists to add. A player standing at the
torn wall and seeing black is standing in a room whose shadow list holds the
room he cannot see, and the `ROOMFLASH ... why` line beside it says whether it
was refused by an empty window or was never reachable at all.

*The size was measured before the line was written, because a seam that fills
a log is a seam that gets switched off.* `RoomProbe -walk` over a full turn on
the spot at one degree a frame - a pan of sixty degrees a second at sixty
frames - from the play test's own eyes:

| | JOBY2 room 130 | ANDREA2 room 210 |
| --- | ---: | ---: |
| frames with any membership change | 16 % | 16 % |
| rooms added-or-removed per frame | 0.24 | 0.33 |

So about ten lines a second while the camera is swinging hard and none at all
while it is still: at 110 bytes a line, ten minutes of nothing but hard panning
is about **640 KiB**. The cap of 20 lines a second is the safety valve for a
case worse than the one measured - it holds ten minutes to 1.3 MiB, and the
five second dump of a forty room list adds 340 KiB, so **under 2 MiB either
way**.

---

**T3-2 - JOBY2. Three things it is NOT, and the one thing it is.**

**(a) The one-frame dropouts are not it, and they cost the rim of the
picture.** All 54 `ROOMFLASH` lines were put back through the offline walk -
69 (eye, missing room) cases in all, each over 648 view directions:

* **0 of 69** are unreachable. Every room the log calls missing IS reachable
  from that same eye at some view direction; not one is a structural hole.
* **17 of 69** enter the picture at their narrowest as a window **one or two
  pixels wide**. They are slivers at the rim.

And the case the log names twice, room 99 seen from room 114, taken to the
pixel. Sweeping the view direction at the logged eye, the flip is at yaw 274 of
a 2560x1440 picture: at 272 the room is refused with `empty [2559 0 2559 687]`
- a rectangle of **zero width against the right edge** - and at 274 it is
reached with a window of `2538,0..2559,697`, **21 pixels of 2560**. Drawn and
not drawn at that exact eye and angle, with rooms 114, 123 and 124 at their own
offsets, the two pictures differ in **455 of 3,686,400 pixels - 0.012 %** - all
of them inside a 256 x 761 strip at the right screen edge
(`flash_with_JOBY2_r114.png` / `flash_without_JOBY2_r114.png`). That is what a
`ROOMFLASH` costs. It cannot be a black room.

**(b) The window - group M's or T2's - is not it either.** The window only ever
cuts art of a room that is BEING DRAWN; a room that is not in `draw_rooms` is
never handed to `HDFrameRoom` at all. JOBY2 has four intrusion pairs (`24`
holds `31`, `24` holds `33`, `38` holds `71`, `53` holds `54`, 2,037 triangles
in 4 groups - the engine's own count in the play test's log). From an eye
inside each holder, over 120 view directions each: the intruder is in the draw
list **0 times**, and `reached=0 side=0 toonear=0 empty=0` - the walk never
even looks at a portal to it, which is what "no portal between them" means.
Whatever the tear shows, no window decided it.

**(c) What is left is the only thing left, and the player's bisect says the
same.** The HD path has no visibility test of its own - fix round T2 grepped
`specific\hd` for it and the frame line's `dropped=` has read 0 in every play
test since, including this one - so the rooms it draws ARE `draw_rooms`. The
remaster's art opens a wall that the classic level keeps shut; the classic
portal graph has no portal through that wall; the walk therefore never lists
the room behind it, and the clear colour stands in the hole. That is a wall
bug, not a portal bug, which is exactly what the bisect said before this round
began.

**And so no rule was built.** The shape asked for was a "reveals" list keyed on
the overhang, and the measurement refuses it twice: a tear is an ABSENCE of art
and has no overhang of its own to key on, and the one list that does describe
"b's art stands inside a with no portal between them" is the intrusion list -
whose whole purpose is to HIDE that art, measured in round T2 as 27,694 pixels
of bleed in ANDREA1's 207/53. Drawing the intruder full screen would put that
bleed straight back. The right key is not knowable until the play test names
the room, and `ROOMLIST`'s shadow line names it in one run.

**ANSWERED IN FIX ROUND T4, and not with a list.** The room the shadow line
would have named is JOBY2's room 99, and what keeps it out of the picture is
not a missing portal but the CLAMP: `SetRoomBounds` cuts the child portal
124 -> 99 against room 124's own window - the projection of the classic
doorway - and room 99's doorway begins six pixels to the right of where that
window ends. T4 gives every room a second answer, the REACH, which is the
portal's own projection against the screen with the parent left out of it, and
the room behind the tear joins the draw list wherever its portal is on the
screen. The classic window stays exactly what it was and stays the scissor, so
the 207/53 bleed above is untouched. See *Fix round T4*.

---

**T3-3 - ANDREA2. The tower is room 222, and it is the walk's reach.**

**(a) Which room.** `RoomProbe` from the play test's own eye cluster (world
about `82000 / -1500 / 41000`, which is the vertical stack of rooms 0, 210, 48
and 8 at `x 79872..84992, z 38912..45056`), looking at each candidate of the
`ROOMPOP` list in turn: **room 222** carries it. The picture
(`a2t_222_ANDREA2_r222.png`) is the photograph - a brick block with a small
BARRED WINDOW high on its face, standing on a pier, with a Greek-key MEANDER
frieze along the ledge beneath it. Room 222 is `x 69632..77824, y -4096..-2048,
z 26624..32768`, **15,221 triangles of which 3,619 stand outside its own
classic box**, and it is `ROOM_OUTSIDE`.

**(b) Why it is late, with the numbers.** It is **9,246 units** from that eye,
against a far plane of 20,480 (`InitWindow`, `specific\gamemain.cpp:261`), so
the far plane has nothing to do with it. Over a fan of 360 view directions at
the eye - 72 yaws a step of five apart, at the five pitches -30, -15, 0, +15
and +30 - it is in the draw list on **17** of them, and the refusals are
`empty` and `side`, never `toonear`. A second, finer fan of 120 yaws a step of
three apart, taken pitch by pitch, says where those seventeen sit:

| pitch | room 222 in the list |
| ---: | ---: |
| -30 deg | 16 of 120 |
| -25 | 12 of 120 |
| -20 | 9 of 120 |
| -15 | 7 of 120 |
| **-10 to +10** | **0 of 120 at every one of -10, -5, 0, +5, +10** |
| +15 | 6 of 120 |
| +20 | 8 of 120 |
| +25 | 9 of 120 |
| +30 | 0 of 120 |

**Looking level, straight at where the tower stands, the walk never reaches its
room at all.** It comes in only when the eye is tilted enough for the chain
through the intervening rooms to open. That is the popping, and it is the
portal walk's reach and nothing else.

*The picture of what is lost*: from the eye, yaw 216, pitch -12, the rooms the
walk does reach drawn at their own offsets, against the same plus room 222 -
**4,011 of 589,824 pixels, 0.68 %**, in a 49 x 250 strip where the tower's near
face and the top of its window stand (`tower_before_ANDREA2_r210.png` /
`tower_after_ANDREA2_r210.png`).

**(c) And the rule was NOT built, because the measurement priced it.** The
shape asked for was "`ROOM_OUTSIDE` rooms whose group box is in the frustum and
within N units of the eye, drawn full screen without continuing the walk".
Measured at that eye:

| | |
| --- | ---: |
| rooms the walk reaches over the whole fan | 65 |
| rooms per frame | 3 to 40 |
| `ROOM_OUTSIDE` rooms in ANDREA2 | 33 of 252 (13 %) |
| ... within 16 sectors of this eye | **25** |
| ... of those, never reached at any direction | 6 |
| rooms of any kind within 16 sectors and never reached | 68 |

The wide reading adds up to 25 rooms to a list of 3 to 40; the narrow one -
only the outside rooms the walk never reaches - still adds 6, which on the
courtyard scene's measured 29 draw calls is over the 20 % the brake allows. And
both need a VIEW FRUSTUM that the HD path has not got: round T2 grepped
`specific\hd` for `frustum`, `cull` and `reject` and every hit is back face
culling on Lara's model. Building one is a round of its own with its own
correctness burden, not a small rule. So this round measures and reports.

**REPLACED IN FIX ROUND T4.** The player's answer was that it had to be a
global fix rather than a list of rooms, and the measurement above pointed at
the right place without naming it: the refusals are `empty` and `side`, never
`toonear`, and `empty` IS the clamp against the parent's window. T4 leaves
that clamp where it is for the window and asks a second question for the draw
list - does the portal face the eye and land on the screen - which needs no
frustum, no `ROOM_OUTSIDE` rule and no distance. Room 222 is in the list at
every level view direction that has it in front of the camera: 0 of 120 yaws
at pitch -10, -5, 0, +5 and +10 becomes 34 to 36 of 120. See *Fix round T4*.

---

**What this round changed, and what it deliberately did not.** Two files.
`game\draw.cpp`: the walk note grew the fields the reason needs, `WhyRoomMissing`
is new, and `NoteRoomWalk` writes the two extended lines - every hunk is inside
the anonymous namespace or inside `NoteRoomWalk`, which returns at its first
line when `-log` is off, and NOTHING in `DrawRooms`, `GetRoomBounds` or
`SetRoomBounds` is touched. So `hd_enable=0` and `fps_mode 0` are unchanged by
construction and not merely by intention. `Preparation\Tools\Diagnose\RoomProbe.cpp`
gained `-walk` and its four companions. `be.bat` builds with **0 errors**;
`InterpTest` builds and runs **282 checks, 0 failed**; every `RoomProbe` run of
this round reports **0 of 59** watched device states changed.

**The tools.** `RoomProbe -walk <seed room> <x y z>` runs the classic portal
walk offline from the classic `.TRC` handed in with `-roombox`, over a fan of
view directions (`-walkyaw <from> <to> <step>`, default `0 359 15`;
`-walkpitch <deg>` or `-walkpitchfan <from> <to> <step>`), and prints the room
list, how often each room is in it with its distance and `OUTSIDE` flag, and
the rooms within sixteen sectors it never reaches at all. `-walkwant <room>`
names one room and prints why it is or is not in the list, with the widest and
narrowest window it ever gets; `-walkeach` prints every direction separately.
It also prints which rooms' boxes hold the eye, so a `ROOMFLASH` line can be
handed to it as it stands.

#### Fix round T4 - the reach: the second rectangle of the portal walk, the room behind the torn wall and the tower of Trajan's Markets (#123, #148)

The play test of 2026-09-08 named two things fix round T3 could only measure,
and the player's decision on both was the same sentence: *"das muss ein
globaler Fix werden"* - no hand-kept list of pairs.

* **JOBY2, the stairs.** The remaster's art tears the wall beside them open,
  and through the hole the room behind is the clear colour. The log's own
  `ROOMLIST` dumps say which rooms are drawn at that eye - 114 (whole), 123
  (whole), 111, 124 - and room 99, the one behind the tear, comes and goes as
  a seventeen pixel splinter at the screen's rim.
* **ANDREA2, the tower.** Room 222 of Trajan's Markets, identified in T3, is
  in the draw list only when the eye is tilted: at every view direction from
  the courtyard between pitch -10 and +10 the walk does not reach it at all,
  and it pops in as she walks.

Both are the same line of arithmetic, and it is not a missing portal.

---

**T4-1 - the mechanism, and what "the reach" has to mean.**

`SetRoomBounds` (`game\draw.cpp`) projects the four corners of a child portal
and then **clamps the result against the window of the room it is seen
through** - `actualRoom->test_left..test_bottom`, which for room 124 is the
projection of the classic doorway out of room 123. Room 99's portal lies to
the right of that rectangle, so the clamp leaves it with no width and the room
is dropped as `empty`. Measured on the play test's own eye
(`RoomProbe -walk 114 40024 -3408 24107 -walkyaw 335 335 1 -walkpitch -10
-size 1280 720`, the eye of the log's dumps - `-walkyaw` takes a from, a to
and a step, so one direction is asked for as `335 335 1`):

| | |
| --- | --- |
| room 124's window, through the classic doorway | `0,0..535,719` |
| the portal 124 -> 99, its own projection | `542,116..754,389` |
| clamped against 124's window | `535,116..535,389` - **no width** |

Six pixels. The doorway ends at 535 and the next room's portal begins at 542,
and everything between them is wall in the classic level and a hole in the
remaster's.

*And "clip the child against the parent's REACH instead" is not a second
reading of that - it is the same walk.* The brief asked for the two to be told
apart by measurement, and they are: with the child clamped against the
parent's reach, JOBY2's fan of 504 directions from that eye comes out at
**the same room list in every single direction, and the same `empty` refusal
of room 99 - a gain of zero**. One line says why: the root's reach IS the
screen, so `reach(child) = projection n reach(parent)` unrolls into
`projection n projection(parent) n ... n screen` -
the chain of intersections the classic window already is. **Only dropping the
parent out of the child's rectangle changes anything**, and that is the rule
this round builds:

> a portal REACHES the room behind it when it faces the eye and lands on the
> screen. The classic window is computed beside it exactly as before, out of
> the parent's window, and may now come out EMPTY.

A room the reach alone brought in is drawn: all of its art over the whole
screen, and **none of its intruding art at all**, because the scissor of fix
round T2 is the classic window and an empty window is an empty scissor. T2's
bleed guard is kept to the letter.

*Why the reach is a SEEN-MARK and not a second rectangle to widen, and this
too is measured rather than reasoned.* The first version kept a second union
per room and re-queued a room whenever a portal gave it something that union
did not hold. It runs the tool out of memory on ANDREA3 and ANDY2: with the
parent gone from the child's rectangle, a ring of rooms whose portals all face
the eye can be walked round and round, each lap widening some room's classic
window by a pixel and earning another queue entry. ANDREA3's worst frame took
**over 20,000 dequeues against the classic walk's 33**. The reach rectangle of
a portal does not depend on how the walk arrived, so one visit per room is the
whole of it; with the mark, the worst frame over the whole game is **319**
(JOBY4) against that walk's own 320.

**The cost, over the whole game.** `RoomProbe -reachsurvey` (new) puts the eye
at the middle of every room of a level in turn and walks a fan of forty
directions from each under both rules - 280 to 10,200 frames a level, 2560x1440.
These are the WALK's own numbers, before the engine's two reductions below - a
reach-only room the `.TRG` has nothing for comes back out of the list, and the
budget drops the farthest ones over 64 - so the game's own lists are at most
these and usually shorter; and an eye at a room's box centre is a worst case of
its own, standing in walls and outside rooms as often as in the open:

| level | rooms | frames | classic mean / max | reach mean / max | reach alone mean / max | over 60 | over 100 | dequeues max (classic) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ANDREA1 | 242 | 9,680 | 6.6 / 42 | 14.5 / 139 | 7.8 / 129 | 441 | **56** | 142 (60) |
| ANDREA2 | 252 | 10,080 | 8.5 / 65 | 15.2 / 111 | 6.7 / 102 | 438 | **14** | 115 (75) |
| ANDREA3 | 130 | 5,200 | 5.3 / 28 | 7.9 / 46 | 2.6 / 37 | 0 | 0 | 48 (33) |
| ANDY1 | 243 | 9,720 | 10.1 / 49 | 14.7 / 96 | 4.7 / 83 | 249 | 0 | 137 (131) |
| ANDY2 | 254 | 10,160 | 4.4 / 16 | 7.2 / 62 | 2.8 / 50 | 1 | 0 | 62 (29) |
| ANDY3 | 211 | 8,440 | 7.0 / 42 | 10.8 / 67 | 3.8 / 52 | 11 | 0 | 71 (65) |
| JOBY2 | 169 | 6,760 | 12.2 / 69 | 21.2 / 121 | 9.1 / 103 | 610 | **30** | 137 (76) |
| JOBY3 | 195 | 7,800 | 4.0 / 16 | 7.7 / 83 | 3.7 / 74 | 41 | 0 | 83 (16) |
| JOBY4 | 137 | 5,480 | 19.0 / **106** | 23.7 / 128 | 4.7 / 80 | 516 | **107** | 319 (320) |
| JOBY5 | 191 | 7,640 | 4.8 / 18 | 8.4 / 73 | 3.6 / 57 | 34 | 0 | 73 (18) |
| RICH1 | 165 | 6,600 | 4.3 / 21 | 7.4 / 58 | 3.1 / 53 | 0 | 0 | 58 (23) |
| RICH2 | 179 | 7,160 | 3.7 / 18 | 5.7 / 42 | 2.0 / 30 | 0 | 0 | 43 (21) |
| RICH3 | 218 | 8,720 | 4.5 / 24 | 7.1 / 69 | 2.6 / 59 | 19 | 0 | 69 (29) |
| RICHCUT2 | 7 | 280 | 3.0 / 6 | 3.0 / 6 | 0.0 / 1 | 0 | 0 | 6 (6) |
| TITLE | 116 | 4,640 | 8.4 / 48 | 8.8 / 49 | 0.5 / 15 | 0 | 0 | 124 (121) |

And from ten named eyes, each at the middle of that room's own box
(`-walkcentre`, the same point the survey uses) over a fan of 504 directions
(yaw every 5 degrees, pitch -30 to +30 every 10, 2560x1440):

| eye | classic mean / max | reach mean / max | the direction that gains most |
| --- | ---: | ---: | --- |
| JOBY2 room 123, the stairs | 3.5 / 6 | 7.6 / 29 | yaw 45 pitch -20, 5 -> 29 |
| ANDREA2 room 210, the courtyard | 20.1 / 43 | 29.4 / 76 | yaw 235 pitch -20, 36 -> 76 |
| ANDREA1 room 2, the alley | 6.9 / 19 | 24.7 / 69 | yaw 30 pitch 10, 4 -> 64 |
| ANDREA1 room 57, the Piazza corner | 7.2 / 19 | 26.2 / 72 | yaw 270 pitch -10, 10 -> 72 |
| ANDREA1 room 209, the open street | 4.0 / 16 | 27.5 / **133** | yaw 180 pitch 0, 4 -> 133 |
| RICH2 room 125, the offices | 5.8 / 17 | 9.4 / 43 | yaw 270 pitch -30, 12 -> 42 |
| JOBY4 room 114, the sea | 12.7 / 72 | 25.4 / 115 | yaw 340 pitch -10, 30 -> 111 |
| JOBY5 room 90, the sub | 5.9 / 15 | 11.2 / 72 | yaw 130 pitch -20, 15 -> 72 |
| ANDY1 room 164, the cemetery | 2.0 / 4 | 16.7 / 87 | yaw 250 pitch -30, 4 -> 87 |
| ANDY3 room 139, the mill | 2.7 / 5 | 10.8 / 63 | yaw 305 pitch -20, 5 -> 63 |

**Both fixes, in one line each.** JOBY2's room 99 goes from **88 of 504 view
directions to 124** at the play test's own eye `40024/-3408/24107` (seeded in
room 114 or in 123, either gives this; yaw every 5 degrees, pitch -30 to +30
every 10, 2560x1440) - and 36 of those 124 are the reach's alone: the 124
verdicts the classic walk gave as `empty` come back as 36 reached and 88
`offscreen`, and `toonear` is 76 under both rules. ANDREA2's tower, per
pitch, 120 yaws each:

| pitch | classic | reach |
| ---: | ---: | ---: |
| -30 | 16 of 120 | 33 of 120 |
| -20 | 9 | 34 |
| **-10 to +10** | **0 at every one** | **34 to 36** |
| +20 | 8 | 37 |
| +30 | 0 | 24 |

Level with the tower, the walk now reaches it in about three directions in
ten, which is what a 80 degree field of view over a full turn comes to.

---

**T4-2 - the fix, in the walk.**

`SetRoomBoundsReach` (`game\draw.cpp:3329`) is the classic function's own
arithmetic on the same four corners, carried into two answers: the projection
is taken once, the classic rectangle is formed out of its minima and maxima
seeded with the parent's window exactly as the seeded loop of `SetRoomBounds`
does, and the reach is the same projection clamped against the screen. The
side test in the caller, `tooNear == 4` and the fixup for a portal straddling
the eye plane are untouched, so a portal the classic rule refuses for any
reason other than the parent's window is refused here too. `FollowPortal`
(`:3582`) is the one line that chooses between the two, `ReachBeginWalk`
(`:1632`) wipes and seeds the state, and `GetRoomBounds` (`:3590`) accumulates
the classic window only for a dequeue that had one.

**And the reach adds .TRG art and nothing else** - three deliberate pieces:

1. **The classic geometry of a reach-only room is not drawn**, and that
   follows from its own empty window: `DrawStaticObjects` (`:2788`) and
   `InsertRoom` (`specific\drawroom.cpp`) both take `phd_left = r->left`, and
   both reject everything when left > right.
2. **A reach-only room the .TRG has nothing for comes straight back out of the
   list** (`ReachTrimRooms`, `:1677`). Otherwise it would be a room with
   nothing of itself in the picture whose ITEMS are still drawn - PrintObjects
   hands every item the whole screen - and an enemy standing in a room that is
   not there is not a fix.
3. **`outside`, the outside rectangle and `snow_outside` are read from the
   classic rooms alone** (`classicHere`, `:3616`). A reach-only room that
   happens to be `ROOM_OUTSIDE` must not be allowed to turn the sky on in a
   room the walk has always called indoors - that is the sky flash of fix
   round T2 coming back through another door.

*What the classic windows come to is unchanged, and that is provable rather
than hopeful.* Every classic rectangle handed out under the reach comes from a
parent that has a classic window, which by induction means a chain from the
root every step of which the classic walk itself follows; and the extra queue
entries the reach adds are unioned into the same `left..bottom`. The only
early out that moves is the one at the top of `SetRoomBounds`, which is skipped
while a room still has its reach visit owing - and it only ever fired for a
rectangle that added nothing to the union.

**The seams**, all behind `-log` and all staying in the build:

* `ROOMLIST   room=%d window=... reach=... REACH|window dist=...` - the two
  rectangles per drawn room, and the word that says which of the two carried
  it. A room marked `REACH` is drawn whole with no intruding art.
* `ROOMLIST frame=... the whole list ... target=(x,y,z) target_room=... cam=..`
  - the five second dump now carries the TARGET and the camera, which T3's did
  not: a room list without the direction it was walked in cannot be handed
  back to `RoomProbe -walk` as it stands.
* `ROOMFLASH ... why ... reach [...]` - a fifth answer beside `side`,
  `toonear`, `empty` and `noportal`: the classic window came out empty and the
  reach carries the room, so a room missing for THAT reason was dropped by the
  budget or the cap and not by the walk. `offscreen` is the reach's own
  refusal.
* `ROOMWALK reach rooms added=%d of %d (mean ... max ... classic ... reach
  ...), budget=64 trimmed=%d no_art=%d cap hit=%d` - once a second. What those
  rooms COST is on the `FPSLOG` line of the same second, which already carries
  `hd_rooms / hd_items / hd_draws` as the maxima over it.

---

**T4-3 - the boundary, and it is the one deliberate change to the walk both
paths share.**

`draw_rooms` is `short[100]` (`:111`) and `draw_room_list` a 128 entry ring
(`:114`), and **neither was ever counted**. `room_list_end` was written into
the ring modulo 128 with no test, so a walk with more than 128 rooms queued at
once would overwrite an entry that had not been dequeued; `number_draw_rooms`
was incremented with no test at all. A level may hold 256 rooms
(`game\setup.cpp:2418`), so both were reachable in principle - and the survey
above says the first one is reachable in the CLASSIC walk too: **JOBY4's
classic frame of 106 rooms is already six past the array.**

So `GetRoomBounds` refuses a room beyond `draw_rooms`' capacity and
`SetRoomBounds` refuses to queue one beyond the ring's, both unconditionally,
both counted as `cap hit=` in `ROOMWALK`. This changes nothing while the list
stays under a hundred - the deepest queue over the whole game is 70 - and
stops a memory overwrite where it does not. It is named here as the one change
of this round inside the code the classic path runs, because a guard that only
ran in the HD path would leave the classic path with the hole.

---

**T4-4 - the safety valve, and its number is measured.**

Above a budget the walk drops reach-only rooms, **farthest first**, and a room
with a classic window of its own is never dropped: it is a room the walk has
always drawn. The number comes out of the survey's own distribution - the
share of frames over a budget of 32 / 40 / 48 / 56 / 64 / 80 rooms:

| level | 32 | 40 | 48 | 56 | **64** | 80 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ANDREA1 | 13.7 | 9.6 | 7.0 | 5.3 | **4.0** | 1.8 % |
| ANDREA2 | 13.3 | 9.7 | 7.0 | 5.2 | **3.5** | 1.2 % |
| JOBY2 | 24.9 | 18.9 | 13.5 | 10.1 | **7.8** | 3.1 % |
| JOBY4 | 25.9 | 18.7 | 13.9 | 10.7 | **8.2** | 4.7 % |
| ANDY1 | 14.2 | 9.4 | 5.7 | 3.5 | **1.9** | 0.4 % |
| ANDY3 | 7.7 | 3.8 | 1.5 | 0.3 | **0.0** | 0.0 % |
| RICH1 / RICH2 / RICH3 | 1.8 / 0 / 2.7 | 0.7 / 0 / 1.0 | 0.3 / 0 / 0.5 | 0.1 / 0 / 0.3 | **0 / 0 / 0.1** | 0 % |
| ANDREA3 / ANDY2 / JOBY3 / JOBY5 | 1.8 / 1.3 / 3.3 / 2.5 | 0.3 / 0.5 / 1.4 / 0.9 | 0 / 0.1 / 1.0 / 0.6 | 0 / 0 / 0.7 / 0.6 | **0 / 0 / 0.4 / 0.3** | 0 % |

**64 is where the two families part.** The indoor levels never reach it at
all, and the four open ones cross it in 3.5 to 8.2 % of the worst-case frames
- so the valve is shut for ordinary play and opens where a street runs away to
the horizon. It is `kReachBudget` (`game\draw.cpp:1675`), one number in one
place, and `ROOMWALK` says how often it fired.

---

**The pictures.** All from `RoomProbe`, which draws the rooms of the walk's
own list at their own offsets with the engine's own renderer, and every run of
this round reports **0 of 59** watched device states changed.

* **JOBY2, the torn wall.** Seeded in room 114 at the play test's eye
  `40024/-3408/24107`, yaw 335, pitch -10, 1280x720, the walk's own list drawn
  (114, 123, 111, 124) against the same plus room 99:
  `j2_before_JOBY2_r114.png` is the play test's photograph - the iron
  staircase with a tall MAGENTA hole beside it where the remaster's art has
  torn the wall open and the clear colour stands behind it.
  `j2_after_JOBY2_r114.png` has room 99's own wall, its grating and its lamp
  in that hole. **9,444 of 921,600 pixels differ, 1.03 %, every one of them
  inside a 62 x 230 strip** - and the walk's reach rectangle for room 99 there
  is `542,116..754,389`, which contains it.
* **ANDREA2, the tower.** From the courtyard cluster `82000/-1500/41000`, yaw
  235, **pitch 0** - level, the case the classic walk never reaches - the
  walk's 29 classic rooms drawn against the same plus room 222:
  `a2_before_ANDREA2_r210.png` and `a2_after_ANDREA2_r210.png`. **6,705
  pixels, 0.73 %, in a 44 x 176 strip** at the left edge of the courtyard's
  opening: the brick block with the barred window, standing where before there
  was sky.
* **ANDREA1 207/53, T2's own bleed proof, to show the scissor did not move.**
  Camera inside room 207 (the tool's own automatic one, 960x720): room 53's
  art drawn whole differs from room 207 alone in **65,732 pixels**, and
  withholding the intruding triangles alone (`-window open`) gives a picture
  that differs from room 207 alone by **0 pixels**. At THAT camera the
  intrusion rule takes the whole of the bleed away; at others it does not take
  all of it - round T2 measured 7,093 pixels of room 53's own wall left
  standing in room 207's border ring at its own camera, and re-measured from
  the same eye turned 90 degrees the remainder here is 99,652. That is round
  T2's trade, unchanged: what is proven is that this round did not move the
  scissor, and `specific\hd\hd_render.cpp` - the file that tags and draws the
  intruding half - is not in this round's diff.

**What this round did NOT change.** With `hd_enable` off `HDLevelActive()` is
false, `g_reach_on` stays false, `FollowPortal` calls the function that
shipped, `classicHere` is true and the four window lines are the four window
lines - the walk is the walk, line for line, and the only difference in the
built code is the two capacity guards of T4-3. `fps_mode 0` is untouched
either way. `be.bat` builds with **0 errors**; `InterpTest` builds and runs
**316 checks, 0 failed** - the eight new ones are the JOBY2 rectangles above,
the invariant that the classic rectangle is inside the reach's over 2,000
random pairs, and the ring that shows what the seen-mark is for.

**The tools.** `RoomProbe` gained `-reach` (the two-rectangle walk, with both
room counts per direction and the mean, maximum, and worst gain over the fan),
`-reachsurvey [step]` (every room of the level as the eye in turn - the table
above, and the budget distribution), `-walkcentre` (the eye at the middle of
the seed room's box) and `-walkboxes` (every room's origin, which is what a
`-with` argument is made of). `-with` now holds 32 groups instead of 8,
because a picture of what the walk hands the frame is a picture of twenty to
forty rooms. `InterpTest` gained the `reach` section.

#### Fix round N - the water: the material that was a surface all along, the HD frames on the classic faces, and the six double rooms (#123)

Three findings and a correction, all of them measured against the fifteen
`.TRG` files, the fifteen classic levels and the remaster's own `TEX` folder
before a line was changed. The correction is ours: fix round R3a read one of
the remaster's materials as a kind of light, and it is not.

**N1 - material 16 is the still water surface.** Round R3a put material 16
beside material 15 and read its `param[2]` as an additive WEIGHT, on the
grounds that both live in the same narrow range - 0.035 to 0.2. That is a
coincidence of two numbers, and taking the material apart part by part
(`TrgProbe -matparts 16 -classic <folder>`) says what it really is:

| | |
|---|---|
| how many | **28 parts in 6 levels**, 245,678 triangles - and nine levels have none at all (ANDREA1 1 / 1,460, ANDREA2 1 / 2,048, ANDY2 2 / 2,384, JOBY3 1 / 128, JOBY4 1 / 73,728, JOBY5 22 / 165,930) |
| where | ANDREA1 59 (the fountain of Rome), ANDREA2 231 (a basin of Trajan's Markets), ANDY2 52 and 57 (pools in the Labyrinth), JOBY3 105, JOBY4 19 (**the sea of Deepsea Dive**, 73,728 triangles), JOBY5 22 parts including 65 and 82 (the open sea) |
| what shape | **17 of the 28 are a DOUBLE sheet**, two units apart, with opposite normals - the view out of the air and the view out of the water |
| where in height | 1 to 20 units off the classic `minfloor` of a `ROOM_REFLECT` room, in every one of the basins |
| its pages | 3255 is a grey noise field (R == G == B, alpha 254.8, **no hole at all**), 3159 a grey field with an alpha ramp |
| shared vertices | not one of them with any part of another material; `param[3]` is 0 everywhere; `sway` is 0 in 27 of the 28 |

And the three numbers scale with the AREA of the body of water, monotonically,
over the whole game:

| squares of floor | what it is | `param[0]` | `param[1]` | `param[2]` |
|---|---|---|---|---|
| 3,538 | JOBY4's sea | 0.1 | 1.0 | 0.035 |
| 900 | JOBY5's sea | 0.3 | 1.0 | 0.08 |
| 36 | a large pool | 0.8 | 0.7 | 0.1 |
| 1 to 28 | a basin | 0.8 | 0.7 | 0.2 |
| 4 | a puddle | 5.0 | 0.1 | 0.5 |

A brightness would not do that. A wave would: `param[0]` behaves like a spatial
frequency and `param[2]` like a rate, and `param[1]` is the one that does not
follow the area at all. JOBY5's group 115 carries the proof by contrast: two
parts of identical geometry to JOBY3's 105, one with `(5.0, 0.1, 0.5)` and one
with the **null tuple** `(0, 0, 0, 0)` - a still puddle beside a rippling one.

*Where the reading comes from, said plainly.* The table above is ours, measured
over the fifteen `.TRG` files, and it forces `param[1]`: it is the one slot
that does not follow the area. It does **not** decide which of `param[0]` and
`param[2]` is the frequency and which the rate - they move together with the
area, and one measurement cannot separate them. The order this round settled on
is the same order TROne's prose names, and TROne has that order from the
remaster's own shaders, which are off limits here at first hand and at second.
So it is written into the source as an **unproven coin flip with a one-line
reversal beside it** (`MaterialRipple`, exchange `out[0]` and `out[2]`) rather
than as a result, and the play test is what decides it. No constant of TROne's
is used anywhere: every number below is derived from the TR5 measurement above,
and the wave in `hd_room.hlsl` is a different curve with different terms.

*What follows from it, in the picture.* Twelve rooms had **no water surface at
all**: fix round H suppressed the classic face because the `.TRG` covers that
plane, and what covers it was drawn at a fifth or a twenty-eighth of its
strength and could not be seen (ANDREA2 231; JOBY5 2, 14, 49, 50, 52, 53, 61,
117, 139, 143, 151). `MaterialAddWeight` now lets material 16 fall through to
1.0 like every other material, and `MaterialRipple` (`hd_render.cpp`) reads its
three numbers as what they are.

*The ripple, per pixel, in our own words.* A new per-draw-call constant
(`gRipple`, pixel register c36) carries three floats and is **(0, 0, 0) for
every draw call in the game that is not one of those 28 parts**, where the
shader's one compare on `.y` skips the whole of it. The anchors are ours and
each is named in the source:

- the **scale** - `param[0]` = 0.8, the commonest value and the basins' own, is
  one wave per one and a half floor squares (1,536 units); everything else in
  proportion, with a floor of 0.05 so the null tuple has a finite wavelength.
- the **rate** - the classic water surface walks one `WaterTable` row per game
  step (`wibble += 4`, and the row is `(wibble >> 2) & 0x3F`), so its own
  period is 64 steps of a 30 Hz clock: 2.13 s. `param[2]` = 0.2 is put on that
  period, which makes 0.5 -> 0.85 s, 0.1 -> 4.3 s and JOBY4's 0.035 -> 12 s, a
  swell rather than a ripple.
- the **travel** - `param[1]` = 1 moves the coordinate by a **tenth of the
  part's own smaller UV span**. 23 of the 28 parts live in a sub-rectangle of
  their page (the tightest is 40 of 255, JOBY5's group 4) and the sampler is on
  CLAMP for a still surface, so anything that moves a coordinate has to know
  how far it may move it. On that tightest part the cap is 4 texels of a 256
  page - a bound on the travel, not a proof that the sample stays inside the
  part's own rectangle: a pixel already on its rim can be pushed that tenth
  over it.

The wave itself is the smoothstep of a triangle rather than a sine -
`abs(frac(p) * 2 - 1)`, then `3f^2 - 2f^3`, doubled and centred - which agrees
with a cosine to within 6 % of its own amplitude and has the same zero mean and
the same continuous slope. The reason is the ceiling: four sines take the room
pixel shader from **453 to 512** instruction slots, which is exactly what
shader model 3 guarantees and leaves the next round nowhere to stand; four of
these take it to **476**. Nothing at all happens in the vertex shader - `sway`
is 0 on 27 of the 28 parts, and a sheet that bobbed would tear away from the
stone rim it is let into.

*The brightness, measured before and after, `RoomProbe … -black -all`, the
additive pass alone, at the tool's default clock (`t = 0`; the ripple moves, so
a `-time` of its own shifts these by a tenth of a level and a few peaks):*

| room | camera | before | after |
|---|---|---|---|
| ANDREA2 231, the basin | `4400 -1200 1600` yaw 0 pitch 30 | mean 1.7/255, peak 120 | mean **4.3**, peak **159** |
| JOBY4 19, the sea from below | `712 3302 -19490` yaw 0 pitch -55 | mean 4.1/255, peak 13 | mean **120.8**, peak **255**, 20.6 % of it white |
| JOBY5 49, a flooded room | `3500 2600 1500` yaw 0 pitch 35 | mean 0.4/255, peak 46 | mean **3.2**, peak **107** |
| ANDREA1 59, the fountain | `5072 -1334 900` yaw 0 pitch 15 | mean 1.7/255, peak 143 | mean **2.6**, peak 143 |
| ANDY2 52, a pool in the Labyrinth | the room's own frame | mean 0.4/255, peak 43 | mean **2.8**, peak **130** |

Two rooms carry material 16 in a place the water survey never looks, because
their level has no `ROOM_REFLECT` room at all or the part is opaque, and they
belong in the play test all the same: **ANDY2's pools (52 and 57)**, one
additive - the line above - and one that draws nothing from that camera, and
**JOBY3's 105**, whose part is in the OPAQUE pass, where `MaterialAddWeight` was
never consulted; it is unchanged in brightness (0.7/255 before and after) and
gains only the ripple.

The basins are what the round was for: ANDREA2's is a moving water surface in
the picture where it was a smudge, and JOBY5's flooded rooms have a surface at
all for the first time. **The sea of JOBY4 is the one open question and it is
named here rather than tuned away.** Its sheet is THREE planes 16 units apart
whose normals all face down, and an additive pass sums them, so at full weight
one grey noise page is added three times over: 47 % of white on average with a
fifth of the frame clipped. Round R3a measured the same part at the same weight
from its own camera and got 98.1 % mean with 86.9 % pure white, and dimmed it
for that reason. Ours is not a grey lid - it reads as a bright rippling sea
seen from underneath, and the god rays are still drawn - but it is bright, and
the play test decides. If it has to come down, the honest lever is the sheet's
own baked light or the three-layer sum, **not** a weight invented for the
material: the file has none.

*The six double rooms.* With material 16 visible, six rooms would carry TWO
water surfaces - the classic face fix round H restored and the `.TRG`'s own
sheet 1 to 20 units beside it. `HDRenderGroupWaterArt` asks the same question
`HDRenderGroupFlatArt` asks, but over a second table built from the material 16
triangles alone, and **the face the sheet stands on is not drawn**. Measured
through the engine's own gate (`RoomProbe -watersurvey`), that is exactly six
rooms and no more:

| room | classic faces in the plane | the `.TRG` covers | of those, its own water sheet | restored now |
|---|---|---|---|---|
| JOBY5 4 | 30 | 13 | 6 | 24 |
| JOBY5 27 | 6 | 1 | 1 | 5 |
| JOBY5 28 | 7 | 3 | 3 | 4 |
| JOBY5 83 | 3 | 1 | 1 | 2 |
| JOBY5 115 | 6 | 1 | 1 | 5 |
| JOBY5 184 | 29 | 8 | 2 | 27 |

**Per face, and only the face - which is not where this round started.** The
first reading handed the WHOLE room to the `.TRG` on one sheet anywhere in its
plane, and measured that is far too much: room 184's sheet covers 2 of the 29
sectors the classic surface covers and its baked light there is 5 and 3 of 255,
so 27 sectors of a flooded galley would have gone bare to save a double over 2.
Rendered from `3580 -900 6138` pitch 60 the additive pass of that room is black
but for one patch in the top right corner, mean 0.2 of 255. Room 4 is the same
shape of case, 6 of 30.

The other end is just as wrong and was tried too: leaving the sheeted faces out
of the room's MAJORITY as well turns rooms fix round H deliberately handed to
the `.TRG` back into restored ones - ANDREA2's 231 (26 faces, 14 of them the
`.TRG`'s and all 14 the sheet) and eight more of JOBY5. So the majority is
counted exactly as fix round H counted it, sheeted faces and all, and the only
thing this round changes is that a face under the sheet is not drawn. There is
no hole where one is dropped, because the `.TRG`'s own water is standing there:
the pool stays one continuous surface, part of it the level's own and part of
it the remaster's.

Everything else is untouched: JOBY5's other 15 sheeted rooms and ANDREA2's 231
were already left to the `.TRG` by the majority rule, and JOBY3's 13 rooms - see
N3 - are not sheeted at all. Held against the survey room by room over all
fifteen levels, those six lines are the only ones that move: JOBY5 restores
**72 faces in 8 rooms**, where fix round H restored 86 in the same 8 and the
first reading of this round restored 5 in 2.

**N2 - the HD pictures of the classic water frames.** The surfaces fix round H
restored are the level's own faces, animated the classic way: `S_AnimateTextures`
(`specific\output.cpp`) rotates the CONTENTS of a range of texinfos once every
six game steps, so the face keeps its number and the tile it shows walks along
the range. The player found the other half himself - the remaster ships an HD
painting of each of those frames.

The way in is the `.TEX` sidecar, and it is the one file that maps a classic
tile to an HD page: a page count, that many page numbers, then one `uint16` per
object texture of the level. The format is TRForge's measurement
(`trf_remaster/Remaster.h` and `HdPages.h`, MIT - two more files that could not
be vendored and were used as a DESCRIPTION, exactly as `ItemModels.h` was for
the `.TRM`; `vendor/trforge` holds only what this tree compiles, see
`THIRD-PARTY.md`); the reader is `HDScanLevelTextures` in `hd_scan.cpp`,
written from the description. What an HD page is matters more than the number:
it is a repaint of the whole **64 x 64 cell** the tile sits in, at eight times
the size, so the classic `u` of 0..1 over a 256 page becomes `u * 4` minus the
cell's own column.

Measured over the fifteen levels, per level, with `RoomProbe -watersurvey`:

| level | ranges | frames | the HD pages | missing |
|---|---|---|---|---|
| ANDREA1 | 1 | 8 | 171..177, 236 | 0 |
| ANDREA2 | 1 | 8 | 171..177, 281 | 0 |
| ANDY1 | 1 | 8 | 764, 765, 785..787, 790..792 | 0 |
| ANDY3 | 5 | 33 | 897, 898, 901, 903..905, 915, 916, 935 | 0 |
| JOBY2 | 1 | 8 | 503, 515..521 | 0 |
| JOBY4 | 1 | 8 | the same eight | 0 |
| JOBY5 | 1 | 8 | the same eight | 0 |
| the other seven | 0 | 0 | none | 0 |

which is the player's own list of what he found in `TEX`, minus the three
eighth frames (236, 281, 935) he did not name and plus ANDREA's 149..153 and
170, which belong to ranges no water face names (ANDREA1's `.TEX` names 145 to
185 among its 118 pages; only 171..177 and 236 are on a water frame).
**Not one frame is missing a page.**

Three things were checked rather than believed:

- **the cell rule.** Over all fifteen levels: 217 to 559 tiles with HD art in
  73 to 117 cells, and **0 cells wanting two different files** in every one of
  them - which is what makes "the page belongs to the cell" usable. 0 to 47
  tiles per level DO reach across a cell edge and those are dropped and keep
  the classic picture; none of them is in a water range. (Counting a triangle's
  fourth corner - which is not a corner: `AdjustUV` adjusts three and the draw
  reads three - turned 127 of JOBY2's 382 tiles into false crossers, and that
  is why the triangle bit is read here. **Which word carries that bit differs
  between the file and the runtime**, and the tool read the wrong one until the
  audit of this round: in the `.TRC` it is bit 15 of the `TileAndFlag` word at
  offset +2, and `LoadTextureInfos` builds the runtime `flag` as the low 15
  bits of the file's flag word with bit 15 of the file's *tpage* word on top
  (`tex.tpage ^ (tex.tpage ^ tex.flag) & 0x7FFF`, `specific\file.cpp`), which
  is what `AdjustUV` then tests. The two bits disagree in 16,202 of the game's
  35,717 texinfos - 1,222 of JOBY2's 2,820 - so it was worth measuring: every
  number in this section is identical read either way, and the whole
  `-watersurvey` water-frame block is byte for byte the same before and after
  the correction, in all fifteen levels.)
- **the rectangles.** Every one of the water frames is a 64 x 64 tile filling
  its cell exactly, so its HD rectangle is `(0,0) 512x512` - the whole page -
  and no neighbouring tile can be pulled in whatever the filter does.
- **the rotation.** The engine cannot know which frame a texinfo is showing; it
  matches the CONTENT that is in it right now (page and uv corner, copied
  whole by the classic rotation, so the comparison is on the same bits) against
  the frames recorded at load. The tool simulates the classic rotation on each
  range and reads the answer off after every step: `503 515 516 517 518 519 520
  521 503 …` - it walks the range in order and comes back round, in all seven
  levels.

*How it is drawn.* Through the classic path exactly as before: the face keeps
its light, its shimmer, its sort, its blend and its double-sidedness, and what
changes is the page the bucket binds and the four coordinates on it
(`HDWaterFrameFor`, `hd_level.cpp`; two lines in `InsertRoomWater`,
`specific\drawroom.cpp`, both inside the HD branch). The pages are appended to
the level's own page cache - one texture per page for the whole level, the same
way the models are added - and borrowed into the classic `Textures[]` array
**above `nTextures`**, never by raising that count: the font is addressed as
`nTextures - 2`, the sky as `- 1` and the menu pages as `- 4` and `- 5`, and
every one of them would move. The reference is given back at the next level
build and when the device goes.

**And it is given back by POINTER, not by slot number** - the one thing the
first cut of this got wrong. The slots are taken at the CURRENT level's
`nTextures`, and by the time they are given back the array belongs to another
level: `FreeLevel` calls `FreeTextures`, which walks `0..nTextures-1` of the
OLD level (`specific\texture.cpp`) and therefore leaves a slot above it alone;
then `nTextures` goes back to 1 and `LoadTextures` refills `Textures[1..]` for
the NEW level (`specific\file.cpp`); only then does the main thread reach
`HDLevelBuild`. Where the new level has more pages than the old one, the slots
in the list are by that point the new level's own classic pages - measured from
the file headers, JOBY4 has 13 pages and borrows `Textures[13..20]` for its
eight water frames while JOBY5 has 19 and puts its own in `1..18`, so six of
JOBY5's pages would have been `Release`d and NULLed the moment Sinking
Submarine was built; TITLE's 22 and ANDY3 -> RICH1 are the same collision, and
the randomizer permutes the order, so every pairing is reachable. **And in a
randomized run it is not the exception but the rule**: the randomizer appends
texture pages, so out of a run those same levels carry 81 to 102 pages instead
of 19 to 21 (ANDY1 102, JOBY5 99, RICH1 97, ANDREA2 81) while JOBY4 still
carries 13 and DEL 10 - measured from the `.TRC` headers of a randomized data
folder beside its vanilla backup. Every borrow made in one of the small levels
lands inside the next level's own pages. So the
`AddRef`'d `IDirect3DTexture9*` is kept beside the slot, the reference is
always given back through that pointer, and the slot is cleared only while
`Textures[at].tex` is still it. The `HDLOG` line says how many were given back
and how many slots the next level had taken over.

**N3 - JOBY3's thirteen rooms, measured and left alone.** Fix round H
suppresses 26 classic water faces in JOBY3's rooms 26, 60, 144, 148, 150, 160,
161, 164, 166, 180, 184, 191 and 192, and the art that covers those planes is
not water - it is opaque material 0. Rendered (`RoomProbe`, rooms 26 and 164,
looking down at the plane in question): the remaster draws a **diamond-plate
deck** there, with crates standing on it. The submarine's rooms carry
`ROOM_REFLECT` for the wet sheen on the plating, not for a pool. The
suppression is right and nothing was changed - no fix on suspicion.

**N4 - and the documentation.** `docs/Engine-Manual.md` named JOBY4 "the
submarine" and ANDREA2 "the Colosseum", both one level out (JOBY3 is
`LVL5_SUBMARINE` and JOBY4 `LVL5_DEEPSEA_DIVE`; ANDREA2 is
`LVL5_TRAJAN_MARKETS` and ANDREA3 `LVL5_COLOSSEUM`, `game/gameflow.h`). Both
corrected, and the *HD from the Remaster* section gained the two entries this
round is about.

**The regression.** Sixteen rooms in eight levels - ANDREA1 12, 19 and 59,
ANDREA2 132 and 231, ANDREA3 20, ANDY1 30, JOBY2 19, JOBY4 19 and 40, JOBY5 49,
100 and 184, RICH1 13 and 136, RICH2 25 - rendered through the same cameras at
the same clock before and after: **byte for byte identical**. Eleven of them
carry no material 16 part at all, which is the point of those: the ripple
constant is three zeroes there and the shader's one compare skips it. The other
five are the water rooms of this round, and they are identical because the
change that follows the round - the water surface rule and the borrowed pages -
lives in `hd_level.cpp`, which the room renderer does not link. The state
bracket reports
**0 of 59 watched device states changed** on every run. `be.bat` builds with no
error and the four shader stamps match the MD5 of their `.hlsl` to the digit.
With `hd_enable` off nothing here runs at all: `InsertRoomWater` is only
reached from the HD branch of `InsertRoom`, and the two tables behind N1 and N2
are empty.

**The tools.** `RoomProbe -watersurvey` now prints, per room, how much of the
`.TRG`'s cover is the remaster's own water sheet and how many faces the room
restores of how many, which is the engine's own answer; and after the survey
the **water frames** of that level - the
animated ranges, their tiles, the cell each sits in, the HD page and rectangle,
the cell-rule count and the rotation simulation. It is the offline twin of
`BuildWaterFrames`, and both read the `.TEX` through the same
`HDScanLevelTextures`. `Tr5Level` gained `debugAnimTexBlock`, which is where the
animated ranges live in a `.TRC`.

#### Fix round O - pickups in the level: the EXTRA prefix (#123)

Fix round C bound two pickups by hand - `REVOLVER_ITEM` and
`REVOLVER_AMMO_ITEM`, the Desert Eagle - and left the rest of the pickup family
"for a round of its own" (`hd_bind.cpp`, the note beside `hand_table`). This is
that round: a **generic third binding step**, so the pistols, the uzi, the
shotgun, the HK, the grapple, both medipacks, the lasersight, the binoculars,
the flare, the crowbar, the burning torch, the two shell casings and the
compass all reach a real HD model without a line of the hand table spent on
any of them.

**The rule.** `bind_against` (`hd_bind.cpp`) tries a third thing, after the
level's `.MAP` and the hand table and before the slot falls back to its own
bare name: `EXTRA5_<slot>`, then `EXTRA_<slot>`, then `EXTRA4_<slot>`, each
checked against `5\ITEM` and then `4\ITEM` exactly as `find_model`
(`hd_model.cpp`) checks the plain name. `<slot>` is the engine's own symbol
with no prefix - `HK_ITEM`, `SMALLMEDI_ITEM`, and so on.

**The measurement.** Every candidate name was checked against the real files
of the installed bundle, not assumed: `5\ITEM` carries 16 `EXTRA5_*` models,
`4\ITEM` carries 52 (23 `EXTRA_*`, 29 `EXTRA4_*`). None of them are "squashed"
- the underscores in `EXTRA_SMALLMEDI_ITEM.TRM` are the underscores of the
slot name, not stripped and reapplied - so a plain, case-insensitive
concatenation of the prefix and the slot's own symbol is enough; no alias
table was needed. 24 of the 471 slot names resolve this way, and 23 of those
24 are loaded by at least one shipped level (measured by asking the rule about
every slot 0..470 in turn):

| via | slot |
|---|---|
| `EXTRA5_` (own game) | `CROSSBOW_ITEM` (the grapple), `CROSSBOW_BOLT`, `HK_ITEM`, `HK_AMMO_ITEM` |
| `EXTRA_` (the shared TR4-VI base) | `PISTOLS_ITEM`, `PISTOLS_AMMO_ITEM`, `UZI_ITEM`, `UZI_AMMO_ITEM`, `SHOTGUN_ITEM`, `SHOTGUN_AMMO1_ITEM`, `SHOTGUN_AMMO2_ITEM`, `BIGMEDI_ITEM`, `SMALLMEDI_ITEM`, `LASERSIGHT_ITEM`, `BINOCULARS_ITEM`, `FLARE_ITEM`, `FLARE_INV_ITEM`, `GUNSHELL`, `SHOTGUNSHELL`, `CROWBAR_ITEM`, `BURNING_TORCH_ITEM` |
| `EXTRA4_` (a TR4-only survivor) | `COMPASS_ITEM`, `CROSSBOW_AMMO1_ITEM`, `CROSSBOW_AMMO2_ITEM` |

`CROSSBOW_AMMO2_ITEM` is the twenty-fourth: it resolves to
`4\ITEM\EXTRA4_CROSSBOW_AMMO2_ITEM.TRM` (which confirms the TR4 tier is real
and not a fluke of the one slot) but no shipped level loads the slot at all,
so no level table below ever counts it. `SILENCER_ITEM` is the other half of
that measurement and the opposite case: no file under any of the three
prefixes nor under its own name, and no shipped level loads it either - it is
`docs\Engine-Manual.md`'s "documented classic" list, not a gap in this round.
`MEMCARD_*`/`PC_*_INV_ITEM` have no model anywhere in either folder and stay
exactly as documented before this round.

**The exclusions.** Two kinds of slot never reach the third step. Lara's own
composite (`LARA`..`HAIR`) is excluded structurally, the same way it always
was - `bind_against`'s Lara branch returns before the third step exists.
The second kind is new: a small, named `extra_excluded` table in
`hd_bind.cpp` - `GUN_FLASH` and the twelve pure light/particle/mist slots
Core's own `game\objects.h` groups beside it (`SMOKE_EMITTER_WHITE`,
`SMOKE_EMITTER_BLACK`, `STEAM_EMITTER`, `EARTHQUAKE`, `BUBBLES`,
`WATERFALLMIST`, `COLOURED_LIGHT`, `BLINKING_LIGHT`, `PULSE_LIGHT`,
`STROBE_LIGHT`, `ELECTRICAL_LIGHT`, `LENS_FLARE`). `GUN_FLASH` is the one
line here that is a measurement and not caution alone:
`4\ITEM\EXTRA_GUN_FLASH.TRM` really exists - the same "Effect category"
TROne's own source excludes the slot for - but measuring further shows there
is no bracket for it to tear. `SetGunFlash` (`game\tomb4fx.cpp`) only records
a matrix, called from `draw.cpp` after Lara's own node walk has already
returned, and the flash is painted afterwards by `DrawGunflashes`, a bare
`phd_PutPolygons` call against the classic mesh outside any
`HDObjectBegin`/`HDObjectEnd` bracket and outside this file's path entirely.
`GUN_FLASH` never reaches the per-item HD bind at all, so a bound name would
sit unused; it stays excluded regardless, as plain caution against a future
engine change routing it through a bracket that does not exist today. The two
shell casings sit in the very same block of the
enum and are excluded from the exclusion: `GUNSHELL`/`SHOTGUNSHELL` have their
own measured model
(`4\ITEM\EXTRA_GUNSHELL.TRM`, `EXTRA_SHOTGUNSHELL.TRM`) and stay in the pool.
Measured as carefully as `GUN_FLASH` was, though - and the answer is the same
one: they are not level items either. `DrawGunshells` (`game\tomb4fx.cpp`)
walks its own `Gunshells[24]` table and paints
`meshes[objects[..].mesh_index]` with `phd_PutPolygons`, called from
`draw.cpp` outside any `HDObjectBegin` bracket, so the model bound to them is
built and never drawn. `FLARE_ITEM` and `CROSSBOW_BOLT` are the same case one
step further out: `game\setup.cpp` gives each of them a `draw_routine` of its
own - `DrawFlareInAir` and `DrawWeaponMissile` - instead of
`DrawAnimatingItem`, and both paint the classic mesh. (`FLARE_INV_ITEM`, the
flare a level lays down to be picked up, keeps the default routine and does
draw.) What those four cost is a vertex buffer and no page: on the shipped
bundle `EXTRA_GUNSHELL` shares page 8523 with the uzi, `EXTRA_SHOTGUNSHELL`
page 8534 with the shotgun ammo and the crowbar, and `EXTRA_FLARE_ITEM` page
8519 with `EXTRA_FLARE_INV_ITEM`. They are left in the pool as this round
found them; moving them into `extra_excluded` beside `GUN_FLASH` is a
behaviour change and is left to whoever decides it.
None of the twelve caution-only lines had a matching file at measurement time;
they are excluded anyway so a future remaster patch adding one cannot silently
bracket-break something again.

**The hand table is asked first and keeps the Deagle.** The generic rule
alone would resolve `REVOLVER_ITEM` to `EXTRA5_REVOLVER_ITEM` - Tomb Raider
IV's six-shooter, first in trial order - and never reach the `_JOBY` Desert
Eagle variant, because it has no notion of "try the other spelling". Fix
round C's two hand-table lines are therefore asked BEFORE the generic rule in
`bind_against`, not after, and win outright: `REVOLVER_ITEM` keeps binding to
`EXTRA5_REVOLVER_ITEM_JOBY` (`via=hand` in the log), measured on JOBY2's own
slot table below.

**Two level variants the generic rule does not reach, and does not have to.**
The same "no notion of the other spelling" that costs the Deagle its name costs
two more: `5\ITEM` also ships `EXTRA5_HK_ITEM_RICH2.TRM` and
`EXTRA5_BINOCULARS_ITEM_RICH.TRM`, and no `.MAP` names either slot, so RICH2's
HK resolves to the plain `EXTRA5_HK_ITEM` and all three rich levels' binoculars
to the shared `EXTRA_BINOCULARS_ITEM` (measured, `via=prefix5` and
`via=prefix`). Both are the right object in the right hand - an HK is an HK -
just not the variant the bundle keeps for that level, and both were plain
classic before this round, so neither is a regression. Two hand-table lines
would close them if the pictures ever turn out to differ enough to care;
nobody has looked yet.

**The node guard catches a real mismatch, not a theoretical one.**
`COMPASS_ITEM` resolves to `EXTRA4_COMPASS_ITEM.TRM`, a two-part model (a
hinged lid over the case) - but in 13 of the 15 shipped levels the classic
slot's own mesh tree has exactly **one** node, because the compass is
inventory-only there and never drawn as a world pickup; a vertex weighted to
the model's second node names a node the classic tree does not have, and the
existing skeleton test (fix round B) throws it out - `SKELETON: names node 1
of 1` in the log - falling back to the classic mesh precisely as it does for
a foreign creature in the wrong skeleton. Only `RICHCUT2`, whose classic
compass slot really does carry two meshes (measured: `nodes=2` there against
`nodes=1` everywhere else), passes and draws the HD model. No new code was
needed for this - the round's whole safety net is the guard fix round B
already shipped, doing exactly the job it was built for.

**Measured across all 15 shipped levels** (`ModelProbe -level <STEM> -all`,
classic `.TRC` from a Tomb Raider Chronicles install, `.MAP` from the
Remastered one):

The six `via` columns do NOT add up to the `slots` column and are not meant
to: Lara's own slots are asked no bind question at all (rule 3's structural
exclusion, above) and so fall into none of the six. ANDREA1 is the worked
example - 42+6+2+15+1+63 = 129 answered, plus the 24 `LARA`..`HAIR` slots the
level loads, is its 153.

The last column is the level's TOTAL number of node-count refusals, not this
round's share of them: `COMPASS_ITEM` is the one this round brought, and every
level that also loads `FIREROPE` has that one too - a pre-existing refusal on
a slot that binds by its own bare name and has nothing to do with the EXTRA
prefix (`FIREROPE.TRM` names node 16 against a classic tree of 15, or of 1).
`ANDREA2` and `ANDREA3` do not load `FIREROPE` at all, which is the whole of
why they read 1 where their neighbours read 2.

| level | slots | map | hand | prefix5 | prefix | prefix4 | classic | node-count refusals |
|---|---|---|---|---|---|---|---|---|
| ANDREA1 | 153 | 42 | 6 | 2 | 15 | 1 | 63 | 2 (COMPASS_ITEM, FIREROPE) |
| ANDREA2 | 150 | 43 | 9 | 2 | 15 | 1 | 57 | 1 (COMPASS_ITEM) |
| ANDREA3 | 141 | 27 | 6 | 2 | 16 | 1 | 66 | 1 (COMPASS_ITEM) |
| ANDY1 | 154 | 33 | 8 | 2 | 16 | 1 | 68 | 2 |
| ANDY2 | 156 | 32 | 8 | 2 | 15 | 1 | 74 | 2 |
| ANDY3 | 159 | 31 | 10 | 2 | 17 | 1 | 72 | 2 |
| JOBY2 | 151 | 45 | 8 | 2 | 14 | 1 | 57 | 2 |
| JOBY3 | 191 | 65 | 7 | 2 | 16 | 1 | 74 | 2 |
| JOBY4 | 69 | 11 | 0 | 0 | 14 | 1 | 31 | 2 |
| JOBY5 | 200 | 50 | 8 | 2 | 16 | 1 | 97 | 2 |
| RICH1 | 196 | 64 | 7 | 2 | 15 | 1 | 91 | 2 |
| RICH2 | 181 | 43 | 5 | 2 | 13 | 1 | 94 | 2 |
| RICH3 | 183 | 40 | 7 | 4 | 15 | 2 | 93 | 2 |
| RICHCUT2 | 54 | 5 | 0 | 0 | 0 | 1 | 25 | 0 (compass fits here) |
| TITLE | 84 | 38 | 0 | 0 | 1 | 0 | 37 | 0 (no compass item loaded) |

RICH3 stands out at `prefix5=4, prefix4=2`: it is the one shipped level whose
own moveable list still carries `CROSSBOW_ITEM` (the grapple) and
`CROSSBOW_BOLT` with real geometry, on top of `HK_ITEM`/`HK_AMMO_ITEM`, which
make up the `prefix5=2` every other level with a submachine gun already
shows; `CROSSBOW_AMMO1_ITEM` joins `COMPASS_ITEM` in RICH3's `prefix4`
column for the same reason - it is native to RICH3 and no other level keeps
it loaded. `COMPASS_ITEM` is the one slot in every level's `prefix4` column,
present as a loaded slot in all 15 files whether or not that level's own
vanilla content ever shows it, because the randomizer can put the compass
anywhere.

**HDLOG.** Behind `-log`, one line per slot -
`HDLOG bind slot=<n> name=<sym> via=<map|hand|donor|prefix5|prefix|prefix4|classic>
model=<file> nodes=<trm>/<classic>` - and a level summary,
`HDLOG bind total=N map=N hand=N donor=N prefix=N classic=N` (`prefix` there
is the three prefix tiers added together; the per-slot line keeps them apart).
A donor slot (the randomizer's own cross-level copies) is tagged `via=donor`
by the caller rather than asked again here: its name was already decided
earlier, when the slot list was built, by `HDBindDonorSlot` - which runs the
same `bind_against` against the DONOR's `.MAP`, and since this round can be
answered by the EXTRA prefix there too, exactly as the level's own slots can.

**The tools.** `MapProbe` and `ModelProbe` both compile against the new
`HDBindSlot`/`HDBindDonorSlot` signature (an added `installRoot` - rule 3
needs a disk to ask - and an added `HDBindVia*` output). `ModelProbe -all`
gained a `via` column on every row, loaded or classic, and a bind-count
summary line under the table; looped over the 15 stems with `-level <STEM>
-classic <classic .trc> -all` it is the whole of the table above, no new
tool needed. `-name EXTRA_SMALLMEDI_ITEM` (and any other `EXTRA*` name) draws
it as a standalone `.PNG` exactly as any other model name always could - this
round changes what `bind_against` reaches FOR, not how a model is loaded or
drawn once named.

**hd_enable off, unaffected.** `HDObjectLevelBuild` - and with it every call
this round touches - is only reached from `HDLevelBuild`, which returns at
its own `if (!HDEnabled() || !g_stem[0]) return;` before a single HD line
runs (`hd_level.cpp`). With HD off, `bind_against`'s new step never executes;
the classic path takes the identical code it always did. `be.bat` builds with
no error.

**The pointer-lifetime bug the audit caught before this round was committed.**
The first version of rule 3 wrote its answer into one shared `std::string`
(`hd_bind.cpp`'s old `g_alt_buffer`) and returned a pointer into it - fine for
a caller that reads the pointer before asking `bind_against` again, which
every probe tool and the `HDLOG bind` line do, but not for
`HDObjectLevelBuild`'s donor pass (`hd_object.cpp`): it resolves EVERY donor
slot's model name into `DonorPlan::model` first and reads them all back only
later, once the bind poses are built, by which point the one shared buffer
held whatever slot rule 3 was asked about last - so an earlier donor's name
would silently read back as a LATER donor's, or as whatever the buffer held
after a failing lookup on a third slot in between. This is exactly the shape
the randomizer's own weapon-copy rules hit on every run with weapon
randomisation on: no shipped `.MAP` names a donated weapon pickup, so slots
334-340, 345/346 and 351/352 all fall through to rule 3, several to a level in
the same donor pass (347/348, the Desert Eagle pair, are the two of the
thirteen that stop at the hand table one step earlier - measured `via=hand`). The fix answers rule 3 from a cache instead, one entry per
slot (`hd_bind.cpp`'s `g_alt_cache`, a `std::map<int, AltAnswer>`) - a `std::map`
node, unlike a `std::string`'s buffer, never moves or is overwritten once
written, so a pointer taken from it stays correct no matter how many other
slots are asked afterwards. `HDBindSlot`/`HDBindDonorSlot`'s header comment is
updated to match: the returned pointer now outlives the call, the same
guarantee rules 1, 2 and 4 always gave, rather than the one-call lifetime the
header used to warn about (and which the donor pass violated anyway). Caught
by the fixing agent's audit against the review report before anything was
committed; a 20-odd-line program linking `hd_bind.cpp` alone reproduces the
old bug (kept pointer for one slot silently changes value after a second
slot's lookup, or after a failing third lookup) and confirms the fix.

#### Fix round O2 - the grappling gun in Lara's hand (#123)

The play test of fix round O: *"the weapon on Lara's back is the HD grappling
gun, on the floor too, only as the active weapon in her hand it is still the
Tomb Raider IV crossbow."* And the user's own explanation, which the engine
confirms line for line: *"TR5 has no laser sight for the grappling gun, that is
a TR4 leftover - it is standard on the weapon, forced at pickup."*

**Measured.** `game\larafire.cpp` (`WeaponObject`) draws the gun in the hand
from `LARA_CROSSBOW_LASER` whenever `lara.crossbow_type_carried` carries
`W_LASERSIGHT`, and from `CROSSBOW_ANIM` otherwise. In TR5 that bit is never
off: `game\newinv2.cpp`'s pickup of `CROSSBOW_ITEM` in the VCI levels
(`LVL5_THIRTEENTH_FLOOR..LVL5_RED_ALERT`) writes `W_PRESENT | W_LASERSIGHT |
W_AMMO1` outright, the weapons cheat does the same, `game\laramisc.cpp` does
it at a level start, and `combine_crossbow_lasersight`, the one function that
clears it, is not reachable from TR5's inventory. So the grappling gun in the
hand is ALWAYS the `LARA_CROSSBOW_LASER` slot - and the wardrobe
(`specific\hd\hd_lara.cpp`) had that slot on `HAND_BARE_CROSSBOW_LASER` /
`HAND_GLOVES_CROSSBOW_LASER`, Tomb Raider IV's crossbow with a scope, since fix
round C, with a note in every level's log that the picture was the user's to
judge ("the grappling gun with a sight wears HAND_BARE_CROSSBOW_LASER - the
wardrobe ships no grapple variant"). The remaster ships `HAND_BARE_GRAPPLE`
and `HAND_GLOVES_GRAPPLE` in `4\ITEM` and no grapple-with-sight model at all.

**Fix.** `LARA_CROSSBOW_LASER` now maps to the same wardrobe part as
`CROSSBOW_ANIM`, `HD_LARA_PART_HAND_GRAPPLE` (`hd_lara.cpp`, `kSlotRole`);
the part `HD_LARA_PART_HAND_GRAPPLE_LASER` is gone from the enum
(`hd_lara.h`), the part table, the rank table, the worn-pairs table and the
log note. Nothing else changes: the holster on the back, the pickup on the
floor and the laser dot (an effect of the engine, not a mesh) are as they
were. `ModelProbe -lara RICH3 -guns grapple_laser` before and after: the
crossbow with its scope in her hands, then the grappling gun.

**Classic path.** Untouched - the wardrobe only exists behind the HD guard.

#### Fix round O3 - the headset and Rich2's own HK (#123)

Two user findings from the fix round P play test, both in the VCI levels:
*"for the Rich levels he still has to fetch the headset instead of the
binoculars"*, and *"Rich2 has a special HK - here the wrong one was used, it
has a strange rotation in that level only."*

**Measured.** Neither slot is named in any of the fifteen `.MAP` files, so
both took fix round O's prefix rule. For `BINOCULARS_ITEM` that rule finds
`4\ITEM\EXTRA_BINOCULARS_ITEM.TRM`, the binoculars, in every level; the
headset the VCI levels call the same slot (`game\newinv2.cpp` renames it,
`TXT_Headset`) sits one folder up as `5\ITEM\EXTRA5_BINOCULARS_ITEM_RICH.TRM`,
a name no prefix rule can build. For `HK_ITEM` the rule finds
`EXTRA5_HK_ITEM.TRM`, the HK of every other level; RICH2's HK is the scripted
second one, a mesh of its own, and the inventory turns it by angles of its own
(`newinv2.cpp` rewrites `INV_HK_ITEM1`'s `xrot`, `yrot`, `zrot`, `flags` and
`yoff` for `LVL5_ESCAPE_WITH_THE_IRIS`, and the pickup display lifts it by 70
units there). The generic HD model, modelled in the other levels' frame and
turned by RICH2's angles, is the user's "strange rotation". The remaster
ships the RICH2 mesh's own HD twin as `EXTRA5_HK_ITEM_RICH2.TRM`. It ships an
`EXTRA5_HK_ITEM_RICH2_CUT.TRM` too, which nothing here uses: RICHCUT2 carries
no `HK_ITEM` slot at all, only Lara's `HK_ANIM`, which is the wardrobe's.

**Fix.** The hand table in `specific\hd\hd_bind.cpp` can now bind a line to
named levels: a third field lists the level stems the line holds in, space
separated and compared without regard to case, 0 meaning every level as
before. Two lines use it - `BINOCULARS_ITEM -> EXTRA5_BINOCULARS_ITEM_RICH` in
RICH1, RICH2 and RICH3, and `HK_ITEM -> EXTRA5_HK_ITEM_RICH2` in RICH2. A line
for another level is skipped and the slot goes on to rule 3 as before. For the
level to be known, both map loaders now remember the stem (`HDMapLoadForLevel`
as given, `HDMapLoad` from the file name, whether or not the file exists), a
donor's bind hands in the donor's stem, and `HDMapFree` forgets it again;
`HDMapLevel()` reports it.

**Proof.** `ModelProbe <5> -level <LEVEL> -classic <LEVEL.TRC> -all`, before
and after:

| level | `HK_ITEM` | `BINOCULARS_ITEM` |
|---|---|---|
| RICH2 | prefix5 `EXTRA5_HK_ITEM` -> hand `EXTRA5_HK_ITEM_RICH2` | prefix `EXTRA_BINOCULARS_ITEM` -> hand `EXTRA5_BINOCULARS_ITEM_RICH` |
| RICH1, RICH3 | prefix5 `EXTRA5_HK_ITEM`, unchanged | prefix `EXTRA_BINOCULARS_ITEM` -> hand `EXTRA5_BINOCULARS_ITEM_RICH` |
| ANDREA1 | no such slot | prefix `EXTRA_BINOCULARS_ITEM`, unchanged |
| RICHCUT2 | no such slot | no such slot |

Whether the RICH2 ring now turns the HK the way the remaster does is the play
test's to say: the angles are RICH2's own and the model is the one made for
them, but that is an expectation, not a measurement.

**Classic path.** Untouched - the table is only asked behind the HD guard.

#### Fix round P - the inventory ring in HD (#123)

The ring was the last place in the game that still drew 1999 objects while
everything around them was the remaster's. It is now the same seam once more,
and deliberately not a second render path.

**Measured first, three things.**

*The seam is already there.* `game\newinv2.cpp`'s `DrawInventoryItemMe` walks a
mesh tree exactly as `DrawAnimatingItem` does - a matrix per node,
`item->mesh_bits` deciding which node draws - and only the last step differs:
`phd_PutPolygonsPickup` instead of `phd_PutPolygons`, at four call sites. The
inventory's own loop already calls `S_InitialisePolyList` and
`S_OutputPolyList` once per present, and those two already call `HDFrameReset`
and `HDFrameDraw`, so the HD bracket was entered on every inventory frame
before this round and drew nothing.

*The off-centre projection is the one real problem.* The classic ring projects
each item about the item's own place on the screen: `phd_PutPolygonsPickup`
moves `f_centerx` / `f_centery` to the item's x/y for the length of one mesh
and puts them back afterwards. The HD path builds ONE projection per frame,
about the screen centre. The difference is two numbers,

    kx = (X - Cx) / persp,   ky = (Y - Cy) / persp

and the only question is where to put them. The projection divides by `vz`, so
anything added to `vx` in proportion to `vz` survives it as a constant pixel
offset:

    vx' = vx + kx * vz
    sx  = vx' * persp / vz + Cx = vx * persp / vz + X

which is the classic answer to the arithmetic and not to a tolerance, with
`vz` - and therefore the depth - untouched.

*And it goes into the PROJECTION, not into the item's matrix.* The idea of
folding the ring's centre into the item at all is TROne's
(`games\tr5\glue\hostglue_2d5.cpp:502-506`), and TROne does it as a SHEAR of
the item's matrix - row 0 += kx * row 2. The first cut of this round copied
that road and it was wrong, for a reason the pictures show at once:
`hd_model.hlsl` skins the vertex **normal** through the very same `gJoints`
matrices and renormalises the result, so a sheared node matrix tilts and
rescales every normal of the item and the ring's one sun then lights it by an
amount that depends on where on the screen it stands. Measured on the shader
itself, `ModelProbe -ringlight 160` over the object's own pixels
(`EXTRA_SMALLMEDI_ITEM`, mean brightness): **93.61** with no offset, **67.18**
in the middle of the inventory row, **64.15** at a side slot, **108.87** on the
ammo row - two rings that stand on the screen at once and 45 % apart in
brightness.

So `HDModelOffCentre` (`specific\hd\hd_model.cpp`) writes the two numbers into
`_31` and `_32` of a copy of the frame's projection for the length of one draw
call - `_31 = kx * _11`, and `_11` is `persp / Cx`, so the term is exactly the
`X - Cx` pixels asked for - and the node matrices reach the card as the
engine's own, untouched. A normal never goes through a projection, so the
offset cannot reach one. The same measurement afterwards reads **93.61 at all
four places**, over the identical 3108 object pixels the unsheared render has -
the same number to the hundredth, not a tolerance. (The four places are
`kx`/`ky` of 0/0, 0/-0.408477, -0.519208/-0.408477 and 0/+0.305127, which at
512x512 are whole pixel offsets, so the object lands on the same pixel grid in
all four; move it by a fraction of a pixel and the grid alone moves the mean by
a quarter of a percent.)
The derivation stands in full over `HDInvNode` (`specific\hd\hd_object.cpp`)
and over `HDModelOffCentre` (`hd_model.h`); `ModelProbe -ringcheck` now
measures both halves, the place and the light.

*And there is NO scale term*, which is the part that had to be measured rather
than argued. TROne multiplies the sheared row by `f_mpersp/persp` because its
host builds a projection out of the window's real width and would otherwise
draw ring objects four thirds too wide on 16:9 - its own "diary drawn buggy"
report. TR5 does not have that problem to correct: `AlterFOV`
(`specific\3dmath.cpp`) computes `f_persp` from `phd_winheight * 320 / 240`, a
4:3 implied width whatever the window is, and our HD projection is built from
that very `f_persp` (`HDFrameCaptureProjection` takes it,
`HDRenderMakeProjection` is fed it). So the factor is exactly one, and the HD
ring is the same size as the classic ring by construction rather than by
correction. `ModelProbe -ringcheck` says so in pixels: the model's bounding
rectangle projected both ways at 24 ring places - the six visible slots of both
rings, at 640x480 and at 1920x1080 - agrees in position and in **width and
height** to a worst **0.00013 px**, budget 1 px. The rectangle is 519.33 px
wide at 4:3 and 1168.50 at 16:9, which is 2.25 = 1080/480 exactly: the ring
scales with the picture's HEIGHT and gains nothing from the extra width, on
both paths.

**The seam.** `HDInvBegin` / `HDInvNode` / `HDInvEnd`
(`specific\hd\hd_object.cpp`, declared in `hd_object.h`) at the four
`phd_PutPolygonsPickup` call sites. They are `HDObjectBegin/Node/End` with
three differences, each a property of the ring rather than a choice: the item's
own screen centre above, which it carries to `HDModelOffCentre` at draw time
and never into its matrices; the light below; and no animation, because the
ring stands on frame 0
and never interpolates, so there is no interpolated branch. An item with no HD
model, one whose skeleton the model does not fit, and every item with the HD
path off, answers false and is drawn classically at the same four call sites -
the ring never has a hole in it. They share the frame list, the bracket and the
draw with the world's HD objects: no second list, no second projection upload,
no second state bracket.

**The node mask is exact, and needed no flag.** `INVOBJ::meshbits` reaches
`HDModelDraw` as `nodeMask` the way `item->mesh_bits` has since stage S3-2, so
a node the ring deliberately leaves out - the passport's page states, a globe's
markers - draws nothing. TROne needs `TRONE_LIGHT_EXACT_NODES` for this because
its default invents a matrix for a node the item bracket never saw; ours takes
the mask as a mask and has no such default.

**The light is the ring's own and nothing else.** `phd_PutPolygonsPickup`
throws the level lighting away for an inventory object and puts up two things:
an ambient of 8/8/8, and ONE sun of the ring's `pcbright` for this item -
`0x202020` for the objects to the side, up to `0xA0A0A0` for the selected one -
pointing along `(0, -50, -100)` normalised over `sqrt(12500)`, doubled. That is
what travels. The classic line reads the direction out of `D3DMView`, which is
the NODE's matrix at that moment, so what it computes is `R^T d`, and the
shading term `dot(n_model, R^T d)` is `dot(n_view, d)`: in view space, where
`HD_OBJLIGHT_SUN` lives, the direction is simply `d` and no per-node work is
left. The doubling is `sunScale = 2.0`, which is the field `hd_model.h` already
had for exactly this ("the sun is doubled in the inventory"). No room cube, no
point lights, no dynamics, and no sheen - an item has none in the level either
(fix round D, D2), so a pistol on the ring shines as the same pistol shines
lying on the floor.

That last sentence is only true because the offset lives in the projection. The
classic term is `dot(M n, d)` with `M` the node's own matrix and no
renormalisation (`aTransformLightClipMesh`); the shader's is
`dot(normalize(M n), d)`, and for a matrix that is a rotation the two are the
same number, because a rotation leaves a unit normal a unit normal. Put an
offset into `M` and it is no longer a rotation. `ModelProbe -ringcheck` prints
that as a column: over 26 sample normals at each of the 24 ring places, the
engine's road differs from the classic term by **4e-9** and the shear road by
up to **34.6 %**.

**The fog bulbs are taken off a ring frame** (`hd_level.cpp`, `HDFrameDraw`).
`ActiveFogBulbs` holds positions the game already turned into VIEW space, for
the view the last played frame had; the ring draws through an identity view
with the item pushed down +z, so a bulb of the room Lara is standing in would
sit in front of her medipacks. The distance fog is NOT taken off with them: the
classic ring keeps it and a ring item stands a few hundred units away, far
short of the threshold on either path, so both do the same nothing.

**One side effect of the ring having HD objects at all**, named here because
nothing else in this round covers it: `HDFrameDraw` no longer returns early on
an inventory frame, so `FillFrameLighting` runs, and with it stage S5's
`AdvanceClock`. The HD material clock is therefore *sampled* on ring frames,
which it was not before. It does not RUN: the clock is the difference of
`GlobalCounter`, `GlobalCounter` is stepped in `ControlPhase`'s tick loop
(`game\control.cpp:291`), and `S_CallInventory2` blocks inside that loop - so
every ring frame adds exactly zero. The one real change is that the tick or two
between the last played frame and the ring opening is added on the first ring
frame instead of on the first frame after the ring closes. The total is the
same to the last hundredth of a second either way.

**Depth needed nothing at all, and that was measured rather than assumed.** The
classic ring's polygons go through the ordinary sort list with the TEXTURE's
own `drawtype`, which for an opaque pickup is 0 - and `HWR_DrawSortList` case 0
(`specific\polyinsert.cpp`) draws it with the depth write ON and the depth test
left on. So the classic ring already sorts by depth, and the HD ring writes the
same depth values through the same projection (`HDRenderSelfTest`: both paths
write `a - b/z` for the same point). Two consequences: HD items sort against
each other correctly, and a classic ring item - the compass, a passport page -
sorts correctly against an HD one, because `HDFrameDraw` runs before
`DrawSortList` and leaves its depth behind. The frozen backdrop cannot occlude
anything either: `S_DrawTile` (`specific\LoadSave.cpp`) puts it at `sz` 0.995,
which with the engine's `znear` 20 / `zfar` 20480 is the far plane exactly. No
per-item depth clear, no reordering.

**Sixty frames a second.** `HDObjectFrameReset` runs once per DRAWN ring frame
and never per logic tick: the ring's own loop calls `S_InitialisePolyList` once
per present, that calls `HDFrameReset`, and that calls `HDObjectFrameReset`. In
any case `GameFrameMinTicks()` holds the whole inventory at thirty presents
(5.8, order 1b), and the menu skip that rule exists to prevent is untouched -
this round changes no input path and no title code. The ring's own spin stays
where it always was, in `draw_current_object_list`.

**The pickup display stayed classic in this round, and fix round P2 took it
back.** `DrawThreeDeeObject2D` has a second caller: `S_DrawPickup`
(`specific\output.cpp`), the little turning object in the corner when Lara picks
something up. It is called from INSIDE `S_OutputPolyList` (`output.cpp:1860`),
after `HDFrameDraw` (`output.cpp:1820`) has already drawn this frame's HD list -
so an item pushed there would suppress its own classic polygons and be drawn by
nobody. The list is not emptied until the next `S_InitialisePolyList` reaches
`HDObjectFrameReset`; it would simply sit there and then be thrown away. Round P
therefore had `HDInvBegin` refuse when `InventoryActive` is 0, which is the one
line that separates the two callers.

That reasoning about the LIST still stands and is what P2 is built on: P2 does
not push the item onto the list, it draws it on the spot in a bracket of its
own. The one line still separates the two callers - it routes now instead of
refusing. See "Fix round P2" below.

**The title menu has no ring.** `TitleOptions` (`game\gameflow.cpp`) and
everything it reaches - the level select, the options tree, the load screen -
is `PrintString` over the title level, and the title level's own objects have
gone through the world seam since stage S3-2. There is nothing there for this
round to do, and it touches no title code.

**What stays classic on the ring**, and it is the same three in every campaign
level: `COMPASS_ITEM` and the two passport pages `PC_LOAD_INV_ITEM` and
`PC_SAVE_INV_ITEM`. (The enum calls those two `INV_MEMCARD_LOAD/SAVE_INV_ITEM`
- `game\newinv2.h:162` - but rows 27 and 28 of `inventry_objects_list`
(`newinv2.cpp:374`) hold the `PC_` slots, so the ring draws two passport
objects and not four; the `MEMCARD_` slots exist in the level and can never
reach the ring.) The passport pages have no `.TRM` in either bundle. The
compass has one - `4\ITEM\EXTRA4_COMPASS_ITEM.TRM`, which fix round O's
`EXTRA4_` prefix found - but in a campaign level it names node 1 of a mesh tree
that has 1 node, so the skeleton test refuses it. RICHCUT2 is the exception:
its own `COMPASS_ITEM` has 2 nodes, the model fits, and it is the one HD ring
slot that cutscene level has. One more slot stays classic, in RICH1 alone, and
for a different reason - the remaster ships no model for it at all
(`ModelProbe`: "asks for `<NAME>.TRM`, which is in no ITEM folder"):
`PUZZLE_HOLE8`, the keypad. Everything else on the ring is HD. Counted over
exactly the object slots `inventry_objects_list` can name, with `ModelProbe
-level <STEM> -classic <vanilla .TRC> -all -nodraw` on all fifteen VANILLA
levels (the `.TRC` files of the checksum list, not a randomized run):

| Level | HD | classic | Level | HD | classic |
|---|---|---|---|---|---|
| ANDREA1 | 17 | 3 | JOBY4 | 15 | 3 |
| ANDREA2 | 19 | 3 | JOBY5 | 20 | 3 |
| ANDREA3 | 23 | 3 | RICH1 | 15 | 4 |
| ANDY1 | 10 | 3 | RICH2 | 14 | 3 |
| ANDY2 | 6 | 3 | RICH3 | 14 | 3 |
| ANDY3 | 10 | 3 | RICHCUT2 | 1 | 0 |
| JOBY2 | 15 | 3 | TITLE | 0 | 0 |
| JOBY3 | 22 | 3 | | | |

TITLE reads 0 and 0 because the only ring slot it holds at all,
`PICKUP_ITEM4`, is a DUMMY there and draws nothing on either path.

A RANDOMIZED level carries more, because the randomizer scatters weapons,
ammunition and keys into levels that never had them: the same count on one
randomized run reads 21 for ANDREA1, 19 for ANDY2 and 25 for RICH3, and it adds
one classic slot to JOBY3 - `KEY_ITEM8`, which the randomizer's own key chain
puts there and which the remaster has no model for either. The classic three
are the same in both cases; the number that moves is the HD one, and it moves
with the seed, which is why the table above is the vanilla one.

**One honest deviation, and it is the keypad.** `DrawInventoryItemMe` scales
the matrix non-uniformly for `PUZZLE_HOLE8` in keypad mode
(`ScaleCurrentMatrix`, 1.5 / 1.0 / 0.25). RICH2's keypad **is** drawn HD -
`ModelProbe RICH2`: `map PUZZLE_HOLE8_1`, 25 nodes, cover 25 - so that scale
really does reach the shader, and it is the one place left where it does.
Moving the ring's offset into the projection does not help here, because this
is a different mismatch: the classic path computes `dot(M n, d)` with no
renormalisation, and with a non-uniform scale in `M` the length of `M n` varies
from 0.25 to 1.5, while the shader must renormalise (a `.TRM` normal is a byte
triple that has been through a weighted sum of rotations). So the keypad's
shading differs slightly from the classic keypad's. One object, one screen, one
level - RICH1's keypad has no HD model at all. If the play test dislikes it,
the fix is to hand that one draw a normal scale of its own.

**The log.** With `-log`, opening the ring writes `HDLOG ring items=N hd=N
classic=N` once per OPENING - not per frame; the ring is thirty frames a second
of the same six objects - and one line per object: `HDLOG ring slot=<n>
name=<sym> model=<file> nodes=<n> mask=<hex> z=<view z> via=<rule>`, where
`via` is fix round O's own bind rule for an HD item and the refusal reason for
a classic one. `z` is node 0's view depth: the push down +z the ring gives the
item, which is `INVOBJ::scale1`, plus the item's own root offset out of the
animation frame. `scale1` is in this tree - the table in `newinv2.cpp` holds
the built-in value (`0x3E8` for the pistols, `0x898` for a passport page) and
`init_new_inventry` moves the binoculars in code - but it is not fixed:
`LoadGameflow` (`game\gameflow.cpp:455`) overwrites it out of `SCRIPT.DAT` for
every slot a level names, which is every key, puzzle, pickup and examine item.
So the number is per level, and the line says it rather than the manual
guessing at it. It is also what decides that a ring item stands in front of the
backdrop's 0.995.

**The tool.** `ModelProbe` gains three switches
(`Preparation\Tools\Diagnose\ModelProbe.cpp`, built with `bmodel.bat`):
`-ringcheck`, the proof above in both halves - the place, and the light against
the shear road it replaced - which needs no game and no level; `-ring <x> <y>`,
which draws the model where the ring would put it by the engine's own road (the
matrices untouched, `HDModelOffCentre` for the offset), so the arithmetic can
be looked at as well as counted; and `-ringlight <bright>`, which lights the
model the way the ring lights it and nothing else, so a ring object can be
photographed offline. `-ring` with `-ringlight` at several places is the
end-to-end proof that the ring's light does not depend on where the item
stands.

**Classic path.** Untouched. Every line added to `newinv2.cpp` is behind
`HDInvBegin`, which returns false on its second test with `hd_enable` or
`hd_objects` off; `g_ring_frame` is set after that test, so `hd_level.cpp`'s
bulb line and `hd_object.cpp`'s log block are never reached either. With HD off
the four call sites run the same four `phd_PutPolygonsPickup` calls in the same
order with the same arguments. `hd_model.cpp`'s new `HDModelOffCentre` is
inside the HD bracket by definition, and with `(0, 0)` - which is every object
in the world, on every frame - it returns before it touches a register, so the
world path pays nothing for the ring either.

---

---

#### Fix round Q - the font in HD (#123)

Every line of text the game draws - the title menu, the names under the
inventory ring, the options pages, the load and save lists with their
"(quicksave)" line, the statistics, the HUD's ammunition counter, the name of
a pickup, the subtitles of a cutscene - comes out of ONE 256 x 256 page and
ONE table of 106 rectangles. So one branch in `DrawChar` covers all of them,
and there is no second text path anywhere in the engine to forget.

**Measured first, and the order matters, because two of these answers moved
the design.**

*The font page is the same in every level.* `FontProbe -pages` over all
sixteen shipped `.TRC` files, vanilla copies: every one of them carries the
same font page, md5 `191cfc2fdba5b4c54024361c463d7537`, the title included.
So the match runs ONCE in a session and every further level load is a
comparison of that md5 (`hd_font.cpp`, `HDFontLevelPage`). It is keyed on the
page and not on a flag on purpose: a level file that ever carried a different
font page would be matched again rather than drawn with a table belonging to
another alphabet.

*The remaster's atlas is not the classic page, and it is not the small
picture times eight either.* The bundle ships `5\TEX\FONT0_OG.DDS` - 256 x
256, uncompressed B8G8R8A8, the classic glyph bitmaps re-typeset in ASCII
order - and `5\TEX\FONT0.DDS` - 2048 x 2048, BC7, six mip levels, the same
alphabet in the same places at eight times the size. The big one is a NEW
drawing: shrunk back to 256 with an 8 x 8 box filter it differs from the small
one in **12.6 %** of the texels of the matched rectangles, its letters reach up
to three classic texels past the eightfold rectangle, and it carries a dark
rim and a drop shadow the small one does not have (measured over the 93
matched glyphs: fully opaque texels have a mean brightness of 224 of 255, the
half-transparent ones a mean of 44). That is the look the remaster's own
screens have - white letters with a dark edge - and it is what makes the
letters readable over a bright wall.

*The ink is WHITE, so `FontShades` still works.* The engine multiplies the
texture by the vertex colour and adds the specular half
(`function_table.cpp`, `D3DTOP_MODULATE` with `D3DTA_DIFFUSE`), which is how a
menu title comes out orange and a locked options row red. White ink takes that
colour exactly as the classic page's white ink does; the dark rim stays dark
whatever it is multiplied by, which is the point of a rim. **Nothing about
`FontShades`, `PrintString`'s flags or the colours changed, and nothing needed
to.**

*And the alpha test is already harmless.* This engine sets `D3DRS_ALPHAREF` 0
with `D3DCMP_NOTEQUAL` (`specific\function_table.cpp:112`), so the test throws
away only what is completely transparent; every antialiased edge texel passes.
The soft edges of the atlas need no state change to survive - the classic path
already blends. (TROne had to switch their `alpha < 0.5` discard off for the
same reason; we have nothing to switch off.)

**How a glyph is found.** The classic rectangle's ink mask is slid over the
`_OG` atlas and scored intersection over union
(`specific\hd\hd_fontmatch.cpp`, `HDFontMatchAll`; the idea is TROne's, see
below). A hit is accepted at 0.94 and above, which is their threshold and
their reason for it: a similar character is the wrong character.

Result, `FontProbe -match`: **93 of the 106 rectangles found in `FONT0`, 92 of
them at score 1.000** - the remaster took the glyph bitmaps over texel for
texel, and the one at 0.975 is the K. The thirteen that are not found are the
twelve PlayStation pad icons, which the remaster's atlas does not carry at all
(where they would be it has ammunition and arrow sprites), and the c-cedilla
at 0.895. Each of those keeps the classic picture on its own, without
affecting any other letter.

`FONT1` is checked in the same run and comes back with 22 - and the pair
picture says why: it is a **Cyrillic** alphabet, and its 22 are the letters
Cyrillic and Latin draw alike - measured, `FontProbe -match`: `H I K N O R S T
U n o r s t u` and seven punctuation marks. Which of the two is used is
decided by that count and not by the file's name, so an installation that
ships them the other way round still works.

**How a glyph is drawn**, `game\text.cpp`, `DrawChar`:74-185. The atlas is
the classic page at eight times the size, so a NORMALISED coordinate on the
one is a normalised coordinate on the other and the glyph sits at the same
place plus a whole number of classic texels. The UV is therefore not
recomputed but SHIFTED by that number. Nothing else is touched, so nothing can
move, grow or shrink: `FontProbe -sample` renders a line through the very same
arithmetic classic and HD and reports the texels a screen pixel walks in both,
and the two agree to **1.6e-06 relative** - which is float noise and not a
difference. Position, size, `FontShades` colour and specular are the classic
ones because they are the classic code.

**The margin, and why it is per glyph.** The classic arithmetic samples the
rectangle inset by half a texel on each side, which the classic page needs -
it packs its glyphs with no gap at all. At eight times the size that inset is
four HD texels and it cuts off exactly the antialiased rim and the drop
shadow: measured, **12.6 % of a glyph's ink**. So the window is widened AND the
quad is widened by the same fraction of itself - TROne's rule, and the reason
it works is that widening both in one ratio changes what is visible and not
where anything is.

A uniform margin was built first and it was wrong, and the picture said so
before any number did. The measured trade-off (`FontProbe -atlas`) reads:

| margin | of the glyph's own ink | of its white ink | foreign ink, worst glyph |
|---|---|---|---|
| 0.0 texels | 87.4 % | 85.8 % | 1.3 % |
| 0.5 | 94.5 % | 93.4 % | 1.8 % |
| 1.0 | 97.8 % | 97.4 % | 1.9 % |
| 1.5 | 99.1 % | 99.0 % | 3.0 % |
| 2.0 | 99.6 % | 99.6 % | 7.6 % |

which looks like "1.0 and be done with it" - but the atlas packs the small
marks close together, and at every margin from half a texel upwards a slice of
the neighbouring tick appears beside the apostrophe and beside the comma,
plainly visible in a rendered line at 1920 x 1080. The averages hide it
because three glyphs out of ninety-three cannot move the average.

So `HDFontMargins` works out per glyph how far the window can be opened before
it touches another letter, and 1.0 is only the ceiling. Over the shipped
bundle: **90 of the 93 get the whole texel, three get part of it - the
apostrophe 0.25, the asterisk and the comma 0.125 - and none gets zero.**

Getting that routine right took three measurements, each of them a wrong
version first:

- *Not over alpha.* A flood fill over "is there ink here" walks from a letter
  into its own drop shadow and straight on into the next letter's, and then
  reports that there is nothing else nearby - for exactly the tightly packed
  marks that need the care. It fills over WHITE ink instead, which the dark
  shadow between two letters separates.
- *Not on the small picture.* There, ink outside the matched rectangle belongs
  to another letter by construction, which is tempting - but the big picture
  is a new drawing whose letters are larger, so the room the small one has is
  not the room the big one has.
- *Not seeded from the whole rectangle.* Because the big picture's letters are
  larger, a neighbour can reach a texel or two INTO the matched rectangle -
  the second tick of the double quote does - and seeding the rectangle floods
  the neighbour and calls it part of this letter. The seeds come from the
  classic page: a texel that carries ink THERE is this letter beyond doubt,
  and the eight by eight block under it on the atlas is this letter's white.

**Accents keep their classic arithmetic.** `PrintString` composes an accented
letter from two ordinary glyphs - the base at the letter's place and the mark
at `def->w / 2 + x - 3, y + def->YOffset` over it - and each of the two goes
through `DrawChar` on its own, so each is relocated on its own and the
relative position between them is the classic one. The German umlauts and the
sz are in the sample picture; the dieresis is `CharDef[61]` and it matches at
1.000 like the rest.

**How the quad reaches the atlas.** `AddQuadClippedSorted` knows a texture
only as a tpage, and the tpage is an index into `Textures[]` - which must not
grow, because the font, the sky and the two title pages are addressed as
`nTextures` minus a small number and every one of them would move
(MODIFICATIONS.md:265-270, and the same note over the water frame pages in
`hd_level.cpp`). So the glyph quad carries `HD_FONT_TPAGE` = 4096, which is
not an index at all, and `HWR_DrawSortList` (`specific\polyinsert.cpp`) turns
it into the font texture before any of its cases can read `Textures[4096]`.
Nothing else in the engine can ever write that number: a tpage is otherwise
always an index into `Textures[]`, and `LoadTextures` refuses a level with
more than the 256 entries that array has. The sort list already groups by
tpage, so every glyph of a frame ends up in one draw call without a line of
batching code.

**Mip levels and a LOD bias, and they are not decoration.** At 1920 x 1080 a
thirteen-texel glyph is drawn 58 pixels high out of 104 atlas texels, so the
atlas is MINIFIED by 1.8 - by 2.7 at 1280 x 720 and by 1.35 at 2560 x 1440.
Without mips that is a bilinear undersample and thin strokes crawl. The whole
six-level chain out of the `.DDS` is uploaded and the draw sets
`D3DTEXF_LINEAR` with `D3DSAMP_MIPMAPLODBIAS` -0.7, which lands at level 0.14
at 1080p and at 0 at 1440p: sharp instead of soft, and the states are put back
the way the classic path keeps them the moment the call returns.

**Where it lives.**

| | |
|---|---|
| `game\chardef.inc` | the 106 rectangles, moved out of `text.cpp` unchanged so that the engine, the HD font and the tool `FontProbe` read the same numbers rather than three copies of them. `game\accent.inc` is the same for the 46 accented letters. |
| `specific\hd\hd_fontmatch.cpp` | the matcher, the margins, the uncompressed-DDS reader and an MD5. No engine dependency at all, so `FontProbe` compiles this very file - the deal `bc7.cpp` already has with `DdsProbe`. |
| `specific\hd\hd_font.cpp` | the engine side: finds the atlas, runs the match on the load thread, makes the texture on the main thread, and answers `DrawChar`'s one question. |
| `game\text.cpp` | `DrawChar`, one branch. |
| `specific\polyinsert.cpp` | `HWR_DrawSortList`, one branch. |
| `specific\file.cpp` | two lines: the font page to the matcher at the place it is read, and `HDFontBuild` beside `HDLevelBuild`. |

**The log.** `HDLOG font atlas=FONT0 matched=93/106 below=13 page=<md5>`, one
line per glyph that did not make it with its score, and one line saying how
many glyphs got the whole margin. It stays, like every other seam.

**Classic path.** Untouched. `HDFontLevelPage` returns at its first line with
`hd_enable` off, so no file is opened and not a byte of the page is copied;
`g_matched` stays false, so `HDFontBuild` returns, so there is no texture, so
`HDFontGlyphAt` returns 0 on its first test, so `DrawChar` runs the same
arithmetic it ran before with the same `tex.tpage = nTextures - 2` - and
`HWR_DrawSortList`'s new branch tests for a tpage no quad in the engine can
then carry. `fps_mode` is not touched anywhere in this round.

---

#### Fix round P2 - the pickup display in HD (#123)

The little turning object in the bottom right corner after Lara picks something
up. Fix round P left it classic and said why; the play test's picture said what
that costs - Lara in her JOBY outfit, an HD room around her, an HD gun in her
hand, and the 1999 red ammunition box turning in the corner.

**Measured first, and one of the three answers changed the design.**

*The order in the frame, and it needs nothing.* `S_DrawPickup`
(`specific\output.cpp:66`) is called from `S_OutputPolyList` at line 1860, and
the block it sits in is below the frame's SECOND depth clear (`output.cpp:1841`,
the one fix round #127 kept, which separates the world from the binocular mask,
the display and the fade) and above `Fade()` at 1874. So the display stands over
the whole world by construction and under everything the fade block draws - on
the classic path and on this one alike, because both write depth into a buffer
that was cleared a few lines earlier and nothing else is drawn into it. The
classic display goes through the ordinary sort list with the TEXTURE's own
`drawtype`, which for an opaque pickup is 0, and `HWR_DrawSortList` case 0
(`specific\polyinsert.cpp:103`) draws it with the depth write on, the alpha test
off and the alpha blend off. The HD bracket's own state is the same picture:
`ZENABLE` on, `ZFUNC` LESSEQUAL, `ZWRITEENABLE` on, the alpha test off because
the shader clips itself. The depth VALUE is the same too, and that was already
proved: both paths write `a - b/z` for the same point (`HDRenderSelfTest`; the
classic half is `p->sz = f_a - f_boo * v->rhw` in `polyinsert.cpp` with
`rhw = f_moneopersp * f_mpersp / z`). No per-item depth clear, no reordering, no
new rule.

*Which objects can appear there, and every one of them has a model.* The display
draws `inventry_objects_list[convert_obj_to_invobj(object_number)]`, so the only
slots it can ever name are the ones that table holds - and of those, four can
never be picked up (the compass, the passport's two pages, the keypad). What
fills it is `game\pickup.cpp` (`:253` PUZZLE_ITEM1 at a keyhole, `:412` a
searched object's contents, `:1013` the burning torch, `:1022`/`:1101`/`:1139`
the object itself), `game\deltapak.cpp` (`:1785` PICKUP_ITEM1, `:1951`
KEY_ITEM7), `game\newinv2.cpp`'s secret bonus, and `AddDisplayPickup`'s own
crane substitution (PUZZLE_ITEM1/3 to CROWBAR_ITEM in JOBY3 and ANDY3). Counted
with `ModelProbe -level <STEM> -classic <vanilla .TRC> -all -nodraw` over all
fifteen vanilla levels, restricted to exactly those slots:

| Level | slots | HD | classic | Level | slots | HD | classic |
|---|---|---|---|---|---|---|---|
| ANDREA1 | 17 | 17 | 0 | JOBY4 | 15 | 15 | 0 |
| ANDREA2 | 19 | 19 | 0 | JOBY5 | 20 | 20 | 0 |
| ANDREA3 | 23 | 23 | 0 | RICH1 | 15 | 15 | 0 |
| ANDY1 | 10 | 10 | 0 | RICH2 | 13 | 13 | 0 |
| ANDY2 | 6 | 6 | 0 | RICH3 | 14 | 14 | 0 |
| ANDY3 | 10 | 10 | 0 | RICHCUT2 | 0 | 0 | 0 |
| JOBY2 | 15 | 15 | 0 | TITLE | 1 | 0 | 1 |
| JOBY3 | 22 | 22 | 0 | | | | |

Not one classic object in any playable level - which is the difference from the
ring, where the compass and the two passport pages stay classic in every level.
The ring can show three things nobody can pick up; the display cannot. TITLE's
one slot is `PICKUP_ITEM4` and it is a DUMMY there, drawing nothing on either
path - and the title has no pickups anyway. RICHCUT2 is a cutscene level and
holds none of these slots at all. A randomized level moves the HD count up and
never the classic one, for the same reason the ring's does: the randomizer
scatters weapons and ammunition into levels that never carried them, and those
have models.

*And the light is NOT the ring's.* This is the measurement that changed the
build. `aTransformLightClipMesh` (`specific\output.cpp:150-157`) has one line
for a sun's strength:

    if (!InventoryActive) val *= 0.75F; else val += val;

("fucking shit", says Core's own comment beside it.) The ring is drawn with
`InventoryActive` at 1 and the display with it at 0, so the ring's sun is
DOUBLED and the display's is three quarters of itself - a factor of 2.667
between them. Everything else about the two lights is identical, because both
come out of the same `phd_PutPolygonsPickup`: an ambient of 8/8/8 and ONE sun
along `(0, -50, -100)` normalised over `sqrt(12500)`. The colour is
`pcbright`, and for the display it is always `0x7F7F7F`:
`S_DrawPickup` passes `bright` 0 and `DrawThreeDeeObject2D` turns 0 into
`0x7F7F7F` (`game\newinv2.cpp:916`). Handing the display the ring's `sunScale`
of 2.0 would have made it visibly brighter than the classic display it
replaces, and it is exactly the kind of number that gets carried over without
being asked about.

**The decision: draw it on the spot, not on the list.** Fix round P's argument
against the display was about the LIST and it is still true - `S_DrawPickup`
runs after `HDFrameDraw` has drawn this frame's HD list and after
`HDFrameDrawGlow` has finished with it, so an item pushed there would suppress
its own classic polygons and be drawn by nobody. P2 does not push it. The node
walk collects the matrices exactly as the ring's does, through the SAME four
`phd_PutPolygonsPickup` call sites - the seam does not grow - and `HDInvEnd`
opens a bracket of its own, draws the one object and closes it again. The
frame's list is not touched and `g_count` is not incremented, so the list
`HDObjectFrameReset` empties on the next picture is still the world's alone.

That bracket is not a new one: `HDFramePickupBegin` / `HDFramePickupEnd`
(`specific\hd\hd_level.cpp`) are `HDRenderFrameBegin` / `HDRenderFrameEnd`, the
very pair the rooms, the objects and the ring are drawn between, so there is
one HD path and one list of device states. It is also exactly what `ModelProbe`
has always done for a single model, which is why the diagnostic tool can
measure the real bracket rather than a copy of it.

**Two things in that bracket are the display's own, and both were measured
rather than inherited.**

*The projection is read at the call.* Not `HDFrameProjectionInfo`:
`HDFrameCaptureProjection` takes the field of view the WORLD was collected
with, and `AlterFOV` moves `f_persp` inside a frame whenever the binoculars are
up. The classic display projects with whatever `f_mpersp` holds at the moment
`S_DrawPickup` runs, and `f_mpersp` is `f_persp` to the bit (`specific\3dmath.cpp`,
`AlterFOV` sets both from the same line), so `pickup_begin` reads `f_persp`,
`f_centerx`, `f_centery`, `f_a` and `f_b` there and hands them over.

*The off-centre offset goes into the PROJECTION*, which is fix round P's whole
lesson and needed no second thought:

    kx = (X - centreX) / persp,   ky = (Y - centreY) / persp

with `X` = `phd_winwidth - GetFixedScale(80) + PickupX` and `Y` =
`phd_winheight - GetFixedScale(75)` plus `INVOBJ::yoff`, which is exactly what
the classic path hands `phd_PutPolygonsPickup` as its temporary projection
centre. `HDModelOffCentre` writes `_31 = kx * _11` and `_32 = ky * _22` for the
length of one draw call and the node matrices reach the card as the engine's
own. Putting it into the matrices as a shear instead would tilt every normal
the shader skins through them; fix round P measured that at 45 % of brightness
across a screen and this round does not repeat it.

**The slide comes for free, and there is no fade to copy.** `DrawPickups`
(`game\health.cpp:212`) draws nothing at all despite its name: it is the
animation - how long the icon stays (`pu->life`, 45), how far it has slid in
(`PickupX`, from `GetFixedScale(128)` to 0) and which of the eight queued
pickups is up. `PickupX` moves `X`, `X` moves `kx`, `kx` moves the projection,
and the object walks out of the picture exactly as it always did. There is no
alpha ramp anywhere in either path.

**Sixty frames a second changes nothing here either.** The spin is
`(GnFrameCounter & 0x7F) << 9` and `GnFrameCounter` is stepped once per LOGIC
TICK since #148 order 3 (`output.cpp:1509`, `if (!InterpIsTweenFrame())`), so
the display turns at thirty steps a second at either rate - and both paths read
the same `yrot`, because it is computed in `S_DrawPickup` and travels down the
matrix stack. `DrawPickups` returns early on a tween frame, so the slide is the
same fraction of a second at either rate. Not a line of this round reads the
frame rate.

**What was changed.**

| File | What |
|---|---|
| `specific\hd\hd_object.cpp` | `HDInvBegin` routes on `InventoryActive` instead of refusing; `pickup_begin` (the display's half of it, every refusal settled before the answer goes back) and `pickup_draw` (the immediate draw); `HDInvNode` and `HDInvEnd` carry both modes; the sun scale is `pickup ? 0.75f : 2.0f`; the level's own seam at the end of `HDObjectLevelBuild`. |
| `specific\hd\hd_object.h` | the block over `HDInvBegin` - what round P refused, what P2 does instead, and the three things that are the display's own. |
| `specific\hd\hd_level.cpp/.h` | `HDFramePickupBegin` / `HDFramePickupEnd`: the bracket, the display's own projection, this frame's lighting with no fog bulbs and with the material clock READ rather than stepped. |
| `specific\hd\hd_render.cpp/.h` | `HDRenderInFrame()` - `HDRenderFrameBegin` refuses to nest and says nothing about it, and the display has to know before it suppresses its own classic polygons. |
| `specific\hd\hd_model.cpp` | one comment: `HDModelOffCentre` has two callers now. |
| `game\newinv2.cpp` | one comment at the seam - the four call sites are unchanged. |
| `Preparation\Tools\Diagnose\ModelProbe.cpp` | `-pickupcheck`, and `ShotGrab` / the state snapshot it needs. |

**The proof.** `ModelProbe -pickupcheck`, in three halves.

*The place*, arithmetically, as `-ringcheck` does it for the ring: the model's
bounding rectangle put on the screen twice - once by the classic off-centre
projection and once through the engine's own `HDModelOffCentreMatrix` and the
viewport - at 1920x1080 and 2560x1440, at rotation 0 and 90 degrees, and with
the slide both fully in and fully out. Eight places per object; the two roads
agree in position AND in width and height to a worst **0.00008 px** (budget 1
px) on `BIGMEDI_ITEM`, `KEY_ITEM1` and `HK_AMMO_ITEM` alike. The rectangle is
987.76 px wide at 1920x1080 and 1317.01 at 2560x1440, which is 1.3333 = 1440 /
1080 exactly: the display scales with the picture's HEIGHT and gains nothing
from the extra width, on both paths.

*The light*, end to end and by picture: the same object rendered twice at
1920x1080 with the display's own light - once about the screen centre, once
about the display's corner (1740, 911) - and then compared where the two
overlap after the whole known shift of 780, 371 px. The offset is a whole
number of pixels, so the two must be the SAME picture moved:

| Object | object pixels | only in one | mean centre | mean corner | apart | > 2/255 |
|---|---|---|---|---|---|---|
| `BIGMEDI_ITEM` | 5642 | 0 | 37.4751 | 37.4742 | 0.0024 % | 0 of 5642 |
| `KEY_ITEM1` | 1498 | 0 | 48.2419 | 48.2405 | 0.0028 % | 0 of 1498 |
| `HK_AMMO_ITEM` | 1486 | 0 | 32.3739 | 32.3724 | 0.0049 % | 5 of 1486 |

Not one pixel of any of the three is in one picture and not the other, and the
bounding boxes are shifted by exactly 780, 371 with identical sizes (89x70,
65x93, 75x42). The handful of pixels that differ at all are edge texels: the
projection reaches the offset through `_31 = kx * _11` in single precision
rather than through an integer move, so an edge sample can land a hundredth of
a pixel further along. What is being ruled out is a light that depends on WHERE
the object stands, which the shear of fix round P had at 45 %.

*The state bracket*: every watched device state, sampler state and texture stage
state read before and after the corner draw, with the wider list `RoomProbe`
uses rather than the one `hd_render` saves - **0 of 58 changed**, on all three
objects. The `DrawSortList` that follows the display in `S_OutputPolyList` runs
unchanged; with the object drawn HD its own list is empty and `DrawSortList`
returns at its first line without touching a state (`polyinsert.cpp:236`), and
`SortPolyList(0, ...)` returns at its own.

**The log.** With `-log`, a level writes one `HDLOG pickupdisplay level=<stem>
slots=N hd=N classic=<list>` line at build time, naming every slot the display
can show and every one of them without a model; and one
`HDLOG pickupdisplay slot=<n>(<name>) hd=<0|1> x=<n> y=<n>` line per object, the
first time that object is shown in the level - the display can show the same
medipack two hundred times and the line would be the same line. Both stay,
behind `-log`, like every other seam.

**Classic path.** Untouched, and at the same line as before: `HDInvBegin` still
answers false at `if (!g_active || !tomb5.hd_objects)` with the HD path off, so
`DrawInventoryItemMe` runs the walk it always ran and puts the same classic
polygons into the same sort list. What changed is only what happens AFTER that
test, and only for a caller that used to be turned away one line earlier.
`fps_mode` is not read anywhere in this round.

---

#### Fix round R - the half turn at 60 fps: the roll (#148)

The play test of fix round T2: *"Classic textures at 60 FPS have the problem
with the roll too. So it really is the interpolation."* Deferred then, by the
user's decision; taken up now, after he saw the roll run clean in Todesengel's
TROne, which has carried 60 fps since its stand of 2026-09-06.

**Measured, TROne first.** `host\render\frame_interp.h/.cpp` and
`frame_record.h` (GPLv3): the logic stays at 30 Hz, the draw list of one tick
is recorded with its finished VIEW-space matrices per node and replayed twice,
once blended at t = 0.5 against the previous tick. The camera delta is
recovered from a static room transform, rotation goes by quaternion slerp on
the shortest arc, and four thresholds (512 units, 40 degrees for the camera,
512 units, 120 degrees for an object) keep the blend off jumps. There is no
rule about the roll at all. The roll is clean there by construction: the
blend sees the finished node matrix, in which the item's half turn and the
animation's opposite half have already cancelled, so per node the matrix moves
only by the body's real motion between two ticks.

**Measured, ours.** The half turn is `turn180_effect` (`game\effects.cpp:142`):
`x_rot = -x_rot; y_rot -= 0x8000`, fired by an animation command. `AnimCmd`
over Lara (slot 0) in ANDREA1 names seven animations, six of them in every
level and one the level's own: 48 (state 45 `AS_ROLL`, rate 4, its last
frame), 147 (the second half of the roll - `ANIM_ROLL` 146 continues into it;
rate 1, its first frame), 205 (state 66 `AS_WATERROLL`, rate 1, its second
frame), 207 and 210 (state 3, rates 4 and 1, last frame), 212 (state 25, rate
4, last frame) and 451 (state 120, rate 2, last frame, ANDREA1 only). On that
tick the animation's own root bone turns the other half, so at thirty presents
a second the picture is continuous.

Our interpolator blends the root and the pose apart. `InterpLerpAngle` takes
exactly half a turn the arbitrary anticlockwise way - its own comment called
that "a snap in the animation, not a rotation" and lerped it anyway - and the
pose blend is refused on that same tick: by rule K6 where the animation changes
at the flip (five of the seven fire on their last frame), by `InterpPoseFar`'s
ninety degrees otherwise. So the live pose is drawn under a root that stands
ninety degrees off, and Lara is sideways for one tween frame. `fps_mode 0` has
no tween frame, which is why thirty never showed it.

**Fix.** `InterpHalfTurn` (`game\interpolate_math.h`): a y difference of
exactly 32768 between the two ticks. `ApplyNode` (`game\interpolate.cpp`)
blends the position as before and, for such a node, leaves the whole rotation
at the live value - the effect negates `x_rot` in the same breath, so the
rotation goes as one. The tween frame at the flip shows the new tick's picture:
a held frame where there was a sideways one, and every other frame of the roll
blends exactly as before. The `INTERPLOG` line gains `halfturn=N` per second - one per roll, and the
braid's own flips besides (fix round W's addendum below). The rule is the one `InterpLerpAngle`'s comment argued for and did
not enforce; TROne needs none, and nothing of its blend was taken
(`Preparation\60FPS-Plan.md`, 0.5).

**Not done, on purpose.** Two larger ways were priced and set aside: blending
the composite yaw of item and root bone (one frame of facing smoothness in the
middle of a somersault, at the price of a gimbal case where the root bone is
pitched), and blending per-node matrices as TROne does (a rewrite of the pose
path, and a decision of its own about animation transitions in general, which
rule K6 today holds at thirty for one frame each).

**Proof.** `InterpTest`, section `angles`: seven new checks - exactly half a
turn either way, and `turn180_effect` from every angle on the 512-unit lattice,
is a half turn; one unit short of it is not; 289 checks, 0 failed. `be.bat` 0
errors. `fps_mode 0` byte-identical: `ApplyNode` runs on tween frames only.

**Play test.** The roll at 60 fps in third person, several times, on flat
ground and on a slope; the roll in water; the twist of the jump back; first
person; then 30 fps as the counter-check. `INTERPLOG`'s `halfturn=` reads one
per roll.

---

#### Fix round W - the ninth step of the screen fade: the black tick in every fade-in at 60 fps (#148)

The play test of fix round R, at a 165 Hz panel in `fps_mode 1`: *"when I
load the game the picture flickers black about three times, level- and
movement-independent, not at 30 fps"*, then, with three screenshots and a
video: *"at the fade-in"*, and *"the other flicker looks the same in the
game"*.

**Measured, the video first.** 670 frames at 60 in 11.17 s, three load
cycles. In each: the menu's fade to black, the loading picture, the black of
the load itself, then the level's fade-in begins (mean brightness 4.6 of 255)
- and 0.22 s into it TWO frames of pure black (mean 0.0), then the fade-in
goes on (12.1). Two frames at sixty are 33 ms: one logic tick. Not a single
present, a whole tick's worth of them.

**Measured, the log.** Nothing in the seams: no `ROOMLIST` with `rooms=0`,
all 13 `ROOMFLASH` lines `empty` (the rim of the picture, T3's finding),
`TICKLOG fade=16 screenfade=9` at every level start, `Fade()` and
`DoScreenFade()` drawing the same grey on in-between frames (R1, R2). The
room walk and the fades' walks were clean. (`Non present sample:N` in the
same log is `game\sound.cpp`'s line about a missing sound sample, nothing
to do with presents.)

**Measured, the code.** `SetFade(start, end)` (`specific\specificfx.cpp`)
sets `FadeStep = (end - start) >> 3` and `DoScreenFade` walks `FadeVal` by
it once per logic tick, reads `a = FadeVal << 24` BEFORE the step, and on the
ninth call (`FadeCnt > 8`) puts `FadeEnd` over `a` and sets `DoFade = 2`. For
the fade-in every level starts with (`SetFade(255, 0)`, `game\gameflow.cpp`
`DoLevel`; also `DoTitle` and `RenderLoadPic`) the step is -32, and the
eighth step lands on 255 - 256 = **-1**, which shifted into the colour is
`0xFF000000`: alpha 255, black. At thirty presents a second that value is
read exactly once - by the ninth call, which overrides it - so the thirty
hertz picture never shows it. At sixty and above, every in-between frame
between the eighth and the ninth tick draws `a = FadeVal << 24` as it stands:
one tick of full black, Lara included, in the middle of every fade-in. That
is the video's two frames, the "not at 30", the "level-independent", and the
"at the fade-in". The fade-out direction (`SetFade(0, 255)`, step 31) lands
on 248 and never overshoots; `Fade()`'s grey tile clamps inside its own step
and never did.

**Fix.** `InterpFadeClamp` (`game\interpolate_math.h`): once the walked value
has passed `end` it IS `end`. `DoScreenFade` applies it right after the step.
The ninth tick drew `end` anyway, so no thirty hertz picture changes - the
proof plays the nine ticks through in both directions and compares them with
what they always drew - and the in-between frames after the eighth tick now
draw `end` as well. Two lines in the engine.

**Proof.** `InterpTest`, section `fade`: seven new checks - the nine ticks of
255 -> 0 and 0 -> 255 draw what they always drew, the eighth step of 255 -> 0
lands on -1 unclamped and the in-between frame after it now draws 0, the
eighth step of 0 -> 255 lands on 248 and stays 248, and -1 shifted into the
colour is alpha 255 - 296 checks, 0 failed. `be.bat` 0 errors. `fps_mode 0`
byte-identical by the same arithmetic.

**Play test.** Load a savegame at 60 (or whatever the panel gives) several
times: the fade-in must run through without a black tick; the title's start
and the loading picture the same; 30 as the counter-check. The flickers the
user saw "randomly in between" and says look the same: after this build, say
whether they are still there. `SetFade(255, 0)` runs at a level start, the
title and the loading picture and nowhere else, so a black tick anywhere
else is NOT this and wants the present seam that fix round V was going to
build (see below).

**Addendum to fix round R.** The same log shows `halfturn=` five or six a
second while Lara stands still, and 39 in one second of ANDREA1. That is not
the roll: it is the BRAID. `HAIRLOG` logs `dyaw 180` on the same seconds -
when a braid segment hangs straight down its yaw is undefined and flips by
exactly half a turn from one tick to the next, and R's rule now holds those
flips at the live value instead of blending them a quarter turn sideways.
Harmless (a vertical segment turning about its own axis), and if anything an
improvement; the line "one per roll" in fix round R was corrected to say so.

**Noted, not changed.** `S_DrawLoadBar` (`specific\drawbars.cpp`) draws only
the bar onto whatever the back buffer holds and presents, with
`D3DSWAPEFFECT_DISCARD` and one back buffer (`specific\dxshell.cpp`), whose
content after a present the D3D9 contract calls undefined. It has always
been so and no play test has shown it; it is on the audit's list, with the
present seam (a line per present that drew nothing, or nothing but a fade
tile) as the way to see it if it ever does.

---

#### Fix round X - the camera hand-over is not a cut: drawing a weapon at 60 fps (#148)

The play test of fix round W, third person, `fps_mode 1`: *"in any movement,
when I draw the weapon, I get a very short camera reset - the picture jumps
irregularly back and forth. In first person this does not happen, there it
is all smooth."*

**Measured.** The log of that session (JOBY4, JOBY2): 45 camera cuts, every
one of them of the `type` kind - `camera.type` or `camera.old_type` changed
- none of the `eye` kind (the eye never moved more than `CAM_SNAP` in a tick)
and none of the `spot` kind; 22 of the 45 also crossed the aim threshold.
They come in bursts of five or six a second, exactly the seconds Lara draws
or holsters. `game\larafire.cpp` sets `COMBAT_CAMERA` while the guns are up
and `CalculateCamera` (`game\camera.cpp`) resets the type to chase at its
own end, so `camera.old_type` moves chase -> combat -> chase around every
draw. The engine hands the view between those cameras SMOOTHLY - the eye
walks to the new camera's place at `camera.speed`, the target is Lara's
either way - which is why the eye test never fired. The cut rule of the
interpolator (`CameraCut`, `game\interpolate.cpp`) took every type change
for a cut all the same: that tick was drawn live, the next in-between frame
was blended half a tick back, and the picture went forward, back, forward
at every draw. First person has one camera and never changes type.

**Fix.** `InterpCamTypeCut` (`game\interpolate_math.h`): a change of
`camera.type` or `camera.old_type` is a cut only when it LEAVES the smooth
family - chase, look, combat hand over to each other and never jump - i.e.
when a fixed, heavy or cinematic camera is on either side; a change of
`camera.fixed_camera` is a cut on its own as before, and the eye and aim
tests beside it still catch a real jump whatever the type says. The
hand-overs it now lets through are counted as `handover=` in the `INTERPLOG`
line, and `type=` beside it counts only the changes that leave the family.
`interpolate.cpp` static_asserts the three type values the header mirrors
against `camera_type`, because the header is built without the engine.

**What stays a cut.** A fixed camera taking over (`FIXED_CAMERA`, or
`fixed_camera` changing), a heavy camera, a cinematic, a spot camera stepping
(`spot`), the binoculars (the eye jumps to her head - `eye` and `aim`), any
eye jump over `CAM_SNAP`, any aim swing over the sphere threshold. The 22
cuts of this log that were type AND aim stay cuts on the aim: those are
ticks where the combat camera really swung the view more than the threshold
in one tick, and vanilla draws that as a step too.

**Proof.** `InterpTest`, section `camera`: twelve checks over the type table
- the six hand-overs are none, the six leaves are cuts - 308 checks, 0
failed. `be.bat` 0 errors. `fps_mode 0` byte-identical: `CameraCut` is only
asked on in-between frames.

**Play test.** Third person at 60: run, walk, jump and draw or holster the
pistols and the HK in each - no jump back and forth; look around with the
look key; a fixed camera and a flyby still cut cleanly (the courtyard of
ANDREA2, the crane of JOBY2); the binoculars up and down; then 30 as the
counter-check. `INTERPLOG`: `handover=` one or two per draw, `type=` nought
outside fixed cameras and cutscenes.

---

#### Fix round Y - ARMLOG: both arms, frame by frame (#148)

The T4 play test (build of 21:43, md5 a4ebe715) brought a new fault: *"Lara's
right arm is buggy with the shotgun and the HK, it shakes massively."* Two
videos followed, 60 Hz captures of a 165 Hz screen. The first stands still for
its first 28 frames and those frames are pixel-identical - nothing shakes while
she stands - and runs from frame 28 on, where a frame-to-frame difference of
the arm region cannot tell a shake from the run. The second, 425 frames of
JOBY2 with the HK, shows nothing in any one frame, as the user said it would:
*"the shake is so fast that every frame looks normal."* So the arms are written
down as numbers instead, and the fix waits for them.

**The seam.** `ARMLOG`, one line per drawn frame while a weapon is in her
hands or on the way there or back (`LG_READY`, `LG_DRAW_GUNS`,
`LG_UNDRAW_GUNS`, `LG_FLARE` - the test `DrawLara` itself makes for `gun`),
written right after `CalcLaraMatrices(0)` in `game\draw.cpp`, so that it reads
the VIEW set: `lara_matrices`, the table the HD body is skinned from
(`specific\hd\hd_laradraw.cpp`) and the classic body is drawn from. Three
calls in the draw's own order (`game\interpolate.cpp`): `CalcLaraMatrices`
notes which path built her (`InterpArmLogPath` - `GetFramesNow`'s `frac`, the
body's blend, the held first person body of U4, whether the HD body is on),
`Rich_CalcLaraMatrices_Interpolated` notes what it did with the arms
(`InterpArmLogNote` - `gun`, both answers of `InterpArmPrev`, the arms' alphas
and the previous tick's frames), and `InterpArmLogFrame` writes the line. A
frame built by `Rich_CalcLaraMatrices_Normal` never reaches the second call,
which the line shows as `ok=-1/-1`. Empty hands write nothing.

```
ARMLOG f=812 gc=4711 tween=1 a=0.545 path=2 blend=0.545 still=0 hd=1 lara=0/7/1 gun=5/4 wi=3/12/4 armR=3/12/11 armL=3/12/11 ok=1/1 aa=0.545/0.545 aimR=0,0,0 aimL=0,0,0 torso=0,0,0 head=0,0,0 hips=(-88,-412,1530) R=(-201,-661,1522)(-249,-486,1469)(-190,-397,1373) L=(35,-668,1516)(-4,-503,1440)(-92,-397,1369) Rx=(0.912,0.061,-0.406)
```

| field | what it says |
|---|---|
| `f` | serial of the line, one per drawn frame with a weapon |
| `gc` | `GlobalCounter`, the logic tick the frame belongs to - frames sharing it are the tween frames of one tick |
| `tween`, `a` | `InterpIsTweenFrame`, and what the frame is drawn at |
| `path`, `blend`, `still`, `hd` | `GetFramesNow`'s answer for the body (0 no pair, 1 key pair, 2 blended in time), the body's factor, the held body of U4, `HDLaraDrawActive` |
| `lara` | her animation, the frame within it, her state |
| `gun` | weapon type, `gun_status` |
| `wi` | the weapon item's animation, frame within it, state (`-1` without one) |
| `armR`, `armL` | the arm's animation, its frame, and the previous tick's frame the blend used (`-1` when it did not) |
| `ok`, `aa` | `InterpArmPrev`'s two answers and the arms' alphas |
| `aimR`, `aimL`, `torso`, `head` | the aim angles the player steers plus the head, y/x/z |
| `hips`, `R`, `L` | view-space positions of the hips, then shoulder, elbow and hand of the right arm, then of the left |
| `Rx` | the right hand matrix's first column - its own x axis, for a hand that turns rather than moves |

**Reading it.** The camera and the hips move smoothly, so the hand RELATIVE TO
THE HIPS is what the arm does. A shake is that vector going there and back
between consecutive frames - a step that turns against the previous one while
`gc` and the animation frames stand still - and the line says on which frames
it happens and what the draw did on them: tick or tween, which path, arm blend
on or refused, which frames. What the numbers say is the next round's.

**Classic path and 30.** Nothing here writes into the game: without `-log`
the three functions return at their first line, and with it they only read.
At `fps_mode 0` the line is still written (`tween=0`, `a=1.000`) and is the
reference a 60 run is held against. Build: 0 errors, 0 warnings; the game
folder was locked at build time, so the executable stayed in `release\`
(22:43:13, md5 9e0c1c41).

#### Fix round Y2 - the left arm's root: the shake of the rifles at 60 (#148)

The ARMLOG run came back within the hour (build 22:43, md5 9e0c1c41; ANDREA1
with the shotgun, first at 165 Hz, then at 30, then at 60), with the user's
verdict alongside: *"60 and 165 both equally broken, only 30 fps good."* What
the numbers say, the hand taken relative to the hips so that the camera and
the run fall out:

| frames per tick | ticks measured | left hand: ticks with a sawtooth | right hand: ticks with a sawtooth |
|---|---|---|---|
| 5 to 6 (165 Hz) | 115 | 41 (36 %) | 2 (2 %) |

A sawtooth is a hand that travels through the tween frames of one tick and
jumps the same way back on the first frame of the next. Tick 630, Lara in
animation 90 (rate 2), the shotgun idle in hand (`wi=699/0`): the LEFT hand
moves 13 to 16 units on every one of the five tween frames, 41 in all, and on
the first frame of tick 631 it jumps 80 units back; the right hand moves 5 a
frame and never jumps. The body's own factor on those frames reads `blend=`
0.086, 0.165, 0.258, 0.359, 0.438 against `a=` 0.172, 0.331, 0.515, 0.718,
0.876 - half, because the animation is rate 2. Over the whole run the two
factors are equal only in animation 0 (the run, rate 1), where nothing shakes;
in 6, 8, 10, 11, 13, 69, 88, 89, 90 and 103 they differ, and those are the
animations the videos were made in. So it is not the arm's own blend and not
the HD body: it is the joint the left arm hangs from.

**The defect (`game\delstuff.cpp`, `Rich_CalcLaraMatrices_Interpolated`, the
shotgun / HK / crossbow branch).** U3 collapsed the two stacks - `InterpArmStack`
- INSIDE the right arm's own `phd_PushMatrix_I`, on the pushed copy of the
torso level, and set the blend factor to the arm's time alpha. The right arm
was built off that collapsed joint and came out right. Then `phd_PopMatrix_I`,
and the LEFT arm was pushed off the torso level as it had been: stack one the
body's key frame A, stack two its key frame B - and blended, from there down,
at the factor `InterpArmStack` had left behind, the arm's alpha, which nothing
sets back. For a rate 1 animation the body's factor and the alpha are the same
number and the error is nought, which is why the run was clean. For every
other rate the left shoulder sat at `lerp(A, B, alpha)` while the drawn torso
sat at `lerp(A, B, blend)`: the arm drifted off the body through the tick and
snapped back on the next. The U7 note over `InterpArmStack` said the collapse
*"hangs both arm chains off that one joint"*; it did so for one of them.

**The fix.** The collapse moves up: before either arm is pushed, on the torso
level itself. Both chains then descend from the same root, the torso the
drawn body has. For the right arm nothing changes - a translation applied to
both stacks alike commutes with their blend, so collapsing before the shoulder
translate or after it is the same joint. With the HK in state 0 to 4, where
the torso collapsed once already and walked the two stacks apart through the
torso and the head, the call now takes the torso to its midpoint in time and
both arms hang off that - which is the order U7 described. `InterpArmStack`
itself is unchanged; its note says where its caller sits now.

**Proof.** The next ARMLOG run: the left hand's sawtooth share has to fall to
the right hand's, and the 30 Hz run has to read as before. The tick frames
and `fps_mode 0` never enter this branch's blend at all (`InterpArmPrev`
answers false there) and the classic build `Rich_CalcLaraMatrices_Normal` is
untouched, so thirty hertz is byte for byte what it was. Build: 0 errors, 0
warnings; the game folder was locked at build time, so the executable stayed
in `release\` (22:55:05, md5 d82d8d88).

#### Fix round T5 - capacity to the engine's room maximum and the valve at 128 (#123, #148)

Fix round T4 shipped three things at once: the reach, two counted capacity
guards, and a safety valve at 64 rooms. The play test of 2026-09-09 - two runs
of the T4 build (`PCtomb5 built Sep 9 2026 22:55:03`) on a 165 Hz panel with
HD on - says the first of the three is right, the second never fired, and the
third is set too low. The player's decision on the third was one sentence:
raise the limit straight away, so that there is headroom.

* **The two fixes hold.** The torn wall of JOBY2 and the tower of Trajan's
  Markets are both closed - *"T4 ist fixed"*.
* **Neither guard ever fired.** All **107** `ROOMWALK reach` lines of the two
  logs read `cap hit=0`.
* **But the valve was open for twenty seconds at a stretch, twice.** In JOBY2
  (log seconds 49 to 72) and in the courtyard of Trajan's Markets (seconds 113
  to 119) the per-frame `ROOMLIST` lines sit at **exactly 64 rooms** and never
  above it, with **up to 30 reach-only rooms dropped in a single frame**;
  three seconds of ANDREA2 have a mean list of `64.0`, which is every frame of
  that second at the budget. Over the twenty one-second lines that trimmed
  anything at all, **23,011 rooms were dropped**, the worst second 3,604 - 22
  a frame at 165 frames a second.
* **And that shows in the picture.** `ROOMFLASH` names three rooms of ANDREA2
  - **230** (frame 15940), **47** (15984) and **229** (16007) - missing with a
  `reach[...]` of their own, which is precisely the seam's way of saying *the
  walk reached this room and something downstream dropped it*. Distant art
  popping, which is what fix round T3 set out to end.
* **The frame had headroom while it did this.** At 64 rooms and 690 HD draws
  (the log's highest anywhere is **754**), `FPSLOG` reads `real_fps=165.1
  avg_ms=6.1 late=0`, and the `PROFLOG` line of the same second says where
  those 6.1 ms go: `frame=6.06/6.97 work=6.02/6.97 flip=5.35/6.32`. The frame
  is waiting for the display. The thirteen ANDREA2 seconds are the same -
  `real_fps=165.0 avg_ms=6.1 late=0` at 63 to 64 rooms.

So the valve is the thing to open, and while it is being opened the two arrays
under it are worth taking to the engine's own maximum rather than to the next
round figure.

---

**T5-1 - the two arrays, and why no level can fill them.**

`draw_rooms` goes from `short[100]` to **`short[kDrawRoomsMax]`, 256**, and
`draw_room_list` from a 128 entry ring to **`long[kRoomListRing]`, 512**
(`game\draw.cpp`, the note over both). Both sizes are named constants now, and
the ring's every literal went with it: the two writes (`SetRoomBounds` and
`SetRoomBoundsReach`), the two `>= 128` guards beside them and the dequeue in
`GetRoomBounds`, which is the read side and the one a half-done rename would
have left behind. The wrap is written `& (kRoomListRing - 1)` rather than
`% 128`; the two agree exactly because `room_list_start` and `room_list_end`
are reset to 0 at the top of every walk and only ever counted up, so neither
is ever negative, and the size is a power of two.

Neither number is a guess:

* **256 is the engine's own room maximum.** `BuildOutsideTable`
  (`game\setup.cpp:2418`) refuses a level with more than 256 rooms outright
  and says so in a message box. A room enters `draw_rooms` exactly once per
  frame - that is the `bound_active & 1` test in `GetRoomBounds` - so no
  loadable level can fill the array.
* **512 is twice the deepest queue that can exist.** A room is queued only
  while `bound_active & 2` is clear and queueing sets that bit, which the
  dequeue clears again: **at most one entry per room is pending at any
  moment**, so the queue's depth is bounded by the room count, 256. (A room
  may be queued again after it has been dequeued, which is why one frame can
  take more dequeues than the level has rooms - 319 is the worst measured -
  but the DEPTH cannot pass 256.) Measured against that bound, the deepest
  queue over the fifteen levels is **70**, in JOBY4. That number does not move
  with the budget and cannot: `ReachTrimRooms` runs after the walk is over, on
  the list and not on the queue, so what the queue does is the same at 64 as
  at 128.

The guards of T4-3 stay, and stay counted as `cap hit=` in `ROOMWALK`. They
are backstops now rather than limits - no level the engine will load can reach
either of them - and they are kept because "cannot happen" and "is not
counted" together are how the hole they were built for got there.

**Three tables that had to follow the list, and one that did not.** The
`ROOMLIST` note (`draw.cpp`, `RoomWalkNote`) copies `draw_rooms` into arrays
of its own, three frames deep; they were `[100]` with the count clamped to
them, so a longer list truncated silently and `ROOMFLASH` would have called
every room past the hundredth missing on every frame in a row. They are
`[kDrawRoomsMax]` now: three static notes of about 4.7 KiB, copied only while
`-log` is on. `HD_MAX_FRAME` (`specific\hd\hd_level.cpp`) is the HD path's
per-frame room table, one entry per listed room from `InsertRoom`; it was 128
against a list of 100, which was headroom, and against a budget of 128 it
would have been the exact worst case with none - so it follows to 256, at
`sizeof(FrameRoom)` = 380 bytes, 47.5 KiB more BSS. The `seen[128]` near
`draw.cpp:800` does **not** follow: it is CLIPLOG's table of (slot, anim)
pairs and has nothing to do with the walk, and neither have the `after_death`
128s or `GetRandomDraw() & 127`.

---

**T5-2 - the ordering finding of the T4 review, answered by arithmetic.**

The review of T4 left this: the capacity guards fire DURING the walk while
`ReachTrimRooms` runs only after it, so at a full `draw_rooms` a room with a
classic window - a room the walk has always drawn - could be refused while
reach-only rooms held places, and no eviction rule existed to prefer the
classic one.

With 256 places the case cannot arise. It is not that it is unlikely: one
entry per room and at most 256 rooms means the array cannot be full while any
room is still unlisted, so the guard cannot refuse anything, classic or reach.
No eviction rule was added, because there is nothing left for it to arbitrate.
The same argument closes the ring: depth at most 256 against 512 places.

The survey below is what says the arithmetic is not being leaned on alone. The
worst list of the whole game is **139 rooms** (ANDREA1) against 256, and the
deepest queue **70** against 512.

---

**T5-3 - the valve, 64 -> 128, and the column it comes out of.**

`RoomProbe -reachsurvey` again, over all fifteen levels, the same fan as T4 -
the eye at the middle of every room of the level in turn, yaw every 45 degrees
and pitch -30 to +30 every 15, 2560x1440, 280 to 10,200 frames a level. **Every
number of the T4 table came back identical**, which is what makes the three new
columns comparable with the six old ones:

| level | rooms | frames | classic mean / max | reach mean / max | over 64 | over 128 | deepest queue | tool dequeue cap |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ANDREA1 | 242 | 9,680 | 6.6 / 42 | 14.5 / **139** | 4.0 % | **0.1 %** | 19 | 0 |
| ANDREA2 | 252 | 10,080 | 8.5 / 65 | 15.2 / 111 | 3.5 % | 0 | 20 | 0 |
| ANDREA3 | 130 | 5,200 | 5.3 / 28 | 7.9 / 46 | 0 | 0 | 11 | 0 |
| ANDY1 | 243 | 9,720 | 10.1 / 49 | 14.7 / 96 | 1.9 % | 0 | 24 | 0 |
| ANDY2 | 254 | 10,160 | 4.4 / 16 | 7.2 / 62 | 0 | 0 | 11 | 0 |
| ANDY3 | 211 | 8,440 | 7.0 / 42 | 10.8 / 67 | 0 | 0 | 18 | 0 |
| JOBY2 | 169 | 6,760 | 12.2 / 69 | 21.2 / 121 | 7.8 % | 0 | 25 | 0 |
| JOBY3 | 195 | 7,800 | 4.0 / 16 | 7.7 / 83 | 0.4 % | 0 | 9 | 0 |
| JOBY4 | 137 | 5,480 | 19.0 / **106** | 23.7 / 128 | 8.2 % | 0 | **70** | 0 |
| JOBY5 | 191 | 7,640 | 4.8 / 18 | 8.4 / 73 | 0.3 % | 0 | 8 | 0 |
| RICH1 | 165 | 6,600 | 4.3 / 21 | 7.4 / 58 | 0 | 0 | 11 | 0 |
| RICH2 | 179 | 7,160 | 3.7 / 18 | 5.7 / 42 | 0 | 0 | 12 | 0 |
| RICH3 | 218 | 8,720 | 4.5 / 24 | 7.1 / 69 | 0.1 % | 0 | 11 | 0 |
| RICHCUT2 | 7 | 280 | 3.0 / 6 | 3.0 / 6 | 0 | 0 | 4 | 0 |
| TITLE | 116 | 4,640 | 8.4 / 48 | 8.8 / 49 | 0 | 0 | 24 | 0 |

**The last column is the TOOL's cap, not the engine's**, and it is named apart
from `cap hit=` on purpose, because that phrase belongs to the engine's counter
everywhere else in this section. `RoomProbe` models NEITHER engine array:
its own ring is 4,096 entries written with no capacity test at all, and its
draw list is a `std::vector`. What the column counts is the tool's own brake -
20,000 dequeues in one frame, which is there because a ring of rooms whose
portals all face the eye once walked the tool out of memory - so a zero means
only that no frame of the survey ran that long. It cannot say, and no run of
the tool can say, whether `draw_rooms` or `draw_room_list` would have been
passed. That is answered instead by the arithmetic of T5-1 and by two numbers
of this same table: the worst list of the whole survey is **139** against
`draw_rooms`' 256, and the deepest queue **70** against the ring's 512. The
engine's own guards keep their counter in `ROOMWALK`, and only the play test
can move it.

The whole distribution, the six columns of T4 and the three of T5:

| level | 32 | 40 | 48 | 56 | 64 | 80 | **96** | **112** | **128** |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ANDREA1 | 13.7 | 9.6 | 7.0 | 5.3 | 4.0 | 1.8 | 0.7 | 0.4 | **0.1** |
| ANDREA2 | 13.3 | 9.7 | 7.0 | 5.2 | 3.5 | 1.2 | 0.3 | 0 | **0** |
| JOBY2 | 24.9 | 18.9 | 13.5 | 10.1 | 7.8 | 3.1 | 0.8 | 0 | **0** |
| JOBY4 | 25.9 | 18.7 | 13.9 | 10.7 | 8.2 | 4.7 | 2.4 | 0.6 | **0** |
| ANDY1 | 14.2 | 9.4 | 5.7 | 3.5 | 1.9 | 0.4 | 0 | 0 | **0** |
| ANDY3 | 7.7 | 3.8 | 1.5 | 0.3 | 0 | 0 | 0 | 0 | **0** |
| TITLE | 3.9 | 1.5 | 0 | 0 | 0 | 0 | 0 | 0 | **0** |
| JOBY3 | 3.3 | 1.4 | 1.0 | 0.7 | 0.4 | 0.1 | 0 | 0 | **0** |
| RICH3 | 2.7 | 1.0 | 0.5 | 0.3 | 0.1 | 0 | 0 | 0 | **0** |
| JOBY5 | 2.5 | 0.9 | 0.6 | 0.6 | 0.3 | 0 | 0 | 0 | **0** |
| ANDREA3 | 1.8 | 0.3 | 0 | 0 | 0 | 0 | 0 | 0 | **0** |
| RICH1 | 1.8 | 0.7 | 0.3 | 0.1 | 0 | 0 | 0 | 0 | **0** |
| ANDY2 | 1.3 | 0.5 | 0.1 | 0 | 0 | 0 | 0 | 0 | **0** |
| RICH2, RICHCUT2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | **0** % |

At 128 the valve is shut in fourteen levels of fifteen and open in one tenth of
one percent of ANDREA1's worst-case frames. `kReachBudget` (`game\draw.cpp`)
is 128.

**And the two places the play test complained about, walked.** Both eyes come
out of the log's own five-second `ROOMLIST` dumps, which carry the eye and the
target, so the direction is the player's and not one chosen afterwards - how
nearly the player's is the paragraph after the table (2560x1440, fov 80, the
tool's own default, and `-roombox` on the classic levels):

| eye | classic | with the reach | reach's alone | dropped at 64 | dropped at 128 |
| --- | ---: | ---: | ---: | ---: | ---: |
| JOBY2 room 96, `54410/-5104/43819`, yaw 216.4 pitch 8.0 | 37 | **66** | 29 | 2 | **0** |
| ANDREA2 room 211, `86922/-1231/43587`, yaw 271.3 pitch 15.9 | 39 | **75** | 36 | 11 | **0** |

The engine's own `ROOMLIST` dump of each of those two frames stands in the log
beside the eye it was taken from, and the tool does not quite reproduce either
of them. Both differences below are on the same basis: the rooms the WALK
ended with, before the valve took anything out.

* **ANDREA2, one room apart, and the no-art trim is the reason.** The dump is
  64 rooms with `trimmed=10`, and `trimmed` counts the budget drops ALONE -
  `ReachTrimRooms` counts the rooms it takes out for want of `.TRG` art in a
  second counter of its own, `no_art` - so the walk ended at **74** against
  the tool's **75**. The `ROOMWALK` line of that second reads `no_art=172`
  over its 165 frames, which is one room a frame, and one room a frame is
  exactly the difference. The tool does not model that trim.
* **JOBY2, three rooms apart, and that is NOT accounted for.** The dump is
  **63** rooms with `trimmed=0` against the tool's **66**. The no-art trim
  cannot be the reason here: both `ROOMWALK` lines bracketing that frame read
  `no_art=0`, so nothing at all was taken out for want of art anywhere in that
  second, and `trimmed=0` says the budget took nothing either. Seed room,
  resolution and field of view are not it either - the dump's first room is 96
  with a full-screen window, its windows run `0,0..2559,1439`, and the last
  `ALTERFOV` before it reads `14560 (80 deg)`, all of which the tool was given.
  The open candidate is the DIRECTION. The dump carries the walk's own eye -
  `w2v_matrix` M03/M13/M23, which at a tween frame is the BLENDED position the
  frame was really walked from - but its target is the LOGIC camera's, which is
  not blended, and 135 of that second's 165 frames were tween frames. A yaw and
  a pitch reconstructed as `target - eye` are therefore near the direction the
  frame had rather than equal to it, and three rooms at the rim of a
  sixty-room list is what a fraction of a degree buys at a portal. Settling it
  needs a dump that carries the walk's direction and not only its eye. That is
  a measurement of its own and it is not this round's.

Nothing here rests on the gap: it is the distance between the tool's model and
the engine at one eye, three rooms in the sixties, and every count on either
side of it is far below 128. Deepest queue at those two eyes: 17 and 11.

**What is NOT measured, and it is the play test that decides it.** What a
frame of 128 rooms COSTS. The 6.1 ms above is a frame of 64 with 690 HD draws,
and the survey is a distribution of room counts, not of milliseconds - which
is the second thing the review of T4 said about the number 64, and it is still
true of 128. What the survey does say is that such frames are rare enough to
be a spike rather than a load: one frame in a thousand, in one level. If they
turn out not to be affordable, `kReachBudget` is one number in one place.

---

**The classic path.** With `hd_enable` off `HDLevelActive()` is false,
`g_reach_on` stays false, `FollowPortal` calls the `SetRoomBounds` that
shipped, `classicHere` is true, nothing is trimmed and no reach rectangle is
ever taken. The whole of this round's difference in the code the classic path
runs is **the two array sizes and the shape of the ring's wrap**: `& 511`
where it was `% 128`, which for counters that start at 0 and only count up is
the same index. `fps_mode 0` is untouched either way.

That leaves one behavioural difference and it is the point of T5-1: T4's guard
could refuse a CLASSIC room once the list passed a hundred, and the survey
found a JOBY4 frame of 106 classic rooms - so under T4 six rooms of that frame
were not drawn. Under T5 they are. Before T4 they were "drawn" by writing six
shorts past the end of the array, which is what T4-3 was built to stop; T5 is
the version of that fix that costs no rooms.

**The proof.** `be.bat`: **0 errors, 0 warnings**. `bit.bat` and `InterpTest`:
**324 checks, 0 failed** - the eight new ones are the T5 section, and they do
not restate the constants. They check that the ring's size is a power of two
and that `& (size - 1)` agrees with `% size` over 5,000 consecutive writes;
they run the engine's own queue loop, interlock and guard included, over the
worst graph there is - 256 rooms each with a portal to every other - and
require that the queue never gets deeper than the room count and that
`draw_rooms` takes one entry per room; and they require that **the same level
against a ring of 128 DOES trip the guard**, so that the passing case above is
not a tautology. `broom.bat` and the survey: the tables above, saved as
`reachsurvey_T5.txt` and `usereyes_T5.txt`.

**The tools.** `RoomProbe -reachsurvey` prints nine budget columns instead of
six (96, 112 and 128 added), and `-reach` counts frames over 64 and over 128
where it counted 60 and 100 - the two numbers a named eye is now asked about.
Its walk itself is untouched, which is what lets the T4 table be reproduced
line for line. `InterpTest` gained `walk_capacity`, eight checks.

**What the play test has to show.** At the two places the last one named -
JOBY2's stairs around log second 49 to 72, and the courtyard of Trajan's
Markets - `ROOMWALK` must read `trimmed=0` or nearly, `cap hit=0` still, and a
list above 64; `ROOMFLASH` must stop naming rooms 230, 47 and 229 with a
`reach[...]`, because nothing is dropping them any more. And `FPSLOG` of those
same seconds is what says whether 128 rooms fit in the frame: `real_fps` at the
panel's rate and `late=0` is the answer this round is betting on, and a
`late` count that climbs with `hd_rooms` is the answer that sends
`kReachBudget` back down.

#### Fix round Z - the marker rooms keep their HD art: the portal cut (#123)

**What the play test said.** ANDY3, build T5 of 2026-09-10, with the user's
own glitchless secret list: *"floors and walls are partly HD and partly not,
and seams open between them where the void or the skybox shows"* - mostly
around room 34. His log says the rest: `HDLOG level=ANDY3 rooms=211 hd=198
classic=13`, and of the thirteen classic rooms **nine are empty groups in the
.TRG** (26, 28, 88, 95, 103, 118, 122, 138, 210) while **four are MARKER
rooms** - 34, 36, 117 and 143, the line `markers rooms=4 of them classic
now=4`. The same seed without the secret list gives `hd=202 classic=9` and no
seams. So the seam is not the .TRG's: it is what stage S7 does with the
sidecar the randomizer writes.

**What stage S7 did, and why it was too coarse.** S7 gave the engine one
sentence per room - *this room's shape changed* - and the engine's only answer
was to draw the whole room classic. It had to be: the remaster's art still
shows the wall the run had just cut through, and a closed wall in front of an
open doorway is worse than a room drawn the old way. But the ANDY3 list opens
exactly TWO doorways, `(Make_Portal@Room34(x0,z5)->Room36(x4,z5))` and
`(Make_Portal@Room117(x5,z0)->Room143(x1,z3))`, and for two sectors' worth of
wall it took four whole rooms - among them room 34, which is 11 x 14 sectors
and the whole village courtyard - out of the HD picture. Two paths then meet
along the boundary of those rooms, and where the classic geometry of a
randomized level has a hole of its own, the void shows through it.

**So the unit is the opening and not the room.** The randomizer records the
BOX its edit opened; the engine drops every HD triangle that really meets that
box and keeps the rooms.

---

**Z-1 - the randomizer: a cut box instead of a whole-room mark.**

`makePortal`, `addViewWindow`, `makeVerticalPortal` and `clearWallFaces` no
longer call `hdMarkRoom`. They record an axis-aligned box in WORLD units,
derived from the bytes the edit itself wrote:

* **A wall opening** is read back out of the VIEW WINDOW that now stands at
  that sector - the 32-byte portal record's own four corners. One derivation
  for both ways into `makePortal` (the window it just built, and the window
  that was already in the file), so the two cannot disagree; and that record
  IS the opening as far as the engine is concerned, since `SetRoomBounds`
  clips the neighbouring room to exactly those four corners.
* **A vertical opening** (`Make_Ceiling_Portal` / `Make_Floor_Portal`) writes
  no window at all - one sector byte is the whole connection - so the box is
  the sector square at the height the link was opened: byte 7 for a ceiling,
  byte 5 for a floor, `* 256`. A wall carries -127 there and is no height;
  then the sector's other byte stands in, and if that is a wall too the room's
  own floor does. All three are planes inside that sector's column.
* **`@Clear`** takes the box over the faces it really degenerated. It used to
  mark the room *whether or not a face had been found*, on the argument that
  the sector had been NAMED as an opening either way; a box has to be the box
  of something, so where nothing was cleared nothing is recorded. Nothing is
  lost by that: `@Clear` is reachable from one place only (`Patch::apply`'s
  `portalAdds` loop) and that loop has just opened the very sector through
  `makePortal` or `makeVerticalPortal`, both of which record it themselves.

**The padding, and the two reasons for it.** Half a sector, **512**, along the
opening's own normal, because the remaster builds a wall as a thick solid
whose faces do not sit on the classic wall plane; **128** along the other two
axes, so the reveal round the opening goes with it and no rim of the old wall
is left standing in the doorway. *(The second half of that is wrong and fix
round Z1 below reverses its sign: the 128 in the plane points INWARD from this
round on. What it cost while it pointed outward is measured in the proof
below.)*

**Two details that are worth their lines.** The one-unit inset of a view
window is undone before the box is taken: a window does not sit ON its plane,
it sits one unit in FRONT of it where its normal component is +1 (measured off
vanilla andy1, and written down in `addViewWindow`'s own comment). Without
undoing it the two sides of ONE opening name two boxes one unit apart and the
sidecar carries two lines for every doorway in the game. And `clearWallFaces`
finds the axis its degenerated faces are FLAT in and treats that as the
normal - `faceOnSectorWall` only ever accepts a face whose corners share their
x or their z, so such an axis always exists. Measured consequence, ANDREA3
with the glitched list: the box over the three cleared walls then comes out to
the same six numbers as the box over the three view windows, and the sidecar
carries three cut lines where it carried six.

**`moveRoom` with dy and `buildRoomGeometry` keep the whole-room mark**, and
that is the whole of what `room` means now: a room that was lifted, or that
had no faces at all and was given some, shares nothing with the art the .TRG
holds for it. There is nothing to keep.

**The sidecar is version 2.** One new line, `cut <x0> <y0> <z0> <x1> <y1>
<z1>` - six integers, world units, ascending corners, and Y GROWS DOWNWARD as
everywhere else in this format, so the first y is the opening's ceiling and
the second its floor. `room`, `outfit` and `donor` are unchanged. The lines
ascend by their six numbers (a `std::set`, like the rooms), so the same seed
writes the same file byte for byte.

**The version is the LOWEST one that can say what is in the file.** A file
with a `cut` line declares 2; a file without one declares 1 and stays readable
by an engine older than this round. The user takes intermediate builds out of
`release\` and runs them beside finished levels; a format number is no reason
to break the ones that still work. It is deterministic all the same - the same
seed writes the same lines, so it writes the same version.

---

**Z-2 - the engine: the triangle test at build time.**

`hd_markers.cpp` accepts version **1 and 2** and keeps the boxes
(`HDMarkersCutCount` / `HDMarkersCut`). Two numbers pass and both are read the
same way, because 2 only ADDS a line and changes the meaning of nothing that
was there: a version 1 file is a version 2 file with no cut in it, so levels
randomized before this round go on working exactly as they did. A line the
reader has no rule for is still ignored and still counted, so a newer
randomizer talking to this engine stays visible rather than silent.

`hd_level.cpp`'s `BuildRoomCuts` hands the renderer one entry per **(group,
box) pair**, in the group's own local frame - the room's origin off x and z,
`room[].y` off the vertical, the same three lines `BuildRoomBoxes` and
`BuildRoomIntrusions` use. **Every group is asked about every box.** This
round argued that from ANDY3, where cut box 0 took **557** triangles out of
room 34, **176** out of room 36, **575** and **176** out of the flipmap twins
183 and 208, and **30** out of a third room 35 - and read that spread as proof
that the art shutting a doorway need not belong to either room of the portal.
**Fix round Z1 took that reading away**: with the corrected box every one of
those five rooms drops to zero, because none of that art stood in the doorway
in the first place. The breadth stays, on fix round T's ground that the
remaster's rooms reach into each other by construction and on the flipmap
argument, which nothing has yet tested either way - but it is insurance, not a
measured need. Over the eight levels whose lists carry geometry edits, every
corrected box takes art from exactly one room, and that room is always one of
its own two.

**The intersection test is the exact one** - the separating axis test over
thirteen axes (the box's three face normals, the triangle's plane, and the
nine cross products of box axis with triangle edge). The two cheaper tests
each get one half of the question wrong, and both failures are visible in the
ANDY3 pictures below:

* **A vertex inside the box** would leave the doorway SHUT. One wall triangle
  can cross the opening with all three corners outside the box, and that is
  exactly the triangle that has to go.
* **The triangle's own box overlapping** would eat MORE than the opening. A
  slanted triangle that passes a doorway by has a bounding box reaching into
  it, and a wall would lose art that was never in the way.

The drop is the FIRST thing in the per-triangle loop of `HDRenderLevelBegin`,
before the page slot, the horizontal-plane row, the overhang test and the
intrusion test. A cut triangle therefore never reaches the sort list: it costs
no index, no draw call and no count, every number the build prints afterwards
is the number for the geometry that is really in the buffers, and there is no
second place where a dropped triangle could still be counted in. A group with
an empty cut list pays one compare per triangle, which is every group of every
vanilla level.

**A room named only by cut boxes stays HD.** The `HDLOG room=` line has a new
`cut=` field and a new reason word, `cut`, for a room that lost art to an
opening and is still drawn HD; `marker` now means only the whole-room case.
One `HDLOG ... cut N x .. y .. z ..` line per box names the rooms it took art
from and how much, a box that caught nothing says so out loud, and the level
line carries `cuts=` and `cut_tris=`.

**Nothing of this runs with `hd_enable=0`.** `HDLevelLoad` returns before
`HDMarkersLoad` and `HDLevelBuild` returns before `BuildRoomCuts`, both on
`HDEnabled()`. The frame path is not touched at all, so `fps_mode 0` is
unchanged by construction, and the randomizer's own output for the `.trc` is
byte-identical - see the regression below.

---

**Z-3 - the tools.**

`HdmDump` and `MapProbe` read version 2 and print the boxes with their spans;
`MapProbe` still uses the engine's own reader, so what it says is what the
game reads. `RoomProbe` did not read the sidecar at all - it calls
`HDRenderLevelBegin` itself and has no `hd_level.cpp` in it - so a picture of
"the doorway cut open" drawn by it would have been a picture of the doorway
still shut. It has a `-hdm` switch now, `hd_markers.cpp` is on `broom.bat`'s
line, and the switch does what `hd_level.cpp` does: the cut boxes go to the
renderer, and a room the sidecar names WHOLE is not drawn at all - which is
the hole the engine leaves for the classic path to fill, and the thing that
had to be photographed.

---

**The proof, in pixels.** One randomized ANDY3, seed 380, the user's
glitchless list, rendered with `RoomProbe` out of the engine's own sources.
The sidecar is

```
hdm 2
cut 38400 8576 25472 39424 10368 26752
cut 44928 10624 39424 46208 11648 40448
```

and both boxes check out against the level file to the unit: room 34's sector
(x0,z5) has its wall plane at world x 38912 (origin 37888 + one sector), floor
10240 and ceiling 8704, so the box is 38912 +- 512 across, 8704 - 128 ..
10240 + 128 high and 25600 - 128 .. 26624 + 128 deep. The second is the same
arithmetic on room 117's (x5,z0), plane z 39936.

* **The seam is gone.** From the user's own eye point in the room-34 area
  (46880, 10057, 25454), 2560x1440, rooms 34, 35, 36, 37, 42 and 70 drawn:
  with the sidecar as it ships today **80.49 %** of the picture has no HD
  geometry at all - the village is two roofs and a cliff floating in the clear
  colour. With the cut it is **34.26 %**, all of it sky and rooms that were
  not in the shot. **1,703,969 pixels of 3,686,400 came back, and not one
  pixel of the picture became clear.** The second wide shot (46926, 8937,
  33795) says the same: **1,213,380** pixels back, none lost.
* **Away from the openings the cut costs nothing.** Between "no cut" and "the
  cut", the first wide shot differs in **0 of 3,686,400 pixels**; the second
  in 19,334, and of those **0 became clear** - they are pixels where the
  doorway opened and the art behind it showed through.
* **The gate.** From inside room 34 at (41984, 9600, 26112) looking at the
  doorway, the opening's screen rectangle is x 545..735, y 193..479. Before
  the cut that rectangle is **0.00 % clear**; after it, **17,836 of its 54,340
  pixels** have changed, 11,988 of them to the clear colour and 5,848 from one
  drawn colour to another. This round read the 5,848 as "the corridor of room
  36 showing through where the gate face used to be". **Fix round Z1 measured
  that the gate face is not there**: no remaster triangle of any group meets
  the doorway's own interior, so the dark face in the before picture is the
  far wall of the passage beyond the arch and not a gate. The archway is
  already open in the .TRG and the 5,848 are art further in, uncovered along
  with the lintel. From the far side,
  standing in room 36's passage and looking back, the courtyard, the hillside
  and the cliff of room 34 stand in the opening.
* **One honest qualifier on that pair.** From room 36's side the arch's throat
  was already open in the remaster's art before the cut - the courtyard is
  visible in the before picture too - so there the cut takes the soffit and
  the threshold and reveals nothing new (5,555 pixels of the rectangle became
  clear, 0 became drawn). This round read that as "the closed face sits on
  room 34's side"; fix round Z1 measured that there is no closed face on
  either side, and that both of these two pictures are therefore cost without
  gain.
* **From inside room 117** the new doorway is a rectangular hole in the
  corridor wall that **shows nothing at all**: 31,331 pixels of that picture
  change between "no cut" and "the cut", **every one of them becomes the clear
  colour and none becomes drawn**, which is the opposite of what this round
  set out to produce. The corridor round the hole is not untouched either -
  group 117 loses 754 of its 1,065 triangles and four of its six horizontal
  planes of art. (Corrected 2026-09-11 in fix round Z1, which measured this
  view again; the sentence that stood here called it the best picture of the
  round.)
* **And the buffers stay whole.** Per group, `RoomProbe -groups` without and
  with the sidecar: room 34 **26,307 -> 25,750** opaque triangles with its
  1,674 additive ones and its 37 draw calls unchanged; room 36 **41,670 ->
  41,494**, 12 draw calls unchanged; room 143 **162 -> 114**; room 117
  **1,065 -> 311** with one draw call fewer. Every number moves by exactly
  what the box's own line says it took, and the groups' bounding boxes shrink
  with them (room 117's z 946..2048 becomes 1024..2048) - a cut triangle
  never enters a buffer, so nothing downstream has to be corrected for it.
* **What the cut also eats, and this round does not hide it.** The padding is
  applied to all three axes, so a wall opening's box reaches 128 past the
  opening's own floor and ceiling as well as past its jambs. At ANDY3's gate
  that costs the threshold, the soffit and a rim of the two piers - 66,631
  pixels of the 867,260 outside the doorway rectangle, 7.7 %, **and 41,687 of
  those became the clear colour**, three and a half times the 11,988 inside
  the doorway. That is the number that says the change outside the doorway is
  hole and not reveal, and the round as first written left it out. (The
  sentence that stood here went on to argue the apron was harmless because
  `FaceCheck ANDY3.TRC 34 0 5` reports *no face covers that sector*. It does
  not carry that weight: FaceCheck reports floors and ceilings as well as
  walls, so the answer only says the vanilla WALL sector had no faces of its
  own - true of every wall sector in the game - and says nothing about the
  threshold and soffit art, which belongs to the neighbouring sectors and to
  room 36. Struck in fix round Z1.) At the second
  doorway it costs more than that: rooms 117 and 143 are water passages
  **exactly as tall as the opening** - floor 11520, ceiling 10752, and the box
  runs 10624..11648 - so the 128 takes their floor and their ceiling with the
  wall. Room 117 loses **754 of its 1,065** triangles that way, room 143
  **48 of 162**, and from inside room 143 that is **43.5 %** of the picture
  drawn as the clear colour - 400,950 pixels became clear there and none
  became drawn. The doorway is open and in the right place; the chamber round
  it is not. **Named here rather than rounded off: an in-plane INSET instead
  of the outset would keep the floor and the ceiling and still cut every wall
  triangle that crosses the opening, and it is the first thing to try if the
  play test dislikes that passage.**

**The verdict of this round, restated 2026-09-11.** The seam is gone and the
rooms keep their art - that half held up under review. The openings were not
yet right: across the six doorway views the cut turned pixels into the clear
colour and turned **not one** pixel into a drawn one, which is a picture of
holes and not of doorways. Fix round Z1 below is the correction.

**What the play test has to show.** ANDY3 with the glitchless list: the
`HDLOG room=` lines for **34, 36, 117 and 143** must read `hd=1` with reason
`cut` or `ok` and no longer `marker`; the level line must read `cuts=2` with
`markers rooms=0`; the village around room 34 must be HD all the way to its
boundaries with no void in the seams; and the two new doorways must be open
with the room behind them visible. Any other level whose list opens a wall is
the same test - ANDREA3, ANDY1, JOBY3, JOBY5, RICH1, RICH2 and RICH3 in the
glitched list, where ANDREA3 is the one that keeps a whole-room `room 74`
because its list also moves and rebuilds that room.

**The regression.** The randomizer's output for the `.trc` is unchanged. Eight
levels - ANDREA3, ANDY1, JOBY3, JOBY5, RICH1, RICH2, RICH3 and ANDY3 - built
with seed 380, `mix cross allsanity 8 weapons` and the glitched secret list,
against a build of the commit before this round, come out **byte-identical**
(SHA-256 per file). Those eight are the levels whose lists carry geometry
edits at all, so every changed path was exercised: `Make_Portal`,
`Make_Ceiling_Portal`, `Make_Floor_Portal`, `@Clear`, `Move_Room` and
`Make_Room_Geometry`. ANDY3 with the glitchless list is byte-identical too,
and running the same seed twice writes the same `.hdm` byte for byte.

#### Fix round Z1 - the cut box steps inward, not outward (#123)

**What the review said, and it was right.** Fix round Z above kept the marker
rooms HD and cut the opening out of their art, and it measured everything
except its own central claim. A cut room stays HD (`hd_level.cpp` sets
`g_group_hd[r] = 1` when only boxes named it) and an HD room never draws its
classic roomlets (`drawroom.cpp:167`, `InsertRoom`: `if (hd_room) {
InsertRoomWater(r); return; }`), so **every triangle the box takes is replaced
by nothing.** The 128 units of padding in the plane of the opening therefore
did not widen a reveal; it punched a hole. And it always did, because a view
window runs from its sector's ceiling to its floor by construction, so any
outward step at all reaches the passage's own floor and ceiling quads, which
lie exactly in the box's boundary plane.

Measured on the doorway views of round Z's own proof: the cut turned
**53,675** pixels clear at ANDY3's gate, **400,950** looking at the new water
passage from room 143 (43.5 % of the frame) and **31,331** from room 117 - and
turned **not one** pixel from clear into drawn, in any view. In the game those
pixels are the void or the skybox, word for word what the play test of
2026-09-11 reported. The round replaced the old HD/classic seam with new holes
at every opening.

---

**Z1-1 - the randomizer: the step in the plane changes sign.**

`Tr5Level::hdMarkCutBox` is the whole change. The box still grows **512
outward along the opening's normal** - the remaster builds walls as thick
solids whose faces do not sit on the classic wall plane, and that half sector
reaches them on both sides. Along the other two axes it now steps **128
INWARD**, clamped to half the extent so that an opening narrower than 256
units collapses to a plane through its own middle instead of turning inside
out. The two constants are called `HD_CUT_OUT_NORMAL` and `HD_CUT_IN_PLANE`
now, not `HD_CUT_PAD_*`, so nobody has to read the sign out of the arithmetic.

Nothing else moved: not the derivations (the view window's own four corners,
the sector square at the height a vertical link was opened, the faces `@Clear`
degenerated), not the one-unit window inset that makes both sides of an
opening name one box, not the sidecar's shape, not its version rule, not
`moveRoom` and `buildRoomGeometry`, which still take the whole room. **The
engine is untouched by this round** - the reader, the (group, box) pairing and
the separating-axis test all take the box as it comes.

**Why inward is enough, and why it does not shut the doorway again.** The
engine's test is a real triangle/box intersection, not a corner-inside test,
so a wall triangle that merely *crosses* the opening still meets a box inset
by 128 on every side. A floor or ceiling quad lying in the opening's own
boundary plane no longer does. ANDY3's water passage is the clean case: floor
11520, ceiling 10752, so the box's y runs 10880..11392 inside a passage
10752..11520, and the two quads miss it by 128 units each.

**The price, and it is paid in one place.** A triangle that lives wholly
inside the outer 128 units of an opening now survives as a rim. Over the eight
levels whose lists carry geometry edits, no such triangle exists: every
opening either comes out fully cut or is already open in the .TRG. What does
survive is the other half of triangle-granular cutting - a triangle that
crosses the box may reach far outside it and is dropped whole. That is
measured below on RICH2, where it costs 6,567 pixels of one frame.

---

**What the measurement turned up on the way, and it is the more interesting
half.** The first of the two openings ANDY3's glitchless list makes - the
village gate, the one the play test complained about - **was already open in
the remaster's art all along.** With the corrected box, cut 0 catches nothing
at all, and `RoomProbe` says so in as many words:
`cut 0: NOTHING - the remaster drew no triangle in that box`. The picture
agrees: drawn with no sidecar, with rooms 34, 35 and 36 all in the frame, the
gatehouse arch stands open and room 36's passage is visible through it. The
1,514 triangles round Z took out of five rooms there were the threshold, the
soffit, the lintel band, the piers and room 35's floor above the arch - the
surround, never the gate.

So the seam the play test photographed was caused by the whole-room marker
alone, and for ANDY3's gate the right amount to cut is zero. The second
opening is the other case and the cut earns its keep there: the water passage
between rooms 117 and 143 really does have remaster art across it - a wooden
cross-brace - and 305 triangles of it go.

---

**The proof, in pixels.** One randomized ANDY3, seed 380, the user's
glitchless list, source the classic `Backup\ANDY3.TRC`, rendered with
`RoomProbe` out of the engine's own sources. The sidecar is now

```
hdm 2
cut 38400 8832 25728 39424 10112 26496
cut 45184 10880 39424 45952 11392 40448
```

against round Z's `cut 38400 8576 25472 39424 10368 26752` and
`cut 44928 10624 39424 46208 11648 40448`: the normal axis is untouched (x
38400..39424 is 38912 +- 512; z 39424..40448 is 39936 +- 512) and the other
two have come in by 256 each. `HdmDump` prints the spans, and exactly one of
the three is 1024 in each box - that one is the normal.

**The seven views, "no sidecar" against "the cut", whole frame:**

| view | round Z: became clear / drawn | round Z1: became clear / drawn |
|---|---|---|
| room 34 -> 36, 3 sectors back | 53,675 / 0 | **0 / 0** (0 pixels differ) |
| room 34 -> 36, 6 sectors back | 7,830 / 0 | **0 / 0** (0 pixels differ) |
| room 36 -> 34 | 26,716 / 0 | **0 / 0** (0 pixels differ) |
| room 117 -> 143 | 31,331 / 0 | **0 / 0** (2,233 pixels differ) |
| room 143 -> 117 | 400,950 / 0 | **0 / 0** (54,992 pixels differ) |
| wide shot 1 (the user's eye point) | 0 / 0 | **0 / 0** |
| wide shot 2 | 0 / 0 (19,334 differ) | **0 / 0** (0 differ) |

**Not one pixel of any of the seven frames becomes the clear colour.** The two
frames that change at all are the two that look at the water passage, and they
change from one drawn colour to another - which is what a doorway opening
looks like when there is something behind it. From inside room 143 the wooden
cross-brace that stood in the passage is gone and room 117's chamber, its far
wall and its floor are visible through the opening, with the passage's own
floor, ceiling and side walls untouched. That is the picture round Z was after
and did not get.

**The wide shot still says what it said.** At the user's own eye point
(46880, 10057, 25454), 2560x1440, the sidecar as it ships today leaves
**80.49 %** of the picture with no HD geometry - the village floating in the
clear colour, which is his report - and keeping the rooms HD brings **34.26
%**, all of it sky and rooms not in the shot. That half of round Z was right
and is unchanged.

**Per group, ANDY3:** round Z dropped **2,316** triangles in **7** groups (34,
35, 36, 117, 143, 183, 208); round Z1 drops **305** in **one** - room 117,
**1,065 -> 760**, with its four draw calls and its bounding box **x
1024..6144, y 10752..11520, z 946..2048 unchanged**. That box is the shortest
proof of the round: y still reaches the passage's floor at 11520 and its
ceiling at 10752, so neither quad went with the wall. Round Z left that group
at 311 triangles and pulled its z in to 1024..2048. Room 143 keeps all **162**
of its triangles (round Z took 48), group 117 keeps all **six** of its
horizontal planes of art (round Z left it two), and the level keeps all
**1,117** flat-art rows - so fix round H's water-surface question is answered
out of the same table as before the cut existed.

**The survey over the other seven levels** (glitched list, seed 380, `mix
cross allsanity 8 weapons`), triangles dropped, round Z -> round Z1:

| level | round Z | round Z1 | rooms that lose art now |
|---|---|---|---|
| ANDREA3 | 169 in 1 group | 100 in 1 | 1 |
| ANDY1 | 0 | 0 | - (both boxes catch nothing) |
| JOBY3 | 1,239 in 4 | 240 in 1 | 76 |
| JOBY5 | 1,134 in 3 | 240 in 1 | 10 |
| RICH1 | 159 in 2 | 0 | - |
| RICH2 | 2,175 in 7 | 801 in 1 | 120 |
| RICH3 | 728 in 6 | 258 in 3 | 46, 120, 86 |

Every box now takes art from exactly one room, and that room is always one of
the two its own portal joins. The neighbours that used to lose art - JOBY3's
rooms 4, 10 and 13, JOBY5's 4 and 87, RICH2's 2, 26, 28, 45, 125 and 126,
RICH3's 150, 85 and 87 - lose none.

**Two more pictures, because two levels needed one.**

* **RICH1**, the level whose cut drops to zero. Standing in room 130's
  corridor looking at the new doorway into 129: round Z's box turns **680,519
  of 921,600 pixels** clear - 73.8 % of the frame, the corridor gutted, 0
  pixels drawn - out of **159** triangles, which is how coarsely the remaster
  tessellates a corridor wall. Round Z1's box changes **0 pixels** against no
  sidecar at all: no remaster triangle stands inside that opening. What the
  player will see THROUGH it this picture cannot say - group 129 holds 78
  triangles in a single sector cube and `RoomProbe` drew the doorway square
  empty with or without the sidecar, which is a question about the probe's
  `-with` and not about the cut. It is on the play-test list below.
* **RICH2**, the level with the largest remaining cut, and the one that shows
  the mechanism working. Standing in room 120 looking east at the two new
  doorways into room 28: with no cut the VCI lobby's **wooden double doors are
  shut**. With the cut they are gone, the room behind is visible through the
  opening, and the door frame, the lintel, the blinds either side and the
  marble floor all stand. 256,326 pixels change and **6,567** become clear
  (0.71 % of the frame) against round Z's **101,640** (11.0 %). Those 6,567
  are a band above the doorway: room 28 has no geometry above the opening's
  top plane (its box is y -4352..-2048, the opening's top is -4352), so no
  neighbour could ever fill that band - it is room 120's own lintel art, taken
  by a triangle that crossed the box and reached past it. **Named rather than
  rounded off: triangle-granular cutting cannot do better than this without
  clipping triangles against the box, which this round did not attempt.**

---

**The classic path.** Nothing in this round runs with `hd_enable=0`: the only
code that changed is in the randomizer, and on the engine side round Z's own
gates stand where they stood (`HDLevelLoad` returns before `HDMarkersLoad`,
`HDLevelBuild` before `BuildRoomCuts`). `fps_mode 0` is untouched by
construction - there is no frame-path code in this round.

**The regression.** Byte-identical `.trc` over the eight levels whose lists
carry geometry edits - ANDREA3, ANDY1, JOBY3, JOBY5, RICH1, RICH2, RICH3
(glitched list) and ANDY3 (glitchless) - against the files round Z produced,
SHA-256 per file. Only the `.hdm` changes, and only its `cut` lines. The same
seed twice writes the same `.trc` and the same `.hdm`.

**What the play test has to show.** ANDY3 with the glitchless list, unchanged
from round Z's list except for one line: rooms **34, 36, 117 and 143** read
`hd=1` in `HDLOG` with reason `ok` or `cut` and no longer `marker`, the level
line reads `cuts=2` with `markers rooms=0`, the village around room 34 is HD
to its boundaries with no void in the seams, and **both doorways are open with
no hole anywhere near them** - at the gate because the arch was never shut, at
the water passage because the brace across it is cut away. Two things no
picture here can settle and the play test can: **RICH2's lintel band** above
the new double doors, where one triangle reached past the box, and **RICH1's
doorway between rooms 130 and 129**, where the cut now takes nothing and the
question is only whether the remaster's art lets one see through.

#### Round M1 - the Mods folder: models, pages and sounds from a mod (#153)

The remaster has a drop-in mod layout of its own - `Mods\<Category>\<Mod>\{4,5}\{ITEM,TEX,DATA}` -
and people have made things in it: replacement hands, holsters, pickups,
outfits, whole sound banks. This round gives our engine the same folder, with
one rule the user set above every other: **a mod may never change a page of a
model it did not supply.** Vanilla is untouchable, and the mechanism has to
make that impossible rather than merely unlikely.

**The scanner** is `specific\hd\hd_mods.cpp`, engine-free like `hd_install.cpp`
so that the diagnostic tools link the same file the engine does. A mod is a
folder directly under `Mods`, or one level deeper, that has a `4` or a `5`
subfolder or a `mod.txt`. A name beginning with `_` is switched off at every
depth - that is the whole "give me vanilla back" mechanism, and it needs no
uninstall. The order is the relative path compared as lower-case ASCII, and
that order is the priority. The scan runs once per session; both the HD level
build and `HQSfxOpen` ask for it, so whichever gets there first, the two see
one list.

**The model resolver** sits in front of `find_model` (`hd_model.cpp`): the
active mods in order, each `5\ITEM` then `4\ITEM`, then the installs exactly as
before. It is a replacement by resolution and never on disk. `OUTFIT_*.TRM` is
refused here by name - Lara's composite is a measured table (`kOutfit`, the
head weights, the holster cut out of one page of an outfit) and a foreign
outfit that does not carry what the table expects is not a different Lara but a
broken one. That is round M2.

**The page namespace** is the safety rule made into arithmetic
(`hd_pagecache.h`). A mod's page is offered to the cache as
`HD_PAGE_MOD_BASE + modIndex * HD_PAGE_MOD_STRIDE + page`, so mod 2's 6111 and
the install's 6111 are two cache slots holding two different pictures. Three
`static_assert`s prove the three namespaces - plain, the I-III bundle's
`HD_PAGE_ALT_BASE`, and the mods' - cannot meet, and a build in which they
could fails to compile. A page a **vanilla** model asks for never carries a mod
key, so it never enters the mod branch of `resolve_page` and cannot be replaced
by a mod: the guarantee is structural.

The chain for a mod's page is the mod's own `5\TEX`, then its `4\TEX`, then -
only when `mod.txt` says `origin=tr1|tr2|tr3` - that game folder of the Tomb
Raider I-III bundle and its two siblings, and then the ordinary IV-VI chain
with the plain number. The fall-through is what lets a mod ship models and no
art at all.

**Why `origin=` exists, measured.** The user's `IMI Uzis` mod ships
`HAND_BARE_UZI.TRM` (31267 vertices) which names pages 6111 and 6112 - its own,
shipped beside it - and **8002 and 8010**, which are Lara's skin sheets of the
**I-III** bundle. In the IV-VI bundle those same numbers hold other art: `4\TEX\8002.DDS`
is a cut-out sheet with 14.5 % holes. Without `origin=tr1` the hand is painted
with it; with it, both pages come from `1\TEX` and read 0.0 % holes. Two
`ModelProbe` renders of the same file, the same install and the same code show
the difference and nothing else does. The other two gun mods need no line at
all: the HK USP and the Spas-12 wear the install's own 8001 for skin and only
bring their weapon pages.

**The sound half is independent of `hd_enable`** - it is audio, like the HQ
bank has always been. `HQSfxOpen` now walks three candidates: the first active
mod that ships `5\DATA\MAIN.SFX`, then `audio\MAIN.SFX`, then the installed
remaster's own `5\DATA\MAIN.SFX` read where it lies. The third is new and it is
what makes the hand copy into `audio\` unnecessary. `4\DATA\MAIN.SFX` is
deliberately not used: that is Tomb Raider IV's bank with its own numbering,
and pairing a TR5 level's bridge with it would play the wrong recordings.

A loose `<NNNN>.wav` - exactly four digits, any depth under an active mod, so a
modder's own `GlobeRaider Files\0000.wav` works where it lies - replaces that
numbered sample of whichever bank was opened. First active mod wins per number.
The file is checked the way the bank's own samples are: a RIFF WAVE,
uncompressed PCM, mono, 16 bit, with a `data` chunk inside the file, and a
4 MiB cap so a mistyped name cannot pull a video into memory. Without any bank
there is no bridge from a level's sample number to a bank number, and the log
says so once.

**The plausibility rule needed a ruler, and this is the measurement that
proved it.** The rule that has guarded the HQ bank since #117 compares the
bank's sample against the level's own: at twice the rate the same recording
must be about twice as long, and anything further off than a fifth is a wrong
pairing and keeps Core's version. It works because both sides hold the same
sound. **A sound mod breaks that on purpose.** Measured with `SfxProbe` on the
user's own `Underworld Bare Footsteps`: sound id 0 is bank sample 0 in both
banks, and it is 3388 bytes of audio at 44100 Hz in the unmodded bank (0.038 s)
against 28782 bytes at 48000 Hz in the mod's (0.300 s) - **7.81 times longer**.
Id 4, the landing, is 1.64 times longer. Both are outside the fifth, so the
rule as it stood would have refused precisely the five samples the mod exists
to change, and the mod would have done nothing at all while its pairing was
correct throughout.

So when a mod's bank is the one being played, the engine opens the first
**unmodded** bank of the chain beside it and measures the pairing against that
instead. The ruler is never played from. It is walked through its own sound id
table rather than by sample number, because the two banks have different sample
counts - 463 against 473 in the user's case - and nothing says a number means
the same sound in both; the sound id does. If the ruler's sample for this level
sample is the right length the bridge is right and the mod's recording plays
whatever its length; if it is not, the level keeps its own sample exactly as
before. With no mod bank, or with no unmodded bank to be had, none of this runs
and the rule is what it always was.

**The parser moved out of the engine.** `specific\sfxbank.cpp` and `.h` now
hold the `MAIN.SFX` layout, the chain walk that tests it and the RIFF check;
`hqsfx.cpp` keeps the engine's half - the level bridge, the substitution and
the log. `SfxProbe` links the same file, so what the tool prints is what the
engine did.

**One consequence of the namespace, measured and left standing.** A page a
mod's model shares with the install is loaded twice: once under the mod's key
for the mod's model and once plain for everyone else. On ANDREA1's wardrobe
with the user's `Mods` folder active that is **+4 pages and +5.3 MiB** (21 ->
25 pages, 28.0 -> 33.3 MiB); on the whole level it is nothing at all, because
the two pistol pickups' mod page displaces the vanilla page nobody else wore
(53 model pages either way, 350.5 MiB total either way). It would be safe to
share the slot - the fall-through reads the install's own file, byte for byte
the same texture - but the saving is 1.5 % of a wardrobe and the rule "a mod's
model never shares a slot with a vanilla one" is worth more than that while
this is one round old.

**And one rule that had to learn about the namespace.** `hd_lara.cpp` tells a
weapon hand's gun from its skin by "every page this model has that
`HAND_BARE_REST` has not got is the weapon" (fix round I, I4). With a mod hand
and vanilla rest hands the two are in different namespaces, so *nothing* would
match and the skin would take the gunmetal material along with the gun. The
comparison strips the namespace and compares page NUMBERS - which is what
`hd_model`'s own `Range` says a material is named by. The HK USP and the Spas-12
are then read exactly right (both wear 8001 for skin), and a mod that renumbers
is read right as soon as it ships its own `HAND_BARE_REST`.

**Every seam behind `-log`, all of them new lines and none of them thinned.**
`MODLOG folder=`, one `MODLOG mod<i> path= origin= models= pages= bank= wavs=`
per mod, `MODLOG disabled <path>`, `MODLOG model <NAME> <- mod<i>` once per
replaced model per session, `MODLOG page mod<i>:<n> comes from ...` per page,
`MODLOG bank <label> (<path>) - opened/not opened`, `MODLOG bank ruler:`,
`MODLOG bridge:`, `MODLOG bank sample <n> is ... - a recording the mod
replaced`, `MODLOG wav <nnnn> <- mod<i>` and `MODLOG wav <nnnn> refused - <why>`.
A mod page prints as `mod<i>:<n>` wherever a page number is printed, so a
reader can always tell it from the install's. `HDLOG models` gained a `mod:`
column and `HDLOG models budget` a `frommod=`; the `HQ SFX:` summary line now
names the bank it really used.

**What is proved and what is not.** Proved with `ModelProbe` through the
engine's own resolver and renderer: the three gun mods' hands, the Uzi's skin
pages from the I-III bundle with `origin=tr1` and the wrong art without it, and
that with no `Mods` folder every page key, every model origin and the whole
wardrobe come out identical to the build before the round. Proved with
`SfxProbe` on the user's game folder: the chain, which bank wins and why the
other two are not reached, the bridge for ids 0 and 4 through both banks, and
all five loose wavs accepted. **Not proved:** anything that needs the game
running - the play test owes the sound of the footsteps, the look of the hands
in motion, and whether a level with mods still holds its texture budget on the
user's card.

**Tools.** `Preparation\Tools\Diagnose\ModelProbe` gained `-mods <folder>` and
`-tr3 <3-folder>`, prints which mods were found and what each ships, and its
page list now names the folder every page was really read from - which replaced
a copy of the 5-then-4 rule that had gone out of step with both newer
namespaces. `SfxProbe` and `bsfx.bat` are new.

#### Round M1, the fix round - a crafted .wav must not end the process (#153)

The round above was reviewed and one thing in it was not merely untidy but
fatal, and it is worth writing down because the shape of the mistake is a
common one.

**Never add in order to compare.** `SfxRiffCheck` walks a RIFF's chunks and
guarded each one with `at + 8 + len > bytes`. `long` is 32 bit in this build,
so a chunk length near `LONG_MAX` makes that sum **overflow to a negative
number**; the test comes out false, the chunk the guard exists to catch is
accepted, `at += 8 + len` drives the cursor negative, and the loop's own
condition stays true while the next read lands about two gigabytes below the
buffer. A 60-byte file ended `SfxProbe` with an access violation, and it is a
**mod's** file that reaches this call, so the crafted length is not a
theoretical worry. The guard subtracts now - `len > bytes - at - 8`, where the
loop's invariant `at + 8 <= bytes` makes the difference non-negative by
construction. The identical idiom in `SfxBankOpen`'s outer chain walk was
corrected with it; that one ended in "the chain of sounds does not add up to
the file" rather than in a crash, but it is the same unsafe form at the same
kind of seam. The brief's rule was *reject with a line, never crash*, and the
two crafted files are now simply refused: `a chunk runs past the end of the
file`.

**One gate, not two.** `HQSfxPayload` still judged the *bank's* sample with a
hand-rolled walk of the same chunks - no bound on the length at all - and a
`WAVEFORMATEX` laid straight over offset 20 on the assumption that `fmt` comes
first. That was survivable while the bank could only be a file the player had
copied out of his own installation. This round put a **downloaded** mod's
`5\DATA\MAIN.SFX` at the head of the chain, and `SfxBankOpen` validates only
the outer chain of RIFFs and never the chunks inside one, so a single garbage
inner length reached an unguarded pointer walk during level load. It calls
`SfxRiffCheck` now - the same bounds-checked gate a loose `.wav` goes through.
That gate is *stricter* than what it replaced, so the obligation was to show
the player's real data still passes it whole: `SfxProbe -samples` (new) pushes
every sample of the chosen bank and of the ruler through it, and on the user's
own folder **463 of 463** and **473 of 473** pass with none refused.

**One order, in one place.** `HQSfxOpen` held the chain's order and `SfxProbe`
held a second copy of it - the very arrangement that had let `ModelProbe`'s
page rule drift out of step with the cache until this round caught it. The
order is `SfxBankChainLink` in `sfxbank.cpp` now, lazy so that nothing pays for
`HDInstallRoot()`'s walk through the registry and every Steam library unless
the link that needs it is reached, and both the engine and the tool walk it.
The tool's chain print is numbered by position rather than by tier as a
consequence, and says so.

**Three smaller things.** The `MODLOG model <NAME> <- mod<i>` note was written
inside the resolver before `find_model` had accepted the answer, so a face
looked up for a body that came out of another mod could log a replacement that
was never drawn - and its one-line-per-model memory then made the correct line
unsayable. The wanted mod goes into the search now and the note is made where
the answer is accepted; the memory is keyed by name *and* mod. The
out-of-video-memory note printed the cache **key** raw, which for a mod's page
reads `page 1342183535`; it uses `page_text` like every other line in that
file. And `MODLOG folder=... present=no` now carries `mods=0`, so both branches
of that line have the same shape.

**One finding refuted rather than fixed.** The review also flagged
`hd_lara.cpp`'s two wardrobe notes - `page %d is named as her skin ...` and
`her throat: page %d ...` - as keys printed raw that would become unreadable in
round M2. They are not keys. The first prints `kOutfit[].skin[i]`, a
hand-written table of plain page numbers, and the second prints `g_inner_page`,
which comes from `HDTrmPageOf` and is `model->texture[...]`, the `unsigned
short` straight out of the `.TRM`. Both stay correct and readable when a mod
can supply an outfit. What *does* break then is the **comparison** on the line
between them, `HDModelPage(slot, j) == page`, because `HDModelPage` returns
`raw + pageBias` - and that was already on the open list as M2's work. No
change was made; a helper added for the finding was taken out again.

#### Round M1, the second fix round - the format block is where the file says (#153)

The fix round above said it had removed "the `WAVEFORMATEX` over offset 20".
It had removed it from `hqsfx.cpp`. The site that decides what is actually
**played** still had it, and this round is precisely what lets a stranger's
file reach that site.

**A legal wav need not begin with its format block.** `SfxRiffCheck` *walks*
the chunks, deliberately, because a `fact` or a `LIST` may stand in front of
`fmt` - and the shape every digital audio workstation writes is a 28 byte
`JUNK` block first, room it can later rewrite into `ds64` if the recording
grows past four gigabytes. Reaper, Pro Tools and libsndfile all emit it. Such
a file passed the gate, correctly, and then met `LoadSamples()`:

```
const WAVEFORMATEX* wf = (const WAVEFORMATEX*)(riff + 20);
if (riffSize > 20 && wf->wFormatTag == WAVE_FORMAT_PCM)
```

In a `JUNK`-first file the two bytes at offset 20 are the middle of that
block's header - measured on a crafted one, `0x0000`. The test came out false,
the mod's recording was dropped without a word, the level's own sample was
decoded instead, and `MODLOG wav 0000 <- mod0 ...` and the load summary both
went on claiming an overlay nobody would ever hear. A silent wrong result and
a log that lies, which is worse than a crash because nothing ever reports it.

**The offsets come out of the walk that accepted the file.** `SfxRiffCheck`
already finds both chunks; it now hands back where they were, as
`SfxRiffParts`, and `HQSfxPayload` passes that up to its caller. `LoadSamples`
reads the format block at `parts.fmtAt` and the audio at `parts.dataAt`, and
takes the PCM branch on `hq` alone rather than on a re-read of the tag:
everything the payload path returns has been shown to be mono 16 bit PCM with
a data chunk inside the buffer, so there is nothing left to decide there and
nothing left to discard behind the counters' back. `HQSfxSubstituted()` and
`HQSfxOverlaid()` are true by construction now rather than hopeful, and the
contract that makes them so is written into `hqsfx.h` beside the function.

The level's own samples are untouched: their tag is still read at 20 and their
data block still found by the walk in `file.cpp`, which is what that code has
always done and which no mod can reach - the one call that still lays a
`WAVEFORMATEX` over offset 20, `DXCreateSampleADPCM`, is only ever handed
`samples_buffer`. A mod's payload no longer goes through that walk at all, so
the round removed its own reachability rather than hardening a second copy of
a guard.

**The one semantic difference it carried, and the measurement that discharged
it.** `file.cpp`'s walk *breaks* at the first `data` chunk; `SfxRiffCheck`'s
does not, so on a file carrying two of them the gate reports the **last**. For
a payload out of a bank the offsets now come from the gate, so the two would
disagree on such a file. Counted rather than assumed, over every sample of all
three banks the player can reach - his `audio\MAIN.SFX`, the mod's, and the
installed remaster's, 1409 samples between them - and over the five loose
`.wav` files: **not one** has a second `data` chunk, a second `fmt`, or a
`fmt` anywhere but +20. On his own data the two walks cannot part company.

**Measured.** Of the reviewer's six crafted files the gate accepts four, and
**three of those four** have their format block somewhere other than +20 -
`+56`, `+48` and `+28`. All three used to be accepted and then thrown away.
`SfxProbe` prints a `layout` line for every accepted file and `-samples`
counts the bank's, so the fact is now visible rather than assumed. On the
user's own data nothing moves: his five loose `.wav` files and both banks -
473 and 463 samples - all put `fmt` first, so this never bit him; it was
waiting for the first mod he downloads that was mastered in an editor.

**One finding refuted rather than fixed, one documented.** A mod that ships
`<NAME>_HEAD.TRM` without the body's `<NAME>.TRM` is silently inert, because
a face is resolved wherever its body came from so that the two wear pages from
one source. That is right, and the review agreed; what was wrong was the
README promising "anything the HD path resolves by name". The README's
not-supported paragraph says it now. No code changed.

#### Round M2 - the wardrobe is a scan, and a mod may bring an outfit (#153)

Round M1 refused `OUTFIT_*.TRM` from a mod on purpose: Lara's wardrobe was
eight rows of a hand-written table in `hd_lara.cpp` and a stranger's outfit
would have been dressed by somebody else's measurements. This round makes the
LIST a scan and leaves the measurements where they belong.

**What the scan finds, and what it passes over.** One engine-free unit,
`specific\hd\hd_wardrobe.cpp`, walks three places in order and the order is
the priority: the IV-VI install's `4\ITEM` and `5\ITEM`, then the I-III
install's `1\ITEM`, then every active mod's `5\ITEM` and `4\ITEM`. Measured
over the two installs the user has: **52 entries**, 22 from IV-VI and 30 from
I-III, with **50 files passed over** - and every refusal says why, in the log
and in `ModelProbe -wardrobe`. Two are `OUTFIT_YOUNG*` of the install (young
Lara carries no weapon and a randomized run hands her one in every level - the
user's decision); four are `OUTFIT_HAIR` and the three `OUTFIT_HANDS_*`, which
are parts of the composite and not bodies; fourteen are the I-III bundle's
copies of names the IV-VI bundle already has, because those copies are
self-contained and better (`OUTFIT_TR1_CLASSIC`: 14,515 vertices in IV-VI
against 8,382 in I-III, and IV-VI page numbers, so no alternate root is
needed at all); and thirty are the `_HD` twins, which have a section of their
own below. The young filter does NOT apply to a mod - three of the four outfit
mods the user runs are grown-up bodies sitting in the young slot, and refusing
them would refuse the very thing the round is for.

**The key.** `46/OUTFIT_TR2_HOME`, `13/OUTFIT_TRX_WETSUIT_1`,
`mod/outfits/aod wetsuit/OUTFIT_CLASSIC` - the source, a slash, the file, and
for a mod its folder in between, because two mods may ship the same file name
and both are then in the wardrobe at once. It has to survive a text line, a
settings file, a command line and a log, so it is forward slashes throughout
and compared case-insensitively.

**One implementation, four callers.** The engine draws out of it, `ModelProbe`
prints it, `Randomize.exe` rolls out of it and the GUI shows it - and all four
compile the same `hd_wardrobe.cpp`, the way the font has one `chardef.inc`. A
list two programs work out separately is a list that will one day disagree
with itself, and here that disagreement would read as "the outfit I picked is
not the outfit I got".

**The properties, and where each one comes from.** The eight measured rows
moved out of `hd_lara.cpp` into `hd_wardrobe.cpp` with the measurements that
produced them; what is left in `hd_lara.cpp` is the enum's two words, which
file an `HD_LARA_OUTFIT_*` number stands for and what to call it in the log.
An entry's flags are resolved in four tiers - a mod's `outfit.txt`, the table
keyed by file name, a measurement off the file, the defaults - and **every
property's origin is in the log line**, so a picture the user does not like
can be traced to the row or the line that made it without opening the source.

**The measured tier, checked against all eight rows.** Only one property has
one: which pages are Lara's own skin. The rule is the throat measurement fix
round K already had - the page whose torso-node vertices inside the neck
cylinder span the greatest depth down the neck axis - and it was run against
every row of the table:

```
  OUTFIT_CLASSIC      8001   the table says 8001            agrees
  OUTFIT_RUSSIA       8001   the table says 8001            agrees
  OUTFIT_VCI          8001   the table says 8001            agrees
  OUTFIT_TR3_ANTARC   8201   the table says 8201 and 8202   the first of two
  OUTFIT_TR3_NEVADA   8201   the table says 8201 and 8202   the first of two
  OUTFIT_TR3_COAST    8201   the table says 8201 and 8202   the first of two
  OUTFIT_TR3_CATSUIT  8201   the table says 8201 and 8202   the first of two
  OUTFIT_SUBSUIT      8047   the table says NOTHING         disagrees
```

Seven of eight, the rule finds the table's first page exactly. The two
disagreements are the same limit twice and both are real: the four Tomb Raider
III guests carry a SECOND skin page, 8202, which is their bare ARMS, and a
measurement taken at the neck cannot reach an arm - the same page number is
bare skin under the Antarctica parka and a latex sleeve under the London
catsuit, and geometry cannot tell the two apart. And the diving suit HAS a
throat page, 8047, and it is the HELMET: the rule is right about the file and
wrong about Lara. So the measured tier gives a new entry exactly one page, its
throat, and the table keeps the eight it measured - which is what stops the
measurement from making the whole diving suit matt.

**The `_HD` twins, and why the plain one is worn.** The thirty
`OUTFIT_TRX_*_HD.TRM` files of the I-III bundle were refused by `hd_trm` with
"has bytes left over". They have no bytes left over: their **vertex stride is
30 and not 24**, and the difference is the two texture coordinates, which
leave the gaps in the bone and weight block and stand at the end as FLOATS.
The layout is arithmetic and then byte comparison - the index block, aligned,
plus vertices times 30 ends at the last byte in all thirty, and the leading
vertices are byte-identical with the twin's in position, normal, layer, bone
and weight. Measured over all **2,923 `.TRM` files of both remasters**,
exactly thirty are laid out that way and every one is an `OUTFIT_TRX_*_HD`
with its plain twin beside it; nothing else in either bundle, at any name.

`hd_trm` reads them now. The wardrobe still wears the **plain twin**, and that
is a measurement too: this engine's vertex holds `u` and `v` as BYTES
(`hd_model.cpp`, the `UBYTE4` lanes of the declaration), and over
`OUTFIT_TRX_WETSUIT_1_HD` **14,208 of its 14,262 coordinates lie off that
1/255 grid** - and off every other grid a texture atlas might use, 1/256 up to
1/4096. Against its own twin's vertices they differ by as much as half a step
in both directions, spread, not by a constant half-texel offset. Wearing the
wide file would mean drawing a picture the file does not describe, and the
twin is the same Lara without the rounding. A wide file with NO twin - a mod's
- is worn, with the rounding written into the log, because a rounded picture
beats no picture.

**How a level says which.** *(Withdrawn in the fix round below: the choice is a
registry value, `hd_outfit`, and this engine has no `wear` line at all - a
sidecar that carries one reaches the log as a line it has no rule for. The
paragraph stands because the reasoning about the version number is still what
the sidecar format rests on.)* A new `.hdm` line, `wear <key>`, the rest of the
line taken whole because a mod's key holds its folder name and may carry
spaces. The version stays **2**: an engine older than this round does not know
the word, ignores the line and draws what it drew before, which is the whole
reason the choice went into a line and not into a new version. At the level
build the key wins where this machine's scan has it, a key it has not got
falls through to the level's own facts with `HDLOG wardrobe wear=... not
found`, and **young Lara stays classic whatever the line says** - the question
is asked before the line is read, so a sidecar cannot put an adult body on the
child.

**What the randomizer does with it, and what it deliberately does not.**
*(Withdrawn whole in the fix round below: the randomizer knows no scanned
outfits. Commit `234df1b` was reverted and `TR5Randomizer/` is byte-identical
to what it was before this round - no divider in the wardrobe window, no
`outfitsOn`, no `wear` line in any sidecar. What is left of the paragraph is
the argument, which is why the decision went the other way.)* The
scanned entries stand under a divider in the wardrobe window, greyed out when
their source is not there, and are **off unless the user ticks one**. Held is
therefore the SELECTION and not the deselection, which is the opposite of the
seven old entries and is not an inconsistency: which scanned entries exist
depends on what is installed and which mods are in the folder, so "on unless
said otherwise" would make a run change silently the day a mod is added. An
entry the roll draws changes **nothing at all** in the level file - the body is
a `.TRM` the engine reads at draw time out of the player's own installation -
so `data[3]` stays 0, no donor is copied, and the roll's whole effect is one
line in the sidecar.

The first design also wrote a `wear` line for the seven old kinds, so that a
level file would say the same thing in two ways. It does not, and the reason is
the user's rule for this round: a seed rolled with nothing new selected has to
produce byte-identical `.trc` **and** `.hdm` files, and a `wear` line for the
old seven changes every sidecar of every run. So a `wear` line means exactly
one thing - the roll chose an outfit that nothing in the level file can name.

**Proved rather than argued.** The randomizer from before this round and the
one after it, seed 380, `mix cross allsanity 8 weapons outfit-tr3` with the
Tomb Raider III installation, over eleven levels: **every `.trc` and every
`.hdm` byte-identical**, twenty-two files, and the risky path was exercised -
five of the eleven rolled a Tomb Raider III guest, two a vanilla donor, two
kept their own. Then the other half: two runs whose candidate list holds
exactly ONE entry each, so both consume exactly one draw at the same place -
one where that entry is the outfit the level already wears and one where it is
a scanned entry. ANDREA1 and JOBY5, `.trc` byte-identical both times, and the
only difference in the whole output is the one `wear` line.

**And the picture did not move.** The eight known outfits, drawn through
`ModelProbe -lara ANDREA1 -outfit <x> -guns pistols` before and after the
round: the `-nodraw` tables are identical line for line apart from the scan's
own new note, and **all eight `.PNG` files are byte-identical**.

**The two round M1 leftovers, both closed.** `hd_lara.cpp` compared
`HDModelPage` - a cache KEY - against a plain page number when it looked for
Lara's skin. It is one helper now, `page_number_of`, which strips the mod
namespace AND the alternate-bundle base, and the held-item rule beside it uses
the same one. The alternate-bundle half was not theory: `OUTFIT_TRX_WETSUIT_1`
wears 8415, 8001 and 8002 of the I-III bundle and the first picture of it
reported "page 8415 is named as her skin and the model does not wear it".

And the page a mod's model asks for but the mod does not ship was cached
TWICE - once under the install's key for the vanilla models and once under the
mod's for the mod's. `HDPageCacheKeyOf` folds exactly that case and no other:
a mod key whose mod has no such `.DDS` and whose origin bundle has none either
becomes the plain number. The merge is safe by construction rather than by
care - in that case `resolve_page` itself does `page = real` and runs the
ordinary chain, so the two keys open the same file, byte for byte. Measured on
ANDREA1's wardrobe with the user's four gun and outfit mods: **25 slots and
33.3 MiB before, 24 and 32.0 MiB after**, and the rendered picture is
**byte-identical**. One page folded (the install's 8001, which the HK USP's
hands ask for and the vanilla wardrobe already held); two more - 8527 and 8528,
which the Spas-12's holster asks for - moved from the mod's namespace to the
install's without a saving, because no vanilla model in that level wears them
any more. The remaining difference to the 21 slots a modless ANDREA1 pays is
the mods' own art, which is not a duplicate at all.

**What the user's own outfit mods do now.** All four are wardrobe entries and
all four draw. Three of them sit in the `OUTFIT_YOUNG` slot and are grown-up
bodies, which is exactly the case the young filter had to be kept away from.
One finding for the player rather than for the code: the **Mistress** mod's
model names pages 8009 and 8114, which neither the mod nor the IV-VI install
has - her boots came out WHITE and her belt buckle with them. Both pages are in
the I-III bundle's `1\TEX`: the mod was drawn against that numbering and wants
one line, `origin=tr1`, in its `mod.txt`. Proved on a copy of the mod rather
than argued: with that line the boots are black and the buckle is brass. That
is round M1's own mechanism doing its work, and the README says it.

#### Round M2, the fix round - the choice is the player's, and the load is quick (#153)

The user played the round M2 build and it cost him two things: a level took
twenty-five seconds to load where it had taken seven-tenths of one, and his
`.hdm` files came back carrying outfits that only exist on his machine. Both
are undone here, and the second one is a design decision and not a bug.

**THE RANDOMIZER KNOWS NO SCANNED OUTFITS.** Round M2 let the roll draw from
the wardrobe scan and wrote the key into the sidecar as a `wear` line. A seed
and its settings travel from one player to another; an installation and a
`Mods` folder do not. So a run rolled on this machine could not be played on
another - the very thing a seed is for - and the fault is not in the
implementation but in whose choice it is. An extra outfit is the PLAYER'S
choice, and it belongs to the in-game changer of round M3.

Commit `234df1b` is reverted whole. `TR5Randomizer/` is byte-identical to
`07cda76` again: the seven old kinds, the wardrobe window, `optOutfitsOff`,
the seed hash, the command line, `Config` and both build scripts. A settings
file written by the M2 build still carries an `outfitsOn=` line and loads
without a word - `Config::load` is an `if`/`else if` chain over the key with
no final `else`, so a key it has no rule for is passed over and the next save
drops it. Measured rather than read off the source: a file with `outfitsOn=`
and `wardrobeFollowsGameFolder=` in it loads with every other key intact and
comes back out without the two.

**Proved.** The randomizer of `07cda76`, built in a scratch folder, against
this one: seed 380 and seed 777, thirteen levels each, `mix cross allsanity 8
weapons outfit-tr3` with the Tomb Raider III installation - **52 files, every
`.trc` and every `.hdm` byte-identical**, and the risky path was exercised
(eight of the thirteen levels rolled a Tomb Raider III guest). No `.hdm` the
new build writes carries a `wear` line at all.

**THE CHOICE IS A REGISTRY KEY.** The engine keeps everything else of round
M2 - the scan, its four property tiers, `hd_trm`'s 30 byte layout, the page
merge, the `hd_lara` refactor, `ModelProbe -wardrobe` - and takes its key from
a `REG_SZ` named **`hd_outfit`** under the settings key where `hd_install` and
every `tomb5.` option already live. `REG_PeekString` is `REG_PeekLong` for a
string and for the same reason: read-only, creating nothing, so an absent
value cannot take `init_tomb5_stuff`'s first run away.

`hd_laradraw` reads it **once per level build**, where the `wear` key used to
be read, and deliberately not once per session: the changer writes it while
the game runs and the next level has to see it. Empty or absent is the
ordinary case and is exactly what the engine did before round M2. A key the
scan knows **wins over the level facts and over `data[3]`**, because it is the
only one of the three that a person chose on purpose, and the log line says
what it overrode. A key the scan has not got falls through with
`HDLOG wardrobe hd_outfit=<key> not found - level facts`. Young Lara stays
classic whatever it says - the question is asked before the value is read.

`hd_markers` no longer knows the word `wear`, so a sidecar the M2 build wrote
reaches the log as `markers line this engine has no rule for`, which is what
every engine older than round M2 always did with it.

**THE TWENTY-FIVE SECONDS, MEASURED BEFORE ANYTHING WAS CHANGED.** A counter
in `HDPageCacheKeyOf`, over ANDREA1's models with the user's nine mods:
**249,702 calls, of which 25,058 reached the mod branch, and those 25,058 cost
770.8 ms - 30.8 microseconds each.** Without the mods the branch is never
reached and the cost is zero, which is why the regression only showed with a
`Mods` folder.

The branch is not cheap by nature. It asks `HDModsFindPage`, which is two
`std::filesystem` probes - each a UTF-8 to UTF-16 conversion and three string
joins - and then up to three more probes into the mod's origin bundle, the
last of which `fopen`s the file. Round M2 put that on the hottest path this
engine has: **once per CORNER of every triangle of every model a mod
supplied**. The user's ANDREA1 log says `mod:10`, and ten mod models at that
rate is where his twenty-five seconds went.

The fix is that the question has far fewer answers than it was asked for. A
vertex names its page by its LAYER, an index into the model's own texture
list, so a model has exactly `trm.textures` answers however many triangles
wear them. `build()` works them out once into a small table and reads the
table in the loop, and that is not an approximation - `HDTrmPageOf` IS
`texture[layer-1]`, so the two agree value for value. `HDPageCacheKeyOf`
remembers its answer per key as well, so no other caller can grow the same
cost back; the memory is dropped in `HDPageCacheAltRoot`, the one call that
can change an answer.

ANDREA1, `-all -nodraw` against the classic level, five runs, the median of
the tool's own `ms building`:

```
                              without -mods   with -mods
  07cda76, before round M2        143 ms        313 ms
  1ef8f9f, round M2's head        136 ms       1295 ms
  this round                      133 ms        278 ms
```

and the whole `-all` table - every slot, every page, every range, every note -
is **identical** between the M2 head and this round, with the mods and
without. The eight known outfits are byte-identical `.PNG` files against the
M2 head. The `with -mods` column is now below where it stood before round M2,
because round M2's own page merge is doing its work with the cost taken off
it.

**WHAT THE LOG SAYS NOW.** The level's models line only said `models ms=`, so
the whole hunt was for which half of it had grown. It now carries the phases
of `build_ms` - `find_ms`, `key_ms`, `page_ms`, `buf_ms` - and three counters
from the `.TRM` reader, `trm_open`, `trm_load` and `trm_kib`. The wardrobe's
own scan line gains `ms`, `probed`, `read_kib` and `geometry`.

Those numbers answer the user's own hypothesis - "the renderer tries to load
every outfit at every level change" - with measurements instead of a promise.
Measured on this machine: the scan probes **112 headers and reads 28.8 MiB
ONCE per session** and its `geometry` count is **0**, because it reads headers
and never a body; and a level build's `trm_load` is **14**, the parts Lara
actually wears. The wardrobe is not loaded at a level change, and now the log
says so every time.

The phases also name what is left. `find_ms` - the walk of the ITEM folders
that turns a slot name into a file - is 210 ms of the 278 with the user's nine
mods against 60 ms without them. That is round M1's resolver doing its work,
it was there before round M2, and it is now a number in the log rather than
something to guess at.

#### Round M2b - the hands, and a mod's own inventory model (#153)

Four things the user asked for after playing the round M2 build, and the six
small findings its review left over. Everything here is the wardrobe and the
ring; the randomizer is not touched at all.

**THE GLOVES ARE NEVER WORN.** "In every level with every outfit Lara has had
TR5's bare hands for the weapons; we never used the gloves - force it, then it
is uniform." `takes_gloves` answers false for every outfit now. The wardrobe's
`gloves` flag, `outfit.txt`'s `gloves=` line and `kPart`'s gloves column are
all still parsed, still printed and still logged - a file has to go on saying
what it says, and a modder who writes the line is owed the reason rather than
silence - and none of them reaches a model name any more. One line per level
build when something asked, and a `MODLOG` line when an `outfit.txt` did.

The review's brief said no outfit in the wardrobe asked for gloves. One does:
`OUTFIT_RUSSIA`'s row says `gloves=true` and has since fix round C, so **JOBY1
to JOBY5 drew `HAND_GLOVES_*` until this round**. That is the one picture the
round moves on an unmodded game: of the eight known outfits drawn with pistols
before and after, seven are byte-identical `.PNG` files and the Russia one is
not.

**THE MIRRORED HAND WEIGHTS ARE FOLDED BACK.** A `HAND_` file is modelled once
and mirrored, and an exporter that mirrors the geometry without the bone names
leaves vertices of one hand carrying weight on the other. In the bind pose the
two hands sit beside each other and nothing shows; the moment an animation
moves them apart the far bone drags those vertices into the gap. That is the
user's RICH2 screenshot - a sheet of skin stretching out of the wrist into
the air.

`HDTrmFoldMirrored` (hd_trm.cpp) moves the far side's weight onto that bone's
counterpart on the near side, by the vertex's majority side, and renormalises
the three weights to 255. A vertex weighted exactly half and half has no
majority, and the tie is broken by the sign of its own **x**: over every hand
file of the install - all 94 of them, and 4\ITEM is the only ITEM folder in
that bundle that holds one - and all seven hand files of the user's three gun
mods, there are 6,723 crossed vertices; 6,719 of them have a majority and on
every one of those the majority side and the sign of x agree, not one
disagrees, and the four that have no majority are the ties this rule exists
for. So the tie-break is the same rule read off the geometry instead of off
the weights.

**THE BAR IS A MEASUREMENT AND NOT A TASTE.** Over all 101 of those files a
crossed vertex's share on the far hand is one of exactly two things:

```
    4 per mille      ONE weight unit out of 255 - 3.92, which the share's own
                     arithmetic rounds to 4 - the exporter's rounding.
                     6,677 vertices: every crossed vertex of every file of the
                     install (5,884, out of 485,491 vertices), and of the Uzi,
                     the Spas-12 and the gloved pistol mod (793).
    371..500         the 46 crossed vertices of the user's HK USP
                     HAND_BARE_PISTOL, four of them exactly 500 - half the
                     vertex on the wrong hand. The only 46 in the whole set.
```

Nothing at all lies between the two, so the bar is an eighth (125 per mille):
31 times the largest share the remaster's own files carry and a third of the
smallest the mod's do.

What the bar buys was measured too, with `ModelProbe -handfold`, which skins
every vertex twice through the same node matrices the card is given and takes
the distance between the two answers. Which hand parts a run loads is decided by
the LEVEL's weapon slots, so the sweep walks all thirteen classic levels and
covers all **eleven** hand parts of the table in `hd_lara.cpp`; four of them -
REST, FLARE, TORCH, CROWBAR - carry no crossed vertex at all and never move.
Folding the other seven at a bar of **0** moves them, largest of the seven:

```
    0.56 units    both forearms turned 50 degrees out - the pose the round
                  was asked to measure in. DEAGLE and DEAGLE_LASER; PISTOL
                  0.51, UZI 0.52, SHOTGUN, HK and GRAPPLE 0.53.
    0.84          both upper arms straight out sideways; the others 0.80-0.81.
    0.97          upper arms out 90 AND forearms folded 90 on top of that -
                  all seven, to the hundredth.
    2.00          UZI, in a contortion no animation reaches.
```

Small - and pointless, because one weight unit on the far hand cannot make a
sliver. With the bar the remaster's own files come out of the reader **byte for
byte as they went in**, which is a guarantee rather than a measurement. The
mod's 46 vertices move up to **65.49 units** (mean 51.27) in the brief's pose,
which is the sliver, and the two rendered pictures of that one hand - `-nofold`
and without it - show it and then show it gone.

**MOD GUNS ON THE INSTALL'S HANDS.** A mod may ship a weapon hand - a hand and
the gun in it, on two or three pages of one file - and paint the HAND half with
pages of the Tomb Raider I-III bundle, which its `mod.txt` reaches with
`origin=`. The user's IMI Uzis do exactly that: `HAND_BARE_UZI` names 6111 and
6112, its own, and **8002 and 8010, which are that bundle's skin sheets**. He
calls it "the Uzis pull on gloves".

`hd_model` asks the page cache's own chain where each of a model's pages really
resolves (`HDPageCacheOriginOf`, which calls `resolve_page` and loads nothing),
and when any of them lands in the origin bundle it does two things: the mod's
triangles on those pages are **dropped**, and the **install's own file of the
same name** is loaded into a companion slot, cut to the pages it shares with
`HAND_BARE_REST` - whose every page is skin and nothing else. The two are
drawn together, with the same matrices, the same node mask and the same light,
and the user sees TR5's hands holding the mod's guns.

The rule is general over any hand part and any origin, and it fires on nothing
else: the HK USP and the Spas-12 both wear the install's own 8001 and have no
`origin=` line, so they are untouched, and so is `HOLSTER_UZI`, which wears the
gun's own pages alone. Measured on ANDREA3 with the user's nine mods: the mod's
hand keeps 2 pages and 31,267 vertices on nodes 10 and 13, the install's twin
brings 1 page, and `HOLSTER_UZI` stays whole at 27,624 vertices on nodes 1 and
4.

The decision is made inside `hd_model` and not by the caller, because only
`hd_model` has the file open and the pages resolved; a caller would have to
read the `.TRM` a second time and keep a copy of `resolve_page`'s chain - the
very drift round M1 caught in `ModelProbe`'s page rule.

**A MOD'S OWN INVENTORY MODEL, IN THE RING AND IN THE CORNER.** The Tomb Raider
I-III remaster draws the inventory out of `<BASE>_OPTION` models and IV-VI out
of `EXTRA_<BASE>_ITEM`. A mod written for I-III carries the OPTION names with
it - the user's IMI Uzis ship `UZI_OPTION` and `UZI_AMMO_OPTION` - and nothing
in this engine ever looked for such a name, so the ring went on showing the
install's own model while the mod's gun was in Lara's hands.

The name is **derived** and not listed: the slot's own name with `_ITEM`
replaced by `_OPTION`, the way rule 3 derives the EXTRA names
(`HDBindModOption`). It is taken only when a mod really ships it AND no mod
ships an `EXTRA_<BASE>_ITEM` of any of the three prefixes - a mod that brings
the IV-VI name has said which convention it means, and it wins. The pickup
lying on the floor keeps `EXTRA_*` either way; only the ring and the display in
the corner read this.

A file out of another game stands in another frame, so `HDModelRingFit` (named
`HDModelBoxFit` when this was written; round M3 took the rule off the box and
renamed it) puts it
into this slot's: centred on its own bounding box, turned so that its longest
axis lies where the target's longest lies and its shortest where the shortest
lies, and scaled by the ratio of the two longest extents. Nothing about either
game is written down - the two boxes say which way round they are:

```
  install 4\ITEM\EXTRA_UZI_ITEM        266 x  41 x 212   x longest  (two guns
                                                          lying flat)
  mod     UZI_OPTION                   147 x 253 x 159   y longest  (one gun
                                                          standing up)
      -> y onto x, z onto z, x onto y; scale 266/253 = 1.05

  install EXTRA_UZI_AMMO_ITEM          147 x  26 x 185   z longest
  mod     UZI_AMMO_OPTION              162 x  28 x  74   x longest
      -> x onto z; scale 185/162 = 1.14
```

Fitting the whole box INSIDE the target's was refused by the same measurement:
the vanilla Uzi pickup is two guns side by side and 41 units thick, so that
rule would have scaled the mod's upright gun to a sixth of its size to match
the thinnest axis.

It is drawn **rigidly on the item's root node**, which is the honest statement
about a file whose bone numbers are another game's, and the correction goes in
FRONT of the engine's own matrix rather than into the projection - the ring's
off-centre term lives in the projection precisely because it must not touch a
normal (fix round P2), and this one must, because the model is turned.

`-ringcheck` and `-pickupcheck` on ANDREA1 with the mods active: worst place
error **0.00013 px** and **0.00008 px** against a budget of 1, and the ring's
light unmoved. Both inventory models were rendered and looked at.

**AND WHAT THE NEW QUESTION COSTS, because it is asked of every slot.** The
`_OPTION` lookup runs once per loaded slot at every level build and walks every
active mod's two ITEM folders - the shape of thing that turned round M2's level
build into twenty-five seconds - and it stands OUTSIDE the models timer, so
without a line of its own it would be time nobody could account for. Measured
with `ModelProbe -inventory`, which asks the engine's own `HDBindModOption`
over the level's whole slot list: ANDREA1, 130 slots, the user's nine mods,
**24.6 ms** - because the three EXTRA prefixes were being asked first, for every
`_ITEM` slot, when only one of the four names decides anything. Asking the
OPTION name first and the prefixes only after it hits: **7.7 ms**, and 0.11 ms
with no `Mods` folder. The engine writes those numbers itself now,
`HDLOG level=<L> ring asked= of= found= ms=`.

**WITH NO MODS FOLDER NOTHING IN ANY OF THIS HAPPENS**, and that is not an
argument but a diff: `ModelProbe -all -nodraw` over ANDREA1, the build before
this round against the build after it, is identical line for line - every slot,
every model, every page, every range, every verdict - apart from the
millisecond columns and the peak commit.

**AND THE PEAK COMMIT IS NOT A LINE TO READ THREE TIMES.** It is a memory
number - `PeakPagefileUsage`, the process's high-water commit charge - and the
round M2 review read a 10 MiB step out of three runs of each build. Measured
properly (`fix/round2/peak_20pairs.txt`, `peak_control.txt`, `bisect_peak.txt`):
the number's own run-to-run spread for ONE unchanged binary is about 12 MiB, min
to max, and its values are bimodal, clustering near 89 and near 95 MiB with
occasional excursions past 100. Over 20 interleaved pairs the means are 95.3 MiB
after the round against 91.6 before it; reversing which build runs first leaves
that unchanged (95.4 against 91.7), so it is not an order effect, and the same
binary against itself gives 96.2 against 97.2. A ModelProbe built at each of the
round's five commits and swept ten times gives means of 91.1, 93.4, 92.2, 91.5,
92.6 and 92.2 MiB - within 2.3 MiB of each other, with **no step at any
commit**, and the pre-round baseline itself reached 100.2 MiB in that sweep.
Every run creates a Direct3D 9 device (`-nodraw` skips the drawing, not the
device) and decodes 45 MiB of pages, and the driver's own commit is in this
figure. The single pair kept as this entry's own diff makes the point by
itself: there the build **before** the round measures 96.7 MiB and the build
after it 88.8. So: a real memory number, no measurable step, and three samples
cannot resolve it.

**THE SIX SMALL FINDINGS of the round M2 review.**

1. The README said the wardrobe finds 24 IV-VI outfits. It finds **22**: the
   bundle holds 24 `OUTFIT_*` bodies and the two `OUTFIT_YOUNG*` are passed
   over. The line says both numbers now.
2. `hd_mods.cpp`'s refusal of `OUTFIT_*.TRM` still called a foreign outfit
   "round M2's problem". Round M2 answered it, and the refusal stands for a
   different reason now - a mod's outfit is an ENTRY in the wardrobe scan and
   never a replacement - which is what the comment says.
3. `HDTrmCounters`' note was wrong twice. `loads` counts **calls**, before the
   file is opened, so a call for a path that is not there adds nothing to
   `opens` or `bytes`; and the reader really is reached from two threads - the
   load thread's `HDTrmExists` in the level scan and the main thread's
   `HDTrmProbe` in the wardrobe scan. There is no race because `file.cpp`
   JOINS the load thread before `HDLevelBuild` runs, and the note says that
   rather than "single-threaded".
4. `props_of` ended in a walk of the whole wardrobe scan with a `std::string`
   per entry, and `HDLaraPartSheen` calls it once per part per draw. One answer
   is remembered, keyed by the outfit, dropped in `HDLaraLevelEnd`.
5. `readable()` reads whole files to look at headers - 28.8 MiB (read_kib=29525) once per
   session - and goes on doing so. The reason is in the comment with the
   numbers: the prefix a header needs is 140 KiB of a 480 KiB outfit file
   (the index list sits inside it), so the saving would be two thirds of the
   bytes and not the file; the
   whole scan costs 24 to 36 ms once, and reading less would mean two different
   lengths inside the one `.TRM` parser a downloaded mod's file goes through.
6. The round M2 entry above presented the `.hdm` `wear` line and the
   randomizer half as current. Both now say, at the top of their own
   paragraph, that the fix round withdrew them.

**TOOLS.** `ModelProbe` gained `-handfold` (the fold, its counters and the
displacement it causes in the pose the command line asks for - the engine's own
call, not a copy), `-nofold` and `-nohandmix` (the two halves of the picture
from before the round), and `-inventory` (a mod's `_OPTION` model in the ring's
place, through the same two calls the engine makes). `hd_model` exports
`HDModelFindFile`, so a tool asks the engine's own model search instead of
keeping a copy of it.

**THE FIX ROUND.** The round's own review found no defect in what the engine
does and six in what is written about it. Five are corrected here and one is
code.

1. **The counts beside the bar were taken over a sample and written down as if
   they were the whole set.** They said "every hand file of the IV-VI install -
   the eleven `HAND_BARE_` and the four `HAND_GLOVES_`", 1,311 crossed vertices
   at the low share and 1,357 at the tie-break. The bundle holds **94** hand
   files, not fifteen: 4\ITEM has thirty `HAND_BARE_`, thirty `HAND_GLOVES_`,
   thirty `HAND_XRAY_` and four `HANDS_BARE_`, and games 5 and 6 have none at
   all; and the user runs **three** gun mods with **seven** hand files between
   them. Recounted over all 101 files with a reader written from the layout
   table alone: **6,723 crossed vertices** - 6,677 at one weight unit out of
   255 (5,884 of them the install's own, out of its 485,491 hand vertices) and
   **the same 46** at 371 to 500 per mille, all of them in the user's HK USP
   `HAND_BARE_PISTOL`, with nothing whatever in between. 6,719 have a majority
   and every one of those agrees with the sign of its own **x**; the four that
   do not are the HK USP's exact ties. **So the conclusion is unchanged and
   `HD_LARA_HAND_FOLD_BAR` stays 125** - it still sits in a total gap, 31 times
   the largest share the remaster's own art carries - and the four places that
   carried the old numbers (`hd_lara.h`, `hd_trm.h`, `hd_trm.cpp` and this
   entry) carry the measured ones.
2. **The dead `gloves` flag was documented as dead in two places and as live in
   four.** The override table's own header, the `kRow` column legend,
   `HDWardrobeEntry::gloves` and `HDLaraPartModel` - the one function that
   still names the column - all read as though it decided something. Each now
   says what is true: parsed, printed and logged, read by nothing, and
   `takes_gloves` is the one line to change if the decision is revisited.
3. `HDLaraPartSheen`'s new comment named `kSheenLeather`, which the same commit
   removed. Leather is the holster rig's `0.18/20` in `kPartSheen`, and the
   comment says so - the same words its sibling forty lines up already used.
4. **The twin was in `verts=` and not in `vb_kib=`.** The install's hands under
   a mod's gun added to the vertex and index counts but not to the ranges, the
   pages or either byte count, so one line described two different sets of
   models - about 155 KiB of buffer, under the user's Uzi mod, that the count
   saw and the budget did not. It is counted in every column now.
5. **Two page cuts could have collided in silence.** The twin rule wrote its
   own cut over whatever the caller had asked for. No part does both today -
   the only `onlyPage` is the holster rig, which is kind 4 and never gets a
   twin - but the two were made ONE mechanism so that they could not disagree,
   and this was that disagreement waiting. They combine now, as a drop set
   (what a keep set leaves out is dropped anyway), with a line in the log; a
   drop set is used rather than a keep set because dropping every page of a
   file is representable and keeping none of it is not.
6. **An existing log line was deleted rather than given a gate that works.**
   "`%s: no gloved variant in the wardrobe`" fired when a gloved outfit met a
   hand part with no gloved twin - a case there is none of today, and the line
   was written for the day there is. Its gate was `takes_gloves`, which now
   answers false for everything, so the line was dead; it is back, gated on
   what the wardrobe ASKED for, which is the same question and one that can
   still be yes. Diagnostic seams are not thinned.

**THE SECOND FIX ROUND.** The next review found no defect in the engine either,
and three more numbers in the record that the build does not print - the same
species again. Seven findings, listed the same way; one of them does not stand.

1. **The mod's displacement was the draft's number, not the build's.** The entry
   said the mod's 46 vertices move up to 65.71 units, mean 51.25. The tree as
   committed prints **65.49**, mean **51.27** - re-measured here
   (`fix/round2/fold_numbers.txt`) and corrected above. No code line in the fold
   or in `-handfold` changed after the figure was taken; the draft simply
   survived into the entry.
2. **"At most 0.51 units" was one part's number written as the set's, and the
   0.97 named the wrong pose.** In the pose the round was asked to measure in,
   `HAND_BARE_DEAGLE` and `HAND_BARE_DEAGLE_LASER` move **0.56**, two lines
   under the 0.51 in the round's own sweep; and "both arms straight out
   sideways" is **0.84**, while 0.97 is the pose with the forearms folded 90 as
   well. Re-measured over **all eleven** hand parts by walking every classic
   level - which hand parts load is the LEVEL's weapon list, not `-guns` -
   and both `hd_lara.h` and this entry now carry the whole table
   (`fix/round2/sweep_install.txt`). The argument for the bar is untouched by
   either correction.
3. **`HDTrmFoldCounts::tied` said the opposite of what the code does.**
   "crossed, no majority - left exactly as they were". `HDTrmFoldMirrored`
   counts a tie and then folds it like any other vertex above the bar, taking
   the far side from the sign of the vertex's own x - which is the rule the
   same header explains eight lines higher, and the reason it exists. Anyone
   reading `crossed=46 folded=46 tied=4` against that comment concluded four
   vertices were skipped, when those four are the user's exactly-128/128
   vertices the tie-break was written for. The comment says what happens now,
   and both the HDLOG line and `-handfold`'s table footer spell it out in words
   rather than leaving `tied=` to be guessed at.
4. **The peak commit was filed under "timing".** It is a memory number and it
   has no measurable step - both halves are measured above.
5. **The ring's log line printed both boxes and the scale and never said the
   model leaves the box.** The scale matches the two LONGEST extents only, so
   the mod's upright Uzi ends up about 154 units thick where the vanilla item
   is 41 - **3.73x**, measured - which is the intended picture and was argued
   for at `HDModelRingFit`, but a ring that looks crowded in a play test had
   nothing to point at. `HDModelRingFitOverflow` puts the number on it, out of
   the same sorted axes the fit uses (one `sort_axes`, so the two cannot
   drift), and the line now ends in "and it stands 3.73x out of that box on y"
   or "and it stays inside that box". The Uzi AMMO model, whose proportions are
   closer, comes out at 1.25x.
6. **`props_of`'s memo comment was called wrong, and is not.** The review read
   "dropped in `HDLaraLevelEnd`, which every level build calls first", grepped
   for callers, found `HDLaraDrawEnd`'s and concluded no level build drops the
   memory. `HDLaraLevelBuild`'s own first statement is `HDLaraLevelEnd();`
   (`hd_lara.cpp`) - the sentence was right. It now names that line so the
   search does not have to be repeated.
7. **Eleven hand parts, not twelve.** Counted in `kPart`: REST, PISTOL, UZI,
   SHOTGUN, HK, DEAGLE, DEAGLE_LASER, GRAPPLE, FLARE, TORCH, CROWBAR. "Twelve"
   has stood in that comment since #123 (and in the C2 paragraph of section 4)
   and was never the number of rows. All eleven do have a `HAND_GLOVES_` twin
   in `4\ITEM`, which was the claim that rested on it.

**WHAT IS NOT PROVED.** Everything here is `ModelProbe`'s renderer, which is
the engine's renderer with no game around it. The play test still owes the
hands in motion, the ring opened in the game with the Uzi mod in the folder,
and whether a Russia level looks right with bare hands.

#### Round M3 - the outfit is the player's, and the vulture stops talking (#153)

Four things: a key in the game that changes Lara's outfit without leaving the
level, the footsteps that follow the body she is wearing, the ring's fit read
off the shape instead of off the bounding box, and a sound the randomizer
imported that the remaster's bank had other plans for.

**THE MENU IS THE PAUSE MENU'S TWIN, on purpose.** F4 stops the world the way
`S_PauseMenu` stops it - `CreateMonoScreen` freezes the picture, `S_UpdateInput`
with `SetDebounce` drives it, `dbinput`'s six bits are the only inputs it reads,
`SFX_MENU_CHOOSE` and `SFX_MENU_SELECT` are the sounds, `PrintString` and
`font_height` draw it in the two columns the options page uses, and a literal
with no string id goes through `CustomText` exactly as the `Language` row does.
A gamepad works in it because the bits it reads are the ones the pad already
drives, and `InventoryActive` is held for the whole of it so that the press
which closes the menu cannot also roll Lara - which is #115, and the reason the
latch exists. It is not in the title, not with Lara dead, not while the
binoculars or the laser sight are up; the inventory and the pause menu cannot
be open under it at all, because both run their own frame loop and the call
site is not in it.

The list is the round M2 wardrobe scan in its own order, a window of nine rows
with the selection kept in view, each row carrying the source it came from -
`IV-VI`, `I-III` or the mod's own folder name - and a `*` on the one being worn.
The first row is **Level facts**, which is not an outfit: it is what clears the
setting again, and it is what a player who has never opened this menu has.
Under the list stands one value row, the highlighted outfit's footsteps, and
left or right turns it over.

**WHOSE CHOICE IT IS, and where it lives.** Round M2's fix round settled that
an extra outfit is the PLAYER'S and not the run's - a seed and its settings
travel between players, an installation and a `Mods` folder do not - and put
the answer in a registry value that nothing wrote. This writes it. The outfit is
the `REG_SZ` **`hd_outfit`** the level build already reads; the footsteps are
one `REG_DWORD` per outfit under **`tomb5\hd_footsteps`**, the wardrobe key as
the VALUE's name, because a key holds slashes and spaces and a subkey name may
hold no backslash while a value name may hold almost anything. `OpenRegistry`
creates the key it is handed, which is safe here and nowhere earlier:
`init_tomb5_stuff` asks `REG_KeyWasCreated()` once at startup and has long since
run by the time a level is on the screen.

**AND SHE WEARS IT AT ONCE.** `HDLevelRedress` is the level's own model build
run a second time with the game stopped: `HDObjectRelease` and
`HDObjectLevelBuild`, which is exactly the pair a level change makes, while the
rooms, their page cache and every classic table stay where they are - her
meshes, her holsters, the weapon in her hand and the inventory are not touched
at all. It is safe on the main thread because there is no other one: the load
thread that builds a level is joined before `HDLevelBuild` ever runs
(`specific\file.cpp`, the note at the end of `LoadLevel`), and the caller has
stopped the world, so no frame is being drawn and nothing else is reading the
models.

**THE CAP IS BYTES AND NOT A COUNT, and the measurement is why.** The page
cache never gives a page back - other models hold slot indexes, which is the
A2 note in `hd_pagecache.cpp` - so every outfit worn in one level stays in it.
Measured with the new `ModelProbe -pagelist` over ANDREA1 with the user's own
wardrobe, all 56 entries, one run each:

```
  one outfit's wardrobe        9 to 28 pages, 12.00 to 37.30 MiB
  its OWN pages (what the rest of the wardrobe has not got)
                               up to 15 pages, 20.00 MiB
  the dearest single change    +10 pages, +13.33 MiB
  the mean over all 2970 ordered pairs      +2.94 MiB
  every one of the 55 worn in one level     106.64 MiB, 80 distinct pages
```

So a count would have been a guess and the bytes are not: a level may spend
`HD_REDRESS_ALLOWANCE`, and that number is not invented either - it is the
**64 MiB of slack `AutoBudgetBytes` already holds back** for what a driver
keeps that we do not know about. At the measured mean that is about twenty
changes and at the measured worst four; past it the menu says *applies at the
next level* and the log says how much has been spent. The alternative - freeing
the old outfit's pages - was refused for the reason the cache carries: a slot
index another model holds does not move, and a page given back under one would
draw white.

**THE FOOTSTEPS ARE TWO SETS, AND THE OUTFIT CHOOSES.** Her own foot reaches
the mixer through an ANIMATION COMMAND - `game\control.cpp` reads `num = cmd[1]
& 0x3FFF` out of the animation and calls `SoundEffect` - so bending sound id 0
and sound id 4 costs two shorts, with no reload and no allocation after the
level is up.

*This paragraph said "and that is the WHOLE switch, there is no material branch
and no second caller", and that was wrong.* The grep it rested on could not
have found the second caller, because that caller does not use those names:
`game\footprnt.cpp` plays the SURFACE under her foot on the same frame, sound
ids 288 to 294, and it is 4.7 times as long as the foot itself. See the fix
round below, which measured it and takes it away while she is barefoot.

**BOOTS ARE NOT A MOD'S TO TAKE.** The samples behind those two ids are loaded
from the first UNMODDED bank of the chain and not from a mod's bank or a mod's
loose `.wav`. That is the user's decision and his own mod is the case: the
Underworld bare footsteps made Lara barefoot in boots, in a wetsuit and in a
parka alike, because a sound mod replaces a sound for every outfit at once.
With the outfit choosing between two sets, the two have to BE two.

**BARE** goes into `DS_Buffers` slots after the level's own, with two spare
`SAMPLE_INFO` entries `LoadSamples` now asks `game_malloc` for - sixteen bytes,
in the one place where the level's bump allocator is still open. What goes in
them is what the ORDINARY chain plays for ids 0 and 4 when a mod's bank is the
one being played, so a barefoot sound mod is heard exactly where it belongs;
and otherwise the remaster's own barefoot recordings, **sound id 417 of the
bank**, which no level carries and which is therefore reached by bank sample
number rather than through the level bridge every other sound uses.

**THE INFOS' FIELDS, AND THE ONE THAT IS NOT THE LEVEL'S.** Everything about
how a footstep is PLAYED stays the level's - the volume, the hearing radius,
the randomness and the play mode - because those describe the game and not the
recording. Two do not: the sample number and the count are what was really
loaded, and the PITCH is the bank's. Measured, and both of the user's banks
agree: id 0 is volume 191, radius 10, randomness 0, **pitch 12**, flags 0x6010,
and every one of the sixteen classic levels carries exactly those bytes; id 417
is volume 191, radius 10, randomness 0, **pitch 0**, flags 0x6014. The remaster
turned the random pitch off for those five recordings and that is a statement
about them, so it is kept.

**AND THE LANDING, MEASURED RATHER THAN ASSUMED.** The boots are four steps of
33 to 41 ms and one landing of 175 ms - a landing is **4.7 times** the length
of a step and **2.4 times** its peak, and it is the only thing that separates
the two. Of the bank's five barefoot recordings none stands out that way:

```
  the boots      id 0   38 / 40 / 38 / 33 ms   peak 10690..12563  rms ~1937
                 id 4   175 ms                 peak 27351         rms 2728
                        4.7x the steps' length, 2.4x their peak

  bare (id 417)  462..466  106 / 86 / 134 / 69 / 113 ms
                        the longest is 1.4x the mean of the other four;
                        the loudest by rms is 1.25x theirs, and it is not
                        the longest one - 464 is the longest and the
                        QUIETEST of the five
```

So **all five are steps**, the landing keeps the boots' recording, and the log
says so in words rather than leaving it to be worked out. Cross-checked against
the user's Underworld mod in case the two were the same takes resampled: they
are not - his are 286 to 302 ms at 48 kHz against 69 to 134 ms at 22.05 kHz -
and his mod DOES distinguish its landing, at 4.4 times the RMS of its own
steps, so when that mod is the source its id 4 is the bare landing and the
second spare info is used.

**CORRECTED THE SAME NIGHT, FROM THE REMASTER ITSELF.** The user recorded the
remaster with a barefoot outfit - eight standing jumps, then running in a
circle - and every footfall in the recording was matched against the ten
recordings above by normalised cross-correlation (pitch variants of +-6 %,
`scratchpad/round153/video/match.py`). Under each of the eight landings the
remaster played one of the SAME FIVE (463 / 462 / 462 / 466 / 465 / 462 / 464 /
464, at 0.65 to 0.93), and the boots' 175 ms landing never rose above 0.32
anywhere in the clip. So the remaster has no barefoot landing of its own
either: it draws the landing from the five steps. Ours does the same now - the
second spare info points at the same takes with the level's own id 4 volume and
radius - and the log line says "the landing drawn from the same recordings".
The measurement above stands; only the conclusion drawn from it changed.

**THE FOLLOW-UP OF THE SAME NIGHT** (2026-09-14, after the third review's lows):
the window closing with the menu open now counts as cancel instead of falling
through to a write and a re-dress during shutdown; Alt+Enter inside the menu
toggles the display and chooses nothing, the pause menu's own guard; a footstep
flip is written for the row it was made on when that row is LEFT, so moving to
another row and choosing it no longer drops the flip and pins the other row's
default as a choice; the game clock restarts AFTER the re-dress, so its cost no
longer becomes catch-up ticks of game logic; and F4's edge is read before the
other conditions, so a key held through a cutscene does not open the menu the
moment the cutscene ends. And in the randomizer, ANIM_PICKUP_UW (130) joins the
always-list: found by the user in Andrea1, it is a Core stub in seven levels
(Andrea1, joby3, joby5, rich1, rich2, rich3, richcut2) and the randomizer puts
pickups under water everywhere.

**THE ROOM FOR THEM.** `MAX_SAMPLES` is 450. Measured over all sixteen classic
levels the sample count runs from 76 (JOBY4) to 148 (RICH2), so five more fit
with 297 to spare, and a randomized level carrying all four Tomb Raider III
guests adds 21 and still fits. The check is made before a single sample is
decoded; a level that has no room says so and the menu row reads *unavailable*,
as it does with no bank at all.

**EVERYTHING OF THIS IS INSIDE THE HD GATE**, the F4 key and the boots' bypass
included. With `hd_enable` off the two ids take the ordinary chain and a sound
mod plays exactly as round M1 left it - that half of round M1 is independent of
`hd_enable` and stays so.

**THE RING: A BOX IS NOT A SHAPE.** Round M2b fitted a mod's inventory model by
matching the two bounding BOXES, and the user's picture of the result is why
that is over. His `UZI_OPTION` is a PAIR of guns and its box - 147 x 253 x 159,
y longest - reads exactly like one tall gun standing up; the rule laid the pair
flat, the two guns one above the other with a gap between them.

The principal axes of the vertex cloud say which way a model really runs, and
they are taken in `build()`, in the same loop the box is taken in, because that
is the one place the vertices are in hand. An eigenvector has no sign and a gun
has two ends, so each axis points to the side on which the cloud reaches
further from its own centre of mass, and the third is the cross product of the
other two - the frame can rotate a model and can never mirror it, which the box
rule had to repair with a negated axis. Measured:

```
  install EXTRA_UZI_ITEM       box 266 x  41 x 212   cloud 256 x 236 x  42
                                                     flat along -y
  mod     UZI_OPTION           box 147 x 253 x 159   cloud 242 x 240 x 132
                                                     flat along -x
  install EXTRA_UZI_AMMO_ITEM  box 147 x  26 x 185   cloud 184 x 143 x  26
  mod     UZI_AMMO_OPTION      box 162 x  28 x  74   cloud 165 x 107 x  22
```

Two flat things, each flat along a different axis, and the boxes said nothing
of the kind. Sending the source's k-th axis onto the target's k-th lays the
mod's pair into the plane the vanilla item lies in, and the ring's own per-item
rotation then presents the two alike. The overflow figure falls from **3.73x to
3.36x** for the Uzi and from **1.25x to 1.00x** for its ammo, which now sits
inside the room the ring keeps for it; what is left of the Uzi's is the mod's
own geometry, 132 units of thickness where the vanilla item is 42.

**AND THE TOOL CAN NOW SHOW THE RING.** `ModelProbe -inventory` drew the item's
OWN frame, which is not what the player sees - the ring puts the item's three
angles from `inventry_objects_list` on the matrix stack first, and for the Uzi
those are 90, 135 and 90 degrees. A picture without them says nothing about the
picture with them, and that is precisely the gap round M2b fell into. `-invrot
<x> <y> <z>` puts them there the way `phd_RotYXZ` does, and it is calibrated
against the user's own words: `EXTRA_PISTOLS_ITEM` with its own `0x4000 0xAD4C
0xBD40` comes out as two crossed pistols facing the camera, which is what he
described seeing in the ring.

**WHAT THE RING PICTURE SHOWS, AND WHAT IT DOES NOT.** The vanilla
`EXTRA_UZI_ITEM` is ONE Uzi lying flat (1371 vertices; from above the whole gun,
from the front its edge). The mod's `UZI_OPTION` is TWO, and they are not a
crossed X in the file: face-on to their own plane they stand side by side and
nearly parallel. So no rotation of that file can produce the X the user
remembers from the Tomb Raider I-III remaster's own display - that X is what
THAT game's inventory rotation makes of it, and this engine has no access to
that table without touching a remaster binary. What the round delivers is the
pair standing upright and facing the camera the way the vanilla item does,
instead of lying flat one above the other. The pictures are in the round's
proof folder and the numbers above are the whole of the rule.

**THE FLOOR PICKUP KEEPS THE VANILLA MODEL, measured.** The question was whether
the fitted OPTION model would also do as the thing lying on the floor. It would
not: the fit centres the replacement on the vanilla item's box, and the Uzi pair
is 140 units thick after the fit where the vanilla item is 42 - so half of that
difference, 49 units, would be under the floor. The ammo model does fit (1.00x),
but a rule that takes one and refuses the other is a per-file exception, and
this project does not keep those. The ring and the corner display read the
OPTION model; the floor pickup keeps `EXTRA_`, which is what round M2b already
did.

**THE VULTURE DIED SAYING SOMETHING IN ENGLISH.** The user's report: the Tomb
Raider III vulture the randomizer imports dies with a VCI scientist voice line
when the installed remaster's bank is the HQ source, and is right when his sound
mod's bank is in the folder. Both halves are now explained by numbers.

The guests take free slots of the TR5 soundmap - the vulture 420..423, the
Compy 430..434, the raptor 438..444, the T-Rex 445..449 (`Tr3Source::dinos`).
**Free means free IN A TR5 LEVEL.** It says nothing about the remaster's bank,
and there ids 418..423 are six voice lines whose level mask names exactly one
level file. The HQ bridge pairs by sound id and judges the pairing by LENGTH,
and a length cannot tell a wrong pairing from a different recording. Measured on
seed 380's randomized ANDREA1:

```
  id   the level's own   the bank's         the length rule
  420  322 ms            624 ms (mask 11)   refused
  421  425 ms            624 ms (mask 11)   refused
  422  604 ms            624 ms (mask 11)   ACCEPTED - 1.03x the expected
  423  406 ms            1037 ms (mask 11)  refused
```

One of the four, and it is the one the player heard. The mod's bank has 390
sound ids against the install's 396 and simply has not got 418..423, so nothing
is paired there at all - which is the second half of the report, with no further
mechanism needed.

**A LENGTH CANNOT DECIDE THIS AND A NAME CAN.** The run knows exactly which ids
it wrote, so it writes them: one **`sfximport <id> <id> ...`** line in the
`.hdm`, ascending out of a `std::set` so the same seed gives the same file, and
no line at all in a run that imported nothing. The engine's bridge passes those
ids over with a line each. An engine older than this round has no rule for the
word and ignores it, which is what the sidecar format was built for. Ids the run
MOVED between TR5 levels are NOT in that list and keep their pairing - those
really are the remaster's own sounds, which is exactly why the bank's level mask
cannot be the rule.

**IT IS THE SAFETY NET ALL THE SAME, and it needed no table.** Which of the
bank's 64 level bits is the level being loaded is worked out from the level's
own sound map: the bit whose ids cover this level's and no others, every id the
level has and the bit claims counting for it and every one it does not counting
against it. Proven over all sixteen classic levels - each matches exactly ONE
bit and matches it COMPLETELY while the runner-up covers at most two thirds:

```
  ANDREA1 bit 1   ANDREA2 2   ANDREA3 3   JOBY2 4   JOBY3 5   JOBY4 6
  JOBY5 7   ANDY1 8   ANDY2 9   ANDY3 10   RICH1 11   RICH2 12   RICH3 14
```

Worked out rather than tabulated, because a table of level names is a table a
randomized level or a level pack would be silently wrong about. A pairing that
bit excludes now gets a line: on seed 380's ANDREA1 that is 32 sounds, every one
of them an enemy the run moved here, and it is the line that would have found
the vulture without anybody having to report it.

Two rules the bridge rests on moved into `sfxbank.cpp` with it - `SfxBankLevelBit`
and `SfxPairPlausible` - so that `SfxProbe` judges a pairing with the engine's
own arithmetic and not with a copy of it, which is the drift round M1 found in
`ModelProbe`'s page rule and fixed. `SfxProbe -level <path.trc>` reads a finished
level and the sidecar beside it and prints the whole decision table.

**THE THREE DIAGNOSTIC LOWS of the round M2b review, all three closed.** The
ring plan stopped silently when its table of sixteen was full - a Mods folder
with seventeen inventory models would have had the seventeenth quietly draw the
install's own, which reads in a play test as "my mod does not work" and has
nothing to point at; it says so now. The twin's own fold counters were worked
out by the reader and thrown away, so the one model on the card whose source a
reader of the line cannot see was also the one whose fold nobody could check;
they are in the part line. And the twin rule's SECOND `.TRM` read - the rest
hands, a whole file off the disk per hand part with a mod's gun in it - was not
counted into `read_ms`, so the one line that says how long a level spent reading
models was short by exactly the work that rule adds.

**PROVED.**

- *The randomizer's output does not move.* The build of `bf105b4` against this
  one, seed 380, thirteen levels, `mix cross allsanity 8 weapons outfit-tr3`
  with both Tomb Raider III installations: **every `.trc` byte-identical**, and
  the only difference in any `.hdm` is the one new `sfximport` line. Ten of the
  thirteen rolled a guest; the three that did not have byte-identical sidecars
  as well.
- *Nothing moved that was not meant to.* `ModelProbe -all -nodraw` over ANDREA1
  with NO `Mods` folder, the build before the round against the build after it:
  **287 lines identical**, every slot, every model, every page, every range,
  every verdict. And the eight known outfits drawn with pistols: **eight
  byte-identical `.PNG` files**.
- *The vulture.* `SfxProbe -level` over the randomized ANDREA1: with no `Mods`
  folder the four ids are refused and 422 is named in capitals as the one the
  length would have let through; with the user's `Mods` folder the mod's bank
  has none of them and nothing is refused because nothing was ever paired.
- *The ring.* `ModelProbe -invrot` renders of the vanilla pistols (the user's
  own reference picture), the vanilla Uzi and the mod's fitted pair, all three
  looked at.

**WHAT IS NOT PROVED.** Everything here that is not a file comparison is
`ModelProbe`'s renderer and `SfxProbe`'s reader, which are the engine's own code
with no game around them. The play test still owes: the menu on the screen and
under a gamepad, the re-dress in the middle of a level, the barefoot steps
heard, the boots heard with the sound mod in the folder, and the ring opened in
the game with the Uzi mod active.

#### Round M3, the fix round - a step is two sounds, and the sidecar is not an HD fact (#153)

The review of round M3 found two places where a claim had been tested with an
instrument that could not see the fault, and five smaller ones. Both of the two
are corrected here, and both corrections are measurements rather than guesses.

**A STEP IS TWO SOUNDS ON ONE FRAME, AND THE ROUND ONLY BENT ONE.** Round M3
wrote, in the header of `specific\hd\hd_feet.h`, that footsteps reach the mixer
through animation commands alone and that bending sound ids 0 and 4 was
therefore the whole switch. The grep it rested on could not have found the
second caller, because that caller does not use those names:
`game\footprnt.cpp` `AddFootprint` plays `footsounds[floor->fx] +
SFX_FOOTSTEPS_MUD` - sound ids 288 to 294 - for every sector whose `fx` is not
5, 6 or 11. It is `effect_routines[32]`, and Lara's own animations carry it
beside the footstep.

Measured, because the size of it decides what to do about it:

- `AnimCmd` over the `LARA` slot of the vanilla ANDREA1: **148 frames carry
  `PlaySound sfx 0`, 132 carry `FlipEffect no 32`, and 121 carry both.** Three
  more call sites need no command at all - `game\lara.cpp` calls `AddFootprint`
  straight out of `lara_as_walk`, `lara_as_null` and `lara_as_gymnast`.
- `Tr5Sound` over all sixteen classic levels: **13 of them carry footsteps at
  all** (TITLE and RICHCUT2 hold four samples between them, `del.trc` has no
  sound blocks whatever), and every one of those 13 carries at least two of the
  seven floor ids - ids 292 and 294 are in all thirteen. The foot layer is 52
  takes of **35 to 43 ms**; the floor layer is 111 takes of **87 to 433 ms,
  mean 186**. *The floor layer is 4.7 times the foot layer: it is the loud half
  of a step.*

So with **Bare** chosen, the player would have heard the bank's 22 kHz barefoot
recording under her foot and, on the very same frame, the level's own BOOT on
mud, on metal or on wood over it. That is the bug the F4 changer would have
shipped. The floor layer is now passed over while a bare set is in force -
`HDFeetSurfaceQuiet()`, asked at the one call site - and the answer is false
with `hd_enable` off, false with boots on and false in a level that has no
second set, so the line is the one it always was in every other case. The
footPRINT is a picture and is untouched. Two log lines: one per level where the
set is built, naming the ids, and one the first time in each bare spell, which
is the seam a play test reads.

*Sound id 17 (`SFX_LARA_WET_FEET`) stands beside id 0 on the same frames for
the water case and is NOT switched.* The bank has one barefoot set and it is a
dry one; there is nothing to put behind 17, and inventing one would be art.

**THE VULTURE'S REFUSAL WAS INERT IN THE BUILD THE ENGINE SHIPS.** The bridge
asks `HDMarkersSoundImported()`, and the sidecar that answers it was read in
exactly one place: inside `HDLevelLoad`, below `if (!HDEnabled()) return;`. The
bank chain it protects is deliberately NOT gated - round M1's own words, "sound
does not care whether hd_enable is on". So with `hd_enable` off, which is how
this engine ships, the list was empty, id 422 paired with RICH1's voice line
again, and the summary line said "the run declared 0 imported sound ids" with
nobody the wiser. No tool could show it: a tool calls `HDMarkersLoad`
unconditionally by construction, which is why `SfxProbe` proved a fix that was
not in force.

The sidecar is now read ABOVE the gate. It is a level fact, not an HD fact; the
rooms, the cuts and the donors below the gate are still read by HD code alone,
and the other half of the bracket - `HDMarkersFree` in `HDLevelUnload` - was
never gated either. The order is unchanged and is all on the load thread:
`FreeLevel` -> `HDMarkersFree`, `HDLevelLoad` -> `HDMarkersLoad`, then
`LoadSamples` -> `HQSfxBuildMap` -> the refusal.

**AND FIVE MORE.**

*A dead `game_malloc` pointer, the joby3 class.* `hd_feet.cpp`'s header
promised its tables were thrown away with the level and the only caller of
`HDFeetLevelEnd` was its own `LevelBuild`. `LoadSamples` returns before reaching
that in seven places and is skipped entirely when the sound is off - and
`del.trc`, which is in `SCRIPT.DAT`'s level list, has no sound blocks at all.
The next level would then have found a bare set that answered "available"
through the previous level's freed `sample_lut`. `HDFeetLevelEnd()` now stands
in `FreeLevel`, two lines above `malloc_ptr = malloc_buffer`, which is the line
that makes every such pointer stale and already carries the `baddie_slots` note
about it.

*A counter that saturated at its own printing cap.* The mask safety net counted
`(*outside)++` inside the `*outside < 32` that limits the lines, so the summary
could never report more than 32 however many there were. Counted first, printed
second, which is the shape the refusal counter beside it always had.

*F4 during a cutscene.* The guard had `!gfGameMode`, Lara alive and
`!BinocularRange` but not `!cutseq_trig` - which the inventory one screen up
has carried all along. A player could freeze a running cutscene and re-dress
over it with the scene's actors bound.

*"Bare" silently becoming "boots" behind any sound mod.* The bare set's source
was decided by `HQSfxFromMod()` alone - "a mod's bank is being played" - and
most sound mods are not about footsteps. Measured over the user's own two banks
by sample hash: of the 390 sound ids his Underworld mod's bank shares with the
install's, **387 hold byte-identical recordings and three do not** - ids 0, 4
and 417. So a door mod or a voice mod carries the install's own id 0 byte for
byte, and the round handed the BOOTS' four recordings back under the name
"bare" while the log announced a barefoot set. The two payloads for sound id 0
are now compared - the banks each own their read buffer, so both can be held at
once - and the mod supplies the bare set only when it really differs. One
`SFXLOG` line either way.

*The menu's two loose ends.* Left and right used to toggle the footsteps on the
**Level facts** row, where there is no key to write the choice to: it was heard
at once, nothing was written, and the next level put it back. That row now says
*as the outfit says* and does not answer left and right. And when the re-dress
was deferred because the 64 MiB allowance was spent, the new outfit's footsteps
were applied anyway - the old body walking with the new one's feet. Both halves
wait together now; both are still written to the registry, so the next level
wears both.

**AND THE LOWS.** The two spare `SAMPLE_INFO`s are asked of `game_malloc` only
with `hd_enable` on, so a player with it off gets the allocation layout he had.
Three code comments carried the round's first-pass arithmetic for the vulture -
id 423 at 1.18 times the expected length - where the level's own bytes say id
**422**, 604 ms against the bank's 624; the round's report and record had the
right table and only the comments were wrong. The ring's overflow note quoted
3.18 where the measurement is **3.36**. One block-comment continuation and two
indentation runs in `SfxProbe` are lined up.

**THE RING, AND THE PICTURE THAT WAS NOT THERE.** Round M3 said the user's Uzi
mod cannot be turned into the crossed X he asked for, and defended it with a
face-on render of the model's own plane - a file in the proof folder that is
100 % magenta and shows nothing. Two things were wrong with the tool, both
fixed:

- a `-name` model is loaded over 32 nodes with no mesh tree behind it, and
  `UZI_OPTION`'s 27449 vertices sit on **nodes 1 and 2** (13723 and 13726 - one
  gun each), not on node 0, so nothing placed them and nothing drew. `-rigid`
  loads it the way the ring loads it: one node, every vertex rigid on it.
- `-invrot` turned the model about the ORIGIN while the camera framed the
  untouched box. This model's middle stands 73 units off the origin, so it
  swung out of the picture. The camera frames the turned box now.

With those two, the three orthogonal views of the model in its own principal
frame exist and were looked at: **the two Uzis stand side by side and parallel,
offset from each other - not a crossed X from any direction.** And the control
case settles that the switch is not hiding one: the same three words on
`EXTRA_PISTOLS_ITEM` draw the two vanilla pistols **crossed like an X,
face-on** - which is the very picture the user wants - so on a file that holds
a cross it shows a cross. A rigid rotation
keeps the angle between two parts, so no rotation of this file can cross them,
and the fit does not pretend to. Where the I-III remaster's own X comes from is
NOT established here: the `.TRM` carries no node transforms at all (per-vertex
bone indices and weights, nothing else), and placing those two nodes would mean
reading the donor game's own mesh tree and frame out of the user's I-III data,
which is a feature and not a fit - a decision for the project. The vanilla
`EXTRA_UZI_ITEM` beside it is **one** gun of 1371 vertices, which the round's
own comments still called a crossed pair; they do not now.

**THE HEADROOM, which the record owed.** The brief asked for the page cost per
outfit AND the x86 headroom before choosing between releasing pages and capping
them. The pages were measured in full and the headroom was not written down.
It is now: `release\PCtomb5.exe` is x86 with `IMAGE_FILE_LARGE_ADDRESS_AWARE`
set (PE characteristics 0x0122), so the ceiling is 4 GiB, and the heaviest
offline load of ANDREA1 - 179 room pages and 53 model pages, 350.5 MiB of
texture on the card, the user's `Mods` folder, every slot - peaks at **209.4
MiB of private commit**. The 64 MiB allowance is 18 % of that level's own page
bytes. So the cap is not holding back a ceiling that was about to be hit; it is
holding back unbounded growth, which is the real risk, because the page cache
never gives a page back.

**PROVED.**

- *The randomizer's output still does not move.* The fix round's rebuilt
  `Randomize.exe`, seed 380, five levels, `mix cross allsanity 8 weapons
  outfit-tr3`: every `.trc` byte-identical to the PRE-round build, the only
  `.hdm` difference the one `sfximport` line, and every file byte-identical to
  the round's own outputs. (The only change this round made under
  `TR5Randomizer/` is a comment.)
- *Nothing moved.* `ModelProbe -all -nodraw` over ANDREA1 with no `Mods`
  folder against the stored pre-round run: identical but for the path that was
  typed. The eight known outfits drawn with pistols: eight byte-identical
  `.PNG` files.
- *The floor layer.* `AnimCmd` and `Tr5Sound`, the two tables above.
- *The bare set's source.* The two banks compared sample by sample by hash.
- *The vulture.* `SfxProbe -level` over the randomized ANDREA1 again, with and
  without the `Mods` folder - unchanged, which is the point: the tool never
  could see the gate.
- *The ring.* Five renders looked at: the mod's model from all three sides of
  its own frame, the vanilla item in its own frame, and the fitted pair as the
  ring shows it.

**WHAT IS STILL NOT PROVED.** The engine's own log with `hd_enable = 0` - no
tool can stand in for it, which is exactly why the fault was there. The play
test still owes everything the game has to run for: the menu on the screen and
under a gamepad, the re-dress in the middle of a level, the barefoot steps
heard with the floor layer silent, the boots heard with the sound mod in the
folder, the ring opened with the Uzi mod active, and the vulture's death with
and without that mod.

#### Round M3, the review after the fix round - both ways of shipping a footstep (#153)

The second review of round M3 found one medium and five lows; the fix round's
two high findings held. All six are answered here, and the medium is a real
hole in the round's own rule rather than a wording fault.

**A FOOTSTEP MOD CAN SHIP NO BANK AT ALL, AND THIS ONE FELL THROUGH BOTH SETS.**
The round's rule is that sound ids 0 and 4 come from the first UNMODDED bank of
the chain while the outfit wears boots, and that a mod which really replaces
them supplies the BARE set instead. The bare half asked `HQSfxFromMod()`, which
answers "is a mod's BANK being played". A mod may also replace a sample with a
loose `<NNNN>.wav` beside it - round M1's own overlay, `a mod's <NNNN>.wav
replaces bank sample NNNN` - and for the footsteps those files are exactly
`0000.wav` to `0003.wav` and `0010.wav`. Such a mod ships no bank, so
`HQSfxFromMod()` said no, the bare set fell back to the remaster's own id 417,
and the boots had already stepped round the overlay by design: **the mod's five
recordings reached neither set, with no log line and nothing in the docs.**

The fix is to ask the question that was always meant. `HQSfxPayload` for sound
id 0 - the ORDINARY chain, bank and then the loose overlay over it - is compared
with `HQSfxPayloadBoots` for the same sample, which is the unmodded bank. They
differ exactly when a mod really replaces the footsteps, whichever way it ships
them, and they are byte-identical for the door mods and voice mods that are most
of what exists (387 of the 390 sound ids the user's two banks share are
byte-identical, measured in the fix round). One rule, both kinds of mod, and no
list of cases to keep in step. `HQSfxWavSource` names the mod for the log
without reading any audio, and the bypass line the brief asked for by hand is
written once per level:

```
SFXLOG footsteps: <mod> supplies the bare set - its own loose .wav stands on sound id 0
SFXLOG footsteps: <mod>'s own sound ids 0 and 4 are bypassed while the outfit wears boots -
                  the boots are <bank>'s, and the mod supplies the bare set instead
```

Measured, with five files made in a scratch folder out of the bank's own
exported recordings: `SfxProbe` reports `bank=no wavs=5`, all five ACCEPTED by
the engine's own `SfxRiffCheck`, claiming bank samples 0000-0003 and 0010 - and
the bridge on that game folder puts sound id 0 on bank sample 0 (four takes) and
sound id 4 on bank sample 10. So the five files a footstep mod ships are exactly
the five the two ids stand on. No tool can walk the rest: `SfxProbe` links
`sfxbank.cpp` and `hd_mods.cpp`, not `hqsfx.cpp` and not `hd_feet.cpp`, which
need DirectSound and the engine's `Log`. The two lines above are on the play
test's list.

**THE FOOTSTEP CHOICE NOW SURVIVES A FAILED HD BUILD.** `HDLevelBuild` has five
ways to give up - no stem, a bad room count, `HDRenderInit` refusing the card,
no page cache, no room drawing HD - and the call that applies the player's
persisted footstep choice sat below all five. The bare set does not: it is built
by `HDFeetLevelBuild` whenever `hd_enable` is on, in `LoadSamples`, on the load
thread. So a player whose card cannot do shader model 3 - the session goes
classic while `hd_enable` stays 1 - had an F4 row that worked inside the level
and was silently dropped at the next level start. The build's body is now
`HDLevelBuildHD()` and `HDLevelBuild` is four lines around it, so the question is
asked on every way out and none of them can skip it.

On those ways out the answer is the LEVEL's own body and not the player's key,
and that is not a second oversight: `HDLaraDrawBuild` runs inside
`HDObjectLevelBuild`, which is past all five returns, so there is no HD body to
follow - `HDLaraDrawOutfit()` reads `HD_LARA_OUTFIT_NONE`, the key is empty, she
is wearing Core's outfit, and Core's outfit has boots. The fix is that the
answer is now given and written into the log instead of being left to whatever
the menu happened to set inside the level.

**AND FOUR SMALLER ONES.**

1. **`ModelProbe` still named the overflow axis `x`/`y`/`z`.** Round M3 changed
   `HDModelRingFitOverflow`'s contract to hand back WHICH OF THE TARGET'S OWN
   principal axes is overflowed and updated the engine's wording to "its longest
   / middle / flattest axis"; the tool's table was left behind. It printed
   `3.36x ... (z)` where the engine said "its flattest axis", and a reader
   chasing the number would have looked at a model extent that cannot produce
   it. The tool now prints the engine's three words.
2. **The `*` was drawn on wardrobe rows only.** With no registry value - every
   player who has never opened the menu - the row in force is **Level facts**,
   and nothing on the screen carried the mark. It does now.
3. **Three stale symbol names in the round M2b entry**, `HDModelBoxFit` and
   `HDModelBoxFitOverflow`, renamed this round to `HDModelRingFit` and
   `HDModelRingFitOverflow`. Corrected in place, which is this record's
   convention; and the same sweep found `hd_model.h` still calling the mod's
   parallel Uzis a crossed pair and the vanilla `EXTRA_UZI_ITEM` a pair at all,
   which the fix round had corrected in `hd_model.cpp` and not in its header.
4. **The ring's IN-PLANE angle was a known weakness that lived only in a
   report.** It is in the source now, with the numbers: the PLANE is decided by
   a wide margin (132.3 against 239.5 on the mod's file, 41.6 against 235.7 on
   the vanilla one - a factor of nearly six), but the two axes INSIDE it are
   242.3 against 239.5, a gap of 1.2 %, and `sort_axes` orders them by that gap
   alone. The roll inside the plane is therefore deterministic and not
   meaningful: a mod update that moved a few dozen vertices could turn the item
   a quarter turn. The brief asked for a symmetry test and there is none; a
   max-extent sweep in the plane is the next step if the user dislikes an angle.
   Written down so that a quarter turn after a mod update is not read as a new
   bug.

**PROVED.**

- *The loose wav.* `SfxProbe -wavs` over a scratch mod of five files: `bank=no
  wavs=5`, all five accepted, claiming bank samples 0000-0003 and 0010; and the
  bridge putting ids 0 and 4 on exactly those. Plus the call chain with
  `file:line` for what no tool can reach.
- *The ring did not move.* `-ringcheck` worst dx 0.00013 px, `-pickupcheck`
  0.00008 px, both PASS, overflow 3.36x - the same readings as before - and the
  three ring renders byte-identical to the fix round's. Only the axis wording
  changed.
- *Nothing moved.* `ModelProbe -all -nodraw` over ANDREA1 with no `Mods`
  folder against the stored pre-round run: identical but for the path that was
  typed; the eight known outfits drawn with pistols, eight byte-identical `.PNG`
  files.
- *The vulture.* `SfxProbe -level` over the randomized ANDREA1 again: output
  identical, line for line, to the fix round's run - 420, 421 and 423 refused as
  imports, 422 refused where the length would have let it through.
- *Round M1's own guarantee.* `SfxProbe -samples`: 473 of 473 pass, 0 refused.
- *Nothing under `TR5Randomizer/`* was touched at all this round, so the
  regression that round M3 and its fix round both ran still stands.

#### Round 155 (#155) - the cross is in the donor game's first frame, not in the file

The fifth round of the Mods overlay (#153, rounds M1, M2, M2b and M3), and a
task of its own. The user's report, with pictures: in our ring his IMI Uzis
mod stands as two guns SIDE BY SIDE and parallel, and in the Tomb Raider I-III
remaster's own inventory the very same `UZI_OPTION.TRM` shows them CROSSED like
an X, facing the camera, seen slightly from below. Round M3 measured the
parallel pair from all three sides, said no rotation of a rigid file can cross
two parts, and left the question where it belongs: the cross is not IN the file.

**IT IS IN THE BIND POSE, and the bind pose is only half a pose.** A remaster
`.TRM` stores each node's vertices with the classic mesh tree's offsets applied
and NO rotation at all; the game it came from puts the rotation on when it
draws, out of the classic object's own frame. So the two guns of `UZI_OPTION`
really do lie parallel in the file - round M3's measurement stands - and the
game that ships it shows them crossed all the same. What was missing was the
frame, and the frame is in the user's own level files.

**THE READER, `specific\hd\hd_classic.cpp`.** Engine-free like `hd_wardrobe`
and `hd_install` beside it - no engine global, no `Log`, plain C on the outside
- it walks `<I-III bundle>\<game>\DATA` in name order until a level holds the
object with at least one mesh, and hands back the mesh count, the bones, frame
0 and the level file it used. The three level layouts are the published
TR1/TR2/TR3 formats; every count read out of a file is checked against the
bytes that are left, and the walk streams past the texture pages rather than
loading them. One parse per (game, id) per session, refusals cached too.
Nothing of any remaster binary is in it: the `.TRM` says which node a vertex
rides on and the classic level files say where the mesh tree puts that node -
both are the user's own data files, read where they lie.

**THE WORD ORDER OF A TR1 FRAME, SETTLED BY MEASUREMENT.** Readers disagree
about which of a TR1 frame's two rotation words is the high half, and the
question is not one to settle by picking a reader to believe. A frame carries
the bounding box of the pose it describes, so the walk has the file's own
answer to check itself against. Over the eight weapon and ammunition objects of
`1\DATA\GYM.PHD`:

```
  object   99  100  101  102  103  104  105  106     worst corner, units
  w2 high  1.0  1.0  1.0  2.1  0.8  0.6  0.7  0.5    <- the rounding of a
  w1 high 50.3 58.5 57.5 106.7 58.1 63.9 101.6 79.4     1024 step angle table
```

**The second word is the high half.** The cross-check is independent and
complete: read that way, Tomb Raider 1's object 102 gives the same three
rotations, the same two bones and the same frame bounds as Tomb Raider 2's
object 160 and Tomb Raider 3's object 188, whose packing is not in doubt.

**THE ROTATION CONVENTION, ALSO MEASURED.** `phd_RotYXZ` is y, then x, then z,
the classic matrix takes a point as a column and the frame's offset is part of
the pose. That is four choices, and all four were put to the same test rather
than asserted: over eleven weapon objects of all three games, every one of the
ninety-six combinations of composition order, multiplication side, angle sign
and vector convention was run against the frames' own bounding boxes. The
winner reproduces them to **2.1 units at worst**; the next family misses by
40.8, and the worst by 184.7. Including the frame offset is what separates 2.1
from 40.8.

**WHAT THE CLASSIC OBJECT SAYS,** the same out of all three games
(`ModelProbe <IV-VI game 5 folder> -tr3 <I-III game 3 folder> -classicpose
1|2|3 UZI_OPTION`):

```
  UZI_OPTION      game 1 object 102 in 1\DATA\GYM.PHD     3 meshes
                  game 2 object 160 in 2\DATA\BOAT.TR2    3 meshes
                  game 3 object 188 in 3\DATA\ANTARC.TR2  3 meshes
    bone 1  flags 2 (push)  offset  54  -2  61
    bone 2  flags 1 (pop)   offset  36  22 -69
    frame 0 offset -2 0 0, bounds x -83..87 y -24..30 z -82..140
    mesh 0  rot 1001 333 1006 of 1024     mesh 1  239 563 712
    mesh 2   814 901  270
  UZI_AMMO_OPTION game 1 object 106, game 2 167, game 3 196   2 meshes
    bone 1  flags 0  offset -5 -7 -47
    frame 0 offset -15 0 -14, bounds x -73..53 y -29..21 z -90..90
    mesh 0  rot 13 672 20      mesh 1  0 119 14
```

**MESH 0 IS A THREE-VERTEX STUB.** TR3's `UZI_OPTION` has three meshes where
the mod has two nodes of geometry, and the third is not a third gun: mesh 0 is
3 vertices with a collision radius of 6, a root the two guns hang off, and the
remaster's file simply has no vertices on node 0 (13723 on node 1 and 13726 on
node 2). Meshes 1 and 2 are 56 vertices each, centres -90/35/10 and -94/37/8,
radius 109 - one gun apiece.

**AND THE MOD'S FILE REALLY IS IN THAT BIND POSE.** The claim was checked on
the user's own file before a line of it was believed: the two node clouds of
`UZI_OPTION.TRM` stand **-22.1 / +28.3 / -134.8** apart, where the difference
of TR1's two bone offsets is **-18 / +24 / -130** and the remaining four units
are the two meshes' own centroids. The ammunition model is the same story on
nodes 0 and 1.

**THE POSE IS APPLIED WHERE THE MIRROR FOLD IS APPLIED** - in `hd_model.cpp`,
right after the `.TRM` is read and before `build()` takes the box and the
principal axes. It has to be: `HDModelRingFit` fits the SHAPE those axes
describe, so a pose applied afterwards would be a pose the fit never saw. It is
real skinning and not a rigid turn - three nodes, three weights, normals turned
and re-normalised - although every file this has met so far is rigid.

It needs three things and refuses with a line for each: the model came from a
**mod with an `origin=` game**, its name is in the table, and the classic
object's mesh count IS the `.TRM`'s node count. That last one is the proof that
the two belong together, because a `.TRM`'s bone numbers ARE classic mesh
numbers.

**THE TABLE, AND WHERE ITS NUMBERS COME FROM.** Thirteen rows: the four weapons
and the ammunition boxes, under the donor games' own names (`GUN_OPTION`,
`MAGNUM_OPTION`, `SG_AMMO_OPTION` ...) and under this engine's, where a mod
names its file after the TR5 slot it replaces (`PISTOLS_OPTION`,
`REVOLVER_OPTION`, `SHOTGUN_AMMO1_OPTION` ...). TR3's ids are counted out of
the user's copy of the GPL **tomb3** sources, `game/objects.h`; TR1's and TR2's
out of the GPL project **TRX** (LostArtefacts), `catalog_objects.csv`, whose
TR3 column agrees with tomb3 on every id in the table. Each one was then found
in the user's own installation. A name that is in neither column gets no pose
and says so.

**"THE FIRST LEVEL IN NAME ORDER" IS A RULE AND WAS CHECKED AS ONE.** Over the
eight option objects of game 1 and of game 2, every level file that holds the
object holds exactly the same mesh count, bones and frame 0 - 16 and 17 levels.
Game 3 agrees on 17 of its 20; the three that differ are `TITLE.TR2` and
`AREA51.TR2` carrying a stub or a left-over of the game before, and name order
lands on `ANTARC.TR2`, which agrees with every other real level of that game.
A table of level names would have been silently wrong about a level pack.

**WHAT IT DOES TO THE PICTURE.** The angle between the two guns' own long axes
goes from **0.0 degrees to 65.5**, and the pair from 132 units thick to 60:

```
                          box              cloud            two parts
  UZI_OPTION      before  147x253x159   242 x 240 x 132     0.0 deg apart
                  after   198x 55x231   222 x 217 x  60    65.5 deg apart
  UZI_AMMO_OPTION before  162x 28x 74   165 x 107 x  22     0.2 deg apart
                  after   121x 53x174   174 x 121 x  32    42.1 deg apart
  (the vanilla EXTRA_UZI_ITEM, for scale: box 266x41x212, cloud 256x236x42)
```

The flat direction of the posed pair is now the vanilla item's own, so the ring
fit of round M3 needs no change at all and the **overflow falls from 3.36x to
1.65x**. The pictures were looked at: the mod's two Uzis come out CROSSED,
face-on to the camera, beside the vanilla pistols' own crossed pair that the
user named as his reference.

**THE AMMUNITION GOT BULKIER AND THAT IS THE RIGHT ANSWER.** Its overflow goes
the other way, 1.00x to 1.31x, because the classic frame CROSSES the two
magazines where the file ships them flat and parallel. That is what Tomb Raider
1 shows and what the round is for; a rule that posed the guns and left the
ammunition flat because the number was prettier would be a per-file exception.

**THE FLOOR PICKUP KEEPS THE VANILLA MODEL, measured again.** Round M3 refused
the fitted `_OPTION` model for the thing lying on the ground because half the
difference in thickness - 49 units - would be under the floor. The question was
asked again with the pose on, and with a better measurement: the eight corners
of the source's box really through the fit, the deepest of them against the
target's own deepest point (`HDModelRingFitUnderFloor`, one implementation,
engine and tool). The pose HELPS and does not settle it:

```
                  under the item's lowest point, which is the floor
  UZI_OPTION       80.6 units unposed   ->   32.2 posed
  UZI_AMMO_OPTION   9.6 units unposed   ->   29.5 posed
```

Both still sink, so the level pickup keeps `EXTRA_` for every slot - one rule,
the ammunition included - and the number is now in the log per slot, so the
refusal is a measurement anybody can re-read rather than this round's word.
(Round M3's "49" was half a difference of cloud extents and is not the same
quantity as the 80.6 above; the corner measurement assumes neither a centred
fit nor a symmetric model, and the posed pair is neither.)

**WHAT IT COSTS, because a level build opens files now.** The walk seeks past
the texture pages rather than loading them and stops at the first level file
that holds the object, and the answer is kept for the session - a ring that
asks in every level of a play session reads a level file once. Measured over
three runs each of `ModelProbe -classicpose`, whole process, against the same
command on a name the table does not know (which reads nothing at all): 85 ms
against 90, 93 and 87 for games 1, 2 and 3. Single-digit milliseconds, once,
and game 1 is the slow case - `CUT1..CUT4.PHD` sort before `GYM.PHD` and have
no weapons in them.

**THE TOOL.** `ModelProbe <IV-VI game 5 folder> -tr3 <I-III game 3 folder>
-classicpose <game> <MODEL_NAME>` prints the object, the level it came from,
the bones, frame 0 and the 4x4 each node gets, through the very reader the
engine uses. Both folders are wanted: the positional one is where every run of
this tool starts, and the bundle the reader walks is the parent of the other. `-noclassicpose` runs `-inventory` without the
pose, which is what keeps this round's gain a measurement instead of a memory,
and `-inventory` now prints the pose's own verdict and the floor figure.

**THE LOG.** Four new lines behind `-log`, none of them thinning anything:
`HDLOG classic <game>/<level> object <id> meshes=.. bones=.. frame0 offset=..
bounds=.. rot=..`, its refusal `HDLOG classic <game> object <id>: <why>`,
`HDLOG level=.. ring pose slot=.. model=.. classic=<game>/<level>#<id>
nodes=.. applied (box .. -> ..)` or `... not posed: <why>`, and `HDLOG level=..
ring floor slot=.. name=.. keeps the level's own <model>: the fitted <model>
reaches N units under the item's lowest point`.

**PROVED.**

- *The cross.* `ModelProbe -slot 336 -inventory -invrot 0x4000 0x6000 0x4000`
  over ANDREA3 with the user's `Mods` folder, looked at: two Uzis crossed,
  facing the camera, where round M3's picture of the same command has them side
  by side and parallel. The ammunition the same, two magazines crossed. The
  vanilla pistols rendered beside them as the control.
- *The reader.* The same three angles, bones and bounds for `UZI_OPTION` out of
  Tomb Raider 1, 2 and 3 - three different level layouts, one of them read with
  a word order this round had to measure. And the walk against the frames' own
  bounding boxes: 2.1 units at worst over eleven objects.
- *Nothing moved.* `ModelProbe -all -nodraw` over ANDREA1 with no `Mods`
  folder, the build before the round against the build after it: **287 lines
  identical**. The eight known outfits drawn with pistols: **eight
  byte-identical `.PNG` files**. And the ring itself with `-noclassicpose`
  against the pre-round build: **byte-identical renders and identical fit
  lines** for both of the mod's models - so what changed on that path is the
  pose and nothing else.
- *The budgets.* `-ringcheck` and `-pickupcheck` both PASS on the posed model.
- *Nothing under `TR5Randomizer/`* was touched.

**WHAT IS NOT PROVED.** The engine's own log, and the ring opened in the game
with the mod active - no tool stands in for either, which is exactly the gap
round M3's fix round was about. And the roll INSIDE the plane is still decided
by a 2.3 % gap between the posed pair's two in-plane extents (222.2 against
217.1), so the cross faces the camera but the angle it stands at in that plane
is not chosen by anything that knows what a gun is; round M3 wrote that down
and it is still true.

#### Round 155b (#155b) - the pickup lies ON the floor, and the number that says so

Round 155 refused the mod's fitted Uzis for the thing on the ground and printed
its reason: the fitted pair reached under the item's own lowest point, and a
gun half sunk in the floor is not a picture. The user's answer was "das mit dem
Bodenversatz kannst du machen, ist ne gute Idee" - measure the sinking and lift
it out, rather than hide the mod he installed. This round is that, as ONE rule
for every pickup model, and the MP5 mod he added the same day, which lies
invisible on the floor for a reason that is not this one.

**THE REFERENCE IS THE CLASSIC MESH, AND THAT WAS MEASURED BEFORE IT WAS
BELIEVED.** The obvious reading - "a pickup's origin is on the floor, so the
floor is y = 0" - is wrong: an item is drawn at its animation FRAME's own
offset, and the offsets are large. ANDREA3's classic `PISTOLS_ITEM` mesh has
its deepest point **59 units below** its node and its `CROWBAR_ITEM` **one unit
above**. So the ground is wherever the level's own mesh ends, the rule is
stated against that mesh, and the frame offset cancels because it reaches the
HD model through the same matrix. Stated that way it needs no case for a
pedestal or for a wall place (OCB 2/9): those move the item and take both
meshes with them.

**THE RULE.** At level build, for every slot of the pickup family that came out
HD, the lowest point of the HD model's drawn VERTICES against the lowest point
of the classic mesh it replaces. Deeper by more than half a unit and the model
is lifted by exactly that difference, once, stored per slot, applied in the
object path; never scaled, never turned, never moved sideways. The family is
`HDSlotIsPickup` - a slot whose name ends in `_ITEM`, which is the test
`HDBindModOption` already made for "an inventory object" and is now written
down once. It is a rule about the SLOT and not about which binder rule found
the model, which matters: the generic `EXTRA` prefix finds most of these, but
`REVOLVER_ITEM`'s Desert Eagle, `REVOLVER_AMMO_ITEM` and the two VCI pickups
are named by the hand table and lie on the floor exactly like the rest.

**AND THE FLOOR TAKES THE FITTED `_OPTION` MODEL NOW.** A slot with a mod's
`<BASE>_OPTION` model and no `EXTRA_` name from any mod draws that model on the
ground as well, through the ring's own fit, plus the lift measured on the
FITTED vertices. The ring and the corner display are not touched by a line of
it: the lift is added where the item is DRAWN, in the object path, and the two
paths that have no floor go on multiplying through `fix` exactly as they did.

**THE NUMBERS, ANDREA3 WITH THE USER'S `Mods` FOLDER AND WITHOUT**
(`ModelProbe -floorcheck`; y is DOWN, so a positive sink stands IN the ground.
Every row is measured in the frame the LEVEL draws the item in; the bind-pose
figure is beside it where the two differ, and the whole of fix round 1 is that
difference):

```
  slot                model                        classic  lowest   sink  lift
  UZI_ITEM            UZI_OPTION fitted (mod)         19.5    38.7   19.2  19.2
                        in the bind pose               19.0    30.5   11.5
                        by round 155's box corners             52.9   33.9
  UZI_ITEM            EXTRA_UZI_ITEM (install)        19.5    21.1    1.6   1.6
  UZI_AMMO_ITEM       UZI_AMMO_OPTION fitted (mod)    26.0    32.8    6.8   6.8
                        by round 155's box corners             57.0   31.0
  UZI_AMMO_ITEM       EXTRA_UZI_AMMO_ITEM (install)   26.0    27.5    1.5   1.5
  PISTOLS_ITEM        EXTRA_PISTOLS_ITEM (HK USP)     59.0    53.4   -5.6   0.0
  PISTOLS_ITEM        the install's own               59.0    52.3   -6.7   0.0
  PISTOLS_AMMO_ITEM   EXTRA_PISTOLS_AMMO (HK USP)     33.1    35.9    2.9   2.9
                        in the bind pose                0.0     6.1    6.1
  PISTOLS_AMMO_ITEM   the install's own               33.1    29.8   -3.3   0.0
                        in the bind pose                0.0     3.2    3.2
  SHOTGUN_ITEM        EXTRA_SHOTGUN_ITEM (Spas-12)     8.0     5.5   -2.5   0.0
  SHOTGUN_ITEM        the install's own                8.0     8.4    0.4   0.0
  HK_ITEM   (RICH1)   EXTRA5_HK_ITEM (MP5)            15.2    11.1   -4.1   0.0
                        in the bind pose               23.0    25.0    2.0
  HK_ITEM   (RICH1)   the install's own               15.2    11.1   -4.1   0.0
                        in the bind pose               23.0    21.9   -1.1
  CROWBAR_ITEM        EXTRA_CROWBAR_ITEM              -1.0     5.2    6.2   6.2
  LASERSIGHT_ITEM     EXTRA_LASERSIGHT_ITEM           30.0    34.2    4.2   4.2
  FLARE_ITEM          EXTRA_FLARE_ITEM                12.0    13.0    1.0   1.0
  COMPASS_ITEM        EXTRA4_COMPASS_ITEM            143.1   150.0    6.9   6.9
                        in the bind pose               30.0   174.8  144.8
  BIGMEDI / SMALLMEDI / SHOTGUN_AMMO1 / SHOTGUN_AMMO2 / BINOCULARS /
  HK_AMMO / REVOLVER / REVOLVER_AMMO / FLARE_INV: not lifted
```

The two HK rows reading the same 11.1 is not a copy: the two files really do
reach the same depth in that frame to within 0.03 units (11.0703 for the
install's, 11.0982 for the MP5), which an independent reader of the `.TRC` and
the two `.TRM` files confirms.

**THE INSTALL'S OWN FILES ARE NOT ALL AT ZERO, AND THAT IS A FINDING.** Six of
the eighteen pickup models ANDREA3 loads reach below the classic mesh they
replace, the remaster's own among them: the crowbar by 6.2 units, the compass
by 6.9, the laser sight by 4.2, the Uzi and its ammunition by 1.6 and 1.5, the
flare by 1.0. At 1024 units to a metre that is one to seven millimetres of gun
in the ground - under what a player will see, and now zero. The rule is applied
to them like everything else; a per-source exception would be the kind of
hand-kept list this project does not keep, and a tolerance chosen to make the
install's files come out inert would be the same thing spelled as a number.
Half a unit is the tolerance, and it is there for float rounding on a mesh
whose own coordinates are SHORTs, not for a policy.

> **This paragraph was true of ANDREA3 and wrong about the campaign, and fix
> round 2 corrects it.** The round measured two levels. Over all fifteen the
> largest lift is not seven millimetres but **126.6 units - RICH3's
> `CROSSBOW_AMMO1_ITEM`**, the install's own TR4 quiver on a TR5 slot, on three
> real items. The complete table, the picture and what it means are in FIX
> ROUND 2 at the end of this entry.

**`COMPASS_ITEM` AND THE 144.8 THAT WAS NOT THERE.** In the bind pose the
remaster's inventory compass reads 144.8 units below the tiny classic one, and
the first build of this round computed a lift that large. The frame ANDREA3
draws that slot in turns the model's y onto the world's z, and the real figure
is **6.9**. It is still a number nobody sees: there is no `COMPASS_ITEM` item
in any of the fifteen shipped levels (`ItemCheck <level>.trc 356`, all fifteen:
0 items). `FLARE_INV_ITEM` DOES stand in levels - ANDREA1 has three - and reads
0.2, inside the slack. The save and load crystals (`DC_LOAD_INV_ITEM` and its
five siblings) end in `_ITEM` as well and would be measured like any other
pickup if a level ever drew one HD; none does, and a rule about "objects that
are only ever in the inventory" has no engine fact to hang on today.

**WHAT ROUND 155'S CORNERS COST.** That round pushed the eight corners of the
source's BOX through the fit; this one pushes every kept vertex. Measured in
round 155's own frame, the bind pose, so that the figures are comparable to its
line for line: the posed pair reads **52.9 by the corners and 30.5 by the
vertices** against a classic mesh at 19.0, a sink of 33.9 against 11.5, and the
ammunition **57.0 against 32.8**, 31.0 against 6.8. The corner answers are
exactly the ones round 155 printed, which is the two measurements checking each
other on the same fit. A box corner of a pair of crossed guns is empty space,
and a rule that lifted by the corner number would have hung the model in the
air.

**WHAT THE SWEEP COSTS.** ANDREA3's eighteen pickup models with the user's
`Mods` folder are **57,378 vertices through the frame in 360 to 397
microseconds** over five runs, once per slot at a level build; without the
folder, 18,526 in 115 to 121 over three. That is 6.3 to 6.9 nanoseconds a
vertex on this machine - an Intel Core i9-9900K at 3.6 GHz - and the per-vertex
figure is the one to compare a different machine against. The largest single
model is the posed pair of Uzis at 27,449. The positions are kept beside the
model only while the plan runs (`HDModelSlot::keepPoints`, set on every model
of the pickup family) and `HDModelReleasePoints` gives them back before the
first frame. Nothing of this is in a frame: the draw adds one translation to a
matrix it already has, and the direction it adds is worked out once per CAMERA
and not per item.

> Fix round 2: this paragraph read "185 microseconds ... 18,526 in 51" and
> those two numbers do not reproduce - the reviewer measured about 2.3x that
> and so do five fresh runs here, on the same machine and the same build. The
> figures above are the measured spread rather than one run of it, and the
> per-vertex cost is stated because it is the part that travels.

**THE MP5, WHICH IS NOT THIS ROUND'S FAULT AND IS NOT ITS FIX.** The user's
report was "it is actually TR5 but lies invisible on the floor", with no log.
Measured: `5\ITEM\EXTRA5_HK_ITEM.TRM` is found by `HDModsFindModel` in the
mod's game 5 folder and bound to slot 345 under the `EXTRA5_` prefix, it loads
(5478 vertices, 17046 indices), and its two pages 8558 and 8559 come out of the
mod's own `TEX`. In RICH1's own frame for that slot it stands 4.1 units clear
of the classic mesh and is not lifted at all (2.0 units UNDER it in the bind
pose, which is not the frame the level draws it in). And it is invisible,
because **all 5478 of its vertices name classic mesh 1** -
bones (1, 0, 0), weights (255, 0, 0), not one vertex on node 0 - while
`game\items.cpp:222` initialises `HK_ITEM`, `HK_AMMO_ITEM`, `CROSSBOW_ITEM` and
`REVOLVER_ITEM` with `mesh_bits = 1`, so this game never shows their mesh 1.
That is Core's own rule for the laser sight, and the remaster's own
`EXTRA5_HK_ITEM` obeys it: 2426 of its 2499 vertices on node 0 and the 73 that
are not are the sight. In pictures: the mod's file with the full mask covers
8.5 % of the render, and with `-meshbits 1` - what the item really carries -
the render is 100.0 % background at a mean brightness of exactly 170.0, which
is the magenta ground and nothing else at all.

It is the mod's file, so it is reported and not special-cased. What the round
adds is the line that says so, once per slot per level, behind `-log`:

```
HDLOG level=RICH1 item slot=345 name=HK_ITEM model=EXTRA5_HK_ITEM draws
nothing: its geometry sits on node mask 2 and this item shows 1 - the
model's vertices name a classic mesh this slot never draws
```

**AND THE TOOL WAS BLIND IN EXACTLY THAT SPOT.** `ModelProbe`'s `-slot`
picture handed `HDModelDraw` a SINGLE `float[16]` with the slot's node COUNT
beside it, so the draw read past the end of a stack array for every node but
the root - since #153 round M3 put the ring turn in front of the matrix. A
model whose geometry sits on node 0 drew correctly and everything else
vanished, which is how this round came within one measurement of reporting a
perfectly good file as empty. `EXTRA5_HK_AMMO_ITEM` is the clearest case: 1752
of its 1988 vertices sit on nodes 1, 2 and 3 and none of them reached a
picture. Fixed, and the `-invrot` note that described the old behaviour as if
it were a decision is corrected with it.

**THE TOOL, otherwise.** `-floor` draws a slot the way the LEVEL draws it on
the ground - in the item's own animation frame, the mod's fitted `_OPTION`
model where there is one - with the lift applied, the classic mesh's own lowest
plane over the picture as a narrow green outline, and a small square of it
right under the model's deepest point, which is the one place the model can
touch that plane. The three numbers stand beside the picture. The small square
is there because the big one cannot be read: at a low camera its near edge is
well in front of the point the rule is about, so a model resting exactly on the
plane still shows a gap there. Where the deepest point is also the lowest thing
on the screen the outline meets the patch and the contact is plain; where it is
not, the patch crosses the silhouette, because the overlay goes into the pixels
after the model and takes no part in the depth test. The picture shows the
FRAME and the MOVE; that the lift lands exactly on the mesh is the numbers'
work, and they are read by three independent readers below. `-nofloorlift` is the same picture with the lift
left off - the BEFORE half of every pair below - and `-floorcheck` is the
table, every row in the drawn frame with the bind-pose row under it and the
sweep's cost at the end.

**THE LOG.** Five lines behind `-log`, none of them thinning anything: `HDLOG
level=.. pickup floor slot=.. name=.. model=.. lowest point N above|under the
classic mesh's - lifted by N units|not lifted`, its fitted twin `HDLOG
level=.. ring floor slot=.. name=.. takes the fitted <model> on the floor,
lifted by N units (its lowest point stood N under|above the classic mesh's)`,
the frame line `HDLOG level=.. pickup floor slot=.. name=.. drawn frame: node
0's y axis points (x y z) in the item's frame, N vertices swept, N of them
blended`, a `not measured: ..` line for a slot the rule could not read, and the
`item slot=.. draws nothing` line above. Round 155's unconditional "keeps the
level's own" line is gone with the refusal it described.

**THE NINE LOWS OF ROUND 155'S REVIEW, all closed.**

1. `hd_classic.cpp`'s `Reader::open` leaked the `FILE*` on a zero-byte file -
   every caller returns on false without a close. It closes it now.
2. Without an I-III installation `parent_of("")` gave `"."` and the walk
   scanned `<cwd>\1\DATA`. Proved by running the old build twice from two
   working directories: with a decoy `1\DATA` holding two `.PHD` files it said
   *"in no level file of that game"*, without one *"no level file in that
   game's DATA folder"* - two different answers about the same absent
   installation. The new build says *"there is no Tomb Raider I-III
   installation to read game 1's levels from"* in both.
3. The refusal line doubled its prefix: the reason itself began "game 1 object
   102: " and the engine's line had already said "classic 1 object 102: ". The
   reason says WHY now and every printer says WHICH - including ModelProbe's,
   which did not name the object at all before.
4. The floor line that read like a decision and was unconditional is gone: the
   rule replaced it, and both of its lines say what was really done.
5. `HDModelRingFitUnderFloor`'s box corners are replaced by `HDModelFloorLift`'s
   vertices - one function for the plain and the fitted case, engine and tool.
6. The README's name list was missing `REVOLVER_AMMO_OPTION`; the table in
   `hd_classic.cpp` has thirteen rows and the list has thirteen names now.
7. The `-classicpose` examples in this file and in `Preparation\Tools\README.md`
   left out the positional game 5 folder and `-tr3`, so neither was runnable as
   written. Both are spelled in full.
8. `-noclassicpose` printed no pose line at all, so the two runs the switch
   exists to compare were told apart only by a box measurement further down. It
   says so now.
9. The two markers reading "(#153, round 155)" say `#155`.

**PROVED.**

- *The floor, in pictures.* `ModelProbe -floor -pitch 4`, looked at: the mod's
  two Uzis lie CROSSED and FLAT on the classic mesh's own plane - seen from
  above in `uzi_mod_top`, where the cross is plain - and the `-nofloorlift`
  twin of the same command has that plane cutting through the guns. The
  crowbar, the largest lift of an install file, rests its tip on the plane
  where its twin has it through it. The HK USP's pistols float 5.6 units clear
  and are not lifted, exactly as expected of an install-shaped file; the
  Spas-12 stands 2.5 clear and is not lifted either. Every lifted model's
  silhouette stands HIGHER in its picture than in its `-nofloorlift` twin, by
  the pixels the lift is worth - 41 for the fitted pair's 19.2 units, 25 for
  the crowbar's 6.2, 20 for the pistol ammunition's 2.9, 6 for the install
  Uzi's 1.6 - which is the one thing about the lift a picture can carry on its
  own. Twenty-three renders with the numbers printed beside each.
- *The MP5.* The full mask against `-meshbits 1`, the second one 100.0 %
  background at a mean brightness of 170.0.
- *Nothing moved.* `ModelProbe -all -nodraw` over ANDREA1 with no `Mods`
  folder, the build before the round against the build after it: **287 lines
  identical**. The eight known outfits drawn with pistols: **eight
  byte-identical `.PNG` files**. And round 155's own three ring commands,
  re-run: **seven byte-identical renders**, the same pose lines, the same fit
  lines, `-ringcheck` and `-pickupcheck` still PASS.
- *Nothing under `TR5Randomizer\`* was touched, and nothing outside the HD
  gate.

**WHAT IS NOT PROVED.** The engine's own log and the pickup seen in the game -
no tool stands in for either, and the play test is the user's. The six install
models that now move by one to six millimetres are the part of this round a
play test could still argue with; the numbers are above and the switch that
draws them either way is in the tool.

**FIX ROUND 1 (the review) - THE LIFT WENT THE WRONG WAY, AND THE PICTURES
COULD NOT SEE IT.**

The rule above was measured and applied in the slot's BIND pose. An item is not
drawn in its bind pose: `DrawAnimatingItem` calls `gar_RotYXZsuperpack` for node
0 before the first mesh is drawn (`game\draw.cpp:955-960`) and once more for
every node below it, so node 0's matrix carries the animation frame's own turn.
**Six of ANDREA3's eighteen pickup slots carry a turn that is not a plain yaw**,
and `UZI_ITEM`'s is 180 degrees about z: the model stands on its head. The first
build wrote the lift onto the node matrix's y ROW - the image of the MODEL's y
axis - so for that slot it pushed the mod's fitted pair **11.5 units DEEPER into
the ground**, with a log line saying it had been lifted onto the classic mesh's
lowest point. `PISTOLS_AMMO_ITEM`'s install model, which as drawn stands 3.3
units ABOVE the mesh and needs nothing, was moved 3.2 units in a direction that
is 96 % down and 24 % sideways. Neither could be seen in the round's seventeen
renders, because `ModelProbe`'s `-floor` drew the bind pose while its usage
block promised "the way the LEVEL draws it".

**WHAT CHANGED.**

- `HDFramePose` (`hd_model.cpp`) is `DrawAnimatingItem`'s walk with the frame's
  rotations in, once per slot at level build: twelve floats a node, the engine's
  own 3x4. It is written once and linked by both the engine and the tool, like
  the fit. The frame's OFFSET and the item's own rotation are left out, and the
  second of those is measured and not assumed: `InitialiseItem` zeroes `x_rot`
  and `z_rot` (`game\items.cpp:201-202`), the level file gives an item one
  angle, and the one place that tilts a pickup puts all three back before
  anything is drawn (`PickUpCollision`, `game\pickup.cpp:789-793, 1124-1126`).
- Both lowest points go through it. The classic side is no longer
  `MESH_DATA::bbox[4]` but the mesh's own vertices (`aVtx`, which
  `ProcessMeshData` already filled), because a box under rotation is a bound and
  not a mesh - the very fault round 155 had on the HD side.
- The HD side keeps the node each vertex rides on, so the bind fold `set_joint`
  does per node can be undone per point. Every pickup model keeps its vertices
  now, not only the fitted ones: with a rotation on a node, the box the build
  took is no longer a box of the DRAWN vertices.
- The lift is added **straight up in the world**, in one place for both cases:
  the node matrices are model-to-VIEW, so it is world up expressed in view
  space, worked out once per CAMERA. `RingModel::floorFix` - the fit with the
  lift folded into its translation row - is gone; that row is the ITEM's frame,
  which is exactly the frame that is not up.
- `ModelProbe -floor` draws the item's own animation frame, frames the camera on
  the box of the DRAWN vertices, puts the floor patch under the model's deepest
  point, and leaves `-invrot` / `-axisview` out of a floor picture altogether.
  `-floorcheck` prints the drawn frame and the bind pose side by side.

**THE PROOF THAT IT IS RIGHT THIS TIME.** The reviewer wrote an independent
decoder of the frame block and the mesh block and printed six of ANDREA3's
slots in the drawn frame. This build reproduces all six exactly - slot 240
-1.0/5.2/6.2, slot 335 33.1/29.8/-3.3, slot 336 19.5/21.1/1.6, slot 337
26.0/27.5/1.5, slot 351 30.0/34.2/4.2, slot 352 49.0/45.6/-3.4 - and a third
reader written for this fix round (`drawnlowest.py`: its own walk of the level,
its own matrix code, its own `.TRM` reader) reproduces RICH1's `HK_ITEM` for
both the install's file and the MP5's, node chain and bind fold included.

**AND THE LOW ONES.** The slack comment said half a unit was "a twentieth of a
millimetre"; 0.5 of 1024 units to a metre is half a millimetre, and the comment
says so. The fitted log line said "under the item's" where it had measured the
classic mesh. `ModelProbe`'s `-floor` with `-invrot` applied the lift in the
turned frame - there is no turned frame in a floor picture any more. The save
and load crystals that `HDSlotIsPickup` also catches are named in the
`COMPASS_ITEM` paragraph above. `ring_model_of` no longer walks its list per
item per frame: the plan pointer is a field of the slot's verdict, set after
the plan is compacted.

**PROVED AGAIN.** Round 155's three ring commands: **seven byte-identical
renders**, the same pose and fit lines, `-ringcheck` and `-pickupcheck` PASS -
the ring is untouched, and that is the check that caught a first attempt at
this fix which had changed how the camera frames an `-invrot` picture.
`ModelProbe -all -nodraw` over ANDREA1 with no `Mods` folder against the build
before the round: **287 lines identical**; the eight known outfits with
pistols: **eight byte-identical `.PNG` files**. Twenty-three floor renders with
their numbers, looked at one by one.

**FIX ROUND 2 (the second review) - THE ROUND HAD MEASURED TWO LEVELS OUT OF
FIFTEEN, AND THE BIGGEST LIFT IN THE GAME WAS IN NEITHER.** Everything above
was measured on ANDREA3 and RICH1. Run over all fifteen shipped levels with
each level's own `.MAP` in hand, the rule moves one pickup twenty times further
than anything the record admitted to: **RICH3's `CROSSBOW_AMMO1_ITEM` is lifted
126.6 units**, and the paragraph above told the reader the opposite ("one to
seven millimetres of gun in the ground"). The rule is not wrong; the round's
coverage was.

**WHAT RICH3 LOOKS LIKE.** The slot is the grappling gun's ammunition - the
engine's own inventory table gives it the text `TXT_GRAP2`
(`game\newinv2.cpp:455`) - and the installation has no Tomb Raider V model for
it, so the binder's last resort finds the Tomb Raider IV one:
`4\ITEM\EXTRA4_CROSSBOW_AMMO1_ITEM.TRM`, an upright quiver of bolts (the tool's
`-all` table says `via prefix4`). Drawn as the file stands it reaches **153.6** where the level's own
mesh ends at **27.0** - the quiver is 203 units tall and **126.6 of them, 62%
of it, are below the ground line**, which is a stub of leather sticking out of
the floor. Three real items carry it (`ItemCheck RICH3.TRC 342`: items 10, 82
and 209, OCB 0, 4 and 0). The two renders are in the proofs: before, the floor
plane cuts the quiver a third of the way down; after, it stands on the plane.

**AND THE RULE'S PREMISE, CHECKED AGAINST THE ENGINE'S OWN PLACEMENT.**
`InitialisePickup` puts a pickup down with `item->pos.y_pos -= bounds[3]`
(`game\init.cpp:489-490`) for OCB 0, 3, 4, 7, 8 and 11, and `GetBoundsAccurate`
hands it the animation frame's own six box shorts - so **`bounds[3]`, the box's
deepest point, is placed exactly ON the floor**. Walking the `.TRC` for every
object between slots 240 and 360 of ANDREA3 and RICH3, that stored box bottom
equals the classic mesh's own deepest point plus the frame's y offset, row for
row (two exceptions, neither a pickup this rule moves: RICH3's slot 284, whose
stored box is 16 units off its mesh, and ANDREA3's `PISTOLS_AMMO_ITEM`, 1.1 off
because Core rounded the box of a TURNED mesh into shorts). So "as deep as the
classic mesh" IS "on the floor" for these items, and the lift puts the quiver
where the engine already promised the ground was.

**THE WHOLE CAMPAIGN, EVERY LIFT IT PRODUCES** (`ModelProbe -floorcheck` with
`-level`, fifteen levels, once with the user's `Mods` folder and once without;
rows that are not lifted are left out - there are sixteen of them and they are
the same in both passes):

```
  lift  slot                 model                        levels
 126.6  CROSSBOW_AMMO1_ITEM  EXTRA4_CROSSBOW_AMMO1_ITEM   RICH3          install
  19.2  UZI_ITEM             UZI_OPTION, fitted           ANDREA3 JOBY2-5    mod
   6.9  COMPASS_ITEM         EXTRA4_COMPASS_ITEM          13 levels      install
   6.8  UZI_AMMO_ITEM        UZI_AMMO_OPTION, fitted      ANDREA1-3 JOBY2-5  mod
   6.2  CROWBAR_ITEM         EXTRA_CROWBAR_ITEM           ANDREA3 ANDY3  install
                             CROWBAR_ITEM_ANDREA2         ANDREA2 JOBY3-5
   4.2  LASERSIGHT_ITEM      EXTRA_LASERSIGHT_ITEM        ANDREA1-3 JOBY2 ins
   2.9  BURNING_TORCH_ITEM   EXTRA_BURNING_TORCH_ITEM     ANDY1 ANDY3    install
   2.9  PISTOLS_AMMO_ITEM    EXTRA_PISTOLS_AMMO_ITEM      ANDREA1-3 JOBY2-5
                             (the HK USP mod's file; the install's own is 3.3
                              ABOVE the mesh and is not lifted)                mod
   1.6  UZI_ITEM             EXTRA_UZI_ITEM               ANDREA3 JOBY2-5 ins
   1.5  UZI_AMMO_ITEM        EXTRA_UZI_AMMO_ITEM          ANDREA1-3 JOBY2-5 ins
   1.1  REVOLVER_ITEM        EXTRA5_REVOLVER_ITEM_JOBY    JOBY2 JOBY5    install
                             (the same file in ANDREA1-3 stands 10.4 ABOVE the
                              mesh - a different frame, not a different file)
   1.0  FLARE_ITEM           EXTRA_FLARE_ITEM             10 levels      install
```

`BURNING_TORCH_ITEM` and `REVOLVER_ITEM` were not named anywhere in this record
before; neither was RICH3. The `UZI_ITEM` row is 19.2 with the mod and 1.6
without it, because with the mod the floor takes the fitted `UZI_OPTION`.

**THE ONE THING THIS ROUND CANNOT DECIDE.** 126.6 units is 12 centimetres of
vanilla pickup moving up in a level the user has played a hundred times. It is
the rule doing exactly what it was built to do, and the picture before it is
worse than the picture after it - but it is his call, and `-nofloorlift` draws
it both ways for him. Nothing else in the fifteen levels moves by more than 7
units without a mod installed.

**THE RULE MEASURED THE FRAME THE ENGINE DOES NOT DRAW** (`hd_object.cpp`,
`first_frame_of`). It took `objects[slot].frame_base`; the draw reads
`anims[item->anim_number].frame_ptr` (`GetFrames`, `game\draw.cpp:2512-2524`,
at `frame_number == frame_base`, so frame zero of that animation). The first
build logged a line when the two differed and then deliberately measured the
one the engine does not draw. It now measures the animation's and logs the
difference. **On the shipped levels this changes nothing, and that is measured
rather than assumed**: walking the fifteen `.TRC` files, of **1,791 objects**
the **1,748 that have an animation at all carry that animation's own frame
offset** as their object frame offset, every one of them, and the 43 that have
none (slots 25, 28, 127, 369 and 370, no pickup among them) take the rest-pose
fallback. Every
`-floorcheck` table of this round comes out line for line identical to the
reviewer's own runs from before the change.

**A LARGE LIFT USED TO WALK OUT OF ITS OWN PICTURE** (`ModelProbe`, `-floor`).
The camera was framed on the box of the model BEFORE the rule moved it, so
RICH3's quiver was rendered with its top cut off - the one case where the
picture is most wanted. The box is stretched upward by the lift now, and by the
same amount **whether or not the lift is applied**, because `-floor` and
`-nofloorlift` are a pair and a pair that does not share a camera cannot be
compared in pixels. The tool says so in a line of its own: *"and 127 units of
room above it, which is where the lift puts the model"*.

**THE "A PICKUP ONLY EVER CARRIES A YAW" CLAIM WAS NOT EXHAUSTIVE**
(`hd_model.cpp`, the frame walk). Two of the family tilt while they FLY:
`FlareControl` turns a thrown flare by 546 about x and 910 about z every frame
(`game\laraflar.cpp:31-37`) and `FlameTorchControl` a falling torch by 910
about z (`game\flmtorch.cpp:286-287`), both outside `PickUpCollision`'s
save-and-restore - and both zero the two angles the moment the thing comes to
rest, which is when a floor rule is about it at all. Their lifts are 1.0 and
2.9 units, and the lift goes straight up in the world in any case: a tilt
changes what the number would have been, never which way it goes. The comment
and this record say so now instead of citing `items.cpp` as if it settled it.

**THE `mesh_bits` GAP, WRITTEN DOWN - AND ONE ROW OF IT MEASURED THE OTHER
WAY.** `InitialiseItem` gives `HK_ITEM`, `HK_AMMO_ITEM`, `CROSSBOW_ITEM` and
`REVOLVER_ITEM` `mesh_bits = 1` (`game\items.cpp:222-225`), so those four draw
node 0 and nothing else, while both sides of this rule sweep every node. The
review reported that no verdict changes because of it. Measured here with an
independent reader, node 0 alone against the whole object: `HK_ITEM` -4.1 and
`HK_AMMO_ITEM` -2.7 either way, `CROSSBOW_ITEM` -0.9 (one node), `REVOLVER_ITEM`
in ANDREA3 -10.4 either way - **and one row that does change: `REVOLVER_ITEM`
in JOBY2 and JOBY5**, where the whole object reaches 15.3 and is lifted 1.1
units while node 0 alone reaches 12.5 and would not be lifted at all. The
deepest millimetre of that model is on nodes 1 and 2, which the item never
shows. It stays as it is: the mask is the ITEM's and the lift is per SLOT, so
closing the gap means a lift per item, and the only other place in TR5 that
touches a pickup's mask sets it to zero (`InitialisePickup`, `trigger_flags`
0x100), which draws nothing at all. One millimetre against that is not a trade
worth making today, and the paragraph in the code says what it would buy.

**AND THE SMALL ONES.** The comment at `HDModelReleasePoints` said a level
build does not carry "a third of a megabyte" of points into its first frame;
the kept lists are 13 bytes a vertex - 12 of position and 1 for the node - so
ANDREA3 with the user's four mods keeps **746 KB** and the install's own case
241 KB, and the comment said the smaller number about the larger case. The
round's four `-floorcheck` proof runs had been taken WITHOUT `-level`, so they
described generic-prefix bindings rather than the ones the engine builds (one
row differed: RICH1's `BINOCULARS_ITEM` is `EXTRA5_BINOCULARS_ITEM_RICH` at
-6.4 with the map and `EXTRA_BINOCULARS_ITEM` at -247.0 without it, neither of
them lifted); every table in this round was re-taken with `-level`.

**PROVED, FIX ROUND 2.** The fifteen-level tables with `-level`, twice
(`fix2/allfloor`, `fix2/table_out.txt`). The `-floorcheck` rows identical line
for line to the reviewer's own pre-change runs, all fifteen levels
(`fix2/allfloor` against `review2/allfloor`). Twenty-four floor renders with
their numbers, every one looked at, including RICH3's quiver before and after
(`fix2/floor`). The lift in pixels, each pair on its shared camera:
126.6 units = 255 px, 19.2 = 44, 6.8 = 47, 6.9 = 25, 6.2 = 30, 2.9 = 17,
1.6 = 7, 1.1 = 8 (`fix2/pixels_out.txt`). And the camera change reaches only
the slots that are lifted: six renders of NOT-lifted slots come out **byte for
byte identical** to the reviewer's own runs on the build before it, while the
four lifted ones differ, which is the change doing exactly its own job and
nothing beside it (`fix2/camera_scope.txt`). The independent `.TRC`/`.TRM` reader
on RICH3's quiver, on the four masked slots and on the MP5
(`fix2/drawnfloor.py`, `fix2/meshbits_out.txt`, `fix2/mp5_out.txt`). The frame
pointer over 1,791 objects (`fix2/framepointer_out.txt`). The frame box against
the classic mesh over two levels (`fix2/frameboxes_*.txt`). The ring again:
**seven byte-identical renders**, same pose, same fit, PASS. `ModelProbe -all
-nodraw` over ANDREA1 against the pre-round build: **287 lines identical**,
eight byte-identical outfit renders.


#### Round 156 (#156) - an outfit of Tomb Raider I-III has no hair, so it borrows TR5's head

The user's report of 2026-09-15, with three photographs: "the outfits that come
from TR1-3 all have no hair. They all still need the TR5 head, or another
treatment, as a GLOBAL rule. For me, on the Mistress outfit the head from the
outfit has to go - it overlays the TR5 head, now she is blond." His pictures:
a bald skull with a face and eyes and the braid hanging off it, the same skull
from the side, and a blond head where the TR5 one should be.

**WHAT WAS MEASURED FIRST.** Node by node, over the file, with the engine's own
reader. A `.TRM` vertex names three bones and those are classic mesh tree nodes,
so "what is on Lara's head node" is a count and not an opinion. The triangle
column is the DOMINANT node - the node carrying the most weight over the
triangle's three corners, which is the rule `hd_model.cpp` buckets its draw
ranges by, so it is also the count of what a node mask would hide.

```
  file                        node 14      of which      triangles   pages on
                              vertices     weight 255    on node 14  node 14
  OUTFIT_CLASSIC      (IV-VI)     6,860         6,486        9,485   8001, 8002
  OUTFIT_TR2_HOME     (IV-VI)     6,830         6,241        9,481   8330, 8332,
                                                                     8201, 8002
  OUTFIT_TRX_WETSUIT_1 (I-III)    1,987         1,918          998   8415
  OUTFIT_TRX_WETSUIT_3 (I-III)    1,987         1,918          998   8417
  OUTFIT_TRX_ICE_1     (I-III)      461           461          784   8406
  OUTFIT_TRX_BIKER_1   (I-III)       10             0            0   8409
  OUTFIT_Mistress        (mod)   26,370        19,066       30,811   8002, 8110,
                                                                     8111, 8112,
                                                                     8001, 8003
  OUTFIT_CLASSIC_1       (mod)      932            31           94   8010, 8011,
                                                                     8005, 8002,
                                                                     8001
  OUTFIT_Black_1         (mod)      203             0            0   -
  LARA_HEAD           (IV-VI)   a FACIAL RIG: 5,913 vertices over bones 0..31,
                                which are not mesh tree nodes; rigid on node 14
  OUTFIT_HAIR         (I-III)   3,542 vertices over FIVE nodes at z 0..242 -
                                a BRAID, not a cap; see below
```

Read across, the whole round is in that table.

**A IV-VI BODY CARRIES LARA'S WHOLE HEAD, AND IT IS HAIR.** `OUTFIT_CLASSIC`'s
node 14 is 9,485 triangles, of which **9,483 wear page 8002** and 2 wear 8001 -
the head node of a IV-VI outfit is her hair cap and two triangles of nape. The
separate `LARA_HEAD.TRM` that rides rigidly on the same node is the face and
the front of the skull, and its back is **open**: a picture of it alone from
behind looks through the missing back of the skull at the inside of her own
face. The two are halves of one head by the remaster's own design.

**AN I-III BODY CARRIES HEADGEAR THERE, AND NO HAIR.** All thirty
`OUTFIT_TRX_*` of the I-III bundle - which are exactly the thirty entries of
that install the wardrobe lists, because every `OUTFIT_TR1/2/3_*` name is
refused in favour of the IV-VI bundle's own copy - carry a hood, a hat, a
helmet or a headband on node 14 and nothing else: the wetsuits' diving hood and
goggles (1,987 vertices, all on the suit's own page), the ice hood (461), the
Greek headband (1,127), and six of the thirty carry near enough nothing at all
(`BIKER`: 10 vertices, 0 triangles; `DRAGON`: 29, 0). **Not one of the thirty
wears page 8002.** So the hair is not in the file, `LARA_HEAD`'s open skull
stands in the picture, and what the player sees is the bald head of his
photographs. A mod converted from such a file inherits it, and so does a mod
whose own head IS complete but wrong for TR5 - the Mistress body carries a
full 26,370-vertex head whose 30,811 triangles are 9,497 of TR5's own page
8002 with 20,680 of the mod's 8112 laid over them, which is why she is blond.

**`OUTFIT_HAIR` IS NOT A HAIR CAP - THE ROUND'S ONE REFUTED ASSUMPTION.** The
round was planned on "the I-III bundle keeps Lara's hair in a separate
`OUTFIT_HAIR.TRM` the wardrobe never loads". Measured, that file is a **braid**:
3,542 vertices over five nodes whose boxes run 0..53, 47..107, 93..161,
93..208 and 205..242 along z, which is a chain hanging back out of the skull -
the I-III bundle's own `HAIR_OG`, whose six links measure 0..55, 50..117,
108..175, 165..232, 223..322, 274..326 the same way. The Legend mod's own
`OUTFIT_HAIR.TRM` is the same thing with six links. The wardrobe has always
passed both over as "a braid and not a body" and was right to. Nothing of this
round loads either, and the mode that was to have drawn one is re-aimed on the
measurement (below).

**THE NECK, and whether dropping node 14 opens a hole.** Measured per file:
with node 14's triangles hidden, what is left of the body reaches up to
y -240 .. -250 on every one of the seven bodies - the collar, which belongs to
the TORSO node - while `OUTFIT_CLASSIC`'s head node reaches DOWN to y -238.7.
So the donor overlaps the neck it is put on by 1 to 11 units on these bodies
and there is no hole to open. (y is down; the top of the skull is -340.)

**THE RULE - THREE MODES, ONE BIT AND ONE PART.**

| mode | the body's node 14 | `LARA_HEAD` | TR5's head node |
| --- | --- | --- | --- |
| `outfit` | drawn | drawn | not drawn |
| `tr5` | **not** drawn | drawn | drawn |
| `hair` | drawn | drawn | drawn |

`outfit` is the picture this engine drew before the round and is what every
IV-VI body gets. `tr5` is the global rule the user asked for. `hair` keeps a
hood or a hat that is worth keeping and puts TR5's hair over it - measured on
the diving suit, the goggles stay and the hair comes through, which is a better
picture than either half.

**`hair` IS THE RE-AIMED THIRD MODE.** As planned it was "the outfit's own head
is drawn, `LARA_HEAD` is NOT (the I-III head brings its own face), and
`OUTFIT_HAIR` is drawn on top". Both halves are refuted by the measurement
above: the I-III head is a HAT and brings no face - the face comes from
`LARA_HEAD` in both bundles - and `OUTFIT_HAIR` is a braid. What survives is
the intention, "its own head and hair over it", with the donor corrected to
what the hair really is.

**THE DEFAULT IS THE BUNDLE AND NOTHING ELSE.** `tr5` for every entry of the
I-III install and for every mod whose `mod.txt` says `origin=tr1|tr2|tr3`;
`outfit` for everything else. A mod's `outfit.txt` overrides with
`head=tr5|hair|outfit`, an unknown value is a note and the default stands. On
this machine: 104 entries, 63 `tr5` and all 63 by origin, 41 keeping their own
head.

**AND THERE IS NO RULE BY NAME AND NONE BY GEOMETRY, ON PURPOSE.**
`OUTFIT_TR2_HOME` is the name of a IV-VI body with a full head AND the name a
mod gives an I-III conversion; counting vertices to tell a bald head from a
hooded one would put `OUTFIT_TRX_BIKER_1` (10 vertices) and
`OUTFIT_CLASSIC_1` (932) on one side of a line with the ice hood (461) and
would still have to guess about the Mistress body's 26,370. A mod without a
`mod.txt` therefore keeps its own head until its author writes `origin=` or
`head=`, and the log, the F4 window and `ModelProbe -wardrobe` all say which it
is.

**WHERE IT LIVES.** The mode is a field of the wardrobe entry
(`HDWardrobeEntry::head`, with `fromHead` saying who decided), settled once at
scan. `hd_lara.cpp` turns it into two answers at LEVEL BUILD and the draw asks
nothing else per frame: `HDLaraBodyMask()` is the node mask the body draws with
- `~0u`, or every node but 14 - and `HD_LARA_PART_HEAD` is the new part.

**THE NEW PART AND THE NODE CUT.** `HD_LARA_PART_HEAD` is node 14 of the
install's own `OUTFIT_CLASSIC`. No file in either remaster holds that node
alone, so `hd_model` learned to cut one out: `HDModelSlot::onlyNode`, a node
mask, the page cut's twin. Only triangles whose dominant node is in the mask
are built, and only the pages those triangles wear are loaded - which needs a
walk of the index list at page-gathering time, because "which pages does this
node wear" is a fact about triangles and not about the texture list. Only a
slot that asks for a node cut pays for that walk.

**BY NODE AND NOT BY PAGE**, although for this donor the two would almost agree
(9,483 of 9,485 triangles are page 8002, and 14 triangles of that page belong
to other nodes). A head is a node: cutting it that way makes the part's drawn
coverage exactly node 14, so the first person mask that takes the body's own
head off takes the borrowed one with it and nothing else, and a donor whose
artist had put the hair on two pages would still come out whole.

**THE DRAW** puts it after the body and before the face - where the body's own
head node is drawn - with the FRAME's mask (so `mesh_bits` and first person
reach it), the body's cut and the body's cull. That order matters for one
reason: the hair is the wardrobe's only see-through pass, and this keeps it
reaching the card in the order it reaches it when the head is part of the body.

**WHAT IT COSTS**, ANDREA1 with the wetsuit, measured against the same run
without the rule: +27,349 vertices (641 KiB - the index list is the file's own
and is not renumbered, exactly as the page cut leaves it), +28,455 indices
(55.6 KiB), +3 draw calls and **one** new texture page, because 8001 is already
in the cache for the hands and 8002 is not. A body in mode `outfit` pays none
of it.

**THE FALLBACK.** A body told to drop its own head and given no donor - no
IV-VI install, or its `OUTFIT_CLASSIC` unreadable - keeps its own head and the
log says so. No head at all is worse than a bald one.

**THE F4 WINDOW** gains a `Head` row under `Footsteps`, saying the mode and who
decided it. It is the footsteps row's opposite: that one is a choice and left
and right change it, this one is a fact. The page keeps its height - eight
outfit rows plus two value rows plus the hint is the same eleven lines nine
plus one plus one was.

**THE LOG**, behind `-log`:

```
HDLOG wardrobe scan: 104 entries - ... ; heads tr5=63 hair=0 outfit=41; ms=...
HDLOG wardrobe 13/OUTFIT_TRX_WETSUIT_1: head=tr5 (from origin) donor=OUTFIT_CLASSIC#14
HDLOG level=ANDREA1 lara part=the head model=OUTFIT_CLASSIC hd=1 cover=4(000041a0)
                 verts=27349 drift=0/0 sheen=0.14/18 held=0.00/1 pagesheen=0
HDLOG frame lara ... braid=1 head=tr5/1 mask=ffffffff body=ffffbfff mirror=0 ...
```

`head=<mode>/<1 if TR5's head is on the card>` on the frame line, and `body=`
beside `mask=` so that the one bit the rule changes can be read off a frame.

**THE TOOL.** `ModelProbe -lara-head tr5|hair|outfit` forces the rule for one
picture through the engine's own switch, so `outfit` IS the picture from before
the round rather than a description of it. It is not called `-head`, which the
round's brief asked for: that name has meant "draw the separate
`<NAME>_HEAD.TRM` as well" since #123, and a second meaning that swallows the
next argument would have broken it silently. `-wardrobe` gains a `head` column.

**PROVED.** `ModelProbe -lara ANDREA1` on the build of this round against the
build before it, the user's own `Mods` folder and both installs, 25 renders
each: **16 byte for byte identical** - the eleven IV-VI bodies front and side,
a mod with no `origin=` line, and all four first person pictures, at both eye
positions the game uses, on a IV-VI body AND on the wetsuit whose head node is
now dropped in third person, so the neck lid of fix round U is untouched - and
**9 different**, which are exactly the nine bodies of Tomb Raider I-III origin
the round is for. Then the other way round: `-lara-head outfit` on the new
build reproduces the OLD build's picture **byte for byte** on all four I-III
bodies, so what changed is the rule and not the draw. Every picture was looked
at: the diving suit with TR5's hair and no bald skull, the same body in `hair`
mode with its goggles kept and the hair through them, the Mistress head brown
instead of blond, the headless Legend body and the near-headless barefoot TR1
body both whole.

**A NOTE ON THE MOD NAMES IN THIS ENTRY.** The user was rearranging his
`Mods\Outfits` folder while the round ran - folders and files were renamed
twice between two proof runs - so a key here (`mod/outfits/legend/OUTFIT_BLACK_1`)
is the key that folder had when the picture was taken. Nothing in the rule
depends on a name, which is the point of deciding it by `origin=`: the same
bodies came out `tr5` under all three spellings.

#### Round 159 (#159, #158) - an outfit without a folder can be told what it is, and her skin has its own glow

Two wishes of the user, both of 2026-09-14. The first is a file he had already
written: `Mods\Outfits\outfit_1-5.txt`, 52 blocks naming every outfit of his two
Remaster installations, with a header line saying *NOT READ BY THE ENGINE YET*.
The second is one key more - "shine" is the outfit, and he wants a "glow" for
Lara's own skin.

**WHY HALF THE WARDROBE COULD NOT BE DESCRIBED AT ALL.** A mod is a FOLDER and
may carry an `outfit.txt`; that is how round M2 gave a mod's outfit its hands,
its braid, its material and its skin pages. The 52 outfits of the two installs
have no folder, and a mod that ships seventeen bodies under one `outfit.txt` -
the Legend mod does - cannot say anything about ONE of them. So of the 103
entries this machine scans, 52 had only the built-in table of eight measured
rows and the defaults, and 51 had one file for all of them.

**THE LIST FILE.** A `.txt` lying BESIDE the mod folders - directly in `Mods`,
or directly in a category folder such as `Mods\Outfits` - is read as blocks:

```
Outfit: TRX_Wetsuit_3
sheen=0.10
exponent=14
```

Any name, any number of them, read in PATH order - `hd_mods` sorts the files by
their lower-cased full path, so the folder decides before the name does, and
`Mods\n_top.txt` is read before `Mods\Outfits\a_first.txt` although its name
sorts after it (measured: `a_first`'s `exponent=11` beats `n_top`'s `99`).
`#` is a comment and closes
nothing (his own file puts one under every `Outfit:` line, which is the one
parsing trap here: cut the comment off first and test for empty, and the
comment closes the block it belongs to). A block opens at `Outfit:` and closes
at the next one or at an empty line.

**FOUND BY THE ONE WALK THAT KNOWS WHAT A MOD IS.** `hd_mods.cpp`'s `gather`
never steps into a mod folder, so collecting the loose `.txt` files there -
`HDModsListCount` / `HDModsListPath` - is what makes it impossible to mistake a
mod's own `mod.txt` or `outfit.txt` for a list. A leading `_` switches a list
off exactly as it switches a mod off. The mods line says `lists=` and each file
gets a `MODLOG list<n>` line, so a player whose file does nothing can see
whether it was seen at all - and that holds with the HD path off, where the
mods scan still runs for the sound bank.

**THE NAME RULE.** Matched against the outfit's file stem without regard to
case, to the `OUTFIT_` prefix, or to `_` against a space: `TR2_Swim`, `tr2 swim`
and `OUTFIT_TR2_SWIM` are one name. Without a prefix a block reaches EVERY entry
of that stem - his `Outfit: Classic` reaches `46/OUTFIT_CLASSIC` and the 4-5
Barefoot mod's `OUTFIT_CLASSIC` both, which is what a player who writes one line
about the Rome body means. A key prefix pins one source: `13/TRX_Wetsuit_3`,
`46/Classic`, `mod/outfits/legend/Black_1`.

**THE RANK, AND IT IS PER KEY.**

| tier | |
| --- | --- |
| a list block | `HD_WARDROBE_FROM_LIST` |
| the mod's `outfit.txt` | `HD_WARDROBE_FROM_MOD_TXT` |
| the built-in table of eight measured rows | `HD_WARDROBE_FROM_TABLE` |
| measured out of the `.TRM` | `HD_WARDROBE_FROM_MEASURED` |
| the defaults | `HD_WARDROBE_FROM_DEFAULT` |

A block sets only the keys it has. Proved against the table, with no other list
present: `OUTFIT_TR3_CATSUIT` is `0.45/26` in the table, a block saying
`sheen=0.10` alone makes it **0.10/26** - the exponent stays; `OUTFIT_TR3_COAST`
is `0.08/12`, a block saying `exponent=30` alone makes it **0.08/30** - the
sheen stays. And against a mod's own file: his `TRX_Wetsuit_1` block sets the
material of the Wetsuit Green mod's body while that mod's `outfit.txt` keeps
naming its skin page, so one row reads `0.10/14 (list)` and `8001 (outfit.txt)`
at once. (The tier word is per PROPERTY GROUP and not per key - `fromSheen`
covers the pair, as `fromFlags` has covered five flags since round M2 - so a
pair one block half-set reads `list`.)

**ONE READER FOR BOTH KINDS OF FILE.** An `outfit.txt` and a block take the same
keys, so `take_key` and `apply_txt` are one implementation with a `where` string
and a tier number: two readers of one format are two readers that will one day
disagree about it, and the rank is only real if every tier writes the same
fields in the same order. The `MODLOG` notes of `outfit.txt` therefore read
`MODLOG mod3 Outfits\Legend outfit.txt: ...` where they used to read
`MODLOG mod3 Outfits\Legend: outfit.txt ...` - the same words, one clause
earlier.

**NOTHING IN A LIST CAN BREAK ANYTHING.** A scratch file with a key before any
block, a block with no name, a line without `=`, a name that matches nothing, a
key prefix no source has, two values out of range and two words neither `head=`
nor `footsteps=` knows: **5 blocks, 3 matched, 2 unmatched**, nine fault lines,
no crash, and the one good line in the same block as two bad ones still applied.
The one fault that is not a note is the byte order mark Windows Notepad's
"UTF-8" writes in front of the first line: on a comment line it is one harmless
note, on an `Outfit:` line it silently cost the block, so it is taken off (and
only off the first line, which is the only one that can carry one). Measured
with a mark in front of `Outfit: TR2_Tibet`: the block is read and the row says
`0.66/14 (list)`.

**#158, AND WHAT WAS MEASURED FIRST.** `kSheenSkin` - `{0.12f, 10.0f}` - was
read by the eleven hand parts (`kPart[].kind == 1`: REST, PISTOL, UZI, SHOTGUN,
HK, DEAGLE, DEAGLE_LASER, GRAPPLE, FLARE, TORCH, CROWBAR) through
`HDLaraPartSheen`, and by the pages of the BODY the wardrobe names as her skin
through `HDLaraSkinSheen`. Her FACE and the lid on her neck did **not**: they
came out of `kPartSheen`'s own two rows, which carry the identical pair. Two
constants and two table rows saying one thing about thirteen draws.

They are one answer now - `skin_sheen()`, out of the wardrobe entry - so
`skin_sheen=` / `skin_exponent=` (0..2 and 1..128, the ranges all four numbers
of the format take since the fix round below; out of range is a note and the
tier below stands) moves all thirteen at once. The table has no column for
it, because none of the eight measured rows ever separated her skin from her
clothes, so the tier under a list block or an `outfit.txt` is the material
table's own `skin` pair and nothing in between. The two `kPartSheen` rows stay
and say so: they are what that table says skin is, and the default is taken from
them.

**WHERE IT CHANGES THE PICTURE, IN PIXELS.** `ModelProbe -lara ANDREA1 -outfit
46/OUTFIT_CLASSIC` with `skin_sheen=1.0 skin_exponent=8` against the same render
with no list file, counting pixels that moved by more than 8 levels:

```
  her face      4574 of  9000   worst 111      the teal top    10 of 11700
  her right arm 2846 of  5950   worst 110      the shorts       1 of  2800
  her left arm  3001 of  5100   worst 101      the holster      0 of  5850
  her thigh     1321 of  3900   worst  39      the boots        0 of 15400
  her hand       124 of  2450   worst  85      her hair         0 of  2400
```

The handful inside the two cloth boxes is the skin at the neckline and the waist
that those boxes also contain; looked at side by side the top is the same
picture and the face is visibly wet. The whole change is bounded by
`y 39..779` - nothing at or below the boots moved at all.

**BYTE IDENTITY.** With no `.txt` anywhere in the Mods copy, all **32** renders
of round 156 come out **byte for byte identical**, and all **103** rows of
`-wardrobe` say exactly what they said in round 156 - the new column and the new
word aside. The user's own file, on a copy of his Mods folder: **52 blocks, 52
matched, 0 unmatched**, reaching 54 entries, `46/OUTFIT_TR2_SWIM` reading
`1.00/50` with skin `8001` as he wrote it. Its cost is inside the noise: the
whole scan is 34-36 ms with the file and 32-38 ms without it.

**AND THE HEAD RULE IS A LIST KEY TOO.** `head=hair` written in a list for
`13/TRX_Wetsuit_1` draws the picture round 156's `-lara-head hair` drew, **byte
for byte** - the diving goggles kept with TR5's hair through them - so a rule
that needed a mod folder in that round needs a line now.

**THE WORDS.** `HDWardrobeFromTag` is the one-word form of a tier, and a SWITCH
rather than an array: two callers kept their own array of those words and
indexed it with the entry's own from-number, and after #156 `hd_laradraw`'s
still had four rows against five tiers. Both are gone. The level build line
gains `skinsheen=` and the `head=` it never carried; the F4 window gains a
**Shine** and a **Skin glow** row under the Head row, each with the tier that
decided it, so a line a player wrote can be checked in the game - it reads
`(the list)` when it arrived; `ModelProbe -wardrobe` gains a `skin glow` column
and a tier for its `sheen` column. The window uses the PHRASE form,
`HDWardrobeFromWord`, in all three rows: the tag is for a column and a log
field, and two spellings of one tier in three rows of one window ("the table"
over "table") is a player being asked whether they mean the same thing.

```
HDLOG wardrobe list outfit_1-5.txt: Outfit TR2_Swim -> 46/OUTFIT_TR2_SWIM: sheen exponent skin
HDLOG wardrobe list outfit_1-5.txt: 52 blocks, 52 matched, 0 unmatched
HDLOG wardrobe scan: 103 entries - ...; lists=1 blocks=52 matched=52 unmatched=0; heads ...
HDLOG wardrobe 46/OUTFIT_CLASSIC: skin sheen=1.00/8 (from the list)
HDLOG level=ANDREA1 wardrobe key=46/OUTFIT_CLASSIC ... sheen=0.10/14 (list)
                 skinsheen=1.00/8 (list) skin=8001,0,0 (list) feet=0 (default)
                 head=outfit (default) pages=the install's
```

**THE FIX ROUND: THE TWO NUMBERS THAT WERE NOT RANGED.** The round above gave
its two NEW keys a range and left the two OLD ones taking any value at all,
while this record and the README promised in capitals that nothing in a list
file can break anything. Measured on a copy of the user's Mods folder:

| written in a block | what it drew | what it said |
| --- | --- | --- |
| `sheen=14` + `exponent=0.10` | her top, shorts, holsters, boots and hair **pure white** | nothing |
| `sheen=1e40` | the same, **solid black** | nothing |
| `sheen=-1`, `exponent=0` | nothing - dropped by `apply_txt` | "Outfit … : exponent sheen", both named as taken |

The first line is the user's own two lines transposed, which is the typo a
format of `key=value` invites. All four numbers now go through one reader,
`take_number`: **0..2** for `sheen` and `skin_sheen`, **1..128** for `exponent`
and `skin_exponent`, and a value that is no number at all is refused as well -
`sheen=shiny` used to be a silent `0.0`, which is matt cloth and nobody's
intention. Out of range is a note, the tier below stands, and the key is **not**
named as taken, so a block's own line can no longer claim a value that never
arrived. The ranges hold every value any file on this machine writes: the
built-in table tops out at `0.45/26`, all nine of the user's outfit mods write
`1/50`, his own list writes `0.10/14` and `1/50`. Measured afterwards on a file
holding every one of those mistakes: **eight notes**, `46/OUTFIT_CLASSIC` still
`0.10/14 (table)`, the render **byte for byte** the one drawn with no list file
at all - and the one good value in the same file (`sheen=0.55` on
`OUTFIT_TR3_COAST`) still applied, reading `0.55/12 (list)`: the exponent from
the table, the sheen from the list, in one row.

**AND THE SUMMARY COUNTED ITS OWN PROSE.** `apply_lists` read `matched` and
`unmatched` back out of the notes the blocks had just written, looking for
`-> no such outfit` in them - and `note()` cuts at 512 bytes. Measured: a block
named with 912 characters writes a note of exactly 512 bytes that ends in the
middle of the name, with no tail to find, so it counted as **matched**; on a
file of six blocks the old summary read "4 matched, 2 unmatched" where the
lines under it said three and three. `close_block` counts what `apply_block`
returned, and the same kind of file now reads **3 blocks, 1 matched, 2
unmatched** with the over-long note still cut. `ModelProbe
-wardrobe` had the same class of fault in its table: `sprintf` into `sheen[48]`,
where `3e38` alone is 42 characters before the tier word is added. `snprintf`
and 96 bytes. The engine refuses such a value now, but a tool that prints what
it is handed may not depend on that.

**WHAT HIS OWN FILE DOES TO TWO OF HIS MODS.** Dropping `outfit_1-5.txt` in
takes the wet shine off two mod bodies, because two of his names carry no key
prefix and a list is the top tier:

```
  mod/outfits/4-5 barefoot/OUTFIT_CLASSIC        1.00/50 (outfit.txt) -> 0.10/14 (list)
  mod/outfits/wetsuit green/OUTFIT_TRX_WETSUIT_1 1.00/50 (outfit.txt) -> 0.10/14 (list)
```

That is the no-prefix rule doing exactly what it says, and 100 of the 103 rows
are untouched (the third is his own `TR2_Swim`), but it is a decision to make
with the numbers in front of you: `46/Classic` and `13/TRX_Wetsuit_1` in those
two blocks keep the two mods as they were.

**RE-TAKEN AFTER THE FIXES.** 32 of 32 round-156 renders byte for byte
identical with no list present; the whole `-wardrobe` report identical to this
round's own, the `ms=` field aside, both with his file and without it; and
nothing written anywhere in his Mods folder or his game folder.

**HIS OWN FILE CHANGED DURING THE ROUND, AND NOT BY US.** The paragraph above
used to end "his file untouched on disk (`004a4633…`)". That hash was the
file's when the round began and not the one it has: at **2026-09-16 01:52:23**,
while the round was running, three `head=hair` lines appeared in the
`TRX_Wetsuit_1/2/3` blocks of
`Tomb5 Steam Version\Mods\Outfits\outfit_1-5.txt` and its md5 became
`eb4b3016fe7d72562e42ada61c02116d`. It is his file and his edit - the lines
stand in the file's own house style and carry no key prefix, where every test
file this round wrote used prefixed names - and it was left exactly as found.
The sentence is corrected rather than deleted because it invited the one check
a reader would run: `md5sum` on that file today does **not** give `004a4633…`,
and a hash in a record is worthless the moment it reads as a broken promise.
What is true, and is all that was ever meant, is that nothing of this project
wrote into his Mods folder or his game folder. His edit works, which is its own
proof that the round did what it says: those three blocks log `sheen exponent
head`, their rows read `hair (list)`, the head census moves `tr5=62 hair=0` to
`tr5=58 hair=4`, and the body renders byte for byte round 156's forced-hair
picture. His file's own header still says "NOT READ BY THE ENGINE YET" and does
not mention `skin_sheen=`/`skin_exponent=`; refreshing it is his or the project
manager's (the generator is `scratchpad/round155/outfit_list.py`).

**THE SECOND REVIEW: A VALUE THAT NEVER ARRIVED, TWICE MORE.** The fix round
above says it closed this - "`took()` runs only on a value `take_number` kept,
so the block's own line and the picture can no longer disagree" - and it had,
for the four numbers. Two other kinds of value were still being dropped in
silence while the block's line named them, which is the same fault wearing
different clothes:

- **`skin=` WIPED AN OUTFIT'S SKIN PAGES WITHOUT SAYING SO.** `take_key` set
  `hasSkin` and called `took()` *before* it read a character, so a `skin=` line
  with nothing after it copied an empty array over the tier below.
  `46/OUTFIT_TR3_CATSUIT` went from `8201,8202 (table)` to `- (list)` and the
  log said `-> 46/OUTFIT_TR3_CATSUIT: skin`, as though something had arrived.
  It is one keystroke away from a player, because his own `outfit_1-5.txt`
  carries `# skin=` ready-written in **44 of its 52 blocks**: uncommenting the
  line he was invited to uncomment, without filling in a number, took round K's
  neck seam fix off that body. LOOKED AT, 4x on the neckline: her throat and
  sternum carried the catsuit's own tight `0.45/26` streak instead of skin's
  wide `0.12/10`; **6543 px** differ, worst channel 37, the box `x 419..602
  y 184..435` - her chest and forearms and nothing else. `std::atoi` was the
  other half of it: it answers 0 for `abc` and 0 terminates this array, so
  `skin=8001,oops` quietly became `8001` alone. `strtol` now, each piece that
  is not a whole page number above zero is a note, and if none survives the
  line is refused entirely. The render of the bare `skin=` block is now byte
  for byte (`2ee14cf7…`) the render with no list file at all.
- **THE FIVE FLAG KEYS DROPPED AN UNKNOWN VALUE IN COMPLETE SILENCE.** `hands`,
  `gloves`, `braid`, `holsters`, `rig`: `as_bool` answered -1, `apply_txt`
  skipped on `>= 0`, and nothing anywhere said a word. Measured:
  `braid=maybe holsters=ja hands=2 rig=nonsense` in one block logged "braid
  holsters hands rig" as taken, wrote no note, and drew a body byte for byte
  the one with no list. `take_flag` now, one note per key naming the value and
  **spelling out the vocabulary** (`1, yes, true, on / 0, no, false, off`),
  because a player who wrote `ja` learns the words from the log or not at all;
  the block then logs "nothing - the block has no key this build knows" and the
  picture agrees with the line. The `gloves` note of round M2b still prints
  word for word in every case it printed before.

**AND A LINE LONGER THAN THE READ BUFFER WAS APPLIED IN HALVES.** Both readers
used `char raw[512]` and `fgets`, which cuts a longer line in two - and the
HEAD of it was parsed as a whole `key=value`. Measured: a `sheen=` line of 900
zeros and then `.25` was read as `sheen=000…0`, which is `0.0`, which is in
range, which is accepted - `46/OUTFIT_TR2_VEGAS` at `0.00/14 (list)`, a wholly
matt body out of a line that asked for `0.25`, with the tail logged after it as
a line with no `=`. One `read_line` for both files now, which reads until the
line ends however long it is: the same file gives `0.25/14 (list)`, and
`exponent=` + 900 spaces + `20` gives `0.10/20 (list)` instead of a refusal and
a stray line. A NUL byte still ends the line's text exactly as before, so the
hostile files of the review - NUL inside a line, CR-only endings, a 912-
character name, 20001 blocks in 969 KB - all answer byte for byte what they
answered before (the 969 KB file costs 405 ms instead of 331).

**THE F4 PAGE IS THIRTEEN LINES AND ITS COMMENT STILL SAID ELEVEN.** #156 cut
`kWindow` from 9 to 8 to keep the page eleven lines; #159 added the Shine and
Skin glow rows and did not cut it again. No behaviour finding - the page ends
at 16.5 font heights and the screen holds 480/28 = 17.1 of them at every
resolution 4:3 or wider - but the invariant the comment recorded had been
broken silently, so the comment now carries the arithmetic line by line and
says where the next row has to come from (`kWindow` 7, and the page is 12.5
lines). Nobody has SEEN this page since #156: the rules of these rounds forbid
starting the game.

**RE-TAKEN AFTER THE SECOND FIX ROUND.** 32 of 32 round-156 renders byte for
byte identical with no list present; the whole `-wardrobe` report identical to
this round's own accepted run (`ms=` aside) both with no list and with his file
as it now stands (`lists=1 blocks=52 matched=52 unmatched=0`); every hostile
file, the range file and the read-order file identical to the runs the review
took, the one `rv_long` row that the buffer fix corrects excepted; the mod scan
`mods=14` and no mod's own `outfit.txt` value refused by any of the new
readers. The two `-wardrobe` columns of the round's own features are unmoved:
`skin_sheen=1.0 skin_exponent=8` on `46/OUTFIT_CLASSIC` renders `0b8e43f7…`
and `head=hair` asked for by a list renders `a7114937…`, both byte for byte
what the review measured.

**WHAT WAS LEFT ALONE, AND WHY.** The tier word is still per property group -
`fromSheen` covers `sheen` and `exponent`, so a pair a block half-sets reads
"the list" for both halves. The VALUES are per key and proved so (`0.55/12` =
the list's sheen over the table's exponent); splitting the word would mean a
from-field per key, the review asked for no change, and inventing one here is
not this round's work. Nor is a spelling for "this body has no skin pages":
nothing on this machine wants one, and a `skin=` line that names nothing is
now a note rather than a silent wipe, which is the whole of what was wrong
with it.

**THE THIRD REVIEW, AND THE ONE KEY THE TIERS DID NOT CARRY.** The two fix
rounds above closed the same fault twice - a value named as taken and thrown
away - and a tenth key was still doing it, the oldest one in the format.
`take_key` read `name=` into the shared `ModTxt` and named it; `apply_txt` had
no name field at all. The display name was decided in ONE place, at the moment
a mod was added, out of that mod's own `outfit.txt` - so a block's `name=` was
read, logged and dropped in silence, while both documents promised the
opposite (`README.md`: "every key an `outfit.txt` takes is a key of a block";
this record: "An `outfit.txt` and a block take the same keys"). Measured before
anything was touched: a file of two blocks, `name=My Own Home` on
`46/OUTFIT_TR2_HOME` and `name=Renamed Mistress` on the Mistress mod's body,
logs `-> 46/OUTFIT_TR2_HOME: name` and, on the mod,
`-> mod/outfits/mistress/OUTFIT_MISTRESS: name` - and both rows still
read `TR2 Home` and `Mistress`, byte for byte the table with no list at all.

The name is applied in `apply_txt` now, in the same shape as the twelve keys
beside it, and the mod-add path no longer sets it: a mod's own `outfit.txt`
name arrives through the very same call, one tier lower, which is what makes a
list block's name beat it. Measured, with a `name=` put into the copy's
Mistress `outfit.txt` for the experiment: the mod names itself **Mistress By
Her Own File**, a block saying `name=` over it makes the row **Renamed By The
List**, and a block WITHOUT a name leaves that name standing while its
`sheen=0.20` lands - the rank, per key, on the name as on everything else. The
thirteen-key block of the review now moves all thirteen: `name hands braid
holsters rig gloves sheen exponent skin_sheen skin_exponent skin footsteps
head`, where it moved twelve and named thirteen.

**AND AN EMPTY NAME IS NOT A NAME.** `name=` with nothing after it, or with
nothing but spaces, would blank a row and leave a line in the window with no
title - the wipe `skin=` used to do, one key along - so it is a note and the
name the row had stands, and the key is NOT named as taken (`nothing - the
block has no key this build knows`). The field is 64 bytes: a longer name is
cut to 63 and the note says so, quoting **what it kept** and not what it was
given, because `note()` cuts at 512 bytes and a note that echoes a 900
character name in full is a note whose own reason is the part that gets cut
off. Measured, one block each: 900 characters -> the first 63 and the note;
`name=` and `name=<spaces>` -> the note, `TR2 Tibet` and `TR2 Vegas` standing;
a NUL in the middle of the value -> `Before`, the tail dropped by `read_line`
exactly as it is dropped for every other key; 63 characters -> kept whole, no
note.

**IT CARRIES NO TIER WORD, AND THAT IS A DECISION.** Every other from-field has
somewhere its word is shown - a row of the F4 window, a column of ModelProbe's
table, a field of the level build line. The name has none of its own: it IS the
label of the F4 row and the second column of that table, so a `(list)` after it
would have to displace something a player reads to make room for a word nobody
asked for. The block's own log line already names `name` among the keys it set,
which is where a player checks whether his line arrived.

**AND THE DASH RULE UNDER ONE COLUMN WAS THREE SHORT.** `ModelProbe -wardrobe`
prints its flags column with `%-22s`, and both the header (`hands braid hols
glov rig`) and every row (`%5d %5d %4d %4d %3d`) are exactly 25 characters
wide - so the two agreed with each other and the rule of dashes under them, 22
long, stood three columns to the left of everything from `flags` rightwards.
The field is 25 now in all three `printf`s and the rule is 25 dashes: nothing a
row prints moves by a character, which is why the whole report is identical
line for line to the run before it.

**THE PROOFS.** With no `.txt` in the Mods copy the `-wardrobe` table says what
round 156's said on all **103** rows, and the whole report is line for line
what the second fix round's was (`ms=` and the rule aside); with the user's own
`outfit_1-5.txt` in the copy, **52 blocks, 52 matched, 0 unmatched** and again
line for line the run before it - his file carries no `name=`, so nothing may
move, and nothing does. `mods=14` in every run. And **32 of 32** round-156
renders byte for byte identical: a name changes no pixel, which is the whole
of what it is allowed to change.

**NOT IN THIS ROUND.** The head rule itself (#156), the neck lid disc (#156c),
the OperaDress hand rule, #160, #162, the randomizer, the ring, the floor rule.
Nothing of the round is inside the 60 fps files or outside the HD gate, except
the two `MODLOG` lines the mods scan writes about the list files it found -
which is log and not behaviour, and is the one thing a player with the HD path
off can read to see that his file exists.

#### Round 164 (#164) - the outfit changer asks the two rooms that exist, and the one it was afraid of turns out to be free

The user's wish of 2026-09-15, in his own words: *can we raise the budget?* -
and, on being shown that his card holds 16 GiB and his machine 96 GiB of RAM,
*the 32 bit limit is what bites*. His log of that day is the case:

```
  HDLOG level=ANDREA1 budget=3878 MiB (worked out) ... cache 465 -> 517 MiB
  six changes of +4 to +10 pages, +4 to +13 MiB each
  HDLOG redress deferred ... this level has spent 52 of 64 MiB
```

Three gigabytes of his own texture budget untouched, and the sixth change of
outfit refused by a constant.

**WHAT ROUND M3 ACTUALLY CAPPED.** The page cache never gives a page back inside
a level - other models hold slot indexes (`hd_pagecache.cpp`, the A2 note) - so
every outfit worn in one level stays in it, and round M3 (#153) capped that
growth at `HD_REDRESS_ALLOWANCE`, 64 MiB, which is the slack `AutoBudgetBytes`
already holds back on the card. That number was never a measurement of the
player's machine. It was the one number in reach.

**MEASURED FIRST, AND THE MEASUREMENT MOVED THE ROUND'S OWN PREMISE.** The round
was ordered on the assumption that the wall is the ADDRESS SPACE of a 32 bit
process - the managed system-memory copies, the level, the heaps, the driver.
`PoolProbe -pool` (new, below) makes real pages of the cache's own shapes in all
three pools and measures what each takes out of the address space, out of the
card and out of the process's commit. Eight pages a row, on the user's machine:

| pool | format | side | address space a page | card a page | one page's biggest reserve | given back |
| --- | --- | --- | --- | --- | --- | --- |
| `D3DPOOL_DEFAULT` | A8R8G8B8 | 4096 | **0.04 MiB** | 85.25 MiB | 0.21 MiB in 6 regions | nothing was taken |
| `D3DPOOL_MANAGED` | A8R8G8B8 | 4096 | **85.37 MiB** | 0.00 MiB | 85.34 MiB in 1 of 3 | 682.96 of 682.96 |
| `D3DPOOL_SYSTEMMEM` | A8R8G8B8 | 4096 | 85.38 MiB | 0.00 MiB | 85.34 MiB in 1 of 3 | 683.03 |
| `D3DPOOL_DEFAULT` | DXT5 | 4096 | **0.00 MiB** | 21.25 MiB | no new region at all | nothing was taken |
| `D3DPOOL_MANAGED` | DXT5 | 4096 | 21.36 MiB | 0.00 MiB | 21.34 MiB | 170.89 |
| `D3DPOOL_DEFAULT` | A8R8G8B8 | 2048 | 0.00 MiB | 21.25 MiB | no new region at all | nothing was taken |
| `D3DPOOL_MANAGED` | A8R8G8B8 | 2048 | 21.37 MiB | 0.00 MiB | 21.34 MiB | 171.00 |

**The engine's pages are `D3DPOOL_DEFAULT`** - all three callers pass
`managed = false` and there is no setting for it: the rooms
(`hd_level.cpp`, `HDRenderLevelBegin`), the water frames (`hd_level.cpp`,
`BuildWaterFrames`) and the models and the wardrobe (`hd_object.cpp`,
`HDModelLevelBegin`). So a page costs this process **four hundredths of a
megabyte** and puts its 85.25 MiB on the card, where the budget is already
watching it. The address space is NOT the wall the pages run into. Only a
managed page would cost the process its own size, and the engine makes none.

That is said here loudly because it is the opposite of what the round set out
from, and because it decides which condition does the work: **the card's budget
is what really lifts the cap, from 64 MiB to the 3477 MiB his own level has.**

**AND THE ADDRESS SPACE IS STILL WORTH ASKING, for a reason the measurement
handed over.** A `D3DPOOL_DEFAULT` texture cannot be locked, so every mip of
every page goes through ONE reused `D3DPOOL_SYSTEMMEM` staging texture of the
page's whole size (`hd_pagecache.cpp`, `ensure_staging`) - and that is a single
block of 85.34 MiB, measured. A process with 900 MiB free in ten thousand crumbs
cannot make it. That is the fragmentation condition, and it is the one part of
the address side that is not merely a guard.

**THE RULE.** `hd_memory.cpp`, pure, with every input handed in:

```
  spent      = cache - this level's mark                     (as in round M3)
  card room  = budget + HD_REDRESS_ALLOWANCE - cache
  addr room  = ullAvailVirtual - HD_REDRESS_ADDRESS_RESERVE

  allowed = (card room >= worst || spent < allowance)    the card, with round
                                                         M3 as its FLOOR
         && addr room >= worst                           the address space
         && largest free block >= one page               fragmentation
```

`worst` is `HD_REDRESS_WORST`, 20.00 MiB - round M3's own measurement of the
dearest outfit's own pages over the user's whole wardrobe. **The exact bytes of
the chosen outfit were weighed against it and refused**, and not for tidiness:
the hint under the F4 list is drawn for the HIGHLIGHTED row on every frame and
the highlight moves as the player scrolls, so an exact answer would mean reading
that outfit's `.TRM` parts per row per frame. Round M2's own numbers say what
that costs - a model build is 278 ms with the user's nine mods, `find_ms` alone
210 of them - and this project's fifth rule says what may not be computed per
frame. A measured worst case that is read from nothing is the cheaper truth.

**THE RESERVE, AND IT IS A FIRST SETTING.** `HD_REDRESS_ADDRESS_RESERVE` is 512
MiB: what a level load, a Bink film, the save screenshot and the driver's own
transient allocations have to find after a re-dress has taken its share. It is
not measured, because the measurement it wants is a play test with the log on,
and it is printed in every level build line for exactly that reason. It is the
one number in this round that is meant to be argued with.

**THE RULE'S OWN ANSWERS.** `PoolProbe -grid` calls `HDRedressDecide` itself -
the function the engine calls - so this is not a description of the rule. The
user's own numbers: budget 3878 MiB, cache 465 MiB + what the changes added.

| spent | card room | addr room | round M3 said | the rule says | refused by | room for |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 3477 | 2488 | yes | yes | nothing | 124 changes |
| 52 | 3425 | 2488 | yes | yes | nothing | 124 |
| 63 | 3414 | 2488 | yes | yes | nothing | 124 |
| **64** | 3413 | 2488 | **no** | **yes** | nothing | 124 |
| 200 | 3277 | 2488 | no | yes | nothing | 124 |

His sixth change was the one round M3 refused. The new rule refuses the
hundred and twenty fifth.

**NEVER STRICTER THAN ROUND M3 ON THE CARD SIDE**, and the rows that prove it
are the ones where the card room is smaller than one change and the old rule
still says yes (cache 465 + spent, address space fine):

| budget | spent | card room | round M3 | the rule | refused by |
| --- | --- | --- | --- | --- | --- |
| 500 | 70 | 29 | no | yes | nothing |
| 480 | 70 | 9 | no | **no** | the card |
| 455 | 40 | **14** | yes | **yes** | nothing |
| 400 | 40 | **-41** | yes | **yes** | nothing |
| 400 | 70 | -71 | no | no | the card |

**AND IT REFUSES EXACTLY WHERE THE ROOM RUNS OUT** (budget 3878, cache 465,
spent 0, so round M3 says yes to every row):

| avail_virtual | addr room | the rule | refused by | room for |
| --- | --- | --- | --- | --- |
| 3000 | 2488 | yes | nothing | 124 |
| 1000 | 488 | yes | nothing | 24 |
| 600 | 88 | yes | nothing | 4 |
| 532 | 20 | yes | nothing | 1 |
| **531** | 19 | **no** | the address space | 0 |
| 300 | -212 | no | the address space | 0 |

| largest free block | one page needs | the rule | refused by |
| --- | --- | --- | --- |
| 1000 | 85.33 | yes | nothing |
| 86 | 85.33 | yes | nothing |
| **85** | 85.33 | **no** | fragmentation |
| 8 | 85.33 | no | fragmentation |

**WITH NOTHING MEASURED it is round M3's rule and only that.** If
`GlobalMemoryStatusEx` refuses, `known` stays 0, the walk is not even made, and
`allowed` is `spent < 64 MiB` - the whole grid over again, identical to the old
column, with a line of its own in the log. A rule may not spend an answer it did
not get.

**WHERE IT IS ASKED, AND WHY NOT WHERE IT WAS.** `HDLevelRedressAllowed` used to
work the answer out on the spot, and the F4 window asks it on **every drawn
frame** it is open (the row that says *applies at the next level*). The new
answer stands on a `GlobalMemoryStatusEx` and a walk of the whole address space:
**203 microseconds over 262 regions**, measured with `PoolProbe -walk` - nothing
once, and a system call storm sixty times a second. So it is worked out where
something CHANGES it - at the end of the level build, before and after every
change, and once as the F4 window opens - and `HDLevelRedressAllowed` hands out
what was stored. Nothing else can grow the cache inside a level: the only
callers of `HDPageCacheAppend` are the level build and `HDObjectLevelBuild`,
which runs in a level build or in a re-dress.

**THE LOG, and every word that was there is still there.**

```
HDLOG level=%s budget=... classic_pages=%d framebuffer=%dx%d
      pool=default avail_virtual=%.0f MiB largest_free=%.0f MiB
      reserve=%.0f MiB walk_ms=%d

HDLOG redress room (the level build): another change is allowed, refused by
      nothing, card=%.0f addr=%.0f largest_free=%.0f MiB (avail_virtual=%.0f
      reserve=%.0f page=%.0f MiB, %d regions, walk_ms=%d), this level has
      spent %.1f of %.0f MiB and has room for %d more

HDLOG redress %d (%s) ms=%d pages ... this level has spent %.1f of %.0f MiB,
      room card=%.0f addr=%.0f largest_free=%.0f MiB, avail_virtual %.0f ->
      %.0f MiB (-%.1f for +%.1f MiB of pages)

HDLOG redress deferred to the next level (%s): ... and the allowance is %.0f -
      refused by %s, room card=%.0f addr=%.0f largest_free=%.0f MiB and one
      change needs %.0f MiB (a page needs %.0f)

HDLOG redress: GlobalMemoryStatusEx refused - the address space is unknown, so
      the allowance is the 64 MiB of round M3 and nothing more
```

`this level has spent %.1f of %.0f MiB` keeps printing the 64 MiB, because the
64 MiB is still the floor of the card condition - a reader of an old log is not
misled into thinking the line means something else now. The `room (%s)` line is
the new one, and it is where the user's next log will settle the reserve: the
`avail_virtual %.0f -> %.0f (-%.1f for +%.1f MiB of pages)` clause on a real
change is the measurement of this round repeated on his machine at run time. If
that drop ever tracks the MiB of pages instead of staying a rounding error
beside them, the pages are not in the pool this rule was measured for.

**THE F4 WINDOW IS UNCHANGED.** The hint still reads *applies at the next
level*, in the same place, with the same words; the only new line in
`hd_outfitmenu.cpp` is one call as the window opens, so that the frames which
follow read a fresh answer instead of measuring one each. (A second row showing
the room in MiB would fit there - it is not in this round, because the window is
the player's and the numbers belong in the log.)

**WHAT IT CANNOT DO, and did not try.** No eviction and no shrinking cache: a
slot index another model holds does not move, which is the reason round M3 gave
and it has not changed. No change of pool - the measurement above says
`D3DPOOL_DEFAULT` is the right one and a managed cache would cost the process
85 MiB a page for nothing. No automatic rebuild when the room does run out; a
Save and Load already starts a fresh cache. And x64 (#143) would end the
question altogether, which is why `PoolProbe` is built x86 and
`/LARGEADDRESSAWARE` like the engine: without that switch the same process reads
1995 MiB of free address space instead of 3986, and a tool that measures the
wrong wall is worse than no tool.

**THE GATE.** Everything new sits inside `HDEnabled()`: the budget line and the
first answer are inside `HDLevelBuildHD`, past its own `HDEnabled()` test;
`HDLevelRedressAllowed`, `HDLevelRedressRefresh` and `HDLevelRedress` each test
`HDEnabled() && g_level_active` in their first line; and the F4 window cannot
open at all without `HDEnabled()` (`HDOutfitMenuWanted`). Nothing of this round
is on a drawn frame's path, nothing of it is in the 60 fps files, and
`hd_enable=0` and `fps_mode 0` draw the frame they drew before. The three
literal `false` arguments in `hd_level.cpp` became one named `kPagesManaged`
with the same value, so the `pool=` the log prints is the pool the call really
uses and not a typed word.

**NOT IN THIS ROUND.** Page eviction, a shrinking cache, `D3DPOOL` changes, an
automatic level rebuild, x64 (#143), the randomizer, the ring, the wardrobe, and
the F4 window's own layout.

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
