; GET.COM para MSX-DOS 1 con el SDF-Pico (docs/RED.md)
;
;   GET http://host[:puerto]/ruta [archivo]
;
; Baja un archivo por HTTP/1.0 y lo graba en el disco. Sin archivo, toma el
; nombre del final de la ruta (INDEX.HTM si termina en /). La conexion la
; abre la Pico (CMD_NETCONN) y los bytes pasan por CMD_NETIO; aca se arma el
; pedido, se leen los encabezados y se graba el cuerpo. Sin HTTPS.
;
; Se ensambla con PASMO:  pasmo get.asm get.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_NETCONN	EQU	0xB1
CMD_NETIO	EQU	0xB2

; Bits del estado de CMD_NETIO (red.h)
R_CONECTADO	EQU	0
R_CONECTANDO	EQU	1
R_CORTADA	EQU	2

HOSTMAX		EQU	60		; lo que entra en CMD_NETCONN
TXMAX		EQU	60		; RED_MAX_TX
LINEMAX		EQU	100		; de cada encabezado se guarda esto
BUFSZ		EQU	8192		; se graba de a esto

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail
	XOR	A
	LD	(Loc),A

	; La linea de comandos (0080h: largo, 0081h: texto): URL y archivo
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0			; termina en 0
	LD	HL,0x0081
	CALL	SkipSp
	LD	DE,Url
	LD	B,127
	CALL	Token
	LD	A,(Url)
	OR	A
	JP	Z,Usage
	CALL	SkipSp
	LD	DE,FName
	LD	B,63
	CALL	Token

	CALL	ParseUrl
	CALL	MakeFcb
	CALL	GetHz

	; Si el archivo ya existe, preguntar
	LD	DE,Fcb
	LD	C,0x0F			; abrir
	CALL	BDOS
	OR	A
	JR	NZ,NoFile
	LD	HL,(Fcb+16)		; lo que ocupa: se libera al reemplazarlo
	LD	(OldSize),HL
	LD	HL,(Fcb+18)
	LD	(OldSize+2),HL
	LD	HL,MsgExists1		; "Ya existe "
	CALL	PrStr
	CALL	PrFcbName
	LD	HL,MsgExists2		; ". Reemplazar (S/N)? "
	CALL	PrStr
	LD	C,0x08
	CALL	BDOS
	PUSH	AF
	CALL	PrChar
	LD	HL,MsgCrLf
	CALL	PrStr
	POP	AF
	AND	0xDF
	CP	'S'
	JP	NZ,Exit
NoFile:

	; Conectar
	LD	HL,MsgConn		; "Conectando a "
	CALL	PrStr
	LD	HL,Host
	CALL	PrZ
	LD	HL,MsgDots
	CALL	PrStr
	LD	HL,Host
	LD	A,(HostLen)
	LD	DE,(Port)
	CALL	NetConn
	OR	A
	LD	HL,MsgBadHost
	JP	NZ,Fail
	LD	HL,(JIFFY)
	LD	(TStart),HL
	LD	(TIdle),HL
ConnWait:
	CALL	CheckEsc
	XOR	A
	CALL	NetIO
	BIT	R_CONECTADO,A
	JR	NZ,Connected
	BIT	R_CORTADA,A
	LD	HL,MsgNoConn
	JP	NZ,Abort
	LD	HL,(Hz20)
	CALL	IdleOver
	LD	HL,MsgNoConn
	JP	NC,Abort
	JR	ConnWait

Connected:
	; El pedido, de a TXMAX bytes
	CALL	BuildReq
	LD	HL,ReqBuf
	LD	(SendPtr),HL
SendLoop:
	LD	HL,(SendLeft)
	LD	A,H
	OR	L
	JR	Z,Receive
	LD	A,TXMAX
	LD	DE,TXMAX
	OR	A
	SBC	HL,DE
	JR	NC,SendChunk		; quedan mas de TXMAX
	ADD	HL,DE
	LD	A,L			; los que quedan
	LD	HL,TXMAX
SendChunk:
	LD	E,A			; E = los que salen ahora
	LD	D,0
	LD	HL,(SendLeft)
	OR	A
	SBC	HL,DE
	LD	(SendLeft),HL
	LD	HL,(SendPtr)
	PUSH	HL
	ADD	HL,DE
	LD	(SendPtr),HL
	POP	HL
	CALL	NetIO
	CALL	ProcessRx
	LD	A,(Stop)		; el servidor puede contestar enseguida
	OR	A
	JP	NZ,NotOk
	JR	SendLoop

	; La respuesta
Receive:
	LD	HL,(JIFFY)
	LD	(TIdle),HL
