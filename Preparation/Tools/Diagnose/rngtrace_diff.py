# Modified 2026 by ChrisChanSan.
#
# Part of the TOMB5 native Windows 11 port (Direct3D 9 / XAudio2) and of the
# TR5 Randomizer project. The individual changes are recorded in the git
# history of this source tree.
#
# TOMB5 is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See the LICENSE file at the root of this source tree.
"""rngtrace_diff.py - the first call where two -rngtrace logs part.

    python rngtrace_diff.py <log A> <log B> [--exe <PCtomb5 exe>]
                            [--addrline <AddrLine.exe>] [--around N]

Reads every `RNGTRACE tick=` line of both logs (one per span between two
input calls, specific/function_stubs.cpp) with the `RNGTRACE section=`
lines that follow it, pairs the spans in their order, and finds the first
pair whose call lists differ. Prints that tick, both spans' counts and
generator states, the section marks of both side by side with the first
one whose state differs, the index of the first differing call and the
marks it falls between on either side, and N addresses on either side of
it (default 5: ten around it) in both logs; then hands every address
shown to AddrLine with -ret (they are return addresses) against --exe,
which must be the build that wrote both logs, its PDB beside it.

Without --exe the Asan build of this tree is taken
(<repo>/releaseasan/PCtomb5_asan.exe), without --addrline the AddrLine.exe
beside this script. Python 3, no packages. Nothing is written.
"""
import os
import re
import subprocess
import sys

# Relative to this script's folder, Preparation/Tools/Diagnose: AddrLine.exe
# is built beside it (bal.bat), the Asan engine at the repository's root in
# releaseasan/ (basan.bat, tomb5.vcxproj's Asan|x64 OutDir).
HERE = os.path.dirname(os.path.abspath(__file__))
DEF_EXE = os.path.normpath(os.path.join(HERE, "..", "..", "..", "releaseasan",
                                        "PCtomb5_asan.exe"))
DEF_AL = os.path.join(HERE, "AddrLine.exe")

TICK = re.compile(r"RNGTRACE tick=(\d+) calls=(\d+) seed_in=0x([0-9A-Fa-f]+)"
                  r" seed_out=0x([0-9A-Fa-f]+)(?: overflow=(\d+))? addrs=(\S*)")
SECT = re.compile(r"RNGTRACE section=(\S+) tick=(\d+) at=(\d+) "
                  r"seed=0x([0-9A-Fa-f]+)")


def read_spans(path):
    spans = []
    with open(path, "rb") as f:
        for raw in f:
            line = raw.decode("latin-1").rstrip("\r\n")
            m = TICK.search(line)
            if m:
                addrs = m.group(6).split(",") if m.group(6) else []
                spans.append({
                    "tick": int(m.group(1)), "calls": int(m.group(2)),
                    "seed_in": m.group(3).upper(),
                    "seed_out": m.group(4).upper(),
                    "overflow": int(m.group(5) or 0),
                    "addrs": [a.lower() for a in addrs], "sections": []})
                continue
            m = SECT.search(line)
            if m and spans:
                spans[-1]["sections"].append(
                    (m.group(1), int(m.group(3)), m.group(4).upper()))
    return spans


def section_of(span, index):
    """Where a call falls: before the first mark with more calls before it
    than the call's index (and after the mark before that one); past the
    last mark, the closing tick's start up to its input call."""
    prev = "the span's opening input"
    for name, at, _ in span["sections"]:
        if index < at:
            return "after %s, before mark '%s'" % (prev, name)
        prev = "mark '%s'" % name
    return "after %s, in the closing tick's start" % prev


def window(addrs, at, n):
    lo = max(0, at - n)
    return lo, addrs[lo:at + n]


def resolve(exe, addrline, addrs):
    if not addrs:
        return {}
    if not os.path.isfile(addrline):
        print("AddrLine not found: %s" % addrline)
        return {}
    out = subprocess.run([addrline, exe, "-ret"] + addrs,
                         capture_output=True, text=True)
    print(out.stdout.splitlines()[0] if out.stdout else out.stderr)
    names = {}
    for line in out.stdout.splitlines()[1:]:
        parts = line.split(" ", 1)
        if len(parts) == 2:
            names[parts[0].lower()] = parts[1]
    return names


