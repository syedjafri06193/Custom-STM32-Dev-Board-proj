"""The design document's rules, as tests against board.yaml.

Section 14 calls schematic review "the highest-ROI milestone in the project",
and section 2.2 lists the things that will destroy hardware.  Both are
checklists, and checklists get skimmed.  Every rule here is one that a board
can violate silently: nothing about a 470 uF bulk cap connected directly to
VBUS looks wrong in a schematic, and nothing about a missing reverse isolator
channel looks wrong until layout is finished.

These run in CI.  A change to the board that breaks one of them fails the
build, which is the only version of a design rule that survives contact with a
deadline.
"""

from __future__ import annotations

import pathlib

import pytest
import yaml

BOARD_PATH = pathlib.Path(__file__).resolve().parents[1] / "board.yaml"


@pytest.fixture(scope="module")
def board():
    with BOARD_PATH.open() as fh:
        return yaml.safe_load(fh)


# ---------------------------------------------------------------- isolation


def test_every_barrier_crossing_goes_through_an_isolating_part(board):
    """Section 3: nothing crosses the barrier except the isolator parts and
    the transformer.  No traces, no pours, no thermal reliefs, no fiducials.

    A net listed as crossing without an isolating part is a copper bridge, and
    a copper bridge makes the isolation rating on the silkscreen a lie."""
    allowed = {"ADuM4154", "isolated_dcdc", "y_capacitor"}
    for crossing in board["barrier_crossings"]:
        assert crossing["via"] in allowed, (
            f"{crossing['net']} crosses the barrier via {crossing['via']}, "
            "which is not an isolating part"
        )


def test_isolator_direction_count_matches_the_signal_chain(board):
    """Section 4.3: count your isolator directions before you pick the part.

    SPI across an isolator is not four channels in one direction.  SCK, MOSI
    and CS go out; MISO and DRDY come back.  That is 3 forward and 2 reverse,
    and a generic quad isolator -- or a 4-channel part split 3/1 or 2/2 --
    cannot do it.  Discovering this after layout means a new part, a new
    footprint and a re-route.

    The requirement is derived from the signal chain, not read back out of the
    part's own spec.  Checking the declared channel count against the declared
    crossing list would only prove the two fields agree, and somebody swapping
    in a 3/1 part would update both together and sail straight through."""
    iso = board["afe"]["isolator"]
    crossings = {c["net"]: c for c in board["barrier_crossings"]}

    # What an SPI ADC with a data-ready line needs, regardless of which part
    # ends up in the footprint.
    required_forward = {"SPI_SCK", "SPI_MOSI", "SPI_CS"}
    required_reverse = {"SPI_MISO", "ADC_DRDY"}

    for net in required_forward | required_reverse:
        assert net in crossings, (
            f"{net} has to cross the barrier for the ADC to work, and it is "
            "not in the crossing list"
        )

    for net in required_forward:
        assert crossings[net]["direction"] == "to_isolated", net
    for net in required_reverse:
        assert crossings[net]["direction"] == "from_isolated", net

    part = board["afe"]["isolator"]["part"]
    isolated_nets = {n for n, c in crossings.items() if c["via"] == part}
    assert (required_forward | required_reverse) <= isolated_nets, (
        f"some signal nets do not go through {part}"
    )

    assert iso["forward_channels"] >= len(required_forward), (
        f"the signal chain needs {len(required_forward)} forward channels; "
        f"{iso['part']} provides {iso['forward_channels']}"
    )
    assert iso["reverse_channels"] >= len(required_reverse), (
        f"the signal chain needs {len(required_reverse)} reverse channels; "
        f"{iso['part']} provides {iso['reverse_channels']}. A 3/1 split does "
        "not work: MISO and DRDY both come back."
    )


def test_creepage_matches_the_declared_isolation_class(board):
    """Section 3: the class determines the floorplan, so the two must agree.

    Claiming reinforced isolation with 2.5 mm of creepage is worse than
    claiming functional isolation with 2.5 mm, because somebody will trust it.
    """
    iso = board["isolation"]
    minimums = {"functional": 1.0, "basic": 5.0, "reinforced": 8.0}
    needed = minimums[iso["class"]]
    assert iso["creepage_mm"] >= needed, (
        f"{iso['class']} isolation needs >= {needed} mm, board has "
        f"{iso['creepage_mm']} mm"
    )
    if iso["class"] == "reinforced":
        assert iso["routed_slot"], "reinforced isolation needs a routed slot"


