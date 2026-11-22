## What's new in 0.1.1

### `fz_real_t` — float32 default, option 64-bit double
- All `float` fields/functions now use `fz_real_t` (default `float32`).
- Build `-DFZ_USE_DOUBLE` for 64-bit `double` (`fz_real_t` → `double`).
- Parity vs Octave: worst rel err ~1.37e-6 (float), ~4.74e-9 (double).

### STM32F103 build gate
- New `tests/stm32f103` cross-compile gate (Cortex-M3, soft-float).
- Measured: 13.6 KB flash / 1.5 KB RAM (float32); 17.5 KB / 1.9 KB (double).
- `make -C tests/stm32f103 run`, `FZ_DOUBLE=1` supported.

### CI — no more hardcoded fixture lists
- `tools/scan_fis_codegen.py` auto-discovers every tracked `*.fis`,
  regenerates headers, and fails on drift (C99 + C++11 compile).
- New `stm32f103` job: cross-compile float + double.
- Parity job added over golden fixtures.

### Tooling / fixes
- `fis2json.py` bisector now mirrors C/Octave; grid-quantisation documented.
- trimf/trapmf degenerate shoulder fix in oracle.
- `compare_serial.py`: compare live MCU-serial vs MATLAB reference.
- CI moved off Node 20 actions.
