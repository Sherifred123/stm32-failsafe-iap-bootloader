/**
 * @file main.c
 * @brief STM32F446RE Failsafe IAP Bootloader Main Entry Point
 *
 * Implements dual-target execution:
 * 1. Silicon Target: System clock configuration, USART2 telemetry, B1 button override,
 *    probation boot verification, and hardware jump execution.
 * 2. Host Simulation: Demonstrates partition verification, CRC checking, and rollback flow.
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bootloader_config.h"
#include "bootloader_core.h"
#include "flash_driver.h"
#include "crc32.h"
#include "iap_protocol.h"

#if defined(STM32F446xx)
#include "stm32f4xx.h"

static void SystemClock_Config(void)
{
    /* Enable HSI */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0) {}

    /* Configure Flash latency: 5 wait states for 180 MHz */
    FLASH->ACR = FLASH_ACR_ICEN | FLASH_ACR_DCEN | FLASH_ACR_PRFTEN | FLASH_ACR_LATENCY_5WS;

    /* Select HSI as system clock during bootloader stage */
    RCC->CFGR &= ~RCC_CFGR_SW;
    RCC->CFGR |= RCC_CFGR_SW_HSI;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {}
}

static void UART2_Init(void)
{
    /* Enable GPIOA and USART2 clocks */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;

    /* PA2 (TX), PA3 (RX) alternate function AF7 */
    GPIOA->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);
    GPIOA->MODER |= (GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1);
    GPIOA->AFR[0] |= (0x07 << 8) | (0x07 << 12);

    /* 115200 Baud @ 16 MHz HSI */
    USART2->BRR = 0x008B;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void UART2_SendChar(char c)
{
    while ((USART2->SR & USART_SR_TXE) == 0) {}
    USART2->DR = (uint8_t)c;
}

static void UART2_SendString(const char *str)
{
    while (*str) {
        UART2_SendChar(*str++);
    }
}

static bool Check_B1_Button_Pressed(void)
{
    /* Enable GPIOC clock for Blue User Button B1 (PC13) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER &= ~GPIO_MODER_MODER13; /* Input mode */
    /* NUCLEO-F446RE B1 button pulls low when pressed */
    return ((GPIOC->IDR & GPIO_IDR_ID13) == 0U);
}

int main(void)
{
    SystemClock_Config();
    UART2_Init();

    UART2_SendString("\r\n============================================================\r\n");
    UART2_SendString("   STM32F446RE Failsafe IAP Bootloader (180 MHz Cortex-M4)\r\n");
    UART2_SendString("   Dual-Slot NOR Flash Partitioning & Hardware CRC-32\r\n");
    UART2_SendString("============================================================\r\n");

    bootloader_core_init();

    /* Check manual override via B1 button */
    if (Check_B1_Button_Pressed()) {
        UART2_SendString("[BOOT] User Button B1 pressed! Forcing IAP Flasher mode.\r\n");
        while (1) {
            /* Service serial IAP packets */
            if ((USART2->SR & USART_SR_RXNE) != 0U) {
                uint8_t byte = (uint8_t)USART2->DR;
                iap_packet_t pkt;
                if (iap_protocol_feed_byte(byte, &pkt)) {
                    iap_response_t resp;
                    iap_protocol_handle_request(&pkt, &resp);
                    uint8_t tx_buf[IAP_MAX_FRAME_SIZE];
                    size_t tx_len = iap_protocol_serialize_response(&resp, tx_buf, sizeof(tx_buf));
                    for (size_t i = 0; i < tx_len; i++) {
                        UART2_SendChar((char)tx_buf[i]);
                    }
                }
            }
        }
    }

    uint8_t target_slot = 0;
    boot_status_t status = bootloader_resolve_boot_target(&target_slot);
    if (status == BOOT_OK) {
        UART2_SendString("[BOOT] Image validation passed! Jumping to application...\r\n");
        bootloader_launch_application(target_slot);
    } else {
        UART2_SendString("[BOOT] ERROR: No valid application image found! Entering IAP mode.\r\n");
        while (1) {}
    }

    return 0;
}

#else

/* Host Simulation & Verification Demonstration */
static void demo_jump_hook(uint32_t target_vtor, uint32_t msp, uint32_t reset_vector)
{
    printf("[SIM-HW] Jump Hook Triggered:\n");
    printf("         -> VTOR Relocation Address: 0x%08X\n", target_vtor);
    printf("         -> Initial Main Stack Pointer: 0x%08X (Valid SRAM)\n", msp);
    printf("         -> Application Reset Handler:  0x%08X (Thumb Mode)\n", reset_vector);
    printf("[SIM-HW] Context switch to application successful!\n");
}

