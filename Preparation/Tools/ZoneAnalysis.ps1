# Progression zone analysis for TR5 levels (prototype)
#
# Idea: flood from Lara's start sector through the sector graph, but stop at
# closed doors -> zone 0. Then check which doors can be opened with what is
# reachable in zone 0 -> unlock, keep flooding -> zone 1. And so on. Result:
# every key item gets "must lie in zone <= N", where N is the zone of its
# door.
#
# Works on the JSON files from TombItemEdit2026 -exportinfo.
#
# The understanding of the TR floordata and trigger format the export rests
# on comes in essential parts from trview by Chreden (MIT license):
#   https://github.com/chreden/trview
#
# Known simplifications (deliberate, not overlooked):
#  - Flipmap rooms are permanently connected with their base room. Whether
#    the corresponding FlipMap trigger has fired is not checked.
#  - Jumping, climbing and jump distances are not modeled; the graph
#    connects walkable neighbor sectors regardless of the height difference.
#  - One-way transitions and equipment gates are hand-curated (ONEWAY_OUT
#    and EQUIP_GATE further down).
param(
    [string]$LevelJson,
    [string]$LevelName = "Level",
    [string[]]$DebugSectors = @(),
    [string]$DebugPathTo = "",
    [string]$ExportZonesJson = "",
    # Maximum climb Lara manages without tools. An engine value, not an
    # estimate: TestLaraVault (game/lara.cpp:1107) handles hdif down to -1920
    # (Vault2/Vault3/high jump) and above that falls into
    # "if (!lara.climb_status) return 0;" - higher only works at climbable
    # walls. The user had judged it exactly right in game ("anything above
    # 2x1024 cannot be done from a standstill" - the Andrea1 wall at 2048
    # really is insurmountable, 1920 is the precise code limit below it).
    [int]$CLIMB_LIMIT = 1920,
    # PHYSICS MODEL, PARKED (2026-08-04): direction-dependent climb rule +
    # jump edges. Three measurement rounds showed: the rule closes real leaks
    # (the Andrea1 wall) but tears multi-story levels apart, because the
    # sector-data semantics do not suffice for it. Core finding from GetFloor
    # (game/control.cpp:1206): the descent through pit_room happens ONLY when
    # y >= floor level - the floor byte is thus real, solid ground EVEN when
    # roomBelow is set (stacked stories, Andrea2 room 211 above 54: both
    # levels walkable). A "standing-height resolution downwards" is therefore
    # fundamentally wrong; a correct model needs the full GetHeight semantics
    # including NoCol triangles. Until then: switch off, leak closing via
    # GATED_AREA curation instead.
    [switch]$EnableClimbModel
)

$ErrorActionPreference = "Stop"

# Before anything is computed: does this level info come from an untouched
# level? A file generated from randomized data looks exactly the same, and
# the zones would then be silently wrong instead of visibly broken.
. (Join-Path $PSScriptRoot "VanillaGate.ps1")
[void](Test-VanillaOrigin -LevelJson $LevelJson)

$d = Get-Content $LevelJson -Raw | ConvertFrom-Json

# --- Type IDs (from RESOURCES/TRCatalog2.xml, TR5 section) -------------------
$DOOR_TYPES = @(284,286,288,290,292,294,296,298,   # Door Type 1-8
                300,302,304,306,308,310,           # Closed Door 1-6
                312,314,316,318,320,322,324,326,328,332,
                98)                                # cables - see below
# Cables (typeId 98) are no door but block just the same: they are live from
# level start and impassable until a wall switch cuts the whole level's
# power. joby5: the jump switch (item 190, room 165) switches all four cables
# (91/136/165/206) AND FlipMap 6 with one trigger. Room 143 is a corridor
# whose sector (1,3) carries exactly such a cable - room 61 lies behind it,
# and only after a visit there does the soldier with Key 6 spawn. The cables
# carry flags=0, so the CODEBITS rule correctly does not treat them as
# "unresolved from start". Taking them into DOOR_TYPES makes the model
# stricter - the safe direction, because a key item behind a live cable
# logically lies later than a permissive model believes.
$PUZZLE_ITEM_BASE = 172; $PUZZLE_HOLE_BASE = 242
$KEY_ITEM_BASE    = 196; $KEY_HOLE_BASE    = 258

# --- Lookup tables ----------------------------------------------------------
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

# Water rooms (waterRoom from the export, ROOM_UNDERWATER bit 0x1 of the room
# header). While swimming, height is irrelevant - every climb rule must
# exempt water rooms completely.
$roomWater = @{}
foreach($r in $d.rooms){ $roomWater[[int]$r.index] = [bool]$r.waterRoom }

# Effective standing height of a sector. A portal sector's floorY is NOT the
# height Lara stands on there - that was the core reason the first height
# rule attempt tore the levels apart (joby3 -82% etc.). The engine does it in
# GetHeight()/GetFloor() like this: at a vertical portal (pit_room) it walks
# on into the room below until real floor is found; at a horizontal portal
# Lara ultimately stands on the target room's floor. Exactly that is what
# this resolution mirrors: roomBelow first (you fall through vertically),
# else the portal target, else the own floorY. Not resolvable (cycles, wall
# sentinel) => $null, and the climb check deliberately treats $null as
# "allowed" - better one edge too many than a level wrongly cut apart.
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
            if ($f -gt -30000) { $v = $f }   # wall sentinel (-127 clicks) => unknown
        }
    }
    $effFloorCache[$k] = $v
    return $v
}

# May Lara take the edge from $ka to $kb? Down always (falling is unlimited -
# the old attempt also capped descents, and THAT was the second mistake
# besides the portal height, joby3's deep shafts dropped out completely
# because of it). Up only to CLIMB_LIMIT (1920, TestLaraVault) - unless one
# of the sectors is marked as a climbable wall, one of the rooms is water
# (swimming), or a bridge sector is involved (their floorY describes the
# ground below the bridge, not the bridge).
function Test-ClimbEdge([string]$ka, [string]$kb) {
    if (-not $EnableClimbModel) { return $true }   # physics model parked, see the parameter comment
    if ($bridgeSec.ContainsKey($ka) -or $bridgeSec.ContainsKey($kb)) { return $true }
    $sa = $sec[$ka]; $sb = $sec[$kb]
    if ($roomWater[[int]$sa.room] -or $roomWater[[int]$sb.room]) { return $true }
    $fa = Get-EffFloorY $ka
    $fb = Get-EffFloorY $kb
    if ($null -eq $fa -or $null -eq $fb) { return $true }
    $rise = $fa - $fb    # Y points down: > 0 means the target lies HIGHER
    if ($rise -le $CLIMB_LIMIT) { return $true }
    if ($sa.climbable -or $sb.climbable) { return $true }
    # Ladders hang on WALLS: the CLIMB_TYPE flag regularly sits on the wall
    # sector itself, and wall sectors are never endpoints of an edge. Lara
    # stands on the neighbor sector and climbs the marked wall next to it.
    # Hence a directly adjacent climbable sector (wall or floor) at either
    # endpoint counts as well.
    foreach($p in @($sa,$sb)){
        foreach($off in @(@(1,0),@(-1,0),@(0,1),@(0,-1))){
            $ak = "$($p.room),$([int]$p.sectorX + $off[0]),$([int]$p.sectorZ + $off[1])"
            if ($sec.ContainsKey($ak) -and $sec[$ak].climbable) { return $true }
        }
    }
    return $false
}

# --- Build the sector graph -------------------------------------------------
# Manually curated one-way transitions: a room Lara can enter but not leave
# again toward other rooms. Not derivable from the geometry alone (the portal
# exists in both directions), hence hand work - the same reasoning as
# boIsManuallyExcludedRoom in the exporter.
$ONEWAY_OUT = @{
    'Andrea1' = @(194)   # rat cage: in yes, on to 170 no
}

# Rooms Lara never enters without a glitch - typically roofs and ledges that
# look geometrically walkable but lie too high. That would really be the
# height model's job (-EnableClimbModel): it provably detects these cases
# (Andrea2 rooms 177/226 drop out with it), but at the same time classifies
# 86 rooms as unreachable, among them 63 and 69 with the puzzle slots -
# unusable both as a rule and as a suspicion generator. Hence hand curation:
# one number per room, directly readable in trview.
$EXCLUDE_ROOMS = @{
    # Reviewed by the user in trview. Roofs (177/226 and neighbors), pure air
    # rooms above the alleys and the area behind the deco doors 89/90.
    'Andrea2' = @(148,215,83,177,205,220,189,153,226,232,242,47,224,162,237,
                  181,156,124,127,185,112,122,119,118,128,114,115,141,87)
    # One-way rooms that only show the skybox and give the level depth.
    'Andrea3' = @(113,114,115,116,64,66,67,69)
    # joby2: terrain that looks walkable and is not - the big slanted
    # surfaces at the facility's edge (room 158 is the most striking case,
    # checked by the user in trview). The zone flood runs across them because
    # the sectors report floor; Lara never gets there.
    # Together 296 placeable sectors that would otherwise count as spots.
    # Second round after a trview review: roofs, ship hulls, quay walls and
    # other surfaces that report floor and are none. Not a single pickup lies
    # in them - only two snipers, animatings and bridge flats.
    'joby2' = @(6,7,9,14,149,155,158,
                8,10,13,15,19,21,139,145,147,148,150,151,153,154,159,
                # 39, 41 and 44 only surfaced at all through the lighting
                # correction - all three spots per room are new. The user
                # recognized them as invalid immediately. So the correction
                # does not only bring gains, it also creates new review work.
                39,41,44)
    # joby3, reviewed by the user in trview. No pickup lies in them - only an
    # electrical light and an animating in room 104 plus one closed door each
    # in 118 and 130.
    #
    # 116, 119, 129 and 184 joined during measuring: they hang only on the
    # rooms above and thereby dropped out. 116, 129 and 184 have no spot at
    # all anyway, so they are pure passages. Room 119 has four - per the
    # user they lie behind a deco door and are unreachable.
    #
    # Note on that: our deco-door rule treats a door without a trigger as
    # NOT blocking. In joby2 that was right because Lara could jump onto the
    # ledges. Here it is the other way round - the door never opens and
    # there is no other way. Both cases look identical in the data, the
    # difference sits solely in the geometry behind it.
    'joby3' = @(73,104,115,116,118,119,129,130,163,171,174,177,184)
    # andy1, reviewed by the user in trview. Room 236 is with 36 spots the
    # level's largest single item and lies behind the level's end - pure
    # scenery. 71, 92, 93, 162 and 211 likewise. No item lies in any of them.
    #
    # Expressly NOT included although the suspicion generator listed them:
    # 113 (a cutscene fires there, core area) plus 195, 50, 197, 170, 38 and
    # 37 - all playable. The generator shows suspects, not results; here it
    # was wrong in seven of thirteen cases.
    #
    # Second group: 6, 14, 16, 20, 58, 59 and 185 lie together in one level
    # corner and are unreachable one and all. Together only 19 spots and not
    # a single item. Room 59 was the occasion - it has 26 walkable sectors
    # and portals to 6, 16 and 185, so it looks like a proper room. It is
    # one, only nobody enters it.
    # User: "We will have that more often in this level."
    #
    # Third and largest group, 30 rooms from a further review. The user on
    # the procedure: "We take whole rooms, anything else would be too
    # extreme." Room 193 stands as the example - eight spots, floor heights
    # from 5632 to 8192, i.e. up to two and a half meters of difference
    # between neighboring sectors. Geometrically walkable, unapproachable
    # with Lara's moves. Curating sector by sector would be out of
    # proportion here.
    #
    # Costs 127 of the 502 spots and not a single pickup. Rooms 50 and 195
    # still counted as core level in an earlier review and on closer
    # inspection are not - the later statement applies.
    # The 30 rooms of the third review are NOT here but in NO_ITEM_ROOMS:
    # they are core areas Lara walks through, only their edges are no spot.
    # As a walkability bar they would have cut the key chain.
    'andy1' = @(6,14,16,20,58,59,71,92,93,162,185,211,236)
    # joby5: four rooms right at the level start. The doors before them -
    # 176, 187, 193 and 195 - have NO trigger, so our deco-door rule lets
    # them pass as non-blocking and the rooms behind count as reachable.
    # They are not. Three of them contain only a pulse light.
    # Third case of this kind after joby3 room 119 and joby2 room 11: in the
    # data, blocking and non-blocking deco doors look identical.
    # Room 112 comes from the same review: it hangs on a Floor Trapdoor1
    # (item 130) with flags 0 and without any trigger - it never opens.
    # Fourth case of the deco-door limit in this level.
    'joby5' = @(112,160,169,172,175)
    # joby4 first stood here with rooms 116, 135, 136 - following the 3x1024
    # estimate and room-clean at the time. The user's in-game measurement
    # then placed the limit at around y 13200, and there it is NOT room-clean
    # anymore. It now stands as a number in $MIN_FLOOR_Y and covers those
    # three rooms as well.
    #
    # 120 and 122 join from the user's review. They are the level's largest
    # single item: 96 spots each, together 192 of 586, so a scant third. No
    # item lies in either.
    'joby4' = @(120,122)
}

# Bar individual sectors where a whole room would be too coarse. Room 141 in
# joby2 is the case: it is playable, only two tiles at its edge are not.
# Format as everywhere else: "room,x,z".
$EXCLUDE_SECTORS = @{
    # Room 141: two tiles at the edge, otherwise fully playable.
    # Room 152: the big slanted surface - x 2 to 11 on z 2 and z 3, plus the
    # bulge (5,4) and (6,4). Barring the whole room would have been wrong:
    # Lara lands right there after the cutscene at door 41, and the two
    # cutscene triggers on (11,7) and (11,8) would have become unreachable.
    # The walkable part lies at x 10/11 in rows z 7-13 and 17-18.
    'joby2' = @('141,10,12','141,11,12') +
              @(2..11 | ForEach-Object { "152,$_,2" }) +
              @(2..11 | ForEach-Object { "152,$_,3" }) +
              @('152,5,4','152,6,4')
    # rich2's laser (item 178) between room 119 and 57. It is the only one of
    # the level's four lasers WITHOUT a trigger - 21, 181 and 34 can be
    # switched off, this one cannot -, so it is permanently on and a real
    # wall.
    #
    # It long cost 63 rooms - 590 spots down to 131, zones 6 down to 3 - and
    # was therefore removed three times. The reason was never the wall: the
    # portal graph knew only TWO entrances into this area, and both are shut.
    # 119->57 is the wall itself, and 58->59 leads through door 115, which
    # switch 0 in room 7 opens - from INSIDE. The real way was missing from
    # the model entirely.
    #
    # It now stands in MANUAL_LINK: room 130 -> room 139, the jump across the
    # WC niche into the elevator shaft. With it the wall costs NOTHING
    # anymore - 590 spots with it as without it -, and the zones grow from 6
    # to 7, because the elevator section now hangs behind the keypad instead
    # of next to the shortcut. Exactly how a proper bar must behave: it
    # orders, it does not take away.
    #
    # Note for the next case of this kind: a bar that costs half the level is
    # almost never too strict - it is missing a way the model does not know.
    # Find the way first, then judge the bar.
    #
    # Four sectors, not two. (8,1) and (8,2) are the tiles the beam lies on -
    # the user named them so, item 178 stands there. (9,1) and (9,2) are the
    # PORTAL to room 57, one sector behind.
    #
    # Barring both is necessary because Add-Edge creates portals both ways:
    # from 57 the flood arrived over the back edge into 119(9,z) without ever
    # having had to pass (8,z). For the main flood that changed nothing,
    # (8,z) was already the brake there - but the elevator island thereby
    # reached for room 119 and with it for a zone-0 room of the main side.
    'rich2' = @('119,8,1','119,8,2','119,9,1','119,9,2')
}
$excludeRooms = @( if($EXCLUDE_ROOMS.ContainsKey($LevelName)){ $EXCLUDE_ROOMS[$LevelName] } )
$excludeSectors = @( if($EXCLUDE_SECTORS.ContainsKey($LevelName)){ $EXCLUDE_SECTORS[$LevelName] } )

# Additional bars per item TYPE instead of per level: the room stays walkable
# and part of the path network, only THIS item may not lie there. The typical
# case are rooms that are reachable early but lie behind the lock the key
# opens - the zone alone does not see that, because it only knows "reachable
# from when on", not "still needed for what".
$TYPE_EXCLUDE_ROOMS = @{
    'Andrea2' = @{
        174 = @(17,134,214,46,9,10,207,219)   # Puzzle Item3
        175 = @(70,204)                       # Puzzle Item4
        176 = @(70,204)                       # Puzzle Item5 (141/87 are
                                              # entirely barred by now)
        # The valve (#61, user review at the wall test 2026-08-19). Puzzle
        # Item3 is already caught correctly by its zone limit, the valve
        # must stop earlier:
        #  - 31 is the leak path. From there the flood runs to 132 (and on
        #    to 246 - the statue's boss room) although all of that lies
        #    behind the door. User:
        #    "Room 31 must become a blocker."
        #  - 103 is only reachable AFTER the valve has been used - a valve
        #    lying there could not unlock itself.
        #  - 246 is the statue's boss room, confirmed by the user: "that
        #    lies behind the door". With it the area is self-contained.
        #  - The flood still runs on: TYPE_EXCLUDE_ROOMS bars only the TYPE,
        #    not the zone run. The zone behind the door therefore stays low,
        #    and the rooms behind it stayed allowed. The user read them off
        #    in trview and gave the go-ahead to bar them individually. The
        #    structural fix would be GATED_AREA (gate + stop edge as in
        #    Andrea1) - that needs the door numbers and a closed
        #    PreGateRooms list.
        172 = @(31,132,246,103,
                109,175,174,217,136,131,93,86,135,76)   # Puzzle Item1 (valve)
    }
    # joby2: Key Item7 must not land behind door 41. The derived rule "not
    # behind its own door" barely applies here - it finds only a single
    # sector, because you get behind the door via the outside area without
    # opening it. From the model's view the area thus does not lie behind it
    # at all, in game it does. Hence by hand, compiled by the user in trview.
    'joby2' = @{
        # Room 67 joined like 39/41/44 only with the lighting correction,
        # with 23 spots in one stroke. Unlike those it stays usable for
        # normal items - only Key Item7 may not lie there.
        202 = @(11,12,16,20,24,25,34,40,42,60,61,65,67,73,76,92,93,98,138,152)
        203 = @(83,115)                       # Key Item8
        # The fuse. Room 146 from the user's last review; the three single
        # tiles in room 141 stand in TYPE_EXCLUDE_SECTORS because the rest of
        # that room is fine for it.
        172 = @(146)                          # fuse
    }
    # joby3: Key Item7 must not lie behind door 175, it is its deadstop
    # (user: "Key 6/7 may appear up to room 106"). Measured with a flood from
    # room 109, door 175 as a wall, room 123 as the boundary: 58 rooms with
    # spots lie behind it - but 52 of them are already excluded by the zone
    # limit <=Z2 anyway. These six remain, and only they make the
    # difference. Room 109 is the case the user saw in the test level.
    # Key Item6 needs nothing of the kind: it stays with the chef in
    # VANILLA_TYPES.
    'joby3' = @{
        202 = @(49,60,109,126,152,159)
    }
    # joby5: Keyhole6 opens rooms 18, 54 and 106. Key Item6 must not lie
    # there - it would be behind the door it opens itself. Exactly the case
    # the withdrawn automatic rule was meant to cover; until there is a
    # sound solution for it (task #49), it stands here.
    # Key Item8 (the keycard for door 15) needs NO entry here. On 2026-08-23
    # one stood here briefly, after a tester report about permanently live
    # cables - it was superfluous and moreover narrower than what applies
    # anyway: NO_KEY_ROOMS already bars all five electrified water rooms for
    # joby5 (126,127,128,158,183), and that holds for EVERY key type.
    # Checked in the zone export: type 203 arrives there with maxZone 0 and
    # exactly these five rooms.
    'joby5' = @{
        201 = @(18,54,106)
    }
    # andy3: crowbar (174) and chalk (172) hang together and both hang on the
    # same point of no return. The user explained the chain: with the crowbar
    # Lara breaks the chalk out of the wall (Puzzle Item1 therefore carries
    # OCB 2, the crowbar pickup), with the chalk she draws the banishing seal
    # in room 18 sector (2,5), the cutscene makes the teleporter in room 110
    # reachable - and that is a one-way street.
    #
    # The level has neither a puzzle hole nor a key hole, so the zone logic
    # sees nothing of this chain and derives not a single constraint.
    #
    # An island group would be overkill here. The area behind the teleporter
    # is the component around room 140, and measured it comprises six rooms
    # with FOUR spots in total. In the portal graph it hangs on room 135 via
    # 142, in game you only get in by teleporter and not back. Barring six
    # room numbers is more traceable than a second flood and works just the
    # same.
    'andy3' = @{
        172 = @(136,137,139,140,141,142)      # chalk
        174 = @(136,137,139,140,141,142)      # crowbar
        # The coin: its COARSE area stands as TYPE_ALLOW_ISLAND (from 142,
        # stops 137/97/149). Within that island there are still rooms where
        # it has no business - user review in the debug level 2026-08-24,
        # which completes the list.
        173 = @(37,42,43,45,46,47,50,51,53,55,56,57,77,78,81,
                143,144,151,195,196,197)
    }
}
$typeExclude = @{}
if ($TYPE_EXCLUDE_ROOMS.ContainsKey($LevelName)) {
    foreach($kv in $TYPE_EXCLUDE_ROOMS[$LevelName].GetEnumerator()){ $typeExclude[[int]$kv.Key] = @($kv.Value) }
}

# Rooms that stay walkable and part of the path network but in which no key
# or puzzle item may lie - regardless of type. Unlike EXCLUDE_ROOMS this is
# deliberately NO walkability bar: Andrea3's start island contains Lara's
# start room 1, and making that non-walkable would choke the flood
# immediately. The reason is purely a placement reason - behind room 3 lies
# a point of no return (lava pit), a key overlooked there would be
# irrecoverable.
$NO_PLACEMENT_ROOMS = @{
    'Andrea3' = @(1,2,3,4,5,6,11,19,20,21,22,29,30,34,112,123,124)
}
$noPlacement = @( if($NO_PLACEMENT_ROOMS.ContainsKey($LevelName)){ $NO_PLACEMENT_ROOMS[$LevelName] } )

# The same as NO_PLACEMENT_ROOMS, only not as a room list but as a section
# between entrance and exit: `StartRoom` and `DeadstopRooms`, the rest
# follows from the sector graph. Meant for closed sections you do not want
# to write off room by room.
#
# The flood runs on THE SECTOR GRAPH, not on the room graph. That is the
# whole difference: the room graph knows no barred sectors, and in rich2 it
# therefore ran at 57 -> 119 straight through the laser wall back into the
# level - 128 of 179 rooms, down to Lara's start room. The sector graph
# carries the wall and stays inside.
$NO_KEY_ISLAND = @{
    # andy3 (#61): the two wall pickups - chalk (172) and fence crowbar
    # (174) - belong in the first island group, in which Lara starts and in
    # which both are needed. User statement:
    #
    #   "Deadstops are rooms 18, 19, 22. The chalk is used there, which is
    #    the only way to reach the teleporter Trigger74 in room 110 at all.
    #    That should now form a wall so the flood cannot get out."
    #
    # The entrance is room 140 - the same start room ISLAND_YBAND keeps for
    # island 2. Everything that lies behind the wall 18/19/22 from there
    # carries no key item anymore; ordinary pickups stay allowed.
    #
    # Deliberately NOT via TYPE_EXCLUDE_ROOMS: that only bars the type in
    # named rooms and does NOT stop the zone run - in Andrea2 the flood
    # seeped on because of that and in the end it took 14 rooms by hand.
    # ExemptTypes (#61, 2026-08-24): this island is meant FOR chalk and
    # crowbar - they should stay on the start island, so it bars everything
    # else (134 rooms). The coin needs the exact opposite: it must NOT go
    # onto the start island. Without the exemption both rules apply at once,
    # and a single room remains (in a test over 15 seeds always room 119).
    #
    # The exemption takes the type out of THIS island - out of the sector
    # list (keyBarredSectors) as well as out of the excludeRooms. Its own
    # bound stays: ISLAND_GROUP keeps it off the start island.
    'andy3' = @(
        @{ StartRoom = 140; DeadstopRooms = @(18,19,22); ExemptTypes = @(173) }
    )
    # rich2's elevator section. The user: the entrance is room 140, the exit
    # room 113, and what lies in between should carry no key items -
    # ordinary pickups yes.
    #
    # The reason sits in the section itself. It is a chain of switches, lift
    # doors and five teleporters, at whose end Lara is placed into the
    # ground floor by cutscene; the model sees only the sectors of it and
    # walks them in arbitrary order. A key inside would have a zone number
    # nobody can trust. A medipack, by contrast, does no harm.
    'rich2' = @(
        # Three deadstops, and each has its own reason.
        #
        # 141 is the shaft above the entrance. Without it the flood runs over
        # the hand edge 130 -> 139 backwards into the WC again and from
        # there into the whole level.
        #
        # 46 is the actual leak, and finding it took a while because it does
        # not stand in the sector data: the user saw it in trview. On the
        # ground floor, with FlipMap 0 the shaft is open, and FlipMap 0 is
        # set from level start - room 42 thereby hangs permanently on room
        # 46, the ground-floor hall. Over it the flood ran down to Lara's
        # start room 129 every time. My first attempt to bar 46's neighbors
        # 122 and 123 instead was symptom treatment: 46 itself no longer
        # belongs to the section.
        #
        # 113 is the exit the user named. It is needed in addition - 46
        # alone does not suffice, because 113 is also reachable from inside
        # via 115 and 172 and the flood runs on from there.
        #
        # Result: 51 rooms, 638 sectors, 93 of the 588 spots. Without the
        # deadstops it would have been 586 of 588 - practically the whole
        # level.
        @{ StartRoom = 140; DeadstopRooms = @(141,46,113) }
    )
}
$noKeyIslands = @( if($NO_KEY_ISLAND.ContainsKey($LevelName)){ $NO_KEY_ISLAND[$LevelName] } )