RecvLoop:
	LD	HL,EscCount
	DEC	(HL)
	CALL	Z,CheckEsc
	XOR	A
	CALL	NetIO
	LD	(NetSt),A
	LD	A,B
	OR	C
	JR	Z,RecvIdle
	CALL	ProcessRx
	LD	A,(Stop)
	OR	A
	JP	NZ,NotOk
	LD	HL,(JIFFY)
	LD	(TIdle),HL
	CALL	Complete
	JR	C,RecvLoop
	JR	Done
RecvIdle:
	CALL	Complete
	JR	NC,Done
	LD	A,(NetSt)
	BIT	R_CONECTADO,A		; el servidor cerro y ya no queda nada
	JR	Z,Done
	LD	HL,(Hz30)
	CALL	IdleOver
	JR	C,RecvLoop
	LD	HL,MsgIdle
	JP	Abort

Done:
	LD	A,(InBody)
	OR	A
	JR	NZ,DoneBody
	LD	A,(GotStatus)
	OR	A
	LD	HL,MsgNoResp
	JP	Z,Abort
	LD	HL,MsgBadResp		; "Respuesta incompleta: "
	CALL	PrStr
	LD	HL,StatusLine
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	Abort
DoneBody:
	CALL	Flush
	LD	A,(CLenKnown)
	OR	A
	JR	Z,DoneOk
	CALL	CmpTotal
	JR	NC,DoneOk
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,MsgShort
	JP	Abort
DoneOk:
	LD	DE,Fcb
	LD	C,0x10			; cerrar
	CALL	BDOS
	XOR	A
	LD	(FileOpen),A
	CALL	NetCut
	CALL	Progress		; por si fue de menos de un bufer
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,MsgSaved		; "Grabado en "
	CALL	PrStr
	CALL	PrFcbName
	LD	HL,MsgIn		; ", en "
	CALL	PrStr
	CALL	PrTime
	LD	HL,MsgSec		; " s"
	CALL	PrStr
	JP	Exit

	; El servidor no contesto 200
NotOk:
	CALL	NetCut
	LD	HL,MsgStatus		; "El servidor contesto: "
	CALL	PrStr
	LD	HL,StatusLine
	CALL	PrZ
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	A,(Loc)
	OR	A
	JP	Z,Exit
	LD	HL,MsgLoc		; "Redirige a: "
	CALL	PrStr
	LD	HL,Loc
	CALL	PrZ
	LD	HL,MsgCrLf
	CALL	PrStr
	JP	Exit

Usage:
	LD	HL,MsgUsage
Fail:
	CALL	PrStr
Exit:
	JP	0			; vuelve al DOS

; Abort, HL = mensaje: corta la conexion y borra el archivo a medias
Abort:
	CALL	PrStr
	CALL	NetCut
	LD	A,(FileOpen)
	OR	A
	JR	Z,Exit
	LD	DE,Fcb
	LD	C,0x10			; cerrar
	CALL	BDOS
	LD	DE,Fcb
	LD	C,0x13			; borrar
	CALL	BDOS
	LD	HL,MsgDeleted
	CALL	PrStr
	JR	Exit

; ----------------------------------------------------------------------------
; Linea de comandos y URL

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

; ParseUrl: Host, HostLen, Port, PortTxt y PathPtr desde Url
ParseUrl:
	LD	HL,Url
	LD	DE,TxtHttps
	CALL	Prefix
	LD	HL,MsgHttps
	JP	Z,Fail
	LD	HL,Url
	LD	DE,TxtHttp
	CALL	Prefix
	JR	Z,PUHost
	LD	HL,Url			; sin http://
PUHost:
	LD	DE,Host
	LD	B,0
PUHostLoop:
	LD	A,(HL)
	OR	A
	JR	Z,PUHostEnd
	CP	':'
	JR	Z,PUHostEnd
	CP	'/'
	JR	Z,PUHostEnd
	LD	(DE),A
	INC	HL
	INC	DE
	INC	B
	LD	A,B
	CP	HOSTMAX+1
	JR	C,PUHostLoop
	LD	HL,MsgBadHost
	JP	Fail
PUHostEnd:
	XOR	A
	LD	(DE),A
	LD	A,B
	LD	(HostLen),A
	OR	A
	JP	Z,Usage
	LD	A,(HL)
	CP	':'
	JR	NZ,PUPath
	INC	HL
	LD	(PortTxt),HL
	LD	DE,0			; el puerto en DE
PUPort:
	LD	A,(HL)
	SUB	'0'
	JR	C,PUPortEnd
	CP	10
	JR	NC,PUPortEnd
	PUSH	HL
	LD	H,D			; DE = DE * 10 + digito
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
	JR	PUPort
