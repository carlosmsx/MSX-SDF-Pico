// Configuracion global del SDF-Pico, incluida por pico/config.h en todo el
// programa (PICO_CONFIG_HEADER_FILES en CMakeLists.txt).

#pragma once

// El firmware del CYW43 (~230 KB) se queda en la flash: con copy_to_ram todo
// lo demas se copia a la RAM, y no entraria junto al buffer de la SD. Las
// secciones .flashdata son las que el linker deja en la flash. Solo se lee
// al arrancar el Wi-Fi.
#define CYW43_RESOURCE_ATTRIBUTE __attribute__((section(".flashdata.cyw43"), aligned(4)))

#define CYW43_HOST_NAME "sdf-pico"
