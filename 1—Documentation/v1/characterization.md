# Characterization — rev A

Section 16's numbers. **Empty until the board exists**; this is the template,
with the predictions already filled in so the measurements have something to
be compared against rather than merely recorded.

Writing the prediction down first is the point. A measurement with nothing to
compare it to is a number; a measurement against a prediction is either a
confirmation or a question.

---

## ADC noise floor

```
noise_free_bits = log2(full_scale_range / (6.6 × rms_noise_volts))
```

FSR at PGA = 1 with the 2.048 V internal reference is 4.096 V peak to peak.

| Condition | Predicted | Measured | RMS noise |
|---|---|---|---|
| Inputs shorted, island on bench jumper | **19.0 bits** | — | — |
| Inputs shorted, isolated DC-DC running | **17.5 bits** | — | — |
| **Cost of isolation** | **1.5 bits** | — | — |

Predictions from `board.yaml` → `afe.expected_noise_free_bits`, which the
design rules check for plausibility (anything above ~21 is the mistake section
8.4 exists to prevent).

```
# 1000 samples each, then:
python3 tools/noise_floor.py bench.txt iso.txt --compare \
    --label "bench power" --label "isolated DC-DC"
```

**Report both numbers and the difference.** The delta is the quantified cost
of isolation and almost nobody measures it.

Note which convention is being quoted: this is *noise-free* resolution. The
ADS1220 datasheet quotes *effective* resolution, which is always exactly
`log2(6.6) = 2.72` bits higher. The same board is honestly "18.5 noise-free"
or "21.2 effective", and only one of those is what a single reading is good to.

## Accuracy

Sweep a precision current source across 4–20 mA in 500 µA steps. Plot measured
against applied.

| Figure | Predicted | Measured | Notes |
|---|---|---|---|
| Gain error | ≤3500 ppm | — | Removable in firmware (`afe_channel_cfg_t.gain_ppm`) |
| Offset error | — | — | Removable in firmware (`offset_uv`) |
| **INL (best fit)** | — | — | **Not removable.** This is the accuracy limit. |
| Worst raw error | — | — | |
| After two-point cal | — | — | Should equal INL |

Error budget from `afe_error_budget_ppm()`, which is the same sum
`tools/accuracy.py --budget` computes:

| Contributor | ppm |
|---|---|
| Shunt tolerance (0.1%) | 1000 |
| Shunt tempco, 25 ppm/°C over 40 °C | 1000 |
| ADS1220 internal reference | 2000 |
| ADC gain error | 500 |
| **Total** | **4500 (0.45% of span)** |

Worth noticing: **the shunt's tempco alone is as large as its tolerance** once
the board warms up. Buying a 0.05% shunt without also buying a better tempco
moves nothing.

```
python3 tools/accuracy.py sweep.csv --budget
```

Repeat after a temperature soak if you can — drift is where the tempco stops
being a datasheet number.

## Power

| Measurement | Predicted | Measured |
|---|---|---|
| Buck efficiency at 5 V in | 88% | — |
| Buck efficiency at 12 V in | 88% | — |
| Buck efficiency at 20 V in | 88% | — |
| Board consumption at 20 V | **58 mA / 1.16 W** | — |
| With 4 loops at 20 mA | **171 mA / 3.41 W** | — |
| Quiescent, MCU in stop mode | — | — |
| Isolated DC-DC efficiency | 75% | — |

```
make report     # prints the full budget, including per-voltage current
```

The number section 5.2 cares about: the board alone is **1.16 W**, which a
5 V/500 mA port would run. The loop supply is **2.26 W**, and that is the
entire justification for PD being on this board.

## Thermal

Full load, 20 V input, 30 minutes.

| Part | Predicted | Measured |
|---|---|---|
| 3V3_D LDO | warm (0.26 W) | — |
| 3V3_A LDO | ambient (0.03 W) | — |
| Buck | warm (0.14 W) | — |
| Isolated DC-DC | — | — |
| Shunt (×4, at 20 mA) | 40 mW each | — |
| **Hottest component** | — | — |

Anything above ~85 °C ambient-adjusted is a rev B problem.

## CAN

| Measurement | Predicted | Measured |
|---|---|---|
| Bit rate error at 500 kbit/s | **0 ppm** | — |
| Sample point | **87.6%** | — |
| Oscillator tolerance available | **4796 ppm** | — |
| Crystal actual error (from MCO) | ≤30 ppm | — |
| TEC/REC after 1 h at max rate, longest cable | 0 / 0 | — |
| Recessive level, CANH and CANL | ~2.5 V | — |

`make report` prints the solved timing for every standard rate. The margin
here is enormous — 4796 ppm available against 30 ppm used — which is exactly
section 7.2's point: the crystal is not a close call, and the HSI16 at
10 000 ppm is not a near miss either.

## PD

**Compatibility across sources is the real test of a PD implementation.**

Predicted behaviour for fourteen sources is in
`python3 tools/pd_matrix.py --markdown`. Fill in the measured column from a PD
analyzer.

| Source | Predicted | Measured | Notes |
|---|---|---|---|
| USB-A brick + A-to-C | 5 V @ 2.1 A, mismatch | — | |
| Legacy 5 V/500 mA | 5 V @ 0.5 A, mismatch | — | loop supply unavailable |
| 18 W phone charger | 9 V @ 2 A, mismatch | — | |
| 30 W phone charger | 15 V @ 2 A, mismatch | — | |
| 45 W laptop charger | 20 V @ 2.25 A | — | |
| 65 W laptop charger | 20 V @ 3.25 A | — | |
| 100 W laptop charger | 20 V @ 5 A | — | |
| Multi-port hub, idle | 20 V @ 3 A | — | |
| Multi-port hub, derated | 9 V @ 2 A, mismatch | — | |
| Car adapter, 5 V only | 5 V @ 3 A, mismatch | — | |
| **140 W EPR charger** | **20 V @ 5 A** | — | **28 V PDO ignored** |
| **240 W EPR charger** | **20 V @ 5 A** | — | **28/36/48 V ignored** |
| Non-standard 12 V supply | 12 V @ 3 A, mismatch | — | no 5 V PDO, still works |
| Overloaded 12 V port | no contract | — | the only genuine failure |

The two rows that matter most are the EPR chargers. **The board must take
their 20 V and ignore everything above it.** If an analyzer ever shows a
contract above 20 V, stop: the SPR invariant is broken, and every part on the
input side was chosen on the assumption that it holds.

`vbus_classify()` reports `OVER 21 V -- SPR invariant violated` if it ever
sees one, so the board will say so itself.

---

## Summary line for the project write-up

Fill in once the four measurements above exist:

> 24-bit isolated 4–20 mA logger. **__ noise-free bits** on bench power,
> **__ bits** with the isolated converter running — a **__ bit** cost of
> isolation, measured rather than assumed. **__ ppm** accuracy after
> two-point calibration, limited by INL. CAN FD at 2 Mbit/s with 0 ppm bit
> rate error and **__ ppm** of oscillator margin. Powers up on **__ of 14**
> USB-C sources including EPR chargers, never leaving SPR.
