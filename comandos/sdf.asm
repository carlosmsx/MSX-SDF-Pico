; SDF.COM para MSX-DOS 1 con el SDF-Pico (docs/SD.md)
;
;   SDF                 estado: firmware, SD e imagenes montadas
;   SDF DIR             imagenes .DSK de la SD
;   SDF MOUNT d: X      monta X.DSK en d: (A: o B:)
;   SDF UMOUNT d:       deja d: sin imagen
;   SDF NEW X [360]     crea X.DSK vacia y formateada (720 KB si no se dice)
;   SDF DRIVES n        unidades que ve el DOS, 1 o 2, desde el proximo arranque
;
; Los CALL SDF... de BASIC desde el DOS, con los mismos comandos del
; protocolo, mas CMD_SDFINFO para saber que hay montado. Despues de MOUNT el
; DOS se entera solo del cambio (DSKCHG).
;
; Se ensambla con PASMO:  pasmo sdf.asm sdf.com

BDOS		EQU	0x0005
LINLEN		EQU	0xF3B0

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01
CMD_SDFTEST	EQU	0xD1
CMD_SDFMOUNT	EQU	0xE1
CMD_SDFFILES	EQU	0xE2
CMD_SDFUMOUNT	EQU	0xE3
CMD_SDFNEW	EQU	0xE4
CMD_SDFDRIVES	EQU	0xE5
CMD_SDFINFO	EQU	0xE6
CMD_WRITE	EQU	0xF0

SDFNEW_BUSY	EQU	0xFE		; "todavia llenando la imagen", uno por segundo
NEW_DRIVE	EQU	2		; la imagen recien creada
MEDIA_720K	EQU	0xF9
MEDIA_360K	EQU	0xF8
EST_ERROR	EQU	0		; bit del registro de estado
ARGMAX		EQU	12		; 8.3
LISTMAX		EQU	4096		; lo que se guarda del listado

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail

	; La linea de comandos (0080h: largo, 0081h: texto), terminada en 0
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0
	LD	HL,0x0081
	LD	(ArgPtr),HL
	CALL	NextArg
	JP	Z,Status
	LD	HL,Commands
Dispatch:
	LD	A,(HL)
	OR	A
	JP	Z,Usage
	LD	DE,Arg
	PUSH	HL
DCmp:
	LD	A,(DE)
	CP	(HL)
	JR	NZ,DNext
	OR	A
	JR	Z,DFound
	INC	HL
	INC	DE
	JR	DCmp
DNext:
	POP	HL
DSkip:
	LD	A,(HL)			; al final del nombre, y la direccion
	INC	HL
	OR	A
	JR	NZ,DSkip
	INC	HL
	INC	HL
	JR	Dispatch
DFound:
	POP	DE
	INC	HL
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	JP	(HL)

Commands:
	DB	"DIR",0
	DW	Dir
	DB	"MOUNT",0
	DW	Mount
	DB	"UMOUNT",0
	DW	Umount
	DB	"NEW",0
	DW	New
	DB	"DRIVES",0
	DW	Drives
	DB	"/?",0
	DW	Usage
	DB	0

Usage:
	LD	HL,MsgUsage
Fail:
	CALL	PrStr
Exit:
	EI
	JP	0			; vuelve al DOS

; ----------------------------------------------------------------------------
; Los comandos

; SDF: version, SD e imagenes
Status:
	DI
	LD	A,CMD_SDFTEST
	CALL	SendCmd
	CALL	ReadByte		; protocolo
	OR	A
	LD	HL,MsgOldFw
	JP	Z,Fail
	LD	HL,Text			; "SDF-Pico 0.9",13,10,"SD ok"
	LD	B,40
	CALL	ReadZ
	EI
	LD	HL,Text
	CALL	PrZ
	LD	HL,MsgCrLf
	CALL	PrStr
	CALL	GetInfo
	LD	A,0
	CALL	PrMounted
	LD	A,1
	CALL	PrMounted
	LD	A,(Info)
	CP	1
	JP	NZ,Exit
	LD	HL,MsgOneDrive
	JP	Fail

; SDF DIR
Dir:
	DI
	LD	A,CMD_SDFFILES
	CALL	SendCmd
	LD	HL,List			; todo a la memoria, y despues a la pantalla
