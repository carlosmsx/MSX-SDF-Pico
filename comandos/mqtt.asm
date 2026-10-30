; MQTT.COM para MSX-DOS 1 con el SDF-Pico (docs/MQTT.md)
;
;   MQTT [broker] [topico]
;   MQTT /P topico texto
;   MQTT /U [broker | -]
;
; Cliente MQTT. El cliente de verdad corre en la Pico (firmware/src/mqtt.c):
; aca se le pide conectar, suscribirse y publicar, y se le consulta con
; CMD_MQTTPOLL lo que llego. La pantalla se maneja directo en la VRAM, en 40
; columnas: arriba los mensajes, que van subiendo, una linea de estado y
; abajo dos lineas para escribir. Lo que se escribe se publica en el topico
; elegido; las lineas que empiezan con / son comandos (/? los lista).
;
; Se ensambla con PASMO:  pasmo mqtt.asm mqtt.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
CALSLT		EQU	0x001C
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E
; Rutinas de la ROM principal (con CALSLT: en MSX-DOS la pagina 0 es RAM)
INITXT		EQU	0x006C
ERAFNK		EQU	0x00CC
DSPFNK		EQU	0x00CF
LINL40		EQU	0xF3AE
CNSDFG		EQU	0xF3DE

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_MQTTCONN	EQU	0xA8
CMD_MQTTCLOSE	EQU	0xA9
CMD_MQTTSUB	EQU	0xAA
CMD_MQTTPUB	EQU	0xAB
CMD_MQTTPOLL	EQU	0xAC
CMD_MQTTURL	EQU	0xAD
CMD_MQTTVER	EQU	0xAE

; mqtt.h
MQ_SIN_CONEXION	EQU	1
MQ_LLENO	EQU	2
MQ_CONECTADO	EQU	0x01
MQ_CONECTANDO	EQU	0x02
MQ_CORTADA	EQU	0x04
MQ_MENSAJE	EQU	0x08
MQ_PERDIDOS	EQU	0x10
MQ_ENVIANDO	EQU	0x20
MQ_CANCELAR	EQU	0x80
MQ_RETENER	EQU	0x02
MQ_TEXTO	EQU	0x04
MQ_CORTADO	EQU	0x01

CR		EQU	13
LF		EQU	10
ESC		EQU	27
BS		EQU	8

COLS		EQU	40
MSGROWS		EQU	21		; filas de mensajes
STROW		EQU	21		; la de estado
INROW		EQU	22		; las dos de abajo: lo que se escribe
INVIS		EQU	2*COLS-3	; lo que se ve de la linea ("> " y el cursor)
INMAX		EQU	200
URLMAX		EQU	200
TOPMAX		EQU	127
LINEMAX		EQU	220
CURCHAR		EQU	0xFE		; caracter cuyo dibujo es el cursor
LINECHAR	EQU	0x17		; la raya horizontal del juego del MSX
BURST		EQU	8		; mensajes seguidos sin redibujar
LASTMSG		EQU	(MSGROWS-1)*COLS	; donde empieza la fila de abajo de los mensajes

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail

	; La linea de comandos (0080h: largo, 0081h: texto)
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0			; termina en 0
	LD	HL,0x0081
	CALL	SkipSp
	LD	A,(HL)
	CP	'/'
	JR	NZ,ArgsPlain
	INC	HL
	LD	A,(HL)
	INC	HL
	AND	0xDF			; MSX-DOS 1 no pasa la linea a mayusculas
	CP	'U'
	JP	Z,DoUrl
	CP	'P'
	JP	Z,DoPub
	JP	Usage

	; [broker] [topico]: con una sola palabra es el topico, salvo que
	; tenga "://"
ArgsPlain:
	LD	DE,Word1
	LD	B,URLMAX
	CALL	TokenChk
	CALL	SkipSp
	LD	DE,Word2
	LD	B,TOPMAX
	CALL	TokenChk
	CALL	SkipSp
	LD	A,(HL)
	OR	A
	JP	NZ,Usage		; sobran palabras
	LD	A,(Word2)
	OR	A
	JR	NZ,TwoWords
	LD	HL,Word1
	CALL	IsUrl
	JR	Z,OneUrl
	LD	HL,Word1
	LD	DE,DefTopic
	LD	B,TOPMAX
	CALL	CopyZ
	JP	Interactive
OneUrl:
	LD	HL,Word1
	LD	DE,Url
	LD	B,URLMAX
	CALL	CopyZ
	JP	Interactive
TwoWords:
	LD	HL,Word1
	LD	DE,Url
	LD	B,URLMAX
	CALL	CopyZ
	LD	HL,Word2
	LD	DE,DefTopic
	LD	B,TOPMAX
	CALL	CopyZ
	JP	Interactive

Usage:
	LD	HL,MsgUsage
	JP	Fail

; ----------------------------------------------------------------------------
; MQTT /U: guarda el broker, lo olvida (-) o lo muestra (sin broker)

DoUrl:
	CALL	SkipSp
	LD	DE,Url
	LD	B,URLMAX
	CALL	TokenChk
	LD	A,(Url)
	OR	A
	JR	Z,ShowUrl
	LD	HL,Url
	CALL	StrLen
	CP	1
	JR	NZ,DUSend
	LD	A,(Url)
	CP	'-'
	LD	A,1
	JR	NZ,DUSend
	XOR	A			; "-": largo 0, lo olvida
