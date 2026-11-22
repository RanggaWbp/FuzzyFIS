/* test_parse.c — .fis parser + golden regression.
 *
 * tests/golden/<name>.csv holds (inputs..., outputs...) rows produced by the
 * MATLAB-compatible oracle (Octave + fuzzy-logic-toolkit, patched where the
 * toolkit diverges from MATLAB). Every row below is compared against the real
 * library output through the real parser: no hand-typed expected values.
 *
 * Tolerance: 3e-5 relative (CONTRACT.md §H).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fz.h"

#define GOLDEN_DIR "tests/golden"
#ifndef TOL
#define TOL 3e-5
#endif

static char text[131072];
static fz_mf_t mf[256];
static fz_var_t vin[16], vout[16];
static fz_rule_t rules[512];
static int16_t idx[4096];
static fz_work_t work;

static int fails;

static int load(const char *path, char *buf, size_t cap) {
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = 0;
    return (int)n;
}

/* Compare one golden file against the library, parsing the .fis it uses. */
/* Bisector is resolved on the sample grid, in the reference as well as here, so
 * two conforming implementations can differ by at most ONE grid step. That is
 * a structural property of the algorithm, not slop: state it instead of hiding
 * it. Everything else is held to the relative tolerance below. */
static void run_case_tol(const char *name, const char *fis_path, double step_tol) {
    char path[256];
    char line[1024];
    fz_fis_t fis;
    FILE *g;
    int rc, rows = 0, bad = 0;
    double worst_abs = 0.0, worst_rel = 0.0;
    float in_worst[16];
    int n_in_worst = 0;

    rc = load(fis_path, text, sizeof text);
    if (rc < 0) { printf("  FAIL %s: cannot read %s\n", name, fis_path); ++fails; return; }
    rc = fz_fis_parse_text(text, sizeof text, &fis, mf, 256, vin, 16, vout, 16, rules, 512, idx, 4096);
    if (rc != 0) { printf("  FAIL %s: parse rc=%d\n", name, rc); ++fails; return; }
    if (!fz_fis_check(&fis)) { printf("  FAIL %s: structure check\n", name); ++fails; return; }

    snprintf(path, sizeof path, "%s/%s.csv", GOLDEN_DIR, name);
    g = fopen(path, "r");
    if (!g) { printf("  FAIL %s: cannot read golden %s\n", name, path); ++fails; return; }

    while (fgets(line, sizeof line, g)) {
        float in[16], out[16];
        double ref[16];
        char *p = line, *end;
        int k = 0, j;
        for (;;) {
            double v = strtod(p, &end);
            if (end == p) break;
            ref[k++] = v;
            p = end;
        }
        if (k != fis.n_in + fis.n_out) continue;
        for (j = 0; j < fis.n_in; ++j) in[j] = (float)ref[j];
        fz_eval_all(&fis, in, out, &work);
        for (j = 0; j < fis.n_out; ++j) {
            double got = (double)out[j], want = ref[fis.n_in + j];
            double d = fabs(got - want);
            double allowed = TOL * (fabs(want) > 1.0 ? fabs(want) : 1.0);
            double r = d / (fabs(want) > 1.0 ? fabs(want) : 1.0);
            if (r > worst_rel) { worst_rel = r; memcpy(in_worst, in, sizeof(float) * (size_t)fis.n_in); n_in_worst = fis.n_in; }
            if (d > worst_abs) worst_abs = d;
            if (d > allowed && d > step_tol) {
                if (bad < 3)
                    printf("    mismatch in=(%g,%g) out=%d got %.9g want %.9g rel=%.2e\n",
                           (double)in[0], fis.n_in > 1 ? (double)in[1] : 0.0, j, got, want, r);
                ++bad;
            }
        }
        ++rows;
    }
    fclose(g);

    if (rows == 0) { printf("  FAIL %s: empty golden\n", name); ++fails; return; }
    printf("  %-26s rows=%3d  worst_abs=%.2e worst_rel=%.2e%s %s\n",
           name, rows, worst_abs, worst_rel,
           step_tol > 0 ? " (grid-quantised)" : "", bad ? "FAIL" : "ok");
    if (bad) {
        printf("    (%d/%d values outside tolerance", bad, rows * fis.n_out);
        if (n_in_worst) {
            int i;
            printf(", worst input:");
            for (i = 0; i < n_in_worst; ++i) printf(" %g", (double)in_worst[i]);
        }
        printf(")\n");
        ++fails;
    }
}

static void run_case(const char *name, const char *fis_path) {
    run_case_tol(name, fis_path, 0.0);
}

