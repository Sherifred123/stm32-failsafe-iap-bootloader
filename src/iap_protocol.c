/**
 * @file iap_protocol.c
 * @brief In-Application Programming (IAP) Serial Transport Implementation
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include "iap_protocol.h"
#include "bootloader_core.h"
#include "flash_driver.h"
#include "crc32.h"
#include <string.h>

typedef enum {
    PARSE_WAIT_SOF,
    PARSE_READ_CMD,
    PARSE_READ_LEN_H,
    PARSE_READ_LEN_L,
    PARSE_READ_SEQ,
    PARSE_READ_PAYLOAD,
    PARSE_READ_CRC,
    PARSE_READ_EOF
} parse_state_t;

static parse_state_t s_state = PARSE_WAIT_SOF;
static iap_packet_t  s_rx_pkt;
static uint16_t      s_rx_payload_idx = 0U;
static uint8_t       s_rx_crc_bytes[4];
static uint8_t       s_rx_crc_idx = 0U;

bool iap_protocol_feed_byte(uint8_t byte, iap_packet_t *packet)
{
    if (packet == NULL) {
        return false;
    }

    switch (s_state) {
        case PARSE_WAIT_SOF:
            if (byte == IAP_SOF_BYTE) {
                memset(&s_rx_pkt, 0, sizeof(iap_packet_t));
                s_state = PARSE_READ_CMD;
            }
            break;

        case PARSE_READ_CMD:
            s_rx_pkt.cmd = byte;
            s_state = PARSE_READ_LEN_H;
            break;

        case PARSE_READ_LEN_H:
            s_rx_pkt.length = (uint16_t)((uint16_t)byte << 8U);
            s_state = PARSE_READ_LEN_L;
            break;

        case PARSE_READ_LEN_L:
            s_rx_pkt.length |= (uint16_t)byte;
            if (s_rx_pkt.length > IAP_MAX_PAYLOAD_SIZE) {
                s_state = PARSE_WAIT_SOF;
            } else {
                s_state = PARSE_READ_SEQ;
            }
            break;

        case PARSE_READ_SEQ:
            s_rx_pkt.seq = byte;
            s_rx_payload_idx = 0U;
            if (s_rx_pkt.length > 0U) {
                s_state = PARSE_READ_PAYLOAD;
            } else {
                s_rx_crc_idx = 0U;
                s_state = PARSE_READ_CRC;
            }
            break;

        case PARSE_READ_PAYLOAD:
            s_rx_pkt.payload[s_rx_payload_idx++] = byte;
            if (s_rx_payload_idx >= s_rx_pkt.length) {
                s_rx_crc_idx = 0U;
                s_state = PARSE_READ_CRC;
            }
            break;

        case PARSE_READ_CRC:
            s_rx_crc_bytes[s_rx_crc_idx++] = byte;
            if (s_rx_crc_idx >= 4U) {
                s_rx_pkt.crc32 = ((uint32_t)s_rx_crc_bytes[0]) |
                                 (((uint32_t)s_rx_crc_bytes[1]) << 8U) |
                                 (((uint32_t)s_rx_crc_bytes[2]) << 16U) |
                                 (((uint32_t)s_rx_crc_bytes[3]) << 24U);
                s_state = PARSE_READ_EOF;
            }
            break;

        case PARSE_READ_EOF:
            s_state = PARSE_WAIT_SOF;
            if (byte == IAP_EOF_BYTE) {
                /* Verify Frame Checksum (CMD + LEN_H + LEN_L + SEQ + PAYLOAD) */
                uint8_t hdr[4] = {
                    s_rx_pkt.cmd,
                    (uint8_t)(s_rx_pkt.length >> 8U),
                    (uint8_t)(s_rx_pkt.length & 0xFFU),
                    s_rx_pkt.seq
                };
                crc32_reset();
                crc32_update(hdr, 4U);
                if (s_rx_pkt.length > 0U) {
                    crc32_update(s_rx_pkt.payload, s_rx_pkt.length);
                }
                uint32_t expected = crc32_update(NULL, 0U);

                if (expected == s_rx_pkt.crc32) {
                    *packet = s_rx_pkt;
                    return true;
                }
            }
            break;

        default:
            s_state = PARSE_WAIT_SOF;
            break;
    }

    return false;
}

