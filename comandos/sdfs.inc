; Archivos de la SD del SDF-Pico, para programas de MSX-DOS (docs/ARCHIVOS.md)
;
; La API de archivos del firmware (CMD_FSCD ... CMD_FSFREE), una rutina por
; comando. La usan SD.COM y la puede usar un explorador: el directorio actual,
; el ultimo listado y el archivo abierto quedan en la Pico entre llamadas.
;
; Las rutas son ASCIIZ de hasta 127 letras, con \ o /, relativas al directorio
; actual o absolutas, con nombres largos o cortos. Las rutinas devuelven en A
; el error (0 = bien, los codigos FSE_...) con el flag Z puesto si es 0.
; Cortan las interrupciones mientras hablan con la Pico y las vuelven a
; habilitar al salir. Cambian AF, BC, DE y HL.
;
; El programa define FsNoPico: si la Pico no contesta, se salta ahi con las
; interrupciones habilitadas y la pila como haya quedado.
;
;	INCLUDE	"sdfs.inc"

SDFDATA		EQU	0x00
SDFCMD		EQU	0x01

CMD_FSCD	EQU	0xC0
CMD_FSPWD	EQU	0xC1
CMD_FSDIR	EQU	0xC2
CMD_FSLIST	EQU	0xC3
CMD_FSOPEN	EQU	0xC4
CMD_FSREAD	EQU	0xC5
CMD_FSWRITE	EQU	0xC6
CMD_FSCLOSE	EQU	0xC7
CMD_FSDEL	EQU	0xC8
CMD_FSMKDIR	EQU	0xC9
CMD_FSREN	EQU	0xCA
CMD_FSSTAT	EQU	0xCB
CMD_FSFREE	EQU	0xCC

FS_RUTA_MAX	EQU	128		; una ruta, con el 0
FS_ENTRADA	EQU	64		; bytes por entrada de FsList y FsStat
FS_LISTA_MAX	EQU	8		; entradas por FsList
FS_BLOQUE	EQU	512		; lo mas que mueven FsRead y FsWrite

; Opciones de FsDir
FS_OCULTOS	EQU	0x01		; tambien ocultos y de sistema
FS_PADRE	EQU	0x02		; una entrada ".." primero, fuera de la raiz

; Modos de FsOpen
FS_LEER		EQU	0
FS_CREAR	EQU	1		; nuevo: error si ya existe
FS_REEMPLAZAR	EQU	2		; nuevo, o vacia el que haya

; Una entrada (FS_ENTRADA bytes)
FSE_ATRIB	EQU	0		; atributos de FAT
FSE_LARGO	EQU	1		; 4 bytes, L primero
FSE_FECHA	EQU	5		; de FAT, como MSX-DOS
FSE_HORA	EQU	7
FSE_CORTO	EQU	9		; nombre 8.3, ASCIIZ
FSE_NOMBRE	EQU	22		; el largo (o el corto), ASCIIZ, hasta 41

FSA_SOLOLECT	EQU	0x01		; bits de los atributos
FSA_OCULTO	EQU	0x02
FSA_SISTEMA	EQU	0x04
FSA_DIR		EQU	0x10

; Errores (los del BASIC de disco, y 80-82 propios)
FSE_ILEGAL	EQU	5
FSE_NOEXISTE	EQU	53
FSE_ENUSO	EQU	54		; imagen montada, el archivo abierto o el dir actual
FSE_NOMBRE_MAL	EQU	56
FSE_NOABIERTO	EQU	59
FSE_YAEXISTE	EQU	65
FSE_LLENA	EQU	66
FSE_SOLOLECT	EQU	68
FSE_ES		EQU	69		; error de la SD
FSE_SINSD	EQU	70
FSE_SINDIR	EQU	80		; no existe el directorio
FSE_NOVACIO	EQU	81
FSE_ESDIR	EQU	82

; ----------------------------------------------------------------------------

; FsCd, HL = ruta: cambia el directorio actual
FsCd:
	LD	A,CMD_FSCD
	JR	FsPathCmd

