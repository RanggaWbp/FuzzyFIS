/* fz_eval.c — CONTRACT.md §C step for step. No allocation, no I/O, no globals.
 *
 * The firing strength is folded directly over the participating antecedent
 * values, with no fixed-size scratch array. A buffer would cap the library at
 * whatever size it happened to be, and a FIS should not inherit that. */
#include "fz_eval.h"
#include "fz_mf.h"
#include "fz_ops.h"
#include "fz_defuzz.h"
#include <math.h>

void fz_slot_decode(int16_t slot, fz_slot_t *out) {
    int16_t mag = slot;
    out->not_flag = 0;
    if (mag < 0) { out->not_flag = 1; mag = (int16_t)(-mag); }
    out->mf_index = (uint16_t)(mag / 100);
    /* CONTRACT §B: stored scaled by 100; exponent = (mag % 100) / 10, so .2 is "very". */
    out->hedge = (fz_real_t)(mag % 100) / 10.0f;
    if (out->mf_index == 0) out->hedge = 0.0f;
}

fz_real_t fz_fuzzify(const fz_var_t *var, int16_t slot, fz_real_t x) {
    fz_slot_t s;
    fz_slot_decode(slot, &s);
    if (s.mf_index == 0 || var == 0 || var->mfs == 0) return 0.0f;
    if (s.mf_index > var->nmf) return 0.0f;
    {
        const fz_mf_t *mf = &var->mfs[s.mf_index - 1];
        return fz_mf_eval(mf->type, mf->p, x, s.hedge, s.not_flag);
    }
}

/* Fold the participating antecedent values left-to-right, no storage. */
static fz_real_t fz_fold_antecedent(const fz_fis_t *fis, const fz_rule_t *rule,
                                const fz_real_t *in, uint8_t op) {
    uint16_t i;
    fz_real_t acc = 0.0f;
    int have = 0;

    for (i = 0; i < rule->n && i < fis->n_in; ++i) {
        fz_real_t v;
        if (rule->ante[i] == 0) continue; /* variable omitted from the rule */
        v = fz_fuzzify(&fis->in[i], rule->ante[i], in[i]);
        if (!have) { acc = v; have = 1; }
        else acc = fz_op2(op, acc, v);
    }
    return have ? acc : 0.0f;
}

fz_real_t fz_firing(const fz_fis_t *fis, const fz_rule_t *rule, const fz_real_t *in) {
    uint8_t op;
    if (!fis || !rule || !in || rule->n > fis->n_in) return 0.0f;
    op = (rule->conn == 1) ? fis->and_op : fis->or_op;
    return rule->weight * fz_fold_antecedent(fis, rule, in, op);
}

/* Mamdani: build the aggregated set for output `out_index` and defuzzify it. */
static fz_real_t fz_eval_mamdani(const fz_fis_t *fis, const fz_real_t *in, uint16_t out_index, fz_work_t *w) {
    const fz_var_t *ov = &fis->out[out_index];
    uint16_t np = fz_npoints(fis);
    uint16_t k;
    uint16_t r;
    const fz_real_t step = (np <= 1) ? 0.0f : (ov->hi - ov->lo) / (fz_real_t)(np - 1);

    for (k = 0; k < np; ++k) w->buf[k] = 0.0f;

    for (r = 0; r < fis->n_rules; ++r) {
        const fz_rule_t *rule = &fis->rules[r];
        fz_slot_t s;
        const fz_mf_t *mf;
        fz_real_t strength;

        fz_slot_decode(rule->conse[out_index], &s);
        if (s.mf_index == 0 || s.mf_index > ov->nmf) continue;

        strength = fz_firing(fis, rule, in);
        if (strength == 0.0f) continue;

        mf = &ov->mfs[s.mf_index - 1];
        for (k = 0; k < np; ++k) {
            fz_real_t x = ov->lo + (fz_real_t)k * step;
            fz_real_t y = fz_mf_eval(mf->type, mf->p, x, s.hedge, s.not_flag);
            y = fz_op2(fis->imp_op, strength, y);          /* min | prod */
            w->buf[k] = fz_op2(fis->agg_op, w->buf[k], y); /* max | sum | probor */
        }
    }

    return fz_defuzz(fis->defuzz_op, ov->lo, ov->hi, w->buf, np,
                     (ov->lo + ov->hi) * 0.5f);
}

