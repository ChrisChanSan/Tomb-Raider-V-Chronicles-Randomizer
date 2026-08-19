# Liest die Secret-Vorlage des Nutzers (Kuration\Secrets.txt) und macht daraus
# einen maschinenlesbaren Plan je Level und Satz (#53).
#
# Die Vorlage ist von Hand gepflegt und soll das auch bleiben - sie ist in
# trview ablesbar und ohne Werkzeug zu aendern. Dieser Parser passt sich ihr
# an, nicht umgekehrt.
#
# Aufbau der Vorlage:
#   <Satzname>{
#   Level        Anzahl  Type_ID  Level_ID        New_Rooms/Sektor        Extra_Options
#   Andrea1.trc  3       223      (134,140,148)   Room210(x3,z1), ...     (add ...)+(add ...)
#   }
#
# Zuordnung ist POSITIONSBASIERT: das erste Level_ID gehoert an die erste
# genannte Stelle. Stimmen die Laengen nicht ueberein, meldet der Parser das,
# statt still danebenzugreifen.
#
# Extra_Options, Beispiel aus Andrea1:
#   (add Type_ID(196)@Room14(x1,z3))
#   +(add Type_ID(258)@Room181(x1,z3)@Angle0+Trigger_Type(Key)to->Door Item_Level_ID(120))
# Die Gruppen sind mit + verkettet, und + kommt auch INNERHALB einer Gruppe vor
# (vor Trigger_Type). Deshalb wird auf Klammertiefe 0 getrennt.
param(
    [string]$Datei = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Kuration\Secrets.txt",
    [string]$Satz = "",          # leer = alle Saetze ausgeben
    [string]$LevelName = "",     # leer = alle Level
    [string]$AlsJson = ""        # Zielpfad; leer = nur Bericht auf der Konsole
)

