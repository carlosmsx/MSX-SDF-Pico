; VI.COM - editor al estilo de vi para MSX-DOS 1 (pantalla de 40 columnas)
;
;   VI [archivo]
;
; Modos normal, insertar y de linea de comandos (:). No depende del SDF-Pico:
; sirve en cualquier MSX con MSX-DOS 1. Se ensambla con PASMO:
;
;   pasmo vi.asm vi.com
;
; El texto vive en un solo bufer con hueco (gap buffer): el cursor es el
; hueco. Lo de antes del cursor queda en [BufBeg, GapS) y lo de despues en
; [GapE, BufEnd). Cada linea termina en LF; al leer se quitan los CR y al
; grabar se vuelven a poner. Las posiciones "logicas" (como Top) cuentan los
; bytes de texto sin contar el hueco.

BDOS		EQU	0x0005
CALSLT		EQU	0x001C
EXPTBL		EQU	0xFCC1
; Rutinas de la ROM principal. En MSX-DOS 1 la pagina 0 es RAM, asi que se
; llaman con CALSLT (ver Bios) y no pueden recibir datos de esa pagina: por eso
; la VRAM se maneja directo, por los puertos del VDP (VWrite y VRead8).
RDSLT		EQU	0x000C
INITXT		EQU	0x006C
CLS		EQU	0x00C3
POSIT		EQU	0x00C6
ERAFNK		EQU	0x00CC
LINL40		EQU	0xF3AE
CNSDFG		EQU	0xF3DE

LF		EQU	10
CR		EQU	13
ESC		EQU	27
EOFCH		EQU	0x1A

ROWS		EQU	23		; filas de texto; la 24 es la linea de estado
COLS		EQU	40
CURCHAR		EQU	0xFE		; caracter cuyo dibujo es el cursor
YANKMAX		EQU	1024

	ORG	0x0100

Start:
	; COMMAND.COM vive justo debajo del DOS (6656 bytes): el bufer termina
	; antes, asi no hay que recargarlo al volver. La pila, 256 bytes sobre el
	; fin del bufer.
	LD	HL,(0x0006)
	LD	DE,0x1C00
	OR	A
	SBC	HL,DE
	LD	SP,HL
	LD	DE,256
	OR	A
	SBC	HL,DE
	LD	(BufEnd),HL
	LD	HL,TextArea
	LD	(BufBeg),HL
	LD	(GapS),HL
	LD	HL,(BufEnd)
	LD	(GapE),HL

	LD	A,(LINL40)
	LD	(OldWidth),A
	LD	A,COLS
	LD	(LINL40),A
	XOR	A
	LD	(CNSDFG),A
	LD	IX,INITXT
	CALL	Bios
	LD	IX,ERAFNK
	CALL	Bios
	CALL	VdpInit

	; el nombre: el DOS ya armo un FCB con el primer argumento en 005Ch
	LD	A,(0x005D)
	CP	' '
	JR	Z,NoArg
	LD	HL,0x005C
	LD	DE,Fcb
	LD	BC,12
	LDIR
	CALL	LoadFile
NoArg:
	JP	Main

; ----------------------------------------------------------------------------
; Bucle principal

Main:
	CALL	Redraw
	CALL	GetKey
	LD	C,A
	XOR	A
	LD	(MsgPtr),A
	LD	(MsgPtr+1),A
	LD	A,(InsMode)
	OR	A
	JP	NZ,InsKey
	JP	NormKey

; Bios: llama a la rutina de la ROM principal en IX
Bios:
	LD	IY,(EXPTBL-1)
	CALL	CALSLT
	EI
	RET

; GetKey: A = tecla (la consola del DOS, sin eco y sin esperar CTRL+C)
GetKey:
	LD	E,0xFF
	LD	C,6
	CALL	BDOS
	OR	A
	RET	NZ
	EI
	HALT
	JR	GetKey

Reset:
	XOR	A
	LD	(Count),A
	LD	(Pending),A
	JP	Main

DoneH:
	CALL	ClampCursor
	CALL	SetDesCol
	JP	Reset

DoneV:
	CALL	ClampCursor
	JP	Reset

GetCnt:
	LD	A,(Count)
	OR	A
	JR	NZ,GetCnt1
	INC	A
GetCnt1:
	LD	B,A
	RET

; ----------------------------------------------------------------------------
; Modo normal

NormKey:
	LD	A,(Pending)
	OR	A
	JP	NZ,PendKey
	LD	A,C
	CP	'1'
	JR	C,NkZero
	CP	'9'+1
	JR	NC,NkLookup
	JR	NkDigit
NkZero:
	CP	'0'
	JR	NZ,NkLookup
	LD	A,(Count)
	OR	A
	JR	Z,NkLookup		; 0 sin cuenta: columna 0
	LD	A,'0'
NkDigit:
	SUB	'0'
	LD	B,A
	LD	A,(Count)
	CP	26
	JR	NC,NkCap
	ADD	A,A			; x2
	LD	D,A
	ADD	A,A
	ADD	A,A			; x8
	ADD	A,D			; x10
	ADD	A,B
	LD	(Count),A
	JP	Main
NkCap:
	LD	A,255
	LD	(Count),A
	JP	Main

NkLookup:
	LD	HL,KeyTab
NkLoop:
	LD	A,(HL)
	OR	A
	JP	Z,Reset
	CP	C
	JR	Z,NkHit
	INC	HL
	INC	HL
	INC	HL
	JR	NkLoop
NkHit:
	INC	HL
	LD	E,(HL)
	INC	HL
	LD	D,(HL)
	EX	DE,HL
	JP	(HL)

KeyTab:
	DB	'h'
	DW	CmdLeft
	DB	0x1D
	DW	CmdLeft
	DB	8
	DW	CmdLeft
	DB	'l'
	DW	CmdRight
	DB	0x1C
	DW	CmdRight
	DB	' '
	DW	CmdRight
	DB	'j'
	DW	CmdDown
	DB	0x1F
	DW	CmdDown
	DB	'k'
	DW	CmdUp
	DB	0x1E
	DW	CmdUp
	DB	'0'
	DW	CmdLineStart
	DB	'$'
	DW	CmdLineEnd
	DB	'w'
	DW	CmdWord
	DB	'b'
	DW	CmdBack
	DB	'x'
	DW	CmdX
	DB	127
	DW	CmdX
	DB	'i'
	DW	CmdI
	DB	'a'
	DW	CmdA
	DB	'I'
	DW	CmdIc
	DB	'A'
	DW	CmdAc
	DB	'o'
	DW	CmdO
	DB	'O'
	DW	CmdOc
	DB	'G'
	DW	CmdG
	DB	'p'
	DW	CmdP
	DB	'P'
	DW	CmdPb
	DB	'J'
	DW	CmdJ
	DB	'/'
	DW	CmdSlash
	DB	'n'
	DW	CmdN
	DB	':'
	DW	CmdColon
	DB	'd'
	DW	CmdPend
	DB	'y'
	DW	CmdPend
	DB	'g'
	DW	CmdPend
	DB	'Z'
	DW	CmdPend
	DB	'r'
	DW	CmdPend
	DB	0x06			; ^F
	DW	CmdPgDown
	DB	0x02			; ^B
	DW	CmdPgUp
	DB	0x04			; ^D
	DW	CmdHalfDown
	DB	0x15			; ^U
	DW	CmdHalfUp
	DB	0x07			; ^G
	DW	CmdInfo
	DB	0

CmdPend:
	LD	A,C
	LD	(Pending),A
	JP	Main

