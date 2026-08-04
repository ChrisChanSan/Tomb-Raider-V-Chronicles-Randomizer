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
    [string]$LevelName = "Level",
    [string[]]$DebugSectors = @(),
    [string]$DebugPathTo = "",
    [string]$ExportZonesJson = "",
    # Maximaler Aufstieg, den Lara ohne Hilfsmittel schafft. Engine-Wert, nicht
    # Schaetzung: TestLaraVault (game/lara.cpp:1107) behandelt hdif bis -1920
    # (Vault2/Vault3/Hochsprung) und faellt darueber in
    # "if (!lara.climb_status) return 0;" - hoeher geht nur an Kletterwaenden.
    # Der Nutzer hatte es im Spiel exakt richtig eingeschaetzt ("alles ueber
    # 2x1024 geht nicht aus dem Stand" - die Andrea1-Mauer mit 2048 ist real
    # unueberwindbar, 1920 ist die praezise Codegrenze darunter).
    [int]$CLIMB_LIMIT = 1920,
    # PHYSIKMODELL, GEPARKT (2026-08-04): richtungsabhaengige Kletterregel +
    # Sprungkanten. Drei Messrunden ergaben: die Regel schliesst echte Lecks
    # (Andrea1-Mauer), zerlegt aber Stockwerk-Level, weil die Sektordaten-
    # Semantik dafuer nicht ausreicht. Kernbefund aus GetFloor (game/
    # control.cpp:1206): der Abstieg durch pit_room passiert NUR wenn
    # y >= Bodenebene - der Floor-Byte ist also echter, solider Boden, AUCH
    # wenn roomBelow gesetzt ist (gestapelte Stockwerke, Andrea2 Raum 211
    # ueber 54: beide Ebenen begehbar). Eine "Standhoehen-Aufloesung nach
    # unten" ist damit grundfalsch; ein korrektes Modell braucht die volle
    # GetHeight-Semantik inkl. NoCol-Dreiecken. Bis dahin: Schalter aus,
    # Leck-Schliessung stattdessen per GATED_AREA-Kuration.
    [switch]$EnableClimbModel
)

$ErrorActionPreference = "Stop"
$d = Get-Content $LevelJson -Raw | ConvertFrom-Json

# --- Typ-IDs (aus RESOURCES/TRCatalog2.xml, TR5-Abschnitt) -------------------
$DOOR_TYPES = @(284,286,288,290,292,294,296,298,   # Door Type 1-8
                300,302,304,306,308,310,           # Closed Door 1-6
                312,314,316,318,320,322,324,326,328,332,
                98)                                # Cables - siehe unten
# Kabel (typeId 98) sind keine Tuer, sperren aber genauso: sie stehen von
# Levelbeginn an unter Strom und sind unpassierbar, bis ein Wandschalter das
# ganze Level stromlos macht. joby5: der Jump Switch (Item 190, Raum 165)
# schaltet mit einem Trigger alle vier Kabel (91/136/165/206) UND FlipMap 6.
# Raum 143 ist ein Korridor, dessen Sektor (1,3) genau so ein Kabel traegt -
# dahinter liegt Raum 61, und erst nach einem Besuch dort spawnt der Soldat mit
# Key 6. Die Kabel tragen flags=0, werden von der CODEBITS-Regel also korrekt
# nicht als "ab Start offen" behandelt. Sie in DOOR_TYPES aufzunehmen macht das
# Modell strenger - die sichere Richtung, weil ein Key-Item hinter einem
# stromfuehrenden Kabel logisch spaeter liegt, als ein permissives Modell glaubt.
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

# Wasserraeume (waterRoom aus dem Export, ROOM_UNDERWATER-Bit 0x1 des Room-
# Headers). Beim Schwimmen ist Hoehe irrelevant - jede Kletterregel muss
# Wasserraeume vollstaendig ausnehmen.
$roomWater = @{}
foreach($r in $d.rooms){ $roomWater[[int]$r.index] = [bool]$r.waterRoom }

# Effektive Standhoehe eines Sektors. floorY eines Portalsektors ist NICHT die
# Hoehe, auf der Lara dort steht - das war die Kernursache, warum der erste
# Hoehenregel-Versuch die Level zerlegte (joby3 -82% usw.). Die Engine macht es
# in GetHeight()/GetFloor() so: bei einem vertikalen Portal (pit_room) wird in
# den Raum darunter weitergegangen, bis echter Boden gefunden ist; bei einem
# horizontalen Portal steht Lara letztlich auf dem Boden des Zielraums. Genau
# das bildet diese Aufloesung nach: roomBelow zuerst (vertikal faellt man
# durch), sonst Portalziel, sonst der eigene floorY. Nicht aufloesbar (Zyklen,
# Wand-Sentinel) => $null, und $null behandelt die Kletterpruefung bewusst als
# "erlaubt" - lieber eine Kante zu viel als ein Level faelschlich zerschnitten.
$effFloorCache = @{}
function Get-EffFloorY([string]$k, [int]$depth = 0) {
    if ($effFloorCache.ContainsKey($k)) { return $effFloorCache[$k] }
    $v = $null
    if ($sec.ContainsKey($k) -and $depth -lt 6) {
        $s = $sec[$k]
        $rm = $rooms[[int]$s.room]
        $wx = [int]$rm.originX + [int]$s.sectorX*1024
        $wz = [int]$rm.originZ + [int]$s.sectorZ*1024
        if ($s.roomBelow -ne 255) {
            $t = Get-SectorInRoom ([int]$s.roomBelow) $wx $wz
            if ($t) { $v = Get-EffFloorY "$($t.room),$($t.sectorX),$($t.sectorZ)" ($depth+1) }
        }
        elseif ($s.isPortal -and $null -ne $s.portalTarget) {
            $t = Get-SectorInRoom ([int]$s.portalTarget) $wx $wz
            if ($t) { $v = Get-EffFloorY "$($t.room),$($t.sectorX),$($t.sectorZ)" ($depth+1) }
        }
        else {
            $f = [int]$s.floorY
            if ($f -gt -30000) { $v = $f }   # Wand-Sentinel (-127 Klicks) => unbekannt
        }
    }
    $effFloorCache[$k] = $v
    return $v
}

# Darf Lara die Kante von $ka nach $kb nehmen? Runter immer (Fallen ist
# unbegrenzt - der alte Versuch hatte auch Abstiege gedeckelt, DAS war neben
# der Portalhoehe der zweite Fehler, joby3s tiefe Schaechte fielen deshalb
# komplett aus). Hoch nur bis CLIMB_LIMIT (1920, TestLaraVault) - es sei denn,
# einer der Sektoren ist als Kletterwand markiert, einer der Raeume ist Wasser
# (Schwimmen), oder ein Brueckensektor ist beteiligt (deren floorY beschreibt
# den Grund unter der Bruecke, nicht die Bruecke).
function Test-ClimbEdge([string]$ka, [string]$kb) {
    if (-not $EnableClimbModel) { return $true }   # Physikmodell geparkt, s. Parameterkommentar
    if ($bridgeSec.ContainsKey($ka) -or $bridgeSec.ContainsKey($kb)) { return $true }
    $sa = $sec[$ka]; $sb = $sec[$kb]
    if ($roomWater[[int]$sa.room] -or $roomWater[[int]$sb.room]) { return $true }
    $fa = Get-EffFloorY $ka
    $fb = Get-EffFloorY $kb
    if ($null -eq $fa -or $null -eq $fb) { return $true }
    $rise = $fa - $fb    # Y zeigt nach unten: > 0 heisst Ziel liegt HOEHER
    if ($rise -le $CLIMB_LIMIT) { return $true }
    if ($sa.climbable -or $sb.climbable) { return $true }
    # Leitern haengen an WAENDEN: das CLIMB_TYPE-Flag sitzt regelmaessig auf dem
    # Wandsektor selbst, und Wandsektoren sind nie Endpunkte einer Kante. Lara
    # steht auf dem Nachbarsektor und klettert die markierte Wand daneben hoch.
    # Deshalb zaehlt auch ein direkt angrenzender kletterbarer Sektor (egal ob
    # Wand oder Boden) an einem der beiden Endpunkte.
    foreach($p in @($sa,$sb)){
        foreach($off in @(@(1,0),@(-1,0),@(0,1),@(0,-1))){
            $ak = "$($p.room),$([int]$p.sectorX + $off[0]),$([int]$p.sectorZ + $off[1])"
            if ($sec.ContainsKey($ak) -and $sec[$ak].climbable) { return $true }
        }
    }
    return $false
}

# --- Sektorgraph aufbauen ---------------------------------------------------
# Manuell kuratierte Einbahn-Uebergaenge: Raum, den Lara betreten, aber nicht
# wieder in Richtung anderer Raeume verlassen kann. Aus der Geometrie allein
# nicht ableitbar (das Portal existiert in beide Richtungen), deshalb Handarbeit
# - dieselbe Begruendung wie bei boIsManuallyExcludedRoom im Exporter.
$ONEWAY_OUT = @{
    'Andrea1' = @(194)   # Rattenkaefig: rein ja, weiter nach 170 nein
}

# Raeume, die Lara ohne Glitch gar nicht betritt - typischerweise Daecher und
# Simse, die geometrisch begehbar aussehen, aber zu hoch liegen. Das waere
# eigentlich Sache des Hoehenmodells (-EnableClimbModel): es erkennt diese
# Faelle nachweislich korrekt (Andrea2 Raum 177/226 fallen damit weg), stuft
# aber gleichzeitig 86 Raeume als unerreichbar ein, darunter 63 und 69 mit den
# Puzzle-Slots - als Regel UND als Verdachtsgenerator unbrauchbar. Deshalb
# Handkuration: eine Zahl pro Raum, in trview direkt ablesbar.
$EXCLUDE_ROOMS = @{
    # Vom Nutzer in trview durchgesehen. Daecher (177/226 und Nachbarn), reine
    # Luftraeume ueber den Gassen und der Bereich hinter den Dekotueren 89/90.
    'Andrea2' = @(148,215,83,177,205,220,189,153,226,232,242,47,224,162,237,
                  181,156,124,127,185,112,122,119,118,128,114,115,141,87)
    # Einbahnraeume, die nur die Skybox zeigen und dem Level Tiefe geben.
    'Andrea3' = @(113,114,115,116,64,66,67,69)
}
$excludeRooms = @( if($EXCLUDE_ROOMS.ContainsKey($LevelName)){ $EXCLUDE_ROOMS[$LevelName] } )

