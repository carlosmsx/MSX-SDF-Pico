; CAM.COM para MSX-DOS 1 con el SDF-Pico (docs/CAMARA.md)
;
;   CAM [/C] [/F archivo] [URL]
;   CAM /U [URL | -]
;
; Le pide a la Pico una foto de una camara IP. La Pico baja el JPEG y lo
; convierte a SCREEN 2 (firmware/src/camara.c); aca se pasan las tablas de
; patrones y de colores directo al VDP, o a un archivo que se carga con
; BLOAD"archivo",S. Sin URL se usa la que la Pico tiene guardada (CAM /U).
;
; Se ensambla con PASMO:  pasmo cam.asm cam.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
CALSLT		EQU	0x001C
CHGMOD		EQU	0x005F
TOTEXT		EQU	0x00D2
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E
FORCLR		EQU	0xF3E9		; BAKCLR y BDRCLR siguen
VDPDATA		EQU	0x98
VDPCTRL		EQU	0x99

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_CAMSHOT	EQU	0xB9
CMD_CAMPOLL	EQU	0xBA
CMD_CAMREAD	EQU	0xBB
CMD_CAMURL	EQU	0xBC
CMD_CAMVER	EQU	0xBD

; camara.h
CAM_OCUPADA	EQU	1
CAM_LISTA	EQU	2
CAM_HTTP	EQU	4
CAM_JPEG	EQU	5
CAM_TAMANO	EQU	6
CAM_TLS		EQU	9
CAM_ERRORES	EQU	11		; resultados conocidos (0 a 10)
BLOQUES		EQU	48		; de 256 bytes: 24 de patrones y 24 de colores

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail
	XOR	A
	LD	(Url),A
	LD	(FName),A

	; La linea de comandos (0080h: largo, 0081h: texto)
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0			; termina en 0
	LD	HL,0x0081
Args:
	CALL	SkipSp
	LD	A,(HL)
	OR	A
	JR	Z,ArgsEnd
	CP	'/'
	JR	NZ,ArgUrl
	INC	HL
	LD	A,(HL)
	INC	HL
	AND	0xDF			; MSX-DOS 1 no pasa la linea a mayusculas
	CP	'C'
	JR	NZ,ArgNotC
	LD	(Cont),A
	JR	Args
ArgNotC:
	CP	'U'
	JR	NZ,ArgNotU
	LD	(SetUrl),A
	JR	Args
ArgNotU:
	CP	'F'
	JP	NZ,Usage
	LD	(ToFile),A
	CALL	SkipSp
	LD	DE,FName
	LD	B,63
	CALL	Token
	LD	A,(FName)
	OR	A
	JP	Z,Usage
	JR	Args
ArgUrl:
	LD	A,(Url)
	OR	A
	JP	NZ,Usage		; dos URL
	LD	DE,Url
	LD	B,127
	CALL	Token
	JR	Args
ArgsEnd:
	LD	HL,Url
	CALL	StrLen
	LD	(UrlLen),A
	CALL	GetHz

	LD	A,(SetUrl)
	OR	A
	JP	NZ,DoUrl
	LD	A,(Cont)
	OR	A
	JP	NZ,DoCont

	; Una foto: a la pantalla o al archivo
	LD	HL,MsgShooting		; "Sacando la foto..."
	CALL	PrStr
	CALL	Shot
	LD	A,(ToFile)
	OR	A
	JP	NZ,DoFile
	CALL	Screen2
	CALL	ShowVram
WaitKey:
	CALL	PollKey
	LD	A,(KeyHit)
	OR	A
	JR	Z,WaitKey
	CALL	Text
	LD	HL,MsgPhoto		; "Foto de "
	CALL	PrStr
	CALL	PrInfo
	JP	Exit

; Sin parar, hasta una tecla
DoCont:
	LD	HL,MsgShooting
	CALL	PrStr
	LD	HL,(JIFFY)
	LD	(TLoop),HL
	CALL	Shot
	CALL	Screen2
