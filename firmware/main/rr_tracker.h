#ifndef EDGEECG_RR_TRACKER_H
#define EDGEECG_RR_TRACKER_H

#include <stdint.h>
#include <stdbool.h>
#include "pan_tompkins.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RR_MIN_VALID_MS          300.0f    // 200 BPM ceiling
#define RR_MAX_VALID_MS          2000.0f   // 30 BPM floor
#define RR_WINDOW_CAPACITY       120       // Holds up to 120 beats (approx 1-2 minutes)
#define RR_MIN_SAMPLES_FOR_RMSSD 10        // Minimum valid intervals needed for RMSSD

typedef struct {
    float    last_rr_ms;
    float    heart_rate_bpm;
    float    rmssd_ms;
    bool     rmssd_valid;
    bool     rr_jump_detected;
    uint32_t total_accepted_beats;
    uint32_t total_rejected_beats;
} rr_metrics_t;

typedef struct {
    float    rr_buffer[RR_WINDOW_CAPACITY];
    int      head;
    int      count;
    float    prev_rr_ms;

    uint32_t accepted_count;
    uint32_t rejected_count;
} rr_tracker_t;

/**
 * @brief Initialize RR interval and HRV tracker.
 */
void rrt_init(rr_tracker_t *tracker);

/**
 * @brief Process an accepted R-peak and update HR and RMSSD metrics.
 * @param tracker Pointer to tracker state.
 * @param peak Detected peak from Pan-Tompkins.
 * @param out_metrics Populated with latest HR, RMSSD, and flags.
 * @return true if peak interval was physiologically valid and accepted.
 */
bool rrt_process_peak(rr_tracker_t *tracker, const pt_peak_t *peak, rr_metrics_t *out_metrics);

/**
 * @brief Get current rolling metrics without registering a new peak.
 */
void rrt_get_current_metrics(const rr_tracker_t *tracker, rr_metrics_t *out_metrics);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_RR_TRACKER_H
