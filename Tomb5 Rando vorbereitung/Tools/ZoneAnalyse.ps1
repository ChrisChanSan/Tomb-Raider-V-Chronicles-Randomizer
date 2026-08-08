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
    # joby2: Gelaende, das begehbar aussieht und es nicht ist - die grossen
    # geneigten Flaechen am Rand der Anlage (Raum 158 ist der auffaelligste
    # Fall, vom Nutzer in trview geprueft). Der Zonen-Flood laeuft ueber sie
    # hinweg, weil die Sektoren Boden melden; Lara kommt dort nie hin.
    # Zusammen 296 platzierbare Sektoren, die sonst als Ablage gelten wuerden.
    # Zweite Runde nach Durchsicht in trview: Daecher, Schiffshuellen, Kaimauern
    # und andere Flaechen, die Boden melden und keiner sind. Kein einziges
    # Pickup liegt darin - nur zwei Sniper, Animatings und Bridge-Flats.
    'joby2' = @(6,7,9,14,149,155,158,
                8,10,13,15,19,21,139,145,147,148,150,151,153,154,159,
                # 39, 41 und 44 sind erst durch die Lichtenkorrektur ueberhaupt
                # aufgetaucht - alle drei Ablagen je Raum sind neu. Der Nutzer
                # hat sie sofort als ungueltig erkannt. Die Korrektur bringt
                # also nicht nur Gewinn, sie schafft auch neuen Pruefbedarf.
                39,41,44)
    # joby3, vom Nutzer in trview durchgesehen. Kein Pickup liegt darin - nur
    # ein Electrical Light und ein Animating in Raum 104 sowie je eine
    # geschlossene Tuer in 118 und 130.
    #
    # 116, 119, 129 und 184 sind beim Messen dazugekommen: sie haengen nur an
    # den obigen Raeumen und fielen dadurch heraus. 116, 129 und 184 haben
    # ohnehin keine einzige Ablage, sind also reine Durchgaenge. Raum 119 hat
    # vier - die liegen laut Nutzer hinter einer Dekotuer und sind unerreichbar.
    #
    # Merkposten dazu: unsere Dekotueren-Regel behandelt eine Tuer ohne Trigger
    # als NICHT sperrend. In joby2 war das richtig, weil Lara die Simse
    # anspringen konnte. Hier ist es umgekehrt - die Tuer geht nie auf und es
    # gibt keinen anderen Weg. Beide Faelle sehen in den Daten gleich aus, der
    # Unterschied steckt allein in der Geometrie dahinter.
    'joby3' = @(73,104,115,116,118,119,129,130,163,171,174,177,184)
    # andy1, vom Nutzer in trview durchgesehen. Raum 236 ist mit 36 Ablagen der
    # groesste Einzelposten des Levels und liegt hinter dem Levelende - reine
    # Kulisse. 71, 92, 93, 162 und 211 ebenso. Kein Item liegt in einem davon.
    #
    # Ausdruecklich NICHT dabei, obwohl der Verdachtsgenerator sie oben listete:
    # 113 (dort wird eine Cutscene ausgeloest, Kernbereich) sowie 195, 50, 197,
    # 170, 38 und 37 - alle bespielbar. Der Generator zeigt Verdaechtige, keine
    # Ergebnisse; hier lag er in sieben von dreizehn Faellen daneben.
    'andy1' = @(71,92,93,162,211,236)
    # joby5: vier Raeume gleich am Levelanfang. Die Tueren davor - 176, 187,
    # 193 und 195 - haben KEINEN Trigger, unsere Dekotueren-Regel laesst sie
    # deshalb als nicht sperrend durchgehen und die Raeume dahinter gelten als
    # erreichbar. Sie sind es nicht. Drei davon enthalten nur ein Pulse Light.
    # Dritter Fall dieser Art nach joby3 Raum 119 und joby2 Raum 11: in den
    # Daten sehen sperrende und nicht sperrende Dekotueren identisch aus.
    # Raum 112 kommt aus derselben Durchsicht: er haengt an einer Floor
    # Trapdoor1 (Item 130) mit flags 0 und ohne jeden Trigger - sie geht nie
    # auf. Vierter Fall der Dekotueren-Grenze in diesem Level.
    'joby5' = @(112,160,169,172,175)
    # joby4 stand hier zuerst mit den Raeumen 116, 135, 136 - der Schaetzung
    # 3 x 1024 folgend und damals raumsauber. Die Messung des Nutzers im Spiel
    # hat die Grenze dann bei rund y 13200 verortet, und dort liegt sie NICHT
    # mehr raumsauber. Sie steht jetzt als Zahl in $MIN_FLOOR_Y und deckt diese
    # drei Raeume mit ab.
    #
    # 120 und 122 kommen aus der Durchsicht des Nutzers dazu. Sie sind der
    # groesste Einzelposten des Levels: je 96 Ablagen, zusammen 192 von 586,
    # also ein knappes Drittel. Kein Item liegt in beiden.
    'joby4' = @(120,122)
}

