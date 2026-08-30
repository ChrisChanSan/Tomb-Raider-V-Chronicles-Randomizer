<p align="center">
  <img src="docs/banner.jpg" alt="Tomb Raider Chronicles Randomizer" width="760">
</p>

# TR5 Randomizer

**Beta_1.0**

A randomizer for **Tomb Raider Chronicles** (PC) — including a build of the
game engine that runs natively on Windows 11 (D3D9 + XAudio2, no wrappers).
Game data is **not** included — bring your own installation. Everything is
GPLv3.

## Manuals

- **[The engine](docs/Engine-Manual.md)** - every option, every file it
  reads, what plays differently from the original, and what to do when
  something looks wrong.
- **[The randomizer](docs/Randomizer-Manual.md)** - the window section by
  section, seeds, and what actually changes in a randomized game.

## At a glance

| | |
|---|---|
| **Items and keys** | Rolled per level within verified zones — every boundary was checked in trview, so a key never lands where it cannot be reached |
| **Enemies** | Swapped among themselves, or drawn from the whole game; extra enemies on request. Guests from Tomb Raider III (T-Rex, raptor, compy, vulture) come along |
| **Outfits** | Lara's three Chronicles outfits, plus outfits imported from Tomb Raider III — pick what may be rolled in the wardrobe |
| **Secrets** | Placed in a glitchless or a glitched set, or rolled from a pool |
| **Music** | Ambience and triggers permuted; HQ soundtracks in OGG are played if you supply them |
| **Level order** | Optional, from shuffling inside a section up to a free run through the whole game |
| **Difficulty** | Every weapon, enemy and hazard tunable by hand in `Balance.txt`, or by three sliders |
| **Engine** | Native Direct3D 9 and XAudio2, controller support, a first-person view, and a long list of fixed original bugs |

The same **seed** produces the same game on any machine. The randomizer
window shows the version and the build id, and every run writes
`spoiler.txt` beside the program with the seed, every setting and one line
per level — that file is what makes a run reproducible, and what a bug
report should carry.

<p align="center">
  <img src="docs/gui-main.jpg" alt="The randomizer window" width="860">
</p>

<table>
<tr>
<td width="50%"><img src="docs/gui-outfits-enemies.jpg" alt="Wardrobe and enemy pool"><br>
Pick what the wardrobe may draw, and which enemies belong in the pool.</td>
<td width="50%"><img src="docs/gui-settings.jpg" alt="Settings"><br>
Every switch has a tooltip that says what it does and what it costs.</td>
</tr>
</table>

## Quick start

1. **[Download the release](../../releases)** and unpack it into your Tomb
   Raider Chronicles folder — the one holding `PCTomb5.exe`.
2. Start **`TR5Randomizer.exe`**. On the first start it asks for that
   folder; either the game folder or its `data` folder is accepted.
3. Press **Randomize**. Your originals are backed up to `data\Backup`
   first, and every later run starts from that backup — so re-rolling as
   often as you like is safe.
4. Start the game with `PCTomb5.exe`.

Nothing else is required. Every option is optional; **Restore original**
puts the levels back. The two manuals above cover the rest.

## What this is

Two programs working together:

- **TR5Randomizer** rolls a new game out of your Chronicles installation:
  items, keys and weapons travel, enemies swap roles and journey between
  levels (up to guests from Tomb Raider III), outfits, music, secrets and,
  on request, the level order. The same seed produces the same game on any
  machine.
