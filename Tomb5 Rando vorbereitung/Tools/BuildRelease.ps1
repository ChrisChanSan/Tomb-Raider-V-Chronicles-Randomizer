# Baut den auslieferbaren Ordner zusammen: Randomizer, Engine, mitgelieferter
# Quelltext und die Lizenzen.
#
# Warum der VOLLSTAENDIGE Engine-Quelltext mit muss und nicht nur unsere
# geaenderten Dateien: TOMB5 steht unter der GNU GPL v3. Unsere Fassung ist ein
# abgeleitetes Werk, und GPLv3 Paragraph 6 verlangt bei der Weitergabe eines
# Binaerprogramms den vollstaendigen zugehoerigen Quelltext - nicht einen Satz
# Aenderungen plus Verweis auf das Ursprungsprojekt. Der Baum ist 8 MB gross,
# ihn komplett beizulegen kostet also praktisch nichts und macht die Frage
# gegenstandslos. Paragraph 5a verlangt zusaetzlich einen deutlichen Hinweis auf
# die Aenderung samt Datum: das ist TOMB5 Code\MODIFICATIONS.md.
#
# Was hier BEWUSST NICHT hineinkommt: Spieldaten. Weder Level noch Musik,
# Videos oder Sprachdateien. Tomb Raider Chronicles gehoert seinen
# Rechteinhabern; der Nutzer bringt seine eigene Installation mit, und der
# Randomizer kopiert sie sich beim ersten Start selbst zurecht.
param(
    [string]$OutDir = "",
    [switch]$Clean
)
$ErrorActionPreference = "Stop"

# Alles relativ zum Skriptort (Tools -> zwei Ebenen hoch = Repo-Wurzel),
# damit das Skript auf jedem Rechner laeuft und keine Entwickler-Pfade
# traegt (#72).
$Root      = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if ($OutDir -eq "") { $OutDir = Join-Path $Root "Release Paket" }
$EngineSrc = "$Root\TOMB5 Code"
$EngineExe = "$Root\release\PCtomb5.exe"
$RandoRoot = "$Root\TR5Randomizer"
$RandoExe  = "$RandoRoot\build\TR5Randomizer.exe"
# Die MIT-Lizenz kommt aus dem VERSIONIERTEN imgui (external\imgui) - der
# lose Download-Ordner unter "Tomb5 Rando vorbereitung" ist untracked und
# existiert in einem frischen Clone gar nicht.
$ImGui     = "$Root\TR5Randomizer\external\imgui"
$ZoneTools = "$Root\Tomb5 Rando vorbereitung\Tools"
$LevelInfo = "$Root\Tomb5 Rando vorbereitung\Level Infos"

function Say($t) { Write-Output $t }

if ($Clean -and (Test-Path $OutDir)) {
    Say "Raeume $OutDir"
    Get-ChildItem $OutDir -Force | Remove-Item -Recurse -Force
}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null

# --- 1. Engine ---------------------------------------------------------------
if (-not (Test-Path $EngineExe)) { throw "Engine nicht gebaut: $EngineExe" }
Copy-Item $EngineExe "$OutDir\PCTomb5.exe" -Force
Say ("Engine        : PCTomb5.exe ({0:N0} KB)" -f ((Get-Item $EngineExe).Length/1KB))

# --- 2. Randomizer -----------------------------------------------------------
if (Test-Path $RandoExe) {
    Copy-Item $RandoExe "$OutDir\TR5Randomizer.exe" -Force
    Say ("Randomizer    : TR5Randomizer.exe ({0:N0} KB)" -f ((Get-Item $RandoExe).Length/1KB))
} else {
    Say "Randomizer    : noch nicht gebaut - uebersprungen"
}

# --- 3. Vorberechnete Daten, die der Randomizer liest ------------------------
# Damit braucht das Werkzeug zur Laufzeit KEIN PowerShell. Das war eine
# ausdrueckliche Vorgabe: eine PowerShell-Abhaengigkeit waere fuer normale
# Nutzer unangemessen und faellt ausserdem regelmaessig Virenscannern auf.
$dataOut = "$OutDir\data"
New-Item -ItemType Directory -Path $dataOut -Force | Out-Null
Copy-Item "$LevelInfo\VanillaLevelChecksums.csv" $dataOut -Force
$zones = 0
foreach ($f in (Get-ChildItem "$LevelInfo\*.json" | Sort-Object Name)) {
    if ($f.BaseName -eq 'Title') { continue }
    & "$ZoneTools\ZoneAnalyse.ps1" -LevelJson $f.FullName -LevelName $f.BaseName `
        -ExportZonesJson "$dataOut\zones_$($f.BaseName).json" *> $null
    Copy-Item $f.FullName "$dataOut\level_$($f.Name)" -Force
    $zones++
}
Say "Daten         : $zones Zonen-Exporte + Level-Infos + Pruefsummenliste"

# --- 4. Quelltext, GPLv3-Pflicht ---------------------------------------------
$srcOut = "$OutDir\Source\TOMB5"
New-Item -ItemType Directory -Path $srcOut -Force | Out-Null
$skipDirs = '\\(Build|Release|Debug|x64|\.vs|ipch)\\'
$skipExt  = @('.obj','.pdb','.exe','.dll','.ipdb','.iobj','.tlog','.log','.user','.aps')
$copied = 0
foreach ($f in (Get-ChildItem $EngineSrc -Recurse -File)) {
    if ($f.FullName -match $skipDirs) { continue }
    if ($skipExt -contains $f.Extension.ToLower()) { continue }
    $rel = $f.FullName.Substring($EngineSrc.Length).TrimStart('\')
    $dst = Join-Path $srcOut $rel
    New-Item -ItemType Directory -Path (Split-Path $dst -Parent) -Force | Out-Null
    Copy-Item $f.FullName $dst -Force
    $copied++
}
Say ("Quelltext     : $copied Dateien ({0:N1} MB) - GPLv3-Pflicht" -f `
    ((Get-ChildItem $srcOut -Recurse -File | Measure-Object Length -Sum).Sum/1MB))

