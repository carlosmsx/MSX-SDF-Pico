; RDTEST.COM para MSX-DOS 1 con el SDF-Pico (docs/COMANDOS.md)
;
;   RDTEST [d:] [sector]
;
; Prueba de lectura: pide el mismo sector 200 veces por los puertos, igual que
; el DSKIO de la DiskROM (CMD_READ, WaitReady y dos INIR), sin pasar por el
; DOS ni por su bufer. Las primeras 100 con las interrupciones habilitadas y
; las otras 100 sin. Cada lectura se compara con la primera y, si difiere, se
; muestra desde que byte y los 4 bytes de cada una. d: es la unidad del
; SDF-Pico (A: o B:), A: si no se dice; el sector, 7 (el directorio de un
; disco de 720 KB).
;
; Despues de cada lectura manda CMD_DEBUG con el XOR del sector: la consola
; USB muestra "checksum: MSX xx, Pico yy" cuando lo que llego no es lo que
; la Pico tenia, aunque la primera lectura tambien haya salido mal.
;
; Se ensambla con PASMO:  pasmo rdtest.asm rdtest.com

BDOS		EQU	0x0005

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_DEBUG	EQU	0xD0
CMD_READ	EQU	0xF1
MEDIA_720K	EQU	0xF9
SECTOR		EQU	512
VUELTAS		EQU	100		; por modo

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	DE,MsgNoPico
	JP	Z,Fail

	; La linea de comandos (0080h: largo, 0081h: texto), terminada en 0
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0
	LD	HL,0x0081
	CALL	SkipSp
	JR	Z,ArgsOk
	INC	HL			; d: si el segundo caracter es ':'
	LD	A,(HL)
	DEC	HL
	CP	':'
	JR	NZ,ArgSector
	LD	A,(HL)
	AND	0xDF			; mayuscula
	SUB	'A'
	CP	2
	JP	NC,Usage
	LD	(Drive),A
	INC	HL
	INC	HL
	CALL	SkipSp
	JR	Z,ArgsOk
ArgSector:
	CALL	Number
	LD	(SecNum),DE
	CALL	SkipSp
	JP	NZ,Usage
ArgsOk:
	LD	DE,MsgLeo
	CALL	Print
	LD	HL,(SecNum)
	CALL	PrintDec
	LD	DE,MsgDe
	CALL	Print
	LD	A,(Drive)
	ADD	A,'A'
	CALL	PutChar
	LD	DE,MsgVeces
	CALL	Print

	; La referencia: la primera lectura
	CALL	ReadSector
	LD	HL,Buf
	LD	DE,Ref
	LD	BC,SECTOR
	LDIR

	LD	DE,MsgConInt
	CALL	Print
	CALL	Pasada
	LD	(MalEI),HL
	LD	A,1
	LD	(ConDI),A
	LD	DE,MsgSinInt
	CALL	Print
	CALL	Pasada
	LD	(MalDI),HL

	LD	DE,MsgResEI
	CALL	Print
	LD	HL,(MalEI)
	CALL	PrintDec
	LD	DE,MsgDe100
	CALL	Print
	LD	DE,MsgResDI
	CALL	Print
	LD	HL,(MalDI)
	CALL	PrintDec
	LD	DE,MsgDe100
	CALL	Print
	LD	DE,MsgFin
	CALL	Print
	JP	0

; Pasada: VUELTAS lecturas comparadas con Ref. Sale con HL = las distintas.
Pasada:
	LD	HL,0
	LD	(Malas),HL
	LD	HL,1
	LD	(Vuelta),HL
PasLoop:
	LD	C,0x0B			; CTRL+C: el DOS corta el programa
	CALL	BDOS
	OR	A
	JR	Z,PasRead
	LD	C,0x08
	CALL	BDOS
PasRead:
	CALL	ReadSector
	CALL	Compare
	JR	Z,PasNext
	CALL	ShowDiff
	LD	HL,(Malas)
	INC	HL
	LD	(Malas),HL
PasNext:
	LD	HL,(Vuelta)
	INC	HL
	LD	(Vuelta),HL
	LD	DE,VUELTAS+1
	OR	A
	SBC	HL,DE
	JR	NZ,PasLoop
	LD	HL,(Malas)
	RET

; ReadSector: el sector a Buf, como el READ de la DiskROM, y despues el
; checksum con CMD_DEBUG. Con ConDI en 1, sin interrupciones mientras dura.
ReadSector:
	LD	A,(ConDI)
	OR	A
	JR	Z,RsGo
	DI
RsGo:
	LD	A,CMD_READ
	CALL	SendCmd
	LD	A,(Drive)
	OUT	(SDFDATA),A
	LD	A,1			; un sector
	OUT	(SDFDATA),A
	LD	A,MEDIA_720K
	OUT	(SDFDATA),A
	LD	A,Buf / 256		; la direccion: el firmware no la usa
	OUT	(SDFDATA),A
	LD	A,Buf % 256
	OUT	(SDFDATA),A
	LD	HL,(SecNum)
	LD	A,H
	OUT	(SDFDATA),A
	LD	A,L
	OUT	(SDFDATA),A
	CALL	Wait
	IN	A,(SDFDATA)		; 0 o el error de DSKIO
	OR	A
	JR	NZ,RsError
	CALL	Wait
	LD	HL,Buf
	LD	BC,SDFDATA		; B = 0: 256 bytes por INIR
	INIR
	INIR
	EI

	LD	HL,Buf
	LD	BC,SECTOR
	LD	E,0