PendKey:
	LD	B,A			; B = la tecla pendiente
	XOR	A
	LD	(Pending),A
	LD	A,B
	CP	'd'
	JR	NZ,Pk1
	LD	A,C
	CP	'd'
	JP	Z,CmdDD
	JP	Reset
Pk1:
	CP	'y'
	JR	NZ,Pk2
	LD	A,C
	CP	'y'
	JP	Z,CmdYY
	JP	Reset
Pk2:
	CP	'g'
	JR	NZ,Pk3
	LD	A,C
	CP	'g'
	JP	Z,CmdGG
	JP	Reset
Pk3:
	CP	'Z'
	JR	NZ,Pk4
	LD	A,C
	CP	'Z'
	JP	Z,CmdZZ
	JP	Reset
Pk4:
	CP	'r'
	JP	Z,CmdReplace
	JP	Reset

; --- movimiento ---

CmdLeft:
	CALL	GetCnt
CmdLeft1:
	CALL	CharBefore
	JP	C,DoneH
	CP	LF
	JP	Z,DoneH
	CALL	MoveLeft
	DJNZ	CmdLeft1
	JP	DoneH

CmdRight:
	CALL	GetCnt
CmdRight1:
	CALL	CanRight
	JP	C,DoneH
	CALL	MoveRight
	DJNZ	CmdRight1
	JP	DoneH

CmdDown:
	CALL	GetCnt
CmdDown1:
	CALL	LineDown
	DJNZ	CmdDown1
	CALL	GotoCol
	JP	DoneV

CmdUp:
	CALL	GetCnt
CmdUp1:
	CALL	LineUp
	DJNZ	CmdUp1
	CALL	GotoCol
	JP	DoneV

CmdPgDown:
	LD	B,20
	JR	CmdDown1
CmdPgUp:
	LD	B,20
	JR	CmdUp1
CmdHalfDown:
	LD	B,11
	JR	CmdDown1
CmdHalfUp:
	LD	B,11
	JR	CmdUp1

CmdLineStart:
	CALL	ToLineStart
	XOR	A
	LD	(DesCol),A
	JP	DoneV

CmdLineEnd:
	CALL	ToLineEnd
	CALL	CharBefore
	JR	C,CmdLineEnd1
	CP	LF
	JR	Z,CmdLineEnd1
	CALL	MoveLeft
CmdLineEnd1:
	LD	A,255
	LD	(DesCol),A
	JP	DoneV

CmdWord:
	CALL	GetCnt
CmdWord1:
	CALL	WordFwd
	DJNZ	CmdWord1
	JP	DoneH

CmdBack:
	CALL	GetCnt
CmdBack1:
	CALL	WordBack
	DJNZ	CmdBack1
	JP	DoneH

CmdG:
	LD	A,(Count)
	OR	A
	JR	Z,CmdG1
	LD	L,A
	LD	H,0
	CALL	GotoLine
	JP	DoneH
CmdG1:
	CALL	ToBottom
	CALL	ClampCursor
	CALL	ToLineStart
	JP	DoneH

CmdGG:
	LD	A,(Count)
	OR	A
	JR	NZ,CmdG
	CALL	ToTop
	JP	DoneH

; --- edicion ---

CmdX:
	CALL	GetCnt
CmdX1:
	CALL	CharAt
	JP	C,DoneH
	CP	LF
	JP	Z,DoneH
	CALL	DelAfter
	DJNZ	CmdX1
	JP	DoneH

CmdReplace:
	LD	A,C
	CP	32
	JP	C,Reset
	CALL	CharAt
	JP	C,Reset
	CP	LF
	JP	Z,Reset
	CALL	DelAfter
	LD	A,C
	CALL	Insert
	CALL	MoveLeft
	JP	DoneH

CmdJ:
	CALL	ToLineEnd
	CALL	CharAt
	JP	C,DoneH
	CALL	DelAfter		; el LF
	LD	A,' '
	CALL	Insert
	CALL	MoveLeft
	JP	DoneH

CmdI:
	JR	StartInsert
CmdA:
	CALL	CharAt
	JR	C,StartInsert
	CP	LF
	JR	Z,StartInsert
	CALL	MoveRight
	JR	StartInsert
CmdIc:
	CALL	ToLineStart
	JR	StartInsert
CmdAc:
	CALL	ToLineEnd
	JR	StartInsert
CmdO:
	CALL	ToLineEnd
	CALL	CharAt
	JR	C,CmdO1			; sin LF al final del texto
	CALL	MoveRight		; pasa el LF
	LD	A,LF
	CALL	Insert
	CALL	MoveLeft
	JR	StartInsert
CmdO1:
	LD	A,LF
	CALL	Insert
	LD	A,LF
	CALL	Insert
	CALL	MoveLeft
	JR	StartInsert
CmdOc:
	CALL	ToLineStart
	LD	A,LF
	CALL	Insert
	CALL	MoveLeft
StartInsert:
	LD	A,1
	LD	(InsMode),A
	XOR	A
	LD	(Count),A
	JP	Main

; dd: borra Count lineas y las deja en el registro
CmdDD:
	CALL	ToLineStart
	CALL	GetCnt
	LD	A,B
	LD	(Rep),A
	LD	HL,YankBuf
	LD	(YPtr),HL
	LD	HL,0
	LD	(YLen),HL
CmdDD1:
	CALL	CharAt
	JR	C,CmdDD9
	LD	C,A
	CALL	YankAdd
	CALL	DelAfter
	LD	A,C
	CP	LF
	JR	NZ,CmdDD1
	LD	A,(Rep)
	DEC	A
	LD	(Rep),A
	JR	NZ,CmdDD1
CmdDD9:
	CALL	YankEnd
	JP	DoneH

; yy: copia Count lineas al registro, sin mover el cursor
CmdYY:
	CALL	CurColumn
	LD	(SaveCol),A
	CALL	ToLineStart
	CALL	GetCnt
	LD	A,B
	LD	(Rep),A
	LD	HL,YankBuf
	LD	(YPtr),HL
	LD	HL,0
	LD	(YLen),HL
	LD	HL,(GapE)
CmdYY1:
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,CmdYY2
	LD	A,H
	CP	D
	JR	Z,CmdYY9
CmdYY2:
	LD	A,(HL)
	INC	HL
	LD	C,A
	PUSH	HL
	CALL	YankAdd
	POP	HL
	LD	A,C
	CP	LF
	JR	NZ,CmdYY1
	LD	A,(Rep)
	DEC	A
	LD	(Rep),A
	JR	NZ,CmdYY1
CmdYY9:
	CALL	YankEnd
	LD	A,(SaveCol)
	LD	(DesCol),A
	CALL	GotoCol
	JP	DoneV

; YankAdd: agrega C al registro si hay lugar
YankAdd:
	LD	HL,(YLen)
	LD	DE,YANKMAX-1
	OR	A
	SBC	HL,DE
	RET	NC
	LD	HL,(YPtr)
	LD	(HL),C
	INC	HL
	LD	(YPtr),HL
	LD	HL,(YLen)
	INC	HL
	LD	(YLen),HL
	RET

; YankEnd: el registro termina en LF
YankEnd:
	LD	HL,(YLen)
	LD	A,H
	OR	L
	RET	Z
	LD	HL,(YPtr)
	DEC	HL
	LD	A,(HL)
	CP	LF
	RET	Z
	LD	C,LF
	JP	YankAdd

; p: el registro va en la linea de abajo
CmdP:
	LD	HL,(YLen)
	LD	A,H
	OR	L
	JP	Z,Reset
	CALL	ToLineEnd
	CALL	CharAt
	JR	C,CmdP1
	CALL	MoveRight
	JR	CmdPut
