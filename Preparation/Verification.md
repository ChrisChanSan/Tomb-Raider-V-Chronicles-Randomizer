# How to check things in this project

A regression test must prove that a change has not altered what the
randomizer produces, down to a single byte of the game.

Each recipe below is here because the obvious way of checking gave the wrong
answer at least once. The wrong answers are written down too - a method is
only trustworthy if you know how it fails.

---

## 1. Did a change alter what the randomizer produces?

The only answer that counts is a byte comparison against a build of the
commit before the change.
Reading the diff is not enough; the point of the check is the things you did
not think of.

```
git worktree add --detach <tmp> <commit-before>
# build the old randomizer there, build the new one as usual
# run BOTH with the same seed, the same switches and the same inputs
cmp old.trc new.trc
# across the change of the texture packing (#306): the content
LevelDiff -content old.trc new.trc
```

**What this caught:** nothing, and that was the result - four levels came out
byte-identical across an evening of renames. Without it, "it only touched
comments and names" would have been a belief.

**Two traps in the recipe itself:**

- **The old commit may not build.** A build script like `b1.bat` can carry
  an absolute path into a folder that does not exist under that name any
  more. Copy the CURRENT build script into the old worktree; the sources
  are what you are testing, not the script.
- **Your own shell holds the folder.** `git worktree remove` and folder
  renames fail with "in use by another process" when a shell sits inside.
  Leave the folder first.
