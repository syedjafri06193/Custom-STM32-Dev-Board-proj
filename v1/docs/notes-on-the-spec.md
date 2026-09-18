# Notes on the design document

Places where building this turned up something the design document did not
say, said differently, or where a choice had to be made that it left open.
Recorded here rather than silently absorbed, because the next person to read
`Documentation/README.md` alongside this code should be able to see where the
two diverge and why.

---

## 1. The CAN oscillator tolerance claim is right, and tighter than stated

Section 7.2 says standard configurations "need roughly ≤0.5%". That checks
out, and `can_timing.c` computes it from ISO 11898-1 rather than quoting it:

| Rate | Sample point | Tolerance per node |
|---|---|---|
| 125 kbit/s | 87.5% | 4854 ppm (0.49%) |
| 250 kbit/s | 87.5% | 4854 ppm |
| 500 kbit/s | 87.6% | 4796 ppm |
| 1 Mbit/s | 87.6% | 4796 ppm |
| 2 Mbit/s FD | 82.3% | 6880 ppm |

So 0.5% is accurate. The HSI16 at ±1% misses by a factor of two at *every*
standard rate — not marginally, and not only at high rates, which is the form
the argument usually takes. `hsi_fails_at_every_standard_rate` asserts that.

One caveat the document does not mention and this implementation cannot fix:
the STM32 `TSEG1` register lumps the propagation segment together with phase
segment 1, while the ISO formula wants phase segment 1 alone. Using `TSEG1`
for `PS1` makes the second term optimistic. At ordinary sample points the
first term (the SJW one) binds and is exact, so the number above is sound —
but it is a design aid, not a certification. This is stated in
`can_timing.h`'s header rather than buried.

## 2. Two bugs the C/Python cross-check found

`tools/can_timing.py` implements the same specification by exhaustive search,
and `tools/tests/test_can_timing_crosscheck.py` compares the two across a grid
of clocks, rates and sample points. It found two real defects in the C solver:

**A sample-point "tie band" that was far too wide.** The solver treated any
sample point within 5% of the request as equal to the request, then broke the
tie on oscillator tolerance. Asked for 87.5% at 125 kbit/s it was returning
**82.7%**, because a wider phase segment 2 scored better. Five percent is not
a rounding error — it is a real change in how a bus behaves on a long cable,
and section 7.3's discussion of propagation delay is exactly about that. The
band existed to stop the solver answering 2 Mbit/s CAN FD with five time
quanta; that problem is now handled by ranking *resolution* above sample point
up to a floor of 16 tq, which is the constraint that was actually being
expressed.

**Only ever rounding the time-quanta count down.** For bit rates that do not
divide the kernel clock exactly — 800 kbit/s at 170 MHz wants 212.5 tq — the
solver considered only `floor()`, never `ceil()`. It was returning the worse
of two available answers, by about 10 ppm. Small, but it is the class of bug
that is invisible by inspection and only shows up as a slightly worse margin
on a long bus.

Neither would have been found by testing the C solver against itself.

## 3. The design document does not specify a pinout

It specifies the MCU (STM32G474RET6, LQFP64) and every peripheral, but not
which pin does what. The choices are in `firmware/target/inc/board.h` with the
reasoning next to each. The one worth flagging:

**FDCAN1 is on PB8/PB9, not PA11/PA12.** Both are valid AF9 mappings. PA11
and PA12 are also USB_DM/USB_DP, and spending them on CAN permanently
forecloses adding a USB device interface — on a board whose entire front end
is a USB-C connector. Section 9.2 discusses routing D+/D−, so the document
clearly expects them to exist.

## 4. 16 MHz crystal, and why the PLL settings are what they are

Section 4.2 says "8 or 16 MHz". 16 MHz was chosen because it gives a clean
path to both clocks that matter:

```
16 MHz / 4 = 4 MHz PLL input   (spec range 2.66–16 MHz)
        × 85 = 340 MHz VCO      (spec range 96–344 MHz)
        / 2 (R) = 170 MHz SYSCLK — the part's maximum
        / 2 (Q) = 170 MHz FDCAN kernel clock
```

The FDCAN kernel clock is taken from **PLLQ, not PCLK1**. The document does
not raise this, but it matters: with PCLK1 as the source, anybody who halves
the APB1 prescaler for a power experiment silently halves every CAN bit rate
on the bus. PLLQ decouples them.

## 5. The G4's boost mode is a sequencing trap

