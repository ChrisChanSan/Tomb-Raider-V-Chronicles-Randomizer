# Generates a test level in which the WALL PICKUPS of a level lie on EVERY
# found wall edge - for viewing in trview (#61).
#
# Why: the four OCB pickups (crowbar OCB 2, Joby crow OCB 9) must hang on a
# wall and with the right angle toward it. Whether our wall search is any
# good is decided by the eye in the viewer - just like with the key item
# range. The user looks for outliers: items standing free, stuck in the
# wall, or pointing the wrong way.
#
# MEASURED FOUNDATIONS (vanilla Andrea2/andy3, see task #61):
#   - Position: exactly sector center, like every other item
#     (X/Z = room origin + sector index*1024 + 512).
#   - Y      : floor height of the sector. OCB 2 and 9 get NO floor
#              correction in InitialisePickup - the value applies literally.
#   - Angle  : POINTS AT THE WALL. Measured at Andrea2 item 121: wall at
#              -Z, angle -32768 (facing -Z). The models' mesh sits around
#              +450..500 in model Z and thereby lands on the wall face;
#              Lara moves in at +215 in between.
#   - The engine NEVER checks the wall (PickUpCollision only tests Lara's
#     position relative to the item) - the wall is looks and reachability.
#
# Flow:
#   1. ZoneAnalyse.ps1 -ExportZonesJson delivers wallAngles per sector.
#   2. For every sector and EVERY one of its wall edges, an item with that
#      angle. Corner sectors thereby get several stacked items - intended,
#      you see every possible orientation this way.
#   3. Patch via BauWerk.exe (our own patcher, knows angle and ocb -
#      TombItemEdit2026 -patchlevel is deliberately NOT used here).
#
# MAX_ITEMS (512) gets exceeded in the process. That is intended and fine:
# the test level is ONLY viewed in trview, never loaded. The user on it:
# "No, I test it in TRview, that does not matter."
param(
    [string[]]$Levels = @('Andrea2'),
    [string]$VanillaDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Steam Version\data\Vanilla",
    [string]$LevelInfoDir = "C:\Cloude AI\Tomb5 Native win 11 fix\TR5Randomizer\build\data",
    [string]$OutDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Wandtest",
    [string]$ScratchDir = "$env:TEMP\wandtest",
    [string]$ZoneScript = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Tools\ZoneAnalyse.ps1",
    [string]$Exe = "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender\BauWerk.exe"
)

