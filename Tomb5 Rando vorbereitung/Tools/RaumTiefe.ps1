# Raumweise Portal-Flut, analog zu trviews "Depth"-Funktion: von Laras Start
# aus werden Raeume nach Anzahl der Portal-Spruenge (Depth 0, 1, 2, ...)
# eingeteilt - NICHT sektorweise wie ZoneAnalyse.ps1, sondern rein ueber
# portalTarget/roomBelow/roomAbove, unabhaengig davon ob eine Tuer im Weg
# steht oder ein Sektor eine Wand ist.
#
# Zweck: Wenn ein Raum hier erreichbar ist, mein sektorweiser Flood in
# ZoneAnalyse.ps1 ihn aber als unerreicht meldet, zeigt das eine ECHTE
# Blockade (Tuer, Wand, fehlende Sektorverbindung) - nicht bloss eine Luecke
# in der Portal-Graph-Konnektivitaet. So laesst sich unterscheiden, ob ein
# Levelbereich wie der Luftschacht in joby3 durch Bewegung (Klettern/Crawlen)
# erreicht wird, die in den Sektordaten gar nicht als Portal auftaucht, oder
# ob er einfach nur hinter einer (noch geschlossenen) Tuer liegt.
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
Write-Output " Raumweite Portal-Tiefe (wie trview Depth): $LevelName"
Write-Output "=============================================================="
Write-Output "Laras Startraum: $startRoom"
Write-Output "Raeume gesamt: $($allRooms.Count)   ueber Portale erreicht: $($depth.Count)   nie erreicht: $($unreached.Count)"
if ($unreached.Count -gt 0) {
    Write-Output "Portal-unerreichbare Raeume: $(($unreached | Sort-Object) -join ', ')"
}
Write-Output ""
if ($HighlightRooms.Count -gt 0) {
    Write-Output "--- Angefragte Raeume ---"
    foreach($hr in $HighlightRooms){
        if ($depth.ContainsKey($hr)) { "  Raum {0,4}: Portal-Tiefe {1}" -f $hr,$depth[$hr] }
        else { "  Raum {0,4}: UEBER PORTALE NICHT ERREICHBAR" -f $hr }
    }
    Write-Output ""
}
Write-Output "--- Raeume nach Tiefe (erste 15 Stufen) ---"
$maxD = ($depth.Values | Measure-Object -Maximum).Maximum
for($i=0; $i -le [math]::Min(15,$maxD); $i++){
    $rms = @($depth.GetEnumerator() | Where-Object { $_.Value -eq $i } | ForEach-Object { $_.Key } | Sort-Object)
    "  Tiefe {0,2}: {1,3} Raeume  ({2})" -f $i,$rms.Count,(($rms | Select-Object -First 12) -join ',')
}
