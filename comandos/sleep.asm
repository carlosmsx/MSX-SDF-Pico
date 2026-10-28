; SLEEP.COM para MSX-DOS 1 (docs/COMANDOS.md)
;
;   SLEEP segundos
;
; Espera esa cantidad de segundos (0 a 65535) y vuelve al DOS. Sirve en los
; .BAT, entre un comando y otro. Cuenta con JIFFY, que la interrupcion del VDP
; incrementa 50 o 60 veces por segundo: no usa la Pico y anda en cualquier MSX.
; CTRL+C corta la espera.
;
; Se ensambla con PASMO:  pasmo sleep.asm sleep.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E

	ORG	0x0100

Start:
	; Los segundos, de la linea de comandos (0080h: largo, 0081h: texto)
	LD	A,(0x0080)
	LD	B,A
	LD	HL,0x0081
SkipSp:
	LD	A,B
	OR	A
	JP	Z,Usage
	LD	A,(HL)
	CP	' '
	JR	NZ,Number
	INC	HL
	DEC	B
	JR	SkipSp

	; Decimal, hasta un espacio o el final; en DE
Number:
	LD	DE,0
	LD	C,0			; digitos leidos
NumLoop:
	LD	A,(HL)
	CP	' '
	JR	Z,NumEnd
	CP	13
	JR	Z,NumEnd
	OR	A
	JR	Z,NumEnd
	SUB	'0'
	JP	C,Usage
	CP	10
	JP	NC,Usage
	PUSH	HL
	LD	H,D			; DE = DE * 10 + A, sin pasarse de 16 bits
	LD	L,E
	ADD	HL,HL
	JR	C,TooBig
	ADD	HL,HL
	JR	C,TooBig
	ADD	HL,DE
	JR	C,TooBig
	ADD	HL,HL
	JR	C,TooBig
	LD	E,A
	LD	D,0
	ADD	HL,DE
	JR	C,TooBig
	EX	DE,HL
	POP	HL
	INC	HL
	INC	C
	DJNZ	NumLoop
NumEnd:
	LD	A,C
	OR	A
	JP	Z,Usage
	LD	(Secs),DE

	; Cuantos jiffies tiene un segundo: bit 7 de 002Bh, 1 = 50 Hz
	LD	HL,0x002B
	LD	A,(EXPTBL)
	CALL	RDSLT
	EI
	AND	0x80
	LD	A,60
	JR	Z,Hz60
	LD	A,50
Hz60:
	LD	(Hz),A

	; Un segundo por vuelta. T0 avanza de a un segundo justo, sin tomar
	; JIFFY de nuevo, para que el tiempo del bucle no se acumule.
	LD	HL,(JIFFY)
	LD	(T0),HL
SecLoop:
	LD	HL,(Secs)
	LD	A,H
	OR	L
	JP	Z,0			; listo: vuelve al DOS
	DEC	HL
	LD	(Secs),HL
	LD	A,(Hz)
	LD	E,A
	LD	D,0
	LD	HL,(T0)
	ADD	HL,DE
	LD	(T0),HL
Wait:
	CALL	CheckKey
	LD	HL,(JIFFY)		; hasta que JIFFY - T0 deje de ser negativo
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	BIT	7,H
	JR	NZ,Wait
	JR	SecLoop

TooBig:
	POP	HL
Usage:
	LD	DE,MsgUsage
	LD	C,9
	CALL	BDOS
	JP	0

; CheckKey: si hay una tecla la lee (el DOS corta el programa con CTRL+C)
CheckKey:
	LD	C,0x0B
	CALL	BDOS
	OR	A
	RET	Z
	LD	C,0x08
	JP	BDOS

MsgUsage:	DB	"Uso: SLEEP segundos (0 a 65535)",13,10,'$'

Secs:		DW	0
T0:		DW	0
Hz:		DB	0
