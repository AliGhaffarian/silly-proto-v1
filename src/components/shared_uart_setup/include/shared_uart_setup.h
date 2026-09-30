#pragma once

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "soc/gpio_num.h"

// TODO: make this unshared (this is shared for simplicity)
#define SHARED_UART_NUM ((uart_port_t)UART_NUM_1)

#define TX_GPIO_NUM  GPIO_NUM_18
#define RX_GPIO_NUM  GPIO_NUM_19
#define RTS_GPIO_NUM GPIO_NUM_21
#define CTS_GPIO_NUM GPIO_NUM_22

#define SHARED_BAUD_RATE           115200
#define SHARED_DATA_BITS           UART_DATA_8_BITS
#define SHARED_PARITY              UART_PARITY_DISABLE
#define SHARED_STOP_BITS           UART_STOP_BITS_1
#define SHARED_FLOW_CTRL           UART_HW_FLOWCTRL_CTS_RTS
#define SHARED_RX_FLOW_CTRL_THRESH 122

void shared_setup_uart(QueueHandle_t *qh_ptr, int queue_size);