CmdP1:
	LD	A,LF
	CALL	Insert
	JR	CmdPut

; P: arriba
CmdPb:
	LD	HL,(YLen)
	LD	A,H
	OR	L
	JP	Z,Reset
	CALL	ToLineStart
CmdPut:
	CALL	GetCnt
CmdPut1:
	PUSH	BC
	LD	HL,YankBuf
	LD	BC,(YLen)
CmdPut2:
	LD	A,(HL)
	INC	HL
	CALL	Insert
	JR	C,CmdPutFull
	DEC	BC
	LD	A,B
	OR	C
	JR	NZ,CmdPut2
	POP	BC
	DJNZ	CmdPut1
	JR	CmdPut3
CmdPutFull:
	POP	BC
	LD	HL,MsgFull
	LD	(MsgPtr),HL
CmdPut3:
	; el cursor, al principio de lo ultimo que se puso
	CALL	PreLen
	LD	DE,(YLen)
	OR	A
	SBC	HL,DE
	JR	NC,CmdPut4
	LD	HL,0
CmdPut4:
	CALL	GapMove
	JP	DoneH

; ----------------------------------------------------------------------------
; Modo insertar

InsKey:
	LD	A,C
	CP	ESC
	JP	Z,IkEsc
	CP	CR
	JP	Z,IkCr
	CP	8
	JP	Z,IkBs
	CP	127
	JP	Z,IkDel
	CP	0x1D
	JP	Z,IkLeft
	CP	0x1C
	JP	Z,IkRight
	CP	0x1E
	JP	Z,IkUp
	CP	0x1F
	JP	Z,IkDown
	CP	9
	JP	Z,IkPut
	CP	32
	JP	C,Main
IkPut:
	LD	A,C
	CALL	Insert
	JP	NC,Main
IkFull:
	LD	HL,MsgFull
	LD	(MsgPtr),HL
	JP	Main
IkCr:
	LD	A,LF
	CALL	Insert
	JP	NC,Main
	JR	IkFull
IkBs:
	CALL	DelBefore
	JP	Main
IkDel:
	CALL	DelAfter
	JP	Main
IkLeft:
	CALL	CharBefore
	JP	C,Main
	CP	LF
	JP	Z,Main
	CALL	MoveLeft
	JP	Main
IkRight:
	CALL	CharAt
	JP	C,Main
	CP	LF
	JP	Z,Main
	CALL	MoveRight
	JP	Main
IkUp:
	CALL	CurColumn
	LD	(DesCol),A
	CALL	LineUp
	CALL	GotoCol
	JP	Main
IkDown:
	CALL	CurColumn
	LD	(DesCol),A
	CALL	LineDown
	CALL	GotoCol
	JP	Main
IkEsc:
	XOR	A
	LD	(InsMode),A
	CALL	CharBefore
	JR	C,IkEsc1
	CP	LF
	JP	Z,IkEsc1
	CALL	MoveLeft
IkEsc1:
	JP	DoneH

; ----------------------------------------------------------------------------
; El bufer

; PreLen: HL = bytes antes del cursor
PreLen:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	OR	A
	SBC	HL,DE
	RET

; TextLen: HL = bytes de texto
TextLen:
	CALL	PreLen
	PUSH	HL
	LD	HL,(BufEnd)
	LD	DE,(GapE)
	OR	A
	SBC	HL,DE
	POP	DE
	ADD	HL,DE
	RET

; Las primitivas del cursor conservan BC.

; CharAt: A = el byte bajo el cursor; carry si no hay (fin del texto)
CharAt:
	LD	HL,(GapE)
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,CharAt1
	LD	A,H
	CP	D
	JR	NZ,CharAt1
	SCF
	RET
CharAt1:
	LD	A,(HL)
	OR	A
	RET

; CharBefore: A = el byte antes del cursor; carry si no hay
CharBefore:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	LD	A,L
	CP	E
	JR	NZ,CharBef1
	LD	A,H
	CP	D
	JR	NZ,CharBef1
	SCF
	RET
CharBef1:
	DEC	HL
	LD	A,(HL)
	OR	A
	RET

; MoveLeft / MoveRight: el cursor un caracter; carry si no se puede
MoveLeft:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	LD	A,L
	CP	E
	JR	NZ,MoveLeft1
	LD	A,H
	CP	D
	JR	NZ,MoveLeft1
	SCF
	RET
MoveLeft1:
	DEC	HL
	LD	(GapS),HL
	LD	A,(HL)
	LD	HL,(GapE)
	DEC	HL
	LD	(GapE),HL
	LD	(HL),A
	OR	A
	RET

MoveRight:
	LD	HL,(GapE)
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,MoveRight1
	LD	A,H
	CP	D
	JR	NZ,MoveRight1
	SCF
	RET
MoveRight1:
	LD	A,(HL)
	INC	HL
	LD	(GapE),HL
	LD	HL,(GapS)
	LD	(HL),A
	INC	HL
	LD	(GapS),HL
	OR	A
	RET

; Insert: A en el cursor; carry si el bufer esta lleno
Insert:
	PUSH	DE
	PUSH	HL
	LD	HL,(GapS)
	LD	DE,(GapE)
	PUSH	AF
	LD	A,L
	CP	E
	JR	NZ,Insert1
	LD	A,H
	CP	D
	JR	NZ,Insert1
	POP	AF
	POP	HL
	POP	DE
	SCF
	RET
Insert1:
	POP	AF
	LD	(HL),A
	INC	HL
	LD	(GapS),HL
	LD	A,1
	LD	(Dirty),A
	POP	HL
	POP	DE
	OR	A
	RET

; DelBefore / DelAfter: borra el caracter antes / bajo el cursor
DelBefore:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	LD	A,L
	CP	E
	JR	NZ,DelBef1
	LD	A,H
	CP	D
	JR	NZ,DelBef1
	SCF
	RET
DelBef1:
	DEC	HL
	LD	(GapS),HL
	LD	A,1
	LD	(Dirty),A
	OR	A
	RET

DelAfter:
	LD	HL,(GapE)
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,DelAft1
	LD	A,H
	CP	D
	JR	NZ,DelAft1
	SCF
	RET
DelAft1:
	INC	HL
	LD	(GapE),HL
	LD	A,1
	LD	(Dirty),A
	OR	A
	RET

; GapMove: el cursor a la posicion logica HL, moviendo el texto en bloque
GapMove:
	PUSH	HL
	CALL	PreLen
	POP	DE
	OR	A
	SBC	HL,DE			; pre - destino
	RET	Z
	JR	C,GapMoveR
	LD	(Tmp16),HL		; hacia la izquierda: n = pre - destino
	LD	B,H
	LD	C,L
	LD	HL,(GapS)
	DEC	HL
	LD	DE,(GapE)
	DEC	DE
	LDDR
	LD	HL,(GapS)
	LD	DE,(Tmp16)
	OR	A
	SBC	HL,DE
	LD	(GapS),HL
	LD	HL,(GapE)
	OR	A
	SBC	HL,DE
	LD	(GapE),HL
	RET
GapMoveR:
	EX	DE,HL
	LD	HL,0
	OR	A
	SBC	HL,DE			; n = destino - pre
	LD	(Tmp16),HL
	LD	B,H
	LD	C,L
	LD	HL,(GapE)
	LD	DE,(GapS)
	LDIR
	LD	HL,(GapS)
	LD	DE,(Tmp16)
	ADD	HL,DE
	LD	(GapS),HL
	LD	HL,(GapE)
	ADD	HL,DE
	LD	(GapE),HL
	RET

ToTop:
	LD	HL,0
	JP	GapMove

