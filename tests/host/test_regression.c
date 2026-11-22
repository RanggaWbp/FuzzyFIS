/* test_regression.c -- numerical regression against the Octave oracle.
 *
 * Every expected number below was produced by running the real MATLAB-compatible
 * oracle, never typed by hand:
 *
 *   octave: addpath('/var/tmp/flt-src/inst');
 *           f = readfis('mamdani_tip_calculator.fis');
 *           y = evalfis([10 2], f)
 *
 * Grid: inputs 1, 2.5, 4, 5.5, 7, 10 x 1, 3, 5, 6.5, 8, 10 -> 36 rows per FIS,
 * printed with printf('%.17g').  Tolerance: 3e-5 relative (CONTRACT §H),
 * compared as |got - ref| <= tol * max(1, |ref|).
 *
 * Self-contained: the FIS text is embedded, so this file needs no fixtures.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fz.h"
#include "fz_fis_parse.h"

#ifndef FZ_REGRESSION_TOL
#define FZ_REGRESSION_TOL 3e-5f
#endif

#define MAX_ROWS 36
#define MAX_MF 128
#define MAX_VARS 8
#define MAX_RULES 256
#define MAX_IDX 4096

/* ---------------------------------------------------------------- FIS text */

static const char mamdani_text[] =
    "[System]\n"
    "Name='Mamdani-Tip-Calculator'\n"
    "Type='mamdani'\n"
    "NumInputs=2\n"
    "NumOutputs=2\n"
    "NumRules=4\n"
    "AndMethod='min'\n"
    "OrMethod='max'\n"
    "ImpMethod='min'\n"
    "AggMethod='max'\n"
    "DefuzzMethod='centroid'\n"
    "[Input1]\n"
    "Name='Food-Quality'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Input2]\n"
    "Name='Service'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Output1]\n"
    "Name='Tip'\n"
    "Range=[0 30]\n"
    "NumMFs=3\n"
    "MF1='About-Ten-Percent':'gaussmf',[2 10]\n"
    "MF2='About-Fifteen-Percent':'gaussmf',[2 15]\n"
    "MF3='About-Twenty-Percent':'gaussmf',[2 20]\n"
    "[Output2]\n"
    "Name='Check-Plus-Tip'\n"
    "Range=[1 1.3]\n"
    "NumMFs=3\n"
    "MF1='Plus-About-Ten-Percent':'gaussmf',[0.02 1.10]\n"
    "MF2='Plus-About-Fifteen-Percent':'gaussmf',[0.02 1.15]\n"
    "MF3='Plus-About-Twenty-Percent':'gaussmf',[0.02 1.20]\n"
    "[Rules]\n"
    "1 1, 1 1 (1) : 1\n"
    "1 2, 2 2 (1) : 1\n"
    "2 1, 2 2 (1) : 1\n"
    "2 2, 3 3 (1) : 1\n";

static const char sugeno_text[] =
    "[System]\n"
    "Name='Sugeno-Tip-Calculator'\n"
    "Type='sugeno'\n"
    "NumInputs=2\n"
    "NumOutputs=3\n"
    "NumRules=10\n"
    "AndMethod='einstein_product'\n"
    "OrMethod='einstein_sum'\n"
    "ImpMethod='prod'\n"
    "AggMethod='sum'\n"
    "DefuzzMethod='wtaver'\n"
    "[Input1]\n"
    "Name='Food-Quality'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Input2]\n"
    "Name='Service'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Output1]\n"
    "Name='Cheap-Tip'\n"
    "Range=[5 25]\n"
    "NumMFs=3\n"
    "MF1='Low':'constant',[10]\n"
    "MF2='Medium':'constant',[15]\n"
    "MF3='High':'constant',[20]\n"
    "[Output2]\n"
    "Name='Average-Tip'\n"
    "Range=[5 25]\n"
    "NumMFs=3\n"
    "MF1='Low':'constant',[10]\n"
    "MF2='Medium':'constant',[15]\n"
    "MF3='High':'constant',[20]\n"
    "[Output3]\n"
    "Name='Generous-Tip'\n"
    "Range=[5 25]\n"
    "NumMFs=3\n"
    "MF1='Low':'constant',[10]\n"
    "MF2='Medium':'constant',[15]\n"
    "MF3='High':'constant',[20]\n"
    "[Rules]\n"
    "1.30 1.30, 1.30 1.20 1.00 (1) : 1\n"
    "2.00 1.30, 1.00 1.00 2.00 (1) : 1\n"
    "2.20 1.20, 1.00 2.00 3.00 (1) : 1\n"
    "1.00 1.00, 1.00 1.00 2.00 (1) : 1\n"
    "2.00 1.00, 1.00 2.00 3.00 (1) : 1\n"
    "2.30 1.00, 1.00 2.00 3.20 (1) : 1\n"
    "1.00 2.00, 1.00 2.00 3.00 (1) : 1\n"
    "2.00 2.00, 2.00 2.00 3.20 (1) : 1\n"
    "1.20 2.20, 1.00 2.00 3.00 (1) : 1\n"
    "2.40 2.40, 3.00 3.20 3.30 (1) : 1\n";

