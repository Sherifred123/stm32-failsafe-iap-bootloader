/**
 * @file bootloader_core.c
 * @brief Core Boot Management, Integrity Verification & Rollback Implementation
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include "bootloader_core.h"
#include "flash_driver.h"
#include "crc32.h"
#include <string.h>

#if defined(STM32F446xx)
#include "stm32f4xx.h"
#endif

typedef struct {
    uint32_t start_addr;
    uint32_t max_size;
} slot_boundary_t;

static const slot_boundary_t s_slot_boundaries[NUM_SLOTS] = {
    { SLOT_A_START_ADDR, SLOT_A_MAX_SIZE }, /* Slot A: 0x08010000, 192 KB */
    { SLOT_B_START_ADDR, SLOT_B_MAX_SIZE }  /* Slot B: 0x08040000, 256 KB */
};

static boot_metadata_t s_metadata;
static jump_hook_fn s_jump_hook = NULL;

static uint32_t compute_metadata_crc(const boot_metadata_t *meta)
{
    size_t len_to_hash = offsetof(boot_metadata_t, crc32);
    return crc32_calculate((const uint8_t*)meta, len_to_hash);
}

static void init_default_metadata(boot_metadata_t *meta)
{
    memset(meta, 0, sizeof(boot_metadata_t));
    meta->magic = METADATA_MAGIC;
    meta->active_slot = SLOT_INDEX_A;
    meta->slot_state[SLOT_INDEX_A] = SLOT_STATE_CONFIRMED;
    meta->slot_state[SLOT_INDEX_B] = SLOT_STATE_EMPTY;
    meta->boot_attempts = 0U;
    meta->max_boot_attempts = DEFAULT_MAX_BOOT_ATTEMPTS;
    meta->rollback_count = 0U;
    meta->last_error_code = (uint32_t)BOOT_OK;
    meta->crc32 = compute_metadata_crc(meta);
}

boot_status_t bootloader_core_init(void)
{
    crc32_init();
    flash_init();

    /* Read metadata from non-volatile flash Sector 2 */
    boot_status_t status = flash_read(METADATA_FLASH_ADDR, (uint8_t*)&s_metadata, sizeof(boot_metadata_t));
    if (status != BOOT_OK) {
        init_default_metadata(&s_metadata);
        return bootloader_save_metadata(&s_metadata);
    }

    /* Validate magic and integrity */
    uint32_t expected_crc = compute_metadata_crc(&s_metadata);
    if (s_metadata.magic != METADATA_MAGIC || s_metadata.crc32 != expected_crc) {
        /* Unprogrammed or corrupt metadata: reinitialize defaults */
        init_default_metadata(&s_metadata);
        return bootloader_save_metadata(&s_metadata);
    }

    return BOOT_OK;
}

const boot_metadata_t *bootloader_get_metadata(void)
{
    return &s_metadata;
}

boot_status_t bootloader_save_metadata(const boot_metadata_t *meta)
{
    if (meta == NULL) {
        return BOOT_ERR_PARAM;
    }

    boot_metadata_t record = *meta;
    record.magic = METADATA_MAGIC;
    record.crc32 = compute_metadata_crc(&record);

    boot_status_t status = flash_unlock();
    if (status != BOOT_OK) {
        return status;
    }

    /* Sector 2 is dedicated to metadata storage */
    status = flash_erase_sector(METADATA_SECTOR);
    if (status != BOOT_OK) {
        flash_lock();
        return status;
    }

    status = flash_write(METADATA_FLASH_ADDR, (const uint8_t*)&record, sizeof(boot_metadata_t));
    flash_lock();

    if (status == BOOT_OK) {
        s_metadata = record;
    }
    return status;
}

