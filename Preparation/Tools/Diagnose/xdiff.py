# xdiff.py - compares two -log files of the engine seam by seam.
#
# Made for the differential run (Preparation/Verification.md, section 8):
# one run recorded with -record, played again with -replay on another build
# (or the same one), and the two log.txt files compared here. Python 3, no
# packages.
#
#   python xdiff.py A.log B.log [--float-eps E] [--keep-render]
#                   [--keep-layout] [--show N]
#   python xdiff.py --self-test
#
# Every line is put in a family by its first word (and its second, where a
# family writes lines of different kinds: "CAMLOG tick" is the logic's,
# "CAMLOG draw" a drawn frame's). The table below gives each family a class:
#
#   logic   written by the game logic, a fixed number per tick: the same
#           input gives the same lines. Compared; a difference is one.
#   load    written once per level load or session start, deterministic
#           but for the milliseconds, which are masked. Compared, reported
#           apart from the logic.
#   audio   SFXLOG's sound lines: whether a sound of the "wait till it has
#           finished" kind starts depends on the audio device's clock, which
#           no recording holds. Compared, reported apart.
#   render  written per drawn frame or by the HD renderer: depends on the
#           frame rate, the window and the card. Dropped, unless
#           --keep-render (two replays at the same frame-rate mode draw the
#           same frames, so they are comparable then).
#   timing  carries wall time or is gated by it, or describes the machine.
#           Dropped.
#   session the build stamp, the replay's own open/end lines, addresses of
#           released interfaces. Dropped.
#
# A log of a recorded or a replayed run is compared only where the replay
# holds the game: logic and audio lines from the first "REPLAYLOG level="
# line, load lines from the start, both up to the first "REPLAYLOG end"
# line - after it the devices drive the game again (the title a run returns
# to, a menu). Each family is then compared up to the shorter of the two
# runs' counts; the lines left out are counted in the summary.
#
# A family that is not in the table and looks like one (a word in capitals
# of three letters or more) is compared as "unclassified" and named in the
# summary; a line of free text (a word with small letters first) is dropped
# as "text".
#
# Before a comparison every line loses its "[   ms] " prefix, its addresses
# ("@ 1a2b3c4d", "base=0x...", "caller=PCtomb5+0x...", "at=0x...") and its
# wall times ("ms=", "*_ms=", "fps=", "budget=", ...). SAVELOG's "stored=",
# "side=", "items=+0x" and "slots=+0x" are distances in the pool of the
# build that wrote them - a 64-bit build lays the pool out differently - and
# are masked too, unless --keep-layout (for two runs of one build).
#
# The lines of each family are compared in order, the n-th with the n-th, so
# an extra line in one family does not shift the others. A pair whose words
# match and whose numbers differ only in floats (a number with a point) is
# "float-only", within --float-eps (default 0) "equal"; any integer that
# differs makes the pair a hard difference. The first hard difference prints
# the line in both logs, the logic's second before it (TICKLOG at=), the last
# tick counter a logic line wrote (f=, tick=, gc=), the last ITEMLOG item
# before it and the item= of the line itself.
#
# Exit code: 0 when no logic, load or unclassified line differs, 1 when one
# does, 2 for a usage error. REPLAYLOG desync lines are printed first: they
# say where a replay left the recorded run.

import re
import sys

# ---------------------------------------------------------------- the table

LOGIC, LOAD, AUDIO, RENDER, TIMING, SESSION = (
    'logic', 'load', 'audio', 'render', 'timing', 'session')

