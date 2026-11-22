/* test_ops.c — operators and name lookups (CONTRACT.md §E). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fz.h"

static int fails;

static void near(float got, float want, float tol, const char *what) {
    if (!(fabsf(got - want) <= tol)) {
        printf("  FAIL %s: got %.9g want %.9g\n", what, (double)got, (double)want);
        ++fails;
    }
}
static void ok(int c, const char *what) {
    if (!c) { printf("  FAIL %s\n", what); ++fails; }
}

int main(void) {
    /* pair operators */
    near(fz_op2(FZ_OP_MIN, 0.3f, 0.7f), 0.3f, 1e-7f, "min");
    near(fz_op2(FZ_OP_MAX, 0.3f, 0.7f), 0.7f, 1e-7f, "max");
    near(fz_op2(FZ_OP_PROD, 0.3f, 0.7f), 0.21f, 1e-7f, "prod");
    near(fz_op2(FZ_OP_SUM, 0.3f, 0.7f), 1.0f, 1e-7f, "sum");
    near(fz_op2(FZ_OP_PROBOR, 0.3f, 0.7f), 0.79f, 1e-7f, "probor = a+b-ab");
    near(fz_op2(FZ_OP_BOUNDED_SUM, 0.6f, 0.7f), 1.0f, 1e-7f, "bounded_sum clamps");
    near(fz_op2(FZ_OP_BOUNDED_SUM, 0.2f, 0.3f), 0.5f, 1e-7f, "bounded_sum");
    /* einstein_product(0.8,0.9) = .72/(2-(1.7-.72)) = .72/1.02 */
    near(fz_op2(FZ_OP_EINSTEIN_PRODUCT, 0.8f, 0.9f), 0.72f / 1.02f, 1e-6f, "einstein_product");
    near(fz_op2(FZ_OP_EINSTEIN_SUM, 0.8f, 0.9f), 1.7f / 1.72f, 1e-6f, "einstein_sum");
    /* hamacher_product(0.8,0.9) = .72/(1.7-.72) = .72/.98 */
    near(fz_op2(FZ_OP_HAMACHER_PRODUCT, 0.8f, 0.9f), 0.72f / 0.98f, 1e-6f, "hamacher_product");
    /* hamacher_sum(0.8,0.9) = (1.7-1.44)/(1-0.72) = .26/.28 */
    near(fz_op2(FZ_OP_HAMACHER_SUM, 0.8f, 0.9f), 0.26f / 0.28f, 1e-6f, "hamacher_sum");
    /* algebraic_product is prod, algebraic_sum is probor (reference aliases) */
    near(fz_op2(FZ_OP_ALGEBRAIC_PRODUCT, 0.4f, 0.5f), fz_op2(FZ_OP_PROD, 0.4f, 0.5f), 1e-7f, "algebraic_product alias");
    near(fz_op2(FZ_OP_ALGEBRAIC_SUM, 0.4f, 0.5f), fz_op2(FZ_OP_PROBOR, 0.4f, 0.5f), 1e-7f, "algebraic_sum alias");

    /* fold: left-to-right, matches MATLAB prod/sum/reduce order */
    {
        float v[] = {0.9f, 0.8f, 0.7f};
        near(fz_op_fold(FZ_OP_MIN, v, 3), 0.7f, 1e-7f, "fold min");
        near(fz_op_fold(FZ_OP_MAX, v, 3), 0.9f, 1e-7f, "fold max");
        near(fz_op_fold(FZ_OP_PROD, v, 3), 0.9f * 0.8f * 0.7f, 1e-6f, "fold prod");
        near(fz_op_fold(FZ_OP_PROD, v, 1), 0.9f, 1e-7f, "fold single");
        near(fz_op_fold(FZ_OP_MIN, v, 0), 0.0f, 1e-7f, "fold empty");
    }
    /* einstein_product fold must start at the identity 1, not re-fold x[0] */
    {
        float v[] = {0.8f, 0.9f};
        float e = 0.72f / 1.02f;
        near(fz_op_fold(FZ_OP_EINSTEIN_PRODUCT, v, 2), e, 1e-6f, "fold einstein_product identity");
    }

    /* name -> id */
    ok(fz_op_from_name("min") == FZ_OP_MIN, "op name min");
    ok(fz_op_from_name("probor") == FZ_OP_PROBOR, "op name probor");
    ok(fz_op_from_name("einstein_sum") == FZ_OP_EINSTEIN_SUM, "op name einstein_sum");
    ok(fz_op_from_name("nope") == -1, "op name unknown");
    ok(fz_defuzz_from_name("centroid") == FZ_DEFUZZ_CENTROID, "defuzz name centroid");
    ok(fz_defuzz_from_name("centroid_integral") == FZ_DEFUZZ_CENTROID_INTEGRAL, "defuzz name centroid_integral");
    ok(fz_defuzz_from_name("wtaver") == FZ_DEFUZZ_WTAVER, "defuzz name wtaver");
    ok(fz_mf_type_from_name("trapmf") == FZ_MF_TRAPMF, "mf name trapmf");
    ok(fz_mf_type_from_name("linear") == FZ_MF_LINEAR, "mf name linear");
    ok(fz_mf_type_from_name("nope") == -1, "mf name unknown");

    printf("test_ops: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
