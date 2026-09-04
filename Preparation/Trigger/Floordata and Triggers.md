# Floordata and triggers in TR5

Everything here was read off the engine code (`TOMB5 Code\TOMB5`), not
taken from community documentation.

> **On the tools named below, 2026-08-30.** `FloorTool`, `DumpItems` and
> `TRFile_RandomizerExport.cpp` appear in this text and nowhere else in
> the repository. They belonged to an earlier workflow around
> TombItemEdit2026, which is a separate program. What they measured is
> measured today by the tools in `Preparation\Tools\Diagnose` -
> `ItemCheck` for items and OCBs, `SectorCheck` and `FloorCheck` for
> floordata, `TriggerCommands` for trigger chains.

> **On the line numbers, 2026-08-30.** They were right when written and
> they drift with every edit to the engine. **The symbol name is what is
> authoritative here, not the number** - search for it rather than jumping
> to the line. Measured today: everything in `global/types.h` has moved
> down by exactly 60 lines, while `game/control.cpp` has drifted
> unevenly (+1 for `GetHeight`, +23 for `_TestTriggers`), so there is no
> single offset to apply. The structure and the reasoning below were
> re-checked against the source and are unchanged.

The decisive places:
`GetHeight` (`game/control.cpp:960`) walks the chain,
`_TestTriggers` (`game/control.cpp:2126`) evaluates a trigger,
`struct FLOOR_INFO` (`global/types.h:603`) is the entry point.

---

## 1. Where the data lies

In the main block, right behind the rooms:

```
long numFloorData;  ushort floor_data[numFloorData];
```

Every sector of a room carries a `FLOOR_INFO`:

```c
struct FLOOR_INFO {
    ushort index;       // offset into floor_data - 0 means NO floordata
    ushort fx : 4;
    ushort box : 11;
    ushort stopper : 1;
    uchar pit_room;  char floor;
    uchar sky_room;  char ceiling;
};
```

**`index == 0` means "nothing".** Offset 0 in the floordata block is
thereby unusable — the same pattern as with mesh index 0, only this time
meant that way by Core itself (`if (!floor->index) return;`,
`control.cpp:968`).

## 2. The chain

`floor_data[index]` is the start of a variable-length list. Per entry:

| Bits | Meaning |
|---|---|
| 0–4 (`& 0x1F`) | function, see the table |
| 5–14 | function-dependent payload (e.g. the height offset for the split types) |
| 15 (`& 0x8000`) | **last entry of the chain** |

Functions from `enum floor_types` (`types.h:322`) with the number of
**additionally** consumed words, read off `GetHeight`:

| # | Name | Extra words |
|---|---|---|
| 0 | FLOOR_TYPE | – |
| 1 | DOOR_TYPE (portal) | 1 |
| 2 | TILT_TYPE (floor tilt) | 1 |
| 3 | ROOF_TYPE (ceiling tilt) | 1 |
| **4** | **TRIGGER_TYPE** | **1 + a variable command list** |
| 5 | LAVA_TYPE | – |
| 6 | CLIMB_TYPE | – |
| 7–10 | SPLIT1…SPLIT4 | 1 |
| 11–18 | NOCOLF*/NOCOLC* | 1 |
| 19 | MONKEY_TYPE | – |
| 20 | TRIGTRIGGER_TYPE | – |
| 21 | MINER_TYPE | – |

## 3. The trigger entry

```
Word 0:  function 4 in bits 0-4, TRIGGER KIND in bits 8-13
Word 1:  timer (bits 0-7) | OneShot (bit 8) | code bits/mask (bits 9-13)
[Word 2: only for SWITCH, KEY and PICKUP - the item to operate resp. pick up]
Word n:  commands, until one has bit 15 set
```

**PICKUP belongs in that line.** `_TestTriggers` reads the word the same
way for all three kinds: `control.cpp:2251` (SWITCH), `:2266` (KEY),
`:2274` (PICKUP), each time `value = *(data++) & 0x3FF;`.

Exporter and FloorTool long checked only SWITCH and KEY. That did not
shift the chain — the word was simply printed as the **first command** —
and because a pickup reference always comes along as command kind 0 with
the index of its own item, it even looked like a plausible `Object`
command. It is not:

- It is no command but the trigger's reference to its item.
- When **rebuilding** a chain it would have counted as a deletable
  command. Whoever removes the command `Object 107` from a pickup trigger
  in truth removes the reference word and silently destroys the trigger.

The earlier Andrea1 attempt (removing the command `Object 26` from
trigger 118) only survived that because 26 ≠ 107. That was luck, not
correctness.

From `_TestTriggers`:
`type = (*data++ >> 8) & 0x3F;` then `flags = *data++; timer = flags & 0xff;`

**Trigger kinds** (`enum trigger_types`, `types.h:414`): TRIGGER, PAD,
SWITCH, KEY, PICKUP, HEAVY, ANTIPAD, COMBAT, DUMMY, ANTITRIGGER,
HEAVYSWITCH, HEAVYANTITRIGGER, MONKEY, SKELETON_T, TIGHTROPE_T,
CRAWLDUCK_T, CLIMB_T.

**Command word:** parameter in bits 0–9, command kind in bits 10–13, end
in bit 15.

**Command kinds** (`enum trigobj_types`, `types.h:435`): TO_OBJECT,
TO_CAMERA, TO_SINK, TO_FLIPMAP, TO_FLIPON, TO_FLIPOFF, TO_TARGET,
TO_FINISH, TO_CD, TO_FLIPEFFECT, TO_SECRET, TO_BODYBAG, TO_FLYBY,
TO_CUTSCENE.

**TO_CAMERA and TO_FLYBY consume an additional word.** Whoever forgets
that while walking reads the rest of the chain shifted — `GetHeight`
handles it explicitly at `control.cpp:1027`.

### Settled: the command kind has 4 bits

The engine proves it at two independent places:

- `_TestTriggers`: `switch ((trigger & 0x3FFF) >> 10)` — `control.cpp:2350`
- `GetHeight`: `(trigger & 0x3C00) != (TO_OBJECT << 10)` — `control.cpp:1025`

Bit 15 is the end bit, and **the engine does not evaluate bit 14 at
all**. The exporter read 5 bits (`(wCmd & 0x7C00) >> 10`) and would have
interpreted a set bit 14 differently than the game. Aligned to `0x3C00`
at both occurrences in `TRFile_RandomizerExport.cpp` and in the
FloorTool.

---

## 4. How changes are made: only append and repoint

The user's requirement, the same as with the items: **add and replace, no
remove.** Here that is not a restriction but the solution.

Floordata is a shared block that all sectors point into. Removing bytes
in the middle would shift every subsequent index — so **never edit inside
the existing chain**. Instead:

1. **Append the new chain** (old entries plus/minus the trigger) **to the
   end of `floor_data`**.
2. Set the affected sector's `FLOOR_INFO.index` to the new offset.
3. The old chain stays behind as dead ballast. It bothers nobody — no
   sector points at it anymore afterwards.

That way **not a single existing index shifts**. Removing a trigger from
a sector runs the same way: append a new chain without it, repoint, done
— nothing in the block gets deleted.

**Update the header along:** `numFloorData` at the block's start, the
main block's size (it stands twice before it, size and compressedSize) —
exactly as when appending items.

### Where in the chain a trigger may go

A trigger may **not** stand at an arbitrary spot. The interplay of
`GetHeight` and `_TestTriggers` dictates a fixed order:

`GetHeight` (`control.cpp:1014–1053`) remembers a pointer
`trigger_index` while walking the chain. It is set to the **first** entry
from {TRIGGER, CLIMB, MONKEY, TRIGTRIGGER}; LAVA even resets it
unconditionally. Portals, tilts and the SPLIT/NOCOL entries do **not**
set it — `GetHeight` simply eats those while walking.

`_TestTriggers` starts at this pointer and from there skips **exactly
four** entry kinds — LAVA, CLIMB, MONKEY, TRIGTRIGGER
(`control.cpp:2151–2195`) — and then in line 2197 **unconditionally**
reads a trigger head:

```c
type = (*data++ >> 8) & 0x3F;
```

From that follows the appending rule:

> **Appending at the end is allowed as long as the chain contains no
> LAVA, CLIMB, MONKEY, TRIGTRIGGER or TRIGGER entry.**

