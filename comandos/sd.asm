; SD.COM para MSX-DOS 1 con el SDF-Pico (docs/ARCHIVOS.md)
;
;   SD                      consola: "SD \JUEGOS>", EXIT para salir
;   SD CD [ruta]            cambia el directorio de la SD (sin ruta, lo muestra)
;   SD DIR [ruta] [/A]      lista (/A: tambien ocultos y de sistema)
;   SD GET ruta [d:archivo] copia de la SD al disco del MSX
;   SD PUT d:archivo [ruta] copia del disco del MSX a la SD
;   SD DEL ruta             borra un archivo, o un directorio vacio (tambien RD)
;   SD MD ruta              crea un directorio
;   SD REN ruta nombre      renombra, o mueve si el nombre trae directorio
;
; Navega la tarjeta SD como un FTP, con la API de archivos del firmware
; (sdfs.inc). El directorio actual queda en la Pico: un SD CD vale para el
; SD DIR siguiente. Las rutas con espacios van entre comillas.
;
; Se ensambla con PASMO:  pasmo sd.asm sd.com

BDOS		EQU	0x0005
RDSLT		EQU	0x000C
EXPTBL		EQU	0xFCC1
JIFFY		EQU	0xFC9E
LINLEN		EQU	0xF3B0
CRTCNT		EQU	0xF3B1

ARGMAX		EQU	127
BUFSZ		EQU	8192		; se graba y se lee de a esto (multiplo de 512)

	ORG	0x0100

Start:
	IN	A,(SDFCMD)		; estado: FFh es el bus al aire
	CP	0xFF
	LD	HL,MsgNoPico
	JP	Z,Fail
	CALL	GetHz
	; Un firmware sin la API contesta FFh: la ruta actual empieza con \
	LD	DE,Pwd
	CALL	FsPwd
	LD	A,(Pwd)
	CP	'\'
	LD	HL,MsgOldFw
	JP	NZ,Fail

	; La linea de comandos (0080h: largo, 0081h: texto), terminada en 0
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0
	LD	HL,0x0081
	CALL	SkipSp
	LD	A,(HL)
	OR	A
	JR	Z,Shell
	LD	(ArgPtr),HL
	CALL	RunLine
	JP	Exit

; La consola: un comando por linea hasta EXIT
Shell:
	LD	A,1
	LD	(Interactive),A
	LD	HL,MsgBanner
	CALL	PrStr
	LD	(SavedSp),SP
Prompt:
	LD	SP,(SavedSp)		; Fail vuelve aca desde cualquier lado
	LD	HL,MsgPrompt		; "SD "
	CALL	PrStr
	LD	DE,Pwd
	CALL	FsPwd
	LD	HL,Pwd
	CALL	PrZ
	LD	A,'>'
	CALL	PrChar
	LD	A,' '
	CALL	PrChar
	LD	A,ARGMAX
	LD	(LineBuf),A
	LD	DE,LineBuf
	LD	C,0x0A			; leer una linea
	CALL	BDOS
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,LineBuf+1
	LD	C,(HL)
	LD	B,0
	INC	HL
	PUSH	HL
	ADD	HL,BC
	LD	(HL),0
	POP	HL
	LD	(ArgPtr),HL
	CALL	RunLine
	JR	Prompt

; RunLine: el comando de la linea en ArgPtr
RunLine:
	LD	DE,Cmd
	LD	B,8
	CALL	NextArg
	RET	Z			; linea vacia
	LD	HL,Cmd
RLUpper:
	LD	A,(HL)
	OR	A
	JR	Z,RLFind
	CALL	Upper
	LD	(HL),A
	INC	HL
	JR	RLUpper
RLFind:
	LD	HL,Commands
DFirst:
	LD	A,(HL)
	OR	A
	JR	Z,DNone
	LD	DE,Cmd
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
	JR	DFirst
DNone:
	LD	HL,MsgUnknown
	JP	Fail
DFound:
	POP	DE
	INC	HL
	LD	A,(HL)
	INC	HL
	LD	H,(HL)
	LD	L,A
	JP	(HL)

Commands:
	DB	"CD",0
	DW	Cd
	DB	"PWD",0
	DW	PrintPwd
	DB	"DIR",0
	DW	Dir
	DB	"GET",0
	DW	Get
	DB	"PUT",0
	DW	Put
	DB	"DEL",0
	DW	Del
	DB	"RD",0
	DW	Del
	DB	"MD",0
	DW	Md
	DB	"REN",0
	DW	Ren
	DB	"EXIT",0
	DW	Exit
	DB	"HELP",0
	DW	Help
	DB	"?",0
	DW	Help
	DB	"/?",0
	DW	Help
	DB	0

