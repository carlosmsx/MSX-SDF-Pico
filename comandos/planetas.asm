; PLANETAS.COM para MSX-DOS 1 con el SDF-Pico (docs/PLANETAS.md)
;
;   PLANETAS
;
; El sistema solar con las orbitas reales, acelerado. Lo calcula y lo dibuja
; la Pico (firmware/src/planetas.c); el motor (juego.inc) manda en cada
; cuadro el joystick 1 y las filas 0 a 8 del teclado, y copia lo que vuelve.
;
; Se ensambla con PASMO:  pasmo planetas.asm planetas.com

CMD_INI		EQU	0x09		; CMD_PLANETASINI
CMD_CUAD	EQU	0x0A		; CMD_PLANETASCUAD
MODO		EQU	2

	ORG	0x0100

	INCLUDE	"juego.inc"

; Entrada: Teclas = el registro 14 del PSG (joystick 1) y las filas 0 a 8
; del teclado. Z si ESC (fila 7, bit 2) esta apretada.
Entrada:
	DI
	LD	A,15
	OUT	(PSGSEL),A
	IN	A,(PSGRD)
	AND	0xBF			; bit 6 en 0: el joystick 1
	OUT	(PSGWR),A
	LD	A,14
	OUT	(PSGSEL),A
	IN	A,(PSGRD)
	LD	HL,Teclas
	LD	(HL),A
	LD	B,0
EnFila:
	INC	HL
	IN	A,(PPIC)
	AND	0xF0
	OR	B
	OUT	(PPIC),A
	IN	A,(PPIB)
	LD	(HL),A
	INC	B
	LD	A,B
	CP	9
	JR	NZ,EnFila
	EI
	LD	A,(Teclas+8)
	AND	0x04
	RET

; Mandar: los parametros de CMD_PLANETASCUAD
Mandar:
	LD	HL,Teclas
	LD	BC,10*256+SDFDATA
	OTIR
	RET

MsgViejo:	DB	"El firmware no tiene PLANETAS (hace",13,10
		DB	"falta el 0.17 o posterior)",13,10,'$'
Teclas:		DS	10
