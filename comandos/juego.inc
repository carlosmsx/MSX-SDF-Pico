; Motor de los juegos que corren en la Pico (CUEVA.COM).
;
; El programa no sabe nada del juego: en cada interrupcion lee la entrada,
; se la manda a la Pico y copia lo que vuelve:
;
;   11 bytes: los registros 0 a 10 del PSG
;   tramos:   dir (H, L), largo (L, H) y los bytes, a la VRAM desde dir
;             FEh, registro, valor: un registro del VDP
;             FFh: fin
;
; Los tramos van al VDP a 29 ciclos por byte, lo que acepta el TMS9918 con
; la pantalla encendida. ESC termina y muestra cuantos cuadros se perdieron.
;
; El que lo incluye define, antes del INCLUDE y del ORG:
;
;   CMD_INI, CMD_CUAD   los comandos del juego
;   MODO                el SCREEN (1 o 2)
;
; y despues:
;
;   Entrada             lee la entrada; Z si ESC esta apretada
;   Mandar              manda los parametros de CMD_CUAD
;   MsgViejo            el aviso de un firmware sin el juego

BDOS		EQU	0x0005
CALSLT		EQU	0x001C
CHGMOD		EQU	0x005F
KILBUF		EQU	0x0156
FORCLR		EQU	0xF3E9
SCRMOD		EQU	0xFCAF
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01

VDPCTRL		EQU	0x99
VDPDATA		EQU	0x98
PSGSEL		EQU	0xA0
PSGWR		EQU	0xA1
PSGRD		EQU	0xA2
PPIB		EQU	0xA9		; teclado: la fila elegida
PPIC		EQU	0xAA		; bits 0-3: la fila

PSGREGS		EQU	11

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	DE,MsgNoPico
	JP	Z,Fail

	CALL	Pantalla
	LD	A,CMD_INI
	CALL	SendCmd
	CALL	Wait
	DI
	CALL	Copiar
	EI
	LD	A,(Viejo)		; un firmware sin el juego contesta FFh
	OR	A
	LD	DE,MsgViejo
	JP	NZ,Fail

	HALT
	LD	HL,(JIFFY)
	LD	(T0),HL
	LD	HL,0
	LD	(Cuadros),HL
Loop:
	HALT				; el principio del borde de abajo
	CALL	Entrada
	JR	Z,Fin
	LD	A,CMD_CUAD
	CALL	SendCmd
	CALL	Mandar
	CALL	Wait
	DI
	CALL	Copiar
	EI
	LD	HL,(Cuadros)
	INC	HL
	LD	(Cuadros),HL
	JR	Loop

Fin:
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	LD	(Jiffies),HL
	CALL	Silencio
	CALL	Text
	LD	IX,KILBUF		; lo que se tecleo jugando
	CALL	Bios

	LD	HL,(Cuadros)
	CALL	PrintDec
	LD	DE,MsgCuadros
	CALL	Print
	LD	HL,(Jiffies)		; perdidos: los cuadros que no se atendieron
	LD	DE,(Cuadros)
	OR	A
	SBC	HL,DE
	JR	C,SinPerder
	DEC	HL			; el HALT del ESC no es un cuadro atendido
	BIT	7,H
	JR	Z,Perdidos
SinPerder:
	LD	HL,0
Perdidos:
	CALL	PrintDec
	LD	DE,MsgPerdidos
	CALL	Print
	JP	0

; ----------------------------------------------------------------------------
; Lo que manda la Pico

; Copiar: los registros del PSG y los tramos, hasta FFh. Sin interrupciones.
; El registro 7 tiene que tener el bit 7 en 1 y el 6 en 0 (el puerto A del
; PSG es el de los joysticks): si no, no se toca nada y Viejo queda en 1. Es
; lo que pasa con un firmware que no conoce el comando, que contesta FFh.
Copiar:
	LD	HL,Psg
	LD	BC,PSGREGS*256+SDFDATA
	INIR
	LD	A,(Psg+7)
	AND	0xC0
	CP	0x80
	LD	A,1
	LD	(Viejo),A
	RET	NZ
	XOR	A
	LD	(Viejo),A
	LD	HL,Psg
	LD	C,0
CpPsg:
	LD	A,C
	OUT	(PSGSEL),A
	LD	A,(HL)
	OUT	(PSGWR),A
	INC	HL
	INC	C
	LD	A,C
	CP	PSGREGS
	JR	NZ,CpPsg
CpTramo:
	IN	A,(SDFDATA)
	CP	0xFF
	RET	Z
	LD	D,A
	CP	0xFE
	JR	Z,CpReg
	IN	A,(SDFDATA)		; la direccion: D = H, E = L
	LD	E,A
	CALL	VdpWrite
	IN	A,(SDFDATA)		; el largo: B = H, A = L
	LD	C,A
	IN	A,(SDFDATA)
	LD	B,A
CpBloque:
	LD	A,B
	OR	A
	JR	Z,CpResto
	PUSH	BC
	CALL	PumpSafe
	POP	BC
	DEC	B
	JR	CpBloque
CpResto:
	LD	A,C			; menos de 256: se entra en la bomba
	OR	A			; 5 bytes de codigo antes del final
	JR	Z,CpTramo		; por cada uno
	LD	L,C
	LD	H,0
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,DE
	EX	DE,HL
	LD	HL,PumpEnd
	OR	A
	SBC	HL,DE
	CALL	JpHL
	JR	CpTramo
