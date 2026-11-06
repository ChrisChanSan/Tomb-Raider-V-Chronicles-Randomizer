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
"""spark_life.py - two logs' SPARKLOG lines, spark by spark.

    python spark_life.py <log A> <log B> [--tick N] [--near K] [--allocs]

Pairs the SPARKLOG life lines of two logs: born, fire, end, light,
light_index, and the draw's kill line (why=kill). A spark is keyed by
(slot, born tick, n), n counting the births in that slot and tick of one
log, so the same spark in both runs has the same key while the two runs
make the same sparks.

Prints every chain spark alive at tick N (default 841) in either log with
its lines in each, and marks the sparks whose fate differs (DIFF) or that
one log does not have (ONLY A / ONLY B). A spark counts as alive from its
born tick to its fire, its end, the next birth in its slot or born + life,
whichever comes first. Then the fire lines within K ticks of N (default 3)
that one log has and the other does not, and the light/light_index lines
in that window.

--allocs prints instead, for each log, the SPARKLOG draw_alloc lines
(spark slots GetFreeSpark handed out inside a frame that gives its draws
back) up to tick N with their running count, then per tick the two counts
and the running difference A - B, then the first born line at tick N or
later in each log with the draw_allocs before it in the log and its next=:
the difference of the two next= against the difference of the counts.
Also the SPARKLOG level lines and the rain lines up to tick N per log.

Python 3, no packages; names no path of its own. Nothing is written.

GlobalCounter is the tick: UpdateSparks runs after the input of the tick
it names, so the body in the -rngtrace span of tick T+1 logs tick=T.
"""
import re
import sys

LINE = re.compile(r"SPARKLOG (born|fire|end|light_index|light|kill) (.*)$")
ALLOC = re.compile(r"SPARKLOG (draw_alloc|born|rain|level) (.*)$")
KV = re.compile(r"(\w+)=(\S+)")


def num(v):
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def parse(path):
    """Sparks by key, in the order of their birth, and the loose lines."""
    sparks = {}
    order = []
    current = {}        # slot -> key of the last spark born there
    births = {}         # (slot, tick) -> births so far
    pending = {}        # slot -> light lines waiting for their born line
    loose = []          # light/light_index/fire/end/kill lines with no born
    with open(path, "rb") as f:
        for no, raw in enumerate(f, 1):
            text = raw.decode("latin-1").rstrip("\r\n")
            m = LINE.search(text)
            if not m:
                continue
            kind = m.group(1)
            kv = {k: num(v) for k, v in KV.findall(m.group(2))}
            ev = {"kind": kind, "no": no, "kv": kv,
                  "text": text[m.start():]}
            if "slot" not in kv or "tick" not in kv:
                continue
            slot = kv["slot"]
            if kind == "light":
                pending.setdefault(slot, []).append(ev)
                continue
            if kind == "born":
                t = kv["tick"]
                n = births.get((slot, t), 0)
                births[(slot, t)] = n + 1
                key = (slot, t, n)
                if slot in current:
                    old = sparks[current[slot]]
                    if old["until"] is None:
                        old["until"] = t
                        old["reborn"] = True
                sp = {"key": key, "born": ev, "events": pending.pop(slot, []),
                      "until": None, "reborn": False}
                sp["events"].append(ev)
                sparks[key] = sp
                order.append(key)
                current[slot] = key
                continue
            # fire, end, kill, light_index: the spark last born in the slot.
            if kind == "kill" and "chain" not in kv:
                continue
            key = current.get(slot)
            if key is None:
                loose.append(ev)
                continue
            sp = sparks[key]
            sp["events"].append(ev)
            if kind in ("fire", "end", "kill") and sp["until"] is None:
                sp["until"] = kv["tick"]
            if kind in ("end", "kill"):
                del current[slot]
    for sp in sparks.values():
        b = sp["born"]["kv"]
        cap = b["tick"] + b.get("life", 255)
        sp["last"] = cap if sp["until"] is None else min(sp["until"], cap)
    return sparks, order, loose


def alive(sp, tick):
    b = sp["born"]["kv"]
    return b.get("chain", 0) > 0 and b["tick"] <= tick <= sp["last"]


def fate(sp):
    """What happened to the spark after its birth, as comparable tuples."""
    out = []
    for ev in sp["events"]:
        kv = ev["kv"]
        if ev["kind"] == "born":
            out.append(("born", kv.get("chain"), kv.get("fires_at"),
                        kv.get("life"), kv.get("dyn"), kv.get("parent"),
                        kv.get("in_pass")))
        elif ev["kind"] in ("fire", "end", "kill"):
            out.append((ev["kind"], kv["tick"], kv.get("why"),
                        kv.get("age")))
        else:
            out.append((ev["kind"], kv["tick"], kv.get("dyn")))
    if sp["reborn"] and sp["until"] is not None:
        out.append(("slot reused", sp["until"]))
    return out


def show(tag, sp):
    if sp is None:
        print("    %s: none" % tag)
        return
    for ev in sp["events"]:
        print("    %s %6d: %s" % (tag, ev["no"], ev["text"]))
    if sp["reborn"]:
        print("    %s         (slot born again at tick %d with no end line)"
              % (tag, sp["until"]))


