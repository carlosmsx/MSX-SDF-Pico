// Certificados raiz con que https.c reconoce a los servidores de GitHub. Los
// arma tools/raices.py en raices.c; quedan en la flash (.flashdata).

#pragma once

typedef struct {
    const unsigned char *der;
    unsigned largo;
} raiz_t;

extern const raiz_t raices[];
extern const int n_raices;