; FsDel, HL = ruta: borra un archivo o un directorio vacio
FsDel:
	LD	A,CMD_FSDEL
	JR	FsPathCmd

; FsMkdir, HL = ruta
FsMkdir:
	LD	A,CMD_FSMKDIR
FsPathCmd:
	DI
	CALL	FsCmd
	CALL	FsSendZ
FsResult:
	CALL	FsByte
	EI
	OR	A
	RET

; FsRen, HL = ruta, DE = nombre nuevo (sin directorio: en el mismo; con
; directorio: lo mueve ahi)
FsRen:
	DI
	LD	A,CMD_FSREN
	CALL	FsCmd
	PUSH	DE
	CALL	FsSendZ
	POP	HL
	CALL	FsSendZ
	JP	FsResult

; FsPwd, DE = destino (FS_RUTA_MAX): la ruta actual, "\" en la raiz
FsPwd:
	DI
	LD	A,CMD_FSPWD
	CALL	FsCmd
	LD	B,FS_RUTA_MAX-1
FsPwdLoop:
	CALL	FsByte
	LD	(DE),A
	OR	A
	JR	Z,FsPwdEnd
	INC	DE
	DJNZ	FsPwdLoop
	XOR	A
	LD	(DE),A
FsPwdEnd:
	EI
	RET

; FsDir, HL = ruta o patron ("" el actual, "*.ROM", "JUEGOS\*.DSK"),
; C = opciones: lee el directorio en la Pico, ordenado (los directorios
; primero). out: HL = cantidad de entradas, C = 1 si se corto en 512
FsDir:
	DI
	LD	A,CMD_FSDIR
	CALL	FsCmd
	LD	A,C
	OUT	(SDFDATA),A
	CALL	FsSendZ
	CALL	FsByte
	LD	E,A
	CALL	FsByte
	LD	L,A
	CALL	FsByte
	LD	H,A
	CALL	FsByte
	LD	C,A
	EI
	LD	A,E
	OR	A
	RET

; FsList, HL = desde, B = cuantas (hasta FS_LISTA_MAX), DE = destino:
; entradas del ultimo FsDir. out: A = cuantas copio (0 al final)
FsList:
	DI
	LD	A,CMD_FSLIST
	CALL	FsCmd
	LD	A,L
	OUT	(SDFDATA),A
	LD	A,H
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	CALL	FsByte
	OR	A
	JR	Z,FsListEnd
	PUSH	AF
	EX	DE,HL
	LD	C,SDFDATA
	LD	D,A
FsListLoop:
	LD	B,FS_ENTRADA
	INIR
	DEC	D
	JR	NZ,FsListLoop
	POP	AF
FsListEnd:
	EI
	OR	A
	RET

; FsStat, HL = ruta, DE = destino (FS_ENTRADA bytes): la entrada de un
; archivo o directorio
FsStat:
	DI
	LD	A,CMD_FSSTAT
	CALL	FsCmd
	CALL	FsSendZ
	CALL	FsByte
	PUSH	AF
	EX	DE,HL
	LD	B,FS_ENTRADA
	LD	C,SDFDATA
	INIR
	POP	AF
	EI
	OR	A
	RET

; FsOpen, HL = ruta, C = modo: abre un archivo (cierra el que hubiera).
; El largo queda en FsSize.
FsOpen:
	DI
	LD	A,CMD_FSOPEN
	CALL	FsCmd
	LD	A,C
	OUT	(SDFDATA),A
	CALL	FsSendZ
	LD	DE,FsSize
FsLong:
	CALL	FsByte			; el error, y 4 bytes en DE
	PUSH	AF
	EX	DE,HL
	LD	B,4
	LD	C,SDFDATA
	INIR
	POP	AF
	EI
	OR	A
	RET

; FsRead, DE = destino, BC = cuantos (hasta FS_BLOQUE). out: BC = los que
; leyo (0 al final del archivo)
FsRead:
	DI
	LD	A,CMD_FSREAD
	CALL	FsCmd
	LD	A,C
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	CALL	FsByte
	PUSH	AF
	CALL	FsByte
	LD	C,A
	CALL	FsByte
	LD	B,A
	PUSH	BC
	EX	DE,HL
	CALL	FsInBlock
	POP	BC
	POP	AF
	EI
	OR	A
	RET

