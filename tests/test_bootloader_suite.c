/**
 * @file test_bootloader_suite.c
 * @brief Comprehensive Unity Unit Test Suite for STM32F446RE IAP Bootloader
 *
 * Verifies CRC32 calculation, NOR flash physics emulation, image header
 * authentication, stack pointer boundary checking, failsafe rollback policy,
 * and IAP serial packet framing.
 *
 * (C) 2026 Sherifred Singh. Production Engineering Reference.
 */

#include "unity.h"
#include "bootloader_config.h"
#include "bootloader_core.h"
#include "flash_driver.h"
#include "crc32.h"
#include "iap_protocol.h"
#include <string.h>

static uint32_t s_last_jump_vtor = 0;
static uint32_t s_last_jump_msp = 0;
static uint32_t s_last_jump_reset = 0;

static void mock_jump_hook(uint32_t vtor, uint32_t msp, uint32_t reset_vec)
{
    s_last_jump_vtor = vtor;
    s_last_jump_msp = msp;
    s_last_jump_reset = reset_vec;
}

void setUp(void)
{
    bootloader_core_init();
    bootloader_set_jump_hook(mock_jump_hook);
    s_last_jump_vtor = 0;
    s_last_jump_msp = 0;
    s_last_jump_reset = 0;
}

void tearDown(void)
{
}

/* Helper to generate a valid test payload in a slot */
static void flash_synthetic_slot(uint8_t slot_idx, uint32_t msp_val, uint32_t reset_val, bool corrupt_crc)
{
    uint32_t slot_start = (slot_idx == 0) ? SLOT_A_START_ADDR : SLOT_B_START_ADDR;

    flash_unlock();
    flash_erase_sector((slot_idx == 0) ? SLOT_A_SECTOR_START : SLOT_B_SECTOR_START);
    flash_erase_sector((slot_idx == 0) ? SLOT_A_SECTOR_END : SLOT_B_SECTOR_END);

    uint8_t payload[1024];
    memset(payload, 0xEE, sizeof(payload));

    uint32_t *vectors = (uint32_t*)&payload[APP_VECTOR_TABLE_OFFSET];
    vectors[0] = msp_val;
    vectors[1] = reset_val;

    size_t payload_len = sizeof(payload) - APP_VECTOR_TABLE_OFFSET;
    uint32_t payload_crc = crc32_calculate(payload + APP_VECTOR_TABLE_OFFSET, payload_len);
    if (corrupt_crc) {
        payload_crc ^= 0xDEADBEEFUL;
    }

    image_header_t hdr;
    memset(&hdr, 0, sizeof(image_header_t));
    hdr.magic = IMAGE_HEADER_MAGIC;
    hdr.version_major = 1;
    hdr.version_minor = 0;
    hdr.version_patch = 0;
    hdr.image_size = (uint32_t)payload_len;
    hdr.image_crc32 = payload_crc;
    hdr.entry_point = reset_val;
    hdr.load_address = slot_start;
    hdr.build_timestamp = 1774000000UL;
    strncpy(hdr.git_sha, "abcdef0", sizeof(hdr.git_sha));
    hdr.header_crc32 = crc32_calculate((const uint8_t*)&hdr, offsetof(image_header_t, header_crc32));

    flash_write(slot_start, (const uint8_t*)&hdr, sizeof(image_header_t));
    flash_write(slot_start + APP_VECTOR_TABLE_OFFSET, payload + APP_VECTOR_TABLE_OFFSET, payload_len);
    flash_lock();
}

/* TEST 1: CRC-32 Engine Consistency */
void test_crc32_calculation(void)
{
    const uint8_t test_data[] = "STM32F446RE-IAP-BOOTLOADER-VERIFICATION";
    uint32_t crc1 = crc32_calculate(test_data, strlen((const char*)test_data));
    uint32_t crc2 = crc32_calculate(test_data, strlen((const char*)test_data));

    TEST_ASSERT_NOT_EQUAL(0, crc1);
    TEST_ASSERT_EQUAL_UINT32(crc1, crc2);

    /* Verify incremental update matches one-shot */
    crc32_reset();
    crc32_update(test_data, 10);
    crc32_update(test_data + 10, strlen((const char*)test_data) - 10);
    uint32_t crc_split = crc32_update(NULL, 0);

    TEST_ASSERT_EQUAL_UINT32(crc1, crc_split);
}