# key: "FAMILY" or "FAMILY sub" -> (class, reason)
TABLE = {
    'TICKLOG': (LOGIC, "the logic second's step counts; 30 and 60 fps alike"),
    'CAMLOG tick': (LOGIC, 'CalculateCamera, once per tick'),
    'CAMLOG sum': (RENDER, "the drawn eye's fix, per walk"),
    'CAMLOG draw': (RENDER, 'a drawn frame, alpha='),
    'LEDGELOG': (LOGIC, "Lara's ledge phases and the ledge camera"),
    'CHEATLOG': (LOGIC, 'a cheat taken'),
    'AILOG': (LOGIC, "GetAITarget's marker events, #346"),
    'ROPELOG': (LOGIC, 'AlignLaraToRope, per tick on a rope'),
    'ROPELOG draw': (RENDER, 'per drawn frame on a rope'),
    'BRAWLLOG': (LOGIC, 'the brawler, per tick'),
    'DRONELOG': (LOGIC, 'the drone, per tick'),
    'COGLOG': (LOGIC, 'the raising cog'),
    'ZIPLOG': (LOGIC, 'the zip call'),
    'ZIPSKIP': (LOGIC, 'the zip skip'),
    'HOLDLOG': (LOGIC, 'the scene hold'),
    'FLYLOG': (LOGIC, 'a flyby'),
    'FLYEND': (LOGIC, "a flyby's end"),
    'SLOTLOG': (LOGIC, 'EnableBaddieAI'),
    'HAIRLOG': (RENDER, 'the braid: from the drawn frame; no logic reads it'),
    'FPHEAD': (LOGIC, 'the first-person camera, from CalculateCamera'),
    'SAVELOG': (LOGIC, 'the save and the load'),
    'SKIPLATCH': (LOGIC, 'the skip latch in S_UpdateInput'),
    'MOUSELOG': (LOGIC, 'the mouse as S_UpdateInput took it'),
    'MOUSELOG capture=': (TIMING, "the window's capture"),
    'MODERNLOG': (LOGIC, 'the modern control, per tick'),
    'MODELOG': (LOGIC, 'the sprint and duck modes'),
    'COMBATLOG': (LOGIC, 'the combat seam'),
    'KEYLOG': (LOGIC, "the action keys' word"),
    'PADLOG': (LOGIC, "the pad's modifier"),
    'MENULOG': (LOGIC, 'menu steps, input-driven'),
    'ATANLOG': (LOGIC, "phd_atan's clamp, its caller masked"),
    'REPLAYLOG': (LOGIC, "the replay's level starts, saves and loads"),
    'REPLAYLOG open': (SESSION, 'differs between -record and -replay'),
    'REPLAYLOG end': (SESSION, 'differs between -record and -replay'),
    'REPLAYLOG refused': (SESSION, 'the file could not be read'),
    'REPLAYLOG warn': (SESSION, 'another frame-rate mode'),
    'REPLAYLOG desync': (SESSION, 'printed apart, first'),
    'REPLAYLOG drift': (SESSION, 'printed apart, first'),
    'HDLOG debris': (LOGIC, 'a debris break in the logic'),
    'SFXLOG': (AUDIO, 'a sound asked; the audio clock decides "once"'),
    'SFXLOG footsteps:': (LOAD, "the level's footstep sets"),
    'SFXLOG bridge:': (LOAD, "the level's imported sounds"),
    'ITEMLOG': (LOAD, 'LoadItems, one line per item'),
    'ITEMWATCH:': (LOAD, 'LoadItems, a corrupted item'),
    'OBJLOG': (LOAD, 'LoadObjects'),
    'CARRYLOG': (LOAD, 'GetCarriedItems at the level start'),
    'AILOG start': (LOAD, 'GetAIPickups at the level start, #346'),
    'STRINGS': (LOAD, 'LoadExtraStrings'),
    'SKINLOG': (LOAD, 'CreateSkinningData'),
    'SKINREACH': (LOAD, 'CreateSkinningData'),
    'WARMUP': (LOAD, 'WarmUpLevel, ms= masked'),
    'LOADBIND': (LOAD, 'the level load'),
    'MODLOG': (LOAD, 'the mods folder'),
    'FEET': (LOAD, "the footsteps' sets"),
    'GLOSSLOG': (LOAD, 'the HD build, field_ms= masked'),
    'SPRINGLOG': (LOAD, 'the HD build'),
    'SPRINGRULE': (LOAD, 'the HD build'),
    'SPRINGCRACK': (LOAD, 'the HD build'),
    'LOADLOG': (LOAD, 'the load steps, ms= masked'),
    'LIFELOG': (LOAD, "the level's life"),
    'LIFELOG second=': (RENDER, 'drawn frames per second'),
    'INTERPLOG': (RENDER, 'frames, tween frames, alpha'),
    'ARMLOG': (RENDER, 'per drawn frame'),
    'KEYFARLOG': (RENDER, 'blended frames only'),
    'KEYSEAMLOG': (RENDER, 'blended frames only'),
    'TURNLOG': (RENDER, 'the draw path'),
    'BRIDGELOG': (RENDER, 'the draw path'),
    'CLIPLOG': (RENDER, 'DrawAnimatingItem'),
    'CLIPDETAIL': (RENDER, 'aCheckMeshClip'),
    'MESHLOG': (RENDER, 'the draw path'),
    'SKIPLOG': (RENDER, 'PrintObjects'),
    'ROOMFLASH': (RENDER, 'DrawRooms, frame='),
    'ROOMPOP': (RENDER, 'DrawRooms, frame='),
    'ROOMLIST': (RENDER, 'DrawRooms, frame= and window events'),
    'ROOMWALK': (RENDER, 'counts drawn and tween frames'),
    'POISONLOG': (RENDER, 'per view build and drawn frame'),
    'INVLOG': (RENDER, "the ring's frames and alpha"),
    'CUTLOG': (RENDER, 'InterpBeginFrame, blended frames'),
    'LIPLOG': (RENDER, 'the HD head'),
    'SWAPLOG': (RENDER, 'the HD Lara'),
    'HIDELOG': (RENDER, 'the HD Lara'),
    'XRAYLOG': (RENDER, 'the HD Lara'),
    'LARALOG': (RENDER, 'the HD Lara, tween='),
    'WATERLOG': (RENDER, 'the HD water, built and drawn'),
    'SPLASHLOG': (RENDER, 'the HD water cards'),
    'ALTERFOV': (RENDER, 'every field of view set, tween frames too'),
    'HDLOG': (RENDER, 'the HD renderer: its build and its frames'),
    'LOADFRAME': (TIMING, 'one per loading-screen frame, paced by the clock'),
    'VISLOG': (TIMING, 'gated by GetTickCount, once a second'),
    'FPSLOG': (TIMING, 'wall-clock seconds and fps'),
    'PROFLOG': (TIMING, 'wall-clock seconds and phase times'),
    'DXLOG': (TIMING, 'the device and the window'),
    'WINLOG': (TIMING, 'the window and its focus'),
    'HANG': (TIMING, 'the hang watch'),
    'CRASH': (TIMING, 'an exception'),
    'POOL': (SESSION, "the pool's address"),
    'INSTALL': (SESSION, 'paths of this machine'),
    'WM_MOVE': (TIMING, 'a window message'),
    'WM_CREATE': (TIMING, 'a window message'),
    'WM_ACTIVE': (TIMING, 'a window message'),
    'WM_INACTIVE': (TIMING, 'a window message'),
    'WM_COMMAND': (TIMING, 'a window message'),
    'KA_ALTENTER': (TIMING, 'a key the window took'),
    'ACM': (TIMING, 'the audio codec'),
    'FMV': (SESSION, 'films: the 64-bit build has none'),
    'HD': (RENDER, 'the HD screen'),
    'HQ': (LOAD, 'the HQ sound folder'),
    'ERROR': (TIMING, 'a device call that failed'),
    'FAILED': (TIMING, 'a device call that failed'),
    'END': (SESSION, 'the end of the session'),
    'OUT': (TIMING, 'a device message'),
}

