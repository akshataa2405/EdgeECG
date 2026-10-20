#include "persistent_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#ifndef EDGEECG_HOST_BUILD
#include "esp_littlefs.h"
#include "esp_log.h"
static const char *TAG = "QUEUE";
#endif

static char s_base_path[128] = "./queue_store";
static uint32_t s_next_seq = 1;
static uint32_t s_last_acked_seq = 0;
static size_t s_queue_depth = 0;

// Simple standard CRC32 table & computation
static uint32_t calculate_crc32(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return ~crc;
}

static void ensure_directory_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == -1) {
        mkdir(path, 0755);
    }
}

static void scan_queue_on_boot(void) {
    s_queue_depth = 0;
    DIR *dir = opendir(s_base_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".rec") != NULL) {
            uint32_t seq = 0;
            if (sscanf(entry->d_name, "%u.rec", &seq) == 1) {
                s_queue_depth++;
                if (seq >= s_next_seq) {
                    s_next_seq = seq + 1;
                }
            }
        }
    }
    closedir(dir);
}

int pq_init(const char *base_path) {
    if (base_path && strlen(base_path) > 0) {
        strncpy(s_base_path, base_path, sizeof(s_base_path) - 1);
        s_base_path[sizeof(s_base_path) - 1] = '\0';
    }

#ifndef EDGEECG_HOST_BUILD
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return -1;
    }
#endif

    ensure_directory_exists(s_base_path);
    scan_queue_on_boot();
    return 0;
}

int pq_enqueue(const pq_record_t *rec) {
    if (rec == NULL) return -1;
    if (s_queue_depth >= PQ_MAX_QUEUE_CAPACITY) {
        return -1; // Queue capacity full, refuse to drop without ack
    }

    pq_record_t to_write = *rec;
    to_write.schema_version = PQ_SCHEMA_VERSION;
    to_write.sequence = s_next_seq++;

    // Compute CRC32 over all fields except the crc32 field itself
    size_t data_len = offsetof(pq_record_t, crc32);
    to_write.crc32 = calculate_crc32((const uint8_t *)&to_write, data_len);

    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s/%010u.rec", s_base_path, to_write.sequence);

    FILE *fp = fopen(filepath, "wb");
    if (!fp) return -2;

    size_t written = fwrite(&to_write, 1, sizeof(pq_record_t), fp);
    fclose(fp);

    if (written != sizeof(pq_record_t)) {
        unlink(filepath);
        return -2;
    }

    s_queue_depth++;
    return 0;
}

static int compare_uint32(const void *a, const void *b) {
    uint32_t val_a = *(const uint32_t *)a;
    uint32_t val_b = *(const uint32_t *)b;
    if (val_a < val_b) return -1;
    if (val_a > val_b) return 1;
    return 0;
}

int pq_get_pending_batch(pq_record_t *out_batch, size_t max_records, size_t *out_count) {
    if (!out_batch || !out_count) return -1;
    *out_count = 0;

    if (max_records > PQ_MAX_BATCH_SIZE) {
        max_records = PQ_MAX_BATCH_SIZE;
    }

    DIR *dir = opendir(s_base_path);
    if (!dir) return -1;

    // Collect available sequences
    uint32_t seqs[PQ_MAX_BATCH_SIZE * 2];
    size_t found = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && found < (sizeof(seqs) / sizeof(seqs[0]))) {
        if (strstr(entry->d_name, ".rec") != NULL) {
            uint32_t seq = 0;
            if (sscanf(entry->d_name, "%u.rec", &seq) == 1) {
                seqs[found++] = seq;
            }
        }
    }
    closedir(dir);

    if (found == 0) {
        return 0;
    }

    // Sort to deliver in strict monotonic sequence order
    qsort(seqs, found, sizeof(uint32_t), compare_uint32);

    size_t count = 0;
    size_t limit = (found < max_records) ? found : max_records;

    for (size_t i = 0; i < limit; i++) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%010u.rec", s_base_path, seqs[i]);

        FILE *fp = fopen(filepath, "rb");
        if (!fp) continue;

        pq_record_t rec;
        size_t n = fread(&rec, 1, sizeof(pq_record_t), fp);
        fclose(fp);

        if (n == sizeof(pq_record_t)) {
            // Verify CRC32
            size_t data_len = offsetof(pq_record_t, crc32);
            uint32_t computed = calculate_crc32((const uint8_t *)&rec, data_len);
            if (computed == rec.crc32) {
                out_batch[count++] = rec;
            } else {
                // Corrupted file, remove or quarantine
                unlink(filepath);
                if (s_queue_depth > 0) s_queue_depth--;
            }
        }
    }

    *out_count = count;
    return 0;
}

