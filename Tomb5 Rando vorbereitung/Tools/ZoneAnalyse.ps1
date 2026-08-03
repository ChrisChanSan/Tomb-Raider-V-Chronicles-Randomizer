# Progressions-Zonen-Analyse fuer TR5-Level (Prototyp)
#
# Idee: Von Laras Startsektor durch den Sektorgraphen fluten, aber an
# geschlossenen Tueren stoppen -> Zone 0. Dann pruefen, welche Tueren sich mit
# dem oeffnen lassen, was in Zone 0 erreichbar ist -> aufschliessen, weiter
# fluten -> Zone 1. Usw. Ergebnis: jedes Key-Item bekommt "muss in Zone <= N
# liegen", wobei N die Zone seiner Tuer ist.
#
# Arbeitet auf den JSON-Dateien aus TombItemEdit2026 -exportinfo.
#
# Das Verstaendnis des TR-Floordata- und Trigger-Formats, auf dem der Export
# beruht, stammt in wesentlichen Teilen aus trview von Chreden (MIT-Lizenz):
#   https://github.com/chreden/trview
#
# Bekannte Vereinfachungen (bewusst, nicht uebersehen):
#  - Flipmap-Raeume werden mit ihrem Basisraum dauerhaft verbunden. Ob der
#    zugehoerige FlipMap-Trigger schon ausgeloest wurde, wird nicht geprueft.
#  - Springen, Klettern und Sprungweiten sind nicht modelliert; der Graph
#    verbindet begehbare Nachbarsektoren, unabhaengig vom Hoehenunterschied.
#  - Einbahn-Uebergaenge und Ausruestungs-Sperren sind handkuratiert
#    (ONEWAY_OUT bzw. EQUIP_GATE weiter unten).
param(
    [string]$LevelJson,
    [string]$LevelName = "Level"
)

$ErrorActionPreference = "Stop"
$d = Get-Content $LevelJson -Raw | ConvertFrom-Json

# --- Typ-IDs (aus RESOURCES/TRCatalog2.xml, TR5-Abschnitt) -------------------
$DOOR_TYPES = @(284,286,288,290,292,294,296,298,   # Door Type 1-8
                300,302,304,306,308,310,           # Closed Door 1-6
                312,314,316,318,320,322,324,326,328,332)
$PUZZLE_ITEM_BASE = 172; $PUZZLE_HOLE_BASE = 242
$KEY_ITEM_BASE    = 196; $KEY_HOLE_BASE    = 258

# --- Nachschlagetabellen ----------------------------------------------------
$rooms = @{}; foreach($r in $d.rooms){ $rooms[[int]$r.index] = $r }
$items = @{}; foreach($i in $d.items){ $items[[int]$i.index] = $i }
$sec   = @{}
foreach($s in $d.sectors){ $sec["$($s.room),$($s.sectorX),$($s.sectorZ)"] = $s }

function Get-SectorInRoom([int]$targetRoom, [int]$worldX, [int]$worldZ) {
    if (-not $rooms.ContainsKey($targetRoom)) { return $null }
    $tr = $rooms[$targetRoom]
    $tx = [math]::Floor(($worldX - $tr.originX) / 1024)
    $tz = [math]::Floor(($worldZ - $tr.originZ) / 1024)
    $k = "$targetRoom,$tx,$tz"
    if ($sec.ContainsKey($k)) { return $sec[$k] }
    return $null
}

# --- Sektorgraph aufbauen ---------------------------------------------------
# Manuell kuratierte Einbahn-Uebergaenge: Raum, den Lara betreten, aber nicht
# wieder in Richtung anderer Raeume verlassen kann. Aus der Geometrie allein
# nicht ableitbar (das Portal existiert in beide Richtungen), deshalb Handarbeit
# - dieselbe Begruendung wie bei boIsManuallyExcludedRoom im Exporter.
$ONEWAY_OUT = @{
    'Andrea1' = @(194)   # Rattenkaefig: rein ja, weiter nach 170 nein
}
$onewayOut = @( if($ONEWAY_OUT.ContainsKey($LevelName)){ $ONEWAY_OUT[$LevelName] } )

