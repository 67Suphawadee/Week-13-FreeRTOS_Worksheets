#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"

static const char *TAG = "QUEUE_LAB";

#define LED_SENDER GPIO_NUM_2
#define LED_RECEIVER GPIO_NUM_4

QueueHandle_t xQueue;

typedef struct {
    int id;
    char message[50];
    uint32_t timestamp;
} queue_message_t;

void sender_task(void *pvParameters) {
    queue_message_t message;
    int counter = 0;
    ESP_LOGI(TAG, "Sender task started");
    while (1) {
        message.id = counter++;
        snprintf(message.message, sizeof(message.message), "Hello from sender #%d", message.id);
        message.timestamp = xTaskGetTickCount();

        BaseType_t xStatus = xQueueSend(xQueue, &message, pdMS_TO_TICKS(1000));
        if (xStatus == pdPASS) {
            ESP_LOGI(TAG, "Sent: ID=%d, Time=%lu", message.id, message.timestamp);
            gpio_set_level(LED_SENDER, 1);
            vTaskDelay(pdMS_TO_TICKS(100));
            gpio_set_level(LED_SENDER, 0);
        } else {
            ESP_LOGW(TAG, "Failed to send message (queue full?)");
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // Send every 2 seconds
    }
}

void receiver_task(void *pvParameters) {
    queue_message_t received_message;
    ESP_LOGI(TAG, "Receiver task started");
    while (1) {
        BaseType_t xStatus = xQueueReceive(xQueue, &received_message, pdMS_TO_TICKS(5000));
        if (xStatus == pdPASS) {
            ESP_LOGI(TAG, "Received: ID=%d, MSG=%s", received_message.id, received_message.message);
            gpio_set_level(LED_RECEIVER, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_RECEIVER, 0);
            vTaskDelay(pdMS_TO_TICKS(1500)); // Simulate work
        } else {
            ESP_LOGW(TAG, "No message received within timeout");
        }
    }
}

void queue_monitor_task(void *pvParameters) {
    UBaseType_t uxMessagesWaiting;
    UBaseType_t uxSpacesAvailable;
    ESP_LOGI(TAG, "Queue monitor task started");
    while (1) {
        uxMessagesWaiting = uxQueueMessagesWaiting(xQueue);
        uxSpacesAvailable = uxQueueSpacesAvailable(xQueue);
        ESP_LOGI(TAG, "Queue Status - Messages: %d, Free spaces: %d", uxMessagesWaiting, uxSpacesAvailable);
        printf("Queue: [\n");
        for (int i = 0; i < 5; i++) {
            if (i < uxMessagesWaiting) printf("■");
            else printf("□");
        }
        printf("]\n");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Basic Queue Operations Lab Starting...");

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << LED_SENDER) | (1ULL << LED_RECEIVER);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    xQueue = xQueueCreate(5, sizeof(queue_message_t));
    if (xQueue != NULL) {
        ESP_LOGI(TAG, "Queue created successfully (size: 5 messages)");
        xTaskCreate(sender_task, "Sender", 2048, NULL, 2, NULL);
        xTaskCreate(receiver_task, "Receiver", 2048, NULL, 1, NULL);
        xTaskCreate(queue_monitor_task, "Monitor", 2048, NULL, 1, NULL);
    } else {
        ESP_LOGE(TAG, "Failed to create queue!");
    }
}
