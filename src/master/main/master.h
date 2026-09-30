#pragma once

#include "driver/uart.h"
#include "silly_proto.h"

enum MASTER_STATES {
    _MASTER_STATES_UNSPEC = 0,
    MASTER_STATES_IDLE,
    MASTER_STATES_TOKEN_SENT,
    MASTER_STATES_TALKING_TO_SLAVE,
    _MASTER_STATES_SIZE,
};

struct master_ctx {
    enum MASTER_STATES state;
    silly_slave_id_t current_slave;
    silly_pkt_id_t last_pkt_id;
    silly_pkt_id_t token_pkt_id;
    silly_pkt_id_t synced_until_id;
    uart_port_t uart_port;
    TickType_t master_do_state_timeout;
};

void do_state_idle(struct master_ctx *ctx, TickType_t _);
void do_state_token_sent(struct master_ctx *ctx, TickType_t timeout);
void do_state_talking_to_slave(struct master_ctx *ctx, TickType_t timeout);

extern void (*state_handlers[])(struct master_ctx *ctx, TickType_t timeout);