def test_isolator_footprint_allows_a_reinforced_revision(board):
    """Section 3 again: use wide-body footprints from the start.  They accept
    narrow parts; the reverse is not true.  This is a free option on v1 and an
    expensive re-layout if v2 needs it and v1 did not take it."""
    assert board["isolation"]["isolator_footprint"] == "wide_body_soic"


def test_y_capacitor_is_present_and_placed(board):
    """Section 3: a Y-capacitor across the barrier substantially reduces the
    common-mode current the isolated DC-DC pushes through the measurement.
    Section 8.4 names that current as one of the four coupling paths."""
    y = board["isolation"]["y_capacitor"]
    assert y["present"]
    assert 100 <= y["value_pf"] <= 1000, "100 pF - 1 nF is the useful range"
    assert "transformer" in y["placement"], (
        "a Y-cap far from the transformer gives the common-mode current a "
        "long loop, which is the thing it was supposed to prevent"
    )


# ----------------------------------------------------------- USB-C and PD


def test_precontract_capacitance_is_within_the_type_c_bound(board):
    """Section 6.3, and the one that actually destroys the user experience.

    A source charging 470 uF through a 5 V -> 20 V transition sees an inrush
    spike, declares a fault and shuts down.  The board then oscillates between
    negotiating and browning out, which reads as "PD doesn't work" rather than
    as a capacitance problem."""
    p = board["power"]
    assert p["precontract_capacitance_uf"] <= 10, (
        "Type-C bounds sink bypass capacitance to about 10 uF before a "
        "contract exists"
    )
    assert p["bulk_capacitance_uf"] > p["precontract_capacitance_uf"]
    assert "NMOS" in p["bulk_switch"] or "load switch" in p["bulk_switch"], (
        "the bulk has to be gated by something"
    )
    assert "slew" in p["bulk_switch"], (
        "a hard-switched gate charges the bulk as a step, which is the inrush "
        "this rule exists to avoid"
    )


def test_pd_never_leaves_spr(board):
    """Section 6.1, and the firmware enforces the same thing in pd_policy.c.

    A sink only ever sees more than 20 V if it asks.  Staying in SPR is what
    lets every part on the input side be a 40 V part instead of a 60 V part."""
    pd = board["usb_c"]["pd"]
    assert pd["spr_only"] is True
    assert pd["epr_requested"] is False
    assert pd["max_request_mv"] <= 20000
    assert pd["fallback_mv"] == 5000, (
        "section 6.5: a dev board that only works with a PD charger is a dev "
        "board that does not work at the moment you need it"
    )


def test_input_stage_is_rated_above_the_worst_case(board):
    """Section 10: 20 V nominal plus transients.  A 25 V part on a 20 V rail
    with an inductive cable is a part that dies on the bench and takes a day
    to diagnose because it works fine at 12 V."""
    p = board["power"]["input"]
    assert p["input_rating_v"] >= 2 * (p["max_mv"] / 1000), (
        "rate the input stage for at least twice the maximum negotiated "
        "voltage; 40 V for a 20 V SPR ceiling"
    )
    assert p["tvs_standoff_v"] > p["max_mv"] / 1000, (
        "a TVS that clamps at or below the negotiated voltage conducts "
        "continuously and cooks itself"
    )


def test_dead_battery_rd_does_not_depend_on_the_mcu(board):
    """Section 6.2, the bricking trap, and the single most important rule on
    this board.

    If the CC pull-downs come from the MCU, then a board with no other power
    source presents no Rd, the source never supplies VBUS, the MCU never
    boots, and the board is unrecoverable without lifting a part.  The Rd has
    to exist while the MCU is dark."""
    u = board["usb_c"]
    assert u["dead_battery_rd"] in ("external_tcpp01", "external_resistors"), (
        "dead-battery Rd must not come from the MCU"
    )
    assert u["cc_protection"], "CC lines need overvoltage protection"
    assert u["cc_overvoltage_v"] >= 22, (
        "section 6.4: a shorted cable can put VBUS on CC, and the CC pins are "
        "not 20 V tolerant"
    )


# ------------------------------------------------------------------- power