DCLoop:
	CALL	ShowVram
	LD	HL,(Frames)
	INC	HL
	LD	(Frames),HL
	CALL	PollKey
	LD	A,(KeyHit)
	OR	A
	JR	NZ,DCEnd
	CALL	Shot
	LD	A,(KeyHit)		; la que llego mientras se sacaba no se muestra
	OR	A
	JR	Z,DCLoop
DCEnd:
	CALL	Text
	LD	HL,(Frames)
	CALL	PrDec16
	LD	HL,MsgFrames		; " fotos en "
	CALL	PrStr
	LD	HL,(JIFFY)
	LD	DE,(TLoop)
	OR	A
	SBC	HL,DE
	LD	A,(Hz)
	LD	C,A
	CALL	Div16			; segundos, y el resto en A
	PUSH	HL
	LD	L,A			; decimas: resto * 10 / Hz
	LD	H,0
	ADD	HL,HL
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,DE
	CALL	Div16
	LD	A,L
	POP	HL
	CALL	PrTenths
	JP	Exit

; La foto a un archivo .SC2 (BSAVE de 0000h-37FFh)
DoFile:
	CALL	ReadRam
	CALL	MakeFcb
	; Lo que va entre los patrones y los colores: la tabla de nombres
	; (0-255 en cada tercio) y los sprites apagados (D0h en 1B00h)
	LD	HL,Mid
	LD	C,3
DFNames:
	XOR	A
DFName:
	LD	(HL),A
	INC	HL
	INC	A
	JR	NZ,DFName
	DEC	C
	JR	NZ,DFNames
	LD	(HL),0xD0
	INC	HL
	LD	BC,MIDSZ-768-1
DFZero:
	LD	(HL),0
	INC	HL
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,DFZero

	LD	DE,Fcb
	LD	C,0x13			; borrar el anterior, si hay
	CALL	BDOS
	LD	DE,Fcb
	LD	C,0x16			; crear
	CALL	BDOS
	OR	A
	LD	HL,MsgNoCreate
	JP	NZ,Fail
	LD	HL,1			; registros de un byte, desde el 0
	LD	(Fcb+14),HL
	DEC	HL
	LD	(Fcb+33),HL
	LD	(Fcb+35),HL
	LD	DE,BsaveHdr
	LD	HL,7
	CALL	WrBlock
	LD	DE,Buf			; patrones, 0000h
	LD	HL,6144
	CALL	WrBlock
	LD	DE,Mid			; 1800h
	LD	HL,MIDSZ
	CALL	WrBlock
	LD	DE,Buf+6144		; colores, 2000h
	LD	HL,6144
	CALL	WrBlock
	LD	DE,Fcb
	LD	C,0x10			; cerrar
	CALL	BDOS
	LD	HL,MsgSaved		; "Grabada en "
	CALL	PrStr
	CALL	PrFcbName
	LD	HL,MsgColon		; ": "
	CALL	PrStr
	CALL	PrInfo
	JP	Exit

; DE = datos, HL = bytes. Si no entra, borra el archivo.
WrBlock:
	PUSH	HL
	LD	C,0x1A			; direccion de transferencia
	CALL	BDOS
	POP	HL
	LD	DE,Fcb
	LD	C,0x26			; escritura al azar en bloque
	CALL	BDOS
	OR	A
	RET	Z
	LD	DE,Fcb
	LD	C,0x10
	CALL	BDOS
	LD	DE,Fcb
	LD	C,0x13
	CALL	BDOS
	LD	HL,MsgFull
	JP	Fail

; CAM /U: guarda la URL, la olvida (-) o la muestra (sin URL)
DoUrl:
	LD	A,(Url)
	OR	A
	JR	Z,ShowUrl
	LD	A,(UrlLen)
	CP	1
	JR	NZ,DUSend
	LD	A,(Url)
	CP	'-'
	LD	A,1
	JR	NZ,DUSend
	XOR	A			; "-": largo 0, la olvida
