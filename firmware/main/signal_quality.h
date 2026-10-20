#ifndef EDGEECG_SIGNAL_QUALITY_H
#define EDGEECG_SIGNAL_QUALITY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SQ_VALID = 0,
    SQ_DEGRADED = 1,
    SQ_INVALID = 2
} signal_quality_t;

/**
 * @brief Evaluate the signal quality based on recent window statistics.
 * @param leads_off_active Whether leads-off digital pin is asserted.
 * @param window_samples Total samples in evaluation period.
 * @param saturation_samples Saturated ADC samples in period.
 * @param overrun_samples Hardware overruns in period.
 * @param hr_bpm Current calculated heart rate (BPM).
 * @return signal_quality_t Classification.
 */
signal_quality_t sq_evaluate(
    bool     leads_off_active,
    uint32_t window_samples,
    uint32_t saturation_samples,
    uint32_t overrun_samples,
    float    hr_bpm
);

/**
 * @brief Convert signal quality enum to string literal ("valid", "degraded", "invalid").
 */
const char* sq_to_string(signal_quality_t sq);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_SIGNAL_QUALITY_H
