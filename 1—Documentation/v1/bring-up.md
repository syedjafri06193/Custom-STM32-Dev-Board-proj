# Bring-up procedure

Section 15 of the design document, with the firmware's part filled in.

> **Never plug a new board into USB first.** Follow this order. Each stage
> exists because a failure in it makes the next stage's result meaningless.

The self-test in `app/src/main.c` automates stages 2–5 and stops at the first
failure. Everything before stage 2, and anything needing a second node or a
bench source, is yours.

---

## Stage 0 — before power

Nothing electrical. All four of these have caught boards.

1. **Inspect under magnification.** Solder bridges on the LQFP64 and the
   TSSOP ADS1220, tombstoned passives, missing parts, wrong orientations.
2. **Continuity-check every rail to ground.** There are six: VBUS, 5V_MAIN,
   3V3_D, 3V3_A, 5V_ISO, 3V3_ISO. Test points for all of them are on the
   board precisely so this takes two minutes.
3. **Measure isolation resistance across the barrier.** Should read open
   (>10 MΩ). Anything lower is a copper bridge, and it is far easier to find
   now than after the board is powered and the reading is ambiguous.
   **Remove the barrier power jumper first** — it is designed to short across
   the barrier, so with it fitted this test reads whatever the jumper reads.
4. **Check no rail is shorted to any other rail.**

## Stage 1 — power, from a bench supply

5. **Inject 5 V on VBUS from a bench supply, 100 mA current limit.** Not USB.
   If something is wrong the supply limits and nothing dies.
6. Watch the current. Tens of milliamps is expected —
   `make report` prints the budget, which says **58 mA at 20 V** or
   **~230 mA at 5 V** for the board alone. Hundreds of milliamps at 5 V with
   no loops connected means a fault: power down.
7. **Measure every rail** and check the five rail LEDs.
8. **Feel for hot parts.** The 3V3_D LDO dissipates 0.26 W at peak and will
   be warm; nothing else should be.
9. Raise to 12 V, then 20 V, checking rails and temperatures at each step.

## Stage 2 — MCU

Run by `stage_mcu()`.

10. **Connect the ST-Link and read the device ID.** If this works, the MCU is
    alive, powered and correctly wired — a large milestone.
11. **Flash and run the self-test.** The user LED blinks six times, then the
    console comes up at **115200 8N1 on PA2**.

    Legible console output is itself the first measurement: the baud divider
    is computed from `BOARD_PCLK1_HZ`, so if the PLL were not actually at
    170 MHz the text would be garbled.

12. **Verify the crystal on MCO (PA8), not on the crystal pins.** A scope
    probe on OSC_IN loads the resonator and can stop it oscillating, which
    looks exactly like a dead crystal. The firmware drives SYSCLK/16 on MCO
    and prints the expected frequency: **10.625 MHz**.

    Measure the error. CAN needs better than ~4800 ppm per node
    (`make report` gives the exact figure per bit rate); a 30 ppm crystal has
    two orders of magnitude of margin, so anything close to the limit means
    the load caps are wrong, not that the budget is tight.

**If the clock fails**, the firmware says which failure it was and blinks the
fault LED, because the three have different fixes:

| Message | Where to look |
|---|---|
| `HSE crystal did not start` | load caps (`CL = 2 × (C_load − C_stray)`), crystal part number, solder joints. The most common first-assembly failure, and not a firmware bug. |
| `PLL did not lock` | PLLCFGR values, or HSE running at a different frequency than `BOARD_HSE_HZ` says |
| `voltage scaling stuck` | VDD, and the PWR clock gate |

## Stage 3 — USB-C

Run by `stage_usb_c()`.

13. **First with a dumb 5 V source** — a USB-A to C cable from an old
    charger. The board should power up at 5 V. This validates the connector,
    the protection and the power path with no PD involved at all.

    The self-test prints both CC line states and the detected orientation.
    Try the cable both ways round; both should work and report different CC
    lines. A reversible connector that only works one way is a bug, not a
    quirk.

