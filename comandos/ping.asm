; PING.COM para MSX-DOS 1 con el SDF-Pico (docs/RED.md)
;
;   PING host
;
; Manda cuatro ecos ICMP, uno por segundo, como el ping de Windows. El nombre
; lo resuelve y el eco lo manda la Pico: aca solo se piden con CMD_PINGSEND y
; se consulta con CMD_PINGPOLL hasta que el resultado esta listo.
;
; Se ensambla con PASMO:  pasmo ping.asm ping.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_PINGSEND	EQU	0xB3
CMD_PINGPOLL	EQU	0xB4

COUNT		EQU	4		; ecos por corrida
HOSTMAX		EQU	60		; lo que entra en un comando del protocolo

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail

	; El nombre, de la linea de comandos (0080h: largo, 0081h: texto)
	LD	A,(0x0080)
	LD	B,A
	LD	HL,0x0081
SkipSp:
	LD	A,B
	OR	A
	JP	Z,Usage
	LD	A,(HL)
	CP	' '
	JR	NZ,CopyName
	INC	HL
	DEC	B
	JR	SkipSp
CopyName:
	LD	DE,Host
	LD	C,0
CopyLoop:
	LD	A,(HL)
	CP	' '
	JR	Z,CopyEnd
	CP	13
	JR	Z,CopyEnd
	OR	A
	JR	Z,CopyEnd
	LD	(DE),A
	INC	DE
	INC	HL
	INC	C
	LD	A,C
	CP	HOSTMAX
	JR	NC,CopyEnd
	DJNZ	CopyLoop
CopyEnd:
	XOR	A
	LD	(DE),A
	LD	A,C
	LD	(HostLen),A
	OR	A
	JP	Z,Usage

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

	LD	HL,MsgPing
	CALL	PrStr
	CALL	PrHost
	LD	HL,MsgPing2
	CALL	PrStr

	LD	A,COUNT
	LD	(Left),A
Again:
	CALL	PingOnce
	OR	A
	JR	NZ,Summary		; error que corta (nombre, Wi-Fi)
	LD	HL,Left
	DEC	(HL)
	JR	Z,Summary
	CALL	Delay1s
	JR	Again

Summary:
	LD	HL,MsgStats1		; "Enviados = "
	CALL	PrStr
	LD	A,(Sent)
	CALL	PrDec8
	LD	HL,MsgStats2		; ", recibidos = "
	CALL	PrStr
	LD	A,(Recvd)
	CALL	PrDec8
	LD	HL,MsgStats3		; ", perdidos = "
	CALL	PrStr
	LD	A,(Recvd)
	LD	B,A
	LD	A,(Sent)
	SUB	B
	LD	(Lost),A
	CALL	PrDec8
	LD	HL,MsgStats4		; " ("
	CALL	PrStr
	; porcentaje = perdidos * 100 / enviados
	LD	HL,0
	LD	A,(Lost)
	OR	A
	JR	Z,PctDone
	LD	B,A
	LD	DE,100
PctMul:
	ADD	HL,DE
	DJNZ	PctMul
PctDone:
	LD	A,(Sent)
	LD	C,A
	CALL	Div16By8
	CALL	PrDec16
	LD	HL,MsgStats5		; "% de perdida)"
	CALL	PrStr

	LD	A,(Recvd)
	OR	A
	JR	Z,Exit
	LD	HL,MsgTimes1		; "Tiempos: minimo = "
	CALL	PrStr
	LD	HL,(RttMin)
	CALL	PrMs
	LD	HL,MsgTimes2		; ", maximo = "
	CALL	PrStr
	LD	HL,(RttMax)
	CALL	PrMs
	LD	HL,MsgTimes3		; ", promedio = "
	CALL	PrStr
	LD	HL,(RttSum)
	LD	A,(Recvd)
	LD	C,A
	CALL	Div16By8
	CALL	PrMs
	LD	HL,MsgCrLf
	CALL	PrStr
Exit:
	JP	0			; vuelve al DOS

Usage:
	LD	HL,MsgUsage
Fail:
	CALL	PrStr
	JP	0

; ----------------------------------------------------------------------------
; PingOnce, un eco: pide, espera el resultado y lo muestra
; out: A = 0 sigue, 1 hay que cortar
PingOnce:
	LD	HL,Sent
	INC	(HL)
POSend:
	CALL	SendReq
	CP	1			; ocupada: hay un eco anterior en curso
	JR	NZ,POSent
	CALL	WaitDone
	JR	POSend
