# STM32G474 Industrial Sensor Logger — v1

A 4-layer board with USB-C Power Delivery, CAN FD, and an isolated 24-bit
analog front end for 4–20 mA industrial sensors — implemented from
[`Documentation/README.md`](../Documentation/README.md).

> **Functional isolation only. NOT rated for mains-referenced sensors.**
> The barrier power jumper defeats isolation and is for bench use only.

```
make            # every check: 57 firmware tests, 66 design rules, 29 tool tests
make target     # cross-compile for the STM32G474RET6
make report     # the tables to have on the bench before the board arrives
```

---

## What is here

```
firmware/
  core/          portable C11 — PD policy, CAN bit timing, ADS1220, analog
                 scaling, VBUS monitoring. No vendor headers. Runs on the
                 host under test and on the target unchanged.
  target/        STM32G474 register map, clock tree, and drivers
  app/           the staged bring-up self-test
  tests/         57 host unit tests
hardware/
  board.yaml     the board as machine-readable data
  rules/         the design document's rules as executable tests, plus
                 mutation tests that prove each rule actually fires
  bom.md         the parts where the choice matters
tools/           characterization: noise floor, accuracy sweep, PD matrix,
                 power budget, and an independent CAN timing solver
docs/            bring-up procedure, characterization template, and notes
                 on where this diverges from the design document
```

## The organising idea

**Every claim in the design document that could be wrong is a test.**

The document is full of assertions — "you need a crystal", "keep pre-contract
capacitance under 10 µF", "count your isolator directions", "a competently
executed board gets 18–19 noise-free bits". Each of those is something a board
can quietly violate, and most of them are invisible until the hardware exists.
So each one is written down somewhere a build can check it:

| The document says | Checked by |
|---|---|
| CAN needs a crystal; the HSI16 is not good enough (§7.2) | `can_timing.c` computes the ISO 11898-1 tolerance; tests assert HSI16 fails at *every* standard rate |
| Never request EPR; stay under 20 V (§6.1) | `pd_policy.c` refuses EPR PDOs structurally; `vbus_classify()` flags >21 V as a violated invariant |
| Dead-battery Rd must not depend on the MCU (§6.2) | a design rule on `board.yaml`, and the handover order in `gpio.c` |
| ≤10 µF of bypass before a contract (§6.3) | `pd_policy_bulk_enable_allowed()`, and a design rule |
| 3 forward + 2 reverse isolator channels (§4.3) | a design rule derived from the signal chain, not from the part's own spec |
| Every rail gets a jumper, a test point and an LED (§11) | design rules over the enumerated lists in `board.yaml` |
| 4–20 mA through 100 Ω fits the 2.048 V reference (§8.2) | asserted from both directions — in `afe.c` and in the design rules |
| Below 3.6 mA is a fault, not a reading (NE43) | `afe_convert()` returns `AFE_FAULT_LOW` and still reports the value |

And because a rule suite that has only ever seen a passing board might be
asserting nothing, `hardware/rules/test_rules_catch_violations.py` breaks the
board 32 different ways and asserts each rule catches its own violation. That
found a real weakness on the first run: the isolator-channel rule was only
comparing two fields against each other, so a substitution that updated both
would sail through. It now derives the requirement from the signal chain.

## The C/Python cross-check

`firmware/core/src/can_timing.c` picks the FDCAN register values that reach
the silicon. If it is wrong, every frame on the bus is wrong, and the symptom
— occasional errors that get worse with cable length and temperature — is
indistinguishable from a dozen other problems.

So the bit timing is computed twice: once by the C solver (a local search,
small enough to run on the target) and once by `tools/can_timing.py` (an
exhaustive search over every legal combination). The two share a written
ranking specification and nothing else.
`tools/tests/test_can_timing_crosscheck.py` compares them across 180
combinations of clock, bit rate and sample point.

**It found two real bugs.** A sample-point tie band wide enough to return
82.7% when asked for 87.5%, and a search that only ever rounded the
time-quanta count down. Details in
[`docs/notes-on-the-spec.md`](docs/notes-on-the-spec.md).

## What the board actually runs at

```
      rate    phase  brp  tseg1  tseg2  sjw   tq      SP  err ppm  tol ppm  crystal HSI16
   125,000  nominal    5    237     34   34  272   87.5%        0     4854  OK       FAIL
   250,000  nominal    5    118     17   17  136   87.5%        0     4854  OK       FAIL
   500,000  nominal    2    148     21   21  170   87.6%        0     4796  OK       FAIL
 1,000,000  nominal    1    148     21   21  170   87.6%        0     4796  OK       FAIL
 2,000,000     data    5     13      3    3   17   82.3%        0     6880  OK       FAIL
 5,000,000     data    1     26      7    7   34   79.4%        0     8045  OK       FAIL
```

Every rate exact, and the HSI16 column is section 7.2 as a fact rather than a
recommendation: there is no standard bit rate at which skipping the crystal is
acceptable.

## Firmware

The split is deliberate: **no policy lives below the driver line.** What
voltage to ask a source for, what bit timing to program, what 3.5 mA means —
all of that is in `core/`, which has no vendor headers, compiles for the host,
and is covered by 57 tests. `target/` moves bytes and toggles pins. When a
decision starts creeping into a driver, that is the signal it belongs in
`core/`.

```
$ make firmware
pd_policy:   14 tests, 0 failed
can_timing:  13 tests, 0 failed
ads1220:     13 tests, 0 failed
afe + vbus:  17 tests, 0 failed

$ make target
   text    data     bss     dec     hex
  18444      96    9704   28244    6e54   build/target/logger.elf
```

The register map in `target/inc/stm32g474.h` is hand-written rather than
CMSIS, so every register touched is one somebody chose to touch. A hand-written
map goes wrong in exactly one way — a reserved array off by a word, silently
shifting everything after it — so there are 40 compile-time `offsetof`
assertions that make that impossible to do unnoticed.

`app/src/main.c` is section 15's bring-up procedure as a program: it runs the
stages in order, stops at the first failure (because a CAN loopback failure
means nothing if the crystal was never proved), and prints numbers rather than
verdicts.

## Characterization

Section 16 asks for numbers, and these produce them:

- **`noise_floor.py`** — the `log2(FSR / 6.6 × rms)` formula, and the
  comparison the document says almost nobody makes: island on bench power
  versus island on the isolated converter. The difference is the quantified
  cost of isolation.
- **`accuracy.py`** — gain error, offset error and INL, reported separately,
  because two of those are free to fix in firmware and the third is the
  board's actual accuracy limit.
- **`pd_matrix.py`** — the board's own policy against fourteen real sources,
  including the EPR chargers whose 28 V and 48 V PDOs must be ignored while
  still taking their 20 V.
- **`power_budget.py`** — and the answer to section 5.2's real question: the
  board draws 1.16 W, so PD is not justified by the board. The 4–20 mA loop
  supply is 2.26 W, and that is why PD is here.

## Before ordering

One test is `xfail` on purpose:

```
test_fab_stackup_has_been_confirmed
  Section 9.1: ask the fab for their actual stackup. Different fabs ship
  wildly different dielectric spacing under the same "4-layer 1.6 mm"
  description. Flip this to True once confirmed, before ordering.
```

It is `strict`, so it will start failing the moment somebody sets the flag
without meaning to — and it is the last thing standing between the design
rules and a fab order.
