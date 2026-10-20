#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#ifndef EDGEECG_HOST_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#endif

#include "acquisition.h"
#include "filters.h"
#include "pan_tompkins.h"
#include "rr_tracker.h"
#include "signal_quality.h"
#include "persistent_queue.h"
#include "sync_client.h"

#ifndef EDGEECG_HOST_BUILD
static const char *TAG = "MAIN";

// Global Session ID generated at boot
static char s_session_id[37] = "boot-session-001";

static void generate_boot_session_id(void) {
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    uint32_t rand_val = esp_random();
    snprintf(s_session_id, sizeof(s_session_id), "%02x%02x%02x%02x-%08" PRIx32,
             mac[2], mac[3], mac[4], mac[5], rand_val);
}

static void processing_task(void *pvParameters) {
    pan_tompkins_t detector;
    rr_tracker_t   tracker;
    pt_init(&detector);
    rrt_init(&tracker);

    acq_sample_t sample;
    pt_peak_t peak;
    rr_metrics_t current_metrics;
    rrt_get_current_metrics(&tracker, &current_metrics);

    uint32_t window_samples = 0;
    uint32_t sat_samples = 0;
    uint32_t last_log_time = 0;
    uint32_t last_enqueue_time = 0;

    ESP_LOGI(TAG, "ECG DSP Processing task started on Core %d", xPortGetCoreID());

    while (1) {
        // Drain samples from acquisition ring buffer
        while (acq_pop(&sample)) {
            window_samples++;
            if (sample.saturated) {
                sat_samples++;
            }

            // Feed raw ADC to Pan-Tompkins detector
            if (pt_process_sample(&detector, (float)sample.raw_adc, sample.sample_index, &peak)) {
                // R-peak confirmed
                rrt_process_peak(&tracker, &peak, &current_metrics);
            }
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Commit metric record periodically (every 1000 ms)
        if (now - last_enqueue_time >= 1000) {
            last_enqueue_time = now;

            signal_quality_t sq = sq_evaluate(
                sample.leads_off,
                window_samples,
                sat_samples,
                acq_overrun_count(),
                current_metrics.heart_rate_bpm
            );

            pq_record_t rec;
            memset(&rec, 0, sizeof(rec));
            rec.schema_version = PQ_SCHEMA_VERSION;
            strncpy(rec.device_id, "edgeecg-001", sizeof(rec.device_id) - 1);
            strncpy(rec.session_id, s_session_id, sizeof(rec.session_id) - 1);
            rec.timestamp_ms = sync_get_timestamp_ms();
            rec.ts_synced = sync_is_time_synced() ? 1 : 0;
            rec.heart_rate_bpm = current_metrics.heart_rate_bpm;
            rec.rmssd_ms = current_metrics.rmssd_valid ? current_metrics.rmssd_ms : NAN;
            rec.rr_interval_ms = current_metrics.last_rr_ms > 0 ? current_metrics.last_rr_ms : NAN;
            rec.signal_quality = (uint8_t)sq;
            rec.detected_peaks = pt_get_detected_count(&detector);
            rec.rejected_peaks = pt_get_rejected_count(&detector);

            pq_enqueue(&rec);

            // Reset evaluation window
            window_samples = 0;
            sat_samples = 0;
        }

        // Diagnostic log every 5 seconds
        if (now - last_log_time >= 5000) {
            last_log_time = now;
            ESP_LOGI(TAG, "[DIAG] HR: %.1f BPM | RMSSD: %.1f ms | Peaks: %" PRIu32 " | Q-Depth: %zu | Overruns: %" PRIu32,
                     current_metrics.heart_rate_bpm,
                     current_metrics.rmssd_ms,
                     pt_get_detected_count(&detector),
                     pq_get_depth(),
                     acq_overrun_count());
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Yield 10 ms to FreeRTOS
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting EdgeECG Firmware...");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    generate_boot_session_id();

    // 2. Initialize Persistent Queue
    pq_init("/littlefs/queue");

    // 3. Initialize Acquisition (ADC and 250 Hz Timer)
    acq_init();
    acq_start();

    // 4. Initialize Sync Client
    sync_init(NULL);
    sync_start_task();

    // 5. Start DSP Processing Task pinned to Core 1
    xTaskCreatePinnedToCore(processing_task, "ecg_dsp_task", 8192, NULL, 5, NULL, 1);

    ESP_LOGI(TAG, "EdgeECG initialization complete.");
}
#else
int main(void) {
    printf("EdgeECG Core Library Compiled for Host Target.\n");
    return 0;
}
#endif
