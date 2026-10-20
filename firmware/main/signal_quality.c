#include "signal_quality.h"
#include <math.h>

signal_quality_t sq_evaluate(
    bool     leads_off_active,
    uint32_t window_samples,
    uint32_t saturation_samples,
    uint32_t overrun_samples,
    float    hr_bpm
) {
    // 1. Leads disconnected is immediate invalid
    if (leads_off_active) {
        return SQ_INVALID;
    }

    // 2. High saturation rate (> 10% of samples) or zero/NAN heart rate
    if (window_samples > 0) {
        float sat_ratio = (float)saturation_samples / (float)window_samples;
        if (sat_ratio > 0.10f) {
            return SQ_INVALID;
        }
        if (sat_ratio > 0.02f) {
            return SQ_DEGRADED;
        }
    }

    // 3. Hardware timing overruns
    if (overrun_samples > 0) {
        return SQ_DEGRADED;
    }

    // 4. Heart rate physiological validity check
    if (isnan(hr_bpm) || hr_bpm < 30.0f || hr_bpm > 220.0f) {
        return SQ_DEGRADED;
    }

    return SQ_VALID;
}

const char* sq_to_string(signal_quality_t sq) {
    switch (sq) {
        case SQ_VALID:    return "valid";
        case SQ_DEGRADED: return "degraded";
        case SQ_INVALID:  
        default:          return "invalid";
    }
}
