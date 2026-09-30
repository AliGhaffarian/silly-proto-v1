#pragma once

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "soc/gpio_num.h"

#define READ_SLEEP_TICKS 100

void uart_raed_bytes_blocking(uart_port_t uart_port, void *buf, size_t size);