- **The data folder.** Since #306 the CLI reads its zone export only from
  the path it is given and the checksum list only from `data\` beside the
  EXE; neither EXE carries a copy. Copy each build in turn to the SAME
  folder beside the same `data\` before its run: the log names the paths it
  read, and two EXEs in two folders give logs that differ in that line.

**The texture packing changed with #306.** A build before it packs the
texture blocks a run grows (every level that takes a page from another
level) with zlib's compressor; a build from #306 on writes them as stored
deflate blocks. Every pixel is the same, the file carries about twice the
bytes, and `cmp` reports the whole rest of the file. Across that change the
recipe compares with `LevelDiff -content old.trc new.trc` (`bld.bat`): it
unpacks the three texture blocks of both files with the randomizer's own
reader and compares their content, and it compares every other byte - the
ten header bytes in front and everything from `data[16]` to the end of the
file - as it stands. `SAME CONTENT` (exit code 0) is the pass; anything
else is a regression. Between two builds that both carry #306, `cmp` holds
again: a stored block depends on nothing but its raw bytes. Measured over
the thirteen campaign levels at seed 380 plain and with `mix cross
allsanity 8 weapons items20 weather`, seed 1 with `mix cross 8 weapons` and
seed 77 with `mix cross allsanity 8 items20`: every `.hdm` and log
identical, every `.trc` identical or `SAME CONTENT`.

**Exercise the risky path on purpose.** A run that never touches the changed
code proves nothing. When the change was to the TR3 cache keys, the test had
to place a TR3 guest - and the proof was that the new build re-extracted from
Tomb Raider III and produced files byte-identical to the old cache.

**One difference is allowed: the HD outfit marker.**
Every randomized level carries the HD outfit marker in `data[3]` of its
header block, and a sidecar `<level>.hdm` beside it. The check:

- With no Tomb Raider III outfit in the run, every `.trc` must still be
  **byte-identical**. That was measured over all thirteen levels of seed 380
  with `mix cross allsanity 8 weapons`, once plain and once with the glitched
  secret list - the second one because the marker's recording sits inside
  `makePortal`, `clearWallFaces` and `moveRoom`, and a run that never opens a
  portal would not have touched the changed code at all.
- With a guest outfit rolled, **exactly one byte** per level may differ, and
  it is `data[3]`: `cmp -l` shows a single line with a value of 5, 6, 7 or 8
  (octal `10`). Anything else is a regression. Seven of thirteen levels came
  out identical in that run because their roll landed on a vanilla outfit.
- The `.hdm` files are part of the reproducibility: the same seed twice must
  give the same sidecar byte for byte, not only the same level.

**The `.hdm` format: shape that must not touch the `.trc`.**
An OPENING (`Make_Portal`, `Make_Ceiling_Portal`,
`Make_Floor_Portal`, `@Clear`, a view window) writes a `cut <x0> <y0> <z0>
<x1> <y1> <z1>` box instead of two `room` lines, and the version line reads
`hdm 2` whenever such a box is in the file. Only `Move_Room` and
`Make_Room_Geometry` still write `room`. Checking a change to this format:
every `.trc` must still be **byte-identical** - measured over the eight
levels whose secret-list lines carry geometry edits at all (ANDREA3, ANDY1,
JOBY3, JOBY5, RICH1, RICH2, RICH3, ANDY3, seed 380, `mix cross allsanity 8
weapons`, glitched list) - and the `.hdm` must change. `HdmDump <level.trc>`
prints both kinds of line.

**The box's own numbers: an INSET, not an outset, without touching the
`.trc`.** The step of 128 in the plane of the opening is an INSET, so a
`cut` line using the outset convention has its two non-normal axes 256 units
wider than one using the inset convention. Checking a change between the
two: the eight `.trc` must be byte-identical (SHA-256 per file, plus ANDY3
with the glitchless list), and every `cut` line must move by exactly +128 on
its two smaller-span axes' lower corner and -128 on their upper. The span
along the normal, the only one that reads 1024, must not move at all. That
is a two-line check with `HdmDump` and it is the whole difference.

`HdmDump <level.trc>` prints `data[3]` with its meaning and the sidecar
beside it, and says so when the two disagree.

**More changes that are allowed to move the bytes:**

- **The underwater pickup animation.** The randomizer
  backfills Lara's anim 130 (`ANIM_PICKUP_UW`) into every level whose own
  copy of it is a stub, so items can be picked up under water everywhere.
  This one fires with NO switches at all: the smallest case is
  `Randomize <level> 380` on a level that lacks the animation - six of the
  thirteen did - and the run says so itself, `note: Lara anim 130
  backfilled from joby4`. The seven levels that carry the animation stay
  byte-identical.
- **The headroom rule (see 1b).** Enemy placement asks whether the
  model's rest-pose height fits under the ceiling, and the Hydra asks for
  five of its nine tiles; both change WHICH candidate a draw lands on, so
  the RNG's own order moves with it. Any run with `mix` or `cross` can
  differ.
- **Weather.** With the weather option on, a level that has a room
  which can show rain or snow gets a new draw - six of the thirteen can,
  and those are the ones to compare. The run names the result in its own
  notes.
- **A missing view window (LevelFix, `SightWindow`, #214).** Every run on
  ANDREA2 writes the window between rooms 231 and 102 over the arch, so
  ANDREA2 differs from a build without it on every seed and with no
  switches at all: 64 bytes longer, one 32-byte record at the end of each
  of the two rooms' portal lists, their two counts, five header offsets
  each moved by 32, the two rooms' size words and the main block's two
  size words. Every other room, every byte in front of the rooms and
  behind them, every other level and every `.hdm` stay byte-identical -
  the entry records no HD cut.
- **A trigger set to one-shot (LevelFix, `TriggerOneShot`, #223).** Every run
  on RICH2 sets the one-shot bit on the two floor-laser triggers of room
  5, so RICH2 differs from a build without it on every seed and with no
  switches at all: the same size, exactly two bytes, each the high byte
  of a setup word, `3E00 -> 3F00` inside the chain words `8004 3E00
  8022`. Measured at seed 380, plain and with `mix cross allsanity 8
  weapons`: every other `.trc` and every `.hdm` byte-identical, and the
  log one line longer (`LevelFix 1 fixes applied`). `FixCheck` on the
  vanilla RICH2 gives the same two words at their source offsets and
  `BYTES 2 of 4805655 differ`.
- **A trigger's timer byte (LevelFix, `TriggerTimer`, #244).** Every run
  on RICH2 sets the timer byte of the heavy trigger under the X-ray
  controller, room 12 (2,3), from 240 to 255, so RICH2 differs from a build
  without it on every seed and with no switches at all: the same size,
  exactly one byte, the low byte of a setup word, `3EF0 -> 3EFF` inside the
  chain words `8504 3EF0 800B`. Measured at seed 380, plain and with `mix
  cross allsanity 8 weapons`: every other `.trc` and every `.hdm`
  byte-identical, and the log line `LevelFix 2 fixes applied` for RICH2.
  `FixCheck` on the vanilla RICH2 gives the word at its source offset
  3,041,860 and `BYTES 3 of 4805655 differ`, the two one-shot bytes and this
  one.
- **The cyborg in the cross pool.** One more foreign kind to draw
  from, so a `cross` run places a different set. Byte-identity with a
  build lacking the cyborg is claimed when the two new rows are
  deselected - the case to run when checking that claim.
- **The brawler in the cross pool (#288).** One more foreign kind, so
  every cross run draws differently from the first draw he joins on; a
  plain run does not move. Byte-identity with the build before him is
  claimed for a build with only his pool row removed: measured over the
  thirteen campaign levels at seed 380 plain and with `mix cross allsanity
  8 weapons items20 weather`, seed 1 with `mix cross 8 weapons` and seed 77
  with `mix cross allsanity 8 items20`, all 156 `.trc`, `.hdm` and log
  files identical. Deselecting him (`enemiesoff=471`) takes him out
  before the roll and is the same case: the full build so, at seed 380
  with the cross switches above, gives the old build's 39 files.
- **Not the build id.** `RANDO_BUILD_ID` (`TR5Randomizer/src/core/
  BuildId.h`) moved from 19 to 20 because #214 and #223 change what a seed
  writes, and `RANDO_VERSION_STRING` became `Beta_3.0` with it. Neither
  enters the seed string or a level file: the two stand at the head of
  `spoiler.txt`, in the GUI and in the version resources only. Measured at
  seed 380 over the thirteen levels, plain and with `mix cross allsanity 8
  weapons items20 weather`: all 78 files (`.trc`, `.hdm`, log) of the
  `Randomize.exe` before the bump and after it byte-identical. With #244 it
  moved from 20 to 21: one byte of RICH2's level changes at every seed, the
  version string stays.

In every one of these the difference is EXPECTED. What still has to hold
is the other half of the recipe: the same seed twice must give the same
bytes, and a level the change cannot reach must not move at all.

**A known break in the chain: portal windows built from floordata.**
Where geometry is built for rooms that have none, `Make_Portal` writes its
view windows corner-exact out of the floordata instead of out of the
sector's two height bytes. Every level a secret list opens a portal in
therefore comes out with DIFFERENT bytes than a build using the old window
size, and that is intended, not a regression - the old windows were too
small and the engine clips the neighbouring room to them
(`SetRoomBounds`). Andrea3 is the measure: exactly 20 bytes differ, the six
y values of the three new windows, and no geometry byte moves. Checking a
change here, expect this and check that the differing bytes are only window
corners.

---

## 1b. Does every enemy still fit under its ceiling?

`Diagnose\HeightCheck` reads a FINISHED level and compares, for every enemy
item, the model's rest-pose height (anim 0 / frame 0) against the clearance
of its sector. Run it over all fifteen output levels after anything that
touches enemy placement, and over the fifteen vanilla levels as the
calibration.

```
HeightCheck <every output .trc>          expected: 0 violations
HeightCheck <every Backup .trc>          expected: 0 violations - the calibration
HeightCheck heights <level.trc>          the measured heights, for a spot check
```

**Why the vanilla run matters more than the output run.** Core's own
placements are the yardstick. A naive measure calls ten of
them too tall, when the measure itself is wrong: `GetCeiling` walks up
through the ceiling portals (`sky_room`) before it reads a ceiling byte, and
a sector whose ceiling IS a portal carries the room boundary there, not a
ceiling. Whoever writes a headroom rule and skips that walk bans spots the
game has used since 2000.

**What it caught:** 28 enemies with their head through the ceiling in the
fifteen levels of one seed, among them a 1546 unit soldier statue under a
1024 unit ceiling (rich1 item 259, the player's find in game).

**A spot check the tool cannot give you:** whether the placement gave up too
often. That number stands in the run's own notes - `headroom: N of M swap
places kept their vanilla kind` and `headroom: N extra-enemy spots dropped`.
Both were 0 in every level of the seed the rule was built on; anything much
above that means the filter is too tight, not that the level is.

---

## 2. What is object id N really?

Count the enum, do not trust a name or a list.

```python
m = re.search(r"enum\s+object_types\s*\{(.*?)\n\};", text, re.S)
# walk the entries, honour explicit "= 123", strip comments
```

**What this caught:** fourteen entries written as `???` in a documentation
file, and a switch listed as 274 that is really 272 - 274 being a different
object that occurs seventeen times in three levels.

**Why by hand fails:** the enum has explicit assignments, `_MIP` twins and
comments between entries. Counting lines gives a plausible wrong number.

**And the trap behind the trap:** four slots do not mean what their enum
NAME says - 31, 43, 53 and 75 hold the Tomb Raider III guests. 31 is the
T-Rex while still being called `SAS`. Read the number.

---

## 3. Is a claim in a document still true?

grep the symbol, then **read the lines around the hit** before concluding.

A hit is not proof. It can sit in a comment, in a dead branch, in a string,
or in the other of two number spaces - object slots and inventory slots look
identical as integers and are not.

**Line numbers rot.** The trigger document cites `struct FLOOR_INFO` at
`global/types.h:603`; it stands at line 683, and the same document's
`control.cpp` citations are off by different amounts (`GetHeight` by 80,
`_TestTriggers` by 102). Cite the symbol; treat the number as an anchor with
a date.

**What this caught:** a header promising a version check between engine and
randomizer that was never built - `grep -r RANDO_BUILD_ID "TOMB5 Code"` finds
nothing. A promise in a comment is not a feature.

---

## 4. Is there any German, or any machine path, left?

**One detector always lies.** Use three, and whitelist by LINE, not by file.

1. Function words (der/die/das) - finds prose, misses everything else.
2. Morphology (`-ung -heit -keit -schaft -chen -nis`) - finds compounds a
   word list never has.
3. Transliterated umlauts (`waehl`, `fuer`, `ueber`, `pruef`) - the strongest
   remaining signal, because the English pass wrote them that way.

**How this failed:** a sweep reported the repository clean. It had whitelisted
whole FILES for their allowed German - so German anywhere else inside those
files was invisible. Nine leftovers were sitting in them, including an
identifier and a runtime log line.

**What a word list can never find:** identifiers (`ziel`, `ton`, `Wofuer`),
data tokens (cache suffixes, ImGui ids), and file names. Those need their own
pass.

**And a fourth detector: the WIDE
string literal.** A German sentence inside `L"..."` goes through a line scan
untouched and surfaces only in the identifier pass, through the single word
`Ordner` - four unambiguous German words in one line, invisible to the
detector that is supposed to find exactly that. It was the launcher's
folder-picker title (`TR5Randomizer/src/gui/Main.cpp`), the one UI string
that went round the translation table instead of through it; `pickFolder`
passes it through `T()` and `Language.cpp` like the rest. Scan `L"` and `u8"` literals in
their own pass, and treat a string that bypasses the translation table as a
finding in itself - it is where German hides.

**Expect false positives and name them.** The licence header contains
"native Windows 11 port", which matches a search for the work-tree folder
name. 110 of 112 hits in one such sweep were that one line.

---

## 5. Encoding

**Never send a file through a line-ending pass without knowing what it is.**
In UTF-16 every `0A` is half a character; converting it to `0D 0A` shreds the
file. In a binary it is data.

```python
def byte_exact(data):
    return data[:2] in (b"\xff\xfe", b"\xfe\xff") or b"\x00" in data[:8192]
