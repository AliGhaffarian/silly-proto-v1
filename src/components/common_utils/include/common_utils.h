#pragma once

#include "freertos/FreeRTOS.h"
#include <stddef.h>
#include <stdlib.h>

void inline freep(void *ptr)
{
    free(*(void **)ptr);
    *(void **)ptr = NULL;
}

TickType_t remaining_time(TickType_t deadline, TickType_t timeout);

#define __cleanup_free__      __attribute__((__cleanup__(freep)))
#define GET_NTH_BYTE(data, n) ((data & (0xffULL << ((n) * 8))) >> ((n) * 8))

#define MOVE(ptr)                                                              \
    ({                                                                         \
        void *__tmp_move_var = ptr;                                            \
        ptr = NULL;                                                            \
        __tmp_move_var;                                                        \
    })

int safe_realloc(void **ptr, size_t size);
