/**
 * @file bootloader_config.h
 * @brief Production Configuration & Memory Layout for STM32F446RE IAP Bootloader
 *
 * Defines Flash sector boundaries, application slots, image header layout,
 * and failsafe rollback constants for the 512KB on-chip flash memory.
 *
 * Hardware Target: STM32F446RE (ARM Cortex-M4 @ 180 MHz, 512KB Flash, 128KB SRAM)
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                STM32F446RE HARDWARE MEMORY SPECIFICATIONS                  */
/* ========================================================================== */

#define FLASH_BASE_ADDR                 (0x08000000UL)
#define FLASH_TOTAL_SIZE                (512UL * 1024UL)          /* 512 KBytes */
#define FLASH_END_ADDR                  (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE - 1UL)

#define SRAM_BASE_ADDR                  (0x20000000UL)
#define SRAM_TOTAL_SIZE                 (128UL * 1024UL)          /* 128 KBytes */
#define SRAM_END_ADDR                   (SRAM_BASE_ADDR + SRAM_TOTAL_SIZE)

/* ========================================================================== */
/*                   FLASH SECTOR PARTITIONING MAP                            */
/* ========================================================================== */

/* Sector 0-1: Primary Bootloader (32 KB total) */
#define BOOTLOADER_START_ADDR           (0x08000000UL)
#define BOOTLOADER_SIZE                 (32UL * 1024UL)
#define BOOTLOADER_SECTOR_START         (0U)
#define BOOTLOADER_SECTOR_END           (1U)

/* Sector 2: Bootloader Metadata & NVRAM State (16 KB) */
#define METADATA_FLASH_ADDR             (0x08008000UL)
#define METADATA_FLASH_SIZE             (16UL * 1024UL)
#define METADATA_SECTOR                 (2U)

/* Sector 3: Reserved Configuration Storage (16 KB) */
#define RESERVED_FLASH_ADDR             (0x0800C000UL)
#define RESERVED_FLASH_SIZE             (16UL * 1024UL)
#define RESERVED_SECTOR                 (3U)

/* Sector 4-5: Application Slot A (Primary / Factory - 192 KB) */
#define SLOT_A_START_ADDR               (0x08010000UL)
#define SLOT_A_MAX_SIZE                 (192UL * 1024UL)
#define SLOT_A_SECTOR_START             (4U)
#define SLOT_A_SECTOR_END               (5U)

/* Sector 6-7: Application Slot B (Secondary / OTA Staging - 256 KB) */
#define SLOT_B_START_ADDR               (0x08040000UL)
#define SLOT_B_MAX_SIZE                 (256UL * 1024UL)
#define SLOT_B_SECTOR_START             (6U)
#define SLOT_B_SECTOR_END               (7U)

/* Number of application slots available */
#define NUM_SLOTS                       (2U)
#define SLOT_INDEX_A                    (0U)
#define SLOT_INDEX_B                    (1U)

/* ========================================================================== */
/*                       IMAGE HEADER SPECIFICATION                           */
/* ========================================================================== */

#define IMAGE_HEADER_MAGIC              (0x424F4F54UL)  /* ASCII 'BOOT' */
#define METADATA_MAGIC                  (0x4D455441UL)  /* ASCII 'META' */

#define IMAGE_HEADER_OFFSET             (0x00000000UL)  /* Header placed at start of slot */
#define IMAGE_HEADER_SIZE               (sizeof(image_header_t))
#define APP_VECTOR_TABLE_OFFSET         (0x00000200UL)  /* Vector table placed 512B in */

/**
 * @brief Application Image Header structure placed at offset 0 of each slot.
 * Enforces cryptographic / integrity verification before boot.
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;             /**< Must match IMAGE_HEADER_MAGIC ('BOOT') */
    uint8_t  version_major;     /**< SemVer Major */
    uint8_t  version_minor;     /**< SemVer Minor */
    uint8_t  version_patch;     /**< SemVer Patch */
    uint8_t  reserved1;         /**< Alignment padding */
    uint32_t image_size;        /**< Payload size in bytes (excluding header) */
    uint32_t image_crc32;       /**< Hardware STM32 CRC-32 over application payload */
    uint32_t entry_point;       /**< Reset handler address */
    uint32_t load_address;      /**< Target flash address */
    uint32_t build_timestamp;   /**< UNIX Epoch timestamp of build */
    char     git_sha[8];        /**< Short git commit SHA string (null-terminated) */
    uint32_t header_crc32;      /**< CRC-32 of header fields (from magic to git_sha) */
} image_header_t;

/* ========================================================================== */
/*                   SLOT LIFECYCLE & STATE MACHINE                           */
/* ========================================================================== */

typedef enum {
    SLOT_STATE_EMPTY        = 0xFF, /**< Erased flash state (unprogrammed) */
    SLOT_STATE_VALID        = 0x01, /**< Image verified, ready for probation boot */
    SLOT_STATE_TESTING      = 0x02, /**< In probationary boot testing phase */
    SLOT_STATE_CONFIRMED    = 0x03, /**< Confirmed operational by application */
    SLOT_STATE_CORRUPT      = 0x0E, /**< Corrupted CRC or validation failure */
    SLOT_STATE_ROLLED_BACK  = 0x0F  /**< Failed boot probation; locked out */
} slot_state_t;

/**
 * @brief Non-volatile boot metadata tracking table (Stored in Sector 2).
 */
typedef struct __attribute__((packed)) {
    uint32_t     magic;               /**< Must match METADATA_MAGIC ('META') */
    uint8_t      active_slot;         /**< Currently selected slot (0 = A, 1 = B) */
    uint8_t      slot_state[NUM_SLOTS]; /**< Lifecycle state of each slot */
    uint8_t      boot_attempts;       /**< Active boot retry counter */
    uint8_t      max_boot_attempts;   /**< Retry limit before failsafe rollback (e.g. 3) */
    uint8_t      reserved[3];         /**< 4-byte boundary padding */
    uint32_t     rollback_count;      /**< Lifetime rollback event counter */
    uint32_t     last_error_code;     /**< Telemetry diagnostic code of last failure */
    uint32_t     crc32;               /**< Integrity checksum of metadata record */
} boot_metadata_t;

/* Default maximum allowable probation boot attempts */
#define DEFAULT_MAX_BOOT_ATTEMPTS       (3U)

/* ========================================================================== */
/*                       BOOTLOADER RETURN CODES                              */
/* ========================================================================== */

typedef enum {
    BOOT_OK                     =  0,
    BOOT_ERR_INVALID_MAGIC      = -1,
    BOOT_ERR_CRC_MISMATCH       = -2,
    BOOT_ERR_INVALID_STACK_PTR  = -3,
    BOOT_ERR_INVALID_ENTRY_PT   = -4,
    BOOT_ERR_ROLLBACK_TRIGGERED = -5,
    BOOT_ERR_ALL_SLOTS_INVALID  = -6,
    BOOT_ERR_FLASH_WRITE_FAIL   = -7,
    BOOT_ERR_FLASH_ERASE_FAIL   = -8,
    BOOT_ERR_TIMEOUT            = -9,
    BOOT_ERR_PARAM              = -10
} boot_status_t;

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_CONFIG_H */