PUPortEnd:
	PUSH	HL
	LD	BC,(PortTxt)
	OR	A
	SBC	HL,BC
	LD	A,L
	LD	(PortLen),A
	POP	HL
	LD	A,D
	OR	E
	LD	BC,MsgBadPort
	JR	Z,PUBadPort
	LD	(Port),DE
	LD	A,(HL)
	OR	A
	JR	Z,PUPath
	CP	'/'
	JR	Z,PUPath
	LD	BC,MsgBadPort
PUBadPort:
	LD	H,B
	LD	L,C
	JP	Fail
PUPath:
	LD	(PathPtr),HL
PUFrag:
	LD	A,(HL)			; el #fragmento no va al servidor
	OR	A
	RET	Z
	INC	HL
	CP	'#'
	JR	NZ,PUFrag
	DEC	HL
	LD	(HL),0
	RET

; MakeFcb: el FCB con el archivo pedido. Sin archivo, o con solo la unidad,
; el nombre es el ultimo tramo de la ruta (hasta '?'), o INDEX.HTM.
MakeFcb:
	LD	HL,(PathPtr)
	LD	D,H
	LD	E,L			; DE = comienzo del tramo
MFSeg:
	LD	A,(HL)
	OR	A
	JR	Z,MFSegEnd
	CP	'?'
	JR	Z,MFSegEnd
	INC	HL
	CP	'/'
	JR	NZ,MFSeg
	LD	D,H
	LD	E,L
	JR	MFSeg
MFSegEnd:
	EX	DE,HL
	LD	DE,DefName
	LD	B,63
MFCopy:
	LD	A,(HL)
	OR	A
	JR	Z,MFCopied
	CP	'?'
	JR	Z,MFCopied
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	MFCopy
MFCopied:
	XOR	A
	LD	(DE),A
	LD	A,(DefName)
	OR	A
	JR	NZ,MFDrive
	LD	HL,TxtIndex
	LD	DE,DefName
	LD	BC,10
	LDIR
MFDrive:
	; Unidad
	LD	HL,FName
	XOR	A
	LD	(Fcb),A
	LD	A,(HL)
	OR	A
	JR	Z,MFDef
	INC	HL
	LD	A,(HL)
	DEC	HL
	CP	':'
	JR	NZ,MFName
	LD	A,(HL)
	CALL	Upper
	SUB	'A'-1
	LD	(Fcb),A
	INC	HL
	INC	HL
	LD	A,(HL)
	OR	A
	JR	NZ,MFName
MFDef:
	LD	HL,DefName
MFName:
	; Nombre y extension en blanco, el resto en 0
	PUSH	HL
	LD	HL,Fcb+1
	LD	B,11
MFBlank:
	LD	(HL),' '
	INC	HL
	DJNZ	MFBlank
	CALL	ClearFcb
	POP	HL
	; Nombre: hasta el primer punto, 8 letras
	LD	DE,Fcb+1
	LD	B,8
MFNameLoop:
	LD	A,(HL)
	OR	A
	RET	Z
	CP	'.'
	JR	Z,MFExt
	CALL	FcbChar
	INC	HL
	LD	A,B
	OR	A
	JR	Z,MFNameLoop		; las que sobran se saltean
	LD	A,C
	LD	(DE),A
	INC	DE
	DEC	B
	JR	MFNameLoop
MFExt:
	; Extension: despues del ultimo punto, 3 letras
	LD	D,H
	LD	E,L
MFLastDot:
	INC	HL
	LD	A,(HL)
	OR	A
	JR	Z,MFExtGo
	CP	'.'
	JR	NZ,MFLastDot
	LD	D,H
	LD	E,L
	JR	MFLastDot
MFExtGo:
	EX	DE,HL
	INC	HL
	LD	DE,Fcb+9
	LD	B,3
MFExtLoop:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	FcbChar
	LD	A,C
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	MFExtLoop
	RET

; FcbChar, A = letra del nombre: C = la que va al FCB (mayuscula, o '_' si
; MSX-DOS no la acepta)
FcbChar:
	CALL	Upper
	LD	C,A
	CP	'0'
	JR	C,FCOther
	CP	'9'+1
	RET	C
	CP	'A'
	JR	C,FCOther
	CP	'Z'+1
	RET	C
FCOther:
	PUSH	HL
	PUSH	BC
	LD	HL,FcbOk
	LD	B,FcbOkEnd-FcbOk
