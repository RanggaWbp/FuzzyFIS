/* fz_mf.c — CONTRACT.md §D verbatim. */
#include "fz_mf.h"
#include <math.h>

/* Guard: a degenerate divisor returns 0 instead of NaN/inf (CONTRACT §D). */
static float fz_div(float num, float den) {
    return (den == 0.0f) ? 0.0f : (num / den);
}

static float fz_sigm(float a, float c, float x) {
    float e = -a * (x - c);
    /* exp overflow guard: saturation of the logistic is 0 */
    if (e > 80.0f) return 0.0f;
    if (e < -80.0f) return 1.0f;
    return 1.0f / (1.0f + expf(e));
}

static float fz_gauss(float sig, float c, float x) {
    float d = x - c;
    if (sig == 0.0f) return (d == 0.0f) ? 1.0f : 0.0f;
    return expf(-(d * d) / (2.0f * sig * sig));
}

float fz_hedge_apply(float mu, float hedge, int not_flag) {
    if (hedge != 0.0f) mu = powf(mu, hedge);
    if (not_flag) mu = 1.0f - mu;
    return mu;
}

void fz_linspace(float lo, float hi, float *dst, uint16_t n) {
    uint16_t k;
    if (n == 0) return;
    if (n == 1) { dst[0] = lo; return; }
    for (k = 0; k < n; ++k)
        dst[k] = lo + ((float)k * (hi - lo)) / (float)(n - 1);
}

float fz_mf_eval(uint8_t type, const float *p, float x, float hedge, int not_flag) {
    float y = 0.0f;

    switch (type) {
    case FZ_MF_TRIMF: { /* [a b c] */
        float a = p[0], b = p[1], c = p[2];
        if (x == b) y = 1.0f;
        else if (x > a && x < b) y = fz_div(x - a, b - a);
        else if (x > b && x < c) y = fz_div(c - x, c - b);
        else y = 0.0f;
        break;
    }
    case FZ_MF_TRAPMF: { /* [a b c d] */
        float a = p[0], b = p[1], c = p[2], d = p[3];
        if (b <= c && x >= b && x <= c) y = 1.0f;
        else if (x > a && x < b) y = fz_div(x - a, b - a);
        else if (x > c && x < d) y = fz_div(d - x, d - c);
        else y = 0.0f;
        break;
    }
    case FZ_MF_GAUSSMF: /* [sig c] */
        y = fz_gauss(p[0], p[1], x);
        break;
    case FZ_MF_GAUSS2MF: { /* [sig1 c1 sig2 c2] */
        float sig1 = p[0], c1 = p[1], sig2 = p[2], c2 = p[3];
        if (x <= c1) y = fz_gauss(sig1, c1, x);
        else if (x > c2) y = fz_gauss(sig2, c2, x);
        else y = 1.0f;
        break;
    }
    case FZ_MF_GBELLMF: { /* [a b c] */
        float a = p[0], b = p[1], c = p[2];
        float t;
        if (a == 0.0f) { y = (x == c) ? 1.0f : 0.0f; break; }
        t = fabsf((x - c) / a);
        y = 1.0f / (1.0f + powf(t, 2.0f * b));
        break;
    }
    case FZ_MF_SIGMF: /* [a c] */
        y = fz_sigm(p[0], p[1], x);
        break;
    case FZ_MF_DSIGMF: { /* [a1 c1 a2 c2] */
        float d = fz_sigm(p[0], p[1], x) - fz_sigm(p[2], p[3], x);
        y = (d > 0.0f) ? d : 0.0f;
        break;
    }
    case FZ_MF_PSIGMF: /* [a1 c1 a2 c2] */
        y = fz_sigm(p[0], p[1], x) * fz_sigm(p[2], p[3], x);
        break;
    case FZ_MF_ZMF: { /* [a b] */
        float a = p[0], b = p[1], mid = (a + b) * 0.5f, w = b - a;
        if (x <= a) y = 1.0f;
        else if (x <= mid) { float t = fz_div(x - a, w); y = 1.0f - 2.0f * t * t; }
        else if (x < b) { float t = fz_div(x - b, w); y = 2.0f * t * t; }
        else y = 0.0f;
        break;
    }
    case FZ_MF_SMF: { /* [a b] */
        float a = p[0], b = p[1], mid = (a + b) * 0.5f, w = b - a;
        if (x <= a) y = 0.0f;
        else if (x <= mid) { float t = fz_div(x - a, w); y = 2.0f * t * t; }
        else if (x < b) { float t = fz_div(x - b, w); y = 1.0f - 2.0f * t * t; }
        else y = 1.0f;
        break;
    }
    case FZ_MF_PIMF: { /* [a b c d] */
        float a = p[0], b = p[1], c = p[2], d = p[3];
        float m1 = (a + b) * 0.5f, m2 = (c + d) * 0.5f;
        float w1 = b - a, w2 = d - c;
        if (x <= a || x >= d) y = 0.0f;
        else if (x > b && x <= c) y = 1.0f;
        else if (x <= m1) { float t = fz_div(x - a, w1); y = 2.0f * t * t; }
        else if (x <= b) { float t = fz_div(x - b, w1); y = 1.0f - 2.0f * t * t; }
        else if (x <= m2) { float t = fz_div(x - c, w2); y = 1.0f - 2.0f * t * t; }
        else { float t = fz_div(x - d, w2); y = 2.0f * t * t; }
        break;
    }
    case FZ_MF_LINSMF: { /* [a b] : 1 - linzmf */
        float a = p[0], b = p[1];
        if (a == b) y = (x >= a) ? 1.0f : 0.0f;
        else {
            float t = fz_div(b - x, b - a);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            y = 1.0f - t;
        }
        break;
    }
    case FZ_MF_LINZMF: { /* [a b] */
        float a = p[0], b = p[1];
        if (a == b) y = (x < a) ? 1.0f : 0.0f;
        else {
            float t = fz_div(b - x, b - a);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            y = t;
        }
        break;
    }
    default: /* constant / linear / unknown: not sampled here */
        return 0.0f;
    }

    return fz_hedge_apply(y, hedge, not_flag);
}
