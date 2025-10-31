#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_random.h"

static const char *TAG = "QUEUE_SETS";

#define LED_SENSOR GPIO_NUM_2
#define LED_USER GPIO_NUM_4
#define LED_NETWORK GPIO_NUM_5
#define LED_TIMER GPIO_NUM_18
#define LED_PROCESSOR GPIO_NUM_19

QueueHandle_t xSensorQueue, xUserQueue, xNetworkQueue;
SemaphoreHandle_t xTimerSemaphore;
QueueSetHandle_t xQueueSet;

typedef struct { int sensor_id; float temperature; float humidity; uint32_t timestamp; } sensor_data_t;
typedef struct { int button_id; bool pressed; uint32_t duration_ms; } user_input_t;
typedef struct { char source[20]; char message[100]; int priority; } network_message_t;
typedef struct { uint32_t sensor_count, user_count, network_count, timer_count; } message_stats_t;
message_stats_t stats = {0,0,0,0};

void sensor_task(void *p) {
    sensor_data_t data;
    ESP_LOGI(TAG, "Sensor task started");
    while(1) {
        data.temperature = 20.0 + (esp_random() % 200) / 10.0;
        data.humidity = 30.0 + (esp_random() % 400) / 10.0;
        if (xQueueSend(xSensorQueue, &data, 0) == pdPASS) {
            ESP_LOGI(TAG, "📊 Sensor: T=%.1f, H=%.1f", data.temperature, data.humidity);
            gpio_set_level(LED_SENSOR, 1); vTaskDelay(50); gpio_set_level(LED_SENSOR, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 3000)));
    }
}

void user_input_task(void *p) {
    user_input_t input;
    ESP_LOGI(TAG, "User input task started");
    while(1) {
        input.button_id = 1 + (esp_random() % 3);
        if (xQueueSend(xUserQueue, &input, 0) == pdPASS) {
            ESP_LOGI(TAG, "🔘 User: Button %d pressed", input.button_id);
            gpio_set_level(LED_USER, 1); vTaskDelay(50); gpio_set_level(LED_USER, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(3000 + (esp_random() % 5000)));
    }
}

void network_task(void *p) {
    network_message_t msg;
    ESP_LOGI(TAG, "Network task started");
    while(1) {
        strcpy(msg.source, "WiFi"); strcpy(msg.message, "Status update");
        if (xQueueSend(xNetworkQueue, &msg, 0) == pdPASS) {
            ESP_LOGI(TAG, "🌐 Network: Msg from %s", msg.source);
            gpio_set_level(LED_NETWORK, 1); vTaskDelay(50); gpio_set_level(LED_NETWORK, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(1000 + (esp_random() % 3000)));
    }
}

void timer_task(void *p) {
    ESP_LOGI(TAG, "Timer task started");
    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (xSemaphoreGive(xTimerSemaphore) == pdPASS) {
            ESP_LOGI(TAG, "⏰ Timer: Event fired");
            gpio_set_level(LED_TIMER, 1); vTaskDelay(100); gpio_set_level(LED_TIMER, 0);
        }
    }
}

void processor_task(void *p) {
    QueueSetMemberHandle_t xActivatedMember;
    sensor_data_t sensor_data; user_input_t user_input; network_message_t network_msg;
    ESP_LOGI(TAG, "Processor task started");
    while(1) {
        xActivatedMember = xQueueSelectFromSet(xQueueSet, portMAX_DELAY);
        gpio_set_level(LED_PROCESSOR, 1);
        if (xActivatedMember == xSensorQueue && xQueueReceive(xSensorQueue, &sensor_data, 0) == pdPASS) {
            stats.sensor_count++; ESP_LOGI(TAG, "→ Processing SENSOR data");
        } else if (xActivatedMember == xUserQueue && xQueueReceive(xUserQueue, &user_input, 0) == pdPASS) {
            stats.user_count++; ESP_LOGI(TAG, "→ Processing USER input");
        } else if (xActivatedMember == xNetworkQueue && xQueueReceive(xNetworkQueue, &network_msg, 0) == pdPASS) {
            stats.network_count++; ESP_LOGI(TAG, "→ Processing NETWORK message");
        } else if (xActivatedMember == xTimerSemaphore && xSemaphoreTake(xTimerSemaphore, 0) == pdPASS) {
            stats.timer_count++; ESP_LOGI(TAG, "→ Processing TIMER event");
            ESP_LOGI(TAG, "--- STATS | Sensor:%lu, User:%lu, Net:%lu, Timer:%lu ---", stats.sensor_count, stats.user_count, stats.network_count, stats.timer_count);
        }
        vTaskDelay(pdMS_TO_TICKS(200)); // Simulate processing
        gpio_set_level(LED_PROCESSOR, 0);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Queue Sets Lab Starting...");
    gpio_config_t io_conf = { .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE };
    io_conf.pin_bit_mask = (1ULL<<LED_SENSOR)|(1ULL<<LED_USER)|(1ULL<<LED_NETWORK)|(1ULL<<LED_TIMER)|(1ULL<<LED_PROCESSOR);
    gpio_config(&io_conf);

    xSensorQueue = xQueueCreate(5, sizeof(sensor_data_t));
    xUserQueue = xQueueCreate(3, sizeof(user_input_t));
    xNetworkQueue = xQueueCreate(8, sizeof(network_message_t));
    xTimerSemaphore = xSemaphoreCreateBinary();
    xQueueSet = xQueueCreateSet(5 + 3 + 8 + 1);

    if (xQueueSet && xQueueAddToSet(xSensorQueue, xQueueSet) == pdPASS &&
        xQueueAddToSet(xUserQueue, xQueueSet) == pdPASS &&
        xQueueAddToSet(xNetworkQueue, xQueueSet) == pdPASS &&
        xQueueAddToSet(xTimerSemaphore, xQueueSet) == pdPASS) {
        
        ESP_LOGI(TAG, "Queue set created successfully");
        xTaskCreate(sensor_task, "Sensor", 2048, NULL, 3, NULL);
        xTaskCreate(user_input_task, "UserInput", 2048, NULL, 3, NULL);
        xTaskCreate(network_task, "Network", 2048, NULL, 3, NULL);
        xTaskCreate(timer_task, "Timer", 2048, NULL, 2, NULL);
        xTaskCreate(processor_task, "Processor", 3072, NULL, 4, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create or configure queue set!");
    }
}
