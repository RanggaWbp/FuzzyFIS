/* test_mf.c — membership function shape + degenerate cases.
 * Values from the MATLAB Fuzzy Logic Toolbox MF definitions (CONTRACT.md §D). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fz.h"

static int fails;

static void ok(int cond, const char *what) {
    if (!cond) { printf("  FAIL %s\n", what); ++fails; }
}

static void near(float got, float want, float tol, const char *what) {
    if (!(fabsf(got - want) <= tol)) {
        printf("  FAIL %s: got %.9g want %.9g\n", what, (double)got, (double)want);
        ++fails;
    }
}

int main(void) {
    /* trimf [0 5 10] */
    {
        static const float p[] = {0, 5, 10};
        float xs[] = {-1, 0, 2.5f, 5, 7.5f, 10, 11};
        float want[] = {0, 0, 0.5f, 1, 0.5f, 0, 0};
        size_t i;
        for (i = 0; i < sizeof xs / sizeof xs[0]; ++i)
            near(fz_mf_eval(FZ_MF_TRIMF, p, xs[i], 0, 0), want[i], 1e-6f, "trimf");
    }
    /* trapmf [0 0 1 3] shoulder, and [7 9 10 10] shoulder */
    {
        static const float p1[] = {0, 0, 1, 3};
        static const float p2[] = {7, 9, 10, 10};
        near(fz_mf_eval(FZ_MF_TRAPMF, p1, 0.0f, 0, 0), 1.0f, 1e-6f, "trapmf shoulder left (x==a==b)");
        near(fz_mf_eval(FZ_MF_TRAPMF, p1, 2.0f, 0, 0), 0.5f, 1e-6f, "trapmf right slope");
        near(fz_mf_eval(FZ_MF_TRAPMF, p2, 10.0f, 0, 0), 1.0f, 1e-6f, "trapmf shoulder right (x==c==d)");
        near(fz_mf_eval(FZ_MF_TRAPMF, p2, 8.0f, 0, 0), 0.5f, 1e-6f, "trapmf left slope");
    }
    /* gaussmf [1.5 5] */
    {
        static const float p[] = {1.5f, 5};
        near(fz_mf_eval(FZ_MF_GAUSSMF, p, 5.0f, 0, 0), 1.0f, 1e-6f, "gaussmf centre");
        near(fz_mf_eval(FZ_MF_GAUSSMF, p, 6.5f, 0, 0), expf(-0.5f), 1e-6f, "gaussmf sigma");
    }
    /* gbellmf [2 4 6] */
    {
        static const float p[] = {2, 4, 6};
        near(fz_mf_eval(FZ_MF_GBELLMF, p, 6.0f, 0, 0), 1.0f, 1e-6f, "gbellmf centre");
        near(fz_mf_eval(FZ_MF_GBELLMF, p, 8.0f, 0, 0), 1.0f / (1.0f + 1.0f), 1e-6f, "gbellmf a=2");
    }
    /* sigmf / dsigmf / psigmf / gauss2mf */
    {
        static const float p[] = {2, 5};
        near(fz_mf_eval(FZ_MF_SIGMF, p, 5.0f, 0, 0), 0.5f, 1e-6f, "sigmf centre");
        near(fz_mf_eval(FZ_MF_SIGMF, p, 0.0f, 0, 0), 1.0f / (1.0f + expf(10.0f)), 1e-6f, "sigmf tails");
    }
    {
        static const float p[] = {2, 3, 2, 7}; /* dsigmf */
        float v = fz_mf_eval(FZ_MF_DSIGMF, p, 3.0f, 0, 0);
        near(v, 0.5f - 1.0f / (1.0f + expf(8.0f)), 1e-6f, "dsigmf");
    }
    {
        static const float p[] = {1, 3, 1, 7}; /* gauss2mf */
        near(fz_mf_eval(FZ_MF_GAUSS2MF, p, 5.0f, 0, 0), 1.0f, 1e-6f, "gauss2mf plateau");
        near(fz_mf_eval(FZ_MF_GAUSS2MF, p, 2.0f, 0, 0), expf(-0.5f), 1e-6f, "gauss2mf left");
        near(fz_mf_eval(FZ_MF_GAUSS2MF, p, 8.0f, 0, 0), expf(-0.5f), 1e-6f, "gauss2mf right");
    }
    /* zmf / smf / pimf / linsmf / linzmf */
    {
        static const float p[] = {2, 8};
        near(fz_mf_eval(FZ_MF_ZMF, p, 2.0f, 0, 0), 1.0f, 1e-6f, "zmf left");
        near(fz_mf_eval(FZ_MF_ZMF, p, 8.0f, 0, 0), 0.0f, 1e-6f, "zmf right");
        near(fz_mf_eval(FZ_MF_ZMF, p, 5.0f, 0, 0), 0.5f, 1e-6f, "zmf midpoint");
        near(fz_mf_eval(FZ_MF_SMF, p, 2.0f, 0, 0), 0.0f, 1e-6f, "smf left");
        near(fz_mf_eval(FZ_MF_SMF, p, 8.0f, 0, 0), 1.0f, 1e-6f, "smf right");
        near(fz_mf_eval(FZ_MF_SMF, p, 5.0f, 0, 0), 0.5f, 1e-6f, "smf midpoint");
        near(fz_mf_eval(FZ_MF_LINSMF, p, 2.0f, 0, 0), 0.0f, 1e-6f, "linsmf a");
        near(fz_mf_eval(FZ_MF_LINSMF, p, 8.0f, 0, 0), 1.0f, 1e-6f, "linsmf b");
        near(fz_mf_eval(FZ_MF_LINZMF, p, 2.0f, 0, 0), 1.0f, 1e-6f, "linzmf a");
        near(fz_mf_eval(FZ_MF_LINZMF, p, 8.0f, 0, 0), 0.0f, 1e-6f, "linzmf b");
    }
    {
        static const float p[] = {1, 3, 6, 9}; /* pimf */
        near(fz_mf_eval(FZ_MF_PIMF, p, 4.0f, 0, 0), 1.0f, 1e-6f, "pimf plateau");
        near(fz_mf_eval(FZ_MF_PIMF, p, 1.0f, 0, 0), 0.0f, 1e-6f, "pimf left edge");
    }
    /* hedge + NOT: very(x) = x^2, somewhat(x) = x^0.5, not = 1 - x */
    {
        static const float p[] = {0, 5, 10};
        near(fz_mf_eval(FZ_MF_TRIMF, p, 2.5f, 2.0f, 0), 0.25f, 1e-6f, "hedge very");
        near(fz_mf_eval(FZ_MF_TRIMF, p, 2.5f, 0.5f, 0), 0.70710678f, 1e-6f, "hedge somewhat");
        near(fz_mf_eval(FZ_MF_TRIMF, p, 2.5f, 0.0f, 1), 0.5f, 1e-6f, "not");
        near(fz_mf_eval(FZ_MF_TRIMF, p, 2.5f, 2.0f, 1), 0.75f, 1e-6f, "not very");
    }
    /* degenerate: no NaN / inf may escape */
    {
        static const float dz[] = {5, 5, 5};
        static const float sg[] = {0, 5};
        static const float zb[] = {5, 5};
        float v;
        v = fz_mf_eval(FZ_MF_TRIMF, dz, 5.0f, 0, 0);
        ok(!isnan(v) && !isinf(v), "trimf degenerate finite");
        v = fz_mf_eval(FZ_MF_GAUSSMF, sg, 5.0f, 0, 0);
        ok(!isnan(v) && !isinf(v), "gaussmf sigma=0 finite");
        v = fz_mf_eval(FZ_MF_ZMF, zb, 5.0f, 0, 0);
        ok(!isnan(v) && !isinf(v), "zmf a==b finite");
        v = fz_mf_eval(FZ_MF_TRAPMF, dz, 5.0f, 0, 0);
        ok(!isnan(v) && !isinf(v), "trapmf fully degenerate finite");
    }
    /* outside every support must stay in [0,1] */
    {
        static const float p[] = {0, 5, 10};
        int i;
        for (i = -50; i <= 50; ++i) {
            float v = fz_mf_eval(FZ_MF_TRIMF, p, (float)i * 0.37f, 0, 0);
            if (v < 0.0f || v > 1.0f) { printf("  FAIL trimf out of range at %g\n", (double)((float)i * 0.37f)); ++fails; break; }
        }
    }
    /* linspace matches MATLAB: first == lo, last == hi, uniform */
    {
        float buf[101];
        fz_linspace(0.0f, 30.0f, buf, 101);
        near(buf[0], 0.0f, 1e-7f, "linspace first");
        near(buf[100], 30.0f, 1e-4f, "linspace last");
        near(buf[50], 15.0f, 1e-4f, "linspace middle");
    }

    printf("test_mf: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
