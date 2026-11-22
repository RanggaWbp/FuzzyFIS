/* fz_eval.c — CONTRACT.md §C step for step. No allocation, no I/O, no globals. */
#include "fz_eval.h"
#include "fz_mf.h"
#include "fz_ops.h"
#include "fz_defuzz.h"
#include <math.h>

void fz_slot_decode(int16_t slot, fz_slot_t *out) {
    int16_t mag = slot;
    out->not_flag = 0;
    if (mag < 0) { out->not_flag = 1; mag = (int16_t)(-mag); }
    out->mf_index = (uint8_t)(mag / 100);
    /* CONTRACT §B: stored scaled by 100; exponent = (mag % 100) / 10, so .2 is "very". */
    out->hedge = (float)(mag % 100) / 10.0f;
    if (out->mf_index == 0) out->hedge = 0.0f;
}

float fz_fuzzify(const fz_var_t *var, int16_t slot, float x) {
    fz_slot_t s;
    fz_slot_decode(slot, &s);
    if (s.mf_index == 0 || var == 0 || var->mfs == 0) return 0.0f;
    if (s.mf_index > var->nmf) return 0.0f;
    {
        const fz_mf_t *mf = &var->mfs[s.mf_index - 1];
        return fz_mf_eval(mf->type, mf->p, x, s.hedge, s.not_flag);
    }
}

float fz_firing(const fz_fis_t *fis, const fz_rule_t *rule, const float *in) {
    float mus[FZ_LINEAR_MAX_IN + 1];
    uint8_t i, n = 0;

    if (rule->n > FZ_LINEAR_MAX_IN) return 0.0f;
    for (i = 0; i < rule->n; ++i) {
        int16_t slot = rule->ante[i];
        if (slot == 0) continue; /* variable omitted from the rule */
        mus[n++] = fz_fuzzify(&fis->in[i], slot, in[i]);
    }
    if (n == 0) return 0.0f;
    return rule->weight * fz_op_fold((rule->conn == 1) ? fis->and_op : fis->or_op, mus, n);
}

/* Mamdani: build the aggregated set for output `out_index` and defuzzify it. */
static float fz_eval_mamdani(const fz_fis_t *fis, const float *in, uint8_t out_index, fz_work_t *w) {
    const fz_var_t *ov = &fis->out[out_index];
    uint16_t np = fz_npoints(fis);
    uint16_t k;
    uint16_t r;

    for (k = 0; k < np; ++k) w->buf[k] = 0.0f;

    for (r = 0; r < fis->n_rules; ++r) {
        const fz_rule_t *rule = &fis->rules[r];
        fz_slot_t s;
        const fz_mf_t *mf;
        float strength;

        fz_slot_decode(rule->conse[out_index], &s);
        if (s.mf_index == 0 || s.mf_index > ov->nmf) continue;

        strength = fz_firing(fis, rule, in);
        if (strength == 0.0f) continue;

        mf = &ov->mfs[s.mf_index - 1];
        for (k = 0; k < np; ++k) {
            float x = (np <= 1) ? ov->lo
                                : ov->lo + ((float)k * (ov->hi - ov->lo)) / (float)(np - 1);
            float y = fz_mf_eval(mf->type, mf->p, x, s.hedge, s.not_flag);
            y = fz_op2(fis->imp_op, strength, y);          /* min | prod */
            w->buf[k] = fz_op2(fis->agg_op, w->buf[k], y); /* max | sum | probor */
        }
    }

    return fz_defuzz(fis->defuzz_op, ov->lo, ov->hi, w->buf, np,
                     (ov->lo + ov->hi) * 0.5f);
}

/* Sugeno: singleton (location, height) per rule, then weighted average/sum. */
static float fz_eval_sugeno(const fz_fis_t *fis, const float *in, uint8_t out_index) {
    const fz_var_t *ov = &fis->out[out_index];
    float num = 0.0f, den = 0.0f;
    uint16_t r;

    for (r = 0; r < fis->n_rules; ++r) {
        const fz_rule_t *rule = &fis->rules[r];
        fz_slot_t s;
        const fz_mf_t *mf;
        float h, z;

        fz_slot_decode(rule->conse[out_index], &s);
        if (s.mf_index == 0 || s.mf_index > ov->nmf) continue;

        h = fz_firing(fis, rule, in);
        if (h == 0.0f) continue;

        /* Congruent with the reference: hedge inverts for Sugeno, NOT applies after. */
        if (s.hedge != 0.0f) h = powf(h, 1.0f / s.hedge);
        if (s.not_flag) h = 1.0f - h;

        mf = &ov->mfs[s.mf_index - 1];
        if (mf->type == FZ_MF_CONSTANT) {
            z = mf->p[0];
        } else { /* linear: [a1..an c] . [in.. 1] */
            uint8_t i;
            uint8_t nin = (fis->n_in < FZ_LINEAR_MAX_IN) ? fis->n_in : (uint8_t)FZ_LINEAR_MAX_IN;
            z = 0.0f;
            for (i = 0; i < nin; ++i) z += mf->p[i] * in[i];
            z += mf->p[nin];
        }

        num += h * z;
        den += h;
    }

    if (fis->defuzz_op == FZ_DEFUZZ_WTSUM) return num;
    return (den == 0.0f) ? (ov->lo + ov->hi) * 0.5f : (num / den);
}

float fz_eval(const fz_fis_t *fis, const float *in, uint8_t out_index, fz_work_t *w) {
    if (!fis || !in || !w || out_index >= fis->n_out) return 0.0f;
    if (fis->type == FZ_SUGENO) return fz_eval_sugeno(fis, in, out_index);
    return fz_eval_mamdani(fis, in, out_index, w);
}

void fz_eval_all(const fz_fis_t *fis, const float *in, float *out, fz_work_t *w) {
    uint8_t j;
    if (!fis || !out) return;
    for (j = 0; j < fis->n_out; ++j) out[j] = fz_eval(fis, in, j, w);
}

int fz_fis_check(const fz_fis_t *fis) {
    uint16_t r;
    uint8_t j;
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
        }
    }
    return 1;
}
