<p align="center">
  <img src="docs/banner.jpg" alt="Tomb Raider Chronicles Randomizer" width="760">
</p>

# TR5 Randomizer

**Beta_5.0**

A randomizer for **Tomb Raider Chronicles** (PC) — including a build of the
game engine that runs natively on Windows 11 (D3D9 + XAudio2, no wrappers).
Game data is **not** included — bring your own installation. Everything is
GPLv3.

> **An unofficial, non-commercial fan project.** It is not affiliated with,
> authorised by or endorsed by Core Design, Eidos, Crystal Dynamics, Aspyr,
> Saber Interactive or Embracer. *Tomb Raider*, *Tomb Raider Chronicles* and
> *Tomb Raider IV-VI Remastered* belong to their rights holders. Nothing of
> theirs is included here and nothing is ever copied out of their
> installations: you need your own copy of Chronicles, and — for the HD art,
> the HQ sound bank and the HD loading screens — your own copy of the
> Remaster. The screenshots on this page show the original game's own levels,
> and where HD is on, art that the engine reads at run time out of the
> author's own Remaster installation.

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
| **Sound** | With your own Tomb Raider IV-VI Remastered installed, the game plays its sound effects at 44.1 kHz — measurably wider, not upsampled. A `MAIN.SFX` you put into `audio\` yourself is used before it |
| **Loading screens** | Your own Remaster's pictures in `screens\HD\` make the loading screens run at 1920x1080 instead of 640x480, while *HD textures* is on |
| **HD art** | With your own Remaster installed, the levels, Lara and the pickups can be drawn with its HD models and textures — read where they lie, never copied, never shipped. They are loaded with every level whenever the Remaster is found, and *Options -> Graphics -> HD textures* or F12 switches the world and the interface between them and the classic look at any moment; the game starts classic until you switch it once. Which rooms an HD frame draws is the visibility path by default (registry `hd_vis` 1, the HD art of every room in sight drawn whole); `hd_vis` 0 keeps the classic portal walk. An installation an earlier build set up may hold the `hd_vis` 0 that build wrote; the first start moves it to 1 once (writing `hd_vis_rev` beside it), and a 0 set afterwards stays - see the [engine manual](docs/Engine-Manual.md) |
| **HD water** | In an HD level most pools, canals and stretches of sea (every one with an underwater room below) get the engine's own moving water surface - waves, reflection, a clear or murky depth, foam in patches along the bank and where a waterfall lands, streaks on a fast river, a wet line round whatever stands in it but Lara and the creatures swimming in it - in place of the water art your installation has over it, tunable per body of water in `hd_water.txt` (`art keep` brings that art back). It is part of the HD look: the F12 switch to the classic look brings back the level's own water faces |
| **Reflections** | In an HD level the polished floors of Rome, the VCI building and the title mirror the room over them - every floor height and slope in view at its own height, each tread of a stair, the ramps and the corridors going up, and ANDREA1's church mosaic; a sliver showing less than a thousandth of the screen stays plain and leaves without a fade. *Options -> Graphics -> Reflections* turns the mirror off at once, for a machine that cannot carry it; the floors stay solid until the next level load. See the [engine manual](docs/Engine-Manual.md) |
| **60 frames a second** | The default. The game still thinks thirty times a second and what is drawn in between is interpolated, so nothing about the physics changes. *Options -> Graphics -> Display -> Frame rate* offers 30, 60 and the display's own refresh |
| **Level order** | Optional, from shuffling inside a section up to a free run through the whole game |
| **Weather** | Rolled per level — the rainy Ireland levels can come up dry, the Russian base can get rain instead of snow |
| **Difficulty** | Every weapon, enemy and hazard tunable by hand in `Balance.txt`, or by three sliders |
| **Engine** | Native Direct3D 9 and XAudio2, controller support with an optional modern (analog-stick) control scheme — a free orbit camera on the right stick, matching swim controls — a first-person view, an optional modern keyboard scheme with its own rebindable W/S/A/D-style default keys, a Combat mode that can hold the draw key instead of toggling it, an Action row that splits Interact and the Secondary (climb/grab/shoot) button onto two keys with the pad's own second bindings page, Sprint mode, Duck mode and Grab mode (let go of a ledge without holding the key) as Toggle instead of Hold, an optional Ledge hang move that turns a run or walk at an edge into an automatic hang (Modern controls only, off by default), rebindable hotkeys for the medipacks, quick save/load, the six weapons, (with Cheats on) five cheat actions and an *HD / SD* switch (F12, for comparing the HD and the classic draw at one spot) — on the keyboard as F-keys, on a pad as chords behind a Modifier button — and a long list of fixed original bugs |

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
<td width="50%"><img src="docs/engine-settings.jpg" alt="Balance.txt and SecretRewards.txt"><br>
Two plain text files beside the game: every enemy’s hit points and every
weapon’s damage, and what each level awards for all its secrets.</td>
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

## If your antivirus flags the randomizer

The programs are built from the source in this repository and are not signed,
and some scanners' machine-learning heuristics flag new unsigned programs.
Compare the file's SHA-256 with `SHA256SUMS.txt` of the release
(`certutil -hashfile TR5Randomizer.exe SHA256` in a command prompt prints it).
If it matches, the file is the one built here: please report it to the
scanner's maker as a false positive.

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

Everything the engine adds sits under **Options**, in the pause menu and on
the title screen, as a tree of tabs: **Audio**, **Controls**, **Graphics**,
**HUD**, **tomb5 options**, **Special Features** and **Credits** — with
sub-pages of their own, *Key bindings*, *View* and *Modes* under Controls and
*Display* under Graphics. The original's own rows are where they always were;
nothing was taken away.

<table>
<tr>
<td width="50%"><img src="docs/menu-options.jpg" alt="The Options menu"><br>
The seven tabs. <i>Special Features</i> opens from the title screen only;
anywhere else it is drawn in red, as here, and the cursor steps over it.</td>
<td width="50%"><img src="docs/menu-tomb5-options.jpg" alt="tomb5 options"><br>
<b>tomb5 options</b>: the cutscene skipper, cheats, level select, and the
three movement fixes — climb-up delay, flexible crawling, crawl tilting.</td>
</tr>
<tr>
<td width="50%"><img src="docs/menu-graphics.jpg" alt="Graphics"><br>
<b>Graphics</b>, over two pages, the first shown here: <i>Display</i> on
top, then HD build, HD textures with Body physics under it, Reflections, Fog,
Distance fog and Shimmer. The arrows at the bottom turn to the second page and
its seven rows.</td>
<td width="50%"><img src="docs/menu-display.jpg" alt="Display"><br>
<b>Display</b>, under Graphics: the frame rate — 30, 60 or <i>Display</i>,
whatever your screen refreshes at; 60 is what a fresh installation gets —
VSync, and Gamma, a slider that lightens or darkens every picture the game
shows, in a window or full screen; its middle, the default, changes nothing,
and the number beside the bar, a percentage, says where it stands (with the
percent sign on screens of about 3:2 and wider, without it on narrower ones
such as 4:3).</td>
</tr>
<tr>
<td width="50%"><img src="docs/menu-controls.jpg" alt="Controls"><br>
<b>Controls</b>: Tank or Modern, the Action row that splits Interact from the
Secondary button, the pad layout, and for Modern controls camera speed, dead
zone and mouse sensitivity (bars with their number beside them, a percentage,
its sign shown as on Display) and Invert mouse; then targeting and the ammo
hotkeys.</td>
<td width="50%"><img src="docs/menu-key-bindings.jpg" alt="Key bindings"><br>
<b>Key bindings</b>: every role rebindable, on the keyboard and - as here -
on the pad, over as many pages as the list needs, with <i>Reset to
defaults</i> standing above each of them.</td>
</tr>
<tr>
<td width="50%"><img src="docs/menu-view.jpg" alt="View"><br>
<b>View</b>: the first-person view, its field of view, the death camera, the
head-bone look, the damping, and whether her body stays animated or holds
still while she stands.</td>
<td width="50%"><img src="docs/menu-modes.jpg" alt="Modes"><br>
<b>Modes</b>: Combat, Sprint, Duck and Grab, each of them Toggle or Hold, and
Ledge hang (Modern controls only, off by default).</td>
</tr>
<tr>
<td width="50%"><img src="docs/menu-hud.jpg" alt="HUD"><br>
<b>HUD</b>: bar mode and positions, enemy bars, the ammo counter, the
game-over menu, the loading bar style (TR5, TR4 or off - no loading screen
for a short load) and the loading text (the name of the level being
loaded).</td>
<td width="50%"><img src="docs/menu-audio.jpg" alt="Audio"><br>
<b>Audio</b>: music and effect volume, each a bar with its number beside it,
a percentage from 0 to 100 (its sign shown as on Display), sound quality —
and the language, which can be changed while the game runs.</td>
</tr>
</table>

<sup>HD art is read at run time from the player's own Tomb Raider IV-VI
Remastered installation — none of it is included here.</sup>

The HD look, in pictures: the engine's own water, mirrors, shine, shadows and
fog over the Remaster's art, with *HD textures* on.

<table>
<tr>
<td width="50%"><img src="docs/hd-water-river.jpg" alt="Floats on a canal"><br>
<b>HD water</b> on a canal: the engine's own moving surface, the floor seen
bent through it, and a barrel and smaller things drifting on its current.</td>
<td width="50%"><img src="docs/hd-water-foam.jpg" alt="Falling water and foam"><br>
<b>HD water</b>: water falls from the ceiling of a flooded corridor, and foam
spreads on the surface where it lands.</td>
</tr>
<tr>
<td width="50%"><img src="docs/hd-water-spring.jpg" alt="A spring, from below"><br>
<b>HD water</b> from below: a spring's small bubbles rise from a crack in the
floor, the light the waves gather plays on the rock, and the surface overhead
shows the room above it, bent. The larger bubbles are Lara's own breath.</td>
<td width="50%"><img src="docs/hd-water-underwater.jpg" alt="Under the sea"><br>
<b>HD water</b> in Deepsea Dive: the sea's surface seen from below, a dark
blue underside with its small waves and a soft glow of light through it.</td>
</tr>
<tr>
<td width="50%"><img src="docs/hd-water-fish.jpg" alt="The fish tank"><br>
The fish tank in the VCI building holds a school of fish behind its glass,
and the black marble in front of it mirrors the room (<i>Reflections</i>,
on Graphics).</td>
<td width="50%"><img src="docs/hd-reflection-floor.jpg" alt="A mirroring floor"><br>
<b>Reflections</b>: a polished floor mirrors the room standing over it - the
columns in the dark marble, the walls in the mosaic under Lara. <i>Options
-> Graphics -> Reflections</i> switches it.</td>
</tr>
<tr>
<td width="50%"><img src="docs/hd-reflection-mirror.jpg" alt="A washroom mirror"><br>
A washroom of the VCI building: Lara in the mirror on the wall, and the green
tiles of the floor mirroring the room (<i>Reflections</i>). The MP5 on her
back is a third-party mod, credited under <i>Mods shown in the
screenshots</i> below.</td>
<td width="50%"><img src="docs/hd-reflection-surface.jpg" alt="Lamplight on metal"><br>
A lamp's light lies on the wall where it hangs, and the metal round Lara
catches it: in an HD room metal, marble and ice shine. It is a gloss, not a
mirror - nothing of the room is drawn a second time.</td>
</tr>
<tr>
<td width="50%"><img src="docs/hd-shadows.jpg" alt="Lara's projected shadow"><br>
<b>Shadow mode</b> <i>Projected</i> (<i>Graphics</i>, second page), the
default, offered while HD is on: Lara's own outline lies soft-edged on the
deck beside her, in place of the round blob.</td>
<td width="50%"><img src="docs/hd-fog.jpg" alt="Fog in a cave"><br>
<b>Fog</b> (<i>Graphics</i>): the level's fog bulbs with a body - the haze
fills the cave as a volume and thickens with the depth of the view as Lara
dives.</td>
</tr>
<tr>
<td width="50%"><img src="docs/hd-distance.jpg" alt="A view to the horizon at dusk"><br>
A long view across the level at dusk: the windmill of the next room, the far
hills, the trees and the standing stones against the evening sky.</td>
<td width="50%"><img src="docs/first-person.jpg" alt="First person view"><br>
The <b>first-person view</b> (<i>Controls -> View -> First person
view</i>): the camera sits behind her eyes, her head is not drawn, and her
arms aim the Uzis down the street.</td>
</tr>
</table>

<table>
<tr>
<td width="50%"><img src="docs/features-3.jpg" alt="Per-level secret counter"><br>
The pause screen now counts the secrets of the level you are in, in
brackets behind the campaign total — useful when the level order is rolled
and you cannot tell from the sequence which level you are in.</td>
<td width="50%"><img src="docs/setup-dialog.jpg" alt="The setup dialog"><br>
The setup dialog, driving a native Direct3D 9 device — resolution,
fullscreen or windowed, FMVs and sound, with the build date in the corner.</td>
</tr>
<tr>
<td colspan="2" align="center"><img src="docs/gui-credits.jpg" alt="The in-game credits screen" width="640"><br>
Credits for everyone whose work is in here.</td>
</tr>
</table>

<details>
<summary><b>More from a rolled run</b> (10 screenshots)</summary>

<table>
<tr><td width="50%"><img src="docs/ingame-01.jpg" alt=""></td><td width="50%"><img src="docs/ingame-02.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-03.jpg" alt=""></td><td width="50%"><img src="docs/ingame-04.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-05.jpg" alt=""></td><td width="50%"><img src="docs/ingame-06.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-07.jpg" alt=""></td><td width="50%"><img src="docs/ingame-09.jpg" alt=""></td></tr>
<tr><td width="50%"><img src="docs/ingame-10.jpg" alt=""></td><td width="50%"><img src="docs/ingame-11.jpg" alt=""></td></tr>
</table>

</details>

## Outfit changer (F4)

Press **F4** in the game and everything stops, exactly as the pause menu
stops it. The list is every outfit this machine can dress Lara in: the 22
bodies of your **Tomb Raider IV-VI Remastered**, the 30 of **Tomb Raider
I-III Remastered** the first has not got under the same name, and every one
your `Mods` folder adds. Each row carries where it came from — `IV-VI`,
`I-III`, or the mod's own folder name — and the row in force carries a small
raised mark after its name, the font's `*`, which reads like an apostrophe.

<table>
<tr>
<td width="50%"><img src="docs/outfit-changer.jpg" alt="The F4 outfit changer"><br>
The bodies of the installed Remasters, with the footsteps, the head rule,
the shine and the skin glow of the highlighted one — and, in brackets, which
tier decided the head rule, the shine and the glow.</td>
<td width="50%"><img src="docs/outfit-changer-mods.jpg" alt="The F4 outfit changer with mod outfits"><br>
The same list with a mod folder installed: every row names the folder it came
out of. The outfits shown here are a third-party mod, credited under
<i>Mods shown in the screenshots</i> below.</td>
</tr>
</table>

<sup>HD art is read at run time from the player's own Tomb Raider IV-VI
Remastered installation — none of it is included here.</sup>

The first row is **Level facts**, and it means "no choice at all": the level
and the rolled run decide, which is what a player who has never opened this
menu has — so on a machine where nothing was ever chosen, that first row is
the one carrying the `*`. Choosing it again clears the setting.

| key | what it does |
| --- | --- |
| up / down | move through the list |
| left / right | switch the highlighted outfit between **Boots** and **Bare** |
| select (Enter, or the pad's action button) | wear it and close |
| deselect (Esc, Backspace, the pad's back button) | close and change nothing |

On the **Level facts** row there is no outfit to hang a footstep choice on, so
left and right do nothing there and the row says *as the outfit says* instead.

Under it stands **Head**, and that one is not a choice: it says whose head the
highlighted body wears — *own*, *TR5* or *TR5 hair* — and in
brackets who decided, so a body that comes out of Tomb Raider I-III and loses
its hood to TR5's hair says so before you pick it. What the three mean, and how
a mod says which it wants, is under *Mods* below.

A gamepad works in it because it reads the same input bits the pause menu
and the options page read. F4 does nothing in the title screen, in a cutscene,
with Lara dead or while you are looking through the binoculars or a laser
sight; the inventory and the pause menu run their own loop and cannot be open
at the same time.

**It takes effect at once.** The level's models are built again with the new
body while the game stands still; the rooms, the inventory, her weapons and
everything classic are untouched. The page cache never gives a page back
inside a level, so there is a limit on what one level may spend on outfits —
and that limit is what your machine really has. Before every change the game
asks three questions: is there room in the **texture budget** this level was
given, is there room in the **address space** of the program, and is there a
single free **block** big enough for one page. All three have to say yes;
otherwise the row under the list says *applies at the next level*. **64 MiB**
is the floor of the first question, so a level that has spent less than that
is never stopped on the card's account.

**Why the address space, on a machine with 16 GiB of video memory?** Because
`PCtomb5.exe` is a 32 bit program: four gigabytes of address space and no
more, whatever the card and the RAM hold. Measured with `PoolProbe`, the
pages themselves live on the CARD and cost the program almost nothing of it —
0.04 MiB for a page of 85.25 — so in practice it is the texture budget that
decides, and on a big card's automatic budget a level has room for well over
a hundred changes. A **Save and Load**
starts a fresh page cache and gives all of it back, and so does the next
level.

With `-log` the log says the whole of it: every level build prints the budget,
the free address space, the largest free block and the reserve that is held
back; every change prints what it cost and how much room is left; and a change
that waits names which of the three questions said no.

When it does wait, the **footsteps wait with it**: the body on the screen is
still the old one and it keeps the old one's footsteps until the next level.

**It is remembered.** The outfit is the string `hd_outfit` under
`HKCU\Software\Core Design\Tomb Raider Chronicles\tomb5`, and the
footsteps are one DWORD per outfit under `...\tomb5\hd_footsteps` — the
wardrobe key is the value's name, `1` is bare and `2` is boots. Both are read
at every level start. An outfit whose mod you have taken out again is a line
in the log and the level's own body, not an error. Young Lara stays young
whatever any of it says.

### Footsteps

A step in this engine is **two sounds on one frame**: her own foot (sound id
0, about 40 ms) and the surface under it (sound ids 288 to 294, about 190 ms —
a boot on mud, on metal, on wood, chosen by the sector). The floor layer is
the loud half.

The footsteps follow **what is drawn**, at every step, and only Lara's do:

| | Lara drawn classic | Lara drawn HD, *Boots* | Lara drawn HD, *Bare* |
|---|---|---|---|
| no sound mod | the level's own boots | the same boots | the Remaster's barefoot steps |
| a mod that replaces the footsteps | the mod's footsteps | your installation's unmodded boots | the mod's footsteps |
| anyone else who walks | the level's own steps, a mod's if one replaces them | the same | the same |

**Boots** in the HD look is the sound your own **installed Remaster** has for
the footsteps, unmodded, so a sound mod that replaces them does not make every
outfit barefoot. **Bare** is your sound mod's own footsteps when a mod really
replaces sound id 0, or otherwise the barefoot steps your installation's own
bank carries (sound id 417, five recordings that no level uses) — most sound
mods do not replace them, and one that only changes a door or a voice is passed
over here rather than handing the boots back under the name *bare*. In the
classic look Lara wears the classic boots and her steps are the level's own,
your sound mod's included. A guard or a SWAT beside her keeps the level's own
steps whatever she wears. F12 changes her step at the next footfall; every set is
in the sound memory from the level's load.

Both ways of shipping footsteps count: a mod's own `5\DATA\MAIN.SFX`, and
loose `0000.wav` to `0003.wav` and `0010.wav` beside it with no bank at all.
The test is not which of the two a mod uses but whether what it puts on the
footsteps differs from your installed Remaster's, so a mod that ships only the
five wav files counts exactly as a mod's bank does. While she walks barefoot in
the HD look the **floor layer does not play at all**, because those recordings
are a boot on a surface and putting one under a barefoot step is exactly the
double sound this setting exists to avoid; drawn classic, or in boots, it
plays as the level has it.

What an outfit does unless you say otherwise comes from the wardrobe — a
mod's `outfit.txt` can say `footsteps=bare`, and the built-in table says it
for the bodies that have no boots. Your own choice wins over both and is kept
per outfit.

There is no separate barefoot LANDING among those five recordings. Measured on
the bank, the boots' landing (175 ms) is more than four times as long as their
steps (33 to 41 ms), while none of the five barefoot takes (106, 86, 134, 69
and 113 ms) stands out from the others that way: all five are steps. So the
barefoot landing draws from the same five, at the volume and range the level
gives its own landing. A level with no room left for a second
set, or with no sound bank to take one from, shows the row as *unavailable*
and the log says which of the two it was. Every line of it begins with
`SFXLOG footsteps:`.

## Mods

Put a folder called `Mods` next to `PCtomb5.exe` and the engine reads it at
the first level. The layout is the remaster's own drop-in layout, so a mod
made for the Remaster works here as it lies:

```
Mods\<Category>\<Mod>\5\ITEM\<NAME>.TRM      models
                     \5\TEX\<n>.DDS          the pages those models wear
                     \5\DATA\MAIN.SFX        a whole sound bank
                     \4\...                  the same, searched after 5
