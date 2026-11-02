; i386 glue for test_m68k_diff.c.
; Gens' M68K_RB/RW/WB/WW handlers preserve ECX and EDX, and Starscream relies on ECX surviving a
; read (its long reads build the result across two calls). These stubs give the C test callbacks
; the same register behavior. Int_Ack receives the level in DL and returns the new level in AL.
section .text
bits 32

extern ref_rb_c
extern ref_rw_c
extern ref_wb_c
extern ref_ww_c
extern int_ack_c
extern _main68k_exec

; ECX's upper half enters main68k_exec from the caller and can reach hook values. Use zero, as the port does.
global ref_exec_zero_ecx
ref_exec_zero_ecx:
	xor ecx, ecx
	jmp _main68k_exec

global M68K_RB
M68K_RB:
	push ecx
	push edx
	push dword [esp + 12]
	call ref_rb_c
	add esp, 4
	pop edx
	pop ecx
	ret

global M68K_RW
M68K_RW:
	push ecx
	push edx
	push dword [esp + 12]
	call ref_rw_c
	add esp, 4
	pop edx
	pop ecx
	ret

global M68K_WB
M68K_WB:
	push ecx
	push edx
	push dword [esp + 16]
	push dword [esp + 16]
	call ref_wb_c
	add esp, 8
	pop edx
	pop ecx
	ret

global M68K_WW
M68K_WW:
	push ecx
	push edx
	push dword [esp + 16]
	push dword [esp + 16]
	call ref_ww_c
	add esp, 8
	pop edx
	pop ecx
	ret

global Int_Ack
Int_Ack:
	push ecx
	push edx
	movzx eax, dl
	push eax
	call int_ack_c
	add esp, 4
	pop edx
	pop ecx
	ret