def parse_allocs(path):
    """The draw_alloc, born, rain and level lines of a log, in its order."""
    out = []
    with open(path, "rb") as f:
        for no, raw in enumerate(f, 1):
            text = raw.decode("latin-1").rstrip("\r\n")
            m = ALLOC.search(text)
            if not m:
                continue
            kv = {k: num(v) for k, v in KV.findall(m.group(2))}
            out.append({"kind": m.group(1), "no": no, "kv": kv,
                        "text": text[m.start():]})
    return out


def allocs(path_a, path_b, tick):
    logs = (("A", path_a, parse_allocs(path_a)),
            ("B", path_b, parse_allocs(path_b)))
    per_tick = {}
    first = {}
    for tag, path, evs in logs:
        print("%s = %s" % (tag, path))
        n = 0
        for e in evs:
            if e["kind"] == "level":
                print("  %s %6d: %s" % (tag, e["no"], e["text"]))
        for e in evs:
            t = e["kv"].get("tick")
            if e["kind"] == "draw_alloc":
                n += 1
                per_tick.setdefault(t, {"A": 0, "B": 0})[tag] += 1
                if t <= tick:
                    print("  %s %6d: #%d %s" % (tag, e["no"], n, e["text"]))
            elif e["kind"] == "rain" and t is not None and t <= tick:
                print("  %s %6d: %s" % (tag, e["no"], e["text"]))
            elif (e["kind"] == "born" and tag not in first and
                  t is not None and t >= tick):
                first[tag] = (e, n)
        print("  %s: %d draw_alloc lines in all\n" % (tag, n))
    print("per tick (draw_alloc A, B, running A - B) up to tick %d:" % tick)
    run = 0
    for t in sorted(k for k in per_tick if k <= tick):
        c = per_tick[t]
        run += c["A"] - c["B"]
        print("  tick=%d  A=%d B=%d  A-B=%d" % (t, c["A"], c["B"], run))
    print("\nfirst born at tick %d or later:" % tick)
    for tag in ("A", "B"):
        if tag in first:
            e, n = first[tag]
            print("  %s %6d: after %d draw_allocs: %s" % (tag, e["no"], n,
                  e["text"]))
        else:
            print("  %s: none" % tag)
    if "A" in first and "B" in first:
        da = first["A"][1] - first["B"][1]
        dn = (first["A"][0]["kv"].get("next", 0) -
              first["B"][0]["kv"].get("next", 0)) % 1024
        if dn > 512:
            dn -= 1024
        print("\nnext= A - B = %d, draw_allocs before it A - B = %d%s"
              % (dn, da, "" if dn == da else
                 "  (they differ: skipped slots or logic allocations)"))
    return 0


def main(argv):
    args = [a for a in argv[1:]]
    tick, near = 841, 3
    if "--allocs" in args:
        args.remove("--allocs")
        do_allocs = True
    else:
        do_allocs = False
    if "--tick" in args:
        i = args.index("--tick")
        tick = int(args[i + 1])
        del args[i:i + 2]
    if "--near" in args:
        i = args.index("--near")
        near = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    if do_allocs:
        return allocs(args[0], args[1], tick)
    a, order_a, loose_a = parse(args[0])
    b, order_b, loose_b = parse(args[1])
    print("A = %s: %d sparks born" % (args[0], len(a)))
    print("B = %s: %d sparks born" % (args[1], len(b)))

    keys = [k for k in order_a if alive(a[k], tick)]
    keys += [k for k in order_b if alive(b[k], tick) and k not in keys]
    keys.sort()
    differ = 0
    print("\nchain sparks alive at tick %d: %d" % (tick, len(keys)))
    for k in keys:
        sa, sb = a.get(k), b.get(k)
        if sa is None or sb is None:
            mark = "ONLY A" if sb is None else "ONLY B"
        else:
            mark = "same" if fate(sa) == fate(sb) else "DIFF"
        if mark != "same":
            differ += 1
        print("  slot=%d born=%d n=%d  %s" % (k[0], k[1], k[2], mark))
        show("A", sa)
        show("B", sb)

    print("\nfire lines within %d ticks of %d in one log only:" % (near, tick))
    fa = {(k, e["kv"]["tick"]) for k, s in a.items() for e in s["events"]
          if e["kind"] == "fire" and abs(e["kv"]["tick"] - tick) <= near}
    fb = {(k, e["kv"]["tick"]) for k, s in b.items() for e in s["events"]
          if e["kind"] == "fire" and abs(e["kv"]["tick"] - tick) <= near}
    for tag, mine, other, sp in (("A", fa, fb, a), ("B", fb, fa, b)):
        for k, t in sorted(mine - other, key=lambda x: (x[1], x[0])):
            print("  %s fire tick=%d slot=%d born=%d n=%d" % (tag, t, k[0],
                  k[1], k[2]))

    print("\nlight / light_index lines within %d ticks of %d:" % (near, tick))
    for tag, sparks, loose in (("A", a, loose_a), ("B", b, loose_b)):
        evs = [e for s in sparks.values() for e in s["events"]
               if e["kind"] in ("light", "light_index")] + \
              [e for e in loose if e["kind"] in ("light", "light_index")]
        for e in sorted(evs, key=lambda e: e["no"]):
            if abs(e["kv"]["tick"] - tick) <= near:
                print("  %s %6d: %s" % (tag, e["no"], e["text"]))
    if loose_a or loose_b:
        print("\nlines with no born line before them: A %d, B %d"
              % (len(loose_a), len(loose_b)))

    print("\n%d of %d sparks differ" % (differ, len(keys)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
