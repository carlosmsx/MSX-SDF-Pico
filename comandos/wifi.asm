; WIFI.COM para MSX-DOS 1 con el SDF-Pico (docs/WIFI.md)
;
;   WIFI        busca las redes, las lista numeradas y conecta a la elegida
;   WIFI /S     solo muestra el estado
;
; Lo mismo que CALL WIFI, en modo texto: el firmware da el estado, busca las
; redes y conecta (CMD_WIFISTAT, CMD_WIFISCAN, CMD_WIFIREDES y CMD_WIFIJOIN);
; aca solo se arma la pantalla y se piden la red y la clave. Si conecta, la
; Pico guarda la red, igual que con CALL WIFI.
;
; Se ensambla con PASMO:  pasmo wifi.asm wifi.com

BDOS		EQU	0x0005
JIFFY		EQU	0xFC9E

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_WIFISTAT	EQU	0xB5
CMD_WIFISCAN	EQU	0xB6
CMD_WIFIREDES	EQU	0xB7
CMD_WIFIJOIN	EQU	0xB8

; Estado del Wi-Fi, primer byte de CMD_WIFISTAT (wifi.h)
W_APAGADO	EQU	0
W_SIN_RED	EQU	1
W_CONECTANDO	EQU	2
W_CONECTADO	EQU	3
W_ESPERA	EQU	4

; Marcas de cada red en CMD_WIFIREDES
F_ABIERTA	EQU	0x01
F_GUARDADA	EQU	0x02

REDESMAX	EQU	16		; las que guarda el firmware
TXTMAX		EQU	40		; tope de cada texto que llega
CLAVEMIN	EQU	8
CLAVEMAX	EQU	63

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail

	; La linea de comandos (0080h: largo, 0081h: texto): nada o /S
	LD	A,(0x0080)
	LD	B,A
	LD	HL,0x0081
SkipSp:
	LD	A,B
	OR	A
	JR	Z,ArgsDone
	LD	A,(HL)
	CP	' '
	JR	NZ,Option
	INC	HL
	DEC	B
	JR	SkipSp
Option:
	CP	'/'
	JP	NZ,Usage
	INC	HL
	LD	A,(HL)
	AND	0xDF			; a mayuscula
	CP	'S'
	JP	NZ,Usage
	LD	A,1
	LD	(OnlyStat),A
ArgsDone:

	CALL	GetStat
	CALL	ShowStat
	LD	A,(OnlyStat)
	OR	A
	JP	NZ,Exit
	LD	A,(StEstado)
	CP	W_APAGADO
	JP	Z,Exit

	; Buscar
	LD	A,CMD_WIFISCAN
	CALL	Begin
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	OR	A
	LD	HL,MsgNoScan
	JP	NZ,Fail
	LD	HL,MsgScan		; "Buscando redes..."
	CALL	PrStr
	LD	B,80			; unos 20 s, de a un cuarto
ScanWait:
	PUSH	BC
	LD	A,15
	CALL	Delay
	CALL	GetNets
	POP	BC
	LD	A,(Busy)
	OR	A
	JR	Z,ScanDone
	DJNZ	ScanWait
	LD	HL,MsgScanLate
	JP	Fail
ScanDone:
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	A,(NNets)
	OR	A
	LD	HL,MsgNoNets
	JP	Z,Fail

	; La lista: " 1  -48 dBm  Casa (abierta)"
	LD	HL,NetBuf
	LD	C,1
ListLoop:
	LD	A,C
	CP	10
	JR	NC,ListNum
	LD	A,' '
	CALL	PrChar