ToBottom:
	CALL	TextLen
	JP	GapMove

; ToLineStart / ToLineEnd
ToLineStart:
	CALL	CharBefore
	RET	C
	CP	LF
	RET	Z
	CALL	MoveLeft
	JR	ToLineStart

ToLineEnd:
	CALL	CharAt
	RET	C
	CP	LF
	RET	Z
	CALL	MoveRight
	JR	ToLineEnd

; CanRight: carry si no hay otro caracter de la linea a la derecha
CanRight:
	CALL	CharAt
	JR	C,CanRightNo
	CP	LF
	JR	Z,CanRightNo
	LD	HL,(GapE)
	INC	HL
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,CanRight1
	LD	A,H
	CP	D
	JR	Z,CanRightNo
CanRight1:
	LD	A,(HL)
	CP	LF
	JR	Z,CanRightNo
	OR	A
	RET
CanRightNo:
	SCF
	RET

; CurColumn: A = columna del cursor (hasta 255)
CurColumn:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	LD	B,0
CurCol1:
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	Z,CurCol9
	DEC	HL
	LD	A,(HL)
	CP	LF
	JR	Z,CurCol9
	INC	B
	JR	NZ,CurCol1
	LD	B,255
CurCol9:
	LD	A,B
	RET

SetDesCol:
	CALL	CurColumn
	LD	(DesCol),A
	RET

; GotoCol: al principio de la linea y hasta DesCol caracteres a la derecha
GotoCol:
	CALL	ToLineStart
	LD	A,(DesCol)
	LD	B,A
GotoCol1:
	LD	A,B
	OR	A
	RET	Z
	CALL	CanRight
	RET	C
	CALL	MoveRight
	DJNZ	GotoCol1
	RET

; LineDown / LineUp: pasan a la linea de abajo / arriba (si la hay)
LineDown:
	CALL	ToLineEnd
	CALL	CharAt
	RET	C
	CALL	MoveRight
	CALL	CharAt
	RET	NC
	JP	MoveLeft		; el LF era lo ultimo: se queda en esa linea

LineUp:
	CALL	ToLineStart
	CALL	CharBefore
	RET	C
	JP	MoveLeft

; GotoLine: HL = numero de linea (1 es la primera)
GotoLine:
	PUSH	HL
	CALL	ToTop
	POP	HL
GotoLine1:
	DEC	HL
	LD	A,H
	OR	L
	JR	Z,GotoLine9
	PUSH	HL
	CALL	LineDown
	POP	HL
	JR	GotoLine1
GotoLine9:
	JP	ToLineStart

; ClampCursor: en modo normal el cursor tiene que estar sobre un caracter
ClampCursor:
	CALL	CharAt
	JR	NC,Clamp1
	CALL	CharBefore		; al final del texto
	RET	C			; bufer vacio
	CALL	MoveLeft
	JR	ClampCursor
Clamp1:
	CP	LF
	RET	NZ
	CALL	CharBefore		; sobre un LF: vale solo en una linea vacia
	RET	C
	CP	LF
	RET	Z
	JP	MoveLeft

; CClass: A = 0 espacio, 1 letra o digito, 2 otro
CClass:
	CP	' '
	JR	Z,CClassSp
	CP	9
	JR	Z,CClassSp
	CP	LF
	JR	Z,CClassSp
	CP	'0'
	JR	C,CClassPu
	CP	'9'+1
	JR	C,CClassAl
	CP	'A'
	JR	C,CClassPu
	CP	'Z'+1
	JR	C,CClassAl
	CP	'_'
	JR	Z,CClassAl
	CP	'a'
	JR	C,CClassPu
	CP	'z'+1
	JR	C,CClassAl
	CP	0x80
	JR	NC,CClassAl
CClassPu:
	LD	A,2
	RET
CClassAl:
	LD	A,1
	RET
CClassSp:
	XOR	A
	RET

WordFwd:
	CALL	CharAt
	RET	C
	CALL	CClass
	OR	A
	JR	Z,WordFwd2
	LD	C,A
WordFwd1:
	CALL	MoveRight
	RET	C
	CALL	CharAt
	RET	C
	CALL	CClass
	CP	C
	JR	Z,WordFwd1
WordFwd2:
	CALL	CharAt
	RET	C
	CALL	CClass
	OR	A
	RET	NZ
	CALL	MoveRight
	JR	WordFwd2

WordBack:
	CALL	MoveLeft
	RET	C
WordBack1:
	CALL	CharAt
	RET	C
	CALL	CClass
	OR	A
	JR	NZ,WordBack2
	CALL	MoveLeft
	RET	C
	JR	WordBack1
WordBack2:
	LD	C,A
WordBack3:
	CALL	CharBefore
	RET	C
	CALL	CClass
	CP	C
	RET	NZ
	CALL	MoveLeft
	JR	WordBack3

; ----------------------------------------------------------------------------
; Busqueda

CmdSlash:
	LD	A,'/'
	CALL	ReadLine
	JP	C,Reset
	LD	A,(CmdLen)
	OR	A
	JR	Z,CmdN
	LD	(SLen),A
	LD	B,A
	LD	HL,CmdBuf
	LD	DE,SBuf
CmdSlash1:
	LD	A,(HL)
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	CmdSlash1
CmdN:
	LD	A,(SLen)
	OR	A
	JR	NZ,DoSearch
	LD	HL,MsgNoPat
	LD	(MsgPtr),HL
	JP	Reset

DoSearch:
	CALL	PreLen
	LD	(SrchOrig),HL
DoSearch1:
	CALL	MoveRight
	JR	C,DoSearchWrap
	CALL	MatchHere
	JR	NC,DoSearchFound
	JR	DoSearch1
DoSearchWrap:
	CALL	ToTop
DoSearch2:
	CALL	MatchHere
	JR	NC,DoSearchFound
	CALL	PreLen
	LD	DE,(SrchOrig)
	OR	A
	SBC	HL,DE
	JR	NC,DoSearchNo
	CALL	MoveRight
	JR	NC,DoSearch2
DoSearchNo:
	LD	HL,(SrchOrig)
	CALL	GapMove
	LD	HL,MsgNotFound
	LD	(MsgPtr),HL
	JP	DoneH
DoSearchFound:
	JP	DoneH

; MatchHere: NC si el patron esta en el cursor
MatchHere:
	LD	A,(SLen)
	LD	E,A
	LD	D,0
	LD	HL,(GapE)
	ADD	HL,DE
	LD	DE,(BufEnd)
	OR	A
	SBC	HL,DE
	JR	Z,MatchHere0
	JR	NC,MatchNo
MatchHere0:
	LD	A,(SLen)
	LD	B,A
	LD	HL,(GapE)
	LD	DE,SBuf
MatchHere1:
	LD	A,(DE)
	CP	(HL)
	JR	NZ,MatchNo
	INC	HL
	INC	DE
	DJNZ	MatchHere1
	OR	A
	RET
MatchNo:
	SCF
	RET

; ----------------------------------------------------------------------------
; Linea de comandos

; ReadLine: A = ':' o '/'; lee en CmdBuf. Carry si se cancelo
ReadLine:
	LD	(Prompt),A
	XOR	A
	LD	(CmdLen),A
	LD	A,1
	LD	(CmdActive),A
RdLine1:
	CALL	Redraw
	CALL	GetKey
	CP	CR
	JR	Z,RdLineOk
	CP	ESC
	JR	Z,RdLineCancel
	CP	8
	JR	Z,RdLineBs
	CP	32
	JR	C,RdLine1
	LD	C,A
	LD	A,(CmdLen)
	CP	COLS-3
	JR	NC,RdLine1
	LD	E,A
	LD	D,0
	LD	HL,CmdBuf
	ADD	HL,DE
	LD	(HL),C
	INC	A
	LD	(CmdLen),A
	JR	RdLine1