FCLoop:
	CP	(HL)
	JR	Z,FCYes
	INC	HL
	DJNZ	FCLoop
	POP	BC
	POP	HL
	LD	C,'_'
	RET
FCYes:
	POP	BC
	POP	HL
	RET

FcbOk:		DB	"!#$%&'()-@^_`{}~"
FcbOkEnd:

; BuildReq: el pedido HTTP en ReqBuf y su largo en SendLeft
BuildReq:
	LD	DE,ReqBuf
	LD	HL,TxtGet		; "GET "
	CALL	CopyZ
	LD	HL,(PathPtr)
	LD	A,(HL)
	OR	A
	JR	NZ,BRPath
	LD	A,'/'
	LD	(DE),A
	INC	DE
	JR	BRProto
BRPath:
	CALL	CopyZ
BRProto:
	LD	HL,TxtProto		; " HTTP/1.0", "Host: "
	CALL	CopyZ
	LD	HL,Host
	CALL	CopyZ
	LD	A,(PortLen)
	OR	A
	JR	Z,BRRest
	LD	B,A
	LD	A,':'
	LD	(DE),A
	INC	DE
	LD	HL,(PortTxt)
BRPort:
	LD	A,(HL)
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	BRPort
BRRest:
	LD	HL,TxtRest		; User-Agent, Connection y la linea en blanco
	CALL	CopyZ
	EX	DE,HL
	LD	DE,ReqBuf
	OR	A
	SBC	HL,DE
	LD	(SendLeft),HL
	RET

; CopyZ, de HL a DE hasta el 0 (sin copiarlo)
CopyZ:
	LD	A,(HL)
	OR	A
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	JR	CopyZ

; Prefix: Z si el texto en HL empieza con el patron en DE (en minusculas,
; ASCIIZ), sin distinguir mayusculas; HL queda despues del patron
Prefix:
	LD	A,(DE)
	OR	A
	RET	Z
	LD	C,A
	LD	A,(HL)
	CALL	Lower
	CP	C
	RET	NZ
	INC	HL
	INC	DE
	JR	Prefix

Lower:
	CP	'A'
	RET	C
	CP	'Z'+1
	RET	NC
	OR	0x20
	RET

Upper:
	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	AND	0xDF
	RET

; ----------------------------------------------------------------------------
; La respuesta

; ProcessRx, HL = bytes recibidos, BC = cuantos
ProcessRx:
	LD	A,B
	OR	C
	RET	Z
	LD	A,(InBody)
	OR	A
	JP	NZ,BodyBytes
	LD	A,(HL)
	INC	HL
	DEC	BC
	PUSH	HL
	PUSH	BC
	CALL	HdrByte
	POP	BC
	POP	HL
	LD	A,(Stop)
	OR	A
	RET	NZ
	JR	ProcessRx

; HdrByte, A = un byte de los encabezados
HdrByte:
	CP	10
	JR	Z,HdrEol
	CP	13
	RET	Z
	LD	C,A
	LD	A,(LineLen)
	CP	LINEMAX
	RET	NC
	LD	E,A
	LD	D,0
	LD	HL,Line
	ADD	HL,DE
	LD	(HL),C
	INC	A
	LD	(LineLen),A
	RET
HdrEol:
	LD	A,(LineLen)
	LD	E,A
	LD	D,0
	LD	HL,Line
	ADD	HL,DE
	LD	(HL),0
	XOR	A
	LD	(LineLen),A
	OR	E
	JR	Z,HdrEnd
	LD	A,(GotStatus)
	OR	A
	JR	Z,HdrStatus
	LD	HL,Line
	LD	DE,TxtCLen
	CALL	Prefix
	JR	Z,HdrCLen
	LD	HL,Line
	LD	DE,TxtLoc
	CALL	Prefix
	RET	NZ
	CALL	SkipSp
	LD	DE,Loc
	LD	B,LINEMAX
	JP	Token			; hasta un espacio: alcanza para una URL
HdrCLen:
	CALL	SkipSp
	CALL	ParseDec32
	LD	(CLen),HL
	LD	(CLen+2),DE
	LD	A,1
	LD	(CLenKnown),A
	RET
HdrStatus:
	LD	A,1
	LD	(GotStatus),A
	LD	HL,Line			; se guarda para mostrarla
	LD	DE,StatusLine
	LD	BC,LINEMAX+1
	LDIR
	LD	HL,Line			; "HTTP/1.x 200 ..."
	LD	DE,TxtHttpVer
	CALL	Prefix
	RET	NZ
HSSkip:
	LD	A,(HL)
	OR	A
	RET	Z
	INC	HL
	CP	' '
	JR	NZ,HSSkip
	CALL	SkipSp
	LD	DE,Txt200
	CALL	Prefix
	RET	NZ
	LD	A,1
	LD	(StatusOk),A
	RET
