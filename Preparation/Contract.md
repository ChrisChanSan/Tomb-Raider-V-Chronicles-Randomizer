# What the engine and the randomizer owe each other

Written 2026-08-30. The two live in separate trees, `TOMB5 Code` and
`TR5Randomizer`, and they are coupled in half a dozen places. **Nothing
enforces any of it.** Each coupling is described where its own half happens
to sit, so changing one side and forgetting the other compiles cleanly, ships
cleanly, and breaks in game.

This page is the list. It is short on purpose; the point is that it exists.

## The couplings

**Object slots beyond the file boundary**
`SLOT_FILE_BOUNDARY` in `game/objects.h` marks where the level file's own
slots end and this project's added ones begin (460 BLUE_GUARD2, 461
ROMAN_GOD2, and the sound slots above them). The randomizer places into those
numbers. Add a slot on one side only and the other places into nothing.
*Both sides:* `game/objects.h` and `kCross` / `enemyKinds` in
`core/Randomizer.cpp`.

**The mesh-swap fallback slots**
`MESHSWAP_CROSS_*` in `game/objects.h` name the SMASH_OBJECT slots the engine
uses as fallback mesh warehouses; the randomizer fills them. The ids must be
the same on both sides, and the document that got this wrong for weeks said
3..6 while the header said 5..8.
*Both sides:* `game/objects.h` and the swap columns in `core/Randomizer.cpp`.

**The version string**
`RANDO_VERSION_STRING` in `core/BuildId.h` and the two Windows version
resources - `TR5Randomizer.rc` and `tomb5.rc`. Three places, kept in step by
hand. A mismatch is harmless to run and confusing to a bug report.

**The build id - the one that is NOT enforced, though it says it is**
`core/BuildId.h` states that the engine carries the same constant and that
the randomizer refuses to work on a mismatch. It does not:
`grep -r RANDO_BUILD_ID "TOMB5 Code"` finds nothing. The check was never
built. The id is still bumped and still lands in `spoiler.txt`, so a report
can be read - it just cannot be rejected. **Worth building.**

**The data the randomizer writes and the engine reads**
`Balance.txt`, `SecretRewards.txt` and the savegame folder are a format
agreed between the two trees. `BalanceCheck` and `BalanceWriteCheck` in
`Tools\Diagnose` exist precisely to test both ends of the first of them.

**The sound and animation assumptions**
The soldier family's animation base (guard 460) is chosen in the engine's
`setup.cpp` and relied on by the randomizer's import, which must bring the
base type along. Change the fallback chain on one side and imported soldiers
go invisible - not crash, invisible, while still dealing damage.

## The habit that would catch all of it

When you change one of the six, grep the other tree for the symbol before you
build. Every one of these is a single name, and every one of them is greppable.
