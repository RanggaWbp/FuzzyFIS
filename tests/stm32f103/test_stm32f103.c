/* STM32F103 bare-metal smoke test (Cortex-M3, no FPU, soft-float).
 * Builds every src .c into a cortex-m3 ELF and checks size + self-check.
 * Not run on target; this is a compile/link/static-analysis gate. */
#include "fz.h"
#include <stdint.h>

/* small Mamdani FIS: 1 input trimf, 1 output trimf, 1 rule */
static const fz_real_t in_mf[3] = { 0, 5, 10 };
static const fz_mf_t in_mfs[1] = { { FZ_MF_TRIMF, 3, {0,5,10}, 0 } };
static const fz_mf_t out_mfs[1] = { { FZ_MF_TRIMF, 3, {0,5,10}, 0 } };
static const fz_var_t ins[1] = { { "in", 0, 10, 1, in_mfs } };
static const fz_var_t outs[1] = { { "out", 0, 10, 1, out_mfs } };
static const int16_t ante[1] = { 1 }; /* slot: mf 1, no hedge/not */
static const int16_t conse[1] = { 1 };
static const fz_rule_t rule = { 1, 1, 1.f, ante, conse };
static const fz_fis_t fis = { "s", FZ_MAMDANI, 1, 1, 1, ins, outs, &rule,
                              FZ_OP_MIN, FZ_OP_MAX, FZ_OP_MIN, FZ_OP_MAX,
                              FZ_DEFUZZ_CENTROID, 101 };
static fz_work_t work;
static fz_real_t out[1];

int main(void) {
    if (!fz_fis_check(&fis)) return 1;
    out[0] = fz_eval(&fis, in_mf, 0, &work);
    /* x=5 => trimf apex => centroid ~5 */
    if (out[0] < 4.5f || out[0] > 5.5f) return 2;
    return 0;
}
