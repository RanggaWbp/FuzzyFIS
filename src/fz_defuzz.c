/* fz_defuzz.c — no allocation, grid computed on the fly. */
#include "fz_defuzz.h"

static float fz_x(float lo, float hi, uint16_t k, uint16_t n) {
    if (n <= 1) return lo;
    return lo + ((float)k * (hi - lo)) / (float)(n - 1);
}

/* Trapezoid area of ys over [lo, hi], from index 0..upto (exclusive). */
static float fz_area(const float *ys, float lo, float hi, uint16_t n, uint16_t upto) {
    uint16_t k;
    float area = 0.0f;
    float dx = (n <= 1) ? 0.0f : (hi - lo) / (float)(n - 1);
    for (k = 0; k + 1 < upto; ++k) area += 0.5f * (ys[k] + ys[k + 1]) * dx;
    return area;
}

/* Area of the samples from index 0 up to and including index `upto`, exactly as
 * the reference computes it: trapz over x[0..upto] with everything beyond
 * zeroed, which contributes a trailing HALF segment of ys[upto]. Omitting that
 * half step shifts the bisector by one grid point. */
static float fz_area_upto(const float *ys, float lo, float hi, uint16_t n, uint16_t upto) {
    uint16_t k;
    float area = 0.0f;
    float dx = (n <= 1) ? 0.0f : (hi - lo) / (float)(n - 1);
    for (k = 0; k + 1 <= upto && k + 1 < n; ++k) area += 0.5f * (ys[k] + ys[k + 1]) * dx;
    if (upto < n) area += 0.5f * ys[upto] * dx;
    return area;
}

float fz_defuzz(uint8_t method, float lo, float hi, const float *ys, uint16_t n, float fallback) {
    uint16_t i;

    if (n == 0 || !ys) return fallback;

    switch (method) {
    case FZ_DEFUZZ_CENTROID:
    case FZ_DEFUZZ_WTAVER: {
        float sw = 0.0f, swx = 0.0f;
        for (i = 0; i < n; ++i) { float x = fz_x(lo, hi, i, n); sw += ys[i]; swx += ys[i] * x; }
        return (sw == 0.0f) ? fallback : (swx / sw);
    }

    case FZ_DEFUZZ_WTSUM: {
        float swx = 0.0f;
        for (i = 0; i < n; ++i) swx += ys[i] * fz_x(lo, hi, i, n);
        return swx;
    }

    case FZ_DEFUZZ_CENTROID_INTEGRAL: {
        /* Reference-toolkit extension: trapz(x, x.*y) / trapz(x, y). */
        float num = 0.0f, den = 0.0f;
        float dx = (n <= 1) ? 0.0f : (hi - lo) / (float)(n - 1);
        for (i = 0; i + 1 < n; ++i) {
            float x0 = fz_x(lo, hi, i, n), x1 = fz_x(lo, hi, i + 1, n);
            num += 0.5f * (ys[i] * x0 + ys[i + 1] * x1) * dx;
            den += 0.5f * (ys[i] + ys[i + 1]) * dx;
        }
        return (den == 0.0f) ? fallback : (num / den);
    }

    case FZ_DEFUZZ_MOM:
    case FZ_DEFUZZ_SOM:
    case FZ_DEFUZZ_LOM: {
        /* Note: these do NOT use `fallback` on an all-zero set. The reference
         * (and MATLAB's defuzz) picks the first/last/mean of the samples that
         * equal the maximum, and on an all-zero set every sample is a maximum:
         * som -> lo, lom -> hi, mom -> midpoint. Returning `fallback` here
         * makes som/lom wrong at the edge of every rule base. */
        float mx = ys[0], acc = 0.0f;
        uint16_t count = 0;
        for (i = 1; i < n; ++i) if (ys[i] > mx) mx = ys[i];
        for (i = 0; i < n; ++i) {
            if (ys[i] == mx) {
                if (method == FZ_DEFUZZ_SOM) return fz_x(lo, hi, i, n);
                acc += fz_x(lo, hi, i, n);
                ++count;
            }
        }
        if (count == 0) return fallback;
        /* LOM: last max (acc holds only the running sum -> recompute last) */
        if (method == FZ_DEFUZZ_LOM) {
            for (i = n; i-- > 0;) if (ys[i] == mx) return fz_x(lo, hi, i, n);
            return fallback;
        }
        return acc / (float)count;
    }

    case FZ_DEFUZZ_BISECTOR: {
        /* Literal port of the reference algorithm: binary search on the sample
         * grid using round((lower+upper)/2), area measured from x[0] up to and
         * including the midpoint, and `lower = midpoint` (not midpoint+1) on the
         * other branch. The termination detail matters: it decides whether the
         * answer lands one grid step early or late. */
        float total = fz_area(ys, lo, hi, n, n);
        float half;
        uint16_t lower = 0, upper = (uint16_t)(n - 1), mid = 0, guard = 0;
        if (total == 0.0f) return fallback;
        half = total * 0.5f;
        while (lower <= upper && guard++ < n) {
            mid = (uint16_t)((lower + upper + 1) / 2);
            if (fz_area_upto(ys, lo, hi, n, mid) - half > 0.0f) upper = mid;
            else lower = mid;
        }
        return fz_x(lo, hi, mid, n);
    }

    default:
        return fallback;
    }
}
