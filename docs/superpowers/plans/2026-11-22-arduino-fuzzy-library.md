# fz Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship an Arduino C++ library whose type-1 fuzzy inference matches MATLAB Fuzzy Logic Toolbox numerically, verified against a real oracle.

**Architecture:** Static `const` FIS structs + caller-owned work buffer; streaming per-rule aggregation so no rules×points matrix exists; one evaluation path used by host tests and MCU. FIS structs are filled either by a no-malloc `.fis` parser or by a Python codegen that emits the same structs as a header.

**Tech Stack:** C11 math core, C++11 public headers, Python 3 stdlib tools, gcc host tests, Octave + fuzzy-logic-toolkit as parity oracle, arduino-cli for compile matrix.

**Spec:** `docs/superpowers/specs/2026-11-22-arduino-fuzzy-matlab-parity-design.md`
**Implementation contract (formulas, signatures, grammar):** `CONTRACT.md` — normative; this plan never restates a formula.

## Global Constraints

- `float` in the math core; no `double` on the device path.
- No `malloc`, no exceptions, no RTTI, no I/O in `fz_eval*`.
- Default `n_points = 101`; grid is `lo + k*(hi-lo)/(n_points-1)`.
- Headers compile as C99 and C++11 with `-Wall -Wextra -Werror -pedantic`.
- Parity tolerances: Mamdani 3e-5 relative, Sugeno 1e-6 relative (vs oracle).
- Every shipped number in docs is produced by execution, never estimated.

## Review Focus

- Input outside every MF support (`x < lo` or `x > hi`): a reasonable person
  expects a finite, sane output — must not divide by zero in `(x-a)/(b-a)`
  guards. Pinned by `test_mf.c` degenerate cases.
- All rules fire at 0 (output area zero): MATLAB returns the midpoint of the
  output range. Must be pinned in `test_eval.c`, and cross-checked by the oracle.
- `n_points` lowered for RAM (e.g. 21): centroid must degrade gracefully and the
  measured error must be published, not hidden.
- Hedge/NOT combined in a consequent (`-2.2`): Sugeno inverts the hedge
  exponent (`h^(1/hedge)`) — a builder will get this backwards. Pinned by
  `test_parse.c` + `test_regression.c` over a golden row.
- A `.fis` with `NumMFs` disagreeing with the `MF#` lines present: must fail
  parse with an error code, not read past the pool. Pinned by `test_parse.c`.

---

### Task 1: Numeric core types, MF, operators (owner A)

**Files:** `src/fz_types.h`, `src/fz_mf.{h,c}`, `src/fz_ops.{h,c}`, `tests/host/test_mf.c`, `tests/host/test_ops.c`

**Interfaces — Produces:** all types in `CONTRACT.md §B`; `float fz_mf_eval(uint8_t type, const float *p, float x)`; `float fz_op_apply(uint8_t op, const float *v, uint8_t n)`.

- [ ] Step 1: Write `test_mf.c` asserting `trimf` at `[0 5 10]` for `x = -1,0,2.5,5,7.5,10,11` → `0,0,0.5,1,0.5,0,0`.
- [ ] Step 2: Run `gcc -std=c11 -Wall -Wextra -Werror -pedantic -Isrc tests/host/test_mf.c src/fz_mf.c -lm -o /tmp/t && /tmp/t` → expect compile failure (`fz_mf.h` missing).
- [ ] Step 3: Implement `fz_mf.c` from `CONTRACT.md §D` verbatim, with the degenerate-divisor guard returning 0.
- [ ] Step 4: Re-run; expect all asserts pass.
- [ ] Step 5: Add oracle comparison: `tools/oracle_mf.m` prints 101-point CSV for each MF; a throwaway Python diff must show ≤ 1e-6 relative.
- [ ] Step 6: Commit `feat: numeric core types, membership functions, operators`.

### Task 2: Defuzzification (owner A)

**Files:** `src/fz_defuzz.{h,c}`, `tests/host/test_defuzz.c`