boot_status_t iap_protocol_handle_request(const iap_packet_t *req, iap_response_t *resp)
{
    if (req == NULL || resp == NULL) {
        return BOOT_ERR_PARAM;
    }

    resp->seq = req->seq;
    resp->length = 0U;

    switch (req->cmd) {
        case IAP_CMD_PING: {
            resp->status_code = IAP_RESP_ACK;
            /* Payload: MCU Target ID (0x0446 = STM32F446), Version (1.0.0), Active Slot */
            const boot_metadata_t *m = bootloader_get_metadata();
            resp->payload[0] = 0x04U; /* STM32F4 Family */
            resp->payload[1] = 0x46U; /* 46 Device */
            resp->payload[2] = 1U;    /* Bootloader Major */
            resp->payload[3] = 0U;    /* Bootloader Minor */
            resp->payload[4] = 0U;    /* Bootloader Patch */
            resp->payload[5] = m ? m->active_slot : 0U;
            resp->length = 6U;
            return BOOT_OK;
        }

        case IAP_CMD_GET_METADATA: {
            resp->status_code = IAP_RESP_ACK;
            const boot_metadata_t *m = bootloader_get_metadata();
            if (m != NULL) {
                memcpy(resp->payload, m, sizeof(boot_metadata_t));
                resp->length = (uint16_t)sizeof(boot_metadata_t);
            }
            return BOOT_OK;
        }

        case IAP_CMD_PREPARE_SLOT: {
            if (req->length < 1U) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }
            uint8_t slot_idx = req->payload[0];
            if (slot_idx >= NUM_SLOTS) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }

            boot_status_t status = flash_unlock();
            if (status != BOOT_OK) {
                resp->status_code = IAP_RESP_NACK;
                return status;
            }

            uint8_t start_sec = (slot_idx == SLOT_INDEX_A) ? SLOT_A_SECTOR_START : SLOT_B_SECTOR_START;
            uint8_t end_sec = (slot_idx == SLOT_INDEX_A) ? SLOT_A_SECTOR_END : SLOT_B_SECTOR_END;

            for (uint8_t s = start_sec; s <= end_sec; s++) {
                status = flash_erase_sector(s);
                if (status != BOOT_OK) {
                    flash_lock();
                    resp->status_code = IAP_RESP_NACK;
                    return status;
                }
            }

            flash_lock();
            resp->status_code = IAP_RESP_ACK;
            return BOOT_OK;
        }

        case IAP_CMD_WRITE_CHUNK: {
            /* Payload layout: [Addr: 4 bytes] [Data: N bytes] */
            if (req->length < 5U) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }
            uint32_t dest_addr = ((uint32_t)req->payload[0]) |
                                 (((uint32_t)req->payload[1]) << 8U) |
                                 (((uint32_t)req->payload[2]) << 16U) |
                                 (((uint32_t)req->payload[3]) << 24U);
            size_t data_len = req->length - 4U;

            boot_status_t status = flash_unlock();
            if (status != BOOT_OK) {
                resp->status_code = IAP_RESP_NACK;
                return status;
            }

            status = flash_write(dest_addr, &req->payload[4], data_len);
            flash_lock();

            if (status == BOOT_OK) {
                resp->status_code = IAP_RESP_ACK;
            } else {
                resp->status_code = IAP_RESP_NACK;
            }
            return status;
        }

        case IAP_CMD_VERIFY_SLOT: {
            if (req->length < 1U) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }
            uint8_t slot_idx = req->payload[0];
            boot_status_t status = bootloader_verify_slot(slot_idx);
            if (status == BOOT_OK) {
                resp->status_code = IAP_RESP_ACK;
            } else {
                resp->status_code = IAP_RESP_NACK;
                resp->payload[0] = (uint8_t)(-status);
                resp->length = 1U;
            }
            return status;
        }

        case IAP_CMD_ACTIVATE_SLOT: {
            if (req->length < 2U) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }
            uint8_t slot_idx = req->payload[0];
            uint8_t probation = req->payload[1];

            if (slot_idx >= NUM_SLOTS) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }

            boot_metadata_t meta = *bootloader_get_metadata();
            meta.active_slot = slot_idx;
            meta.slot_state[slot_idx] = (probation != 0U) ? SLOT_STATE_TESTING : SLOT_STATE_CONFIRMED;
            meta.boot_attempts = 0U;

            boot_status_t status = bootloader_save_metadata(&meta);
            resp->status_code = (status == BOOT_OK) ? IAP_RESP_ACK : IAP_RESP_NACK;
            return status;
        }

        case IAP_CMD_TRIGGER_JUMP: {
            if (req->length < 1U) {
                resp->status_code = IAP_RESP_NACK;
                return BOOT_ERR_PARAM;
            }
            uint8_t slot_idx = req->payload[0];
            resp->status_code = IAP_RESP_ACK;

            /* In hardware, this initiates execution transfer to application */
            bootloader_launch_application(slot_idx);
            return BOOT_OK;
        }

        default:
            resp->status_code = IAP_RESP_NACK;
            return BOOT_ERR_PARAM;
    }
}

size_t iap_protocol_serialize_response(const iap_response_t *resp, uint8_t *out_buf, size_t max_buf)
{
    if (resp == NULL || out_buf == NULL) {
        return 0U;
    }

    size_t required = IAP_HEADER_OVERHEAD + resp->length + IAP_TRAILER_OVERHEAD;
    if (max_buf < required) {
        return 0U;
    }

    out_buf[0] = IAP_SOF_BYTE;
    out_buf[1] = resp->status_code;
    out_buf[2] = (uint8_t)(resp->length >> 8U);
    out_buf[3] = (uint8_t)(resp->length & 0xFFU);
    out_buf[4] = resp->seq;

    if (resp->length > 0U) {
        memcpy(&out_buf[5], resp->payload, resp->length);
    }

    /* Compute Response CRC-32 */
    crc32_reset();
    crc32_update(&out_buf[1], 4U + resp->length);
    uint32_t crc = crc32_update(NULL, 0U);

    size_t trailer_idx = 5U + resp->length;
    out_buf[trailer_idx]     = (uint8_t)(crc & 0xFFU);
    out_buf[trailer_idx + 1] = (uint8_t)((crc >> 8U) & 0xFFU);
    out_buf[trailer_idx + 2] = (uint8_t)((crc >> 16U) & 0xFFU);
    out_buf[trailer_idx + 3] = (uint8_t)((crc >> 24U) & 0xFFU);
    out_buf[trailer_idx + 4] = IAP_EOF_BYTE;

    return required;
}
