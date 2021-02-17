#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include "driver/gpio.h"

#define BEGINNING_SLEEP_MS 1000
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
#define ENABLE_PRODUCER_DELAY false
#define ENABLE_CONSUMER_DELAY false

#define DPRINT(format, args...) \
    { if (DEBUG) { \
        UBaseType_t uxPriority = uxTaskPriorityGet(NULL); \
        char const *pcTaskName = pcTaskGetTaskName(NULL); \
        printf("[Task \"%s\" on core %d (priority %d)] " format "\n", pcTaskName, xPortGetCoreID(), uxPriority, args); \
    } }
typedef int message_t;

typedef struct {
    int iGpioId;
    message_t xMessage;
    TickType_t xBeginTime;
    SemaphoreHandle_t xSemaphore;
} ProducerParams_t;

typedef struct {
    int iGpioId;
    TickType_t xBeginTime;
} ConsumerParams_t;

char const *pcPretty(message_t xMessage) {
    char *pcResult;
    switch (xMessage) {
        case SWITCH_ON:
            pcResult = "SWITCH_ON";
            break;
        case SWITCH_OFF:
            pcResult = "SWITCH_OFF";
            break;
        case NO_OP:
            pcResult = "NO_OP";
            break;
        default:
            pcResult = "<Unknown>";
            break;
    }
    return pcResult;
}

int iProducerIndex = 0;
int iConsumerIndex = 0;

SemaphoreHandle_t xProducerTokenCount, xConsumerTokenCount, xProducerIndexMutex, xConsumerIndexMutex, xProducer1Action, xProducer2Action;

message_t axBuffer[BUFFER_SIZE];

void vIsrHandlerProducer1(void *params) {
    xSemaphoreGiveFromISR(xProducer1Action, NULL);
}

void vIsrHandlerProducer2(void *params) {
    xSemaphoreGiveFromISR(xProducer2Action, NULL);
}

void vPushMessage(message_t xMessage, TickType_t *pxLastWakeTime) {
    xSemaphoreTake(xProducerTokenCount, portMAX_DELAY);
    xSemaphoreTake(xProducerIndexMutex, portMAX_DELAY);
    *pxLastWakeTime = xTaskGetTickCount();
    axBuffer[iProducerIndex] = xMessage;
    DPRINT("Message written: %s", pcPretty(xMessage));
    iProducerIndex = (iProducerIndex + 1) % BUFFER_SIZE;
    xSemaphoreGive(xProducerIndexMutex);
    xSemaphoreGive(xConsumerTokenCount);
}

message_t mPopMessage(TickType_t *pxLastWakeTime) {
    xSemaphoreTake(xConsumerTokenCount, portMAX_DELAY);
    xSemaphoreTake(xConsumerIndexMutex, portMAX_DELAY);
    *pxLastWakeTime = xTaskGetTickCount();
    message_t xMessage = axBuffer[iConsumerIndex];
    DPRINT("Message read: %s", pcPretty(xMessage));
    iConsumerIndex = (iConsumerIndex + 1) % BUFFER_SIZE;
    xSemaphoreGive(xConsumerIndexMutex);
    xSemaphoreGive(xProducerTokenCount);
    return xMessage;
}

void vProducer1Task(void *pvParameters) {
    ProducerParams_t params = *((ProducerParams_t *) pvParameters);
    TickType_t xLastWakeTime;
    vTaskDelayUntil(&params.xBeginTime, pdMS_TO_TICKS(BEGINNING_SLEEP_MS));
    for (;;) {
        xSemaphoreTake(xProducer1Action, portMAX_DELAY);
        printf("%d\n", SWITCH_ON);
        vPushMessage(SWITCH_ON, &xLastWakeTime);
    }
}

void vProducer2Task(void *pvParameters) {
    ProducerParams_t params = *((ProducerParams_t *) pvParameters);
    TickType_t xLastWakeTime;
    vTaskDelayUntil(&params.xBeginTime, pdMS_TO_TICKS(BEGINNING_SLEEP_MS));
    for (;;) {
        xSemaphoreTake(xProducer2Action, portMAX_DELAY);
        printf("%d\n", SWITCH_OFF);
        vPushMessage(SWITCH_OFF, &xLastWakeTime);
    }
}

