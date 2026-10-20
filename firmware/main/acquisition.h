#ifndef EDGEECG_ACQUISITION_H
#define EDGEECG_ACQUISITION_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAMPLE_RATE_HZ         250
#define SAMPLE_PERIOD_US       4000     // 4 ms
#define ACQ_RING_CAPACITY      512      // ~2.048 seconds of buffered samples

// ADC 12-bit range is 0 - 4095
#define ADC_SATURATION_LOW     15
#define ADC_SATURATION_HIGH    4080

typedef struct {
    uint64_t sample_index;
    uint16_t raw_adc;
    bool     leads_off;
    bool     saturated;
} acq_sample_t;

/**
 * @brief Initialize ADC hardware, GPIO pins, and sampling timer.
 */
void acq_init(void);

/**
 * @brief Start timer-driven ADC sampling at 250 Hz.
 */
void acq_start(void);

/**
 * @brief Stop sampling timer.
 */
void acq_stop(void);

/**
 * @brief Pop the oldest sample from the ring buffer (non-blocking).
 * @param out Pointer to output sample structure.
 * @return true if a sample was retrieved, false if buffer was empty.
 */
bool acq_pop(acq_sample_t *out);

/**
 * @brief Push a sample directly into the ring buffer (used by ISR and test harness).
 */
bool acq_push_sample(uint16_t raw_adc, bool leads_off);

/**
 * @brief Check if raw ADC reading is in saturation rail.
 */
bool acq_is_saturated(uint16_t raw);

/**
 * @brief Retrieve diagnostic counters.
 */
uint64_t acq_total_samples(void);
uint32_t acq_overrun_count(void);
uint32_t acq_saturation_count(void);
uint32_t acq_leads_off_count(void);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_ACQUISITION_H
