#pragma once

#include "silly_proto.h"

enum SLAVE_STATES {
    _SLAVE_STATES_UNSPEC = 0,
    SLAVE_STATES_IDLE,
    SLAVE_STATES_TALKING_TO_MASTER,
    _SLAVE_STATES_SIZE,
};

struct slave_ctx {
    enum SLAVE_STATES state;
    silly_slave_id_t my_slave_id;
    silly_pkt_id_t last_pkt_id;
    silly_pkt_id_t synced_until_id;
    silly_pkt_id_t master_synced_until_id;
    uart_port_t uart_port;
    TickType_t slave_do_state_timeout;
    // TODO: make this a ring buffer
    struct silly_proto_header *prev_out;
};

void do_state_idle(struct slave_ctx *ctx, TickType_t _);
void do_state_talking_to_master(struct slave_ctx *ctx, TickType_t timeout);

extern void (*state_handlers[])(struct slave_ctx *ctx, TickType_t timeout);
