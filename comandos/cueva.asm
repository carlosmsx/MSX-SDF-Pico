; CUEVA.COM para MSX-DOS 1 con el SDF-Pico (docs/CUEVA.md)
;
;   CUEVA
;
; Un juego que corre entero en la Pico (firmware/src/cueva.c). El motor
; (juego.inc) manda en cada cuadro el joystick 1 y las filas 5 (W, S y Z) y
; 2 (A y el punto) del teclado, y copia a la VRAM y al PSG lo que vuelve.
;
; Se ensambla con PASMO:  pasmo cueva.asm cueva.com

CMD_INI		EQU	0x07		; CMD_CUEVAINI
CMD_CUAD	EQU	0x08		; CMD_CUEVACUAD
MODO		EQU	1

	ORG	0x0100

	INCLUDE	"juego.inc"

; Entrada: D = registro 14 del PSG (joystick 1), E = fila 5 del teclado
; (W, S y Z), Fila2 = la fila 2 (A y el punto). Z si ESC esta apretada.
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
	LD	D,A
	IN	A,(PPIC)
	AND	0xF0
	OR	5
	OUT	(PPIC),A
	IN	A,(PPIB)
	LD	E,A
	IN	A,(PPIC)
	AND	0xF0
	OR	2
	OUT	(PPIC),A
	IN	A,(PPIB)
	LD	(Fila2),A
	IN	A,(PPIC)
	AND	0xF0
	OR	7			; fila 7: ESC es el bit 2
	OUT	(PPIC),A
	IN	A,(PPIB)
	EI
	AND	0x04
	RET

; Mandar: los parametros de CMD_CUEVACUAD
Mandar:
	LD	A,D
	OUT	(SDFDATA),A
	LD	A,E
	OUT	(SDFDATA),A
	LD	A,(Fila2)
	OUT	(SDFDATA),A
	RET

MsgViejo:	DB	"El firmware no tiene CUEVA (hace",13,10
		DB	"falta el 0.17 o posterior)",13,10,'$'
Fila2:		DB	0xFF
