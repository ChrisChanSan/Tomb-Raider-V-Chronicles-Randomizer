# The tools, and which one answers which question

Written 2026-08-30 from the folder itself, four tools added 2026-09-03, two
more 2026-09-05, `SaveDiff` and `InterpTest` on 2026-09-07. Forty-two
diagnostic tools sit in `Diagnose\`, thirty-three of them with a build script
beside them, and until now there was no index at all - so the way to find
the right one was to already know it existed.

Every tool prints its own usage when started without arguments, and every
one carries a header comment saying why it was written. That header is the
authority; this page only points at it.

## Building

`Diagnose\<b*.bat>` - run it, it builds in place. Every script is relative to
its own folder, so a fresh clone builds them. They need Visual Studio with
the C++ workload; the path to `vcvars` is the one absolute thing in them.

Nine tools have no script and are built by hand from their header comment:
BoneCheck, CornerCheck, DinoCheck, FaceCheck, GeomTest, TR3FetchCheck,
TR3SoundCheck, TriggerCommands, WinCheck.

CornerCheck, GeomTest and WinCheck were written in a scratch folder outside
the clone, and their build scripts stayed behind on purpose: those carried
absolute paths, and two of them were even named `bcc.bat` and `bwc.bat`,
which belong to CameraCheck and BalanceWriteCheck here. All three take the
same command line as CameraCheck's script - `Tr5Level.cpp` plus zlib, nothing
else - with their own name substituted.

## By subject

**Levels, rooms, sectors**
| Tool | Script | What it answers |
|---|---|---|
| `ItemCheck` | bip | every item with OCB, flags, room and angle - the first thing to run on "why is this item behaving oddly". |
| `RoomCheck` | brp | room flags and flipmap partners. |
| `InfoCompare` | bic | compares what `Rando::levelInfoFromLevel` reads out of a `.trc` against the old `level_<name>.json`, field by field. Written to prove the 45 MB of exports could go; keep it for the next time someone doubts the level file is the better source. |
| `SectorCheck` | bsek | one sector in plain text, including the raw eight FLOOR_INFO bytes. |
| `FloorCheck` | bb | floor heights and slopes, the rule set behind item placement. |
| `FaceCheck` | - | which drawn faces cover a sector. |
| `CornerCheck` | - | proof for `cornerFloor` / `cornerCeiling` (#137): every horizontal vanilla face that covers one sector must sit corner for corner on the height the floordata computes. Run it after any change to the floordata reading - it is the only independent witness there is. |
| `WinCheck` | - | holds `wallWindow` (#137) against the 32-byte view windows Core shipped. A window only ever clips, so when the two differ the direction is what matters: too large costs nothing, too small blacks out part of the neighbouring room. |
| `GeomTest` | - | tries `buildRoomGeometry` (#137) on one room and reports what came out - face count, triangles, bounding box, degenerate faces. The tool for a room that has no drawn surface at all. |
| `LevelDiff` | bld | two levels compared block by block. If everything but the images is byte-equal, only textures were exchanged. |
| `LevelBuild` | bbw | applies a patch JSON to a level - how the hand-built test levels are made. |
| `FixCheck` | bfc | applies LevelFix and saves the result, so the repairs can be looked at in trview. |
| `TriggerCommands` | - | counts which trigger commands occur in a level. |
| `CameraCheck` | bcc | the fixed cameras with their flags. |
| `TexRec` | btx | texture pages and texinfos. |
| `HdmDump` | bhdm | the HD markers of stage S7 (#123): `data[3]` of the level header with its meaning, and the `.hdm` sidecar beside the file. Doubles as the reference reader for the sidecar grammar - the engine's own reader has to answer the same way on the same file. Run it on a finished level; a vanilla one says "no markers", which is what the engine reads too. |

**Objects, animations, meshes**
| Tool | Script | What it answers |
|---|---|---|
| `SlotList` | bsl | which moveable slots a level carries at all. |
| `SlotAnims` | bsa | an object slot's anim slice - the first stop when an import reaches past its own animations. |
| `AnimMap` | bam | all slots sorted by anim_index, with overlaps and gaps in the appended area. |
| `AnimCmd` | bak | a slot's anim commands, above all which sound hangs on which frame. |
| `AnimProbe` | bap | cross-check of `Tr5Level::laraAnimUsable`. |
| `LaraAnim` | bla | which of Lara's animations a level REALLY carries. Levels stub the ones they do not need, and a stub is not a missing entry - it looks present. |
| `BoneCheck` | - | a slot's bone tree, or two compared. |
| `ModelOffset` | bmm | where a model sits relative to the item origin, and with `-sound` the sound chain of a map id. |
| `HeightCheck` | bhc | headroom (#141): does every enemy in a finished level have its model's rest-pose height between floor and ceiling? Reads model and floordata out of the level under test and knows nothing about the placement code. `heights` prints the measured heights, `-v` every item. Calibrated: on the 15 vanilla levels it must report 0 violations. |

**Sound and music**
| Tool | Script | What it answers |
|---|---|---|
| `Tr5Sound` | btp | the whole chain: soundmap entry, SAMPLE_INFO, wave headers. The tool for "the wrong sound plays". |
| `MusicCheck` | bmp | which TO_CD tracks a level's triggers actually use. |
| `TR3Sound` / `TR3SoundCheck` | bton / - | the same for Tomb Raider III, for the imported enemies. |

**Script and balance**
| Tool | Script | What it answers |
|---|---|---|
| `ScriptCheck` | bsk | the object commands per level out of SCRIPT.DAT - names and inventory poses. |
| `ScriptProbe` | bsp | cross-check for the randomizer's own SCRIPT.DAT writer. |
| `BalanceCheck` | bbc | test rig for the engine's `balance.cpp`. The only 32-bit tool here, because it compiles against the engine. |
| `BalanceWriteCheck` | bwc | the randomizer's side of Balance.txt. |

**Savegames (#148)**
| Tool | Script | What it answers |
|---|---|---|
| `SaveDiff` | bsd | two savegames compared **field by field, by name**. Prints `IDENTICAL` when the files match to the byte, otherwise one line per differing field with both values - so "played four seconds longer" (`Game.Timer`, the clock, Lara's position) is told apart at a glance from "the game ran differently" (`Level.Kills`, the ammunition counts, `Lara.air`). Written for the frame-rate proof: the draw path has its own RNG that the simulation never reads, so the same seed and the same inputs must arrive at the same state at 30 and at 60 frames a second, and a savegame is that state written down. The recipe is in `docs/Engine-Manual.md` under *Frame rate*.<br><br>Takes every offset from the engine's own `global/types.h` with `offsetof`, so it cannot go stale - and is therefore built **x86**, like `BalanceCheck` and `RoomProbe`: the structs are `#pragma pack(1)` but carry pointers, and a pointer is four bytes in the engine. What it cannot name is the savegame *stream* inside `SAVEGAME_INFO::buffer` - that is what `WriteSG` (`game/savegame.cpp`) packed in a variable-length, conditional order - so those are reported as differing byte runs with their offset. |
| `InterpTest` | bit | the ARITHMETIC of the 60 FPS interpolation (#148, orders 2, 3, 4, 5 and 6), checked with no game, no level and no device. It **includes the engine's own `TOMB5 Code\TOMB5\game\interpolate_math.h`** rather than a copy of it, so it cannot drift from what the engine does: a change to the lerps or the snap thresholds is a change this test sees. 198 checks - the short-angle lerp over a sweep of 16,256 angle pairs (that every one turns the SHORT way round and lands within a unit of halfway, with the 128 exact half-turns stepped over and asserted on their own), the position lerp over the whole coordinate range of a level, the snap thresholds against the engine's own speeds (a terminal fall interpolates, a sector-wide teleport does not), that a Manhattan sum is never smaller than the true distance, and the cadence: `framecount` and `DrawPhaseGame`'s alpha rule run for a simulated minute at both frame rates and through a hitch - 1800 logic ticks either way, exactly half the frames interpolated at sixty, none at thirty, every drawn step exactly half a tick.<br><br>It exists because a short angle blended the LONG way round is invisible almost all the time and looks like a rendering glitch when it is not: one enemy, facing roughly south, spinning backwards for half a tick on every second frame. That has to be proven at the wrap, and a play test will not put a body on the wrap when you want it to. Order 3 added a `counters` section: a simulated wall-clock second at both rates with the engine's own tween-frame rule, asserting that a guarded counter steps thirty times in BOTH modes (not sixty), that the nine-step screen fade spans the same nine ticks - 2n-1 presents at sixty, because it begins and ends on a live frame - and that at `fps_mode 0` not one frame is ever an in-between frame. It also states the rule's one honest limit: a frame that ran long carries TWO logic ticks and can only step a counter once, so a counter falls one behind per doubled frame and does not catch up - which is what the thirty hertz engine does too. Order 4 added `pose_factor` and `pose_time`: the four hard-wired blend branches of `game\draw.cpp` transcribed as the REFERENCE and compared against the header's one float factor over 441 matrix element pairs for every `(frac, rate)` the retail levels can produce - 336,471 animation frames' worth, then every pair up to rate 32 - with a **maximum difference of 0 units on both the float and the integer stack**, which is what makes `fps_mode 0` still byte-identical; and the pose walked over a whole animation at sixty hertz, printed as a table (rate 4 comes out as even eighths, rate 2 as even quarters, rate 1 - which never interpolated at all in vanilla - as exact halves, rate 5 as vanilla's own uneven steps with a midpoint between each pair), asserting that it never walks backwards, never leaves its key frame pair, is vanilla's own value on every tick frame, follows an animation's shortened last interval, and is refused by each of the six K6 cases. Order 6 added `refresh`, for the frame rate bound to the display: it models the engine's own loop - draw, flip at the panel's vblank, read the clock AFTER the flip, hand the whole ticks to `ControlPhase` - with the vblanks on their own grid at their own phase, so the two clocks are as independent as a 144 Hz panel and a 1/60 s counter really are. Ten simulated seconds at 60, 120 and 144 Hz each run **300 logic steps** against 600, 1200 and 1440 frames; the blend factor is **never clamped** (a clamp means a frame asked for a state the logic has not computed, and the picture stands still for it); no frame draws what the one before it drew; exactly one frame per logic tick carries the per-frame counters, which at these rates can no longer be `alpha < 1`; and what is drawn tracks the wall clock with a spread under 1e-7 ticks, which is a float's precision and not drift. A 200 ms hitch is caught up rather than dropped, less the two ticks vanilla's own `if (nframes > 10)` cap discards. Order 5 added `particles`: a falling particle under gravity, simulated tick by tick and drawn beside an ITEM whose previous state really is remembered, at sixty hertz and at 144 - the two must agree **to the unit on every single frame**, and the particle must never be drawn past the state the game holds, never before the state it came from and never a step backwards. The identity is also swept over every step a TR5 particle can carry, in both signs, against the obvious rearrangement `cur - step * (1 - alpha)`, which disagrees on 16,384 of the 40,965 pairs - that is the rounding trap the engine's form avoids, and it is printed rather than described. Prints `234 checks, 0 failed` and returns 0. |

**The Tomb Raider III import**
| Tool | Script | What it answers |
|---|---|---|
| `TR3Peek` | bt | look into a TR3 level. |
| `TR3Fetch` / `TR3FetchCheck` | bh / - | pull a model out and package it; the check compares the library against the tool byte for byte. |
| `TR3Insert` | bs | insert a package into a TR5 level. |
| `Tr3Probe` | bpr | cross-check of the conversion. |
| `DinoCheck` | - | do all four guests still come out identical to their reference packages? |

**The HD art of the Remaster (#123)**

These read the user's own *Tomb Raider IV-VI Remastered* installation where it
lies. Nothing of it is copied into this tree, and none of them touch
`tomb456.exe` or `tomb5.dll`.

| Tool | Script | What it answers |
|---|---|---|
| `TrgProbe` | btrg | what is in a `LEVELn.TRG`: groups, parts, passes, pages, vertices, indices, and how many draw ranges a level would cost. |
| `MapProbe` | bmap | the `LEVELn.MAP` — which `.TRM` model the level names for which object slot. `-hdm` reads the `.hdm` sidecar of a randomized level and prints the engine's own decision per line (a room classic with `reason=marker`, a donor slot resolved through the DONOR level's map, the `data[3]` outfit and the `.TRM` it maps to); `-tr3 <…>` points it at a *Tomb Raider I-III Remastered* game folder so a Tomb Raider III guest's model can be looked for. |
| `DdsProbe` | bdp | decodes one BC7 `.DDS` with the engine's own decoder and writes a `.PNG`, so what the tool shows is what the game will show. |
| `RoomProbe` | broom | **draws one room** with the engine's own HD renderer into a `.PNG`, from a camera you give it, without starting the game. Also prints the depth proof (classic `sz` against the projection matrix over 50 distances), reads back what the device really compares depth with, checks the state bracket over 58 device states, measures the page cache in both texture pools, and with `-matrixcheck` prints the world-matrix proof: the engine's own three transform lines against the matrix that goes to the card, over up to 128 of the room's vertices, worst difference in **pixels**. Built **x86**, unlike its neighbours: the number it exists to measure is the peak commit of a 32-bit process.<br><br>The stage S2b switches make up the engine state the tool has no engine for and hand it to the very structures `hd_level.cpp` fills in the game: `-light <x y z r g b falloff>` a torch (room-local, repeat for up to 8), `-bulb <x y z rad den [r g b]>` a fog bulb (room-local; the tool transforms it into view space as `aBuildFogBulbList` does, up to 6), `-fog <start end r g b>` the distance fog (only `start` reaches the picture - TR5's fog has no end and no colour, and the tool says so rather than pretending), `-underwater`, `-shimmer`, `-effect <0..21>` the WaterTable row, `-wibble <n>` the engine's own counter - two runs with two values are two moments in the water. `-paritycheck` runs the classic formula and the shader formula over up to 200 of the room's own vertices on the processor and prints the worst difference per channel; the budget is 2 of 255.<br><br>The stage S5 switches say what MOVES: `-time <seconds>` is the game clock the UV scroll and the wind ride on, and it also sets the wibble counter to the value the engine would have at that second, so two `-time` runs are two moments of the WHOLE picture and not of half of it; `-water` declares the drawn room a water surface (the same whole-room override `-shimmer` uses); `-nomotion` is the engine's `hd_motion = 0`. `-animtex` prints, room by room, what the `.TRG` itself says moves there: scrolling draw calls with their speed in page units per second (material 1), vertices carrying a wind weight and the largest of them (`TrgVertex::sway`), and the flipbook parts (material 13) that are counted and NOT drawn - so the size of that gap is a number per level.<br><br>The stage S6 switches are the small card, which no machine here has: `-budget <MiB>` forces a texture budget (over it the largest pages give up their largest mip level, biggest first, never a whole page), `-format rgba|dxt5` holds the pages compressed instead of lossless and `-psnr` says in decibels what that costs, `-caps maxtex <n>` and `-caps noidx32` bend the caps the renderer read from the device so the fallbacks can be seen at all, `-vramhog` fills the card with 64 MiB blocks until it refuses so the out-of-video-memory path runs, and `-seq <A,B,C>` builds those levels one after the other printing commit and free texture memory between each - the proof that a level change never holds two levels at once. |
| `ModelProbe` | bmodel | **draws one object model** with the engine's own HD object path into a `.PNG`, and prints how its nodes line up with the classic mesh tree. `-name <NAME>` takes a `.TRM` by name with no level behind it (bind pose; `4\ITEM` and `6\ITEM` are searched too, which is where `SAS` and five others live), `-slot <n>` takes the model a level binds to that slot, `-all` walks every slot of the level as a table — with the two skeleton columns the engine routes on, `maxb` (the highest mesh-tree node the model names) and `drift` (how far the model's node-to-node distances are from the slot's own), and the same verdict the game takes: a slot marked `SKELETON` draws classic in the game too. The level comes from `-level <STEM>` (the `.MAP` beside the remaster's `DATA`) and `-classic <path.trc>` (the classic level the mesh trees are read from — point it at a randomized level and it says what the randomizer's slots would draw).<br><br>`-pose <node> <yaw> <pitch>` turns one node of the mesh tree **and everything below it**: that is the skinning proof, because a model that comes back with a turned head and a body that stayed put has been skinned, one that turns as a lump has not, and one that tears at the neck has the wrong bind pose. Repeat the switch for more nodes. `-nodes` prints the node table (TRM vertices per node beside the classic bind offsets, and which nodes have no geometry at all), `-head` draws a separate `<NAME>_HEAD.TRM` as well, `-meshbits <mask>` hides nodes exactly as `item->mesh_bits` does in the game (`-meshbits 0xBFFF` takes a SWAT's head off), `-itemcheck` prints the matrix proof as a table — every node's origin put on the screen once by the classic transform and once by the matrix the card is handed, which is the check the engine itself logs as `HDLOG itemcheck` on the first HD object of every level, run here without the game —, `-light <x y z r g b range>` puts a point light in view space, and `-yaw`/`-pitch`/`-dist`/`-fov`/`-size` move the camera, which otherwise frames the model's own bounding box.<br><br>**`-lara <LEVEL>` draws Lara's whole composite** (stage S4-1) against that level's own Lara mesh tree: the outfit, the face, the braid, the hands, the thigh holsters and the weapon on the back, in bind pose. The level's own outfit is worked out the way the engine works it out - the header's `LaraType` byte plus the braid test that tells the Russia cap from the Rome body - and printed with its reason. `-outfit <NAME>` wears another one (`OUTFIT_VCI`, the bare `VCI`, or the randomizer's own guest id `tr3_london` / `tr3_jungle` / `tr3_antarctica` / `tr3_nevada`), `-guns <what>` puts pistols, uzi, shotgun, hk, revolver, revolver_laser, grapple, grapple_laser or crowbar in her hands and empties the holsters as the game does, `-holsters full|empty` sets the thighs, `-flare` and `-torch` fill the left hand alone, `-back <what>` hangs a weapon on her back, `-lara-nodes` prints every node 0..14 with its bind offset, its tree flags and the `.TRM` that covers it from each source, `-lara-pose` is `-pose` under its own name, and `-mirror` draws through a view matrix of **negative determinant** - the mirror-room case, which the HD path survives because its state bracket culls nothing. `-firstperson` draws her the way the seam draws her while the first-person camera is on (#126): the head node masked off the body, no `LARA_HEAD`, no braid, and since fix round K the **inner skin** that closes the hole the neck cut opens - the body drawn a second time keeping the faces the first draw culls, in her own throat's colour; `-noinner` leaves it out, which is the BEFORE picture (a hole one sees the room through), and `-neckcut <S> <R>` overrides the cut's own cylinder - S units down the neck axis from the head joint and R off it, `0 0` for the picture with no cut at all (#123, fix rounds J and K). `-fpforward <units>` puts the eye where the game puts it: 48 in front of the head joint when the sight line is clear, 0 with a wall in the way, which is the case that shows the collar. `-necknear <units>` sets the **near zone** about the eye (#123, fix round L) - a sphere around the camera in which nothing of her body is drawn, on the same nodes the cut may touch - and `-cutnodes <mask>` says which nodes those are: the engine names the torso AND the head, `-cutnodes 0x80` is the torso alone and therefore round K's picture, and pointing it at one node at a time is how a surface standing in the camera is traced to the node it hangs off. `-onlypart <n>` draws ONE part of the composite and nothing else, which is how a question about one part ("does the sheen reach the hands?") is answered by a picture of that part instead of a hunt through a picture of the whole of her. `-neck` prints the neck measurements the cut and the inner skin rest on: the stump against both eyes, the neck as a TUBE, the neck as a CYLINDER - an (s, r) grid of the torso's own vertices per outfit, the ring at four candidate depths, and a scan over nine candidate (S, R) pairs saying how much of the stump each takes, how much **collar** it leaves standing, whether it reaches an arm and what it leaves nearest the eye -, the neck in **two-unit bins** with a column counting how many survivors carry head weight, **what a candidate cut leaves** - the only one of these tables that models the shader exactly, with the weighted node push and the range mask, which is how fix round L found that a collar skinned half to the head was never inside the reach of any cut - and the stump **by page**, which is where the inner skin's one texel is chosen from. `-handlight` prints the **wrist seam** (#123, fix round K): the body's texture pages on the arm and hand nodes beside the `HAND_` model's, what each shines like, and the seam as one number - the grey levels the highlight adds at N.V = 1 in a room of 255, which is the quantity that made the play test read a material difference as a light one. `-oldskin` puts the outfit's own material back on Lara's skin pages, which is that picture before the round, and `-look wrist` frames the joint it is about. `-laracheck` prints the **two proofs of the Lara seam** as tables, the offline twins of what the engine logs as `HDLOG laracheck` and `HDLOG braidcheck` on its first HD Lara frame - every joint's origin put on the screen twice (the classic transform against the matrix the card is handed), and the braid's chain rule, where a link's own frame stepped one bone along has to land on the next link, which is how `HairControl` derives those positions in the first place. Both need the camera, so `-laracheck` does not combine with `-nodraw`. Built **x86** for the same reason `RoomProbe` is.<br><br>**`-braidsim [steps]` runs the braid's own physics offline** (#123, fix round F): `HairControl`'s chain loop transcribed line for line - integer positions, the game's integer push-out, the same rebuild out of the two angles - and driven with this level's Lara skeleton, her six collision spheres and the two HD backpack spheres, in the bind pose, with no wind, no water line and no floor (a braid that will not settle standing still in a dry indoor room will not settle anywhere). One line per regime - the classic braid, what fix round D shipped, what fix round D meant, and what ships now - saying how far the busiest link still travels per step after 1, 10, 30 and `steps` steps, how deep a link still stands inside the backpack, and how many links the pack pushed. A settled braid reads zero and stays there; the regimes that read anything else are the ones the game had. Both torso anchors are run, the ordinary one and the Russia/bunhead one. Default 60 steps, a second of game time. Since fix round I the pack is a **box** and the "in a pack" column is measured against it whatever the regime, so the old sphere regimes say how far into the backpack the braid used to walk. `-classicroot` draws the braid from the classic anchor, which is the picture before fix round I's I2; **`-braidrest [regime]` draws the braid where the SIMULATION leaves it** rather than in a studio pose, which is the only way a still picture can show whether the plait lies on the pack or in it (no number: the regime the engine ships).<br><br>**`-braidanim [<n>]` runs the same chain under a REAL animation** (#123, fix round G): the bind pose has no anchor motion at all, so this reads the level's own animation table, decodes the frames Lara really wears and rebuilds all eight collision spheres and the braid root from them once per game frame - the same node walk, the same skips through the packed rotation stream, the same frame blending, the positions truncated to longs afterwards, and the floor clamp in (a braid 336 units long hanging from a head 251 units up reaches through the ground she stands on; `-nobraidfloor` takes it out). One row per physics variant - the classic body alone, the pack as fix round F ships it, and three ways of softening it - counting the TIP's travel per frame in the same four buckets a play test's `HAIRLOG` is counted in, plus the shortest the chain ever got and the worst one-frame change of a link's YAW. Then a second table: the DRAWN bone's twist under five roll rules, which is how fix round G found that the braid stands still to within a unit and spins half a turn on one frame in eight. Repeat the switch for more animations; with no number it runs 103 (breathing on the spot), 0 (the run loop) and 1 (the walk loop). The step count comes from `-braidsim`, or 120 without it.<br><br>`-withrooms <LEVEL>` loads that level's ROOM pages into the cache first (stage S6), so "pages added" is the marginal cost of the models rather than their cost in isolation - which is what the engine really pays, because `HDLevelBuild` appends the models to the cache the rooms filled. |

One thing the two picture tools cannot make up: they have no classic level, so
`RoomProbe` cannot read a room's flags or its roomlets. `-underwater` and `-shimmer` are therefore
switches rather than answers, and `-shimmer` - and `-water` with it - lights or
wobbles the WHOLE room (the selector is aimed at the vertex's constant fourth
flag byte) where the engine touches only the vertices its own predicate marked.
That difference is large: over the vanilla levels the classic water set is 0 to
188 vertices per level, so a `-water` picture over-reports on purpose. The
picture proves the arithmetic and the animation; which vertices get it is the
engine's business and the play test's. The stage S5 scroll and wind are NOT in
that class - they are marked in the `.TRG` itself, so what the tool draws there
is what the engine draws.

`bshader` is not a tool but a build step of its own: it runs `fxc` from the
Windows SDK over `specific\hd\hd_shader\*.hlsl` and writes the bytecode next to
it as `.inc` byte arrays. The engine's project has **no** shader compile step.
Both the `.hlsl` and the generated `.inc` are checked in — GPLv3 wants the
source that produced the bytecode to sit beside it.

**`be.bat` now checks that for you** (#123, fix round G). Every `.inc` carries
the MD5 of the `.hlsl` it came from on its last line (`bshader.bat`, `:stamp`)
and `be.bat` compares the two before it builds; where they differ it prints
`be: regenerating shaders - ... out of step with the HLSL`, runs `bshader.bat`
itself and only then builds, and it refuses to build at all if that fails. A
**content** stamp and not a file date, because a fresh clone gives every file
the same date and a date guard would then either cry wolf or say nothing at
all. It exists because round R3b changed `hd_room.hlsl`, never ran `bshader`,
and shipped a build whose source and whose picture disagreed - nothing in the
tree could say so, and it took a play test and two fix rounds to find. Running
`bshader.bat` by hand still works and is still the right thing to do while
iterating on a shader; the guard is the net underneath.

## The other half: the engine tells you things too

The engine writes nothing unless it is started with `-log`. `Log.bat` in the
release package does exactly that, which is why it is in there. The seams
below are permanent - they are not debug leftovers to be tidied away, they
are how a player's crash report becomes usable.

| Symptom | Seam | What it prints |
|---|---|---|
| the wrong sound plays | `SFXLOG` | which sample really played, and which anim command asked for it |
| an enemy is invisible | `SLOTLOG`, `SKIPLOG`, `MESHLOG`, `CLIPLOG` | the four separate reasons: no free AI slot, control skipped it, no mesh, clipped away |
| ...and if it is the clipping | `CLIPDETAIL` | the actual window and the camera it was measured against |
| an item is in the wrong place, or vanishes | `ITEMLOG` | slot, room, OCB, flags per item at load |
| ...and something moves it later | `ITEMWATCH` | who wrote which room number, and when |
| the outfit looks wrong, a limb is misplaced | `SKINLOG` | the skin's bone stack |
| the first-person view sits wrong | `FPHEAD`, `ALTERFOV` | the head bone per frame, and the field of view |
| an HD object or Lara is wrong, or classic when it should not be | `HDLOG` | per level: which slot got which model and why it was refused; Lara's outfit, her parts and the reason for a classic one. Per level's first HD frame: `itemcheck` and `lightcheck` for the objects, `laracheck` and `braidcheck` for Lara |
| it stuttered, or dropped under the frame rate | `FPSLOG` | one line per second: frames presented, the longest gap, and `logic_ticks=` - what the SIMULATION ran, which must stay 30 whatever the frame rate is (#148) |
| ...and *why* it stuttered | `PROFLOG` | the same second split into the fifteen phases of a frame (logic, room walk, HD rooms, HD objects, buckets, sort, the wait for the slot, `Present`), mean and worst; plus one line for **every** frame that missed its slot with all its phases, and one `first_frame` line per level (#148) |
| at *Frame rate 60* the motion is no smoother than at 30, or something jerks | `INTERPLOG` | one line per LOGIC second: how many frames were interpolated, how far Lara moved per drawn picture (must HALVE against a 30 Hz run over the same walk), how many things really moved, and every deliberate snap by cause - teleport, room jump, camera cut, cut scene (#148, order 2) |
| the first seconds of a level are rough | `WARMUP` | one line per load: how many texture pages and buffers were made resident before the first frame instead of inside it (#148) |

`SLOTLOG` also prints `MAX_LOT`, which is the number that decides how many
enemies can be active at once - see `Tomb5 Limits\Limits.txt`.
