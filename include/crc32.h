/**
 * @file crc32.h
 * @brief STM32 Hardware-Compatible CRC-32 Verification Engine
 *
 * Implements standard IEEE 802.3 polynomial (0x04C11DB7) matching
 * the STM32F4 hardware CRC peripheral accelerator (CRC->DR).
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Standard STM32 Hardware CRC Polynomial: X^32 + X^26 + ... + 1 */
#define STM32_CRC_POLYNOMIAL            (0x04C11DB7UL)
#define STM32_CRC_INIT_VALUE            (0xFFFFFFFFUL)

/**
 * @brief Initializes the CRC engine (hardware or software lookup table).
 */
void crc32_init(void);

/**
 * @brief Resets the CRC accumulator to 0xFFFFFFFF.
 */
void crc32_reset(void);

/**
 * @brief Accumulates data buffer into running CRC-32 calculation.
 *
 * Fully compatible with STM32F4 hardware CRC unit:
 * processes 32-bit words, zero-pads trailing bytes if not word-aligned.
 *
 * @param data Pointer to input byte stream
 * @param length Length of data in bytes
 * @return Current running CRC-32 checksum
 */
uint32_t crc32_update(const uint8_t *data, size_t length);

/**
 * @brief Computes one-shot CRC-32 checksum over a memory buffer.
 *
 * @param data Pointer to buffer
 * @param length Length in bytes
 * @return Computed CRC-32 checksum
 */
uint32_t crc32_calculate(const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */

/* Streaming unaligned remainder buffer validated for non-word-aligned UART chunk frames */