static const char linear_text[] =
    "[System]\n"
    "Name='Linear-Tip-Calculator'\n"
    "Type='sugeno'\n"
    "NumInputs=2\n"
    "NumOutputs=1\n"
    "NumRules=4\n"
    "AndMethod='min'\n"
    "OrMethod='max'\n"
    "ImpMethod='min'\n"
    "AggMethod='max'\n"
    "DefuzzMethod='wtaver'\n"
    "[Input1]\n"
    "Name='Food-Quality'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Input2]\n"
    "Name='Service'\n"
    "Range=[1 10]\n"
    "NumMFs=2\n"
    "MF1='Bad':'trapmf',[0 1 3 7]\n"
    "MF2='Good':'trapmf',[3 7 10 11]\n"
    "[Output1]\n"
    "Name='Tip'\n"
    "Range=[10 20]\n"
    "NumMFs=3\n"
    "MF1='Ten-Percent':'linear',[0 0 10]\n"
    "MF2='Fifteen-Percent':'linear',[0 0 15]\n"
    "MF3='Twenty-Percent':'linear',[0 0 20]\n"
    "[Rules]\n"
    "1 1, 1 (1) : 1\n"
    "1 2, 2 (1) : 1\n"
    "2 1, 2 (1) : 1\n"
    "2 2, 3 (1) : 1\n";

/* ------------------------------------------------------- oracle tables */

static const float mamdani_ref[MAX_ROWS][4] = {
    { 1.0f, 1.0f, 10.000002f, 1.10000002f },
    { 1.0f, 3.0f, 10.000002f, 1.10000002f },
    { 1.0f, 5.0f, 12.5012441f, 1.12501244f },
    { 1.0f, 6.5f, 14.1528683f, 1.14152868f },
    { 1.0f, 8.0f, 15.0f, 1.15f },
    { 1.0f, 10.0f, 15.0f, 1.15f },
    { 2.5f, 1.0f, 10.000002f, 1.10000002f },
    { 2.5f, 3.0f, 10.000002f, 1.10000002f },
    { 2.5f, 5.0f, 12.5012441f, 1.12501244f },
    { 2.5f, 6.5f, 14.1528683f, 1.14152868f },
    { 2.5f, 8.0f, 15.0f, 1.15f },
    { 2.5f, 10.0f, 15.0f, 1.15f },
    { 4.0f, 1.0f, 11.4720468f, 1.11472047f },
    { 4.0f, 3.0f, 11.4720468f, 1.11472047f },
    { 4.0f, 5.0f, 14.0918054f, 1.14091805f },
    { 4.0f, 6.5f, 15.6023813f, 1.15602381f },
    { 4.0f, 8.0f, 16.4696672f, 1.16469667f },
    { 4.0f, 10.0f, 16.4696672f, 1.16469667f },
    { 5.5f, 1.0f, 12.991828f, 1.12991828f },
    { 5.5f, 3.0f, 12.991828f, 1.12991828f },
    { 5.5f, 5.0f, 15.3955531f, 1.15395553f },
    { 5.5f, 6.5f, 17.0086749f, 1.17008675f },
    { 5.5f, 8.0f, 17.99557f, 1.1799557f },
    { 5.5f, 10.0f, 17.99557f, 1.1799557f },
    { 7.0f, 1.0f, 15.0f, 1.15f },
    { 7.0f, 3.0f, 15.0f, 1.15f },
    { 7.0f, 5.0f, 17.4987559f, 1.17498756f },
    { 7.0f, 6.5f, 19.1543487f, 1.19154349f },
    { 7.0f, 8.0f, 19.999998f, 1.19999998f },
    { 7.0f, 10.0f, 19.999998f, 1.19999998f },
    { 10.0f, 1.0f, 15.0f, 1.15f },
    { 10.0f, 3.0f, 15.0f, 1.15f },
    { 10.0f, 5.0f, 17.4987559f, 1.17498756f },
    { 10.0f, 6.5f, 19.1543487f, 1.19154349f },
    { 10.0f, 8.0f, 19.999998f, 1.19999998f },
    { 10.0f, 10.0f, 19.999998f, 1.19999998f }
};

