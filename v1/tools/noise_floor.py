#!/usr/bin/env python3
"""ADC noise floor, section 16's first characterization.

    noise_free_bits = log2(full_scale_range / (6.6 * rms_noise_volts))

Short the inputs, take a thousand samples, and report it twice: once with the
island on the barrier jumper (bench power) and once with the isolated DC-DC
running.  **The difference is the quantified cost of isolation, and almost
nobody measures it.**

Input is whatever the bring-up firmware dumps over the console: one signed
24-bit ADC code per line, `#` comments ignored.

    python3 noise_floor.py bench.txt --label "bench power"
    python3 noise_floor.py bench.txt iso.txt --compare
"""

from __future__ import annotations

import argparse
import math
import pathlib
import statistics
import sys

# ADS1220 at PGA = 1 with the 2.048 V internal reference: +/-2.048 V, so the
# full-scale range peak to peak is 4.096 V.
DEFAULT_VREF_MV = 2048.0
FULL_SCALE_COUNTS = 0x7FFFFF  # 2^23 - 1

# The 6.6 in the formula is the peak-to-peak-to-RMS factor for Gaussian noise
# at a 99.9% confidence interval -- it is where "noise-free" differs from
# "effective number of bits", which uses a different factor and always looks
# better on a datasheet.
PP_TO_RMS = 6.6


def code_to_volts(code: float, vref_mv: float = DEFAULT_VREF_MV,
                  gain: int = 1) -> float:
    return (code * vref_mv / 1000.0) / (FULL_SCALE_COUNTS * gain)


def load_codes(path: pathlib.Path) -> list[int]:
    codes: list[int] = []
    for raw in path.read_text().splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        for field in line.replace(",", " ").split():
            try:
                codes.append(int(field))
            except ValueError:
                pass
    return codes


class Result:
    def __init__(self, codes: list[int], vref_mv: float, gain: int,
                 label: str):
        if len(codes) < 16:
            raise ValueError(f"{label}: only {len(codes)} samples; want >= 16")

        self.label = label
        self.n = len(codes)
        self.mean_code = statistics.fmean(codes)
        self.rms_code = statistics.stdev(codes)
        self.pp_code = max(codes) - min(codes)

        self.full_scale_v = 2.0 * (vref_mv / 1000.0) / gain
        self.rms_v = abs(code_to_volts(self.rms_code, vref_mv, gain))
        self.pp_v = abs(code_to_volts(self.pp_code, vref_mv, gain))
        self.offset_v = code_to_volts(self.mean_code, vref_mv, gain)

        denom = PP_TO_RMS * self.rms_v
        self.noise_free_bits = (math.log2(self.full_scale_v / denom)
                                if denom > 0 else float("inf"))
        # TI's "effective resolution", log2(FSR / Vrms), reported alongside
        # because the ADS1220 datasheet quotes that one and it is always
        # log2(6.6) = 2.72 bits larger.  Saying which of the two you measured
        # is the difference between a characterization and a marketing claim:
        # the same board is honestly "18.5 noise-free bits" or "21.2 effective
        # bits", and only one of those is what a reading is good to.
        self.effective_bits = (math.log2(self.full_scale_v / self.rms_v)
                               if self.rms_v > 0 else float("inf"))

    def report(self) -> None:
        print(f"{self.label}")
        print(f"  samples          {self.n}")
        print(f"  mean code        {self.mean_code:,.1f} "
              f"({self.offset_v * 1e6:+.1f} uV offset)")
        print(f"  RMS noise        {self.rms_code:.2f} LSB "
              f"= {self.rms_v * 1e6:.3f} uV")
        print(f"  peak-to-peak     {self.pp_code} LSB "
              f"= {self.pp_v * 1e6:.3f} uV")
        print(f"  noise-free bits  {self.noise_free_bits:.2f}")
        print(f"  effective bits   {self.effective_bits:.2f}  "
              "(datasheet convention; always 2.72 higher)")

        if self.noise_free_bits > 21:
            print("  WARNING: above ~21 noise-free bits on a real board means"
                  "\n           the inputs are probably not actually shorted,"
                  "\n           or the part is returning a constant.")
        elif self.noise_free_bits < 15:
            print("  NOTE: below 15 bits suggests a supply or grounding"
                  "\n        problem rather than an inherent limit."
                  "\n        Check the post-regulating LDO first.")


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", type=pathlib.Path)
    ap.add_argument("--label", action="append", default=None)
    ap.add_argument("--vref-mv", type=float, default=DEFAULT_VREF_MV)
    ap.add_argument("--gain", type=int, default=1)
    ap.add_argument("--compare", action="store_true",
                    help="two files: report the cost of isolation between them")
    args = ap.parse_args()

    labels = args.label or []
    results = []
    for i, path in enumerate(args.files):
        label = labels[i] if i < len(labels) else path.stem
        try:
            results.append(Result(load_codes(path), args.vref_mv, args.gain,
                                  label))
        except (OSError, ValueError) as exc:
            print(f"error: {exc}", file=sys.stderr)
            return 1

    for r in results:
        r.report()
        print()

    if args.compare and len(results) >= 2:
        bench, iso = results[0], results[1]
        delta = bench.noise_free_bits - iso.noise_free_bits
        print("=" * 60)
        print("COST OF ISOLATION")
        print(f"  {bench.label:<22} {bench.noise_free_bits:.2f} bits")
        print(f"  {iso.label:<22} {iso.noise_free_bits:.2f} bits")
        print(f"  difference             {delta:.2f} bits")
        print()
        if delta < 0:
            print("  The converter appears to have improved the noise floor,")
            print("  which does not happen. Something else changed between")
            print("  the two runs -- check the jumper was actually moved.")
        elif delta < 0.5:
            print("  Under half a bit: the four coupling paths in section 8.4")
            print("  are well handled. Worth publishing.")
        elif delta < 2.0:
            print("  A normal, honest result for a first board.")
        else:
            print("  More than two bits. Section 8.4's list, in the order")
            print("  worth checking: the post-regulating LDO's PSRR at the")
            print("  switching frequency, physical separation between the")
            print("  converter and the ADC, the Y-capacitor's placement, and")
            print("  the isolated-side ground pour.")
        print("=" * 60)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
