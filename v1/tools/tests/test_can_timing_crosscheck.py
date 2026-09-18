"""Cross-check the Python CAN solver against the firmware's C solver.

Two implementations of the same specification, written separately, compared
across a grid of clocks, bit rates and sample points.  Where they disagree,
one of them is wrong, and the disagreement is visible here rather than as
intermittent error frames on a warm bus six months from now.

This is the highest-value test in the project.  Bit timing is the one thing
on this board that cannot be checked by looking at it: the register values
are plausible whatever they are, the bus either works or produces errors that
look like a dozen other problems, and by the time the symptom appears the
board is in a cabinet somewhere.
"""

from __future__ import annotations

import json
import pathlib
import subprocess
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import can_timing as py  # noqa: E402

FIRMWARE = pathlib.Path(__file__).resolve().parents[2] / "firmware"

HARNESS = r"""
/* Dumps the C solver's answers as JSON so the Python solver can be compared
 * against them.  Built on demand by the test; not part of the firmware. */
#include <stdio.h>
#include "can_timing.h"

int main(void) {
    const uint32_t clocks[] = {170000000u, 160000000u, 80000000u, 40000000u};
    const uint32_t rates[] = {125000u, 250000u, 500000u, 800000u, 1000000u};
    const uint32_t fd_rates[] = {2000000u, 4000000u, 5000000u, 8000000u};
    const int points[] = {700, 750, 800, 875, 900};

    printf("[");
    int first = 1;
    for (unsigned c = 0; c < sizeof(clocks)/sizeof(*clocks); c++) {
        for (int p = 0; p < 5; p++) {
            for (unsigned r = 0; r < sizeof(rates)/sizeof(*rates); r++) {
                can_timing_t t = can_timing_solve(clocks[c], rates[r],
                                                  points[p], false);
                if (!first) printf(",");
                first = 0;
                printf("{\"clock\":%u,\"rate\":%u,\"sp\":%d,\"fd\":false,"
                       "\"valid\":%s,\"brp\":%u,\"tseg1\":%u,\"tseg2\":%u,"
                       "\"sjw\":%u,\"nbt\":%u,\"bitrate\":%u,\"err\":%d,"
                       "\"sp_out\":%d,\"tol\":%d,\"nbtp\":%u}",
                       clocks[c], rates[r], points[p],
                       t.valid ? "true" : "false", t.brp, t.tseg1, t.tseg2,
                       t.sjw, t.nominal_bt_tq, t.bitrate_bps,
                       t.bitrate_error_ppm, t.sample_point_permil,
                       can_timing_max_tolerance_ppm(&t),
                       can_timing_nbtp(&t));
            }
            for (unsigned r = 0; r < sizeof(fd_rates)/sizeof(*fd_rates); r++) {
                can_timing_t t = can_timing_solve(clocks[c], fd_rates[r],
                                                  points[p], true);
                if (!first) printf(",");
                first = 0;
                printf("{\"clock\":%u,\"rate\":%u,\"sp\":%d,\"fd\":true,"
                       "\"valid\":%s,\"brp\":%u,\"tseg1\":%u,\"tseg2\":%u,"
                       "\"sjw\":%u,\"nbt\":%u,\"bitrate\":%u,\"err\":%d,"
                       "\"sp_out\":%d,\"tol\":%d,\"nbtp\":%u}",
                       clocks[c], fd_rates[r], points[p],
                       t.valid ? "true" : "false", t.brp, t.tseg1, t.tseg2,
                       t.sjw, t.nominal_bt_tq, t.bitrate_bps,
                       t.bitrate_error_ppm, t.sample_point_permil,
                       can_timing_max_tolerance_ppm(&t),
                       can_timing_nbtp(&t));
            }
        }
    }
    printf("]\n");
    return 0;
}
"""