DirRead:
	PUSH	HL			; si no entra otro nombre, se corta aca
	LD	DE,List+LISTMAX-14
	OR	A
	SBC	HL,DE
	POP	HL
	JR	NC,DirEnd
	CALL	ReadByte
	LD	(HL),A
	INC	HL
	OR	A
	JR	Z,DirEnd		; nombre vacio: no hay mas
DirReadChar:
	CALL	ReadByte
	LD	(HL),A
	INC	HL
	OR	A
	JR	NZ,DirReadChar
	JR	DirRead
DirEnd:
	LD	(HL),0
	EI
	; Columnas de 13: las que entran en el ancho
	LD	A,(LINLEN)
	LD	C,0
DirCols:
	INC	C
	SUB	13
	JR	NC,DirCols
	DEC	C
	JR	NZ,DirColsOk
	INC	C
DirColsOk:
	LD	A,C
	LD	(Cols),A
	LD	HL,List
	LD	A,(HL)
	OR	A
	LD	HL,MsgNoImages
	JP	Z,Fail
	LD	HL,List
	LD	B,0			; columna
DirName:
	LD	A,(HL)
	OR	A
	JR	Z,DirDone
	LD	C,13
DirChar:
	LD	A,(HL)
	INC	HL
	OR	A
	JR	Z,DirPad
	CALL	PrChar
	DEC	C
	JR	DirChar
DirPad:
	INC	B
	LD	A,(Cols)
	CP	B
	JR	Z,DirRow
	LD	A,(HL)			; despues del ultimo no hace falta relleno
	OR	A
	JR	Z,DirDone
DirSpace:
	LD	A,' '
	CALL	PrChar
	DEC	C
	JR	NZ,DirSpace
	JR	DirName
DirRow:
	LD	B,0
	PUSH	HL
	LD	HL,MsgCrLf
	CALL	PrStr
	POP	HL
	JR	DirName
DirDone:
	LD	A,B
	OR	A
	JP	Z,Exit
	LD	HL,MsgCrLf
	JP	Fail

; SDF MOUNT d: X
Mount:
	CALL	DriveArg
	LD	(Drive),A
	CALL	NextArg
	JP	Z,Usage
	DI
	LD	A,CMD_SDFMOUNT
	CALL	SendCmd
	LD	A,(Drive)
	OUT	(SDFDATA),A
	CALL	SendArg
	CALL	Result
	CALL	GetInfo
	LD	A,(Drive)
	CALL	PrMounted
	JP	Exit

; SDF UMOUNT d:
Umount:
	CALL	DriveArg
	LD	(Drive),A
	DI
	LD	A,CMD_SDFUMOUNT
	CALL	SendCmd
	LD	A,(Drive)
	OUT	(SDFDATA),A
	CALL	Result
	CALL	GetInfo
	LD	A,(Drive)
	CALL	PrMounted
	JP	Exit

; SDF DRIVES n
Drives:
	CALL	NextArg
	JP	Z,Usage
	LD	A,(Arg)
	SUB	'0'
	LD	(Drive),A
	DI
	LD	A,CMD_SDFDRIVES
	CALL	SendCmd
	LD	A,(Drive)
	OUT	(SDFDATA),A
	CALL	Result
	LD	HL,MsgDrives1		; "Unidades del DOS al arrancar: "
	CALL	PrStr
	LD	A,(Drive)
	ADD	A,'0'
	CALL	PrChar
	LD	HL,MsgDotCrLf
	JP	Fail

; SDF NEW X [360]
New:
	CALL	NextArg
	JP	Z,Usage
	LD	HL,Arg			; el nombre, para el pedido y los mensajes
	LD	DE,Name
	LD	BC,ARGMAX+1
	LDIR
	LD	A,(ArgLen)
	LD	(NameLen),A
	LD	IX,Fmt720
	CALL	NextArg
	JR	Z,NewGo
	LD	HL,Arg
	LD	DE,Txt720
	CALL	StrEq
	JR	Z,NewGo
	LD	HL,Arg
	LD	DE,Txt360
	CALL	StrEq
	JP	NZ,Usage
	LD	IX,Fmt360
