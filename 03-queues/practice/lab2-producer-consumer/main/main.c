#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "PROD_CONS";

#define LED_PRODUCER_1 GPIO_NUM_2
#define LED_PRODUCER_2 GPIO_NUM_4
#define LED_PRODUCER_3 GPIO_NUM_5
#define LED_CONSUMER_1 GPIO_NUM_18
#define LED_CONSUMER_2 GPIO_NUM_19

QueueHandle_t xProductQueue;
SemaphoreHandle_t xPrintMutex;

typedef struct { uint32_t produced; uint32_t consumed; uint32_t dropped; } stats_t;
stats_t global_stats = {0, 0, 0};

typedef struct {
    int producer_id; int product_id; char product_name[30];
    uint32_t production_time; int processing_time_ms;
} product_t;

void safe_printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (xSemaphoreTake(xPrintMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        vprintf(format, args);
        xSemaphoreGive(xPrintMutex);
    }
    va_end(args);
}

void producer_task(void *pvParameters) {
    int producer_id = *((int*)pvParameters);
    product_t product;
    int product_counter = 0;
    gpio_num_t led_pin = (producer_id == 1) ? LED_PRODUCER_1 : (producer_id == 2) ? LED_PRODUCER_2 : LED_PRODUCER_3;
    safe_printf("Producer %d started\n", producer_id);
    while (1) {
        product.producer_id = producer_id;
        product.product_id = product_counter++;
        snprintf(product.product_name, sizeof(product.product_name), "Product-P%d-#%d", producer_id, product.product_id);
        product.production_time = xTaskGetTickCount();
        product.processing_time_ms = 500 + (esp_random() % 2000);

        if (xQueueSend(xProductQueue, &product, pdMS_TO_TICKS(100)) == pdPASS) {
            global_stats.produced++;
            safe_printf("✓ P%d: Created %s (%dms)\n", producer_id, product.product_name, product.processing_time_ms);
            gpio_set_level(led_pin, 1); vTaskDelay(pdMS_TO_TICKS(50)); gpio_set_level(led_pin, 0);
        } else {
            global_stats.dropped++;
            safe_printf("✗ P%d: Queue full! Dropped %s\n", producer_id, product.product_name);
        }
        vTaskDelay(pdMS_TO_TICKS(1000 + (esp_random() % 2000)));
    }
}

void consumer_task(void *pvParameters) {
    int consumer_id = *((int*)pvParameters);
    product_t product;
    gpio_num_t led_pin = (consumer_id == 1) ? LED_CONSUMER_1 : LED_CONSUMER_2;
    safe_printf("Consumer %d started\n", consumer_id);
    while (1) {
        if (xQueueReceive(xProductQueue, &product, pdMS_TO_TICKS(5000)) == pdPASS) {
            global_stats.consumed++;
            uint32_t queue_time = (xTaskGetTickCount() - product.production_time) * portTICK_PERIOD_MS;
            safe_printf("→ C%d: Processing %s (q_time: %lums)\n", consumer_id, product.product_name, queue_time);
            gpio_set_level(led_pin, 1);
            vTaskDelay(pdMS_TO_TICKS(product.processing_time_ms));
            gpio_set_level(led_pin, 0);
            safe_printf("✓ C%d: Finished %s\n", consumer_id, product.product_name);
        } else {
            safe_printf("⏰ C%d: No products (timeout)\n", consumer_id);
        }
    }
}

void statistics_task(void *pvParameters) {
    safe_printf("Statistics task started\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        UBaseType_t queue_items = uxQueueMessagesWaiting(xProductQueue);
        float efficiency = global_stats.produced > 0 ? (float)global_stats.consumed / global_stats.produced * 100 : 0;
        safe_printf("\n═══ STATS | Produced: %lu | Consumed: %lu | Dropped: %lu | Efficiency: %.1f%% ═══\n",
                    global_stats.produced, global_stats.consumed, global_stats.dropped, efficiency);
        printf("Queue: [\" );
        for (int i = 0; i < 10; i++) { printf(i < queue_items ? "■" : "□"); }
        printf("] (%d items)\n\n", queue_items);
    }
}

void load_balancer_task(void *pvParameters) {
    safe_printf("Load balancer started\n");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (uxQueueMessagesWaiting(xProductQueue) > 8) {
            safe_printf("⚠️ HIGH LOAD DETECTED! Queue > 8\n");
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Producer-Consumer System Lab Starting...");
    gpio_config_t io_conf = { .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE };
    io_conf.pin_bit_mask = (1ULL<<LED_PRODUCER_1)|(1ULL<<LED_PRODUCER_2)|(1ULL<<LED_PRODUCER_3)|(1ULL<<LED_CONSUMER_1)|(1ULL<<LED_CONSUMER_2);
    gpio_config(&io_conf);

    xProductQueue = xQueueCreate(10, sizeof(product_t));
    xPrintMutex = xSemaphoreCreateMutex();

    if (xProductQueue != NULL && xPrintMutex != NULL) {
        ESP_LOGI(TAG, "Queue and mutex created successfully");
        static int p_ids[] = {1, 2, 3}; static int c_ids[] = {1, 2};
        xTaskCreate(producer_task, "Producer1", 3072, &p_ids[0], 3, NULL);
        xTaskCreate(producer_task, "Producer2", 3072, &p_ids[1], 3, NULL);
        xTaskCreate(producer_task, "Producer3", 3072, &p_ids[2], 3, NULL);
        xTaskCreate(consumer_task, "Consumer1", 3072, &c_ids[0], 2, NULL);
        xTaskCreate(consumer_task, "Consumer2", 3072, &c_ids[1], 2, NULL);
        xTaskCreate(statistics_task, "Statistics", 3072, NULL, 1, NULL);
        xTaskCreate(load_balancer_task, "LoadBalancer", 2048, NULL, 1, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create queue or mutex!");
    }
}