#----------------------------------------------------------------------------
# Rooms that stay walkable but in which NOTHING AT ALL may lie.
#
# The third bar level, and it was needed because the other two do not hit
# the case: EXCLUDE_ROOMS also takes the room out of the path network,
# NO_PLACEMENT_ROOMS bars only key and puzzle items.
#
# andy1 has the case in its pure form. The user describes a gorge Lara walks
# through; at its edges lie spots that are geometrically walkable and in
# reality unapproachable. Sector-precise would be out of proportion - "I
# would now have to hunt 12 sectors as invalid just to save 4 good ones".
# Entered as EXCLUDE_ROOMS, the list however cut the access to 229 sectors
# together with Puzzle Item1 and its lock, the level was no longer solvable
# in the model.
#
# Hence here: the way stays, the spots vanish.
#----------------------------------------------------------------------------
$NO_ITEM_ROOMS = @{
    #------------------------------------------------------------------------
    # The user's last review across all levels, after the test levels showed
    # both layers (223 on every sector, key ranges above). These six levels
    # had no entry at all until then.
    #------------------------------------------------------------------------
    'Andrea1' = @(216,67,155)
    # Room 6 joined 2026-08-23 (user). It carried 6 spot sectors in zone 0,
    # vanilla has nothing lying there. Deliberately here and not in
    # EXCLUDE_ROOMS: room 6 is a connector (portals to 4, 7 and 55), and
    # room 4 hangs only on 1 and 6 - as a walkability bar it could cut
    # paths. The way stays, the spots vanish.
    'Andrea2' = @(149,6)
    'Andrea3' = @(129,68,75)
    'joby2'   = @(72)
    'joby3'   = @(65)
    # 16, 40, 41, 42 and 156 joined 2026-08-23 (user, seen in game): enemies
    # and items still lay in all five. Right after the level intro Flipmap 0
    # engages, and all five are flip ORIGINALS - measured at the file
    # (RoomCheck): 16->84, 40->80, 41->69, 42->79, 156->90. From that moment
    # the flip version stands everywhere, the originals are never walked. An
    # enemy there never wakes, an item is never to be fetched.
    #
    # Deliberately here and not in EXCLUDE_ROOMS: as a walkability bar they
    # would cut the path network although their flip versions are very much
    # walked. The way stays, the spots vanish.
    'joby5'   = @(181,16,40,41,42,156)
    # Third review: 55, 78, 95, 145, 150, 186, 228, 231 - together 20 spots.
    # Room 186 is at the same time the island group's deadstop - barred here
    # only as a spot, the way stays.
    #
    # 55 and 78 hang together and are a special case the user cleared up.
    # The pickup trigger in room 51 sector (2,2) - exactly where the heart
    # lies - fires Object:8 (the heart itself), Object:12 (the door in room
    # 55) and Cutscene:19. If the heart is randomized elsewhere, nobody
    # picks anything up on this sector anymore, the trigger never fires and
    # door 12 stays shut. It cannot be moved because it also carries the
    # cutscene.
    #
    # The small area behind it is thereby dead, and that is accepted as
    # such: "Should the heart ever happen to lie on its trigger, ok, then
    # it's just empty. Otherwise whatever." That is also why nothing lies
    # there.
    # (Item 15 in room 78 is a smallmedi, not the heart - that is item 8.)
    # 27 and 200 joined in the last review.
    'andy1' = @(2,11,24,27,30,32,33,41,42,50,55,64,65,68,69,72,74,75,76,77,78,
                95,103,129,139,140,145,147,150,160,161,163,171,174,175,186,191,
                193,195,198,200,228,231,242)
    # Andy2, room 29: the mill's roof. In an 11x11 room a plateau stands in
    # the middle, around it there is no floor - a shaft. Exactly the nine
    # plateau sectors are spot surface, and the user confirmed them in
    # trview as unreachable ("those are on the mill").
    #
    # The model considers them reachable, and understandably so: sectors
    # 29(5,1) and (5,2) have no floor of their own, Lara stands there on
    # room 30 at y=13568. From there onto the plateau at 12288 is 1280
    # units, and Test-ClimbEdge allows up to 1920. The edge is not grabbable
    # in game though.
    #
    # Two automatic detections failed on this. A local test for "ascent >
    # 1024" does not find the case, because the gate sector 29(5,3) itself
    # is no spot and the plateau sectors lie level among themselves. And a
    # test for "plateau above floorless surroundings" hits over eighty rooms
    # in Andy2, because every small 3x3 room looks like that - room 29 was
    # not even among them. The viable variant would be a second flood with
    # climb bound 1024; until then the room stands here.
    #
    # The rest of the list is the user's second and third trview review. 31
    # and 41 are the rooms with the fire and spike traps - the sectors in
    # between look perfectly fine as spots.
    #
    # 132 and 134 are the flip variants of 31 and 41 and have no spot
    # surface at all anyway: the exporter bars every flip variant on
    # principle (RoomIsAlternate, see TRFile_RandomizerExport.cpp). They
    # stand here as a safety net in case that rule is ever loosened - then
    # the trap rooms must not silently come back.
    'Andy2' = @(7,29,31,41,132,134,143,144,145,173,174,175,176,177,179,180,181,248,250)
    # andy3 is a different beast among the bar lists than the other levels -
    # the user estimates at least a hundred rooms. The list grows here in
    # blocks as he works through them in trview.
    #
    # rich1, the user's first review. Five of the named rooms (46, 47, 87,
    # 90, 92) already had no spot before - they stand here anyway so the
    # list fully mirrors the review.
    #
    # In room 123 lies a vanilla smallmedi (item 178). That is entered
    # deliberately: the item is not lost, it only travels elsewhere, and the
    # acceptance rule "never forbid a vanilla position" is aimed at wrong
    # DERIVED rules, not at a hand decision of the user.
    #
    # Room 17 has its own reason only the user can know: AS SOON AS THE
    # HAMMER IS USED, the area is unreachable. What lies there is lost
    # afterwards. Nothing of that stands in the level data - the room looks
    # walkable, is reached and carries twelve flawless spots. Exactly the
    # kind of one-way street the zone model can in principle not see.
    # Room 5 joined in the last review.
    'rich1' = @(5,17,19,20,46,47,68,83,85,87,89,90,92,123,130,154)
    # rich2, room 107 in the elevator shaft. The user: walkable only once
    # Flipmap 7 is on, and Lara only gets there from the ground floor - so
    # AFTER the whole elevator section. The model sees it in zone 2,
    # together with the shaft itself, and that is several stations too
    # early. A FLIP_GATE does not help: 107 is a base room without its own
    # flip variant, the geometry change comes from neighboring rooms
    # flipping. Two spots are the whole price.
    #
    # Likewise from the user and noted here only so it does not get lost:
    # room 105 is the deadstop after the elevator crash - from there it
    # goes back into the level. It has no spots anyway, all its sectors are
    # too steep; for a later island group it is the right stop.
    #
    # The user's first bar list from the trview pass: 19, 42, 43, 44, 60
    # and 66. Four of them (42, 43, 44, 66) lie in the elevator island and
    # were already barred there for key items - now nothing at all may lie
    # there.
    # 34 and 110 joined in the last review. Room 34 is the one with the
    # motion sensor that sets off the traps - that question is settled too.
    # 14, 15, 16 and 18 joined on 2026-08-15: per the user they lie behind
    # door 171 and thus belong neither to the start island nor reliably to
    # the area behind it. 19 and 60 already stood here before.
    'rich2' = @(107,14,15,16,18,19,34,42,43,44,60,66,110)
    # rich3, the user's bar list from the trview pass. 28 rooms; room 121 he
    # had named twice, 209 came last. With that rich3 is done.
    'rich3' = @(2,23,24,33,41,47,51,52,56,58,60,68,71,72,84,85,92,121,151,153,
                169,170,171,178,193,198,204,209)
    # Room 113 is the first and at the same time shows why the model can
    # help so little here: the room contains NOT ONE item and NOT ONE
    # static, its spikes sit in the room geometry itself. Neither the item
    # nor the statics list knows of them, and a sector with floor and head
    # room looks flawless to the exporter. Such traps can only be found by
    # eye.
    # 155 is at the same time island 2's deadstop. That does not clash: the
    # sections are determined via the room graph and DeadstopRooms,
    # NO_ITEM_ROOMS acts only afterwards on the spots.
    # 109 joined with #61 (user review at the wall test): "room 109 may be
    # nothing."
    'andy3' = @(109,10,12,14,15,16,17,19,22,23,24,25,27,35,36,86,87,108,113,115,116,
                120,121,133,155,156,158,167,171,172,209)
}
$noItemRooms = @( if($NO_ITEM_ROOMS.ContainsKey($LevelName)){ $NO_ITEM_ROOMS[$LevelName] } )

# A positive list instead of a bar list: some types may only lie in a
# clearly outlined section. Andrea3 is the case - both keys must lie in the
# corridor from room 26 via 27 toward the end, Key Item1 up to and including
# its lock in room 15, Key Item2 up to room 102. The zones alone do not
# suffice for that: the teleporter is a one-way street because of the
# crumble floor, so rooms before it can share the same zone and still be the
# wrong side. Room lists derived from the portal graph (flood from 27, stop
# at 26 backwards and at the lock forwards).
#----------------------------------------------------------------------------
# Like TYPE_EXCLUDE_ROOMS, only finer: individual sectors instead of whole
# rooms, per type. Needed when a room is mostly fine for a type and only a
# few tiles in it are not - then the room bar would be too coarse and the
# general NO_ITEM_SECTORS list too wide, because it hits ALL types.
#
# joby2 is the first case: the fuse (typeId 172) may not lie on three tiles
# in room 141, while the rest of the room suits it. The nine other tiles of
# the same room are barred for every type and therefore stand in
# NO_ITEM_SECTORS.
#
# Added to bannedSectors below, the same path as NotOnIslandTypes - that way
# it takes effect in both constraint paths, derived as well as hand-set.
#----------------------------------------------------------------------------
$TYPE_EXCLUDE_SECTORS = @{
    'joby2' = @{ 172 = @('141,9,3','141,8,3','141,6,2') }
    # andy3 (#61), user statements from the debug level 2026-08-24. A room
    # bar would be too coarse in all cases - the rooms are mostly fine for
    # the respective type.
    'andy3' = @{
        173 = @('38,2,1','38,3,1','34,2,12','34,3,12')   # coin
        # Chalk and crowbar: room 31, tile (2,1) - "the geometry there is
        # too sharp". Both are wall grabs (OCB 2 and 9) and have NO
        # constraint; they are therefore barred via typeNoSectors, the list
        # that takes effect outside the constraint branch.
        172 = @('31,2,1')                                # chalk
        174 = @('31,2,1')                                # crowbar
    }
}

$TYPE_ALLOW_ROOMS = @{
    'Andrea2' = @{
        # Rooms confirmed as "safe" by the user for the crowbar.
        # 240, NOT 172: typeId 172 is called "Puzzle Item1" everywhere, but
        # in Andrea2 it is the valve (item 128, room 196) that is broken
        # open with the crowbar. The real crowbar is typeId 240 "Crowbar
        # Item" (item 2, room 1) - in the whole game only here. In joby3 and
        # rich1 the crowbar really is disguised as a puzzle/key item, hence
        # the risk of confusion.
        # Room 26 joined in the last review.
        240 = @(0,1,10,17,25,26,46,50,54,207)
    }
    'Andrea3' = @{
        196 = @(15,16,26,27,107,108)                         # Key Item1, end 15
        197 = @(10,15,16,17,18,26,27,28,55,56,70,83,85,87,   # Key Item2, end 102
                88,89,90,100,101,102,103,104,105,106,107,
                108,110,111,119,120,121,129)
        # Puzzle Item4 Combo1: from room 0 toward 63. The direction toward 58
        # is barred - a slope lies there, i.e. a one-way street. The dead
        # stop per the user is room 35.
        186 = @(0,9,31,32,33,35,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,
                52,53,54,59,60,61,62,63,65,68,75,76,78,79,81,82,84,
                86,93,94,95,96,97,98,99,109,117,118,125,128)
    }
}
#----------------------------------------------------------------------------
# Like TYPE_ALLOW_ROOMS, only not counted by hand: the allowed room list is
# derived as a FLOOD over the room graph - start room plus stops.
#
# Needed for andy3's coin (#61). The user described the area exactly so, as
# a directional statement:
#
#   "Room 137 -> 139 coin forbidden. Room 137 -> 142 coin allowed, up to 2
#    points room 97 and room 149. Room 97 must be a stop because that is
#    behind the water witch - there is a teleporter into another area,
#    everything around room 157. Room 149 is the other deadstop, that is
#    the return from the other teleporter. The island in which the coin may
#    occur is (137 -> 142 and everything after that up to |149|97)."
#
# As a list that would be 104 room numbers - unreadable and dead as soon as
# the level graph changes. As a flood, the INTENT stands there: from 142,
# and the three stops are exactly the three doors behind which it would go
# wrong (137 back onto the start island, 97 behind the witch, 149 the
# second teleporter's return path).
#----------------------------------------------------------------------------
$TYPE_ALLOW_ISLAND = @{
    'andy3' = @{
        173 = @{ StartRoom = 142; DeadstopRooms = @(137,97,149) }
    }
}

$typeAllow = @{}
if ($TYPE_ALLOW_ROOMS.ContainsKey($LevelName)) {
    foreach($kv in $TYPE_ALLOW_ROOMS[$LevelName].GetEnumerator()){ $typeAllow[[int]$kv.Key] = @($kv.Value) }
}
if ($TYPE_ALLOW_ISLAND.ContainsKey($LevelName)) {
    # Room adjacency from the portals, ceiling passages and floors - the
    # same derivation as at ISLAND_YBAND further down.
    $allowAdj = @{}
    foreach($sx in $d.sectors){
        $a = [int]$sx.room
        foreach($t in @($sx.portalTarget, $sx.roomBelow, $sx.roomAbove)){
            if ($null -eq $t -or "$t" -eq "") { continue }
            $b = [int]$t
            if ($b -lt 0 -or $b -ge $d.rooms.Count) { continue }
            if (-not $allowAdj.ContainsKey($a)) { $allowAdj[$a] = New-Object System.Collections.Generic.HashSet[int] }
            if (-not $allowAdj.ContainsKey($b)) { $allowAdj[$b] = New-Object System.Collections.Generic.HashSet[int] }
            [void]$allowAdj[$a].Add($b); [void]$allowAdj[$b].Add($a)
        }
    }
    foreach($kv in $TYPE_ALLOW_ISLAND[$LevelName].GetEnumerator()){
        $nt = [int]$kv.Key
        $stopA = @($kv.Value.DeadstopRooms | ForEach-Object { [int]$_ })
        $seenA = New-Object System.Collections.Generic.HashSet[int]
        [void]$seenA.Add([int]$kv.Value.StartRoom)
        $qa = New-Object System.Collections.Generic.Queue[int]
        $qa.Enqueue([int]$kv.Value.StartRoom)
        while($qa.Count -gt 0){
            $ca = $qa.Dequeue()
            if (-not $allowAdj.ContainsKey($ca)) { continue }
            foreach($na in $allowAdj[$ca]){
                if ($stopA -contains $na -or $seenA.Contains($na)) { continue }
                [void]$seenA.Add($na); $qa.Enqueue($na)
            }
        }
        $typeAllow[$nt] = @($seenA | Sort-Object)
        Write-Output "TYPE_ALLOW_ISLAND type $nt (from $($kv.Value.StartRoom), stops $($stopA -join ', ')): $($seenA.Count) rooms allowed"
    }
}
$typeExcludeSectors = @{}
if ($TYPE_EXCLUDE_SECTORS.ContainsKey($LevelName)) {
    foreach($kv in $TYPE_EXCLUDE_SECTORS[$LevelName].GetEnumerator()){ $typeExcludeSectors[[int]$kv.Key] = @($kv.Value) }
}

# Types that belong in the pool but whose zone limit the model cannot derive
# itself, because they lack a lock of their own in the level. Combination
# parts are the case: Andrea3's Puzzle Item4 Combo1 (186) and Combo2 (187)
# are united in the inventory into Puzzle Item4, only that goes into a
# Puzzle Hole4 - and only the hole has a zone. Decisive is item 34 in room
# 24: there the complete combo triggers the teleport. The second Puzzle
# Hole4 (item 20, room 8) is per the user pure decoration masking the
# teleport, and no bound. Both lie in zone 3 anyway.
# Combo2 (187) deliberately does NOT stand here - see FIXED_ITEMS.
# Value here: typeId -> maxZone.
$EXTRA_POOL_TYPES = @{
    'Andrea3' = @{ 186 = 3 }
    # Andrea2's crowbar (typeId 240, item 2, room 1, zone 0) may be
    # randomized along - that does not contradict the rule "the crowbar
    # stays vanilla": that one holds for joby3 and rich1, where the crowbar
    # is disguised as a puzzle/key item and hangs on a pickup cutscene. Here
    # it is a normal pickup. Zone limit 1 = the zone of the valve (item 128,
    # room 196) that cannot be broken open without the crowbar; the actual
    # bound is the room list the user confirmed as "safe".
    'Andrea2' = @{ 240 = 1 }
    # rich2's only real key item: Puzzle Item3 (typeId 174, item 234, room
    # 166). Of its four locks only Puzzle Hole3 (item 207, room 147) finds a
    # matching item - Hole8 stands twice for the keypads, whose "key" is a
    # code to read off, and Hole2 in room 149 has no Puzzle Item2 in the
    # whole level.
    #
    # The model derives NOTHING here by itself, and the reason is
    # instructive: it only knows locks that unlock a DOOR. All 27 zone doors
    # in rich2 hang on triggers and switches, none on a lock. Hole3, by
    # contrast, raises a Raising Block2 (item 214) in room 149 and switches
    # four animatings - only through that does Lara reach Hole2, whose
    # trigger sets the teleporter to room 152. A lift block is no bound in
    # the zone model, so it does not see the chain.
    #
    # Decisive is the lock's zone: room 147 lies in zone 4, so the item must
    # lie in zone 4 at the latest. Its vanilla position in room 166 is
    # likewise zone 4 - the limit does not forbid it.
    'rich2'   = @{ 174 = 4 }
    # rich3's Puzzle Item1 does not exist in the level at all - it arises in
    # the inventory from two halves, and both are CARRIED by the cyborgs:
    #   Combo1 (180), item 201, lies on cyborg 200 (Hitman) in room 183
    #   Combo2 (181), item 184, lies on cyborg 183 (Hitman) in room 152
    # The report therefore said "comes via gameflow, no placement needed" -
    # it looks for type 172 and does not find it. That was misread, the
    # chain is there, only split in two. The same construction as Andrea3's
    # Puzzle Item4.
    #
    # The two cyborgs cannot be killed with weapons: one needs his room
    # flooded (FlipMap 6, short circuit), the other the gas area around room
    # 149. The complete Puzzle Item1 goes into slot 1 (item 144, room 119)
    # and opens door 213 to the level's end.
    #
    # Decisive is the lock's zone: room 119 lies in zone 2. Both vanilla
    # positions also lie on zone-2 sectors, so the limit forbids neither.
    'rich3'   = @{ 180 = 2; 181 = 2 }
    # andy3's coin (typeId 173, Puzzle Item2) - the last remnant of #61.
    #
    # It has NO lock in the whole level and therefore does not appear in the
    # keyConstraints by itself; it is the trigger of the swamp witch
    # (swampy.cpp asks have_i_got_object(PUZZLE_ITEM2)). Without an entry it
    # counts as an ordinary pickup, and the deadstop bar (keyBarredSectors)
    # does not touch it at all - that one only grips types WITH a constraint
    # or wall grabs (Randomizer.cpp).
    #
    # maxZone 1 is the level's maximum, so the zone restricts nothing - that
    # is intentional. The actual bound is the deadstop in room 137
    # (NO_KEY_ISLAND above): behind it lies a teleporter onto the start
    # island, and a coin beyond it would be a softlock (user statement
    # 2026-08-24: range 142 -> 135).
    'andy3'   = @{ 173 = 1 }
}

# rich2's start island: everything Lara can enter BEFORE the X-ray scanner.
# List from the user (2026-08-15) - it is not derivable from the data, the
# sector graph is undirected and floods past the point of no return.
# Needed by REQUIRED_INJECTIONS and TYPE_NO_ROOMS, hence it stands before
# both.
#
# Addendum of the same day: 14, 15, 16, 18, 19 and 60 lie per the user
# BEHIND the door and do not belong to the island. They now stand in
# NO_ITEM_ROOMS - nothing lands there at all anymore, which also settles
# the HK question for them.
$RICH2_START_ISLAND = @(8,9,10,12,21,29,48,49,50,51,52,53,54,55,56,
                      84,85,88,89,90,91,95,96,97,116,117,118,119,130,131,132,133,
                      134,135,136,137,138,139,141,170,177)

# The item-free rooms stand in NO_ITEM_ROOMS further down - for rich2, 14,
# 15, 16 and 18 are added there too (behind door 171, user statement).

# Items a level absolutely NEEDS but that are missing in vanilla. Andrea3
# contains not a single crowbar (typeId 172, counted in the level: 0
# instances) - a latent softlock already in vanilla. The randomizer must not
# only redistribute here but add. Room 120 per the user, the secret (item
# 102, Pickup Item4) already lies there.
$REQUIRED_INJECTIONS = @{
    # typeId 240 "Crowbar Item", NOT 172: 172 is called "Puzzle Item1" but is
    # something different per level (a disguised crowbar in joby3/rich1, a
    # valve in Andrea2). 172 first stood here wrongly - noticed only when the
    # randomizer actually produced the patch.
    'Andrea3' = @( @{ typeId = 240; room = 120; reason = 'level contains no crowbar - vanilla softlock' } )
    # rich2: one HK and two magazines on 140(1,2), spot chosen by the user.
    # Unlike Andrea3's crowbar with a fixed SECTOR, because here the one tile
    # matters - all three should lie there together.
    #
    # The reason is a softlock only the randomizer creates: from room 140 on,
    # a key item can land under an enemy, and without a weapon Lara could
    # not get at it. In vanilla the question does not arise because nothing
    # travels there.
    #
    # That does NOT contradict NO_WEAPON_LEVELS, even if it looks like it.
    # The bar there is aimed at randomly distributed weapons BEFORE the
    # X-ray scanner in room 9 - Lara must disarm herself at mesh 15, and a
    # second weapon anywhere before it makes that pointless. Room 140 lies
    # behind the keypad door and per the user behind a point of no return,
    # the scanner is long passed by then. The level end is safe as well: the
    # trigger-triggerer at the fire extinguisher hangs on FlipMap 0, which
    # is only switched off in room 58 - a weapon alone thus opens nothing.
    # Injections do not run through the weapon filter anyway, they are
    # placed directly; the bar stays in force for the distribution.
    'rich2'   = @(
        # This HK's height offset does NOT stand here but in TYPE_OFFSET
        # under rich2 - a second entry here would double it.
        # Globally it is gone since #61: the engine already lifts a pickup
        # by bounds[3] at OCB 0, our -100 came on top and left the HK
        # floating. Only at this hand-picked spot is it needed.
        @{ typeId = 345; room = 140; sectorX = 1; sectorZ = 2; reason = 'HK - from here on a key item can lie under an enemy' }
        @{ typeId = 346; room = 140; sectorX = 1; sectorZ = 2; reason = 'HK ammo next to the HK on the same tile' }
        @{ typeId = 346; room = 140; sectorX = 1; sectorZ = 2; reason = 'HK ammo next to the HK on the same tile' }

        # Mandatory Deagle for the start island (user wish 2026-08-15). The
        # start island is enemy-rich, and since only the HK is barred there,
        # other weapons may lie around - but chance alone delivers none:
        # only three to four weapons are scattered per run, and the island
        # provides 117 of the 544 spots (21 percent). Measured over six
        # seeds, not a single one landed there.
        #
        # No fixed room but the whole island as the choice: the seed picks a
        # different free sector in it every time ("the room can always be
        # random"). Weapon and magazines do NOT lie together here - each
        # line rolls its own spot.
        #
        # Amount: the Deagle brings 6 shots on pickup, each magazine another
        # 6 (newinv2.cpp:2550 and 2622) - with three magazines that is 24
        # shots, the "about 20" the user wanted.
        @{ typeId = 347; rooms = $RICH2_START_ISLAND; reason = 'Deagle - the start island is enemy-rich, pistols alone do not suffice' }
        @{ typeId = 348; rooms = $RICH2_START_ISLAND; reason = 'Deagle ammo for the start island (6 shots)' }
        @{ typeId = 348; rooms = $RICH2_START_ISLAND; reason = 'Deagle ammo for the start island (6 shots)' }
        @{ typeId = 348; rooms = $RICH2_START_ISLAND; reason = 'Deagle ammo for the start island (6 shots)' }
    )
}
$requiredInjections = @( if($REQUIRED_INJECTIONS.ContainsKey($LevelName)){ $REQUIRED_INJECTIONS[$LevelName] } )

#----------------------------------------------------------------------------
# Placement offset per type: dx/dy/dz against the sector center at floor
# height. Needed when an object's mesh origin does not sit where the model
# assumes it - the item then stands in the floor or at the sector's edge
# instead of the center. That is not readable from the level data, it only
# shows in the picture. y grows downward in TR, so a negative dy lifts.
#
# Two layers, and both are needed:
#   'ALL'       - a property of the object, holds in every level.
#   '<level>'   - the same object behaves like that only there. TR5 refills
#                 its puzzle and key slots per level, so "Puzzle Item3" is
#                 five different things in five levels.
# Level values override global ones per axis.
#
# Why as a rule at all and not as a hand entry at the individual injection:
# the next person who places such an item should not have to think of it.
# The same maxim as with VANILLA_TYPES - a table that only describes an
# intent and enforces nothing does not work.
#
# 345 (HK): otherwise sits in the floor, invisible and unfindable. Observed
# by the user. The game's only HK (rich2 item 6, room 8) lies 384 units up
# in vanilla, but stands on a pedestal there (OCB 4) and is scripted; for a
# freely placed one, 100 suffice.
#
# Andrea2 / 174: not a height but a depth problem - the object lands at the
# sector's edge instead of the center, its origin sits half a sector off.
# HOLDS ONLY HERE: typeId 174 occurs in five levels (Andrea2, andy1, andy3,
# rich1, rich2), and in all five the vanilla item lies exactly on its
# sector floor. A global entry would override four occupied vanilla
# positions.
#----------------------------------------------------------------------------
$TYPE_OFFSET = @{
    # The HK (345) stood here with dy -100 and is out again (#61, user
    # observation 2026-08-19: "the HK floats above the floor again").
    #
    # Recomputed instead of guessed. ModelOffset on the model in rich2: the
    # meshes lie at y 23..50, i.e. BELOW the origin (y grows downward).
    # InitialisePickup corrects with y -= bounds[3] at OCB 0, and bounds[3]
    # is exactly that 50. An item at floor height is thus already lifted by
    # the engine to floorY-50, putting the model's lower edge exactly on
    # the floor. Our -100 came on top: floorY-150, the model floats 100
    # units in the air - exactly the picture the user sees.
    #
    # The old rule "the HK must go 100 higher or it is invisible" dates
    # from the time of the faulty rich2 import; it compensated a different
    # problem. Should the HK stick in the floor without an offset after
    # all, -50 is the next candidate, not -100.
    'ALL'     = @{ }
    # ... except in rich2. There REQUIRED_INJECTIONS puts an HK onto a spot
    # hand-picked by the user (room 140, sector 1,2), and exactly there the
    # offset is still needed - user finding 2026-08-19: "Only for rich2 we
    # must keep the Y offset. The other levels can stay without the
    # offset."
    #
    # Practically affects only this one HK: rich2's vanilla HK (item 6)
    # stands in VANILLA_TYPES and does not travel anyway.
    'rich2'   = @{ 345 = @{ dy = -100 } }
    # Andrea2 / 174 stood here with dz -512 and is out again: the type now
    # stays vanilla (see VANILLA_TYPES), so it is not moved at all anymore.
    # The measured offset stays noted for completeness in case the object
    # ever returns to the pool.
}

#----------------------------------------------------------------------------
# Types whose OCB must SURVIVE a relocation - the exception to rule 3.
#
# Rule 3 zeroes the OCB on every move, and for pedestal and hiding-spot
# OCBs that is right: they describe the old spot and would be meaningless
# at the new one. For some objects, though, the OCB is no place statement
# but the PICKUP MANNER. Andrea2's Puzzle Item3 carries OCB 2 - the crowbar
# pickup with its own bounds, its own animation and inventory check
# (pickup.cpp, case 2). With OCB 0 it lands in the default branch, and
# there it is provably not pickable at all: the user tried it at three
# different spots, two of them without any trigger binding. With OCB 2 Lara
# plays the animation and picks it up.
#
# Level-bound here as well: typeId 174 is five different things in five
# levels, and only in Andrea2 is it a crowbar object.
#----------------------------------------------------------------------------
# ANDREA2 / 174 STOOD HERE AND IS OUT AGAIN - the attempt succeeded and
# still led to the opposite. With OCB 2 Lara picks the object up at any
# spot, the animation runs, the piece lands in the inventory. Only then the
# rest of the condition stands there: the crowbar pickup needs a WALL
# behind it and an angle matching that wall (the user saw it in game and
# named it himself). Both lie below the sector resolution - the model
# computes in whole 1024 tiles and knows no wall orientation. It could thus
# not guarantee a valid position, only guess one.
#
# The user made the same decision in THIS level once before: Andrea2's
# valve (typeId 172) stands in VANILLA_TYPES for exactly this reason. 174
# follows it.
#
# Measured and recorded here in case the object ever returns to the pool:
# it then needs dz -512 (mesh origin offset by half a sector) and dy +512,
# because the engine does the automatic floor correction only for OCB 0, 3,
# 4, 7, 8 and 11 (init.cpp:477) and OCB 2 is exempt from it.
#
# The mechanism stays, it is built and tested - there is just currently no
# type for which it would be right.
$KEEP_OCB = @{
    # Andrea2's wall pickups (#61). Both carry OCB 2 (crowbar grip); the
    # randomizer otherwise zeroes the OCB on moving, and with OCB 0 the
    # object would not be pickable at all anymore.
    #
    # They used to stand in VANILLA_TYPES for that reason: keeping the OCB
    # meant having to satisfy the grip's full condition - a wall behind it
    # and a matching angle -, and the model could not guarantee that. Since
    # ZoneAnalysis delivers wallAngles per sector and the placement picks
    # wall edge and angle, exactly that is satisfied.
    'Andrea2' = @(172,174)
    # andy3 the same: chalk (OCB 2) and fence crowbar (OCB 9).
    'andy3'   = @(172,174)
}
$keepOcbTypes = @( if($KEEP_OCB.ContainsKey($LevelName)){ $KEEP_OCB[$LevelName] } )
$typeOffsetOf = @{}
foreach($layer in @('ALL', $LevelName)){
    if (-not $TYPE_OFFSET.ContainsKey($layer)) { continue }
    foreach($kv in $TYPE_OFFSET[$layer].GetEnumerator()){
        $t = [int]$kv.Key
        if (-not $typeOffsetOf.ContainsKey($t)) { $typeOffsetOf[$t] = @{ dx=0; dy=0; dz=0 } }
        foreach($axis in @('dx','dy','dz')){
            if ($null -ne $kv.Value[$axis]) { $typeOffsetOf[$t][$axis] = [int]$kv.Value[$axis] }
        }
    }
}
$typeOffsets = @( $typeOffsetOf.GetEnumerator() | ForEach-Object {
    [PSCustomObject]@{ typeId=[int]$_.Key; dx=[int]$_.Value.dx; dy=[int]$_.Value.dy; dz=[int]$_.Value.dz } } )

