"""Mutation tests: prove each design rule actually fires.

A rule suite that has only ever seen a passing board is a suite that might be
asserting nothing.  Every rule in test_design_rules.py exists because a real
board could violate it, so each entry below breaks the board in exactly that
way and asserts the corresponding rule catches it.

This is the check on the checker.  It is cheap, it runs in the same second as
everything else, and it is the difference between "the design rules pass" and
"the design rules would notice."
"""

from __future__ import annotations

import copy
import pathlib

import pytest
import yaml

import test_design_rules as rules

BOARD_PATH = pathlib.Path(__file__).resolve().parents[1] / "board.yaml"


def _load():
    with BOARD_PATH.open() as fh:
        return yaml.safe_load(fh)


def _mutate(fn):
    b = copy.deepcopy(_load())
    fn(b)
    return b


# Each entry: (rule under test, a mutation that should break it).
# The mutations are written as things a real revision might plausibly do --
# swapping a part, moving a value, dropping a jumper to save a footprint --
# rather than as nonsense, because a rule that only catches nonsense is not
# protecting anything.

def _drop_reverse_channel(b):
    """Swap the ADuM4154 for a generic quad isolator, 3 forward + 1 reverse."""
    b["afe"]["isolator"]["reverse_channels"] = 1
    b["barrier_crossings"] = [
        c for c in b["barrier_crossings"] if c["net"] != "ADC_DRDY"
    ]


def _pick_a_3_1_isolator(b):
    """Keep every net, but choose a 4-channel part split 3 forward / 1
    reverse -- the exact mistake section 4.3 describes."""
    b["afe"]["isolator"]["part"] = "ADuM1401"
    b["afe"]["isolator"]["reverse_channels"] = 1
    for c in b["barrier_crossings"]:
        if c["via"] == "ADuM4154":
            c["via"] = "ADuM1401"


def _poll_instead_of_wiring_drdy(b):
    """Drop /DRDY and plan to poll, which needs one fewer reverse channel --
    and quietly makes the conversion-ready timing unobservable."""
    b["barrier_crossings"] = [
        c for c in b["barrier_crossings"] if c["net"] != "ADC_DRDY"
    ]


def _route_a_ground_across_the_barrier(b):
    b["barrier_crossings"].append(
        {"net": "GND_STITCH", "direction": "bidirectional", "via": "direct_trace"}
    )


def _claim_reinforced_at_functional_spacing(b):
    b["isolation"]["class"] = "reinforced"


def _hang_the_bulk_on_vbus(b):
    b["power"]["precontract_capacitance_uf"] = 470.0


def _hard_switch_the_bulk(b):
    b["power"]["bulk_switch"] = "NMOS, gate tied to a GPIO"


def _reach_for_epr(b):
    b["usb_c"]["pd"]["max_request_mv"] = 28000
    b["usb_c"]["pd"]["epr_requested"] = True


def _use_25v_parts(b):
    b["power"]["input"]["input_rating_v"] = 25


def _put_rd_on_the_mcu(b):
    b["usb_c"]["dead_battery_rd"] = "mcu_ucpd"


def _drop_a_rail_jumper(b):
    next(r for r in b["power"]["rails"] if r["name"] == "3V3_A")["jumper"] = False


def _ferrite_bead_the_analog_rail(b):
    b["power"]["analog_digital_separation"]["method"] = "ferrite_from_3v3d"
    next(r for r in b["power"]["rails"]
         if r["name"] == "3V3_A")["source"] = "LDO, digital"


def _skip_the_isolated_ldo(b):
    b["afe"]["isolated_supply"]["post_regulated"] = False


def _use_the_internal_oscillator(b):
    b["mcu"]["oscillator"]["type"] = "hsi16"
    b["mcu"]["oscillator"]["tolerance_ppm"] = 10000


def _use_a_single_120_ohm_terminator(b):
    b["can"]["termination"]["type"] = "single"
    b["can"]["termination"]["resistors_ohm"] = [120]


def _pick_a_10_ohm_shunt(b):
    b["afe"]["shunt"]["resistance_ohm"] = 10
    b["afe"]["shunt"]["dissipation_mw_at_20ma"] = 4


def _pick_a_1_percent_shunt(b):
    b["afe"]["shunt"]["tolerance_ppm"] = 10000


def _shrink_the_shunt_package(b):
    b["afe"]["shunt"]["package"] = "0402"


def _size_protection_for_transients_only(b):
    b["afe"]["protection"]["series_resistor_ohm"] = 100


def _expect_24_noise_free_bits(b):
    b["afe"]["expected_noise_free_bits"]["bench_power"] = 23.5


def _drop_the_isolated_side_spi_test_points(b):
    b["bring_up"]["test_points"] = [
        tp for tp in b["bring_up"]["test_points"] if not tp.endswith("_ISO")
    ] + ["3V3_ISO", "GND_ISO", "5V_ISO"]


def _probe_the_crystal_instead(b):
    tps = b["bring_up"]["test_points"]
    tps.remove("MCO")
    tps += ["OSC_IN", "OSC_OUT"]


def _soften_the_barrier_jumper_label(b):
    b["bring_up"]["barrier_power_jumper"]["silkscreen"] = "ISO BYPASS"


def _use_a_tag_connect(b):
    b["bring_up"]["swd_header"] = "Tag-Connect TC2030"


def _reorder_the_stackup(b):
    b["stackup"]["order"] = ["signal", "signal", "ground", "power"]


