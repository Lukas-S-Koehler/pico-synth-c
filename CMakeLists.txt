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
set(PICO_BOARD pico_w CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Initialize Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(pico_synth C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable (must be BEFORE pico_generate_pio_header)
add_executable(pico_synth
    src/main.c
    src/synth.c
    src/audio_driver.c
    src/display.c
    src/shared_state.c
    src/wifi_server.c
)

# Generate PIO header (must be AFTER add_executable)
pico_generate_pio_header(pico_synth ${CMAKE_CURRENT_LIST_DIR}/src/i2s.pio)

# Include directories
target_include_directories(pico_synth PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Link libraries
target_link_libraries(pico_synth
    pico_stdlib
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_adc
    hardware_spi
    hardware_clocks
    pico_cyw43_arch_lwip_poll
)

# Enable USB serial for debugging
pico_enable_stdio_usb(pico_synth 1)
pico_enable_stdio_uart(pico_synth 0)

# Create UF2 file for easy flashing
pico_add_extra_outputs(pico_synth)

# Disable optimizations for debugging
target_compile_options(pico_synth PRIVATE
    -O0
)

# Optional ST7735 display (pins overlap with keys GP19/20 and waveform button GP21)
option(SYNTH_ENABLE_DISPLAY "Drive the ST7735 display" OFF)

# WiFi configuration
target_compile_definitions(pico_synth PRIVATE
    CYW43_HOST_NAME="PicoSynth"
    SYNTH_ENABLE_DISPLAY=$<BOOL:${SYNTH_ENABLE_DISPLAY}>
)