NewGo:
	LD	HL,MsgCreating		; "Creando "
	CALL	PrStr
	LD	HL,Name
	CALL	PrZ
	LD	HL,MsgDots
	CALL	PrStr
	DI
	LD	A,CMD_SDFNEW
	CALL	SendCmd
	LD	A,(IX+0)		; media: el tamano
	OUT	(SDFDATA),A
	LD	HL,Name
	LD	A,(NameLen)
	CALL	SendText
	; La Pico la llena de ceros: un SDFNEW_BUSY por segundo
	LD	DE,120			; hasta 2 minutos
NewWait:
	PUSH	DE
	CALL	ReadByte
	POP	DE
	CP	SDFNEW_BUSY
	JR	NZ,NewFilled
	LD	B,0			; ~2 ms: que la Pico vuelva a levantar OCUPADO
NewPause:
	EX	(SP),HL
	EX	(SP),HL
	DJNZ	NewPause
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,NewWait
	LD	A,69			; Disk I/O error: no termina
NewFilled:
	CALL	CheckErr
	CALL	Format
	EI
	LD	HL,MsgCreated		; "Creada, de "
	CALL	PrStr
	LD	L,(IX+3)
	LD	H,(IX+4)
	CALL	PrDec16
	LD	HL,MsgKb		; " KB. SDF MOUNT para usarla."
	JP	Fail

; Format: los sectores de sistema de la imagen recien creada (NEW_DRIVE),
; como FORMAT de la DiskROM pero con un sector de arranque propio: disco de
; datos, con un RET en la entrada de arranque (si se arranca de ahi, sigue
; BASIC). IX = formato.
Format:
	LD	A,CMD_WRITE
	CALL	SendCmd
	LD	A,NEW_DRIVE
	OUT	(SDFDATA),A
	LD	A,(IX+1)		; sectores de sistema
	OUT	(SDFDATA),A
	LD	A,(IX+0)		; media
	OUT	(SDFDATA),A
	XOR	A
	OUT	(SDFDATA),A		; direccion: el firmware no la usa
	OUT	(SDFDATA),A
	OUT	(SDFDATA),A		; desde el sector 0
	OUT	(SDFDATA),A
	CALL	ReadByte		; 0, o el error de DSKIO
	OR	A
	JR	NZ,FormatErr
	LD	(Sec),A
FmtSector:
	LD	HL,SecBuf		; en cero
	LD	DE,SecBuf+1
	LD	BC,511
	LD	(HL),0
	LDIR
	LD	A,(Sec)
	OR	A
	JR	NZ,FmtFat
	; Sector de arranque: salto, nombre, BPB y RET en 1Eh
	LD	HL,BootHead
	LD	DE,SecBuf
	LD	BC,BootHeadEnd-BootHead
	LDIR
	PUSH	IX
	POP	HL
	LD	BC,5
	ADD	HL,BC
	LD	BC,19
	LDIR
	LD	A,0xC9
	LD	(DE),A
	JR	FmtSend
FmtFat:
	; Primer sector de cada FAT: media, FFh, FFh
	CP	1
	JR	Z,FmtFatHead
	DEC	A
	CP	(IX+2)			; sectores por FAT
	JR	NZ,FmtSend
FmtFatHead:
	LD	A,(IX+0)
	LD	(SecBuf),A
	LD	A,0xFF
	LD	(SecBuf+1),A
	LD	(SecBuf+2),A
FmtSend:
	LD	HL,SecBuf
	LD	BC,SDFDATA		; B = 0: 256 bytes por OTIR
	OTIR
	OTIR
	CALL	Wait			; la Pico tiene OCUPADO hasta recibir el sector
	LD	A,(Sec)
	INC	A
	LD	(Sec),A
	CP	(IX+1)
	JR	NZ,FmtSector
	; Con el ultimo sector graba todo; si falla, levanta ERROR en el estado
	CALL	Wait
	IN	A,(SDFCMD)
	BIT	EST_ERROR,A
	RET	Z
FormatErr:
	EI
	LD	HL,MsgFmtErr
	JP	Fail

BootHead:	DB	0xEB,0xFE,0x90,"SDFPICO "
BootHeadEnd:

; Un formato por tamano: media, sectores de sistema, sectores por FAT, KB, y
; el BPB del sector de arranque (bytes 0Bh-1Dh)
Fmt720:
	DB	MEDIA_720K,1+3+3+7,3
	DW	720
	DW	512
	DB	2
	DW	1
	DB	2
	DW	112,1440
	DB	MEDIA_720K
	DW	3,9,2,0