POSent:
	OR	A
	JR	Z,POWait
	LD	HL,MsgBadName		; el nombre no entra
	CALL	PrStr
	LD	A,1
	RET
POWait:
	CALL	WaitDone
	LD	A,(Result+1)		; resultado
	OR	A
	JR	Z,POReply
	CP	1
	JR	Z,POTimeout
	CP	2
	LD	HL,MsgNoName
	JR	Z,POFatal
	LD	HL,MsgNoWifi
POFatal:
	CALL	PrStr
	LD	A,1
	RET
POTimeout:
	LD	HL,MsgTimeout
	CALL	PrStr
	XOR	A
	RET
POReply:
	LD	HL,Recvd
	INC	(HL)
	LD	HL,(Result+6)		; tiempo en ms
	LD	DE,(RttMin)
	OR	A
	SBC	HL,DE
	JR	NC,NoMin
	LD	HL,(Result+6)
	LD	(RttMin),HL
NoMin:
	LD	HL,(Result+6)
	LD	DE,(RttMax)
	OR	A
	SBC	HL,DE
	JR	C,NoMax
	LD	HL,(Result+6)
	LD	(RttMax),HL
NoMax:
	LD	HL,(Result+6)
	LD	DE,(RttSum)
	ADD	HL,DE
	LD	(RttSum),HL

	LD	HL,MsgReply1		; "Respuesta de "
	CALL	PrStr
	LD	A,(Result+2)
	CALL	PrDec8
	LD	A,'.'
	CALL	PrChar
	LD	A,(Result+3)
	CALL	PrDec8
	LD	A,'.'
	CALL	PrChar
	LD	A,(Result+4)
	CALL	PrDec8
	LD	A,'.'
	CALL	PrChar
	LD	A,(Result+5)
	CALL	PrDec8
	LD	HL,MsgReply2		; ": tiempo "
	CALL	PrStr
	LD	HL,(Result+6)
	CALL	PrMs
	LD	HL,MsgReply3		; " TTL="
	CALL	PrStr
	LD	A,(Result+8)
	CALL	PrDec8
	LD	HL,MsgCrLf
	CALL	PrStr
	XOR	A
	RET

; ----------------------------------------------------------------------------
; La Pico

; Wait, espera a que la Pico baje OCUPADO (bit 7 del estado), unos 0,7 s
; out: carry si no lo hizo, o si el bus esta al aire
Wait:
	LD	DE,0
WaitLoop:
	IN	A,(SDFCMD)
	CP	0xFF
	JR	Z,WaitFail
	AND	0x80
	RET	Z
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,WaitLoop
WaitFail:
	SCF
	RET

; SendReq, CMD_PINGSEND con el nombre
; out: A = 0 aceptado, 1 ocupada, 2 nombre no valido
SendReq:
	DI
	LD	A,CMD_PINGSEND
	OUT	(SDFCMD),A
	LD	A,(HostLen)
	OUT	(SDFDATA),A
	LD	B,A
	LD	HL,Host
	LD	C,SDFDATA
	OTIR
	CALL	Wait
	JR	C,NoAnswer
	IN	A,(SDFDATA)
	EI
	RET

NoAnswer:
	EI
	LD	HL,MsgNoPico
	JP	Fail

; Poll, CMD_PINGPOLL: llena Result con los 9 bytes
Poll:
	DI
	LD	A,CMD_PINGPOLL
	OUT	(SDFCMD),A
	LD	B,25			; ~70 us para que la Pico levante OCUPADO
PollDelay:
	DJNZ	PollDelay
	CALL	Wait
	JR	C,NoAnswer
	LD	HL,Result
	LD	B,9
	LD	C,SDFDATA
	INIR
	EI
	RET

; WaitDone, consulta hasta que el eco termino (la Pico da por perdido a los
; 3 s; aca se espera hasta 10 por si acaso)
WaitDone:
	LD	HL,(JIFFY)
	LD	(T0),HL
WDLoop:
	CALL	CheckKey
	CALL	Poll
	LD	A,(Result)
	CP	2			; PING_LISTO
	RET	Z
	; transcurrido = JIFFY - T0; limite = Hz * 10
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	EX	DE,HL			; DE = transcurrido
	LD	A,(Hz)
	LD	L,A
	LD	H,0
	LD	B,H			; BC = Hz
	LD	C,L
	ADD	HL,HL			; x2
	ADD	HL,HL			; x4
	ADD	HL,BC			; x5
	ADD	HL,HL			; x10
	EX	DE,HL
	OR	A
	SBC	HL,DE			; transcurrido - limite
	JR	C,WDLoop
	LD	A,2			; PING_LISTO, PING_TIEMPO
	LD	(Result),A
	LD	A,1
	LD	(Result+1),A
	RET