# Tueren, die kein Trigger im Level schaltet, weil die Engine sie aus
# levelspezifischem Skriptcode heraus oeffnet (game/deltapak.cpp ruft dort
# FlipMap() und TestTriggers() direkt auf - z.B. cranecut_control -> FlipMap(1),
# die Kran-Cutscene). Solche Oeffnungen stehen nirgends in den Leveldaten und
# sind aus ihnen grundsaetzlich nicht ableitbar. Wer hier eintraegt, sagt dem
# Analyser: diese Tuer geht im Spielverlauf auf, behandle sie als offen.
# Die Diagnose "kein Trigger im Level schaltet diese Tuer" listet die Kandidaten.
# Wichtig: NICHT "immer offen", sondern an eine Bedingung geknuepft - erreiche
# den Raum, in dem das Skript-Ereignis ausgeloest wird. Eine Skript-Tuer pauschal
# zu oeffnen waere schlimmer als sie zu ignorieren: der Analyser wuerde erlauben,
# ein frueh benoetigtes Key-Item hinter eine Tuer zu legen, die real erst am Ende
# einer langen Kette aufgeht - der Randomizer erzeugt dann unloesbare Seeds.
# joby2: Key 7 -> Tuer -> Keycard vom Guard -> eine Ebene hoeher -> ueber die
# Kisten springen -> Kranraum (104) -> Flipmap gibt den Weg frei.
# Noch kein Eintrag verifiziert. joby2 138/141 standen hier kurzzeitig drin und
# hoben das Level scheinbar von 1 auf 90 Prozent - falsch: kein Key-/Switch-
# Trigger in joby2 schaltet sie, sie oeffnen also nie und Lara geht daran vorbei.
# Sie standen nur zufaellig am Rand des erreichten Bereichs. joby2 stoppt in
# Wahrheit an der Geometrie (Sprung ueber die Kisten), siehe MANUAL_LINK.
# Merke: "Tuer ohne Trigger" heisst zuerst "Tuer oeffnet nie", nicht
# "Tuer wird per Skript geoeffnet" - Letzteres erst nach Beleg annehmen.
$SCRIPT_DOORS = @{
}
$scriptDoors = @{}
if ($SCRIPT_DOORS.ContainsKey($LevelName)) { $scriptDoors = $SCRIPT_DOORS[$LevelName] }

# Uebergaenge, die Lara schafft, der Sektorgraph aber nicht kennt: Spruenge,
# Kletterstellen, Monkeyswing. Format "Raum,x,z" -> "Raum,x,z", beidseitig.
# Das ist die Handkuration fuer die Klasse "Flood stoppt an der Geometrie".
$MANUAL_LINK = @{
}
$manualLinks = @( if($MANUAL_LINK.ContainsKey($LevelName)){ $MANUAL_LINK[$LevelName] } )

$adj = @{}
function Add-Edge($a, $b) {
    if (-not $adj.ContainsKey($a)) { $adj[$a] = New-Object System.Collections.Generic.HashSet[string] }
    [void]$adj[$a].Add($b)
}

foreach($s in $d.sectors){
    if ($s.isWall) { continue }
    $key = "$($s.room),$($s.sectorX),$($s.sectorZ)"
    $rm = $rooms[[int]$s.room]
    $wx = $rm.originX + $s.sectorX * 1024
    $wz = $rm.originZ + $s.sectorZ * 1024

    # Nachbarn im selben Raum
    foreach($dz in @(-1,0,1)){ foreach($dx in @(-1,0,1)){
        if (($dx -eq 0 -and $dz -eq 0) -or ($dx -ne 0 -and $dz -ne 0)) { continue }
        $nk = "$($s.room),$($s.sectorX + $dx),$($s.sectorZ + $dz)"
        if ($sec.ContainsKey($nk) -and -not $sec[$nk].isWall) { Add-Edge $key $nk }
    }}

    # Portal / vertikale Verbindungen
    $leavesOneway = $onewayOut -contains [int]$s.room
    if ($null -ne $s.portalTarget) {
        $t = Get-SectorInRoom ([int]$s.portalTarget) $wx $wz
        if ($t -and -not $leavesOneway) { Add-Edge $key "$($t.room),$($t.sectorX),$($t.sectorZ)" }
    }
    foreach($vr in @($s.roomBelow, $s.roomAbove)){
        if ($vr -ne 255) {
            $t = Get-SectorInRoom ([int]$vr) $wx $wz
            if ($t -and -not $leavesOneway) { Add-Edge $key "$($t.room),$($t.sectorX),$($t.sectorZ)" }
        }
    }
}