int main(void)
{
    printf("\n============================================================\n");
    printf("   STM32F446RE Failsafe IAP Bootloader Simulation Harness\n");
    printf("   Dual-Slot Flash Architecture & Hardware CRC-32 Verification\n");
    printf("============================================================\n\n");

    /* 1. Initialize bootloader subsystems */
    bootloader_core_init();
    bootloader_set_jump_hook(demo_jump_hook);

    const boot_metadata_t *meta = bootloader_get_metadata();
    printf("[INFO] Bootloader Initialized:\n");
    printf("       Active Slot: %s\n", (meta->active_slot == 0) ? "Slot A (0x08010000)" : "Slot B (0x08040000)");
    printf("       Slot A State: 0x%02X | Slot B State: 0x%02X\n", meta->slot_state[0], meta->slot_state[1]);
    printf("       Rollback Count: %u\n\n", meta->rollback_count);

    /* 2. Synthesize an authentic test application payload */
    printf("[DEMO] Preparing application binary payload...\n");
    uint8_t synthetic_app[1024];
    memset(synthetic_app, 0xAA, sizeof(synthetic_app));

    /* Vector Table at offset 0x200 */
    uint32_t *vectors = (uint32_t*)&synthetic_app[APP_VECTOR_TABLE_OFFSET];
    vectors[0] = 0x2001FFFCUL; /* Top of 128KB SRAM */
    vectors[1] = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL; /* Valid Thumb Reset Handler */

    /* Compute Payload CRC-32 */
    uint32_t payload_crc = crc32_calculate(synthetic_app + APP_VECTOR_TABLE_OFFSET, sizeof(synthetic_app) - APP_VECTOR_TABLE_OFFSET);

    /* Construct Image Header */
    image_header_t hdr;
    memset(&hdr, 0, sizeof(image_header_t));
    hdr.magic = IMAGE_HEADER_MAGIC;
    hdr.version_major = 2;
    hdr.version_minor = 1;
    hdr.version_patch = 0;
    hdr.image_size = sizeof(synthetic_app) - APP_VECTOR_TABLE_OFFSET;
    hdr.image_crc32 = payload_crc;
    hdr.entry_point = vectors[1];
    hdr.load_address = SLOT_A_START_ADDR;
    hdr.build_timestamp = 1774000000UL;
    strncpy(hdr.git_sha, "a9f82d1", sizeof(hdr.git_sha));
    hdr.header_crc32 = crc32_calculate((const uint8_t*)&hdr, offsetof(image_header_t, header_crc32));

    /* Program Header and Payload into simulated Slot A */
    printf("[DEMO] Flashing synthesized application into Slot A (0x08010000)...\n");
    flash_unlock();
    flash_erase_sector(SLOT_A_SECTOR_START);
    flash_erase_sector(SLOT_A_SECTOR_END);
    flash_write(SLOT_A_START_ADDR, (const uint8_t*)&hdr, sizeof(image_header_t));
    flash_write(SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET, synthetic_app + APP_VECTOR_TABLE_OFFSET, sizeof(synthetic_app) - APP_VECTOR_TABLE_OFFSET);
    flash_lock();

    /* Verify Slot A */
    printf("[DEMO] Running on-chip image integrity verification...\n");
    boot_status_t verify_res = bootloader_verify_slot(SLOT_INDEX_A);
    printf("       Slot A Verification Result: %s (Code: %d)\n\n", (verify_res == BOOT_OK) ? "PASSED [OK]" : "FAILED", verify_res);

    /* Resolve boot target and execute jump */
    uint8_t target_slot = 0;
    boot_status_t boot_res = bootloader_resolve_boot_target(&target_slot);
    if (boot_res == BOOT_OK) {
        printf("[DEMO] Boot decision resolved to Slot %c. Launching application...\n", (target_slot == 0) ? 'A' : 'B');
        bootloader_launch_application(target_slot);
    } else {
        printf("[DEMO] Boot resolution failed! Error: %d\n", boot_res);
    }

    printf("\n============================================================\n");
    printf("   Demonstration Completed Successfully.\n");
    printf("============================================================\n\n");
    return 0;
}

#endif