ListNum:
	LD	A,C
	CALL	PrDec8
	LD	A,' '
	CALL	PrChar
	LD	A,(HL)			; senal
	INC	HL
	CALL	PrRssi
	LD	A,(HL)			; marcas
	INC	HL
	LD	(Flags),A
	PUSH	HL
	LD	HL,MsgDbm		; " dBm  "
	CALL	PrStr
	POP	HL
	CALL	PrZ			; el nombre; HL queda en la red siguiente
	PUSH	HL
	LD	A,(Flags)
	AND	F_ABIERTA
	LD	HL,MsgOpen
	CALL	NZ,PrStr
	LD	A,(Flags)
	AND	F_GUARDADA
	LD	HL,MsgSaved
	CALL	NZ,PrStr
	LD	HL,MsgCrLf
	CALL	PrStr
	POP	HL
	LD	A,(NNets)
	CP	C
	JR	Z,Ask
	INC	C
	JR	ListLoop

	; Cual: el numero por la linea del DOS (funcion 0Ah)
Ask:
	LD	HL,MsgAsk1		; "Red (1-"
	CALL	PrStr
	LD	A,(NNets)
	CALL	PrDec8
	LD	HL,MsgAsk2		; ", RETURN sale): "
	CALL	PrStr
	LD	A,2
	LD	(InBuf),A
	LD	DE,InBuf
	LD	C,0x0A
	CALL	BDOS
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	A,(InBuf+1)
	OR	A
	JP	Z,Exit
	LD	B,A
	LD	HL,InBuf+2
	LD	C,0			; el numero
AskDigit:
	LD	A,(HL)
	SUB	'0'
	JR	C,AskBad
	CP	10
	JR	NC,AskBad
	LD	E,A
	LD	A,C			; C = C * 10 + digito
	ADD	A,A
	LD	D,A
	ADD	A,A
	ADD	A,A
	ADD	A,D
	ADD	A,E
	LD	C,A
	INC	HL
	DJNZ	AskDigit
	LD	A,C
	OR	A
	JR	Z,AskBad
	LD	A,(NNets)
	CP	C
	JR	NC,Chosen
AskBad:
	LD	HL,MsgBadNum
	CALL	PrStr
	JR	Ask

	; La red C (desde 1): nombre y marcas
Chosen:
	LD	HL,NetBuf
ChSkip:
	DEC	C
	JR	Z,ChFound
	INC	HL			; senal
	INC	HL			; marcas
ChSkipName:
	LD	A,(HL)
	INC	HL
	OR	A
	JR	NZ,ChSkipName
	JR	ChSkip
ChFound:
	INC	HL
	LD	A,(HL)
	LD	(Flags),A
	INC	HL
	LD	DE,Ssid
	LD	B,0
ChCopy:
	LD	A,(HL)
	OR	A
	JR	Z,ChCopied
	LD	(DE),A
	INC	HL
	INC	DE
	INC	B
	JR	ChCopy
ChCopied:
	XOR	A
	LD	(DE),A
	LD	A,B
	LD	(SsidLen),A

	XOR	A
	LD	(KeyLen),A
	LD	A,(Flags)
	AND	F_ABIERTA
	JR	NZ,Join
AskKey:
	LD	HL,MsgKey		; "Clave (ESC vuelve): "
	CALL	PrStr
	CALL	ReadKey
	JP	C,Ask
	LD	A,(KeyLen)
	CP	CLAVEMIN
	JR	NC,Join
	LD	HL,MsgShortKey
	CALL	PrStr
	JR	AskKey

	; Conectar: largo, nombre, largo, clave
Join:
	LD	A,CMD_WIFIJOIN
	CALL	Begin
	LD	A,(SsidLen)
	OUT	(SDFDATA),A
	LD	B,A
	LD	HL,Ssid
	LD	C,SDFDATA
	OTIR
	LD	A,(KeyLen)
	OUT	(SDFDATA),A
	OR	A
	JR	Z,JoinSent		; OTIR con B = 0 serian 256
	LD	B,A
	LD	HL,Key
	OTIR
JoinSent:
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	PUSH	AF
	CALL	WipeKey
	POP	AF
	OR	A
	JR	Z,Joining
	CP	1
	LD	HL,MsgNoWifi
	JP	Z,Fail
	LD	HL,MsgBadJoin
	JP	Fail

