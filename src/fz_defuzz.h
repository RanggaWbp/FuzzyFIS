/* Defuzzification over MATLAB's sample grid. No xs array is materialised:
 * x_k = lo + k*(hi-lo)/(n-1), k = 0..n-1 (MATLAB linspace), computed on the fly.
 * The only buffer the whole evaluator needs is one output's aggregated set. */
#ifndef FZ_DEFUZZ_H
#define FZ_DEFUZZ_H

#include "fz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* `ys` holds n membership samples over [lo, hi]. `fallback` is returned when the
 * area is zero (MATLAB returns the midpoint of the output range). */
float fz_defuzz(uint8_t method, float lo, float hi, const float *ys, uint16_t n, float fallback);

#ifdef __cplusplus
}
#endif
#endif /* FZ_DEFUZZ_H */