HdrEnd:
	LD	A,(GotStatus)
	OR	A
	RET	Z			; lineas en blanco antes del estado
	LD	A,(StatusOk)
	OR	A
	JR	Z,HdrStop
	CALL	CheckSpace
	CALL	CreateFile
	LD	A,1
	LD	(InBody),A
	RET
HdrStop:
	LD	A,1
	LD	(Stop),A
	RET

; BodyBytes, HL = bytes del cuerpo, BC = cuantos: al bufer, y al disco cada
; BUFSZ
BodyBytes:
	LD	(BLeft),BC
BBLoop:
	LD	BC,(BLeft)
	LD	A,B
	OR	C
	RET	Z
	PUSH	HL
	LD	HL,BUFSZ		; lugar en el bufer
	LD	DE,(BufFill)
	OR	A
	SBC	HL,DE
	OR	A
	SBC	HL,BC
	JR	NC,BBFit		; entra todo
	ADD	HL,BC
	LD	B,H			; lo que entra
	LD	C,L
BBFit:
	LD	HL,(BLeft)
	OR	A
	SBC	HL,BC
	LD	(BLeft),HL
	LD	HL,(Total)		; Total += BC
	ADD	HL,BC
	LD	(Total),HL
	JR	NC,BBNoCarry
	LD	HL,(Total+2)
	INC	HL
	LD	(Total+2),HL
BBNoCarry:
	LD	HL,(BufFill)
	PUSH	HL
	ADD	HL,BC
	LD	(BufFill),HL
	POP	DE
	LD	HL,Buf
	ADD	HL,DE
	EX	DE,HL			; DE = donde va
	POP	HL			; HL = de donde viene
	LDIR
	PUSH	HL
	LD	HL,(BufFill)
	LD	DE,BUFSZ
	OR	A
	SBC	HL,DE
	CALL	Z,Flush
	POP	HL
	JR	BBLoop

; Complete: sin carry si ya llego todo el cuerpo que anuncio Content-Length
Complete:
	LD	A,(InBody)
	OR	A
	SCF
	RET	Z
	LD	A,(CLenKnown)
	OR	A
	SCF
	RET	Z
				; sigue en CmpTotal
; CmpTotal: carry si Total < CLen
CmpTotal:
	LD	HL,(Total+2)
	LD	DE,(CLen+2)
	OR	A
	SBC	HL,DE
	RET	NZ			; el carry ya dice
	LD	HL,(Total)
	LD	DE,(CLen)
	OR	A
	SBC	HL,DE
	RET

; ----------------------------------------------------------------------------
; El archivo

; CheckSpace: con Content-Length, que entre en el disco (en clusters, con lo
; que libera el archivo que se reemplaza). Si no entra, corta antes de tocar
; nada.
CheckSpace:
	LD	A,(CLenKnown)
	OR	A
	RET	Z
	LD	A,(Fcb)
	LD	E,A
	LD	C,0x1B			; datos del disco
	CALL	BDOS
	CP	0xFF
	RET	Z			; unidad no valida: lo dira la creacion
	LD	(FreeCl),HL
	; ClShift = log2(sectores por cluster * bytes por sector)
	LD	D,0
CSSpc:
	CP	2
	JR	C,CSBps
	SRL	A
	INC	D
	JR	CSSpc
CSBps:
	LD	A,B
	OR	A
	JR	NZ,CSBpsShift
	LD	A,C
	CP	2
	JR	C,CSShiftOk
CSBpsShift:
	SRL	B
	RR	C
	INC	D
	JR	CSBps
CSShiftOk:
	LD	A,D
	LD	(ClShift),A
	; disponibles = libres + los del archivo viejo
	LD	HL,(OldSize)
	LD	DE,(OldSize+2)
	CALL	ToClusters
	LD	BC,(FreeCl)
	ADD	HL,BC
	JR	NC,CSAvail
	INC	DE
CSAvail:
	LD	(AvailCl),HL
	LD	(AvailCl+2),DE
	; necesarios
	LD	HL,(CLen)
	LD	DE,(CLen+2)
	CALL	ToClusters
	; necesarios - disponibles: carry o cero si entra
	LD	BC,(AvailCl)
	OR	A
	SBC	HL,BC
	EX	DE,HL
	LD	BC,(AvailCl+2)
	SBC	HL,BC
	RET	C			; entra con lugar de sobra
	LD	A,H
	OR	L
	OR	D
	OR	E
	RET	Z			; entra justo
	LD	HL,MsgNoSpace1		; "Hacen falta "
	CALL	PrStr
	LD	HL,(CLen)
	LD	DE,(CLen+2)
	CALL	PrDec32
	LD	HL,MsgNoSpace2		; " bytes, en el disco hay "
	CALL	PrStr
	LD	HL,(AvailCl)
	LD	DE,(AvailCl+2)
	LD	A,(ClShift)
	OR	A
	JR	Z,CSPrint
	LD	B,A