- **PCTomb5** is a modified version of [TOMB5](https://github.com/Trxyebeep/TOMB5),
  the decompilation project for the PC engine: Direct3D 9 and XAudio2
  instead of DirectDraw/DirectSound, plus the extensions the randomizer
  needs — and a few extras such as a switchable first-person view.

Both are under the **GNU GPL v3**; the complete source code lives here.

### In the game

<table>
<tr>
<td width="50%"><img src="docs/features-1.jpg" alt="New options page"><br>
An options page of its own: PSX skies, crawl tilting, distance fog, the
loading bar and screen styles, ammo hotkeys, static lighting, level select.</td>
<td width="50%"><img src="docs/features-2.jpg" alt="First person view options"><br>
The first-person view, with field of view, a death camera, the head-bone
look and how hard the camera is damped.</td>
</tr>
<tr>
<td><img src="docs/gui-language.jpg" alt="Language switching"><br>
The language can be changed while the game runs.</td>
<td><img src="docs/gui-credits.jpg" alt="Credits"><br>
Credits for everyone whose work is in here.</td>
</tr>
</table>

<details>
<summary><b>More from a rolled run</b> (11 screenshots)</summary>

<table>
<tr><td width="50%"><img src="docs/ingame-01.jpg" alt=""></td><td width="50%"><img src="docs/ingame-02.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-03.jpg" alt=""></td><td width="50%"><img src="docs/ingame-04.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-05.jpg" alt=""></td><td width="50%"><img src="docs/ingame-06.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-07.jpg" alt=""></td><td width="50%"><img src="docs/ingame-08.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-09.jpg" alt=""></td><td width="50%"><img src="docs/ingame-10.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-11.jpg" alt=""></td><td></td></tr>
</table>

</details>

## What you need

Your own installation of **Tomb Raider Chronicles**. Game data is
deliberately **not** part of this project — neither levels nor music,
videos or language files. The game belongs to its rights holders; the
randomizer patches your own files at runtime and backs everything up to
`data\Backup` first.

## Building

- **Engine:** `TOMB5 Code\tomb5.vcxproj` with Visual Studio (Win32, Release).
- **Randomizer:** `TR5Randomizer\build\b1.bat` (command line) and
  `b2.bat` (GUI) — both run relative to the repo; Visual Studio with the
  C++ workload is required.
- **Package:** `Preparation\Tools\BuildRelease.ps1` assembles
  the shippable folder including source code and licenses.

## Thanks and origin

- **[TOMB5](https://github.com/Trxyebeep/TOMB5)** by Troye (Trxyebeep),
  ChocolateFan, thatguyfrombb and Raildex — without this decompilation
  there would be nothing here.
- **[tomb3](https://github.com/Trxyebeep/tomb3)** by Troye (Trxyebeep) and
  contributors — the T-Rex, raptor, compy and vulture control code comes
  from there (GPLv3, taken over directly rather than rewritten, so its
  origin stays visible). Their models and sounds are not shipped; the
  randomizer reads them from your own Tomb Raider III installation.
- **[trview](https://github.com/chreden/trview)** by chreden — the tool
  every single zone boundary of this randomizer was verified with.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut (MIT) —
  the interface.
- **stb_vorbis / stb_image** by Sean Barrett (public domain) — HQ music and
  the background image.
- **[zlib](https://zlib.net/)** by Jean-loup Gailly and Mark Adler — the
  level files are compressed with it.
- **[discord-rpc](https://github.com/discord/discord-rpc)** by Discord, Inc.
  (MIT) — inherited from upstream and linked into the engine.

Every licence text is in `Licenses\` in the release, and
[THIRD-PARTY.md](THIRD-PARTY.md) says which is which.

**Contributors** — testing, findings and ideas, the people who played the
seeds and reported what broke: XeNoN3560, Lucifer230, Todesengel1931,
Crockett, Grylle.

## Made with AI

This project was built with **[Claude](https://www.anthropic.com/claude)**
(Anthropic) as a coding partner, and that is too large a part of the work to
hide in a credits line. A great deal of the code, most of the analysis tools
and both manuals were written in conversation with it. Every commit in this
repository carries a `Co-Authored-By: Claude` trailer, so the history says
exactly where.

What that does not change: the design decisions, the curated lists and the
call on what shipped were ChrisChanSan's, and nothing went into a release
that had not been run in the actual game first. An AI is fast and it is
confident, which is a bad combination — more than one bug in the changelog
was put there by it and found by playing, not by reading. So the code was
reviewed and it was tested, by a human, in Tomb Raider Chronicles. Judge it
the way you would judge any other code: by whether it works.

What was changed in the engine and why is recorded in
`TOMB5 Code\MODIFICATIONS.md`; the player manual ships with the release
package as `README.txt`.
