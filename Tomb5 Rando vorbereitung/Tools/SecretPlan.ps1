# Reads the secret lists from Kuration\Secrets and turns them into a
# machine-readable plan (#53).
#
# ONE FILE = ONE LIST. The file name carries author and category:
#   ChrisChanSan_Glitchless.txt  ->  dropdown "Glitchless", display "ChrisChanSan"
#   ChrisChanSan_Glitched.txt    ->  dropdown "Glitched",   display "ChrisChanSan"
# Files with a leading underscore appear in no dropdown (references).
#
# Whoever wants to contribute their own list puts another file there -
# nothing in the program needs changing.
#
# Two modes need no list at all and therefore do not stand in this folder:
# "Vanilla" (secrets stay put) and "Pool" (they travel like ordinary
# pickups, can turn up anywhere).
#
# The lists are maintained by hand and shall stay that way - readable in
# trview, changeable without a tool. This parser adapts to them.
#
# Usage:
#   SecretPlan.ps1 -List                        show all available lists
#   SecretPlan.ps1 -Author X -Category Y        print this list's plan
#   ... additionally -LevelName A -AsJson path
param(
    [string]$Folder = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Rando vorbereitung\Kuration\Secrets",
    [switch]$List,
    [string]$Author = "",
    [string]$Category = "",
    [string]$LevelName = "",
    [string]$AsJson = "",
    # Cross-check against the real level data: do the Level_IDs really match
    # the secrets (type 223) of the vanilla file? People mistype, and a
    # wrong index silently moves the wrong item.
    [string]$VanillaDir = "C:\Cloude AI\Tomb5 Native win 11 fix\Tomb5 Steam Version\data\Vanilla",
    [string]$ItemPruef = "C:\Cloude AI\Alte sachen\Two gun test\Bittenichtbitdefender\ItemPruef.exe",
    [string]$AsPatch = "",
    [string]$Trc = "",
    # Level info JSON: delivers the room origins for room+sector -> world.
    [string]$LevelInfo = ""
)

function Split-AtDepth0([string]$text, [char]$separator) {
    $parts = @(); $depth = 0; $cur = ""
    foreach ($c in $text.ToCharArray()) {
        if ($c -eq '(') { $depth++ }
        elseif ($c -eq ')') { $depth-- }
        if ($c -eq $separator -and $depth -eq 0) { $parts += $cur; $cur = "" }
        else { $cur += $c }
    }
    if ($cur.Trim().Length -gt 0) { $parts += $cur }
    return @($parts | ForEach-Object { $_.Trim() } | Where-Object { $_.Length -gt 0 })
}

# Angles: the list is written in DEGREES (@Angle90 = a quarter turn), the
# patch wants the raw TR unit (yRot, int16: 65536 = a full turn). Without
# the conversion, 90 degrees became half an arc minute - invisible in the
# level, because the item simply stood unrotated.
function ConvertTo-RawAngle([int]$deg) {
    $raw = [int][math]::Round($deg * 65536.0 / 360.0) % 65536
    if ($raw -gt 32767) { $raw -= 65536 }
    if ($raw -lt -32768) { $raw += 65536 }
    return $raw
}

