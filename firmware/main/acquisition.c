#include "acquisition.h"
#include <string.h>

#ifndef EDGEECG_HOST_BUILD
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ACQ";

#define ECG_ADC_CHANNEL        ADC_CHANNEL_6    // GPIO 34 (ADC1)
#define ECG_PIN_LO_PLUS        GPIO_NUM_18
#define ECG_PIN_LO_MINUS       GPIO_NUM_19

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static esp_timer_handle_t sample_timer = NULL;
#endif

// Ring Buffer Storage
static acq_sample_t ring_buffer[ACQ_RING_CAPACITY];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

// Diagnostics
static volatile uint64_t total_samples_counter = 0;
static volatile uint32_t overrun_counter = 0;
static volatile uint32_t saturation_counter = 0;
static volatile uint32_t leads_off_counter = 0;

bool acq_is_saturated(uint16_t raw) {
    return (raw <= ADC_SATURATION_LOW || raw >= ADC_SATURATION_HIGH);
}

bool acq_push_sample(uint16_t raw_adc, bool leads_off) {
    uint32_t next_head = (ring_head + 1) % ACQ_RING_CAPACITY;
    bool saturated = acq_is_saturated(raw_adc);

    total_samples_counter++;
    if (saturated) {
        saturation_counter++;
    }
    if (leads_off) {
        leads_off_counter++;
    }

    if (next_head == ring_tail) {
        // Buffer overrun: discard oldest sample to make room
        ring_tail = (ring_tail + 1) % ACQ_RING_CAPACITY;
        overrun_counter++;
    }

    ring_buffer[ring_head].sample_index = total_samples_counter;
    ring_buffer[ring_head].raw_adc = raw_adc;
    ring_buffer[ring_head].leads_off = leads_off;
    ring_buffer[ring_head].saturated = saturated;

    ring_head = next_head;
    return true;
}

bool acq_pop(acq_sample_t *out) {
    if (ring_head == ring_tail) {
        return false;
    }
    if (out != NULL) {
        *out = ring_buffer[ring_tail];
    }
    ring_tail = (ring_tail + 1) % ACQ_RING_CAPACITY;
    return true;
}

uint64_t acq_total_samples(void) {
    return total_samples_counter;
}

uint32_t acq_overrun_count(void) {
    return overrun_counter;
}

uint32_t acq_saturation_count(void) {
    return saturation_counter;
}

uint32_t acq_leads_off_count(void) {
    return leads_off_counter;
}

#ifndef EDGEECG_HOST_BUILD
static void timer_callback(void* arg) {
    int raw = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, ECG_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) {
        return;
    }

    int lo_plus = gpio_get_level(ECG_PIN_LO_PLUS);
    int lo_minus = gpio_get_level(ECG_PIN_LO_MINUS);
    bool leads_off = (lo_plus != 0 || lo_minus != 0);

    acq_push_sample((uint16_t)raw, leads_off);
}
#endif

void acq_init(void) {
    ring_head = 0;
    ring_tail = 0;
    total_samples_counter = 0;
    overrun_counter = 0;
    saturation_counter = 0;
    leads_off_counter = 0;

#ifndef EDGEECG_HOST_BUILD
    // Configure ADC1
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_12,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ECG_ADC_CHANNEL, &config));

    // Configure Leads-Off GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ECG_PIN_LO_PLUS) | (1ULL << ECG_PIN_LO_MINUS),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // Create 250 Hz hardware timer (period: 4000 microseconds)
    const esp_timer_create_args_t timer_args = {
        .callback = &timer_callback,
        .name = "ecg_sampler"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sample_timer));
#endif
}

void acq_start(void) {
#ifndef EDGEECG_HOST_BUILD
    if (sample_timer != NULL) {
        ESP_ERROR_CHECK(esp_timer_start_periodic(sample_timer, SAMPLE_PERIOD_US));
    }
#endif
}

void acq_stop(void) {
#ifndef EDGEECG_HOST_BUILD
    if (sample_timer != NULL) {
        esp_timer_stop(sample_timer);
    }
#endif
}
