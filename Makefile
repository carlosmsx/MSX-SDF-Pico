# Compilacion de la DiskROM del SDF-Pico (Z80). El firmware de la Pico se
# compila aparte, con firmware/build.ps1.
#
# La DiskROM deriva de la del SDF-1 (mismas fuentes y mismo armado; ver el
# Makefile de ese repo para el detalle de cada paso). Se ensambla con
# Nestor80 y se linkea contra el MSX-DOS kit de ASCII.
#
# build/ NO VIENE EN ESTE REPO: es el kit de ASCII (codigo de terceros, no
# redistribuido). Hay que poner la copia propia antes de "make rom": los .REL
# que listan MODULES y MODULES2, y el BOOT.Z80. "make check" avisa si falta.
# Por lo mismo out/sdfpico.rom NO se publica.
#
# Nestor80 (N80 y LK80) va en tools/bin/, que tampoco se versiona:
#   https://github.com/Konamiman/Nestor80/releases
#
# Uso:
#   make            la DiskROM -> out/sdfpico.rom
#   make check      solo verifica que esten las herramientas y el kit
#   make clean      borra out/
#
# Sin sintaxis de sh en las recetas: sin sh en el PATH, make las corre con
# cmd.exe (igual que en el SDF-1).

DRIVER   := diskrom/DSKDRV.MAC
PAGE2    := diskrom/PAGE2.MAC
PROTO    := diskrom/PROTOCOL.INC
KIT      := build
OUT      := out

N80      := tools/bin/N80.exe
LK80     := tools/bin/LK80.exe
PYTHON   := python

# Orden de modulos del kit (build/LINK.SUB): define el mapa de memoria.
MODULES  := DOSHEAD BIOS MSXDOS SMLDISP INIT DSKBASIC MSXDATA
MODULES2 := BASDATA

CODE_ORG  := 4000h
DATA_ORG  := F237h
DATA_ORG2 := F327h

REL_KIT  := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES)))
REL_KIT2 := $(addprefix $(KIT)/,$(addsuffix .REL,$(MODULES2)))

.PHONY: all rom check clean

all: rom

rom: $(OUT)/sdfpico.rom

check:
	$(if $(wildcard $(N80)),,$(error falta $(N80) — ver el encabezado de este Makefile))
	$(if $(wildcard $(LK80)),,$(error falta $(LK80) — ver el encabezado de este Makefile))
	$(if $(wildcard $(KIT)/DOSHEAD.REL),,$(error falta el MSX-DOS kit en $(KIT)/))
	$(if $(wildcard $(KIT)/BOOT.Z80),,$(error falta $(KIT)/BOOT.Z80 del MSX-DOS kit))
	$(info N80  $(shell $(N80) --version))
	$(info LK80 $(shell $(LK80) --version))
	$(info kit  $(words $(MODULES) $(MODULES2)) modulos en $(KIT)/)

$(OUT):
	@$(PYTHON) -c "import os; os.makedirs(r'$(OUT)', exist_ok=True)"

# 1. El driver, relocalizable (-l8c: simbolos de 8 caracteres, como M80).
$(OUT)/DSKDRV.REL: $(DRIVER) $(PROTO) $(KIT)/BOOT.Z80 | $(OUT)
	$(N80) $(DRIVER) $@ -bt rel -l8c -id $(KIT)

# 2. Link contra el kit. --data $(DATA_ORG2) antes de BASDATA es obligatorio:
#    ver el Makefile del SDF-1.
LK80_ARGS = --code $(CODE_ORG) --data $(DATA_ORG) \
            $(REL_KIT) $(OUT)/DSKDRV.REL --data $(DATA_ORG2) $(REL_KIT2) \
            --symbols-file $(OUT)/msxdos.sym \
            --output-format hex --output-file
$(OUT)/msxdos.hex: $(OUT)/DSKDRV.REL
	$(LK80) $(LK80_ARGS) $@

# 3. La pagina 2, binario absoluto en 8000h.
$(OUT)/page2.bin: $(PAGE2) $(PROTO) | $(OUT)
	$(N80) $(PAGE2) $@ -bt abs

# 4. La imagen de EEPROM de 64 KB, con la entrada DEVICE y el parche del reloj.
$(OUT)/sdfpico.rom: $(OUT)/msxdos.hex $(OUT)/page2.bin tools/mkrom.py tools/patch_rtc_kernel.py
	$(PYTHON) tools/mkrom.py $< $@ $(OUT)/page2.bin $(OUT)/msxdos.sym
	$(PYTHON) tools/patch_rtc_kernel.py $@ $(OUT)/msxdos.sym

clean:
	@$(PYTHON) -c "import shutil; shutil.rmtree(r'$(OUT)', ignore_errors=True)"
