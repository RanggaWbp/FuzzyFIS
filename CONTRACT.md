# fz — MATLAB-compatible fuzzy inference, contract for implementers

Repo: `<repo>` (C++11 + Python 3 tools).
No MATLAB here. Oracle = Octave 10.3 + fuzzy-logic-toolkit 0.4.6 at
`/var/tmp/flt-src/inst` (run via `/home/ranggawbp/.hermes/cache/scratch/mm/bin/micromamba run -p /var/tmp/octenv octave --no-gui --quiet FILE.m`).
The MATLAB Fuzzy Logic Toolbox is the target semantics; the toolkit reproduces the
same formulas (verified: same defaults 101 sample points, same operators, same MF math).

## A. Rules of engagement

- Deterministic output. No dynamic allocation in evaluation. No exceptions. No RTTI.
- `fz_real_t` throughout (default `float`; define `FZ_USE_DOUBLE` for 64-bit `double`).
  MATLAB computes in `double`; parity criterion is explained in §J.
- C++11 for library, C99-compatible headers where noted. Works on AVR (2 KB RAM),
  ESP32, STM32, and host.
- Every numeric constant from the contract is copied verbatim, not re-derived.

## B. Static FIS model (`src/fz_types.h`, agent A owns)

```c
typedef enum { FZ_MAMDANI = 0, FZ_SUGENO = 1, FZ_TSUKAMOTO = 2 } fz_type_t;

typedef enum {
    FZ_MF_TRIMF = 0, FZ_MF_TRAPMF, FZ_MF_GAUSSMF, FZ_MF_GAUSS2MF, FZ_MF_GBELLMF,
    FZ_MF_SIGMF, FZ_MF_DSIGMF, FZ_MF_PSIGMF, FZ_MF_ZMF, FZ_MF_SMF, FZ_MF_PIMF,
    FZ_MF_LINSMF, FZ_MF_LINZMF, FZ_MF_CONSTANT, FZ_MF_LINEAR, FZ_MF_COUNT
} fz_mf_type_t;

/* Operator ids. Fold order is caller-vector order (MATLAB prod/sum order). */
typedef enum {
    FZ_OP_MIN = 0, FZ_OP_MAX, FZ_OP_PROD, FZ_OP_SUM, FZ_OP_PROBOR,
    FZ_OP_ALGEBRAIC_PRODUCT, FZ_OP_ALGEBRAIC_SUM, FZ_OP_BOUNDED_SUM,
    FZ_OP_EINSTEIN_PRODUCT, FZ_OP_EINSTEIN_SUM, FZ_OP_HAMACHER_PRODUCT,
    FZ_OP_HAMACHER_SUM, FZ_OP_COUNT
} fz_op_t;

typedef enum {
    FZ_DEFUZZ_CENTROID = 0, FZ_DEFUZZ_BISECTOR, FZ_DEFUZZ_MOM, FZ_DEFUZZ_SOM,
    FZ_DEFUZZ_LOM, FZ_DEFUZZ_WTAVER, FZ_DEFUZZ_WTSUM,
    FZ_DEFUZZ_CENTROID_INTEGRAL, /* reference-toolkit extension, not a MATLAB method */
    FZ_DEFUZZ_COUNT
} fz_defuzz_t;

typedef struct {
    uint8_t type;                 /* fz_mf_type_t */
    uint8_t nparam;
    fz_real_t p[FZ_MAX_MF_PARAMS]; /* longest standard MF list is 4 */
    const fz_real_t *lp;          /* `linear` only: [a1..an c], caller-owned */
} fz_mf_t;

typedef struct { char name[FZ_NAME_LEN]; fz_real_t lo, hi; uint16_t nmf;
                 const fz_mf_t *mfs; } fz_var_t;

typedef struct { uint16_t n; uint8_t conn; /*1=AND,2=OR*/ fz_real_t weight;
                 const int16_t *ante;  /* SCALED x100: 0=omitted, sign=NOT, last 2 digits = hedge */
                 const int16_t *conse; } fz_rule_t;
/* Slot encoding (int16, token scaled by 100):
 *   0     -> variable omitted from the rule
 *   200   -> MF 2, no hedge              ("2")
 *   220   -> MF 2, hedge .2  = very       ("2.2")
 *   130   -> MF 1, hedge .3  = extremely  ("1.3")
 *   205   -> MF 2, hedge .05 = somewhat   ("2.05")
 *  -220   -> NOT very(MF2)                ("-2.2")
 * Decode: mf_index = |v|/100, exponent = (|v|%100)/10.
 * The exponent is NOT (|v|%100)/100: the reference computes
 * hedge = round(100*(v - fix(v)))/10, so a stored fraction of .2 is the
 * exponent 2 ("very"), and .05 is 0.5 ("somewhat"). Getting this inverted is
 * the single easiest way to silently break hedge parity.
 * x100 (not x10) is required because both 2.2 and 2.05 must be representable. */

typedef struct { char name[FZ_NAME_LEN]; fz_type_t type;
                 uint16_t n_in, n_out; uint16_t n_rules;
                 const fz_var_t *in, *out; const fz_rule_t *rules;
                 uint8_t and_op, or_op, imp_op, agg_op, defuzz_op;
                 uint16_t n_points; /* 0 -> FZ_MAX_POINTS */
               } fz_fis_t;

typedef struct { fz_real_t buf[FZ_MAX_POINTS]; } fz_work_t; /* caller-owned */

typedef struct { uint16_t n_mf, n_rule, n_idx, n_ante, n_conse;
                 uint16_t n_in, n_out;
                 uint16_t n_lin;   /* fz_real_t for `linear` coefficients */
               } fz_pool_size_t;
```
fz_real_t fz_eval(const fz_fis_t *fis, const fz_real_t *in, uint8_t out_index, fz_work_t *w);
void  fz_eval_all(const fz_fis_t *fis, const fz_real_t *in, fz_real_t *out, fz_work_t *w);
```

`FZ_MAX_POINTS` default 101, overridable by `-DFZ_MAX_POINTS=n`.

**No hardcoded counts.** Nothing in the library caps the number of inputs,
outputs, membership functions, rules or samples: those come from the caller's
pools and the `uint16_t` fields above. `p[4]` is not a limit either — it is
exactly the longest parameter list among the 13 standard MFs (pimf, gauss2mf,
dsigmf, psigmf). A Sugeno `linear` consequent has `n_inputs + 1` coefficients and
carries them through `fz_mf_t.lp`, pointing at a caller-owned float array of any
length. The antecedent fold uses no scratch array, so a rule may reference any
number of inputs.

## C. Inference order (matches toolkit exactly)

1. `x_j = clamp(in_j)` — no clamp in MATLAB; do NOT clamp, pass through.
2. Fuzzify: `mu[i][j]` for rule i input j; `mf_index`, `hedge`, `not` from §F.
   `hedge` applied for all MF types except `constant`/`linear`:
   `mu = mu^(hedge)` then `not: mu = 1-mu`. `mf_index == 0` → `mu = 0`.
   Note: `mu^(hedge)` where hedge = fraction/10 (e.g. `-.2` → index 2, hedge 2).
   Toolkit rounds hedge to 1 decimal: `hedge = round(100*(v-fix(v)))/10`.
3. Firing strength: collect `mu` for all non-zero antecedent slots;
   `w = rule.weight * OP(collection)`, `OP` = `and_op` when `conn==1` else `or_op`.
   `conn` comes from the rule's `: 1` / `: 2` (1=AND, 2=OR).
4. Mamdani, per output j (streaming, no per-rule matrix):
   `acc[k]=0` for k in 0..n_points-1; for each rule i with consequent mf index != 0
   and `w != 0`: compute `y = mf(x_k)` for the consequent MF **with hedge/not applied**
   (same as §C.2), then `y = imp(w, y)` (`min` → `fminf`, `prod` → `w*y`),
   then `acc[k] = agg(acc[k], y[k])` (`max`→fmaxf, `sum`→+, `probor`→`a+b-a*b`).
   Then `out_j = defuzz(x_k, acc, defuzz_op)` with
   `x_k = lo + k*(hi-lo)/(n_points-1)`, `k = 0..n_points-1` (MATLAB `linspace`).
5. Sugeno, per output j: for each rule i with consequent mf index != 0:
   `h = w`; `h = h^(1/hedge)` if hedge!=0; `h = 1-h` if not;
   `z = constant ? p[0] : p[0]*in[0]+...+p[n_in-1]*in[n_in-1]+p[n_in]`
   (linear params = [a1..an c]); accumulate `num += h*z`, `den += h`.
   `wtaver` → `num/den`; `wtsum` → `num`. `den == 0` → NaN (MATLAB behaviour;
   oracle agent must confirm).
   Sugeno implication is always prod, aggregation always sum, regardless of file.
6. Tsukamoto, per output j (like Sugeno, but `z` comes from the monotonic
   consequent MF inverted at `h`): for each rule i with consequent mf index != 0:
   `h = w`; `h = h^(1/hedge)` if hedge!=0; `h = 1-h` if not;
   `z` = inverse of the consequent MF at `h`: `linsmf [a b]` → `a + h*(b-a)`,
   `linzmf [a b]` → `b - h*(b-a)`; accumulate `num += h*z`, `den += h`.
   `wtaver` → `num/den`; `wtsum` → `num`. `den == 0` → midpoint `(lo+hi)/2`.
   Implication always prod, aggregation always sum. Consequent MF must be
   `linsmf`/`linzmf` (validated by `fz_fis_check` and codegen).

## D. Membership functions (`src/fz_mf.c`), formulas copied from MATLAB doc

`trimf [a b c]`: `x<=a||x>=c → 0; x==b → 1; x<b → (x-a)/(b-a); else (c-x)/(c-b)`.
`trapmf [a b c d]`: `x<=a||x>=d → 0; x>=b&&x<=c → 1; x<b → (x-a)/(b-a); else (d-x)/(d-c)`.
`gaussmf [sig c]`: `exp(-(x-c)^2/(2*sig^2))`.
`gauss2mf [sig1 c1 sig2 c2]`: `x<=c1 → gauss(sig1,c1); x>c2 → gauss(sig2,c2); else 1`.
`gbellmf [a b c]`: `1/(1+|((x-c)/a)|^(2b))`.
`sigmf [a c]`: `1/(1+exp(-a*(x-c)))`.
`dsigmf [a1 c1 a2 c2]`: `max(0, sigm(a1,c1)-sigm(a2,c2))`.
`psigmf [a1 c1 a2 c2]`: `sigm(a1,c1)*sigm(a2,c2)`.
`zmf [a b]`: `x<=a → 1; x>=b → 0; x<=(a+b)/2 → 1-2*((x-a)/(b-a))^2; else 2*((x-b)/(b-a))^2`.
`smf [a b]`: `x<=a → 0; x>=b → 1; x<=(a+b)/2 → 2*((x-a)/(b-a))^2; else 1-2*((x-b)/(b-a))^2`.
`pimf [a b c d]`: `x<=a||x>=d → 0; (b<=x&&x<=c) → 1; x<(a+b)/2 → 2*((x-a)/(b-a))^2; x<=b → 1-2*((x-b)/(b-a))^2; x<(c+d)/2 → 1-2*((x-c)/(d-c))^2; else 2*((x-d)/(d-c))^2`.
`linsmf [a b]`: `1 - linzmf`.
`linzmf [a b]`: `a==b ? (x<a) : max(0,min(1,(b-x)/(b-a)))`.
`constant [c]`: handled in Sugeno step 5 (no domain eval).
`linear [a1..an c]`: handled in Sugeno step 5.
Degenerate guard: any `a==b`, `c==b`, `sig==0`, `d==c` division → return 0 (no NaN/inf leaks).
Do NOT invent smoothing, clipping or interpolation.

## E. Operators

`min(a,b)=fminf`, `max=fmaxf`, `prod=a*b`, `sum=a+b`, `probor=a+b-a*b`.
Fold over the collected vector left-to-right (MATLAB `prod`/`sum` order).
`bounded_sum = min(1, a+b)`, `algebraic_sum = a+b-a*b` (== probor),
`einstein_product = ab/(2-(a+b-ab))`, `einstein_sum = (a+b)/(1+ab)`,
`hamacher_product = ab/(a+b-ab)` (`0` if denominator 0), `hamacher_sum = (a+b-2ab)/(1-ab)`.

## F. `.fis` file format (parser grammar)

Sections `[System]`, `[Input#]`, `[Output#]`, `[Rules]`. `%` starts a comment to EOL.
`Key=value`, whitespace around `=` ignored, values may be `'single quoted'`.
System keys: `Name`, `Type` (`mamdani|sugeno`), `Version` (ignore), `NumInputs`,
`NumOutputs`, `NumRules`, `AndMethod`, `OrMethod`, `ImpMethod`, `AggMethod`,
`DefuzzMethod`. Method values: `min prod max probor sum centroid bisector mom som lom wtaver wtsum`.
Variable sections: `Name`, `Range=[lo hi]`, `NumMFs`, `MF#='name':'type',[p1 p2 ...]`
(quotes optional on name; type has no spaces; params comma or space separated, may be empty `[]`).
`[Rules]` line: `MF_in1 MF_in2 ..., MF_out1 ... , (weight) : conn`
  - antecedent/consequent tokens are integers: `0` = variable omitted, `-2` = NOT MF2,
    `2.2` = MF2 with hedge 2, `-2.2` = NOT very(MF2).
  - **Stored scaled by 100** (see §B): `2` → `200`, `2.2` → `220`, `-2.2` → `-220`,
    `2.05` → `205`. Hedge exponent = `(|v| % 100) / 10`, i.e. `.2` is the exponent
    **2**, not 0.2.
  - weight default 1, conn default 1 (1=AND, 2=OR).
  - tolerate `,` between antecedent and consequent and optional spaces.
Parse errors → `int fz_fis_parse_text(const char *text, fz_fis_t *out, fz_mf_t *mf_pool, uint16_t pool_n, fz_rule_t *rule_pool, uint16_t rule_n, int16_t *ante_pool, int16_t *conse_pool, uint16_t idx_pool_n)` and
`int fz_fis_parse(const char *path, ...)` (host only, guarded by `#ifndef ARDUINO`).
Return 0 on success, negative error code. No malloc.

## G. Compile-time codegen (`tools/fis2h.py` → `<name>_fis.h`)

Emit `static const fz_mf_t`, `static const fz_var_t`, `static const int16_t`,
`static const fz_rule_t`, `static const fz_fis_t fz_fis_<name> = {...}`.
Names sanitized to C identifiers, strings truncated to `FZ_NAME_LEN` (default 20).
Output must compile with `-Werror -Wall -Wextra -pedantic` as C99 and C++11.
Also emit a `#define FZ_<NAME>_MAX_POINTS` hint when the file needs non-default points.

## H. Parity workflow

- `tools/fis2json.py` (reference reader, Python, double precision) → canonical JSON
  (`{"type","and","or","imp","agg","defuzz","n_points","inputs":[...],"outputs":[...],"rules":[...]}`).
- `tools/check_parity.py --fis X.fis --inputs in.txt --lib build/libfz.a --tol 3e-5`
  builds a tiny C harness, runs `fz_eval_all`, compares to the Python reference and
  reports max abs/rel error per output. Exit 1 on tolerance breach.
- Octave oracle: `tools/oracle.m` (agent C) loads the same `.fis` via `readfis`
  (toolkit) or `readfis` (MATLAB) and prints full-precision CSV for the same inputs,
  plus per-rule firing strength and aggregated output when available.
- Tolerance: `abs(err) <= 3e-5 * max(1, |ref|)` for Mamdani centroid/bisector/mom/som/lom
  at `n_points=101`; `1e-6` relative for Sugeno. Bit-exact only claimed for
  `n_points` and operator parity, not float32 vs float64.

## I. Deliverable layout

```
library.properties            Arduino library metadata (name=fz, arch=*)
library.json                  PlatformIO
keywords.txt
src/fz_types.h  fz_mf.h/.c  fz_ops.h/.c  fz_defuzz.h/.c  fz_eval.h/.c
src/fz_fis_parse.h/.c
src/fz.h                      umbrella header, Arduino-friendly
examples/tipper_mamdani/      runtime .fis from PROGMEM/SD, prints output
examples/tipper_sugeno/
examples/generated_static/    codegen'ed header, no filesystem
tests/host/                   make -C tests/host run   (assert-based, no framework)
tests/fis/                    *.fis fixtures + rules.mk
tools/                        fis2h.py fis2json.py check_parity.py oracle.m convert_fis.py
docs/COMPATIBILITY.md         what is identical, what is not, with measured error table
docs/MATLAB-VERIFY.md         exact commands to run in MATLAB (user side)
.github/workflows/ci.yml      host tests + arduino-cli compile matrix + codegen check
```

## J. Non-goals (do not implement)

Type-2 FIS, `genfis`/ANFIS training, Simulink blocks, Fuzzy Logic Designer GUI,
custom function handles (MATLAB `@func`), `addvar`/`addrule` mutation API.
Type-2 is planned as a separate opt-in build (`docs/TIR.md`), not part of parity core.
