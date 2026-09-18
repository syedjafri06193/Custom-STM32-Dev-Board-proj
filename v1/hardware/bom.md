# Bill of materials — key parts

Rev A. Passives, decoupling and connectors are not enumerated here; this is
the list where the *choice* matters and a substitution changes the design.

Every part is checked against `board.yaml` by `hardware/rules/`, so a change
here that contradicts a design rule fails the build rather than the board.

---

## The parts that gate everything

| Function | Part | Package | Why this one |
|---|---|---|---|
| MCU | **STM32G474RET6** | LQFP64 | UCPD + 3× FDCAN + 5× 12-bit ADC in one part. 0.5 mm pitch is hand-solderable; 0.4 mm QFN is not. 64 pins leaves spare GPIO for a header. |
| USB-C protection | **TCPP01-M12** | QFN-12 | The single most important part on the board. Provides **dead-battery Rd independent of MCU power** (section 6.2 — without it, first plug-in can brick the board unrecoverably), CC overvoltage protection to ~22 V, VBUS discharge, and a gate driver for the bulk-cap pass FET. One part, three problems. |
| SPI isolator | **ADuM4154** | Wide-body SOIC-16 | **3 forward + 2 reverse channels.** A generic quad isolator cannot do SPI-plus-DRDY (section 4.3). Also handles the round-trip clock delay that limits SPI speed across a barrier. Wide-body footprint from the start so a reinforced v2 is a part swap, not a re-layout. |
| ADC | **ADS1220** | TSSOP-16 | 24-bit ΔΣ, 2.048 V internal reference, PGA, and IDACs that make RTD support a v2 firmware change rather than a redesign. |
| Crystal | **16 MHz, ±30 ppm, 8 pF CL** | 3225 | **Required for CAN** (section 7.2). The HSI16's ±1% fails at every standard bit rate — `can_timing.c` proves it. Load caps: `CL = 2 × (C_load − C_stray)`, stray 3–5 pF. |
| CAN transceiver | **TCAN1042HGV** | SOIC-8 | 3.3 V I/O with 5 V supply, ~110 ns loop delay (which `can_timing_tdco()` compensates for). Lay out the **ISO1042** footprint alongside and populate one. |

## Power

| Function | Part class | Key requirement |
|---|---|---|
| Buck, VBUS → 5 V | 40 V-rated synchronous buck, ≥500 mA | **40 V rating, not 25 V.** Twice the 20 V SPR ceiling, for the inductive transient when a cable is yanked. This is a design rule, not a preference. |
| LDO, 5 V → 3V3_D | 300 mA, any | Dissipates 0.26 W at peak — needs a thermal pad. |
| LDO, 5 V → 3V3_A | 150 mA, **low noise, high PSRR** | Separate part from 3V3_D. Section 5.3 is explicit: do not ferrite-bead a shared rail and call it separated. |
| Isolated DC-DC | 1 W module, 5 V → 5 V | A module, not a discrete transformer design. Removes transformer selection, snubber tuning and EMI iteration from the first board. Noise spec is mediocre; the LDO below fixes that. |
| LDO, isolated | 100 mA, **high PSRR** | **Post-regulation is mandatory** (section 8.3). Feeding a 24-bit ADC directly from a switching converter is how you get 12 noise-free bits from it. |

## Analog front end, per channel

| Function | Part | Requirement |
|---|---|---|
| Shunt | **100 Ω, 0.1%, ≤25 ppm/°C** | 0805. **This resistor is the measurement accuracy.** 20 mA × 100 Ω = 2.0 V, inside the 2.048 V reference at PGA = 1 with headroom; 40 mW is a third of an 0805's rating. |
| Series protection | 1 kΩ | Sized for a **continuous** 24 V fault (24 mA through the clamp), not an 8/20 µs surge. Somebody will leave the loop supply on a 0–10 V input for an hour. |
| Input protection | PTC + TVS | Section 10. |
| Divider (0–10 V mode) | 1:6, 0.1% | Jumper-selectable alternative on the same channels. |

## Protection and passives that are design decisions

| Item | Value | Why |
|---|---|---|
| VBUS TVS | Bidirectional, ≥24 V standoff | Must not conduct at the negotiated 20 V. |
| Pre-contract bypass | **8.2 µF total** | Type-C bounds this at ~10 µF. `test_precontract_capacitance_is_within_the_type_c_bound` enforces it. |
| Bulk capacitance | 470 µF, **switched** | Behind an NMOS with a **slew-limited** gate. Hard-switching it charges the bulk as a step, which is the inrush this is here to avoid. |
| Y-capacitor | 470 pF C0G | Across the barrier, **adjacent to the transformer**. Gives common-mode current a short return instead of routing it through the measurement. |
| CAN termination | 2 × 60 Ω + 4.7 nF | Split termination. Same 120 Ω differential, plus a common-mode path, for one extra part. Behind a jumper. |
| VBUS divider | 1 MΩ / 143 kΩ + 100 nF | Microamps of quiescent draw, ~125 kΩ source impedance. The 100 nF at the ADC pin is **not optional** — without it the ADC cannot settle through that impedance. |

## Bring-up hardware (section 11 — all of it)

| Item | Count | Note |
|---|---|---|
| 0 Ω jumper / 2-pin header per rail | 6 | Break the rail to measure current or inject bench power. |
| Barrier power jumper | 1 | Silkscreen: **"DEFEATS ISOLATION — BENCH USE ONLY"**. This is what makes the section 16 noise measurement a jumper move rather than a soldering job. |
| CAN termination jumper | 1 | Labelled. |
| BOOT0 jumper | 1 | ST bootloader. |
| Test points | 30 | Enumerated in `board.yaml`; includes **every SPI line on both sides of the isolator**. |
| SWD header | 1 | **2.54 mm, not Tag-Connect.** You will plug this in hundreds of times. |
| LEDs | 8 | One per rail (5) + user + CAN activity + fault. |
| Spare GPIO header | 1 | For toggling pins to scope the PD state machine. |

## Substitutions that are *not* acceptable

These are the ones worth writing down, because each looks reasonable and each
breaks something the design depends on:

- **A generic quad digital isolator** in place of the ADuM4154. SPI plus DRDY
  is 3 forward and 2 reverse. A 4-channel part split 3/1 or 2/2 cannot do it,
  and the discovery comes after layout.
- **The HSI16 in place of the crystal.** Fails at every standard CAN bit rate.
- **A 25 V buck** in place of a 40 V one. Survives the bench and dies on a
  cable transient.
- **Ferrite-beading 3V3_D to make 3V3_A.** Section 5.3 names this specifically.
- **An unswitched 470 µF on VBUS.** Sources declare a fault and drop out; the
  board oscillates between negotiating and browning out, and it reads as
  "PD doesn't work".
- **Skipping the LDO after the isolated DC-DC.** Costs several noise-free
  bits, which is most of what a 24-bit part was bought for.

## Spares

Section 12.3: order at least **three** of every fine-pitch part and **five**
of the cheap passives. The MCU, TCPP01, ADuM4154 and ADS1220 are the ones you
will kill during bring-up, and a three-week reorder for a €4 part in the
middle of debugging is the worst possible reason to stop.