/* TEST 2: Flash Driver Erase and Write Simulation */
void test_flash_driver_erase_and_program(void)
{
    flash_unlock();
    TEST_ASSERT_TRUE(flash_is_unlocked());

    /* Erase Sector 4 (Slot A Start) */
    boot_status_t status = flash_erase_sector(4);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);

    /* Verify erased bytes are 0xFF */
    uint8_t readback[16];
    status = flash_read(SLOT_A_START_ADDR, readback, sizeof(readback));
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
    for (size_t i = 0; i < sizeof(readback); i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, readback[i]);
    }

    /* Program specific pattern */
    const uint8_t pattern[] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    status = flash_write(SLOT_A_START_ADDR, pattern, sizeof(pattern));
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);

    status = flash_read(SLOT_A_START_ADDR, readback, sizeof(pattern));
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, sizeof(pattern));

    flash_lock();
    TEST_ASSERT_FALSE(flash_is_unlocked());
}

/* TEST 3: Valid Image Authentication */
void test_image_header_validation_success(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    uint32_t valid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, valid_reset, false);

    boot_status_t status = bootloader_verify_slot(SLOT_INDEX_A);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
}

/* TEST 4: Corrupted Payload CRC Rejection */
void test_corrupted_payload_crc_rejection(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    uint32_t valid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, valid_reset, true);

    boot_status_t status = bootloader_verify_slot(SLOT_INDEX_A);
    TEST_ASSERT_EQUAL_INT(BOOT_ERR_CRC_MISMATCH, status);
}

/* TEST 5: Invalid Stack Pointer Outside SRAM Bounds */
void test_invalid_stack_pointer_rejection(void)
{
    /* Out of SRAM bounds address: 0x10000000 */
    uint32_t invalid_msp = 0x10000000UL;
    uint32_t valid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    flash_synthetic_slot(SLOT_INDEX_A, invalid_msp, valid_reset, false);

    boot_status_t status = bootloader_verify_slot(SLOT_INDEX_A);
    TEST_ASSERT_EQUAL_INT(BOOT_ERR_INVALID_STACK_PTR, status);
}

/* TEST 6: Invalid Reset Vector (Missing Thumb bit 0) */
void test_invalid_reset_vector_arm_mode_rejection(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    /* Even address (Bit 0 = 0 is illegal on Cortex-M) */
    uint32_t invalid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x100UL;
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, invalid_reset, false);

    boot_status_t status = bootloader_verify_slot(SLOT_INDEX_A);
    TEST_ASSERT_EQUAL_INT(BOOT_ERR_INVALID_ENTRY_PT, status);
}

/* TEST 7: Probation Boot Retry Counter */
void test_probation_boot_retry_increment(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    uint32_t valid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, valid_reset, false);

    /* Mark Slot A in TESTING state */
    boot_metadata_t meta = *bootloader_get_metadata();
    meta.active_slot = SLOT_INDEX_A;
    meta.slot_state[SLOT_INDEX_A] = SLOT_STATE_TESTING;
    meta.boot_attempts = 0;
    bootloader_save_metadata(&meta);

    uint8_t target = 0xFF;
    boot_status_t status = bootloader_resolve_boot_target(&target);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
    TEST_ASSERT_EQUAL_UINT8(SLOT_INDEX_A, target);

    /* Attempt counter should have incremented to 1 */
    const boot_metadata_t *updated_meta = bootloader_get_metadata();
    TEST_ASSERT_EQUAL_UINT8(1, updated_meta->boot_attempts);
}

/* TEST 8: Automatic Failsafe Rollback on Exceeded Attempts */
void test_failsafe_rollback_on_timeout(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    uint32_t valid_reset_a = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    uint32_t valid_reset_b = SLOT_B_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;

    /* Populate both slots */
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, valid_reset_a, false);
    flash_synthetic_slot(SLOT_INDEX_B, valid_msp, valid_reset_b, false);

    /* Slot A has reached its retry limit (3 attempts) while still unconfirmed */
    boot_metadata_t meta = *bootloader_get_metadata();
    meta.active_slot = SLOT_INDEX_A;
    meta.slot_state[SLOT_INDEX_A] = SLOT_STATE_TESTING;
    meta.slot_state[SLOT_INDEX_B] = SLOT_STATE_CONFIRMED;
    meta.boot_attempts = 3;
    meta.max_boot_attempts = 3;
    bootloader_save_metadata(&meta);

    /* Resolve boot target should trigger rollback to Slot B */
    uint8_t target = 0xFF;
    boot_status_t status = bootloader_resolve_boot_target(&target);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
    TEST_ASSERT_EQUAL_UINT8(SLOT_INDEX_B, target);

    /* Verify metadata reflects rollback */
    const boot_metadata_t *m = bootloader_get_metadata();
    TEST_ASSERT_EQUAL_UINT8(SLOT_STATE_ROLLED_BACK, m->slot_state[SLOT_INDEX_A]);
    TEST_ASSERT_EQUAL_UINT8(SLOT_INDEX_B, m->active_slot);
    TEST_ASSERT_EQUAL_UINT32(1, m->rollback_count);
}

