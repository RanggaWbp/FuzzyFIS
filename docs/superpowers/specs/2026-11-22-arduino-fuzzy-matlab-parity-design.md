# fz — Arduino Fuzzy Logic Library with MATLAB Toolbox Parity

Status: approved design (2026-11-22) · Repo: `/home/ranggawbp/Projects/embedded-fuzzy`

## 1. Intent

A C++/C Arduino library whose fuzzy inference results are numerically
interchangeable with MATLAB® Fuzzy Logic Toolbox™ for type-1 systems, on
AVR / ESP32 / STM32 / host.

The reason this is buildable at all: MATLAB's `.fis` file is a plain-text
contract (four sections, MF literals, integer-with-fraction rules), and its
inference pipeline is a fixed sequence of documented operators over a fixed
sample grid. Parity is therefore a specification problem, not a
reverse-engineering problem.

Why nobody has it today: eFLL (the popular Arduino fuzzy library) supports
triangle/trapezoid/singleton only, centroid only, Mamdani only, has no `.fis`
reader, and computes centroid over a caller-chosen domain rather than
MATLAB's 101-point `linspace` of the output range. That last difference alone
is enough to break numeric parity.

## 2. Goals

1. Read MATLAB `.fis` files (v2.0 text format) with no conversion step.
2. All 5 Mamdani defuzzification methods: `centroid`, `bisector`, `mom`, `som`, `lom`.
3. Sugeno: `wtaver`, `wtsum`, `constant` and `linear` consequents, any number of inputs.
4. All 13 membership function types MATLAB ships: `trimf trapmf gaussmf gauss2mf
   gbellmf sigmf dsigmf psigmf zmf smf pimf linsmf linzmf`.
5. Operator coverage: AND/OR `min max prod probor sum`; implication `min prod`;
   aggregation `max sum probor`; plus the algebraic/Einstein/Hamacher/bounded
   families the reference toolkit supports.
6. Rules with weight, NOT, hedges (`somewhat .05`, `very .2`, `extremely .3`,
   `very very .4`, custom `.dd`), and per-rule AND/OR connection.
7. Runs with zero dynamic allocation on AVR (2 KB SRAM) via static FIS
   structures + compile-time codegen from `.fis`.
8. Verified parity, not claimed parity: measured error table against an
   MATLAB-compatible oracle, reproducible on the user's machine.

## 3. Non-goals

Type-2 FIS, ANFIS/`genfis` training, Fuzzy Logic Designer GUI, Simulink block,
custom function-handle MFs, runtime mutation API (`addvar`/`addrule`).
Type-2 is a separate opt-in module, not part of the parity core.

## 4. Anti-goals

- A "close enough" centroid over an arbitrary grid. Parity dies the moment the
  sample grid differs from MATLAB's.
- Hidden smoothing, clipping, or epsilon guards that change a result.
- A heap. A `String`. A `std::vector` on the device path.
- A dependency on any Arduino core API in the math path.

## 5. Constraints

- C++11 for the public library; C11 for the math core; headers C99-clean.
- `float` on device. MATLAB is double; the parity criterion is stated as a
  relative tolerance, with the measured worst case published.
- No exceptions, no RTTI, no `malloc`, no I/O in evaluation.
- The oracle is Octave 10.3 + `fuzzy-logic-toolkit` 0.4.6, which reproduces the
  documented MATLAB formulas (same 101 default sample points, same operator set,
  same MF mathematics). MATLAB itself is unavailable here; the user runs the
  final cross-check on their own MATLAB install with the shipped `.m` script.

## 6. Approaches considered

**A. Fork eFLL.** Rejected: eFLL's data model (point lists, no FIS, centroid
only) is structurally wrong for parity; the diff would be a rewrite wearing a
fork's clothes.

**B. Runtime-only FIS with heap.** Rejected: fails the AVR constraint, and
fragmentation risk on ESP32 for a bounded-size problem.

**C. Static FIS + optional stream parser. (chosen)** The FIS is a `const`
struct; evaluation walks it with a caller-owned work buffer. A `.fis` parser
fills those structs from PROGMEM/SD/host files, and a Python codegen emits the
same structs as a header for boards where even the parser is too much. One
semantic implementation, two ways to fill it.

**D. Emit a lookup table instead of evaluating.** Rejected as the primary path
(breaks exactness off-grid); kept as a *possible future* acceleration, never as
the definition of correctness.

Rationale for C: one code path for all boards, parity is testable on the host
with the same code that runs on the MCU, and the AVR case degrades to a header
include instead of a different implementation.

## 7. Design

### 7.1 Data model

```c
fz_fis_t  { type, n_in, n_out, n_rules, in*, out*, rules*, ops…, n_points }
fz_var_t  { name, lo, hi, nmf, mfs* }
fz_mf_t   { type, nparam, p[6] }
fz_rule_t { n, conn, weight, ante*, conse* }   /* int16: sign=NOT, frac=hedge */
```

All arrays are caller-owned and `const` after load. `fz_work_t` is a single
`float[FZ_MAX_POINTS]` (default 101) that holds the running aggregated set for
one output at a time — the aggregation is streamed per rule, so no
rules × points matrix ever exists in memory. That is the single decision that
makes AVR viable without giving up MATLAB's grid.

### 7.2 Evaluation order (must match the reference exactly)

fuzzify → firing strength (op fold over non-omitted antecedents, × weight) →
per output: implication → aggregation, streaming over rules → defuzzify over
`linspace(lo, hi, n_points)`.

Mamdani and Sugeno differ in exactly one place: Mamdani materialises a sampled
output set per rule and aggregates sets; Sugeno computes a singleton
(location, height) per rule and does a weighted average or sum. Sugeno ignores
file-specified implication/aggregation (MATLAB hard-codes prod/sum).

### 7.3 Parity strategy

Three independent implementations of the same spec, cross-checked:

1. C library (the product).
2. `tools/fis2json.py` + Python evaluator (double precision reference).
3. Octave/MATLAB via `readfis` + `evalfis` (the oracle).

Parity gate: C vs oracle ≤ 3e-5 relative on the 101-point grid for Mamdani,
≤ 1e-6 for Sugeno, over generated input grids for every shipped fixture. The
measured table ships in `docs/COMPATIBILITY.md`; the "exactness" claim is
always scoped to (method, n_points), never to "all fuzzy logic".

Known, declared divergence: `bisector` is resolved on the sample grid (a
discrete search), so its result is one of the grid points — inherently
grid-quantised, and reported as such rather than papered over.

### 7.4 Sizing per board

| Board | Path | RAM (work only) |
|---|---|---|
| AVR (2 KB) | codegen header, 1 output at a time | 101 floats ≈ 404 B + FIS in flash |
| ESP32 | runtime `.fis` from PROGMEM/SD | same + FIS in PSRAM/flash |
| STM32 | runtime `.fis` or codegen | same |

`n_points` is compile-time configurable downward when RAM is tight; the error
cost of doing so is measured by the harness, not estimated.

### 7.5 Testing

- `tests/host` — assert-based, no framework, `make run`.
- `tests/golden` — real oracle output, generated by execution, committed.
- `tools/check_parity.py` — builds a harness against the actual library and
  diffs against the reference; exit code is the gate.
- CI — host tests + codegen drift check + `arduino-cli compile` for
  `arduino:avr:uno`, `esp32:esp32:esp32`, `STMicroelectronics:stm32:GenF4`.

### 7.6 What the builder decides

File splits inside `src/`, error-code values, pool-sizing helpers, example
sketches, and documentation wording. Not open: the formulas, the evaluation
order, the sample grid, the defaults, and the tolerance policy.
