#include "pan_tompkins.h"
#include "acquisition.h"
#include <string.h>
#include <math.h>

void pt_init(pan_tompkins_t *pt) {
    if (pt == NULL) return;
    memset(pt, 0, sizeof(pan_tompkins_t));

    filt_init(&pt->filter);

    // Initial threshold estimates
    pt->spki = 0.5f;
    pt->npki = 0.125f;
    pt->threshold_i1 = pt->npki + 0.25f * (pt->spki - pt->npki);
    pt->threshold_i2 = 0.5f * pt->threshold_i1;

    // Default 1-second RR expectation (250 samples at 250 Hz)
    pt->rr_average_samples = 250;
    for (int i = 0; i < PT_SEARCHBACK_RR_COUNT; i++) {
        pt->rr_history[i] = 250;
    }
    pt->rr_hist_count = 0;
    pt->rr_hist_idx = 0;
}

static void update_thresholds(pan_tompkins_t *pt) {
    pt->threshold_i1 = pt->npki + 0.25f * (pt->spki - pt->npki);
    pt->threshold_i2 = 0.5f * pt->threshold_i1;
}

static void record_valid_rr(pan_tompkins_t *pt, uint32_t rr_samples) {
    pt->rr_history[pt->rr_hist_idx] = rr_samples;
    pt->rr_hist_idx = (pt->rr_hist_idx + 1) % PT_SEARCHBACK_RR_COUNT;
    if (pt->rr_hist_count < PT_SEARCHBACK_RR_COUNT) {
        pt->rr_hist_count++;
    }

    // Compute mean of valid RR intervals
    uint32_t sum = 0;
    for (int i = 0; i < pt->rr_hist_count; i++) {
        sum += pt->rr_history[i];
    }
    pt->rr_average_samples = sum / pt->rr_hist_count;
}

// Localize peak in bandpassed signal within the backward search window
static uint64_t find_r_peak_in_history(pan_tompkins_t *pt, float *out_amplitude) {
    // Look back ~40 samples from current index to find max peak in bandpass signal
    int lookback = 45;
    if (lookback > PT_HISTORY_BUFFER_SIZE) {
        lookback = PT_HISTORY_BUFFER_SIZE;
    }

    float max_val = -1e9f;
    uint64_t max_sample_idx = pt->current_sample_idx;

    for (int i = 0; i < lookback; i++) {
        int idx = (pt->hist_idx - 1 - i + PT_HISTORY_BUFFER_SIZE) % PT_HISTORY_BUFFER_SIZE;
        float val = pt->bp_history[idx];
        if (fabsf(val) > max_val) {
            max_val = fabsf(val);
            max_sample_idx = pt->idx_history[idx];
        }
    }

    if (out_amplitude != NULL) {
        *out_amplitude = max_val;
    }
    return max_sample_idx;
}

bool pt_process_sample(pan_tompkins_t *pt, float raw_adc, uint64_t sample_index, pt_peak_t *peak_out) {
    if (pt == NULL) return false;

    pt->current_sample_idx = sample_index;

    // 1. Digital Bandpass Filter (5 - 15 Hz)
    float bp_val = filt_bandpass(&pt->filter, raw_adc);

    // Save bandpassed sample in circular delay buffer for true R-peak localization
    pt->bp_history[pt->hist_idx] = bp_val;
    pt->idx_history[pt->hist_idx] = sample_index;
    pt->hist_idx = (pt->hist_idx + 1) % PT_HISTORY_BUFFER_SIZE;

    // 2. Pan-Tompkins 5-point Derivative
    float deriv_val = filt_derivative(&pt->filter, bp_val);

    // 3. Nonlinear Squaring
    float squared_val = deriv_val * deriv_val;

    // 4. Moving Window Integration (MWI) over 120 ms (30 samples)
    float oldest_mwi = pt->mwi_buffer[pt->mwi_idx];
    pt->mwi_buffer[pt->mwi_idx] = squared_val;
    pt->mwi_sum += (squared_val - oldest_mwi);
    if (pt->mwi_sum < 0.0f) pt->mwi_sum = 0.0f; // Prevent floating point drift below 0
    pt->mwi_idx = (pt->mwi_idx + 1) % PT_MWI_WINDOW_SIZE;

    float mwi_val = pt->mwi_sum / (float)PT_MWI_WINDOW_SIZE;

    // Manage refractory countdown
    if (pt->refractory_countdown > 0) {
        pt->refractory_countdown--;
    }

    bool detected = false;
    bool is_searchback = false;

    // Detect local peak in MWI output: transitions from rising to falling
    if (mwi_val > pt->mwi_prev_val) {
        pt->mwi_rising = true;
        pt->mwi_peak_candidate = mwi_val;
        pt->mwi_peak_sample_idx = sample_index;
    } else if (pt->mwi_rising && mwi_val < pt->mwi_prev_val) {
        // Local peak detected in MWI signal
        pt->mwi_rising = false;
        float peak_height = pt->mwi_peak_candidate;

        if (pt->refractory_countdown == 0) {
            // Check primary detection threshold THRESHOLD_I1
            if (peak_height >= pt->threshold_i1) {
                // Confirmed QRS Detection!
                pt->spki = 0.125f * peak_height + 0.875f * pt->spki;
                detected = true;
                is_searchback = false;
                pt->detected_peaks_count++;
            } else if (peak_height >= pt->threshold_i2) {
                // Check if searchback condition is triggered
                uint32_t samples_since_last = (uint32_t)(sample_index - pt->last_qrs_sample_idx);
                if (samples_since_last > (pt->rr_average_samples * 166) / 100) {
                    // Searchback QRS detection
                    pt->spki = 0.25f * peak_height + 0.75f * pt->spki;
                    detected = true;
                    is_searchback = true;
                    pt->searchback_peaks_count++;
                } else {
                    // Classified as Noise Peak
                    pt->npki = 0.125f * peak_height + 0.875f * pt->npki;
                    pt->rejected_noise_peaks_count++;
                }
            } else {
                // Below searchback threshold: Noise Peak
                pt->npki = 0.125f * peak_height + 0.875f * pt->npki;
                pt->rejected_noise_peaks_count++;
            }

            update_thresholds(pt);
        }
    }
    pt->mwi_prev_val = mwi_val;

    if (detected) {
        // Enforce 200 ms refractory period (50 samples at 250 Hz)
        pt->refractory_countdown = PT_REFRACTORY_SAMPLES;

        // Localize true peak in bandpassed signal
        float r_amp = 0.0f;
        uint64_t true_r_sample = find_r_peak_in_history(pt, &r_amp);

        // Compute RR interval
        float rr_ms = 0.0f;
        if (pt->last_qrs_sample_idx > 0 && true_r_sample > pt->last_qrs_sample_idx) {
            uint32_t rr_samples = (uint32_t)(true_r_sample - pt->last_qrs_sample_idx);
            rr_ms = (float)rr_samples * (1000.0f / (float)SAMPLE_RATE_HZ);
            record_valid_rr(pt, rr_samples);
        }

        pt->last_qrs_sample_idx = true_r_sample;

        if (peak_out != NULL) {
            peak_out->sample_index = true_r_sample;
            peak_out->amplitude = r_amp;
            peak_out->rr_ms = rr_ms;
            peak_out->is_searchback = is_searchback;
        }
        return true;
    }

    return false;
}

uint32_t pt_get_detected_count(const pan_tompkins_t *pt) {
    return pt ? pt->detected_peaks_count : 0;
}

uint32_t pt_get_rejected_count(const pan_tompkins_t *pt) {
    return pt ? pt->rejected_noise_peaks_count : 0;
}
