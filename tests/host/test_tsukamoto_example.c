/* host: validate the tipper_tsukamoto example FIS parses, passes check, and
 * evaluates sanely. Mirrors the runtime parser path used by the .ino. */
#include "fz.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static char fis_text[4096];

static int load_fis(const char *path, fz_fis_t *fis,
                    fz_mf_t *mfs, uint16_t nmf,
                    fz_var_t *in, uint16_t nin,
                    fz_var_t *out, uint16_t nout,
                    fz_rule_t *rules, uint16_t nrules,
                    int16_t *idx, uint16_t nidx,
                    float *lin, uint16_t nlin) {
    FILE *f = fopen(path, "rb");
    if (!f) return -100;
    size_t n = fread(fis_text, 1, sizeof fis_text - 1, f);
    fclose(f);
    fis_text[n] = '\0';
    return fz_fis_parse_text(fis_text, n + 1, fis,
                             mfs, nmf, in, nin, out, nout,
                             rules, nrules, idx, nidx, lin, nlin);
}

int main(void) {
    const char *base = "examples/tipper_tsukamoto/tipper.fis";
    fz_mf_t mf_pool[16];
    fz_var_t in_pool[4], out_pool[4];
    fz_rule_t rule_pool[8];
    int16_t idx_pool[32];
    float lin_pool[16];
    fz_fis_t fis;
    fz_work_t work;
    float out[1];

    int rc = load_fis(base, &fis,
                      mf_pool, 16, in_pool, 4, out_pool, 4,
                      rule_pool, 8, idx_pool, 32, lin_pool, 16);
    fprintf(stderr, "parse rc=%d\n", rc);
    fflush(stderr);
    assert(rc == FZ_OK);
    assert(fis.type == FZ_TSUKAMOTO);
    assert(fis.n_in == 2 && fis.n_out == 1 && fis.n_rules == 3);
    assert(fz_fis_check(&fis) == 1);

    /* service=0 food=0 -> all poor/rancid -> cheap (linzmf [0 10]) inverts
     * to the low end; service=10 food=10 -> all generous -> high end. */
    fz_eval_all(&fis, (fz_real_t[]){ 0.0f, 0.0f }, out, &work);
    printf("tip(s0,f0)=%.3f\n", out[0]);
    fz_eval_all(&fis, (fz_real_t[]){ 10.0f, 10.0f }, out, &work);
    printf("tip(s10,f10)=%.3f\n", out[0]);
    fz_eval_all(&fis, (fz_real_t[]){ 5.0f, 5.0f }, out, &work);
    printf("tip(s5,f5)=%.3f\n", out[0]);

    /* monotonicity of outputs: low end < high end */
    float low, high, mid;
    fz_eval_all(&fis, (fz_real_t[]){ 0.0f, 0.0f }, &low, &work);
    fz_eval_all(&fis, (fz_real_t[]){ 10.0f, 10.0f }, &high, &work);
    fz_eval_all(&fis, (fz_real_t[]){ 5.0f, 5.0f }, &mid, &work);
    assert(low < mid && mid < high);

    printf("test_tsukamoto_example: ok\n");
    return 0;
}