- [ ] Step 1: Assert `centroid` on `([1 2 3 4],[1 1 1 1]) = 2.5`, `([1 2 3 4],[1 2 3 4]) = 3`, `([1 2 3 4],[0 0 1 1]) = 3.5` (values from the reference toolkit's own tests).
- [ ] Step 2: Assert `bisector` on `([1 2 3 4],[1 2 3 4]) = 3`, `mom`/`som`/`lom` = 4, `wtsum` variants = 10/30/7.
- [ ] Step 3: Implement per `CONTRACT.md §E`-adjacent `defuzz` semantics (`centroid` = Σxy/Σy).
- [ ] Step 4: Run, expect pass; then the all-zero case returns `(lo+hi)/2`.
- [ ] Step 5: Commit `feat: defuzzification methods`.

### Task 3: Evaluation engine, Mamdani + Sugeno (owner A)

**Files:** `src/fz_eval.{h,c}`, `tests/host/test_eval.c`

- [ ] Step 1: Build a 2-input/1-output/3-rule Mamdani FIS literal in the test.
- [ ] Step 2: Run and capture `fz_eval_all` output for a grid; diff against oracle CSV for the equivalent `.fis`.
- [ ] Step 3: Implement streaming implication+aggregation per `CONTRACT.md §C.4`; no per-rule matrix.
- [ ] Step 4: Assert parity ≤ 3e-5 relative for all 5 Mamdani defuzz methods; ≤ 1e-6 for `wtaver`/`wtsum`.
- [ ] Step 5: Assert hedge inversion for Sugeno (`h^(1/hedge)`) matches a hand-computed case.
- [ ] Step 6: Commit `feat: Mamdani and Sugeno evaluation engine`.

### Task 4: `.fis` parser (owner B)

**Files:** `src/fz_fis_parse.{h,c}`, `tests/fis/*.fis`, `tests/host/test_parse.c`

- [ ] Step 1: Copy 6 real fixtures from `/var/tmp/flt-src/inst/*.fis`; add 4 edge cases (comments, odd spacing, `-2.2`, `(0.5) : 2`, multi-output).
- [ ] Step 2: Assert `NumRules`/`NumMFs`/params/weight/conn for each fixture against `readfis` output.
- [ ] Step 3: Implement no-malloc parser over caller pools per `CONTRACT.md §F`.
- [ ] Step 4: Assert pool overflow returns a negative code and leaves `out` untouched.
- [ ] Step 5: Commit `feat: .fis parser`.

### Task 5: Oracle + golden data (owner C)

**Files:** `tools/oracle.m`, `tools/run_oracle.sh`, `tools/fis2json.py`, `tests/golden/*`, `docs/MATLAB-VERIFY.md`

- [ ] Step 1: Write `oracle.m` printing `%.17g` CSV for a grid, plus intermediate outputs via the 4-output `evalfis` form.
- [ ] Step 2: Generate golden CSVs for 6 fixtures × ≥11×11 grid; commit them.
- [ ] Step 3: Write independent `fis2json.py` + evaluator; assert it reproduces the oracle to 1e-9.
- [ ] Step 4: Commit `test: Octave oracle and golden datasets`.

### Task 6: Codegen + packaging (owner D)

**Files:** `tools/fis2h.py`, `src/fz.h`, `library.properties`, `library.json`, `keywords.txt`, `examples/*`, `.github/workflows/ci.yml`

- [ ] Step 1: Generate a header from a fixture; compile it with `-std=c99 -Wall -Wextra -Werror -pedantic`.
- [ ] Step 2: Assert generated header round-trips: parse header values back in Python and compare to the `.fis`.
- [ ] Step 3: Write the umbrella header with parser gated behind `ARDUINO`/`FZ_ENABLE_PARSER`.
- [ ] Step 4: `python3 -m json.tool library.json`.
- [ ] Step 5: Commit `feat: codegen, packaging, examples, CI`.

### Task 7: Parity harness + measured compatibility doc (owner E)

**Files:** `tools/check_parity.py`, `tools/convert_fis.py`, `tests/host/Makefile`, `tests/host/test_regression.c`, `docs/COMPATIBILITY.md`

- [ ] Step 1: `make -C tests/host run` discovers every `test_*.c` via wildcard and fails on non-zero exit.
- [ ] Step 2: `check_parity.py` compiles a harness against `src/*.c`, diffs to oracle, exits 1 on breach.
- [ ] Step 3: `test_regression.c` pins ≥30 oracle points per fixture at 3e-5.
- [ ] Step 4: `COMPATIBILITY.md` publishes the measured max error per method and the declared `bisector` grid quantisation.
- [ ] Step 5: Commit `test: parity harness and measured compatibility report`.

### Task 8: Integration gate

- [ ] Step 1: `make -C tests/host run` green.
- [ ] Step 2: `python3 tools/check_parity.py --all` green.
- [ ] Step 3: `python3 tools/fis2h.py` twice → `git diff --exit-code` (no drift).
- [ ] Step 4: `arduino-cli compile` for `arduino:avr:uno`, `esp32:esp32:esp32`, `STMicroelectronics:stm32:GenF4`.
- [ ] Step 5: Tag `v0.1.0`, publish the MATLAB verification script for the user's own MATLAB run.
