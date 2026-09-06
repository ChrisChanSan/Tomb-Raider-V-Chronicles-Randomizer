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
cameras all now leave you where you are. With *HD textures* on, her HD body
loses its neck the same way and is drawn one-sided while the view runs; the
section on the HD Lara below says why both are needed.

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

### HQ sounds

The same remaster also has the game's sound effects at twice the sample rate.
Copy its `MAIN.SFX` into `audio\`:

```
audio\MAIN.SFX
```

In your remaster installation it sits in `5\DATA\MAIN.SFX`. Keep the name - the
engine looks for exactly that, and the original game has no file of that name,
so nothing can collide.

**Nothing ships**, the same as with the soundtracks. Without the file the game
plays Core's samples as it always has.

What it buys you: the original stores its sounds as 22 kHz ADPCM inside every
level file. The remaster's are 44.1 kHz PCM, and they are not the old ones
upsampled - there is genuinely more in them. Same recordings at the same
volume, with the bandwidth the 2000 release threw away.

About nine sounds in ten are swapped. The rest keep their original version on
purpose: enemies the randomizer moved in from other levels, and the four
guests from Tomb Raider III, have no counterpart in the remaster's bank, so
they keep the sounds they arrived with. A couple more are held back by a
length check, which is what stops a swapped-in sound from being handed the
wrong audio.

Start the game once with `-log` if you want to watch it work: `log.txt` then
carries a line per level saying how many sounds came out of the bank.

### HD loading screens

The same remaster also has the loading pictures redrawn at 1920x1080. Copy them
into a folder `HD` under `screens\`:

```
screens\HD\ANDREA1.DDS   ANDREA2  ANDREA3
screens\HD\JOBY2.DDS     JOBY3  JOBY4  JOBY5
screens\HD\ANDY1.DDS     ANDY2  ANDY3
screens\HD\RICH1.DDS     RICH2  RICH3
screens\HD\TITLE.DDS
screens\HD\LOGO_DE.DDS   LOGO_EU  LOGO_FR  LOGO_US
```

Keep the names — the engine looks up the picture by the level's own file name,
which is what makes it follow the level through a shuffled campaign instead of
sticking to the slot. The startup screen is built out of two of these files:
`TITLE.DDS` is the picture, and the wordmark is cut out of the `LOGO_` file
that matches your **Language** row and blended over it, centred just under the
top edge — the remaster keeps the two apart, and either one on its own is shown
as it is if you did not copy the other.

**Nothing ships**, the same as with the sounds and the soundtracks. Any picture
you do not copy simply stays the original 640x480 one, so a half-filled folder
is fine. There is no `RICHCUT2.DDS` in the remaster and none is needed — that
level keeps Core's picture.

The originals are 4:3 and the engine has always stretched them onto your
window. The HD ones are 16:9 and are fitted instead: on a 16:9 window they land
pixel for pixel, and on anything else you get black bars rather than a
stretched picture.

The **title menu** takes its wordmark from the same `LOGO_` file. Core's own
menu logo is `data\logo512.pak`, and the German and French versions of that
file (`grlogo.pak`, `frlogo.pak`) only ever existed on the German and French
CDs — the Steam edition does not have them, so those two languages have always
fallen back to the English lettering. With the HD file in place they get their
own at last: **DIE CHRONIK**, **SUR LES TRACES DE LARA CROFT**, and for
American English the flaming emblem. The mark is fitted into the same
rectangle the old logo filled, so it keeps its shape whatever your window size
is. Without the file nothing changes and the pak logo is drawn as before.

> The wordmark is picked when the title level loads, so switching the
> **Language** row in the options changes the menu text at once but not the
> logo above it. Leave the title screen and come back — start a game and quit
> to the title, or restart — and the logo follows. This is how the pak logo has
> always behaved too.

Start the game once with `-log` to watch it: `log.txt` gets a line per picture
saying which file was decoded and at what size, or why one was refused, plus a
`Title logo:` line saying which wordmark went into the menu.

### HD from the Remaster — what it is so far

*Tomb Raider IV-VI Remastered* does not repaint the old game. Beside every
level it keeps a whole second version of the geometry — every room rebuilt and
lit, with the static objects baked into it — and a model of its own for each
object, at eight times the texture resolution. The long-term plan (#123) is for
this engine to draw that art when it finds the remaster installed, and the
original art when it does not: file present → HD, otherwise vanilla, exactly
the deal the loading screens and the HQ sounds already have.

**As of this build the rooms, the objects and Lara are drawn in HD.** Enemies,
doors, switches, pickups, traps and machinery come out of the remaster's own
models, skinned on the classic mesh tree so that they move exactly as they
always did; the rooms around them were already HD; and Lara is drawn out of the
remaster's wardrobe — see *Lara in HD* below for which of her stays classic and
why.

**What the first round of fixes changed in the rooms.** A play-through of the
whole HD path turned up a list of faults, and five of them lived in the room
renderer:

- **A flipped room now changes its art with its geometry.** A flipmap swaps
  the contents of two rooms and leaves their numbers where they are; the HD
  path had the art nailed to the number, so a wall that had gone was still
  standing as a texture.
- **Leaves are leaves again.** The remaster draws its vegetation — the kelp of
  the Deepsea Dive, the trees and grass of Ireland, the ivy of Rome — in the
  pass that has no transparency test, and the pages it draws them on are up to
  95 % hole. They came out as solid green rectangles. The test is now switched
  on for the remaster's own cut-out materials, and for no others. (The first
  attempt decided it per texture page instead, from how much of the page was
  transparent, and that was wrong: an ordinary wall's transparency channel is
  a polish and wetness mask rather than a shape, so Rome's shop fronts, the
  snow of the crane yard and the bulkheads of the submarine were eaten full
  of holes. The second round of fixes replaced the rule.)
- **Light shafts are light again, not white surfaces.** The remaster bakes
  those at double brightness and its glow meshes at the maximum; the classic
  engine's own rule stops an added glow at one, and now so does this.
- **A fog bulb is a soft cloud, not a sphere with an outline.** The formula was
  right and exact; the classic engine hides its hard rim by computing it once
  per room vertex, which a per-pixel version cannot borrow. It now fades out
  over its own edge instead. (The second round of fixes took a straight line
  out of the side of that cloud as well: the classic engine skips fog more
  than 45 degrees off the centre of the view, which never showed on the 4:3
  screens the game shipped for. On a wide screen, and above all in first
  person, that skip cut across the picture and the fog jumped away at the
  edge. It is gone; only a fog ball entirely behind the camera is skipped
  now.)
- **Only what is meant to move in the wind moves.** The remaster marks how far
  a vertex may bend, not that it bends at all, and four fifths of the marked
  vertices of Rome belong to ordinary walls, gates and lamp posts. The bend is
  now kept for the remaster's own wind material — ivy, banners, grass, kelp —
  and for flowing water, and taken away from everything else.

**What the third round changed: the light, and the sky.** The play test's
next line about the rooms was that the light was far too bright and stood
perfectly still.

- **A light shaft is a shaft again, and a lamp is a lamp.** The remaster
  builds a shaft or a lamp cone out of dozens of overlapping panes, and its
  caustic net under water is one sheet over the whole sea bed; painted at full
  strength and added on top of each other they went white. The remaster's own
  art says how strong each of them is meant to be — the artists gave every
  glow, haze and sheet a strength of its own, between a fortieth and a whole —
  and the engine now reads it. Measured on the shafts of a VCI corridor: they
  used to reach pure white, they now peak at a quarter of it, which is where
  the remaster's own picture puts them. The Deepsea Dive's sea bed went from
  98 % white to a soft shimmer.
- **And they move.** The same art gives every glow a time, from a twentieth of
  a second for a candle to sixteen seconds for a sun shaft through a hall, and
  the brightness now travels along a shaft at that speed instead of standing
  still. It goes off with `hd_motion` like everything else that moves.
- **Animated pictures run again.** The Remaster paints a monitor wall, a
  bank of ships' displays and a handful of liquid surfaces as a four page
  loop, and the engine reads the loop out of the art: the four pages and
  the time one of them stands, from a twentieth of a second to eleven
  seconds. The VCI control room's closed circuit monitors change their
  camera feed again, the scan lines flicker over them, and the liquid
  surfaces of the submarine and of Rome move. Twenty-seven such surfaces
  in five levels; they stop with `hd_motion`.
- **Water moves over its whole surface, not just at the corners.** The
  classic engine marks a water surface at the corners of its own quads,
  and the Remaster lays a far finer mesh over the same pool - on one VCI
  pool, 448 points where the classic engine has 11. The mark is carried
  along the surface's own plane now, so the whole pool shimmers instead of
  a sliver of it, and over that runs a gentle swell and a caustic net
  computed for every pixel rather than for every corner. It carries no
  brightness of its own - the classic water light is untouched - only the
  movement, and it stops with `hd_motion`.
- **Polished surfaces catch the light at a grazing angle.** The Remaster's
  artists marked which surfaces are glossy and how glossy - the marble of
  Rome, the painted steel of the submarine, 214 surfaces over twelve
  levels - and a floor seen down a long hall now lifts towards its own lit
  colour instead of staying matt. The three VCI levels carry no such mark
  anywhere and are left exactly as they were. See `hd_reflect`.
- **Destructible statics disappear when you shoot them.** A crate, a
  barred window or a pane of glass is baked into the room's art, so
  breaking it used to leave the picture of it standing. The Remaster gives
  every such copy a number of its own, and the engine now takes that copy
  out of the room when the game takes the object out of the level.
- **And the statics the Remaster has no art for are drawn again.** A room
  whose Remaster art covers only part of it used to lose every lamp,
  crate and post standing in the rest. The engine asks, per object,
  whether the art draws anything where it stands, and draws the classic
  one where it does not.
- **The sky is the Remaster's sky.** Every level that has one carries a whole
  panorama of its own past the last room — Rome at sunset with the dome of St
  Peter's, the Irish hills at dusk — painted on a sphere around the camera
  with its own drifting cloud layers over it. Thirteen of the fifteen levels
  have one; the engine draws it in place of the classic horizon mesh and the
  two flat colour layers, and leaves the classic sky whole in the two that do
  not. The moon, the stars and the lightning still draw over it: the panorama
  has clouds but no moon, and the lightning flash is painted through a layer
  the dome would otherwise have replaced.

**What the fourth round changed: the people.** The same play test, everything
that walks and talks rather than stands still.

- **The cutscene actors have faces again.** Every speaker in a cutscene stood
  there without a head — the two old men in front of the fire in the Irish
  chapter, and the guard beside them wearing a cap over nothing. The Remaster
  ships a character's face as a file of its own, and it names it after the
  PERSON while it names the body after the level slot the body fills, so the
  obvious "the face of X is X's face file" found nothing at all for a
  cutscene. It goes by the person now, out of a list measured face by face
  against the collar it has to fill — and the log names which rule found which
  face, so a missing one can be looked up rather than guessed at. Their mouths
  still do not move: the Remaster animates a face with one small file per
  spoken line, a format nothing here reads.
- **The catsuit has holsters.** With the randomizer handing Lara a gun in
  every level, the VCI catsuit — which in the original game never carries one
  — had the pistols hanging in mid air beside her thighs. The pouches, the
  straps and the belt they hang from are not part of the gun model at all;
  they are built into the outfit, and the catsuit has none. It now borrows the
  black leather rig of the Tomb Raider III London catsuit, cut out of that
  outfit and worn over its own. Only that one outfit: two others carry no rig
  either, but the Antarctica parka and the Nevada vest never wore one in Tomb
  Raider III and are finished without it — lending them the London straps only
  put black leather through an orange coat.
- **The braid lies on her pack.** It no longer walks through her backpack: the
  game only ever knew the five round shapes of the 1999 body, none of which
  reaches as far back as the HD pack, so the pack has been given two of its
  own. What it still does is hang a finger's width clear of the back of her
  skull, because the classic braid stitches its first link into the head and
  an HD link is a rigid piece. Moving the braid's anchor closed that gap and
  made the whole braid tremble instead — the anchor is where the physics
  starts, and a physics that is nudged every other frame never comes to rest —
  so the anchor is back where it was and the gap is back with it. A quiet
  braid with a small gap beat a restless one.
- **And the braid stopped twisting.** After all that it still flickered while
  she stood at a door, and the cause was not the physics at all: the game
  works each link's direction out from whole-number positions, and for a braid
  hanging straight down two of those numbers are single digits — so the *twist*
  it derives from them is not an angle but a rounding, and it flips by up to
  half a turn from one frame to the next. Tomb Raider Chronicles never showed
  that because its braid is stitched together out of soft joins; ours is six
  solid pieces of one plait, and a solid plait spun on the spot is very
  visible. Each piece is now turned about its own length instead, all six by
  the same amount, taken from the direction of the whole braid rather than of
  one link — which changes where nothing sits and only which way the plait
  faces.
- **Latex looks like latex.** Tomb Raider Chronicles has no highlight of any
  kind in its lighting, so every HD outfit came out flat matt beside the
  Remaster's own picture, where the catsuits shine. Lara's parts now carry a
  highlight whose strength says what they are made of — latex for the two
  catsuits, rubber for the diving suit, a soft wide sheen on skin, a light
  satin on the Rome top, a hard small point on a weapon. It follows the room's
  own lights, and in a dark corner there is nothing to reflect and it goes
  away. Only Lara has it for now; the creatures and the props are unchanged.

**What the fifth round changed: the order the picture is painted in.** The
play test came back with one sentence about all of it — *"everything behind a
glow is always slightly wrong"* — and that turned out to be the whole story.

- **A lamp cone is not cut off at a doorway any more.** A glow is painted on
  top of what is already there and leaves no mark of its own in the depth
  buffer, so whatever was drawn AFTER it simply painted over it — and the
  rooms were drawn one whole room at a time, so the room beyond a doorway
  always came after the glow hanging in that doorway. The frame is painted in
  two passes now: everything solid first, HD and classic together, and then
  every glow in the game over the finished picture, exactly where the original
  engine puts its own see-through polygons. The wall lamps of the VCI offices
  keep their cones through a doorway, and a glow behind a crate is behind the
  crate rather than gone.
- **The light stopped throbbing.** The travel of brightness along a shaft was
  set by hand at a quarter either way in the third round, and the play test
  called it pulsing. Measured against the Remaster's own two recordings of the
  same corridor three seconds apart, its shafts move their bright places
  around and their level barely changes — four percent over three seconds. The
  travel is a sixth of what it was; the streaks still wander, the room no
  longer breathes.
- **The last white shafts are gone.** The third round read every glow's
  strength out of the art and named four places where the art gives no usable
  number — a lift shaft in the VCI tower and three window glows in Ireland,
  which therefore kept drawing at full white. The art groups its glows into
  presets, and inside a preset the strength is all but the same number in
  every level that uses it, so those four now take their own preset's
  strength. The 13th Floor's lift shaft went from a white block to a shaft
  with streaks in it; nothing saturates anywhere a player stands.
- **Water meets the bank instead of climbing it.** The swell added in the
  third round lifted every point of a surface, the ones at the water's edge
  included, so the water rose over the coping of a pool and covered it with a
  dark band that travelled with the wave. The wave now dies away over the last
  square before the edge, which is what water does; the middle of the pool
  moves exactly as it did.
- **And the water of Rome animates at last.** The original engine answers "is
  this water" on two different marks — one for an ordinary pool, another for a
  room roofed by a reflecting ceiling — and the third round carried only the
  first of them across to the finer Remaster mesh. Ireland's water is all on
  the first mark and moved; Rome's is all on the second and stood still. Both
  are carried now. With it, a correction that had to come along: on the second
  mark the original engine says yes to nearly every surface in the room, walls
  and ceiling included, so the ripple was moving the WALLS in Rome. Only what
  lies on the water's own plane moves; the rest still catches the shimmer in
  its colour, as it always did.
- **And the room shader on disk is the room shader in the source again.** The
  checked-in bytecode had been left one edit behind its own HLSL and still
  carried an experiment the third round had measured and thrown out — 636 of
  the 512 instruction slots a shader model 3 card must provide. Nothing on the
  screen changes (the extra code was unreachable); what changes is that the
  shader now fits inside the guarantee on every card that claims it.

- **The water surface the Remaster does not draw.** Held plane by plane
  against the original level, the Remaster's own room art has **no geometry at
  all** where the original draws its water surface in 61 of the game's 93
  reflecting rooms - every VCI pool among them. An HD room therefore showed
  the pool floor with nothing over it, which is what a dark, patternless
  surface looks like: there was no water there to texture. Where the level has
  a water surface and the Remaster's art has none, the level's **own** water
  face is drawn on top of the HD room now, with its own texture, its own
  animated texture range and its own shimmer. A room whose surface the
  Remaster did build - Ireland's ponds, the Rome cistern, most of JOBY3 - is
  left to it, decided per room out of the art itself.
- **Glass is glass again.** The Remaster marks its see-through surfaces with a
  material of their own and an opacity beside it, and this build drew them
  with an alpha *test*: a texel at 55 % came out solid and one at 45 %
  vanished, so a smooth pane became a hard mask of itself - the dark shard
  pattern in the windows of the submarine. Those surfaces are blended properly
  now, after everything solid and with no depth of their own; 96 of them in
  eight levels, and nothing else in the game touched. The panes still break
  when the game breaks them: the piece is the same one Lara can shoot away.
- **The great hall's sun shafts, down to a minimum.** One tuple of numbers in
  the art - the slowest-drifting shaft in the game, 16 seconds to a cycle -
  carries the shafts of the Rome hall, and it was drawing at a peak of 58 % of
  white. It is 16 % now, faint but there. Every other shaft, cone and glow in
  the game is untouched, the VCI corridor's streaks included: they carry
  different numbers and the rule is the numbers.
- **And no ripple on the wall at the waterline.** The mark that says "this is
  water" is carried to the finer Remaster mesh by POSITION, and a wall
  standing at exactly the water's height inside a pool answers yes to it. It
  was getting the surface's own ripple and its caustic, which on a wall is a
  sheared, darkened triangle at the waterline. The geometry half now asks the
  surface to be **flat** as well; the colour still shimmers on those points,
  exactly as the original engine does.

Still open in the rooms, and named rather than hidden: a **visible border**
where an HD room meets one the remaster draws nothing for (they fall back to
the classic look, and the log's `HDLOG room=… hd=0` lines name every one), and
a **step in the wall brightness at a room boundary**, which is measured and is
the Remaster's own: it bakes each room's light on its own, and at the same
point in space the two sides of a doorway differ by up to a third of full
brightness in its own art. Nothing here widens it and nothing here can close it
without repainting the art.

**Switching it on.** Options → the new *tomb5 options* page → **HD textures**,
the last row of the first page. Like every row on that page it is remembered at
once; unlike most of them it takes effect **at the start of the next level**,
because the art is read and unpacked while the level loads. Switch it on, then
load a save or start a level.

There is no options row for the *path*, and normally there does not need to be:
the engine reads Steam's own folder out of the registry, walks every library in
`steamapps\libraryfolders.vdf`, and looks one level deep in each
`steamapps\common` for a folder that holds a `tomb456.exe` with a subfolder `5`
containing `DATA`, `TEX` and `ITEM`. It goes by what is in the folder, never by
the folder's name — Steam names the same game differently per language and
depot. The same walk also looks for a `tomb123.exe` with a subfolder `3`
holding `DATA` and `TEX`, which is *Tomb Raider I-III Remastered*: the four
Tomb Raider III guests the randomizer can put into a level have their HD
models there and nowhere else. Not having that game costs those four models
and nothing else. For a copy Steam does not know about there are registry
values, in the same key as every other setting
(`HKEY_CURRENT_USER\Software\Core Design\Tomb Raider Chronicles\tomb5`):

| Value | Type | Meaning |
|---|---|---|
| `hd_enable` | DWORD | The options row writes this. `0`, the default, and nothing below is read at all. |
| `hd_objects` | DWORD | HD models for the objects **and for Lara**, `1` by default. There is no options row for it: it is not a matter of taste but a way back if one level's models ever go wrong. Set it to `0` and the rooms stay HD while every object and Lara draw the classic way. Lara has no key of her own on purpose — she is a model like the rest of them and a level's models are one buffer, so a second switch would only be a second way to end up with half a picture. Does nothing at all while `hd_enable` is off. |
| `hd_motion` | DWORD | What **moves** in an HD room, `1` by default: water and waterfalls scrolling, ivy, banners, grass and kelp swaying in the wind, and a small ripple over a water surface, and the slow travel of light along a shaft or a lamp cone. All of it comes from the Remaster's own art — the artists marked which surfaces flow and which vertices bend, and this reads those marks. Like `hd_objects` it has no options row and is a way back rather than a taste: set it to `0` and an HD room stands perfectly still. Does nothing at all while `hd_enable` is off. |
| `hd_reflect` | DWORD | The **gloss** the Remaster's own art carries, `1` by default: the marble of Rome, the painted steel of the submarine and a few hundred other surfaces its artists marked as polished, each with a strength of its own. What they get is not a mirror - nothing is drawn twice - but the one thing a polished floor does that a matt one does not: seen at a grazing angle it lifts towards its own lit colour. Like `hd_motion` it has no options row and is a way back rather than a taste; set it to `0` and every surface is matt again. The three VCI levels carry no such mark in their art at all and look the same either way. Does nothing while `hd_enable` is off. |
| `hd_texture_budget_mb` | DWORD | How much video memory the HD art may take, in mebibytes. `0`, the default, means **work it out from the card** (see below; the card's own answer stops at 4095 MiB, which is a limit of the Direct3D 9 call and not of your card). Set a number and that number is the ceiling. |
| `hd_install` | String | The folder of your *Tomb Raider IV-VI Remastered* — the one holding `tomb456.exe`. Leave it **empty** and the engine finds it by itself. |
| `hd_install_tr3` | String | The folder of your *Tomb Raider I-III Remastered* — the one holding `tomb123.exe`. Needed for exactly one thing: the HD models of the four Tomb Raider III guests the randomizer can import (the T-Rex, the vulture, the Compsognathus and the raptor), which live in that bundle and in no other. Leave it **empty** and the engine looks for it in the same walk it uses for the other one; without the game those four enemies draw the classic way and nothing else changes. |
| `hd_texture_format` | String | `rgba`, the default, keeps every page exactly as the Remaster drew it. `dxt5` compresses them and uses a quarter of the video memory for a small, measurable loss. Anything else reads as `rgba`. |

### What the HD art asks of your graphics card

The floor is 2004 hardware: **vertex and pixel shader 3.0** (a GeForce 6 or a
Radeon X1000 and anything since), **A8R8G8B8 textures**, and a **32 bit index
buffer**. Miss one of those and the game runs classic for the rest of the
session, with the reason in the log — nothing crashes and no message box
appears. Textures up to **4096 pixels** are wanted but not required: eleven of
the Remaster's pages are that big, and a card that stops at 2048 gets those
eleven one mip level down instead of not at all. (The 32 bit index buffer is
the one real cut-off, and it is not a judgement: the Remaster's level files
number their vertices over the whole level, so a single wall tile can name
three vertices a million entries apart, and no amount of splitting brings that
under 65536. Only RICHCUT2 is small enough to draw without one.)

Video memory is the real question, and the answer is measured rather than
guessed. Kept exactly as the Remaster drew them, one level's pages come to
between **191 MiB** (JOBY4, the submarine) and **424 MiB** (ANDREA2, the
Colosseum); the title screen is 397. Of that, the rooms are the bulk — 168 to
376 MiB — the level's models add 5 to 72, and Lara's wardrobe 12 to 28. On top
of the pages come this level's HD geometry buffers (6 to 62 MiB), the classic
texture pages, which are still loaded and still drawn (2 to 24 MiB, measured
over all fifteen levels), and the back buffer with its depth buffer (25 MiB at
1920x1080). A **gigabyte** card has all of that several times over; the setting
exists for the ones that do not.

**The budget.** Left at `0`, the engine asks the card how much texture memory
it is offering at the moment the device is made, and takes off what else has to
live there: the back buffer and depth buffer at your resolution, the classic
texture pages of this level, this level's HD geometry buffers, and 64 MiB of
slack for the driver. What is left is the budget. For the title screen that
reserve is 155 MiB against 397 MiB of pages, so it loads everything at full
size on any card offering **552 MiB or more**; for the heaviest level, ANDREA2,
the number is **595 MiB**. Call it six hundred: a card that offers more than
that never cuts anything, and a two gigabyte card is nowhere near the line.

**Why the log says 4095 MiB on a big card.** The line

```
DXCreateD3DDevice Successful - the device offers 4095 MiB of texture memory
```

is not a mistake and not your card's real size. Direct3D 9 answers that
question with `GetAvailableTextureMem`, which returns a count of **bytes in an
unsigned 32 bit number**: 4096 MiB is the largest value that fits, so every
card with four gigabytes or more reports the same 4095. The figure is also
only ever an estimate — the driver rounds it, and it counts memory the system
may lend the card as well as the memory on it. None of that matters for what
the number is used for. It is read once, to decide whether a level's pages fit
with room to spare, and every card that hits the 4095 ceiling is nearly seven
times over the six hundred the heaviest level needs. If the device answers
nothing at all, the budget is switched off rather than guessed, and the log
says 0.

**What happens over budget.** Never a missing page — a page that is not there
is a hole in a wall and reads as a fault. Instead the **largest pages give up
their largest mip level**, biggest first, until what is left fits: a 512 pixel
page becomes 256, a 4096 one becomes 2048. Texture coordinates do not notice,
so the wall is softer and nothing else changes — no colour shift, no missing
surface, no seam. The log names the budget, what the level wanted, and how many
pages gave up how many levels. The same rule catches a card that cannot hold a
4096 pixel texture at all: those pages start one level down and the reason is
in the log.

**The other way, `hd_texture_format = dxt5`.** Instead of shrinking the pages
it compresses them, on the processor, while the level loads. Video memory drops
to a quarter — ANDREA1's rooms from 280 to 70 MiB — and every page keeps its
full size and its whole mip chain, so distant detail survives where the budget
would have blurred it. The cost is compression noise: measured over sixteen
pages of the Rome levels it is 40.3 dB peak signal to noise on average and
35.4 dB at worst, which shows up as slight banding on smooth gradients and is
hard to see on the Remaster's stone, plaster and metal. It costs about one and
a half seconds of loading on eight cores. **`rgba` remains the default and the
recommendation**; `dxt5` is there for a card that would otherwise have to blur.

If the card genuinely runs out of video memory in the middle of a level load,
that level goes back to classic as a whole — never half of it — and the log
names the page it failed on.

**The light in an HD room now does everything it did before.** An earlier
build drew the remaster's rooms with the remaster's baked light and nothing
else, so a torch did not brighten an HD wall and three option switches were
dead inside one. That is over: every effect the engine computes for a classic
room vertex is computed for an HD room too, from the same numbers, at the same
moment, and hanging off the same switch.

| What | Where you see it | The switch |
|---|---|---|
| Torch, flare, muzzle flash, explosions | any dark room you carry a flare into | — |
| Distance fog | anywhere with a long view | *Distance fog*, page 2 |
| Fog bulbs | the Rome levels — Colosseum, Trajan's Markets, the Base | *Fog* |
| Water shimmer on a surface | any pool, and the mist rooms | *Shimmer* |
| Underwater darkening and the screen wobble | swimming, anywhere | — |
| Red Alert's green gas wobble | Red Alert! | *Shimmer* |

Three things are worth knowing about how close it is. The **numbers** are the
engine's own, proved: both formulas were written out on the processor and run
against each other over a room's own vertices, and the worst disagreement is
one part in 255 — the rounding the classic path does when it truncates its own
sum. The **brightest surfaces** stay brighter than the classic engine would
draw them: TR5 stops multiplying the texture at a certain point and starts
adding instead, which is a trick from the days of fixed graphics hardware, and
the HD path keeps multiplying — which is how the remaster's own art is meant to
be lit and how the HD rooms already looked before this change. And the **water
pattern** is finer: the table is sampled once per vertex, and an HD room has
far more vertices than a classic one. Same table, same speed, same strength — a
denser grid.

### HD objects — what draws, what does not, and why

An object is not a room. A room is one lump of geometry with one matrix; an
enemy is a tree of fifteen to thirty pieces that the engine walks every frame,
pushing and popping a matrix per piece and blending between two animation
frames. The HD model wants exactly those matrices, so the engine does not
build them a second time: it rides along on the walk it already does. The
animation, the frame blending and the mesh tree stay the game's own, down to
the last rounding — only the last step changes, where a piece's classic mesh
used to go into the texture buckets and its **matrix** now goes to the model
instead.

**How many.** Per level, of the slots the level really loads: ANDREA1 draws
50 of its 52 models in HD, JOBY3 65 of 66, RICH3 65 of 68. The rest, and every
slot with no model at all, draws exactly as it did before — the two paths sit
side by side in the same frame and in the same depth buffer.

**When an object stays classic.** Four reasons, and every one of them is
written into the log with the slot it applies to:

| Reason | What it means |
|---|---|
| `no_model` | The remaster ships no model under that name. Mostly emitters, markers and inventory-only items — things that draw nothing in the world anyway. |
| `donor_dummy` | The slot's model came from another level file and that level says, in its own name table, that the slot draws nothing on purpose. |
| `tr3_not_installed` | The slot holds one of the four Tomb Raider III guests and *Tomb Raider I-III Remastered* was not found. Its model lives only there. |
| `skeleton_nodes` | The model names a mesh-tree node the slot does not have. `FIREROPE` in eleven of the fourteen levels: TR5's slot is a rope with one piece and the model comes from TR4, where it had seventeen. |
| `skeleton_drift` | The model fits the node **count** but not the tree. This is the randomizer's case: it puts foreign creatures into slots, and `SAS` in ANDREA1 holds a twenty-five-piece body where `SAS.TRM` was drawn for fifteen. Skinning that would tear the model across the room. |
| `no_geometry` | A model with nothing in it at all. Has not been seen; the guard is there so that it cannot become a crash. |
| `baked` | Not classic at all: **nothing** draws it. The level's own name table says `DUMMY` for that slot, which is the remaster saying it draws nothing there as an object — usually because it has baked the thing into the room's art instead. Rome's lamp posts and its wall lanterns are the clearest case: draw the classic mesh on top and the lamp stands twice, a hand's width apart, which is exactly what the first play-through showed. Only in a room that really draws HD; a room that stayed classic keeps its lamp post. |

**Which model a slot asks for** comes from three places, in this order: the
level's own name table beside it in the remaster's `DATA` folder, which lists
only the deviations; a small hand table of our own for the slots this project
added past the original 460 and for the one vanilla slot the remaster leaves
unnamed — `ROMAN_GOD`, for which it ships a sword version and a hammer version
and only two of the fifteen levels say which; and otherwise the slot's own
name. A randomized level adds a fourth: a slot the run filled from a donor is
asked of the **donor's** table instead, because that is where the model came
from.

The skeleton test is a measurement, not a guess. For every pair of pieces that
carry geometry it compares the distance between them in the model against the
distance between them in the slot's own mesh tree, with each piece's own size
taken off — so where the modeller put an origin does not matter and a model
that sits off-centre as a whole cancels completely. Over all fourteen
randomized levels and their vanilla twins the worst model that **belongs**
where it stands is 239 units out and the best one that does not is 308, and
the line is drawn at 256 — nearer the good end on purpose, because a good
model refused is a classic-looking door and a bad one accepted is a body torn
in half. `ModelProbe -all` prints the whole table for any level.

**What an HD object cannot do yet.**

* **Mesh swaps are answered with a second model, where the remaster ships
  one.** TR5 keeps a second mesh per piece and swaps it in — a guard's hands
  the moment he is created, the Cyborg's damage panels as he is shot, the
  Mafia's pistols coming out of their holsters. A remaster model is one whole
  thing and has no second geometry for a piece, but for two of those cases it
  ships a whole second *model*, and the engine draws that instead: the Mafia
  in the white shirt has one file with his guns holstered and one with them in
  his hands, and the Cyborg has eleven, one per damage stage, cumulative in
  the order the game opens his panels. Every other swap in the game is a
  soldier's hands, and there the remaster needs no second file because its
  Mafia, SWAT and guard models already hold the weapon. What is never done is
  painting the classic swap mesh over the HD body — that is the road TROne
  took and reversed after its own users reported classic patches on an HD
  model.
* **A piece the model does not cover draws nothing** — not even its classic
  mesh. That is deliberate: a door's invisible root and a face that ships as
  its own `_HEAD.TRM` are *meant* to be empty, and drawing the classic mesh
  there put classic patches on an HD body in TROne's own user reports.
* **A separate face brings its neck with it.** Where the remaster ships the
  face as its own file — Pierre, Larson, the guards, Von Croy — that file is
  not only the face: it carries the neck and a piece of the collar as well,
  and Pierre's reaches 165 units below the joint the head turns on. Bolting
  the whole file to the head node made the neck swing with the skull and tear
  a hole at the collar, which is what his death animation showed. The one
  piece of that file that hangs below the head now follows the node the head
  hangs off — the torso — with the file's own weights kept, so the seam bends
  instead of tearing.
* **Distance models are off** for HD objects. TR5 swaps in a coarser model
  five sectors away; the remaster has no coarse model, and a door that turned
  from HD to classic as you walked away from it would pop every time.
* **A glow can be painted over.** A model's additive pass — glowing eyes, a
  lamp — is drawn before the classic opaque objects, so a classic object
  standing *behind* such a glow paints over it. Rare, and the same hole the
  rooms have had since they went HD.

**The light is the engine's own, per vertex.** The classic engine lights an
object vertex by vertex out of a light list it builds per piece; a flat colour
per piece would have made a mannequin of every guard, with the front and the
back of his chest equally bright. So the light list itself goes to the card
and the shading is worked out per vertex there, with the remaster's own
normals — the room's lamps, the room's sun, the shadow lights that darken the
ambient, and every dynamic light the game makes: a flare, a torch, a muzzle
flash, an explosion. Two of those are finer than the classic engine could
afford: a point light's direction and its falloff are measured from the
surface itself rather than from the middle of a limb, so a flare at an enemy's
feet lights his boots and not his helmet.

**The proof, in the log.** With `-log` the first HD object of each level
writes two lines. `HDLOG itemcheck` puts each of its pieces on the screen
twice — once through the classic transform and once through the matrix the
graphics card is handed — and prints the worst disagreement, which is under a
pixel. `HDLOG lightcheck` runs the classic lighting formula on the engine's
own light arrays and the HD one on the list that was sent to the card, on the
same surface normal, and prints both colours side by side.

**The actors of a cutscene draw HD as well.** A cutscene does not run the
ordinary object path: the actors are packed animation nodes played back by a
routine of their own, with their own frame buffer and their own light. Lara
was HD there from the start because she is drawn by her own routine either
way, so a cutscene showed an HD Lara talking to a 1999 Von Croy until the same
seam was put into the actor routine. One actor stays classic on purpose: the
one the script fades in and out of the picture. That fade is a half-transparent
draw an HD model has no pass for, and a fade that jumped to solid halfway
would be worse than a classic model.

**A room the remaster has nothing for stays classic**, on its own, room by
room. In ANDREA1 that is four rooms out of 242. A room the randomizer built
from scratch (a secret's new chamber) is in the same position and behaves the
same way: classic, without being asked.

**A room the randomizer RESHAPED is a different case, and it has to be told.**
The remaster's art describes the level as Core shipped it. When a run cuts a
wall open for a secret, adds a window, joins two rooms or lifts a floor, the
art still shows the wall that is no longer there — and unlike a room the
remaster never had, nothing in the file gives it away. So the randomizer
writes those room numbers into a small text file beside the level,
`data\<LEVEL>.hdm`, and the engine draws each of them the classic way, whose
geometry is by definition the geometry the run really built. It costs the HD
look of a handful of rooms per level — six in RICH3 with the glitched secret
list, two in ANDREA3 — and it is the difference between a level you can play
and a wall you can walk through but not see through.

The same file names the object slots whose **model came out of another level
file**. The remaster resolves a slot by the name *this* level gives it, and
`ANIMATING10` is a different object in every one of the fifteen files — so a
cross enemy, a moved key or a mesh warehouse would be painted as whatever
this level's `ANIMATING10` happens to be. Each of those lines names the donor
level and the slot the model was taken from, and the engine reads the
**donor's** `.MAP` for it. A guest out of Tomb Raider III is written `TR3` and
resolved out of the other bundle (see `hd_install_tr3` above).

The file is written on every run for every level, even when it has nothing to
say, and "restore original" deletes it again; a level with no such file has
no markers, which is what a vanilla installation looks like. It is plain text
and you can read it. A file this engine does not understand — a newer
randomizer, a version line it has no rule for — is refused whole, with the
reason in `log.txt`, and the level then draws the way it did before any of
this existed.

One thing that will look wrong if you go looking for it: a static object you
destroy — a vase, a crate — disappears from the game but not from the room's
art, because that art is one baked piece. It belongs to the stage that draws
objects.

The sky stays the engine's own. TR5 draws it procedurally, before the rooms and
without depth, and the HD walls paint over it exactly as the classic ones did.
The remaster keeps a sky dome of its own in the same file — one extra group per
level, measured — and it is deliberately not drawn.

**The log.** With `-log`, `log.txt` gets a block of `HDLOG` lines per level.
The ones worth reading:

```
HDLOG level=ANDREA1 trg=v3 groups=243 rooms=242 sky=1 empty=4 pages=179 ...
HDLOG room=0 group=0 tris=1216/0/0 ranges=4 statics=2 hd=1 reason=ok
HDLOG room=66 group=66 tris=0/0/0 ranges=0 statics=0 hd=0 reason=empty_group
HDLOG level=ANDREA1 built ms=1180 rooms=242 hd=238 classic=4 sky_groups=1 ...
HDLOG level=ANDREA1 pages named=179 distinct=179 from5=179 ... mib=279.9 ...
HDLOG level=ANDREA1 statics suppressed=255 kept=0 (in 238 HD rooms, 4 classic)
HDLOG level=ANDREA1 water marks=... rooms=...
HDLOG level=ANDREA1 water stamped=... choppy=... shimmer=... ps_slots=...
HDLOG matrixcheck level=ANDREA1 room=12 group=12 samples=128 used=85 behind=43
      maxdx=0.00017 maxdy=0.00009 maxdz=0.000000000 persp=572.0 center=960,540
HDLOG frame rooms=17 calls=131 dropped=0 bulbs=0 fog_start=12288 wibble=930
      cam_uw=0 shimmer=1 objects=9 object_calls=21
HDLOG frame objects hd=6 classic=4 baked=3 dropped=0 calls=20 swap_drawn=1
      swap_ignored=0 meshbits=0 clipped_lights=0(level)
HDLOG slot=33 name=SWAT model=SWAT nodes=15 maxbone=14 cover=15
      drift=22/114 head=-1 hd=1 reason=ok
HDLOG slot=31 name=SAS model=SAS nodes=25 maxbone=14 cover=15
      drift=807/2286 head=-1 hd=0 reason=skeleton_drift
HDLOG slot=436 name=ANIMATING11 model=- nodes=2 ... hd=0 reason=baked
HDLOG level=ANDREA1 map=read lines=42 baked_slots=14
HDLOG level=ANDREA1 swap slot=469 name=CYBORG kind=hitman_stages
      models=11 missing=0
HDLOG level=ANDREA1 objects hd=50 classic=111 baked=14 (of 161 loaded ...
HDLOG donor slot=45 name=LARSON from=Andrea1:45 model=LARSON hd=1
      reason=donor_slot_name
HDLOG donor slot=43 name=CROW from=TR3:43 model=VULTURE hd=0
      reason=tr3_not_installed
HDLOG level=RICH1 donors=15 donor_maps=4 map_lines=93 tr3_install=absent
HDLOG itemcheck level=ANDREA1 slot=33(SWAT) nodes=15 used=15
      maxdx=0.00021 maxdy=0.00013
HDLOG lightcheck level=ANDREA1 slot=33 classic=... hd=... diff=...
```

A randomized level adds one more room reason and a block of its own. A room
the run reshaped reads `hd=0 reason=marker`; the level's `markers` line names
the sidecar it read, how many rooms and donor slots it held, and whether the
Tomb Raider I-III bundle was found:

```
HDLOG level=RICH3 markers file=data\RICH3.hdm rooms=6 donors=14 outfit=0
      tr3_install=absent
HDLOG room=46 group=46 tris=980/0/0 ranges=3 statics=1 hd=0 reason=marker
HDLOG level=RICH3 markers rooms=6 of them classic now=6 donors=14 outfit=0
```

and one `donor` line per named slot says where its name came from and whether
the model arrived: `donor_map` (the donor level's own `.MAP` named it),
`donor_slot_name`, `donor_dummy` (the donor says that slot draws nothing),
`model_missing`, `tr3_guest`, `tr3_not_installed`, or the skeleton test's own
reason when the model was there and did not fit.

`hd=1/0` per room with its reason is the whole decision table; `built ms=` is
what the HD side added to the loading time; `matrixcheck` is the engine
comparing its own vertex transform with the matrix it hands the graphics card,
over 128 of the room's own vertices — `maxdx`/`maxdy` are pixels and they have
to stay far below 1, or an HD wall would stand beside its own floor.

There is one `slot=` line per object slot the level loads: the model it asked
for, how many pieces the mesh tree has against the highest the model names,
how much of it is covered, the skeleton drift, and `hd=1/0` with the reason.
That table answers "why is this door still the old one" without guessing.
`itemcheck` is the object twin of `matrixcheck` — the same comparison on an
object's pieces instead of a room's vertices — and `lightcheck` puts the
classic lighting formula and the HD one side by side on one surface normal.

The two `water` lines say how many of the classic path's water vertices were
found again in the remaster's geometry — `marks` is what the classic level
offered, `stamped` is what was matched. The `frame` line repeats the option
state the HD rooms are actually drawing with, so a "the fog does nothing"
report can be answered from the log: `fog_start` is `Distance fog` times 1024,
`bulbs` is zero whenever `Fog` is off, `shimmer` is the switch itself. And a
`clipped` line appears only if a room ever wanted more than eight dynamic
lights or more than eight fog bulbs at once — if you never see one, the
shader's maxima never bit:

```
HDLOG level=TITLE clipped first frame lights=0 bulbs=1 room=-1
      offered_lights=0 offered_bulbs=9 (maxima 8 / 8)
HDLOG level=TITLE clipped summary frames=173 lights=0 bulbs=173
      worst_lights=0(room -1) worst_bulbs=9 (maxima 8 / 8)
```

The first line is written by the first frame that clipped, the second at the
end of the level — **never once per frame**, which is what an earlier version
did and what got the fog bulb maximum raised from six to eight in the first
place. `offered_` is what the engine wanted, the `maxima` are what the shader
has room for. Eight is a measured number, not a guess: counted out of all
sixteen level files, a level holds up to seventeen fog bulbs (`ANDREA2`, and
`TITLE` has seventeen too), and sweeping every camera position a level allows,
up to sixteen of them can be alight at once — 16 in `ANDREA2`, 14 in `RICH2`,
13 in `ANDY1`, 11 in `TITLE`, 10 in `RICH1`. So a `clipped bulbs` line in those
levels is expected: the bulbs that are dropped are the ones furthest down the
engine's own list. Eight is where the title screen stopped visibly losing one;
the engine's own ceiling is sixteen.

If the HD path refuses, it says so in one line and the game runs on classic —
no error box, no changed picture:

```
HDLOG level=ANDREA1 build refused reason=the device has no vertex/pixel shader 3.0
```

**Memory.** The remaster's pages are BC7, a format Direct3D 9 has never heard
of, so every one of them is unpacked on the processor and handed to the card as
plain 32-bit pixels — no quality lost, which was your call. For the heaviest
level that is 376 MB of texture. Kept in video memory it costs the process
**270 MB** at its peak; kept in Direct3D's *managed* pool, where the driver
holds a second copy in main memory, it costs **545 MB**. The first is what this
engine does. Unpacking them costs about a second of the loading time on a
modern processor — the `built ms=` number above.

**Alt+Enter and the resolution keys.** Changing the resolution or toggling
full screen throws the graphics device away and makes a new one. Everything the
HD path owns lived on the old one, so it is released at that moment and the
rooms go back to classic **until the next level starts**. That is deliberate: a
picture you can explain beats a crash.

**Nothing is copied out of the remaster and nothing of it ships here**, the
same rule as everywhere else in this project: the art is read where it lies, in
your own installation.

### Lara in HD

Lara was the last thing on the screen that was still the original model, and
she is the hardest one, because she is not an object. Every other creature is
one slot and one model file. Lara is a **composite**: the engine draws her out
of five slots at once — the skinned body from `LARA_SKIN`, the seams that close
it from `LARA_SKIN_JOINTS`, the hands from whichever weapon slot the mesh swap
put there this frame, the thigh straps from `LARA_HOLSTERS*` and the braid from
`HAIR` — and the remaster does the same thing with a wardrobe of separate
models.

**She is drawn now.** Her fifteen joint matrices are the engine's own, built by
the same animation code as before, so she moves exactly as she always did; what
changed is which geometry hangs off them. Which parts are on her changes from
frame to frame and is read from the engine's own state: which slot owns each
hand this frame, what is in the two thigh holsters, what is on her back.

**Which outfit a level wears** is decided out of the engine's own facts, not
from a list of level names:

| Lara | Where | The model | How it is recognised |
|---|---|---|---|
| The Rome outfit | Streets of Rome, Trajan's Markets, Colosseum | `OUTFIT_CLASSIC` | everything the three rules below do not claim |
| The Russia outfit, with the cap | Base, Submarine, Sinking Submarine | `OUTFIT_RUSSIA` | the engine's **own** braid test: a Russia level with `LARA_NORMAL`, or `LARA_BUNHEAD` anywhere. That expression already stands three times in the braid code and once in the skinning code, and it is the only thing in this engine that can tell the cap from the Rome outfit — a vanilla Russia level carries the same outfit byte Rome does. |
| The diving suit | Deepsea Dive | `OUTFIT_SUBSUIT` | the outfit byte, which has a value of its own for it |
| The VCI catsuit | The 13th Floor, Escape with the Iris, Red Alert, the title screen | `OUTFIT_VCI` | the outfit byte, likewise |
| Young Lara | Gallows Tree, Labyrinth, Old Mill | — **stays classic** | the script flag `GF_YOUNGLARA`, or the outfit byte `LARA_YOUNG`. Either one. **Not the level range** — see the note below the table. This is your decision, not a limit: `OUTFIT_YOUNG`, `LARA_YOUNG_HEAD` and `HAIR_PIGTAIL` all exist in the remaster. |
| A Tomb Raider III outfit | wherever the randomizer rolls one | `OUTFIT_TR3_ANTARC`, `_NEVADA`, `_COAST`, `_CATSUIT` | `data[3]` of the level header, which the randomizer writes — see below |

Read against all fifteen vanilla level files those rules give the right answer
fifteen times out of fifteen.

**Why young Lara is not the Irish levels.** She used to be, and it was wrong
in a randomized run. The engine builds the child body at exactly two places,
and both of them ask the level range **and** the script flag together — the
flag is what puts the child's ribcage over the skin. The randomizer clears
that flag out of `SCRIPT.DAT` for all three Irish levels, because young Lara
can carry no weapon at all, and puts a grown Lara's skin in the level; the
range on its own therefore said "child" about an adult and drew the whole
Irish section the classic way. The flag and the outfit byte both change when
the randomizer changes her, so either of them is a true answer and the range
is not. In a vanilla game nothing moves: those three levels carry the flag
*and* the byte.

**The rest of the wardrobe.** The face is `LARA_HEAD` and it is a *facial rig*
— its bone numbers run to 31 and mean nothing at all to her 15-piece mesh tree
— so it is nailed rigidly to the head, the same way an enemy's separate face
is. The braid is `HAIR_OG`, six bones over 280 units, which is exactly the six
links TR5's braid has, measured out of the level files themselves. The hands
come from `HAND_BARE_*`, one file per weapon, and the worn weapons from
`HOLSTER_*`: pistols and uzis on both thighs, the revolver on the right one
alone, the shotgun, the HK and the grappling gun on the back. An **empty**
holster draws nothing extra on purpose — the straps are painted into every
outfit already, which is why the game's empty-holster slot has no model in the
remaster either.

Two frames need two hand models at once and get them: a flare or a torch is in
the left hand while the right is empty, and the crowbar is in the right hand
while the left is empty. Each model is then masked to its own arm, so no hand
is drawn twice.

**Only what the level can use is loaded.** The whole wardrobe is 21 models,
3.5 MB of geometry and 30 MB of texture; a level that has no HK slot cannot
hand her an HK, so its wardrobe is smaller. In practice: Streets of Rome 11
models and 20 MB, Sinking Submarine 16 and 27 MB, The 13th Floor 6 and 17 MB,
Deepsea Dive 2 and 12 MB — the diving suit needs neither hands nor braid nor
holsters, it *is* all three.

**Does it fit her skeleton?** The same pairwise measurement the objects are
judged by, and the answer is comfortable: over every level the mean drift of a
wardrobe part against that level's own Lara mesh tree is 3 to 9 units and the
worst single pair 183, against a limit of 256.

**The Tomb Raider III outfits** were the last gap and are closed. The
randomizer rolls one of the four (#122) and writes the outfit byte
`LARA_PONYTAIL` when it does — but that is the same byte it writes for the
Rome outfit inside a Russia level, so the byte alone can say neither *which*
of the four she is wearing nor that she is wearing one at all. It now writes
a marker beside it: `data[3]` of the level header, four bytes the engine
already reads before it loads a single room, carrying the engine's own
outfit number so that neither side has to translate. `0` in every vanilla
file — measured over all seventeen — and 5 to 8 for the four guests.

With the marker there, the `LARA_PONYTAIL` byte means one thing again and
not two: the Rome body inside a Russia level, `OUTFIT_CLASSIC`. The one case
left over is a level file randomized by a version **older** than this
marker: it carries the ponytail byte and a `data[3]` of 0 for a guest as
well, and now shows a Rome Lara where that run rolled a Nevada one. Nothing
in the file can tell those apart — that is what the marker was added for —
and randomizing the level again with the current version says it.
`log.txt` prints the raw facts per level in the `HDLOG … lara …
laratype=… data3=…` line.

**What else stays classic, on purpose:**

| What | Why |
|---|---|
| Young Lara | your decision. The models exist in the remaster. |
| The skeleton cheat (`skelebob`) | its own draw path and its own overlay meshes; the wardrobe has neither. |
| The muzzle flash, the gun shells, the flare in flight | drawn by their own routines outside `DrawLara` and never part of the composite. |
| The diving suit's two thrusters | they are a separate object on her torso, not a part of her body, and the remaster's wardrobe has no model for them. They keep being drawn the classic way **on top of** the HD suit — two small low-resolution pieces, against a diving suit with nothing on its back. |
| The catsuit's sheen | the classic catsuit is drawn through the engine's environment map, a second view-dependent pass an HD model does not have. The remaster's own `OUTFIT_VCI` carries its shading in its textures, so the HD catsuit is matte where the classic one glints. Not faked: inventing a specular term would be inventing a look. |
| The inventory | the ring draws its own objects and never calls `DrawLara`. |

**First person** (#126) drops the same three things from the HD Lara that it
drops from the classic one: the head, the face and the braid, all three of
which sit exactly where the camera does. The braid's *physics* keeps running
the whole time — `HairControl` is part of the game loop and not of the draw —
so switching back to third person shows it hanging where it belongs on the
very next frame.

Two things then happen that have no counterpart in the classic body, and both
are dealt with on **her body's draw alone**, so that her hands, her holsters
and the weapon on her back are never touched (#123, stage S8):

- **The neck goes with the head.** The classic head *mesh* is head and neck in
  one piece; an HD body is a single skinned surface whose neck stump hangs off
  the **torso**, so masking the head node leaves it standing. Measured against
  the eye the camera really uses, that stump comes to **18.6 units** — inside
  the near plane. Everything within **60 units** of the eye is therefore
  discarded in the shader. Sixty is a *radius*, not a depth: the nearest
  shoulder or chest vertex of any of the eight outfits is 68.9 units away, so
  one number separates the two from every angle, while a cut at a fixed depth
  would lie flat across her chest the moment she looks down and open a window
  through her (measured, and rejected for it).
- **Her body is drawn one-sided there.** The HD path draws every model double
  sided; the classic engine only *looks* as if it does, because it culls back
  faces in software before the card ever sees one. No camera but this one
  stands inside a body, and with the camera in her neck the difference is the
  whole picture: the inside of her own chest. Outside first person nothing
  changes — the same view of her with and without the culling is identical to
  the pixel.

*Left over, and honestly:* in the **diving suit** the helmet is modelled on the
torso node too, so it cannot be masked off with the head and it is far too big
for a 60-unit cut. Looking down inside it you see the helmet from within. The
classic suit hides it, because there the helmet is part of the head mesh.

**In a mirror room** she is drawn twice, as before. The engine mirrors her by
*moving* her — the position reflected in the plane, the angles negated — which
is a rotation and keeps every triangle's winding, so the first person culling
above is right there too. Proved with a picture through a view matrix of
negative determinant.

**The binoculars and the laser sight never reach the HD path at all.**
`BinocularCamera` sets `lara_item->mesh_bits = 0`, and `game/draw.cpp` asks
`if (lara_item->mesh_bits && !SCNoDrawLara)` before it calls `DrawLara` — so
nothing of her is put on the HD frame list while the scope is up, and leaving
it puts `mesh_bits` back to −1 in the same place it was always put back. The
scope's own mask, the pickup display and the fade lie **above** the HD world:
the HD rooms and objects are drawn immediately before `DrawBuckets`, and the
depth clear that separates the world from the overlays (#127) comes after all
of it. `AlterFOV` moves the field of view *inside* the frame while zooming, and
the HD path takes its projection when the world is **collected** rather than
when it is drawn, which is the same field of view the classic path transformed
its vertices with.

**The two proofs in the log.** Start with `-log` and the first HD Lara frame of
every level writes two lines. `HDLOG laracheck` puts each of her fifteen joint
origins on the screen twice — once through the classic transform, once through
the matrix the graphics card is handed — and reports the worst disagreement in
pixels; `HDLOG braidcheck` takes each braid link's own frame, steps it one bone
along, and reports how far it misses the next link. Both are the same checks
`ModelProbe -laracheck` runs without the game, where they read 0.00000 px and
0.00002 units.

A third line, `HAIRLOG`, is written **every frame** for every braid: which
anchor offset that frame used, where the root of the chain landed, how many
collision spheres it was tested against, how many of its links the backpack
pushed, where the tip ended up, how far the root and the tip travelled since
the last frame, and the worst one-frame twist of any link — once as the game
works it out and once as the picture really shows it. It is there because the
braid's physics carries state from frame to frame — the anchor moving between
two places on alternating frames is what once made it tremble — and a
per-frame trace is the only thing that shows it. Standing still, the travel
figures go to zero and stay there and the twist figure stays in single digits;
anything else is worth a bug report. `ModelProbe -braidsim` runs the same
chain offline, without a game around it, and says whether a braid ever comes
to rest; `ModelProbe -braidanim` runs it under Lara's own animations, which is
where the twist was caught.

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
| Enemy hit points | 32 lines, `HP_<enemy>`, absolute as in the original |
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

Keys now count like puzzle items — a second key of the same type is kept
instead of being thrown away, and the ring shows how many you carry; the save
format grew with it, so savegames from an older build are refused rather than
loaded.

---

## What plays differently from the original

These are deliberate. A randomized run walks into situations Core never had to
think about, and each of these is one of them.

**Three enemies can now be killed.** The chef, the skeletons and the swamp
witch had hit points and took damage, but nothing ever checked whether they
had run out. In the original it never showed: the chef's level takes Lara's
weapons away first, the skeletons' level hands her nothing that hurts them,
and the swamp witch only wakes when the level is as good as over.

**The cyborg comes in two versions.** The one in Red Alert is untouched: no
weapon hurts him, and he dies only in the gas or by the short circuit in the
water — that is the puzzle, and the body has to stay lying there afterwards.
None of that can travel. The one the enemy pool places is a second copy on a
slot of its own: he can be shot, his damage panels blow open as he takes
hits, and when he goes down he shorts out, lies there for five seconds and
fades like any other corpse. He has his own two lines in `Balance.txt`
(`HP_Cyborg`, `DMG_Cyborg_Shot`), so tuning him leaves Red Alert alone. He is
also the only enemy in the game that can follow Lara along a monkey swing.

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
| it felt like it stuttered, or dropped under 30 | `FPSLOG` — one line per second: frames presented, and the longest gap between two of them |

These seams are permanent, not debug leftovers. They exist so that a report
carries something to work with.

**The frame rate, measured.** "It felt slow in that room" is not something
anybody can check by looking, so with `-log` the engine writes one `FPSLOG`
line per second of wall clock, and one summary per level:

```
FPSLOG level=ANDREA1 frames=30 min_ms=32.9 max_ms=34.1 avg_ms=33.3 slow=13
       late=0 hd_rooms=17 hd_items=9 hd_draws=152
FPSLOG level=ANDREA1 frames=24 min_ms=33.0 max_ms=66.9 avg_ms=41.6 slow=11
       late=6 hd_rooms=23 hd_items=14 hd_draws=241
FPSLOG level=ANDREA1 summary seconds=418 frames=12461 worst_ms=83.4
       slow_frames=5602 late_frames=39
```

The game's logic is a fixed **30 frames a second** — that is the engine, not
this build — so a healthy second reads `frames=30` and every gap between two
presented frames sits on 33.3 ms. Read the line like this:

- **`frames=`** is the frame rate itself. Anything under 30 is a second in
  which the game did not keep up.
- **`late=`** counts the frames that actually missed their slot (over 41.7 ms).
  This is the number that matters. `worst_ms`/`max_ms` say how badly.
- **`slow=`** counts every gap over the 33.3 ms budget. Half of a perfectly
  healthy second lands here on measurement jitter alone, so a large `slow=`
  with `late=0` means nothing at all.
- **`hd_rooms` / `hd_items` / `hd_draws`** are the heaviest HD frame of that
  second — rooms drawn, objects drawn, draw calls — so a drop can be put next
  to what was on the screen when it happened. They are zero with the HD path
  off, and `FPSLOG` still works: the frame clock has nothing to do with
  `hd_enable`.

The measurement is taken where the finished picture is handed to the graphics
card, so every frame counts once — the game, the inventory, the title menu, the
loading bar and the movies alike. Without `-log` not one clock is read.

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
