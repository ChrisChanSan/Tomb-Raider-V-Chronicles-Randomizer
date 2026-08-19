# Erzeugt ein Testlevel, in dem die WAND-PICKUPS eines Levels auf JEDER
# gefundenen Wandkante liegen - zum Ansehen in trview (#61).
#
# Warum: die vier OCB-Pickups (Brechstange OCB 2, Joby-Crow OCB 9) muessen an
# einer Wand haengen und mit dem richtigen Winkel dazu. Ob unsere Wandsuche
# taugt, entscheidet das Auge im Betrachter - so wie bei der Key-Item-Range.
# Der Nutzer sucht nach Ausreissern: Items, die frei stehen, in der Wand
# stecken oder falsch herum zeigen.
#
# GEMESSENE GRUNDLAGEN (Vanilla Andrea2/andy3, siehe Aufgabe #61):
#   - Position: exakt Sektormitte, wie jedes andere Item auch
#     (X/Z = Raumursprung + Sektorindex*1024 + 512).
#   - Y      : Bodenhoehe des Sektors. OCB 2 und 9 bekommen KEINE
#              Bodenkorrektur in InitialisePickup - der Wert gilt woertlich.
#   - Winkel : ZEIGT AUF DIE WAND. Nachgemessen an Andrea2 Item 121:
#              Wand bei -Z, Winkel -32768 (Blick -Z). Das Mesh der Modelle
#              sitzt rund +450..500 in Modell-Z und landet damit auf der
#              Wandflaeche; Lara rueckt auf +215 dazwischen.
#   - Die Engine prueft die Wand NIE (PickUpCollision testet nur Laras Lage
#     relativ zum Item) - die Wand ist Optik und Erreichbarkeit.
#
# Ablauf:
#   1. ZoneAnalyse.ps1 -ExportZonesJson liefert je Sektor wallAngles.
#   2. Fuer jeden Sektor und JEDE seiner Wandkanten ein Item mit diesem Winkel.
#      Eckensektoren bekommen dadurch mehrere Items uebereinander - gewollt,
#      man sieht so jede moegliche Ausrichtung.
#   3. Patch ueber BauWerk.exe (unser eigener Patcher, kennt angle und ocb -
#      TombItemEdit2026 -patchlevel wird hier bewusst NICHT benutzt).
#
# MAX_ITEMS (512) wird dabei ueberschritten. Das ist Absicht und in Ordnung:
# das Testlevel wird NUR in trview angesehen, nie geladen. Nutzer dazu:
# "Nein, ich teste es in TRview das ist egal."
param(
    [string[]]$Levels = @('Andrea2'),
    [string]$VanillaDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Steam Version\data\Vanilla",
    [string]$LevelInfoDir = "C:\Cloude AI\Tomb5 Native win 11 fix\TR5Randomizer\build\data",
    [string]$OutDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Wandtest",
    [string]$ScratchDir = "$env:TEMP\wandtest",
    [string]$ZoneScript = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Tools\ZoneAnalyse.ps1",
    [string]$Exe = "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender\BauWerk.exe"
)