Joining:
	LD	HL,MsgJoin		; "Conectando a "
	CALL	PrStr
	LD	HL,Ssid
	CALL	PrZ
	LD	B,100			; unos 50 s; la Pico se rinde a los 30
JoinWait:
	PUSH	BC
	LD	A,30
	CALL	Delay
	CALL	GetStat
	POP	BC
	LD	A,(StResult)
	OR	A
	JR	NZ,Joined
	BIT	0,B			; un punto por segundo
	LD	A,'.'
	CALL	Z,PrChar
	DJNZ	JoinWait
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,MsgJoinLate
	JP	Fail
Joined:
	LD	HL,MsgCrLf
	CALL	PrStr
	CP	1
	JR	NZ,JoinFail
	LD	HL,MsgOk		; "Conectado. IP "
	CALL	PrStr
	LD	HL,StIp
	CALL	PrZ
	LD	HL,MsgOk2		; "La red quedo guardada."
	CALL	PrStr
	JR	Exit
JoinFail:
	LD	HL,MsgFail		; "No se pudo conectar:"
	CALL	PrStr
	LD	HL,StMotivo
	CALL	PrZ
	LD	HL,MsgCrLf
	CALL	PrStr
Exit:
	CALL	WipeKey
	JP	0			; vuelve al DOS

Usage:
	LD	HL,MsgUsage
Fail:
	CALL	PrStr
	JR	Exit

; ----------------------------------------------------------------------------
; Estado

; ShowStat, una linea con el estado de GetStat
ShowStat:
	LD	A,(StEstado)
	CP	W_APAGADO
	LD	HL,MsgOff
	JP	Z,PrStr
	CP	W_SIN_RED
	LD	HL,MsgNoNet
	JP	Z,PrStr
	CP	W_CONECTANDO
	JR	Z,SSConnecting
	CP	W_CONECTADO
	JR	Z,SSConnected
	LD	HL,MsgWaiting		; "Sin conexion a "
	CALL	PrStr
	CALL	PrStRed
	LD	HL,MsgCrLf
	JP	PrStr
SSConnecting:
	LD	HL,MsgJoin		; "Conectando a "
	CALL	PrStr
	CALL	PrStRed
	LD	HL,MsgDots
	JP	PrStr
SSConnected:
	LD	HL,MsgOn		; "Conectado a "
	CALL	PrStr
	CALL	PrStRed
	LD	HL,MsgOnIp		; ", IP "
	CALL	PrStr
	LD	HL,StIp
	CALL	PrZ
	LD	HL,MsgComma
	CALL	PrStr
	LD	A,(StRssi)
	CALL	PrRssi
	LD	HL,MsgDbmCrLf
	JP	PrStr

PrStRed:
	LD	HL,StRed
	JP	PrZ

; ----------------------------------------------------------------------------
; La Pico

; Begin, A = comando: lo manda con las interrupciones cortadas. Siguen los
; parametros, Finish y la lectura de la respuesta, y despues EI.
Begin:
	DI
	OUT	(SDFCMD),A
	RET

; Finish, espera la respuesta. Estos comandos los atiende el nucleo 0 de la
; Pico, que puede tardar mas que el 1: hasta unos 3 s.
Finish:
	LD	B,25			; ~70 us para que la Pico levante OCUPADO
FinDelay:
	DJNZ	FinDelay
	LD	B,4
FinOuter:
	LD	DE,0
FinLoop:
	IN	A,(SDFCMD)
	CP	0xFF
	JR	Z,NoAnswer
	AND	0x80
	RET	Z
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,FinLoop
	DJNZ	FinOuter
NoAnswer:
	EI
	LD	HL,MsgNoPico
	JP	Fail

; GetStat, CMD_WIFISTAT: estado, resultado, senal, red, IP, motivo
GetStat:
	LD	A,CMD_WIFISTAT
	CALL	Begin
	CALL	Finish
	LD	HL,StEstado
	LD	BC,3*256+SDFDATA
	INIR
	LD	HL,StRed
	CALL	ReadZ
	LD	HL,StIp
	CALL	ReadZ
	LD	HL,StMotivo
	CALL	ReadZ
	EI
	RET

