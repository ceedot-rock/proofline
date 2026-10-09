/* classifier.h — Stage I placeholder defect classifier.
 *
 * HONEST LABEL: this is a statistical anomaly heuristic over synthetic
 * images, NOT a trained defect-detection model. It exists so the
 * inspect -> journal -> verify pipeline runs end to end for Stage I.
 * Stage II replaces it with a real defect-detection model compiled for
 * the Hailo-10H on the ASUS UGen300.
 *
 * Heuristic: convert to grayscale, compute mean and standard deviation,
 * count pixels whose brightness deviates from the mean by more than
 * max(20, 6*sigma). If the anomalous fraction exceeds 0.05% of pixels,
 * the part FAILs. Uniform manufactured surfaces with sensor noise pass;
 * scratches and blotches (dark anomalous regions) fail.
 */
#ifndef PROOFLINE_CLASSIFIER_H
#define PROOFLINE_CLASSIFIER_H

#define PL_MODEL_ID "heuristic-v0.1"
#define PL_MODEL_VER "0.1.0-stage1"

/* Read a binary PPM (P6) image. Allocates *rgb (w*h*3 bytes, caller frees).
 * Returns 0 on success, -1 on error. */
int pl_read_ppm(const char *path, unsigned char **rgb, int *w, int *h);

/* Classify grayscale-converted pixels. Returns 0 = PASS, 1 = FAIL.
 * *confidence_out is set to a 0..1 score (higher = more sure). */
int pl_classify(const unsigned char *rgb, int w, int h, float *confidence_out);

#endif /* PROOFLINE_CLASSIFIER_H */
