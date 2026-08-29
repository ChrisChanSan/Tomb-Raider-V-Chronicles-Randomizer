# Rolling the level order — findings and plan

As of 2026-08-22. Occasion: the two grayed-out checkboxes "Shuffle level
sections" and "Shuffle level order". Before building: what travels between levels, and what does the
engine hang on?

## 1. What the SCRIPT.DAT gives and takes per level

From the vanilla SCRIPT.DAT (`ScriptCheck -raw`), inventory numbers
resolved via `invobj_types`:

| Level | gives at start | takes at start |
|---|---|---|
| Andrea1 | PICKUP_ITEM1, Uzi | Uzi (a dev relic: give and immediately take) |
| joby3 | — | Uzi, pistols, revolver, lasersight (Lara captured) |
| rich1 | HK | — |
| rich2 | HK, PUZZLE_ITEM2 | PICKUP_ITEM2, KEY_ITEM7, KEY_ITEM8 (+ our pistols) |
| GIBBY (dev) | crossbow ammo, HK, lasersight | — |

No other level touches the inventory. **Everything a level does not take
stays in the inventory** — keys included.

## 2. Key matrix (items vs locks per level)

`ItemCheck` over all vanilla levels, family PUZZLE/KEY/PICKUP (IDs
172–265 per the engine enum; counting trap heeded, cross-check LARA=0,
KEY_HOLE8=265). PICKUP_ITEM4 = the three secret roses per level.

| Level | items | locks | finding |
|---|---|---|---|
| Andrea1 | PUZZLE1×2, KEY1, PICKUP2 | PUZZLE_HOLE1×2, KEY_HOLE1 | self-contained; PICKUP2 without a lock |
| Andrea2 | PUZZLE1,3,4,5, **crowbar** | PUZZLE_HOLE1,3,4,5 | self-contained; the crowbar stays in the inventory |
| Andrea3 | PUZZLE4 (2 halves), KEY1, KEY2, PICKUP1 | PUZZLE_HOLE4×2, KEY_HOLE1, KEY_HOLE2 | PICKUP1 = story (cutscene, `deltapak.cpp:1767`) |
| joby2 | PUZZLE1, **KEY7×2, KEY8** | PUZZLE_HOLE1, KEY_HOLE7×2, KEY_HOLE8×4 | KEY8 travels on |
| joby3 | PUZZLE1, PUZZLE3+4 (halves), KEY6, KEY7 | PUZZLE_HOLE3,4, KEY_HOLE6,7, **KEY_HOLE8×2** | **needs KEY8 from joby2**; PUZZLE1 without a lock |
| joby4 | — | — | a pure diving level |
| joby5 | PUZZLE1,2, KEY6, KEY8 | PUZZLE_HOLE1,2, KEY_HOLE6, **KEY_HOLE7**, KEY_HOLE8×2 | **needs KEY7 from joby2/joby3** |
| andy1 | PUZZLE1, PUZZLE3, PUZZLE2 (halves), torch | PUZZLE_HOLE1 | **PUZZLE2+3 travel on** |
| Andy2 | — | PUZZLE_HOLE1, PUZZLE_HOLE2 | **needs puzzles from andy1** |
| andy3 | PUZZLE1,2,3, torch | — | items without locks (story/cutscene) |
| rich1 | PUZZLE3,4, KEY7, KEY8, PICKUP2, bottles, cloth | PUZZLE_HOLE1,3,4, KEY_HOLE8×4 | **PUZZLE_HOLE1 without an item** (check its origin); KEY7 is taken away by rich2 |
| rich2 | PUZZLE3, PICKUP2, bottles, cloth | PUZZLE_HOLE2,3, PUZZLE_HOLE8×2 | HOLE2 via the scripted gift PUZZLE2; check HOLE8 |
| rich3 | PUZZLE1 (halves) | PUZZLE_HOLE1 | self-contained |
| richcut2 | — | — | cutscene level between rich2 and rich3 |

Caution: lock counts can be inflated by flipmap rooms (an item in both
the normal and the flip room counts twice). The *direction* of the
dependency is unambiguous anyway.

**Cross-level dependencies (vanilla):**
- joby2 → joby3: KEY_ITEM8 (exactly what the user had built in for his
  secret — now we know why joby3 never needed the model: the key came in
  the inventory).
- joby2/joby3 → joby5: KEY_ITEM7.
- andy1 → Andy2: PUZZLE_ITEM2 (combined) and PUZZLE_ITEM3.
- Andrea2 → (Andrea3?): crowbar — `door.cpp:314ff` knows crowbar doors;
  which doors those are in Andrea3 remains to be checked (no
  CROWBAR_SWITCH in any level).