# Einzelne Sektoren sperren, wo ein ganzer Raum zu grob waere. Raum 141 in joby2
# ist der Fall: er ist bespielbar, nur zwei Felder am Rand sind es nicht.
# Format wie ueberall sonst "Raum,x,z".
$EXCLUDE_SECTORS = @{
    # Raum 141: zwei Felder am Rand, sonst voll bespielbar.
    # Raum 152: die grosse schraege Flaeche - x 2 bis 11 auf z 2 und z 3, dazu
    # die Ausbuchtung (5,4) und (6,4). Den ganzen Raum zu sperren waere falsch
    # gewesen: Lara landet nach der Cutscene an Tuer 41 genau dort, und die
    # beiden Cutscene-Trigger auf (11,7) und (11,8) waeren unerreichbar geworden.
    # Der begehbare Teil liegt bei x 10/11 in den Reihen z 7-13 und 17-18.
    'joby2' = @('141,10,12','141,11,12') +
              @(2..11 | ForEach-Object { "152,$_,2" }) +
              @(2..11 | ForEach-Object { "152,$_,3" }) +
              @('152,5,4','152,6,4')
}
$excludeRooms = @( if($EXCLUDE_ROOMS.ContainsKey($LevelName)){ $EXCLUDE_ROOMS[$LevelName] } )
$excludeSectors = @( if($EXCLUDE_SECTORS.ContainsKey($LevelName)){ $EXCLUDE_SECTORS[$LevelName] } )

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
    # joby2: Key Item7 darf nicht hinter Tuer 41 landen. Die abgeleitete Regel
    # "nicht hinter der eigenen Tuer" greift hier fast nicht - sie findet nur
    # einen einzigen Sektor, weil man ueber den Aussenbereich hinter die Tuer
    # kommt, ohne sie zu oeffnen. Aus Modellsicht liegt der Bereich damit gar
    # nicht dahinter, im Spiel schon. Deshalb hier von Hand, vom Nutzer in
    # trview zusammengestellt.
    'joby2' = @{
        # Raum 67 kam wie 39/41/44 erst mit der Lichtenkorrektur dazu, mit 23
        # Ablagen auf einen Schlag. Anders als jene bleibt er fuer normale
        # Items nutzbar - nur Key Item7 darf dort nicht liegen.
        202 = @(11,12,16,20,24,25,34,40,42,60,61,65,67,73,76,92,93,98,138,152)
        203 = @(83,115)                       # Key Item8
    }
    # joby3: Key Item7 darf nicht hinter Tuer 175 liegen, sie ist sein Deadstop
    # (Nutzer: "Bis Raum 106 duerfen Key 6/7 auftauchen"). Gemessen mit einer
    # Flut ab Raum 109, Tuer 175 als Wand, Raum 123 als Grenze: dahinter liegen
    # 58 Raeume mit Ablagen - aber 52 davon schliesst die Zonengrenze <=Z2
    # ohnehin schon aus. Uebrig bleiben diese sechs, und nur sie machen den
    # Unterschied. Raum 109 ist der Fall, den der Nutzer im Testlevel sah.
    # Key Item6 braucht nichts dergleichen: er bleibt beim Chef in VANILLA_TYPES.
    'joby3' = @{
        202 = @(49,60,109,126,152,159)
    }
    # joby5: Keyhole6 oeffnet die Raeume 18, 54 und 106. Key Item6 darf dort
    # nicht liegen - er waere hinter der Tuer, die er selbst oeffnet. Genau der
    # Fall, den die zurueckgenommene automatische Regel abdecken sollte; bis es
    # dafuer eine tragfaehige Loesung gibt (Aufgabe #49), steht er hier.
    'joby5' = @{
        201 = @(18,54,106)
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
    # typeId 240 "Crowbar Item", NICHT 172: 172 heisst zwar "Puzzle Item1", ist
    # aber je nach Level etwas anderes (in joby3/rich1 eine getarnte
    # Brechstange, in Andrea2 ein Ventil). Hier stand zuerst faelschlich 172 -
    # aufgefallen erst, als der Randomizer den Patch tatsaechlich erzeugt hat.
    'Andrea3' = @( @{ typeId = 240; room = 120; reason = 'Level enthaelt keine Brechstange - Vanilla-Softlock' } )
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
    'rich1'   = @{ 27 = 'CD - Invisible, wird per Switch aus dem Anzug enthuellt' }
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
    # joby3s Puzzle Item1 (Item 41, Raum 22) ist die getarnte Brechstange -
    # sie sieht wie ein Puzzleteil aus, oeffnet aber die Brechstangentueren 25
    # und 125. Sie bleibt an ihrem Platz, damit die Pickup-Cutscene stimmt.
    #
    # Dazu zwei Dinge rund um den Chef in Raum 120/107, beide Nutzerangabe:
    # Key Item6 (201) bleibt beim Chef liegen, und die Pistolen (334) bleiben in
    # Raum 107 - werden sie verschoben, tritt der Chef-Glitch auf. Deshalb ist
    # Key6 auch kein Fall fuer einen Deadstop; nur Key Item7 braucht einen, und
    # den liefert die abgeleitete Regel "nicht hinter der eigenen Tuer" (Tuer
    # 175) bereits von selbst.
    'joby3' = @(172,201,334)
    # rich1s Puzzle Item3 ist die CD. Sie traegt in Vanilla Invisible=true und
    # liegt ausserhalb der Karte; der Switch in Raum 15 aktiviert sie zusammen
    # mit Door 4 und Door 2. Im Spiel faellt sie aus dem Anzug, sobald dessen
    # Animation laeuft. Ein Ortswechsel wuerde die Enthuellung zerstoeren.
    'rich1'   = @(174)
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

#----------------------------------------------------------------------------
# Flipmap-Raeume.
#
# Ein Flipraum ist kein zusaetzlicher Ort, sondern ein ZUSTAND desselben Ortes.
# Die Sektordaten enthalten beide Fassungen nebeneinander, und wer sie einfach
# uebereinanderlegt, bekommt eine Geometrie, die es im Spiel nie gibt: jede
# Wand der Basis UND jeder Durchgang der Flipvariante, alles gleichzeitig, alles
# ab Levelstart.
#
# Genau daran haette joby2 einen sicheren Softlock produziert. Der Kran schaltet
# per FlipMap das Rolltor auf (Basis 89/87/80 zu, Flipvariante 53/54/69 offen).
# Die Flut lief durch die Flipvariante, also galt der ganze Bereich dahinter ab
# Zone 0 als erreichbar - und Key Item8 durfte dort liegen. Nur wird die Flipmap
# ihrerseits erst durch Key8 ausgeloest. Wer den Schluessel hinter das Tor legt,
# das er selbst oeffnen soll, hat das Level unloesbar gemacht.
#
# Deshalb die Vorgabe: Sektoren einer Flipvariante tragen NICHT. Das ist die
# konservative Richtung - sie kann Erreichbarkeit nur verkleinern, nie einen Weg
# erfinden, und ein zu kleiner Bereich faellt im Bericht auf, ein zu grosser
# erst im Spiel. Nutzer dazu: "Flipmaps koennen oft Wege blocken."
#
# Wo eine Flipvariante wirklich einen Weg oeffnet, wird sie hier freigegeben -
# gleiche Form wie $SCRIPT_DOORS: erreichter Raum als Bedingung, nicht "immer
# offen". Vorher gilt sie als geschlossen, danach beginnt eine neue Zone.
#----------------------------------------------------------------------------
$FLIP_GATE = @{
    # andy1, die Kirche. Basis 48 -> Variante 214, Basis 151 -> Variante 213.
    # Das Holz verbaut den groessten Teil des Levels. Lara baut aus den beiden
    # Haelften Puzzle Item2 Combo1 (Item 18, Raum 82) und Combo2 (Item 39, Raum
    # 172) eine Zwille; damit laesst eine Cutscene die Glocken herunterkrachen,
    # die das Holz wegsprengen - erst danach ist der Rest des Levels begehbar.
    # Deshalb NeedTypes und nicht AfterRoom: der Kirchenraum allein genuegt
    # nicht, Lara steht dort lange bevor sie die Teile beisammen hat.
    'andy1' = @( @{ Rooms = @(213,214); NeedTypes = @(182,183); Why = 'Zwille laesst die Glocken fallen' } )
    # joby2, das Rolltor am Kran. Die Flipmap dahinter wird durch Key Item8
    # ausgeloest - liegt der Schluessel hinter dem Tor, das er oeffnen soll, ist
    # das Level unloesbar. Als Deadstop gesetzt: Raum 89 und alles dahinter
    # bilden eine eigene Inselgruppe, erst ab dem Kran erreichbar.
    # Gesperrt wird die GLEISBETT-KETTE, nicht nur der Raum hinter dem Tor.
    # Das Gleisbett liegt 256 Units tiefer als das Rolltor, die Flut laeuft also
    # schlicht darunter hindurch: 132 -> 5 -> 4 -> 2 und von dort in alles, was
    # hinter dem Tor liegt. Nur Raum 89 zu sperren liess diesen Umweg offen.
    # 2, 4 und 5 haben selbst keinen einzigen platzierbaren Sektor - sie kosten
    # als Sperre also nichts und schneiden den Umweg an der Quelle ab.
    'joby2' = @( @{ BlockRooms = @(2,4,5,89); AfterRoom = 35; Why = 'Rolltor am Kran' } )
}
$flipGates = @( if($FLIP_GATE.ContainsKey($LevelName)){ $FLIP_GATE[$LevelName] } )

#----------------------------------------------------------------------------
# Inselgruppen: Bodenhoehe als Grenze fuer Schluessel.
#
# Ein Sektor ist noch lange kein erreichbarer Ablageort. In joby2 stehen in Raum
# 84 Kistenstapel; ihre Oberseiten sind begehbare Sektoren, aber Lara kommt dort
# ueberhaupt erst hinauf, NACHDEM sie Key Item8 hat. Ein Schluessel obendrauf ist
# damit unerreichbar - genau der Fall, den der Nutzer an Item 1480 gezeigt hat.
#
# Statt solche Ablagen einzeln auszuschliessen, gibt ein Raum der Gruppe die
# Bodenhoehe vor, und Schluessel duerfen nicht mehr als MaxYAbove darueber
# liegen. Das erspart sehr viel Handarbeit und traegt sofort fuer die ganze
# Gruppe.
#
# Y-KONVENTION: in TR liegt KLEINERES y HOEHER. "Hoechstens 1024 hoeher" ist
# also floorY >= baselineY - 1024, nicht <=.
#
# RequireTypes ist zusaetzlich eine Zusicherung, keine blosse Filterung: von
# jedem dieser Typen muss mindestens eine Instanz INNERHALB der Gruppe liegen,
# damit sie aus sich heraus loesbar bleibt.
#----------------------------------------------------------------------------
# Typen, die in den Pool sollen, obwohl kein Schloss sie erzwingt - und die
# dabei KEINE Zonengrenze haben. joby2: die Fuse (Puzzle Item1) hinter den Tueren
# 105/106. Sie ist Fortschritt, aber keine Sperre fuer irgendetwas Frueheres,
# deshalb darf sie ueberall liegen. Nutzerentscheidung.
# Ohne Eintrag taucht so ein Typ gar nicht in den keyConstraints auf und bliebe
# damit im Testlevel unsichtbar und im Randomizer unangetastet.
#----------------------------------------------------------------------------
# Raeume, in denen keine Waffe liegen darf.
#
# joby3: die Pistolen muessen in Raum 107 bleiben, sonst tritt der Chef-Glitch
# auf - das erledigt VANILLA_TYPES. Es reicht aber nicht: VANILLA_TYPES nagelt
# einen Typ an seinen Platz, hindert aber keinen ANDEREN Typ daran, dorthin zu
# wandern. Eine zufaellig im Chef-Bereich platzierte Schrotflinte loeste denselben
# Glitch aus. Nutzerangabe fuer die Chef-Raeume: 105, 106, 120, 124, 125.
#
# Gesperrt wird 334-348, also Waffen und ihre Munition. Medipacks (349/350),
# Lasersight (351), Fernglas (352), Flares (354/355) bleiben erlaubt - keine
# Waffen. Typ 353 "Silencer" steht bewusst nicht in der Liste: er kommt in
# keinem der 16 Level vor, ist also ein ungenutzter Engine-Slot.
#----------------------------------------------------------------------------
#----------------------------------------------------------------------------
# Levelweite Hoehengrenze fuer Ablagen.
#
# joby4 ist ein Tauchlevel, und der Nutzer hat im Spiel gemessen: Lara kommt
# nirgends hoeher als etwa y 13200. Das ist keine Engine-Grenze - der Code kennt
# fuer vertikalen Zwang nur die Sinks (laraswim.cpp:721, sie ziehen Lara auf
# ihre eigene Hoehe) - sondern die Wasseroberflaeche. Oberhalb beginnt bei
# yBottom 13056 die naechste Raumschicht, in die sie vom Wasser aus nicht mehr
# aufsteigt: 21 Raeume dort, weitere 27 darueber.
#
# Anders als die Inselgruppe in joby2 ist das keine Eigenschaft einer Gruppe und
# gilt fuer ALLE Typen, nicht nur fuer Schluessel. Und anders als zuerst
# angenommen ist es nicht raumsauber - Raum 72 etwa hat Boeden bei 11008, 12032
# und 12544, liegt also teils ueber und teils unter der Linie. Eine Raumliste
# kann das nicht abbilden, deshalb hier als Zahl.
#
# KLEINERES y liegt HOEHER: die Bedingung ist floorY >= Grenze.
#----------------------------------------------------------------------------
$MIN_FLOOR_Y = @{
    'joby4' = 13312
}
$levelMinFloorY = $null
if ($MIN_FLOOR_Y.ContainsKey($LevelName)) { $levelMinFloorY = [int]$MIN_FLOOR_Y[$LevelName] }

#----------------------------------------------------------------------------
# Mindestlichte zwischen Boden und Decke.
#
# Der Exporter schneidet von sich aus bei 512 ab - kein Level hat eine Ablage
# darunter. Der Nutzer hat in joby4 Raum 123 Sektor (3,3) geprueft: floorY
# 15616, ceilingY 15104, also genau 512, und Lara kommt dort nicht hin. 768 ist
# die Kriechhoehe der Engine und damit die naechste sinnvolle Stufe.
#
# BEWUSST NICHT GLOBAL. Die Gegenprobe ueber alle Level findet 25 Vanilla-
# Pickups auf Sektoren mit clearance < 768 - darunter joby3s Key Item6 in Raum
# 120 bei 256 und in rich2 ein HK bei 0. Grund: clearance beschreibt den SEKTOR,
# nicht das Item. Ein Pickup hat seine eigene y-Koordinate und kann auf einem
# Sims, in einer Nische oder auf einem Sockel liegen, wo der Sektorboden an die
# Decke stoesst. Die Lichte ist also kein verlaesslicher Stellvertreter fuer
# "Lara kommt da hin".
#
# joby4 kommt in dieser Trefferliste NICHT vor - dort liegt kein Vanilla-Pickup
# unter 768. Nur deshalb steht es hier. Jedes weitere Level braucht denselben
# Nachweis, und fuer joby3, joby5, rich2 und rich3 scheitert er nachweislich.
#----------------------------------------------------------------------------
$MIN_CLEARANCE = @{
    'joby4' = 768
}
$levelMinClearance = $null
if ($MIN_CLEARANCE.ContainsKey($LevelName)) { $levelMinClearance = [int]$MIN_CLEARANCE[$LevelName] }

#----------------------------------------------------------------------------
# Raeume, in denen kein Schluessel- oder Puzzle-Item liegen darf.
#
# joby5 hat elektrifiziertes Wasser. Vier Electrical Cables (Typ 98, Items 91,
# 136, 165, 206) haengen ins Wasser; drei Bodentrigger in den Raeumen 15, 60
# und 138 schalten sie scharf, der Schalter 190 in Raum 165 schaltet sie wieder
# aus. Solange sie aktiv sind, kostet der Aufenthalt 10 Trefferpunkte pro Frame
# und toetet bei <= 32 (game/joby.cpp, ControlElectricalCables).
#
# Die Reichweite ist dabei nicht der Raum des Kabels - massgeblich ist die
# FlipNumber der beruehrten Wassermasse, siehe joby.cpp:191. Welche Raeume das
# sind, steht deshalb nicht in den Leveldaten; die Zuordnung kommt vom Nutzer:
# Kabel 91 -> Raum 128, Kabel 136 -> 126 und 127, Kabel 206 -> 183,
# Kabel 165 -> 158.
#
# Nutzerentscheidung: dort duerfen NORMALE Items liegen, aber keine Schluessel.
# Der Grund ist einleuchtend - ein normales Pickup holt Lara nach dem Schalter,
# ein Schluessel dagegen kann Teil einer Kette sein, die vorher gebraucht wird.
#----------------------------------------------------------------------------
$NO_KEY_ROOMS = @{
    'joby5' = @(126,127,128,158,183)
}
$noKeyRooms = @( if($NO_KEY_ROOMS.ContainsKey($LevelName)){ $NO_KEY_ROOMS[$LevelName] } )

$WEAPON_TYPES = 334..348
$NO_WEAPON_ROOMS = @{
    'joby3' = @(105,106,120,124,125)
}
$noWeaponRooms = @( if($NO_WEAPON_ROOMS.ContainsKey($LevelName)){ $NO_WEAPON_ROOMS[$LevelName] } )

$POOL_TYPES = @{
    'joby2' = @(172)
    # joby3: die vier Combo-Haelften. Puzzle Item3 Combo1+2 ergeben das
    # Atemgeraet, Puzzle Item4 Combo1+2 die Batterie; beides wandert in den
    # Anzug und loest die Schluss-Cutscene aus. Die beiden Slots in Raum 40
    # liegen uebereinander und taeuschen eine Kombinierbarkeit nur vor.
    # Nutzer: das Level hat keinen Point of no return, die Teile sind rein
    # fuers Ende - sie duerfen deshalb ueberall auftauchen.
    'joby3' = @(184,185,186,187)
    # joby5: Puzzle Item1 und Item2 oeffnen das Levelende. Slot 1 oeffnet Slot 2,
    # Slot 2 loest das Ende aus. Das erklaert auch, warum der Trigger an Puzzle
    # Hole2 (Item 108) leer aussieht - er hat kein Object-Kommando, weil er das
    # Level beendet statt eine Tuer zu oeffnen. Beide duerfen laut Nutzer
    # ueberall liegen, ohne jede Einschraenkung.
    'joby5' = @(172,173)
}
$poolTypes = @( if($POOL_TYPES.ContainsKey($LevelName)){ $POOL_TYPES[$LevelName] } )

$ISLAND_GROUP = @{
    # DeadstopRooms definiert die Insel ueber einen ZWEITEN, schaerferen Flood.
    # Die Gleisbett-Kette 2/4/5 und Raum 89 sind darin gesperrt, ihre
    # Flippartner 43 und 53 werden abgeleitet. Was dann noch ab Laras Start
    # erreichbar ist, IST die Startinsel - dort und nur dort darf Key8 liegen.
    # Ausdruecklich nicht im Hauptlauf: das Gleisbett ist zugleich der legitime
    # Hauptweg, und mit dieser Sperre waere Key Item7 in Raum 52 unerreichbar.
    'joby2' = @( @{ BaselineRoom = 84; MaxYAbove = 1024; RequireTypes = @(202,203)
                    IslandTypes = @(203); DeadstopRooms = @(2,4,5,89) } )
}
$islandGroups = @( if($ISLAND_GROUP.ContainsKey($LevelName)){ $ISLAND_GROUP[$LevelName] } )

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
    # joby2: der Weg auf den vorderen Sims von Raum 11, wo Lasersight und
    # Revolver Ammo liegen. Zweimal derselbe Sprung mit Anlauf ueber eine Luecke
    # von genau drei Sektoren - erst innerhalb von Raum 152, dann innerhalb von
    # Raum 11 ueber die bodenlosen Felder x6/x7/x8 hinweg.
    # Nutzerregel dahinter: Lara ueberwindet mit Anlauf 3 x 1024, sofern beide
    # Kanten denselben Y-Wert haben. Hier erfuellt, alles liegt auf -4608.
    'joby2' = @(
        @('152,10,13', '152,10,17')
        @('11,9,1',    '11,5,1')
    )
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
    if ($excludeSectors -contains $key) { return $false }
    if ($bridgeSec.ContainsKey($key)) { return $true }
    return (-not $sec[$key].isWall)
}