Help:
	LD	HL,MsgHelp
	JP	PrStr

; Fail, HL = mensaje: en la consola vuelve al prompt, si no al DOS
Fail:
	CALL	PrStr
	LD	A,(Interactive)
	OR	A
	JP	NZ,Prompt
Exit:
	EI
	JP	0			; vuelve al DOS

; La Pico dejo de contestar (sdfs.inc)
FsNoPico:
	CALL	CleanDos
	LD	HL,MsgNoPico
	CALL	PrStr
	JR	Exit

; ----------------------------------------------------------------------------
; Directorios

Cd:
	CALL	PathArg
	JR	Z,PrintPwd
	LD	HL,Arg1
	CALL	FsCd
	CALL	CheckErr
	LD	A,(Interactive)		; la consola lo dice en el prompt
	OR	A
	RET	NZ
PrintPwd:
	LD	DE,Pwd
	CALL	FsPwd
	LD	HL,Pwd
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	PrStr

Md:
	CALL	NeedPath
	CALL	FsMkdir
	JP	CheckErr

Del:
	CALL	NeedPath
	CALL	FsDel
	JP	CheckErr

Ren:
	CALL	NeedPath
	LD	DE,Arg2
	LD	B,ARGMAX
	CALL	NextArg
	LD	HL,MsgNoArg
	JP	Z,Fail
	LD	HL,Arg1
	LD	DE,Arg2
	CALL	FsRen
	JP	CheckErr

; DIR [ruta] [/A]
Dir:
	XOR	A
	LD	(Arg1),A
	LD	(DirOpts),A
DirArgs:
	LD	DE,Arg2
	LD	B,ARGMAX
	CALL	NextArg
	JR	Z,DirGo
	LD	A,(Arg2)
	CP	'/'
	JR	NZ,DirPath
	LD	A,(Arg2+1)
	CALL	Upper
	CP	'A'
	LD	HL,MsgBadOpt
	JP	NZ,Fail
	LD	A,FS_OCULTOS
	LD	(DirOpts),A
	JR	DirArgs
DirPath:
	LD	HL,Arg2
	LD	DE,Arg1
	LD	BC,ARGMAX+1
	LDIR
	JR	DirArgs
DirGo:
	LD	HL,Arg1
	LD	A,(DirOpts)
	LD	C,A
	CALL	FsDir
	CALL	CheckErr
	LD	A,C
	LD	(DirCut),A
	LD	HL,0
	LD	(Index),HL
	LD	(NFiles),HL
	LD	(NDirs),HL
	LD	(Total),HL
	LD	(Total+2),HL
	XOR	A
	LD	(Lines),A
DirBatch:
	LD	HL,(Index)
	LD	B,FS_LISTA_MAX
	LD	DE,ListBuf
	CALL	FsList
	JR	Z,DirDone
	LD	B,A
	LD	HL,ListBuf
DirEntry:
	PUSH	BC
	PUSH	HL
	CALL	PrEntry
	POP	HL
	LD	DE,FS_ENTRADA
	ADD	HL,DE
	PUSH	HL
	LD	HL,(Index)
	INC	HL
	LD	(Index),HL
	POP	HL
	POP	BC
	DJNZ	DirEntry
	JR	DirBatch
DirDone:
	; "3 archivos, 2 directorios", "45678 bytes, 1234 KB libres"
	LD	HL,(NFiles)
	LD	DE,0
	CALL	PrDec32
	LD	HL,MsgFiles
	CALL	PrStr
	LD	HL,(NDirs)
	LD	DE,0
	CALL	PrDec32
	LD	HL,MsgDirs
	CALL	PrStr
	LD	HL,(Total)
	LD	DE,(Total+2)
	CALL	PrDec32
	LD	HL,MsgBytes
	CALL	PrStr
	CALL	FsFree
	JR	NZ,DirNoFree
	LD	HL,MsgComma
	CALL	PrStr
	LD	HL,(FsFreeKb)
	LD	DE,(FsFreeKb+2)
	CALL	PrDec32
	LD	HL,MsgKbFree
	CALL	PrStr
DirNoFree:
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	A,(DirCut)
	OR	A
	RET	Z
	LD	HL,MsgCut
	JP	PrStr

