/* classifier.c — Stage I placeholder defect classifier (see classifier.h). */
#include "classifier.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- minimal P6 PPM reader --- */

static int read_byte(FILE *f) {
    int c = fgetc(f);
    return c;
}

/* Skip whitespace and '#' comments; returns first non-space byte or -1. */
static int next_token_byte(FILE *f) {
    int c;
    for (;;) {
        c = read_byte(f);
        if (c < 0)
            return -1;
        if (c == '#') {
            do {
                c = read_byte(f);
            } while (c >= 0 && c != '\n');
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            continue;
        return c;
    }
}

static int read_int(FILE *f, int *out) {
    int c = next_token_byte(f);
    long v = 0;
    int nd = 0;
    if (c < 0 || c < '0' || c > '9')
        return -1;
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        nd++;
        c = read_byte(f);
    }
    if (c < 0)
        return -1;
    ungetc(c, f);
    if (nd == 0 || v > 100000)
        return -1;
    *out = (int)v;
    return 0;
}

int pl_read_ppm(const char *path, unsigned char **rgb, int *w, int *h) {
    FILE *f = NULL;
    int c1, c2, wi, hi, maxv;
    unsigned char *px = NULL;
    size_t npix;

    *rgb = NULL;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    c1 = read_byte(f);
    c2 = read_byte(f);
    if (c1 != 'P' || c2 != '6') {
        fclose(f);
        return -1;
    }
    if (read_int(f, &wi) != 0 || read_int(f, &hi) != 0 || read_int(f, &maxv) != 0) {
        fclose(f);
        return -1;
    }
    if (wi <= 0 || hi <= 0 || maxv != 255 || (size_t)wi * (size_t)hi > (size_t)1 << 30) {
        fclose(f);
        return -1;
    }
    /* Exactly one whitespace byte follows maxval before raster data. */
    if (read_byte(f) < 0) {
        fclose(f);
        return -1;
    }
    npix = (size_t)wi * (size_t)hi;
    px = malloc(npix * 3);
    if (!px) {
        fclose(f);
        return -1;
    }
    if (fread(px, 1, npix * 3, f) != npix * 3) {
        free(px);
        fclose(f);
        return -1;
    }
    fclose(f);
    *rgb = px;
    *w = wi;
    *h = hi;
    return 0;
}

/* --- heuristic --- */

int pl_classify(const unsigned char *rgb, int w, int h, float *confidence_out) {
    size_t n = (size_t)w * (size_t)h;
    double sum = 0.0, sumsq = 0.0;
    double mean, var, sigma, thresh;
    size_t anomalous = 0;
    double frac;

    for (size_t i = 0; i < n; i++) {
        /* Rec. 601 luma; synthetic images are grayscale anyway. */
        double v = 0.299 * rgb[3 * i] + 0.587 * rgb[3 * i + 1] +
                   0.114 * rgb[3 * i + 2];
        sum += v;
        sumsq += v * v;
    }
    mean = sum / (double)n;
    var = sumsq / (double)n - mean * mean;
    if (var < 0)
        var = 0;
    sigma = sqrt(var);

    /* A pixel is anomalous if it is far outside the surface's own noise.
     * The 20-grey-level floor keeps near-flat images from tripping on
     * quantization; 6*sigma adapts to noisier surfaces. */
    thresh = 20.0;
    if (6.0 * sigma > thresh)
        thresh = 6.0 * sigma;

    for (size_t i = 0; i < n; i++) {
        double v = 0.299 * rgb[3 * i] + 0.587 * rgb[3 * i + 1] +
                   0.114 * rgb[3 * i + 2];
        double d = v - mean;
        if (d < 0)
            d = -d;
        if (d > thresh)
            anomalous++;
    }
    frac = (double)anomalous / (double)n;

    if (frac > 0.0005) {
        /* FAIL: confidence grows with the anomalous fraction, capped. */
        double c = 0.5 + (frac / 0.05) * 0.49;
        if (c > 0.99)
            c = 0.99;
        *confidence_out = (float)c;
        return 1;
    }
    /* PASS: confidence falls as we approach the threshold. */
    {
        double c = 1.0 - (frac / 0.0005) * 0.5;
        if (c < 0.5)
            c = 0.5;
        *confidence_out = (float)c;
        return 0;
    }
}