Mods\<Mod>\...                               a mod without a category works too
```

**Order and priority.** Alphabetical by the folder path, and the first mod
that has a file wins. A folder whose name starts with `_` is switched off,
and everything under it with it — rename `IMI Uzis` to `_IMI Uzis` and the
original is back, with nothing else to undo. Nothing is ever written into
your installation or into the mod: a mod replaces a file by being *looked at
first*, never on disk.

**What a mod may replace.** Anything the HD path resolves by name: the hands
`HAND_*`, the holsters `HOLSTER_*` and the pickups `EXTRA_*`. A page a mod does
not ship is taken from your installation, so a mod that brings only models
works. **A mod can never change a page of a model it did not supply** — the
vanilla look cannot be damaged by installing one.

**Inventory models.** A mod written for Tomb Raider I-III carries that game's
names — `UZI_OPTION` instead of `EXTRA_UZI_ITEM` — and the ring, the pickup
display in the corner **and the thing lying on the floor** draw it where the
slot's own model would stand. It is turned and scaled onto that model's own
**shape** — the directions its vertices really run in, not its bounding box —
because the I-III inventory models stand upright and the IV-VI ones lie flat.
A mod that ships an `EXTRA_*` of its own wins everywhere. A mod whose
`mod.txt` names an `origin=` game is **posed** with that game's own first
frame as well, so a pair of guns comes out crossed instead of parallel; the
long version is under *A mod's own inventory model* below.

**A pickup never sinks into the floor.** Every pickup model that comes out HD
is measured against the classic mesh it replaces, once when the level is
built: if its lowest point reaches deeper than that mesh's, it is lifted by
exactly the difference, straight up in the world and nothing else — never
scaled, never turned, never moved sideways. A model that already lies on that
point or above it is not touched at all.

Both lowest points are taken **in the frame the level really draws the item
in**. A pickup is not drawn in the pose its file is modelled in: the item's
animation frame turns it first, and six of ANDREA3's eighteen pickups carry a
turn that is not a plain yaw — the Uzi item's stands the model on its head. So
the number a mod author reads is the one his model really reaches down to on
the ground. The log says which it was, per slot:

```
HDLOG level=ANDREA3 pickup floor slot=240 name=CROWBAR_ITEM
      model=EXTRA_CROWBAR_ITEM lowest point 6.2 under the classic
      mesh's - lifted by 6.2 units