# Parse one extra option. There are three forms, distinguished by 'kind':
#
#   'item'    add Type_ID(N)@Room R(xA,zB)          - append an item
#             optionally with @OCB(n), @Angle(w) and a trigger on it
#   'trigger' add Trigger_Type(Kind)@Room R(xA,zB)  - ONLY a trigger, no item
#   'replace' replace Item_Level_ID(N)with->Type_ID(T) - retype an existing item
#             optionally with @OCB(n), @Angle(w)
#
# Everything the parser does not recognize gets reported - a mistyped
# "@Angel0" shall not vanish silently.
function Read-Option([string]$o) {
    $o = $o.Trim()
    $e = $null

    # --- Form 3: retype an existing item ----------------------------------
    if ($o -match 'replace\s+Item_Level_ID\(\s*(\d+)\s*\)\s*with\s*->\s*Type_?ID\(\s*(\d+)\s*\)') {
        $e = [PSCustomObject]@{
            kind = 'replace'; index = [int]$Matches[1]; typeId = [int]$Matches[2]
            room = $null; sectorX = $null; sectorZ = $null
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
        # Optionally with a move: @Room relocates the item (sector center,
        # floor computed, @Height on top). Before, an @Room here was
        # swallowed silently - the joby5 trapdoor, noticed by the user.
        if ($o -match '@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') {
            $e.room = [int]$Matches[1]; $e.sectorX = [int]$Matches[2]; $e.sectorZ = [int]$Matches[3]
        }
    }
    # --- Form 1: append an item -------------------------------------------
    elseif ($o -match 'add\s+Type_ID\(\s*(\d+)\s*\)' ) {
        $typ = [int]$Matches[1]
        # The spot comes either from room+sector or from a reference item
        # (@LikeItem). One of the two must be there.
        if ($o -match '@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') {
            $rm = [int]$Matches[1]; $sx = [int]$Matches[2]; $sz = [int]$Matches[3]
        } elseif ($o -match '@\s*LikeItem\(\s*\d+\s*\)') {
            $rm = $null; $sx = $null; $sz = $null
        } else { return $null }
        $e = [PSCustomObject]@{
            kind = 'item'; index = $null; typeId = $typ
            room = $rm; sectorX = $sx; sectorZ = $sz
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
    }
    # --- Form 2: only a trigger, without an item --------------------------
    elseif ($o -match 'add\s+Trigger_Type\(') {
        if ($o -notmatch '@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') { return $null }
        $e = [PSCustomObject]@{
            kind = 'trigger'; index = $null; typeId = $null
            room = [int]$Matches[1]; sectorX = [int]$Matches[2]; sectorZ = [int]$Matches[3]
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
    }
    # --- Form 6: make a room a water room / drain it ---------------------
    # Make_Room_Water(202) resp. Make_Room_Nowater(202). A real
    # intervention: ROOM_UNDERWATER hangs on 44 engine spots (swimming,
    # camera, sound, AI paths, fire), not only on the looks.
    elseif ($o -match 'Make_Room_(No)?Water\s*\(\s*(\d+)\s*\)') {
        $e = [PSCustomObject]@{
            kind = 'water'; index = $null; typeId = $null
            room = [int]$Matches[2]; sectorX = $null; sectorZ = $null
            roomA = $null; sxA = $null; szA = $null; roomB = $null; sxB = $null; szB = $null
            waterOn = [string]::IsNullOrEmpty($Matches[1])
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
    }
    # --- Form 5: climbing wall -------------------------------------------
    # Make_Climb@Room112(x1,z3)@Angle90 - the sector becomes climbable, the
    # angle is Lara's LOOK at the wall (the wall-grab convention).
    # Conversion into the engine bits is done by the patch output.
    elseif ($o -match 'Make_Climb\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') {
        $e = [PSCustomObject]@{
            kind = 'climb'; index = $null; typeId = $null
            room = [int]$Matches[1]; sectorX = [int]$Matches[2]; sectorZ = [int]$Matches[3]
            roomA = $null; sxA = $null; szA = $null; roomB = $null; sxB = $null; szB = $null
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
    }
    # --- Form 4: open a portal on both sides ------------------------------
    # Make_Portal@Room34(x0,z5)->Room36(x4,z5) - the "->" is pairing, not
    # direction: ONE line turns both sectors into portals onto each other.
    # The view windows must already exist in the level (a precondition the
    # patch path checks).
    elseif ($o -match 'Make_Portal\s*@\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)\s*->\s*Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)') {
        $e = [PSCustomObject]@{
            kind = 'portal'
            roomA = [int]$Matches[1]; sxA = [int]$Matches[2]; szA = [int]$Matches[3]
            roomB = [int]$Matches[4]; sxB = [int]$Matches[5]; szB = [int]$Matches[6]
            room = $null; sectorX = $null; sectorZ = $null; index = $null; typeId = $null
            ocb = $null; angle = $null; height = $null; xOff = $null; zOff = $null; likeItem = $null; triggerKind = $null; triggerTargets = @()
        }
    }
    else { return $null }

    if ($o -match '@\s*OCB\(\s*(\d+)\s*\)')       { $e.ocb   = [int]$Matches[1] }
    if ($o -match '@\s*Angle\s*\(?\s*(-?\d+)')    { $e.angle = [int]$Matches[1] }
    if ($o -match '@\s*Height\(\s*(-?\d+)\s*\)') { $e.height = [int]$Matches[1] }
    if ($o -match '@\s*XOff\(\s*(-?\d+)\s*\)')      { $e.xOff    = [int]$Matches[1] }
    if ($o -match '@\s*ZOff\(\s*(-?\d+)\s*\)')      { $e.zOff    = [int]$Matches[1] }
    # Take over the position of an existing item. Needed for stacking:
    # yAuto computes the REAL floor position at the sector center, and on a
    # slightly tilted sector that lies up to 128 units away from the flat
    # base value Core put its item on. For a double door the upper part
    # must sit exactly above the lower one, not above the floor - so the
    # reference item's position is copied and only @Height added on top.
    if ($o -match '@\s*LikeItem\(\s*(\d+)\s*\)') { $e.likeItem = [int]$Matches[1] }
    if ($o -match 'Trigger_Type\(\s*(\w+)\s*\).*?Item_Level_ID\(\s*([^)]*)\)') {
        $e.triggerKind = [string]$Matches[1]
        # Several targets with commas: "Item_Level_ID(13, Neu1)". An entry
        # is either an existing item index or "Neu<k>" - the k-th item that
        # this level's Extra_Options themselves create. Without that, an
        # item just created could not be addressed, for only the patch run
        # knows its index (double door: bottom retyped, top new, one switch
        # for both).
        foreach ($part in ([string]$Matches[2] -split ',')) {
            $t = $part.Trim()
            if ($t -match '^\d+$')            { $e.triggerTargets += [int]$t }
            elseif ($t -match '^Neu\s*(\d+)$'){ $e.triggerTargets += "Neu$($Matches[1])" }
            elseif ($t.Length -gt 0)          { $e.triggerTargets += "?$t" }
        }
    }

    # At 'replace', Item_Level_ID refers to the item ITSELF, not to a
    # trigger target - otherwise the line would enter itself as a target.
    if ($e.kind -eq 'replace') { $e.triggerKind = $null; $e.triggerTargets = @() }

    # A trigger without a target is pointless and would otherwise silently
    # fall onto kind 0.
    if ($e.kind -eq 'trigger' -and $e.triggerTargets.Count -eq 0) { return $null }

    # Strictness instead of leniency: what the parser did NOT recognize
    # gets reported.
    $rest = $o
    $rest = $rest -replace 'Make_Portal\s*@\s*Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)\s*->\s*Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)', ''
    $rest = $rest -replace 'Make_Climb\s*@\s*Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)', ''
    $rest = $rest -replace 'Make_Room_(No)?Water\s*\(\s*\d+\s*\)', ''
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
    if ($rest.Length -gt 0) { $e | Add-Member -NotePropertyName unknown -NotePropertyValue $rest }
    return $e
}

function Read-List([string]$path) {
    $level = @(); $warn = @()
    foreach ($z in (Get-Content $path)) {
        if ($z -match '^\s*#') { continue }
        if ($z -notmatch '^\s*(\w+)\.trc\s+(\d+)\s+(\d+)\s+\(([^)]*)\)\s*(.*)$') { continue }
        $lvl = $Matches[1]; $count = [int]$Matches[2]; $typ = [int]$Matches[3]
        $ids = @($Matches[4] -split ',' | ForEach-Object { [int]$_.Trim() })
        $rest = $Matches[5]

        # The spots stand BEFORE the Extra_Options; the options themselves
        # name rooms and would otherwise be counted as additional secrets.
        #
        # The boundary used to be the literal "(add" - and thus blind to
        # every group that starts differently. A line with "(replace ...)"
        # dropped out SILENTLY: no option read, no warning. Exactly what
        # the head of this file rules out.
        #
        # Now without a word list: the spots consist of "Room<N>(x..,z..)",
        # and their parentheses are masked out. The first parenthesis still
        # left after that starts the Extra_Options - whatever the verb.
        $mask = $rest.ToCharArray()
        foreach ($m in [regex]::Matches($rest, 'Room\d+\(\s*x\d+\s*,\s*z\d+\s*\)')) {
            for ($i = $m.Index; $i -lt $m.Index + $m.Length; $i++) { $mask[$i] = ' ' }
        }
        $pExtra = (-join $mask).IndexOf('(')
        $front = if ($pExtra -ge 0) { $rest.Substring(0, $pExtra) } else { $rest }
        $spots = @()
        foreach ($m in [regex]::Matches($front, 'Room(\d+)\(\s*x(\d+)\s*,\s*z(\d+)\s*\)')) {
            $spots += [PSCustomObject]@{ room=[int]$m.Groups[1].Value; sectorX=[int]$m.Groups[2].Value; sectorZ=[int]$m.Groups[3].Value }
        }

        # Split groups. Recommended is ';' - unambiguous because it occurs
        # nowhere else. The original '+' stays allowed (the existing lists
        # use it) but is split at parenthesis depth 0, because '+' also
        # stands INSIDE a group (before Trigger_Type).
        $extras = @()
        if ($pExtra -ge 0) {
            $raw = $rest.Substring($pExtra)
            $groups = if ($raw.Contains(';')) { Split-AtDepth0 $raw ';' } else { Split-AtDepth0 $raw '+' }
            foreach ($g in $groups) {
                $o = Read-Option $g
                if ($null -eq $o) { $warn += "${lvl}: option not readable -> $g"; continue }
                if ($o.PSObject.Properties.Name -contains 'unknown') {
                    $warn += "${lvl}: part of an option not understood -> '$($o.unknown)'"
                }
                $extras += $o
            }
        }

        if ($ids.Count -ne $count) { $warn += "${lvl}: count $count, but $($ids.Count) Level_ID" }

        # Cross-check against the real vanilla file: are these really the
        # secrets? A mistyped index otherwise silently moves an entirely
        # different item, and that only shows in game.
        $trc = Join-Path $VanillaDir "$lvl.trc"
        if ((Test-Path $trc) -and (Test-Path $ItemPruef)) {
            $real = @(& $ItemPruef $trc $typ 2>$null | ForEach-Object {
                if ($_ -match "^\s*(\d+)\s+$typ\s") { [int]$Matches[1] } })
            if ($real.Count -gt 0) {
                $missing = @($ids | Where-Object { $real -notcontains $_ })
                if ($missing.Count -gt 0) { $warn += "${lvl}: item $($missing -join ',') is not type $typ in the file" }
                if ($real.Count -ne $ids.Count) { $warn += "${lvl}: the file has $($real.Count) of type $typ, the list names $($ids.Count)" }
            }
        }
        if ($spots.Count -gt 0 -and $spots.Count -ne $ids.Count) {
            $warn += "${lvl}: $($ids.Count) secrets, but $($spots.Count) new spots"
        }

        $level += [PSCustomObject]@{ level=$lvl; count=$count; typeId=$typ; ids=$ids; spots=$spots; extras=$extras }
    }
    return [PSCustomObject]@{ level = $level; warnings = $warn }
}

if (-not (Test-Path $Folder)) { Write-Output "folder not found: $Folder"; exit 1 }

# Collect the available lists
$found = @()
foreach ($f in (Get-ChildItem $Folder -Filter *.txt | Sort-Object Name)) {
    if ($f.Name.StartsWith('_')) { continue }
    if ($f.BaseName -notmatch '^(.+)_(Glitchless|Glitched)$') {
        Write-Output "  skipped (name does not match Author_Category): $($f.Name)"
        continue
    }
    $found += [PSCustomObject]@{ author = $Matches[1]; category = $Matches[2]; file = $f.FullName }
}

if ($List -or ($Author -eq "" -and $Category -eq "")) {
    Write-Output "Available secret lists in $Folder :"
    foreach ($k in @('Glitchless','Glitched')) {
        $inCat = @($found | Where-Object { $_.category -eq $k })
        Write-Output "  dropdown ${k}: $(if($inCat.Count){ ($inCat.author) -join ', ' } else { '(none)' })"
    }
    Write-Output "  Always available without a list: Vanilla, Pool"
    if (-not $List) { exit 0 }
    exit 0
}

$hits = @($found | Where-Object { ($Author -eq "" -or $_.author -eq $Author) -and ($Category -eq "" -or $_.category -eq $Category) })
if ($hits.Count -eq 0) { Write-Output "no list for author '$Author' / category '$Category'"; exit 1 }

$all = @()
foreach ($t in $hits) {
    $p = Read-List $t.file
    foreach ($w in $p.warnings) { Write-Output "  WARNING $($t.author)_$($t.category): $w" }
    Write-Output "=== $($t.author) / $($t.category) ==="
    foreach ($l in $p.level) {
        if ($LevelName -ne "" -and $l.level -ne $LevelName) { continue }
        $target = if ($l.spots.Count -gt 0) {
            (0..($l.ids.Count-1) | ForEach-Object { "$($l.ids[$_])->R$($l.spots[$_].room)($($l.spots[$_].sectorX),$($l.spots[$_].sectorZ))" }) -join '  '
        } else { 'stays vanilla' }
        Write-Output ("  {0,-10} {1}" -f $l.level, $target)
        foreach ($e in $l.extras) {
            $a  = if ($null -ne $e.angle) { " angle $($e.angle)" } else { "" }
            $ob = if ($null -ne $e.ocb)   { " OCB $($e.ocb)" }      else { "" }
            $hh = if ($null -ne $e.height) { " $($e.height) above the floor" } else { "" }
            $zl = ($e.triggerTargets -join ', ')
            $tr = if ($null -ne $e.triggerKind) { "  + trigger $($e.triggerKind) -> item $zl" } else { "" }
            switch ($e.kind) {
                'item'    {
                    $wo = if ($null -ne $e.likeItem) { "like item $($e.likeItem)" }
                          else { "room $($e.room)($($e.sectorX),$($e.sectorZ))" }
                    Write-Output ("      plus: type {0} {1}{2}{3}{4}{5}" -f $e.typeId,$wo,$ob,$a,$hh,$tr)
                }
                'trigger' { Write-Output ("      plus: trigger {0} in room {1}({2},{3}) -> item {4}" -f $e.triggerKind,$e.room,$e.sectorX,$e.sectorZ,$zl) }
                'replace' {
                    $wo = if ($null -ne $e.room) { " -> room $($e.room)($($e.sectorX),$($e.sectorZ))" } else { "" }
                    Write-Output ("      plus: item {0} becomes type {1}{2}{3}{4}{5}" -f $e.index,$e.typeId,$wo,$ob,$a,$hh)
                }
                'portal'  { Write-Output ("      plus: portal room {0}({1},{2}) <-> room {3}({4},{5})" -f $e.roomA,$e.sxA,$e.szA,$e.roomB,$e.sxB,$e.szB) }
                'climb'   { Write-Output ("      plus: climbing wall room {0}({1},{2}) angle {3}" -f $e.room,$e.sectorX,$e.sectorZ,$e.angle) }
                'water'   { Write-Output ("      plus: room {0} becomes {1}" -f $e.room, $(if ($e.waterOn) { "A WATER ROOM" } else { "drained" })) }
            }
        }
    }
    $all += [PSCustomObject]@{ author=$t.author; category=$t.category; level=$p.level }
}

if ($AsJson -ne "") {
    $all | ConvertTo-Json -Depth 8 -Compress | Out-File $AsJson -Encoding utf8
    Write-Output "JSON written: $AsJson"
}

#----------------------------------------------------------------------------
# Patch output (#53): from one level's plan, a patch JSON for BauWerk.
#
# Why as a patch AFTER the roll and not in the randomizer: the secret spots
# are hand-curated and thus seed-independent. A patch describes exactly
# that - it puts the secrets in their places, whatever the roll did with
# them before, and appends the Extra_Options.
#
# The y is NOT computed but left to the file with "yAuto": there sits the
# ported floor math of the engine, also for slopes.
#
# The trigger of an extra option needs the item index of the object it is
# bound to (keyhole). That is a NEWLY appended item, so its index is only
# fixed after appending: item count of the target file plus position in
# the list of additions.
if ($AsPatch -ne "") {
    if ($LevelName -eq "" -or $Trc -eq "") { Write-Output "AsPatch needs -LevelName and -Trc"; exit 1 }
    if (-not (Test-Path $Trc)) { Write-Output "target file not found: $Trc"; exit 1 }

    $planLevel = $null
    foreach ($a in $all) { foreach ($l in $a.level) { if ($l.level -eq $LevelName) { $planLevel = $l } } }
    if ($null -eq $planLevel) { Write-Output "no plan entry for $LevelName"; exit 1 }

    # Room origins from the level info JSON
    $info = Get-Content $LevelInfo -Raw | ConvertFrom-Json
    $ro = @{}; foreach ($r in $info.rooms) { $ro[[int]$r.index] = $r }

    # Item count of the target file - the basis for the new items' indices
    $head = & $ItemPruef $Trc 2>$null | Select-Object -First 1
    if ($head -notmatch ':\s*(\d+)\s+Items') { Write-Output "item count not readable from $Trc"; exit 1 }
    $itemBase = [int]$Matches[1]

    $edits = @(); $adds = @(); $trigger = @(); $portals = @(); $climbs = @(); $water = @()

    for ($i = 0; $i -lt $planLevel.spots.Count; $i++) {
        $s = $planLevel.spots[$i]
        $rm = $ro[[int]$s.room]
        # Rule 3 here as well: a plinth/hideout OCB describes the OLD spot
        # and does not survive the move (Andrea3 secret 75: OCB 4 =
        # plinth, at the new place FindPlinth looks for a plinth that is
        # not there).
        $edits += [PSCustomObject]@{
            index = [int]$planLevel.ids[$i]; room = [int]$s.room
            x = ([int]$rm.originX + [int]$s.sectorX * 1024 + 512)
            z = ([int]$rm.originZ + [int]$s.sectorZ * 1024 + 512)
            y = 0; yAuto = $true; ocb = 0
        }
    }

    $n = 0

    # "Neu<k>" in a trigger target means the k-th item that this level's
    # Extra_Options themselves create. Only this run knows its index: the
    # new items append from $itemBase on, in the order they stand in the
    # line. Needed for a double door - the bottom retypes an existing item,
    # the top adds a new one, and ONE switch must drive both.
    $addCount = @($planLevel.extras | Where-Object { $_.kind -eq 'item' }).Count
    function Resolve-Target($z) {
        if ($z -is [int]) { return $z }
        $s = [string]$z
        if ($s -match '^Neu(\d+)$') {
            $k = [int]$Matches[1]
            if ($k -lt 1 -or $k -gt $addCount) {
                Write-Output "  WARNING: Neu$k does not exist - the line creates $addCount items."
                return $null
            }
            return $itemBase + $k - 1
        }
        Write-Output "  WARNING: trigger target '$s' not understood - skipped."
        return $null
    }
    function Get-TargetCommands($targets) {
        $c = @()
        foreach ($z in $targets) {
            $i = Resolve-Target $z
            if ($null -ne $i) { $c += [PSCustomObject]@{ type = 0; param = [int]$i } }
        }
        return @($c)
    }
    foreach ($e in $planLevel.extras) {



        if ($e.kind -eq 'water') {
            $water += [PSCustomObject]@{ room = [int]$e.room; on = [bool]$e.waterOn }
            continue
        }
        # Climbing wall: angle (degrees, Lara's look at the wall) into the
        # engine bits (control.cpp:2189): 0->0x100, 90->0x200, 180->0x400,
        # 270->0x800. Odd angles round to the nearest quarter.
        if ($e.kind -eq 'climb') {
            if ($null -eq $e.angle) { Write-Output "  WARNING: Make_Climb without @Angle - skipped."; continue }
            $deg = (($e.angle % 360) + 360) % 360
            $quad = [int][math]::Floor(($deg + 45) / 90) % 4
            $climbs += [PSCustomObject]@{
                room = [int]$e.room; sectorX = [int]$e.sectorX; sectorZ = [int]$e.sectorZ
                dirBits = [int][math]::Pow(2, $quad + 8)
            }
            continue
        }
        # Portal: both sides in one entry; the values go unchanged to
        # makePortal, which checks the preconditions (view windows!)
        # itself.
        if ($e.kind -eq 'portal') {
            $portals += [PSCustomObject]@{
                roomA = [int]$e.roomA; sxA = [int]$e.sxA; szA = [int]$e.szA
                roomB = [int]$e.roomB; sxB = [int]$e.sxB; szB = [int]$e.szB
            }
            continue
        }

        # Retype an existing item: no new item, so no new index either. The
        # position stays where it is - only the type and optionally angle
        # and OCB change. yAuto would be wrong here: the item shall not
        # jump onto the sector floor.
        if ($e.kind -eq 'replace') {
            $ed = [ordered]@{ index = [int]$e.index; typeId = [int]$e.typeId }
            # With @Room the item moves (sector center, floor computed).
            if ($null -ne $e.room) {
                $rm = $ro[[int]$e.room]
                $ed['room']  = [int]$e.room
                $ed['x']     = ([int]$rm.originX + [int]$e.sectorX * 1024 + 512)
                $ed['z']     = ([int]$rm.originZ + [int]$e.sectorZ * 1024 + 512)
                $ed['y']     = 0
                $ed['yAuto'] = $true
            }
            if ($null -ne $e.angle) { $ed['angle'] = ConvertTo-RawAngle ([int]$e.angle) }
            if ($null -ne $e.ocb)   { $ed['ocb']   = [int]$e.ocb }
            if ($null -ne $e.height) { $ed['yOff']  = -[int]$e.height; $ed['yAuto'] = $true }
            if ($null -ne $e.xOff)  { $ed['xOff']  =  [int]$e.xOff }
            if ($null -ne $e.zOff)  { $ed['zOff']  =  [int]$e.zOff }
            $edits += [PSCustomObject]$ed
            continue
        }

        # Trigger kinds of the engine: 0 = TRIGGER, 2 = SWITCH, 3 = KEY,
        # 4 = PICKUP. Only 2/3/4 carry a reference word.
        $kindNo = 0
        if ($null -ne $e.triggerKind) {
            $kindNo = switch ($e.triggerKind.ToLower()) {
                'key'     { 3 }
                'switch'  { 2 }
                'pickup'  { 4 }
                'trigger' { 0 }
                default   { -1 }
            }
            if ($kindNo -lt 0) {
                Write-Output "  WARNING: unknown trigger kind '$($e.triggerKind)' - treated as a plain trigger."
                $kindNo = 0
            }
        }

        # A pure trigger without an item: nothing appended, no index
        # consumed, no reference word.
        if ($e.kind -eq 'trigger') {
            $trigger += [PSCustomObject]@{
                room = [int]$e.room; sectorX = [int]$e.sectorX; sectorZ = [int]$e.sectorZ
                kind = $kindNo; timer = 0; oneShot = 1; mask = 31
                commands = (Get-TargetCommands $e.triggerTargets)
            }
            continue
        }

        # Append an item. With @LikeItem the spot is not fixed yet - then
        # it comes from the reference item in a moment, and room/sector
        # stay empty here.
        $entry = [ordered]@{ typeId = [int]$e.typeId }
        if ($null -ne $e.room) {
            $rm = $ro[[int]$e.room]
            $entry['room']  = [int]$e.room
            $entry['x']     = ([int]$rm.originX + [int]$e.sectorX * 1024 + 512)
            $entry['z']     = ([int]$rm.originZ + [int]$e.sectorZ * 1024 + 512)
            $entry['y']     = 0
            $entry['yAuto'] = $true
        }

        # @LikeItem: take over an existing item's position instead of
        # computing it from room and sector. Then NO yAuto - the item shall
        # sit exactly where the reference item sits, not on the computed
        # floor position (which on tilted sectors lies up to 128 units
        # away).
        if ($null -ne $e.likeItem) {
            $ref = $info.items | Where-Object { $_.index -eq [int]$e.likeItem }
            if ($null -eq $ref) {
                Write-Output "  WARNING: LikeItem($($e.likeItem)) - this item does not exist in the level JSON."
            } else {
                $entry['x'] = [int]$ref.x
                $entry['z'] = [int]$ref.z
                $entry['y'] = [int]$ref.y
                $entry['room'] = [int]$ref.room
                $entry['yAuto'] = $false
            }
        }
        if ($null -ne $e.angle) { $entry['angle'] = ConvertTo-RawAngle ([int]$e.angle) }
        if ($null -ne $e.ocb)   { $entry['ocb']   = [int]$e.ocb }
        if ($null -ne $e.height) { $entry['yOff']  = -[int]$e.height }
        if ($null -ne $e.xOff)  { $entry['xOff']  =  [int]$e.xOff }
        if ($null -ne $e.zOff)  { $entry['zOff']  =  [int]$e.zOff }
        $adds += [PSCustomObject]$entry
        $myIndex = $itemBase + $n
        $n++

        if ($null -ne $e.triggerKind) {
            $trigger += [PSCustomObject]@{
                room = [int]$e.room; sectorX = [int]$e.sectorX; sectorZ = [int]$e.sectorZ
                kind = $kindNo; timer = 0; oneShot = 1; mask = 31
                refItem = $myIndex
                commands = (Get-TargetCommands $e.triggerTargets)
            }
        }
    }

    $patch = [PSCustomObject]@{ itemEdits = $edits; itemAdds = $adds; triggerAdds = $trigger; portalAdds = $portals; climbAdds = $climbs; roomWater = $water; itemBase = $itemBase }
    $json = $patch | ConvertTo-Json -Depth 8 -Compress
    [System.IO.File]::WriteAllText($AsPatch, $json, (New-Object System.Text.UTF8Encoding($false)))
    $retyped = @($planLevel.extras | Where-Object { $_.kind -eq 'replace' }).Count
    Write-Output "patch written: $AsPatch  ($($planLevel.spots.Count) secrets moved, $retyped retyped, $($adds.Count) extra items, $($trigger.Count) triggers; new items from index $itemBase)"
}