If one of these five is already inside, `trigger_index` points there, and
everything between it and the appended trigger — a portal, a tilt — gets
misread by `_TestTriggers` as a trigger head. A portal word read as a
trigger yields a random trigger kind with random commands.

Portals and tilts **alone**, by contrast, are harmless: they stand before
the pointer, not behind it. A sector with pure geometry floordata is thus
a valid target. The randomizer checks this in `canAppendTrigger` (`Tr5Level.cpp`). **2026-08-30:** this used to name a tool `FloorTool` and a function `darfAnhaengen()`; neither exists - the tool was never in the repository and the function was renamed when the project went English.

### There is enough room

`FLOOR_INFO.index` is a `ushort`, so at most 65535. Measured over all
thirteen levels:

| Level | Floordata | | Level | Floordata |
|---|---|---|---|---|
| Andrea1 | 5 345 | | joby3 | 1 717 |
| Andrea2 | 6 035 | | joby4 | 10 688 |
| Andrea3 | 4 771 | | joby5 | 1 809 |
| andy1 | 10 692 | | rich1 | 2 740 |
| **andy3** | **14 890** | | rich2 | 5 240 |
| Andy2 | 6 169 | | rich3 | 5 189 |
| joby2 | 8 476 | | | |

The largest sits at 23% of the limit. Even if we quadruple the floordata,
air remains. The limit still deserves a check before appending — a level
that tears it must be rejected, not silently written broken.

---

## 4a. What was counted

Over all thirteen levels (from the `level_*.json` exports):

| | |
|---|---|
| Triggers in total | 2 057 |
| of which wake an enemy | 269 |
| of which pickup/key triggers | **7** |
| Items stuck today | **7** |
| Items that pure enemy-detaching would free | **0** |

Distribution: Trigger 1366, HeavyTrigger 240, AntiTrigger 181, Switch 82,
Pad 53, Key 39, Skeleton 36, Dummy 26, **Pickup 11**, HeavyAntiTrigger
10, Monkey 5, Tightrope 4, HeavySwitch 2, Antipad 1, Climb 1.

**Conclusion:** for item freedom the trigger work is almost worthless —
it is about seven items, and in each of the seven cases something else
hangs on the trigger besides the enemy (a door, a flipmap, a cutscene).
The value lies with the **enemy pool** and with **adding** enemies: 269
triggers are the mass to work with.

## 4b. Rules for moving (the user's requirement)

### Decided on 2026-08-13: the items stay as curated

After the count had shown that of 2044 items only **two** would be freed
by trigger moving, the user closed the item branch:

> "Ok dann lass die Items so lassen wie wir die manuell kurratiert haben.
> Dann nutzen wir die Trigger nur für neue Gegner die wir dem Level
> hinzufügen." (Leave the items as we hand-curated them; we use the
> triggers only for new enemies added to the level.)

Therefore:

- The **general coupled move** of item and trigger is **not** pursued.
  The curation stays the yardstick.