# Zusaetzliche Sperren pro Item-Typ statt pro Level: der Raum bleibt begehbar
# und Teil des Wegenetzes, nur DIESES Item darf dort nicht liegen. Typischer
# Fall sind Raeume, die zwar frueh erreichbar sind, aber hinter dem Schloss
# liegen, das der Schluessel oeffnet - die Zone allein sieht das nicht, weil
# sie nur "ab wann erreichbar" kennt, nicht "wofuer noch gebraucht".
$TYPE_EXCLUDE_ROOMS = @{
    'Andrea2' = @{
        174 = @(17,134,214,46,9,10,207,219)   # Puzzle Item3
        175 = @(70,204)                       # Puzzle Item4
        176 = @(70,204)                       # Puzzle Item5 (141/87 sind
                                              # inzwischen ganz gesperrt)
    }
}
$typeExclude = @{}
if ($TYPE_EXCLUDE_ROOMS.ContainsKey($LevelName)) {
    foreach($kv in $TYPE_EXCLUDE_ROOMS[$LevelName].GetEnumerator()){ $typeExclude[[int]$kv.Key] = @($kv.Value) }
}

# Raeume, die begehbar bleiben und Teil des Wegenetzes sind, in denen aber gar
# kein Key-/Puzzle-Item liegen darf - unabhaengig vom Typ. Anders als
# EXCLUDE_ROOMS ist das bewusst KEINE Begehbarkeitssperre: Andrea3s Startinsel
# enthaelt Laras Startraum 1, den nicht begehbar zu machen wuerde die Flut
# sofort abwuergen. Der Grund ist ein reiner Platzierungsgrund - hinter Raum 3
# liegt ein Point of no return (Lavapit), ein dort uebersehener Schluessel
# waere unwiederbringlich.
$NO_PLACEMENT_ROOMS = @{
    'Andrea3' = @(1,2,3,4,5,6,11,19,20,21,22,29,30,34,112,123,124)
}
$noPlacement = @( if($NO_PLACEMENT_ROOMS.ContainsKey($LevelName)){ $NO_PLACEMENT_ROOMS[$LevelName] } )

# Positivliste statt Sperrliste: manche Typen duerfen nur in einem klar
# umrissenen Abschnitt liegen. Andrea3 ist der Fall - beide Keys muessen im
# Korridor ab Raum 26 ueber 27 Richtung Ende liegen, Key Item1 bis
# einschliesslich seines Schlosses in Raum 15, Key Item2 bis Raum 102. Die
# Zonen allein reichen dafuer nicht: der Teleporter ist wegen des Crumble
# Floor eine Einbahnstrasse, davor liegende Raeume koennen also dieselbe Zone
# haben und trotzdem die falsche Seite sein. Raumlisten aus dem Portalgraphen
# abgeleitet (Flut ab 27, Stop an 26 rueckwaerts und am Schloss vorwaerts).
$TYPE_ALLOW_ROOMS = @{
    'Andrea2' = @{
        # Vom Nutzer als "save" bestaetigte Raeume fuer die Brechstange.
        # 240, NICHT 172: typeId 172 heisst zwar ueberall "Puzzle Item1", ist in
        # Andrea2 aber das Ventil (Item 128, Raum 196), das mit der Brechstange
        # aufgebrochen wird. Die echte Brechstange ist typeId 240 "Crowbar Item"
        # (Item 2, Raum 1) - im ganzen Spiel nur hier. In joby3 und rich1 ist die
        # Brechstange dagegen tatsaechlich als Puzzle-/Key-Item getarnt, daher
        # die Verwechslungsgefahr.
        240 = @(0,1,10,17,25,46,50,54,207)
    }
    'Andrea3' = @{
        196 = @(15,16,26,27,107,108)                         # Key Item1, Ende 15
        197 = @(10,15,16,17,18,26,27,28,55,56,70,83,85,87,   # Key Item2, Ende 102
                88,89,90,100,101,102,103,104,105,106,107,
                108,110,111,119,120,121,129)
        # Puzzle Item4 Combo1: ab Raum 0 Richtung 63. Richtung 58 ist gesperrt -
        # dort liegt eine Schraege, also eine Einbahnstrasse. Dead Stop laut
        # Nutzer ist Raum 35.
        186 = @(0,9,31,32,33,35,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
                52,53,54,59,60,61,62,63,65,68,75,76,78,79,81,82,84,
                86,93,94,95,96,97,98,99,109,117,118,125,128)
    }
}
$typeAllow = @{}
if ($TYPE_ALLOW_ROOMS.ContainsKey($LevelName)) {
    foreach($kv in $TYPE_ALLOW_ROOMS[$LevelName].GetEnumerator()){ $typeAllow[[int]$kv.Key] = @($kv.Value) }
}

# Typen, die in den Pool gehoeren, deren Zonengrenze das Modell aber nicht
# selbst herleiten kann, weil ihnen ein eigenes Schloss im Level fehlt.
# Kombinationsteile sind der Fall: Andrea3s Puzzle Item4 Combo1 (186) und
# Combo2 (187) werden im Inventar zum Puzzle Item4 vereint, erst das kommt in
# ein Puzzle Hole4 - und nur das Loch hat eine Zone. Massgeblich ist Item 34 in
# Raum 24: dort loest das vollstaendige Combo den Teleport aus. Das zweite
# Puzzle Hole4 (Item 20, Raum 8) ist laut Nutzer reine Deko, die den Teleport
# kaschiert, und keine Schranke. Beide liegen ohnehin in Zone 3.
# Combo2 (187) steht bewusst NICHT hier - siehe FIXED_ITEMS.
# Wert hier: typeId -> maxZone.
$EXTRA_POOL_TYPES = @{
    'Andrea3' = @{ 186 = 3 }
    # Andrea2s Brechstange (typeId 240, Item 2, Raum 1, Zone 0) darf
    # mitrandomisiert werden - das widerspricht der Regel "Brechstange bleibt
    # Vanilla" nicht: die gilt fuer joby3 und rich1, wo die Brechstange als
    # Puzzle-/Key-Item getarnt ist und an einer Aufnahme-Cutscene haengt. Hier
    # ist es ein normaler Pickup. Zonengrenze 1 = die Zone des Ventils
    # (Item 128, Raum 196), das ohne Brechstange nicht aufzubrechen ist; die
    # eigentliche Schranke ist die Raumliste, die der Nutzer als "save"
    # bestaetigt hat.
    'Andrea2' = @{ 240 = 1 }
}

# Items, die ein Level zwingend BRAUCHT, die in Vanilla aber fehlen. Andrea3
# enthaelt keine einzige Brechstange (typeId 172, im Level nachgezaehlt: 0
# Instanzen) - ein latenter Softlock schon in Vanilla. Der Randomizer muss hier
# also nicht nur umverteilen, sondern hinzufuegen. Raum 120 laut Nutzer, dort
# liegt bereits das Secret (Item 102, Pickup Item4).
$REQUIRED_INJECTIONS = @{
    'Andrea3' = @( @{ typeId = 172; room = 120; reason = 'Level enthaelt keine Brechstange - Vanilla-Softlock' } )
}
$requiredInjections = @( if($REQUIRED_INJECTIONS.ContainsKey($LevelName)){ $REQUIRED_INJECTIONS[$LevelName] } )

# Items, die an ihrer Vanilla-Position bleiben MUESSEN. Nicht verschieben,
# nicht ersetzen, nicht in den Pool aufnehmen. Grund ist jeweils eine
# Nebenwirkung, die an der Position und nicht am Item haengt und die aus den
# Leveldaten nicht ablesbar ist - hier: Andrea3 Item 49 (Puzzle Item4 Combo2)
# triggert eine Tuer. Wird ins Zonen-JSON exportiert, damit ein spaeterer
# Randomizer die Sperre uebernehmen kann, statt sie neu entdecken zu muessen.
$FIXED_ITEMS = @{
    'Andrea3' = @{ 49 = 'Puzzle Item4 Combo2 - triggert eine Tuer' }
    'Andrea2' = @{ 128 = 'Ventil - muss an einer Wand sitzen (siehe VANILLA_TYPES 172)' }
}

# Typen, die komplett an ihren Vanilla-Positionen bleiben. Ihre Zwangsbedingung
# wird weiter abgeleitet und im Report angezeigt - man soll sehen, dass es sie
# gibt -, sie wandern aber nicht in den Pool und bekommen keine Marker.
# Andrea2s Ventil (172) ist der Fall: es muss innerhalb von 512 Units an einer
# Wand sitzen UND sein Angle muss zu dieser Wand passen. Beides sind
# Bedingungen unterhalb der Sektoraufloesung - das Modell arbeitet mit ganzen
# 1024er-Sektoren und kennt keine Wandausrichtung, koennte eine gueltige
# Position also gar nicht garantieren. Nutzerentscheidung: "Die Brechstange als
# zufall reicht."
$VANILLA_TYPES = @{
    'Andrea2' = @(172)
}
$vanillaTypes = @( if($VANILLA_TYPES.ContainsKey($LevelName)){ $VANILLA_TYPES[$LevelName] } )
$fixedItems = @()
if ($FIXED_ITEMS.ContainsKey($LevelName)) {
    foreach($kv in $FIXED_ITEMS[$LevelName].GetEnumerator()){
        $fixedItems += [PSCustomObject]@{ item=[int]$kv.Key; reason=[string]$kv.Value }
    }
}
$extraPool = @{}
if ($EXTRA_POOL_TYPES.ContainsKey($LevelName)) {
    foreach($kv in $EXTRA_POOL_TYPES[$LevelName].GetEnumerator()){ $extraPool[[int]$kv.Key] = [int]$kv.Value }
}
$onewayOut = @( if($ONEWAY_OUT.ContainsKey($LevelName)){ $ONEWAY_OUT[$LevelName] } )

