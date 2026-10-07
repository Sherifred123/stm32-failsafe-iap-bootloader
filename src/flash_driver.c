/**
 * @file flash_driver.c
 * @brief STM32F446RE Flash Memory Controller Driver Implementation
 *
 * Implements Flash programming interface with dual-mode execution:
 * 1. STM32 Bare-Metal Register Driver for Cortex-M4 Silicon
 * 2. High-Fidelity NOR Flash Physical Emulation for Host Unit Testing
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include "flash_driver.h"
#include <string.h>

#if defined(STM32F446xx)
#include "stm32f4xx.h"
#endif

/* STM32F446RE Flash Sector Table (RM0390 Table 5) */
static const flash_sector_info_t s_sectors[8] = {
    { 0U, 0x08000000UL,  16UL * 1024UL }, /* Sector 0: 16 KB */
    { 1U, 0x08004000UL,  16UL * 1024UL }, /* Sector 1: 16 KB */
    { 2U, 0x08008000UL,  16UL * 1024UL }, /* Sector 2: 16 KB */
    { 3U, 0x0800C000UL,  16UL * 1024UL }, /* Sector 3: 16 KB */
    { 4U, 0x08010000UL,  64UL * 1024UL }, /* Sector 4: 64 KB */
    { 5U, 0x08020000UL, 128UL * 1024UL }, /* Sector 5: 128 KB */
    { 6U, 0x08040000UL, 128UL * 1024UL }, /* Sector 6: 128 KB */
    { 7U, 0x08060000UL, 128UL * 1024UL }  /* Sector 7: 128 KB */
};

static bool s_unlocked = false;

#if !defined(STM32F446xx)
/* Simulated NOR Flash Memory Array for Host Verification */
static uint8_t s_sim_flash[FLASH_TOTAL_SIZE];
static bool s_sim_initialized = false;
#endif

void flash_init(void)
{
    s_unlocked = false;

#if !defined(STM32F446xx)
    if (!s_sim_initialized) {
        /* Erased NOR flash states are all 0xFF */
        memset(s_sim_flash, 0xFF, sizeof(s_sim_flash));
        s_sim_initialized = true;
    }
#endif
}

