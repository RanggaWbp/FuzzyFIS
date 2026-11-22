/* test_unbounded.c — proves the "no hardcoded counts" claim.
 *
 * A 12-input / 2-output / 6-MF-per-variable / 30-rule Sugeno system whose
 * linear consequents need 13 coefficients each. Twelve inputs exceed any
 * fixed-size antecedent buffer, and 13 coefficients exceed fz_mf_t.p[]. If a
 * cap remained anywhere, this would fail to parse or read out of bounds. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fz.h"

#define NIN 12
#define NOUT 2

int main(void) {
    char text[16384];
    size_t off = 0;
    int i, j;
    static fz_mf_t mf[128];
    static fz_var_t vin[NIN], vout[NOUT];
    static fz_rule_t rules[30];
    static int16_t idx[30 * (NIN + NOUT)];
    static float lin[NOUT * 6 * (NIN + 1)];
    static fz_work_t w;
    fz_fis_t fis;
    fz_pool_size_t need;
    float in[NIN], out[NOUT];
    int rc;

    off += (size_t)snprintf(text + off, sizeof text - off,
        "[System]\nName='big'\nType='sugeno'\nNumInputs=%d\nNumOutputs=%d\nNumRules=30\n"
        "AndMethod='prod'\nOrMethod='probor'\nImpMethod='prod'\nAggMethod='sum'\n"
        "DefuzzMethod='wtaver'\n", NIN, NOUT);
    for (i = 0; i < NIN; ++i)
        off += (size_t)snprintf(text + off, sizeof text - off,
            "[Input%d]\nName='i%d'\nRange=[0 1]\nNumMFs=6\n"
            "MF1='a':'trimf',[0 0 0.2]\nMF2='b':'trimf',[0 0.2 0.4]\n"
            "MF3='c':'trimf',[0.2 0.4 0.6]\nMF4='d':'trimf',[0.4 0.6 0.8]\n"
            "MF5='e':'trimf',[0.6 0.8 1]\nMF6='f':'trimf',[0.8 1 1]\n", i + 1, i);
    for (j = 0; j < NOUT; ++j) {
        off += (size_t)snprintf(text + off, sizeof text - off,
            "[Output%d]\nName='o%d'\nRange=[0 10]\nNumMFs=6\n", j + 1, j);
        for (i = 0; i < 6; ++i) {
            int k;
            off += (size_t)snprintf(text + off, sizeof text - off,
                "MF%d='l%d':'linear',[", i + 1, i);
            for (k = 0; k < NIN; ++k)
                off += (size_t)snprintf(text + off, sizeof text - off, "%d ", (k + i) % 3);
            off += (size_t)snprintf(text + off, sizeof text - off, "%d]\n", i);
        }
    }
    off += (size_t)snprintf(text + off, sizeof text - off, "[Rules]\n");
    for (i = 0; i < 30; ++i) {
        for (j = 0; j < NIN; ++j)
            off += (size_t)snprintf(text + off, sizeof text - off, "%d ", (i + j) % 6 + 1);
        off += (size_t)snprintf(text + off, sizeof text - off, ", %d %d (0.9) : 1\n",
                                (i % 6) + 1, ((i + 1) % 6) + 1);
    }

    fz_fis_measure(text, &need);
    printf("measure: in=%u out=%u rules=%u mf=%u lin=%u idx=%u\n",
           need.n_in, need.n_out, need.n_rule, need.n_mf, need.n_lin, need.n_idx);
    if (need.n_in != NIN || need.n_out != NOUT || need.n_rule != 30) { puts("MEASURE WRONG"); return 1; }

    rc = fz_fis_parse_text(text, strlen(text) + 1, &fis, mf, 128, vin, NIN, vout, NOUT,
                           rules, 30, idx, 30 * (NIN + NOUT), lin, NOUT * 6 * (NIN + 1));
    printf("parse rc=%d\n", rc);
    if (rc != 0) return 1;
    if (!fz_fis_check(&fis)) { puts("CHECK FAILED"); return 1; }
    printf("check: %u inputs, %u outputs, %u rules accepted\n", fis.n_in, fis.n_out, fis.n_rules);

    for (i = 0; i < NIN; ++i) in[i] = (float)i / (float)NIN;
    fz_eval_all(&fis, in, out, &w);
    printf("out: %.6f %.6f\n", out[0], out[1]);
    for (j = 0; j < NOUT; ++j) {
        if (!(out[j] > -1e30f && out[j] < 1e30f)) { puts("NON-FINITE OUTPUT"); return 1; }
    }
    puts("OK: 12 inputs, 6 MFs, 30 rules, 13-coefficient linear consequents");
    return 0;
}