RdLineBs:
	LD	A,(CmdLen)
	OR	A
	JR	Z,RdLineCancel
	DEC	A
	LD	(CmdLen),A
	JR	RdLine1
RdLineOk:
	LD	A,(CmdLen)
	LD	E,A
	LD	D,0
	LD	HL,CmdBuf
	ADD	HL,DE
	LD	(HL),0
	XOR	A
	LD	(CmdActive),A
	RET
RdLineCancel:
	XOR	A
	LD	(CmdActive),A
	SCF
	RET

CmdColon:
	LD	A,':'
	CALL	ReadLine
	JP	C,Reset
	LD	HL,CmdBuf
ExLoop:
	LD	A,(HL)
	CP	' '
	JR	NZ,ExStart
	INC	HL
	JR	ExLoop
ExStart:
	OR	A
	JP	Z,Reset
	CP	'0'
	JR	C,ExWord
	CP	'9'+1
	JR	NC,ExWord
	; :N, ir a la linea N
	LD	DE,0
ExNum:
	LD	A,(HL)
	SUB	'0'
	JR	C,ExNumEnd
	CP	10
	JR	NC,ExNumEnd
	PUSH	HL
	LD	H,D
	LD	L,E
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,DE
	ADD	HL,HL			; x10
	LD	E,A
	LD	D,0
	ADD	HL,DE
	EX	DE,HL
	POP	HL
	INC	HL
	JR	ExNum
ExNumEnd:
	EX	DE,HL
	LD	A,H
	OR	L
	JR	NZ,ExNum1
	INC	HL
ExNum1:
	CALL	GotoLine
	JP	DoneH
ExWord:
	LD	A,(HL)
	INC	HL
	CP	'w'
	JR	Z,ExW
	CP	'x'
	JR	Z,ExSave
	CP	'q'
	JR	Z,ExQ
	LD	HL,MsgBadCmd
	LD	(MsgPtr),HL
	JP	Reset
ExQ:
	LD	A,(HL)
	CP	'!'
	JP	Z,Quit
	OR	A
	JR	NZ,ExBad
	LD	A,(Dirty)
	OR	A
	JP	Z,Quit
	LD	HL,MsgNoWrite
	LD	(MsgPtr),HL
	JP	Reset
ExBad:
	LD	HL,MsgBadCmd
	LD	(MsgPtr),HL
	JP	Reset
ExSave:
	LD	A,(Dirty)
	OR	A
	JP	Z,Quit
	CALL	SaveFile
	JP	C,Reset
	JP	Quit
ExW:
	LD	A,(HL)
	CP	'q'
	JR	Z,ExWQ
	CP	'!'
	JR	NZ,ExW1
	INC	HL
	LD	A,(HL)
ExW1:
	CP	' '
	JR	NZ,ExW2
ExW1b:
	INC	HL
	LD	A,(HL)
	CP	' '
	JR	Z,ExW1b
	OR	A
	JR	Z,ExW2
	PUSH	HL			; :w nombre graba en otro archivo, sin cambiar el actual
	LD	HL,Fcb
	LD	DE,FcbBak
	LD	BC,40
	LDIR
	POP	HL
	LD	A,(Dirty)
	LD	(DirtyBak),A
	LD	A,1
	LD	(NameGiven),A
	LD	DE,Fcb
	CALL	ParseName
ExW2:
	CALL	SaveFile
	PUSH	AF
	LD	A,(NameGiven)
	OR	A
	JR	Z,ExW3
	XOR	A
	LD	(NameGiven),A
	LD	A,(FcbBak+1)
	CP	' '
	JR	Z,ExW3			; no tenia nombre: se queda con el nuevo
	LD	HL,FcbBak
	LD	DE,Fcb
	LD	BC,40
	LDIR
	LD	A,(DirtyBak)
	LD	(Dirty),A
ExW3:
	POP	AF
	JP	Reset
ExWQ:
	CALL	SaveFile
	JP	C,Reset
	JP	Quit

CmdZZ:
	LD	A,(Dirty)
	OR	A
	JP	Z,Quit
	CALL	SaveFile
	JP	C,Reset
	JP	Quit

CmdInfo:
	; "nombre" [Modificado] linea N de M
	LD	DE,MsgBuf
	CALL	PutName
	LD	A,(Dirty)
	OR	A
	JR	Z,CmdInfo1
	LD	HL,TxtMod
	CALL	PutStr
CmdInfo1:
	LD	HL,TxtLine
	CALL	PutStr
	PUSH	DE
	CALL	PreLen
	LD	(Tmp16),HL
	CALL	CountLinesBefore
	INC	HL
	POP	DE
	CALL	PutDec
	LD	HL,TxtOf
	CALL	PutStr
	PUSH	DE
	CALL	CountLinesAll
	POP	DE
	CALL	PutDec
	XOR	A
	LD	(DE),A
	LD	HL,MsgBuf
	LD	(MsgPtr),HL
	JP	Reset

; CountLinesBefore: HL = LF antes del cursor
CountLinesBefore:
	LD	HL,(GapS)
	LD	DE,(BufBeg)
	LD	BC,0
CntLB1:
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	Z,CntLB9
	DEC	HL
	LD	A,(HL)
	CP	LF
	JR	NZ,CntLB1
	INC	BC
	JR	CntLB1
CntLB9:
	LD	H,B
	LD	L,C
	RET

; CountLinesAll: HL = LF del texto (mas 1 si no termina en LF)
CountLinesAll:
	CALL	CountLinesBefore
	PUSH	HL
	LD	HL,(GapE)
	LD	DE,(BufEnd)
	LD	BC,0
CntLA1:
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	Z,CntLA9
	LD	A,(HL)
	INC	HL
	CP	LF
	JR	NZ,CntLA1
	INC	BC
	JR	CntLA1
CntLA9:
	POP	HL
	ADD	HL,BC
	RET

; ----------------------------------------------------------------------------
; Archivos

; ParseName: HL = texto ASCIIZ, DE = FCB. Pasa "[d:]nombre[.ext]" al FCB
ParseName:
	LD	(PnFcb),DE
	INC	DE			; nombre y extension, en blanco
	LD	B,11
PnBlank:
	LD	A,' '
	LD	(DE),A
	INC	DE
	DJNZ	PnBlank
	LD	DE,(PnFcb)
	XOR	A
	LD	(DE),A			; unidad por omision
	INC	HL
	LD	A,(HL)
	DEC	HL
	CP	':'
	JR	NZ,PnName
	LD	A,(HL)
	CALL	ToUpper
	SUB	'A'-1
	LD	(DE),A
	INC	HL
	INC	HL
PnName:
	LD	DE,(PnFcb)
	INC	DE
	LD	B,8
PnName1:
	LD	A,(HL)
	OR	A
	RET	Z
	CP	'.'
	JR	Z,PnExt
	CALL	ToUpper
	LD	(DE),A
	INC	DE
	INC	HL
	DJNZ	PnName1
PnSkip:
	LD	A,(HL)
	OR	A
	RET	Z
	CP	'.'
	JR	Z,PnExt
	INC	HL
	JR	PnSkip
PnExt:
	INC	HL			; despues del punto
	LD	DE,(PnFcb)
	LD	BC,9
	EX	DE,HL
	ADD	HL,BC
	EX	DE,HL			; DE = FCB + 9
	LD	B,3
