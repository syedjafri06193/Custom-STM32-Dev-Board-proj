#!/usr/bin/env python3
"""PD charger compatibility matrix, section 16's last characterization.

    "Compatibility across sources is the real test of a PD implementation,
     and a table of works / doesn't work / negotiates what across a dozen
     chargers is genuinely useful documentation."

This runs the board's own policy -- the same selection rules pd_policy.c
applies on the target -- against a catalogue of real chargers, so the table
can be produced before the hardware exists and then checked against it.

    python3 pd_matrix.py                    # the built-in catalogue
    python3 pd_matrix.py --chargers my.yaml # measured PDOs from an analyzer
    python3 pd_matrix.py --check-invariant  # assert SPR is never left
"""

from __future__ import annotations

import argparse
import pathlib
import sys

SPR_MAX_MV = 20000
FALLBACK_MV = 5000

# The board's profile, matching board.yaml and BOARD_PD_* in board.h.
BOARD_NEEDS_MA = 1500
BOARD_PREFERRED_MV = 20000

# A dozen real sources, of the kinds somebody will actually plug in.  The
# awkward ones are the point: the 30 W phone charger that offers 9 V and 15 V
# but not 20 V, the hub that derates when other ports are busy, and the EPR
# charger whose 28 V PDO must be ignored rather than accepted.
CATALOGUE = [
    ("USB-A brick + A-to-C cable", [(5000, 2100)]),
    ("Legacy 5 V/500 mA port", [(5000, 500)]),
    ("18 W phone charger", [(5000, 3000), (9000, 2000)]),
    ("30 W phone charger", [(5000, 3000), (9000, 3000), (15000, 2000)]),
    ("45 W laptop charger", [(5000, 3000), (9000, 3000), (15000, 3000),
                             (20000, 2250)]),
    ("65 W laptop charger", [(5000, 3000), (9000, 3000), (15000, 3000),
                             (20000, 3250)]),
    ("100 W laptop charger", [(5000, 3000), (9000, 3000), (15000, 3000),
                              (20000, 5000)]),
    ("Multi-port hub, idle", [(5000, 3000), (9000, 3000), (15000, 3000),
                              (20000, 3000)]),
    ("Multi-port hub, derated", [(5000, 3000), (9000, 2000)]),
    ("Car adapter, 5 V only", [(5000, 3000)]),
    ("140 W EPR charger", [(5000, 3000), (9000, 3000), (15000, 3000),
                           (20000, 5000), (28000, 5000)]),
    ("240 W EPR charger", [(5000, 3000), (9000, 3000), (15000, 3000),
                           (20000, 5000), (28000, 5000), (36000, 5000),
                           (48000, 5000)]),
    # Non-standard but functional: no 5 V PDO, which is out of spec for a
    # USB-C source, but it can supply the board at 12 V so the policy takes
    # it and flags the mismatch. Worth keeping in the table because it is the
    # case where "out of spec" and "does not work" come apart.
    ("Non-standard 12 V supply", [(12000, 3000)]),
    # And the one that genuinely cannot power the board: no 5 V to fall back
    # to, and not enough current at the voltage it does offer.
    ("Overloaded 12 V port", [(12000, 200)]),
]