DUSend:
	LD	(UrlLen),A
	LD	B,A
	DI
	LD	A,CMD_MQTTURL
	OUT	(SDFCMD),A
	LD	A,B
	OUT	(SDFDATA),A
	LD	HL,Url
	CALL	OutBlock
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	CALL	ClearUrl
	CP	0xFF
	JP	Z,OldFw
	OR	A
	JP	NZ,BadUrl
	LD	A,(UrlLen)
	OR	A
	LD	HL,MsgForgot		; "Broker olvidado."
	JP	Z,Fail
	LD	HL,MsgSaved		; "Broker guardado."
	JP	Fail

ShowUrl:
	DI
	LD	A,CMD_MQTTVER
	OUT	(SDFCMD),A
	CALL	Finish
	LD	HL,Line
	LD	B,LINEMAX
SULoop:
	IN	A,(SDFDATA)
	CP	0xFF
	JR	Z,SUOld
	LD	(HL),A
	OR	A
	JR	Z,SUEnd
	INC	HL
	DJNZ	SULoop
	LD	(HL),0
SUEnd:
	EI
	LD	A,(Line)
	OR	A
	LD	HL,MsgNoUrl
	JP	Z,Fail
	LD	HL,MsgUrl		; "Broker: "
	CALL	PrZ
	LD	HL,Line
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	Fail
SUOld:
	EI
	JP	OldFw

; ----------------------------------------------------------------------------
; MQTT /P topico texto: conecta con el broker guardado, publica y sale

DoPub:
	CALL	SkipSp
	LD	DE,DefTopic
	LD	B,TOPMAX
	CALL	TokenChk
	LD	A,(DefTopic)
	OR	A
	JP	Z,Usage
	CALL	SkipSp
	LD	(TextPtr),HL		; lo que queda de la linea
	CALL	GetHz
	LD	HL,MsgConnecting	; "Conectando..."
	CALL	PrZ
	CALL	Conn
	CALL	WaitConn
	LD	A,MQ_TEXTO
	LD	HL,DefTopic
	LD	DE,(TextPtr)
	CALL	MqPub
	OR	A
	JR	Z,DPSent
	CALL	PubErrText
	JP	Fail
DPSent:
	CALL	WaitSent
	CALL	Close
	LD	HL,MsgPublished		; "Publicado en "
	CALL	PrZ
	LD	HL,DefTopic
	CALL	PrZ
	LD	HL,MsgDotCrLf
	JP	Fail

; WaitConn, espera hasta 15 s a que conecte; si no, sale con el motivo
WaitConn:
	LD	A,15
	CALL	StartTimer
WCLoop:
	CALL	CheckEsc
	CALL	Poll
	LD	A,(Est)
	LD	B,A
	AND	MQ_CONECTADO
	RET	NZ
	LD	A,B
	AND	MQ_CORTADA
	JR	NZ,WCFail
	CALL	TimeUp
	JR	NC,WCLoop
	LD	HL,TxtM19		; "el broker no contesto"
	JR	WCMsg
WCFail:
	LD	A,(Mot)
	CALL	MotText
WCMsg:
	PUSH	HL
	CALL	Close
	LD	HL,MsgNoConn		; "No se pudo conectar: "
	CALL	PrZ
	POP	HL
	CALL	PrZ
	LD	HL,MsgDotCrLf
	JP	Fail

; WaitSent, espera hasta 5 s a que el broker reciba lo publicado
WaitSent:
	LD	A,5
	CALL	StartTimer
WSLoop:
	CALL	CheckEsc
	CALL	Poll
	LD	A,(Est)
	AND	MQ_ENVIANDO
	RET	Z
	CALL	TimeUp
	JR	NC,WSLoop
	RET

; CheckEsc: ESC o CTRL+C cortan
CheckEsc:
	LD	E,0xFF
	LD	C,6
	CALL	BDOS
	CP	ESC
	JR	Z,CECancel
	CP	3
	RET	NZ
CECancel:
	CALL	Close
	LD	HL,MsgCancel
	JP	Fail

; ----------------------------------------------------------------------------
; La sesion: los mensajes arriba, lo que se escribe abajo

Interactive:
	CALL	GetHz
	CALL	Conn
	LD	A,(DefTopic)
	OR	A
	JR	Z,INoSub
	XOR	A
	LD	HL,DefTopic
	CALL	MqSub
	LD	(SubRes),A
INoSub:
	CALL	ScrInit
	LD	HL,TxtHello
	CALL	Event
	LD	A,(DefTopic)
	OR	A
	JR	Z,INoTopic
	LD	A,(SubRes)
	LD	HL,DefTopic
	CALL	SubReport
INoTopic:
	CALL	DrawStatus		; "sin conexion" hasta la primera consulta
	CALL	DrawInput
	LD	A,0xFF			; que el primer estado se muestre
	LD	(LastEst),A