void vConsumerTask(void *pvParameters) {
    ConsumerParams_t params = *((ConsumerParams_t *) pvParameters);
    TickType_t xLastWakeTime;
    message_t xMessage;
    vTaskDelayUntil(&params.xBeginTime, pdMS_TO_TICKS(BEGINNING_SLEEP_MS));
    for (;;) {
        xMessage = mPopMessage(&xLastWakeTime);
        switch (xMessage) {
            case SWITCH_ON:
                gpio_set_level(params.iGpioId, 1);
                break;
            case SWITCH_OFF:
                gpio_set_level(params.iGpioId, 0);
                break;
            default:
                break;
        }
        if (ENABLE_CONSUMER_DELAY) {
            vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(CONSUMER_SLEEP_MS));
        }
    }
}

void app_main() {
    xProducerTokenCount = xSemaphoreCreateCounting(BUFFER_SIZE, BUFFER_SIZE);
    xConsumerTokenCount = xSemaphoreCreateCounting(BUFFER_SIZE, 0);
    xProducerIndexMutex = xSemaphoreCreateMutex();
    xConsumerIndexMutex = xSemaphoreCreateMutex();
    xProducer1Action = xSemaphoreCreateCounting(1, 0);
    xProducer2Action = xSemaphoreCreateCounting(1, 0);

    gpio_set_direction(GPIO_NUM_18, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT);
    gpio_config_t xIoButtonConf;
    xIoButtonConf.intr_type = GPIO_INTR_POSEDGE;
    xIoButtonConf.mode = GPIO_MODE_INPUT;
    xIoButtonConf.pin_bit_mask = (1ULL << GPIO_NUM_4) | (1ULL << GPIO_NUM_16);
    xIoButtonConf.pull_up_en = true;
    xIoButtonConf.pull_down_en = true;
    gpio_config(&xIoButtonConf);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_NUM_4, vIsrHandlerProducer1, NULL);
    gpio_isr_handler_add(GPIO_NUM_16, vIsrHandlerProducer2, NULL);
    

    TickType_t xBeginTime = xTaskGetTickCount();

    DPRINT("\n\n\n\n------------------ %s ------------------\n\n\n\n", "START");
    
    ProducerParams_t *pxProducer1Params = malloc(sizeof(*pxProducer1Params));
    pxProducer1Params->xMessage = SWITCH_ON;
    pxProducer1Params->xBeginTime = xBeginTime;
    pxProducer1Params->iGpioId = GPIO_NUM_4;
    pxProducer1Params->xSemaphore = xProducer1Action;
    // Création des tâches
    xTaskCreatePinnedToCore(
        vProducer1Task,        // Pointeur sur la fonction implémentant la tache
        "P1 - SWITCH_ON",   // String pour Debugging
        DEFAULT_STACK_SIZE,   // Taille de la pile associée à la tâche
        pxProducer1Params,   // Paramètres passés à la tâche
        PRODUCER_PRIORITY,    // Prioritée associée
        NULL,
        0 // Core ID
    );                // Handler pour la gestion de la tâche

    ProducerParams_t *pxProducer2Params = malloc(sizeof(*pxProducer2Params));
    pxProducer2Params->xMessage = SWITCH_OFF;
    pxProducer2Params->xBeginTime = xBeginTime;
    pxProducer2Params->iGpioId = GPIO_NUM_16;
    pxProducer2Params->xSemaphore = xProducer2Action;
    // Création des tâches
    xTaskCreatePinnedToCore(
        vProducer2Task,        // Pointeur sur la fonction implémentant la tache
        "P2 - SWITCH_OFF",   // String pour Debugging
        DEFAULT_STACK_SIZE,   // Taille de la pile associée à la tâche
        pxProducer2Params,   // Paramètres passés à la tâche
        PRODUCER_PRIORITY,    // Prioritée associée
        NULL,
        0 // Core ID
    );                // Handler pour la gestion de la tâche
    
    ConsumerParams_t *pxConsumer1Params = malloc(sizeof(*pxConsumer1Params));
    pxConsumer1Params->iGpioId = GPIO_NUM_18;
    pxConsumer1Params->xBeginTime = xBeginTime;
    xTaskCreatePinnedToCore(
        vConsumerTask,
        "C1 - green",
        DEFAULT_STACK_SIZE,
        pxConsumer1Params,
        CONSUMER_PRIORITY,
        NULL,
        0 // Core ID
    );

    ConsumerParams_t *pxConsumer2Params = malloc(sizeof(*pxConsumer2Params));
    pxConsumer2Params->iGpioId = GPIO_NUM_19;
    pxConsumer2Params->xBeginTime = xBeginTime;
    xTaskCreatePinnedToCore(
        vConsumerTask,
        "C2 - red",
        DEFAULT_STACK_SIZE,
        pxConsumer2Params,
        CONSUMER_PRIORITY,
        NULL,
        0 // Core ID
    );
}