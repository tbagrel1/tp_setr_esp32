#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#define DEFAULT_STACK_SIZE 10000
#define PRODUCER_PRIORITY 2
#define PRODUCER_SLEEP_MS 1250
#define CONSUMER_PRIORITY 1
#define CONSUMER_SLEEP_MS 2500
#define BUFFER_SIZE 10
#define SWITCH_ON 0
#define SWITCH_OFF 1
#define NO_OP 2
#define DEBUG true

#define DPRINT(format, args...) \
    { if (DEBUG) { \
        UBaseType_t uxPriority = uxTaskPriorityGet(NULL); \
        char const *pcTaskName = pcTaskGetTaskName(NULL); \
        printf("[Task \"%s\" on core %d (priority %d)] " format "\n", pcTaskName, xPortGetCoreID(), uxPriority, args); \
    } }

typedef int message_t;

char const *pcPretty(message_t message) {
    char *result;
    switch (message) {
        case SWITCH_ON:
            result = "SWITCH_ON";
            break;
        case SWITCH_OFF:
            result = "SWITCH_OFF";
            break;
        case NO_OP:
            result = "NO_OP";
            break;
        default:
            result = "<Unknown>";
            break;
    }
    return result;
}

int producerIndex = 0;
int consumerIndex = 0;

SemaphoreHandle_t producerTokenCount, consumerTokenCount, producerIndexMutex, consumerIndexMutex;

message_t buffer[BUFFER_SIZE];

void vPushMessage(message_t message) {
    xSemaphoreTake(producerTokenCount, portMAX_DELAY);
    xSemaphoreTake(producerIndexMutex, portMAX_DELAY);
    buffer[producerIndex] = message;
    DPRINT("Message written: %s", pcPretty(message));
    producerIndex = (producerIndex + 1) % BUFFER_SIZE;
    xSemaphoreGive(producerIndexMutex);
    xSemaphoreGive(consumerTokenCount);
}

message_t mPopMessage() {
    xSemaphoreTake(consumerTokenCount, portMAX_DELAY);
    xSemaphoreTake(consumerIndexMutex, portMAX_DELAY);
    message_t message = buffer[consumerIndex];
    DPRINT("Message read: %s", pcPretty(message));
    consumerIndex = (consumerIndex + 1) % BUFFER_SIZE;
    xSemaphoreGive(consumerIndexMutex);
    xSemaphoreGive(producerTokenCount);
    return message;
}

void vProducerTask(void *pvParameters) {
    TickType_t xLastWakeTime;
    bool state = false;
    message_t message;
    for (;;) {
        xLastWakeTime = xTaskGetTickCount();
        state = !state;
        message = state ? SWITCH_ON : SWITCH_OFF;
        vPushMessage(message);
        // vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(PRODUCER_SLEEP_MS));
    }
}

void vConsumerTask(void *pvParameters) {
    int iGpioId = *((int *) pvParameters);
    TickType_t xLastWakeTime;
    message_t message;
    gpio_set_direction(iGpioId, GPIO_MODE_OUTPUT);
    for (;;) {
        xLastWakeTime = xTaskGetTickCount();
        message = mPopMessage();
        switch (message) {
            case SWITCH_ON:
                gpio_set_level(iGpioId, 1);
                break;
            case SWITCH_OFF:
                gpio_set_level(iGpioId, 0);
                break;
            default:
                break;
        }
        // vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(CONSUMER_SLEEP_MS));
    }
}

void app_main() {
    producerTokenCount = xSemaphoreCreateCounting(BUFFER_SIZE, BUFFER_SIZE);
    consumerTokenCount = xSemaphoreCreateCounting(BUFFER_SIZE, 0);
    producerIndexMutex = xSemaphoreCreateMutex();
    consumerIndexMutex = xSemaphoreCreateMutex();
    
    // Création des tâches
    xTaskCreatePinnedToCore(
        vProducerTask,        // Pointeur sur la fonction implémentant la tache
        "P1",   // String pour Debugging
        DEFAULT_STACK_SIZE,   // Taille de la pile associée à la tâche
        NULL,                 // Paramètres passés à la tâche
        PRODUCER_PRIORITY,    // Prioritée associée
        NULL,
        0 // Core ID
    );                // Handler pour la gestion de la tâche
    
    int *piGreenLedId = malloc(sizeof(*piGreenLedId));
    *piGreenLedId = GPIO_NUM_18;
    xTaskCreatePinnedToCore(
        vConsumerTask,
        "C1 - green",
        DEFAULT_STACK_SIZE,
        piGreenLedId,
        CONSUMER_PRIORITY,
        NULL,
        0 // Core ID
    );

    int *piRedLedId = malloc(sizeof(*piRedLedId));
    *piRedLedId = GPIO_NUM_19;
    xTaskCreatePinnedToCore(
        vConsumerTask,
        "C2 - red",
        DEFAULT_STACK_SIZE,
        piRedLedId,
        CONSUMER_PRIORITY,
        NULL,
        0 // Core ID
    );
}