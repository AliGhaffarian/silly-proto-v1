/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "slave.h"
#include "common_utils.h"
#include "shared_uart_setup.h"
#include "silly_proto.h"
#include "uart_utils.h"
#include <errno.h>
#include <esp_log.h>
#include <esp_random.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "slave"

void (*state_handlers[])(struct slave_ctx *ctx, TickType_t timeout) = {
    [_SLAVE_STATES_UNSPEC] = NULL,
    [SLAVE_STATES_IDLE] = do_state_idle,
    [SLAVE_STATES_TALKING_TO_MASTER] = do_state_talking_to_master,
};

void update_prev_sent(struct slave_ctx *ctx, struct silly_proto_header **out)
{
    free(ctx->prev_out);
    ctx->prev_out = MOVE(*out);
}

void do_state_idle(struct slave_ctx *ctx, TickType_t timeout)
{
    int err = 0;
    __cleanup_free__ struct silly_proto_header *incoming =
        calloc(1, sizeof(struct silly_proto_header));
    __cleanup_free__ struct silly_proto_header *ack =
        calloc(1, sizeof(struct silly_proto_header));
    TimeOut_t do_timeout;
    TickType_t ticks_to_wait = timeout;
    BaseType_t timeout_result = 0;
    vTaskSetTimeOutState(&do_timeout);

    silly_uart_recv_pkt(ctx->uart_port, &incoming, ticks_to_wait);
    timeout_result = xTaskCheckForTimeOut(&do_timeout, &ticks_to_wait);

    if(timeout_result == pdTRUE) {
        ESP_LOGI(TAG, "timed out");
        return;
    }

    if(incoming->dst_slave_id != ctx->my_slave_id)
        return;

    silly_update_synced_until_id(&ctx->synced_until_id, incoming);

    if(!(incoming->flags & SILLY_USE_TOKEN))
        return;

    err = silly_reset_header(&ack);
    if(err)
        return; // unexpected

    ESP_LOGI(TAG, "token received");

    ack->synced_until_id = ctx->synced_until_id;
    ack->flags = SILLY_USE_TOKEN | SILLY_ACK;
    ack->header_checksum = silly_compute_header_checksum(ack);

    ESP_LOGI(TAG, "sending ack");
    __builtin_dump_struct(ack, printf);
    uart_write_bytes(ctx->uart_port, ack, ack->len);
    update_prev_sent(ctx, &ack);

    ctx->state = SLAVE_STATES_TALKING_TO_MASTER;
}

void retransmit_old_pkt(struct slave_ctx *ctx)
{
    ESP_LOGI(TAG, "retransmitting previously sent packet");
    uart_write_bytes(ctx->uart_port, ctx->prev_out, ctx->prev_out->len);
}

void st_talking_to_master_handle_incoming(
    struct slave_ctx *ctx,
    struct silly_proto_header *incoming,
    TimeOut_t *do_timeout,
    TickType_t *ticks_to_wait,
    TickType_t timeout)
{

    if(incoming->flags & SILLY_KEEP_ALIVE) {
        ESP_LOGI(TAG, "got a keep alive, resetting the timeout");
        vTaskSetTimeOutState(do_timeout);
        *ticks_to_wait = timeout;
    }
    // TODO: implement the retransmittion via packet history, not just the
    // previous packet
    if((incoming->flags & SILLY_ACK) &&
       (incoming->synced_until_id == ctx->last_pkt_id)) {

        ESP_LOGI(TAG, "got an edge pkt_id ack");
        ctx->master_synced_until_id = ctx->last_pkt_id;

    } else if(incoming->flags & SILLY_ACK) {

        // at this point ctx->prev_out is impossible to be NULL, because we
        // certainly sent something before it
        ESP_LOGI(TAG, "got an old pkt_id ack, retransmitting the last packet");
        retransmit_old_pkt(ctx);

    } else if(incoming->flags & SILLY_TOKEN_REVOKED) {

        ESP_LOGI(TAG, "got token revoked");
        silly_reset_state_variables(&ctx->last_pkt_id, &ctx->synced_until_id);
        ctx->master_synced_until_id = 0;
        ctx->state = SLAVE_STATES_IDLE;
    }
}

