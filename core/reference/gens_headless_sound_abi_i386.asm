; Register-preserving entry points for the C sound chip functions that Gens' assembly calls.
; The second YM2612 write of a Z80 word write reuses the ECX and EDX left by the first call.
; Those values depend on the compiler that built the original binary. The port models them
; as preserved, and these wrappers give the reference the same behavior.
section .text
bits 32

extern _YM2612_Write_Traced
extern _YM2612_Read_C
extern _PSG_Write_C

global _YM2612_Write
_YM2612_Write:
	push ecx
	push edx
	push dword [esp + 16]
	push dword [esp + 16]
	call _YM2612_Write_Traced
	add esp, 8
	pop edx
	pop ecx
	ret

global _YM2612_Read
_YM2612_Read:
	push ecx
	push edx
	call _YM2612_Read_C
	pop edx
	pop ecx
	ret

global _PSG_Write
_PSG_Write:
	push ecx
	push edx
	push dword [esp + 12]
	call _PSG_Write_C
	add esp, 4
	pop edx
	pop ecx
	ret