def test_every_rail_has_a_jumper_and_a_test_point(board):
    """Section 11.1 and 11.2.  The jumper lets you break the rail to measure
    current or inject bench power; the test point is the alternative to a
    bodge wire on a 0.5 mm pitch part."""
    for rail in board["power"]["rails"]:
        assert rail["jumper"], f"{rail['name']} has no series jumper"
        assert rail["test_point"], f"{rail['name']} has no test point"


def test_every_regulated_rail_has_an_led(board):
    """Section 11.3: when you are staring at a dead board, knowing which rails
    came up saves an hour.  Costs microamps through a high-value resistor."""
    for rail in board["power"]["rails"]:
        if rail["name"] == "VBUS":
            continue  # not a regulated rail; 5V_MAIN's LED covers the path
        assert rail["led"], f"{rail['name']} has no indicator LED"


def test_analog_and_digital_rails_come_from_separate_regulators(board):
    """Section 5.3, stated there as an explicit warning: do not just
    ferrite-bead a shared rail and call it separated.  A 24-bit ADC's
    reference sharing a rail with a CAN transceiver's 70 mA switching burst is
    how you get 12 noise-free bits from a 24-bit part."""
    p = board["power"]
    assert p["analog_digital_separation"]["method"] == "separate_ldos"
    assert p["analog_digital_separation"]["star_point"]

    analog = next(r for r in p["rails"] if r["name"] == "3V3_A")
    digital = next(r for r in p["rails"] if r["name"] == "3V3_D")
    assert analog["source"] != digital["source"]
    assert p["vdda_filter"], "VDDA needs its own filtering"


def test_power_budget_adds_up_and_fits_the_input(board):
    """Section 5.2: build this table before choosing the buck, not after."""
    rails = {r["name"]: r for r in board["power"]["rails"]}

    downstream_peak = sum(
        r["peak_ma"] * r["voltage_mv"]
        for name, r in rails.items()
        if name in ("3V3_D", "3V3_A", "5V_ISO")
    )
    supply_peak = rails["5V_MAIN"]["peak_ma"] * rails["5V_MAIN"]["voltage_mv"]

    assert supply_peak >= downstream_peak, (
        f"5V_MAIN budgeted at {supply_peak / 1e6:.2f} W but downstream rails "
        f"want {downstream_peak / 1e6:.2f} W before conversion losses"
    )

    # And the honest total, which is where section 5.2's real point lands.
    board_w = supply_peak / 1e6
    loop = board["power"]["loop_supply"]
    loop_w = (loop["channels"] * loop["current_per_channel_ma"]
              * loop["voltage_mv"]) / 1e6

    assert board_w < 2.0, (
        "the board's own consumption is under 2 W, which means PD is not "
        "justified by the board alone"
    )
    assert loop["present"] and loop_w > board_w, (
        "section 5.2: if PD is on this board it should be powering something. "
        "The 4-20 mA loop supply is that something, and it has to be in the "
        "budget explicitly or the buck gets sized for the wrong load"
    )


def test_isolated_supply_is_post_regulated(board):
    """Section 8.3, stated there as an unconditional: either way, post-
    regulate with an LDO.  Feeding a 24-bit ADC directly from a switching
    converter is how you get 12 noise-free bits from it."""
    assert board["afe"]["isolated_supply"]["post_regulated"] is True


# --------------------------------------------------------------------- CAN


def test_can_requires_a_crystal(board):
    """Section 7.2.  can_timing.c proves the tolerance requirement from the
    bit timing; this asserts the board actually has the part that meets it."""
    osc = board["mcu"]["oscillator"]
    assert osc["type"] == "crystal", (
        "the HSI16's +/-1% does not fit inside CAN's tolerance at any "
        "standard bit rate"
    )
    assert osc["tolerance_ppm"] <= 50
    assert "C_stray" in osc["load_caps_note"], (
        "guessing the load caps causes startup failures and frequency error"
    )


def test_can_termination_is_split(board):
    """Section 7.3: two 60 ohm resistors with a cap to ground at the midpoint.
    Same 120 ohm differential termination, plus a common-mode termination
    path, for one extra resistor and one capacitor."""
    t = board["can"]["termination"]
    assert t["type"] == "split"
    assert sum(t["resistors_ohm"]) == 120
    assert t["midpoint_cap_nf"] > 0
    assert t["jumper"], "section 11.1: the termination has to be removable"


def test_can_has_protection_and_a_differential_pair(board):
    """Section 7.4.  Asymmetry converts differential to common mode, which
    radiates; the TVS belongs at the connector, not at the transceiver."""
    assert board["can"]["differential_pair"]
    assert "TVS" in board["can"]["protection"]
    assert "connector" in board["can"]["protection"]