boot_status_t flash_unlock(void)
{
#if defined(STM32F446xx)
    if ((FLASH->CR & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR = FLASH_KEY_1;
        FLASH->KEYR = FLASH_KEY_2;
    }
    s_unlocked = ((FLASH->CR & FLASH_CR_LOCK) == 0U);
    return s_unlocked ? BOOT_OK : BOOT_ERR_FLASH_WRITE_FAIL;
#else
    s_unlocked = true;
    return BOOT_OK;
#endif
}

void flash_lock(void)
{
#if defined(STM32F446xx)
    FLASH->CR |= FLASH_CR_LOCK;
#endif
    s_unlocked = false;
}

bool flash_is_unlocked(void)
{
    return s_unlocked;
}

int8_t flash_get_sector(uint32_t address)
{
    if (address < FLASH_BASE_ADDR || address > FLASH_END_ADDR) {
        return -1;
    }

    for (uint8_t i = 0; i < 8U; i++) {
        uint32_t start = s_sectors[i].start_addr;
        uint32_t end = start + s_sectors[i].size_bytes - 1UL;
        if (address >= start && address <= end) {
            return (int8_t)i;
        }
    }
    return -1;
}

bool flash_get_sector_info(uint8_t sector, flash_sector_info_t *info)
{
    if (sector >= 8U || info == NULL) {
        return false;
    }
    *info = s_sectors[sector];
    return true;
}

boot_status_t flash_erase_sector(uint8_t sector)
{
    if (sector >= 8U) {
        return BOOT_ERR_PARAM;
    }
    if (!s_unlocked) {
        return BOOT_ERR_FLASH_ERASE_FAIL;
    }

#if defined(STM32F446xx)
    /* Wait for previous operation to complete */
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {}

    /* Clear any prior error flags */
    FLASH->SR = (FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | 
                 FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR);

    /* Configure Sector Erase on target sector (SNB bits 3..6) */
    FLASH->CR &= ~(FLASH_CR_SNB | FLASH_CR_PSIZE);
    FLASH->CR |= (FLASH_CR_SER | ((uint32_t)sector << 3U) | FLASH_CR_PSIZE_1); /* 32-bit parallelism */

    /* Trigger Erase */
    FLASH->CR |= FLASH_CR_STRT;

    /* Wait for completion */
    while ((FLASH->SR & FLASH_SR_BSY) != 0U) {}

    /* Clear SER bit */
    FLASH->CR &= ~FLASH_CR_SER;

    /* Check error bits */
    if ((FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) != 0U) {
        return BOOT_ERR_FLASH_ERASE_FAIL;
    }
    return BOOT_OK;
#else
    uint32_t offset = s_sectors[sector].start_addr - FLASH_BASE_ADDR;
    uint32_t size = s_sectors[sector].size_bytes;
    memset(&s_sim_flash[offset], 0xFF, size);
    return BOOT_OK;
#endif
}

boot_status_t flash_erase_range(uint32_t start_addr, size_t length)
{
    if (length == 0U) {
        return BOOT_OK;
    }
    uint32_t end_addr = start_addr + (uint32_t)length - 1UL;
    int8_t start_sec = flash_get_sector(start_addr);
    int8_t end_sec = flash_get_sector(end_addr);

    if (start_sec < 0 || end_sec < 0 || start_sec > end_sec) {
        return BOOT_ERR_PARAM;
    }

    for (int8_t s = start_sec; s <= end_sec; s++) {
        boot_status_t status = flash_erase_sector((uint8_t)s);
        if (status != BOOT_OK) {
            return status;
        }
    }
    return BOOT_OK;
}

boot_status_t flash_write(uint32_t address, const uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U) {
        return BOOT_OK;
    }
    if (address < FLASH_BASE_ADDR || (address + length - 1UL) > FLASH_END_ADDR) {
        return BOOT_ERR_PARAM;
    }
    if (!s_unlocked) {
        return BOOT_ERR_FLASH_WRITE_FAIL;
    }

#if defined(STM32F446xx)
    /* Program byte-by-byte or word-by-word into silicon */
    for (size_t i = 0; i < length; i++) {
        while ((FLASH->SR & FLASH_SR_BSY) != 0U) {}

        /* Clear prior error flags */
        FLASH->SR = (FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | 
                     FLASH_SR_PGPERR | FLASH_SR_PGSERR);

        FLASH->CR &= ~FLASH_CR_PSIZE; /* Byte programming mode */
        FLASH->CR |= FLASH_CR_PG;

        *(__IO uint8_t*)(address + i) = data[i];

        while ((FLASH->SR & FLASH_SR_BSY) != 0U) {}

        FLASH->CR &= ~FLASH_CR_PG;

        if ((FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) != 0U) {
            return BOOT_ERR_FLASH_WRITE_FAIL;
        }
    }
    return BOOT_OK;
#else
    uint32_t offset = address - FLASH_BASE_ADDR;
    for (size_t i = 0; i < length; i++) {
        /* Emulate NOR flash physical behavior: bit flips only from 1 -> 0 */
        uint8_t current_val = s_sim_flash[offset + i];
        uint8_t write_val = data[i];

        /* Verify that no bit is asked to flip from 0 to 1 without an erase */
        if ((current_val & write_val) != write_val && current_val != 0xFF) {
            /* NOR flash over-programming violation */
            return BOOT_ERR_FLASH_WRITE_FAIL;
        }
        s_sim_flash[offset + i] = current_val & write_val;
    }
    return BOOT_OK;
#endif
}

boot_status_t flash_read(uint32_t address, uint8_t *buffer, size_t length)
{
    if (buffer == NULL || length == 0U) {
        return BOOT_OK;
    }
    if (address < FLASH_BASE_ADDR || (address + length - 1UL) > FLASH_END_ADDR) {
        return BOOT_ERR_PARAM;
    }

#if defined(STM32F446xx)
    memcpy(buffer, (const void*)address, length);
    return BOOT_OK;
#else
    uint32_t offset = address - FLASH_BASE_ADDR;
    memcpy(buffer, &s_sim_flash[offset], length);
    return BOOT_OK;
#endif
}
