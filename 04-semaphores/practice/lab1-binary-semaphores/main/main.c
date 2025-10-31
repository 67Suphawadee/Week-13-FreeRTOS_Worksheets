#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_random.h"

static const char *TAG = "BINARY_SEM";

#define LED_PRODUCER GPIO_NUM_2
#define LED_CONSUMER GPIO_NUM_4
#define LED_TIMER GPIO_NUM_5
#define BUTTON_PIN GPIO_NUM_0

SemaphoreHandle_t xBinarySemaphore, xTimerSemaphore, xButtonSemaphore;
gptimer_handle_t gptimer = NULL;
typedef struct { uint32_t sent, received, timer, button; } stats_t;
stats_t stats = {0,0,0,0};

static bool IRAM_ATTR timer_callback(gptimer_handle_t t, const gptimer_alarm_event_data_t *e, void *u) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(xTimerSemaphore, &woken);
    return woken == pdTRUE;
}

static void IRAM_ATTR button_isr_handler(void* arg) {
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(xButtonSemaphore, &woken);
    portYIELD_FROM_ISR(woken);
}

void producer_task(void *p) {
    ESP_LOGI(TAG, "Producer task started");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 3000)));
        ESP_LOGI(TAG, "🔥 Producer: Generating event");
        if (xSemaphoreGive(xBinarySemaphore) == pdTRUE) {
            stats.sent++;
            gpio_set_level(LED_PRODUCER, 1); vTaskDelay(100); gpio_set_level(LED_PRODUCER, 0);
        } else {
            ESP_LOGW(TAG, "✗ Producer: Failed to signal (semaphore already given?)");
        }
    }
}

void consumer_task(void *p) {
    ESP_LOGI(TAG, "Consumer task started");
    while (1) {
        ESP_LOGI(TAG, "🔍 Consumer: Waiting for event...");
        if (xSemaphoreTake(xBinarySemaphore, pdMS_TO_TICKS(10000)) == pdTRUE) {
            stats.received++;
            ESP_LOGI(TAG, "⚡ Consumer: Event received! Processing...");
            gpio_set_level(LED_CONSUMER, 1);
            vTaskDelay(pdMS_TO_TICKS(1000 + (esp_random() % 2000)));
            gpio_set_level(LED_CONSUMER, 0);
        } else {
            ESP_LOGW(TAG, "⏰ Consumer: Timeout waiting for event");
        }
    }
}

void timer_event_task(void *p) {
    ESP_LOGI(TAG, "Timer event task started");
    while (1) {
        if (xSemaphoreTake(xTimerSemaphore, portMAX_DELAY) == pdTRUE) {
            stats.timer++;
            ESP_LOGI(TAG, "⏱️ Timer: Periodic event #%lu", stats.timer);
            gpio_set_level(LED_TIMER, 1); vTaskDelay(200); gpio_set_level(LED_TIMER, 0);
            if (stats.timer % 5 == 0) {
                ESP_LOGI(TAG, "📊 Stats | Sent:%lu, Rcvd:%lu, Timer:%lu, Btn:%lu", stats.sent, stats.received, stats.timer, stats.button);
            }
        }
    }
}

void button_event_task(void *p) {
    ESP_LOGI(TAG, "Button event task started");
    while (1) {
        if (xSemaphoreTake(xButtonSemaphore, portMAX_DELAY) == pdTRUE) {
            stats.button++;
            ESP_LOGI(TAG, "🔘 Button: Press #%lu", stats.button);
            vTaskDelay(pdMS_TO_TICKS(300)); // Debounce
            ESP_LOGI(TAG, "🚀 Button: Triggering immediate event");
            if(xSemaphoreGive(xBinarySemaphore) == pdTRUE) stats.sent++;
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Binary Semaphores Lab Starting...");

    gpio_config_t led_conf = { .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE, .pin_bit_mask = (1ULL<<LED_PRODUCER)|(1ULL<<LED_CONSUMER)|(1ULL<<LED_TIMER) };
    gpio_config(&led_conf);
    gpio_config_t btn_conf = { .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE, .intr_type = GPIO_INTR_NEGEDGE, .pin_bit_mask = (1ULL<<BUTTON_PIN) };
    gpio_config(&btn_conf);

    xBinarySemaphore = xSemaphoreCreateBinary();
    xTimerSemaphore = xSemaphoreCreateBinary();
    xButtonSemaphore = xSemaphoreCreateBinary();

    if (xBinarySemaphore && xTimerSemaphore && xButtonSemaphore) {
        ESP_LOGI(TAG, "Semaphores created");
        gpio_install_isr_service(0);
        gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);

        gptimer_config_t timer_config = { .clk_src = GPTIMER_CLK_SRC_DEFAULT, .direction = GPTIMER_COUNT_UP, .resolution_hz = 1000000 };
        gptimer_new_timer(&timer_config, &gptimer);
        gptimer_event_callbacks_t cbs = { .on_alarm = timer_callback };
        gptimer_register_event_callbacks(gptimer, &cbs, NULL);
        gptimer_enable(gptimer);
        gptimer_alarm_config_t alarm_config = { .alarm_count = 8000000, .flags.auto_reload_on_alarm = true };
        gptimer_set_alarm_action(gptimer, &alarm_config);
        gptimer_start(gptimer);

        xTaskCreate(producer_task, "Producer", 2048, NULL, 3, NULL);
        xTaskCreate(consumer_task, "Consumer", 2048, NULL, 2, NULL);
        xTaskCreate(timer_event_task, "TimerEvent", 2048, NULL, 4, NULL);
        xTaskCreate(button_event_task, "ButtonEvent", 2048, NULL, 5, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create semaphores!");
    }
}
