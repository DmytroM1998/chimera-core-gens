bits 32

section .bss
global M_Z80
M_Z80: resd 1

section .text
global Read_VDP_Data, Read_VDP_Status, Read_VDP_V_Counter, Read_VDP_H_Counter
global Write_Byte_VDP_Data, Write_Word_VDP_Data, Write_VDP_Ctrl
global _main68k_readOdometer, z80_Reset, z80_Exec, z80_Set_Odo
global _YM2612_Reset, _PSG_Write
global _YM2612_Read, _YM2612_Write
extern reference_odometer, reference_z80_event
extern reference_ym_read, reference_ym_write, reference_psg_write
extern reference_vdp_byte_write, reference_vdp_status, reference_v_counter, reference_h_counter
_YM2612_Read:
    jmp reference_ym_read
_YM2612_Write:
    jmp reference_ym_write
_PSG_Write:
    jmp reference_psg_write
Write_Byte_VDP_Data:
    jmp reference_vdp_byte_write
Read_VDP_Status:
    push ecx
    push edx
    call reference_vdp_status
    pop edx
    pop ecx
    ret
Read_VDP_V_Counter:
    push ecx
    push edx
    call reference_v_counter
    pop edx
    pop ecx
    ret
Read_VDP_H_Counter:
    push ecx
    push edx
    call reference_h_counter
    pop edx
    pop ecx
    ret

_main68k_readOdometer:
    jmp reference_odometer

z80_Set_Odo:
    push edx
    push dword 1
    call reference_z80_event
    add esp, 8
    ret

z80_Exec:
    push edx
    push dword 2
    call reference_z80_event
    add esp, 8
    ret

z80_Reset:
    push dword 0
    push dword 3
    call reference_z80_event
    add esp, 8
    ret

_YM2612_Reset:
    push dword 0
    push dword 4
    call reference_z80_event
    add esp, 8
    ret

Read_VDP_Data:
Write_Word_VDP_Data:
Write_VDP_Ctrl:
unexpected_device:
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