MainLoop:
	CALL	Poll
	; Cambios de estado: a la linea de estado y como aviso
	LD	A,(Est)
	AND	MQ_CONECTADO|MQ_CONECTANDO|MQ_CORTADA
	LD	B,A
	LD	A,(Mot)
	LD	C,A
	LD	A,(LastEst)
	CP	B
	JR	NZ,MLChange
	LD	A,(LastMot)
	CP	C
	JR	Z,MLSame
MLChange:
	LD	A,B
	LD	(LastEst),A
	LD	A,C
	LD	(LastMot),A
	CALL	StateEvent
	CALL	DrawStatus
MLSame:
	LD	A,(Est)
	AND	MQ_PERDIDOS
	JR	Z,MLKept
	LD	HL,TxtLost
	CALL	Event
MLKept:
	LD	A,(Est)
	AND	MQ_MENSAJE
	JR	Z,MLIdle
	CALL	ShowMsg
	CALL	Keys
	; Puede haber mas: se piden enseguida, y se redibuja cada BURST
	LD	HL,Burst
	INC	(HL)
	LD	A,(HL)
	CP	BURST
	JR	C,MainLoop
	LD	(HL),0
	CALL	Refresh
	JR	MainLoop
MLIdle:
	XOR	A
	LD	(Burst),A
	CALL	Keys
	CALL	Refresh
	EI
	HALT				; una interrupcion: 1/60 o 1/50 s
	JR	MainLoop

Quit:
	CALL	Close
	CALL	ScrOff
	JP	0

; StateEvent, el aviso de un cambio de estado (LastEst, LastMot)
StateEvent:
	LD	A,(LastEst)
	LD	B,A
	AND	MQ_CONECTADO
	JR	Z,SENotConn
	LD	A,(LastMot)
	OR	A
	LD	HL,TxtConnected
	JP	Z,Event
	JR	SEMot			; conectado, con una suscripcion rechazada
SENotConn:
	LD	A,B
	AND	MQ_CORTADA
	JR	Z,SENotCut
SEMot:
	CALL	NewLine
	LD	HL,TxtStar
	CALL	PutZ
	LD	A,(LastMot)
	CALL	MotText
	CALL	PutZ
	LD	A,(LastEst)
	AND	MQ_CONECTANDO
	RET	Z
	LD	HL,TxtRetry		; " (reintenta)"
	JP	PutZ
SENotCut:
	LD	A,B
	AND	MQ_CONECTANDO
	RET	Z
	LD	HL,TxtConnecting
	JP	Event

; ShowMsg, el mensaje que trajo Poll. Si el topico es el elegido, solo el
; texto; si no, "topico: texto".
ShowMsg:
	CALL	NewLine
	CALL	TopicIsDef
	JR	Z,SMText
	LD	HL,MsgTop
	LD	A,(TopLen)
	CALL	PutN
	LD	HL,TxtColon
	CALL	PutZ
SMText:
	LD	HL,MsgTxt
	LD	A,(TxtLen)
	CALL	PutN
	LD	A,(MsgFlags)
	AND	MQ_CORTADO
	RET	Z
	LD	HL,TxtCut		; "..."
	JP	PutZ

; Z si el topico del mensaje es DefTopic
TopicIsDef:
	LD	HL,DefTopic
	CALL	StrLen
	LD	B,A
	LD	A,(TopLen)
	CP	B
	RET	NZ
	OR	A
	JR	Z,TIDNo			; sin topico elegido
	LD	HL,DefTopic
	LD	DE,MsgTop
TIDLoop:
	LD	A,(DE)
	CP	(HL)
	RET	NZ
	INC	HL
	INC	DE
	DJNZ	TIDLoop
	RET
TIDNo:
	INC	A			; NZ
	RET

; ----------------------------------------------------------------------------
; El teclado

Keys:
	LD	E,0xFF
	LD	C,6			; entrada directa, sin eco y sin esperar
	CALL	BDOS
	OR	A
	RET	Z
	CP	ESC
	JP	Z,Quit
	CP	3			; CTRL+C
	JP	Z,Quit
	CP	CR
	JP	Z,Enter
	CP	BS
	JR	Z,KBack
	CP	127			; DEL
	JR	Z,KBack
	CP	' '
	RET	C			; otros de control: nada
	LD	C,A
	LD	A,(InLen)
	CP	INMAX
	RET	NC
	LD	E,A
	LD	D,0
	LD	HL,InBuf
	ADD	HL,DE
	LD	(HL),C
	INC	HL
	LD	(HL),0
	INC	A
	LD	(InLen),A
	JP	DrawInput
KBack:
	LD	A,(InLen)
	OR	A
	RET	Z
	DEC	A
	LD	(InLen),A
	LD	E,A
	LD	D,0
	LD	HL,InBuf
	ADD	HL,DE
	LD	(HL),0
	JP	DrawInput

; Enter, la linea escrita: un comando o un texto para publicar
Enter:
	LD	A,(InLen)
	OR	A
	RET	Z
	LD	HL,InBuf
	LD	A,(HL)
	CP	'/'
	JR	Z,ECmd
	LD	A,(DefTopic)
	OR	A
	JR	NZ,EPubDef
	LD	HL,TxtNoTopic		; "sin topico: /t topico"
	CALL	Event
	JR	EDone