COMPARED = (LOGIC, LOAD, AUDIO, 'unclassified')

PREFIX = re.compile(r'^\[\s*\d+\] ')
FAMILY = re.compile(r'^[A-Z][A-Z0-9_]{1,}:?$')
SUB_WORD = re.compile(r'^[a-z][a-z0-9_]*:?$')
SUB_FIELD = re.compile(r'^([a-z][a-z0-9_]*=)')
HEXWORD = re.compile(r'^[0-9A-Fa-f]{6,16}$')

MASKS = [
    (re.compile(r'@ (?:0x)?[0-9A-Fa-f]{1,16}\b'), '@ <addr>'),
    (re.compile(r'\bbase=0x[0-9A-Fa-f]+'), 'base=<addr>'),
    (re.compile(r'\bcaller=PCtomb5\+0x[0-9A-Fa-f]+'), 'caller=<addr>'),
    (re.compile(r'\bat=0x[0-9A-Fa-f]+'), 'at=<addr>'),
    (re.compile(r'\bmodule=\S+'), 'module=<addr>'),
    (re.compile(r'\b((?:[a-z]+_)?ms|fps|real_fps|budget|wait|waited|'
                r'parked_ms|walk_ms)=-?[0-9.]+'), r'\1=<t>'),
    (re.compile(r'\bafter \d+ ms'), 'after <t> ms'),
]

