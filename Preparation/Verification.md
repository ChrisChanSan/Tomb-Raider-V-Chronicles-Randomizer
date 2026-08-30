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