DUSend:
	LD	(UrlLen),A
	LD	B,A
	DI
	LD	A,CMD_CAMURL
	OUT	(SDFCMD),A
	LD	A,B
	OUT	(SDFDATA),A
	OR	A
	JR	Z,DUFin
	LD	HL,Url
	LD	C,SDFDATA
	OTIR
DUFin:
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	CP	0xFF
	JP	Z,OldFw
	OR	A
	LD	HL,MsgBadUrl
	JP	NZ,Fail
	LD	A,(UrlLen)
	OR	A
	LD	HL,MsgForgot		; "URL olvidada."
	JP	Z,Fail
	LD	HL,MsgSavedUrl		; "URL guardada."
	JP	Fail

ShowUrl:
	DI
	LD	A,CMD_CAMVER
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
	LD	HL,MsgUrl		; "URL: "
	CALL	PrStr
	LD	HL,Line
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	Fail
SUOld:
	EI
	JP	OldFw

Usage:
	LD	HL,MsgUsage
Fail:
	PUSH	HL
	LD	A,(InGfx)
	OR	A
	CALL	NZ,Text
	POP	HL
	CALL	PrStr
Exit:
	JP	0			; vuelve al DOS

OldFw:
	LD	HL,MsgOldFw
	JR	Fail

; ----------------------------------------------------------------------------
; La foto

; Pide una foto y espera que este. Si falla, sale con el mensaje.
Shot:
	LD	A,(UrlLen)
	CALL	CamShot
	CP	CAM_OCUPADA
	JR	NZ,ShotSent
	CALL	WaitDone		; la de un CAM cancelado
	LD	A,(UrlLen)
	CALL	CamShot
ShotSent:
	CP	0xFF
	JP	Z,OldFw
	OR	A
	LD	HL,MsgBadUrl
	JP	NZ,Fail
	CALL	WaitDone
	OR	A
	RET	Z
	; A = resultado (camara.h)
	CP	CAM_HTTP
	JR	Z,ErrHttp
	CP	CAM_JPEG
	JR	Z,ErrJpeg
	CP	CAM_TAMANO
	JR	Z,ErrSize
	CP	CAM_TLS
	JR	Z,ErrTls
	CP	CAM_ERRORES
	JR	C,ErrTable
	XOR	A
ErrTable:
	ADD	A,A
	LD	E,A
	LD	D,0
	LD	HL,ErrTab
	ADD	HL,DE
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	EX	DE,HL
	JP	Fail
ErrHttp:
	CALL	LeaveGfx
	LD	HL,MsgHttp		; "La camara contesto "
	CALL	PrStr
	LD	HL,(PollBuf+2)
	CALL	PrDec16
	LD	HL,(PollBuf+2)
	LD	DE,401
	OR	A
	SBC	HL,DE
	LD	HL,MsgDot
	JP	NZ,Fail
	LD	HL,MsgAuth		; " (usuario o clave?)."
	JP	Fail
ErrJpeg:
	CALL	LeaveGfx
	LD	HL,MsgJpeg		; "...(error "
	CALL	PrStr
	LD	HL,(PollBuf+2)
	CALL	PrDec16
	LD	HL,MsgParen		; ")."
	JP	Fail
ErrTls:
	CALL	LeaveGfx
	LD	HL,MsgTls		; "...(error "
	CALL	PrStr
	LD	HL,(PollBuf+2)
	CALL	PrDec16
	LD	HL,MsgParen		; ")."
	JP	Fail
ErrSize:
	CALL	LeaveGfx
	LD	HL,MsgSize		; "Tamano no soportado: "
	CALL	PrStr
	CALL	PrSize
	LD	HL,MsgDot
	JP	Fail

; Hasta que la foto este (CAM_LISTA). Devuelve el resultado en A. Una tecla
; cancela, salvo con /C: ahi solo queda anotada para terminar.
WaitDone:
	LD	HL,(JIFFY)
	LD	(TStart),HL