# Items that MUST stay at their vanilla position. Do not move, do not
# replace, do not take into the pool. The reason is in each case a side
# effect that hangs on the position and not on the item and that is not
# readable from the level data - here: Andrea3 item 49 (Puzzle Item4
# Combo2) triggers a door. Exported into the zones JSON so a later
# randomizer can adopt the bar instead of having to rediscover it.
# The enemy entries below come from the user's list "Gegner die Vanilla
# bleiben muessen.txt" - enemies that must stay vanilla (2026-08-14):
# scripted enemies that stay untouched
# for cutscenes, stealth sequences or later reworks. FIXED_ITEMS beats both
# the enemy swap and every placement.
$FIXED_ITEMS = @{
    'Andrea3' = @{
        49 = 'Puzzle Item4 Combo2 - triggers a door'
        79 = 'Roman god (room 90) - stays vanilla (user list 2026-08-14)'
    }
    'Andrea2' = @{
        # Item 128 (the valve) stood here with the reason "must sit at a
        # wall". Exactly that is what the placement does itself since #61:
        # wall edge and angle are chosen, KEEP_OCB keeps the OCB 2. The
        # reason is thus gone, the item may travel.
        # The scripted Larson. Cutscene 8 (andrea3_end, deltapak.cpp) wakes
        # EXACTLY THIS item and makes it killable, its death fires the heavy
        # trigger on 72,2,2 and with it the death cutscene. A different
        # enemy type at this index would mean: the cutscene wakes nobody.
        76 = 'Larson - scripted miniboss, hangs on cutscenes 8 and 7'
        # The Hydra is THREE items on one position; the OCB picks the chain
        # link (hydra.cpp:18). Tearing them apart destroys the boss.
        91 = 'Hydra chain link (room 116) - stays vanilla (user list)'
        92 = 'Hydra chain link (room 116) - stays vanilla (user list)'
        93 = 'Hydra chain link (room 116) - stays vanilla (user list)'
        65 = 'type 420 (room 69) - scripted, stays vanilla (user list)'
        108 = 'Roman god (room 131) - stays vanilla (user list)'
    }
    'Andy2'   = @{
        0  = 'skeleton (room 0) - scripted riser (user list)'
        4  = 'skeleton (room 5) - scripted riser (user list)'
        5  = 'skeleton (room 5) - scripted riser (user list)'
        10 = 'skeleton (room 6) - scripted riser (user list)'
        11 = 'skeleton (room 6) - scripted riser (user list)'
    }
    'andy3'   = @{ 20 = 'Green Teeth (room 99) - stays vanilla (user list)' }
    'joby2'   = @{
        20 = 'sniper (room 19) - scripted, stays vanilla (user list)'
        21 = 'sniper (room 21) - scripted, stays vanilla (user list)'
        # Item 69 (guard OCB 3, room 58 - gets up from the chair) stood here
        # until the slot decoupling #111: its OCB sets meshswap_meshbits,
        # and the shared slot 400 carried a foreign donor's set. Since every
        # enemy has its own parking spot (462-467), a native guard falls
        # back to the level's own MESHSWAP1 - the hands are right again,
        # the item may travel.
    }
    'joby3'   = @{ 186 = 'chef (room 120) - only killable via the stealth cutscene, stays vanilla (user list)' }
    'joby5'   = @{ 145 = 'Crane Guy MIP (room 121) - cutscene double, stays vanilla (user list)' }
    'rich1'   = @{
        27 = 'CD - invisible, revealed from the suit by a switch'
        85 = 'scientist (room 34) - aim-at cutscene, stays vanilla (user list)'
        180 = 'Twogun (room 130) - stealth-kill sequence, stays vanilla (user list)'
    }
    # rich2's guards (USSR). User requirement since #38, confirmed and
    # extended by the user list 2026-08-14.
    'rich2'   = @{
        154 = 'guard (USSR, room 84) - stays vanilla (user requirement)'
        155 = 'guard (USSR, room 84) - stays vanilla (user list)'
        219 = 'guard (USSR, room 156) - stays vanilla (user list)'
    }
    'rich3'   = @{
        183 = 'Hitman (room 152) - stays vanilla (user list)'
        200 = 'Hitman (room 183) - stays vanilla (user list)'
        # User decision 2026-08-17 (#109): the soldier family may now be
        # swapped with an OCB too - but not these two. They stand together
        # in room 68 (both OCB 5) and hang on the ROPE; a different type
        # there makes the spot unplayable. "53 54 must stay for now because
        # of the rope."
        53 = 'SWAT+ (room 68) - hangs on the rope, stays vanilla for now'
        54 = 'SWAT (room 68) - hangs on the rope, stays vanilla for now'
    }
}

# Types that stay entirely at their vanilla positions. Their constraint is
# still derived and shown in the report - one should see that it exists -,
# but they do not travel into the pool and get no markers.
# Andrea2's valve (172) is the case: it must sit within 512 units of a wall
# AND its angle must match that wall. Both are conditions below the sector
# resolution - the model works with whole 1024 sectors and knows no wall
# orientation, so it could not guarantee a valid position at all. User
# decision: "The crowbar as random is enough."
$VANILLA_TYPES = @{
    # 174 joined after a long search and for the same reason as 172. The
    # randomizer zeroes the OCB when moving, and with OCB 0 the object is
    # NOT pickable - the user tried it at three spots, two of them without
    # any trigger binding. With the OCB 2 kept it works (tested), but then
    # the crowbar pickup's full condition applies: a wall behind it and a
    # matching angle. A model made of 1024 sectors without wall orientation
    # cannot guarantee that. See the long reasoning at KEEP_OCB.
    # Andrea2 listed 172 and 174 here - the two wall pickups. Since #61 they
    # may travel: the placement now picks wall edge and angle, and KEEP_OCB
    # holds their OCB 2. The reason for the shutdown is thus gone. A side
    # effect that had to be checked: for shut-down types this script derives
    # no key constraint - which is why the wall items in the first test
    # level reached all the way to the level's end.
    # joby3's Puzzle Item1 (item 41, room 22) is the disguised crowbar - it
    # looks like a puzzle piece but opens the crowbar doors 25 and 125. It
    # stays in place so the pickup cutscene is right.
    #
    # Plus two things around the chef in room 120/107, both user statements:
    # Key Item6 (201) stays lying with the chef, and the pistols (334) stay
    # in room 107 - if they are moved, the chef glitch occurs. That is also
    # why Key6 is no case for a deadstop; only Key Item7 needs one, and the
    # derived rule "not behind its own door" (door 175) already delivers it
    # by itself.
    'joby3' = @(172,201,334)
    # rich1's Puzzle Item3 is the CD. In vanilla it carries Invisible=true
    # and lies outside the map; the switch in room 15 activates it together
    # with Door 4 and Door 2. In game it falls out of the suit once its
    # animation runs. A relocation would destroy the reveal.
    'rich1'   = @(174)
    # andy3: all three puzzle items stay put. User decision, and the trview
    # pictures show why - each sits in the scenery instead of lying on the
    # floor. The chalk (172) sits in a rock wall and carries OCB 2, the
    # crowbar pickup; the crowbar (174, OCB 9) sits in a wire fence; the
    # coin (173, OCB 11) lies on an ornamental lid in the water. On moving,
    # OCB is set to 0, all three would afterwards lie flat on the floor and
    # the staging would be gone - the same reason rich1's CD does not
    # travel.
    # andy3: 172 (chalk, OCB 2) and 174 (crowbar in the fence, OCB 9) are
    # free since #61 - the placement picks wall edge and angle, KEEP_OCB
    # holds their OCB.
    #
    # 173 (coin) is ALSO FREE since 2026-08-24 - the last remnant of #61.
    # The old reason was its OCB 11, and that turned out to be an error:
    # `PickUpCollision` knows only 1, 2, 3/4/7/8 and 9 as special branches,
    # EVERYTHING else (11 too) runs into `default` and is picked up
    # perfectly normally; the floor correction in `InitialisePickup` holds
    # for 0 and 11 alike. A coin set to 0 thus behaves exactly as before -
    # which is why it does NOT stand in KEEP_OCB either.
    #
    # What is really lost is only the staging: it lay on an ornamental lid
    # in the water (room 97, right next to the water witch in room 99).
    # Exactly that is what the user is now checking in game - including the
    # question what the witch does when nobody has to climb into her pool
    # anymore.
    'andy3'   = @()
    # rich2: the HK (typeId 345, item 6 in room 8). It is no find but a
    # deposit - Lara lays it down at mesh 15 to disarm herself so the X-ray
    # scanner in room 9 sets off no traps. The item accordingly carries
    # flags 256 (invisible) and OCB 4 (pedestal); on moving, the OCB falls
    # to 0 and the deposit would become an ordinary pickup.
    'rich2'   = @(345)
    # rich3's crossbow ammo (typeId 342). There are exactly THREE pieces in
    # the whole level - items 10 (room 2), 82 (room 79) and 209 (room 188)
    # - and of typeId 343 not a single one. The user wanted three each in
    # the crossbow and in the combo island; that cannot be distributed with
    # three, it would take three injections. Until he has decided on a
    # solution, all three stay where they are - the safe direction, because
    # wrongly distributed ammo is a softlock, ammo left in place is not.
    'rich3'   = @(342)
}
$vanillaTypes = @( if($VANILLA_TYPES.ContainsKey($LevelName)){ $VANILLA_TYPES[$LevelName] } )
$fixedItems = @()
if ($FIXED_ITEMS.ContainsKey($LevelName)) {
    foreach($kv in $FIXED_ITEMS[$LevelName].GetEnumerator()){
        $fixedItems += [PSCustomObject]@{ item=[int]$kv.Key; reason=[string]$kv.Value }
    }
}

# Items whose pickup trigger TRAVELS ALONG instead of pinning them.
#
# Normally a pickup trigger nails its item down: the trigger sticks to the
# sector, the item would wander off without it and its effect would stay at
# the old spot. If an item stands here, both are moved together - one
# operation, not two.
#
# This is curation and expressly NO general rule. The count across all 13
# levels showed that of 2044 items only two would qualify at all; the user
# thereupon closed the item branch and released exactly this one case
# ("Andre1 the Key107, there we only need to move the trigger along",
# 13.08.2026).
#
# Do not add without checking:
#   - Does the trigger carry a blocking command (flipmap, antitrigger,
#     heavy, cutscene)? Those expect room-specific circumstances.
#   - Does the item have a special OCB? That usually describes the old
#     spot.
# The trigger's sector is NOT maintained here but read from the level data
# at runtime - a hand-maintained coordinate would be the one entry that
# silently goes wrong at the next level rework.
$COUPLED_TRIGGERS = @{
    'Andrea1' = @{ 107 = 'key - the trigger additionally wakes dog (26) and Flame Emitter2 (108)' }
}
$coupledTriggers = @()
if ($COUPLED_TRIGGERS.ContainsKey($LevelName)) {
    foreach($kv in $COUPLED_TRIGGERS[$LevelName].GetEnumerator()){
        $coupledTriggers += [PSCustomObject]@{ item=[int]$kv.Key; reason=[string]$kv.Value }
    }
}

#----------------------------------------------------------------------------
# Items whose codebits are preset - the third way besides "leave in place"
# and "take the trigger along".
#
# The case that led to it (Andrea2 item 62, Puzzle Item4): on its sector
# lies a HeavyTrigger with Object(62), and on the same position stands the
# Guardian (item 51). User: "That is a protection so the Guardian's death
# fires the heavy trigger and sets the pickup to 11111." The puzzle piece
# is thus only unlocked by the Guardian's death.
#
# A HeavyTrigger cannot be taken along - it needs something heavy on
# exactly this tile, and that is the dying Guardian. Instead of nailing the
# item down forever because of that, the curation relieves it of the
# condition: codebits to 11111 right at the start, then it is there from
# the beginning and may travel. User decision: "We don't care, set it to
# 11111 and off into the pool."
#
# Per entry: item index -> @{ flags = <value>; reason = <reasoning> }.
# 31 is 11111 in binary, i.e. all five codebits.
$PRESET_FLAGS = @{
    'Andrea2' = @{
        62 = @{ flags = 31; reason = 'Puzzle Item4 - the HeavyTrigger hangs on the death of the Guardian (item 51); codebits preset so it may travel' }
    }
    'andy1' = @{
        8 = @{ flags = 31; reason = 'the heart - OCB 5 makes it invisible and unpickable (InitialisePickup, PickUpCollision:783); with the preset and a zeroed OCB it lies there visible, so its finished KeyItemRange can finally be used' }
    }
}
$flagPresets = @()
if ($PRESET_FLAGS.ContainsKey($LevelName)) {
    foreach($kv in $PRESET_FLAGS[$LevelName].GetEnumerator()){
        $flagPresets += [PSCustomObject]@{ item=[int]$kv.Key; flags=[int]$kv.Value.flags; reason=[string]$kv.Value.reason }
    }
}

#----------------------------------------------------------------------------
# ENEMIES that may be ADDED to the level.
#
# Hand curation, deliberately empty until checked. An enemy only belongs in
# here once it has proven in game that it behaves without a level script.
# Larson is the yardstick for that: he looks like an ordinary enemy in the
# data but cannot be killed at all without OneShot on the trigger and
# stayed standing after death. Both only showed in game, not while reading
# the file.
#
# Per entry: typeId -> reason/proof. The randomizer sets each of these
# types into the level with its own OneShot trigger (see addTrigger).
#
# NEVER add (user requirement): Scientist, Guardian. The chef has been in
# the cross pool since #124 (kCross joby3, baseSlot 460, borrowed soldier
# death), the Hydra since 2026-08-15 - both run through the cross
# machinery, not through this hand curation list.
# Before adding, check:
#   - Does he need AI objects (AI_GUARD, AI_AMBUSH, ...)? Those do not
#     exist elsewhere.
#   - Does his death hang on a level script or a cutscene?
#   - Does his corpse fade out? (anim command 4 in the death animation,
#     Diagnose\AnimCmd.exe shows it.)
#   - Does he need water, ceiling height, a track?
#----------------------------------------------------------------------------
# The user's list of 13.08.2026 ("Gegner Pool.txt" - the enemy pool),
# restricted per level to
# the types whose model is also loaded in THIS level. An enemy type the
# level does not know cannot be placed - that would first need the object
# transfer between levels (as with the weapon set).
#
# Checked with Diagnose\SlotList.exe <level.trc> (lists all object
# slots). One deviation from the list: DOG (41) lies ONLY in Andrea1, not
# in the joby levels.
#
# HARD RULE, learned from the Pierre case: an enemy type may only go into a
# level in which it has AT LEAST ONE vanilla instance.
#
# "Model loaded" does not suffice. A level only carries the sound samples
# it needs itself - if the enemy never occurs in it, they are missing.
# Pierre is the proof: his model lies with 15 meshes in Andrea2 AND
# Andrea3, but he has no instance in ANY of the 13 levels, and his shot
# sample (SFX 90) is present in not a single level. In game he shoots
# silently.
#
# Verify with Diagnose\Tr5Sound.exe: it reads EXACTLY each enemy's
# animation block (the limit stands in the next slot's anim_index, a slot
# has no count of its own) and looks the SFX up in the level's sample_lut.
$ENEMY_TYPES = @{
    'Andrea1' = @{
        41 = 'Dog'
        45 = 'Larson - checked in game: stays, pursues, dies, fades out'
    }
    'Andrea2' = @{
        45 = 'Larson - caution: item 76 is the scripted miniboss and stays untouched, these here are additional'
        47 = 'Pierre - checked in game. EXCEPTION to the instance rule, needs ENEMY_SOUND_FIX'
        61 = 'Roman god (Soldier Statue, sword_green)'
        # 63 = 'Hydra - used to stand on "never randomize"'
    }
    'Andrea3' = @{
        47 = 'Pierre - checked in game. EXCEPTION to the instance rule, needs ENEMY_SOUND_FIX'
        57 = 'Lion'
        59 = 'Gladiator'
        61 = 'Roman god (Soldier Statue, Hammer_blue)'
    }
    # Imp checked in game (andy1): falls somewhat oddly to the ground but
    # fades out. The odd fall is explained by the data - his death
    # animation is anim_index + 45 (imp.cpp) and in andy1 a stub with
    # frames 0..0 and no commands.
    'andy1'   = @{ 89 = 'Imp - checked in game: falls oddly but fades out' }
    'Andy2'   = @{ 77 = 'Maze Monster - marked by the user as "test whether usable"' }
    'andy3'   = @{ 89 = 'Imp - checked in andy1, not here yet' }
    'joby2'   = @{
        37 = 'Guard (Blue Guard)'
        # Released by the user 17.08.2026. The Huskie is an enemy of its
        # OWN, not the dog (41): its own control, 24 instead of 18 hit
        # points, 26 instead of 21 meshes. joby2 is the only level with a
        # model in slot 85 and has three vanilla instances (items 4, 86,
        # 153) - the hard instance rule above is thereby satisfied.
        85 = 'Huskie - the dog from joby2, its own type with its own control'
    }
    'joby3'   = @{
        37 = 'Guard (Blue Guard)'
        49 = 'Soldier MG (Mafia)'
        51 = 'Soldier Pistols (Mafia2)'
    }
    'joby5'   = @{
        # 37 = 'Guard - OUT: joby5 has zero vanilla instances, and it lacks
        #       samples 72, 74, 82, 85. Same situation as Pierre in Andrea3.
        49 = 'Soldier MG (Mafia)'
        51 = 'Soldier Pistols (Mafia2)'
    }
    'rich1'   = @{
        37 = 'Guard (Blue Guard)'
        39 = 'Two Gun'
    }
    'rich2'   = @{
        37 = 'Guard (Blue Guard)'
        39 = 'Two Gun'
    }
    'rich3'   = @{
        33 = 'SWAT'
        35 = 'Advanced SWAT (SWAT Plus)'
        39 = 'Two Gun'
    }
}
$enemyTypes = @()
if ($ENEMY_TYPES.ContainsKey($LevelName)) {
    foreach($kv in $ENEMY_TYPES[$LevelName].GetEnumerator()){
        $enemyTypes += [PSCustomObject]@{ typeId=[int]$kv.Key; reason=[string]$kv.Value }
    }
}

#----------------------------------------------------------------------------
# Sound samples an enemy lacks in the target level - and where they come
# from.
#
# The rule above says: an enemy only into a level in which it has vanilla
# instances. Pierre is the reasoned exception. He has an instance in NONE
# of the 13 levels, but his model lies completely in Andrea2 and Andrea3 -
# he is a cutscene actor there. What he lacks are exactly his samples, and
# those can be copied over. His shot sample (map 90) was never shipped by
# Core at all; the user picked matching replacements from joby5 in trview's
# soundboard, checked in game.
#
# IMPORTANT: whoever takes an enemy with an entry here into the pool
# promises something placement alone cannot deliver - the patch path must
# copy the samples along (task #67). Without that the enemy stands mute in
# the level.
#
# typeId -> list of: Map (the missing SFX number), FromLevel, FromMap, What.
$ENEMY_SOUND_FIX = @{
    47 = @(
        @{ Map = 90; FromLevel = 'joby5'; FromMap = 45;  What = 'shot' },
        @{ Map = 91; FromLevel = 'joby5'; FromMap = 123; What = 'death' }
    )
    # Larson: 96 and 98 sit in his death animation (rel15, frames 283 and
    # 340) and are missing in Andrea1 AND Andrea2 - in vanilla too, it just
    # never became audible because he never dies there (flight at 40 HP).
    # Since the OneShot trigger he dies, so he needs the sounds. The
    # replacements were picked by the user in trview's soundboard
    # (14.08.2026).
    45 = @(
        @{ Map = 96; FromLevel = 'rich1'; FromMap = 85;  What = 'death' },
        @{ Map = 98; FromLevel = 'rich1'; FromMap = 143; What = 'death' }
    )
}
$enemySounds = @()
foreach($t in @($enemyTypes | ForEach-Object { [int]$_.typeId })){
    if (-not $ENEMY_SOUND_FIX.ContainsKey($t)) { continue }
    foreach($e in $ENEMY_SOUND_FIX[$t]){
        $enemySounds += [PSCustomObject]@{ typeId=$t; map=[int]$e.Map; fromLevel=[string]$e.FromLevel;
                                           fromMap=[int]$e.FromMap; what=[string]$e.What }
    }
}
#----------------------------------------------------------------------------
# Secrets stay where they are for now.
#
# Which type is a secret does not stand in the object table but in the
# engine: newinv2.cpp, case PICKUP_ITEM4 increments savegame.Level.Secrets.
# That is typeId 223 and holds game-wide, independent of the level.
# Together there are 36 - exactly the number the loading screen shows.
#
# The user wants to treat the secrets separately later: an own list of
# hand-picked hiding spots plus a switch in the tool that takes them into
# the pool. Until then they must not travel along - a secret lying openly
# in the corridor is none anymore, and the niche finds (Andy2 item 142 with
# OCB 1 in room 163) would be lost.
#
# VANILLA_TYPES alone does NOT suffice for that: it only steers the
# constraints, but the randomizer moves every pickup that is not pinned.
# Only the entry in fixedItems really holds them.
#----------------------------------------------------------------------------
$SECRET_TYPE = 223
foreach($it in ($d.items | Where-Object { [int]$_.typeId -eq $SECRET_TYPE })){
    $fixedItems += [PSCustomObject]@{ item=[int]$it.index; reason='secret - stays in place until the dedicated secret list' }
}

#----------------------------------------------------------------------------
# VANILLA_TYPES must really hold the items.
#
# It did not. The entry only kept a type out of the keyConstraints - i.e.
# out of the pool and out of the test levels. But the randomizer moves
# every pickup that is not in fixedItems, and a type without a constraint
# simply counts as an ordinary item to it. The entries were thereby pure
# documentation.
#
# It surfaced at joby3: pistols (334), Key Item6 at the chef (201) and the
# disguised crowbar (172) have stood in VANILLA_TYPES there forever, and
# all three were moved in every seed - including the chef glitch the entry
# was meant to prevent. That rich1's CD stayed put anyway was owed solely
# to its additional FIXED_ITEMS entry.
#
# Both stay side by side and mean different things: VANILLA_TYPES is "this
# TYPE stays where it is", FIXED_ITEMS is "this one ITEM stays where it
# is". The reason is written along so the report shows which entry holds
# an item.
#----------------------------------------------------------------------------
$fixedAlready = @($fixedItems | ForEach-Object { [int]$_.item })
foreach($it in ($d.items | Where-Object { $vanillaTypes -contains [int]$_.typeId })){
    if ($fixedAlready -contains [int]$it.index) { continue }
    $fixedItems += [PSCustomObject]@{ item=[int]$it.index; reason="VANILLA_TYPES: type $($it.typeId) stays at its vanilla position" }
}

$extraPool = @{}
if ($EXTRA_POOL_TYPES.ContainsKey($LevelName)) {
    foreach($kv in $EXTRA_POOL_TYPES[$LevelName].GetEnumerator()){ $extraPool[[int]$kv.Key] = [int]$kv.Value }
}
$onewayOut = @( if($ONEWAY_OUT.ContainsKey($LevelName)){ $ONEWAY_OUT[$LevelName] } )

# Fenced areas: a contiguous building/courtyard complex that in game can be
# entered EXCLUSIVELY through certain doors, but that the graph wrongly
# reaches earlier via portal rim edges (fences, door lintels, mezzanine
# portals). The inner area is computed at runtime as a connected component
# from the seed (without entering barrier rooms, without crossing the gate
# sectors); afterwards every edge from outside to inside that does not run
# through the gate sectors is cut. Requirement verified by the user in
# trview for Andrea1: "from the gate 53/54 on, everything behind it may
# contain no key item positions" - the red box on his map is exactly this
# complex, the double gate 53/54 (room 112) the only legal access. A full
# height/physics model would detect this automatically but is parked (see
# -EnableClimbModel).
# Rooms = the explicit list of rooms behind the gate. Three automatic
# approaches were measured and discarded; the reasoning belongs here so
# nobody repeats them blindly:
#  1. Island flood from the courtyard: fails on principle because the area
#     is only sealed ON ONE SIDE - in only through the gate, out by jumping
#     off at many spots (room 117 alone borders 112,113,116,118,119,144,
#     150,156). A flood follows the jump-offs outward and eats the level.
#  2. Barrier room (street 112) as the stop: the flood immediately found
#     other ways out (117->150->130->113->120->114->...).
#  3. Elevation as the separator: refuted - room 150 (outside) lies at
#     yBottom -6144 like 145/165/166/181 (inside), room 113 (outside) at
#     -2048 like 118/119 (inside).
# Only a complete height/climb model (-EnableClimbModel) would solve this
# cleanly, and it lacks reliable floor semantics at portals. The list is
# transparent for that, directly readable in trview and correctable per
# room with one number.
# The solution came from the user by INVERTING the question: instead of
# guessing the inner rooms (failed, see above), he names the rooms that are
# still walkable BEFORE the gate. They form a closed stop edge against
# which a flood from the courtyard runs - the inner area thereby results
# automatically and completely (Andrea1: 65 rooms, 1499 sectors, no escape
# route left).
# PreGateRooms expressly stay valid for placements; room 217 is included
# because per the user it is to be excluded in general and as a stop has
# the same effect anyway. Pure "air" rooms above these rooms are per the
# user irrelevant. The emergency hard stop would be 114/120.
$GATED_AREA = @{
    'Andrea1' = @( @{
        Doors        = @(53,54)
        SeedRooms    = @(117)
        PreGateRooms = @(112,132,127,124,158,114,120,163,159,164,139,170,217)
    } )
}
$gatedAreas = @( if($GATED_AREA.ContainsKey($LevelName)){ $GATED_AREA[$LevelName] } )

