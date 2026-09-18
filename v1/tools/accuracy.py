#!/usr/bin/env python3
"""Accuracy sweep, section 16's second characterization.

Sweep a precision current source across 4-20 mA, record what the board reads,
and extract gain error, offset error and INL.  Those three numbers are what
turn "it seems accurate" into a specification.

The reason all three matter separately: gain and offset errors are calibrated
out in firmware and cost nothing to fix.  INL is not -- it is the part of the
error that survives a two-point calibration, and it is the board's actual
accuracy limit.  A report that only quotes total error hides which kind you
have, and therefore whether the fix is a config change or a respin.

Input: two columns, applied microamps and measured microamps.

    python3 accuracy.py sweep.csv
    python3 accuracy.py sweep.csv --budget   # compare against board.yaml
"""

from __future__ import annotations

import argparse
import pathlib
import sys

SPAN_LO_UA = 4000
SPAN_HI_UA = 20000


def load(path: pathlib.Path) -> list[tuple[float, float]]:
    rows = []
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.replace(",", " ").split()
        if len(parts) < 2:
            continue
        try:
            rows.append((float(parts[0]), float(parts[1])))
        except ValueError:
            continue  # header row
    return sorted(rows)


def least_squares(points: list[tuple[float, float]]) -> tuple[float, float]:
    """Best-fit measured = gain * applied + offset."""
    n = len(points)
    sx = sum(p[0] for p in points)
    sy = sum(p[1] for p in points)
    sxx = sum(p[0] * p[0] for p in points)
    sxy = sum(p[0] * p[1] for p in points)
    denom = n * sxx - sx * sx
    if denom == 0:
        return 1.0, 0.0
    gain = (n * sxy - sx * sy) / denom
    offset = (sy - gain * sx) / n
    return gain, offset


def analyse(points: list[tuple[float, float]]) -> dict:
    gain, offset = least_squares(points)
    span = SPAN_HI_UA - SPAN_LO_UA

    # INL against the best-fit line, which is the convention that reports the
    # smallest honest number.  Endpoint INL would be larger and is also valid;
    # what matters is saying which was used.
    residuals = [(a, m - (gain * a + offset)) for a, m in points]
    worst_at, worst = max(residuals, key=lambda r: abs(r[1]))

    return {
        "n": len(points),
        "gain": gain,
        "gain_error_ppm": (gain - 1.0) * 1e6,
        "offset_ua": offset,
        "offset_ppm_of_span": offset / span * 1e6,
        "inl_ua": worst,
        "inl_ppm_of_span": worst / span * 1e6,
        "inl_at_ua": worst_at,
        "max_raw_error_ua": max(abs(m - a) for a, m in points),
        "residual_after_cal_ua": max(abs(r[1]) for r in residuals),
    }


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", type=pathlib.Path)
    ap.add_argument("--budget", action="store_true",
                    help="compare the measured error against board.yaml's "
                         "predicted error budget")
    args = ap.parse_args()

    points = load(args.csv)
    if len(points) < 3:
        print(f"error: need at least 3 sweep points, got {len(points)}",
              file=sys.stderr)
        return 1

    r = analyse(points)
    span = SPAN_HI_UA - SPAN_LO_UA

    print(f"Sweep: {r['n']} points, {points[0][0]:.0f} to "
          f"{points[-1][0]:.0f} uA\n")
    print(f"  gain              {r['gain']:.6f}  "
          f"({r['gain_error_ppm']:+.0f} ppm)")
    print(f"  offset            {r['offset_ua']:+.3f} uA  "
          f"({r['offset_ppm_of_span']:+.0f} ppm of span)")
    print(f"  INL (best fit)    {r['inl_ua']:+.3f} uA  "
          f"({r['inl_ppm_of_span']:+.0f} ppm of span) at "
          f"{r['inl_at_ua']:.0f} uA")
    print()
    print(f"  worst raw error   {r['max_raw_error_ua']:.3f} uA")
    print(f"  after 2-pt cal    {r['residual_after_cal_ua']:.3f} uA")
    print()
    print("  Gain and offset are removable in firmware (afe.c's gain_ppm and")
    print("  offset_uv). INL is not -- it is what the board is actually good")
    print("  to once calibrated.")

    if args.budget:
        try:
            import yaml
        except ImportError:
            print("\n(--budget needs pyyaml)", file=sys.stderr)
            return 0

        board_path = (pathlib.Path(__file__).resolve().parents[1]
                      / "hardware" / "board.yaml")
        board = yaml.safe_load(board_path.read_text())
        shunt = board["afe"]["shunt"]

        # The same sum afe_error_budget_ppm() computes on the target, so the
        # prediction and the measurement are made the same way.
        delta_t = 40
        predicted = (shunt["tolerance_ppm"]
                     + shunt["tempco_ppm_per_c"] * delta_t
                     + 2000   # ADS1220 internal reference tolerance
                     + 500)   # ADC gain error

        measured = abs(r["gain_error_ppm"]) + abs(r["offset_ppm_of_span"])

        print(f"\n  predicted budget  {predicted} ppm "
              f"(shunt {shunt['tolerance_ppm']} + tempco "
              f"{shunt['tempco_ppm_per_c'] * delta_t} over {delta_t} C "
              f"+ vref 2000 + adc 500)")
        print(f"  measured          {measured:.0f} ppm before calibration")
        if measured > predicted:
            print("\n  Over budget. The budget is a worst-case sum, so being")
            print("  above it means something is not in the budget -- check")
            print("  the reference and the shunt's actual part number before")
            print("  calibrating the error away.")
        else:
            print("\n  Within the predicted budget.")

        inl_ppm = abs(r["inl_ppm_of_span"])
        print(f"\n  INL is {inl_ppm:.0f} ppm of span, which is the floor: no")
        print("  amount of calibration removes it.")
        print(f"  In engineering units on a 0-250 C transmitter that is "
              f"{inl_ppm * 250 / 1e6:.3f} C.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