# Der Randomizer ist ein eigenstaendiges Programm, das nur Datendateien liest
# und schreibt - kein abgeleitetes Werk der Engine. Sein Quelltext geht
# trotzdem mit, weil das ohnehin der Anspruch ist.
$rsOut = "$OutDir\Source\TR5Randomizer"
New-Item -ItemType Directory -Path $rsOut -Force | Out-Null
Copy-Item "$RandoRoot\src" $rsOut -Recurse -Force
Say "Quelltext     : Randomizer mitgeliefert"

# --- 5. Lizenzen -------------------------------------------------------------
$lic = "$OutDir\Licenses"
New-Item -ItemType Directory -Path $lic -Force | Out-Null
Copy-Item "$EngineSrc\LICENSE" "$lic\TOMB5-GPLv3.txt" -Force
if (Test-Path "$ImGui\LICENSE.txt") { Copy-Item "$ImGui\LICENSE.txt" "$lic\DearImGui-MIT.txt" -Force }
Say "Lizenzen      : GPLv3 (Engine), MIT (Dear ImGui)"

# --- 6. Anleitung ------------------------------------------------------------
$readme = @'
TR5 Randomizer
==============

Was hier drin ist
-----------------
  TR5Randomizer.exe   Das Werkzeug. Hier stellst du den Seed ein.
  PCTomb5.exe         Die Spiel-Engine, nativ fuer Windows 11.
  data\               Vorberechnete Daten, die der Randomizer braucht.
  Source\             Der vollstaendige Quelltext von beidem.
  Licenses\           Die Lizenzen.

Du brauchst zusaetzlich deine eigene Installation von Tomb Raider
Chronicles. Spieldaten sind hier bewusst KEINE enthalten - das Spiel
gehoert seinen Rechteinhabern.

So faengst du an
----------------
  1. TR5Randomizer.exe starten.
  2. Beim ersten Start fragt er nach dem Ordner deiner
     Chronicles-Installation. Beim ersten Randomisieren legt er
     data\Backup an und sichert deine Originallevel dorthin.
     Randomisiert wird danach direkt in data - du startest das
     Spiel also wie gewohnt.
  3. Seed eingeben oder wuerfeln lassen, randomisieren, spielen.

Der Seed
--------
Derselbe Seed erzeugt immer dasselbe Spiel - auf jedem Rechner. Wenn
dir etwas komisch vorkommt, notiere den Seed und die Build-Kennung;
damit laesst sich genau dein Spielstand nachbauen. Beides wird im
Pausenmenue angezeigt, ein Screenshot reicht also.

Jede Eingabe ist ein gueltiger Seed. "Weihnachten" funktioniert.

Wenn etwas schiefgeht
---------------------
Im Randomizer gibt es "Auf Original zuruecksetzen". Damit werden alle
Level aus data\Backup zurueckgespielt. Diesen Ordner bitte nicht
loeschen - er ist die einzige Kopie deiner Originallevel, die das
Programm kennt.

Hinweis zu Spielstaenden: das Speicherformat wurde gegenueber dem
Original geaendert. Alte Spielstaende werden mit einer Meldung
abgelehnt statt stillschweigend kaputtzugehen - beende einen
laufenden Durchgang also, bevor du auf eine neue Version wechselst.

Herkunft und Lizenz
-------------------
Die Engine ist eine modifizierte Fassung von TOMB5, dem
Dekompilationsprojekt zu PC Tomb Raider Chronicles von Troye
(Trxyebeep) und Mitwirkenden:

    https://github.com/Trxyebeep/TOMB5

TOMB5 steht unter der GNU GPL v3, und diese modifizierte Fassung
ebenso. Deshalb liegt der vollstaendige Quelltext bei - du kannst die
EXE selbst nachbauen und musst uns nicht glauben. Was wir geaendert
haben und warum, steht in Source\TOMB5\MODIFICATIONS.md.

Die Oberflaeche des Randomizers benutzt Dear ImGui von Omar Cornut
(MIT-Lizenz). Fuer HQ-Musik und das Hintergrundbild stecken stb_vorbis
und stb_image von Sean Barrett drin (Public Domain).

Ein besonderer Dank geht an trview (https://github.com/chreden/trview) -
ohne dieses Werkzeug waere keine einzige Zonengrenze dieses Randomizers
pruefbar gewesen.
'@
[System.IO.File]::WriteAllText("$OutDir\LIESMICH.txt", $readme, (New-Object System.Text.UTF8Encoding($false)))
Say "Anleitung     : LIESMICH.txt"

# --- 7. Dieselben Daten in den Build-Ordner spiegeln -------------------------
# Beim Entwickeln startet man die EXE aus build\, nicht aus dem Paket - und
# dort fehlte data\, was zu einer korrekten, aber unnoetigen Fehlermeldung
# fuehrte. Die paar Dateien doppelt vorzuhalten ist billiger, als sich das
# jedes Mal zu merken.
if (Test-Path (Split-Path $RandoExe -Parent)) {
    Copy-Item $dataOut (Split-Path $RandoExe -Parent) -Recurse -Force
    Say "Build-Ordner  : data\ gespiegelt, EXE laeuft auch von dort"
}

Say ""
Say "Fertig: $OutDir"
