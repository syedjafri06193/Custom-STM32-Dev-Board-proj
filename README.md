# Custom STM32 Dev Board — Industrial Sensor Logger

[![CI](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/actions/workflows/ci.yml/badge.svg)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/actions/workflows/ci.yml)

4-layer STM32G474 board with USB-C Power Delivery, CAN FD, and an isolated
24-bit analog front end for 4–20 mA industrial sensor logging.

> **Functional isolation only. Not rated for mains-referenced sensors.**
> See [`1—Documentation/design.md`](1—Documentation/design.md) for the full rationale and limits.

## Repository layout

```
.
├── README.md          ← you are here
├── 1—Documentation/   ← all documentation (index in its README)
│   ├── design.md      ← full design guide (the spec code comments cite)
│   ├── design.pdf     ← same guide, PDF
│   └── v1/            bring-up procedure, characterization template, spec errata
└── v1/                ← first implementation
    ├── firmware/      portable C core, STM32G474 target drivers, bring-up app, host tests
    ├── hardware/      board.yaml, executable design rules, BOM notes
    └── tools/         characterization: noise floor, accuracy, PD matrix, power, CAN timing
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

## Feedback

Feedback, bug reports and ideas are welcome.

- **Hardware or firmware issues:** [open an issue](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/issues/new) and include the board revision, how it's powered, the toolchain or IDE you used, and photos or scope captures if they help show the problem.
- **Ideas or questions:** start a thread in [Discussions](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/discussions) (enable it under Settings → General → Features).
- **Design changes:** pull requests to the schematic, layout or firmware are appreciated; please describe what you changed and whether it's been tested on real hardware.

<!-- project-metrics:start -->
## Project Metrics

[![Stars](https://img.shields.io/github/stars/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/stargazers) [![Forks](https://img.shields.io/github/forks/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/forks) [![Open issues](https://img.shields.io/github/issues/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/issues) [![Last commit](https://img.shields.io/github/last-commit/syedjafri06193/Custom-STM32-Dev-Board-proj/improvements?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/commits/improvements) [![Commits](https://img.shields.io/github/commit-activity/t/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj/commits/improvements) [![Top language](https://img.shields.io/github/languages/top/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj) [![Repo size](https://img.shields.io/github/repo-size/syedjafri06193/Custom-STM32-Dev-Board-proj?style=flat-square)](https://github.com/syedjafri06193/Custom-STM32-Dev-Board-proj)

### Code

| Metric | Value |
|---|---|
| Lines of code | 3,332 |
| Source files | 29 |
| Tracked files | 57 |
| Documentation | 8 Markdown files (16,883 words), 1 PDF |
| Commits | 11 |
| Contributors | 1 |
| Active | 2026-09-11 → 2026-10-28 |

| Language | Lines | Share |
|---|---:|---:|
| C | 1,783 | 53.5% |
| Python | 1,188 | 35.7% |
| YAML | 239 | 7.2% |
| Makefile | 122 | 3.7% |

### Performance

| Metric | Result |
|---|---|
| CAN sample point, 125 kbit/s – 1 Mbit/s | 87.5–87.6% |
| Oscillator tolerance per node (ISO 11898-1) | 4796–4854 ppm (≈0.49%) |
| CAN FD 2 Mbit/s | 82.3% sample point, 6880 ppm |

<sub>Computed by the project's code, recorded in [`1—Documentation/v1/notes-on-the-spec.md`](1%E2%80%94Documentation/v1/notes-on-the-spec.md).</sub>

<sub>Code stats as of 2026-10-28, counted with pygount (comments and blank lines excluded). Badges update live.</sub>
<!-- project-metrics:end -->