# Doors no trigger in the level switches, because the engine opens them
# from level-specific script code (game/deltapak.cpp calls FlipMap() and
# TestTriggers() directly there - e.g. cranecut_control -> FlipMap(1), the
# crane cutscene). Such openings stand nowhere in the level data and are
# fundamentally not derivable from it. Whoever enters here tells the
# analyser: this door opens in the course of play, treat it as opened.
# The diagnosis "no trigger in the level switches this door" lists the
# candidates.
# Important: NOT "always open" but tied to a condition - reach the room in
# which the script event is triggered. Opening a script door wholesale
# would be worse than ignoring it: the analyser would allow laying an
# early-needed key item behind a door that in reality only opens at the
# end of a long chain - the randomizer then produces unsolvable seeds.
# joby2: Key 7 -> door -> keycard from the guard -> one level up -> jump
# over the crates -> crane room (104) -> flipmap frees the way.
# No entry verified yet. joby2 138/141 stood in here briefly and seemingly
# lifted the level from 1 to 90 percent - wrong: no key/switch trigger in
# joby2 switches them, so they never open and Lara walks past them. They
# merely happened to stand at the edge of the reached area. joby2 in truth
# stops at the geometry (the jump over the crates), see MANUAL_LINK.
# Note: "door without a trigger" first means "door never opens", not "door
# is opened by script" - assume the latter only after evidence.
#
# joby3, user decision (not proven from data but deliberately set as a
# randomizer rule): the chef in room 120 is to be killed by stealth kill
# (proximity-based, game/deltapak.cpp stealth3_start/end - see the analysis
# further up, technically not derivable from sector/item data). Since
# killing the chef is mandatory once room 120 is reached, the two doors
# behind him count as safely passable as soon as room 120 is reached:
#   door 190 (room 124) <- Key Item6 (typeId 201), the chef's vanilla drop
#   door 175 (room 106) <- Key Item7 (typeId 202), lies behind door 190 in room 107
# Placement rule for the randomizer: Key Item6 stays bound to/near the chef
# (the zone of room 120), Key Item7 may lie up to and including the zone of
# room 120 - both doors count as walkable from there.
$SCRIPT_DOORS = @{
    'joby3' = @{ 190 = 120; 175 = 120 }
    # rich1: the scientist (item 85, room 34, OCB 9) has an aim-at cutscene -
    # aiming at him longer opens door 84 directly (the easy way). Kill him
    # instead and a detour is needed. Same class as the chef in joby3:
    # proximity/behavior-based, not derivable from sector/item data.
    # User decision, analogous to joby3: room reached = door counts as open.
    'rich1' = @{ 84 = 34 }
    # Andrea3: the Roman god (item 79, typeId 61, room 90, sector 2,3)
    # stands exactly on the HeavyTrigger that opens door 103 (Object:103,
    # mask 31) - he literally falls onto his own trigger when dying. Unlike
    # chef/scientist this is no stealth/aim-at special case but a normal
    # fightable boss (no known immortality bug); the rule still models the
    # same case, because an NPC death cannot be modeled as "Lara reaches
    # sector X". The chain afterwards per the user: door 103 -> Key Item2
    # (room 119) -> key lock room 102 -> door 77 (room 89) -> level end
    # area (EndLevel trigger, room 70).
    'Andrea3' = @{ 103 = 90 }
}
$scriptDoors = @{}
if ($SCRIPT_DOORS.ContainsKey($LevelName)) { $scriptDoors = $SCRIPT_DOORS[$LevelName] }

#----------------------------------------------------------------------------
# Flipmap rooms.
#
# A flip room is no additional place but a STATE of the same place. The
# sector data holds both versions side by side, and whoever simply lays
# them on top of each other gets geometry that never exists in game: every
# wall of the base AND every passage of the flip variant, all at once, all
# from level start.
#
# Exactly there joby2 would have produced a certain softlock. The crane
# switches the roll gate open via FlipMap (base 89/87/80 shut, flip
# variant 53/54/69 open). The flood ran through the flip variant, so the
# whole area behind it counted as reachable from zone 0 - and Key Item8 was
# allowed to lie there. Only, the flipmap is itself triggered by Key8.
# Whoever lays the key behind the gate it is supposed to open has made the
# level unsolvable.
#
# Hence the rule: sectors of a flip variant do NOT carry. That is the
# conservative direction - it can only shrink reachability, never invent a
# way, and a too-small area shows in the report, a too-large one only in
# game. The user on it: "Flipmaps can often block ways."
#
# Where a flip variant really opens a way, it is released here - same form
# as $SCRIPT_DOORS: a reached room as the condition, not "always open".
# Before that it counts as closed, after it a new zone begins.
#----------------------------------------------------------------------------
$FLIP_GATE = @{
    # andy1, the church. Base 48 -> variant 214, base 151 -> variant 213.
    # The timber blocks the largest part of the level. Lara builds a
    # slingshot from the two halves Puzzle Item2 Combo1 (item 18, room 82)
    # and Combo2 (item 39, room 172); with it a cutscene lets the bells
    # crash down, which blast the timber away - only after that is the rest
    # of the level walkable. Hence NeedTypes and not AfterRoom: the church
    # room alone does not suffice, Lara stands there long before she has
    # the parts together.
    'andy1' = @( @{ Rooms = @(213,214); NeedTypes = @(182,183); Why = 'the slingshot lets the bells fall' } )
    # joby2, the roll gate at the crane. The flipmap behind it is triggered
    # by Key Item8 - if the key lies behind the gate it is supposed to
    # open, the level is unsolvable. Set as a deadstop: room 89 and
    # everything behind it form their own island group, reachable only from
    # the crane on.
    # What is barred is the TRACK BED CHAIN, not only the room behind the
    # gate. The track bed lies 256 units lower than the roll gate, so the
    # flood simply runs underneath it: 132 -> 5 -> 4 -> 2 and from there
    # into everything behind the gate. Barring only room 89 left this
    # detour open. 2, 4 and 5 themselves have not a single placeable
    # sector - as a bar they cost nothing and cut the detour at its source.
    'joby2' = @( @{ BlockRooms = @(2,4,5,89); AfterRoom = 35; Why = 'roll gate at the crane' } )
    # rich1: the second, hard way to the hammer, explained by the user. At
    # door 153 in room 85 sits a lock that looks as if it could be shot
    # open. Do it, and the shell casing fires the HeavyTrigger in room 89 -
    # it switches FlipMap 0, and Lara falls into the room with the Twogun.
    # That is no bug but the alternative to the hammer.
    #
    # The release therefore hangs on room 85: before it Lara cannot shoot,
    # after it the way stands open to her. Groups 1 and 2 (rooms 106 and
    # 107) hang on their own HeavyTriggers in 101 and 102 and get their own
    # condition.
    'rich1' = @(
        @{ Rooms = @(86,94,95,122,124); AfterRoom = 85;  Why = 'shoot the fake lock at door 153 - hard way to the hammer' }
        @{ Rooms = @(106);              AfterRoom = 101; Why = 'HeavyTrigger in room 101' }
        @{ Rooms = @(107);              AfterRoom = 102; Why = 'HeavyTrigger in room 102' }
    )
    # rich3, all seven groups explained by the user. The conditions do not
    # stand there guessed but come from the trigger data - which sector
    # sets which FlipMap is counted in the level.
    #
    # Group 0 (room 18, base 10) is deliberately missing here: per the user
    # it only changes the looks of the staircase around Lara's start. It
    # thus stays barred, and that costs nothing.
    #
    # Group 7 is missing as well, for the opposite reason: it opens no way
    # the base does not already have, so it does not appear in the report.
    # It is the gas area around room 149, with which the second cyborg
    # (item 183) is killed - mechanics without path effect.
    'rich3' = @(
        # Grows the staircase by new rooms, among them room 207 with the
        # secret. FlipMap 1 is set in room 12 (4,2) and room 119 (5,7);
        # room 12 is itself the base of flip room 45, i.e. the earliest
        # spot where Lara can trigger it.
        @{ Rooms = @(43,45);   AfterRoom = 12;  Why = 'the staircase grows - room 207 with the secret' }
        # Connects the staircase via room 61 to the level part in which the
        # crossbow lies (room 79). FlipMap 3 is set among others in room 26
        # - the base of this flip room.
        @{ Rooms = @(13);      AfterRoom = 26;  Why = 'connection to room 61, from there to the crossbow' }
        # The big group: nine rooms leading the staircase via room 57 into
        # the rest of the level. The user: for that Lara FIRST needs the
        # grappling gun - in the level that is the crossbow (typeId 341,
        # items 75 and 76 in room 79). Hence NeedTypes and not AfterRoom:
        # the room alone does not suffice, as with andy1's slingshot.
        @{ Rooms = @(9,20,28,32,48,49,50,53,54); NeedTypes = @(341); Why = 'the way via room 57 - needs the crossbow from room 79' }
        # The two shooting ranges. They open the grates behind which the
        # level continues; Lara must hit moving targets there. FlipMap 4
        # and 5 are both set from room 46, plus 170 and 169.
        @{ Rooms = @(74);      AfterRoom = 46;  Why = 'shooting range 1 - opens the grates in room 73, way through door 284' }
        @{ Rooms = @(87);      AfterRoom = 46;  Why = 'shooting range 2 - door 86 to the secret in room 78' }
        # The cyborg. Shooting damages him, but he cannot be killed that
        # way - the flipmap puts his room partly under water, then there is
        # a short circuit. FlipOn 6 stands in room 183 (4,5), i.e. in his
        # own room; Lara must be there to shoot.
        @{ Rooms = @(190,192); AfterRoom = 183; Why = 'cyborg 200 - the room floods, short circuit' }
    )
}
$flipGates = @( if($FLIP_GATE.ContainsKey($LevelName)){ $FLIP_GATE[$LevelName] } )

#----------------------------------------------------------------------------
# Island groups: floor height as a limit for keys.
#
# A sector is far from being a reachable placement spot. In joby2, crate
# stacks stand in room 84; their tops are walkable sectors, but Lara only
# gets up there at all AFTER she has Key Item8. A key on top is thereby
# unreachable - exactly the case the user showed at item 1480.
#
# Instead of excluding such spots one by one, one room of the group sets
# the floor height, and keys may lie no more than MaxYAbove above it. That
# saves a lot of hand work and carries immediately for the whole group.
#
# Y CONVENTION: in TR, SMALLER y lies HIGHER. "At most 1024 higher" is thus
# floorY >= baselineY - 1024, not <=.
#
# RequireTypes is additionally an assurance, not a mere filter: of each of
# these types at least one instance must lie WITHIN the group, so it stays
# solvable out of itself.
#----------------------------------------------------------------------------
# Types that should go into the pool although no lock forces them - and
# that have NO zone limit in the process. joby2: the fuse (Puzzle Item1)
# behind doors 105/106. It is progress but no bound for anything earlier,
# so it may lie anywhere. User decision.
# Without an entry such a type does not appear in the keyConstraints at
# all and would thereby stay invisible in the test level and untouched in
# the randomizer.
#----------------------------------------------------------------------------
# Rooms in which no weapon may lie.
#
# joby3: the pistols must stay in room 107 or the chef glitch occurs -
# VANILLA_TYPES handles that. But it does not suffice: VANILLA_TYPES nails
# one type to its place but keeps no OTHER type from wandering there. A
# shotgun randomly placed in the chef area triggered the same glitch. User
# statement for the chef rooms: 105, 106, 120, 124, 125.
#
# Barred is 334-348, i.e. weapons and their ammo. Medipacks (349/350),
# lasersight (351), binoculars (352), flares (354/355) stay allowed - no
# weapons. Type 353 "Silencer" deliberately does not stand in the list: it
# occurs in none of the 16 levels, so it is an unused engine slot.
#----------------------------------------------------------------------------
#----------------------------------------------------------------------------
# Level-wide height limit for spots.
#
# joby4 is a diving level, and the user measured in game: Lara gets
# nowhere higher than about y 13200. That is no engine limit - for
# vertical force the code only knows the sinks (laraswim.cpp:721, they pull
# Lara to their own height) - but the water surface. Above it, at yBottom
# 13056, the next room layer begins, into which she no longer ascends from
# the water: 21 rooms there, another 27 above.
#
# Unlike the island group in joby2 this is no property of a group and
# holds for ALL types, not only keys. And unlike first assumed it is not
# room-clean - room 72 for instance has floors at 11008, 12032 and 12544,
# so it lies partly above and partly below the line. A room list cannot
# express that, hence here as a number.
#
# SMALLER y lies HIGHER: the condition is floorY >= limit.
#----------------------------------------------------------------------------
$MIN_FLOOR_Y = @{
    'joby4' = 13312
}
$levelMinFloorY = $null
if ($MIN_FLOOR_Y.ContainsKey($LevelName)) { $levelMinFloorY = [int]$MIN_FLOOR_Y[$LevelName] }

#----------------------------------------------------------------------------
# Minimum clearance between floor and ceiling.
#
# The exporter cuts off at 512 by itself - no level has a spot below that.
# The user checked joby4 room 123 sector (3,3): floorY 15616, ceilingY
# 15104, i.e. exactly 512, and Lara does not get there. 768 is the
# engine's crawl height and thus the next sensible step.
#
# DELIBERATELY NOT GLOBAL. The cross-check over all levels finds 25
# vanilla pickups on sectors with clearance < 768 - among them joby3's Key
# Item6 in room 120 at 256 and in rich2 an HK at 0. Reason: clearance
# describes the SECTOR, not the item. A pickup has its own y coordinate
# and can lie on a ledge, in a niche or on a pedestal where the sector
# floor meets the ceiling. The clearance is thus no reliable proxy for
# "Lara can get there".
#
# joby4 does NOT occur in this hit list - no vanilla pickup lies below 768
# there. Only for that reason does it stand here. Every further level
# needs the same proof, and for joby3, joby5, rich2 and rich3 it provably
# fails.
#----------------------------------------------------------------------------
$MIN_CLEARANCE = @{
    'joby4' = 768
}
$levelMinClearance = $null
if ($MIN_CLEARANCE.ContainsKey($LevelName)) { $levelMinClearance = [int]$MIN_CLEARANCE[$LevelName] }

#----------------------------------------------------------------------------
# Rooms in which no key or puzzle item may lie.
#
# joby5 has electrified water. Four electrical cables (type 98, items 91,
# 136, 165, 206) hang into the water; three floor triggers in rooms 15, 60
# and 138 arm them, switch 190 in room 165 switches them off again. While
# they are active, staying there costs 10 hit points per frame and kills
# at <= 32 (game/joby.cpp, ControlElectricalCables).
#
# The reach is not the cable's room - decisive is the FlipNumber of the
# touched water mass, see joby.cpp:191. Which rooms those are therefore
# does not stand in the level data; the mapping comes from the user:
# cable 91 -> room 128, cable 136 -> 126 and 127, cable 206 -> 183,
# cable 165 -> 158.
#
# User decision: NORMAL items may lie there, but no keys. The reason is
# obvious - a normal pickup Lara fetches after the switch, a key by
# contrast can be part of a chain needed earlier.
#----------------------------------------------------------------------------
$NO_KEY_ROOMS = @{
    'joby5' = @(126,127,128,158,183)
}
$noKeyRooms = @( if($NO_KEY_ROOMS.ContainsKey($LevelName)){ $NO_KEY_ROOMS[$LevelName] } )

$WEAPON_TYPES = 334..348

$NO_WEAPON_ROOMS = @{
    'joby3' = @(105,106,120,124,125)
}
#----------------------------------------------------------------------------
# Individual TYPES that may not lie in certain rooms - unlike
# NO_WEAPON_ROOMS, which bars all weapons at once, and unlike
# TYPE_EXCLUDE_ROOMS, which only steers the relocation of existing items:
# this bar holds for EVERY placement path (moving, weapon scattering, item
# abundance).
#
# rich2: ONLY the HK may not go into the start island. The user checked in
# game that the X-ray scanner in room 9 reacts exclusively to it - Lara
# lays it down at mesh 15 in room 8 and passes unarmed. The scanner does
# NOT respond to Uzi, shotgun or Deagle, so those may lie there; without
# them the start island with its many enemies would have to be survived
# with pistols only. HK ammo (346) is useless without the weapon and stays
# allowed.
#----------------------------------------------------------------------------
$TYPE_NO_ROOMS = @{
    'rich2' = @{ 345 = $RICH2_START_ISLAND }
}

#----------------------------------------------------------------------------
# Items that should become immune to their antitrigger (flag bit 0x80,
# IFL_ANTITRIGGER_ONESHOT - TestTriggers then skips them,
# control.cpp:2378). The trigger stays fully intact and keeps acting on
# all other targets.
#
# rich2 door 171 (room 118, type 300): opened by a HeavyTrigger in room
# 118, shut again by the antitrigger in room 18 - which carries ONLY this
# one pointer. As long as the start island was weapon-free, that did not
# matter. Now a weapon may lie there, and with it a player can open the
# door earlier than intended; walk across room 18 afterwards and it falls
# shut again. With the immunity bit it stays open once it was open.
#----------------------------------------------------------------------------
$ANTITRIGGER_IMMUNE = @{
    'rich2' = @(171)
}
$typeNoRooms = @(
    if ($TYPE_NO_ROOMS.ContainsKey($LevelName)) {
        foreach ($t in $TYPE_NO_ROOMS[$LevelName].Keys) {
            [PSCustomObject]@{ typeId = [int]$t; rooms = @($TYPE_NO_ROOMS[$LevelName][$t]) }
        }
    }
)
$antitriggerImmune = @( if ($ANTITRIGGER_IMMUNE.ContainsKey($LevelName)) { $ANTITRIGGER_IMMUNE[$LevelName] } )
foreach ($e in $typeNoRooms) {
    Write-Output "type bar: typeId $($e.typeId) is barred in $($e.rooms.Count) rooms of $LevelName"
}
if ($antitriggerImmune.Count -gt 0) {
    Write-Output "antitrigger immunity: $($antitriggerImmune.Count) item(s) in $LevelName"
}
#----------------------------------------------------------------------------
# Equipment a switch presupposes.
#
# A shoot switch stands in the level data only as a switch - that it needs
# a certain weapon stands nowhere. Andrea1: the bell (item 75) can only be
# shot down with revolver/Desert Eagle plus lasersight. Item 72, by
# contrast, is NOT gated - it can be hit with the pistols too by aiming
# while kneeling (confirmed by the user in game).
#
# Evaluated before the constraint build: the zone in which the switch is
# needed limits the named types. Before, this only stood in the text
# report, and the randomizer was allowed to lay the equipment behind the
# switch it is meant to operate.
#----------------------------------------------------------------------------
$EQUIP_GATE = @{
    'Andrea1' = @{ 75 = @{ Need = @(347,351); Desc = 'revolver + lasersight (shoot down the bell)' } }
}

# Whole levels without any weapon: none is currently listed. rich2 stood
# here and is now solved room-wise (see NO_WEAPON_ROOMS) - the bar on the
# whole level would have cost it, as the only level, the ammo for the
# imported weapon set.
$NO_WEAPON_LEVELS = @()
$noWeaponRooms = @( if($NO_WEAPON_ROOMS.ContainsKey($LevelName)){ $NO_WEAPON_ROOMS[$LevelName] } )
if ($NO_WEAPON_LEVELS -contains $LevelName) {
    $noWeaponRooms = @($d.rooms | ForEach-Object { [int]$_.index })
    Write-Output "weapon bar: no weapon may lie in the whole level $LevelName ($($noWeaponRooms.Count) rooms)"
}
elseif ($noWeaponRooms.Count -gt 0) {
    Write-Output "weapon bar: $($noWeaponRooms.Count) rooms in $LevelName are weapon-free"
}

$POOL_TYPES = @{
    'joby2' = @(172)
    # andy3 stood here briefly with 172 and 174 because the level has not a
    # single lock and the progress chain otherwise does not appear in the
    # keyConstraints at all. The user then decided for vanilla - see
    # VANILLA_TYPES. The entry stays as a comment because the thought still
    # holds should the puzzles ever travel after all:
    #   'andy3' = @(172,174)   together with TYPE_EXCLUDE_ROOMS further down
    # rich1: the hammer (Key Item7, typeId 202, item 35 in room 22). It has
    # no lock - its door 153 opens via the hamgate cutscene -, so without
    # this entry it appears in no constraint. But it needs one, because
    # "not behind its own door" holds for it as for every key.
    #
    # The limiting is done by the island group above (flood from Lara,
    # deadstop room 12), not the zone. The derived door rule on its own
    # finds only a single sector - that of door 153 itself - because the
    # flood comes around it; from the model's view nothing lies behind it.
    # A zone limit would with 150 spots be three times as loose as the
    # island and would moreover wrongly forbid seven island sectors from
    # zone 1. Hence here without a zone limit.
    'rich1' = @(202)
    # rich2: the three types the user wants in the start area - Pickup
    # Item2 (221), Bottle (235) and Cloth (236). None has a lock, so
    # without this entry no constraint object arises at all on which the
    # island group could hang - the island was dutifully computed and ran
    # into the void. The same trap as with rich1's hammer.
    'rich2' = @(221,235,236)
    # rich3: the crossbow (typeId 341, items 75 and 76 in room 79). In game
    # it is the grappling gun and thus a progress item - FlipMap 2, the way
    # via room 57 into the rest of the level, hangs on it (see FLIP_GATE).
    # It has no lock, so it needs this entry for a constraint to arise at
    # all on which its island group can hang. The same trap as rich1's
    # hammer and rich2's bottle.
    'rich3' = @(341)
    # joby3: the four combo halves. Puzzle Item3 Combo1+2 yield the
    # breathing apparatus, Puzzle Item4 Combo1+2 the battery; both go into
    # the suit and trigger the final cutscene. The two slots in room 40 lie
    # on top of each other and only feign combinability.
    # User: the level has no point of no return, the parts are purely for
    # the end - they may therefore appear anywhere.
    'joby3' = @(184,185,186,187)
    # joby5: Puzzle Item1 and Item2 open the level's end. Slot 1 opens slot
    # 2, slot 2 triggers the end. That also explains why the trigger at
    # Puzzle Hole2 (item 108) looks empty - it has no object command
    # because it ends the level instead of opening a door. Per the user
    # both may lie anywhere, without any restriction.
    'joby5' = @(172,173)
    # andy1: three types, two different limits. Written as a hashtable
    # because a pure type list cannot express that - typeId -> maxZone.
    #
    # 182/183 are the slingshot halves that open the church flipmap gate
    # (FLIP_GATE, NeedTypes). They must lie BEFORE the church, or the gate
    # they themselves pull open never opens - the transition is zone 0 ->
    # 1, so zone 0. They may lie behind the point of no return, that is no
    # contradiction: the teleporter leads INTO zone 0.
    #
    # 174 is the iron clapper with which Lara fends off the imps in
    # cutscene 10 - without it she dies there. The user allows it like the
    # heart in both areas; in the real course of play the cutscene lies
    # behind room 202, i.e. after the church. Zone 1 covers that.
    #
    # 172, the heart, does not stand here - it has a lock and with it an
    # already derived limit (zone <= 1).
    'andy1' = @{ 174 = 1; 182 = 0; 183 = 0 }
}
# Two spellings: a list of typeIds (no zone limit) or a hashtable typeId ->
# maxZone. Both are brought to the same form here.
$poolTypes = @{}
if ($POOL_TYPES.ContainsKey($LevelName)) {
    $pv = $POOL_TYPES[$LevelName]
    if ($pv -is [hashtable]) { foreach($kv in $pv.GetEnumerator()){ $poolTypes[[int]$kv.Key] = [int]$kv.Value } }
    else { foreach($t in @($pv)){ $poolTypes[[int]$t] = $null } }
}

$ISLAND_GROUP = @{
    # andy1 has a point of no return: the teleporter in room 47 takes Lara
    # to room 109, and from there it only goes forward. A key lying behind
    # it and needed earlier would be unreachable.
    #
    # The second deadstop is room 186 with door 52. That is the actual way
    # into the level's end, from room 202 - but the flood wrongly finds it
    # earlier via bridge 43. User statement.
    #
    # The island is computed and stands ready - IslandTypes is however
    # deliberately EMPTY. Puzzle Item1 does NOT belong in it: its lock
    # (item 51, room 202) itself lies behind the teleporter, exactly like
    # its vanilla place in room 51. Both on the same side, so no softlock -
    # vanilla does it that way.
    #
    # From that the general insight that corrected the first attempt: a key
    # does not have to lie before the point of no return, only before ITS
    # LOCK. The zone rule checks that anyway (zone <= 1). The point of no
    # return only becomes a bound when a lock lies before it and its key
    # could land behind it.
    # NotOnIslandTypes: all four progress items may NOT lie before the
    # teleporter. For each of them the same softlock holds - Lara walks
    # through without having collected it and never comes back.
    #
    # Together with the respective zone limit that yields two corridors:
    #   182/183 (slingshot)      zone 0 -> between teleporter and church
    #   172 (heart), 174 (clapper) zone 1 -> from the teleporter to behind the church
    'andy1' = @( @{ IslandTypes = @(); NotOnIslandTypes = @(172,174,182,183); DeadstopRooms = @(47,186) } )
    # andy3 has the same case as andy1 - only pointed the other way (#61,
    # 2026-08-24). The teleporter stands in room 137 and takes Lara back to
    # the START ISLAND; the coin (173) may therefore not lie there, or it
    # grasps at nothing once she has left the section. User statement: "Key
    # item range is from room 142 -> 135. It must not wander toward 137,
    # there is a teleporter into the start island, then it would be a
    # softlock."
    #
    # Why ISLAND_GROUP and not NO_KEY_ISLAND: the teleporter is NO portal,
    # the room graph does not see it. An ordinary flood can therefore not
    # separate the two sides at all - from 142 it runs over normal ways all
    # the way into the start island (measured: 114 rooms). The island
    # flood, by contrast, runs from Lara's start WITH CLOSED DOORS and
    # thereby maps exactly the area Lara reaches without progress.
    #
    # The first two attempts were wrong and stand here as a warning: 137 as
    # an additional deadstop on the existing NO_KEY_ISLAND shrank the bar
    # from 3227 to 13 sectors (i.e. the opposite), and a second flood from
    # room 29 barred 64 rooms without hitting the reported find spot.
    # andy3 needs nothing here anymore: the coin is positively bounded via
    # TYPE_ALLOW_ISLAND (from 142, stops 137/97/149). The interim state
    # with NotOnIslandTypes is thereby superfluous - three overlapping bars
    # for the same type were exactly the knot at which on 2026-08-24 only a
    # single room remained.
    # DeadstopRooms defines the island via a SECOND, sharper flood. The
    # track bed chain 2/4/5 and room 89 are barred in it, their flip
    # partners 43 and 53 are derived. What is then still reachable from
    # Lara's start IS the start island - there and only there may Key8 lie.
    # Expressly not in the main run: the track bed is at the same time the
    # legitimate main way, and with this bar Key Item7 in room 52 would be
    # unreachable.
    'joby2' = @( @{ BaselineRoom = 84; MaxYAbove = 1024; RequireTypes = @(202,203)
                    IslandTypes = @(203); DeadstopRooms = @(2,4,5,89) } )
    # rich1, the hammer. The user's proposal, and it fits better than the
    # zone limit: a flood from Lara's start with room 12 as the deadstop.
    #
    # The island is no subset of a zone - it contains sectors from zone 0
    # AND zone 1 -, because "before room 12" and "before the first door"
    # are simply not the same. Exactly for that reason it cannot be
    # expressed with maxZone, and that is why with about fifty spots it is
    # also three times as tight as the zone limit, which left 150. The
    # hammer lies vanilla in room 22 and thus inside.
    'rich1' = @(
        @{ IslandTypes = @(202); DeadstopRooms = @(12) }
        # Key Item8, four deadstops delimited pairwise by the user: room 63
        # yes / 111 no, 28 yes / 27 no, 81 yes / 31 no, 43 yes / 42 no. The
        # key is reusable, a single instance serves all four locks - so it
        # must lie before each of them.
        @{ IslandTypes = @(203); DeadstopRooms = @(27,31,42,111); DoorsOpen = $true }
        # Puzzle Item4. The user's requirement: a section between two bars,
        # room 116 on one side and room 66 (door 107 stands there) on the
        # other - in between the part may lie anywhere. Room 116 is
        # reachable via room 114 with explosions and thus presumably itself
        # a point of no return.
        #
        # This is the first section that does not hang on Lara's start,
        # hence StartRoom. From Lara it could not be described: the first
        # bar would then already lie before it.
        @{ IslandTypes = @(175); StartRoom = 114; DeadstopRooms = @(66,116); DoorsOpen = $true }
    )
    # rich2's start area: "everything up to door 105" (user). That is the
    # keypad door in room 55, behind which it goes into the WC and with it
    # into the rest of the level. Hence room 55 as the deadstop and a flood
    # from Lara's start.
    #
    # Three things should lie there, all three user requirements:
    #   221  Pickup Item2 - exactly one instance in the level, must go in
    #   235  Bottle       - four instances, at least one must go in
    #   236  Cloth        - exactly one instance, must go in
    # Bottle and cloth are combined in the inventory into the chloroform
    # cloth with which Lara silently takes out enemies. If one of the two
    # is missing in the start area, this way is closed from the beginning.
    #
    # IslandTypes for 221 (the type as a whole belongs in), RequireTypes
    # for 235 and 236 ("at least one instance in the group") - for 236 both
    # come to the same thing because there is only one.
    'rich2' = @(
    # The second deadstop is room 58, and it is not obvious: from there the
    # ceiling hatch (item 7, opened by the trigger in 58) leads back to
    # room 8. That is the return way from the level's end with which Lara
    # fetches the HK. Without it the flood ran via 58 -> 59 -> 7 into
    # rooms 152, 156 and 161 - zones 4 and 5 - and the "start area" would
    # have comprised half the level's end.
        @{ IslandTypes = @(221); DeadstopRooms = @(55,58); DoorsOpen = $true }
        # Bottle and cloth need a TIGHTER area than Pickup Item2, even
        # though both are called "start area". User requirement: no further
        # than room 52, because from room 84 the enemy the chloroform cloth
        # is meant for already stands there - if one of the two halves only
        # lies there, Lara arrives at the combination too late.
        #
        # Room 84 is therefore the third deadstop; it borders 52, 85, 97
        # and 170, so it cuts off exactly the enemy area. The vanilla cloth
        # in room 50 hangs only on room 54 and stays inside - important,
        # because there is only one in the whole level and RequireTypes
        # thereby must place it here.
        @{ IslandTypes = @(); RequireTypes = @(235,236); DeadstopRooms = @(55,58,84); DoorsOpen = $true }
        # Puzzle Item3 gets a LOWER limit the zone cannot express. User
        # requirement: it may only lie from room 128 toward room 5, and
        # doors 180/179 are the deadstop - both stand in room 120 and are
        # opened by switch 43 in room 26.
        #
        # maxZone 4 from EXTRA_POOL_TYPES says "no later than the lock";
        # this island says "no earlier than room 128". Only both together
        # describe the corridor in which the part may lie.
        # Room 58 must join as the second deadstop, for the same reason as
        # in the start area: the ceiling hatch from 58 to 8 is the return
        # way from the level's end. With room 120 alone the island
        # comprised 163 rooms and reached down to Lara's start room - the
        # exact opposite of a lower limit. With both it is 586 sectors, and
        # together with maxZone 4, 214 spots in 26 rooms remain: the
        # corridor 128 -> 5 -> 23 -> 27 -> ... -> 149 -> 152 -> 166. The
        # vanilla place in room 166 and the lock in room 147 both lie
        # inside.
        # The third deadstop is room 152, the target of the teleporter from
        # room 149 (item 217, fired by the trigger on 149,5,5 - the same
        # sector Puzzle Hole2 stands on). User requirement: behind the
        # teleporter the part may no longer lie. That costs 86, 98, 152,
        # 156, 161 and 162; room 166 stays reachable without going through
        # 152, and with it the vanilla place.
        @{ IslandTypes = @(174); StartRoom = 128; DeadstopRooms = @(120,58,152); DoorsOpen = $true }
    )
    # rich3: the two puzzle combos may only lie behind the one-way
    # teleporter. User statement, two points of no return in this level:
    # room 58 via the slope, and switch 310 in room 133, whose teleporter
    # leads to room 134 - in room 134 there is no trigger that sets Lara
    # back.
    #
    # The lock (Puzzle Hole1, item 144) lies in room 119, i.e. beyond this
    # one-way spot. Were one of the two halves before it, Lara could walk
    # through without having collected it and never back.
    #
    # The deadstop is room 133, the teleporter's source - it only prevents
    # the flood from running out backwards. More is not needed: room 134
    # has only ONE neighbor at all, namely 119. The area thus bounds
    # itself and comprises 76 rooms, among them both vanilla positions
    # (Combo1 in room 183, Combo2 in room 152) and the lock - Lara's start
    # room, by contrast, not.
    'rich3' = @(
        @{ IslandTypes = @(180,181); StartRoom = 134; DeadstopRooms = @(133); DoorsOpen = $true }
        # The crossbow - in game the grappling gun. It MUST lie here and
        # may ONLY lie here; IslandTypes delivers both, it is a hard
        # positive list over all instances.
        #
        # The area is deliberately tiny, and the user tightened it twice.
        # First it was the whole route from room 61 (359 spots in 31
        # rooms); then came the decisive finding: once Lara reaches the
        # antitrigger (item 149, room 124), she does not get out again
        # without the crossbow. It may therefore only appear from room 79
        # on and lie no further than room 93 - room 167 is only reachable
        # with it at all.
        #
        # The three rooms 79, 93 and 94 are a chain: 79 -> 94 -> 93. The
        # deadstops cap both ends - 82 the way back via room 80, and 167
        # what only opens with the crossbow. 85 spots remain; the vanilla
        # position (items 75/76 in room 79) lies within.
        #
        # What everything hangs on: FlipMap 2 (the way via room 57) needs
        # it, and the three ropes (items 29, 105, 204) cannot be used
        # without it - via rope 29 it goes to room 58.
        @{ IslandTypes = @(341); StartRoom = 79; DeadstopRooms = @(82,167); DoorsOpen = $true }
    )
    # For Puzzle Item4 a second group was tried here (deadstop room 66, in
    # which door 107 stands) and discarded again: it forbade the part its
    # own vanilla place in room 153.
    #
    # The reason is no wrong room choice but a thinking error in the
    # assignment. An island group floods from Lara's start with ALL doors
    # shut - it describes "what stands open from the beginning". That fits
    # the hammer, which lies early in the level, and on principle not a
    # part whose vanilla place sits in zone 7 behind a dozen doors. For
    # such cases it takes a room list in TYPE_EXCLUDE_ROOMS.
}
$islandGroups = @( if($ISLAND_GROUP.ContainsKey($LevelName)){ $ISLAND_GROUP[$LevelName] } )