# Welche Typen je Level an die Wand gehoeren, mit ihrer OCB. Die Werte sind
# gemessen (ItemPruef an den Vanilla-Dateien), nicht geraten.
$WANDTYPEN = @{
    'Andrea2' = @( @{ typeId = 174; ocb = 2 }, @{ typeId = 172; ocb = 2 } )
    'andy3'   = @( @{ typeId = 172; ocb = 2 }, @{ typeId = 174; ocb = 9 } )
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
New-Item -ItemType Directory -Force $ScratchDir | Out-Null
$results = @()

foreach ($lvl in $Levels) {
    $infoJson = "$LevelInfoDir\level_$lvl.json"
    $srcTrc = "$VanillaDir\$lvl.trc"
    if (-not (Test-Path $infoJson)) { "SKIP $lvl - kein Level-Info-JSON"; continue }
    if (-not (Test-Path $srcTrc))   { "SKIP $lvl - keine Vanilla-.trc"; continue }
    if (-not $WANDTYPEN.ContainsKey($lvl)) { "SKIP $lvl - keine Wandtypen hinterlegt"; continue }

    $d = Get-Content $infoJson -Raw | ConvertFrom-Json
    $rooms = @{}
    foreach ($r in $d.rooms) { $rooms[[int]$r.index] = $r }

    # --- Sperren aus der Nutzerdurchsicht (2026-08-19) --------------------
    #
    # (a) WASSERRAEUME. PickUpCollision laeuft nur bei water_status
    #     LW_ABOVE_WATER oder LW_WADE (game\pickup.cpp:766) - unter Wasser ist
    #     der Griff unmoeglich, ein Wanditem dort waere unerreichbar.
    $wasserRaum = @{}
    foreach ($r in $d.rooms) { if ($r.waterRoom) { $wasserRaum[[int]$r.index] = $true } }

    # (b) BELEGTE SEKTOREN. Steht dort schon etwas, das kein Pickup ist -
    #     eine Aufnahme (Slot), ein Animating, ein Schalter, eine Stange -,
    #     darf kein Wanditem daneben. Nutzer: "Animatings muessen auch noch
    #     mit einbezogen werden und Slots die nicht zum Item gehoeren muessen
    #     als Blocker gelten."
    #
    #     Statt Slot- und Animating-Nummern zu raten (die TRCatalog-Liste
    #     zaehlt anders als TR5) gilt die Umkehrung: Pickup-Bereiche sind
    #     bekannt und belegt (172-241, 334-355) - alles andere ist Kulisse
    #     oder Mechanik und blockt. Ausgenommen sind nur die unsichtbaren
    #     Marker: KI-Objekte (378-383) und Kameraziele (409).
    $blocker = New-Object System.Collections.Generic.HashSet[string]
    foreach ($it in $d.items) {
        $t = [int]$it.typeId
        $istPickup = (($t -ge 172 -and $t -le 241) -or ($t -ge 334 -and $t -le 355))
        $istMarker = (($t -ge 378 -and $t -le 383) -or $t -eq 409)
        if ($istPickup -or $istMarker) { continue }
        [void]$blocker.Add("$([int]$it.room),$([int]$it.sectorX),$([int]$it.sectorZ)")
    }

    $zonesJson = "$ScratchDir\zones_$lvl.json"
    & $ZoneScript -LevelJson $infoJson -LevelName $lvl -ExportZonesJson $zonesJson 2>&1 | Out-Null
    if (-not (Test-Path $zonesJson)) { "SKIP $lvl - Zonen-Export fehlgeschlagen"; continue }
    $z = Get-Content $zonesJson -Raw | ConvertFrom-Json

    $itemAdds = @()
    $mitWand = 0; $wegWasser = 0; $wegBelegt = 0
    foreach ($s in $z.sectors) {
        $wa = @( if ($null -ne $s.wallAngles) { $s.wallAngles } )
        if ($wa.Count -eq 0) { continue }
        if ($wasserRaum.ContainsKey([int]$s.room)) { $wegWasser++; continue }
        if ($blocker.Contains("$([int]$s.room),$([int]$s.sectorX),$([int]$s.sectorZ)")) { $wegBelegt++; continue }
        $mitWand++
        $rm = $rooms[[int]$s.room]
        if (-not $rm) { continue }
        $wx = [int]$rm.originX + [int]$s.sectorX * 1024 + 512
        $wz = [int]$rm.originZ + [int]$s.sectorZ * 1024 + 512
        # Je Wandkante ein Item, die hinterlegten Typen reihum - so liegen in
        # einer Ecke zwei verschiedene Modelle nebeneinander und man sieht
        # sofort, ob beide gleich stehen.
        $i = 0
        foreach ($w in $wa) {
            $t = $WANDTYPEN[$lvl][$i % $WANDTYPEN[$lvl].Count]
            $itemAdds += [PSCustomObject]@{ typeId = [int]$t.typeId; room = [int]$s.room;
                                            x = $wx; y = [int]$s.floorY; z = $wz;
                                            angle = [int]$w; ocb = [int]$t.ocb }
            $i++
        }
    }

    # UTF-8 ohne BOM - der handgeschriebene JSON-Leser stolpert sonst ueber
    # den Vorspann (bekannter Fall aus dem Projekt).
    $patch = [PSCustomObject]@{ itemAdds = $itemAdds } | ConvertTo-Json -Depth 4 -Compress
    $patchPath = "$ScratchDir\patch_wand_$lvl.json"
    [System.IO.File]::WriteAllText($patchPath, $patch, (New-Object System.Text.UTF8Encoding($false)))

    $dstTrc = "$OutDir\$lvl.trc"
    if (Test-Path $dstTrc) { Remove-Item $dstTrc -Force }
    $argList = @("`"$srcTrc`"", "`"$patchPath`"", "`"$dstTrc`"", '-mehritems')
    $p = Start-Process -FilePath $Exe -ArgumentList $argList -Wait -PassThru -NoNewWindow
    $ok = (Test-Path $dstTrc) -and $p.ExitCode -eq 0
    $results += [PSCustomObject]@{ Level = $lvl; Wandsektoren = $mitWand; Wasser = $wegWasser; Belegt = $wegBelegt; Items = $itemAdds.Count; OK = $ok }
    "{0,-10} Wandsektoren={1,4}  (-{2} Wasser, -{3} belegt)  Items={4,4}  {5}" -f $lvl, $mitWand, $wegWasser, $wegBelegt, $itemAdds.Count, ($(if ($ok) { 'OK' } else { 'FEHLER' }))
}

Write-Output ""
Write-Output "=== Zusammenfassung ==="
$results | Format-Table -AutoSize
Write-Output "Ausgabe: $OutDir"

