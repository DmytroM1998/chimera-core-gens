bits 32

extern Z80_ReadB_Table, Z80_ReadW_Table, Z80_WriteB_Table, Z80_WriteW_Table
extern reference_native_backend

section .text
global reference_z80_native
reference_z80_native:
    push ebx
    push esi
    push edi
    push ebp
    mov edi, [esp + 20]
    mov esi, Z80_ReadB_Table
    cmp dword [esp + 28], 0
    jne .write
    cmp dword [esp + 24], 2
    jne .dispatch
    mov esi, Z80_ReadW_Table
    jmp .dispatch
.write:
    mov esi, Z80_WriteB_Table
    cmp dword [esp + 24], 2
    jne .dispatch
    mov esi, Z80_WriteW_Table
.dispatch:
    mov ecx, [edi + 4]
    mov ebx, ecx
    and ebx, 0xf000
    shr ebx, 10
    mov esi, [esi + ebx]
    mov eax, [edi]
    mov edx, [edi + 8]
    call esi
    mov [edi], eax
    mov [edi + 4], ecx
    mov [edi + 8], edx
    pop ebp
    pop edi
    pop esi
    pop ebx
    ret

%macro RAW_BACKEND 2
global %1
%1:
    push ebp
    mov ebp, esp
    sub esp, 12
    mov [ebp - 12], eax
    mov [ebp - 8], ecx
    mov [ebp - 4], edx
    lea eax, [ebp - 12]
    push eax
    push dword %2
    call reference_native_backend
    add esp, 8
    mov eax, [ebp - 12]
    mov ecx, [ebp - 8]
    mov edx, [ebp - 4]
    mov esp, ebp
    pop ebp
    ret
%endmacro

RAW_BACKEND _YM2612_Read, 0
RAW_BACKEND _YM2612_Write, 1
RAW_BACKEND Read_VDP_Status, 2
RAW_BACKEND Read_VDP_V_Counter, 3
RAW_BACKEND Read_VDP_H_Counter, 4
RAW_BACKEND Write_Byte_VDP_Data, 5
RAW_BACKEND Write_Word_VDP_Data, 6
RAW_BACKEND _PSG_Write, 7
RAW_BACKEND M68K_RB, 8
RAW_BACKEND M68K_WB, 9

section .note.GNU-stack noalloc noexec nowrite progbits
