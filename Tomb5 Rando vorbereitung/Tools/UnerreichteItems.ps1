# Listet je Level die unerreichten Gebiete mit den Objekten, die dort liegen -
# mit aufgeloesten TR5-Namen. Das ist die Bruecke zwischen Walkthrough-Text und
# Leveldaten: der Text nennt Gegner und Gegenstaende, hier steht, in welchem
# Raum sie liegen und ob dieser Raum vom Analyser erreicht wird.
#
# WICHTIG: TRCatalog2.xml enthaelt alle Spiele. Die TR5-Sektion muss explizit
# gewaehlt werden - ein blosses //moveable liefert TR4-Namen und damit still
# falsche Ergebnisse (typeId 105 kam so als "Magnum ammo" zurueck, in TR5 ist
# es "Crumbling Floor").
param(
    [string]$LevelName,
    [string]$InfoDir  = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Level Infos",
    [string]$Catalog  = "C:\Cloude AI\Two gun test\TombItemEdit2026\RESOURCES\TRCatalog2.xml"
)
$ErrorActionPreference = "Stop"

$x = [xml](Get-Content $Catalog)
$names = @{}
foreach($m in $x.SelectSingleNode("//game[@id='TR5']").SelectNodes(".//moveable")){
    $names[[int]$m.id] = $m.name
}

# Dieselbe Herkunftspruefung wie in ZoneAnalyse: die Raumnummern hier sind nur
# so viel wert wie die Level-Info, aus der sie stammen.
. (Join-Path $PSScriptRoot "VanillaGate.ps1")
[void](Test-VanillaHerkunft -LevelJson "$InfoDir\$LevelName.json")

$d = Get-Content "$InfoDir\$LevelName.json" -Raw | ConvertFrom-Json

# Unerreichte Raeume aus dem Report ziehen
$unreRooms = @()
$inGap = $false
foreach($line in (Get-Content "$InfoDir\ZonenReports\$LevelName.txt")){
    if ($line -match '^--- Groesste unerreichte') { $inGap = $true; continue }
    if ($inGap -and $line -match '^--- ') { break }
    if ($inGap -and $line -match '^\s+Raeume: (.+)$') {
        $unreRooms += ($Matches[1] -split ',\s*' | Where-Object { $_ -match '^\d+$' } | ForEach-Object { [int]$_ })
    }
}

# Rauschen ausblenden: Munition, Medipacks, Flares, reine Deko
$noise = @(348,349,350,354,355,336,337,338,339,340,341,342,343,344,345,346)
Write-Output "=============================================================="
Write-Output " $LevelName - Objekte in den unerreichten Gebieten"
Write-Output "=============================================================="
Write-Output "Unerreichte Raeume (aus den 4 groessten Gebieten): $(($unreRooms | Sort-Object -Unique) -join ', ')"
Write-Output ""
$hits = @($d.items | Where-Object { $unreRooms -contains [int]$_.room -and $noise -notcontains [int]$_.typeId })
foreach($g in ($hits | Group-Object typeId | Sort-Object { [int]$_.Name })){
    $tid = [int]$g.Name
    $nm  = if($names.ContainsKey($tid)){ $names[$tid] } else { "?" }
    "{0,4}  {1,-28} {2,2}x  Raeume: {3}" -f $tid,$nm,$g.Count,(($g.Group | ForEach-Object { $_.room } | Sort-Object -Unique) -join ',')
}
Write-Output ""
Write-Output "--- Zum Vergleich: dieselben Objekttypen im ERREICHTEN Teil ---"
$types = @($hits | ForEach-Object { [int]$_.typeId } | Sort-Object -Unique)
foreach($t in $types){
    $ok = @($d.items | Where-Object { [int]$_.typeId -eq $t -and $unreRooms -notcontains [int]$_.room })
    if ($ok.Count -eq 0) { continue }
    $nm = if($names.ContainsKey($t)){ $names[$t] } else { "?" }
    "{0,4}  {1,-28} {2,2}x  Raeume: {3}" -f $t,$nm,$ok.Count,(($ok | ForEach-Object { $_.room } | Sort-Object -Unique) -join ',')
}
