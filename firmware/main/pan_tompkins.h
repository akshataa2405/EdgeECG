#ifndef EDGEECG_PAN_TOMPKINS_H
#define EDGEECG_PAN_TOMPKINS_H

#include <stdint.h>
#include <stdbool.h>
#include "filters.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PT_MWI_WINDOW_SIZE         30    // 120 ms at 250 Hz
#define PT_REFRACTORY_SAMPLES      50    // 200 ms at 250 Hz
#define PT_SEARCHBACK_RR_COUNT     8     // Number of RR intervals for running average
#define PT_HISTORY_BUFFER_SIZE     128   // Buffer for R-peak localization

typedef struct {
    uint64_t sample_index;   // Exact ADC sample index where peak occurred
    float    amplitude;      // Peak amplitude in bandpassed signal
    float    rr_ms;          // RR interval since previous peak in ms
    bool     is_searchback;  // true if detected via searchback
} pt_peak_t;

typedef struct {
    ecg_filter_t filter;

    // Moving window integrator
    float mwi_buffer[PT_MWI_WINDOW_SIZE];
    int   mwi_idx;
    float mwi_sum;

    // Circular delay buffer for R-peak localization
    float    bp_history[PT_HISTORY_BUFFER_SIZE];
    uint64_t idx_history[PT_HISTORY_BUFFER_SIZE];
    int      hist_idx;

    // Adaptive threshold estimators
    float spki;              // Signal peak level estimate
    float npki;              // Noise peak level estimate
    float threshold_i1;      // Primary detection threshold
    float threshold_i2;      // Searchback detection threshold

    // Timing and beat tracking
    uint64_t current_sample_idx;
    uint64_t last_qrs_sample_idx;
    uint32_t refractory_countdown;

    // Running RR average
    uint32_t rr_history[PT_SEARCHBACK_RR_COUNT];
    int      rr_hist_count;
    int      rr_hist_idx;
    uint32_t rr_average_samples;

    // Local peak candidate tracking in MWI
    float    mwi_peak_candidate;
    uint64_t mwi_peak_sample_idx;
    float    mwi_prev_val;
    bool     mwi_rising;

    // Diagnostics
    uint32_t detected_peaks_count;
    uint32_t searchback_peaks_count;
    uint32_t rejected_noise_peaks_count;
} pan_tompkins_t;

/**
 * @brief Initialize or reset the Pan-Tompkins detector state.
 */
void pt_init(pan_tompkins_t *pt);

/**
 * @brief Process one raw ECG sample through the detector.
 * @param pt Pointer to detector state.
 * @param raw_adc Raw ADC reading.
 * @param sample_index Monotonic index of current sample.
 * @param peak_out Populated with peak details if a QRS is confirmed.
 * @return true if a QRS peak was detected and peak_out is valid, false otherwise.
 */
bool pt_process_sample(pan_tompkins_t *pt, float raw_adc, uint64_t sample_index, pt_peak_t *peak_out);

/**
 * @brief Get diagnostic statistics from detector.
 */
uint32_t pt_get_detected_count(const pan_tompkins_t *pt);
uint32_t pt_get_rejected_count(const pan_tompkins_t *pt);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_PAN_TOMPKINS_H
