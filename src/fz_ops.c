/* fz_ops.c — CONTRACT.md §E verbatim. */
#include "fz_ops.h"
#include <string.h>

static fz_real_t fz_pair(uint8_t op, fz_real_t a, fz_real_t b) {
    switch (op) {
    case FZ_OP_MIN: return (a < b) ? a : b;
    case FZ_OP_MAX: return (a > b) ? a : b;
    case FZ_OP_PROD:
    case FZ_OP_ALGEBRAIC_PRODUCT: return a * b;
    case FZ_OP_SUM: return a + b;
    case FZ_OP_PROBOR:
    case FZ_OP_ALGEBRAIC_SUM: return a + b - a * b;
    case FZ_OP_BOUNDED_SUM: { fz_real_t s = a + b; return (s > 1.0f) ? 1.0f : s; }
    case FZ_OP_EINSTEIN_PRODUCT: { fz_real_t d = 2.0f - (a + b - a * b); return (d == 0.0f) ? 0.0f : (a * b) / d; }
    case FZ_OP_EINSTEIN_SUM: { fz_real_t d = 1.0f + a * b; return (d == 0.0f) ? 0.0f : (a + b) / d; }
    case FZ_OP_HAMACHER_PRODUCT: { fz_real_t d = a + b - a * b; return (d == 0.0f) ? 0.0f : (a * b) / d; }
    case FZ_OP_HAMACHER_SUM: { fz_real_t d = 1.0f - a * b; if (d == 0.0f) return (a == 1.0f && b == 1.0f) ? 1.0f : 0.0f; return (a + b - 2.0f * a * b) / d; }
    default: return 0.0f;
    }
}

fz_real_t fz_op2(uint8_t op, fz_real_t a, fz_real_t b) { return fz_pair(op, a, b); }

fz_real_t fz_op_fold(uint8_t op, const fz_real_t *v, uint8_t n) {
    uint8_t i;
    fz_real_t acc;
    if (n == 0) return 0.0f;
    acc = v[0];
    for (i = 1; i < n; ++i) acc = fz_pair(op, acc, v[i]);
    return acc;
}

static int fz_eq(const char *a, const char *b) { return strcmp(a, b) == 0; }

int fz_op_from_name(const char *name) {
    if (!name) return -1;
    if (fz_eq(name, "min")) return FZ_OP_MIN;
    if (fz_eq(name, "max")) return FZ_OP_MAX;
    if (fz_eq(name, "prod")) return FZ_OP_PROD;
    if (fz_eq(name, "sum")) return FZ_OP_SUM;
    if (fz_eq(name, "probor")) return FZ_OP_PROBOR;
    if (fz_eq(name, "algebraic_product")) return FZ_OP_ALGEBRAIC_PRODUCT;
    if (fz_eq(name, "algebraic_sum")) return FZ_OP_ALGEBRAIC_SUM;
    if (fz_eq(name, "bounded_sum")) return FZ_OP_BOUNDED_SUM;
    if (fz_eq(name, "einstein_product")) return FZ_OP_EINSTEIN_PRODUCT;
    if (fz_eq(name, "einstein_sum")) return FZ_OP_EINSTEIN_SUM;
    if (fz_eq(name, "hamacher_product")) return FZ_OP_HAMACHER_PRODUCT;
    if (fz_eq(name, "hamacher_sum")) return FZ_OP_HAMACHER_SUM;
    if (fz_eq(name, "bounded_difference")) return FZ_OP_MIN; /* unsupported: degrade to min */
    if (fz_eq(name, "drastic_product")) return FZ_OP_MIN;
    return -1;
}

int fz_defuzz_from_name(const char *name) {
    if (!name) return -1;
    if (fz_eq(name, "centroid")) return FZ_DEFUZZ_CENTROID;
    if (fz_eq(name, "bisector")) return FZ_DEFUZZ_BISECTOR;
    if (fz_eq(name, "mom")) return FZ_DEFUZZ_MOM;
    if (fz_eq(name, "som")) return FZ_DEFUZZ_SOM;
    if (fz_eq(name, "lom")) return FZ_DEFUZZ_LOM;
    if (fz_eq(name, "wtaver")) return FZ_DEFUZZ_WTAVER;
    if (fz_eq(name, "wtsum")) return FZ_DEFUZZ_WTSUM;
    if (fz_eq(name, "centroid_integral")) return FZ_DEFUZZ_CENTROID_INTEGRAL;
    return -1;
}

int fz_mf_type_from_name(const char *name) {
    if (!name) return -1;
    if (fz_eq(name, "trimf")) return FZ_MF_TRIMF;
    if (fz_eq(name, "trapmf")) return FZ_MF_TRAPMF;
    if (fz_eq(name, "gaussmf")) return FZ_MF_GAUSSMF;
    if (fz_eq(name, "gauss2mf")) return FZ_MF_GAUSS2MF;
    if (fz_eq(name, "gbellmf")) return FZ_MF_GBELLMF;
    if (fz_eq(name, "sigmf")) return FZ_MF_SIGMF;
    if (fz_eq(name, "dsigmf")) return FZ_MF_DSIGMF;
    if (fz_eq(name, "psigmf")) return FZ_MF_PSIGMF;
    if (fz_eq(name, "zmf")) return FZ_MF_ZMF;
    if (fz_eq(name, "smf")) return FZ_MF_SMF;
    if (fz_eq(name, "pimf")) return FZ_MF_PIMF;
    if (fz_eq(name, "linsmf")) return FZ_MF_LINSMF;
    if (fz_eq(name, "linzmf")) return FZ_MF_LINZMF;
    if (fz_eq(name, "constant")) return FZ_MF_CONSTANT;
    if (fz_eq(name, "linear")) return FZ_MF_LINEAR;
    return -1;
}
