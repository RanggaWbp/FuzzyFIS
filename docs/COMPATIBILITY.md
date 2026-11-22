# Compatibility with MATLAB® Fuzzy Logic Toolbox™

Every number on this page was produced by execution, not estimation. Target:
type-1 Mamdani and Sugeno systems, evaluated with the default
`NumSamplePoints = 101`.

## 1. Measured parity

Reference = MATLAB-compatible oracle (Octave 10.3 + `fuzzy-logic-toolkit` 0.4.6,
patched only where the toolkit itself deviates from MATLAB — see §3).
Grid = 11 points per input over each variable's range.

| Fixture | Type | In/Out/Rules | Rows | Worst abs | Worst rel |
|---|---|---|---|---|---|
| `cubic_approximator` | Sugeno wtaver | 1/1/11 | 11 | 0 | 0 |
| `heart_disease_risk` | Sugeno wtaver | 2/1/15 | 121 | 0 | 0 |
| `defuzz-mom` | Mamdani mom | 1/1 | 11 | 0 | 0 |
| `defuzz-lom` | Mamdani lom | 1/1 | 11 | 0 | 0 |
| `mf-zs` | Mamdani som | 1/1/2 | 13 | 1.9e-7 | 4.0e-8 |
| `sugeno-linear2` | Sugeno linear | 2/1 | 99 | 1.7e-6 | 4.9e-7 |
| `sugeno_tip_calculator` | Sugeno wtaver | 2/3/10 | 121 | 3.1e-6 | 2.0e-7 |
| `hedges` | Mamdani centroid | 1/1 | 36 | 1.5e-6 | 3.0e-7 |
| `linear_tip_calculator` | Sugeno linear | 2/1/4 | 121 | 1.2e-6 | 9.2e-8 |
| `mf-gauss` | Mamdani centroid | 1/1 | 441 | 4.9e-7 | 4.9e-7 |
| `investment_portfolio` | Mamdani centroid_integral | 2/1/4 | 121 | 2.7e-5 | 6.4e-7 |
| `mamdani_tip_calculator` | Mamdani centroid | 2/2/4 | 121 | 1.1e-5 | 8.9e-7 |
| `mf-sigmoid` | Mamdani centroid | 1/1 | 187 | 6.1e-5 | 1.4e-6 |
| `edge_hedges` | Mamdani bisector | 2/1/3 | 121 | 1.0e0 | 2.2e-2 |

**Worst relative error across everything except bisector: 1.4e-6.**
Tolerance used by the test suite: 3e-5 relative
(`|got - ref| <= 3e-5 * max(1, |ref|)`), i.e. a ~20× margin.

The library computes in `float`, MATLAB in `double`. That is the entire source of
the residual: same formulas, same grid, same fold order.

## 2. Bisector is grid-quantised, by construction

`bisector` finds the vertical line splitting the area in half by binary search
over the discretised set, in the reference as well as here. Two conforming
implementations therefore can differ by at most **one grid step**
(`(hi-lo)/(n_points-1)`; 1.0 on a `[0 100]` range with 101 points). The fixture
measures 1.0 exactly, which is that bound — not drift.

If you need bisector tighter, raise `n_points`. It cannot be made exact without
leaving the sample grid that defines MATLAB's own answer.

## 3. Where the reference toolkit deviates from MATLAB

Four real deviations were found and proven while building this. The oracle used
for the golden files is patched on all four; the library follows **MATLAB**, not
the toolkit.

| # | Toolkit behaviour | MATLAB behaviour (what we implement) |
|---|---|---|
| 1 | `trapmf_params` rejects degenerate shoulders: `[0 0 1 3]`, `[7 9 10 10]` fail validation | Shoulders are legal and common; `x<=a` → 0, `b<=x<=c` → 1 |
| 2 | `defuzz.m` `bisector` returns `midpoint`, the **array index** | Returns the x value of the bisector |
| 3 | Sugeno aggregation uses the file's `AggMethod` | Sugeno always aggregates by **sum**; a file saying `AggMethod='max'` silently drops duplicate singletons in the toolkit |
| 4 | `einstein_product` fold starts at 1 and re-folds `x(1)`, squaring the first term | T-norm fold starts from the identity 1 |

Additionally, on a **zero output area** the toolkit returns the top of the range
for `bisector`, whereas MATLAB documents the range **midpoint**. `som`/`lom`/`mom`
return the first/last/mean of the samples attaining the maximum, which on an
all-zero set means `lo`/`hi`/midpoint respectively — the library matches that.

## 4. Exactness claims, scoped

Claimed identical (within float32 rounding) for:

- Mamdani: `centroid`, `mom`, `som`, `lom`, `wtaver`, `wtsum`, `centroid_integral`
- Sugeno: `wtaver`, `wtsum`, with `constant` and `linear` consequents
- Operators: AND/OR `min max prod probor sum`, implication `min prod`,
  aggregation `max sum probor`, plus `algebraic_*`, `bounded_sum`,
  `einstein_*`, `hamacher_*`
- All 13 membership functions: `trimf trapmf gaussmf gauss2mf gbellmf sigmf
  dsigmf psigmf zmf smf pimf linsmf linzmf`
- Rule weights, `NOT` (negative slot), hedges `somewhat .05`, `very .2`,
  `extremely .3`, `very very .4`, custom `.dd`, per-rule AND/OR connection
- Multi-input, multi-output systems

Not claimed: bit-exactness against MATLAB's `double` on an arbitrary grid, and
anything in §5.

## 5. Not implemented

Type-2 FIS, ANFIS/`genfis` training, Fuzzy Logic Designer GUI, Simulink block,
custom function-handle MFs (`@myfunc`), runtime mutation (`addvar`/`addrule`).

A `.fis` using any of those fails to parse with a specific error code rather
than producing a wrong number.

## 6. Resource cost (measured, `-Os`, x86-64 host)

| Item | Size |
|---|---|
| `fz_work_t` (the only runtime buffer) | 404 B |
| `fz_fis_t` | 40 B |
| `fz_rule_t` / `fz_var_t` / `fz_mf_t` | 24 / 40 / 28 B |
| Whole library `.text` | 6.8 KB |

Allocations in `src/`: **none**. No `malloc`, `calloc`, `realloc`, `new`,
`delete`, `std::vector`, `std::string`. External symbols required: `expf`,
`powf`, `strcmp`, `memset` (and `powf` only on hedge/`gbellmf` paths).

Flash scales with the FIS you generate or parse, not with the library: the
evaluator is one path for every board.