// @brief writes some data to out and updates len. will realloc *out
int st_talking_to_master_write_data_to_out(struct silly_proto_header **out)
{
    uint32_t tmp;
    char random_array[8] = {0};
    tmp = esp_random();
    int err;

    if(tmp % 10 == 0) {
        ESP_LOGI(TAG, "returning no data");
        return ENODATA;
    }

    for(int j = 0; j < 4; j++)
        random_array[j] = GET_NTH_BYTE(tmp, j);
    tmp = esp_random();
    for(int j = 4; j < 8; j++)
        random_array[j] = GET_NTH_BYTE(tmp, j - 4);

    err = safe_realloc(
        (void **)out, sizeof(struct silly_proto_header) + sizeof(random_array));
    if(err)
        return err;

    memcpy((*out)->data, random_array, sizeof(random_array));
    (*out)->len = sizeof(struct silly_proto_header) + sizeof(random_array);

    return 0;
}

int send_token_gaveup(struct slave_ctx *ctx)
{
    __cleanup_free__ struct silly_proto_header *token_gaveup =
        calloc(1, sizeof(struct silly_proto_header));
    int err = 0;

    if(!token_gaveup) {
        return ENOMEM;
    }

    err = silly_reset_header(&token_gaveup);

    if(err)
        return err; // unexpected

    token_gaveup->flags = SILLY_GAVEUP_TOKEN;
    token_gaveup->synced_until_id = ctx->synced_until_id;
    token_gaveup->header_checksum = silly_compute_header_checksum(token_gaveup);

    ESP_LOGI(TAG, "giving up token");
    uart_write_bytes(ctx->uart_port, token_gaveup, token_gaveup->len);

    return 0;
}

void do_state_talking_to_master(struct slave_ctx *ctx, TickType_t timeout)
{
    int err = 0;
    __cleanup_free__ struct silly_proto_header *incoming =
        calloc(1, sizeof(struct silly_proto_header));
    __cleanup_free__ struct silly_proto_header *out =
        calloc(1, sizeof(struct silly_proto_header));
    TimeOut_t do_timeout;
    TickType_t ticks_to_wait = timeout;
    BaseType_t timeout_result = 0;
    vTaskSetTimeOutState(&do_timeout);

    silly_uart_recv_pkt(ctx->uart_port, &incoming, ticks_to_wait);
    timeout_result = xTaskCheckForTimeOut(&do_timeout, &ticks_to_wait);

    if(timeout_result != pdTRUE) {

        ESP_LOGI(TAG, "got packet from master");
        silly_update_synced_until_id(&ctx->synced_until_id, incoming);
        st_talking_to_master_handle_incoming(
            ctx, incoming, &do_timeout, &ticks_to_wait, timeout);
    }

    if(ctx->master_synced_until_id == (ctx->last_pkt_id)) {
        err = silly_reset_header(&out);
        if(err) {
            return; // unexpected
        }
        err = st_talking_to_master_write_data_to_out(&out);
        if(err == ENODATA) {
            err = send_token_gaveup(ctx);
            if(err) {
                return; // this state is tried again
            }
            silly_reset_state_variables(
                &ctx->last_pkt_id, &ctx->synced_until_id);
            ctx->state = SLAVE_STATES_IDLE;
            ctx->master_synced_until_id = 0;
            return;
        } else if(err) {
            ESP_LOGI(TAG, "failed to write data to out: %s", strerror(err));
            return;
        }
        ESP_LOGI(TAG, "sending new data");
        out->flags = SILLY_DATA;
        out->synced_until_id = ctx->synced_until_id;
        ctx->last_pkt_id++;
        out->pkt_id = ctx->last_pkt_id;
        out->data_checksum = silly_compute_data_checksum(out);
        out->header_checksum = silly_compute_header_checksum(out);
        __builtin_dump_struct(out, printf);

        uart_write_bytes(ctx->uart_port, out, out->len);
        update_prev_sent(ctx, &out);
    } else {
        ESP_LOGI(TAG, "master is not ready to get new data");
        retransmit_old_pkt(ctx);
        __builtin_dump_struct(ctx, printf);
    }
}
