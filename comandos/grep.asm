; GREP.COM para MSX-DOS 1 (docs/COMANDOS.md)
;
;   GREP [/I] [/N] [/V] [/C] patron archivo...
;
; Muestra las lineas de los archivos que contienen el patron. Los archivos
; pueden llevar comodines (*.TXT) y ser varios; con mas de uno, cada linea va
; con el nombre adelante.
;
;   /I  sin distinguir mayusculas      /V  las lineas que NO lo contienen
;   /N  con el numero de linea         /C  solo cuantas lineas
;
; El patron: las letras se buscan tal cual, salvo
;   .   cualquier caracter             ^   al principio (si va primero)
;   *   cero o mas del anterior        $   al final (si va ultimo)
;   \   el que sigue, tal cual (\. busca un punto)
; Con espacios, entre comillas: GREP "HOLA MUNDO" *.TXT
;
; Se ensambla con PASMO:  pasmo grep.asm grep.com

BDOS		EQU	0x0005

PATMAX		EQU	64
LINEMAX		EQU	255		; de cada linea se mira esto
NAMESMAX	EQU	112		; archivos por corrida (las entradas de un directorio)
READSZ		EQU	4096

; Cada parte del patron compilado: marcas y caracter
F_STAR		EQU	0		; bit: cero o mas veces
F_ANY		EQU	1		; bit: cualquier caracter
T_END		EQU	0x80		; fin del patron

	ORG	0x0100

Start:
	; La linea de comandos (0080h: largo, 0081h: texto), terminada en 0
	LD	HL,0x0080
	LD	C,(HL)
	LD	B,0
	INC	HL
	ADD	HL,BC
	LD	(HL),0
	LD	HL,0x0081

	; Opciones: /I /N /V /C, juntas o separadas
Options:
	CALL	SkipSp
	CP	'/'
	JR	NZ,Pattern
	INC	HL
OptChar:
	LD	A,(HL)
	CALL	Upper
	LD	DE,IgnCase
	CP	'I'
	JR	Z,OptSet
	LD	DE,Numbers
	CP	'N'
	JR	Z,OptSet
	LD	DE,Invert
	CP	'V'
	JR	Z,OptSet
	LD	DE,CountOnly
	CP	'C'
	JP	NZ,Usage
OptSet:
	LD	A,1
	LD	(DE),A
	INC	HL
	LD	A,(HL)
	CP	'/'
	JR	Z,Options
	CP	' '
	JR	Z,Options
	OR	A
	JR	NZ,OptChar
	JP	Usage			; opciones y nada mas

	; El patron, entre comillas o hasta un espacio
Pattern:
	OR	A
	JP	Z,Usage
	LD	DE,Pat
	LD	B,PATMAX
	LD	C,' '			; lo que lo termina
	CP	'"'
	JR	NZ,PatLoop
	LD	C,'"'
	INC	HL
PatLoop:
	LD	A,(HL)
	OR	A
	JR	Z,PatEnd
	INC	HL
	CP	C
	JR	Z,PatEnd
	LD	(DE),A
	INC	DE
	DJNZ	PatLoop
	LD	HL,MsgLongPat
	JP	Fail
PatEnd:
	XOR	A
	LD	(DE),A
	PUSH	HL
	CALL	Compile
	POP	HL

	; Los archivos: cada uno, con sus comodines, a la lista de nombres
	CALL	SkipSp
	OR	A
	JP	Z,Usage
	LD	DE,Search		; los resultados de la busqueda van aca
	LD	C,0x1A
	PUSH	HL
	CALL	BDOS
	POP	HL
NextSpec:
	CALL	SkipSp
	OR	A
	JR	Z,SpecsDone
	CALL	MakeFcb			; HL queda despues del nombre
	PUSH	HL
	CALL	FindFiles
	POP	HL
	JR	NextSpec
SpecsDone:
	LD	A,(NNames)
	OR	A
	JP	Z,Exit
	CP	2
	JR	C,OneFile
	LD	A,1
	LD	(Multi),A
OneFile:
	LD	HL,Names
	LD	A,(NNames)
	LD	B,A
EachFile:
	PUSH	BC
	PUSH	HL
	CALL	GrepFile
	POP	HL
	LD	DE,12
	ADD	HL,DE
	POP	BC
	DJNZ	EachFile
Exit:
	JP	0			; vuelve al DOS

Usage:
	LD	HL,MsgUsage		; lleva un '$': de a una letra
	CALL	PrZ
	JR	Exit
