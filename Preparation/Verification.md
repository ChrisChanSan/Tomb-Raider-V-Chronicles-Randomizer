# How to check things in this project

Written 2026-08-30, out of a day in which a documentation audit turned up
sixty-eight findings and a regression test had to prove that none of the
day's work had changed a single byte of the game.

Each recipe below is here because the obvious way of checking gave the wrong
answer at least once. The wrong answers are written down too - a method is
only trustworthy if you know how it fails.

---

## 1. Did a change alter what the randomizer produces?

The only answer that counts is a byte comparison against the previous build.
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

- **The old commit may not build.** `b1.bat` used to carry an absolute path
  into a folder that has since been renamed. Copy the CURRENT build script
  into the old worktree; the sources are what you are testing, not the
  script.
- **Your own shell holds the folder.** `git worktree remove` and folder
  renames fail with "in use by another process" when a shell sits inside.
  Leave the folder first.

**Exercise the risky path on purpose.** A run that never touches the changed
code proves nothing. When the change was to the TR3 cache keys, the test had
to place a TR3 guest - and the proof was that the new build re-extracted from
Tomb Raider III and produced files byte-identical to the old cache.

**The one difference that is allowed, from 2026-09-05 (#123, stage S7).**
Every randomized level now carries the HD outfit marker in `data[3]` of its
header block, and a sidecar `<level>.hdm` beside it. Comparing across that
commit:

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

**The `.hdm` changed shape on 2026-09-11 (#123, fix round Z), and the `.trc`
did not.** From that commit an OPENING (`Make_Portal`, `Make_Ceiling_Portal`,
`Make_Floor_Portal`, `@Clear`, a view window) writes a `cut <x0> <y0> <z0>
<x1> <y1> <z1>` box instead of two `room` lines, and the version line reads
`hdm 2` whenever such a box is in the file. Only `Move_Room` and
`Make_Room_Geometry` still write `room`. So comparing across that commit:
every `.trc` must still be **byte-identical** - it was measured over the eight
levels whose secret-list lines carry geometry edits at all (ANDREA3, ANDY1,
JOBY3, JOBY5, RICH1, RICH2, RICH3, ANDY3, seed 380, `mix cross allsanity 8
weapons`, glitched list) - and the `.hdm` must NOT be, which is the point of
the round. `HdmDump <level.trc>` prints both kinds of line.

**The box's own numbers changed once more on 2026-09-11 (fix round Z1), and
the `.trc` again did not.** The step of 128 in the plane of the opening turned
from an outset into an INSET, so every `cut` line written before that commit
has its two non-normal axes 256 units wider than the one written after it.
Comparing across it: the eight `.trc` must be byte-identical again (they were,
SHA-256 per file, plus ANDY3 with the glitchless list), and every `cut` line
must have moved by exactly +128 on its two smaller-span axes' lower corner and
-128 on their upper. The span along the normal, the only one that reads 1024,
must not have moved at all. That is a two-line check with `HdmDump` and it is
the whole difference.

`HdmDump <level.trc>` prints `data[3]` with its meaning and the sidecar
beside it, and says so when the two disagree.

**Four more changes that are allowed to move the bytes** (written down in
audit round A1, 2026-09-21, after a bare `rich2` run across Beta 1.0 and
today's head came out different and the list here gave no way to recognise
it as expected):

- **#153, Round M3 - the underwater pickup animation.** The randomizer
  backfills Lara's anim 130 (`ANIM_PICKUP_UW`) into every level whose own
  copy of it is a stub, so items can be picked up under water everywhere.
  This one fires with NO switches at all: the smallest case is
  `Randomize <level> 380` on a level that lacks the animation - six of the
  thirteen did - and the run says so itself, `note: Lara anim 130
  backfilled from joby4`. The seven levels that carry the animation stay
  byte-identical.
- **#141, the headroom rule (see 1b).** Enemy placement now asks whether the
  model's rest-pose height fits under the ceiling, and the Hydra asks for
  five of its nine tiles; both change WHICH candidate a draw lands on, so
  the RNG's own order moves with it. Any run with `mix` or `cross` can
  differ from before.
- **#144, weather.** With the weather option on, a level that has a room
  which can show rain or snow gets a new draw - six of the thirteen can,
  and those are the ones to compare. The run names the result in its own
  notes.
- **#146, the cyborg in the cross pool.** One more foreign kind to draw
  from, so a `cross` run places a different set. Its own commit claims
  byte-identity with the previous build when the two new rows are
  deselected - the case to run when checking that claim.

In every one of these the difference is EXPECTED. What still has to hold
is the other half of the recipe: the same seed twice must give the same
bytes, and a level the change cannot reach must not move at all.

**A known break in the chain, 2026-09-03 (#137).** From the commit that
builds geometry for rooms that have none, `Make_Portal` writes its view
windows corner-exact out of the floordata instead of out of the sector's two
height bytes. Every level a secret list opens a portal in therefore comes out
with DIFFERENT bytes than before that commit, and that is intended, not a
regression - the old windows were too small and the engine clips the
neighbouring room to them (`SetRoomBounds`). Andrea3 is the measure: exactly
20 bytes differ, the six y values of the three new windows, and no geometry
byte moves. Comparing across that commit, expect this and check that the
differing bytes are only window corners; from that commit onward the byte
comparison is meaningful again.

---

## 1b. Does every enemy still fit under its ceiling? (#141)

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
placements are the yardstick. The first version of the measure called ten of
them too tall, and the measure was what was wrong: `GetCeiling` walks up
through the ceiling portals (`sky_room`) before it reads a ceiling byte, and
a sector whose ceiling IS a portal carries the room boundary there, not a
ceiling. Whoever writes a headroom rule and skips that walk bans spots the
game has used since 2000.

**What it caught:** 28 enemies with their head through the ceiling in the
fifteen levels of one seed, among them a 1546 unit soldier statue under a
1024 unit ceiling (rich1 item 259, the user's find in game).

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

**And the trap behind the trap:** four slots no longer mean what their enum
NAME says. 31 is the T-Rex while still being called `SAS`. Read the number.

---

## 3. Is a claim in a document still true?

grep the symbol, then **read the lines around the hit** before concluding.

A hit is not proof. It can sit in a comment, in a dead branch, in a string,
or in the other of two number spaces - object slots and inventory slots look
identical as integers and are not.

**Line numbers rot.** Everything in `global/types.h` has moved down by sixty
lines since the trigger document was written, while `control.cpp` drifted
unevenly. Cite the symbol; treat the number as an anchor with a date.

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

**And a fourth detector the sweep of audit round A1 had to add: the WIDE
string literal.** A German sentence inside `L"..."` went through a line scan
untouched and surfaced only in the identifier pass, through the single word
`Ordner` - four unambiguous German words in one line, invisible to the
detector that was supposed to find exactly that. It was the launcher's
folder-picker title (`TR5Randomizer/src/gui/Main.cpp`), the one UI string
that went round the translation table instead of through it; round A1 routed
it through `Language.cpp` like the rest. Scan `L"` and `u8"` literals in
their own pass, and treat a string that bypasses the translation table as a
finding in itself - it is where German hides.

**Expect false positives and name them.** The licence header contains
"native Windows 11 port", which matches a search for the old work-tree folder
name. 110 of 112 hits in the last sweep were that one line.

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
into thirty-four comparisons against the same value. It compiled, it ran, and
it sat there for weeks.

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
