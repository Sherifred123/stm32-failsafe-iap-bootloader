/**
 * @file crc32.c
 * @brief STM32 Hardware-Compatible CRC-32 Engine Implementation
 *
 * Implements standard IEEE 802.3 polynomial (0x04C11DB7) matching
 * the STM32F4 hardware CRC peripheral accelerator (CRC->DR).
 * Supports arbitrary streaming chunk sizes with internal byte buffering.
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include "crc32.h"
#include <stdbool.h>
#include <string.h>

static uint32_t s_running_crc = STM32_CRC_INIT_VALUE;
static uint8_t  s_rem_buf[4];
static uint8_t  s_rem_len = 0U;

void crc32_init(void)
{
    s_running_crc = STM32_CRC_INIT_VALUE;
    s_rem_len = 0U;
}

void crc32_reset(void)
{
    s_running_crc = STM32_CRC_INIT_VALUE;
    s_rem_len = 0U;
}

static uint32_t process_word(uint32_t crc, uint32_t word)
{
    crc ^= word;
    for (uint8_t bit = 0; bit < 32U; bit++) {
        if (crc & 0x80000000UL) {
            crc = (crc << 1) ^ STM32_CRC_POLYNOMIAL;
        } else {
            crc = (crc << 1);
        }
    }
    return crc;
}

uint32_t crc32_update(const uint8_t *data, size_t length)
{
    /* If data is NULL or length is 0, flush any unaligned trailing bytes */
    if (data == NULL || length == 0U) {
        if (s_rem_len > 0U) {
            uint32_t trailing_word = 0U;
            for (uint8_t r = 0; r < s_rem_len; r++) {
                trailing_word |= ((uint32_t)s_rem_buf[r]) << (r * 8U);
            }
            s_running_crc = process_word(s_running_crc, trailing_word);
            s_rem_len = 0U;
        }
        return s_running_crc;
    }

    size_t in_idx = 0U;

    /* 1. Complete any partial 32-bit word buffered from previous call */
    while (s_rem_len > 0U && s_rem_len < 4U && in_idx < length) {
        s_rem_buf[s_rem_len++] = data[in_idx++];
        if (s_rem_len == 4U) {
            uint32_t word = ((uint32_t)s_rem_buf[0]) |
                            (((uint32_t)s_rem_buf[1]) << 8U) |
                            (((uint32_t)s_rem_buf[2]) << 16U) |
                            (((uint32_t)s_rem_buf[3]) << 24U);
            s_running_crc = process_word(s_running_crc, word);
            s_rem_len = 0U;
        }
    }

    /* 2. Process bulk 32-bit words */
    size_t remaining_bytes = length - in_idx;
    size_t full_words = remaining_bytes / 4U;

    for (size_t i = 0; i < full_words; i++) {
        size_t offset = in_idx + (i * 4U);
        uint32_t word = ((uint32_t)data[offset]) |
                        (((uint32_t)data[offset + 1U]) << 8U) |
                        (((uint32_t)data[offset + 2U]) << 16U) |
                        (((uint32_t)data[offset + 3U]) << 24U);
        s_running_crc = process_word(s_running_crc, word);
    }
    in_idx += (full_words * 4U);

    /* 3. Buffer remaining trailing bytes (1 to 3 bytes) for subsequent update or flush */
    while (in_idx < length) {
        s_rem_buf[s_rem_len++] = data[in_idx++];
    }

    return s_running_crc;
}

uint32_t crc32_calculate(const uint8_t *data, size_t length)
{
    crc32_reset();
    crc32_update(data, length);
    return crc32_update(NULL, 0U); /* Flushes remainder */
}