EPubDef:
	LD	A,MQ_TEXTO
	LD	HL,DefTopic
	LD	DE,InBuf
	CALL	MqPub
	CALL	PubReport
EDone:
	XOR	A
	LD	(InLen),A
	LD	(InBuf),A
	JP	DrawInput

ECmd:
	INC	HL
	LD	A,(HL)
	OR	0x20			; minuscula
	LD	C,A
	INC	HL
	LD	A,(HL)			; despues de la letra, un espacio o nada
	OR	A
	JR	Z,ECmdOk
	CP	' '
	JR	NZ,EUnknown
ECmdOk:
	CALL	SkipSp			; HL = el argumento
	LD	A,C
	CP	's'
	JR	Z,ESub
	CP	'u'
	JR	Z,EUnsub
	CP	't'
	JR	Z,ETopic
	CP	'p'
	JR	Z,EPub
	CP	'r'
	JR	Z,ERet
	CP	'q'
	JP	Z,Quit
	CP	'?'
	JR	Z,EHelp
	CP	'h'
	JR	Z,EHelp
EUnknown:
	LD	HL,TxtUnknown
	CALL	Event
	JR	EDone

ESub:
	XOR	A
	JR	ESubUn
EUnsub:
	LD	A,MQ_CANCELAR
ESubUn:
	LD	(SubMode),A
	LD	A,(HL)
	OR	A
	JR	Z,ENeedTopic
	PUSH	HL
	LD	A,(SubMode)
	CALL	MqSub
	POP	HL
	CALL	SubReport
	JR	EDone

ENeedTopic:
	LD	HL,TxtNeedTopic
	CALL	Event
	JR	EDone

ETopic:
	LD	DE,DefTopic
	LD	B,TOPMAX
	CALL	CopyZ
	CALL	DrawStatus
	LD	A,(DefTopic)
	OR	A
	LD	HL,TxtNoDef		; "* sin topico"
	JR	Z,ETEvent
	CALL	NewLine
	LD	HL,TxtTopicIs		; "* topico: "
	CALL	PutZ
	LD	HL,DefTopic
	CALL	PutZ
	JP	EDone
ETEvent:
	CALL	Event
	JP	EDone

EHelp:
	LD	HL,TxtHelp
EHLoop:
	LD	A,(HL)
	OR	A
	JP	Z,EDone
	CALL	Event			; deja HL despues del 0
	JR	EHLoop

; /p y /r: "topico texto"
ERet:
	LD	A,MQ_TEXTO|MQ_RETENER
	JR	EPubAny
EPub:
	LD	A,MQ_TEXTO
EPubAny:
	LD	(PubFlags),A
	LD	DE,PubTopic
	LD	B,TOPMAX
	CALL	Token
	LD	A,(PubTopic)
	OR	A
	JR	Z,ENeedTopic
	CALL	SkipSp
	PUSH	HL
	EX	DE,HL			; DE = el texto
	LD	HL,PubTopic
	LD	A,(PubFlags)
	CALL	MqPub
	POP	DE
	OR	A
	JR	NZ,EPubErr
	CALL	NewLine			; "-> topico: texto"
	PUSH	DE
	LD	HL,TxtArrow
	CALL	PutZ
	LD	HL,PubTopic
	CALL	PutZ
	LD	HL,TxtColon
	CALL	PutZ
	POP	HL
	CALL	PutZ
	JP	EDone
EPubErr:
	CALL	PubReport
	JP	EDone

; SubReport: A = resultado de MqSub, HL = topico
SubReport:
	OR	A
	JR	NZ,SRErr
	PUSH	HL
	CALL	NewLine
	LD	HL,TxtSubOk		; "* suscripto a "
	LD	A,(SubMode)
	OR	A
	JR	Z,SROk
	LD	HL,TxtUnsubOk		; "* suscripcion cancelada: "
SROk:
	CALL	PutZ
	POP	HL
	JP	PutZ
SRErr:
	CALL	PubErrText
	JR	PRShow

; PubReport: A = resultado de MqPub; solo avisa los errores
PubReport:
	OR	A
	RET	Z
	CALL	PubErrText
PRShow:
	PUSH	HL
	CALL	NewLine
	LD	HL,TxtStar
	CALL	PutZ
	POP	HL
	JP	PutZ

; PubErrText: A = resultado (1 a 3) -> HL = texto (con CR LF al final para DOS)
PubErrText:
	CP	MQ_SIN_CONEXION
	LD	HL,TxtNoConn
	RET	Z
	CP	MQ_LLENO
	LD	HL,TxtBusy
	RET	Z
	LD	HL,TxtBadTopic
	RET

; MotText: A = motivo -> HL = texto
MotText:
	LD	HL,MotTab
MTLoop:
	LD	B,(HL)
	INC	HL
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	CP	B
	JR	Z,MTHit
	INC	B			; FFh: el ultimo, para los desconocidos
	JR	NZ,MTLoop
MTHit:
	EX	DE,HL
	RET

; ----------------------------------------------------------------------------
; La Pico