; PrEntry, HL = entrada: "NOMBRE.EXT   12345 19-10-26 Nombre largo"
PrEntry:
	LD	(EntPtr),HL
	LD	DE,FSE_CORTO
	ADD	HL,DE
	LD	B,12
	CALL	PrPad
	LD	HL,(EntPtr)
	LD	A,(HL)			; atributos
	AND	FSA_DIR
	JR	Z,PEFile
	LD	HL,MsgDirTag		; "    <DIR>"
	CALL	PrStr
	LD	HL,(NDirs)
	INC	HL
	LD	(NDirs),HL
	JR	PEDate
PEFile:
	LD	HL,(NFiles)
	INC	HL
	LD	(NFiles),HL
	LD	HL,(EntPtr)
	INC	HL
	LD	E,(HL)			; el largo en DEHL
	INC	HL
	LD	D,(HL)
	INC	HL
	LD	C,(HL)
	INC	HL
	LD	B,(HL)
	EX	DE,HL
	LD	D,B
	LD	E,C
	PUSH	DE
	PUSH	HL
	LD	BC,(Total)		; Total += largo
	ADD	HL,BC
	LD	(Total),HL
	EX	DE,HL
	LD	BC,(Total+2)
	ADC	HL,BC
	LD	(Total+2),HL
	LD	A,' '
	CALL	PrChar
	POP	HL
	POP	DE
	LD	A,8
	CALL	PrDec32W
PEDate:
	LD	C,21			; columnas hasta aca
	LD	A,(LINLEN)
	CP	31
	JR	C,PEEnd
	LD	A,' '
	CALL	PrChar
	CALL	PrDate
	LD	C,30
	; El nombre largo, si es otro y entra
	LD	HL,(EntPtr)
	LD	DE,FSE_CORTO
	ADD	HL,DE
	PUSH	HL
	LD	DE,FSE_NOMBRE-FSE_CORTO
	ADD	HL,DE
	POP	DE
	PUSH	HL
	CALL	StrEq
	POP	HL
	JR	Z,PEEnd
	LD	A,(LINLEN)		; lugar: LINLEN - 1 - 30 - 1
	SUB	32
	JR	C,PEEnd
	CP	3
	JR	C,PEEnd
	LD	B,A
	LD	A,' '
	CALL	PrChar
PELong:
	LD	A,(HL)
	OR	A
	JR	Z,PEEnd
	CALL	PrChar
	INC	HL
	DJNZ	PELong
PEEnd:
	LD	HL,MsgCrLf
	CALL	PrStr
	; ESC corta; cada pantalla, una pausa
	CALL	CheckEsc
	LD	A,(CRTCNT)
	SUB	2
	LD	B,A
	LD	A,(Lines)
	INC	A
	LD	(Lines),A
	CP	B
	RET	C
	XOR	A
	LD	(Lines),A
	LD	HL,MsgMore
	CALL	PrStr
	LD	C,0x08			; una tecla
	CALL	BDOS
	CP	27
	LD	HL,MsgClrMore
	CALL	PrStr
	RET	NZ
	LD	HL,MsgCancel
	JP	Fail

; PrDate, la fecha de la entrada: dd-mm-aa
PrDate:
	LD	HL,(EntPtr)
	LD	DE,FSE_FECHA
	ADD	HL,DE
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	LD	A,E
	AND	0x1F
	CALL	Pr2
	LD	A,'-'
	CALL	PrChar
	LD	A,E			; mes: bits 5-8
	SRL	D
	RRA
	SRL	A
	SRL	A
	SRL	A
	SRL	A
	AND	0x0F
	CALL	Pr2
	LD	A,'-'
	CALL	PrChar
	LD	A,D			; anio - 1980
	ADD	A,80
PDYear:
	CP	100
	JR	C,Pr2
	SUB	100
	JR	PDYear

; Pr2, A = 0-99 con dos cifras
Pr2:
	LD	B,'0'-1
Pr2Tens:
	INC	B
	SUB	10
	JR	NC,Pr2Tens
	ADD	A,10+'0'
	PUSH	AF
	LD	A,B
	CALL	PrChar
	POP	AF
	JP	PrChar

; PrPad, HL = ASCIIZ: B letras, completando con espacios
PrPad:
	LD	A,(HL)
	OR	A
	JR	Z,PPSpace
	CALL	PrChar
	INC	HL
	DJNZ	PrPad
	RET
PPSpace:
	LD	A,' '
	CALL	PrChar
	DJNZ	PPSpace
	RET

; ----------------------------------------------------------------------------
; GET ruta [d:archivo]: de la SD al disco

