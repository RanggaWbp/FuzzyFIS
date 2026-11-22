/* .fis text parser — CONTRACT.md §F. No malloc: every table is a caller pool. */
#ifndef FZ_FIS_PARSE_H
#define FZ_FIS_PARSE_H

#include "fz_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes (negative; 0 == success). */
#define FZ_OK 0
#define FZ_ERR_ARG (-1)      /* null pointer or n_points out of range */
#define FZ_ERR_SECTION (-2)  /* [System]/[Input#]/[Output#]/[Rules] missing or malformed */
#define FZ_ERR_KEY (-3)      /* unrecognised or unparsable key */
#define FZ_ERR_POOL (-4)     /* caller pool too small */
#define FZ_ERR_RULE (-5)     /* rule line does not match the grammar */
#define FZ_ERR_TYPE (-6)     /* unknown FIS type / MF type / operator name */
#define FZ_ERR_COUNT (-7)    /* NumInputs / NumOutputs / NumMFs / NumRules disagree with content */

/* Sizes a caller must supply for a given .fis text. Cheap counting pass.
 * The struct itself lives in fz_types.h (fz_pool_size_t). */
void fz_fis_measure(const char *text, fz_pool_size_t *out);

/* Parse `text` into `out` using the supplied pools. `text_len` bounds the scan,
 * so a buffer that is not NUL-terminated is safe: pass the byte count. Pass 0 to
 * mean "strlen(text)" when you know it is terminated. `idx_pool` holds every
 * rule's antecedent slots followed by its consequent slots, contiguous.
 * Pools are not freed by the library; they are yours. On any error the
 * function returns a negative code and `out` must be considered invalid. */
int fz_fis_parse_text(const char *text, size_t text_len, fz_fis_t *out,
                      fz_mf_t *mf_pool, uint16_t n_mf,
                      fz_var_t *in_pool, uint16_t n_in,
                      fz_var_t *out_pool, uint16_t n_out,
                      fz_rule_t *rule_pool, uint16_t n_rule,
                      int16_t *idx_pool, uint16_t n_idx);

#ifndef ARDUINO
/* Host convenience: read a file into memory, then parse. `buf` is caller-owned. */
int fz_fis_parse_file(const char *path, char *buf, unsigned buf_len,
                      fz_fis_t *out,
                      fz_mf_t *mf_pool, uint16_t n_mf,
                      fz_var_t *in_pool, uint16_t n_in,
                      fz_var_t *out_pool, uint16_t n_out,
                      fz_rule_t *rule_pool, uint16_t n_rule,
                      int16_t *idx_pool, uint16_t n_idx);
#endif

#ifdef __cplusplus
}
#endif
#endif /* FZ_FIS_PARSE_H */