$adj = @{}
function Add-Edge($a, $b) {
    # Handgesperrte Einzelsektoren hier abfangen und nicht in Test-Walkable
    # allein: Portal-, Bruecken- und Manuallink-Kanten rufen Test-Walkable gar
    # nicht auf, ein Sektor kaeme also ueber sie doch in den Graphen.
    if ($excludeSectors -contains $a -or $excludeSectors -contains $b) { return }
    # Dasselbe fuer ganze Raeume. Raum 145 in joby2 stand in EXCLUDE_ROOMS und
    # tauchte trotzdem mit 36 Ablagen auf: er enthaelt vier Bridge-Flats, und
    # Brueckenkanten holen ihre Sektoren ohne Test-Walkable in den Graphen.
    if ($excludeRooms -contains [int](($a -split ',')[0])) { return }
    if ($excludeRooms -contains [int](($b -split ',')[0])) { return }
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

#----------------------------------------------------------------------------
# Dekotueren.
#
# Eine Tuer geht in TR erst auf, wenn ihre Maske vollstaendig ist - die Engine
# prueft (flags & IFL_CODEBITS) == CODEBITS, also alle fuenf Bits. Findet sich im
# ganzen Level kein Trigger, der eine Tuer auf 11111 bringen kann, dann geht sie
# nie auf. Da Vanilla durchspielbar ist, kann so eine Tuer nichts versperren, was
# gebraucht wird: sie ist Deko. Nutzer zu joby2 Raum 11: "Hinter den Tueren ist
# nichts sind Dekotueren."
#
# Vorher sperrten sie fuer immer, und dahinter lagen echte Pickups (Lasersight
# und Revolver Ammo), die damit nie in den Pool kamen.
#
# Gezaehlt wird ueber ALLE Trigger des Levels, auch die in der Floordata von
# Flipraeumen - die existieren erst nach dem Flip, zaehlen aber sehr wohl
# (Nutzerhinweis). Drei Ausnahmen bleiben Tueren: Skript-Tueren, Cutscene-Tueren
# und Brechstangentueren haben ebenfalls keinen Trigger, gehen aber trotzdem auf.
#----------------------------------------------------------------------------
$doorMaskTotal = @{}
foreach($tr in $d.triggers){
    foreach($c in $tr.commands){
        if ($c.type -ne 'Object') { continue }
        $di = [int]$c.index
        if (-not $doorSector.ContainsKey($di)) { continue }
        if (-not $doorMaskTotal.ContainsKey($di)) { $doorMaskTotal[$di] = 0 }
        $doorMaskTotal[$di] = $doorMaskTotal[$di] -bor [int]$tr.mask
    }
}
$decoDoors = @()
foreach($kv in $doorSector.GetEnumerator()){
    $di = [int]$kv.Key
    if ($scriptDoors.ContainsKey($di)) { continue }
    if ($cutsceneDoors.ContainsKey($di)) { continue }
    if ($null -ne $crowbarDoors -and $crowbarDoors -contains $di) { continue }
    $m = if($doorMaskTotal.ContainsKey($di)){ [int]$doorMaskTotal[$di] } else { 0 }
    if (($m -band 31) -eq 31) { continue }
    $decoDoors += ,$di
}
foreach($di in $decoDoors){ [void]$blocked.Remove($doorSector[$di]) }
if ($decoDoors.Count -gt 0) {
    Write-Output "Dekotueren: $($decoDoors.Count) Tueren bringt kein Trigger auf 11111 - sie sperren nicht"
    Write-Output "  Items: $(($decoDoors | Sort-Object) -join ', ')"
}

# Flipvarianten bestimmen: das Feld alternateRoom im Raumkopf zeigt von der Basis
# auf ihre Flipfassung, die Zielraeume sind also genau die Varianten.
$flipVariantRooms = @{}
foreach($r in $d.rooms){
    $ar = "$($r.alternateRoom)"
    if ($ar -eq '' -or $ar -eq '65535') { continue }
    $flipVariantRooms[[int]$ar] = [int]$r.index
}
# Eine Flipvariante darf keinen Weg schaffen, den die Basis nicht auch hat.
#
# Ein pauschales Sperren aller Flipvarianten waere zwar sicher, ist aber viel zu
# grob: gemessen ueber alle Level tragen sie ueberwiegend voellig legitime Wege
# (rich3 stuerzte damit von 4 auf 2098 unerreichte Sektoren ab). Die meisten
# Flipmaps aendern die Verbindungen naemlich gar nicht - Wasserstand, Trueb-
# mmer, Beleuchtung -, und ihre Sektoren sind Alias derselben Stelle.
#
# Gefaehrlich ist nur der andere Fall: die Flipvariante OEFFNET etwas, das in der
# Basis zu ist - das Rolltor am Kran in joby2. Genau den trifft diese Regel: ein
# Flipsektor traegt nur, wenn der Sektor gleicher Position im Basisraum ebenfalls
# begehbar ist. Kosmetische Flips bleiben unberuehrt, neue Durchgaenge nicht.
$flipNeedsGate = @{}   # sectorKey der Variante -> Nummer des Tors (-1 = keins)
foreach($s0 in $d.sectors){
    $rn = [int]$s0.room
    if (-not $flipVariantRooms.ContainsKey($rn)) { continue }
    if ($s0.isWall) { continue }
    $baseKey = "$($flipVariantRooms[$rn]),$($s0.sectorX),$($s0.sectorZ)"
    # Basis an derselben Stelle ebenfalls begehbar? Dann ist der Flipsektor nur
    # eine zweite Schreibweise desselben Weges und braucht keine Freigabe.
    if ($sec.ContainsKey($baseKey) -and -not $sec[$baseKey].isWall) { continue }
    $g = -1
    for($gi = 0; $gi -lt $flipGates.Count; $gi++){ if ($flipGates[$gi].Rooms -contains $rn) { $g = $gi; break } }
    $flipNeedsGate["$rn,$($s0.sectorX),$($s0.sectorZ)"] = $g
}
# BlockRooms: ganz normale Raeume hinter dasselbe Tor haengen.
#
# Nicht jede Flipmap laesst sich an ihren Variantensektoren fassen. In joby2 ist
# das Rolltor am Kran echte Wandgeometrie, und die Flut umgeht es ohnehin - der
# Erstzugang zu Raum 89 laeuft ueber 80 und 4, ohne einen einzigen Flipsektor zu
# beruehren. Der Blocker liegt also nicht dort, wo die Variante etwas oeffnet.
#
# Deshalb auf Nutzervorschlag der direkte Weg: das Rolltor als Deadstop setzen
# und die Raeume dahinter als eigene Inselgruppe fuehren. Damit faellt alles
# Weitere von selbst heraus - die Zonenlogik zwingt Key Item8 in die Startinsel,
# weil hinter dem Deadstop eine spaetere Zone beginnt.
foreach($gi0 in 0..([Math]::Max(0,$flipGates.Count-1))){
    if ($flipGates.Count -eq 0) { break }
    if ($null -eq $flipGates[$gi0].BlockRooms) { continue }
    foreach($s0 in $d.sectors){
        if ($s0.isWall) { continue }
        if ($flipGates[$gi0].BlockRooms -notcontains [int]$s0.room) { continue }
        $flipNeedsGate["$($s0.room),$($s0.sectorX),$($s0.sectorZ)"] = $gi0
    }
}
$flipGateOf = $flipNeedsGate
$openGates = New-Object System.Collections.Generic.HashSet[int]
if ($flipGateOf.Count -gt 0) {
    $noGate = @($flipGateOf.GetEnumerator() | Where-Object { $_.Value -lt 0 })
    $rr = @($noGate | ForEach-Object { [int](($_.Key -split ',')[0]) } | Sort-Object -Unique)
    Write-Output "Flipmap: $($flipGateOf.Count) Sektoren oeffnen etwas, das die Basis nicht hat - $($noGate.Count) davon ohne Freigabe"
    if ($rr.Count -gt 0) { Write-Output "  betroffene Flipraeume: $($rr -join ', ')" }
}

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

# Welche Tueren stehen schon zu Levelbeginn offen (IFL_CODEBITS)? Wird fuer den
# Insel-Flood weiter unten gebraucht, der genau den Zustand vor der ersten
# geoeffneten Tuer abbildet - also Zone 0.
$doorsOpenAtStart = New-Object System.Collections.Generic.HashSet[int]
foreach($x in $openDoors){ [void]$doorsOpenAtStart.Add([int]$x) }

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
            # Sektor einer Flipvariante: traegt erst, wenn sein Tor freigegeben
            # ist - ohne deklariertes Tor nie.
            if ($flipGateOf.ContainsKey($n) -and -not $openGates.Contains($flipGateOf[$n])) { continue }
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

    #------------------------------------------------------------------------
    # Reihenfolge der Schloesser, wenn ein Schluesseltyp knapp ist.
    #
    # Die Schleife lief bisher in Arrayreihenfolge, ein Schluessel wurde also an
    # dem Schloss verbraucht, das die Flut zufaellig zuerst erreichte. Diese Wahl
    # kann falsch sein und das halbe Level unerreichbar lassen.
    #
    # joby2 ist der belegte Fall: Key Item7 existiert zweimal, aber nur eine
    # Instanz ist anfangs erreichbar. Verbraucht das Modell sie an Tuer 41, ist
    # Schluss - Tuer 134 bleibt zu, dahinter liegt Key8, und ohne den geht nichts
    # weiter. Richtig ist die umgekehrte Wahl: Tuer 134 zuerst, sie legt Key8
    # frei, damit oeffnen sich 105/106, dahinter der zweite Key7, und DER oeffnet
    # dann Tuer 41. Die Reihenfolge entscheidet ueber das halbe Level.
    #
    # Regel: ist ein Typ knapp (weniger verfuegbare Instanzen als offene
    # Schloesser, die ihn brauchen), kommen zuerst die Schloesser dran, hinter
    # denen ein noch unerreichter Schluessel liegt. Das ist die Standardheuristik
    # fuer Schluessel-Schloss-Erreichbarkeit und hier nachweislich richtig.
    # Bewusst NICHT ins Gegenteil gekippt und einfach alle Schloesser geoeffnet -
    # das waere optimistisch und wuerde unspielbare Seeds erzeugen.
    #------------------------------------------------------------------------
    # Bedarf je Typ unter den noch offenen Schloessern.
    $trigOrder = @($allTrig)
    $demand = @{}
    foreach($e in $allTrig){
        if ($firedTrig.Contains($e.Id)) { continue }
        if ($e.Req.Kind -ne 'item' -or $null -eq $e.Req.NeedItemType) { continue }
        $nt = [int]$e.Req.NeedItemType
        if ($reusableKeys -contains $nt) { continue }   # Key8: unbegrenzt, nie knapp
        if (-not $demand.ContainsKey($nt)) { $demand[$nt] = 0 }
        $demand[$nt]++
    }
    $scarce = @{}
    foreach($kv in $demand.GetEnumerator()){
        $nt = [int]$kv.Key
        $hv = if($haveCounts.ContainsKey($nt)){ [int]$haveCounts[$nt] } else { 0 }
        $cs = if($consumed.ContainsKey($nt)){ [int]$consumed[$nt] } else { 0 }
        if ([int]$kv.Value -gt ($hv - $cs)) { $scarce[$nt] = $true }
    }

    if ($scarce.Count -gt 0) {
        # Legt dieses Schloss einen noch unerreichten Schluessel frei? Dazu ab dem
        # Sektor der Zieltuer fluten - andere geschlossene Tueren bleiben Sperre -
        # und nachsehen, ob ein noch unerreichtes Key-/Puzzle-Item darin liegt.
        $ord = -1
        $ordered = foreach($e in $allTrig){
            $ord++
            $rank = 1
            if ($e.Req.Kind -eq 'item' -and $null -ne $e.Req.NeedItemType -and
                $scarce.ContainsKey([int]$e.Req.NeedItemType)) {
                $rank = 2
                $seeds = @()
                foreach($c in $e.T.commands){
                    if ($c.type -eq 'Object' -and $doorSector.ContainsKey([int]$c.index)) {
                        $seeds += ,$doorSector[[int]$c.index]
                    }
                }
                if ($seeds.Count -gt 0) {
                    $seen = New-Object System.Collections.Generic.HashSet[string]
                    $qq = New-Object System.Collections.Generic.Queue[string]
                    foreach($sd in $seeds){ if($seen.Add($sd)){ $qq.Enqueue($sd) } }
                    while($qq.Count -gt 0 -and $seen.Count -lt 4000){
                        $k = $qq.Dequeue()
                        if (-not $adj.ContainsKey($k)) { continue }
                        foreach($nb in $adj[$k]){
                            if ($seen.Contains($nb) -or $reached.Contains($nb)) { continue }
                            if ($blocked.ContainsKey($nb) -and $seeds -notcontains $nb) { continue }
                            [void]$seen.Add($nb); $qq.Enqueue($nb)
                        }
                    }
                    foreach($it in $d.items){
                        $ti = [int]$it.typeId
                        if ($ti -lt $KEYPUZZ_LO -or $ti -gt $KEYPUZZ_HI) { continue }
                        $ik = "$($it.room),$($it.sectorX),$($it.sectorZ)"
                        if ($reached.Contains($ik)) { continue }
                        if ($seen.Contains($ik)) { $rank = 0; break }
                    }
                }
            }
            [PSCustomObject]@{ Rank = $rank; Ord = $ord; E = $e }
        }
        # Ord als zweites Sortierkriterium: Windows PowerShell 5.1 kennt kein
        # -Stable, ohne den Index waere die Reihenfolge gleichrangiger Trigger
        # nicht reproduzierbar - und Reproduzierbarkeit ist hier Bedingung.
        $trigOrder = @($ordered | Sort-Object Rank,Ord | ForEach-Object { $_.E })
    }

    # Erreichbare Trigger ausloesen bzw. Slots befuellen. Ein eingesetztes Item
    # ist verbraucht und steht fuer keinen zweiten Slot mehr zur Verfuegung.
    foreach($e in $trigOrder){
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

    # Flipmap-Tore: geben ihre Sektoren frei, sobald der ausloesende Raum erreicht
    # ist - dieselbe Bedingungsform wie die Skript-Tueren.
    $newlyOpenedGates = @()
    for($gi = 0; $gi -lt $flipGates.Count; $gi++){
        if ($openGates.Contains($gi)) { continue }
        $hit = $true
        # Bedingung "Raum erreicht" - wie bei den Skript-Tueren.
        if ($null -ne $flipGates[$gi].AfterRoom) {
            $needRoom = [int]$flipGates[$gi].AfterRoom
            $seenRoom = $false
            foreach($k in $reached){ if ([int](($k -split ',')[0]) -eq $needRoom) { $seenRoom = $true; break } }
            if (-not $seenRoom) { $hit = $false }
        }
        # Bedingung "Items in der Hand". Manche Flipmaps haengen nicht an einem
        # Ort, sondern an Ausruestung: andy1 laesst die Glocken erst fallen, wenn
        # Lara die Zwille aus ihren beiden Combo-Haelften gebaut hat. Nur den
        # Kirchenraum abzufragen wuerde das Tor zu frueh oeffnen - Lara steht dort
        # laengst, bevor sie die Teile hat.
        if ($null -ne $flipGates[$gi].NeedTypes) {
            foreach($nt in $flipGates[$gi].NeedTypes){
                if (-not $haveCounts.ContainsKey([int]$nt)) { $hit = $false; break }
            }
        }
        if ($hit) { $newlyOpenedGates += ,$gi }
    }

    if ($newlyOpened.Count -eq 0 -and $newlyOpenedGates.Count -eq 0) { break }
    foreach($n in $newlyOpened){
        [void]$openDoors.Add($n.Door)
        $log += ,@{ Zone=$zone; Door=$n.Door; Req=$n.Req }
    }
    foreach($gi in $newlyOpenedGates){
        [void]$openGates.Add($gi)
        $why = if($flipGates[$gi].Why){ $flipGates[$gi].Why } else { 'Flipmap' }
        $cond = @()
        if ($null -ne $flipGates[$gi].AfterRoom) { $cond += "Raum $($flipGates[$gi].AfterRoom) erreicht" }
        if ($null -ne $flipGates[$gi].NeedTypes) { $cond += "Typen $($flipGates[$gi].NeedTypes -join '+') vorhanden" }
        Write-Output "Zone $zone -> $($zone+1): Flipmap-Tor $($flipGates[$gi].Rooms -join '/') frei - $why ($($cond -join ', '))"
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
#----------------------------------------------------------------------------
# Insel-Flood: ein zweiter, schaerferer Durchlauf, der NUR die Gruppe bestimmt.
#
# Er laeuft ab Laras Start, mit allen Tueren geschlossen (ausser den von Haus aus
# offenen) und zusaetzlich mit den Deadstop-Raeumen der Gruppe gesperrt. Was
# uebrigbleibt, ist die Startinsel.
#
# Warum getrennt und nicht im Hauptlauf: in joby2 ist das Gleisbett beides -
# der Schleichweg unter dem Rolltor hindurch UND der legitime Hauptweg. Sperrt
# man es im Hauptlauf, steigt "nie erreicht" von 463 auf 6751 und Key Item7 in
# Raum 52 wird unerreichbar, Vanilla faellt also durch. Der Hauptlauf braucht
# den Weg, die Insel darf ihn nicht haben. Also zwei Laeufe.
#----------------------------------------------------------------------------
$islandSectors = @{}   # typeId -> HashSet der erlaubten Sektoren
$islandHeightScope = @{}   # typeId -> HashSet, in dem die Hoehengrenze gilt
foreach($ig in $islandGroups){
    if ($null -eq $ig.IslandTypes -or $null -eq $ig.DeadstopRooms) { continue }
    $stopR = @($ig.DeadstopRooms | ForEach-Object { [int]$_ })
    foreach($b in @($stopR)){
        if ($flipVariantRooms.ContainsKey($b)) { $stopR += [int]$flipVariantRooms[$b] }
        foreach($kvv in $flipVariantRooms.GetEnumerator()){ if ([int]$kvv.Value -eq $b) { $stopR += [int]$kvv.Key } }
    }
    $stopR = @($stopR | Sort-Object -Unique)

    $isl = New-Object System.Collections.Generic.HashSet[string]
    if ($null -ne $startKey) {
        $qi = New-Object System.Collections.Generic.Queue[string]
        [void]$isl.Add($startKey); $qi.Enqueue($startKey)
        while($qi.Count -gt 0){
            $k = $qi.Dequeue()
            if (-not $adj.ContainsKey($k)) { continue }
            foreach($n in $adj[$k]){
                if ($isl.Contains($n)) { continue }
                if ($stopR -contains [int](($n -split ',')[0])) { continue }
                if ($blocked.ContainsKey($n) -and -not $doorsOpenAtStart.Contains([int]$blocked[$n])) { continue }
                if ($flipGateOf.ContainsKey($n)) { continue }
                [void]$isl.Add($n); $qi.Enqueue($n)
            }
        }
    }
    $islRooms = @($isl | ForEach-Object { [int](($_ -split ',')[0]) } | Sort-Object -Unique)
    Write-Output "Inselgruppe (Deadstops $($stopR -join ', ')): $($isl.Count) Sektoren in $($islRooms.Count) Raeumen"
    foreach($it in $ig.IslandTypes){ $islandSectors[[int]$it] = $isl }
    # Die Hoehengrenze ist eine Eigenschaft der GRUPPE, nicht der Zone. Sie an
    # die Zone zu haengen war zu weit gegriffen: Zone 0 umfasst in joby2 7756
    # Sektoren, die Insel nur 1900 - fuer Key Item7, das ausserhalb der Insel
    # liegen darf, wirkte die Schranke dadurch im halben Level. Deshalb bekommt
    # jeder Typ der Gruppe die Inselmenge als Geltungsbereich, auch die, die
    # nicht auf die Insel beschraenkt sind.
    foreach($rt in $ig.RequireTypes){ $islandHeightScope[[int]$rt] = $isl }
}

# Hoehengrenze je Schluesseltyp aus den Inselgruppen ableiten. Baseline ist der
# haeufigste Bodenwert der begehbaren Sektoren des Baseline-Raums - das Mittel
# einer einzelnen Ecke waere zu zufaellig, der Modus trifft den eigentlichen
# Hallenboden.
$keyMinFloorY = @{}
foreach($ig in $islandGroups){
    $bs = @($d.sectors | Where-Object { [int]$_.room -eq [int]$ig.BaselineRoom -and -not $_.isWall -and $_.hasFloor })
    if ($bs.Count -eq 0) { Write-Output "WARNUNG: Inselgruppe - Baseline-Raum $($ig.BaselineRoom) hat keine begehbaren Sektoren"; continue }
    $baseY = [int](($bs | Group-Object floorY | Sort-Object Count -Descending | Select-Object -First 1).Name)
    $limit = $baseY - [int]$ig.MaxYAbove
    # Die Grenze gilt NUR in der Zone, in der die Gruppe liegt - sonst trifft sie
    # das halbe Level mit. Die Zone ergibt sich aus dem Baseline-Raum selbst,
    # muss also nicht zusaetzlich gepflegt werden.
    $bz = @()
    foreach($b in $bs){ $bk = "$($b.room),$($b.sectorX),$($b.sectorZ)"; if ($zoneOf.ContainsKey($bk)) { $bz += ,[int]$zoneOf[$bk] } }
    if ($bz.Count -eq 0) { Write-Output "WARNUNG: Inselgruppe - Baseline-Raum $($ig.BaselineRoom) wird nie erreicht"; continue }
    $gz = ($bz | Sort-Object | Select-Object -First 1)
    foreach($rt in $ig.RequireTypes){ $keyMinFloorY[[int]$rt] = @{ Limit = $limit; Zone = $gz } }
    Write-Output "Inselgruppe: Baseline Raum $($ig.BaselineRoom) y=$baseY in Zone $gz, Schluessel dort hoechstens $($ig.MaxYAbove) darueber (floorY >= $limit) fuer Typen $($ig.RequireTypes -join ', ')"
}

# HINWEIS: Hier stand die abgeleitete Regel "ein Schluessel darf nie hinter
# einer Tuer liegen, die er selbst oeffnet" (Commit 3af3f46). Sie ist wieder
# entfernt worden, weil sie nachweislich zu grob war: in Andrea3 sperrte sie
# die VANILLA-Position von Key Item2 (Item 101, Raum 119) und schrumpfte
# dessen erlaubte Raeume von 32 auf 6. Ursache war vermutlich, dass sie je
# Schluesseltyp die Tueren ALLER Schloesser dieses Typs vereinigte statt sie
# einzeln zu betrachten - der Bericht zeigte fuer Typ 197 die Tueren
# 25/24/77/94 aus mehreren Schloessern zusammengeworfen.
#
# Der Maszstab bleibt gueltig und ist die Abnahmebedingung fuer jeden neuen
# Versuch: eine Regel, die verbietet, wo Vanilla das Item hinlegt, ist
# konstruktionsbedingt falsch. Siehe Aufgabe #49.


$keyConstraints = @()
foreach($kv in $byType.GetEnumerator()){
    $nt = [int]$kv.Key
    # Bleibt Vanilla: Anforderung oben im Report sichtbar, aber nicht im Pool.
    if ($vanillaTypes -contains $nt) { continue }
    $maxZ = ($kv.Value | ForEach-Object { [int]$_.Zone } | Measure-Object -Maximum).Maximum
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $noPlacement + $noKeyRooms | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=$maxZ; instances=$kv.Value.Count; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms; minFloorY=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Limit}else{$null}); minFloorYZone=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Zone}else{$null}); islandSectors=$(if($islandSectors.ContainsKey($nt)){@($islandSectors[$nt])}else{$null}); minFloorYIsland=$(if($islandHeightScope.ContainsKey($nt)){@($islandHeightScope[$nt])}else{$null}) }
}
# Handgesetzte Zusatztypen anhaengen, sofern das Modell sie nicht ohnehin schon
# selbst abgeleitet hat (dann gilt die abgeleitete Grenze, nicht die Handangabe).
foreach($kv in $extraPool.GetEnumerator()){
    $nt = [int]$kv.Key
    if ($byType.ContainsKey($nt)) { continue }
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $noPlacement + $noKeyRooms | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=[int]$kv.Value; instances=0; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms; minFloorY=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Limit}else{$null}); minFloorYZone=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Zone}else{$null}); islandSectors=$(if($islandSectors.ContainsKey($nt)){@($islandSectors[$nt])}else{$null}); minFloorYIsland=$(if($islandHeightScope.ContainsKey($nt)){@($islandHeightScope[$nt])}else{$null}) }
}