# Umzaeunte Bereiche: ein zusammenhaengender Gebaeude-/Hofkomplex, der im Spiel
# AUSSCHLIESSLICH durch bestimmte Tueren betreten werden kann, den der Graph
# aber ueber Portal-Randkanten (Zaeune, Tuerstuerze, Hochparterre-Portale)
# faelschlich frueher erreicht. Der Innenbereich wird zur Laufzeit als
# Zusammenhangskomponente ab Seed berechnet (ohne Barrier-Raeume zu betreten,
# ohne Torsektoren zu durchqueren); danach wird jede Kante von aussen nach
# innen gekappt, die nicht ueber die Torsektoren laeuft. Vom Nutzer in trview
# verifizierte Vorgabe fuer Andrea1: "ab dem Tor 53/54 darf alles was dahinter
# liegt keine KeyItem-Positionen mehr enthalten" - der rote Kasten auf seiner
# Karte ist genau dieser Komplex, das Doppeltor 53/54 (Raum 112) der einzige
# legale Zugang. Ein volles Hoehen-/Physikmodell wuerde das automatisch
# erkennen, ist aber geparkt (siehe -EnableClimbModel).
# Rooms = explizite Liste der Raeume hinter dem Tor. Drei automatische Ansaetze
# wurden gemessen und verworfen, die Begruendung gehoert hierher, damit sie
# niemand blind wiederholt:
#  1. Insel-Flut ab dem Hof: scheitert prinzipiell, weil der Bereich nur
#     EINSEITIG versiegelt ist - rein nur durchs Tor, raus per Absprung an
#     vielen Stellen (Raum 117 grenzt allein an 112,113,116,118,119,144,150,156).
#     Eine Flut folgt den Abspruengen nach draussen und frisst das Level.
#  2. Barriere-Raum (Strasse 112) als Stopp: die Flut fand sofort andere
#     Auswege (117->150->130->113->120->114->...).
#  3. Hoehenlage als Trennkriterium: widerlegt - Raum 150 (aussen) liegt bei
#     yBottom -6144 wie 145/165/166/181 (innen), Raum 113 (aussen) bei -2048
#     wie 118/119 (innen).
# Sauber loesen wuerde das nur ein vollstaendiges Hoehen-/Kletterodell
# (-EnableClimbModel), dem die verlaessliche Bodensemantik an Portalen fehlt.
# Die Liste ist dafuer transparent, in trview direkt ablesbar und je Raum mit
# einer Zahl korrigierbar.
# Die Loesung kam vom Nutzer durch UMKEHRUNG der Fragestellung: statt die
# Innenraeume zu erraten (scheiterte, s.o.), nennt er die Raeume, die VOR dem
# Tor noch begehbar sind. Sie bilden eine geschlossene Stoppkante, gegen die
# eine Flut ab dem Hof laeuft - damit ergibt sich der Innenbereich automatisch
# und vollstaendig (Andrea1: 65 Raeume, 1499 Sektoren, kein Fluchtweg mehr).
# PreGateRooms bleiben ausdruecklich valide fuer Platzierungen; Raum 217 steht
# mit drin, weil er laut Nutzer generell auszuschliessen ist und als Stopp
# ohnehin dieselbe Wirkung hat. Ueber diesen Raeumen liegende reine "Luft"-
# Raeume sind laut Nutzer irrelevant. Notfall-Hard-Stop waeren 114/120.
$GATED_AREA = @{
    'Andrea1' = @( @{
        Doors        = @(53,54)
        SeedRooms    = @(117)
        PreGateRooms = @(112,132,127,124,158,114,120,163,159,164,139,170,217)
    } )
}
$gatedAreas = @( if($GATED_AREA.ContainsKey($LevelName)){ $GATED_AREA[$LevelName] } )

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
#
# joby3, User-Entscheidung (nicht aus Daten bewiesen, sondern bewusst als
# Randomizer-Regel festgelegt): der Chef in Raum 120 ist per Stealth-Kill zu
# toeten (proximity-basiert, game/deltapak.cpp stealth3_start/end - siehe
# Analyse weiter oben, technisch nicht aus Sektor-/Item-Daten ableitbar). Da
# das Toeten des Chefs zwingend ist, sobald Raum 120 erreicht ist, gelten die
# beiden Tueren dahinter als sicher passierbar, sobald Raum 120 erreicht wird:
#   Tuer 190 (Raum 124) <- Key Item6 (typeId 201), Vanilla-Drop des Chefs
#   Tuer 175 (Raum 106) <- Key Item7 (typeId 202), liegt hinter Tuer 190 in Raum 107
# Platzierungsregel fuer den Randomizer: Key Item6 bleibt an/bei den Chef
# gebunden (Zone von Raum 120), Key Item7 darf bis einschliesslich der Zone
# von Raum 120 liegen - beide Tueren zaehlen ab dort als begehbar.
$SCRIPT_DOORS = @{
    'joby3' = @{ 190 = 120; 175 = 120 }
    # rich1: der Scientist (Item 85, Raum 34, OCB 9) hat eine Anvisier-Cutscene -
    # laenger auf ihn zielen oeffnet Tuer 84 direkt (der leichte Weg). Toetet man
    # ihn stattdessen, braucht es einen Umweg. Gleiche Klasse wie der Chef in
    # joby3: proximity-/verhaltensbasiert, nicht aus Sektor-/Item-Daten ableitbar.
    # User-Entscheidung, analog zu joby3: Raum erreicht = Tuer gilt als offen.
    'rich1' = @{ 84 = 34 }
    # Andrea3: der Roman God (Item 79, typeId 61, Raum 90, Sektor 2,3) steht
    # exakt auf dem HeavyTrigger, der Tuer 103 oeffnet (Object:103, mask 31) -
    # er faellt beim Sterben buchstaeblich auf seinen eigenen Ausloeser. Anders
    # als Chef/Scientist ist das kein Stealth-/Anvisier-Sonderfall, sondern ein
    # normaler bekaempfbarer Boss (kein bekannter Unsterblichkeits-Bug); die
    # Regel bildet trotzdem denselben Fall ab, weil ein NPC-Tod nicht als
    # "Lara erreicht Sektor X" modelliert werden kann. Kette danach laut User:
    # Tuer 103 -> Key Item2 (Raum 119) -> Key-Schloss Raum 102 -> Tuer 77
    # (Raum 89) -> Levelende-Bereich (EndLevel-Trigger, Raum 70).
    'Andrea3' = @{ 103 = 90 }
}
$scriptDoors = @{}
if ($SCRIPT_DOORS.ContainsKey($LevelName)) { $scriptDoors = $SCRIPT_DOORS[$LevelName] }

# Tueren, die eine per-Level-Skriptfunktion (game/deltapak.cpp) ueber ein
# Cutscene-Kommando oeffnet, statt ueber ein Object-Kommando. Anders als die
# leere SCRIPT_DOORS-Tabelle oben ist das hier durch den Engine-Code UND die
# Leveldaten belegt, nicht geraten:
# rich1 Item 153 (Door Type1, Raum 89) - das anschiessbare Schloss ist eine
# Falle (loest wahrscheinlich einen Heavy Trigger fuer einen FlipMap-Wechsel
# aus, der gedachte Weg ist der Hammer). Der Bodentrigger in Raum 89 (1,4)
# feuert "Cutscene 20"; cutseq_control_routines[20] in deltapak.cpp ist exakt
# { hamgate_init, hamgate_control, hamgate_end }. hamgate_end() teleportiert
# Lara nach (31232,-3328,48344) - praktisch identisch mit der Position von
# Item 153 (31232,-3328,47616). hamgate_init() setzt fuer das gefundene
# DOOR_TYPE1-Item IFL_CODEBITS, exakt derselbe Mechanismus wie bei den
# "Flags 11111"-Tueren, nur hier per Skript statt beim Levelstart gesetzt.
# Kein Hammer-Item (typeId 239) existiert irgendwo in rich1 - Lara braucht ihn
# hier nicht als Inventarbedingung, der Bodentrigger allein genuegt.
$CUTSCENE_DOORS = @{
    'rich1' = @{ 153 = 20 }   # doorItem -> Cutscene-Index
}
$cutsceneDoors = @{}
if ($CUTSCENE_DOORS.ContainsKey($LevelName)) { $cutsceneDoors = $CUTSCENE_DOORS[$LevelName] }
$cutsceneDoorByCutscene = @{}
foreach($kv in $cutsceneDoors.GetEnumerator()){ $cutsceneDoorByCutscene[[int]$kv.Value] = [int]$kv.Key }

# Uebergaenge, die Lara schafft, der Sektorgraph aber nicht kennt: Spruenge,
# Kletterstellen, Monkeyswing. Format "Raum,x,z" -> "Raum,x,z", beidseitig.
# Das ist die Handkuration fuer die Klasse "Flood stoppt an der Geometrie".
$MANUAL_LINK = @{
}

# Schluessel, die NICHT verbraucht werden. Der Normalfall in TR ist: Key oder
# Puzzle-Item wandert ins Schloss und ist weg - KeyHoleCollision (game/pickup.cpp:
# 594) ruft dafuer remove_inventory_item auf. EINE einzige harte Ausnahme steht
# im Code selbst, nicht in Leveldaten: "if (item->object_number != KEY_HOLE8)
# remove_inventory_item(...)" - Key Hole 8 (typeId 265) und damit Key Item8
# (typeId 203) sind ueber ALLE TR5-Level hinweg unverbrauchbar, das ist keine
# Level-Eigenschaft. joby5 (Item 118, EIN Key fuer ZWEI Keyhole8-Schloesser) und
# rich1 (EIN Key fuer VIER Keyhole8-Schloesser, Raeume 26/69x2/81) sind beide
# einfach Anwendungsfaelle derselben globalen Engine-Regel, kein Zufall.
$REUSABLE_KEYS_GLOBAL = @(203)
$REUSABLE_KEYS = @{
}