RsXor:
	LD	A,(HL)
	XOR	E
	LD	E,A
	INC	HL
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,RsXor
	LD	A,CMD_DEBUG
	CALL	SendCmd
	LD	A,E
	OUT	(SDFDATA),A
	RET
RsError:
	EI
	LD	DE,MsgError
	CALL	Print
	JP	0

; Compare: Buf contra Ref. Z si son iguales; si no, NZ y HL = el primer byte
; distinto de Buf.
Compare:
	LD	HL,Buf
	LD	DE,Ref
	LD	BC,SECTOR
CmpLoop:
	LD	A,(DE)
	CP	(HL)
	RET	NZ
	INC	HL
	INC	DE
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,CmpLoop
	RET

; ShowDiff: " 37 @157: 04 B2 8D 03 / BF 04 00 00" (la vuelta, el primer byte
; distinto, lo que llego y lo de la primera lectura), HL en Buf
ShowDiff:
	PUSH	HL
	LD	A,' '
	CALL	PutChar
	LD	HL,(Vuelta)
	CALL	PrintDec
	LD	DE,MsgDesde
	CALL	Print
	POP	HL
	PUSH	HL
	LD	DE,Buf
	OR	A
	SBC	HL,DE
	PUSH	HL
	CALL	PrintDec
	LD	A,':'
	CALL	PutChar
	POP	BC			; el desplazamiento
	POP	HL
	CALL	Show4
	LD	DE,MsgPrimera
	CALL	Print
	LD	HL,Ref
	ADD	HL,BC
	CALL	Show4
	LD	DE,MsgCierre
	JP	Print

; Show4: los 4 bytes desde HL, en hex. Respeta BC.
Show4:
	PUSH	BC
	LD	B,4
S4Loop:
	LD	A,' '
	CALL	PutChar
	LD	A,(HL)
	CALL	PrintHex
	INC	HL
	DJNZ	S4Loop
	POP	BC
	RET

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

; SkipSp: saltea espacios en HL. Z si llego al final.
SkipSp:
	LD	A,(HL)
	CP	' '
	JR	NZ,SkipEnd
	INC	HL
	JR	SkipSp
SkipEnd:
	OR	A
	RET

; Number: decimal en HL, hasta un espacio o el final; en DE
Number:
	LD	DE,0
	LD	C,0			; digitos leidos
NumLoop:
	LD	A,(HL)
	CP	' '
	JR	Z,NumEnd
	OR	A
	JR	Z,NumEnd
	SUB	'0'
	JP	C,Usage
	CP	10
	JP	NC,Usage
	PUSH	HL
	LD	H,D			; DE = DE * 10 + A
	LD	L,E
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,DE
	ADD	HL,HL
	LD	E,A
	LD	D,0
	ADD	HL,DE
	EX	DE,HL
	POP	HL
	INC	HL
	INC	C
	JR	NumLoop
NumEnd:
	LD	A,C
	OR	A
	JP	Z,Usage
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

; PrintHex: A en dos digitos
PrintHex:
	PUSH	AF
	RRCA
	RRCA
	RRCA
	RRCA
	CALL	Nibble
	POP	AF
Nibble:
	AND	0x0F
	ADD	A,'0'
	CP	'9'+1
	JR	C,PutChar
	ADD	A,'A'-'9'-1
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

Usage:
	LD	DE,MsgUsage
Fail:
	CALL	Print
	JP	0

MsgUsage:	DB	"Uso: RDTEST [d:] [sector]",13,10,'$'
MsgNoPico:	DB	"No contesta el SDF-Pico",13,10,'$'
MsgError:	DB	"Error de lectura",13,10,'$'
MsgLeo:		DB	"Sector $"
MsgDe:		DB	" de $"
MsgVeces:	DB	":, 2 x 100 lecturas",13,10
		DB	"vuelta @byte: llego / primera",13,10,'$'
MsgConInt:	DB	"Con interrupciones:",13,10,'$'
MsgSinInt:	DB	"Sin interrupciones (DI):",13,10,'$'
MsgDesde:	DB	" @$"
MsgPrimera:	DB	" /$"
MsgCierre:	DB	13,10,'$'
MsgResEI:	DB	"Con interrupciones: $"
MsgResDI:	DB	"Sin interrupciones: $"
MsgDe100:	DB	" de 100 mal",13,10,'$'
MsgFin:		DB	"Si fallan casi todas, la mala fue la",13,10
		DB	"primera lectura.",13,10,'$'

Drive:		DB	0
SecNum:		DW	7
ConDI:		DB	0
Vuelta:		DW	0
Malas:		DW	0
MalEI:		DW	0
MalDI:		DW	0

; Los bufers, despues del programa (no van en el .COM)
Buf		EQU	$
Ref		EQU	Buf + SECTOR