; FsWrite, HL = datos, BC = cuantos (hasta FS_BLOQUE)
FsWrite:
	DI
	LD	A,CMD_FSWRITE
	CALL	FsCmd
	LD	A,C
	OUT	(SDFDATA),A
	LD	A,B
	OUT	(SDFDATA),A
	CALL	FsOutBlock
	JP	FsResult

; FsClose: cierra el archivo abierto (si se grabo, recien ahi queda completo)
FsClose:
	DI
	LD	A,CMD_FSCLOSE
	CALL	FsCmd
	JP	FsResult

; FsFree: KB libres en FsFreeKb y en total en FsTotalKb (4 bytes cada uno)
FsFree:
	DI
	LD	A,CMD_FSFREE
	CALL	FsCmd
	LD	DE,FsFreeKb		; FsTotalKb sigue
	CALL	FsByte
	PUSH	AF
	EX	DE,HL
	LD	B,8
	LD	C,SDFDATA
	INIR
	POP	AF
	EI
	OR	A
	RET

; ----------------------------------------------------------------------------
; Con la Pico

; FsCmd, A = comando, y el tiempo para que la Pico levante OCUPADO
FsCmd:
	OUT	(SDFCMD),A
	PUSH	BC
	LD	B,25
FsCmdDelay:
	DJNZ	FsCmdDelay
	POP	BC
	RET

; FsSendZ, HL = ASCIIZ: el largo (hasta 127) y el texto
FsSendZ:
	PUSH	HL
	LD	B,0
FsSZLen:
	LD	A,(HL)
	OR	A
	JR	Z,FsSZGo
	INC	HL
	INC	B
	BIT	7,B
	JR	Z,FsSZLen
	DEC	B
FsSZGo:
	POP	HL
	LD	A,B
	OUT	(SDFDATA),A
	OR	A
	RET	Z
	LD	C,SDFDATA
	OTIR
	RET

; FsInBlock, HL = destino, BC = cuantos: INIR de a 256
FsInBlock:
	LD	A,B
	OR	C
	RET	Z
	LD	A,C
	LD	C,SDFDATA
FsIBLoop:
	INC	B
	DEC	B
	JR	Z,FsIBTail
	PUSH	AF
	LD	A,B
	LD	B,0
	INIR
	LD	B,A
	DEC	B
	POP	AF
	JR	FsIBLoop
FsIBTail:
	OR	A
	RET	Z
	LD	B,A
	INIR
	RET

; FsOutBlock, HL = datos, BC = cuantos: OTIR de a 256
FsOutBlock:
	LD	A,B
	OR	C
	RET	Z
	LD	A,C
	LD	C,SDFDATA
FsOBLoop:
	INC	B
	DEC	B
	JR	Z,FsOBTail
	PUSH	AF
	LD	A,B
	LD	B,0
	OTIR
	LD	B,A
	DEC	B
	POP	AF
	JR	FsOBLoop
FsOBTail:
	OR	A
	RET	Z
	LD	B,A
	OTIR
	RET

; FsWait, OCUPADO en 0 (hasta unos 3 s)
FsWait:
	PUSH	BC
	PUSH	DE
	LD	B,4
FsWOuter:
	LD	DE,0
FsWLoop:
	IN	A,(SDFCMD)
	CP	0xFF
	JR	Z,FsWGone
	AND	0x80
	JR	Z,FsWOk
	DEC	DE
	LD	A,D
	OR	E
	JR	NZ,FsWLoop
	DJNZ	FsWOuter
FsWGone:
	EI
	JP	FsNoPico
FsWOk:
	POP	DE
	POP	BC
	RET

FsByte:
	CALL	FsWait
	IN	A,(SDFDATA)
	RET

FsSize:		DW	0,0
FsFreeKb:	DW	0,0
FsTotalKb:	DW	0,0