14. **Then a PD source**, with an inline PD analyzer if you have one.
15. **Scope VBUS during negotiation** to see the voltage transitions.
16. **Read the negotiated voltage** through the divider and confirm it
    matches what the analyzer says. The self-test prints it in millivolts.

    `make report` prints the expected result for fourteen different sources,
    so there is something to compare against rather than a bare number.

> **The bulk capacitance stays gated until a contract exists.** The self-test
> asserts this — with no PD protocol layer present there is no contract, so
> `bulk_switch_is_enabled()` must be false. If it is not, the 470 µF is on
> VBUS during negotiation and sources will trip.

## Stage 4 — CAN

Run by `stage_can()`.

17. **Internal loopback first — no transceiver.** The self-test puts the
    transceiver in standby and uses `CCCR.TEST + TEST.LBCK + CCCR.MON`, so
    nothing reaches the pins. This proves the peripheral and the bit timing
    with no bus, no transceiver and no second node — which is the only way to
    know which of those is broken when the real thing does not work.

    It sends a CAN FD frame with BRS set, so the 2 Mbit/s data phase actually
    runs, and checks the ID, payload, FD and BRS bits all survive.

18. **Loopback through the transceiver.** Take it out of standby and use
    `CAN_MODE_EXTERNAL_LOOPBACK` (`TEST + LBCK`, `MON` off). This catches
    swapped TX/RX and a transceiver stuck in standby.
19. **Two nodes on a real bus**, terminated at both ends, short cable.
20. **Scope CANH/CANL.** Recessive should sit near 2.5 V on both lines.
21. **Long cable, maximum bit rate, monitor error counters for an hour.**
    This is where a marginal crystal shows up, and where the tolerance
    numbers from stage 2 get their real test.

## Stage 5 — isolated front end

Run by `stage_isolated_afe()`.

22. **Power the island from the bench through the barrier jumper**, isolated
    DC-DC disabled. The firmware turns it off in software. Verify 3V3_ISO.

    > The jumper is marked **DEFEATS ISOLATION — BENCH USE ONLY**. It is
    > fitted for exactly this measurement and removed afterwards.

23. **Talk to the ADS1220.** The self-test writes the configuration and reads
    all four registers back, comparing against what it wrote. **This is the
    test that proves the isolator works in both directions** — three forward
    channels carrying SCK/MOSI/CS and two reverse channels carrying
    MISO/DRDY. If the write lands and the read comes back, all five are
    alive. If it fails, the test points on both sides of the isolator tell
    you in two minutes whether the signal is arriving.

24. **Short the inputs and read.** This is the noise floor with clean power —
    the best case. The self-test takes 64 samples and reports noise-free
    bits; for the real characterization, dump 1000 samples and run
    `tools/noise_floor.py`.

    Expect **18–19 bits**. More than about 21 means the inputs are not
    actually shorted or the part is returning a constant, and the self-test
    flags that rather than celebrating it.

25. **Apply a known current** from a bench source through the shunt. The
    self-test prints the expected ADC voltage for 4, 12 and 20 mA:
    400 000 µV, 1 200 000 µV, 2 000 000 µV.

26. **Now enable the isolated DC-DC and repeat the noise measurement.**
    **The difference is the cost of isolation**, and it is the number section
    16 wants. Run both dumps through `tools/noise_floor.py --compare`.

    If the delta is more than two bits, section 8.4's list in order of
    likelihood: the post-regulating LDO's PSRR at the switching frequency,
    physical separation between the converter and the ADC, the Y-capacitor's
    placement, and the isolated-side ground pour.

27. **Real sensor.** A 4–20 mA transmitter, or a precision current source
    standing in for one. Then unplug the loop mid-reading and confirm the
    board reports `fault: below NE43 low (broken wire?)` and not `0.0`.

---

## After bring-up

`docs/characterization.md` is the template for the numbers. The four worth
producing, in the order they are cheapest to get:

1. Noise floor, both ways, and the difference.
2. Accuracy sweep → gain, offset, INL (`tools/accuracy.py`).
3. PD compatibility across every charger you can find
   (`tools/pd_matrix.py --markdown` gives the table shape).
4. CAN error counters over an hour at full rate on your longest cable.
