## What's new in 0.1.2

### Tsukamoto inference support
- New FIS type `FZ_TSUKAMOTO`: each rule is evaluated by inverting its
  monotonic consequent MF (`linsmf` / `linzmf`) at the firing strength.
- `<fz_eval.c` dispatches `fz_eval_tsukamoto`; `fz_fis_check` validates that
  Tsukamoto consequents are monotonic (falls back to a hard error otherwise).

### Source documentation
- Full English line/function comments added to `fz_ops.c` and
  `fz_fis_parse.c` (operator semantics, degenerate/divisor guards, parser
  helpers, `[System]`/`[Input#]`/`[Output#]`/`[Rules]` parsing flow).
- README stale counts corrected (reference systems, test binaries, files).

### Fixes
- README ZIP download label de-versioned so it points at `latest`.
