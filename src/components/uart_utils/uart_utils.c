#include "include/uart_utils.h"
#include "driver/uart.h"
#include "soc/gpio_num.h"
#include <inttypes.h>

void uart_raed_bytes_blocking(
    uart_port_t uart_port, void *buf, size_t read_size)
{
    size_t total_read_bytes = 0;
    size_t read_bytes = 0;

    while(total_read_bytes != read_size) {
        read_bytes = uart_read_bytes(
            uart_port, buf, read_size - total_read_bytes, READ_SLEEP_TICKS);
        total_read_bytes += read_bytes;
    }
}
