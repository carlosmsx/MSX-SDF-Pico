; COPRO.COM para MSX-DOS 1 con el SDF-Pico (docs/COPRO.md)
;
;   COPRO
;
; Prueba de la Pico como coprocesador de juegos: la Pico calcula y el Z80
; solo copia al VDP lo que lee de 00h. Primero mide cuantos bytes por cuadro
; llegan, en tres casos, y los verifica:
;
;   - Solo el bus: INIR a la RAM, sin VDP.
;   - Al VDP con la pantalla apagada: IN y OUT seguidos, 24 ciclos por byte.
;   - Al VDP con la pantalla encendida: 29 ciclos por byte, lo minimo que
;     acepta el TMS9918 en SCREEN 2.
;
; Cada caso son 4 pedidos de 16 KB (64 KB): bytes por cuadro = 65536 / los
; cuadros que tardo (JIFFY). El ultimo pedido se compara con lo que tenia
; que llegar (CMD_COPROBLQ: el byte k del bloque j es k xor j xor semilla).
;
; Despues, 32 sprites de 16x16 que mueve la Pico: en cada cuadro el Z80 lee
; el joystick y la fila 8 del teclado, se los manda, y copia los 128 bytes
; de la tabla de atributos que vuelven. Los cursores o el joystick mueven la
; pelota amarilla; ESC termina y muestra los resultados.
;
; Se ensambla con PASMO:  pasmo copro.asm copro.com

BDOS		EQU	0x0005
CALSLT		EQU	0x001C
CHGMOD		EQU	0x005F
KILBUF		EQU	0x0156
TOTEXT		EQU	0x00D2
RG1SAV		EQU	0xF3E0
FORCLR		EQU	0xF3E9
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_COPROBLQ	EQU	0x04
CMD_COPROINI	EQU	0x05
CMD_COPROCUAD	EQU	0x06

VDPDATA		EQU	0x98
VDPCTRL		EQU	0x99
PSGSEL		EQU	0xA0
PSGWR		EQU	0xA1
PSGRD		EQU	0xA2
PPIB		EQU	0xA9		; teclado: la fila elegida
PPIC		EQU	0xAA		; bits 0-3: la fila

BLOQUES		EQU	64		; 16 KB por pedido
REPS		EQU	4		; 4 pedidos: 65536 bytes
SPRITES		EQU	32
SAT		EQU	0x1B00		; atributos de sprites en SCREEN 2
SPRPAT		EQU	0x3800		; patrones de sprites en SCREEN 2

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	DE,MsgNoPico
	JP	Z,Fail

	; La escena y el patron de la pelota. Un firmware sin COPRO no
	; contesta y se lee FFh (la primera fila de la pelota no es FFh).
	LD	A,CMD_COPROINI
	CALL	SendCmd
	LD	A,SPRITES
	OUT	(SDFDATA),A
	CALL	Wait
	LD	HL,Pelota
	LD	BC,32*256+SDFDATA
	INIR
	LD	A,(Pelota)
	CP	0xFF
	LD	DE,MsgViejo
	JP	Z,Fail

	LD	DE,MsgMido
	CALL	Print

	; 1. Solo el bus
	CALL	MideBus
	LD	(ResBus),HL
	LD	(ErrBus),DE

	; 2. Al VDP con la pantalla apagada
	CALL	Screen2
	LD	A,(RG1SAV)
	AND	0xBF
	CALL	WriteReg1
	LD	HL,PumpFast
	LD	A,0x10
	CALL	MideVdp
	LD	(ResOff),HL
	LD	(ErrOff),DE
	LD	A,(RG1SAV)
	CALL	WriteReg1

	; 3. Al VDP con la pantalla encendida
	LD	HL,PumpSafe
	LD	A,0x20
	CALL	MideVdp
	LD	(ResOn),HL
	LD	(ErrOn),DE

	; 4. Los sprites, hasta ESC
	CALL	Sprites
	CALL	Text
	LD	IX,KILBUF		; lo que se tecleo en la demo
	CALL	Bios

	LD	DE,MsgRes
	CALL	Print
	LD	DE,MsgBus
	LD	HL,(ResBus)
	LD	BC,(ErrBus)
	CALL	Linea
	LD	DE,MsgOff
	LD	HL,(ResOff)
	LD	BC,(ErrOff)
	CALL	Linea
	LD	DE,MsgOn
	LD	HL,(ResOn)
	LD	BC,(ErrOn)
	CALL	Linea

	LD	DE,MsgSpr
	CALL	Print
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

; Linea: DE = el texto, HL = bytes por cuadro, BC = errores
Linea:
	PUSH	BC
	PUSH	HL
	CALL	Print
	POP	HL
	CALL	PrintDec
	LD	DE,MsgErrores
	CALL	Print
	POP	HL
	CALL	PrintDec
	LD	DE,MsgCrLf
	JP	Print