/* Sugeno: singleton (location, height) per rule, then weighted average/sum. */
static fz_real_t fz_eval_sugeno(const fz_fis_t *fis, const fz_real_t *in, uint16_t out_index) {
    const fz_var_t *ov = &fis->out[out_index];
    fz_real_t num = 0.0f, den = 0.0f;
    uint16_t r;

    for (r = 0; r < fis->n_rules; ++r) {
        const fz_rule_t *rule = &fis->rules[r];
        fz_slot_t s;
        const fz_mf_t *mf;
        fz_real_t h, z;

        fz_slot_decode(rule->conse[out_index], &s);
        if (s.mf_index == 0 || s.mf_index > ov->nmf) continue;

        h = fz_firing(fis, rule, in);
        if (h == 0.0f) continue;

        /* Congruent with the reference: hedge inverts for Sugeno, NOT applies after. */
        if (s.hedge != 0.0f) h = FZ_POW(h, 1.0f / s.hedge);
        if (s.not_flag) h = 1.0f - h;

        mf = &ov->mfs[s.mf_index - 1];
        if (mf->type == FZ_MF_LINEAR) {
            /* coefficients [a1..an c] over [in1..inn 1]; the count is nparam, so
             * the input count is bounded by the FIS, not by this library. */
            const fz_real_t *c = mf->lp;
            uint16_t i, n;
            if (!c) continue;
            n = mf->nparam;
            if (n > fis->n_in) n = fis->n_in;
            z = 0.0f;
            for (i = 0; i < n; ++i) z += c[i] * in[i];
            z += c[n];
        } else {
            z = mf->p[0]; /* constant */
        }

        num += h * z;
        den += h;
    }

    if (fis->defuzz_op == FZ_DEFUZZ_WTSUM) return num;
    return (den == 0.0f) ? (ov->lo + ov->hi) * 0.5f : (num / den);
}

fz_real_t fz_eval(const fz_fis_t *fis, const fz_real_t *in, uint16_t out_index, fz_work_t *w) {
    if (!fis || !in || !w || out_index >= fis->n_out) return 0.0f;
    if (fis->type == FZ_SUGENO) return fz_eval_sugeno(fis, in, out_index);
    return fz_eval_mamdani(fis, in, out_index, w);
}

void fz_eval_all(const fz_fis_t *fis, const fz_real_t *in, fz_real_t *out, fz_work_t *w) {
    uint16_t j;
    if (!fis || !out) return;
    for (j = 0; j < fis->n_out; ++j) out[j] = fz_eval(fis, in, j, w);
}

int fz_fis_check(const fz_fis_t *fis) {
    uint16_t r, j;
    if (!fis || !fis->in || !fis->out || (fis->n_rules && !fis->rules)) return 0;
    if (fis->n_in == 0 || fis->n_out == 0) return 0;
    for (r = 0; r < fis->n_rules; ++r) {
        if (!fis->rules[r].ante || !fis->rules[r].conse) return 0;
        if (fis->rules[r].n != fis->n_in) return 0;
        for (j = 0; j < fis->n_in; ++j) {
            fz_slot_t s;
            fz_slot_decode(fis->rules[r].ante[j], &s);
            if (s.mf_index > fis->in[j].nmf) return 0;
        }
        for (j = 0; j < fis->n_out; ++j) {
            fz_slot_t s;
            fz_slot_decode(fis->rules[r].conse[j], &s);
            if (s.mf_index > fis->out[j].nmf) return 0;
            if (s.mf_index) {
                const fz_mf_t *mf = &fis->out[j].mfs[s.mf_index - 1];
                if (mf->type == FZ_MF_LINEAR && (!mf->lp || mf->nparam < fis->n_in + 1)) return 0;
            }
        }
    }
    return 1;
}