Fail:
	CALL	PrStr
	JR	Exit

; ----------------------------------------------------------------------------
; El patron

; Compile: Pat a Tokens (marcas y caracter por parte), y Caret y Dollar
Compile:
	LD	HL,Pat
	LD	IX,Tokens
	LD	A,(HL)
	CP	'^'
	JR	NZ,CoLoop
	LD	(Caret),A
	INC	HL
CoLoop:
	LD	A,(HL)
	OR	A
	JR	Z,CoEnd
	CP	'$'
	JR	NZ,CoChar
	INC	HL
	LD	A,(HL)
	DEC	HL
	OR	A
	LD	A,'$'
	JR	NZ,CoChar		; un $ en el medio es un $
	LD	(Dollar),A
	JR	CoEnd
CoChar:
	LD	C,0			; marcas
	CP	'.'
	JR	NZ,CoEsc
	LD	C,1 SHL F_ANY
	JR	CoGot
CoEsc:
	CP	'\'
	JR	NZ,CoGot
	INC	HL
	LD	A,(HL)
	OR	A
	JR	NZ,CoGot
	DEC	HL			; un \ al final es un \
	LD	A,'\'
CoGot:
	CALL	CaseFold
	LD	(IX+1),A
	INC	HL
	LD	A,(HL)
	CP	'*'
	JR	NZ,CoStore
	SET	F_STAR,C
	INC	HL
CoStore:
	LD	(IX+0),C
	INC	IX
	INC	IX
	JR	CoLoop
CoEnd:
	LD	(IX+0),T_END
	RET

; SearchLine: carry si la linea en LineBuf tiene el patron
SearchLine:
	LD	IX,Tokens
	LD	HL,LineBuf
	LD	A,(Caret)
	OR	A
	JR	NZ,MatchHere
SLLoop:
	PUSH	HL
	PUSH	IX
	CALL	MatchHere
	POP	IX
	POP	HL
	RET	C
	LD	A,(HL)
	OR	A
	RET	Z			; ya se probo tambien el final de la linea
	INC	HL
	JR	SLLoop

; MatchHere: carry si el patron desde IX coincide con el texto desde HL
; (el de Kernighan y Pike en "The Practice of Programming")
MatchHere:
	LD	A,(IX+0)
	CP	T_END
	JR	Z,MHEnd
	BIT	F_STAR,A
	JR	NZ,MHStar
	LD	A,(HL)
	OR	A
	RET	Z			; se acabo el texto: no
	CALL	TokEq
	JR	NZ,MHNo
	INC	HL
	INC	IX
	INC	IX
	JR	MatchHere
MHNo:
	OR	A
	RET
MHEnd:
	LD	A,(Dollar)
	OR	A
	SCF
	RET	Z			; sin $: coincide
	LD	A,(HL)
	OR	A
	SCF
	RET	Z			; con $, solo al final del texto
	OR	A
	RET
MHStar:
	; c*: el resto, desde aca y despues de cada c que siga
	PUSH	HL
	PUSH	IX
	INC	IX
	INC	IX
	CALL	MatchHere
	POP	IX
	POP	HL
	RET	C
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	TokEq
	JR	NZ,MHNo
	INC	HL
	JR	MHStar

; TokEq: Z si el caracter A coincide con la parte del patron en IX
TokEq:
	LD	B,A
	LD	A,(IX+0)
	AND	1 SHL F_ANY
	JR	Z,TEChar
	XOR	A			; cualquiera: Z
	RET
TEChar:
	LD	A,B
	CALL	CaseFold
	CP	(IX+1)
	RET

; CaseFold: con /I, a mayuscula
CaseFold:
	PUSH	AF
	LD	A,(IgnCase)
	OR	A
	JR	Z,CFKeep
	POP	AF
	JP	Upper
CFKeep:
	POP	AF
	RET

Upper:
	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	AND	0xDF
	RET

; ----------------------------------------------------------------------------
; Los archivos

SkipSp:
	LD	A,(HL)
	CP	' '
	RET	NZ
	INC	HL
	JR	SkipSp

; MakeFcb: el nombre en HL (con unidad y comodines) a Fcb; * llena de ?
MakeFcb:
	LD	(SpecPtr),HL
	LD	DE,Fcb
	XOR	A
	LD	(DE),A
	INC	HL
	LD	A,(HL)
	DEC	HL
	CP	':'
	JR	NZ,MFBlank
	LD	A,(HL)
	CALL	Upper
	SUB	'A'-1
	LD	(DE),A
	INC	HL
	INC	HL