LOAD_MASKS = {
    'LIFELOG': [(re.compile(r'\bbytes=\d+'), 'bytes=<layout>')],
    'LOADLOG': [(re.compile(r'\b(frames|shown)=\d+'), r'\1=<t>')],
}

LAYOUT_MASKS = [
    (re.compile(r'\b(stored|side)=0x[0-9A-Fa-f]+'), r'\1=<layout>'),
    (re.compile(r'\b(items|slots)=\+0x[0-9A-Fa-f]+'), r'\1=<layout>'),
]

NUMBER = re.compile(r'0x[0-9A-Fa-f]+|-?\d+\.\d+(?:[eE][-+]?\d+)?|-?\d+')


def family_of(text):
    """(key, family) of a line without its prefix."""
    words = text.split()

    if not words:
        return ('<text>', '<text>')

    first = words[0]

    # A room's door list printed as its address alone (alexstuff.cpp).
    if HEXWORD.match(first) and len(words) == 1 and re.search(r'\d', first):
        return ('<address>', '<address>')

    if not FAMILY.match(first):
        return ('<text>', '<text>')

    fam = first

    if len(words) > 1:
        second = words[1]

        if SUB_WORD.match(second) and (fam + ' ' + second) in TABLE:
            return (fam + ' ' + second, fam)

        m = SUB_FIELD.match(second)

        if m and (fam + ' ' + m.group(1)) in TABLE:
            return (fam + ' ' + m.group(1), fam)

        # LIFELOG's per-second line names its second after the level.
        for w in words[1:4]:
            m = SUB_FIELD.match(w)
            if m and (fam + ' ' + m.group(1)) in TABLE:
                return (fam + ' ' + m.group(1), fam)

    return (fam, fam)


def class_of(key, fam):
    if key in ('<text>',):
        return 'text'
    if key == '<address>':
        return SESSION
    if key in TABLE:
        return TABLE[key][0]
    if fam in TABLE:
        return TABLE[fam][0]
    return 'unclassified'


def mask(text, fam, keep_layout):
    for rx, to in MASKS:
        text = rx.sub(to, text)
    for rx, to in LOAD_MASKS.get(fam, []):
        if fam == 'LIFELOG' and keep_layout:
            continue
        text = rx.sub(to, text)
    if fam == 'SAVELOG' and not keep_layout:
        for rx, to in LAYOUT_MASKS:
            text = rx.sub(to, text)
    return text