# Flipmap: ein Alternate-Raum tauscht die Geometrie des Basisraums an Ort und
# Stelle aus, Sektor (x,z) gegen Sektor (x,z). Fuer die Item-Platzierung sind
# Alternate-Raeume gesperrt (FlipMap() stellt item_number des Basisraums wieder
# her, dort liegen nie Items) - fuer die Progression zaehlen sie aber sehr wohl,
# weil ihre Trigger feuern. Andrea1 endet genau so: Raum 234 ist der Alternate
# von 171, und erst nach FlipMap 8 liegt dort der Cutscene-Trigger.
foreach($r in $d.rooms){
    if ($null -eq $r.alternateRoom -or $r.alternateRoom -eq '') { continue }
    $ai = [int]$r.alternateRoom
    if ($ai -eq [int]$r.index) { continue }
    # Fuer jeden Sektor, der im Alternate begehbar ist: mit dem Basisraum
    # verbinden. Deckungsgleich, wo beide begehbar sind - und zusaetzlich ueber
    # die Nachbarsektoren, wo die Basis eine Wand hat. Genau das ist der Sinn
    # vieler Flipmaps: der Flip *oeffnet* einen Durchgang, wo vorher Wand war
    # (joby2: der Kran-Cutscene-Flip auf Raum 4/43 gibt den Levelfortgang frei).
    # Ohne den zweiten Fall bleibt der ganze Alternate-Teil eine eigene Insel
    # und der Flood stoppt am Levelanfang.
    foreach($s in $d.sectors){
        if ([int]$s.room -ne $ai -or $s.isWall) { continue }
        $ak = "$ai,$($s.sectorX),$($s.sectorZ)"
        $bk = "$($r.index),$($s.sectorX),$($s.sectorZ)"
        if ($sec.ContainsKey($bk) -and -not $sec[$bk].isWall) {
            Add-Edge $bk $ak; Add-Edge $ak $bk
            continue
        }
        foreach($dz in @(-1,0,1)){ foreach($dx in @(-1,0,1)){
            if (($dx -eq 0 -and $dz -eq 0) -or ($dx -ne 0 -and $dz -ne 0)) { continue }
            $nb = "$($r.index),$([int]$s.sectorX + $dx),$([int]$s.sectorZ + $dz)"
            if ($sec.ContainsKey($nb) -and -not $sec[$nb].isWall) { Add-Edge $nb $ak; Add-Edge $ak $nb }
        }}
    }
}

# --- Tueren: welcher Sektor wird blockiert? ---------------------------------
$doorSector = @{}   # itemIndex -> sectorKey
$openAtStart = @()
foreach($i in $d.items){
    if ($DOOR_TYPES -notcontains [int]$i.typeId) { continue }
    $k = "$($i.room),$($i.sectorX),$($i.sectorZ)"
    if (-not $sec.ContainsKey($k)) { continue }
    # Eine Tuer, deren Item-Flags die 5 Code-Bits (IFL_CODEBITS, 0x3E00) schon
    # beim Laden alle gesetzt haben, gilt der Engine sofort als vollstaendig
    # getriggert und geht ohne jeden Trigger beim Levelstart auf. In trview
    # steht dann "Flags 11111". joby2 Item 138 ist genau so ein Fall - er sah
    # aus wie eine unschaltbare Tuer, ist aber von Anfang an offen.
    if (([int]$i.flags -band 0x3E00) -eq 0x3E00) { $openAtStart += ,[int]$i.index; continue }
    $doorSector[[int]$i.index] = $k
}

