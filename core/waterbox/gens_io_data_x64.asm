bits 64

%macro VARIABLE 2
global %1
global _%1
_%1:
%1: dd %2
%endmacro

%macro PAD 1
VARIABLE Controller_%1_Type, 0
VARIABLE Controller_%1_Up, 1
VARIABLE Controller_%1_Down, 1
VARIABLE Controller_%1_Left, 1
VARIABLE Controller_%1_Right, 1
VARIABLE Controller_%1_Start, 1
VARIABLE Controller_%1_Mode, 1
VARIABLE Controller_%1_A, 1
VARIABLE Controller_%1_B, 1
VARIABLE Controller_%1_C, 1
VARIABLE Controller_%1_X, 1
VARIABLE Controller_%1_Y, 1
VARIABLE Controller_%1_Z, 1
%endmacro

%macro PORT 1
VARIABLE Controller_%1_State, 0x40
VARIABLE Controller_%1_COM, 0
VARIABLE Controller_%1_Counter, 0
VARIABLE Controller_%1_Delay, 0
PAD %1
%endmacro

section .data align=64
PORT 1
PORT 2
PAD 1B
PAD 1C
PAD 1D
PAD 2B
PAD 2C
PAD 2D
global Lag_Frame
global _Lag_Frame
_Lag_Frame:
Lag_Frame: db 0

section .note.GNU-stack noalloc noexec nowrite progbits