def _pay_for_controlled_impedance(b):
    b["stackup"]["controlled_impedance"] = True


def _move_the_y_cap(b):
    b["isolation"]["y_capacitor"]["placement"] = "near the connector"


def _narrow_the_isolator_footprint(b):
    b["isolation"]["isolator_footprint"] = "narrow_soic"


def _undersize_the_buck(b):
    next(r for r in b["power"]["rails"] if r["name"] == "5V_MAIN")["peak_ma"] = 80


def _drop_the_loop_supply(b):
    b["power"]["loop_supply"]["present"] = False


def _wire_an_led_to_a_nonexistent_rail(b):
    b["bring_up"]["leds"].append({"name": "LED_MYSTERY", "rail": "3V3_QUANTUM"})


def _drop_can_connector_protection(b):
    b["can"]["protection"] = "TVS array at the transceiver"


VIOLATIONS = [
    (rules.test_isolator_direction_count_matches_the_signal_chain, _drop_reverse_channel),
    (rules.test_isolator_direction_count_matches_the_signal_chain, _pick_a_3_1_isolator),
    (rules.test_isolator_direction_count_matches_the_signal_chain, _poll_instead_of_wiring_drdy),
    (rules.test_every_barrier_crossing_goes_through_an_isolating_part, _route_a_ground_across_the_barrier),
    (rules.test_creepage_matches_the_declared_isolation_class, _claim_reinforced_at_functional_spacing),
    (rules.test_isolator_footprint_allows_a_reinforced_revision, _narrow_the_isolator_footprint),
    (rules.test_y_capacitor_is_present_and_placed, _move_the_y_cap),
    (rules.test_precontract_capacitance_is_within_the_type_c_bound, _hang_the_bulk_on_vbus),
    (rules.test_precontract_capacitance_is_within_the_type_c_bound, _hard_switch_the_bulk),
    (rules.test_pd_never_leaves_spr, _reach_for_epr),
    (rules.test_input_stage_is_rated_above_the_worst_case, _use_25v_parts),
    (rules.test_dead_battery_rd_does_not_depend_on_the_mcu, _put_rd_on_the_mcu),
    (rules.test_every_rail_has_a_jumper_and_a_test_point, _drop_a_rail_jumper),
    (rules.test_analog_and_digital_rails_come_from_separate_regulators, _ferrite_bead_the_analog_rail),
    (rules.test_power_budget_adds_up_and_fits_the_input, _undersize_the_buck),
    (rules.test_power_budget_adds_up_and_fits_the_input, _drop_the_loop_supply),
    (rules.test_isolated_supply_is_post_regulated, _skip_the_isolated_ldo),
    (rules.test_can_requires_a_crystal, _use_the_internal_oscillator),
    (rules.test_can_termination_is_split, _use_a_single_120_ohm_terminator),
    (rules.test_can_has_protection_and_a_differential_pair, _drop_can_connector_protection),
    (rules.test_shunt_lands_inside_the_adc_reference, _pick_a_10_ohm_shunt),
    (rules.test_shunt_tolerance_is_the_measurement_accuracy, _pick_a_1_percent_shunt),
    (rules.test_shunt_package_can_dissipate_full_scale, _shrink_the_shunt_package),
    (rules.test_input_protection_survives_a_continuous_fault, _size_protection_for_transients_only),
    (rules.test_noise_expectation_is_a_number_not_a_mood, _expect_24_noise_free_bits),
    (rules.test_every_spi_line_is_probeable_on_both_sides, _drop_the_isolated_side_spi_test_points),
    (rules.test_mco_is_broken_out_rather_than_the_crystal_pins, _probe_the_crystal_instead),
    (rules.test_barrier_jumper_is_labelled_as_defeating_isolation, _soften_the_barrier_jumper_label),
    (rules.test_debug_access_is_adequate_for_a_first_board, _use_a_tag_connect),
    (rules.test_every_led_names_the_rail_it_indicates, _wire_an_led_to_a_nonexistent_rail),
    (rules.test_stackup_puts_a_plane_next_to_every_signal_layer, _reorder_the_stackup),
    (rules.test_controlled_impedance_is_not_paid_for_without_a_reason, _pay_for_controlled_impedance),
]


@pytest.mark.parametrize(
    "rule,mutation",
    VIOLATIONS,
    ids=[f"{r.__name__}::{m.__name__.lstrip('_')}" for r, m in VIOLATIONS],
)
def test_rule_catches_its_violation(rule, mutation):
    broken = _mutate(mutation)
    with pytest.raises(AssertionError):
        rule(broken)


def test_the_real_board_passes_every_rule():
    """The other direction: none of these fire on the board as designed."""
    good = _load()
    for rule, _ in VIOLATIONS:
        rule(good)


def test_every_rule_has_at_least_one_mutation():
    """A rule with no mutation behind it is a rule nobody has checked.

    The one exemption is the fab-stackup gate, which is an xfail on purpose --
    it is a reminder to confirm the stackup before ordering, not a property of
    the design."""
    covered = {rule.__name__ for rule, _ in VIOLATIONS}
    exempt = {"test_fab_stackup_has_been_confirmed",
              "test_every_regulated_rail_has_an_led",
              "test_every_analog_input_has_a_test_point",
              "test_the_minimum_test_point_set_is_present"}

    all_rules = {
        name for name in dir(rules)
        if name.startswith("test_") and callable(getattr(rules, name))
    }
    uncovered = all_rules - covered - exempt
    assert not uncovered, f"design rules with no mutation test: {sorted(uncovered)}"
