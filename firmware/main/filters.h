#ifndef EDGEECG_FILTERS_H
#define EDGEECG_FILTERS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Biquad section 1 state
    float w1_1, w1_2;
    // Biquad section 2 state
    float w2_1, w2_2;
    // Derivative circular buffer
    float d_buf[5];
    int   d_idx;
} ecg_filter_t;

/**
 * @brief Initialize digital filter state.
 */
void filt_init(ecg_filter_t *f);

/**
 * @brief Apply 5-15 Hz bandpass filter to input sample.
 * @param f Pointer to filter state.
 * @param raw_sample Input sample (ADC amplitude or floating-point value).
 * @return Filtered output sample.
 */
float filt_bandpass(ecg_filter_t *f, float raw_sample);

/**
 * @brief Apply Pan-Tompkins 5-point derivative filter:
 *        y[n] = (2*x[n] + x[n-1] - x[n-3] - 2*x[n-4]) / 8
 * @param f Pointer to filter state.
 * @param sample Bandpass filtered sample.
 * @return Derivative value.
 */
float filt_derivative(ecg_filter_t *f, float sample);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_FILTERS_H
