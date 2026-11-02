; i386 stubs that preserve ECX around the C test handlers of test_z80_diff.c.
; The port assumes a handler leaves ECX unchanged unless it reports otherwise.
section .text
bits 32

%macro THUNK 1
	extern %1_c
	global %1_thunk
%1_thunk:
	push ecx
	push edx
	call %1_c
	pop edx
	pop ecx
	ret
%endmacro

THUNK ref_rb
THUNK ref_rw
THUNK ref_wb
THUNK ref_ww
