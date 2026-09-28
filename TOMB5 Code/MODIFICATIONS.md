# Modifications to TOMB5

## 1. What this is

This is a modified version of **TOMB5**, the PC Tomb Raider Chronicles decompilation
project by Troye/Trxyebeep and contributors, <https://github.com/Trxyebeep/TOMB5>. It
was modified between **2026-07-30 and 2026-09-23** (ongoing) for the *TR5 Randomizer*
project.

The upstream code as received is preserved as the first commit of this repository
(no hash is named here on purpose: it changes whenever the history is rewritten for
privacy scrubbing, the content does not), so every change made here is visible as a diff
against it. That
baseline differs from current upstream in exactly three files: one upstream commit made
after this copy was taken (`setup.cpp`: Two Gun gains `undead = 1`) and two small local
fixes that predate this repository and are documented in the files themselves
(`dxsound.cpp` LOCSOFTWARE, `function_stubs.cpp` DO_LOG).

Measured on **2026-09-22** by comparing every file under `TOMB5/` byte for byte against
that first commit: **122 files changed, 98 entirely new - 220 in all**, none deleted,
**206** of them carrying the modification notice at the top, which `grep -rl "Modified
2026 by ChrisChanSan" TOMB5/` lists. The project file `tomb5.vcxproj` carries the same
notice as an XML comment. Seven files that earlier work had changed without a notice -
`game/draw.h`, `game/tomb4fx.cpp`, `game/rope.cpp`, `game/sphere.cpp`,
`game/deathsld.cpp`, `game/flmtorch.cpp` and `specific/polyinsert.h` - were given it in
`7c02357`.

Fourteen of the 220 do not carry the notice, and none of them should:

- the four `specific/hd/hd_shader/*.inc` are compiler output (`fxc` bytecode as a C array), regenerated from the `.hlsl` files beside them and carrying no source of this project at all;
- `tomb5/libs/stb/stb_vorbis.c` and `tomb5/libs/discordRPC/LICENSE` are other people's files that are not modified here in any way - stb_vorbis carries its own licence at the end of the file, and the second one *is* a licence. A notice on them would claim an authorship this project does not have;
- the other eight are under `vendor/trforge/`: five source files copied byte for byte from TRForge (MIT), its `LICENSE`, its `NOTICE.md` and a `VERSION.txt` recording where they came from. The five sources carry a hidden signature in their first three lines; writing anything into them, a notice included, would destroy it, and nothing in them is changed, so there is nothing to notice.

`game/trex.h`, `game/raptor.h` and `game/compy.h` carry the notice and additionally name
tomb3 as their origin further down (section 2.3).

This modified version remains licensed under the **GNU General Public License version
3**, the same licence as the original. See `LICENSE`. The complete corresponding source
is distributed alongside any binary released. Every file changed under `TOMB5/` carries
a modification notice at the top, as required by GPLv3 section 5(a).

Original credits are unchanged and remain with the upstream project: Troye (main
programmer, project leader), ChocolateFan, thatguyfrombb, Raildex. The work recorded
below is layered on top of theirs.

The modifications fall into two separable groups. The first would be useful to anyone
who owns Tomb Raider Chronicles and has nothing to do with randomizing; the second
exists only to support the Randomizer.

## 2.1 Native Windows 11 port

TOMB5 as received draws through DirectDraw and Direct3D 3 and plays its effects through
DirectSound. On Windows 11 that stack no longer creates a rendering device at all, so
the port replaces it with Direct3D 9 and XAudio2 and the game runs without a wrapper
such as dgVoodoo2. The rendering work follows the approach TR2Main used for Tomb Raider
II. Everything that touches the player's own files is optional and read at run time: the
remaster's `MAIN.SFX`, his own OGG soundtracks. No audio and no game data of any kind
ships with this project, and nothing is taken from another program's binaries. Paths
below are relative to `TOMB5 Code/TOMB5/`.

### Direct3D 9 renderer in place of DirectDraw and Direct3D 3
2026-07-30 - 2026-07-31

**What.** The whole rendering path is Direct3D 9: device creation, surface handling,
present, adapter and mode enumeration and the cooperative level. Textures are uploaded
with `CreateTexture`/`LockRect`; room drawing keeps its bump mapping; effects, HUD bars
and the transform state constants follow. The adapter selection defaults to the adapter
that drives the Windows primary monitor instead of a hard-coded index, and the setup
dialog appends the Windows display number (`\\.\DISPLAYn`) to each Graphics Adapter
entry so that two identical GPUs can be told apart. A window is sized from the adapter's
current desktop resolution (`GetAdapterDisplayMode`) rather than from the enumerated
exclusive-fullscreen mode list, which can be much shorter than what the monitor
supports. Fullscreen is borderless windowed. The device is created with
`D3DCREATE_MULTITHREADED`, and `FullScreen_RefreshRateInHz` is left 0 so the driver
keeps the display at its current rate.

**Why.** On Windows 11 `IDirect3D3::CreateDevice` fails outright, for the hardware
device and for software emulation alike. Real exclusive fullscreen never recovered from
device loss: after alt-tabbing back in, `TestCooperativeLevel` stayed at
`D3DERR_DEVICELOST` permanently and froze the game. `LoadTextures` runs on the
background level-loading thread and calls `CreateTexture`/`LockRect` on the same device
the main thread presents with, which is undefined behaviour per the D3D9 documentation
and hung level loads intermittently. Forcing an exact refresh rate from the enumerated
mode list was rejected by `CreateDevice` at higher resolutions, for example 2560x1440.
`setupdlg.cpp` hard-coded `nD3DDevice = 1`, the "hardware" index of the old two-device
D3D3 enumeration, while D3D9 enumeration produces one device per adapter at index 0.

**How.** `specific/dxshell.cpp`, `.h` hold device, surfaces, present, enumeration,
cooperative level and the adapter choice (`DXGetInfo` matches the enumerated adapter
against the primary monitor via `GetAdapterMonitor` and `MonitorFromPoint`).
`specific/output.cpp`, `.h` the render and present path; `specific/function_table.cpp`,
`.h` the one-time render state setup in `HWInitialise` plus an `HWDrawPrimitive` helper
wrapping `DrawPrimitiveUP`; `specific/texture.cpp`, `.h` the upload;
`specific/drawroom.cpp` the bump-mapped rooms; `specific/polyinsert.cpp` the
`HWR_DrawSortList` submission only, the clip and subdivide maths untouched;
`specific/specificfx.cpp` and `specific/drawbars.cpp` effects and HUD bars;
`specific/3dmath.cpp` and `specific/d3dmatrix.cpp` the transform state constants;
`specific/file.cpp` one stray texture call; `specific/winmain.cpp` and
`specific/setupdlg.cpp` back buffer access, `Clear`, render states and the adapter
dropdown, whose text is built in `InitDDDevice` from
`DDIdentifier.Description`/`.Driver`; `global/types.h` and `tomb5/pch.h` the `DXPTR`
struct and the includes. Two sampler differences the port had to absorb:
`SetSamplerState` instead of `SetTextureStageState` for the filters, and explicit CLAMP
addressing, since D3D9 defaults to WRAP. `HWInitialise` had `D3DTSS_ALPHAARG1` on
`D3DTA_TFACTOR` while `D3DRS_TEXTUREFACTOR` is never set anywhere, so alpha test and
blend never saw real per-pixel texture alpha; it is `D3DTA_TEXTURE`, which is what
removed the menu text ghosting and the black boxes around foliage and vines.

**Commits.** `d8e8a6b`, `274cd0e`, `c991fcc`, `ae66bd7`, `e30924d`, `7774945`,
`5b678a4`, `7ca8cab`, `0e1cce0`, `33f09cd`, `822a7f6`, `a55b79d`, `e215ba9`, `d0b00cb`,
`81f62fa`, `e4fbc81`, `5094e04`, `fe6d663`, `7b61991`, `c07c162`, `b542ba0`

### Bink video drawn through a Direct3D 9 texture
2026-07-30

**What.** FMV frames are uploaded into a D3D9 texture and drawn as a full-screen quad
instead of being blitted with the GDI `StretchBlt` the original used. `fmv.cpp` keeps a
small standalone DirectDraw object and surface as the Bink decode target only; nothing
of the rendering path goes through it.

**Why.** GDI blitting does not reach the screen while a D3D9 device holds the display,
because the display scans out directly from the D3D back buffer, so every FMV played as
a black screen. This is also why `Ddraw.lib` has to remain on the linker line.

**How.** `specific/fmv.cpp`.

**Commits.** `7774945`, `c560c71`, `33f09cd`

### Savegames: the screen grab, the folder, and the check on read
2026-07-30 - 2026-08-22

**What.** The frame grab behind the save thumbnail and the Escape-menu backdrop is
copied with `GetRenderTargetData` into a `D3DPOOL_SYSTEMMEM` surface and locked there.
Savegames live in a `savegame\` subfolder, and any `savegame.N` still lying loose in the
game folder is moved there once at startup, never overwriting. The number of bytes the
read actually returned is kept, and the checksum is computed the moment the file is read
rather than after the fade and the level reload.

**Why.** D3D9 back buffers are not directly lockable; locking one crashed the game
whenever a save screen or the Escape menu was opened. Keeping the read length means a
save written before the struct grew is recognised before it is applied. The game loop
keeps running behind the menu and increments the level timer, so a load during Lara's
death animation failed with "corrupted savegame" on a file that was provably read in
full, and worked after a restart when no frames lay in between.

**How.** `specific/LoadSave.cpp`, `.h` (`CreateMonoScreen`) and the screenshot key path
in `specific/dxshell.cpp` (`DXShowFrame`) for the grab; `specific/gamemain.cpp`, `.h`
for the folder and the migration; `game/savegame.h` and `specific/LoadSave.h` for the
read length and the checksum point.

**Commits.** `b542ba0`, `2071d07`, `415db3e`, `6b40a1b`

### Sound effects on XAudio2
2026-07-30

**What.** The sound effect pipeline runs on XAudio2. Pooled, reusable
`IXAudio2SourceVoice` channels submit shared decoded PCM with `SubmitSourceBuffer`
instead of creating and destroying a duplicated DirectSound buffer per play.

**Why.** The effect path was the other half of the legacy DirectX stack being replaced,
and one decoded buffer can serve every voice that plays the sample, so the
duplicate-buffer-per-play model had nothing worth keeping.

**How.** `specific/dxsound.cpp`, `.h`. Volume and pan keep DirectSound's
hundredths-of-a-decibel convention at the call sites and are converted to XAudio2's
linear gain inside this file.

**Commits.** `8291574`

### Music and FMV sound off DirectSound
2026-08-17 - 2026-08-21

**What.** The CD-track music stream plays through one XAudio2 source voice fed by a
refill thread instead of a DirectSound ring buffer with position notifications; Bink FMV
audio goes through WaveOut instead of a raw `IDirectSound` object. Only the device layer
changed: the MS-ADPCM to PCM decoding, the looping seek, the end-of-file run-out and the
return to the level ambience are as they were. Volume keeps DirectSound's
hundredths-of-a-decibel convention at the call sites and is converted to XAudio2's linear
factor at the API boundary.

**Why.** Music and film sound were the last two playback paths still on DirectSound; one
audio path instead of two is the more robust delivery, and after them nothing in the game
itself used DirectSound any more.

**How.** `specific/audio.cpp`, `.h` hold `MusicVoice`, the `MusicVoiceCallback` that only
wakes the refill thread, and the queue that serves both the original tracks and the OGG
path; `specific/dxsound.cpp`, `.h`, `specific/gamemain.cpp` and `specific/winmain.cpp`
lose the DirectSound set-up and `App.dx.lpDS`; `specific/fmv.cpp` calls
`BinkSetSoundSystem(BinkOpenWaveOut, WAVE_MAPPER)`, which needs a device id rather than a
COM object and picks the same default device as the XAudio2 mastering voice.

**Commits.** `5d3c0f6`, `90f6d1d`

### Optional high quality sound bank from the player's remaster
2026-09-02 - 2026-09-21

**What.** Chronicles has no `MAIN.SFX` - that was TR4. From TR5 on the samples live in
the level files, so every `.TRC` carries its own bank of MS-ADPCM at 22050 Hz. The TR4-6
Remaster went back to one global `MAIN.SFX` of plain PCM at 44100 Hz. If the player puts
that file into `audio\`, the engine takes the sample data from it; without it nothing
changes at all.

**Why.** The remaster's `MAIN.SFX` is not the old material resampled: its spectrum runs
through 11025 Hz without the cliff a resampled signal always shows, so it is genuinely
better material and worth offering. Only files the player already owns are read, and
nothing is taken from the remaster's own programs.

**How.** `specific/hqsfx.cpp` and `specific/hqsfx.h`, both new. The bridge between a
level's own sample numbers and the bank's global ones runs over the sound ids, which
both sides index the same way. Only the audio data is swapped; the level's own tables
stay in charge, so the sounds imported from Tomb Raider III keep their original samples
and `MAX_SAMPLES` is untouched. Playback needed three changes in `specific/dxsound.cpp`:
a source sample rate per voice, read from the sample rather than assumed;
`DSAdjustPitch` reduced to the plain 16.16 ratio it had always been computing, clamped
to 100/22050 at the bottom and 4.5 at the top; and `MaxFrequencyRatio` lowered from 8.0
to 4.5 at `CreateSourceVoice`, because XAudio2 caps `MaxFrequencyRatio * source rate` at
`XAUDIO2_MAX_SAMPLE_RATE` (200000), so at 8.0 a voice could never be switched to 44100
Hz while 4.5 clears both rates (4.5 * 44100 = 198450) and costs nothing that was ever
reachable. Two unrelated holes were closed on the way: `LoadSamples` read samples into a
fixed buffer with no length check, and `DXCreateSample` did not check its `malloc`. The
bank writes `MODLOG bank ...` lines to the log and tests the log switch before it builds
a line.

**Commits.** `dbc0b30`, `e6fc7c4`

### HQ soundtracks in OGG
2026-08-22

**What.** If a file named after the track number lies in `audio\HQ`, it is played
instead of the original track; without those files nothing changes. Language-specific
sets follow the language selection.

**Why.** No music is shipped with this project - the player supplies his own files.

**How.** `specific/audio.cpp` looks the file up by track number and decodes it with
stb_vorbis by Sean Barrett, vendored unchanged as `tomb5/libs/stb/stb_vorbis.c` and
placed in the public domain by its own header, so it adds no licence obligation of its
own.

**Commits.** `d0c295d`, `b628107`, `d26dd1d`

### Real controller support
2026-07-31 - 2026-08-25

**What.** `DXUpdateJoystick` polls XInput and falls back to DirectInput8 for controllers
that are not XInput devices, so the remapping screen the game already had finally binds
something. Pause, Quicksave, Quickload and the small and large medipack hotkeys are
bindable and persist to the registry; the four tilt directions of the right stick are
bindable as extra buttons; buttons are named after the DualSense (Cross, Circle, ..., PS
Button); Circle acts as menu back and cancel alongside Escape; binding a button that
another action already holds unbinds that other action; the Joystick controls screen no
longer prints keyboard key hints; the Pause button also closes the pause menu, not only
Escape. Joystick enumeration runs on its own thread instead of on the startup path. The
*Control method* row offers Keyboard, PS5 Controller, Xbox Controller, generic Joystick
and Reset; the three controller settings pick only the button *vocabulary* (registry
`JoyStyle`, `-1` meaning automatic - an XInput pad found picks the Xbox page, otherwise
PS5), while the bit order the engine reads comes from `joystick_is_xinput`, set by each
poll. Back and confirm are asked for by role, not by a fixed bit. Circle also closes the
inventory ring, and the still-held button is no longer read as a roll on the frame after.

**Why.** `DXUpdateJoystick` was a stub returning zeros despite the complete remapping
screen, so no controller worked, and `Key()` shifted joystick button indices by the
wrong amount and was therefore dead code. `IDirectInput8::EnumDevices` can take ten
seconds when a controller is paired over Bluetooth but attached over USB, because
DirectInput waits for the absent paired device; the delay is a DirectInput timeout,
reproducible in a standalone program outside this code, and it left the game on a black
screen for that time. The right stick read the wrong DirectInput axes (Rx/Ry instead of
Z/Rz), and the D-pad produced no diagonals through the DirectInput fallback. A DualSense
reaches the engine raw over DirectInput but as an XInput device behind a wrapper, so one
fixed bit order can never name its buttons right; back sat hard on bit 2, which under
XInput is X, and the cutscene skip on bit 1, which is B. The inventory ring closed on
`IN_OPTION` alone, the inventory toggle, which a gameplay button cannot carry without
tearing the ring open on every roll, so the ring was the one screen without the way back
every other menu has.

**How.** `specific/input.cpp` holds the polling, the bindings, `Key()`, the binding
screen, `JoyStyle`, `ActiveJoyStyle()`, `JoyBitConfirm()` and `JoyBitBack()`;
`specific/dxshell.cpp` the enumeration thread; `game/newinv2.cpp` reads a left-over
deselect at the end of the frame, after the modes above the ring have consumed theirs;
`specific/LoadSave.cpp` and `specific/registry.cpp` carry the control-method row and
`JoyStyle`; `game/spotcam.cpp` and `game/control.cpp` ask by role.

**Commits.** `314e8eb`, `a465044`, `56b9508`, `a187566`, `0f6445b`, `321cba1`,
`95104be`, `b6a83f6`, `28db502`, `21d1513`, `4c62c33`, `3999fde`, `fb67fbb`, `05fe1b2`,
`9174a36`, `5f421b7`, `0bd9e6b`, `767de46`, `744ab38`, `8c05c2d`, `e24bb27`

### The setup dialog offers only what the engine still honours
2026-08-25

**What.** The 3D device list and the texture format list are gone and the device is a
label naming the renderer; the sound device list is gone. The resolution list holds one
entry per resolution rather than one per mode. The five registry indices (`DD`, `D3D`,
`VMode`, `TFormat`, `DS`) are clamped on read and written back as 0 where the control is
gone. Sound is switched on only by `DXDSCreate`, on success, and the voice creation
checks the engine pointer. `Dsound.lib` leaves the linker line. The dialog corner
carries the build date, the same stamp the log writes.

**Why.** Three controls had quietly died with the port: D3D9 enumerates exactly one
device per adapter, so the device and format lists offer one thing each, and the sound
device selection was never read at all, since XAudio2 always plays through the system's
default endpoint. Nothing checked the registry indices although they can outlive the
machine that wrote them - a saved `DD=2` reads past the adapter list once a monitor is
unplugged, and any `TFormat` above 0 from an older DirectX 3 build points past a
one-element array that `LoadTextures` walks three of in one expression. `SOUND_Init`
used to set `sound_active` unconditionally, so a machine without an audio device would
have turned sound on over a null XAudio2 and crashed on the first effect; it never
showed only because the dialog forced "Disable" whenever the DirectSound enumeration
found nothing. `EnumAdapterModes` reports each resolution once per refresh rate, so a
165 Hz screen produced seven identical "2560x1440" lines for one thing, the rate never
being used because device creation deliberately leaves `FullScreen_RefreshRateInHz` at
0. A `VMode` saved before this points at a different entry, is clamped, and re-picking
the resolution once settles it. `dsound.h` stays for `DSBPLAY_LOOPING`. The build stamp
lets a screenshot and a `log.txt` be matched up.

**How.** `specific/setupdlg.cpp`, `specific/registry.cpp`, `specific/dxshell.cpp`,
`game/sound.cpp`, `tomb5.rc`.

**Commits.** `5123d30`, `fe51ec8`

### Bounds checks, a log that survives a crash, a glyph guard, and the build
2026-07-30 - 2026-08-22

**What.** Fixed-size arrays that are filled from counts in the level file are
bounds-checked before they are written: the savegame buffer, the texture page array,
`items[]`, spot cameras, animating-texture ranges, the sound buffer pool (`DS_Buffers`,
raised from 256 to `MAX_SAMPLES`, 450), `BuildOutsideTable`'s stack array and the
per-frame queue. `Log()` writes millisecond
timestamps and flushes after every line. `PrintString` skips a character whose code lies
outside the font table instead of reading past it; what the font can show is shown.
Discord Rich Presence is compiled out behind `TOMB5_DISCORD_RPC` - the code stays in
place and one `#define` re-enables it - and the executable links the static C runtime
(`/MT`), no longer links `discord-rpc.lib`, and writes its output to the top-level
release folder.

**Why.** The bounds checks are one bug class: a level file longer than the array
silently overwrote whatever followed it. Upstream TR2's original 1996 code already had a
guard in its equivalent of `WriteSG`; TR5 either never had one or lost it in
decompilation, and the randomizer produces levels denser than the originals. `DS_Buffers`
was the narrowest place in the engine's own sound system: `sample_lut` has always been
laid out with `MAX_SAMPLES` and the level format's sound map holds the same 450 entries,
while the memory for the decoded sounds stayed at 256 - and a randomized level reaches
223 samples where the original reaches 218. Without the
flush the log was routinely empty after a crash, which made it useless exactly when it
was needed. `CharDef` holds 106 entries plus the accent codes 128 to 173, and the kana
indices of the Japanese PSX language file reach to 181. The Discord application id in
that call belongs to the upstream project and is not ours to use; the bundled
`discord-rpc.lib` is built against the dynamic C runtime, which prevented linking
statically, and with `/MT` the executable runs without the Visual C++ redistributable.
discord-rpc is therefore not part of the shipped binary.

**How.** `game/savegame.cpp`, `.h` (savegame buffer), `game/setup.cpp`
(`BuildOutsideTable`), `game/items.cpp` (per-frame queue), `specific/file.cpp` (the
level-load bounds checks), `specific/polyinsert.cpp` (the diagnostic drawtype logging
read `Textures[texture].bump` unconditionally, including for line-draw calls where
`texture` is outside the 0-63 range), `specific/function_stubs.cpp` (`Log()`),
`game/text.cpp` (`PrintString`), `specific/dxsound.cpp` (`DS_Buffers`),
`tomb5/tomb5.cpp` (the guarded Discord call) and
`tomb5.vcxproj` (runtime, output path, removed library).

**Commits.** `2071d07`, `5b678a4`, `744ab38`, `aaea134`, `668f453`, `bea5bf2`,
`974da28`

## 2.2 Engine support for the randomizer

These are the engine changes a rolled campaign needs. They fall into three kinds: limits
raised where the original sizes were built for hand-made levels, places where the engine
asked the level when it should have asked the item, and defects of the original that
only become visible once items, enemies, outfits and the level order move. One rule runs
through all of them: new behaviour hangs on marks that no vanilla level file carries,
and new object slots sit beyond the range any original level uses, so an unmodified
level plays byte-identically. Paths are relative to `TOMB5 Code/TOMB5/` unless a
randomizer path is given in full.

### Level identity separated from level slot
2026-08-22

**What.** `gfLevelId` is a second number, taken from the level's file name at load time,
and every level-specific branch in the engine asks it. `gfCurrentLevel` keeps its slot
meaning: which entry of `SCRIPT.DAT` is being played, for order, savegame and level
names.

**Why.** Upstream uses one number for two different questions, which is correct while
the two are the same, as in vanilla. As soon as the level order is shuffled they are
not, and every place where the engine does something only one particular level needs
fired in the wrong level: the Iris hazard, the gunship, the Area 51 laser, a shatter
sound table indexed per level, footprints, the puzzle-hole special case.

**How.** `game/gameflow.h`, `game/gameflow.cpp` derive and hold the identity;
`game/tower.cpp`, `game/objects.cpp`, `game/switch.cpp`, `game/footprnt.cpp`,
`game/health.cpp` and `game/traps.cpp` ask it.

**Commits.** `adfc0b5`

### Twice as many items per level
2026-08-02 - 2026-08-15

**What.** `MAX_ITEMS` is 512 instead of 256. The savegame buffer grows to 28980 bytes
because the per-item section scales with the item count. Buffers that depend on what a
level contains are sized from the level rather than from a fixed guess.

**Why.** The densest vanilla levels already hold 248 (rich2) and 244 (joby3) items,
which left almost no room for the randomizer's extra enemies and scattered weapons.
Saves from older builds are cleanly refused by the existing short-read detection.

**How.** `global/types.h` holds the constant and the savegame buffer; `items[]` is
allocated from the `game_malloc` pool with it and the free list is parametric. Triggers
address items through a 10-bit field (`items[trigger & 0x3FF]`), so 512 stays fully
reachable. The `ItemNewRooms` deferred queue and its kill flag `0x8000`, which tolerates
indices up to `0x7FFF`, derive from `MAX_ITEMS` too, including the extern declaration in
`game/control.h`; `game/control.cpp`, `game/init.cpp` and `game/items.cpp` carry the
headroom and the guards.

**Commits.** `2071d07`, `5bc67fc`

### Sixteen AI enemies active at once
2026-08-15

**What.** `MAX_LOT` is 16 instead of 5, and Lara's target arrays are sized from it
through the new `MAX_TARGETS (MAX_LOT + 1)`. The `game_malloc` pool is raised from 15 to
20 MB (`MALLOC_SIZE` 20000000). The eviction path remains as the fallback for the
seventeenth enemy.

**Why.** Upstream allowed five AI enemies to be active at once; the sixth triggered
enemy evicted the one farthest from the camera and froze it as `ITEM_INVISIBLE`. With 10
to 20 extra enemies placed per level, frozen bystanders were routine. The pool is raised
not for the slots but because a randomized level carries the imported enemies' meshes,
animations and frames and so runs several times the size of a vanilla one. Lara's target
acquisition collected up to `MAX_LOT` targets plus a terminator into fixed
`TargetList[8]`/`LastTargets[8]` arrays, correct for five slots and a buffer overflow
from eight active enemies up.

**How.** `global/types.h` holds `MAX_LOT`, `MAX_TARGETS` and `MALLOC_SIZE`;
`game/larafire.cpp` derives both arrays and their loop bounds from `MAX_TARGETS`;
`game/lot.cpp` allocates per slot a `CREATURE_INFO` plus `num_boxes x BOX_NODE` from the
`game_malloc` pool, roughly 25 KB per slot in the largest level. Savegames store
creature state per item and re-acquire slots on load, so there is no compatibility
impact.

**Commits.** `8ff9579`

### Engine-only object slots beyond the original 460
2026-08-15 - 2026-09-04

**What.** `SLOT_FILE_BOUNDARY` is 460 and the loader uses it instead of the enum
constant `NUMBER_OBJECTS` when splitting static-object sprite slots from moveable slots;
`LoadObjects` skips, and logs, a moveable record whose slot this build does not know. On
that footing the enum carries slots no original level uses: `BLUE_GUARD2` (460), the
second guard model, a full member of the soldier family; `ROMAN_GOD2` (461), the hammer
version of the Roman god, so that both of his versions can stand in one level with the
weapon decided by the item's OCB (`trigger_flags` 1 = sword, 0 = hammer) and the model
by the slot; `MESHSWAP_CROSS_SWAT` (462), `_SWATPLUS` (463), `_TWOGUN` (464), `_GUARD`
(465), `_MAFIA` (466), `_GUARD2` (467), `_HITMAN` (468) and `_CRANEGUY` (470) as mesh
warehouses, one per enemy, beside the four that alias `SMASH_OBJECT5..8`
(`MESHSWAP_CROSS_GLADIATOR`, `_IMP`, `_ROMANGOD`, `_ROMANGOD2`). Enemies of the imported
pool take their swap meshes through `MeshSwapSource()`, which prefers the designated
fallback slot and otherwise behaves exactly like upstream.

**Why.** The level file format encodes static-object sprite slots as `460 + static
number`, 460 being the value `NUMBER_OBJECTS` had when the original levels were built;
`LoadSprites` used the enum constant for that split, which silently breaks every
existing level the moment a new moveable slot is appended, and an unknown slot wrote
past the end of `objects[]`. `MESHSWAP1` holds, per level, the swap meshes of that
level's own enemies - the soldier family's gun hands 10/13, the Imp's alternate head 10,
Two Gun's source mesh 2, the Gladiator's and the Roman God's full damage sets - so an
imported enemy needs the set of a level that actually carries him, and that cannot go
into `MESHSWAP1` without breaking the target level's native users. Vanilla carries two
different guard models in the same slot 37, rich levels one and joby levels the other,
and both Roman gods share slot 61, so in each case the two can never coexist. With the
enum unchanged both loader changes are no-ops: 460 equals `NUMBER_OBJECTS` there, and no
vanilla level carries unknown slots; the fallback slots are loaded by no vanilla level
and are reached by the engine only through `item->object_number` range checks, so
without items of those types they are inert.

**How.** `game/objects.h` defines `SLOT_FILE_BOUNDARY` and the slots,
`specific/file.cpp` uses the constant in `LoadSprites` and `LoadObjects`,
`game/setup.cpp` wires `BaddyObjects` through `MeshSwapSource()`. Grouping is by
swap-set donor, which can differ from the model donor: rich1, the model source for the
Guard and Two Gun, carries no `MESHSWAP1` at all, because its enemies never set their
swap bits in vanilla. The slots need no `_MIP` twin, since distance models are off for
enemies; the cyborg's own slot 469 belongs to its own entry below.

**Commits.** `ac086d2`, `8bf7ddc`, `a6c8c49`, `0723c68`, `4daf4f7`, `9daedbb`

### Distance models switched off for enemies
2026-08-17

**What.** `DrawAnimatingItem` no longer draws an enemy's distance model (MIP/LOD). Doors
and bridges (mip 2048-4096) keep theirs.

**Why.** The routine took the slot after the enemy as soon as anything at all was loaded
there. That is safe in an original level, where level and enemies were built together,
but the randomizer places enemies into levels whose MIP slot holds something unrelated,
and the enemy is then drawn as that foreign model with its own bones and frame; 51 such
combinations are reachable across the thirteen levels. Almost nothing is lost: the game
has exactly seven genuine MIP cases, SWAT, SWAT+, Guard and Two Gun in the three rich
levels, all with the same mesh count as the full model. It also removes the need to
reserve a `_MIP` twin for every new enemy slot.

**How.** `game/draw.cpp`; `intelligent` separates the two groups cleanly, because every
enemy with an `object_mip` sets it and the scenery does not.

**Commits.** `02d1e15`

### An enemy's animations come from its own slot
2026-08-15 - 2026-09-04

**What.** Four places that picked one globally chosen animation base read
`objects[item->object_number].anim_index` per item instead. `SasControl` takes the
soldier family's vault and special animations from its own object entry; `ResetGuards`,
the body-bag trigger, gives each thinking item its own set; `HitmanControl` reads
`item->object_number` in the twelve places it reached for `objects[HITMAN].anim_index`;
the flipeffect wake-up in `effects.cpp` uses the second guard slot as the last anchor of
the same alias chain.

**Why.** In vanilla no level loads two members of the soldier family at once, so a
global pick is correct there; with a mixed enemy pool they coexist, and the loser of the
pick indexed into the other one's animations. `ResetGuards` wrote SWAT's index if
loaded, else the mini sub's, else the Guard's, into every thinking item - almost
harmless in the four vanilla levels that carry a body bag, where what stands in them is
soldier family, but with a mixed pool it hands a hydra or a dinosaur an absolute
animation number out of a foreign slot, which is the wrong frame data over the wrong
bone tree. A family member left without an anchor falls through to animation 0 and
becomes invisible and invulnerable while its shadow and its damage remain. Two vanilla
resets change, both broken before: in Andy2 no anchor is loaded at all, so the chain
gave the skeletons animation 0, which is Lara's first animation, and in joby3 Mafia 2,
who carries 58 animations of his own, got the Guard's. The stub soldiers resolve to
exactly the same number as before, because `setup.cpp` already aliases their
`anim_index` onto that same anchor.

**How.** `game/sas.cpp` (the index from the item's own object entry, the set
`InitialiseObjects` already aliased for that type), `game/control.cpp` (`ResetGuards`;
the forced rest state is left as written, Two Gun keeps his `+6` because his `+0` is a
walk, and the chef, the maze monster and the will-o'-wisp asked for their own set
already), `game/hitman.cpp`, `game/effects.cpp`, `game/setup.cpp` (the alias chain, with
the guard on slot 460 as the family base), `game/romangod.cpp` (`RomangodControl`
fetches its animations via `item->object_number`, which is what lets both Roman gods
coexist).

**Commits.** `329cce6`, `ade9077`, `66a866f`, `8bf7ddc`, `a9b4aab`, `a762fde`, `9daedbb`

### Enemies that could not die, and corpses that never left
2026-08-14 - 2026-08-29

**What.** Larson's corpse fades out like every other enemy: when his death animation
reaches its last frame the routine sets `after_death = 1` beside the heavy-trigger probe
it already ran. Pierre's death animation can no longer restart itself - the routine
holds it one frame short of its end and sets `after_death = 1`. Larson's and Pierre's
40-hit-point flee only engages when a matching escape marker actually exists, checked by
the new `PierreCanFlee()`. The chef, the skeletons and the swamp witch can be killed:
the chef borrows the soldier family's gunshot death, the other two shred. The Imp and
the Maze Monster shred as well, through `CreatureDie(item, 1)` into `ExplodingDeath2`.

**Why.** All of these rest on assumptions the randomizer breaks. Larson's death
animation carries no `KillItem` command, because the only place his body is ever removed
is Trajan's Markets, where a cutscene takes over at exactly that frame, and everywhere
else he never dies at all, since he flees below 40 hit points; his death animation is a
207-frame set piece that ends upright, so without the fade he would stand there dead for
good. Pierre's `+12` death animation jumps back into a living animation when it ends,
because Core never uses his generic death - in vanilla he dies by cutscene. Core places
the escape marker in every vanilla level that needs it; as pool enemies both can land in
a level without one, and then `FindAITargetObject` never finds a target, they chase Lara
around fleeing forever, and the hit-point clamp makes them immortal. The chef, the
skeletons and the swamp witch have hit points and take damage but their control
functions never read `hit_points`; in vanilla it never shows, because the chef's level
takes Lara's weapons away beforehand, the swamp witch only wakes when the level is as
good as over, and the skeletons' level hands Lara nothing to shoot them with. Trajan's
Markets is unaffected by the Larson fade, because the cutscene hides him in the same
instant.

**How.** `game/pierre.cpp` holds all three Larson/Pierre changes; `PierreCanFlee()` runs
the same type/OCB/zone match as `FindAITargetObject` before the clamp, so with a
reachable marker everything behaves exactly as vanilla and without one they fight to a
normal death. `game/chef.cpp` borrows the soldier family's gunshot death - his skeleton
is bone-identical with theirs and his own death animation is a one-frame stub - and OCB
1 switches him from kitchen work to hunting. `game/skeleton.cpp`, `game/swampy.cpp`,
`game/imp.cpp` and `game/willwisp.cpp` use `CreatureDie(item, 1)` into
`ExplodingDeath2`: the scraps are the enemy's own meshes thrown as `BODY_PART` effects,
so no extra model is needed, and `CreatureDie` also does everything a death animation
would - `KillItem`, dropped loot, AI off, the invisible flag for the savegame. For the
Maze Monster this replaces a stopgap, because Core never finished that death and the
lying pose sinks into the floor. The fade in `game/draw.cpp` is what `after_death`
drives.

**Commits.** `1bb6ca4`, `2f1ae8c`, `6204552`, `4f9721e`, `e898447`, `0da67d2`,
`c4ca75e`, `ebc7fe2`, `d749ade`, `4b4fa8e`, `a762fde`

### The VCI cyborg as a pool enemy on a slot of his own
2026-09-04

**What.** A second cyborg slot, `CYBORG` (469), carries the same model and the same
control routine as slot 67 but is mortal: `save_mesh` for its eleven damage panels, its
own hit-point and damage lines in `Balance.txt` (shipped at 180 health and a 50-point
shot), the panels opening with the health it loses, and a death of its own - the
electrocution animations, all panels torn open, and after five seconds `CreatureDie`
plus `after_death`, so the body fades and the AI slot is released. Slot 67 is left byte
for byte as it was. The cyborg is excluded from `GetCarriedItems`. Two further slots
come with it: `MESHSWAP_CROSS_HITMAN` (468) as his swap warehouse and
`MESHSWAP_CROSS_CRANEGUY` (470), which closes the last raw `MESHSWAP1` read left in the
soldier family.

**Why.** Core marks slot 67 `undead`, so no weapon can reduce its hit points at all -
`larafire.cpp` returns before the damage - and its only two deaths are Red Alert's own:
the gas, keyed off a room flag that means something entirely different in the other
fifteen levels, and the water short circuit, whose animation chain carries no `KillItem`
at all. That last one is deliberate there, because the electrocuted body has to stay
lying in the corridor and keep the water lethal, which is the puzzle. Dropped into a
random level the same three properties are a defect each: an enemy nothing can hurt, an
enemy that dies of nothing, and a corpse that never fades and holds its AI slot for the
rest of the level. The exclusion from `GetCarriedItems` keeps him from swallowing a
pickup lying beside him.

**How.** `game/objects.h` (the three slots), `game/setup.cpp`, `game/hitman.cpp`,
`game/control.cpp`, `game/lot.cpp`, `game/balance.cpp`, `game/balance.h`.

**Commits.** `9daedbb`, `5e087f7`, `70e96d4`, `5abd426`

### Balance.txt: every weapon, enemy, attack and hazard settable
2026-08-02 - 2026-09-04

**What.** A plain-text settings file, `Balance.txt`, is read once at startup from the
folder the game runs in. It carries an individual value for each of Lara's weapons, the
hit points of each enemy, each separate enemy attack, each of five fixed level hazards,
the audible range of the stationary level ambience (`SceneryRange`, shipped at 65, the
original being 100), and three percentages that scale everything at once:
`WeaponDamage`, `EnemyHealth` and `EnemyDamage`, each 100 by default. If the file is
absent the game writes it out with its defaults and an explanation of every line; a
missing or unreadable line keeps the built-in value; settings a newer version has added
are filled in. Every value is clamped on read, and the weapon percentage is capped at
127 because the engine holds weapon damage in a `char`. Two hit-point figures are
changed in the code itself: the Maze Monster from 4000 to 120 and Two Gun from 24 to 90.
The Roman God's armour stages, fixed at 225/150/75 out of 300 upstream, are computed as
three quarters, one half and one quarter of the object's own maximum.

**Why.** The randomizer needs one place from which enemy and weapon strength can be
scaled, and a single slider for all of Lara's weapons that does not overwrite per-weapon
values a player set by hand. Upstream built the Maze Monster effectively unkillable,
because in his home level he is a chase set piece and not a combat target, and as a pool
enemy he must die. The Roman God's stages are computed so that they still line up when
the file scales his hit points; with the original hit points the numbers are identical
to vanilla. Falls, fire, poison, drowning and the trap code deliberately bypass all of
it and stay exactly as hard as the original. The five hazards - the autogun, the
gunship, the Iris, the Area 51 laser and the electrical cables in the submarine pen -
are settable individually but are not enemies and are left out of the percentages,
because they never move between levels.

**How.** `game/balance.cpp` and `game/balance.h` are new. One table in `balance.cpp` is
the single source of truth: it holds the shipped default, a second figure that is what
`setup.cpp` writes, the allowed range, the explanatory text and, for hit points, the
object slot; the template file is generated from that same table, so the two cannot
drift, and `BalanceSetEnemies` logs a warning where the code and the table disagree.
`game/setup.cpp` calls `BalanceSetWeapons` at level start, the point where vanilla
already re-set the revolver's damage per level, and `BalanceSetEnemies` once in
`InitialiseObjects` after the three setup routines have written the originals; that
function writes the per-enemy hit points, then applies the percentage to everything
intelligent and damageable, so Lara and the flare keep theirs. `game/lara1gun.cpp` takes
the HK's two values, which vanilla rewrites on every shot, and applies `WeaponDamage()`.
`game/sound.cpp` reads the ambience range that used to be a compile-time constant.
`game/hitman.cpp`, `game/mafia2.cpp`, `game/pierre.cpp`, `game/sniper.cpp` and
`game/sas.cpp` pass their own gunshot damage into `ShotLara` instead of a literal. The
per-attack values are applied through `EnemyDamage()` at the 28 places in 20 enemy files
that subtract from Lara's hit points - `game/dog.cpp`, `game/huskie.cpp`,
`game/lion.cpp`, `game/bat.cpp`, `game/spider.cpp`, `game/people.cpp`,
`game/twogun.cpp`, `game/gladiatr.cpp`, `game/joby.cpp`, `game/hydra.cpp`,
`game/minisub.cpp`, `game/autogun.cpp`, `game/tower.cpp`, `game/chef.cpp`,
`game/skeleton.cpp`, `game/swampy.cpp` and the rest - which is also where the global
percentage is applied. `game/romangod.cpp` computes the armour stages. The wiring sits
where the engine really decides these numbers rather than in one patched-over table,
because three of the values in `weapons[9]` are dead.

**Commits.** `d2b6be7`, `1a66f5b`, `811469a`, `cc54867`, `119a30a`, `8a80b93`,
`ebc7fe2`, `5abd426`, `df542ff`

### Lara's meshes follow the outfit, not the level
2026-08-12 - 2026-08-26

**What.** Five places that upstream keys on the level range key on what Lara is actually
wearing. The child-Lara torso swap and the young-Lara left-hand mesh correction
additionally require `GF_YOUNGLARA`. The Russia braid anchoring (torso sphere shifted by
-11/-16/+25, fixed radius 94) and the braid-root welding table engage for the Russia
outfit, marked with `LaraType` 2 (`LARA_BUNHEAD`), or for an unmodified Russia level
(`LARA_NORMAL`). A new draw type `LARA_PONYTAIL`, `LaraType` byte 5, draws exactly like
`LARA_NORMAL` and exists for the standard outfit worn in a Russia level. The catsuit
draw path draws the thigh holsters like the standard one, but only when the holster
objects (slots 13-16) are loaded. A skin joint whose seam pairing found no vertex to sew
is left undrawn.

**Why.** The randomizer moves outfits between levels, and upstream could conflate outfit
and level because in vanilla an outfit never leaves its section. Without these, clearing
`GF_YOUNGLARA` to play the Ireland levels with the adult Lara left her with the child's
chest and a bent hand triangle; the braid pierced the body or collapsed into a spike
when the Russia outfit travelled; and a joint that matched nothing could only put
geometry where it does not belong. No vanilla file carries `LaraType` 2 or 5 and vanilla
Russia levels are `LARA_NORMAL`, so vanilla behaviour is unchanged. The `loaded` guard
on the holsters also prevents drawing a stray mesh through `objects[].mesh_index == 0`.

**How.** `game/laramisc.cpp` (`LaraInitialiseMeshes`, the torso swap),
`specific/drawroom.cpp` (`ProcessMeshData`, triangle 30 of `LARA_SKIN`'s left hand),
`game/hair.cpp` (the anchoring), `game/laraskin.cpp` (`HairSkinVertNums[5]` versus
`[0]`, and `SkinJointUsable`), `global/types.h` and `specific/drawlara.cpp`
(`LARA_PONYTAIL`, the `DrawLara__4` holsters), `game/delstuff.cpp`, `game/delstuff.h`
(`SkinJointUsable`, asked by the three joint loops in `specific/drawlara.cpp`).

**Commits.** `e423913`, `1bb6ca4`, `fb12eb4`

### Cutscene mesh swaps no longer outlive the cutscene
2026-08-15

**What.** `CutseqLaraMeshesBack()` resets all 15 of `lara.mesh_ptrs` from slot 0 at the
single place in `handle_cutseq_triggering` where every cutscene teardown passes, beside
Core's own holster restore. `handle_lara_chatting` skips the speech-head install when
the new `GfForeignSpeechHeads` flag is set.

**Why.** Cutscenes graft vanilla-outfit meshes onto Lara at run time:
`handle_lara_chatting` swaps her head against `LARA_SPEECH_HEAD1-4` during dialog
passages, and the joby5 suit exit swaps hips and legs. The swap-back only happened when
the respective table ran past its terminator, so skipping the cutscene mid-passage, or a
passage reaching the final frame, left the last swap state installed into normal
gameplay until the next weapon draw reset the head. This is an original-game defect that
vanilla masks, because there the grafted meshes match the level outfit. The flag exists
because a donor outfit may have no speech heads of its own - the VCI levels know no
dialog cutscenes - in which case installing them would graft a foreign head.

**How.** `game/deltapak.cpp`, `game/lara.cpp`, `game/lara.h`. Slot 0 is primed with the
current skin by `LaraInitialiseMeshes`, the child torso from `ANIMATING6_MIP` as there.
The flag comes from `data[2]` of the 32-byte block before the main level chunk, read in
`LoadLevel` (`specific/file.cpp`) next to LaraType and WeatherType; all 16 vanilla files
carry 0 there, so unpatched levels behave byte-identically. The randomizer sets it when
an outfit donor has no speech heads and otherwise imports the donor's, so dialog mouths
keep animating. A weapon or torch held in hand across a cutscene is reset to the skin
hand too; the weapon state machine reinstalls it on the next draw.

**Commits.** `215ddfb`

### Room for a full inventory reset at every level start
2026-08-22

**What.** `gfPickups` and `gfTakeaways` hold 128 script entries each instead of 16, and a
`GIVEOBJ` that stands in the block behind a `TAKEOBJ` is applied after the takeaways in a
third phase rather than being swallowed by them. Both arrays are bounds-checked on fill;
previously `gfPickups` was written unchecked into sixteen entries.

**Why.** A randomized campaign clears the whole key, puzzle and pickup family at every
level start - about 74 `TAKEOBJ` lines per level - so that no key or puzzle item can
travel into a level that holds a different object in the same generic slot. Sixteen
entries could not hold that. The counters are `uchar`, so 128 costs nothing. The third
phase is needed because the takeaways deliberately run last in `setup.cpp`, where they
also remove the pistols that every level with a loaded pistol model presses back into
Lara's hands; without it a deliberate gift after a clear-out would be taken away again.
Vanilla puts all gifts before all takeaways, so nothing changes there.

**How.** `game/gameflow.cpp`, `game/gameflow.h` (`gfPickups[128]`, `gfTakeaways[128]`,
`gfLateGives`), applied in `game/setup.cpp`.

**Commits.** `3682b75`

### Keys are counted, not flagged
2026-09-04

**What.** `LARA_INFO::keyitems` is an eight-byte counter array, read and written exactly
like `puzzleitems`: a lock consumes one copy, a script `TAKEOBJ` clears the slot, and
the inventory ring shows the number from two copies upwards. Savegames written before
this are refused.

**Why.** The field was a 16-bit bitfield with one bit per `KEY_ITEM1..8`, while puzzle
items had always been counters. Picking up a key ORed the bit in and the pickup was
removed either way, so a second key of the same type was silently destroyed - vanilla
joby2 hands out two Key 7 for two separate locks, and a randomized layout can put both
of them in front of the first door. The change alters `sizeof(SAVEGAME_INFO)`, which is
serialized raw, so old saves are refused by the existing size and checksum check in
`sgRestoreGame()` rather than decoded from a shifted layout. `keyitemscombo`, the halves
of a combinable key, is genuinely unique and stays a flag field.

**How.** `global/types.h` (the field), `game/newinv2.cpp` (pickup, display,
consumption), `game/setup.cpp`.

**Commits.** `67c0e77`

### Relocated pickups: a dropped wall pickup and the pushable block
2026-08-20

**What.** When an enemy carrying a wall pickup dies, the low OCB bits (2 and 9) are
cleared on the drop, so it lands as an ordinary floor pickup; the pickup trigger's own
bits are untouched. A pushable block rolls over pickups again: the collision test asks
what is in the way, and pickups let the block through and are there again once it moves
on, while enemies, statues and a second block still stop it. The next entry keeps a wall
pickup out of every carried chain in the first place; the clearing stays for a chain a
savegame restores.

**Why.** OCB 2 and 9 mark the pickups Lara pulls off a wall, and that manner demands a
wall behind the item and her angle onto it; `DropBaddyPickups` puts the item on open
floor, where the condition can never be met again and the item is lost for good. TR5
refuses a push as soon as `GetCollidedObjects` reports anything at all on the target
tile, a medipack included - TR1 to TR4 had no such general test - so with items
relocated a stray medipack could block the only push path to a puzzle.

**How.** `game/box.cpp` (`DropBaddyPickups`), `game/moveblok.cpp`.

**Commits.** `bbe4c47`

### An enemy never carries a wall grab
2026-09-26

**What.** `GetCarriedItems` hands every enemy and every searchable object the pickups
within half a sector of it at level start and takes them out of their room; a wall grab,
a pickup whose OCB (`trigger_flags & 0x3F`) is 2 or 9, is not among them any more. It
stays on its wall as the level placed it. Every other pickup in reach is carried as
before. With `-log` the level start writes one `CARRYLOG` line for every pickup in a
carrier's reach.

**Why.** A wall grab an enemy takes off its wall is lost either way. The drop of the entry
above makes it a floor pickup in the engine's bookkeeping only: the item keeps the model
made for the wall and lies where its carrier fell, and in the game it was then either gone
or on the floor at a wrong place, and could not be picked up. A searchable object hands
its chain to Lara herself when she searches it (`game/pickup.cpp:1128-1143`), so a wall
grab it carried was not lost; it, too, now leaves the grab on its wall. Both manners
measure Lara against the item's origin on the wall (`game/pickup.cpp:52-53`, `:65-66`):
OCB 2 is pried off with the crowbar (X -256..256, Y -100..100, Z 200..512, 30 degrees
either way, Lara put at 0,0,215), OCB 9 is pulled out by hand (X -512..0, Z 0..512, Lara
put at -224,0,240). The item on the wall is no stand-in for another pickup: it is the slot
the inventory receives, and in the original levels it is the only item of its slot in its
level, so no floor pickup of the same type exists there (`ItemCheck` over slots 172, 174
and 220). The six are Andrea2 item 121, `PUZZLE_ITEM3` (slot 174), the golden coin;
Andrea2 item 128, `PUZZLE_ITEM1` (slot 172), the valve wheel; Andrea3 item 68,
`PICKUP_ITEM1` (slot 220), the philosopher's stone; andy3 item 26, `PUZZLE_ITEM1`, the
chalk - these four with OCB 2 - and andy3 item 6, `PUZZLE_ITEM3`, and joby3 item 41,
`PUZZLE_ITEM1`, both with OCB 9, the two crowbars: `AddDisplayPickup` turns those two
slots into `CROWBAR_ITEM` in Old Mill and Submarine (`game/health.cpp:270`). The names
come from the object commands of the original `SCRIPT.DAT` (`ScriptCheck`) and the text
list in `game/gameflow.h`; andy3 names no `PUZZLE3` and joby3 no `PUZZLE1`. Picked up, a
puzzle item adds one to `lara.puzzleitems`, `PICKUP_ITEM1` sets bit 0 of
`lara.pickupitems`, and a crowbar sets `lara.crowbar` (`game/newinv2.cpp:3136`, `:3167`,
`:3175`). Items 121 and 68 carry bit 0x40 (raw OCB 66): the pickup leaves them in place,
invisible and triggered, for the pickup trigger (`game/pickup.cpp:1035`); what that
trigger fires in Andrea2 and Andrea3 is not measured. The other four are removed.

**How.** `game/setup.cpp:2300-2317`, in `GetCarriedItems`, at the place a pickup in reach
is chosen: the OCB is read (`:2300`) and a pickup with OCB 2 or 9 is not linked into the
carrier's chain (`:2311`); its room link, its position and every other field stay as the
level loaded them. The seam (`:2305`) goes through `Log`, which writes nothing without
`-log`, and runs once per level start, never per tick:

    CARRYLOG baddy=<item>/<slot> item=<item>/<slot> ocb=<ocb> d=<dx>,<dz> <verdict>

`<verdict>` is `carried` or `skipped wall grab`, and `d` is the item's position less the
carrier's. The slot names come from `HDSlotName` (`specific/hd/hd_bind.cpp`, a plain
table that is linked into every build and needs no HD state; `game/setup.cpp:84` includes
its header). A wall grab in reach of two carriers writes a line for each. The comment of
`DropBaddyPickups` (`game/box.cpp:857-865`) names the case its OCB clearing now serves.

**Checked.** A probe (`CarryProbe`, kept outside the repository) replays `GetCarriedItems`
over level files, with the reach as `game/setup.cpp:2279-2289` states it: a carrier is a
slot with `intelligent` set (the 32 slots `game/setup.cpp` marks) or `SEARCH_OBJECT1..3`,
never `CYBORG`; it looks only at the items linked into its own room; an item is in reach
at |dx| < 512, |dy| < 256 and |dz| < 512 against the carrier and when its collision is
`PickUpCollision` (`PUZZLE_ITEM1..BURNING_TORCH_ITEM`, slots 172-241,
`PISTOLS_ITEM..FLARE_INV_ITEM`, 334-355 without `CROSSBOW_BOLT` 344, and `FLARE_ITEM`
354); the height of a pickup is the one `InitialisePickup` leaves (`game/init.cpp:489`).
It ran over the 16 files in the game's `data/Backup`, each equal in size and SHA-256 to
its line in `Preparation/Level Infos/VanillaLevelChecksums.csv`. It finds six wall grabs,
the six above, and no carrier in the room of any of them, so no vanilla level start
changes. The nearest carrier in any room: Andrea2 121, `ROMAN_GOD` item 108, 27285 units;
Andrea2 128, the same, 15981; Andrea3 68, `ROMAN_GOD` item 79, 10370; andy3 6, `IMP` item
3 in room 11, 2944 units straight below the item in room 13; andy3 26, `IMP` item 2, 5146;
joby3 41, `MAFIA` item 188, 5843. A level start of the originals writes `CARRYLOG` lines
for 59 carried pickups, all with OCB 0 and all at d=0,0 (Andrea1 5, Andrea3 1, andy1 1,
joby2 11, joby3 10, joby5 10, rich1 13, rich2 6, rich3 2) and no `skipped wall grab` line;
the lines are derived by the probe, not read from a game log. The savegame: `carried_item`
is written and read for every carrier (`game/savegame.cpp:447-448`, `:872-873`), and a
load runs `SetupGame`, and with it `GetCarriedItems`, before `sgRestoreGame`
(`game/gameflow.cpp:1015`, `:1028`). A save written while a wall grab was carried
therefore restores the chain: the item stands on its wall and in the chain at once, and
the drop's OCB clearing is what the carrier's death meets. What happens when Lara takes
such an item off its wall before the carrier dies is not measured. Not handled either: a
save in which the wall grab was the first item of a carrier's chain with another pickup
behind it - on load the new build carries the other pickup, the restored head then makes
the chain the wall grab alone, and the other pickup is in no chain and in no room. Only a
hand-written list line beside a triggered carrier could have produced such a save.
ModernTest 10783, MenuTest 35547 and InterpTest 635 checks, 0 failed; LipsTest 270 with
the `.OBJ` folder and 243 without, 0 failed - all as before. Not measured: the game.

**Unchanged.** The drop keeps its OCB clearing, as belt and braces for a restored chain
and for any path that still fills one. The randomizer's bar on wall grabs within a
carrier's reach stays: a spot under an enemy's feet is a poor place for an item, and the
engine's rule is the net under a placement, not its reason. Every other pickup in reach
is carried exactly as before, with `hd_enable` 0 and in Tank mode alike; the code of
`DropBaddyPickups`, `CreatureDie` and every pickup path is untouched, and no existing
log line changed.

**Decisions.** Items that are broken out of a wall are never taken along by enemies in
the first place, rather than repaired after the drop; the drop's position and room stay
as they are.

**Commits.** `700f4c5`, `2096a92`

### A secret bonus, and the reward read from a file
2026-08-03 - 2026-08-30

**What.** Finding every secret in a level awards items, in the manner of Tomb Raider II.
What it awards is read from `SecretRewards.txt` beside the executable, keyed by the
level's position in the campaign (`gfCurrentLevel`) rather than by its file name, with
one line for each of the fourteen levels that are really played. Only weapons,
ammunition and medipacks are accepted as rewards. If the file is absent it is written
out with the defaults on first start, and a line naming something else is skipped with a
message.

**Why.** The level order is itself rolled, so a key by file name would attach the reward
to the wrong level. A key or a puzzle item as a reward would open a door out of order.
The title screen and the three development levels have no secrets and get no line.

**How.** `game/secretreward.cpp`, `game/secretreward.h` read, validate and complete the
file (`SECRET_REWARD_FILE`); `game/newinv2.cpp` hands the reward out in
`GrantSecretBonus` at the point where the secret count becomes complete,
`game/savegame.cpp` carries the per-level state. The randomizer can write the file; a
player can edit it.

**Commits.** `05eb6c0`, `a762fde`, `df542ff`

### The stationary level ambience: a stacking guard and a range brake
2026-08-18 - 2026-08-22

**What.** Ambient scenery sound sources are marked as such while they are created, and
the stacking guard and the range brake in the sound code hang on that mark. The audible
range is the `SceneryRange` value in `Balance.txt` rather than a compile-time constant;
enemies and Lara always keep their full range.

**Why.** Without the mark the engine could not tell a scenery loop from any other sound,
and identical loops stacked on top of one another. The original let machines, fire and
dripping water carry through half the level.

**How.** `game/effects.cpp` sets the mark at creation; `game/sound.cpp`, `game/sound.h`
carry the guard and the range, `game/balance.cpp` supplies the value.

**Commits.** `beafe46`, `811469a`

### Two level-start freezes in dense levels
2026-08-15

**What.** `aInit()` (room vertex buffers) and `aMakeCutsceneResident()` (cutscene
textures) run on the main thread after the loader joins, instead of on the loading
thread. `S_FinishLoadBar()` forces the loading bar complete once the loader has joined,
and the bar's catch-up loop is bounded.

**Why.** Two threads used the one device while the main thread presented the loading
bar. Separately, the bar's completion test (`loadbar_maxpos <= loadbar_steps`, with
steps growing by `100/maxpos` per step and `maxpos` = texture pages + 20) becomes
mathematically unreachable at roughly 75 texture pages and up, leaving the catch-up loop
spinning forever with a full bar. Vanilla levels carry 9 to 17 pages and never reach it;
randomized levels with 79 and 82 pages hit it at once.

**How.** `specific/file.cpp` moves the two calls behind the loader join;
`specific/drawbars.cpp`, `specific/drawbars.h` hold `S_FinishLoadBar()` and the bounded
loop; `game/gameflow.cpp` and `game/control.cpp` carry the level-start milestones. Cheap
seam logs remain around the load hand-off and those milestones.

**Commits.** `4c896fc`, `ea6c325`, `57206bd`

### A door wrote through the previous level's AI slots
2026-08-25

**What.** `FreeLevel` clears `baddie_slots` where the pool dies, and `ShutThatDoor` runs
its slot loop only while the array belongs to the current level. `LoadItems` validates
the slot and the room number of every item as it reads them and stops with a message box
instead of a wild access, in the same manner as the existing `MAX_ITEMS` check.

**Why.** `FreeLevel` sets the `game_malloc` pool back to its start, which is the moment
every global pointer into it goes stale: the address from the level before now points
into the middle of the level about to be loaded. `baddie_slots` was read again before it
was reallocated, because `InitialiseLOTarray` runs after `S_LoadLevelFile` while
`ShutThatDoor`, called by `InitialiseDoor` during loading, walks all `MAX_LOT` slots and
writes to each. Skipping that loop during loading loses nothing, since it drops the path
target of creatures that are already thinking, and at load time none are. This is an
upstream defect that only became fatal with levels dense enough for the stray writes to
hit live data.

**How.** `specific/file.cpp` (`FreeLevel`, `LoadItems`), `game/door.cpp`
(`ShutThatDoor`). Behind `-log`, `LoadItems` also writes `ITEMLOG`, one line per item
before its `initialise`, so a crash during loading names the item, and `ITEMWATCH`, a
scan after each `initialise` that reports both an item whose room number became
impossible and the item being initialised when it happened; the scan is quadratic, is
therefore gated at the call site, and switches itself off after the first find.

**Commits.** `bc328a8`

### Lara blanked out in the half turn on a rope: the ROPELOG seam

**What.** When Lara turns half round on a rope she disappears for a moment. The defect
belongs to the base engine: it appears with the HD art off, at *Frame rate 30* and under
both control schemes. No fix exists yet, because the rope arithmetic does not produce it.
The squeezed body that led to this seam belongs to the turn on the tightrope, not to the
climbing rope of `game/rope.cpp` measured here; its cause and its fix are the next entry.
Behind `-log` two lines record every tick on a rope instead. `ROPELOG` gives her state,
animation and frame, her heading round the rope (`RopeY=`, 256 units per tick of a turn,
128 ticks for a half turn), the segment, the offset and the frame's root `y`. It also
gives the three lengths `Normalise` divides by (`len=`), and the angles and position
`AlignLaraToRope` gives her. `ROPELOG draw` is written on every drawn frame while she
hangs on a rope and on the frame after she lets go. It says whether the Lara block of
`DrawRooms` draws her (`drawn=`) and lists the terms that decide it: `mesh_bits`,
`SCNoDrawLara`, `ModernLaraHidden` and `IFL_INVISIBLE`. It also gives the camera type,
the eye and the look-at point with their rooms, the eye's distance to the look-at point
(`eyed=`) and to her (`larad=`), and the candidate `ChaseCamera` took (`cand=`, `clip=`).

**Why.** An offline probe compiles `AlignLaraToRope` and every function it calls
verbatim. On a still rope, all 65536 values of `RopeY` give continuous results.
`x_rot` and `z_rot` stay 0, `y_rot` moves at most 22 units per 1-unit step of the
heading, and the position moves by at most 2 units between neighbouring headings. No
input to `Normalise` comes near zero: the smallest are 191 for the rope's direction,
4094 for the turned facing and 16370 for their cross product. The same holds for all 21
segments with four root offsets (84 of 84 cases), and the half-turn step itself is
clean. The placement can only break down when the rope at her grip lies along the world
X axis, which takes a tilt past 90 degrees; a turn starts only from the plain hang,
which a swing returns to only below 6750 (about 37 degrees), and the probe finds no
degenerate step up to its 63-degree swings. The one integer defect the
probe does find is a different symptom. `Normalise` takes its reciprocal as `0x10000 /
length` in integers, and the cross product can leave it 16385 to 16455 long, where the
reciprocal comes out at 3 instead of 4. On a swinging rope `z_rot` then comes out up to
a quarter too large, up to 1539 units (8.5 degrees), a roll flicker that leaves the
position alone. The classic Lara draw clips nothing against her body as a whole, so a
wrong angle would tilt her rather than hide her. What hides her is therefore outside the
rope code. The chase camera closing in on her while her origin circles the rope at 112
units is the likeliest cause, but it is not measured, because it depends on the level
geometry round the rope. A blank-out tick shows one of three things: `drawn=0` in the
draw line; `eyed=` or `larad=` near zero, which points at the camera; or a jump in
`xyz=` or `pos=` in the tick line, which points at the rope.

**How.** `game/rope.cpp`: `RopeLog` and `RopeLogLength`. The lengths are taken where
`AlignLaraToRope` shifts each vector for `Normalise`, and only when `gLogEnabled` is
set; the line is written at the end of the function. `game/draw.cpp`: `RopeDrawLog`,
called from `DrawRooms` just before the Lara block. `game/camera.cpp` / `.h`: `chase_log`
is set to -1 at the top of every `CalculateCamera` pass and written by `ChaseCamera`,
and `ChaseCameraLogPick` reads it. Without `-log` nothing is computed or written, and
the arithmetic of `AlignLaraToRope` is unchanged.

**Commits.** `4d578e9`

### Lara facing the wrong way for one tick in the jump twists: the early half turn
2026-09-24

**What.** In the twist of the back jump (animation 212 into 213), in the two twists of
the forward jump (210 into 211, out of the standing jump's flight 77; 207 into 209, out
of the running jump's start 17 and 19) and at the end of the roll 47 (state `AS_ROLL`, 48
into the stand 11), Lara's whole body faced the other way for one tick and then turned
back. The defect
belongs to the base engine and is drawn at every frame rate: the four animations fire
FlipEffect 0 (`turn180_effect`, `y_rot -= 0x8000`, `x_rot` negated) on their last frame,
and the effect turns the item on the tick whose `frame_number` reaches that frame
(`AnimateLara`, `game/laramisc.cpp`), so that tick draws the frame under the turned yaw;
but the frame, a blend of two key frames made for the yaw before the effect, belongs to
the old yaw. That frame, and the tween frames into it, are now drawn from copies of the
two key frames turned by half a turn, which land exactly where the frames land under the
old yaw; the braid hangs off the same frames. Behind `-log` a `TURNLOG` line names every
tick that takes this path.

**Why.** Measured over the thirteen campaign levels with an offline probe that includes
`game/interpolate_math.h`: Lara's playable animations carry 77 `turn180_effect`
commands. 13 fire on the first frame of the roll 147, whose change turns the root (see
*The animation's root offset on a refused frame*); 12 fire on a key frame made for the
new yaw inside a key pair whose hips turn more than 90 degrees (the underwater roll 205
in eight levels, the tightrope turn 451 in four); and 52 fire on the last frame of 48,
207, 210 and 212 in every level, a frame whose key pair turns the hips by less than 90
degrees. In ANDREA1 the hips turn from the tick before to the flip frame by 179.3
degrees as drawn and 0.9 under the old yaw in 48, 176.8 and 5.3 in 207, 177.0 and 11.0
in 210, 171.9 and 9.7 in 212; the next animation's first frame follows the frame drawn
under the old yaw by 5.9 (11), 13.4 (209) and 19.4 (213) degrees. The owner's `-log`
session (build `Sep 24 2026 22:43:40`) shows it on four back twists: at tick 684 frame
5566 of 212 is drawn (`ARMLOG gc=684 lara=212/10`) while `MODERNLOG lara=` has gone from
-15317 to 17451, the hips' view-space x in `ARMLOG` jumps from 44 to -44 on that tick,
and the second's `INTERPLOG` reads `halfturn=6 root_flip=0`: the tweens into the flip
tick blended the pose under the live, turned yaw. His video of the same build
(`Saltos.mp4`, 60 frames a second at 165 Hz) shows a whole body seen from the front
between frames seen from behind in both back twists it holds, video frames 237 to 239
and 363 to 364, about one tick each, and in the forward twist two ticks unlike their
neighbours (frames 613 to 614 and 615 to 616); which of the two forward twists that
session played is not measured (the logged session played 210 and never 207). With the
frame turned, the back twist turns 9.7, 19.4 and then 24.6 to 24.8 degrees a tick, 207
turns 5.3 and 13.4, 48 0.9 and 5.9. It does not bridge 211's own start: 211's first
key lies half a turn about her lateral axis from 210's last frame, 179.2 degrees under
either yaw, a seam in the level's data, so the forward twist out of 77 keeps one tick of
179 degrees where it had two (177.0 and 179.7); the tween frames across that seam are
bridged (*The change after it, bridged*, below). TROne (GPLv3) by Todesengel1931 -
https://github.com/todesengel1931 - was read for comparison before this was built: its
guard (`TROneKeyframesFlipped`, `games/tr5/src/game/draw.cpp`, a root turn of more than
150 degrees between two key frames, then the first key unblended, in `CalcLaraMatrices`
and in `hair.cpp`) holds 209 (152.6 degrees) on its first key and leaves 213 (98.8) to
vanilla's blend, and nothing in its `draw.cpp`, `delstuff.cpp` or `hair.cpp` touches the
frame the effect fires on (no line names 207, 209, 212, 213, a twist, a quaternion or a
slerp). Nothing of its code is used.

**How.** `game/interpolate_math.h:1178` (`InterpTurnKind`): for the flip frame `f` the key
pair `k = (f - 1 - frame_base) / rate` and `k + 1` gives `INTERP_TURN_INSIDE` where the
hips turn more than 90 degrees inside it (`InterpKeyFar`), `INTERP_TURN_EARLY` where they
do not, `INTERP_TURN_NONE` on the animation's first frame. `game/interpolate.cpp:1945`
(`InterpTurnOf`) walks an animation's commands as `AnimateLara` does, once per animation,
into a 512-slot table indexed by the animation number and checked against it;
`InterpInvalidate` empties it (`game/interpolate.cpp:1589`), and `FreeLevel`
invalidates. `game/interpolate_math.h:1089` (`InterpKeyFrameTurn`): the offset turned as
`InterpRootFlip` turns it, node 0's y angle half a turn on (y is the outer angle of
`phd_RotYXZ`'s order, so the half turn adds exactly; a one-axis x or z hips node becomes
three-axis, rounded to a step of 64), the box's x and z extents turned, every other node
copied; exact where the item's `z_rot` is zero, whatever its `x_rot`. `game/draw.cpp:2826`
(`LaraPoseFrames`, called from `CalcLaraMatrices`, `game/delstuff.cpp:196`, for every
set, and from `HairControl`, `game/hair.cpp:309`): on the flip frame of an early
animation (`LaraTurnFrame`, `game/draw.cpp:2544`) both frames of the pair are replaced by
turned copies (:2845); the tween frames between the tick before and the flip tick stand
on the same frame number and take the same path. The change that follows is refused by
the pose rule, as every animation change is (its tween frames are bridged, below), and
its root blend turns the previous tick's offset:
`GetRootAt` reports `turned` (`game/draw.cpp:2715`), `InterpRootRefused` keeps it in
`g_rootTurned` (`game/interpolate.cpp:1708`) and `InterpRootNow` turns by `g_rootFlip ^
g_rootTurned` (:1732). `TURNLOG f= anim= frame= flip= keys= drawn=turned`
(`game/interpolate.cpp:1923`), view set only, once per tick. What it costs: a table
lookup per call of `LaraPoseFrames`, two turned copies on the 52 frames. At `fps_mode 0`
the four flip frames change and nothing else. Checked by
`Preparation/Tools/Diagnose/InterpTest.cpp` (`key_build`, :4084), blocks 6 and 7: the
turned offset and box, node 0's matrix with its x and z columns negated, a frame turned
twice is itself, the one-axis cases, and the three kinds with their pairs.

**The change after it, bridged** (2026-09-25). *What.* The tick after the flip tick
stands on another animation (11, 209, 211 or 213, key 0 in all 52 cases, measured over the
thirteen campaign levels), and the pose rule refuses every tween across an animation
change, so the frames between the two ticks wore the new tick's pose from the first of
them on. After 210 that pose is 211's first key, the seam above: in the owner's `-log`
session of build `Sep 25 2026 00:05:52` (JOBY2), `ARMLOG`'s joints out of
`lara_matrices` step 178.3, 179.0, 178.6 and 178.3 degrees in the HD body and 178.3 and
177.8 in the classic one from the last frame of the `TURNLOG` tick to the first of 211,
where a step between two frames at 165 Hz is about 3 degrees, and the first tick of 211
then stands still for 33 ms. Those tween frames are now drawn from a key frame built at
the frame's weight between the pose the flip tick drew and the pose of the current tick:
a fast turn over the in-between frames, a motion the animation does not carry and the
draw invents, there to be judged in game. At *Frame rate 60* that is one frame per
change (the halfway pose), at 165 Hz five or six, at 30 none. *How.*
`game/draw.cpp:2901` (`GetFramesNow`): after `GetFramesAt` refused, Lara's frame asks
`LaraBridgeFrames` (:2770); a bridged frame is counted as a pose (`InterpNotePose`) and
not as a skip, returns 2 like a blend in time, and raises the root latch as every refusal
does (:2911). It fires where `InterpBridgeFires` (`game/interpolate_math.h:1260`) holds -
`0 < t < 1`, `InterpTurnOf` of the previous tick's animation `INTERP_TURN_EARLY`, the
previous frame its flip frame, the animation changed - and not where the current tick
fires a `turn180_effect` itself (`game/draw.cpp:2782`; none of the 52 does). The first
pose is the previous tick's pair from `GetKeyAt` (:2683, the key arithmetic `GetRootAt`,
:2731, now calls), both keys turned by `InterpKeyFrameTurn` and, where the remainder is
not zero, built at the tick's factor by `InterpKeyFrameLerp`; the second is the current
tick's pair, built the same way where its remainder is not zero. The frame is
`InterpKeyFrameBridge` (`game/interpolate_math.h:1344`): the second pose's box and root
offset, every node by `InterpKeyNodeLerp` (:1033, the node step `InterpKeyFrameLerp`,
:1081, now calls), in a static buffer put into both slots. The hips take the longer way
round (`InterpSlerpRot` with `longArc`, :968) where `InterpBridgeLong` (:1312) finds the
two more than 90 degrees apart and the shorter way turning against the hips' own turn
(`InterpHipsTurn`, :1274, the quaternion of the turn the stack sees) both over the turned
key pair the flip frame lies on and over the pair the current tick stands on. The body
takes the frame through `CalcLaraMatrices` (`game/delstuff.cpp:184`; `LaraPoseFrames`,
:196, leaves two equal frames alone) into `Rich_CalcLaraMatrices_Interpolated` (:290),
whose `InterpRootNow` (:772) reads the bridge's offset - the current tick's own, which
the refused frame read before - so the root comes out unchanged; the classic body copies
`lara_matrices` (`specific/drawlara.cpp:159`) and the HD body reads them
(`specific/hd/hd_laradraw.cpp:893`, from `specific/drawlara.cpp:147`); neither the HD
switch nor the control scheme is read anywhere on the way. The braid does not take these
frames: `HairControl` runs on the tick (`game/control.cpp:662`, from `GetFrames`,
`game/hair.cpp:302`) and its links are blended as positions between the ticks
(`game/interpolate.cpp:913`, :1308), as before; only its joint at the head
(`game/hair.cpp:97`, `GetCorrectStashPoints`) follows the bridged head. Bounds
(`GetBoundsAccurate`, `game/draw.cpp:192`), the shadow (`game/delstuff.cpp:289`) and the
collision read the item's own frame. At `fps_mode 0` none of it is reached:
`InterpAlphaNow` answers 1 (`game/interpolate.cpp:1009`), `InterpBeginFrame` returns with
`g_applied` false (:1170, :1202), `InterpPoseTime` answers 0 (:1644), and `GetFramesNow`'s
`if (t > 0.0F)` (`game/draw.cpp:2863`) is not entered. Behind `-log`, `INTERPLOG` ends in
`bridge=N`, the bridged frames of the second (`game/interpolate.cpp:770`), and `BRIDGELOG
f= anim=A->B frame=F->G hips= arc=short|long` (:2004) is written once per bridged tick.
*The four changes*, measured with an offline probe that includes
`game/interpolate_math.h`, identical in ANDREA1 and JOBY2, the same way round in all
thirteen levels (the hips' angle from the first pose to the second, the way taken, the
step per drawn frame at 165 Hz, the first pose against the matrix blend the flip tick
drew at the hips): 210 into 211, 179.15 degrees about the item's x axis, her lateral
axis; the shorter way, about -x, turns against the hips over 210's keys 7 to 8 (11.0
degrees, cosine -0.415) and over 211's keys 0 to 1 (22.4, cosine -0.490), so the longer
way, 180.8 degrees about +x: 32.9 degrees a frame in place of 179.2 on one frame and none
after it, the one frame at 60 lying 90.3 and 90.6 degrees from the two ends; the first
pose is the turned key 8 itself, 0.00 degrees (in ANDREA2, whose 210 runs at rate 3, the
first pose is built at 0.75, 176.8 degrees apart, long way 183.2). 212 into 213, 19.5
degrees, the shorter way, which turns with the hips on both sides (cosines 0.998 and
0.485): 3.4 to 3.7 a frame; 0.49 degrees from the blend (the worst node 1.67). 207 into
209, 13.5 degrees, the shorter way (near): 2.3 to 2.6 a frame; 0.45 (worst node 1.12). 48
into 11, 5.9 degrees, the shorter way (near): 0.8 to 1.3 a frame; 0.13 (worst node
0.70). Checked by `Preparation/Tools/Diagnose/InterpTest.cpp` (`key_bridge`, :4461, 26
checks): the ends of the bridge against both poses, the arc of 210 into 211 with the
level's own numbers (90.4 degrees from each end at t 0.5, a rotation, on the far side of
the shorter way), 212 and 207 kept short, the box and offset, and when it fires;
`RootProbe -sweep` over ANDREA1 and its `-grab` of the four changes and of 146 into 147
print what they printed before.

**Commits.** `b7607c9`, `67407bf`, `afae389`

### Lara squeezed to a sliver in the tightrope turn: the far key-frame pair
2026-09-24

**What.** When Lara turns round on the tightrope (Roll or Back while balancing), her
whole body is squeezed to a thin vertical sliver for one tick and then is whole again.
The defect belongs to the base engine: it appears with the HD art off, at *Frame rate
30* and under both control schemes. The turn is animation 451 (`ANIM_TROPETURN`); in
ANDREA1 it runs frames 8864 to 8916 at rate 2, 27 key frames. Over keys 0 to 25 it turns
her hips (node 0) from 21.8 to -161.4 degrees of yaw, and between key 25 (frame 8914)
and key 26 (frame 8916) it swings them back by 176.9 degrees, while the animation's own
FlipEffect 0 on frame 8916 (`turn180_effect`) turns the item by half a turn, so the key
frames join up. The one frame in between, 8915, goes through vanilla's key-frame blend,
which averages the two fully multiplied-out matrix stacks element by element at 0.5.
Across hips 176.9 degrees apart that average is almost singular: the hips' columns come
out 0.087, 0.995 and 0.059 long (determinant 0.001), the fifteen node origins shrink
from 488 and 507 units wide to 9, and she is flattened onto her vertical axis. In
ANDREA2 and ANDREA3 the same animation is rate 4 with the far pair at keys 12 and 13
(171.3 degrees), drawn on three frames at column lengths 0.508, 0.105 and 0.508. At
sixty frames and above the tween blended the same pair, because the far-pose rule was
asked only at rate 1. The same blend squeezes her in three more pairs: the end of the
forward twist 209 and of the back twist 213 (key 0 to 1, the hips' shortest column 0.36
and 0.70 of its length at the middle frame) and rich3's crouch roll 219. A key pair of
Lara's whose hips turn more than 90 degrees is now drawn in one of two ways, at every
frame rate. Where `turn180_effect` fires on the pair's second key - the tightrope turn's
four pairs - the first key frame is drawn in place of the blend, and the tween across it
falls back to the pose of the current tick (the first key inside the pair, the second
key on the tick that reaches it). Every other far pair is drawn from a key frame built
at the weight in hand, each node on the shortest turn between its two keys, so that
every in-between frame is a whole body turned part of the way; the tween frames build
theirs at the weight between the two ticks. Behind `-log` a `KEYFARLOG` line names every
tick that takes either path, with `mode=hold` or `mode=lerp`.

**Why.** The squeeze is the arithmetic of the blend, not of the tightrope: an
element-wise average of two rotations D apart shrinks the frame by `cos(D/2)` and is
singular at half a turn, and the hips' frame is the one every other node inherits. The
measurement ties the picture to the pair: a `-log` run of ANDREA1 at 165 Hz shows 7
tightrope turns of 52 ticks each, and `INTERPLOG` read `stretch=0.12` or `0.13` at node 13 on animation 451,
frame 8915 or 8916, in the second of every one of them against 0.84 in every other
second; in the video of the same session the sliver falls on the far pair's in-between
tick in 5 of 5 turns and nowhere else between source frames 360 and 887. The test is the
hips alone and the limit is the rate 1 rule's 90 degrees (`INTERP_POSE_TURN_COS`):
testing every node would take in 611 blended pairs, mostly arm and leg swings that
stretch one bone and collapse nothing, and a composed world rotation of hips or torso
would take in 77. The first key frame and not the nearer one, because in all four levels
with a playable 451 the half turn fires on the second key's frame, so every frame in
between carries the yaw that belongs to the first key; the second key at that yaw would
face 177 degrees wrong in ANDREA1, and in ANDREA2 and ANDREA3 frame 8527 and 9407 are
nearer to key 13. The same frame goes into both slots of the interpolated draw, as the
still-body hold does, so the arms keep their own time blend; a pose blended with itself
is that pose. TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931 -
guards the same pair: it tests only the root node's turn between the two key frames and
draws the first key frame unblended where that turn passes 150 degrees. Reading it
shaped the choice of the first key frame and of the root-only test, which this
measurement confirmed; the threshold here is 90 degrees, because at 150 animation 213
(98.8 degrees) and rich3's animation 219 (94.9 degrees) would stay squeezed. Nothing of
its code is used.

The first key is right only where the half turn fires on the second. On 209, 213 and 219
no effect fires inside the pair, and held there the body stood still for three ticks and
then snapped 98.8 or 152.6 degrees in one; vanilla's blend moved but shrank. The built
key frame gives each of the three in-between frames a rotation part of the way along:
over the thirteen levels the probe builds all 27 such pairs at their own weights (0.25,
0.50, 0.75) and finds the hips' columns of length 1 to four decimals, 213's middle frame
49.4 degrees from each key, 209's 76.3 and 219's 47.5, and the back twist turning 24.6
to 24.8 degrees a tick through 213's pair. The nodes are blended on the shortest turn
(a spherical blend of their quaternions) and not angle by angle: a node's two keys may be
stored on the two sides of x = 90 degrees, as (x, y, z) and its twin (180 - x, y + 180,
z + 180), and in 209 node 9 turns 31.6 degrees while its y and z move 157 each; blended
angle by angle, node 12's frame in the world strays 191 degrees off the way between its
keys at the middle, where the spherical blend leaves the hips on the shortest way
exactly and the worst node 10.5 degrees off it once composed down the chain (rich3's 219
turns its head 39.5 degrees further than the shortest way, hips and head turning about
opposite x). Nodes both keys store about the same single axis blend that angle and keep
their form; a node equal in both keys is copied word for word.

Over the thirteen campaign levels, 35358 of Lara's key pairs are blended by the engine,
and the rule fires on exactly 31 of them. The first key is held on 4: 451 key 25 to 26
in ANDREA1 and rich2 (176.9 degrees) and key 12 to 13 in ANDREA2 and ANDREA3 (171.3
degrees), each with `turn180_effect` on its second key (frames 8916, 7545, 8528 and
9408). A key frame is built on the other 27: animations 209 (after the running jump's
twist 207, 152.6 degrees) and 213 (after the back jump's twist 212, 98.8 degrees), key 0
to key 1, in every level, and rich3's animation 219 (the crouch roll, state
`AS_DUCKROLL`, 94.9 degrees), key 0 to key 1. The nearest hips turns on
either side of the limit are 85.97 and 94.92 degrees, so float rounding cannot move a
pair across it; the engine's float test and a double-precision turn agree on every pair.
Every other pair is drawn exactly as before at every frame rate, with the HD art on or
off and under both control schemes. Animation 205 (rate 1, 179.9 degrees) is never
key-blended and keeps the rate 1 rule. Animations whose frame data do not match their
header's frame size (87 to 187 of Lara's per level, with boxes turned inside out and
root offsets beyond 4096) are left out of these counts; the hips test fires on 2321 of
their key pairs, which were not measured in play. Other items keep vanilla's blend: 0 to
18 root-node far blended pairs per level exist in slots 43, 53, 75, 172, 174, 220 and
442, counted but not looked at. Where only another node of Lara's turns more than 90
degrees - 580 blended pairs, arm and leg swings - vanilla's blend stays: a limb averaged
across 120 degrees keeps half its length on the middle frame, but none was reported in
play, and testing every node (`InterpPoseFar` over all fifteen) would change the
in-between frames of all 580 at once; not done here. It was reported afterwards and is
done below, in the drawn frame (*Every node, in the drawn frame*).

**How.** `game/interpolate_math.h:786`: `InterpKeyFar`, `InterpPoseFar` over node 0 with
`INTERP_POSE_TURN_COS`. `game/interpolate_math.h:1718` (`InterpKeyHold`): the far pair is
held where `InterpTurnKind` finds the half turn inside it (`INTERP_TURN_INSIDE`, see
*Lara facing the wrong way for one tick in the jump twists*) and the flip frame is the
pair's second key. `game/interpolate_math.h:1386` (`InterpKeyFrameLerp`): the built key
frame - the box of the nearer key, the offset blended in eighths exactly as
`InterpRootKey` blends it, a node equal in both keys copied, a node both keys store about
the same single axis blended by `InterpLerpAngle` in that form, every other node by
`InterpSlerpRot` (:1160, through `InterpRotToQuat` :1054 and `InterpRotToAngles` :1115,
the two exact inverses of a quaternion and `InterpPoseRot`'s matrix) and packed
three-axis by `InterpPackRot` (:1030; `InterpRotAxis` :1015 names a node's form).
`game/draw.cpp:3006` (`LaraPoseFrames`, called from `CalcLaraMatrices`,
`game/delstuff.cpp:197`, before the still-body hold, and from `HairControl`,
`game/hair.cpp:309`): on a blended frame whose pair is far, the held pair gets `frm[1] =
frm[0]` (:3070), every other pair a built frame at the frame's own factor in both slots
(:3074), in a static buffer of `INTERP_KEYFRAME_MAX` shorts; `InterpRootNow` then blends
that frame's offset with itself. `game/draw.cpp:2668` (`GetFramesAt`): at rates above 1
only the tween across a held pair of Lara's is refused (`LaraKeyHold`, :2554), so
`GetFramesNow` falls back to `GetFrames` of the current tick there; the refusal counts in
`pose_skips`, and `pose_far` stays the rate 1 count. The tween across a built pair is
accepted and builds its frame at the weight between the two ticks. `game/draw.cpp:2733`
(`GetKeyAt`, for `GetRootAt`): the previous tick's root offset on a held pair is the
first key's, the offset that tick drew; on a built pair it is `InterpRootKey`'s, the same
number the built frame carries. `game/interpolate.cpp:1911` (`InterpNoteKeyFar`, declared
in `game/interpolate.h`, called from `LaraPoseFrames` for the view set): `KEYFARLOG f=
anim= frame= keys= rate= hips= drawn= mode=`, once per tick, `drawn=-1` with `mode=lerp`.
The braid follows the same frames; left alone: the collision spheres (`game/sphere.cpp`),
the bounds of `GetBoundsAccurate` and `GetBestFrame`, and `DrawAnimatingItem`. The HD
body reads `lara_matrices` and follows without a change. What it costs: the hips test
(155 to 168 ns per call) on Lara's blended frames, and on the in-between frames of a
built pair a node blend for each node that differs between the keys, fifteen at most. At
`fps_mode 0` the in-between frames of the 27 built pairs change and nothing else; the 4
held pairs draw as before. Checked by `Preparation/Tools/Diagnose/InterpTest.cpp`:
`pose_wrap`, block 5 (half a turn and 91 degrees at the hips are far; 89 degrees, a key
frame against itself and a half turn below the hips are not), and `key_build` (:4084),
blocks 1 to 5: every packed form unpacks and packs to itself, two rotations 98.8 degrees
apart blend to 49.4 from each, the short way across the wrap, a one-axis node against a
three-axis one, the built frame's box, offset and copied node, and 213's and 209's hips
copied from ANDREA1 (`03f2 6ed1` / `3bc0 3fb1`, `3280 7942` / `022f 5be6`) built at a
half into a rotation 49.4 and 76.3 degrees from each key, where vanilla's middle frame
keeps a hips column of 0.70 and 0.36; `key_build` adds 38 checks, 564 in all. Measured
with an offline probe that reads the thirteen levels and includes
`game/interpolate_math.h`: `InterpKeyFar` fires on the 31 pairs above and on no other
blended pair, `InterpKeyHold` on the 4 tightrope pairs and on no other.

**Every node, in the drawn frame** (2026-09-25). *What.* With the hips' rule in place the
owner still saw her distorted now and then: at the take-off of a jump both arms stretched
flat to the sides with paper-thin hands, at times what looked like an arm drawn twice, a
twisted body in the somersaults. The hips are one node of fifteen. Vanilla's blend
averages every node's drawn rotation - its own rotation multiplied onto its parents',
the matrices `Rich_CalcLaraMatrices_Interpolated` (`game/delstuff.cpp:710`) builds on two
stacks and `InitInterpolateAlpha` / `InterpolateMatrix` (`game/draw.cpp:249`, :3381)
average - so a limb whose drawn rotation turns far between two keys collapses as the
hips did. In the standing jump forward (animation 76, key 0 to 1, rate 2, the same numbers
in ANDREA1 and JOBY2) the upper arm (node 11) turns 143.7 degrees on its own and the
forearm (node 12, 0.4 on its own) 170.1 in the drawn frame; on the one in-between frame
vanilla's average keeps 0.086 of the forearm's breadth (the smallest singular value of the
averaged matrix; node 11 0.087, the other arm 0.20, the thighs 0.45 and 0.42). A key pair
of Lara's is now far where the drawn rotation of any node turns more than 90 degrees
between its keys, and every far pair is drawn as the hips' far pairs were: held on its
first key where `turn180_effect` fires on its second (the same four tightrope pairs),
else from a key frame built at the weight in hand, at every frame rate, with the HD art
on or off and under both control schemes. Every other pair is drawn as before.

*The pairs.* Over the thirteen campaign levels' playable Lara animations the engine
blends 35364 key pairs - the 35358 counted above and 6 whose node data run past their
frame size (five of them far: 379 key 2 to 3 in andy1, Andy2 and joby4, 152 key 3 to 4 and
4 to 5 in rich3; one more in rich3, not far), which the engine reads as it always has.
1204 of them are far: ANDREA1 80, ANDREA2 114, ANDREA3 98, andy1 83, Andy2 89, andy3 96,
JOBY2 81, joby3 76, joby4 93, joby5 72, rich1 113, rich2 75, rich3 134. 32 of them are far
at the hips (the 31 above and rich3's 152 key 3 to 4, 165.9 degrees, one of the six), 1172
only at another node; 4 are held (the four tightrope pairs, unchanged), 1200 built. The
take-offs, with the node that turns furthest, its drawn turn, and what vanilla's middle
frame kept of it (`cos(D/2)`), identical in ANDREA1 and JOBY2: 76 forward
(`AS_FORWARDJUMP`) node 12, 170.1 degrees, 0.09; 91 up (`AS_UPJUMP`) node 11, 133.8,
0.39 - its own turn is 179.9, of which its parent's turn takes part back; 74 back node 12,
153.8, 0.23; 78 left node 9, 147.9, 0.28; 80 right node 12, 131.7, 0.41. The back jump's
twist 212 key 0 to 1, node 10, 137.0, 0.37, and key 1 to 2, node 1, 114.4, 0.54 (the hips
turn 38.7 and 45.3, so the hips' rule did not fire); the running jump's twist 207 key 2 to
3, node 3, 162.5, 0.15; 208's six pairs from key 0 to key 7, 102.7 to 127.1 at the head,
an arm and the legs; hitting a wall 53 and 54 key 2 to 3 (113.9 and 106.1); the bar swing
159 key 1 to 2, node 8, 177.9, 0.02. At rate 1 the table marks 205 key 0 to 1 in eight
levels and nothing else; the rate 1 rule refuses it before (`game/draw.cpp:2662`), as
before. 159 key 1 to 2, called the bar swing here, is the start of the handstand from a
ledge (`AS_GYMNAST`); it was reported afterwards and is left to vanilla's blend below
(*A seam in the data*).

*Why the drawn turn and not the node's own.* The own turn is not what the blend averages:
it would mark 617 blended pairs, missing 719 whose drawn turn passes 90 degrees (10 key 0
to 1, state `AS_RUN`: no node over 56.7 on its own, node 13 94.2 drawn) and taking 132 whose
drawn turn stays under it (53 key 0 to 1: node 9 147.6 on its own, no node over 83.6
drawn, 0.75 kept). The threshold is the rate 1 rule's, 90 degrees, as a constant of its
own, `INTERP_KEY_DRAWN_COS` (`0.0F`, the cosine), so it can move without moving the rate
1 rule. The nearest drawn turns on either side of it are 89.925 and 90.043 degrees. The
largest drawn turn over all 35364 blended pairs, with what the middle frame keeps
(`cos(D/2)` at the band's edges): below 45 degrees 27402 pairs (1.00 to 0.92); 45 to 60
3429 (0.92 to 0.87); 60 to 75 2110 (0.87 to 0.79); 75 to 90 1219 (0.79 to 0.71); 90 to
120 736 (0.71 to 0.50); 120 to 150 364 (0.50 to 0.26); 150 to 180 104 (0.26 to 0). The
running jump's first frames stay under the limit (JOBY2, 16 to 19): 17 key 0 to 1 at
79.8 degrees (node 11) and 19 key 0 to 1 at 77.8 (node 8), 0.77 and 0.78 kept; a limit
of 75 degrees would take in the 1219 pairs of that band as well.

*The way round near half a turn.* The built frame takes every node the shorter way round
its own turn (`InterpKeyFrameLerp`). How far that strays in the drawn frame was measured
as how much longer each node's drawn path through the built frame is than the shortest
turn between the two keys' drawn rotations (0 on that turn), the worst node and weight
of each pair: under 1 degree on 549 of the 1200 built pairs, under 5 on 1055, under 20
on all but 17. The standing jump up, 91 key 0 to 1 in every level, is 13 of them: the
upper arm (node 11) turns 179.85 degrees on its own and 133.8 in the drawn frame, the
shorter way of its own turn is decided by the data's last bits, and it puts the arm 180
degrees off the middle of the drawn turn on the in-between frame (116.0 and 115.9 degrees
from the two keys). The other four: rich3's 219 key 0 to 1, its head 39.5 (as measured
when that pair was first built), andy3's 380 key 1 to 2, 21.3, and rich3's 152 key 3 to 4
and 4 to 5, whose node data run past their frame size, 144.2 and 59.9. A node whose own
turn lies within a degree of half a turn (`INTERP_KEY_HALF_COS`) now takes the way whose
drawn rotation lies nearer the middle of the drawn turn, decided once for the pair at
its middle under its parent as built, so that every weight follows one path
(`InterpKeyFrameFar`). Over the thirteen levels that changes 14 of the 2741 built frames
(pairs times in-between weights): 91's in-between frame in every level, the arm now 20.7
degrees off the middle of its drawn turn, and rich3's 152 key 4 to 5, whose path by the
measure above goes from 59.9 to 102.1 degrees - frame data that are no pose. The other
2727 are `InterpKeyFrameLerp`'s word for word. Two other rules were measured and dropped:
slerping every node's drawn rotation and deriving its own puts every node on the drawn
path but swings a joint through half a turn where a drawn turn itself lies near 180
degrees (in 159 key 1 to 2, in every level, the forearm's own rotation, the elbow, runs
274.8 degrees further than the shortest way between its keys; 18 pairs in all); and
choosing the way for every node past 150 degrees on its own lengthens 159 key 1 to 2's
drawn path from 4.2 to 342.8 degrees, its upper arm turning 177.9 in the drawn frame.

*The early turn.* `InterpTurnKind` keeps asking the hips (`InterpKeyFar`,
`game/interpolate_math.h:786`), and with it the hold and the bridge: a pair far only at
another node never becomes an inside turn. Over the thirteen levels one such pair has
`turn180_effect` on its second key, rich3's 210 key 1 to 2 (flip 3992, the head 96.9
degrees in the drawn frame, the hips 23.3): an early turn, never held. Its in-between
ticks are built like any far pair; the tween frames into the flip tick take the pair
turned by half a turn (`InterpKeyFrameTurn`), and where that pair is far they are built
from the two turned copies (`game/draw.cpp:3049`), whose drawn turn is the pair's own
(96.877 degrees both). At *Frame rate 30* the flip frame lies on key 2 and is drawn as
before.

*How.* `game/interpolate_math.h:856` (`InterpDrawnRots`): every node's drawn rotation in
`InterpPoseRot`'s layout, the node's own on the left of its parent's, the parent found by
the bone table's pop (bit 0) and push (bit 1) as the draw finds it; node 0's is its own,
bit for bit, so every pair `InterpKeyFar` fires on is far here too. :901
(`InterpDrawnTurnCos`) the largest turn and its node; :927 (`InterpKeyFarDrawn`) the
test against `INTERP_KEY_DRAWN_COS` (:834); :950 (`InterpKeyFarFill`) one animation's
pairs as one bit each, each key frame walked once; :980 (`InterpKeyFarBit`) false
outside the table, whose width is `INTERP_KEY_FAR_WORDS` (:835) 32-bit words, 256 pairs:
the longest playable animation of Lara's has 199 (rate 1, 471 in ANDREA3; 100 at rates
above 1, 471 in andy3). :1480 (`InterpKeyFrameFar`): `InterpKeyFrameLerp` (:1386) unless
a node turns past `INTERP_KEY_HALF_COS` (:1297) on its own, then the ways decided at the
middle through `InterpSlerpMat` (:1302) and handed to `InterpKeyNodeLerp` as `longArc`.
`game/interpolate.cpp:2160` (`InterpKeyFarOf`, declared in `game/interpolate.h:348`): the
table, 512 slots like `InterpTurnOf`'s (:218), filled for an animation on its first call
and emptied by `InterpInvalidate` (:1619). `game/draw.cpp`: `LaraKeyFar` (:2528) asks it
with Lara's mesh count and bone table; `LaraKeyOf` (:2538) names the pair `frm` holds, or
none for the bridge's frame; `GetFramesAt` (:2668) and `GetKeyAt` (:2733) ask
`LaraKeyFar` where they asked `InterpKeyFar` before `LaraKeyHold`, which answers as before
because only the hips make an inside turn; `LaraPoseFrames` (:3006) takes the pair's key
from `LaraKeyOf` and builds every far pair (:3074), holds the four (:3070), and builds a
far pair under an early flip from its turned copies (:3049).
The body takes it through `CalcLaraMatrices` (`game/delstuff.cpp:197`), the braid through
`HairControl` on the tick's pair (`game/hair.cpp:302`, :309), the HD body from
`lara_matrices`; the root is untouched, the built frame carrying the offset vanilla's
blend gives. Behind `-log` `KEYFARLOG` ends in `node= turn=` (`game/interpolate.cpp:1961`),
the node that turns furthest in the drawn frame and its turn in degrees, and is written on
the new pairs too. What it costs: 18 KB of table; filling it 1.6 microseconds a pair (x86
`/O2`, measured offline), 12 to 20 on average for one animation and 285 for the longest,
a level's 3.8 to 7.6 milliseconds spread over each animation's first draw; per frame a
lookup where the hips test cost 155 to 168 ns.

*Checked.* An offline probe that includes `game/interpolate_math.h` runs the engine's
table fill on every animation of the thirteen levels and compares every pair with a
double-precision walk of its own (its own unpacker and rotations after `specific/3dmath.cpp`):
0 disagreements, the drawn turns at most 0.056 degrees apart, next to half a turn where
the arccosine is steep (on 20 far pairs the two walks name different nodes of turns that
close); the engine's `InterpKeyFrameFar` against `InterpKeyFrameLerp` on all 2741 built
frames, as above. `Preparation/Tools/Diagnose/InterpTest.cpp` (`key_far_all`, :4705, 30
checks, 620 in all): the walk over a hand-built tree (a child turned 60 degrees under a parent
turned 60 turns 120, a node popped back to the root not at all, turned back against its
parent not at all, a parent's y and a child's x in the draw's order); ANDREA1's 76 key 0
to 1 far at node 12, 170.1 degrees, not far at the hips, marked by the table, built at 0.5
into drawn frames with columns of length 1, the upper arm about 72 degrees from each key
and the forearm about 85, where vanilla's middle frame leaves the forearm a determinant of
0.007; 10 key 0 to 1 far (94.2 drawn, near by every node's own turn), 53 key 0 to 1 near
(83.6 drawn, far by its own), the run's 1 key 0 to 1 near (14.5); 451 key 25 to 26 far,
inside and held, 76 far and not held, an early far pair not held; the table's bound; 91
key 0 to 1, whose upper arm the shorter way puts about 180 degrees off the middle of its
drawn turn and `InterpKeyFrameFar` about 21, on that way at a quarter and three quarters
too, and 76 built by `InterpKeyFrameFar` word for word as by `InterpKeyFrameLerp`.
`RootProbe -sweep` over ANDREA1 prints what it printed before, byte for byte.

**A seam in the data** (2026-09-25). *What.* With every far pair built, the owner found
the handstand from a ledge broken: while she pulls up, her arms make strange turns and
her armpits clip wildly about, through the wall as well; the ordinary pull-up looks
right. His recording of an earlier build shows the same move with both hands on the ledge
in every picture. The handstand is animation 159 (`AS_GYMNAST`, rate 4, frames 3935 to
4127 in ANDREA1), and the pair is key 1 to 2 (frames 3939 to 3943): both keys hold her
wrists on the ledge (the right wrist 6.5 units apart between them, the left 7.3) while
her shoulders rise 189 units past them and both upper arms flip from above and in front
of the shoulders to behind them, the right one's direction from (0.63, -0.45, 0.64) to
(0.04, 0.28, -0.96) in the item's frame (y down, z towards the wall: the wrists stay at z
74 to 86 over keys 0 to 3, the hips at 45 to 55). The right upper arm (node 8) turns
173.8 degrees on its own and 177.9 in the drawn frame, the left (node 11) 156.4 and
162.8. The built frame takes node 8 the shorter way of its own turn, which is the longer
way in the drawn frame, about 182 degrees, through her side: at a half both upper arms
point down and out to the sides ((0.64, 0.76, 0.13) and (-0.59, 0.79, 0.14)) and the
wrists stand at (120, -649, 144) and (-146, -644, 121) where the line between their key
places runs through (88, -675, 75) and (-97, -676, 77), 80.4 and 73.2 units off it, 69
and 44 of them towards the wall (80.7 and 73.2 at most over the three in-between
weights). Vanilla's blend averages the two keys' whole matrices, position included, so
each joint walks the straight line between its key places: the wrists stay on the ledge
and the arms shorten across the turn (the right upper arm 0.60, 0.38 and 0.60 of its
length on the in-between frames, its averaged frame's determinant 0.25, 0.0003 and 0.25).
A key pair of Lara's whose hips turn less than 90 degrees and where a node from 1 on
turns more than 170 degrees both on its own and in the drawn frame is now a seam in the
data and is left to vanilla's blend, at every frame rate, with the HD art on or off and
under both control schemes. Every other pair is drawn as before.

*Why not a turn.* Three ways through the pair were measured, the wrists at a half: the
shorter way of node 8's own turn (the build above), the longer way ((39, -646, -31), 106
units further from the wall than the line, behind her back) and a spherical path of every
node's drawn rotation with the own rotations derived ((68, -808, 89), 133 units above the
ledge). None keeps a hand on the ledge: two keys that pin the hands while the arm flips
carry no turn to interpolate. The arm's own motion names no side either. Node 8 turns
38.2 degrees over key 0 to 1 in the sense of its shorter way (the cosine of the two
turns' axes 0.81) and 84.0 over key 2 to 3 against it (-0.85); in the drawn frame 40.1
against the drawn shorter way (-0.85) and 65.3 with it (0.86). Over all 1200 built pairs
no node turning more than 150 degrees on its own has both neighbouring pairs turning with
its longer way: 159's two arms in every level and two nodes of rich3's 152 key 3 to 4
come in with one sense and leave with the other, 209's hips in every level have only a
following pair and it runs against the shorter way (a pair the owner accepts), 91's two
arms in every level have no neighbour in their animation, and rich3's 152 has six more. A
rule that takes the longer way where the neighbouring pairs ask for it would change
nothing past 150 degrees, or turn 209's hips. Holding key 1 until the tick that reaches
key 2 keeps the hands as well, but freezes her body for a pair in which it rises 189
units. Vanilla's blend is the owner's own reference.

*The pole.* The owner also saw odd poses for a few frames on a pole, in slow motion, and
was not sure they were vanilla's. In RICH2, where the log shows him climbing one, the
only far pair of the pole animations is 329 (`AS_POLEUP`) key 0 to 1: the left hand lets
go and grips again 131.4 units higher, turning 100.8 degrees in the drawn frame (64.3 on
its own; the left upper arm 72.8, the forearm 63.0). No node comes near half a turn, so
there is no way round to choose and no seam; the frames built across it keep the left
wrist within 20.0 units of the line between its key places, which vanilla's blend walks,
and the right wrist, which holds the pole, within 2.5. The log's RICH2 session writes 44
`KEYFARLOG` lines on it (`node=13 turn=100.8`, from f=15496). In his recording the
climbing hand reaches up beside the chain with the fingers up (the grip changing), and
the curled pictures after it, knees at the chest, both hands at her face and off the
chain, her body swung away from it, are the push-off before the back jump from the pole,
animation 327 (keys 2 to 4: the hips move 116 units back from the pole and the wrists
leave it, z from 35 and 41 to -202 and -176), which the table does not mark in RICH2 and
vanilla's blend draws as it always has; then 75 for one tick, and the back somersault 212
and 213, whose far pairs are built as in the standing back jump. Both belong to the
level's data, and this change touches neither. Both recordings are playbacks scrubbed
with the mouse, a new picture only every 0.2 to 0.8 seconds, so which frame of which pair
each picture shows was not measured.

*The pairs.* Over the thirteen campaign levels' playable Lara animations 14 of the 1204
far pairs are seams: 159 key 1 to 2 in every level (node 8, 173.8 degrees on its own and
177.9 drawn, the hips 2.8) and rich3's 152 key 4 to 5, whose node data run past their
frame size (node 1, 179.8 and 174.3). Far, marked, held and built per level: ANDREA1 80,
79, 1, 78; ANDREA2 114, 113, 1, 112; ANDREA3 98, 97, 1, 96; andy1 83, 82, 0, 82; Andy2
89, 88, 0, 88; andy3 96, 95, 0, 95; JOBY2 81, 80, 0, 80; joby3 76, 75, 0, 75; joby4 93,
92, 0, 92; joby5 72, 71, 0, 71; rich1 113, 112, 0, 112; rich2 75, 74, 1, 73; rich3 134,
132, 0, 132; in all 1190 marked, 4 held (the tightrope's, as before) and 1186 built.
Every built frame (pair and in-between weight) before and after: 1200 pairs and 2741
frames before, 1186 and 2701 after; the 40 frames that go are the 14 seams' (three
weights of 159 key 1 to 2 in each level, one of 152 key 4 to 5), and every other frame is
word for word what it was. Every other far pair whose hips turn less than 90 degrees
stays at or below 143.7 at every limb by the smaller of its two turns (76 key 0 to 1, the
upper arm: 143.7 on its own, 170.0 drawn), so any limit between 143.7 and 173.8 gives the
same list; 170 is where a limb's two ways round differ by 20 degrees. The limbs of the 32
pairs whose hips turn far reach 159.1 (rich3's 152 key 3 to 4), and those pairs keep the
hips' rules whatever their limbs do. The standing jumps (76, 91, 74, 78, 80), the twists
(207, 208, 209, 212, 213), the roll 151, the run's 10, swimming's 86, climbing out of
water 111, the fast fall 33, 379 and 406 are no seams and are built as before. The rate 1
marks (205 key 0 to 1 in eight levels) are pairs of the hips and stay. Of the animations
whose frame data do not match their header's frame size, 143 far pairs are seams as well
(ANDREA3 21, andy1 21, Andy2 19, andy3 39, joby4 17, joby5 1, rich3 25), left to
vanilla's blend with the rest; they were not measured in play.

*How.* `game/interpolate_math.h:989` (`INTERP_KEY_SEAM_COS`, -0.98481, the cosine of 170
degrees); :997 (`InterpKeySeamAt`): -1 where the hips turn far (`InterpKeyFar`), else the
first node from 1 on whose drawn turn (from the drawn rotations the fill already has) and
own turn both pass the limit; :1045 (`InterpKeySeam`), the same from two key frames
alone; :1063 (`InterpKeyFarFill`, the test at :1087) marks a far pair only where it is no
seam. `InterpKeyFarOf` (`game/interpolate.cpp:2160`) and `LaraKeyFar`
(`game/draw.cpp:2530`) keep their code: a seam reads as a pair that is not far, so
`GetFramesAt` accepts the tween across it, `GetKeyAt` blends its root as vanilla does,
and `LaraPoseFrames` (:3013) leaves both slots on the stored keys for vanilla's blend. On
a blended frame of a pair that is not far, `LaraPoseFrames` calls `InterpNoteKeySeam`
(:3071; `game/interpolate.cpp:1977`, declared in `game/interpolate.h:305`), which returns
at once without `-log` and otherwise writes `KEYSEAMLOG f= anim= frame= keys= rate= node=
own= turn=` (`game/interpolate.cpp:2009`) once per tick on a seam: `keys=` the pair
drawn, `node=` the seam's node, `own=` and `turn=` its two turns in degrees. `KEYFARLOG`
is unchanged; on the handstand its lines with `node=8 turn=177.9` (frames 3940 to 3943)
give way to `KEYSEAMLOG ... anim=159 ... keys=1-2 rate=4 node=8 own=173.8 turn=177.9`.
What it costs: the fill runs the test on its far pairs alone; a level's whole table took
6060.9 microseconds before and 6088.6 after in ANDREA1, 3799.9 and 3816.3 in rich3,
5308.2 and 5285.8 in andy3 (x86 `/O2`, offline), within the noise; per frame, without
`-log`, a call that returns.

*Checked.* An offline probe that includes `game/interpolate_math.h` runs the engine's
fill on every Lara animation of the thirteen levels: the table marks exactly the far
pairs that are no seam (0 disagreements over all 5522 far pairs, playable or not, any
rate), and the engine's seam agrees with a double-precision walk of its own on every pair
(0 differences). The survey probe of *Every node, in the drawn frame*, rebuilt against
the new header, prints the same list less the 14 seams (its 14 `MISMATCH` lines are they;
`marked 1190`, `hold 4`, `lerp 1186`) and its paths of the built frames byte for byte,
`InterpKeyFrameFar` being unchanged: it still decides the upper arm of 91 key 0 to 1
alone. `Preparation/Tools/Diagnose/InterpTest.cpp` (`key_seam`, :5124, 15 checks, 635 in
all): a hand-built chain (a limb at 175 degrees on its own and drawn is a seam at node 1
and left unmarked though far, at 169 not, at 171 so; 175 on its own and 135 drawn, 75 on
its own and 175 drawn, and 175 on its own under hips turned 95, 176.6 drawn, are none);
159 keys 1 to 3 copied from ANDREA1 (far at node 8, 177.9 drawn, and a seam there, 173.8
on its own; the table leaves key 1 to 2 and marks key 2 to 3; both keys hold the right
wrist 6.5 apart; the frame built at a half carries the wrists 80.4 and 73.2 off their
line; vanilla's averaged upper arm has determinants 0.25, 0.0003 and 0.25); 76 and 209
key 0 to 1 far and no seam. `RootProbe -sweep` over ANDREA1 prints what it printed
before, byte for byte.

**Commits.** `b643607`, `b7607c9`, `a9da3ac`, `ef9891f`

### Level Select, a keyhole that survives a save, and the underwater pickup animation
2026-07-31 - 2026-09-14

**What.** The main menu carries a Level Select entry that opens the level-select screen
the game already had behind `PlayAnyLevel`; it sits behind an option that is off by
default, so an ordinary playthrough sees the menu unchanged, and the original
`available_levels` restriction is in force, so the three dev/test levels are not
offered. Inserting a key assigns `IFL_TRIGGERED` to the keyhole's flags instead of
running `item->flags &= IFL_TRIGGERED`. `ANIM_PICKUP_UW` (130) joins the list of Lara
animations every randomized level is given, and the card-pull animation is transplanted
into a level that only has the stub rather than the interaction falling back to a plain
`USEKEY`.

**Why.** A randomized campaign and any diagnosis of it need to start a chosen level
directly instead of playing up to it. The `&=` clears every flag bit unless that one was
already set, and a keyhole starts at flags 0 in the level file; the savegame writer
stores an item only when `CODEBITS`, `INVISIBLE` or `TRIGGERED` is set, so the keyhole
dropped out of the save entirely and came back as unused while the key itself was
already gone, and the door could never open, because `KeyTrigger` requires
`ITEM_ACTIVE`. That is an original-game defect, reproducible in unmodified Chronicles;
it is the only `&=` of its kind in the engine, the other 26 sites use `|=`, which is why
the puzzle hole survives save and load correctly, and the assignment keeps the original
intent of clearing the other bits while guaranteeing the one. `ANIM_PICKUP_UW` is a stub
in seven of the campaign levels - Andrea1, joby3, joby5, rich1, rich2, rich3 and
richcut2 - while a randomized run places pickups under water everywhere, so without it
Lara cannot take them there.

**How.** `game/gameflow.cpp` reuses the existing level-select screen rather than
building a new one; the option is held in `tomb5/tomb5.cpp` and is the *Level Select*
row of the *tomb5 options* tab in `game/menu_frame.cpp`. `game/pickup.cpp` holds the
keyhole flag and the card-pull path. The always-list is in
`TR5Randomizer/src/core/ApplyPatch.cpp`; the animation transplant itself
(`Tr5Level::copyLaraAnimFrom`) is in `TR5Randomizer/src/core/Tr5Transfer.cpp` and uses
the model transfer's splice machinery: the animation slot exists in every level, only
its frame data are Core stubs, so the frames, rate, frame size, states and speeds of the
donor set are spliced in and the animation commands appended with their sound and
flip-effect frame numbers recomputed for the target's counting, while frame base and
end, `next_anim`, `next_frame` and state changes stay the level's own.

**Commits.** `f63d3f9`, `5a0b80d`, `3ae1d43`, `52c94ee`, `3a214cf`, `dd5be08`,
`04c1a42`

## 2.3 Tomb Raider III enemies

Four enemies from Tomb Raider III can appear in randomized levels. Their control code is
not this project's own: it comes from tomb3, Troye's decompilation of Tomb Raider III,
<https://github.com/Trxyebeep/tomb3>, under the GNU GPL v3 - the same licence as TOMB5
and as this project, which is what makes combining them possible at all. It was taken
over directly rather than rewritten, so that its origin stays visible, and every file
names that origin, its licence and every deviation from the tomb3 original with a
reason. No game data from Tomb Raider III ships with this project: the models,
animations, textures and sounds are read at run time from the player's own Tomb Raider
III installation, and without it those enemies simply do not appear.

### Four Tomb Raider III enemies, with tomb3's control code
2026-08-16 - 2026-08-25

**What.** The T-Rex (slot `SAS`, 31), the raptor (slot `REAPER`, 75), the Compsognathus
(slot `SAILOR`, 53) and the vulture (slot `CROW`, 43) join the enemy pool. The raptor
hunts only Lara, and the Compsognathus only attacks in a pack and gathers on Lara in its
idle mode. Each has its own hit-point and damage lines in `Balance.txt` like every other
pool enemy. They wear their own TR3 textures, carried over as extra texture pages. The
imported TR3 sounds always sit from slot 445 on, those five slots being free in every
TR5 level, and an imported sound may carry several variants; the T-Rex's footstep is
reachable under the number its animation commands call while the data come from the
number that sounds as it did in TR3.

**Why.** The port was possible because the AI substructure is the same in both engines:
`CreatureActive`, `CreatureAIInfo`, `GetCreatureMood`, `CreatureMood`, `CreatureTurn`
and `CreatureAnimation` exist word for word in TOMB5's `box.cpp`, so the state machines
could be taken over unchanged. Three spots were deliberately left out because TR5 has no
counterpart: tomb3's prey search over the neighbouring rooms for RAPTOR and FLARE_ITEM,
which uses a room list TR5 does not have, so Lara is always the target; `CreatureKill`
with `EXTRA_DINOKILL`, because TR5 has no Lara animation for being eaten and a wrong
index would force her into a foreign one, so the bite deals lethal damage instead; and
Lara's `AS_STOP`/`AS_DUCK` state query while the T-Rex sniffs, because the state numbers
differ between the games, so what is checked is whether Lara holds a torch and whether
the T-Rex was just hit.

**How.** New files taken over from tomb3's files of the same name: `game/trex.cpp`,
`game/trex.h`, `game/raptor.cpp`, `game/raptor.h`, `game/compy.cpp`, `game/compy.h`.
`game/crow.cpp` is not new but extended: TR5 kept only the crow branch of tomb3's
`bird.cpp`, which chooses by `object_number` between the crow's `anim_index + 14` in the
feeding state and the vulture's `+ 5` in the standing state, so the vulture's animation
offsets and its separate death handling were added back for the imported bird. The
credits name tomb3. The run-time extraction from the player's own installation lives in
the randomizer: `TR5Randomizer/src/core/Tr3Source.cpp`, `.h` (named `Tr3Quelle.cpp`,
`.h` until the source tree was put into English) read the models, textures and sounds
out of the installed game, `TR5Randomizer/src/core/Tr5Tr3Import.cpp` installs a TR3
moveable into a TR5 level, and `TR5Randomizer/src/core/ApplyPatch.cpp`, `.h` inject them
into the level being built; the installation paths are settings in the randomizer's own
options. `specific/file.cpp` decides in `LoadSamples` from the format tag how a sample
reaches the buffer: Core's own MS-ADPCM samples still go through the ACM decoder, plain
PCM goes straight into the XAudio2 buffer through `DXCreateSample`. TR3's `MAIN.SFX`
holds 414 RIFF WAVs, mono PCM at 22050 Hz, which is the rate the engine expects, so no
encoder is needed and the samples can be taken over byte for byte; the variants of one
sound become one `SAMPLE_INFO` with N consecutive samples and the count in bits 2-5 of
its flags, the form the engine already reads. `game/trex.cpp` carries the footstep and
bite corrections. The textures are appended by
`TR5Randomizer/src/core/Tr5Transfer.cpp` at the same place a TR5 transfer inserts them,
with texinfos and sprites drawn along after: pages are only appended, never moved.

**Commits.** `83cc2c6`, `745373b`, `5703685`, `239a7e6`, `f6a0d9a`, `4ea724a`,
`cceaee8`, `a21caa5`, `8123127`, `6528139`, `4c6e41c`, `f8e9786`, `46956ac`, `b75bbd0`,
`c3c7ebb`, `52c94ee`, `11e5ff2`, `c5ac60f`, `c7dd3aa`

## 2.4 HD art from the Remaster, read at run time

With one option switched on, the engine reads the art of an installed *Tomb Raider IV-VI
Remastered* out of the player's own installation at run time and draws the levels with
it: a second, shader-based renderer beside the classic fixed-function one, sharing the
back buffer and the depth buffer and nothing else. Nothing of that art is copied into
this repository or shipped with it; with the option off, or with no installation found,
every path here returns on its first line and the picture is the one the classic engine
draws. The file formats are read against TRForge's published format description (MIT,
`vendor/trforge/LICENSE`), and three passages follow TRForge's own code closely - the
facial-rig rescan and the byte reader in `hd_trm.cpp`, and `read_map_file` in
`hd_bind.cpp`; the design of the HD renderer, the installation walk and the water effect
come from TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931 - with his
copyright in the files' own headers and in `THIRD-PARTY.md`; everything else is this
project's own measurement over the player's files. The taboo holds throughout: nothing
is taken from the Remaster's programs `tomb5.dll`, `tomb456.exe` or `tomb123.exe`,
neither bytes nor logic, not at second hand - `hd_install.cpp` only tests that such a
file EXISTS, as a marker that a folder is the right one.

### Finding an installed Remaster, reading what it holds for a level, and the settings that steer it
2026-09-05 - 2026-09-20

**What.** With the setting `hd_enable` on, the engine finds an installed *Tomb Raider
IV-VI Remastered*, reads what that installation holds for the level being loaded, binds
it to the level's rooms and object slots, and writes the counts into the log.
`hd_enable` is 0 out of the box (`tomb5/tomb5.cpp:224`): with it off, `HDEnabled()`
returns before any folder is searched or any file is opened, and the two calls in
`specific/file.cpp` are the only way into the new code at all. The switch is the row
**HD textures** (on/off) on the *Graphics* page of the options frame, saved with its
neighbouring rows, and it takes effect at the next level start - the frame path tests a
flag that only `HDLevelBuild` sets. Ten settings steer the path in all: `hd_enable`,
`hd_objects`, `hd_motion`, `hd_reflect`, `hd_texture_budget_mb`, `hd_install`,
`hd_install_tr3`, `hd_texture_format`, `hd_outfit` and the `hd_footsteps` subkey, and the
manual lists those ten and no others. The four Tomb
Raider III enemies this project ports into TR5 can be drawn from the *other* bundle:
`Tomb Raider I-III Remastered` is looked for in the same walk (`tomb123.exe` with a
subfolder `3` carrying `DATA` and `TEX`), with the registry override `hd_install_tr3`; a
slot marked `fromTr3` is looked up in that bundle only (game 3, then the siblings 1 and
2), never in the IV-VI folders and never the other way round, and the four names are
`TREX.TRM` for TR5 slot 31 (`SAS`), `VULTURE.TRM` for slot 43 (`CROW`), `COMPY.TRM` for
slot 53 (`SAILOR`) and `RAPTOR.TRM` for slot 75 (`REAPER`). Not finding the I-III bundle
costs those four models and nothing else.

**Why.** The Remaster's art is the player's own property, read from his own installation
and never shipped. The two bundles sit side by side in the same Steam library, so one
walk finds both, and the page cache had to learn that page 12 of Tomb Raider III and
page 12 of Tomb Raider V are two different pictures. The four TR3 names are an inference
and not a measurement - there is no I-III installation here to look in - derived from
TRForge's finding (MIT) that the Remaster names a model after the engine's own symbol
for the slot unless the level's `.MAP` says otherwise: keyed on the English words that
rule matched 60 of 211 `.MAP` keys, keyed on the symbol 197, and it holds across all
five games. Because it is an inference, the log says out loud which way it went. A card
that reports 4095 MiB of texture memory is a ceiling and not a fault:
`GetAvailableTextureMem` answers in an unsigned 32 bit count of bytes, so 4096 MiB is
the largest number that fits, and every card that reaches it is nearly seven times over
what the heaviest level needs.

**How.** New, under `TOMB5/specific/hd/`: `hd_install.cpp`/`.h` finds the installation -
the registry value `hd_install` first (same key as every `tomb5.*` setting,
`HKCU\Software\Core Design\Tomb Raider Chronicles\tomb5`), then every Steam library;
carried over from TROne (GPLv3) by Todesengel1931. `hd_bind.cpp`/`.h` decides which
`.TRM` belongs to which slot: the level's own `LEVELn.MAP` first, then a hand table for
the slots this project added beyond the original 460, then the slot's own name; the
slot-name table is generated from `game/objects.h` and a `static_assert` in
`hd_level.cpp` catches it drifting away from the enum, and `read_map_file` follows
TRForge's own code (MIT). `hd_trm.cpp`/`.h` reads the header of a `.TRM`, including the
rescan that gets past the facial rig the head models carry - written against TRForge's
format description, except that rescan and the small byte reader beside it, which follow
TRForge's own code closely (MIT); TRForge's own file was not vendored because it pulls
in a level library this project does not want. `hd_scan.cpp`/`.h` reads the `.TRG`,
counts groups, pages, vertices, indices and draw ranges, binds the slots and times the
BC7 decoder over every page; it is engine-free on purpose, so that `TrgProbe` and
`MapProbe` compile the same file and measure what the game measures. `hd_level.cpp`/`.h`
is the level bracket and the `HDLOG` line. New, under `TOMB5/vendor/trforge/`: verbatim
TRForge (MIT), five source files plus `LICENSE`, `NOTICE.md` and `VERSION.txt`; those
five carry no modification notice of this project and must not - they are copied byte
for byte, their first three lines carry a hidden signature, and nothing in them is
changed. Changed: `specific/file.cpp` calls `HDLevelLoad` after `LoadTextureInfos` (the
first moment rooms, meshes and slots are all in memory, in their randomized state) and
`HDLevelUnload` in `FreeLevel`; `specific/registry.cpp`/`.h` gained
`REG_ReadString`/`REG_WriteString` for the settings that are paths and not numbers
(`hd_install`, `hd_install_tr3`, declared in `registry.h`); `global/types.h` and
`tomb5/tomb5.cpp`/`.h` hold the seven settings; `tomb5.vcxproj` carries the new files
and `/std:c++17` per file for the ones that use `<filesystem>`, the engine keeping its
own default. The options row and its label live in `game/menu_frame.cpp`
(`menu_rows_graphics`) and `game/gameflow.cpp` (the frame's `CustomText` table); nothing
in the randomizer's own `Language.cpp` is involved. The TR3 bundle:
`HDModelSlot::fromTr3` in `hd_model.cpp`/`.h` for the one-way lookup,
`hd_pagecache.cpp`/`.h` offering a guest's pages with `HD_PAGE_ALT_BASE` added and
taking the base off again at the one place a number becomes a file name, a
`static_assert` in `hd_level.cpp` holding the four slot numbers against the enum, and
log words `tr3_not_installed`, `tr3_model_missing` or the model with its origin, plus
`tr3_install=found/absent` on the per-level marker line. The settings table is in
`docs/Engine-Manual.md`.

**Commits.** `e185420`, `7715dda`, `5d0fd65`, `998917f`, `66b0587`

### The second renderer, the seam where the engine draws HD rooms, and what a .TRG material means
2026-09-05 - 2026-09-08

**What.** Where the classic path is fixed-function with vertices the engine has
transformed, clipped and lit itself (`FVF = XYZRHW|DIFFUSE|SPECULAR|TEX2`,
`DrawPrimitiveUP`, no shader anywhere), the HD path is `vs_3_0`/`ps_3_0` with its own
vertex declaration, its own buffers and one texture per remaster page. One vertex buffer
and one 32-bit index buffer serve a whole level; every group's triangles are bucketed by
`(pass, page)` with a stable sort into ranges, one range being one draw call, inside the
state bracket `HDRenderFrameBegin`/`HDRenderFrameEnd`. An HD frame is drawn pass-major:
`HDFrameDraw` draws everything of the HD path that writes depth - every room's opaque
and alpha passes, then every HD object's - `DrawBuckets` then draws the classic opaque
world, `HDFrameDrawGlow` draws the glass phase and the additive pass of the same rooms
and objects, and `DrawSortList` draws TR5's own see-through polygons last. A room's
identity with the `.TRG` group it draws is a table rather than the room number, so a
flipmap keeps its art. A part's material decides how it is drawn: materials 6, 7 and 8
carry a cut-out alpha test at their own `param[0]`; material 14 in the alpha pass is
glass, drawn in a phase of its own (`SRCALPHA`/`INVSRCALPHA`, no depth write, no alpha
test, 96 parts in eight of fifteen levels); material 13 is a four-page flipbook, its
`param[0..2]` layer numbers and `param[3]` seconds per frame; materials 3 and 17 carry a
gloss drawn as a grazing-angle lift behind `hd_reflect` (default 1); a vertex's `sway`
weight is kept only on materials 1, 7 and 8. A material is read as its **low byte**, so
a destructible copy - bit 15 set, a counter in bits 8 to 14 - is recognised as the
material it copies. Static meshes are not drawn twice: `DrawStaticObjects` returns at
once for an HD room, the Remaster having baked them into the room art. A group with no
triangle inside its own room (a *hollow group*: ANDREA2 124, JOBY2 19 and 21, RICHCUT2
2, 3 and 4, TITLE 70, 101 and 103) does not count as HD art and its room stays classic.

**Why.** The remaster's room art has to be drawn where the classic room polygons were
drawn, with the same depth, the same projection and the same effects order, or Lara, the
items and the see-through polygons would sort wrongly against the walls. The structure
of the renderer comes from TROne (GPLv3). Passes 0 and 1 write depth and sort themselves
out wherever they are drawn; the additive pass does not - drawn room-major, a
depth-writing draw standing behind a glow passed the depth test and painted over it,
cutting a room's lamp cones along a straight vertical line where the next room's
geometry began. `FlipMap` does not move a room: it swaps the CONTENTS of two `ROOM_INFO`
records and leaves their numbers alone, so a fixed room-equals-group identity drew the
art of the room that had gone. A page's share of dark texels is not a coverage mask - an
opaque page's alpha carries gloss or wetness and is mostly dark (ANDREA1's facade pages
201, 211 and 207 are 95, 96 and 78 per cent dark) - so a cut-out driven by that share
discarded whole facades; measured over the fifteen `.TRG` files, materials 6, 7 and 8
are the only ones whose `param[0]` is an alpha reference (0.5 in every tuple but one at
0.503). The alpha test is right for a grating and wrong for glass: a pane whose sheet is
80.6 per cent below alpha 16 comes out at a test of 0.5 as a hard mask of shards, and
material 14 is the only glass in the game. `TrgVertex::sway` is a weight and not
permission - 416,153 vertices carrying one are material 8, 92,896 material 1, 46,611
material 7, against 160,620 on material 0 and 3,699 on materials 3, 6, 14 and 16, which
are ordinary geometry, glass, glow and light shafts that must not move. A material 13
part drawn on one page left a bank of monitors and a row of ceiling panels frozen.
Drawing a room's statics again is TROne's "double lantern". Without the hollow-group
rule a room whose group holds only art outside it counted as HD, its classic roomlets
were dropped and it rendered black.

**How.** `specific/hd/hd_render.cpp`/`.h` is the renderer,
`specific/hd/hd_level.cpp`/`.h` the engine-facing module; `hd_model.{h,cpp}` and
`hd_object.{h,cpp}` carry the object half of the frame order. An HD room is not drawn
where it is met: `InsertRoom` (`specific/drawroom.cpp:63`) writes the room number and
the matrix on the stack at that moment - view * world, the one `aSetViewMatrix()` has
just handed the device - into a frame list and returns, skipping its roomlets;
`S_OutputPolyList` (`specific/output.cpp`) draws the list immediately before
`DrawBuckets()`, and `S_InitialisePolyList` empties it beside the buckets and the sort
list. One depth buffer serves both paths because the classic path writes `sz = f_a -
f_boo * rhw` with `rhw = 2048 / z_view` (`specific/polyinsert.cpp:434`,
`specific/drawroom.cpp:379`), which a Direct3D projection with `_33 = f_a`, `_43 =
-f_b`, `_34 = 1`, `_44 = 0` reproduces exactly after the w division; `HDRenderSelfTest`
compares the two over 50 view distances spread across the reciprocal of z, worst
difference 8.9e-08, with `D3DZB_TRUE` (`specific/function_table.cpp:107`), `ZFUNC
LESSEQUAL` (the Direct3D default) and the viewport at `MinZ 0 / MaxZ 1` read back from
the device rather than assumed. `SetD3DMatrix` (`specific/d3dmatrix.cpp`) turns the
engine's 3x4 row-major matrix into a `D3DMATRIX`; the HD path draws with that as its
world matrix, an identity view, and a projection built from the live
`f_persp`/`f_centerx`/`f_centery`/`f_a`/`f_b`, taken when the rooms are collected and
not when they are drawn, because `AlterFOV` moves `f_persp` inside a frame.
`MatrixCheck` (`hd_level.cpp`) runs the engine's own three transform lines against the
matrix that really goes to the card over up to 128 of a room's vertices, once per level:
ANDREA1 room 12, 0.00017 px in x, 0.00009 px in y, depth difference 0; those vertices
are kept aside while the vertex buffer is filled (128 per group, 192 KiB a level)
because the `.TRG` is freed at the end of the build and the buffer is `WRITEONLY`.
`HDLevelLoad` stays on the load thread and only reads and measures; the Direct3D objects
are made by `HDLevelBuild` from `S_LoadLevelFile` beside `aInit()` on the main thread,
and `HDLevelUnload` in `FreeLevel` makes no Direct3D call at all. `DXToggleFullScreen`
and `DXChangeVideoMode` replace the device rather than resetting it (`DXCreate` ->
`DXClose`), so `HDLevelDeviceLost` releases every HD object first and the rooms fall
back to classic until the next level starts; the `Reset()` path in `DXShowFrame` gets
the same call, `Reset` refusing while any `D3DPOOL_DEFAULT` object is alive.
`HDFlipRoom` (`hd_level.cpp`) swaps `g_room_group` in the same statement as the
`ROOM_INFO` records and is called from `FlipMap` (`game/control.cpp`); a loaded save
follows for free, the engine restoring a flip by calling `FlipMap` once per set flip
(`game/savegame.cpp`), and the water marks are built before any flip, on every room
including the alternates. `MaterialAlphaRef` (`hd_render.cpp`) answers materials 6, 7
and 8 with the part's own `param[0]` and every other material with zero, and the answer
travels with the draw call in its sort key and reaches `hd_shader/hd_room.hlsl` as
`gPass.x` for `clip(texel.a - gPass.x)`; zero discards nothing, so the alpha pass (0.5,
the classic `D3DRS_ALPHAREF` 128) and the additive pass are unchanged to the bit. The
per-page cut-out share is kept and printed as a measurement and decides nothing
(`HDPageCacheSlotCutout`, `HDPageCacheSlotCutoutPermille`). The sway weight is taken
away once, at level build, from every vertex no part of material 1, 7 or 8 names. A
material 13 part's triangles are drawn from `flipSlot[frame]`, the frame taken from the
same monotonic game clock the scroll and the wind use, modulo the whole cycle; 27 parts
in JOBY2, JOBY3, JOBY5, RICH1 and RICH2 resolve. The gloss reads material 3's strength
in `param[2]` (0.333 to 1.0, 165,897 triangles) and material 17's in `param[1]` (0.05 or
0.1, 17,453 triangles) and draws Schlick's fifth power of one minus N-dot-V times that
strength, applied to the multiply so that a surface lifts towards its own lit colour;
214 parts over twelve levels have one, and the exponent in `param[0]` is read, logged
and unused, a Blinn highlight inside the eight-times-unrolled light loop taking the room
pixel shader from 319 to 636 slots. RICH1, RICH2 and RICH3 carry neither material, so
the VCI marble is left matt. The glass phase keeps working with the engine's three
shatter paths (`game/control.cpp`, `game/camera.cpp`, `game/objects.cpp` clear
`MESH_INFO::Flags` bit 0, which `UpdateDestructibles` reads). `hd_reflect` lives in
`global/types.h` and is read in `tomb5/tomb5.cpp`. Seams: one `HDLOG` line per room with
the decision and its reason (`hollow_group` among them) and `overhang=`, one `HDLOG
flippair` line per flip pair and one `HDLOG flip` line per swap, and the level line
carrying `cutout_parts=`, `sway_parts=`, `sway_dropped=`, `flipbook_pages=`,
`flipbook_dropped=`, `flipbook_ranges=`, `gloss_parts=` and `reflect=`, the frame line
`flipbook_calls=` and `gloss_calls=`. `HDRenderBlendOverride` exists for the diagnostic
tools alone and the engine never calls it. Tools:
`Preparation/Tools/Diagnose/RoomProbe.cpp` (`broom.bat`) - a hidden window, a real
Direct3D 9 device and a render target, so one room can be drawn from a given camera into
a `.PNG` without starting the game, with `-groups`, `-plane`, `-hasgeom`, `-shatter`,
`-noreflect`, `-with <group> <dx dy dz>`, `-oldorder`, `-black`, `-noblend`,
`-paritycheck`, and a device-state check reading 58 states before and after an HD block;
`Preparation/Tools/Diagnose/TrgProbe.cpp` (`-mat`, `-parts`, `-flat`, `-seam`).

**Commits.** `2435068`, `4f2183b`, `791d844`, `36c068a`, `e50e94d`, `2059a46`,
`34d1b81`, `5285f99`, `a1267b5`

### The room shader, and a build that needs no shader compiler
2026-09-05 - 2026-09-06

**What.** The HD room path is one HLSL source, `specific/hd/hd_shader/hd_room.hlsl`,
with its compiled bytecode checked in beside it as `hd_room_vs.inc` and
`hd_room_ps.inc`; the model path is the same pair around `hd_model.hlsl`. No shader
compiler is part of the build, so the project still builds with nothing but the C++
toolset. `Preparation/Tools/Diagnose/bshader.bat` regenerates the `.inc` files with
`fxc` from the Windows SDK, and the call is written at the top of the `.hlsl`. Every
generated `.inc` carries the MD5 of the `.hlsl` it was compiled from on its last line,
and the engine build compares the two before it builds, regenerating a shader that is
out of step on the spot.

**Why.** GPLv3 is why the source sits next to the bytecode: a reader of the history gets
the form he can change, not only the form the card runs. The shaders are written against
what shader model 3 guarantees - 512 instruction slots - and the device's `ps_3_0` figure
is read at start-up and refused below 512 rather than letting `CreatePixelShader` fail
with an HRESULT. `ps_3_0` has no relative addressing of constant registers, only
`vs_3_0` has, so the dynamic-light loop is unrolled against a compile-time maximum with
the run-time count as a dynamic `if`; written as a plain loop, `fxc` emulates
`gLights[i]` with 32 `cmp` instructions per access, about 140 instructions per light per
pixel. The fog bulb loop stays dynamic because its body is long and its array is short.
The stamp is a CONTENT stamp and not a file date, because a fresh clone gives every file
the same date; and the answer is to rebuild rather than to report, because a build that
quietly draws the wrong thing is the failure this exists to stop. It happened once: the
checked-in `hd_room_ps.inc` was 636 slots with eight `pow` instructions - a Blinn
highlight that had been built, measured and rejected for passing 512 - while its source
was 453 slots with none, the HLSL having been edited without the compiler being run
again; ten regression pictures and a scene with a dynamic light are byte for byte
identical across the swap, so the cost was 183 slots of portability and nothing on the
screen.

**How.** The full register table lives in `hd_room.hlsl` beside the code, and
`specific/hd/hd_render.cpp` is its only writer. `bshader.bat` writes the stamp in its
`:stamp` step as a comment line `bshader-stamp <MD5 of the .hlsl>`;
`Preparation/Tools/Diagnose/be.bat` compares each `.inc`'s stamp against the MD5 of its
`.hlsl` and, where they differ, prints `be: regenerating shaders - ... out of step with
the HLSL` and calls `bshader.bat`. On the tree as it stands `be.bat` regenerates
nothing, and regenerating the four `.inc` files by hand reproduces the checked-in
bytecode byte for byte apart from the stamp line. The generated `.inc` files carry no
licence notice of their own, being compiler output. `tomb5.vcxproj` carries the new
`.cpp` files with `/std:c++17` per file and no precompiled header, the new headers, and
the `.hlsl` as a `None` entry, so it is visible in the solution without becoming a build
step. The shipped room shaders stand at 476 pixel-shader and 348 vertex-shader
instruction slots, the model pair at 348 and 553.

**Commits.** `2435068`, `1087750`, `cf2d049`, `2059a46`

### The engine's own per-vertex lighting, as shader arithmetic
2026-09-05 - 2026-09-07

**What.** Everything the classic path computes per room vertex on top of the baked light
(`specific/drawroom.cpp`, `aRoomletTransformLight`, lines 305-685) is computed for HD
rooms as well: the dynamic lights of torch, flare, muzzle flash and explosions (pixel
shader, up to 8 per room); the WaterTable choppy displacement (vertex shader, on the
marked vertices); shimmer, mist and reflection (vertex shader, selector per room,
switched by `tomb5.shimmer`); underwater darkening; the Red Alert gas rooms; the
underwater screen wobble (vertex shader, clip-space offset); the distance fog (pixel
shader, `tomb5.distance_fog`, 3..30 blocks); the 128 overbright split; and the fog bulbs
(pixel shader, `tomb5.fog`), of which `HD_MAX_BULBS` = **16** reach the shaders in one
frame. The additive pass multiplies by at most one, as the classic path does, and is
weighted from its own material: material 2 (shafts, lamp cones, window and candle glows)
from `param[1]`, material 14 from `param[1]`, material 15 from `param[2]`, with material
2's weight breathing `HD_RAY_SWING` = 0.06 of itself once per `param[3]` seconds. A
material-2 part whose weight falls outside (0, 1) takes the median weight of its own
`(param[0], param[2])` preset, and an unknown preset takes 0.20. A fog bulb's intensity
falls smoothly to zero at its silhouette and is not cut by a 45 degree cone. An HD
item's ambient is `aAmbientR/G/B` after the engine's own
`InitObjectLighting(current_item)` and its light list is the item's own
`il.pCurrentLights` converted to view space one for one, `HD_MAX_OBJ_LIGHTS` = 12 of
them.

**Why.** Without it a torch did not light an HD wall, the distance fog stopped at the
door and three option switches were dead in HD rooms. Sixteen bulbs because a level
holds up to 17 (ANDREA2, and TITLE likewise), a single room up to 5 (ANDY3), and up to
16 fall inside the 20480-unit circle `aBuildFogBulbList` keeps around the camera at once
(16 ANDREA2, 14 RICH2, 13 ANDY1, 11 TITLE, 10 RICH1); sixteen is also the classic path's
own ceiling, `RoomletFogBulbs[16]` in `drawroom.cpp` being filled with no bound check at
all, and at six the title screen visibly lost bulbs. The additive ceiling of one: the
classic path switches the specular off for its additive draw type
(`specific/polyinsert.cpp`), so a classic additive polygon is at most `texel * 1`, while
the HD pass multiplied up to `texel * 2` and blended ONE/ONE - 56.3 per cent of RICH1's
additive channels exceed 1.0 (mean 1.26, max 2.00) and 10.9 per cent of ANDREA1's;
beyond that, the Remaster's shafts and lamp cones are fans of dozens of quads that all
meet at the lamp and therefore SUM, and painting them with no weight of their own
saturates them to white. A quarter-swing moved a candle glow by 8 to 9 per cent of the
whole pixel between two arbitrary moments where the reference material's own shafts
travel 4.4 per cent; at 0.06 the worst pixel of a measured room moves 5.4 per cent
instead of 17.0. The chord through a fog sphere, `2*sqrt(r^2 - d^2)`, has an infinite
derivative at the rim, so a per-pixel evaluation stands the intensity up vertically at
the silhouette where the classic per-vertex evaluation lets the rasteriser smear it; and
the classic cull - a 45 degree cone about the view axis - was affordable at 4:3, where
the frame reaches 40 degrees to the side, while `AlterFOV` (`specific/3dmath.cpp`)
builds the perspective from `height * 320/240` and uses that value across as well, so
16:9 turns a nominal 80 degrees into about 96 across and the first person view's 90 to
100 into 107 to 118, which is 53 to 59 degrees to the side and draws the cut as a
straight vertical line with fog on one side and none on the other. Two brightness scales
are bridged: the classic path builds a colour in 0..255 and splits it at 128, doubling
below and putting the halved excess into the specular colour, which the fixed-function
pipeline adds after the texture modulation (`D3DRS_SPECULARENABLE` on,
`specific/function_table.cpp:104`); a classic surface therefore covers 0..2, and the
Remaster's baked light covers the same 0..2 (`colour1 + colour2`) as a straight
multiply. Below 128 the two are the same line, and that is the bridge. The split itself
is not reproduced, and that is the single place this work leaves the classic path: the
split is a hardware workaround for fitting a brightness of 2 into two bytes, and a
shader has floats; following it would dull every bright surface the Remaster bakes - 20
of 384 sampled light channels in ANDREA1 room 19 are above 1, 111 of 384 in room 69, and
RICH1 room 12 reaches the full 2.0, so `texel x 1.5` would become `texel x 1 + 0.25`.
The fog bulbs keep the split, because for them it is not a workaround: a bulb is haze
between eye and surface, so its density has to darken the multiply and its colour be
added on top, or a bulb in front of a black wall would not glow. The HD path's
brightness is a strictly linear function of the ambient it is handed - measured over
ambients 8, 16, 32, 64, 128 and 255, the model's own mean rising 2.5 to 78.4 of 255 at a
constant ratio of 0.31 to 0.32 - so a creature at ambient 8 is dark because the room is
dark, which is the classic answer too; the twelve-light limit against the classic list's
21 (`ITEM_INFO::il.CurrentLights[21]`) is the one term that can differ, so it is counted
on the log rather than argued.

**How.** Each contribution hangs off the very global the classic path reads, in one
place - `specific/hd/hd_level.cpp`, `FillFrameLighting` and `FillRoomLighting` - so a
switch cannot be on in one path and off in the other; `tomb5.fog` does not reach the
shader at all but gates `aBuildFogBulbList` (`game/draw.cpp:1011`), so with fog off both
paths lose their bulbs in the same frame. Every contribution is computed in the classic
path's own units and order and converted with the one line `f = light * 127.5`, back
with `light = f * 2 / 255`. `specific/hd/hd_light.cpp` holds two transcriptions of one
vertex colour - one of `aRoomletTransformLight` with its longs and its truncations, one
of the HLSL - and `RoomProbe -paritycheck` runs both over 128 of a room's vertices,
printing the contribution in 0..255 before the split and the final brightness on a white
texel. What is per pixel and what is per vertex is decided by the function: dynamic
lights, fog bulbs and distance fog are smooth functions of a position and are per pixel;
the WaterTable effects stay per vertex, the table being indexed by the position
quantised to 64 x 64 x 128 units, so per pixel would paint visible blocks. The water
marks are computed, not read - the `.TRG` marks nothing - and are worked out once at
level build on the classic vertices (`aRoomVertexWaterMarks`, new) and carried to the HD
vertex at the same room-local position through a hash, the road TROne takes for its own
water flag; three bits per vertex in a fifth vertex element of four bytes, the vertex
growing from 20 to 24, and which bit the shader looks at is decided per frame out of
`r->flags`, because a room can flood while a level runs. TROne's `hd.light_stage` was
not carried over: it is an A/B switch whose stage 0 throws the baked `.TRG` light away
for a flat `vec3(0.75)`, a debugging aid for a renderer whose classic path is a
fallback, and here `hd_enable` already gives that comparison. The 16 bulbs occupy pixel
registers `c40`-`c71`, two per bulb, uploaded once per frame by `HDRenderFrameBegin`
with `SetPixelShaderConstantF(40, ...)`, and both pixel shaders share that upload and
must agree (`hd_room.hlsl`, `hd_model.hlsl`); the pixel shaders come to 355 (room) and
340 (model) of 512 slots at that point, about 4.5 slots per bulb. The additive ceiling
is keyed on `gPass.y` in both shaders; `MaterialAddWeight` (`hd_render.cpp`) reads the
weights, 20 to 131 parts per level take one and the smallest is 0.025, the sun shafts
being keyed on the one tuple carrying `param[3]` = 16 seconds (34 parts in three levels)
and brought to a mean of 1.7 per cent of white with a peak of 15.7, under the 16 to 31
per cent a reference streak reads; where the file gives no usable number a part falls
through to 1 or to its preset median, and materials 0, 1, 6, 7, 8, 13 and the
destructible high-bit numbers draw exactly as they did. The swing dies with `hd_motion`
0. The fog bulb chord is weighted with a `smoothstep` over its own range, rim weight
`kRim` = 0.35f, in the shader and in its twin in `hd_light.cpp` behind a `soften` flag
so `RoomProbe -paritycheck` can run both formulas side by side; what remains of the cone
is the half that is exact rather than conservative, `P.z + rad <= 0` on the bulb, which
also settles the camera-inside-the-bulb case, and the branch that returned a density for
a bulb behind the camera now returns zero. The deviation is named rather than hidden:
the classic path culls fog at 45 degrees off axis and this one does not. The model shader
takes the same bulb term, and a material 2 swing under half a second stands still: see
"The light shafts compose as screen, a stacked sea is drawn once, a fast swing stands
still" below. A step in the
baked light at a room boundary is the art's own - the HD path culls a dynamic light
against the whole room's box where the classic path culls against a roomlet's, with the
same test and the same falloff, and across four measured portals the two sides' baked
light at the same world position differs by up to 0.234 on a 0..2 scale with no side
above 1. Item lighting: `HD_MAX_OBJ_LIGHTS` in `specific/hd/hd_model.h` (mirrored in
`hd_model.hlsl`), the item lighting and the `HDLOG creaturelight` line in
`specific/hd/hd_object.cpp`, Lara's own list in `hd_laradraw.cpp`, the `clipped=` count
on the `HDLOG frame` line in `hd_level.cpp`. Seams behind `-log`: `clipped` counted for
a whole level - one line at the first frame that clips, naming the room and how many
lights and bulbs were offered, and one total at the level's end, both `Fill...Lighting`
functions counting what they turn away instead of stopping at the cap - `HDLOG
creaturelight` once a game second for up to six `objects[].intelligent` items with the
classic colour, the HD colour, their difference, the distance to the lens, the ambient
and the light counts, and the level's `motion=` line with the weighted parts, the
smallest weight and the swinging parts. Tools: `RoomProbe` (`-paritycheck`, the `additive:`
line, a bulb dropped at the new maximum) and `ModelProbe -name SWAT -shade N`.

**Commits.** `1087750`, `80e070b`, `791d844`, `36c068a`, `c0f9ede`, `a1dbf32`

### The light shafts compose as screen, a stacked sea is drawn once, a fast swing stands still
2026-09-26

**What.** In the HD additive pass the draw calls of `.TRG` material 2 - the light shafts,
lamp cones and window and candle glows - compose as screen, dst + src - dst * src, instead
of the ONE / ONE sum, and are sampled with the anisotropic filter; every other additive
material keeps the sum and the trilinear filter. An additive material 16 part whose
triangles the file carries three or more times, stacked in y, is drawn once. A material 2
part whose swing period (`param[3]`) is under half a second draws still. The model
shader's fog-bulb term is the room shader's: the exact behind-the-camera cull and the
softened rim.

**Why.** The shafts were harsh because their cards stack, not because a card is bright.
ANDREA2 room 131's window shaft is about 130 overlapping cards, 6.2 over the average pixel
and 12 at most; one card reaches at most 0.55 x 1.0 x 0.83 = 0.46 of white, but the
unclamped sum averages 1.22 and peaks at 3.54 times white, so 56.7 per cent of the shaft's
pixels clipped in the additive pass alone and the core was a flat white slab crossed by
the cards' own edges. RICH1 room 3's grate shaft is 405 triangles in one square whose
vertex light is 2.0 at every vertex, capped to a flat 1.0: 25.8 per cent of its additive
pixels were white and 51.9 per cent of the full frame over it, Lara inside it washed out.
The harsh cyan of JOBY4 room 19 was mostly its sea: group 19 part 3 is 73,728 additive
triangles, four exact copies of 18,048 footprints at y -4269, -4253 and twice -4237, drawn
at weight 1 with a vertex red of 0, so green and blue clipped on 40.2 per cent of the
ceiling view's additive pixels, none of them white - the hue locked to cyan; JOBY5 carries
the same fourfold sheet in groups 65 and 82 and a threefold one in group 117. ANDREA2's
0.06 s period made its swing a 16.7 Hz flicker of +-1.4 per cent. And the model shader's
own bulb term kept the classic 45-degree cone, whose per-pixel edge removed haze worth up
to 78.7 of 255 from 2.4 per cent of a first-person frame at fov 100 (5.0 with the camera
inside the bulb), and the raw chord, whose rim stood up to 11.2 of 255 brighter than the
room term. Both bulb figures come from the two terms transcribed to numpy over a flat
wall in a 640 x 360 frame: for the cone the wall 4000 units ahead at fov 100 and a bulb of
radius 2048 at (3500, 0, 3000) in view space, at (1000, 0, 800) for the camera inside; for
the rim the wall 6000 units ahead at fov 80 and a bulb of radius 3072 at (0, 0, 5000);
density 0.02 in each. The measurement behind every other number is RoomProbe on the
additive pass over black, and a footprint count over the fifteen `.TRG` files through the
vendored TRForge reader, which RoomProbe's `additive:` block confirms level by level.

**How.** `specific/hd/hd_render.cpp`:
- The screen blend: `draw_pass` (`:4113`-`:4125` set up, `:4350`-`:4370` per range,
  `:4403`-`:4414` at the end of the pass) sets `D3DRS_SRCBLEND` `D3DBLEND_INVDESTCOLOR`
  against the pass's `D3DBLEND_ONE` when a run of material 2 draw calls starts and puts
  `D3DBLEND_ONE` back when it ends; src * (1 - dst) + dst is the screen. The sampler goes
  to `D3DSAMP_MINFILTER` `D3DTEXF_ANISOTROPIC` with `D3DSAMP_MAXANISOTROPY` min(16,
  `MaxAnisotropy`) for the same run, when the device reports `D3DPRASTERCAPS_ANISOTROPY`
  and `D3DPTFILTERCAPS_MINFANISOTROPIC`, and back to `D3DTEXF_LINEAR` and the anisotropy
  it found. The screen composition of the glow is TROne's own rule: TROne (GPLv3) by
  Todesengel1931 - https://github.com/todesengel1931, credited at the blend (`:4089`).
  Screen and the sum do not commute, so where a glow overlaps another additive material
  (JOBY4 room 19's rays over its sea) the picture follows the draw order: within a group
  the sort key's, led by the page slot, and between groups the order of the groups.
- Every additive draw call holds one material: `HDTri::material` and `HDRange::material`,
  `MaterialBase(part)` in pass 2 and 0 in the other two passes (`:2766`), in the sort key
  after the destructible id (`:3134`) and in the range merge.
- The stacked sea: `MarkStackedCopies` (`:485`) builds a footprint key per triangle of an
  additive material 16 run - the corners' x and z in (x, z) order and the two other
  corners' height above the first, so only an exact vertical translate is a copy - sorts
  the keys, and where a footprint holds three or more triangles marks every one whose y
  sum is above the smallest (the topmost copy stays, y counting downwards). The build loop
  calls it per part (`:2758`) and skips a marked triangle (`:2997`) after the
  horizontal-plane rows, so the water-surface tables still see every copy. Two-sheet
  stacks never qualify.
- The period fence: `MaterialAddPeriod` (`:439`) returns 0, a still part, below
  `kAddStillBelow` = 0.5 s (`:437`); 0.05 to 60 s stays the range that is read at all.
- Diagnostics for the tools alone, never called by the engine: `HDRenderStackOverride`,
  `HDRenderSwingOverride`, `HDRenderAdditiveOnly` and `HDRenderGlowOverride`
  (`:1905`-`:1924`, declared in `hd_render.h`), and three counts in `HDRenderStats`
  (`addStillParts`, `stackedParts`, `stackedTris`).

`specific/hd/hd_shader/hd_model.hlsl`: `BulbIntensity` culls on `P.z + rad <= 0` (`:491`)
and weights the chord with `smoothstep(0, kRim, chord / diameter)`, `kRim` 0.35
(`:558`-`:563`), line for line `hd_room.hlsl:777` and `:864`-`:869`; `hd_model_ps.inc` and
`hd_model_vs.inc` regenerated by `bshader.bat`, the model pixel shader at 353 instruction
slots (348 before).

Tools: `RoomProbe -glow` (`Preparation/Tools/Diagnose/RoomProbe.cpp:4866`) draws the
additive pass of every drawn group again after the full frame, alone over black, once over
the frame's depth and once over a cleared one, each in its own checked state bracket;
`-glowblend add|screen`, `-aniso 0|1`, `-fastswing`, `-stackedseas` and `-addmat <n>`
(`:3457`-`:3474`) draw the before pictures and one material's glow out of the same build,
and the `additive:` block prints the new counts (`:4284`). `ModelProbe -bulb <x y z radius
density [r g b]>` (`ModelProbe.cpp:8777`, `PutBulbs` at `:6299`) puts view-space fog bulbs
into the frame's lighting block.

**Checked.** A fixed list of 46 views (report views of ANDREA2 131, RICH1 3, JOBY4 19 and
TITLE 6, ANDREA2 131 from 4811 -1250 5864, TITLE 6 grazing, JOBY5 65 and 82 looking up,
and one view per material 2 grouping of the fifteen levels, 36 of them, three of which -
two candle halos and a RICH1 lamp cone - no camera in the room sees) drawn by RoomProbe
before (the build of the tools commit) and after.
- The gate: on the six report views the additive pass alone reproduces the earlier
  statistics exactly - ANDREA2 side coverage 19.1 per cent, mean 190.0, clipped 56.7,
  white 17.3; inside 85.6, 153.4, 27.4, 0.0; RICH1 corridor 12.7, 158.0, 25.8, 25.8;
  inside 88.1, 163.0, 18.4, 18.4; JOBY4 98.5, 128.1, 40.2, 0.0; TITLE 69.3, 22.9, 0.0,
  0.0. The depth-kept glow is cut by the scene where the depth-free one is not: RICH1's
  grate slats and the corridor's lower edge take 11.5 per cent of the shaft, TITLE's tomb,
  trees and gravestones 14.9, ANDREA2's floor the foot of the shaft (1,536 pixels in the
  4811 view); ANDREA2's pillars stand behind the shaft and cut nothing.
- The metrics over the glow's own pixels (glow alone over the scene's depth; clipped = any
  channel at 250 or more, white = all three, hue-pinned = two at 250 or more and one at 0,
  steps = neighbour pairs more than 40 of 255 apart; before -> after):

  | view | clipped | white | pinned | steps | mean /255 |
  |---|---|---|---|---|---|
  | ANDREA2 131 side | 48.7->0.7 | 14.4->0.0 | 0->0 | 7121->2633 | 174.8->139.7 |
  | ANDREA2 131 inside | 25.7->0.0 | 0.0->0.0 | 0->0 | 6160->3488 | 147.1->119.9 |
  | ANDREA2 131 4811 | 36.5->0.3 | 7.6->0.0 | 0->0 | 10145->4413 | 149.3->119.4 |
  | RICH1 3 corridor | 26.1->0.1 | 26.1->0.1 | 0->0 | 3878->2097 | 159.4->125.7 |
  | RICH1 3 inside | 18.3->0.0 | 18.3->0.0 | 0->0 | 4853->2614 | 163.4->127.4 |
  | JOBY4 19 up | 40.2->0.2 | 0.0->0.0 | 0.4->0 | 11849->1023 | 128.1->77.4 |
  | TITLE 6 church | 0.0->0.0 | 0.0->0.0 | 0->0 | 1385->898 | 28.8->27.6 |
  | TITLE 6 grazing | 0.0->0.0 | 0.0->0.0 | 0->0 | 762->558 | 19.1->18.7 |
  | JOBY5 65 sea view | 16.7->0.0 | 0.0->0.0 | 0->0 | 3258->2814 | 110.0->56.0 |

  The full frame over the same pixels: ANDREA2 side clipped 65.9 -> 1.9 and white 20.3 ->
  0.0, RICH1 corridor white 57.4 -> 0.8, RICH1 inside white 57.2 -> 1.4. Over all 46 views
  the largest clipped share left is 1.7 per cent (ANDREA2 room 225's window glow, 60.0
  before).
- The brightness rule, material 2 alone (`-addmat 2`) over the same pixels before and
  after: no view falls under 55 per cent of its old mean; the lowest are ANDREA3 room 34's
  window glow at 73.3, RICH1 3 inside at 78.0 and the RICH1 corridor at 78.8. No preset
  weight changed.
- The anisotropic filter, the after build with `-aniso 0` against `-aniso 1`, glow alone
  over the scene's depth: the change is slight. At most 9 of 255 in TITLE 6's church view
  (148 of its 546,610 glow pixels by more than 4), 7 in the grazing view (63), 12 in
  ANDREA2 131's side view (697) and 15 in the view from 4811 -1250 5864 (2,181 of 164,685,
  1.3 per cent); side by side the pictures are not told apart by eye.
- The loader finds what the file table says: JOBY4 1 part, 55,296 copies left out of
  73,728; JOBY5 3 parts, 111,312 left out (groups 65 and 82 55,296 each, group 117 720 of
  1,080). No other level has one. The fence stills 85 material 2 parts: ANDREA1 1, ANDREA2
  9, ANDREA3 19, ANDY1 2, ANDY2 24, ANDY3 9, RICH1 5, TITLE 16.
- Identity: the after build with `-stackedseas -fastswing -glowblend add -aniso 0` draws
  all six pictures of all 46 views md5-identical to the before build (full, opaque, alpha,
  additive, glow, glow without depth). The opaque and alpha pictures of all 46 views are
  identical before and after. Every pixel the additive pictures change lies where material
  2 or 16 draws (the `-addmat` pictures as the mask), in every view; material 16 changes
  only in the five views of the stacked seas (JOBY4 room 19 twice, JOBY5 rooms 65, 82 and
  the 65 sweep view). The state bracket passes in all 46 views, for the frame and for both
  glow pictures. `hd_room_ps.inc` is byte-identical (md5
  `4a0664f86372905f5c45697410b748f3`).
- Q5: ModelProbe `-lara ANDREA1 -shade 0 -bulb 60 -120 360 220 0.4`, the bulb between the
  camera and her: all 114,363 model pixels within 1 of 255 of a numpy transcription of the
  room term; the old shader against the old term the same, and the two shaders differ by
  up to 16 of 255 on 2.7 per cent of her pixels, the rim.
- ModernTest 10808, MenuTest 35547, InterpTest 635 checks, LipsTest 270 with the `.OBJ`
  folder and 243 without, 0 failed. Not measured: the game.

**Unchanged.** `hd_enable` 0: nothing of the HD path runs (`HDEnabled`, `hd_level.cpp`),
and no file outside `hd_render.cpp`, `hd_render.h`, `hd_model.hlsl` and its two `.inc`
changed in the engine. The classic path, the classic buckets and the interpolator. The
room pixel shader. Every additive material other than 2, and material 16 wherever it is
not stacked three times - the double sheets two units apart stay double. The swing of
every part at half a second or more.

**Decisions.** `param[3]` stays a period, and anything under 0.5 s draws still. Screen for
material 2 only; every other additive material keeps ONE / ONE. The presets stay unless a
shaft's mean falls under 55 per cent of before, and then that preset's weight rises until
it does not, never past 1 per cent clipped - none did. A sea stacked three times or more
is drawn once. The depth fade, and the near, `|N.V|`, edge and end fades of a dedicated
glow shader, come in a later round; no face culling of material 2. No sun rays. No fine
modulation. The model shader takes the room's bulb term. The four fades of the glow
shader are built in the entry that follows, "The light shafts have a pixel shader of
their own".

**Commits.** `e97cfb6`, `7857a97`, `ed53a0a`, `9ba3a2e`

### The light shafts have a pixel shader of their own
2026-09-26

**What.** The additive draw calls of `.TRG` material 2 - the light shafts, lamp cones and
window and candle glows - are drawn with a pixel shader of their own, `hd_glow.hlsl`,
behind the room's own vertex shader. It draws what the room pixel shader draws in the
additive pass and multiplies four fades onto it: near the eye, where a card is seen
edge-on (`|N.V|`), at the card sides of a part cut out of the middle of its page, and at
both ends of an asset cut out of its page along v. Three weight presets of material 2
are raised where the fades would take a glow under 55 per cent of the mean the sum
drew. Every other additive material, material 16 included, is drawn by the room shader
as before.

**Why.** After screen composition the clipping was gone, and what stayed was measured
in the same fixed views (RoomProbe `-glow`, the glow alone over the scene's depth):
- The cards' own edges. Neighbour steps of more than 40 of 255 stood at 2633 in ANDREA2
  room 131's side view, 3488 inside it, 4413 from 4811 -1250 5864, 2097 in RICH1 room
  3's corridor view and 2614 inside the grate shaft, and at 6386 in the sweep view of
  ANDREA2 131. The window shaft takes u 48..206 of page 3038, whose columns there still
  carry 61 to 64 per cent of the brightest one, so every card side is a line.
- The camera inside a shaft: the glow's mean over its pixels was 119.9 of 255 inside
  ANDREA2's shaft and 127.4 inside RICH1's, the additive pass covering 85.6 and 88.1 per
  cent of the frame.
- A card seen in its own plane: TITLE room 6 from 6013 -151 3599 shows its streaks
  converging on the plane's vanishing point.
- RICH1's grate shaft is a cylinder of cards whose vertex light is a flat 1.0, a block
  of equal brightness with the rings of its foot showing, where `|N.V|` gives the chord
  profile of a column.

**How.** `specific/hd/hd_shader/hd_glow.hlsl` (new) includes `hd_room.hlsl` and so reads
the same `VS_OUT`, registers and `BulbIntensity`; `GlowPS` is `RoomPS` in the additive
pass - texel, alpha test, vertex and dynamic light, WaterTable term, distance fog, the
ceiling of one, the bulbs' darkening, the caustic, texel alpha, weight and swing - less
the ripple, the gloss and the bulbs' added colour, which are zero on these draw calls,
and times the fades (`:188`-`:212`):
- near: `smoothstep` over `HD_GLOW_NEAR` = 1024 units of `|viewPos|` (`:73`, `:190`);
- facing: `|N.V|` with the interpolated file normal, or with the face normal out of
  `ddx`/`ddy` of the view position (`:202`), chosen per part by a constant;
- edge: `smoothstep` over the outer 15 per cent of the part's used u range (`:205`);
- ends: `smoothstep` over the fade length at both ends of the asset along its page-v
  axis, `dot(roomPos, axis)` against the asset's ends in the drawn room's frame (`:209`).

The fade near the eye and the `|N.V|` term are TROne's rules for its glow material:
TROne (GPLv3) by Todesengel1931 - https://github.com/todesengel1931, credited in the
shader's header; the file-normal variant, the edge and the end fade are this project's.
The per-draw constants sit in c77 to c79 (`:98`), which no pixel shader of the renderer
reads or holds a literal in; the glow shader's own literals take c0 to c15 outside c4
and c10 and c36 to c39, so gRipple (c36) is written again when the room shader comes
back. `hd_glow_ps.inc` is 479 instruction slots; `bshader.bat` compiles it with the
others (`:53`, stamp `:64`) and `be.bat` guards it (`:56`, `:76`).

`specific/hd/hd_render.cpp`:
- `BuildGlowParts` (`:1516`) runs before the build loop (`:3053`) and takes per material
  2 part with additive triangles: the u and v range; the edge fade where u spans less than
  90 per cent of the page (`:1652`); the face normal where `|N.f|` is under 0.9 on more
  than half of the part's corners (`:1653`); the page-v axis as the mean of dP/dv over
  its triangles. Parts of one tuple and one page whose world boxes come within a sector
  (`kGlowTouch`, `:1490`) join one asset (`:1707`); the rooms' origins for that come with
  the room boxes, `HDRoomBox::originX/originZ` (`hd_render.h:210`, filled at
  `hd_level.cpp:1085` and `RoomProbe.cpp:3909`), and without them an asset is one
  group's parts. The asset's axis is the sum of its parts', its ends the extent of all
  their vertices along it; the end fade is 512 units (`kGlowEndFade`, `:1488`), at most a
  quarter of the extent, and only for an asset with a part that uses less than 90 per
  cent of the page's v (`:1756`). `GlowConstants` (`:1818`) turns a part into the three
  registers; a fade that does not apply gets a range the pixel never leaves.
- The glow part is in the additive sort key after the material (`:3650`) and in the range
  merge (`:3722`); the parts are numbered in file order, so the triangle order is the one
  before.
- `draw_pass` switches to the glow shader for a range with a glow part and back to the
  room shader for any other and at the end of the pass (`:4780`-`:4798`, `:4965`), the
  per-part registers written when the part changes.
- `GlowGain` (`:472`) multiplies the weight of a glow part by its preset's gain (`:3277`).
- `HDRenderGlowOverride` takes a third switch, `noShader` (`:2383`), and
  `HDRenderGlowPartCount` / `HDRenderGlowPartInfo` hand the decisions to the tool;
  `HDRenderStats` counts `glowParts`, `glowEdgeParts`, `glowFaceParts`, `glowAssets`.

Tools: `RoomProbe -glowps 0|1` (`RoomProbe.cpp:3480`) draws material 2 with the room
shader (0) or the glow shader (1, the renderer's own), and the additive block prints a
`glow:` line and one `glow part:` line per part (`:4303`, `:4314`).

**Checked.** The fixed list of 46 views of the first half, six pictures each (full,
opaque, alpha, additive, glow, glow without depth), the build of `d929ed1` against this
one.
- Identity: with `-glowps 0` all 276 pictures are md5-identical to `d929ed1`'s (decoded
  pixels). With the fades taken out of a scratch copy of the shader and the gains at 1,
  the glow shader draws the additive, glow and depth-free glow pictures of all 46 views
  pixel-identical to the room shader. The opaque and alpha pictures are identical before
  and after in every view; every changed additive pixel lies where material 2 draws, and
  material 16 alone (`-addmat 16`) is identical in all 46 views. The state bracket
  passes in all 46 views for the frame and both glow pictures.
- The loader, over the fifteen levels: 253 material 2 parts in 182 assets; 13 with the
  edge fade (ANDREA1 room 157, ANDREA2 rooms 93, 131 and 217, ANDREA3's eight parts of
  page 3037, ANDY2 room 34); 31 on the face normal (JOBY2's 21 lamp cones, ANDREA3's
  seven window slabs of page 3232, ANDY2 room 34, ANDY3 room 150, TITLE room 43); 94
  parts in 78 assets with an end fade. ANDREA2's window shaft is one asset of rooms 93,
  131 and 217, axis (0.657, 0.680, -0.326), 6097 units long.
- The metrics over the glow's own pixels (as in the entry before; steps and mean,
  before the first half -> after it -> now; mean in per cent of the first column):

  | view | steps | mean /255 | now |
  |---|---|---|---|
  | ANDREA2 131 side | 7121->2633->805 | 174.8->139.7->130.1 | 74.5 |
  | ANDREA2 131 inside | 6160->3488->0 | 147.1->119.9->28.1 | 19.1 |
  | ANDREA2 131 4811 | 10145->4413->1449 | 149.3->119.4->116.1 | 77.7 |
  | RICH1 3 corridor | 3878->2097->552 | 159.4->125.7->121.4 | 76.2 |
  | RICH1 3 inside | 4853->2614->94 | 163.4->127.4->33.9 | 20.7 |
  | JOBY4 19 up | 11849->1023->186 | 128.1->77.4->68.6 | 53.5 |
  | TITLE 6 church | 1385->898->22 | 28.8->27.6->22.7 | 78.6 |
  | TITLE 6 grazing | 762->558->0 | 19.1->18.7->15.4 | 80.5 |

  On all 46 views the clipped, white and hue-pinned shares of the glow stay at 1 per
  cent or less; the largest is ANDREA3 room 34's window glow at 1.0 clipped and white.
  The steps of the eight views above fall to at most half of the first half's. Inside
  the two shafts the mean falls and ANDREA2's frame has no white pixel; RICH1's inside
  view keeps 1.11 per cent of its frame white, 1.10 per cent with no additive pass drawn
  at all - the room itself - and the glow adds 0.01.
- The brightness rule, material 2 alone (`-addmat 2`), the mean over its own pixels
  against the sum before the first half: every outside view at 55 per cent or more but
  two, ANDREA2 131's sweep view from 2560 -717 3072, looking down through the shaft's
  fan at 52.1, and ANDY2 room 34 at 53.8; the gains that would lift them clip more than
  1 per cent elsewhere. With every material drawn, JOBY4 19 up falls from 60.4 to 53.5
  per cent of before the first half with the fades (68.6 of 128.1); its preset
  (0.5, 0.6) stops at 1.25 because of the clip limit below, and material 2 alone holds
  71.9 there. The JOBY5 sea views and the sweep views of JOBY4 room 19 and JOBY5 room 65
  stood under 55 per cent already after the entry before, which draws the stacked seas
  once (41.0, 40.9, 49.9 and 50.9 per cent then, 34.1, 34.3, 42.6 and 41.6 now);
  material 2 alone holds 78.8, 79.5, 68.5 and 65.0 there.
- The mean over the glow's own pixels rises when faint pixels drop out, so it hides a glow
  that fades away as a whole. Over the pixels material 2 covered before the first half
  (more than 8 of 255; the mean over those fixed pixels in per cent of before, and in
  brackets the share of them still above 8): ANDREA1 room 19, the haze of page 3232
  (preset (0.5, 0.1, 0, 0.6633), 38 parts in 38 rooms) 1.1 (1.1), where the own-pixel mean
  reads 70.7; ANDY2 room 6, the church mist of page 2100 (1, 0.2, 2, 1) 29.9 (1.4), own
  133.9; TITLE room 8, page 2146, 50.8 (53.5); RICH2 room 78, page 3225, 43.6 (32.1);
  TITLE room 6, the church view, 69.2 over the whole frame and 37.8 in its right third,
  and its grazing view 39.9 (37.0), because `|N.V|` fades the fan seen edge-on at the
  horizon. Scratch builds with one fade off at a time name the fades: without the near
  fade ANDREA1 comes back to 29.9 and ANDY2 to 55.1, without `|N.V|` to 4.5 and 65.9, so
  the haze and the mist go under both together; TITLE room 8 comes back whole without
  `|N.V|` (100.9) and does not change without the near fade; RICH2 room 78 comes back to
  79.9 without `|N.V|` and to 55.0 without the end fade. At full size the room-wide haze
  of ANDREA1 and the mist between ANDY2 room 6's window beams and over its floor are gone;
  the lamp cones and the beams themselves stay.
- The gains (`GlowGain`), searched in steps against the rule: (1.5, 0.4) 1.45, ANDREA2
  room 68's lamp cones 50.0 -> 55.4 per cent; (0.5, 0.6) 1.25, where ANDREA3 room 66's
  shaft reaches 0.79 and ANDREA2 room 225's window glow 0.63 per cent clipped (1.6 and
  1.8 at 1.3); (0.5, 0.8) 1.03, where ANDREA3 room 34's window glow reaches 0.98.
- The end fade: over an asset that uses the page's whole v it is left out, because such
  a page fades by itself - JOBY2's lamp cones of page 3070 lost the light at the lamp
  under it (no glow pixel above 8 of 255 in the room 17 view, 13.1 on average before).
- `hd_room_ps.inc` `4a0664f86372905f5c45697410b748f3`, `hd_room_vs.inc`
  `bff9e930306fc0dfad80d1776a066032` and `hd_model_ps.inc`
  `2e3867532e2e3971991f5bd039227d2e` unchanged; `hd_glow_ps.inc` 479 instruction slots.
- ModernTest 10808, MenuTest 35547, InterpTest 635 checks, LipsTest 270 with the `.OBJ`
  folder and 243 without, 0 failed. Not measured: the game; the first things to look at
  there are ANDREA1's haze in every room, the mist of ANDY2 room 6, the title's right-hand
  ray fan at the horizon, then the inside of ANDREA2's shaft.

**Unchanged.** `hd_enable` 0: the glow shader is created in `HDRenderInit` and the
parts are taken in `HDRenderLevelBegin`, both reached only behind `HDEnabled`
(`hd_level.cpp`), and the origins are filled in `BuildRoomBoxes` behind the same guard.
The classic path, the classic buckets and the interpolator. The room shaders and the
model pixel shader. Every additive material other than 2, material 16 included: the
fades never touch it. The opaque and alpha passes. Screen composition and the
anisotropic filter as the first half built them.

**Decisions.** The fade near the eye works within 1024 units. The fades belong to
material 2 alone. `|N.V|` takes the file normal where it agrees with the faces and the
face normal elsewhere. The end fade takes the asset's whole extent, so that the parts
of one shaft do not seam at the room borders, and only where the art is cut out of its
page in v. The presets stay unless the 55 per cent rule raises one, never past 1 per
cent clipped; three are raised. The depth fade where a shaft meets a wall comes in a
later round with a depth texture. No sun rays, no fine modulation, no face culling of
material 2.

Open: whether a haze or mist part is spared the near fade and `|N.V|`. A rule on parts
that fill their page in u and in v (90 per cent or more of both) takes ANDY2's page
2100 and TITLE's page 2146, but not ANDREA1's page 3232, whose 38 parts use u 1..254
and v 159..248, 35 per cent of the page's v. The entry that follows, "The light shafts
are as faint as the church mist", settles it - no part is spared - and sets the gains
anew against that mist in place of the 55 per cent rule.

**Commits.** `8b175ad`

### The light shafts are as faint as the church mist
2026-09-27

**What.** The glow shader's gain on a `.TRG` material 2 part is set per preset - the
whole parameter tuple - so that every light shaft, ray fan, lamp cone and window glow
shows at the strength of the church mist of ANDY2 room 6: in its brightest view it is
no brighter than that mist, barely visible. The mist itself, the candle halos beside
it, ANDREA1's haze and RICH2 room 78's glow keep a gain of 1.

**Why.** The mist of page 2100, preset (1, 0.2, 2, 1), is the strength that is right
as the game draws it after the fades, and it is the measure here.
- The mist's level. RoomProbe over ANDY2 room 6 from 8150 7000 2100, yaw -94, pitch 2.2,
  fov 86.2 - the view of a recording of that room at 2.1 seconds, matched by the gradient
  correlation of the pictures (0.40 at 320 x 180; window, arch and post within about 20
  pixels at 1280 x 720). Over the pixels the mist's glow lights above 2 of 255 (25.7 per
  cent of the frame) its light is 5.2 on average and 8.0 at the 95th percentile, and its
  share of the frame - the frame with material 2 drawn against the frame with no additive
  material, in luma - is 5.1 on average and 8.0 at the 95th percentile, 20.7 per cent of
  the frame's luma there. Above 8 of 255 lie only 0.84 per cent of the frame - the near
  candle halos (29 per cent of those pixels) and two brighter patches of the mist by the
  far wall: mean 12.6, 95th percentile 23.3, 30.9 per cent of the frame's luma. As a
  picture measurement, the recording shows the mist 4.55 +- 0.54 of 255 above the ring 16
  to 48 pixels around it, over 19 steady frames, with the scene's own contrast taken out
  through RoomProbe's frame without additive material; RoomProbe's own frame gives 4.45
  over the same pixels. The raw lift over the ring is only about 1 of 255; most of the
  4.55 is the background correction taken from RoomProbe's frame, so the agreement rests
  largely on RoomProbe itself.
- The flash that comes now and then in that room is not a gain of material 2. The
  recording shows bursts of 0.8 to 1.3 seconds, three to four seconds apart, that step at
  30 Hz, the logic tick; each lifts the lit surfaces of the room's middle and right (up to
  2.3 times, the left wall not at all, the texture showing in the difference, so light is
  added) and with them the beams of page 2100, because the glow shader multiplies the page
  by the room's light. No file of the HD path reads the storm's `LightningRGB`
  (`game/draw.cpp`), and the source of the burst was not isolated; the bursts' length and
  spacing do match the storm clock there (`LightningCount` runs 16 to 47 ticks, a strike
  starts with a chance of 1 in 128 per tick), so the storm reaches the room by some path
  other than the sky's. It is left as it is.
- The Remaster's picture, measured on recordings of the Remaster and of this build.
  The title flyby is spline sequence 1 of the TITLE `.TRC`'s spot cameras, computed
  as `game/spotcam.cpp` does and drawn by RoomProbe with the roll applied after;
  the matched ticks correlate 0.46 to 0.73 with this build's recording, the
  Remaster's frames 0.40 to 0.72 with it, at a lag of 0.65 seconds. The far ray fan
  of page 2146 seen past the churchyard wall (9.2, 9.8 and 10.4 seconds) stands 20.0
  of 255 above its ring in this build's recording and -5.3 +- 2.0 in the Remaster's:
  no lift at all within the background's own error, a ratio of 0.1 or less. The fans
  over the sky (1.0 to 2.6 seconds) cover 26 to 38 per cent of the frame, their ring
  lies on the church and the ground, and they could not be measured. ANDREA2 room 131
  could not be matched: this build's recording only roughly, the Remaster's not at all
  (it follows another camera), and the Remaster's recording shows no shaft in that
  room in any frame from 4 to 15 seconds. That scene leans on the mist.
- The rule of the entry before - no preset under 55 per cent of its brightness before
  the fades - kept the shafts far above the mist: the 95th percentile of the share of
  the frame, in each preset's brightest view, was 191.8 in ANDREA2 room 131, 223.5 in
  room 225, 42.6 at the title's ray fans (TITLE room 6 from 7000 -400 1500) against
  the mist's 8.0; in the title flyby RoomProbe put the far fan 15.4 above its ring.
- One factor for all does not do: the gains that reach the mist lie from 0.0128
  (ANDREA3 room 36's window glow) to 0.584 (JOBY5 room 115's glow), ANDREA2 room
  131's shaft at 0.0273 and the title's fans at 0.171.

**How.** `specific/hd/hd_render.cpp`: `GlowGain` (`:454`-`:520`) holds 22 entries keyed
by the four parameters exactly as the file stores them (literals checked bit for bit
against all 26 material 2 tuples of the fifteen files); a tuple the table does not
name draws at 1. The gain multiplies the weight of a draw call that carries a glow
part (`:3311`), which exists only for the glow shader: with `-glowps 0`, as in
`HDRenderGlowOverride`'s `noShader`, no glow part is built and no gain applies. Each
gain was found by drawing: the brightest RoomProbe view of the tuple, `-glow -addmat 2`
against `-addmat 99`, the 95th percentile of the share of the frame over the pixels
the glow of 29c4d45 lit above 2, the gain scaled by 8 over it and the view drawn again,
four rounds at most; an 8-bit frame drops a card's share once it falls under half a
step, so the picture does not follow a gain this small in proportion: the window glow
of ANDREA2 room 225 reads 20.0, 10.9, 3.7 and 7.7 at gains of 0.042, 0.0168, 0.0124
and 0.0146.

**Checked.** The fixed list of 46 views of the entries before, six pictures each, the
build of 29c4d45 against this one, and every tuple's calibration view with material 2
alone.
- The calibration: the 95th percentile of the share of the frame lies between 7.71
  and 8.09 in all 21 tuples with a gain below 1 and a view that sees them, the mean
  between 1.16 and 3.83, under the mist's 5.1. RICH2 room 78's glow shows 6.86 at
  gain 1 and is not lowered; RICH1 room 17's glow, which no camera in its room sees,
  takes the gain of its sibling (1, 0.2, 0.1, 0.6).
- The title flyby at the matched ticks: RoomProbe's far fan 15.4 -> 2.4 above its
  ring, x 0.157; this build's recording stood at 1.30 times RoomProbe there, so about
  3 of 255 are to be expected in the game, under the mist's 4.55. Material 2 alone
  over the pixels it lit above 2: mean 12.8 -> 1.4 and 95th percentile 31.7 -> 4.3 at
  9.2 seconds, 16.5 -> 2.2 and 37.0 -> 6.0 at 1.0 seconds.
- ANDREA2 room 131 from the camera nearest this build's recording (5750 -350 1800, yaw
  -79, pitch -3, fov 75): the share of the frame 34.4 -> 0.3 on average, 93.7 -> 1.9 at
  the 95th percentile.
- ANDY2 room 6: all six pictures of the matched view and of the two views of the room
  in the list (the candle halos of page 2002 and the mist of page 2100) are
  byte-identical before and after.
- The 46 views, the glow alone over the scene's depth with every additive material;
  own = mean over the glow's own pixels (a channel above 8), fixed = mean and 95th
  percentile over the pixels the glow lit above 2 before (before -> after, of 255).
  Where material 16 or another additive material shares the view (JOBY4 room 19,
  JOBY5 rooms 65 and 82, JOBY3 room 10, RICHCUT2 room 5) it stays and holds the
  numbers up; where faint pixels drop out the own mean can rise:

  | view | own | fixed mean | fixed p95 |
  |---|---|---|---|
  | ANDREA2 131 side | 130.1 -> 8.2 | 129.2 -> 3.7 | 211.7 -> 8.3 |
  | ANDREA2 131 inside | 28.1 -> 0.0 | 21.7 -> 0.2 | 51.3 -> 1.0 |
  | RICH1 3 corridor | 121.4 -> 11.7 | 115.3 -> 4.4 | 221.0 -> 12.0 |
  | RICH1 3 inside | 33.9 -> 0.0 | 28.5 -> 0.3 | 75.0 -> 2.0 |
  | JOBY4 19 up | 68.6 -> 37.5 | 68.0 -> 23.3 | 128.3 -> 64.3 |
  | TITLE 6 church | 22.7 -> 9.0 | 18.1 -> 2.5 | 46.7 -> 8.0 |
  | ANDREA2 131 4811 | 116.1 -> 8.0 | 113.5 -> 2.9 | 204.7 -> 7.7 |
  | TITLE 6 grazing | 15.4 -> 0.0 | 11.3 -> 1.0 | 24.3 -> 3.3 |
  | JOBY5 65 up | 11.2 -> 8.9 | 9.6 -> 6.5 | 16.0 -> 11.7 |
  | JOBY5 82 up | 11.1 -> 8.9 | 9.5 -> 6.5 | 16.0 -> 11.7 |
  | ANDREA1 19 3232 (0.5, 0.1, 0, 0.6633) | 11.2 -> 11.2 | 8.0 -> 8.0 | 17.7 -> 17.7 |
  | ANDREA1 64 3070 (1.5, 0.3, 0.4, 16) | 11.5 -> 13.4 | 6.1 -> 1.0 | 14.3 -> 4.7 |
  | ANDREA1 157 3038 (0.5, 0.55, 0.6, 0.13) | 62.3 -> 9.2 | 48.3 -> 2.6 | 138.7 -> 8.3 |
  | ANDREA2 68 3070 (1.5, 0.3, 0.4, 3.0667) | 12.9 -> 8.5 | 10.4 -> 2.8 | 25.3 -> 8.0 |
  | ANDREA2 225 3232 (0.5, 0.4, 0.6, 0.1) | 135.5 -> 8.9 | 128.5 -> 0.9 | 238.7 -> 6.7 |
  | ANDREA2 131 3038 (0.5, 0.55, 0.6, 0.06) | 94.1 -> 0.0 | 93.7 -> 1.9 | 146.7 -> 4.0 |
  | ANDREA2 234 3232 (0.5, 0.4, 0.6, 0.5) | 76.9 -> 8.7 | 70.8 -> 1.6 | 174.7 -> 6.7 |
  | ANDREA3 66 3037 (0.5, 0.55, 0.6, 0.06) | 83.7 -> 9.7 | 77.6 -> 1.9 | 192.3 -> 6.7 |
  | ANDREA3 20 3232 (0.5, 0.4, 0.8, 0.05) | 91.5 -> 8.2 | 85.5 -> 1.8 | 190.7 -> 6.7 |
  | ANDREA3 34 3232 (0.5, 0.4, 0.8, 0.08) | 127.9 -> 15.1 | 127.9 -> 3.9 | 214.0 -> 8.0 |
  | ANDREA3 36 3232 (0.5, 0.4, 0.8, 0.2) | 131.4 -> 8.2 | 130.4 -> 1.4 | 224.7 -> 6.7 |
  | ANDREA3 59 2002 (1, 0.05, 0.1, 0.2) | 15.3 -> 15.3 | 10.4 -> 10.4 | 20.7 -> 20.7 |
  | ANDY1 84 2002 (1, 0.05, 0.1, 0.2) | 13.2 -> 13.2 | 8.9 -> 8.9 | 21.3 -> 21.3 |
  | ANDY1 194 3225 (0.5, 0, 0.8, 0.2133) | 48.5 -> 7.4 | 40.3 -> 2.6 | 88.3 -> 6.7 |
  | ANDY2 6 2002 (1, 0.05, 0.1, 0.2) | 14.2 -> 14.2 | 4.7 -> 4.7 | 8.0 -> 8.0 |
  | ANDY2 6 2100 (1, 0.2, 2, 1) | 17.3 -> 17.3 | 4.7 -> 4.7 | 6.0 -> 6.0 |
  | ANDY2 34 3225 (0.5, 0, 0.8, 0.05) | 50.0 -> 8.0 | 42.7 -> 1.7 | 129.8 -> 7.0 |
  | ANDY3 145 2002 (1, 0.05, 0.1, 0.2) | 0.0 -> 0.0 | 0.0 -> 0.0 | 0.0 -> 0.0 |
  | ANDY3 86 3225 (0.5, 0, 0.8, 0.2133) | 43.5 -> 6.9 | 41.8 -> 2.8 | 84.3 -> 6.7 |
  | JOBY2 17 3070 (1.5, 0.3, 0.4, 16) | 13.4 -> 8.6 | 8.9 -> 0.4 | 19.7 -> 4.0 |
  | JOBY3 10 3225 (1.5, 0.1, 0, 5) | 17.8 -> 20.6 | 13.1 -> 9.0 | 37.0 -> 37.0 |
  | JOBY4 19 3256 (0.5, 0.75, 0.6, 0.8) | 61.2 -> 40.1 | 58.9 -> 33.4 | 115.3 -> 67.3 |
  | JOBY5 115 3225 (1.5, 0.1, 0, 1.5333) | 10.9 -> 9.0 | 7.0 -> 3.9 | 13.7 -> 7.7 |
  | JOBY5 54 3225 (1.5, 0.1, 0, 5) | 11.0 -> 0.0 | 7.2 -> 2.8 | 13.0 -> 5.0 |
  | JOBY5 65 3256 (0.5, 0.75, 0.6, 0.5) | 45.7 -> 27.3 | 44.3 -> 19.3 | 86.0 -> 36.0 |
  | RICH1 3 3038 (0.5, 2.683, 0.6, 0.327) | 127.9 -> 12.1 | 122.9 -> 4.8 | 228.0 -> 14.0 |
  | RICH1 15 3070 (1, 0.2, 0.1, 0.6) | 17.9 -> 12.3 | 13.5 -> 3.1 | 29.0 -> 8.0 |
  | RICH1 17 3070 (1, 0.2, 0.1, 1) | 0.0 -> 0.0 | 0.0 -> 0.0 | 0.0 -> 0.0 |
  | RICH2 120 3070 (1.5, 0.3, 0.4, 2.5) | 30.5 -> 13.7 | 21.5 -> 3.0 | 71.0 -> 11.3 |
  | RICH2 78 3225 (1.5, 0.1, 0, 1.9333) | 11.3 -> 11.3 | 6.4 -> 6.4 | 15.7 -> 15.7 |
  | RICH3 65 3070 (1.5, 0.3, 0.4, 16) | 16.8 -> 9.7 | 9.5 -> 1.9 | 29.3 -> 7.7 |
  | RICHCUT2 5 3070 (1, 0.2, 0.1, 2) | 46.2 -> 50.5 | 36.0 -> 33.3 | 96.3 -> 93.7 |
  | TITLE 4 2002 (1, 0.05, 0.1, 0.2) | 28.3 -> 16.5 | 22.5 -> 3.1 | 57.3 -> 8.7 |
  | TITLE 8 2146 (1, 0.2, 2, 0.35) | 12.2 -> 0.0 | 7.6 -> 0.7 | 17.0 -> 2.7 |
  | TITLE 19 2134 (0.5, 0.2, 1, 0.1) | 28.2 -> 11.5 | 23.2 -> 2.5 | 61.3 -> 8.0 |
  | TITLE 52 3070 (1, 0.2, 0.1, 2) | 16.4 -> 12.0 | 9.3 -> 2.7 | 24.0 -> 8.0 |

- Clipping: no view has more clipped, white or hue-pinned glow pixels than before;
  over all 46 views the clipped pixels fall from 6,889 to 233 and the white from 4,712
  to 58. The largest clipped share is TITLE room 4's view at 1.5 per cent: the same 67
  pixels of candle-flame cores (page 2002, gain 1) as before, now among 4,402 glow
  pixels instead of 38,434; every other view is at 1 per cent or less.
- Identity: with `-glowps 0` all 276 pictures are pixel-identical to those of 29c4d45.
  The opaque and alpha pictures of all 46 views are identical before and after; so
  are the full frame and the glow over the scene's depth of the eight views whose
  tuples keep a gain of 1 or show no glow (ANDREA1 room 19, ANDREA3 room 59, ANDY1
  room 84, ANDY2 room 6 twice, ANDY3 room 145, RICH1 room 17, RICH2 room 78), all six
  pictures of ANDY2 room 6's two, and in the others only a picture drawn without the
  scene's depth changes, where a lowered glow of another room shows through a wall.
  The state bracket passes in all 138 runs (46 views, three runs of this build) for
  the frame and both glow pictures. `hd_glow_ps.inc`
  `881dcbebc8ad4bc15eb1e9552a00bb9f`, `hd_room_ps.inc`
  `4a0664f86372905f5c45697410b748f3`, `hd_room_vs.inc`
  `bff9e930306fc0dfad80d1776a066032` and `hd_model_ps.inc`
  `2e3867532e2e3971991f5bd039227d2e` are unchanged.
- ModernTest 10808, MenuTest 35547, InterpTest 635 checks, LipsTest 270 with the `.OBJ`
  folder and 243 without, 0 failed. Not measured: the game; the first things to look
  at there are ANDREA2 room 131 and the title flyby against the Remaster, ANDY2 room 6
  as before, and the lamp cones and window glows of the other levels.

**Unchanged.** `hd_enable` 0 and `-glowps 0`: no glow part, no gain. The shaders, the
four fades, screen composition, the anisotropic filter, the stacked seas and the
still fast swings. Every additive material other than 2. The data fixes of the rounds
before. The weight each part reads from the art (`MaterialAddWeight`), including the
sun shafts' 0.08: the gain multiplies it.

**Decisions.** The strength follows the church mist and the Remaster's picture, not a
share of the brightness the glow had before its fades: the 55 per cent rule of the
entry before is withdrawn. The haze and mist layers keep the fades: ANDY2's mist at
its faded level is the target, and ANDREA1's page-3232 haze needs no exemption. Each
preset gets its own gain, because the presets need gains 45 times apart; the measure
is the 95th percentile of its share of the frame in its brightest view, 8 of 255 like
the mist's. Nothing is raised above the gain it had. ANDY2 room 6 stays as it is,
the candle halos beside the mist included. The flash in that room stays as it is.

**Commits.** `3eddf54`

### The sky
2026-09-05 - 2026-09-08

**What.** Where a level's `.TRG` carries a sky group, the HD path draws the Remaster's
own sky - a panorama band and one to three cloud discs - in the classic horizon mesh's
place, and the classic horizon mesh and its two flat colour layers are left out; the
moon, the stars and the lightning flash still run, and layer 1 survives the dome when
`GF_LIGHTNING` is set, and only then, because the game paints the flash through that
layer in the lightning's own colour. Rain and snow were never part of this phase and are
untouched. Where a level has no sky group the engine's procedural sky is drawn as before
and the HD walls paint over it: JOBY3 and JOBY5 have none, TITLE has two, and the other
twelve levels have one, always the last group. The sky group's alpha pass blends -
source alpha against one minus source alpha, alpha reference 0 - where every other
alpha-pass group is an alpha test at 0.5, and its depth write stays on.

**Why.** Every `.TRG` whose level has a sky ends in one group past the last room: an
origin-centred dome spanning the full int16 range - ANDREA1 group 242, x -17250..17250,
y -3956..3956, 1580 alpha triangles; RICH1 group 165 the same; JOBY4 group 137, plus or
minus 4098 - real geometry that was at first deliberately not drawn, TROne drawing its
own dome only because its games have no procedural sky. The group holds the band (1,280
triangles on a sphere of about four blocks' radius, 21 rings, radius 1060 at the poles
and 4100 at the equator, wearing one 4096 x 2048 panorama out of the page block
7000..7011, alpha 0 above the horizon line and opaque below it) and one to three flat
cloud discs of up to 24,000 units' radius at fixed heights above the camera, material 1,
which therefore scroll on their own parameters (`p1` -0.03 to 0.06 page units per
second) and carry baked light of 0.26 to 2.00. The band's baked light is exactly zero on
every vertex of every level, because the Remaster's own sky shader does not read it, so
through the room formula it would be black. The two alpha-pass sky pages carry a soft
ramp with every value in between - 42.0 per cent of their texels under alpha 16 and 35.9
per cent over 240 for the second cloud sheet, 33.5 and 59.2 for the dome - and a test at
0.5 leaves a feathered cloud as a hard-edged silhouette and puts a hard rim across the
dome. The depth write stays on, unlike the glass surfaces, because the sky sorts inside
itself in its own depth slice: the dome stands in front of the cloud sheets at the
horizon and behind them overhead.

**How.** A sky part with no light anywhere is drawn at light 1 and its page is the whole
picture. No scaling and no rotation: the sphere's four blocks sit far inside the far
plane of 20480, the discs reach past it, which only cuts a disc where the band already
covers it, and the group is centred on the origin, so it takes the matrix the classic
path builds for its own horizon - `phd_TranslateAbs(camera.pos)` and nothing else - and
rides on the camera exactly as the horizon did. Because the discs hang in front of the
band overhead and behind it at the horizon, no draw order gets both right, so the sky is
given its own depth range instead: the viewport's `MinZ` is pushed to `HD_SKY_MINZ` =
0.996 for the length of the call, folding the whole sky into the last four thousandths
of the depth buffer, where the depth test sorts it properly and outside which every
pixel of the world is nearer, the classic depth of the far plane itself being only 0.995
(`SetupZRange(20, 20480)`: a - b/z = 0.99500); the viewport is read back and put
together again before the call returns. The dome is drawn in `SkyDrawPhase`
(`game/draw.cpp`), in the classic horizon mesh's own place in that function - after the
moon and before the stars, because none of the three writes depth and the order in that
function is the order on the screen; drawn with the rooms in `HDFrameDraw` it would have
painted over the moon. It rides on the classic path's own two gates because it is drawn
inside them - `outside`, which `GetRoomBounds` sets when a room that sees the sky is in
view, and `objects[HORIZON].loaded`, which `SkyDrawPhase` returns on - so a level with
an HD sky group but no classic HORIZON object draws no sky at all. The blend branch is
in `specific/hd/hd_render.cpp:3245`, in the alpha pass state block (`gPass.x` carrying
the alpha reference); only a group past the last room takes it, and `HDRenderRoomMask`
is the one place that still knows a group's number. Texture addressing needed no change:
the renderer already sets `D3DTADDRESS_CLAMP` for every range that does not scroll and
`WRAP` only for the ones that do, which is what TROne's sky finding (a clamp-to-edge
sampler for the painted dome) asks for. The opaque and the alpha pass multiply by
exactly one and the sort key's two new fields are constant across them, so no ordinary
surface can change. `RoomProbe` gained `-sky` and `-skyonly` (whose device-state bracket
reports no watched state left changed after the sky call) and `-groups`; the engine
writes one `HDLOG ... sky groups=` line per level.

**Commits.** `1087750`, `c0f9ede`, `5285f99`

### A floor whose page is part-way clear is drawn blended, and a mirror shows the room's items
2026-09-29

**What.** Two rules of the HD level. The alpha-pass triangles of a room whose page is
part-way clear in every texel - every texel at or above the alpha test's 0.5, the smallest
at most 243 of 255 - are no longer drawn with the alpha test but blended, `SRCALPHA` over
`INVSRCALPHA` with the depth write on, in a phase of their own (`HD_PASS_CLEAR`) after
every room's solid art and before the models and the classic buckets. And the two washroom
mirrors of RICH2 show Lara and the items in front of them: with an HD level active, every
item whose reflection in a mirror's plane lands in the copy of the room behind the glass
is drawn once more through a view reflected in that plane, and Lara the same.

**Why.** The Remaster builds RICH2's polished floors as a floor on pages at alpha 231 to
239 lying over an upside-down copy of the room: the lobby, room 8, is 657 floor triangles
on pages 1047 and 1045 (0.917 on the average) over 2,895 triangles of copy. The alpha test
at 0.5 kept every texel of those pages and drew the floor solid, so the copy was drawn and
never seen. An inventory of every alpha-pass page of the fifteen `.TRG` files, decoded
with the engine's own BC7 reader, finds the class in three levels: RICH2, 13 pages (1029,
1045, 1047, 1049, 1053, 1079 and 1081 at 231 to 239, the inlaid floors 3314 to 3319 at 231
to 255) with 17,989 triangles; RICHCUT2, page 1103 (211 to 255) with 936; TITLE, pages
2147, 2149 and 2154 (174 to 255) with 1,860; no other level has one. The glass pages of
the same range (3068 and 3309, 165 to 247) are material 14 and were blended already. The
mirrors: TR5 has no mirror of its own. Neither the game's SCRIPT.DAT nor the original one
carries a `CMD_MIRROR` or sets `GF_MIRROR` for any level, so `gfMirrorRoom` stays unset
and `Draw_Mirror_Lara` never runs. The Remaster builds its two washroom mirrors out of
art, a copy of the room reflected in the wall behind a pane, and the copy holds nothing
that moves: at the glass a player saw the cabins and a toilet in it, no Lara and no doors,
and looked straight into the cabins.

**How.** `specific/hd/hd_pageclass.h:59`, `HDPageClassOf`, is the class, a pure function
of the smallest and the largest alpha. `hd_pagecache.cpp:682-697` measures both over every
texel of the top mip a page keeps, beside the cut-out share and on the decoding thread;
`:1302` keeps the class per slot and `:1703`, `HDPageCacheSlotClear`, answers it.
`hd_render.cpp:4516` decides per triangle: an alpha-pass triangle of a room group, not
glass already, on a clear page gets blend 1 (its page's own alpha) and `HDRange::clear`.
`draw_pass` draws the clear ranges alone when asked with `clearOnly` (`:5766` the depth
write, `:5780` the filter), `HDRenderRoomMask` draws them after a room's alpha pass
(`:6190`), and `hd_level.cpp:4549` draws `HD_PASS_CLEAR` for every room of the frame that
has one, after the solid loop. `hd_render.cpp:2552`, `FindMirrors`, runs at the level
build (`:5019`) over the four walls of every room's inside box and records a mirror where
the wall has a pane (triangles in its plane in the additive pass or of material 14), a
room's worth of the group's art behind it (100 triangles or more, reaching 1,024 units
deep) and 90 per cent of that art the reflection of the level's art in front to one 8-unit
cell; `HDRenderMirrorInfo` and `hd_level.cpp:4034`, `HDLevelMirrorCount` and
`HDLevelMirrorAt`, hand them to the game. `game/draw.cpp:2498` calls `Draw_Mirror_Items`
(`game/mirror.cpp:284`) after Lara's own draw and before the rooms' objects are printed,
while the mirror room's portal window is still this frame's. For a mirror whose room is on
the draw list and whose glass faces the camera, `DrawOneMirror` (`:155`) copies the view -
`w2v_matrix`, `aW2VMx` and the bases of both matrix stacks - and reflects it
(`game/mirror_math.h`, `MirrorView`: the view's column of the plane's axis negated, the
camera reflected). It draws every item in front of the glass whose reflected origin lands
in the copy's box, with 256 units of slack, through `PrintObjectReflection`
(`game/draw.cpp:1182`, the item loop's body with the mirror's room as the clip room),
moves Lara's fifteen matrices into the reflected view (`MirrorViewSpaceApply`) and draws
her with `DrawLara(lara_item, 1)`, and copies the view back. The classic path's winding
test is turned round with `SetCullCW` as `Draw_Mirror_Lara` turns it; the HD models are
drawn double sided (`hd_object.cpp:3014`) and need nothing, and HD Lara in first person
culls the other side on her mirror flag (`hd_laradraw.cpp:1309`). `HDObjectReflection`
(`hd_object.cpp:3037`) tells the object seam the items are a reflection: they are left out
of the two light checks (`:3023`) and counted for the mirror's log line. A reflected item
casts no shadow - its slot's `shadow_size` is 0 for the call - and its ambient fade, which
`CalcAmbientLight` steps per draw, is put back. The motion line of `HDLOG level=` ends in
` clear_pages=%d clear_parts=%d mirrors=%d`, the level's first frame line in
` clear_calls=%d`; one `HDLOG mirrorwall` line per mirror at the build, and
`HDLOG mirror items=%d plane=%ld room=%d axis=%c lara=%d hd=%d` once a second while a
mirror draws. `RoomProbe` draws the clear phase in its frame, gives the before picture
with `-noclear`, and prints a `clear:` line and, with `-roombox`, the mirrors.

**Checked.** The inventory, by a probe built against the engine's BC7 decoder and the
`.TRG` reader, over all fifteen levels, and the engine's own count through
`RoomProbe -nodraw`: 55 parts on 13 pages and 149 draw calls in RICH2, one part on one
page in one draw call in RICHCUT2, nine parts on three pages in nine draw calls in TITLE,
none elsewhere. Against a `RoomProbe` built from the tree before this entry the vertex,
index and range counts and the glass lines of all fifteen levels are the same, and renders
of JOBY3 room 9, ANDREA2 room 65 and RICH2 room 134 are the same to the pixel. Every RICH2
group with a clear floor holds art beneath its floor's plane, down to the one-sector
doorways (groups 53, 54, 90, 95, 97, 101, 103, 117, 118, 154, 170, 171 and 176); TITLE
groups 50 and 51 hold none. The lobby from the pose
`RICH2 8 4100 -25300 1700 -12 35 -size 1600 900 -roombox`: with `-noclear` the same to the
pixel as the build before, without it 792,628 of 1,440,000 pixels change, all on the
floor, which shows the copy's doorway and a light stripe faintly. The mirrors, through
`RoomProbe -roombox` over the fifteen levels: RICH2 room 134 (plane x = 33792, pane 12,
6,161 triangles behind reaching 2,054 deep, 6,107 reflected; copy x 31738..33785, y
-25607..-24042, z 74758..78848) and room 135 (x = 30720, pane 16, 6,237 behind, 6,201
reflected), nothing else; the rule costs 77 ms of the level build in RICH2 and 13 to 21 ms
in ANDREA1, JOBY2 and JOBY5. The items the rule draws, out of the original RICH2 item
list: in room 134's glass the kick doors 189, 190 and 192 of cabins 131 to 133, the
ceiling trapdoor 188 and the electrical light 191 above them, the three ANIMATING4 items
193 to 195 of the washroom, and Lara; in room 135's the kick doors 199, 200 and 202 of
cabins 136 to 138, the large medipack 201, the ANIMATING4 items 196 to 198, and room 177's
door 246 and generator slot 247; nothing that stands behind either glass. ModernTest pins
the class and the reflection (14,018 checks become 14,048); MenuTest 42,364, InterpTest
683, and LipsTest 270 with the lip objects and 243 without, all without a failure. The
mirror itself cannot be drawn outside the game - `RoomProbe` draws no item - so its
picture is the play test's.

**Unchanged.** `hd_enable 0`: the clear phase is part of `HDFrameDraw`, whose rooms are
the HD ones only, and the page class exists only in the page cache the HD level builds;
`Draw_Mirror_Items` is called behind `HDLevelActive()` (`game/draw.cpp:2498`), false with
`hd_enable` off (`hd_level.cpp:3126`), and `HDLevelMirrorCount` asks it again (`:4036`).
The classic mirror of Lara and its call, the classic item loop, the tick, the logic camera
and the savegames: nothing of them is written - the reflection lives in the view and in
copies of it, and an item's own position and pose are never touched. The three frame-rate
modes alike: the reflection draws the pose the frame draws. Every alpha-pass page outside
the class keeps the alpha test; the glass and the cut-outs of every level are as they
were.

**Decisions.** A rule from the data and no list per level: a page part-way clear in every
texel is meant to be seen through. The class reads the smallest alpha: from the alpha
test's reference, 128, up to 243 - halfway between the highest smallest alpha of a floor
over a copy (233, TITLE page 2149) and the first near-opaque page (254, JOBY3 pages 14070
and 14071, ANDY1 page 793, three sky sheets); a page that also carries texels at 255 stays
in the class, because the floors of TITLE and RICHCUT2 and RICH2's inlaid ones do, and a
texel at 255 blends as solid. The floor keeps the depth write and is drawn in a phase of
its own before the models, not with the glass after everything: Lara stands on it, her
shadow falls 16 units above it, and the glass and the glows of the copy must stay beneath
it. HD only. The mirrors come from the art, because the script has none, and every item
whose reflection lands in the copy is drawn, not a list of objects. The reflection is put
into the view and not into the item, so every item keeps the pose the frame draws: a pose
written into the item needs the -1 scale `CalcLaraMatrices(2)` gives Lara and
`DrawAnimatingItem` does not have, and both mirrors stand in planes of constant x, which
the z plane of `Draw_Mirror_Lara` does not serve. A reflected item casts no shadow of its
own and its effects are not reflected.

**Left.** TITLE groups 50 and 51 carry the class on page 2154 with nothing beneath the
floor in their own art: there the floor blends over whatever the frame holds behind it and
comes out a little darker (`RoomProbe` over black: a mean of 18.1 against 17.0 of 255 in
group 51); whether the title flyby ever shows it is the play test's. Shadows and effects -
flames, sparks, the gun flash - of what the mirror reflects. The lobby floor shows its
static copy only: Lara and the items are not reflected in it, and the Remaster's floor
does not reflect them either. In first person the reflected Lara is drawn the way the
first-person draw draws her, without her face and her braid. Between two rooms the glass
of one may be drawn before the clear floor of the other. The play test judges the lobby's
brightness, the floor's edge, a kicked door swinging open in the glass, and that nothing
is doubled outside it.

**Commits.** `7235cb2`, `0737b1d`, `4f4815e`

### The portal walk, the window on intruding art, and the doorway a randomized run opens
2026-09-07 - 2026-09-11

**What.** In the HD path a room's remaster art is drawn over the whole screen, except
for the triangles that stand inside a *different* room: those are drawn inside the
classic portal window of the room they belong to, as a scissor rectangle. A triangle of
room `b` is windowed when its centroid lies inside the interior of some other room `a` -
TR5's own "does an item fit wholly inside its room" box (`game/draw.cpp:3593-3598`)
pulled one whole sector further in on each side - and `a` is not a portal neighbour of
`b`, not linked to it by a vertical floor link, and not its flipmap twin. Over the
fifteen levels that is 105 room pairs and 81,261 triangles in 90 groups of 2,709, 0.54
per cent of the game's 15,052,011 triangles, and the only draw calls the scissor test is
ever switched on for; the overhang of a group is counted per triangle with 128 units of
slack, `HD_OVERHANG_SLACK`. The engine's own portal walk gained three changes that both
paths share or that the HD path alone uses. On a frame whose camera was interpolated,
`DrawRooms` seeds the walk with the room that *contains* the interpolated eye -
`GetFloor(x, y, z, &room)` walked from the logic room - instead of
`camera.pos.room_number`; nothing is written back, and `outside` and `camera_underwater`
are still read from the logic room. Every portal now gets a second rectangle beside the
classic window, its own projection clamped against the screen with the parent room's
window left out of it: a portal *reaches* the room behind it when it faces the eye and
lands on the screen, and a room that only the reach brought in is drawn with all of its
remaster art over the whole screen and none of its intruding art, its classic geometry
not drawn either. Each room is visited once; above `kReachBudget` = 128 rooms the walk
drops reach-only rooms farthest first, and a room with a classic window of its own is
never dropped. `GetRoomBounds` and `SetRoomBounds` refuse a room beyond the capacity of
`draw_rooms` and `draw_room_list`, unconditionally and counted. When a randomized level
opens a doorway, the room keeps its HD art: the randomizer records the box its edit
opened in the `.hdm` sidecar and the engine drops every HD triangle that really meets
that box. `makePortal`, `addViewWindow`, `makeVerticalPortal` and `clearWallFaces`
record such a box; only `moveRoom` with a dy and `buildRoomGeometry` still mark a whole
room. The sidecar is version 2, one new line `cut <x0> <y0> <z0> <x1> <y1> <z1>` - six
integers, world units, ascending corners, Y growing downward - and a file without one
declares 1 and stays readable by an older engine.

**Why.** Rooms that stand inside one another in the world bleed through each other's
walls and the depth buffer has nothing to say about it, because both rooms really are at
that distance: ANDREA1's room 53, an air shaft, has 45 of its 272 art cells more than
one sector deep inside room 207 with no portal between the two. Scissoring every HD room
to its classic rectangle - which is what the first form of this did - is not equivalent
to the classic clip and cuts away art that belongs in the picture: the classic path
drops a polygon only when every corner lies beyond one edge of the window and otherwise
draws it whole (`specific/polyinsert.cpp:883/1055/1281/1422`), while a scissor cuts at
the pixel, and
a room's border ring stands on the camera's side of the portal plane and from a shallow
angle projects far outside the portal's rectangle - ANDREA1's room 57 lost its pier to a
vertical strip of sky, and ANDREA2's courtyard rooms 134 and 203 met along a horizontal
black band across the palm trunk and the door lintels. The holder of intruding art never
needed a window at all, so the question is asked of the intruding triangle; it is asked
of a *point* because a cell test cannot be asked identically twice,
`HDRenderGroupCellsIn` counting a cell that merely touches the box and so being one
256-unit ring wider than a corner test. The constants are measured: slack 128 sits
between the two clumps of overhang distance - the Remaster's relief within 64 units of
the box edge, the facades and piers a whole sector past it - and takes ANDREA2 from 77.6
per cent overhang to 4.5; pulling the holder's box one sector in takes ANDREA1 from 158
pairs to 7 and ANDREA2 from 148 to 26, and it is what tells two rooms that merely share
a wall from a room that really stands inside another. The eye's own room: at sixty
frames a second the eye is blended per frame while the logic camera's room steps once
per tick, and where the two disagree two pieces of the walk throw a neighbour away - the
side test in `GetRoomBounds` (`if (x && (x ^ p->normal[0]) < 0)`) and `SetRoomBounds`'
`tooNear == 4` - leaving the near room drawn alone with the sky standing in the hole,
its edges dead straight along the portal line, for one frame; measured on ANDREA1's own
portal records, room 2's portal to room 57 lies at world x 48128 with normal -1/0/0, and
a blended eye at x 48200 seeded with room 2 is refused it in 69 of 101 sampled positions
while seeded with room 57 the portal back is followed. At `fps_mode 0` no frame is drawn
from a blended eye, so the two can never disagree. The reach: `SetRoomBounds` clamps a
child portal against the window of the room it is seen through, so a room whose doorway
lies beside the parent's rectangle is dropped as `empty` although its portal is plainly
on the screen - JOBY2's room 99 behind the stairs, whose portal projects to
542,116..754,389 while room 124's window ends at 535, and ANDREA2's room 222, the brick
tower of Trajan's Markets, in the draw list at no level view direction from the
courtyard at all (0 of 120 yaws at each of five pitches) and popping into the picture as
Lara walks up to it; under the reach it is in the list at 34 to 36 of 120 at each of
those pitches and JOBY2's room 99 goes from 88 of 504 view directions to 124. Clipping
the child against the parent's *reach* instead is not a second rule but the same walk
unrolled and gains nothing in any of 504 directions; only leaving the parent out changes
the answer, and the seen-mark is what makes it terminate - with the parent gone, a ring
of rooms whose portals all face the eye can be walked round and round, over 20,000
dequeues on one ANDREA3 frame against the classic walk's 33, while one visit per room
makes the worst frame in the game 319 dequeues (JOBY4) against that walk's own 320.
Neither capacity was ever counted: `room_list_end` was written into the ring modulo 128
with no test and `number_draw_rooms` incremented with no test at all, while a level may
hold 256 rooms and JOBY4's classic worst-case frame of 106 rooms is already six past the
old `short[100]`. The cut boxes: a whole-room mark paid four rooms for two sectors'
worth of wall, among them an 11 x 14 sector courtyard, and where the classic geometry of
a randomized level has a hole of its own the void or the skybox shows through the
boundary between the HD and the classic room.

**How.** The intrusion rule: `HDRoomBox`, `HDIntrusion` and the setters
`HDRenderSetRoomBoxes`/`HDRenderSetIntrusions`/`HDRenderIntrusionHits`/`HDRenderGroupIntrudingTriangles`
(`specific/hd/hd_render.h`) hand the boxes and the list in before the build;
`BuildRoomBoxes` (`hd_level.cpp:1252`) fills the boxes out of `ROOM_INFO`,
`BuildRoomIntrusions` (`:2149`) builds the list out of `room[]` alone - boxes, portals,
floor links - so an offline tool can reproduce it, and `ReportRoomIntrusions` (`:2376`)
writes `HDLOG level=%s overlap pairs=%d intruding tris=%d in %d groups`;
`hd_render.cpp:3722` tags the triangle and `:925` carries the tag last in `HDRange`'s
sort key, so a level built with no list sorts exactly as before, and `DrawRoomWindowed`
(`hd_level.cpp:801`) draws the open half over the whole screen and the intruding half
inside the window. The scissor itself: `HDFrameRoom` takes
`ROOM_INFO::left/top/right/bottom` at the seam it already had in `InsertRoom`
(`specific/drawroom.cpp`, one line beside the matrix) and keeps them as a half-open
rectangle with one pixel of margin on every side - the numbers are taken there because
`PrintObjects` resets all four as soon as it is done with a room's items - and
`HDRenderScissor`/`HDRenderScissorOff`/`HDRenderScissorAvailable` (`hd_render.{h,cpp}`)
set `D3DRS_SCISSORTESTENABLE` in both room phases, because a lamp cone hanging in a
portal has to be cut by that portal like the wall beside it; `hd_object.cpp` switches
the test off for every item, items being handed the whole screen as the classic path
hands it to them. The window's units are backbuffer pixels one to one: `InitWindow`
(`specific/3dmath.cpp`) is called with `App.dx.dwRenderWidth/dwRenderHeight`, which are
the present parameters' `BackBufferWidth/Height` (`specific/dxshell.cpp`), and the
engine calls `SetViewport` nowhere except in the HD sky, which restores it. Without
`D3DPRASTERCAPS_SCISSORTEST` both scissor calls do nothing and the rooms draw whole. The
saved state bracket grew by one, the rectangle being a device state with its own
`Get/SetScissorRect` pair, so the bracket check counts 59 items and reports 0 changed.
The walk: `game/interpolate.h`/`.cpp` hold `InterpEyeRoom`, gated on `g_applied` so that
a cutscene frame and the first frame after a level start are untouched; `game/draw.cpp`
holds `DrawRooms` (`:1403`) seeding the walk with it, `SetRoomBoundsReach` (`:3329`)
taking the projection once and forming both rectangles from it while the side test,
`tooNear == 4` and the fixup for a portal straddling the eye plane are untouched,
`FollowPortal` (`:3582`) choosing between the two, `ReachBeginWalk` (`:1632`),
`GetRoomBounds` (`:3590`) accumulating the classic window only for a dequeue that had
one, `ReachTrimRooms` (`:1677`) removing a reach-only room with no `.TRG` art so its
items cannot be drawn in a room that is not in the picture, `classicHere` (`:3616`)
keeping `outside`, the outside rectangle and `snow_outside` on the classic rooms, and
the named capacities `kDrawRoomsMax` = 256 (`draw_rooms`, was `short[100]`),
`kRoomListRing` = 512 (`draw_room_list`, was a 128-entry ring, the wrap written `&
(kRoomListRing - 1)`) and `kReachBudget` = 128; `HD_MAX_FRAME` in `hd_level.cpp` follows
to 256, one `FrameRoom` of 380 bytes per listed room. 256 is the engine's own maximum -
`BuildOutsideTable` refuses a level with more rooms outright - and a room enters
`draw_rooms` once per frame (`bound_active & 1`), so the array cannot be full while a
room is still unlisted and no eviction rule is needed; 512 is twice the deepest queue
that can exist, a room being queued only while `bound_active & 2` is clear, against a
measured deepest queue of 70 in JOBY4; 128 for the budget because the worst list in the
game is 139 rooms (ANDREA1) and at 64 the list stood at exactly 64 for seconds at a
stretch with up to 30 reach-only rooms dropped in a frame. The cut boxes: the
randomizer's `TR5Randomizer/src/core/Tr5Level.cpp`/`.h` hold `Tr5Level::hdMarkCutBox`
with `HD_CUT_OUT_NORMAL` = 512 (half a sector outward along the opening's own normal,
because the Remaster builds a wall as a thick solid whose faces do not sit on the
classic wall plane) and `HD_CUT_IN_PLANE` = 128 inward along the other two axes (because
a cut room stays HD and never draws its classic roomlets, so every triangle the box
takes is replaced by nothing), clamped to half the extent so that an opening narrower
than 256 units collapses to a plane through its own middle; a wall opening's box is
derived from the view window now standing at that sector - the 32-byte portal record's
own four corners, which is the opening as far as `SetRoomBounds` is concerned - with the
one-unit inset of a view window undone first, and one derivation serves both ways into
`makePortal`; a vertical opening writes no window at all, so the box is the sector
square at the height the link was opened (byte 7 for a ceiling, byte 5 for a floor,
times 256, with the sector's other byte and then the room's own floor standing in for
-127); `@Clear` takes the box over the faces it really degenerated. Cut lines ascend by
their six numbers (a `std::set`), so the same seed writes the same file byte for byte.
In the engine, `specific/hd/hd_markers.cpp`/`.h` accept versions 1 and 2 and keep the
boxes as `HDMarkersCutCount`/`HDMarkersCut`, a line the reader has no rule for still
being ignored and counted; `hd_level.cpp`'s `BuildRoomCuts` hands the renderer one entry
per (group, box) pair in the group's own local frame; `hd_render.cpp`/`.h` hold the
exact separating-axis test over thirteen axes - the box's three face normals, the
triangle's plane, and the nine cross products of box axis with triangle edge - used
because a vertex-inside test leaves the doorway shut, one wall triangle crossing an
opening with all three corners outside being exactly the triangle that has to go, while
a bounding-box overlap eats more than the opening. The drop is the first thing in the
per-triangle loop of `HDRenderLevelBegin`, before the page slot, the horizontal-plane
row, the overhang test and the intrusion test, so a cut triangle costs no index, no draw
call and no count, and a group with an empty cut list pays one compare per triangle. The
cost of the whole rule is the intruding split alone: whole-level draw calls go from
1,859 to 1,874 in ANDREA1, 1,702 to 1,743 in ANDREA2, 1,553 to 1,563 in JOBY2 and 1,586
to 1,602 in RICH2, and the ANDREA2 courtyard scene draws 29 calls a frame. Nothing of it
runs with `hd_enable` off: `HDLevelBuild` returns at `HDEnabled()` before any box is
built, `HDLevelLoad` before `HDMarkersLoad`, `g_reach_on` stays false and `FollowPortal`
calls the `SetRoomBounds` that shipped, so the whole difference the classic path sees is
the two capacity guards, the two array sizes, the shape of the ring's wrap and the
eye-room correction in the walk both paths share. Seams behind `-log`, all staying in
the build: one line at the first frame that draws an HD room saying whether the device
has the cap and what the backbuffer is, one line per room with the window it got (`HDLOG
room=53 group=53 clip=812,391,1096,655`), the tally `HDLOG clip rooms=11 full=1
scissored=10 empty=0` once a game second, `ROOMFLASH` (a room in the list on the frame
before and after but not the one between, with the eye the walk really used and, per
missing room, the reason - `side`, `toonear`, `empty`, `noportal` or `reach`),
`ROOMLIST` (on every membership change and every five seconds the whole list with each
room's window, its reach, which carried it, its distance, its `outside` flag, its
undrawn portal neighbours and the camera and target it was walked from), `ROOMPOP` (a
room joining the list while more than eight sectors away, four lines a second),
`ROOMWALK` (once a second: tween-frame corrections, reach rooms added, budget, trimmed,
`cap hit=`), and for the cuts a `cut=` field per room, the reason word `cut`, one `HDLOG
... cut N x .. y .. z ..` line per box naming the rooms it took art from and how much,
and `cuts=`/`cut_tris=` on the level line; `marker` now means only the whole-room case.
Tools: `RoomProbe` carries `GetRoomBounds` and `SetRoomBounds` over line for line and
runs the walk out of the classic `.TRC` with no device (`-walk`, `-walkyaw`,
`-walkpitch`, `-walkpitchfan`, `-walkwant`, `-walkeach`, `-walkcentre`, `-walkboxes`,
`-reach`, `-reachsurvey [step]` with nine budget columns), and gained `-scissor
<l,t,r,b>`, `-roombox <classic folder>`, `-window all|open|intruding`, `-clipgroup`,
`-tround` and `-hdm`, with `-with` now holding 32 groups; `TrgProbe` gained
`-roomspace`, `-room <n>`, `-statics`/`-slot <n>`, `-share`/`-slack`/`-deep` and `-near
<x> <y> <z> <radius>`, and run against each other the two build an identical intrusion
listing, line for line, on all fifteen levels; `InterpTest` gained a `portal walk`
section, a `reach` section (316 checks, among them the JOBY2 rectangles, the invariant
that the classic rectangle lies inside the reach's over 2,000 random pairs, and the ring
that shows what the seen-mark is for) and `walk_capacity` (eight checks over the ring's
power of two, the queue depth over 256 rooms each with a portal to every other, and the
same level against a ring of 128 tripping the guard); `HdmDump` and `MapProbe` read
version 2 and print the boxes with their spans, `MapProbe` through the engine's own
reader. One
consequence that was measured and left as it is: a triangle crossing a cut box may reach
far outside it and is dropped whole, which at one measured doorway costs a lintel band
of 6,567 pixels; clipping triangles against the box was not attempted.

**Commits.** `85977f5`, `a1267b5`, `43014f4`, `351803f`, `798c5ca`, `c37561a`, `5285f99`

### The room's inside decides, per frame, and the back of a wall is not drawn
2026-09-27

**What.** Two changes to how the HD path draws the remaster's room art. The opaque room
ranges - pass 0 with no alpha test, no water and no sky - are drawn with their back faces
culled (`D3DCULL_CCW`); every other range stays two-sided as before. And the art of room
`b` that stands inside another room `a` is now asked about with TR5's own inside of `a` -
one sector in from the edge of its grid, no second sector taken off - and is no longer
windowed unconditionally: each intruding triangle is bound at load to the nearest classic
face of `b` that it stands on, within 128 units of the face's plane and inside its
outline, and per frame a pair's claimed art is drawn whole when the classic path draws any
of the pair's claiming faces, inside `b`'s portal window otherwise. A triangle no face
claims keeps the answer of the box one sector smaller in x and z: inside the window when
it stands that far inside `a`, drawn whole otherwise. A pair whose holder is not on the
frame's room list is not asked and is drawn whole with the rest of the room. A room the
reach adds to the frame's list with no window of its own has no classic face to ask, and
all of its intruding art keeps the smaller box's answer: the pieces standing in that box
are not drawn, the rest is drawn whole.

**Why.** The owner's pictures showed three things: the bricks of one room on the wall of
another (ANDREA1, from the cafe awning in room 64, room 93's brick hull on the facade of
room 101), marble in a metal room (JOBY2, rooms 24 and 60 in the hub of rooms 28, 30 and
33), and in RICH2's X-ray room a corridor wall seen from behind as a flat green pane where
the Remaster shows the corridor through the glass. The measurement found four mechanisms,
of which this round takes the first three. The window the rule used is the intruder's own
and the holder box was one sector too small: room 93's hull stands in room 101's second
sector ring, inside no box of the rule, and was drawn whole; JOBY2's rooms 30 (8 x 4
sectors) and 33 (4 x 3) had no box at all. With TR5's own inside the hull is windowed, but
room 101's facade, which stands inside room 93, is then cut to 101's strip of a window at
the same view and room 93's interior shows in its place (10,999 pixels against the
reference picture); from inside room 93 the same facade has to go. The box cannot tell
those two apart and the classic path can: it drops a face only when all its corners lie
beyond one edge of the room's window (`specific/drawroom.cpp:577-606`,
`specific/polyinsert.cpp:883/1055/1281/1422`). At the awning three of the six classic
faces of 101 that claim its facade survive that rule and none of room 93's six; inside
room 93 none of 101's six survives. The X-ray pane is not an overlap at all: the renderer
drew every range two-sided (`D3DCULL_NONE`), so the rear face of group 11's corridor wall
painted over the corridor. The fourth mechanism, the second hull of RICH2's lift shaft in
rooms 62 to 64, and the lobby's mirror floors are left for a later round.

**How.** The cull: `RangeCulls` (`specific/hd/hd_render.cpp:199`) decides per part at the
build (`:3769`), the answer rides in `HDRange::cull` and in the sort key (`:4235`), and
`draw_pass` switches `D3DRS_CULLMODE` per range (`:5633`) and puts `D3DCULL_NONE` back at
the end of the pass (`:5666`), so a model drawn between two rooms finds the state the
bracket installed (`:4979`). The winding: `D3DCULL_CCW` shows the X-ray corridor,
`D3DCULL_CW` at the same view shows the pane and no corridor (RoomProbe). The box:
`BuildRoomIntrusions` (`hd_level.cpp:2382`) takes `r->x + 1024 .. r->x + (y_size - 1) *
1024` and the same in z, TR5's own box (`game/draw.cpp:3628-3633`), with the neighbour,
flipmap and touch tests unchanged and a room of two sectors or fewer on a side left
without an inside. The classic faces: `BuildPairFaces` (`hd_level.cpp:2415`) hands the
renderer every drawn quad and triangle of every intruder room, in the roomlets' order and
in the room's own frame, left out only where a face stands further than
`HD_PAIR_FACE_REACH` from every box of its room; `HDPairFace`, `HD_PAIR_FACE_REACH` and
the inline `HDPairFaceDistance` are in `hd_render.h:272-350`. The pairs: the index fill
(`hd_render.cpp:3990-4144`) gives a triangle to the first entry whose box holds it and to
the nearest claiming face; a triangle no face claims stays with the pair only when its
centroid also lies in the box one sector smaller in x and z, and goes to the open half
otherwise (`:4059-4068`, `:4100-4105`); the same test marks every claimed triangle, and
the pieces (pair, face, mark) and (pair, no face) are numbered per group, so a face can
own two pieces of a pair; `HDRange::intruding` is one plus the piece, so the pieces never
share a draw call, and `HDRenderGroupPairRange` and `HDRenderPairWhole` (`:2862`) read
them out and set the frame's answer. The per-frame test: `HDFrameRoom`
(`hd_level.cpp:4238`) runs `ClassicFaceDrawn` (`:623`) on the faces of the room it
collects, at `InsertRoom`, with the room's own view * world matrix, `f_mpersp`,
`f_centerx/y`, the underwater wobble and the four numbers `r->left..r->bottom` - the
classic path's coordinate space, a corner nearer than `f_mznear` carrying -128 as in
`aRoomletTransformLight`; the roomlet's box test is implied by the corner rule and not
asked, and so are the facing test and the water chop, which the window does not decide.
The answer: `DecidePairs` (`:713`), called per room at the top of `HDFrameDraw` (`:4457`),
sets each piece whole or windowed and the glow phase keeps it; a room with an empty window
(one the reach lists and the walk gives no window) is answered by the mark alone, the
marked pieces not drawn and the rest whole (`:744-745`); `draw_pass`
(`hd_render.cpp:5384`) draws the whole pieces with the open half and the rest with the
intruding half, which `DrawRoomWindowed` (`hd_level.cpp:801`) scissors to the room's own
window as before (`:820`). The seams, all behind `-log`: the level line ends in
`holder=inside cull=opaque faces=%d bound=%d unbound=%d` (`:2649`), each pair line in
`faces=%d bound=%d` (`:2623`, a face counted once however many pieces it owns,
`:2579-2621`), the clip line in `pairs=%d whole=%d scissored=%d` (`:4734`, the pieces
asked, drawn whole and windowed; the earlier `scissored=` counts rooms), `windowed_tris=`
now counts only the triangles really drawn inside a window, and a new line `HDLOG cull
level=%s ranges opaque=%d two_sided=%d` follows the build (`:3468`); the first frame's
room list ends an `EMPTY` room's line in ` where it stands in the box one sector smaller`
(`:4668-4679`). The pair line's
triangle count is every triangle the holder's inside holds, the level line's `intruding
tris=` and `unbound=` only those drawn with the window. `FrameRoom` grew to 384 bytes, the
frame array to 96 KiB. Tools: `TrgProbe -roomspace` uses the same box by default (`-deep
1024` asks the old one, which windows every triangle it holds) and the renderer's own
inline face test and unclaimed rule, and prints the same per-pair fields and the three
totals; `RoomProbe` follows the box and the faces out of the classic `.TRC`, prints the
engine's two lines, and gained `-cull none|opaque`, `-roomwin <g>:<l>,<t>,<r>,<b>[;...]`
(a group's walk window, drawn as `DrawRoomWindowed` draws it) and `-decide
whole|scissor|frame|face` with one `decide:` line per piece, ending in `inner` for a
marked one; an empty `-roomwin` window is answered as the engine answers it.

**Checked.** The binding distance: all 540 of room 101's triangles inside room 93 lie
within 86 units of one of six classic faces of 101, 90 of them within 1 unit, and all 108
of room 93's inside room 101 within 1 unit of six faces of 93; 128 takes all of them. Of
the triangles the insides hold, the faces claim 15,993 with 558 faces on ANDREA1, 12,439
with 254 on JOBY2, 16,980 with 480 on RICH2 and 12,650 with 527 on ANDREA2; of the
unclaimed rest 725, 442, 82 and 898 stand a sector further in and keep the window, and
11,020, 16,290, 22,697 and 2,437 are drawn whole, which leaves 16,718, 12,881, 17,062 and
13,548 intruding triangles. `TrgProbe -roomspace` and RoomProbe's build of the engine's
code print the same level line and the same per-pair lines on ANDREA1 (158 pairs), ANDREA2
(148), JOBY2 (53) and RICH2 (164), and `-deep 1024` gives the old 7, 4 and 9 pairs with
1,067, 2,037 and 1,425 triangles; every classic vertex coordinate of the fifteen levels is
a whole number, so the engine's float corners are the tools' integer ones. Pictures
(RoomProbe, 960 x 540, fov 80; pixels differing by more than 8), each drawn on two walks:
the classic walk (RoomProbe `-walk` without `-reach`, its rooms and windows), and the walk
the HD path runs, the classic walk with the reach (`-walk ... -reach`), whose added rooms
are drawn with an empty window - 27 of 38 rooms at the view from room 93, 16 of 33 at the
awning. The classic walk's numbers: the awning view 153 against the reference with room
101 left whole, and 0 against the same reference with the opaque cull, the 153 being the
cull; the view from room 101 0 against the reference with the TR5 inside and no cull, and
1,293 with the cull, on the flagpole, whose back faces go; the view from room 93 0 against
today's picture without the cull, 1,040 with it, a ceiling board's back at the top edge;
the X-ray view 0 against the reference with the opaque cull; JOBY2 from room 28 2 against
the reference with the TR5 inside, 2,928 against today's, the marble above the corridor's
door gone; the ANDREA2 courtyard from room 134 at four yaws 0 against today's, the Piazza
corner from room 57 at four yaws 0, 8, 0 and 0, room 30 at four yaws 0, 13, 0 and 11, and
the RICH2 shaft from room 63 0. On the reach walk: the awning 153 and the view from room
101 1,293 against the references, as on the classic walk; the view from room 93 1 pixel
from today's without the cull and 1,041 with it (before an empty window kept the smaller
box's answer, 4,082 without the cull, 2,038 of them hole in the doorway toward room 101,
whose facade and plaster art the empty window silenced); JOBY2 from room 28 1,235 against
the reference and 370 against today's without the cull, no new hole; the X-ray view lists
room 9, the unflipped twin of 11, and differs from today's by the cull alone; the
courtyard, the Piazza corner, room 30 and the shaft as on the classic walk. The unclaimed
rule changes none of these pictures by a pixel. Holding each piece to its own face alone
leaves the lower storey of 101's facade, 144 triangles on a face wholly below 101's
window, cut at the awning (7,318 pixels, room 93's interior); scissoring a pair whose
holder is not listed brings the Piazza's strip of sky back (3,081 pixels). The box sweep:
one view from the middle of every room of ANDREA1, ANDREA2, JOBY2 and RICH2 at eye height,
along the longer axis and the other way, without the cull, against today's engine on the
same walk. On the classic walk 1,644 views drawn (40 more are of rooms with no art), 47
differ. 82,413 pixels of today's holes are filled in 21 views - RICH2 room 40 (26,400),
JOBY2 room 33 (10,372), ANDREA2 rooms 130 (7,644), 177 and 149, ANDREA1 room 110 (6,366)
and fifteen more - and room 93's brick hull is gone from the views out of rooms 101, 102,
103 and 218, and a slab of brick over the corridor seen from room 199 is gone. 3,552 new
pixels of hole appear in 12 views, every one of them claimed art whose pair has all its
claiming faces dropped by the corner rule, and every one of them today's picture when
drawn whole: ANDREA1 room 78 (1,164, a block of an upper storey beside the flag), room 5
(558, a slit beside the sign), rooms 24 and 237 (313 and 222, slits in a wall), ANDREA2
room 115 (701, a notch under the balustrade) and room 205 (379, the roofline), and six
views under 110 each. These are left for the game to judge beside the filled holes; the
corner rule cannot tell them from room 93's hull, which it has to hide. Without the
unclaimed rule the same sweep over every fifth room found 16,250 new pixels of hole,
15,272 of them under ANDREA2 room 115's balustrade, and ANDREA1's palm fronds (room 150,
1,227 pixels) and spiral stair (room 225, 2,297) sliced; with it the palm and the stair
are today's picture and 1,638 new pixels remain there. On the reach walk the same 1,644
views give 48 that differ, 68,161 pixels of today's holes filled in 17 views and 3,659 new
in 12: the slits above (ANDREA2 room 115 540 instead of 701), and ANDREA1 room 81 (383,
the wall above a balcony, whose holder only the reach lists) and room 31 (51). The views
from ANDREA1 rooms 107 and 212 that the reach walk changed before an empty window kept its
answer (104,136 pixels, and 105 of hole) are today's picture. The cull's void sweep: one
view from the middle of every third room of ANDREA1, JOBY2 and RICH2 at eye height along
its longer axis, 192 views drawn each way; 25,660 pixels turn to the clear colour, four
views more than 500 each - a near object's back at the edge of JOBY2 room 21's view, a
wall's back that hid RICH2 room 108's corridor, and two window openings of one JOBY2
building (rooms 138 and 153) through which the back of the opening's side wall was seen
and behind which no room is drawn from there. On the reach walk, both ways along the
longer axis and with ANDREA2 added, 548 views: 541,034 pixels, 15 views over 500 - 20,996
pixels (2 views) on ANDREA1, 63,397 (6) on JOBY2, 63 (0) on RICH2 and 456,578 (7) on
ANDREA2, whose room 177 alone gives 386,361. In the largest - ANDREA2 rooms 177, 75, 81
and 219, ANDREA1 room 228, JOBY2 rooms 12 and 36 - the eye at the room's middle stands
inside a roof, a floor slab or a solid block: two-sided it sees that solid's back faces,
culled the scene behind it, and none of them is a wall lost; the rest are a wall's back
that hid a hole behind it, as above. The JOBY4 sea seen from below is the same picture
either way. Draw calls: ANDREA1's level goes from 1,874 ranges to 2,470, ANDREA2 1,743 to
2,310, JOBY2 1,563 to 1,935, RICH2 1,602 to 2,110, the cull adding none; the awning view
draws 297 calls against 228, the view from room 93 204 against 155, and on the reach walk
473, 97 more than before an empty window drew its unmarked pieces. The suites: ModernTest
13,992 checks, MenuTest 36,180, InterpTest 635, LipsTest 243 and 270 with the `.OBJ`
folder, 0 failed, as before.

**Unchanged.** With `hd_enable` off nothing of it runs: `HDLevelBuild` returns at
`HDEnabled()` before any box or face is built, `HDFrameRoom` returns at `GroupOfRoom`
before the corner test, and `HDFrameDraw` with an empty list returns before `DecidePairs`.
The portal walk and its rectangles, the reach, the overhang split, the items' exemption
from the scissor, the two-sidedness of the cut-outs, the alpha pass, the glass, the
additive pass, the water and the sky, and the mirror floors are as they were.

**Decisions.** The cull is for the opaque ranges only: cut-out, blended and additive
ranges stay two-sided, and so do water and the sky. The holder box is TR5's own inside
with no extra sector. A pair is decided per frame from the classic faces its art crosses,
and a triangle no classic face crosses keeps today's answer, the window inside the box one
sector smaller, and so does all the intruding art of a room the reach lists with no window
of its own, which has no classic face to ask. The lift shaft's second hull and the mirror
floors are left for a later round. Two points of the engine depart from the rule as
decided and wait for confirmation. The rule decides per crossing classic face; the engine
draws all of a pair's claimed art whole when any of the pair's claiming faces is drawn,
because per face leaves the lower storey of room 101's facade cut at the awning (7,318
pixels against the reference, 0 per pair) - per face is one line in `DecidePairs`. And the
rule leaves a pair whose holder is not on the frame's list unasked without naming its
answer; the engine draws it whole, because scissored it brings the Piazza's strip of sky
back (3,081 pixels).

**Credits.** The principle of deciding the art by the classic faces it crosses - TROne
keeps it per crossing classic face, this engine per pair of rooms (see Decisions) - and
the back-face cull follow TROne (GPLv3) by Todesengel1931 -
https://github.com/todesengel1931 - as descriptions only; no code of it was read for this
round. The ANDREA1 viewpoints in rooms 93 and 101 and the triangle counts of that pair
(540 and 90) are Todesengel's own measurements.

**Commits.** `f7ffe49`, `edc7b03`, `679da06`, `00bd9fe`, `56de221`

### A room the reach alone lists draws none of its art in a listed holder
2026-09-28

**What.** A room the reach lists with no classic window of its own draws none of its
intruding art inside a holder that is on the frame's room list; in a holder that is not
listed its art keeps the answer of the box one sector smaller, as before. The seams,
behind `-log`: the clip line ends in four `reach_` counters, a new `reachart` line names
every such room and holder, the level line ends in `band_unclaimed=`, and the ROOMLIST
whole-list dump is also written on the first frame after every `WM_INACTIVE` and
`WM_ACTIVE`, its header ending in `why=`. RoomProbe answers the same way, walks with the
engine's own camera, holds 256 groups, prints a room's final window and replays a log's
whole-list dumps.

**Why.** After round 1 the pictures showed art of another room through a wall at five
places: a dark block beside a flag in front of a facade (ANDREA1, rooms 57 and 59), a
marble ceiling above a metal stair opening (JOBY2, room 28 looking into 32), window frames
in a brick loft (ANDREA1, room 65), a lit floor in a doorway (ANDREA1, room 19) and black
pipes beside a pillar (JOBY2, room 153). The measurement found four of them to be pieces
of rooms the reach lists with no window, drawn whole by the empty-window answer: room 93's
brick hull in the air of room 101 (6 pieces, 108 triangles), room 24's marble ceiling in
the hub of rooms 30, 31 and 33, room 101's window frames inside room 93's loft and room
215's floor in room 44's doorway. The log of a play session shows the switch at the flag:
the same pieces left out while room 93 had a classic window, and drawn a few seconds later
with the eye 26 units away and 93 listed by the reach alone. The fifth, the pipes, is room
30's art standing in alley 16: 1,209 pixels of claimed pieces and about 26,600 of
unclaimed art in the holder's wall ring, which the index fill hands to the open half and
nothing asks. Round 1's reason for the empty-window answer rested on RoomProbe's first
walk note and not on the room's final window: at the view it cited, from inside room 93
toward the doorway to room 101, room 101 has a classic window of 1481,401..1681,972, and
the answer there does not change. This entry supersedes round 1's sentence that all of a
reach-listed room's intruding art keeps the smaller box's answer.

**How.** `DecidePairs` (`specific/hd/hd_level.cpp:713`) looks each piece's holder up on
the frame's list (`g_frame`, whatever window the holder has) for every piece (`:733-739`),
not only for the asked ones, and the empty-window branch reads `whole = piece.inner == 0
&& !listed` (`:744-745`). The holder is a group, the room of that number at the build,
and the lookup compares it with the groups on the list: after a flipmap a group is drawn
at its pair's room, and a lookup by room index misses it there. JOBY2 swaps five pairs in
its first seconds and keeps them swapped; at the marble view rooms 87 and 89 are listed by
the reach alone and drawn by groups 54 and 53, and by room index group 54's pieces in
group 53 were drawn whole. The ask branch has shared that lookup since round 1 and changes
with it, on a level with a flipmap only. A piece of an empty-window room that is not
answered whole is not drawn: `DrawRoomWindowed` skips its second, scissored call for an
empty window (`:818`). The ask branch, the claimed-face loop and the answer for a holder
that is not listed are unchanged, and `pairs=` still counts the asked pieces alone. The
counters `g_clip_reach_rooms`, `_pieces`, `_whole` and `_hidden` (`:517-520`) count the
empty-window rooms that have intruding pieces, their pieces, the pieces drawn whole, and
the pieces outside the smaller box left out because their holder is listed; the clip line
ends in `reach_rooms=%d reach_pieces=%d reach_whole=%d reach_hidden=%d` (`:4734`).
`LogReachArt` (`:848`) writes `HDLOG reachart level=%s room=%d reach=%d,%d..%d,%d
holder=%d listed=%d pieces=%d tris=%d answer=whole|hidden band_tris=%d`, one line per
empty-window room and holder, with the clip line and on the frame of a window event's dump
(`:4746`); the reach rectangle comes from the walk's log note through `HDFrameNoteReach`
(`game/draw.cpp:1847`), the event through `HDFrameNoteDump` (`:2090`). The index fill
counts per intrusion entry the unclaimed triangles it hands to the open half
(`hd_render.cpp:4100-4106`, read by `HDRenderIntrusionBand`), `ReportRoomIntrusions` keeps
them for the level (`hd_level.cpp:2588`) and the level line ends in `band_unclaimed=%d`
(`:2649`); `TrgProbe -roomspace` ends its level line in the same number. The first frame's
room list ends an `EMPTY` room's line in ` or its holder is on the list`. The dump:
`RoomListWindowEvents` (`game/draw.cpp:1208`) is set by the window procedure with `-log`
only (`specific/winmain.cpp:671` and `:735`), and the whole-list dump is written when the
five-second timer is due or an event waits (`draw.cpp:2073-2092`), a `WM_INACTIVE` before
a `WM_ACTIVE`, the header ending in `why=timer|inactive|active`. RoomProbe's `-decide
frame` answers an empty window the same way
(`Preparation/Tools/Diagnose/RoomProbe.cpp:4028`), a window being empty, as `ClipWindow`
has it, when it has no width or no height before any widening; a `decide: reachart` line
per holder and a `decide: clip` line after the picture give the engine's reachart fields
and clip counters over the named groups. The walk takes the engine's camera (`enginecam`,
`:532`): the integer angles of `phd_GetVectorAngles` with `phd_atan`, `phdtantab` and
`phd_sqrt`, the rotation `aGenerateW2V` builds out of the float sine table, and
`AlterFOV`'s integer perspective. `-walktarget x y z` builds it from the eye and a world
target as a ROOMLIST dump prints them, `-walkyaw` and `-walkpitch` are rounded once to
angle units, and without `-walk` the same switch hands the engine's yaw, pitch and
perspective to the picture, whose `LookAt` then builds the view from them. With `-reach`
the list is trimmed as `ReachTrimRooms` trims it (`:2567`), the art read from the `.TRG`,
and the reach rectangle is kept as `g_reach_box` keeps it. `-walkwant` and a new
`-walkfinal` print a room's final window, `final=l,t..r,b reach=l,t..r,b classic|REACH`,
per direction with `-walkeach`; `-walkshut <items|all>` walks with closed doors shut,
whose portals `ShutThatDoor` zeroes (`:2292`, `:2381`); `-with` and `-roomwin` hold 256
groups and refuse a 257th with a message (`:3856`). `-walkdump <log> [frame]` (`:3073`)
walks every whole-list dump of a log again with the level, the field of view of the last
`ALTERFOV` line, the backbuffer, the art of the `HDLOG room= ... hd=` lines and the flips
of the `HDLOG flip` lines, searches for what no log line carries - which closed doors are
shut, and a flyby's roll - and prints per dump both lists, the rooms only one of them has,
how many windows are equal, within a pixel and further off, how many reach rectangles are
equal, `doors_shut=` and `roll=`. `-decide frame` counts a holder as drawn when it is the
seed or any `-with` group; the engine lists only the groups it draws in HD, so the groups
the log's `HDLOG room=` lines give `hd=0` stay out of `-with` for the two to agree.

**Checked.** The places, at the camera of each picture, walked with the engine's camera
(in JOBY2 with the doors the replay found shut and the flips the log names), 2560 x 1440,
the played level files with their `.hdm`, before (5c573c8's answer, the tool's group caps
raised) against after, pixels differing by more than 8: the block beside the flag 41,735
(room 101's facade behind it), the same block in a picture 41 seconds earlier 60,734, the
marble ceiling 15,003 (the metal behind it), the loft's frames 18,843, room 19's doorway
5,318 in its first picture (room 44's own floor) and 0 in its third, the snow yard 1,215
(the thin claimed pieces go, the black pipes stay), and 0 at room 19's railing, at room
65's end wall and in a picture of the facade 17 seconds later. At the flag, the earlier
picture, the loft, the end wall, the railing and both JOBY2 places the tool's clip
counters equal the engine's clip line of that frame in every field that line carried,
which has no `reach_` field yet; at the doorway the camera was moving. The tool looks a
holder up among the groups, so at the marble view it counts group 54's pieces in group 53
as hidden, which the engine drew whole until its lookup did the same. The marble view
draws 593 calls against 673. The pinned views of round
1, on the reach walk the game uses, 1280 x 720, the classic files: 0 pixels at the awning,
room 101, room 93 toward 101, the X-ray room, the shaft, and the courtyard, the Piazza
corner and room 30 at four yaws each; JOBY2 room 28 1,657, a strip of metal at the right
edge replaced by the marble behind it, no hole. The reach walk lists more rooms than the
classic walk at ten of the eighteen (the awning 33 against 17, room 93 38 against 11) and
drops none. The sweep, on the reach walk: round 2's 38 sample views 0 pixels; round 1's 21
filled and 12 slit views (32 distinct) 23 pixels in one (JOBY2 room 9); and all 396 of
round 2's 1,684 views in which the rule changes a piece (393 drawn, three seed rooms have
no art): 175,994 pixels, 125,531 of them turning to the clear colour in five views -
ANDREA2 room 140 (46,094) and room 89 (38,152) at yaw 0, ANDREA1 room 10 at yaw 270
(31,587, a room seen through a wire fence), ANDREA2 room 127 (8,636) and ANDREA1 room 127
(1,062) at yaw 0. In each the piece left out fills a wall or a doorway of its holder,
which is listed and draws nothing of its own there. Three more views change by more than
500 pixels with no hole: ANDREA1 room 146 (41,171, a piece of brick wall gone from in
front of a facade), JOBY2 room 149 (1,620) and ANDREA1 room 91 (555). RICH2's toilets: the
seven poses of the whole-list dumps from inside rooms 134 to 137, cubicle 136's middle at
yaw 0 and 180 and corridor 134 along z both ways, 0 pixels, with the rooms and windows of
the dumps, `pairs=0` in the cubicles and no listed holder where a reach room is listed.
The dump replay: 214 whole-list dumps of the first play session and 13 of the second, room
lists equal in all 227; windows equal in 5,273 rooms, within a pixel in 17 and further off
in none; 37 dumps agree only with closed doors shut - JOBY2's door on room 106's opening
to room 22 is why rooms 22, 120, 23 and 63 are never listed, the two doors on the halves
of room 1's opening to room 35 are why 35 is not, and a door in room 31 is why that room's
window starts at 1262 and not at 1104 - and 12 TITLE dumps only with a flyby's roll, the
17 windows within a pixel all among them, the roll found to a whole angle unit; the reach
rectangles are equal in 5,262 rooms and off in 28, all in those rolled dumps.
`band_unclaimed` in the engine's build (RoomProbe) and in TrgProbe: ANDREA1 11,020,
ANDREA2 2,437, JOBY2 16,290, RICH2 22,697. The suites: ModernTest 13,992 checks, MenuTest
42,364, InterpTest 651, LipsTest 243 and 270 with the `.OBJ` folder, 0 failed, as before.

**Unchanged.** With `hd_enable` off nothing of it runs: `HDLevelBuildHD` returns at
`HDEnabled()` before any box, face or piece is built, `HDFrameRoom` returns at
`GroupOfRoom`, and `DecidePairs` and `LogReachArt` run only over a frame list that stays
empty; `HDFrameNoteReach` and `HDFrameNoteDump` store and draw nothing. The classic files
change only in code that runs with `-log`: `NoteRoomWalk` returns at its first line
without it, the dump sits inside its `if (gLogEnabled ...)` block, and the window
procedure's two calls are inside `if (gLogEnabled)`, beside the flag's definition and its
declaration. The portal walk, the reach, `SetRoomBounds`, `SetRoomBoundsReach`,
`ReachTrimRooms`, the camera and the interpolation, the classic path, the unclaimed band
art and the pipes it leaves in the alley, the answer for a holder that is not on the list,
and ROOMLIST's change lines with their cap of 20 a second are as they were.

**Decisions.** A reach-only room's pieces are left out in a holder that is on the frame's
list and keep the smaller box's answer otherwise. The unclaimed band art keeps today's
answer: a change to it was measured and is left undecided. The tool follows the engine's
camera exactly, so that a render is proof.

**Open.** The holes the sweep found in five views are not decided: ANDREA2 rooms 140 and
89 and ANDREA1 room 10 through its fence open most, ANDREA2 room 127 and ANDREA1 room 127
less, and in each a hidden piece is the only art at a wall or a doorway of its listed
holder, so that the holder's own walls, which the rule counts on, are not there to show. A
narrower rule was measured in a scratch build of RoomProbe and not adopted: a holder
counts as listed only when it is the seed or has a window of its own. At the five views it
closes the holes from ANDREA1 room 10 and from both rooms 127 (0 pixels against the
picture before this entry) and leaves those from ANDREA2 rooms 140 and 89 (46,187 and
38,215 pixels, 46,094 and 38,152 of them clear colour), whose holders 140 and 149 have a
classic window there; it clears the four places by the same amounts as this rule (41,735,
15,003, 18,843 and 5,318 pixels); and it keeps the piece of brick wall in front of the
facade from ANDREA1 room 146 (0 pixels against before). The play test decides whether the
holes show in the game.

**Commits.** `8dfae67` (its answer for a reach-only room taken back by `205eb33`; the
seams and the tool follow stay), `3884153`, `faa3be5`, `666593f`, `9e176b2`

### The reach can be switched off
2026-09-28

**What.** A registry setting `hd_reach`, `1` by default, decides whether the portal walk
of an HD level carries its second rectangle, the reach. At `0` the walk is the classic one
in an HD level too: a room is listed only through the classic chain of portal windows, and
the art of a room the reach alone would have listed is not drawn. The ROOMLIST whole-list
dump ends in `reach=0|1`, so that a replay walks the same way. With `hd_enable` off the
setting is dead, as the reach is.

**Why.** The reach lists rooms the classic walk refuses, so that the art seen through a
wall the Remaster opened is there: JOBY2's room 99 behind the stairs, whose portal lies
beside the window of room 124, and ANDREA2's room 222, the brick tower, in the classic
list from no view direction in the courtyard. The same rooms, listed with no classic
window, are what draws the art of one room through the walls of another at the places the
entry above describes, and every rule that hides that art in the frame opens a hole
somewhere else. Whether the classic list alone looks better in play than the reach with
its ghosts, and at how many places an opened wall then shows the clear colour, is a
question the game answers faster than a render: the switch lets the two answers be
compared at the same place with the same build.

**How.** `global/types.h:2704` holds `tomb5.hd_reach` beside `hd_reflect`;
`tomb5/tomb5.cpp` writes its default (`:237-239`), reads it (`:446-447`) and saves it with
the other settings (`:671-672`); `game/draw.cpp` sets `g_reach_on = HDLevelActive() &&
tomb5.hd_reach` at the start of the game walk (`:2335`) and of the cutscene walk
(`:3246`), the head comment of the reach (`:172-177`) says so, and the whole-list dump's
header (`:2101-2105`) ends in ` reach=%d`. The manual's settings table
(`docs/Engine-Manual.md:1658`) has the row. The settings entry of 2026-09-05 above counts
ten settings and says the manual lists no others; this is the eleventh.

**Checked.** The engine builds with no new warning; ModernTest 13,992 checks, MenuTest
42,364, InterpTest 651, LipsTest 243 and 270 with the `.OBJ` folder, 0 failed; RoomProbe
and TrgProbe build from the changed header. With the setting at `1` the walk is as before,
line for line, and with `hd_enable` off `HDLevelActive()` is false before the setting is
asked.

**Unchanged.** The walk itself, the reach's own rules, `ReachTrimRooms`, `DecidePairs`,
the seams and the tools. There is no options row: the switch is a way to compare, not a
taste.

**Decisions.** The setting is on by default, so that a player sees what the entries above
built, and it is documented as the way to see the classic walk's answer in an HD level.

**Commits.** `ab6fa0f`

### The reach-only answer goes back to the smaller box
2026-09-28

**What.** In `DecidePairs` the pieces of a room the reach lists with no window keep the
answer of the box one sector smaller than the inside whatever holder they stand in: drawn
whole outside it, left out inside it. That is the answer of the entry before the last, in
place of the last entry's, which left them out in a holder on the frame's list. The seams
stay: `reach_hidden` now counts the pieces inside the smaller box, and a `reachart` line
answers `whole` when any piece of that holder stands outside it and `hidden` when none
does. RoomProbe's `-decide frame` follows.

**Why.** The last entry's answer cleared four places where the art of one room showed
through the walls of another and opened holes at five of the 396 views in which it changes
a piece - a wall panel beside a corridor in Trajan's Markets, the room behind a wire fence
in the Streets of Rome - because at those places the piece it left out is the holder's own
wall, modelled in the neighbour's group. Judged in play, beside the switch of the entry
above, the holes weigh more than the ghosts: a wall that is not there is a fault at every
angle, a hull in the sky at some.

**How.** `specific/hd/hd_level.cpp`: the empty-window branch of `DecidePairs` reads `whole
= piece.inner == 0` again (`:742`), the counter `g_clip_reach_hidden` takes every piece
not drawn whole (`:775`), `LogReachArt` answers by the pieces of the holder (`:900`), the
first frame's room line no longer names the holder, and the comments of the counters, of
`DecidePairs` and of the rule over `BuildRoomIntrusions` state the answer as it is.
`Preparation/Tools/Diagnose/RoomProbe.cpp`: the `frame` answer for an empty window
(`:4027`), the `decide: reachart` answer (`:4133`) and the usage text. The manual's
paragraph on the reach-only room and the tools README's two rows say the same.

**Checked.** The engine builds; ModernTest 13,992 checks, MenuTest 42,364, InterpTest 651,
LipsTest 243 and 270 with the `.OBJ` folder, 0 failed; RoomProbe and TrgProbe build from
the changed source. With `hd_enable` off nothing of it runs, as before.

**Unchanged.** The holder lookup by group, the ask branch and the claimed-face loop, the
format of every seam, the tool's camera and its switches, the `hd_reach` setting, the
walk.

**Decisions.** The four places show their ghosts again. A rule that tells a ghost from a
wall is not to be had from the geometry alone - a hull in the sky and a wall panel in the
neighbour's group are the same kind of piece - and none is built; `hd_reach` stays as the
way to see the classic walk's answer.

**Commits.** `205eb33`

### The material 15 books in motion: a fan's shadow that turns, and flames that play
2026-09-27

**What.** A `.TRG` part of material 15 is drawn as a book of pages: its own page and the
pages after it by page number, one page per step of the time the part gives, starting
where its phase says. Every such part in the game plays, 168 part-passes in nine levels,
in the opaque pass and in the additive one alike. The five fan floors of the 13th Floor
(RICH1) show the blade shadow turning with the fan, and the monitor wall of the Security
Breach cutscene (RICHCUT2) plays its loop. The additive books - flames, smoke, candle and
gas flames, the X-ray scanner's figure and rain rings in eight levels - move, and keep the
weight they were drawn at while they stood still: their page time doubles as it. The page
follows the time of the state the frame draws, so at 60 frames a second a 0.05 second
page stands for three presents every time.

**Why.** Under a turning fan in RICH1 the shadow on the vent floor stood still, while a
recording of the Remaster at the same place shows it turning: a soft four-blade shadow
stepping every third frame at 60 frames a second, 0.050 seconds, 12 degrees anticlockwise
seen from above per step, repeating after 1.5 seconds, with the grille and the rivets
still. The measurement found it in the data.
- The book is in the art. Each fan floor is a material 15 part with the parameters (0, 30,
  0.05, 0) on page 3071, and pages 3071 to 3100 are thirty pages of that floor with the
  shadow turned about 12 degrees further on each (355 degrees over the thirty, measured on
  the pages). The same scheme holds in every material 15 part of the game: `param[0]` is a
  phase, a whole number below `param[1]`; `param[1]` is the page count, 14, 29 or 30;
  `param[2]` is the seconds a page stands, 0.025 to 0.2; `param[3]` is 0 in all 168
  part-passes.
- The next pages go by page NUMBER, not by texture layer. RICHCUT2 group 0 part 5 names
  layer 51 = page 1139, and layers 52 to 58 hold parquet, carpets and water (pages 1103,
  1108, 1111, 1109, 1112, 3068 and 3006), while pages 1139 to 1168 are one clean loop of
  the monitors' picture. By page number all 168 part-passes resolve; by layer that one
  breaks.
- The renderer read `param[2]` as an additive weight and nothing else: `MaterialAddWeight`
  answered material 15 with `param[2]`, the entry "The engine's own per-vertex lighting,
  as shader arithmetic" says so ("material 15 from `param[2]`"). The number is a time,
  and read as a weight it is an accident of naming - one the additive books keep (the
  last point below). No material 15 part ever left its own page, so the fan
  floors showed frame 0 for ever; the other pages were already in the page cache, which
  loads every layer of the file, and nothing pointed to them. RoomProbe drew RICH1 room 3
  at `-time` 0, 0.05 and 0.75 byte-identically.
- The HD clock steps once per logic tick in thirtieths of a second and is never
  interpolated. At 60 frames a second a 0.05 second page on it stands for 2, 4, 2, 4
  presents; the recording steps every 3. The time of the drawn state - the tick clock less
  the part of a tick the drawn state stands behind it - steps every 3, 3, 3, and at
  `fps_mode` 0 it is the tick clock itself.
- The file gives an additive book no weight at all, and nothing here measures one. At
  weight 1 the brightest fire adds 253.9 of 255 at the 95th percentile of its share of
  the frame, against the church mist's 8; a gain per picture sheet held to that ceiling
  was measured and fails the small sprites (**Checked**). The page time, read as the
  weight all along, draws the fires and the X-ray figure visibly and stays the weight.

**How.** `specific/hd/hd_book.h` is new and holds the arithmetic alone, with no engine
header, so that the suite includes it as it includes `interpolate_math.h`: `HDBookClock`
(the drawn time, `max(0, clock - (1 - alpha) / 30)`) and `HDBookPage` (`((int)(t / step) +
phase) % count`, in double, the step read to the microsecond and a thousandth of a step
added before the truncation, so that a present landing on a page boundary shows the same
page every time and an hour of play does not drift). `specific/hd/hd_render.cpp`:
- `MaterialBookTime` (`:988`) answers a material 15 part with its `param[2]` when
  `param[1]` is a whole number from 2 to 64, `param[2]` lies in 0.02 to 0.5 seconds and
  `param[0]` is a whole number below `param[1]`, and 0 for everything else.
  `MaterialBookSlots` (`:1026`) resolves pages 1 to N - 1 as `HDPageCacheSlotOf(page0 +
  k)` into `g_book_slots`, once per (first page, count), and returns the index of page 1
  or -1; `RunFirstPage` (`:1060`) gives `page0`, the page the first corner of the part's
  run names. A part whose numbers or pages do not all resolve keeps its own page and is
  counted as dropped.
- In the part loop the book is resolved in every pass (`:3800`-`:3839`); only triangles on
  the book's own first page carry it. `HDRange` and `HDTri` carry `bookTime`, `bookFirst`,
  `bookCount` and `bookPhase` (`:1224`-`:1228`, `:3532`-`:3534`), all four in the sort key
  (`:4189`-`:4192`) and the merge test (`:4289`-`:4292`) beside `flipSlot`, so parts with
  the same book, step and phase share a draw call and parts that differ in any of them
  never do; `bookPart` names the first triangle's part for the log and is not in the key.
- The draw (`:5412`-`:5444`) takes `HDBookPage(g_book_clock, ...)` and the page's slot out
  of `g_book_slots` where `hd_motion` is on; page 0 is the range's own `pageSlot`, so
  `hd_motion` 0 shows page 0 and a book costs no draw call more than a still page.
  `g_book_clock` is the frame's `bookClock` (`:2456`).
- `HDFrameLighting` gains `bookClock` (`hd_render.h:121`). `specific/hd/hd_level.cpp`
  fills it in `FillFrameLighting` (`:2824`-`:2832`) from `g_clock` and
  `InterpFrameAlpha()`, the factor the frame's state was really blended at - 1 at
  `fps_mode` 0, on a frame drawn live, on a camera cut and outside `DrawPhaseGame` - and
  the pickup and sky brackets read the same value (`:4018`, `:4189`).
- The weight: `MaterialAddWeight` (`:371`) answers material 15 with `param[2]`, the page
  time, as it did before the books turned (`:424`-`:430`); the note on the parameter slots
  says the same (`:243`-`:248`). The weight goes into the sort key like every additive
  weight. `HDRenderBookPageOverride` exists for the diagnostic tool alone.
- Seams, behind `-log` (`hd_level.cpp`): the motion line (`:3354`) ends in `book_parts=`,
  `book_pages=` (distinct pages), `book_dropped=`, `book_additive=`, `book_step_min=`,
  `book_step_max=` and `book_ranges=`; one `HDLOG book group= part= page=a..b frames=
  step= phase= pass= weight= dropped=` line per part and pass after the build (`:3387`),
  where an additive book's `weight` equals its `step`; one `HDLOG bookstep level= group=
  part= frame= clock= tick= alpha= present=` line for each of the first 31 page changes
  of the first book the level draws (`:4463`; `present` counts the HD frames filled in the
  level); the frame line ends in `book_calls=` (`:4430`), the draw calls that turned a
  book in the solid bracket and the glow bracket together - `g_frame_book_calls` takes
  the solid bracket's count (`:4365`) and adds the glow bracket's (`:4423`), as
  `g_frame_room_calls` does for all draw calls, because `HDRenderFrameBegin` zeroes the
  renderer's counter per bracket and the opaque books draw in the solid one.
- Tools: RoomProbe drives the books with `-time` and `-nomotion` like everything else that
  moves, prints a `book:` total and one `book` line per part after the build and the draw
  calls that turned a page after the draw, and gains `-bookpage <n>` (every book on page n
  of its run); TrgProbe's `-parts` and `-mat` print each material 15 part's run and how
  many of its pages the file's list holds, `-mat` from each pass's own first page as the
  engine's `RunFirstPage` reads it. InterpTest includes `hd_book.h` and gains 16 checks
  (`books`).

**Checked.** RoomProbe at 1280 x 720 unless said, the classic `.TRC` originals as
`-roombox`.
- RICH1 room 3 from 1536 -700 3300, yaw 180, pitch 22, at `-time` 0.00 to 1.50 by 0.05 (31
  pictures): the only pixels that change over the series are those of the fan floor
  (31,278 pixels in the box x 475..807, y 311..432). The polar step measurement of the
  recording, run on the renders, gives 30 steps from 9.0 to 13.75 degrees, 11.6 on
  average, every one anticlockwise, 348.2 degrees in all; the pages' own steps are 9.5 to
  13.5 degrees and 355 in all, and the render follows them step for step within 0.75
  degrees, the two short steps and the two long ones included - the perspective takes the
  rest. `-time` 0 and 1.5 are byte-identical; `-nomotion` is byte-identical to the
  measurement's picture of `-time` 0, 0 of 921,600 pixels.
- RICH1's seven material 13 parts (groups 10, 31, 34, 147, 150, 162 and 163), each from
  below at 960 x 540 at `-time` 0, 0.2, 0.4 and 0.6: 0 pixels differ between the build
  before and this one, with each flipbook visibly stepping.
- RICHCUT2 group 0 from 5122 2304 3000, yaw 0, fov 60: the picture on the monitor wall
  correlates best with page 1139 at `-time` 0 (0.439, the next page 0.350), with 1140 at
  0.2 (0.480) and with 1141 at 0.4 (0.501) of the thirty pages; 0 pixels differ from the
  build before at `-time` 0.
- All fifteen levels built with `-nodraw`: 168 book part-passes, 0 dropped - ANDREA3 6,
  ANDY1 15, ANDY2 101, ANDY3 32, JOBY3 1, JOBY5 4, RICH1 5 (opaque, 30 pages, 0.050 s),
  RICH2 1, RICHCUT2 3 (one additive, 60 distinct pages). Draw calls: RICH1 room 3 13
  before and after, the level 1,295 and 1,295; ANDY2's largest room (group 24, 38,019
  triangles) 13 and 13, room 6 35 and 39, the level 4,378 and 4,421.
- JOBY2 room 17 and ANDREA1 room 19 with their neighbours, at `-time` 0 and 0.73: the
  frame and the additive pass alone are byte-identical before and after.
- A gain per picture sheet, measured and not used, by the method of the glow gains: the
  frame with material 15 drawn (`-glow -addmat 15`) against the frame with no additive
  material (`-addmat 99`), luma, over the pixels the book's own additive picture lights
  above 2 at weight 1; every page of the book drawn with `-bookpage`, 960 x 540, one fixed
  view at the largest book part of every level that holds the book and one more where the
  level has a second. The 95th percentile at the book's brightest page in that view, at
  weight 1 and at the gain that holds the brightest view to the church mist's 8:

  | view | book | step | weight 1 | at the gain |
  |---|---|---|---|---|
  | JOBY5 room 117 part 6 | 2101 | 0.025 | 253.9 | 7.85 |
  | JOBY5 room 184 part 8 | 2101 | 0.025 | 213.8 | 7.85 |
  | ANDY3 room 159 part 3 | 2101 | 0.1 | 242.4 | 4.43 |
  | JOBY3 room 105 part 3 | 2101 | 0.04 | 237.7 | 5.15 |
  | ANDY1 room 82 part 3 | 2101 | 0.025 | 215.4 | 4.14 |
  | ANDY2 room 6 part 9 | 2101 | 0.1 | 211.9 | 2.00 |
  | RICHCUT2 room 0 part 4 | 2101 | 0.025 | 202.5 | 4.56 |
  | ANDY1 room 161 part 2 | 2101 | 0.025 | 199.2 | 0.00 |
  | ANDREA3 room 59 part 4 | 2101 | 0.05 | 157.4 | 0.43 |
  | ANDY2 room 0 part 4 | 2101 | 0.1 | 154.8 | 1.94 |
  | ANDY3 room 145 part 3 | 2101 | 0.1 | 140.8 | 2.00 |
  | ANDREA3 room 81 part 4 | 2101 | 0.05 | 123.6 | 0.43 |
  | RICH2 room 11 part 2 | 2101 | 0.1 | 109.1 | 0.72 |
  | ANDY2 room 165 part 2 | 3273 | 0.06 | 35.4 | 7.93 |
  | ANDY3 room 34 part 5 | 3273 | 0.06 | 33.1 | 7.64 |
  | ANDY1 room 113 part 5 | 3273 | 0.06 | 26.0 | 5.86 |

  A first gain of 8 over 253.9 for book 2101 left JOBY5 room 184's stacked fire at 35.7;
  0.00705 puts the brightest view at 7.85. For book 3273, 0.2263 puts ANDY2 room 165 at
  7.93. That is one gain per picture sheet, and sheet 2101 holds JOBY5's stacked fires,
  the candles of ANDY2 and ANDREA3, ANDY1's smoke column and RICH2's X-ray figure alike:
  the fires set 0.00705 for all of them, which leaves the candles at 0.43 and 2.0, the
  smoke at 0.00 and the X-ray figure at 0.72 against a plainly visible 109 at weight 1.
  The ceiling was set for room-sized haze and shafts, not for small sprites whose own
  pixels are meant to be bright. ANDY1 room 170's view showed no ring and is left out.
- One sheet per additive level (ANDREA3, ANDY1, ANDY2, ANDY3, JOBY3, JOBY5, RICH2,
  RICHCUT2), at the page time as weight and at the gain, the full frame and the additive
  pass alone, at `-time` 0 and one step later. At the gain the fires of JOBY3, JOBY5,
  ANDY1 and RICHCUT2 play and stay visible, the candle flames of ANDREA3 and ANDY2 and
  ANDY1's smoke are barely there, and RICH2's X-ray figure, drawn at the 0.1 its page
  time gives it, is gone to the eye (0.72 at the 95th percentile, 20,107 pixels darker).
- The page time as weight, against the build before the books turned: RoomProbe at 960 x
  540 with `-glow` and `-time` 0 draws RICH2 room 11 (the X-ray figure, part 2) and JOBY5
  room 184 (the fire, part 8) byte-identically, the frame and the additive pass alone
  with and without depth. ANDY2 room 6 (the candles, part 9) differs in 74 pixels, by 4 of
  255 at most and in none by more than 8: the candle parts of that view with a phase of 7,
  15 or 22, which begin on a later page. With `-nomotion` all three views are
  byte-identical to that build, and at `-time` 0 to the build that first turned the books
  with the page time as weight. RICH1 room 3 at the 31 times and RICHCUT2 at `-time` 0,
  0.2 and 0.4 are byte-identical to the build with the gain table: the opaque books
  carry no weight.
- TrgProbe `-mat 15` over all fifteen levels prints the same 49 lines with the first page
  read per pass as with the first non-empty pass read for all of them.
- The suites: ModernTest 13992, MenuTest 36180, InterpTest 635 -> 651 checks, LipsTest 270
  with the `.OBJ` folder and 243 without, 0 failed. `PCtomb5 built Sep 27 2026 22:08:36`,
  md5 `5e4226bbee965e14a77cb48dfbd31b2b`. Not measured: the game, and with it the frame
  line's `book_calls`.

**Unchanged.** `hd_enable` 0: no classic file is touched, and nothing of this runs before
`HDLevelBuildHD` returns on `HDEnabled()`. `fps_mode` 0: the books' time is the tick
clock. Material 13 keeps its own fields and code; every other material draws as before.
The scrolls, the material 13 flipbooks and the material 2 swings stay on the tick clock.
The classic animated texture range of those floors is still not drawn in an HD room. The
fan model.

**Decisions.** Every material 15 part is a book, opaque and additive. The page comes from
the drawn frame's time, and `hd_motion` 0 shows page 0. The book is not linked to the fan
item: it is time-driven, like the classic range, which `S_AnimateTextures` steps for every
room whatever the fan does, and like the data, which gives a time and nothing else - so
the shadows under RICH1's fans 30 and 90 turn before those fans are triggered, as the
classic game's did. An additive book's weight is its page time, `param[2]`, as the
renderer read it before the books turned: the file carries no weight and no reference
exists to measure one against. The gain per picture sheet held to the church mist's
ceiling is not used, because one sheet holds fires and candles alike and the fires set it
for all of them; a measured weight waits for a reference picture. Parts with the same
book, step and phase may share a draw call, others never.

**Commits.** `bbbecbe`, `d8617b8` (reverted by `0b32731`), `59cd41e`, `4ebb0c8`

### Water in the HD rooms
2026-09-06 - 2026-09-08

**What.** The mark that tells the HD mesh "this is a water surface" is flooded from the
classic mesh on **both** of the engine's water bits - a plain water room, and a
`ROOM_REFLECT` + `ROOM_REFLECT_CEILING` room - each into its own box per room and per
height, a vertex being marked only at a height and inside an extent the classic mesh
already marked. Geometry and UV ride on the marked set at the surface plane, the room's
own `minfloor`, and only on vertices whose normal is vertical; the colour term keeps the
whole mark. On that mask the HD water carries a vertical swell of `HD_WATER_SWELL` = 24
units, a UV wobble of six texels and a per-pixel caustic that modulates the multiply
without adding brightness of its own, the swell fading to nothing within one square of
the flood box's edge (`gWaterBox`, vertex register c15). Material 16 of a `.TRG` is the
still-water surface, drawn at full additive weight like every other material, its three
`param` floats read as a per-pixel ripple of the surface's texture coordinates. The
classic animated water faces a level draws keep the classic path but bind the Remaster's
HD painting of the frame they are showing at that moment. Where a level has a water
surface and the `.TRG` has none, the level's **own** face is drawn on top of the HD room
by the classic path, with its own texinfo and therefore its own animated texture range,
its own light and the shimmer the classic path has always given it; and where a room
would carry both the level's own face and the `.TRG`'s own water sheet in the same
plane, the face the sheet stands on is not drawn (six rooms: JOBY5 4, 27, 28, 83, 115,
184).

**Why.** The HD path carries the classic answer to the HD vertex at the same position,
and on a water surface the classic answers exist only at the corners of the classic
quads; the Remaster remeshes a room, so a triangle whose three corners are all unmarked
got weight zero over its whole area - the busiest horizontal plane of a water room holds
1 to 15 times as many HD vertices as the classic quad grid has corners (JOBY5 room 126:
544 against 36), and in JOBY2's room 67 the corner rule finds 11 of 448.
`ROOMLET::nWVtx` is zero in every level of TR5, so the classic engine never displaces a
water vertex either and what moves its water is the animated texture range; the classic
water term that does exist, the WaterTable shimmer, is per vertex and on a few large
quads reaches the screen as a slow blotch, which TROne (GPLv3) met and describes in the
same terms. Both bits were needed because in the Rome levels the water is all on the
second (ANDREA3 has 291 marked vertices and none on the first, ANDREA2 740 of 809), so
nothing was flooded there and the surface stood still while ANDY1's animated. The
whole-room bit says yes to almost every vertex of such a room, walls
and ceiling included, so flooding it moved the walls in Rome, and the normal test costs
three vertex-shader instructions for the same reason: a wall standing at exactly the
water's height inside the pool's box answers the position test (34 of 257 marked
vertices in one room, 205 of 374 in another are not a flat surface) and would take the
surface's UV wobble and caustic as a sheared triangle on the waterline; the colour term
keeps them because the classic path asks the height and not the normal. The swell lifts
a marked vertex by up to one and a half times 24 units and the vertices at the water's
own edge carry the mark like every other, so the surface rose over the coping it should
meet; it fades within one square because a TR5 water quad is one square and anything
shorter would fade inside a single triangle and read as a crease - measured on a marked
plane, the worst pixel then moves 111 of 255 instead of 255 while the number of pixels
that move at all is unchanged. Material 16 had been read as an additive weight and
dimmed, and twelve rooms then had no visible water surface at all (ANDREA2 231; JOBY5 2,
14, 49, 50, 52, 53, 61, 117, 139, 143, 151): it is a surface and not a brightness - 28
parts in 6 of the 15 levels, 17 of them a double sheet two units apart with opposite
normals, standing 1 to 20 units above the classic `minfloor` of a `ROOM_REFLECT` room,
sharing no vertex with any other material, and its three numbers scale monotonically
with the area of the body of water, (0.1, 1.0, 0.035) for JOBY4's sea of 3,538 floor
squares down to (5.0, 0.1, 0.5) for a four-square puddle. Where the pool looked like
patternless blotches there was no water surface in the HD data at all: over the game's
93 reflecting rooms the classic levels hold water faces in planes the `.TRG` leaves
empty - ANDY3 has 223 classic faces and 0 `.TRG` triangles, ANDREA1 33 and 0 - so the HD
room drew the pool floor and nothing over it. The animated faces show one tile of a
range that `S_AnimateTextures` rotates once every six game steps and the Remaster ships
an HD painting of each frame; without the binding they were the one classic surface left
in an HD room.

**How.** `specific/hd/hd_level.{h,cpp}`, `specific/hd/hd_render.{h,cpp}`,
`specific/hd/hd_shader/hd_room.hlsl`, `specific/drawroom.cpp`. The flood takes, per room
and per height, the axis-aligned box of the marked corners and gives the surface bit to
every HD vertex of that room at that height inside that box - a box and not the whole
plane, so a walkway at the water's height on the far side of the room stays dry, and per
height, so two pools at two levels in one room stay apart; the classic answer for that
bit is itself a decision about a plane (`specific/drawroom.cpp` opens the branch with
`pos.y == CurrentRoomPtr->minfloor`). The swell, the wobble and the caustic are TROne's
water effect (GPLv3, by Todesengel1931, re-derived here): the WaterTable term stays
untouched, parity and all, and the caustic is added as a zero-mean modulation of the
multiply so that it carries no average brightness of its own, its amplitude TROne's 0.32
either way, which matches the measured swing of a body of water between two frames (two
reference shots of one pool differ by 8.4 of 255 on average, 17 per cent of the pixels
more than 16 apart against a mean of 34). The ripple: `MaterialRipple` (`hd_render.cpp`)
reads material 16's three numbers and the per-draw-call constant `gRipple` (pixel
register c36) carries them, `(0, 0, 0)` for every other draw call in the game, where the
shader's one compare on `.y` skips the whole of it; `MaterialAddWeight` lets material 16
fall through to weight 1.0. The anchors come from the TR5 measurement: `param[0]` = 0.8,
the commonest value, is one wave per 1,536 units with a floor of 0.05; `param[2]` = 0.2
is put on the classic water surface's own period of 64 steps of a 30 Hz clock (2.13 s),
so 0.5 becomes 0.85 s and JOBY4's 0.035 twelve seconds; `param[1]` = 1 moves the
coordinate by a tenth of the part's own smaller UV span, 23 of the 28 parts living in a
sub-rectangle of their page with the sampler on CLAMP. The wave is the smoothstep of a
triangle, `abs(frac(p) * 2 - 1)` then `3f^2 - 2f^3`, doubled and centred, which agrees
with a cosine to within 6 per cent of its own amplitude and has the same zero mean and
continuous slope - four sines take the room pixel shader from 453 to 512 slots, exactly
what shader model 3 guarantees, and four of these take it to 476. The vertex shader does
nothing for it: `sway` is 0 on 27 of the 28 parts and a bobbing sheet would tear away
from the stone rim it is let into. Which of `param[0]` and `param[2]` is the frequency
and which the rate is not decided by the measurement; it is written into
`MaterialRipple` as a reversible choice with a one-line reversal beside it. The water
frames: `HDScanLevelTextures` (`specific/hd/hd_scan.cpp`) reads the `.TEX` sidecar - a
page count, that many page numbers, then one `uint16` per object texture - written
against TRForge's format description (MIT; `trf_remaster/Remaster.h` and `HdPages.h`
used as a description and not vendored). Seven of the fifteen levels have animated water
ranges, 81 frames in all, and every frame has a page; `HDWaterFrameFor` (`hd_level.cpp`)
matches by the content a texinfo holds at that moment - page and uv corner, which the
classic rotation copies whole - and two lines in `InsertRoomWater`
(`specific/drawroom.cpp`), inside the HD branch, swap the page and the four coordinates,
the face keeping its light, its shimmer, its sort, its blend and its double-sidedness.
Each frame is a 64 x 64 tile filling its cell, so its HD rectangle is the whole page.
Those pages are appended to the level's own page cache and borrowed into the classic
`Textures[]` array **above `nTextures`**, never by raising that count, because the font
is addressed as `nTextures - 2`, the sky as `- 1` and the menu pages as `- 4` and `- 5`;
the reference is given back by POINTER and not by slot number, because `FreeLevel` calls
`FreeTextures` over `0..nTextures-1` of the OLD level (`specific/texture.cpp`),
`LoadTextures` then refills for the NEW one (`specific/file.cpp`) and only then does
`HDLevelBuild` run, so a borrowed slot number belongs to the next level's pages by the
time it is released (JOBY4 has 13 pages and borrows `Textures[13..20]`, JOBY5 has 19 and
puts its own in `1..18`) - the rule rather than the exception in a randomized run, where
the randomizer's appended pages take a level from 19 to 21 up to 81 to 102; the
`AddRef`'d `IDirect3DTexture9*` is kept beside the slot and the slot cleared only while
`Textures[at].tex` is still it, and the `HDLOG` line says how many were given back and
how many slots the next level had taken over. The restored surface is the level's own
art and nothing is invented - a face qualifies on the engine's own two tests (every
corner at the room's `minfloor`, every corner marked by `aRoomVertexWaterMarks`), and
whether the Remaster already covers that piece of plane is asked of a per-group table of
horizontal art kept by 1024-unit **sector**, the decision then taken per room by
majority, because the 24 triangles the Remaster does draw at that height sit in two
corners (the coping, not the water) and one box round them would span the room, and half
a restored pool is worse than either whole; restoring gives 87 faces in 5 rooms of one
level, 86 in 8 of 30 in another, 34 in 4 in a third, and nothing at all in the two
levels whose surfaces the Remaster really did build, plus JOBY5's 72 faces in 8 rooms
from the per-face rule (`HDRenderGroupWaterArt` over a table built from the material 16
triangles alone). It costs one classic roomlet transform per visible room that carries
restored water and nothing for any other room; with `hd_enable` off there are no
restored faces at all and `InsertRoomWater` is not reached. No page is missing: over all
fifteen levels not one of 12,247,000 vertices is on texture layer 0 and every level's
`fallbackPageTris` is 0. A wave band from the Remaster's effect atlas was measured as an
alternative and rejected - `EFFECT.DDS` rows 936..1008 and 1014..1060 are a dark
modulation strip of mean luminance 24 of 255, not a surface texture - so no page cache
for named sheets was built. JOBY3's thirteen suppressed rooms were measured and left
alone: the art covering those planes is opaque material 0, a diamond-plate deck, and
those rooms carry `ROOM_REFLECT` for wet sheen and not for a pool.
`HDRenderShoreOverride` and `HDRenderUprightOverride` exist for the diagnostic tools
alone and the engine never calls them. `docs/Engine-Manual.md` corrected along the way:
JOBY3 is `LVL5_SUBMARINE` and JOBY4 `LVL5_DEEPSEA_DIVE`, ANDREA2 is
`LVL5_TRAJAN_MARKETS` and ANDREA3 `LVL5_COLOSSEUM` (`game/gameflow.h`). Tools:
`RoomProbe` (`-noshore`, `-noupright`, `-roomplane`, `-plane`, `-watersurvey`, which
prints per room how much of the `.TRG`'s cover is the Remaster's own water sheet, how
many faces the room restores, the level's water frames, their cells, HD pages and
rectangles, and a simulation of the classic rotation, `-flatart`, and the `water:` line
with the water marks and the flooded count), `TrgProbe` (`-layer0`, `-surface`,
`-watergap`, `-matparts`), `DdsProbe` (`-rows`, `-cut`), `Tr5Level`
(`debugAnimTexBlock`).

**Commits.** `e50e94d`, `2059a46`, `34d1b81`, `4b7ffde`

### Address space, the page cache and what the machine can pay for
2026-09-05 - 2026-09-16

**What.** The level's `.DDS` pages are BC7-decoded on the processor over a thread pool
and uploaded as A8R8G8B8 with the whole mip chain, lossless, into `D3DPOOL_DEFAULT`
through one reused system-memory staging texture; decoding runs in byte-budgeted
batches, and both Win32 configurations link `/LARGEADDRESSAWARE`. Two registry values
steer the cost, both registry-only: `hd_texture_budget_mb` (DWORD, default 0 = work the
ceiling out from the card) and `hd_texture_format` (string, default `rgba` = A8R8G8B8
lossless; `dxt5` = BC3, a quarter of the memory). When the pages do not fit the budget
the biggest pages give up their top mip level, repeatedly if one pass is not enough and
never past a page's last level; pages themselves are never dropped. The device's own
limits are asked and answered: no `vs_3_0`/`ps_3_0`, fewer than 512 `ps_3_0` pixel-shader slots,
no `UBYTE4` vertex element or no `A8R8G8B8` texture put the session on the classic path
with one line in the log; no `DXT5` while `dxt5` is asked for falls quietly back to
`rgba`; a `MaxTextureWidth/Height` below a page starts that page far enough down to fit;
`CreateTexture` running out of video memory mid-level puts the whole level back to
classic, naming the page, and the same rule covers the models. A page that cannot be
found anywhere is white, the cache making four white texels itself. How many changes of
outfit a level still allows is worked out from the machine rather than from a constant:
the card's own texture budget, the free address space less a reserve, and whether one
page still fits in a single free block, with the old flat allowance of 64 MiB kept as
the floor of the card condition so the rule is never stricter than it was.

**Why.** The engine is a 32-bit process. For TITLE's 220 pages the peak commit is 270
MiB in `DEFAULT` against 545 MiB in `MANAGED`, and the batches are counted in bytes and
not in pages because 2391 of the 2406 pages are 512x512 and decode to 1.4 MiB while
eleven are 4096x4096 and decode to 85 MiB each - a batch counted in pages would
occasionally ask for one and a half gigabytes. A missing page is a hole in a wall and
reads as a rendering fault, while a page that starts one mip level down is only a softer
wall, the texture coordinates being 0..1 of whatever the texture turns out to be. The
cost sort is stable because almost every comparison is a tie and an unstable sort would
pick a different set each run and make two screenshots of the same wall disagree. The
decision is taken from the DDS headers because an estimate is not a thing to promise a
card: the earlier estimate of four times the file length is now exact to the mebibyte
(TITLE plans 375.8 MiB and loads 375.8 MiB). The white page needed its own texels
because `TEX\WHITE.DDS` exists in `4\TEX` only, so an installation without the fourth
game had no white tile and the wall was not white but gone. 32-bit indices cannot be
worked around for the large levels: a `.TRG` numbers its vertices over the whole level
rather than room by room, so the widest single triangle reaches over 235,507 vertices in
JOBY4 and 1,391,696 in RICH2, only RICHCUT2 (33,585) being under 65536, and the floor
under any cut is one triangle, which cannot be cut. The outfit rule: the page cache
never gives a page back inside a level - other models hold slot indexes - so every
outfit worn in one level stays in it, and that growth was capped at
`HD_REDRESS_ALLOWANCE`, 64 MiB, which is the slack the automatic budget already holds
back on the card and was never a measurement of the player's machine. The wall is not
the address space: measured pool by pool, a 4096-side A8R8G8B8 page in `D3DPOOL_DEFAULT`
takes 0.04 MiB out of the address space and puts 85.25 MiB on the card, where the budget
is already watching it, while the same page managed or in system memory costs the
process its own 85.37 MiB. The address space is still asked for one reason the same
measurement handed over: a `D3DPOOL_DEFAULT` texture cannot be locked, so every mip goes
through one reused `D3DPOOL_SYSTEMMEM` staging texture of the page's whole size - a
single block of 85.34 MiB, which a process holding its free memory in crumbs cannot
make.

**How.** `specific/hd/hd_pagecache.cpp`/`.h` is the cache, with the cost sort, the mip
drop and `ensure_staging`; `specific/bc7.cpp`/`.h` gained `BC7DecodeDDSMip`,
`BC7DDSMipCount` (so a page can be decoded at any level of its chain, `BC7DecodeDDS`
being that call with mip 0 and unchanged in behaviour) and `BC7DDSShape`, which reads
the 148-byte DDS header before a block is decoded. The automatic budget:
`DXCreateD3DDevice` (`specific/dxshell.cpp`/`.h`) asks `GetAvailableTextureMem()` one
instruction after the device exists, the only moment the answer means what it says, and
`specific/hd/hd_level.cpp` takes off the back and depth buffers from the present
parameters, `nTextures` classic pages of `App.TextureSize` squared (measured 7 to 96
pages, 1.8 to 24.0 MiB), this level's HD vertex and index buffers, and 64 MiB of slack
for the driver; the floor is 16 MiB. Measured over all fifteen levels the pages come to
190.6 MiB (JOBY4) up to 423.9 MiB (ANDREA2), rooms, models and Lara adding without
overlap since a page number the rooms use and one a model uses are different name spaces
in the Remaster's art; the title screen's reserve is 155 MiB against 397 MiB of pages,
so any card offering 552 MiB loads it whole, and ANDREA2, the heaviest, needs 595 MiB.
`specific/hd/hd_dxt5.cpp`/`.h` is a BC3 encoder of about 150 lines written here - the
bounding box of each 4x4 block pulled in by a sixteenth at each end, endpoints quantised
to RGB565, indices chosen as the nearest of the four palette entries in the colour the
card will produce with the classic 2:4:1 weighting, alpha always in the eight-value mode
- and uses no `d3dx`, so the port still depends on `d3d9.dll` and nothing else; it
carries its own decoder for `RoomProbe -psnr`, which measures 40.28 dB mean and 35.41 dB
worst over sixteen pages of ANDREA1 while taking them from 279.9 to 70.0 MiB, at 1.46 s
of compression on eight cores and a peak process memory of 136 instead of 270 MiB. The
16-bit index path is built and runs: a draw call is one range and a range is given
16-bit indices rebased on its own lowest vertex with the stream moved there by
`SetStreamSource`'s byte offset; RICHCUT2 uses it today and its picture is
byte-identical to the 32-bit one, so a card without 32-bit indices draws RICHCUT2 in HD
and every other level classic, with the number in the log. The level change holds one
level and not two: `HDLevelBuild` releases the previous level's objects, buffers and
pages at its top, and `HDLevelUnload` makes no Direct3D call at all; `RoomProbe -seq
TITLE,ANDREA2,TITLE` shows free texture memory returning to its starting value after
every level. The out-of-memory path is in `HDRenderLevelBegin` (`hd_render.cpp`/`.h`)
and `HDModelLevelBegin` (`hd_model.cpp`), the settings in `tomb5/tomb5.cpp`/`.h` and
`global/types.h`. The outfit rule is `specific/hd/hd_memory.cpp`/`.h`, pure, with every
input handed in (`HDRedressDecide`): `spent = cache - this level's mark`; `card room =
budget + HD_REDRESS_ALLOWANCE - cache`; `addr room = ullAvailVirtual -
HD_REDRESS_ADDRESS_RESERVE`; `allowed = (card room >= worst || spent < allowance) &&
addr room >= worst && largest free block >= one page`. `HD_REDRESS_WORST` is 20.00 MiB,
the measured cost of the dearest outfit's own pages over a whole wardrobe - a measured
worst case rather than the chosen outfit's exact bytes, because the hint under the F4
list is drawn for the HIGHLIGHTED row on every frame and an exact answer would mean
reading that outfit's `.TRM` parts per row per frame against a model build that costs
278 ms with nine mods. `HD_REDRESS_ADDRESS_RESERVE` is 512 MiB - what a level load, a
Bink film, the save screenshot and the driver's transients have to find after a change
has taken its share; it is not measured and is printed in every level build line so that
a log can settle it. If `GlobalMemoryStatusEx` refuses, the address walk is not made and
`allowed` is `spent < 64 MiB`, the old rule exactly, with a line of its own. On a
machine whose level reports a budget of 3878 MiB and a cache of 465 MiB the rule gives
room for 124 changes where the flat allowance refused the sixth; too generous a card
side costs mip levels and not a texture, and at the floor the pages are loaded anyway
rather than leaving holes. The answer is worked out where something changes it - at the
end of the level build, before and after every change, and once as the F4 window opens -
and `HDLevelRedressAllowed` hands out what was stored, because the answer stands on a
`GlobalMemoryStatusEx` and a walk of the whole address space (203 microseconds over 262
regions) while the F4 window asks on every drawn frame; the only callers of
`HDPageCacheAppend` are the level build and `HDObjectLevelBuild`. Everything new sits
inside `HDEnabled()`, nothing of it is on a drawn frame's path and nothing of it is in
the 60 fps files; the two literal `false` pool arguments in `hd_level.cpp` became one
named `kPagesManaged` of the same value so that the `pool=` the log prints is the pool
the calls really use. Two places where a pointer meets a signed comparison were settled
with the compiler rather than by argument: in `game/savegame.cpp` the guard `if
(creature->enemy < 0) creature->enemy = 0;` was removed, with the reason written where
it stood - MSVC compiles the comparison of an `ITEM_INFO*` with the null pointer
constant as an unsigned comparison, so it is false for every address there is,
`0x90000000` included, the cast the original C had was lost in the decompilation, and
the save writes `enemy - malloc_buffer`, so a null enemy already comes back as 0, while
"repairing" it to `(long)creature->enemy < 0` would be true for every address above 2 GB
and would blank every enemy pointer in a reloaded save; in `specific/alexstuff.cpp`,
`(long)pR->door & 1` is a check that a `short*` is not odd, `& 1` reads bit 0 whatever
the sign and on Win32 a `long` is exactly as wide as a pointer, so it is left as it is.
A sweep of every `(long)`/`(ulong)` cast in the engine found no other one whose operand
is an address and whose result meets a comparison, a division or a shift -
`specific/file.cpp`'s `(long)meshes[lp] / 2` divides a file offset the loader has just
read. Seams behind `-log`: the level build line carries `pool=`, `avail_virtual=`,
`largest_free=`, `reserve=` and `walk_ms=`, a `redress room` line says what is allowed
and what refuses it, the per-change line carries `avail_virtual %.0f -> %.0f MiB (-%.1f
for +%.1f MiB of pages)`, a `redress deferred` line names the condition that refused,
one line covers a refused `GlobalMemoryStatusEx`, and the stats carry `droppedMip0` and
`droppedLevels`. Tools: `RoomProbe` gained `-budget <MiB>`, `-format rgba|dxt5`,
`-psnr`, `-caps maxtex <n>`, `-caps noidx32`, `-seq <A,B,C>` and `-vramhog` (the two
`-caps` switches bending the caps `HDRenderInit` read through `HDRenderCapOverride`,
which the engine never calls and for which there is no registry value), `ModelProbe`
gained `-withrooms <LEVEL>`, and `Preparation/Tools/Diagnose/PoolProbe.cpp` is new -
`-pool` makes real pages of the cache's own shapes in all three pools and measures what
each takes out of the address space, the card and the process's commit, `-walk` walks
the address space, `-grid` calls `HDRedressDecide` itself so that the tables it prints
are the rule and not a description of it; it is built x86 and `/LARGEADDRESSAWARE` like
the engine, without which the same process reads 1995 MiB of free address space instead
of 3986. `tomb5.vcxproj` sets `LargeAddressAware` on both Win32 configurations.

**Commits.** `e185420`, `2435068`, `57d2919`, `798c5ca`, `76e9f15`, `99860cd`,
`9fec064`, `9a261b8`

### HD loading screens from the Remaster, when they are there
2026-09-04 - 2026-09-04

**What.** Chronicles keeps its 21 loading pictures in `screens\SCREENS.STR`: raw 640x480
A1R5G5B5, 614400 bytes each, no header, picture n at offset n * 614400. The Remaster
ships the same pictures redrawn at 1920x1080 as BC7 `.DDS`, one per level and named
after the level; if the player copies those into `screens\HD\`, the engine uses them,
and if the folder is not there nothing changes. None of the pictures ship - the release
carries the empty `screens\HD` folder with a note saying where the Remaster keeps them
(`5\PIX\HD`). The 640x480 pictures are stretched onto the window and always were,
distortion included; the HD ones are 16:9 and are fitted instead, so on a 16:9 window
they land pixel for pixel and on anything else they get black bars rather than a
squeeze. The Security Breach cutscene borrows Red Alert's picture.

**Why.** The pictures are the player's own data from an installation he already owns,
and the path costs nothing when they are absent. Direct3D 9 predates BC7 by about a
decade and cannot sample it, so the blocks have to be unpacked on the CPU. The bars are
painted with a `Clear`, since nothing clears the frame before a loading screen goes
down. `S_DrawTile` needed a filter argument because point sampling is right for a
640x480 picture blown up to the window and wrong for one already at the window's
resolution.

**How.** `specific/bc7.cpp`, `specific/bc7.h`, `specific/hdscreen.cpp`,
`specific/hdscreen.h`, all new; call sites in `specific/file.cpp` and
`specific/LoadSave.cpp`. `bc7.cpp` unpacks the blocks to plain 32 bit pixels and is
written here against the published format definition - no third-party code was taken in,
and `THIRD-PARTY.md` therefore gained no entry for it. Its partition and anchor tables
check each other at run time in `BC7SelfTest`: every partition must start in subset 0,
must really use all of its subsets, and the anchor listed for subset n must be a pixel
that partition actually assigns to subset n; the engine refuses the HD path outright if
that check fails. `Preparation/Tools/Diagnose/DdsProbe.cpp` compiles the same `bc7.cpp`
and writes a `.PNG`, so the decoder is provable without starting the game. The pixels go
into one square power-of-two `A8R8G8B8` texture, 2048 for a 1920x1080 picture, with the
picture in the top left corner: the engine counts texture coordinates in 256ths, and
1920 and 1080 out of 2048 come out as exactly 240 and 135, so the addressing stays
lossless. Which picture belongs to which level is the original code's own answer:
`S_LoadLevelFile(num)` gets `num` from `DoLevel(gf[3], ...)`, and `gf[3]` is the file
name index out of `CMD_LEVEL`, not the slot; the randomizer permutes only the script
offset table, never the file name table nor the indices pointing into it, so `num` is
the level's identity in a vanilla and in a shuffled campaign alike and `num + 2` is the
correct picture in both. The HD file name is taken from the same place,
`gfFilenameWad[gfFilenameOffset[num]]`; a comment in `file.cpp` records that changing
this to `gfIdFromSlot[num]` would look like a repair and would in fact index a slot
table with an identity. The loader writes `HD screen: ...` lines to the log for every
file it refuses, cannot read or decodes.

**Commits.** `ee61e4c`, `33047a4`, `e81dbf7`, `72bc5bf`

### The logo screen and the title menu wordmark
2026-09-04 - 2026-09-04

**What.** Picture 0, the game's logo screen, is the one picture the engine builds rather
than loads, and the only one that picks its HD file by `Gameflow->Language`: `LOGO_DE`,
`LOGO_FR`, `LOGO_US`, and `LOGO_EU` for English and for every language the Remaster did
not draw a logo for. Those four files carry an alpha channel and are composed over
black. The wordmark is cut out of the `LOGO_` file and blended over `TITLE.DDS` at load
time, horizontally centred with the top edge of the lettering one seventy-second of the
picture height down; whichever of the two files is missing simply drops out, so a
half-filled `screens\HD\` still works. The title menu draws the same wordmark: with
`screens\HD\LOGO_<language>.DDS` present the mark out of that file replaces Core's
`data\logo512.pak`, and German, French and the US emblem then appear in the menu at all.
One always-on change goes with it: American English no longer loads `data\uslogo.pak`,
that branch is deleted, and a US player gets `logo512.pak` like everybody else - this
happens with `hd_enable=0` and with no `screens\HD\` folder, so it is a change a US
player sees.

**Why.** The Remaster draws the wordmark alone on a transparent frame and the title
picture without any lettering, so the two only ever appear one after the other and the
screen has to be composed to exist. Core's menu logo is 512x256 of 24 bit colour with
magenta standing in for transparent, cut into two 256x256 pages and drawn as two quads
filling a rectangle 2:1 wide; the language logos `grlogo.pak` and `frlogo.pak` only ever
existed on the respective CDs, so on the Steam edition German and French fall back to
the English one. The Steam data's `uslogo.pak` - and the unused `uklogo.pak` beside it -
carry Tomb Raider IV's "The Last Revelation" wordmark on opaque black, a leftover like
`JAPAN.DAT`, and because the colour key is magenta that black stayed as a box behind the
wrong game's name.

**How.** `HDScreenComposeLogo` in `specific/hdscreen.cpp` and `LoadScreenHDLogo` in
`specific/LoadSave.cpp` for the loading screen; `LoadTitleLogoHD` and `SuperShowLogo` in
`specific/specificfx.cpp` with `HDScreenLogoSize` and `HDScreenCutLogo` in
`specific/hdscreen.cpp` for the menu; call sites and the deleted US branch in
`specific/file.cpp` (`file.cpp:681-685` carries the reason in a comment). Two bounding
boxes are involved and they are not the same: what has to be drawn is every pixel with
any alpha at all, because the lettering sits in a soft black halo reaching about 20
pixels past it on every side, but what has to be placed is only the lettering, alpha
above 128. The blend premultiplies before it interpolates - the files store straight
alpha, so interpolating the colour would drag the black of the transparent pixels into
the letter edges and leave a seam. For the menu the mark is cut with that same alpha
box, resampled with the same premultiplied bilinear tap and put into one texture page,
at most 1024x512 and a power of two, with its alpha intact; the page is addressed with
texture coordinates, so the unused part is never drawn. The menu then draws one quad
instead of two, into exactly the rectangle the pak logo filled, scaled to fit without
distortion and hugging the top edge - the three wordmarks are far wider than 2:1 and
come out as a band across the upper part of that rectangle, the US emblem is nearly 2:1
and fills it. The draw type is the pak logo's own, `specific/polyinsert.cpp` case 1:
alpha blending on, `SRCALPHA`/`INVSRCALPHA`, alpha test against 0, which the pak logo
only ever exercised at alpha 0 or 255 but which blends the HD mark's soft edges
properly. The new page takes the place of the pak logo's left half rather than being
added to the array, because every page in the title level is addressed as a distance
from the end of `Textures[]` - the font is `nTextures-2`, the sky `nTextures-1`, the
logo `nTextures-5` and `-4` - so the count has to stay what it was. The pak is still
decoded first and stays the fallback, which is why a missing, unreadable or empty HD
file costs nothing. `DdsProbe -logo <title> <logo> <out.png>` compiles the same
`hdscreen.cpp` with `HDSCREEN_NO_ENGINE` (which is why that one file is built without
the precompiled header) and writes the finished screen as a PNG, reporting the boxes it
used; `DdsProbe -crop <logo.dds> <out.png>` builds the same texture page and writes it
over a checkerboard.

**Commits.** `c9df47b`, `f928f10`, `b79b649`

### HD models for items, enemies and machinery
2026-09-05 - 2026-09-07

**What.** With the HD runtime on, an item is drawn from the Remaster's own model instead
of its classic meshes: one `.TRM` per slot out of `<game>\ITEM`, skinned on the card and
drawn at every node the model covers. A registry value `hd_objects` (DWORD, default `1`,
same key as the other settings, no options row) switches it off; with `hd_enable` off it
is never entered. An HD object never switches to its MIP slot. `item->mesh_bits` is
honoured per node; `item->meshswap_meshbits` is not, because a `.TRM` is one whole model
with no second geometry for a node (counted per level in the log as `swap_ignored`),
with two exceptions that ship their own models: the Mafia man drawing his pistols
(`MAFIA2_SWAP.TRM` for the torso swap 0x80) and the cyborg's opening panels (the eleven
`HITMAN_<n+1>.TRM` files 5,6,7,8,11,12,13,14,15,16,20 answering panel nodes
4,5,6,7,10,11,12,13,14,15,19 as cumulative bodies, the file chosen by the panel opened
last out of `item_flags[0]` and `hitman_nodes`). The level's own `<LEVEL>.MAP` is in
hand while the models are bound, so a slot gets the model the map names for it before
any name rule applies. Cutscene actors are drawn HD as well, all but actor 1 with the
see-through fade. The model and page search covers games 1 to 5 and no longer falls
through to game 6. Per level this leaves ANDREA1 drawing 50 of its 52 models in HD,
JOBY3 65 of 66 and RICH3 65 of 68; what stays classic is Lara's composite (slots 0-30),
a slot with no model, a model that fails the fit tests, and the fourteen objects whose
draw routine is not `DrawAnimatingItem` (scaled spikes, the three lasers, darts, lift
doors, portal door, gas cloud, flare in flight, thrown weapon). Every HD item is drawn
against the whole screen and not against its room's portal window.

**Why.** The Remaster does not repaint an object's classic tiles: it discards the mesh
and draws a model of its own, so HD art for objects can only come from those models. The
randomizer puts foreign creatures into slots, so a model must be proved to fit the
skeleton it will be hung on, or a body is torn across the room. The light is per vertex
because the classic path is per vertex (`aTransformLightClipMesh` runs the light formula
with each vertex's own normal), and one flat colour per node would draw a chest whose
front and back are equally bright. The matrices are taken from the engine rather than
rebuilt so that animation, frame interpolation and mesh-tree flags stay the game's own.
Without the map in hand, binding fell through to the slot's own name: ANDREA1 built
`loaded=46 dummy=0 missing=107` where the scan of the same files reported 55 bound and
14 DUMMY, 18 of those 46 came out of game 4, and `ANIMATING10` asked for a file that is
in no ITEM folder while ANDREA1's map names `ANIMATING10_ANDREA1`. A `<NAME>_HEAD.TRM`
is not a face alone: measured over all twelve head files of game 5, `PIERRE_HEAD.TRM`
spans y -376..-177 where the body ends at -286 and the head node sits at -342, so it
reaches 165 units below the joint and carries head, neck and a piece of collar; in seven
of the eight files that use both base bones the small bone reaches below the big one and
is the neck. `DrawCutSeqActors` plays actors back with its own frame buffer and its own
light, so the object seam never saw them and every actor but Lara kept his 1999 mesh; a
cutscene actor's body is named after the level slot it fills (`ANIMATING4_RICHCUT2`)
while his face keeps the person's name, so of the twelve `*_HEAD.TRM` of game 5 only
three were reachable by name. Game 6 is Angel of Darkness: a different engine, different
objects, so a name that matched there would be a wrong model rather than a missing one.
The classic path hands every item the whole screen - `PrintObjects` sets
`phd_left`/`phd_top`/`phd_right`/`phd_bottom` to the frame around its item loop - so
clipping HD items to their room's window invented a rule the engine has not got: an
enemy in a neighbouring room was cut to the doorway rectangle and one whose window came
out small was not drawn at all.

**How.** `specific/hd/hd_trm.cpp`/`.h` reads the format, written against TRForge's
measurement (MIT, `ItemModels.h`) and re-measured over the 566 models of game 5: a
24-byte vertex of three position floats, a byte normal biased by 127, a 1-based texture
layer, three bone indices and three weights summing to 255, the two texture coordinates
in the fourth byte of each triple; a model's triangles never span two pages, a part's
three passes are contiguous, and the highest bone index a weighted vertex names is 28,
which allows a joint palette of 32. A `.TRM` bone index is the slot's classic mesh-tree
node, so `specific/hd/hd_model.cpp`/`.h` computes only a coverage list per slot; an
uncovered node is not a classic fallback (in most door slots it is node 0, the invisible
root). The bind offset is folded into the node matrix on the processor (`t' = t - bind *
R`), the bind pose being the mesh tree's accumulated offsets with every rotation left
out. `hd_shader/hd_model.hlsl` (`vs_3_0`/`ps_3_0`, generated to
`hd_model_vs.inc`/`hd_model_ps.inc`) takes the disk vertex unchanged through a `FLOAT3`
+ `UBYTE4` + `UBYTE4` + `UBYTE4N` declaration; 32 joints of three registers at `c8`, the
projection at `c0`, the light list at `c136`-`c159` with count and sun factor at `c160`;
the pixel half shares `gPass` `c4`, `gFog` `c16`, `gCounts` `c17` and `gBulbs` `c40`
with the room shader, and an object writes `c17.x = 0` so the room shader's per-pixel
light loop does not light it twice, while `gPass.y` carries the additive ceiling.
`gPass` is written through `SetPixelShaderConstantF` only, because `fxc` puts two `def`
constants of its own at `c4` and `c5` in the vertex half. The seam is five places in
`DrawAnimatingItem` (`game/draw.cpp`): `HDObjectBegin` after `phd_RotYXZ`, the MIP
branch guarded, node 0 and the node loop in both the plain and the interpolated branch,
and `HDObjectEnd` outside `if (clip)`; the interpolated branch records the matrix
`phd_PutPolygons_I` would have drawn with. `specific/hd/hd_object.cpp`/`.h` is the
engine-facing half; `HDMapLoadForLevel` loads the map in `HDObjectLevelBuild` and
`HDObjectLevelEnd` frees it (`specific/hd/hd_bind.cpp`). Two fit tests per slot at level
build: a weighted vertex naming a node the slot does not have refuses the model outright
(`FIREROPE` fails this in eleven of the fourteen levels), and for a model with the right
node count the bind drift is measured pairwise - the distance between two nodes' vertex
centres in the model against the distance between their bind positions in the mesh tree,
each mesh's own RMS size subtracted - with a limit of **256**, set inside the measured
gap between the worst model that belongs where it stands (239) and the best that does
not (308); a one-node model has no pair and is never refused. Triangles are sorted by
`(page, node)`, every range carrying the node that holds most weight over its three
corners, and consecutive visible ranges of the same page are merged at draw time, so at
`mesh_bits` `-1` the draw-call count is unchanged (`SWAT` builds 16 ranges and draws 2).
Light: suns, points and spots go to the vertex shader in view space, one list for every
node because a node matrix is a rotation without scale; ambient is per object, a point
light's direction and falloff are worked out per surface point, and the activity flags,
the eight-frame fades and range culling stay the engine's own in `CreateLightList`
inside `CalculateObjectLighting`. HD objects are drawn in `HDFrameDraw` after the rooms
and before `DrawBuckets`, inside the bracket the rooms opened, sharing an exact depth
buffer. Shared pieces: `HDPageCacheAppend` (`hd_pagecache.cpp`/`.h`) so models and rooms
keep one texture per page, and `HDRenderStateSave`/`HDRenderStateRestore`
(`hd_render.cpp`/`.h`). `aW2VMx` in `specific/3dmath.cpp`/`.h` was un-`static`ed: the
float twin of `w2v_matrix` puts a light's world position into view space. Slot binding
by name goes through the hand table in `specific/hd/hd_bind.cpp`, which resolves the
randomizer's own slots (`CYBORG` 469 to `HITMAN`, `BLUE_GUARD2` 460 to
`BLUE_GUARD_JOBY`), carries `ROMAN_GOD` -> `ROMAN_GOD_ANDREA_SWORD` (consulted only when
the level's `.MAP` is silent: there is no `ROMAN_GOD.TRM`, and of the fifteen `.MAP`
files only ANDREA2 and ANDREA3 name the slot), and names `VULTURE_CHUNNEL` for slot 43,
because the randomizer's vulture comes from THE LOST ARTIFACT's `chunnel.TR2` and the
I-III Remaster ships `VULTURE.TRM` and `VULTURE_CHUNNEL.TRM` side by side. Mesh-swap
sources are read from `game/setup.cpp` (`MeshSwapSource`) and the run-time bits set in
`game/init.cpp`, `game/hitman.cpp`, `game/mafia2.cpp`, `game/romangod.cpp`,
`game/sas.cpp` and `game/imp.cpp`; the swap models are ordinary models on pseudo slots
and `hitman_nodes` moved from a `static` in `game/hitman.cpp` into `game/hitman.h`. A
head file's neck bone hangs off the head node's PARENT with the file's own weights kept,
so the seam blends rather than tears; `bind_pose_of` returns the classic mesh tree
beside the bind pose. `find_head_model` (`hd_model.cpp`) adds a STRIP rule that cuts the
last `_<QUALIFIER>` off and asks again (17 names over the 1,335 model names of the 4 and
5 folders; ten of them reached a face not theirs through a `_MIP`, and the rule now stops
there, see *Six cutscene faces, and none on a MIP prop* below) and a TABLE of two entries
for bodies carrying no part of the person's name - `ANIMATING4_RICHCUT2` ->
`VON_CROY_SENIOR_HEAD` (7.4 units apart by neck-ring measurement, next nearest face
11.4, sharing texture page 9075) and `ANIMATING6_RICHCUT2` -> `VON_CROY_ELDER_HEAD` (2.7
against 35.3), each confirmed by picture; a face is never given a face of its own.
`DrawCutSeqActors` (`game/deltapak.cpp`) hands the node walk's MATRIX to the HD path
with `duff_item` carrying slot number and mesh bits. Item pages keep the Remaster's own
rule, the alpha test in the alpha pass only, because an item page's alpha is not a
coverage mask (`ROMAN_GOD_ANDREA_SWORD`'s three body pages measure 64.1, 48.7 and 56.4
per cent of texels below alpha 128, PIERRE and BLUE_GUARD share a page at 26.2); the
number is kept as a measurement in `HDPageCacheSlotCutoutPermille`.
`HDFrameScissorForRoom` (`hd_level.cpp`) gives an item the full frame and counts what
the room window would have cut (`items_spared=` on the `HDLOG clip` line); a view serial
(`ViewSerial`, stepped once per `phd_LookAt`, `specific/3dmath.cpp`/`.h`) is written
beside every room matrix and every collected object and reported as `view=`/`view_obj=`,
a counter nothing branches on. Seams with `-log`: per slot one line with its reason
(`baked`, `model_missing`, `skeleton_nodes`, `skeleton_drift`, `no_geometry`), per level
`hd=`/`classic=`/`baked=`, one `HDLOG cutseq actor=` line per actor per cutscene, and on
the first HD object of every level `HDLOG itemcheck …` (each node's origin on the screen
through the classic transform and through the projection the card is handed, worst
disagreement in pixels) and `HDLOG lightcheck …` (the classic light formula and the HD
one on the same normal). `Preparation/Tools/Diagnose/ModelProbe.cpp` (built with
`bmodel.bat`; `bshader.bat` builds the model shader) links the engine's own sources and
draws the same picture without the game (`-nodes`, `-meshbits`, `-all`). Files:
`specific/hd/hd_trm.*`, `hd_model.*`, `hd_object.*`, `hd_pagecache.*`, `hd_render.*`,
`hd_level.*`, `hd_bind.*`, `hd_shader/hd_model.hlsl` and its two `.inc`,
`game/draw.cpp`, `game/deltapak.cpp`, `game/hitman.*`, `game/setup.cpp`,
`game/init.cpp`, `game/mafia2.cpp`, `game/romangod.cpp`, `game/sas.cpp`, `game/imp.cpp`,
`global/types.h`, `specific/3dmath.*`, `specific/output.cpp`, `tomb5/tomb5.cpp`,
`tomb5.vcxproj`, `docs/Engine-Manual.md`.

**Six cutscene faces, and none on a MIP prop** (2026-09-25). *What.* In many cutscenes the
actors stood without heads. The 44 cutscenes of `DATA\cutseq.bin` put 133 actors on 98
level slots (97 in the levels that play them, and cutscene 3, which no level plays); six
bodies among them had a face file on disk that no rule reached, and the strip rule hung a
face that was not theirs on eight props. Bound now, by the face table and keyed on the
model: `PATRICK_1`, the priest (ANDY1's `SAILOR_MIP`, cutscene 19) ->
`PATRICK_BROWN_HEAD`; `PATRICK_2`, the priest (ANDY2's and ANDY3's `SAILOR_MIP`, 33, 35,
37, 38 and 44) -> `PATRICK_WHITE_HEAD`; `ANIMATING6_MIP_JOBY2`, the admiral (JOBY2 25, and
as `CRANE_GUY_MIP` in JOBY3 and JOBY5, 12, 13, 15 and 18) -> `ADMIRAL_HEAD`;
`ANIMATING9_MIP_JOBY2`, Sergie (JOBY2 25, and as `LION_MIP` 15 and 32) -> `SERGIE_HEAD`,
on node 3, which the body leaves empty; `SNIPER_MIP_ANDY1`, the hanged man (21) ->
`HANGMAN_HEAD` on node 21, turned half a turn about the node's x axis; `SWAT_MIP_ANDY3`,
the demon knight (37 and 44) -> `DEMON_HEAD`. The strip rule stops at a stem that ends in
`_MIP`, which takes the faces off `LARSON_MIP_ANDY1` and `PIERRE_MIP_ANDY1` (planks,
cutscene 14), `BLUE_GUARD_MIP_ANDY1` (a stone block with a chain, 14),
`BLUE_GUARD_MIP_JOBY4` (a pillar, 40), `LARSON_MIP_RICH1` (an ID card, 24),
`LARSON_MIP_ANDY3` (the hussar, 36 and 38, whose own face Larson's covered),
`PIERRE_MIP_ANDY3` (a horse) and `LARSON_MIP_JOBY3_JOBY5` (a rack of torpedoes that JOBY3
loads and no cutscene draws).

*Why.* The Remaster names a cutscene body after the slot it fills and a face after the
person, so neither the name rule nor the strip rule reaches these six, and every binding
rests on the engine's own sources and the installation's data. `handle_actor_chatting` in
`game/deltapak.cpp` names the slot and the node each person speaks on: the priest
`SAILOR_MIP` node 2 in cutscenes 19, 33, 35, 37 and 44; the admiral node 2, as
`ANIMATING6_MIP` in 25 and as `CRANE_GUY_MIP` in 12, 13, 15 and 18; Sergie node 3, as
`ANIMATING9_MIP` in 25 and as `LION_MIP` in 15 and 32; the hanged man `SNIPER_MIP` node 21
in 21; the knight `SWAT_MIP` node 15 in 37 and 44. A face's spoken lines are files named
after the audio track of the cutscene that plays them: `PATRICK_BROWN_HEAD_119.TRL` is
cutscene 19's track, `PATRICK_WHITE_HEAD`'s 101, 97, 134 and 135 are 33, 35, 37 and 44,
`ADMIRAL_HEAD`'s 113, 114, 115, 116 and 122 are 25, 18, 15, 13 and 12, `SERGIE_HEAD`'s
113, 115 and 120 are 25, 15 and 32, `HANGMAN_HEAD_132` is 21 and `DEMON_HEAD`'s 134 and
135 are 37 and 44. Body and face share texture pages 9090 and 9091 (the admiral), 9093
(Sergie) and 9107 (the hanged man), and the hussar beside the knight, in the same uniform
on the same pages 9097..9100, carries the demon's face in his own body on `DEMON_HEAD`'s
pages 9113 and 9114.

The hanged man's file stands upside down against the level's own head mesh of node 21, the
mesh the speech table swaps: laid over it, its vertices lie 37.4 units from that mesh on
average as they stand and 7.6 turned half a turn about the node's x axis, the best of
every turn in 30-degree steps about all three axes, where the admiral's, a priest's,
Sergie's and the knight's lie best as they stand (8.3, 9.2, 6.7 and 20.7). The node is the
speech table's: the nearest-node rule would take node 2, 40.4 units from the face's centre
against 44.6 for node 21. Not bound, on purpose: `DEMON_HEAD` on `LARSON_MIP_ANDY3`, whose
shared pages are his own face; `ADMIRAL_HEAD` on any body but its own, although 55.8 per
cent of its neck vertices lie within 10 units of `PATRICK_2`'s collar against 42.5 on its
own body; the two priests' faces the other way round; `ZIP_HEAD`, which no cutscene body
speaks with; and every game 4 face on a game 5 body. The strip rule's claim above is
corrected: `DrawCutSeqActors` draws a `_MIP` slot in HD like any other actor, and over the
1,335 model names of the 4 and 5 folders every one of the ten names that reached a face
through a `_MIP` reached one that is not theirs - the eight props above and game 4's
`JEAN_YVES_MIP_ANG_RACE` and `TROOPS_MIP_ANG_RACE`. The rule now answers for seven names,
all right. ModelProbe's node table called a node the model does not cover a classic
fallback; nothing is drawn there, neither the model nor the classic mesh
(`game/deltapak.cpp` and `game/draw.cpp` hand the HD path only the node's matrix), and it
now says so.

*How.* Each line of `g_head_of_model` (`specific/hd/hd_model.cpp:495`) carries a node (-1
measures it) and `halfTurnX`; `find_head_model` (`:571`) hands the table's line back, and
the face pass rides the face on the table's node and turns it before the neck is looked
for (`:3259`: y and z mirrored about the node's bind, each normal byte 254 minus itself,
so every triangle keeps its winding). `ends_in_mip` (`:551`) stops the strip loop. The
face note gains `, turned half a turn about x` at its end on the hanged man. Proof:
`ModelProbe -all` over the fifteen levels, built from `e1bc056` and from this change - of
1,820 bound slots 19 change, eleven gaining a face (the six models in ANDY1, ANDY2, ANDY3,
JOBY2, JOBY3 and JOBY5) and eight losing one; over the 97 played level slots of the
cutscenes 11 gain, 7 lose and 79 are unchanged, and besides those rows only the level
totals (heads, geometry, pages) differ. Every face lands on the speech table's node: 2 for
the priests and the admiral, 3 for Sergie, 21 for the hanged man, 15 for the knight.
ModelProbe pictures of the six before and after and of the eight props without their
faces. Unchanged: the speech-head mesh swap stays ignored over an HD body (`HDObjectBegin`
counts it), the lips (`.TRL`) are not played, and the actors drawn classic
(`SWAT_PLUS_MIP`, `DOG_MIP`, `GLADIATOR_MIP`, `CROW_MIP`, `LARSON_MIP` in RICH2, and
`CHEF_MIP_RICH1`, refused for drift) keep their classic head and its swap. With
`hd_enable` off no cutscene actor is drawn HD: `HDObjectBegin` returns false before
anything (`hd_object.cpp:2707`).

**Commits.** `2ab35c1`, `d7b4330`, `2ffd951`, `7715dda`, `3fbbd20`, `5d0fd65`,
`72988c3`, `45d9130`, `957c53e`

### The cutscene faces speak: their lips follow the Remaster's track files
2026-09-25 - 2026-09-26

**What.** In HD the faces of the cutscene actors, and Lara's face in the cutscenes she
acts in, move the way the Remaster moves them: the lips part on the spoken line, the jaw
drops, the dark of the mouth and the teeth show, the mouth closes between phrases, and
now and then the eyes blink. The animation is the installation's own. A face
`<FACE>_HEAD.TRM` has one file per spoken line, `<FACE>_HEAD_<track>.TRL` in `5\ITEM`,
where `<track>` is the audio track the cutscene plays: 45 files for 13 faces, 18 of them
Lara's. Past the end of a file the face eases back to rest over ten frames. Between her
lines Lara's face plays the loop of its own block and bares its teeth while she fires -
see *Lara's face between her lines* below.

**Why.** The faces stood still while the voices played: the separate faces are bound (see
*Six cutscene faces, and none on a MIP prop* above), and nothing read their track files.
A track file moves a face's own joints, and which joint each of its matrix columns moves
is written in the face's `.TRM`: between the rig and the geometry every rigged head
carries a block with a header, `[N][id[N]][parent[N]][C][column[C]][frames]`, and frames
of joint matrices whose frame 0 is the pose the face was bound in. The header parses
exactly in all 23 rigged `*_HEAD.TRM` of the IV-VI install's games 4 and 5. Column c
drives rig joint `column[c]` where that is below the rig's joint count and is a helper
node otherwise, and each matrix is local to its parent's. That header is the map, with
no threshold and no vote: a map voted from bind translations puts 12 of its 21 matches
on helper columns that stand where a joint stands, and drives none of the 16 turning lip
and mouth columns and not the chin - at `LARA_HEAD_98` frame 556 its jaw drops and the
lips stay shut, 6.86 units from the header's result.

**How.** `specific/hd/hd_trm.cpp` keeps the rig's inverse bind matrices, the block's
parents and columns and its frame 0 (`keep_block`, `:147`; only when header and frames end
exactly where the geometry begins) and every vertex's own bones and weights (`:559`),
because `rig_neck_of` renames a rigid face's body joints before it builds; the layout
stands at `HDTrmModel::rigBind` (`hd_trm.h:185`). `specific/hd/hd_trl.cpp` (new, with
`hd_trl.h`) reads, maps and deforms. `HDTrlFrames` (`:192`) is the frame rule: the first
word is the frame count where it fills the file at the face's column count, and its low
half otherwise - 15 of the 45 files, every file of `ADMIRAL_HEAD`, `DEMON_HEAD`, the two
`PATRICK` faces and `SERGIE_HEAD`; a file of another column count is refused.
`HDTrlOpen` and `HDTrlReadFrame` (`:226`, `:283`) read one frame at a time, 1920 bytes at
40 columns, because a whole clip is up to 11.5 MB (`DEMON_HEAD_135.TRL`) and cutscene 44
speaks three such faces at once. `HDLipsMapBuild` (`:327`) turns the header into the
driven joints, the parents in composition order, the head joint (the rig joint the driven
columns hang off - 4 in `LARA_HEAD`, 0, 1 or 2 in the others) and the axis order, a table
keyed on the face file's name (`g_axis`, `:134`: `SERGIE_HEAD` (x,-z,y), `HANGMAN_HEAD`
(x,-y,-z), `ZIP_HEAD` refused, the same axes for the other ten), and inverts each
column's composed bind with a general inverse (`:64`), because the columns carry a
scale. `HDLipsSkins` (`:517`) composes a frame, `D_c = G_c * inverse(Gb_c)`, and turns it
about the head joint in the vertices' axes, `S_j = T(head) * A * D_c * A^T * T(-head)`;
every joint no column drives keeps the identity. `HDLipsDeform` (`:645`) blends every
vertex by the file's own bones and weights over 255, carries the normal through the
cofactor matrix at its own length, and copies a vertex whose weights lie only on joints
that do not move.

`specific/hd/hd_model.cpp` keeps a `Lips` (`:124`) beside every separate face and beside a
rigid face whose file carries its block, which is Lara's (`:3585`, and `:3668` for the
separate faces, taken before the hanged man's half turn, which the deformed copy gets
back after the deform). `make_lips` (`:2022`) looks for the track files in game 5's
`ITEM` alone, because track numbers belong to a game: `4\ITEM`, where `LARA_HEAD.TRM`
lies, holds 24 `LARA_HEAD` files numbered after Tomb Raider IV's tracks, three of them
under numbers this game uses for other lines. `lips_stream` (`:2182`) deforms a face when
the frame changes into a buffer of its own, `D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY` in
`D3DPOOL_DEFAULT` (`:2210`), written with `D3DLOCK_DISCARD` (`:2252`) and made on the
first speaking frame, and holds it while the frame stands; `draw_model` binds it in place
of the static buffer (`:2447`) - the same stride, count and order, so the index buffer,
the ranges and the constants stay, and so do the static buffer's bones and weights, which
the deformed copy takes over. Buffer and file go in `lips_close` (`:902`), called by
`HDModelLipsClose`, by `free_model` (`:934`) and so by `HDModelShutdown` in the
device-lost chain (`hd_level.cpp:3323`, `hd_object.cpp:2599`), because `Reset` refuses
while a DEFAULT resource lives. `HDModelLipsOpen` (`:4039`) opens a face's clip, once per
track.

The seam: `DrawCutSeqActors` (`game/deltapak.cpp:3245`) calls `HDObjectLips`
(`hd_object.cpp:2855`) between `HDObjectBegin` and `HDObjectEnd` with the cut's audio
track - none while the credits run, which play no audio - and `GLOBAL_cutseq_frame - 1`,
the frame on screen, because `do_new_cutscene_camera` decodes a frame and then steps the
counter (`deltapak.cpp:3126`). The frame item carries the pair to the draw
(`hd_object.cpp:3583`, `:3597`). Lara's face gets the same pair (`hd_laradraw.cpp:1229`)
while a cut she acts in plays: actor 0 of the cut is not `NO_ITEM`. `HDObjectFrameReset`
closes every clip once `GLOBAL_playing_cutseq` clears (`hd_object.cpp:2616`).

The frame rule: the cutscene frame is the clock. A file's frame index is the cutscene
frame - its motion peaks against the engine's own speech ranges (the `*_chat_ranges`
tables) at a lag of 0 to -6 frames in 19 of the 20 speaker pairs they name - and a
cutscene is drawn without a tween (`game/interpolate.cpp:1228`), so the deform runs on
the integer frame, once per change and never on a blend. The end of a clip:
`HDLipsSourceOf` (`hd_trl.cpp:598`) shows the clip's last frame at (10 - k) / 10, k
frames past it, and from the tenth frame on the bind, which is the static buffer. A
file's last frame stands up to 9.72 units off the bind (`LARSON_HEAD_108.TRL`);
`LARA_HEAD_103` and `LARA_HEAD_119` hold 5608 frames, more than their cuts, and the
frame index never reaches their ends. No audio cursor is read.

With `-log`: at level build `HDLOG level=<L> lips slot=<n> face=<STEM>
file=<STEM>_<track>.TRL frames=<n> cols=<c> joints=<driven>/<rig> axis=<identity |
(x,-z,y) | (x,-y,-z) | -> len=<s>s reason=<ok | no file | refused | columns>`, one per
face and track file, one with `file=-` for a face with none, and `from=4 trl_from=5` at
the end of Lara's (`hd_object.cpp:1904`); while a cut plays `LIPLOG f=<GlobalCounter>
cut=<cutseq_num> frame=<n> track=<n> slot=<n> src=<trl | ease | bind> jaw=<units>
lang=<EN | DE | FR | IT | ES | JP>`, once per frame change of a speaking face, `jaw`
being the farthest vertex's distance from the bind and `lang` the language the audio
path looks for first (`hd_object.cpp:3628`); and at the end of `HDLOG cutseq actor=`
` lips=<track>/<frames>` or ` lips=-` (`deltapak.cpp:3265`), at the end of `HDLOG frame
lara` ` lips=<track>` or ` lips=-` (`hd_laradraw.cpp:1339`). `ModelProbe -lips <FILE.TRL>
<frame>` draws a face at a frame of its track by the same road, `<a>:<b>:<step>` tiles a
sheet, and every run prints the engine's lips lines.

**Checked.** `LipsTest` (`Preparation/Tools/Diagnose`, built by `blips.bat` out of
`hd_trm.cpp`, `hd_trl.cpp` and `hd_install.cpp`, reading the installation read-only):
194 checks, 0 failed. The map of the 12 faces it takes - 26 of `LARA_HEAD`'s 33 joints
driven, 25 in `ADMIRAL_HEAD` and `SERGIE_HEAD`, 5 of `HANGMAN_HEAD`'s 8, 26 in the other
eight - with `ZIP_HEAD` refused; each face's composed frame 0 against its rig's own
joints: at most 0.0002 units in `LARA_HEAD`, `ADMIRAL_HEAD`, `DEMON_HEAD`, the two
`PATRICK` faces and `SERGIE_HEAD`, 0.0005 in `HANGMAN_HEAD`, 0.49 to 0.80 in
`BLUE_GUARD_HEAD`, the two `VON_CROY` faces and `PIERRE_HEAD`, and 3.46 in `LARSON_HEAD`.
Frame 0 through the deform returns every vertex of the 12 faces to itself (at most
2.8e-13 units) and every normal byte exactly. `LARA_HEAD_98` frame 556: the farthest
vertex moves 7.19 units, 1.68 on average, 2198 vertices past a unit, the jaw (joint 18)
turns 10.3 degrees and the chin travels 4.30 units; `PIERRE_HEAD_108` frame 1107 8.18,
`LARSON_HEAD_102` frame 978 12.03. Lara's 176 throat vertices with weight on her body
joints move by their face share alone (to 1.3e-5 units), 61 of them past a unit, the
farthest 6.81; a vertex on no driven joint does not move by a bit. The frame rule over
all 45 files; the ease on its straight line; the ease at 0 the bind bit for bit. Against
an independent implementation of the same arithmetic, written out as `.OBJ` files, the
engine's positions agree to at most 0.0001 units at 13 frames: `LARA_HEAD_98` bind, 646
and 556, `PIERRE_HEAD_108` bind, 1035 and 1107, `LARSON_HEAD_98` bind, 740 and 1056,
`HANGMAN_HEAD_132` bind and 3252, `SERGIE_HEAD_113` bind and 1966.

ModelProbe pictures through the game's draw: `LARA_HEAD_98` at rest, 646 and 556 on
`OUTFIT_CLASSIC`, front and side, and a sheet of every 60th frame; `PIERRE_HEAD_108` at
rest, 1035 and 1107 on ANDREA1's `ANIMATING2_MIP`; `LARSON_HEAD_98` at rest, 740 and
1056 on ANDREA1's `LARSON`; `SERGIE_HEAD_113` at rest and 1966 on JOBY2's
`ANIMATING9_MIP` (with `-meshbits 0x8`: in the bind pose his body lies on its side and
its collar covers the mouth); `HANGMAN_HEAD_132` at rest and 3252 on ANDY1's
`SNIPER_MIP`, half turned. The mouths open on the teeth and the tongue, the corners do
not tear, the throat stays, the hanged man's mouth opens where his face is, and the sheet
shows the mouth changing from frame to frame and a blink at 1020. The farthest vertex
the draw reports is LipsTest's and the `.OBJ` files' (7.19, 8.18, 9.81 at `LARSON_HEAD_98`
1056, 8.88, 10.06). Frame 1301 of `LARA_HEAD_98`, past the ease, draws the picture
without `-lips` byte for byte, and that picture is byte for byte the one `96d79cc`'s
ModelProbe draws. The reader alone (`9fd76bd`) left `ModelProbe -all` over JOBY2, ANDREA1
and ANDY1 unchanged but for the timings and the peak memory, and two pictures byte for
byte. The level lines: JOBY2 11 (`ADMIRAL_HEAD` 5, `SERGIE_HEAD` 3 with
`axis=(x,-z,y)`, `BLUE_GUARD_HEAD_107` on three guard slots), ANDREA1 12 (`LARSON_HEAD`
4, `PIERRE_HEAD` 3 on each of two slots, `BLUE_GUARD_HEAD` 2), ANDY1 7 (`PIERRE_HEAD` 3,
`PATRICK_BROWN_HEAD_119`, `HANGMAN_HEAD_132` with `cols=7 joints=5/8 axis=(x,-y,-z)`,
`BLUE_GUARD_HEAD` 2), and Lara's 18 with `from=4 trl_from=5`; every one `reason=ok`.
ModernTest 10783, MenuTest 35547 and InterpTest 635 checks, 0 failed, as before. Not
measured: a cutscene in the game.

**Unchanged.** With `hd_enable` off no level is built (`HDEnabled`, `hd_level.cpp:2538`),
`HDObjectBegin` returns false (`hd_object.cpp:2738`), so `DrawCutSeqActors` never calls
`HDObjectLips` (the call stands behind `hd`), and Lara's plan and draw return before
anything (`hd_laradraw.cpp:502`, `:875`); the seam reads nothing of the game for an actor
drawn classic and writes nothing for any actor. A classic actor keeps its classic speech
swap; the speech-head swap over an HD body stays ignored. First person draws no face, so
the neck cut and its lid are untouched. A face outside a cutscene gets no track. The
existing log lines are extended at their end only; `INTERPLOG` is as it was.

**One set of files for every language.** The six spoken languages' tracks are equal in
length to the millisecond in 20 of 22 tracks; 113 and 119 are half a second longer in
English. One set of track files serves them all, so the engine indexes a file by the
cutscene frame and stretches nothing, in every language. That is a decision, and a
dubbed line that runs off the English timing keeps the English mouth.

The layout of the track files and the deform - a frame's matrix times the inverse of the
bind's - were found on the data files by TROne (GPLv3) by Todesengel1931 -
https://github.com/todesengel1931; both were measured again on the installation's files,
and the column map is the face's own header.

**Commits.** `9fd76bd`, `1a24c28`, `87ce6db`, `2c70550`, `41a5a68`

### Lara's face between her lines: the blink loop and the firing face
2026-09-26

**What.** In HD, Lara's face moves between her cutscene lines. Her own `LARA_HEAD.TRM`
carries, behind the frame it was bound in, a block of 508 frames of its joints: frames
0-498 are one loop of 16.6 seconds in which her eyes blink eight times (once twice in a
row), glance up to 9.7 degrees and her brows move, while the ten mouth columns, the
nostrils and the cheeks stand at the bind. The engine plays that loop whenever her face
speaks no cutscene line. And while the classic game has `LARA_SCREAM`'s head on her - the
fire key held with a gun ready, and the 16 ticks after each shot a cutscene fires - her HD
face shows the block's frame 501, bared and clenched teeth under knitted brows, eased in
over three ticks and out over three.

**Why.** The classic scream head is `LARA_SKIN`'s head with the same vertices and other
paint, and the HD path draws nothing of it (`LARA_SCREAM` is `HD_LARA_ROLE_SUPPRESS`,
`hd_lara.cpp:552`): her HD face stood still while she fired. Between her lines it stood
still as well, although its own file carries the loop.

**How.** The block. `keep_block` (`specific/hd/hd_trm.cpp:148`) keeps where the block's
frame 0 lies in the file, `blockAt` (`:201`: 2264 in `LARA_HEAD.TRM` and
`LARA_YOUNG_HEAD.TRM`, 2260 in `LARA_CREEPY_HEAD.TRM`, 2648 in `LARSON_HEAD.TRM`), and the
first frame whose matrices equal frame 0's byte for byte, `blockLoop` (`:208`: 499 in the
three faces of Lara, 300 in `LARSON_HEAD`, 0 or 1 in the eleven other rigged heads).
`HDTrlClip` gains `at` and `HDTrlReadFrame` reads at `at + frame * bytes`
(`specific/hd/hd_trl.cpp:373`, 4 for a track file as before); `HDTrlOpenBlock` (`:305`)
opens the block in the `.TRM` as a clip, so its frames are streamed 1920 bytes at a time
like a track file's rather than held, 975,360 bytes.

The mask. A table keyed on the face file's name, `g_loop` (`hd_trl.cpp:154`,
`HDLipsLoopMask` `:721`): the three faces of Lara play the eye columns 12-15, 22 and 23
and the brow columns 16-21 with the helper columns they hang off, 26-29, 33 and 34
(`0x63cfff000`); `LARSON_HEAD` its eye columns 12-15 alone (`0xf000`); every other face 0.
`HDLipsSkins` (`:595`) takes the mask and the block's frame 0: a column outside it takes
frame 0's local matrix before the columns are composed, and its joint keeps the identity
(`:613`), so `HDLipsDeform` copies every vertex on no masked joint. A mask of every
column, or no frame 0, is the old call. The blinks: `HDLipsBlinks` (`:735`) marks the
first frame of the loop at which the upper lids (columns 12 and 13, their turn from the
bind by `HDLipsTurn`, `:695`) stand more than 15 degrees closed and the frame before them
does not.

The two tracks. `HD_LIPS_TRACK_LOOP` 1000 and `HD_LIPS_TRACK_FIRE` 1001
(`specific/hd/hd_model.h:706`), above every audio track. `make_lips` (`hd_model.cpp:2046`)
fills in the loop of a face (`:2108`) - the path of its `.TRM`, `blockAt`, the loop, the
mask, frame 0's locals and the blinks, read once at level build - for a block that loops
after 30 frames or more and a name the table knows. `HDModelLipsOpen` (`:4213`) opens the
block for either track (`:4228`), one clip for both, so a switch between them only forgets
the frame shown. The frame a draw hands is a key (`HDModelLipsLoopKey`,
`HDModelLipsFireKey`, `:4276`, `:4281`), taken apart by `pseudo_source` and `pseudo_skins`
(`:2259`, `:2280`) inside `lips_stream` (`:2347`), which deforms into the face's own
buffer on a change of key as for a track. The loop's key is the loop's frame; after she
dies it carries the ticks since, and the loop's frame eases to the bind the way a track
eases past its end (`HDLipsSourceOf`, ten ticks). The firing face is frame 501 in full,
`step / 3` of the way from the loop's frame of the moment through the mask - from the bind
while she is dead - by `HDLipsSkinsBlend` (`hd_trl.cpp:816`), which from the identities is
the ease of `HDLipsSkins` to the last bit; at the full step the key leaves the loop's
frame out, so a held fire key deforms once. A track whose file is not there is remembered
(`hd_model.cpp:4257`) and asked again it answers 0 without closing the loop, so a cut
whose line her face has no file for does not open the block again every frame.
`HDObjectPush` marks a clip as playing only for a track below 1000 (`hd_object.cpp:3792`),
so `HDObjectFrameReset` (`:2616`) leaves the loop's clip open outside a cut, and the
LIPLOG writer passes over the two tracks' frame changes (`:3631`).

The choice. `scream_head` (`hd_laradraw.cpp:287`): `lara.mesh_ptrs[LM_HEAD]` is
`LARA_SCREAM`'s head mesh and that mesh is not `LARA`'s. `choose_face` (`:373`) runs once
in each `HDLaraDrawFrame` before the face is pushed and picks, in this order: first
person, the face as built (`:411`); the cut's track where her face has its file, exactly
as before (`:413`); the face as built while the frame's mask hides the head node (`:421`)
or the face has no loop (`:423`); the firing face while its step is above 0 (`:425`); the
loop while `lara_item->hit_points` is above 0 (`:434`); and once she is dead the loop's
ease to the bind and then the face as built (`:443`). The step (`HDLipsFireStep`,
`hd_trl.cpp:798`, advanced at `hd_laradraw.cpp:394`) goes up one a tick while the scream
head is on and down one while it is off: 1/3, 2/3 and 1 on, 2/3, 1/3 and the loop off
(`HDLipsEaseIn`, `hd_trl.cpp:787`). The clock is `GlobalCounter` (`game/control.cpp:128`,
stepped once a logic tick at `:289`): the loop's frame is `GlobalCounter` modulo 499, so
the inventory and the pause, which hold the counter, hold the face, the mirror's second
draw shows the same frame, and the counter's wrap at 65536 ticks jumps the loop once in 36
minutes. The state lives in `g_face` and is reset with the level (`face_reset`, `:359`,
from `HDLaraDrawEnd`).

With `-log`: at level build one line per face that keeps lips, after its track lines
(`loop_line`, `hd_model.cpp:2196`), `HDLOG level=<L> lips loop slot=<n> face=<STEM>
frames=<block frames> loop=<n> at=<offset> mask=<hex> blinks=<n> reason=<ok | no block |
no loop | no mask | unmapped>`; on each change of face `LIPLOG f=<GlobalCounter> lara
face=<cut | fire | loop | static> was=<...> slot=<n> frame=<n> why=<cutscene | fire on |
fire off | first person | masked | dead | no block | start>` (`hd_laradraw.cpp:462`),
`start` for the first choice of a level and `frame=-1` for the face as built; on the first
frame of each blink `LIPLOG f=<GlobalCounter> lara blink frame=<n> lid=<degrees>`
(`:475`); and `lips=loop` or `lips=fire` at the end of `HDLOG frame lara` (`:1550`).
`ModelProbe -loop <frame>` or `<a>:<b>:<step>` and `-fire <on>[:<off>]` draw the loop and
the firing face by the same road.

**Checked.** `LipsTest` with the `.OBJ` folder: 270 checks, 0 failed (194 before); without
it 243 (167 before); with the folder of the blocks the measurement before the build
exported as track files as a second argument 274. Its sections 1 to 6 print what the build
of `8321293` prints, number for number. Section 7: `blockAt` and `blockLoop` of the four
faces as above; frame 0 read at `blockAt` is `blockBind` bit for bit in all four; the
blocks of `LARA_HEAD` and `LARSON_HEAD` read that way equal an independent export of them
frame by frame (508 and 301 frames, none differing); the creepy and the young face carry
`LARA_HEAD`'s block byte for byte; over the loop no column outside the mask moves in the
three faces of Lara (in `LARSON_HEAD` 274 of 299 frames move one - his lips - and the mask
leaves them out); over every frame of the loop no vertex off the masked joints moves by a
bit, neither its position nor its normal, while 1845 of `LARA_HEAD`'s 5913 vertices move,
the farthest 6.6811 units, 6.8736 in the young face and 4.4048 in `LARSON_HEAD` (531 of
9619); a mask of every column and a mask without frame 0 are the plain skins bit for bit;
the blinks start at 92, 137, 198, 254, 314, 322, 420 and 468 (the lids 29.8, 17.9, 17.2,
22.3, 21.4, 16.5, 16.2 and 22.4 degrees there) in all three faces of Lara and at 71, 148
and 229 (23.7) in `LARSON_HEAD`; the other eleven faces have no mask. Section 8: the ease
in is 1/3, 2/3, 1; the steps on and off, several ticks at once, and a release half way in;
frame 501 opens the lips 4.08 units with the jaw at 0.00 degrees and moves its farthest
vertex 3.9736 units, frame 506 9.36, 10.89 and 8.9752; the blend from the bind is the ease
of `HDLipsSkins` bit for bit at every step, on the straight line (1.3245, 2.6491, 3.9736),
and step 0 is the bind bit for bit; from loop frame 93 (the upper lid 25.25 degrees
closed) a third of the way to frame 501 (3.65) the lid stands at 15.99.

ModelProbe pictures, `OUTFIT_CLASSIC` in JOBY2 with `-look head -pitch 0 -dist 160`: the
loop every 20th frame from 0 to 480 is, tile by tile, pixel for pixel the picture the
measurement before the build drew of the same frames out of the block exported as a track
file and played through `-lips`; from row 112 of 320 down - the nose tip, the mouth, the
chin - every tile is frame 0's. Frames 92 and 138 at 512 pixels are the measurement's
pixel for pixel; the blink from 84 to 100 differs from its sheet of the eyes alone only in
rows 0 to 91, the brows its file held still. The firing face in full at `-dist 110` is the
measurement's frame 501 pixel for pixel; at ticks 0, 1, 2 and 3 of the swap and 1, 2 and 3
ticks after it the farthest vertex stands at 1.32, 2.65, 3.97, 3.97, 2.65, 1.32 and 0.00
units, the last being the loop's frame 0, and a tick of the release is the same picture as
the tick on the way in with the same step from the same loop frame, byte for byte. From
the blink at frame 92 the eyes open part of the way at the first step. On `OUTFIT_VCI` the
brows move above the glasses and the firing face comes out as on the Rome outfit.
`LARSON_HEAD` on ANDREA1's slot 45, frames 66 to 76, is the measurement's eyes-only sheet
pixel for pixel. No picture shows a tear. The face as built, `LARA_HEAD_98` frame 556 and
`LARSON`'s face as built are the same PNG byte for byte out of the ModelProbe of `8321293`
and out of this one; the printed text differs in the timings, the peak memory and the new
`lips loop` line alone. The level lines: JOBY2 `LARA_HEAD` `frames=508 loop=499 at=2264
mask=63cfff000 blinks=8 reason=ok`, and `reason=no loop` for `BLUE_GUARD_HEAD` on three
slots, `ADMIRAL_HEAD` and `SERGIE_HEAD`; ANDREA1 `LARSON_HEAD` `frames=301 loop=300
at=2648 mask=f000 blinks=3 reason=ok`, and `reason=no loop` for `PIERRE_HEAD` and
`BLUE_GUARD_HEAD` on two slots each. ModernTest 10783, MenuTest 35547 and InterpTest 635
checks, 0 failed, as before. Not measured: the game - the choice of face, the scream test
against a real fire key, the LIPLOG lines, and whether the lids' step at 30 a second shows
against the body's tween.

**Unchanged.** With `hd_enable` off no level is built (`HDEnabled`, `hd_level.cpp:2538`),
so no face keeps a loop, Lara's plan returns before anything (`hd_laradraw.cpp:713`) and
so does her draw (`:1087`), before `choose_face`. The new code reads
`lara.mesh_ptrs[LM_HEAD]`, `lara_item->hit_points`, `objects[LARA_SCREAM]` and
`GlobalCounter` and writes none of them; the classic swaps (`game/larafire.cpp:405`,
`game/deltapak.cpp:2709`) and every classic frame are untouched. First person pushes no
face (`hd_laradraw.cpp:1457`). A cut's track comes first, opens as before, marks its clip
as playing as before and writes its LIPLOG lines as before; when the cut is over the frame
reset closes its clip and the loop opens again at the next draw. Only Lara's draw asks for
the two tracks, so every other face keeps still between its lines. The existing log lines
are extended at their end only; `INTERPLOG` is as it was.

The outfit changer (F4) runs `HDObjectRelease` and `HDObjectLevelBuild`
(`hd_level.cpp:3233`), whose build asks `HDLaraDrawPlan` again, which calls
`HDLaraDrawEnd` and so `face_reset`: the face is built again with its loop and starts over
at the next draw with a `why=start` line. Nothing is built for it beyond that, and it is
not measured in the game.

**Decisions.** The firing face follows the classic head swap and has no rule of its own:
it shows while the fire key is held with a gun ready - an empty gun included, the flare
never - and for the 16 ticks after each scripted shot of a cutscene, and where the classic
head stays on after the gun was put away some other way, the HD face stays with it -
except inside a cut whose line her face speaks, where the line comes first. Its frame is
the block's 501, the clenched teeth, as the counterpart of the classic painted face; 506,
the open shout, is the other candidate, and the number lives in one constant
(`HD_LIPS_FIRE_FRAME`, `hd_trl.h:230`). Three ticks soften the instant classic swap
(`HD_LIPS_EASE_IN_TICKS`, `:234`). The loop stops when she is dead. Her brows move with
her eyes in her loop. `LARSON_HEAD`'s loop plays through his eyes alone, without the two
slow presses of his lips his block also holds. In a cut she acts in whose line her face
has no file for, and in the credits, where the cutscenes replay without their lines, the
firing face and the loop come next in the order: there she blinks, and after the scripted
shots she bares her teeth with the classic head.

**Commits.** `c2697e9`, `218b22c`, `3f18539`

### Statics: baked into the room, shot to pieces, or drawn classic where the art does not cover them
2026-09-05 - 2026-09-08

**What.** Three rules decide what happens to a static or animating inside an HD room. A
slot the level's own `.MAP` calls DUMMY is drawn by nobody, because the Remaster has
baked that object into the room mesh. A destructible static's HD copy is kept in draw
calls of its own and follows the classic mesh's shattered flag, so it disappears when it
is shot. And a classic static is suppressed in favour of the `.TRG`'s art only where
that art really covers it: at least a twentieth of the static's own bounding box,
otherwise it is drawn the classic way inside the HD room.

**Why.** An HD room's `MESH_INFO` list was suppressed whole, and ANDREA1's lamp post and
the lantern on its wall arm each stood twice, slightly apart, once as the baked HD copy
and once as the classic mesh. A DUMMY line in a `.MAP` is a real file with no parts in
it - 240 of the 2,940 bindings across both installs point at `DUMMY.TRM` - and means
draw nothing here as an object, sometimes because the thing is baked into the room mesh
(Todesengel1931's own measurement, TRForge, MIT,
`vendor/trforge/src/trf_remaster/HdMesh.cpp`, the note at `items_dummy`). Measured by
counting Remaster triangles inside each object's own box, in ANDREA1 every DUMMY slot
that has items has the art standing in it and no other slot does: the wall lantern 169
triangles per million cubic units, the alcove 44, the lamp post 59, the arch 30, the
waterfall 12, against a highest of 4.0 for any slot the map does not call DUMMY and 0.01
to 0.04 for the big ones, which is a room's own floor inside an enemy's box. A
destructible static carries its material with bit 15 set, bits 8..14 an instance counter
0..5 and bits 0..7 an ordinary material number (0, 3 or 14 and nothing else): 69 such
parts in six levels and 32 rooms (ANDREA1 6, ANDREA2 5, JOBY2 3, RICH1 5, RICH2 14,
RICH3 36). The counter is an instance index and not a damage stage - JOBY2 room 57
carries 0x8000, 0x8100 and 0x8200 with 552 triangles each side by side at x 1024..2048,
2048..3072 and 3072..4096, same y, same z, 25 units thick; RICH2 room 151 has three
2,226-triangle copies at three corners and RICH3 room 204 four. A group may also cover
only part of its room: 21 statics of ANDREA1 sit in no `.TRG` group at all, above all in
rooms 121 and 221, whose groups do have geometry (27,252 and 2,803 triangles) but whose
boxes stop short of where the statics stand. The coverage threshold is measured: over
the fifteen levels 271 statics catch nothing at all, 44 catch more than nothing but
under a twentieth, and 2,286 catch a twentieth or more, so the threshold sits in the gap
between the two clumps. The 44 were suppressed with nothing to put in their place - a
TR5 palm is two statics, trunk in the ground room (slot 8) and crown in the room above
(slot 9), and ANDREA2 room 125's crowns caught 4 cells of 768, the wall behind them, so
the crown went and the trunk stayed; the same level's room 162 crown catches 313 of 768
and really is in the art.

**How.** The DUMMY test is per ITEM and not per slot, so a room the sidecar marked
classic keeps its object the old way, and it sits ahead of the object path's own guard,
because the second copy comes out of the `.TRG` whether `hd_objects` is on or off; a
model that reads but carries no triangle counts as the same statement, and a donor slot
is never baked, because its model came out of another level file. Six of ANDREA1's DUMMY
items have no Remaster triangles in their box either and nothing is drawn there, which
is the Remaster's own picture. For the destructible pieces, each (group, material) is
one piece whose triangles are kept in draw calls of their own with the id in the sort
key so they never merge with the wall behind them; `specific/hd/hd_level.cpp` pairs a
piece with the classic `MESH_INFO` standing inside its box out of the slots 50 to 57
that `game/control.cpp` calls shatterable, and `MESH_INFO::Flags` bit 0, which
`control.cpp` clears when Lara shoots one, decides per frame. Coverage is asked per
static rather than per room: the renderer keeps, per group, a sorted list of the
256-unit cells its own vertices fall in - a quarter of a floor square on a side, ten
bits per axis, built while the index buffer is filled, about 25 bytes per thousand
vertices - and `HDRenderGroupCellsIn` answers how many cells of a room-local box carry
geometry (`hd_level.cpp:1462`). A static's box is its own visibility box turned by the
mesh's `y_rot` through the engine's own sine table. Where the coverage is under a
twentieth, `DrawStaticObjects` (`game/draw.cpp`) draws that static the classic way
inside the HD room. A part's bounding box cannot answer this, because one part of a
Roman room spans the whole room, and walking triangles per static would be a million
tests per lantern; a static baked into a NEIGHBOUR's group (room 136's static sits in
group 154) is still rightly suppressed in its own room. TROne's `.MAP` `DUMMY` family
list is about moveables baked into the room art and is not a statics rule; it is not
adopted here. Log: `baked_slots=` per level, `reason=baked` per slot, `baked=` per
frame, one `HDLOG ... statics classic=` line per level, one `HDLOG static room=` line
per static that changed sides and `HDLOG static ... classic cells=%d of %d` for the
count. Tools: `RoomProbe -shatter`, `-hasgeom`; `TrgProbe -statics` / `-slot <n>` lists
every static mesh with its box and how much art stands in it. Files:
`specific/hd/hd_level.cpp`/`.h`, `hd_render.cpp`/`.h`, `hd_bind.cpp`/`.h`,
`hd_object.cpp`, `game/draw.cpp`, `Preparation/Tools/Diagnose/RoomProbe.cpp`,
`Preparation/Tools/Diagnose/TrgProbe.cpp`.

**Commits.** `3fbbd20`, `e50e94d`, `5285f99`

### HD models for the pickups: the EXTRA prefix, a mod's own inventory model, and lying on the floor
2026-09-08 - 2026-09-15

**What.** `bind_against` (`specific/hd/hd_bind.cpp`) has a generic third binding step.
After the level's `.MAP` and the hand table, and before a slot falls back to its own
bare name, it tries `EXTRA5_<slot>`, then `EXTRA_<slot>`, then `EXTRA4_<slot>`, each
against `5\ITEM` and then `4\ITEM`, `<slot>` being the engine's own symbol concatenated
case-insensitively. Twenty-four of the 471 slot names resolve this way and 23 of those
are loaded by at least one shipped level: the pistols, the uzi, the shotgun, the HK, the
grapple and its bolt, both medipacks, the lasersight, the binoculars, the flare in both
forms, the crowbar, the burning torch, the two shell casings, the compass and the
crossbow ammunition. The hand table is asked first and a line may name levels: a third
field lists level stems, space separated, case-insensitive, 0 meaning every level -
`BINOCULARS_ITEM` -> `EXTRA5_BINOCULARS_ITEM_RICH` in RICH1, RICH2 and RICH3, and
`HK_ITEM` -> `EXTRA5_HK_ITEM_RICH2` in RICH2. A slot for which a mod ships a
`<BASE>_OPTION` model, and for which no mod ships an `EXTRA_<BASE>_ITEM` of any prefix,
draws that model fitted into the slot's frame, on the ring, in the corner display and on
the floor. And every pickup-family slot that came out HD is measured at level build: the
lowest of the HD model's drawn vertices against the lowest vertex of the classic mesh it
replaces, both in the animation frame the level really draws the item in; deeper by more
than half a unit and the model is lifted by exactly that difference, once, per slot,
straight up in the world, never scaled, turned or moved sideways. Over the fifteen
shipped levels the lift moves twelve slot/model pairs, the largest RICH3's
`CROSSBOW_AMMO1_ITEM` by **126.6 units**; everything else without a mod moves by 7 units
or less (`COMPASS_ITEM` 6.9, `CROWBAR_ITEM` 6.2, `LASERSIGHT_ITEM` 4.2,
`BURNING_TORCH_ITEM` and `PISTOLS_AMMO_ITEM` 2.9, `UZI_ITEM` 1.6, `UZI_AMMO_ITEM` 1.5,
`REVOLVER_ITEM` 1.1, `FLARE_ITEM` 1.0), and with a mod's fitted Uzis on the floor
`UZI_ITEM` lifts 19.2 and its ammunition 6.8.

**Why.** A hand-written line per pickup was the alternative and the names need none:
`5\ITEM` carries 16 `EXTRA5_*` models and `4\ITEM` 52 (23 `EXTRA_*`, 29 `EXTRA4_*`),
with the slot names' own underscores intact. The hand table must win, because the
generic rule has no notion of the other spelling: it resolves `REVOLVER_ITEM` to
`EXTRA5_REVOLVER_ITEM`, Tomb Raider IV's six-shooter, and never reaches the Desert Eagle
variant. Level-bound lines exist because two slots carry a different object in the VCI
levels and no `.MAP` names either: `BINOCULARS_ITEM` is renamed to the headset there
(`game/newinv2.cpp`, `TXT_Headset`) and its model is a name no prefix can build, and
RICH2's HK is the scripted second weapon whose inventory angles `newinv2.cpp` rewrites
for `LVL5_ESCAPE_WITH_THE_IRIS`, so the generic HD HK came out visibly mis-rotated in
that level alone. The I-III Remaster draws the inventory out of `<BASE>_OPTION` models
and IV-VI out of `EXTRA_<BASE>_ITEM`, so a mod written for I-III carries the OPTION
names and nothing looked for them; a mod that brings the IV-VI name has said which
convention it means and wins. An HD replacement model has its own depth, and one that
reaches deeper than the mesh it replaces stands partly in the ground: RICH3 has no Tomb
Raider V model for the grappling gun's ammunition, so the binder's last resort takes
`4/ITEM/EXTRA4_CROSSBOW_AMMO1_ITEM.TRM`, an upright quiver 203 units tall of which 62
per cent stood below the ground line on three real items. The reference is the level's
own classic mesh and not y = 0, because an item is drawn at its animation frame's own
offset and those offsets are large (ANDREA3's `PISTOLS_ITEM` mesh has its deepest point
59 units below its node, its `CROWBAR_ITEM` one unit above). That the mesh's deepest
point IS the floor is the engine's own placement: `InitialisePickup` puts a pickup down
with `item->pos.y_pos -= bounds[3]` (`game/init.cpp:489-490`) for OCB 0, 3, 4, 7, 8 and
11 and `GetBoundsAccurate` hands it the animation frame's own box; walking two levels'
`.TRC` for every object between slots 240 and 360, that stored box bottom equals the
classic mesh's deepest point plus the frame's y offset row for row. Stated against the
mesh, the rule needs no case for a pedestal or a wall place (OCB 2/9), which move the
item and take both meshes with them. The half-unit tolerance - half a millimetre at 1024
units to a metre - is for float rounding on a mesh whose coordinates are SHORTs; the
installation's own files are lifted like any other, six of the eighteen pickup models
ANDREA3 loads reaching below the mesh they replace.

**How.** The trial order is checked exactly as `find_model` (`specific/hd/hd_model.cpp`)
checks a plain name. Two kinds of slot never reach it: Lara's own composite
(`LARA`..`HAIR`), whose branch returns first, and a named `extra_excluded` table holding
`GUN_FLASH` and the twelve pure light, particle and mist slots grouped beside it in
`game/objects.h` (`SMOKE_EMITTER_WHITE`, `SMOKE_EMITTER_BLACK`, `STEAM_EMITTER`,
`EARTHQUAKE`, `BUBBLES`, `WATERFALLMIST`, `COLOURED_LIGHT`, `BLINKING_LIGHT`,
`PULSE_LIGHT`, `STROBE_LIGHT`, `ELECTRICAL_LIGHT`, `LENS_FLARE`); `GUNSHELL`,
`SHOTGUNSHELL`, `FLARE_ITEM` and `CROSSBOW_BOLT` stay in the pool but are drawn outside
any `HDObjectBegin`/`HDObjectEnd` bracket, so a model bound to them costs a vertex
buffer and no page. The answer is cached per slot (`g_alt_cache`, `std::map<int,
AltAnswer>`) so the pointer `HDBindSlot`/`HDBindDonorSlot` return outlives the call,
which matters because `HDObjectLevelBuild`'s donor pass resolves every donor slot's name
first and reads them all back later. Both map loaders remember the level stem
(`HDMapLoadForLevel` as given, `HDMapLoad` from the file name, whether or not the file
exists), a donor's bind hands in the donor's stem, `HDMapFree` forgets it and
`HDMapLevel()` reports it; a hand-table line for another level is skipped and the slot
goes on to the generic rule. The skeleton test is the safety net and needed no new code:
`COMPASS_ITEM` resolves to a two-part model but in 13 of the 15 levels the classic
slot's mesh tree has one node, so the model is refused (`SKELETON: names node 1 of 1`)
and only `RICHCUT2` draws it. `HDBindModOption` (`hd_bind.cpp`/`.h`) derives the OPTION
name the way the third rule derives the EXTRA names; a file out of another game is
fitted by `HDModelRingFit` and `HDModelRingFitOverflow` (`hd_model.cpp`, both out of one
`sort_axes`): centred on its own centre, the principal axes of its vertex cloud sent
onto the target's, scaled by the ratio of the two longest extents, drawn rigidly on the
root node with the correction in front of the engine's own matrix so that the turn
reaches the normals. A bounding box cannot do this - one mod's `UZI_OPTION` is a pair of
guns whose box, 147 x 253 x 159, reads like one tall gun standing up, and matching boxes
laid the pair flat; the two files measure 256 x 236 x 42 (flat along -y) for the vanilla
item against 242 x 240 x 132 (flat along -x) for the mod's, and sending the k-th axis
onto the k-th lays the pair into the plane the vanilla item lies in, the overflow figure
falling from 3.73x to 3.36x for that Uzi and 1.25x to 1.00x for its ammunition. An
eigenvector has no sign, so each axis points to the side on which the cloud reaches
further from its centre of mass and the third is the cross product of the other two,
which means the frame can rotate and never mirror. Fitting the whole model inside the
target's box is refused: a vanilla Uzi pickup is 41 units thick and that rule would
scale an upright gun to a sixth. The in-plane angle is deterministic but not meaningful
- the plane is decided by a factor of nearly six (132.3 against 239.5, 41.6 against
235.7) while the two axes inside it stand 242.3 against 239.5, a gap of 1.2 per cent.
The floor lift: the family is `HDSlotIsPickup` (`hd_bind.cpp`), a slot whose name ends
in `_ITEM`, so the generic prefix and the hand table's names are treated alike; the
frame is `HDFramePose` (`hd_model.cpp`), `DrawAnimatingItem`'s walk with the frame's
rotations in, twelve floats a node, once per slot at level build, the frame's OFFSET and
the item's own rotation left out because `InitialiseItem` zeroes `x_rot` and `z_rot`
(`game/items.cpp:201-202`) and the one place that tilts a pickup puts all three back
before anything is drawn (`PickUpCollision`, `game/pickup.cpp:789-793, 1124-1126`), the
two that tilt in flight zeroing the angles as the thing comes to rest (`FlareControl`,
`game/laraflar.cpp:31-37`; `FlameTorchControl`, `game/flmtorch.cpp:286-287`). Both sides
sweep VERTICES and not boxes - the classic side the mesh's own `aVtx` that
`ProcessMeshData` filled, the HD side keeping the node each vertex rides on so the bind
fold `set_joint` does per node can be undone per point (`HDModelSlot::keepPoints` on
every pickup model, given back by `HDModelReleasePoints` before the first frame; 13
bytes a vertex, 746 KB for ANDREA3 with four mods, 241 KB for the installation's files
alone). `HDModelFloorLift` is one implementation for the plain and the fitted case,
engine and tool, and the lift is added in view space in the object path, so world up is
worked out once per camera; the ring and the corner display are untouched by it.
`first_frame_of` (`hd_object.cpp`) reads `anims[item->anim_number].frame_ptr` at
`frame_number == frame_base` (`GetFrames`, `game/draw.cpp:2443-2467`) and not
`objects[slot].frame_base`, and logs a difference; over the fifteen `.TRC` files this
changes nothing, 1,748 of 1,791 objects carrying their animation's own frame offset and
the other 43 taking the rest-pose fallback. One gap is written down rather than closed:
`InitialiseItem` gives `HK_ITEM`, `HK_AMMO_ITEM`, `CROSSBOW_ITEM` and `REVOLVER_ITEM`
`mesh_bits = 1` (`game/items.cpp:222-225`) so they draw node 0 alone while both sides
sweep every node - one row differs by it (`REVOLVER_ITEM` in JOBY2 and JOBY5, 1.1 units)
- and closing it would mean a lift per item where the lift is per slot. Cost: ANDREA3's
eighteen pickup models are 57,378 vertices through the frame in 360 to 397 microseconds
at level build with four mods, 18,526 in 115 to 121 without, and nothing of it is in a
frame; the `_OPTION` lookup runs once per loaded slot at level build and has its own
line, `HDLOG level=<L> ring asked= of= found= ms=` (asking the OPTION name before the
three EXTRA prefixes took one level's 130 slots with nine mods from 24.6 ms to 7.7 ms,
0.11 ms with no `Mods` folder). Seams behind `-log`: one line per slot `HDLOG bind
slot=<n> name=<sym> via=<map|hand|donor|prefix5|prefix|prefix4|classic> model=<file>
nodes=<trm>/<classic>` and a level summary `HDLOG bind total=N map=N hand=N donor=N
prefix=N classic=N`; `HDLOG level=.. pickup floor slot=.. name=.. model=.. lowest point
N above|under the classic mesh's - lifted by N units|not lifted`, its fitted twin `ring
floor slot=.. takes the fitted <model> on the floor, lifted by N units`, the drawn-frame
line, a `not measured: ..` line, and `item slot=.. name=.. model=.. draws nothing: its
geometry sits on node mask N and this item shows N`. Tools: `MapProbe` and `ModelProbe`
compile against the new `HDBindSlot`/`HDBindDonorSlot` signature (an added
`installRoot`, since the third rule needs a disk to ask, and an added `HDBindVia`
output), `ModelProbe -all` gained a `via` column and a bind-count summary, and `-floor`,
`-nofloorlift`, `-floorcheck`, `-inventory`, `-invrot <x> <y> <z>` and `-rigid` draw and
tabulate the rest. The classic path is untouched: every line is reached only from
`HDObjectLevelBuild`, which runs only from `HDLevelBuild`, which returns at `if
(!HDEnabled() || !g_stem[0]) return;`. Files: `specific/hd/hd_bind.cpp`/`.h`,
`hd_model.cpp`/`.h`, `hd_object.cpp`, `hd_classic.cpp`, `hd_lara.cpp`/`.h`,
`game/newinv2.cpp`, `game/larafire.cpp`, `game/laramisc.cpp`, `game/objects.h`,
`game/setup.cpp`, `game/tomb4fx.cpp`, `game/init.cpp`, `game/items.cpp`,
`game/draw.cpp`, `game/pickup.cpp`, `game/laraflar.cpp`, `game/flmtorch.cpp`,
`docs/Engine-Manual.md`, `Preparation/Tools/Diagnose/ModelProbe.cpp`, `MapProbe.cpp`,
`SfxProbe.cpp`, `Preparation/Tools/README.md`.

**Commits.** `c5e873b`, `28e53cb`, `09e4971`, `71db1ee`, `a9754d6`, `199f4f0`,
`0947713`, `3ca5065`, `5b09f98`, `1ff12e4`, `2d63638`, `39cc6ab`, `60ced36`, `de328c4`,
`e02e1ea`, `3aeb03c`, `377b7ae`, `54eaa88`

### The grappling gun in the hand, and the Desert Eagle in the revolver's slot
2026-09-06 - 2026-09-08

**What.** TR5's revolver slot draws the Desert Eagle - in hand, laser hand, holster,
pickup and ammunition - and the weapon `LARA_CROSSBOW_LASER` draws in the hand is the
grappling gun and not Tomb Raider IV's crossbow with a scope.

**Why.** TR5's revolver slot kept its Tomb Raider IV name and changed its weapon,
exactly as `CROSSBOW_ANIM` became the grappling gun, and the randomizer settles it
outright by copying JOBY2's slots 6, 12, 16, 347 and 348 into every level
(`Randomizer.cpp`, `kWeaponRules`), so a binding by name drew the six-shooter. The
Remaster ships both guns side by side in `4\ITEM` and they are different models -
`HAND_BARE_REVOLVER` has a cylinder and a 1703-vertex `HOLSTER_REVOLVER` on node 4,
`HAND_BARE_DEAGLE` is the slab-sided automatic with a 1270-vertex `HOLSTER_DEAGLE` on
the same node - so the change is a change of names and nothing else. No `.MAP` names
`REVOLVER_ITEM` at all, so the pickup slot fell through to its own name and found
nothing. For the grapple: `game/larafire.cpp` (`WeaponObject`) draws the gun in the hand
from `LARA_CROSSBOW_LASER` whenever `lara.crossbow_type_carried` carries `W_LASERSIGHT`,
and in TR5 that bit is never off - `game/newinv2.cpp`'s pickup of `CROSSBOW_ITEM` in
`LVL5_THIRTEENTH_FLOOR..LVL5_RED_ALERT` writes `W_PRESENT | W_LASERSIGHT | W_AMMO1`
outright, the weapons cheat does the same, `game/laramisc.cpp` does it at a level start,
and `combine_crossbow_lasersight`, the one function that clears it, is not reachable
from TR5's inventory. The Remaster ships `HAND_BARE_GRAPPLE` and `HAND_GLOVES_GRAPPLE`
and no grapple-with-sight model at all.

**How.** The hand table in `specific/hd/hd_bind.cpp` takes the DEAGLE files for hand,
laser hand and holster, binds `REVOLVER_ITEM` to `EXTRA5_REVOLVER_ITEM_JOBY` (the plain
`EXTRA5_REVOLVER_ITEM` is the six-shooter, measured by picture) and the ammunition to
`EXTRA5_REVOLVER_AMMO_ITEM`. `specific/hd/hd_lara.cpp` (`kSlotRole`) maps
`LARA_CROSSBOW_LASER` to `HD_LARA_PART_HAND_GRAPPLE`; the part
`HD_LARA_PART_HAND_GRAPPLE_LASER` is gone from the enum (`hd_lara.h`), the part table,
the rank table and the worn-pairs table. The holster on the back, the pickup on the
floor and the laser dot (an effect of the engine, not a mesh) are unchanged.

**Commits.** `5d0fd65`, `72988c3`, `c5e873b`, `28e53cb`, `09e4971`

### The inventory ring and the pickup display in HD
2026-09-08 - 2026-09-09

**What.** The objects on the inventory ring and the little turning object in the corner
after a pickup are drawn from the Remaster's models, through the same HD seam the world
uses and not through a second render path. `HDInvBegin` / `HDInvNode` / `HDInvEnd`
(`specific/hd/hd_object.cpp`, declared in `hd_object.h`) sit at the four
`phd_PutPolygonsPickup` call sites in `game/newinv2.cpp`, and `InventoryActive` routes
between the two callers: the ring pushes its object onto the frame's HD list, the pickup
display draws its one object on the spot in a bracket of its own. An item with no HD
model, one whose skeleton the model does not fit, and every item with the HD path off
answers false and is drawn classically at the same four call sites, so the ring never
has a hole in it. On the ring, three slots stay classic in every campaign level -
`COMPASS_ITEM` and the two passport pages `PC_LOAD_INV_ITEM` and `PC_SAVE_INV_ITEM` -
and in RICH1 a fourth, `PUZZLE_HOLE8`, the keypad; the pickup display has no classic
object in any playable level.

**Why.** The seam was already there and drew nothing: `DrawInventoryItemMe` walks a mesh
tree exactly as `DrawAnimatingItem` does - a matrix per node, `item->mesh_bits` deciding
which node draws - and only the last step differs, while the inventory's own loop
already calls `S_InitialisePolyList` and `S_OutputPolyList`, which already call
`HDFrameReset` and `HDFrameDraw`. The classic ring projects each item about the item's
own place on the screen (`phd_PutPolygonsPickup` moves `f_centerx`/`f_centery` for the
length of one mesh) while the HD path builds one projection per frame about the screen
centre, and that difference had to be closed without touching the node matrices:
`hd_model.hlsl` skins the vertex normal through the very same `gJoints` matrices and
renormalises, so a sheared node matrix tilts and rescales every normal and the ring's
one sun then lights an item by an amount depending on where on the screen it stands -
93.61 mean brightness with no offset against 64.15 to 108.87 at the ring's four places,
45 per cent apart on one screen. The pickup display could not go on the frame's list at
all: `S_DrawPickup` (`specific/output.cpp`) is called from inside `S_OutputPolyList`
after `HDFrameDraw` has drawn this frame's list, so an item pushed there would suppress
its own classic polygons and be drawn by nobody. The display's light is not the ring's:
`aTransformLightClipMesh` has one line for a sun's strength, `if (!InventoryActive) val
*= 0.75F; else val += val;`, a factor of 2.667, so handing the display the ring's scale
would have made it visibly brighter than the classic display it replaces.

**How.** The offset lives in the projection: `kx = (X - Cx) / persp`, `ky = (Y - Cy) /
persp`, and since the projection divides by `vz`, anything added to `vx` in proportion
to `vz` survives as a constant pixel offset with the depth untouched. `HDModelOffCentre`
(`hd_model.cpp`) writes `_31 = kx * _11` and `_32 = ky * _22` into a copy of the frame's
projection for the length of one draw call and the node matrices reach the card as the
engine's own; a normal never goes through a projection, so the measurement reads 93.61
at all four ring places over the identical 3,108 object pixels, and over 26 sample
normals at each of 24 ring places this road differs from the classic shading term by
4e-9 where the shear road differs by up to 34.6 per cent. There is no scale term:
`AlterFOV` (`specific/3dmath.cpp`) computes `f_persp` from `phd_winheight * 320 / 240`,
a 4:3 implied width whatever the window is, and the HD projection is built from that
very `f_persp`, so the factor is one by construction - measured at 24 ring places at
640x480 and 1920x1080 the projected rectangle agrees to a worst 0.00013 px against a
budget of 1, 519.33 px wide at 4:3 against 1168.50 at 16:9, exactly 1080/480; the
display's eight places at 1920x1080 and 2560x1440 agree to 0.00008 px, 987.76 against
1317.01, exactly 1440/1080. The light is the ring's own and nothing else: the level
lighting is thrown away and `phd_PutPolygonsPickup` puts up an ambient of 8/8/8 and ONE
sun of the item's `pcbright` - `0x202020` at the side, up to `0xA0A0A0` for the selected
one, always `0x7F7F7F` for the display (`S_DrawPickup` passes 0 and
`DrawThreeDeeObject2D` turns 0 into that value) - along `(0, -50, -100)` normalised over
`sqrt(12500)`, with `sunScale` `pickup ? 0.75f : 2.0f`; the classic line reads the
direction out of `D3DMView`, which is the node's matrix, so what it computes is
`dot(n_view, d)`, and in view space the direction is simply `d`. No room cube, no point
lights, no dynamics, no sheen. The item's screen centre goes to `HDModelOffCentre` at
draw time and never into its matrices, and there is no animation, because the ring
stands on frame 0 and never interpolates; `INVOBJ::meshbits` reaches `HDModelDraw` as
`nodeMask`, so a node the ring leaves out draws nothing. Depth needed nothing: the
classic ring goes through the ordinary sort list with `drawtype` 0, which
`HWR_DrawSortList` case 0 (`specific/polyinsert.cpp`) draws with the depth write on, and
both paths write `a - b/z` for the same point; the frozen backdrop sits at `sz` 0.995,
the far plane with `znear` 20 and `zfar` 20480. The fog bulbs are taken off a ring frame
(`hd_level.cpp`, `HDFrameDraw`), because `ActiveFogBulbs` holds positions the game
turned into view space for the last played frame while the ring draws through an
identity view; the distance fog stays. The display's bracket is
`HDFramePickupBegin`/`HDFramePickupEnd` (`hd_level.cpp`), which are
`HDRenderFrameBegin`/`HDRenderFrameEnd`, so there is one HD path and one list of device
states; the frame's list is untouched and `g_count` is not incremented, and
`HDRenderInFrame()` (`hd_render.cpp`) reports whether a bracket is already open, because
`HDRenderFrameBegin` refuses to nest silently. Its projection is read at the call -
`f_persp`, `f_centerx`, `f_centery`, `f_a`, `f_b` - rather than from
`HDFrameProjectionInfo`, because `AlterFOV` moves `f_persp` inside a frame whenever the
binoculars are up, and the classic display projects with `f_mpersp`, which is `f_persp`
to the bit. Its place is `X = phd_winwidth - GetFixedScale(80) + PickupX`, `Y =
phd_winheight - GetFixedScale(75)` plus `INVOBJ::yoff`, so the slide of `DrawPickups`
(`game/health.cpp`; `pu->life` 45, `PickupX` from `GetFixedScale(128)` to 0) comes for
free through `kx`; there is no alpha ramp on either path, and the display sits below the
frame's second depth clear and above `Fade()`. One deviation: `DrawInventoryItemMe`
scales the matrix non-uniformly for `PUZZLE_HOLE8` in keypad mode (1.5 / 1.0 / 0.25),
and RICH2's keypad is drawn HD, so that scale reaches the shader - the classic path
computes `dot(M n, d)` without renormalising and the shader must renormalise, so that
one object shades slightly differently. The material clock is sampled on ring frames but
does not run: `HDFrameDraw` no longer returns early on an inventory frame, so
`FillFrameLighting` and `AdvanceClock` execute, but the clock is the difference of
`GlobalCounter`, stepped in `ControlPhase`'s tick loop while `S_CallInventory2` blocks
inside it, so every ring frame adds zero; the title menu has no ring, `TitleOptions`
(`game/gameflow.cpp`) being `PrintString` over the title level. Sixty frames a second
changes nothing here: `HDObjectFrameReset` runs once per drawn frame,
`GameFrameMinTicks()` holds the inventory at thirty presents, and the display's spin is
`(GnFrameCounter & 0x7F) << 9` with `GnFrameCounter` stepped once per logic tick. Seams:
`HDLOG ring items=N hd=N classic=N` once per opening plus one line per object (`slot`,
`name`, `model`, `nodes`, `mask`, view `z`, `via`), and `HDLOG pickupdisplay
level=<stem> slots=N hd=N classic=<list>` at build time plus one line per object the
first time it is shown. Tools: `ModelProbe -ringcheck`, `-ring <x> <y>`, `-ringlight
<bright>`, `-pickupcheck`; with the wider state list `RoomProbe` uses, 0 of 58 watched
device, sampler and texture stage states change across the display's draw. Classic path
untouched: `HDInvBegin` answers false at `if (!g_active || !tomb5.hd_objects)`, so the
four call sites run the same calls with the same arguments, `g_ring_frame` is set after
that test, and `HDModelOffCentre` with `(0, 0)` returns before it touches a register;
`fps_mode` is not read. Files: `specific/hd/hd_object.cpp`/`.h`, `hd_model.cpp`/`.h`,
`hd_level.cpp`/`.h`, `hd_render.cpp`/`.h`, `hd_shader/hd_model.hlsl`,
`game/newinv2.cpp`, `game/health.cpp`, `game/pickup.cpp`, `specific/output.cpp`,
`specific/polyinsert.cpp`, `specific/3dmath.cpp`,
`Preparation/Tools/Diagnose/ModelProbe.cpp`.

**Commits.** `d2c3b15`, `6588c6c`

### The seam for Lara: the HD composite drawn by the engine
2026-09-05 - 2026-09-13

**What.** `DrawLara` hands its drawing variants over to the HD composite. `DrawLara__1`
(everything but the catsuit and the diving suit) and `DrawLara__4` (the catsuit) leave
the function after `CalculateObjectLightingLara()`, popping the matrix they pushed and
restoring the clipping window as their own tail does; everything they would have drawn -
the skinned body, the fourteen seam pieces from `LARA_SKIN_JOINTS`, `DrawHair`, the two
thigh holsters and the weapon on the back - is what the composite replaces.
`DrawLara__5` (the diving suit) skips only its skin loop and still draws the two
`LARA_EXTRA_MESH1` thrusters; `DrawLara__6` (the skeleton cheat) is untouched. Her parts
go on the object path's own frame list, in the classic order, and she has no registry
value of her own: `hd_objects` covers her. What one of the hands holds is not drawn a
second time on the thigh or on the back. No outfit wears gloves: `takes_gloves` answers
false for every outfit, so no `HAND_GLOVES_*` model is ever named, while the wardrobe's
`gloves` flag, `outfit.txt`'s `gloves=` line and `kPart`'s gloves column are still
parsed, printed and logged. The `.TRM` reader folds mirrored hand weights back onto the
near hand. The braid's root sits at the hair knot while the HD wardrobe draws, it is
pushed out of a backpack box, and each drawn bone is rolled about its own axis. Known
losses, named: the catsuit's environment-map sheen, the diving suit's two classic
thrusters, and the muzzle flash, gun shells and flare in flight, which are drawn by
their own routines outside `DrawLara`. A talking head is not reproduced: the engine
records which of the four speech-head slots it has put on Lara's neck and draws the
ordinary head.

**Why.** The matrices are the engine's own so that animation, frame interpolation and
`gar_RotYXZsuperpack` stay what the game computed this frame. `mesh_bits` are read for
`DrawLara__1`, which reads them, and not for the two variants that do not, because a
seam that read them where the classic path does not would make the catsuit disappear in
the very cutscenes that hide the classic joints. The ambient is per node because a body
half in the water is lit by two rooms at once and the classic path switches per node;
the hands are identified by pointer comparison because the engine keeps them in
`lara.mesh_ptrs` as pointers and not as slot numbers, and a hand it cannot place is
drawn as the rest hand. The braid is built from the base of the matrix stack and not
from `aW2VMx`, because `phd_GenerateW2V` puts the poison shake on the stack's base and
leaves `aW2VMx` alone. The mirror needs no special handling: TR5 reflects Lara by moving
her - `game/mirror.cpp` negates `x_rot` and `y_rot`, adds half a turn to `z_rot` and
`CalcLaraMatrices(2)` scales the matrix by -1 on all three axes (`ScaleCurrentMatrix`,
`game/delstuff.cpp`), a determinant of -1, so every winding flips and the classic engine
turns its own software back-face test round with `SetCullCW`/`SetCullCCW`; the HD path
draws double sided (`D3DCULL_NONE`), so a flipped winding is invisible to it, and the
hair is mirrored in world space by the same function. The braid's root had to move
because the classic braid hides the distance between root and skull by SKINNING -
`DrawHair` draws meshes 1, 3 and 5 rigidly and 0, 2 and 4 as connectors stitched out of
stashed vertices - while an HD link is rigid: `game/hair.cpp` walks to the head bone and
translates by (-4, -4, -48), and measured on the skull (one mesh for all eight bodies,
head joint at 1/-247/-12, root at -3/-251/-60, nearest skull vertex at
-0.7/-251.2/-43.3) the root sits 16.8 units away in +z while `HAIR_OG`'s link 0 reaches
only 1.7 units back, leaving 15.2 units open. The five spheres `HairControl` pushes the
braid out of are all CLASSIC meshes - hips, torso, head, the two upper arms - and an HD
outfit's pack stands 92 units behind the torso joint, the same slab in all seven braided
outfits (centre 1/-132/-61, radius 71 to 83, y -197..-68, z -92..-30). The roll exists
because `game/hair.cpp` gives link n-1 the spherical angles of the vector to link n out
of differences of LONGS: hanging straight down, `dy` is the whole bone, 56, while `dx`
and `dz` are single digits, so the yaw is the rounding of two small integers and one
unit of `dx` swings it ninety degrees; TR5 survives that with three rigid meshes and
three skinned connectors and `GetCorrectStashPoints`, a rigid plait of six bones does
not. Measured over ANDREA2's own animation table at 110 frames apiece, the standing
animation 103 moves the tip less than two units on every frame and yet turns a bone by
up to 180 degrees on 14 of them, while run and walk turn by at most 7 - the fault is in
the drawing and not the physics, and the same table with the pack spheres out, damped or
thinner moves the tip identically to two decimals. The root and the pack had at first
been changed in only one of `HairControl`'s two frame branches, so the root jumped
between z -48 and z -33 from frame to frame, and since `hairs[0][0]` seeds the chain and
every link's velocity is how far it moved since the last frame, that fed the braid 15
units of velocity for ever. The gloves are off so that the hands are uniform across
outfits and levels. The mirror fold exists because a `HAND_` file is modelled once and
mirrored, and an exporter that mirrors geometry without bone names leaves vertices of
one hand carrying weight on the other: nothing shows in the bind pose, and an animation
that moves the hands apart drags them into the gap as a sheet of skin out of the wrist.
A talking head is a MESH SWAP inside the normal draw (`handle_lara_chatting` writes
`lara.mesh_ptrs[LM_HEAD]` from one of the four `LARA_SPEECH_HEAD1..4` slots;
`handle_actor_chatting` sets the node's bit in `cutseq_meshswapbits` and writes an
`ACTOR*_SPEECH_HEAD*` mesh), and both ends are meshes the HD path has not got: no `.MAP`
names the six speech-head slots and neither `4\ITEM` nor `5\ITEM` ships a
`LARA_SPEECH_HEAD1.TRM`. What the Remaster does instead is one rigged head per speaker
plus a `.TRL` file per spoken line (`LARA_HEAD_96.TRL`..`LARA_HEAD_135.TRL`,
`ADMIRAL_HEAD_113.TRL`, `PIERRE_HEAD_108.TRL`, 2.6 MiB apiece), a format nothing here
reads; inventing a mouth movement would be inventing a look.

**How.** `specific/hd/hd_laradraw.cpp`/`.h` is the engine-side half that reads TR5's
state and turns it into arguments for `hd_lara` and `hd_model`; `hd_lara.cpp` stays
engine-free. Three single `if`s in `specific/drawlara.cpp`, one per variant, directly
after `CalculateObjectLightingLara()`. `lara_matrices` already holds fifteen 3x4
view-space matrices built this frame by `CalcLaraMatrices`, in `LMX_` order, and one
table turns them into mesh-tree `LM_` nodes - the whole difference is the head, matrix 8
against node 14. Per frame the parts come from state `DrawLara` already holds:
`LHolster`, `lara.holster` and `lara.back_gun` are slot numbers; the hands are found by
comparing `lara.mesh_ptrs` against `meshes[objects[s].mesh_index + 2 * LM_xHAND]` over
Lara's own loaded slots, the same test `game/larafire.cpp` makes for the flare, at most
a dozen comparisons twice a frame. When the two hands differ - the flare, the torch, the
crowbar - both hand models are drawn and each masked to its own arm through `hd_model`'s
`mesh_bits` argument (8, 9, 10 right; 11, 12, 13 left); when they agree it is one draw
with every node, and the same split answers the two holster slots. The held-item rule is
one table and one lookup a frame in `HDLaraFrameParts` (`hd_lara.cpp`) rather than a
reach into the engine's weapon state machine, because the engine's three variables are
bookkeeping and not fact - `draw_pistol_meshes` (`game/lara2gun.cpp`) clears
`lara.holster` for every weapon and `LHolster` for all but the revolver,
`draw_shotgun_meshes` (`game/lara1gun.cpp`) clears `lara.back_gun` and neither holster,
and `InitialiseNewWeapon` (`game/larafire.cpp`) sends the revolver down the second path
- so a weapon could be drawn in the hand and in the holster at once. The braid needs all
six link matrices where TR5 builds only 1, 3 and 5; the angles for the other three are
already written by `HairControl` for every one of `hairs[0][0..5]`, so all six are built
the same way the rigid ones are, with `phd_TranslateAbs`, `phd_RotY`, `phd_RotX`.
`HD_LARA_BRAID_ROOT_Z_HD` is -33 against the classic `HD_LARA_BRAID_ROOT_Z` -48
(`hd_lara.h`), applied in `HairControl` and only while the HD wardrobe draws, read from
one flag once per frame and applied in BOTH frame branches; with the HD path off
`HairControl` is the function it was. The pack is `HdPackFrame`/`HdPackPush` in
`game/hair.cpp`, a BOX in the torso node's own frame serving all seven adult bodies (x
-60..62, y -197..-68, z -92..-30, the coat tail left to the torso sphere), pushed out
along whichever of the six faces is nearest with the rear face preferred (a divisor of 2
on its own depth) and a margin per moving point of the plait's own measured radius, 18,
18, 15, 13, 11 and 9. Simulated sixty steps in the bind pose the chain comes to rest at
zero movement from either root, the deepest a link stands inside the pack falls from 38
units with spheres to 7 with the box, and under her own animations the tip never moves
more than 14 units in a frame. The roll is `HDLaraBraidRollStep` / `HDLaraBraidRolls`
(`hd_laradraw.cpp`/`.h`): a `phd_RotZ` after `phd_RotY` and `phd_RotX` in
`braid_matrices`, which costs nothing because the drawn bone's own +z is the link
direction. ONE reference serves all six bones and it is the azimuth of the WHOLE braid,
root to tip, six bones long where a link is one and ten to thirty times better
conditioned; it is HELD while even that is noise - below 16 units of horizontal span it
does not move, above 48 it follows completely, in between part of the way - so no
threshold is crossed in one frame. Over the three ANDREA2 animations its worst one-frame
turn is 0 standing, 3 running, 1 walking, against 180/7/7 for the raw yaw and 45/3/1 for
an unheld whole-chain azimuth; it is advanced from `HairControl`, the one place per
frame where the chain is new, because the draw may run twice (the mirror) or not at all
(first person). Light goes through `HDObjectLights` in view space, twelve lights, the
same conversion the object path uses; `InitObjectLighting` runs once with
`bLaraUnderWater` forced dry, answering `LaraNodeAmbient[0]`, and the wet answer is read
out of `LaraNodeAmbient[1]`, because a second call would push every dynamic light onto
the list twice. Two new calls in `hd_object.h` (`HDObjectLights`, `HDObjectPush`) and
its bind-pose walk `HDObjectBindPose` put her parts on the one frame list. `HairControl`
is called from `game/control.cpp` in the game loop and not from `DrawLara`, so the
braid's simulation runs on while it is not drawn. `HDTrmFoldMirrored`
(`specific/hd/hd_trm.cpp`, counters and layout note in `hd_trm.h`) moves the far side's
weight onto that bone's counterpart on the near side, decided by the vertex's majority
side with the sign of its own `x` as the tie-break, and renormalises the three weights
to 255; only weights above `HD_LARA_HAND_FOLD_BAR` (125 per mille, `hd_lara.h`, read at
`HDLaraPlan`) are folded, so files whose crossed weight is exporter rounding come out of
the reader byte for byte. The bar is a measurement: over all 101 hand files - the
install's 94 (`4\ITEM` holds thirty `HAND_BARE_`, thirty `HAND_GLOVES_`, thirty
`HAND_XRAY_` and four `HANDS_BARE_`; games 5 and 6 have none) and seven from three gun
mods - there are 6,723 crossed vertices whose share on the far hand is one of exactly
two things: 4 per mille for 6,677 of them (one weight unit of 255, the exporter's
rounding; 5,884 of these are the install's own out of its 485,491 hand vertices) and 371
to 500 per mille for 46, all in one mod's `HAND_BARE_PISTOL`, four of them exactly half.
Nothing lies between, so an eighth sits in a total gap, 31 times the largest share the
Remaster's own files carry and a third of the smallest a mod's do; folding below it
would move the seven affected hand parts by at most 0.56 units with both forearms turned
50 degrees out, 0.84 with the upper arms straight out, 0.97 with both, and 2.00 for one
part in a contortion no animation reaches, while the mod's 46 vertices move up to 65.49
units (mean 51.27). Of the 6,723 crossed vertices 6,719 have a majority and on every one
the majority side and the sign of `x` agree; the four without are the ties the rule
exists for. In first person the head node, the separate `LARA_HEAD` and the braid are
dropped, which is what the classic path drops; the classic reason for hiding the head
through the draw rather than through `mesh_bits` - the neighbours pull their rim points
out of the head mesh's stash - does not apply, because an HD model has no stash.
`speech_head_slot()` (`hd_laradraw.cpp`) says which of the four slots the engine has
just put on her neck and the frame log carries it as `speechhead=`. Seams with `-log`:
two lines per level on the first HD Lara frame, `HDLOG laracheck` (each of the fifteen
joint origins on the screen twice, the classic transform against the matrix the card is
handed) and `HDLOG braidcheck` (each braid link's own frame stepped one bone along,
measuring how far it misses the next link); one `HAIRLOG` line per braid per frame with
the root the frame really used, the room position of the root, how many spheres it
tested, `packhits`, whether the HD wardrobe is drawing, where the tip ended up,
`rootstep`, `tipstep`, `dyaw` (the worst one-frame change of any link's raw yaw, in
degrees) and `droll` (the same after the roll, `-1` for the classic braid) - a line on
which the root or the sphere count changes is the two-branch fault back again; `HDLOG
... lara part=` carries `held=` and `pagesheen=` per part, and the `.TRM` fold counters
`crossed`, `folded` and `tied`. `ModelProbe -laracheck` runs the joint and chain checks
without the game: 0.00000 px over all fifteen joints and a worst chain gap of 0.00006
units over the five braid joints; `-firstperson`, `-mirror`, `-braid`, `-braidsim`,
`-braidanim`, `-braidrest`, `-classicroot`, `-handfold`, `-nofold`, `-nohandmix`,
`-onlypart`, `-nodes`, `-meshbits`, `-handlight`, `-oldskin`, `-nosheen`, `-look head`,
`-look wrist` draw and measure the rest, `-handfold` skinning every vertex twice through
the same node matrices the card is given and calling the engine's own fold rather than a
copy, `-braidanim` reading the level's own animation table and rebuilding all eight
collision spheres and the braid root once per game frame with a floor clamp. `hd_model`
exports `HDModelFindFile` so a tool asks the engine's own model search instead of
keeping a copy. Files: `specific/hd/hd_laradraw.cpp`/`.h`, `hd_lara.cpp`/`.h`,
`hd_object.cpp`/`.h`, `hd_model.cpp`/`.h`, `hd_trm.cpp`/`.h`, `hd_wardrobe.cpp`/`.h`,
`hd_pagecache.cpp`/`.h`, `specific/drawlara.cpp`, `game/hair.cpp`, `game/control.cpp`,
`game/larafire.cpp`, `game/mirror.cpp`, `game/delstuff.cpp`, `game/draw.cpp`,
`tomb5.vcxproj`, `Preparation/Tools/Diagnose/ModelProbe.cpp`, `README.md`,
`docs/Engine-Manual.md`.

**Commits.** `91714c1`, `c5bfab8`, `5d0fd65`, `72988c3`, `cf2d049`, `918a28d`,
`6520654`, `de4a3cc`, `c0cd3ab`, `e8f779a`, `5bbfb40`, `0947713`, `bf105b4`, `79d9eee`

### First person: Lara's head and neck cut away, and the lid that closes the opening
2026-09-06 - 2026-09-08

**What.** In first person the HD path removes Lara's head and neck and closes the
opening with a lid of her own skin. The cut is a BONE WEIGHT and not a shape in space:
`ModelVS` sums the vertex's share of the head node out of the three bones the skinning
already walks and `ModelPS` clips per pixel on the interpolated value, so every fragment
the head node owns at least `HD_LARA_HEAD_CUT_WEIGHT` (0.15) of is discarded. Collars
stay: the Russia jacket, the parka and the VCI catsuit's collar ring all lie below the
cut and are drawn. The lid is `HD_LARA_PART_NECK_CAP`, a two-ring triangle fan built
over the RIM of the hole - the vertices that miss the threshold but share a triangle
with one that reaches it. Every rim vertex is copied whole (position, three bones, three
weights, texture coordinate), and every spoke of the fan wears the texel of the rim
vertex it hangs from all the way in, so the fan's middle is one copy per spoke rather
than one shared vertex; it wears the SKIN material, its normal points head-ward, and it
is drawn with no cut of its own, no cull and its own node mask, because first person
clears the head node from the body's mask and the lid rides on that node. Lara's body is
drawn once: the HD path has one depth rule, `D3DCMP_LESSEQUAL`.

**Why.** The near cut of the first person view takes the neck stump off a body that is a
hollow shell and culls back faces, so one looks clean through her. A neck vertex is
skinned to two nodes at once - the one under the chin is 55 per cent torso and 45 per
cent head - so a shape defined in the bind pose falls on different skin in every
animation: a sphere about the eye takes a bite out of the chest, a cylinder with a node
mask leaves the collar standing and takes half the neck with the mask, and a plane
carried by the head node tilts with the head and runs through the chest at forty degrees
of pitch. The head node's share of a vertex is the one quantity no animation moves. The
threshold is measured over the eight wardrobe bodies (`ModelProbe -headw`): any weight
at all takes 9,622 vertices on the four Tomb Raider III bodies, which carry a whisper of
head weight down to the feet, against the 6,729 that are really the head; 0.50 leaves
the highest surviving vertex inside a neck's radius 1.6 units above the head joint, a
visible ring of neck around the camera; between 0.05 and 0.35 all eight bodies read the
same 24.5 units for the widest thing taken below the chin, which is the neck's own
radius and no collar. The lid's edge is skinned by the same sum over the same joints as
the skin it meets, so the two land on the same point in every pose and no animation can
open a gap. The per-spoke texel prevents a dark point with a cone running into it: on
the game 5 pages the neck is one column six texels wide, so any texel inside it is skin,
but on page 8201 it is two separate bands (v 154..158 and v 198..202) and a single
averaged middle texel makes every triangle sweep the gap between them. The texel itself
is taken from the page whose vertex column runs the whole height of the neck - a throat
runs from the head joint to the shoulders, while a collar, a zip and a hairline are
bands across it - and at the CENTROID of the biggest triangle on that page, which is
inside the paint by construction where a real vertex sits on the edge of its texture
island. The second, inward draw of the whole body that earlier closed the opening was
removed because with the lid in place it changes nothing - forty first person pictures
over five outfits, three pitches, both eyes, head and torso turned and tilted, identical
to the pixel - while costing a second pass over 27,000 triangles every first person
frame and one real fault: a strap, a shoulder piece or a flap is one layer of triangles
whose two sides sit at the same depth to the bit, the inward draw won that tie, and the
piece came out in one flat texel with its normal turned round, per pixel where float
precision differed.

**How.** `HD_LARA_HEAD_CUT_WEIGHT` 0.15 in `specific/hd/hd_lara.h`; `HDLaraHeadCut` and
`build_neck_cap` (over `HDModelSlot::synth`) in `hd_lara.cpp`; `hd_laradraw.cpp` passes
node and threshold to `HDModelNeckCut` with nothing built per frame; `hd_model.cpp`
writes them to shader constant c162; the cut is in `hd_shader/hd_model.hlsl` (`ModelVS`
adds the head node's weight, `ModelPS` clips) with the generated `hd_model_vs.inc` and
`hd_model_ps.inc`. The rim measures 20 to 32 vertices per outfit, 60 to 96 fan triangles
over two rings. `measure_neck_skin` stays because the lid wears the throat texel of the
body it belongs to. Gone with the earlier shapes: `HD_LARA_NECK_PLANE_S`,
`HDLaraNeckPlane` and its four view-space numbers, `gNeckAxis` (c163), `gNeckNear`
(c165) and the node mask in `gNodeLight[].a`; gone with the inner skin:
`HD_MODEL_SKIN_CW` / `_CCW` / `_INSIDE`, `HDModelSetInnerSkin`, `HDModelInnerSkin`,
`Model::innerPage*` and `gInnerSkin` (c164). The cut follows the game's own choice of
eye - `FP_EYE_OFFSET` 48 units forward, or the eye on the joint when `mgLOS` forbids the
offset - without being told which happened, because in first person the camera is the
eye; in third person the cut is off and the third person picture is byte-identical
across the change for all five outfits measured. The renderer's near plane is at 20
(`InitWindow`, `specific/gamemain.cpp`). The diving suit is outside this work: it has no
throat and its helmet sits on the torso node. Everything here is inside `hd_enable`.
Measured and pictured with `Preparation/Tools/Diagnose/ModelProbe.cpp` (`-headw`,
`-neck`, `-firstperson`, `-noinner`, `-neckcut`, `-cutnodes <mask>`).

**Commits.** `6520654`, `de4a3cc`, `c0cd3ab`, `9d855f0`, `45d9130`, `a1dbf32`, `db696bf`

### Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn
2026-09-05 - 2026-09-16

**What.** The Remaster answers Lara not with one model but with a wardrobe of separate
ones - the body outfit, the head, the braid, the hand pairs, the thigh and torso
holsters and the weapon on the back - and the engine loads the parts a level can
actually use (ANDREA1 11 models and 20 MiB, JOBY5 16 and 27 MiB, RICH1 6 and 17 MiB,
JOBY4 two and 12 MiB, 34 to 79 ms to build; the full IV-VI wardrobe is 21 models, 3.5
MiB of buffers and 30 MiB of pages). The list of outfits is a SCAN and not a table: one
engine-free unit walks three places in a fixed order, which is the priority - the IV-VI
install's `4\ITEM` and `5\ITEM`, then the I-III install's `1\ITEM`, then every active
mod's `5\ITEM` and `4\ITEM`. An entry's key is the source, a slash and the file, with a
mod's folder name between (`46/OUTFIT_TR2_HOME`, `13/OUTFIT_TRX_WETSUIT_1`,
`mod/outfits/aod wetsuit/OUTFIT_CLASSIC`), forward slashes throughout, compared
case-insensitively. Measured over two installed bundles: 52 entries, 22 from IV-VI and
30 from I-III, with 50 files passed over and every refusal named in the log. An entry's
properties are resolved in tiers, each tier per key: a list block, a mod's `outfit.txt`,
a table of eight measured rows, a measurement off the file, the defaults. Which outfit
is worn is decided from the level's own data unless a `REG_SZ` named `hd_outfit` under
the settings key names a scanned entry, which wins; `HDLaraOutfitFor` answers the diving
suit and the catsuit from `LaraDrawType`, the Russian cap from the engine's own
level-range test, and the four Tomb Raider III guest outfits (#122) from the header
marker `data[3]`. Young Lara stays classic whatever any of it says. Worn parts are
planned by the OUTFIT and not by the slots the level loaded. An outfit that holsters but
has no holster rig of its own is given one - `HD_LARA_PART_HOLSTER_RIG`,
`OUTFIT_TR3_CATSUIT` cut to texture page 8205 - and `Outfit::wearsRig` says who wears
it: `OUTFIT_VCI` and no other body. Every wardrobe part carries a named material, a
specular strength and exponent, per texture page, and the outfit table names which of
the body's own pages are Lara's skin; `skin_sheen` and `skin_exponent` set the material
of her skin separately from her clothes and move all thirteen skin draws at once. An
outfit also decides whose head a body wears, in three modes: `outfit` (the body's own
head node and `LARA_HEAD`), `tr5` (the body's head node left out, `LARA_HEAD` with Tomb
Raider V's own head node in its place) and `hair` (all three, a hood or hat kept with
TR5's hair over it), the default being `tr5` for every I-III entry and for every mod
whose `mod.txt` says `origin=tr1|tr2|tr3` and `outfit` for everything else.

**Why.** Every other creature is one slot and one `.TRM`; Lara is drawn out of five
slots in one pass and which parts are drawn changes from frame to frame, so she needs a
module of her own. The whole IV-VI wardrobe lives in `4\ITEM` rather than `5\ITEM`, the
shared-folder rule TROne's design already follows. Only loaded slots are planned,
because a weapon whose slot is missing cannot be drawn by the classic path either - that
path reads `meshes[objects[slot].mesh_index]`. The outfit test uses the engine's own
expression rather than a second copy, because a copy can drift. The `LARA_PONYTAIL` byte
means two different things - the Rome body inside a Russia level, and every Tomb Raider
III guest - and nothing else in the level file separates them, which is why the marker
byte exists and why only values 5..8 are accepted from it: 1..4 are the vanilla bodies
and letting one through would let a stale byte overrule the level's own `LaraType`. The
young-Lara test is the script flag `GF_YOUNGLARA` or the `LARA_YOUNG` outfit byte and
not the Irish level range, because the randomizer clears that flag out of `SCRIPT.DAT`
(young Lara can carry no weapon) and replaces her skin with an adult's. A table of eight
hand-measured rows could hold only outfits somebody had measured, so a mod's
`OUTFIT_*.TRM` had to be refused outright; a scanned list carries one while the
measurements stay where they belong. The choice is a registry value and not part of a
run because a seed and its settings travel from one player to another while an
installation and a `Mods` folder do not: a run that named a scanned outfit could not be
played on another machine. Planning a worn part only where the level had loaded the
matching `LARA_HOLSTERS*` object is right for a HAND part and wrong for anything WORN,
because those variables hold slot numbers written from the INVENTORY - `setup.cpp` puts
`LARA_HOLSTERS_PISTOLS` on both thighs at every level start whatever the level loaded,
and the three VCI levels load no holster slot at all, because Core drew no thigh
holsters on the catsuit, while the randomizer hands that Lara pistols anyway.
`HOLSTER_PISTOL.TRM` is the two guns and nothing else - 4268 vertices, two pistols
floating in space - while pouch, thigh strap and hip belt are built into the outfit
model, and three bodies have none (`OUTFIT_VCI`, `OUTFIT_TR3_ANTARC`,
`OUTFIT_TR3_NEVADA`); a page-derived rig rule reached all three and two of them are
wrong, because the Antarctica parka and the Nevada outfit never wore a thigh rig in Tomb
Raider III and their coats are modelled to sit flush, so straps stood off the hip at the
width the London catsuit's thin thighs want. The London catsuit is the donor rather than
the Rome one because its rig is black leather over a black leg, which is what the VCI
catsuit is, where `OUTFIT_CLASSIC`'s is brown suede over bare skin. TR5 has no specular
term of any kind - `aTransformLightClipMesh` adds an ambient and a diffuse cosine and
stops - so latex, rubber and skin all read matt, and there is nothing per texel to read
instead: over the wardrobe's pages 8001, 8008, 8016, 8018, 8027, 8028 and 8205, 100 per
cent of the texels fall in the 240..255 alpha bucket, a flat opaque channel with BC7
quantisation noise, the one exception being page 8002, Lara's hair, at 15.2 per cent
below 128, which is coverage and not gloss. One sheen for the whole draw of a weapon
hand made her skin metal, because a `HAND_` model is a hand and what it holds on two
pages of one file; and where a room carries no dynamic lights the shader's highlight is
`light * strength * pow(N.V, power) / 255`, so a difference in material between forearm
and hand is multiplied by the room's ambient and the seam at the wrist brightens and
darkens from room to room - on the Rome body forearm and hand are the SAME page, 8001,
and that difference is 5.1 grey levels at N.V = 1 in a room of 255, and 84.1 on the Tomb
Raider III catsuit. Geometry cannot tell skin from latex, so which pages are skin is a
table; three outfits keep a difference on purpose, a latex sleeve, a parka mitten and a
coat sleeve meeting a bare hand being material boundaries in the art. The measured skin
rule is given only one page per new entry because it cannot be right about more: the
four Tomb Raider III guests carry a second skin page, 8202, their bare arms, and a
measurement at the neck cannot reach an arm - the same page is bare skin under the
Antarctica parka and a latex sleeve under the London catsuit - and the diving suit's
throat page, 8047, is its helmet, so a measurement alone would make the whole suit matt.
The head mode exists because a IV-VI body carries Lara's whole hair on head node 14 - of
`OUTFIT_CLASSIC`'s 9,485 triangles there, 9,483 wear page 8002 and two are nape - while
the separate `LARA_HEAD.TRM` riding rigidly on the same node is only the face and the
front of the skull, open at the back: the two are halves of one head. All thirty
`OUTFIT_TRX_*` bodies of the I-III bundle carry a hood, hat, helmet or headband on that
node and nothing else (diving hood and goggles 1,987 vertices, ice hood 461, Greek
headband 1,127, six of the thirty near enough nothing) and not one wears page 8002, so
drawn as it stands such a body shows a bald skull with the open back of `LARA_HEAD` in
it. Dropping the head node opens no hole: with its triangles hidden every measured body
still reaches up to y -240..-250 (the collar, which belongs to the torso node) while
`OUTFIT_CLASSIC`'s head node reaches down to y -238.7, an overlap of 1 to 11 units.
There is deliberately no rule by name and none by geometry: `OUTFIT_TR2_HOME` is both
the name of a IV-VI body with a full head and the name a mod gives an I-III conversion,
and a vertex count cannot separate a 10-vertex headband and a 932-vertex converted body
from a 461-vertex hood. The outfits of the two installations have no folder at all - on
a typical machine 52 of 103 scanned entries - and a mod that ships seventeen bodies
under one `outfit.txt` cannot say anything about one of them, which is why a list file
can name any outfit. The plain twin of an `_HD` file is worn because this engine's
vertex holds `u` and `v` as bytes and 14,208 of `OUTFIT_TRX_WETSUIT_1_HD`'s 14,262
coordinates lie off that 1/255 grid, and off every grid from 1/256 to 1/4096, differing
from the twin's by as much as half a step in both directions, spread and not by a
constant offset; a wide file with no twin is worn all the same, because a rounded
picture beats no picture.

**How.** `specific/hd/hd_lara.cpp`/`.h` is engine-free so the diagnostic tools link the
same reader; `specific/hd/hd_wardrobe.cpp`/`.h` hold the scan, the key format, the
property tiers and the override table and are compiled into the engine, `ModelProbe`,
`Randomize.exe` and the GUI alike, because two programs working the same list out
separately will one day disagree. The composite's skeleton is Lara's classic mesh tree:
every `OUTFIT_` model skins to nodes 0..14 and names no sixteenth, so the fifteen
matrices in `lara_matrices` are the joint palette, `OUTFIT_SUBSUIT` covering thirteen,
the diving suit ending in flippers that belong to the shins. `LARA_HEAD.TRM` covers 31
nodes because it carries a facial rig whose bone numbers are not mesh-tree nodes, so it
rides rigidly on node 14 through `HDModelSlot::rigidNode` (default -1, so every existing
caller is unchanged), passed on by `HDModelLevelBegin` - all of it but its throat, which
hangs on node 7 (*The face's throat on her torso*, below). `HAIR_OG.TRM` has six bones
matching the classic `HAIR` mesh tree's straight chain at z = 0, 56, 112, 168, 224, 280
in every adult level. A `HAND_` model covers nodes 9 and 10 on the right and 12 and 13
on the left, `HAND_BARE_REST` adds the upper-arm stubs 8 and 11, and three are
one-sided: `HAND_BARE_FLARE` and `HAND_BARE_TORCH` cover 12 and 13 only,
`HAND_BARE_CROWBAR` 9 and 10 only. `HOLSTER_PISTOL` and `HOLSTER_UZI` cover nodes 1 and
4, `HOLSTER_REVOLVER` node 4, `HOLSTER_SHOTGUN`/`_HK`/`_GRAPPLE` node 7, where
`DrawLara` puts the classic ones; an empty holster draws nothing extra, the straps being
painted into every `OUTFIT_` model, which is why the Remaster ships no empty-holster
file. The Russian-cap test is the engine's own expression `(gfLevelId >= LVL5_BASE &&
gfLevelId <= LVL5_SINKING_SUBMARINE && LaraDrawType == LARA_NORMAL) || LaraDrawType ==
LARA_BUNHEAD`, which already stands three times in `game/hair.cpp` and once in
`game/laraskin.cpp`; read against all fifteen vanilla level files the rules answer
correctly fifteen times out of fifteen. Which model plays which role is TROne's wardrobe
(GPLv3, TROne by Todesengel1931 - https://github.com/todesengel1931), with its TR5
glue's ANDY/JOBY assignment deliberately not followed: in these level files, measured,
`JOBY` is the Russian submarine section and `ANDY` is Ireland. The marker:
`specific/file.cpp` reads the fourth word of the 32-byte header block behind the texture
pages into `GfHdOutfit`, beside `LaraType` at `data[0]`, the weather at `data[1]` and
the foreign-speech-head flag at `data[2]`; `hd_laradraw.cpp` hands it to
`HDLaraOutfitFor` unchanged, because the randomizer writes the engine's own
`HD_LARA_OUTFIT_*` numbers, and a `static_assert` in `hd_level.cpp` holds the enum to
5..8. Mapping: `tr3_london` -> `OUTFIT_TR3_CATSUIT`, `tr3_jungle` -> `OUTFIT_TR3_COAST`,
`tr3_antarctica` -> `OUTFIT_TR3_ANTARC`, `tr3_nevada` -> `OUTFIT_TR3_NEVADA`; a level
carrying `LARA_PONYTAIL` and `data[3] = 0` draws `OUTFIT_CLASSIC`, and not one of the
seventeen shipped files carries that byte. The young-Lara test follows the two places
the engine builds the child body, `LaraInitialiseMeshes` in `game/laramisc.cpp` and its
twin in `game/deltapak.cpp`; the flag is cleared by the randomizer in
`core/ScriptDat.cpp`. The scan passes over the install's two `OUTFIT_YOUNG*` bodies,
`OUTFIT_HAIR` and the three `OUTFIT_HANDS_*` (parts of the composite, not bodies), the
fourteen I-III copies of names the IV-VI bundle already carries self-contained
(`OUTFIT_TR1_CLASSIC`: 14,515 vertices in IV-VI against 8,382 in I-III, with IV-VI page
numbers) and the thirty `_HD` twins; the young filter does not apply to a mod, because
mod outfits are commonly grown-up bodies shipped in the young slot. `OUTFIT_HAIR.TRM` is
not a hair cap and is loaded by nothing: it is a braid, 3,542 vertices over five nodes
whose boxes run 0..53, 47..107, 93..161, 93..208 and 205..242 along z, the I-III
bundle's own `HAIR_OG` in another spelling. `hd_trm.cpp`/`.h` read the thirty
`OUTFIT_TRX_*_HD.TRM` files whose vertex stride is 30 and not 24, the six extra bytes
being two texture coordinates that leave the gaps in the bone and weight block and stand
at the end as floats; the layout is arithmetic and then byte comparison - the aligned
index block plus vertices times 30 ends at the last byte in all thirty files and the
leading vertices are byte-identical with the twin's in position, normal, layer, bone and
weight - and over all 2,923 `.TRM` files of both remasters exactly thirty are laid out
that way. `readable()` reads whole files to look at headers (28.8 MiB, `read_kib=29525`,
once per session) because the prefix a header needs is 140 KiB of a 480 KiB outfit file.
`specific/registry.cpp`/`.h` gained `REG_PeekString`, the string form of `REG_PeekLong`,
read-only so an absent value cannot take `init_tomb5_stuff`'s first-run detection away;
`hd_laradraw.cpp` reads `hd_outfit` once per level build, because the in-game changer
writes it while the game runs. A list file is a `.txt` lying directly in `Mods` or
directly in a category folder, read as blocks (`Outfit: TRX_Wetsuit_3` then keys),
taking the same thirteen keys a mod's `outfit.txt` takes - `name`, `hands`, `gloves`,
`braid`, `holsters`, `rig`, `sheen`, `exponent`, `skin_sheen`, `skin_exponent`, `skin`,
`footsteps`, `head` - and setting only the keys it has; the files are found by
`hd_mods.cpp`'s own walk (`HDModsListCount` / `HDModsListPath`), which never steps into
a mod folder, a leading `_` switches a list off, and they are read in lower-cased
full-path order. `#` is a comment and closes nothing, a block opens at `Outfit:` and
closes at the next one or an empty line, and a UTF-8 byte order mark is taken off the
first line. A name is matched against the outfit's file stem without regard to case, to
the `OUTFIT_` prefix or to `_` against a space, so `TR2_Swim`, `tr2 swim` and
`OUTFIT_TR2_SWIM` are one name; a key prefix (`13/TRX_Wetsuit_3`, `46/Classic`) pins one
source. One reader serves both kinds of file - `take_key` and `apply_txt` with a `where`
string and a tier number - because the rank is only real if every tier writes the same
fields in the same order. `take_number` takes 0..2 for `sheen` and `skin_sheen` and
1..128 for the exponents, `take_flag` refuses an unknown word and the note spells the
vocabulary out (`1, yes, true, on / 0, no, false, off`), `skin=` with nothing after it
is a note instead of a silent wipe of the tier below, each page number goes through
`strtol`, `name=` is 64 bytes with a longer one cut to 63 and noted, and `read_line`
reads a line however long it is. The material is named per texture page in one table in
`hd_lara.cpp` - latex 0.45/26 for the two catsuits, rubber 0.30/22 for the diving suit,
cloth 0.10/14 for the Rome top, skin 0.12/10, hair 0.14/18, leather 0.18/20 for the
holster rig, gunmetal 0.35/34 for a weapon - through `HDModelSetPageSheen`, which is
already the unit a draw call is cut to, so it costs one constant write on the ranges
that have one; `skin_sheen()` out of the wardrobe entry is the one answer for all
thirteen skin draws, the eleven hand parts (`kPart[].kind == 1`: REST, PISTOL, UZI,
SHOTGUN, HK, DEAGLE, DEAGLE_LASER, GRAPPLE, FLARE, TORCH, CROWBAR), the body's own skin
pages, the face and the neck lid. The term is Blinn-Phong in the model VERTEX shader
beside the diffuse it belongs to, out of the object's own light list with the same
direction and distance falloff; when the list is empty the eye is the lamp, weighted by
the light the vertex already has so that a catsuit in an unlit corner stays dark, the
highlight is added AFTER the texel and never multiplied through it, and never in the
additive pass. One new vertex register, `c161` (`gSpec`: strength, exponent), is written
for every draw so that Lara's gloss cannot be left on the next creature;
`HDModelLighting` carries the two numbers and every object passes 0, so the creature
path is unchanged to the bit. `HDModelSlot::onlyPage` builds only the triangles wearing
one `.DDS` page and loads only that page; the cut is by page because a page is the
artist's own division into materials and the measurement says it is exact here - 8017
appears on nodes 0, 1, 4 and 79 triangles of node 7 in the Russia coat and nowhere else,
8205 on nodes 0, 1 and 4 alone. The head mode is `HDWardrobeEntry::head` with `fromHead`
saying who decided, settled once at scan; `hd_lara.cpp` turns it into two answers at
level build and the draw asks nothing else per frame - `HDLaraBodyMask()` is `~0u` or
every node but 14, and `HD_LARA_PART_HEAD` is node 14 of the installation's own
`OUTFIT_CLASSIC`. No file in either remaster holds that node alone, so `hd_model.cpp`
learned `HDModelSlot::onlyNode`, the page cut's twin: only triangles whose dominant node
is in the mask are built and only the pages those triangles wear are loaded, which needs
a walk of the index list at page-gathering time, paid for only by a slot that asks. The
cut is by node and not by page although for this donor the two nearly agree, so that the
part's coverage is exactly node 14 and the first person mask takes the borrowed head
with the body's own. `hd_laradraw.cpp` draws the part after the body and before the
face, with the frame's mask, the body's cut and the body's cull, because the hair is the
wardrobe's only see-through pass; a body told to drop its own head with no donor to be
had keeps its own head and the log says so. `props_of` in `hd_lara.cpp` memoises one
answer keyed by the outfit, dropped by `HDLaraLevelEnd()`, because `HDLaraPartSheen`
calls it once per part per draw; `page_number_of` strips the mod namespace and the
alternate-bundle base for every page comparison, the held-item rule using the same
helper. `hd_model.cpp`'s `build()` works a model's page answers out once per texture
layer into a small table, exact because `HDTrmPageOf` is `texture[layer-1]`;
`HDPageCacheKeyOf` remembers its answer per key, dropped in `HDPageCacheAltRoot`, the
one call that can change an answer - before that the mod branch cost 30.8 microseconds a
call and was asked once per corner of every triangle - and folds a mod-namespace key to
the plain page number when neither the mod nor its origin bundle ships that `.DDS`, safe
by construction because `resolve_page` then runs the ordinary chain; measured on one
level's wardrobe with nine mods, 25 cache slots and 33.3 MiB before, 24 and 32.0 MiB
after, rendered picture byte-identical. Cost of the borrowed head, ANDREA1 with the
diving suit: +27,349 vertices (641 KiB), +28,455 indices (55.6 KiB), +3 draw calls and
one new texture page; a body in mode `outfit` pays none of it. `hd_markers.cpp`/`.h`
carry no `wear` line, and `hd_mods.cpp` goes on refusing a mod's `OUTFIT_*.TRM` as a
replacement, because a mod's outfit is an entry in the scan and never a replacement. A
settings file written by an intermediate build still carrying `outfitsOn=` or
`wardrobeFollowsGameFolder=` loads with every other key intact and the next save drops
them, because `Config::load` is an `if`/`else if` chain with no final `else`. Log seams:
the level's models line carries the phases of `build_ms` (`find_ms`, `key_ms`,
`page_ms`, `buf_ms`) and `trm_open`, `trm_load`, `trm_kib`; the wardrobe scan line
carries `ms`, `probed`, `read_kib`, `geometry` (0, because it reads headers and never a
body), `lists=`, `blocks=`, `matched=`, `unmatched=` and `heads tr5=.. hair=..
outfit=..`; each entry logs `head=<mode> (from ..) donor=OUTFIT_CLASSIC#14` and its skin
pair, the Lara line carries `laratype=`, `data3=`, `skinsheen=` and `head=<mode>/<1 if
TR5's head is on the card>` with `body=` beside `mask=`, a `MISMATCH` line is written
when `data[3]` and the sidecar's `outfit` disagree (the header byte wins), `HDLOG
wardrobe list <file>: Outfit <name> -> <key>: <the keys it set>` names every list block
with the file's own `N blocks, N matched, N unmatched`, and each list file also gets a
`MODLOG list<n>` line, which the mods scan writes even with the HD path off. Tools:
`ModelProbe -wardrobe` prints the scan, every refusal, a `head` column, a `sheen` column
with its tier and a `skin glow` column; `-lara`, `-outfit`, `-guns`, `-holsters`,
`-flare`, `-torch`, `-lara-nodes`, `-lara-pose`, `-mirror`, `-onlypage <n>`, `-nosheen`,
`-lara-head tr5|hair|outfit`, `-pagelist` build the composite against a level's own Lara
mesh tree without the game; `DdsProbe -alpha` writes an alpha channel as a grey picture
and prints a histogram; `bmodel.bat` builds with the new units. `HDTrmCounters`' note
records that `loads` counts calls before the file is opened and that the reader is
reached from two threads - the load thread's `HDTrmExists` in the level scan and the
main thread's `HDTrmProbe` in the wardrobe scan - without a race, because
`specific/file.cpp` joins the load thread before `HDLevelBuild` runs.
`HDWardrobeFromWord` is the phrase form of a tier and `HDWardrobeFromTag` the one-word
form. Files: `specific/hd/hd_wardrobe.cpp`/`.h`, `hd_lara.cpp`/`.h`,
`hd_laradraw.cpp`/`.h`, `hd_model.cpp`/`.h`, `hd_trm.cpp`/`.h`, `hd_level.cpp`,
`hd_markers.cpp`/`.h`, `hd_mods.cpp`/`.h`, `hd_pagecache.cpp`/`.h`, `hd_object.cpp`,
`hd_render.cpp`, `hd_outfitmenu.cpp`, `hd_shader/hd_model.hlsl` and `hd_model_vs.inc`,
`specific/file.cpp`, `specific/registry.cpp`/`.h`, `specific/winmain.cpp`,
`specific/3dmath.h`, `specific/drawroom.h`, `game/lara.cpp`/`.h`, `game/laramisc.cpp`,
`game/deltapak.cpp`, `game/hair.cpp`, `game/laraskin.cpp`, `tomb5.vcxproj`,
`TR5Randomizer/src/core/ScriptDat.cpp`, `Preparation/Tools/Diagnose/ModelProbe.cpp`,
`DdsProbe.cpp`, `bmodel.bat`, `README.md`, `TOMB5 Code/README.md`,
`Preparation/Tools/README.md`, `docs/Engine-Manual.md`, `docs/Randomizer-Manual.md`.

**The face's throat on her torso** (2026-09-25). *What.* In HD, Lara's head did not move
with her body under the chin: in the handstand, in a crouch and lying on her back after a
fall the owner saw a flap and a crease at the throat and a long straight neck. The turns
are the animations' own (the handstand 159 tilts the head 65 to 80 degrees back against
the torso at keys 7 to 40, the crouch idle 222 60 degrees, the death 25 more than 100),
and every head piece is placed with the frame's own matrices (`kLmxOfNode`, now
`lara_lmx_of_node`, maps node 14 to matrix 8 for the body, the donor head, the lid, the
face and the hands alike). The fault was `LARA_HEAD.TRM` riding wholly rigid on node 14.
It reaches 27.8 units below the head joint - 541 of its 5,913 vertices lie below it, 102
more than 10 below - and that strip of throat, which runs down into the body's own neck,
swung with the skull and folded out under the jaw. Now the throat hangs on the torso, node
7, with the file's own weights, and the rest of the face stays on node 14. The face's part
line reads `cover=2(00004080)` (ModelProbe's part table: nodes 7 and 14, where it read 14
alone), and the model note `LARA_HEAD (slot 23042): 171 of 5913 vertices of the face take
the body's neck (node 7) with the file's own weights - rig joints 1,3,6 stand 199.3..270.8
from its head joint 4, the limit is 120`.

*Why this rule.* The file says itself where its throat belongs. Its facial rig carries a
skeleton: of its 33 joints (each stored as an inverse bind matrix, 3x4 and row-major, the
joint at -R^T t), joint 4 stands on the bind of node 14, joint 1 at (-1, -49, 11) on the
bind of node 7, the torso, joint 6 at the hips and joint 3 at the left thigh, and 171
vertices carry weight on joints 1, 3 and 6 - 0.8 per cent of the file's weight, 12,690
units of it on joint 1. A joint farther than 120 units from the rig's head joint (the one
carrying the most weight) is a joint of the body: over the 23 rigged `*_HEAD.TRM` of the
IV-VI install's games 4 and 5 the farthest joint of a face stands 99.4 units out
(ZIP_HEAD) and the nearest joint of a body 131.8 (LARSON_HEAD), the gap TROne measured on
data over 58 files of both installs (GPLv3, TROne by Todesengel1931 -
https://github.com/todesengel1931), and its rule shaped this design. LARA_HEAD's two
shoulder joints stand 81 units out and count as the face's; they carry 27 units of weight
over 26 vertices. The file's weights match the body's own: the 66 throat vertices that lie
within a unit of `OUTFIT_CLASSIC`'s neck in the bind pose carry a head weight 0.022 from
the neck vertex under them on average (0.058 on `OUTFIT_VCI`), and with the head pitched
70 degrees about its joint the two skins stay 0.8 units apart on average and 4.0 at most
(1.5 and 9.5 on `OUTFIT_VCI`), where the rigid face parted from the neck by 15.1 and 34.6.

*The design measured against it.* Every face vertex below the head joint taking the head
weight of the nearest outfit vertex in the neck column (within 60 units of the neck axis,
found in 2-unit height bins), the rest of its weight on node 7. It was built into the
engine as a measurement and drawn by ModelProbe at head pitches of 60 and 70 degrees and
in the crouch 222's head turn (-18, 61), front and side, in `OUTFIT_CLASSIC` and
`OUTFIT_VCI`. It reaches the jaw and the chin as well - 387 of those 541 vertices, on
average 25 units from the nearest neck vertex - and hands them 0.46 to 0.61 of the head on
average (`OUTFIT_VCI` to `OUTFIT_RUSSIA`): in all twelve pictures the jaw tears open into
spikes. The two designs agree only where the two skins coincide (23 vertices 16 to 28
units below the joint, 0.3 units from the neck), to 0.05. The file's own rig leaves no
flap, no step and no hole in the same twelve pictures; the rigid face leaves the flap
under the jaw and, from the front, a hole under the chin.

*How.* `specific/hd/hd_trm.cpp` keeps the rig's joints while it reads a head
(`HDTrmModel::rigJoints`/`rigJoint`, `hd_trm.h:146`; the list is taken only when it lies
whole between the joint count and the geometry the rescan found, `hd_trm.cpp:403`, and is
freed by `HDTrmFree`). `HDModelSlot::neckNode` (`hd_model.h:104`, -1 by default) asks for
the rule on a rigid face; `rig_neck_of` (`hd_model.cpp:1800`, limit `kNeckJointDistance`
at `:1772`) renames every body joint to the lowest of them in the copy read into memory
and hands that bone to `build()`'s existing rigid branch as the neck bone, so the file's
weights are kept and `dominant_node_of` puts a triangle on node 7 where the throat
outweighs the face. It runs in pass one while the file is open (`:2693`), and pass two
hands the bone to the model (`:3165`). `HDLaraPlan` asks for node 7 on the face
(`hd_lara.cpp:1899`); the reasoning and the numbers stand at `HD_LARA_PART_FACE`
(`hd_lara.h:713`). A file with no rig, or none beyond the limit, stays rigid, and the note
says why. Cost: once per level build, 5,913 vertices walked twice. Creature faces keep
`neck_bone_of`: the distance rule picks the same single joint for `BLUE_GUARD`, `PIERRE`,
`ADMIRAL` and the two `VON_CROY` faces, but adds joints 10 and 13 to `LARSON`'s joint 1
and finds a body joint where `neck_bone_of` finds none in the two `PATRICK`, `DEMON` and
`SERGIE` faces, so it is not carried over to them. The same file's rig moves her lips in
the cutscenes she acts in: see *The cutscene faces speak* above.

*Unchanged.* With `hd_enable` off no level is built at all (`HDEnabled`,
`hd_level.cpp:2538`), and with `hd_objects` off `HDLaraDrawPlan` returns before the
wardrobe is planned (`hd_laradraw.cpp:500`). First person does not draw the face at all
(`hd_laradraw.cpp`, the face's push is guarded by `!firstPerson`), so the neck cut and the
lid are untouched: the eight first person pictures (two outfits, pitches 60 and 80, the
eye at 48 and 0 units) are byte-identical before and after. A measurement build with the
rule switched off drew all 24 pictures byte-identical to the build of `e1bc056`, so the
rule is the only difference; in the bind pose the rule changes 3 pixels by one grey level,
the blend's rounding.

**Commits.** `91714c1`, `c5bfab8`, `7715dda`, `5d0fd65`, `72988c3`, `918a28d`,
`6520654`, `de4a3cc`, `c0cd3ab`, `2aff299`, `234df1b`, `1ef8f9f`, `20f488c`, `ce1bf61`,
`b5983cf`, `c543269`, `0c84e7e`, `604d36c`, `b6c60f7`, `aaf229c`, `b1187c7`, `e203ae2`,
`3079d85`, `4ea9d89`, `c179e0a`, `653369e`, `6c459a2`, `c4c8faa`, `ac39393`, `dc2bf9c`,
`fc30b92`, `44d8e11`, `db83663`, `7fec5a7`, `b7af512`, `1950e3e`, `9948ca5`, `3a37411`

### F4 changes Lara's outfit inside the level
2026-09-13 - 2026-09-14

**What.** F4 opens a wardrobe window in the middle of a level and Lara wears the choice
at once, without leaving the level. The window is the pause menu's twin by construction:
`CreateMonoScreen` freezes the picture, `S_UpdateInput` with `SetDebounce` drives it,
`dbinput`'s six bits are the only inputs it reads, `SFX_MENU_CHOOSE` and
`SFX_MENU_SELECT` are its sounds, `PrintString` and `font_height` draw it in the two
columns the options page uses, and a literal with no string id goes through `CustomText`
exactly as the `Language` row does; a gamepad works in it because the bits it reads are
the ones the pad already drives. `InventoryActive` is held for the whole of it, so the
press that closes the window cannot also roll Lara. It does not open in the title, with
Lara dead, during a cutscene, or while the binoculars or the laser sight are up, and the
inventory and the pause menu cannot be open under it, because both run their own frame
loop and the call site is not in it. Alt+Enter inside it toggles the display and chooses
nothing; closing the window counts as cancel. The list is the wardrobe scan in its own
order, a window of nine rows with the selection kept in view, each row naming its source
(`IV-VI`, `I-III` or the mod's folder name) and a `*` on the row in force; the first row
is **Level facts**, which is not an outfit but what clears the setting again. Under the
list stand value rows: the highlighted outfit's **Footsteps**, its **Head** mode with
who decided it, its **Shine** and its **Skin glow**, each turned over with left and
right and naming the tier that decided it; on the **Level facts** row the footsteps read
*as the outfit says* and do not answer left or right. The outfit is written to the
`REG_SZ` `hd_outfit` the level build reads; the footsteps are one `REG_DWORD` per outfit
under `tomb5\hd_footsteps`, the wardrobe key as the value's name, written for the row it
was made on when that row is left. A level may spend `HD_REDRESS_ALLOWANCE` on changes;
past it the window says *applies at the next level*, the log says how much has been
spent, and the new outfit's footsteps wait with the body. Both halves are written to the
registry either way, so the next level wears both.

**Why.** The registry value the level build reads had nothing that wrote it.
`OpenRegistry` creates the key it is handed, which is safe here and nowhere earlier,
because `init_tomb5_stuff` asks `REG_KeyWasCreated()` once at startup and has long since
run by the time a level is on the screen. The footsteps are a value per outfit rather
than a subkey per outfit because a wardrobe key holds slashes and spaces, and a subkey
name may hold no backslash while a value name may hold almost anything. The re-dress is
safe on the main thread because there is no other one: the load thread that builds a
level is joined before `HDLevelBuild` ever runs, and the caller has stopped the world.
The cap is bytes and not a count because the cost of a change is not uniform: measured
over one level with a 56-entry wardrobe, one outfit's own pages run from 9 to 28 pages
and 12.00 to 37.30 MiB, a single change costs up to 15 pages and 20.00 MiB nothing else
holds, the dearest is +10 pages and +13.33 MiB, the mean over all 2,970 ordered pairs is
+2.94 MiB, and wearing all 55 in one level is 106.64 MiB over 80 distinct pages - about
twenty changes at the mean and four at the worst. Freeing the old outfit's pages was
refused because the page cache never gives a page back: other models hold slot indexes,
and a page given back under one would draw white. The allowance is the 64 MiB of slack
`AutoBudgetBytes` already holds back, and it holds back unbounded growth rather than a
ceiling about to be hit - `release\PCtomb5.exe` is x86 with
`IMAGE_FILE_LARGE_ADDRESS_AWARE` set (PE characteristics 0x0122), so the ceiling is 4
GiB, and the heaviest offline load measured (179 room pages, 53 model pages, 350.5 MiB
of texture on the card, a `Mods` folder, every slot) peaks at 209.4 MiB of private
commit. The persisted footstep choice is applied on every way out of the HD level build
because `HDLevelBuild` has five ways to give up - no stem, a bad room count,
`HDRenderInit` refusing the card, no page cache, no room drawing HD - while the bare
footstep set is built whenever `hd_enable` is on; a player whose card cannot do shader
model 3 keeps `hd_enable` at 1 and goes classic, so the row worked inside the level and
was silently dropped at the next level start. On those ways out there is no HD body to
follow - `HDLaraDrawOutfit()` reads `HD_LARA_OUTFIT_NONE` and Lara wears the engine's
own outfit, which has boots - so the answer is given and written into the log.

**How.** `specific/hd/hd_outfitmenu.cpp`/`.h` hold the window; `game/control.cpp` holds
the F4 edge and its guard (`!gfGameMode`, Lara alive, `!BinocularRange`,
`!cutseq_trig`), the edge read before the other conditions so that a key held through a
cutscene does not open the window the moment it ends. `HDLevelRedress` in
`hd_level.cpp`/`.h` is `HDObjectRelease` plus `HDObjectLevelBuild`, the same pair a
level change makes, with `HDLevelRedressAllowed`, `HDLevelRedressRefresh` and
`HDLevelRedressCount` beside it; `HD_REDRESS_ALLOWANCE` is 64 MiB in `hd_memory.h`. The
rooms, their page cache and every classic table stay where they are - Lara's meshes, her
holsters, the weapon in her hand and the inventory are not touched - and the game clock
restarts after the re-dress so its cost does not become catch-up ticks of game logic.
`HDLevelBuild`'s body is `HDLevelBuildHD()` and `HDLevelBuild` is four lines around it,
so the footstep question is asked on every way out and none can skip it;
`specific/file.cpp` carries the note at the end of `LoadLevel` about the load thread
being joined before `HDLevelBuild` runs, and `hd_laradraw.cpp`/`.h` supply
`HDLaraDrawOutfit()`. The page cost was measured with `ModelProbe -pagelist`, which
lists a level's pages per wardrobe entry. Files: `specific/hd/hd_outfitmenu.cpp`/`.h`,
`hd_level.cpp`/`.h`, `hd_memory.h`, `hd_laradraw.cpp`/`.h`, `game/control.cpp`,
`specific/file.cpp`, `tomb5.vcxproj`, `Preparation/Tools/Diagnose/ModelProbe.cpp`,
`README.md`, `Preparation/Tools/README.md`.

**Commits.** `4698b8f`, `7f020e7`, `a25c4e4`, `d9a5822`, `1ff12e4`, `5d98092`

### The outfit chooses the footsteps, and a bare foot silences the floor layer
2026-09-13 - 2026-09-14

**What.** A barefoot outfit walks barefoot. Lara's own foot reaches the mixer through an
animation command - `game/control.cpp` reads `num = cmd[1] & 0x3FFF` out of the
animation and calls `SoundEffect` - so the switch is two shorts: sound ids 0 (the step)
and 4 (the landing) are bent to a second set of samples, with no reload and no
allocation after the level is up. The bare set goes into `DS_Buffers` slots after the
level's own, with two spare `SAMPLE_INFO` entries asked of `game_malloc` in the one
place where the level's bump allocator is still open, and only with `hd_enable` on, so a
player with it off keeps the allocation layout he had. What goes in them is what the
ordinary chain plays for ids 0 and 4 when a mod really replaces the footsteps, and
otherwise the Remaster's own barefoot recordings, sound id 417 of the bank, which no
level carries and which is therefore reached by bank sample number rather than through
the level bridge; the landing is drawn from the same five recordings as the steps. While
boots are worn, ids 0 and 4 are taken from the first unmodded bank of the chain rather
than from a mod's bank or a mod's loose `.wav`. A step is two sounds on one frame and
the second is switched too: `game/footprnt.cpp`'s `AddFootprint` plays the surface under
Lara's foot (`footsounds[floor->fx] + SFX_FOOTSTEPS_MUD`, sound ids 288 to 294) for
every sector whose `fx` is not 5, 6 or 11, and while a bare set is in force that layer
is passed over. The footprint itself is a picture and is untouched, and sound id 17
(`SFX_LARA_WET_FEET`), which stands beside id 0 on the same frames for the water case,
is not switched. With `hd_enable` off the two ids take the ordinary chain and a sound
mod plays as it always did.

**Why.** A sound mod replaces a sound for every outfit at once, so a barefoot footstep
mod made Lara barefoot in boots, in a wetsuit and in a parka alike; with the outfit
choosing between two sets, the two have to be two, and the boots must come from an
unmodded bank. The floor layer had to be silenced with the foot because it is the loud
half of a step: over thirteen classic levels that carry footsteps at all, the foot layer
is 52 takes of 35 to 43 ms while the floor layer is 111 takes of 87 to 433 ms, mean 186,
which is 4.7 times the foot layer. Id 17 is not switched because the bank has one
barefoot set and it is a dry one. The infos' fields are the level's except two:
everything about how a footstep is played - volume, hearing radius, randomness, play
mode - describes the game and stays the level's; the sample number and count are what
was really loaded and the pitch is the bank's. Measured, two banks agree: id 0 is volume
191, radius 10, randomness 0, pitch 12, flags 0x6010 in all sixteen classic levels; id
417 is volume 191, radius 10, randomness 0, pitch 0, flags 0x6014, the Remaster having
turned the random pitch off for those five recordings. The landing is drawn from the
steps because the bank has no barefoot landing: the boots' set is four steps of 33 to 41
ms and one landing of 175 ms, 4.7 times the length of a step and 2.4 times its peak, and
among the bank's five barefoot recordings (106 / 86 / 134 / 69 / 113 ms) none stands out
that way - the longest is 1.4 times the mean of the other four and the loudest by RMS is
1.25 times theirs and is not the longest. A recording of the Remaster with a barefoot
outfit confirms it: matched by normalised cross-correlation with pitch variants of +-6
per cent, every one of eight landings played one of those same five at 0.65 to 0.93
while the boots' 175 ms landing never rose above 0.32. A mod that does distinguish its
own landing - one measured at 4.4 times the RMS of its own steps - supplies the bare
landing from its own id 4, and the second spare info is used. Whether a mod really
replaces the footsteps is decided by comparing payloads and not by asking whether a
mod's bank is being played: of the 390 sound ids one mod's bank shares with the
install's, 387 hold byte-identical recordings and three do not, so a door mod or a voice
mod carries the install's own id 0 byte for byte and would otherwise have had the boots'
recordings handed back under the name "bare"; and a footstep mod need not ship a bank at
all - it may replace a sample with a loose `<NNNN>.wav`, which for the footsteps are
exactly `0000.wav` to `0003.wav` and `0010.wav`. There is room for the extra samples:
`MAX_SAMPLES` is 450 and the sample count over all sixteen classic levels runs from 76
to 148, so five more fit with 297 to spare, and a randomized level carrying all four
Tomb Raider III guests adds 21 and still fits; the check is made before a single sample
is decoded, and a level that has no room says so, the window's row reading *unavailable*
as it does with no bank at all.

**How.** `specific/hd/hd_feet.cpp`/`.h` hold the two sets, the spare `SAMPLE_INFO`
entries and `HDFeetSurfaceQuiet()`, which `game/footprnt.cpp` asks at its one call site
and which is false with `hd_enable` off, false with boots on and false in a level with
no second set. `specific/file.cpp` builds the bare set inside `LoadSamples`, on the load
thread, after the line that counts the level's own samples, and takes the boots from
`HQSfxPayloadBoots`; `HQSfxPayload` for sound id 0 - the ordinary chain, bank and then
the loose overlay - is compared with `HQSfxPayloadBoots` for the same sample to decide
whether a mod really replaces the footsteps, the banks each owning their read buffer so
both can be held at once, and `HQSfxWavSource` (`specific/hqsfx.cpp`/`.h`) names the mod
for the log without reading any audio. `HDFeetLevelEnd()` stands in `FreeLevel`, two
lines above `malloc_ptr = malloc_buffer`, the line that makes every `game_malloc`
pointer stale, because `LoadSamples` returns before reaching the end in seven places, is
skipped entirely when the sound is off, and one level in the script's list has no sound
blocks at all, so the next level would otherwise have found a bare set answering
"available" through the previous level's freed `sample_lut`. Log seams, all `SFXLOG`:
one line per level where the set is built, naming the ids; one the first time in each
bare spell; one naming which bank or which mod supplies the bare set; and one naming the
mod whose ids 0 and 4 are bypassed while the outfit wears boots. Tools: `AnimCmd` over a
level's `LARA` slot counts the frames carrying `PlaySound sfx 0`, `FlipEffect no 32` or
both (148, 132 and 121 on one level); `Tr5Sound` counts and times the two layers over
every classic level; `SfxProbe -wavs` reads a mod of loose files, checks each with the
engine's own `SfxRiffCheck` and prints which bank samples they claim - `SfxProbe` links
`sfxbank.cpp` and `hd_mods.cpp` and not `hqsfx.cpp` or `hd_feet.cpp`, which need
DirectSound and the engine's `Log`, so the two bypass lines are read in the game's own
log. Files: `specific/hd/hd_feet.cpp`/`.h`, `hd_level.cpp`/`.h`, `game/footprnt.cpp`,
`game/control.cpp`, `specific/file.cpp`, `specific/hqsfx.cpp`/`.h`,
`Preparation/Tools/Diagnose/ModelProbe.cpp`, `README.md`.

**Commits.** `4698b8f`, `77b3028`, `52ec0c9`, `1ff12e4`, `5d98092`

### The Mods folder: models, texture pages and sounds from a mod
2026-09-13 - 2026-09-14

**What.** The engine reads the Remaster's own drop-in mod layout,
`Mods\<Category>\<Mod>\{4,5}\{ITEM,TEX,DATA}`. A mod is a folder directly under `Mods`,
or one level deeper, that has a `4` or a `5` subfolder or a `mod.txt`; a name beginning
with `_` is switched off at every depth, which is the whole way back to vanilla and
needs no uninstall. The order is the relative path compared as lower-case ASCII, and
that order is the priority. The scan runs once per session and both the HD level build
and `HQSfxOpen` ask for it, so the two see one list. A mod may supply models (`.TRM`,
resolved in front of the installs), texture pages, a whole `MAIN.SFX` sound bank, and
single replacement samples as loose `<NNNN>.wav` files - exactly four digits, at any
depth under an active mod, first active mod wins per number. `OUTFIT_*.TRM` is refused
as a replacement, because a mod's outfit is an entry in the wardrobe scan. `mod.txt` may
carry `origin=tr1|tr2|tr3`, which puts that game folder of the Tomb Raider I-III bundle
and its two siblings into the mod's page chain. The sound half does not depend on
`hd_enable`. `HQSfxOpen` walks three bank candidates: the first active mod that ships
`5\DATA\MAIN.SFX`, then `audio\MAIN.SFX`, then the installed Remaster's own
`5\DATA\MAIN.SFX` read where it lies, the third making a hand copy into `audio\`
unnecessary. Two further rules act on a mod's models. A weapon hand whose pages resolve
into the origin bundle is drawn on the install's own hands: the mod's triangles on those
pages are dropped and the install's file of the same name is loaded into a companion
slot cut to the pages it shares with `HAND_BARE_REST`, the two drawn together with the
same matrices, node mask and light. And a `.TRM` inventory model a mod brought over from
Tomb Raider I, II or III is put into the donor game's own pose before the ring fits it.

**Why.** A mod may never change a page of a model it did not supply: vanilla is
untouchable, and the mechanism makes that impossible rather than merely unlikely, which
is why the page namespace is arithmetic with compile-time assertions behind it.
`origin=` exists because a mod's model may name pages belonging to another game of the
collection: a measured hand model names 6111 and 6112, its own, and 8002 and 8010, which
are Lara's skin sheets of the I-III bundle, while in the IV-VI bundle those numbers hold
other art (`4\TEX\8002.DDS` is a cut-out sheet that reads 14.5 per cent holes) - without
the line the hand is painted with it, with it both pages come from `1\TEX` and read 0.0
per cent holes. `4\DATA\MAIN.SFX` is deliberately not used: that is Tomb Raider IV's
bank with its own numbering. The plausibility rule that has guarded the HQ bank since
#117 compares the bank's sample against the level's own - at twice the rate the same
recording must be about twice as long, and anything further off than a fifth is a wrong
pairing - and a sound mod breaks it on purpose: at one measured sound id the unmodded
bank holds 3388 bytes at 44100 Hz (0.038 s) against a mod's 28782 bytes at 48000 Hz
(0.300 s), 7.81 times longer, and the landing 1.64 times, so the rule as it stood would
have refused precisely the samples the mod exists to change. A mod's file is a
stranger's file, so the bank and wav readers had to be safe against a crafted one:
`long` is 32 bit in this build, so a chunk guard written `at + 8 + len > bytes`
overflows to a negative number on a length near `LONG_MAX`, the test comes out false,
the cursor is driven negative and the next read lands about two gigabytes below the
buffer - a 60-byte file ended the process that way. A legal wav need not begin with its
format block: a `fact` or a `LIST` may stand in front of `fmt`, and the shape audio
workstations write is a 28-byte `JUNK` block first, so a `WAVEFORMATEX` laid over offset
20 reads the middle of that header, the format test comes out false, the mod's recording
is dropped without a word and the log goes on claiming an overlay nobody will hear; of
six crafted files the gate accepts four and three of those have their format block at
+56, +48 and +28. Without the hand rule a mod's hand is painted with the donor bundle's
skin sheets and comes out looking gloved; the decision is made inside `hd_model` and not
by the caller, because only `hd_model` has the file open and the pages resolved, and the
twin's pages are cut against `HAND_BARE_REST` because that file's pages are skin and
nothing else, so the cut cannot bring the install's own gun in beside the mod's. Without
the classic pose a converted model stands in half a pose: a Remaster `.TRM` stores each
node's vertices with the classic mesh tree's offsets applied and no rotation at all, the
game it came from putting the rotation on when it draws out of the classic object's own
frame 0, so no turn of the rigid file can reproduce the picture the donor game draws.
Applied, the two guns of a mod's `UZI_OPTION` go from parallel to the crossed pair its
home game shows - the angle between their long axes from 0.0 to 65.5 degrees, the pair
from 132 units thick to 60, its overflow of the ring's place from 3.36x to 1.65x - and
the ammunition crosses the same way (0.2 to 42.1 degrees).

**How.** `specific/hd/hd_mods.cpp`/`.h` are the scanner, engine-free like
`hd_install.cpp` so the diagnostic tools link the same file; `winmain.cpp` starts it.
The model resolver sits in front of `find_model` (`hd_model.cpp`): the active mods in
order, each `5\ITEM` then `4\ITEM`, then the installs exactly as before - a replacement
by resolution and never on disk. The page namespace (`hd_pagecache.h`/`.cpp`) offers a
mod's page to the cache as `HD_PAGE_MOD_BASE + modIndex * HD_PAGE_MOD_STRIDE + page`,
`HD_PAGE_MOD_BASE` = 0x50000000 and `HD_PAGE_MOD_STRIDE` = 0x00010000, so a mod's 6111
and the install's 6111 are two cache slots holding two pictures; three `static_assert`s
prove the three namespaces - plain, the I-III bundle's `HD_PAGE_ALT_BASE` (0x40000000)
and the mods' - cannot meet, and a build in which they could fails to compile. A page a
vanilla model asks for never carries a mod key. The chain for a mod's page is the mod's
own `5\TEX`, then its `4\TEX`, then - only with `origin=` - that game folder of the
I-III bundle and its two siblings, then the ordinary IV-VI chain with the plain number,
which lets a mod ship models and no art. `hd_lara.cpp`'s rule that tells a weapon hand's
gun from its skin ("every page this model has that `HAND_BARE_REST` has not got is the
weapon") strips the namespace and compares page numbers. One consequence is left
standing: a page a mod's model shares with the install is loaded twice, +4 pages and
+5.3 MiB on one wardrobe (21 to 25 pages, 28.0 to 33.3 MiB) and nothing on the whole
level; sharing the slot would be safe, but the rule that a mod's model never shares a
slot with a vanilla one is worth more than 1.5 per cent of a wardrobe. Sound:
`specific/sfxbank.cpp`/`.h` hold the `MAIN.SFX` layout, the chain walk that tests it,
the RIFF check and the chain's order (`SfxBankChainLink`, lazy so nothing pays for
`HDInstallRoot()`'s walk through the registry and every Steam library unless the link
that needs it is reached), and both the engine and `SfxProbe` walk that one order;
`hqsfx.cpp`/`.h` keep the level bridge, the substitution and the log. A loose `.wav` is
checked the way the bank's own samples are - RIFF WAVE, uncompressed PCM, mono, 16 bit,
a `data` chunk inside the file - with a 4 MiB cap so a mistyped name cannot pull a video
into memory; without any bank there is no bridge from a level's sample number to a bank
number and the log says so once. The ruler is the first unmodded bank of the chain,
opened beside the mod's and never played from, walked through its own sound id table
rather than by sample number, because the two banks have different sample counts (473
against 463 in the measured case) and nothing says a number means the same sound in
both; if the ruler's sample for this level sample is the right length the bridge is
right and the mod's recording plays whatever its length. The chunk guards subtract
instead of adding - `len > bytes - at - 8`, where the loop's invariant `at + 8 <= bytes`
makes the difference non-negative by construction - in `SfxRiffCheck` and in
`SfxBankOpen`'s outer chain walk, and a bad file is refused with `a chunk runs past the
end of the file`. `HQSfxPayload` goes through that same bounds-checked gate, which
matters because a mod's `5\DATA\MAIN.SFX` stands at the head of the chain and
`SfxBankOpen` validates only the outer chain of RIFFs. `SfxRiffCheck` hands back where
it found the chunks as `SfxRiffParts` and `HQSfxPayload` passes that on; `LoadSamples`
(`specific/file.cpp`) reads the format block at `parts.fmtAt` and the audio at
`parts.dataAt` and takes the PCM branch on `hq` alone, because everything the payload
path returns has already been shown to be mono 16-bit PCM, and the contract that makes
`HQSfxSubstituted()` and `HQSfxOverlaid()` true by construction is written into
`hqsfx.h`. The level's own samples are untouched - their tag is still read at 20, and
the one call that still lays a `WAVEFORMATEX` over offset 20, `DXCreateSampleADPCM`, is
only ever handed `samples_buffer`, which no mod can reach; the two walks differ in one
point (`file.cpp`'s breaks at the first `data` chunk, `SfxRiffCheck`'s reports the last)
and over 1409 samples of three banks and five loose files not one has a second `data`
chunk, a second `fmt`, or a `fmt` anywhere but +20. The hand rule: `hd_model.cpp` asks
`HDPageCacheOriginOf`, which calls `resolve_page` and loads nothing, for where each of a
model's pages really resolves (`hd_pagecache.cpp`/`.h` export it); measured on one level
with nine mods, the mod's hand keeps 2 pages and 31,267 vertices on nodes 10 and 13, the
install's twin brings 1 page, and the mod's holster model stays whole at 27,624 vertices
on nodes 1 and 4. The twin is counted in every column of the part line - vertices,
indices, ranges, pages and both byte counts, about 155 KiB of buffer under one Uzi mod -
its own fold counters are printed there, and its second `.TRM` read is counted into
`read_ms`; the twin's page cut and a caller's own `onlyPage` cut are one mechanism,
combined as a DROP set with a line in the log, because dropping every page of a file is
representable and keeping none of it is not. The classic pose:
`specific/hd/hd_classic.cpp`/`.h` is engine-free, plain C on the outside, with no engine
global and no `Log`; it walks `<I-III bundle>/<game>/DATA` in name order until a level
file holds the object with at least one mesh and hands back the mesh count, the bones,
frame 0 and the level file it used, every count read out of a file checked against the
bytes that are left and the texture pages seeked past rather than loaded, one parse per
(game, id) per session with refusals cached. The three level layouts are the published
TR1/TR2/TR3 formats; the `.TRM` says which node a vertex rides on and the classic level
file says where the mesh tree puts that node, and both are the player's own data files -
nothing of any Remaster binary is in it. Two format facts were settled by measurement
against the files' own data: in a TR1 frame the SECOND of the two rotation words is the
high half (read that way the walk reproduces the frames' own bounding boxes to 2.1 units
at worst over the eight weapon and ammunition objects of `1/DATA/GYM.PHD`, against 50 to
107 units the other way, and Tomb Raider 1's object 102 then gives the same rotations,
bones and bounds as Tomb Raider 2's 160 and Tomb Raider 3's 188), and the pose is
`phd_RotYXZ` (y, then x, then z) with the point taken as a column and the frame's offset
part of the pose, reproducing those boxes to 2.1 units where the next family of
conventions misses by 40.8 and the worst by 184.7. "The first level in name order" is a
rule and holds: over the eight option objects of games 1 and 2 every level file that
holds the object holds the same mesh count, bones and frame 0, and game 3 agrees on 17
of its 20, the three exceptions being `TITLE.TR2` and `AREA51.TR2` carrying a stub or a
left-over. The pose is applied in `hd_model.cpp` where the mirror fold is, right after
the `.TRM` is read and before `build()` takes the box and the principal axes, and it is
real skinning - three nodes, three weights, normals turned and re-normalised; it refuses
with a line unless the model came from a mod with an `origin=` game, its name is in the
table, and the classic object's mesh count IS the `.TRM`'s node count, which is the
proof that the two belong together because a `.TRM`'s bone numbers are classic mesh
numbers. The table in `hd_classic.cpp` has thirteen rows - the four weapons and the
ammunition boxes under the donor games' own names and under this engine's slot names -
and its object ids come from GPL sources: TR3's out of the **tomb3** sources
(`game/objects.h`), TR1's and TR2's out of the **TRX** project (LostArtefacts),
`catalog_objects.csv`, whose TR3 column agrees with tomb3 on every id; each was then
found in the player's own installation. Cost, whole process: 85 to 93 ms once per (game,
id) per session. Seams behind `-log`: `MODLOG folder=` (with `mods=` on both branches),
one `MODLOG mod<i> path= origin= models= pages= bank= wavs=` per mod, `MODLOG disabled
<path>`, `MODLOG model <NAME> <- mod<i>` once per replaced model per session, written
where `find_model` accepts the answer and keyed by name and mod so a face looked up for
a body from another mod cannot log a replacement that was never drawn, `MODLOG page
mod<i>:<n> comes from ...`, `MODLOG bank <label> (<path>) - opened/not opened`, `MODLOG
bank ruler:`, `MODLOG bridge:`, `MODLOG bank sample <n> ...`, `MODLOG wav <nnnn> <-
mod<i>` and `MODLOG wav <nnnn> refused - <why>`; a mod page prints as `mod<i>:<n>`
wherever a page number is printed, `HDLOG models` gained a `mod:` column and `HDLOG
models budget` a `frommod=`, the `HQ SFX:` summary line names the bank really used, and
`HDLOG classic <game>/<level> object <id> meshes=.. bones=.. frame0 ..`, its refusal,
and `HDLOG level=.. ring pose slot=.. model=.. classic=<game>/<level>#<id> nodes=..
applied (box .. -> ..)` or `not posed: <why>` cover the pose. Tools: `ModelProbe -mods
<folder>`, `-tr3 <3-folder>`, `-nohandmix`, `-classicpose <game> <MODEL_NAME>`,
`-noclassicpose`, with the page list naming the folder every page was really read from;
`SfxProbe` and `bsfx.bat` are new, `-samples` pushing every sample of the chosen bank
and of the ruler through the gate with a `layout` line per accepted file. Both tools
link the engine's own files. Not supported and documented as such in the README: a mod
that ships `<NAME>_HEAD.TRM` without the body's `<NAME>.TRM` is inert, because a face is
resolved wherever its body came from so that the two wear pages from one source. Files:
`specific/hd/hd_mods.cpp`/`.h`, `hd_model.cpp`/`.h`, `hd_pagecache.cpp`/`.h`,
`hd_classic.cpp`/`.h`, `hd_lara.cpp`/`.h`, `hd_bind.cpp`, `hd_object.cpp`,
`specific/hqsfx.cpp`/`.h`, `specific/sfxbank.cpp`/`.h`, `specific/file.cpp`,
`specific/winmain.cpp`, `tomb5.vcxproj`, `Preparation/Tools/Diagnose/ModelProbe.cpp`,
`SfxProbe.cpp`, `bsfx.bat`, `bmap.bat`, `bmodel.bat`, `broom.bat`, `btrg.bat`,
`Preparation/Tools/README.md`, `README.md`.

**Commits.** `8156128`, `989dd90`, `f5d2370`, `07cda76`, `e8f779a`, `0947713`,
`9378ef7`, `0fac003`, `d63f0d9`, `047779b`

### Imported enemy sounds are named in the sidecar, not guessed at by length
2026-09-13

**What.** A run that imports a Tomb Raider III enemy writes the sound ids it wrote into
the level as one `sfximport <id> <id> ...` line in the `.hdm` sidecar, ascending out of
a `std::set` so that the same seed gives the same file, and no line at all in a run that
imported nothing. The HQ sound bridge refuses to pair those ids with the Remaster's bank
and writes a line for each; ids the run merely moved between TR5 levels are not in the
list and keep their pairing. Beside that the bridge works out, from the level's own
sound map alone, which of the bank's 64 level bits is the level being loaded - the bit
whose ids cover this level's and no others, every id the level has and the bit claims
counting for it and every one it does not counting against it - and writes a line for
every pairing that bit excludes. The sidecar is read above the HD gate, so the list is
in force in the shipping build with `hd_enable` off.

**Why.** The guests take free slots of the TR5 sound map - the vulture 420..423, the
Compy 430..434, the raptor 438..444, the T-Rex 445..449 - and free means free in a TR5
level; it says nothing about the Remaster's bank, where ids 418..423 are six voice
lines. The bridge pairs by sound id and judges a pairing by length, and a length cannot
tell a wrong pairing from a different recording: on one randomized level the level's own
420, 421 and 423 (322, 425 and 406 ms) were refused against the bank's 624, 624 and 1037
ms, but 422 at 604 ms against the bank's 624 was accepted at 1.03 times the expected
length, so an imported vulture died speaking an English voice line. A mod's bank with
390 sound ids against the install's 396 simply has not got 418..423. A length cannot
decide this and a name can: the run knows exactly which ids it wrote. The level bit is
worked out and not tabulated, because a table of level names would be silently wrong
about a randomized level or a level pack, and it is sound: over all sixteen classic
levels each matches exactly one bit and matches it completely while the runner-up covers
at most two thirds (ANDREA1 bit 1, ANDREA2 2, ANDREA3 3, JOBY2 4, JOBY3 5, JOBY4 6,
JOBY5 7, ANDY1 8, ANDY2 9, ANDY3 10, RICH1 11, RICH2 12, RICH3 14); on one randomized
level it names 32 excluded pairings, every one an enemy the run moved there. The sidecar
is read above the HD gate because it is a level fact and not an HD fact: read below it
the list was empty in the shipping build - the bank chain the refusal protects is
deliberately not gated, since sound does not care whether `hd_enable` is on - so the
wrong pairing came back and the summary reported nought imported ids, and no tool could
show it, because a tool calls the sidecar loader unconditionally by construction.

**How.** `specific/hd/hd_markers.cpp`/`.h` read the `sfximport` line and answer
`HDMarkersSoundImported`. `hd_level.cpp`/`.h` call `HDMarkersLoad` above the
`HDEnabled()` gate; the other half of the bracket, `HDMarkersFree` in `HDLevelUnload`,
was never gated. The order is unchanged and all on the load thread: `FreeLevel` ->
`HDMarkersFree`, `HDLevelLoad` -> `HDMarkersLoad`, then `LoadSamples` -> `HQSfxBuildMap`
-> the refusal. `specific/hqsfx.cpp`/`.h` hold the bridge and the refusal lines;
`SfxBankLevelBit` and `SfxPairPlausible` live in `specific/sfxbank.cpp`/`.h`, so a tool
judges a pairing with the engine's own arithmetic rather than a copy, and the mask
counter counts first and prints second, so the summary is not capped at the 32 lines it
prints. On the writing side, `TR5Randomizer/src/core/Tr5Level.cpp`/`.h` collect the ids
and `core/ApplyPatch.cpp` writes the line, the slot ranges being `Tr5Source::dinos`; an
engine older than this has no rule for the word and ignores the line, which is what the
sidecar format was built for. Tool: `SfxProbe -level <path.trc>` reads a finished level
and the sidecar beside it and prints the whole decision table, `-samples` checks the
bank's 473 samples, and `Preparation/Tools/Diagnose/bsfx.bat` builds it.

**Commits.** `410ddbd`, `ee2aff0`, `d9a5822`

### The font in HD
2026-09-09

**What.** Every line of text the game draws - the title menu, the names under the
inventory ring, the options pages, the load and save lists, the statistics, the
ammunition counter, the name of a pickup, the subtitles of a cutscene - is drawn from
the Remaster's 2048 x 2048 font atlas when it is installed, through one branch in
`DrawChar` (`game/text.cpp`). Position, size, `FontShades` colour and specular stay the
classic ones, because they stay the classic code. Thirteen of the 106 glyph rectangles
keep the classic picture: the twelve PlayStation pad icons, which the atlas does not
carry, and the c-cedilla.

**Why.** All text in the engine comes out of ONE 256 x 256 page and ONE table of 106
rectangles, so a single branch covers every text path and there is no second one to
forget. The big atlas is a new drawing and not the small picture times eight: shrunk
back to 256 with an 8 x 8 box filter it differs from `FONT0_OG.DDS` in 12.6 per cent of
the texels of the matched rectangles, its letters reach up to three classic texels past
the eightfold rectangle, and it carries a dark rim and a drop shadow the small one has
not (over the 93 matched glyphs, fully opaque texels have a mean brightness of 224 of
255 and the half-transparent ones 44), which is what makes white letters readable over a
bright wall.

**How.** The page is the same in every level - md5 `191cfc2fdba5b4c54024361c463d7537`
over all sixteen shipped `.TRC` files, the title included - so the match runs once in a
session and every further level load is a comparison of that md5 (`HDFontLevelPage`,
`specific/hd/hd_font.cpp`); it is keyed on the page and not on a flag, so a level file
carrying a different font page would be matched again rather than drawn with another
alphabet's table. Matching: the classic rectangle's ink mask is slid over
`5\TEX\FONT0_OG.DDS` (256 x 256, uncompressed B8G8R8A8, the classic glyph bitmaps
re-typeset in ASCII order) and scored intersection over union
(`specific/hd/hd_fontmatch.cpp`, `HDFontMatchAll`), accepted at 0.94 and above. The idea
and the threshold are TROne's (GPLv3, TROne by Todesengel1931 -
https://github.com/todesengel1931), and the reason is theirs too: a similar character is
the wrong character. 93 of the 106 rectangles are found, 92 at score 1.000 and the K at
0.975; `FONT1` is checked in the same run and matches 22 - it is a Cyrillic alphabet and
those 22 are the letters both scripts draw alike - so which atlas is used is decided by
the count and not by the file's name, and an installation that ships them the other way
round still works. Drawing: `5\TEX\FONT0.DDS` is 2048 x 2048, BC7, six mip levels, the
same alphabet in the same places at eight times the size, so a normalised coordinate on
the one is a normalised coordinate on the other and the glyph sits at the same place
plus a whole number of classic texels; the UV is therefore not recomputed but SHIFTED by
that number, and rendered classic and HD the texels a screen pixel walks agree to
1.6e-06 relative. The margin is per glyph: the classic arithmetic samples the rectangle
inset by half a texel on each side, which the classic page needs because it packs its
glyphs with no gap, and at eight times the size that inset is four HD texels and cuts
off exactly the antialiased rim and the drop shadow, 12.6 per cent of a glyph's ink; the
sampling window and the quad are widened by the same fraction of themselves (TROne's
rule), which changes what is visible and not where anything is. A uniform margin does
not work - at every margin from half a texel upwards a slice of the neighbouring tick
appears beside the apostrophe and beside the comma - so `HDFontMargins` works out per
glyph how far the window may open before it touches another letter, with one texel as
the ceiling: 90 of the 93 get the whole texel, the apostrophe 0.25, the asterisk and the
comma 0.125, none gets zero. The flood fill runs over WHITE ink, which the dark shadow
between two letters separates where alpha does not; it runs on the big picture and is
seeded from the classic page's ink rather than from the whole matched rectangle, because
a neighbour's larger letter can reach a texel or two into that rectangle. Accents keep
their classic arithmetic: `PrintString` composes an accented letter from two ordinary
glyphs - the base at the letter's place and the mark at `def->w / 2 + x - 3, y +
def->YOffset` - and each goes through `DrawChar` on its own, so the relative position
between them is the classic one. Colour and alpha needed no change: the engine
multiplies the texture by the vertex colour and adds the specular half
(`D3DTOP_MODULATE` with `D3DTA_DIFFUSE`, `specific/function_table.cpp`), the atlas's ink
is white so it takes that colour as the classic page's white ink does, the dark rim
stays dark whatever it is multiplied by, and `D3DRS_ALPHAREF` is 0 with
`D3DCMP_NOTEQUAL`, so the alpha test throws away only what is completely transparent.
`Textures[]` must not grow, because the font is addressed as `nTextures - 2`, the sky as
`- 1` and the two title pages as `- 4` and `- 5`, so the glyph quad carries
`HD_FONT_TPAGE` = 4096, which is not an index at all, and `HWR_DrawSortList`
(`specific/polyinsert.cpp`) turns it into the font texture before any of its cases can
read `Textures[4096]`; nothing else can write that number, a tpage being otherwise
always an index and `LoadTextures` refusing a level with more than the 256 entries the
array has, and the sort list already groups by tpage, so every glyph of a frame ends up
in one draw call. Mips and a LOD bias: at 1920 x 1080 a thirteen-texel glyph is drawn 58
pixels high out of 104 atlas texels, so the atlas is minified by 1.8 - by 2.7 at 1280 x
720 and 1.35 at 2560 x 1440 - and without mips that is a bilinear undersample in which
thin strokes crawl, so the whole six-level chain out of the `.DDS` is uploaded and the
draw sets `D3DTEXF_LINEAR` with `D3DSAMP_MIPMAPLODBIAS` -0.7, which lands at level 0.14
at 1080p and 0 at 1440p, the sampler states being put back the way the classic path
keeps them the moment the call returns; the three minification figures are our own
measurement and the bias -0.7 is TROne's number (GPLv3, `host/render/game_renderer.cpp`
of the stand of 2026-09-13), cited as such in `specific/polyinsert.cpp`. Where it lives:
`game/chardef.inc` holds the 106 rectangles, moved out of `text.cpp` unchanged, and
`game/accent.inc` the 46 accented letters, so the engine, the HD font and `FontProbe`
read the same numbers; `specific/hd/hd_fontmatch.cpp` holds the matcher, the margins, an
uncompressed-DDS reader and an MD5 with no engine dependency at all, so `FontProbe`
compiles that very file; `specific/hd/hd_font.cpp` finds the atlas, runs the match on
the load thread, makes the texture on the main thread and answers `DrawChar`'s one
question; `game/text.cpp` gains one branch, `specific/polyinsert.cpp` one, and
`specific/file.cpp` two lines - the font page to the matcher where it is read, and
`HDFontBuild` beside `HDLevelBuild`. Seam: `HDLOG font atlas=FONT0 matched=93/106
below=13 page=<md5>`, one line per glyph that did not make it with its score, and one
line saying how many glyphs got the whole margin. Classic path: `HDFontLevelPage`
returns at its first line with `hd_enable` off, so no file is opened and not a byte of
the page is copied, `g_matched` stays false, `HDFontBuild` returns, there is no texture,
`HDFontGlyphAt` returns 0 on its first test, and `DrawChar` runs the same arithmetic
with the same `tex.tpage = nTextures - 2`, while `HWR_DrawSortList`'s new branch tests
for a tpage no quad can then carry; `fps_mode` is not touched. Files: `game/text.cpp`,
`game/chardef.inc`, `game/accent.inc`, `specific/hd/hd_font.cpp`/`.h`,
`hd_fontmatch.cpp`/`.h`, `specific/polyinsert.cpp`, `specific/file.cpp`,
`specific/function_table.cpp`, `Preparation/Tools/Diagnose/FontProbe.cpp`.

**Commits.** `f6d2e6e`

## 2.5 Sixty frames a second

The engine presents up to sixty pictures a second while its simulation keeps the thirty
logic steps a second it has always had. Nothing of the game's arithmetic moved: what a
frame that carries no logic tick shows is interpolated from the two ticks around it, and
every counter that used to measure time in drawn frames was put on the logic tick
instead. The setting is `tomb5.fps_mode`, default 1 (sixty); `fps_mode 0` is the engine
as it was, and every rule below is guarded so that at that setting no line of it is
reached. Engine paths are relative to `TOMB5 Code/TOMB5`; the design is written out in
the 60 fps plan (not part of the repository). Where TROne (GPLv3) by Todesengel1931 -
https://github.com/todesengel1931 - solves the same problem differently, its idea was
re-derived and credited in the header rather than taken, and nothing of it is copied;
nothing here comes from the Remaster's programs.

### The frame rate option and the clock
2026-09-06 - 2026-09-07

**What.** `tomb5.fps_mode` is a `ulong` in `tomb5_options` (`global/types.h`), read and
written under the registry name `fps_mode`: `0` is thirty presents a second, `1` is
sixty and is the default, `2` hands the pacing to the display. A value above 2 is
clamped back to 1 (`tomb5/tomb5.cpp`). The option is the *Frame rate* row and prints
`30`, `60` or `Display`. The simulation runs thirty logic steps a second in every mode.
The modal screens that stop the world - the pause menu, the load and save screens, the
death menu, the special features, the F4 outfit screen and the loading fade - keep their
two-tick present; the inventory ring and the title screen have since been given the
frame rate by rules of their own.

**Why.** The thirty hertz cap was a single busy wait and nothing else. `Sync()`
(`specific/time.cpp`) counts whole sixtieths of a second (`frequency = pfq / 60`) and
`S_DumpScreen()` (`specific/output.cpp`) waited for two of those ticks before it
presented, while everything downstream was already built for a 60 Hz tick:
`ControlPhase()` (`game/control.cpp`) runs `for (framecount += nframes; framecount > 0;
framecount -= 2)`, where `nframes` is how many sixtieths the frame really took and a
logic step costs two of them. Feeding one tick per present therefore yields sixty
presents and thirty logic steps with no line of the simulation touched. A modal loop
steps its own animation counters once per present and those counters were written for a
thirty hertz present, which is why two ticks is what leaves them alone.

**How.** `gPresentMinTicks` (`specific/output.cpp`, declared in `specific/output.h`)
replaces the constant `2` in `S_DumpScreen`'s wait. `DrawPhaseGame()` (`game/draw.cpp`)
sets it to `GameFrameMinTicks()` around its own present and puts it back to `2` after
`ProfileFrameEnd`, so a frame is judged against the slot it really waited for.
`GameFrameMinTicks()` returns 2 at `fps_mode 0` and while `InventoryActive`, 0 when the
present itself blocks (`fps_mode 2`) and 1 otherwise; `ActiveFpsMode()` reports the mode
the device really got and `TargetPresentHz()` is the same decision as a rate (30 or 60).
The blend factor is `InterpAlphaNow()` (`game/interpolate.cpp`): at `fps_mode 2` it is
the clock (`InterpTickFraction()`), otherwise 1 for any mode but 1, and 1 again for any
frame whose present waits two ticks (`GameFrameMinTicks() >= 2`) - the same test the
display-bound branch makes, so a modal frame is never drawn between two ticks. `DoTitle`
clears `framecount` as `DoLevel` has always done (`game/gameflow.cpp`): the accumulator
survives a level change and the two-ticks-a-present loop returns it to where it started,
so a title entered on the -1 parity read a factor of 0.5 on every frame, every frame
counted as an in-between frame, every counter guarded on the logic tick stood still -
among them `DoScreenFade`, the only thing that sets `DoFade` to 2 - and the fade-in from
black never advanced, leaving a black quad over the menu that swallowed every key. The
clearing costs at most half a logic tick at the moment the title is entered and runs at
`fps_mode 0` too. The *Frame rate* row stands on the *Display* tab of the table-driven
options frame (`game/menu_frame.cpp`) that replaced the tomb5 options pages
(`tomb5/troyestuff.cpp`). At `fps_mode 0` every branch is inert: `gPresentMinTicks` is
2, `S_DumpScreen` reads `while (n < 2)`, `TargetPresentHz()` is 30 and the frame budget
constants are the old ones to the bit. `Preparation/Tools/Diagnose/InterpTest.cpp`
carries a `title_cadence` section that runs the title's two-ticks-a-present accumulator
from both parities and checks that no frame of either is a tween frame and that the
logic tick count is unchanged.

**Commits.** `1a0cf87`, `34a7ccc`, `5301765`, `a785a50`, `45d9130`

### VSync and the rate the monitor sets
2026-09-07

**What.** `tomb5.vsync` (on by default) switches on the vertical sync the port never
had, and `fps_mode 2` waits for nothing of its own and lets `Present` wait for the
panel. The two rows stand together on the *Display* tab; the *Display* row reads
`Display (60)` when VSync is off, because the blocking present is what paces that mode,
and the VSync row reads `on (restart)`, being a device-creation parameter and the one
setting in the build that is not instant. There is no uncapped mode and no path to one:
without a blocking present `ActiveFpsMode()` reports 1 and the sixty hertz spin is back.

**Why.** With the panel pacing the frames the blend factor can no longer come from the
parity of the loop, and two further rules break with it. "`alpha < 1`" is only the same
thing as "this frame carries no logic tick" while the cadence is two frames to a tick;
at 144 Hz it is true on four frames out of five, and every fade, raindrop, corpse and
caption would have stopped. The clock also has to be read on the other side of the flip:
every other present reads `Sync()` first and flips afterwards because there the wait is
the read, but here the wait is inside `Present`, and a `Sync()` taken before it lands
the waiting ticks in the next frame's count - the factor would ask for a state the logic
has not reached and the picture would stand still for a frame at every logic step. A
driver can also lie: a control panel forcing vsync off leaves the device claiming the
interval while `Present` returns at once, and then nothing bounds the loop at all.

**How.** `PresentationInterval` is part of `D3DPRESENT_PARAMETERS`, so the option is
read in `DXCreate` (`specific/dxshell.cpp`) at startup, on Alt+Enter and on a resolution
change, and the device-lost path in `DXShowFrame` resets from the same `PresentParams`.
`DXCreateD3DDevice` checks `caps.PresentationIntervals` first and drops back to
`IMMEDIATE` with a log line rather than failing to create a device; `DXVSyncActive()`
afterwards answers what the device really got. Because `init_tomb5_stuff` runs on the
game thread that `WinMain` starts after the first `DXCreate`,
`load_tomb5_present_settings()` (`tomb5/tomb5.cpp`) reads that one value ahead of time
through the read-only registry helper `REG_PeekLong` - `OpenRegistry` uses
`RegCreateKeyEx` and `init_tomb5_stuff` decides from `REG_KeyWasCreated()` whether to
write the whole set of defaults, so a second reader opening the key early would have
taken a first run's defaults away. `GameFrameMinTicks()` returns 0 for a display-bound
game frame, so the spin is never entered; at 144 Hz two frames out of three come back
from `Sync()` with no whole tick, `ControlPhase(0)` runs no logic step and the frame is
drawn from the clock. `specific/time.cpp` gained `TIME_SubTick()`, how far past the last
tick the clock stands, read without consuming it and deliberately not capped at one;
`InterpRefreshAlpha` (`game/interpolate_math.h`) is `(framecount + subTick + 1) / 2`,
the only mapping that never has to be clamped - the loop spends two ticks per step but
runs one as soon as the accumulator is positive, so the newest state is the world at `S
= 2k+1` and the factor walks 0 to 1 across every logic step at any refresh rate with no
accumulator of its own. The clamp is for hitches and for the early returns inside
`ControlPhase`, both of which draw the newest state, which is what thirty hertz shows on
a slow frame. Logic ticks are counted rather than inferred (`g_tickSerial`, stepped in
`InterpCapturePrev`), and the drawn frame that finds a new one claims the per-tick
counters, so exactly one frame per logic tick steps them whatever the panel does.
`S_DumpScreen` flips first and reads the clock afterwards in this one mode, which leaves
`PROF_SPIN` empty and `PROF_FLIP` carrying the wait. `NoteDisplayBoundPresent` is the
watchdog: six hundred display-bound presents in a row under half the panel's period is
an overridden interval, and the frame rate falls back to the sixty hertz spin for the
session with one line in the log; an iconic window is not counted, because `Present` on
a minimised window can return at once and coming back from the taskbar must not cost the
player the setting. The mode costs one 1/60 s tick more display latency than `fps_mode
1`, which is why 60 remains the default; in exchange the busy wait is gone and the core
that spun inside `while (!Sync());` waits inside the driver. Options rows in
`game/menu_frame.cpp`.

**Commits.** `a785a50`

### The in-between frame: the root snapshot
2026-09-07 - 2026-09-20

**What.** A drawn frame that carries no logic tick shows the midpoint between the two
ticks around it instead of a duplicate of the frame before it. New files
`game/interpolate.cpp`, `game/interpolate.h` and `game/interpolate_math.h` (the
arithmetic alone, so that an offline test can include it). Three call sites are the
whole seam: `InterpCapturePrev()` as the first statement inside `ControlPhase`'s tick
body, before `GlobalCounter++` (`game/control.cpp`);
`InterpBeginFrame(InterpAlphaNow())` as the first line of `DrawPhaseGame` after
`ProfileFrameBegin`; and `InterpEndFrame()` after `S_AnimateTextures`, before
`ProfileFrameEnd` (`game/draw.cpp`). The snapshot holds `items[512]` (position, three
rotations, room, object, anim, frame, valid), `effects[24]`, `hairs[2][7]`, the camera
and Lara's nine aim angles (the torso twist, the head turn and the two arms, eighteen
shorts) - 15.0 KB, beside 10.7 KB for the live values a frame overwrites and 1.1 KB of
touched-index lists, 27 KB of statics before the particle movers join them, with no
allocation and no pointer into the `game_malloc` pool. Only slots that moved in the tick
are touched. A node whose y angle differs by exactly 32768 between two ticks keeps the
live rotation instead of blending it; its position still blends.

**Why.** `InterpCapturePrev` is the first statement of the tick body because when one
`ControlPhase` runs several ticks - a hitch, the two priming ticks of a level start -
each overwrites the last, and what stands at draw time is the state before the final
tick, the only neighbour the drawn state has. `room`, `object`, `anim` and `frame` are
stored so that a slot handed to a different object can be told apart, otherwise a dead
enemy's slot reused by a spawn would be drawn sliding from the grave to the spawn point.
The braid is in the snapshot because `HairControl` derives every link from the head, and
a head that moves half a step while the braid does not is a plait hanging in the air.
Cut scenes are snapped whole as a correctness requirement: their camera is decoded from
a frame index (`game/deltapak.cpp`) and `frigup_lara()` moves Lara from inside
`DrawPhaseGame` itself, so blending the first would fight a decoder and restoring after
the second would undo what it did (the entry *The cut scenes at the frame rate* below
finds otherwise and blends them). The simulation's own generator had to be protected:
the draw path calls `GetRandomControl` in five places and the star twinkle alone takes
four thousand draws out of the logic stream on every drawn frame, so without a restore
two runs of the same level with the same inputs would drift apart between the frame
rates. The half-turn rule answers `turn180_effect` (`game/effects.cpp`), which does
`x_rot = -x_rot; y_rot -= 0x8000` on an animation command - Lara's roll, its second
half, the water roll and four more, seven over her slot in a level and six of them in
every level: on that tick the animation's own root bone turns the other half, so at
thirty the picture is continuous, but the interpolator blends root and pose apart,
`InterpLerpAngle` took exactly half a turn the arbitrary anticlockwise way while the
pose blend was refused on that same tick, and Lara was drawn sideways for one tween
frame. The braid's own flips are the same case: a segment hanging straight down has an
undefined yaw that flips by exactly half a turn from tick to tick, five or six times a
second while she stands still.

**How.** At `fps_mode 1` the blend factor needs no clock: after `ControlPhase` returns,
`framecount` is `-1` when a tick ran and `0` when none did, so the drawn states are `T1,
(T1+T2)/2, T2, ...`. The frame is drawn live instead of blended when there is no
previous state, when `object_number` changed, on a teleport (`abs(dx)+abs(dy)+abs(dz) >
512` in one tick - nothing in TR5 crosses half a sector under its own power in 33 ms, a
terminal fall is about 130 units), on the same test across a room boundary, on a camera
cut, and while `GLOBAL_playing_cutseq` is set, in which case `InterpBeginFrame` returns
before it writes anything. `InterpInvalidate(why)` drops the previous state at a level
and a title start (`game/gameflow.cpp`), in `FreeLevel` (`specific/file.cpp`), when a
savegame is read (`game/savegame.cpp`, beside `JustLoaded = 1`) and on the return from
the inventory, the save screen, the load screen and the pause menu. `InterpBeginFrame`
remembers `rand_1` on an in-between frame and `InterpEndFrame` puts it back
(`GetRandomControlSeed()`, `specific/function_stubs.cpp`), so the consumption per logic
tick is what it is at thirty whatever the drawing code does; the star twinkle
(`DrawStars`, `specific/specificfx.cpp`) and the floor lasers' water pulse are guarded
on top because both write their result back into persistent state. Angles blend the
short way round; Lara's nine aim angles are blended here rather than by the pose machine
because they are per-tick values that come from the player, written by `LookLeftRight`
and the weapon code and read by both halves of `CalcLaraMatrices` through `phd_RotYXZ`.
The half-turn case is named by `InterpHalfTurn` (`game/interpolate_math.h`) - a y
difference of exactly 32768 - and `ApplyNode` (`game/interpolate.cpp`) then leaves the
whole rotation at the live value, the effect negating `x_rot` in the same breath so that
the rotation goes as one; on such a tick the previous animation root offset is also
turned by the same flip before the root-offset blend of the next entry runs
(`InterpRootFlip`, `game/interpolate.h`/`.cpp`, `game/interpolate_math.h`), because the
underwater roll's animation change flips `y_rot` while the two offsets are the same
world point one turn apart and would otherwise drag her through her own pivot by about
140 units - two sign changes are the whole correction, since `Rx(a) * Ry(180) * Rx(a) =
diag(-1, 1, -1)` for every `a`. Of the seven `turn180_effect` animations three reach
that rule on the flip tick, whose tween is refused: the roll 147 fires on its first
frame, so the change 146 to 147 is the flip tick (the owner's `-log` prints `INTERPLOG
root lara anim 146->147 ... flip=1 turned=(9,-376,4) ... kept=(0,0,5)`, and RootProbe
`-grab 146 147 0 0 -51` over ANDREA1 prints the same numbers); the underwater roll 205
fires on a key its rate 1 far-pose rule refuses the tween across; and the tightrope turn
451 fires on the second key of its held far pair. The other four, 48, 207, 210 and 212,
fire on their last frame and step the frame by one on the flip tick, so the pose is
accepted there; that frame belongs to the old yaw and is drawn from turned copies (see
*Lara facing the wrong way for one tick in the jump twists*), and the change that
follows it turns the previous offset the same way (`g_rootTurned`, `game/interpolate.cpp`).
Nothing downstream knows about any of it: `DrawRooms`, `InsertRoom`, `phd_PutPolygons`,
the bucket sorter, the shadows, the lighting, `HDFrameRoom`, `HDObjectNode` and
`HDLaraDraw` read the same globals and get the interpolated ones - not one line of
`specific/hd/` changed, because the HD seam takes its matrices off the engine's own
stack; `HDLOG frame lara` gained `interp=`, what the frame was drawn at, and `INTERPLOG`
gains `halfturn=N` per second (the braid's flips show as `HAIRLOG dyaw 180` on the same
seconds). Nothing of TROne's blend was taken: TROne
records a tick's finished view-space node matrices and replays them blended, in which an
item's half turn and the animation's opposite half have already cancelled, so it needs
no such rule; two larger ways were priced and set aside - blending the composite yaw of
item and root bone, which buys one frame of facing smoothness in a somersault at the
price of a gimbal case where the root bone is pitched, and blending per-node matrices as
TROne does, which is a rewrite of the pose path. Checked by
`Preparation/Tools/Diagnose/InterpTest.cpp`, sections `angles` (exactly half a turn
either way, and `turn180_effect` from every angle on the 512-unit lattice, is a half
turn; one unit short of it is not) and `root_flip`. At `fps_mode 0` `InterpAlphaNow`
returns 1 and both frame functions return before writing a byte.

**Commits.** `324e404`, `ced7f30`, `14f0303`, `83d93c0`, `b7607c9`

### The animation's root offset on a refused frame
2026-09-16 - 2026-09-21

**What.** At `fps_mode 1` and `fps_mode 2` a drawn tween frame whose pose the
interpolator's rule K6 refused still blends the animation's stored root offset in time,
between the offset the previous tick drew and the offset this tick draws, narrowed per
axis to the part this tick's own position step pays for. The pose, the limbs and the
rotations stay the current tick's, untouched. `fps_mode 0` draws exactly the frame it
drew before, and a frame that carries the logic tick, a slot that snapped and a tween
frame whose pose was accepted are unchanged as well.

**Why.** Every stored frame carries a bounding box and then the root offset (`frm[6]`,
`frm[7]`, `frm[8]`), which the draw applies after the item's own rotation, so the drawn
root is `item->pos` plus that offset. A climb carries its whole vertical travel in the
offsets and moves `item->pos` only once, at the wrap, through the animation command
`TranslateItem` (animation 161 `ANIM_CLIMBING` wraps with (0,-512,0), 168
`ANIM_CLIMBDOWN` with (0,+512,0), 169 into 168 with (0,+256,0), 172 into 173 with
(0,-190,0)). The root snapshot blends `item->pos` across such a wrap, but a refused pose
fell back to the current frame's offset, so half of the position step was in the picture
and none of the offset's: 248 units of vertical error on one frame per climb cycle,
presented four or five times in a row on a 165 Hz panel. Blending the offset
unconditionally errs the other way wherever no position step pays for it: the
crawl-to-crouch change (animation 274 to 222) has an offset step of 61 units on y while
`item->pos` does not move at all, and the crouch pose - built for a root 143 units above
the floor - was drawn 54, 45, 33 and 21 units too high over four frames.

**How.** `game/interpolate.cpp` carries the refusal beside the pose flag: `g_poseOk[]`
takes a third value, `POSE_ROOT` (`POSE_NO 0`, `POSE_OK 1`, `POSE_ROOT 2`), so
`GetFramesNow` keeps returning 0 for both of its existing meanings and no call site
needs a new branch. `g_rootPrev[]` holds the offset the previous tick drew;
`g_rootStep[MAX_ITEMS][3]` holds this tick's own position step, filled by `RootStep`
(`TranslateItem`'s own inverse turned back by `y_rot`) inside `InterpBeginFrame`'s
per-item walk, one statement after `g_poseOk[i]` is set and above the `NodeMoved` exit,
so a slot that did not move reads an honest zero. `InterpRootNow` / `InterpRootRefused`
answer the draw site in `DrawAnimatingItem` (`game/draw.cpp`), once per drawn item and
not for Lara alone. The previous tick's offset is recomputed by `GetRootAt`
(`game/draw.cpp`, beside `GetFramesAt`): key index and remainder from the animation's
own rate, the last interval shortened as `GetFrames` shortens it, `InterpKeyAlpha`'s
factor over the pair - bit for bit the arithmetic that produced the picture one
thirtieth of a second earlier; a frame that is not inside the animation it is claimed to
be in is refused. The blend is `InterpRootKey`, `InterpRootLerp` and `InterpRootNarrow`
in `game/interpolate_math.h`: the key pair is blended in eighths (`InterpKeyAlpha`'s
factors are quarters) and the time blend runs at the frame's own alpha through
`InterpLerpLong`, the same primitive and the same alpha the root snapshot blends
`item->pos` with, which is what makes the two halves add up to the whole step. Swept in
1/256 steps over the wrap of animation 161, the worst distance between the drawn root
and the point the two ticks put it at is 504 units as the picture stood before, 31 with
the alpha rounded to an eighth and 1 - the two integer truncations - at the alpha
itself. `InterpRootNarrow` is the per-axis rule, in the item's own `y_rot` frame, with
`d = cur - prev` (`prev` already turned by `InterpRootFlip` on a half-turn tick, or after
a frame the previous tick drew turned by half a turn, `g_rootTurned`): the
blend may keep `min(|d|, |step|)` where offset step and position step point against each
other and nothing where they do not, and it starts from `cur` minus that kept part
instead of from `prev`. `y_rot` alone is the right frame because every hand-written
position step in `game/lara*.cpp` - the ledge grab, the vault and pull-up, the
pushblock, the ladder's corner and hang routines, crawl and duck, pole and rope, and the
water - writes `y_pos` straight and its x/z through `phd_sin`/`phd_cos` of `pos.y_rot`,
exactly as `TranslateItem` turns its three shorts (`game/control.cpp`). The draw applies
the offset through the full `phd_RotYXZ` (`game/delstuff.cpp`), so where pitch or roll
is not zero the comparison is an approximation; what that can cost is bounded by the
rule rather than by the inverse, because the kept part is clamped to `|d|` on its own
axis, so the start always lies on the segment between `cur` and `prev` and the blend is
never larger than before and never in another direction. Two draws keep their own
frame's root on purpose and say so in `CalcLaraMatrices`: the hit reaction, which wears
a frame picked by hand out of a spaz animation, and the first person still body. The HD
path takes the matrices the classic walk builds and needs no line; the randomizer and
`hd_enable` are untouched. `fps_mode 0` is byte-identical: `InterpBeginFrame` returns
before the walk whenever `alpha >= 1.0F`, so `g_rootStep` is never written, and
`InterpRootRefused` and `InterpRootNow` return at their first statement on `!g_applied`.
Seam (`-log`): `INTERPLOG`'s per-second line gains `root_fix=N`, a count of the refused
tween frames whose drawn root the rule really moved (frames whose two ticks name the
same animation and frame blend to the offset they already carry and are not counted),
and beside it, at most four lines a second and for Lara alone, `INTERPLOG root lara anim
A->B frame F->G offset_prev=(x,y,z) offset_cur=(x,y,z) drawn=(x,y,z) pos_y=N
step=(x,y,z) kept=(x,y,z)`; a transition the rule leaves at `cur` prints no line.
Measured with `Preparation/Tools/Diagnose/RootProbe.cpp`, which links
`game/interpolate_math.h` itself so that tool and engine run the same function: over
Lara's own 474 animations, every wrap and every transition with an offset step of 8
units or more on any axis, ANDREA1 has 433 such transitions (4 paid in full by a
position step, 27 in part, 402 not at all) and JOBY5 352 (4, 26, 322); the invariant -
no row where the narrowed draw leaves the segment from the frame's own offset to the old
draw - is counted by the tool and reads 0 of 433 and 0 of 352.

Across the jump twists the previous tick drew the frame an early `turn180_effect` fires
on, turned by half a turn (*Lara facing the wrong way for one tick in the jump twists*),
so the change that follows blends from that offset turned. RootProbe models it: `readTurn`
and `drawnTurned` ask the engine's own `InterpTurnKind`, `-sweep` turns the offset of such
a frame, and `-grab` turns the previous offset where the leaving frame is drawn turned or
the entering frame fires the effect, as `InterpRootNow` does, and takes the whole step
(`-grab <a> <b> <y> <x> <z>`). Over ANDREA1 the sweep changes on the four wraps out of 48,
207, 210 and 212 and nowhere else (434 transitions, 403 not paid at all: 207 into 209
enters the table with an offset step of (8,-7,58), 212 into 213 goes from (-101,-19,0) to
(-13,-19,-14), 210 into 211 from (-153,0,0) to (-15,0,-14), 48 into 11 from (6,-18,0) to
(-22,-18,-14)); JOBY5 reads 353 transitions, 323 not paid at all; the invariant still
reads 0. With the back twist's step as the owner's
log gives it after the effect ((-1,-22,50) in her new frame: 44 units of fall and 50
forward on the tick 213 hands over to 77, 66 less fall eleven ticks earlier), 212 into
213 keeps the 14 units on z and draws x and y at the new frame's offset, 13 and 19 units
early at most. With steps worked out from the jumps' own JumpVelocity commands (fall
-100, speed 75 for the running jump, speed 50 for the standing jump) and six units of
gravity a tick, not measured in play: (0,2,-75) keeps (0,2,58) of 207 into 209 and draws
(8,5,0) early, and (0,-34,-50) keeps nothing of 210 into 211, (15,0,14) early. These are
the root's share of the one thirty hertz step every refused animation
change draws, while the pose on those frames is the new animation's own; the narrowing
is left as it is. The roll's change 146 into 147 draws exactly as before (`-grab 146 147
0 0 -51` prints the owner's logged line, kept (0,0,5)).
`Preparation/Tools/Diagnose/InterpTest.cpp` gains a `root_narrow` group of 25 checks
(the ladder's whole step kept for every alpha in 1/256 steps, the crawl's nothing kept,
a partial case, same-sign and zero steps, each axis alone, the flip before the
narrowing, `LONG_MIN`/`LONG_MAX` without overflow); `bit.bat` runs 398 checks, 0 failed.
The tools are listed in `Preparation/Tools/README.md`.

**Commits.** `7397d57`, `c676e29`, `ba9d624`, `523e4cc`, `8253932`, `95cb240`,
`dc34dc1`, `8b0c1fc`, `983c834`, `b7607c9`, `67407bf`

### The pose between two ticks
2026-09-07 - 2026-09-08

**What.** A body's limbs move on the in-between frame and not only its position. The
engine's second matrix stack (`IMstack`, `aIFMStack`, the `phd_*_I` functions) already
blended two stored key frames; its blend factor is now one number instead of four
hard-wired branches, and the drawing code can ask for the midpoint between the poses of
two consecutive logic ticks. `InterpKeyAlpha(frac, rate)` in `game/interpolate_math.h`
produces `IM_alpha` (and `IM_alpha8`, the same factor in eighths for the integer stack),
and `aInterpolateMatrix`, `InterpolateMatrix`, `InterpolateArmMatrix` and
`aInterpolateArmMatrix` are each a loop over twelve elements instead of twelve
written-out copies of three constants. `InterpPoseAt` (`game/interpolate_math.h`) is the
midpoint, `GetFramesAt` (`game/draw.cpp`) turns it into a pair of frame pointers, and
`GetFramesNow` is what the draw sites call: `GetFrames` on a frame that carries a tick
and at `fps_mode 0`, the midpoint on the frame between two ticks. The blend is vanilla's
own element-wise lerp of two 3x4 matrices - no angle is interpolated anywhere in the
pose path. Animations of rate 1 - 3619 of the game's animations, which never
interpolated at all before - now interpolate, except that `InterpPoseFar` refuses a rate
1 key frame pair whose two poses turn more than ninety degrees at any node,
`GetFramesAt` then returning 0 so that the frame is drawn exactly as thirty hertz draws
it. At rates above 1 a key pair of Lara's where some node turns more than ninety degrees
in the drawn frame (`InterpKeyFarOf`; the hips alone, `InterpKeyFar`, until *Every node,
in the drawn frame*), the pair vanilla's own key-frame blend collapses her on, is drawn from
a key frame built at the weight in hand, the tween's weight included; only where the
pair is held on its first key (the tightrope turn) does `GetFramesAt` refuse the tween
(see *Lara squeezed to a sliver in the tightrope turn*).

**Why.** Both poses a tick apart lie on the same key frame pair, so their midpoint does
too and no third matrix stack is needed: within an interval the midpoint is `lerp(K, K',
(ap + ac) / 2)`, and where the pose crosses a key frame it is `lerp(K, K', (ap + 1) /
2)`. The factor stays vanilla's own classification rather than the arithmetically
correct `frac / rate`: walking the animation tables of all sixteen retail level files
shows rates 1, 2, 3, 4, 5, 6, 9 and 10 in use (3619, 3350, 35, 5399, 3187, 2, 2 and 120
animations), and at rate 5 vanilla draws the four frames of an interval at 0.25, 0.75,
0.75, 0.75 - over three thousand animations would move differently from the day the game
shipped, which is a different game and not a smoother one. The replacement of the
branches had to be exact so that `fps_mode 0` still produces the frame the build
produced before: the float stack keeps vanilla's three expressions rather than an
algebraic rearrangement of them, because `(a + b) * 0.5f` and `a + (b - a) * 0.5f` can
differ in the last bit in IEEE floats, and the integer stack keeps vanilla's shifts,
because `(-3) >> 2` is -1 where `(long)(-3 * 0.25f)` is 0 - the factor is applied as `a
+ ((d * alpha8) >> 3)` below a half and `b - ((d * (8 - alpha8)) >> 3)` above it, which
is `a + (d >> 1)`, `a + (d >> 2)` and `b - (d >> 2)` for the three vanilla factors,
negative values included. An element-wise lerp of two rotations always lands on the
short-way bisector, so the packed-angle wrap cannot happen (1000 to 20 gives 1022 on all
three axes and in both directions), and over ANDREA1's 319 stored Lara animations no
adjacent key frame pair has a per-axis packed difference over 512 after the wrap. What
the blend does instead is shorten: it walks the chord of the arc, so a bone whose frame
turns D comes out `cos(D/2)` of its length - 13 % gone at 60 degrees, 29 % at 90, and at
180 the plane across the axis collapses, the bone's frame is singular and whatever hangs
off it lands on neither pose. Vanilla blends only when the rate is above one; adding the
rate 1 animations adds ANDREA1's 23, of which twenty-two turn at most 52.7 degrees
between two adjacent frames at every one of the fifteen nodes (a shrink of a tenth)
while the twenty-third turns her root 179.9 degrees in one game frame (animation 205,
node 0, key 0 to key 1). Ninety degrees is the last turn whose midpoint keeps more than
seventy per cent of the frame, and over ANDREA1 the rule refuses animation 205 and
nothing else; for every other rate the tween frame lands inside the same segment the two
ticks are blended on, so refusing it would itself be a jump. An offline replay of the
whole LARA table at sixty (the 337 animations ANDREA1 really stores of 474 entries,
every drawn frame, all fifteen nodes) reads a worst distance of a tween frame from the
midpoint of its two ticks of 0.00 - the blend is affine in its factor, so a limb cannot
come out beyond the two poses it stands between - and a worst shortening over the rolls
and the sprint dive of 0.89.

**How.** Three feed points. `DrawAnimatingItem` (`game/draw.cpp`) for every enemy, door,
machine and pickup takes `GetFramesNow` and `InitInterpolateAlpha`, and its `if (frac)`
branch is now entered on in-between frames for animations that never entered it.
`CalcLaraMatrices` (`game/delstuff.cpp`) carries Lara, and with her the HD composite,
the classic body and the mirror, all of which read `lara_matrices`:
`Rich_CalcLaraMatrices_Interpolated` takes a float alpha in place of `frac` and `rate`.
Lara's nine aim angles are blended by the root snapshot instead, being per-tick values
from the player rather than from the animation, and the four nodes her own animation
does not drive have a rule of their own (the next entry). The blend is refused when
the item's root snapped (`InterpPoseTime`, `game/interpolate.cpp`), when the animation
number changed between the two ticks (`GetFramesAt`, `game/draw.cpp`), or when the
frame number did not step by exactly one (`InterpPoseAt`, `game/interpolate_math.h`: a
loop wrapping, a jump, a frame written by hand, an item standing still); that frame is
then drawn with the current tick's pose, which is exactly what thirty hertz shows, for a
sixtieth of a second. The one exception to the animation-number rule is Lara's change
after an early half turn (the tick after the last frame of 48, 207, 210 and 212): its
tween frames are drawn from a bridge between the two poses and counted as poses, not in
`pose_skips` (see *Lara facing the wrong way for one tick in the jump twists*, *The
change after it, bridged*). The one exception to the frame-step rule is the ledge hang's
lowering, whose backwards steps `InterpPoseBack` blends (see *Ledge hang: an automatic
turn and lowering at an edge*). The snap verdict is recorded in `InterpBeginFrame`'s own
item walk, one line before it writes the blended position over the live one, because
asked later from the draw the same test would measure half the distance and a teleport
could slip through. The animation's shortened last interval - `GetFrames` reduces the
rate there, comparing a relative frame against an absolute `frame_end`, Core's own
mismatch, reproduced rather than repaired - is followed by `InterpKeyRate`.
`InterpPoseFar` (`game/interpolate_math.h`) walks the two packed rotation streams in
lockstep, builds each node's frame in `phd_RotYXZ`'s own order and compares them by
`trace(A^T B) = 1 + 2 cos`; `GetFramesAt` asks it for `rate == 1`.
`InterpNoteLaraStretch` (`game/interpolate.cpp`, declared in `game/interpolate.h`,
called from `game/delstuff.cpp` on every tween frame of the view set) measures each of
Lara's bones against its own length in the skeleton and keeps the worst of the second
with its node, animation and frame (`INTERPLOG stretch=0.87@node3(anim232 f5843)`; a
rigid pose reads 1.00). `HairControl` needed nothing: it is called from inside
`ControlPhase`'s tick loop (`game/control.cpp`) and `DrawHair` (`game/hair.cpp`) builds
its matrices from the already blended positions. The HD path followed with no change to
`specific/hd/`, `HDObjectNode`'s `interpolated` branch simply being entered on
in-between frames as well. `game/sphere.cpp` is touched so that the pose does not reach
it: `InitInterpolate2` serves the half of the second stack that answers where a joint is
rather than what it looks like - `GetJointAbsPosition`, `GetJointAbsPositionMatrix` and
the creature collision spheres - and now takes its factor from
`InterpKeyAlpha`/`InterpEighths` like its neighbour, but is fed `frac` and `rate` from
the logic tick and never a midpoint in time, so a sphere a collision test asks about
stands where that tick put it whatever the picture rate. Deliberately left on vanilla's
own pair: `GetBoundsAccurate` and `GetBestFrame`, which produce bounding boxes for clip
windows, shadows and lighting - the same approximation vanilla already makes on every
interpolated tick frame. `INTERPLOG` carries `pose_items`, `pose_skips`, `pose_far`,
`pose_hd` (Lara's own HD body as well as HD items), `lara_pose` and `stretch=`,
documented in `docs/Engine-Manual.md`. Checked by
`Preparation/Tools/Diagnose/InterpTest.cpp` (`pose_wrap`, and the pose factor against
the four hard-wired branches it replaced over 441 matrix element pairs for every `(frac,
rate)` the retail levels can produce) and measured with
`Preparation/Tools/Diagnose/ModelProbe.cpp` (`-poserisk`, `-tween60`). `InterpPoseFar`
is reached only through `GetFramesNow` inside `if (t > 0)`, so `fps_mode 0` reaches none
of it.

**The stretch reading's matrices** (2026-09-25). *What.* `stretch=` read the wrong pair of
joints for nodes 8 to 14. `InterpNoteLaraStretch` walks the bone table in mesh tree order
(`LM_`, the head last), and `lara_matrices` is in draw order (`LMX_`, the head ninth), and
the node number went straight into the matrix array: node 14 compared the torso with the
left hand, node 9 the head with the right shoulder. `stretch=0.42@node14(anim0 f5)` in the
owner's log is the torso-to-left-hand distance over the head bone's length on animation 0,
where the head bone is whole. Now every node's matrix, and its parent's, is read through
`lara_lmx_of_node`. Log only: the drawn picture is the same by construction.

*How.* The table moves out of `hd_laradraw.cpp` (`kLmxOfNode`) into `game/delstuff.h:32`,
beside `lara_matrices`, as `constexpr int lara_lmx_of_node[15]` written with the enum
names; the HD seam's three reads use it with the same numbers (`hd_laradraw.cpp:878`,
`:924`, `:948`, and a `static_assert` that it has a matrix for every node of the
composite), and `InterpNoteLaraStretch` reads `matrices + lara_lmx_of_node[...] * 12` for
the node and its parent (`game/interpolate.cpp:1883`), its walk bounded by the table's
fifteen entries, Lara's own mesh count. The legend of `INTERPLOG` in
`game/interpolate.cpp` and the comment in `game/interpolate.h` say which node is counted.
The table is written out and not derived; a probe outside the tree reads it,
`lara_mesh_sweetness_table` (`specific/drawlara.cpp`, the draw order) and both enums out
of the source and finds it a permutation and the draw order's inverse - LM 14 is LMX 8,
LM 8..13 are LMX 9..14, the rest unchanged.

*Checked.* A second probe builds Lara's matrices out of JOBY2's own frames and bone table
the way `Rich_CalcLaraMatrices` builds them, blends two keys element by element as a tween
does, and walks them both ways: animation 0 frame 5 reads 0.42 at node 14 the old way
and 1.00 through the map (0.99 on the tween at a half, at node 10); 86 frame 1309 0.49
and, at a quarter, 0.92 (a real shortening, at node 13); 6 frame 109 0.44 and 0.99; 108
frame 1623 0.84 at node 9 and 0.99; the idle 103 frame 1530 0.68 and 1.00. Readings quoted
in this record at nodes 8 to 14 named the wrong bone: the tightrope's `stretch=0.12` at
node 13 (ANDREA1, 451 frame 8915) reads 0.12 at node 12 through the map as well, the left
upper arm, and the 0.84 of every other second was head against shoulder and reads 1.00.

**Commits.** `2628a7e`, `a1dbf32`, `db696bf`, `b643607`, `b7607c9`, `afae389`, `0f22873`

### The arms between two ticks
2026-09-08 - 2026-09-09

**What.** The four of Lara's fifteen nodes that her own animation does not drive are
blended in time like the rest of her. With a weapon in her hands the two arms are driven
by the weapon slot's animation through `lara.right_arm` / `lara.left_arm`, and with the
HK in states 0 to 4 the torso and the head are read from it as well. `InterpArmPrev`
keeps each arm's animation number, frame number and `frame_base` pointer at the tick and
hands the previous one back with the frame's time alpha. It answers false - and the arms
are drawn as before - at `fps_mode 0`, on a frame that carries a tick, on a frame drawn
live, with no previous tick, when the arm's animation changed, when its `frame_base`
moved, or when its frame number stepped by more than one in either direction, because a
pistol arm counts down as well as up. It is not first-person only: `InterpArmPrev` asks
`g_applied` and not the camera, so the same four nodes interpolate in third person too.
Both arm chains descend from the same root, the torso the drawn body has.

**Why.** The arm chain indexes the frame data by the frame number and not by the key, so
it reads one stored pose per game frame however the animation's rate is declared
(ANDREA1's weapon slots are not all rate 1: `HK_ANIM` has 13 animations at rate 1, two
at 2 and fifteen at 4). Writing those frames raw left the hands and the weapon stepping
on the logic tick while the background moved every frame: over a still first person
frame at 60 Hz the background moves 1.87 and 1.88 units a frame while the hand and the
HK move 0.7 to 1.2 for five frames and then 3.2 to 3.6 on the sixth. At thirty hertz it
is invisible. The refusals are the cases where the two frames are not two poses of one
continuous movement. `g_prevArm` must be invalidated with the rest of the previous state
because its `base[]` is a `short*` into the level's frame data - memory `FreeLevel`
returns to the `game_malloc` pool and the next level allocates over. The collapse of the
two stacks had to move up to the torso: done inside the right arm's own
`phd_PushMatrix_I` it left the left arm pushed off the torso level with stack one on key
frame A and stack two on key frame B and blended at the factor the collapse had left
behind - the arm's alpha, which nothing set back. For a rate 1 animation the body's
factor and the arm's alpha are the same number and the error is zero; for every other
rate the left shoulder sat at `lerp(A, B, alpha)` while the drawn torso sat at `lerp(A,
B, blend)`, so the arm drifted off the body through the tick and snapped back on the
next - a visible shake of the rifles at any rate above thirty. Measured over 115 ticks
at five to six frames per tick, the hand taken relative to the hips so that the camera
and the run fall out: 41 ticks (36 %) with a left-hand sawtooth against 2 (2 %) on the
right; in animation 90 at rate 2, with the shotgun idle in hand, the left hand moved 13
to 16 units on each of five tween frames, 41 in all, and jumped 80 units back on the
first frame of the next tick, while the right moved 5 a frame and never jumped, the
body's factor reading 0.086, 0.165, 0.258, 0.359, 0.438 against the frame alphas 0.172,
0.331, 0.515, 0.718, 0.876 - half, because the animation is rate 2.

**How.** `InterpArmPrev`, `InterpBlendFloat` and `g_prevArm` (cleared in
`InterpInvalidate` beside `g_prevAim`) in `game/interpolate.cpp` and
`game/interpolate.h`; `InterpArmStack` in `game/draw.cpp` and `game/draw.h`; the six
weapon branches of `Rich_CalcLaraMatrices_Interpolated` in `game/delstuff.cpp`. The draw
takes one of two shapes: the pistol, uzi and revolver branches walk the three arm nodes
twice from the same shoulder - once with the previous tick's frame into a scratch of
three matrices, once with this tick's into the output - and blend them with
`InterpBlendFloat`; the shotgun, HK and crossbow branch already holds both stacks in the
weapon's own animation, so `InterpArmStack` collapses them to their blend at the body's
factor on the torso level, before either arm is pushed, and sets the arm's factor, after
which stack one walks the previous tick's frame and stack two this tick's. For the right
arm the move of the collapse changes nothing, because a translation applied to both
stacks alike commutes with their blend. With the HK in states 0 to 4, where the torso is
collapsed once already, the call takes the torso to its midpoint in time and both arm
chains hang off that one joint. `Rich_CalcLaraMatrices_Normal` is untouched and tick
frames and `fps_mode 0` never enter this branch's blend (`InterpArmPrev` answers false
there), so thirty hertz is byte for byte what it was. Seams: `INTERPLOG` carries
`arms=<blended>/<refused>/<tween frames>`, counted in the draw where the weapon is
known, so empty hands, a flare and a torch read nought. `ARMLOG` (behind `-log`) writes
one line per drawn frame while a weapon is in Lara's hands or on the way there or back
(`LG_READY`, `LG_DRAW_GUNS`, `LG_UNDRAW_GUNS`, `LG_FLARE`), written right after
`CalcLaraMatrices(0)` in `game/draw.cpp` so that it reads the view set `lara_matrices` -
the table the HD body is skinned from (`specific/hd/hd_laradraw.cpp`) and the classic
body drawn from. Three calls in the draw's own order (`game/interpolate.cpp`, declared
in `game/interpolate.h`): `InterpArmLogPath` notes which path built her
(`GetFramesNow`'s `frac`, the body's blend, the held first person body, whether the HD
body is on), `InterpArmLogNote` notes what was done with the arms (the weapon type, both
answers of `InterpArmPrev`, the arms' alphas and the previous tick's frames), and
`InterpArmLogFrame` writes the line; a frame built by `Rich_CalcLaraMatrices_Normal`
never reaches the second call, which the line shows as `ok=-1/-1`, and empty hands write
nothing. The line carries its own serial, `GlobalCounter` (so that the frames of one
tick can be grouped), whether the frame is a tween and at what alpha, the body path and
factor, Lara's animation, frame and state, the weapon type and `gun_status`, the weapon
item's animation, frame and state, each arm's animation, frame and the previous tick's
frame the blend used, `InterpArmPrev`'s two answers and the arms' alphas, the aim angles
for both arms plus torso and head, and the view-space positions of the hips and of
shoulder, elbow and hand of each arm, with the right hand matrix's first column for a
hand that turns rather than moves; the hand taken relative to the hips is what the arm
does, because the camera and the hips move smoothly. Without `-log` the three functions
return at their first line; at `fps_mode 0` the line is still written (`tween=0`,
`a=1.000`) and is the reference a sixty run is held against. Measured with
`Preparation/Tools/Diagnose/AnimMap` over ANDREA1's six weapon slots.

**Commits.** `db696bf`, `92b27b2`, `0ffabbe`

### The camera on the in-between frame
2026-09-07 - 2026-09-09

**What.** The eye, the target and the roll are blended with the rest of the snapshot,
and the whole view is rebuilt for the in-between frame: the interpolator calls
`phd_LookAt` again with the moved eye, and a second time in `InterpEndFrame` with the
live one. `camera.pos.room_number` is never written; the room walk of a blended frame is
seeded instead with the room that contains the blended eye (`InterpEyeRoom`). The frame
is drawn live from end to end when `camera.fixed_camera` changed, when a change of
`camera.type` or `camera.old_type` leaves the smooth family (chase, look and combat,
which hand over to each other), or when the eye or the target jumped more than 1024
units, the target test being scale-aware.

**Why.** Moving `camera.pos` does nothing on its own: the world-to-view matrix,
`CamPos`, `CamRot`, `D3DMView` and the device's own view transform are built by
`phd_LookAt`, once per logic tick, at the end of whichever camera routine ran. The
second rebuild with the live eye keeps the tween frame's matrix from being handed to the
next frame, which would leave the world half a step ahead of the camera looking at it on
every second frame. `old_type` is tested beside `type` because `CalculateCamera` resets
`type` to `CHASE_CAMERA` at its own end unless a heavy camera's timer is running, so
`old_type` is what the view was actually built as. The roll has to travel with the eye
because three cameras hand `phd_LookAt` a roll that is not zero - first person rolls
with the head bone, a flyby rolls along its track, a cut scene rolls with the scene -
and rebuilding the view without it would snap a first-person frame upright and back on
every other frame. The room is not interpolated because it drives `DrawRooms`' whole
portal walk and every clip window; but seeding that walk with the logic room while the
eye has crossed a portal plane between two ticks makes `GetRoomBounds` refuse the portal
(`x ^ p->normal[0]` flips sign) and `SetRoomBounds` drop it a second way (`tooNear ==
4`), so the room behind it is missing for one frame with the sky in the hole and the
hole's edge on the portal's own line. The aim test is scale-aware because the first
person camera has to express a look direction through a target position, the engine's
camera having no field for one: it puts the target 0x5100 (20736) units down the line of
sight, where one degree of turn moves the point 362 units, so an absolute threshold of
1024 read any turn past about three degrees between two ticks as a cut - ten to
seventeen false cuts a second in a quiet room, an idle animation turning her head by
more than that. And a type change is not by itself a cut because `game/larafire.cpp`
sets `COMBAT_CAMERA` while the guns are up while `CalculateCamera` (`game/camera.cpp`)
resets the type to chase at its own end, so `camera.old_type` moves chase - combat -
chase around every draw and every holster, bursts of five or six type changes a second,
which the engine itself hands over smoothly (the eye walks to the new camera's place at
`camera.speed` and the target is Lara either way, which is why the eye test never
fired): taking each for a cut drew that tick live and the next tween frame half a tick
back, so the picture went forward, back, forward at every weapon draw. A cut makes the
whole frame live because a cut that left the camera live and the world at its halfway
values moved the entire level against the eye - in first person, where the eye hangs in
Lara's head, that produced copies of her body, moving room seams and a near zone
measuring a distance the picture did not have; it costs one frame of thirty hertz at the
one moment the picture jumps anyway.

**How.** `phd_LookAt` records the roll it was handed in `CamRoll`
(`specific/3dmath.cpp`), the one place that catches all three cameras; in first person
the value is read through the camera's own getter `GetFirstPersonRoll()` instead,
because `fpRoll` is private to `game/camera.cpp`. It is blended the short way round like
any other angle. `phd_GenerateW2V`, which `phd_LookAt` calls, carries game state inside
it - `lara.poisoned += (lara.dpoisoned - lara.poisoned) >> 4`, an exponential ramp
written when there was exactly one `phd_LookAt` per logic tick - so the ramp is guarded
at its source in `aGenerateW2V` and `phd_GenerateW2V`, which covers every caller
including `S_DrawPickup` (`specific/output.cpp`), whose own `phd_LookAt` runs once per
drawn frame while a pickup icon is on screen; Core's arithmetic is untouched, including
the fact that `phd_GenerateW2V` ends by calling `aGenerateW2V` and so steps the ramp
twice per `phd_LookAt`. `InterpEyeRoom` (`game/interpolate.h`, `game/interpolate.cpp`)
answers with the room that contains the blended eye - `GetFloor` from the logic room,
the very call `camera.cpp` makes at every tick - and `DrawRooms` (`game/draw.cpp`) seeds
its walk from that; nothing is written back, `camera.pos.room_number` keeps the logic
room and `outside` and `camera_underwater` are still read from it, `g_applied` is the
gate, and the correction lives in the walk both paths share, so it mends the classic
picture as well as the HD one. `InterpCamAimCut`, `InterpCamAim`,
`INTERP_CAM_FAR_TARGET` (4096) and `INTERP_CAM_DIR_RADIUS` (1024) in
`game/interpolate_math.h`: beyond the far-target distance a target is treated as a
direction - pulled back onto a sphere of 1024 units about its own eye, with the
1024-unit threshold applied to the change of direction alone while the eye's own travel
keeps its own test - and the branch is chosen once for the pair of camera states (far if
either length is beyond the shell), both offsets taking the same scale, the radius over
the longer of the two, because choosing per camera put a cliff in the middle of the
rule. At the direction radius the threshold is a turn of 41 to 57 degrees in one
thirtieth of a second, over 1200 degrees a second, while a real cut still measures 1448
at a right angle and 2048 at a half turn; the chase camera is untouched, its target
sitting on Lara about a sector and a half out, under the shell, where the comparison is
the absolute one to the bit. `InterpCamTypeCut` (`game/interpolate_math.h`) lets a
hand-over inside the smooth family through, and `game/interpolate.cpp` `static_assert`s
the three type values the header mirrors against `camera_type`, because the header is
built without the engine. Still a cut: a fixed camera taking over (`FIXED_CAMERA`, or
`fixed_camera` changing), a heavy camera, a cinematic, a spot camera stepping, the
binoculars (where the eye jumps to Lara's head and the eye and aim tests catch it), any
eye jump over `CAM_SNAP`, and any aim swing over the sphere threshold. The cut test runs
at the top of `InterpBeginFrame`, beside the cutscene test, and not after the items,
effects, hair links, particles and aim angles have been blended. The first-person
filters `fpVel`, `fpSpd`, `fpRel` and `fpSmoothSlow[]` are left alone: they are tuned as
per-logic-tick exponentials, `FirstPersonCamera` stays inside `CalculateCamera` at
thirty and sees exactly one sample per tick, and only its output - `camera.pos`,
`camera.target` and `fpRoll` - is interpolated. `INTERPLOG` counts the hand-overs it now
lets through as `handover=` and only the changes that leave the family as `type=`.
Checked by `Preparation/Tools/Diagnose/InterpTest.cpp`: `camera_aim` (the old rule and
the new one side by side over a swept first person camera, 500 cases of turn plus eye
travel, and a chase camera), `camera_shell` (a target walked out through the shell one
unit at a time, 500 steps, and jittered on it for thirty ticks), `camera` (twelve checks
over the type table - the six hand-overs are none, the six leaves are cuts) and a
`portal walk` section of 13 checks on ANDREA1's own portal records. All of it lives past
`InterpBeginFrame`'s `alpha >= 1` return, so `fps_mode 0` reaches none of it. The entry
that follows, "The drawn eye at a corner", keeps the blended eye out of a pillar or a door
jamb the straight blend cuts, and seeds the walk from the room that holds the eye.

**Commits.** `324e404`, `ced7f30`, `45d9130`, `a1dbf32`, `a1267b5`, `0fc3487`

### The drawn eye at a corner
2026-09-27

**What.** On a frame drawn between two logic ticks - `fps_mode` 1 and 2, in both control
schemes - the blended eye is tested before the view is built: `GetFloor` from the logic
room, then `GetHeight` and `GetCeiling`, and the point has to lie between the two. An eye
the logic room refuses is tested from the previous tick's room; if that room holds it, the
eye stays where it is and the portal walk starts in that room. An eye neither room holds
is moved to the nearest open point of the chord between the two tick eyes, with the target
and the roll taken at the same blend factor. When no point of the chord opens, the eye
stays and that frame draws no sky, so what its walk leaves undrawn is the clear colour,
black. A blended eye the logic room holds - nearly every frame - is drawn exactly as
before, and no tick eye moves. A flyby is left out and drawn as before. `-log` gains
`CAMLOG`: a `tick` line per pass of the chase, combat, fixed and look cameras (none during
flybys, binoculars or cutscenes), a `draw` line per frame the rule acted on and a `sum`
line per second.

**Why.** Measured over one ANDREA1 run at 165 Hz, in two recordings of 18.2 s and 17.0 s
matched to the run's log to within 9 ms: eight black flashes in 35.2 s, each one or two
video frames long (one to six drawn frames), four at room 40's east door and four at the
pillars of rooms 3 and 11, every room involved indoors (flags 0x40, no `ROOM_OUTSIDE`) and
so black rather than sky. Both causes lie in the draw. The chord between two open tick
eyes cuts a corner block or a pillar for two or three drawn frames: at log t 136500 the
tick eyes stand at about (42945,-338,31749) in sector (5,3) and (43074,-314,31584) in
(6,2) of room 3, the drawn eye (42957,-336,31734) is 50 and 9 units inside the pillar at
(5,2), `GetFloor` from room 3 follows the pillar's floor data up to room 36, whose floor
is 1456 units above the eye, and the walk draws one room. And the room seed: the first
drawn frames after a tick apply the new tick's room to an eye still beside the old tick
eye - at t 94656 and t 100125 the eye stands in open space of room 40's sector (6,2) while
the logic room is already 225, whose corner sector (0,0) is a wall there; at t 95297 it
stands in the doorway column between rooms 225 and 40, and `GetFloor` from room 38 reads
38's edge sector (1,3), whose door leads up to room 37. The logic camera is not the cause:
`MoveCamera` tests every tick eye against its room and pulls or pins it in
(`game/camera.cpp:2789`-`2849`), and the drawn eyes `ROOMLIST` logs, replayed through a
`GetFloor` emulator over the run's own level, reproduce the logged walk room on all 1287
ANDREA1 lines and fail the test on 26 eyes in 18 episodes over 126 s of play - a lower
bound, since `ROOMLIST` writes only frames whose room list changed.

**How.** `ModernCameraChordClamp` (`game/modern_camera.cpp:302`) is pure: from the blended
point it scans the chord outward on both sides at once in steps of a thirty-second of its
length (`MODERN_CAMERA_CHORD_SCAN`, `game/modern_camera.h:315`), tests an end itself when
a scan reaches it, halves each bracket it found twelve times toward its open end
(`MODERN_CAMERA_CHORD_BISECT`, `modern_camera.cpp:367`, `:377`) and returns the nearer
answer - the newer tick eye's side on a tie - or none. It builds its points with
`InterpLerpLong`, the interpolator's own blend, so a point it tests is the point drawn.
`ModernCameraDrawnEye` (`:427`) is the order: the logic room, the previous tick's room
(`:468`), the chord (`:474`), none (`:478`); a chord answer equal to a tick eye is
reported as `tick` (`:497`). `CameraEyeTest` (`game/camera.cpp:1062`) is the engine's
test, the five terms of `ModernCameraEyeInRoom`; it saves `trigger_index`, `tiltxoff`,
`tiltyoff`, `OnObject` and `height_type` before `GetFloor`, `GetHeight` and `GetCeiling`
and writes them back after (`:1065`-`1090`). `InterpBeginFrame` calls the rule after the
blend (`game/interpolate.cpp:1617`) unless either tick is a flyby (`:1608`; entering or
leaving one is a cut, `:656`), moves the eye and the target to the answer for `chord` and
`tick`, rebuilds the view with the roll at the same factor (`:1647`) and raises
`g_skyWithheld` for `nosky` (`:1632`); `InterpEyeRoom` returns the rule's room for
`prevroom`, `chord` and `tick` (`:1149`); `ClearEyeFix` clears all of it at the top of
every `InterpBeginFrame` (`:1305`) and in `InterpEndFrame` (`:1749`), which puts the tick
eye back as on every frame (`:1709`). `SkyDrawPhase` reads `InterpSkyWithheld()`
(`game/draw.cpp:550`), leaves out the moon, the HD dome (`HDFrameSky` is called nowhere
else), the flat layers, the horizon and the stars, and returns once the storm's clock has
stepped (`:603`). `nosky` needs both tick eyes to fail as well, since the chord's ends are
among the points searched, so it happens only where the logic camera itself left an eye
that neither tick's room holds; at `fps_mode` 1 the live frame between two such blended
frames still draws that eye with its sky. The seams: `CameraTickLog`
(`game/camera.cpp:1220`, at the end of `CalculateCamera`, `:1707`) with `CameraEyeNear`
(`:1147`) for the distances, fed by `CamTickWant`, `CamTickLos` and `CamTickIdeal` in
`ChaseCamera` and `CombatCamera` (`:2372`, `:2471`, `:2496`, `:2604`, `:2677`, `:2699`),
which store only behind `gLogEnabled`; `InterpLogEyeFix` (`game/interpolate.cpp:1189`),
called by `DrawRooms` after the walk and the sky (`game/draw.cpp:2315`); the sum in
`LogSecond` (`game/interpolate.cpp:873`). `CAMLOG tick` prints `f=`, `eye=(x,y,z)/room`,
`ideal=(x,y,z)/room` or `-`, `want=(x,y,z)` or `-`, `target=(x,y,z)/room`, `los=` 1, 0 or
-1, `los_lp=` 0 to 8 or -1, `pull=` 8 to 1, 0 or -1, `lead=yaw,pitch`, `mc_pull=`,
`snaps=`, `kind=` (`open`, `wall`, `door->room`, `below:units`, `above:units`), `face=`
and `corner=` (units, -1 for none). `CAMLOG draw` prints `frame=` (`ROOMLIST`'s serial),
`tick=`, `alpha=`, `eye=(x,y,z)`, `prev=(x,y,z)/room`, `cur=(x,y,z)/room`, `kind=`, `fix=`
(`prevroom`, `chord`, `tick` or `nosky`), `alpha2=`, `moved=`, `walk_room=`, `rooms=`,
`outside_logic=` and `sky=`. `CAMLOG sum` prints `flagged=`, `prevroom=`, `chord=`,
`tick=`, `nosky=` and `max_moved=`.

**Checked.** Before the engine was touched, the rule as designed was replayed over the
run's log with the measurement's `GetFloor` emulator and the tick eyes it reconstructed
(kept outside the repository). All eight events resolve: the five wrong-room frames at t
94656 to 94672, 95297 to 95312 and 100125 by the previous tick's room with the eye
unmoved; the rest by the chord - frame 14206 at t 95312 by 6 units, t 96594 by 2 and 26, t
136500 by 13, t 137641 by 35, t 128156 by 116 and t 140625 by 574 and 456, the last two
that far because the reconstructed older tick eye stands in the pillar itself
(reconstructed at a phase of 0.18 of a tick instead of 0.09, it is open and the moves are
28, and 72 and 190). The moves match the measurement's own chord table to within one unit.
All 1261 logged eyes the logic room holds keep their logged walk room. Of the 13 other
failing frames in the log, 6 are mended by the previous room and 2 by the chord, and 5
have one logged eye in their tick and no chord to replay. After the change, a probe
linking the committed `game/modern_camera.cpp` ran `ModernCameraDrawnEye` over the same
frames with the emulator answering each test: the same fix, eye and walk room as the
replay on all 21 replayed failing frames, `none` with the logged eye and walk room on all
1261 others, at most 47 tests for one frame. The run's flyby lies in ANDREA1's first
second (`INTERPLOG` at t 19031, `spot=7`, and `spot=0` in every later second), before the
first failing frame at t 21469, so leaving flybys out changes no replayed frame; no suite
reaches that guard, since only the engine compiles `game/interpolate.cpp`. ModernTest's
`camera_chord` holds the rule on the niche map's 2x2 pillar
(`Preparation/Tools/Diagnose/ModernTest.cpp`), 19 checks: eight chords past its four
corners at every 64th of the chord, each answer open, on the chord and the nearest open
point against a brute force at 1/1024 of it, inside the test budget;
an open chord unchanged after one test; a chord solid from end to end without an answer;
solid ends with an open middle; one end solid; and the order over two rooms - none,
prevroom, chord, tick, nosky. ModernTest 10827 checks, 0 failed (10808 before and 19 new);
MenuTest 35547, InterpTest 635, LipsTest 270 with the `.OBJ` folder and 243 without, 0
failed - as before. InterpTest compiles no engine file (`bit.bat` builds `InterpTest.cpp`
alone against `game/interpolate_math.h`), so the room order and the sky flag are held in
ModernTest through `ModernCameraDrawnEye`, the function the engine calls. The globals, by
the code: `GetFloor` writes only the room through its pointer, here a local
(`game/control.cpp:1253`-`1340`); `GetCeiling` writes no global, and the ceiling callbacks
it runs write only the height they are handed (`game/objects.cpp:186`, `:220`, `:244`;
`game/traps.cpp:1271`, `:1611`); `GetHeight` writes the five
(`game/control.cpp:1049`-`1052`, `:1068`, `:1095`-`1101`, `:1119`, `:1145`, `:1153`,
`:1170`, `:1224`-`1228`) and runs floor callbacks that write only `height_type`,
`OnObject` and the height (`game/objects.cpp:176`, `:206`, `:230`; `game/traps.cpp:1253`,
`:1598`) - no item, no random number, nothing a savegame holds. Not measured: the game.

**Unchanged.** `fps_mode 0`: `InterpAlphaNow` answers 1 there
(`game/interpolate.cpp:1086`-`1087`) and `InterpBeginFrame` returns at `alpha >= 1`
(`:1328`) before the camera block, the only caller of `ModernCameraDrawnEye`; the rule's
state is cleared on every frame (`:1305`), so `InterpEyeRoom` and `SkyDrawPhase` find
nothing to do, and the `CAMLOG` lines are written only with `-log`. The logic camera of
both schemes: `MoveCamera`, `mgLOS`, `ModernCameraRayBounds` and the orbit are not edited,
and `ChaseCamera` and `CombatCamera` gained three calls each that store what `CAMLOG`
prints and return at once without `-log`. `camera.pos.room_number`; the eye and the target
the logic holds, since the frame's moved copies go back in `InterpEndFrame`; the logic
room's `outside` and `camera_underwater`. Every log line already there prints as before. A
frame whose blended eye the logic room holds. A flyby: `InterpBeginFrame` leaves the rule
out while either tick is one (`game/interpolate.cpp:1608`), so a flyby frame is drawn with
the plain blend, its room seed and its sky as before.

**Decisions.** The rule acts on the drawn eye alone: no tick eye moves, the tick eye keeps
no clearance margin and `mgLOS` gets no diagonal corner test - that test mends none of the
eight events and would change the camera at every pillar, and a margin waits until
`CAMLOG`'s `face=` and `corner=` have counted how often a tick eye stands that close to a
face or a corner. A frame whose eye neither tick's room holds shows the nearest open point
on the chord between its two tick eyes and never a position of its own. The fallback
colour is the clear colour, black. Both control schemes take the rule, because the blend
is the interpolator's and not a scheme's, and `fps_mode` 1 and 2 alike. Flybys are left
out: `CalculateSpotCams` sets a flyby's eye straight from its spline
(`game/spotcam.cpp:458`-`483`) and `MoveCamera` never tests it, so a flyby passing outside
every room would fail at both ends of every chord and, under the rule, lose its sky or
stall at the tick rate; a flyby at a corner keeps the plain blend and whatever that draws.
The net can still fire on an eye the logic itself left untested: `MoveCamera` keeps its
ideal when the ceiling meets the floor height or a height is `NO_HEIGHT`
(`game/camera.cpp:2837`-`2843`), and a fixed camera or the first person head eye is never
room-tested; an outdoor stretch of that kind alternates sky and black at the tick rate,
and `CAMLOG`'s `nosky=` count shows it. The see-through and wall-fill frames of the near
plane are a separate matter. The margin that `CAMLOG`'s counts asked for is the entry
*The Modern eye clear of a wall*.

**Commits.** `d79350d`, `4f2fde6`

### The Modern eye clear of a wall
2026-09-28

**What.** In the Modern scheme the orbit's tick eye is moved square off every closed axis
face of its sector that stands nearer than the reach of the near plane's visible
rectangle, to exactly that reach, at the end of `MoveCamera`: after its own `mgLOS`
pull-in and `CameraSnaps`, before its floor and ceiling clamp. The reach comes from the
projection the frame uses, 33 units at 16:9 and 39 at 21:9. Only x, z and the eye's room
change, each axis by at most the reach. `-log`'s `CAMLOG tick` line ends in `clear=`: the
units moved, 0 when there was nothing to do or the pass ran no Modern orbit, -1 when the
moved point opened in no room and the eye stayed. The rule belongs to the logic camera, so
`fps_mode` 0, 1 and 2 change alike.

**Why.** The near plane stands 20 units in front of the eye
(`InitWindow(0, 0, w, h, 20, 20480, 80, ...)`, `specific/gamemain.cpp:260`; `phd_znear`
holds it shifted up by `W2V_SHIFT`, `specific/3dmath.cpp:796`, and the HD renderer clips
at the same `f_mznear`, `specific/hd/hd_level.cpp:638` and `:1248`). `AlterFOV` sets the
focal length `phd_persp = (winh * 320 / 240 / 2) * cos(fov / 2) / sin(fov / 2)`
(`specific/3dmath.cpp:139`-`140`, `GAME_FOV` 80 degrees, `global/types.h:55`), and a
view-space point is on screen while `|x| <= z * (W / 2) / persp` and
`|y| <= z * (H / 2) / persp`. The corners of the rectangle the screen shows at z = 20
therefore lie `R = sqrt(20^2 + (20 * (W / 2) / persp)^2 + (20 * (H / 2) / persp)^2)` from
the eye: at 1920x1080 persp is 858 and R 32.5, at 2560x1080 R is 38.1, at 2560x1440 persp
is 1144 and R 32.5 again. A closed face nearer than R can cross that rectangle at some
view angle, and whatever lies behind it shows at the screen's edge; a face at R or more
never can. Nothing kept the eye that far off. When no eighth of the ray clears the four
probes of `ModernCameraRayBounds` at 384 units - her back to a wall - the `mgLOS` point
stands (`game/camera.cpp:2125`-`2136`), and `mgLOS` (`:1801`) steps one sample back from
the first one in a wall, so that point lies anywhere in the last eighth before the face,
down to 0 units from it. `MoveCamera` smooths the eye towards it by 1/speed a tick
(`:2858`), runs `mgLOS` again only for an eye outside its floor or ceiling (`:2892`) and
asks no wall. The gate, counted before the engine was touched over the `CAMLOG tick` lines
of two `-log` runs at 2560x1440 (37196 ticks, 30156 and 7040, every one `kind=open`): 2584
ticks stood nearer than 33 units to a closed face and 2812 nearer than 39, 203 of them at
0 or 1 unit; the first run alone had 2321 under 28 units and 202 at 0 or 1. Per level,
under 33 and under 39: ANDREA2 998 and 1060 of 3808 ticks (26.2 %), ANDREA1 1312 and 1416
of 17304 (7.6 %), RICH1 69 and 75 of 789 (8.7 %), RICH2 185 and 230 of 7247 (2.6 %), JOBY2
16 and 23 of 3405, ANDY2 2 and 3 of 1796, ANDY3 0 and 3 of 1645, RICH3 1 and 1, ANDY1 1
and 1, JOBY4 and JOBY5 none. The rooms with the most: ANDREA2 room 78 (341 ticks), ANDREA1
rooms 180 (337) and 0 (252), ANDREA2 rooms 81 (136) and 183 (98). Every near-face tick ran
the Modern ray (`pull=` never -1 among them); of the 2584 under 33, 2394 had `pull=0`, the
fallback, and 22, 31, 40, 28, 23, 10, 10 and 26 had `pull=` 1 to 8. The push the rule
makes, R - face, at R = 33: 1 to 4 units on 170 ticks, 5 to 8 on 159, 9 to 12 on 225, 13
to 16 on 246, 17 to 20 on 386, 21 to 24 on 477, 25 to 28 on 472, 29 to 32 on 315 and 33 on
134; at R = 39 the same ticks and 228 more, and the ticks at face 0 move the full 39.

**How.** `ModernCameraNearRadius` (`game/modern_camera.cpp:646`) is the formula in
doubles, rounded up to a whole unit, 0 without a focal length or a near plane; whole
numbers would leave a long at a 4K window. `ModernCameraWallClearance` (`:663`, declared
under the formula and the rule at `game/modern_camera.h:526`-`560`) is pure and takes its
open test as a callback of the type `ModernCameraDrawnEye` uses (`MODERN_EYE_OPEN`,
`:508`). It answers 0 at once when no edge of the eye's sector lies within the radius;
otherwise it asks the eye itself (not open: 0, untouched), then, for each edge within the
radius, the point one unit past it at the eye's height from the eye's room - the probe
points of `CameraEyeNear` (`game/camera.cpp:1182`), so `face=` and the rule read the same
faces - and moves x to `(x & ~1023) + radius` for a closed west face or to
`(x & ~1023) + 1024 - radius` for a closed east face, z likewise. The moved point is asked
last: open, it answers the units moved, `|dx| + |dz|`, with the room it resolved to;
closed, it answers -1 and nothing moves. A radius outside 1 to 512 answers 0, since two
faces of one axis within it would need a sector narrower than 1024. The design asked all
four probes on every tick; the code asks only the edges within the radius, so nearly every
tick asks nothing. `ModernCameraClearWall` (`game/camera.cpp:2778`) hands it `camera.pos`
with its room, the radius from `phd_znear >> W2V_SHIFT`, `phd_winwidth / 2`,
`phd_winheight / 2` and `phd_persp`, and `ModernCameraWallOpen` (`:2750`), which is
`CameraEyeTest` (`:1097`), and writes x, z and the room back when the eye moved.
`MoveCamera` (`:2800`) reads the flag `modern_cam_clear_wall` (`:280`), which
`ChaseCamera` and `CombatCamera` set to their `modern_orbit` just before they call it
(`:2536`, `:2740`), and clears it at once, so `FixedCamera`, its one other caller, never
runs the rule. It calls the rule after the `CameraSnaps` block (`:2927`), before the floor
and ceiling clamp (`:2930`-`2950`, the same three cases and the same 255 as
`ModernCameraEyeY`), `CheckForRichesIllegalDiagonalWalls` in RICH1 and RICH2 and the final
`GetFloor` into `camera.pos.room_number` (`:2955`), so all of them apply to the pushed
point. The open test runs `GetFloor`, `GetHeight` and `GetCeiling`; `GetHeight` writes
`tiltxoff`, `tiltyoff`, `OnObject`, `height_type` and `trigger_index` and runs floor
callbacks. The rule runs inside `MoveCamera`, in the tick, where the camera already calls
`GetHeight` freely (`mgLOS`, `CameraCollisionBounds`, the clamp that follows), so nothing
needs saving there; `CameraEyeTest` puts the five back all the same, which is harmless.
The pure function calls nothing of the engine. The seam: `cam_tick.clear` (`:273`),
written only behind `-log`, cleared with the rest of `cam_tick` at the top of every pass
(`:1337`) and printed last on the tick line (`:1312`).

**Checked.** The gate above, by a script over the two log copies (kept outside the
repository). ModernTest's `camera_wall_clear`
(`Preparation/Tools/Diagnose/ModernTest.cpp`), 25 checks on the niche map: AlterFOV's
focal length, built from the sine table as `phd_sin` reads it, is 858 at 1080 lines and
1144 at 1440; the radius is 33 for near plane 20, half extents 960 and 540 and focal
length 858, 39 for 1280, 540 and 858, 33 at 2560x1440, and 0 without a focal length or a
near plane. Fifteen eyes 0, 1 and R - 1 units from the pillar's four faces and from two
faces of the outer wall land exactly R off the face, on that axis alone, the answer is R
minus the distance, the room is the moved point's, and one face costs at most three tests.
An eye R or R + 1 units off stays and answers 0; a concave corner moves both axes to R; an
open neighbour is no face; an eye with no edge within R asks nothing; an eye in the pillar
stays after one test; a refused moved point answers -1 without a move; a radius of 0 or
513 moves nothing. A sweep of the 685035 open points of a 7-unit grid, at 33 and at 39
(46052 pushes in all), leaves no closed face nearer than the radius, moves no axis further
than it and moves no eye that had nothing within it. ModernTest 14018 checks, 0 failed
(13992 before and 26 new); MenuTest 42364, InterpTest 683, LipsTest 270 with the `.OBJ`
folder and 243 without, 0 failed, as before. The engine builds with `be.bat`. Not
measured: the game.

**Unchanged.** The classic scheme: the flag is `modern_orbit`, 0 there, and
`CameraCollisionBounds(&ideal, 384, 1)` still resolves its eye (`game/camera.cpp:2523`,
`:2729`). `mgLOS`, `CameraCollisionBounds`, `ModernCameraRayBounds` and the orbit are not
edited; the first person, binocular and fixed cameras, the look camera and the flybys
never set the flag. Nothing under `specific/hd` and nothing in `game/interpolate.cpp`
changed. Savegames: `camera.pos` is not saved (`game/savegame.cpp` writes and reads only
`camera.fixed[i].flags` of the camera, `:292`-`293`, `:749`-`750`). Every log line already
there prints as before; `CAMLOG tick` gained one field at its end. In the Modern scheme
`fps_mode` 0, 1 and 2 are not byte identical to before: the rule moves the tick eye, and
the interpolator blends the moved eyes as it blends any others.

**Decisions.** The radius is computed from the projection the frame uses (`phd_znear`,
`phd_winwidth`, `phd_winheight`, `phd_persp`), never a constant, so it follows the
window's aspect and the field of view. The rule runs for the Modern orbit only; the
classic scheme stays as it is. The push is square to the face and at most the radius,
never along the ray - a wall along the ray would let the eye slide far - and never round
her beyond that. Convex corners are left out: one stood nearer than 33 units on 6 of the
37196 ticks, 3 of them in the first run's 30156. The floor and the ceiling need nothing:
the clamp that follows keeps 255 from both.

**Left.** Convex corners (above). The frame of an earlier recording in which a room
dropped out of the picture is another mechanism, a matter of the portal walk rather than
of the near plane. The first person and binocular cameras, the classic scheme and the
interpolator are outside this rule. The faces are asked at the eye's height before the
floor and ceiling clamp; an eye that the clamp then raises or lowers is not asked again.
The rule has not been seen in the game.

**Commits.** `a69397a`, `ed3a2de`, `1aaf5f5`

### Particles on the same time axis
2026-09-07

**What.** Everything that moves but is not a body is drawn between two logic ticks as
well: the sparks, the fire and smoke sparks, the blood, the drips, the flying shards of
a smashed object, the rain, the snow, the underwater dust and the shockwave's expanding
ring are walked back from the state the game holds, while the gunshells, the rats, the
bats, the spiders, the bubbles and a rope Lara hangs from join the root snapshot.

**Why.** A particle does not need a snapshot, because it already carries what one would
have held: every particle simulation in this engine settles the velocity first and then
moves the position by it, so the stored velocity is the step the last tick took and
`previous position = current position - step` is exact rather than an estimate - a
thousand sparks and two thousand raindrops for a subtraction and no memory at all. The
walk is backwards and never forwards: `cur + vel * alpha` would draw the particle at
tick + alpha, a whole logic step ahead of everything around it - sparks running out in
front of the gun that made them, rain falling through a floor that has not arrived yet -
and it would look perfectly smooth while doing it. The six that joined the snapshot have
a place but no usable step: a gunshell's bounce negates the fall speed and writes the
position back; rats, bats and spiders rewrite both the angle and the speed after the
position moves; a bubble's drift is a sine of a rotation that has already advanced; and
a rope is a constraint solve with no velocity in its drawn segments at all - and Lara
hangs on it, so an uninterpolated rope would leave her hands by half a step on every
second frame.

**How.** `InterpTrailLong(cur, step, alpha) = InterpLerpLong(cur - step, cur, alpha)` in
`game/interpolate_math.h`, written as the snapshot's own lerp of a reconstructed
previous position and not as `cur - step * (1 - alpha)`: the two are the same algebra
and not the same integer, because a C cast truncates towards zero, and the wrong form is
out on 16,384 of the 40,965 step pairs a TR5 particle can carry. Ten draw sites ask
`InterpParticleAlpha()` once before their loop and take no branch at all when it answers
1: `spark[1024]` (`S_DrawSparks`, `Xvel >> 5` plus the outdoor wind), `fire_spark[20]`,
`smoke_spark[32]`, `blood[32]` (`DrawBlood`), `Drips[32]` (`Yvel >> 5` and the wind if
the room is outside), `debris[256]` (`Speed * sin(Dir) >> W2V_SHIFT`, `Yvel >> 4`),
`Rain[1024]` (`xv + 4 * SmokeWindX`, `yv << 3`), `Snow[1024]` (`xv`, `(yv >> 1) & 0xFC`,
and nothing for a settled flake), `uwdust[256]` and `ShockWaves[16]` (the radius:
`Speed` and `Speed >> 1`) - all in `specific/specificfx.cpp`, with `game/tomb4fx.cpp`
and `game/draw.cpp` beside them. Two thirds of the spark array hangs off an item or an
effect and stores an offset from a body the root snapshot has already blended; the walk
back is applied to the finished world position, which is the same number as blending
both halves, and leaves the "let go of the thing you came off" code above it writing the
live position, which is game state. `Gunshells[24]`, `Rats[32]`, `Bats[64]`,
`Spiders[64]`, `Bubbles[40]` and `RopeList[8]` with 24 links each cost 16.7 KB of
statics - 10.1 for the 184 movers with their pointer table, 1.2 for the bubbles, 5.6 for
the ropes, taking the whole snapshot to about 44 KB - and not one line of their own
drawing code: `DrawGunshells`, `DrawRats`, `DrawBats`, `DrawSpiders`, `DrawBubbles` and
`DrawRope` read the same globals and find the blended values. They carry the root's snap
rule, and "was this slot alive on the last tick" is their identity test, because a
bubble slot is freed by writing its size to zero and handed straight to the next bubble.
Deliberately left at thirty: the lightning, because `UpdateLightning` re-rolls its
segments out of the random generator on every tick, so a bolt's previous shape is a
different bolt and blending them would draw a shape the game never draws; a shard's
tumble, whose two rotations are unsigned bytes shifted up by eight and would wrap the
wrong way half the time (where it flies is interpolated); and splashes and ripples,
whose vertical term steps its velocity on the same line as the position and whose life
is decremented in two places, so the walk back is not exact. Two approximations are
marked where they are made: a snowflake's sideways drift is ramped towards the wind
after it has moved, and a dust mote's sink is a quantised reading of a field stepped
after the move - a unit or two out of a step of sixty-odd, on a sprite four pixels wide.
`INTERPLOG` carries `particles=` and `particle_kinds=`, one bit per draw site, because a
guard that never fires looks exactly like thirty hertz and there is no other way to see
it.

**Commits.** `80df417`

### Every per-frame counter on the logic tick
2026-09-07 - 2026-09-09

**What.** Every counter in the engine that measured time in drawn frames steps once per
logic tick, so a death fade, a rain shower, a level caption, a footprint and a corpse
last exactly as many seconds at sixty as at thirty. One criterion serves all of them:
`InterpIsTweenFrame()` (`game/interpolate.h`, `game/interpolate.cpp`) is true when the
frame being drawn carries no logic tick, and every guarded counter steps only when it is
false. At `fps_mode 0` it is false always, so the thirty hertz behaviour is the old
behaviour line for line. `DoScreenFade` also clamps the walked fade value to the fade's
end value once the step has carried it past (`InterpFadeClamp`,
`game/interpolate_math.h`).

**Why.** A counter stepped per drawn frame runs at double speed the moment the engine
presents twice as often. The latch is set from the cadence rather than from `alpha < 1`,
because a cutscene frame and a frame with no previous state are drawn live and still
carry no tick, so every fade through every cutscene would otherwise have run at double
speed. It lives for the whole loop iteration and not just the draw, because two counters
sit outside `DrawPhaseGame` - `gfLegendTime--` before it and `cutseq_busy_timeout--`
after it - one reading the previous frame's latch and one this frame's. It is false in
every modal loop: `S_DumpScreen` calls `InterpNoteClassicPresent()` whenever the present
it is about to make waited the full two ticks and `S_DumpScreenFrame` calls it always,
without which a modal loop entered straight after an in-between frame would inherit a
`true` nothing inside it could clear, and `RenderLoadPic`'s `do { ... } while (DoFade !=
2)` (`specific/output.cpp`) would never end, `DoScreenFade` being the only thing in the
build that sets `DoFade` to 2. The fade clamp is the one place where stepping on the
tick was not enough: `SetFade(start, end)` (`specific/specificfx.cpp`) sets `FadeStep =
(end - start) >> 3` and `DoScreenFade` walks `FadeVal` by it once per logic tick,
reading `a = FadeVal << 24` before the step and, on the ninth call, putting `FadeEnd`
over `a`; for the fade-in every level starts with - `SetFade(255, 0)`, from `DoLevel`,
`DoTitle` and `RenderLoadPic` in `game/gameflow.cpp` - the step is -32 and the eighth
step lands on 255 - 256 = -1, which shifted into the colour is `0xFF000000`, alpha 255,
black. At thirty presents a second that value is read exactly once, by the ninth call,
which overrides it; above thirty every in-between frame between the eighth and the ninth
tick drew it, so every fade-in carried one whole logic tick of full black, two frames at
sixty, 33 ms. The fade-out direction (`SetFade(0, 255)`, step 31) lands on 248 and never
overshoots, and `Fade()`'s grey tile clamps inside its own step.

**How.** The counters and where they are: `Fade()` (`game/tomb4fx.cpp`) - the grey walks
on a tick, the tile is drawn every frame; `DoScreenFade()` (`specific/specificfx.cpp`) -
nine steps on nine ticks, `a` read before the step so the in-between frame draws the
same grey, and `InterpFadeClamp` applied right after the step; `FlashFader -= 2`
(`specific/output.cpp`) - `DrawFlash()` every frame, the walk on a tick; `FlashIt()` and
`DrawPickups()` (`game/health.cpp`), the second the whole slide-in animation, returning
early; the sky lightning machine and `aWibble++` (`game/draw.cpp`); rain and snow
(`specific/specificfx.cpp`) - the simulation loop on the tick, the draw loop every
frame; a spark letting go of its item - the random threshold rolled on the tick;
`GnFrameCounter++` (`specific/output.cpp`) - on the tick in a game frame, per present in
a modal loop; `RegeneratePickups()` moved from above `ControlPhase`'s loop into the tick
body (`game/control.cpp`); `gfLegendTime--` (`game/gameflow.cpp`);
`cutseq_busy_timeout--` (`game/deltapak.cpp`); `ammo_change_timer--` and `PoisonFlag--`
(`game/health.cpp`); the F12 bump-mapping debounce (`specific/output.cpp`); the
underwater dust simulation `DoUwEffect` and the gas cloud's `yo`/`sTime`/`mTime` in
`DrawGasCloud` (`specific/specificfx.cpp`); `print->Active--` (`game/footprnt.cpp`); the
monitor screens' scan band in `DoMonitorScreen` (`specific/file.cpp`). The resolution
caption's `resChangeCounter` (`specific/output.cpp`) counts 120 logic ticks, which is
vanilla's own 120 frames at thirty; it is the one deliberate difference at `fps_mode 0`,
and it is the version that cannot leave the caption standing (a step scaled by a
measured rate truncates to zero above the target). Two per-frame rolls are deliberately
left: the enemy muzzle flash's random Z rotation (`DrawBaddieGunFlash`,
`game/objects.cpp`) and the steel door's lens flare colour (`DrawSteelDoorLensFlare`,
`game/tower.cpp`), as is Lara's cutscene speech head (`game/deltapak.cpp`) - the rule is
that a counter carrying state or a cadence is guarded and pure noise is not. The corpse
fade left the draw path entirely: `PrintObjects` (`game/draw.cpp`) only marks the bodies
it drew, one byte per item slot, and `StepAfterDeathFade()` (`game/draw.cpp`, called
from `ControlPhase`) does the counting and the killing. That also removes a latent bug -
`KillItem` was being called from inside the walk of the room's own item list, and for a
spawned item (`item_num >= level_items`) it ends with `item->next_item = next_item_free;
next_item_free = item_num;` (`game/items.cpp`), rewriting the very link the loop was
about to follow. Only bodies in a room that was actually drawn age at all, exactly as at
thirty, which is why the tick does not sweep all 512 slots; the mark is wider than the
old increment (`after_death > 0`) because the skeleton writes that field directly
(`game/skeleton.cpp`). The dead test `!(wibble & 3)` is left dead on purpose:
`ControlPhase` steps `wibble = (wibble + 4) & 0xFC`, so it is always true, and repairing
it would change how long every body in the game lies there. The fade clamp changes no
thirty hertz picture, the ninth tick having drawn `end` anyway, and `fps_mode 0` is
byte-identical by the same arithmetic; `SetFade(255, 0)` runs at a level start, at the
title and at the loading picture and nowhere else, so a black frame anywhere else is a
different fault. Noted and not changed: `S_DrawLoadBar` (`specific/drawbars.cpp`) draws
only the bar onto whatever the back buffer holds and presents, with
`D3DSWAPEFFECT_DISCARD` and one back buffer (`specific/dxshell.cpp`), whose content
after a present the D3D9 contract calls undefined. `TICKLOG`, one line a logic second
under `-log`, prints the guarded counters so that the same line comes out of a thirty
and a sixty hertz run of the same scene. Checked by
`Preparation/Tools/Diagnose/InterpTest.cpp`, section `fade`: the nine ticks of 255 -> 0
and of 0 -> 255 draw what they always drew, the eighth step of 255 -> 0 lands on -1
unclamped and the frame after it now draws 0, the eighth step of 0 -> 255 stays 248, and
-1 shifted into the colour is alpha 255.

**Commits.** `ced7f30`, `34a7ccc`, `a785a50`, `eb0c6df`

### The inventory ring at the frame rate
2026-09-15

**What.** The inventory ring presents at the rate the option asks for and keeps its
logic at thirty steps a second. `S_CallInventory2` (`game/newinv2.cpp`) keeps its own
modal loop and its own present, and that present asks for `InvFrameMinTicks()`
(`game/inventory_interp.cpp`), `gPresentMinTicks` being raised around that present only
and put straight back to two. The loop body runs its logic once per two ticks through an
accumulator of `ControlPhase`'s shape (`InvInterpBeginFrame`), and on a frame with no
step the spin of the selected icon, the brightness ramp, the row slide, the two ring
fades, the ammo selector's fade and spin and the examine zoom are blended, while every
discrete value is held. Nothing else moved: the pause screen, the load and save screens,
the death menu, the F4 outfit screen and the loading fade still wait their two ticks,
and the randomizer is untouched.

**Why.** At sixty the ring was the one thing on screen still presenting at thirty. The
accumulator is clamped to one step's worth of credit, with one difference from
`ControlPhase` that is written out where it lives: the ring's body draws as it steps, so
the step can run at most once per frame, and a hitch that banked four steps would
otherwise make the ring step on every frame until the debt was paid - the ring running
at the frame rate, which is what this change exists to stop. The blended values are
handed to the draw at the read rather than written into the ring, because
`draw_current_object_list` steps and draws in one pass: the spin and the brightness sit
inside the very loop that draws each icon and the slide is stepped at the bottom of the
same function, so a value written before that body is stepped over by the body itself
and one written after it is too late for the picture. It is the step frame that is
blended: in the game the draw comes before the tick and the drawn states are `T1,
mid(T1,T2), T2`, while in the ring the step comes before the draw, so the same
arrangement would draw `C1, mid(C0,C1), C2` - a picture walking backwards on every
second frame. The factor the ring's accumulator produces through `InterpRefreshAlpha` is
0.5 on the frame whose step ran and 1.0 on the next, which is the even, monotone order.

**How.** `game/inventory_interp.cpp` and `game/inventory_interp.h` hold the snapshot and
nine accessors, each returning the live value whenever there is nothing to blend. Six
take the previous generation and the live one - the values vanilla steps before it draws
them; three - the row slide, the current slot and the ammo fade - take the two previous
generations, because their subject has already been stepped by the loop that draws it,
which keeps each of them exactly one step behind the spin, the relation they have at
thirty. On a tween frame `S_UpdateInput` is not called and `input`, `dbinput` and the
six `go_` flags of `do_debounced_joystick_poo` are shadowed to zero and restored;
`GameTimer++` (the stats page's clock, `GameTimer / 30`), `UpdatePulseColour`
(`game/text.cpp`), the keymap cheats, `GnFrameCounter`, `objlistmovement` and
`curobjinlist`, `combine_ring_fade_val` and `normal_ring_fade_val`,
`ammo_selector_fade_val`, `examine_mode`, `stats_mode`, `keypadpause--` with its six
tones, and every discrete piece of state are held; `LookCnt` and `med_hotkey_timer` step
once per logic step, which is the rate they had. The ring raises the build's own tween
latch for itself (`InterpNoteMenuFrame`, `game/interpolate.cpp`) now that its present no
longer comes through `InterpNoteClassicPresent`, so `Fade`, `FlashFader`,
`DoScreenFade`, the F12 debounce and `resChangeCounter` inside `S_OutputPolyList`
(`specific/output.cpp`) stay guarded. The row's slot and its offset are taken from one
generation (`InvDrawCur` / `InterpRingSlot`), because the step that puts
`objlistmovement` back to 0 moves `curobjinlist` in the same breath and a frame drawing
the new slot with the old offset put the whole row seven eighths of a slot to the side;
a slot left over from a list that has since been rebuilt falls back to the live one, and
the option list under the row keeps the live slot, which is the relation it already has
at thirty. `InvInterpCut` drops the snapshot so that the next step frame draws live on
opening or closing a mode, the ammo ring taking over, the combine ring being built, the
list being rebuilt by a combine, a separate or a weapon hot key, the load/save screen,
and opening and closing the ring itself; a rebuilt list needs no call site, each slot's
`invitem` travelling in the snapshot. `INVLOG` (new, `-log` only, behind `gLogEnabled`)
writes an open line, one line per logic second with `frames=`, `steps=`, `tween=`,
`alpha=min/max`, `budget=`, `blend=` and `cuts=`, a cut line with its reason, and a
close line; `blend=` names the motions that really blended, because a field that never
blends simply looks like thirty. `InterpRingStep`, `InterpRingAlpha`, `InterpRingSlide`
and `InterpRingSlot` live in `game/interpolate_math.h` so that the offline test runs the
engine's own arithmetic.

**Commits.** `6eab5e9`, `ca17073`

### The title menu at the frame rate, one tap one row
2026-09-07 - 2026-09-15

**What.** The title screen is drawn at the rate the option asks for, with its fly-by
interpolated through the normal draw path, and one tap of a direction still moves one
row on every page it reaches. On the call in which the menu would read an edge for the
second time, its input is shadowed: `input` and `dbinput` are put aside, zeroed for the
length of that one call and put back byte for byte the moment it returns
(`TitleInputShadow`, `game/gameflow.cpp`).

**Why.** `DoTitle`'s loop is `TitleOptions(); DrawPhaseGame(); ControlPhase(nframes);`,
so the menu is walked once per present while the debounced input it walks with,
`dbinput`, is written once per tick inside `ControlPhase`'s tick loop
(`specific/input.cpp`) and nowhere else. At two ticks per present the two rates are the
same and every edge is read exactly once; at one, every second `ControlPhase` runs no
tick, `dbinput` keeps standing and the next `TitleOptions` reads the same press again -
one tap of Down moved the cursor two rows and *New Game* could not be selected. A menu
only has to be told which of its calls is reading an edge for the second time, which
costs it no frames. The shadow is restored rather than left zeroed for a reason the code
really has: `SetDebounce` is cleared inside the first tick of a `ControlPhase` call
(`game/control.cpp`), so a call that runs three ticks writes `dbinput` on one of them
and a zero left behind would swallow the edge for the other two.

**How.** The question is asked of the engine's own tick counter and not of a clock of
its own: `g_tickSerial` steps once per logic tick at every frame rate and is readable as
`InterpTickSerial()` (`game/interpolate.cpp`, `game/interpolate.h`), so the rule holds
at `fps_mode 1`, at `fps_mode 2` where four or five frames fall inside one tick, on the
first iteration after `DoTitle`'s priming `ControlPhase` and through a hitch.
`GameFrameMinTicks()` (`specific/output.cpp`) therefore gives the title what it gives a
played frame - the `InventoryActive` test remains as the guard for any future path into
`DrawPhaseGame` from a menu - and the fly-by is drawn through `DrawPhaseGame` with the
root and camera interpolation underneath it. Everything `TitleOptions` reaches steps on
`dbinput`, which the shadow covers: the level select, the whole options tree and the
options pages; `JoyMenuStep` has a latch of its own and the two direct `keymap` reads in
the key-mapping screen are idempotent. One counter behind the title needed the guard the
rest of the build has: `DoCredits`' roll (`StartPos--`, `specific/alexstuff.cpp`) is one
pixel a present and asks `InterpIsTweenFrame()` now.

**Commits.** `5301765`, `7cbff9d`, `ca17073`

### The cut scenes at the frame rate
2026-09-28

**What.** A cut scene is drawn between its steps like the game around it. The camera's eye
and target are blended between the last two ticks by the interpolator's own camera block,
and every actor - Lara, actor 0, included - is drawn from its node table blended between
the same two ticks: `game/cutseq_interp.cpp` copies the decoded tables at the start of the
tick, writes the blend into them before `DrawPhaseGame` draws and puts the tick's values
back after it. A cut in a camera track (the eye or the target stepping more than 256
units) draws the frame live, an actor whose root jumps more than 1024 is drawn at the
tick's values while the others glide, and a node with an axis exactly half a turn from its
previous value keeps the tick's three values. The braid, which a cut scene swings inside
the draw, swings once per tick. New: `game/cutseq_interp.cpp` and `.h`,
`game/cutseq_decode.h` (the track decoder, lifted unchanged out of `game/deltapak.cpp` so
that a tool compiles it), `Preparation/Tools/Diagnose/CutseqProbe.cpp` with `bcp.bat`, the
`-log` seam `CUTLOG`, and a `cutseq` section in `InterpTest`.

**Why.** At sixty and at 165 hertz every cut scene stepped at thirty - camera, actors and
Lara - while `FPSLOG` presented sixty or 165 pictures a second, because `InterpBeginFrame`
returned at `GLOBAL_playing_cutseq`. The return was there because the cut scene camera is
decoded from a frame index and `frigup_lara` moves Lara inside `DrawPhaseGame`, so a
restore was taken to undo it. Read against the code that does not hold: `DecodeAnim` is
the only writer of the actors' node values (`xrot_run`, `yrot_run`, `zrot_run`), and their
only readers are `do_new_cutscene_camera` (the camera's two nodes) and `updateAnimFrame`,
which `DrawCutSeqActors`, `frigup_lara`, `GrabActorMatrix` and `GetActorJointAbsPosition`
call - the last two only from the scenes' control routines, inside the tick. The decoder
keeps its own state per node (`RTDECODE`) and only the tick advances it, so the decoded
outputs of two ticks can be blended and restored like `items[]`, and the tick sequence is
the one a thirty hertz run has. The tracks cut as a film cuts: over the 44 cut scenes of
`DATA/CUTSEQ.BIN`, 77,231 frames and 77,187 steps, `CutseqProbe` counts 371 camera cuts in
33 cut scenes (0.48 % of the steps; none in 1-4, 11, 20, 28-31 and 34), the smallest a
jump of 294 units and the largest in cut scene 25 at frame 1622, the eye 27,494 and the
target 30,686 (Manhattan; 20,116 and 21,798 in a straight line). The eye's 99th percentile
is 78 a frame and the fastest drive 216 to 222 a frame (cut scene 30, frames 405-409), and
35 of the cuts step no more than 1024 on both the eye and the target, so a 1024 Manhattan
rule on the two would have blended them as motion. The braid was a live fault of its own:
`frigup_lara` runs `HairControl` once per drawn frame, so at sixty and 165 the braid's
simulation ran two to five and a half times per tick.

**How.** The snapshot is `g_prev` and `g_saved`, x, y and z per node for ten actors of up
to forty nodes, 2,400 bytes each; the largest actor in the file has 30 nodes and the
largest scene 115. `CutseqInterpOpen` (`game/cutseq_interp.cpp:132`) is called at the end
of `init_cutseq_actors` (`game/deltapak.cpp:3578`), after the tables are laid out, and
invalidates the whole interpolation (`InterpInvalidate("cutseq open")`);
`CutseqInterpClose` (`:170`) at the teardown in `handle_cutseq_triggering`
(`game/deltapak.cpp:2237`), before the next credits scene opens; `CutseqInterpSkip`
(`:188`) at the end of `do_cutseq_skipper_stuff` (`game/deltapak.cpp:3723`).
`CutseqCapturePrev` (`:200`) runs in `InterpCapturePrev` beside the hair capture
(`game/interpolate.cpp:1007`), at the start of the tick and so before
`do_new_cutscene_camera` (`game/camera.cpp:1413`) decodes; a capture before frame 0 was
decoded leaves no previous table. `InterpBeginFrame` asks `CutseqBlendable` (`:226`) where
the return stood (`game/interpolate.cpp:1366`) - the scene's first tick is drawn live -
then `CutseqCameraCut` (`:247`, `game/interpolate.cpp:1404`) beside `CameraCut`, and after
`g_applied` is set `CutseqApply` (`:270`, `game/interpolate.cpp:1429`), so
`InterpParticleAlpha` and `InterpEyeRoom` take the blended frame as on any other;
`CutseqRestore` (`:335`) runs in `InterpEndFrame`'s restore block
(`game/interpolate.cpp:1743`) and in `CutseqInterpInvalidate` (`:194`, called from
`InterpInvalidate`, `game/interpolate.cpp:1863`), because the decoder adds its next deltas
to whatever the tables hold. The order in the game loop is `InterpBeginFrame`
(`game/draw.cpp:466`), `frigup_lara` (`:476`), `DrawRooms` (`:480`) with
`DrawCutSeqActors` inside it (`:2489`), `InterpEndFrame` (`:518`); `DoLevel` draws at
`game/gameflow.cpp:1153` and ticks at `:1172`, `DoTitle`, which plays the credits, at
`:923` and `:925`. The arithmetic is in `game/interpolate_math.h` so that `InterpTest`
runs it: `INTERP_CUTSEQ_SNAP` 256 (`:150`, beside `INTERP_CAM_SNAP`),
`INTERP_CUTSEQ_ROOT_SNAP` 1024, `INTERP_CUTSEQ_ACTORS` 10 and `INTERP_CUTSEQ_NODES` 40
(`:2000`-`:2002`), `InterpLerpAngle10` (`:2007`, the difference folded into -512..511 and
the result masked to ten bits), `InterpHalfTurn10` (`:2014`, a difference of exactly 512),
`InterpCutseqCameraCut` (`:2022`, eye and target measured apart), `InterpCutseqRootStep`
(`:2034`, node 0 at three times its value, as `updateAnimFrame` draws it) and
`InterpCutseqBlend` (`:2051`: the root blended as a position, every other node as three
ten-bit angles, a node with a half-turned axis kept whole at the tick's values). Two rules
of the camera block are left out of a cut scene: the drawn-eye rule
(`game/interpolate.cpp:1681`), because a cut scene camera's room comes from
`IsRoomOutside`, which matches no room within a sector of a room's walls and keeps the old
one, so the rule would withhold the sky; and the braid's blend (`:1490`), because the
snapshot is taken before the draw steps the braid and the frames after the step would pull
it back. The braid's guard is `if (!InterpIsTweenFrame())` around the two `HairControl`
calls in `frigup_lara` (`game/deltapak.cpp:3289`). `InitPackNodes`, `GetTrackWord`,
`DecodeTrack` and `DecodeAnim` (`game/deltapak.cpp:2917`-`:2935`) forward to the templates
`CutseqInitPackNodes`, `CutseqTrackWord`, `CutseqDecodeTrack` and `CutseqDecodeAnim` in
`game/cutseq_decode.h`, which hold their bodies unchanged and include no engine header.
The seams, `-log` only: `CUTLOG open cutseq=17 frames=2097 actors=3 nodes=48 fps_mode=1`;
`CUTLOG cut cutseq=25 frame=1622 why=camera eye=27494 target=30686 actor=-1 node=-1
step=-1` once per cut tick (`why=` also `actor` with the root step, `halfturn` with the
node, `open`, `skip`, `close`; `frame=` is the index `DecodeAnim` decoded); `CUTLOG close
cutseq=17 frames=4194 ticks=2097 blended=2084 live=2110 cuts=12 actor_cuts=1 halfturns=3
hair=2097 eye_rooms=- hair_lag=69` as `CutseqProbe -cutlog 17` predicts it (the tool
prints `-` for `eye_rooms=`, which needs the level's rooms), where `hair=` counts the
braid's steps (`CutseqNoteHair`, `game/deltapak.cpp:3296`), `eye_rooms=` the blended
frames whose room walk starts in another room than the tick's
(`game/interpolate.cpp:1182`) and `hair_lag=` how far Lara's blended root stood from her
previous tick's, which is how far the braid trails. `INTERPLOG`'s `cutscene=` keeps its
place and now counts frames between two ticks of a cut scene drawn live for a reason that
is not a cut (an actor larger than the snapshot, a scene never opened), nought in a
healthy log; `cutscene_cut=` at the end of the line counts the frames a cut scene camera's
cut drew live. `CutseqProbe` (`bcp.bat`, x86 like `SaveDiff`, because it instantiates the
decoder with `global/types.h`'s structs) reads a `cutseq.bin` in place, decodes every cut
scene with `game/cutseq_decode.h` and measures it with `game/interpolate_math.h`; `-frames
<n> <a> <b>` prints the decoded eye, target and roots, `-cutlog <n>` the `CUTLOG` lines a
run at sixty writes. `InterpTest` gains `cutseq`
(`Preparation/Tools/Diagnose/InterpTest.cpp:5522`, 32 checks): ten-bit angles across the
wrap (1000 to 24 and 24 to 1000 land on 0, every 0.5 blend of a 147 x 205 sweep lies on
the shorter arc), the half turn refused, the cut rule at 255, 256 and 257 with eye and
target apart, the root test at 1023 and 1026 drawn units, and the apply-and-restore round
trip on a four-node table.

**Checked.** `CutseqProbe` over the file against the earlier analysis: 371 cuts in 33 cut
scenes over 77,231 frames, the cut-free list, the eye's 99th percentile of 78 and the 914
exact half turns (on 570 node steps among 12,830,640 axis steps) come out as it said.
Where the tool differs, its numbers stand: the largest cut is 27,494 and 30,686 in
Manhattan units (the analysis' 20,116 and 21,798 are the same jump in a straight line),
cut scene 30's drive is 216 to 222 a frame (195.7 was the target's straight-line speed),
35 cuts step no more than 1024 on either end, not 74, the root jumps above 1024 without a
camera cut are 44, not 35, and a scene holds at most 115 actor nodes (117 with the
camera's two). The camera tracks end 0 to 7 words from their end after the last frame, and
the hard-coded braid frames of `hair.cpp` fall on camera cuts of the decoded tracks (cut
scene 17 at 208 and 477, 8 at 841, 27 at 930, 16 at 409, 1872 and 3048, each one less than
the `GLOBAL_cutseq_frame` the draw reads). The lift: the four original bodies and the
templates decode all 44 scenes side by side to the same node values and decoder fields in
all 4,769,672 node states (a scratch program built from the pre-lift source). The suites:
`ModernTest` 13992, `MenuTest` 42364, `InterpTest` 683 (651 before, 32 new), `LipsTest`
270 with the reference meshes and 243 without, 0 failed. At `fps_mode 0` every new call
that writes a node table sits behind `alpha >= 1`, which returns before the cut scene
branch, or behind `g_applied`; `InterpIsTweenFrame()` is false, so the braid steps on
every frame as before; the capture reads the tables, and the open, the skip and the close
write only the interpolator's statics, and `InterpInvalidate` was already called at every
setting. `hd_enable 0` and the HD path: no file under `specific/hd` changed, and
`DrawCutSeqActors` hands its matrix stack to the HD path as before. The blend of one frame
costs 0.74 microseconds for 115 nodes (scratch timing).

**Unchanged.** `fps_mode 0`; the decoder and the tick sequence, so `TICKLOG` has to read
the same at every setting; `DrawCutSeqActors` and its HD seam; the movies, a level's
flybys and the title's flight; the braid's own blend, which stays off in a cut scene as it
was; the randomizer.

**Decisions.** The decoded outputs of the last two ticks are blended, not a snapshot of
two generations: the draw comes before the tick in `DoLevel` and `DoTitle`, so the
previous tick and the live tables are the pair. The camera needs no snapshot of its own:
`g_prevCam` is filled every tick in a cut scene too, and the camera block with
`RebuildView` blends it. A cut in the track is a step of the eye or the target above 256
Manhattan. An actor's root jumping above 1024 draws that actor live. An exact half turn on
a node axis draws that node at the tick's values, all three axes, because the other axes
of such a node turn with it. The credits are cut scenes like the others. The drawn-eye
rule and the braid's blend are left out of cut scenes.

**Left.** The braid steps once per tick and is drawn where the tick left it, so on a frame
between two ticks it trails Lara: at sixty by half her step, at the display's rate by up
to a whole step, because the frame that carries the tick is drawn at a small alpha and the
braid is stepped from that pose; `hair_lag=` is taken against the previous tick's root and
so bounds the lag from above there. Over the file 520 of her 72,662 blended ticks put it
more than 32 units behind, at most 426 in cut scene 22 (294 in 12, 213 in 19). Stepping it
from the tick's own pose at the tick and blending two steps, as the game does outside cut
scenes, would take a hair snapshot of its own. A frame between two ticks starts its room
walk where `GetFloor` lands from the tick's room for the blended eye, and the tick's room
from `IsRoomOutside` can be stale within a sector of walls, so the two can start in
different rooms (`eye_rooms=` counts it); should that show as a flicker of the sky or of
the rooms drawn, the frame between ticks would take the tick's room. No root threshold
parts jumps from motion: 17 lone jumps under 1024 without a camera cut are blended as a
slide for one tick, and a falling object in cut scene 38 (1029 to 1032 a frame, frames
1271-1275) is drawn at the tick. 63 node steps blend more than 30 degrees off the shortest
arc, 62 of them with an axis step of 384 or more; holding a node on such a step would
catch them.

**Commits.** `d2bfb86`, `04ba96f`, `ee81554`, `8b64a97`

### The level warm-up
2026-09-07

**What.** `WarmUpLevel()` (`specific/file.cpp`) runs after the load thread has joined
and the HD build is done, with the loading bar still on screen, and calls
`IDirect3DResource9::PreLoad` for every classic texture page (`specific/texture.cpp`,
`CreateTexturePage`), every HD page slot (`HDPageCacheSlotTexture`) and every HD model's
vertex and index buffer (`HDModelPreload`, new in `specific/hd/hd_model.cpp` and
`specific/hd/hd_model.h`). `PreTouchDrawBuffers()` (`specific/polyinsert.cpp`) writes
through `SortBuffer` (2.5 MB), `SortList` (256 KB) and `Bucket` (30 x 2080 vertices, 2.4
MB) from the same place. One line goes into the log: `WARMUP pages=112 hd_pages=143
hd_buffers=94 ms=38.2`.

**Why.** Nearly every texture in this engine is `D3DPOOL_MANAGED`, and a managed
resource lives in system memory until something draws with it - Direct3D uploads it at
that first bind, so the frame that first shows a wall, a texture or an enemy pays for
the upload, and a level shows most of its art in its first seconds. `PreLoad` is
Direct3D's own answer to that: it draws nothing, touches no render state, costs nothing
for a resident resource and is documented to be ignored for one in `D3DPOOL_DEFAULT`.
The three draw buffers are plain statics living in `.bss`, which Windows hands out as
demand-zero pages - roughly 1300 first-write faults, every one of them in the first
frame that draws anything. Writing through them cannot change a pixel: `InitBuckets` and
`InitialiseSortList` reset all three at the top of every frame, and nothing reads a byte
of any of them that was not written in the same frame first. What the pass cannot cover
is named rather than guessed at - the water mark tables, a room's first light list, the
driver's first use of a shader or a vertex declaration, and Windows paging the process
in.

**How.** The warm-up runs in every frame-rate mode, being by construction unable to
change what is drawn. Its numbers and the `first_frame` and low-`sec=` late lines that
answer for the rest are documented with the profile seams.

**Commits.** `34a7ccc`, `5301765`

### The frame-rate seams and the offline proofs
2026-09-07 - 2026-09-15

**What.** New files `specific/profile.cpp` and `specific/profile.h` carry `PROFLOG`, a
per-frame breakdown taken with `QueryPerformanceCounter` in seventeen phases: `logic`
(`ControlPhase`, timed at its call site in `game/gameflow.cpp` because `ControlPhase`
has early returns), `gather`, `rooms`, `sky`, the HD path's three shares of the gather
(`hdroom` in `InsertRoom`, `hdobj` in `HDObjectBegin`/`Node`/`End`, `hdlara` in
`HDLaraDrawFrame`), `output` with `hddraw`, `hdlight`, `hdobjdraw` and `hdglow` inside
it, `buckets`, `sort` and `sortdraw` as whole-frame totals, and the present split into
`spin` (the frame finished and waiting for its slot) and `flip` (`Present` itself). It
writes one line a second with mean and worst per phase, one line for every frame that
missed its slot with all its phases and the second of the level it happened in, one
`first_frame` line per level, and a short line for an interval that presented nothing of
the loop's own - `pause at=41s frame=5516.30 work=8.90 presents=1204`. `FPSLOG`
(`specific/hd/hd_level.cpp`) carries `fps=` (the rate the budget was taken from),
`logic_ticks=` (`GlobalCounter`'s difference over the second), `budget=` (the slot the
frame was judged against) and `real_fps=` (the rate worked out from the mean present
interval). `TICKLOG`, `INTERPLOG` and `INVLOG` each write one line per logic second. All
of it is behind `-log`.

**Why.** A frame count cannot say where a frame's milliseconds went. The budget must be
the slot the frame really waited for and not what `fps_mode` says, because only the game
frame's own present ever drops below two ticks - measured against `fps_mode` alone,
every menu second reported thirty slow frames and up to thirty late ones. An interval of
5516 ms whose own work was 32 ms is not a slow frame but a loop that was somewhere else,
so those are counted as pauses, never as late, and are kept out of the second's mean and
maximum. A logic second is the unit for `TICKLOG`, `INTERPLOG` and `INVLOG` because
`GlobalCounter` steps thirty times a second by construction in both modes and stands
still while the ring is up, so the same line has to come out of a thirty and a sixty
hertz run of the same scene: a number that doubles is a counter that was missed, one
that halves is a guard that also stopped a tick frame.

**How.** Every seam is `ProfBegin(...)` / `ProfEnd(...)`, both of which are `if
(gLogEnabled)` around a call, so without `-log` not one clock is read, no accumulator
moves and no string is built. `ProfPresent()` sits in both present functions
(`specific/output.cpp`), so the profile knows how many pictures went out during an
interval and recognises a modal loop drawing behind its back; an interval that exceeds
its own work by more than a second catches what presents nothing at all. `buckets`,
`sort` and `sortdraw` are whole-frame totals because each is called from three or more
places; `SkyDrawPhase` has a phase of its own because it is the one place in the frame
that draws buckets and a sort list outside `S_OutputPolyList`, which makes the printed
remainder `outrest = output - hddraw - hdglow`. `sort` and `sortdraw` are separate
phases because `SortPolyList` is a quicksort and `DrawSortList` is vertex copying and
`DrawPrimitiveUP`, and the line carries `sortn=`, how many entries the list held.
`PROF_LOGIC` is deliberately not cleared by `ProfileFrameBegin`, because `ControlPhase`
runs between the present and that call; `ProfileFrameEnd` clears it once it has reported
it and once the second's totals have taken it. The definition is written down in
`specific/profile.h` and in `docs/Engine-Manual.md`: the loop is draw, present, logic,
so the game step that follows a frame is counted into the next frame's line, `logic` is
not part of `work` (`ProfileFrameBegin` to `ProfileFrameEnd`), and the invariant to
check a log against is `work + logic <= frame`. `FrameBudgetMs()` / `MissedSlotMs()`
(`specific/hd/hd_level.cpp` and `specific/profile.cpp`) read `gPresentMinTicks` at the
moment the frame is judged, which is why `DrawPhaseGame` (`game/draw.cpp`) restores the
two after `ProfileFrameEnd`; `PresentSlotMs()` (`specific/output.cpp`) is the single
definition both logs call, so they cannot disagree about a frame whose slot is the
panel's period rather than a whole number of ticks. `INTERPLOG` (`game/interpolate.cpp`,
with the ring's own line in `game/inventory_interp.cpp`) carries `lara_step`, how far
Lara moved on screen from one picture to the next, measured on the position that was
drawn rather than the one the logic holds - over the same walk it must read half at
sixty what it reads at thirty - and `alpha=` is a `min/mean/max` distribution, because
bound to the display every frame has a factor of its own and a maximum pinned at 1.00 is
the clamp firing. Two offline tools stand beside the logs:
`Preparation/Tools/Diagnose/SaveDiff.cpp` (`bsd.bat`) compares two savegames field by
field by name, taking every offset from the engine's own `global/types.h` with
`offsetof`, which is why it is built for Win32 - the structs are `#pragma pack(1)` but
carry pointers; and `Preparation/Tools/Diagnose/InterpTest.cpp` (`bit.bat`) includes
`game/interpolate_math.h` itself rather than a copy, so there is no twin to keep in
step. It asserts the short-angle lerp over a sweep of 16,256 angle pairs with the 128
exact half-turns stepped over and asserted separately, the position lerp over a level's
whole coordinate range, the snap thresholds against the engine's own speeds, the cadence
over a simulated minute at both settings and through a hitch, the counter rule at both
rates including the nine-step screen fade, the pose factor against the four hard-wired
branches it replaced over 441 matrix element pairs for every `(frac, rate)` the retail
levels can produce - 336,471 animation frames' worth, maximum difference 0 units on both
stacks - the pose in time at rates 1, 2, 4 and 5, the display-bound loop at 60, 120 and
144 Hz with the vblanks on their own grid and their own phase (ten simulated seconds run
300 logic steps against 600, 1200 and 1440 frames, the factor never clamped, no frame
drawing what the one before it drew, and a 200 ms hitch caught up less the two ticks
vanilla's own `if (nframes > 10)` cap discards), the particle walk-back against an item
whose previous state really is remembered, and the ring's cadence and the title's
shadow. The recipes are in `docs/Engine-Manual.md` under *Frame rate*.

**Commits.** `34a7ccc`, `5301765`, `ced7f30`, `2628a7e`, `a785a50`, `80df417`, `ca17073`

## 2.6 Modern controls and the options menu

Beside the engine's own Tank control the game offers a second scheme, **Modern**, in
which the left stick steers Lara relative to the camera and the right stick moves a free
orbit around her. With it come an adjustable pad dead zone, a second keyboard set and a
second pad set of bindings, chord bindings behind a Modifier, an optional second action
key and four hold-or-toggle modes; the options menu that carries all of them is one
table-driven frame instead of five hand-written pages. Every one of these is a stored
setting whose default is the engine's own behaviour, and every statement added to an
existing engine file sits behind a test on that setting, inside a new file nothing
reaches without one, or in menu and settings code — so the shipped default is the engine
as it was. Origins, as facts: the movement design is TROne's (GPLv3) by Todesengel1931 —
https://github.com/todesengel1931 — re-derived here in this engine's own units and state
numbers, with 32 test expectations (28 `modern.*` and 4 `combatmode.*` cases of their
`tests/test_controller.cpp`) taken across verbatim and named as such in the test's own
header; none of their modern-control code is in this tree. The orbit camera shares only
the idea that a camera angle is a state: TROne's world angle never returns behind the
player, this one falls home. Nothing in the scheme comes from the Remaster's programs at
first or second hand — it was rebuilt by playing the Remaster and watching what it does,
a walk of all 32 constants of the design found none traceable to a Remaster program (16
kept as they stood, 9 re-measured by play, 5 not applicable, 2 unknown and therefore not
taken), and the only Remaster-derived facts in the set are a handful of option names
read out of a plain text file of the player's own installation. The same statement
stands in the origin block of `game/modern_move.h` beside the code.

### Modern control: the stick turned into the classic direction bits
2026-09-17 - 2026-09-20

**What.** The setting `control_modern` (default 0 = Tank) makes the left stick steer
Lara relative to the camera instead of relative to her own heading. It is not a new way
of moving her: a pure function turns "stick direction plus a remembered world heading"
into exactly the `IN_FORWARD` / `IN_BACK` / `IN_LEFT` / `IN_RIGHT` / `IN_WALK` /
`IN_LSTEP` / `IN_RSTEP` bits the keyboard has produced since 1996, and `game/lara.cpp`
does the turning at the classic automaton's own rate — with one named exception, a
direct write to `pos.y_rot` in ten standing and running states so that a half turn on
the spot does not cost twenty-three ticks. The direct turn moves her heading toward the
course the stick asks for at `MODERN_TURN_RATE` 5461 (30 degrees a tick); a reversal
turns at that same rate the short way round, a tie broken by her own last turn, and
there is no one-tick snap. While she is running the turn keeps `IN_FORWARD` however much
turn is left; from a stand it is withheld only past 90 degrees
(`MODERN_SNAP_FORWARD_STAND`, states 2, 6, 7, 20) and handed over anyway after
`MODERN_STAND_FORWARD_TICKS` (8) ticks of standing, turning and arriving nowhere. She
steers in the air: in `AS_FORWARDJUMP`, the only airborne state whose routine reads a
turn bit, the heading moves toward the stick's course by `MODERN_AIR_TURN_RATE` 728 (4
degrees a tick, four times the classic 182) and the classic `IN_LEFT` / `IN_RIGHT` are
withheld so she is not turned twice. Under water the left stick moves her on a flat
circle relative to the camera, at `MODERN_SWIM_TURN_RATE` 2730 (15 degrees a tick) with
a pitch of `MODERN_SWIM_PITCH_RATE` 728 a tick, her swim attitude following the camera's
own. She can run while she fires. The yaw step the right stick gave the orbit on its
last tick, and only that step, is added to the held reference and the held course before
the course is read, so the camera the player turns takes her course with it. At Tank
nothing of it runs. A camera cut while the stick is held keeps her course until the stick
is let go (*A camera cut keeps the held course until the stick is let go* below).

**Why.** The scheme is the last piece the project was missing against the Remaster's own
control, and it survives a 1:1 engine because it only sets the classic bits: the
engine's `input` word carries the same bits at the same values, and TR5's state numbers
are the numbers the design was measured against. The held world course exists because
the obvious alternative does not work — reading the camera fresh every tick under a
chase camera that swings to stay behind Lara is a feedback loop that spins her in
circles. The direct turn exists because the automaton's fastest rate (1456, about 8
degrees a tick) does not read as the reference game; it is barred from the air, the
ladder, the water and every interaction pose because there `y_rot` belongs to the
animation and an outside hand on it puts a pose crooked. 30 degrees a tick is the rate a
thumb can circle the stick at (15 to 30); the older lid of 3277 sat below it, so a
faster circle broke out of the turn and she ran off or stopped, and a one-tick flick
above 90 degrees snapped a whole turn into a single frame where the reference takes six
ticks. Withholding forward past 45 degrees of turn dropped a running Lara into `AS_STOP`
for single ticks (203 of 937 run ticks, 25 stumble episodes); a gate that asks how much
turn is left can never open for a stick circling at exactly her own rate, so the second
question is asked in ticks, and a held stick never reaches that counter (thrown a half
turn and held she is inside the 90-degree gate by tick 4; over 128 measured runs the
counter reaches 3 of 8), which leaves the ledge the standing gate protects untouched.
The classic forward jump turns 1 degree a tick, 1 to 36 degrees over 40 ticks, where the
reference curves about 90 degrees around a pillar in one jump. Nothing in this engine
writes `lara_item->pos.y_rot` while she aims — only her arms and torso move, in
`larafire.cpp`, `lara1gun.cpp` and `lara2gun.cpp` — so the refusal to run while aiming
guarded a collision that does not exist here. The water's old 6 degrees a tick left up
to 59 consecutive ticks more than 45 degrees off the stick. Carrying the camera's own
player step is what keeps the re-latch rule honest: the held course re-latched only when
the left stick's own direction moved, which also ignored the camera when the player
moved it, so the course stood still and then paid the whole jump in one tick (in the
water 423 camera-steered pushed ticks gave 34 impulses, median 13.6 degrees, max 15.0 —
the water rate itself; running and sprinting, 617 ticks gave 57 impulses, median 13.2,
max 30.0 — the land rate, and under water every impulse also kicked the roll lean).

**How.** `game/modern_move.h` / `.cpp` (new) is the pure translation: eight numbers in
(stick x/y, camera yaw, Lara's yaw, a situation struct), a result struct out, no engine
header and no engine global, so the test compiles the engine's own file rather than a
copy. Its arc tangent is the C library's `atan2` scaled to the 16-bit convention — over
all 131,769 integer stick pairs a controller can produce the worst disagreement with the
engine's `phd_atan` is 5 of 65,536 units (0.03 degrees), against a smallest threshold of
384. `game/modern_state.h` / `.cpp` (new) is the one file that knows the engine: the
per-tick state, the reset on every break in the tick chain it can see, the blocked-wall
counter (measured from Lara's own position, never a collision internal, so a wall, a
crate and a corner read alike), the camera yaw of the tick taken from the camera's own
two points, the direct turn's one write to `pos.y_rot`, the air turn as the third arm of
the turn chain, and `MODERNLOG`. The seam is one call, `ModernUpdateInput`, in
`specific/input.cpp` between the block that puts the stick on the four direction bits
and the block that reads the key roles, guarded by `tomb5.control_modern &&
ControlMethod == 1 && !FirstPersonActive() && !bUseSpotCam && !GLOBAL_playing_cutseq &&
!bDisableLaraControl && lara_item && !InventoryActive && gfLevelId != LVL5_TITLE &&
!(input & IN_LOOK)`. `S_UpdateInput` is also called from eight menu loops, where the
direction bits steer a menu, so the seam tests `GlobalCounter`, which `game/control.cpp`
raises exactly once immediately before its own call: a call that does not follow a fresh
count leaves the input word untouched, and the tick right after a break in the chain is
skipped so the blocked counter is never measured against a position from before the
break. That counter cannot see a level change or a savegame restore, because between two
levels not one tick runs — so the held course is also forgotten by one line in
`game/gameflow.cpp` beside `InterpInvalidate("level start")`, which covers the savegame
restore as well. The inventory is deliberately not a reset: the world did not move while
the ring was open. The exempt-state table is `ModernStateIsClassic`
(`game/modern_move.cpp`), answering state by state from 0 to 138 — 95 classic, 44 modern
when the translation was built, eight of them retired again for the water. A direction
bit stays classic wherever its state's own routine in `lara_control_routines[]` reads it
as something other than "where do I want to go": which way to shimmy, whether to let go
of a rope, which way to push a block — the hangs and shimmies, the ladder, block push
and pull, the poses grouped as 89-104, and TR5's own extension past 104 (states
107-138). States 25, 26 and 27 (the back flip and the two side flips) are classic, read
out of the routines themselves: `lara_as_leftjump` sets `AS_TWIST` on `IN_RIGHT`,
`lara_as_rightjump` the mirror, `lara_as_backjump` reads `IN_FORWARD | IN_ROLL` to end
the flip early; 28 and 29 (`lara_as_upjump`, `lara_as_fallback`) read no direction bit
and stay modern. The constants, each with its origin in the file's own comment:
`MODERN_WALK_BELOW` 0, so the stick's own magnitude can never produce `IN_WALK` — the
pad has a dedicated walk key, and a stick still travelling toward the speed asked for
read 20 to 60 of 128 on the first tick of 19 of 32 pushes from rest and latched the
automaton into `AS_WALK` (the constant and `cfg->walk_below` remain, provable under an
explicit config); aim window `MODERN_AIM_WINDOW` 0x0400 with its hysteresis doubled once
aligned; spin enter 0x5000 and exit 0x2800 (turn on the spot above 112.5 degrees of
error, run again below 56.25); `MODERN_STAND_SPIN` 0x3000, because standing
`lara_as_stop` can only express "turn" without `IN_FORWARD`; `MODERN_STEER_STEP` 0x0180,
the noise floor, with `MODERN_RECENTER = MODERN_STEER_STEP`, so any stick movement above
that floor re-seats the course on the camera of that tick while a stick that is not
moving keeps its course whatever the camera does underneath it; `MODERN_AIM_SECTOR`
0x2000; blocked after `MODERN_BLOCK_FRAMES` 6 ticks of moving under `MODERN_BLOCK_EPS` 4
world units while asking to go forward and turn at once; `MODERN_TURN_RATE` 5461 with
`MODERN_TURN_RATE_VANILLA` 1456, `_MIDDLE` 2184 and `_TRONE` 3277 kept beside it as
named alternatives; `MODERN_DIRECT_TURN` 1, a second gate so a play test can separate
the direct turn from the scheme without a rebuild; full stick deflection 128; and five
build-time switches, `hold_course`, `back_turns`, `jump_dir` and `block_turn` on with
`MODERN_AIM_STRAFE` 0, each switching exactly one rule. `MODERN_STATE::last_course` /
`have_course`, `MODERN_RESULT::course_step` and `MODERN_SITUATION::cam_player_step`
carry the reversal and the camera's step, which `ModernTranslate`'s step 2a applies. The
direct turn is allowed in ten states: standing, walking, running, the three turns on the
spot, the wall splat, the landing, the sprint and `AS_COMPRESS` (0, 1, 2, 6, 7, 12, 14,
15, 20, 73). The translation stops aiming on the tick she jumps, so a side flip and a
back flip both survive firing; with no weapon drawn the directional jump stands aside
(`sit->drawn`) and the jump key gets the ordinary standing jump in the direction she is
facing. Under water, `ModernStateIsSwim`, `ModernWaterTurnRate` and `ModernWaterMove`
(`game/modern_move.cpp`) with the one write to the engine — turn, attitude, bank, stroke
— in `game/modern_state.cpp`; the surface rates are the engine's own, measured out of
`game/laraswim.cpp` and `game/larasurf.cpp` (surface turn 728, surface side 364, pitch
limits ±15470, swim roll 546), auto stroke on so the jump key is not needed to move, and
the pitch cap costs the modern dive 75 degrees against Tank's 85 because the pointer is
the camera's elevation with `pitch_home` taken out. `specific/input.cpp` / `.h` wire the
air turn into the turn chain and `specific/dxshell.cpp` supplies the stick. Byte
identity at the defaults holds by construction, and `tomb5_options tomb5;` is a global,
so `control_modern` reads 0 before any registry is opened. Seam behind `-log`:
`MODERNLOG` prints one `init` line per break in the tick chain and one line per real
logic tick — 30 lines a second at every `fps_mode`, `mode=off` earning a line like any
other, with `f=` being `GlobalCounter` so a gap in it is the scheme standing aside.
Tests: `Preparation/Tools/Diagnose/ModernTest.cpp` with `bmt.bat` links no game, no
level and no pad — it compiles the engine's own `game/modern_move.cpp` beside itself. It
holds the 32 ported expectations plus sweeps a play test cannot reach: 16,384 blocked
stick/camera/state combinations with no violation of "forward and a turn bit never go
out together while she is blocked" and the counter-proof beside it (the same sweep
unblocked turns while running in 1,208 of 2,048), 2,997 stick pairs inside a raised dead
zone silent, 10,240 ticks after a 180-degree camera cut with the held course unmoved,
eleven checks on what the level-start reset is worth, and the groups `reversal`,
`spin_sweep`, `air_turn`, `turn_chain`, `circling`, `firing_run` and `cam_steer`; 34
single-edit mutations of the scheme were tried against the finished tests and all 34
were caught. `TOMB5 Code/tomb5.vcxproj` lists the new files and
`Preparation/Tools/README.md` the test.

**Commits.** `9870a95`, `b675690`, `4b485da`, `8217cc4`, `7407748`, `005fafe`,
`ff1a5c7`, `2892abe`, `0876b5a`, `9dc99e4`, `0b995ca`, `0f7624a`, `7f554c7`, `10e147b`,
`8a2461c`, `98647aa`, `84dac52`, `6e1d404`, `df9e184`, `22827e1`, `189c963`, `d4e8808`,
`98f733e`, `6ece2ce`, `e3bf34b`, `4fec613`, `12eb4e0`, `d6d23fc`

### The modern camera: a free orbit on the right stick
2026-09-17 - 2026-09-20

**What.** With Modern control on, the right stick moves the camera on a sphere around
Lara. The angle the player dials is held as a WORLD yaw
(`MODERN_CAMERA_STATE::world_yaw`), so her own turning does not move the camera at all;
only the return does, at a half-life of 122 ticks (4.07 s) for a 90-degree offset while
she is moving, and eight times as fast while the eye is clipped. The engine's
`camera.target_angle` receives `world_yaw - lara.y_rot` every tick, so nothing
downstream changed. The stick reaches the eye in the same tick it is pushed: the eye is
turned about the target by exactly this tick's stick step before `MoveCamera`'s damping
sees it, and the elevation step goes straight into `camera.actual_elevation`. At a wall
the dialled angle stays and the distance shrinks — both orbit cameras keep exactly one
candidate, clipped by `mgLOS` at any distance, and the clipped point is walked back
toward the target along the ray until clearance is found rather than snapped onto the
sector grid. Below a measured eye-to-target distance Lara is not drawn at all. Fixed
level cameras are discarded exactly as first person discards them, with the rich2 sniper
camera excepted, and a discarded one leaves no trace: over a Camera tile the orbit keeps
its damping, a trailing eye is not pulled onto its ideal place in one tick, and the tile
edges are not counted as camera cuts; flybys and cutscenes are untouched in every mode.
A heading change of more than 150 degrees in one tick that the modern turn did not
cause hands the orbit a pending swing of half a turn, paid out over 20 ticks on an
eased profile and cancelled the moment the stick is pushed. The camera's own rate is a
setting: 0 to 100 maps linearly to 364..2184 yaw units per tick (60 to 360 degrees a
second), pitch always half the yaw, default 40 = 1092 units (180 deg/s), re-read every
tick so a change on the pause menu is felt the next tick. Under water the orbit's
elevation becomes the attitude her body swims toward. At Tank there is no orbit. Under
the rich2 sniper camera the stick reads against the sniper view, and a stick held through
the cut into or out of it keeps her course until it is let go (*A camera cut keeps the
held course until the stick is let go* below).

**Why.** The orbit is the camera half of the modern control: a scheme that steers
relative to the camera needs a camera the player aims. It is built on the engine's own
chase and combat cameras rather than as a new camera so that distance, damping,
collision and the interpolator keep working as they do, and the combat camera gets the
identical substitution because `game/larafire.cpp` sets `camera.type = COMBAT_CAMERA` on
every tick a weapon is ready or being drawn — an orbit built into `ChaseCamera` alone
would have stopped answering the stick the moment the player armed himself. An offset
from her heading swung the camera behind her whenever she turned (a 62-degree turn was
followed inside 20 ticks, overshooting to 80), where the reference camera stands still
while she spins under it; a world yaw minus this tick's heading is exact regardless,
where an offset carrying a per-tick heading delta drifts on every rounding and loses any
tick the orbit does not run. Without the lead the eye lagged the orbit by most of a
second: `MoveCamera` walks `pos += (ideal - pos) / speed` at speed 10 and the angle had
no damping of its own, so the eye needed about 25 ticks to reach the orbit's rate (47 %
at tick 10, 81 % at tick 20) and showed 418 units a tick against the orbit's own 1066;
with the lead the first-tick response is 99.9 % at both damping speeds and at both ends
of the speed setting. Feeding a wall's displacement back into the yaw wrote
`ChaseCamera`'s quadrant snap onto a world cardinal into the state, so the next tick
asked for the cardinal plus one stick step, was blocked again, and the stick was
swallowed — 290 of 855 ticks pushed against a wall did not advance or ran against the
stick, in episodes of up to 37 ticks. Above 60 degrees of elevation the ideal eye's
horizontal radius falls below 768 units (half a sector), close enough that the measured
eye-to-target vector reads as her own running direction: at the old pitch limit her
course jumped by more than 45 degrees on 57 ticks. The lead's arc grows with the radius,
and after a flyby, a cutscene, a room change or a released fixed camera the eye walks in
from thousands of units out — unscaled the lead reached 2856 units in one tick against
`INTERP_CAM_SNAP` 1024; scaled by the radius ratio the swept step is 407, 609 and 812
units at the three distances the engine sets (1536 behind her, 1024 in the puzzle
states, 2048 on the rope). Answers from the orbit must expire when the orbit stops,
because `ModernCameraActive()` reads the option, `lara_item`, the title, the spotcam,
the cutscene and first person but not which camera the engine is running, and a frozen
world yaw steered her along the bearing the chase camera had before a cut.
`CameraCollisionBounds` snapping a clipped point onto the sector grid made the eye's
picture and the movement reference disagree by 60 degrees or more on 481 of 930 pushed
ticks. The fixed-camera discard is decided at the top of `CalculateCamera` because taken
further down it would have handed the orbit the trigger's own object: a trigger carrying
a Camera and a Target command makes `game/control.cpp` set `camera.item` and the whole
shot is then built around that item — an orbit around a door at 1536 units with Lara out
of the picture; twenty floor triggers in the shipped campaign carry both commands on one
tile (Andrea1 4, Andrea2 1, joby2 1, rich1 4, rich2 3, rich3 7, counted over the
player's own `.trc` files). The discard kept the level camera from taking the view, but
two side effects of its trigger still reached the orbit. `TO_CAMERA` writes
`camera.speed` = the camera word's code bits + 1 (`game/control.cpp:2576`; the three
Camera tiles Lara crosses in Andrea1, rooms 19, 42 and 225, all carry 0x0000, so 1),
`RefreshCamera` sets `camera.type = FIXED_CAMERA` again on every tick she stands on the
tile (`game/control.cpp:2818`), and `ChaseCamera` forces speed 1 while
`camera.old_type` is FIXED (`game/camera.cpp:2178`-`2179`): speed 1 switches off the eye
damping in `MoveCamera` (`pos += (ideal - pos) / speed`) and the target damping in
`CalculateCamera` (`game/camera.cpp:1261`), so a trailing eye landed on its ideal place
in one tick — measured in Andrea1 on stepping onto room 42's tile, the eye-to-target
distance went from 1444 to 840 units in one tick. And `MoveCamera` records
`camera.old_type = camera.type` (`game/camera.cpp:2531`), which was FIXED during the
discarded pass; FIXED is outside the interpolator's smooth family
(`InterpCamSmoothType`, `game/interpolate_math.h:93`), so every entry and exit of the
tile counted as a camera cut and drew its tick live (INTERPLOG's `type=` count matched
her 12 crossings of room 19's tile exactly).

**How.** Origin: TROne's orbit holds a world angle too, and that is where the
resemblance stops — theirs never returns behind the player, this one falls home at
`MODERN_CAMERA_RETURN` whenever she is moving and the thumb is off the stick; nothing of
theirs is copied, and `game/modern_camera.h`'s own header says so before a line of
arithmetic. `game/modern_camera.h` / `.cpp` (new) hold the pure half and no engine
header: the state (`world_yaw`, `stick_step`, the last return step), the config with an
origin comment on each field, `ModernCameraUpdate` (the wall, then the stick, then the
return, in that order), `ModernCameraReset`, `ModernCameraHidesLara`,
`ModernCameraSwimPitch`, `ModernCameraSpeedRates` (the speed mapping, 18 or 19 units per
setting), `ModernCameraCarryStep` (a 64-bit numerator, because 3·n³·4096 passes 2³¹ at n
= 56) and `ModernCameraTickFollows` (the "exactly one tick apart" arithmetic including
the wrap the engine meets every thirty-six minutes). `game/camera.cpp` / `.h` hold the
seam in eight places: the axes (`ModernCameraStick`, fed from `specific/input.cpp` on
every poll, read once per logic tick), the tick (`ModernCameraOrbit`, from whichever of
`ChaseCamera` / `CombatCamera` runs), the angle (`camera.target_angle`, which
`UpdateCameraElevation` already adds to her heading), the height
(`camera.target_elevation`, replacing the two lines `ChaseCamera` opens with, because
those accumulate her slope pitch tick after tick), the wall, the draw
(`ModernLaraHidden`), the water (`ModernCameraSwimTarget`) and the log; plus
`ModernCameraLeadEye` (scaled down by the radius ratio whenever the eye is further out
than `camera.target_distance`), `ModernCameraRayBounds` (the walk back along the ray,
with `CameraCollisionBounds`' y-correction kept and the function itself untouched),
`ModernCameraCarryFromAnim` (its rule is "a heading change this turn did not make",
since `CreatureKill`'s grab, the lift, the electric grab and the ceiling trapdoor also
write `lara_item->pos.y_rot`), `ModernCameraArm`, and `ModernCameraWorldYaw` /
`ModernCameraPlayerStep`, which decline unless the `GlobalCounter` stamp
`ModernCameraOrbit` writes is exactly one tick old (`game/control.cpp` steps the
counter, then runs `S_UpdateInput` and `CalculateCamera` in the same tick body; the
subtraction is done in the counter's own unsigned width). `game/draw.cpp` gains one
term, `!ModernLaraHidden()`, on the single condition every path that draws her passes,
braid and shadow included. `specific/dxshell.cpp` hands the right stick over as axes
(`joystick_read_rx` / `ry`) rather than only as four buttons. `game/modern_state.cpp`
seats the movement reference on `ModernCameraWorldYaw()`, and in first person, where the
orbit does not run, `ModernWaterMove` asks for her own current attitude instead. The
constants: `MODERN_CAMERA_PITCH_MIN` -10923 (60 degrees above her), pitch max 15470 —
`ChaseCamera`'s own elevation clamp (84.98 degrees) — pitch home -1820, `ChaseCamera`'s
own resting elevation, never 0 because that line reads 0 as "unset";
`MODERN_CAMERA_RETURN` 377 of 65536 per tick with a floor of 4 so the fall really
arrives; wall factor 8 on the return, a 14.7-tick half-life while a wall lasts; hide
distance 122 = the near clip plane (20) plus her standing radius (102), the distance at
which the plane stops being in front of her and starts being inside her; invert pitch 0,
because the classic look camera already turns stick-forward into "look up"; pitch return
0; `MODERN_CAMERA_CARRY_FLIP` 27307 with `MODERN_CAMERA_CARRY_TICKS` 20;
`MODERN_CAMERA_PULL_STEPS` 8 and `MODERN_CAMERA_PULL_PUSH` 384; `MODERN_CAMERA_LEAD_MAX`
8192. Removed as refuted and not left standing: `ModernCameraNoteWall`,
`modern_cam_push` / `_push_at`, `wall_push`, `MODERN_CAMERA_FOLLOW_WALL`,
`MODERN_CAMERA_WALL_DEAD`, `MODERN_CAM_ANGLE_RADIUS`, `MODERN_CAM_CLIP_DEAD` and the
`angle_radius` fields; `wall` / `clip` is now the candidate loop's own `mgLOS` answer,
and the classic quadrant snap (3072 Manhattan) no longer fires in Modern at all. The
discarded level camera's two side effects are closed in `CalculateCamera`, both behind
`modernDiscard` (`game/camera.cpp:1115`), which is false under Tank, in first person
(`ModernCameraActive` refuses it, `game/camera.cpp:319`) and for the rich2 sniper camera
(`sniperCam`), so those stay as they were: right after the discard is decided the pass
sets `camera.speed = 10` (`game/camera.cpp:1130`-`1131`), the chase speed the reset at
the end of the function hands every pass (`game/camera.cpp:1393`); and the discard branch
runs `ChaseCamera` with `camera.type` set to `CHASE_CAMERA` and puts the level's type
back afterwards (`game/camera.cpp:1342`-`1346`), so `MoveCamera` records `old_type`
CHASE and `ChaseCamera`'s speed-1 rule stays quiet, while the reset's hold of a heavy
camera and `DiscardedFixedCameraTimer` see the type they saw before. Inside `ChaseCamera`
and `MoveCamera` nothing else reads `camera.type` or `camera.speed`: beside the speed-1
rule, the speed it hands `MoveCamera` (`game/camera.cpp:2181`) and the record, only
`MoveCamera`'s reuse of the last ideal compares
`camera.old_type == camera.type` (`game/camera.cpp:2403`), which now holds on a tile as
it does off one. On a look or combat pass the branch further down writes its own speed
over the 10. A known and accepted difference: `lara_as_switchon` and `lara_as_switchoff`
set `camera.speed = 6` (`game/lara.cpp:3176`, `3187`), and on a lever whose trigger also
carries a Camera command the orbit runs at 10 instead on the ticks the level camera is
discarded. The forced fixed camera Core uses for three animations - the floor trapdoor
lift (`game/traps.cpp:1061`), the ceiling trapdoor pull (`:1120`) and the turn switch
(`game/switch.cpp:606`) - is set as `FIXED_CAMERA`, with speed 1, by
`game/camera.cpp:1053`-`1058` and discarded under the orbit the same way (its
`camera.number` is -1, so it is never the sniper camera): its eye ran undamped for the
whole animation and its start and end counted as camera cuts, and now it keeps the
damping and counts no cut, a change the player sees - the orbit eases through a trapdoor
pull or a wheel turn instead of snapping. During the ledge hang's move and on the ten
ticks from its hand-over on, the chase camera's look-at point is looked up from the room
it was in the tick before and accepted from that room or from hers, so it follows her
down over the edge instead of standing still and then jumping (*Ledge hang: an automatic
turn and lowering at an edge* below). The orbit does not trip the 60 fps camera-cut
detection, swept rather than argued: over the whole sphere at the chase radius (about
128,000 sample pairs) the worst tick of a fully pushed stick moves the ideal eye 309
units Manhattan against `INTERP_CAM_SNAP` 1024, and `InterpCamAimCut` fires on none of
them. Seam behind `-log`: `MODERNLOG`'s init line carries `cam_return`,
`cam_return_min`, `cam_wall_factor`, `cam_hide_dist`, `cam_invert`, `cam_pitch_return`,
`cam_speed`, `cam_yaw_rate`, `cam_pitch_min`, `cam_pitch_max`, `cam_pull_steps` and
`cam_pull_push`; the tick line carries `cam_off`, `cam_pitch`, `cam_ret`, `cam_world`,
`clip`, `view_err`, `carry`, `cam_step`, `rs=` and `hide=`, and after them `cam_speed`
and `old_type` (`game/modern_state.cpp:659`, `667`), the camera fields one tick behind
because the line runs before `CalculateCamera` and `rs=` this tick's own; `hide=` is the
raw measured distance and not a flag, and two read-only accessors
(`ModernCameraLogState`, `ModernCameraLogConfig`) carry the numbers out rather than
exposing the orbit's statics.
The tick line's `cam_speed` is `camera.speed`, not the slider the init line's
`cam_speed` names: 10 after every `CalculateCamera` pass, a heavy camera the orbit
discards included (`game/camera.cpp:1131`); a heavy camera the orbit does not discard
(in first person, or the sniper camera when its trigger fires it as heavy) keeps the
trigger's speed while its timer runs (the reset at `game/camera.cpp:1390`-`1393` leaves
a `HEAVY_CAMERA` alone until then; the trigger decides heavy, `game/control.cpp:2578`-`2581`),
and a flyby writes its own 1 (`game/spotcam.cpp:701`, `742`, `788`); `old_type` reads 0
(CHASE) on a Camera tile whose level camera is discarded and 1 (FIXED), or 5 (HEAVY,
`global/types.h:455`) for a heavy camera, where a level camera takes the view
(`MoveCamera` records `old_type = camera.type`, `game/camera.cpp:2531`).
Tests: `Preparation/Tools/Diagnose/ModernTest.cpp` compiles `game/modern_camera.cpp`
beside `game/modern_move.cpp`, both whole, in the groups `camera_orbit`, `camera_speed`,
`camera_pitch`, `camera_wall`, `camera_hide`, `camera_seam`, `camera_lead`,
`camera_camcut`, `camera_stamp`, `camera_carry`, `camera_pull`, `camera_flip`,
`cam_steer` and `water_move`; `camera_lead` and `camera_pull` mirror `game/camera.cpp`
by hand rather than linking it, which is disclosed at both ends, and
`MODERN_CAMERA_LEAD_MAX` lives in `game/modern_camera.h` so the test reads the tree's
own number. `TOMB5 Code/tomb5.vcxproj` lists the new files.

**Commits.** `d2d4a34`, `3b26ba3`, `9a23208`, `b4c3bb9`, `6782cb9`, `04aa78a`,
`ccaedb1`, `23b8183`, `b7950a5`, `38925fd`, `d7a2259`, `405ad19`, `98f733e`, `15fb7bc`,
`4fec613`, `d6d23fc`, `2b6ac8e`, `9be2d31`, `be07363`, `c2112d7`

### A camera cut keeps the held course until the stick is let go
2026-09-26

**What.** With Modern control, a left stick held through a camera cut - into or out of the
rich2 sniper camera, the one level camera Modern keeps - leaves Lara on the course she was
running. The thumb still steers the held course, but from the cut until the stick is let
go its direction is read against the camera the course was seated on, not against the view
of the tick. Let go and pushed again, the stick seats a fresh course on the camera of that
tick: inside the sniper view it means the sniper view's directions, after the cut back the
chase camera's. With `-log` the `MODERNLOG` tick line ends in `cut=` and `latch=`.

**Why.** The held course is re-seated on the camera of the tick whenever the stick's own
direction has moved `recenter` (384 units, 2.1 degrees) from the direction it was last
drawn from (`MODERN_STATE::last_off`), and a thumb is never still: the median stick step
is 61 units a tick over the 2199 held ticks of the log below, so the drift from `last_off`
passes 384 within seconds even where single ticks move far less. Under a camera that moves
by itself the re-seat costs the little that camera moved. Across a cut the camera of the
tick is another camera: the seam hands the translation the orbit's world yaw under the
chase camera and the measured eye-to-target vector under the sniper camera, and the two
differ by the angle between the views. The measurement is a rich2 log through the sniper
camera (build `Sep 26 2026 12:18:13`, 2626 `MODERNLOG` tick lines, one init line, no gap
in `f=`): 38 changes of `old_type` between 0 and 1, 37 of them with the stick out on both
sides, the right stick at rest and `cam_step=0` at every one. The course survived the cut
itself only while the thumb had not yet drifted 384 units since the last seat. 36 of the
37 held cuts ended in a re-seat onto the other camera before the stick was let go - 10 on
the tick of the cut, 21 within four ticks, the last 48 ticks after it - and on each of
those 36 ticks `ref=` is the new camera's reference of that tick and the course jumps by
11,268 to 32,343 units, the angle between the two views, while the stick itself moved only
66 to 5629 units on that tick (16 of the 36 under 384). The 37th held cut was let go 11
ticks later without a re-seat. The cut back re-seated onto the first camera again: from
69875 ms the stick was held without a break to the end of the log, through 20 cuts, and
the course flipped after every one of them - the swing to and fro between the sniper view
and the chase camera. Under a camera with no cut in between the same re-seat is harmless
because the reference has hardly moved: the 41 re-seats under the chase camera and the 122
under the sniper camera inside holds seated on that same camera moved the reference by a
median 117 and 146 units.

**How.** The seam marks the cut: `game/modern_state.cpp:349` keeps whether the reference
of the tick is the orbit's world yaw (`ref_orbit`), and `:429`-`431` set
`MODERN_SITUATION::cam_cut` (`game/modern_move.h:583`) on the tick that source differs
from the previous tick's; the two statics behind it (`:67`-`68`) are cleared by
`ModernResetControls` (`:84`-`85`), so the first tick after a break in the chain is no
cut. The source switch is the signal rather than `camera.old_type` because the reference
is what the latch protects and the source is what hands it over, whatever camera type took
the view - a sniper camera fired as heavy (`old_type` 5) switches it the same way. On the
log the two agree tick for tick: the 405 ticks with the stick at rest show the reference
handed in, the orbit's on all 235 with `old_type` 0 and the measured vector on all 170
with `old_type` 1, and a replay with that rule reproduces all 2626 ticks, so the signal
falls on the 38 changes of `old_type` and on nothing else. The translation
(`game/modern_move.cpp`) sets the latch in its step 2b (`:1504`-`1505`) when a cut arrives
while a course is held with `hold_course` on, and the re-seat (`:1535`) then seats on the
held reference instead of the camera of the tick; the course minus the reference is
`last_off` all through a hold, so a latched re-seat turns the course by exactly the
stick's own change. `ModernRelease` clears the latch with the course (`:252`), the aiming
branch clears it because it reads the camera fresh (`:1579`), and `ModernResetState`
(`:313`) and the empty situation (`:1440`) start at 0. The rule has its own switch beside
the five, `MODERN_CUT_LATCH` 1 (`game/modern_move.h:424`, `MODERN_CFG::cut_latch` at
`:508`, filled at `game/modern_move.cpp:292`), and its state `MODERN_STATE::cut_latch`
(`game/modern_move.h:553`). `MODERNLOG`'s tick line appends `cut=` (`sit.cam_cut`) and
`latch=` (`cut_latch` after the translation) after `old_type=`
(`game/modern_state.cpp:659`, `:667`-`668`, the legend at `:625`). Tests:
`camera_cut_latch()` (`Preparation/Tools/Diagnose/ModernTest.cpp:6201`, run after
`sweeps()`) with its helpers `thumb` and `cut_twin` (`:6124`, `:6144`), 25 checks: the
log's case against a twin whose camera never cuts - a thumb wandering 60 units a tick out
to 600 either side, a reference jump of 29198 and a second cut of 12000 on top, the course
equal to the twin's on all 300 held ticks while the thumb re-seats it 30 times - the
release and the push seated on the new camera, the stick rolled far under the latch, the
cut back; the mirror of sweep (c) with the thumb walking 100 units a tick over 16,384
ticks; the player's camera turn under the latch; `hold_course` off and aiming. The suite
zeroes `cam_cut` at the 20 places it builds a situation.

**Checked.** A replay (kept outside the repository) compiles the engine's own
`game/modern_move.cpp` and drives `ModernTranslate` with each tick line's stick, state,
`cam_step` and reference - `cam_world=` where `old_type` is 0, `cam=` where it is 1 - and
compares `ref=`, `course=` and `cstep=` tick by tick. Against the translation of the
commit before, all 2626 ticks agree in all three, the 37 reference jumps of 8000 units or
more on held ticks included (the 36 above and one under the sniper camera at 98703 ms,
8373 units of the measured vector turning while she ran, inside a hold that had crossed a
cut). With this change and `cut_latch` forced to 0 the replay's per-tick output is byte
for byte the one before. With the latch on, the 37 cuts with the stick held all latch, the
one cut with the stick at rest does not, the latch stands on 1495 ticks in 7 holds, the
reference moves on none of them except by the player's own camera step, and no held tick
has a reference jump left; each of the 6 pushes that follow a release seats on the camera
of its tick, four inside the sniper view (17422, 25188, 43391 and 58500 ms) and two under
the chase camera (57360 and 69875 ms). ModernTest 10808 checks, 0 failed (10783 before and
25 new; every line the suite printed before is printed unchanged), and with `cut_latch` 0
in the test's own copy of the config the moving thumb of sweep (c)'s mirror leaves the
twin's course on 7424 of 16,384 ticks, the log's case on 150 of 150 with a largest step of
28788; MenuTest 35547 and InterpTest 635 checks, 0 failed; LipsTest 270 with the `.OBJ`
folder and 243 without, 0 failed - all as before. Not measured: the game.

**Unchanged.** Tank: `ModernUpdateInput` runs only behind `modern_seam`
(`specific/input.cpp:1289`, `:1420`), which needs `tomb5.control_modern`. The classic
camera, the HD path and the interpolator: no file of theirs changed. The sniper camera
itself as the level camera Modern keeps (`sniperCam` and `modernDiscard`,
`game/camera.cpp:1133`-`1135`), and the reference under it, the measured vector of the
sniper view. The re-seat under a camera without a cut, which takes the camera of the tick
as before. The player's own camera turn (step 2a), which moves reference and course
together, latch or not. The small course steps under the sniper camera between the cuts:
they are re-seats of the thumb against the measured vector, which turns as she runs past
the sniper's fixed eye (a median of 146 units of reference in a step of a median 934), and
they stay as they are.

**Decisions.** The latch drops when the stick is let go, on a tick the translation leaves
classic (which forgets the course anyway) and when aiming reads the camera fresh, and at
no other time; there is no timeout. While it stands, a stick rolled to a new direction is
read against the reference the course was seated on - the camera before the cut - and not
against the view on the screen; a direction relative to the new view is had by letting go
and pushing again. A second cut while the latch stands changes nothing, the cut back to
the first camera included. The signal is the switch of the reference source, not
`camera.old_type`. The latch ships on.

**Commits.** `dc21121`, `848528c`

### The pad: radial dead zones, a second binding set and a Modifier for chords
2026-09-18 - 2026-09-20

**What.** Modern control has its own pad table and its own way of reading the sticks.
Both sticks are read through `ModernStickDeadZone`, a radial dead zone with rescaling,
beside the per-axis square the hardware holds — the stick is dead in either. The widths
are chosen by `joystick_is_xinput`: XInput radial 31 on the left stick and 34 on the
right (the hardware's own 7849 and 8689 of 32767), per-axis 30 and 33; the DirectInput
fallback radial 24 on both, per-axis 18 and 20. A *Dead zone* slider (`tomb5.dead_zone`,
0 to 100, default 50, registry value `dead_zone`) scales all eight: at 50 it reproduces
the tuned constants exactly, at 0 there is no zone at all and at 100 twice the tuned
one; it is offered only under Modern, like *Camera speed*. The right stick becomes the
camera, so its four button roles (bits 16-19) are masked rather than raised and the
medipacks move onto the D-pad (bit 20 up, bit 21 down); the D-pad no longer reaches the
movement stick under Modern. Switching the control type repairs the bindings the new
scheme cannot honour and keeps every other binding the player made. The pad keeps two
complete binding sets, one for *Action* **One key** and one for **Separate**, the
setting seating one and parking the other. Any pad role may be a chord: a *Modifier*
role plus a button; twenty-eight pad roles exist — the sixteen movement and interaction
roles, the Modifier, the six weapons and the five cheat roles — and with the Modifier
unbound the pad behaves exactly as before. The value column prints `Mod+<Button>` for a
chord and `---+<Button>` for one whose Modifier is unbound, and bit 13 of the PS5
DirectInput page is named *Touchpad*.

**Why.** DirectInput's own zone was 500 of 1000 on the right stick and 150 of 1000 on
the left, and a per-axis zone zeroes each component on its own: the direction histogram
piled onto the four world cardinals (0: 68 ticks, 90: 41, 180: 48, 270: 43 against about
30 everywhere else) and emptied just beside them (80: 1, 170: 2, 260: 6, 350: 4), with
18 % of pushed ticks having one axis exactly zero. Over 360 sampled directions the
per-axis zone put 244 of them (68 %) exactly on a cardinal; the radial zone puts 4 of
360 there, which is unavoidable since the cardinals are themselves directions, and keeps
the direction to within a third of a degree. A radial zone alone covers only the disc
inside the square the hardware held, so every rest position in the square's corners —
out to 1.41 times the half-width — stopped being dead and a pad lying on the desk could
turn the camera by itself; asking both costs the axis snap nothing, because a push the
per-axis zone snapped is by definition one it let through and therefore always has a
component outside the square. The DirectInput widths are 24 and not the first 62: that
number was sized from a comment's claim, where the pad measures rests within 10 of 128,
so 24 is above the hardware's gate and below the shared 31, which had been throwing away
every real left-stick push between 18 and 31. The right stick cannot be a camera and
four buttons at once, and a scheme that takes bits away must leave the roles that sat on
them somewhere usable — without a repair, switching to Modern left the player with no
medipack buttons at all, and pressing *Reset* in Modern wrote bits 20/21 into the
registry so that back under Tank every D-pad Up tapped forward and ate a small medipack.
The stick repair is needed because `specific/dxshell.cpp` does not OR the D-pad onto the
stick's two axes, it assigns ±20 of 128 over them: under Tank that is right, because a
direction bit has no size, but under Modern the size is the speed and the direction is a
course — measured over the whole square of stick values with D-Pad Up held, 770 of
66,049 positions stopped her outright and 65,024 more had their course bent, by up to
176.7 degrees. Two binding sets keep a Separate player's layout and a One-key player's
apart. The Modifier exists because the pad has no free buttons left for fifteen hotkeys
and a chord costs none; the two XInput-ordered default tables name no spare button, so
their Modifier stays unbound rather than take one a player needs. A registry written
before the chords existed must not have the new mask laid over its buttons: that would
have put six buttons on two roles each — Look and Save both on R3, Sprint and Load both
on L3, Jump and Pistols both on Cross — and silenced every plain role while the touchpad
was held.

**How.** `game/modern_move.h` / `.cpp`: `ModernStickDeadZone` with its `axis_zone`
argument, `MODERN_DEAD_LEFT` 31, `MODERN_DEAD_RIGHT` 34, `MODERN_DEAD_LEFT_DINPUT` 24,
`MODERN_DEAD_RIGHT_DINPUT` 24, `MODERN_DEAD_AXIS_LEFT` 30, `MODERN_DEAD_AXIS_RIGHT` 33,
`MODERN_DEAD_AXIS_LEFT_DINPUT` 18, `MODERN_DEAD_AXIS_RIGHT_DINPUT` 20 — each per-axis
width the hardware's own number divided the way that path divides its axes and floored —
and `ModernDeadZoneUser(base, user) = (base*user+25)/50`, pure with both arguments
guarded, applied at the two `ModernStickDeadZone` calls in `specific/input.cpp`.
`specific/dxshell.cpp` keeps the left stick's own two axes (`joystick_read_lx` / `ly`)
beside the pair the D-pad writes over, exposes the D-pad as four digital buttons, and
publishes the raw axes `joystick_raw_lx` / `_ly` / `_rx` / `_ry` before every classic
gate on both paths, so the smallest effective zone is a true zero; `joy_x` / `joy_y`
still carry the D-pad so it still steers the menus and the binding page in either
scheme, and `joy_fire` still carries its four bits so they stay bindable. `dead_zone` is
the last field of `tomb5_options` (`global/types.h`), so no existing offset moves, and
`tomb5_options` is registry-only, so the new field changes no file format and no
savegame; `tomb5/tomb5.cpp` reads, writes and clamps it, and `game/modern_state.cpp`
prints the effective widths as `dead_l` / `dead_r` with a `dead_user=` field on the
`MODERNLOG` init line, beside a `MODERNLOG pad` line giving the four raw resting axes
and `xinput=` so the numbers can be re-measured on any pad. The modern set is derived
rather than hand-written: `jLayout[16..31]` copied roles 0-12 from the same
per-controller-style source `ApplyJoyDefaults()` resolves (refactored around one record
per style, `JoyDefaultRecord`) and overrode only the two medipack roles — checked side
by side for all four styles (`PS5-DInput`, `PS5-XInput`, `Xbox`, `Generic`), roles 0-12
identical and roles 13 and 14 the only ones that differ — and that unread snapshot was
retired once `ApplyJoyScheme` recomputed the repair on demand. `ApplyJoyScheme()`
(`specific/input.cpp`) is the switch's rule: a role bound to a bit the active scheme has
taken for something else, or still sitting on the other scheme's own default for itself,
goes to this scheme's default for that role, and every other binding is kept; Modern
cannot honour bits 16..19, Tank cannot honour 20..23; where freeing a bit collides with
a role that already holds it, the older role is unbound exactly as the binding page does
and then gets its own default when that button is free. It is idempotent and is
deliberately not `ApplyJoyDefaults()`. It runs from the Control-type row in
`specific/LoadSave.cpp` in both directions, and once inside `S_UpdateInput`'s `if
(tomb5.control_modern)` block for a session that starts in Modern with a table written
by Tank, which has to wait for a pad to really answer because `joystick_is_xinput` is
set by the poll and not by the init. One named cost: a binding the player chose that
equals the other scheme's default for that same role cannot be told apart from one
nobody repaired, so it follows the switch, and because the registry keeps the table but
not which scheme wrote it that one-time seat repeats on every launch; it can only ever
reach the two medipacks. `game/pad_scheme.h` is pure arithmetic with no engine include:
`JOY_ROLES` 28, `JOY_ROLE_SECOND` pinned to the literal 15, `JOY_ROLE_MODIFIER` 16,
`JOY_ROLE_WEAPON_FIRST` 17, `JOY_ROLE_CHEAT_FLY` 23 through `JOY_ROLE_CHEAT_SKIP` 27,
`PadSetSeat`, `PadBitDown` and `PadRoleFires` — one function deciding whether a role
fires, read by every pad producer, the medipacks, save/load and both hotkey families.
`specific/input.cpp` holds the four Separate default tables `jSepPS5DInput`,
`jSepPS5XInput`, `jSepXbox`, `jSepGeneric` beside the four untouched One-key tables,
plus `jSplitParked`, `joy_split_seated`, `JoySplitSet`, `JoySplitDefaults` and
`ApplyJoySplit`, which swaps the sets unconditionally and calls `ApplyJoyScheme()` only
once a scheme has been seated. The chord flag lives in its own array with its own
registry names per set (`JChord`, `SJChord`) and is never folded into the button value;
the Separate set has its own names `SJDck..SJBmd` / `SJSec` (`specific/registry.cpp`),
and `LoadSettings()` reads `SJChord` unconditionally and lets only its absence decide,
because `REG_ReadLong` writes its default when a name is missing and
`specific/winmain.cpp` calls `LoadSettings` a second time whenever the first returns
`REG_Setup` 0, which a virgin registry always does. Capture, in `game/menu_frame.cpp`
and `specific/LoadSave.cpp`: with the Modifier held the next other button is stored as a
chord, a plain binding and its chord twin coexist as two rows, and binding the Modifier
onto a used button clears every role there. Defaults on the two DirectInput-numbered
tables (PS5 DirectInput, generic): Modifier on the touchpad click, quick save
Modifier+R3, quick load Modifier+L3, flare Modifier+R1, the four weapons
Modifier+Cross/Circle/Triangle/Square, HK, crossbow and the cheats unbound; the two
XInput-ordered tables keep their earlier plain defaults, and `game/gameflow.cpp` carries
the button names. Format compatibility runs both ways: an older build ignores
`JChord`/`SJChord` and reads the chord roles as plain bindings, and one *Reset to
defaults* restores a whole row in either build. Proved by a table walk that printed all
four styles through both directions of the switch and by a sweep holding three
properties the rule may never break — idempotent, no two roles sharing a button
afterwards, no role left on a bit the scheme cannot honour — over both directions, all
four styles and four starting tables; `Preparation/Tools/Diagnose/ModernTest.cpp`'s
`deadzone` group sweeps the whole 257 × 257 domain for all four pairs and answers zero
points that were dead before and are alive now, and its D-pad block catches all eight
D-pad readings and nothing else in a sweep of 66,049 stick pairs, with the counter-proof
that the raw ±20 would have made her walk; `Preparation/Tools/Diagnose/MenuTest.cpp`
covers the role tables.

**Commits.** `052783c`, `d10d67d`, `5ea2d28`, `52ec301`, `251a440`, `b118f59`,
`88ae528`, `38925fd`, `98f733e`, `6ece2ce`, `ebb48fd`, `fb4e770`, `d69e74d`, `f331108`,
`b3930c8`, `83e56f7`, `0f1602b`, `89427f4`

### The pad's scheme repair never displaces the Modifier
2026-09-27

**What.** Flipping the *Control type* row repairs the pad bindings the new scheme cannot
honour (the entry above); that repair no longer unbinds the Modifier. A role whose
repaired default is the Modifier's own pair is left unbound instead, remembered in its
set's yield mask, and seated on its default again by the next repair that finds that
default free and not blocked. On the PS5 DirectInput page at *One key* the small
medipack now waits while the Modifier holds the touchpad under Tank and is back on d-pad
up under Modern, and every chord keeps its Modifier through both flips.

**Why.** With the Modifier on the touchpad and chords on some hotkeys, a flip to Tank and
back left the binding page showing every chord as `---+L3`, `---+L2`, `---+R1`
(`game/menu_frame.cpp:1347`-`:1352` prints `---` for an unbound Modifier), and a
Touchpad+button press bound the plain touchpad, because the capture records a chord only
while a Modifier is bound and otherwise takes the highest bit down, which the touchpad's
bit 13 is (`:2007`-`:2031`); a `-log` run read `PADLOG mod=-1`. The mechanism is in the
tables and the repair: at *One key* on that page Tank's Small Medipack default is bit 13
(`jDefPS5DInput`, `specific/input.cpp:189`) and Modern's is d-pad up, 20. Into Tank the
medipack on 20 is on a blocked bit - Tank keeps the d-pad moving Lara - and goes to 13;
the repair's displacement then found the Modifier on (13, plain) and gave it its own One
key default, which is none. Back into Modern the medipack follows the switch to 20, and
nothing gave the Modifier back. The Separate DirectInput row escapes the collision by
leaving both medipacks unbound (`game/pad_scheme.h`, `JOY_SEP_PS5_DINPUT`); the One-key
Tank row does not.

**How.** The repair's rule moves out of `ApplyJoyScheme` into `game/pad_scheme.h`, pure
arithmetic like the rest of that header, so that MenuTest runs the engine's own rule:
`PadSchemeRepair` (`:246`-`:320`) with `PadPairTaken`, `PadChordSet` and
`PadModifierHolds` beside it. Part 1 is the repair as it was. Part 2: before a role takes
its default, `PadModifierHolds` asks whether the Modifier holds that pair; if so the role
is set to -1, its chord flag cleared and its bit set in the yield mask (`:262`-`:269`),
and the displacement loop skips the Modifier (`:281`) - a role displaced there whose own
default the Modifier holds is remembered the same way. Part 3 (`:298`-`:319`): every role
in the mask that is still unbound is seated on its default when the bit is not blocked
and the pair is free; a role the player has bound in the meantime leaves the mask.
`ApplyJoyScheme` (`specific/input.cpp:403`) calls it on the active set (`:424`). The mask
is `jYield` beside `jChord` and `jSplitParkedYield` beside `jSplitParkedChord`
(`:264`-`:265`), exchanged with them by the Action row (`ApplyJoySplit`, `:533`-`:539`),
cleared by *Reset* (`:346`), reached as `JoyYieldSet` (`:479`), and stored beside
`JChord`/`SJChord` as `JYield`/`SJYield` (`specific/registry.cpp:67`-`:71`, read at `:532`
and `:610`, written at `:732`-`:733`), so a medipack waiting under Tank at shutdown still
returns after a restart into Modern. Behind `-log`, new lines (`specific/input.cpp:427`-
`:438`): `PADLOG repair role=<n> default=<bit> held by the modifier, left unbound` when a
role yields and `PADLOG repair role=<n> seated=<bit>` when it is seated again; the
`PADLOG mod= chord= split=` line is unchanged.

**Checked.** `Preparation/Tools/Diagnose/MenuTest.cpp` gains the family
`pad_scheme_repair`, 6,184 checks, 36,180 -> 42,364, 0 failed. It holds the four One-key
rows, the blocked masks, the Modern medipack override and the call itself against the
lines of `specific/input.cpp`, and keeps a copy of the rule as it ran before as the
reference. (a) One key, PS5 DirectInput, Modern seated, Modifier 13, medipacks 20/21,
chords on pistols, the guns cheat and the skip cheat: into Tank the Modifier stays 13,
the chord flags are unchanged, Small Medipack -1 and remembered, Large Medipack 14, roles
0-12 as Tank's; the reference unbinds the Modifier there; a second repair in Tank changes
nothing. (b) Back into Modern: Small Medipack 20, Large 21, Modifier 13, the mask empty,
the whole set as it started. A role the player binds while it waits keeps the binding and
leaves the mask; a Modifier moved off 13 under Tank lets the next repair seat the
medipack on 13. (c) No Modifier bound: Small Medipack 13 under Tank and 20 under Modern,
the whole set equal to the reference's, no yield. (d) The parked Separate set is
untouched by One-key flips, and its own flips equal the reference's with its Modifier on
13 and its Small Medipack -1 under Tank and 20 under Modern. (e) The PS5 XInput and Xbox
pages: medipacks 18/19 under Tank and 20/21 under Modern, equal to the reference's. A
sweep of every role on every bit and unbound, on all four pages, both flips and no
Modifier, equals the reference in all 5,400 cases; with the Modifier on each of bits 0
to 15, on all four pages and from both schemes, a flip and its return never move it and
give the starting set back (640 checks). ModernTest 13992, InterpTest 651 and LipsTest
270 / 243 unchanged. `PCtomb5 built Sep 27 2026 22:22:56`, md5
`31138e80e332736d7ff14d698c277d96`. Not measured: the game.

**Unchanged.** Without a bound Modifier the repair gives exactly what it gave before.
The Separate set, whose Tank medipacks are unbound, repairs as before. The defaults
themselves, the capture and the binding page. Play input: the repair runs only on a
scheme flip, on an Action flip once a scheme is seated and once at a Modern start, never
per frame.

**Decisions.** The repair never displaces the Modifier: it is the anchor of every chord,
and one default outranks none of them. A role left unbound for it is remembered per
binding set, in the registry beside the chord masks, and seated again by the next repair
that finds its default free. The defaults stay as they are: Tank's One-key Small
Medipack keeps bit 13 wherever no Modifier holds it. A Modifier on a bit the new scheme
blocks is still repaired like any other role, and *Reset* at One key still clears a
player's Modifier.

**Commits.** `8a9d181`, `268e664`

### The keyboard: two binding sets, bindable hotkey roles, and the look key's turn on the spot
2026-08-30 - 2026-09-20

**What.** The keyboard keeps two complete binding sets, one per control type, both
persisted: the classic (Tank) set stays under the registry names `Key0..Key33` and a
Modern set lives under `MKey0..MKey33`. Switching *Control type* seats one and parks the
other, so Modern bindings survive a flip to Tank and back. The Modern default row is a
hand-on-the-left layout around W/S/A/D: forward W, back S, left A, right D, duck C, dash
Left Shift, walk Left Alt, jump Space, action E, draw Q, flare F, look R, roll Left
Ctrl, inventory Escape, step left Z, step right X, pause P, select Return. Fifteen keys
the engine used to read directly from the keyboard became rebindable roles: the small
and large medipack, quick save, quick load, the six weapons of `DoWeaponHotkey`, whose
order is fixed and no longer level-dependent (1 pistols, 2 shotgun, 3 uzis, 4 revolver,
5 HK, 6 crossbow), and the five cheat roles; their defaults are the keys the engine already
answered to — 0, 9, F5, F6, 1-6, F2, F7, F8, F11, F1 — identical in both rows but for
quick load, which is F9 under Modern and F6 under Tank. *Reset to defaults* restores the
active scheme's own defaults and recomputes the conflict table. Under Modern the four
arrow keys and Return also drive any menu that owns the input. Separately, while the
look key is held with no weapon ready, the modern translation is bypassed and `IN_LOOK`
itself is kept from the engine for those ticks (latch `modern_look_tank`), so
`LookLeftRight` and `LookUpDown` never eat the direction bits and `AS_TURN_L`,
`AS_TURN_R` and `AS_FASTTURN` run with their real animation — Tank turning on the spot
inside the modern scheme. On such a tick the stick is read by sector: `ModernTankSector`
lets exactly one axis through, at the stick's magnitude rather than its component —
inside `MODERN_TANK_TURN_SECTOR` (9102, 50 degrees either side of the horizontal) a pure
turn, outside it a pure step, with no diagonals. With a weapon ready the sector stands
aside and the classic look keeps the bits.

**Why.** At base `DoWeaponHotkey` let two of its four keys change meaning on
`gfLevelId`: key 1 drew the pistols early and the HK later, key 2 the shotgun early and
the crossbow later. `gfLevelId` is the level's identity, not its position, so under a
rolled level order "from the 13th Floor on" means "in levels that happen to have a high
id", scattered unpredictably through the run - and because the weapons are scattered
too, Lara can carry an HK in the first level, where no key draws it, and the pistols in
the last, where key 1 reaches for the HK. Keys 5 to 8 were free in play; only the
save/load screen uses 1 to 4, which is a different context. At base `SaveSettings()`
wrote `Key0..17` from the active row, so the first
exit in Modern would have overwritten the classic keys with the Modern ones. A
hard-wired `keymap[DIK_*]` read cannot be rebound, and the two keyboard name tables
named none of `DIK_F1..DIK_F12`, so eight of the new default keys would have printed a
null pointer on the binding page. The reset never called `CheckKeyConflicts()`, so
`conflict[]` stayed stale after a reset until the next capture. The look-key bypass is
needed because on a bypass tick the classic word is built from `move_x` / `move_y` with
`> 8` / `< -8` per axis — Core's own arithmetic, written to sit behind the hardware's
per-axis zone — while in the modern scheme the pair has come through the radial zone
instead, so 8 of 128 is a window of about 3.6 degrees either side of the exact
horizontal, where a thumb pushes "left" at a median of -100 to -106 degrees. Of 962
pushed tank ticks 680 came out LEFT+BACK, 115 RIGHT+BACK, 85 LEFT+FORWARD, 20
RIGHT+FORWARD and only 57 as a pure side, and `lara_as_stop` lets `IN_BACK` and
`IN_FORWARD` overwrite the turn goal, so the turn could not be started and she stepped
backwards instead; the camera has no part in it, the pattern being the same at every
camera offset. Diagonals are refused because this mode exists to place her exactly, and
the magnitude is used because the component shrinks toward the sector's edge and would
fall back under the very threshold the sector exists to clear. With a weapon ready the
look bit survives twice over — the `IN_TARGET` swallow releases it from the seventh tick
of a hold (`LookCnt` 100), and `game/control.cpp` puts it back with `input |= IN_LOOK`
on every tick of a binocular or lasersight session — and on exactly those ticks the
classic direction bits are what aim the head, the binoculars and the crosshair, where
one surviving axis would make a diagonal impossible.

**How.** `game/key_scheme.h` is pure arithmetic with no engine include:
`KEY_SCHEME_ROLES` 34, `KEY_ROLE_SECOND` pinned to the literal 18, `KEY_ROLE_MEDI_SMALL`
19 through `KEY_ROLE_CHEAT_SKIP` 33, `KEY_SCHEME_MODERN_ROW`, `KeySchemeConflicts`
(Core's own double loop run against the active scheme's default row, with arguments
saying whether the Secondary role and the five cheat roles are live, since an inert
role's key must claim nothing), `KeySchemeSeat` and `KeySchemeRowSound`.
`specific/input.cpp` holds `short layout[4][KEY_SCHEME_ROLES]` — row 0 Core's Tank
defaults unchanged, row 1 the active set every existing reader and writer already uses,
row 2 the Modern defaults, row 3 the parked set — plus thirty-four `static_assert`s
tying the header's bare scan codes to their `DIK_` names, `key_scheme_seated`,
`KeyDefaultRow`, `KeySchemeSet(long modern)` and `ApplyKeyScheme()`; `Key()`'s fallback
reads `KeyDefaultRow()` instead of `layout[0]`, and ten hard-wired `keymap[DIK_*]` reads
became `Key(role)` one for one with the same guard, timer and ammo-cycle condition.
`KeyboardButtons` and `GermanKeyboard` now name F1, F2, F5-F9 and F11; F3, F4, F10 and
F12 stay nameless on purpose, because the outfit changer
(`specific/hd/hd_outfitmenu.cpp`), `IN_E` and `specific/output.cpp` own them and a
capture must keep refusing them. `specific/registry.cpp`'s `LoadSettings()` stores the
two sets by name while Tank is still seated, because it runs in `WinMain` before
`tomb5.control_modern` is read; the one-time seat is a single `ApplyKeyScheme()` call in
`specific/gamemain.cpp` after `init_tomb5_stuff()`, and `SaveSettings()` writes `Key*`
from `KeySchemeSet(0)` and `MKey*` from `KeySchemeSet(1)`. `MenuResolveKeybinds`
(`game/menu_frame.cpp`) builds the keyboard list into a static buffer: the sixteen base
roles, *Secondary* only under *Separate*, the ten hotkeys, and the five cheat rows only
while *Cheats* is on; `specific/LoadSave.cpp` carries the capture. A block in
`S_UpdateInput` ORs the four arrow keys and Return into
`IN_FORWARD/IN_BACK/IN_LEFT/IN_RIGHT/IN_SELECT` under `tomb5.control_modern &&
ModernMenuReadsPad()`, reading `keymap[]` raw because those five are the menu's own
fixed keys. One measured collision is left as it is and written down beside the row:
Modern's walk key is Left Alt, and the title screen (`game/gameflow.cpp`), load/save
(`specific/LoadSave.cpp`) and the outfit changer suppress confirm while Left Alt is
down, which is the guard that keeps Alt+Enter from choosing a row while it toggles
fullscreen; Escape still leaves and nothing locks. The look-key bypass lives in
`specific/input.cpp`: the latch, the bypass condition moved up to where the stick is
read so the sector and the classic assembly answer the same question once
(`modern_tank`), `modern_tank_aim` gating the sector call alone, the sector call with
its one-tick static, and the tank log line; `ModernTankSector` and its two constants —
`MODERN_TANK_TURN_SECTOR` 9102 and `MODERN_TANK_SECTOR_HYST` 1456 (8 degrees), which
keeps a thumb resting on the boundary from alternating at 30 Hz — are in
`game/modern_move.h` / `.cpp`, `ModernLookTankActive()` is the predicate the seam and
the log share, and `specific/input.h` with `specific/dxshell.cpp` carry the stick
reading it sits behind. The bypass costs no extra tick on release: it is one tick behind
at both ends, the same latency the plain `input & IN_LOOK` gate had. Tests:
`Preparation/Tools/Diagnose/ModernTest.cpp` has `look_tank` (ten held ticks against ten
bypassed ones) and `tank_sector`, whose boundary walk checks the two constants
themselves and asks the two edges strictly against the plain boundary, because every
other assertion in the group derives its expectation from the constant; both groups
state in writing that they mirror `specific/input.cpp` and `game/camera.cpp` rather than
linking them. `Preparation/Tools/Diagnose/MenuTest.cpp` with `bmenu.bat` covers the role
tables, `TOMB5 Code/tomb5.vcxproj` lists the new header, and `docs/Engine-Manual.md`
carries the layouts.

**Commits.** `951a4ac`, `6ece2ce`, `9c74cdc`, `57d2783`, `a841773`, `51e22a4`,
`a6ecbb5`, `ea110cc`, `fdb288d`, `81a8827`, `b3930c8`, `89427f4`

### The mouse: the modern camera's second input, five bindable buttons and two rows
2026-09-27

**What.** Under *Control type: Modern* the mouse turns the orbit camera beside the right
stick, and a camera turned with the mouse takes her held course with it exactly as the
stick's does. The five mouse buttons - left, right, middle and the two side buttons - are
bindable on the keyboard's *Key bindings* list in either scheme and named *Mouse L*,
*Mouse R*, *Mouse M*, *Mouse 4* and *Mouse 5* (*Maus L* and so on in German). The Modern
default row draws on the right button, looks on the middle one and takes the secondary
action on the left one; Q, R and Numpad 0 leave the row. Two *Controls* rows follow *Dead
zone*: *Mouse sensitivity*, 0 to 100 with 50 as the default, and *Invert mouse*, off by
default. The cursor is hidden and held inside the client area while the window is active,
Modern is on and no FMV plays; with `-log` the `MOUSELOG` seam records the counts, the
steps and every change of the capture.

**Why.** The Remaster's modern keyboard page binds draw, look and the secondary action to
the mouse and turns the camera with it. The engine read no mouse at all: the
`WM_MOUSEMOVE` case of `WinMainWndProc` stored `mouseX`, `mouseY` and `mouseB` into
statics that nothing read, and `Key()` read `keymap` alone, which is why
`game/key_scheme.h` called Q, R and Numpad 0 placeholders for the three roles.

**How.** Raw Input: `WinCreateWindow` registers usage page 1, usage 2, no flags, the game
window as target (`specific/winmain.cpp:822`-`838`), so the mouse arrives as `WM_INPUT`
while the window is in the foreground. `WinMouseInput` (`:506`) folds the button flags -
button n down on bit 2n, up on bit 2n + 1 - into a five-bit mask of the buttons held and
one of the buttons pressed since the last take, whether captured or not, and adds the
move to two counters with `InterlockedExchangeAdd` (`:569`-`572`) only while captured; an
absolute report (a tablet, a remote desktop) is turned into pixels and differenced
against the previous position, the first one after a capture only seeding it. The dead
`WM_MOUSEMOVE` stores are gone. The window procedure runs on the main thread inside
`WinProcMsg`'s `GetMessage` loop (`:154`), which does nothing else once `GameMain` is
started on its own thread (`:1000`), so `WM_INPUT` is handled at the mouse's report rate
whatever the frame rate. The game thread takes what gathered with `WinMouseTake` (`:589`),
an `InterlockedExchange` of both counters to 0 and the pressed mask ORed with the held
one, once per `S_UpdateInput` (`specific/input.cpp:1192`) - once per logic tick in play
(`game/control.cpp:293`) - so the counts of every drawn frame between two ticks reach the
one tick at 30, 60 and the display's own rate alike, and a click shorter than a tick still
counts for one. No lock: everything the two threads share is a `LONG` changed through the
Interlocked functions, and a lock would be a hazard of its own, because `WM_ACTIVATE`
suspends the game thread with `SuspendThread` and a suspended holder would never let go.

The capture, `WinMouseCapture` (`:436`), runs on the main thread only, because
`ShowCursor`'s counter belongs to the thread that owns the window: captured is
(window active) and (Modern) and not (FMV) and not (closing). Catching the cursor calls
`ShowCursor(FALSE)` until the counter is below 0 and counts the calls; letting it go calls
`ShowCursor(TRUE)` exactly that many times and `ClipCursor(0)`, so the counter ends where
it was found and a full screen that kept it at -1 stays hidden. While captured the clip
rectangle is set again on every call, which `WM_MOVE` and `WM_SIZE` make. The game
thread's two terms come from `WinMouseWant` (`:578`), called at the top of every
`S_UpdateInput` (`input.cpp:1191`) with `tomb5.control_modern` and `App.fmv` and posting
`WM_APP_MOUSE` only on a change, so the *Control type* row, the start of a video
(`specific/fmv.cpp:230`, then the player's own `S_UpdateInput`) and its end reach the
window thread at once. The window's term comes from `WM_ACTIVATE` as its first statement,
ahead of the FMV guard (`:650`-`655`), from `WM_SETFOCUS` and `WM_KILLFOCUS`
(`:642`-`648`), and from `WM_SIZE` (`:625`-`632`), which takes it afresh as "the
foreground window and not minimised": a window restored from the taskbar gets its
`WM_ACTIVATE` while still minimised, which reads as inactive, and a later `WM_SETFOCUS` is
not certain, so the restore itself seats the term again. Losing the term also clears both
button masks, because the up messages go to another window. The releases, each logging a
line: the window going inactive or being minimised, the focus leaving, *Tank*, a video,
`WM_CLOSE` (`:742`) and `WinClose` (`:758`), since a clip holds for the whole system.
While the cursor is not captured the counts are dropped where they arrive and the
buttons still count, as a keyboard key does.

The orbit term is pure arithmetic in `game/modern_camera.cpp`. `ModernCameraMouseGain`
(`:154`) is the gain in 512ths of a unit per count, 16384 * 2^((s - 50) / 25) for a
sensitivity s of 0 to 100: a table `pow25` of round(4096 * 2^(r / 25)) for r = s mod 25,
shifted left by s / 25 (0 to 4), so the gain is 8, 16, 32, 64 and 128 units a count at 0,
25, 50, 75 and 100 - 8192, 4096, 2048, 1024 and 512 counts a full turn of 65536 - and
every step of the slider is the same 2.8 per cent. 512ths and not 256ths because the
bottom end is a quarter of the middle and a quarter of every table value has to stay whole
for the doubling to be exact. `ModernCameraMouseSteps` (`:242`) makes yaw = dx * gain /
512 and pitch = -dy * gain / 512, the invert flipping the pitch alone, so a mouse pushed
away looks up as the stick pushed forward does. The numbers are this engine's own: 2048
counts is about 13 cm of desk at 400 counts per inch and 3.3 cm at 1600.

A step is bounded at 2048 units each way a tick (`MODERN_CAMERA_MOUSE_STEP_MAX`,
`game/modern_camera.h:365`), 11.25 degrees, 337.5 degrees a second at 30 ticks, because
the 60 fps interpolator calls a tick a camera cut, and draws it without interpolation,
when the eye moves more than `INTERP_CAM_SNAP` (1024 Manhattan) or the aim does
(`InterpCamAimCut`). The camera cut sweep in ModernTest measures the bound on both axes at
once at the three target distances the engine sets: 485, 726 and 967 at 1024, 1536 and
2048, no aim cut, the led eye 709 and walking in 954, while 2176 on both axes already
measures 1027 with 648 aim cuts at 2048. The stick's own top, 2184 a tick at *Camera
speed* 100, is about the same. The counts beyond the bound are not dropped:
`ModernCameraMouseSpend` (`modern_camera.cpp:199`) spends at most the counts of one
bounded step a tick - 64 at sensitivity 50, 16 at 100, 256 at 0 - and owes the rest to
the next tick, so a brisk sweep turns the camera as far as the hand went, spread over a
few more ticks. What is owed is bounded at half a turn of steps
(`MODERN_CAMERA_MOUSE_OWED_MAX`, 1024 counts at 50), sixteen ticks of the largest step,
so the camera goes on turning for at most about half a second after the hand stops; the
counts past that are dropped. `S_UpdateInput` keeps the two owed counts
(`input.cpp:747`), forgets them while the cursor is not captured (`:1278`), spends, and
hands the two steps to `ModernCameraMouse` (`:1284`-`1288`, `game/camera.cpp:369`)
inside the Modern block that also hands over the stick (`input.cpp:1215`);
`ModernCameraOrbit` moves them into `MODERN_CAMERA_STATE::mouse_yaw` and `mouse_pitch`
(`game/modern_camera.h:439`-`440`) before `ModernCameraUpdate` and clears its own copy
(`camera.cpp:537`-`540`). The update counts a nonzero mouse step as steering
(`modern_camera.cpp:729`-`730`), so it stands the return still and cancels a carried
swing as the stick does; takes `stick_step = ModernWrap(rate(rs_x) + mouse_yaw)`
(`:740`); adds `mouse_pitch` after the stick's pitch term, before the clamp; and clears
both steps (`:752`-`754`). Nothing else in the camera
changes: `ModernCameraPlayerStep` hands `stick_step` to her held course
(`game/modern_state.cpp:417`), and the eye's lead takes the world yaw's step, the mouse's
included. The first person view does not take the orbit's input: `FirstPersonCamera`
looks along `lara.head_x_rot` and `head_y_rot` plus her own rotation
(`game/camera.cpp:3005`), `ModernCameraActive` refuses the orbit under it (`:394`) and the
modern seam excludes it (`input.cpp:1378`), so the mouse does not turn it.

The buttons are key codes above every scan code: `KEY_MOUSE_FIRST` 0x100 to
`KEY_MOUSE_X2` 0x104 in Raw Input's order left, right, middle, X1, X2
(`game/key_scheme.h:85`-`91`), with `KeySchemeIsMouse` (`:93`), `KeySchemeMouseBit`
(`:99`) and `KeySchemeMouseName` (`:108`), which has the key tables' two columns, German
and every other language. `Key()` (`input.cpp:1094` for the binding, `:1120` for the
default row's fallback) reads the button mask this `S_UpdateInput` took for a code at or
above 0x100 and `keymap` below it; a code above 0x104 answers 0 instead of reading past
`keymap`. The `static_assert`s say the Modern row's slots 9, 11 and 18 are the right, the
middle and the left button and that the codes start above `keymap` and fit a short
(`:604`, `:606`, `:613`, `:616`-`617`). `KeySchemeConflicts` compares codes, so a button
claims and is claimed as a key is. The page names a button before it indexes the key
table (`game/menu_frame.cpp:1319`), whose entries past 0xFF are Core's old joystick names
and would call the right button "Joy 2". `MenuSelectBinding` notes the buttons already
down (`:1369`) and `MenuCaptureBinding` takes a button newly pressed during the wait
(`:1963`-`1976`): the press and not the release, one role per press, a key in the same
frame first, the lowest button on a tie, on the keyboard page only. The Modern row is
`game/key_scheme.h:163` and `:165`; Tank's row has no button. The registry keeps `Key*`
and `MKey*` as longs, so a code is stored as it is (`specific/registry.cpp:470`); a
registry that already holds `MKey9`, `MKey11` and `MKey18` keeps the player's keys there,
the buttons answering beside them through `Key()`'s default-row fallback, and *Reset to
defaults* seats the new row. A set saved by this build is not safe for an engine that
predates the mouse codes: that one indexes its 256-entry `keymap` with the code and reads
past the end, and its bindings page names the buttons with Core's joystick names; *Reset
to defaults* in that engine puts its own row back. *Combat mode* at *Hold* needs nothing
new: its seam (`input.cpp:2139`) reads `IN_DRAW` as `Key(9)` left it (`:1601`), a level
read of the
mask, so a held right button keeps the weapons out - read from the code, not seen in
play.

The rows: `MenuStepMouseSensitivity` (`game/menu_frame.cpp:525`) steps through
`MenuSliderStep` (`game/menu_nav.h:179`), the rule Camera speed and Dead zone write out by
hand, and `MenuStepInvertMouse` (`:541`) flips the bit; the rows stand at `:1483`-`1484`
as a `MENU_SLIDER` and a `MENU_VALUE_IF`, both `MENU_SAVE_TOMB5` and `MenuEnabledModern`,
which makes *Controls* twelve rows, its `MENU_MAX_ROWS`, still one page. Labels in the
string table (`game/gameflow.cpp:1392`-`1393`, German *Mausempfindlichkeit* and *Maus
invertieren*, the values the table's own *on* and *off*); fields
`tomb5.mouse_sensitivity` and `tomb5.mouse_invert` (`global/types.h:2862`, `:2868`);
registry values `mouse_sens` and `mouse_invert`, written on a first run, read with 50 and
off for a missing value and 50 for one over 100, and saved (`tomb5/tomb5.cpp:304`-`310`,
`:525`-`531`, `:704`-`708`). The seams behind `-log`: `MOUSELOG f=<tick> dx=<counts>
dy=<counts> buttons=0x<mask> yaw=<units> pitch=<units> sens=<0..100> captured=<0/1>
owed_x=<counts> owed_y=<counts>` once per `S_UpdateInput` on which the mouse moved, a
step was handed over (owed counts paid out with `dx=0 dy=0`) or the mask changed
(`input.cpp:1309`), and `MOUSELOG capture=<on|off> active=<0/1> modern=<0/1> fmv=<0/1>`
on every change of the capture (`winmain.cpp:480`).

**Checked.** ModernTest 13965 checks, 0 failed (10827 before, 3138 new: 3133 in
`camera_mouse`, `Preparation/Tools/Diagnose/ModernTest.cpp:9124`, with
`mouse_course_step` at `:9087`, and 5 in `camera_camcut` at `:7529`): the gain at both
ends and the middle, its doubling every 25 steps and its even 2.8 per cent over the whole
slider, the steps and their bounds, the spend at every sensitivity - the largest spend
stays inside the step bound and one count more would pass it, a jolt leaves at most half
a turn owed - the owed counts paid out tick by tick, set against a move back and capped,
a sweep of 100 counts a tick for 20 ticks paid out whole at no more than the bound, the
invert swept over counts and sensitivities, a mouse tick against a stick tick of the same
rate at 16 headings - world yaw, elevation and the step to the held course equal - and
the held course taking that step whole, the stick and the mouse adding up, a mouse step
as steering that cancels a swing and stands the return still, the hand-over cleared, the
elevation clamp and the wrap past the half turn; and the bound swept against the camera
cut rule at the three target distances, for the ideal, the led eye and the eye walking
in, with the step 128 units over it shown to cut. MenuTest 36180 checks, 0 failed (35547
before; `key_mouse` at `Preparation/Tools/Diagnose/MenuTest.cpp:656` adds 275 - the
codes, the predicate over every short, the bits, the ten names, the Modern row with its
three buttons and still sound, the conflict rule with buttons - and `controls_mouse_rows`
at `:1151` adds 358, reading the Controls table, the two rows' kind, gate and save class,
their five strings and the registry names out of the tree and sweeping `MenuSliderStep`;
the Modern row's Secondary check now names the left button and the Controls paging
checks say twelve rows).
InterpTest 635 and LipsTest 243 without the `.OBJ` folder and 270 with it, 0 failed, as
before. The engine built as `PCtomb5 built Sep 27 2026 12:19:02`. Not measured: the game.

**Unchanged.** *Tank* with an untouched registry: the capture's Modern term is 0, so the
cursor is never caught and no count is ever gathered; no step is handed to any camera,
the hand-over standing inside `if (tomb5.control_modern)`; Tank's row has no button, so
`Key()` reads `keymap` for every default exactly as before. The pad: its axes, the stick's
dead zone and `ModernCameraStick` are untouched. `hd_enable` 0 and `fps_mode` 0: no file
of theirs changed. The menus and the inventory keep their own navigation, keys and pad;
the mouse drives none of them. Every existing log line prints as before.

**Decisions.** Raw Input (`WM_INPUT`) carries the counts and the buttons, not
DirectInput's mouse device. The mouse turns the camera under *Modern* only; *Tank* keeps
its classic look. The mouse does nothing in the menus and the inventory. The three Modern
defaults are the Remaster's keyboard page: draw on the right button, look on the middle
one, the secondary action on the left one. A full circle takes 2048 counts at the middle
of the slider, and the slider spans a quarter to four times that; the yaw and the pitch
share one gain, and the mouse's pitch invert is a setting of its own. The camera turns no
faster than 2048 units a tick with the mouse, so that the 60 fps interpolator never
takes a mouse turn for a camera cut; a faster hand is paid out over the following ticks,
at most half a turn behind, rather than drawn at 30 Hz. The capture has no term
for the F4 outfit window: that window is drawn with `PrintString` and driven by `dbinput`
(`specific/hd/hd_outfitmenu.cpp`), not an ImGui window, and wants nothing of the mouse,
so it is a menu like the pause menu and the cursor stays caught under it.

**Commits.** `fa1028b`, `8d0f5b7`, `8236d73`, `26484e8`, `d12e07e`

### The movement keys as the modern stick
2026-09-27

**What.** Under *Control type: Modern* the four movement keys are the modern control's
left stick: forward, back, left and right each push one axis to full deflection, so the
forward key runs Lara where the camera looks and the other three move her as the stick
pushed that way does, on the keyboard page and on the pad page alike. The tick line of
`MODERNLOG` ends in `feed=pad`, `feed=keys` or `feed=none`.

**Why.** The scheme translated only the pad's left stick. Its seam was closed on the
keyboard page, where `ControlMethod == 1` was a term of `modern_seam`, and on the pad page
a stick at rest left the keys' classic bits standing. So a keyboard player under Modern
moved as at Tank - the forward key ran her along her own heading, left and right turned
her - while the mouse turned the camera round her.

**How.** `ModernKeyStick` (`game/modern_move.cpp:240`, declared at
`game/modern_move.h:666`) is pure. With the pad's pair off centre it hands that pair on
unchanged and answers `MODERN_FEED_PAD`; otherwise it makes x = (right ? 128 : 0) minus
(left ? 128 : 0) and y = (back ? 128 : 0) minus (forward ? 128 : 0), 128 being
`MODERN_STICK_FULL`, and answers `MODERN_FEED_KEYS`, or `MODERN_FEED_NONE` for 0/0
(`modern_move.h:197`-`199`). The sign is the pad's: a stick pushed forward reads y
negative on both paths (`specific/dxshell.cpp:889` on DirectInput, `-ly / 256` at `:1016`
on XInput, the d-pad's up -20 at `:902` and `:1022`), the classic assembly reads
`tank_y < -8` as `IN_FORWARD` (`specific/input.cpp:1460`) and `ModernTranslate` takes the
stick's angle from (x, -y) (`modern_move.cpp:1518`). The call stands in the seam itself
(`input.cpp:1539`-`1542`), inside `if (modern_seam && !modern_tank)`, reads the same four
`Key(0..3)` as the classic bits above it (`:1464`-`1474`) and writes a local pair,
`seam_x`/`seam_y`, so `move_x`/`move_y`, `tank_x`/`tank_y` and the classic assembly keep
the pad's own reading. `ControlMethod == 1` leaves `modern_seam` (`:1385`); the pad's pair
is kept out on the keyboard page instead: the block that zeroes it asks
`tomb5.control_modern && !ModernMenuReadsPad()` (`:1347`) and applies the stick's dead
zone only under `ControlMethod == 1` (`:1359`), so a controller resting on the desk, or a
d-pad press that reads +-20 in `joy_x`/`joy_y`, cannot steer a keyboard player. The seam's
action and jump reads carry the term themselves: `JoyRole(4)`, `JoyRole(JOY_ROLE_SECOND)`
and `JoyRole(3)` count only under `ControlMethod == 1` (`:1516`-`1519`), as at every other
`JoyRole` call site, so a connected pad's button cannot put a keyboard player into aiming
or a directional jump. The keys' classic bits stay in the word: when the translation
takes the vector, the one write in `ModernUpdateInput` (`game/modern_state.cpp:561`)
replaces `MODERN_MOVE_MASK` as it does for the pad, and when it steps aside - a classic
state, a ladder, a push block - they stand. The look key's bypass is the same code as
before: `modern_tank` (`input.cpp:1393`) stands the seam aside and the keys' classic bits
turn her on the spot. The walk key is still read after the seam. `ModernUpdateInput`
takes the source as a sixth argument (`game/modern_state.h:49`, `modern_state.cpp:150`),
and its tick line (`modern_state.cpp:672`) ends in `feed=%s` through `ModernFeedName`
(`:140`). The field is not called `src`: the line's `src=` already says whether the
translation wrote the word (`pad`) or stood aside (`keys`), and keeps saying it.

The keyboard page now runs the whole seam, and with it two things the pad page already
had: with no weapon ready the look key is taken out of the word (`input.cpp:1836`), so
held it steers her as described above instead of turning the classic look camera, and
`ModernHeadingAfterInput` answers on the keyboard page too, so the orbit swings after a
roll or a jump's twist as it does for a pad player.

**Checked.** ModernTest 13992 checks, 0 failed (13965 before, 27 new in `key_stick`,
`Preparation/Tools/Diagnose/ModernTest.cpp:4448`): the forward key alone reads
0/-128, the pair of a stick pushed straight forward and of the pad's rim after its dead
zone; it gives the bits, course and mode of the pad at full forward and at the XInput
reading of 127; the four keys and four diagonals translate exactly as the stick at the
same heading under 16 camera headings and 8 of hers; a diagonal reads a magnitude of 128,
not 181, and lies on exactly 0x2000, 0x6000, -0x6000 and -0x2000; a pad pair off centre
is handed on unchanged under all 16 key sets; both at rest feed nothing and the
translation stands aside; opposite keys cancel on their axis; and `key_seam_model`
(`:4416`), a model of the guard chain read against `input.cpp` like `look_tick`, shows the
key stick consulted only under Modern, in play and without the look key, never at Tank,
and a d-pad press on the keyboard page not taken for movement. MenuTest 36180,
InterpTest 635 and LipsTest 243 without the `.OBJ` folder and 270 with it, 0 failed, as
before. The engine built as `PCtomb5 built Sep 27 2026 12:54:11`. Not measured: the game.

**Unchanged.** *Tank*: `tomb5.control_modern` is the first term of the zeroing block and
of `modern_seam`, so neither is entered and `ModernKeyStick` is never called. The pad
page under Modern with the stick off centre or no movement key held: the block, the seam
and the call hand `ModernUpdateInput` the same pair as before. The menus and the
inventory: `modern_seam` still refuses the inventory, the pause menu, the load and save
screens, the outfit window and the title, and the arrows and Return still step them
(`input.cpp:1725`).
The first person view: `!FirstPersonActive()` is still a term of the seam. Every existing
log line prints as before, the tick line only longer at its end.

**Decisions.** The four keys are a full-deflection stick, fed to the seam's one call and
nowhere else. The pad wins: its stick off centre is taken and the keys are not added. A
diagonal of two keys is exactly 45 degrees, the magnitude clamp making it full. The look
key still bypasses the translation, so the keys turn her on the spot as at Tank. The keys
give no half deflection; the walk key walks. The keys still write their classic bits, so
every state where the scheme steps aside behaves as before. Opposite keys cancel to
nothing, so the forward-and-back roll chord fires only while no side key is held.

**Commits.** `2dffffb`, `b9b8a0f`, `26c1fba`, `0cfbdf8`

### Two action keys: the interaction bit and the Secondary role
2026-09-20 - 2026-09-20

**What.** A *Controls* setting **Action**, **One key** by default (the engine's own
behaviour: one button both interacts with the world and fires) or **Separate**. Under
*Separate* the *Action* role interacts with the world — switches, pickups, doors,
pushable blocks, the pulley, use-item triggers — and a new *Secondary* role does the
climbing, grabbing, hanging, monkey swinging and firing, so a player can hold one button
to bring the weapons up and press the other to fire. *Secondary* has a keyboard role
(slot 18, Tank default Home, Modern default Numpad 0, registry `Key18`/`MKey18`) and a
pad slot (`jLayout[15]`, registry `JSec`, default -1 = unbound); under *One key* the
role produces no bit, is not offered on the *Key bindings* page and claims no key in the
conflict table.

**Why.** The unused bit was there: `IN_A = 0x4000` was declared and read nowhere in the
tree, so it is retired and `IN_INTERACT` takes its place at the same value. Under *One
key* the one action key raises both bits in a single statement, so the engine is
byte-identical at the default by construction rather than by argument. Two state
routines had to change with it: `LaraAboveWater`'s `input &= ~IN_ACTION` is followed by
`TestTriggers()`, whose `TO_CUTSCENE` case reaches five interaction sites, and left as
it was it would have let those five triggers fire where the base engine cannot; and
`lara_col_poledown` substituted the new bit inside a composite equality, which is sound
while the right-hand side is one bit and wrong when it is two — under *Separate* only
ever one of the two arrives, so the test was true on every tick and sliding down a pole
was reachable by no key at all.

**How.** `global/types.h`: `IN_A` retired, `IN_INTERACT = 0x4000`, `IN_ACTION_ANY =
IN_ACTION | IN_INTERACT`, `bool action_split`, with `specific/registry.cpp` storing it.
The two producers in `specific/input.cpp` / `.h` set `IN_SELECT | IN_ACTION |
IN_INTERACT` at one word under *One key*, and `IN_SELECT | IN_INTERACT` for *Action*
with `IN_ACTION` alone for *Secondary* under *Separate*. The `IN_ACTION` reads in
nineteen files are split three ways: 35 read `IN_INTERACT` alone (`game/switch.cpp` 13 —
every switch but `JumpSwitchCollision` — `game/pickup.cpp` 6, `game/control.cpp` 5 in
`NeatAndTidyTriggerCutscene`, `game/door.cpp` 4, `game/moveblok.cpp` 3, `game/traps.cpp`
`FloorTrapDoorCollision`, `game/flmtorch.cpp` `FireCollision`, and the two state
routines `lara_as_ppready` and `lara_as_pulley`); 20 read `IN_ACTION_ANY`,
so neither key is ever the wrong one — `game/objects.cpp` (`PoleCollision` twice,
`TightRopeCollision`, `ParallelBarsCollision`), `game/rope.cpp` `RopeCollision`,
`game/deathsld.cpp`, `game/switch.cpp` `JumpSwitchCollision`, `game/traps.cpp`
`CeilingTrapDoorCollision`, and every state routine in `game/lara.cpp` and
`game/rope.cpp` that holds her on a pole, a rope, the parallel bars or a death slide,
since a state entered by an either-key collision routine but held by `IN_ACTION` alone
would throw her off the moment *Secondary* let go; and the rest stay `IN_ACTION`, which
under *Separate* is the *Secondary* key's — her grab/climb/hang/monkey/swim automaton, the
sixteen weapon reads, the binocular and laser-sight reads and the swim cheat.
`LaraAboveWater`'s mask clears `IN_ACTION_ANY` under *One key* and `IN_ACTION` alone
under *Separate*; `lara_col_poledown` is two mask tests now, the shape `lara_col_poleup`
already had. The inventory ring's confirm (`game/newinv2.cpp`) reads `IN_SELECT`, with
Core's `IN_ACTION` term kept behind `!tomb5.action_split`. `MenuResolveKeybinds`
(`game/menu_frame.cpp`) drops the *Secondary* row of either list under *One key*,
`KeySchemeConflicts` (`game/key_scheme.h`) takes an argument saying whether the role is
live, `game/pad_scheme.h` pins `JOY_ROLE_SECOND` to 15, and
`Preparation/Tools/Diagnose/MenuTest.cpp` and `docs/Engine-Manual.md` carry the rule.

**Commits.** `d86b8a6`, `b29adbf`, `0f1602b`, `661de4a`, `2fb27d5`, `2ae3ba9`

### Combat, Sprint, Duck and Grab: hold or toggle
2026-09-20 - 2026-09-21

**What.** Four settings on a *Modes* sub-page of *Controls* (German *Modi*), each
defaulting to 0 = the engine's own behaviour, each read on both control types and for
keyboard and pad alike. **Combat mode** defaults to *Toggle*, the engine's own, where
the draw key draws and holsters in turn; at *Hold* the weapons stay out while the key is
held and are holstered when it is released. **Sprint mode** and **Duck mode** default to
*Hold*; at *Toggle* a press latches the bit and the next press releases it. **Grab
mode** defaults to *Hold*; at *Toggle* she keeps hold without the button — a ledge, both
shimmies and the corner turns, the monkey swing, the climb and ladder states, the pole,
the rope, the zip line and the parallel bars — and a fresh press lets go. Registry names
`combat_hold`, `sprint_toggle`, `duck_toggle`, `grab_toggle`.

**Why.** Nothing in the base engine debounces the draw key: `IN_DRAW` has two producers
and no consumer reads its edge, so what limits it is Lara's own animation and a key held
down really does draw and holster without end. A latch must not manufacture behaviour
the held key does not have: the sprint latch drops when she stops or when the dash timer
ends the sprint, and the duck latch must survive a crawl, since `ANIM_DUCK_TO_ALL4S`
sets `lara.gun_status = LG_HANDS_BUSY` and that would otherwise drop it the instant she
goes down. The grab latch may not ask about `bUseSpotCam`: `InitialiseSpotCam` leaves
that flag standing through an ordinary flyby, and `game/control.cpp` holds
`bDisableLaraControl` at 1 for as long as a `GF_OFFICE` level's flipmap is above 10 — in
both windows the player still has control, and a *Toggle* player would have lost
`IN_ACTION` and fallen where a *Hold* player hangs on. The let-go press needs a mask of
its own: thirteen of the family's thirty-five states test `IN_ACTION_ANY`, and under
*One key* both bits leave the producer together, so clearing `IN_ACTION` alone left them
reading "still holding on" — on a rope or in an `AS_PBSPIN` there is no other way out,
so she swung or spun until a reload.

**How.** The arithmetic is pure and lives in `game/modern_move.cpp` / `.h`:
`ModernWeaponBits` (one request per tick on which the weapon state and the key disagree,
none while the animation runs, self-correcting on the next tick), `ModernModeLatch` /
`ModernModeLatchDrop` with `modern_latch_bits` keeping `MODERN_LATCH_ON` and
`MODERN_LATCH_KEY` apart, `ModernGrabLatch(..., bit, release, bits)` and
`ModernStateIsGrab`. All of it is applied at two seams at the end of `S_UpdateInput`
(`specific/input.cpp`): the mode latches immediately before the weapon seam and after
the walk-sidestep rewrite, so a sidestepping player is not read as pushing nothing; the
weapon seam after the `IN_RIGHT | IN_LEFT` cancel, so it sees `linput` as every other
consumer does. The weapon seam's guard is `!App.fmv` (a bit held through the outro video
would have saturated `specific/fmv.cpp`'s rising-edge skip on
`IN_OPTION|IN_DRAW|IN_SELECT` and made the video unskippable) plus `lara_item`,
`GLOBAL_playing_cutseq`, `bUseSpotCam`, `bDisableLaraControl`, `InventoryActive`,
`gfLevelId != LVL5_TITLE` and `lara.gun_type != WEAPON_TORCH`, so a burning torch passes
through untouched and can still be thrown by `game/flmtorch.cpp`; the sprint and duck
latches drop on those eight terms, the grab latch asks the seven without `bUseSpotCam`.
The grab family is 35 of Lara's 140 animation states — fifteen read plain `IN_ACTION`
(hang, both shimmies, both hang turns, the overhead hang, the four monkey-swing states,
five of the six ladder states), thirteen read `IN_ACTION_ANY` (pole 99-103, rope
90/91/111/114/115, `AS_SLIDEROPE` 113, the zip line 70, the parallel bars 128), six read
no input and are in the family only so the bit cannot blink off mid-transition
(`AS_CLIMBEND` 59, `AS_CLIMBROPE` 112, the four corner shimmies 107-110), read out of
`game/lara.cpp` and `game/rope.cpp`; the thirty-fifth is `AS_LEDGEHANG` 139, the *Ledge
hang* move's turn and lowering (its own entry below), a transition into `AS_HANG` that
begins with the key held and is a grab from there on. Delivery is `IN_ACTION` alone; the
let-go mask is composed at the seam as `IN_ACTION_ANY` under *One key* and `IN_ACTION`
under *Separate*, and `IN_SELECT` is never in either; while a let-go press lasts the
whole mask stays clear wherever she is, so under *One key* that press also picks nothing
up and operates nothing until the button has been up once. The duck-and-crawl family is
twelve states — duck, its roll, both duck turns, all fours, crawl forward and back, both
crawl turns and the three pickup states reachable from a duck or a crawl — and the latch
holds through all of them; a low ceiling still keeps her down through `lara.keep_ducked`
exactly as a held key does. All three latches drop through `InputModeLatchReset()` at a
level's priming tick (`game/gameflow.cpp`), so a savegame load gets it too, and through
the menu rows' own step functions; a key held across a level change is not a fresh
press, because `ModernModeLatchDrop` clears `MODERN_LATCH_ON` and leaves
`MODERN_LATCH_KEY` standing. With both toggles off the whole mode seam is skipped by one
`if (tomb5.sprint_toggle || tomb5.duck_toggle)`, and `if (tomb5.combat_hold)` guards the
weapon seam the same way, so the binocular and laser-sight zoom, which read `inputBusy &
IN_SPRINT`/`IN_DUCK` on a held tick, are unaffected. The four settings live in
`global/types.h` and `tomb5/tomb5.cpp`; the rows sit on `MENU_PAGE_MODES`
(`game/menu_frame.cpp` / `.h`), opened by a centred tab on *Controls* after *View*, and
kept their callbacks, save class and `MENULOG` row names when they moved there. Seams
`COMBATLOG` and `MODELOG` behind `-log`. Tests in
`Preparation/Tools/Diagnose/ModernTest.cpp` and
`Preparation/Tools/Diagnose/MenuTest.cpp`; `docs/Engine-Manual.md` carries the four
rows.

**Commits.** `60df63c`, `2d9fc8a`, `712c833`, `c750b0d`, `7e5f9a2`, `d509eea`,
`39efaa2`, `b448d37`, `cb55d72`, `da66ba8`, `9a26fbc`, `83e56f7`, `c50ffae`, `e0c9651`,
`8747fe6`, `eafab87`

### Ledge hang: an automatic turn and lowering at an edge
2026-09-24

**What.** A fifth row, *Ledge hang*, on the *Modes* sub-page alongside Combat,
Sprint, Duck and Grab mode (`game/menu_frame.cpp:1419`, `MENU_VALUE_IF`, a new
macro beside `MENU_VALUE`/`MENU_SLIDER` that puts a predicate in a value row's
`enabled` slot), registry name `ledge_hang`, default Off, read and written at
the same three sites `grab_toggle` is (`tomb5/tomb5.cpp:284`, `:488`,
`:671`), greyed under Tank controls by the same `MenuEnabledModern` predicate
the Controls-page camera sliders use. With it on and Modern controls active,
running, walking, or standing and pushing toward an edge with the grab key
held (`IN_ACTION` — the action key under *One key*, the Secondary key under
*Separate*) stops her on the sector's standing line, turns her half round on
the spot, and lowers her into the settled hang by playing Tomb Raider 5's own
pull-up animation (97) backwards, instead of letting her run off. The turn is
her own turn-on-the-spot animation at the direct turn's rate:
`MODERN_LEDGE_HALF_TURN` (0x8000) over `MODERN_LEDGE_TURN_TICKS` (6) ticks,
5461 or 5462 units a tick, `MODERN_TURN_RATE` (5461, 30 degrees) to within a
unit, so the half turn takes 0.2 s (`game/modern_move.h:439`, `:440`), turning
to the side `MODERN_LEDGE_TURN_SIDE` (1, right) names (`:428`); the lowering
steps the pull-up down `MODERN_LEDGE_FRAME_STEP` (3) frames a tick (`:422`);
the standing line sits
`MODERN_LEDGE_STAND_IN` (99) units inside the sector's edge and the hang
`MODERN_LEDGE_HANG_OUT` (100) units past it, the engine's own catch-snap
distances; `MODERN_LEDGE_RESIDUAL` (15) is the most a walk or a stand may lie
off the line and still start the move; `MODERN_LEDGE_SNAP` (0x18E2) is the
catch's own 34.6-degree cardinal window (`:433`, `:443`-`:445`);
`MODERN_LEDGE_NEAR` (512) is how close an edge has to be before `LEDGELOG`
reports the refusal set (`:449`). The nine refusals, each one engine test asked the way its
own site asks it (`LaraLedgeRefusals`, `game/lara.cpp:6150`-`6317`, with the
legend at `:6115`-`6142` and the bit values at `game/lara.h:197`-`205`):
`SHALLOW` the drop is 768 units or less; `SLOPE` the ledge floor's two front
corners differ by 120 or more; `SLIDE` the floor there would start
`TestLaraSlide`; `OBJECT` an item, static, or the catch's own `hit_static` is
in the way; `CLIMB` a climbable wall sits under the edge; `CEILING` the hang
position's ceiling is too low or would catch her into the monkey hang instead;
`CATCH` the heading is outside the cardinal window, there is no floor, or the
engine's own edge catch itself fails; `WATER` water, lava, or a death floor
lies below; `LARA` her weapons are drawn, she is dead, a creature hit her this
tick, or a cutscene holds her. The triggers in the floor data of the sector she
hangs in are not asked: the engine's own catch hangs her in that same sector, and
`LaraAboveWater` hands its `coll->trigger` to `TestTriggers` every tick of the hang
(`game/lara.cpp:5017`), so a camera, music, door or flipmap trigger there fires after
the move exactly as after a catch made by hand; a lava or death floor below is
`WATER`, a ladder below `CLIMB`.
With the option on, holding the grab key while running off an edge the move
accepts no longer reaches for a ledge below the way `lara_as_forwardjump` does
(`game/lara.cpp:864`-`872`: the action key sets `goal_anim_state =
AS_REACH` once she is already falling) — the new trigger catches her before
she leaves the ledge; a sprint and a refused edge still reach below. The phase (`modern_ledge_phase`) and the
heading she hangs with are appended to `tomb5_save_info` as `ledge_phase` and
`ledge_heading` (`global/types.h:1399`-`1400`) and written/read only while she
is in the move (`game/savegame.cpp:103`, `:116`-`117`); a save that cannot go
on this way is settled once by the same refusal test into the hang or a fall.
Under the Modern orbit the camera swings round with her turn, ending behind her
back over the drop where the walls allow it, and keeps looking at her all the
way down: its look-at point follows her out over the edge and down the lowering
in even steps, never standing still and never jumping.
`LEDGELOG` behind `-log` prints one line per phase change, a line when the
refusal set changes while the key is held near an edge, and one fire line when
the move starts (`game/lara.cpp:6441`-`6479`, `:6760`-`6773`, `:6797`-`6807`),
and, under the Modern orbit, one `LEDGELOG cam` line per tick of the move and
on the ten ticks from the hand-over on (`ModernCameraLedgeLog`,
`game/camera.cpp:938`-`984`, printed at the end of `CalculateCamera`,
`:1404`-`1405`): `f=` `GlobalCounter`, `phase=` turn, lower, hang (the
hand-over's tick) or tail, the eye and the look-at point as the pass left them
and the point before the quarter smoothing (`raw=`), each with its room,
`item_room=`, `snaps=` `TargetSnaps`, `rejected=` 1 when the in-room test put
the previous point back, `head_fallback=` 1 when `ConfirmCameraTargetPos` took
the head position, `angle=` and `elev=` the actual angle and elevation,
`world=` the orbit's world yaw, `carry=` the swing still owed / its ticks paid,
`lead=` the yaw step asked of `ModernCameraLeadEye` / the step it gave after
its distance scaling, `clip=` the dialled eye's `mgLOS` clip, `pull=` the step
of eight at which `ModernCameraRayBounds` found clearance (0 none),
`mc_pull=` 1 when `MoveCamera`'s own `mgLOS` pulled an eye in, and `csnaps=`
`CameraSnaps`.
Under *Grab mode* *Toggle* the grab is carried through the move: from the tick
it fires she keeps hold whatever the key does; a fresh press while she turns
stands her back up, a fresh press still down on the tick of the hand-over lets go of
the ledge there, and a short press during the lowering that is up again before
the hand-over changes nothing. Under *Hold* nothing changes: the lowering runs
to the hand-over whatever the key does, and the hang then needs the key as
every hang does.

**Why.** The design is this project's own and plays Tomb Raider 5's own
pull-up, animation 97, backwards. The pull-up can be played backwards because
during it `item->pos` never moves — only the root offsets do, and the whole
climb resolves in the one `TranslateItem` at the animation's wrap — so
counting the frame down and undoing that one translate at the start
reconstructs it exactly, which is why the state's own guard in
`game/laramisc.cpp`'s `AnimateLara` is enough (`laramisc.cpp:502`-`505`). The option defaults off because it changes
what holding the grab key while running off an edge already does — the
`AS_REACH` catch below — and a player who relies on that should not have it
silently replaced. It is Modern-controls only because the turn leans on the
Modern camera swinging behind her (`ModernCameraCarryHalfTurn`,
`game/camera.cpp:385`-`401`, called once on the turn's first tick and gated on
`ModernCameraActive()` so a swing is never seated while nothing pays it out);
Tank stays byte-identical by not being asked at all. An aborted turn drops the
camera's remaining swing (`ModernCameraDropSwing`, `game/camera.cpp:403`-`417`,
called on a stand after the turn's first tick), as the right stick drops it, so
the camera never ends in front of her. The look-at point needs a rule of its
own because the lowering's first tick moves her 200 units out over the drop and
726 down (`LaraLedgeToHang`, `game/lara.cpp:6537`-`6553`), and `UpdateLaraRoom`
(`:6551`) puts her into the room below the ledge. `CalculateCamera` builds the
point in her room (`game/camera.cpp:1214`) and moves it only a quarter of the way
after her each tick (`:1261`-`1270`), which leaves it above the ledge; in her new
room that sector can be closed above, and `ChaseCamera`'s in-room test then finds
the point above the ceiling and puts the previous one back (`:2039`-`2048`), tick
after tick, until `TargetSnaps` passes 8 and the raw point is taken unsmoothed
(`:1263`, `:1270`). Measured in Andrea1 at room 40's west edge (x 25600): she
hangs from a walkway slab (top -1792, underside -1536 at her edge) whose sector
in room 41 below is closed at that underside with no room above, so on seven
moves of nine the point stood still for nine ticks while she lowered and then
jumped about 200 units across and 300 down in one tick - a 12-degree tilt of
the view within 33 ms, the picture dropping by 23 % of its height half a second
after the turn began. The two moves at the north edge had no jolt, because the
sector of room 41 under that edge has a door back to room 40. The swing itself
was even (its 3u^2 - 2u^3 profile, 13.5 degrees a tick at the peak), no wall or
line-of-sight pull fired (`clip=0` on every tick) and the 60 fps interpolator
did not cut; none of that is changed. The lowering is drawn at the full frame
rate: the interpolator's neighbour rule (`curFrame != prevFrame + 1`,
`game/interpolate_math.h:346`) refuses every backwards-stepped frame, so
`InterpPoseBack`, asked by `GetFramesAt` only while `LaraLedgeReversing`, walks
the animation's own path between the two tick poses instead - the previous
tick's pose at the start of the tween, the current tick's at its end, their
midpoint at a half. The tween after each animation change, into the pull-up and
out of it into the hang, stays a tick frame, as everywhere.

**How.** `ModernLedgeDecide` (`game/modern_move.cpp:1744`-`1810`) is the pure
decision for one tick of the move — the turn's ramp, signed by the side, and
its abort tests, the lowering's frame step and its hand-over — read by
`lara_as_ledgehang` (`game/lara.cpp:6585`-`6700`), the routine for the new
state `AS_LEDGEHANG = 139` (`game/lara_states.h:147`); its collision row stays
`lara_default_col`, since the state itself fills `coll` for `TestTriggers` and
`LaraGun` and never moves her by walking. The turn runs in Tomb Raider 5's own
turn on the spot, animation 12 (`ANIM_TURN_RIGHT`, the animation of `AS_TURN_R`)
or 13 (`ANIM_TURN_LEFT`, of `AS_TURN_L`), both named at
`game/lara_states.h:160`-`164`: the trigger starts the one for
`MODERN_LEDGE_TURN_SIDE` at its `frame_base`, `AnimateLara` plays it forward
as any other animation (its footstep included; none of its state changes
leads out of `AS_LEDGEHANG`), the frame counts the turn's ticks, and the side
is read back off it, so a restored savegame goes on turning the way it started; a save that
holds the idle, `ANIM_BREATH`, in the turn phase, as a build before the turn
animation wrote it, is settled into the hang or a fall like any other that
cannot go on.
`ModernTest`'s `ledge_hang()` block
(`Preparation/Tools/Diagnose/ModernTest.cpp:8240`) and `ledge_trigger()` block
(`:8563`) check the arithmetic, every turn step within a unit of
`MODERN_TURN_RATE` among it; together with the rest of the suite the count
grew from 5788 to 10783, 0 failed. One guard at the head of `AnimateLara`
(`game/laramisc.cpp:502`-`505`, `LaraLedgeReversing`) holds the frame and
skips every animation command while the lowering plays backwards, for
`AS_LEDGEHANG` only. The hand-over is exactly the engine's own settled hang —
`ANIM_GRABLEDGE` at `frame_base + 21`, `AS_HANG`, `LG_HANDS_BUSY`
(`LaraLedgeHang`, `game/lara.cpp:6499`-`6514`) — so `lara_col_hang` and
`LaraHangTest` take over from there exactly as they do from any other catch.
The hand-over happens inside the control routine, and the collision routine of
the same tick is dispatched on the state it wrote (`game/lara.cpp:5000`), so
`lara_col_hang` → `LaraHangTest` reads a word `S_UpdateInput` built for 139:
the word is composed with the state the last tick ended in
(`specific/input.cpp:1907`, the family asked at `:1945`), and without
`IN_ACTION` `LaraHangTest` drops her at once (`game/lara.cpp:5241`). So
`AS_LEDGEHANG` is in the grab family (`ModernStateIsGrab`,
`game/modern_move.cpp:615`-`617`, its reasons at `:543`-`548`), and under
*Toggle* `ModernGrabLatch` delivers the bit from the turn's first tick on. The
key memory the run set makes that first tick no fresh press
(`MODERN_LATCH_KEY`, `game/modern_move.cpp:1295`-`1304`); a savegame restored
in the move goes on under *Toggle* without a key, as one restored in any hang
does, since `InputModeLatchReset` (`game/gameflow.cpp:1119`) clears the let-go
and keeps the key memory.
`MODELOG`'s `grab=` field reads 1 from the turn's first tick on (the family
term at `specific/input.cpp:1992`, the line at `:1998`); the line itself is
unchanged. Under *Hold*,
Tank controls and with the option off the family changes nothing: *Hold*
hands the word back untouched, and nothing enters 139 without the option and
Modern controls.
The trigger (`LaraLedgeTrigger`, `game/lara.cpp:6718`-`6810`) is asked from
`lara_col_run`, `lara_col_walk`, and `lara_col_stop`, right after each
routine's own ceiling test and after `GetLaraCollisionInfo` has measured this
tick's movement, so a state change and a position write there land inside the
tick order between `AnimateLara` and `TestTriggers`/`LaraGun` exactly once. A
run needs no forward input (so braking with the key held is still caught) and
is put on the standing line by a shift that is always forward and at most one
ordinary step; a standing state that became a run or a walk this tick is
judged at `coll->old`, where she actually stood, so the start of a run's own
23-unit hop never counts.
The lowering steps the pull-up (74 frames at rate 4) from its last frame down by
three, 73, 70, ..., 4, 1, and cuts the last step short to land on 0: 25
backwards steps, each with its tween frames drawn at the full frame rate. `InterpPoseBack`
(`game/interpolate_math.h:414`-`443`), a pure function beside `InterpPoseAt`,
which it leaves untouched, places both tick poses in key units exactly as
vanilla does (`InterpKeyAlpha` over `InterpKeyRate`), puts the drawn frame at
`pp + (pc - pp) * t` on that path and hands out the key interval it lies in -
the upper pair `kc + 1` when a step passes over a key and the midpoint still
lies above it (70 down to 67: 17.125, pair 17 at 0.125); it refuses a forward
or standing frame, no rate, a frame outside the animation and two frames more
than one key interval apart. `GetFramesAt` asks it only when `InterpPoseAt` has
refused, and only for `lara_item` while `LaraLedgeReversing`
(`game/draw.cpp:2515`-`2517`), so with the option off, under Tank controls and at
`fps_mode 0` nothing reaches it; the pose and its root offset then come from the
same key pair and factor, and `item->pos` does not move during the lowering, so
there is nothing for the root rule to pay. `INTERPLOG` gains `pose_back=` at the
end of its once-a-second line (`game/interpolate.cpp:726`, legend at `:707`,
noted by `InterpNotePoseBack` at `:1643`, called at `game/draw.cpp:2552`): the
tween frames the rule blended, one per drawn in-between frame, so a lowering's
25 ticks times the tween frames per tick - 25 at sixty hertz, about 137 at 165
(`fps_mode 2`, where every frame is an in-between one; a play-test log read
61 + 76 over two seconds for one lowering) - and 0 everywhere else. `InterpTest`'s
`pose_back()` block (`Preparation/Tools/Diagnose/InterpTest.cpp:1583`) proves the rule
over rates 1, 2, 4 and 5 and steps 1 to 3, over the real lowering at `frame_base` 0 and
3000, its refusals and a clamped last interval; the suite grew from 398 to 521 checks, 0
failed. The camera's look-at rule holds while `ModernCameraActive()` and
`LaraLedgeCamera` (`game/lara.cpp:6363`-`6387`, declared with its `lara_ledge_camera`
phases at `game/lara.h:222`-`234`) names a phase: `AS_LEDGEHANG` throughout, turn or
lower as `LedgePhase` stands after the tick's control, and a tail of
`LEDGE_CAMERA_TAIL_TICKS` (10) ticks, the hand-over's own included
(`game/lara.cpp:6351`-`6361`). `LaraLedgeHang` stamps the tail with `GlobalCounter`
(`:6512`-`6513`), so it runs out on time whatever runs in between; `LaraLedgeStand`
and `LaraLedgeFall` end it (`:6492`, `:6529`), and so does every level start
(`LaraLedgeCameraReset`, `:6389`-`6394`, called from `InitialiseCamera`,
`game/camera.cpp:887`-`890`). `CalculateCamera` decides the rule once a pass, off for
the sniper camera and for a camera built round an item (`game/camera.cpp:1144`-`1161`).
Under it the smoothed point's room walk starts in the room the point was in last tick,
`last_target.room_number`, instead of hers (`:1274`-`1277`) — only the walk's starting
room changes, never the point, and `ConfirmCameraTargetPos` still tests her own column
from her own room — and `ChaseCamera` asks a point that walk finds in no room once
more from her room, as the vanilla lookup asks it, and takes that room when the point
lies inside it (`:2022`-`2037`); only a point both refuse counts as a snap and is put
back, by the vanilla lines (`:2039`-`2048`). A point accepted either way lies inside
the open volume of the room the walk found — `GetFloor` (`game/control.cpp:1253`-`1341`)
clamps a point outside a room onto the room's edge sectors, which are walls or doors,
and a wall sector fails the test — so a point inside rock is still refused, and the
eye, built from the point by `mgLOS` as before, is not sent into geometry. At room
40's west edge (RoomGrid over the player's `andrea1.trc`: rooms 40 and 41 share their
origin; room 40's walkway sector floors at -1792 with no room below, room 41's sector
under it is ceiled at -1536/-1024 with no room above, and the pit sectors west of
x 25600 carry the 40/41 portal at -1792 both ways) the point goes 50, 38, 28, 21,
16 ... units across, a quarter of the remaining 200 each tick, and is over the pit by
the third tick while it is still at about y -2450, some 650 units above the walkway's
top (the play-test log's `ROOMLIST` lines read the point at y -2454 while she runs
onto the walkway and at -1826 to -2200, room 40, in the settled hang), so it never
lies in the slab, and it stays above -1792 for the whole move: the walk from room 40
needs no portal at all. A point that did sink below -1792 over the pit would be
carried into room 41 by room 40's floor portal there, and back by room 41's ceiling
portal. Under Tank and in first person the rule never holds (`ModernCameraActive()`
is false there, and `LaraLedgeCamera` is not even asked), nor with the option off,
where nothing enters 139 (a move under way when the option is switched off from the
pause menu ends in an immediate hand-over and tail from the lowering, and in a stand
from the turn); the smoothing and the rule that takes the raw point after eight snaps
stay vanilla's, and only what counts as a snap changes, during the move and its tail.
`last_target` is not in the savegame: `InitialiseCamera` seats it at her own position in
her own room (`game/camera.cpp:892`-`895`) before its first pass, so in a savegame
restored in the move the first walk starts from her room, exactly as without the rule,
and from the point's own room after that. The `LEDGELOG cam` values are collected in
`ledge_log` (`game/camera.cpp:232`-`244`), cleared at the top of every pass (`:1001`)
and written where each is computed (`:595`, `:1246`-`1249`, `:1780`, `:1873`, `:2050`,
`:2472`): stores to a static nothing else reads, so they change nothing in any mode.

**Commits.** `bf84c3f`, `7e7e2b3`, `cba4d46`, `2a84a0d`, `8da2259`, `93bfff2`,
`8b4a761`, `8924c7b`, `6ca7c09`, `e0c0771`, `eafab87`, `5a5bf5d`, `2b6ac8e`,
`be07363`, `eaa95a4`, `c2112d7`, `8840d13`

### The options menu as one table-driven frame
2026-08-24 - 2026-09-20

**What.** The options menu is one tree of pages built from tables instead of
hand-written page functions. A root *Options* page offers seven tabs: **Audio** (Music
volume, SFX volume, Sound quality, Language); **Controls** (the sub-tabs *Key bindings*,
*View* and *Modes*, then Control type, Action, Control layout, Camera speed, Dead zone,
Targeting, Ammotype hotkeys — ten rows on one page); **Graphics** (the *Display*
sub-tab, then HD textures, Fog, Distance fog, Shimmer, Mono screen style, Shadow mode,
PSX skies, Static lighting, UW Effect, Look transparency, Footprints); **HUD** (Bar
mode, Bar positions, Enemy bars, Ammo counter, Gameover menu, TR4 loadscreens, Loadbar
style, Loading text); **tomb5 options** (Cutscene skipper, Cheats, Level Select, Fix
climb up delay, Flexible crawling, Crawl Tilting); **Special Features**; **Credits**.
The *View* sub-tab carries the six first-person rows — First person view, Field of view,
Death camera, Follow head bone, Camera damping, Body — and *Modes* the four
hold-or-toggle rows. A row that opens another page is drawn centred; a setting is drawn
left-aligned with its value right-aligned; rows use stepped values rather than free
sliders wherever a step is plainly visible, and *Camera speed* and *Dead zone* are bars
driven by a continuous `input & IN_LEFT/IN_RIGHT`, written once per adjustment when the
value stops moving, when the cursor leaves the row or when the page is left. Forty-seven
settings exist, each exactly once, with the variable, range, default, registry name and
side effect they had before. A list too long for one page splits into pages walked with
the left/right arrows on the bottom line, with a centred `1/N` indicator between the
arrows while there is more than one page, and *Key bindings* carries a centred *Reset to
defaults* action above its list on every page. Entering Options from the title screen or
from the pause menu always starts on the *Options* page. Every label, title and value of
a framework page goes through `CustomText`, which has an English and a German column (86
rows; French, Italian and Spanish repeat the English literal), keyed by the exact
English literal, one literal per setting. Settings of the *Game* registry block are
written when a page holding a changed value is left, instead of only at a clean exit.

**Why.** The engine grew more settings than hand-written pages hold, and those pages had
no common geometry: the *Controls* page had to force its own glyph pitch and still could
not grow, and a page with nothing on it should not appear in the cycle at all. Four
defects went with them. The key-binding reset copied 72 bytes with `memcpy(layout[1],
layout, 72)`, 36 bytes past `layout` and overlapping its own source. The two blocks that
turned a selected bit back into an array index shifted by one place too few after rows
were added above them, so they wrote three slots too far ("Up" into "Right", "Duck" into
"Jump", "Inventory" into "Load"), the last keyboard row wrote `layout[1][18]` one past
the end of `short layout[2][18]`, and the three last pad rows landed outside the `k <
15` duplicate-unbind loop — in the default Tank configuration. The *Game* block lived
only in memory until a clean exit, so anything short of one lost the player's settings.
And on the two legacy pages the frame returned before its own `IN_PAUSE` line, so the
pause key closed Options with a page still on the stack and the next visit opened
straight into it. A slider meant to be held rather than tapped is driven by `input &`,
so a held direction was opening the registry key and rewriting every setting up to sixty
times a second.

**How.** `game/menu_nav.h` is pure arithmetic with no engine include — `MenuCursorStep`,
`MenuFirstEnabled`, `MenuClampCursor`, `MenuPageCount`, `MenuRowsPerPage`,
`MenuPageFirstRow`, `MenuRowsOnPage`, `MenuPageStep` — and has its own harness,
`Preparation/Tools/Diagnose/MenuTest.cpp`, built by `bmenu.bat` and listed in
`Preparation/Tools/README.md`. `game/menu_frame.h` is the contract: `menu_row_kind` (TAB
/ ACTION / VALUE / SLIDER / BINDING), `menu_page_id`, the three `MENU_SAVE_*` targets
(`MENU_SAVE_GAME` through a dirty flag when a page is left, `MENU_SAVE_TOMB5` at once,
`MENU_SAVE_NONE`), the five callback types, `struct menu_row`, `struct menu_page`, the
four geometry constants and `MenuFrameRun()`. `game/menu_frame.cpp` holds one table per
page with every row's existing logic carried over, the draw-and-drive loop, the key
capture and the `MENULOG` seam; `MENU_PAGE_OPTIONS` stays 0 and `MENU_PAGE_MODES` is
appended after every existing id, so no page already in `menu_pages[]` changed number.
`specific/LoadSave.cpp`'s `DoOptions()` is three lines calling `MenuFrameRun()` (55
lines added against 917 removed), with `SpecialFeaturesPageRun()` split out for the
still legacy-dispatched *Special Features* page and `specific/LoadSave.h` declaring it;
the legacy branch of `MenuFrameRun()` reads the same `IN_PAUSE` line before returning as
every other page, and the reset copies `sizeof(layout[0])` from `layout[0]`.
`tomb5/troyestuff.cpp` / `.h`, which had carried the tomb5 option pages and for that
reason pulled declarations from `specific/input.h`, `specific/dxshell.h`,
`specific/output.h`, `specific/texture.h`, `specific/drawbars.h`,
`specific/function_table.h`, `specific/dxsound.h`, `specific/audio.h`, `game/camera.h`,
`game/lara.h`, `game/laramisc.h` and `resource.h`, lost `TroyeMenu`, `TroyeMenuPage`,
`Page0`, `Page1`, `Page2`, `PageDisplay` and the page-number constants; `CreditsScreen`
is unchanged. Geometry is one recomputed rule for every page: at `font_height =
GetFixedScale(28)`, which scales with the window so the row count is the same at every
resolution, the title is row 2, rows 4 to 15 are usable and row 16 is the arrow row —
twelve rows, eleven in a list page that also carries the fixed Reset row. Paging splits
balanced, `pages = ceil(n / max)` and `rows per page = ceil(n / pages)`, with wrap; the
`1/N` indicator is drawn in its own colour on `MENU_BOTTOM_ROW`, and the widest string
the tree can produce is `1/3`, 27 px scaled at 640x480. The settings themselves live in
`global/types.h` (`control_modern`, `combat_hold`, `action_split`, `camera_speed`
default 40, `dead_zone` default 50, and the rest) and are read, clamped and written by
`tomb5/tomb5.cpp` as `ctrl_modern`, `combat_hold`, `action_split`, `cam_speed`,
`dead_zone` beside every other `tomb5.*` value; the *Control type* row calls
`ModernResetControls()`, `ModernCameraResetOrbit()` and `ApplyJoyScheme()` on every
change, so a stale held course, blocked counter, orbit or unhonourable binding never
survives a switch. `game/gameflow.cpp` holds the `CustomText` rows, the German column
writing umlauts as the Latin-1 bytes `0xE4`, `0xF6`, `0xFC`; `AccentToCode`
(`game/text.cpp`) maps them to 128+4, 128+20 and 128+1 and `PrintString` composes each
from `CharDef` plus its row of `AccentTable` (`game/accent.inc`) — the sharp s has no
row, so the table avoids it, and a matched row returns its column unconditionally, which
is why the other three languages repeat the English literal rather than leaving a cell
empty. `specific/input.cpp`'s `ModernMenuReadsPad()` mirrors the seam's own "is this a
menu" predicate (`InventoryActive`, `gfLevelId == LVL5_TITLE`) and feeds the classic
assembly the left stick together with the d-pad while it holds, a medipack bound to a
d-pad bit being declined for the one tick that predicate holds so one press cannot mean
two things. `TOMB5 Code/tomb5.vcxproj` lists the new files and `docs/Engine-Manual.md`
describes the tree.

**Commits.** `4a2377d`, `12a0cda`, `366ba76`, `52c94ee`, `a762fde`, `3ec8918`,
`91924c9`, `ff1a5c7`, `88ae528`, `b7950a5`, `c011a24`, `dc2a594`, `998917f`, `a7c3498`,
`66b0587`, `2605da8`, `d56d4e1`, `a6ffcff`, `b73c2de`, `bf9fc9e`, `9d6531c`, `a9b1922`,
`81a8827`, `9a26fbc`, `8c31d0f`

### A credits screen
2026-08-22 - 2026-08-30

**What.** The game has a credits screen of its own, reachable from its own row in the
options menu, with a Contributors section. Groups are separated by colour rather than by
blank lines, and the running state is not a function static, so an abort lets the next
roll start from the bottom again.

**Why.** GPLv3 courtesy and a project requirement: the names without which this would
not exist belong in the game, not only in a readme. It is not a page of the options,
because as a page it turned up unasked while paging through the settings and the paging
arrows are drawn at a fixed row that landed across a line of names. The screen holds
fifteen rows, so four blank separators would push the tool credits off it.

**How.** `specific/alexstuff.cpp` / `.h` hold the screen and its roll,
`tomb5/troyestuff.cpp` the `CreditsScreen` page and the Contributors list, and the
*Credits* tab of `game/menu_frame.cpp` opens it.

**Commits.** `12a0cda`, `366ba76`, `83c7195`, `9f0ec17`, `52c94ee`, `be95b08`, `402a243`

### Skipping cutscenes and the intro flyby with the controller
2026-07-31 - 2026-08-22

**What.** Cross on a controller, and Enter, skip a cutscene and the level-intro camera
flyby, as Escape already did. The skip fires only while `cutseq_trig == 2`, the actual
playing state, and Cross must be freshly pressed; Escape keeps its original held
behaviour. The end credits roll can be aborted and the intro can no longer be skipped
twice.

**Why.** The skip check ran for any non-zero `cutseq_trig`, so entering a cutscene
trigger while firing meant Cross — the fire button on controllers — was still held when
the pre-start fade finished, and `do_cutseq_skipper_stuff()` aborted the scene before
`Load_and_Init_Cutseq` ever ran. The played flag, set inside `Load_and_Init_Cutseq`, was
therefore never written, Lara still stood on the floor trigger, and the scene was
requested again the next frame: an endless approach-and-abort loop that only Alt+F4
ended. The edge is required because an earlier form of the Cross check read
already-masked input.

**How.** `game/control.cpp` (the state test and the edge), `game/spotcam.cpp` (the intro
flyby), `specific/alexstuff.cpp` (the credits roll and the intro videos).

**Commits.** `bdf95ad`, `e980790`, `790f4f2`, `6176647`, `be95b08`

## 2.7 First person view, cheats and the -log seams

Three things that share one property: they are switched on by the player and are off in
a default game. The first person view (#126, #134) hangs the camera in Lara's head joint
and is a row of the *View* sub-tab; the cheats are reached only while the *Cheats*
option is on; and `Log()` returns on its first line unless the game was started with
`-log`, behind which sit all the diagnostic seams the engine carries. The seams are a
permanent part of the build, not scaffolding: every feature that can fail in a way a
screenshot cannot explain has one, they are formatted only when the switch is given, and
nothing in the engine branches on them.

### The first person view
2026-09-05 - 2026-09-08

**What.** With *First person view* on, the camera sits in Lara's head joint and her body
is drawn around it. Her HD body is cut at the head and drawn one-sided for as long as
the view runs; both are per draw, since `HDModelDraw` takes a cut and a cull argument
and only her body asks for either — every other model, including her own hands,
holsters, back weapon and the lid that closes the hole, gets no cut and
`HD_MODEL_CULL_NONE`, so a hand brought up to the camera is never taken. The cut is one
bone weight: every fragment whose head weight reaches `HD_LARA_HEAD_CUT_WEIGHT` (0.15 of
1) is discarded while the view runs, which takes the neck stump and the diving suit's
helmet, both of which hang on the torso node, without touching a chest or an arm; a
level with no HD body has no cut. The binoculars and the laser sight never reach the HD
seam at all, and nothing of her is left behind when the scope comes down. The braid is
not drawn in first person while its simulation runs on, so it hangs where it belongs on
the first third-person frame. `FirstPersonEnabled()`, which decides whether the camera
runs, refuses the title level, a flyby and a cutscene, the way `FirstPersonActive()`,
which the draw asks, already did. A setting *Body* (`tomb5.first_person_body_still`,
registry `ego_body_still`, default `animated`) offers `still`: at `still` her body is
held on one stored frame while she is standing, crouching or kneeling still on the
ground — the states `AS_STOP` (which aiming while standing keeps, so a rifle held on a
target stops breathing too), `AS_POSE`, `AS_DUCK` and `AS_ALL4S`, each only while
`current_anim_state == goal_anim_state` — on the key frame the idle began on. Not held:
the head's own look angles, which are not in the frame at all; the arms, which the
weapon slot drives, so drawing, aiming, firing and reloading are untouched and keep
their blend-in time; and every state that is not in the list, meaning every step, jump,
climb, pull, roll and swim. The hold is dropped the moment the state stops being an idle
one, the animation changes underneath it, or the view leaves first person, so a
savegame, a cutscene or a death cannot leave her frozen.

**Why.** The classic path answers a camera in the head joint by leaving the head *mesh*
out, and for a classic body that is the whole of it, `LM_HEAD` being head and neck in
one piece. An HD body is a single skinned surface whose neck stump under the head is
weighted to the torso, so masking node 14 takes the head off and leaves the stump
standing in the camera — a collar of triangles 18.6 units from the eye when a wall holds
the camera at the joint, inside the near plane of 20 (`InitWindow`, `gamemain.cpp`). A
bone weight is written in the `.TRM` file, is the same number in every pose, at every
angle and in every level, and the shader already has it in hand for the skinning, where
any shape carried by the head node tilts with the head; between 0.05 and 0.35 every one
of the eight wardrobe bodies reads the same 24.5 units (25 on the Russia coat) for the
widest thing taken below the chin, which is the neck's own radius and no collar, and
0.15 is taken out of that plateau, three times clear of the TR3 conversion's noise and
the one threshold at which the rim below is a single texture page on all eight bodies.
The one-sided drawing is needed because the HD path draws every model double sided
(`D3DCULL_NONE`), which is what the classic engine appears to do —
`specific/function_table.cpp` sets `D3DCULL_NONE` for the whole session — while TR5 in
fact culls back faces in software, per polygon, through the `IsVisible` function
pointer, so the card never sees one. No camera stands inside an object except this one,
which stands inside Lara: with the camera in her neck the inside of her own chest is the
whole picture, and cutting a hole in a closed body only shows more of it. `D3DCULL_CCW`
is the mode that keeps the outside, measured and not assumed: from a normal camera,
`CCW` and `NONE` give the identical background share and the identical mean brightness
(162.1 of 255) while `CW` differs (160.1), so outside first person the culling is a
no-op, and the wardrobe's winding carries it — over the eight bodies, 25,384 to 39,440
triangles each, at most 59 have their geometric normal on the other side of the vertex
normal (0.06 % to 0.18 %). One consequence of the mirror being a true reflection
(determinant −1): in first person, and only there, the reflected draw culls the other
side, because that is the only frame where the HD path culls at all. The gate on
`FirstPersonEnabled()` is needed because `DoTitle` calls `InitialiseCamera`, which ends
in `CalculateCamera`, at a moment when `bUseSpotCam` is still 0 because the flyby is
triggered two lines later: without it one full pass of `FirstPersonCamera` ran on the
title level's Lara, moved the camera onto her head, took the field of view through
`AlterFOV` and left `fpFovSet` and `fpHeadHidden` standing with nothing to hand either
back. The still body exists because in first person the camera hangs in the head joint,
so every breath of the idle animation moves the whole picture, and it is the one thing
about the view a player cannot avoid by looking elsewhere; holding by state and not by a
list of animation numbers is necessary because which animation she idles in depends on
what she is holding and what she did last, and holding the idle's own starting key frame
means the pose really exists in the file, nothing can drift, entering the hold cannot
jump because it is the pose she was already wearing, and leaving it costs at most what
the idle travels in one cycle, on the frame she starts moving anyway.

**How.** `specific/hd/hd_model.cpp` / `.h` and `hd_object.cpp` / `.h` carry the two
arguments; the cut rides in `gPass.z`, the pixel-side constant `hd_model.cpp` already
writes per pass, and the cull mode is set and put back around the model's own draw
calls. `gPass.z` is the one component in which the two shaders differ, and it is safe
because `hd_model.cpp` writes `c4` to the pixel constant file alone while
`hd_shader/hd_room.hlsl` reads its own `gPass.z` in the vertex half; the register table
at the top of `hd_shader/hd_model.hlsl` names it accordingly and
`hd_shader/hd_model_ps.inc` is the compiled form. `specific/hd/hd_laradraw.cpp` and
`hd_lara.h` decide when the body asks and hold `HD_LARA_HEAD_CUT_WEIGHT` with its
measurement. The eye's position comes from `FirstPersonCamera` (`game/camera.cpp`),
which reads the head joint with `GetLaraJointPos(&head, LMX_HEAD)`, lifts it 16 units
and asks `mgLOS` whether it may go `FP_EYE_OFFSET` = 48 units forward — 48 in the open,
0 with a wall, ledge or doorway in front; `FirstPersonEnabled()` is in the same file.
The binoculars: `BinocularCamera` (`game/camera.cpp`) sets `lara_item->mesh_bits = 0`
for the scope and the laser sight both, `game/draw.cpp` asks `if (lara_item->mesh_bits
&& !SCNoDrawLara)` before it calls `DrawLara` at all, and leaving the scope puts
`mesh_bits` back to −1 in the same function; the scope's mask lies above the HD world by
the drawing order, since `HDFrameDraw()` runs immediately before `DrawBuckets` and the
depth clear that separates the world from mask, pickup display and fade (#127) comes
after all of it, while `AlterFOV` moves `f_persp` inside the frame during the zoom,
which is why the projection is taken when the world is collected
(`HDFrameCaptureProjection`) and not when it is drawn. The black frame on zooming out was
a vanilla inheritance: the depth buffer was cleared only in mid-frame, so the binocular
mask's depth values stood until the next frame and the world was tested against them.
`S_InitialisePolyList` (`specific/output.cpp`) now clears depth together with the colour
at the top of every frame, and `DXSaveScreen` (`specific/dxshell.cpp`), which read the
D3D9 back buffer as 16-bit and started one row past its end, writes a correct 24-bit TGA
(`8d3e5de`). `HairControl` is called
from `game/control.cpp`, in the game loop and not from `DrawLara`, which is also how the
classic path skips `DrawHair` (`specific/drawlara.cpp`). `LaraBodyStillFrame` in
`game/delstuff.cpp` puts the held frame in both key frame slots with the factor 0 beside
it, so both draw paths wear it — the classic first person path draws her body too,
leaving out only the head mesh — and so that the interpolated branch stays reachable; a
blend of a pose with itself is that pose, and `frac` is lifted to 1 only on a frame that
had no pair of its own, while `GLaraShadowframe` takes the held frame's own bounds. The
option field is `first_person_body_still` in `global/types.h`, its default and its
stored value `ego_body_still` in `tomb5/tomb5.cpp`, and its
row is the last of the *View* page, directly under *Camera damping*
(`game/menu_frame.cpp`); it needs `tomb5.first_person` as well, and it is the one change
of these rounds that alters the picture at thirty hertz too. Seams with `-log`: `HDLOG
firstperson on|off headcut=0.15` once on the edge, `HDLOG headcut cut=1 w=0.15 node=14
head=.. torso=.. min=..@node.. lid=1` once a second, `HDLOG binoculars lara_pushed=N
mesh_bits=N` one frame after the scope comes up when the frame it describes was wholly
inside the binocular view, and `HDLOG binoculars off` when it goes down.
`Preparation/Tools/Diagnose/ModelProbe.cpp` `-firstperson` puts the camera where
`FirstPersonCamera` puts the eye (head joint, 16 up, `-fpforward` units in front,
default 48) looking along +z with `-pitch` as the look-down angle, and `-nearcut`,
`-cull` and `-neck` switch the halves and print the stump table. `docs/Engine-Manual.md`
describes the view.

**Commits.** `949c130`, `45d9130`, `db696bf`

### The cheats: DOZY as a toggle, and five bindable hotkeys
2026-08-23 - 2026-09-20

**What.** The DOZY key combination reacts to the rising edge and switches flight off
again if it is already on; the Walk-key exit inside `LaraCheat` stays, and both exits
share one function, `LaraCheatEnd`, which also sets `current_anim_state` and
`goal_anim_state`. The typed DOZY chord works under the Modern key table. Five cheat
roles are bindable on the keyboard (slots 29-33, defaults F2 fly mode, F7 all weapons,
F8 all items, F11 heal, F1 skip level) and on the pad (roles 23-27, unbound by default);
they are offered on the *Key bindings* page and can fire only while the *Cheats* option
is on, only with a living Lara in a level — not on the title screen, not in a cutscene,
not dead — and each on its own press edge. BITS has no role. The five typed cheat words
are unchanged and still fire every tick their letters are held.

**Why.** Vanilla had only a way in; the only way out was the Walk key, which nobody
discovers unaided and which leaves a tester stuck in flight, and upstream also left the
two animation states on the swim-cheat state for one frame. Under the Modern key table
the step-left key is Z, one of DOZY's four letters, and it also carries `IN_WALK` while
`IN_LOOK` is stripped; `LaraCheat`'s exit reads `IN_WALK && !IN_LOOK`, so the chord's
own switch and its own exit fired in the same tick and she rose 128 units and fell
straight back out of `LW_FLYCHEAT` before a frame was drawn. A role that cannot fire
must not claim a key: with *Cheats* off the five rows are not offered and nothing reads
them, so counting their keys in the conflict table would have silenced another role's
default-key fallback in favour of a binding that can do nothing.

**How.** All of the cheat logic is in `game/laramisc.cpp`. `LaraCheatLetterDown()` makes
the exit ignore the walk key while any of the four DOZY letters is still physically
down; the walk key and the other three original exits are untouched, so a Tank player
holding walk and a cheat letter at once also keeps flying. `LaraCheatFlyToggle()` is
shared by the chord and the fly hotkey; the other four hotkeys call exactly what GUNS,
ITEM, HEAL and SKIP already do from the inventory ring, reached from `LaraControl`. The
roles are `KEY_ROLE_CHEAT_FLY`..`KEY_ROLE_CHEAT_SKIP` (`game/key_scheme.h`) and
`JOY_ROLE_CHEAT_FLY`..`JOY_ROLE_CHEAT_SKIP` (`game/pad_scheme.h`), read through
`specific/input.cpp`; `KeySchemeConflicts` takes a `cheats` argument so the five slots
are inert exactly as *Secondary* is under *One key*, and the *Cheats* row in
`game/menu_frame.cpp` recomputes the table like every other row that makes a role live
or inert. The default keys are the free F keys: F4 belongs to the outfit changer
(`specific/hd/hd_outfitmenu.cpp`), F10 to `IN_E` and F12 to `specific/output.cpp`,
F5/F6/F9 to save and load, and F3 is left unassigned because Core's own level skip in
the `_DEBUG` branch of `LaraCheatyBits` uses it, which no release build contains. Seam
behind `-log`: `CHEATLOG f=%ld what=<fly_on|fly_off|weapons|items|heal|skip>
src=<word|hotkey>`, for every cheat that fires including the typed words.
`docs/Engine-Manual.md` lists the roles.

**Commits.** `60d7ca5`, `884bd0a`, `b3930c8`, `8c31d0f`

### The -log switch and the diagnostic seams
2026-08-25 - 2026-09-21

**What.** `Log()` is compiled in and returns on its first line unless the game was
started with `-log`: without the switch nothing is opened, nothing is formatted and no
`log.txt` appears. Behind the switch sit the engine's diagnostic seams, printed on a
change or throttled per item and per 30 frames rather than per frame wherever a one-tick
pulse would otherwise be buried: `SFXLOG` (which sample a sound really played, and which
anim command asked for it), `CLIPLOG`, `SKIPLOG`, `MESHLOG`, `SLOTLOG` and `CLIPDETAIL`
(why an enemy is not drawn — bounds check, status branch, slot allocation or mesh
discard); `FPSLOG`, which writes one line per second of wall clock — frames presented,
shortest, longest and average gap, how many gaps missed the budget, and the heaviest HD
room, object and draw-call counts of that second — plus one summary per level; the HD
seams `HDLOG firstperson`, `HDLOG headcut` (the cut's threshold and node, the distance
from the lens to the head joint, to the torso joint and to the nearest of all fifteen,
in view space, with the lid's presence), `HDLOG creaturelight` (up to six intelligent
items a second with the classic and the HD colour side by side, their difference, the
distance to the lens, the ambient and the light counts), `HDLOG clip` with
`items_spared=`, `view=` and `view_obj=`, and `HDLOG frame` with `clipped=` lights;
`INTERPLOG` with `camcut=` (which cut rule fired), `pose_far=`, `pose_hd=`, `stretch=`
(the node, animation and frame of the worst bone of the second) and
`arms=<blended>/<refused>/<tween frames>`; `MODERNLOG` with its `init`, `pad` and
per-tick lines and a separate `mode=tank` line; `MENULOG enter` and `MENULOG set`;
`COMBATLOG`, `MODELOG` and `CHEATLOG`. The engine build script deletes `winmain.obj` and
`setupdlg.obj` before `msbuild`.

**Why.** Upstream compiled `Log()` out entirely, so a player who ran into something
could never produce a log; the only way was a special build. The switch is read by hand
as the first thing `WinMain` does rather than through the `commandlines` table, because
the run check and the gameflow load have already logged by the time that table is worked
off, and those startup lines are the ones a diagnosis needs most; it is matched as a
whole word, so a stray "log" inside a path cannot silently turn on per-frame logging. A
picture cannot answer how far the cut plane stands from the lens, whether an HD creature
is darker than a classic one, whether all rooms and objects of one frame were collected
with one camera, which frame of an animation stretched a bone, or which half of an input
seam answered; a counter can. The frame rate has to be counted where the player actually
sees a frame, including the wait and the driver time: `DXShowFrame` is the engine's only
present — the game frame, the inventory and the title menu through `S_DumpScreen`, the
loading bar and the credits through `S_DumpScreenFrame`, and the movies in `fmv.cpp` all
end there — so one call counts every frame exactly once, where counting inside
`S_OutputPolyList` would miss the loading bar and the movies and counting in the main
loop would count message pumps that drew nothing. Two counters and not one, because the
logic is a fixed 30 Hz built on a 60 Hz tick: `slow` is every gap over the 33.3 ms
budget, which half a healthy second reaches on jitter alone, and `late` is every gap
over 41.7 ms, which jitter cannot reach because a missed slot costs a whole tick.
`MODERNLOG`'s derived rate is printed beside the setting it came from, so a play test
can tell "the slider is wrong" from "the mapping came out wrong", the live `modern_cfg`
rates being read back rather than restated so a hand edit of the registry shows up too;
`dead_l=` and `dead_r=` print the one zone actually in force this session, chosen by
`joystick_is_xinput`; the `pad` line exists because a dead zone sized from a claim
rather than a measurement can only be corrected by a log that says where the pad really
rests; the tank line is separate because the bypass runs instead of `ModernUpdateInput`,
so that function's line cannot describe it; `aim_strafe=` prints 0 and `mode=aim`
therefore cannot appear in a default log, the field and the enum value staying so the
absence is not read as a dropped field; and `MENULOG`'s `row=` is always the English
label even in a German menu. `COMBATLOG`'s `draw` field is the bit that actually leaves
`S_UpdateInput`, and `seam` says which half answered, because printing the weapon half's
produced bit alone read as the opposite of what happened whenever the seam was off while
the key's own `IN_DRAW` was still in the word. The build stamp: `winmain.cpp` writes the
first line of every `-log` run, `PCtomb5 built <__DATE__ __TIME__>`, and those macros
are fixed when that one file is compiled, so an incremental build that touched a shader
or a file in `specific\hd` left yesterday's stamp on today's binary and a log could name
a build that never ran; `setupdlg.cpp` carries the same `__DATE__` in the launcher's
window.

**How.** `specific/function_stubs.cpp` and `.h` hold `Log()` and `gLogEnabled`;
`specific/winmain.cpp` reads the switch and writes the stamp. A seam that has to build
something before it can log — walking a list, formatting a name — tests `gLogEnabled`
itself at the call site, because the gate inside `Log()` only saves the writing, not the
preparing. The seams live in `game/draw.cpp`, `game/lot.cpp`, `game/sound.cpp` / `.h`,
`specific/output.cpp`, `specific/hd/hd_laradraw.cpp` (`HDLOG headcut`),
`specific/hd/hd_object.cpp` (`HDLOG creaturelight`), `specific/hd/hd_level.cpp` / `.h`
(`HDLOG clip`, `HDLOG frame` and the frame counting), `game/interpolate.cpp`
(`INTERPLOG`), `game/modern_state.cpp` (`MODERNLOG init` and tick lines),
`specific/input.cpp` (`MODERNLOG pad`, the `mode=tank` line, `COMBATLOG` and `MODELOG`),
`game/menu_frame.cpp` (`MENULOG`, through `MenuAfterChange`), `game/laramisc.cpp`
(`CHEATLOG`) and `specific/hqsfx.cpp`, whose eleven lines are formatted only inside `if
(gLogEnabled)`. Every camera number `MODERNLOG` prints comes through `game/camera.cpp` /
`.h`, whose `ModernCameraLogState` and `ModernCameraLogConfig` were widened rather than
given accessors of their own; `view_err=` is abs(`cam` - `cam_world`) wrapped the short
way, in the same raw units `cam`, `cam_world`, `err` and `yawd` share, and the camera
fields read one tick behind because the line runs before `CalculateCamera`. The frame
counter is one line in `specific/dxshell.cpp`, in `DXShowFrame` directly after
`IDirect3DDevice9::Present`, calling `HDFramePresented`; without `-log` its first line
returns, no clock is read, no counter stepped and nothing allocated. The per-level
summary is flushed at the top of `HDLevelBuild` for the level before and in
`HDLevelDeviceLost`, so the level the player quits from gets one too, both on the main
thread after the load thread has joined. The build stamp is two `del` lines in
`Preparation/Tools/Diagnose/be.bat` against `TOMB5 Code/Win32/Release/winmain.obj` and
`setupdlg.obj`. The diagnostic tools that carry these measurements — `InterpTest`,
`ModelProbe`, `AnimMap`, `ModernTest` and `MenuTest` — are listed in
`Preparation/Tools/README.md`, with `Preparation/Tools/Diagnose/InterpTest.cpp` and
`Preparation/Tools/Diagnose/ModelProbe.cpp` in the tree; `docs/Engine-Manual.md` carries
the field tables.

**Commits.** `268d2e8`, `1a142b2`, `80e070b`, `45d9130`, `a1dbf32`, `db696bf`,
`b7950a5`, `f770533`, `e3bf34b`, `9c74cdc`, `57d2783`, `d6d23fc`, `998917f`, `a73ca8c`,
`884bd0a`, `da66ba8`, `9a26fbc`, `e6fc7c4`

## 2.8 What the push audit corrected

Before the repository was made public it was walked once against the licence and against
its own statements. The result is this: every file the project modified carries the
GPLv3 section 5(a) modification notice, every origin sentence in the code names the
lawful source the code really rests on — TRForge's MIT format description, TROne's GPLv3
code with its file and line, or a measurement made here on the player's own data files —
and a fresh clone builds on a Windows checkout. Nothing in the tree opens, maps, links,
parses or disassembles a program of the Tomb Raider IV-VI Remaster; `hd_install.cpp`
tests that `tomb456.exe` or `tomb123.exe` exists and nothing more.

### Licence notices, origin sentences and the corrections made before the public push
2026-09-21 - 2026-09-22

**What.** Seven engine files changed long before and never marked — `game/draw.h`,
`game/tomb4fx.cpp`, `game/rope.cpp`, `game/sphere.cpp`, `game/deathsld.cpp`,
`game/flmtorch.cpp`, `specific/polyinsert.h` — carry the modification notice; the
twelve diagnostic tools that still lacked one and `Preparation/Tools/EmbedData.ps1`
carry theirs, so all eighteen tools under `Preparation/Tools/Diagnose` now have one; the
randomizer's `TR5Randomizer/src/core/Tr5Tr3Import.cpp` carries the
project header, the last source file under `src/` that lacked it. `tomb5.vcxproj` names
`game/interpolate_math.h` and `tomb5.vcxproj.filters` gained 87 entries, so the project
file lists every source it builds. Two things the player sees are repaired: the French
and German footers of the save/load page, where each accented letter had become two
glyphs, and the CP-1252 accent table of `game/text.cpp`. `.gitattributes` pins
`**/hd_shader/*.hlsl` and `**/hd_shader/*.inc` to `eol=lf`, and the regenerated `.inc`
files then differ from the tracked ones in their stamp line alone. `bmap.bat` and
`btrg.bat` use the `%~dp0` pattern of the other build scripts, `bmap` links what its own
header promises, and `SfxProbe`'s usage banner names its principal switch. The eleven
diagnostic lines of the HQ sound bank are formatted only when `-log` is given. On the
randomizer's side, `TR5Randomizer/.gitignore` exempts the tracked `build/b1.bat` and
`build/b2.bat` from its own blanket ignore; `RANDO_BUILD_ID` is 19; and the launcher has
no *Patch remaster textures* checkbox — a never-wired plan — while the folder picker's
title goes through the translation table, leaving no German literal in the GUI.
`THIRD-PARTY.md` and `Preparation/Credits/Credits.txt` carry a row or a line for every
taking and every library: TROne with its handle and URL, TRForge, the sub-component
holders inside zlib and Dear ImGui, the RapidJSON embedded by a discord-rpc library, and
a sentence that the repository's five pictures are the author's own AI-generated
artwork. `docs/Engine-Manual.md` matches the engine (the HQ sound bank is read out of
the installed Remaster and needs no hand copy; `RICHCUT2` borrows Red Alert's HD
picture; the graphics-card requirements; the `INTERPLOG` field table; what happens to an
old savegame) and has a chapter for the F4 outfit changer, the Mods folder and the
per-outfit footsteps. `docs/Randomizer-Manual.md` and `README.md` carry the disclaimer,
and `README.md` a "what you need" naming the player's own Remaster, rows for HD and 60
fps, screenshots with captions and credits for the four third-party mods that appear in
them. `Preparation/Verification.md` names the four post-Beta tasks that are allowed to
move randomized bytes and a fourth detector for the encoding sweep.

**Why.** GPLv3 section 5(a) requires every modified file to carry a prominent notice of
the change. An origin sentence that names the wrong source is a licence claim that
cannot be honoured: every rule in the engine rests on TRForge's MIT format description,
on TROne's own GPLv3 code with its file and line, or on a measurement made here on the
player's own data files, and the sentences say so. A byte-hashing shader guard fires
falsely when git rewrites line endings on checkout, which breaks the build from a clean
clone. A diagnostic seam that formats its lines without testing `-log` costs work on
every level load for output nobody reads. A checkbox that promises a feature the program
does not have misleads the player, and a hard-coded German string cannot be translated.
`RANDO_BUILD_ID` distinguishes builds whose output can differ: four commits since Beta
1.0 change where an enemy may stand, so 18 became 19.

**How.** Engine: the notice block was copied byte for byte out of `game/lara.cpp` into
the seven unmarked files (`7c02357`). Origin sentences throughout the engine and in
`THIRD-PARTY.md` were rewritten to name TRForge (MIT), TROne (GPLv3) by Todesengel1931 —
https://github.com/todesengel1931 — with file and line, or a measurement on the player's
own files (`8219710`, `9157c00`). The save/load page footers (`a1c6129`) and the CP-1252
accent table of `game/text.cpp` (`c8b89b0`) were repaired byte for byte. The eleven log
lines of `specific/hqsfx.cpp` now sit inside `if (gLogEnabled)` (`e6fc7c4`). Three
miscounting or misnaming comments and the parked comment items of earlier rounds were
swept (`1bcacee`, `5b5412d`). Build: `.gitattributes` gained the two `eol=lf` lines
(`5dcc9fb`); `Preparation/Tools/Diagnose/bmap.bat` and `btrg.bat` moved to `%~dp0`
(`1e4f21a`); `Preparation/Tools/Diagnose/SfxProbe.cpp` fixed its banner (`f768981`); the
twelve diagnostic tools that still lacked one and `Preparation/Tools/EmbedData.ps1` got
their notices (`86fc9c0`); `tomb5.vcxproj` and `tomb5.vcxproj.filters` were completed
(`84a0c93`).
Randomizer: `TR5Randomizer/src/core/Tr5Tr3Import.cpp` got the project header
(`fdd7508`); `TR5Randomizer/.gitignore` exempts `TR5Randomizer/build/b1.bat` and
`build/b2.bat` (`b20664e`); `TR5Randomizer/src/core/BuildId.h` sets `RANDO_BUILD_ID` to
19 with a ledger line naming `38b8bd5`, `9b7b827`, `4dd8c6c` and `5e087f7`, the number
being printed in the launcher and written into `spoiler.txt` and not part of the seed
(`f4530f1`); `TR5Randomizer/src/gui/Main.cpp` and `TR5Randomizer/src/gui/Language.cpp`
lost the checkbox with its tooltip and both language rows and route the folder-picker
title through the translation table (`3bde4b1`). Origins inside the HD readers, stated
as facts: `specific/hd/hd_scan.cpp` is re-typed from TRForge's MIT format description
alone; the facial-rig rescan of `specific/hd/hd_trm.cpp` with the small byte reader
beside it, and `read_map_file` in `specific/hd/hd_bind.cpp`, follow TRForge's own code
closely, which MIT permits and which the notice in `vendor/trforge/LICENSE` covers — the
two file headers and `THIRD-PARTY.md` say this rather than claiming the whole files were
merely re-derived (`1823f98`, `33153c8`, `3c7345d`, `31b0e7d`). What stays as it is:
Core Design's setup render reached this tree through the upstream import baseline and
remains, as it does in upstream's own history; the two `discord-rpc` libraries are
upstream's, switched off at the source (`TOMB5_DISCORD_RPC 0` in `tomb5/tomb5.cpp`),
with no call site compiled and nothing of them in any binary, which
`Preparation/Credits/Credits.txt` and `THIRD-PARTY.md` state instead of the older
sentence claiming they are linked and called; the in-game credits screen
(`tomb5/troyestuff.cpp`) names Todesengel1931 on its Contributors row, and the GPLv3
obligation for the twenty-one source files carrying his copyright is met by those file
headers and by the entries in `THIRD-PARTY.md` and `Preparation/Credits/Credits.txt`.
The document commits of the round, covering `README.md`, `docs/Engine-Manual.md`,
`docs/Randomizer-Manual.md`, `Preparation/Verification.md` and `TOMB5
Code/MODIFICATIONS.md`, are `e069d1d`, `3c1fd9b`, `1400796`, `300c98f`, `f5814b5`,
`a740567`, `2f5376f` and `3ba8600`.

**Commits.** `7c02357`, `a1c6129`, `8219710`, `e6fc7c4`, `1bcacee`, `5b5412d`,
`c8b89b0`, `5dcc9fb`, `1e4f21a`, `86fc9c0`, `f768981`, `84a0c93`, `9157c00`, `b20664e`,
`fdd7508`, `f4530f1`, `3bde4b1`, `e069d1d`, `3c1fd9b`, `1400796`, `300c98f`, `f5814b5`,
`33153c8`, `a740567`, `3c7345d`, `2f5376f`, `1823f98`, `31b0e7d`, `3ba8600`

## 2.9 The cleanup before the public push

The record and the project's own comments were rewritten to state facts, separately from
section 2.8's push audit: the record was condensed from a diary into a licence document, and
every comment this project had added or changed was rewritten to state facts instead of
narrating the work. The origin facts already in place - TRForge (MIT), TROne (GPLv3) by
Todesengel1931, tomb3 (Troye's decompilation of Tomb Raider III), and measurement made here on
the player's own files - stand as before; nothing changed about what the code does.

### The record: from an eighteen-thousand-line diary to a licence document
2026-09-22

**What.** `TOMB5 Code/MODIFICATIONS.md` was rewritten from 18,337 lines to 6,090 lines in that
commit: one head with one dated ledger, the changes grouped by area, a single entry form
(What, Why, How, Commits) for every topic, and the appendix that maps task numbers to entries.

**Why.** GPLv3 section 5(a) asks only for the date and the nature of a change; a reader of the
record needs what was done and why, not how the change was found, discussed or reviewed.

**How.** The old record was condensed area by area, one entry per topic. A script compared
the old text against the new and confirmed that every commit hash and every task number the
old record carried is present in the new one.

**Commits.** `edd305e`

### Comments in the engine, the randomizer and the tools say what, why and what for
2026-09-22 - 2026-09-23

**What.** Every comment this project added or changed in the engine, the randomizer and the
tools now states what the code does, why, and what it is for. Task numbers, round and stage
names, dates and play-test narration are gone. Measured facts, failure modes and origin
sentences (TROne, TRForge, tomb3 by Troye, or a measurement made here) stand as facts. Line
numbers that pointed into another file of this project were dropped; file and function names
stayed.

**Why.** The same reader GPLv3 section 5(a) and the record itself serve needs what a comment
states now, not the round, the stage or the question that produced it.

**How.** Files were rewritten in batches by folder - `game`, `specific`, `specific/hd`,
`specific/hd/hd_shader`, `tomb5` and the global headers, `TR5Randomizer/src`,
`Preparation/Tools` - each batch checked before its commit by a script that strips comments
and collapses whitespace from the old and the new version of every file and finds the rest
identical: line endings, high bytes, the modification notice and every upstream comment of
Core's and Troye's stayed untouched. After the batches the engine and the tools were rebuilt,
the three test suites (ModernTest, MenuTest, InterpTest) were run, and the corrections in
`83ce380` and `af0a11d` followed. In all 272 distinct files changed: 165 under `TOMB5 Code`,
39 under `TR5Randomizer/src`, 68 under `Preparation/Tools`.

**Commits.** `9f25965`, `2c0fb40`, `8ecc379`, `533e9f3`, `83ce380`, `4d3f21f`, `838070c`,
`b6ca3b1`, `926df57`, `39a1a3c`, `0d1c6b6`, `af0a11d`, `023b7c4`

### String literals, a build script's working directory, and two questions measured
2026-09-23

**What.** String literals that still named a round or stated the project owner's own decision
were reworded to what they mean, in the engine's log lines and menu text and in the
diagnostic tools' banners and check names. `Preparation/Tools/Diagnose/bpr.bat` gained the
`cd /d "%~dp0"` line the project's other build scripts already carry. The device check in
`specific/hd/hd_render.cpp` now reads the ps_3_0 instruction slot cap
(`D3DCAPS9::MaxPixelShader30InstructionSlots`) and refuses a device below 512 slots, in place
of the ps_2_x cap (`PS20Caps.NumInstructionSlots`) tested against a stale floor of 320. A
comment at `HDRenderGroupSampleCount` states that the function has no caller. The fog-bulb
cost table in `specific/hd/hd_shader/hd_room.hlsl` was re-measured: each bulb costs about 4.5
instruction slots in each of the two pixel shaders, room/model come to 431/303 at 6 bulbs,
440/312 at 8 and 476/348 at 16, of the 512 ps_3_0 guarantees (fxc 10.0.26100).

**Why.** `bpr.bat` run from another folder failed to find its source file and left a stray
object file behind; the build scripts it now matches already avoid that. The ps_2_x cap and
the number 320 described neither the device the project requires nor the shader it ships,
which had grown past both. The bulb table's own numbers were last measured against an earlier
shader and no longer matched the one shipped today.

**How.** Two questions the comment pass raised were measured and left unchanged: the first
person camera tests its own conditions in the same priority order the engine's own binocular
camera already used, so nothing there needed changing; and the model shader's fog-bulb term,
named in a comment `83ce380` added to `hd_model.hlsl`, is written differently from the room
shader's own, a difference neither file's history explains, so a comment states the fact
rather than the code being changed to match. `e041c3f` also restates the device floor in
`TOMB5 Code/MODIFICATIONS.md` and `docs/Engine-Manual.md`, regenerates the `.inc` shader
files, adds a comment naming what `RoomProbe.cpp` actually prints, and removes the last
round, plan and project-owner references from `hd_pagecache.h`, `game/accent.inc`,
`game/chardef.inc` and ten level reports.

**Commits.** `bc0b13c`, `6a1854d`, `9c7d1e5`, `d28d251`, `bcdbc7d`, `e041c3f`

## 3. Randomizer changes since Beta 1.0

The randomizer itself - `TR5Randomizer/`, the CLI `Randomize.exe` and the GUI
`TR5Randomizer.exe` - is this project's own work from the first line, so it owes
nobody a GPLv3 section 5(a) notice and has no record of the kind above. What
changed in it since the public Beta 1.0 is listed here by task number.
Twenty-one files under `TR5Randomizer/src` changed in that span.

- **#123 - the HD markers.** `Tr5Level.cpp` / `.h`, `ApplyPatch.cpp` / `.h`,
  `Randomize.cpp` and `gui/Main.cpp`: the randomizer writes the HD room markers
  into the `.TRC` so the engine can find the right room again after the levels
  have been shuffled, keeps a marker room's HD art across the portal cut, and
  carries material 16 as the still-water surface.
- **#137 - secret lists.** `SecretList.cpp`, `ApplyPatch.cpp`, `Tr5Level.cpp` /
  `.h`: two further forms in the secret list, and geometry built for rooms that
  have none.
- **#141 - where an enemy may stand.** `Randomizer.cpp` / `.h`,
  `ApplyPatch.cpp` / `.h`, `Tr3Source.cpp` / `.h`, `Tr5Level.cpp` / `.h`: the
  headroom check, the Hydra's footprint of five free tiles out of nine, and its
  doubled weight in the kind roll.
- **#142 - the data baked into the binary.** `EmbeddedData.cpp` and `.h` are new
  and **generated** by `Preparation/Tools/EmbedData.ps1` - the fifteen
  `zones_*.json` and the two checksum tables as zlib streams, 7,186 lines.
  `core/DataFile.cpp` is new and hand-written: it unpacks them, looks a name up
  and implements the rule that a real file in `data\` wins and the baked copy is
  the floor. It is kept apart from the generated file on purpose, so
  regenerating the tables cannot overwrite logic. `DataFolder.cpp` / `.h` gained
  `loadChecksumsText()`, a second entry point for callers that hold the checksum
  text rather than a path.
- **#144 - the weather roll.** `Config.cpp` / `.h`, `Randomizer.cpp` / `.h`,
  `ApplyPatch.cpp` / `.h`, `Tr5Level.cpp` / `.h`, `Randomize.cpp`,
  `gui/Main.cpp`, `gui/Language.cpp`: the option, the roll and the level fields
  it writes. Six of the thirteen playable levels can show it.
- **#146 - the cyborg in the cross pool.** `Randomizer.cpp` and
  `gui/Language.cpp`: slot 469 joins the pool the cross mix draws from.
- **#147 - the flipmap mirror.** `ApplyPatch.cpp` / `.h`, `Randomizer.cpp`,
  `Randomize.cpp`, `gui/Main.cpp`: an enemy waker moved into a room is mirrored
  into that room's flipmap partner, and (#147b) a moved key trigger with it.
- **#153 - the Mods folder.** `Config.cpp` / `.h`, `Randomizer.cpp` / `.h`,
  `Tr5Level.cpp` / `.h`, `ApplyPatch.cpp`, `Randomize.cpp`, `gui/Main.cpp`,
  `gui/Language.cpp`: the randomizer rolls out of the same scan the engine's
  wardrobe uses, and knows again when there are no scanned outfits.
- **#172 - the push audit.** `core/BuildId.h`: `RANDO_BUILD_ID` 18 to 19 with a
  ledger line naming the four commits since Beta 1.0 that move where an enemy
  may stand (`38b8bd5`, `9b7b827`, `4dd8c6c`, `5e087f7`); the number is printed
  in the launcher and written into `spoiler.txt` and is not part of the seed.
  `core/Tr5Tr3Import.cpp` got the project's licence header, the only source file
  under `src/` that lacked it. `gui/Main.cpp` and `gui/Language.cpp` lost the
  never-wired checkbox *Patch remaster textures* with its tooltip and both
  language rows, and the folder picker's German title now goes through the
  translation table like every other string. `TR5Randomizer/.gitignore` keeps
  the tracked `build/b1.bat` and `b2.bat` out of its own blanket ignore. The
  change is harmless in the sense `Preparation/Verification.md` prescribes: the
  same seed 380 through the old and the new build gives byte-identical `.trc`
  and `.hdm` for a bare `andy1` run and for a `rich2` run with the enemy, weapon
  and cross-mix switches on.

## 4. Building this yourself

The published binary need not be trusted. Either:

- build from this tree directly - open `tomb5.sln`, configuration `Release`,
  platform `x86`; the result is `release/PCtomb5.exe`. `Release|x64` builds the
  same sources (#143) and is there for whoever wants it; `x86` is the shipped
  configuration and the one every measurement in this record was made on. The
  four `TOMB5/specific/hd/hd_shader/*.inc` are `fxc` output of the two `.hlsl`
  beside them and are tracked, so nothing has to be regenerated to get a working
  binary - but after editing a shader, run
  `Preparation/Tools/Diagnose/bshader.bat` (`be.bat` carries the same guard and
  regenerates them); or
- start from upstream <https://github.com/Trxyebeep/TOMB5> and apply these
  changes on top. Every change is a commit in this repository, so
  `git diff "$(git rev-list --max-parents=0 HEAD)" HEAD -- "TOMB5 Code"`
  is the complete change set. It asks git for the repository's first commit -
  the unmodified decompilation - rather than naming a hash, deliberately: this
  history has been rewritten before, and a written-down hash does not survive
  that.

Unlike upstream, this build produces a **single self-contained executable**.
Upstream ships `PCTomb5.exe` plus `tomb5.dll` - **the upstream TOMB5 project's
own DLL, built from this very source tree's ancestor, and not the `tomb5.dll` of
the Tomb Raider IV-VI Remaster, which this project never opens, links or reads**
(the two names are spelled the same and mean different files). The upstream DLL
carries the game code and does its work from `DllMain`; that is why upstream
updates can ship as "dll only". This fork builds the Win32 target as an ordinary
application with its own `WinMain`, so there is no DLL.

**No game data is included, here or in any release made from this tree.** Tomb
Raider Chronicles itself remains the property of its rights holders; a copy of
the game is needed to play.

## Appendix: task numbers in commit subjects

Commit subjects carry task numbers (`#NNN`). This table maps them to the entries above.

| task | entries whose commits carry it |
|---|---|
| #32 | Level Select, a keyhole that survives a save, and the underwater pickup animation |
| #33 | The options menu as one table-driven frame; A credits screen |
| #36 | randomizer or tools only - see `git log --grep=#36` |
| #53 | Level Select, a keyhole that survives a save, and the underwater pickup animation |
| #61 | Enemies that could not die, and corpses that never left |
| #71 | randomizer or tools only - see `git log --grep=#71` |
| #72 | randomizer or tools only - see `git log --grep=#72` |
| #73 | randomizer or tools only - see `git log --grep=#73` |
| #74 | randomizer or tools only - see `git log --grep=#74` |
| #76 | randomizer or tools only - see `git log --grep=#76` |
| #78 | Skipping cutscenes and the intro flyby with the controller |
| #79 | Enemies that could not die, and corpses that never left |
| #81 | Enemies that could not die, and corpses that never left |
| #83 | Engine-only object slots beyond the original 460; Distance models switched off for enemies |
| #84 | Engine-only object slots beyond the original 460; An enemy's animations come from its own slot |
| #86 | randomizer or tools only - see `git log --grep=#86` |
| #87 | randomizer or tools only - see `git log --grep=#87` |
| #88 | Four Tomb Raider III enemies, with tomb3's control code |
| #90 | Four Tomb Raider III enemies, with tomb3's control code |
| #91 | Four Tomb Raider III enemies, with tomb3's control code |
| #92 | Four Tomb Raider III enemies, with tomb3's control code |
| #93 | Four Tomb Raider III enemies, with tomb3's control code |
| #94 | Bounds checks, a log that survives a crash, a glyph guard, and the build |
| #95 | randomizer or tools only - see `git log --grep=#95` |
| #96 | randomizer or tools only - see `git log --grep=#96` |
| #97 | Four Tomb Raider III enemies, with tomb3's control code |
| #98 | Four Tomb Raider III enemies, with tomb3's control code |
| #99 | Four Tomb Raider III enemies, with tomb3's control code |
| #100 | Music and FMV sound off DirectSound |
| #101 | randomizer or tools only - see `git log --grep=#101` |
| #102 | randomizer or tools only - see `git log --grep=#102` |
| #103 | randomizer or tools only - see `git log --grep=#103` |
| #104 | randomizer or tools only - see `git log --grep=#104` |
| #105 | Engine-only object slots beyond the original 460 |
| #106 | Level Select, a keyhole that survives a save, and the underwater pickup animation |
| #107 | randomizer or tools only - see `git log --grep=#107` |
| #108 | Balance.txt: every weapon, enemy, attack and hazard settable |
| #109 | randomizer or tools only - see `git log --grep=#109` |
| #111 | Engine-only object slots beyond the original 460 |
| #112 | randomizer or tools only - see `git log --grep=#112` |
| #113 | randomizer or tools only - see `git log --grep=#113` |
| #114 | The stationary level ambience: a stacking guard and a range brake |
| #115 | Real controller support |
| #117 | Optional high quality sound bank from the player's remaster |
| #118 | randomizer or tools only - see `git log --grep=#118` |
| #119 | randomizer or tools only - see `git log --grep=#119` |
| #120 | randomizer or tools only - see `git log --grep=#120` |
| #121 | An enemy's animations come from its own slot |
| #122 | Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn |
| #123 | Finding an installed Remaster, reading what it holds for a level, and the settings that steer it; The second renderer, the seam where the engine draws HD rooms, and what a .TRG material means; The room shader, and a build that needs no shader compiler; HD models for items, enemies and machinery; The engine's own per-vertex lighting, as shader arithmetic; The seam for Lara: the HD composite drawn by the engine; Address space, the page cache and what the machine can pay for; The first person view; First person: Lara's head and neck cut away, and the lid that closes the opening; The portal walk, the window on intruding art, and the doorway a randomized run opens; Water in the HD rooms; HD models for the pickups: the EXTRA prefix, a mod's own inventory model, and lying on the floor; The inventory ring and the pickup display in HD; The font in HD |
| #124 | Enemies that could not die, and corpses that never left |
| #125 | The -log switch and the diagnostic seams |
| #126 | The options menu as one table-driven frame |
| #127 | The first person view |
| #128 | randomizer or tools only - see `git log --grep=#128` |
| #129 | Balance.txt: every weapon, enemy, attack and hazard settable |
| #134 | The first person view |
| #137 | randomizer or tools only - see `git log --grep=#137` |
| #139 | The keyboard: two binding sets, bindable hotkey roles, and the look key's turn on the spot |
| #141 | Licence notices, origin sentences and the corrections made before the public push |
| #142 | randomizer or tools only - see `git log --grep=#142` |
| #143 | (outside an entry); Building this yourself |
| #144 | randomizer or tools only - see `git log --grep=#144` |
| #146 | Engine-only object slots beyond the original 460; The VCI cyborg as a pool enemy on a slot of his own |
| #147 | randomizer or tools only - see `git log --grep=#147` |
| #148 | The frame rate option and the clock; The in-between frame: the root snapshot; The pose between two ticks; Particles on the same time axis; HD models for items, enemies and machinery; The engine's own per-vertex lighting, as shader arithmetic; First person: Lara's head and neck cut away, and the lid that closes the opening; The second renderer, the seam where the engine draws HD rooms, and what a .TRG material means; The portal walk, the window on intruding art, and the doorway a randomized run opens; Every per-frame counter on the logic tick; The camera on the in-between frame; The arms between two ticks |
| #149 | HD loading screens from the Remaster, when they are there; The logo screen and the title menu wordmark |
| #150 | Keys are counted, not flagged |
| #151 | The logo screen and the title menu wordmark |
| #153 | The Mods folder: models, texture pages and sounds from a mod; Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn; The seam for Lara: the HD composite drawn by the engine; HD models for the pickups: the EXTRA prefix, a mod's own inventory model, and lying on the floor; Imported enemy sounds are named in the sidecar, not guessed at by length; F4 changes Lara's outfit inside the level; The outfit chooses the footsteps, and a bare foot silences the floor layer; Level Select, a keyhole that survives a save, and the underwater pickup animation |
| #154 | The inventory ring at the frame rate; The title menu at the frame rate, one tap one row |
| #155 | The Mods folder: models, texture pages and sounds from a mod |
| #155b | HD models for the pickups: the EXTRA prefix, a mod's own inventory model, and lying on the floor |
| #156 | Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn |
| #157 | The animation's root offset on a refused frame |
| #158 | Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn |
| #159 | Lara's HD wardrobe: a scan of the installed art, its properties, and which outfit is worn |
| #160 | The cut scenes at the frame rate |
| #161 | Modern control: the stick turned into the classic direction bits; The options menu as one table-driven frame; The modern camera: a free orbit on the right stick; The pad: radial dead zones, a second binding set and a Modifier for chords; The -log switch and the diagnostic seams; The keyboard: two binding sets, bindable hotkey roles, and the look key's turn on the spot; Finding an installed Remaster, reading what it holds for a level, and the settings that steer it; Combat, Sprint, Duck and Grab: hold or toggle; Two action keys: the interaction bit and the Secondary role; The cheats: DOZY as a toggle, and five bindable hotkeys |
| #164 | Address space, the page cache and what the machine can pay for |
| #169 | The in-between frame: the root snapshot |
| #171 | Ledge hang: an automatic turn and lowering at an edge |
| #172 | (outside an entry); Licence notices, origin sentences and the corrections made before the public push; Optional high quality sound bank from the player's remaster |
| #173 | The record: from an eighteen-thousand-line diary to a licence document; Comments in the engine, the randomizer and the tools say what, why and what for; String literals, a build script's working directory, and two open questions |
| #177 | The drawn eye at a corner; The Modern eye clear of a wall |
| #181 | The room's inside decides, per frame, and the back of a wall is not drawn; A room the reach alone lists draws none of its art in a listed holder; The reach can be switched off; The reach-only answer goes back to the smaller box |
| #182 | The material 15 books in motion: a fan's shadow that turns, and flames that play |
| #183 | The material 15 books in motion: a fan's shadow that turns, and flames that play |
| #184 | The pad's scheme repair never displaces the Modifier |
| #186 | A floor whose page is part-way clear is drawn blended, and a mirror shows the room's items |