; ----------------------------------------------------------------------------
; Mediciones

; MideBus: REPS pedidos con INIR a Buf. HL = bytes por cuadro, DE = errores
MideBus:
	CALL	Sync
	LD	C,0
MBRep:
	PUSH	BC
	LD	A,C
	CALL	PideBloques
	LD	HL,Buf
	LD	BC,SDFDATA		; B = 0: 256 bytes por INIR
	LD	E,BLOQUES
MBBlk:
	INIR
	DEC	E
	JR	NZ,MBBlk
	POP	BC
	INC	C
	LD	A,C
	CP	REPS
	JR	NZ,MBRep
	CALL	Tiempo
	PUSH	HL
	LD	A,REPS-1		; la semilla del ultimo pedido
	CALL	VerificaRam
	EX	DE,HL
	POP	HL
	RET

; MideVdp: REPS pedidos a la VRAM desde 0000h con la bomba de HL; A = la
; primera semilla. HL = bytes por cuadro, DE = errores
MideVdp:
	LD	(Bomba),HL
	LD	(Semilla),A
	CALL	Sync
	LD	C,0
MVRep:
	PUSH	BC
	LD	DE,0
	CALL	VdpWrite
	EI
	LD	A,(Semilla)
	ADD	A,C
	CALL	PideBloques
	LD	B,BLOQUES
MVBlk:
	PUSH	BC
	CALL	CallBomba
	POP	BC
	DJNZ	MVBlk
	POP	BC
	INC	C
	LD	A,C
	CP	REPS
	JR	NZ,MVRep
	CALL	Tiempo
	PUSH	HL
	LD	A,(Semilla)
	ADD	A,REPS-1
	CALL	VerificaVram
	EX	DE,HL
	POP	HL
	RET

CallBomba:
	LD	HL,(Bomba)
	JP	(HL)

; PideBloques: CMD_COPROBLQ con BLOQUES y la semilla en A
PideBloques:
	PUSH	AF
	LD	A,CMD_COPROBLQ
	CALL	SendCmd
	LD	A,BLOQUES
	OUT	(SDFDATA),A
	POP	AF
	OUT	(SDFDATA),A
	JP	Wait

; Sync: espera la proxima interrupcion y anota JIFFY
Sync:
	HALT
	LD	HL,(JIFFY)
	LD	(T0),HL
	RET

; Tiempo: HL = 65536 / los cuadros desde Sync (65535, en realidad)
Tiempo:
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	EX	DE,HL
	LD	HL,0xFFFF
	JP	Div

; VerificaRam: Buf contra la semilla A. HL = los bytes distintos
VerificaRam:
	LD	C,A
	LD	HL,Buf
	LD	IX,0
	LD	D,0			; el bloque
VRBlk:
	LD	E,0			; el byte del bloque
VRByte:
	LD	A,E
	XOR	D
	XOR	C
	CP	(HL)
	JR	Z,VROk
	INC	IX
VROk:
	INC	HL
	INC	E
	JR	NZ,VRByte
	INC	D
	LD	A,D
	CP	BLOQUES
	JR	NZ,VRBlk
	PUSH	IX
	POP	HL
	RET

; VerificaVram: los 16 KB de la VRAM contra la semilla A, sin interrupciones.
; Entre lectura y lectura pasan mas de 29 ciclos. HL = los bytes distintos
VerificaVram:
	LD	C,A
	DI
	XOR	A			; lectura desde 0000h
	OUT	(VDPCTRL),A
	OUT	(VDPCTRL),A
	LD	IX,0
	LD	D,0
VVBlk:
	LD	E,0
VVByte:
	IN	A,(VDPDATA)
	XOR	E
	XOR	D
	XOR	C
	JR	Z,VVOk
	INC	IX
VVOk:
	INC	E
	JR	NZ,VVByte
	INC	D
	LD	A,D
	CP	BLOQUES
	JR	NZ,VVBlk
	EI
	PUSH	IX
	POP	HL
	RET

; ----------------------------------------------------------------------------
; Sprites

Sprites:
	CALL	Screen2
	LD	A,(RG1SAV)		; sprites de 16x16
	OR	0x02
	LD	(RG1SAV),A
	CALL	WriteReg1
	LD	DE,SPRPAT		; la pelota que mando la Pico
	CALL	VdpWrite
	LD	HL,Pelota
	LD	BC,32*256+VDPDATA
	OTIR
	EI

	CALL	Sync
	LD	HL,0
	LD	(Cuadros),HL
