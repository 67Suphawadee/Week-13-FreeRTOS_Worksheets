#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "MUTEX_LAB";

#define LED_TASK1 GPIO_NUM_2
#define LED_TASK2 GPIO_NUM_4
#define LED_TASK3 GPIO_NUM_5
#define LED_CRITICAL GPIO_NUM_18

SemaphoreHandle_t xMutex;

typedef struct { uint32_t counter; char shared_buffer[100]; uint32_t checksum; uint32_t access_count; } shared_resource_t;
shared_resource_t shared_data = {0, "", 0, 0};

typedef struct { uint32_t successful_access; uint32_t failed_access; uint32_t corruption_detected; } access_stats_t;
access_stats_t stats = {0, 0, 0};

uint32_t calculate_checksum(const char* data, uint32_t counter) {
    uint32_t sum = counter;
    for (int i = 0; data[i] != '\0'; i++) { sum += (uint32_t)data[i] * (i + 1); }
    return sum;
}

void access_shared_resource(int task_id, const char* task_name, gpio_num_t led_pin) {
    ESP_LOGI(TAG, "[%s] Requesting access...", task_name);
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
        ESP_LOGI(TAG, "[%s] ✓ Mutex acquired", task_name);
        stats.successful_access++;
        gpio_set_level(led_pin, 1); gpio_set_level(LED_CRITICAL, 1);

        // CRITICAL SECTION
        uint32_t temp_counter = shared_data.counter;
        char temp_buffer[100];
        strcpy(temp_buffer, shared_data.shared_buffer);
        uint32_t expected_checksum = shared_data.checksum;
        uint32_t calculated_checksum = calculate_checksum(temp_buffer, temp_counter);
        if (calculated_checksum != expected_checksum && shared_data.access_count > 0) {
            ESP_LOGE(TAG, "[%s] ⚠️ DATA CORRUPTION DETECTED!", task_name);
            stats.corruption_detected++;
        }
        vTaskDelay(pdMS_TO_TICKS(500 + (esp_random() % 1000)));
        shared_data.counter = temp_counter + 1;
        snprintf(shared_data.shared_buffer, sizeof(shared_data.shared_buffer), "Modified by %s #%lu", task_name, shared_data.counter);
        shared_data.checksum = calculate_checksum(shared_data.shared_buffer, shared_data.counter);
        shared_data.access_count++;
        // END CRITICAL SECTION

        gpio_set_level(led_pin, 0); gpio_set_level(LED_CRITICAL, 0);
        xSemaphoreGive(xMutex);
        ESP_LOGI(TAG, "[%s] Mutex released", task_name);
    } else {
        ESP_LOGW(TAG, "[%s] ✗ Failed to acquire mutex", task_name);
        stats.failed_access++;
    }
}

void high_priority_task(void *p) {
    while (1) {
        access_shared_resource(1, "HIGH_PRI", LED_TASK1);
        vTaskDelay(pdMS_TO_TICKS(5000 + (esp_random() % 3000)));
    }
}

void medium_priority_task(void *p) {
    while (1) {
        access_shared_resource(2, "MED_PRI", LED_TASK2);
        vTaskDelay(pdMS_TO_TICKS(3000 + (esp_random() % 2000)));
    }
}

void low_priority_task(void *p) {
    while (1) {
        access_shared_resource(3, "LOW_PRI", LED_TASK3);
        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 1000)));
    }
}

void monitor_task(void *p) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(15000));
        ESP_LOGI(TAG, "\n═══ MUTEX MONITOR | Success: %lu | Failed: %lu | Corrupted: %lu ═══", stats.successful_access, stats.failed_access, stats.corruption_detected);
        uint32_t current_checksum = calculate_checksum(shared_data.shared_buffer, shared_data.counter);
        if (current_checksum != shared_data.checksum && shared_data.access_count > 0) {
            ESP_LOGE(TAG, "⚠️ CURRENT DATA CORRUPTION DETECTED!");
        }
        ESP_LOGI(TAG, "Shared Counter: %lu | Last Modifier: %s\n", shared_data.counter, shared_data.shared_buffer);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Mutex and Critical Sections Lab Starting...");
    gpio_config_t io_conf = { .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE };
    io_conf.pin_bit_mask = (1ULL<<LED_TASK1)|(1ULL<<LED_TASK2)|(1ULL<<LED_TASK3)|(1ULL<<LED_CRITICAL);
    gpio_config(&io_conf);

    xMutex = xSemaphoreCreateMutex();
    if (xMutex != NULL) {
        ESP_LOGI(TAG, "Mutex created successfully");
        shared_data.checksum = calculate_checksum(shared_data.shared_buffer, shared_data.counter);

        xTaskCreate(high_priority_task, "HighPri", 3072, NULL, 5, NULL);
        xTaskCreate(medium_priority_task, "MedPri", 3072, NULL, 3, NULL);
        xTaskCreate(low_priority_task, "LowPri", 3072, NULL, 2, NULL);
        xTaskCreate(monitor_task, "Monitor", 3072, NULL, 1, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create mutex!");
    }
}