# --------------------------------------------------------------------- AFE


def test_shunt_lands_inside_the_adc_reference(board):
    """Section 8.2.  4-20 mA through 100 ohm is 0.4-2.0 V, which sits inside
    the ADS1220's 2.048 V internal reference at PGA = 1 with headroom.  The
    firmware's afe.c asserts the same thing from the other direction."""
    afe = board["afe"]
    full_scale_mv = 20 * afe["shunt"]["resistance_ohm"]  # 20 mA * R
    assert full_scale_mv < afe["vref_mv"], (
        f"20 mA through {afe['shunt']['resistance_ohm']} ohm is "
        f"{full_scale_mv} mV, which does not fit under a "
        f"{afe['vref_mv']} mV reference"
    )
    assert full_scale_mv > afe["vref_mv"] * 0.8, (
        "a shunt that only uses a fraction of the reference throws away "
        "resolution for nothing"
    )


def test_shunt_tolerance_is_the_measurement_accuracy(board):
    """Section 8.2: this resistor *is* your measurement accuracy.  0.1% and
    25 ppm/C or better, and the tempco matters as much as the tolerance once
    the board warms up."""
    s = board["afe"]["shunt"]
    assert s["tolerance_ppm"] <= 1000, "0.1% or better"
    assert s["tempco_ppm_per_c"] <= 25


def test_shunt_package_can_dissipate_full_scale(board):
    s = board["afe"]["shunt"]
    computed_mw = (0.020 ** 2) * s["resistance_ohm"] * 1000
    assert abs(computed_mw - s["dissipation_mw_at_20ma"]) < 1.0
    limits_mw = {"0402": 62.5, "0603": 100.0, "0805": 125.0, "1206": 250.0}
    assert computed_mw < limits_mw[s["package"]] * 0.5, (
        "derate to half the package rating; a shunt running at its limit "
        "drifts, and drift in this part is measurement error"
    )


def test_input_protection_survives_a_continuous_fault(board):
    """Section 10: size sensor-input protection for a continuous fault, not a
    transient.  A TVS rated for an 8/20 us surge will happily die if somebody
    leaves 24 V connected for an hour, and somebody will."""
    p = board["afe"]["protection"]
    assert p["ptc"] and p["tvs"]
    assert p["continuous_fault_v"] >= 24

    # The series resistor has to do most of the work; the clamp only handles
    # what is left.  At 24 V through 1 k that is 24 mA, which a clamp can sink
    # indefinitely.  Through the 100 ohm shunt alone it would be 240 mA.
    fault_ma = p["continuous_fault_v"] * 1000 / p["series_resistor_ohm"]
    assert fault_ma <= 30, (
        f"a continuous {p['continuous_fault_v']} V fault would push "
        f"{fault_ma:.0f} mA through the clamp; the series resistance is not "
        "doing enough of the work"
    )


def test_noise_expectation_is_a_number_not_a_mood(board):
    """Section 8.4: a competently executed board gets 18-19 noise-free bits,
    not 24.  Writing the expectation down is what makes the measured number in
    characterization.md mean something."""
    n = board["afe"]["expected_noise_free_bits"]
    assert 16 <= n["bench_power"] <= 21, (
        "expecting more than about 21 noise-free bits from a 24-bit part on a "
        "real board is the mistake section 8.4 exists to prevent"
    )
    assert n["isolated_dcdc"] < n["bench_power"], (
        "the isolated converter costs bits; that cost is the measurement"
    )


# ------------------------------------------------------- design for bring-up


def test_the_minimum_test_point_set_is_present(board):
    """Section 11.2's list, enumerated.  A test point you didn't add is a
    bodge wire you will add."""
    required = {
        "VBUS", "5V_MAIN", "3V3_D", "3V3_A", "5V_ISO", "3V3_ISO",
        "CC1", "CC2", "BUCK_SW", "MCO", "CANH", "CANL", "CAN_TX", "CAN_RX",
        "ADC_VREF", "GND", "GND_ISO",
    }
    present = set(board["bring_up"]["test_points"])
    assert required <= present, f"missing test points: {sorted(required - present)}"