```

**How far that can go.** Most of these numbers are millimetres: over the
fifteen levels of the campaign, eleven pickups are lifted at all and nine of
them by 7 units or less — a crowbar 6.2, a laser sight 4.2, a flare 1.0. Two
are not. A mod's fitted pair of Uzis comes up 19.2, and the largest in the game
is your own installation's: RICH3's crossbow ammunition, where your installation
has no Tomb Raider V model for the slot and the engine falls back to the Tomb
Raider IV quiver. That model is drawn at TR5's item origin and reaches **126.6
units** below the level's own mesh — at that origin nearly two thirds of it
would stand in the floor; the engine lifts it onto it. A big number always
means the same thing:
the model was modelled over a different item's origin. A model authored over
this game's own reads zero.

**Lara always has TR5's bare hands.** `HAND_GLOVES_*` is never worn, whatever
the outfit or an `outfit.txt` says, so every level and every outfit look alike
in that one respect. (On screen this concerns the Russia levels: their outfit
would otherwise draw the gloved set.)

**A mod's hand may keep its gun and give back the hands.** A hand model painted
with pages of the I-III bundle — `origin=tr1` below — would give Lara that
game's skin tone and its glove seams. Those pages are dropped, and your
installation's own hands are drawn under the mod's gun instead. A mod with no
`origin=` line is untouched.

**A mirrored hand model is straightened out.** A `HAND_*` file is modelled once
and mirrored, and an exporter that mirrors the shape without the bone names
leaves part of one hand hanging on the other — invisible while Lara stands
still, a sheet of skin stretching between her wrists the moment she aims. The
engine folds those weights back onto the hand they belong to. Only a real
mirroring error is touched; the rounding every one of your installation's own
files carries is left alone.

**Outfits are different: they are ADDED, never swapped in.** An
`OUTFIT_*.TRM` a mod ships becomes an entry of its own in **Lara's wardrobe**,
beside the ones your two Remasters bring — your `OUTFIT_CLASSIC` does *not*
take the place of your installation's Rome outfit, and a level that asks for
the Rome outfit still gets the Rome outfit.

**An outfit from Tomb Raider I-III wears TR5's head.** The two bundles build
Lara's head differently. A IV-VI body carries her whole head — skull and hair —
on its head node, and the separate face model that rides on it supplies the
face and nothing else: the two are halves of one head. An I-III body carries
**headgear** on that node instead — the wetsuits' diving hood and goggles, the
ice hood, the Greek headband — and six of the thirty carry nothing there at
all. Drawn as it lies, the face model's open skull is what you see: a bald head
with a face on it and the braid hanging off it.

So every outfit of the I-III installation, and every mod whose `mod.txt` says
`origin=tr1`, `tr2` or `tr3`, wears **TR5's head** instead of its own — the
Rome outfit's head node, hair and all. Everything else keeps its own head,
which is every IV-VI body and every mod that does not name an origin, so
nothing that looked right before this changes at all.

Three rules exist and `outfit.txt` can name one with `head=`:

| `head=` | what is drawn |
| --- | --- |
| `tr5` | its own head node is **not** drawn; TR5's head stands there instead |
| `hair` | its own head **and** TR5's hair over it — for a hood or a hat worth keeping |
| `outfit` | its own head alone, which is what every IV-VI body does |

**A mod without a `mod.txt` keeps its own head**, whatever its files are
called. There is no rule by name and none by geometry: `OUTFIT_TR2_HOME` is
the name of a IV-VI body with a full head *and* the name a mod gives an I-III
conversion, and counting vertices to tell a bald head from a hooded one is a
guess. Add `origin=tr1` to the mod, or `head=tr5` to its `outfit.txt`, and it
is dressed. Which rule each body got, and who decided it, stands in the F4
window, in the log, and in the `head` column of
`ModelProbe … -wardrobe`.

(`OUTFIT_HAIR.TRM`, which the I-III bundle and some mods ship, is **not** a
head of hair: it is that bundle's name for the braid, a six-link chain, and the
wardrobe passes it over the same way it passes over `OUTFIT_HANDS_*`.)

The wardrobe finds, in this order: the outfits of your **Tomb Raider IV-VI
Remastered** (22 of them — the bundle holds 24 `OUTFIT_*` bodies and the two
`OUTFIT_YOUNG*` are passed over, see below), those of **Tomb Raider I-III
Remastered** that the first has not got under the same name (30 more — the
`OUTFIT_TRX_*` sets), and then every mod's. `OUTFIT_YOUNG*` of the two
installations is passed over
— young Lara carries no weapon and a randomized run hands her one in every
level — but a **mod** in that slot is not, because a mod there is usually a
grown-up body. Every key the wardrobe knows is printed by
`Diagnose\ModelProbe <5-folder> -wardrobe -tr3 <3-folder> -mods <Mods>`.

**The wardrobe belongs to the ENGINE and not to the randomizer.** These
entries are what the in-game outfit changer offers (F4, above); no
roll ever draws one and nothing about them is written into a level file. The
reason is reproducibility: a seed and its settings travel from one player to
another, an installation and a `Mods` folder do not, so a run whose outfit
list depended on what happens to be installed here could not be played there.
The randomizer's own **Wardrobe** window is unchanged — the three Tomb Raider
V outfits and the four Tomb Raider III guests, and nothing else.

The engine reads the choice from one registry value, a string named
`hd_outfit` beside `hd_install` under
`HKCU\Software\Core Design\Tomb Raider Chronicles\tomb5`, and re-reads it at
every level. **The F4 outfit changer writes it**, and while it is empty or
absent every level dresses her exactly as it always did.
A key your machine no longer has — you took the mod out again — is a line in
the log and the level's own outfit, not an error. Young Lara stays young
whatever it says.

**`mod.txt`** is optional and goes in the mod's own folder. One `key=value`
per line, `#` starts a comment. One key so far:

```
origin=tr1        # or tr2, tr3
```

Say this when the mod's models were drawn against **Tomb Raider I-III
Remastered** and name its page numbers. Without it those pages are looked
for in the IV-VI bundle, where the same numbers hold different pictures —
which is why a hand can come out black, or a boot white. Keys this build does
not know are written to the log and ignored.

**`outfit.txt`** is optional too, goes beside `mod.txt`, and says what the
mod's outfit IS. Everything in it is optional; what is not said is measured
off the file or falls back to the wardrobe's own defaults (hands, braid and
holsters on, no gloves, a plain cloth sheen).

```
name=Sola's wetsuit     # what the wardrobe window calls it - a list
                        # block's name= sets it over this one
hands=1                 # does she wear the separate HAND_ models
gloves=0                # ... the gloved ones - read, logged, and then
                        # ignored: see "Lara always has TR5's bare hands"
braid=1                 # the TR5 braid
holsters=1              # thigh holsters
rig=0                   # the pouches they hang in, cut out of the
                        # London catsuit - for a body with bare thighs
sheen=0.45              # how wet the material looks, 0 to 2 ...
exponent=26             # ... and how tight the highlight is, 1 to 128
skin_sheen=0.12         # and how her own SKIN shines - her face, the
skin_exponent=10        # lid on her neck, her bare hands and the pages
                        # named by skin= below. 0 to 2 and 1 to 128
skin=8001               # which of its pages are Lara's own skin, so that
                        # a bare forearm and the hand on it shine alike
footsteps=bare          # or boots - what she walks on in this body,
                        # unless the player says otherwise in F4
head=tr5                # or hair, or outfit - whose head this body wears.
                        # Without the line: tr5 when mod.txt says origin=,
                        # outfit otherwise. See "An outfit from Tomb Raider
                        # I-III wears TR5's head" above
```

