# The tools, and which one answers which question

Written 2026-08-30 from the folder itself, four tools added 2026-09-03 and one
more 2026-09-05. Thirty-nine diagnostic tools sit in `Diagnose\`, thirty of
them with a build script beside them, and until now there was no index at all -
so the way to find the right one was to already know it existed.

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
| `MapProbe` | bmap | the `LEVELn.MAP` — which `.TRM` model the level names for which object slot. |
| `DdsProbe` | bdp | decodes one BC7 `.DDS` with the engine's own decoder and writes a `.PNG`, so what the tool shows is what the game will show. |
| `RoomProbe` | broom | **draws one room** with the engine's own HD renderer into a `.PNG`, from a camera you give it, without starting the game. Also prints the depth proof (classic `sz` against the projection matrix over 50 distances), reads back what the device really compares depth with, checks the state bracket over 58 device states, measures the page cache in both texture pools, and with `-matrixcheck` prints the world-matrix proof: the engine's own three transform lines against the matrix that goes to the card, over up to 128 of the room's vertices, worst difference in **pixels**. Built **x86**, unlike its neighbours: the number it exists to measure is the peak commit of a 32-bit process.<br><br>The stage S2b switches make up the engine state the tool has no engine for and hand it to the very structures `hd_level.cpp` fills in the game: `-light <x y z r g b falloff>` a torch (room-local, repeat for up to 8), `-bulb <x y z rad den [r g b]>` a fog bulb (room-local; the tool transforms it into view space as `aBuildFogBulbList` does, up to 6), `-fog <start end r g b>` the distance fog (only `start` reaches the picture - TR5's fog has no end and no colour, and the tool says so rather than pretending), `-underwater`, `-shimmer`, `-effect <0..21>` the WaterTable row, `-wibble <n>` the engine's own counter - two runs with two values are two moments in the water. `-paritycheck` runs the classic formula and the shader formula over up to 200 of the room's own vertices on the processor and prints the worst difference per channel; the budget is 2 of 255.<br><br>The stage S5 switches say what MOVES: `-time <seconds>` is the game clock the UV scroll and the wind ride on, and it also sets the wibble counter to the value the engine would have at that second, so two `-time` runs are two moments of the WHOLE picture and not of half of it; `-water` declares the drawn room a water surface (the same whole-room override `-shimmer` uses); `-nomotion` is the engine's `hd_motion = 0`. `-animtex` prints, room by room, what the `.TRG` itself says moves there: scrolling draw calls with their speed in page units per second (material 1), vertices carrying a wind weight and the largest of them (`TrgVertex::sway`), and the flipbook parts (material 13) that are counted and NOT drawn - so the size of that gap is a number per level. |
| `ModelProbe` | bmodel | **draws one object model** with the engine's own HD object path into a `.PNG`, and prints how its nodes line up with the classic mesh tree. `-name <NAME>` takes a `.TRM` by name with no level behind it (bind pose; `4\ITEM` and `6\ITEM` are searched too, which is where `SAS` and five others live), `-slot <n>` takes the model a level binds to that slot, `-all` walks every slot of the level as a table — with the two skeleton columns the engine routes on, `maxb` (the highest mesh-tree node the model names) and `drift` (how far the model's node-to-node distances are from the slot's own), and the same verdict the game takes: a slot marked `SKELETON` draws classic in the game too. The level comes from `-level <STEM>` (the `.MAP` beside the remaster's `DATA`) and `-classic <path.trc>` (the classic level the mesh trees are read from — point it at a randomized level and it says what the randomizer's slots would draw).<br><br>`-pose <node> <yaw> <pitch>` turns one node of the mesh tree **and everything below it**: that is the skinning proof, because a model that comes back with a turned head and a body that stayed put has been skinned, one that turns as a lump has not, and one that tears at the neck has the wrong bind pose. Repeat the switch for more nodes. `-nodes` prints the node table (TRM vertices per node beside the classic bind offsets, and which nodes have no geometry at all), `-head` draws a separate `<NAME>_HEAD.TRM` as well, `-meshbits <mask>` hides nodes exactly as `item->mesh_bits` does in the game (`-meshbits 0xBFFF` takes a SWAT's head off), `-itemcheck` prints the matrix proof as a table — every node's origin put on the screen once by the classic transform and once by the matrix the card is handed, which is the check the engine itself logs as `HDLOG itemcheck` on the first HD object of every level, run here without the game —, `-light <x y z r g b range>` puts a point light in view space, and `-yaw`/`-pitch`/`-dist`/`-fov`/`-size` move the camera, which otherwise frames the model's own bounding box.<br><br>**`-lara <LEVEL>` draws Lara's whole composite** (stage S4-1) against that level's own Lara mesh tree: the outfit, the face, the braid, the hands, the thigh holsters and the weapon on the back, in bind pose. The level's own outfit is worked out the way the engine works it out - the header's `LaraType` byte plus the braid test that tells the Russia cap from the Rome body - and printed with its reason. `-outfit <NAME>` wears another one (`OUTFIT_VCI`, the bare `VCI`, or the randomizer's own guest id `tr3_london` / `tr3_jungle` / `tr3_antarctica` / `tr3_nevada`), `-guns <what>` puts pistols, uzi, shotgun, hk, revolver, revolver_laser, grapple, grapple_laser or crowbar in her hands and empties the holsters as the game does, `-holsters full|empty` sets the thighs, `-flare` and `-torch` fill the left hand alone, `-back <what>` hangs a weapon on her back, `-lara-nodes` prints every node 0..14 with its bind offset, its tree flags and the `.TRM` that covers it from each source, `-lara-pose` is `-pose` under its own name, and `-mirror` draws through a view matrix of **negative determinant** - the mirror-room case, which the HD path survives because its state bracket culls nothing. `-firstperson` draws her the way the seam draws her while the first-person camera is on (#126): the head node masked off the body, no `LARA_HEAD`, no braid. `-laracheck` prints the **two proofs of the Lara seam** as tables, the offline twins of what the engine logs as `HDLOG laracheck` and `HDLOG braidcheck` on its first HD Lara frame - every joint's origin put on the screen twice (the classic transform against the matrix the card is handed), and the braid's chain rule, where a link's own frame stepped one bone along has to land on the next link, which is how `HairControl` derives those positions in the first place. Both need the camera, so `-laracheck` does not combine with `-nodraw`. Built **x86** for the same reason `RoomProbe` is. |

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
it as `.inc` byte arrays. The engine's project has **no** shader compile step,
so this has to be run by hand after every shader change, before `be.bat`. Both
the `.hlsl` and the generated `.inc` are checked in — GPLv3 wants the source
that produced the bytecode to sit beside it.

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

`SLOTLOG` also prints `MAX_LOT`, which is the number that decides how many
enemies can be active at once - see `Tomb5 Limits\Limits.txt`.
