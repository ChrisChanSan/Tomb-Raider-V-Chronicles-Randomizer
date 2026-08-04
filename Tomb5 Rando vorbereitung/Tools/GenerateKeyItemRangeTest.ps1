# Erzeugt je Vanilla-Level eine .trc, in der jeder valide, per Zonen-Flood
# erreichte Sektor ein Marker-Item traegt - der Item-TYP kodiert die Zone.
# Zweck: visuelle/manuelle Verifikation der ZoneAnalyse.ps1-Ergebnisse direkt
# in trview - Item-Typ anklicken zeigt sofort, in welcher Zone der Sektor
# liegt, ohne den Textreport parallel offen halten zu muessen.
#
# Ablauf pro Level:
#   1. ZoneAnalyse.ps1 -ExportZonesJson liefert je validForItems-Sektor Zone+Y.
#   2. Marker-Item-Palette (10 unterschiedliche, bekanntermassen sichere
#      Pickup-Typen - dieselbe Klasse, die schon in den bestehenden
#      *_AllValidSectorsFilled.trc-Dateien verwendet wurde) wird nach Zone
#      indiziert; mehr Zonen als Palettenplaetze gibt es bisher bei keinem
#      Level (Maximum bislang: 6).
#   3. Patch-JSON (itemAdds) wird gebaut, TombItemEdit2026.exe -patchlevel
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
$VanillaDir   = "C:\Cloude AI\Tomb5 Native win 11 fix\Vanilla Tomb Raider (V) Chronicles Steam Version ENG\data"
$OutDir       = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Level Test\KeyItemRange Level Test"
$ZoneScript   = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Tools\ZoneAnalyse.ps1"
$Exe          = "C:\Cloude AI\Two gun test\TombItemEdit2026\Release\Beta\TombItemEdit2026.exe"
$ScratchDir   = "C:\Users\dev\AppData\Local\Temp\claude\C--Cloude-AI\b7de1344-2e88-43df-b893-8f3cb52ddbcd\scratchpad"

if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir | Out-Null }

# Zone 0..9 - reine Ammo/Deko-Pickups, bereits als "sicher patchbar" bestaetigt
# (dieselbe Klasse wie in den frueheren AllValidSectorsFilled-Fuellungen).
$Palette = @(350,349,355,348,337,339,340,342,343,346)   # SmallMedi,BigMedi,Flares,RevolverAmmo,UziAmmo,ShotgunAmmo1,ShotgunAmmo2,CrossbowAmmo1,CrossbowAmmo2,HkAmmo

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
    if ($z.maxZone -ge $Palette.Count) {
        "WARNUNG ${lvl}: maxZone=$($z.maxZone) >= Palettengroesse $($Palette.Count) - hoechste Zonen teilen sich einen Marker-Typ"
    }

    $itemAdds = @()
    foreach($s in $z.sectors){
        $rm = $rooms[[int]$s.room]
        if (-not $rm) { continue }
        $tid = $Palette[[math]::Min([int]$s.zone, $Palette.Count-1)]
        $wx = [int]$rm.originX + [int]$s.sectorX*1024 + 512
        $wz = [int]$rm.originZ + [int]$s.sectorZ*1024 + 512
        $itemAdds += [PSCustomObject]@{ typeId=$tid; room=[int]$s.room; x=$wx; y=[int]$s.floorY; z=$wz }
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
    $results += [PSCustomObject]@{ Level=$lvl; MaxZone=$z.maxZone; Items=$itemAdds.Count; OK=$ok }
    "{0,-10} maxZone={1}  Marker={2,5}  ExitCode={3}  {4}" -f $lvl,$z.maxZone,$itemAdds.Count,$p.ExitCode,($(if($ok){'OK'}else{'FEHLER'}))
}

Write-Output ""
Write-Output "=== Zusammenfassung ==="
$results | Format-Table -AutoSize