# Raeume, die es nur fuer eine Kamerafahrt gibt und die Lara nie betritt.
# Die Automatik erkennt solche Gebiete nur, wenn ausser Animatings nichts darin
# steht (siehe Andy2). Enthaelt die Kulisse auch Tueren oder anderes Inventar,
# faellt sie durch - dann hilft nur Handkuration.
# joby3: die Inselgruppe um Raum 140 wird waehrend der Cutscene aus dem
# Luftschacht (Raum 76) nur GESEHEN, ist aber nicht begehbar (Nutzerangabe im
# Zonenreport). Sie enthaelt drei Tueren und ist deshalb automatisch nicht als
# Kulisse erkennbar.
$CAMERA_ROOMS = @{
    'joby3' = @(4,10,11,94,96,97,114,140,185)
    # Andrea3 Raeume 73/74: je 9 Sektoren, komplett leer (keine Items), isoliert.
    # User: "Ueberbleibsel wie in Andrea2, da war wohl mal was geplant" - Cut
    # Content, nicht zum Betreten gedacht.
    'Andrea3' = @(73,74)
    # rich3 Raeume 64/65/66/67: geschlossener 4-Raum-Kreislauf ohne jede
    # Verbindung zum Rest des Levels. User: liegt in trview sichtbar ausserhalb
    # der eigentlichen Levelgeometrie ("out of map") - das ist die Intro-Flyby
    # mit den ums Level kreisenden Helikoptern, rein kosmetisch, nie begehbar.
    'rich3' = @(64,65,66,67)
}
$cameraRooms = @( if($CAMERA_ROOMS.ContainsKey($LevelName)){ $CAMERA_ROOMS[$LevelName] } )
$reusableKeys = @($REUSABLE_KEYS_GLOBAL) + @( if($REUSABLE_KEYS.ContainsKey($LevelName)){ $REUSABLE_KEYS[$LevelName] } )
$manualLinks = @( if($MANUAL_LINK.ContainsKey($LevelName)){ $MANUAL_LINK[$LevelName] } )

# Floor-Override-Objekte erzeugen begehbaren Boden, wo die Sektordaten Leere
# melden. Vollstaendige Liste per grep "obj->floor = " in game/setup.cpp:
#   BRIDGE_FLAT/TILT1/TILT2  -> BridgeFlatFloor usw.   (448..453 inkl. Mips)
#   FALLING_BLOCK/2, CRUMBLING_FLOOR -> FallingBlockFloor (103, 104, 105)
#   TWOBLOCK_PLATFORM        -> TwoBlockPlatformFloor  (136, "Expanding Platform")
# Ein rein sektorbasierter Graph kann sie prinzipiell nicht sehen. Die
# Crumbling Floors fehlten hier zunaechst - mit fatalem Effekt, sobald die
# Kletterregel kam: Andrea3s Einstieg (12 Crumbling Floors ueber Lava, Raum 8)
# liegt auf Sektoren, deren aufgeloeste "Standhoehe" der Lavagrund weit unten
# ist; der Rueckweg von der Bruecke wirkte wie ein unmoeglicher 4000er-Aufstieg
# und das Level kollabierte auf 8%.
$BRIDGE_TYPES = @(448,449,450,451,452,453, 103,104,105, 136)
$bridgeSec = @{}
foreach($i in $d.items){
    if ($BRIDGE_TYPES -notcontains [int]$i.typeId) { continue }
    $bridgeSec["$($i.room),$($i.sectorX),$($i.sectorZ)"] = $true
}

function Test-Walkable($key) {
    if (-not $sec.ContainsKey($key)) { return $false }
    # Handkuratierte Nicht-Betretbar-Raeume zuerst: sie sollen weder als
    # Platzierungsziel noch als Durchgang dienen (ein Dach ist auch kein Weg).
    if ($excludeRooms -contains [int]$sec[$key].room) { return $false }
    if ($bridgeSec.ContainsKey($key)) { return $true }
    return (-not $sec[$key].isWall)
}

$adj = @{}
function Add-Edge($a, $b) {
    if (-not $adj.ContainsKey($a)) { $adj[$a] = New-Object System.Collections.Generic.HashSet[string] }
    [void]$adj[$a].Add($b)
}