# --- Was oeffnet welche Tuer? ------------------------------------------------
# openedBy[itemIndex] = Liste von Anforderungen (je ein Trigger)
$openedBy = @{}
foreach($t in $d.triggers){
    foreach($c in $t.commands){
        if ($c.type -ne 'Object') { continue }
        $tgt = [int]$c.index
        if (-not $openedBy.ContainsKey($tgt)) { $openedBy[$tgt] = @() }
        $openedBy[$tgt] += ,$t
    }
}

function Get-TriggerRequirement($t) {
    # Gibt zurueck: @{ Kind='auto'|'reach'|'item'; Sector=..; NeedItemType=..; Desc=.. }
    $tk = "$($t.room),$($t.sectorX),$($t.sectorZ)"
    switch ($t.triggerType) {
        { $_ -in 'Trigger','Pad','HeavyTrigger','Combat','Dummy' } {
            return @{ Kind='reach'; Sector=$tk; Desc="Trigger betreten ($tk)" }
        }
        'Switch' {
            $ri = $t.refItem
            $d2 = if($null -ne $ri -and $items.ContainsKey([int]$ri)){ "Schalter Item $ri (Raum $($items[[int]$ri].room))" } else { "Schalter" }
            # Operable: Lara bedient einen Schalter vom Nachbarsektor aus, sie
            # muss den Triggersektor nicht betreten. Das ist nicht kosmetisch -
            # Tuer-Item und Schalter stehen regelmaessig auf demselben Sektor
            # (Andrea1: Door 7 Item 71 und Shoot switch 1 Item 72 beide auf
            # 139/(1,3)). Ohne diese Unterscheidung verlangt der Analyser, einen
            # Sektor zu betreten, den er selbst wegen der Tuer sperrt -> Deadlock,
            # und der ganze dahinterliegende Levelteil faellt aus der Zonenkette.
            return @{ Kind='reach'; Sector=$tk; Operable=$true; Desc=$d2 }
        }
        'Key' {
            $ri = $t.refItem
            if ($null -eq $ri -or -not $items.ContainsKey([int]$ri)) { return @{ Kind='reach'; Sector=$tk; Operable=$true; Desc="Schloss (unbekannt)" } }
            $hole = $items[[int]$ri]; $ht = [int]$hole.typeId
            $needType = $null; $label = $null
            if ($ht -ge $KEY_HOLE_BASE -and $ht -lt ($KEY_HOLE_BASE+8)) {
                $n = $ht - $KEY_HOLE_BASE; $needType = $KEY_ITEM_BASE + $n; $label = "Key Item$($n+1)"
            } elseif ($ht -ge $PUZZLE_HOLE_BASE -and $ht -lt ($PUZZLE_HOLE_BASE+8)) {
                $n = $ht - $PUZZLE_HOLE_BASE; $needType = $PUZZLE_ITEM_BASE + $n; $label = "Puzzle Item$($n+1)"
            }
            return @{ Kind='item'; Sector=$tk; NeedItemType=$needType; Desc="$label in Schloss Item $ri (Raum $($hole.room))" }
        }
        'Pickup' {
            $ri = $t.refItem
            return @{ Kind='reach'; Sector=$tk; Desc="Pickup-Trigger" }
        }
        default { return @{ Kind='reach'; Sector=$tk; Desc="$($t.triggerType)" } }
    }
}

function Test-ReqSector($req, $reached) {
    # Bodentrigger (Pad/Heavy/Trigger) muss Lara wirklich betreten. Schalter und
    # Schloesser bedient sie dagegen vom Nachbarsektor aus - deshalb zaehlt dort
    # auch, wenn nur ein angrenzender Sektor erreicht ist.
    if ($reached.Contains($req.Sector)) { return $true }
    if (-not $req.Operable) { return $false }
    if (-not $adj.ContainsKey($req.Sector)) { return $false }
    foreach($n in $adj[$req.Sector]){ if ($reached.Contains($n)) { return $true } }
    return $false
}

