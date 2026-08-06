# Erzeugt je Vanilla-Level eine .trc, in der die ECHTEN Key-/Puzzle-Item-Typen
# dieses Levels auf allen Sektoren liegen, in denen sie laut Zonen-Flood
# platzierbar waeren. Nutzerwunsch: das ist leichter zu lesen als eine
# generische, nur nach Zonennummer eingefaerbte Alias-Palette - man sieht in
# trview direkt "Key Item1" ausgewaehlt und jeder Sektor leuchtet auf, der fuer
# genau diesen Typ erlaubt ist.
#
# Ablauf pro Level:
#   1. ZoneAnalyse.ps1 -ExportZonesJson liefert je validForItems-Sektor Zone+Y,
#      dazu keyConstraints: [{typeId, maxZone, instances, reusable}, ...] - die
#      im Level tatsaechlich vorkommenden Key-/Puzzle-Typen mit ihrer lockersten
#      "muss in Zone <= N liegen"-Grenze (das Maximum ueber alle Slot-Instanzen
#      dieses Typs, falls mehrere Schloesser denselben Typ brauchen).
#   2. Fuer jeden Constraint: auf jedem Sektor mit zone <= maxZone ein Item
#      dieses typeId platzieren. Ein Sektor kann so mehrere uebereinanderliegende
#      Items bekommen, wenn mehrere Key-Typen dort gleichzeitig erlaubt sind -
#      gewollt, trview unterscheidet sie ueber die Typ-Liste.
#   3. Levels ohne abgeleitete Key-Constraints (z.B. Andy2) werden ausgelassen -
#      da gibt es nichts Key-Spezifisches zu zeigen.
#   4. Patch-JSON (itemAdds) wird gebaut, TombItemEdit2026.exe -patchlevel
#      wendet ihn auf die Vanilla-.trc an und schreibt das Ergebnis in den
#      Zielordner.
#
# Y-Konvention verifiziert: Item.y == Sektor.floorY exakt (kein Offset),
# X/Z = Raumursprung + Sektorindex*1024 + 512 (Sektormitte) - gegengeprueft an
# einem realen Vanilla-Item (Andrea1 Key Item1, Item 107).
param(
    [string[]]$Levels = @('Andrea1','Andrea2','Andrea3','andy1','Andy2','andy3','joby2','joby3','joby4','joby5','rich1','rich2','rich3','richcut2')
)
$ErrorActionPreference = "Stop"

$LevelInfoDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Level Infos"
# Bewusst der Backup-Unterordner und NICHT data\ selbst: seit der Randomizer
# eine Oberflaeche hat, wird data\ regelmaessig randomisiert - die Originale
# liegen dann in data\Backup. Zeigte dieses Skript weiter auf data\, wuerde es
# still auf bereits randomisierten Leveln rechnen. Nichts wuerde abstuerzen,
# die Zonen waeren einfach falsch. Genau dieser Fall ist schon einmal
# aufgetreten und hat eine Stunde Fehlersuche gekostet, siehe die
# Pruefsummenpruefung weiter unten.
$VanillaDir   = "C:\Cloude AI\Tomb5 Native win 11 fix\Vanilla Tomb Raider (V) Chronicles Steam Version ENG\data\Backup"
$OutDir       = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Level Test\KeyItemRange Level Test"
$ZoneScript   = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Tools\ZoneAnalyse.ps1"
$Exe          = "C:\Cloude AI\Two gun test\TombItemEdit2026\Release\Beta\TombItemEdit2026.exe"
$ScratchDir   = "C:\Users\dev\AppData\Local\Temp\claude\C--Cloude-AI\b7de1344-2e88-43df-b893-8f3cb52ddbcd\scratchpad"

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

#----------------------------------------------------------------------------
# Pruefsummen der Quelle kontrollieren, bevor irgendetwas erzeugt wird.
#
# Der Randomizer hat diese Absicherung seit jeher; den Vorbereitungswerkzeugen
# fehlte sie, und das ist aufgeflogen, als der Zielordner eines GUI-Tests
# derselbe war, aus dem hier gelesen wurde. Das Ergebnis sah plausibel aus und
# war falsch - die unangenehmste Sorte Fehler. Lieber laut abbrechen.
#----------------------------------------------------------------------------
$ChecksumCsv = Join-Path $LevelInfoDir "VanillaLevelChecksums.csv"
if (-not (Test-Path $ChecksumCsv)) { throw "Pruefsummenliste fehlt: $ChecksumCsv" }
$expect = @{}
foreach ($row in (Import-Csv $ChecksumCsv)) { $expect[$row.FileName] = $row.SHA256.ToUpper() }

$bad = @()
foreach ($lvl in $Levels) {
    $trc = Join-Path $VanillaDir "$lvl.trc"
    if (-not (Test-Path $trc)) { $bad += "$lvl.trc fehlt"; continue }
    $have = (Get-FileHash $trc -Algorithm SHA256).Hash.ToUpper()
    if (-not $expect.ContainsKey("$lvl.trc")) { $bad += "$lvl.trc nicht in der Liste"; continue }
    if ($have -ne $expect["$lvl.trc"]) { $bad += "$lvl.trc weicht von Vanilla ab" }
}
if ($bad.Count -gt 0) {
    Write-Output "ABBRUCH - die Quelle ist nicht unveraendert:"
    $bad | ForEach-Object { Write-Output "  $_" }
    Write-Output "Quellordner: $VanillaDir"
    throw "Quelle nicht vanilla - es wurde nichts erzeugt."
}
Write-Output "Quelle geprueft: $($Levels.Count) Level unveraendert ($VanillaDir)"

