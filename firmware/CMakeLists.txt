cmake_minimum_required(VERSION 3.13)

# Pico 2 W: los GPIO 23-25 y 29 son del modulo inalambrico y no se usan.
set(PICO_BOARD pico2_w)
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

# Variantes para buscar fallas, con el mismo codigo:
#   sdf_pico_escucha  la Pico nunca maneja el bus (para probar el cableado)
#   sdf_pico_diag     latido por segundo y espia de 8 fotos por acceso
foreach(variante escucha diag)
    add_executable(sdf_pico_${variante} src/main.c)
    pico_generate_pio_header(sdf_pico_${variante} ${CMAKE_CURRENT_LIST_DIR}/src/bus.pio)
    pico_enable_stdio_usb(sdf_pico_${variante} 1)
    pico_enable_stdio_uart(sdf_pico_${variante} 0)
    target_link_libraries(sdf_pico_${variante} pico_stdlib hardware_pio hardware_dma)
    pico_add_extra_outputs(sdf_pico_${variante})
endforeach()
target_compile_definitions(sdf_pico_escucha PRIVATE SOLO_ESCUCHA=1)
target_compile_definitions(sdf_pico_diag PRIVATE SOLO_ESCUCHA=1 DIAGNOSTICO=1)
