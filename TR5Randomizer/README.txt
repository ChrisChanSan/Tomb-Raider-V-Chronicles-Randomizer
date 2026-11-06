TR5 Randomizer  Beta_5.0
========================

What is in here
---------------
  TR5Randomizer.exe   The tool. This is where you set the seed.
  docs\               The two manuals with their pictures, and the
                      picture behind the tool's window (Background.jpg);
                      without it the window is plain.
  PCTomb5.exe         The game engine, a native 64-bit program; it
                      needs 64-bit Windows 10 or 11.
  vcruntime140.dll, vcruntime140_1.dll, msvcp140.dll
                      Microsoft's C++ runtime, which both programs
                      need; nothing has to be installed.
  avutil-61.dll, avcodec-63.dll, avformat-63.dll
                      FFmpeg, which plays the films. Keep all six DLLs
                      next to the two EXEs.
  Setup.bat           Opens the engine's setup dialog: graphics
                      adapter, resolution, window or fullscreen.
  Log.bat             Starts the game writing a logfile - use this if
                      you want to report a bug.
  audio\, movie\      Where your HQ soundtracks and FMVs go; each
                      folder has a note in it.
  screens\            Where your own remaster's loading screens go;
                      there is a note in it.
  savegame\           Savegames live here, not loose in the folder.
  data\               Precomputed data the randomizer needs: the zone
                      files, the checksum lists, the secret lists. The
                      program carries no copy of them; if one is
                      missing, it says which and where it looked.
  Source\             The complete source code: TOMB5 engine\ is the
                      engine, TR5Randomizer\ the tool.
  Licenses\           The licenses.
  trview\             Slot names for the trview level viewer (optional).
  SHA256SUMS.txt      The SHA-256 of every program and DLL in here.

You additionally need your own installation of Tomb Raider
Chronicles. Game data is deliberately NOT included here - the game
belongs to its rights holders.

This is an unofficial, non-commercial fan project. It is not
affiliated with, authorised by or endorsed by Core Design, Eidos,
Crystal Dynamics, Aspyr, Saber Interactive or Embracer. Tomb Raider,
Tomb Raider Chronicles and Tomb Raider IV-VI Remastered belong to
their rights holders. Nothing of theirs is included here and nothing
is ever copied out of their installations: for the HD art, the HQ
sound bank and the HD loading screens you need your own copy of the
Remaster, and the engine reads it where it lies while the game runs.

Getting started
---------------
  1. Start TR5Randomizer.exe.
  2. On first start it asks for the folder of your Chronicles
     installation. On the first randomize it creates data\Backup and
     saves your original levels there. After that, randomization
     writes directly into data - so you start the game as usual.
  3. Enter a seed or roll one, randomize, play.

If your antivirus flags the randomizer
--------------------------------------
The programs are built from the source code in Source\ and are not
signed; some scanners' machine-learning heuristics flag new unsigned
programs. If a file's SHA-256 matches SHA256SUMS.txt of the release,
it is the one built from that source: please report it to the
scanner's maker as a false positive.

The seed
--------
The same seed always produces the same game - on any machine. If
something looks odd to you, note down the seed and the build ID; with
those, exactly your game can be rebuilt. Both stand in the randomizer
window, and every run writes spoiler.txt beside the program with the
seed, every setting and one line per level. That file is what a bug
report should carry - the game itself shows neither.

Any input is a valid seed. "Christmas" works.

The share code under the seed field carries the seed text and every
setting of the window in one line. "Copy" puts it on the clipboard;
someone who pastes it into the field below and presses "Apply" gets
your exact settings, and with them your game. Folders, window size
and language stay on each machine. A code only works on the build
that made it.

Tuning the difficulty yourself
------------------------------
IF YOU LOOK FOR Balance.txt AND SecretRewards.txt AND THEY ARE NOT
THERE: that is correct. Neither is shipped. The game writes both
itself, next to PCtomb5.exe, the first time you start it - not when you
unpack the package, and not when you run the randomizer. They are
deliberately not delivered, because the game takes its defaults from
them: a file shipped with the package would pin today's numbers to your
installation for good, and later corrections would never reach you.

Balance.txt lists every weapon individually, every enemy with its
hit points, every single attack with its damage and every built-in
level hazard. Below them stand three percentage lines that make
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


Better sound and music, from your own remaster
----------------------------------------------
If you own the Tomb Raider 4-6 Remaster, three things from it can be
used here. Nothing of it is included - the engine reads your own
files where they lie, and copies nothing out of them.

