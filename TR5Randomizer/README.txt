TR5 Randomizer
==============

What is in here
---------------
  TR5Randomizer.exe   The tool. This is where you set the seed.
  PCTomb5.exe         The game engine, native for Windows 11.
  data\               Precomputed data the randomizer needs.
  Source\             The complete source code of both.
  Licenses\           The licenses.
  trview\             Slot names for the trview level viewer (optional).

You additionally need your own installation of Tomb Raider
Chronicles. Game data is deliberately NOT included here - the game
belongs to its rights holders.

Getting started
---------------
  1. Start TR5Randomizer.exe.
  2. On first start it asks for the folder of your Chronicles
     installation. On the first randomize it creates data\Backup and
     saves your original levels there. After that, randomization
     writes directly into data - so you start the game as usual.
  3. Enter a seed or roll one, randomize, play.

The seed
--------
The same seed always produces the same game - on any machine. If
something looks odd to you, note down the seed and the build ID; with
those, exactly your game can be rebuilt. Both are shown in the pause
menu, so a screenshot is enough.

Any input is a valid seed. "Christmas" works.

Tuning the difficulty yourself
------------------------------
On first start the game creates a file Balance.txt next to
PCtomb5.exe. It lists every weapon individually, every enemy with its
hit points, every single attack with its damage and every built-in
level hazard. At the very bottom stand two percentage lines that make
all enemies harder or softer in one stroke without touching the fine
tuning above.

The file explains itself - every line names its original value.
Change, save, restart the game. Whoever deletes it gets a fresh one
on the next start.

The three percentage lines can also be set in the randomizer:
"Weapon damage" under Items, "Enemy health" and "Enemy damage" under
Enemies. The sliders write only these three lines - whatever else you
set by hand in the file stays in place. The "Reset balance" button
sets all three back to 1.00x.

Next to each slider sits a "Random" checkbox. With it the seed rolls
the value, in steps of five between 0.50x and 2.00x - manual and
rolled can be mixed freely. The same seed always gives the same
values. Weapon damage is never rolled below 0.50x; 0.00x exists only
by hand.

Traps, falls, fire, poison and drowning always stay as hard as in the
original, even if you set the enemies to harmless.


Playing with a controller
-------------------------
The engine reads a controller itself, and the buttons can be bound in
the options like the keys.

If you start the game through Steam, turn Steam Input OFF for it
first. Steam's database still lists Tomb Raider Chronicles as a game
without controller support - which was true of the engine from 2000,
but is not true of this one. Believing that, Steam translates your
controller into KEYBOARD presses using a community layout, while the
engine reads the same controller directly at the same time. Every
press then arrives twice by two different routes, and the controls
behave as if possessed.

Turning it off:

    Steam library -> the game -> "View controller settings"
    under CONTROLLER -> "Disable Steam Input"

After that the engine sees your controller as it is, and everything
works normally. Nothing needs changing when starting the game
directly - Steam Input only ever runs when Steam launches the game.


Looking at a level yourself
---------------------------
The folder trview holds a types.json for trview, the level viewer by
chreden (https://github.com/chreden/trview). Copy it to

    %LOCALAPPDATA%\trview\types.json

and restart trview; it merges the file over its built-in list at
startup.

It only affects names. Ten slots would otherwise read wrongly or not
at all: seven are unknown to trview, because its TR5 list ends before
them, and three carry different contents after randomizing - the four
guests from Tomb Raider III take over slots that held a SAS man, a
crow, a sailor and a reaper.

Nothing here is needed to play. It is worth having if you ever report
something odd: a viewer that shows "Crow" where a vulture stands turns
a clear report into a confusing one.


If something goes wrong
-----------------------
The randomizer has "Restore original". It plays all levels back from
data\Backup. Please do not delete that folder - it is the only copy
of your original levels the program knows about.

A note on savegames: the save format was changed compared to the
original. Old savegames are rejected with a message instead of
breaking silently - so finish a running playthrough before you switch
to a new version.

If you want to report something, start the game once with -log:

    PCTomb5.exe -log

It then writes a log.txt next to itself that records what the engine
loaded and what it played. Send that along with the seed and it can be
looked at without guessing. Without the switch nothing is written at
all - no file, no slowdown - so leave it off for normal play.

Origin and license
------------------
The engine is a modified version of TOMB5, the decompilation project
for PC Tomb Raider Chronicles by Troye (Trxyebeep) and contributors:

    https://github.com/Trxyebeep/TOMB5

Credits of the original project: Troye (lead programmer and project
lead), ChocolateFan, thatguyfrombb and Raildex. Our work rests on
theirs.

The dinosaurs and the vulture are not ours either: their control code
comes from tomb3, Troye's decompilation of Tomb Raider III, and is
GPLv3 just like everything else here:

    https://github.com/Trxyebeep/tomb3

The models and sounds behind them are NOT included - the randomizer
reads them from your own Tomb Raider III installation at runtime.

TOMB5 is under the GNU GPL v3, and so is this modified version. That
is why the complete source code is included - you can rebuild the EXE
yourself and do not have to take our word for it. What we changed and
why is in Source\TOMB5\MODIFICATIONS.md.

The randomizer itself is also under the GNU GPL v3. Its source code
lies in Source\TR5Randomizer, the license in
Licenses\TOMB5-GPLv3.txt - it is the same one.

Contributors - testing, findings and ideas, the people who played the
seeds and reported what broke:

    XeNoN3560, Lucifer230, Todesengel1931, Crockett, Grylle, Claude AI

The randomizer's interface uses Dear ImGui by Omar Cornut (MIT
license).

The engine can play music in the OGG format (for HQ soundtracks you
provide yourself - we ship none). The decoder for that is
stb_vorbis by Sean Barrett and contributors (public domain):

    https://github.com/nothings/stb

The curation of the levels - that is, the question of where an item
may lie - would not have been possible without trview. Every zone
boundary in this randomizer was verified with it:

    https://github.com/chreden/trview
