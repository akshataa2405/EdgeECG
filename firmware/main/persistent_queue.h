#ifndef EDGEECG_PERSISTENT_QUEUE_H
#define EDGEECG_PERSISTENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PQ_SCHEMA_VERSION          1
#define PQ_MAX_BATCH_SIZE          100
#define PQ_MAX_QUEUE_CAPACITY      10000

typedef struct __attribute__((packed)) {
    uint8_t  schema_version;
    char     device_id[32];
    char     session_id[37];
    uint32_t sequence;
    int64_t  timestamp_ms;
    uint8_t  ts_synced;          // 1 if synced via NTP, 0 otherwise
    float    heart_rate_bpm;
    float    rmssd_ms;
    float    rr_interval_ms;
    uint8_t  signal_quality;     // 0=valid, 1=degraded, 2=invalid
    uint32_t detected_peaks;
    uint32_t rejected_peaks;
    uint32_t crc32;
} pq_record_t;

/**
 * @brief Initialize LittleFS (or host directory) and recover sequence state.
 * @param base_path Directory path (e.g., "/littlefs/queue" on ESP32, "./queue_store" on host).
 * @return 0 on success, non-zero error code otherwise.
 */
int pq_init(const char *base_path);

/**
 * @brief Enqueue a new metric record durably to storage.
 * @param rec Pointer to record to persist.
 * @return 0 on success, -1 on capacity error / disk full, -2 on write failure.
 */
int pq_enqueue(const pq_record_t *rec);

/**
 * @brief Peek up to max_records unacknowledged records in sequence order.
 * @param out_batch Array of records to populate.
 * @param max_records Maximum records to retrieve (<= PQ_MAX_BATCH_SIZE).
 * @param out_count Actual number of records retrieved.
 * @return 0 on success.
 */
int pq_get_pending_batch(pq_record_t *out_batch, size_t max_records, size_t *out_count);

/**
 * @brief Mark records as acknowledged by deleting their persistent files.
 * @param sequences Array of sequence numbers returned by server.
 * @param count Number of sequences acknowledged.
 * @return Number of records successfully removed.
 */
int pq_acknowledge_records(const uint32_t *sequences, size_t count);

/**
 * @brief Serialize a batch of records into JSON format matching POST /api/v1/ingest.
 * @param device_id Device identifier string.
 * @param batch Array of records.
 * @param count Number of records.
 * @param out_json Buffer to receive null-terminated JSON string.
 * @param max_len Maximum length of out_json buffer.
 * @return Number of bytes written, or -1 on overflow.
 */
int pq_serialize_batch_json(
    const char *device_id,
    const pq_record_t *batch,
    size_t count,
    char *out_json,
    size_t max_len
);

/**
 * @brief Get queue statistics.
 */
size_t   pq_get_depth(void);
uint32_t pq_get_next_sequence(void);
uint32_t pq_get_last_acknowledged_sequence(void);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_PERSISTENT_QUEUE_H
