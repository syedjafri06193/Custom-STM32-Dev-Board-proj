"""Tests for the characterization tools.

The tools produce the numbers that go in the report, so an arithmetic error
here is an arithmetic error in the published characterization -- which is
worse than no characterization, because somebody will believe it.

Each test feeds in data whose answer is known analytically, so the assertion
is against the maths rather than against whatever the code happened to print
the first time it ran.
"""

from __future__ import annotations

import math
import pathlib
import random
import subprocess
import sys

import pytest

TOOLS = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import accuracy  # noqa: E402
import noise_floor  # noqa: E402
import pd_matrix  # noqa: E402


# --------------------------------------------------------------- noise floor


def _synthetic(rms_codes: float, n: int = 4000, seed: int = 1) -> list[int]:
    rng = random.Random(seed)
    return [round(rng.gauss(0.0, rms_codes)) for _ in range(n)]


def test_noise_free_bits_matches_the_closed_form():
    """Gaussian noise of a known RMS has a known answer, so the tool can be
    checked against the formula rather than against itself."""
    rms_codes = 200.0
    codes = _synthetic(rms_codes)
    r = noise_floor.Result(codes, noise_floor.DEFAULT_VREF_MV, 1, "synthetic")

    rms_v = rms_codes * (noise_floor.DEFAULT_VREF_MV / 1000) / \
        noise_floor.FULL_SCALE_COUNTS
    full_scale_v = 2 * noise_floor.DEFAULT_VREF_MV / 1000
    expected = math.log2(full_scale_v / (6.6 * rms_v))

    assert r.noise_free_bits == pytest.approx(expected, abs=0.05)


def test_halving_the_noise_buys_exactly_one_bit():
    a = noise_floor.Result(_synthetic(200.0, seed=2),
                           noise_floor.DEFAULT_VREF_MV, 1, "a")
    b = noise_floor.Result(_synthetic(100.0, seed=3),
                           noise_floor.DEFAULT_VREF_MV, 1, "b")
    assert b.noise_free_bits - a.noise_free_bits == pytest.approx(1.0, abs=0.05)


def test_effective_bits_is_always_2_72_higher():
    """log2(6.6) = 2.7224. The two conventions differ by exactly that, and
    quoting one while calling it the other is how a 24-bit part gets claimed
    to deliver 21 useful bits."""
    r = noise_floor.Result(_synthetic(150.0, seed=4),
                           noise_floor.DEFAULT_VREF_MV, 1, "x")
    assert r.effective_bits - r.noise_free_bits == pytest.approx(
        math.log2(6.6), abs=1e-9)


def test_the_firmware_and_the_tool_agree_on_noise_free_bits():
    """afe_noise_free_millibits() runs on the target with no floating point;
    this runs on a host with numpy-grade arithmetic.  They have to produce the
    same number or the board's own report disagrees with the analysis of the
    samples it produced."""
    src = TOOLS.parents[0] / "firmware"
    harness = r"""
#include <stdio.h>
#include "afe.h"
int main(void) {
    const int32_t rms[] = {1, 2, 3, 5, 10, 25, 100, 500, 2000};
    for (unsigned i = 0; i < sizeof(rms)/sizeof(*rms); i++)
        printf("%d %d\n", rms[i], afe_noise_free_millibits(4096000, rms[i]));
    return 0;
}
"""
    import tempfile
    with tempfile.TemporaryDirectory() as td:
        d = pathlib.Path(td)
        (d / "h.c").write_text(harness)
        subprocess.run(
            ["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
             f"-I{src / 'core' / 'inc'}", "-o", str(d / "h"), str(d / "h.c"),
             str(src / "core" / "src" / "afe.c")],
            check=True, capture_output=True)
        out = subprocess.run([str(d / "h")], check=True, capture_output=True,
                             text=True).stdout

    for line in out.strip().splitlines():
        rms_uv, millibits = (int(x) for x in line.split())
        expected = math.log2(4.096 / (6.6 * rms_uv / 1e6)) * 1000
        assert abs(millibits - expected) <= 2, (
            f"rms {rms_uv} uV: firmware says {millibits} millibits, "
            f"the formula says {expected:.0f}")


def test_too_few_samples_is_an_error_not_a_guess():
    with pytest.raises(ValueError):
        noise_floor.Result([1, 2, 3], noise_floor.DEFAULT_VREF_MV, 1, "tiny")


# ------------------------------------------------------------------ accuracy


def test_a_perfect_sweep_reports_no_error():
    points = [(float(ua), float(ua)) for ua in range(4000, 20001, 1000)]
    r = accuracy.analyse(points)
    assert r["gain_error_ppm"] == pytest.approx(0, abs=1)
    assert r["offset_ua"] == pytest.approx(0, abs=1e-6)
    assert r["inl_ua"] == pytest.approx(0, abs=1e-6)


def test_gain_and_offset_are_recovered_separately():
    """The point of reporting them separately: one of them is free to fix in
    firmware and the other tells you the reference is wrong."""
    gain, offset = 1.002, 12.0
    points = [(float(ua), gain * ua + offset)
              for ua in range(4000, 20001, 500)]
    r = accuracy.analyse(points)
    assert r["gain_error_ppm"] == pytest.approx(2000, rel=0.01)
    assert r["offset_ua"] == pytest.approx(offset, abs=0.01)
    assert r["inl_ua"] == pytest.approx(0, abs=0.01)