def test_every_spi_line_is_probeable_on_both_sides(board):
    """Section 11.2 asks for every SPI line on *both* sides of the isolator.

    This is the one that turns "the ADC doesn't answer" from a two-day
    mystery into a two-minute measurement: if the signal is present on the
    main side and absent on the isolated side, the isolator or its supply is
    the problem, and you know that without lifting anything."""
    present = set(board["bring_up"]["test_points"])
    for signal in ("SPI_SCK", "SPI_MOSI", "SPI_MISO", "SPI_CS"):
        assert f"{signal}_MAIN" in present, f"{signal} not probeable on the main side"
        assert f"{signal}_ISO" in present, f"{signal} not probeable on the isolated side"


def test_every_analog_input_has_a_test_point(board):
    present = set(board["bring_up"]["test_points"])
    for ch in range(board["afe"]["channels"]):
        assert f"AIN{ch}" in present


def test_mco_is_broken_out_rather_than_the_crystal_pins(board):
    """Section 11.2 and bring-up step 12.  Probing the crystal pins directly
    loads them and can stop oscillation, which looks exactly like a dead
    crystal.  MCO is the measurement that does not disturb what it measures."""
    present = set(board["bring_up"]["test_points"])
    assert "MCO" in present
    assert "OSC_IN" not in present and "OSC_OUT" not in present, (
        "do not invite anyone to probe the crystal pins"
    )


def test_barrier_jumper_is_labelled_as_defeating_isolation(board):
    """Section 11.1.  This jumper is what makes the section 16 noise
    measurement possible, and it is also the one that makes the isolation
    claim on the silkscreen untrue while it is fitted.  Both are true, so it
    has to say so on the board."""
    j = board["bring_up"]["barrier_power_jumper"]
    assert j["present"]
    assert "DEFEATS ISOLATION" in j["silkscreen"].upper()
    assert "BENCH" in j["silkscreen"].upper()


def test_debug_access_is_adequate_for_a_first_board(board):
    """Section 11.3: a real SWD header, not a Tag-Connect.  You will be
    plugging and unplugging it hundreds of times."""
    b = board["bring_up"]
    assert "2.54" in b["swd_header"], "Tag-Connect on a first board is a mistake"
    assert b["uart_broken_out"]
    assert b["boot0_jumper"]
    assert b["spare_gpio_header"], (
        "spare GPIO is how you debug the PD state machine on a scope"
    )


def test_every_led_names_the_rail_it_indicates(board):
    """A power LED wired to the wrong rail is worse than no LED: it tells you
    a rail came up when it did not."""
    rail_names = {r["name"] for r in board["power"]["rails"]}
    for led in board["bring_up"]["leds"]:
        assert led["rail"] in rail_names, f"{led['name']} indicates an unknown rail"

    indicated = {led["rail"] for led in board["bring_up"]["leds"]}
    for rail in board["power"]["rails"]:
        if rail["led"]:
            assert rail["name"] in indicated, (
                f"{rail['name']} claims an LED but no LED names it"
            )


# ----------------------------------------------------------------- stackup


def test_stackup_puts_a_plane_next_to_every_signal_layer(board):
    """Section 9.1.  Signal / ground / power / signal means every trace has a
    return path directly underneath it.  The alternative arrangements put a
    signal layer next to a signal layer, and the return current then goes
    looking for a path, which is the same thing as radiating."""
    s = board["stackup"]
    assert s["layers"] == 4
    order = s["order"]
    assert order == ["signal", "ground", "power", "signal"], (
        f"stackup {order} does not reference every signal layer to a plane"
    )


def test_controlled_impedance_is_not_paid_for_without_a_reason(board):
    """Section 9.2: USB full-speed at 12 Mbps over 40 mm does not need it.  It
    costs money, adds lead time, and constrains the stackup for no benefit."""
    assert board["stackup"]["controlled_impedance"] is False
    assert board["usb_c"]["data"]["controlled_impedance"] is False
    assert board["usb_c"]["data"]["series_resistors_ohm"] == 22, (
        "the reference design's series resistors are still worth the "
        "footprints even at full speed"
    )


@pytest.mark.xfail(
    reason="Section 9.1: ask the fab for their actual stackup. Different fabs "
           "ship wildly different dielectric spacing under the same "
           "'4-layer 1.6 mm' description. Flip this to True once confirmed, "
           "before ordering.",
    strict=True,
)
def test_fab_stackup_has_been_confirmed(board):
    assert board["stackup"]["fab_stackup_confirmed"] is True
