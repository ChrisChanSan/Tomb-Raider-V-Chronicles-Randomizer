# Third-party code in this repository

This project is GPLv3 (see [LICENSE](LICENSE)). It builds on other people's
work, and this page says whose, under what terms, and where the full text of
each licence lives in this tree. Nothing here is a summary you should rely on
instead of the licence itself — every entry points at the authoritative text.

## The engine

**TOMB5** — Troye / Trxyebeep and contributors,
<https://github.com/Trxyebeep/TOMB5> — **GPLv3**.
Everything under `TOMB5 Code/TOMB5/` is a modified copy of that decompilation.
What was changed and why is recorded file by file in
[TOMB5 Code/MODIFICATIONS.md](TOMB5%20Code/MODIFICATIONS.md), which is also the
dated notice of modification GPLv3 section 5(a) asks for.
Full text: `TOMB5 Code/LICENSE`.

**tomb3** — Troye / Trxyebeep, <https://github.com/Trxyebeep/tomb3> — **GPLv3**.
The control code of four Tomb Raider III enemies (T-Rex, raptor,
Compsognathus, vulture) was taken over directly rather than rewritten, so that
its origin stays visible. Each of those files names its source in its own
header; the whole arrangement is set out in section 3 of MODIFICATIONS.md.
Same licence as this project, which is what allows the combination.

## Libraries compiled into the binaries

| Library | Licence | Full text in this tree |
|---|---|---|
| **Dear ImGui** — ocornut, <https://github.com/ocornut/imgui> | MIT | `TR5Randomizer/external/imgui/LICENSE.txt` |
| **zlib** — Jean-loup Gailly and Mark Adler | zlib licence | notice at the top of `TOMB5 Code/TOMB5/tomb5/libs/zlib/zlib.h` |
| **stb_vorbis** — Sean Barrett, <https://github.com/nothings/stb> | MIT **or** public domain, your choice | notice at the end of `TOMB5 Code/TOMB5/tomb5/libs/stb/stb_vorbis.c` |
| **stb_image** — Sean Barrett, <https://github.com/nothings/stb> | MIT **or** public domain, your choice | notice at the end of `TR5Randomizer/src/libs/stb/stb_image.h` |

### discord-rpc — shipped in the source tree, not in the binaries

| Library | Licence | Full text in this tree |
|---|---|---|
| **discord-rpc** — Discord, Inc., <https://github.com/discord/discord-rpc> | MIT | `TOMB5 Code/TOMB5/tomb5/libs/discordRPC/LICENSE` |

discord-rpc arrived from upstream TOMB5 as two prebuilt static libraries and
two headers, with no licence file. It is Troye's Discord Rich Presence, and
**this fork switches it off**: `TOMB5 Code/TOMB5/tomb5/tomb5.cpp` opens with
`#define TOMB5_DISCORD_RPC 0`, and every call site sits inside `#if
TOMB5_DISCORD_RPC`. The shipped `PCTomb5.exe` accordingly contains no
occurrence of the string `Discord` at all.

The notice still has to travel, because the two `.lib` files and their headers
are part of the source tree this project distributes — shipping someone's MIT
code without its notice is a violation whether or not a compiler used it. The
licence file above was added for that reason and is the project's own verbatim
copy of the upstream text.

The release package carries all of these in its `Licenses\` folder;
`Preparation/Tools/BuildRelease.ps1` puts them there.

## Credited, but not contained here

trview (chreden), TRmod (b122251), TR Viewer (E. Popov) and Tomb Raider
Chronicles itself are named in the credits because the work would not exist
without them. None of their code is in this repository.

## No game data

No Tomb Raider content ships with this project — no levels, no scripts, no
sounds, no music, no videos, no language files, no textures. The randomizer
reads what it needs from the player's own installation at run time, and the
four Tomb Raider III enemies are read from a Tomb Raider III installation the
same way. Without those installations, the affected features simply do not
appear.
