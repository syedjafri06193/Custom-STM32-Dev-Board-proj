#!/usr/bin/env python3
"""CAN bit timing, computed independently of the firmware's C solver.

This exists to be a second opinion.  can_timing.c runs on the target and picks
the register values the bus actually sees; if it is wrong, every frame is
wrong, and the symptom -- occasional errors that get worse with cable length
-- is indistinguishable from a dozen other problems.  So the numbers get
computed twice, by two implementations written to the same specification
rather than one ported from the other, and tests/test_can_timing_crosscheck.py
asserts they agree.

Usage:
    python3 can_timing.py --clock 170000000 --bitrate 500000
    python3 can_timing.py --clock 170000000 --bitrate 2000000 --fd
    python3 can_timing.py --table
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass

# STM32G4 FDCAN register limits.  The data phase registers are much smaller
# than the nominal ones, and a solver that ignores that produces values which
# silently truncate when written.
NBRP_MAX = 512
NTSEG1_MAX = 256
NTSEG2_MAX = 128
NSJW_MAX = 128

DBRP_MAX = 32
DTSEG1_MAX = 32
DTSEG2_MAX = 16
DSJW_MAX = 16

# CiA 601-2's default, and the one to use when nothing else is specified.
DEFAULT_SAMPLE_POINT_PERMIL = 875

# Time quanta beyond this buy nothing.  Below it, resolution is the binding
# constraint: a five-tq bit can express a sample point exactly, but it leaves
# SJW at 1 and no room for transmitter delay compensation.  Ranking resolution
# above sample point up to this floor -- and ignoring it above -- is what stops
# the solver answering 2 Mbit/s CAN FD with five time quanta.
RESOLUTION_ENOUGH_TQ = 16

OSC_PPM_HSI16 = 10000
OSC_PPM_CRYSTAL_30 = 30
OSC_PPM_CRYSTAL_50 = 50


@dataclass(frozen=True)
class Timing:
    valid: bool = False
    brp: int = 0
    tseg1: int = 0
    tseg2: int = 0
    sjw: int = 0
    nominal_bt_tq: int = 0
    bitrate_bps: int = 0
    bitrate_error_ppm: int = 0
    sample_point_permil: int = 0

    @property
    def register_nbtp(self) -> int:
        return (((self.sjw - 1) & 0x7F) << 25 | ((self.brp - 1) & 0x1FF) << 16
                | ((self.tseg1 - 1) & 0xFF) << 8 | ((self.tseg2 - 1) & 0x7F))

    @property
    def register_dbtp(self) -> int:
        return (((self.brp - 1) & 0x1F) << 16 | ((self.tseg1 - 1) & 0x1F) << 8
                | ((self.tseg2 - 1) & 0xF) << 4 | ((self.sjw - 1) & 0xF))


def max_tolerance_ppm(t: Timing) -> int:
    """The oscillator tolerance this bit timing can absorb, per node.

    ISO 11898-1 gives two bounds and the answer is the smaller:

        df <= sjw / (20 * nbt)
        df <= min(phase_seg1, phase_seg2) / (2 * (13 * nbt - phase_seg2))

    The first is resynchronisation headroom; the second is the five-bit
    stretch a receiver has to tolerate between resynchronisation
    opportunities.  Which one binds depends on the sample point, which is why
    both have to be evaluated rather than quoting the familiar one.
    """
    if not t.valid or t.nominal_bt_tq == 0:
        return 0

    nbt = t.nominal_bt_tq
    ps2 = t.tseg2
    ps1 = t.tseg1  # prop_seg + phase_seg1; the bound uses the pair together

    term_sjw = (t.sjw * 1_000_000) // (20 * nbt)

    smaller = min(ps1, ps2)
    denom = 2 * (13 * nbt - ps2)
    term_phase = (smaller * 1_000_000) // denom if denom > 0 else 0

    return min(term_sjw, term_phase)


def osc_ok(t: Timing, osc_ppm: int) -> bool:
    """Both nodes contribute error, so each must fit inside the budget."""
    return max_tolerance_ppm(t) >= osc_ppm


def score(t: Timing, want_sp: int) -> tuple:
    """The preference order, as a sort key -- lower is better.

    This is the specification the two implementations share.  How each one
    searches is deliberately different, so that a disagreement means
    something; what counts as a better answer has to be the same, or the
    comparison is meaningless.

      1. Bit rate error, because a bit rate that is merely close is a bus that
         works until it does not.
      2. Resolution, up to RESOLUTION_ENOUGH_TQ and no further.  Below the
         floor, more time quanta genuinely matter; above it they are free and
         should not outrank anything.
      3. Sample point, exactly as requested.  An earlier sample point is a
         real change in how the bus behaves on a long cable, so this is not
         somewhere to be approximate -- an earlier version of this solver had
         a five-percent tie band here and quietly returned 82.7% when asked
         for 87.5%, because the wider phase segment 2 bought it a better
         tolerance score.  That is a trade a person should make deliberately
         by asking for a different sample point, not one a solver should make
         on their behalf.
      4. Oscillator tolerance, because among genuine equals, prefer the timing
         that survives a worse crystal.
      5. More time quanta, purely so the answer is deterministic.
    """
    return (t.bitrate_error_ppm,
            -min(t.nominal_bt_tq, RESOLUTION_ENOUGH_TQ),
            abs(t.sample_point_permil - want_sp),
            -max_tolerance_ppm(t),
            -t.nominal_bt_tq)


def solve(clock_hz: int, bitrate_bps: int,
          sample_point_permil: int = DEFAULT_SAMPLE_POINT_PERMIL,
          data_phase: bool = False) -> Timing:
    """Exhaustive search over every legal (brp, tseg1, tseg2).

    The C solver does a local search: it computes the ideal tseg1 for each
    prescaler and examines that value plus its two neighbours.  That is fast
    and fits on the target, but a local search can miss.  This one enumerates
    the whole space, which is slow and cannot, so the cross-check can tell the
    difference between "the two agree" and "the two agree because one copied
    the other".
    """
    if clock_hz == 0 or bitrate_bps == 0:
        return Timing()
    if sample_point_permil <= 0 or sample_point_permil >= 1000:
        sample_point_permil = DEFAULT_SAMPLE_POINT_PERMIL

    brp_max = DBRP_MAX if data_phase else NBRP_MAX
    tseg1_max = DTSEG1_MAX if data_phase else NTSEG1_MAX
    tseg2_max = DTSEG2_MAX if data_phase else NTSEG2_MAX
    sjw_max = DSJW_MAX if data_phase else NSJW_MAX

    best: Timing | None = None
    best_key: tuple | None = None

    for brp in range(1, brp_max + 1):
        # The hardware divides the kernel clock by brp, then counts nbt time
        # quanta per bit.  Only the total brp * nbt matters to the bit rate,
        # so enumerate nbt directly instead of deriving it.
        for nbt in range(4, 2 + tseg1_max + tseg2_max):
            divisor = brp * nbt
            actual = clock_hz // divisor
            if actual == 0:
                break
            error_ppm = abs(actual - bitrate_bps) * 1_000_000 // bitrate_bps
            # Anything beyond a few percent is never going to win; skipping it
            # keeps the exhaustive search tolerable.
            if error_ppm > 50_000:
                continue

            for tseg1 in range(1, min(tseg1_max, nbt - 2) + 1):
                tseg2 = nbt - 1 - tseg1
                if tseg2 < 1 or tseg2 > tseg2_max:
                    continue

                cand = Timing(
                    valid=True, brp=brp, tseg1=tseg1, tseg2=tseg2,
                    sjw=min(tseg2, sjw_max), nominal_bt_tq=nbt,
                    bitrate_bps=actual, bitrate_error_ppm=error_ppm,
                    sample_point_permil=((1 + tseg1) * 1000) // nbt,
                )
                key = score(cand, sample_point_permil)
                if best_key is None or key < best_key:
                    best, best_key = cand, key

    return best or Timing()


def tdco(t: Timing, transceiver_delay_ns: int, clock_hz: int) -> int:
    """Secondary sample point offset, in FDCAN clock cycles.

    Above roughly 1 Mbit/s the transceiver's loop delay is a real fraction of
    a data bit, and without compensation the controller samples its own
    transmission at the wrong moment and reports bit errors on a bus that is
    working correctly.
    """
    if not t.valid or clock_hz == 0:
        return 0
    delay_cycles = (transceiver_delay_ns * clock_hz) // 1_000_000_000
    ssp = delay_cycles + t.brp * (1 + t.tseg1)
    return min(ssp, 127)


def _report(t: Timing, label: str, clock_hz: int) -> None:
    if not t.valid:
        print(f"{label}: NO SOLUTION")
        return
    tol = max_tolerance_ppm(t)
    print(f"{label}")
    print(f"  bit rate        {t.bitrate_bps:,} bit/s "
          f"(error {t.bitrate_error_ppm} ppm)")
    print(f"  time quanta     {t.nominal_bt_tq} "
          f"(brp {t.brp}, tq = {clock_hz / t.brp / 1e6:.3f} MHz)")
    print(f"  tseg1 / tseg2   {t.tseg1} / {t.tseg2}")
    print(f"  sjw             {t.sjw}")
    print(f"  sample point    {t.sample_point_permil / 10:.1f}%")
    print(f"  osc tolerance   {tol} ppm per node")
    print(f"    30 ppm xtal   {'OK' if osc_ok(t, OSC_PPM_CRYSTAL_30) else 'FAIL'}")
    print(f"    50 ppm xtal   {'OK' if osc_ok(t, OSC_PPM_CRYSTAL_50) else 'FAIL'}")
    print(f"    HSI16 +/-1%   {'OK' if osc_ok(t, OSC_PPM_HSI16) else 'FAIL'}"
          "   <- section 7.2")
    print(f"  NBTP            0x{t.register_nbtp:08X}")
    print(f"  DBTP            0x{t.register_dbtp:08X}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--clock", type=int, default=170_000_000,
                    help="FDCAN kernel clock in Hz (default 170 MHz)")
    ap.add_argument("--bitrate", type=int, default=500_000)
    ap.add_argument("--sample-point", type=int,
                    default=DEFAULT_SAMPLE_POINT_PERMIL,
                    help="per mil, e.g. 875 for 87.5%%")
    ap.add_argument("--fd", action="store_true",
                    help="solve within the tighter data-phase register limits")
    ap.add_argument("--table", action="store_true",
                    help="every standard rate, plus the FD data phases")
    args = ap.parse_args()

    if args.table:
        print(f"FDCAN kernel clock: {args.clock:,} Hz\n")
        header = (f"{'rate':>10} {'phase':>8} {'brp':>4} {'tseg1':>6} "
                  f"{'tseg2':>6} {'sjw':>4} {'tq':>4} {'SP':>7} "
                  f"{'err ppm':>8} {'tol ppm':>8}  crystal HSI16")
        print(header)
        print("-" * len(header))
        rows = [(r, False, DEFAULT_SAMPLE_POINT_PERMIL)
                for r in (125_000, 250_000, 500_000, 1_000_000)]
        rows += [(r, True, 800) for r in (2_000_000, 5_000_000)]
        for rate, fd, sp in rows:
            t = solve(args.clock, rate, sp, fd)
            if not t.valid:
                print(f"{rate:>10,} {'data' if fd else 'nominal':>8}  NO SOLUTION")
                continue
            print(f"{rate:>10,} {'data' if fd else 'nominal':>8} {t.brp:>4} "
                  f"{t.tseg1:>6} {t.tseg2:>6} {t.sjw:>4} {t.nominal_bt_tq:>4} "
                  f"{t.sample_point_permil / 10:>6.1f}% "
                  f"{t.bitrate_error_ppm:>8} {max_tolerance_ppm(t):>8}  "
                  f"{'OK   ' if osc_ok(t, OSC_PPM_CRYSTAL_30) else 'FAIL '}   "
                  f"{'OK' if osc_ok(t, OSC_PPM_HSI16) else 'FAIL'}")
        print("\nThe HSI16 column is section 7.2: there is no standard bit "
              "rate at which\nskipping the crystal is acceptable.")
        return 0

    t = solve(args.clock, args.bitrate, args.sample_point, args.fd)
    _report(t, "data phase" if args.fd else "nominal phase", args.clock)
    return 0 if t.valid else 1


if __name__ == "__main__":
    raise SystemExit(main())