# --- Zonen-Flut --------------------------------------------------------------
$lara = $d.items | Where-Object { $_.typeId -eq 0 } | Select-Object -First 1
$startKey = "$($lara.room),$($lara.sectorX),$($lara.sectorZ)"

$blocked = @{}   # sectorKey -> itemIndex der Tuer
foreach($kv in $doorSector.GetEnumerator()){ $blocked[$kv.Value] = $kv.Key }

foreach($ml in $manualLinks){ Add-Edge $ml[0] $ml[1]; Add-Edge $ml[1] $ml[0] }

$zoneOf = @{}    # sectorKey -> Zonennummer
$openDoors = New-Object System.Collections.Generic.HashSet[int]
$zone = 0
$log = @()

$reached = New-Object System.Collections.Generic.HashSet[string]
[void]$reached.Add($startKey)

# Alle Trigger, die ueberhaupt eine Tuer schalten - einmal vorab aufgeloest.
$allTrig = @(); $tn = 0
foreach($t in $d.triggers){
    $hitsDoor = $false
    foreach($c in $t.commands){ if ($c.type -eq 'Object' -and $doorSector.ContainsKey([int]$c.index)) { $hitsDoor = $true } }
    if (-not $hitsDoor) { continue }
    $allTrig += ,@{ Id = $tn; T = $t; Req = (Get-TriggerRequirement $t) }
    $tn++
}
$firedTrig = New-Object System.Collections.Generic.HashSet[int]
$consumed  = @{}   # typeId -> wie viele Instanzen schon in Slots stecken
$doorMask  = @{}   # doorItem -> bisher gesetzte Maskenbits
$doorWhy   = @{}   # doorItem -> Liste der beitragenden Anforderungen
$itemNeed  = @()   # je Slot-Befuellung: welcher Typ, in welcher Zone gebraucht

