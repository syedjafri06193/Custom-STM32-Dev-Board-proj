#!/usr/bin/env python3
"""Power budget and part sizing, from board.yaml.

Section 5.2: "Build this table before choosing the buck, not after."  The
reason it comes first is that the buck's current rating, the LDOs' dropout
headroom and the isolated module's power all fall out of it, and changing any
of them after layout is a new board.

It also settles the question section 5.2 raises and most projects duck: the
board's own consumption is under 2 W, so PD is not justified by the board.
If PD is on the board it has to be powering something, and the 4-20 mA loop
supply is that something.

    python3 power_budget.py
    python3 power_budget.py --input-mv 5000    # what a dumb 5 V brick gives
"""

from __future__ import annotations

import argparse
import pathlib

import yaml

BOARD = (pathlib.Path(__file__).resolve().parents[1] / "hardware" / "board.yaml")

# Conversion efficiencies, deliberately conservative.  Optimistic numbers here
# produce a buck that is exactly big enough on paper and thermally marginal in
# a warm cabinet.
BUCK_EFFICIENCY = 0.88
ISO_DCDC_EFFICIENCY = 0.75   # 1 W modules are not good, and that is fine
LDO_EFFICIENCY = None        # LDOs do not have one; they burn the difference


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--input-mv", type=int, default=None,
                    help="VBUS to size against (default: the board's maximum)")
    ap.add_argument("--board", type=pathlib.Path, default=BOARD)
    args = ap.parse_args()

    board = yaml.safe_load(args.board.read_text())
    power = board["power"]
    rails = {r["name"]: r for r in power["rails"]}
    vin_mv = args.input_mv or power["input"]["max_mv"]

    print(f"Power budget at {vin_mv / 1000:g} V input\n")
    header = f"{'rail':<10} {'V':>6} {'typ mA':>8} {'peak mA':>9} {'peak W':>9}"
    print(header)
    print("-" * len(header))
    for name in ("3V3_D", "3V3_A", "5V_ISO", "3V3_ISO"):
        r = rails[name]
        print(f"{name:<10} {r['voltage_mv'] / 1000:>6.1f} {r['typical_ma']:>8} "
              f"{r['peak_ma']:>9} "
              f"{r['peak_ma'] * r['voltage_mv'] / 1e6:>9.3f}")

    # --- 5V_MAIN, which carries everything ---
    # The two 3.3 V LDOs draw their output current from 5 V regardless of the
    # voltage drop -- an LDO is a current pass-through, so 60 mA out is 60 mA
    # in, and the 1.7 V difference is heat.  This is the step people get wrong
    # by dividing power instead of passing current through.
    ldo_ma = rails["3V3_D"]["peak_ma"] + rails["3V3_A"]["peak_ma"]
    iso_in_ma = (rails["5V_ISO"]["peak_ma"] * rails["5V_ISO"]["voltage_mv"]
                 / (5000 * ISO_DCDC_EFFICIENCY))
    five_v_ma = ldo_ma + iso_in_ma

    print()
    print(f"  LDO input current    {ldo_ma:.0f} mA "
          "(an LDO passes current, it does not divide power)")
    print(f"  isolated DC-DC input {iso_in_ma:.0f} mA "
          f"(at {ISO_DCDC_EFFICIENCY:.0%} efficiency)")
    print(f"  5V_MAIN total        {five_v_ma:.0f} mA "
          f"= {five_v_ma * 5 / 1000:.2f} W")

    budgeted = rails["5V_MAIN"]["peak_ma"]
    verdict = "OK" if budgeted >= five_v_ma else "UNDER-BUDGETED"
    print(f"  board.yaml budgets   {budgeted} mA  [{verdict}]")

    # --- LDO dissipation, which is where the thermal surprise lives ---
    print("\nLDO dissipation at peak:")
    for name in ("3V3_D", "3V3_A"):
        r = rails[name]
        drop_v = (5000 - r["voltage_mv"]) / 1000
        watts = drop_v * r["peak_ma"] / 1000
        note = ""
        if watts > 0.4:
            note = "  <- needs a package with a thermal pad"
        print(f"  {name:<8} {drop_v:.1f} V x {r['peak_ma']} mA = "
              f"{watts:.3f} W{note}")

    # --- input side ---
    buck_in_w = five_v_ma * 5 / 1000 / BUCK_EFFICIENCY
    buck_in_ma = buck_in_w / (vin_mv / 1000) * 1000
    print(f"\nBuck input at {vin_mv / 1000:g} V "
          f"({BUCK_EFFICIENCY:.0%} efficient):")
    print(f"  {buck_in_ma:.0f} mA, {buck_in_w:.2f} W")
    print(f"  buck dissipation {buck_in_w - five_v_ma * 5 / 1000:.2f} W")

    # --- the loop supply, and why PD is on this board ---
    loop = power["loop_supply"]
    print("\nLoop supply (section 5.2 -- the reason PD is here at all):")
    if not loop["present"]:
        print("  none. The board draws under 2 W, so PD is not earning its")
        print("  place: a 5 V/500 mA port would run this design.")
    else:
        loop_w = (loop["channels"] * loop["current_per_channel_ma"]
                  * loop["voltage_mv"] / 1e6)
        print(f"  {loop['channels']} channels x "
              f"{loop['current_per_channel_ma']} mA at "
              f"{loop['voltage_mv'] / 1000:g} V = {loop_w:.2f} W")
        total_w = buck_in_w + loop_w / 0.85  # boost stage
        print(f"  total input {total_w:.2f} W "
              f"= {total_w / (vin_mv / 1000) * 1000:.0f} mA at "
              f"{vin_mv / 1000:g} V")
        print(f"  the board alone is {buck_in_w:.2f} W of that; the loops are "
              f"{loop_w / 0.85:.2f} W")

    # --- what this means for PD negotiation ---
    print("\nWhat a source has to offer:")
    for mv in (5000, 9000, 15000, 20000):
        need_ma = (buck_in_w + (loop_w / 0.85 if loop["present"] else 0)) \
            / (mv / 1000) * 1000
        typical_max = {5000: 3000, 9000: 3000, 15000: 3000, 20000: 2250}[mv]
        ok = "OK" if need_ma <= typical_max else "NOT ENOUGH"
        print(f"  {mv / 1000:>4.0f} V: needs {need_ma:>5.0f} mA  "
              f"(a typical source offers {typical_max} mA)  [{ok}]")
    print("\n  The board comes up on any of them. At 5 V the loop supply is")
    print("  what runs short first, which is why pd_policy.c reports a")
    print("  capability mismatch rather than refusing the contract.")

    return 0 if budgeted >= five_v_ma else 1


if __name__ == "__main__":
    raise SystemExit(main())