; Conn, CMD_MQTTCONN con Url (vacia: el broker guardado). Sale si falla.
Conn:
	LD	HL,Url
	CALL	StrLen
	LD	B,A
	DI
	LD	A,CMD_MQTTCONN
	OUT	(SDFCMD),A
	LD	A,B
	OUT	(SDFDATA),A
	LD	HL,Url
	CALL	OutBlock
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	CALL	ClearUrl		; la clave no queda en la memoria
	CP	0xFF
	JP	Z,OldFw
	OR	A
	JR	NZ,ConnErr
	LD	A,1
	LD	(Opened),A
	RET
ConnErr:
	CP	MQ_LLENO
	LD	HL,TxtBusy
	JP	Z,Fail
BadUrl:
	LD	HL,MsgBadUrl
	JP	Fail

; Close, CMD_MQTTCLOSE si se conecto
Close:
	LD	A,(Opened)
	OR	A
	RET	Z
	XOR	A
	LD	(Opened),A
	DI
	LD	A,CMD_MQTTCLOSE
	OUT	(SDFCMD),A
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	RET

; MqSub: A = modo, HL = topico (ASCIIZ). Devuelve A = resultado.
MqSub:
	LD	(SubMode),A
	PUSH	HL
	CALL	StrLen
	POP	HL
	LD	B,A
	DI
	LD	A,CMD_MQTTSUB
	OUT	(SDFCMD),A
	LD	A,(SubMode)
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	CALL	OutBlock
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	RET

; MqPub: A = flags, HL = topico, DE = texto (ASCIIZ los dos). A = resultado.
MqPub:
	LD	(PubFlags),A
	LD	(PubTop),HL
	LD	(PubTxt),DE
	CALL	StrLen
	LD	(PubTL),A
	LD	HL,(PubTxt)
	CALL	StrLen
	LD	(PubXL),A
	DI
	LD	A,CMD_MQTTPUB
	OUT	(SDFCMD),A
	LD	A,(PubFlags)
	OUT	(SDFDATA),A
	LD	A,(PubTL)
	OUT	(SDFDATA),A
	LD	HL,(PubTop)
	CALL	OutBlock
	LD	A,(PubXL)
	OUT	(SDFDATA),A
	LD	HL,(PubTxt)
	CALL	OutBlock
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	RET

; Poll, CMD_MQTTPOLL con el texto en el juego del MSX: deja Est, Mot y, si
; hay mensaje, MsgFlags, TopLen y MsgTop, TxtLen y MsgTxt
Poll:
	DI
	LD	A,CMD_MQTTPOLL
	OUT	(SDFCMD),A
	LD	A,MQ_TEXTO
	OUT	(SDFDATA),A
	CALL	Finish
	IN	A,(SDFDATA)
	CP	0xFF
	JR	Z,PollOld
	LD	(Est),A
	IN	A,(SDFDATA)
	LD	(Mot),A
	LD	A,(Est)
	AND	MQ_MENSAJE
	JR	Z,PollEnd
	IN	A,(SDFDATA)
	LD	(MsgFlags),A
	IN	A,(SDFDATA)
	LD	(TopLen),A
	LD	HL,MsgTop
	CALL	InBlock
	IN	A,(SDFDATA)
	LD	(TxtLen),A
	LD	HL,MsgTxt
	CALL	InBlock
PollEnd:
	EI
	RET
PollOld:
	EI
	JP	OldFw

; OutBlock: A bytes de HL a la Pico (0: ninguno)
OutBlock:
	OR	A
	RET	Z
	LD	B,A
	LD	C,SDFDATA
	OTIR
	RET

; InBlock: A bytes de la Pico a HL (0: ninguno)
InBlock:
	OR	A
	RET	Z
	LD	B,A
	LD	C,SDFDATA
	INIR
	RET

; Finish, espera a que la Pico baje OCUPADO (bit 7 del estado)
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
	XOR	A
	LD	(Opened),A		; no hay a quien pedirle CLOSE
	LD	HL,MsgNoPico
	JP	Fail

OldFw:
	LD	HL,MsgOldFw
Fail:
	PUSH	HL
	CALL	Close
	CALL	ScrOff
	POP	HL
	CALL	PrZ
	JP	0

ClearUrl:
	PUSH	AF
	LD	HL,Url
	LD	B,URLMAX
CULoop:
	LD	(HL),0
	INC	HL
	DJNZ	CULoop
	POP	AF
	RET

; ----------------------------------------------------------------------------
; La pantalla: Scr es una copia de la VRAM (24 filas de 40) que Refresh
; manda entera cuando cambio

ScrInit:
	LD	A,(LINL40)
	LD	(OldWidth),A
	LD	A,(CNSDFG)
	LD	(OldFnk),A
	LD	A,COLS
	LD	(LINL40),A
	XOR	A
	LD	(CNSDFG),A
	LD	IX,INITXT
	CALL	Bios
	LD	IX,ERAFNK
	CALL	Bios
	CALL	VdpInit
	LD	HL,CurPat
	LD	DE,0x0800+CURCHAR*8
	LD	BC,8
	CALL	VWrite
	LD	A,1
	LD	(ScrOn),A
	LD	(Dirty),A
	RET

; ScrOff: el modo de texto de antes (INITXT tambien repone el dibujo del cursor)
ScrOff:
	LD	A,(ScrOn)
	OR	A
	RET	Z
	XOR	A
	LD	(ScrOn),A
	LD	A,(OldWidth)
	LD	(LINL40),A
	LD	IX,INITXT
	CALL	Bios
	LD	A,(OldFnk)
	LD	(CNSDFG),A
	OR	A
	RET	Z
	LD	IX,DSPFNK
	JP	Bios

