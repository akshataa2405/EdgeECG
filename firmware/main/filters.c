#include "filters.h"
#include <string.h>

// 4th order Butterworth bandpass (5-15 Hz at 250 Hz sample rate)
// Implemented as 2 cascaded Second-Order Sections (SOS) in Direct Form II Transposed

// Section 1:
// b = [0.01335920, 0.02671840, 0.01335920]
// a = [1.0, -1.68135386, 0.77989214]
static const float b1[3] = { 0.0133592000f, 0.0267184001f, 0.0133592000f };
static const float a1[2] = { -1.68135386f, 0.779892144f }; // a[1], a[2]

// Section 2:
// b = [1.0, -2.0, 1.0]
// a = [1.0, -1.87959360, 0.89870989]
static const float b2[3] = { 1.00000000f, -2.00000000f, 1.00000000f };
static const float a2[2] = { -1.87959360f, 0.898709888f }; // a[1], a[2]

void filt_init(ecg_filter_t *f) {
    if (f == NULL) return;
    f->w1_1 = 0.0f;
    f->w1_2 = 0.0f;
    f->w2_1 = 0.0f;
    f->w2_2 = 0.0f;
    memset(f->d_buf, 0, sizeof(f->d_buf));
    f->d_idx = 0;
}

float filt_bandpass(ecg_filter_t *f, float raw_sample) {
    if (f == NULL) return raw_sample;

    // --- Section 1 Direct Form II Transposed ---
    float y1 = b1[0] * raw_sample + f->w1_1;
    f->w1_1 = b1[1] * raw_sample - a1[0] * y1 + f->w1_2;
    f->w1_2 = b1[2] * raw_sample - a1[1] * y1;

    // --- Section 2 Direct Form II Transposed ---
    float y2 = b2[0] * y1 + f->w2_1;
    f->w2_1 = b2[1] * y1 - a2[0] * y2 + f->w2_2;
    f->w2_2 = b2[2] * y1 - a2[1] * y2;

    return y2;
}

float filt_derivative(ecg_filter_t *f, float sample) {
    if (f == NULL) return 0.0f;

    // Store in circular buffer of length 5
    f->d_buf[f->d_idx] = sample;

    // x[n], x[n-1], x[n-2], x[n-3], x[n-4]
    int idx_n   = f->d_idx;
    int idx_n1  = (f->d_idx + 4) % 5;
    int idx_n3  = (f->d_idx + 2) % 5;
    int idx_n4  = (f->d_idx + 1) % 5;

    float x_n   = f->d_buf[idx_n];
    float x_n1  = f->d_buf[idx_n1];
    float x_n3  = f->d_buf[idx_n3];
    float x_n4  = f->d_buf[idx_n4];

    // Advance buffer index
    f->d_idx = (f->d_idx + 1) % 5;

    // y[n] = (2*x[n] + x[n-1] - x[n-3] - 2*x[n-4]) / 8
    return (2.0f * x_n + x_n1 - x_n3 - 2.0f * x_n4) * 0.125f;
}
