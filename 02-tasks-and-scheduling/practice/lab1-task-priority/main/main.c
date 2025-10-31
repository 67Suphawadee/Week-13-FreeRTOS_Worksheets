#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define LED_HIGH_PIN GPIO_NUM_2
#define LED_MED_PIN GPIO_NUM_4
#define LED_LOW_PIN GPIO_NUM_5
#define BUTTON_PIN GPIO_NUM_0

static const char *TAG = "PRIORITY_DEMO";

// Global variables
volatile uint32_t high_task_count = 0;
volatile uint32_t med_task_count = 0;
volatile uint32_t low_task_count = 0;
volatile bool priority_test_running = false;
volatile bool shared_resource_busy = false;

// --- Task Functions from README ---

// High Priority Task (Priority 5)
void high_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "High Priority Task started (Priority 5)");
    while (1) {
        if (priority_test_running) {
            high_task_count++;
            ESP_LOGI(TAG, "HIGH PRIORITY RUNNING (%d)", high_task_count);
            gpio_set_level(LED_HIGH_PIN, 1);
            for (int i = 0; i < 100000; i++) { volatile int dummy = i * 2; }
            gpio_set_level(LED_HIGH_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(200));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Medium Priority Task (Priority 3)
void medium_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "Medium Priority Task started (Priority 3)");
    while (1) {
        if (priority_test_running) {
            med_task_count++;
            ESP_LOGI(TAG, "Medium priority running (%d)", med_task_count);
            gpio_set_level(LED_MED_PIN, 1);
            for (int i = 0; i < 200000; i++) { volatile int dummy = i + 100; }
            gpio_set_level(LED_MED_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Low Priority Task (Priority 1)
void low_priority_task(void *pvParameters) {
    ESP_LOGI(TAG, "Low Priority Task started (Priority 1)");
    while (1) {
        if (priority_test_running) {
            low_task_count++;
            ESP_LOGI(TAG, "Low priority running (%d)", low_task_count);
            gpio_set_level(LED_LOW_PIN, 1);
            for (int i = 0; i < 500000; i++) {
                volatile int dummy = i - 50;
                if (i % 100000 == 0) { vTaskDelay(1); }
            }
            gpio_set_level(LED_LOW_PIN, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// Equal Priority Tasks (Priority 2)
void equal_priority_task(void *pvParameters) {
    int task_id = (int)pvParameters;
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "Equal Priority Task %d running", task_id);
            for (int i = 0; i < 300000; i++) { volatile int dummy = i; }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// Priority Inversion Demo Tasks
void priority_inversion_high(void *pvParameters) {
    while (1) {
        if (priority_test_running) {
            ESP_LOGW(TAG, "High-PI task needs shared resource");
            while (shared_resource_busy) {
                ESP_LOGE(TAG, "High-PI task BLOCKED by low priority task!");
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            ESP_LOGI(TAG, "High-PI task got resource");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void priority_inversion_low(void *pvParameters) {
    while (1) {
        if (priority_test_running) {
            ESP_LOGI(TAG, "Low-PI task using shared resource");
            shared_resource_busy = true;
            vTaskDelay(pdMS_TO_TICKS(2000)); // Simulate long resource usage
            shared_resource_busy = false;
            ESP_LOGI(TAG, "Low-PI task released resource");
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// Control Task - starts/stops the test
void control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Control Task started");
    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {
            if (!priority_test_running) {
                ESP_LOGW(TAG, "=== STARTING PRIORITY TEST (10 seconds) ===");
                high_task_count = 0;
                med_task_count = 0;
                low_task_count = 0;
                priority_test_running = true;

                vTaskDelay(pdMS_TO_TICKS(10000));

                priority_test_running = false;
                ESP_LOGW(TAG, "=== PRIORITY TEST RESULTS ===");
                ESP_LOGI(TAG, "High Priority Task runs: %d", high_task_count);
                ESP_LOGI(TAG, "Medium Priority Task runs: %d", med_task_count);
                ESP_LOGI(TAG, "Low Priority Task runs: %d", low_task_count);
                uint32_t total_runs = high_task_count + med_task_count + low_task_count;
                if (total_runs > 0) {
                    ESP_LOGI(TAG, "High priority percentage: %.1f%%", (float)high_task_count / total_runs * 100);
                    ESP_LOGI(TAG, "Medium priority percentage: %.1f%%", (float)med_task_count / total_runs * 100);
                    ESP_LOGI(TAG, "Low priority percentage: %.1f%%", (float)low_task_count / total_runs * 100);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== FreeRTOS Priority Scheduling Demo ===");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LED_HIGH_PIN) | (1ULL << LED_MED_PIN) | (1ULL << LED_LOW_PIN),
        .pull_down_en = 0, .pull_up_en = 0,
    };
    gpio_config(&io_conf);

    gpio_config_t button_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = 1ULL << BUTTON_PIN,
        .pull_up_en = 1, .pull_down_en = 0,
    };
    gpio_config(&button_conf);

    ESP_LOGI(TAG, "Creating tasks...");

    // Basic Priority Demo
    xTaskCreate(high_priority_task, "HighPrio", 3072, NULL, 5, NULL);
    xTaskCreate(medium_priority_task, "MedPrio", 3072, NULL, 3, NULL);
    xTaskCreate(low_priority_task, "LowPrio", 3072, NULL, 1, NULL);

    // Round-Robin Demo
    xTaskCreate(equal_priority_task, "Equal1", 2048, (void*)1, 2, NULL);
    xTaskCreate(equal_priority_task, "Equal2", 2048, (void*)2, 2, NULL);
    xTaskCreate(equal_priority_task, "Equal3", 2048, (void*)3, 2, NULL);

    // Priority Inversion Demo
    // Note: This is a simplified demo. Real solutions use mutexes.
    xTaskCreate(priority_inversion_high, "PI-High", 2048, NULL, 6, NULL); // Highest priority
    xTaskCreate(priority_inversion_low, "PI-Low", 2048, NULL, 1, NULL);   // Lowest priority

    // Control Task
    xTaskCreate(control_task, "Control", 3072, NULL, 4, NULL);

    ESP_LOGI(TAG, "Press button (GPIO0) to start priority test");
}