- rich1 → rich2: nothing (rich2 deliberately takes KEY7/8 away; PUZZLE2
  comes by script).

Story gates hanging on the inventory: `control.cpp:781` only ends a
level with PICKUP_ITEM1 in the inventory (flipeffect 65) — level-bound,
must travel with the level, not with the slot.

## 3. What the engine hangs on

- **The level end is purely sequential:** `TO_FINISH` sets
  `gfLevelComplete = gfCurrentLevel + 1`. Order = the order of the
  blocks in the SCRIPT.DAT. Hard jump targets exist only for the cheat,
  the cutscene selection (`cutsel[].lvl`) and "new game = slot 1".
- **121 places check the level number**, of which **120 against
  `LVL5_` constants** (identity: "am I on the 13th floor?") and one as
  an index (`wanky_secrets_table[gfCurrentLevel]`, secrets per level).
  Plus **23 places** that need the slot as an index/arithmetic
  (`gfLevelNames[]`, `+1`, savegame, level select).
- **What travels with a block:** FMV commands, resident cutscenes
  (CUT1–4), flags (horizon, weather, young Lara), the level music, the
  object/inventory commands. Everything a level's story needs stands in
  its block — good for us.
- **What does NOT travel:** the level number itself. After a block
  permutation the engine believes slot 1 is still Rome.

## 4. Core idea A — identity instead of slot

A new engine variable `gfLevelId` (a `LVL5_*` value), derived at level
start from the **file name** of the loaded level (`DoLevel(Name)` →
file index → "DATA\JOBY3" → `LVL5_SUBMARINE`). Then:

- mechanically switch the 120 identity comparisons to `gfLevelId`
  (all compare against `LVL5_` names — one pass with a cross-check);
- index `wanky_secrets_table` via `gfLevelId`;
- leave the 23 slot places **unchanged** (sequence, names, saves);
- the cheat jump and the cutscene selection get a reverse table
  id → slot (built from the SCRIPT.DAT at load time).

Without a permutation, `gfLevelId == gfCurrentLevel` — vanilla behaves
byte-identically. That is the safety anchor: the conversion can be
played through without any order roll.

## 5. Core idea B — resolve dependencies by script

If a level travels before its donor level, a key is missing. Instead of
forbidding the dependency: **the target level gets the item via
`GIVEOBJ` at start** — exactly the command `ScriptDat` already inserts
today for KEY8 and the HK. Dependency table (from section 2):

| Level | receives via GIVEOBJ when the donor does not lie before it |
|---|---|
| joby3 | KEY_ITEM8 |
| joby5 | KEY_ITEM7 (and KEY8 if joby2/joby3 are not before it) |
| Andy2 | PUZZLE_ITEM2, PUZZLE_ITEM3 |
| Andrea3 | crowbar (after checking the doors) |

The scripted **takeaways** were meant to stay where they are.
**Corrected 2026-08-30: they do not.** On every run, before any order roll,
the randomizer deletes joby3's four weapon takeaways and Andrea1's Uzi
takeaway, and ADDS two to rich2 that vanilla has not - pistols and pistol
ammo, because rich2's slot 1 is a scripted second HK. The original text
follows.

The scripted **takeaways** (joby3 weapons, rich2 keys) stay where they
are — they belong to the level's story and travel with the block.

## 6. Staged plan

**Stage 0 — engine identity (the prerequisite for everything).** Core
idea A, without a roll. Test: a vanilla playthrough, all level-specific
things (cutscenes, tower, young-Lara flag, secrets bonus) unchanged.

**Corrected 2026-08-30:** stage 1 is no longer the untouched-inventory case
below. As soon as either checkbox is set, the full machinery runs -
cleanupInventory on every level, then resolveChains with its two grants.

**Stage 1 — shuffle the level sections.** Permute the four sections as
indivisible blocks: Rome (Andrea1–3), Russia (joby2–5), Ireland
(andy1–3), VCI (rich1, rich2, richcut2, rich3). The order inside stays,
so **all** dependencies from section 2 stay valid — no GIVEOBJ needed.
The FMV order also still makes sense (each section brings its own
videos). Risk: low.

**Stage 2 — order inside the sections.** Permutation within the
section, `richcut2` stays glued to rich2. Needs core idea B for
joby3/joby5/Andy2. Risk: medium (verify the key chains in game).

**Stage 3 — free over everything.** Both checkboxes together. Risk:
medium to high — the difficulty curve and the weapon state become
arbitrary (joby3 takes the weapons away no matter when it comes; rich1
gives the HK no matter when).