; ----------------------------------------------------------------------------
; Tiempo y teclado

Delay1s:
	LD	HL,(JIFFY)
	LD	(T0),HL
DelLoop:
	CALL	CheckKey
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	LD	A,(Hz)
	LD	C,A
	LD	B,0
	OR	A
	SBC	HL,BC
	JR	C,DelLoop
	RET

; CheckKey: si hay una tecla la lee (el DOS corta el programa con CTRL+C)
CheckKey:
	LD	C,0x0B
	CALL	BDOS
	OR	A
	RET	Z
	LD	C,0x08
	JP	BDOS

; ----------------------------------------------------------------------------
; Pantalla

PrChar:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,2
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	POP	AF
	RET

PrStr:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	PUSH	HL
	EX	DE,HL
	LD	C,9
	CALL	BDOS
	POP	HL
	POP	DE
	POP	BC
	POP	AF
	RET

PrHost:
	LD	HL,Host
PrHostLoop:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PrChar
	INC	HL
	JR	PrHostLoop

; PrMs, HL milisegundos: "<1 ms" si es 0
PrMs:
	LD	A,H
	OR	L
	JR	NZ,PrMsNum
	LD	A,'<'
	CALL	PrChar
	LD	A,'1'
	CALL	PrChar
	JR	PrMsUnit
PrMsNum:
	CALL	PrDec16
PrMsUnit:
	LD	HL,MsgMs
	JP	PrStr

PrDec8:
	LD	L,A
	LD	H,0
PrDec16:
	LD	B,0			; 0 = todavia no salio ningun digito
	LD	DE,10000
	CALL	PrDigit
	LD	DE,1000
	CALL	PrDigit
	LD	DE,100
	CALL	PrDigit
	LD	DE,10
	CALL	PrDigit
	LD	A,L
	ADD	A,'0'
	JP	PrChar

PrDigit:
	XOR	A
PrDigitSub:
	OR	A
	SBC	HL,DE
	JR	C,PrDigitEnd
	INC	A
	JR	PrDigitSub
PrDigitEnd:
	ADD	HL,DE
	OR	A
	JR	NZ,PrDigitOut
	LD	A,B
	OR	A
	RET	Z			; cero a la izquierda
	XOR	A
PrDigitOut:
	LD	B,1
	ADD	A,'0'
	JP	PrChar

; Div16By8: HL = HL / C (C distinto de 0)
Div16By8:
	LD	B,0
	LD	DE,0
Div16Loop:
	OR	A
	SBC	HL,BC
	JR	C,Div16End
	INC	DE
	JR	Div16Loop
Div16End:
	EX	DE,HL
	RET

; ----------------------------------------------------------------------------
; Textos y variables

MsgUsage:	DB	"Uso: PING host",13,10,'$'
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgBadName:	DB	"El nombre es demasiado largo.",13,10,'$'
MsgNoName:	DB	"No se pudo resolver el nombre.",13,10,'$'
MsgNoWifi:	DB	"La Pico no esta conectada al Wi-Fi.",13,10,'$'
MsgPing:	DB	"Haciendo ping a $"
MsgPing2:	DB	":",13,10,'$'
MsgReply1:	DB	"Respuesta de $"
MsgReply2:	DB	": tiempo $"
MsgReply3:	DB	" TTL=$"
MsgTimeout:	DB	"Tiempo de espera agotado.",13,10,'$'
MsgStats1:	DB	13,10,"Enviados = $"
MsgStats2:	DB	", recibidos = $"
MsgStats3:	DB	", perdidos = $"
MsgStats4:	DB	" ($"
MsgStats5:	DB	"% de perdida)",13,10,'$'
MsgTimes1:	DB	"Tiempos: minimo = $"
MsgTimes2:	DB	", maximo = $"
MsgTimes3:	DB	", promedio = $"
MsgMs:		DB	" ms$"
MsgCrLf:	DB	13,10,'$'

HostLen:	DB	0
Hz:		DB	60
Left:		DB	0
Sent:		DB	0
Recvd:		DB	0
Lost:		DB	0
T0:		DW	0
RttMin:		DW	0xFFFF
RttMax:		DW	0
RttSum:		DW	0
Result:	DS	9,0
Host:		DS	HOSTMAX+1,0
