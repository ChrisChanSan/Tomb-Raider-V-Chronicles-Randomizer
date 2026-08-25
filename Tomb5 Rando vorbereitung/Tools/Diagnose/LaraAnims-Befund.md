# Lara's interaction animations: which level really carries which

Measured on 2026-08-21 with `LaraAnim` over all 13 playable levels.
Occasion: the keycard animation stood frozen for 22 seconds in joby3, a
switch in andy3 played a far too short movement, and rich1's crawl roll
moved nothing at all.

## The basic problem

Lara's anim table has the same length in **every** level — so the slot for
an animation exists everywhere. The **frame data**, however, is
level-specific. Where a level never needs a movement, Core leaves the
entry as a stub.

Two stub forms, both measurable:

| Form | Marker | Effect in game |
|---|---|---|
| 1 | `FrameSize 0` | The engine computes `frame_ptr[n * 0]` → always the same picture. A frozen pose. |
| 2 | `frame_base == frame_end` | An empty frame range, nothing to play. |

Plus a third case that is no stub but goes just as wrong: the frame size
is right, yet between the own frame pointer and the next larger one lies
**less space than the keyframes need** — then the data there belongs to a
different animation.

`LaraAnim` checks all three:

```
keyframes = (frame_end - frame_base) / rate + 1
needed    = keyframes * FrameSize * 2 bytes
available = next larger frame pointer of any anim - own one
```

Several anims may share **one** pointer when they have the same
`frame_base` — then they are variants of the same movement block (andy3:
468/469/470 all start on frame 8190). That is normal.

A shared pointer does **not automatically mean "all fine"** though. It
can also mean a real owner lies there and the others merely point at it
with `FrameSize 0` — exactly rich1's case (218–220 pointed at 221's
block). The two cases can only be told apart via the frame size, not via
the pointer.

## Result: not a single level carries all of them

Richest donors: **joby4** and **andy1**. Poorest: **rich1, rich2, rich3**
— of all things, the outfit donors.

Excerpt of the donor table (which levels really carry the anim):

| Anim | Name | Levels with real data |
|---|---|---|
| 63/64 | SWITCHOFF / SWITCHON | 8 resp. 7 |
| 131 | USEKEY | 11 |
| 134 | USEPUZZLE | **13 (all)** |
| 135 | PICKUP | **13 (all)** |
| 218 | DUCKROLL (crawl roll) | **12 — rich1 is the only one missing** |
| 316 | BLOCKSWITCH | 3 (andy1, Andy2, Andy3) |
| 319/320 | TURNSWITCH C/A | **2** (joby4, Andy3) |
| 324 | RAILSWITCH | **1 (only andy1)** |
| 412 | TWODOOR | 2 (andy1, Andrea1) |
| 420 | CROWSWITCH | **1 (only andy1)** |
| 421 | CRAWLJUMP | **13 (all)** |
| 437 | KEYCARD | 4 (joby4, rich1, joby5, joby2) |
| 470 | AIRLOCKSW | 7 |

**No** level carries `WATERSWITCH` (415) and `WATERDOOR` (416) — they are
unused in TR5.

**Careful with joby4:** it stands in almost every row, but itself has
stubs of form 2 (`AIRLOCKSW`: FrameSize 10 but frames 0..0). The check
catches that — a donor table that only asks "slot present" does not.

## What the randomizer makes of this

`ApplyPatch` has a post-pass (after all object changes):

1. It derives the anim demand from the objects in the **finished** level
   — switches including their OCB, keyholes and puzzle holes, trapdoors,
   pushable blocks.
2. Added are the movements that hang on no object but on Lara's controls
   and therefore must **always** be present: the crawl roll (218) and the
   crawl jump (421).
3. Each of them is checked with `Tr5Level::laraAnimUsable` and, when
   needed, fetched via `copyLaraAnimFrom` from the first donor that
   **really** carries it — verified on the loaded donor, not believed.
4. **The demand is expanded along the chain** before anything is fetched
   — see below. Self-reference (222 → 222) ends the walk.

A vanilla pass over all levels thereby repairs nine animations on its
own.

## Animations are chains — and Core stubs them as a whole

A movement is rarely one animation. `next_animation` (offset +28 in the
40-byte entry) attaches the next one, and Core leaves the **whole chain**
in place when stubbing. The crawl roll in rich1:

| Anim | Frame pointer | Rate | continues with |
|---|---|---|---|
| 218 | 103896 | 5 | 219 |
| 219 | 103896 | 5 | 220 |
| 220 | 103896 | 5 | 222 |
| 221 | 103896 | 4 | 11 |

All four on **one** pointer. 221 is the real owner (size 37, needs 518 of
the 592 bytes), 218–220 are stubs with size 0.

Repairing only 218 therefore does not suffice: Lara starts the roll and
falls back into the crawl pose after six frames, because 219 sends her
back onto the still picture. Seen exactly like that in game.

The post-pass therefore first expands the demand via `laraAnimNext`
before fetching anything. After the run:

| Anim | Frame pointer | Rate |
|---|---|---|
| 218 | 410112 | 2 |
| 219 | 410408 | 2 |
| 220 | 411032 | 2 |
| 221 | 103896 | 4 (untouched, was never broken) |

The sound commands come along and are rebased onto the target base:
joby4's PlaySound on frame 5500 with base 5497 lands in rich1 on 4278
with base 4275 — the same distance.

**Cross-check:** `AnimKmd <level.trc> 0` shows frame pointer, rate,
successor, state changes and all commands per animation. When two
neighboring anims show the same `frameOfs`, immediately check the size
with `LaraAnim -anim <no>`.

## The OCB picks Lara's movement

For switches the animation hangs on the OCB (`switch.cpp:246ff`):

| OCB | Lara's animation |
|---|---|
| 0 | SWITCHON (64) — the classic wall lever |
| 1–2 | HIDDENPICKUP (325) |
| 3 | SMALLSWITCH (413) |
| 4 | GENERATORSW (196) |
| 5–7 | ONEHANDPUSHSW (197) |
| from 8 | AIRLOCKSW (470) |

A rebuild with the wrong OCB plays the wrong movement. If it is too
short, Lara is done before the **object** is — she leaves the sector and
**the trigger never takes hold**. Exactly that happened in andy3, where
the vanilla switch carries OCB 8.

## Usage

```
LaraAnim <level.trc> [level.trc ...]      overview: what is missing
LaraAnim -all <level.trc>                 every anim individually
LaraAnim -anim <no> <level.trc> [...]     one anim in level comparison
```

`AnimProbe <level.trc> <animNo>` instead asks the library
(`Tr5Level::laraAnimUsable`) — the cross-check when tool and randomizer
disagree.