@pytest.fixture(scope="module")
def c_results(tmp_path_factory):
    tmp = tmp_path_factory.mktemp("crosscheck")
    src = tmp / "dump.c"
    src.write_text(HARNESS)
    exe = tmp / "dump"

    subprocess.run(
        ["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
         f"-I{FIRMWARE / 'core' / 'inc'}", "-o", str(exe), str(src),
         str(FIRMWARE / "core" / "src" / "can_timing.c")],
        check=True, capture_output=True,
    )

    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
    return json.loads(out.stdout)


def _case_id(c):
    return (f"{c['clock'] // 1000000}MHz/{c['rate'] // 1000}k/"
            f"sp{c['sp']}{'/fd' if c['fd'] else ''}")


def test_the_harness_produced_a_real_grid(c_results):
    assert len(c_results) == 4 * 5 * (5 + 4)
    assert any(c["valid"] for c in c_results)


def test_bitrate_and_validity_agree(c_results):
    """The two things a disagreement would be unambiguously fatal about."""
    mismatches = []
    for c in c_results:
        p = py.solve(c["clock"], c["rate"], c["sp"], c["fd"])
        if p.valid != c["valid"]:
            mismatches.append(f"{_case_id(c)}: C valid={c['valid']} "
                              f"Python valid={p.valid}")
        elif c["valid"] and p.bitrate_bps != c["bitrate"]:
            mismatches.append(f"{_case_id(c)}: C {c['bitrate']} bit/s, "
                              f"Python {p.bitrate_bps} bit/s")
    assert not mismatches, "\n".join(mismatches)


def _as_timing(c) -> py.Timing:
    """Rebuild the C solver's answer as a Timing so it can be scored by the
    shared ranking function.  Nothing is recomputed here -- these are the
    fields the C code produced."""
    return py.Timing(
        valid=c["valid"], brp=c["brp"], tseg1=c["tseg1"], tseg2=c["tseg2"],
        sjw=c["sjw"], nominal_bt_tq=c["nbt"], bitrate_bps=c["bitrate"],
        bitrate_error_ppm=abs(c["err"]), sample_point_permil=c["sp_out"],
    )


def test_the_local_search_never_loses_to_the_exhaustive_one(c_results):
    """The property that actually matters.

    The C solver does a local search -- one nbt per prescaler, and the ideal
    tseg1 plus its two neighbours.  The Python one enumerates every legal
    combination.  A local search is allowed to take a different route; it is
    not allowed to arrive somewhere worse.

    Scored by the shared ranking in can_timing.score(), so this asserts the
    heuristic is sound rather than that the two implementations happen to
    match.
    """
    worse = []
    for c in c_results:
        if not c["valid"]:
            continue
        p = py.solve(c["clock"], c["rate"], c["sp"], c["fd"])
        c_key = py.score(_as_timing(c), c["sp"])
        p_key = py.score(p, c["sp"])
        if c_key > p_key:
            worse.append(
                f"{_case_id(c)}: C chose brp={c['brp']} tseg1={c['tseg1']} "
                f"tseg2={c['tseg2']} (SP {c['sp_out']}, err {c['err']} ppm, "
                f"tol {c['tol']}) but brp={p.brp} tseg1={p.tseg1} "
                f"tseg2={p.tseg2} (SP {p.sample_point_permil}, "
                f"err {p.bitrate_error_ppm} ppm, "
                f"tol {py.max_tolerance_ppm(p)}) ranks better")
    assert not worse, "the C local search missed a better answer:\n" + \
        "\n".join(worse)


