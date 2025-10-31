#include <stdio.h>
#include <malloc.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define LED_RUNNING GPIO_NUM_2
#define LED_READY GPIO_NUM_4
#define LED_BLOCKED GPIO_NUM_5
#define LED_SUSPENDED GPIO_NUM_18
#define BUTTON1_PIN GPIO_NUM_0
#define BUTTON2_PIN GPIO_NUM_35

static const char *TAG = "TASK_STATES";

// Task handles
TaskHandle_t state_demo_task_handle = NULL;
TaskHandle_t control_task_handle = NULL;
TaskHandle_t external_delete_handle = NULL;

SemaphoreHandle_t demo_semaphore = NULL;

const char* state_names[] = {"Running", "Ready", "Blocked", "Suspended", "Deleted", "Invalid"};

const char* get_state_name(eTaskState state) {
    return (state <= eDeleted) ? state_names[state] : state_names[5];
}

// --- Task Functions from README ---

void state_demo_task(void *pvParameters) {
    ESP_LOGI(TAG, "State Demo Task started");
    int cycle = 0;
    while (1) {
        cycle++;
        ESP_LOGI(TAG, "=== Cycle %d: RUNNING ===", cycle);
        gpio_set_level(LED_RUNNING, 1);
        gpio_set_level(LED_READY, 0);
        gpio_set_level(LED_BLOCKED, 0);
        gpio_set_level(LED_SUSPENDED, 0);
        for (int i = 0; i < 1000000; i++) { volatile int dummy = i * 2; }

        ESP_LOGI(TAG, "Task -> READY (yielding)");
        gpio_set_level(LED_RUNNING, 0);
        gpio_set_level(LED_READY, 1);
        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(100));

        ESP_LOGI(TAG, "Task -> BLOCKED (waiting for semaphore)");
        gpio_set_level(LED_READY, 0);
        gpio_set_level(LED_BLOCKED, 1);
        if (xSemaphoreTake(demo_semaphore, pdMS_TO_TICKS(2000)) == pdTRUE) {
            ESP_LOGI(TAG, "Got semaphore! Task -> RUNNING");
        } else {
            ESP_LOGI(TAG, "Semaphore timeout!");
        }
        gpio_set_level(LED_BLOCKED, 0);

        ESP_LOGI(TAG, "Task -> BLOCKED (in vTaskDelay)");
        gpio_set_level(LED_BLOCKED, 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
        gpio_set_level(LED_BLOCKED, 0);
    }
}

void ready_state_demo_task(void *pvParameters) {
    while (1) {
        ESP_LOGI(TAG, "Ready-demo task running (makes other task Ready)");
        vTaskDelay(pdMS_TO_TICKS(150));
    }
}

void self_deleting_task(void *pvParameters) {
    int lifetime = *(int *)pvParameters;
    ESP_LOGI(TAG, "Self-deleting task will live for %d seconds", lifetime);
    for (int i = lifetime; i > 0; i--) {
        ESP_LOGI(TAG, "Self-deleting task countdown: %d", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGW(TAG, "Self-deleting task -> DELETED");
    vTaskDelete(NULL);
}

void external_delete_task(void *pvParameters) {
    int count = 0;
    while (1) {
        ESP_LOGI(TAG, "External delete task running: %d", count++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void control_task(void *pvParameters) {
    ESP_LOGI(TAG, "Control Task started");
    bool suspended = false;
    bool external_deleted = false;
    int control_cycle = 0;

    while (1) {
        control_cycle++;

        if (gpio_get_level(BUTTON1_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            if (!suspended) {
                ESP_LOGW(TAG, "=== SUSPENDING State Demo Task ===");
                vTaskSuspend(state_demo_task_handle);
                gpio_set_level(LED_SUSPENDED, 1);
                gpio_set_level(LED_RUNNING, 0); gpio_set_level(LED_READY, 0); gpio_set_level(LED_BLOCKED, 0);
                suspended = true;
            } else {
                ESP_LOGW(TAG, "=== RESUMING State Demo Task ===");
                vTaskResume(state_demo_task_handle);
                gpio_set_level(LED_SUSPENDED, 0);
                suspended = false;
            }
            while (gpio_get_level(BUTTON1_PIN) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (gpio_get_level(BUTTON2_PIN) == 0) {
            vTaskDelay(pdMS_TO_TICKS(50)); // Debounce
            ESP_LOGW(TAG, "=== GIVING SEMAPHORE ===");
            xSemaphoreGive(demo_semaphore);
            while (gpio_get_level(BUTTON2_PIN) == 0) vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (control_cycle % 30 == 0) { // 3 seconds
            ESP_LOGI(TAG, "--- Task Status Report ---");
            eTaskState demo_state = eTaskGetState(state_demo_task_handle);
            ESP_LOGI(TAG, "State Demo Task: %s (Prio: %d, Stack: %d)", get_state_name(demo_state), uxTaskPriorityGet(state_demo_task_handle), uxTaskGetStackHighWaterMark(state_demo_task_handle));
        }

        if (control_cycle == 150 && !external_deleted) { // After 15 seconds
            ESP_LOGW(TAG, "Control task deleting external_delete_task");
            if(external_delete_handle != NULL) vTaskDelete(external_delete_handle);
            external_deleted = true;
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void system_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "System Monitor started");
    // NOTE: This task requires configGENERATE_RUN_TIME_STATS and configUSE_TRACE_FACILITY
    char *buffer = malloc(2048);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate buffer for monitor");
        vTaskDelete(NULL);
    }
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "\n--- SYSTEM MONITOR ---");
        vTaskList(buffer);
        printf("Name\t\tState\tPrio\tStack\tNum\n%s\n", buffer);
        vTaskGetRunTimeStats(buffer);
        printf("Task\t\tAbs Time\t%%Time\n%s\n", buffer);
    }
    free(buffer);
}

void app_main(void) {
    ESP_LOGI(TAG, "=== FreeRTOS Task States Demo ===");

    gpio_config_t io_conf = { .intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = (1ULL << LED_RUNNING) | (1ULL << LED_READY) | (1ULL << LED_BLOCKED) | (1ULL << LED_SUSPENDED), .pull_down_en = 0, .pull_up_en = 0 };
    gpio_config(&io_conf);
    gpio_config_t btn_conf = { .intr_type = GPIO_INTR_DISABLE, .mode = GPIO_MODE_INPUT, .pin_bit_mask = (1ULL << BUTTON1_PIN) | (1ULL << BUTTON2_PIN), .pull_up_en = 1, .pull_down_en = 0 };
    gpio_config(&btn_conf);

    demo_semaphore = xSemaphoreCreateBinary();

    ESP_LOGI(TAG, "LEDs: GPIO2=Run, GPIO4=Ready, GPIO5=Block, GPIO18=Suspend");
    ESP_LOGI(TAG, "Btns: GPIO0=Suspend/Resume, GPIO35=Give Semaphore");

    static int self_delete_time = 10;
    xTaskCreate(state_demo_task, "StateDemo", 4096, NULL, 3, &state_demo_task_handle);
    xTaskCreate(ready_state_demo_task, "ReadyDemo", 2048, NULL, 3, NULL);
    xTaskCreate(control_task, "Control", 3072, NULL, 4, &control_task_handle);
    xTaskCreate(system_monitor_task, "Monitor", 4096, NULL, 1, NULL);
    xTaskCreate(self_deleting_task, "SelfDelete", 2048, &self_delete_time, 2, NULL);
    xTaskCreate(external_delete_task, "ExtDelete", 2048, NULL, 2, &external_delete_handle);

    ESP_LOGI(TAG, "All tasks created.");
}
