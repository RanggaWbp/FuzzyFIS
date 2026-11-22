/* fz — MATLAB-compatible fuzzy inference core, C11, no malloc.
 * Contract: CONTRACT.md (normative). Do not change signatures without updating it. */
#ifndef FZ_TYPES_H
#define FZ_TYPES_H

#include <stdint.h>
#include <math.h>

#ifndef FZ_MAX_POINTS
#define FZ_MAX_POINTS 101 /* MATLAB evalfisOptions.NumSamplePoints default */
#endif
#ifndef FZ_NAME_LEN
#define FZ_NAME_LEN 20 /* stored name length, including NUL */
#endif
/* Longest parameter list among the 13 standard membership functions: pimf,
 * gauss2mf, dsigmf and psigmf all take 4. `linear` consequents are NOT bounded
 * here: they carry their coefficients through fz_mf_t.lp, which points at an
 * array of any length. Nothing here limits the number of variables, MFs or
 * rules; those are sized by the caller's pools. */
#ifndef FZ_MAX_MF_PARAMS
#define FZ_MAX_MF_PARAMS 4
#endif

/* Scalar type for all numerical values (MF params, inputs, outputs, scratch).
 * Default is float (32-bit). Define FZ_USE_DOUBLE to build a 64-bit library.
 * 64-bit requires hardware double on the MCU (ESP32, Cortex-M4F/FPU, ...).
 * On AVR (software float64) it is very slow — keep the float default there. */
#ifdef FZ_USE_DOUBLE
typedef double fz_real_t;
#define FZ_EXP(x)   exp(x)
#define FZ_POW(a,b) pow(a, b)
#define FZ_FABS(x)  fabs(x)
#else
typedef float fz_real_t;
#define FZ_EXP(x)   expf(x)
#define FZ_POW(a,b) powf(a, b)
#define FZ_FABS(x)  fabsf(x)
#endif

typedef enum {
    FZ_MAMDANI = 0,
    FZ_SUGENO = 1
} fz_type_t;

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
    uint8_t type;   /* fz_mf_type_t */
    uint8_t nparam;
    fz_real_t p[FZ_MAX_MF_PARAMS];
    /* Sugeno `linear` only: coefficients [a1 .. an c], nparam of them, pointing
     * at caller-owned storage. This is how a linear consequent gets an
     * arbitrary number of inputs without a compile-time cap. NULL for every
     * other MF type. */
    const fz_real_t *lp;
} fz_mf_t;

typedef struct {
    char name[FZ_NAME_LEN];
    fz_real_t lo, hi;
    uint16_t nmf;
    const fz_mf_t *mfs;
} fz_var_t;

typedef struct {
    uint16_t n;     /* number of input slots in `ante` (== n_in of the FIS) */
    uint8_t conn;   /* 1 = AND, 2 = OR */
    fz_real_t weight;
    const int16_t *ante;   /* 0 omitted | sign = NOT | 2-digit fraction = hedge x0.1 */
    const int16_t *conse;  /* one slot per output */
} fz_rule_t;

typedef struct {
    char name[FZ_NAME_LEN];
    fz_type_t type;
    uint16_t n_in, n_out;
    uint16_t n_rules;
    const fz_var_t *in, *out;
    const fz_rule_t *rules;
    uint8_t and_op, or_op, imp_op, agg_op, defuzz_op; /* fz_op_t / fz_defuzz_t */
    uint16_t n_points; /* 0 -> FZ_MAX_POINTS */
} fz_fis_t;

/* Caller-owned scratch: one output's aggregated set. Never heap-allocated. */
typedef struct {
    fz_real_t buf[FZ_MAX_POINTS];
} fz_work_t;

/* Pool sizes needed for a parsed FIS. */
typedef struct {
    uint16_t n_mf, n_rule, n_idx, n_ante, n_conse;
    uint16_t n_in, n_out;
    uint16_t n_lin;  /* floats needed for `linear` consequent coefficients */
} fz_pool_size_t;

static inline uint16_t fz_npoints(const fz_fis_t *f) {
    return (uint16_t)((f->n_points == 0 || f->n_points > FZ_MAX_POINTS) ? FZ_MAX_POINTS : f->n_points);
}

#endif /* FZ_TYPES_H */