$results = @()
foreach($lvl in $Levels){
    $infoJson = "$LevelInfoDir\$lvl.json"
    $srcTrc   = "$VanillaDir\$lvl.trc"
    if (-not (Test-Path $infoJson)) { "SKIP $lvl - kein Level-Info-JSON"; continue }
    if (-not (Test-Path $srcTrc))   { "SKIP $lvl - keine Vanilla-.trc gefunden"; continue }

    $d = Get-Content $infoJson -Raw | ConvertFrom-Json
    $rooms = @{}; foreach($r in $d.rooms){ $rooms[[int]$r.index] = $r }

    $zonesJson = "$ScratchDir\zones_$lvl.json"
    & $ZoneScript -LevelJson $infoJson -LevelName $lvl -ExportZonesJson $zonesJson 2>&1 | Out-Null
    if (-not (Test-Path $zonesJson)) { "SKIP $lvl - Zonen-Export fehlgeschlagen"; continue }
    $z = Get-Content $zonesJson -Raw | ConvertFrom-Json
    $constraints = @($z.keyConstraints)
    if ($constraints.Count -eq 0) {
        "SKIP ${lvl} - keine Key-/Puzzle-Constraints abgeleitet, nichts zu zeigen"
        continue
    }

    $itemAdds = @()
    foreach($c in $constraints){
        # excludeRooms: Raeume, die zwar in der Zone liegen, in denen dieser eine
        # Typ aber trotzdem nicht liegen darf (Nutzerangabe aus trview).
        $ex = @( if($null -ne $c.excludeRooms){ $c.excludeRooms | ForEach-Object { [int]$_ } } )
        # allowRooms ist eine Positivliste: ist sie gesetzt, zaehlt nur noch
        # sie (die Zone bleibt trotzdem Obergrenze). Leer = keine Einschraenkung.
        $al = @( if($null -ne $c.allowRooms){ $c.allowRooms | ForEach-Object { [int]$_ } } )
        # minFloorY: Hoehengrenze der Inselgruppe. Kleineres y liegt in TR HOEHER,
        # ein Sektor unterhalb dieser Schranke ist also zu HOCH - typischerweise
        # die Oberseite eines Kistenstapels, auf den Lara erst mit dem Schluessel
        # kommt, der dort liegen soll.
        $eligible = @($z.sectors | Where-Object {
            [int]$_.zone -le [int]$c.maxZone -and $ex -notcontains [int]$_.room -and
            ($al.Count -eq 0 -or $al -contains [int]$_.room) -and
            ($null -eq $c.minFloorY -or [int]$_.floorY -ge [int]$c.minFloorY) })
        foreach($s in $eligible){
            $rm = $rooms[[int]$s.room]
            if (-not $rm) { continue }
            $wx = [int]$rm.originX + [int]$s.sectorX*1024 + 512
            $wz = [int]$rm.originZ + [int]$s.sectorZ*1024 + 512
            $itemAdds += [PSCustomObject]@{ typeId=[int]$c.typeId; room=[int]$s.room; x=$wx; y=[int]$s.floorY; z=$wz }
        }
    }

    $patch = [PSCustomObject]@{ itemAdds = $itemAdds } | ConvertTo-Json -Depth 4 -Compress
    $patchPath = "$ScratchDir\patch_$lvl.json"
    # UTF-8 ohne BOM - der handgeschriebene JSON-Reader in TombItemEdit2026
    # scheitert sonst am Vorspann (siehe frueherer BOM-Bugfix im Projekt).
    [System.IO.File]::WriteAllText($patchPath, $patch, (New-Object System.Text.UTF8Encoding($false)))

    $dstTrc = "$OutDir\$lvl.trc"
    if (Test-Path $dstTrc) { Remove-Item $dstTrc -Force }
    # & mit einzelnen Pfad-Argumenten zerlegt leerzeichenhaltige Pfade
    # (Projektordner heisst "Tomb5 Native win 11 fix" etc.) falsch - deshalb
    # ueber Start-Process mit sauber gequoteter ArgumentList aufrufen.
    $argList = @('-patchlevel', "`"$srcTrc`"", "`"$dstTrc`"", "`"$patchPath`"")
    $p = Start-Process -FilePath $Exe -ArgumentList $argList -Wait -PassThru -NoNewWindow
    $ok = (Test-Path $dstTrc) -and $p.ExitCode -eq 0
    $typeList = ($constraints | ForEach-Object {
        $x = @( if($null -ne $_.excludeRooms){ $_.excludeRooms } )
        $a = @( if($null -ne $_.allowRooms){ $_.allowRooms } )
        "$($_.typeId)<=Z$($_.maxZone)$(if($x.Count){" -$($x.Count)R"})$(if($a.Count){" nur$($a.Count)R"})"
    }) -join ', '
    $results += [PSCustomObject]@{ Level=$lvl; Types=$constraints.Count; Items=$itemAdds.Count; OK=$ok }
    "{0,-10} Typen=[{1}]  Items={2,5}  ExitCode={3}  {4}" -f $lvl,$typeList,$itemAdds.Count,$p.ExitCode,($(if($ok){'OK'}else{'FEHLER'}))
}

Write-Output ""
Write-Output "=== Zusammenfassung ==="
$results | Format-Table -AutoSize