/* TEST 9: IAP Protocol Ping & Response Serialization */
void test_iap_protocol_ping_flow(void)
{
    /* Build raw PING packet */
    uint8_t raw_frame[] = {
        0xAA,       /* SOF */
        0x01,       /* CMD: PING */
        0x00, 0x00, /* LEN: 0 */
        0x05,       /* SEQ: 5 */
        0x00, 0x00, 0x00, 0x00, /* Placeholder CRC */
        0x55        /* EOF */
    };

    /* Calculate frame CRC (CMD + LEN + SEQ) */
    crc32_reset();
    crc32_update(&raw_frame[1], 4);
    uint32_t crc = crc32_update(NULL, 0);
    raw_frame[5] = (uint8_t)(crc & 0xFF);
    raw_frame[6] = (uint8_t)((crc >> 8) & 0xFF);
    raw_frame[7] = (uint8_t)((crc >> 16) & 0xFF);
    raw_frame[8] = (uint8_t)((crc >> 24) & 0xFF);

    iap_packet_t rx_pkt;
    bool frame_complete = false;
    for (size_t i = 0; i < sizeof(raw_frame); i++) {
        if (iap_protocol_feed_byte(raw_frame[i], &rx_pkt)) {
            frame_complete = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(frame_complete);
    TEST_ASSERT_EQUAL_UINT8(IAP_CMD_PING, rx_pkt.cmd);
    TEST_ASSERT_EQUAL_UINT8(5, rx_pkt.seq);

    /* Process request */
    iap_response_t resp;
    boot_status_t status = iap_protocol_handle_request(&rx_pkt, &resp);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);
    TEST_ASSERT_EQUAL_UINT8(IAP_RESP_ACK, resp.status_code);
    TEST_ASSERT_EQUAL_UINT8(0x04, resp.payload[0]); /* STM32F4 */
    TEST_ASSERT_EQUAL_UINT8(0x46, resp.payload[1]); /* 46 */
}

/* TEST 10: Jump Hook Execution */
void test_jump_hook_execution(void)
{
    uint32_t valid_msp = 0x2001FFFCUL;
    uint32_t valid_reset = SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET + 0x101UL;
    flash_synthetic_slot(SLOT_INDEX_A, valid_msp, valid_reset, false);

    boot_status_t status = bootloader_launch_application(SLOT_INDEX_A);
    TEST_ASSERT_EQUAL_INT(BOOT_OK, status);

    TEST_ASSERT_EQUAL_HEX32(SLOT_A_START_ADDR + APP_VECTOR_TABLE_OFFSET, s_last_jump_vtor);
    TEST_ASSERT_EQUAL_HEX32(valid_msp, s_last_jump_msp);
    TEST_ASSERT_EQUAL_HEX32(valid_reset, s_last_jump_reset);
}

int main(void)
{
    UNITY_BEGIN();

    printf("\n------------------------------------------------------------\n");
    printf("   UNITY UNIT TEST EXECUTION: STM32F446RE Failsafe IAP Bootloader\n");
    printf("------------------------------------------------------------\n");

    RUN_TEST(test_crc32_calculation);
    RUN_TEST(test_flash_driver_erase_and_program);
    RUN_TEST(test_image_header_validation_success);
    RUN_TEST(test_corrupted_payload_crc_rejection);
    RUN_TEST(test_invalid_stack_pointer_rejection);
    RUN_TEST(test_invalid_reset_vector_arm_mode_rejection);
    RUN_TEST(test_probation_boot_retry_increment);
    RUN_TEST(test_failsafe_rollback_on_timeout);
    RUN_TEST(test_iap_protocol_ping_flow);
    RUN_TEST(test_jump_hook_execution);

    return UNITY_END();
}
