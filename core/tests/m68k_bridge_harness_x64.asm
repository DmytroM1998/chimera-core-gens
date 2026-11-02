bits 64
default rel

section .data
global bridge_bad_stack_alignment
bridge_bad_stack_alignment: dd 0

section .text
%macro CHECK_STACK 1
extern __real_gens_m68k_%1
global __wrap_gens_m68k_%1
__wrap_gens_m68k_%1:
    mov rax, rsp
    and eax, 15
    cmp eax, 8
    je %%aligned
    mov dword [rel bridge_bad_stack_alignment], 1
%%aligned:
    mov rax, __real_gens_m68k_%1
    jmp rax
%endmacro

CHECK_STACK read
CHECK_STACK write

global run_m68k_bridge
run_m68k_bridge:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    sub rsp, 40
    mov [rsp], rsi
    mov [rsp + 8], rdx
    mov [rsp + 16], rcx
    mov [rsp + 24], rdi
    mov r11, rsi
    mov rax, [r11]
    mov rbx, [r11 + 8]
    mov rcx, [r11 + 16]
    mov rdx, [r11 + 24]
    mov rbp, [r11 + 40]
    mov rdi, [r11 + 48]
    mov r8, [r11 + 56]
    mov r9, [r11 + 64]
    mov r12, [rsp + 24]
    mov r13, [r11 + 88]
    mov r14, [r11 + 96]
    mov r15, [r11 + 104]
    mov rsi, [r11 + 32]
    mov r11, [r11 + 72]
    cmp qword [rsp + 16], 0
    jne .skew
    call qword [rsp + 8]
    jmp .record
.skew:
    lea rsp, [rsp - 8]
    call qword [rsp + 16]
    lea rsp, [rsp + 8]
.record:
    setc byte [rsp + 32]
    mov [rsp + 24], r11
    mov r11, [rsp]
    mov [r11], rax
    mov [r11 + 8], rbx
    mov [r11 + 16], rcx
    mov [r11 + 24], rdx
    mov [r11 + 32], rsi
    mov [r11 + 40], rbp
    mov [r11 + 48], rdi
    mov [r11 + 56], r8
    mov [r11 + 64], r9
    mov [r11 + 80], r12
    mov [r11 + 88], r13
    mov [r11 + 96], r14
    mov [r11 + 104], r15
    mov [r11 + 112], r10
    movzx eax, byte [rsp + 32]
    mov [r11 + 120], rax
    mov rax, [rsp + 24]
    mov [r11 + 72], rax
    add rsp, 40
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