boot_status_t bootloader_verify_slot(uint8_t slot_idx)
{
    if (slot_idx >= NUM_SLOTS) {
        return BOOT_ERR_PARAM;
    }

    uint32_t slot_base = s_slot_boundaries[slot_idx].start_addr;
    uint32_t max_size = s_slot_boundaries[slot_idx].max_size;

    /* 1. Read Image Header */
    image_header_t header;
    boot_status_t status = flash_read(slot_base, (uint8_t*)&header, sizeof(image_header_t));
    if (status != BOOT_OK) {
        return status;
    }

    /* 2. Validate Header Magic */
    if (header.magic != IMAGE_HEADER_MAGIC) {
        return BOOT_ERR_INVALID_MAGIC;
    }

    /* 3. Validate Header CRC-32 */
    size_t header_len = offsetof(image_header_t, header_crc32);
    uint32_t calc_hdr_crc = crc32_calculate((const uint8_t*)&header, header_len);
    if (calc_hdr_crc != header.header_crc32) {
        return BOOT_ERR_CRC_MISMATCH;
    }

    /* 4. Validate Size Boundaries */
    if (header.image_size == 0U || header.image_size > (max_size - APP_VECTOR_TABLE_OFFSET)) {
        return BOOT_ERR_PARAM;
    }

    /* 5. Compute Application Payload CRC-32 (Stream in 256-byte chunks) */
    crc32_reset();
    uint8_t chunk[256];
    size_t bytes_remaining = header.image_size;
    uint32_t read_ptr = slot_base + APP_VECTOR_TABLE_OFFSET;

    while (bytes_remaining > 0U) {
        size_t to_read = (bytes_remaining > sizeof(chunk)) ? sizeof(chunk) : bytes_remaining;
        status = flash_read(read_ptr, chunk, to_read);
        if (status != BOOT_OK) {
            return status;
        }
        crc32_update(chunk, to_read);
        read_ptr += to_read;
        bytes_remaining -= to_read;
    }

    uint32_t payload_crc = crc32_update(NULL, 0U);
    if (payload_crc != header.image_crc32) {
        return BOOT_ERR_CRC_MISMATCH;
    }

    /* 6. Validate Initial Main Stack Pointer (MSP) in SRAM bounds */
    uint32_t vtor_addr = slot_base + APP_VECTOR_TABLE_OFFSET;
    uint32_t initial_msp = 0U;
    status = flash_read(vtor_addr, (uint8_t*)&initial_msp, sizeof(uint32_t));
    if (status != BOOT_OK) {
        return status;
    }

    if (initial_msp < SRAM_BASE_ADDR || initial_msp > SRAM_END_ADDR || (initial_msp & 0x03U) != 0U) {
        return BOOT_ERR_INVALID_STACK_PTR;
    }

    /* 7. Validate Reset Handler Address in Slot Bounds (Thumb mode bit 0 must be 1) */
    uint32_t reset_handler = 0U;
    status = flash_read(vtor_addr + 4U, (uint8_t*)&reset_handler, sizeof(uint32_t));
    if (status != BOOT_OK) {
        return status;
    }

    if (reset_handler < vtor_addr || reset_handler >= (slot_base + max_size) || (reset_handler & 0x01U) == 0U) {
        return BOOT_ERR_INVALID_ENTRY_PT;
    }

    return BOOT_OK;
}