foreach($s in $d.sectors){
    $key = "$($s.room),$($s.sectorX),$($s.sectorZ)"
    if (-not (Test-Walkable $key)) { continue }
    $rm = $rooms[[int]$s.room]
    $wx = $rm.originX + $s.sectorX * 1024
    $wz = $rm.originZ + $s.sectorZ * 1024

    # Nachbarn im selben Raum - gerichtet: die Gegenrichtung entsteht, wenn der
    # Nachbar selbst an der Reihe ist und SEINE Kletterpruefung besteht. So darf
    # Lara eine hohe Kante hinunterfallen, ohne dass daraus ein Rueckweg wird.
    foreach($dz in @(-1,0,1)){ foreach($dx in @(-1,0,1)){
        if (($dx -eq 0 -and $dz -eq 0) -or ($dx -ne 0 -and $dz -ne 0)) { continue }
        $nk = "$($s.room),$($s.sectorX + $dx),$($s.sectorZ + $dz)"
        if (-not (Test-Walkable $nk)) { continue }
        if (-not (Test-ClimbEdge $key $nk)) { continue }
        Add-Edge $key $nk
    }}

    # Sprungkanten: TR-Kernbewegung, die dem reinen Nachbargraphen fehlt.
    # Standsprung ueberwindet 1 Lueckensektor, Anlaufsprung 2. Das alte Modell
    # (ohne Hoehenregel) kaschierte das, indem der Flood DURCH das Loch lief
    # und drueben wieder "hochkam" - physikalisch falsch, aber zufaellig
    # verbindend. Mit korrekten Fallkanten wurde jedes Loch zur Einbahnfalle
    # und Schacht-/Sims-Level kollabierten (joby3 8%, rich2s Fahrstuhlschacht,
    # Andrea3s Colosseum-Simse - vom Nutzer exakt so benannt). Regel: bis zu
    # 2 aufeinanderfolgende Sektoren, deren aufgeloeste Standhoehe deutlich
    # (>512) unter der Absprunghoehe liegt, duerfen uebersprungen werden;
    # die Landung darf hoechstens 1 Klick (256) hoeher liegen, beliebig tief
    # ist erlaubt. Nur geradlinig und innerhalb desselben Raums - Raumgrenzen
    # ueberspannende Spruenge sind eine bekannte, noch offene Luecke.
    $fa = if ($EnableClimbModel) { Get-EffFloorY $key } else { $null }
    if ($null -ne $fa) {
        foreach($dir in @(@(1,0),@(-1,0),@(0,1),@(0,-1))){
            for($step = 1; $step -le 3; $step++){
                $ck = "$($s.room),$($s.sectorX + $dir[0]*$step),$($s.sectorZ + $dir[1]*$step)"
                if (-not $sec.ContainsKey($ck)) { break }
                $cs = $sec[$ck]
                if ($cs.isWall) { break }
                $fc = Get-EffFloorY $ck
                $isGap = ($null -eq $fc) -or (($fc - $fa) -gt 512)
                if ($isGap) { if ($step -eq 3) { break }; continue }
                # erster nicht-Luecken-Sektor = Landekandidat; bei step 1 ist es
                # ein gewoehnlicher Nachbar, den die normale Kante schon abdeckt
                if ($step -ge 2 -and (Test-Walkable $ck) -and ($fa - $fc) -le 256) { Add-Edge $key $ck }
                break
            }
        }
    }

    # Portal / vertikale Verbindungen, beidseitig.
    # Portale stehen in den Leveldaten nur auf der Seite, von der aus sie
    # eingetragen wurden. Nur der Vorwaertskante zu folgen ist deshalb zu streng:
    # grenzen zwei Raeume an denselben Nachbarraum (joby3 Raum 23 und 73 zeigen
    # beide auf 58), fehlt der Rueckweg und ganze Levelteile fallen aus. Beidseitig
    # gemessen: Andrea1 98->100%, Andrea2 95->100%, richcut2 50->99%, joby5 54->62%,
    # rich3 21->26%, ohne dass Andrea1 seine Zonenstruktur oder eine Key-Item-Kette
    # verliert. Echte Einbahn-Uebergaenge bleiben ueber ONEWAY_OUT ausgenommen -
    # dort darf auch die Rueckkante nicht entstehen, sonst waere die Kuration wirkungslos.
    $leavesOneway = $onewayOut -contains [int]$s.room
    $targets = @()
    if ($null -ne $s.portalTarget) { $targets += ,[int]$s.portalTarget }
    foreach($vr in @($s.roomBelow, $s.roomAbove)){ if ($vr -ne 255) { $targets += ,[int]$vr } }
    foreach($tgt in $targets){
        $t = Get-SectorInRoom $tgt $wx $wz
        if (-not $t) { continue }
        $tk2 = "$($t.room),$($t.sectorX),$($t.sectorZ)"
        # Auch ueber Portale/roomBelow/roomAbove darf kein ausgeschlossener
        # Raum betreten werden - sonst waere die Kuration wirkungslos, weil
        # Daecher fast immer per Portal statt per Nachbarsektor haengen.
        # Bewusst NUR die Raumsperre, nicht das volle Test-Walkable: ein Portal
        # darf durchaus auf einem als Wand markierten Sektor landen (Randring
        # des Zielraums), von dem aus es im Raum weitergeht - das zu kappen
        # kostete in Andrea2 die Raeume 149/191/197 und in joby2/andy3 je
        # einige Sektoren, ohne dass dem eine Spielmechanik entspraeche.
        if ($excludeRooms -contains [int]$t.room) { continue }
        # Kletterpruefung je Richtung einzeln (Test-ClimbEdge: runter frei,
        # hoch nur bis 1920 bzw. Kletterwand/Wasser/Bruecke). Der erste Versuch
        # einer Hoehenregel hier scheiterte an zwei Fehlern zugleich: rohes
        # floorY an Portalsektoren verglichen (statt der ueber Portale/roomBelow
        # aufgeloesten Standhoehe, siehe Get-EffFloorY) UND auch Abstiege
        # gedeckelt - gemessen kostete das joby3 -82%, rich2 -40%, joby2 -16%.
        if (-not $leavesOneway -and (Test-ClimbEdge $key $tk2)) { Add-Edge $key $tk2 }
        if ($onewayOut -notcontains $tgt -and (Test-ClimbEdge $tk2 $key)) { Add-Edge $tk2 $key }
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

# Teleporter versetzen Lara an die Position des Teleporter-Items selbst.
# ControlTeleporter (game/tomb4fx.cpp:531ff): bei trigger_flags == 666 laeuft nur
# der Effekt, in ALLEN anderen Faellen greift der else-Zweig und setzt
# lara_item->pos auf item->pos plus ItemNewRoom. Also ist der Teleporter das
# ZIEL und sein Trigger die Quelle - die Kante ist gerichtet, vom Triggersektor
# zum Teleportersektor. (Frueher hatte ich mir das genau falsch herum notiert;
# alle Teleporter der 16 Vanilla-Level haben OCB 0, warpen also tatsaechlich.)
$TELEPORT_TYPES = @(387,388)
foreach($i in $d.items){
    if ($TELEPORT_TYPES -notcontains [int]$i.typeId) { continue }
    if ([int]$i.ocb -eq 666) { continue }
    $dst = "$($i.room),$($i.sectorX),$($i.sectorZ)"
    if (-not $sec.ContainsKey($dst)) { continue }
    foreach($t in $d.triggers){
        $hit = $false
        foreach($c in $t.commands){ if ($c.type -eq 'Object' -and [int]$c.index -eq [int]$i.index) { $hit = $true } }
        if (-not $hit) { continue }
        Add-Edge "$($t.room),$($t.sectorX),$($t.sectorZ)" $dst
    }
}

# --- Tueren: welcher Sektor wird blockiert? ---------------------------------
$doorSector = @{}   # itemIndex -> sectorKey
$openAtStart = @()
$crowbarDoors = @()
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
    # Brechstangen-Tuer: DoorCollision (game/door.cpp:313) prueft
    # item->trigger_flags == 2, also OCB 2, und setzt nach dem Aufhebeln
    # item->flags |= IFL_CODEBITS - dieselbe Mechanik wie oben, nur von Hand
    # ausgeloest. Kein Trigger im Level schaltet so eine Tuer, sie sah deshalb
    # aus wie unschaltbar. Braucht das CROWBAR_ITEM (typeId 240; Enum-Reihenfolge
    # in game/objects.h: HAMMER 239, CROWBAR 240, BURNING_TORCH 241, PUZZLE_HOLE1
    # 242 - Letzteres unabhaengig bestaetigt).
    if ([int]$i.ocb -eq 2) { $crowbarDoors += ,[int]$i.index }
    $doorSector[[int]$i.index] = $k
}
# Welcher TypeId die Brechstange in diesem Level ist, steht NICHT in den
# Leveldaten: Level duerfen sie als Puzzle-Item fuehren und ueber den Gameflow
# umbenennen (CMD_PUZZLE1 objname, String-Wad in ENGLISH.DAT). joby3 macht genau
# das - dort ist die Brechstange Item 41, typeId 172 "Puzzle 1" mit OCB 9, und
# Lara hebt sie mit Aktion plus kleiner Cutscene auf. Deshalb je Level kuratiert.
# Ohne Eintrag gilt der regulaere CROWBAR_ITEM-Slot 240.
$CROWBAR_TYPE = @{
    'joby3' = 172
}
$crowbarType = if($CROWBAR_TYPE.ContainsKey($LevelName)){ [int]$CROWBAR_TYPE[$LevelName] } else { 240 }
$crowbarInLevel = @($d.items | Where-Object { [int]$_.typeId -eq $crowbarType })
if ($crowbarDoors.Count -gt 0 -and $crowbarInLevel.Count -eq 0) {
    # Nirgends im Level platziert -> Lara bringt sie mit bzw. bekommt sie per
    # CMD_GIVEOBJ, die Tueren sind dann ab Start offen.
    foreach($cd in $crowbarDoors){ $openAtStart += ,$cd; $doorSector.Remove($cd) }
}

# Der Hammer folgt demselben Umbenennungs-Muster wie die Brechstange: TR5 kann
# ihn als Key/Puzzle-Item fuehren und per Gameflow (CMD_KEYn/CMD_PUZZLEn
# objname) umbenennen. rich1: der Hammer ist Item 35, typeId 202 "Key Item7",
# Raum 22 - die EINZIGE Instanz dieses Typs im Level, ohne zugehoeriges
# Key Hole7 und ohne Key-Trigger, der es referenziert (verifiziert). Anders als
# die Brechstange in den meisten Leveln liegt der Hammer hier tatsaechlich im
# Level und muss erst erreicht werden - Lara bringt ihn NICHT automatisch mit
# (User-Korrektur: "die bringt Lara beide nicht mit wie in Joby").
$HAMMER_TYPE = @{
    'rich1' = 202
}
$hammerType = if($HAMMER_TYPE.ContainsKey($LevelName)){ [int]$HAMMER_TYPE[$LevelName] } else { $null }
$hammerInLevel = @( if($null -ne $hammerType){ $d.items | Where-Object { [int]$_.typeId -eq $hammerType } } )

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
            # Cutscene-Tueren (rich1 hamgate): der Bodentrigger, der die
            # Skript-Cutscene ausloest, sitzt auf demselben Sektor wie die Tuer
            # selbst (89/(1,4) fuer Item 153) - derselbe Deadlock wie bei
            # Schalter/Schloss auf dem Tuersektor, deshalb hier ebenfalls vom
            # Nachbarsektor aus bedienbar.
            $isCutsceneDoor = $false
            foreach($c in $t.commands){ if ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) { $isCutsceneDoor = $true } }
            return @{ Kind='reach'; Sector=$tk; Operable=$isCutsceneDoor; Desc="Trigger betreten ($tk)" }
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
            # Operable, wie beim Schalter: KeyHoleCollision (game/pickup.cpp:594)
            # prueft eine Naeherungs-/Ausrichtungszone (TestLaraPosition mit
            # KeyHoleBounds), Lara muss nicht auf dem exakten Sektor des
            # Schlosses stehen. Ohne diese Angabe deadlockt jedes Schloss, das
            # auf demselben Sektor wie seine eigene Tuer sitzt (Andrea3:
            # Keyhole1, Item 27, Raum 15/(2,5) steht auf einem tuerblockierten
            # Sektor) - derselbe Fehler, der beim Switch-Fall schon behoben war,
            # hier aber im Hauptzweig (aufgeloestes Schloss) vergessen wurde.
            return @{ Kind='item'; Sector=$tk; Operable=$true; NeedItemType=$needType; Desc="$label in Schloss Item $ri (Raum $($hole.room))" }
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
    # auch, wenn nur ein angrenzender Sektor erreicht ist (Operable-Flag).
    if ($reached.Contains($req.Sector)) { return $true }
    # Verallgemeinerung, dritter Fund derselben Deadlock-Klasse (nach Switch und
    # Key): steht die TUER selbst genau auf dem Triggersektor - egal welcher
    # Triggertyp -, kann Lara den Sektor per Definition nie *vor* dem Oeffnen
    # betreten. rich3 Raum 134: Lift Doors (Item 172/173) UND ihr eigener
    # Boden-Trigger sitzen auf denselben zwei Sektoren (1,1)/(2,1) - ein
    # gewoehnlicher "Trigger betreten"-Fall, kein Switch/Key, trotzdem derselbe
    # Widerspruch. Statt jeden Triggertyp einzeln mit Operable zu markieren:
    # sobald der exakte Sektor durch eine (noch geschlossene) Tuer blockiert ist,
    # zaehlt ein bereits erreichter Nachbarsektor automatisch mit. Bewusst NICHT
    # pauschal fuer jeden Trigger - nur wenn eine Tuer nachweislich genau dort
    # steht, sonst bliebe die Unterscheidung "muss wirklich betreten werden"
    # wertlos.
    $doorOnSector = $blocked.ContainsKey($req.Sector)
    if (-not $req.Operable -and -not $doorOnSector) { return $false }
    if (-not $adj.ContainsKey($req.Sector)) { return $false }
    foreach($n in $adj[$req.Sector]){ if ($reached.Contains($n)) { return $true } }
    return $false
}

# --- Zonen-Flut --------------------------------------------------------------
# Wo Lara wirklich losgeht, steht nicht zwingend im Lara-Item: laeuft am
# Levelanfang eine Cutscene, setzt sie Lara danach woanders ab. joby5 ist so ein
# Fall - das Lara-Item liegt in Raum 40, gespielt wird ab Raum 3, und Raum 3 ist
# selbst ein Flipmap-Alternate, der Flip ist zu Spielbeginn also schon aktiv.
# Ohne Uebersteuerung flutet der Analyser ab der falschen Stelle und meldet
# grosse Teile des Levels als unerreichbar.
$START_ROOM = @{
    'joby5' = 3
}
$lara = $d.items | Where-Object { $_.typeId -eq 0 } | Select-Object -First 1
$startKey = "$($lara.room),$($lara.sectorX),$($lara.sectorZ)"
$startSeeds = @()
if ($START_ROOM.ContainsKey($LevelName)) {
    $sr = [int]$START_ROOM[$LevelName]
    foreach($s in $d.sectors){
        if ([int]$s.room -eq $sr -and -not $s.isWall) { $startSeeds += ,"$($s.room),$($s.sectorX),$($s.sectorZ)" }
    }
    if ($startSeeds.Count -gt 0) { $startKey = $startSeeds[0] }
}

$blocked = @{}   # sectorKey -> itemIndex der Tuer
foreach($kv in $doorSector.GetEnumerator()){ $blocked[$kv.Value] = $kv.Key }

foreach($ml in $manualLinks){ Add-Edge $ml[0] $ml[1]; Add-Edge $ml[1] $ml[0] }

