#include "rr_tracker.h"
#include <string.h>
#include <math.h>

void rrt_init(rr_tracker_t *tracker) {
    if (tracker == NULL) return;
    memset(tracker, 0, sizeof(rr_tracker_t));
}

static float calculate_rmssd(const rr_tracker_t *tracker) {
    if (tracker->count < RR_MIN_SAMPLES_FOR_RMSSD) {
        return NAN;
    }

    float sum_squared_diffs = 0.0f;
    int diff_count = 0;

    // Traverse circular buffer in chronological order
    int start_idx = (tracker->head - tracker->count + RR_WINDOW_CAPACITY) % RR_WINDOW_CAPACITY;

    float prev = tracker->rr_buffer[start_idx];
    for (int i = 1; i < tracker->count; i++) {
        int idx = (start_idx + i) % RR_WINDOW_CAPACITY;
        float current = tracker->rr_buffer[idx];
        float diff = current - prev;
        sum_squared_diffs += (diff * diff);
        diff_count++;
        prev = current;
    }

    if (diff_count == 0) return NAN;
    return sqrtf(sum_squared_diffs / (float)diff_count);
}

bool rrt_process_peak(rr_tracker_t *tracker, const pt_peak_t *peak, rr_metrics_t *out_metrics) {
    if (tracker == NULL || peak == NULL) return false;

    float rr_ms = peak->rr_ms;

    // First peak has rr_ms == 0 (no predecessor)
    if (rr_ms <= 0.0f) {
        if (out_metrics != NULL) {
            rrt_get_current_metrics(tracker, out_metrics);
        }
        return false;
    }

    // Plausibility range check (300 ms - 2000 ms, or 30 - 200 BPM)
    if (rr_ms < RR_MIN_VALID_MS || rr_ms > RR_MAX_VALID_MS) {
        tracker->rejected_count++;
        if (out_metrics != NULL) {
            rrt_get_current_metrics(tracker, out_metrics);
            out_metrics->last_rr_ms = rr_ms;
        }
        return false;
    }

    // Check for large sudden RR jump (> 20% difference from predecessor)
    bool jump = false;
    if (tracker->prev_rr_ms > 0.0f) {
        float pct_diff = fabsf(rr_ms - tracker->prev_rr_ms) / tracker->prev_rr_ms;
        if (pct_diff > 0.20f) {
            jump = true;
        }
    }

    // Append to rolling buffer
    tracker->rr_buffer[tracker->head] = rr_ms;
    tracker->head = (tracker->head + 1) % RR_WINDOW_CAPACITY;
    if (tracker->count < RR_WINDOW_CAPACITY) {
        tracker->count++;
    }

    tracker->prev_rr_ms = rr_ms;
    tracker->accepted_count++;

    if (out_metrics != NULL) {
        out_metrics->last_rr_ms = rr_ms;
        out_metrics->heart_rate_bpm = 60000.0f / rr_ms;
        out_metrics->rmssd_ms = calculate_rmssd(tracker);
        out_metrics->rmssd_valid = !isnan(out_metrics->rmssd_ms);
        out_metrics->rr_jump_detected = jump;
        out_metrics->total_accepted_beats = tracker->accepted_count;
        out_metrics->total_rejected_beats = tracker->rejected_count;
    }

    return true;
}

void rrt_get_current_metrics(const rr_tracker_t *tracker, rr_metrics_t *out_metrics) {
    if (out_metrics == NULL) return;
    memset(out_metrics, 0, sizeof(rr_metrics_t));

    if (tracker == NULL) return;

    out_metrics->last_rr_ms = tracker->prev_rr_ms;
    if (tracker->prev_rr_ms > 0.0f) {
        out_metrics->heart_rate_bpm = 60000.0f / tracker->prev_rr_ms;
    }
    out_metrics->rmssd_ms = calculate_rmssd(tracker);
    out_metrics->rmssd_valid = !isnan(out_metrics->rmssd_ms);
    out_metrics->total_accepted_beats = tracker->accepted_count;
    out_metrics->total_rejected_beats = tracker->rejected_count;
}
