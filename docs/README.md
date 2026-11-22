# fz documentation

Start with the top-level [README](../README.md) for install and a working sketch.

| File | Answers |
| --- | --- |
| [COMPATIBILITY.md](COMPATIBILITY.md) | What is identical to MATLAB, what is not, with the measured error table. |
| [MATLAB-VERIFY.md](MATLAB-VERIFY.md) | Exact commands to reproduce the parity numbers on your own MATLAB. |
| [../CONTRACT.md](../CONTRACT.md) | The normative spec: data model, inference order, MF formulas, `.fis` grammar, tolerances. |

## The three things people get wrong

**1. Sample points.** Mamdani defuzzification samples the aggregated output on a
`linspace(lo, hi, n_points)` grid, default 101 — the same default `evalfis` uses.
Change `n_points` and the centroid moves. `-DFZ_MAX_POINTS=n` only sets the
scratch buffer ceiling; the grid size is `fz_fis_t.n_points`, where `0` means
"use `FZ_MAX_POINTS`". `fis2h.py --points N` writes N explicitly and emits a
`#error` guard if the build's `FZ_MAX_POINTS` is smaller.

**2. Sugeno implication and aggregation are not configurable.** The `.fis` may
say `ImpMethod='min'`; a Sugeno system still uses product implication and sum
aggregation, exactly as MATLAB does. Only `DefuzzMethod` (`wtaver` / `wtsum`)
matters. `tools/fis2h.py` encodes this, so don't "fix" the generated header.

**3. `float` vs `double`.** The library is `float32`; MATLAB is `float64`. Parity
is asserted with `abs(err) <= 3e-5 * max(1, |ref|)` for Mamdani at 101 points and
`1e-6` relative for Sugeno — not bit equality. Bit equality is only claimed for
`n_points` and operator parity.

## Adding a model

1. Save the `.fis` from the MATLAB Fuzzy Logic Designer (or write it by hand —
   grammar in [CONTRACT.md §F](../CONTRACT.md)).
2. Generate the header: `python3 tools/fis2h.py model.fis -o model_fis.h`.
3. Commit the header and let CI's `codegen` job diff it. If the diff is non-empty
   CI fails, so the `.fis` stays the source of truth.

## Header/contract skew you may trip over

`src/fz_types.h` (the shipped header) and `CONTRACT.md` §B differ in two ways that
matter to generated code: the shipped enums are prefixed (`FZ_MF_TRIMF`, `FZ_OP_MIN`)
and `fz_fis_t` carries a `char name[FZ_NAME_LEN]` that §B does not list.
`fis2h.py` targets the shipped header, because that is what the library compiles
against. Building against a §B-literal `fz_types.h` works too — pass
`-DFZ_FIS_HAS_NAME=0` so the generator omits the FIS name field.

## Host verification

```sh
make -C tests/host run                       # assert suite, no framework
python3 tools/check_parity.py --fis tests/fis/tipper.fis \
    --inputs in.txt --lib build/libfz.a --tol 3e-5
```

## Series checks for a generated model

`examples/generated_static/` is the worked reference. Two sweeps that caught real
bugs while this was built, both worth repeating after a model change:

```sh
# 1. Same inputs through the C side and the toolkit must agree (CONTRACT §H).
#    Mamdani: abs err <= 3e-5. Sugeno: 1e-6 relative.
# 2. No input may land on a value where every rule dies. A FIS whose MFs go to
#    zero at both range ends returns the midpoint silently (centroid of an empty
#    set), which hides a modelling error behind a plausible number.
```
