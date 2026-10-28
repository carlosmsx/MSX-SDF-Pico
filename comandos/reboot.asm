; REBOOT.COM para MSX-DOS 1 (docs/COMANDOS.md)
;
;   REBOOT
;
; Reinicia el MSX como el boton de reset: salta a 0000h de la BIOS, que
; arranca todo de nuevo (slots, RAM, cartuchos y el DOS). No usa la Pico y
; anda en cualquier MSX.
;
; Se ensambla con PASMO:  pasmo reboot.asm reboot.com

CALSLT		EQU	0x001C
EXPTBL		EQU	0xFCC1

	ORG	0x0100

Start:
	; Llamada entre slots a 0000h del slot de la BIOS: CALSLT pone la BIOS
	; en la pagina 0 y salta. No vuelve.
	DI
	LD	A,(EXPTBL)
	LD	IYH,A			; IYH = slot de la BIOS
	LD	IX,0x0000
	JP	CALSLT
