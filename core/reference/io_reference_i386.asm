bits 32

extern RD_Controller_1
extern RD_Controller_2

section .text
global reference_read_controller_1
reference_read_controller_1:
    push ebx
    call RD_Controller_1
    pop ebx
    ret

global reference_read_controller_2
reference_read_controller_2:
    push ebx
    call RD_Controller_2
    pop ebx
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