WDLoop:
	CALL	PollKey
	LD	A,(KeyHit)
	OR	A
	JR	Z,WDPoll
	LD	A,(Cont)
	OR	A
	LD	HL,MsgCancel
	JP	Z,Fail
WDPoll:
	CALL	CamPoll
	CP	CAM_LISTA
	JR	Z,WDDone
	CP	0xFF
	JP	Z,OldFw
	LD	HL,(JIFFY)
	LD	DE,(TStart)
	OR	A
	SBC	HL,DE
	LD	DE,(Hz30)
	OR	A
	SBC	HL,DE
	JR	C,WDLoop
	LD	HL,MsgPicoTime
	JP	Fail
WDDone:
	LD	A,(PollBuf+1)
	RET

; A = largo de la URL (0: la guardada); devuelve la respuesta en A
CamShot:
	LD	B,A
	DI
	LD	A,CMD_CAMSHOT
	OUT	(SDFCMD),A
	LD	A,B
	OUT	(SDFDATA),A
	OR	A
	JR	Z,CSSent
	LD	HL,Url
	LD	C,SDFDATA
	OTIR
CSSent:
	CALL	Finish
	IN	A,(SDFDATA)
	EI
	RET

; Los 10 bytes del estado en PollBuf; devuelve el primero en A
CamPoll:
	DI
	LD	A,CMD_CAMPOLL
	OUT	(SDFCMD),A
	CALL	Finish
	LD	HL,PollBuf
	LD	BC,10*256+SDFDATA
	INIR
	EI
	LD	A,(PollBuf)
	RET

; Los 48 bloques directo al VDP: los 24 primeros son los patrones (0000h),
; los otros los colores (2000h). Entre un OUT y el siguiente pasan 35
; ciclos: mas que los 29 que pide el TMS9918 en SCREEN 2 con la pantalla
; encendida.
ShowVram:
	LD	E,0
SVLoop:
	LD	A,E
	CP	24
	JR	C,SVAddr
	ADD	A,0x20-24
SVAddr:
	LD	D,A
	DI
	XOR	A
	OUT	(VDPCTRL),A
	LD	A,D
	OR	0x40			; escritura
	OUT	(VDPCTRL),A
	LD	A,CMD_CAMREAD
	OUT	(SDFCMD),A
	LD	A,E
	OUT	(SDFDATA),A
	PUSH	DE
	CALL	Finish
	POP	DE
	LD	B,0
SVByte:
	IN	A,(SDFDATA)
	OUT	(VDPDATA),A
	DJNZ	SVByte
	EI
	INC	E
	LD	A,E
	CP	BLOQUES
	JR	NZ,SVLoop
	RET

; Los 48 bloques a Buf
ReadRam:
	LD	HL,Buf
	LD	E,0
RRLoop:
	DI
	LD	A,CMD_CAMREAD
	OUT	(SDFCMD),A
	LD	A,E
	OUT	(SDFDATA),A
	PUSH	DE
	PUSH	HL
	CALL	Finish
	POP	HL
	POP	DE
	LD	BC,0*256+SDFDATA	; B = 0: 256 bytes
	INIR
	EI
	INC	E
	LD	A,E
	CP	BLOQUES
	JR	NZ,RRLoop
	RET

; ----------------------------------------------------------------------------
; Pantalla

Screen2:
	LD	HL,FORCLR		; los colores de DOS, para volver
	LD	DE,OldClr
	LD	BC,3
	LDIR
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
; Linea de comandos y archivo

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

; El FCB desde FName: [d:]nombre[.ext], .SC2 si no tiene extension
MakeFcb:
	LD	HL,Fcb
	LD	B,37
MFClear:
	LD	(HL),0
	INC	HL
	DJNZ	MFClear
	LD	HL,Fcb+1
	LD	B,11
MFBlank:
	LD	(HL),' '
	INC	HL
	DJNZ	MFBlank
	LD	HL,FName
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
MFName:
	LD	DE,Fcb+1
	LD	B,8
