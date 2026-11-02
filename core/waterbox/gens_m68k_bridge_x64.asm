bits 64
default rel

extern gens_m68k_read
extern gens_m68k_write

%define LIVE_PC       0
%define LIVE_BASE     8
%define LIVE_CYCLES  16
%define LIVE_ADDRESS 20
%define RESULT       24
%define STATUS       28
%define SAVE_RAX     32
%define SAVE_RBX     40
%define SAVE_RCX     48
%define SAVE_R8      56
%define SAVE_R9      64
%define SAVE_R11     72
%define SAVE_R12     80
%define SAVE_R13     88
%define SAVE_R14     96
%define FRAME_SIZE  112

%macro BRIDGE 4
global gens_m68k_bridge_%1
gens_m68k_bridge_%1:
    push r15
    mov r15, rsp
    and rsp, -16
    sub rsp, FRAME_SIZE

    mov [rsp + LIVE_PC], rsi
    mov [rsp + LIVE_BASE], rbp
    mov [rsp + LIVE_CYCLES], edi
    mov [rsp + LIVE_ADDRESS], edx
    mov [rsp + SAVE_RAX], rax
    mov [rsp + SAVE_RBX], rbx
    mov [rsp + SAVE_RCX], rcx
    mov [rsp + SAVE_R8], r8
    mov [rsp + SAVE_R9], r9
    mov [rsp + SAVE_R11], r11
    mov [rsp + SAVE_R12], r12
    mov [rsp + SAVE_R13], r13
    mov [rsp + SAVE_R14], r14

    mov rdi, r12
    lea rsi, [rsp + LIVE_PC]
    mov edx, %2
    mov ecx, %3
%if %4 = 0
    lea r8, [rsp + RESULT]
    mov rax, gens_m68k_read
%else
    mov r8d, [rsp + SAVE_RCX]
    mov rax, gens_m68k_write
%endif
    call rax
    mov [rsp + STATUS], eax

    mov rax, [rsp + SAVE_RAX]
    mov rbx, [rsp + SAVE_RBX]
    mov rcx, [rsp + SAVE_RCX]
%if %4 = 0
    cmp dword [rsp + STATUS], 0
    jne %%restore
    mov ecx, [rsp + RESULT]
%%restore:
%endif
    mov r8, [rsp + SAVE_R8]
    mov r9, [rsp + SAVE_R9]
    mov r11, [rsp + SAVE_R11]
    mov r12, [rsp + SAVE_R12]
    mov r13, [rsp + SAVE_R13]
    mov r14, [rsp + SAVE_R14]
    mov rsi, [rsp + LIVE_PC]
    mov rbp, [rsp + LIVE_BASE]
    mov edi, [rsp + LIVE_CYCLES]
    mov edx, [rsp + LIVE_ADDRESS]
    mov r10d, [rsp + STATUS]
    mov rsp, r15
    pop r15
    test r10d, r10d
    jz %%success
    stc
    ret
%%success:
    clc
    ret
%endmacro

section .text
BRIDGE read_byte, 1, 0, 0
BRIDGE read_byte_predecrement, 1, 1, 0
BRIDGE read_word, 2, 0, 0
BRIDGE read_word_predecrement, 2, 1, 0
BRIDGE read_long, 4, 0, 0
BRIDGE read_long_predecrement, 4, 1, 0
BRIDGE write_byte, 1, 0, 1
BRIDGE write_byte_predecrement, 1, 1, 1
BRIDGE write_word, 2, 0, 1
BRIDGE write_word_predecrement, 2, 1, 1
BRIDGE write_long, 4, 0, 1
BRIDGE write_long_predecrement, 4, 1, 1

section .note.GNU-stack noalloc noexec nowrite progbits