- **One exception**, also the user's requirement: **Andrea1 key 107** may
  enter the pool, its pickup trigger travels along ("we only have to move
  the trigger with it"). See 4e.
- The actual purpose of the trigger work is **adding new enemies**. For
  that, no existing chain is touched — see 4f.

**Trigger and item stay together** wherever anything moves at all: the
trigger travels onto the new sector with it, one operation, not two. The
sector is new, the coupling remains.

**These triggers are NEVER moved**, because they expect room-specific
circumstances:

- **Flipmap** (all `TO_FLIPMAP`/`TO_FLIPON`/`TO_FLIPOFF`)
- **Antitrigger** (`ANTIPAD`, `ANTITRIGGER`, `HEAVYANTITRIGGER`)
- **Heavy** (`HEAVY`, `HEAVYSWITCH`, `HEAVYANTITRIGGER`)
- **Cutscene** (`TO_CUTSCENE`, and `TO_FLYBY` belongs with it in spirit)

Together that is over 400 of the 2057 triggers. They stay where they are,
and the item on them stays pinned.

**These items stay vanilla for now** (user decision):

> **Corrected 2026-08-30: nearly all of these have since been released.**
> Of the eleven, only rich1 item 27 (the CD) is still pinned, and joby3
> item 41 survives indirectly as typeId 172. Andrea2's valve 128 is
> explicitly retired in `$FIXED_ITEMS` with the reason that the placement
> does that job itself since #61. The authoritative list is
> `$FIXED_ITEMS` in `ZoneAnalysis.ps1`, not this table.

| Level | Items |
|---|---|
| Andrea2 | 71, 121, 128 |
| Andrea3 | 68 |
| andy3 | 6, 26 |
| joby3 | 41, 187 |
| rich1 | 14, 27 |

Looking them up revealed a pattern that explains the list: **almost all
carry a special OCB** — 121/128/68/26 have OCB 2, 6/41 have OCB 9,
rich1's Iris (14) has OCB 8, item 27 has OCB 6. Out of line are only
Andrea2 item 71 (an animating, not a pickup at all) and joby3 item 187
(Key6 on a cutscene trigger, so already covered by the global rule).

## 4b-bis. Bit 0x40: the condition without which no pickup trigger fires

A pickup trigger does **not** fire because it lies on the right sector.
It only fires if the collected item still exists at all afterwards — and
that is decided by a bit **above** the visible OCB.

`PickUpCollision`, `game/pickup.cpp:989`:

```c
if (!(item->trigger_flags & 0xC0))
    KillItem(item_number);
else
{
    item->item_flags[3] = 1;
    item->flags |= IFL_TRIGGERED;
    item->status = ITEM_INVISIBLE;
}
```

And `PickupTrigger`, `pickup.cpp:158`, demands exactly these two states:

```c
if (item->flags & IFL_CLEARBODY || item->status != ITEM_INVISIBLE ||
    item->item_flags[3] != 1 || item->trigger_flags & 128)
    return 0;
```

Without 0x40 (or 0x80) the item is simply killed on pickup, never reaches
`ITEM_INVISIBLE`, and `case PICKUP:` in `_TestTriggers` bails out with
`return` **before** a single command is read.

> **Mnemonic:** bit 0x40 is the pickup trigger's ticket. When an item
> travels, the bit must travel with it.

**Why this is hard to see:** every tool — trview, our JSON export,
`DumpItems` — displays the OCB masked to `0x3F`. Andrea1 item 107 stands
everywhere as "OCB 0"; raw it is `0x0040`. Whoever looks only at the
displayed OCB takes the bit for absent.

**Cross-check over all 13 levels:** all seven items that are place-bound
via a pickup trigger carry the bit — and only they:

| Level | Item | raw |
|---|---|---|
| Andrea1 | 107, 94 | `0x0040` |
| Andrea3 | 68 | `0x0042` (OCB 2 + bit) |
| andy1 | 8 | `0x0045` (OCB 5 + bit) |
| Andy2 | 29 | `0x0040` |
| rich1 | 14 | `0x0048` (OCB 8 + bit) |
| rich3 | 75 | `0x0044` (OCB 4 + bit) |

The bit is thereby also a **recognition mark**: a pickup with 0x40 has a
pickup trigger that does something.

Bit 0x80 next to it marks a regenerating pickup (`game/init.cpp:492`) and
counts the same for `& 0xC0` — which is why **both** bits must be
preserved when moving, not just one.

## 4b-ter. OneShot is no switch but a behavior change

Bit 8 of the setup word is called "OneShot" but does more than making the
trigger fire once. `_TestTriggers` passes it on to the woken item
(`control.cpp:2415`):

```c
if (flags & IFL_INVISIBLE)
    item->flags |= IFL_INVISIBLE;
```

And `IFL_INVISIBLE` (0x100) is, per `types.h:243`, expressly *"also used
as oneshot for everything else"* — the same bit carries both meanings.
Some enemies query it and then behave differently.

**Larson (and Pierre) are the case where this surfaced.** `pierre.cpp:68`:

```c
if (item->hit_points <= 40 && !(item->flags & IFL_INVISIBLE))
{
    item->hit_points = 40;
    pierre->flags++;      // -> :133 resets them to 60 and sends him away
}
```

Without the bit Larson is **not killable at all** — his hit points get
clamped at 40 and immediately reset to 60. Then he flees, and outside of
Trajan's Markets `pierre.cpp:341` deletes him on reaching his goal.

> **Mnemonic:** whoever places a new enemy into a level gives its trigger
> OneShot. For most it changes nothing; for Larson it decides whether he
> is an enemy at all.

Verified in game (Andrea1, Larson in room 2, a OneShot trigger three
sectors before): he stays, pursues Lara across room borders, goes down to
0 hit points.

### What else Larson demands

- **OCB 0.** At OCB != 0, `pierre.cpp:84` sets `ai_bits = GUARD`, and
  then he looks for AI objects that do not stand at a new spot.
- **His death fires a heavy trigger at his start position.**
  `pierre.cpp:118`; the position comes from `draw_room`/`item_flags[2]`,
  which `InitialiseCreature` (`box.cpp:30`) packs from the placement. If
  he lands on a sector with a heavy trigger, he fires it when dying.
- **Andrea2's Larson (item 76) must never be moved.** The death check
  follows his placement, but the cutscene trigger lies fixed on `72,2,2`
  — otherwise the death cutscene never happens.
- **His corpse used to remain standing.** His death animation (slot 45,
  relative 15, frames 281..488) carries seven `PlaySound` commands and no
  anim command 4 (`KillItem`) — the dog has it, Larson does not, because
  in vanilla his corpse is only cleaned up by the Andrea2 cutscene. Fixed
  in our engine: `pierre.cpp` sets `after_death = 1` on the last frame,
  the rest is done by `draw.cpp:742`. Recorded in `MODIFICATIONS.md`.

## 4c. What the OCB values really demand

Read completely off `PickUpCollision` (`game/pickup.cpp:728`):

| OCB | Behavior | movable? |
|---|---|---|
| 0 | ordinary pickup | yes |
| 1 | hidden pickup, `ANIM_HIDDENPICKUP` | no, needs a special environment |
| **2** | **crowbar**, `CrowbarPickUpBounds` | only with the wall filter |
| 3, 4, 7, 8 | plinth, `FindPlinth()` — without a plinth **silently uncollectable** | no |
| 5, 10 | never collectable (early return at the very top) | – |
| **6** | falls into the `default` branch, so an **ordinary pickup**, only animated | **yes** |
| **9** | "JobyCrow", `JobyCrowPickUpBounds`, its own animation | like 2 |
| 11 | coin glitter | yes |

**OCB 6 is harmless:** it stands in no `case` of the selection and lands
in the default branch; it only appears in the animation list
(`pickup.cpp:580`).

**With rich1's item 27, though, the OCB is not the reason at all.** The
CD carries `Invisible=true` in vanilla and lies outside the map; the
switch in room 15 activates it together with Door 4 and Door 2, and in
game it falls out of the suit as soon as its animation runs. A change of
place destroys the reveal. It therefore stands **permanently** in
`FIXED_ITEMS['rich1']` *and* in `VANILLA_TYPES['rich1']` (ZoneAnalysis.ps1)
— **not** "vanilla for now" but never in the pool. The wall filter
changes nothing about that.

The mnemonic from this: a special OCB is a *hint* that an item is
special, but not the proof — and its absence is no carte blanche.

## 4d. Wall filter for OCB 2 and 9

Both branches reset `item->pos.y_rot = roty` — the item's **own** angle —
before `TestLaraPosition` checks (`pickup.cpp:783` resp. `:872`). The
angle thus determines from which direction Lara must approach. Floating
freely it is collectable but looks wrong.

Releasing it needs **three things, two of which were still missing**:

> **Closed 2026-08-30.** Both gaps were filled when the wall-pickup work
> (#61) shipped: the patch format carries `angle` for moved and for added
> items and writes it into the item's yRot, and the placement picks the
> wall edge and the matching angle itself, while KEEP_OCB preserves OCB 2
> and 9. This section reads as an open to-do for a finished feature; it is
> kept because the reasoning is still the reasoning.

1. A **sector with a wall neighbor**. Data situation: present — the
   sector export carries `isWall` per sector, the four neighbors in the
   same room are checkable.
2. Setting the **angle to match the wall**. **Missing:** the level export
   delivered no item angle at all back then.
3. Being able to **write** the angle. **Missing:** the patch format knew
   no `angle`; a moved item deliberately kept its orientation.

## 4e. The one coupled move: Andrea1 key 107

Starting position, with the reference word read correctly:

```
192,1,2  [3:0003 0200][4:8404 kind=4 timer=0 once=0 mask=31 ref=107 c0:26 c0:108]
```

The sector carries two entries: a ROOF entry and the pickup trigger. The
trigger references item 107 (key, type 196, OCB 0) and additionally wakes
object 26 (dog, type 41) and object 108 (Flame Emitter2, type 122).

**Why moving preserves the behavior:** `PickupTrigger` (`pickup.cpp:152`)
checks exclusively the **item's state** — `ITEM_INVISIBLE`,
`item_flags[3]`, `trigger_flags & 128` — and then calls `KillItem`.
Nothing about that is place-bound. The trigger fires as soon as Lara
stands on its sector and the item was just collected. Since Lara stands
on the item's sector to collect it anyway, "move item and trigger
together" is behavior-equal to vanilla. The reference word stays valid
because the trigger addresses its item via the **index**, not via the
place.

**Proven** (`FloorTool move`, target 59,1,1 as a probe):

| Check | Result |
|---|---|
| Sectors changed | 2 of 6787 |
| Trigger at the target | word-identical, `ref=107 c0:26 c0:108` |
| Source sector | keeps the ROOF entry, `0003` → `8003` |
| Items | 151 vs 151, no difference |
| File size | +14 bytes (7 shorts) |

The line `0003` → `8003` is the point where such a rebuild tips over: the
remaining entry is now the **last** of the chain and needs bit 15.
Whoever copies the chain word by word instead of decomposing it into
entries forgets that.

**Target sector condition:** the rule from "Where in the chain a trigger
may go" applies. Simplest and safe without exception: allow only sectors
**without any floordata**. In Andrea1 those are 678 of the 971 walkable
sectors — more than enough for a single key.

**A side effect that remains:** the Flame Emitter2 still ignites at its
fixed place, just triggered at the new spot. That is accepted
deliberately, because the trigger travels as a whole.

## 4f. New enemies: the strictest variant suffices

For the actual purpose — **adding** enemies to the level — no existing
chain is touched at all. New enemy triggers were meant to go exclusively
onto sectors with `index == 0`: append a fresh chain at the end, repoint
the sector, done.

> **Corrected 2026-08-30.** The shipping rule is wider. A sector qualifies
> when its chain passes `canAppendTrigger` - no trigger, lava, climb,
> monkey or TrigTrigger entry in it. Sectors that already carry portals,
> tilts, roof entries or NOCOL/SPLIT entries ARE accepted: their chain is
> read out and rebuilt with the new trigger appended. Empty sectors are
> the simple case, not the only one.

That makes the check rule from section 5 trivial: **every sector with
`index != 0` must stay byte-identical.** No decomposing, no reassigning
of end bits, no reference word in play.

Room is plentiful — walkable sectors entirely without floordata:

| Level | walkable | of which without floordata |
|---|---|---|
| Andrea1 | 971 | 678 |
| Andrea2 | 1031 | 730 |
| Andrea3 | 671 | 298 |
| andy1 | 600 | 178 |
| Andy2 | 652 | 401 |
| andy3 | 822 | 198 |
| joby2 | 1718 | 1335 |
| joby3 | 307 | 226 |
| joby4 | 1210 | 461 |
| joby5 | 257 | 190 |
| rich1 | 405 | 215 |
| rich2 | 592 | 325 |
| rich3 | 808 | 408 |
| **Total** | **10044** | **5643** |

Even joby5 at the bottom has 190 free places. So the restriction costs
nothing.

**Placement** (the user's requirement): from the enemy, walk the flood
path backwards about 3 sectors. By construction that guarantees
reachability, the right zone and that Lara crosses the trigger — and
keeps the needed distance: if the trigger lies on the enemy's sector,
the enemy spawns into Lara.

## 4g. How a new enemy enters the level

The path has stood end to end since 2026-08-13. Four stations:

**1. Curation** — `$ENEMY_TYPES` in `ZoneAnalysis.ps1`, per level a list
of typeIds with a reason. It was deliberately empty until an enemy had
proven in game that it works without a level script. Lands in the zone
export as `enemyTypes`.

> **2026-08-30: it has not been empty for a long time.** Essentially every
> campaign level carries reasoned entries now - dog and Larson in Andrea1,
> Pierre and the Roman god in Andrea2 and Andrea3, the Imp in the andy
> levels, the Maze Monster in Andy2, guard and husky in joby2, and so on.
> Read the table itself; it is the record.

**2. Approach sectors** — also `ZoneAnalysis.ps1`. For every item-capable
sector, up to three sectors are provided that Lara steps on shortly
before it on her **first-access path**, about three steps, best choice
first.

The source is `$firstParent` — the tree recording via which edge a sector
was reached for the **first** time. That is the way Lara really takes,
not some way through the finished level with all doors open. The approach
sector is thereby derived, not guessed.

Measured: 889 of 961 sectors in Andrea1 have one, comparable across all
levels (joby4 brings up the rear with 260 of 394).

**3. Placement** — `Rando::run`. An enemy may go onto a free sector whose
approach sector is also free **and** tolerates a trigger
(`Patch::markTriggerSafeSectors` reads that from the real floordata,
because the level-info export knows lava and climb but not monkey and
trigtrigger). The enemy comes as `itemAdds`, its trigger as
`triggerAdds`.

**4. Writing** — `Tr5Level::addTrigger`, purely additive.

**5. Flipmap copy (#147)** — every waker is written a second time, into the
flipmap partner of its room. `FlipMap()` (`game/control.cpp:2634`) swaps the
two `ROOM_INFO` structs whole and then hands the base room its own item list
back: the **items stay**, the **floordata is exchanged**. A trigger that
lives in only one of the two variants is therefore dead for as long as the
flip lasts — and a flip need never be turned back. joby5 is the case that
found this: its intro cutscene (script `PLAYCUT 32` → `joby8`,
`deltapak.cpp:1802/1852`) fires the `FLIPMAP(0)` heavy trigger in room 40
before Lara can move, no `FLIPOFF` exists anywhere in the level, and the zone
export names base rooms only — so nine of twenty-two wakers of a measured run
sat in rooms whose floordata had already been swapped away. The copy is
refused, and noted, when the partner tile is a wall, lies more than one
sector height away, starts at another world corner, or already carries
floordata (`canAppendTrigger`). Only entries carrying `mirrorFlip` are
copied; the curated triggers of the secret lists are not.

**The rules kept in the process:**

| Rule | Why |
|---|---|
| Never a trigger on the enemy's sector | otherwise the enemy spawns into Lara |
| The trigger on the approach path | otherwise he never wakes up |
| The trigger in **both** flip variants | after a flip the base room reads the partner's floordata (#147) |
| OneShot set | see 4b-ter — for Larson it decides over life and death |
| OCB 0 | otherwise `ai_bits = GUARD` and he looks for AI objects that are not there |
| At most 512 items | `MAX_ITEMS` (doubled from 256 - see Tomb5 Limits\Limits.txt); rich2 sits at 248 |

**Measured** (Andrea1, seed 4711, 6 enemies): 6 of 6 have their trigger,
all on a different sector than the enemy. Floordata: 11 rows affected — 3
sectors already had a chain (a tilt, a ROOF, the key's source sector) and
were rebuilt, 4 were added fresh. Items 151 → 157, file +202 bytes.

## 5. Check rule: the others' indices must stay in place

The user's requirement, and the actual danger of the whole thing: **if
the list shifts, everything is broken** — and silently at that, because a
wrong index simply reads a different chain.

From that follows a check that must run after **every** floordata change:

1. Before: remember index and decoded chain for **every** sector of every
   room.
2. After: the same.
3. Compare: **every sector we did not deliberately repoint must have the
   same index and the same chain.** A single deviating entry is a reason
   to abort, not a warning.

That is the same discipline that applies to the item indices, and the
same with which the weapon imports were secured: do not look at the
result, look at what must **not** have changed.

A tool for this was still missing — it was the first thing to build,
**before** the first write attempt.