```

**What this cost once:** every accented character in `AccentToCode` was
replaced by the Unicode replacement character, turning thirty-four branches
into thirty-four comparisons against the same value. It compiled and it
ran.

**How to find it:** `grep -c $'\xef\xbf\xbd'` over the sources, plus a check
for control characters that are not CR, LF or TAB.

**How to repair it:** take the pristine upstream file, match line by line on
the ASCII skeleton (every byte > 126 replaced by `?`, runs collapsed), and
replace ONLY lines that contain the corrupted sequence. Own changes survive.

---

## 6. Did the build really do what you think?

**A clean build is not proof of anything but a clean build.**

- A version resource can compile without complaint, put its strings in the
  EXE, and still report `0.0.0.0` - `VS_VERSION_INFO` needs `winres.h`, or
  the block is stored under that name instead of as resource id 1. Read it
  back with `[System.Diagnostics.FileVersionInfo]::GetVersionInfo(<exe>)`.
- A tool's usage line can be years behind its own `main()`. Compare the
  header against the argument parsing, not against memory.
- A build script can name a source with an absolute path and work perfectly
  on one machine. Build from a fresh clone before believing it.

---

## 7. Generated data drifts silently

`build\data` is gitignored. A change to the curation in `ZoneAnalysis.ps1`
does nothing until the zone exports are regenerated, and nothing warns you.
`BuildRelease.ps1` regenerates all fifteen; see `Preparation\README.md` for
the whole chain.

The same applies to the TR3 extraction cache: it is a cache, and the code
says so - "extract anew every time - slower, but always correct". If a cache
key changes, entries are orphaned and re-extracted. That was measured, not
assumed: the re-extracted files came back byte-identical.

---

## 8. Does the 64-bit build play the same game?

The 64-bit build (`release64\PCtomb5_x64.exe`, `be64.bat`) must play
exactly the game the 32-bit one plays. A pointer cut to 32 bits, a struct
laid out wider or a list running into its neighbour does not show at once;
it shows as a line of the `-log` seams that differs. So the same game is
played on both builds and their logs are compared line by line: the same
level files, the same input, the same ticks.

A person cannot play the same five minutes twice, so the 32-bit build
records what the run takes from outside and both builds play it again:

- `-record <file>` writes, once per call of the input function, what the
  keyboard, the mouse and the pad delivered, with a check of the game's
  state; per present the number of 1/60 s ticks the logic takes from it;
  per drawn frame its place between two ticks as the clock gave it (at the
  display-bound frame rate the frame is blended by it, and the next tick
  reads Lara's joints from that frame - the camera's target, a drone's aim
  at her torso); at every level start the two random generators' seeds and
  the frame counter; at every save and load the slot.
- `-replay <file>` puts the file's values in place of the devices and the
  clock, record by record, and checks each one against the run (its type
  and its tick). When the file ends, or a record does not fit, a
  `REPLAYLOG end` or `REPLAYLOG desync` line says so and the game goes on
  with the devices. The first input call whose state check differs writes
  `REPLAYLOG drift tick=`: from that tick the replayed game is another one.
  A file of an older version (`run1.rec` of the first build of the
  recorder) is refused; record again.

**What must be the same in every run.** The level files (the same `data\`
folder, or the byte-identical randomizer output of section 1), the options
(both builds read the same stored settings - do not change an option during
the recorded run, or the replays start with the changed one), and the
savegame folder as it was when the recording began (a replay loads what
the recording loaded). Films are left out with `-nofmv`: the 64-bit build
has none, and the 32-bit one must not play them either.

**The run.** In a Command Prompt in the game folder, with `PCtomb5.exe`
and `PCtomb5_x64.exe` (and their `.pdb`) side by side, `<repo>` the clone:

```
rem 0. once: keep the savegames as they are now
xcopy /e /i /y savegame savegame_before