Get:
	CALL	NeedPath
	LD	DE,Arg2
	LD	B,ARGMAX
	CALL	NextArg
	LD	HL,Arg1
	LD	DE,Entry
	CALL	FsStat
	CALL	CheckErr
	LD	A,(Entry+FSE_ATRIB)
	AND	FSA_DIR
	LD	A,FSE_ESDIR
	CALL	NZ,CheckErr
	LD	HL,(Entry+FSE_LARGO)
	LD	(CLen),HL
	LD	HL,(Entry+FSE_LARGO+2)
	LD	(CLen+2),HL
	; El archivo del disco: el que se pidio, o el nombre corto de la SD
	LD	HL,Entry+FSE_CORTO
	LD	(DefPtr),HL
	LD	HL,Arg2
	CALL	MakeFcb
	CALL	AskReplace
	CALL	CheckSpace
	LD	HL,Arg1
	LD	C,FS_LEER
	CALL	FsOpen
	CALL	CheckErr
	CALL	CreateFile
	CALL	StartCopy
GetLoop:
	CALL	CheckEsc
	LD	HL,0
	LD	(BufFill),HL
GetFill:
	LD	HL,Buf
	LD	DE,(BufFill)
	ADD	HL,DE
	EX	DE,HL
	LD	BC,FS_BLOQUE
	CALL	FsRead
	JR	NZ,GetErr
	LD	A,B
	OR	C
	JR	Z,GetEof
	LD	HL,(BufFill)
	ADD	HL,BC
	LD	(BufFill),HL
	CALL	AddTotal
	LD	HL,(BufFill)
	LD	DE,BUFSZ
	OR	A
	SBC	HL,DE
	JR	C,GetFill
	CALL	Flush
	JR	GetLoop
GetEof:
	CALL	Flush
	LD	DE,Fcb
	LD	C,0x10			; cerrar
	CALL	BDOS
	XOR	A
	LD	(FileOpen),A
	CALL	FsClose
	CALL	Progress		; por si fue de menos de un bufer
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,MsgSaved		; "Copiado en "
	CALL	PrStr
	CALL	PrFcbName
	JP	PrElapsed
GetErr:
	CALL	ErrMsg
	JP	Abort

; ----------------------------------------------------------------------------
; PUT d:archivo [ruta]: del disco a la SD

Put:
	CALL	NeedPath
	LD	DE,Arg2
	LD	B,ARGMAX
	CALL	NextArg
	LD	HL,0
	LD	(DefPtr),HL
	LD	HL,Arg1
	CALL	MakeFcb
	LD	HL,MsgBadName
	JP	NZ,Fail
	LD	DE,Fcb
	LD	C,0x0F			; abrir
	CALL	BDOS
	OR	A
	LD	HL,MsgNoDosFile
	JP	NZ,Fail
	LD	HL,(Fcb+16)
	LD	(CLen),HL
	LD	HL,(Fcb+18)
	LD	(CLen+2),HL
	LD	HL,1			; registros de un byte, desde el 0
	LD	(Fcb+14),HL
	DEC	HL
	LD	(Fcb+33),HL
	LD	(Fcb+35),HL
	; El destino: el nombre del archivo en el actual, o en la ruta pedida
	LD	DE,Dest
	CALL	FcbText
	LD	A,(Arg2)
	OR	A
	JR	Z,PutDest
	LD	HL,Arg2
	LD	DE,Entry
	CALL	FsStat
	JR	Z,PutExists
	CP	FSE_NOEXISTE
	CALL	NZ,CheckErr
	JR	PutArg2			; no existe: es el nombre nuevo
PutExists:
	LD	A,(Entry+FSE_ATRIB)
	AND	FSA_DIR
	JR	Z,PutArg2		; un archivo: se reemplaza
	; Un directorio: Arg2 \ nombre
	LD	HL,Dest
	LD	DE,Name
	LD	BC,13
	LDIR
	LD	HL,Arg2
	LD	DE,Dest
	CALL	CopyZ
	DEC	DE
	LD	A,(DE)
	INC	DE
	CP	'\'
	JR	Z,PutSep
	CP	'/'
	JR	Z,PutSep
	LD	A,'\'
	LD	(DE),A
	INC	DE
PutSep:
	LD	HL,Name
	CALL	CopyZ
	XOR	A
	LD	(DE),A
	JR	PutDest
PutArg2:
	LD	HL,Arg2
	LD	DE,Dest
	LD	BC,ARGMAX+1
	LDIR
PutDest:
	; Si ya esta en la SD, preguntar
	LD	A,FS_CREAR
	LD	(OpenMode),A
	LD	HL,Dest
	LD	DE,Entry
	CALL	FsStat
	JR	NZ,PutNew
	LD	A,(Entry+FSE_ATRIB)
	AND	FSA_DIR
	LD	A,FSE_ESDIR
	CALL	NZ,CheckErr
	LD	HL,MsgExists1		; "Ya existe "
	CALL	PrStr
	LD	HL,Dest
	CALL	PrZ
	CALL	Confirm
	JP	NZ,Exit2
	LD	A,FS_REEMPLAZAR
	LD	(OpenMode),A
	JR	PutOpen
