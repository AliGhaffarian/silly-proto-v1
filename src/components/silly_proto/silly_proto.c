#include "include/silly_proto.h"
#include "common_utils.h"
#include "driver/uart.h"
#include "errno.h"
#include "esp_log.h"
#include "uart_utils.h"

static const char *TAG = "silly_proto";

void silly_reset_state_variables(
    silly_pkt_id_t *pkt_id, silly_pkt_id_t *synced_until_id)
{
    *pkt_id = 0;
    *synced_until_id = 0;
}

int silly_verify_data_checksum(struct silly_proto_header *const hdr)
{
    if(hdr->data_checksum == silly_compute_data_checksum(hdr))
        return SILLY_CHECKSUM_VERIFY_SUCCESS;
    else
        return SILLY_CHECKSUM_VERIFY_FAILED;
}

int silly_verify_header_checksum(struct silly_proto_header *const hdr)
{
    if(hdr->header_checksum == silly_compute_header_checksum(hdr))
        return SILLY_CHECKSUM_VERIFY_SUCCESS;
    else
        return SILLY_CHECKSUM_VERIFY_FAILED;
}

int silly_verify_checksum(struct silly_proto_header *const hdr)
{
    int err = 0;

    err = silly_verify_header_checksum(hdr);
    if(err != SILLY_CHECKSUM_VERIFY_SUCCESS)
        return err;

    if(hdr->flags & SILLY_DATA) {
        err = silly_verify_data_checksum(hdr);
    }

    return err;
}

int silly_skip_until_magic(uart_port_t uart_port, TickType_t timeout)
{
    ESP_LOGI(TAG, "skipping until magic");
    size_t magic_arr_size = sizeof(((struct silly_proto_header *)NULL)->magic);
    uint8_t magic_byte;
    size_t i = 0;
    TimeOut_t vtask_timeout;
    TickType_t ticks_to_wait = timeout;
    vTaskSetTimeOutState(&vtask_timeout);

    while(i != magic_arr_size) {
        uart_read_bytes(uart_port, &magic_byte, 1, ticks_to_wait);

        if(xTaskCheckForTimeOut(&vtask_timeout, &ticks_to_wait) == pdTRUE)
            return ETIMEDOUT;

        if(magic_byte == GET_NTH_BYTE(SILLY_MAGIC, i)) {
            i++;
        } else if(magic_byte == GET_NTH_BYTE(SILLY_MAGIC, 0)) {
            i = 1;
        } else {
            i = 0;
        }
    }

    ESP_LOGI(TAG, "successfully skipped until magic");
    return 0;
}

int silly_reset_header(struct silly_proto_header **hdr)
{
    int err = 0;

    ESP_LOGD(TAG, "resetting the header");

    err = safe_realloc((void **)hdr, sizeof(struct silly_proto_header));

    if(err)
        return err;

    memset(*hdr, 0, sizeof(struct silly_proto_header));

    (*hdr)->magic = SILLY_MAGIC;
    (*hdr)->len = sizeof(struct silly_proto_header);

    return 0;
}

/**
 * @brief recv a silly packet, skip until magic
 *
 * @return 0 on success, EBADMSG on corrupted packet, ETIMEDOUT on timeout
 */
int silly_uart_recv_pkt(
    uart_port_t uart_port, struct silly_proto_header **hdr, TickType_t timeout)
{
    int err = 0;
    TimeOut_t vtask_timeout;
    TickType_t ticks_to_wait = timeout;
    vTaskSetTimeOutState(&vtask_timeout);

    ESP_LOGI(TAG, "trying to recv a packet");

    silly_skip_until_magic(uart_port, ticks_to_wait);

    if(xTaskCheckForTimeOut(&vtask_timeout, &ticks_to_wait) == pdTRUE)
        return ETIMEDOUT;

    (*hdr)->magic = SILLY_MAGIC;

    uart_read_bytes(
        uart_port,
        ((void *)(*hdr)) + sizeof((*hdr)->magic),
        sizeof(struct silly_proto_header) - sizeof((*hdr)->magic),
        ticks_to_wait);

    err = silly_verify_header_checksum(*hdr);
    if(err != SILLY_CHECKSUM_VERIFY_SUCCESS) {
        ESP_LOGI(
            TAG,
            "corrupted header, in header:%d, computed:%d",
            (*hdr)->header_checksum,
            silly_compute_header_checksum(*hdr));
        return EBADMSG;
    }

    __builtin_dump_struct(*hdr, printf);

    ESP_LOGI(TAG, "successfully read the header");
    if((*hdr)->len == sizeof(struct silly_proto_header)) {
        ESP_LOGI(TAG, "packet has no data, recv done");
        return 0;
    }

    ESP_LOGI(TAG, "trying to read the data");
    // if we haven't returned yet, we got data to process
    err = safe_realloc((void **)hdr, (*hdr)->len);
    if(err)
        return err;

    if(xTaskCheckForTimeOut(&vtask_timeout, &ticks_to_wait) == pdTRUE)
        return ETIMEDOUT;

    uart_read_bytes(
        uart_port,
        (*hdr)->data,
        (*hdr)->len - sizeof(struct silly_proto_header),
        ticks_to_wait);

    err = silly_verify_data_checksum(*hdr);

    if(err != SILLY_CHECKSUM_VERIFY_SUCCESS) {
        ESP_LOGI(
            TAG,
            "corrupted data, in header:%d, computed:%d",
            (*hdr)->data_checksum,
            silly_compute_data_checksum(*hdr));
        return EBADMSG;
    }
    ESP_LOGI(TAG, "packet recv done");
    return 0;
}

int silly_update_synced_until_id(
    silly_pkt_id_t *synced_until_id, struct silly_proto_header *incoming)
{
    if(*synced_until_id + 1 == incoming->pkt_id) {
        ESP_LOGI(TAG, "got an edge pkt_id");
        (*synced_until_id)++;
        return 1;
    } else {
        ESP_LOGI(TAG, "got an old pkt_id");
        return 0;
    }
}

silly_checksum_t
silly_compute_data_checksum(struct silly_proto_header *const hdr)
{
    silly_checksum_t checksum = 0;

    for(int i = 0; i < hdr->len - sizeof(struct silly_proto_header); i++)
        checksum += hdr->data[i];

    return checksum;
}

silly_checksum_t
silly_compute_header_checksum(struct silly_proto_header *const hdr)
{
    silly_checksum_t checksum = 0;
    checksum += hdr->magic;
    checksum += hdr->len;
    checksum += hdr->flags;
    checksum += hdr->data_checksum;
    checksum += hdr->synced_until_id;
    checksum += hdr->pkt_id;
    checksum += hdr->dst_slave_id;
    return checksum;
}