CSBytes:
	CALL	Shl32
	DJNZ	CSBytes
CSPrint:
	CALL	PrDec32
	LD	HL,MsgNoSpace3		; "."
	JP	Abort

; ToClusters, DEHL bytes: DEHL = clusters que ocupan (redondeando para arriba)
ToClusters:
	LD	A,(ClShift)
	OR	A
	RET	Z
	LD	B,A
	LD	C,0			; algun bit que se cayo
TCLoop:
	SRL	D
	RR	E
	RR	H
	RR	L
	JR	NC,TCNext
	LD	C,1
TCNext:
	DJNZ	TCLoop
	LD	A,C
	OR	A
	RET	Z
	INC	HL
	LD	A,H
	OR	L
	RET	NZ
	INC	DE
	RET

CreateFile:
	LD	DE,Fcb
	LD	C,0x13			; borrar el anterior, si hay
	CALL	BDOS
	CALL	ClearFcb
	LD	DE,Fcb
	LD	C,0x16			; crear
	CALL	BDOS
	OR	A
	LD	HL,MsgNoCreate
	JP	NZ,Abort
	LD	HL,1			; registros de un byte, desde el 0
	LD	(Fcb+14),HL
	DEC	HL
	LD	(Fcb+33),HL
	LD	(Fcb+35),HL
	LD	A,1
	LD	(FileOpen),A
	RET

; ClearFcb, de la extension (12) al registro al azar (36)
ClearFcb:
	LD	HL,Fcb+12
	LD	B,36-12+1
CFLoop:
	LD	(HL),0
	INC	HL
	DJNZ	CFLoop
	RET

; Flush, el bufer al disco y el avance a la pantalla
Flush:
	LD	HL,(BufFill)
	LD	A,H
	OR	L
	RET	Z
	LD	DE,Buf
	LD	C,0x1A			; direccion de transferencia
	CALL	BDOS
	LD	DE,Fcb
	LD	HL,(BufFill)
	LD	C,0x26			; escritura al azar en bloque
	CALL	BDOS
	OR	A
	LD	HL,MsgFull
	JP	NZ,Abort
	LD	HL,0
	LD	(BufFill),HL
Progress:
	LD	A,13
	CALL	PrChar
	LD	HL,(Total)
	LD	DE,(Total+2)
	CALL	PrDec32
	LD	HL,MsgBytes		; " bytes"
	CALL	PrStr
	LD	A,(CLenKnown)
	OR	A
	RET	Z
	LD	HL,MsgOf		; " de "
	CALL	PrStr
	LD	HL,(CLen)
	LD	DE,(CLen+2)
	JP	PrDec32

PrFcbName:
	LD	A,(Fcb)
	OR	A
	JR	Z,PFNName
	ADD	A,'A'-1
	CALL	PrChar
	LD	A,':'
	CALL	PrChar
PFNName:
	LD	HL,Fcb+1
	LD	B,8
	CALL	PrBlank
	LD	A,(Fcb+9)
	CP	' '
	RET	Z
	LD	A,'.'
	CALL	PrChar
	LD	HL,Fcb+9
	LD	B,3
PrBlank:
	LD	A,(HL)
	CP	' '
	RET	Z
	CALL	PrChar
	INC	HL
	DJNZ	PrBlank
	RET

; ----------------------------------------------------------------------------
; La Pico

; NetConn, HL = host, A = largo (0 corta), DE = puerto
; out: A = 0 aceptado
NetConn:
	LD	B,A
	DI
	LD	A,CMD_NETCONN
	OUT	(SDFCMD),A
	LD	A,E
	OUT	(SDFDATA),A
	LD	A,D
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	OR	A
	JR	Z,NCSent
	LD	C,SDFDATA
	OTIR
NCSent:
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	RET

NetCut:
	LD	A,(PicoGone)
	OR	A
	RET	NZ
	XOR	A
	LD	D,A
	LD	E,A
	CALL	NetConn
	RET

; NetIO, HL = bytes a enviar, A = cuantos (hasta TXMAX)
; out: A = estado, HL = RxBuf, BC = bytes recibidos
NetIO:
	LD	B,A
	DI
	LD	A,CMD_NETIO
	OUT	(SDFCMD),A
	LD	A,255			; lo mas que se acepta recibir
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	OR	A
	JR	Z,NISent
	LD	C,SDFDATA
	OTIR