while ($true) {
    # fluten
    $q = New-Object System.Collections.Generic.Queue[string]
    foreach($k in $reached){ $q.Enqueue($k) }
    while($q.Count -gt 0){
        $k = $q.Dequeue()
        if (-not $zoneOf.ContainsKey($k)) { $zoneOf[$k] = $zone }
        if (-not $adj.ContainsKey($k)) { continue }
        foreach($n in $adj[$k]){
            if ($reached.Contains($n)) { continue }
            if ($blocked.ContainsKey($n) -and -not $openDoors.Contains($blocked[$n])) { continue }
            [void]$reached.Add($n); $zoneOf[$n] = $zone; $q.Enqueue($n)
        }
    }

    # Wie viele Instanzen jedes Typs sind erreichbar? Anzahl, nicht nur "ja/nein" -
    # zwei Slots desselben Typs brauchen zwei Items.
    $haveCounts = @{}
    foreach($i in $d.items){
        $k = "$($i.room),$($i.sectorX),$($i.sectorZ)"
        if (-not $reached.Contains($k)) { continue }
        $ti = [int]$i.typeId
        if (-not $haveCounts.ContainsKey($ti)) { $haveCounts[$ti] = 0 }
        $haveCounts[$ti]++
    }

    # Erreichbare Trigger ausloesen bzw. Slots befuellen. Ein eingesetztes Item
    # ist verbraucht und steht fuer keinen zweiten Slot mehr zur Verfuegung.
    foreach($e in $allTrig){
        if ($firedTrig.Contains($e.Id)) { continue }
        if (-not (Test-ReqSector $e.Req $reached)) { continue }
        if ($e.Req.Kind -eq 'item') {
            $nt = $e.Req.NeedItemType
            if ($null -eq $nt) { continue }
            $nt = [int]$nt
            $has  = if($haveCounts.ContainsKey($nt)){ $haveCounts[$nt] } else { 0 }
            $used = if($consumed.ContainsKey($nt)){ $consumed[$nt] } else { 0 }
            if (($has - $used) -le 0) { continue }
            $consumed[$nt] = $used + 1
            $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc=$e.Req.Desc }
        }
        [void]$firedTrig.Add($e.Id)
        # Maske dieses Triggers auf alle Ziel-Tueren anrechnen.
        foreach($c in $e.T.commands){
            if ($c.type -ne 'Object') { continue }
            $tgt = [int]$c.index
            if (-not $doorMask.ContainsKey($tgt)) { $doorMask[$tgt] = 0; $doorWhy[$tgt] = @() }
            $doorMask[$tgt] = $doorMask[$tgt] -bor ([int]$e.T.mask)
            $doorWhy[$tgt] += ,$e.Req
        }
    }

    # Eine Tuer oeffnet erst, wenn ihre Maske vollstaendig ist. TR setzt pro
    # Trigger Bits in item->flags (IFL_CODEBITS, 5 Bits) und oeffnet erst bei
    # (flags & CODEBITS) == CODEBITS. Andrea1: Slot 64 liefert Maske 3, Slot 63
    # Maske 28 - erst zusammen 31. Ohne diese Regel wuerde eine Ein-Item-Loesung
    # als ausreichend gelten und der Randomizer koennte das Level unloesbar machen.
    $newlyOpened = @()
    foreach($kv in $doorSector.GetEnumerator()){
        $di = $kv.Key
        if ($openDoors.Contains($di)) { continue }
        if (-not $doorMask.ContainsKey($di)) { continue }
        if (($doorMask[$di] -band 31) -ne 31) { continue }
        $newlyOpened += ,@{ Door=$di; Req=$doorWhy[$di][-1]; All=$doorWhy[$di] }
    }

    # Skript-Tueren: gehen auf, sobald der Raum erreicht ist, in dem die Engine
    # das Ereignis ausloest - nicht frueher.
    foreach($sd in $scriptDoors.GetEnumerator()){
        $di = [int]$sd.Key
        if ($openDoors.Contains($di)) { continue }
        $needRoom = [int]$sd.Value
        $hit = $false
        foreach($k in $reached){ if ([int](($k -split ',')[0]) -eq $needRoom) { $hit = $true; break } }
        if ($hit) { $newlyOpened += ,@{ Door=$di; Req=@{ Desc="Skript-Ereignis (Raum $needRoom erreicht)" } } }
    }

    if ($newlyOpened.Count -eq 0) { break }
    foreach($n in $newlyOpened){
        [void]$openDoors.Add($n.Door)
        $log += ,@{ Zone=$zone; Door=$n.Door; Req=$n.Req }
    }
    $zone++
}