An unknown key is written to the log and ignored, exactly as in `mod.txt`. So is
a number outside its range, and so is a value that is no number at all:
`sheen` and `skin_sheen` take **0 to 2**, `exponent` and `skin_exponent` take
**1 to 128**. A refused line changes nothing — the value the engine had stands,
and the log names the line and the reason. (A `sheen` of 14 would draw her
clothes pure white and one of `1e40` solid black, which is why the range is
kept rather than trusted.)

The same holds for the keys that are not numbers. `hands`, `gloves`, `braid`,
`holsters` and `rig` take **`1`, `yes`, `true`, `on`** or **`0`, `no`,
`false`, `off`** and nothing else — a `ja`, a `y` or a `2` is a line in the log
and the answer the engine had stands. `skin` wants **one to three page
numbers** separated by commas; a piece that is not a page number is named in
the log and passed over, and a `skin=` line with no page number in it at all
changes nothing. There is deliberately no way to say *no* skin pages: leave the
line out and the engine measures her throat off the file itself.

**`sheen` is the clothes, `skin_sheen` is her.** The first is what the outfit
is made of — latex shines, a parka does not — and the second what Lara is made
of while she wears it, which is not the same question: a wetsuit wants wet skin
and a desert body does not. Without the two skin lines she is `0.12/10`, the
soft wide sheen every build before this one drew, so a mod written earlier
looks exactly as it did.

