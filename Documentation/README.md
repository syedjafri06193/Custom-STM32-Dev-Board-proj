# Custom STM32 Dev Board — Design & Build Guide

**Project:** 4-layer PCB with USB-C PD negotiation, CAN bus interface, and isolated analog front end for industrial sensor logging
**Status of this document:** planning + reference. Written assuming you've done simpler 2-layer boards but not something with this many interacting subsystems.

---

## Table of contents

1. [Executive summary and scope](#1-executive-summary-and-scope)
2. [Reality check](#2-reality-check)
3. [The isolation decision](#3-the-isolation-decision)
4. [Part selection](#4-part-selection)
5. [Power architecture](#5-power-architecture)
6. [USB-C Power Delivery](#6-usb-c-power-delivery)
7. [CAN bus](#7-can-bus)
8. [Isolated analog front end](#8-isolated-analog-front-end)
9. [Stackup, impedance, and floorplan](#9-stackup-impedance-and-floorplan)
10. [Protection](#10-protection)
11. [Design for bring-up](#11-design-for-bring-up)
12. [Manufacturing](#12-manufacturing)
13. [Tools and setup](#13-tools-and-setup)
14. [Milestone ladder](#14-milestone-ladder)
15. [Bring-up procedure](#15-bring-up-procedure)
16. [Characterization](#16-characterization)
17. [Stretch goals](#17-stretch-goals)
18. [References](#18-references)

---

## 1. Executive summary and scope

### The original statement

> 4-layer PCB with USB-C PD negotiation, CAN bus interface, and isolated analog front-end for industrial sensor logging.

This is buildable and is a genuinely strong portfolio board. But it is **three hard subsystems on one PCB**, each of which is a board's worth of debugging on its own, and each of which has a specific failure mode that will brick or destroy your first revision if you don't design around it up front.

The findings that matter most:

1. **Your MCU choice gates two of the three headline features.** USB-C PD in silicon (the UCPD peripheral) and CAN FD (the FDCAN peripheral) exist on STM32G0/G4/L5/U5/H5. They do **not** exist on the F1/F4/F7 families that most people reach for by default. Pick an F4 and you've committed to an external PD controller and classic CAN before you've drawn a single net. See section 4.
2. **A USB-C sink without dead-battery Rd resistors never powers on.** The source won't apply VBUS until it sees Rd on CC. If Rd is provided by the MCU's UCPD peripheral, and the MCU has no power because there's no VBUS, you have a deadlock and a board that appears completely dead. This is a real and common first-revision failure. See section 6.2.
3. **The isolation class you need is not obvious, and it determines your floorplan.** Functional isolation (breaking ground loops) needs ~1–2 mm of creepage. Reinforced isolation (safety, 250 Vrms working) needs ~8 mm plus a routed slot. That's the difference between a compact board and one where a third of the area is empty FR4. Decide before you floorplan. See section 3.
4. **Switching noise from the isolated DC-DC will dominate your analog noise floor.** A 24-bit ADC on an isolated island powered by a switching converter realistically delivers ~18–19 noise-free bits, not 24. Plan for it and measure it. See section 8.4.
5. **USB-PD EPR is not your problem unless you opt in.** EPR tops out at 48 V, but a sink only ever sees 28/36/48 V if it explicitly enters EPR mode. Stay in SPR and your worst case is 20 V — a much easier board to protect. Make "never request EPR" an explicit firmware invariant. See section 6.1.

### Revised project statement

> A 4-layer STM32G4 development board: USB-C PD sink (SPR only, 5–20 V) with dedicated CC/VBUS protection and dead-battery support, CAN FD with selectable termination and optional isolation, and a functionally-isolated 4-channel 4–20 mA / 0–10 V analog front end built around a 24-bit delta-sigma ADC, with staged bring-up jumpers and comprehensive test points.

The additions to the original — SPR-only, functional isolation, staged bring-up — are the difference between a board that works on revision B and one that works on revision D.

### Explicit non-goals

- **Not a safety-certified product.** No UL/CE marking, no mains connection, no compliance testing. Say so on the silkscreen.
- **Not USB-IF certified.** You can build a working PD sink without certification; you just can't use the logo.
- **Not USB high-speed.** The G4's USB is full-speed (12 Mbps). This is a feature — see section 9.2.
- **Not a 2-week project.** Budget two board revisions minimum. Three is normal.

---

## 2. Reality check

### 2.1 Three subsystems, three debugging campaigns

| Subsystem | Hard part | Typical first-rev failure |
|---|---|---|
| **USB-C PD** | The PD stack is firmware, not a peripheral you enable. CC lines need protection. Dead-battery handling. | Board never powers up (no Rd), or CC pin destroyed by a bad cable |
| **CAN** | Bit timing needs an accurate clock. Termination and topology. | Works on the bench with 30 cm of wire, fails on a real bus |
| **Isolated AFE** | Isolated power is a switching converter sitting next to your most sensitive circuit | Noise floor 20 dB worse than the datasheet suggests |

**The implication for design:** every subsystem must be independently powerable and independently testable. That means 0 Ω jumpers in every rail, test points on every node you might want to probe, and the ability to inject bench power at each stage. A board where you must bring everything up at once is a board where a single fault in any subsystem makes the whole thing undiagnosable.

### 2.2 The things that will destroy hardware

These are worth designing against specifically, because each has a documented history of killing real boards:

| Hazard | Mechanism | Defense |
|---|---|---|
| **CC shorted to VBUS** | Miswired cables have done this in the wild and killed laptops. STM32 CC pins are not 20 V tolerant. | Dedicated CC protection IC (TCPP01-M12) or series R + clamp |
| **VBUS transient during PD transition** | The source ramps 5 V → 20 V; overshoot and cable inductance add margin on top | Input stage rated ≥40 V, TVS, no exceptions |
| **Inrush faulting the source** | Type-C limits sink bypass capacitance to ~10 µF before a contract. Hanging 470 µF of bulk on VBUS makes sources trip. | Load switch gating bulk caps, enabled after contract |
| **24 V loop supply applied to a 0–10 V input** | It will happen the first time someone wires a sensor | Series resistance + clamps sized for continuous 30 V |
| **CAN bus fault voltages** | Industrial and automotive buses see large transients and miswiring | TVS on CANH/CANL, transceiver rated for bus faults |
| **ESD on every external connector** | Handling, cable insertion | TVS diodes at connectors, before anything else |

### 2.3 Scope questions you must answer before schematic capture

These change the design fundamentally, and changing your mind after layout means starting over:

1. **Isolation class?** Functional or reinforced. (Section 3.)
2. **Is CAN isolated too?** Industrial nodes often need it for ground-potential differences. A second isolation domain roughly doubles the isolation cost and area.
3. **What sensors, specifically?** 4–20 mA, 0–10 V, thermocouple, and RTD need completely different front ends. Pick one or two for v1.
4. **Is USB data needed, or power only?** Power-only removes D+/D- entirely.
5. **How much power does the board need?** If it's 2 W, PD negotiation is decoration — a 5 V dumb charger works. PD earns its place when you need 9 V/12 V/20 V for loop supplies or motor drive.

That last question deserves a hard look. **If the board only needs 5 V, USB-C PD is complexity for its own sake.** The honest justification is usually "I need 24 V for the current loop" — and you can't get 24 V from PD anyway without EPR. A defensible answer: negotiate 20 V and boost to 24 V for loop power, or negotiate 12 V and use a small boost. Decide what PD is *for*.

---

## 3. The isolation decision

Pin this down first. It determines board area, part cost, and whether you need a routed slot.

| | **Functional isolation** | **Basic isolation** | **Reinforced isolation** |
|---|---|---|---|
| Purpose | Break ground loops, reject common-mode | One layer of protection against shock | Two layers / double thickness |
| Typical working voltage | Whatever your common-mode is (tens of volts) | 250 Vrms | 250 Vrms |
| Creepage needed | ~1–2 mm | ~5 mm | ~8 mm |
| Slot required | No | Usually no | Usually yes |
| Isolator packages | Narrow SOIC fine | Wide-body SOIC | Wide-body SOIC, specific parts |
| Isolated DC-DC | Any module | Rated module | Rated module, reinforced |
| Board area cost | Small | Moderate | **Large** |

### Which do you need?

**For 24 V industrial sensors: functional isolation.** The sensor loop is nowhere near hazardous voltage. What you're actually solving is ground-potential difference between the sensor's location and your board's ground, plus common-mode rejection. A few millimetres of creepage handles it.

**Reinforced isolation is only needed if a fault could put mains on the sensor side.** That's a real scenario in some plants — a 230 V motor winding shorting to a thermocouple sheath, for instance — but if it's not your scenario, 8 mm of empty board plus safety-rated parts buys you nothing.

Worth noting: the USB side is never the concern. EPR's 48 V ceiling was chosen specifically to sit below the thresholds where stricter insulation and labelling requirements kick in, so nothing on your USB-C input is hazardous by any standard.

**Recommendation for v1: functional isolation, ~2.5 mm barrier, no slot.** Document the limitation clearly on the board and in the README: "functional isolation only — not rated for mains-referenced sensors." Design the footprint so a reinforced version is a straightforward v2 if you ever need it (use wide-body isolator footprints from the start; they accept narrow parts but not vice versa).

### Barrier rules, whichever class you pick

- **The barrier cuts every copper layer.** L1, L2, L3, L4 — a continuous gap. A ground plane bridging the barrier on L2 defeats the entire thing, and it's invisible in a 2D layer view. Check it in 3D.
- **Nothing crosses the barrier except the isolator parts and the isolated DC-DC transformer.** No traces, no pours, no thermal reliefs, no fiducials.
- **Silkscreen may cross** (it's not conductive) and should — draw the barrier explicitly so anyone looking at the board can see it.
- **A Y-capacitor across the barrier** (100 pF – 1 nF) substantially reduces common-mode current and radiated emissions from the isolated DC-DC. For functional isolation, any COG/NP0 part works. For safety isolation it must be a rated Y-class capacitor. Place it directly between the two ground pours, close to the transformer.

---

## 4. Part selection

### 4.1 The MCU decision gates everything

| Family | UCPD (PD in silicon) | FDCAN | Verdict |
|---|---|---|---|
| STM32F1, F4, F7 | ❌ | ❌ (bxCAN only) | Needs an external PD controller. Avoid for this project. |
| **STM32G0** | ✅ | ✅ | Good, cheap, fewer pins |
| **STM32G4** | ✅ | ✅ (up to 3×) | **Recommended** |
| STM32L5, U5, H5 | ✅ | ✅ | Fine, more expensive |

**Recommendation: STM32G474RET6** (LQFP64, 512 KB flash, 128 KB RAM, 170 MHz, UCPD, 3× FDCAN, 5× 12-bit ADC).

Reasons beyond the peripherals:

- **LQFP, not QFN.** Every pin is visible, probeable, and reworkable with an iron. On a first board this matters enormously — you will need to cut a trace and bodge a wire, and you cannot do that under a QFN.
- 64 pins is enough for everything here with room to break out spare GPIO to headers.
- Well-documented, huge community, ST provides the PD stack.

If cost or size matters more, **STM32G431CBT6** (LQFP48, 128 KB flash, 1× FDCAN) does everything except give you spare peripherals.

### 4.2 Bill of materials, key parts

| Function | Part | Why |
|---|---|---|
| MCU | STM32G474RET6 | UCPD + FDCAN + LQFP |
| **USB-C port protection** | **TCPP01-M12** | Purpose-built for STM32 UCPD sinks. Provides **dead-battery Rd**, CC overvoltage protection to ~22 V, VBUS discharge, and a gate driver for an external NMOS. This single part solves the bricking problem in 2.2. |
| USB-C receptacle | 16-pin, through-hole mounting tabs | 16-pin has no SuperSpeed pairs — you don't need them. TH tabs survive cable yanking; SMD-only tabs tear off. |
| Buck converter | 40 V-rated, 5–20 V in → 5 V out, e.g. LMR51440 class | Must survive 20 V plus transients. 40 V rating gives real margin. |
| Digital LDO | 3.3 V, e.g. TLV75733 | |
| Analog LDO | 3.3 V low-noise, separate | Separate rail from digital |
| Crystal | 8 or 16 MHz, ±30 ppm, with correct load caps | **Required for CAN** — see 7.2 |
| CAN transceiver (non-isolated) | TCAN1042 or TJA1051T/3 | 3.3 V logic I/O |
| CAN transceiver (isolated option) | ISO1042 | If you need CAN isolation |
| **ADC** | **ADS1220** | 24-bit ΔΣ, 4-channel mux, internal PGA, internal 2.048 V reference, two IDACs for RTD excitation. One chip is the whole isolated measurement side. |
| SPI isolator | ADuM3154 / ADuM4154 (SPIsolator family) | See 4.3 |
| Isolated DC-DC | Off-the-shelf 1 W module (e.g. Murata MEJ1S0505SC class) for v1 | See 8.3 |
| Sensor connector | Pluggable screw terminal (3.5 mm pitch) | Industrial expectation; no crimp tooling needed |
| CAN connector | DE-9 (pin 2 = CANL, 7 = CANH, 3 = GND) and/or terminal block | DE-9 is the de facto CAN standard |

### 4.3 Count your isolator directions before you pick the part

This trips people up. An SPI link to the ADS1220 needs:

| Signal | Direction across the barrier |
|---|---|
| SCLK | MCU → ADC |
| MOSI (DIN) | MCU → ADC |
| CS | MCU → ADC |
| MISO (DOUT) | ADC → MCU |
| DRDY | ADC → MCU |

That's **3 forward, 2 reverse**. A generic quad isolator won't do it, and a 4-channel part split 3/1 or 2/2 won't either. The ADuM315x/415x "SPIsolator" family exists for exactly this shape and additionally handles the round-trip clock delay that limits SPI speed across an isolator.

**Make the direction count an explicit line in your part-selection notes.** Discovering you need one more reverse channel after layout means a new part, a new footprint, and a re-route.

---

## 5. Power architecture

### 5.1 Power tree

```
USB-C VBUS (5–20 V, SPR only)
   │
   ├─ TVS (bidirectional, standoff > 20 V)
   │
   ├─ TCPP01-M12 ── gate ──▶ NMOS pass FET
   │                            │
   │                      ┌─────┴──────┐
   │                      │ Bulk caps  │  ≤10 µF before contract;
   │                      │ (switched) │  bulk switched in after
   │                      └─────┬──────┘
   │                            │
   │                      ┌─────▼──────────┐
   │                      │ Buck 40 V-rated│──▶ 5V_MAIN
   │                      └─────┬──────────┘
   │                            │
   │              ┌─────────────┼──────────────┐
   │              │             │              │
   │        ┌─────▼────┐  ┌─────▼──────┐  ┌───▼─────────────┐
   │        │ LDO 3.3V │  │ LDO 3.3V   │  │ Isolated DC-DC  │
   │        │ digital  │  │ analog     │  │ 5V → 5V_ISO     │
   │        └─────┬────┘  └─────┬──────┘  └───┬─────────────┘
   │              │             │             │
   │           3V3_D         3V3_A            │  ═══ BARRIER ═══
   │         (MCU, CAN)   (MCU VDDA,          │
   │                       ref, VREF+)   ┌────▼──────┐
   │                                     │ LDO 3.3V  │
   └─ divider ──▶ ADC (VBUS monitor)     │ isolated  │
                                         └────┬──────┘
                                           3V3_ISO
                                        (ADS1220, front end)
```

### 5.2 Power budget

Build this table before choosing the buck, not after. Numbers are order-of-magnitude placeholders — fill in from datasheets.

| Rail | Load | Typical | Peak |
|---|---|---|---|
| 3V3_D | MCU @ 170 MHz | 40 mA | 60 mA |
| 3V3_D | CAN transceiver (dominant) | 20 mA | 70 mA |
| 3V3_D | LEDs, pull-ups | 15 mA | 25 mA |
| 3V3_A | MCU VDDA + reference | 10 mA | 15 mA |
| 3V3_ISO | ADS1220 + conditioning | 5 mA | 10 mA |
| 5V_ISO | (before isolated LDO) | 15 mA | 25 mA |
| **Total at 5 V** | including conversion losses | **~180 mA** | **~300 mA** |

At 20 V input that's under 2 W. **Which means the board doesn't need PD at all for its own consumption.** If PD is in the project, it should be powering something — loop supply for the 4–20 mA sensors is the natural answer. Add that to the budget explicitly, because a 24 V loop supply at 20 mA × 4 channels is another 2 W and it's the reason PD exists on this board.

### 5.3 Rules

- **Separate analog and digital 3.3 V rails from separate LDOs**, joined only at a single star point at the MCU's ground. Do not just ferrite-bead a shared rail and call it separated.
- **The MCU's VDDA gets its own filtering** — ferrite + 1 µF + 100 nF, per the reference manual's recommendation.
- **Every rail gets a test point and a 0 Ω jumper.** The jumper lets you break the rail to measure current or inject bench power.
- **Every rail gets an LED** (through a high-value resistor so it costs microamps). When you're staring at a dead board, knowing which rails came up saves an hour.

---

## 6. USB-C Power Delivery

### 6.1 Stay in SPR

PD 3.1 Extended Power Range reaches 48 V, but **EPR is opt-in**. A sink only sees 28/36/48 V after it explicitly requests entry into EPR mode. If your firmware never does, the highest voltage that can appear on VBUS is 20 V.

Make this a hard invariant:

- Firmware never sends an EPR mode entry request
- Sink capability PDOs declare SPR voltages only
- Input protection is rated for 20 V nominal with margin to ~40 V for transients

Documenting this as a design constraint, rather than assuming it, is what keeps someone from "improving" the firmware later and destroying the board.

### 6.2 Dead battery: the bricking trap ★

A USB-C source applies VBUS only after it detects a sink — which it does by sensing the 5.1 kΩ Rd pull-downs on CC1/CC2.

The failure mode: if Rd is provided by the MCU's UCPD peripheral, then:

```
No VBUS  →  MCU unpowered  →  no Rd presented  →  source sees nothing
                                                        ↓
                                                  never applies VBUS
                                                        ↓
                                                  (loop forever)
```

The board is completely dead and there is nothing to debug, because nothing is powered.

**Solutions:**

1. **TCPP01-M12** (recommended) — presents dead-battery Rd independent of MCU power, and hands CC over to the MCU once it's alive. It also handles the CC overvoltage problem in 2.2. One part, two problems solved.
2. **Discrete 5.1 kΩ Rd to ground on both CC lines** — always present, works, but then the MCU can't control CC state for role swaps or advanced PD features, and the resistors are still in circuit when UCPD drives the line.
3. **A separate always-on PD sink controller** (e.g. STUSB4500, which stores its PDO config in NVM and needs no firmware at all) — simplest of all if you don't want to write a PD stack.

**If your goal is to learn PD, use STM32 UCPD + TCPP01. If your goal is a working board, STUSB4500 negotiates without a single line of firmware.** Both are defensible; pick consciously.

### 6.3 Inrush and pre-contract capacitance

The Type-C specification bounds a sink's VBUS bypass capacitance (roughly 1–10 µF) before a power contract exists. This is not pedantry — a source charging 470 µF through a 5 V → 20 V transition sees an inrush spike and may declare a fault and shut down. Your board then oscillates between negotiating and browning out.

```
VBUS ──┬── 10 µF (always present, meets the spec)
       │
       └── [load switch] ── 220 µF bulk (enabled after contract)
```

The TCPP01's gate driver output can drive the pass FET for this. Use a controlled slew rate on the gate so the bulk charges gradually rather than as a step.

### 6.4 CC line protection

The CC pins on the MCU are low-voltage logic pins. A faulty cable that shorts CC to VBUS puts 20 V on them. This has destroyed real hardware.

- Put the protection IC **immediately at the connector**, before the CC traces go anywhere near the MCU
- Series resistance on CC per the reference design (typically a few hundred ohms; follow the TCPP01 datasheet exactly — this value interacts with Rd detection thresholds)
- Keep CC traces short and free of stubs; PD signalling is BMC-encoded at ~300 kHz, so it's not fast, but stubs and long routes degrade the edges

### 6.5 Firmware reality

**The PD stack is a substantial firmware project, not a peripheral you enable.** ST provides X-CUBE-TCPP with a USB-PD core stack, and it's usable, but expect:

- Days of integration before the first successful contract
- A state machine you'll need to understand to debug anything
- Careful attention to the sink PDO list — declare 5 V as acceptable so the board works with dumb chargers, and request higher voltages only when you actually need them

**Always make 5 V an acceptable operating point.** A dev board that only works with a PD-capable charger is a dev board that doesn't work at the exact moment you need it to.

### 6.6 VBUS monitoring

Divide VBUS into an ADC channel, scaled for ~24 V full scale into 3.0 V. This gives you:

- Confirmation the negotiated voltage actually appeared
- Detection of a source that dropped out
- A debugging signal during bring-up worth more than any amount of printf

Size the divider for microamps of quiescent draw, and put a small cap at the ADC pin to keep the source impedance manageable.

---

## 7. CAN bus

### 7.1 Peripheral and transceiver

The G4's FDCAN gives you CAN FD (up to 8 Mbps data phase) and classic CAN. For 3.3 V logic use a 3.3 V-supplied transceiver — TCAN1042 or TJA1051T/3. These interoperate fine with 5 V transceivers on the same bus; the bus levels are differential and the standard accommodates both.

**Isolated option:** ISO1042 puts basic/reinforced isolation in the transceiver itself, but needs an isolated supply on the bus side — another isolated DC-DC, or a part like ADM3053 with the converter integrated. Decide in M0 whether you need this. Industrial buses spanning a plant usually do; a bench dev board usually doesn't.

Good compromise for a dev board: **lay out both footprints and populate one.** The pinouts differ, so this needs care, but a DNP option is far cheaper than a respin.

### 7.2 You need a crystal ★

CAN bit timing requires the oscillator to be accurate. The exact tolerance depends on your bit-timing parameters, but standard configurations need roughly **≤0.5%**, and CAN FD's faster data phase is tighter still.

The STM32G4's internal HSI16 is factory-trimmed to about ±1% and drifts further over temperature. **That is not good enough for CAN.** A board that works at room temperature on a short bench cable and throws error frames in a warm cabinet is the classic symptom.

Use an 8 or 16 MHz crystal at ±30 ppm or better. Get the load capacitors right — `CL = 2 × (C_load − C_stray)`, where stray is typically 3–5 pF for a decent layout. Guessing here causes startup failures and frequency error.

(The HSI48 + CRS trick that synchronizes to USB SOF packets isn't a substitute: it only works while USB is connected, and your CAN bus shouldn't depend on that.)

### 7.3 Termination

A CAN bus needs exactly 120 Ω at each physical end, and nowhere else. A dev board might be at an end or in the middle, so **make termination jumper-selectable** and label it clearly on the silkscreen.

Offer split termination as the populated option:

```
CANH ──┬── 60 Ω ──┬── 60 Ω ──┬── CANL
       │          │          │
       │        4.7 nF       │
       │          │          │
       │         GND         │
```

Two 60 Ω resistors with a capacitor to ground at the midpoint. This gives the same 120 Ω differential termination while providing a common-mode termination path, which meaningfully reduces radiated emissions and improves common-mode stability. It costs one extra resistor and one capacitor.

### 7.4 Layout and protection

- **Route CANH/CANL as a tight differential pair**, equal length, over a continuous ground reference. Exact impedance control isn't required (the bus is 120 Ω and dominated by cable, not your 40 mm of PCB), but symmetry is — asymmetry converts differential to common mode, which radiates.
- **TVS diode array across CANH/CANL** (e.g. a purpose-built CAN protection part), placed at the connector.
- **Common-mode choke** footprint between transceiver and connector. Populate it if EMI is a concern; 0 Ω jumpers otherwise.
- **Keep stubs short.** The transceiver-to-connector run is a stub on the bus; a few centimetres is fine, tens are not.

---

## 8. Isolated analog front end

### 8.1 Pick one input type for v1

Industrial sensor interfaces are not interchangeable:

| Type | Front end | Difficulty |
|---|---|---|
| **4–20 mA current loop** | Precision shunt resistor, done | **Easiest, most industrial** |
| 0–10 V | Precision divider + buffer | Easy |
| Thermocouple | µV signals, cold-junction compensation, high gain | Hard |
| RTD (3/4-wire) | Excitation current, lead compensation | Moderate |
| Strain gauge | Bridge excitation, very high gain | Hard |

**Recommendation: 4–20 mA for v1, with 0–10 V as a jumper-selectable alternative on the same channels.** Current loops are the industrial default, inherently noise-immune (current, not voltage), and the front end is a single resistor. That leaves your engineering effort available for the isolation and noise problems, which is where it's actually needed.

The ADS1220's built-in IDACs mean RTD support is a v2 firmware-and-a-few-resistors change, not a redesign. Worth leaving the footprints.

### 8.2 The signal chain

```
                ═════ ISOLATION BARRIER ═════
                            ║
Terminal  ┌──────────┐  ┌───────────┐  ┌──────────┐  ║  ┌─────────┐
  block ──│ PTC +    │──│ 100 Ω     │──│ RC filter│──║──│ ADS1220 │
          │ TVS      │  │ 0.1% shunt│  │ + clamps │  ║  │ 24-bit  │
          └──────────┘  └───────────┘  └──────────┘  ║  └────┬────┘
                                                     ║       │ SPI
                                                     ║  ┌────▼─────┐
                                                     ║  │ ADuM4154 │══▶ MCU
                                                     ║  └──────────┘
                                                     ║
                                          5V_ISO ◀───║─── isolated DC-DC
```

**Shunt sizing for 4–20 mA:** 100 Ω gives 0.4–2.0 V, which lands nicely inside the ADS1220's 2.048 V internal reference at PGA = 1 with headroom. Power dissipation at 20 mA is 40 mW — a 0805 handles it comfortably. Use 0.1% tolerance and low tempco (25 ppm/°C or better); this resistor *is* your measurement accuracy.

**Input protection** (see section 10) must survive someone applying the 24 V loop supply directly across the input. Series resistance plus clamps to the isolated rails, sized for continuous fault, not just transient.

### 8.3 Isolated power

Two approaches:

**Off-the-shelf module (recommended for v1).** A 1 W isolated DC-DC module is one part with known isolation rating and known efficiency. It removes transformer selection, snubber tuning, and EMI iteration from your first board. Cost is a few dollars and the noise spec is mediocre.

**Discrete push-pull (v2).** A transformer driver like the SN6505B plus a small transformer and an LDO gives better efficiency, lower cost at volume, and — importantly — a spread-spectrum option that smears the switching harmonics instead of concentrating them. But you're now selecting a transformer, tuning a snubber, and iterating on EMI.

Either way: **post-regulate with an LDO.** Feeding the ADC directly from a switching converter's output is how you get 12 noise-free bits from a 24-bit part. The LDO's PSRR at the switching frequency is doing real work.

### 8.4 The noise reality ★

**The isolated DC-DC is a switching converter sitting a few millimetres from your most sensitive circuit, and it will dominate your noise floor.**

Realistic expectations for a competently-executed first board: a 24-bit ΔΣ ADC delivering around **18–19 noise-free bits**. Not 24. Datasheet noise figures are measured with a clean bench supply, not an isolated flyback.

Mitigations, roughly in order of effectiveness:

1. **LDO post-regulation** on the isolated side. Non-negotiable.
2. **Physical separation** — put the DC-DC at the far end of the isolated island from the ADC. This is a floorplan decision and cannot be fixed later.
3. **LC filter** on the isolated rail, with the inductor's self-resonance above the switching frequency.
4. **Y-capacitor across the barrier**, close to the transformer, to give common-mode current a short return path instead of routing it through your measurement.
5. **Common-mode choke** on the isolated supply output.
6. **Averaging in firmware.** The ADS1220 at 20 SPS with its internal filter rejects a lot. For sensor logging you rarely need kSPS.
7. **Spread spectrum** if using a discrete driver.

**Measure the coupling explicitly during characterization** (section 16): take the noise floor with the isolated DC-DC running, then again with the island powered from a bench supply through the barrier jumper. The difference is your coupling, in numbers. That measurement is one of the more impressive things this project can produce.

### 8.5 Isolated-side grounding

The isolated island has its own ground, `GND_ISO`, which is a **local star**, not a plane tied to anything. Keep the ADC's analog and digital grounds joined at a single point under the ADC, per its datasheet layout guidance. Pour `GND_ISO` on L1 and L4 of the island, and give it plane area on L2/L3 within the barrier.

The isolated island is, in effect, a small complete board. Treat it that way.

---

## 9. Stackup, impedance, and floorplan

### 9.1 Stackup

```
L1  Signal + components            35 µm Cu
    ─── prepreg ~0.2 mm ───
L2  GROUND (solid)                 35 µm Cu
    ─── core ~1.065 mm ───
L3  Power planes (3V3_D / 3V3_A / 5V / island)
    ─── prepreg ~0.2 mm ───
L4  Signal + ground pour           35 µm Cu

Total ≈ 1.6 mm
```

Rules:

- **L2 is a solid, uninterrupted ground plane** across the entire non-isolated section. No splits, no routing, no exceptions. This is the single highest-value layout decision on the board.
- **L3 carries split power planes.** Splits on L3 are fine because L2 provides a continuous return reference for everything on L1.
- **The isolation barrier cuts all four layers.** Verify in 3D view, not layer-by-layer — a plane bridging the barrier is easy to miss.
- **Ask your fab for their actual stackup.** The default "4-layer 1.6 mm" at different fabs has wildly different dielectric spacing, and it matters for both impedance and plane capacitance. JLCPCB and PCBWay both publish theirs.

### 9.2 You probably don't need controlled impedance ★

The STM32G4's USB is **full-speed only, 12 Mbps**. Full-speed USB has rise times around 4–20 ns, giving a critical trace length in the tens of centimetres. Your D+/D- run is maybe 30 mm.

**So: skip the controlled-impedance fab option.** It costs money, adds lead time, and constrains your stackup for no benefit here. Just route D+/D- as a reasonably tight pair, keep them the same length, keep them over solid ground, and add the 22 Ω series resistor footprints the reference design calls for (you can populate 0 Ω).

If you later move to a part with USB high-speed (480 Mbps), impedance control becomes mandatory. Not here.

This is a genuine simplification, and noticing it is worth more than getting the 90 Ω geometry exactly right.

### 9.3 Floorplan

Floorplan before you place a single component. The isolation barrier and the noise sources determine everything.

```
┌─────────────────────────────────────────────────────────────────┐
│                                                    ║             │
│  ┌────────┐  ┌──────────┐  ┌──────────────────┐   ║  ┌────────┐ │
│  │ USB-C  │  │ TCPP01 + │  │                  │   ║  │ Iso    │ │
│  │        │──│ TVS      │──│  Buck → 5 V      │   ║  │ DC-DC  │ │
│  └────────┘  └──────────┘  │  LDOs → 3V3      │   ║  └───┬────┘ │
│                            └──────────────────┘   ║      │      │
│  ┌──────────┐   ┌────────────────────┐            ║  ┌───▼────┐ │
│  │ SWD hdr  │   │                    │            ║  │ LDO    │ │
│  └──────────┘   │    STM32G474       │  ┌──────┐  ║  └───┬────┘ │
│                 │                    │──│ADuM  │══║══┐   │      │
│  ┌──────────┐   │      LQFP64        │  │4154  │  ║  │   │      │
│  │ Crystal  │───│                    │  └──────┘  ║  │   │      │
│  └──────────┘   └──────┬─────────────┘            ║ ┌▼───▼───┐  │
│                        │                          ║ │ADS1220 │  │
│  ┌──────────┐   ┌──────▼──────┐                   ║ └───┬────┘  │
│  │  DE-9    │───│ CAN xcvr +  │                   ║     │       │
│  │  CAN     │   │ term + TVS  │                   ║ ┌───▼────┐  │
│  └──────────┘   └─────────────┘                   ║ │ Shunts │  │
│                                                   ║ │+ clamps│  │
│                                                   ║ └───┬────┘  │
│                                                   ║ ┌───▼────┐  │
│                                                   ║ │Terminal│  │
│                                                   ║ │ block  │  │
└───────────────────────────────────────────────────╨─┴────────┴──┘
                                          ISOLATION BARRIER
```

The important adjacencies:

- **Isolated DC-DC at the far end of the island from the ADC.** Non-negotiable. This is the single biggest lever on your noise floor and it's purely a placement decision.
- **All connectors on board edges**, protection components immediately at each connector before anything else.
- **Crystal close to the MCU** with a guard ground pour and no signals routed underneath.
- **Buck converter's switching loop kept tiny** — input cap, FET, and inductor in the smallest possible loop area. Keep it away from the analog LDO and the MCU's VDDA.
- **The barrier runs straight**, not in a jog. A straight barrier is easy to verify visually; a jogged one hides violations.

---

## 10. Protection

Assume every external connection will be miswired at least once.

| Input | Fault to survive | Defense |
|---|---|---|
| **VBUS** | 20 V nominal + transients | TVS (standoff > 20 V), 40 V-rated downstream parts |
| **CC1/CC2** | Short to VBUS in a faulty cable | TCPP01 (or series R + clamp) at the connector |
| **CANH/CANL** | Bus fault voltages, ESD, ground offset | CAN TVS array, transceiver rated for bus faults, common-mode choke |
| **Sensor inputs** | 24 V loop supply applied directly; reverse polarity | PTC + series R + clamps to isolated rails, sized for **continuous** fault |
| **All connectors** | ESD from handling | TVS at the connector, ground pour under the connector |

Two points that get missed:

**Size sensor-input protection for continuous fault, not transient.** A TVS rated for an 8/20 µs surge will happily die if someone leaves 24 V connected for an hour. You need the series resistance to limit current to something the clamp can dissipate indefinitely — which for a 100 Ω shunt and 24 V means a series resistor doing most of the work.

**Protection goes at the connector, before anything else.** A TVS diode 20 mm downstream of the connector protects the trace, not the circuit — the inductance of those 20 mm lets the transient through before the diode conducts. Place protection first in the signal path, physically as well as schematically.

---

## 11. Design for bring-up

This section is the difference between a board you can debug and a board you throw away. Add all of it.

### 11.1 Jumpers

- **0 Ω jumper (or 2-pin header) in every power rail.** Lets you break the rail to measure current, isolate a subsystem, or inject bench power.
- **Barrier power jumper** — a way to power the isolated island from the main side during bring-up, bypassing the isolated DC-DC. Clearly marked "DEFEATS ISOLATION — BENCH USE ONLY". This lets you measure the ADC's noise floor without the converter, which is exactly the characterization you need.
- **CAN termination jumper**, labelled.
- **BOOT0 jumper** for the ST bootloader.

### 11.2 Test points

Put a test point on everything you might want to probe. **A test point you didn't add is a bodge wire you will add**, and bodge wires on a 0.5 mm pitch LQFP are miserable.

Minimum set: every power rail, VBUS, CC1, CC2, the buck's switch node, crystal output (via MCU MCO, not the crystal pin — see below), CANH, CANL, CAN_TX, CAN_RX, every SPI line on both sides of the isolator, every ADC input, the ADC reference, and both grounds.

Use proper loop test points or at minimum a 1 mm exposed pad, not a via you hope to hook onto.

### 11.3 Debug access

- **A real SWD header** (2.54 mm, not Tag-Connect) on a first board. You will be plugging and unplugging it hundreds of times.
- **A UART broken out** for printf. The G4 has plenty. Route it to a header.
- **LEDs**: one per power rail, one user LED, one for CAN activity. Cheap, and invaluable when nothing works.
- **Spare GPIO broken out to a header** — for toggling pins to measure timing on a scope, which is how you'll debug the PD state machine.

### 11.4 Silkscreen

Silkscreen is documentation that can't get lost. Label: every connector and its pinout, every jumper and what it does, the isolation barrier, every test point, the board name and revision, and a warning that isolation is functional-only.

Put your name and a date on it. Six months from now you'll want to know which revision you're holding.

---

## 12. Manufacturing

### 12.1 Fab

4-layer, 1.6 mm, HASL or ENIG, no controlled impedance (section 9.2), no slot (with functional isolation). This is the cheapest tier of 4-layer at every major fab — expect roughly $10–60 for five boards plus shipping, with a one-to-two-week turnaround.

ENIG costs a little more than HASL and is worth it: flat pads make fine-pitch parts much easier to place and solder, and it doesn't oxidize while the boards sit in a drawer between revisions.

### 12.2 Assembly

**Options:**

| Approach | When |
|---|---|
| Fully hand-assembled | You have a hot plate or hot air, and every part is LQFP/SOIC/0603 |
| Fab-assembled (PCBA) | Faster, but constrains you to their parts library |
| Hybrid | Fab assembles the fine-pitch parts, you hand-solder connectors and DNP options |

**If you plan to use fab assembly, check part availability in their library during part selection, not after.** Discovering that your chosen isolator isn't stocked, after the layout is done, means either a respin or hand-soldering it anyway.

**Design for hand rework regardless.** Use 0603 rather than 0402 where space allows. Use LQFP rather than QFN. Leave a couple of millimetres around fine-pitch parts for hot air. You will rework this board.

### 12.3 Order spares

Order at least five boards (they're nearly free at that quantity) and **order 3–5× the components**. You will destroy parts. Losing a week waiting for a replacement MCU because you ordered exactly one is a bad way to spend a week.

---

## 13. Tools and setup

| Tool | Notes |
|---|---|
| **KiCad** | Free, and genuinely capable for a 4-layer board of this complexity. Has a built-in impedance calculator, 3D viewer, and interactive router. Use a current stable release and stay on it for the whole project — file-format churn between major versions is annoying mid-project. |
| Altium / Fusion Electronics | Fine if you have access, but nothing here needs them |
| **Bench supply with current limit** | **The single most important tool.** Set 100 mA before first power-on; a short then dissipates nothing instead of releasing the magic smoke. |
| Oscilloscope, ≥100 MHz, 2+ channels | For the buck switch node, CAN waveforms, crystal startup |
| Multimeter | Continuity, rails, isolation resistance |
| USB-C PD analyzer/tester | Inline power meters that display the negotiated PDO cost very little and turn PD debugging from guesswork into observation. Strongly recommended. |
| USB-CAN adapter | To talk to your CAN interface from a PC |
| ST-Link V3 | SWD debug |
| Hot air station + flux + fine tweezers | Assembly and rework |
| Microscope or good magnifier | Inspecting 0.5 mm pitch solder joints. Non-optional. |
| Thermal camera (optional) | Finds shorts and overloaded parts instantly. Cheap phone attachments work well enough. |

For library management: **build your own footprints for anything critical, or verify vendor ones against the datasheet drawing.** See M4.

---

## 14. Milestone ladder

Hardware has a brutal iteration cost — every mistake is two weeks and a new board order. The milestone structure therefore front-loads review heavily.

---

### M0 — Requirements
**Est. 2–3 days**

Answer the five scope questions in 2.3 and write them down. Isolation class, CAN isolation, sensor types, USB data scope, power budget including what PD is actually for.

**Done when:** you can state the isolation class and creepage in millimetres, and justify why PD is on the board.

---

### M1 — Part selection and block diagram
**Est. 1 week**

Every major part chosen, datasheets read (not skimmed), power budget filled in, isolator direction count confirmed, availability checked at your distributor *and* your assembly house.

**Done when:** you have a BOM with real part numbers and stock, and a block diagram with every rail and every signal crossing the barrier drawn.

---

### M2 — Schematic capture
**Est. 2 weeks**

Draw it block by block, one sheet per subsystem: power, MCU, USB-C, CAN, isolated AFE. Reference the vendor reference designs constantly — ST, TI, and ADI all publish schematics for exactly these blocks, and deviating from them without a reason is how you find out why they did it that way.

**Done when:** ERC passes clean, every net is named, and every part has a value and a footprint assigned.

---

### M3 — Schematic review ★ **the highest-ROI milestone in the project**
**Est. 3–4 days**

A bug caught here costs an hour. The same bug caught after fabrication costs two weeks and a board order.

Do all of:
- **Read every net yourself, out loud, against the datasheet.** Every pin of every IC. Yes, all of them.
- **Check every decoupling cap** exists and is on the right rail
- **Check every pull-up/pull-down** — especially BOOT0, NRST, and CS lines
- **Check power sequencing** requirements in each datasheet
- **Verify the isolation barrier in the schematic** — every net that crosses goes through an isolator
- **Have another person review it.** If you don't have someone, post it somewhere public. The embedded community reviews schematics generously and catches things you're blind to.

---

### M4 — Footprint verification ★ **the #1 cause of dead first boards**
**Est. 2–3 days**

Footprint errors are the single most common reason a board arrives and doesn't work, and they're completely preventable.

- **Check every footprint against the datasheet's mechanical drawing.** Pad size, pitch, courtyard, pin 1 location.
- **Print the board 1:1 on paper and place the actual physical components on it.** This catches wrong-package errors and connector footprint mistakes in about ten minutes. It feels silly and it works.
- **Check connector orientations** — a USB-C footprint mirrored is a dead board
- **Check pin 1 markers** on every polarized part
- **Verify the crystal footprint** matches the actual crystal package

---

### M5 — Stackup and floorplan
**Est. 2 days**

Fix the stackup with your chosen fab. Place the barrier. Place connectors on edges. Place the major blocks per section 9.3. **Do not start routing yet.**

**Done when:** every major component is placed and you could explain the reason for each position.

---

### M6 — Layout
**Est. 2–3 weeks**

Order of operations:

1. Power first — buck loop tight, decoupling caps within a couple of millimetres of their pins
2. Critical nets — crystal, CC lines, the buck switch node
3. Differential pairs — USB, CAN
4. Everything else
5. Pour grounds, verify L2 continuity
6. **Verify the barrier in 3D view**

**Done when:** DRC is clean, L2 is solid, and no copper crosses the barrier.

---

### M7 — Design review and fab output
**Est. 3–4 days**

- DRC against the fab's actual capability file, not defaults
- Visual inspection of every layer individually
- 3D render inspection — catches mechanical collisions and missing parts
- Generate Gerbers, drill files, pick-and-place, BOM
- **Open the Gerbers in a separate viewer.** Never send output you haven't inspected in a different tool than the one that generated it.

---

### M8 — Order
**Est. 1–2 weeks lead time**

Boards, stencil if hand-assembling with paste, components (3–5× quantity).

Use the wait productively: write the bring-up plan (section 15), set up the firmware project, get blinky running on a Nucleo with the same MCU so the toolchain is proven before your board arrives.

---

### M9 — Bring-up
**Est. 2 weeks**

Follow section 15 exactly. Do not skip steps because you're excited.

---

### M10 — Characterization
**Est. 1 week**

Section 16. Produce actual numbers.

---

### M11 — Revision B
**Est. 2–3 weeks**

**Plan for this from the start.** Keep a running list of every bodge, every "I wish I'd added a test point there," and every marginal value from the moment you power the first board. Rev B with a documented change list is a sign of competence, not failure — nobody gets a board this complex right first time, and the people reviewing your work know that.

---

## 15. Bring-up procedure

**Never plug a new board into USB first.** Follow this order.

### Stage 0 — Before power

1. **Inspect under magnification.** Solder bridges on fine-pitch parts, tombstoned passives, missing parts, wrong orientations.
2. **Continuity check every rail to ground.** A short here means something is wrong; find it before applying power, not after.
3. **Measure isolation resistance across the barrier.** Should read open (>10 MΩ). If it doesn't, you have a copper bridge — find it now.
4. **Verify no rail is shorted to any other rail.**

### Stage 1 — Power, from a bench supply

5. **Inject 5 V on VBUS from a bench supply with a 100 mA current limit.** Not USB. If something is wrong, the supply limits and nothing dies.
6. Watch the current. Expect tens of milliamps. Hundreds means a fault — power down immediately.
7. **Measure every rail.** 5 V, 3V3_D, 3V3_A. Check the rail LEDs.
8. **Feel for hot parts.** Or use a thermal camera if you have one.
9. Raise the input to 12 V, then 20 V, checking rails and temperatures at each step.

### Stage 2 — MCU

10. **Connect the ST-Link, read the device ID.** If this works, your MCU is alive, powered, and correctly wired — a huge milestone.
11. **Blink an LED.**
12. **Verify the crystal.** Probing the crystal pins directly loads them and can stop oscillation — instead, output the system clock on MCO and measure that. Compare to your expected frequency and check the error is within CAN's tolerance.

### Stage 3 — USB-C

13. **First with a dumb 5 V source.** A USB-A to C cable from an old charger. The board should power up at 5 V. This validates the connector, the protection, and the power path without any PD involved.
14. **Then a PD source**, with an inline PD analyzer if you have one. Watch what gets negotiated.
15. **Scope VBUS during negotiation** to see the voltage transitions.
16. **Read the negotiated voltage** through your VBUS divider and confirm it matches what the analyzer says.

### Stage 4 — CAN

17. **Internal loopback mode first** — no transceiver involved. Proves the FDCAN peripheral and your bit-timing configuration.
18. **Loopback through the transceiver** — proves the transceiver and its wiring.
19. **Two nodes on a real bus** with proper termination, short cable.
20. **Scope CANH/CANL.** Check levels, edges, and that recessive sits around 2.5 V on both lines.
21. **Long cable, high bit rate, monitor error counters.** This is where a marginal crystal shows up.

### Stage 5 — Isolated front end

22. **Power the island from the bench through the barrier jumper**, isolated DC-DC not populated or disabled. Verify 3V3_ISO.
23. **Talk to the ADS1220 over SPI.** Read a register with a known reset value — this proves the isolator and the SPI wiring in both directions.
24. **Short the inputs and read.** This is your noise floor with clean power — your best case.
25. **Apply a known current** from a bench source through the shunt. Verify the reading.
26. **Now enable the isolated DC-DC** and repeat the noise measurement. **The difference is your coupling**, and it's the number section 16 wants.
27. **Real sensor.** A 4–20 mA transmitter, or a precision current source standing in for one.

---

## 16. Characterization

Producing numbers is what separates "I built a board" from "I engineered a board." All of these are a few hours of work each.

### ADC noise floor

Short the inputs, take 1000 samples, compute RMS noise in LSBs, convert to noise-free bits:

```
noise_free_bits = log2(full_scale_range / (6.6 × rms_noise_volts))
```

Report it both with the isolated DC-DC running and with the island bench-powered. **Publish both numbers and the difference.** The delta is the quantified cost of isolation, and almost nobody measures it.

### Accuracy

Sweep a precision current source across 4–20 mA. Plot measured versus applied. Extract gain error, offset error, and INL. Repeat after a temperature soak if you can — drift is where the shunt's tempco shows up.

### Power

Efficiency of the buck at 5 V, 12 V, and 20 V input across your load range. Quiescent current with the MCU in stop mode. Isolated DC-DC efficiency.

### Thermal

Full load, 20 V input, after 30 minutes. Identify the hottest component. If anything is above about 85 °C ambient-adjusted, you have a problem to solve in rev B.

### CAN

Error counters over an hour at maximum bit rate on your longest cable. Scope the differential waveform; check the eye is open and edges are clean.

### PD

Log every negotiated PDO against a range of chargers — a dumb 5 V brick, a 60 W laptop charger, a phone charger, a multi-port hub. **Compatibility across sources is the real test of a PD implementation**, and a table of "works / doesn't work / negotiates what" across a dozen chargers is genuinely useful documentation.

---

## 17. Stretch goals

| Feature | Effort | Value |
|---|---|---|
| **RTD support** | Small | The ADS1220's IDACs are already there. A few resistors and firmware. |
| **Thermocouple input** | Medium | Needs cold-junction compensation; a dedicated part like MAX31856 sidesteps most of it |
| **SD card logging** | Small | "Sensor logging" in the title implies storage. SDIO or SPI. |
| **RTC + coin cell** | Small | Timestamps make logged data actually useful |
| **Ethernet / Modbus TCP** | Large | The G4 has no MAC; needs W5500 or similar over SPI |
| **RS-485 / Modbus RTU** | Small | Far more common in industrial than CAN. Arguably should be in v1. |
| **Isolated CAN** | Medium | Second isolation domain; genuinely needed for plant-wide buses |
| **Enclosure + DIN rail mount** | Medium | Turns a dev board into something that looks like a product |
| **4-channel simultaneous sampling** | Medium | The ADS1220 muxes; simultaneous needs multiple ADCs |
| **PoE** | Large | A different power architecture entirely |
| **EMC pre-compliance scan** | Medium | A near-field probe and a cheap spectrum analyzer will find your isolated DC-DC's harmonics. Educational and genuinely impressive. |

Two of those deserve a second look. **RS-485/Modbus RTU is more widely deployed in industrial sensing than CAN** — if the goal is a board people would actually use, it may belong in v1 rather than the stretch list. And **EMC pre-compliance** is the kind of thing almost no hobby project attempts; even a rough near-field scan showing the isolated converter's harmonics, before and after adding the Y-cap, is a compelling result.

---

## 18. References

### Datasheets and app notes to have open

| Document | For |
|---|---|
| STM32G4 reference manual (RM0440) | UCPD, FDCAN, ADC, clock tree |
| STM32G4 datasheet | Pin assignments, electrical limits, HSI accuracy |
| AN5225 / AN5418 (ST, USB-C PD) | UCPD implementation and the TCPP01 reference design |
| TCPP01-M12 datasheet | CC protection, dead-battery behaviour, external FET drive |
| ADS1220 datasheet | Especially the layout and grounding section |
| ADuM415x SPIsolator datasheet | Channel directions, clock delay compensation |
| Your CAN transceiver's datasheet | Bus fault ratings, slope control |
| USB Type-C Cable and Connector Specification | Rd values, capacitance limits, CC detection |
| USB Power Delivery Specification | PDO structure, negotiation state machine |
| ISO 11898-2 | CAN physical layer, termination, bus topology |
| IEC 62368-1 | Creepage and clearance tables, if you go beyond functional isolation |

### Design guidance worth reading before you start

- **Analog Devices AN-0971**, "Recommendations for Control of Radiated Emissions with iCoupler Devices" — directly addresses the isolated DC-DC EMI problem in section 8.4
- **TI application notes on isolated power supply design** — transformer selection, snubbers, spread spectrum
- **Henry Ott, *Electromagnetic Compatibility Engineering*** — the reference on grounding, plane splits, and return paths. Read the chapters on grounding before you lay out the isolation barrier.
- **Howard Johnson, *High-Speed Digital Design*** — return path and reference plane theory, even though this board isn't fast
- **Rick Hartley's talks on PCB grounding and stackup** — widely available and excellent; the material on why a solid L2 plane matters is directly applicable here
- **Phil's Lab** (YouTube) — practical STM32 PCB design walkthroughs at roughly this level of complexity

### Reference designs to study

- ST's **NUCLEO-G474RE** schematic — MCU support circuitry done correctly
- ST's **STEVAL-2STPD01** or similar USB-C PD sink reference — TCPP01 + UCPD wiring
- TI's isolated **4–20 mA input** reference designs (TIDA series) — front-end protection and scaling
- Any **CAN transceiver EVM** schematic for termination and protection

---

## Appendix A — Decision record

| Decision | Rationale |
|---|---|
| STM32G4 (not F4) | UCPD and FDCAN exist in silicon on G0/G4/L5/U5/H5 and do not exist on F1/F4/F7. The MCU choice gates two of three headline features. |
| LQFP, not QFN | Every pin visible, probeable, and reworkable. On a first board you *will* bodge. |
| Functional isolation, ~2.5 mm, no slot | 24 V industrial sensors are nowhere near hazardous voltage. Reinforced costs ~8 mm creepage plus a slot for protection you don't need. |
| Wide-body isolator footprints anyway | Accepts narrow parts; leaves a reinforced v2 open without a re-layout |
| SPR only, never request EPR | EPR reaches 48 V but is opt-in. Staying in SPR caps worst case at 20 V and makes the input stage far easier. Enforce in firmware as an invariant. |
| TCPP01-M12 for CC protection | Solves dead-battery Rd *and* CC overvoltage in one part. Without dead-battery Rd the board never powers on — a silent, undebuggable brick. |
| ≤10 µF pre-contract, bulk on a load switch | Type-C bounds sink bypass capacitance; large bulk causes inrush faults and negotiate/brownout oscillation |
| Crystal, not HSI | HSI is ~±1% and drifts; CAN typically needs ≤0.5%. A board that works on the bench and throws error frames in a warm cabinet is the classic symptom. |
| Split termination (2×60 Ω + 4.7 nF), jumpered | Same 120 Ω differential plus a common-mode path; meaningfully lower emissions for one extra part |
| 4–20 mA for v1 | Simplest front end (one resistor), most industrial, inherently noise-immune. Leaves engineering effort for the isolation problem, which is the hard one. |
| ADS1220 | 24-bit, 4-channel mux, internal PGA and reference, IDACs for future RTD support. One chip is the entire isolated measurement side. |
| Off-the-shelf isolated DC-DC for v1 | Removes transformer selection and EMI tuning from the first board. Discrete SN6505 + transformer is a v2 improvement. |
| LDO post-regulation on the isolated rail | Feeding a 24-bit ADC directly from a switching converter wastes most of its resolution |
| Isolated DC-DC placed far from the ADC | Purely a floorplan decision, and the single biggest lever on noise floor. Cannot be fixed after layout. |
| Expect ~18–19 noise-free bits, not 24 | Datasheet figures assume bench supplies. Measure the delta and publish it. |
| No controlled impedance | G4's USB is full-speed (12 Mbps); rise times make the critical length far longer than any trace here. Saves cost and lead time. |
| Solid, unbroken L2 ground plane | The highest-value layout decision on the board; splits on L3 are fine because L2 provides continuous return |
| Barrier cuts all four layers, verified in 3D | A plane bridging the barrier defeats isolation entirely and is invisible layer-by-layer |
| Y-cap across the barrier | Gives common-mode current a short return instead of routing it through the measurement |
| 0 Ω jumpers in every rail + barrier bypass jumper | Enables staged bring-up and the noise-coupling measurement. Without these the board must come up all at once. |
| Test points everywhere | A test point you didn't add is a bodge wire on 0.5 mm pitch |
| Bench supply with current limit for first power-on | A short dissipates nothing instead of destroying parts |
| Plan for rev B from day one | Nobody gets a board this complex right first time; a documented change list is a sign of competence |

---

## Appendix B — Quick reference card

```
MCU          STM32G474RET6  LQFP64  (UCPD + 3× FDCAN)
             F1/F4/F7 have NEITHER — do not use

USB-C sink
  Rd         5.1 kΩ on CC1 and CC2
  DEAD BATTERY: Rd must exist with MCU UNPOWERED
               → TCPP01-M12, or discrete Rd, or STUSB4500
  Pre-contract VBUS cap  ≤ 10 µF   (bulk behind a load switch)
  SPR max    20 V   ← never request EPR
  EPR max    48 V   ← opt-in only; don't
  Input stage rated 40 V minimum
  ALWAYS accept 5 V so dumb chargers work

CAN
  Crystal REQUIRED  (HSI ±1% ≫ CAN's ~0.5% need)
  Termination 120 Ω at each bus END only — jumper it
  Split term  60 Ω + 60 Ω, 4.7 nF from midpoint to GND
  DE-9 pinout  2 = CANL   7 = CANH   3 = GND
  TVS at the connector, common-mode choke footprint

Isolated AFE
  4–20 mA → 100 Ω 0.1% shunt → 0.4–2.0 V  (fits 2.048 V ref, PGA=1)
  Shunt power at 20 mA = 40 mW
  Protect for CONTINUOUS 24 V fault, not just transient
  Iso DC-DC placed FAR from the ADC
  LDO post-regulate the isolated rail
  Expect 18–19 noise-free bits, not 24

Isolation
  Functional   ~1–2 mm creepage, no slot
  Basic        ~5 mm
  Reinforced   ~8 mm + slot
  Barrier cuts L1 L2 L3 L4 — verify in 3D, not layer-by-layer
  Y-cap 100 pF–1 nF across the barrier, near the transformer

Stackup      L1 sig / L2 SOLID GND / L3 power / L4 sig   1.6 mm
             No controlled impedance needed (USB FS only)

Bring-up order
  continuity → isolation resistance → bench 5 V @ 100 mA limit
  → rails → SWD ID → blink → crystal via MCO
  → dumb 5 V USB → PD source → CAN loopback → CAN bus
  → island on bench power → ADC noise floor
  → island on iso DC-DC → noise floor again   (delta = coupling)
```