Sound effects: there is nothing to copy. The engine reads MAIN.SFX
out of your installed remaster (it sits there in 5\DATA) where it
lies. If you put a copy of that file in the audio folder, under the
same name, that copy is used instead.

The original stores its sounds as 22 kHz ADPCM inside every level
file; those of your remaster are 44.1 kHz, and they are not the old ones
upsampled - there really is more in them. About nine sounds in ten
are swapped. The rest keep
their original version on purpose: enemies the randomizer moved in
from other levels, and the guests from Tomb Raider III, have no
counterpart in your remaster and keep the sounds they arrived with.

Music: the HQ soundtracks go into audio\HQ EN, audio\HQ DE and so on,
named after the track number. There is a note in the audio folder.

Loading screens and logos: copy your own remaster's DDS pictures from
5\PIX\HD (ANDREA1 to RICH3, TITLE, and LOGO_DE, LOGO_EU, LOGO_FR,
LOGO_US) into screens\HD. The game then shows them while a level
loads, builds the logo screen from TITLE plus the logo of your
language, and draws that logo in the title menu - so the German and
French titles get their own wordmark, which the original never
shipped. Security Breach borrows RICH3. The logo follows the
language you set in the options after the next restart. There is a
note in the screens folder.


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

It only affects names. Fourteen slots would otherwise read wrongly or
not at all, for four different reasons:

  - Four (31, 43, 53, 75) carry different contents after randomizing:
    the guests from Tomb Raider III take over slots that held a SAS
    man, a crow, a sailor and a reaper. trview names them confidently
    and wrongly.
  - Two (47, 55) are ordinary Chronicles slots trview has no entry
    for at all.
  - Six (460, 461, 469, 471, 472, 473) lie beyond the original 460 and
    exist in no vanilla level, so trview's list ends before them - its
    last TR5 id is 452.
  - Two (85, 61) trview does know but cannot tell apart: it calls both
    41 and 85 "Dog" although only one is the husky, and "Soldier
    Statue" is not enough when two Roman gods can stand in one
    level.

Nothing here is needed to play. It is worth having if you ever report
something odd: a viewer that shows "Crow" where a vulture stands turns
a clear report into a confusing one.


If something goes wrong
-----------------------
The randomizer has "Restore original". It plays all levels back from
data\Backup. Please do not delete that folder - it is the only copy
of your original levels the program knows about.

A note on savegames: this version writes savegame format 3. Saves
of Beta 5.0 and of every earlier version are refused with a message
box, and the game closes - so move your old saves aside, or finish a
running playthrough, before you switch to this version.

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
If they come out wrong, check your files against
data\Tr3Checksums.csv: it lists the size and SHA256 of exactly the
Tomb Raider III files the four guests were ported from.

TOMB5 is under the GNU GPL v3, and so is this modified version. That
is why the complete source code is included - you can rebuild the EXE
yourself and do not have to take our word for it. What we changed and
why is in Source\TOMB5 engine\MODIFICATIONS.md.

The randomizer itself is also under the GNU GPL v3. Its source code
lies in Source\TR5Randomizer, the license in
Licenses\GPLv3.txt - it is the same one.

Contributors - testing, findings and ideas, the people who played the
seeds and reported what broke:

    XeNoN3560, Lucifer230, Todesengel1931, Crockett, Grylle


MADE WITH AI

This project was built with Claude (Anthropic) as a coding partner, and
that is too large a part of the work to hide in a credits line. A great
deal of the code, most of the analysis tools and both manuals were
written in conversation with it; every commit from 2026-08-15 on carries a
Co-Authored-By trailer and the ones before it do not - the work was the
same, the trailer was not yet being written, so the history says exactly where.

What that does not change: the design decisions and the call on what
shipped were ChrisChanSan's, and nothing went into a release that had
not been run in the actual game first.

The randomizer's interface uses Dear ImGui by Omar Cornut (MIT
license).

The films are played by FFmpeg, by the FFmpeg developers (LGPL v2.1
or later, https://ffmpeg.org): the three DLLs beside the engine,
loaded at run time and not compiled in. The licence is
Licenses\COPYING.LGPLv2.1; Licenses\THIRD-PARTY.md names the source
snapshot they were built from, which lies beside the release as
FFmpeg-master.zip.

The engine can play music in the OGG format (for HQ soundtracks you
provide yourself - we ship none). The decoder for that is
stb_vorbis by Sean Barrett and contributors
(public domain or MIT, your choice):

    https://github.com/nothings/stb

The curation of the levels - that is, the question of where an item
may lie - would not have been possible without trview. Every zone
boundary in this randomizer was verified with it:

    https://github.com/chreden/trview