# --- Ausgabe ----------------------------------------------------------------
Write-Output "=============================================================="
Write-Output " Progressions-Zonen: $LevelName"
Write-Output "=============================================================="
Write-Output "Laras Start: Raum $($lara.room), Sektor ($($lara.sectorX),$($lara.sectorZ)) -> Zone 0"
Write-Output ""
Write-Output "--- Zonenuebergaenge (Blocker) ---"
foreach($e in $log){
    $di = $e.Door; $it = $items[$di]
    "Zone {0} -> {1}: Tuer Item {2} (typeId {3}, Raum {4})  geoeffnet durch: {5}" -f `
        $e.Zone, ($e.Zone+1), $di, $it.typeId, $it.room, $e.Req.Desc
}
Write-Output ""
$maxZone = ($zoneOf.Values | Measure-Object -Maximum).Maximum
Write-Output "--- Sektoren pro Zone ---"
for($z=0; $z -le $maxZone; $z++){
    $cnt = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z }).Count
    $rms = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z } | ForEach-Object { ($_.Key -split ',')[0] } | Sort-Object -Unique).Count
    "Zone {0}: {1,5} Sektoren in {2,3} Raeumen" -f $z,$cnt,$rms
}
$unreachedSec = @($d.sectors | Where-Object { -not $_.isWall -and -not $zoneOf.ContainsKey("$($_.room),$($_.sectorX),$($_.sectorZ)") })
Write-Output "Nie erreicht: $($unreachedSec.Count) begehbare Sektoren"
Write-Output ""
Write-Output "--- Key-/Puzzle-Items: erlaubter Platzierungsbereich ---"
# Zaehl-Regel statt Typ-Regel: braucht ein Level zwei Slots desselben Typs,
# reicht es nicht, dass "irgendein" Item frueh genug liegt. Die k-te Instanz
# muss spaetestens in der Zone liegen, in der der k-te Slot bedient wird.
$byType = @{}
foreach($n in $itemNeed){
    $t = [int]$n.Type
    if (-not $byType.ContainsKey($t)) { $byType[$t] = @() }
    $byType[$t] += ,$n
}
foreach($kv in $byType.GetEnumerator() | Sort-Object Name){
    $nt = [int]$kv.Key
    $needs = @($kv.Value | Sort-Object { $_.Zone })
    "TypeId {0}: {1} Instanz(en) noetig" -f $nt, $needs.Count
    for($j=0; $j -lt $needs.Count; $j++){
        "    Instanz {0} -> muss in Zone <= {1} liegen  ({2})" -f ($j+1), $needs[$j].Zone, $needs[$j].Desc
    }
    $cur = @($d.items | Where-Object { [int]$_.typeId -eq $nt })
    $withZone = @($cur | ForEach-Object {
        $k = "$($_.room),$($_.sectorX),$($_.sectorZ)"
        $z = if($zoneOf.ContainsKey($k)){ [int]$zoneOf[$k] } else { 9999 }
        @{ It=$_; Z=$z }
    } | Sort-Object { $_.Z })
    "    Vanilla: {0} Instanz(en) vorhanden" -f $withZone.Count
    for($j=0; $j -lt $withZone.Count; $j++){
        $zt = if($withZone[$j].Z -eq 9999){ "nie erreicht" } else { $withZone[$j].Z }
        $ok = if ($j -ge $needs.Count) { "Reserve" }
              elseif ($withZone[$j].Z -le $needs[$j].Zone) { "OK" }
              else { "WIDERSPRUCH" }
        "      Item {0,3} in Raum {1,4} -> Zone {2,-12} [{3}]" -f $withZone[$j].It.index,$withZone[$j].It.room,$zt,$ok
    }
    if ($withZone.Count -lt $needs.Count) { "      [WIDERSPRUCH] zu wenige Instanzen im Level" }
}

Write-Output ""
Write-Output "--- Warum kam der Flood nicht weiter? ---"
# Ohne diese Diagnose ist "0 Widersprueche" wertlos: ein Level, in dem der Flood
# sofort stehenbleibt, leitet gar keine Key-Item-Anforderung ab und meldet
# deshalb faelschlich "alles in Ordnung". Hier steht, welche Tuer am Rand des
# erreichten Bereichs zu ist und woran es liegt - und wenn dort gar keine Tuer
# steht, ist die Ursache Geometrie (nicht modellierter Uebergang), nicht Logik.
$frontier = 0
foreach($kv in $doorSector.GetEnumerator()){
    $di = $kv.Key; $ks = $kv.Value
    if ($openDoors.Contains($di)) { continue }
    $touch = $false
    if ($adj.ContainsKey($ks)) { foreach($n in $adj[$ks]){ if ($reached.Contains($n)) { $touch = $true; break } } }
    if (-not $touch) { continue }
    $frontier++
    $it = $items[$di]
    $have = if($doorMask.ContainsKey($di)){ $doorMask[$di] } else { 0 }
    "Tuer Item {0,3} (typeId {1}, Raum {2,4}) Maske {3,2}/31" -f $di,$it.typeId,$it.room,$have
    $trigs = @($allTrig | Where-Object { $_.T.commands | Where-Object { $_.type -eq 'Object' -and [int]$_.index -eq $di } })
    if ($trigs.Count -eq 0) { "    kein Trigger im Level schaltet diese Tuer"; continue }
    foreach($e in $trigs){
        $st = if ($firedTrig.Contains($e.Id)) { 'ausgeloest' }
              elseif (-not (Test-ReqSector $e.Req $reached)) { 'Triggersektor nicht erreichbar' }
              else { 'Item fehlt' }
        "    Maske {0,2}  {1,-32} -> {2}" -f $e.T.mask, $e.Req.Desc, $st
    }
}
if ($frontier -eq 0) {
    Write-Output "Keine geschlossene Tuer am Rand - der Flood stoppt an der Geometrie,"
    Write-Output "nicht an einem Blocker (Sprung/Klettern/Teleport/Wasser nicht modelliert)."
}

Write-Output ""
Write-Output "--- Ausruestungs-Sperren (manuell, nicht aus Leveldaten ableitbar) ---"
# Ein Shoot switch steht in den Leveldaten nur als Schalter - dass er eine
# bestimmte Waffe braucht, steht nirgends. Andrea1: die Glocke (Item 75) laesst
# sich nur mit Revolver/Desert Eagle plus Lasersight abschiessen. Item 72
# dagegen ist NICHT gesperrt - der laesst sich auch mit den Pistolen treffen,
# indem man im Knien zielt (vom Nutzer im Spiel bestaetigt).
$EQUIP_GATE = @{
    'Andrea1' = @{ 75 = @{ Need = @(347,351); Desc = 'Revolver + Lasersight (Glocke abschiessen)' } }
}
if (-not $EQUIP_GATE.ContainsKey($LevelName)) {
    Write-Output "Keine hinterlegt."
} else {
    foreach($g in $EQUIP_GATE[$LevelName].GetEnumerator()){
        $swItem = [int]$g.Key
        # In welcher Zone wird dieser Schalter gebraucht?
        $z = ($log | Where-Object { $_.Req.Desc -like "*Item $swItem *" } | ForEach-Object { $_.Zone } | Sort-Object | Select-Object -First 1)
        if ($null -eq $z) { $z = '?' }
        "Schalter Item {0}: {1}" -f $swItem, $g.Value.Desc
        "  wird gebraucht in Zone {0} -> Ausruestung muss in Zone <= {0} liegen" -f $z
        foreach($nt in $g.Value.Need){
            $inst = @($d.items | Where-Object { [int]$_.typeId -eq [int]$nt })
            foreach($c in $inst){
                $k = "$($c.room),$($c.sectorX),$($c.sectorZ)"
                $iz = if($zoneOf.ContainsKey($k)){ $zoneOf[$k] } else { 'nie erreicht' }
                $ok = if($zoneOf.ContainsKey($k) -and $z -ne '?' -and $zoneOf[$k] -le $z){ 'OK' } else { 'PRUEFEN' }
                "    typeId {0,4} Item {1,3} in Raum {2,4} -> Zone {3,-12} [{4}]" -f $nt,$c.index,$c.room,$iz,$ok
            }
        }
    }
}

Write-Output ""
Write-Output "--- Selbsttest: Level durchspielbar? ---"
# Andrea1 hat kein EndLevel-Kommando. Das Level endet ueber einen Cutscene-
# Trigger: nach der Cutscene startet der Gameflow das naechste Level. Der
# Cutscene-Trigger ist damit der Terminus der Zonenkette, EndLevel nur der
# Sonderfall fuer Level, die ihn tatsaechlich benutzen.
$endTrig = @($d.triggers | Where-Object { $_.commands | Where-Object { $_.type -in @('EndLevel','Cutscene') } })
if ($endTrig.Count -eq 0) {
    Write-Output "Weder EndLevel- noch Cutscene-Trigger gefunden - Terminus unbekannt."
} else {
    foreach($t in $endTrig){
        $k = "$($t.room),$($t.sectorX),$($t.sectorZ)"
        $z = if($zoneOf.ContainsKey($k)){ "Zone $($zoneOf[$k])" } else { "NICHT ERREICHBAR" }
        $kind = ($t.commands | Where-Object { $_.type -in @('EndLevel','Cutscene') } | Select-Object -First 1)
        "{0} {1} in Raum {2,4} Sektor ({3},{4}) -> {5}" -f $kind.type,$kind.index,$t.room,$t.sectorX,$t.sectorZ,$z
    }
}