def test_inl_survives_a_two_point_calibration():
    """A pure gain-and-offset error calibrates to zero; a bow in the curve
    does not.  That difference is the whole reason the report separates
    them -- it is the difference between a config change and a respin."""
    points = []
    for ua in range(4000, 20001, 500):
        # A half-microamp bow across the span, on top of gain and offset.
        frac = (ua - 4000) / 16000
        bow = 0.5 * math.sin(math.pi * frac)
        points.append((float(ua), 1.001 * ua + 8.0 + bow))

    r = accuracy.analyse(points)
    assert r["gain_error_ppm"] == pytest.approx(1000, rel=0.05)
    assert abs(r["inl_ua"]) > 0.1, "the bow should show up as INL"
    assert r["residual_after_cal_ua"] == pytest.approx(abs(r["inl_ua"]),
                                                       rel=0.01)


def test_least_squares_is_not_fooled_by_endpoint_noise():
    """Endpoint fitting would take a noisy 4 mA reading as gospel.  Best-fit
    uses every point, which is why it is the convention here."""
    points = [(float(ua), float(ua)) for ua in range(4000, 20001, 500)]
    points[0] = (4000.0, 4005.0)  # one bad endpoint
    r = accuracy.analyse(points)
    assert abs(r["gain_error_ppm"]) < 500
    assert abs(r["offset_ua"]) < 2.0


# ----------------------------------------------------------------- PD matrix


def test_epr_pdos_are_never_selected():
    """The invariant, checked against every source in the catalogue."""
    for name, pdos in pd_matrix.CATALOGUE:
        r = pd_matrix.select(pdos)
        assert not r["ok"] or r["mv"] <= pd_matrix.SPR_MAX_MV, (
            f"{name} produced a {r['mv']} mV contract")


def test_the_epr_chargers_still_power_the_board():
    """Ignoring the EPR PDOs must not mean refusing the charger.  A 240 W
    brick still offers 20 V, and taking it is the right answer."""
    for name, pdos in pd_matrix.CATALOGUE:
        if not any(mv > pd_matrix.SPR_MAX_MV for mv, _ in pdos):
            continue
        r = pd_matrix.select(pdos)
        assert r["ok"], f"{name} was refused entirely"
        assert r["mv"] == 20000
        assert r["ignored_epr"], "the EPR PDOs should be recorded as ignored"


def test_dumb_five_volt_sources_still_bring_the_board_up():
    """Section 6.5, as a property of the whole catalogue: every source that
    offers 5 V at all produces a contract."""
    for name, pdos in pd_matrix.CATALOGUE:
        if not any(mv == 5000 for mv, _ in pdos):
            continue
        r = pd_matrix.select(pdos)
        assert r["ok"], f"{name} offers 5 V but was refused"


def test_out_of_spec_and_unusable_are_different_things():
    """A source with no 5 V PDO is out of spec -- 5 V is mandatory for a USB-C
    source -- but that does not make it unusable.  If it offers 12 V at enough
    current, the board runs on it, and refusing on principle would be the
    policy choosing pedantry over a working board.

    The only genuine failure is a source that offers neither 5 V nor enough
    current at anything else."""
    failures = [name for name, pdos in pd_matrix.CATALOGUE
                if not pd_matrix.select(pdos)["ok"]]
    assert failures == ["Overloaded 12 V port"]

    non_standard = pd_matrix.select([(12000, 3000)])
    assert non_standard["ok"] and non_standard["mv"] == 12000
    assert non_standard["mismatch"], "12 V is not the 20 V the loops want"


def test_reduced_capability_is_flagged_rather_than_hidden():
    r = pd_matrix.select([(5000, 3000), (9000, 2000)])
    assert r["ok"] and r["mismatch"], (
        "9 V is a contract, but not the 20 V the loop supply wants, and the "
        "RDO has to say so")

    r = pd_matrix.select([(5000, 3000), (9000, 3000), (15000, 3000),
                          (20000, 3000)])
    assert r["ok"] and not r["mismatch"]


def test_low_current_sources_fall_back_rather_than_fail():
    """A 5 V/500 mA legacy port cannot run the board at full tilt.  Falling
    back with the mismatch bit set is a board that boots and says so; refusing
    is a board that looks dead."""
    r = pd_matrix.select([(5000, 500)])
    assert r["ok"] and r["mismatch"] and r["mv"] == 5000


# ------------------------------------------------------------ the CLIs run


@pytest.mark.parametrize("argv", [
    ["can_timing.py", "--clock", "170000000", "--bitrate", "500000"],
    ["can_timing.py", "--table"],
    ["pd_matrix.py", "--check-invariant"],
    ["pd_matrix.py", "--markdown"],
    ["power_budget.py"],
    ["power_budget.py", "--input-mv", "5000"],
])
def test_cli_runs_clean(argv):
    """A tool that crashes on its own default arguments is a tool nobody will
    reach for during bring-up, which is the one time it matters."""
    out = subprocess.run([sys.executable, str(TOOLS / argv[0])] + argv[1:],
                         capture_output=True, text=True)
    assert out.returncode == 0, out.stderr
    assert out.stdout.strip()
