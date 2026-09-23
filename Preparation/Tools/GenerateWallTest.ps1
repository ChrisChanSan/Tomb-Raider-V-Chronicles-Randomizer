# Generates a test level in which the wall pickups of a level lie on every
# found wall edge, for viewing in trview.
#
# Why: the four OCB pickups (crowbar OCB 2, Joby crow OCB 9) must hang on a
# wall and face it at the right angle. Whether the wall search is any good
# is decided by eye in the viewer, the same way as with the key item range -
# by looking for outliers: items standing free, stuck in the wall, or
# pointing the wrong way.
#
# Measured facts (from vanilla Andrea2/andy3):
#   - Position: exactly sector center, like every other item
#     (X/Z = room origin + sector index*1024 + 512).
#   - Y      : floor height of the sector. OCB 2 and 9 get NO floor
#              correction in InitialisePickup - the value applies literally.
#   - Angle  : points at the wall. Measured at Andrea2 item 121: wall at
#              -Z, angle -32768 (facing -Z). The models' mesh sits around
#              +450..500 in model Z and thereby lands on the wall face;
#              Lara moves in at +215 in between.
#   - The engine NEVER checks the wall (PickUpCollision only tests Lara's
#     position relative to the item) - the wall is looks and reachability.
#
# Flow:
#   1. ZoneAnalysis.ps1 -ExportZonesJson delivers wallAngles per sector.
#   2. For every sector and EVERY one of its wall edges, an item with that
#      angle. Corner sectors thereby get several stacked items - intended,
#      you see every possible orientation this way.
#   3. Patch via LevelBuild.exe (our own patcher, knows angle and ocb -
#      TombItemEdit2026 -patchlevel is deliberately NOT used here).
#
# MAX_ITEMS (512) gets exceeded in the process. That is fine: the test level
# is only viewed in trview, never loaded in the engine, so the limit does
# not apply.
param(
    [string[]]$Levels = @('Andrea2'),
    [string]$VanillaDir = "$PSScriptRoot\..\..\Tomb5 Steam Version\data\Vanilla",
    [string]$LevelInfoDir = "$PSScriptRoot\..\Level Infos",
    [string]$OutDir = "$PSScriptRoot\..\WallTest",
    [string]$ScratchDir = "$env:TEMP\wandtest",
    [string]$ZoneScript = "$PSScriptRoot\ZoneAnalysis.ps1",
    [string]$Exe = "$PSScriptRoot\Diagnose\LevelBuild.exe"
)

# Which types belong on the wall per level, with their OCB. The values are
# measured (ItemCheck on the vanilla files), not guessed.
$WALL_TYPES = @{
    'Andrea2' = @( @{ typeId = 174; ocb = 2 }, @{ typeId = 172; ocb = 2 } )
    'andy3'   = @( @{ typeId = 172; ocb = 2 }, @{ typeId = 174; ocb = 9 } )
    # Measured over all vanilla levels (ItemCheck, OCB 2 or 9 on a pickup
    # type). Andrea3 (220, item 68) and joby3 (172, item 41) also carry
    # OCB 2/9 but are pinned for other reasons the wall search does not
    # resolve:
    #   Andrea3 68 sticks to the LevelEnd trigger.
    #   joby3 41 is needed right in the start room as the crowbar; the room
    #   offers only four other spots, not worth the effort.
}

New-Item -ItemType Directory -Force $OutDir | Out-Null
New-Item -ItemType Directory -Force $ScratchDir | Out-Null
$results = @()

foreach ($lvl in $Levels) {
    # The tracked level-info exports carry no "level_<name>.json" prefix;
    # without matching that exact name, the tool would skip every level and
    # say so quietly.
    $infoJson = "$LevelInfoDir\$lvl.json"
    $srcTrc = "$VanillaDir\$lvl.trc"
    if (-not (Test-Path $infoJson)) { "SKIP $lvl - no level info JSON"; continue }
    if (-not (Test-Path $srcTrc))   { "SKIP $lvl - no vanilla .trc"; continue }
    if (-not $WALL_TYPES.ContainsKey($lvl)) { "SKIP $lvl - no wall types on record"; continue }

    $d = Get-Content $infoJson -Raw | ConvertFrom-Json
    $rooms = @{}
    foreach ($r in $d.rooms) { $rooms[[int]$r.index] = $r }

    # --- Placement bars -----------------------------------------------------
    #
    # (a) Water rooms: PickUpCollision only runs at water_status
    #     LW_ABOVE_WATER or LW_WADE (game\pickup.cpp) - under water the
    #     grab is impossible, so a wall item there would be unreachable.
    $waterRoom = @{}
    foreach ($r in $d.rooms) { if ($r.waterRoom) { $waterRoom[[int]$r.index] = $true } }

    # (b) Occupied sectors: if something already stands there that is not a
    #     pickup - a receptacle (slot), an animating, a switch, a pole - no
    #     wall item may go next to it; animatings and non-item slots count
    #     as blockers too.
    #
    #     Instead of enumerating slot and animating type numbers (the
    #     TRCatalog list counts differently than TR5), the inversion
    #     applies: the pickup ranges are known and fixed (172-241, 334-355),
    #     so everything else is scenery or mechanics and blocks. The
    #     exceptions are the invisible markers: AI objects (378-383) and
    #     camera targets (409).
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

    # Key item range per type. Without it, wall items would reach all the
    # way to the level's end for any type that stood in VANILLA_TYPES, since
    # the zone filter is simply never switched on for those types.
    # ZoneAnalysis derives a limit for them instead.
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

    # Sectors behind a deadstop (NO_KEY_ISLAND, exported as
    # keyBarredSectors). Unlike the zone limit this needs no key constraint,
    # so it also acts for types without a receptacle, like andy3's chalk and
    # fence crowbar.
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
        # at OCB 2 (game\pickup.cpp) - in the crawl the grab is
        # impossible, the item would be unreachable. That is a hard engine
        # rule like the water bar, not looks.
        #
        # Threshold 768: Lara's standing height is 762 units, so at
        # clearance 768 she fits in standing - only below that does the
        # ceiling force her into the duck state. 512 is the crawl space.
        # A tighter bound of 1024, matched to where the four vanilla wall
        # items happen to sit (1024..2048), would exclude valid spots such
        # as andy3 room 64, which consists almost entirely of 768 sectors.
        if ([int]$s.clearance -lt 768) { $outHeight++; continue }
        $withWall++
        $rm = $rooms[[int]$s.room]
        if (-not $rm) { continue }
        $wx = [int]$rm.originX + [int]$s.sectorX * 1024 + 512
        $wz = [int]$rm.originZ + [int]$s.sectorZ * 1024 + 512
        # One item per wall edge, cycling through the recorded types, so two
        # different models lie side by side in a corner - showing
        # immediately whether both stand the same.
        # Every type that may lie at a wall edge under its own zone limit is
        # set there, so in trview each type's range is visible independently
        # - if both types lie at one edge, the spot is allowed for both.
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
    # over the preamble.
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