; Bios: llama a la rutina de la ROM principal en IX
Bios:
	LD	IY,(EXPTBL-1)
	CALL	CALSLT
	EI
	RET

; VdpInit: los puertos del VDP estan en 0006h y 0007h de la ROM principal
VdpInit:
	LD	HL,0x0006
	LD	A,(EXPTBL)
	CALL	RDSLT
	LD	(VPortR),A
	LD	HL,0x0007
	LD	A,(EXPTBL)
	CALL	RDSLT
	LD	(VPortW),A
	EI
	RET

; VWrite: HL = datos, DE = direccion de VRAM, BC = cantidad
VWrite:
	DI
	PUSH	BC
	LD	A,(VPortW)
	LD	C,A
	INC	C
	LD	A,E
	OUT	(C),A
	LD	A,D
	OR	0x40
	OUT	(C),A
	DEC	C
	POP	DE
VWrite1:
	LD	A,(HL)
	OUT	(C),A
	INC	HL
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,VWrite1
	EI
	RET

Refresh:
	LD	A,(Dirty)
	OR	A
	RET	Z
	XOR	A
	LD	(Dirty),A
	LD	HL,Scr
	LD	DE,0
	LD	BC,24*COLS
	JP	VWrite

; NewLine: los mensajes suben una fila y la de abajo queda vacia
NewLine:
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	HL,Scr+COLS
	LD	DE,Scr
	LD	BC,LASTMSG
	LDIR
	LD	HL,Scr+LASTMSG
	LD	B,COLS
NLClear:
	LD	(HL),' '
	INC	HL
	DJNZ	NLClear
	XOR	A
	LD	(Col),A
	INC	A
	LD	(Dirty),A
	POP	HL
	POP	DE
	POP	BC
	RET

; PutC: A en la fila de abajo de los mensajes; al llegar al borde, sigue en
; una fila nueva. Los de control (el CR LF de algunos textos) no se ponen.
PutC:
	CP	' '
	RET	C
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	A,(Col)
	CP	COLS
	CALL	Z,NewLine
	LD	A,(Col)
	LD	HL,Scr+LASTMSG
	ADD	A,L
	LD	L,A
	JR	NC,PCNoCarry
	INC	H
PCNoCarry:
	LD	(HL),E
	LD	HL,Col
	INC	(HL)
	POP	HL
	POP	DE
	RET

; PutZ: el texto de HL hasta el 0; deja HL despues del 0
PutZ:
	LD	A,(HL)
	INC	HL
	OR	A
	RET	Z
	CALL	PutC
	JR	PutZ

; PutN: A caracteres de HL
PutN:
	OR	A
	RET	Z
	LD	B,A
PNLoop:
	LD	A,(HL)
	CALL	PutC
	INC	HL
	DJNZ	PNLoop
	RET

; Event: el texto de HL en una fila nueva; deja HL despues del 0
Event:
	CALL	NewLine
	JR	PutZ

; DrawStatus: la raya con el estado y el topico elegido
DrawStatus:
	LD	HL,Scr+STROW*COLS
	LD	B,COLS
DSLine:
	LD	(HL),LINECHAR
	INC	HL
	DJNZ	DSLine
	LD	DE,Scr+STROW*COLS+1
	LD	B,COLS-2		; lo que entra
	LD	HL,TxtSpace
	CALL	RowPut
	LD	A,(LastEst)
	LD	HL,TxtStConn		; "conectado"
	BIT	0,A			; MQ_CONECTADO
	JR	NZ,DSState
	LD	HL,TxtStTrying		; "conectando"
	BIT	1,A			; MQ_CONECTANDO
	JR	NZ,DSState
	LD	HL,TxtStNone		; "sin conexion"
DSState:
	CALL	RowPut
	LD	HL,TxtSpace
	CALL	RowPut
	LD	A,(DefTopic)
	OR	A
	JR	Z,DSEnd
	LD	A,B
	OR	A
	JR	Z,DSEnd
	INC	DE			; deja ver la raya
	DEC	B
	LD	HL,TxtSpace
	CALL	RowPut
	LD	HL,DefTopic
	CALL	RowPut
	LD	HL,TxtSpace
	CALL	RowPut
DSEnd:
	LD	A,1
	LD	(Dirty),A
	RET

; RowPut: el texto de HL en DE, B como maximo (B queda con lo que sobra)
RowPut:
	LD	A,B
	OR	A
	RET	Z
	LD	A,(HL)
	OR	A
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	DEC	B
	JR	RowPut

; DrawInput: "> " y lo ultimo de lo escrito, con el cursor al final
DrawInput:
	LD	HL,Scr+INROW*COLS
	LD	B,2*COLS
DIClear:
	LD	(HL),' '
	INC	HL
	DJNZ	DIClear
	LD	HL,Scr+INROW*COLS
	LD	(HL),'>'
	INC	HL
	INC	HL
	EX	DE,HL			; DE = donde va el texto
	LD	A,(InLen)
	LD	C,0			; desde donde se ve
	CP	INVIS+1
	JR	C,DIFits
	SUB	INVIS
	LD	C,A
	LD	A,INVIS