; GetNets, CMD_WIFIREDES: buscando, n, n redes (senal, marcas, nombre)
GetNets:
	LD	A,CMD_WIFIREDES
	CALL	Begin
	CALL	Finish
	IN	A,(SDFDATA)
	LD	(Busy),A
	IN	A,(SDFDATA)
	CP	REDESMAX+1
	JR	C,GNCount
	LD	A,REDESMAX
GNCount:
	LD	(NNets),A
	OR	A
	JR	Z,GNEnd
	LD	C,A
	LD	HL,NetBuf
GNLoop:
	IN	A,(SDFDATA)
	LD	(HL),A
	INC	HL
	IN	A,(SDFDATA)
	LD	(HL),A
	INC	HL
	CALL	ReadZ
	DEC	C
	JR	NZ,GNLoop
GNEnd:
	EI
	RET

; ReadZ, lee un texto ASCIIZ en HL (hasta TXTMAX); HL queda despues del 0
ReadZ:
	LD	B,TXTMAX
RZLoop:
	IN	A,(SDFDATA)
	LD	(HL),A
	INC	HL
	OR	A
	RET	Z
	DJNZ	RZLoop
	DEC	HL
	LD	(HL),0
	INC	HL
	RET

; ----------------------------------------------------------------------------
; Teclado y tiempo

; ReadKey, la clave en Key/KeyLen, con un asterisco por letra
; out: carry si se corto con ESC
ReadKey:
	XOR	A
	LD	(KeyLen),A
RKLoop:
	LD	C,0x08			; sin eco; CTRL+C corta el programa
	CALL	BDOS
	CP	13
	JR	Z,RKEnd
	CP	27
	JR	Z,RKEsc
	CP	8
	JR	Z,RKBack
	CP	0x7F
	JR	Z,RKBack
	JR	NC,RKLoop
	CP	' '
	JR	C,RKLoop
	LD	C,A
	LD	A,(KeyLen)
	CP	CLAVEMAX
	JR	NC,RKLoop
	LD	E,A
	LD	D,0
	LD	HL,Key
	ADD	HL,DE
	LD	(HL),C
	INC	A
	LD	(KeyLen),A
	LD	A,'*'
	CALL	PrChar
	JR	RKLoop
RKBack:
	LD	A,(KeyLen)
	OR	A
	JR	Z,RKLoop
	DEC	A
	LD	(KeyLen),A
	LD	HL,MsgBack
	CALL	PrStr
	JR	RKLoop
RKEsc:
	CALL	WipeKey
	LD	HL,MsgCrLf
	CALL	PrStr
	SCF
	RET
RKEnd:
	LD	HL,MsgCrLf
	CALL	PrStr
	OR	A
	RET

; WipeKey, que la clave no quede en la memoria
WipeKey:
	LD	HL,Key
	LD	B,CLAVEMAX
WKLoop:
	LD	(HL),0
	INC	HL
	DJNZ	WKLoop
	RET

; Delay, A interrupciones (1/60 o 1/50 s cada una)
Delay:
	LD	B,A
DLNext:
	LD	A,(JIFFY)
	LD	C,A
DLWait:
	PUSH	BC
	CALL	CheckKey
	POP	BC
	LD	A,(JIFFY)
	CP	C
	JR	Z,DLWait
	DJNZ	DLNext
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

; PrZ, texto ASCIIZ de la Pico (un nombre de red puede traer '$' o UTF-8:
; va de a una letra, y lo que no es ASCII sale como '?', uno por letra de
; UTF-8). HL queda despues del 0.
PrZ:
	LD	A,(HL)
	INC	HL
	OR	A
	RET	Z
	CP	' '
	JR	C,PrZOdd
	CP	0x7F
	JR	C,PrZOut
	CP	0xC0
	JR	C,PrZ			; 80h-BFh: sigue una letra de UTF-8
PrZOdd:
	LD	A,'?'
PrZOut:
	CALL	PrChar
	JR	PrZ