Not mentioned in the design document, and it is the kind of thing that
produces a board which works on the bench and fails on a cold part. Reaching
170 MHz on a G4 requires range 1 *boost* mode, and RM0440 specifies an order:
AHB prescaler to ÷2, enter boost, switch SYSCLK to the PLL, wait at least 1 µs,
prescaler back to ÷1. Skipping the prescaler steps usually appears to work.
`rcc.c` implements the full sequence with a comment explaining why.

## 6. UCPD: what is implemented and what is not

This is the one place where the firmware is deliberately incomplete, and
saying so is better than shipping something that looks finished.

**Implemented:** UCPD1 as a sink, both CC lines monitored, cable orientation
detection, and the dead-battery handover sequenced correctly against section
6.2 — `PWR_CR3.UCPD1_DBDIS` and the TCPP01's `DBn` are released only *after*
the peripheral is enabled and presenting Rd itself. Getting that order wrong
is the bricking trap, and it is not recoverable without lifting a part.

**Not implemented:** the PD protocol layer — BMC encoding, CRC, GoodCRC,
retries, the policy engine state machine. That is a conformance-grade
component, and an unverified one would be worse than none, because it would
fail in the field rather than on the bench. Section 6.2 itself suggests an
STUSB4500 for exactly this reason.

The seam is `ucpd_on_source_capabilities()`. Whatever supplies the protocol
layer hands it the raw PDOs; from there it is `pd_policy_select()`, which is
covered by 14 host tests. **The SPR invariant survives the gap**: the policy
refuses EPR PDOs and anything above 20 V regardless of what the protocol layer
does, and `vbus_classify()` flags anything above 21 V as a violated invariant
rather than as a reading.

## 7. The FDCAN message RAM layout is device-specific

ST's cut-down M_CAN on the G4 has *fixed* message RAM addresses — unlike the
H7, there is no `SIDFC`/`RXF0C`/`TXBC` address programming, and `RXGFC`
replaces the filter and element-size registers. Each instance owns a
0x350-byte block:

| Offset | Size | Contents |
|---|---|---|
| 0x0000 | 0x0070 | 28 standard filters, 1 word each |
| 0x0070 | 0x0040 | 8 extended filters, 2 words each |
| 0x00B0 | 0x00D8 | Rx FIFO 0, 3 × 18 words |
| 0x0188 | 0x00D8 | Rx FIFO 1, 3 × 18 words |
| 0x0260 | 0x0018 | Tx event FIFO, 3 × 2 words |
| 0x0278 | 0x00D8 | Tx buffers, 3 × 18 words |

It sums to exactly 0x350, which is a good sign, but **this is the one table in
the firmware that cannot be derived from anything else and should be checked
against RM0440 for the exact part before first silicon.** It is isolated in
named constants at the top of `fdcan.c` so that check is a one-line fix.

## 8. NE43 thresholds are not in the design document, and should be

Section 8.1 recommends 4–20 mA and section 8.2 sizes the shunt, but neither
says what to do when the loop current is outside 4–20 mA. NAMUR NE43 does:
below 3.6 mA or above 21 mA is a *fault*, not an out-of-range reading.

The distinction matters more than it sounds. A cut cable reads 0 mA. Scaled
naively that is "0.0% of span", which for a 0–250 °C transmitter is "0 °C" —
a plausible number, logged as data. `afe.c` reports `AFE_FAULT_LOW` and still
returns the value, because the number is what shows the trend that explains
the fault; what it never does is present it as a measurement.

## 9. Where "noise-free bits" and "effective bits" diverge

Section 16 gives `log2(FSR / (6.6 × rms))`, which is noise-free resolution.
The ADS1220 datasheet quotes *effective* resolution, `log2(FSR / rms)`, which
is always exactly `log2(6.6) = 2.72` bits higher. The same board is honestly
"18.5 noise-free bits" or "21.2 effective bits", and only one of those is what
a single reading is good to. `tools/noise_floor.py` prints both and labels
which is which; `test_effective_bits_is_always_2_72_higher` pins the
relationship.

## 10. A design decision the document leaves open: 0–10 V span rounding

Section 8.1 offers 0–10 V as a jumper-selectable alternative. Implementing it
surfaced a small but real question: 10.000000 V through a 1:6 divider comes
back as 10.000002 V after integer scaling, and flagging that as over-range
puts a fault on a sensor sitting exactly at its maximum.

`afe_convert()` allows 10 ppm of span past each end before calling it
out-of-range — far below the ADC's own noise floor, so nothing real hides
inside it. It is a deliberate choice, not an accident of arithmetic, which is
why it has a named function and a comment rather than a magic constant.