DIFits:
	LD	B,A			; cuantos se ven
	LD	HL,InBuf
	LD	A,C
	ADD	A,L
	LD	L,A
	JR	NC,DINoCarry
	INC	H
DINoCarry:
	LD	A,B
	OR	A
	JR	Z,DICursor
DICopy:
	LD	A,(HL)
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	DICopy
DICursor:
	LD	A,CURCHAR
	LD	(DE),A
	LD	A,1
	LD	(Dirty),A
	RET

; ----------------------------------------------------------------------------
; Tiempo

; GetHz: 50 o 60, segun la ROM (bit 7 de 002Bh: 1 = 50 Hz)
GetHz:
	LD	HL,0x002B
	LD	A,(EXPTBL)
	CALL	RDSLT
	EI
	AND	0x80
	LD	A,60
	JR	Z,GHSet
	LD	A,50
GHSet:
	LD	(Hz),A
	RET

; StartTimer: A segundos desde ahora
StartTimer:
	LD	B,A
	LD	A,(Hz)
	LD	E,A
	LD	D,0
	LD	HL,0
STMul:
	ADD	HL,DE
	DJNZ	STMul
	LD	(Limit),HL
	LD	HL,(JIFFY)
	LD	(T0),HL
	RET

; TimeUp: carry si ya paso
TimeUp:
	LD	HL,(JIFFY)
	LD	DE,(T0)
	OR	A
	SBC	HL,DE
	LD	DE,(Limit)
	OR	A
	SBC	HL,DE
	CCF
	RET

; ----------------------------------------------------------------------------
; Textos de la linea de comandos

SkipSp:
	LD	A,(HL)
	CP	' '
	RET	NZ
	INC	HL
	JR	SkipSp

; Token, copia de HL a DE hasta un espacio o el 0, B como maximo; DE en 0
Token:
	LD	A,(HL)
	OR	A
	JR	Z,TokEnd
	CP	' '
	JR	Z,TokEnd
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	Token
TokEnd:
	XOR	A
	LD	(DE),A
	RET

; TokenChk, como Token pero sale con un error si la palabra no entra
TokenChk:
	CALL	Token
	LD	A,(HL)
	OR	A
	RET	Z
	CP	' '
	RET	Z
	LD	HL,MsgTooLong
	JP	Fail

; CopyZ, el texto de HL a DE, B como maximo, terminado en 0
CopyZ:
	LD	A,(HL)
	OR	A
	JR	Z,CZEnd
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	CopyZ
CZEnd:
	XOR	A
	LD	(DE),A
	RET

; IsUrl: Z si el texto de HL tiene "://"
IsUrl:
	LD	A,(HL)
	OR	A
	JR	Z,IUNo
	INC	HL
	CP	':'
	JR	NZ,IsUrl
	LD	A,(HL)
	CP	'/'
	JR	NZ,IsUrl
	INC	HL
	LD	A,(HL)
	CP	'/'
	RET	Z
	JR	IsUrl
IUNo:
	INC	A			; NZ
	RET

; A = largo del texto en HL (hasta 255)
StrLen:
	LD	B,0
SLLoop:
	LD	A,(HL)
	OR	A
	JR	Z,SLEnd
	INC	HL
	INC	B
	JR	NZ,SLLoop
	DEC	B
SLEnd:
	LD	A,B
	RET

; PrZ, el texto de HL hasta el 0, por la consola del DOS
PrZ:
	LD	A,(HL)
	OR	A
	RET	Z
	PUSH	HL
	LD	E,A
	LD	C,2
	CALL	BDOS
	POP	HL
	INC	HL
	JR	PrZ

; ----------------------------------------------------------------------------
; Textos y variables

MsgUsage:	DB	"Uso: MQTT [broker] [topico]",CR,LF
		DB	"     MQTT /P topico texto",CR,LF
		DB	"     MQTT /U [broker | -]",CR,LF
		DB	"Muestra los mensajes de MQTT y",CR,LF
		DB	"publica lo que se escribe. Con",CR,LF
		DB	"topico, se suscribe y publica ahi",CR,LF
		DB	"(/? lista los comandos). Sin broker",CR,LF
		DB	"usa el guardado.",CR,LF
		DB	"  /P  publica un texto y sale",CR,LF
		DB	"  /U  guarda el broker en la Pico;",CR,LF
		DB	"      - lo olvida y sin broker lo",CR,LF
		DB	"      muestra",CR,LF
		DB	"El broker:",CR,LF
		DB	"[mqtt://][usuario:clave@]servidor",CR,LF
		DB	"[:puerto][/cliente] (puerto 1883)",CR,LF,0
MsgNoPico:	DB	"No se detecta el SDF-Pico.",CR,LF,0
MsgOldFw:	DB	"El firmware de la Pico no maneja",CR,LF
		DB	"MQTT (hace falta el 0.16).",CR,LF,0
MsgBadUrl:	DB	"Broker no valido: [mqtt://][usuario:",CR,LF
		DB	"clave@]servidor[:puerto][/cliente]",CR,LF
		DB	"(sin TLS: mqtts:// no se puede).",CR,LF,0
