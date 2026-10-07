# ==============================================================================
# Makefile for STM32F446RE Failsafe IAP Bootloader
# Production Engineering Reference - (C) 2026 Sherifred Singh
# ==============================================================================

CC ?= gcc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -Iinclude
TEST_FLAGS ?= -std=c99 -Wall -Wextra -Iinclude -Itests/unity

# Cross-compilation variables for ARM Cortex-M4 Silicon
ARM_CC ?= arm-none-eabi-gcc
ARM_CFLAGS ?= -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O2 \
              -DSTM32F446xx -Wall -Wextra -Iinclude

SRCS_CORE = src/crc32.c src/flash_driver.c src/bootloader_core.c src/iap_protocol.c
SRCS_TEST = tests/unity/unity.c $(SRCS_CORE) tests/test_bootloader_suite.c
SRCS_HOST = $(SRCS_CORE) src/main.c

BIN_TEST = run_tests
BIN_HOST = build_demo

.PHONY: all test host arm clean

all: test host

# Compile and run automated Unity unit test suite
test:
	@echo "============================================================"
	@echo " Compiling and Executing Unity Unit Test Suite"
	@echo "============================================================"
	$(CC) $(TEST_FLAGS) $(SRCS_TEST) -o $(BIN_TEST)
	./$(BIN_TEST)

# Compile and execute host simulation harness
host:
	@echo "============================================================"
	@echo " Building Host Simulation Demonstration"
	@echo "============================================================"
	$(CC) $(CFLAGS) $(SRCS_HOST) -o $(BIN_HOST)
	./$(BIN_HOST)

# Verify syntax and cross-compilation for Cortex-M4 hardware target
arm:
	@echo "============================================================"
	@echo " Cross-Compiling for STM32F446RE (ARM Cortex-M4)"
	@echo "============================================================"
	$(ARM_CC) $(ARM_CFLAGS) -c src/crc32.c -o /dev/null
	$(ARM_CC) $(ARM_CFLAGS) -c src/flash_driver.c -o /dev/null
	$(ARM_CC) $(ARM_CFLAGS) -c src/bootloader_core.c -o /dev/null
	$(ARM_CC) $(ARM_CFLAGS) -c src/iap_protocol.c -o /dev/null
	@echo "ARM Cortex-M4 cross-compilation syntax verified successfully."

clean:
	rm -f $(BIN_TEST) $(BIN_TEST).exe $(BIN_HOST) $(BIN_HOST).exe *.o *.iap.bin