PutNew:
	CP	FSE_NOEXISTE
	CALL	NZ,CheckErr
PutOpen:
	LD	A,(OpenMode)
	LD	C,A
	LD	HL,Dest
	CALL	FsOpen
	CALL	CheckErr
	LD	A,1
	LD	(SdOpen),A
	CALL	StartCopy
PutLoop:
	CALL	CheckEsc
	LD	DE,Buf
	LD	C,0x1A			; direccion de transferencia
	CALL	BDOS
	LD	DE,Fcb
	LD	HL,BUFSZ
	LD	C,0x27			; lectura al azar en bloque
	CALL	BDOS
	LD	A,H
	OR	L
	JR	Z,PutEnd
	LD	(BLeft),HL
	LD	B,H
	LD	C,L
	CALL	AddTotal
	LD	HL,Buf
	LD	(SendPtr),HL
PutSend:
	LD	HL,(BLeft)
	LD	A,H
	OR	L
	JR	Z,PutShown
	LD	BC,FS_BLOQUE		; BC = lo que sale ahora
	OR	A
	SBC	HL,BC
	JR	NC,PutChunk
	ADD	HL,BC
	LD	B,H
	LD	C,L
	LD	HL,0
PutChunk:
	LD	(BLeft),HL
	LD	HL,(SendPtr)
	PUSH	HL
	ADD	HL,BC
	LD	(SendPtr),HL
	POP	HL
	CALL	FsWrite
	JR	Z,PutSend
	CALL	ErrMsg
	JP	Abort
PutShown:
	CALL	Progress
	JR	PutLoop
PutEnd:
	CALL	FsClose			; recien aca queda completo en la SD
	JR	Z,PutClosed
	CALL	ErrMsg
	JP	Abort
PutClosed:
	XOR	A
	LD	(SdOpen),A
	CALL	Progress
	LD	HL,MsgCrLf
	CALL	PrStr
	LD	HL,MsgSaved		; "Copiado en "
	CALL	PrStr
	LD	HL,Dest
	CALL	PrZ
	JP	PrElapsed

; Abort, HL = mensaje: borra lo que haya quedado a medias de un lado y del
; otro, y Fail
Abort:
	PUSH	HL
	LD	HL,MsgCrLf
	CALL	PrStr
	POP	HL
	CALL	PrStr
	CALL	CleanDos
	CALL	FsClose			; el de GET, o el de PUT a medias
	LD	A,(SdOpen)
	OR	A
	JR	Z,AbortEnd
	XOR	A
	LD	(SdOpen),A
	LD	HL,Dest
	CALL	FsDel
	LD	HL,MsgDeletedSd
	CALL	PrStr
AbortEnd:
	LD	HL,MsgNone
	JP	Fail

; CleanDos: si GET dejo un archivo a medias en el disco, lo borra
CleanDos:
	LD	A,(FileOpen)
	OR	A
	RET	Z
	XOR	A
	LD	(FileOpen),A
	LD	DE,Fcb
	LD	C,0x10			; cerrar
	CALL	BDOS
	LD	DE,Fcb
	LD	C,0x13			; borrar
	CALL	BDOS
	LD	HL,MsgDeleted
	JP	PrStr

; Exit2: "no" a reemplazar
Exit2:
	LD	HL,MsgNone
	JP	Fail

; ----------------------------------------------------------------------------
; El archivo del disco

; MakeFcb, HL = "[d:]nombre": el FCB. Sin nombre usa el de DefPtr (si es 0,
; NZ). out: Z si hay nombre
MakeFcb:
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
	LD	HL,(DefPtr)
	LD	A,H
	OR	L
	JR	NZ,MFName
	INC	A			; NZ: no hay nombre
	RET
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
	JR	Z,MFDone
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
	JR	Z,MFDone
	CALL	FcbChar
	LD	A,C
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	MFExtLoop
MFDone:
	LD	A,(Fcb+1)
	CP	' '
	JR	Z,MFNoName
	XOR	A
	RET
MFNoName:
	OR	A			; NZ
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

; ClearFcb, de la extension (12) al registro al azar (36)
ClearFcb:
	LD	HL,Fcb+12
	LD	B,36-12+1
CFLoop:
	LD	(HL),0
	INC	HL
	DJNZ	CFLoop
	RET