# Doors a per-level script function (game/deltapak.cpp) opens via a
# cutscene command instead of an object command. Unlike the empty
# SCRIPT_DOORS table above, this here is backed by the engine code AND the
# level data, not guessed:
# rich1 item 153 (Door Type1, room 89) - the shootable lock is a trap
# (probably fires a heavy trigger for a FlipMap change, the intended way
# is the hammer). The floor trigger in room 89 (1,4) fires "Cutscene 20";
# cutseq_control_routines[20] in deltapak.cpp is exactly { hamgate_init,
# hamgate_control, hamgate_end }. hamgate_end() teleports Lara to
# (31232,-3328,48344) - practically identical with the position of item
# 153 (31232,-3328,47616). hamgate_init() sets IFL_CODEBITS for the found
# DOOR_TYPE1 item, exactly the same mechanism as with the "flags 11111"
# doors, only set here by script instead of at level start.
# No hammer item (typeId 239) exists anywhere in rich1 - Lara does not
# need it here as an inventory condition, the floor trigger alone
# suffices.
$CUTSCENE_DOORS = @{
    'rich1' = @{ 153 = 20 }   # doorItem -> cutscene index
}

#----------------------------------------------------------------------------
# rich2's level end - recorded because the model does NOT map it correctly
# and the next curation would otherwise build on a wrong zone.
#
# The user explained the chain:
#   - Flipmap 0 is set right at the start. It switches off the
#     trigger-triggerer (item 25, room 14), which in turn blocks the
#     EndLevel trigger in room 14 (1,1).
#   - Lara must play the level up to room 58; there the trigger on (2,1)
#     "Object:7, FlipOff:0" switches the flipmap off again and frees the
#     end.
#   - To open door 171 she must shoot the fire extinguisher (mesh 301,
#     room 118). The HeavyTrigger in 118 (1,1) fires Object:171 together
#     with Object:172, the explosion - triggered not by Lara but by the
#     projectile.
#   - Without this flipmap trickery Lara could skip the disarming, heal
#     through, sprint straight to the fire extinguisher and open the
#     level.
#
# What the model makes of it: it reads the HeavyTrigger in room 118 like
# an ordinary floor trigger and thereby opens door 171 MUCH too early -
# which is why it stands in the report as a zone-0/1 transition and zone 1
# comprises 1285 sectors. The flipmap block of the trigger-triggerer it
# does not know at all.
#
# This is currently without consequence only because rich2 has no derived
# key constraints at all: its four locks (Puzzle Hole2/3/8/8) find, except
# for Puzzle Item3, no matching item in the level. As soon as a type is to
# be limited here, the zone is the wrong basis and it takes deadstops.
#----------------------------------------------------------------------------
$cutsceneDoors = @{}
if ($CUTSCENE_DOORS.ContainsKey($LevelName)) { $cutsceneDoors = $CUTSCENE_DOORS[$LevelName] }
$cutsceneDoorByCutscene = @{}
foreach($kv in $cutsceneDoors.GetEnumerator()){ $cutsceneDoorByCutscene[[int]$kv.Value] = [int]$kv.Key }

# Transitions Lara manages but the sector graph does not know: jumps,
# climbing spots, monkeyswing. Format @{From='room,x,z'; To='room,x,z'},
# both ways. This is the hand curation for "the flood stops at the
# geometry".
#
# WHY HASHTABLES AND NOT PAIRS OF TWO STRINGS: the table long stood as
# @( @('a','b') @('c','d') ) and NEVER worked. @(...) is an array
# EXPRESSION in PowerShell, not a literal list - every inner @(...) is its
# own statement whose output the stream flattens. Two pairs became four
# strings, rich2's seven pairs became fourteen. The loop then read
# $ml[0]/$ml[1] as CHARACTERS of a string and created edges like '1' ->
# '3': silent, without effect, and unnoticed for three levels. A comma
# between the pairs would fix it too - but exactly that gets lost again at
# the next entry. A hashtable is a single value the stream cannot take
# apart, and the field names additionally say which side is which.
$MANUAL_LINK = @{
    # joby2: the way onto the front ledge of room 11, where lasersight and
    # revolver ammo lie. Twice the same running jump across a gap of
    # exactly three sectors - first within room 152, then within room 11
    # across the floorless tiles x6/x7/x8.
    # The user rule behind it: Lara clears 3 x 1024 with a running start,
    # provided both edges have the same Y value. Satisfied here,
    # everything lies on -4608.
    'joby2' = @(
        @{From='152,10,13'; To='152,10,17'}
        @{From='11,9,1';    To='11,5,1'}
    )
    # rich1: the teleport lift between the cabins in room 30 and room 82.
    # In the portal graph it is missing entirely - room 30 borders only 23
    # there, room 82 only 81 -, because the lift moves Lara by teleporter
    # instead of letting her walk through a portal.
    #
    # The two switches 49 (room 30) and 175 (room 82) each operate their
    # side's lift doors, the other side's call button (42 in room 23, 143
    # in room 81) and the other cabin's lift teleporter. The user: the
    # lifts are usable at any time in both directions - hence an ordinary
    # two-way edge and no ONEWAY_OUT.
    'rich1' = @(
        @{From='30,2,2'; To='82,2,2'}
    )
    # rich2. SIX TELEPORTER PAIRS ONCE STOOD HERE - they are gone again,
    # and that is this level's actual lesson. The elevator section hangs on
    # six teleporters, I had copied them from the trigger data and entered
    # them by hand. But the code has long derived them itself (see the
    # TELEPORT_TYPES loop further down) - and DIRECTED at that, from the
    # trigger to the teleporter, the way ControlTeleporter really moves
    # Lara.
    #
    # My hand entries with the same sectors were thus pure duplicates, only
    # TWO-WAY. With that the flood walked the chain backwards: it stood on
    # the ground floor and went from there back into the lift cabin it had
    # never reached. Making a directed level mechanic into a street is
    # worse than not knowing it at all.
    #
    # Note: before an edge comes by hand, check whether the code already
    # derives it. Teleporters, bridges and doors it does.
    'rich2' = @(
        # The entry into the elevator section. The way there is the chain
        # the user described: door 105 by keypad into the WC, from room 134
        # kick the kick door into niche 131, up through ceiling hatch 188
        # there to room 139 into the elevator shaft, and from there with
        # some climbing maneuvers to trapdoor 117, where the lift section
        # begins.
        #
        # The portal graph cannot walk this chain. The portal 134->131 lies
        # on WALL SECTORS on both sides (134(2,3) and 131(0,1)), and
        # 131(1,1) - the niche's only floor - carries blockedByStatic. The
        # flood would thus not get in even with the kick door open; the
        # door is innocent and has long stood in the deco-door list.
        #
        # Hence the jump 130 -> 139 named by the user, which skips the
        # niche. Lara does not linger there anyway, she passes through.
        # 130(1,1) and 139(1,1) are both clean floor.
        @{From='130,1,1'; To='139,1,1'}
    )
}

# Individual sectors in which nothing may lie - unlike EXCLUDE_SECTORS they
# stay walkable. The sector-fine version of NO_ITEM_ROOMS, for rooms that
# are otherwise perfectly fine.
$NO_ITEM_SECTORS = @{
    #------------------------------------------------------------------------
    # The user's last review across all levels. These four had no entry
    # until then.
    #------------------------------------------------------------------------
    'Andrea1' = @('214,1,1')
    'Andrea2' = @('6,1,2','33,2,2','51,2,2')
    'Andrea3' = @('49,5,3')
    # joby2: the nine tiles in room 141 form a contiguous block (x1-x3 on
    # z3-z5), plus one tile in room 138.
    'joby2'   = @('141,1,3','141,1,4','141,1,5','141,2,3','141,2,4','141,2,5',
                  '141,3,3','141,3,4','141,3,5','138,4,1')
    'rich1'   = @('31,3,4')
    # 1(1,1), 70(5,3), 70(5,5) and 13(2,3) joined in the last review.
    'andy1' = @('176,2,1','176,3,1','170,1,6','170,3,6','170,3,4',
                '200,2,0','121,1,1','121,2,3','226,1,2','158,2,2',
                '1,1,1','70,5,3','70,5,5','13,2,3')
    # rich2's laser stood here as a stopgap while it cost half the level as
    # EXCLUDE_SECTORS. Since the jump 130 -> 139 stands in MANUAL_LINK, it
    # is in the right place there - it is a wall, not a placement ban.
    #
    # What stands here is the user's sector list from the trview pass.
    # 149(5,5) carries Puzzle Hole2 itself - no item has any business on a
    # lock. The six tiles in room 166 are the user's; the vanilla place of
    # Puzzle Item3 on 166(3,2) is NOT among them and stays allowed.
    # 9(2,3) joined in the last review.
    'rich2' = @('149,5,5','9,2,3',
                '166,2,1','166,4,1','166,1,3','166,3,3','166,2,4','166,4,4')
    # andy3, the user's first sector review. Room 188 carries a secret and
    # a large medipack - both lie elsewhere in the room, the bar hits no
    # vanilla position.
    'andy3' = @('150,2,1','188,1,2','188,1,3','58,2,1','58,3,1','58,4,1')
    # Andy2, the user's second and third trview review. The entries from
    # 133 on are individual trap sectors; those in the flip variants 133,
    # 135 and 136 are, like rooms 132/134 above, already handled by the
    # flip bar and stand only as a safety net.
    # The fourth review takes the remaining trap sectors in the bridge
    # rooms along. 30, 43, 56 and 57 are the base rooms of flipmap groups
    # 3, 4 and 5 - the same construction as 31 and 41, only that here the
    # whole room does not have to go.
    'Andy2' = @('251,1,3','3,6,4','39,3,1',
                '133,5,4','135,4,5','135,6,5','136,5,5',
                '30,5,4','30,5,6',
                '43,5,4','43,5,6','43,6,5',
                '44,5,4','44,4,5','44,5,5','44,5,6','44,6,5',
                '55,5,4','55,4,5','55,5,5','55,5,6','55,6,5',
                '56,5,4','56,4,5','56,6,5',
                '57,5,5')
}
$noItemSectors = @( if($NO_ITEM_SECTORS.ContainsKey($LevelName)){ $NO_ITEM_SECTORS[$LevelName] } )

#----------------------------------------------------------------------------
# Height bands per level section.
#
# The user's idea for andy3, and there it pays off immediately: the level
# falls apart into sections through its one-way teleporters anyway, and
# within a section the playable floor lies in a narrow height band.
# Everything above and below is roof, scenery or abyss. Instead of
# collecting a hundred room numbers by hand, one band value per section
# suffices.
#
# The section is determined as a room component: from StartRoom through
# portals and vertical connections, without entering DeadstopRooms.
# Deliberately the ROOM GRAPH and not the walkable flood - the section
# should be a property of the level architecture and not depend on which
# door happens to be open. In andy3 that separates cleanly: 64 rooms from
# Lara with a stop at teleporter room 110, 117 rooms from target room 140
# with a stop at the next teleporter 155, without a single shared room.
#
# Y CONVENTION: in TR, SMALLER y lies HIGHER. YMin is therefore the
# world's UPPER bound and YMax the lower. The values come from trview, and
# its display is bit-identical with our floorY - cross-checked at four
# items, nothing is inverted.
#
# $null on one side means "open there". Island 2 in andy3 needs only an
# upper bound; downward it simply goes ever deeper into the level.
#----------------------------------------------------------------------------
$ISLAND_YBAND = @{
    'andy3' = @(
        @{ Name = 'island 1'; StartRoom = 29;  DeadstopRooms = @(110); YMin = 451;  YMax = 6495 }
        @{ Name = 'island 2'; StartRoom = 140; DeadstopRooms = @(155); YMin = 7940; YMax = $null }
    )
}
$ybandBarred = New-Object System.Collections.Generic.HashSet[string]
if ($ISLAND_YBAND.ContainsKey($LevelName)) {
    $roomAdj = @{}
    foreach($sx in $d.sectors){
        $a = [int]$sx.room
        foreach($t in @($sx.portalTarget, $sx.roomBelow, $sx.roomAbove)){
            if ($null -eq $t -or "$t" -eq "") { continue }
            $b = [int]$t
            if ($b -lt 0 -or $b -ge $d.rooms.Count) { continue }
            if (-not $roomAdj.ContainsKey($a)) { $roomAdj[$a] = New-Object System.Collections.Generic.HashSet[int] }
            if (-not $roomAdj.ContainsKey($b)) { $roomAdj[$b] = New-Object System.Collections.Generic.HashSet[int] }
            [void]$roomAdj[$a].Add($b); [void]$roomAdj[$b].Add($a)
        }
    }
    foreach($band in $ISLAND_YBAND[$LevelName]){
        $stopR = @($band.DeadstopRooms | ForEach-Object { [int]$_ })
        $seenR = New-Object System.Collections.Generic.HashSet[int]
        [void]$seenR.Add([int]$band.StartRoom)
        $qr = New-Object System.Collections.Generic.Queue[int]
        $qr.Enqueue([int]$band.StartRoom)
        while($qr.Count -gt 0){
            $cr = $qr.Dequeue()
            if (-not $roomAdj.ContainsKey($cr)) { continue }
            foreach($n in $roomAdj[$cr]){
                if ($stopR -contains $n -or $seenR.Contains($n)) { continue }
                [void]$seenR.Add($n); $qr.Enqueue($n)
            }
        }
        $outTooHigh = 0; $outTooLow = 0
        foreach($sx in $d.sectors){
            if (-not $seenR.Contains([int]$sx.room)) { continue }
            $y = [int]$sx.floorY
            if ($null -ne $band.YMin -and $y -lt [int]$band.YMin) {
                [void]$ybandBarred.Add("$($sx.room),$($sx.sectorX),$($sx.sectorZ)"); $outTooHigh++
            }
            elseif ($null -ne $band.YMax -and $y -gt [int]$band.YMax) {
                [void]$ybandBarred.Add("$($sx.room),$($sx.sectorX),$($sx.sectorZ)"); $outTooLow++
            }
        }
        $upperTxt = if ($null -ne $band.YMin) { "y >= $($band.YMin)" } else { 'open upward' }
        $lowerTxt = if ($null -ne $band.YMax) { "y <= $($band.YMax)" } else { 'open downward' }
        # Counted are all sectors of the section, not only the item-capable
        # ones - how many spots it costs in the end stands in the export
        # line.
        Write-Output "height band $($band.Name): $($seenR.Count) rooms from $($band.StartRoom) (stop $($stopR -join ', ')), allowed $upperTxt and $lowerTxt - outside: $outTooHigh too high, $outTooLow too low (sectors of the section)"
    }
}

# Keys that are NOT consumed. The normal case in TR is: key or puzzle item
# goes into the lock and is gone - KeyHoleCollision (game/pickup.cpp:594)
# calls remove_inventory_item for that. ONE single hard exception stands in
# the code itself, not in level data: "if (item->object_number != KEY_HOLE8)
# remove_inventory_item(...)" - Key Hole 8 (typeId 265) and with it Key Item8
# (typeId 203) are unconsumable across ALL TR5 levels, that is no level
# property. joby5 (item 118, ONE key for TWO Keyhole8 locks) and rich1 (ONE
# key for FOUR Keyhole8 locks, rooms 26/69x2/81) are both simply use cases of
# the same global engine rule, no coincidence.
$REUSABLE_KEYS_GLOBAL = @(203)
$REUSABLE_KEYS = @{
}

# Rooms that exist only for a camera flyby and that Lara never enters.
# The automatic detection recognizes such areas only when nothing but
# animatings stand in them (see Andy2). If the scenery also contains doors
# or other inventory, it falls through - then only hand curation helps.
# joby3: the island group around room 140 is only SEEN during the cutscene
# from the air shaft (room 76) but is not walkable (user statement in the
# zone report). It contains three doors and is therefore not automatically
# recognizable as scenery.
$CAMERA_ROOMS = @{
    'joby3' = @(4,10,11,94,96,97,114,140,185)
    # Andrea3 rooms 73/74: 9 sectors each, completely empty (no items),
    # isolated. User: "leftovers like in Andrea2, something was probably
    # planned there once" - cut content, not meant to be entered.
    'Andrea3' = @(73,74)
    # rich3 rooms 64/65/66/67: a closed 4-room circuit without any
    # connection to the rest of the level. User: lies visibly outside the
    # actual level geometry in trview ("out of map") - that is the intro
    # flyby with the helicopters circling the level, purely cosmetic, never
    # walkable.
    'rich3' = @(64,65,66,67)
}
$cameraRooms = @( if($CAMERA_ROOMS.ContainsKey($LevelName)){ $CAMERA_ROOMS[$LevelName] } )
$reusableKeys = @($REUSABLE_KEYS_GLOBAL) + @( if($REUSABLE_KEYS.ContainsKey($LevelName)){ $REUSABLE_KEYS[$LevelName] } )
$manualLinks = @( if($MANUAL_LINK.ContainsKey($LevelName)){ $MANUAL_LINK[$LevelName] } )

# Floor-override objects create walkable floor where the sector data reports
# emptiness. Complete list per grep "obj->floor = " in game/setup.cpp:
#   BRIDGE_FLAT/TILT1/TILT2  -> BridgeFlatFloor etc.   (448..453 incl. mips)
#   FALLING_BLOCK/2, CRUMBLING_FLOOR -> FallingBlockFloor (103, 104, 105)
#   TWOBLOCK_PLATFORM        -> TwoBlockPlatformFloor  (136, "Expanding Platform")
# A purely sector-based graph cannot see them on principle. The crumbling
# floors were missing here at first - with fatal effect once the climb rule
# came: Andrea3's entry (12 crumbling floors above lava, room 8) lies on
# sectors whose resolved "standing height" is the lava ground far below;
# the way back from the bridge looked like an impossible 4000-unit ascent
# and the level collapsed to 8%.
$BRIDGE_TYPES = @(448,449,450,451,452,453, 103,104,105, 136)
$bridgeSec = @{}
foreach($i in $d.items){
    if ($BRIDGE_TYPES -notcontains [int]$i.typeId) { continue }
    $bridgeSec["$($i.room),$($i.sectorX),$($i.sectorZ)"] = $true
}

function Test-Walkable($key) {
    if (-not $sec.ContainsKey($key)) { return $false }
    # Hand-curated not-enterable rooms first: they shall serve neither as a
    # placement target nor as a passage (a roof is no way either).
    if ($excludeRooms -contains [int]$sec[$key].room) { return $false }
    if ($excludeSectors -contains $key) { return $false }
    if ($bridgeSec.ContainsKey($key)) { return $true }
    return (-not $sec[$key].isWall)
}

$adj = @{}
function Add-Edge($a, $b) {
    # Catch hand-barred single sectors here and not in Test-Walkable alone:
    # portal, bridge and manual-link edges do not call Test-Walkable at all,
    # so a sector would still enter the graph through them.
    if ($excludeSectors -contains $a -or $excludeSectors -contains $b) { return }
    # The same for whole rooms. Room 145 in joby2 stood in EXCLUDE_ROOMS and
    # still showed up with 36 spots: it contains four bridge flats, and
    # bridge edges pull their sectors into the graph without Test-Walkable.
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

    # Neighbors in the same room - directed: the reverse direction arises
    # when the neighbor itself takes its turn and passes ITS climb check.
    # This way Lara may fall down a high edge without that becoming a way
    # back up.
    foreach($dz in @(-1,0,1)){ foreach($dx in @(-1,0,1)){
        if (($dx -eq 0 -and $dz -eq 0) -or ($dx -ne 0 -and $dz -ne 0)) { continue }
        $nk = "$($s.room),$($s.sectorX + $dx),$($s.sectorZ + $dz)"
        if (-not (Test-Walkable $nk)) { continue }
        if (-not (Test-ClimbEdge $key $nk)) { continue }
        Add-Edge $key $nk
    }}

    # Jump edges: TR core movement the plain neighbor graph lacks. A
    # standing jump clears 1 gap sector, a running jump 2. The old model
    # (without the height rule) masked this by letting the flood run
    # THROUGH the hole and "come back up" on the other side - physically
    # wrong, but coincidentally connecting. With correct fall edges every
    # hole became a one-way trap and shaft/ledge levels collapsed (joby3
    # 8%, rich2's elevator shaft, Andrea3's Colosseum ledges - named
    # exactly so by the user). Rule: up to 2 consecutive sectors whose
    # resolved standing height lies clearly (>512) below the takeoff
    # height may be jumped over; the landing may lie at most 1 click (256)
    # higher, arbitrarily deep is allowed. Only straight-line and within
    # the same room - jumps spanning room borders are a known, still open
    # gap.
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
                # first non-gap sector = landing candidate; at step 1 it is
                # an ordinary neighbor the normal edge already covers
                if ($step -ge 2 -and (Test-Walkable $ck) -and ($fa - $fc) -le 256) { Add-Edge $key $ck }
                break
            }
        }
    }

    # Portal / vertical connections, both ways.
    # Portals stand in the level data only on the side from which they were
    # entered. Following only the forward edge is therefore too strict: if
    # two rooms border the same neighbor room (joby3 rooms 23 and 73 both
    # point at 58), the way back is missing and whole level parts drop out.
    # Measured both ways: Andrea1 98->100%, Andrea2 95->100%, richcut2
    # 50->99%, joby5 54->62%, rich3 21->26%, without Andrea1 losing its
    # zone structure or a key-item chain. Real one-way transitions stay
    # excepted via ONEWAY_OUT - there even the back edge must not arise, or
    # the curation would be void.
    $leavesOneway = $onewayOut -contains [int]$s.room
    $targets = @()
    if ($null -ne $s.portalTarget) { $targets += ,[int]$s.portalTarget }
    foreach($vr in @($s.roomBelow, $s.roomAbove)){ if ($vr -ne 255) { $targets += ,[int]$vr } }
    foreach($tgt in $targets){
        $t = Get-SectorInRoom $tgt $wx $wz
        if (-not $t) { continue }
        $tk2 = "$($t.room),$($t.sectorX),$($t.sectorZ)"
        # No excluded room may be entered via portals/roomBelow/roomAbove
        # either - otherwise the curation would be void, because roofs
        # almost always hang by portal rather than by neighbor sector.
        # Deliberately ONLY the room bar, not the full Test-Walkable: a
        # portal may well land on a sector marked as wall (border ring of
        # the target room) from which it continues inside the room -
        # cutting that cost Andrea2 the rooms 149/191/197 and joby2/andy3 a
        # few sectors each, without any game mechanic corresponding to it.
        if ($excludeRooms -contains [int]$t.room) { continue }
        # Climb check per direction individually (Test-ClimbEdge: down
        # free, up only to 1920 or climbable wall/water/bridge). The first
        # attempt at a height rule here failed at two errors at once: raw
        # floorY compared at portal sectors (instead of the standing height
        # resolved via portals/roomBelow, see Get-EffFloorY) AND descents
        # capped too - measured, that cost joby3 -82%, rich2 -40%, joby2 -16%.
        if (-not $leavesOneway -and (Test-ClimbEdge $key $tk2)) { Add-Edge $key $tk2 }
        if ($onewayOut -notcontains $tgt -and (Test-ClimbEdge $tk2 $key)) { Add-Edge $tk2 $key }
    }
}