Fmt360:
	DB	MEDIA_360K,1+2+2+7,2
	DW	360
	DW	512
	DB	2
	DW	1
	DB	2
	DW	112,720
	DB	MEDIA_360K
	DW	2,9,1,0

; ----------------------------------------------------------------------------
; Argumentos

; NextArg: la palabra siguiente en Arg, en mayusculas, y su largo en ArgLen
; out: Z si no hay mas
NextArg:
	LD	HL,(ArgPtr)
NASkip:
	LD	A,(HL)
	CP	' '
	JR	NZ,NACopy
	INC	HL
	JR	NASkip
NACopy:
	LD	DE,Arg
	LD	B,0
NALoop:
	LD	A,(HL)
	OR	A
	JR	Z,NAEnd
	CP	' '
	JR	Z,NAEnd
	INC	HL
	CP	'a'
	JR	C,NAStore
	CP	'z'+1
	JR	NC,NAStore
	AND	0xDF
NAStore:
	LD	C,A
	LD	A,B
	CP	ARGMAX
	JR	NC,NALoop		; lo que sobra se descarta
	LD	A,C
	LD	(DE),A
	INC	DE
	INC	B
	JR	NALoop
NAEnd:
	LD	(ArgPtr),HL
	XOR	A
	LD	(DE),A
	LD	A,B
	LD	(ArgLen),A
	OR	A
	RET

; DriveArg: A = 0 para A:, 1 para B:
DriveArg:
	CALL	NextArg
	JP	Z,Usage
	LD	A,(Arg+1)
	OR	A
	JR	Z,DAOk
	CP	':'
	JR	NZ,DABad
	LD	A,(Arg+2)
	OR	A
	JR	NZ,DABad
DAOk:
	LD	A,(Arg)
	SUB	'A'
	CP	2
	RET	C
DABad:
	LD	HL,MsgBadDrive
	JP	Fail

; StrEq: Z si los textos en HL y DE son iguales
StrEq:
	LD	A,(DE)
	CP	(HL)
	RET	NZ
	OR	A
	RET	Z
	INC	HL
	INC	DE
	JR	StrEq

; ----------------------------------------------------------------------------
; La Pico (con las interrupciones cortadas: que nada mande otro comando en
; el medio)

; SendCmd, A = comando, y el tiempo para que la Pico levante OCUPADO
SendCmd:
	OUT	(SDFCMD),A
	LD	B,25
SCDelay:
	DJNZ	SCDelay
	RET

; SendArg, el largo y el texto de Arg
SendArg:
	LD	HL,Arg
	LD	A,(ArgLen)
SendText:
	OUT	(SDFDATA),A
	OR	A
	RET	Z
	LD	B,A
	LD	C,SDFDATA
	OTIR
	RET

; Wait, OCUPADO en 0 (hasta unos 3 s). Si no, la Pico no esta.
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
	LD	HL,MsgNoPico
	JP	Fail
WaitOk:
	POP	DE
	POP	BC
	RET

ReadByte:
	CALL	Wait
	IN	A,(SDFDATA)
	RET

; ReadZ, un ASCIIZ en HL, B como maximo
ReadZ:
	CALL	ReadByte
	LD	(HL),A
	OR	A
	RET	Z
	INC	HL
	DJNZ	ReadZ
	LD	(HL),0
	RET

; Result: el byte de respuesta, 0 o el error de BASIC
Result:
	CALL	ReadByte
	EI
CheckErr:
	OR	A
	RET	Z
	EI
	LD	HL,Errors
CELoop:
	LD	C,(HL)
	INC	HL
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	INC	C			; 0FFh: fin de la tabla
	JR	Z,CEOther
	DEC	C
	CP	C
	JR	NZ,CELoop
	EX	DE,HL
	JP	Fail
CEOther:
	PUSH	AF
	LD	HL,MsgErr		; "Error "
	CALL	PrStr
	POP	AF
	LD	L,A
	LD	H,0
	CALL	PrDec16
	LD	HL,MsgDotCrLf
	JP	Fail

