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
    [string[]]$Levels = @('Andrea1','Andrea2','Andrea3','andy1','Andy2','andy3','joby2','joby3','joby4','joby5','rich1','rich2','rich3','richcut2'),
    # JEDEN gueltigen Sektor zusaetzlich mit diesem Typ fuellen. 0 = aus.
    #
    # Wozu: die Constraint-Typen zeigen, wo ein KEY-Item liegen darf. Ein
    # gewoehnliches Pickup darf dagegen ueberall liegen, wo ueberhaupt etwas
    # liegen darf - also auf jedem Sektor des Exports. Beide Ebenen
    # uebereinander ergeben genau die Frage, die der Nutzer beantwortet haben
    # will: welche ITEMARTEN sind auf diesem Sektor zugelassen?
    #
    # ALLE, nicht der Rest. Der erste Versuch fuellte nur die Sektoren, die kein
    # Constraint abdeckt - dann fehlte der 223-Marker ueberall dort, wo auch ein
    # Key-Item liegen darf, und in Andrea1 sahen 654 Sektoren so aus, als waeren
    # sie fuer normale Pickups gesperrt. Die Ebenen sollen sich ueberlappen.
    #
    # Sinnvoll ist ein Typ, der im Level vorkommt und sich in trview leicht
    # herausfiltern laesst - 223 (Pickup Item4, das Secret) hat sich bewaehrt.
    [int]$FillAllWith = 0
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
    #------------------------------------------------------------------------
    # Level ohne Schluesselschloesser trotzdem pruefbar machen.
    #
    # Sechs der vierzehn Level haben keine Key-/Puzzle-Constraints - joby4,
    # Andy2, andy3, rich2, rich3, richcut2 - und wurden bisher uebersprungen.
    # Damit gab es fuer sie kein aktuelles Testlevel und keine Moeglichkeit,
    # die Kuration in trview gegenzusehen. Genau das ist bei joby4 aufgefallen:
    # der Nutzer schaute auf eine vier Tage alte Datei aus einem Werkzeug, das
    # es nicht mehr gibt.
    #
    # Ersatzweise wird ein einzelner Constraint gebaut, der ALLE gueltigen
    # Sektoren abdeckt. Als Typ dient ein Pickup, das im Level wirklich
    # vorkommt - ein Typ ohne Eintrag in der Objekttabelle waere im Spiel
    # unsichtbar. Bevorzugt wird der haeufigste, dann ist er in trview leicht
    # ueber die Typliste zu finden.
    #------------------------------------------------------------------------
    # Mit -FillAllWith braucht es den Ersatz-Constraint gar nicht: die Fuellebene
    # deckt ohnehin jeden Sektor ab, und ein zweiter Marker desselben Typs auf
    # demselben Feld waere nur Rauschen.
    if ($constraints.Count -eq 0 -and $FillAllWith -gt 0) {
        "  ${lvl}: keine Key-Constraints - es zaehlt allein die Fuellebene"
    }
    elseif ($constraints.Count -eq 0) {
        $cand = @($d.items | Where-Object { $t=[int]$_.typeId; ($t -ge 172 -and $t -le 241) -or ($t -ge 334 -and $t -le 355) } |
                  Group-Object typeId | Sort-Object Count -Descending)
        if ($cand.Count -eq 0) {
            "SKIP ${lvl} - keine Constraints und kein einziges Pickup im Level, nichts zu zeigen"
            continue
        }
        $genericType = [int]$cand[0].Name
        $maxZ = if($null -ne $z.maxZone){ [int]$z.maxZone } else { 0 }
        $constraints = @([PSCustomObject]@{
            typeId = $genericType; maxZone = $maxZ; instances = $(if($cand.Count){[int]$cand[0].Count}else{0})
            reusable = $false; excludeRooms = @(); allowRooms = @()
            minFloorY = $null; minFloorYZone = $null
            islandSectors = $null; minFloorYIsland = $null })
        "  ${lvl}: keine Key-Constraints - alle gueltigen Sektoren mit Typ $genericType gefuellt"
    }

    # Waffenfreie Raeume. Sie haengen am Level, nicht am Typ, und treffen jeden
    # Waffentyp - auch einen, der hier nur als Ersatz-Constraint auftaucht.
    # Heute ist das wirkungslos (nur joby3 hat solche Raeume, und joby3 hat
    # echte Key-Constraints), aber der Randomizer setzt es durch, und die
    # Testlevel muessen dasselbe zeigen wie das, was am Ende erzeugt wird.
    $noWeapon = @( if($null -ne $z.noWeaponRooms){ $z.noWeaponRooms | ForEach-Object { [int]$_ } } )
    $wTypes   = @( if($null -ne $z.weaponTypes){ $z.weaponTypes | ForEach-Object { [int]$_ } } )

    # Platzierungsversatz je Typ (TYPE_OFFSET, im Export schon fuer dieses Level
    # aufgeloest). Muss hier genauso angewandt werden wie im Randomizer, sonst
    # zeigt das Testlevel die Items woanders, als sie im Seed landen - und genau
    # dafuer sieht der Nutzer es sich an.
    $offOf = @{}
    foreach($to in @($z.typeOffsets)){
        if ($null -eq $to) { continue }
        $offOf[[int]$to.typeId] = @{ dx=[int]$to.dx; dy=[int]$to.dy; dz=[int]$to.dz }
    }

    $itemAdds = @()
    $abgedeckt = New-Object System.Collections.Generic.HashSet[string]
    foreach($c in $constraints){
        $typIstWaffe = $wTypes -contains [int]$c.typeId
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
        # Sie gilt NUR innerhalb der Inselgruppe (minFloorYIsland), denn sie ist
        # deren Eigenschaft. An die Zone gehaengt war sie zu weit gegriffen:
        # Zone 0 umfasst in joby2 7756 Sektoren, die Insel nur 1900 - fuer Key
        # Item7, das ausserhalb der Insel liegen darf, wirkte die Schranke
        # dadurch im halben Level (vom Nutzer bemerkt).
        # islandSectors: die Startinsel aus dem zweiten Flood. Ist sie gesetzt,
        # ist sie eine harte Positivliste - Key Item8 in joby2 darf ausschliess-
        # lich dort liegen, weil die Flipmap am Kran ihn selbst voraussetzt.
        $isl = $null
        if ($null -ne $c.islandSectors) {
            $isl = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.islandSectors){ [void]$isl.Add([string]$k) }
        }
        # requireInIsland ist KEINE Schranke fuer die einzelne Instanz, sondern
        # eine Aussage ueber alle zusammen: mindestens eine muss dort liegen, die
        # uebrigen duerfen ueberall hin. Genau genommen ist der "erlaubte
        # Bereich" dieses Typs also das ganze Level - und genau so hat das
        # Testlevel es zuerst gezeigt, mit 552 Flaschen quer durch rich2. Das ist
        # zwar korrekt, aber unbrauchbar: der Nutzer will die Pflichtzone sehen,
        # denn nur die hat er festgelegt. Deshalb wird sie hier gezeichnet, wenn
        # es keine echte Insel gibt. Beim Lesen also mitdenken - was hier
        # leuchtet, ist "hier muss eine liegen", nicht "nur hier darf eine liegen".
        elseif ($null -ne $c.requireInIsland) {
            $isl = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.requireInIsland){ [void]$isl.Add([string]$k) }
        }
        # bannedSectors: die Umkehrung von islandSectors - hier darf der Typ
        # gerade NICHT liegen. andy1s Zwillenhaelften duerfen nicht vor dem
        # Teleporter liegen, sonst geht Lara ohne sie durch und kommt nie
        # zurueck.
        # forbiddenSectors: hinter der eigenen Tuer - Schnitt ueber alle
        # Schloesser dieses Typs, siehe ZoneAnalyse.
        $fb = $null
        if ($null -ne $c.forbiddenSectors) {
            $fb = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.forbiddenSectors){ [void]$fb.Add([string]$k) }
        }
        $bn = $null
        if ($null -ne $c.bannedSectors) {
            $bn = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.bannedSectors){ [void]$bn.Add([string]$k) }
        }
        $hs = $null
        if ($null -ne $c.minFloorYIsland) {
            $hs = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.minFloorYIsland){ [void]$hs.Add([string]$k) }
        }
        $eligible = @($z.sectors | Where-Object {
            $kk = "$($_.room),$($_.sectorX),$($_.sectorZ)"
            ($null -eq $isl -or $isl.Contains($kk)) -and
            ($null -eq $bn -or -not $bn.Contains($kk)) -and
            ($null -eq $fb -or -not $fb.Contains($kk)) -and
            (-not $typIstWaffe -or $noWeapon -notcontains [int]$_.room) -and
            [int]$_.zone -le [int]$c.maxZone -and $ex -notcontains [int]$_.room -and
            ($al.Count -eq 0 -or $al -contains [int]$_.room) -and
            ($null -eq $c.minFloorY -or ($null -ne $hs -and -not $hs.Contains($kk)) -or
             [int]$_.floorY -ge [int]$c.minFloorY) })
        $off = if ($offOf.ContainsKey([int]$c.typeId)) { $offOf[[int]$c.typeId] } else { @{ dx=0; dy=0; dz=0 } }
        foreach($s in $eligible){
            $rm = $rooms[[int]$s.room]
            if (-not $rm) { continue }
            $kk2 = "$($s.room),$($s.sectorX),$($s.sectorZ)"
            $wx = [int]$rm.originX + [int]$s.sectorX*1024 + 512 + $off.dx
            $wz = [int]$rm.originZ + [int]$s.sectorZ*1024 + 512 + $off.dz
            $itemAdds += [PSCustomObject]@{ typeId=[int]$c.typeId; room=[int]$s.room; x=$wx; y=([int]$s.floorY + $off.dy); z=$wz }
            [void]$abgedeckt.Add($kk2)
        }
    }

    # Die Ebene der gewoehnlichen Pickups: JEDER gueltige Sektor bekommt den
    # Marker, auch die, auf denen zusaetzlich ein Key-Item liegen darf.
    if ($FillAllWith -gt 0) {
        $offR = if ($offOf.ContainsKey($FillAllWith)) { $offOf[$FillAllWith] } else { @{ dx=0; dy=0; dz=0 } }
        $n = 0
        foreach($s in $z.sectors){
            $rm = $rooms[[int]$s.room]
            if (-not $rm) { continue }
            $wx = [int]$rm.originX + [int]$s.sectorX*1024 + 512 + $offR.dx
            $wz = [int]$rm.originZ + [int]$s.sectorZ*1024 + 512 + $offR.dz
            $itemAdds += [PSCustomObject]@{ typeId=$FillAllWith; room=[int]$s.room; x=$wx; y=([int]$s.floorY + $offR.dy); z=$wz }
            $n++
        }
        $nurKey = @($z.sectors | Where-Object { -not $abgedeckt.Contains("$($_.room),$($_.sectorX),$($_.sectorZ)") }).Count
        "  ${lvl}: $n Sektoren mit Typ $FillAllWith belegt, davon $nurKey ohne jede Key-Range"
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
