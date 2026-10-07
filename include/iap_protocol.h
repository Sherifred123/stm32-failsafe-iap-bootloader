/**
 * @file iap_protocol.h
 * @brief In-Application Programming (IAP) Serial Transport Protocol
 *
 * Binary packet protocol for reliable over-the-wire flashing via USART/VCP.
 * Features frame synchronization, chunked flash programming, and CRC verification.
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#ifndef IAP_PROTOCOL_H
#define IAP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bootloader_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IAP_SOF_BYTE                    (0xAAU)
#define IAP_EOF_BYTE                    (0x55U)
#define IAP_MAX_PAYLOAD_SIZE            (256U)
#define IAP_HEADER_OVERHEAD             (5U)  /* SOF + CMD + LEN_H + LEN_L + SEQ */
#define IAP_TRAILER_OVERHEAD            (5U)  /* CRC32 (4) + EOF (1) */
#define IAP_MAX_FRAME_SIZE              (IAP_HEADER_OVERHEAD + IAP_MAX_PAYLOAD_SIZE + IAP_TRAILER_OVERHEAD)

typedef enum {
    IAP_CMD_PING            = 0x01, /**< Health check & MCU identification */
    IAP_CMD_GET_METADATA    = 0x02, /**< Read boot partition table */
    IAP_CMD_PREPARE_SLOT    = 0x03, /**< Erase target slot sectors */
    IAP_CMD_WRITE_CHUNK     = 0x04, /**< Program binary block */
    IAP_CMD_VERIFY_SLOT     = 0x05, /**< Run on-chip hardware CRC check */
    IAP_CMD_ACTIVATE_SLOT   = 0x06, /**< Set slot active with probation state */
    IAP_CMD_TRIGGER_JUMP    = 0x07  /**< Reboot and jump to application */
} iap_cmd_t;

typedef enum {
    IAP_RESP_ACK            = 0x06, /**< Success acknowledgment */
    IAP_RESP_NACK           = 0x15  /**< Rejection / error code */
} iap_resp_code_t;

typedef struct {
    uint8_t  cmd;
    uint8_t  seq;
    uint16_t length;
    uint8_t  payload[IAP_MAX_PAYLOAD_SIZE];
    uint32_t crc32;
} iap_packet_t;

typedef struct {
    uint8_t  status_code;
    uint8_t  seq;
    uint16_t length;
    uint8_t  payload[IAP_MAX_PAYLOAD_SIZE];
} iap_response_t;

/**
 * @brief Parses raw serial byte stream into framed IAP packet.
 *
 * @param byte Incoming stream byte
 * @param packet Pointer to output packet structure if frame completed
 * @return true if complete valid frame received, false if buffering
 */
bool iap_protocol_feed_byte(uint8_t byte, iap_packet_t *packet);

/**
 * @brief Processes an incoming IAP command packet and executes bootloader action.
 *
 * @param req Incoming request packet
 * @param resp Output response packet to transmit back to host
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t iap_protocol_handle_request(const iap_packet_t *req, iap_response_t *resp);

/**
 * @brief Serializes response packet into byte stream for transmission.
 *
 * @param resp Response packet structure
 * @param out_buf Output serialized buffer
 * @param max_buf Maximum available buffer size
 * @return Number of serialized bytes written
 */
size_t iap_protocol_serialize_response(const iap_response_t *resp, uint8_t *out_buf, size_t max_buf);

#ifdef __cplusplus
}
#endif

#endif /* IAP_PROTOCOL_H */
