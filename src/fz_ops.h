/* T-norm / S-norm operators — CONTRACT.md §E. */
#ifndef FZ_OPS_H
#define FZ_OPS_H

#include "fz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fold `v[0..n-1]` left-to-right with operator `op`. n == 0 returns 0. */
fz_real_t fz_op_fold(uint8_t op, const fz_real_t *v, uint8_t n);

/* Single pair, used by implication and aggregation. */
fz_real_t fz_op2(uint8_t op, fz_real_t a, fz_real_t b);

/* Parse a method name ("min", "probor", ...) to its id. -1 when unknown. */
int fz_op_from_name(const char *name);
int fz_defuzz_from_name(const char *name);
int fz_mf_type_from_name(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* FZ_OPS_H */