def main(argv):
    args = list(argv[1:])
    exe, addrline, around = DEF_EXE, DEF_AL, 5
    for flag in ("--exe", "--addrline", "--around"):
        if flag in args:
            i = args.index(flag)
            value = args[i + 1]
            del args[i:i + 2]
            if flag == "--exe":
                exe = value
            elif flag == "--addrline":
                addrline = value
            else:
                around = int(value)
    if len(args) != 2:
        print(__doc__)
        return 2

    a, b = read_spans(args[0]), read_spans(args[1])
    print("A %s: %d spans (ticks %s)" % (args[0], len(a),
          ",".join(str(s["tick"]) for s in a)))
    print("B %s: %d spans (ticks %s)" % (args[1], len(b),
          ",".join(str(s["tick"]) for s in b)))

    for k in range(min(len(a), len(b))):
        sa, sb = a[k], b[k]
        if sa["tick"] != sb["tick"]:
            print("span %d: ticks differ, A %d B %d - the logs are not "
                  "paired" % (k, sa["tick"], sb["tick"]))
            return 1
        if sa["addrs"] == sb["addrs"] and sa["calls"] == sb["calls"]:
            if sa["seed_in"] != sb["seed_in"]:
                print("tick %d: same calls, but the state differs on entry "
                      "(A 0x%s B 0x%s)" % (sa["tick"], sa["seed_in"],
                                          sb["seed_in"]))
            continue

        at = 0
        while (at < len(sa["addrs"]) and at < len(sb["addrs"]) and
               sa["addrs"][at] == sb["addrs"][at]):
            at += 1

        print()
        print("first differing span: tick %d" % sa["tick"])
        for tag, s in (("A", sa), ("B", sb)):
            print("  %s calls=%d kept=%d overflow=%d seed_in=0x%s "
                  "seed_out=0x%s" % (tag, s["calls"], len(s["addrs"]),
                                     s["overflow"], s["seed_in"],
                                     s["seed_out"]))
        # The marks side by side: a frame's draws are given back at its
        # 'draw' -> 'frame' step, so a difference there that is gone at the
        # next 'frame' mark did not reach the logic; the first mark whose
        # state differs and stays different is where it did.
        print("  marks (A at/state | B at/state):")
        first_state = None
        for k in range(max(len(sa["sections"]), len(sb["sections"]))):
            ma = sa["sections"][k] if k < len(sa["sections"]) else None
            mb = sb["sections"][k] if k < len(sb["sections"]) else None
            differ = not ma or not mb or ma[0] != mb[0] or ma[2] != mb[2]
            if differ and first_state is None:
                first_state = k
            print("    %s %-8s %6s 0x%-8s | %-8s %6s 0x%-8s" % (
                "!=" if differ else "  ",
                ma[0] if ma else "-", ma[1] if ma else "-",
                ma[2] if ma else "-", mb[0] if mb else "-",
                mb[1] if mb else "-", mb[2] if mb else "-"))
        if first_state is not None:
            print("  first mark whose state differs: #%d" % first_state)
        print("first differing call: index %d\n  A %s\n  B %s" % (
            at, section_of(sa, at), section_of(sb, at)))
        if at >= len(sa["addrs"]) or at >= len(sb["addrs"]):
            print("  (one list ends there: A kept %d, B kept %d)" % (
                len(sa["addrs"]), len(sb["addrs"])))

        shown = []
        for tag, s in (("A", sa), ("B", sb)):
            lo, part = window(s["addrs"], at, around)
            print("  %s [%d..%d]: %s" % (tag, lo, lo + len(part) - 1,
                                         " ".join(part)))
            shown += part
        uniq = sorted(set(shown), key=shown.index)
        names = resolve(exe, addrline, uniq)
        for tag, s in (("A", sa), ("B", sb)):
            lo, part = window(s["addrs"], at, around)
            print("%s:" % tag)
            for i, ad in enumerate(part):
                mark = ">>" if lo + i == at else "  "
                print("  %s %6d %8s %s" % (mark, lo + i, ad,
                                            names.get(ad, "?")))
        return 0

    print("no span differs in %d pairs" % min(len(a), len(b)))
    if len(a) != len(b):
        print("but A has %d spans and B %d" % (len(a), len(b)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