; FcbText, DE = destino: "NOMBRE.EXT" del FCB, ASCIIZ
FcbText:
	LD	HL,Fcb+1
	LD	B,8
	CALL	FTCopy
	LD	A,(Fcb+9)
	CP	' '
	JR	Z,FTEnd
	LD	A,'.'
	LD	(DE),A
	INC	DE
	LD	HL,Fcb+9
	LD	B,3
	CALL	FTCopy
FTEnd:
	XOR	A
	LD	(DE),A
	RET
FTCopy:
	LD	A,(HL)
	CP	' '
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	FTCopy
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
	LD	DE,Name
	CALL	FcbText
	LD	HL,Name
	JP	PrZ

; AskReplace: si el archivo del disco ya existe, preguntar. Guarda lo que
; ocupa (se libera al reemplazarlo).
AskReplace:
	LD	HL,0
	LD	(OldSize),HL
	LD	(OldSize+2),HL
	LD	DE,Fcb
	LD	C,0x0F			; abrir
	CALL	BDOS
	OR	A
	RET	NZ
	LD	HL,(Fcb+16)
	LD	(OldSize),HL
	LD	HL,(Fcb+18)
	LD	(OldSize+2),HL
	LD	HL,MsgExists1		; "Ya existe "
	CALL	PrStr
	CALL	PrFcbName
	CALL	Confirm
	RET	Z
	JP	Exit2

; Confirm: ". Reemplazar (S/N)? " y Z si contesta S
Confirm:
	LD	HL,MsgExists2
	CALL	PrStr
	LD	C,0x08
	CALL	BDOS
	PUSH	AF
	CALL	PrChar
	LD	HL,MsgCrLf
	CALL	PrStr
	POP	AF
	CALL	Upper
	CP	'S'
	RET

; CheckSpace: que el archivo entre en el disco (en clusters, con lo que
; libera el que se reemplaza). Si no entra, corta antes de tocar nada.
CheckSpace:
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
	LD	HL,MsgNoSpace1		; "No entra en el disco: hacen falta "
	CALL	PrStr
	LD	HL,(CLen)
	LD	DE,(CLen+2)
	CALL	PrDec32
	LD	HL,MsgNoSpace2		; " bytes y hay "
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
	JP	Fail

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
	JR	Z,CFOk
	CALL	FsClose
	LD	HL,MsgNoCreate
	JP	Fail
CFOk:
	LD	HL,1			; registros de un byte, desde el 0
	LD	(Fcb+14),HL
	DEC	HL
	LD	(Fcb+33),HL
	LD	(Fcb+35),HL
	LD	A,1
	LD	(FileOpen),A
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
	LD	HL,MsgOf		; " de "
	CALL	PrStr
	LD	HL,(CLen)
	LD	DE,(CLen+2)
	CALL	PrDec32
	LD	HL,MsgBytes		; " bytes"
	JP	PrStr

; StartCopy: Total en 0 y el reloj en marcha
StartCopy:
	LD	HL,0
	LD	(Total),HL
	LD	(Total+2),HL
	LD	HL,(JIFFY)
	LD	(TStart),HL
	RET

; AddTotal, BC = bytes
AddTotal:
	LD	HL,(Total)
	ADD	HL,BC
	LD	(Total),HL
	RET	NC
	LD	HL,(Total+2)
	INC	HL
	LD	(Total+2),HL
	RET

; ----------------------------------------------------------------------------
; Argumentos

; NeedPath: la ruta en Arg1, o Fail. out: HL = Arg1
NeedPath:
	CALL	PathArg
	LD	HL,MsgNoArg
	JP	Z,Fail
	LD	HL,Arg1
	RET

PathArg:
	LD	DE,Arg1
	LD	B,ARGMAX
				; sigue en NextArg
; NextArg, DE = destino, B = maximo: la palabra siguiente (entre comillas
; puede tener espacios), ASCIIZ. out: A = largo, Z si no hay mas
NextArg:
	LD	HL,(ArgPtr)
	CALL	SkipSp
	LD	C,' '			; hasta donde
	LD	A,(HL)
	CP	'"'
	JR	NZ,NALoopGo
	LD	C,A
	INC	HL
NALoopGo:
	PUSH	DE
NALoop:
	LD	A,(HL)
	OR	A
	JR	Z,NAEnd
	INC	HL
	CP	C
	JR	Z,NAEnd
	LD	(DE),A
	INC	DE
	DJNZ	NALoop
	DEC	DE			; lo que sobra se pierde
	INC	B
	JR	NALoop
