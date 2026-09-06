# 60 frames a second for the TOMB5 port (#148) - implementation plan

Written 2026-09-06 from a read-only survey of branch `hd-runtime` (head
`46a2f31`, HD part complete: rooms, objects and Lara from the Remaster,
seam in `InsertRoom` / `DrawAnimatingItem` / `DrawLara`, matrices off
the engine's own stack), of TROne (GPLv3, `C:\Cloude AI\Todesengel\
TROne-main Aktuell`) and of the frame path of this engine. Nothing was
changed. Every line number is from this tree. Nothing was read from
`tomb5.dll`, `tomb456.exe` or `tomb123.exe`.

Paths are relative to `TOMB5 Code\TOMB5\` unless written out in full.


## 0. Findings - where the 30 comes from, and what already interpolates

### 0.1 The clock is already a 60 Hz clock. Only two lines make it 30.

| # | what | file:line | measured |
|---|---|---|---|
| F1 | the tick source | `specific\time.cpp:34` | `frequency = pfq / 60;` - `Sync()` (`:8`) returns whole 1/60 s ticks since the last call and carries the remainder in `counter`. There is no 30 anywhere in this file. |
| F2 | the present gate | `specific\output.cpp:1277-1294` | `S_DumpScreen()`: `n = Sync(); while (n < 2) { while (!Sync()); n++; }` - this busy-wait is the 30 Hz cap. It returns `n`, i.e. how many 1/60 s ticks the frame really took. |
| F3 | the logic accumulator | `game\control.cpp:261` | `for (framecount += nframes; framecount > 0; framecount -= 2)` - `framecount` (`:121`) is counted in 1/60 s units and a logic step costs 2 of them. Feed it `nframes = 1` per drawn frame and it runs one logic step on every second call. Exactly 30 logic ticks per 60 presents, with no change to this loop at all. |
| F4 | the main loop | `game\gameflow.cpp:1014-1042` (`DoLevel`), `:836-849` (`DoTitle`) | `nframes = DrawPhaseGame(); ... gfStatus = ControlPhase(nframes, 0);` - draw first, then logic. `framecount = 0` and one priming tick at `:1005-1007`. |
| F5 | the draw phase | `game\draw.cpp:394-411` | `CalcLaraMatrices(0/1)` -> `DrawRooms(camera.pos.room_number)` -> `DrawGameInfo(1)` -> `S_OutputPolyList()` -> `camera.number_frames = S_DumpScreen()` -> `S_AnimateTextures(camera.number_frames)`. `camera.number_frames` is used nowhere else. |
| F6 | the one present | `specific\dxshell.cpp:238` | `Present(0,0,0,0)` - the engine's only present; `HDFramePresented()` sits right after it. |
| F7 | vsync | `specific\dxshell.cpp:455` | `PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE` in a windowed device (`:448`). There is no vsync today; the 30 Hz comes purely from F2's spin. |
| F8 | the loading bar already runs at 60 | `specific\output.cpp:1296-1313` | `S_DumpScreenFrame()` waits for `n < 1`, i.e. it presents at up to 60 Hz already (throttled by a `Sleep(1)` at `specific\file.cpp:1870`). A 60 Hz present rate is already proven on this device and driver in this build. |
| F9 | what a real session measured | `docs\Engine-Manual.md:1473-1478` | `FPSLOG level=ANDREA1 frames=30 ... slow=13 late=0 hd_rooms=17 hd_items=9 hd_draws=152`; a second later `frames=24 max_ms=66.9 late=6 hd_rooms=23 hd_items=14 hd_draws=241`; level summary `seconds=418 frames=12461 worst_ms=83.4 late_frames=39`. HD at 30 Hz already misses its slot 39 times in a seven-minute level. The budget constants are hard-wired to 30 Hz at `specific\hd\hd_level.cpp:247-248`. |

So: the title screen, the game and the menus all pass through the same
`S_DumpScreen`, and the only thing standing between this build and 60
presents a second is the `< 2` at `specific\output.cpp:1283`. What is
not free is that everything downstream assumes one draw per logic tick
- see 0.4.

### 0.2 Interpolation that already exists - and what it interpolates

It interpolates animation keyframes, not time. There is a complete
second matrix stack.

| what | file:line | note |
|---|---|---|
| the second stack | `game\draw.cpp:56-61, 122-130` | `IMstack[768]` (int) and `aIFMStack[768]` (float), `IM_frac`, `IM_rate`; `InitInterpolate(frac, rate)` copies the current top of both stacks into them. |
| the paired stack ops | `game\draw.cpp:132-251` | `phd_PushMatrix_I`, `phd_PopMatrix_I`, `phd_RotX/Y/Z_I`, `phd_TranslateRel_I`, `phd_TranslateRel_ID` (two different translations), `phd_RotYXZ_I`, `gar_RotYXZsuperpack_I` (two independent rotation streams). |
| the blend | `game\draw.cpp:296-302` (`phd_PutPolygons_I`), `:304-392` (`aInterpolateMatrix`), `:1508-1557` (`InterpolateMatrix`), `:1559-1621` (arm matrices) | The blend factor is hard-coded to exactly 1/4, 1/2 and 3/4 - branches on `IM_rate == 2`, `IM_frac == 2 && IM_rate == 4`, `IM_frac == 1`, else. There is no float factor anywhere. |
| where the pair comes from | `game\draw.cpp:1183-1207` (`GetFrames`) | `rate = anim->interpolation & 0xFF`; `frac = (item->frame_number - anim->frame_base) % rate`. `frac == 0` -> no interpolation. |
| users | `game\draw.cpp:594, 705-770` (`DrawAnimatingItem`), `game\delstuff.cpp:62-100, 426+` (`CalcLaraMatrices` / `Rich_CalcLaraMatrices_Interpolated`), `game\hair.cpp:297-345` (`HairControl`, both halves), `game\sphere.cpp:25-45, 163-185` (`GetSpheres`, logic side), `game\draw.cpp:92-120` (`GetBoundsAccurate`, `GetBestFrame`) | |

This is the machinery a time interpolation needs, already written,
already shipped, already trusted by the HD seam. What it lacks is (a)
a float factor and (b) a previous tick's frame pointer instead of a
previous keyframe.

### 0.3 The HD seam takes its matrices off the same stack - so it follows for free

| what | file:line | note |
|---|---|---|
| rooms | `specific\drawroom.cpp:170` | `if (HDFrameRoom((int)(r - room), aMXPtr))` - the room is recorded with the matrix that is on the stack right now, i.e. `view * world` as `aSetViewMatrix()` handed it to the device (`game\draw.cpp:1090-1099`). |
| objects | `specific\hd\hd_object.cpp:1544-1565` (`HDObjectNode`) | In the `frac` branch it does `phd_PushMatrix(); InterpolateMatrix(); memcpy(g_raw + node*12, aMXPtr, 48); phd_PopMatrix();` - it records exactly the matrix the classic mesh would have been drawn with. |
| Lara | `game\delstuff.cpp:102-160` / `:426+` | Both branches start from `lara_item->pos` and write `lara_matrices` / `lara_joint_matrices`, which is what `hd_laradraw.cpp` reads. |
| the frame bracket | `specific\output.cpp:1454` (`HDFrameReset`), `:1509` (`HDFrameDraw`), `:1514` (`HDFrameDrawGlow`); `specific\hd\hd_level.cpp:2013` | Rebuilt from scratch every drawn frame, capped at `HD_MAX_FRAME 128` rooms and `HD_OBJ_ITEMS 128` objects x `HD_OBJ_NODES 32`. |
| the HD motion clock | `specific\hd\hd_level.cpp:162-195` | `AdvanceClock()` accumulates `(ushort)(GlobalCounter - last) / 30.0` - it is driven by `GlobalCounter`, which is stepped once per logic tick (`game\control.cpp:263`), not per drawn frame. A second draw inside the same tick adds 0 seconds. The HD scroll, wind and water clock is already safe against a doubled present rate. Same for `wibble` (`game\control.cpp:600`), which the HD shaders read. |

Consequence, and the whole reason way A is cheap here: if the
interpolated position and rotation are written into `item->pos`,
`lara_item->pos` and `camera` before the matrix stack is built, the HD
path follows automatically. Not one line of `specific\hd\` has to
change.

### 0.4 What counts per drawn frame instead of per logic tick - the risk list

This is the real work of way A. Every entry below runs twice per logic
tick if the frame is drawn twice.

| # | what | file:line | what breaks at 60 | risk |
|---|---|---|---|---|
| R1 | `Fade()` - `ScreenFade += ScreenFadeSpeed` | `game\tomb4fx.cpp:1304-1345`, called from `specific\output.cpp:1518` and `:1562` | every screen fade runs at double speed | high, visible everywhere |
| R2 | `DoScreenFade()` - `FadeVal += FadeStep; FadeCnt++; if (FadeCnt > 8) DoFade = 2` | `specific\specificfx.cpp:1267-1281`, called from `specific\output.cpp:1578` | death fade, level-change fade and the load fade-in loop `specific\output.cpp:739-751` all halve; `DoLevel`'s death path waits on `DoFade == 2` (`game\gameflow.cpp:1062-1068`) | high |
| R3 | `FlashFader -= 2` | `specific\output.cpp:1564-1570` | weapon and pickup flash halves | medium |
| R4 | `FlashIt()` - `flash_count`/`flash_state` | `game\health.cpp:41-55`, via `DrawGameInfo` (`:65`) | health-bar blink doubles | low, cosmetic |
| R5 | `DrawPickups()` - `pu->life--`, `PickupX`, `PickupVel` | `game\health.cpp:193-221` | the pickup slide-in doubles | low |
| R6 | `item->after_death++` and `KillItem(i)` | `game\draw.cpp:935-939` (inside `PrintObjects`) | corpses fade out and are deleted twice as fast. The `!(wibble & 3)` guard is dead: `wibble` steps by 4 and is masked `& 0xFC` (`game\control.cpp:600`), so `wibble & 3` is always 0 | high - this mutates game state from the draw path |
| R7 | the sky lightning machine | `game\draw.cpp:432-455` and `UpdateSkyLightning` `:524-556` | flicker rate doubles, `LightningSFXDelay--` halves the thunder delay | medium |
| R8 | rain and snow simulation | `specific\specificfx.cpp:1321-1327` (`DoWeather`), `:1329` (`DoRain`, movement at `:1383-1436`), `:1556` (`DoSnow`) - called from `game\draw.cpp:1121` | rain and snow fall at double speed and drops die twice as fast | medium-high, very visible |
| R9 | `aWibble++` | `specific\alexstuff.cpp:190-193`, called from `game\draw.cpp:1145`; read at `specific\specificfx.cpp:3576, 4237` | that UV scroll doubles | low |
| R10 | the poison filter inside the view matrix | `specific\3dmath.cpp:650-656` inside `phd_GenerateW2V` | `lara.poisoned += (dpoisoned - poisoned) >> 4` runs once per `phd_LookAt`. Camera interpolation means one `phd_LookAt` per drawn frame -> the poison ramp doubles. The engine already knows this hazard: `specific\output.cpp:726-728, 763` saves and zeroes `lara.poisoned` around its fade loop. | medium |
| R11 | `S_DrawSparks` detaches sparks from their item | `specific\specificfx.cpp:2003-2009` and `:2044-2050` | a random-timed one-shot fires twice as often; harmless but not free | low |
| R12 | `GnFrameCounter++` | `specific\output.cpp:1289, 1308`; read at `game\text.cpp:409` (blink), `specific\output.cpp:73` (pickup spin), `game\newinv2.cpp:3062` | text blink and pickup spin double | low |
| R13 | the modal loops that present themselves | inventory `game\newinv2.cpp:663-664`; load/save `specific\LoadSave.cpp:1404-1405, 1879-1880, 2067-2068`; death menu `game\control.cpp:235-236`; load fade `specific\output.cpp:748-749, 760-761` | every one of these steps its own animation once per present. They must stay at 2 ticks | high, but trivially fixed (1.4) |
| R14 | `RegeneratePickups()` | `game\control.cpp:251` (outside the tick loop) | runs per `ControlPhase` call, i.e. twice per tick - idempotent re-check | low |
| R15 | `resChangeCounter -= long(30 / App.fps)` | `specific\output.cpp:1473` | already frame-rate aware; would need `60 /` in 60 Hz mode | trivial |

Safe by construction (verified, no action needed): `S_UpdateInput` and
all debounce (`game\control.cpp:267`, `SetDebounce` `:259/:314` - not
reached on a zero-tick call); `GlobalCounter` and `wibble` (`:263,
:600`); `WeaponDelay--` (`:510`); `health_bar_timer--` (`:636`);
`GameTimer++` / `savegame.Level.Timer++` (`:640-643`); all `Update*`
particle simulations (`:614-630`); `UpdateFadeClip` (`:646`);
`UpdateSky`, `RPC_Update` (`:264-265`); `HairControl` (`:584-587`);
`CalculateCamera` / `CalculateSpotCams` (`:593/:596`); the cutscene
frame counter `GLOBAL_cutseq_frame++` (`game\deltapak.cpp:3113`,
reached only through `CalculateCamera` at `game\camera.cpp:380`); the
first-person filters `fpVel`/`fpSpd`/`fpRel` (`game\camera.cpp:1496-
2145`, one call per tick from `CalculateCamera` `:536/:583`);
`S_AnimateTextures` (`specific\output.cpp:864-874`, `for (comp += n;
comp > 5; comp -= 5)` - it takes the tick count as an argument and is
therefore wall-clock correct at any present rate).

And the single most important safety property: the logic RNG and the
draw RNG are separate generators - `GetRandomControl` on `rand_1`,
`GetRandomDraw` on `rand_2` (`specific\function_stubs.cpp:71-91`).
Extra drawn frames consume only `rand_2`. They cannot desynchronise
the game. That is what makes the whole approach provable (see 4).

### 0.5 TROne: no, it has no 60 FPS and nothing to carry over here

Measured, not assumed:

- `TROne-main Aktuell\games\tr5\src\specific\time.cpp:34` -
  `frequency = pfq / 60;`, byte-for-byte our file.
- `...\games\tr5\src\specific\output.cpp:1220-1236` - `S_DumpScreen()`
  with the same `while (n < 2)`.
- `...\games\tr5\glue\hostglue_draw5.cpp:1406-1453` - the host
  replacement keeps the identical contract, clamped at 10, backlog
  dropped.
- `...\host\main\host_api_impl.cpp:38, 83` - the host tick source is
  documented as a 30 Hz tick accumulator, `elapsed * 30 / 1000`.
- `...\host\main\main.cpp:209` - `SDL_GL_SetSwapInterval(vsync)`; the
  only frame-rate knob is vsync on or off.
- A grep for `interp`, `lerp`, `frame_frac`, `alpha`, `accumulator`,
  `fixed timestep` across `host`, `games/tr5/glue` and `docs` finds
  only shading interpolation and the 30 Hz clock comments.

TROne renders exactly the 30 Hz frames. There is nothing to borrow, and
therefore no GPLv3 attribution to add for this work.

### 0.6 The known pattern (TR1X, OpenLara, the Remasters) - and how it maps onto this tree

The state of the art is: logic stays at 30 Hz, the renderer runs faster
and draws a state interpolated between the last two logic states.

| the pattern | where it lands here |
|---|---|
| snapshot of the previous tick (item pos/rot, camera, Lara, hair) | a new `game\interpolate.cpp`; captured at `game\control.cpp:263` |
| render with `alpha = (now - lastTick) / tickLen` | `alpha` from `framecount` parity - see 1.4 |
| a second matrix stack lerped by `alpha` | already exists: `game\draw.cpp:56-302, 1508-1621` |
| short-angle lerp (rotate the short way round) | `(short)(a + (short)(b - a) * alpha)` - signed short wrap does it for free |
| snap on teleport, level change, camera cut | 1.3 |
| animation-frame change handling | 3, order 4 |
| particles usually left uninterpolated | 3, order 5 (optional) |


## 1. The seam and the snapshot

### 1.1 Three call sites. That is the whole seam for the first, decisive stage.

New files `game\interpolate.cpp` / `game\interpolate.h`.

| # | hook | file:line | what |
|---|---|---|---|
| 1 | capture | `game\control.cpp:263`, as the first statement inside the `for (framecount += nframes; ...)` body, before `GlobalCounter++` | `InterpCapturePrev();` - the state before this tick. After a multi-tick `ControlPhase` it holds the state before the last tick, which is the only one that matters. |
| 2 | apply | `game\draw.cpp:396`, first line of `DrawPhaseGame` | `InterpBeginFrame(alpha);` - writes the interpolated values into the live structures and keeps a copy of what it overwrote. |
| 3 | restore | `game\draw.cpp:410`, after `S_AnimateTextures(...)`, before `return` | `InterpEndFrame();` - puts the exact original bytes back. |

Nothing downstream knows. `DrawRooms`, `InsertRoom`, `phd_PutPolygons`,
`DrawBuckets`, `HDFrameRoom`, `HDObjectNode`, `HDLaraDraw`, the sort
list, the shadows and the lighting all read the same globals they read
today, and get the interpolated values.

### 1.2 The snapshot

```
struct InterpItem {          // 24 bytes
    long  x, y, z;           // item->pos.x_pos / y_pos / z_pos
    short xr, yr, zr;        // item->pos.x_rot / y_rot / z_rot
    short room;              // item->room_number   (snap detector only, never lerped)
    short anim, frame;       // for order 4 and the snap detector
    uchar valid;             // 0 = no previous state, draw live
};
```

| set | count | source | size |
|---|---|---|---|
| items | `MAX_ITEMS` = 512 (`global\types.h:45`) | `items[i].pos`, `.room_number`, `.anim_number`, `.frame_number` | ~12 KB |
| effects | 24 (`game\items.cpp:390`) | `effects[i].pos` | 0.6 KB |
| hair | 2 x 7 (`game\hair.cpp:39`, `HAIR_STRUCT` at `global\types.h:606`) | `hairs[i][j].pos` - required, or the braid detaches from an interpolated head | 0.4 KB |
| camera | 1 | `camera.pos.{x,y,z}`, `camera.target.{x,y,z}`, `camera.pos.room_number`, `camera.type`, and `fpRoll` (`game\camera.cpp:63`, needs a getter) | 60 B |

About 13 KB, one static block, no allocation. `lara_item` is an
`items[]` entry, so it is covered by the item set.

Deliberately not in the snapshot for the first stages: `spark[1024]`
(`game\tomb4fx.cpp:32`), rain and snow, debris, blood, drips, bubbles,
gunshells, rats, bats, spiders, ropes, shockwaves, lightning. Reason in
3, order 5.

### 1.3 Where interpolation must be switched off (snap rules)

| trigger | test | why |
|---|---|---|
| no previous state | `!valid` | first frame of a level, after a load, after the inventory |
| teleport | `abs(dx) + abs(dy) + abs(dz) > 512` for one tick | nothing in TR5 moves half a sector in 33 ms under its own power; a teleport, a `TranslateItem` animation command (`game\control.cpp:3241`) or a flip-map move does |
| room change with a jump | `room` changed and the distance test above trips | a portal walk is normal and must interpolate; a teleport must not |
| camera cut | `camera.type != previous type`, or a distance jump over ~1024, or `camera.fixed_camera` changed | fixed cameras and spot-cam cuts snap by design (`game\camera.cpp:272-607`, `game\spotcam.cpp:329, 511`) |
| cutscene | `GLOBAL_playing_cutseq` | the cutscene camera is decoded per frame index (`game\deltapak.cpp:3050-3119`); interpolating it would fight the decoder. Snap it, or leave cutscenes at 30 entirely |
| whole-state invalidation | `InterpInvalidate()` at `game\gameflow.cpp:1006` (beside `framecount = 0`), at level free, after `S_CallInventory2`/`S_LoadSave`/`S_PauseMenu` return (`game\control.cpp:320, 361-370`), and on `JustLoaded` | |

`camera.pos.room_number` is never interpolated. `DrawRooms(camera.pos.
room_number)` (`game\draw.cpp:405`) drives the whole portal walk and
the clip windows; a half-step across a portal would flip the room list.
The live room number is used with the interpolated eye position - a
sub-33 ms mismatch that nobody can see, and the same compromise TR1X
makes.

### 1.4 The present policy

`S_DumpScreen` gets a module-level minimum instead of a constant:

- `specific\output.cpp:1283` - `while (n < 2)` becomes `while (n <
  gPresentMinTicks)`, default 2.
- `game\draw.cpp:394` - `DrawPhaseGame` sets it from `tomb5.fps_mode`
  (1 for 60 Hz) around its own present and puts it back to 2 afterwards.
- Everything at R13 keeps 2 without touching a line: the inventory, the
  load/save screen, the death menu and the load fade stay at 30 Hz, so
  their per-present animation counters keep their timing.

`alpha` needs no clock of its own in the fixed 60 Hz mode. After
`ControlPhase` returns, `framecount` (`game\control.h:102`) is `-1` if
a tick ran and `0` if none did:

```
alpha = (framecount < 0) ? 0.5f : 1.0f;      // read at the top of DrawPhaseGame
```

That is exact, needs no `QueryPerformanceCounter`, cannot drift, and
costs one comparison. It adds at most one half tick (16.7 ms) of
display latency, on every second frame - average 8.3 ms. That is the
standard cost of this technique and is what TR1X and the Remasters pay
too.

For an uncapped or display-refresh mode `alpha` must come from a real
clock, and then `specific\time.cpp` needs sub-tick resolution: `Sync()`
returns whole 1/60 s ticks, so at 144 Hz two out of three frames would
return `0` and the spin at `specific\output.cpp:1285` would clamp the
whole thing back to 60. That is a separate order (3, order 6) and is
not needed for the goal in the title.


## 2. Way A against way B

### Way A - interpolation (logic 30 Hz, render 60 Hz)

In one sentence: let `S_DumpScreen` wait one tick instead of two, let
`ControlPhase`'s existing `framecount -= 2` accumulator do the halving
it was already built for, and draw the in-between frame from a lerp of
the last two logic states.

What is on our side here:

1. The accumulator already works (F3). No change to `ControlPhase`'s
   control flow.
2. The second matrix stack already exists and is already trusted by the
   HD seam (0.2, 0.3). It even has an exact 1/2 blend (`IM_rate == 2`),
   which is precisely the factor the fixed 60 Hz mode needs.
3. The HD path reads the engine's matrices (0.3). Interpolate before the
   stack and HD follows with zero HD changes.
4. The HD motion clock is already in seconds off `GlobalCounter`, so it
   does not double.
5. Draw and logic have separate RNGs, so extra frames cannot change the
   game.
6. 60 presents a second are already proven in this build on the loading
   bar (F8).
7. `FPSLOG` is already there to prove every stage.

What it costs: the risk list R1-R15, one new file, and a doubling of
the per-frame render work.

Honest cost per frame. Everything below runs once per drawn frame and
would run twice per logic tick: classic rooms transform and light every
room vertex on the CPU (`specific\drawroom.cpp:117-118, 195`); HD rooms
run `FillRoomLighting` + `HDRenderRoomMask` per room - measured 17-23
rooms per frame; HD objects build node matrices and per-node light per
item - measured 9-14 items; draw calls measured 152-241 per frame ->
304-482 per frame, 18,000-29,000 per second; bucket sorting and the
sort list.

Against F9 - `late_frames=39` in 418 s at 30 Hz with HD on - this says
plainly: 60 Hz and HD together will not hold on the machine that
produced that log in the heaviest rooms. The option must therefore be a
user option with a clean fallback (see 6).

### Way B - 60 Hz logic

| what | file:line | scale |
|---|---|---|
| the animation clock | `game\control.cpp:3206-3216` - `AnimateItem`: `item->frame_number++` once per tick; boundaries at `:3230` | every animation in every level file is authored in 30 Hz frames. Halving the step needs a fractional frame number in a `short frame_number` field (`global\types.h:820`) |
| the savegame | `game\savegame.cpp`, `specific\gamemain.cpp:145-236` | `frame_number`, `anim_number`, every `timer`, `GameTimer` and `savegame.Level.Timer` are serialised. Way B breaks every existing savegame. |
| the game clock | `specific\gamemain.cpp:166-169`, `game\health.cpp:79-89`, `game\control.cpp:640-643` | every displayed and stored time is `/30` |
| physics and speeds | 353 lines touch `fallspeed` in `game\` alone; `item->speed`, gravity constants, every `TranslateItem` | each needs halving with a rounding rule; TR positions are integers |
| animation and state code | 747 lines touch `item->frame_number` or `item->anim_number` in `game\` | every one is a hand check |
| timers and triggers | 53 counter step sites in `game\` + `specific\` | plus every OCB timer authored in the level data, which cannot be changed at all |
| the body of code | `game\` + `specific\` (without `hd\`) = 101,344 lines, ~100 object control routines | |
| the randomizer's balance | `game\balance.cpp`, `game\secretreward.cpp`, the enemy tables | all tuned against 30 Hz behaviour |

Verdict: reject way B. It is not a frame-rate change, it is a rewrite
of the game's simulation with a savegame break and a guaranteed loss of
vanilla parity - and the level data itself (animation frame counts, OCB
timers, trigger timings) cannot be converted at all without touching
the level files, which this project deliberately does not do. Way A
costs on the order of 1,500 new lines and touches no game rule; way B
costs the game.


## 3. Stages - each ends with a picture and a log line the user judges

| order | what | new lines (est.) | proof |
|---|---|---|---|
| 1. The clock split | `tomb5.fps_mode` in `tomb5_options` (`global\types.h:2631-2685`), registry read/write (`tomb5\tomb5.cpp:200-206, 333-337, 464-468` pattern), a menu row on the options page (`tomb5\troyestuff.cpp:143+`, `first_person_smooth` at `:726-786` is the exact pattern for a multi-value row). `gPresentMinTicks` in `specific\output.cpp:1283`, set and reset around the present in `game\draw.cpp:394-411`. `FPSLOG`'s budget constants (`specific\hd\hd_level.cpp:247-248`) follow `fps_mode`. No interpolation yet. | ~180 | `FPSLOG frames=60` while `GameTimer` still advances 30 a second. The picture is worse than today - every second frame is a duplicate, which is exactly what proves the clock is split and the logic is not. |
| 2. Root and camera | `game\interpolate.cpp/.h`: the snapshot of 1.2, the snap rules of 1.3, `InterpCapturePrev` / `InterpBeginFrame` / `InterpEndFrame`, and the camera re-`phd_LookAt` per drawn frame (`specific\3dmath.cpp:687`) with R10 guarded. Three call sites. Pose still steps at 30 Hz. | ~420 | Movement per drawn frame halves: a new `INTERPLOG` line per second with mean `|dpos|` of `lara_item` per frame, count of frames with `alpha != 1`, count of snaps. Screenshots of a run and of a turning camera. HD on and HD off must both be smooth - one proof for both, because the HD path was not touched. |
| 3. The per-frame side effects | Guard R1-R12 and R14-R15. The clean shape: a `bool InterpIsTweenFrame()` and an early return, or move the counter to the tick side. R6 (`game\draw.cpp:935-939`) must move into `ControlPhase` - it is game state being mutated from a draw. | ~150 | Fade lengths measured identical at 30 and 60 (count presents from `SetFade` to `DoFade == 2`). Rain falls at the same speed in both modes. Lightning cadence unchanged. |
| 4. The pose | Give `InterpolateMatrix` / `aInterpolateMatrix` / `InterpolateArmMatrix` / `aInterpolateArmMatrix` (`game\draw.cpp:304-392, 1508-1621`) one float `IM_alpha` in place of the four hard-coded branches; keep `InitInterpolate(frac, rate)` as `IM_alpha = frac/(float)rate` so the four existing cases reproduce bit-close. Add `GetFramesAt(item, prevFrame, curFrame, alpha, frm[2], &blend)` beside `GetFrames` (`:1183`). Feed the pairs in `DrawAnimatingItem` (`:594, 705`), `CalcLaraMatrices` (`game\delstuff.cpp:62-100`) and `HairControl` (`game\hair.cpp:297-345`). When `anim_number` changed between the two ticks, do not blend the pose - blend only the root. | ~350 | A slow walk cycle and a slow ladder climb at 60, side by side with 30, HD and classic. `HDObjectNode`'s `interpolated` branch (`specific\hd\hd_object.cpp:1549-1560`) must be exercised - a counter in `INTERPLOG`. |
| 5. Particles (optional) | Either snapshot `spark[1024]` + rain + snow and lerp them, or leave them at 30 (they are small, fast and short-lived). Recommended: leave them, and revisit only if the user's eye finds them. | ~200 if done | Two videos of the same waterfall or torch; the user chooses. |
| 6. Present policy (optional) | `D3DPRESENT_INTERVAL_ONE` behind a `tomb5.vsync` option (`specific\dxshell.cpp:455` + the `Reset` path at `:273`) - this also removes the `while (!Sync())` spin's CPU burn. Uncapped or display-refresh mode needs `specific\time.cpp` reworked to sub-tick resolution and `alpha` from a real clock. | ~150 | `FPSLOG` on a 60/120/144 Hz panel; no tearing; CPU load measured with and without the spin. |
| | total | ~1,450 over 6 orders | |

Orders 1-3 are the shippable unit. Order 4 is the polish. Orders 5-6
are the user's call.


## 4. Proofs

1. The logic is still 30 Hz. `GameTimer` (`game\control.cpp:640`) and
   `savegame.Level.Timer` (`:642-643`) must advance exactly 30 per
   wall-clock second in both modes. Visible in-game on a timed level
   and in the savegame header (`specific\gamemain.cpp:166-169`).
2. The HD motion clock is still in seconds. `GlobalCounter` differences
   over one second must stay 30. A waterfall's UV scroll must run at
   the same speed at 30 and 60. One-line `HDLOG` addition.
3. The frame rate really doubled. `FPSLOG frames=60 ... avg_ms=16.7`,
   with the budget constants moved to `1000/fps`.
4. The interpolation is really happening. New `INTERPLOG` line per
   second: frames with `alpha != 1`, mean per-frame `|d|` of
   `lara_item->pos` (must halve against the 30 Hz run over the same
   movement), interpolated HD nodes, and snap events by cause.
5. The game did not change. Because `rand_1` and `rand_2` are separate,
   a fixed-seed run with the same inputs must produce a byte-identical
   savegame at 30 and at 60. `S_SaveGame` already writes the whole
   `SAVEGAME_INFO` plus `tomb5_save_info` and the checksum path exists
   (`specific\gamemain.cpp:145-229`). The strongest single proof, and
   nearly free.
6. The picture did not change on a still frame. `fps_mode = 0` must
   produce a byte-identical frame to today (`DXSaveScreen`,
   `specific\dxshell.cpp:217-236`).
7. The timings did not change. Order 3's list, each measured: presents
   from `SetFade` to `DoFade == 2`; the pickup slide-in; the corpse
   fade (R6); a thunder delay.
8. The user's play test is the acceptance of every order.


## 5. Risks

| # | risk | severity | mitigation |
|---|---|---|---|
| K1 | HD + 60 Hz does not hold. F9 already shows 39 late frames in 418 s at 30 Hz with HD on; the render work doubles | high | 60 Hz is an option, off by default with HD on. Order 1 ships the measurement before any work is done on top of it. Optionally auto-fall-back: if `late` exceeds a threshold over N seconds, drop to `fps_mode 0` and say so in the log. |
| K2 | Camera cuts. Fixed cameras, spot cams, the cutscene camera and the binoculars all jump on purpose | medium | 1.3 snap rules; `GLOBAL_playing_cutseq` snapped entirely. Cheap to test: every level's first cutscene. |
| K3 | The first-person head filter (#134/#126). `fpVel`, `fpSpd`, `fpRel`, `fpSmoothValid` (`game\camera.cpp:1496-2145`) are tuned as a per-logic-tick exponential filter, and `fpSmoothSlow[]` (`:99`) is a per-tick constant | medium | Do not move the filter. `FirstPersonCamera` stays inside `CalculateCamera`, at 30 Hz. Only the output (`camera.pos`, `camera.target`, `fpRoll`) is interpolated. `fpRoll` needs a short-angle lerp and a getter. The existing `FPHEAD` log line (`game\camera.cpp:2127-2134`) proves the filter still sees one sample per tick. |
| K4 | The modal loops (R13) run their own present loops | medium | `gPresentMinTicks` defaults to 2 and only `DrawPhaseGame` raises the rate. They keep 30 Hz. |
| K5 | R6 mutates game state from the draw path (`game\draw.cpp:935-939`) | medium | Move it into `ControlPhase`. A latent bug today too - the `!(wibble & 3)` guard has been dead since `wibble` was masked `& 0xFC`. Its own note in `MODIFICATIONS.md`. |
| K6 | Pose blending across an animation change | medium | Order 4 blends only the root when `anim_number` changed. |
| K7 | The poison shake filter runs inside the view matrix (`specific\3dmath.cpp:650-656`) | low-medium | Step it only on a logic tick. |
| K8 | Portal room mismatch. The eye is interpolated, the room list is not | low | Live room number, interpolated eye. Under 33 ms of camera travel. If a picture ever shows it, re-run `GetFloor` on the interpolated position. |
| K9 | Latency. +8.3 ms average, +16.7 ms worst | low | Inherent and standard. `fps_mode 0` for anyone who wants the original response. |
| K10 | Uncapped mode needs a new time base | low | Kept out of orders 1-4 on purpose. |
| K11 | Regression in the 30 Hz path | low | `fps_mode 0` must take every new branch out: `alpha == 1`, snapshot not applied, `gPresentMinTicks == 2`. Proof 6 is byte-identical frames. |
| K12 | Particles keep stepping at 30 while everything else runs at 60 | low, cosmetic | Order 5, on the user's judgement. |


## 6. Decisions for the user

Before order 1

1. Is 60 Hz an option or the default? Recommendation: an option,
   default off, because of K1.
2. What shape does the option take? Recommendation: `fps_mode` as a
   `ulong` - `0 = 30 (classic)`, `1 = 60`, reserving `2 = display
   refresh` for order 6. Registry + one row on the tomb5 options page,
   like `first_person_smooth`.
3. Should there be an automatic fall-back when `FPSLOG` sees sustained
   `late` frames? Recommendation: yes, and loud - one log line and a
   return to 30.

Before order 3

4. Fade lengths: measured-identical, or free to be smoother?
   Recommendation: identical. A death fade half as long is a behaviour
   change, not a frame-rate change.

Before order 4

5. Is the pose worth its 350 lines, or is root-and-camera enough? Only
   a picture answers this. Recommendation: ship orders 1-3, look, then
   decide.

Before order 5

6. Rain, snow and sparks: interpolate or leave at 30? Recommendation:
   leave, and revisit only if the user's own eye finds them.

Before order 6

7. VSync. Recommendation: offer `D3DPRESENT_INTERVAL_ONE` as its own
   switch. It removes the `while (!Sync());` spin and its CPU burn, but
   ties the frame rate to the desktop refresh.
8. Uncapped or display refresh at all? Recommendation: not now.


## Summary

The engine's clock is already 60 Hz and its logic accumulator is
already built to take 1/60 s units and spend two of them per logic
step. One constant - the `< 2` in `S_DumpScreen` - is the entire 30 Hz
cap. A second, fully working, HD-trusted matrix stack for blending two
poses already exists, and the HD seam takes its matrices off the
engine's own stack, so interpolating before the stack makes HD follow
with no HD changes at all. TROne has no 60 FPS and nothing to carry
over. What the work really consists of is not the interpolation but the
fifteen counters that step per drawn frame instead of per logic tick -
with `Fade`, `DoScreenFade` and the `after_death` counter in
`PrintObjects` as the three that matter. Way B, a 60 Hz simulation,
would break every savegame and every authored animation and trigger
timing across 101,344 lines and is rejected. Recommendation: way A in
six orders, ~1,450 new lines, shippable after three, as a user option
that is off by default until `FPSLOG` says the machine can hold it.
