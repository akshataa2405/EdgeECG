#ifndef EDGEECG_SYNC_CLIENT_H
#define EDGEECG_SYNC_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char backend_url[128];
    char device_id[32];
    char device_key[64];
    uint32_t upload_interval_ms;
    uint32_t max_backoff_ms;
} sync_config_t;

/**
 * @brief Initialize synchronization client and configure backend endpoints.
 */
void sync_init(const sync_config_t *config);

/**
 * @brief Start the FreeRTOS background sync task and NTP client.
 */
void sync_start_task(void);

/**
 * @brief Trigger an immediate upload sync cycle.
 * @return 0 on success, negative error code on failure.
 */
int sync_trigger_now(void);

/**
 * @brief Check if NTP time has been acquired.
 */
bool sync_is_time_synced(void);

/**
 * @brief Get current UTC timestamp in milliseconds.
 */
int64_t sync_get_timestamp_ms(void);

#ifdef __cplusplus
}
#endif

#endif // EDGEECG_SYNC_CLIENT_H
