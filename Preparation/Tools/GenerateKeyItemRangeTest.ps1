# Generates per vanilla level a .trc in which the REAL key/puzzle item types
# of that level lie on all sectors where the zone flood would allow them.
# User request: that is easier to read than a generic alias palette colored
# only by zone number - in trview you directly select "Key Item1" and every
# sector allowed for exactly this type lights up.
#
# Flow per level:
#   1. ZoneAnalysis.ps1 -ExportZonesJson delivers zone+Y per validForItems
#      sector, plus keyConstraints: [{typeId, maxZone, instances, reusable},
#      ...] - the key/puzzle types actually occurring in the level with
#      their loosest "must lie in zone <= N" limit (the maximum over all
#      slot instances of that type if several locks need the same type).
#   2. For each constraint: place an item of that typeId on every sector
#      with zone <= maxZone. A sector can get several stacked items this
#      way when several key types are allowed there at once - intended,
#      trview distinguishes them via the type list.
#   3. Levels without derived key constraints (e.g. Andy2) are skipped -
#      there is nothing key-specific to show there.
#   4. A patch JSON (itemAdds) is built, TombItemEdit2026.exe -patchlevel
#      applies it to the vanilla .trc and writes the result to the target
#      folder.
#
# Y convention verified: item.y == sector.floorY exactly (no offset),
# X/Z = room origin + sector index*1024 + 512 (sector center) -
# cross-checked at a real vanilla item (Andrea1 Key Item1, item 107).
param(
    [string[]]$Levels = @('Andrea1','Andrea2','Andrea3','andy1','Andy2','andy3','joby2','joby3','joby4','joby5','rich1','rich2','rich3','richcut2'),
    # Additionally fill EVERY valid sector with this type. 0 = off.
    #
    # What for: the constraint types show where a KEY item may lie. An
    # ordinary pickup, by contrast, may lie wherever anything may lie at
    # all - i.e. on every sector of the export. Both layers on top of each
    # other answer exactly the question the user wants answered: which ITEM
    # KINDS are allowed on this sector?
    #
    # ALL, not the rest. The first attempt filled only the sectors no
    # constraint covers - then the 223 marker was missing everywhere a key
    # item may also lie, and in Andrea1 654 sectors looked as if they were
    # barred for normal pickups. The layers are meant to overlap.
    #
    # Sensible is a type that occurs in the level and is easy to filter out
    # in trview - 223 (Pickup Item4, the secret) has proven itself.
    [int]$FillAllWith = 0
)
$ErrorActionPreference = "Stop"

$LevelInfoDir = "$PSScriptRoot\..\Level Infos"
# Deliberately the backup subfolder and NOT data\ itself: since the
# randomizer has a GUI, data\ is regularly randomized - the originals then
# lie in data\Backup. If this script kept pointing at data\, it would
# silently compute on already randomized levels. Nothing would crash, the
# zones would simply be wrong. Exactly this case has occurred once and cost
# an hour of bug hunting, see the checksum check further down.
# Both paths updated 2026-08-23: the own vanilla folder and the old storage
# location of TombItemEdit2026 no longer exist. The source is now the
# backup of the installation - the checksum control underneath proves it is
# unchanged, and that is exactly what it is there for.
$VanillaDir   = "$PSScriptRoot\..\..\Tomb5 Steam Version\data\Backup"
$OutDir       = "$PSScriptRoot\..\Level Test\KeyItemRange Level Test"
$ZoneScript   = "$PSScriptRoot\ZoneAnalysis.ps1"
# ATTENTION: only the build under Release\Beta can do -patchlevel. The
# same-named EXE from the level editor is the GUI version - it opens its
# window and returns exit code 0 without an output file (ran into that
# 2026-08-23).
# TombItemEdit2026 is a program of its own and not part of this
# repository. TR5_ITEMEDIT names its executable.
$Exe          = $env:TR5_ITEMEDIT
if (-not $Exe) {
    throw 'Set TR5_ITEMEDIT to the Release\Beta build of TombItemEdit2026.exe (see above - only that build can do -patchlevel).'
}
# No fixed user path (#72): intermediate files go to the machine's temp
# folder.
$ScratchDir   = Join-Path $env:TEMP "tr5rando-scratch"
if (-not (Test-Path $ScratchDir)) { New-Item -ItemType Directory -Path $ScratchDir | Out-Null }

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

#----------------------------------------------------------------------------
# Check the source's checksums before anything is generated.
#
# The randomizer has had this safeguard all along; the preparation tools
# lacked it, and that came to light when the target folder of a GUI test was
# the same one being read from here. The result looked plausible and was
# wrong - the most unpleasant kind of error. Better to abort loudly.
#----------------------------------------------------------------------------
$ChecksumCsv = Join-Path $LevelInfoDir "VanillaLevelChecksums.csv"
if (-not (Test-Path $ChecksumCsv)) { throw "checksum list missing: $ChecksumCsv" }
$expect = @{}
foreach ($row in (Import-Csv $ChecksumCsv)) { $expect[$row.FileName] = $row.SHA256.ToUpper() }

$bad = @()
foreach ($lvl in $Levels) {
    $trc = Join-Path $VanillaDir "$lvl.trc"
    if (-not (Test-Path $trc)) { $bad += "$lvl.trc missing"; continue }
    $have = (Get-FileHash $trc -Algorithm SHA256).Hash.ToUpper()
    if (-not $expect.ContainsKey("$lvl.trc")) { $bad += "$lvl.trc not in the list"; continue }
    if ($have -ne $expect["$lvl.trc"]) { $bad += "$lvl.trc deviates from vanilla" }
}
if ($bad.Count -gt 0) {
    Write-Output "ABORT - the source is not unchanged:"
    $bad | ForEach-Object { Write-Output "  $_" }
    Write-Output "source folder: $VanillaDir"
    throw "source not vanilla - nothing was generated."
}
Write-Output "source checked: $($Levels.Count) levels unchanged ($VanillaDir)"

$results = @()
foreach($lvl in $Levels){
    $infoJson = "$LevelInfoDir\$lvl.json"
    $srcTrc   = "$VanillaDir\$lvl.trc"
    if (-not (Test-Path $infoJson)) { "SKIP $lvl - no level info JSON"; continue }
    if (-not (Test-Path $srcTrc))   { "SKIP $lvl - no vanilla .trc found"; continue }

    $d = Get-Content $infoJson -Raw | ConvertFrom-Json
    $rooms = @{}; foreach($r in $d.rooms){ $rooms[[int]$r.index] = $r }

    $zonesJson = "$ScratchDir\zones_$lvl.json"
    & $ZoneScript -LevelJson $infoJson -LevelName $lvl -ExportZonesJson $zonesJson 2>&1 | Out-Null
    if (-not (Test-Path $zonesJson)) { "SKIP $lvl - zone export failed"; continue }
    $z = Get-Content $zonesJson -Raw | ConvertFrom-Json
    $constraints = @($z.keyConstraints)
    #------------------------------------------------------------------------
    # Make levels without key locks checkable anyway.
    #
    # Six of the fourteen levels have no key/puzzle constraints - joby4,
    # Andy2, andy3, rich2, rich3, richcut2 - and used to be skipped. So
    # there was no current test level for them and no way to cross-check
    # the curation in trview. Exactly that surfaced at joby4: the user was
    # looking at a four-day-old file from a tool that no longer exists.
    #
    # As a substitute, a single constraint is built that covers ALL valid
    # sectors. The type used is a pickup that really occurs in the level -
    # a type without an entry in the object table would be invisible in
    # game. The most frequent one is preferred, then it is easy to find in
    # trview via the type list.
    #------------------------------------------------------------------------
    # With -FillAllWith the substitute constraint is not needed at all: the
    # fill layer covers every sector anyway, and a second marker of the
    # same type on the same field would only be noise.
    if ($constraints.Count -eq 0 -and $FillAllWith -gt 0) {
        "  ${lvl}: no key constraints - only the fill layer counts"
    }
    elseif ($constraints.Count -eq 0) {
        $cand = @($d.items | Where-Object { $t=[int]$_.typeId; ($t -ge 172 -and $t -le 241) -or ($t -ge 334 -and $t -le 355) } |
                  Group-Object typeId | Sort-Object Count -Descending)
        if ($cand.Count -eq 0) {
            "SKIP ${lvl} - no constraints and not a single pickup in the level, nothing to show"
            continue
        }
        $genericType = [int]$cand[0].Name
        $maxZ = if($null -ne $z.maxZone){ [int]$z.maxZone } else { 0 }
        $constraints = @([PSCustomObject]@{
            typeId = $genericType; maxZone = $maxZ; instances = $(if($cand.Count){[int]$cand[0].Count}else{0})
            reusable = $false; excludeRooms = @(); allowRooms = @()
            minFloorY = $null; minFloorYZone = $null
            islandSectors = $null; minFloorYIsland = $null })
        "  ${lvl}: no key constraints - all valid sectors filled with type $genericType"
    }

    # Weapon-free rooms. They hang on the level, not on the type, and hit
    # every weapon type - even one that only appears here as a substitute
    # constraint. Today that has no effect (only joby3 has such rooms, and
    # joby3 has real key constraints), but the randomizer enforces it, and
    # the test levels must show the same thing that gets generated in the
    # end.
    $noWeapon = @( if($null -ne $z.noWeaponRooms){ $z.noWeaponRooms | ForEach-Object { [int]$_ } } )
    $wTypes   = @( if($null -ne $z.weaponTypes){ $z.weaponTypes | ForEach-Object { [int]$_ } } )

    # Placement offset per type (TYPE_OFFSET, already resolved for this
    # level in the export). Must be applied here exactly as in the
    # randomizer, otherwise the test level shows the items elsewhere than
    # they land in the seed - and that is exactly what the user looks at it
    # for.
    $offOf = @{}
    foreach($to in @($z.typeOffsets)){
        if ($null -eq $to) { continue }
        $offOf[[int]$to.typeId] = @{ dx=[int]$to.dx; dy=[int]$to.dy; dz=[int]$to.dz }
    }

    $itemAdds = @()
    $covered = New-Object System.Collections.Generic.HashSet[string]
    foreach($c in $constraints){
        $typeIsWeapon = $wTypes -contains [int]$c.typeId
        # excludeRooms: rooms that lie in the zone but in which this one
        # type still may not lie (user statement from trview).
        $ex = @( if($null -ne $c.excludeRooms){ $c.excludeRooms | ForEach-Object { [int]$_ } } )
        # allowRooms is a positive list: if set, only it counts (the zone
        # remains the upper bound anyway). Empty = no restriction.
        $al = @( if($null -ne $c.allowRooms){ $c.allowRooms | ForEach-Object { [int]$_ } } )
        # minFloorY: height limit of the island group. Smaller y lies
        # HIGHER in TR, so a sector below this bound is too HIGH -
        # typically the top of a crate stack that Lara only reaches with
        # the key that is supposed to lie there.
        # It applies ONLY within the island group (minFloorYIsland), for it
        # is that group's property. Hung on the zone it was too broad: zone
        # 0 comprises 7756 sectors in joby2, the island only 1900 - for Key
        # Item7, which may lie outside the island, the bound thereby acted
        # in half the level (noticed by the user).
        # islandSectors: the start island from the second flood. If set, it
        # is a hard positive list - Key Item8 in joby2 may lie exclusively
        # there, because the flipmap at the crane itself presupposes it.
        $isl = $null
        if ($null -ne $c.islandSectors) {
            $isl = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.islandSectors){ [void]$isl.Add([string]$k) }
        }
        # requireInIsland is NO bound for the single instance but a
        # statement about all of them together: at least one must lie
        # there, the rest may go anywhere. Strictly speaking the "allowed
        # range" of this type is thus the whole level - and exactly so the
        # test level showed it at first, with 552 bottles all across rich2.
        # That is correct but useless: the user wants to see the mandatory
        # zone, for only that is what he specified. Hence it is drawn here
        # when there is no real island. So think along when reading - what
        # lights up here is "one must lie here", not "only here may one
        # lie".
        elseif ($null -ne $c.requireInIsland) {
            $isl = New-Object System.Collections.Generic.HashSet[string]
            foreach($k in $c.requireInIsland){ [void]$isl.Add([string]$k) }
        }
        # bannedSectors: the inversion of islandSectors - here the type may
        # NOT lie. andy1's slingshot halves may not lie before the
        # teleporter, or Lara walks through without them and never comes
        # back.
        # forbiddenSectors: behind its own door - intersection over all
        # locks of this type, see ZoneAnalysis.
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
            (-not $typeIsWeapon -or $noWeapon -notcontains [int]$_.room) -and
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
            [void]$covered.Add($kk2)
        }
    }

    # The layer of ordinary pickups: EVERY valid sector gets the marker,
    # including those on which a key item may additionally lie.
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
        $keyOnly = @($z.sectors | Where-Object { -not $covered.Contains("$($_.room),$($_.sectorX),$($_.sectorZ)") }).Count
        "  ${lvl}: $n sectors filled with type $FillAllWith, $keyOnly of them without any key range"
    }

    $patch = [PSCustomObject]@{ itemAdds = $itemAdds } | ConvertTo-Json -Depth 4 -Compress
    $patchPath = "$ScratchDir\patch_$lvl.json"
    # UTF-8 without BOM - the hand-written JSON reader in TombItemEdit2026
    # otherwise fails on the preamble (see the earlier BOM bugfix in the
    # project).
    [System.IO.File]::WriteAllText($patchPath, $patch, (New-Object System.Text.UTF8Encoding($false)))

    $dstTrc = "$OutDir\$lvl.trc"
    if (Test-Path $dstTrc) { Remove-Item $dstTrc -Force }
    # & with individual path arguments splits paths containing spaces (the
    # path to this repository may well contain one) wrongly -
    # hence call via Start-Process with a cleanly quoted ArgumentList.
    $argList = @('-patchlevel', "`"$srcTrc`"", "`"$dstTrc`"", "`"$patchPath`"")
    $p = Start-Process -FilePath $Exe -ArgumentList $argList -Wait -PassThru -NoNewWindow
    $ok = (Test-Path $dstTrc) -and $p.ExitCode -eq 0
    $typeList = ($constraints | ForEach-Object {
        $x = @( if($null -ne $_.excludeRooms){ $_.excludeRooms } )
        $a = @( if($null -ne $_.allowRooms){ $_.allowRooms } )
        "$($_.typeId)<=Z$($_.maxZone)$(if($x.Count){" -$($x.Count)R"})$(if($a.Count){" only$($a.Count)R"})"
    }) -join ', '
    $results += [PSCustomObject]@{ Level=$lvl; Types=$constraints.Count; Items=$itemAdds.Count; OK=$ok }
    "{0,-10} types=[{1}]  items={2,5}  exitCode={3}  {4}" -f $lvl,$typeList,$itemAdds.Count,$p.ExitCode,($(if($ok){'OK'}else{'ERROR'}))
}

Write-Output ""
Write-Output "=== Summary ==="
$results | Format-Table -AutoSize