int main(void) {
    struct { const char *name; const char *fis; } cases[] = {
        {"cubic_approximator", "tests/golden/cubic_approximator.fis"},
        {"defuzz-lom", "tests/golden/defuzz-lom.fis"},
        {"defuzz-mom", "tests/golden/defuzz-mom.fis"},
        {"heart_disease_risk", "tests/golden/heart_disease_risk.fis"},
        {"hedges", "tests/golden/hedges.fis"},
        {"investment_portfolio", "tests/golden/investment_portfolio.fis"},
        {"linear_tip_calculator", "tests/golden/linear_tip_calculator.fis"},
        {"mamdani_tip_calculator", "tests/golden/mamdani_tip_calculator.fis"},
        {"mf-gauss", "tests/golden/mf-gauss.fis"},
        {"mf-sigmoid", "tests/golden/mf-sigmoid.fis"},
        {"mf-zs", "tests/golden/mf-zs.fis"},
        {"sugeno-linear2", "tests/golden/sugeno-linear2.fis"},
        {"sugeno_tip_calculator", "tests/golden/sugeno_tip_calculator.fis"},
    };
    size_t i;

    /* Structural expectations, so a silently-truncating parser cannot pass. */
    {
        fz_pool_size_t need;
        if (load("tests/fis/mamdani_tip_calculator.fis", text, sizeof text) > 0) {
            fz_fis_measure(text, &need);
            if (need.n_in != 2 || need.n_out != 2 || need.n_rule != 4 || need.n_mf != 10) {
                printf("  FAIL measure: in=%u out=%u rules=%u mf=%u\n",
                       need.n_in, need.n_out, need.n_rule, need.n_mf);
                ++fails;
            }
        }
    }
    /* Pool overflow must be reported, never silently truncated. */
    {
        fz_fis_t fis;
        if (load("tests/fis/mamdani_tip_calculator.fis", text, sizeof text) > 0) {
            int rc = fz_fis_parse_text(text, sizeof text, &fis, mf, 2 /* far too small */, vin, 16,
                                       vout, 16, rules, 512, idx, 4096);
            if (rc != FZ_ERR_POOL) { printf("  FAIL tiny pool rc=%d (want %d)\n", rc, FZ_ERR_POOL); ++fails; }
        }
    }
    /* NumMFs disagreeing with the MF lines is a hard error, not a guess. */
    {
        static const char bad[] =
            "[System]\nName='x'\nType='mamdani'\nNumInputs=1\nNumOutputs=1\nNumRules=1\n"
            "[Input1]\nName='a'\nRange=[0 10]\nNumMFs=3\nMF1='lo':'trimf',[0 0 5]\n"
            "[Output1]\nName='y'\nRange=[0 10]\nNumMFs=1\nMF1='o':'trimf',[0 5 10]\n"
            "[Rules]\n1, 1 (1) : 1\n";
        fz_fis_t fis;
        int rc = fz_fis_parse_text(bad, sizeof bad, &fis, mf, 256, vin, 16, vout, 16, rules, 512, idx, 4096);
        if (rc != FZ_ERR_COUNT) { printf("  FAIL NumMFs mismatch rc=%d (want %d)\n", rc, FZ_ERR_COUNT); ++fails; }
    }
    /* Rule slot encoding: "2.2" must decode to MF 2 with exponent 2. */
    {
        fz_slot_t s;
        fz_slot_decode(220, &s);
        if (s.mf_index != 2 || fabsf(s.hedge - 2.0f) > 1e-6f || s.not_flag) {
            printf("  FAIL slot 220 -> mf=%u hedge=%g not=%d\n", s.mf_index, (double)s.hedge, s.not_flag);
            ++fails;
        }
        fz_slot_decode(-205, &s);
        if (s.mf_index != 2 || fabsf(s.hedge - 0.5f) > 1e-6f || !s.not_flag) {
            printf("  FAIL slot -205 -> mf=%u hedge=%g not=%d\n", s.mf_index, (double)s.hedge, s.not_flag);
            ++fails;
        }
        fz_slot_decode(0, &s);
        if (s.mf_index != 0) { printf("  FAIL slot 0 should be omitted\n"); ++fails; }
    }

    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) run_case(cases[i].name, cases[i].fis);
    /* bisector resolves on the sample grid: one grid step is the structural limit */
    run_case_tol("edge_hedges", "tests/fis/edge_hedges.fis", 100.0 / 100.0);

    printf("test_parse: %s (%d failures)\n", fails ? "FAIL" : "ok", fails);
    return fails ? 1 : 0;
}
