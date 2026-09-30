#include "master.h"

#include "common_utils.h"
#include "master.h"
#include "shared_uart_setup.h"
#include "silly_proto.h"
#include "uart_utils.h"
#include <errno.h>
#include <esp_log.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define PKT_TIMEOUT 900

void app_main(void)
{
    struct master_ctx ctx = {
        .uart_port = SHARED_UART_NUM,
        .synced_until_id = 0,
        .last_pkt_id = 0,
        .current_slave = 1,
        .master_do_state_timeout = PKT_TIMEOUT,
        .state = MASTER_STATES_IDLE,
    };

    shared_setup_uart(NULL, 0);

    while(1) {
        ESP_LOGI("master: main", "current state:%d", ctx.state);
        __builtin_dump_struct(&ctx, printf);
        state_handlers[ctx.state](&ctx, ctx.master_do_state_timeout);
    }
}