static const float sugeno_ref[MAX_ROWS][5] = {
    { 1.0f, 1.0f, 10.0f, 10.0f, 12.5f },
    { 1.0f, 3.0f, 10.0f, 10.0f, 12.5f },
    { 1.0f, 5.0f, 10.0f, 12.3385564f, 17.2727273f },
    { 1.0f, 6.5f, 10.0f, 14.5325661f, 19.6353591f },
    { 1.0f, 8.0f, 10.0f, 15.0f, 20.0f },
    { 1.0f, 10.0f, 10.0f, 15.0f, 20.0f },
    { 2.5f, 1.0f, 10.0f, 10.0f, 12.5f },
    { 2.5f, 3.0f, 10.0f, 10.0f, 12.5f },
    { 2.5f, 5.0f, 10.0f, 12.3385564f, 17.2727273f },
    { 2.5f, 6.5f, 10.0f, 14.5325661f, 19.6353591f },
    { 2.5f, 8.0f, 10.0f, 15.0f, 20.0f },
    { 2.5f, 10.0f, 10.0f, 15.0f, 20.0f },
    { 4.0f, 1.0f, 10.0f, 10.8295856f, 15.0420168f },
    { 4.0f, 3.0f, 10.0f, 10.8295856f, 15.0420168f },
    { 4.0f, 5.0f, 10.3466382f, 12.7736378f, 18.4367893f },
    { 4.0f, 6.5f, 10.7223758f, 14.7845627f, 19.7726905f },
    { 4.0f, 8.0f, 10.8229426f, 15.1923077f, 20.0f },
    { 4.0f, 10.0f, 10.8229426f, 15.1923077f, 20.0f },
    { 5.5f, 1.0f, 10.0f, 12.5302542f, 17.8429586f },
    { 5.5f, 3.0f, 10.0f, 12.5302542f, 17.8429586f },
    { 5.5f, 5.0f, 11.1346496f, 14.2004642f, 19.3800923f },
    { 5.5f, 6.5f, 12.8915445f, 15.8462531f, 19.9106162f },
    { 5.5f, 8.0f, 13.5963753f, 16.2755102f, 20.0f },
    { 5.5f, 10.0f, 13.5963753f, 16.2755102f, 20.0f },
    { 7.0f, 1.0f, 10.0f, 13.75f, 18.75f },
    { 7.0f, 3.0f, 10.0f, 13.75f, 18.75f },
    { 7.0f, 5.0f, 11.6129032f, 15.2941176f, 19.7673175f },
    { 7.0f, 6.5f, 15.92148f, 17.0010235f, 19.995695f },
    { 7.0f, 8.0f, 17.5f, 17.5f, 20.0f },
    { 7.0f, 10.0f, 17.5f, 17.5f, 20.0f },
    { 10.0f, 1.0f, 10.0f, 13.75f, 18.75f },
    { 10.0f, 3.0f, 10.0f, 13.75f, 18.75f },
    { 10.0f, 5.0f, 11.6129032f, 15.2941176f, 19.7673175f },
    { 10.0f, 6.5f, 15.92148f, 17.0010235f, 19.995695f },
    { 10.0f, 8.0f, 17.5f, 17.5f, 20.0f },
    { 10.0f, 10.0f, 17.5f, 17.5f, 20.0f }
};