MFBlank:
	PUSH	HL
	LD	HL,Fcb+1
	LD	B,11
MFBl:
	LD	(HL),' '
	INC	HL
	DJNZ	MFBl
	LD	B,37-12
MFZero:
	LD	(HL),0
	INC	HL
	DJNZ	MFZero
	POP	HL
	LD	DE,Fcb+1
	LD	B,8
	CALL	MFPart
	LD	A,(HL)
	CP	'.'
	JR	NZ,MFSkip
	INC	HL
	LD	DE,Fcb+9
	LD	B,3
	CALL	MFPart
MFSkip:
	LD	A,(HL)			; lo que sobre del nombre
	OR	A
	RET	Z
	CP	' '
	RET	Z
	INC	HL
	JR	MFSkip

; MFPart: hasta B letras del nombre, hasta '.', espacio o fin
MFPart:
	LD	A,(HL)
	OR	A
	RET	Z
	CP	' '
	RET	Z
	CP	'.'
	RET	Z
	INC	HL
	CP	'*'
	JR	Z,MFStar
	CALL	Upper
	LD	C,A
	LD	A,B
	OR	A
	JR	Z,MFPart		; las que sobran se saltean
	LD	A,C
	LD	(DE),A
	INC	DE
	DEC	B
	JR	MFPart
MFStar:
	LD	A,B
	OR	A
	JR	Z,MFPart
	LD	A,'?'
MFStarFill:
	LD	(DE),A
	INC	DE
	DJNZ	MFStarFill
	JR	MFPart

; FindFiles: los archivos que coinciden con Fcb, a Names (unidad y 11 letras)
FindFiles:
	LD	DE,Fcb
	LD	C,0x11			; buscar el primero
	CALL	BDOS
	OR	A
	JR	Z,FFFound
	LD	HL,MsgNotFound		; "No se encontro "
	CALL	PrStr
	LD	HL,(SpecPtr)
FFName:
	LD	A,(HL)
	OR	A
	JR	Z,FFNameEnd
	CP	' '
	JR	Z,FFNameEnd
	CALL	PrChar
	INC	HL
	JR	FFName
FFNameEnd:
	LD	HL,MsgCrLf
	JP	PrStr
FFFound:
	LD	A,(NNames)
	CP	NAMESMAX
	RET	NC
	LD	L,A			; Names + 12 * n
	LD	H,0
	ADD	HL,HL
	ADD	HL,HL
	LD	D,H
	LD	E,L
	ADD	HL,HL
	ADD	HL,DE
	LD	DE,Names
	ADD	HL,DE
	EX	DE,HL
	LD	A,(Fcb)			; la unidad del pedido
	LD	(DE),A
	INC	DE
	LD	HL,Search+1		; el nombre encontrado
	LD	BC,11
	LDIR
	LD	HL,NNames
	INC	(HL)
	LD	DE,Fcb
	LD	C,0x12			; buscar el siguiente
	CALL	BDOS
	OR	A
	JR	Z,FFFound
	RET

; GrepFile, HL = unidad y nombre (12 bytes)
GrepFile:
	LD	(CurName),HL
	LD	DE,Fcb
	LD	BC,12
	LDIR
	LD	HL,Fcb+12
	LD	B,37-12
GFZero:
	LD	(HL),0
	INC	HL
	DJNZ	GFZero
	LD	DE,Fcb
	LD	C,0x0F			; abrir
	CALL	BDOS
	OR	A
	JR	Z,GFOpen
	LD	HL,MsgNoOpen		; "No se pudo abrir "
	CALL	PrStr
	CALL	PrName
	LD	HL,MsgCrLf
	JP	PrStr
GFOpen:
	LD	HL,1			; registros de un byte, desde el 0
	LD	(Fcb+14),HL
	DEC	HL
	LD	(Fcb+33),HL
	LD	(Fcb+35),HL
	LD	(LineNo),HL
	LD	(Count),HL
	XOR	A
	LD	(LineLen),A
	LD	DE,ReadBuf
	LD	C,0x1A
	CALL	BDOS
GFRead:
	LD	DE,Fcb
	LD	HL,READSZ
	LD	C,0x27			; lectura al azar en bloque
	CALL	BDOS
	LD	A,H
	OR	L
	JR	Z,GFEof
	LD	B,H			; BC = bytes leidos
	LD	C,L
	LD	HL,ReadBuf