int pq_acknowledge_records(const uint32_t *sequences, size_t count) {
    if (!sequences || count == 0) return 0;

    int removed = 0;
    for (size_t i = 0; i < count; i++) {
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%010u.rec", s_base_path, sequences[i]);

        if (unlink(filepath) == 0) {
            removed++;
            if (s_queue_depth > 0) s_queue_depth--;
            if (sequences[i] > s_last_acked_seq) {
                s_last_acked_seq = sequences[i];
            }
        }
    }
    return removed;
}

int pq_serialize_batch_json(
    const char *device_id,
    const pq_record_t *batch,
    size_t count,
    char *out_json,
    size_t max_len
) {
    if (!device_id || !batch || !out_json || max_len == 0) return -1;

    int offset = snprintf(out_json, max_len,
        "{\"schema_version\":%d,\"device_id\":\"%s\",\"records\":[",
        PQ_SCHEMA_VERSION, device_id);

    if (offset < 0 || (size_t)offset >= max_len) return -1;

    for (size_t i = 0; i < count; i++) {
        const pq_record_t *r = &batch[i];

        char hr_str[32], rmssd_str[32], rr_str[32];
        if (isnan(r->heart_rate_bpm)) strcpy(hr_str, "null");
        else snprintf(hr_str, sizeof(hr_str), "%.1f", r->heart_rate_bpm);

        if (isnan(r->rmssd_ms)) strcpy(rmssd_str, "null");
        else snprintf(rmssd_str, sizeof(rmssd_str), "%.1f", r->rmssd_ms);

        if (isnan(r->rr_interval_ms)) strcpy(rr_str, "null");
        else snprintf(rr_str, sizeof(rr_str), "%.1f", r->rr_interval_ms);

        const char *sq_str = "valid";
        if (r->signal_quality == 1) sq_str = "degraded";
        else if (r->signal_quality == 2) sq_str = "invalid";

        int n = snprintf(out_json + offset, max_len - offset,
            "%s{\"sequence\":%u,\"session_id\":\"%s\",\"timestamp_ms\":%lld,"
            "\"heart_rate_bpm\":%s,\"rmssd_ms\":%s,\"rr_interval_ms\":%s,"
            "\"signal_quality\":\"%s\",\"detected_peaks\":%u,\"rejected_peaks\":%u}",
            (i > 0 ? "," : ""),
            r->sequence, r->session_id, (long long)r->timestamp_ms,
            hr_str, rmssd_str, rr_str, sq_str,
            r->detected_peaks, r->rejected_peaks);

        if (n < 0 || (size_t)(offset + n) >= max_len) return -1;
        offset += n;
    }

    int n_end = snprintf(out_json + offset, max_len - offset, "]}");
    if (n_end < 0 || (size_t)(offset + n_end) >= max_len) return -1;
    offset += n_end;

    return offset;
}

size_t pq_get_depth(void) {
    return s_queue_depth;
}

uint32_t pq_get_next_sequence(void) {
    return s_next_seq;
}

uint32_t pq_get_last_acknowledged_sequence(void) {
    return s_last_acked_seq;
}