### A list file, for the outfits that have no folder

A mod is a folder and can carry an `outfit.txt`. The **52 outfits of your two
Remaster installations** have no folder of their own, and a mod that ships
seventeen bodies under one `outfit.txt` cannot say anything about *one* of
them. So a `.txt` file lying **beside** the mod folders — directly in `Mods`,
or directly in a category folder such as `Mods\Outfits` — is read as a list of
blocks, one per outfit:

```
# Lines starting with # are comments and close nothing.

Outfit: TR2_Swim
name=Sola's swimsuit
sheen=1
exponent=50
skin=8001

Outfit: 13/TRX_Wetsuit_1
head=hair
skin_sheen=0.6
```

Any file name, and any number of them; they are read in **path order** — the
whole path compared as text, so the folder decides before the file name does,
and a file in `Mods` is not automatically read before one in `Mods\Outfits`.
Where two files set the same key, the later one wins, and the `MODLOG list0`,
`list1` … lines of the log say the order they were read in. A block opens at
`Outfit:` and closes at the next one or at an empty line, and every key an
`outfit.txt` takes is a key of a block — `name=` included, which is what the
F4 window and `ModelProbe … -wardrobe` call that row, so a block renames the
outfits of your installation as well, which until now only a mod folder could
do for its own. A `name=` with nothing after it, or nothing but spaces, is a
line in the log and the row keeps the name it had; a name longer than 63
characters is cut to 63, and the log says so and quotes what it kept.

**The name** is matched against the outfit's file name without regard to case,
to the `OUTFIT_` prefix, or to `_` against a space — `TR2_Swim`, `tr2 swim` and
`OUTFIT_TR2_SWIM` are one name. Without a prefix it reaches **every** outfit of
that name, your installation's and a mod's reskin of it alike; put a key prefix
in front — `13/`, `46/`, `mod/outfits/legend/` — to mean one of them only. The
keys are the ones `ModelProbe … -wardrobe` prints and the F4 window uses.

**What wins.** A list block beats a mod's `outfit.txt`, which beats the
engine's own table of measured outfits, which beats what the engine measures
off the file, which beats the defaults. **Per key**, and that is the whole
point: a block that says only `exponent=` changes only the exponent and leaves
the sheen where the tier below it had it. Two blocks for one outfit, in one
file or in two, are both applied and the later one wins for the keys it sets.

Nothing in a list file can break anything. A name that matches no outfit on
this machine, a key this build does not know, a line without `=`, a value out
of range, one that is not a number, a yes-or-no that is neither, a `skin` line
naming no page, an empty `name=` — each is a line in the log, the value the
engine had stands, and the rest of the file is read on. A block's own line
names only the keys that really arrived; a block whose every line was refused
says so. Save it as UTF-8 if you like: the byte order mark some editors write
in front of the first line is taken off. A line may be as long as you please —
it is read whole, not in pieces. A file whose name begins with `_` is switched
off, exactly like a mod folder.

With `-log`, the scan says what it did:

```
HDLOG wardrobe list outfit_1-5.txt: Outfit TR2_Swim -> 46/OUTFIT_TR2_SWIM:
      sheen exponent skin
HDLOG wardrobe list outfit_1-5.txt: 52 blocks, 52 matched, 0 unmatched
```

and the F4 window shows every body's **Shine** and **Skin glow** with the tier
that decided each, so a line you wrote can be checked in the game: it says
`(the list)` when it arrived.

One thing worth knowing before you copy a list from somebody else: a block
without a key prefix reaches **every** outfit of that name, a mod's included.
`Outfit: Classic` with `sheen=0.10` in it also takes the shine off the
`OUTFIT_CLASSIC` of a mod that had set `sheen=1` in its own `outfit.txt`, since
the list is the higher tier. Write `46/Classic` to mean the installed one
alone.

