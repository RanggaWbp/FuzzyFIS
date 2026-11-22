/* fz_fis_parse.c — single pass, no allocation, no stdio in the parse path.
 * Grammar and slot encoding: CONTRACT.md §B and §F. */
#include "fz_fis_parse.h"
#include "fz_ops.h"

#include <string.h>
#ifndef ARDUINO
#include <stdio.h>
#endif

/* ------------------------------------------------------------------ lexing */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* Skip blanks and comments (MATLAB '%' and the toolkit's '##'). */
static const char *skip_blanks(const char *p) {
    for (;;) {
        while (*p && is_space(*p)) ++p;
        if (*p == '%') { while (*p && *p != '\n') ++p; continue; }
        break;
    }
    return p;
}

static int name_eq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

/* Copy a quoted-or-bare token into dst, truncated to n-1. */
static void copy_token(const char *p, char *dst, unsigned n) {
    unsigned i = 0;
    while (*p && is_space(*p)) ++p;
    if (*p == '\'' || *p == '"') {
        char q = *p++;
        while (*p && *p != q) {
            if (i + 1 < n) dst[i++] = *p;
            ++p;
        }
    } else {
        while (*p && !is_space(*p) && *p != ',' && *p != ']') {
            if (i + 1 < n) dst[i++] = *p;
            ++p;
        }
    }
    dst[i] = 0;
}

static int read_int(const char **pp, long *out) {
    const char *p = skip_blanks(*pp);
    int neg = 0;
    long v = 0;
    int any = 0;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); ++p; }
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; any = 1; }
    if (!any) return 0;
    *out = neg ? -v : v;
    *pp = p;
    return 1;
}

static int read_float(const char **pp, float *out) {
    const char *p = skip_blanks(*pp);
    int neg = 0, any = 0;
    double v = 0.0, scale = 0.1;
    if (*p == '-' || *p == '+') { neg = (*p == '-'); ++p; }
    while (*p >= '0' && *p <= '9') { v = v * 10.0 + (double)(*p - '0'); ++p; any = 1; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') { v += (double)(*p - '0') * scale; scale *= 0.1; ++p; any = 1; }
    }
    if (*p == 'e' || *p == 'E') {
        const char *save = p;
        int eneg = 0, eany = 0;
        long e = 0;
        ++p;
        if (*p == '-' || *p == '+') { eneg = (*p == '-'); ++p; }
        while (*p >= '0' && *p <= '9') { e = e * 10 + (*p - '0'); ++p; eany = 1; }
        if (!eany) { p = save; }
        else while (e--) v = eneg ? v / 10.0 : v * 10.0;
    }
    if (!any) return 0;
    *out = (float)(neg ? -v : v);
    *pp = p;
    return 1;
}

/* Parse `[a b c]` into p[]; returns the count, -1 on syntax error. */
static int read_vector(const char **pp, float *p, int max) {
    const char *q = skip_blanks(*pp);
    int n = 0;
    if (*q != '[') return -1;
    ++q;
    for (;;) {
        float v;
        q = skip_blanks(q);
        if (*q == ']') { ++q; break; }
        if (*q == ',' || is_space(*q)) { ++q; continue; }
        if (!read_float(&q, &v)) return -1;
        if (n < max) p[n] = v;
        ++n;
    }
    *pp = q;
    return n;
}

/* Rule token "2.2" / "-2.05" -> scaled-by-100 int. */
static int read_slot(const char **pp, int16_t *out) {
    const char *p = skip_blanks(*pp);
    int neg = 0;
    long whole = 0, frac = 0, fdigits = 0;
    if (*p == '-') { neg = 1; ++p; }
    else if (*p == '+') { ++p; }
    if (*p < '0' || *p > '9') return 0;
    while (*p >= '0' && *p <= '9') { whole = whole * 10 + (*p - '0'); ++p; }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9' && fdigits < 2) { frac = frac * 10 + (*p - '0'); ++p; ++fdigits; }
        while (*p >= '0' && *p <= '9') ++p; /* extra digits ignored, matches round-to-2dp */
        if (fdigits == 1) frac *= 10;
        if (fdigits == 0) frac = 0;
    }
    {
        long v = whole * 100 + frac;
        if (v > 32000) return 0;
        *out = (int16_t)(neg ? -v : v);
    }
    *pp = p;
    return 1;
}

