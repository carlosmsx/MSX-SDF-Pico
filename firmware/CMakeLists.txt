cmake_minimum_required(VERSION 3.13)

# Pico 2 W: los GPIO 23-25 y 29 son del modulo inalambrico y no se usan.
set(PICO_BOARD pico2_w)
include(pico_sdk_import.cmake)

project(sdf_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_compile_options(-Wall -O2)

set(FUENTES
    src/main.c
    src/bus.c
    src/protocolo.c
    src/disco.c
    src/consola.c
)

# Una funcion arma cada variante con el mismo codigo:
#   sdf_pico          el firmware
#   sdf_pico_escucha  la Pico nunca maneja el bus (para probar el cableado)
#   sdf_pico_diag     ademas, latido por segundo y espia de 8 fotos por acceso
function(firmware nombre)
    add_executable(${nombre} ${FUENTES})
    pico_generate_pio_header(${nombre} ${CMAKE_CURRENT_LIST_DIR}/src/bus.pio)
    # Todo en RAM: la flash queda para el disco, y grabarla no frena el bus.
    pico_set_binary_type(${nombre} copy_to_ram)
    # La consola va por el USB nativo: la UART0 queda para los perifericos.
    pico_enable_stdio_usb(${nombre} 1)
    pico_enable_stdio_uart(${nombre} 0)
    target_link_libraries(${nombre}
        pico_stdlib
        pico_multicore
        pico_flash
        hardware_pio
        hardware_dma
        hardware_flash
    )
    pico_add_extra_outputs(${nombre})
endfunction()

firmware(sdf_pico)
firmware(sdf_pico_escucha)
firmware(sdf_pico_diag)
target_compile_definitions(sdf_pico_escucha PRIVATE SOLO_ESCUCHA=1)
target_compile_definitions(sdf_pico_diag PRIVATE SOLO_ESCUCHA=1 DIAGNOSTICO=1)
