/* Evaluation engine — CONTRACT.md §C. Streaming: no rules x points matrix,
 * the only buffer is one output's aggregated set (caller-owned fz_work_t). */
#ifndef FZ_EVAL_H
#define FZ_EVAL_H

#include "fz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Decode a rule slot (scaled x100): sign -> NOT, /100 -> MF index, %100/10 -> hedge. */
typedef struct {
    uint16_t mf_index; /* 0 = omitted */
    float hedge;       /* 0 = none, 0.5 = somewhat, 2 = very, ... */
    int not_flag;
} fz_slot_t;

void fz_slot_decode(int16_t slot, fz_slot_t *out);

/* Fuzzify one input against a rule slot. */
float fz_fuzzify(const fz_var_t *var, int16_t slot, float x);

/* Firing strength for one rule over the supplied crisp inputs. */
float fz_firing(const fz_fis_t *fis, const fz_rule_t *rule, const float *in);

/* Evaluate one output index. `w` is caller scratch, reused per output. */
float fz_eval(const fz_fis_t *fis, const float *in, uint16_t out_index, fz_work_t *w);

/* Evaluate every output of the FIS into out[0..n_out-1]. */
void fz_eval_all(const fz_fis_t *fis, const float *in, float *out, fz_work_t *w);

/* Non-zero when the FIS passes structural checks (indices in range, pools set). */
int fz_fis_check(const fz_fis_t *fis);

#ifdef __cplusplus
}
#endif
#endif /* FZ_EVAL_H */