# Flipmap: an alternate room swaps the base room's geometry in place, sector
# (x,z) for sector (x,z). For item placement, alternate rooms are barred
# (FlipMap() restores the base room's item_number, items never lie there) -
# for progression they very much count, because their triggers fire. Andrea1
# ends exactly like that: room 234 is the alternate of 171, and only after
# FlipMap 8 does the cutscene trigger lie there.
foreach($r in $d.rooms){
    if ($null -eq $r.alternateRoom -or $r.alternateRoom -eq '') { continue }
    $ai = [int]$r.alternateRoom
    if ($ai -eq [int]$r.index) { continue }
    # For every sector that is walkable in the alternate: connect it with
    # the base room. Congruent where both are walkable - and additionally
    # via the neighbor sectors where the base has a wall. Exactly that is
    # the point of many flipmaps: the flip *opens* a passage where there
    # was wall before (joby2: the crane cutscene flip on room 4/43 frees
    # the level's continuation). Without the second case the whole
    # alternate part stays its own island and the flood stops at the
    # level's beginning.
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

# Teleporters move Lara to the position of the teleporter item itself.
# ControlTeleporter (game/tomb4fx.cpp:531ff): at trigger_flags == 666 only
# the effect runs, in ALL other cases the else branch applies and sets
# lara_item->pos to item->pos plus ItemNewRoom. So the teleporter is the
# TARGET and its trigger the source - the edge is directed, from the trigger
# sector to the teleporter sector. (Earlier I had noted this exactly the
# wrong way round; all teleporters of the 16 vanilla levels have OCB 0, so
# they really do warp.)
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

# --- Doors: which sector gets blocked? --------------------------------------
$doorSector = @{}   # itemIndex -> sectorKey
$openAtStart = @()
$crowbarDoors = @()
foreach($i in $d.items){
    if ($DOOR_TYPES -notcontains [int]$i.typeId) { continue }
    $k = "$($i.room),$($i.sectorX),$($i.sectorZ)"
    if (-not $sec.ContainsKey($k)) { continue }
    # A door whose item flags already have all 5 code bits (IFL_CODEBITS,
    # 0x3E00) set at load time counts to the engine as fully triggered
    # right away and opens at level start without any trigger. In trview it
    # then says "Flags 11111". joby2 item 138 is exactly such a case - it
    # looked like an unswitchable door but is open from the beginning.
    if (([int]$i.flags -band 0x3E00) -eq 0x3E00) { $openAtStart += ,[int]$i.index; continue }
    # Crowbar door: DoorCollision (game/door.cpp:313) checks
    # item->trigger_flags == 2, i.e. OCB 2, and after prying sets
    # item->flags |= IFL_CODEBITS - the same mechanism as above, only
    # triggered by hand. No trigger in the level switches such a door,
    # which is why it looked unswitchable. Needs the CROWBAR_ITEM (typeId
    # 240; enum order in game/objects.h: HAMMER 239, CROWBAR 240,
    # BURNING_TORCH 241, PUZZLE_HOLE1 242 - the latter independently
    # confirmed).
    if ([int]$i.ocb -eq 2) { $crowbarDoors += ,[int]$i.index }
    $doorSector[[int]$i.index] = $k
}
# Which typeId the crowbar is in this level does NOT stand in the level
# data: levels may carry it as a puzzle item and rename it via the gameflow
# (CMD_PUZZLE1 objname, string wad in ENGLISH.DAT). joby3 does exactly that
# - there the crowbar is item 41, typeId 172 "Puzzle 1" with OCB 9, and
# Lara picks it up with action plus a small cutscene. Hence curated per
# level. Without an entry the regular CROWBAR_ITEM slot 240 applies.
$CROWBAR_TYPE = @{
    'joby3' = 172
}
$crowbarType = if($CROWBAR_TYPE.ContainsKey($LevelName)){ [int]$CROWBAR_TYPE[$LevelName] } else { 240 }
$crowbarInLevel = @($d.items | Where-Object { [int]$_.typeId -eq $crowbarType })
if ($crowbarDoors.Count -gt 0 -and $crowbarInLevel.Count -eq 0) {
    # Placed nowhere in the level -> Lara brings it along or gets it via
    # CMD_GIVEOBJ, the doors are then open from the start.
    foreach($cd in $crowbarDoors){ $openAtStart += ,$cd; $doorSector.Remove($cd) }
}

# The hammer follows the same renaming pattern as the crowbar: TR5 can carry
# it as a key/puzzle item and rename it via gameflow (CMD_KEYn/CMD_PUZZLEn
# objname). rich1: the hammer is item 35, typeId 202 "Key Item7", room 22 -
# the ONLY instance of this type in the level, without an accompanying Key
# Hole7 and without a key trigger referencing it (verified). Unlike the
# crowbar in most levels, the hammer here actually lies in the level and
# must first be reached - Lara does NOT bring it along automatically (user
# correction: "she brings neither of them along like in Joby").
$HAMMER_TYPE = @{
    'rich1' = 202
}
$hammerType = if($HAMMER_TYPE.ContainsKey($LevelName)){ [int]$HAMMER_TYPE[$LevelName] } else { $null }
$hammerInLevel = @( if($null -ne $hammerType){ $d.items | Where-Object { [int]$_.typeId -eq $hammerType } } )

# --- What opens which door? --------------------------------------------------
# openedBy[itemIndex] = list of requirements (one per trigger)
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
    # Returns: @{ Kind='auto'|'reach'|'item'; Sector=..; NeedItemType=..; Desc=.. }
    $tk = "$($t.room),$($t.sectorX),$($t.sectorZ)"
    switch ($t.triggerType) {
        { $_ -in 'Trigger','Pad','HeavyTrigger','Combat','Dummy' } {
            # Cutscene doors (rich1 hamgate): the floor trigger that fires
            # the script cutscene sits on the same sector as the door
            # itself (89/(1,4) for item 153) - the same deadlock as with
            # switch/lock on the door sector, hence operable from the
            # neighbor sector here as well.
            $isCutsceneDoor = $false
            foreach($c in $t.commands){ if ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) { $isCutsceneDoor = $true } }
            return @{ Kind='reach'; Sector=$tk; Operable=$isCutsceneDoor; Desc="enter trigger ($tk)" }
        }
        'Switch' {
            $ri = $t.refItem
            $d2 = if($null -ne $ri -and $items.ContainsKey([int]$ri)){ "switch item $ri (room $($items[[int]$ri].room))" } else { "switch" }
            # Operable: Lara operates a switch from the neighbor sector,
            # she does not have to enter the trigger sector. That is not
            # cosmetic - door item and switch regularly stand on the same
            # sector (Andrea1: Door 7 item 71 and Shoot switch 1 item 72
            # both on 139/(1,3)). Without this distinction the analyser
            # demands entering a sector it itself bars because of the door
            # -> deadlock, and the whole level part behind it drops out of
            # the zone chain.
            return @{ Kind='reach'; Sector=$tk; Operable=$true; Desc=$d2 }
        }
        'Key' {
            $ri = $t.refItem
            if ($null -eq $ri -or -not $items.ContainsKey([int]$ri)) { return @{ Kind='reach'; Sector=$tk; Operable=$true; Desc="lock (unknown)" } }
            $hole = $items[[int]$ri]; $ht = [int]$hole.typeId
            $needType = $null; $label = $null
            if ($ht -ge $KEY_HOLE_BASE -and $ht -lt ($KEY_HOLE_BASE+8)) {
                $n = $ht - $KEY_HOLE_BASE; $needType = $KEY_ITEM_BASE + $n; $label = "Key Item$($n+1)"
            } elseif ($ht -ge $PUZZLE_HOLE_BASE -and $ht -lt ($PUZZLE_HOLE_BASE+8)) {
                $n = $ht - $PUZZLE_HOLE_BASE; $needType = $PUZZLE_ITEM_BASE + $n; $label = "Puzzle Item$($n+1)"
            }
            # Operable, as with the switch: KeyHoleCollision
            # (game/pickup.cpp:594) checks a proximity/alignment zone
            # (TestLaraPosition with KeyHoleBounds), Lara does not have to
            # stand on the lock's exact sector. Without this, every lock
            # sitting on the same sector as its own door deadlocks
            # (Andrea3: Keyhole1, item 27, room 15/(2,5) stands on a
            # door-blocked sector) - the same error already fixed in the
            # switch case but forgotten here in the main branch (resolved
            # lock).
            return @{ Kind='item'; Sector=$tk; Operable=$true; NeedItemType=$needType; Desc="$label into lock item $ri (room $($hole.room))" }
        }
        'Pickup' {
            $ri = $t.refItem
            return @{ Kind='reach'; Sector=$tk; Desc="pickup trigger" }
        }
        default { return @{ Kind='reach'; Sector=$tk; Desc="$($t.triggerType)" } }
    }
}

function Test-ReqSector($req, $reached) {
    # A floor trigger (Pad/Heavy/Trigger) Lara must really enter. Switches
    # and locks she operates from the neighbor sector instead - so there it
    # also counts when only an adjacent sector is reached (Operable flag).
    if ($reached.Contains($req.Sector)) { return $true }
    # Generalization, third find of the same deadlock class (after Switch
    # and Key): if the DOOR itself stands exactly on the trigger sector -
    # whatever the trigger type -, Lara can by definition never enter the
    # sector *before* the opening. rich3 room 134: Lift Doors (item
    # 172/173) AND their own floor trigger sit on the same two sectors
    # (1,1)/(2,1) - an ordinary "enter trigger" case, no switch/key, still
    # the same contradiction. Instead of marking every trigger type
    # individually with Operable: as soon as the exact sector is blocked by
    # a (still closed) door, an already reached neighbor sector counts
    # automatically. Deliberately NOT wholesale for every trigger - only
    # when a door demonstrably stands exactly there, otherwise the
    # distinction "must really be entered" would be worthless.
    $doorOnSector = $blocked.ContainsKey($req.Sector)
    if (-not $req.Operable -and -not $doorOnSector) { return $false }
    if (-not $adj.ContainsKey($req.Sector)) { return $false }
    foreach($n in $adj[$req.Sector]){ if ($reached.Contains($n)) { return $true } }
    return $false
}

# --- Zone flood --------------------------------------------------------------
# Where Lara really sets off does not necessarily stand in the Lara item: if
# a cutscene runs at level start, it drops Lara off elsewhere afterwards.
# joby5 is such a case - the Lara item lies in room 40, play starts from
# room 3, and room 3 is itself a flipmap alternate, so the flip is already
# active at game start. Without the override the analyser floods from the
# wrong spot and reports large parts of the level as unreachable.
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

$blocked = @{}   # sectorKey -> itemIndex of the door
foreach($kv in $doorSector.GetEnumerator()){ $blocked[$kv.Value] = $kv.Key }

#----------------------------------------------------------------------------
# Decorative doors.
#
# A door in TR only opens when its mask is complete - the engine checks
# (flags & IFL_CODEBITS) == CODEBITS, i.e. all five bits. If in the whole
# level no trigger exists that can bring a door to 11111, then it never
# opens. Since vanilla is completable, such a door cannot bar anything that
# is needed: it is decoration. User on joby2 room 11: "There is nothing
# behind the doors, they are deco doors."
#
# Before, they barred forever, and behind them lay real pickups (lasersight
# and revolver ammo) that thereby never entered the pool.
#
# Counted over ALL triggers of the level, including those in the floordata
# of flip rooms - they only exist after the flip but very much count (user
# hint). Three exceptions remain doors: script doors, cutscene doors and
# crowbar doors likewise have no trigger but still open.
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
    Write-Output "deco doors: $($decoDoors.Count) doors no trigger brings to 11111 - they do not bar"
    Write-Output "  items: $(($decoDoors | Sort-Object) -join ', ')"
}

# Determine flip variants: the alternateRoom field in the room header points
# from the base to its flip version, so the target rooms are exactly the
# variants.
$flipVariantRooms = @{}
foreach($r in $d.rooms){
    $ar = "$($r.alternateRoom)"
    if ($ar -eq '' -or $ar -eq '65535') { continue }
    $flipVariantRooms[[int]$ar] = [int]$r.index
}
# A flip variant may not create a way the base does not also have.
#
# Barring all flip variants wholesale would be safe but far too coarse:
# measured over all levels they mostly carry perfectly legitimate ways
# (rich3 crashed from 4 to 2098 unreached sectors with that). Most flipmaps
# do not change the connections at all - water level, debris, lighting -,
# and their sectors are aliases of the same spot.
#
# Only the other case is dangerous: the flip variant OPENS something that is
# closed in the base - the roll gate at the crane in joby2. Exactly that one
# this rule hits: a flip sector only carries if the sector at the same
# position in the base room is walkable too. Cosmetic flips stay untouched,
# new passages do not.
$flipNeedsGate = @{}   # sectorKey of the variant -> gate number (-1 = none)
foreach($s0 in $d.sectors){
    $rn = [int]$s0.room
    if (-not $flipVariantRooms.ContainsKey($rn)) { continue }
    if ($s0.isWall) { continue }
    $baseKey = "$($flipVariantRooms[$rn]),$($s0.sectorX),$($s0.sectorZ)"
    # Base walkable at the same spot too? Then the flip sector is only a
    # second spelling of the same way and needs no release.
    if ($sec.ContainsKey($baseKey) -and -not $sec[$baseKey].isWall) { continue }
    $g = -1
    for($gi = 0; $gi -lt $flipGates.Count; $gi++){ if ($flipGates[$gi].Rooms -contains $rn) { $g = $gi; break } }
    $flipNeedsGate["$rn,$($s0.sectorX),$($s0.sectorZ)"] = $g
}
# BlockRooms: hang perfectly normal rooms behind the same gate.
#
# Not every flipmap can be grasped by its variant sectors. In joby2 the roll
# gate at the crane is real wall geometry, and the flood bypasses it anyway
# - the first access to room 89 runs via 80 and 4 without touching a single
# flip sector. So the blocker does not lie where the variant opens
# something.
#
# Hence, on the user's suggestion, the direct way: set the roll gate as a
# deadstop and carry the rooms behind it as their own island group. With
# that everything else falls out by itself - the zone logic forces Key Item8
# into the start island, because a later zone begins behind the deadstop.
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
    Write-Output "flipmap: $($flipGateOf.Count) sectors open something the base does not have - $($noGate.Count) of them without a release"
    if ($rr.Count -gt 0) { Write-Output "  affected flip rooms: $($rr -join ', ')" }
}

foreach($ml in $manualLinks){ Add-Edge $ml.From $ml.To; Add-Edge $ml.To $ml.From }

# Apply GATED_AREA (after the complete graph build incl. flipmap, teleporter
# and manual links, so that really every entry edge is covered).
foreach($ga in $gatedAreas){
    # TWO separate sets - they must not be mixed:
    #  $gateSecs  = the gate sectors. Only they are exempt from the
    #               cutting, because through them runs the only legal way
    #               inside.
    #  $stopFlood = boundary of the inner-area flood: gate sectors PLUS the
    #               pre-gate rooms.
    # In a first version both were the same set - with the effect that of
    # all things the pre-gate rooms were exempt from the cutting, i.e.
    # exactly the source of the illegal entry edges ("0 entry edges cut").
    $gateSecs = New-Object System.Collections.Generic.HashSet[string]
    foreach($di in $ga.Doors){ if ($doorSector.ContainsKey([int]$di)) { [void]$gateSecs.Add($doorSector[[int]$di]) } }
    $stop = New-Object System.Collections.Generic.HashSet[string]
    foreach($g in $gateSecs){ [void]$stop.Add($g) }
    foreach($s0 in $d.sectors){
        if ($ga.PreGateRooms -contains [int]$s0.room) { [void]$stop.Add("$($s0.room),$($s0.sectorX),$($s0.sectorZ)") }
    }

    # Inner area = flood from the seed rooms up to the stop edge.
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
    Write-Output "GATED_AREA (gates $($ga.Doors -join '/')): $($inside.Count) inner sectors in $($insideRooms.Count) rooms"
    Write-Output "  rooms: $($insideRooms -join ', ')"

    $cut = 0
    foreach($u in @($adj.Keys)){
        if ($inside.Contains($u) -or $gateSecs.Contains($u)) { continue }
        $toRemove = @()
        foreach($v in $adj[$u]){ if ($inside.Contains($v)) { $toRemove += $v } }
        foreach($v in $toRemove){ [void]$adj[$u].Remove($v); $cut++ }
    }
    Write-Output "GATED_AREA: $cut entry edges outside the gates cut"
}

$zoneOf = @{}    # sectorKey -> zone number
$openDoors = New-Object System.Collections.Generic.HashSet[int]
$zone = 0
$log = @()

$reached = New-Object System.Collections.Generic.HashSet[string]
[void]$reached.Add($startKey)
foreach($ss in $startSeeds){ [void]$reached.Add($ss) }
$firstParent = @{}   # sector -> sector via which it was FIRST reached

# All triggers that switch a door at all - resolved once up front.
# Additionally, triggers that activate a key/puzzle item count: a lock does
# not necessarily have to open a door. Andrea2 is the case - the Puzzle
# Hole1 (item 146, room 239) switches the valve (typeId 172) with
# "Object: 128", no door, and thereby dropped out of consideration
# entirely. The chain crowbar -> valve -> lock simply did not exist in the
# model.
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
$consumed  = @{}   # typeId -> how many instances already sit in slots
$doorMask  = @{}   # doorItem -> mask bits set so far
$doorWhy   = @{}   # doorItem -> list of contributing requirements
$itemNeed  = @()   # per slot fill: which type, needed in which zone
$totalOfType = @{} # how many instances of each type exist in the level at all
foreach($i in $d.items){
    $ti = [int]$i.typeId
    if (-not $totalOfType.ContainsKey($ti)) { $totalOfType[$ti] = 0 }
    $totalOfType[$ti]++
}

# Which doors already stand open at level start (IFL_CODEBITS)? Needed for
# the island flood further down, which maps exactly the state before the
# first opened door - i.e. zone 0.
$doorsOpenAtStart = New-Object System.Collections.Generic.HashSet[int]
foreach($x in $openDoors){ [void]$doorsOpenAtStart.Add([int]$x) }

while ($true) {
    # flood
    $q = New-Object System.Collections.Generic.Queue[string]
    foreach($k in $reached){ $q.Enqueue($k) }
    while($q.Count -gt 0){
        $k = $q.Dequeue()
        if (-not $zoneOf.ContainsKey($k)) { $zoneOf[$k] = $zone }
        if (-not $adj.ContainsKey($k)) { continue }
        foreach($n in $adj[$k]){
            if ($reached.Contains($n)) { continue }
            if ($blocked.ContainsKey($n) -and -not $openDoors.Contains($blocked[$n])) { continue }
            # Sector of a flip variant: only carries once its gate is
            # released - without a declared gate never.
            if ($flipGateOf.ContainsKey($n) -and -not $openGates.Contains($flipGateOf[$n])) { continue }
            # Record the first-access parent node: only this way can it be
            # reconstructed later via WHICH edge a sector was reached for
            # the FIRST time. A path over the final state (all doors open)
            # shows some later, legal way instead and hides exactly the
            # too-early-opening edge one is looking for.
            $firstParent[$n] = $k
            [void]$reached.Add($n); $zoneOf[$n] = $zone; $q.Enqueue($n)
        }
    }

    # How many instances of each type are reachable? A count, not just
    # "yes/no" - two slots of the same type need two items.
    $haveCounts = @{}
    foreach($i in $d.items){
        $k = "$($i.room),$($i.sectorX),$($i.sectorZ)"
        if (-not $reached.Contains($k)) { continue }
        $ti = [int]$i.typeId
        if (-not $haveCounts.ContainsKey($ti)) { $haveCounts[$ti] = 0 }
        $haveCounts[$ti]++
    }

    #------------------------------------------------------------------------
    # Order of the locks when a key type is scarce.
    #
    # The loop used to run in array order, so a key was consumed at whichever
    # lock the flood happened to reach first. This choice can be wrong and
    # leave half the level unreachable.
    #
    # joby2 is the documented case: Key Item7 exists twice, but only one
    # instance is reachable at first. If the model consumes it at door 41,
    # that is the end - door 134 stays shut, behind it lies Key8, and
    # without it nothing continues. Correct is the reverse choice: door 134
    # first, it frees Key8, that opens 105/106, behind them the second
    # Key7, and THAT then opens door 41. The order decides over half the
    # level.
    #
    # Rule: if a type is scarce (fewer available instances than open locks
    # that need it), the locks behind which a still-unreached key lies come
    # first. That is the standard heuristic for key-lock reachability and
    # demonstrably correct here. Deliberately NOT flipped to the opposite
    # and simply opening all locks - that would be optimistic and would
    # produce unplayable seeds.
    #------------------------------------------------------------------------
    # Demand per type among the still-open locks.
    $trigOrder = @($allTrig)
    $demand = @{}
    foreach($e in $allTrig){
        if ($firedTrig.Contains($e.Id)) { continue }
        if ($e.Req.Kind -ne 'item' -or $null -eq $e.Req.NeedItemType) { continue }
        $nt = [int]$e.Req.NeedItemType
        if ($reusableKeys -contains $nt) { continue }   # Key8: unlimited, never scarce
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
        # Does this lock free a still-unreached key? For that, flood from
        # the target door's sector - other closed doors remain a bar - and
        # check whether a still-unreached key/puzzle item lies inside.
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
        # Ord as the second sort criterion: Windows PowerShell 5.1 knows no
        # -Stable; without the index the order of equal-ranked triggers
        # would not be reproducible - and reproducibility is a requirement
        # here.
        $trigOrder = @($ordered | Sort-Object Rank,Ord | ForEach-Object { $_.E })
    }

    # Fire reachable triggers / fill slots. An inserted item is consumed and
    # is no longer available for a second slot.
    foreach($e in $trigOrder){
        if ($firedTrig.Contains($e.Id)) { continue }
        if (-not (Test-ReqSector $e.Req $reached)) { continue }
        # Cutscene doors that presuppose a tool (rich1 hamgate: the hammer)
        # must not fire on merely reaching the trigger sector - Lara must
        # actually have the tool first. Without this bar the hammer would
        # be treated as "automatically present", although in rich1 it is a
        # real object that must first be found (user correction).
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
            # If the key type does not exist in the level at all, Lara
            # brings it along via gameflow (CMD_GIVEOBJ) - the same pattern
            # as with the crowbar. joby5 has a Keyhole7 but no Key Item7.
            if (-not $totalOfType.ContainsKey($nt)) {
                $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc=$e.Req.Desc; Gameflow=$true }
            }
            elseif ($reusableKeys -contains $nt) {
                # Multi-user: occupies no supply, competes with nothing.
                if ((&{ if($haveCounts.ContainsKey($nt)){$haveCounts[$nt]}else{0} }) -le 0) { continue }
                $itemNeed += ,@{ Type=$nt; Zone=$zone; Desc="$($e.Req.Desc) [reusable]"; Reusable=$true }
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
        # Credit this trigger's mask to all target doors.
        foreach($c in $e.T.commands){
            $tgt = $null
            if ($c.type -eq 'Object') { $tgt = [int]$c.index }
            elseif ($c.type -eq 'Cutscene' -and $cutsceneDoorByCutscene.ContainsKey([int]$c.index)) {
                $tgt = $cutsceneDoorByCutscene[[int]$c.index]
            }
            if ($null -eq $tgt) { continue }
            # Cables are live from level start. A normal floor trigger on a
            # cable switches it ON, not off - only a switch (or
            # antitrigger) cuts the power. Without this restriction joby5
            # would "open" cables 91 and 136 via a floor trigger in room
            # 138 already, which reverses the direction.
            if ($items.ContainsKey($tgt) -and [int]$items[$tgt].typeId -eq 98 -and
                $e.T.triggerType -notin @('Switch','HeavySwitch','AntiTrigger','HeavyAntiTrigger')) { continue }
            if (-not $doorMask.ContainsKey($tgt)) { $doorMask[$tgt] = 0; $doorWhy[$tgt] = @() }
            # Cutscene doors (rich1 hamgate) have no real 5-bit mask in the
            # IFL_CODEBITS sense - hamgate_init() sets them directly in the
            # script. Force the full mask once the trigger fires.
            if ($cutsceneDoorByCutscene.Values -contains $tgt -and $c.type -eq 'Cutscene') { $doorMask[$tgt] = 31 }
            else { $doorMask[$tgt] = $doorMask[$tgt] -bor ([int]$e.T.mask) }
            $doorWhy[$tgt] += ,$e.Req
        }
    }

    # A door only opens when its mask is complete. TR sets bits in
    # item->flags per trigger (IFL_CODEBITS, 5 bits) and opens only at
    # (flags & CODEBITS) == CODEBITS. Andrea1: slot 64 delivers mask 3,
    # slot 63 mask 28 - only together 31. Without this rule a one-item
    # solution would count as sufficient and the randomizer could make the
    # level unsolvable.
    $newlyOpened = @()
    foreach($kv in $doorSector.GetEnumerator()){
        $di = $kv.Key
        if ($openDoors.Contains($di)) { continue }
        if (-not $doorMask.ContainsKey($di)) { continue }
        if (($doorMask[$di] -band 31) -ne 31) { continue }
        $newlyOpened += ,@{ Door=$di; Req=$doorWhy[$di][-1]; All=$doorWhy[$di] }
    }

    # Crowbar doors: no trigger switches them, so the mask never fills.
    # They open as soon as Lara has the crowbar. The crowbar is NOT
    # consumed (unlike keys and puzzle items) - one suffices for any number
    # of such doors, hence no counting, no competition.
    if ($crowbarDoors.Count -gt 0) {
        $haveCrowbar = $false
        foreach($ci in $crowbarInLevel){
            if ($reached.Contains("$($ci.room),$($ci.sectorX),$($ci.sectorZ)")) { $haveCrowbar = $true; break }
        }
        if ($haveCrowbar) {
            foreach($cd in $crowbarDoors){
                if ($openDoors.Contains($cd)) { continue }
                if (-not $doorSector.ContainsKey($cd)) { continue }
                $newlyOpened += ,@{ Door=$cd; Req=@{ Desc='crowbar (not consumed)' } }
            }
        }
    }

    # Script doors: open as soon as the room is reached in which the engine
    # fires the event - not earlier.
    foreach($sd in $scriptDoors.GetEnumerator()){
        $di = [int]$sd.Key
        if ($openDoors.Contains($di)) { continue }
        $needRoom = [int]$sd.Value
        $hit = $false
        foreach($k in $reached){ if ([int](($k -split ',')[0]) -eq $needRoom) { $hit = $true; break } }
        if ($hit) { $newlyOpened += ,@{ Door=$di; Req=@{ Desc="script event (room $needRoom reached)" } } }
    }

    # Flipmap gates: free their sectors as soon as the triggering room is
    # reached - the same condition form as the script doors.
    $newlyOpenedGates = @()
    for($gi = 0; $gi -lt $flipGates.Count; $gi++){
        if ($openGates.Contains($gi)) { continue }
        $hit = $true
        # Condition "room reached" - as with the script doors.
        if ($null -ne $flipGates[$gi].AfterRoom) {
            $needRoom = [int]$flipGates[$gi].AfterRoom
            $seenRoom = $false
            foreach($k in $reached){ if ([int](($k -split ',')[0]) -eq $needRoom) { $seenRoom = $true; break } }
            if (-not $seenRoom) { $hit = $false }
        }
        # Condition "items in hand". Some flipmaps hang not on a place but
        # on equipment: andy1 only lets the bells fall once Lara has built
        # the slingshot from its two combo halves. Querying only the church
        # room would open the gate too early - Lara stands there long
        # before she has the parts.
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
        $why = if($flipGates[$gi].Why){ $flipGates[$gi].Why } else { 'flipmap' }
        $cond = @()
        if ($null -ne $flipGates[$gi].AfterRoom) { $cond += "room $($flipGates[$gi].AfterRoom) reached" }
        if ($null -ne $flipGates[$gi].NeedTypes) { $cond += "types $($flipGates[$gi].NeedTypes -join '+') present" }
        Write-Output "zone $zone -> $($zone+1): flipmap gate $($flipGates[$gi].Rooms -join '/') released - $why ($($cond -join ', '))"
    }
    $zone++
}