Errors:
	DB	5
	DW	MsgIllegal
	DB	53
	DW	MsgNotFound
	DB	56
	DW	MsgBadName
	DB	61
	DW	MsgBadSize
	DB	62
	DW	MsgBadDrive
	DB	65
	DW	MsgExists
	DB	66
	DW	MsgFull
	DB	69
	DW	MsgIo
	DB	70
	DW	MsgNoSd
	DB	0xFF
	DW	0

; GetInfo: CMD_SDFINFO en Info: drives, imagen de A: e imagen de B:
GetInfo:
	DI
	LD	A,CMD_SDFINFO
	CALL	SendCmd
	CALL	ReadByte
	LD	(Info),A
	LD	HL,InfoA
	LD	B,ARGMAX+1
	CALL	ReadZ
	LD	HL,InfoB
	LD	B,ARGMAX+1
	CALL	ReadZ
	EI
	LD	A,(Info)		; 1 o 2; otra cosa es un firmware sin SDFINFO
	DEC	A
	CP	2
	RET	C
	LD	HL,MsgOldFw
	JP	Fail

; PrMounted, A = drive: "A: JUEGOS.DSK" o "A: sin imagen"
PrMounted:
	PUSH	AF
	ADD	A,'A'
	CALL	PrChar
	LD	HL,MsgColon
	CALL	PrStr
	POP	AF
	LD	HL,InfoA
	OR	A
	JR	Z,PMName
	LD	HL,InfoB
PMName:
	LD	A,(HL)
	OR	A
	JR	NZ,PMHas
	LD	HL,MsgEmpty
	JP	PrStr
PMHas:
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	PrStr

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

PrZ:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	PrChar
	INC	HL
	JR	PrZ

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

; ----------------------------------------------------------------------------
; Textos y variables

MsgUsage:	DB	"Uso: SDF              estado",13,10
		DB	"     SDF DIR          imagenes",13,10
		DB	"     SDF MOUNT d: X   monta X.DSK",13,10
		DB	"     SDF UMOUNT d:    desmonta",13,10
		DB	"     SDF NEW X [360]  crea X.DSK",13,10
		DB	"     SDF DRIVES n     unidades (1-2)",13,10,'$'
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgOldFw:	DB	"El firmware no tiene SDFINFO.",13,10,'$'
MsgOneDrive:	DB	"El DOS ve una sola unidad.",13,10,'$'
MsgNoImages:	DB	"No hay imagenes .DSK en la SD.",13,10,'$'
MsgColon:	DB	": $"
MsgEmpty:	DB	"sin imagen",13,10,'$'
MsgDrives1:	DB	"Unidades del DOS al arrancar: $"
MsgCreating:	DB	"Creando $"
MsgDots:	DB	"...",13,10,'$'
MsgCreated:	DB	"Creada, de $"
MsgKb:		DB	" KB. Se monta con",13,10,"SDF MOUNT.",13,10,'$'
MsgFmtErr:	DB	"No se pudo formatear la imagen.",13,10,'$'
MsgErr:		DB	"Error $"
MsgDotCrLf:	DB	".",13,10,'$'
MsgCrLf:	DB	13,10,'$'
MsgIllegal:	DB	"Pedido no valido.",13,10,'$'
MsgNotFound:	DB	"No existe esa imagen.",13,10,'$'
MsgBadName:	DB	"Nombre no valido.",13,10,'$'
MsgBadSize:	DB	"No es de 720 ni de 360 KB.",13,10,'$'
MsgBadDrive:	DB	"Unidad no valida (A: o B:).",13,10,'$'
MsgExists:	DB	"Ya existe.",13,10,'$'
MsgFull:	DB	"La SD esta llena.",13,10,'$'
MsgIo:		DB	"Error de la SD.",13,10,'$'
MsgNoSd:	DB	"No hay SD.",13,10,'$'

Txt720:		DB	"720",0
Txt360:		DB	"360",0

ArgPtr:		DW	0
ArgLen:		DB	0
NameLen:	DB	0
Drive:		DB	0
Cols:		DB	0
Info:		DB	0
Sec:		DB	0

; Los buferes, despues del programa
Arg		EQU	$
Name		EQU	Arg+ARGMAX+1
InfoA		EQU	Name+ARGMAX+1
InfoB		EQU	InfoA+ARGMAX+1
Text		EQU	InfoB+ARGMAX+1
SecBuf		EQU	Text+41
List		EQU	SecBuf+512
