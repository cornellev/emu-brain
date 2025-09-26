# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(strain_gauge C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(strain_gauge strain_gauge.c )

pico_set_program_name(strain_gauge "strain_gauge")
pico_set_program_version(strain_gauge "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(strain_gauge 0)
pico_enable_stdio_usb(strain_gauge 1)

# Add the standard library to the build
target_link_libraries(strain_gauge
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(strain_gauge PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(strain_gauge
    pico_bootsel_via_double_reset
    pico_stdlib
    hardware_adc
    hardware_spi
    hardware_gpio
    hardware_structs  
    hardware_regs     
    hardware_dma
)

pico_add_extra_outputs(strain_gauge)