NAEnd:
	LD	(ArgPtr),HL
	XOR	A
	LD	(DE),A
	POP	HL
	EX	DE,HL
	OR	A
	SBC	HL,DE
	LD	A,L
	OR	A
	RET

SkipSp:
	LD	A,(HL)
	CP	' '
	RET	NZ
	INC	HL
	JR	SkipSp

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

; CopyZ, de HL a DE hasta el 0 (sin copiarlo)
CopyZ:
	LD	A,(HL)
	OR	A
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	JR	CopyZ

Upper:
	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	AND	0xDF
	RET

; ----------------------------------------------------------------------------
; Errores

; CheckErr, A = error de la API: si no es 0, el mensaje y Fail
CheckErr:
	OR	A
	RET	Z
	CALL	ErrMsg
	JP	Fail

; ErrMsg, A = error: HL = el mensaje
ErrMsg:
	LD	HL,Errors
EMLoop:
	LD	C,(HL)
	INC	HL
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	INC	HL
	INC	C			; 0FFh: fin de la tabla
	JR	Z,EMOther
	DEC	C
	CP	C
	JR	NZ,EMLoop
	EX	DE,HL
	RET
EMOther:
	LD	L,A			; "Error nnn."
	LD	H,0
	LD	DE,MsgErrN+6
	LD	B,100
	CALL	EMDigit
	LD	B,10
	CALL	EMDigit
	LD	A,L
	ADD	A,'0'
	LD	(DE),A
	LD	HL,MsgErrN
	RET
EMDigit:
	LD	A,L
	LD	C,'0'-1
EMDLoop:
	INC	C
	SUB	B
	JR	NC,EMDLoop
	ADD	A,B
	LD	L,A
	LD	A,C
	LD	(DE),A
	INC	DE
	RET

Errors:
	DB	FSE_ILEGAL
	DW	MsgIllegal
	DB	FSE_NOEXISTE
	DW	MsgNotFound
	DB	FSE_ENUSO
	DW	MsgInUse
	DB	FSE_NOMBRE_MAL
	DW	MsgBadName
	DB	FSE_NOABIERTO
	DW	MsgNotOpen
	DB	FSE_YAEXISTE
	DW	MsgExists
	DB	FSE_LLENA
	DW	MsgSdFull
	DB	FSE_SOLOLECT
	DW	MsgReadOnly
	DB	FSE_ES
	DW	MsgIo
	DB	FSE_SINSD
	DW	MsgNoSd
	DB	FSE_SINDIR
	DW	MsgNoDir
	DB	FSE_NOVACIO
	DW	MsgNotEmpty
	DB	FSE_ESDIR
	DW	MsgIsDir
	DB	0xFF
	DW	0

; ----------------------------------------------------------------------------
; Tiempo y teclado

; GetHz: 50 o 60
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
	RET

; CheckEsc: ESC o CTRL+C cortan (funcion 06h: sin el CTRL+C del DOS, que
; dejaria los archivos abiertos)
CheckEsc:
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

; PrElapsed: ", n bytes en t,t s"
PrElapsed:
	LD	HL,MsgIn		; ", en "
	CALL	PrStr
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
	CALL	PrChar
	LD	HL,MsgSec		; " s"
	JP	PrStr

; ----------------------------------------------------------------------------
; Numeros de 32 bits, en DEHL

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
; PrDec32W, DEHL = numero, A = ancho (con espacios a la izquierda)
PrDec32W:
	LD	(Width),A
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
	LD	A,(Digits)
	LD	B,A
	LD	A,(Width)
	SUB	B
	JR	C,P32Out
	JR	Z,P32Out
	LD	B,A
P32Pad:
	LD	A,' '
	CALL	PrChar
	DJNZ	P32Pad
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

	INCLUDE	"sdfs.inc"

; ----------------------------------------------------------------------------
; Textos y variables

MsgHelp:	DB	"CD [ruta]         directorio",13,10
		DB	"DIR [ruta] [/A]   lista",13,10
		DB	"GET ruta [d:arch] SD -> disco",13,10
		DB	"PUT d:arch [ruta] disco -> SD",13,10
		DB	"DEL ruta, RD ruta borra",13,10
		DB	"MD ruta           crea directorio",13,10
		DB	"REN ruta nombre   renombra",13,10
		DB	"EXIT              sale",13,10
		DB	"Con espacios: entre comillas.",13,10,'$'
MsgBanner:	DB	"SD.COM, la SD del SDF-Pico.",13,10
		DB	"HELP para los comandos.",13,10,'$'