/* --------------------------------------------------------------- sections */

typedef enum { SEC_NONE = 0, SEC_SYSTEM, SEC_INPUT, SEC_OUTPUT, SEC_RULES } sec_t;

static int name_eq_n(const char *a, const char *b, unsigned n) {
    unsigned i;
    for (i = 0; i < n; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 1;
}

static void measure_text(const char *text, fz_pool_size_t *sz) {
    const char *p = text;
    int sec = SEC_NONE, idx_in = 0, idx_out = 0;

    memset(sz, 0, sizeof *sz);
    while (*p) {
        const char *eol = p;
        while (*eol && *eol != '\n') ++eol;
        {
            const char *q = skip_blanks(p);
            if (q < eol) {
                if (*q == '[') {
                    const char *r = q + 1;
                    /* separate counters: inputs and outputs are numbered independently */
                    if (name_eq_n(r, "System", 6)) sec = SEC_SYSTEM;
                    else if (name_eq_n(r, "Input", 5)) { sec = SEC_INPUT; ++idx_in; }
                    else if (name_eq_n(r, "Output", 6)) { sec = SEC_OUTPUT; ++idx_out; }
                    else if (name_eq_n(r, "Rules", 5)) sec = SEC_RULES;
                    else sec = SEC_NONE;
                    if (sec == SEC_INPUT && (uint16_t)idx_in > sz->n_in) sz->n_in = (uint16_t)idx_in;
                    if (sec == SEC_OUTPUT && (uint16_t)idx_out > sz->n_out) sz->n_out = (uint16_t)idx_out;
                } else if (sec == SEC_INPUT || sec == SEC_OUTPUT) {
                    if (q[0] == 'M' && q[1] == 'F') ++sz->n_mf;
                } else if (sec == SEC_RULES) {
                    ++sz->n_rule;
                }
            }
        }
        p = (*eol) ? eol + 1 : eol;
    }
    sz->n_idx = (uint16_t)(sz->n_rule * (uint16_t)(sz->n_in + sz->n_out));
    sz->n_ante = (uint16_t)(sz->n_rule * sz->n_in);
    sz->n_conse = (uint16_t)(sz->n_rule * sz->n_out);
}


void fz_fis_measure(const char *text, fz_pool_size_t *out) {
    if (!text || !out) return;
    measure_text(text, out);
}

/* ------------------------------------------------------------ parse pass */

typedef struct {
    fz_mf_t *mf;
    uint16_t n_mf, i_mf;
    fz_var_t *vin, *vout;
    uint16_t n_in, i_in, n_out, i_out;
    fz_rule_t *rules;
    uint16_t n_rule, i_rule;
    int16_t *idx;
    uint16_t n_idx, i_idx;
} ctx_t;

static int parse_variable(ctx_t *c, const char **pp, int is_out) {
    const char *p = *pp;
    fz_var_t *v;
    uint16_t mfs_start;
    int64_t declared_mfs = -1;

    if (is_out) {
        if (c->i_out >= c->n_out) return FZ_ERR_POOL;
        v = &c->vout[c->i_out];
    } else {
        if (c->i_in >= c->n_in) return FZ_ERR_POOL;
        v = &c->vin[c->i_in];
    }
    memset(v, 0, sizeof *v);
    v->mfs = &c->mf[c->i_mf];
    mfs_start = c->i_mf;

    for (;;) {
        const char *eol = p;
        const char *q;
        char key[16];
        const char *val = NULL;
        unsigned kl = 0;
        while (*eol && *eol != '\n') ++eol;
        q = skip_blanks(p);
        if (q < eol) {
            if (*q == '[') { *pp = p; break; }
            /* Key/value split that ignores whitespace around '=' (§F). */
            while (q < eol && *q != '=' && !is_space(*q) && kl + 1 < sizeof key)
                key[kl++] = *q++;
            key[kl] = 0;
            while (q < eol && is_space(*q)) ++q;
            if (q < eol && *q == '=') { ++q; while (q < eol && is_space(*q)) ++q; val = q; }

            if (val && name_eq(key, "Name")) {
                copy_token(val, v->name, FZ_NAME_LEN);
            } else if (val && name_eq(key, "Range")) {
                const char *r = val;
                float lo, hi;
                if (*r == '[') ++r;
                if (!read_float(&r, &lo)) return FZ_ERR_KEY;
                if (!read_float(&r, &hi)) return FZ_ERR_KEY;
                v->lo = lo;
                v->hi = hi;
            } else if (val && name_eq(key, "NumMFs")) {
                const char *r = val;
                long n;
                if (!read_int(&r, &n) || n < 0) return FZ_ERR_KEY;
                declared_mfs = n;
            } else if (key[0] == 'M' && key[1] == 'F' && key[2] >= '0' && key[2] <= '9') {
                const char *r = val ? val : q;
                const char *k = key + 2;
                char type_name[24];
                fz_mf_t *mf;
                int np;
                long num;
                if (!read_int(&k, &num) || num <= 0) return FZ_ERR_RULE;
                if (!val) return FZ_ERR_KEY;
                if (*r == '\'') {
                    const char *s = r + 1;
                    unsigned k = 0;
                    while (*s && *s != '\'') ++s;      /* skip the MF name */
                    if (*s != '\'') return FZ_ERR_KEY;
                    ++s;
                    while (*s && (*s == ' ' || *s == ':' || *s == '\'')) ++s;
                    while (*s && *s != '\'' && *s != ',' && *s != '[' && !is_space(*s) && k + 1 < sizeof type_name)
                        type_name[k++] = *s++;
                    type_name[k] = 0;
                    r = s;
                } else {
                    unsigned k = 0;
                    while (*r && *r != ',' && *r != '[' && !is_space(*r) && k + 1 < sizeof type_name)
                        type_name[k++] = *r++;
                    type_name[k] = 0;
                }
                while (*r && (*r == ' ' || *r == ':' || *r == '\'' || *r == ',')) ++r;

                if (c->i_mf >= c->n_mf) return FZ_ERR_POOL;
                mf = &c->mf[c->i_mf];
                memset(mf, 0, sizeof *mf);
                {
                    int ty = fz_mf_type_from_name(type_name);
                    if (ty < 0) return FZ_ERR_TYPE;
                    mf->type = (uint8_t)ty;
                }
                if (*r == '[') {
                    np = read_vector(&r, mf->p, FZ_MAX_MF_PARAMS);
                    if (np < 0) return FZ_ERR_KEY;
                    if (np > FZ_MAX_MF_PARAMS) return FZ_ERR_POOL;
                    mf->nparam = (uint8_t)np;
                }
                ++c->i_mf;
            }
        }
        p = (*eol) ? eol + 1 : eol;
        if (!*p) break;
    }

    v->nmf = (uint8_t)(c->i_mf - mfs_start);
    if (declared_mfs >= 0 && (uint32_t)declared_mfs != (uint32_t)v->nmf) return FZ_ERR_COUNT;
    if (is_out) ++c->i_out; else ++c->i_in;
    *pp = p;
    return FZ_OK;
}

static int parse_rules(ctx_t *c, const char **pp) {
    const char *p = *pp;
    while (*p) {
        const char *eol = p;
        const char *q;
        while (*eol && *eol != '\n') ++eol;
        q = skip_blanks(p);
        if (q < eol) {
            fz_rule_t *r;
            int16_t *ante, *conse;
            uint16_t need = (uint16_t)(c->n_in + c->n_out);
            uint8_t k;

            if (c->i_rule >= c->n_rule) return FZ_ERR_POOL;
            if ((uint32_t)c->i_idx + need > c->n_idx) return FZ_ERR_POOL;
            r = &c->rules[c->i_rule];
            ante = &c->idx[c->i_idx];
            conse = &c->idx[c->i_idx + c->n_in];
            c->i_idx = (uint16_t)(c->i_idx + need);

            for (k = 0; k < c->n_in; ++k) {
                int16_t s;
                if (!read_slot(&q, &s)) return FZ_ERR_RULE;
                ante[k] = s;
            }
            q = skip_blanks(q);
            if (*q != ',') return FZ_ERR_RULE;
            ++q;
            for (k = 0; k < c->n_out; ++k) {
                int16_t s;
                if (!read_slot(&q, &s)) return FZ_ERR_RULE;
                conse[k] = s;
            }
            r->n = (uint8_t)c->n_in;
            r->weight = 1.0f;
            r->conn = 1;
            r->ante = ante;
            r->conse = conse;

            q = skip_blanks(q);
            if (*q == '(') {
                float w;
                ++q;
                if (read_float(&q, &w)) r->weight = w;
                while (*q && *q != ')') ++q;
                if (*q == ')') ++q;
            }
            q = skip_blanks(q);
            if (*q == ':') {
                long cn;
                ++q;
                if (read_int(&q, &cn)) r->conn = (uint8_t)((cn == 2) ? 2 : 1);
            }
            ++c->i_rule;
        }
        p = (*eol) ? eol + 1 : eol;
    }
    *pp = p;
    return FZ_OK;
}

int fz_fis_parse_text(const char *text, size_t text_len, fz_fis_t *out,
                      fz_mf_t *mf_pool, uint16_t n_mf,
                      fz_var_t *in_pool, uint16_t n_in_pool,
                      fz_var_t *out_pool, uint16_t n_out_pool,
                      fz_rule_t *rule_pool, uint16_t n_rule_pool,
                      int16_t *idx_pool, uint16_t n_idx) {
    ctx_t c;
    const char *p;
    fz_pool_size_t need;
    int64_t decl_in = -1, decl_out = -1, decl_rules = -1;
    uint8_t and_op = FZ_OP_MIN, or_op = FZ_OP_MAX, imp_op = FZ_OP_MIN,
            agg_op = FZ_OP_MAX, def_op = FZ_DEFUZZ_CENTROID;
    fz_type_t type = FZ_MAMDANI;
    int sec = SEC_NONE;

    if (!text || !out || !mf_pool || !in_pool || !out_pool || !rule_pool || !idx_pool)
        return FZ_ERR_ARG;

    /* The scan below is NUL-driven, so the buffer must be terminated WITHIN the
     * window the caller declared. Checking once here is what keeps an
     * unterminated buffer from reading past its end. */
    if (text_len != 0 && memchr(text, 0, text_len) == 0) return FZ_ERR_ARG;

    fz_fis_measure(text, &need);
    if (need.n_mf > n_mf || need.n_in > n_in_pool || need.n_out > n_out_pool ||
        need.n_rule > n_rule_pool || need.n_idx > n_idx)
        return FZ_ERR_POOL;

    memset(&c, 0, sizeof c);
    memset(out, 0, sizeof *out);

    for (p = text; *p;) {
        const char *eol = p;
        const char *q;
        while (*eol && *eol != '\n') ++eol;
        q = skip_blanks(p);
        if (q < eol) {
            if (*q == '[') {
                sec = name_eq_n(q + 1, "System", 6) ? SEC_SYSTEM : SEC_NONE;
            } else if (sec == SEC_SYSTEM) {
                char key[24], val[32];
                const char *r = q;
                unsigned k = 0;
                while (*r && *r != '=' && !is_space(*r) && k + 1 < sizeof key) key[k++] = *r++;
                key[k] = 0;
                while (*r && is_space(*r)) ++r;
                if (*r == '=') {
                    ++r;
                    copy_token(r, val, sizeof val);
                    if (name_eq(key, "Type")) {
                        if (name_eq(val, "mamdani")) type = FZ_MAMDANI;
                        else if (name_eq(val, "sugeno")) type = FZ_SUGENO;
                        else return FZ_ERR_TYPE;
                    } else if (name_eq(key, "Name")) {
                        copy_token(r, out->name, FZ_NAME_LEN);
                    } else if (name_eq(key, "NumInputs")) {
                        long n; const char *s2 = r; if (read_int(&s2, &n)) decl_in = n;
                    } else if (name_eq(key, "NumOutputs")) {
                        long n; const char *s2 = r; if (read_int(&s2, &n)) decl_out = n;
                    } else if (name_eq(key, "NumRules")) {
                        long n; const char *s2 = r; if (read_int(&s2, &n)) decl_rules = n;
                    } else if (name_eq(key, "AndMethod")) {
                        int o = fz_op_from_name(val); if (o < 0) return FZ_ERR_TYPE; and_op = (uint8_t)o;
                    } else if (name_eq(key, "OrMethod")) {
                        int o = fz_op_from_name(val); if (o < 0) return FZ_ERR_TYPE; or_op = (uint8_t)o;
                    } else if (name_eq(key, "ImpMethod")) {
                        int o = fz_op_from_name(val); if (o < 0) return FZ_ERR_TYPE; imp_op = (uint8_t)o;
                    } else if (name_eq(key, "AggMethod")) {
                        int o = fz_op_from_name(val); if (o < 0) return FZ_ERR_TYPE; agg_op = (uint8_t)o;
                    } else if (name_eq(key, "DefuzzMethod")) {
                        int o = fz_defuzz_from_name(val); if (o < 0) return FZ_ERR_TYPE; def_op = (uint8_t)o;
                    }
                    /* Version and unknown keys are ignored on purpose. */
                }
            }
        }
        p = (*eol) ? eol + 1 : eol;
    }
    if (decl_in < 0 || decl_out < 0) return FZ_ERR_SECTION;
    if ((uint32_t)decl_in > n_in_pool || (uint32_t)decl_out > n_out_pool) return FZ_ERR_POOL;

    c.mf = mf_pool;   c.n_mf = n_mf;
    c.vin = in_pool;  c.n_in = (uint16_t)decl_in;
    c.vout = out_pool; c.n_out = (uint16_t)decl_out;
    c.rules = rule_pool; c.n_rule = n_rule_pool;
    c.idx = idx_pool; c.n_idx = n_idx;

    {
        const char *s = text;
        int current = SEC_NONE;
        while (*s) {
            const char *eol = s;
            const char *q;
            while (*eol && *eol != '\n') ++eol;
            q = skip_blanks(s);
            if (q < eol && *q == '[') {
                if (name_eq_n(q + 1, "Input", 5)) current = SEC_INPUT;
                else if (name_eq_n(q + 1, "Output", 6)) current = SEC_OUTPUT;
                else if (name_eq_n(q + 1, "Rules", 5)) { current = SEC_RULES; s = (*eol) ? eol + 1 : eol; break; }
                else current = SEC_NONE;
                s = (*eol) ? eol + 1 : eol;
                if (current == SEC_INPUT || current == SEC_OUTPUT) {
                    int rc = parse_variable(&c, &s, current == SEC_OUTPUT);
                    if (rc != FZ_OK) return rc;
                }
                continue;
            }
            s = (*eol) ? eol + 1 : eol;
        }
        {
            int rc = parse_rules(&c, &s);
            if (rc != FZ_OK) return rc;
        }
    }

    if (c.i_in != c.n_in || c.i_out != c.n_out) return FZ_ERR_COUNT;
    if (decl_rules >= 0 && (uint32_t)decl_rules != (uint32_t)c.i_rule) return FZ_ERR_COUNT;

    out->type = type;
    out->n_in = c.n_in;
    out->n_out = c.n_out;
    out->n_rules = c.i_rule;
    out->in = in_pool;
    out->out = out_pool;
    out->rules = rule_pool;
    out->and_op = and_op;
    out->or_op = or_op;
    out->imp_op = imp_op;
    out->agg_op = agg_op;
    out->defuzz_op = def_op;
    out->n_points = 0;
    return FZ_OK;
}

#ifndef ARDUINO
int fz_fis_parse_file(const char *path, char *buf, unsigned buf_len,
                      fz_fis_t *out,
                      fz_mf_t *mf_pool, uint16_t n_mf,
                      fz_var_t *in_pool, uint16_t n_in_pool,
                      fz_var_t *out_pool, uint16_t n_out_pool,
                      fz_rule_t *rule_pool, uint16_t n_rule_pool,
                      int16_t *idx_pool, uint16_t n_idx) {
    FILE *f;
    size_t n;
    if (!path || !buf || buf_len == 0) return FZ_ERR_ARG;
    f = fopen(path, "rb");
    if (!f) return FZ_ERR_ARG;
    n = fread(buf, 1, (size_t)(buf_len - 1), f);
    fclose(f);
    buf[n] = 0;
    return fz_fis_parse_text(buf, n + 1, out, mf_pool, n_mf, in_pool, n_in_pool,
                             out_pool, n_out_pool, rule_pool, n_rule_pool,
                             idx_pool, n_idx);
}
#endif