# GATED_AREA anwenden (nach vollstaendigem Graphaufbau inkl. Flipmap-,
# Teleporter- und Manuallinks, damit wirklich jede Einstiegskante erfasst ist).
foreach($ga in $gatedAreas){
    # ZWEI getrennte Mengen - sie duerfen nicht vermischt werden:
    #  $gateSecs  = die Torsektoren. Nur sie sind vom Kappen ausgenommen, denn
    #               ueber sie fuehrt der einzige legale Weg hinein.
    #  $stopFlood = Begrenzung der Innenbereichs-Flut: Torsektoren PLUS die
    #               Vor-Tor-Raeume.
    # In einer ersten Fassung war beides dieselbe Menge - mit dem Effekt, dass
    # ausgerechnet die Vor-Tor-Raeume vom Kappen ausgenommen wurden, also genau
    # die Quelle der illegalen Einstiegskanten ("0 Einstiegskanten gekappt").
    $gateSecs = New-Object System.Collections.Generic.HashSet[string]
    foreach($di in $ga.Doors){ if ($doorSector.ContainsKey([int]$di)) { [void]$gateSecs.Add($doorSector[[int]$di]) } }
    $stop = New-Object System.Collections.Generic.HashSet[string]
    foreach($g in $gateSecs){ [void]$stop.Add($g) }
    foreach($s0 in $d.sectors){
        if ($ga.PreGateRooms -contains [int]$s0.room) { [void]$stop.Add("$($s0.room),$($s0.sectorX),$($s0.sectorZ)") }
    }

    # Innenbereich = Flut ab den Seed-Raeumen bis zur Stoppkante.
    $inside = New-Object System.Collections.Generic.HashSet[string]
    $qg = New-Object System.Collections.Generic.Queue[string]
    foreach($s0 in $d.sectors){
        if ($ga.SeedRooms -notcontains [int]$s0.room -or $s0.isWall) { continue }
        $k0 = "$($s0.room),$($s0.sectorX),$($s0.sectorZ)"
        if (-not $stop.Contains($k0)) { [void]$inside.Add($k0); $qg.Enqueue($k0) }
    }
    while($qg.Count -gt 0){
        $k = $qg.Dequeue()
        if (-not $adj.ContainsKey($k)) { continue }
        foreach($n in $adj[$k]){
            if ($inside.Contains($n) -or $stop.Contains($n)) { continue }
            [void]$inside.Add($n); $qg.Enqueue($n)
        }
    }
    $insideRooms = @($inside | ForEach-Object { [int](($_ -split ',')[0]) } | Sort-Object -Unique)
    Write-Output "GATED_AREA (Tore $($ga.Doors -join '/')): $($inside.Count) Innensektoren in $($insideRooms.Count) Raeumen"
    Write-Output "  Raeume: $($insideRooms -join ', ')"

    $cut = 0
    foreach($u in @($adj.Keys)){
        if ($inside.Contains($u) -or $gateSecs.Contains($u)) { continue }
        $toRemove = @()
        foreach($v in $adj[$u]){ if ($inside.Contains($v)) { $toRemove += $v } }
        foreach($v in $toRemove){ [void]$adj[$u].Remove($v); $cut++ }
    }
    Write-Output "GATED_AREA: $cut Einstiegskanten ausserhalb der Tore gekappt"
}

$zoneOf = @{}    # sectorKey -> Zonennummer
$openDoors = New-Object System.Collections.Generic.HashSet[int]
$zone = 0
$log = @()

$reached = New-Object System.Collections.Generic.HashSet[string]
[void]$reached.Add($startKey)
foreach($ss in $startSeeds){ [void]$reached.Add($ss) }
$firstParent = @{}   # Sektor -> Sektor, ueber den er ZUERST erreicht wurde

# Alle Trigger, die ueberhaupt eine Tuer schalten - einmal vorab aufgeloest.
# Zusaetzlich zaehlen Trigger, die ein Key-/Puzzle-Item aktivieren: ein Schloss
# muss nicht zwingend eine Tuer oeffnen. Andrea2 ist der Fall - das Puzzle Hole1
# (Item 146, Raum 239) schaltet mit "Object: 128" das Ventil (typeId 172), keine
# Tuer, und fiel damit komplett aus der Betrachtung. Die Kette Brechstange ->
# Ventil -> Schloss war im Modell schlicht nicht vorhanden.
$KEYPUZZ_LO = $PUZZLE_ITEM_BASE; $KEYPUZZ_HI = $KEY_ITEM_BASE + 7
$allTrig = @(); $tn = 0
foreach($t in $d.triggers){
    $hitsDoor = $false
    foreach($c in $t.commands){
        if ($c.type -eq 'Object' -and $doorSector.ContainsKey([int]$c.index)) { $hitsDoor = $true }
        if ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) { $hitsDoor = $true }
        if ($c.type -eq 'Object' -and $items.ContainsKey([int]$c.index)) {
            $tt = [int]$items[[int]$c.index].typeId
            if ($tt -ge $KEYPUZZ_LO -and $tt -le $KEYPUZZ_HI) { $hitsDoor = $true }
        }
    }
    if (-not $hitsDoor) { continue }
    $allTrig += ,@{ Id = $tn; T = $t; Req = (Get-TriggerRequirement $t) }
    $tn++
}
$firedTrig = New-Object System.Collections.Generic.HashSet[int]
$consumed  = @{}   # typeId -> wie viele Instanzen schon in Slots stecken
$doorMask  = @{}   # doorItem -> bisher gesetzte Maskenbits
$doorWhy   = @{}   # doorItem -> Liste der beitragenden Anforderungen
$itemNeed  = @()   # je Slot-Befuellung: welcher Typ, in welcher Zone gebraucht
$totalOfType = @{} # wie viele Instanzen jedes Typs existieren im Level ueberhaupt
foreach($i in $d.items){
    $ti = [int]$i.typeId
    if (-not $totalOfType.ContainsKey($ti)) { $totalOfType[$ti] = 0 }
    $totalOfType[$ti]++
}

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
            # Erstzugangs-Elternknoten mitschreiben: nur so laesst sich spaeter
            # rekonstruieren, ueber WELCHE Kante ein Sektor zum ERSTEN Mal
            # erreicht wurde. Ein Pfad ueber den Endzustand (alle Tueren offen)
            # zeigt stattdessen irgendeinen spaeteren, legalen Weg und verdeckt
            # genau die zu frueh oeffnende Kante, die man sucht.
            $firstParent[$n] = $k
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
        # Cutscene-Tueren, die ein Werkzeug voraussetzen (rich1 hamgate: der
        # Hammer), duerfen nicht schon beim blossen Erreichen des Trigger-
        # sektors feuern - Lara muss das Werkzeug vorher tatsaechlich haben.
        # Ohne diese Sperre wuerde der Hammer als "automatisch vorhanden"
        # behandelt, obwohl er in rich1 ein echter, erst zu findender Gegen-
        # stand ist (User-Korrektur).
        if ($null -ne $hammerType) {
            $needsHammer = $false
            foreach($c in $e.T.commands){ if ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) { $needsHammer = $true } }
            if ($needsHammer) {
                $haveHammer = $false
                foreach($hi in $hammerInLevel){ if ($reached.Contains("$($hi.room),$($hi.sectorX),$($hi.sectorZ)")) { $haveHammer = $true; break } }
                if (-not $haveHammer) { continue }
            }
        }
        if ($e.Req.Kind -eq 'item') {
            $nt = $e.Req.NeedItemType
            if ($null -eq $nt) { continue }
            $nt = [int]$nt
            # Gibt es den Schluesseltyp im Level ueberhaupt nicht, bringt Lara ihn
            # per Gameflow mit (CMD_GIVEOBJ) - dasselbe Muster wie bei der
            # Brechstange. joby5 hat ein Keyhole7, aber kein Key Item7.
            if (-not $totalOfType.ContainsKey($nt)) {
                $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc=$e.Req.Desc; Gameflow=$true }
            }
            elseif ($reusableKeys -contains $nt) {
                # Mehrfachnutzer: belegt keinen Vorrat, konkurriert mit nichts.
                if ((&{ if($haveCounts.ContainsKey($nt)){$haveCounts[$nt]}else{0} }) -le 0) { continue }
                $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc="$($e.Req.Desc) [mehrfach nutzbar]"; Reusable=$true }
            }
            else {
                $has  = if($haveCounts.ContainsKey($nt)){ $haveCounts[$nt] } else { 0 }
                $used = if($consumed.ContainsKey($nt)){ $consumed[$nt] } else { 0 }
                if (($has - $used) -le 0) { continue }
                $consumed[$nt] = $used + 1
                $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc=$e.Req.Desc }
            }
        }
        [void]$firedTrig.Add($e.Id)
        # Maske dieses Triggers auf alle Ziel-Tueren anrechnen.
        foreach($c in $e.T.commands){
            $tgt = $null
            if ($c.type -eq 'Object') { $tgt = [int]$c.index }
            elseif ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) {
                $tgt = $cutsceneDoorByCutscene[[int]$c.index]
            }
            if ($null -eq $tgt) { continue }
            # Kabel stehen von Levelbeginn an unter Strom. Ein normaler
            # Bodentrigger auf ein Kabel schaltet es ein, nicht aus - nur ein
            # Schalter (bzw. AntiTrigger) macht stromlos. Ohne diese
            # Einschraenkung wuerde joby5 die Kabel 91 und 136 schon ueber einen
            # Bodentrigger in Raum 138 "oeffnen", was die Richtung umdreht.
            if ($items.ContainsKey($tgt) -and [int]$items[$tgt].typeId -eq 98 -and
                $e.T.triggerType -notin @('Switch','HeavySwitch','AntiTrigger','HeavyAntiTrigger')) { continue }
            if (-not $doorMask.ContainsKey($tgt)) { $doorMask[$tgt] = 0; $doorWhy[$tgt] = @() }
            # Cutscene-Tueren (rich1 hamgate) haben keine echte 5-Bit-Maske im
            # Sinne von IFL_CODEBITS - hamgate_init() setzt sie direkt im Skript.
            # Volle Maske erzwingen, sobald der Trigger einmal feuert.
            if ($cutsceneDoorByCutscene.Values -contains $tgt -and $c.type -eq 'Cutscene') { $doorMask[$tgt] = 31 }
            else { $doorMask[$tgt] = $doorMask[$tgt] -bor ([int]$e.T.mask) }
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

    # Brechstangen-Tueren: kein Trigger schaltet sie, die Maske fuellt sich also
    # nie. Sie gehen auf, sobald Lara die Brechstange hat. Die Brechstange wird
    # NICHT verbraucht (anders als Keys und Puzzle-Items) - eine reicht fuer
    # beliebig viele solcher Tueren, deshalb keine Zaehlung, keine Konkurrenz.
    if ($crowbarDoors.Count -gt 0) {
        $haveCrowbar = $false
        foreach($ci in $crowbarInLevel){
            if ($reached.Contains("$($ci.room),$($ci.sectorX),$($ci.sectorZ)")) { $haveCrowbar = $true; break }
        }
        if ($haveCrowbar) {
            foreach($cd in $crowbarDoors){
                if ($openDoors.Contains($cd)) { continue }
                if (-not $doorSector.ContainsKey($cd)) { continue }
                $newlyOpened += ,@{ Door=$cd; Req=@{ Desc='Brechstange (nicht verbraucht)' } }
            }
        }
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

# Pro Key-/Puzzle-Item-Typ die lockerste (maximale) "muss in Zone <= N liegen"-
# Anforderung ueber alle seine Slot-Instanzen - dieselbe Gruppierung, die auch
# der Textreport weiter unten anzeigt, hier vorgezogen, damit der JSON-Export
# sie mitliefern kann. Gameflow-Typen (kein Slot im Level platziert) bleiben
# aussen vor - fuer sie gibt es keine sinnvolle "wo platzieren"-Frage.
$byType = @{}
foreach($n in ($itemNeed | Where-Object { -not $_.Gameflow })){
    $t = [int]$n.Type
    if (-not $byType.ContainsKey($t)) { $byType[$t] = @() }
    $byType[$t] += ,$n
}
$keyConstraints = @()
foreach($kv in $byType.GetEnumerator()){
    $nt = [int]$kv.Key
    # Bleibt Vanilla: Anforderung oben im Report sichtbar, aber nicht im Pool.
    if ($vanillaTypes -contains $nt) { continue }
    $maxZ = ($kv.Value | ForEach-Object { [int]$_.Zone } | Measure-Object -Maximum).Maximum
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $noPlacement | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=$maxZ; instances=$kv.Value.Count; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms }
}
# Handgesetzte Zusatztypen anhaengen, sofern das Modell sie nicht ohnehin schon
# selbst abgeleitet hat (dann gilt die abgeleitete Grenze, nicht die Handangabe).
foreach($kv in $extraPool.GetEnumerator()){
    $nt = [int]$kv.Key
    if ($byType.ContainsKey($nt)) { continue }
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $noPlacement | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=[int]$kv.Value; instances=0; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms }
}