# Which types belong on the wall per level, with their OCB. The values are
# measured (ItemPruef on the vanilla files), not guessed.
$WALL_TYPES = @{
    'Andrea2' = @( @{ typeId = 174; ocb = 2 }, @{ typeId = 172; ocb = 2 } )
    'andy3'   = @( @{ typeId = 172; ocb = 2 }, @{ typeId = 174; ocb = 9 } )
    # Measured over all vanilla levels (ItemPruef, OCB 2 or 9 on a pickup
    # type) - these two stood on no list until then: Andrea3 (220, item 68)
    # and joby3 (172, item 41) do have OCB 2/9 but are pinned for OTHER
    # reasons the wall work does not resolve - the user recorded both
    # earlier:
    #   Andrea3 68 sticks to the LevelEnd trigger.
    #   joby3 41 is needed right in the start room as the crowbar; the room
    #   would offer only four other spots, not worth the effort.
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
New-Item -ItemType Directory -Force $ScratchDir | Out-Null
$results = @()

foreach ($lvl in $Levels) {
    $infoJson = "$LevelInfoDir\level_$lvl.json"
    $srcTrc = "$VanillaDir\$lvl.trc"
    if (-not (Test-Path $infoJson)) { "SKIP $lvl - no level info JSON"; continue }
    if (-not (Test-Path $srcTrc))   { "SKIP $lvl - no vanilla .trc"; continue }
    if (-not $WALL_TYPES.ContainsKey($lvl)) { "SKIP $lvl - no wall types on record"; continue }

    $d = Get-Content $infoJson -Raw | ConvertFrom-Json
    $rooms = @{}
    foreach ($r in $d.rooms) { $rooms[[int]$r.index] = $r }

    # --- Bars from the user review (2026-08-19) ---------------------------
    #
    # (a) WATER ROOMS. PickUpCollision only runs at water_status
    #     LW_ABOVE_WATER or LW_WADE (game\pickup.cpp:766) - under water the
    #     grab is impossible, a wall item there would be unreachable.
    $waterRoom = @{}
    foreach ($r in $d.rooms) { if ($r.waterRoom) { $waterRoom[[int]$r.index] = $true } }

    # (b) OCCUPIED SECTORS. If something already stands there that is no
    #     pickup - a receptacle (slot), an animating, a switch, a pole -,
    #     no wall item may go next to it. User: "Animatings must also be
    #     included and slots that do not belong to the item must count as
    #     blockers."
    #
    #     Instead of guessing slot and animating numbers (the TRCatalog
    #     list counts differently than TR5), the inversion applies: pickup
    #     ranges are known and set (172-241, 334-355) - everything else is
    #     scenery or mechanics and blocks. Excepted are only the invisible
    #     markers: AI objects (378-383) and camera targets (409).
    $blocker = New-Object System.Collections.Generic.HashSet[string]
    foreach ($it in $d.items) {
        $t = [int]$it.typeId
        $isPickup = (($t -ge 172 -and $t -le 241) -or ($t -ge 334 -and $t -le 355))
        $isMarker = (($t -ge 378 -and $t -le 383) -or $t -eq 409)
        if ($isPickup -or $isMarker) { continue }
        [void]$blocker.Add("$([int]$it.room),$([int]$it.sectorX),$([int]$it.sectorZ)")
    }

    $zonesJson = "$ScratchDir\zones_$lvl.json"
    & $ZoneScript -LevelJson $infoJson -LevelName $lvl -ExportZonesJson $zonesJson 2>&1 | Out-Null
    if (-not (Test-Path $zonesJson)) { "SKIP $lvl - zone export failed"; continue }
    $z = Get-Content $zonesJson -Raw | ConvertFrom-Json

    # Key item range per type (#61, point 2 of the review). Without it the
    # wall items in the first test level reached all the way to the level's
    # end - the filter was not broken, it was simply never switched on for
    # these types because they stood in VANILLA_TYPES. Now ZoneAnalyse
    # derives a limit for them.
    $limit = @{}
    foreach ($c in @($z.keyConstraints)) {
        if ($null -eq $c) { continue }
        $limit[[int]$c.typeId] = @{
            maxZone = [int]$c.maxZone
            ex = @( if ($null -ne $c.excludeRooms) { $c.excludeRooms | ForEach-Object { [int]$_ } } )
        }
    }
    foreach ($t in $WALL_TYPES[$lvl]) {
        if (-not $limit.ContainsKey([int]$t.typeId)) {
            "  note ${lvl}: type $($t.typeId) has NO zone limit - lies everywhere"
        } else {
            "  ${lvl}: type $($t.typeId) up to zone $($limit[[int]$t.typeId].maxZone), $($limit[[int]$t.typeId].ex.Count) rooms excluded"
        }
    }

    # Sectors behind a deadstop (NO_KEY_ISLAND, new in the export as
    # keyBarredSectors). Unlike the zone limit this needs no key constraint
    # - so it also acts for types without a receptacle like andy3's chalk
    # and fence crowbar (#61).
    $barred = New-Object System.Collections.Generic.HashSet[string]
    foreach ($k in @($z.keyBarredSectors)) { if ($null -ne $k) { [void]$barred.Add([string]$k) } }
    if ($barred.Count -gt 0) { "  ${lvl}: $($barred.Count) sectors barred behind a deadstop" }

    $itemAdds = @()
    $withWall = 0; $outWater = 0; $outOccupied = 0; $outZone = 0; $outDeadstop = 0; $outHeight = 0
    foreach ($s in $z.sectors) {
        $wa = @( if ($null -ne $s.wallAngles) { $s.wallAngles } )
        if ($wa.Count -eq 0) { continue }
        if ($waterRoom.ContainsKey([int]$s.room)) { $outWater++; continue }
        if ($blocker.Contains("$([int]$s.room),$([int]$s.sectorX),$([int]$s.sectorZ)")) { $outOccupied++; continue }
        if ($barred.Contains("$([int]$s.room),$([int]$s.sectorX),$([int]$s.sectorZ)")) { $outDeadstop++; continue }
        # Standing height. PickUpCollision expressly demands !lara.IsDucked
        # at OCB 2 (game\pickup.cpp:803) - in the crawl the grab is
        # impossible, the item would be unreachable. That is a hard engine
        # rule like the water bar, not looks.
        #
        # Threshold 768: Lara's standing height is 762 units, so at
        # clearance 768 she fits in standing - only below that does the
        # ceiling force her into the duck state. 512 is the crawl space.
        #
        # 1024 stood here first, derived from the four vanilla wall items
        # (they sit on 1024..2048). That was correlation, not the rule: the
        # user thereupon missed the good spots in andy3 room 64, which
        # consists almost entirely of 768 sectors.
        if ([int]$s.clearance -lt 768) { $outHeight++; continue }
        $withWall++
        $rm = $rooms[[int]$s.room]
        if (-not $rm) { continue }
        $wx = [int]$rm.originX + [int]$s.sectorX * 1024 + 512
        $wz = [int]$rm.originZ + [int]$s.sectorZ * 1024 + 512
        # One item per wall edge, the recorded types in rotation - that way
        # two different models lie side by side in a corner and you see
        # immediately whether both stand the same.
        # Set EVERY type per wall edge that may lie there per its zone
        # limit. In trview you then see per type how far each range carries
        # - if both lie at one edge, the spot is allowed for both.
        foreach ($w in $wa) {
            foreach ($t in $WALL_TYPES[$lvl]) {
                $tid = [int]$t.typeId
                if ($limit.ContainsKey($tid)) {
                    if ([int]$s.zone -gt $limit[$tid].maxZone) { $outZone++; continue }
                    if ($limit[$tid].ex -contains [int]$s.room) { $outZone++; continue }
                }
                $itemAdds += [PSCustomObject]@{ typeId = $tid; room = [int]$s.room;
                                                x = $wx; y = [int]$s.floorY; z = $wz;
                                                angle = [int]$w; ocb = [int]$t.ocb; yAuto = $true }
            }
        }
    }

    # UTF-8 without BOM - the hand-written JSON reader otherwise stumbles
    # over the preamble (known case from the project).
    $patch = [PSCustomObject]@{ itemAdds = $itemAdds } | ConvertTo-Json -Depth 4 -Compress
    $patchPath = "$ScratchDir\patch_wand_$lvl.json"
    [System.IO.File]::WriteAllText($patchPath, $patch, (New-Object System.Text.UTF8Encoding($false)))

    $dstTrc = "$OutDir\$lvl.trc"
    if (Test-Path $dstTrc) { Remove-Item $dstTrc -Force }
    $argList = @("`"$srcTrc`"", "`"$patchPath`"", "`"$dstTrc`"", '-moreitems')
    $p = Start-Process -FilePath $Exe -ArgumentList $argList -Wait -PassThru -NoNewWindow
    $ok = (Test-Path $dstTrc) -and $p.ExitCode -eq 0
    $results += [PSCustomObject]@{ Level = $lvl; WallSectors = $withWall; Water = $outWater; Occupied = $outOccupied; Items = $itemAdds.Count; OK = $ok }
    "{0,-10} wallSectors={1,4}  (-{2} water, -{3} occupied, -{4} deadstop, -{5} height, -{6} zone)  items={7,4}  {8}" -f $lvl, $withWall, $outWater, $outOccupied, $outDeadstop, $outHeight, $outZone, $itemAdds.Count, ($(if ($ok) { 'OK' } else { 'ERROR' }))
}

Write-Output ""
Write-Output "=== Summary ==="
$results | Format-Table -AutoSize
Write-Output "output: $OutDir"