boot_status_t bootloader_resolve_boot_target(uint8_t *selected_slot)
{
    if (selected_slot == NULL) {
        return BOOT_ERR_PARAM;
    }

    uint8_t target = s_metadata.active_slot;
    if (target >= NUM_SLOTS) {
        target = SLOT_INDEX_A;
        s_metadata.active_slot = target;
    }

    /* Handle probation boot attempts for new firmware updates */
    if (s_metadata.slot_state[target] == SLOT_STATE_TESTING) {
        s_metadata.boot_attempts++;
        if (s_metadata.boot_attempts > s_metadata.max_boot_attempts) {
            /* Probation retry threshold breached! Execute failsafe rollback */
            s_metadata.slot_state[target] = SLOT_STATE_ROLLED_BACK;
            s_metadata.rollback_count++;
            s_metadata.last_error_code = (uint32_t)BOOT_ERR_ROLLBACK_TRIGGERED;

            /* Switch active slot to fallback slot */
            target = (target == SLOT_INDEX_A) ? SLOT_INDEX_B : SLOT_INDEX_A;
            s_metadata.active_slot = target;
            s_metadata.boot_attempts = 0U;
            bootloader_save_metadata(&s_metadata);
        } else {
            /* Persist updated attempt counter */
            bootloader_save_metadata(&s_metadata);
        }
    }

    /* Verify integrity of the active target */
    boot_status_t verify_status = bootloader_verify_slot(target);
    if (verify_status == BOOT_OK) {
        *selected_slot = target;
        return BOOT_OK;
    }

    /* Active slot corrupted or failed verification! */
    s_metadata.slot_state[target] = SLOT_STATE_CORRUPT;
    s_metadata.last_error_code = (uint32_t)verify_status;

    /* Attempt fallback slot recovery */
    uint8_t fallback = (target == SLOT_INDEX_A) ? SLOT_INDEX_B : SLOT_INDEX_A;
    if (bootloader_verify_slot(fallback) == BOOT_OK) {
        s_metadata.active_slot = fallback;
        s_metadata.slot_state[fallback] = SLOT_STATE_CONFIRMED;
        s_metadata.boot_attempts = 0U;
        bootloader_save_metadata(&s_metadata);
        *selected_slot = fallback;
        return BOOT_OK;
    }

    /* Both application slots are invalid */
    s_metadata.slot_state[fallback] = SLOT_STATE_CORRUPT;
    bootloader_save_metadata(&s_metadata);
    return BOOT_ERR_ALL_SLOTS_INVALID;
}

boot_status_t bootloader_confirm_application(uint8_t slot_idx)
{
    if (slot_idx >= NUM_SLOTS) {
        return BOOT_ERR_PARAM;
    }

    s_metadata.active_slot = slot_idx;
    s_metadata.slot_state[slot_idx] = SLOT_STATE_CONFIRMED;
    s_metadata.boot_attempts = 0U;
    s_metadata.last_error_code = (uint32_t)BOOT_OK;

    return bootloader_save_metadata(&s_metadata);
}

void bootloader_set_jump_hook(jump_hook_fn hook)
{
    s_jump_hook = hook;
}

boot_status_t bootloader_launch_application(uint8_t slot_idx)
{
    if (slot_idx >= NUM_SLOTS) {
        return BOOT_ERR_PARAM;
    }

    uint32_t slot_base = s_slot_boundaries[slot_idx].start_addr;
    uint32_t vtor_addr = slot_base + APP_VECTOR_TABLE_OFFSET;

    uint32_t msp = 0U;
    uint32_t reset_vector = 0U;

    boot_status_t status = flash_read(vtor_addr, (uint8_t*)&msp, sizeof(uint32_t));
    if (status != BOOT_OK) {
        return status;
    }

    status = flash_read(vtor_addr + 4U, (uint8_t*)&reset_vector, sizeof(uint32_t));
    if (status != BOOT_OK) {
        return status;
    }

    if (s_jump_hook != NULL) {
        s_jump_hook(vtor_addr, msp, reset_vector);
        return BOOT_OK;
    }

#if defined(STM32F446xx)
    /* 1. Disable all interrupts */
    __disable_irq();

    /* 2. De-initialize SysTick timer */
    SysTick->CTRL = 0U;
    SysTick->LOAD = 0U;
    SysTick->VAL = 0U;

    /* 3. Clear all pending NVIC interrupt lines */
    for (uint8_t i = 0; i < 8U; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    /* 4. Relocate Vector Table Offset Register (VTOR) */
    SCB->VTOR = vtor_addr;

    /* 5. Set Main Stack Pointer (MSP) */
    __set_MSP(msp);

    /* 6. Jump to Application Reset Handler */
    typedef void (*pFunction)(void);
    pFunction app_entry = (pFunction)reset_vector;
    app_entry();

    /* Should never reach here */
    while (1) {}
#else
    return BOOT_OK;
#endif
}