NISent:
	CALL	Finish
	IN	A,(SDFDATA)
	LD	(NetSt),A
	IN	A,(SDFDATA)
	LD	C,A
	LD	B,0
	OR	A
	JR	Z,NIEnd
	PUSH	BC
	LD	B,A
	LD	C,SDFDATA
	LD	HL,RxBuf
	INIR
	POP	BC
NIEnd:
	EI
	LD	HL,RxBuf
	LD	A,(NetSt)
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
	LD	A,1
	LD	(PicoGone),A		; Abort no le pide nada mas
	LD	HL,MsgNoPico
	JP	Abort

; ----------------------------------------------------------------------------
; Tiempo y teclado

; GetHz: Hz, y los limites de espera en interrupciones
GetHz:
	LD	HL,0x002B		; bit 7 de 002Bh: 1 = 50 Hz
	LD	A,(EXPTBL)
	CALL	RDSLT
	EI
	AND	0x80
	LD	A,60
	JR	Z,GHSet
	LD	A,50
GHSet:
	LD	(Hz),A
	LD	E,A
	LD	D,0
	LD	HL,0
	LD	B,20
GH20:
	ADD	HL,DE
	DJNZ	GH20
	LD	(Hz20),HL
	LD	B,10
GH30:
	ADD	HL,DE
	DJNZ	GH30
	LD	(Hz30),HL
	RET

; IdleOver, HL = limite: carry si todavia no paso desde TIdle
IdleOver:
	EX	DE,HL
	LD	HL,(JIFFY)
	LD	BC,(TIdle)
	OR	A
	SBC	HL,BC
	OR	A
	SBC	HL,DE
	RET

; CheckEsc: ESC o CTRL+C cortan (funcion 06h: sin el CTRL+C del DOS, que
; dejaria el archivo abierto)
CheckEsc:
	LD	A,32
	LD	(EscCount),A
	LD	E,0xFF
	LD	C,0x06
	CALL	BDOS
	CP	27
	JR	Z,Cancel
	CP	3
	RET	NZ
Cancel:
	LD	HL,MsgCancel
	JP	Abort

; PrTime, el tiempo desde TStart en segundos con un decimal
PrTime:
	LD	HL,(JIFFY)
	LD	DE,(TStart)
	OR	A
	SBC	HL,DE
	LD	DE,0
	CALL	Mul10
	LD	A,(Hz)
	LD	C,A
	CALL	DivC			; decimas
	LD	C,10
	CALL	DivC
	PUSH	AF
	CALL	PrDec32
	LD	A,','
	CALL	PrChar
	POP	AF
	ADD	A,'0'
	JP	PrChar

; ----------------------------------------------------------------------------
; Numeros de 32 bits, en DEHL

; ParseDec32, HL = texto: DEHL = el numero
ParseDec32:
	LD	(Ptr),HL
	LD	HL,0
	LD	D,H
	LD	E,L
PDLoop:
	PUSH	HL
	LD	HL,(Ptr)
	LD	A,(HL)
	INC	HL
	LD	(Ptr),HL
	POP	HL
	SUB	'0'
	RET	C
	CP	10
	RET	NC
	PUSH	AF
	CALL	Mul10
	POP	AF
	ADD	A,L
	LD	L,A
	JR	NC,PDLoop
	INC	H
	JR	NZ,PDLoop
	INC	DE
	JR	PDLoop

; Mul10, DEHL = DEHL * 10
Mul10:
	CALL	Shl32			; x2
	PUSH	DE
	PUSH	HL
	CALL	Shl32			; x4
	CALL	Shl32			; x8
	POP	BC
	ADD	HL,BC			; x10
	POP	BC
	EX	DE,HL
	ADC	HL,BC
	EX	DE,HL
	RET

Shl32:
	ADD	HL,HL
	EX	DE,HL
	ADC	HL,HL
	EX	DE,HL
	RET

; DivC, DEHL = DEHL / C, A = resto (C hasta 127)
DivC:
	LD	B,32
	XOR	A
DCLoop:
	ADD	HL,HL
	RL	E
	RL	D
	RLA
	CP	C
	JR	C,DCNext
	SUB	C
	INC	L
DCNext:
	DJNZ	DCLoop
	RET

PrDec32:
	XOR	A
	LD	(Digits),A
P32Div:
	LD	C,10
	CALL	DivC
	PUSH	AF
	LD	A,(Digits)
	INC	A
	LD	(Digits),A
	LD	A,D
	OR	E
	OR	H
	OR	L
	JR	NZ,P32Div