def select(pdos: list[tuple[int, int]]) -> dict:
    """The same policy pd_policy_select() implements, in Python.

    Two rules, and both of them are the design document's:

      1. Never leave SPR. A PDO above 20 V is only reachable by asking for EPR
         mode, and this board's input stage is rated for 40 V on the strength
         of never doing that. Anything above the ceiling is not "too big", it
         is invisible.
      2. 5 V is always acceptable. Falling back with the capability-mismatch
         bit set is a working board that reports reduced capability; refusing
         the contract is a board that does not turn on.
    """
    usable = [(mv, ma) for mv, ma in pdos if mv <= SPR_MAX_MV]
    ignored_epr = [(mv, ma) for mv, ma in pdos if mv > SPR_MAX_MV]

    if not usable:
        return {"ok": False, "mv": 0, "ma": 0, "mismatch": False,
                "ignored_epr": ignored_epr,
                "reason": "nothing at or below the SPR ceiling"}

    # Highest SPR voltage that can also supply the board's current.
    capable = [(mv, ma) for mv, ma in usable if ma >= BOARD_NEEDS_MA]
    if capable:
        mv, ma = max(capable)
        return {"ok": True, "mv": mv, "ma": ma,
                "mismatch": mv < BOARD_PREFERRED_MV,
                "ignored_epr": ignored_epr,
                "reason": "" if mv >= BOARD_PREFERRED_MV
                          else f"best available is {mv / 1000:g} V"}

    five = [(mv, ma) for mv, ma in usable if mv == FALLBACK_MV]
    if five:
        mv, ma = five[0]
        return {"ok": True, "mv": mv, "ma": ma, "mismatch": True,
                "ignored_epr": ignored_epr,
                "reason": f"only {ma} mA offered; loop supply unavailable"}

    return {"ok": False, "mv": 0, "ma": 0, "mismatch": False,
            "ignored_epr": ignored_epr,
            "reason": "no 5 V fallback and nothing meets the current draw"}


def load_chargers(path: pathlib.Path):
    import yaml
    data = yaml.safe_load(path.read_text())
    return [(e["name"], [(p["mv"], p["ma"]) for p in e["pdos"]])
            for e in data["chargers"]]


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--chargers", type=pathlib.Path)
    ap.add_argument("--check-invariant", action="store_true",
                    help="exit non-zero if any source would take us out of SPR")
    ap.add_argument("--markdown", action="store_true")
    args = ap.parse_args()

    chargers = (load_chargers(args.chargers) if args.chargers else CATALOGUE)

    rows = []
    violations = []
    for name, pdos in chargers:
        r = select(pdos)
        rows.append((name, pdos, r))
        if r["ok"] and r["mv"] > SPR_MAX_MV:
            violations.append(name)

    if args.markdown:
        print("| Source | PDOs offered | Negotiated | Mismatch | Notes |")
        print("|---|---|---|---|---|")
        for name, pdos, r in rows:
            offered = ", ".join(f"{mv / 1000:g}V/{ma / 1000:g}A"
                                for mv, ma in pdos)
            got = (f"{r['mv'] / 1000:g} V @ {r['ma'] / 1000:g} A"
                   if r["ok"] else "**no contract**")
            note = r["reason"]
            if r["ignored_epr"]:
                epr = ", ".join(f"{mv / 1000:g}V" for mv, _ in r["ignored_epr"])
                note = (note + "; " if note else "") + f"ignored EPR: {epr}"
            print(f"| {name} | {offered} | {got} | "
                  f"{'yes' if r['mismatch'] else 'no'} | {note} |")
    else:
        width = max(len(n) for n, _, _ in rows)
        print(f"{'source':<{width}}  {'negotiated':>14}  mismatch  notes")
        print("-" * (width + 40))
        for name, _, r in rows:
            got = (f"{r['mv'] / 1000:g} V @ {r['ma'] / 1000:g} A"
                   if r["ok"] else "NO CONTRACT")
            note = r["reason"]
            if r["ignored_epr"]:
                epr = ", ".join(f"{mv / 1000:g}V" for mv, _ in r["ignored_epr"])
                note = (note + "; " if note else "") + f"ignored EPR {epr}"
            print(f"{name:<{width}}  {got:>14}  "
                  f"{'yes' if r['mismatch'] else 'no':>8}  {note}")

        powered = sum(1 for _, _, r in rows if r["ok"])
        full = sum(1 for _, _, r in rows if r["ok"] and not r["mismatch"])
        print()
        print(f"  powers up on {powered}/{len(rows)} sources")
        print(f"  full 20 V loop supply on {full}/{len(rows)}")
        print()
        print("  The sources that give 5 V only still bring the board up, and")
        print("  the firmware reports reduced capability rather than failing.")
        print("  That is section 6.5: a dev board that only works with a PD")
        print("  charger is a dev board that does not work when you need it.")

    if args.check_invariant:
        if violations:
            print(f"\nSPR INVARIANT VIOLATED for: {violations}", file=sys.stderr)
            return 1
        print("\n  SPR invariant holds: no source produces a contract above "
              f"{SPR_MAX_MV / 1000:g} V.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
