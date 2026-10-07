cmake_minimum_required(VERSION 3.16)
project(stm32_failsafe_iap_bootloader C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include)

# Core library
add_library(bootloader_core STATIC
    src/crc32.c
    src/flash_driver.c
    src/bootloader_core.c
    src/iap_protocol.c
)

# Host simulation demo
add_executable(build_demo
    src/main.c
)
target_link_libraries(build_demo PRIVATE bootloader_core)

# Unit test suite
enable_testing()
add_executable(run_tests
    tests/unity/unity.c
    tests/test_bootloader_suite.c
)
target_include_directories(run_tests PRIVATE tests/unity)
target_link_libraries(run_tests PRIVATE bootloader_core)

add_test(NAME BootloaderTests COMMAND run_tests)
