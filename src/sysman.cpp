#include "sysman.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <Arduino.h>

struct SysmanWork {
    sysman_fn_t fn;
    void       *arg;
};

static QueueHandle_t g_sysman_queue = NULL;
static TaskHandle_t  g_sysman_task  = NULL;

static void _sysman_task(void *param) {
    SysmanWork work;
    for (;;) {
        if (xQueueReceive(g_sysman_queue, &work, portMAX_DELAY) != pdTRUE)
            continue;
        if (work.fn)
            work.fn(work.arg);
    }
}

void sysman_init(void) {
    g_sysman_queue = xQueueCreate(SYSMAN_QUEUE_DEPTH, sizeof(SysmanWork));
    if (!g_sysman_queue) {
        log_e("sysman: failed to create queue");
        return;
    }
    xTaskCreatePinnedToCore(_sysman_task, "sysman", 4096, NULL, 0, &g_sysman_task, 0);
    if (!g_sysman_task)
        log_e("sysman: failed to create task");
    else
        log_i("sysman: initialized on Core 0, priority 0");
}

bool sysman_post(sysman_fn_t fn, void *arg) {
    if (!g_sysman_queue || !fn) return false;
    SysmanWork work = {fn, arg};
    BaseType_t ok = xQueueSend(g_sysman_queue, &work, 0);
    if (ok != pdTRUE)
        log_w("sysman: queue full, dropping work item");
    return ok == pdTRUE;
}

bool sysman_is_busy(void) {
    if (!g_sysman_queue) return false;
    return uxQueueMessagesWaiting(g_sysman_queue) > 0;
}
