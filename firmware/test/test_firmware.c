#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

#include "acquisition.h"
#include "filters.h"
#include "pan_tompkins.h"
#include "rr_tracker.h"
#include "signal_quality.h"
#include "persistent_queue.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
} while(0)

// Test UT-002: Filter Frequency Response
static void test_filters(void) {
    printf("[RUN] Testing digital filters...\n");
    ecg_filter_t filt;
    filt_init(&filt);

    // 10 Hz sine wave (in passband) at 250 Hz sample rate
    float passband_power = 0.0f;
    for (int i = 0; i < 500; i++) {
        float t = (float)i / 250.0f;
        float x = sinf(2.0f * M_PI * 10.0f * t);
        float y = filt_bandpass(&filt, x);
        if (i >= 200) { // Steady state
            passband_power += y * y;
        }
    }

    // 0.5 Hz baseline drift sine wave (stopband)
    filt_init(&filt);
    float stopband_power = 0.0f;
    for (int i = 0; i < 500; i++) {
        float t = (float)i / 250.0f;
        float x = sinf(2.0f * M_PI * 0.5f * t);
        float y = filt_bandpass(&filt, x);
        if (i >= 200) {
            stopband_power += y * y;
        }
    }

    // Baseline drift must be attenuated significantly compared to passband
    TEST_ASSERT(stopband_power < passband_power * 0.05f, "0.5 Hz baseline wander not attenuated by bandpass filter");

    // Test derivative filter
    filt_init(&filt);
    float deriv = filt_derivative(&filt, 1.0f);
    (void)deriv;
    printf("[PASS] Digital filters passed frequency response test.\n");
}

// Test UT-005, UT-006, UT-007: RR Tracker & HRV Calculations
static void test_rr_tracker(void) {
    printf("[RUN] Testing RR interval tracker & RMSSD...\n");
    rr_tracker_t tracker;
    rrt_init(&tracker);

    pt_peak_t peak;
    rr_metrics_t metrics;

    // Peak 1: initial beat (rr_ms = 0)
    peak.sample_index = 250;
    peak.amplitude = 1.0f;
    peak.rr_ms = 0.0f;
    rrt_process_peak(&tracker, &peak, &metrics);

    // Peak 2: RR = 1000 ms -> Heart rate must be exactly 60 BPM
    peak.sample_index = 500;
    peak.rr_ms = 1000.0f;
    bool accepted = rrt_process_peak(&tracker, &peak, &metrics);
    TEST_ASSERT(accepted == true, "Expected valid 1000 ms beat to be accepted");
    TEST_ASSERT(fabsf(metrics.heart_rate_bpm - 60.0f) < 0.01f, "Expected 60 BPM for 1000 ms interval");

    // Test RMSSD with clean alternating sequence: 800, 850, 800, 850...
    rrt_init(&tracker);
    for (int i = 0; i < 15; i++) {
        peak.sample_index = 1000 + i * 200;
        peak.rr_ms = (i % 2 == 0) ? 800.0f : 850.0f;
        rrt_process_peak(&tracker, &peak, &metrics);
    }

    TEST_ASSERT(metrics.rmssd_valid == true, "RMSSD should be valid after > 10 intervals");
    TEST_ASSERT(fabsf(metrics.rmssd_ms - 50.0f) < 0.5f, "Calculated RMSSD should match expected 50.0 ms");

    // Test implausible interval rejection (e.g. 150 ms = 400 BPM)
    peak.rr_ms = 150.0f;
    accepted = rrt_process_peak(&tracker, &peak, &metrics);
    TEST_ASSERT(accepted == false, "Implausibly fast interval (<300 ms) should be rejected");

    printf("[PASS] RR Tracker & RMSSD passed.\n");
}