if ($ExportZonesJson -ne "") {
    # Maschinenlesbarer Export: je Sektor, der sowohl im Flood erreicht als auch
    # laut Exporter tatsaechlich itemfaehig ist (validForItems), seine Zone.
    # Nur validForItems-Sektoren, damit ein nachgelagerter Patch-Generator nie
    # ein Item auf eine Wand, ein Portal oder einen sonst ungeeigneten Sektor
    # setzt - dieselbe Prüfung, die schon die *_AllValidSectorsFilled.trc-Dateien
    # sauber gehalten hat, bleibt hier die Grundlage.
    $rows = @()
    foreach($kv in $zoneOf.GetEnumerator()){
        $s = $sec[$kv.Key]
        if (-not $s -or -not $s.validForItems) { continue }
        $rows += [PSCustomObject]@{ room=[int]$s.room; sectorX=[int]$s.sectorX; sectorZ=[int]$s.sectorZ; zone=[int]$kv.Value; floorY=[int]$s.floorY }
    }
    $maxZoneOut = if($rows.Count -gt 0){ ($rows | Measure-Object -Property zone -Maximum).Maximum } else { 0 }
    [PSCustomObject]@{ level=$LevelName; maxZone=$maxZoneOut; sectors=$rows; keyConstraints=$keyConstraints; fixedItems=$fixedItems; requiredInjections=$requiredInjections } | ConvertTo-Json -Depth 4 -Compress | Out-File $ExportZonesJson -Encoding utf8
    Write-Output "Zonen-Export geschrieben: $ExportZonesJson ($($rows.Count) Sektoren, maxZone=$maxZoneOut, $($keyConstraints.Count) Key-Constraints)"
}

if ($DebugPathTo -ne "") {
    Write-Output "--- Debug: ERSTZUGANGS-Pfad zu $DebugPathTo ---"
    if (-not $zoneOf.ContainsKey($DebugPathTo)) {
        Write-Output "  Ziel nie erreicht - kein Pfad im begehbaren Graphen."
    } else {
        # Nutzt $firstParent aus dem Flood selbst, NICHT eine neue BFS ueber den
        # Endzustand: gesucht ist die Kante, ueber die der Sektor zum ersten Mal
        # erreicht wurde. Eine BFS mit allen am Ende offenen Tueren findet
        # stattdessen irgendeinen spaeteren legalen Weg und verdeckt genau die
        # zu frueh oeffnende Kante (bei Andrea1/Raum 117 lief sie faelschlich
        # durch die laengst geoeffnete Tuer 53 statt ueber den echten Frueh-
        # zugang).
        $path = @($DebugPathTo); $cur = $DebugPathTo
        $guard = 0
        while ($firstParent.ContainsKey($cur) -and $guard -lt 10000) { $cur = $firstParent[$cur]; $path = ,$cur + $path; $guard++ }
        for($i=0; $i -lt $path.Count; $i++){
            $zHere = if($zoneOf.ContainsKey($path[$i])){ $zoneOf[$path[$i]] } else { '?' }
            $roomChange = if($i -gt 0){ ($path[$i] -split ',')[0] -ne ($path[$i-1] -split ',')[0] } else { $false }
            $marker = if($roomChange){ " <- Raumwechsel" } else { "" }
            "  [{0,3}] {1,-14} Zone {2}{3}" -f $i,$path[$i],$zHere,$marker
        }
    }
    Write-Output ""
}