MsgPrompt:	DB	"SD $"
MsgNoPico:	DB	"No se detecta el SDF-Pico.",13,10,'$'
MsgOldFw:	DB	"El firmware no tiene la API de",13,10
		DB	"archivos (hace falta el 0.10).",13,10,'$'
MsgUnknown:	DB	"Comando desconocido (HELP).",13,10,'$'
MsgNoArg:	DB	"Falta la ruta (HELP).",13,10,'$'
MsgBadOpt:	DB	"Opcion no valida.",13,10,'$'
MsgDirTag:	DB	"    <DIR>$"
MsgFiles:	DB	" archivos, $"
MsgDirs:	DB	" directorios",13,10,'$'
MsgComma:	DB	", $"
MsgKbFree:	DB	" KB libres$"
MsgCut:		DB	"Solo los primeros 512.",13,10,'$'
MsgMore:	DB	"-- mas --$"
MsgClrMore:	DB	13,"         ",13,'$'
MsgNoDosFile:	DB	"No existe en el disco.",13,10,'$'
MsgExists1:	DB	"Ya existe $"
MsgExists2:	DB	".",13,10,"Reemplazar (S/N)? $"
MsgNoSpace1:	DB	"No entra en el disco:",13,10,"hacen falta $"
MsgNoSpace2:	DB	" bytes",13,10,"y hay $"
MsgNoSpace3:	DB	".",13,10,'$'
MsgNoCreate:	DB	"No se pudo crear el archivo.",13,10,'$'
MsgFull:	DB	"Disco lleno.",13,10,'$'
MsgCancel:	DB	"Cancelado.",13,10,'$'
MsgDeleted:	DB	"Se borro el archivo a medias.",13,10,'$'
MsgDeletedSd:	DB	"Se borro el de la SD.",13,10,'$'
MsgBytes:	DB	" bytes$"
MsgOf:		DB	" de $"
MsgSaved:	DB	"Copiado en $"
MsgIn:		DB	", en $"
MsgSec:		DB	" s",13,10,'$'
MsgCrLf:	DB	13,10
MsgNone:	DB	'$'

MsgIllegal:	DB	"Pedido no valido.",13,10,'$'
MsgNotFound:	DB	"No existe.",13,10,'$'
MsgInUse:	DB	"Esta en uso (imagen montada,",13,10
		DB	"o el directorio actual).",13,10,'$'
MsgBadName:	DB	"Nombre no valido.",13,10,'$'
MsgNotOpen:	DB	"No hay archivo abierto.",13,10,'$'
MsgExists:	DB	"Ya existe.",13,10,'$'
MsgSdFull:	DB	"La SD esta llena.",13,10,'$'
MsgReadOnly:	DB	"Es de solo lectura.",13,10,'$'
MsgIo:		DB	"Error de la SD.",13,10,'$'
MsgNoSd:	DB	"No hay SD.",13,10,'$'
MsgNoDir:	DB	"No existe el directorio.",13,10,'$'
MsgNotEmpty:	DB	"El directorio no esta vacio.",13,10,'$'
MsgIsDir:	DB	"Es un directorio.",13,10,'$'
MsgErrN:	DB	"Error nnn.",13,10,'$'

Hz:		DB	60
Interactive:	DB	0
FileOpen:	DB	0
SdOpen:		DB	0
SavedSp:	DW	0
ArgPtr:		DW	0
DefPtr:		DW	0
EntPtr:		DW	0
SendPtr:	DW	0
BLeft:		DW	0
BufFill:	DW	0
TStart:		DW	0
Index:		DW	0
NFiles:		DW	0
NDirs:		DW	0
Total:		DW	0,0
CLen:		DW	0,0
OldSize:	DW	0,0
FreeCl:		DW	0
AvailCl:	DW	0,0
ClShift:	DB	0
DirOpts:	DB	0
OpenMode:	DB	0
DirCut:		DB	0
Lines:		DB	0
Digits:		DB	0
Width:		DB	0

; Los buferes, despues del programa: no ocupan lugar en el .COM
Fcb		EQU	$
Cmd		EQU	Fcb+37
Arg1		EQU	Cmd+9
Arg2		EQU	Arg1+ARGMAX+1
Dest		EQU	Arg2+ARGMAX+1
Name		EQU	Dest+ARGMAX+1+14	; si Dest crece con \NOMBRE.EXT
Pwd		EQU	Name+14
LineBuf		EQU	Pwd+FS_RUTA_MAX
Entry		EQU	LineBuf+ARGMAX+3
ListBuf		EQU	Entry+FS_ENTRADA
Buf		EQU	ListBuf+FS_LISTA_MAX*FS_ENTRADA