def compare_pair(a, b, eps):
    """'equal', 'float' or 'hard'."""
    if a == b:
        return 'equal'

    if NUMBER.sub('#', a) != NUMBER.sub('#', b):
        return 'hard'

    na = NUMBER.findall(a)
    nb = NUMBER.findall(b)
    result = 'equal'

    for x, y in zip(na, nb):
        if x == y:
            continue
        if '.' in x and '.' in y and not x.startswith('0x'):
            if abs(float(x) - float(y)) <= eps:
                continue
            result = 'float'
            continue
        return 'hard'

    return result


class Log:
    def __init__(self, path, keep_layout):
        self.path = path
        self.lines = []      # (number, raw without prefix)
        self.streams = {}    # key -> [(number, masked, raw)]
        self.cls = {}        # key -> class
        self.desync = []
        self.tick_at = []    # (number, at)
        self.counter = []    # (number, value)
        self.items = []      # (number, item)
        self.start = 0       # the first REPLAYLOG level= line, or 0
        self.end = 0         # the first REPLAYLOG end line, or 0
        self.end_text = ''
        self.outside = 0     # lines left out by the window
        with open(path, 'r', encoding='latin-1', newline=None) as f:
            rows = [PREFIX.sub('', line.rstrip('\r\n')) for line in f]
        for n, raw in enumerate(rows, 1):
            if not self.start and raw.startswith('REPLAYLOG level='):
                self.start = n
            if not self.end and raw.startswith('REPLAYLOG end'):
                self.end = n
                self.end_text = raw
        for n, raw in enumerate(rows, 1):
            if True:
                key, fam = family_of(raw)
                c = class_of(key, fam)
                self.cls[key] = c
                if raw.startswith('REPLAYLOG desync') or \
                        raw.startswith('REPLAYLOG drift'):
                    self.desync.append((n, raw))
                if c in (LOGIC, AUDIO, LOAD, 'unclassified') and (
                        (self.end and n > self.end) or
                        (c != LOAD and self.start and n < self.start)):
                    self.outside += 1
                    continue
                if fam == 'TICKLOG':
                    m = re.search(r'\bat=(\d+)s', raw)
                    if m:
                        self.tick_at.append((n, int(m.group(1))))
                if fam == 'ITEMLOG':
                    m = re.match(r'ITEMLOG (\d+):', raw)
                    if m:
                        self.items.append((n, int(m.group(1))))
                if c == LOGIC:
                    m = re.search(r'\b(?:f|tick|gc)=(\d+)\b', raw)
                    if m:
                        self.counter.append((n, int(m.group(1))))
                self.streams.setdefault(key, []).append(
                    (n, mask(raw, fam, keep_layout), raw))

    @staticmethod
    def last_before(seq, n):
        found = None
        for k, v in seq:
            if k > n:
                break
            found = (k, v)
        return found