P32Out:
	POP	AF
	ADD	A,'0'
	CALL	PrChar
	LD	A,(Digits)
	DEC	A
	LD	(Digits),A
	JR	NZ,P32Out
	RET

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

; PrZ, texto ASCIIZ de a una letra (puede traer '$')
PrZ:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PrChar
	INC	HL
	JR	PrZ

; ----------------------------------------------------------------------------
; Textos y variables

MsgUsage:	DB	"Uso: GET http://host[:puerto]/ruta",13,10
		DB	"         [archivo]",13,10,'$'
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgHttps:	DB	"HTTPS no esta soportado.",13,10,'$'
MsgBadHost:	DB	"Nombre de servidor no valido.",13,10,'$'
MsgBadPort:	DB	"Puerto no valido.",13,10,'$'
MsgExists1:	DB	"Ya existe $"
MsgExists2:	DB	".",13,10,"Reemplazar (S/N)? $"
MsgConn:	DB	"Conectando a $"
MsgDots:	DB	"...",13,10,'$'
MsgNoConn:	DB	"No se pudo conectar.",13,10,'$'
MsgIdle:	DB	13,10,"El servidor dejo de contestar.",13,10,'$'
MsgNoResp:	DB	"El servidor cerro sin contestar.",13,10,'$'
MsgBadResp:	DB	"Respuesta incompleta: $"
MsgStatus:	DB	"El servidor contesto:",13,10,'$'
MsgLoc:		DB	"Redirige a: $"
MsgShort:	DB	"Se corto antes de terminar.",13,10,'$'
MsgNoSpace1:	DB	"No entra en el disco:",13,10,"hacen falta $"
MsgNoSpace2:	DB	" bytes",13,10,"y hay $"
MsgNoSpace3:	DB	".",13,10,'$'
MsgNoCreate:	DB	"No se pudo crear el archivo.",13,10,'$'
MsgFull:	DB	13,10,"Disco lleno.",13,10,'$'
MsgCancel:	DB	13,10,"Cancelado.",13,10,'$'
MsgDeleted:	DB	"Se borro el archivo a medias.",13,10,'$'
MsgBytes:	DB	" bytes$"
MsgOf:		DB	" de $"
MsgSaved:	DB	"Grabado en $"
MsgIn:		DB	", en $"
MsgSec:		DB	" s",13,10,'$'
MsgCrLf:	DB	13,10,'$'

TxtHttp:	DB	"http://",0
TxtHttps:	DB	"https://",0
TxtHttpVer:	DB	"http/",0
Txt200:		DB	"200",0
TxtCLen:	DB	"content-length:",0
TxtLoc:		DB	"location:",0
TxtIndex:	DB	"INDEX.HTM",0
TxtGet:		DB	"GET ",0
TxtProto:	DB	" HTTP/1.0",13,10,"Host: ",0
TxtRest:	DB	13,10,"User-Agent: GET.COM (MSX)",13,10
		DB	"Connection: close",13,10,13,10,0

Hz:		DB	60
Hz20:		DW	0
Hz30:		DW	0
TStart:		DW	0
TIdle:		DW	0
EscCount:	DB	1
NetSt:		DB	0
HostLen:	DB	0
Port:		DW	80
PortTxt:	DW	0
PortLen:	DB	0
PathPtr:	DW	0
SendPtr:	DW	0
SendLeft:	DW	0
Ptr:		DW	0
Digits:		DB	0
FileOpen:	DB	0
PicoGone:	DB	0
GotStatus:	DB	0
StatusOk:	DB	0
Stop:		DB	0
InBody:		DB	0
CLenKnown:	DB	0
CLen:		DW	0,0
Total:		DW	0,0
BLeft:		DW	0
OldSize:	DW	0,0
FreeCl:		DW	0
AvailCl:	DW	0,0
ClShift:	DB	0
BufFill:	DW	0
LineLen:	DB	0

; Los buferes, despues del programa: no ocupan lugar en el .COM y se llenan
; antes de usarlos (Loc se pone en 0 al empezar)
Fcb		EQU	$
Url		EQU	Fcb+37
FName		EQU	Url+128
DefName		EQU	FName+64
Host		EQU	DefName+64
Line		EQU	Host+HOSTMAX+1
StatusLine	EQU	Line+LINEMAX+1
Loc		EQU	StatusLine+LINEMAX+1
ReqBuf		EQU	Loc+LINEMAX+1
RxBuf		EQU	ReqBuf+400
Buf		EQU	RxBuf+256