rem 1. record on the 32-bit build
PCtomb5.exe -log -nofmv -record run1.rec
ren log.txt rec_x86.log
copy savegame\savegame.<m> rec_m.sav

rem 2. play it again on the 32-bit build
rmdir /s /q savegame
xcopy /e /i /y savegame_before savegame
PCtomb5.exe -log -nofmv -replay run1.rec
ren log.txt rep_x86.log
copy savegame\savegame.<m> x86_m.sav

rem 3. play it again on the 64-bit build
rmdir /s /q savegame
xcopy /e /i /y savegame_before savegame
PCtomb5_x64.exe -log -nofmv -replay run1.rec
ren log.txt rep_x64.log
copy savegame\savegame.<m> x64_m.sav

rem 4. compare
python <repo>\Preparation\Tools\Diagnose\xdiff.py rec_x86.log rep_x86.log > xdiff_x86.txt
python <repo>\Preparation\Tools\Diagnose\xdiff.py rep_x86.log rep_x64.log > xdiff_x64.txt
<repo>\Preparation\Tools\Diagnose\SaveDiff.exe x86_m.sav x64_m.sav
```

`SaveDiff.exe` is built once with `bsd.bat`. A file name for `-record` and
`-replay` must not contain the words `setup` or `nofmv`: the command line
takes them as switches of their own.

**What to play in step 1**, about five minutes: from the title, load a
savegame in slot `<n>` that this 32-bit build wrote (an older save holds
pool distances, see below, or is refused by the 64-bit build), play
on into a level start, save into slot `<m>`, load it again, fight the
brawler (`BRAWLLOG`) and a guard, climb a rope or a ladder, open the
inventory and use something, and quit through the menu. Leave the keyboard,
the mouse and the pad alone while a replay runs - it ignores them until its
file ends - and do not switch away from the window, which holds the game.

**The save cross test** is part of the run: in step 3 the 64-bit build
loads slot `<n>`, written by the 32-bit build, plays the recorded minutes
and saves slot `<m>`; in step 2 the 32-bit build does the same.
`SaveDiff x86_m.sav x64_m.sav` must print `IDENTICAL - every one of the
29491 bytes.` - no field is expected to differ. Every pointer a save holds
is written by number (`game\save_pointer_rule.h`): an item by its item
number, a creature's AI target by the number of its creature's item,
Lara's meshes by their number; none is a distance in the pool, which the
64-bit build lays out wider. At the next load `SAVELOG` says how each one
was read - `how=tag_item`, `how=tag_ai`, `how=tag_num` or `how=null` - and
a `SAVELOG save ... kind=outside` line names a pointer the rule could not
name and wrote as null; send that line if one appears.
The reverse: write slot `<n>` with the 64-bit build, record `run2.rec` on
`PCtomb5_x64.exe`, play it again on both builds (steps 2 and 3 with
`run2.rec`) and compare the two `<m>` files the same way.

A save written by a build before this rule holds those pointers as
distances. It is read the old way (`how=legacy_ok` or `how=legacy_null`),
exact only on the build that wrote it, so slot `<n>` must be written by
this build.

**What a clean result prints.** `xdiff_x86.txt` proves the recorder: the
recorded run and its replay on the same build. `xdiff_x64.txt` is the
answer. Both end in the family table and, when the builds agree:

```
no logic difference

