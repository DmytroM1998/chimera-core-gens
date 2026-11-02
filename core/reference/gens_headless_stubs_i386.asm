; Sega CD, 32X and PWM symbols that the shared Gens memory map and Cpu_68k.c reference.
; The Genesis reference never reaches them. Functions trap if called.
section .text
bits 32
global @PWM_Set_Cycle@4
@PWM_Set_Cycle@4:
	int3
global __32X_Set_FB
__32X_Set_FB:
	int3
global _Read_CDC_Host_MAIN
_Read_CDC_Host_MAIN:
	int3
global _S68K_RB
_S68K_RB:
	int3
global _S68K_RW
_S68K_RW:
	int3
global _S68K_WB
_S68K_WB:
	int3
global _S68K_WW
_S68K_WW:
	int3
global _sub68k_init
_sub68k_init:
	int3
global _sub68k_interrupt
_sub68k_interrupt:
	int3
global _sub68k_reset
_sub68k_reset:
	int3
global SH2_DMA0_Request
SH2_DMA0_Request:
	int3
global SH2_Interrupt
SH2_Interrupt:
	int3
global SH2_Reset
SH2_Reset:
	int3
section .bss
align 64
global __32X_ADEN
__32X_ADEN:
	resb 0x40
global __32X_RV
__32X_RV:
	resb 0x40
global _32X_ADEN
_32X_ADEN:
	resb 0x40
global _32X_Comm
_32X_Comm:
	resb 0x100
global _32X_DREQ_DST
_32X_DREQ_DST:
	resb 0x40
global _32X_DREQ_LEN
_32X_DREQ_LEN:
	resb 0x40
global _32X_DREQ_SRC
_32X_DREQ_SRC:
	resb 0x40
global _32X_DREQ_ST
_32X_DREQ_ST:
	resb 0x40
global _32X_FIFO_A
_32X_FIFO_A:
	resb 0x100
global _32X_FIFO_Block
_32X_FIFO_Block:
	resb 0x40
global _32X_FIFO_Read
_32X_FIFO_Read:
	resb 0x40
global _32X_FIFO_Write
_32X_FIFO_Write:
	resb 0x40
global _32X_FM
_32X_FM:
	resb 0x40
global _32X_MINT
_32X_MINT:
	resb 0x40
global _32X_Palette_16B
_32X_Palette_16B:
	resb 0x10000
global _32X_Palette_32B
_32X_Palette_32B:
	resb 0x20000
global _32X_RES
_32X_RES:
	resb 0x40
global _32X_RV
_32X_RV:
	resb 0x40
global _32X_SINT
_32X_SINT:
	resb 0x40
global _32X_VDP
_32X_VDP:
	resb 0x100
global _32X_VDP_CRam
_32X_VDP_CRam:
	resb 0x200
global _32X_VDP_CRam_Ajusted
_32X_VDP_CRam_Ajusted:
	resb 0x200
global _32X_VDP_CRam_Ajusted32
_32X_VDP_CRam_Ajusted32:
	resb 0x400
global _32X_VDP_Ram
_32X_VDP_Ram:
	resb 0x40000
global _COMM
_COMM:
	resb 0x100
global _Font_BITS
_Font_BITS:
	resb 0x40
global _Font_COLOR
_Font_COLOR:
	resb 0x40
global _Init_Timer_INT3
_Init_Timer_INT3:
	resb 0x40
global _Int_Mask_S68K
_Int_Mask_S68K:
	resb 0x40
global _LED_Status
_LED_Status:
	resb 0x40
global _PWM_Cycle_Tmp
_PWM_Cycle_Tmp:
	resb 0x40
global _PWM_FIFO_L
_PWM_FIFO_L:
	resb 0x100
global _PWM_FIFO_L_Tmp
_PWM_FIFO_L_Tmp:
	resb 0x40
global _PWM_FIFO_R
_PWM_FIFO_R:
	resb 0x100
global _PWM_FIFO_R_Tmp
_PWM_FIFO_R_Tmp:
	resb 0x40
global _PWM_FULL_TAB
_PWM_FULL_TAB:
	resb 0x10000
global _PWM_Mode
_PWM_Mode:
	resb 0x40
global _PWM_RP_L
_PWM_RP_L:
	resb 0x40
global _PWM_RP_R
_PWM_RP_R:
	resb 0x40
global _PWM_WP_L
_PWM_WP_L:
	resb 0x40
global _PWM_WP_R
_PWM_WP_R:
	resb 0x40
global _Ram_Prg
_Ram_Prg:
	resb 0x80000
global _Ram_Word_1M
_Ram_Word_1M:
	resb 0x40000
global _Ram_Word_2M
_Ram_Word_2M:
	resb 0x40000
global _Ram_Word_State
_Ram_Word_State:
	resb 0x40
global _S68K_Mem_PM
_S68K_Mem_PM:
	resb 0x40
global _S68K_Mem_WP
_S68K_Mem_WP:
	resb 0x40
global _sub68k_SetContext
_sub68k_SetContext:
	resb 0x40
global _Timer_INT3
_Timer_INT3:
	resb 0x40
global CDC.RS0
CDC.RS0:
	resb 0x40
global CDC.Stop_Watch
CDC.Stop_Watch:
	resb 0x40
global CDD.Control
CDD.Control:
	resb 0x40
global CDD.Ext
CDD.Ext:
	resb 0x40
global CDD.Frame
CDD.Frame:
	resb 0x40
global CDD.Minute
CDD.Minute:
	resb 0x40
global CDD.Rcv_Status
CDD.Rcv_Status:
	resb 0x40
global CDD.Seconde
CDD.Seconde:
	resb 0x40
global CDD.Status
CDD.Status:
	resb 0x40
global Cell_Conv_Tab
global _Cell_Conv_Tab
Cell_Conv_Tab:
_Cell_Conv_Tab:
	resb 0x20000
global COMM.Command
COMM.Command:
	resb 0x40
global COMM.Flag
COMM.Flag:
	resb 0x40
global COMM.Status
COMM.Status:
	resb 0x40
global Int_Mask_S68K
Int_Mask_S68K:
	resb 0x40
global M_SH2
M_SH2:
	resb 0x1000
global Memory_Control_Status
Memory_Control_Status:
	resb 0x40
global Ram_Prg
Ram_Prg:
	resb 0x80000
global Ram_Word_1M
Ram_Word_1M:
	resb 0x40000
global Ram_Word_2M
Ram_Word_2M:
	resb 0x40000
global Ram_Word_State
Ram_Word_State:
	resb 0x40
global S_SH2
S_SH2:
	resb 0x1000
global S68K_Mem_WP
S68K_Mem_WP:
	resb 0x40
