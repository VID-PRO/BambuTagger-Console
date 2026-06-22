#pragma once

#include <stdbool.h>
#include <stddef.h>

#define SYSMAN_QUEUE_DEPTH 16

typedef void (*sysman_fn_t)(void *arg);

void sysman_init(void);
bool sysman_post(sysman_fn_t fn, void *arg);
bool sysman_is_busy(void);