no load difference

no audio difference

no float-only difference
...
0 logic/load lines differ, 0 audio, 0 float-only; ...
```

and no `REPLAYLOG desync` or `drift` line at the top. Between the
recorded run and a replay a few `audio` lines may differ (a sound that waits
for its last one starts by the audio device's clock, and its volume follows
the drawn camera); between the builds the `float-only` lines may differ in
the last digit, where a seam prints a float the two C runtimes computed
(`sin`, `cos`, `sqrt` differ in their last bit). The braid (`HAIRLOG`) is a
render family and dropped. The game logic itself is integer arithmetic and
must not differ. The summary's last lines say where each log was compared:
from the first `REPLAYLOG level=` line to `REPLAYLOG end`; after it the
devices drive the game again (the title the run returns to).

**What a difference means.** `FIRST LOGIC DIFFERENCE` names the family,
both lines with their line numbers, the logic second before it (`TICKLOG
at=`), the last tick counter a logic line wrote and the last `ITEMLOG` item
before it; the `item=` of the line itself where it has one. The first one is
the one that counts: everything after it may be its consequence. A hard
difference in a family that also has float-only ones may be a float that
crossed an integer step - read the float lines before it. A `REPLAYLOG
desync` line means the replay left the recorded run at that tick: the run
asked for input or a present at another moment, and from there the two runs
are different games; the first difference before it is the cause. The
`load` lines differ when the two builds read a level differently - the
level file's records are converted on the 64-bit build.

**What to send:** `run1.rec`, `rec_x86.log`, `rep_x86.log`,
`rep_x64.log`, `xdiff_x86.txt`, `xdiff_x64.txt`, the SaveDiff output and
the slot files `<n>`, `rec_m.sav`, `x86_m.sav`, `x64_m.sav`.

**What it does not see.** The families of the drawn frames (`ARMLOG`,
`ROOMLIST`, `HDLOG`, ...) are dropped: they depend on the frame rate, the
window and the card. Two replays at the same frame-rate mode draw the same
frames, so `--keep-render` compares them too, but the HD renderer's lines
also differ with the card's memory. The loading screen's frames are not in
the file; their number follows how long the load took.

---

## 9. Does a pointer survive a level change?

At every level change `FreeLevel` hands the whole game_malloc pool back, and
the next level is loaded into the same memory. A pointer kept from the level
before then names the new level's data: a write through it lands in a room,
an item or a mesh of the new level, and shows, if at all, as a crash in some
later load. The load crash of 2026-08-25 was one (`baddie_slots`). And a
pointer cut to 32 bits on the 64-bit build is still right whenever the memory
it names happens to lie below 4 GB. Two test builds turn both into a stop at
the first use: the 64-bit engine under AddressSanitizer (run 1) and the
64-bit engine with `-high` (run 2). Neither goes into a package.

**Run 1, the sanitizer build.** `basan.bat` builds
`releaseasan\PCtomb5_asan.exe` and copies the sanitizer's runtime,
`clang_rt.asan_dynamic-x86_64.dll`, beside it - out of the Visual Studio
toolset that compiled it (`VC\Tools\MSVC\<version>\bin\Hostx64\x64`); without
the DLL the EXE does not start. In this build the pool is poisoned wherever
it has not handed out a block since the last `FreeLevel`, and 32 bytes behind
every block. In a Command Prompt in the game folder, `<repo>` the clone:

```
copy <repo>\releaseasan\PCtomb5_asan.exe .
copy <repo>\releaseasan\PCtomb5_asan.pdb .
copy <repo>\releaseasan\clang_rt.asan_dynamic-x86_64.dll .
PCtomb5_asan.exe -log -nofmv
```

The report goes to `asan_report.<pid>` in the game folder without any
setting. To put it elsewhere, `set ASAN_OPTIONS=log_path=asan\run` before the
start, a path without a drive letter (the option list is separated by
colons). The build runs slower than the release one.

**What to play.** Every way the pool is handed back, and the level pairs where
a pointer of the level before was read until `FreeLevel` cleared it (#309):

- From the title into a level, and through the end of a level into the next
  one: ANDREA2 into ANDREA3 (the AI objects and the rats of the one, none in
  the other), ANDY3 into RICH1 (the bats), RICH1 into RICH2 (the spiders); in
  a shuffled order, any level after one of these.
- Back to the title from a level, and a cut level: `CUTS` in the title picks a
  cutscene, `FILTHY` and `1` to `4` in the title jumps to a chapter.
- A savegame of another level loaded from inside a level, and one of the same
  level.
- A death, and the load from the death menu.
- The level skip: the compass in the hand, `S` `K` `I` `P` held together.
- In each level a fight, a door, a pickup and the inventory.

**Where the report lands.** At a report the game closes at once, without a
message (exit code 1):

- `asan_report.<pid>`: the sanitizer's report.
- `log.txt`: the same report line by line as `ASANLOG ...`, then the crash
  seam's line `CRASH code=asan at=0x<pc> module=PCtomb5_asan.exe+0x<offset>
  thread=<main|game|other> kind=<kind> addr=0x<address> access=write
  size=<n> report=asan_report.<pid>` and `CRASH
  dump=PCtomb5_<date>_<time>.dmp`. A level loads on a thread of its own,
  `other` on the line.
  Without `-log` the `CRASH` line goes into `crash.txt`.
- `PCtomb5_<date>_<time>.dmp`: the threads at the moment of the report.

In this build the sanitizer reports an access violation or a division by
zero itself (`kind=` names it), where the other builds' crash seam would.

The first report ends the run. To collect every report of one run instead,
`set ASAN_OPTIONS=continue_on_error=1` before the start, together with
`-log`: each report then lands only in `log.txt` as `ASANLOG` lines, the game
goes on after it (in whatever state the bad access left), and no `CRASH`
line or dump is written for it.

**How to read it.** The report's first frame is the line `#0 0x<address> in
<function> <file>:<line>` under `ERROR: AddressSanitizer: <kind> on address
...`, repeated in the `SUMMARY:` line at its end. With `PCtomb5_asan.pdb`
beside the EXE it names the source line; without it the frame reads
`(PCtomb5_asan.exe+0x<offset>)`, the `module=` of the `CRASH` line, and only
the `.pdb` of the same build resolves it (open the dump in Visual Studio with
that `.pdb`). The kinds:

- `use-after-poison` at an address inside the pool (`is located N bytes
  inside of 28388608-byte region`, the pool; `ASANLOG pool base=` at the top
  of the log is its start): either pool memory no block of this level covers
  - a pointer kept from the level before - or the red zone behind a block -
  a block written past its end. The frames below `#0` say which function
  held the pointer.
