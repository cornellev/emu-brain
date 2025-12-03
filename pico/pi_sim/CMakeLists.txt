# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# Use Pico 2 (RP2350) board definition
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pi_sim C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(pi_sim
    pi_sim.c
)

pico_set_program_name(pi_sim "pi_sim")
pico_set_program_version(pi_sim "0.1")

# Enable stdio over USB; disable UART
pico_enable_stdio_uart(pi_sim 0)
pico_enable_stdio_usb(pi_sim 1)

# Link libraries
target_link_libraries(pi_sim
    pico_stdlib
    pico_stdio_usb           # make USB CDC explicit
    pico_bootsel_via_double_reset
    hardware_spi
    hardware_gpio
    hardware_timer
)

# Include project headers
target_include_directories(pi_sim PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

# Create UF2, bin, etc.
pico_add_extra_outputs(pi_sim)