def test_register_fields_are_identical_where_the_rate_is_exact(c_results):
    """On any operating point the board would actually use, identical.

    Where the bit rate divides exactly there is one correct number of time
    quanta per prescaler, so both searches see the same candidate set and must
    reach the same answer -- which is what lets the Python tool be used to
    check a board's registers against what the firmware programmed.

    Where the rate does not divide exactly the two explore different
    candidates, and the previous test covers the part that matters: neither
    may be worse.
    """
    mismatches = []
    for c in c_results:
        if not c["valid"] or c["err"] != 0:
            continue
        p = py.solve(c["clock"], c["rate"], c["sp"], c["fd"])
        got = (p.brp, p.tseg1, p.tseg2, p.sjw, p.sample_point_permil,
               py.max_tolerance_ppm(p))
        want = (c["brp"], c["tseg1"], c["tseg2"], c["sjw"], c["sp_out"],
                c["tol"])
        if got != want:
            mismatches.append(
                f"{_case_id(c)}: C brp/tseg1/tseg2/sjw/SP/tol={want}, "
                f"Python={got}")
    assert not mismatches, "\n".join(mismatches)


def test_nbtp_encoding_agrees(c_results):
    """The register word itself, which is what actually reaches the silicon."""
    mismatches = []
    for c in c_results:
        if not c["valid"] or c["fd"] or c["err"] != 0:
            continue
        p = py.solve(c["clock"], c["rate"], c["sp"], c["fd"])
        if p.register_nbtp != c["nbtp"]:
            mismatches.append(f"{_case_id(c)}: NBTP C 0x{c['nbtp']:08X} "
                              f"vs Python 0x{p.register_nbtp:08X}")
    assert not mismatches, "\n".join(mismatches)


def test_the_crystal_conclusion_is_the_same_in_both(c_results):
    """Section 7.2's claim, reached independently twice."""
    for c in c_results:
        if not c["valid"] or c["fd"] or c["err"] != 0:
            continue
        p = py.solve(c["clock"], c["rate"], c["sp"], c["fd"])
        assert py.osc_ok(p, py.OSC_PPM_HSI16) == (c["tol"] >= py.OSC_PPM_HSI16)
        assert py.osc_ok(p, py.OSC_PPM_CRYSTAL_30) == (c["tol"] >= 30)


def test_the_boards_own_operating_points_are_bit_exact(c_results):
    """The four configurations this board will actually run, pinned.

    These are the numbers that end up in NBTP and DBTP on real silicon, so
    they are asserted as literals: a change to the solver that moves any of
    them is a change to what goes on the bus, and it should require editing
    this list deliberately rather than watching a test go green."""
    expected = {
        (170_000_000, 125_000, 875, False):
            (5, 237, 34, 34, 272, 875, 0),
        (170_000_000, 250_000, 875, False):
            (5, 118, 17, 17, 136, 875, 0),
        (170_000_000, 500_000, 875, False):
            (2, 148, 21, 21, 170, 876, 0),
        (170_000_000, 1_000_000, 875, False):
            (1, 148, 21, 21, 170, 876, 0),
    }
    by_case = {(c["clock"], c["rate"], c["sp"], c["fd"]): c for c in c_results}

    for key, want in expected.items():
        c = by_case[key]
        got = (c["brp"], c["tseg1"], c["tseg2"], c["sjw"], c["nbt"],
               c["sp_out"], c["err"])
        assert got == want, f"{_case_id(c)}: C gave {got}, expected {want}"

        p = py.solve(*key)
        assert (p.brp, p.tseg1, p.tseg2, p.sjw, p.nominal_bt_tq,
                p.sample_point_permil, p.bitrate_error_ppm) == want


def test_hsi16_fails_at_every_standard_rate_on_this_board():
    """The design document's headline claim, stated as a property rather than
    as one example: there is no standard bit rate at which skipping the
    crystal is acceptable."""
    for rate in (125_000, 250_000, 500_000, 800_000, 1_000_000):
        t = py.solve(170_000_000, rate, 875, False)
        assert t.valid
        assert not py.osc_ok(t, py.OSC_PPM_HSI16), (
            f"HSI16 unexpectedly passes at {rate} bit/s")
        assert py.osc_ok(t, py.OSC_PPM_CRYSTAL_30), (
            f"a 30 ppm crystal should pass at {rate} bit/s")