MFNameLoop:
	LD	A,(HL)
	OR	A
	JR	Z,MFDefExt
	CP	'.'
	JR	Z,MFExt
	CALL	Upper
	LD	(DE),A
	INC	DE
	INC	HL
	DJNZ	MFNameLoop
MFSkip:					; lo que pasa de 8 no va
	LD	A,(HL)
	OR	A
	JR	Z,MFDefExt
	CP	'.'
	JR	Z,MFExt
	INC	HL
	JR	MFSkip
MFExt:
	INC	HL
	LD	DE,Fcb+9
	LD	B,3
MFExtLoop:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	Upper
	LD	(DE),A
	INC	DE
	INC	HL
	DJNZ	MFExtLoop
	RET
MFDefExt:
	LD	HL,TxtSc2
	LD	DE,Fcb+9
	LD	BC,3
	LDIR
	RET

Upper:
	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	SUB	0x20
	RET

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
; Bus, teclado y tiempo

; Espera que la Pico baje OCUPADO. Si no contesta, sale.
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

; Una tecla, si hay, queda en KeyHit
PollKey:
	LD	E,0xFF
	LD	C,0x06			; entrada directa, sin eco
	CALL	BDOS
	OR	A
	RET	Z
	LD	(KeyHit),A
	RET

; Hz: 50 o 60, segun la ROM; Hz30: 30 segundos
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
	LD	B,30
GH30:
	ADD	HL,DE
	DJNZ	GH30
	LD	(Hz30),HL
	RET

; ----------------------------------------------------------------------------
; Numeros y textos

; HL = HL / C, A = resto (C hasta 127)
Div16:
	XOR	A
	LD	B,16
D16Loop:
	ADD	HL,HL
	RLA
	CP	C
	JR	C,D16Next
	SUB	C
	INC	L
D16Next:
	DJNZ	D16Loop
	RET

PrDec16:
	LD	E,0
PDLoop:
	LD	C,10
	PUSH	DE
	CALL	Div16
	POP	DE
	PUSH	AF
	INC	E
	LD	A,H
	OR	L
	JR	NZ,PDLoop
PDOut:
	POP	AF
	ADD	A,'0'
	CALL	PrChar
	DEC	E
	JR	NZ,PDOut
	RET

; "704x576 en 1,2 s." con lo que dejo CMD_CAMPOLL
PrInfo:
	CALL	PrSize
	LD	HL,MsgIn		; " en "
	CALL	PrStr
	LD	HL,(PollBuf+8)		; ms
	LD	C,100
	CALL	Div16			; decimas
	LD	C,10
	CALL	Div16
; HL segundos y A decimas: "1,2 s."
PrTenths:
	PUSH	AF
	CALL	PrDec16
	LD	A,','
	CALL	PrChar
	POP	AF
	ADD	A,'0'
	CALL	PrChar
	LD	HL,MsgSecDot
	JP	PrStr

PrSize:
	LD	HL,(PollBuf+4)
	CALL	PrDec16
	LD	A,'x'
	CALL	PrChar
	LD	HL,(PollBuf+6)
	JP	PrDec16

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

PrZ:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PrChar
	INC	HL
	JR	PrZ

MsgUsage:	DB	"Uso: CAM [/C] [/F archivo] [URL]",13,10
		DB	"     CAM /U [URL | -]",13,10
		DB	"Saca una foto de una camara IP y la",13,10
		DB	"muestra en SCREEN 2 hasta una tecla.",13,10
		DB	"  /C  sin parar, hasta una tecla",13,10
		DB	"  /F  la graba en un archivo .SC2",13,10
		DB	"      (BLOAD",34,"archivo",34,",S)",13,10
		DB	"  /U  guarda la URL en la Pico; - la",13,10
		DB	"      olvida y sin URL la muestra",13,10
		DB	"Sin URL usa la guardada. La URL:",13,10
		DB	"http://usuario:clave@ip:puerto/ruta",13,10
		DB	"(sin :puerto va al 80) o",13,10
		DB	"https://servidor/ruta",13,10,'$'
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgOldFw:	DB	"El firmware de la Pico no maneja la",13,10
		DB	"camara (hace falta el 0.11).",13,10,'$'
