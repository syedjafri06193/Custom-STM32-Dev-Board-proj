# Custom STM32 Dev Board — Industrial Sensor Logger

4-layer STM32G474 board with USB-C Power Delivery, CAN FD, and an isolated
24-bit analog front end for 4–20 mA industrial sensor logging.

> **Functional isolation only. Not rated for mains-referenced sensors.**
> See [`docs/design.md`](docs/design.md) for the full rationale and limits.

## Repository layout

```
.
├── README.md          ← you are here
├── docs/
│   ├── design.md      ← full design guide (the spec code comments cite)
│   └── design.pdf     ← same guide, PDF
└── v1/                ← first implementation
    ├── firmware/      portable C core, STM32G474 target drivers, bring-up app, host tests
    ├── hardware/      board.yaml, executable design rules, BOM notes
    ├── tools/         characterization: noise floor, accuracy, PD matrix, power, CAN timing
    └── docs/          bring-up procedure, characterization template, spec errata
```

Each `vN/` directory is a self-contained iteration. Start with
[`v1/README.md`](v1/README.md).

## Quick start

Needs a C compiler, Python 3 (pyyaml, pytest); `arm-none-eabi-gcc` for the target build:

```bash
cd v1
make            # 57 firmware tests, 66 design rules, 29 tool tests
make target     # cross-compile for the STM32G474RET6
make report     # bench tables to have on hand before the board arrives
```

## Versions and feedback

| Version | Summary | Feedback |
|---|---|---|
| [v1](v1/) | Firmware core + drivers, machine-checked hardware design rules, characterization tools | — |

Add a row per version as new iterations land.