PnExt1:
	LD	A,(HL)
	OR	A
	RET	Z
	CALL	ToUpper
	LD	(DE),A
	INC	DE
	INC	HL
	DJNZ	PnExt1
	RET

ToUpper:
	CP	'a'
	RET	C
	CP	'z'+1
	RET	NC
	SUB	32
	RET

; PutName: DE = destino. Escribe "nombre.ext" entre comillas
PutName:
	LD	A,'"'
	LD	(DE),A
	INC	DE
	LD	A,(Fcb+1)
	CP	' '
	JR	NZ,PutName1
	LD	HL,TxtNoName
	CALL	PutStr
	JR	PutName9
PutName1:
	LD	A,(Fcb)
	OR	A
	JR	Z,PutName2
	ADD	A,'A'-1
	LD	(DE),A
	INC	DE
	LD	A,':'
	LD	(DE),A
	INC	DE
PutName2:
	LD	HL,Fcb+1
	LD	B,8
	CALL	PutBlank
	LD	A,(Fcb+9)
	CP	' '
	JR	Z,PutName9
	LD	A,'.'
	LD	(DE),A
	INC	DE
	LD	HL,Fcb+9
	LD	B,3
	CALL	PutBlank
PutName9:
	LD	A,'"'
	LD	(DE),A
	INC	DE
	RET

; PutBlank: copia B caracteres de HL a DE sin los blancos
PutBlank:
	LD	A,(HL)
	INC	HL
	CP	' '
	JR	Z,PutBlank1
	LD	(DE),A
	INC	DE
PutBlank1:
	DJNZ	PutBlank
	RET

; PutStr: copia el texto ASCIIZ de HL a DE
PutStr:
	LD	A,(HL)
	OR	A
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	JR	PutStr

; PutDec: escribe HL en decimal en DE
PutDec:
	XOR	A
	LD	(PdFlag),A
	LD	BC,10000
	CALL	PutDigit
	LD	BC,1000
	CALL	PutDigit
	LD	BC,100
	CALL	PutDigit
	LD	BC,10
	CALL	PutDigit
	LD	A,L
	ADD	A,'0'
	LD	(DE),A
	INC	DE
	RET

PutDigit:
	XOR	A
PutDigit1:
	OR	A
	SBC	HL,BC
	JR	C,PutDigit2
	INC	A
	JR	PutDigit1
PutDigit2:
	ADD	HL,BC
	OR	A
	JR	NZ,PutDigit3
	LD	A,(PdFlag)
	OR	A
	RET	Z			; cero a la izquierda: no se escribe
	XOR	A
PutDigit3:
	PUSH	AF
	LD	A,1
	LD	(PdFlag),A
	POP	AF
	ADD	A,'0'
	LD	(DE),A
	INC	DE
	RET

; LoadFile: lee el archivo del FCB al bufer
LoadFile:
	LD	DE,Fcb
	XOR	A
	LD	(Fcb+12),A
	LD	(Fcb+32),A
	LD	C,0x0F
	CALL	BDOS
	INC	A
	JR	Z,LoadNew
	LD	DE,IoBuf
	LD	C,0x1A
	CALL	BDOS
LoadRec:
	LD	DE,Fcb
	LD	C,0x14
	CALL	BDOS
	OR	A
	JR	NZ,LoadEnd
	LD	HL,IoBuf
	LD	B,128
LoadByte:
	LD	A,(HL)
	INC	HL
	CP	EOFCH
	JR	Z,LoadEnd
	OR	A
	JR	Z,LoadEnd
	CP	CR
	JR	Z,LoadNext
	CALL	Insert
	JR	C,LoadFull
LoadNext:
	DJNZ	LoadByte
	JR	LoadRec
LoadFull:
	LD	A,1
	LD	(ReadOnly),A
	LD	HL,MsgTooBig
	LD	(MsgPtr),HL
	JR	LoadDone
LoadEnd:
	LD	DE,MsgBuf
	CALL	PutName
	LD	A,' '
	LD	(DE),A
	INC	DE
	PUSH	DE
	CALL	CountLinesAll
	POP	DE
	CALL	PutDec
	LD	A,'L'
	LD	(DE),A
	INC	DE
	LD	A,','
	LD	(DE),A
	INC	DE
	LD	A,' '
	LD	(DE),A
	INC	DE
	PUSH	DE
	CALL	TextLen
	POP	DE
	CALL	PutDec
	LD	A,'C'
	LD	(DE),A
	INC	DE
	XOR	A
	LD	(DE),A
	LD	HL,MsgBuf
	LD	(MsgPtr),HL
	JR	LoadDone
LoadNew:
	LD	DE,MsgBuf
	CALL	PutName
	LD	HL,TxtNew
	CALL	PutStr
	XOR	A
	LD	(DE),A
	LD	HL,MsgBuf
	LD	(MsgPtr),HL
LoadDone:
	LD	DE,Fcb
	LD	C,0x10
	CALL	BDOS
	CALL	ToTop
	XOR	A
	LD	(Dirty),A
	RET

; SaveFile: graba el bufer en el archivo del FCB. Carry si fallo
SaveFile:
	LD	A,(ReadOnly)
	OR	A
	JR	Z,SaveOk0
	LD	HL,MsgRO
	LD	(MsgPtr),HL
	SCF
	RET
SaveOk0:
	LD	A,(Fcb+1)
	CP	' '
	JR	NZ,Save1
	LD	HL,MsgNoName
	LD	(MsgPtr),HL
	SCF
	RET
Save1:
	LD	DE,Fcb
	XOR	A
	LD	(Fcb+12),A
	LD	(Fcb+32),A
	LD	C,0x13			; borra la version anterior
	CALL	BDOS
	LD	DE,Fcb
	XOR	A
	LD	(Fcb+12),A
	LD	(Fcb+32),A
	LD	C,0x16
	CALL	BDOS
	INC	A
	JP	Z,SaveErr
	LD	DE,IoBuf
	LD	C,0x1A
	CALL	BDOS
	LD	HL,IoBuf
	LD	(IoPtr),HL
	XOR	A
	LD	(IoCnt),A
	LD	(SvLast),A
	LD	HL,(BufBeg)
	LD	(SvPtr),HL
	LD	HL,0
	LD	(SvBytes),HL
SaveLoop:
	LD	HL,(SvPtr)
	LD	DE,(GapS)
	LD	A,L
	CP	E
	JR	NZ,SaveNoJump
	LD	A,H
	CP	D
	JR	NZ,SaveNoJump
	LD	HL,(GapE)
SaveNoJump:
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,SaveByte
	LD	A,H
	CP	D
	JR	Z,SaveEnd
SaveByte:
	LD	A,(HL)
	INC	HL
	LD	(SvPtr),HL
	LD	(SvLast),A
	CP	LF
	JR	NZ,SaveCh
	LD	A,CR
	CALL	SvEmit
	JP	C,SaveErr
	LD	A,LF
SaveCh:
	CALL	SvEmit
	JP	C,SaveErr
	JR	SaveLoop
SaveEnd:
	LD	A,(SvLast)		; si el texto no termina en LF, se agrega
	OR	A
	JR	Z,SavePad
	CP	LF
	JR	Z,SavePad
	LD	A,CR
	CALL	SvEmit
	JP	C,SaveErr
	LD	A,LF
	CALL	SvEmit
	JP	C,SaveErr
SavePad:
	LD	A,(IoCnt)
	OR	A
	JR	Z,SaveClose
	LD	A,EOFCH
	CALL	SvEmit
	JP	C,SaveErr
	JR	SavePad