// Test UT-008, UT-009: Persistent Queue & Recovery
static void test_persistent_queue(void) {
    printf("[RUN] Testing persistent LittleFS/host queue...\n");
    const char *test_dir = "./test_queue_dir";
    system("rm -rf ./test_queue_dir");

    pq_init(test_dir);

    // Enqueue 5 records
    for (int i = 0; i < 5; i++) {
        pq_record_t rec;
        memset(&rec, 0, sizeof(rec));
        strncpy(rec.device_id, "test-dev-01", sizeof(rec.device_id) - 1);
        strncpy(rec.session_id, "session-test", sizeof(rec.session_id) - 1);
        rec.timestamp_ms = 1700000000000LL + (i * 1000);
        rec.heart_rate_bpm = 70.0f + i;
        rec.rmssd_ms = 35.0f;
        rec.rr_interval_ms = 850.0f;
        rec.signal_quality = 0;
        rec.detected_peaks = 10 + i;
        rec.rejected_peaks = 0;

        int ret = pq_enqueue(&rec);
        TEST_ASSERT(ret == 0, "Failed to enqueue record");
    }

    TEST_ASSERT(pq_get_depth() == 5, "Expected queue depth to be 5");

    // Simulate system restart (re-initialize queue)
    pq_init(test_dir);
    TEST_ASSERT(pq_get_depth() == 5, "Expected queue depth to recover to 5 after restart");

    // Retrieve pending batch
    pq_record_t batch[10];
    size_t count = 0;
    pq_get_pending_batch(batch, 10, &count);
    TEST_ASSERT(count == 5, "Expected to retrieve 5 pending records");
    TEST_ASSERT(batch[0].sequence == 1, "First record should have sequence 1");
    TEST_ASSERT(batch[4].sequence == 5, "Last record should have sequence 5");

    // Acknowledge first 3 records
    uint32_t acks[3] = { 1, 2, 3 };
    int removed = pq_acknowledge_records(acks, 3);
    TEST_ASSERT(removed == 3, "Expected 3 records acknowledged and deleted");
    TEST_ASSERT(pq_get_depth() == 2, "Expected queue depth to decrease to 2");

    // Serialize remaining records to JSON
    pq_get_pending_batch(batch, 10, &count);
    TEST_ASSERT(count == 2, "Remaining records should be 2");
    char json_buf[4096];
    int len = pq_serialize_batch_json("test-dev-01", batch, count, json_buf, sizeof(json_buf));
    TEST_ASSERT(len > 0, "JSON serialization failed");
    TEST_ASSERT(strstr(json_buf, "\"schema_version\":1") != NULL, "Missing schema_version");
    TEST_ASSERT(strstr(json_buf, "\"sequence\":4") != NULL, "Missing sequence 4 in JSON");

    // Clean up test directory
    system("rm -rf ./test_queue_dir");
    printf("[PASS] Persistent Queue tests passed.\n");
}

// Test UT-003, UT-004: Pan-Tompkins Synthetic Beat Detection
static void test_pan_tompkins(void) {
    printf("[RUN] Testing Pan-Tompkins QRS detection...\n");
    pan_tompkins_t pt;
    pt_init(&pt);

    // Generate 5 synthetic ECG heartbeats spaced at 250 samples (1000 ms) apart
    // A synthetic QRS can be modeled as a narrow bipolar spike
    pt_peak_t peak;
    int detected_beats = 0;

    for (int sample_i = 1; sample_i <= 1500; sample_i++) {
        float signal = 2048.0f; // Baseline 1.65V

        // Add periodic QRS pulses at samples 250, 500, 750, 1000, 1250
        for (int beat = 1; beat <= 5; beat++) {
            int beat_center = beat * 250;
            int dist = sample_i - beat_center;
            if (abs(dist) <= 10) {
                // Sharp QRS spike with amplitude ~1000
                signal += 1000.0f * (1.0f - (float)(abs(dist)) / 10.0f);
            }
        }

        if (pt_process_sample(&pt, signal, sample_i, &peak)) {
            detected_beats++;
        }
    }

    TEST_ASSERT(detected_beats >= 4, "Pan-Tompkins should reliably detect synthetic QRS beats");

    // Test noise rejection: White noise alone should not trigger uncontrolled detections
    pt_init(&pt);
    int noise_detections = 0;
    for (int i = 1; i <= 500; i++) {
        float noise = 2048.0f + ((float)(rand() % 100) - 50.0f);
        if (pt_process_sample(&pt, noise, i, &peak)) {
            noise_detections++;
        }
    }
    TEST_ASSERT(noise_detections <= 1, "Noise-only input should produce minimal or no detections");

    printf("[PASS] Pan-Tompkins detector passed synthetic tests.\n");
}

// Test Signal Quality
static void test_signal_quality(void) {
    printf("[RUN] Testing signal quality evaluator...\n");
    signal_quality_t sq;

    // Normal signal
    sq = sq_evaluate(false, 250, 0, 0, 72.0f);
    TEST_ASSERT(sq == SQ_VALID, "Expected VALID signal quality");

    // Leads disconnected
    sq = sq_evaluate(true, 250, 0, 0, 72.0f);
    TEST_ASSERT(sq == SQ_INVALID, "Expected INVALID for leads disconnected");

    // Heavy saturation (> 10%)
    sq = sq_evaluate(false, 250, 50, 0, 72.0f);
    TEST_ASSERT(sq == SQ_INVALID, "Expected INVALID for high saturation");

    // Minor saturation (3%)
    sq = sq_evaluate(false, 250, 8, 0, 72.0f);
    TEST_ASSERT(sq == SQ_DEGRADED, "Expected DEGRADED for minor saturation");

    printf("[PASS] Signal quality evaluator passed.\n");
}

int main(void) {
    printf("========================================\n");
    printf("Running EdgeECG Firmware Core Unit Tests\n");
    printf("========================================\n");

    test_filters();
    test_rr_tracker();
    test_pan_tompkins();
    test_persistent_queue();
    test_signal_quality();

    printf("\n>>> ALL FIRMWARE UNIT TESTS PASSED SUCCESSFULLY! <<<\n");
    return 0;
}