GFByte:
	LD	A,(HL)
	INC	HL
	CP	0x1A			; fin de un archivo de texto
	JR	Z,GFEof
	CP	10
	JR	Z,GFLine
	CP	13
	JR	Z,GFNext
	PUSH	HL
	LD	E,A
	LD	A,(LineLen)
	CP	LINEMAX
	JR	NC,GFFull		; lo que no entra se pierde
	LD	L,A
	LD	H,0
	PUSH	DE
	LD	DE,LineBuf
	ADD	HL,DE
	POP	DE
	LD	(HL),E
	INC	A
	LD	(LineLen),A
GFFull:
	POP	HL
GFNext:
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,GFByte
	JR	GFRead
GFLine:
	PUSH	BC
	PUSH	HL
	CALL	EndLine
	POP	HL
	POP	BC
	JR	GFNext
GFEof:
	LD	A,(LineLen)		; la ultima, si no termina en un salto
	OR	A
	CALL	NZ,EndLine
	LD	A,(CountOnly)
	OR	A
	RET	Z
	CALL	PrPrefix
	LD	HL,(Count)
	CALL	PrDec16
	LD	HL,MsgCrLf
	JP	PrStr

; EndLine: una linea completa en LineBuf; si corresponde, se muestra
EndLine:
	LD	A,(LineLen)
	LD	L,A
	LD	H,0
	LD	DE,LineBuf
	ADD	HL,DE
	LD	(HL),0
	XOR	A
	LD	(LineLen),A
	LD	HL,(LineNo)
	INC	HL
	LD	(LineNo),HL
	CALL	SearchLine		; carry si la tiene
	LD	A,0
	RLA
	LD	HL,Invert
	XOR	(HL)
	RET	Z
	LD	HL,(Count)
	INC	HL
	LD	(Count),HL
	LD	A,(CountOnly)
	OR	A
	RET	NZ
	CALL	PrPrefix
	LD	A,(Numbers)
	OR	A
	JR	Z,ELText
	LD	HL,(LineNo)
	CALL	PrDec16
	LD	A,':'
	CALL	PrChar
ELText:
	LD	HL,LineBuf
	CALL	PrZ
	LD	HL,MsgCrLf
	JP	PrStr

; PrPrefix: con varios archivos, "NOMBRE.EXT:"
PrPrefix:
	LD	A,(Multi)
	OR	A
	RET	Z
	CALL	PrName
	LD	A,':'
	JP	PrChar

; PrName: el archivo en curso, NOMBRE.EXT
PrName:
	LD	HL,(CurName)
	INC	HL
	LD	B,8
	CALL	PrBlank
	LD	HL,(CurName)
	LD	DE,9
	ADD	HL,DE
	LD	A,(HL)
	CP	' '
	RET	Z
	LD	A,'.'
	CALL	PrChar
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
; Pantalla

PrChar:
	PUSH	AF
	PUSH	BC
	PUSH	DE
	PUSH	HL
	LD	E,A
	LD	C,2			; con CTRL+C y CTRL+S del DOS
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

; PrZ, de a una letra: una linea puede traer '$'
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

MsgUsage:	DB	"Uso: GREP [/I/N/V/C] patron arch...",13,10
		DB	" /I mayusculas  /N numeros",13,10
		DB	" /V las otras   /C contar",13,10
		DB	" . cualquiera   * repetido",13,10
		DB	" ^ principio    $ final",13,10,0
MsgLongPat:	DB	"El patron es demasiado largo.",13,10,'$'
MsgNotFound:	DB	"No se encontro $"
MsgNoOpen:	DB	"No se pudo abrir $"
MsgCrLf:	DB	13,10,'$'

IgnCase:	DB	0
Numbers:	DB	0
Invert:		DB	0
CountOnly:	DB	0
Caret:		DB	0
Dollar:		DB	0
Multi:		DB	0
NNames:		DB	0
LineLen:	DB	0
LineNo:		DW	0
Count:		DW	0
SpecPtr:	DW	0
CurName:	DW	0

; Los buferes, despues del programa
Pat		EQU	$
Tokens		EQU	Pat+PATMAX+1
Fcb		EQU	Tokens+2*PATMAX+2
Search		EQU	Fcb+37
LineBuf		EQU	Search+64
Names		EQU	LineBuf+LINEMAX+1
ReadBuf		EQU	Names+12*NAMESMAX
