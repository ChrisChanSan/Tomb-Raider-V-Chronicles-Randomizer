# Liest die Secret-Listen aus Kuration\Secrets und macht daraus einen
# maschinenlesbaren Plan (#53).
#
# EINE DATEI = EINE LISTE. Der Dateiname traegt Autor und Kategorie:
#   ChrisChanSan_Glitchless.txt  ->  Dropdown "Glitchless", Anzeige "ChrisChanSan"
#   ChrisChanSan_Glitched.txt    ->  Dropdown "Glitched",   Anzeige "ChrisChanSan"
# Dateien mit fuehrendem Unterstrich erscheinen in keinem Dropdown (Referenzen).
#
# Wer eine eigene Liste beisteuern will, legt eine weitere Datei dorthin -
# am Programm ist nichts zu aendern.
#
# Zwei Betriebsarten brauchen ueberhaupt keine Liste und stehen deshalb nicht
# in diesem Ordner: "Vanilla" (Secrets bleiben liegen) und "Pool" (sie wandern
# wie gewoehnliche Pickups, kann ueberall auftauchen).
#
# Die Listen sind von Hand gepflegt und sollen das bleiben - in trview
# ablesbar, ohne Werkzeug aenderbar. Dieser Parser passt sich ihnen an.
#
# Aufruf:
#   SecretPlan.ps1 -Liste                       alle verfuegbaren Listen zeigen
#   SecretPlan.ps1 -Autor X -Kategorie Y        Plan dieser Liste ausgeben
#   ... zusaetzlich -LevelName A -AlsJson Pfad
param(
    [string]$Ordner = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Kuration\Secrets",
    [switch]$Liste,
    [string]$Autor = "",
    [string]$Kategorie = "",
    [string]$LevelName = "",
    [string]$AlsJson = "",
    # Gegenprobe gegen die echten Leveldaten: stimmen die Level_ID wirklich mit
    # den Secrets (Typ 223) der Vanilla-Datei ueberein? Menschen vertippen sich,
    # und ein falscher Index verschiebt still das falsche Item.
    [string]$VanillaDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Steam Version\data\Vanilla",
    [string]$ItemPruef = "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender\ItemPruef.exe",
    [string]$AlsPatch = "",
    [string]$Trc = "",
    # Level-Info-JSON: liefert die Raumurspruenge fuer Raum+Sektor -> Welt.
    [string]$LevelInfo = ""
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

# Winkel: die Liste ist in GRAD geschrieben (@Angle90 = eine Vierteldrehung),
# der Patch will die rohe TR-Einheit (yRot, int16: 65536 = volle Drehung).
# Ohne die Umrechnung wurde aus 90 Grad eine halbe Gradminute - im Level
# unsichtbar, weil das Item einfach ungedreht dastand.
function Grad-InRoh([int]$grad) {
    $roh = [int][math]::Round($grad * 65536.0 / 360.0) % 65536
    if ($roh -gt 32767) { $roh -= 65536 }
    if ($roh -lt -32768) { $roh += 65536 }
    return $roh
}

# Eine Extra-Option zerlegen. Es gibt drei Formen, unterschieden durch 'art':
#
#   'item'    add Type_ID(N)@Room R(xA,zB)          - Item anhaengen
#             dazu wahlweise @OCB(n), @Angle(w) und ein Trigger darauf
#   'trigger' add Trigger_Type(Art)@Room R(xA,zB)   - NUR ein Trigger, kein Item
#   'ersetz'  replace Item_Level_ID(N)with->Type_ID(T) - vorhandenes Item umtypen
#             dazu wahlweise @OCB(n), @Angle(w)
#
# Alles, was der Parser nicht erkennt, wird gemeldet - ein vertipptes "@Angel0"
# soll nicht stillschweigend verschwinden.
function Lies-Option([string]$o) {
    $o = $o.Trim()
    $e = $null

    # --- Form 3: ein vorhandenes Item umtypen -----------------------------
    if ($o -match 'replace\s+Item_Level_ID\(\s*(\d+)\s*\)\s*with\s*->\s*Type_?ID\(\s*(\d+)\s*\)') {
        $e = [PSCustomObject]@{
            art = 'ersetz'; index = [int]$Matches[1]; typeId = [int]$Matches[2]
            room = $null; sectorX = $null; sectorZ = $null
            ocb = $null; angle = $null; hoehe = $null; xOff = $null; zOff = $null; wieItem = $null; triggerArt = $null; triggerZiele = @()
        }
    }
    # --- Form 1: Item anhaengen -------------------------------------------
    elseif ($o -match 'add\s+Type_ID\(\s*(\d+)\s*\)' ) {
        $typ = [int]$Matches[1]
        # Die Stelle kommt entweder aus Raum+Sektor oder von einem Bezugsitem
        # (@LikeItem). Eines von beiden muss dastehen.
        if ($o -match '@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') {
            $rm = [int]$Matches[1]; $sx = [int]$Matches[2]; $sz = [int]$Matches[3]
        } elseif ($o -match '@\s*LikeItem\(\s*\d+\s*\)') {
            $rm = $null; $sx = $null; $sz = $null
        } else { return $null }
        $e = [PSCustomObject]@{
            art = 'item'; index = $null; typeId = $typ
            room = $rm; sectorX = $sx; sectorZ = $sz
            ocb = $null; angle = $null; hoehe = $null; xOff = $null; zOff = $null; wieItem = $null; triggerArt = $null; triggerZiele = @()
        }
    }
    # --- Form 2: nur ein Trigger, ohne Item -------------------------------
    elseif ($o -match 'add\s+Trigger_Type\(') {
        if ($o -notmatch '@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') { return $null }
        $e = [PSCustomObject]@{
            art = 'trigger'; index = $null; typeId = $null
            room = [int]$Matches[1]; sectorX = [int]$Matches[2]; sectorZ = [int]$Matches[3]
            ocb = $null; angle = $null; hoehe = $null; xOff = $null; zOff = $null; wieItem = $null; triggerArt = $null; triggerZiele = @()
        }
    }
    else { return $null }

    if ($o -match '@\s*OCB\(\s*(\d+)\s*\)')       { $e.ocb   = [int]$Matches[1] }
    if ($o -match '@\s*Angle\s*\(?\s*(-?\d+)')    { $e.angle = [int]$Matches[1] }
    if ($o -match '@\s*Height\(\s*(-?\d+)\s*\)') { $e.hoehe = [int]$Matches[1] }
    if ($o -match '@\s*XOff\(\s*(-?\d+)\s*\)')      { $e.xOff    = [int]$Matches[1] }
    if ($o -match '@\s*ZOff\(\s*(-?\d+)\s*\)')      { $e.zOff    = [int]$Matches[1] }
    # Position eines vorhandenen Items uebernehmen. Gebraucht fuers Stapeln:
    # yAuto rechnet die ECHTE Bodenlage in der Sektormitte, und auf einem
    # leicht geneigten Sektor liegt die bis zu 128 Einheiten neben dem flachen
    # Grundwert, auf den Core sein Item gesetzt hat. Fuer eine Doppeltuer muss
    # das obere Teil aber exakt ueber dem unteren sitzen, nicht ueber dem
    # Boden - also wird die Position des Bezugsitems kopiert und nur @Height
    # daraufgerechnet.
    if ($o -match '@\s*LikeItem\(\s*(\d+)\s*\)') { $e.wieItem = [int]$Matches[1] }
    if ($o -match 'Trigger_Type\(\s*(\w+)\s*\).*?Item_Level_ID\(\s*([^)]*)\)') {
        $e.triggerArt = [string]$Matches[1]
        # Mehrere Ziele mit Komma: "Item_Level_ID(13, Neu1)". Ein Eintrag ist
        # entweder ein vorhandener Itemindex oder "Neu<k>" - das k-te Item,
        # das die Extra_Options dieses Levels selbst anlegen. Ohne das liesse
        # sich ein gerade erst erzeugtes Item nicht ansprechen, denn seinen
        # Index kennt erst der Patchlauf (Doppeltuer: unten ersetzt, oben neu,
        # ein Schalter fuer beide).
        foreach ($teil in ([string]$Matches[2] -split ',')) {
            $t = $teil.Trim()
            if ($t -match '^\d+$')            { $e.triggerZiele += [int]$t }
            elseif ($t -match '^Neu\s*(\d+)$'){ $e.triggerZiele += "Neu$($Matches[1])" }
            elseif ($t.Length -gt 0)          { $e.triggerZiele += "?$t" }
        }
    }

    # Bei 'ersetz' verweist Item_Level_ID auf das Item SELBST, nicht auf ein
    # Triggerziel - sonst wuerde die Zeile sich selbst als Ziel eintragen.
    if ($e.art -eq 'ersetz') { $e.triggerArt = $null; $e.triggerZiele = @() }

    # Ein Trigger ohne Ziel ist sinnlos und faellt sonst still auf art 0.
    if ($e.art -eq 'trigger' -and $e.triggerZiele.Count -eq 0) { return $null }

    # Strenge statt Nachsicht: was der Parser NICHT erkannt hat, wird gemeldet.
    $rest = $o
    $rest = $rest -replace 'replace\s+', ''
    $rest = $rest -replace 'with\s*->', ''
    $rest = $rest -replace 'add\s+', ''
    $rest = $rest -replace 'Type_?ID\(\s*\d+\s*\)', ''
    $rest = $rest -replace '@\s*Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)', ''
    $rest = $rest -replace '@\s*OCB\(\s*\d+\s*\)', ''
    $rest = $rest -replace '@\s*Angle\s*\(?\s*-?\d+\s*\)?', ''
    $rest = $rest -replace 'Trigger_Type\(\s*\w+\s*\)', ''
    $rest = $rest -replace 'to\s*->\s*\w+', ''
    $rest = $rest -replace 'Item_Level_ID\([^)]*\)', ''
    $rest = $rest -replace '@\s*Height\(\s*-?\d+\s*\)', ''
    $rest = $rest -replace '@\s*LikeItem\(\s*\d+\s*\)', ''
    $rest = $rest -replace '@\s*XOff\(\s*-?\d+\s*\)', ''
    $rest = $rest -replace '@\s*ZOff\(\s*-?\d+\s*\)', ''
    $rest = ($rest -replace '[()+;\s]', '')
    if ($rest.Length -gt 0) { $e | Add-Member -NotePropertyName unbekannt -NotePropertyValue $rest }
    return $e
}

function Lies-Liste([string]$pfad) {
    $level = @(); $warn = @()
    foreach ($z in (Get-Content $pfad)) {
        if ($z -match '^\s*#') { continue }
        if ($z -notmatch '^\s*(\w+)\.trc\s+(\d+)\s+(\d+)\s+\(([^)]*)\)\s*(.*)$') { continue }
        $lvl = $Matches[1]; $anzahl = [int]$Matches[2]; $typ = [int]$Matches[3]
        $ids = @($Matches[4] -split ',' | ForEach-Object { [int]$_.Trim() })
        $rest = $Matches[5]

        # Die Stellen stehen VOR den Extra_Options; die Optionen nennen selbst
        # Die Stellen stehen VOR den Extra_Options; die Optionen nennen selbst
        # Raeume und wuerden sonst als zusaetzliche Secrets mitgezaehlt.
        #
        # Die Grenze war frueher das woertliche "(add" - und damit blind fuer
        # jede Gruppe, die anders anfaengt. Eine Zeile mit "(replace ...)" fiel
        # STILL heraus: keine Option gelesen, keine Warnung. Genau das, was der
        # Kopf dieser Datei ausschliesst.
        #
        # Jetzt ohne Wortliste: die Stellen bestehen aus "Room<N>(x..,z..)", und
        # deren Klammern werden ausgeblendet. Die erste Klammer, die danach noch
        # uebrig ist, faengt die Extra_Options an - egal mit welchem Verb.
        $maske = $rest.ToCharArray()
        foreach ($m in [regex]::Matches($rest, 'Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)')) {
            for ($i = $m.Index; $i -lt $m.Index + $m.Length; $i++) { $maske[$i] = ' ' }
        }
        $pExtra = (-join $maske).IndexOf('(')
        $vorn = if ($pExtra -ge 0) { $rest.Substring(0, $pExtra) } else { $rest }
        $stellen = @()
        foreach ($m in [regex]::Matches($vorn, 'Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)')) {
            $stellen += [PSCustomObject]@{ room=[int]$m.Groups[1].Value; sectorX=[int]$m.Groups[2].Value; sectorZ=[int]$m.Groups[3].Value }
        }

        # Gruppen trennen. Empfohlen ist ';' - eindeutig, weil es sonst nirgends
        # vorkommt. Das urspruengliche '+' bleibt erlaubt (die vorhandenen
        # Listen benutzen es), wird aber auf Klammertiefe 0 getrennt, weil '+'
        # auch INNERHALB einer Gruppe steht (vor Trigger_Type).
        $extras = @()
        if ($pExtra -ge 0) {
            $roh = $rest.Substring($pExtra)
            $gruppen = if ($roh.Contains(';')) { Split-AufTiefe0 $roh ';' } else { Split-AufTiefe0 $roh '+' }
            foreach ($g in $gruppen) {
                $o = Lies-Option $g
                if ($null -eq $o) { $warn += "${lvl}: Option nicht lesbar -> $g"; continue }
                if ($o.PSObject.Properties.Name -contains 'unbekannt') {
                    $warn += "${lvl}: nicht verstandener Teil in einer Option -> '$($o.unbekannt)'"
                }
                $extras += $o
            }
        }

        if ($ids.Count -ne $anzahl) { $warn += "${lvl}: Anzahl $anzahl, aber $($ids.Count) Level_ID" }

        # Gegenprobe an der echten Vanilla-Datei: sind das wirklich die
        # Secrets? Ein vertippter Index verschiebt sonst still ein ganz
        # anderes Item, und das faellt erst im Spiel auf.
        $trc = Join-Path $VanillaDir "$lvl.trc"
        if ((Test-Path $trc) -and (Test-Path $ItemPruef)) {
            $echt = @(& $ItemPruef $trc $typ 2>$null | ForEach-Object {
                if ($_ -match "^\s*(\d+)\s+$typ\s") { [int]$Matches[1] } })
            if ($echt.Count -gt 0) {
                $fehlt = @($ids | Where-Object { $echt -notcontains $_ })
                if ($fehlt.Count -gt 0) { $warn += "${lvl}: Item $($fehlt -join ',') ist in der Datei kein Typ $typ" }
                if ($echt.Count -ne $ids.Count) { $warn += "${lvl}: Datei hat $($echt.Count) Stueck Typ $typ, die Liste nennt $($ids.Count)" }
            }
        }
        if ($stellen.Count -gt 0 -and $stellen.Count -ne $ids.Count) {
            $warn += "${lvl}: $($ids.Count) Secrets, aber $($stellen.Count) neue Stellen"
        }

        $level += [PSCustomObject]@{ level=$lvl; anzahl=$anzahl; typeId=$typ; ids=$ids; stellen=$stellen; extras=$extras }
    }
    return [PSCustomObject]@{ level = $level; warnungen = $warn }
}

if (-not (Test-Path $Ordner)) { Write-Output "Ordner nicht gefunden: $Ordner"; exit 1 }

# Verfuegbare Listen einsammeln
$gefunden = @()
foreach ($f in (Get-ChildItem $Ordner -Filter *.txt | Sort-Object Name)) {
    if ($f.Name.StartsWith('_')) { continue }
    if ($f.BaseName -notmatch '^(.+)_(Glitchless|Glitched)$') {
        Write-Output "  uebersprungen (Name passt nicht auf Autor_Kategorie): $($f.Name)"
        continue
    }
    $gefunden += [PSCustomObject]@{ autor = $Matches[1]; kategorie = $Matches[2]; datei = $f.FullName }
}

if ($Liste -or ($Autor -eq "" -and $Kategorie -eq "")) {
    Write-Output "Verfuegbare Secret-Listen in $Ordner :"
    foreach ($k in @('Glitchless','Glitched')) {
        $inKat = @($gefunden | Where-Object { $_.kategorie -eq $k })
        Write-Output "  Dropdown ${k}: $(if($inKat.Count){ ($inKat.autor) -join ', ' } else { '(keine)' })"
    }
    Write-Output "  Ohne Liste immer verfuegbar: Vanilla, Pool"
    if (-not $Liste) { exit 0 }
    exit 0
}

$treffer = @($gefunden | Where-Object { ($Autor -eq "" -or $_.autor -eq $Autor) -and ($Kategorie -eq "" -or $_.kategorie -eq $Kategorie) })
if ($treffer.Count -eq 0) { Write-Output "Keine Liste fuer Autor '$Autor' / Kategorie '$Kategorie'"; exit 1 }

$alles = @()
foreach ($t in $treffer) {
    $p = Lies-Liste $t.datei
    foreach ($w in $p.warnungen) { Write-Output "  WARNUNG $($t.autor)_$($t.kategorie): $w" }
    Write-Output "=== $($t.autor) / $($t.kategorie) ==="
    foreach ($l in $p.level) {
        if ($LevelName -ne "" -and $l.level -ne $LevelName) { continue }
        $ziel = if ($l.stellen.Count -gt 0) {
            (0..($l.ids.Count-1) | ForEach-Object { "$($l.ids[$_])->R$($l.stellen[$_].room)($($l.stellen[$_].sectorX),$($l.stellen[$_].sectorZ))" }) -join '  '
        } else { 'bleibt vanilla' }
        Write-Output ("  {0,-10} {1}" -f $l.level, $ziel)
        foreach ($e in $l.extras) {
            $a  = if ($null -ne $e.angle) { " Winkel $($e.angle)" } else { "" }
            $ob = if ($null -ne $e.ocb)   { " OCB $($e.ocb)" }      else { "" }
            $hh = if ($null -ne $e.hoehe) { " $($e.hoehe) ueber Boden" } else { "" }
            $zl = ($e.triggerZiele -join ', ')
            $tr = if ($null -ne $e.triggerArt) { "  + Trigger $($e.triggerArt) -> Item $zl" } else { "" }
            switch ($e.art) {
                'item'    {
                    $wo = if ($null -ne $e.wieItem) { "wie Item $($e.wieItem)" }
                          else { "Raum $($e.room)($($e.sectorX),$($e.sectorZ))" }
                    Write-Output ("      dazu: Typ {0} {1}{2}{3}{4}{5}" -f $e.typeId,$wo,$ob,$a,$hh,$tr)
                }
                'trigger' { Write-Output ("      dazu: Trigger {0} in Raum {1}({2},{3}) -> Item {4}" -f $e.triggerArt,$e.room,$e.sectorX,$e.sectorZ,$zl) }
                'ersetz'  { Write-Output ("      dazu: Item {0} wird Typ {1}{2}{3}{4}" -f $e.index,$e.typeId,$ob,$a,$hh) }
            }
        }
    }
    $alles += [PSCustomObject]@{ autor=$t.autor; kategorie=$t.kategorie; level=$p.level }
}

if ($AlsJson -ne "") {
    $alles | ConvertTo-Json -Depth 8 -Compress | Out-File $AlsJson -Encoding utf8
    Write-Output "JSON geschrieben: $AlsJson"
}

#----------------------------------------------------------------------------
# Patch-Ausgabe (#53): aus dem Plan eines Levels ein Patch-JSON fuer BauWerk.
#
# Warum als Patch NACH dem Wurf und nicht im Randomizer: die Secret-Stellen
# sind handkuriert und damit seedunabhaengig. Ein Patch beschreibt genau das -
# er setzt die Secrets an ihre Plaetze, egal was der Wurf vorher mit ihnen
# gemacht hat, und haengt die Extra_Options an.
#
# Das y wird NICHT gerechnet, sondern mit "yAuto" der Datei ueberlassen: dort
# steckt die portierte Bodenmathematik der Engine, auch fuer Schraegen.
#
# Der Trigger einer Extra-Option braucht den Itemindex des Objekts, an das er
# gebunden ist (Schluesselloch). Das ist ein NEU angehaengtes Item, sein Index
# steht also erst nach dem Anhaengen fest: Itemzahl der Zieldatei plus Position
# in der Liste der Hinzufuegungen.
if ($AlsPatch -ne "") {
    if ($LevelName -eq "" -or $Trc -eq "") { Write-Output "AlsPatch braucht -LevelName und -Trc"; exit 1 }
    if (-not (Test-Path $Trc)) { Write-Output "Zieldatei nicht gefunden: $Trc"; exit 1 }

    $planLevel = $null
    foreach ($a in $alles) { foreach ($l in $a.level) { if ($l.level -eq $LevelName) { $planLevel = $l } } }
    if ($null -eq $planLevel) { Write-Output "Kein Planeintrag fuer $LevelName"; exit 1 }

    # Raumursprünge aus dem Level-Info-JSON
    $info = Get-Content $LevelInfo -Raw | ConvertFrom-Json
    $ro = @{}; foreach ($r in $info.rooms) { $ro[[int]$r.index] = $r }

    # Itemzahl der Zieldatei - Grundlage fuer die Indizes der neuen Items
    $kopf = & $ItemPruef $Trc 2>$null | Select-Object -First 1
    if ($kopf -notmatch ':\s*(\d+)\s+Items') { Write-Output "Itemzahl nicht lesbar aus $Trc"; exit 1 }
    $itemBasis = [int]$Matches[1]

    $edits = @(); $adds = @(); $trigger = @()

    for ($i = 0; $i -lt $planLevel.stellen.Count; $i++) {
        $s = $planLevel.stellen[$i]
        $rm = $ro[[int]$s.room]
        $edits += [PSCustomObject]@{
            index = [int]$planLevel.ids[$i]; room = [int]$s.room
            x = ([int]$rm.originX + [int]$s.sectorX * 1024 + 512)
            z = ([int]$rm.originZ + [int]$s.sectorZ * 1024 + 512)
            y = 0; yAuto = $true
        }
    }

    $n = 0

    # "Neu<k>" in einem Triggerziel meint das k-te Item, das die Extra_Options
    # dieses Levels selbst anlegen. Seinen Index kennt erst dieser Lauf: die
    # neuen Items haengen ab $itemBasis hinten an, in der Reihenfolge, in der
    # sie in der Zeile stehen. Gebraucht fuer eine Doppeltuer - unten wird ein
    # vorhandenes Item umgetypt, oben kommt ein neues dazu, und EIN Schalter
    # muss beide ansteuern.
    $addZahl = @($planLevel.extras | Where-Object { $_.art -eq 'item' }).Count
    function Ziel-Aufloesen($z) {
        if ($z -is [int]) { return $z }
        $s = [string]$z
        if ($s -match '^Neu(\d+)$') {
            $k = [int]$Matches[1]
            if ($k -lt 1 -or $k -gt $addZahl) {
                Write-Output "  WARNUNG: Neu$k gibt es nicht - die Zeile legt $addZahl Items an."
                return $null
            }
            return $itemBasis + $k - 1
        }
        Write-Output "  WARNUNG: Triggerziel '$s' nicht verstanden - uebersprungen."
        return $null
    }
    function Ziel-Befehle($ziele) {
        $c = @()
        foreach ($z in $ziele) {
            $i = Ziel-Aufloesen $z
            if ($null -ne $i) { $c += [PSCustomObject]@{ type = 0; param = [int]$i } }
        }
        return @($c)
    }
    foreach ($e in $planLevel.extras) {

        # Ein vorhandenes Item umtypen: kein neues Item, also auch kein neuer
        # Index. Position bleibt, wo sie ist - nur Typ und wahlweise Winkel
        # und OCB aendern sich. yAuto waere hier falsch: das Item soll nicht
        # auf den Sektorboden springen.
        if ($e.art -eq 'ersetz') {
            $ed = [ordered]@{ index = [int]$e.index; typeId = [int]$e.typeId }
            if ($null -ne $e.angle) { $ed['angle'] = Grad-InRoh ([int]$e.angle) }
            if ($null -ne $e.ocb)   { $ed['ocb']   = [int]$e.ocb }
            if ($null -ne $e.hoehe) { $ed['yOff']  = -[int]$e.hoehe; $ed['yAuto'] = $true }
            if ($null -ne $e.xOff)  { $ed['xOff']  =  [int]$e.xOff }
            if ($null -ne $e.zOff)  { $ed['zOff']  =  [int]$e.zOff }
            $edits += [PSCustomObject]$ed
            continue
        }

        # Triggerarten der Engine: 0 = TRIGGER, 2 = SWITCH, 3 = KEY,
        # 4 = PICKUP. Nur 2/3/4 tragen ein Referenzwort.
        $art = 0
        if ($null -ne $e.triggerArt) {
            $art = switch ($e.triggerArt.ToLower()) {
                'key'     { 3 }
                'switch'  { 2 }
                'pickup'  { 4 }
                'trigger' { 0 }
                default   { -1 }
            }
            if ($art -lt 0) {
                Write-Output "  WARNUNG: unbekannte Triggerart '$($e.triggerArt)' - als einfacher Trigger behandelt."
                $art = 0
            }
        }

        # Reiner Trigger ohne Item: nichts anhaengen, kein Index verbraucht,
        # kein Referenzwort.
        if ($e.art -eq 'trigger') {
            $trigger += [PSCustomObject]@{
                room = [int]$e.room; sectorX = [int]$e.sectorX; sectorZ = [int]$e.sectorZ
                art = $art; timer = 0; oneShot = 1; mask = 31
                commands = (Ziel-Befehle $e.triggerZiele)
            }
            continue
        }

        # Item anhaengen. Bei @LikeItem steht die Stelle noch nicht fest - dann
        # kommt sie gleich vom Bezugsitem, und Raum/Sektor bleiben hier leer.
        $eintrag = [ordered]@{ typeId = [int]$e.typeId }
        if ($null -ne $e.room) {
            $rm = $ro[[int]$e.room]
            $eintrag['room']  = [int]$e.room
            $eintrag['x']     = ([int]$rm.originX + [int]$e.sectorX * 1024 + 512)
            $eintrag['z']     = ([int]$rm.originZ + [int]$e.sectorZ * 1024 + 512)
            $eintrag['y']     = 0
            $eintrag['yAuto'] = $true
        }

        # @LikeItem: Position eines vorhandenen Items uebernehmen statt sie aus
        # Raum und Sektor zu rechnen. Dann KEIN yAuto - das Item soll genau
        # dort sitzen, wo das Bezugsitem sitzt, nicht auf der gerechneten
        # Bodenlage (die auf geneigten Sektoren bis zu 128 Einheiten daneben
        # liegt).
        if ($null -ne $e.wieItem) {
            $bez = $info.items | Where-Object { $_.index -eq [int]$e.wieItem }
            if ($null -eq $bez) {
                Write-Output "  WARNUNG: LikeItem($($e.wieItem)) - dieses Item gibt es im Level-JSON nicht."
            } else {
                $eintrag['x'] = [int]$bez.x
                $eintrag['z'] = [int]$bez.z
                $eintrag['y'] = [int]$bez.y
                $eintrag['room'] = [int]$bez.room
                $eintrag['yAuto'] = $false
            }
        }
        if ($null -ne $e.angle) { $eintrag['angle'] = Grad-InRoh ([int]$e.angle) }
        if ($null -ne $e.ocb)   { $eintrag['ocb']   = [int]$e.ocb }
        if ($null -ne $e.hoehe) { $eintrag['yOff']  = -[int]$e.hoehe }
        if ($null -ne $e.xOff)  { $eintrag['xOff']  =  [int]$e.xOff }
        if ($null -ne $e.zOff)  { $eintrag['zOff']  =  [int]$e.zOff }
        $adds += [PSCustomObject]$eintrag
        $meinIndex = $itemBasis + $n
        $n++

        if ($null -ne $e.triggerArt) {
            $trigger += [PSCustomObject]@{
                room = [int]$e.room; sectorX = [int]$e.sectorX; sectorZ = [int]$e.sectorZ
                art = $art; timer = 0; oneShot = 1; mask = 31
                refItem = $meinIndex
                commands = (Ziel-Befehle $e.triggerZiele)
            }
        }
    }

    $patch = [PSCustomObject]@{ itemEdits = $edits; itemAdds = $adds; triggerAdds = $trigger }
    $json = $patch | ConvertTo-Json -Depth 8 -Compress
    [System.IO.File]::WriteAllText($AlsPatch, $json, (New-Object System.Text.UTF8Encoding($false)))
    $umTyp = @($planLevel.extras | Where-Object { $_.art -eq 'ersetz' }).Count
    Write-Output "Patch geschrieben: $AlsPatch  ($($planLevel.stellen.Count) Secrets verschoben, $umTyp umgetypt, $($adds.Count) Zusatzitems, $($trigger.Count) Trigger; neue Items ab Index $itemBasis)"
}