SaveClose:
	LD	DE,Fcb
	LD	C,0x10
	CALL	BDOS
	INC	A
	JP	Z,SaveErr
	XOR	A
	LD	(Dirty),A
	LD	DE,MsgBuf
	CALL	PutName
	LD	HL,TxtWritten
	CALL	PutStr
	XOR	A
	LD	(DE),A
	LD	HL,MsgBuf
	LD	(MsgPtr),HL
	OR	A
	RET
SaveErr:
	LD	HL,MsgDiskErr
	LD	(MsgPtr),HL
	SCF
	RET

; SvEmit: A al bufer de E/S; graba el registro cuando se llena. Carry si fallo
SvEmit:
	LD	HL,(IoPtr)
	LD	(HL),A
	INC	HL
	LD	(IoPtr),HL
	LD	A,(IoCnt)
	INC	A
	LD	(IoCnt),A
	CP	128
	JR	Z,SvFlush
	OR	A
	RET
SvFlush:
	XOR	A
	LD	(IoCnt),A
	LD	HL,IoBuf
	LD	(IoPtr),HL
	LD	DE,Fcb
	LD	C,0x15
	CALL	BDOS
	OR	A
	RET	Z
	SCF
	RET

Quit:
	XOR	A
	LD	IX,CLS
	CALL	Bios
	LD	A,(OldWidth)
	LD	(LINL40),A
	LD	HL,0x0101
	LD	IX,POSIT
	CALL	Bios
	JP	0

; ----------------------------------------------------------------------------
; Pantalla

Redraw:
	CALL	Layout
	CALL	DrawStatus
	CALL	PutCursor
	LD	HL,Scr
	LD	DE,0
	LD	BC,ROWS*COLS+COLS
	JP	VWrite

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

; VRead8: los 8 bytes de VRAM en HL van a CurPat
VRead8:
	DI
	LD	A,(VPortW)
	LD	C,A
	INC	C
	LD	A,L
	OUT	(C),A
	LD	A,H
	OUT	(C),A
	LD	A,(VPortR)
	LD	C,A
	LD	HL,CurPat
	LD	B,8
	NOP
	NOP
VRead81:
	IN	A,(C)
	LD	(HL),A
	INC	HL
	DJNZ	VRead81
	EI
	RET

; RowAddr: A = fila -> HL = Scr + fila * 40
RowAddr:
	LD	L,A
	LD	H,0
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL			; x8
	LD	B,H
	LD	C,L
	ADD	HL,HL
	ADD	HL,HL			; x32
	ADD	HL,BC			; x40
	LD	BC,Scr
	ADD	HL,BC
	RET

; PosToAddr: HL = posicion logica -> HL = direccion
PosToAddr:
	PUSH	HL
	CALL	PreLen
	POP	DE			; DE = posicion
	LD	(Tmp16),HL		; pre
	EX	DE,HL			; HL = posicion, DE = pre
	OR	A
	SBC	HL,DE			; posicion - pre
	JR	Z,PosToAddr0		; justo en el cursor: la direccion es GapS
	JR	NC,PosToAddr1
PosToAddr0:
	ADD	HL,DE			; posicion
	LD	DE,(BufBeg)
	ADD	HL,DE
	RET
PosToAddr1:
	LD	DE,(GapE)
	ADD	HL,DE
	RET

; AddrToPos: HL = direccion -> HL = posicion logica (la direccion no cae en el hueco)
AddrToPos:
	LD	DE,(GapS)
	OR	A
	SBC	HL,DE
	JR	NC,AddrToPos1
	ADD	HL,DE
	LD	DE,(BufBeg)
	OR	A
	SBC	HL,DE
	RET
AddrToPos1:
	; direccion >= GapS: esta en la parte de despues
	LD	DE,(GapS)
	ADD	HL,DE			; de nuevo la direccion
	LD	DE,(GapE)
	OR	A
	SBC	HL,DE
	PUSH	HL
	CALL	PreLen
	POP	DE
	ADD	HL,DE
	RET

; GapJump: si HL esta en el hueco, salta a su final
GapJump:
	LD	DE,(GapS)
	LD	A,L
	CP	E
	RET	NZ
	LD	A,H
	CP	D
	RET	NZ
	LD	HL,(GapE)
	RET

; BackToLineStart: HL = direccion; vuelve al comienzo de su linea
BackToLineStart:
	LD	DE,(BufBeg)
BackLine1:
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	RET	Z
	DEC	HL
	LD	A,(HL)
	CP	LF
	JR	NZ,BackLine1
	INC	HL
	RET

; LineStartPos: HL = posicion logica del comienzo de la linea del cursor
LineStartPos:
	LD	HL,(GapS)
	CALL	BackToLineStart
	LD	DE,(BufBeg)
	OR	A
	SBC	HL,DE
	RET

; FixTop: Top no pasa del cursor
FixTop:
	CALL	PreLen
	LD	DE,(Top)
	OR	A
	SBC	HL,DE
	RET	NC
	CALL	LineStartPos
	LD	(Top),HL
	RET

; CenterTop: Top diez lineas arriba de la del cursor
CenterTop:
	LD	HL,(GapS)
	CALL	BackToLineStart
	LD	B,10
CenterTop1:
	LD	DE,(BufBeg)
	OR	A
	SBC	HL,DE
	ADD	HL,DE
	JR	Z,CenterTop9
	DEC	HL
	CALL	BackToLineStart
	DJNZ	CenterTop1
CenterTop9:
	LD	DE,(BufBeg)
	OR	A
	SBC	HL,DE
	LD	(Top),HL
	RET

; AdvanceTop: Top una linea mas abajo
AdvanceTop:
	LD	HL,(Top)
	CALL	PosToAddr
AdvTop1:
	CALL	GapJump
	LD	DE,(BufEnd)
	LD	A,L
	CP	E
	JR	NZ,AdvTop2
	LD	A,H
	CP	D
	RET	Z			; no hay mas texto
AdvTop2:
	LD	A,(HL)
	INC	HL
	CP	LF
	JR	NZ,AdvTop1
	CALL	GapJump
	CALL	AddrToPos
	LD	(Top),HL
	RET

; Layout: arma las filas de texto en Scr y ubica el cursor
Layout:
	XOR	A
	LD	(Tries),A
LayTry:
	CALL	FixTop
	CALL	LayoutPass
	LD	A,(CurRow)
	CP	ROWS
	RET	C			; el cursor se ve
	LD	A,(Tries)
	INC	A
	LD	(Tries),A
	CP	40
	JR	NC,LayGiveUp
	CP	4
	JR	C,LayAdv
	JR	NZ,LayAdv
	CALL	CenterTop
	JR	LayTry
LayAdv:
	CALL	AdvanceTop
	JR	LayTry
LayGiveUp:
	LD	A,ROWS-1
	LD	(CurRow),A
	XOR	A
	LD	(CurCol),A
	RET

; LayoutPass: HL = texto, DE = celda de Scr
LayoutPass:
	LD	HL,Scr
	LD	DE,Scr+1
	LD	BC,ROWS*COLS+COLS-1
	LD	(HL),' '
	LDIR
	XOR	A
	LD	(LRow),A
	LD	(LCol),A
	LD	A,0xFF
	LD	(CurRow),A
	LD	HL,(Top)
	CALL	PosToAddr
	LD	DE,Scr
LayLoop:
	LD	BC,(GapS)
	LD	A,L
	CP	C
	JR	NZ,LayNoCur
	LD	A,H
	CP	B
	JR	NZ,LayNoCur
	; el cursor esta aca
	LD	A,(LCol)
	CP	COLS
	JR	C,LayCur1
	; la fila esta llena: el cursor va al comienzo de la siguiente
	CALL	NewLine
	JR	C,LayDone