def run(a_path, b_path, eps, keep_render, keep_layout, show):
    A = Log(a_path, keep_layout)
    B = Log(b_path, keep_layout)

    for name, L in (('A', A), ('B', B)):
        for n, raw in L.desync:
            print('%s line %d: %s' % (name, n, raw))

    windowed = bool(A.end or B.end)

    keys = sorted(set(A.streams) | set(B.streams))
    compared = {}
    dropped = {}
    diffs = {}          # key -> [hard, float, missing]
    first = {}          # group -> (a_line, b_line, key, kind, a_raw, b_raw)
    floats_first = None

    for key in keys:
        c = A.cls.get(key) or B.cls.get(key)
        if c == RENDER and keep_render:
            c = LOGIC
        sa = A.streams.get(key, [])
        sb = B.streams.get(key, [])

        if c not in COMPARED:
            dropped[key] = (c, len(sa), len(sb))
            continue

        if windowed and len(sa) != len(sb):
            cut = min(len(sa), len(sb))
            A.outside += len(sa) - cut
            B.outside += len(sb) - cut
            sa = sa[:cut]
            sb = sb[:cut]

        compared[key] = (c, max(len(sa), len(sb)))
        d = [0, 0, 0]

        for i in range(max(len(sa), len(sb))):
            if i >= len(sa) or i >= len(sb):
                d[2] += 1
                kind = 'missing in ' + ('A' if i >= len(sa) else 'B')
                la = sa[i] if i < len(sa) else (None, '', '(none)')
                lb = sb[i] if i < len(sb) else (None, '', '(none)')
            else:
                la, lb = sa[i], sb[i]
                r = compare_pair(la[1], lb[1], eps)
                if r == 'equal':
                    continue
                if r == 'float':
                    d[1] += 1
                    if floats_first is None or la[0] < floats_first[0]:
                        floats_first = (la[0], lb[0], key, 'float', la[2],
                                        lb[2])
                    continue
                d[0] += 1
                kind = 'hard'

            group = 'logic' if c in (LOGIC, 'unclassified') else c
            at = la[0] if la[0] is not None else (lb[0] or 0)
            if group not in first or at < first[group][6]:
                first[group] = (la[0], lb[0], key, kind, la[2], lb[2], at)

        if any(d):
            diffs[key] = d

    def context(entry):
        a_line = entry[0] if entry[0] is not None else entry[6]
        t = Log.last_before(A.tick_at, a_line)
        g = Log.last_before(A.counter, a_line)
        it = Log.last_before(A.items, a_line)
        own = re.findall(r'\bitem=(\d+)', entry[4] + ' ' + entry[5])
        print('  tick:    %s' % ('TICKLOG at=%ds (A line %d)' % (t[1], t[0])
                                 if t else 'no TICKLOG before it'))
        print('  counter: %s' % ('%d (A line %d)' % (g[1], g[0])
                                 if g else 'no f=/tick= before it'))
        print('  item:    %s%s' % (
            'last ITEMLOG %d (A line %d)' % (it[1], it[0]) if it else
            'no ITEMLOG before it',
            ', the line names item %s' % '/'.join(sorted(set(own)))
            if own else ''))

    for group, title in (('logic', 'logic'), (LOAD, 'load'),
                         (AUDIO, 'audio')):
        if group not in first:
            print('\nno %s difference' % title)
            continue
        e = first[group]
        print('\nFIRST %s DIFFERENCE (%s, %s)' % (title.upper(), e[2], e[3]))
        print('  A line %s: %s' % (e[0], e[4]))
        print('  B line %s: %s' % (e[1], e[5]))
        context(e)

    if floats_first:
        e = floats_first
        print('\nfirst float-only difference (%s)' % e[2])
        print('  A line %d: %s' % (e[0], e[4]))
        print('  B line %d: %s' % (e[1], e[5]))
    else:
        print('\nno float-only difference')

    print('\ncompared families (lines, hard / float / missing):')
    for key in sorted(compared, key=lambda k: (compared[k][0], k)):
        c, n = compared[key]
        d = diffs.get(key, [0, 0, 0])
        flag = '' if not any(d) else '   <-- differs'
        print('  %-24s %-12s %7d   %d / %d / %d%s' % (key, c, n, d[0], d[1],
                                                     d[2], flag))
        if any(d) and show:
            shown = 0
            sa = A.streams.get(key, [])
            sb = B.streams.get(key, [])
            for i in range(max(len(sa), len(sb))):
                la = sa[i] if i < len(sa) else (None, '', '(none)')
                lb = sb[i] if i < len(sb) else (None, '', '(none)')
                if la[1] == lb[1] or compare_pair(la[1], lb[1], eps) == \
                        'equal':
                    continue
                print('      A %s: %s' % (la[0], la[2][:160]))
                print('      B %s: %s' % (lb[0], lb[2][:160]))
                shown += 1
                if shown >= show:
                    break

    print('\ndropped families (class, lines in A / B):')
    for key in sorted(dropped, key=lambda k: (dropped[k][0], k)):
        c, na, nb = dropped[key]
        reason = TABLE.get(key, TABLE.get(key.split(' ')[0], ('', '')))[1]
        print('  %-24s %-8s %7d / %-7d %s' % (key, c, na, nb, reason))

    hard = sum(d[0] + d[2] for k, d in diffs.items()
               if compared[k][0] in (LOGIC, LOAD, 'unclassified'))
    audio = sum(d[0] + d[2] for k, d in diffs.items()
                if compared[k][0] == AUDIO)
    floats = sum(d[1] for d in diffs.values())
    print('\n%d logic/load lines differ, %d audio, %d float-only; '
          '%d compared families, %d dropped' % (hard, audio, floats,
                                                 len(compared), len(dropped)))
    for name, L in (('A', A), ('B', B)):
        if L.start or L.end:
            print('%s: compared from line %d to %s; %d lines outside%s' % (
                name, L.start or 1, ('line %d (%s)' % (L.end, L.end_text))
                if L.end else 'its end', L.outside,
                ' - after the end the devices drive the game'
                if L.end else ''))
    if A.desync or B.desync:
        print('a replay left the recorded run: see the REPLAYLOG desync '
              'or drift line(s) above')
    return 1 if hard else 0