MsgTooLong:	DB	"Una palabra es demasiado larga.",CR,LF,0
MsgSaved:	DB	"Broker guardado.",CR,LF,0
MsgForgot:	DB	"Broker olvidado.",CR,LF,0
MsgNoUrl:	DB	"No hay broker guardado:",CR,LF
		DB	"MQTT /U broker",CR,LF,0
MsgUrl:		DB	"Broker: ",0
MsgConnecting:	DB	"Conectando...",CR,LF,0
MsgNoConn:	DB	"No se pudo conectar: ",0
MsgPublished:	DB	"Publicado en ",0
MsgCancel:	DB	"Cancelado.",CR,LF,0
MsgDotCrLf:	DB	"."
MsgCrLf:	DB	CR,LF,0

; Los avisos de la sesion. Los de error tambien salen por el DOS con /P:
; por eso terminan en CR LF (PutC no pone los de control).
TxtHello:	DB	"MQTT.COM - /? comandos, ESC sale",0
TxtConnected:	DB	"* conectado",0
TxtConnecting:	DB	"* conectando...",0
TxtRetry:	DB	" (reintenta)",0
TxtLost:	DB	"* se perdieron mensajes",0
TxtStar:	DB	"* ",0
TxtColon:	DB	": ",0
TxtArrow:	DB	"-> ",0
TxtCut:		DB	"...",0
TxtSpace:	DB	" ",0
TxtNoTopic:	DB	"* sin topico: /t topico",0
TxtNeedTopic:	DB	"* falta el topico",0
TxtUnknown:	DB	"* comando desconocido (/? los lista)",0
TxtTopicIs:	DB	"* topico: ",0
TxtNoDef:	DB	"* sin topico",0
TxtSubOk:	DB	"* suscripto a ",0
TxtUnsubOk:	DB	"* suscripcion cancelada: ",0
TxtNoConn:	DB	"sin conexion",CR,LF,0
TxtBusy:	DB	"la Pico esta ocupada, probar de nuevo",CR,LF,0
TxtBadTopic:	DB	"topico no valido",CR,LF,0
TxtStConn:	DB	"conectado",0
TxtStTrying:	DB	"conectando",0
TxtStNone:	DB	"sin conexion",0
TxtHelp:	DB	"/s topico  suscribirse (valen + y #)",0
		DB	"/u topico  cancelar la suscripcion",0
		DB	"/t topico  donde va lo que se escribe",0
		DB	"/p topico texto  publicar ahi",0
		DB	"/r topico texto  publicar retenido",0
		DB	"/q o ESC   salir",0
		DB	0

; El motivo de un corte (mqtt.h)
MotTab:		DB	1
		DW	TxtM1
		DB	2
		DW	TxtM2
		DB	3
		DW	TxtM3
		DB	4
		DW	TxtM4
		DB	5
		DW	TxtM5
		DB	16
		DW	TxtM16
		DB	17
		DW	TxtM17
		DB	18
		DW	TxtM18
		DB	19
		DW	TxtM19
		DB	20
		DW	TxtM20
		DB	21
		DW	TxtM21
		DB	0xFF
		DW	TxtM0
TxtM0:		DB	"se corto la conexion",0
TxtM1:		DB	"el broker no acepta MQTT 3.1.1",0
TxtM2:		DB	"el broker rechazo el identificador",0
TxtM3:		DB	"el broker no esta disponible",0
TxtM4:		DB	"usuario o clave incorrectos",0
TxtM5:		DB	"no autorizado",0
TxtM16:		DB	"la Pico no esta conectada al Wi-Fi",0
TxtM17:		DB	"no se pudo resolver el nombre",0
TxtM18:		DB	"fallo la conexion",0
TxtM19:		DB	"el broker no contesto",0
TxtM20:		DB	"no hay broker guardado (MQTT /U)",0
TxtM21:		DB	"suscripcion rechazada",0

CurPat:		DB	0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC,0xFC

Hz:		DB	60
T0:		DW	0
Limit:		DW	0
VPortR:		DB	0x98
VPortW:		DB	0x98
OldWidth:	DB	40
OldFnk:		DB	0
ScrOn:		DB	0
Opened:		DB	0
Dirty:		DB	0
Col:		DB	0
Burst:		DB	0
Est:		DB	0
Mot:		DB	0
LastEst:	DB	0
LastMot:	DB	0
SubMode:	DB	0
SubRes:		DB	0
PubFlags:	DB	0
PubTL:		DB	0
PubXL:		DB	0
PubTop:		DW	0
PubTxt:		DW	0
TextPtr:	DW	0
UrlLen:		DB	0
InLen:		DB	0
MsgFlags:	DB	0
TopLen:		DB	0
TxtLen:		DB	0
Url:		DS	URLMAX+1,0
DefTopic:	DS	TOPMAX+1,0
PubTopic:	DS	TOPMAX+1,0
InBuf:		DS	INMAX+1,0
MsgTop:		DS	256,0
MsgTxt:		DS	256,0
Word1:		DS	URLMAX+1,0
Word2:		DS	TOPMAX+1,0
Line:		DS	LINEMAX+1,0
Scr:		DS	24*COLS,' '
