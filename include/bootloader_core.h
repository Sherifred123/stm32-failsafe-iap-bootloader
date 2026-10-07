/**
 * @file bootloader_core.h
 * @brief Core Boot Management, Integrity Verification & Rollback Engine
 *
 * Implements boot decision logic, image header inspection, hardware CRC verification,
 * stack pointer validation, vector table relocation, and watchdog rollback protection.
 *
 * Target: STM32F446RE (ARM Cortex-M4 @ 180 MHz)
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#ifndef BOOTLOADER_CORE_H
#define BOOTLOADER_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include "bootloader_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*jump_hook_fn)(uint32_t target_vtor, uint32_t msp, uint32_t reset_vector);

/**
 * @brief Initializes bootloader subsystems and loads metadata record.
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t bootloader_core_init(void);

/**
 * @brief Retrieves a read-only pointer to active boot metadata.
 */
const boot_metadata_t *bootloader_get_metadata(void);

/**
 * @brief Writes updated boot metadata into persistent flash (Sector 2).
 *
 * @param meta Pointer to updated metadata structure
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t bootloader_save_metadata(const boot_metadata_t *meta);

/**
 * @brief Verifies cryptographic header, hardware CRC-32, and vector table of a slot.
 *
 * @param slot_idx Slot index (0 for Slot A, 1 for Slot B)
 * @return BOOT_OK if image is completely intact, negative error code otherwise
 */
boot_status_t bootloader_verify_slot(uint8_t slot_idx);

/**
 * @brief Executes boot selection policy, handling rollback logic and probation boots.
 *
 * Resolves which slot should execute, increments probation attempts if untested,
 * and falls back to alternate slot if corrupted or timed out.
 *
 * @param selected_slot Pointer to receive resolved slot index
 * @return BOOT_OK on success, negative error code if all slots invalid
 */
boot_status_t bootloader_resolve_boot_target(uint8_t *selected_slot);

/**
 * @brief Confirms operational stability of newly updated firmware.
 * Called by running application over IPC / shared RAM flag to finalize update.
 *
 * @param slot_idx Slot index to mark confirmed
 * @return BOOT_OK on success, negative error code otherwise
 */
boot_status_t bootloader_confirm_application(uint8_t slot_idx);

/**
 * @brief Registers host simulation jump hook for automated unit tests.
 */
void bootloader_set_jump_hook(jump_hook_fn hook);

/**
 * @brief De-initializes peripherals, sets VTOR, relocates MSP, and jumps to application.
 *
 * @param slot_idx Slot index to launch
 * @return Does not return on hardware; returns BOOT_OK in host simulation
 */
boot_status_t bootloader_launch_application(uint8_t slot_idx);

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_CORE_H */
