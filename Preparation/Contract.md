# What the engine and the randomizer owe each other

Written 2026-08-30. The two live in separate trees, `TOMB5 Code` and
`TR5Randomizer`, and they are coupled in eight places. **Nothing
enforces any of it.** Each coupling is described where its own half happens
to sit, so changing one side and forgetting the other compiles cleanly, ships
cleanly, and breaks in game.

This page is the list. It is short on purpose; the point is that it exists.

## The couplings

**Object slots beyond the file boundary**
`SLOT_FILE_BOUNDARY` in `game/objects.h` marks where the level file's own
slots end and this project's added ones begin (460 BLUE_GUARD2, 461
ROMAN_GOD2, 469 CYBORG, and the sound slots above them). The randomizer
places into those numbers. Add a slot on one side only and the other places
into nothing.
*Both sides:* `game/objects.h` and `kCross` / `enemyKinds` in
`core/Randomizer.cpp`.

**The mesh-swap fallback slots**
`MESHSWAP_CROSS_*` in `game/objects.h` name the SMASH_OBJECT slots the engine
uses as fallback mesh warehouses; the randomizer fills them. The ids must be
the same on both sides, and the document that got this wrong for weeks said
3..6 while the header said 5..8. Since #146 that is 401-404, 462-468 and 470;
the full assignment is written down in
`Preparation\Enemies\Cross Enemy Pool.txt`.
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

**The level header block - four bytes, four meanings**
The 32 bytes between the texture blocks and the main block are `short
data[16]` in `specific\file.cpp`, and the engine reads them before it loads a
single room. Four of the sixteen words are in use; all seventeen shipped
`.trc` files carry 0 in the rest, measured.

| word | written by | read by | meaning |
|---|---|---|---|
| `data[0]` | `Tr5Level::setLaraType` | `LaraDrawType = data[0] + LARA_NORMAL` | the outfit byte, and with it the braid handling in `hair.cpp` / `laraskin.cpp` |
| `data[1]` | `setWeatherType` | `WeatherType`, read only by `DoWeather` | 0 dry, 1 rain, 2 snow (#144) |
| `data[2]` | `setForeignTalkHeads` | `GfForeignSpeechHeads` | 1 = the outfit donor has no `LARA_SPEECH_HEAD1-4`, skip the cutscene head swap |
| `data[3]` | `setHdOutfit`, value from `Patch::hdOutfitId` | `HDLaraOutfitFor`'s `tr3Outfit` argument (#123, stage S7-2) | which Tomb Raider III outfit the wardrobe rolled |

`data[3]` carries the engine's own `HD_LARA_OUTFIT_*` numbers so that neither
side translates: 0 none, 5 `TR3_ANTARC`, 6 `TR3_NEVADA`, 7 `TR3_COAST`, 8
`TR3_CATSUIT`. It exists because `data[0]` cannot say it - the wardrobe
writes `LARA_PONYTAIL` both for the Rome body in a Russia level and for every
guest, and nothing else in the file tells the two apart. **If the enum in
`specific\hd\hd_lara.h` is ever renumbered, `Patch::hdOutfitId` in
`core/ApplyPatch.cpp` has to be renumbered with it.**
*Both sides:* `core/Tr5Level.cpp` (the accessors) and `specific/file.cpp`.

**The `.hdm` sidecar beside every randomized level**
What does not fit in four bytes: which rooms the run changed the SHAPE of,
and which object slots hold a model out of another level file. Both make the
Remaster's HD geometry and its name table wrong for this level, and both are
unbounded, so they go into `data\<LEVEL>.hdm` - plain text, first line
`hdm 1`. Written by `Patch::writeHdMarkers` on every run for every level,
even when empty; a missing file means "no markers". The grammar and the
reference reader are `Tools\Diagnose\HdmDump.cpp`.
*Both sides:* `core/ApplyPatch.cpp` and the engine's reader (stage S7-2).

**The sound and animation assumptions**
The soldier family's animation base (guard 460) is chosen in the engine's
`setup.cpp` and relied on by the randomizer's import, which must bring the
base type along. Change the fallback chain on one side and imported soldiers
go invisible - not crash, invisible, while still dealing damage.

## The habit that would catch all of it

When you change one of the eight, grep the other tree for the symbol before you
build. Every one of these is a single name, and every one of them is greppable.