MsgShooting:	DB	"Sacando la foto...",13,10,'$'
MsgBadUrl:	DB	"URL no valida: http://usuario:clave",13,10
		DB	"@ip/ruta o https://servidor/ruta",13,10
		DB	"(HTTPS desde el firmware 0.15).",13,10,'$'
MsgCancel:	DB	"Cancelado.",13,10,'$'
MsgPicoTime:	DB	"La Pico no termino la foto.",13,10,'$'
MsgUnknown:	DB	"Error desconocido de la Pico.",13,10,'$'
MsgNoWifi:	DB	"La Pico no esta conectada al Wi-Fi",13,10
		DB	"(WIFI.COM).",13,10,'$'
MsgNoHost:	DB	"No se encontro la camara (nombre).",13,10,'$'
MsgNoConn:	DB	"No se pudo conectar con la camara.",13,10,'$'
MsgTimeout:	DB	"La camara dejo de mandar.",13,10,'$'
MsgNoUrl:	DB	"No hay URL guardada: CAM /U URL",13,10,'$'
MsgTls:		DB	"Fallo el HTTPS con el servidor",13,10,"(error $"
MsgNoMem:	DB	"A la Pico no le alcanzo la RAM",13,10,"para el HTTPS.",13,10,'$'
MsgHttp:	DB	"La camara contesto $"
MsgAuth:	DB	".",13,10,"Usuario o clave incorrectos?",13,10,'$'
MsgJpeg:	DB	"La imagen no es un JPEG soportado",13,10,"(error $"
MsgParen:	DB	").",13,10,'$'
MsgSize:	DB	"Tamano no soportado: $"
MsgDot:		DB	".",13,10,'$'
MsgSavedUrl:	DB	"URL guardada.",13,10,'$'
MsgForgot:	DB	"URL olvidada.",13,10,'$'
MsgUrl:		DB	"URL: $"
MsgPhoto:	DB	"Foto de $"
MsgIn:		DB	" en $"
MsgSecDot:	DB	" s.",13,10,'$'
MsgFrames:	DB	" fotos en $"
MsgSaved:	DB	"Grabada en $"
MsgColon:	DB	":",13,10,'$'
MsgNoCreate:	DB	"No se pudo crear el archivo.",13,10,'$'
MsgFull:	DB	"Disco lleno.",13,10,'$'
MsgCrLf:	DB	13,10,'$'

; Mensaje de cada resultado de CMD_CAMPOLL (los que llevan datos, aparte)
ErrTab:		DW	MsgUnknown, MsgNoWifi, MsgNoHost, MsgNoConn, MsgUnknown
		DW	MsgUnknown, MsgUnknown, MsgTimeout, MsgNoUrl
		DW	MsgUnknown, MsgNoMem

BsaveHdr:	DB	0xFE
		DW	0x0000, 0x37FF, 0x0000
TxtSc2:		DB	"SC2"

Cont:		DB	0
SetUrl:		DB	0
ToFile:		DB	0
KeyHit:		DB	0
InGfx:		DB	0
UrlLen:		DB	0
Hz:		DB	60
Hz30:		DW	1800
TStart:		DW	0
TLoop:		DW	0
Frames:		DW	0
OldClr:		DS	3
PollBuf:	DS	10

; Los buferes, despues del programa: no ocupan lugar en el .COM
LINEMAX		EQU	230
MIDSZ		EQU	0x2000-0x1800		; de 1800h a 1FFFh
Fcb		EQU	$
Url		EQU	Fcb+37
FName		EQU	Url+128
Line		EQU	FName+64
Mid		EQU	Line+LINEMAX+1
Buf		EQU	Mid+MIDSZ