# --- Ausgabe ----------------------------------------------------------------
Write-Output "=============================================================="
Write-Output " Progressions-Zonen: $LevelName"
Write-Output "=============================================================="
if ($startSeeds.Count -gt 0) {
    Write-Output "Laras Start: Raum $($START_ROOM[$LevelName]) (nach der Eingangs-Cutscene; Lara-Item liegt in Raum $($lara.room)) -> Zone 0"
} else {
    Write-Output "Laras Start: Raum $($lara.room), Sektor ($($lara.sectorX),$($lara.sectorZ)) -> Zone 0"
}
Write-Output ""
Write-Output "--- Zonenuebergaenge (Blocker) ---"
foreach($e in $log){
    $di = $e.Door; $it = $items[$di]
    "Zone {0} -> {1}: Tuer Item {2} (typeId {3}, Raum {4})  geoeffnet durch: {5}" -f `
        $e.Zone, ($e.Zone+1), $di, $it.typeId, $it.room, $e.Req.Desc
}
Write-Output ""
if ($DebugSectors.Count -gt 0) {
    Write-Output "--- Debug: einzelne Sektoren ---"
    foreach($dk in $DebugSectors){
        $z = if($zoneOf.ContainsKey($dk)){ "Zone $($zoneOf[$dk])" } else { "NICHT ERREICHT" }
        $isBlocked = $blocked.ContainsKey($dk)
        $edges = if($adj.ContainsKey($dk)){ ($adj[$dk] | Sort-Object) -join ', ' } else { '(keine Kanten im Graph)' }
        "  {0}: {1}  blockiert(Tuer)={2}  Kanten -> {3}" -f $dk,$z,$isBlocked,$edges
    }
    Write-Output ""
}

$maxZone = ($zoneOf.Values | Measure-Object -Maximum).Maximum
Write-Output "--- Sektoren pro Zone ---"
for($z=0; $z -le $maxZone; $z++){
    $cnt = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z }).Count
    $rms = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z } | ForEach-Object { ($_.Key -split ',')[0] } | Sort-Object -Unique).Count
    "Zone {0}: {1,5} Sektoren in {2,3} Raeumen" -f $z,$cnt,$rms
}
# Kulissenraeume zaehlen nicht als Fehlbestand - sie sind nicht zum Betreten
# gedacht, ihr Fehlen ist also kein Defizit der Analyse. Dasselbe gilt fuer die
# handkuratierten EXCLUDE_ROOMS: dass sie nicht erreicht werden, ist das
# gewuenschte Ergebnis, kein Loch im Modell - sonst saehe Andrea2 mit seinen
# 763 gesperrten Sektoren aus, als waere die Analyse um 18 Prozent schlechter
# geworden, obwohl die Restluecke unveraendert klein ist.
$unreachedSec = @($d.sectors | Where-Object {
    -not $_.isWall -and $cameraRooms -notcontains [int]$_.room -and
    $excludeRooms -notcontains [int]$_.room -and
    -not $zoneOf.ContainsKey("$($_.room),$($_.sectorX),$($_.sectorZ)") })
Write-Output "Nie erreicht: $($unreachedSec.Count) begehbare Sektoren"
if ($excludeRooms.Count -gt 0) {
    $exSec = @($d.sectors | Where-Object { -not $_.isWall -and $excludeRooms -contains [int]$_.room })
    Write-Output "Handgesperrt (EXCLUDE_ROOMS): $($exSec.Count) Sektoren in $($excludeRooms.Count) Raeumen - gewollt, nicht als Luecke gezaehlt"
}
if ($vanillaTypes.Count -gt 0) {
    Write-Output "Bleiben Vanilla (VANILLA_TYPES) - nicht im Pool: $($vanillaTypes -join ', ')"
}
if ($requiredInjections.Count -gt 0) {
    Write-Output "Pflicht-Injektionen (REQUIRED_INJECTIONS) - fehlen in Vanilla:"
    foreach($ri in $requiredInjections){
        $have = @($d.items | Where-Object { [int]$_.typeId -eq [int]$ri.typeId }).Count
        Write-Output ("  typeId {0,4} -> Raum {1,4} ({2}; im Level vorhanden: {3})" -f $ri.typeId,$ri.room,$ri.reason,$have)
    }
}
if ($fixedItems.Count -gt 0) {
    Write-Output "Unantastbare Items (FIXED_ITEMS) - duerfen nicht verschoben werden:"
    foreach($fi in $fixedItems){
        $inst = @($d.items | Where-Object { [int]$_.index -eq [int]$fi.item })
        $where = if($inst.Count){ "Raum $($inst[0].room), typeId $($inst[0].typeId)" } else { "im Level nicht gefunden" }
        Write-Output ("  Item {0,4} - {1} ({2})" -f $fi.item,$fi.reason,$where)
    }
}
if ($cameraRooms.Count -gt 0) {
    $camSec = @($d.sectors | Where-Object { -not $_.isWall -and $cameraRooms -contains [int]$_.room })
    "  (zusaetzlich {0} Sektoren in {1} reinen Kulissenraeumen, nicht mitgezaehlt)" -f $camSec.Count,$cameraRooms.Count
}
Write-Output ""
Write-Output "--- Key-/Puzzle-Items: erlaubter Platzierungsbereich ---"
# Zaehl-Regel statt Typ-Regel: braucht ein Level zwei Slots desselben Typs,
# reicht es nicht, dass "irgendein" Item frueh genug liegt. Die k-te Instanz
# muss spaetestens in der Zone liegen, in der der k-te Slot bedient wird.
# Gameflow-Items bringt Lara mit, sie brauchen keine Platzierung und gehoeren
# deshalb nicht in die Zaehlregel - sonst meldet sie "0 Instanzen vorhanden,
# WIDERSPRUCH" fuer etwas, das voellig in Ordnung ist (rich3: Puzzle Hole1 in
# Raum 119, aber kein Puzzle Item1 im Level).
$gameflowNeeds = @($itemNeed | Where-Object { $_.Gameflow })
if ($gameflowNeeds.Count -gt 0) {
    Write-Output "Nicht im Level vorhanden, kommt per Gameflow (CMD_GIVEOBJ) - keine Platzierung noetig:"
    foreach($g in ($gameflowNeeds | Group-Object Type)){
        "  TypeId {0} - gebraucht ab Zone {1} ({2})" -f $g.Name,(($g.Group | ForEach-Object { $_.Zone } | Measure-Object -Minimum).Minimum),$g.Group[0].Desc
    }
    Write-Output ""
}

# $byType wurde bereits vor dem ExportZonesJson-Block weiter oben berechnet -
# hier nur noch die Textausgabe.
foreach($kv in $byType.GetEnumerator() | Sort-Object Name){
    $nt = [int]$kv.Key
    $needs = @($kv.Value | Sort-Object { $_.Zone })
    # Mehrfach nutzbare Schluessel brauchen nur EINE Instanz, egal wie viele
    # Schloesser sie bedienen - sonst meldet die Zaehlregel falschen Mangel.
    if ($reusableKeys -contains $nt) {
        "TypeId {0}: mehrfach nutzbar - 1 Instanz reicht fuer {1} Schloss/Schloesser" -f $nt, $needs.Count
        $needs = @($needs | Select-Object -First 1)
    }
    else { "TypeId {0}: {1} Instanz(en) noetig" -f $nt, $needs.Count }
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
Write-Output "--- Groesste unerreichte Gebiete ---"
# Unerreichte Sektoren zu Komponenten gruppieren und je Komponente den
# raeumlich naechsten erreichten Sektor suchen. Damit steht im Report, WO in
# trview nachzusehen ist, statt nur DASS etwas fehlt. Kleine Distanz und
# geringer Hoehenunterschied = vermutlich ein Sprung oder eine Kletterstelle.
# Alternate-Raeume bleiben aussen vor: sie sind nie eigenstaendig betretbar,
# sondern tauschen ihren Basisraum aus. Sie als eigenes "unerreichtes Gebiet" zu
# melden erzeugt nur Rauschen - zwei Alternates verschiedener Flip-Gruppen
# koennen raeumlich exakt uebereinanderliegen (joby5 Raum 52 und 86, Abstand 0,
# Hoehendiff 0) und sehen dann wie eine fehlende Verbindung aus, obwohl sie im
# Spiel nie gleichzeitig existieren. Die richtige Frage ist immer, wie Lara den
# BASISraum erreicht.
$unreach = @{}
foreach($s in $d.sectors){
    if ($s.isWall -or $s.alternateRoom) { continue }
    if ($cameraRooms -contains [int]$s.room) { continue }   # reine Kulisse, siehe CAMERA_ROOMS
    if ($excludeRooms -contains [int]$s.room) { continue }  # handgesperrt, siehe EXCLUDE_ROOMS
    $k = "$($s.room),$($s.sectorX),$($s.sectorZ)"
    if ($zoneOf.ContainsKey($k)) { continue }
    $unreach[$k] = $s
}
$seen = @{}; $comps = @()
foreach($k in $unreach.Keys){
    if ($seen.ContainsKey($k)) { continue }
    $comp = @(); $q = New-Object System.Collections.Generic.Queue[string]
    $q.Enqueue($k); $seen[$k] = $true
    while($q.Count -gt 0){
        $c = $q.Dequeue(); $comp += ,$c
        if (-not $adj.ContainsKey($c)) { continue }
        foreach($nb in $adj[$c]){
            if ($seen.ContainsKey($nb) -or -not $unreach.ContainsKey($nb)) { continue }
            $seen[$nb] = $true; $q.Enqueue($nb)
        }
    }
    $comps += ,$comp
}
# KEIN Weltabstand als Hinweis. TR-Raeume ueberlappen sich im Weltkoordinaten-
# system beliebig; erst die Portale nähen sie zusammen, wodurch nicht-euklidische
# Geometrie entsteht (Area 51 in TR3: das UFO ist geometrisch winzig, ingame
# riesig). Zwei Sektoren an derselben Weltkoordinate koennen im Spiel voellig
# unverbunden sein - "naechster Anschluss nach Abstand" war deshalb irrefuehrend.
# Da Portalkanten inzwischen beidseitig sind, gilt: hat ein Gebiet ueberhaupt ein
# Portal in den erreichten Teil, ist es auch erreicht. Bleibt es uebrig, fuehrt
# der Weg dorthin ueber etwas, das nicht in den Sektordaten steht.
# Ein Gebiet, in dem nur Animating-Objekte stehen und kein einziger Pickup,
# Gegner, Schalter oder Tuerteil, ist mit hoher Wahrscheinlichkeit reine
# Kamerafahrt-Geometrie und gar nicht zum Betreten gedacht. Andy2 (Labyrinth)
# ist der belegte Fall: das eigentliche Level ist klein, wirkt durch die
# beweglichen Bruecken aber gross, und die zusaetzlichen Muehlen-Raeume gibt es
# nur fuer den Flyby - eine drehbare Muehle waere als Animating nicht begehbar,
# deshalb bekam sie einen eigenen Raum. Solche Gebiete sind KEINE Luecke.
$ANIM_LO = 416; $ANIM_HI = 447
# Bereits geklaerte Luecken. Ohne das meldet der Report bei jedem Lauf dieselben
# Raeume als offene Frage, obwohl die Antwort laengst vorliegt - und die Antwort
# steht dann nur im Chatverlauf statt im Werkzeug. Raum -> Grund.
$KNOWN_GAPS = @{
    'Andrea3' = @{
        13 = 'Antitrigger-Fix der Devs, fuer uns ohne Bedeutung'
        71 = 'Ueberbleibsel der Devs, ohne Funktion'
        72 = 'Ueberbleibsel der Devs, ohne Funktion'
        77 = 'ohne Funktion'
    }
}
$knownGaps = @{}
if ($KNOWN_GAPS.ContainsKey($LevelName)) {
    foreach($kv in $KNOWN_GAPS[$LevelName].GetEnumerator()){ $knownGaps[[int]$kv.Key] = [string]$kv.Value }
}
foreach($comp in ($comps | Sort-Object { -$_.Count } | Select-Object -First 4)){
    $compRooms = ($comp | ForEach-Object { [int](($_ -split ',')[0]) } | Sort-Object -Unique)
    $itemsIn = @($d.items | Where-Object { $compRooms -contains [int]$_.room })
    $meaningful = @($itemsIn | Where-Object { [int]$_.typeId -lt $ANIM_LO -or [int]$_.typeId -gt $ANIM_HI })
    $allKnown = ($compRooms.Count -gt 0) -and -not (@($compRooms | Where-Object { -not $knownGaps.ContainsKey($_) }).Count)
    "Gebiet mit {0,5} Sektoren in {1,3} Raeumen" -f $comp.Count,$compRooms.Count
    "   Raeume: {0}" -f (($compRooms | Select-Object -First 20) -join ', ')
    if ($compRooms.Count -gt 20) { "           ... und {0} weitere" -f ($compRooms.Count-20) }
    if ($allKnown) {
        "   GEKLAERT - keine offene Frage:"
        foreach($rr in $compRooms){ "     Raum {0,4}: {1}" -f $rr,$knownGaps[$rr] }
    }
    elseif ($itemsIn.Count -gt 0 -and $meaningful.Count -eq 0) {
        "   Enthaelt {0} Objekte, davon KEINES bespielbar - nur Animatings." -f $itemsIn.Count
        "   => vermutlich reine Kamerafahrt-Geometrie, keine echte Luecke."
    } else {
        "   Kein Portal verbindet dieses Gebiet mit dem erreichten Teil."
        "   Der Zugang ist also Sprung, Kletterstelle, Monkeyswing, Tauchgang,"
        "   Teleporter oder Cutscene - bitte im Spiel bzw. trview klaeren."
        if ($meaningful.Count -gt 0) {
            "   Bespielbare Objekte darin: {0}" -f (($meaningful | Group-Object typeId | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
        }
    }
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