- `heap-buffer-overflow`, `heap-use-after-free`, `stack-buffer-overflow`,
  `global-buffer-overflow`: the same mistakes outside the pool.
- `access-violation`: a wild pointer.

The run cannot see a pointer of the level before whose address the new
level has already been handed again: that block is the new level's and the
access a valid one. That class is held by `FreeLevel`'s clears
(MODIFICATIONS.md, #309). `ITEMWATCH`, on with `-log`, still names an item
whose room was written over while the items were initialised.

**What to send:** `asan_report.<pid>`, `log.txt`, the `.dmp`, and which build
(the first line of the log).

**What the first runs found.** Two runs of this recipe, one with
`continue_on_error=1`, through forty level changes, sixty deaths and the
loads: three faults of the original, all reads at index -1 - a sound the
level does not carry stopped through its soundmap entry (#311), the forced
fixed camera reading the flags of camera -1 on every tick of a trapdoor or
turn switch, and the inventory ring's start position indexed with an empty
slot (#312). Each showed as `use-after-poison` or `global-buffer-overflow`
with the source line in frame `#0`; the engine's own clears of #309 raised
no report.

**Run 2, `-high`.** The same game as section 8's recording, played again by
the 64-bit build with `-high`: at the start every free region below 4 GB is
reserved and the pool is taken from the top of the address space, so a
pointer cut to 32 bits names a reserved page and faults at its first use. In
the game folder of section 8, with `run1.rec` and `rep_x64.log`:

```
rmdir /s /q savegame
xcopy /e /i /y savegame_before savegame
PCtomb5_x64.exe -log -nofmv -high -replay run1.rec
ren log.txt rep_x64_high.log
python <repo>\Preparation\Tools\Diagnose\xdiff.py rep_x64.log rep_x64_high.log > xdiff_high.txt
```

Near the top of `rep_x64_high.log`: `HIGHLOG walk reserved=<n> bytes=<n>
held=<n> left=<n> failed=0` - `held=` the bytes below 4 GB taken before the
switch was read (in a test process only the system's two shared pages, 8192
bytes), `left=` the free pieces too small to reserve - and after the `POOL`
line `HIGHLOG reserved=<n> regions below 4 GB, pool base=0x00007FF...
top-down`. `xdiff_high.txt` must say what `xdiff_x64.txt` says: no logic
difference, since where the pool lies does not enter the game. A cut pointer
shows as a `CRASH code=0xC0000005` line with its dump, at the tick the
`REPLAYLOG` lines before it name. A file name for the command line must not
contain ` -high`.

**What to send:** `rep_x64_high.log`, `xdiff_high.txt`, and a `.dmp` if one
was written.
## 10. Does the 60 fps mode play the classic game?

The logic runs thirty steps a second at every frame rate; the 60 fps modes
only add pictures between two steps. The logic must read nothing back from
such a picture. Lara's joint set (what `GetLaraJointPos` gives the drones'
aim, the ledge test, the weapons, the cameras and the drips) and her wet
nodes are built again from the step's own pose after every blended picture
(`game\draw.cpp`, `LaraJointsLive`). The proof is a replay whose pictures
fall at other places between the steps than the recording's. It needs the
recorder, which the 64-bit build carries:

1. Record a stretch with fights at *Frame rate: Display*, best with
   something shooting at Lara (the drones of ANDREA2):
   `PCtomb5.exe -log -record run.rec`.
2. Replay it with the replaying machine's own clock in place of the
   recorded sub-ticks: `PCtomb5.exe -log -replay run.rec -liveclock`
   (the switch after the file: the word after `-replay` is the file). The
   sub-tick records are still read and checked by type and tick; only the
   value the picture is blended by is the clock's. The open line ends
   `clock=live` - that is the check that the switch was read;
   `clock=recorded` is a replay of the recording's own pictures, which
   proves nothing here.
3. The log of the replay holds no `REPLAYLOG drift` line, and
   `REPLAYLOG end` names the recording's last tick. A drift line names the
   tick where the replayed game left the recorded one. Under the live clock
   the ticks, the input and the generators are the recording's, so a drift
   means the logic read something back from a picture: a reader of the
   blend that #310 did not catch - unless it is one of the two leftovers
   below. Around the drift tick compare the two logs' `TICKLOG` second
   (`after_death=`, a body's fade step) with the `ITEMLOG` line of the item
   that died there (its room), and the level's `DOORLOG moves=`: a fade
   step or a door move that differs between recording and replay, with the
   body's room or the door's portal at the picture's edge, is the known
   leftover; the same counts on both sides and a drift is a new finding.
   The drift line names the tick, not what parted: with `-log` both runs
   write a `STATELOG` line per input call (`tick= seed= draw= x= y= z=
   rot= hp= target= active= dead=`), and `xdiff.py` over the two logs
   names the first line that differs. Its first differing field names the
   reader: `seed=` alone, something in the step before drew another
   number; `hp=`, something hurt Lara; `x=`..`rot=`, her own control;
   `target=`, her weapons chose another enemy; `active=`, an item was
   started or ended on another step; `dead=`, a body fades a step longer
   or shorter (the leftover below). A field that parts on an earlier tick
   than `seed=` is the cause, the seed the consequence. A body's fade one
   step off by itself is no drift: a body whose death animation ended is
   off the active chain and out of the AI slots, and the kill a step later
   draws no number and touches nothing of Lara's (#314: ANDREA2, the fade
   one step apart in one live replay and equal in the other, both
   drifting at the same tick with the same hashes).

   When only `seed=` parts, the call that drew one number more or less is
   named by `-rngtrace` (#316): replay the same file twice with the same
   build, once with the recorded clock and once with `-liveclock`, each
   with `-rngtrace <drift tick - 6> <drift tick + 3>` beside the switches
   the drifting run had, and keep both logs. For a drift at tick 842 with
   the Asan build, from the game folder in `cmd`:

       set "ASAN_OPTIONS=continue_on_error=1" & start /wait PCtomb5_asan.exe -log -high -replay run1.rec -rngtrace 836 845 & ren log.txt rt_rec.log
       set "ASAN_OPTIONS=continue_on_error=1" & start /wait PCtomb5_asan.exe -log -high -replay run1.rec -liveclock -rngtrace 836 845 & ren log.txt rt_live.log

   Each log carries `RNGTRACE window from=836 to=845` near its top and one
   `RNGTRACE tick=` line per input call from 836 to 845, the call list of
   the span that ends there, followed by its `RNGTRACE section=` lines.
   Then, beside the two logs, with the EXE that wrote them and its PDB
   (`bal.bat` builds `AddrLine.exe` beside the script first):

       python <repo>\Preparation\Tools\Diagnose\rngtrace_diff.py rt_rec.log rt_live.log --exe <folder>\PCtomb5_asan.exe

   `Preparation\Tools\Diagnose\rngtrace_diff.py` pairs the spans, prints
   the first tick whose call lists differ, the section marks of both side
   by side with the first one whose state differs, the index of the first
   differing call and the marks it falls between on either side, ten
   addresses around it in both logs, and each of them through
   `AddrLine.exe <exe> -ret <addresses>` as `file:line function`. Without
   `--exe` it takes `<repo>\releaseasan\PCtomb5_asan.exe`, the tree's own
   Asan build. The same lookup by hand:
   `AddrLine.exe <folder>\PCtomb5_asan.exe -ret 5622 2d7a`.
   The call that appears on one side only, or another call at the same
   place, is where the two runs part; what that caller read is the next
   question, not answered by the trace.

   The first run of this kind named `TriggerExplosionSparks` and
   `TriggerExplosionSmokeEnd`, 26 draws, on the live side only (#317): a
   chained explosion, which `UpdateSparks` fires when a spark reaches a set
   age. The draw had ended that spark in the recorded run -
   `S_DrawDrawSparksNEW` ended a spark whose depth reached the far plane,
   and that depth comes from the frame's camera, so where the pictures
   fell decided whether the explosion came. With `-log` each such end is a
   `SPARKLOG kill` line (and `SPARKLOG drip kill`, `SPARKLOG bubble kill`
   for the two of the same kind): with a build before the fix the
   recorded-clock log shows a `SPARKLOG kill` with `chain=` above 0 before
   the drift tick, the live-clock log does not. The first such run wrote no
   `SPARKLOG kill` at all, so `SPARKLOG born`, `fire`, `end` (why=life,
   fade or evict), `light` and `light_index` now follow every chain spark
   in `game\effect2.cpp`: the live log's `fire` line that the other log
   lacks names a slot, the last `born` in that slot before it its born
   tick, and the recorded-clock log's lines for that slot and born tick
   tell its fate there.
4. `JOINTLOG passes=<n> ticks=<m> tween_frames=<k> blended_frames=<b>`,
   one line at the end of each level and of the title: `passes` equals
   `blended_frames`; at 30 both are 0; at 60 they are about `ticks` (a
   late frame adds one); at *Display* nearly every frame is blended.

5. `RNGLOG frames=<f> given_back=<g> undone=<u> kept_frames=<k>
   kept_draws=<d> heights=<h> over=<o>` right after each `JOINTLOG`: the
   pictures that gave the random numbers and `GetHeight`'s globals back
   (`given_back`, `undone` draws) and those that kept them (`kept_frames`,
   `kept_draws`). At 30 `given_back` is 0 and `kept_frames` equals
   `frames`; at 60 each is about `ticks`; at *Display* `kept_frames` is 0.
   Compare the recording's
   and the replay's: at *Display* `kept_draws` is 0 in both, whatever the
   two displays' rates; `over` is 0.
   `DOORLOG moves=<m> frames=<f> skipped_tween=<s>` right after each
   `RNGLOG`: the closed doors' moves between two rooms' item lists, the
   pictures that walked the doors and the tween pictures that did not. At
   30 `skipped_tween` is 0; at 60 `frames` is about `ticks` and
   `skipped_tween` about as many; at *Display* `frames` is about `ticks`
   too, whatever the display's rate.

The corpse fade (`StepAfterDeathFade`) counts a body seen by one picture
per step, the one that is no tween picture. At *Display* that picture is
blended at a fraction the clock sets, so a drift on the tick a body
vanishes, with the body's room at the picture's edge, is the one leftover
#310 measured and left. `ProcessClosedDoors`, which moves a closed door
between its two rooms' item lists by what the picture's walk saw, follows
the same rule and leaves the same residual: at *Display* the lists follow
that blended picture, once per step.

## 11. Do the films play on the 64-bit build?

The 64-bit build cannot load the game's 32-bit `binkw32.dll`; it plays the
films through three FFmpeg DLLs beside its EXE (`specific\fmv_av.cpp`,
`vendor\ffmpeg-min`). Two steps, the first without the game:

1. The decoder. Build `BinkProbe` (`bbp.bat`) and run it on each of the
   nine films: `BinkProbe.exe <repo>\vendor\ffmpeg-min\bin
   <game>\movie\fmv1.bik`. Every film reports a video stream `binkvideo`
   640x480 `yuv420p` and an audio stream, and decodes to its end with no
   error line, many times faster than real time. `-bmp 300 out.bmp` writes
   frame 300 through the engine's own conversion (`specific\yuv_rgb.cpp`):
   the picture must look as the film does, no colour cast, not upside
   down, and the three test pixels it prints are black, white and red.
2. The game. Copy `release64\` with the three DLLs into the game folder
   and start `PCtomb5_x64.exe -log`. Watch the intro: the sound in time
   with the picture at the start and still after a minute (the lips, a
   door, a shot). Skip it once with each of the three keys - Escape
   (`IN_OPTION`), the draw key (`IN_DRAW`) and Enter or Cross
   (`IN_SELECT`): a key already held when a film starts does not skip it,
   only a new press does; the title must come up after it, with its
   music. Play a level to its end for its outro film. Then read the log:
   `FMVLOG av loaded avutil=61.20.100 avcodec=63.26.100
   avformat=63.18.100` at the start, and per film
   `FMVLOG film=<n> frames=<shown> dropped=<d> audio_frames=<a>
   skipped=<0|1> seconds=<t>`. A film played to its end shows about all
   its frames (fmv1: 1857) with `dropped` near 0 and `seconds` near its
   length (fmv1: 74.3); a skipped one says `skipped=1`. A `clock=wall` line
   means its audio stopped being the clock - an audio device that went
   away, or a film whose sound ends early. Without the DLLs the start says
   `FMVLOG av missing avutil-61.dll` and the game runs without films.