# ------------------------------------------------------------ the self test

def self_test():
    fails = 0

    def check(cond, what):
        nonlocal fails
        if not cond:
            fails += 1
            print('FAIL', what)

    m = lambda s, fam='X', kl=False: mask(s, fam, kl)
    check(m('Released Keyboard @ cb4aa680 - RefCnt = 1') ==
          'Released Keyboard @ <addr> - RefCnt = 1', 'released address')
    check(m('Floor Data Size 8330 @ d2fdab00') ==
          'Floor Data Size 8330 @ <addr>', 'floor data address')
    check(m('POOL base=0x00D2F1D040 size=20000000') ==
          'POOL base=<addr> size=20000000', 'pool base')
    check(m('ATANLOG x=6014 y=-2147483648 caller=PCtomb5+0x1A2B3') ==
          'ATANLOG x=6014 y=-2147483648 caller=<addr>', 'atan caller')
    check(m('LOADLOG level=TITLE step=screen ms=97.4', 'LOADLOG') ==
          'LOADLOG level=TITLE step=screen ms=<t>', 'load ms')
    check(m('GLOSSLOG level=TITLE field_kb=1034 field_ms=1.8') ==
          'GLOSSLOG level=TITLE field_kb=1034 field_ms=<t>', 'field_ms')
    check(m('LOADLOG level=A step=wait frames=12 shown=3 ms=5', 'LOADLOG')
          == 'LOADLOG level=A step=wait frames=<t> shown=<t> ms=<t>',
          'load frames')
    check(m('SAVELOG item=44 stored=0x00394556 how=tag_item enemy=item 1',
            'SAVELOG') == 'SAVELOG item=44 stored=<layout> how=tag_item '
          'enemy=item 1', 'savelog stored')
    check(m('SAVELOG load pool base=0xD2F1D040 items=+0x3AB1D8 '
            'slots=+0x673C88', 'SAVELOG') == 'SAVELOG load pool base=<addr> '
          'items=<layout> slots=<layout>', 'savelog pool')
    check(m('SAVELOG item=44 stored=0x00394556 how=tag_item', 'SAVELOG',
            True) == 'SAVELOG item=44 stored=0x00394556 how=tag_item',
          'savelog kept')
    check(m('KEYLOG action=onekey bits=0x00000020 second=0/0') ==
          'KEYLOG action=onekey bits=0x00000020 second=0/0', 'bits kept')
    check(m('TICKLOG level=0 at=3s fade=0') == 'TICKLOG level=0 at=3s fade=0',
          'tick at kept')

    check(family_of('CAMLOG tick f=0 eye=(1,2,3)/4') == ('CAMLOG tick',
          'CAMLOG'), 'camlog tick')
    check(family_of('CAMLOG draw frame=1') == ('CAMLOG draw', 'CAMLOG'),
          'camlog draw')
    check(family_of('MOUSELOG capture=on active=1')[0] ==
          'MOUSELOG capture=', 'mouselog capture')
    check(family_of('MOUSELOG f=3 dx=1')[0] == 'MOUSELOG', 'mouselog f')
    check(family_of('LIFELOG level=ANDREA2 second=0 frames=168')[0] ==
          'LIFELOG second=', 'lifelog second')
    check(family_of('SFXLOG footsteps: no bare set')[0] ==
          'SFXLOG footsteps:', 'sfx footsteps')
    check(family_of('d2fdab00') == ('<address>', '<address>'), 'bare hex')
    check(family_of('Released Keyboard @ x')[0] == '<text>', 'free text')
    check(family_of('   ')[0] == '<text>', 'empty line')
    check(class_of(*family_of('NEWLOG x=1')) == 'unclassified', 'unknown')
    check(class_of(*family_of('REPLAYLOG level=3 tick=5')) == LOGIC,
          'replaylog level')
    check(class_of(*family_of('REPLAYLOG open mode=replay')) == SESSION,
          'replaylog open')
    check(class_of(*family_of('HDLOG debris break level=A')) == LOGIC,
          'hdlog debris')
    check(class_of(*family_of('HDLOG frame lara')) == RENDER, 'hdlog')
    check(class_of(*family_of('HAIRLOG braid rootz -33')) == RENDER,
          'the braid is render')
    check(m('LIFELOG level=A bodies=1 bytes=2480 models=x', 'LIFELOG') ==
          'LIFELOG level=A bodies=1 bytes=<layout> models=x', 'lifelog bytes')
    check(m('LIFELOG level=A bytes=2480', 'LIFELOG', True) ==
          'LIFELOG level=A bytes=2480', 'lifelog bytes kept')
    check(class_of(*family_of('REPLAYLOG drift tick=5 record=9')) == SESSION,
          'replaylog drift')

    check(compare_pair('A x=1 y=2.50', 'A x=1 y=2.50', 0) == 'equal', 'eq')
    check(compare_pair('A x=1 y=2.50', 'A x=1 y=2.51', 0) == 'float',
          'float')
    check(compare_pair('A x=1 y=2.50', 'A x=1 y=2.51', 0.02) == 'equal',
          'float eps')
    check(compare_pair('A x=1 y=2.50', 'A x=2 y=2.51', 0.02) == 'hard',
          'int beside float')
    check(compare_pair('A x=1 y=2.50', 'A z=1 y=2.50', 0) == 'hard',
          'word')
    check(compare_pair('A f=0x10', 'A f=0x11', 1) == 'hard', 'hex')

    print('self test: %d failed' % fails)
    return 1 if fails else 0


def main(argv):
    eps = 0.0
    keep_render = keep_layout = False
    show = 0
    files = []
    i = 0

    while i < len(argv):
        a = argv[i]
        if a == '--self-test':
            return self_test()
        if a == '--float-eps' and i + 1 < len(argv):
            eps = float(argv[i + 1])
            i += 2
            continue
        if a == '--show' and i + 1 < len(argv):
            show = int(argv[i + 1])
            i += 2
            continue
        if a == '--keep-render':
            keep_render = True
        elif a == '--keep-layout':
            keep_layout = True
        elif a.startswith('--'):
            print('unknown switch %s' % a)
            return 2
        else:
            files.append(a)
        i += 1

    if len(files) != 2:
        print(__doc__ or 'usage: python xdiff.py A.log B.log [--float-eps E]'
              ' [--keep-render] [--keep-layout] [--show N] | --self-test')
        return 2

    return run(files[0], files[1], eps, keep_render, keep_layout, show)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