static const float linear_ref[MAX_ROWS][3] = {
    { 1.0f, 1.0f, 10.0f },
    { 1.0f, 3.0f, 10.0f },
    { 1.0f, 5.0f, 12.5f },
    { 1.0f, 6.5f, 14.375f },
    { 1.0f, 8.0f, 15.0f },
    { 1.0f, 10.0f, 15.0f },
    { 2.5f, 1.0f, 10.0f },
    { 2.5f, 3.0f, 10.0f },
    { 2.5f, 5.0f, 12.5f },
    { 2.5f, 6.5f, 14.375f },
    { 2.5f, 8.0f, 15.0f },
    { 2.5f, 10.0f, 15.0f },
    { 4.0f, 1.0f, 11.25f },
    { 4.0f, 3.0f, 11.25f },
    { 4.0f, 5.0f, 14.1666667f },
    { 4.0f, 6.5f, 15.5f },
    { 4.0f, 8.0f, 16.25f },
    { 4.0f, 10.0f, 16.25f },
    { 5.5f, 1.0f, 13.125f },
    { 5.5f, 3.0f, 13.125f },
    { 5.5f, 5.0f, 15.3571429f },
    { 5.5f, 6.5f, 17.0f },
    { 5.5f, 8.0f, 18.125f },
    { 5.5f, 10.0f, 18.125f },
    { 7.0f, 1.0f, 15.0f },
    { 7.0f, 3.0f, 15.0f },
    { 7.0f, 5.0f, 17.5f },
    { 7.0f, 6.5f, 19.375f },
    { 7.0f, 8.0f, 20.0f },
    { 7.0f, 10.0f, 20.0f },
    { 10.0f, 1.0f, 15.0f },
    { 10.0f, 3.0f, 15.0f },
    { 10.0f, 5.0f, 17.5f },
    { 10.0f, 6.5f, 19.375f },
    { 10.0f, 8.0f, 20.0f },
    { 10.0f, 10.0f, 20.0f }
};

/* ------------------------------------------------------------- harness */

static fz_mf_t mf_pool[MAX_MF];
static fz_var_t in_pool[MAX_VARS], out_pool[MAX_VARS];
static fz_rule_t rule_pool[MAX_RULES];
static int16_t idx_pool[MAX_IDX];
static fz_work_t work;

static int failures;

static int run_fis(const char *label, const char *text, const float *ref,
                   int n_in, int n_out)
{
    fz_fis_t fis;
    float in[4], out[4];
    int i, j, bad = 0;
    float worst_abs = 0.0f, worst_rel = 0.0f, worst_ref = 0.0f;

    memset(&fis, 0, sizeof fis);
    i = fz_fis_parse_text(text, 0 /* strlen(text) */, &fis, mf_pool, MAX_MF,
                          in_pool, MAX_VARS, out_pool, MAX_VARS,
                          rule_pool, MAX_RULES, idx_pool, MAX_IDX);
    if (i != 0) {
        printf("%-26s PARSE ERROR %d\n", label, i);
        failures++;
        return 1;
    }

    for (i = 0; i < MAX_ROWS; i++) {
        const float *row = ref + (size_t)i * (size_t)(n_in + n_out);
        if (fis.n_in != n_in || fis.n_out != n_out) {
            printf("%-26s shape mismatch: fis %ux%u, table %dx%d\n", label,
                   (unsigned)fis.n_in, (unsigned)fis.n_out, n_in, n_out);
            failures++;
            return 1;
        }
        for (j = 0; j < n_in; j++)
            in[j] = row[j];
        fz_eval_all(&fis, in, out, &work);
        for (j = 0; j < n_out; j++) {
            float r = row[n_in + j];
            float e = fabsf(out[j] - r);
            float rel = e / (fabsf(r) > 1.0f ? fabsf(r) : 1.0f);
            if (rel > worst_rel)
                worst_rel = rel;
            if (fabsf(r) > worst_ref)
                worst_ref = fabsf(r);
            if (e > worst_abs)
                worst_abs = e;
            if (!(rel <= FZ_REGRESSION_TOL)) {
                if (bad < 5)
                    printf("  %s row %d out %d: got %.9g ref %.9g rel %.3e\n",
                           label, i, j, (double)out[j], (double)r, (double)rel);
                bad++;
            }
        }
    }
    printf("%-26s rows=%d max_abs=%.3e max_rel=%.3e (worst ref %.6g) %s\n", label,
           MAX_ROWS, (double)worst_abs, (double)worst_rel, (double)worst_ref,
           bad ? "FAIL" : "ok");
    if (bad)
        failures += bad;
    return bad;
}

int main(void)
{
    run_fis("mamdani_tip_calculator", mamdani_text, &mamdani_ref[0][0], 2, 2);
    run_fis("sugeno_tip_calculator", sugeno_text, &sugeno_ref[0][0], 2, 3);
    run_fis("linear_tip_calculator", linear_text, &linear_ref[0][0], 2, 1);

    if (failures) {
        printf("FAILED: %d mismatches over tolerance %g\n", failures,
               (double)FZ_REGRESSION_TOL);
        return 1;
    }
    printf("ok: 3 FIS x 36 points within %g relative\n", (double)FZ_REGRESSION_TOL);
    return 0;
}