if ($ExportZonesJson -ne "") {
    # Maschinenlesbarer Export: je Sektor, der sowohl im Flood erreicht als auch
    # laut Exporter tatsaechlich itemfaehig ist (validForItems), seine Zone.
    # Nur validForItems-Sektoren, damit ein nachgelagerter Patch-Generator nie
    # ein Item auf eine Wand, ein Portal oder einen sonst ungeeigneten Sektor
    # setzt - dieselbe PrÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¼fung, die schon die *_AllValidSectorsFilled.trc-Dateien
    # sauber gehalten hat, bleibt hier die Grundlage.
    $rows = @()
    foreach($kv in $zoneOf.GetEnumerator()){
        $s = $sec[$kv.Key]
        if (-not $s -or -not $s.validForItems) { continue }
        if ($null -ne $levelMinFloorY -and [int]$s.floorY -lt $levelMinFloorY) { continue }
        if ($null -ne $levelMinClearance -and [int]$s.clearance -lt $levelMinClearance) { continue }
        $rows += [PSCustomObject]@{ room=[int]$s.room; sectorX=[int]$s.sectorX; sectorZ=[int]$s.sectorZ; zone=[int]$kv.Value; floorY=[int]$s.floorY }
    }
    $maxZoneOut = if($rows.Count -gt 0){ ($rows | Measure-Object -Property zone -Maximum).Maximum } else { 0 }
    # Pool-Typen ohne Zonengrenze anhaengen. maxZone bekommt den hoechsten im
    # Level vorkommenden Wert, damit wirklich jede Zone offensteht; Insel- und
    # Hoehengrenzen bleiben leer, denn diese Typen gehoeren keiner Gruppe an.
    foreach($pt in $poolTypes){
        $pt = [int]$pt
        if (@($keyConstraints | Where-Object { [int]$_.typeId -eq $pt }).Count -gt 0) { continue }
        $inst = @($d.items | Where-Object { [int]$_.typeId -eq $pt }).Count
        # NO_KEY_ROOMS gilt hier ausdruecklich NICHT. Pool-Typen haben keine
        # Kette im Level - joby5s Puzzle 1 und 2 werden erst ganz am Ende
        # gebraucht, und da ist der Strom laengst abgeschaltet. Nutzer: "Die
        # Puzzels koennen wir lassen, nur Keys sind wichtig."
        $keyConstraints += [PSCustomObject]@{ typeId=$pt; maxZone=$maxZoneOut; instances=$inst
            reusable=($reusableKeys -contains $pt); excludeRooms=@(); allowRooms=@()
            minFloorY=$null; minFloorYZone=$null; islandSectors=$null; minFloorYIsland=$null }
        Write-Output "Pool-Typ $pt ohne Zonengrenze aufgenommen ($inst Instanzen im Level, maxZone=$maxZoneOut)"
    }
    [PSCustomObject]@{ level=$LevelName; maxZone=$maxZoneOut; sectors=$rows; keyConstraints=$keyConstraints; fixedItems=$fixedItems; requiredInjections=$requiredInjections; noWeaponRooms=@($noWeaponRooms); weaponTypes=@($WEAPON_TYPES) } | ConvertTo-Json -Depth 4 -Compress | Out-File $ExportZonesJson -Encoding utf8
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
# system beliebig; erst die Portale nÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¾ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¾ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¾Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€šÃ‚Â¦ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬ÃƒÂ¢Ã¢â‚¬Å¾Ã‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã‚Â¦ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã¢â‚¬Â ÃƒÂ¢Ã¢â€šÂ¬Ã¢â€žÂ¢ÃƒÆ’Ã†â€™Ãƒâ€šÃ‚Â¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡Ãƒâ€šÃ‚Â¬ÃƒÆ’Ã¢â‚¬Â¦Ãƒâ€šÃ‚Â¡ÃƒÆ’Ã†â€™Ãƒâ€ Ã¢â‚¬â„¢ÃƒÆ’Ã‚Â¢ÃƒÂ¢Ã¢â‚¬Å¡Ã‚Â¬Ãƒâ€¦Ã‚Â¡ÃƒÆ’Ã†â€™ÃƒÂ¢Ã¢â€šÂ¬Ã…Â¡ÃƒÆ’Ã¢â‚¬Å¡Ãƒâ€šÃ‚Â¤hen sie zusammen, wodurch nicht-euklidische
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
    # joby3: vier Nischen fuer die Ventilatoren. Alle vier bauartgleich - 3x4
    # Raum, genau zwei begehbare Sektoren, kein einziges Portal; in Raum 66
    # sitzt sichtbar ein Electrical Light. Vom Nutzer in trview bestaetigt.
    # joby4: Raum 0 liegt ausserhalb der Karte, ein Ueberbleibsel der Devs ohne
    # Funktion - vom Nutzer in trview angesehen. Einer der drei Nicht-Wasser-
    # raeume des Levels, floorY -1024 bis 256 und damit weit ueber der
    # Schwimmgrenze. Er meldete sich als mehrere Ein-Sektor-Luecken.
    # Laut Nutzer sind 0 und 1 der Raum below und above eines Stapels - beide
    # ausserhalb der Karte. Das deckt sich mit den Daten: gleiche Masse 6x15,
    # dieselben 52 Nicht-Wand-Sektoren, null Ablagen, keine Items, beide kein
    # Wasserraum, und Raum 0 hat sein einziges Portal nach Raum 1.
    'joby4' = @{
        0 = 'Out of map, Ueberbleibsel der Devs, ohne Funktion (Raum below)'
        1 = 'Out of map, Ueberbleibsel der Devs, ohne Funktion (Raum above)'
    }
    # joby5, alle vier Luecken vom Nutzer in trview durchgesehen. Raum 104 ist
    # dabei der mit dem Keyhole8, dessen Tuer 103 ohnehin 11111 traegt und ab
    # Levelstart offen steht - das Schloss ist gegenstandslos.
    'joby5' = @{
        11  = 'Ventilatorraum'
        22  = 'Out of map, ohne Bedeutung'
        23  = 'Out of map, ohne Bedeutung'
        87  = 'Kulisse'
        95  = 'Kulisse'
        104 = 'Kulisse (enthaelt das gegenstandslose Keyhole8 zu Tuer 103)'
        105 = 'Ventilatorraum'
        166 = 'Kulisse'
        167 = 'Kulisse'
    }
    'joby3' = @{
        63  = 'Dekoraum fuer den Ventilator'
        66  = 'Dekoraum fuer den Ventilator'
        111 = 'Dekoraum fuer den Ventilator'
        113 = 'Dekoraum fuer den Ventilator'
        # 180 kam erst zum Vorschein, nachdem die 13 Kulissenraeume gesperrt
        # waren. Bauartgleich mit den vier obigen - 3x4, zwei begehbare
        # Sektoren, kein Portal - und wie Raum 66 mit einem Electrical Light
        # darin. Aus der Bauweise geschlossen, nicht vom Nutzer bestaetigt.
        180 = 'Dekoraum fuer den Ventilator (aus der Bauweise geschlossen)'
    }
    'Andrea3' = @{
        13 = 'Antitrigger-Fix der Devs, fuer uns ohne Bedeutung'
        71 = 'Ueberbleibsel der Devs, ohne Funktion'
        72 = 'Ueberbleibsel der Devs, ohne Funktion'
        77 = 'ohne Funktion'
    }
    'joby2' = @{
        # Der Kran, der am Levelende ueber dem U-Boot schwenkt, ist im
        # eigentlichen Level ein Mesh. Diese Raeume existieren nur fuer die
        # Animation - warum das nicht am echten Kran haengt, weiss auch der
        # Nutzer nicht, aber betretbar ist hier nichts.
        49 = 'Kran-Animation fuers Levelende, im Level selbst ein Mesh'
        50 = 'Kran-Animation fuers Levelende, im Level selbst ein Mesh'
        51 = 'Kran-Animation fuers Levelende, im Level selbst ein Mesh'
        0  = 'nur Animatings, keine bespielbare Geometrie'
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
