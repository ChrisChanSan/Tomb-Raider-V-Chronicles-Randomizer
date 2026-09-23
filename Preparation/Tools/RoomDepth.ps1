# Room-wise portal flood, analogous to trview's "Depth" function: from
# Lara's start, rooms are grouped by the number of portal hops (depth 0, 1,
# 2, ...) - NOT sector-wise like ZoneAnalysis.ps1, but purely via
# portalTarget/roomBelow/roomAbove, regardless of whether a door stands in
# the way or a sector is a wall.
#
# Purpose: if a room is reachable here but the sector-wise flood in
# ZoneAnalysis.ps1 reports it as unreached, that shows a real blockage
# (door, wall, missing sector connection) - not just a gap in the portal
# graph's connectivity. This distinguishes whether a level area like the air
# shaft in joby3 is reached by movement (climbing/crawling) that never
# appears as a portal in the sector data, or whether it simply lies behind a
# still-closed door.
param(
    [string]$LevelJson,
    [string]$LevelName = "Level",
    [int[]]$HighlightRooms = @()
)
$ErrorActionPreference = "Stop"
$d = Get-Content $LevelJson -Raw | ConvertFrom-Json

$adj = @{}
function Add-RoomEdge($a,$b) {
    if (-not $adj.ContainsKey($a)) { $adj[$a] = New-Object System.Collections.Generic.HashSet[int] }
    [void]$adj[$a].Add($b)
}
foreach($s in $d.sectors){
    $r = [int]$s.room
    if ($null -ne $s.portalTarget) { Add-RoomEdge $r ([int]$s.portalTarget); Add-RoomEdge ([int]$s.portalTarget) $r }
    foreach($vr in @($s.roomBelow,$s.roomAbove)){
        if ($vr -ne 255) { Add-RoomEdge $r ([int]$vr); Add-RoomEdge ([int]$vr) $r }
    }
}
foreach($rm in $d.rooms){
    if ($null -ne $rm.alternateRoom -and $rm.alternateRoom -ne '') {
        Add-RoomEdge ([int]$rm.index) ([int]$rm.alternateRoom); Add-RoomEdge ([int]$rm.alternateRoom) ([int]$rm.index)
    }
}

$lara = $d.items | Where-Object { $_.typeId -eq 0 } | Select-Object -First 1
$startRoom = [int]$lara.room
$depth = @{ $startRoom = 0 }
$q = New-Object System.Collections.Generic.Queue[int]
$q.Enqueue($startRoom)
while($q.Count -gt 0){
    $r = $q.Dequeue()
    if (-not $adj.ContainsKey($r)) { continue }
    foreach($n in $adj[$r]){
        if ($depth.ContainsKey($n)) { continue }
        $depth[$n] = $depth[$r] + 1
        $q.Enqueue($n)
    }
}

$allRooms = @($d.rooms | ForEach-Object { [int]$_.index })
$unreached = @($allRooms | Where-Object { -not $depth.ContainsKey($_) })

Write-Output "=============================================================="
Write-Output " Room-wise portal depth (like trview Depth): $LevelName"
Write-Output "=============================================================="
Write-Output "Lara's start room: $startRoom"
Write-Output "rooms total: $($allRooms.Count)   reached via portals: $($depth.Count)   never reached: $($unreached.Count)"
if ($unreached.Count -gt 0) {
    Write-Output "portal-unreachable rooms: $(($unreached | Sort-Object) -join ', ')"
}
Write-Output ""
if ($HighlightRooms.Count -gt 0) {
    Write-Output "--- Requested rooms ---"
    foreach($hr in $HighlightRooms){
        if ($depth.ContainsKey($hr)) { "  room {0,4}: portal depth {1}" -f $hr,$depth[$hr] }
        else { "  room {0,4}: NOT REACHABLE VIA PORTALS" -f $hr }
    }
    Write-Output ""
}
Write-Output "--- Rooms by depth (first 15 levels) ---"
$maxD = ($depth.Values | Measure-Object -Maximum).Maximum
for($i=0; $i -le [math]::Min(15,$maxD); $i++){
    $rms = @($depth.GetEnumerator() | Where-Object { $_.Value -eq $i } | ForEach-Object { $_.Key } | Sort-Object)
    "  depth {0,2}: {1,3} rooms  ({2})" -f $i,$rms.Count,(($rms | Select-Object -First 12) -join ',')
}
