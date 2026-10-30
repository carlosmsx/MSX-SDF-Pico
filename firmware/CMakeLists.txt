cmake_minimum_required(VERSION 3.13)

# Pico 2 W: los GPIO 23-25 y 29 son del modulo inalambrico (wifi.c).
set(PICO_BOARD pico2_w)
list(APPEND PICO_CONFIG_HEADER_FILES ${CMAKE_CURRENT_LIST_DIR}/src/sdf_config.h)
include(pico_sdk_import.cmake)

project(sdf_pico C CXX ASM)
set(CMAKE_C_STANDARD 11)
pico_sdk_init()

add_compile_options(-Wall -O2)

set(FUENTES
    src/main.c
    src/bus.c
    src/protocolo.c
    src/imagenes.c
    src/archivos.c
    src/sd_spi.c
    src/consola.c
    src/wifi.c
    src/menu_wifi.c
    src/wifi_dos.c
    src/red.c
    src/ping.c
    src/ajustes.c
    src/reloj.c
    src/dispositivos.c
    src/camara.c
    src/copro.c
    src/cueva.c
    src/planetas.c
    src/sc2.c
    src/https.c
    src/raices.c
    src/actualizar.c
    src/mqtt.c
    src/mqtt_red.c
    lib/fatfs/ff.c
    lib/fatfs/ffunicode.c
    lib/tjpgd/tjpgd.c
)
# src/disco.c (el disco en la flash de la Pico, docs/DISCO-FLASH.md) quedo
# fuera de la compilacion: fue el respaldo de prueba antes de la SD.

# Una funcion arma cada variante con el mismo codigo:
#   sdf_pico          el firmware
#   sdf_pico_escucha  la Pico nunca maneja el bus (para probar el cableado)
#   sdf_pico_diag     ademas, latido por segundo y espia de 8 fotos por acceso
# Las dos de prueba solo se compilan con -DSDF_VARIANTES=ON.
option(SDF_VARIANTES "Compilar tambien sdf_pico_escucha y sdf_pico_diag" OFF)
function(firmware nombre)
    add_executable(${nombre} ${FUENTES})
    pico_generate_pio_header(${nombre} ${CMAKE_CURRENT_LIST_DIR}/src/bus.pio)
    target_include_directories(${nombre} PRIVATE lib/fatfs lib/tjpgd src)
    # Todo en RAM: el nucleo que atiende el bus no depende de la cache de flash.
    pico_set_binary_type(${nombre} copy_to_ram)
    # Salvo mbedTLS, lwIP y el driver del CYW43, que quedan en la flash
    # (ld/section_copy_to_ram_rodata.incl).
    pico_add_linker_script_override_path(${nombre} ${CMAKE_CURRENT_LIST_DIR}/ld
        FILES section_copy_to_ram_rodata.incl section_heap.incl)
    # La consola va por el USB nativo: la UART0 queda para los perifericos.
    pico_enable_stdio_usb(${nombre} 1)
    pico_enable_stdio_uart(${nombre} 0)
    target_link_libraries(${nombre}
        pico_stdlib
        pico_multicore
        hardware_pio
        hardware_dma
        hardware_spi
        hardware_flash
        pico_cyw43_arch_lwip_threadsafe_background
        pico_lwip_sntp
        pico_lwip_mqtt
        pico_mbedtls
        hardware_watchdog
    )
    pico_add_extra_outputs(${nombre})
endfunction()

firmware(sdf_pico)
if(SDF_VARIANTES)
    firmware(sdf_pico_escucha)
    firmware(sdf_pico_diag)
    target_compile_definitions(sdf_pico_escucha PRIVATE SOLO_ESCUCHA=1)
    target_compile_definitions(sdf_pico_diag PRIVATE SOLO_ESCUCHA=1 DIAGNOSTICO=1)
endif()
