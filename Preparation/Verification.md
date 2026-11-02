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

## 8. Does the 60 fps mode play the classic game?

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
   recorded sub-ticks: `PCtomb5.exe -log -replay run.rec -liveclock`.
3. The log of the replay holds no `REPLAYLOG drift` line, and
   `REPLAYLOG end` names the recording's last tick. A drift line names the
   tick where the replayed game left the recorded one.
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

The corpse fade (`StepAfterDeathFade`) counts a body seen by one picture
per step, the one that is no tween picture. At *Display* that picture is
blended at a fraction the clock sets, so a drift on the tick a body
vanishes, with the body's room at the picture's edge, is the one leftover
#310 measured and left. `ProcessClosedDoors`, called from the room walk of
every picture, is another: it moves a closed door between its two rooms'
item lists by what the picture's walk saw.
