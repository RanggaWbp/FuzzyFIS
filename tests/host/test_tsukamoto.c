/* Tsukamoto evaluation + validation host test. Self-contained, assert-based.
 * Model: 1 input × 1 output, 2 rules.
 *   x low  = trAPMF [0 0 4 6]; x high = trapMF [4 6 10 10]
 *   y low  = LINZMF [0 10]  (monotonic decreasing)
 *   y high = LINSMF [0 10]  (monotonic increasing)
 * Rule1: IF x is low  THEN y is low
 * Rule2: IF x is high THEN y is high
 *
 * Tsukamoto inverse: LINSMF z = a + h*(b-a); LINZMF z = b - h*(b-a).
 * Weighted average: out = sum(h*z)/sum(h).
 *   x=2  -> low=1.0, high=0.0 -> h1=1 -> z=0 -> out=0
 *   x=5  -> low=0.5, high=0.5 -> z1=5, z2=5 -> out=5
 *   x=8  -> low=0.0, high=1.0 -> h2=1 -> z=10 -> out=10
 *   x=12 -> low=0, high=0 -> den=0 -> fallback mid (0+10)/2=5
 */
#include "fz.h"
#include <stdio.h>
#include <assert.h>

static const fz_mf_t in_mf[2] = {
    { FZ_MF_TRAPMF, 4, { 0.0f, 0.0f, 4.0f, 6.0f }, 0 },
    { FZ_MF_TRAPMF, 4, { 4.0f, 6.0f, 10.0f, 10.0f }, 0 },
};
static const fz_mf_t out_mf[2] = {
    { FZ_MF_LINZMF, 2, { 0.0f, 10.0f }, 0 },
    { FZ_MF_LINSMF, 2, { 0.0f, 10.0f }, 0 },
};
static const fz_var_t in_var[1] = {
    { "x", 0.0f, 10.0f, 2, in_mf },
};
static const fz_var_t out_var[1] = {
    { "y", 0.0f, 10.0f, 2, out_mf },
};
static const int16_t ante[4] = { 100, 200 };
static const int16_t conse[2] = { 100, 200 };
static const fz_rule_t rules[2] = {
    { 1, 1, 1.0f, ante + 0, conse + 0 },
    { 1, 1, 1.0f, ante + 1, conse + 1 },
};
static const fz_fis_t fis = {
    "tsuk", FZ_TSUKAMOTO, 1, 1, 2,
    in_var, out_var, rules,
    FZ_OP_MIN, FZ_OP_MAX, FZ_OP_PROD, FZ_OP_SUM,
    FZ_DEFUZZ_WTAVER, 101
};

static void close(float got, float want) {
    float tol = 3e-5f;
    float d = want - got;
    if (d < 0) d = -d;
    assert(d <= tol * (want > 1.0f ? want : 1.0f));
}

int main(void) {
    fz_work_t w;
    fz_real_t out;

    assert(fz_fis_check(&fis) == 1);

    /* x=2 */
    out = fz_eval(&fis, (fz_real_t[]){ 2.0f }, 0, &w);
    close(out, 0.0f);

    /* x=5 */
    out = fz_eval(&fis, (fz_real_t[]){ 5.0f }, 0, &w);
    close(out, 5.0f);

    /* x=8 */
    out = fz_eval(&fis, (fz_real_t[]){ 8.0f }, 0, &w);
    close(out, 10.0f);

    /* x=12 (all firing zero -> fallback midline) */
    out = fz_eval(&fis, (fz_real_t[]){ 12.0f }, 0, &w);
    close(out, 5.0f);

    /* WTSUM returns numerator sum(h*z), not the average. */
    {
        static const fz_fis_t fis_wtsum = {
            "tsuk", FZ_TSUKAMOTO, 1, 1, 2,
            in_var, out_var, rules,
            FZ_OP_MIN, FZ_OP_MAX, FZ_OP_PROD, FZ_OP_SUM,
            FZ_DEFUZZ_WTSUM, 101
        };
        out = fz_eval(&fis_wtsum, (fz_real_t[]){ 5.0f }, 0, &w);
        close(out, 5.0f); /* 0.5*5 + 0.5*5 = 5 */
    }

    /* Validation: a non-monotonic consequent MF must fail check. */
    {
        static const fz_mf_t bad_out_mf[2] = {
            { FZ_MF_TRAPMF, 4, { 0.0f, 0.0f, 4.0f, 6.0f }, 0 },
            { FZ_MF_LINSMF, 2, { 0.0f, 10.0f }, 0 },
        };
        static const fz_var_t bad_out_var[1] = {
            { "y", 0.0f, 10.0f, 2, bad_out_mf },
        };
        static const fz_fis_t bad_fis = {
            "tsuk", FZ_TSUKAMOTO, 1, 1, 2,
            in_var, bad_out_var, rules,
            FZ_OP_MIN, FZ_OP_MAX, FZ_OP_PROD, FZ_OP_SUM,
            FZ_DEFUZZ_WTAVER, 101
        };
        assert(fz_fis_check(&bad_fis) == 0);
    }

    printf("test_tsukamoto: ok\n");
    return 0;
}
