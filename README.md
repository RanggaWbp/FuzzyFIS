# FuzzyFIS

**MATLAB-compatible fuzzy inference for microcontrollers** — Mamdani, Sugeno, and
Tsukamoto, `float32`, zero heap, no exceptions. A `.fis` file you tuned in the MATLAB
Fuzzy Logic Toolbox runs on a 2 KB ATmega328P without retuning anything.

[![CI](https://img.shields.io/github/actions/workflow/status/RanggaWbp/FuzzyFIS/ci.yml?branch=main&label=CI)](../../actions)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Arduino](https://img.shields.io/badge/Arduino-avr%20%7C%20esp8266%20%7C%20esp32%20%7C%20stm32-00979D)](https://www.arduino.cc)
[![C++11](https://img.shields.io/badge/C%2B%2B-11-00599C)](#)

---

## Why this exists

The Arduino fuzzy libraries available today support triangle/trapezoid
membership functions, centroid defuzzification, Mamdani only — and they compute
the centroid over a domain *you* choose. MATLAB computes it over a fixed
101-point `linspace` of the output range. That single difference is enough to
make the numbers disagree, which means a controller tuned in MATLAB needs
retuning on the device.

FuzzyFIS closes that gap by treating MATLAB behaviour as a specification, not an
inspiration: same 13 membership functions, same operators, same fold order, same
sample grid, same defaults.

**Measured, not claimed:** the worst relative difference against a
MATLAB-compatible oracle across 20 reference systems is **1.37 × 10⁻⁶** (float32
build) — float32 rounding, nothing else. Build with `-DFZ_USE_DOUBLE` for a
64-bit `double` build whose worst error drops to **4.74 × 10⁻⁹**. The full table
is in [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md).

---

## What's inside

| | |
|---|---|
| **Inference systems** | Mamdani, Sugeno (zero-order and first-order), Tsukamoto |
| **Defuzzification** | `centroid` `bisector` `mom` `som` `lom` `wtaver` `wtsum` `centroid_integral` |
| **Membership functions** | `trimf` `trapmf` `gaussmf` `gauss2mf` `gbellmf` `sigmf` `dsigmf` `psigmf` `zmf` `smf` `pimf` `linsmf` `linzmf` + `constant` `linear` |
| **Operators** | AND/OR: `min` `max` `prod` `probor` `sum` `algebraic_*` `bounded_sum` `einstein_*` `hamacher_*`<br>Implication: `min` `prod`<br>Aggregation: `max` `sum` `probor` |
| **Rules** | weights, `NOT` (negative index), hedges (`somewhat` `very` `extremely` `very very`), per-rule AND/OR |
| **Structure** | any number of inputs, outputs, membership functions, rules |
| **Memory** | no `malloc`, no `new`, no `std::` — one 404-byte work buffer (float32) |
| **Precision** | `float32` default; build with `-DFZ_USE_DOUBLE` for 64-bit `double` (needs hardware double on MCU: ESP32, Cortex-M4F/FPU) |
| **Platforms** | AVR, ESP8266, ESP32, STM32, and anything with a C99 compiler |

### Footprint, measured with `arduino-cli`

Tipper model: 2 inputs, 1 output, 3 rules.

| Board | Static FIS (no parser) | Runtime `.fis` parser |
|---|---|---|
| `arduino:avr:uno` | 9338 B flash / 878 B RAM of 2048 | parser disabled (2 KB SRAM) |
| `arduino:avr:mega` | 9784 B | 21 772 B |
| `esp8266:nodemcuv2` | 246 360 B / 28 944 B of 80 192 | 252 300 B / 30 576 B |
| `STM32F4` | 20 752 B / 1656 B of 131 072 | 26 576 B / 2632 B |
| `STM32F103` (Cortex-M3, no FPU) | 13 614 B / 1472 B | 13 614 B / 1472 B |

`STM32F103` row measured by `make -C tests/stm32f103 run`
(`arm-none-eabi-gcc 14.2.1`, newlib-nano, `-mcpu=cortex-m3 -mthumb`,
soft-float) — same evaluator + parser, mini 1-input model, not the Tipper model
used by the arduino-cli rows. Flash `.text`, RAM `.data+.bss`.

Speed on x86-64 `-O2`: **3.0 µs per evaluation** (~330 000/s) for that model.
Sugeno skips the sample grid and costs a few hundred nanoseconds.

---

## Install

### Arduino IDE

**Add .ZIP Library (works today):**

1. Download [`FuzzyFIS.zip`](../../releases/latest)
2. *Sketch → Include Library → Add .ZIP Library…*
3. Pick the zip. The examples appear under *File → Examples → FuzzyFIS*.

*Library Manager listing is pending submission to the Arduino Library
Registry — until then use the ZIP or a clone below.*

**Manual** — clone into your sketchbook:

```sh
git clone https://github.com/RanggaWbp/FuzzyFIS.git ~/Arduino/libraries/FuzzyFIS
```

### PlatformIO

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = https://github.com/RanggaWbp/FuzzyFIS.git
```

### Arduino CLI

```sh
arduino-cli lib install --git-url https://github.com/RanggaWbp/FuzzyFIS.git
```

`architectures=*`, so no board-specific flags are needed.

---

## Two ways to load a FIS

### A. Generated header — recommended for AVR

Turn the `.fis` into a `static const` table at build time. Costs flash, not RAM.

```sh
python3 tools/fis2h.py tipper.fis -o tipper_fis.h
```

```cpp
#include <FuzzyFIS.h>
#include "tipper_fis.h"      // defines fz_fis_tipper

static fz_work_t work;

void setup() { Serial.begin(115200); }

void loop() {
  const float in[2] = { service, food };   // 0..10 each
  float tip[1];
  fz_eval_all(&fz_fis_tipper, in, tip, &work);
  Serial.println(tip[0], 3);
  delay(500);
}
```

### B. Runtime `.fis` — for boards with RAM to spare

The parser fills caller-owned pools. It never allocates.

```cpp
#include <FuzzyFIS.h>

static const char FIS_TEXT[] =
  "[System]\nName='tipper'\nType='mamdani'\nNumInputs=2\nNumOutputs=1\n"
  "NumRules=3\nAndMethod='min'\nOrMethod='max'\nImpMethod='min'\n"
  "AggMethod='max'\nDefuzzMethod='centroid'\n"
  /* … [Input1] [Input2] [Output1] [Rules] … */;

/* Budget these against your SRAM before choosing this path. */
static fz_mf_t   mf_pool[16];
static fz_var_t  in_pool[4], out_pool[4];
static fz_rule_t rule_pool[8];
static int16_t   idx_pool[32];   // all rule slots, contiguous
static float     lin_pool[16];   // Sugeno `linear` coefficients
static fz_fis_t  fis;
static fz_work_t work;

void setup() {
  int rc = fz_fis_parse_text(FIS_TEXT, sizeof FIS_TEXT, &fis,
                             mf_pool, 16,
                             in_pool, 4, out_pool, 4,
                             rule_pool, 8, idx_pool, 32, lin_pool, 16);
  if (rc != FZ_OK) { /* see FZ_ERR_* below */ return; }
  if (!fz_fis_check(&fis)) { /* indices out of range */ return; }
}
```

Pools can be sized without guessing — do a counting pass first:

```cpp
fz_pool_size_t need;
fz_fis_measure(FIS_TEXT, &need);
// need.n_mf, need.n_in, need.n_out, need.n_rule, need.n_idx, need.n_lin
```

On a board that can read files, `fz_fis_parse_file(path, buf, len, …)` is the
same call with a caller-supplied buffer. It is compiled out under `ARDUINO`.

---

## API

```c
/* Evaluate. `w` is scratch; Mamdani uses it, Sugeno ignores it. */
float fz_eval    (const fz_fis_t *fis, const float *in, uint16_t out_index, fz_work_t *w);
void  fz_eval_all(const fz_fis_t *fis, const float *in, float *out,         fz_work_t *w);

/* Load. */
int   fz_fis_parse_text(const char *text, size_t text_len, fz_fis_t *out, /* … pools … */);
int   fz_fis_check     (const fz_fis_t *fis);        /* 1 = structurally sound */
void  fz_fis_measure   (const char *text, fz_pool_size_t *out);

/* Pieces, if you want them. */
float fz_mf_eval  (uint8_t type, const float *p, float x, float hedge, int not_flag);
float fz_op2      (uint8_t op, float a, float b);
float fz_op_fold  (uint8_t op, const float *v, uint8_t n);
float fz_defuzz   (uint8_t method, float lo, float hi, const float *ys,
                   uint16_t n, float fallback);
void  fz_linspace (float lo, float hi, float *dst, uint16_t n);
```

### Error codes

| Code | Meaning |
|---|---|
| `FZ_OK` | success |
| `FZ_ERR_ARG` | null pointer, or the buffer is not NUL-terminated within `text_len` |
| `FZ_ERR_SECTION` | missing `[System]` / `[Input#]` / `[Output#]` / `[Rules]` |
| `FZ_ERR_KEY` | unparsable key or value |
| `FZ_ERR_POOL` | a caller pool is too small |
| `FZ_ERR_RULE` | rule line does not match the grammar |
| `FZ_ERR_TYPE` | unknown FIS type, MF type, or operator name |
| `FZ_ERR_COUNT` | `NumInputs`/`NumOutputs`/`NumMFs`/`NumRules` disagree with content |

A failed parse leaves `out` unusable — treat it as an error, not a partial load.

---

## Configuration

| Macro | Default | Effect |
|---|---|---|
| `FZ_MAX_POINTS` | `101` | Sample grid size. Must be ≥ the FIS `n_points`. MATLAB's default is 101; lower it only when you accept the accuracy cost. |
| `FZ_NAME_LEN` | `20` | Stored variable-name length, including the terminator. |
| `FZ_MAX_MF_PARAMS` | `4` | Longest standard MF parameter list. Not a policy cap: `linear` consequents carry their coefficients out of band. |
| `FZ_ENABLE_PARSER` | auto | Force the `.fis` parser on or off. |
| `FZ_HAS_PARSER` | derived | Always defined as `0` or `1` — branch with `#if FZ_HAS_PARSER`. |

The parser is compiled in for every non-AVR Arduino core, and switched off
automatically on AVR parts with less than 4 KB SRAM (`RAMEND < 0x1000`).

---

## Mental model

```
fuzzify -> firing strength -> implication -> aggregation -> defuzzify
```

- **Mamdani** materialises a sampled output set per rule and aggregates sets.
- **Sugeno** computes one (location, height) singleton per rule and takes a
  weighted average. It ignores the file's implication/aggregation: MATLAB
  hard-codes `prod`/`sum` there.
- **Tsukamoto** inverts each rule's firing height through a monotonic consequent
  MF (`linsmf`/`linzmf`) to get a crisp `z`, then takes a weighted average/sum.
  Implication is `prod`, aggregation `sum`, like Sugeno.

Aggregation is **streamed per rule**, so no `rules × points` matrix ever exists.
That is what makes a 101-point grid viable on an 8-bit MCU: the only buffer is
one output's aggregated set.

---

## Codegen

`tools/fis2h.py` reads a `.fis` file and emits `static const` MF/var/rule tables
plus `fz_fis_<name>`. Names are sanitized to C identifiers, strings truncated to
`FZ_NAME_LEN - 1` characters.

```sh
python3 tools/fis2h.py model.fis -o model_fis.h           # writes model_fis.h
python3 tools/fis2h.py model.fis --name tipper -o -       # stdout, custom symbol
python3 tools/fis2h.py model.fis --points 201 -o m.h      # non-default n_points
python3 tools/fis2h.py --selftest                         # encoding self-check
```

Output compiles as C99 and C++11 under `-Wall -Wextra -Werror -pedantic`. Exit
status is 1 with the message on stderr for a malformed file, so it drops straight
into a build rule.

---

## Repository layout

```
src/              the library — 5 translation units, no dependencies
  fz_types.h      static FIS model, rule-slot encoding
  fz_mf.c         13 membership functions, hedges, NOT
  fz_ops.c        t-norms and s-norms
  fz_defuzz.c     8 defuzzification methods
  fz_eval.c       Mamdani + Sugeno + Tsukamoto evaluator
  fz_fis_parse.c  .fis reader (opt-in)
  examples/         generated_static, tipper_mamdani, tipper_sugeno
tools/            fis2h.py, fis2json.py, check_parity.py, oracle.m, patch-oracle.sh
tests/golden/     21 reference systems, 20 parity-checked + oracle output
tests/host/       7 test binaries, `make -C tests/host run`
tests/stm32f103/  Cortex-M3 cross-compile gate, `make -C tests/stm32f103 run`
docs/             COMPATIBILITY.md, MATLAB-VERIFY.md
CONTRACT.md       the normative spec the implementation is written against
```

---

## Verifying it yourself

The host suite needs only a C compiler:

```sh
make -C tests/host run          # 7 test binaries, no framework, no network
make -C tests/stm32f103 run     # cross-compile gate: Cortex-M3 ELF + size
make -C tests/stm32f103 run FZ_DOUBLE=1   # same, 64-bit fz_real_t build
```

Memory safety:

```sh
make -C tests/host clean
make -C tests/host CFLAGS="-std=c11 -Wall -Wextra -O1 -g \
  -fsanitize=address,undefined -I$PWD/src" run
```

Numerical parity against the reference, per system:

```sh
python3 tools/check_parity.py \
  --fis tests/golden/mamdani_tip_calculator.fis \
  --inputs tests/golden/mamdani_tip_calculator.in.txt
```

To reproduce the oracle itself, or to re-run the same check in **real MATLAB**,
follow [`docs/MATLAB-VERIFY.md`](docs/MATLAB-VERIFY.md).

---

## Compatibility notes

Parity is measured against MATLAB semantics, which in six places differ from the
popular `fuzzy-logic-toolkit`: degenerate `trapmf` shoulders are legal,
`bisector` returns an x value rather than an array index, Sugeno always
aggregates by sum, `einstein_product` folds from the identity, `probor` actually
exists, and an empty aggregate yields the range midpoint instead of aborting. All
six are documented in [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) §3.

One known, declared limitation: **`bisector` is grid-quantised.** It resolves on
the sample grid, so two conforming implementations can differ by at most one grid
step. That is a property of the algorithm MATLAB defines, not drift.

**Not implemented:** type-2 FIS, ANFIS/`genfis` training, the Fuzzy Logic
Designer GUI, Simulink blocks, custom function-handle MFs, runtime mutation
(`addvar`/`addrule`).

---

## License

MIT — see [LICENSE](LICENSE).
