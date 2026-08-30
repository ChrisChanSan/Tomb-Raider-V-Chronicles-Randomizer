<p align="center">
  <img src="banner.jpg" alt="PCTomb5" width="760">
</p>

# The engine — the manual

**PCTomb5.exe, Beta_1.0**

A native Windows 11 build of Tomb Raider Chronicles: Direct3D 9 and XAudio2,
no wrappers, one self-contained executable. This page covers everything it
does that the original did not, and everything you can set.

The randomizer has its own manual — [Randomizer-Manual.md](Randomizer-Manual.md).

- [What this build is](#what-this-build-is)
- [Starting it, and the setup dialog](#starting-it-and-the-setup-dialog)
- [Controls](#controls)
- [The options menu](#the-options-menu)
- [tomb5 options](#tomb5-options)
- [First person view](#first-person-view)
- [Language, and HQ music](#language-and-hq-music)
- [Balance.txt — tuning the whole game](#balancetxt--tuning-the-whole-game)
- [The secret counters](#the-secret-counters)
- [Secret rewards](#secret-rewards)
- [Savegames](#savegames)
- [What plays differently from the original](#what-plays-differently-from-the-original)
- [When something looks wrong](#when-something-looks-wrong)
- [Licence, source, and building it yourself](#licence-source-and-building-it-yourself)

---

## What this build is

Upstream TOMB5 — Troye's decompilation of Chronicles — ships `PCTomb5.exe`
plus a `tomb5.dll` that carries the game code. **This build is a single
executable.** There is no DLL to copy, upstream's "dll only" updates do not
apply to it, and because it links the C runtime statically it needs no Visual
C++ redistributable installed.

The rendering is Direct3D 9 and the sound is XAudio2, both native. In
practice that means no compatibility shims, no wrapper, and no fiddling with
DirectPlay or old DirectX installers.

**You supply the game.** No levels, script, sounds, music, videos, textures or
language files ship here. Put `PCTomb5.exe` into your own Chronicles
installation.

---

## Starting it, and the setup dialog

Run the game normally, or use the two helpers in the package:

| | |
|---|---|
| **`Setup.bat`** | opens the setup dialog (`PCTomb5.exe -setup`) |
| **`Log.bat`** | starts the game writing a log (`PCTomb5.exe -log`) |

The setup dialog also opens by itself the very first time the game is run.

<p align="center"><img src="setup-dialog.jpg" alt="The setup dialog" width="670"></p>

**Output Settings** — *Graphics Adapter* (one entry per monitor), the renderer
named underneath, *Resolution*, and *Fullscreen* / *Windowed*.
**Render Options** — *Bilinear*, *Disable FMV*, *Low Quality Textures*.
**Sound** — *Disable*.
The bottom left corner reads `PCtomb5 build <date>`. **Quote that line in a bug
report.**

> **The dialog does not load your previous choices.** It opens at its defaults
> every time: Windowed, Bilinear on, FMVs on, sound on, 640×480. If you open
> it again just to change the resolution and press OK, you have also just
> reset all of those. Set everything you want in one pass.

Three keys work while the game runs, with no menu: **Alt+Enter** switches
fullscreen and windowed, **Alt+Plus** and **Alt+Minus** step the resolution.
They are ignored during FMVs and level loading — that looks like a dead key
and is not.

"Fullscreen" is a borderless window rather than an exclusive mode, so
Alt-Tab is instant and overlays behave.

### Where your settings live

In the registry, under `HKEY_CURRENT_USER\Software\Core Design\Tomb Raider
Chronicles`.

> **The main Options page is written only when the game closes normally.**
> Volumes, sound quality, targeting, language, control method and every key
> and pad binding are saved on exit. If the game crashes or you kill it from
> the Task Manager, that session's changes are lost. **Leave through "Exit"
> after rebinding your keys.**
>
> The tomb5 option pages are the exception — they write themselves the moment
> you change a row.

---

## Controls

**Options → "Control Configuration".** The top row is *Control Method* with
five positions: *Keyboard*, *PS5 Controller*, *Xbox Controller*, *Joystick*
and *Reset*.

The three controller positions mostly mean the same thing to the game — they
choose the **vocabulary** the buttons are named with, and with it the default
layout that *Reset* writes back. The engine works out by
itself whether your pad reports as XInput or DirectInput, so a DualSense
behind Steam still gets PS5 names.

With a controller page selected the list changes to fifteen bindable rows,
including *Pause*, *Save*, *Load* and the two medipacks — vanilla had no
joystick binding screen at all. The four right-stick tilts can be bound like
buttons.

A few things worth knowing:

- **Confirm and back (Cross/A and Circle/B) are fixed.** Whatever you bind
  them to, they still work as menu confirm and back.
- **Binding a button another row uses silently unbinds that row** — it shows
  `---`.
- On the keyboard page, **Enter and the arrow keys are refused** as bindings,
  and every action keeps responding to its *original* default key alongside
  your new one, unless that key has meanwhile gone to another action.
- **Save and Load are unbound by default** on the Xbox page. Bind them
  yourself if you want them.

> **Launching through Steam: switch Steam Input off for the game first.**
> Steam's own store entry lists Chronicles as having no controller support, so
> Steam translates your pad into keyboard presses — while this engine reads
> the same pad directly. Every press then arrives twice. Nothing on our side
> can prevent that.

---

## The options menu

Reached from the title screen row *Options*, or from the pause screen while
playing. The two are identical except for one row: **Special Features** only
appears from the title screen.

| Row | What it does | Default |
|---|---|---|
| Control Configuration | see above | — |
| Music Volume | 0–100, applied live | 80 |
| SFX Volume | 0–100; Lara's breath plays back as you drag | 90 |
| Sound Quality | Low / Medium / High = 11 / 22 / 44 kHz, switched live | Medium |
| Targeting | Automatic or Manual | Automatic |
| Language | switches all game text, no restart | automatic |
| Special Features | the bonus gallery (title screen only) | all locked |
| tomb5 options | the engine's own pages, below | — |
| Credits | who this is built on | — |

A Special Features page unlocks when you have found nine secrets in the
matching quarter of the campaign.

**Credits** is a screen of its own, not one of the tomb5 pages — as a page it
kept turning up unasked while paging through settings, with the paging arrows
drawn across the names. It lists names only; the web addresses live in
`README.txt`, because the game font cannot draw every character.

---

## tomb5 options

<p align="center"><img src="features-1.jpg" alt="The tomb5 options page" width="700"></p>

Two pages the original never had, plus a third when first person is on. The
row of two arrows at the bottom steps forward; there is no going back a page,
you cycle round.

**Page 1** — *Footprints* (on), *Shadow mode* (Original blob / Circle / Faded
circle / PSX — default Faded circle), *Fix climb up delay* (on — removes the
pause before Lara pulls herself up), *Flexible crawling* (on), *Cutscene
skipper* (off), *Cheats* (off), *Bar mode* (Original / TR4 / PSX — default
PSX), *Bar positions* (Original), *Enemy bars* (off), *Ammo counter* (off),
*Gameover menu* (on), *Fog* (on).

**Page 2** — *Crawl Tilting* (on), *PSX skies* (on), *TR4 loadscreens* (on),
*Loadbar style* (TR4), *Mono screen style* (Original), *Loading text* (on),
*Shimmer* (on), *Distance fog* (12 — how many blocks away the world fades,
3–30), *Ammotype hotkeys* (on), *Look transparency* (off), *Static lighting*
(on), *UW Effect* (Original), *First person view* (off), *Level select* (off).

Worth spelling out:

- **Cheats** governs DOZY only — type it while playing to fly. The classic
  cheats still need the compass in hand and are unaffected.
- **Ammotype hotkeys**: the option governs **only** the ammunition cycling -
  pressing a weapon's number again while it is already out. Drawing a weapon
  and the medipacks work whether it is on or off.

  **One key per weapon, the same key in every level:** 1 pistols,
  2 shotgun, 3 uzis, 4 revolver, 5 HK, 6 crossbow (the Grappling Gun in
  rich3). 0 uses a small medipack, 9 a large one.

  Core bound only four keys and let two of them change meaning at the 13th
  Floor - key 1 became the HK, key 2 the crossbow. That works while the
  campaign runs in its built order, because by then Lara no longer carries
  the early weapons. A randomized run breaks it twice over: the switch hangs
  on `gfLevelId`, which is the level's identity and not its position, so
  with a rolled order it fires in scattered levels rather than "later"; and
  because the weapons travel too, Lara can carry an HK in the first level,
  where no key would have drawn it.
- **Level select** adds a fifth row to the title screen that jumps straight to
  any level. It is a development shortcut, hidden by default.
- **Loading text** only shows while *Loadbar style* is TR4.

---

## First person view

<p align="center"><img src="ingame-08.jpg" alt="First person, aiming the HK" width="700"></p>

Switch it on under *tomb5 options*, page 2. A **third page** then appears with
four rows of its own:

| Row | What it does | Default |
|---|---|---|
| Field of view | 60 to 100 in steps of 10; the ends stop, they do not wrap | 90 |
| Death camera | First person, or pull back and show it | Third person |
| Follow head bone | nails the view to the head bone — much more physical, and much shakier | off |
| Camera damping | off / Light / Medium / Strong | Light |

The page only exists while the view is on, so a settings page for something
switched off cannot turn up unasked. Turning the view off while standing on
that page drops you back to page 1.

While first person is on, Lara's head and braid are not drawn, close sounds
are centred instead of panned, and the camera stays behind her eyes where
vanilla would have torn it away — trapdoors, turn switches and trigger
cameras all now leave you where you are.

**Two things deliberately still switch to third person**: the binoculars and
the laser sight, which bring their own head camera, and the real sniper camera
in the 13th Floor. That is not the option failing.

---

## Language, and HQ music

**Options → Language** switches all game text immediately, with no restart —
vanilla took the first file it found and never offered a choice.

Only files in the true TR5 PC text layout are accepted; the game checks the
file's **header**, not its name. `JAPAN.DAT` and `DUTCH.DAT` ship with every
edition of TR5 and are in fact Tomb Raider 4 files, so they are skipped
silently, and some Steam-edition language files are rejected the same way.

> If Left/Right on that row does nothing, only one usable file is present. The
> language files must sit **next to `PCTomb5.exe`**, and so must `SCRIPT.DAT`
> — never inside `data\`.

### HQ soundtracks

Put OGG files named after the track number into the folder for your language:

```
audio\HQ EN\103.OGG
audio\HQ DE\   audio\HQ FR\   audio\HQ ES\   audio\HQ IT\   audio\HQ JP\
```

The engine then plays them instead of the original WAV. **Nothing ships** —
these come from your own TR4-6 Remaster purchase, as 44.1 kHz stereo. With
the folders empty, absolutely nothing changes.

The set follows the **Language** row: German text pulls HQ DE, and so on;
English, English US and Dutch all use HQ EN. A missing track falls back to
HQ EN and then to the original WAV, so a half-filled folder is fine.

> The folder shipped as `HQ JP WIP` is not read — the engine looks for
> `audio\HQ JP`. Rename it when you have the files.

Changing the language mid-game also changes the music set, but only from the
next track that starts.

---

## Balance.txt — tuning the whole game

<p align="center"><img src="engine-settings.jpg" alt="Balance.txt and SecretRewards.txt" width="760"></p>

> **Neither file is shipped, and neither exists until the game has run
> once.** The engine writes both beside `PCTomb5.exe` on its first start -
> not when the package is unpacked, and not when the randomizer runs. That
> is deliberate: the engine takes its defaults from these files, so a
> shipped copy would pin today's numbers to every installation for good and
> no later correction to the balancing would ever reach a player.
>
> All three orders end up the same, which is why it is safe: with no file
> the engine writes a complete one; with the randomizer's three-line stub it
> applies those three and completes the rest (`if (missing)
> BalanceWriteFile(0)`, balance.cpp:334); and with a full file the
> randomizer edits only its three percentage lines and leaves every hand
> tuned value alone (BalanceFile.h).

A plain text file beside `PCTomb5.exe`, created on the first start and never
touched again unless something is missing from it. Roughly eighty settings in
six blocks, and **it documents itself**: every line prints its own default in
a comment behind the value, and where the default differs from the number
the file's own table calls vanilla, that one is printed too. Careful: that
vanilla figure is what `setup.cpp` assigns, which is not always what Core
shipped - and where the two agree, no note is printed at all, so Two Gun
shows nothing even though Core gave him 24.

Format is `Name = number`, one per line; `;` or `#` starts a comment. Names
are not case-sensitive.

> **Changes take effect on the next game start.** Not on a new level, not on a
> reload. Save the file, quit the game completely, start it again.

**For scale: Lara has 1000 hit points and one pistol bullet deals 1.**

| Block | What is in it |
|---|---|
| Damage of Lara's weapons | nine lines. The revolver and the HK have two each — the revolver is weak up to Colosseum and strong from the submarine on, and the HK hits harder aimed from a standstill than fired on the move. `Crossbow` is the Grappling Gun. Shotgun damage counts **per pellet**. |
| Enemy hit points | 31 lines, `HP_<enemy>`, absolute as in the original |
| Enemy damage against Lara | per attack |
| Level hazards | the ceiling gun and its kind |
| Everything at once | `WeaponDamage`, `EnemyHealth`, `EnemyDamage` — percentages applied **on top** of everything above |
| Sound | `SceneryRange` |

**`SceneryRange`** is 65 where the original is 100: machines, fire and dripping
water used to carry through half a level and drown out an enemy walking up
behind you. Set it back to 100 for the original soundscape. Only stationary
scenery is affected — enemies and Lara always keep their full range.

**The three percentage lines** are what the randomizer's difficulty sliders
write, and it touches nothing else in the file. `EnemyHealth` reaches further
than the `HP_` list — it scales every thinking enemy, including those with no
line of their own. Traps, falls, fire, poison and drowning are not tunable at
all.

To undo hand-tuned values, **delete the file** — the next start writes a fresh
one.

---

## The secret counters

**Pause → Statistics** now shows the level's own count in brackets behind the
campaign total:

```
Secrets Found            0 / 36   (0 / 3)
```

<p align="center"><img src="features-3.jpg" alt="The statistics screen" width="700"></p>

The first pair is the whole campaign, as it always was; the pair in brackets
is the level you are in. Where a level holds no secrets the bracket is left
off, which is also what keeps it off the title screen.

Nobody knows by heart how many roses each level was built with, and with a
rolled level order even a player who does cannot tell which level they are in
from the position in the sequence.

The bracket is dropped wherever the table says zero, which today means only
the title screen. Two levels really hold no roses but are entered in the
table as three - `richcut2` and the dev level `del` - so the bracket would
read `(0 / 3)` there. Core's table has always been wrong about those two, and
the same table decides when the all-secrets bonus fires; correcting it would
make the bonus fire the moment such a level starts. It is left alone
deliberately.

## Secret rewards

Finding every secret in a level now awards items, the way Tomb Raider II did
it. The original Chronicles gave nothing at all.

What each level awards is read from `SecretRewards.txt` beside the executable,
one line per level, and you can edit it by hand. The defaults are one large
medipack, two small ones, and a pack each of revolver and uzi ammunition.

Only weapons, ammunition and medipacks are allowed. Handing out a key or a
puzzle item could open a door out of order — anything else you write in is
refused **by name in the log**, not silently ignored.

> With a rolled level order, a line names a **position** in the sequence, not
> a file. "Colosseum = …" belongs to whatever level is played in that slot.

---

## Savegames

They live in a `savegame\` folder next to the executable. Any `savegame.N`
still lying loose in the game folder is moved in there once, at startup, and
never overwrites a file already there.

A save written before the save format grew is **recognised and refused** with
a clear message rather than loaded into a mismatch.

There was also a bug worth knowing about, because it is in the game you
already own: saving during the key-insert animation lost the keyhole's state.
The key was gone from the inventory, the keyhole came back "unused", and the
door could never be opened again. If a door has ever refused to open after a
reload, that was probably why. Fixed here.

---

## What plays differently from the original

These are deliberate. A randomized run walks into situations Core never had to
think about, and each of these is one of them.

**Three enemies can now be killed.** The chef, the skeletons and the swamp
witch had hit points and took damage, but nothing ever checked whether they
had run out. In the original it never showed: the chef's level takes Lara's
weapons away first, the skeletons' level hands her nothing that hurts them,
and the swamp witch only wakes when the level is as good as over.

**Some deaths burst apart instead of playing an animation.** The skeletons,
the swamp witch, the Imp and the Maze Monster have no usable death animation
at all, so they shatter into their own meshes the way the Roman God does. The
chef borrows the soldiers' gunshot death — his skeleton is bone-for-bone
identical with theirs.

**Two hit-point figures moved.** The Maze Monster drops from 4000 to 90 — he
was built as an unkillable chase set piece and has to be able to die as a pool
enemy. (`setup.cpp` still assigns 120; `balance.cpp` overwrites it at every
level start, so 90 is what you fight.) That leaves him below the mini-sub,
the Roman God and the T-Rex. Two Gun rises from 24
to 90.

**Pushable blocks roll over pickups again.** TR5 refused the push if anything
at all stood on the target tile — a single small medipack was enough. TR1–4
had no such test. Enemies, statues and a second block still stop it.

**A wall pickup dropped by an enemy becomes an ordinary one.** Some items are
pulled off a wall at a particular angle; dropped on open floor that can never
happen again and the item would be lost for good.

**Corpses and models behave.** Larson no longer stands dead forever outside
Trajan's Markets. Larson and Pierre only flee where the level actually
contains the marker they run to — without one they used to become immortal.
Enemies no longer switch to a low-detail distance model at range, because the
engine took whatever sat in the next model slot, which in a foreign level is
something else entirely. And a mesh swap a cutscene performs on Lara is undone
when it ends or is skipped, so you do not keep playing as catsuit Lara in a
knitted beanie.

**Room for bigger levels.** Several engine ceilings were raised — items per
level, sound samples, active enemies, the savegame buffer. A level that would
still exceed one stops with a clear message instead of corrupting memory.

---

## When something looks wrong

**Start with `Log.bat`.** It runs `PCTomb5.exe -log` and writes `log.txt` next
to the executable, from the very first startup line.

The log is not one stream — each line begins with the name of the seam that
wrote it, and each seam answers a different question:

| What you see | What to look for |
|---|---|
| the wrong sound plays | `SFXLOG` — which sample really played, and which animation command asked for it |
| an enemy is invisible | `SLOTLOG`, `SKIPLOG`, `MESHLOG`, `CLIPLOG` — four separate reasons: no free AI slot, the control skipped it, no mesh, clipped away |
| …and if it was clipped | `CLIPDETAIL` — the actual window and the camera it was measured against |
| an item is in the wrong place, or vanishes | `ITEMLOG` — slot, room, OCB and flags per item at load |
| …and something moved it later | `ITEMWATCH` — who wrote which room number, and when |
| the outfit looks wrong, a limb is misplaced | `SKINLOG` — the skin's bone stack |
| the first person view sits wrong | `FPHEAD`, `ALTERFOV` — the head bone per frame, and the field of view |

These seams are permanent, not debug leftovers. They exist so that a report
carries something to work with.

**For a complete report, attach four things:** `log.txt`, `spoiler.txt` from
beside the randomizer, the `PCtomb5 build <date>` line from the setup dialog,
and what you were doing.

**Naming things correctly:** the package holds `trview\types.json`. Copy it to
`%LOCALAPPDATA%\trview\` and the level viewer will name the slots this build
uses — otherwise it shows "Crow" where a vulture stands, and a clear report
turns into a confusing one.

### Working as intended

- **The setup dialog forgot my settings.** It always opens at its defaults —
  see above.
- **The Language row does nothing.** Only one usable language file is present.
- **A dinosaur or a guest outfit is missing.** The Tomb Raider III paths are
  not set in the randomizer, or the vulture is missing its second game.
- **An enemy burst instead of dying.** Deliberate — see above.
- **My controller does everything twice.** Steam Input. Switch it off for the
  game.

---

## Licence, source, and building it yourself

TOMB5 is under the **GNU General Public License version 3**, and so is this
modified version and the randomizer. That is why the complete source ships
inside the package rather than only living on a website: you can rebuild the
executable yourself and do not have to take anyone's word for what is in it.

Every changed file carries a modification notice, and
`Source\TOMB5 engine\MODIFICATIONS.md` is the dated record the licence asks
for — written file by file, with the reasoning.

The control code of the four Tomb Raider III guests is not this project's
either: it comes from **tomb3**, Troye's Tomb Raider III decompilation, is also
GPLv3, and was taken over as-is rather than rewritten so its origin stays
visible. The libraries compiled in keep their own notices — Dear ImGui, zlib,
stb_vorbis, discord-rpc. All of them are named in `THIRD-PARTY.md`, and every
licence text is in `Licenses\`.

**No Tomb Raider game data ships with any of it.** The game belongs to its
rights holders; you supply your own copy.

### Building the engine

Open `Source\TOMB5 engine\tomb5.sln` in Visual Studio with the C++ workload.
Configuration **Release**, platform **Win32 (x86)**. The result is
`release\PCtomb5.exe`.

> **Pick Win32.** The x64 configurations are leftovers from the original
> project and build a DLL, not a game.

Or start from upstream at
[github.com/Trxyebeep/TOMB5](https://github.com/Trxyebeep/TOMB5) and apply the
changes on top — the unmodified decompilation is the first commit of this
repository, so a single `git diff` against it is the complete change set.
