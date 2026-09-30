#pragma once

#include "common_utils.h"
#include <driver/uart.h>
#include <inttypes.h>

enum SILLY_PROTO_FLAGS {
    _SILLY_PROTO_FLAGS_UNSPEC = 0,
    SILLY_USE_TOKEN =
        1 << 0, /** used by master: master sending the token to a slave */
    SILLY_GAVEUP_TOKEN =
        1 << 1, /** used by slave: i don't want the token anymore, required by
                   the slave to send when the token is offered but the slave
                   doesn't plan on using it */
    SILLY_TOKEN_REVOKED =
        1 << 2, /** used by master: master stating the token is revoked */
    SILLY_ACK = 1 << 3,  /** ack of a command, should be ORed with the cmd that
                            is being  acked, also synced_until_id must be set */
    SILLY_DATA = 1 << 4, /** packet contains data */
    SILLY_KEEP_ALIVE =
        1 << 5, /** used by the slave: im still doing stuff, not dead! (holding
                   the token) the slave needs to send this every TOKEN_LIFETIME
                   - 1 seconds to tell the master to not revoke the token */
    _SILLY_PROTO_FLAGS_SIZE = 1 << 6,
};

#define SILLY_MAGIC 0x22aa

typedef uint16_t silly_magic_t;
typedef uint32_t silly_len_t;
typedef uint32_t silly_flags_t;
typedef uint32_t silly_checksum_t;
typedef uint16_t silly_pkt_id_t;
typedef uint8_t silly_slave_id_t;

struct silly_proto_header {
    silly_magic_t magic;
    silly_len_t len;     /** len of the header+data */
    silly_flags_t flags; /** flags, of SILLY_PROTO_FLAGS */
    silly_checksum_t header_checksum;
    silly_checksum_t data_checksum;
    silly_pkt_id_t synced_until_id; /** this packet is a response to the packet
                                       id of the opposite peer */
    silly_pkt_id_t
        pkt_id; /** pkt_id of the host, each device maintains its own */
    silly_slave_id_t dst_slave_id; /** 0 for master, 1..31 for slaves */
    char data[];
} __attribute__((packed));

#define SILLY_CHECKSUM_VERIFY_SUCCESS 0
#define SILLY_CHECKSUM_VERIFY_FAILED  1

void silly_reset_state_variables(
    silly_pkt_id_t *pkt_id, silly_pkt_id_t *synced_until_id);

int silly_verify_data_checksum(struct silly_proto_header *const hdr);
silly_checksum_t
silly_compute_data_checksum(struct silly_proto_header *const hdr);

int silly_verify_header_checksum(struct silly_proto_header *const hdr);
silly_checksum_t
silly_compute_header_checksum(struct silly_proto_header *const hdr);

int silly_verify_checksum(struct silly_proto_header *const hdr);

int silly_uart_recv_pkt(
    uart_port_t uart_port, struct silly_proto_header **hdr, TickType_t timeout);

int silly_reset_header(struct silly_proto_header **hdr);

int silly_send_until_acked(
    uart_port_t uart_port,
    struct silly_proto_header *pkt,
    TickType_t timeout,
    TickType_t intervals);

int silly_update_synced_until_id(
    silly_pkt_id_t *synced_until_id, struct silly_proto_header *incoming);
