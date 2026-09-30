/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "master.h"
#include "common_utils.h"
#include "shared_uart_setup.h"
#include "silly_proto.h"
#include "uart_utils.h"
#include <errno.h>
#include <esp_log.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "master:"

void (*state_handlers[])(struct master_ctx *, TickType_t) = {
    [_MASTER_STATES_UNSPEC] = NULL,
    [MASTER_STATES_IDLE] = do_state_idle,
    [MASTER_STATES_TOKEN_SENT] = do_state_token_sent,
    [MASTER_STATES_TALKING_TO_SLAVE] = do_state_talking_to_slave,
    [_MASTER_STATES_SIZE] = NULL};

void update_current_slave(struct master_ctx *ctx)
{
    if(ctx->current_slave == CONFIG_SLAVES_NUM)
        ctx->current_slave = 1;
    else
        ctx->current_slave++;
}

void do_state_idle(struct master_ctx *ctx, TickType_t _)
{
    update_current_slave(ctx);
    __cleanup_free__ struct silly_proto_header *token =
        calloc(1, sizeof(struct silly_proto_header));
    int err = 0;

    if(!token) {
        ESP_LOGI(TAG, "failed to allocate memory for token");
        return;
    }

    err = silly_reset_header(&token);
    if(err)
        return;

    token->flags = SILLY_USE_TOKEN;
    token->synced_until_id = ctx->synced_until_id;
    ctx->last_pkt_id++;
    token->pkt_id = ctx->last_pkt_id;
    token->dst_slave_id = ctx->current_slave;
    token->header_checksum = silly_compute_header_checksum(token);

    // here we first use the pkt_id and then increment it because the first one
    // is not used yet
    ctx->token_pkt_id = token->pkt_id;

    uart_write_bytes(ctx->uart_port, token, token->len);

    ctx->state = MASTER_STATES_TOKEN_SENT;
    ESP_LOGI(TAG, "sent a token");
}

void send_token_revoked(struct master_ctx *ctx)
{
    __cleanup_free__ struct silly_proto_header *token_revoke =
        calloc(1, sizeof(struct silly_proto_header));
    int err = 0;

    if(!token_revoke) {
        ESP_LOGI(TAG, "failed to allocate memory for token_revoke");
        return;
    }

    err = silly_reset_header(&token_revoke);
    if(err)
        return; // unexpected

    ESP_LOGI(TAG, "sending a token revocation");

    token_revoke->synced_until_id = ctx->synced_until_id;
    token_revoke->flags = SILLY_TOKEN_REVOKED;
    token_revoke->dst_slave_id = ctx->current_slave;
    token_revoke->header_checksum = silly_compute_header_checksum(token_revoke);

    uart_write_bytes(ctx->uart_port, token_revoke, token_revoke->len);
}

void do_state_token_sent(struct master_ctx *ctx, TickType_t timeout)
{
    int err = 0;
    __cleanup_free__ struct silly_proto_header *incoming =
        calloc(1, sizeof(struct silly_proto_header));

    if(!incoming) {
        ESP_LOGI(TAG, "failed to allocate memory for incoming");
        return;
    }

    err = silly_uart_recv_pkt(ctx->uart_port, &incoming, timeout);

    if(err == ETIMEDOUT) {

        send_token_revoked(ctx);
        silly_reset_state_variables(
            &(ctx->last_pkt_id), &(ctx->synced_until_id));
        ctx->state = MASTER_STATES_IDLE;

    } else if(err == EBADMSG) {

        // do nothing about it
        return;

    } else if(incoming->flags & SILLY_GAVEUP_TOKEN) {

        send_token_revoked(ctx);
        silly_reset_state_variables(
            &(ctx->last_pkt_id), &(ctx->synced_until_id));
        ctx->state = MASTER_STATES_IDLE;

    } else if(
        (incoming->flags & (SILLY_USE_TOKEN | SILLY_ACK)) &&
        incoming->synced_until_id == ctx->token_pkt_id) {

        silly_update_synced_until_id(&ctx->synced_until_id, incoming);
        ESP_LOGI(TAG, "got ack for token");
        ctx->state = MASTER_STATES_TALKING_TO_SLAVE;

    } else if(incoming->synced_until_id >= ctx->token_pkt_id) {

        ESP_LOGI(
            TAG,
            "didn't get ack for token but seeing traffic from slave, "
            "considering it as ack");
        ctx->synced_until_id =
            1; // we need the slave to retransmit so we don't miss anything
        ctx->state = MASTER_STATES_TALKING_TO_SLAVE;
    }
}

void process_data(struct silly_proto_header *incoming)
{
    ESP_LOGI(TAG, "received new data:");
    ESP_LOG_BUFFER_HEX_LEVEL(
        TAG,
        incoming->data,
        incoming->len - sizeof(struct silly_proto_header),
        ESP_LOG_INFO);
}

void do_state_talking_to_slave(struct master_ctx *ctx, TickType_t timeout)
{
    int err = 0;
    __cleanup_free__ struct silly_proto_header *incoming =
        calloc(1, sizeof(struct silly_proto_header));
    __cleanup_free__ struct silly_proto_header *ack = NULL;
    TimeOut_t do_timeout;
    TickType_t ticks_to_wait = timeout;
    BaseType_t timeout_result = 0;
    vTaskSetTimeOutState(&do_timeout);

    if(!incoming) {
        ESP_LOGI(TAG, "failed to allocate memory for incoming");
        return;
    }

    err = silly_uart_recv_pkt(ctx->uart_port, &incoming, ticks_to_wait);
    timeout_result = xTaskCheckForTimeOut(&do_timeout, &ticks_to_wait);

    if(err == ETIMEDOUT) {

        ESP_LOGI(TAG, "timed out");
        send_token_revoked(ctx);
        silly_reset_state_variables(
            &(ctx->last_pkt_id), &(ctx->synced_until_id));
        ctx->state = MASTER_STATES_IDLE;

    } else if(err == EBADMSG) {

        return;
        // do nothing on corrupted packet

    } else if(incoming->flags & SILLY_GAVEUP_TOKEN) {

        ESP_LOGI(TAG, "slave gave up the token");
        send_token_revoked(ctx);
        silly_reset_state_variables(
            &(ctx->last_pkt_id), &(ctx->synced_until_id));
        ctx->state = MASTER_STATES_IDLE;

    } else if(incoming->flags & SILLY_DATA) {

        ack = calloc(1, sizeof(struct silly_proto_header));
        if(!ack) {
            ESP_LOGI(TAG, "failed to allocate memory for ack");
            return;
        }

        err = silly_reset_header(&ack);
        if(err)
            return; // unexpected

        if(incoming->pkt_id == ctx->synced_until_id + 1) {
            process_data(incoming);
            ctx->synced_until_id = incoming->pkt_id;
        }

        ESP_LOGI(TAG, "acking the latest processed data");
        ack->synced_until_id = ctx->synced_until_id;
        ack->flags = (SILLY_ACK | SILLY_DATA);
        ack->header_checksum = silly_compute_header_checksum(ack);

        __builtin_dump_struct(ack, printf);

        uart_write_bytes(ctx->uart_port, ack, ack->len);

    } else if(incoming->flags & SILLY_KEEP_ALIVE) {

        ESP_LOGI(TAG, "got keep alive, resetting the timeout");
        vTaskSetTimeOutState(&do_timeout);
        ticks_to_wait = timeout;
    }
}
