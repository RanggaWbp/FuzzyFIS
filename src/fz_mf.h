/* Membership functions — formulas from CONTRACT.md §D, copied verbatim from MATLAB doc.
 * All in fz_real_t, no allocation, no exceptions. Degenerate divisors return 0 (no NaN/inf). */
#ifndef FZ_MF_H
#define FZ_MF_H

#include "fz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Evaluate one membership function at a crisp point.
 * hedge: power applied (0 = none). not_flag: 1 results are inverted (1 - mu).
 * `constant` and `linear` are Not evaluable here: returns 0 (handled by fz_eval). */
fz_real_t fz_mf_eval(uint8_t type, const fz_real_t *p, fz_real_t x, fz_real_t hedge, int not_flag);

/* Fill `n` samples over [lo, hi] with linspace spacing (MATLAB grid). */
void fz_linspace(fz_real_t lo, fz_real_t hi, fz_real_t *dst, uint16_t n);

/* Apply hedge/not to a value exactly like the reference does. */
fz_real_t fz_hedge_apply(fz_real_t mu, fz_real_t hedge, int not_flag);

#ifdef __cplusplus
}
#endif
#endif /* FZ_MF_H */