**Sounds.** A mod's `5\DATA\MAIN.SFX` is used instead of `audio\MAIN.SFX` if
there is one, and a loose `<NNNN>.wav` — exactly four digits, anywhere under
the mod — replaces that numbered sample of the bank. The bank itself no
longer has to be copied into `audio\` by hand: without one there, the engine
reads the bank of your own installed Remaster where it lies. Samples `0000`
to `0003` and `0010` are the footsteps and the landing: they replace Lara's
steps in the classic look and everyone else's in both, and in the HD look they
are the **Bare** set of the F4 outfit changer, while an outfit with boots walks
on your installation's unmodded boots — see *Footsteps* above. This holds
whether the mod ships them in a bank or as five loose wav files.

**Not supported.** A face on its own does nothing: a mod
that ships `<NAME>_HEAD.TRM` without the body's `<NAME>.TRM` is passed over,
because a face is looked for wherever its body came from so that the two wear
pages out of one source — a lone face would land on the installation's body
and read its pages under the wrong numbers. Effect and sprite pages such as
`4\TEX\EFFECT.dds` do nothing — this engine draws no Remaster sprites — and
so does anything that only exists for Tomb Raider IV, `4\DATA\MAIN.SFX`
included.

**A mod's own inventory model.** Tomb Raider I-III draws the inventory out of
`<BASE>_OPTION` models where IV-VI uses `EXTRA_<BASE>_ITEM`, so a mod written
for I-III brings the OPTION names with it. The **ring**, the little turning
object in the corner **and the pickup lying on the floor** take it when a mod
ships one and no mod ships an `EXTRA_` name for the same slot; a file from
another game stands in that game's frame, so it is turned onto the vanilla
item's own — by the shape of its vertices and not by its bounding box, which
cannot tell a pair of guns from one gun standing up.

**And it is POSED first, out of your own I-III installation.** An inventory
model in the `.TRM` format is stored in the classic *bind pose*: each part
carries the mesh tree's offsets and no rotation at all, which for the IMI Uzis
mod means its two guns lie side by side and parallel. The cross is that game's
own **frame 0**, and the engine reads it now — the mesh
tree and the first frame of the classic object, out of the level files of your
`Tomb Raider I-III Remastered` folder (`1\DATA\*.PHD`, `2\DATA\*.TR2`,
`3\DATA\*.TR2`), the first level in name order that holds it. The pair comes
out crossed, as it is in the game the file came from.

It needs three things and does nothing at all without them, with a line in the
log saying which one was missing: the model has to come from a **mod whose
`mod.txt` names an `origin=`**; its name has to be one the classic games know
(`GUN_OPTION` or `PISTOLS_OPTION`, `SHOTGUN_OPTION`, `MAGNUM_OPTION` or
`REVOLVER_OPTION`, `UZI_OPTION`, and the ammunition boxes `MAG_AMMO_OPTION`,
`SG_AMMO_OPTION`, `UZI_AMMO_OPTION`, `PISTOLS_AMMO_OPTION`,
`REVOLVER_AMMO_OPTION`, `SHOTGUN_AMMO1_OPTION` and `SHOTGUN_AMMO2_OPTION`);
and the file's parts have to match that object's. Without an I-III
installation, without an `origin=` line, or under a name the table does not
know, the file is drawn exactly as it stands — which is what the ring did
before.

**And the pickup on the floor takes it too.** A fitted model is centred on the
vanilla item's box, so one thicker than the item it stands in for reaches
below that item's lowest point — into the ground, for a thing lying on it.
Until now that was a reason to leave the floor vanilla; it is now a
measurement and a lift. In the frame ANDREA3 draws the item in, the fitted
pair of Uzis stands **19.2 units** below the classic mesh's own lowest point
and its ammunition box **6.8**, measured on the model's own vertices through
the fit, and each is lifted by exactly that much before it is drawn. One rule
for every pickup model, a mod's and your installation's alike, and the number
is in the log per slot:

```
HDLOG level=ANDREA3 ring floor slot=336 name=UZI_ITEM takes the fitted
      UZI_OPTION on the floor, lifted by 19.2 units (its lowest point
      stood 19.2 under the classic mesh's)
```

The ring and the corner display have no floor and are not touched by any of
this: they draw the fit exactly as they did.

**One thing a mod author has to get right, and the engine cannot.** A `.TRM`
vertex names the **classic mesh** it rides on, and four of this game's pickups
are initialised so that only mesh 0 is ever shown — `HK_ITEM`, `HK_AMMO_ITEM`,
`CROSSBOW_ITEM` and `REVOLVER_ITEM`, whose second mesh is the laser sight.
A model whose geometry sits on mesh 1 of one of those is therefore invisible on
the floor, however well it loads. The log says so the first time such an item
is drawn:

```
HDLOG level=RICH1 item slot=345 name=HK_ITEM model=EXTRA5_HK_ITEM draws
      nothing: its geometry sits on node mask 2 and this item shows 1 -
      the model's vertices name a classic mesh this slot never draws
```

Put the geometry on node 0. Your installation's own `EXTRA5_HK_ITEM` has 2426
of its 2499 vertices there and the 73 that are not are the sight.

**And one sidecar line.** A randomized level's `<level>.hdm` now carries an
`sfximport` line naming the sound ids the run imported from another game.
Those are ids your installation's own bank keeps other recordings on, and
without the line the engine's HQ bridge could pair one of them: the Tomb
Raider III vulture died with a scientist's voice line from RICH1. The engine
reads that line **whether the HD textures are on or off**, because the bank
chain it protects is not part of the HD path either. An engine older than
this build ignores the word and plays what it always played.

**Mods are yours, not ours.** Nothing of any mod is part of this project and
nothing of one is ever shipped with it, exactly like the game data and the
HD art. Start the game with `-log` and the log tells you which mods were
found, what each one replaced and what was refused — every one of those
lines begins with `MODLOG`.

<table>
<tr>
<td width="50%"><img src="docs/mods-shotgun.jpg" alt="A mod's shotgun in Lara's hands"><br>
A mod's weapon model in her hands and a mod's outfit on her, in the middle of
a rolled run.</td>
<td width="50%"><img src="docs/mods-uzis.jpg" alt="A mod's Uzis in the inventory ring"><br>
A mod's Uzis in the inventory ring — a model written for Tomb Raider I-III,
turned onto the Chronicles item's own shape and posed out of that game's own
first frame, so the pair comes out crossed.</td>
</tr>
</table>

<sup>HD art is read at run time from the player's own Tomb Raider IV-VI
Remastered installation — none of it is included here.</sup>

### Mods shown in the screenshots

| Mod | Author | Shown in |
| --- | --- | --- |
| [SPAS-12 Shotgun Replacer](https://www.nexusmods.com/tombraidertrilogyremastered/mods/135) | [xKhalix](https://www.nexusmods.com/profile/xKhalix) | the shotgun picture, the mirroring floor, the lamplight on metal and the dusk view (the gun on Lara's back) |
| [IMI Uzi Replacement for Mac-10](https://www.nexusmods.com/tombraidertrilogyremastered/mods/187) | [xKhalix](https://www.nexusmods.com/profile/xKhalix) | the Uzi picture and the first-person view |
| [Tomb Raider - Underworld - AIO](https://www.nexusmods.com/tombraidertrilogyremastered/mods/954) | [DarkLegendAoD](https://www.nexusmods.com/profile/DarkLegendAoD) | the outfit changer with mods, and the Uzi picture |
| [OPERA DRESS (Tomb Raider 5)](https://www.nexusmods.com/tombraidertrilogyremastered/mods/281) | [Zellphie](https://www.nexusmods.com/profile/Zellphie) | the shotgun picture |
| [Tomb Raider - Angel of Darkness - Mag Vega](https://www.nexusmods.com/tombraidertrilogy2remastered/mods/79) | [DarkLegendAoD](https://www.nexusmods.com/profile/DarkLegendAoD) | the washroom mirror and the spring from below (the MP5 on Lara's back) |

These are third-party mods by their respective authors. They are not part of
this project and are not included; get them from the authors' pages.

## What you need

**To play:** your own installation of **Tomb Raider Chronicles**. Game data is
deliberately **not** part of this project — neither levels nor music,
videos or language files. The game belongs to its rights holders; the
randomizer patches your own files at runtime and backs everything up to
`data\Backup` first. What the randomizer does ship is a survey of the
original levels' geometry, which it needs to place anything legally —
[THIRD-PARTY.md](THIRD-PARTY.md) says exactly what that is and is not.

**For the HD art, the HQ sound bank and the HD loading screens:** your own
installation of **Tomb Raider IV-VI Remastered** — and, for the four Tomb
Raider III guests in HD, of **Tomb Raider I-III Remastered**. Not one file of
either is part of this project or of the release package, none is offered for
download anywhere here, and nothing is ever copied out of them: the engine
reads them where they lie, on your own machine, while the game runs. Without
them the game runs exactly as the original did, with the classic art, the
classic sound bank and the classic 640x480 loading screens.

The HD art is built at every level load whether you look at it or not, so that
F12 switches between the classic and the HD look at once. That has a price,
measured over nine loads, the title twice and seven levels: **1.25 to 4.78
seconds more per level load** (3.2 in the middle), 232 to 475 MiB of texture
pages and 10 to 62 MiB of room buffers on the graphics card, and 7 to 17 MiB of
model buffers in the program. If you play classic anyway, switch **Options →
Graphics → HD build** off: from the next level load on nothing of the HD art is
built, the loads are as short as without the Remaster, and F12 says *HD is off*.
Switch it on again and the next level load builds it again.

The four Tomb Raider III guests (T-Rex, raptor, compy, vulture) likewise need
your own **Tomb Raider III** installation - the original release, or the game
folder `3` of *Tomb Raider I-III Remastered*, which carries The Lost Artifact
too; their models and sounds are read from it at run time and none of them
ships here either.

**The machine:** Windows 10 or 11 (the engine is tested on Windows 11; the
XAudio2 it uses comes with Windows 10). `PCtomb5.exe` is a 32-bit program on
Direct3D 9 and XAudio2, marked large-address-aware, so on 64-bit Windows it has
four gigabytes of address space whatever the machine holds. The HD path also
needs a graphics card with shader model 3.0 (vertex and pixel shader 3.0, at
least 512 pixel shader instruction slots, the UBYTE4 vertex type and A8R8G8B8
textures); a card that lacks one of them plays the classic art, with the reason
in the log. Measured on the test machine at
2560 x 1440 and 165 Hz, over eight levels and the title with HD on: a level's
HD pages took 232 to 475 MiB of video memory (an installed outfit mod's pages
among them) and its HD geometry 10 to 62 MiB more, the program used at most
about 870 MiB of its address space, and every level ran at a median of 165
frames a second, no second of play below 144, on a median 0.3 to 0.8 ms of
processor work a frame. A card that offers less than about 600 MiB of texture
memory loads the largest pages one mip level down rather than leaving any out;
the [engine manual](docs/Engine-Manual.md) has the details.

## Building

**The one prerequisite is Visual Studio with the C++ desktop workload.** Every
build script in this repository is otherwise relative to its own folder, so a
fresh clone builds; the single absolute path in each of them is the call to
`vcvars32.bat` or `vcvars64.bat`, pinned to the install this was built with —
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\`.
Another version, edition or drive means editing that one line in the script
you are running, and nothing else. The engine project builds with the
**v142** toolset, "MSVC v142 - VS 2019 C++ x64/x86 build tools": an
individual component of the Visual Studio Installer on newer versions, and
present in Visual Studio 2019 itself.

- **Engine:** `TOMB5 Code\tomb5.vcxproj` with Visual Studio (Win32, Release),
  or `Preparation\Tools\Diagnose\be.bat` on the command line, which first
  checks every compiled HD shader (`.inc`) against its `.hlsl` and rebuilds
  one that is out of step; a build from the Visual Studio window skips that
  check.
- **Randomizer:** `TR5Randomizer\build\b1.bat` (command line) and
  `b2.bat` (GUI).
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
- **TROne (GPLv3) by Todesengel1931** —
  https://github.com/todesengel1931 — and in two different ways. The design
  of the modern (analog-stick) control scheme on the Controls page was
  re-derived here in our own units and our own voice, with the ported test
  suite's expectations taken across verbatim and credited by name in
  `Preparation/Tools/Diagnose/ModernTest.cpp`'s own header. The
  HD-at-run-time path is different: it **carries TROne's own code
  over**. Twenty-six source files in this tree name Todesengel's copyright in
  their own headers, and [THIRD-PARTY.md](THIRD-PARTY.md) lists what came
  from where, file by file, with an origin ledger of every HD rule that came
  by way of TROne or TRForge and the count on the player's own files that
  holds it.
- **[trview](https://github.com/chreden/trview)** by chreden — the tool
  every single zone boundary of this randomizer was verified with.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut (MIT) —
  the interface.
- **stb_vorbis / stb_image** by Sean Barrett (public domain) — HQ music and
  the background image.
- **TRForge** by the TRForge contributors (MIT) — five of its files, the
  reader for the Remaster's own room-geometry format, are vendored into the
  engine and compiled in, byte for byte but for three comment passages left
  out of one header; [THIRD-PARTY.md](THIRD-PARTY.md) says which and why.
- **[discord-rpc](https://github.com/discord/discord-rpc)** by Discord, Inc.
  (MIT) — Troye's Discord Rich Presence. This fork switches it off, so it is
  in no binary; its libraries and headers still travel in the source tree,
  and so does its notice.

Every licence text is in `Licenses\` in the release, and
[THIRD-PARTY.md](THIRD-PARTY.md) says which is which.

**Contributors** — testing, findings and ideas, the people who played the
seeds and reported what broke: XeNoN3560, Lucifer230, Todesengel1931,
Crockett, Grylle.

## Made with AI

This project was built with **[Claude](https://www.anthropic.com/claude)**
(Anthropic) as a coding partner, and that is too large a part of the work to
hide in a credits line. A great deal of the code, most of the analysis tools
and both manuals were written in conversation with it. Every commit from 2026-08-15 on
carries a `Co-Authored-By: Claude` trailer; the ones before it do not — the
work was the same, the trailer simply was not being written yet. The history
therefore understates the AI's share rather than overstating it. No count is
quoted here on purpose, because it would be wrong again by the next commit:
`git log --grep="Co-Authored-By: Claude" --oneline | wc -l` says it exactly.

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
