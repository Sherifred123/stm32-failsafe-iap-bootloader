/**
 * @file flash_driver.h
 * @brief STM32F446RE Flash Memory Controller Driver
 *
 * Implements unlocking/locking, sector-based erasing, word programming,
 * address validation, and simulated Flash memory backend for host unit tests.
 *
 * Target: STM32F446RE Internal NOR Flash (512 KB, 8 Sectors)
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#ifndef FLASH_DRIVER_H
#define FLASH_DRIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "bootloader_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* STM32F4 Flash Unlock Keys */
#define FLASH_KEY_1                     (0x45670123UL)
#define FLASH_KEY_2                     (0xCDEF89ABUL)

typedef struct {
    uint8_t  sector_num;
    uint32_t start_addr;
    uint32_t size_bytes;
} flash_sector_info_t;

/**
 * @brief Initializes Flash driver subsystem.
 */
void flash_init(void);

/**
 * @brief Unlocks Flash control registers to allow write/erase operations.
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t flash_unlock(void);

/**
 * @brief Locks Flash control registers to protect against unintended writes.
 */
void flash_lock(void);

/**
 * @brief Checks if Flash controller is currently unlocked.
 */
bool flash_is_unlocked(void);

/**
 * @brief Erases a specific flash sector by sector index (0 to 7).
 *
 * @param sector Sector index (0 to 7)
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t flash_erase_sector(uint8_t sector);

/**
 * @brief Erases all sectors encompassed by an address range.
 *
 * @param start_addr Starting byte address
 * @param length Total length in bytes
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t flash_erase_range(uint32_t start_addr, size_t length);

/**
 * @brief Programs a stream of bytes into Flash memory.
 * Enforces NOR flash physical constraints (bits can only change 1 -> 0).
 *
 * @param address Destination flash address (must be word-aligned for hardware)
 * @param data Pointer to source data
 * @param length Number of bytes to program
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t flash_write(uint32_t address, const uint8_t *data, size_t length);

/**
 * @brief Reads data directly from Flash memory.
 *
 * @param address Source flash address
 * @param buffer Destination buffer
 * @param length Number of bytes to read
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t flash_read(uint32_t address, uint8_t *buffer, size_t length);

/**
 * @brief Resolves sector index for a given Flash address.
 *
 * @param address Absolute flash memory address
 * @return Sector index (0 to 7), or -1 if outside valid Flash boundaries
 */
int8_t flash_get_sector(uint32_t address);

/**
 * @brief Retrieves metadata information for a specific sector.
 *
 * @param sector Sector index (0 to 7)
 * @param info Pointer to output info structure
 * @return true if valid sector, false otherwise
 */
bool flash_get_sector_info(uint8_t sector, flash_sector_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_DRIVER_H */