SpLoop:
	HALT				; el principio del borde de abajo
	CALL	Entrada
	JR	Z,SpFin
	LD	A,CMD_COPROCUAD
	CALL	SendCmd
	LD	A,D
	OUT	(SDFDATA),A
	LD	A,E
	OUT	(SDFDATA),A
	CALL	Wait
	LD	DE,SAT
	CALL	VdpWrite
	CALL	PumpFast+(256-128)*4	; 128 bytes: todavia es el borde
	EI
	LD	HL,(Cuadros)
	INC	HL
	LD	(Cuadros),HL
	JR	SpLoop
SpFin:
	LD	A,(RG1SAV)		; sprites de 8x8, como estaban
	AND	0xFD
	LD	(RG1SAV),A
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	LD	(Jiffies),HL
	RET

; Entrada: D = registro 14 del PSG (joystick 1), E = fila 8 del teclado.
; Z si ESC esta apretada.
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
	OR	8
	OUT	(PPIC),A
	IN	A,(PPIB)
	LD	E,A
	IN	A,(PPIC)
	AND	0xF0
	OR	7			; fila 7: ESC es el bit 2
	OUT	(PPIC),A
	IN	A,(PPIB)
	EI
	AND	0x04
	RET

; ----------------------------------------------------------------------------
; Bombas: de 00h al VDP, desenrolladas. Cada byte son 4 bytes de codigo
; (5 en PumpSafe): se entra mas adelante para copiar menos de 256.

; 24 ciclos por byte (IN y OUT con el wait de M1): solo con la pantalla
; apagada o en el borde de abajo
PumpFast:
	REPT	256
	IN	A,(SDFDATA)
	OUT	(VDPDATA),A
	ENDM
	RET

; 29 ciclos por byte: con la pantalla encendida
PumpSafe:
	REPT	256
	IN	A,(SDFDATA)
	OUT	(VDPDATA),A
	NOP
	ENDM
	RET

; ----------------------------------------------------------------------------
; VDP y pantalla

; VdpWrite: escritura desde DE. Deja las interrupciones apagadas.
VdpWrite:
	DI
	LD	A,E
	OUT	(VDPCTRL),A
	LD	A,D
	OR	0x40
	OUT	(VDPCTRL),A
	RET

; WriteReg1: A al registro 1
WriteReg1:
	DI
	OUT	(VDPCTRL),A
	LD	A,0x81
	OUT	(VDPCTRL),A
	EI
	RET

Screen2:
	LD	A,(InGfx)
	OR	A
	JR	NZ,S2Mode
	LD	HL,FORCLR		; los colores de DOS, para volver
	LD	DE,OldClr
	LD	BC,3
	LDIR
S2Mode:
	LD	A,15
	LD	(FORCLR),A
	LD	A,1			; fondo y borde negros
	LD	(FORCLR+1),A
	LD	(FORCLR+2),A
	LD	A,2
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
	LD	IX,TOTEXT
	CALL	Bios
	XOR	A
	LD	(InGfx),A
	RET

LeaveGfx:
	LD	A,(InGfx)
	OR	A
	RET	Z
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
; Numeros y texto

; Div: HL = HL / DE (0 si DE es 0)
Div:
	LD	A,D
	OR	E
	JR	NZ,DvGo
	LD	HL,0
	RET
DvGo:
	LD	B,H
	LD	C,L
	LD	HL,0
	LD	A,16
DvLoop:
	SLA	C
	RL	B
	ADC	HL,HL
	OR	A
	SBC	HL,DE
	JR	C,DvBack
	INC	C
	JR	DvNext
DvBack:
	ADD	HL,DE
DvNext:
	DEC	A
	JR	NZ,DvLoop
	LD	H,B
	LD	L,C
	RET

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
MsgViejo:	DB	"El firmware no tiene COPRO (hace",13,10
		DB	"falta el 0.17 o posterior)",13,10,'$'
MsgMido:	DB	"Midiendo... despues, sprites:",13,10
		DB	"cursores o joystick, ESC termina.",13,10,'$'
MsgRes:		DB	"Bytes por cuadro, de la Pico:",13,10,'$'
MsgBus:		DB	"A la RAM (INIR):  $"
MsgOff:		DB	"VDP apagado, 24T: $"
MsgOn:		DB	"VDP, 29T:         $"
MsgErrores:	DB	", errores: $"
MsgSpr:		DB	"Sprites: $"
MsgCuadros:	DB	" cuadros, perdidos: $"
MsgPerdidos:
MsgCrLf:	DB	13,10,'$'

InGfx:		DB	0
OldClr:		DS	3
T0:		DW	0
Bomba:		DW	0
Semilla:	DB	0
ResBus:		DW	0
ErrBus:		DW	0
ResOff:		DW	0
ErrOff:		DW	0
ResOn:		DW	0
ErrOn:		DW	0
Cuadros:	DW	0
Jiffies:	DW	0
Pelota:		DS	32

; El bufer, despues del programa (no va en el .COM)
Buf		EQU	$