# Per key/puzzle item type the loosest (maximum) "must lie in zone <= N"
# requirement over all its slot instances - the same grouping the text
# report further down shows, pulled forward here so the JSON export can
# deliver it too. Gameflow types (no slot placed in the level) stay out -
# for them there is no meaningful "where to place" question.
$byType = @{}
foreach($n in ($itemNeed | Where-Object { -not $_.Gameflow })){
    $t = [int]$n.Type
    if (-not $byType.ContainsKey($t)) { $byType[$t] = @() }
    $byType[$t] += ,$n
}

#----------------------------------------------------------------------------
# Enforce equipment gates, not just report them.
#
# EQUIP_GATE long stood ONLY in the text report: it computed in which zone
# the switch is needed and showed whether the necessary equipment lies early
# enough today. Nothing of it appeared in the export. The randomizer was
# thus free to move Andrea1's revolver and lasersight - even behind the bell
# that cannot be shot down without them. That it went well so far was owed
# to the luck of the tested seeds.
#
# The same lesson as with VANILLA_TYPES and the island groups: a table that
# describes an intent and enforces nothing has no effect. Hence the zone now
# lands in $extraPool and becomes an ordinary constraint from there.
#
# With several gates for the same type, the STRICTEST applies, i.e. the
# smallest zone - the equipment must be there before EVERY switch that
# needs it.
#----------------------------------------------------------------------------
if ($EQUIP_GATE.ContainsKey($LevelName)) {
    foreach($g in $EQUIP_GATE[$LevelName].GetEnumerator()){
        $swItem = [int]$g.Key
        $gz = ($log | Where-Object { $_.Req.Desc -like "*Item $swItem *" } | ForEach-Object { $_.Zone } | Sort-Object | Select-Object -First 1)
        if ($null -eq $gz) {
            Write-Output "WARNING: EQUIP_GATE - switch item $swItem appears in no zone transition, gate has no effect"
            continue
        }
        foreach($nt in $g.Value.Need){
            $t = [int]$nt
            if ($extraPool.ContainsKey($t)) { $extraPool[$t] = [Math]::Min([int]$extraPool[$t], [int]$gz) }
            else                            { $extraPool[$t] = [int]$gz }
        }
        Write-Output "equipment gate: switch item $swItem in zone $gz -> types $($g.Value.Need -join ', ') limited to zone <= $gz"
    }
}
#----------------------------------------------------------------------------
# Island flood: a second, sharper pass that determines ONLY the group.
#
# It runs from Lara's start, with all doors closed (except those open from
# the outset) and additionally with the group's deadstop rooms barred. What
# remains is the start island.
#
# Why separate and not in the main run: in joby2 the track bed is both -
# the sneak way underneath the roll gate AND the legitimate main way. Bar
# it in the main run and "never reached" climbs from 463 to 6751 and Key
# Item7 in room 52 becomes unreachable, so vanilla fails. The main run
# needs the way, the island must not have it. Hence two runs.
#----------------------------------------------------------------------------
$islandSectors = @{}   # typeId -> HashSet of the allowed sectors
$islandBanned  = @{}   # typeId -> HashSet of the FORBIDDEN sectors (inversion)
$islandHeightScope = @{}   # typeId -> HashSet in which the height limit applies
$islandRequire = @{}   # typeId -> HashSet in which at least one instance MUST lie
foreach($ig in $islandGroups){
    if ($null -eq $ig.IslandTypes -or $null -eq $ig.DeadstopRooms) { continue }
    $stopR = @($ig.DeadstopRooms | ForEach-Object { [int]$_ })
    foreach($b in @($stopR)){
        if ($flipVariantRooms.ContainsKey($b)) { $stopR += [int]$flipVariantRooms[$b] }
        foreach($kvv in $flipVariantRooms.GetEnumerator()){ if ([int]$kvv.Value -eq $b) { $stopR += [int]$kvv.Key } }
    }
    $stopR = @($stopR | Sort-Object -Unique)

    # Two readings of "island", and they really mean different things:
    #
    # WITHOUT DoorsOpen the flood runs with ALL doors shut, only those open
    # from level start count. That describes "what is walkable from the
    # first moment" and is right for andy1's area before the teleporter or
    # joby2's island before the crane.
    #
    # WITH DoorsOpen only the deadstop counts: the doors count as open, and
    # the island is simply everything on this side of the bars. That is the
    # user's reading when he names room pairs - "63 yes, 111 no". rich1's
    # Key Item8 needs it: its allowed rooms 81 and 43 lie behind doors that
    # only open in the course of play, and the closed reading therefore
    # wrongly barred them.
    $doorsOpen = ($ig.DoorsOpen -eq $true)

    # Normally the flood begins at Lara. A section in the MIDDLE of the
    # level cannot be described that way though - rich1's Puzzle Item4 lies
    # between two bars, and from Lara's start one would never get past the
    # first. StartRoom therefore sets the seed freely; taken is the first
    # walkable sector of the room that hangs in the graph at all.
    $seedKey = $startKey
    if ($null -ne $ig.StartRoom) {
        $seedKey = $null
        foreach($cand in @($d.sectors | Where-Object { [int]$_.room -eq [int]$ig.StartRoom -and -not $_.isWall -and $_.hasFloor } | Sort-Object sectorX,sectorZ)){
            $ck = "$($cand.room),$($cand.sectorX),$($cand.sectorZ)"
            if ($adj.ContainsKey($ck)) { $seedKey = $ck; break }
        }
        if ($null -eq $seedKey) { Write-Output "WARNING: island group - StartRoom $($ig.StartRoom) has no walkable sector in the graph" }
    }

    $isl = New-Object System.Collections.Generic.HashSet[string]
    if ($null -ne $seedKey) {
        $qi = New-Object System.Collections.Generic.Queue[string]
        [void]$isl.Add($seedKey); $qi.Enqueue($seedKey)
        while($qi.Count -gt 0){
            $k = $qi.Dequeue()
            if (-not $adj.ContainsKey($k)) { continue }
            foreach($n in $adj[$k]){
                if ($isl.Contains($n)) { continue }
                if ($stopR -contains [int](($n -split ',')[0])) { continue }
                if (-not $doorsOpen -and $blocked.ContainsKey($n) -and -not $doorsOpenAtStart.Contains([int]$blocked[$n])) { continue }
                if ($flipGateOf.ContainsKey($n)) { continue }
                [void]$isl.Add($n); $qi.Enqueue($n)
            }
        }
    }
    $islRooms = @($isl | ForEach-Object { [int](($_ -split ',')[0]) } | Sort-Object -Unique)
    Write-Output "island group (deadstops $($stopR -join ', ')): $($isl.Count) sectors in $($islRooms.Count) rooms"
    foreach($it in $ig.IslandTypes){ $islandSectors[[int]$it] = $isl }
    # Inversion: types that may NOT lie on the island.
    #
    # andy1 needs exactly that. The teleporter creates no zone transition -
    # the flood simply runs through -, so zone 0 comprises both the area
    # BEFORE the teleporter and the one behind it up to the church. A
    # slingshot half in Lara's start room 70 satisfies "zone 0" and is
    # still the softlock: Lara goes through the teleporter without having
    # collected it and never comes back. Seen by the user in the test
    # level.
    #
    # Together with the zone limit that yields "after the point of no
    # return and before the church" - which neither of the two conditions
    # can express alone.
    foreach($it in $ig.NotOnIslandTypes){ $islandBanned[[int]$it] = $isl }
    # The height limit is a property of the GROUP, not the zone. Hanging it
    # on the zone was too broad: zone 0 comprises 7756 sectors in joby2,
    # the island only 1900 - for Key Item7, which may lie outside the
    # island, the bound thereby acted in half the level. So every type of
    # the group gets the island set as its scope, including those not
    # restricted to the island.
    foreach($rt in $ig.RequireTypes){ $islandHeightScope[[int]$rt] = $isl }
    # RequireTypes is more than a filter: of each of these types at least
    # one instance must lie WITHIN the group, otherwise the group is not
    # solvable out of itself. That cannot be expressed as a sector filter -
    # a filter says where a SINGLE instance may lie, here it is a statement
    # about ALL instances of a type together. The randomizer therefore gets
    # the set separately and forces one instance in; whoever only filters
    # cannot enforce it.
    foreach($rt in $ig.RequireTypes){ $islandRequire[[int]$rt] = $isl }
}

# Derive the height limit per key type from the island groups. Baseline is
# the most frequent floor value of the baseline room's walkable sectors -
# the average of a single corner would be too random, the mode hits the
# actual hall floor.
$keyMinFloorY = @{}
foreach($ig in $islandGroups){
    # Without BaselineRoom there is no height limit - a group may also
    # consist only of deadstops. Without this check [int]$null would become
    # 0 and thereby accidentally take room 0 as the baseline.
    if ($null -eq $ig.BaselineRoom) { continue }
    $bs = @($d.sectors | Where-Object { [int]$_.room -eq [int]$ig.BaselineRoom -and -not $_.isWall -and $_.hasFloor })
    if ($bs.Count -eq 0) { Write-Output "WARNING: island group - baseline room $($ig.BaselineRoom) has no walkable sectors"; continue }
    $baseY = [int](($bs | Group-Object floorY | Sort-Object Count -Descending | Select-Object -First 1).Name)
    $limit = $baseY - [int]$ig.MaxYAbove
    # The limit applies ONLY in the zone in which the group lies -
    # otherwise it hits half the level along. The zone follows from the
    # baseline room itself, so it needs no extra maintenance.
    $bz = @()
    foreach($b in $bs){ $bk = "$($b.room),$($b.sectorX),$($b.sectorZ)"; if ($zoneOf.ContainsKey($bk)) { $bz += ,[int]$zoneOf[$bk] } }
    if ($bz.Count -eq 0) { Write-Output "WARNING: island group - baseline room $($ig.BaselineRoom) is never reached"; continue }
    $gz = ($bz | Sort-Object | Select-Object -First 1)
    foreach($rt in $ig.RequireTypes){ $keyMinFloorY[[int]$rt] = @{ Limit = $limit; Zone = $gz } }
    Write-Output "island group: baseline room $($ig.BaselineRoom) y=$baseY in zone $gz, keys there at most $($ig.MaxYAbove) above (floorY >= $limit) for types $($ig.RequireTypes -join ', ')"
}

#----------------------------------------------------------------------------
# A key may never lie behind a door it itself opens.
#
# Second attempt. The first (commit 3af3f46, taken back with a7e4a70)
# failed at the UNION: it collected per type the doors of ALL locks of that
# type and barred everything that lay behind any of them. In Andrea3 that
# yielded doors 25/24/77/94 from several locks for Key Item2 and forbade
# its vanilla place in room 119.
#
# Correct is the INTERSECTION. A key of this type can be needed at any of
# its locks; forbidden is therefore only what lies behind EVERY one of
# these doors - there, namely, it could reach no lock at all. What lies
# behind one but before another stays allowed: from there Lara reaches at
# least one lock.
#
# With exactly one lock, intersection and union are the same, so the cases
# in which the first attempt was right are preserved.
#
# Per lock an own flood from Lara's start with EXACTLY THIS door shut and
# all others open. A detour around the door changes nothing about that -
# it is simply used along.
#----------------------------------------------------------------------------
# Mix hand-set sector bars per type (TYPE_EXCLUDE_SECTORS) into
# bannedSectors. ONCE here, not in the constraint loops: there are THREE
# paths through which a constraint can arise - derived from a lock,
# EXTRA_POOL_TYPES and POOL_TYPES -, and on the first attempt I had patched
# only the first two. joby2's safeguard runs via the third, and
# bannedSectors stayed silently empty.
foreach($kv in $typeExcludeSectors.GetEnumerator()){
    $nt = [int]$kv.Key
    if (-not $islandBanned.ContainsKey($nt)) { $islandBanned[$nt] = New-Object System.Collections.Generic.HashSet[string] }
    foreach($sk in $kv.Value){ [void]$islandBanned[$nt].Add([string]$sk) }
}

$forbiddenBehindOwnDoor = @{}
if ($null -ne $startKey) {
    # Per DOOR, not per lock. A lock often opens several doors - Andrea3's
    # Key Hole2 four at once (24, 25, 77, 94) -, and closing all of them
    # together bars far more than each single one. Locks also share doors:
    # 24 and 25 there hang on Key Hole1 too, because TR collects the code
    # bits of several triggers. Closing all four at once cut room 119 and
    # thereby forbade Key Item2 its vanilla place.
    $typeDoors = @{}   # typeId -> set of door items
    foreach($e in $allTrig){
        if ($e.Req.Kind -ne 'item' -or $null -eq $e.Req.NeedItemType) { continue }
        $nt = [int]$e.Req.NeedItemType
        foreach($c in $e.T.commands){
            if ($c.type -ne 'Object' -or -not $doorSector.ContainsKey([int]$c.index)) { continue }
            if (-not $typeDoors.ContainsKey($nt)) { $typeDoors[$nt] = New-Object System.Collections.Generic.HashSet[int] }
            [void]$typeDoors[$nt].Add([int]$c.index)
        }
    }

    # The hammer has no lock in the usual sense - its door does not open
    # via an object command but via a cutscene (rich1: hamgate, door 153).
    # For placement, though, exactly the same holds: it may not lie behind
    # the door it itself opens. User: "It may not be placed further than
    # the door."
    #
    # The flood already knew this condition (it only lets the cutscene door
    # fire once the hammer is reachable); what was missing was the
    # inversion for placement. Both now hang on the same rule.
    if ($null -ne $hammerType -and $cutsceneDoors.Count -gt 0) {
        if (-not $typeDoors.ContainsKey($hammerType)) { $typeDoors[$hammerType] = New-Object System.Collections.Generic.HashSet[int] }
        foreach($cd in $cutsceneDoors.Keys){
            if ($doorSector.ContainsKey([int]$cd)) { [void]$typeDoors[$hammerType].Add([int]$cd) }
        }
    }

    $perLock = @{}   # typeId -> list of HashSets, one per door
    foreach($tdKv in $typeDoors.GetEnumerator()){
      $nt = [int]$tdKv.Key
      foreach($di in $tdKv.Value){
        $shut = New-Object System.Collections.Generic.HashSet[string]
        foreach($kv in $blocked.GetEnumerator()){ if ([int]$kv.Value -eq $di) { [void]$shut.Add([string]$kv.Key) } }
        if ($shut.Count -eq 0 -or $shut.Contains($startKey)) { continue }

        $rr = New-Object System.Collections.Generic.HashSet[string]
        $qq = New-Object System.Collections.Generic.Queue[string]
        [void]$rr.Add($startKey); $qq.Enqueue($startKey)
        while($qq.Count -gt 0){
            $k = $qq.Dequeue()
            if (-not $adj.ContainsKey($k)) { continue }
            foreach($n in $adj[$k]){
                if ($rr.Contains($n) -or $shut.Contains($n)) { continue }
                [void]$rr.Add($n); $qq.Enqueue($n)
            }
        }
        # Behind this door = reached in the main run, but not without it.
        $behind = New-Object System.Collections.Generic.HashSet[string]
        foreach($k0 in $reached){ if (-not $rr.Contains($k0)) { [void]$behind.Add($k0) } }
        if ($behind.Count -eq 0) { continue }

        if (-not $perLock.ContainsKey($nt)) { $perLock[$nt] = @() }
        $perLock[$nt] += ,$behind
      }
    }

    foreach($kv in $perLock.GetEnumerator()){
        $nt = [int]$kv.Key
        $sets = @($kv.Value)
        $cut = New-Object System.Collections.Generic.HashSet[string]
        foreach($s0 in $sets[0]){ [void]$cut.Add($s0) }
        for($si = 1; $si -lt $sets.Count; $si++){ $cut.IntersectWith($sets[$si]) }
        if ($cut.Count -eq 0) { continue }
        $forbiddenBehindOwnDoor[$nt] = $cut
        Write-Output "barred behind own door: type $nt, $($sets.Count) door(s) -> $($cut.Count) sectors (intersection)"
    }
}

# Resolve NO_KEY_ISLAND: flood the section and hang its rooms onto
# $noPlacement. Must stand here - after the complete graph build, so the
# bars and hand edges are in, and before the key evaluation, which reads
# $noPlacement.
# The flooded sector set is additionally collected and exported
# (keyBarredSectors). Reason: internally NO_KEY_ISLAND ends up as a ROOM
# list in $noPlacement, and the key evaluation reads it - but only for
# types for which a key constraint exists at all. In andy3 there is not a
# single one (chalk and crowbar are tools without a receptacle), so the
# deadstop never reached them. As its own sector set in the export, every
# generator can apply it, even for types without a receptacle (#61).
$keyBarred = New-Object System.Collections.Generic.HashSet[string]
# Rooms barred ONLY because of NO_KEY_ISLAND, and the types that stay
# exempt from it (#61). Both are needed because the bar acts in TWO places:
# as a sector set in the export and as excludeRooms per type.
$islandRooms = @()
$keyBarredExempt = @()
foreach($ni in $noKeyIslands){
    $stopN = @($ni.DeadstopRooms)
    if ($ni.ExemptTypes) { $keyBarredExempt += @($ni.ExemptTypes | ForEach-Object { [int]$_ }) }
    $seedN = $null
    foreach($cand in @($d.sectors | Where-Object { [int]$_.room -eq [int]$ni.StartRoom -and -not $_.isWall -and $_.hasFloor } | Sort-Object sectorX,sectorZ)){
        $ck = "$($cand.room),$($cand.sectorX),$($cand.sectorZ)"
        if ($adj.ContainsKey($ck)) { $seedN = $ck; break }
    }
    if ($null -eq $seedN) { Write-Output "WARNING: NO_KEY_ISLAND - StartRoom $($ni.StartRoom) has no walkable sector in the graph"; continue }
    $islN = New-Object System.Collections.Generic.HashSet[string]
    $qn = New-Object System.Collections.Generic.Queue[string]
    [void]$islN.Add($seedN); $qn.Enqueue($seedN)
    while($qn.Count -gt 0){
        $k = $qn.Dequeue()
        if (-not $adj.ContainsKey($k)) { continue }
        foreach($n in $adj[$k]){
            if ($islN.Contains($n)) { continue }
            if ($stopN -contains [int](($n -split ',')[0])) { continue }
            [void]$islN.Add($n); $qn.Enqueue($n)
        }
    }
    foreach($k in $islN){ [void]$keyBarred.Add($k) }
    $rmN = @($islN | ForEach-Object { [int](($_ -split ',')[0]) } | Sort-Object -Unique)
    $islandRooms = @($islandRooms + $rmN | Sort-Object -Unique)
    $noPlacement = @($noPlacement + $rmN | Sort-Object -Unique)
    Write-Output "NO_KEY_ISLAND (start $($ni.StartRoom), deadstops $($stopN -join ', ')): $($islN.Count) sectors in $($rmN.Count) rooms without key items"
}

# NOTE: here stood the derived rule "a key may never lie behind a door it
# itself opens" (commit 3af3f46). It has been removed again because it was
# demonstrably too coarse: in Andrea3 it barred the VANILLA position of Key
# Item2 (item 101, room 119) and shrank its allowed rooms from 32 to 6.
# The cause was presumably that per key type it united the doors of ALL
# locks of that type instead of considering them individually - the report
# showed for type 197 the doors 25/24/77/94 from several locks thrown
# together.
#
# The yardstick stays valid and is the acceptance condition for every new
# attempt: a rule that forbids where vanilla puts the item is wrong by
# construction. See task #49.


$keyConstraints = @()
foreach($kv in $byType.GetEnumerator()){
    $nt = [int]$kv.Key
    # Stays vanilla: requirement visible in the report above, but not in the pool.
    if ($vanillaTypes -contains $nt) { continue }
    # The maximum over all locks - even for reusable keys.
    #
    # Here briefly stood a minimum for the reusable ones, with the
    # reasoning: one instance serves all locks, so it must be there before
    # the earliest. That sounds compelling and is still wrong, and the user
    # refuted it at rich1. There the first Key-Item8 lock (item 55, room
    # 26) lies in zone 0, so the minimum would have nailed the key to zone
    # 0 - but the user expressly names rooms 81 and 43 from zones 2 and 3
    # as allowed.
    #
    # The thinking error: a lock in zone 0 does not mean it MUST be served
    # first. rich1 has seven different doors from the zone-0 into the
    # zone-1 area; doors 64/65 of lock 55 are only one of them. Lara can
    # continue another way, fetch the key later and come back. Which locks
    # really lie on the critical path the zone model does not see - and
    # that is why this limiting is done by an island group with deadstops
    # here, not by the zone.
    $maxZ = ($kv.Value | ForEach-Object { [int]$_.Zone } | Measure-Object -Maximum).Maximum
    # Exempt types do not see the NO_KEY_ISLAND rooms (#61).
    $npT = if ($keyBarredExempt -contains $nt) { @($noPlacement | Where-Object { $islandRooms -notcontains $_ }) } else { $noPlacement }
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $npT + $noKeyRooms | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=$maxZ; instances=$kv.Value.Count; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms; minFloorY=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Limit}else{$null}); minFloorYZone=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Zone}else{$null}); islandSectors=$(if($islandSectors.ContainsKey($nt)){@($islandSectors[$nt])}else{$null}); bannedSectors=$(if($islandBanned.ContainsKey($nt)){@($islandBanned[$nt])}else{$null}); forbiddenSectors=$(if($forbiddenBehindOwnDoor.ContainsKey($nt)){@($forbiddenBehindOwnDoor[$nt])}else{$null}); minFloorYIsland=$(if($islandHeightScope.ContainsKey($nt)){@($islandHeightScope[$nt])}else{$null}); requireInIsland=$(if($islandRequire.ContainsKey($nt)){@($islandRequire[$nt])}else{$null}) }
}
# Append hand-set extra types, unless the model has already derived them
# itself anyway (then the derived limit applies, not the hand entry).
foreach($kv in $extraPool.GetEnumerator()){
    $nt = [int]$kv.Key
    if ($byType.ContainsKey($nt)) { continue }
    # Exempt types do not see the NO_KEY_ISLAND rooms (#61).
    $npT = if ($keyBarredExempt -contains $nt) { @($noPlacement | Where-Object { $islandRooms -notcontains $_ }) } else { $noPlacement }
    $exRooms = @( @( if($typeExclude.ContainsKey($nt)){ $typeExclude[$nt] } ) + $npT + $noKeyRooms | Sort-Object -Unique )
    $alRooms = @( if($typeAllow.ContainsKey($nt)){ $typeAllow[$nt] } )
    $keyConstraints += [PSCustomObject]@{ typeId=$nt; maxZone=[int]$kv.Value; instances=0; reusable=($reusableKeys -contains $nt); excludeRooms=$exRooms; allowRooms=$alRooms; minFloorY=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Limit}else{$null}); minFloorYZone=$(if($keyMinFloorY.ContainsKey($nt)){$keyMinFloorY[$nt].Zone}else{$null}); islandSectors=$(if($islandSectors.ContainsKey($nt)){@($islandSectors[$nt])}else{$null}); bannedSectors=$(if($islandBanned.ContainsKey($nt)){@($islandBanned[$nt])}else{$null}); forbiddenSectors=$(if($forbiddenBehindOwnDoor.ContainsKey($nt)){@($forbiddenBehindOwnDoor[$nt])}else{$null}); minFloorYIsland=$(if($islandHeightScope.ContainsKey($nt)){@($islandHeightScope[$nt])}else{$null}); requireInIsland=$(if($islandRequire.ContainsKey($nt)){@($islandRequire[$nt])}else{$null}) }
}