## 7. Open / to check before building

1. Crowbar doors in Andrea3 (`door.cpp:314`) — which items/OCBs?
2. rich1 PUZZLE_HOLE1 and rich2 PUZZLE_HOLE8×2 without items — flipmap
   duplicates or real imports?
3. The flipeffect-65 gate (`control.cpp:781`): in which level, and does
   PICKUP_ITEM1 there hang on a cutscene of the same level?
4. `available_levels[]` (level select) and the statistics tables are
   slot-based — irrelevant for display, but for the level select they
   must be refilled after a permutation.
5. Savegames: `savegame.CurrentLevel` is a slot. A save thereby belongs
   to *its* SCRIPT.DAT — as with every seed already today. Say so
   clearly in the randomizer.
6. Young-Lara bit (GF_YOUNGLARA): already 0 in the user's script —
   clarify whether vanilla sets it at all or the Ireland outfits handle
   it; otherwise "Ireland first" would be a weaponless starting level.

## 8. Effort

Stage 0 is the lion's share (engine, ~120 mechanical places + the
reverse table + a vanilla regression). Stage 1 afterwards is small
(`ScriptDat`: permute the offset table, the blocks stay untouched).
Stage 2 is medium (dependency table + GIVEOBJ + play tests of the key
chains). Comparable to the language selection plus one TR3-port day.

## 9. State after the first play test (2026-08-22)

- Stages 0 + 1: confirmed in game (sections, videos by identity, the
  finale on the last slot, credits abortable).
- Stages 2 + 3 built (within / free, LevelChains.h, GIVEOBJ + name command
  + model copy). Test seed for "free": **380**.
  **Corrected 2026-08-30: two chains, not three** - Key 8 (joby2 -> joby3)
  and the crowbar (Andrea2 -> Andrea3). Key 7 and the catapult were deleted
  on purpose: joby5's silver key is handed over inside the level by a
  cutscene, and Andy2's Puzzle Hole 2 was repurposed. The "takeaway case"
  is gone as a case - since the per-level reset, resolveChains grants
  unconditionally wherever the donor lies, and the give=false variant is
  used by no chain at all.
- **Play test "free" (seed 380): "what I feared is happening — this is
  going to be something big."** The user is compiling a large error
  list; it will be the basis for the next round. Expected candidates
  (from section 7): orphaned locks (rich1 PUZZLE_HOLE1, rich2
  PUZZLE_HOLE8, Andy2 PUZZLE_HOLE1), the Andrea3 crowbar, level-bound
  story gates, the weapon state after joby3 (takes everything), Ireland
  as the starting level.

## 10. Inventory garbage collector (2026-08-22, the user's proposal)

**The insight:** the inventory slots are generic. `PUZZLE_ITEM1` is the
Garden Key in Andrea1, the Chaff flares in joby4, the Heart in andy1,
the Helipad access key in rich3 — every level renames the same slot. A
carried-along Garden Key *becomes* Chaff flares in joby4. And the
"Grappling Gun" is the crossbow slot (95 + ammo), which only has a
model and anims in rich3 (a hard crash when equipped anywhere else).

**Therefore:** at the start of every campaign level the script takes
away via TAKEOBJ everything from the key family that an earlier level
could have offered and that neither this nor a later level imports via
a chain; the crossbow family everywhere except rich3. Chains
(`LevelChains.h`) are now "give" (GIVEOBJ + name + model) or "keep
only" (story items en route). Engine: `gfPickups`/`gfTakeaways` from 16
to **128** with a guard (**corrected 2026-08-30**; 64 would no longer be
enough - the cleanup writes 74 takeaways into a campaign level's block).

Open for the play test: whether the "keep only" chains suffice (Iris,
Bestiary, Spear, Iron clapper), and whether any level needs an item the
matrix does not show (flipmap duplicates).

**Addendum (play test of the first GC):** the "keep en route" rules let
the Iris travel through half of Rome. Decision: **keys/puzzles/pickups
never travel** — every level starts fresh (**74 TAKEOBJ** per campaign
level, 68 in rich3 - **corrected 2026-08-30**: 8 puzzles and 8 keys with
both halves each = 48, 4 pickups with halves = 12, crowbar, torch, three
examine objects and the three chloroform slots = 68, plus the six-entry
crossbow family everywhere except rich3. The secret rose is no longer
exempt.), only weapons/ammo/medis/flares remain; the four gifts (Key 8
joby3, Key 7 joby5, Catapult Andy2, crowbar Andrea3) always come. A
note for later (no order yet): the gifts could also become pickups in
the level — would need curated areas per lock.
