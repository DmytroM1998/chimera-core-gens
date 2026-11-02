bits 32

section .text
global M68K_RB
global M68K_RW
global M68K_WB
global M68K_WW
global Int_Ack

extern ref_bus_read_byte
extern ref_bus_read_word
extern ref_bus_write_byte
extern ref_bus_write_word
extern ref_irq_ack_count
extern ref_irq_last_level

M68K_RB:
    push ecx
    push edx
    push dword [esp + 12]
    call ref_bus_read_byte
    add esp, 4
    pop edx
    pop ecx
    ret

M68K_RW:
    push ecx
    push edx
    push dword [esp + 12]
    call ref_bus_read_word
    add esp, 4
    pop edx
    pop ecx
    ret

M68K_WB:
    push ecx
    push edx
    push dword [esp + 16]
    push dword [esp + 16]
    call ref_bus_write_byte
    add esp, 8
    pop edx
    pop ecx
    ret

M68K_WW:
    push ecx
    push edx
    push dword [esp + 16]
    push dword [esp + 16]
    call ref_bus_write_word
    add esp, 8
    pop edx
    pop ecx
    ret

Int_Ack:
    movzx eax, dl
    mov [ref_irq_last_level], eax
    inc dword [ref_irq_ack_count]
    xor eax, eax
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
