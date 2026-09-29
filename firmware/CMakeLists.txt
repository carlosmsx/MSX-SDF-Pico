cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico2)
include(pico_sdk_import.cmake)

project(sdf_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_compile_options(-Wall -O2)

add_executable(sdf_pico
    src/main.c
)

pico_generate_pio_header(sdf_pico ${CMAKE_CURRENT_LIST_DIR}/src/bus.pio)

# La consola va por el USB nativo: la UART0 queda para los perifericos.
pico_enable_stdio_usb(sdf_pico 1)
pico_enable_stdio_uart(sdf_pico 0)

target_link_libraries(sdf_pico
    pico_stdlib
    hardware_pio
    hardware_dma
)

pico_add_extra_outputs(sdf_pico)