; PrRssi, A dBm con signo, alineado a 4: " -48"
PrRssi:
	PUSH	BC
	PUSH	HL
	OR	A
	JP	P,PRPos
	NEG
	LD	C,'-'
	JR	PRPad
PRPos:
	LD	C,' '
PRPad:
	CP	100
	JR	NC,PRSign
	PUSH	AF
	LD	A,' '
	CALL	PrChar
	POP	AF
	CP	10
	JR	NC,PRSign
	PUSH	AF
	LD	A,' '
	CALL	PrChar
	POP	AF
PRSign:
	PUSH	AF
	LD	A,C
	CALL	PrChar
	POP	AF
	CALL	PrDec8
	POP	HL
	POP	BC
	RET

PrDec8:
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	L,A
	LD	H,0
	LD	B,0			; 0 = todavia no salio ningun digito
	LD	DE,100
	CALL	PrDigit
	LD	DE,10
	CALL	PrDigit
	LD	A,L
	ADD	A,'0'
	CALL	PrChar
	POP	HL
	POP	DE
	POP	BC
	RET

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

; ----------------------------------------------------------------------------
; Textos y variables

MsgUsage:	DB	"Uso: WIFI      busca redes y conecta",13,10
		DB	"     WIFI /S   muestra el estado",13,10,'$'
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgOff:		DB	"El Wi-Fi de la Pico no responde.",13,10,'$'
MsgNoNet:	DB	"Sin red guardada.",13,10,'$'
MsgWaiting:	DB	"Sin conexion a $"
MsgOn:		DB	"Conectado a $"
MsgOnIp:	DB	13,10,"IP $"
MsgComma:	DB	",$"
MsgDbmCrLf:	DB	" dBm",13,10,'$'
MsgNoScan:	DB	"No se pudo buscar redes.",13,10,'$'
MsgScan:	DB	"Buscando redes...$"
MsgScanLate:	DB	13,10,"La busqueda no termino.",13,10,'$'
MsgNoNets:	DB	"No se encontraron redes.",13,10,'$'
MsgDbm:		DB	" dBm  $"
MsgOpen:	DB	" (abierta)$"
MsgSaved:	DB	" (guardada)$"
MsgAsk1:	DB	"Red (1-$"
MsgAsk2:	DB	", RETURN sale): $"
MsgBadNum:	DB	"Numero no valido.",13,10,'$'
MsgKey:		DB	"Clave (ESC vuelve): $"
MsgShortKey:	DB	"Minimo 8 caracteres.",13,10,'$'
MsgBack:	DB	8," ",8,'$'
MsgNoWifi:	DB	"El Wi-Fi de la Pico no responde.",13,10,'$'
MsgBadJoin:	DB	"La Pico no acepto la red o la clave.",13,10,'$'
MsgJoin:	DB	"Conectando a $"
MsgDots:	DB	"...",13,10,'$'
MsgJoinLate:	DB	"La Pico no termino de conectar.",13,10,'$'
MsgOk:		DB	"Conectado. IP $"
MsgOk2:		DB	13,10,"La red quedo guardada.",13,10,'$'
MsgFail:	DB	"No se pudo conectar:",13,10,'$'
MsgCrLf:	DB	13,10,'$'

OnlyStat:	DB	0
Busy:		DB	0
NNets:		DB	0
Flags:		DB	0
SsidLen:	DB	0
KeyLen:		DB	0

; Respuesta de CMD_WIFISTAT
StEstado:	DB	0
StResult:	DB	0		; 0 en curso, 1 conecto, 2 fallo
StRssi:		DB	0
StRed:		DS	TXTMAX,0
StIp:		DS	TXTMAX,0
StMotivo:	DS	TXTMAX,0

InBuf:		DS	5,0		; funcion 0Ah: tope, largo, 2 digitos y CR
Ssid:		DS	TXTMAX,0
Key:		DS	CLAVEMAX,0
NetBuf:		DS	REDESMAX*(2+TXTMAX),0
