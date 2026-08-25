# Lists per level the unreached areas with the objects that lie there - with
# resolved TR5 names. This is the bridge between walkthrough text and level
# data: the text names enemies and objects, here stands in which room they
# lie and whether the analyser reaches that room.
#
# IMPORTANT: TRCatalog2.xml contains all games. The TR5 section must be
# chosen explicitly - a bare //moveable delivers TR4 names and with them
# silently wrong results (typeId 105 came back as "Magnum ammo" that way,
# in TR5 it is "Crumbling Floor").
param(
    [string]$LevelName,
    [string]$InfoDir  = "C:\Cloude AI\Tomb5 Native win 11 fix\Preparation\Level Infos",
    [string]$Catalog  = "C:\Cloude AI\Two gun test\TombItemEdit2026\RESOURCES\TRCatalog2.xml"
)
$ErrorActionPreference = "Stop"

$x = [xml](Get-Content $Catalog)
$names = @{}
foreach($m in $x.SelectSingleNode("//game[@id='TR5']").SelectNodes(".//moveable")){
    $names[[int]$m.id] = $m.name
}

# The same origin check as in ZoneAnalysis: the room numbers here are only
# worth as much as the level info they come from.
. (Join-Path $PSScriptRoot "VanillaGate.ps1")
[void](Test-VanillaOrigin -LevelJson "$InfoDir\$LevelName.json")

$d = Get-Content "$InfoDir\$LevelName.json" -Raw | ConvertFrom-Json

# Pull the unreached rooms from the report
$unreRooms = @()
$inGap = $false
foreach($line in (Get-Content "$InfoDir\ZoneReports\$LevelName.txt")){
    if ($line -match '^--- Largest unreached') { $inGap = $true; continue }
    if ($inGap -and $line -match '^--- ') { break }
    if ($inGap -and $line -match '^\s+rooms: (.+)$') {
        $unreRooms += ($Matches[1] -split ',\s*' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
    }
}

# Hide the noise: ammo, medipacks, flares, pure deco
$noise = @(348,349,350,354,355,336,337,338,339,340,341,342,343,344,345,346)
Write-Output "=============================================================="
Write-Output " $LevelName - objects in the unreached areas"
Write-Output "=============================================================="
Write-Output "Unreached rooms (from the 4 largest areas): $(($unreRooms | Sort-Object -Unique) -join ', ')"
Write-Output ""
$hits = @($d.items | Where-Object { $unreRooms -contains [int]$_.room -and $noise -notcontains [int]$_.typeId })
foreach($g in ($hits | Group-Object typeId | Sort-Object { [int]$_.Name })){
    $tid = [int]$g.Name
    $nm  = if($names.ContainsKey($tid)){ $names[$tid] } else { "?" }
    "{0,4}  {1,-28} {2,2}x  rooms: {3}" -f $tid,$nm,$g.Count,(($g.Group | ForEach-Object { $_.room } | Sort-Object -Unique) -join ',')
}
Write-Output ""
Write-Output "--- For comparison: the same object types in the REACHED part ---"
$types = @($hits | ForEach-Object { [int]$_.typeId } | Sort-Object -Unique)
foreach($t in $types){
    $ok = @($d.items | Where-Object { [int]$_.typeId -eq $t -and $unreRooms -notcontains [int]$_.room })
    if ($ok.Count -eq 0) { continue }
    $nm = if($names.ContainsKey($t)){ $names[$t] } else { "?" }
    "{0,4}  {1,-28} {2,2}x  rooms: {3}" -f $t,$nm,$ok.Count,(($ok | ForEach-Object { $_.room } | Sort-Object -Unique) -join ',')
}
