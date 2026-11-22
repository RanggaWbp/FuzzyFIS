# fz

MATLAB-compatible fuzzy inference for microcontrollers. Mamdani and Sugeno,
`float32`, no heap, no exceptions, no RTTI. Same membership functions, operators
and inference order as the MATLAB Fuzzy Logic Toolbox, so a FIS you tuned in
MATLAB runs on a 2 KB AVR without retuning. Verified on AVR, ESP8266, ESP32
and STM32.

- Static FIS tables in flash (`.rodata`), generated at build time from a `.fis` file.
- Or parse a `.fis` file at runtime where RAM allows (host, ESP8266, ESP32, STM32).
- Deterministic: fixed sample count, fixed fold order, no allocation in `fz_eval`.

## Guarantees, measured

- **No hardcoded counts.** Inputs, outputs, membership functions, rules and
  sample points are sized by your pools, not by the library. `fz_mf_t.p[4]` is
  the longest standard MF parameter list (pimf/gauss2mf/dsigmf/psigmf), not a
  cap; Sugeno `linear` consequents carry `n_inputs+1` coefficients through a
  caller-owned float array. `tests/host/test_unbounded.c` proves it with a
  12-input, 30-rule system under ASan+UBSan.
- **No heap.** Nothing in `src/` allocates. The only runtime buffer is
  `fz_work_t`, 404 bytes.
- **Parity with MATLAB**, not with another Arduino library: worst relative
  error 1.4e-6 over 13 reference fixtures (bisector is grid-quantised and
  declared as such). `docs/COMPATIBILITY.md` has the table.
- **Footprint**: 5.4 KB evaluation core; the `.fis` parser adds 6.1 KB and is
  opt-in. `arduino:avr:uno` with a generated FIS: 9362 B flash, 922 B RAM.
- **Speed**: 3.0 us per evaluation for a 2-input/3-rule Mamdani FIS on x86-64
  `-O2`; Sugeno skips the sample grid entirely.

## Install

Arduino IDE / Arduino CLI: copy or symlink this directory into `~/Arduino/libraries/fz`
(or use *Sketch → Include Library → Add .ZIP Library*). `architectures=*`.

PlatformIO (`platformio.ini`):

```ini
lib_deps = symlink://path/to/embedded-fuzzy
```

`library.json` points at `src/` and exports only `src/`, so tools and tests
do not end up on the device.

## Quick start: generated (static) FIS

Write `tipper.fis`, then generate a header at build time:

```sh
python3 tools/fis2h.py examples/generated_static/tipper.fis -o tipper_fis.h
```

```cpp
#include <fz.h>
#include "tipper_fis.h"

static fz_work_t work;

void loop() {
  const float in[2] = {service, food};
  float tip[1];
  fz_eval_all(&fz_fis_tipper, in, tip, &work);
  Serial.println(tip[0], 3);
}
```

Everything the FIS needs is `static const`, so it costs flash, not RAM. This is
the only variant that fits an ATmega328P comfortably.

## Quick start: runtime `.fis`

```cpp
#include <fz.h>

static fz_mf_t   mf_pool[16];
static fz_var_t  in_pool[4], out_pool[4];
static fz_rule_t rule_pool[8];
static int16_t   idx_pool[32];   /* all rule slots, contiguous */
static fz_fis_t  fis;
static fz_work_t work;

int rc = fz_fis_parse_text(text, &fis,
                           mf_pool, 16, in_pool, 4, out_pool, 4,
                           rule_pool, 8, idx_pool, 32);
if (rc != FZ_OK) { /* FZ_ERR_POOL means a pool is too small */ }
```

Pools are caller-owned; the parser never allocates. `fz_fis_parse_file(path, ...)`
is host-only (`#ifndef ARDUINO`) and needs a caller-supplied text buffer. Error
codes are `FZ_ERR_*` in `fz_fis_parse.h`. One `fz_mf_t` is 28 bytes, so budget the
pools against your SRAM before choosing this path.

`fz.h` includes the parser when `ARDUINO` is defined **or** `FZ_ENABLE_PARSER` is
set, and sets `FZ_ARDUINO_SAFE` automatically on AVR parts under 4 KB SRAM, which
turns the parser off. Check `FZ_HAS_PARSER` if you need to branch.

## Evaluation

```c
float fz_eval(const fz_fis_t *fis, const float *in, uint8_t out_index, fz_work_t *w);
void  fz_eval_all(const fz_fis_t *fis, const float *in, float *out, fz_work_t *w);
```

`fz_work_t` holds `FZ_MAX_POINTS` floats (default 101, override with
`-DFZ_MAX_POINTS=n`). It is scratch space for the aggregated Mamdani output;
Sugeno does not touch it. `fz_eval` returns `NaN` for Sugeno when no rule fires
(`den == 0`), matching MATLAB.

`fz_fis_t.n_points` is the grid size; `0` means "use `FZ_MAX_POINTS`". Sample
points must match MATLAB, which defaults to 101 — that is what `fis2h.py` emits.
Lower it to trade accuracy for time on slow targets; keep 101 for parity work.
The generated header refuses to compile when `FZ_MAX_POINTS` is smaller than the
`n_points` it asks for, so a short work buffer cannot silently shrink the grid.

## Codegen contract

`tools/fis2h.py` reads a `.fis` file (sections `[System]`, `[Input#]`,
`[Output#]`, `[Rules]`) and emits `static const` MF/var/rule tables plus
`fz_fis_<name>`. Names are sanitized to C identifiers, strings truncated to
`FZ_NAME_LEN - 1` characters (default 20), and a `FZ_<NAME>_MAX_POINTS` hint is
emitted when the FIS needs more than the default sample count.

```sh
python3 tools/fis2h.py model.fis -o model_fis.h           # writes model_fis.h
python3 tools/fis2h.py model.fis --name tipper -o -       # stdout, custom symbol
python3 tools/fis2h.py model.fis --points 201 -o m.h      # non-default n_points
```

Output compiles as C99 and C++11 with `-Wall -Wextra -Werror -pedantic`.
Exit status is 1 and the message goes to stderr on a malformed file, so it can
sit directly in a build rule. `python3 tools/fis2h.py --selftest` checks the rule
slot encoding and name handling without needing a `.fis`; CI runs it first.

## Layout

```
src/            fz.h umbrella + fz_types.h, fz_mf, fz_ops, fz_defuzz, fz_eval, fz_fis_parse
tools/          fis2h.py (codegen), fis2json.py, check_parity.py, oracle.m
examples/       tipper_mamdani, tipper_sugeno (runtime), generated_static (codegen)
tests/          host assert suite + .fis fixtures
docs/           COMPATIBILITY.md, MATLAB-VERIFY.md
tools/          fis2h.py (codegen), fis2json.py (Python reference),
                check_parity.py (C vs reference), oracle.m, patch-oracle.sh
tests/golden/   13 real fixtures + oracle output; tests/host/ six test binaries
```

## Limits

AVR: prefer generated headers; each `fz_work_t` is 404 bytes at 101 points, so
keep it static and reuse it. The parser needs pools on top of that. No type-2
FIS, no ANFIS training, no GUI — see the contract's non-goals.

## License

MIT.
