#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"

#define LED_OK GPIO_NUM_2
#define LED_WARNING GPIO_NUM_4

static const char *TAG = "STACK_MONITOR";

#define STACK_WARNING_THRESHOLD 512
#define STACK_CRITICAL_THRESHOLD 256

// Task handles for monitoring
TaskHandle_t light_task_handle = NULL;
TaskHandle_t medium_task_handle = NULL;
TaskHandle_t heavy_task_handle = NULL;
TaskHandle_t optimized_task_handle = NULL;
TaskHandle_t recursion_task_handle = NULL;

// --- Task Functions from README ---

void stack_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Stack Monitor Task started");
    while (1) {
        ESP_LOGI(TAG, "\n=== STACK USAGE REPORT ===");
        TaskHandle_t tasks[] = {light_task_handle, medium_task_handle, heavy_task_handle, optimized_task_handle, recursion_task_handle, xTaskGetCurrentTaskHandle()};
        const char* task_names[] = {"Light", "Medium", "Heavy", "Optimized", "Recursion", "Monitor"};
        bool stack_warning = false, stack_critical = false;

        for (int i = 0; i < 6; i++) {
            if (tasks[i] != NULL) {
                UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(tasks[i]);
                uint32_t stack_bytes = stack_remaining * sizeof(StackType_t);
                ESP_LOGI(TAG, "%sTask: %d bytes remaining", task_names[i], stack_bytes);
                if (stack_bytes < STACK_CRITICAL_THRESHOLD) {
                    ESP_LOGE(TAG, "CRITICAL: %sTask stack very low!", task_names[i]);
                    stack_critical = true;
                } else if (stack_bytes < STACK_WARNING_THRESHOLD) {
                    ESP_LOGW(TAG, "WARNING: %sTask stack low", task_names[i]);
                    stack_warning = true;
                }
            }
        }

        if (stack_critical) {
            for (int i = 0; i < 10; i++) { gpio_set_level(LED_WARNING, 1); vTaskDelay(pdMS_TO_TICKS(50)); gpio_set_level(LED_WARNING, 0); vTaskDelay(pdMS_TO_TICKS(50)); }
        } else if (stack_warning) {
            gpio_set_level(LED_WARNING, 1); gpio_set_level(LED_OK, 0);
        } else {
            gpio_set_level(LED_OK, 1); gpio_set_level(LED_WARNING, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void light_stack_task(void *pvParameters) {
    ESP_LOGI(TAG, "Light Stack Task started");
    int counter = 0;
    while (1) {
        counter++;
        ESP_LOGD(TAG, "Light task cycle: %d", counter);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void medium_stack_task(void *pvParameters) {
    ESP_LOGI(TAG, "Medium Stack Task started");
    while (1) {
        char buffer[256]; int numbers[50];
        memset(buffer, 'A', sizeof(buffer) - 1); buffer[sizeof(buffer) - 1] = '\0';
        for (int i = 0; i < 50; i++) { numbers[i] = i; }
        ESP_LOGD(TAG, "Medium task running");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void heavy_stack_task(void *pvParameters) {
    ESP_LOGI(TAG, "Heavy Stack Task started");
    while (1) {
        char large_buffer[1024]; int large_numbers[200]; char another_buffer[512];
        ESP_LOGW(TAG, "Heavy task: Using large stack arrays");
        memset(large_buffer, 'X', sizeof(large_buffer) - 1); large_buffer[sizeof(large_buffer) - 1] = '\0';
        ESP_LOGD(TAG, "Heavy task running");
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
}

void optimized_heavy_task(void *pvParameters) {
    ESP_LOGI(TAG, "Optimized Heavy Task started");
    char *large_buffer = malloc(1024);
    if (!large_buffer) { ESP_LOGE(TAG, "Optimized task malloc failed"); vTaskDelete(NULL); }
    while (1) {
        ESP_LOGI(TAG, "Optimized task: Using heap");
        memset(large_buffer, 'Y', 1023); large_buffer[1023] = '\0';
        UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGI(TAG, "Optimized task stack: %d bytes remaining", stack_remaining * sizeof(StackType_t));
        vTaskDelay(pdMS_TO_TICKS(4000));
    }
    free(large_buffer);
}

void recursive_function(int depth) {
    char local_array[100];
    snprintf(local_array, sizeof(local_array), "Recursion depth: %d", depth);
    ESP_LOGI(TAG, "%s - Stack: %d bytes", local_array, uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    if (uxTaskGetStackHighWaterMark(NULL) < 20) { // ~80 bytes
        ESP_LOGE(TAG, "Stopping recursion at depth %d", depth);
        return;
    }
    if (depth < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        recursive_function(depth + 1);
    }
}

void recursion_demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "Recursion Demo Task started");
    while (1) {
        ESP_LOGW(TAG, "=== STARTING RECURSION ===");
        recursive_function(1);
        ESP_LOGW(TAG, "=== RECURSION COMPLETED ===");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// NOTE: This hook is only called if CONFIG_FREERTOS_CHECK_STACKOVERFLOW is enabled in sdkconfig
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    ESP_LOGE("STACK_OVERFLOW", "Task %s has overflowed its stack! System will restart.", pcTaskName);
    for (int i = 0; i < 20; i++) { gpio_set_level(LED_WARNING, 1); vTaskDelay(pdMS_TO_TICKS(25)); gpio_set_level(LED_WARNING, 0); vTaskDelay(pdMS_TO_TICKS(25)); }
    esp_restart();
}

void app_main(void) {
    ESP_LOGI(TAG, "=== FreeRTOS Stack Monitoring Demo ===");

    gpio_config_t io_conf = { .intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << LED_OK) | (1ULL << LED_WARNING), .pull_down_en = 0, .pull_up_en = 0 };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "LEDs: GPIO2=OK, GPIO4=Warning");
    ESP_LOGI(TAG, "Creating tasks...");

    xTaskCreate(light_stack_task, "LightTask", 1024, NULL, 2, &light_task_handle);
    xTaskCreate(medium_stack_task, "MediumTask", 2048, NULL, 2, &medium_task_handle);
    // This heavy task has the same stack size as medium, but uses more, to trigger warnings
    xTaskCreate(heavy_stack_task, "HeavyTask", 2048, NULL, 2, &heavy_task_handle);
    // This optimized task uses the heap and should have a high water mark similar to the light task
    xTaskCreate(optimized_heavy_task, "OptimizedTask", 2048, NULL, 2, &optimized_task_handle);
    xTaskCreate(recursion_demo_task, "RecursionDemo", 3072, NULL, 1, &recursion_task_handle);
    xTaskCreate(stack_monitor_task, "StackMonitor", 4096, NULL, 3, NULL);

    ESP_LOGI(TAG, "All tasks created.");
}