if ($ExportZonesJson -ne "") {
    # Machine-readable export: for every sector that is both reached in the
    # flood and actually item-capable per the exporter (validForItems), its
    # zone. Only validForItems sectors, so a downstream patch generator
    # never places an item on a wall, a portal or an otherwise unsuitable
    # sector. The same validity check the exporter itself keeps clean
    # remains the basis here.
    # In two passes, because the second needs the first's set.
    $exportKeys = New-Object System.Collections.Generic.HashSet[string]
    foreach($kv in $zoneOf.GetEnumerator()){
        $s = $sec[$kv.Key]
        if (-not $s -or -not $s.validForItems) { continue }
        if ($null -ne $levelMinFloorY -and [int]$s.floorY -lt $levelMinFloorY) { continue }
        if ($null -ne $levelMinClearance -and [int]$s.clearance -lt $levelMinClearance) { continue }
        if ($noItemRooms -contains [int]$s.room) { continue }
        if ($noItemSectors -contains "$($s.room),$($s.sectorX),$($s.sectorZ)") { continue }
        if ($ybandBarred.Contains("$($s.room),$($s.sectorX),$($s.sectorZ)")) { continue }
        [void]$exportKeys.Add($kv.Key)
    }

    # APPROACH: for every sector, the sectors Lara steps on shortly before
    # it on her FIRST-ACCESS path.
    #
    # Needed for new enemies. The trigger may never lie on the enemy's own
    # sector - then the enemy spawns into Lara (user finding) -, but it
    # must be crossed reliably, or the enemy never wakes up. Both at once
    # is satisfied by exactly one sector: one that lies on the way there.
    #
    # $firstParent is the right source for that and not some path through
    # the finished level: it records via WHICH edge a sector was reached
    # for the first time, i.e. the way Lara really takes when she first
    # arrives there.
    #
    # Three steps of distance are the target; 2 to 8 are taken along and
    # sorted by closeness to 3, so the randomizer can dodge when the best
    # sector tolerates no trigger (lava, climb, monkey, trigtrigger or
    # already a trigger - only the level itself decides that).
    $approachOf = @{}
    foreach($k in $exportKeys){
        $cands = @()
        $cur = $k
        for($step = 1; $step -le 8; $step++){
            if (-not $firstParent.ContainsKey($cur)) { break }
            $cur = $firstParent[$cur]
            if ($step -lt 2) { continue }
            if ($exportKeys.Contains($cur)) { $cands += @{ K = $cur; D = [Math]::Abs($step - 3) } }
        }
        if ($cands.Count -gt 0) {
            $approachOf[$k] = @($cands | Sort-Object { $_.D } | Select-Object -First 3 | ForEach-Object { $_.K })
        }
    }

    # Wall edges per sector (#61). Needed for the OCB pickups that must
    # hang on a wall (crowbar OCB 2, Joby crow OCB 9).
    #
    # The ANGLE POINTS AT THE WALL, not away from it - measured at vanilla
    # Andrea2 item 121: sector (2,1) in room 166, wall at -Z, item angle
    # -32768 (180 degrees, facing -Z). That fits how the models are built:
    # their mesh sits around +450..500 in model Z, so it lands on the wall
    # face, and Lara moves in at +215 in between and looks at the wall
    # (CrowbarPickUpPosition in game\pickup.cpp).
    #
    # TR directions: 0 = +Z, 16384 = +X, -32768 = -Z, -16384 = -X.
    # A neighbor counts as wall if it carries isWall or does not exist at
    # all (outside the room's sector grid - the room ends there).
    $wallDirections = @(
        @{ dx = 0; dz =  1; angle =      0 },
        @{ dx = 1; dz =  0; angle =  16384 },
        @{ dx = 0; dz = -1; angle = -32768 },
        @{ dx =-1; dz =  0; angle = -16384 }
    )

    $rows = @()
    foreach($kv in $zoneOf.GetEnumerator()){
        if (-not $exportKeys.Contains($kv.Key)) { continue }
        $s = $sec[$kv.Key]
        $wa = @()
        foreach($wr in $wallDirections){
            $nk = "$([int]$s.room),$([int]$s.sectorX + $wr.dx),$([int]$s.sectorZ + $wr.dz)"
            if ((-not $sec.ContainsKey($nk)) -or $sec[$nk].isWall) { $wa += [int]$wr.angle }
        }
        $rows += [PSCustomObject]@{ room=[int]$s.room; sectorX=[int]$s.sectorX; sectorZ=[int]$s.sectorZ; zone=[int]$kv.Value; floorY=[int]$s.floorY; clearance=[int]$s.clearance;
                                    wallAngles=@($wa);
                                    approach=@( if($approachOf.ContainsKey($kv.Key)){ $approachOf[$kv.Key] } ) }
    }
    $withWall = @($rows | Where-Object { $_.wallAngles.Count -gt 0 }).Count
    Write-Output "wall spots: $withWall of $($rows.Count) sectors have at least one wall edge"
    $withApproach = @($rows | Where-Object { $_.approach.Count -gt 0 }).Count
    Write-Output "approach sectors: $withApproach of $($rows.Count) sectors have at least one"
    $maxZoneOut = if($rows.Count -gt 0){ ($rows | Measure-Object -Property zone -Maximum).Maximum } else { 0 }
    # Append pool types without a zone limit. maxZone gets the highest value
    # occurring in the level, so really every zone stands open; island and
    # height limits stay empty, for these types belong to no group.
    foreach($ptKv in $poolTypes.GetEnumerator()){
        $pt = [int]$ptKv.Key
        $ptZone = if ($null -eq $ptKv.Value) { $maxZoneOut } else { [int]$ptKv.Value }
        if (@($keyConstraints | Where-Object { [int]$_.typeId -eq $pt }).Count -gt 0) { continue }
        $inst = @($d.items | Where-Object { [int]$_.typeId -eq $pt }).Count
        # NO_KEY_ROOMS expressly does NOT apply here. Pool types have no
        # chain in the level - joby5's Puzzle 1 and 2 are only needed at
        # the very end, and by then the power has long been switched off.
        # User: "We can leave the puzzles, only keys matter."
        # TYPE_EXCLUDE_ROOMS very much applies here - an empty list stood
        # here before, and that was simply a gap. A pool type has no lock
        # and therefore no derived forcing condition; the hand curation is
        # thus the ONLY thing that can keep it away from anywhere. andy3 is
        # the case that uncovered it: chalk and crowbar have no lock in the
        # level there and must still stay before the teleporter.
        # A pool type carries EXACTLY THE SAME curation fields as a type
        # with a lock - the line below is, apart from $pt instead of $nt,
        # that of the main path.
        #
        # Before, fixed $null values stood here, and that silently
        # swallowed curation three times in a row: first excludeRooms
        # (andy3's chalk), then forbiddenSectors and finally islandSectors
        # (rich1's hammer). The thinking error was the same every time - a
        # pool type has no lock, SO the hand curation is the only thing
        # that can hold it anywhere, and not, say, dispensable.
        $ptEx = @( if($typeExclude.ContainsKey($pt)){ $typeExclude[$pt] } )
        $ptAl = @( if($typeAllow.ContainsKey($pt)){ $typeAllow[$pt] } )
        $keyConstraints += [PSCustomObject]@{ typeId=$pt; maxZone=$ptZone; instances=$inst
            reusable=($reusableKeys -contains $pt); excludeRooms=$ptEx; allowRooms=$ptAl
            minFloorY=$(if($keyMinFloorY.ContainsKey($pt)){$keyMinFloorY[$pt].Limit}else{$null})
            minFloorYZone=$(if($keyMinFloorY.ContainsKey($pt)){$keyMinFloorY[$pt].Zone}else{$null})
            islandSectors=$(if($islandSectors.ContainsKey($pt)){@($islandSectors[$pt])}else{$null})
            bannedSectors=$(if($islandBanned.ContainsKey($pt)){@($islandBanned[$pt])}else{$null})
            forbiddenSectors=$(if($forbiddenBehindOwnDoor.ContainsKey($pt)){@($forbiddenBehindOwnDoor[$pt])}else{$null})
            minFloorYIsland=$(if($islandHeightScope.ContainsKey($pt)){@($islandHeightScope[$pt])}else{$null})
            requireInIsland=$(if($islandRequire.ContainsKey($pt)){@($islandRequire[$pt])}else{$null}) }
        $how = if ($null -eq $ptKv.Value) { 'without a zone limit' } else { "limited to zone <= $ptZone" }
        Write-Output "pool type $pt taken in $how ($inst instances in the level)"
    }
    # Origin stamp: the checksum of THIS script. The randomizer does not
    # read the curation tables, only this export - a change to the tables
    # thus only acts after regenerating. Exactly that surfaced on
    # 2026-08-23: five exports in the working folder dated from 13/14 Aug
    # and still carried the refuted global HK offset, while the table had
    # long known it only for rich2. With the stamp the difference is
    # visible instead of being hunted in game.
    $curationStamp = (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash.Substring(0,16)

    [PSCustomObject]@{ level=$LevelName; curationStamp=$curationStamp; maxZone=$maxZoneOut; sectors=$rows; keyConstraints=$keyConstraints; fixedItems=$fixedItems; coupledTriggers=$coupledTriggers; enemyTypes=$enemyTypes; enemySounds=$enemySounds; requiredInjections=$requiredInjections; typeOffsets=$typeOffsets; keepOcbTypes=@($keepOcbTypes); noWeaponRooms=@($noWeaponRooms); weaponTypes=@($WEAPON_TYPES); typeNoRooms=@($typeNoRooms); typeNoSectors=@($typeExcludeSectors.GetEnumerator() | ForEach-Object { [PSCustomObject]@{ typeId=[int]$_.Key; sectors=@($_.Value) } }); antitriggerImmune=@($antitriggerImmune); keyBarredSectors=@($keyBarred); keyBarredExemptTypes=@($keyBarredExempt | Sort-Object -Unique); flagPresets=@($flagPresets) } | ConvertTo-Json -Depth 4 -Compress | Out-File $ExportZonesJson -Encoding utf8
    Write-Output "zone export written: $ExportZonesJson ($($rows.Count) sectors, maxZone=$maxZoneOut, $($keyConstraints.Count) key constraints)"
}

if ($DebugPathTo -ne "") {
    Write-Output "--- Debug: FIRST-ACCESS path to $DebugPathTo ---"
    if (-not $zoneOf.ContainsKey($DebugPathTo)) {
        Write-Output "  target never reached - no path in the walkable graph."
    } else {
        # Uses $firstParent from the flood itself, NOT a new BFS over the
        # final state: sought is the edge via which the sector was reached
        # for the first time. A BFS with all doors open at the end finds
        # some later legal way instead and hides exactly the
        # too-early-opening edge (at Andrea1/room 117 it wrongly ran
        # through the long-opened door 53 instead of over the real early
        # access).
        $path = @($DebugPathTo); $cur = $DebugPathTo
        $guard = 0
        while ($firstParent.ContainsKey($cur) -and $guard -lt 10000) { $cur = $firstParent[$cur]; $path = ,$cur + $path; $guard++ }
        for($i=0; $i -lt $path.Count; $i++){
            $zHere = if($zoneOf.ContainsKey($path[$i])){ $zoneOf[$path[$i]] } else { '?' }
            $roomChange = if($i -gt 0){ ($path[$i] -split ',')[0] -ne ($path[$i-1] -split ',')[0] } else { $false }
            $marker = if($roomChange){ " <- room change" } else { "" }
            "  [{0,3}] {1,-14} zone {2}{3}" -f $i,$path[$i],$zHere,$marker
        }
    }
    Write-Output ""
}

# --- Output -----------------------------------------------------------------
Write-Output "=============================================================="
Write-Output " Progression zones: $LevelName"
Write-Output "=============================================================="
if ($startSeeds.Count -gt 0) {
    Write-Output "Lara's start: room $($START_ROOM[$LevelName]) (after the intro cutscene; the Lara item lies in room $($lara.room)) -> zone 0"
} else {
    Write-Output "Lara's start: room $($lara.room), sector ($($lara.sectorX),$($lara.sectorZ)) -> zone 0"
}
Write-Output ""
Write-Output "--- Zone transitions (blockers) ---"
foreach($e in $log){
    $di = $e.Door; $it = $items[$di]
    "zone {0} -> {1}: door item {2} (typeId {3}, room {4})  opened by: {5}" -f `
        $e.Zone, ($e.Zone+1), $di, $it.typeId, $it.room, $e.Req.Desc
}
Write-Output ""
if ($DebugSectors.Count -gt 0) {
    Write-Output "--- Debug: single sectors ---"
    foreach($dk in $DebugSectors){
        $z = if($zoneOf.ContainsKey($dk)){ "zone $($zoneOf[$dk])" } else { "NOT REACHED" }
        $isBlocked = $blocked.ContainsKey($dk)
        $edges = if($adj.ContainsKey($dk)){ ($adj[$dk] | Sort-Object) -join ', ' } else { '(no edges in the graph)' }
        "  {0}: {1}  blocked(door)={2}  edges -> {3}" -f $dk,$z,$isBlocked,$edges
    }
    Write-Output ""
}

$maxZone = ($zoneOf.Values | Measure-Object -Maximum).Maximum
Write-Output "--- Sectors per zone ---"
for($z=0; $z -le $maxZone; $z++){
    $cnt = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z }).Count
    $rms = ($zoneOf.GetEnumerator() | Where-Object { $_.Value -eq $z } | ForEach-Object { ($_.Key -split ',')[0] } | Sort-Object -Unique).Count
    "zone {0}: {1,5} sectors in {2,3} rooms" -f $z,$cnt,$rms
}
# Scenery rooms do not count as missing stock - they are not meant to be
# entered, so their absence is no deficit of the analysis. The same holds
# for the hand-curated EXCLUDE_ROOMS: that they are not reached is the
# desired result, not a hole in the model - otherwise Andrea2 with its 763
# barred sectors would look as if the analysis had become 18 percent worse,
# although the remaining gap is unchanged small.
$unreachedSec = @($d.sectors | Where-Object {
    -not $_.isWall -and $cameraRooms -notcontains [int]$_.room -and
    $excludeRooms -notcontains [int]$_.room -and
    -not $zoneOf.ContainsKey("$($_.room),$($_.sectorX),$($_.sectorZ)") })
Write-Output "never reached: $($unreachedSec.Count) walkable sectors"
if ($excludeRooms.Count -gt 0) {
    $exSec = @($d.sectors | Where-Object { -not $_.isWall -and $excludeRooms -contains [int]$_.room })
    Write-Output "hand-barred (EXCLUDE_ROOMS): $($exSec.Count) sectors in $($excludeRooms.Count) rooms - intended, not counted as a gap"
}
if ($vanillaTypes.Count -gt 0) {
    Write-Output "stay vanilla (VANILLA_TYPES) - not in the pool: $($vanillaTypes -join ', ')"
}
if ($requiredInjections.Count -gt 0) {
    Write-Output "required injections (REQUIRED_INJECTIONS) - missing in vanilla:"
    foreach($ri in $requiredInjections){
        $have = @($d.items | Where-Object { [int]$_.typeId -eq [int]$ri.typeId }).Count
        Write-Output ("  typeId {0,4} -> room {1,4} ({2}; present in the level: {3})" -f $ri.typeId,$ri.room,$ri.reason,$have)
    }
}
if ($fixedItems.Count -gt 0) {
    Write-Output "untouchable items (FIXED_ITEMS) - must not be moved:"
    foreach($fi in $fixedItems){
        $inst = @($d.items | Where-Object { [int]$_.index -eq [int]$fi.item })
        $where = if($inst.Count){ "room $($inst[0].room), typeId $($inst[0].typeId)" } else { "not found in the level" }
        Write-Output ("  item {0,4} - {1} ({2})" -f $fi.item,$fi.reason,$where)
    }
}
if ($cameraRooms.Count -gt 0) {
    $camSec = @($d.sectors | Where-Object { -not $_.isWall -and $cameraRooms -contains [int]$_.room })
    "  (additionally {0} sectors in {1} pure scenery rooms, not counted)" -f $camSec.Count,$cameraRooms.Count
}
Write-Output ""
Write-Output "--- Key/puzzle items: allowed placement range ---"
# A counting rule instead of a type rule: if a level needs two slots of the
# same type, it does not suffice that "some" item lies early enough. The
# k-th instance must lie no later than the zone in which the k-th slot is
# served. Gameflow items Lara brings along, they need no placement and
# therefore do not belong in the counting rule - or it reports "0 instances
# present, CONTRADICTION" for something perfectly fine (rich3: Puzzle Hole1
# in room 119, but no Puzzle Item1 in the level).
$gameflowNeeds = @($itemNeed | Where-Object { $_.Gameflow })
if ($gameflowNeeds.Count -gt 0) {
    Write-Output "not present in the level, comes via gameflow (CMD_GIVEOBJ) - no placement needed:"
    foreach($g in ($gameflowNeeds | Group-Object Type)){
        "  typeId {0} - needed from zone {1} ({2})" -f $g.Name,(($g.Group | ForEach-Object { $_.Zone } | Measure-Object -Minimum).Minimum),$g.Group[0].Desc
    }
    Write-Output ""
}

# $byType was already computed above, before the ExportZonesJson block -
# here only the text output remains.
foreach($kv in $byType.GetEnumerator() | Sort-Object Name){
    $nt = [int]$kv.Key
    $needs = @($kv.Value | Sort-Object { $_.Zone })
    # Reusable keys need only ONE instance, no matter how many locks they
    # serve - otherwise the counting rule reports a false shortage.
    if ($reusableKeys -contains $nt) {
        "typeId {0}: reusable - 1 instance suffices for {1} lock(s)" -f $nt, $needs.Count
        $needs = @($needs | Select-Object -First 1)
    }
    else { "typeId {0}: {1} instance(s) needed" -f $nt, $needs.Count }
    for($j=0; $j -lt $needs.Count; $j++){
        "    instance {0} -> must lie in zone <= {1}  ({2})" -f ($j+1), $needs[$j].Zone, $needs[$j].Desc
    }
    $cur = @($d.items | Where-Object { [int]$_.typeId -eq $nt })
    $withZone = @($cur | ForEach-Object {
        $k = "$($_.room),$($_.sectorX),$($_.sectorZ)"
        $z = if($zoneOf.ContainsKey($k)){ [int]$zoneOf[$k] } else { 9999 }
        @{ It=$_; Z=$z }
    } | Sort-Object { $_.Z })
    "    vanilla: {0} instance(s) present" -f $withZone.Count
    for($j=0; $j -lt $withZone.Count; $j++){
        $zt = if($withZone[$j].Z -eq 9999){ "never reached" } else { $withZone[$j].Z }
        $ok = if ($j -ge $needs.Count) { "spare" }
              elseif ($withZone[$j].Z -le $needs[$j].Zone) { "OK" }
              else { "CONTRADICTION" }
        "      item {0,3} in room {1,4} -> zone {2,-12} [{3}]" -f $withZone[$j].It.index,$withZone[$j].It.room,$zt,$ok
    }
    if ($withZone.Count -lt $needs.Count) { "      [CONTRADICTION] too few instances in the level" }
}

Write-Output ""
Write-Output "--- Why did the flood not get further? ---"
# Without this diagnosis, "0 contradictions" is worthless: a level in which
# the flood stops immediately derives no key-item requirement at all and
# therefore wrongly reports "all fine". Here stands which door at the edge
# of the reached area is shut and what it hangs on - and if no door stands
# there at all, the cause is geometry (an unmodeled transition), not logic.
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
    "door item {0,3} (typeId {1}, room {2,4}) mask {3,2}/31" -f $di,$it.typeId,$it.room,$have
    $trigs = @($allTrig | Where-Object { $_.T.commands | Where-Object { $_.type -eq 'Object' -and [int]$_.index -eq $di } })
    if ($trigs.Count -eq 0) { "    no trigger in the level switches this door"; continue }
    foreach($e in $trigs){
        $st = if ($firedTrig.Contains($e.Id)) { 'fired' }
              elseif (-not (Test-ReqSector $e.Req $reached)) { 'trigger sector not reachable' }
              else { 'item missing' }
        "    mask {0,2}  {1,-32} -> {2}" -f $e.T.mask, $e.Req.Desc, $st
    }
}
if ($frontier -eq 0) {
    Write-Output "No closed door at the edge - the flood stops at the geometry,"
    Write-Output "not at a blocker (jump/climb/teleport/water not modeled)."
}

Write-Output ""
Write-Output "--- Largest unreached areas ---"
# Group unreached sectors into components and per component look for the
# spatially closest reached sector. That puts into the report WHERE to look
# in trview, instead of only THAT something is missing. Small distance and
# small height difference = presumably a jump or a climbing spot.
# Alternate rooms stay out: they are never independently enterable, they
# swap out their base room. Reporting them as their own "unreached area"
# only produces noise - two alternates of different flip groups can lie
# spatially exactly on top of each other (joby5 rooms 52 and 86, distance
# 0, height diff 0) and then look like a missing connection, although in
# game they never exist at the same time. The right question is always how
# Lara reaches the BASE room.
$unreach = @{}
foreach($s in $d.sectors){
    if ($s.isWall -or $s.alternateRoom) { continue }
    if ($cameraRooms -contains [int]$s.room) { continue }   # pure scenery, see CAMERA_ROOMS
    if ($excludeRooms -contains [int]$s.room) { continue }  # hand-barred, see EXCLUDE_ROOMS
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
# NO world distance as a hint. TR rooms overlap arbitrarily in world
# coordinates; only the portals connect them, and from that a different
# in-game geometry arises (Area 51 in TR3: the UFO is geometrically tiny,
# in game huge). Two sectors at the same world coordinate can be completely
# unconnected in game - "closest connection by distance" was therefore
# misleading.
# Since portal edges are by now two-way, the following holds: if an area
# has any portal into the reached part at all, it is reached too. If it
# remains, the way there runs over something that does not stand in the
# sector data.
# An area in which only animating objects stand and not a single pickup,
# enemy, switch or door part is with high probability pure camera-flyby
# geometry and not meant to be entered at all. Andy2 (the labyrinth) is the
# documented case: the actual level is small, only seems large through the
# movable bridges, and the additional mill rooms exist only for the flyby -
# a rotatable mill would not be walkable as an animating, so it got its own
# room. Such areas are NOT a gap.
$ANIM_LO = 416; $ANIM_HI = 447
# Already clarified gaps. Without this, the report announces the same rooms
# as an open question on every run, although the answer has long been there
# - and the answer then stands only in the chat history instead of in the
# tool. Room -> reason.
$KNOWN_GAPS = @{
    # joby3: four niches for the ventilators. All four identically built -
    # 3x4 room, exactly two walkable sectors, not a single portal; in room
    # 66 an Electrical Light visibly sits. Confirmed by the user in trview.
    # joby4: room 0 lies outside the map, a leftover of the devs without
    # function - looked at by the user in trview. One of the level's three
    # non-water rooms, floorY -1024 to 256 and thereby far above the
    # swimming limit. It announced itself as several one-sector gaps.
    # Per the user, 0 and 1 are the room below and above of a stack - both
    # outside the map. That matches the data: same dimensions 6x15, the
    # same 52 non-wall sectors, zero spots, no items, neither a water room,
    # and room 0 has its only portal into room 1.
    'andy1' = @{
        49  = 'not connected'
        220 = 'not connected'
    }
    'joby4' = @{
        0 = 'out of map, leftover of the devs, without function (room below)'
        1 = 'out of map, leftover of the devs, without function (room above)'
    }
    # joby5, all four gaps reviewed by the user in trview. Room 104 is the
    # one with the Keyhole8 whose door 103 carries 11111 anyway and stands
    # open from level start - the lock is moot.
    'joby5' = @{
        11  = 'ventilator room'
        22  = 'out of map, without meaning'
        23  = 'out of map, without meaning'
        87  = 'scenery'
        95  = 'scenery'
        104 = 'scenery (contains the moot Keyhole8 for door 103)'
        105 = 'ventilator room'
        166 = 'scenery'
        167 = 'scenery'
    }
    # Andy2. The four large gaps are four almost identically built mill
    # modules - 270/270/215/215 sectors, all on yBottom 12544, side by side
    # in the world, and in all four not a single playable object sits. The
    # user looked at them in trview: "Those are only flyby areas, no core
    # segment." The same holds for the area around room 15.
    #
    # Nothing has to be barred for that - in none of these rooms lies a
    # single reached placement surface. They stand here exclusively so the
    # report does not announce them as an unclarified gap on every run.
    'Andy2' = @{
        15  = 'out of map, flyby only'
        137 = 'mill module, flyby only'; 139 = 'mill module, flyby only'
        140 = 'mill module, flyby only'; 146 = 'mill module, flyby only'
        148 = 'mill module, flyby only'; 149 = 'mill module, flyby only'
        150 = 'mill module, flyby only'; 151 = 'mill module, flyby only'
        153 = 'mill module, flyby only'; 154 = 'mill module, flyby only'
        156 = 'mill module, flyby only'; 158 = 'mill module, flyby only'
        159 = 'mill module, flyby only'; 160 = 'mill module, flyby only'
        189 = 'mill module, flyby only'; 190 = 'mill module, flyby only'
        192 = 'mill module, flyby only'; 193 = 'mill module, flyby only'
        194 = 'mill module, flyby only'; 195 = 'mill module, flyby only'
        196 = 'mill module, flyby only'; 197 = 'mill module, flyby only'
        198 = 'mill module, flyby only'; 203 = 'mill module, flyby only'
        204 = 'mill module, flyby only'; 205 = 'mill module, flyby only'
        207 = 'mill module, flyby only'; 242 = 'mill module, flyby only'
        246 = 'mill module, flyby only'; 247 = 'mill module, flyby only'
    }
    # rich2, all four gaps explained by the user. 137 and 138 are two of
    # six one-sector niches behind kick doors (131/132/133 hang on room
    # 134, 136/137/138 on room 135) - Lara simply kicks them in, via room
    # 131 the level continues. Nothing to bar there: the single sector of
    # each niche carries the door itself and is thus no spot anyway.
    'rich2' = @{
        83  = 'flyby scenery'
        137 = 'niche behind a kick door, Lara kicks it in'
        138 = 'niche behind a kick door, Lara kicks it in'
        158 = 'out of map'
    }
    # rich1, all four gaps explained by the user. 98/99 is the flyby
    # scenery with the Twogun, 64 scenery as well (a scientist and three
    # animatings, without a single portal), 133-135 and 148/149 scenery.
    # Room 142 lies out of map and carries the trigger sector for door 186
    # - so the door can never open, which per the user is again an
    # antitrigger fix.
    'rich1' = @{
        64  = 'scenery, without a portal'
        98  = 'flyby scenery'
        99  = 'flyby scenery'
        133 = 'scenery'
        134 = 'scenery'
        135 = 'scenery'
        142 = 'out of map, antitrigger fix (carries the trigger for door 186)'
        148 = 'scenery'
        149 = 'scenery'
    }
    # andy3, all four gaps reviewed by the user in trview. In none of them
    # lies a playable object.
    'andy3' = @{
        148 = 'out of map'
        180 = 'flyby scenery'
        181 = 'out of map'
        189 = 'flyby scenery'
        190 = 'flyby scenery'
        191 = 'flyby scenery'
        # Room 102 deliberately does NOT stand here. It is core level and
        # is reached too - only a few of its sectors are not, and they
        # announce themselves together with the neighboring 103 as a
        # four-sector gap.
        103 = 'out of map'
    }
    'joby3' = @{
        63  = 'deco room for the ventilator'
        66  = 'deco room for the ventilator'
        111 = 'deco room for the ventilator'
        113 = 'deco room for the ventilator'
        # 180 only came to light after the 13 scenery rooms were barred.
        # Identically built to the four above - 3x4, two walkable sectors,
        # no portal - and like room 66 with an Electrical Light inside.
        # Concluded from the construction, not confirmed by the user.
        180 = 'deco room for the ventilator (concluded from the construction)'
    }
    'Andrea3' = @{
        13 = 'antitrigger fix of the devs, without meaning for us'
        71 = 'leftover of the devs, without function'
        72 = 'leftover of the devs, without function'
        77 = 'without function'
    }
    'joby2' = @{
        # The crane that swings over the submarine at the level's end is a
        # mesh in the actual level. These rooms exist only for the
        # animation - why that does not hang on the real crane, the user
        # does not know either, but nothing here is enterable.
        49 = 'crane animation for the level end, in the level itself a mesh'
        50 = 'crane animation for the level end, in the level itself a mesh'
        51 = 'crane animation for the level end, in the level itself a mesh'
        0  = 'only animatings, no playable geometry'
    }
    # rich3: three one-sector rooms without a portal, all three clarified.
    # 212 and 217 each contain a rollingball - that is its start niche,
    # from there it rolls off. Lara never goes there. Room 217 the user
    # additionally named as an antitrigger-fix room, together with room
    # 112.
    'rich3' = @{
        205 = 'only an animating, camera-flyby geometry'
        212 = 'start niche of a rollingball (item 222)'
        217 = 'start niche of a rollingball (item 224), at the same time an antitrigger fix'
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
    "area with {0,5} sectors in {1,3} rooms" -f $comp.Count,$compRooms.Count
    "   rooms: {0}" -f (($compRooms | Select-Object -First 20) -join ', ')
    if ($compRooms.Count -gt 20) { "           ... and {0} more" -f ($compRooms.Count-20) }
    if ($allKnown) {
        "   CLARIFIED - no open question:"
        foreach($rr in $compRooms){ "     room {0,4}: {1}" -f $rr,$knownGaps[$rr] }
    }
    elseif ($itemsIn.Count -gt 0 -and $meaningful.Count -eq 0) {
        "   Contains {0} objects, NONE of them playable - only animatings." -f $itemsIn.Count
        "   => presumably pure camera-flyby geometry, no real gap."
    } else {
        "   No portal connects this area with the reached part."
        "   So the access is a jump, a climbing spot, monkeyswing, a dive,"
        "   a teleporter or a cutscene - please clarify in game or trview."
        if ($meaningful.Count -gt 0) {
            "   playable objects inside: {0}" -f (($meaningful | Group-Object typeId | ForEach-Object { "$($_.Name)x$($_.Count)" }) -join ', ')
        }
    }
}

Write-Output ""
Write-Output "--- Equipment gates (manual, not derivable from level data) ---"
# The table stands further up with the other curation data, because by now
# it is not only reported but enforced - see the evaluation before the
# constraint build.
if (-not $EQUIP_GATE.ContainsKey($LevelName)) {
    Write-Output "None on record."
} else {
    foreach($g in $EQUIP_GATE[$LevelName].GetEnumerator()){
        $swItem = [int]$g.Key
        # In which zone is this switch needed?
        $z = ($log | Where-Object { $_.Req.Desc -like "*Item $swItem *" } | ForEach-Object { $_.Zone } | Sort-Object | Select-Object -First 1)
        if ($null -eq $z) { $z = '?' }
        "switch item {0}: {1}" -f $swItem, $g.Value.Desc
        "  needed in zone {0} -> equipment must lie in zone <= {0}" -f $z
        foreach($nt in $g.Value.Need){
            $inst = @($d.items | Where-Object { [int]$_.typeId -eq [int]$nt })
            foreach($c in $inst){
                $k = "$($c.room),$($c.sectorX),$($c.sectorZ)"
                $iz = if($zoneOf.ContainsKey($k)){ $zoneOf[$k] } else { 'never reached' }
                $ok = if($zoneOf.ContainsKey($k) -and $z -ne '?' -and $zoneOf[$k] -le $z){ 'OK' } else { 'CHECK' }
                "    typeId {0,4} item {1,3} in room {2,4} -> zone {3,-12} [{4}]" -f $nt,$c.index,$c.room,$iz,$ok
            }
        }
    }
}

Write-Output ""
Write-Output "--- Self-test: level completable? ---"
# Andrea1 has no EndLevel command. The level ends via a cutscene trigger:
# after the cutscene the gameflow starts the next level. The cutscene
# trigger is thus the terminus of the zone chain, EndLevel only the special
# case for levels that actually use it.
$endTrig = @($d.triggers | Where-Object { $_.commands | Where-Object { $_.type -in @('EndLevel','Cutscene') } })
if ($endTrig.Count -eq 0) {
    Write-Output "Neither an EndLevel nor a Cutscene trigger found - terminus unknown."
} else {
    foreach($t in $endTrig){
        $k = "$($t.room),$($t.sectorX),$($t.sectorZ)"
        $z = if($zoneOf.ContainsKey($k)){ "zone $($zoneOf[$k])" } else { "NOT REACHABLE" }
        $kind = ($t.commands | Where-Object { $_.type -in @('EndLevel','Cutscene') } | Select-Object -First 1)
        "{0} {1} in room {2,4} sector ({3},{4}) -> {5}" -f $kind.type,$kind.index,$t.room,$t.sectorX,$t.sectorZ,$z
    }
}
