#include "sync_client.h"
#include "persistent_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#ifndef EDGEECG_HOST_BUILD
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "SYNC";
#endif

static sync_config_t s_config;
static bool s_time_synced = false;
static uint32_t s_current_backoff_ms = 1000;

int64_t sync_get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000LL) + ((int64_t)tv.tv_usec / 1000LL);
}

bool sync_is_time_synced(void) {
    return s_time_synced;
}

#ifndef EDGEECG_HOST_BUILD
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "NTP Time synchronized successfully!");
    s_time_synced = true;
}

static void initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}
#endif

void sync_init(const sync_config_t *config) {
    if (config) {
        s_config = *config;
    } else {
        strcpy(s_config.backend_url, "http://192.168.1.100:8000/api/v1/ingest");
        strcpy(s_config.device_id, "edgeecg-001");
        strcpy(s_config.device_key, "edgeecg_device_secret_sample_key");
        s_config.upload_interval_ms = 5000;
        s_config.max_backoff_ms = 300000; // 5 mins
    }
    s_current_backoff_ms = 1000;
}

// Parse JSON response to extract sequence numbers
// Response format: {"status":"accepted","accepted_sequences":[1,2],"duplicate_sequences":[3]}
static int parse_ack_sequences(const char *response_json, uint32_t *out_seqs, size_t max_seqs) {
    size_t count = 0;
    const char *p = strstr(response_json, "accepted_sequences");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            while (*p && *p != ']' && count < max_seqs) {
                while (*p == ' ' || *p == ',') p++;
                if (*p >= '0' && *p <= '9') {
                    uint32_t seq = (uint32_t)strtoul(p, (char **)&p, 10);
                    out_seqs[count++] = seq;
                } else {
                    break;
                }
            }
        }
    }

    // Also acknowledge duplicate_sequences since server already has them
    p = strstr(response_json, "duplicate_sequences");
    if (p) {
        p = strchr(p, '[');
        if (p) {
            p++;
            while (*p && *p != ']' && count < max_seqs) {
                while (*p == ' ' || *p == ',') p++;
                if (*p >= '0' && *p <= '9') {
                    uint32_t seq = (uint32_t)strtoul(p, (char **)&p, 10);
                    out_seqs[count++] = seq;
                } else {
                    break;
                }
            }
        }
    }

    return count;
}

int sync_trigger_now(void) {
    size_t pending_count = 0;
    static pq_record_t batch[PQ_MAX_BATCH_SIZE];

    if (pq_get_pending_batch(batch, PQ_MAX_BATCH_SIZE, &pending_count) != 0 || pending_count == 0) {
        return 0; // Nothing to sync
    }

    static char json_payload[16384];
    int len = pq_serialize_batch_json(s_config.device_id, batch, pending_count, json_payload, sizeof(json_payload));
    if (len <= 0) {
        return -1;
    }

#ifndef EDGEECG_HOST_BUILD
    esp_http_client_config_t config = {
        .url = s_config.backend_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -2;

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Device-Key", s_config.device_key);
    esp_http_client_set_post_field(client, json_payload, len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        if (status_code == 200 || status_code == 201) {
            char response_buffer[2048] = {0};
            esp_http_client_read_response(client, response_buffer, sizeof(response_buffer) - 1);

            uint32_t ack_seqs[PQ_MAX_BATCH_SIZE * 2];
            int ack_count = parse_ack_sequences(response_buffer, ack_seqs, sizeof(ack_seqs)/sizeof(ack_seqs[0]));

            if (ack_count > 0) {
                pq_acknowledge_records(ack_seqs, ack_count);
                ESP_LOGI(TAG, "Uploaded and acknowledged %d records.", ack_count);
            }

            // Reset exponential backoff on success
            s_current_backoff_ms = 1000;
            esp_http_client_cleanup(client);
            return 0;
        } else {
            ESP_LOGW(TAG, "Server responded with HTTP %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    // Exponential backoff with 20% jitter
    uint32_t jitter = (esp_random() % (s_current_backoff_ms / 5 + 1));
    s_current_backoff_ms = (s_current_backoff_ms * 2) + jitter;
    if (s_current_backoff_ms > s_config.max_backoff_ms) {
        s_current_backoff_ms = s_config.max_backoff_ms;
    }
#endif

    return -1;
}

#ifndef EDGEECG_HOST_BUILD
static void sync_task(void *pvParameters) {
    initialize_sntp();

    while (1) {
        sync_trigger_now();
        vTaskDelay(pdMS_TO_TICKS(s_config.upload_interval_ms));
    }
}

void sync_start_task(void) {
    xTaskCreatePinnedToCore(sync_task, "sync_task", 8192, NULL, 2, NULL, 0); // Pin to Core 0
}
#else
void sync_start_task(void) {}
#endif