LayCur1:
	LD	A,(LRow)
	LD	(CurRow),A
	LD	A,(LCol)
	LD	(CurCol),A
	LD	HL,(GapE)
LayNoCur:
	LD	BC,(BufEnd)
	LD	A,L
	CP	C
	JR	NZ,LayChar
	LD	A,H
	CP	B
	JR	Z,LayEnd
LayChar:
	LD	A,(HL)
	INC	HL
	CP	LF
	JR	Z,LayLF
	CP	9
	JR	Z,LayTab
	CALL	EmitChar
	JR	C,LayDone
	JR	LayLoop
LayLF:
	CALL	NewLine
	JR	C,LayDone
	JR	LayLoop
LayTab:
	LD	A,' '
	CALL	EmitChar
	JR	C,LayDone
	LD	A,(LCol)
	AND	7
	JR	NZ,LayTab
	JR	LayLoop
LayEnd:
	; fin del texto: el resto son filas con "~"
	LD	A,(LCol)
	LD	B,A
	LD	A,(LRow)
	OR	A
	JR	NZ,LayEnd1
	LD	A,B
	OR	A
	JR	NZ,LayEnd1
	LD	A,(CurRow)
	OR	A
	JR	Z,LayEnd2		; texto vacio: la fila 0 es la linea vacia
LayEnd1:
	LD	A,B
	OR	A
	LD	A,(LRow)
	JR	Z,LayEnd3
LayEnd2:
	INC	A
LayEnd3:
	; si el cursor esta en la fila A (columna 0), esa fila es una linea vacia
	LD	B,A
	LD	A,(CurRow)
	CP	B
	LD	A,B
	JR	NZ,LayTilde
	LD	A,(CurCol)
	OR	A
	LD	A,B
	JR	NZ,LayTilde
	INC	A
LayTilde:
	CP	ROWS
	JR	NC,LayDone
	PUSH	AF
	CALL	RowAddr
	LD	(HL),'~'
	POP	AF
	INC	A
	JR	LayTilde
LayDone:
	RET

; NewLine: siguiente fila. Carry si la pantalla se lleno. Mueve DE
NewLine:
	LD	A,(LRow)
	INC	A
	LD	(LRow),A
	XOR	A
	LD	(LCol),A
	LD	A,(LRow)
	CP	ROWS
	CCF
	RET	C
	PUSH	HL
	CALL	RowAddr
	EX	DE,HL
	POP	HL
	OR	A
	RET

; EmitChar: A en la celda actual. Carry si la pantalla se lleno
EmitChar:
	LD	B,A
	LD	A,(LCol)
	CP	COLS
	JR	C,EmitChar1
	PUSH	BC
	CALL	NewLine
	POP	BC
	RET	C
EmitChar1:
	LD	A,B
	CP	32
	JR	C,EmitDot
	CP	127
	JR	NZ,EmitPut
EmitDot:
	LD	A,'.'
EmitPut:
	LD	(DE),A
	INC	DE
	LD	A,(LCol)
	INC	A
	LD	(LCol),A
	OR	A
	RET

; DrawStatus: la ultima fila
DrawStatus:
	LD	A,ROWS
	CALL	RowAddr
	EX	DE,HL
	LD	A,(CmdActive)
	OR	A
	JR	Z,DrawMsg
	LD	A,(Prompt)
	LD	(DE),A
	INC	DE
	LD	A,(CmdLen)
	OR	A
	RET	Z
	LD	B,A
	LD	HL,CmdBuf
DrawCmd1:
	LD	A,(HL)
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	DrawCmd1
	RET
DrawMsg:
	LD	HL,(MsgPtr)
	LD	A,H
	OR	L
	JR	Z,DrawMode
	LD	B,COLS
DrawMsg1:
	LD	A,(HL)
	OR	A
	RET	Z
	LD	(DE),A
	INC	HL
	INC	DE
	DJNZ	DrawMsg1
	RET
DrawMode:
	LD	A,(InsMode)
	OR	A
	RET	Z
	LD	HL,TxtInsert
	JP	PutStr

; PutCursor: dibuja el cursor como un caracter con el dibujo invertido
PutCursor:
	LD	A,(CmdActive)
	OR	A
	JR	Z,PutCur1
	LD	A,(CmdLen)
	INC	A
	LD	E,A
	LD	D,0
	LD	A,ROWS
	CALL	RowAddr
	ADD	HL,DE
	JR	PutCur2
PutCur1:
	LD	A,(CurRow)
	CALL	RowAddr
	LD	A,(CurCol)
	LD	E,A
	LD	D,0
	ADD	HL,DE
PutCur2:
	LD	A,(HL)
	LD	(HL),CURCHAR
	LD	L,A
	LD	H,0
	ADD	HL,HL
	ADD	HL,HL
	ADD	HL,HL
	LD	DE,0x0800
	ADD	HL,DE			; el dibujo del caracter, en la VRAM
	CALL	VRead8
	LD	HL,CurPat
	LD	B,8
PutCur3:
	LD	A,(HL)
	CPL
	LD	(HL),A
	INC	HL
	DJNZ	PutCur3
	LD	HL,CurPat
	LD	DE,0x0800+CURCHAR*8
	LD	BC,8
	JP	VWrite

; ----------------------------------------------------------------------------
; Textos y variables

TxtInsert:	DB	"-- INSERT --",0
TxtNew:		DB	" [New File]",0
TxtWritten:	DB	" written",0
TxtNoName:	DB	"[No Name]",0
TxtMod:		DB	" [Modified]",0
TxtLine:	DB	" line ",0
TxtOf:		DB	" of ",0
MsgFull:	DB	"Buffer full",0
MsgNoPat:	DB	"No previous search",0
MsgNotFound:	DB	"Pattern not found",0
MsgBadCmd:	DB	"Not an editor command",0
MsgNoWrite:	DB	"No write since last change (:q!)",0
MsgNoName:	DB	"No file name",0
MsgTooBig:	DB	"File too large: read-only",0
MsgRO:		DB	"Read-only: the file was truncated",0
MsgDiskErr:	DB	"Disk error",0

BufBeg:		DW	0
BufEnd:		DW	0
GapS:		DW	0
GapE:		DW	0
Top:		DW	0
Tmp16:		DW	0
MsgPtr:		DW	0
YPtr:		DW	0
YLen:		DW	0
IoPtr:		DW	0
SvPtr:		DW	0
SvBytes:	DW	0
SrchOrig:	DW	0
OldWidth:	DB	0
VPortR:		DB	0x98
VPortW:		DB	0x98
InsMode:	DB	0
Dirty:		DB	0
ReadOnly:	DB	0
Count:		DB	0
Pending:	DB	0
DesCol:		DB	0
SaveCol:	DB	0
Rep:		DB	0
CmdActive:	DB	0
CmdLen:		DB	0
Prompt:		DB	':'
SLen:		DB	0
IoCnt:		DB	0
SvLast:		DB	0
Tries:		DB	0
PdFlag:		DB	0
NameGiven:	DB	0
DirtyBak:	DB	0
PnFcb:		DW	0
LRow:		DB	0
LCol:		DB	0
CurRow:		DB	0
CurCol:		DB	0
CurPat:		DS	8,0
CmdBuf:		DS	COLS,0
SBuf:		DS	COLS,0
MsgBuf:		DS	80,0
Fcb:		DS	40,0
FcbBak:		DS	40,0
IoBuf:		DS	128,0
Scr:		DS	ROWS*COLS+COLS,' '
YankBuf:	DS	YANKMAX,0

TextArea:
