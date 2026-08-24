# TR5 Randomizer

Ein Randomizer für **Tomb Raider Chronicles** (PC) — samt einer nativ auf
Windows 11 laufenden Fassung der Spiel-Engine.

*A randomizer for Tomb Raider Chronicles (PC), including a natively running
Windows 11 build of the game engine (D3D9 + XAudio2, no wrappers). Game data
is **not** included — bring your own installation. Everything is GPLv3.*

## Was das ist

Zwei Programme, die zusammenarbeiten:

- **TR5Randomizer** würfelt aus deiner Chronicles-Installation ein neues
  Spiel: Items, Schlüssel und Waffen wandern, Gegner tauschen die Rollen und
  reisen zwischen den Leveln (bis hin zu Gästen aus Tomb Raider III), Outfits,
  Musik, Secrets und auf Wunsch die Levelreihenfolge. Derselbe Seed erzeugt
  auf jedem Rechner dasselbe Spiel.
- **PCTomb5** ist eine modifizierte Fassung von [TOMB5](https://github.com/Trxyebeep/TOMB5),
  dem Dekompilationsprojekt zur PC-Engine: Direct3D 9 und XAudio2 statt
  DirectDraw/DirectSound, dazu die Erweiterungen, die der Randomizer braucht —
  und ein paar Extras wie eine umschaltbare Ego-Sicht.

Beides steht unter der **GNU GPL v3**, der vollständige Quelltext liegt hier.

## Was du brauchst

Eine eigene Installation von **Tomb Raider Chronicles**. Spieldaten sind in
diesem Projekt bewusst **nicht** enthalten — weder Level noch Musik, Videos
oder Sprachdateien. Das Spiel gehört seinen Rechteinhabern; der Randomizer
patcht zur Laufzeit deine eigenen Dateien und sichert vorher alles nach
`data\Backup`.

## Bauen

- **Engine:** `TOMB5 Code\tomb5.vcxproj` mit Visual Studio (Win32, Release).
- **Randomizer:** `TR5Randomizer\build\b1.bat` (Kommandozeile) und
  `b2.bat` (Oberfläche) — beide laufen relativ zum Repo, Visual Studio mit
  C++-Werkzeugen vorausgesetzt.
- **Paket:** `Tomb5 Rando vorbereitung\Tools\BuildRelease.ps1` baut den
  auslieferbaren Ordner samt Quelltext und Lizenzen zusammen.

## Dank und Herkunft

- **[TOMB5](https://github.com/Trxyebeep/TOMB5)** von Troye (Trxyebeep),
  ChocolateFan, thatguyfrombb und Raildex — ohne dieses Dekompilat gäbe es
  hier gar nichts.
- **[trview](https://github.com/chreden/trview)** von chreden — das Werkzeug,
  mit dem jede einzelne Zonengrenze dieses Randomizers geprüft wurde.
- **[Dear ImGui](https://github.com/ocornut/imgui)** von Omar Cornut (MIT) —
  die Oberfläche.
- **stb_vorbis / stb_image** von Sean Barrett (Public Domain) — HQ-Musik und
  Hintergrundbild.

Was an der Engine geändert wurde und warum, steht in
`TOMB5 Code\MODIFICATIONS.md`; die Anleitung für Spieler liegt dem
Release-Paket als `LIESMICH.txt` bei.