CpReg:
	IN	A,(SDFDATA)		; registro
	LD	D,A
	IN	A,(SDFDATA)		; valor
	OUT	(VDPCTRL),A
	LD	A,D
	OR	0x80
	OUT	(VDPCTRL),A
	JR	CpTramo

JpHL:
	JP	(HL)

; 29 ciclos por byte (IN, OUT y NOP con el wait de M1): lo minimo que acepta
; el TMS9918 con la pantalla encendida
PumpSafe:
	REPT	256
	IN	A,(SDFDATA)
	OUT	(VDPDATA),A
	NOP
	ENDM
PumpEnd:
	RET

; Silencio: los volumenes del PSG en 0
Silencio:
	DI
	LD	C,8
SiLoop:
	LD	A,C
	OUT	(PSGSEL),A
	XOR	A
	OUT	(PSGWR),A
	INC	C
	LD	A,C
	CP	11
	JR	NZ,SiLoop
	EI
	RET

; ----------------------------------------------------------------------------
; VDP y pantalla

; VdpWrite: escritura desde DE. Las interrupciones ya estan apagadas.
VdpWrite:
	LD	A,E
	OUT	(VDPCTRL),A
	LD	A,D
	OR	0x40
	OUT	(VDPCTRL),A
	RET

Pantalla:
	LD	HL,FORCLR		; los colores de DOS, para volver
	LD	DE,OldClr
	LD	BC,3
	LDIR
	LD	A,(SCRMOD)		; y el modo: TOTEXT no sale de SCREEN 1 ni 2
	LD	(OldMode),A
	LD	A,15
	LD	(FORCLR),A
	LD	A,1			; fondo y borde negros
	LD	(FORCLR+1),A
	LD	(FORCLR+2),A
	LD	A,MODO
	LD	IX,CHGMOD
	CALL	Bios
	LD	A,1
	LD	(InGfx),A
	RET

Text:
	LD	HL,OldClr
	LD	DE,FORCLR
	LD	BC,3
	LDIR
	LD	A,(OldMode)
	LD	IX,CHGMOD
	CALL	Bios
	XOR	A
	LD	(InGfx),A
	RET

LeaveGfx:
	LD	A,(InGfx)
	OR	A
	RET	Z
	CALL	Silencio
	JR	Text

; IX = rutina de la BIOS
Bios:
	LD	IY,(EXPTBL-1)
	CALL	CALSLT
	EI
	RET

; ----------------------------------------------------------------------------
; SDF-Pico

; SendCmd: el comando y ~60 us, como WriteCommand en la DiskROM
SendCmd:
	OUT	(SDFCMD),A
	PUSH	BC
	LD	B,16
ScDelay:
	DJNZ	ScDelay
	POP	BC
	RET

; Wait: OCUPADO en 0 (hasta unos 3 s). Si no, la Pico no esta.
Wait:
	PUSH	BC
	PUSH	DE
	LD	B,4
WaitOuter:
	LD	DE,0
WaitLoop:
	IN	A,(SDFCMD)
	CP	0xFF
	JR	Z,NoAnswer
	AND	0x80
	JR	Z,WaitOk
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,WaitLoop
	DJNZ	WaitOuter
NoAnswer:
	EI
	LD	DE,MsgNoPico
	JP	Fail
WaitOk:
	POP	DE
	POP	BC
	RET

; ----------------------------------------------------------------------------
; Texto

; PrintDec: HL en decimal, sin ceros a la izquierda
PrintDec:
	LD	B,0			; 1 = ya salio un digito
	LD	DE,10000
	CALL	Digit
	LD	DE,1000
	CALL	Digit
	LD	DE,100
	CALL	Digit
	LD	DE,10
	CALL	Digit
	LD	A,L
	ADD	A,'0'
	JP	PutChar
Digit:
	LD	A,'0'-1
DigLoop:
	INC	A
	OR	A
	SBC	HL,DE
	JR	NC,DigLoop
	ADD	HL,DE
	CP	'0'
	JR	NZ,DigOut
	INC	B
	DEC	B
	RET	Z
DigOut:
	LD	B,1
	PUSH	HL
	PUSH	BC
	CALL	PutChar
	POP	BC
	POP	HL
	RET

; PutChar: A a la pantalla. Respeta BC, DE y HL.
PutChar:
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,0x02
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	RET

; Print: el texto en DE, terminado en '$'. Respeta BC y HL.
Print:
	PUSH	BC
	PUSH	HL
	LD	C,0x09
	CALL	BDOS
	POP	HL
	POP	BC
	RET

Fail:
	PUSH	DE
	CALL	LeaveGfx
	POP	DE
	CALL	Print
	JP	0

MsgNoPico:	DB	"No contesta el SDF-Pico",13,10,'$'
MsgCuadros:	DB	" cuadros, perdidos: $"
MsgPerdidos:	DB	13,10,'$'

InGfx:		DB	0
OldMode:	DB	0
Viejo:		DB	0
OldClr:		DS	3
T0:		DW	0
Cuadros:	DW	0
Jiffies:	DW	0
Psg:		DS	PSGREGS
