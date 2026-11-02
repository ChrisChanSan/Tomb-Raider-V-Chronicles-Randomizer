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
  keyboard, the mouse and the pad delivered; per present the number of
  1/60 s ticks the logic takes from it; at every level start the two random
  generators' seeds and the frame counter; at every save and load the slot.
- `-replay <file>` puts the file's values in place of the devices and the
  clock, record by record, and checks each one against the run (its type
  and its tick). When the file ends, or a record does not fit, a
  `REPLAYLOG end` or `REPLAYLOG desync` line says so and the game goes on
  with the devices.

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
savegame in slot `<n>` that the 32-bit build of this branch wrote (a save
of a build before the 64-bit change is refused by the 64-bit build), play
on into a level start, save into slot `<m>`, load it again, fight the
brawler (`BRAWLLOG`) and a guard, climb a rope or a ladder, open the
inventory and use something, and quit through the menu. Leave the keyboard,
the mouse and the pad alone while a replay runs - it ignores them until its
file ends - and do not switch away from the window, which holds the game.

**The save cross test** is part of the run: in step 3 the 64-bit build
loads slot `<n>`, written by the 32-bit build, plays the recorded minutes
and saves slot `<m>`; in step 2 the 32-bit build does the same.
`SaveDiff x86_m.sav x64_m.sav` prints `IDENTICAL - every one of the 29491
bytes.` when, at the save, no creature had an enemy and `GeneralPtr` named
no item. Otherwise exactly these may differ, and nothing else: a creature's
enemy (a run of at most 4 bytes in the savegame stream) and
`Lara.GeneralPtr` with `Lara.target`. They hold a distance in the item pool
of the build that wrote them (`game\save_pointer_rule.h`), and the 64-bit
build's pool is laid out wider. Whether both builds name the same item
there, the `SAVELOG` lines say: at the save `item= kind=`, at the next load
`how= enemy=`/`general=`, which xdiff compares with the distances masked.
The reverse: write slot `<n>` with the 64-bit build, record `run2.rec` on
`PCtomb5_x64.exe`, play it again on both builds (steps 2 and 3 with
`run2.rec`) and compare the two `<m>` files the same way.

One case is known to fail the reverse and the forward test alike: a
creature whose enemy is another item than Lara or its own AI target
(`SAVELOG save ... kind=none`) is saved by its distance alone and read back
as a distance in the reading build's pool, which on the other build is
another item (`how=legacy_ok` with another number) or none
(`how=legacy_null`). Both runs then differ from the load on.

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

and no `REPLAYLOG desync` line at the top. Between the recorded run and a
replay a few `audio` lines may differ (a sound that waits for its last one
starts by the audio device's clock); between the builds the `float-only`
lines may differ in the last digit, where a seam prints a float the two C
runtimes computed (`sin`, `cos`, `sqrt` differ in their last bit). The game
logic itself is integer arithmetic and must not.

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