function Split-AufTiefe0([string]$text, [char]$trenner) {
    $teile = @(); $tiefe = 0; $akt = ""
    foreach ($c in $text.ToCharArray()) {
        if ($c -eq '(') { $tiefe++ }
        elseif ($c -eq ')') { $tiefe-- }
        if ($c -eq $trenner -and $tiefe -eq 0) { $teile += $akt; $akt = "" }
        else { $akt += $c }
    }
    if ($akt.Trim().Length -gt 0) { $teile += $akt }
    return @($teile | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
}

# Eine Extra-Option zerlegen. Rueckgabe: Objekt mit typeId/room/sectorX/sectorZ,
# optional angle, optional trigger (art + Zielitem).
function Lies-Option([string]$o) {
    $o = $o.Trim()
    if ($o -notmatch 'add\s+Type_ID\((\d+)\)\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') { return $null }
    $e = [PSCustomObject]@{
        typeId  = [int]$Matches[1]
        room    = [int]$Matches[2]
        sectorX = [int]$Matches[3]
        sectorZ = [int]$Matches[4]
        angle   = $null
        trigger = $null
        text    = $o
    }
    if ($o -match '@\s*Angle\s*(-?\d+)') { $e.angle = [int]$Matches[1] }
    # Trigger_Type(Key)to->Door Item_Level_ID(120)
    if ($o -match 'Trigger_Type\(\s*(\w+)\s*\).*?Item_Level_ID\(\s*(\d+)\s*\)') {
        $e.trigger = [PSCustomObject]@{ art = [string]$Matches[1]; zielItem = [int]$Matches[2] }
    }
    return $e
}

if (-not (Test-Path $Datei)) { Write-Output "Vorlage nicht gefunden: $Datei"; exit 1 }
$zeilen = Get-Content $Datei

$saetze = @()
$aktSatz = $null
foreach ($z in $zeilen) {
    if ($z -match '^\s*([^{}\s][^{}]*?)\s*\{\s*$') {
        $aktSatz = [PSCustomObject]@{ name = $Matches[1].Trim(); level = @() }
        continue
    }
    if ($z -match '^\s*\}\s*$') { if ($aktSatz) { $saetze += $aktSatz; $aktSatz = $null }; continue }
    if (-not $aktSatz) { continue }

    # Levelzeile: Name.trc  Anzahl  Type_ID  (ids...)  Rest
    if ($z -notmatch '^\s*(\w+)\.trc\s+(\d+)\s+(\d+)\s+\(([^)]*)\)\s*(.*)$') { continue }
    $lvl = $Matches[1]; $anzahl = [int]$Matches[2]; $typ = [int]$Matches[3]
    $ids = @($Matches[4] -split ',' | ForEach-Object { [int]$_.Trim() })
    $rest = $Matches[5]

    # Neue Stellen: Room<N>(x<A>,z<B>), Trennung egal (Komma oder Leerzeichen).
    # NUR bis zum Beginn der Extra_Options suchen - die nennen selbst Raeume,
    # und die wuerden sonst als zusaetzliche Secret-Stellen mitgezaehlt.
    $pExtra = $rest.IndexOf('(add')
    $restStellen = if ($pExtra -ge 0) { $rest.Substring(0, $pExtra) } else { $rest }
    $stellen = @()
    foreach ($m in [regex]::Matches($restStellen, 'Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)')) {
        $stellen += [PSCustomObject]@{ room=[int]$m.Groups[1].Value; sectorX=[int]$m.Groups[2].Value; sectorZ=[int]$m.Groups[3].Value }
    }

    # Extra_Options beginnen beim ersten "(add"
    $extras = @()
    $p = $pExtra
    if ($p -ge 0) {
        foreach ($g in (Split-AufTiefe0 $rest.Substring($p) '+')) {
            $o = Lies-Option $g
            if ($null -ne $o) { $extras += $o } else { Write-Output "  WARNUNG ${lvl}: Option nicht lesbar -> $g" }
        }
    }

    $aktSatz.level += [PSCustomObject]@{
        level = $lvl; anzahl = $anzahl; typeId = $typ
        ids = $ids; stellen = $stellen; extras = $extras
    }
}

# Pruefungen
foreach ($s in $saetze) {
    foreach ($l in $s.level) {
        if ($l.ids.Count -ne $l.anzahl) {
            Write-Output "  FEHLER $($s.name)/$($l.level): Anzahl $($l.anzahl), aber $($l.ids.Count) Level_ID"
        }
        if ($l.stellen.Count -gt 0 -and $l.stellen.Count -ne $l.ids.Count) {
            Write-Output "  FEHLER $($s.name)/$($l.level): $($l.ids.Count) Secrets, aber $($l.stellen.Count) neue Stellen"
        }
    }
}

# Ausgabe
$auswahl = $saetze
if ($Satz -ne "") { $auswahl = @($saetze | Where-Object { $_.name -eq $Satz }) }
foreach ($s in $auswahl) {
    Write-Output "=== Satz: $($s.name) ==="
    foreach ($l in $s.level) {
        if ($LevelName -ne "" -and $l.level -ne $LevelName) { continue }
        $ziel = if ($l.stellen.Count -gt 0) {
            (0..($l.ids.Count-1) | ForEach-Object { "$($l.ids[$_])->R$($l.stellen[$_].room)($($l.stellen[$_].sectorX),$($l.stellen[$_].sectorZ))" }) -join '  '
        } else { "bleibt vanilla" }
        Write-Output ("  {0,-10} Typ {1}  {2}" -f $l.level, $l.typeId, $ziel)
        foreach ($e in $l.extras) {
            $t = if ($null -ne $e.trigger) { "  + Trigger $($e.trigger.art) -> Item $($e.trigger.zielItem)" } else { "" }
            $a = if ($null -ne $e.angle) { " Winkel $($e.angle)" } else { "" }
            Write-Output ("      dazu: Typ {0} in Raum {1}({2},{3}){4}{5}" -f $e.typeId,$e.room,$e.sectorX,$e.sectorZ,$a,$t)
        }
    }
}

if ($AlsJson -ne "") {
    $auswahl | ConvertTo-Json -Depth 8 -Compress | Out-File $AlsJson -Encoding utf8
    Write-Output "JSON geschrieben: $AlsJson"
}
