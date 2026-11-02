#include "gens_vdp_registers.h"

static const uint32_t horizontal_mask[4] = {0, 7, 0x1f8, 0x1ff};
static const uint8_t horizontal_scroll_multiplier[4] = {5, 6, 6, 7};
static const uint32_t horizontal_scroll_mask[4] = {31, 63, 63, 127};
static const uint32_t vertical_scroll_mask[4][4] = {
    {31, 31, 0, 31},
    {63, 63, 0, 31},
    {31, 31, 0, 31},
    {127, 63, 0, 31}
};

int gens_vdp_registers_valid(const GensVdpRegisters *vdp)
{
    return vdp != 0 && vdp->registers != 0 && vdp->register_count >= 24u &&
        vdp->vram != 0 && vdp->vram_size >= 65536u &&
        vdp->vram_flag != 0 && vdp->cram_flag != 0 && vdp->vscroll_mmask != 0 &&
        vdp->scr_a_addr != 0 && vdp->scr_b_addr != 0 && vdp->window_addr != 0 &&
        vdp->sprite_addr != 0 && vdp->hscroll_addr != 0 && vdp->hcell != 0 &&
        vdp->hwin_mul != 0 && vdp->hpix != 0 && vdp->hpix_begin != 0 &&
        vdp->hscroll_mask != 0 && vdp->hscroll_cmul != 0 &&
        vdp->hscroll_cmask != 0 && vdp->vscroll_cmask != 0 &&
        vdp->window_x != 0 && vdp->window_y != 0 && vdp->dma_length != 0 &&
        vdp->dma_address != 0 && vdp->control_dma_mode != 0;
}

static uint8_t *vram_address(GensVdpRegisters *vdp, uint32_t offset)
{
    return vdp->vram + (offset & UINT32_C(0xffff));
}

static void set_register_byte(GensVdpRegisters *vdp, unsigned int reg, uint8_t value)
{
    vdp->registers[reg] = (vdp->registers[reg] & UINT32_C(0xffffff00)) | value;
}

static void set_window_address(GensVdpRegisters *vdp, uint32_t mask)
{
    *vdp->window_addr = vram_address(vdp, (vdp->registers[3] & mask) << 10);
}

static void set_sprite_address(GensVdpRegisters *vdp, uint32_t mask)
{
    *vdp->sprite_addr = vram_address(vdp, (vdp->registers[5] & mask) << 9);
}

static void set_mode_4(GensVdpRegisters *vdp, uint8_t value)
{
    uint32_t cells, window_x;
    set_register_byte(vdp, 12, value);
    *vdp->cram_flag = 1;
    if ((value & 0x81u) != 0u)
    {
        *vdp->hcell = 40;
        *vdp->hwin_mul = 6;
        *vdp->hpix = 320;
        *vdp->hpix_begin = 0;
    }
    else
    {
        *vdp->hcell = 32;
        *vdp->hwin_mul = 5;
        *vdp->hpix = 256;
        *vdp->hpix_begin = 32;
    }

    set_window_address(vdp, (value & 0x81u) != 0u ? 0x3cu : 0x3eu);
    cells = *vdp->hcell;
    window_x = (vdp->registers[17] & 0x1fu) * 2u;
    *vdp->window_x = (*vdp->window_x & UINT32_C(0xffffff00)) |
        (window_x > cells ? cells : window_x);
    set_sprite_address(vdp, (value & 0x81u) != 0u ? 0x7eu : 0x7fu);
}

static void set_screen_size(GensVdpRegisters *vdp, uint8_t value)
{
    unsigned int vertical = (value >> 4) & 3u;
    unsigned int horizontal = value & 3u;
    set_register_byte(vdp, 16, value);
    *vdp->hscroll_cmul = horizontal_scroll_multiplier[horizontal];
    *vdp->hscroll_cmask = horizontal_scroll_mask[horizontal];
    *vdp->vscroll_cmask = vertical_scroll_mask[vertical][horizontal];
}

int gens_vdp_set_register(GensVdpRegisters *vdp, GensM68kBusState *state,
                          unsigned int reg, uint8_t value)
{
    uint32_t derived;

    if (!gens_vdp_registers_valid(vdp))
        return 0;
    reg &= 31u;

    switch (reg)
    {
    case 0:
    case 1:
        set_register_byte(vdp, reg, value);
        return vdp->update_irq != 0 && state != 0 &&
            vdp->update_irq(vdp->opaque, state);
    case 2:
        set_register_byte(vdp, 2, value);
        *vdp->scr_a_addr = vram_address(vdp, (value & 0x38u) << 10);
        return 1;
    case 3:
        set_register_byte(vdp, 3, value);
        set_window_address(vdp, (vdp->registers[12] & 1u) != 0u ? 0x3cu : 0x3eu);
        return 1;
    case 4:
        set_register_byte(vdp, 4, value);
        *vdp->scr_b_addr = vram_address(vdp, (value & 7u) << 13);
        return 1;
    case 5:
        set_register_byte(vdp, 5, value);
        *vdp->vram_flag |= 2u;
        set_sprite_address(vdp, (vdp->registers[12] & 1u) != 0u ? 0x7eu : 0x7fu);
        return 1;
    case 7:
        *vdp->cram_flag = 1;
        vdp->registers[7] = value & 0x3fu;
        return 1;
    case 11:
        set_register_byte(vdp, 11, value);
        *vdp->hscroll_mask = horizontal_mask[value & 3u];
        *vdp->vscroll_mmask = (value & 4u) != 0u ? 0x7e : 0;
        return 1;
    case 12:
        set_mode_4(vdp, value);
        return 1;
    case 13:
        set_register_byte(vdp, 13, value);
        *vdp->hscroll_addr = vram_address(vdp, (value & 0x3fu) << 10);
        return 1;
    case 16:
        set_screen_size(vdp, value);
        return 1;
    case 17:
        set_register_byte(vdp, 17, value);
        derived = (value & 0x1fu) * 2u;
        *vdp->window_x = derived > *vdp->hcell ? *vdp->hcell : derived;
        return 1;
    case 18:
        set_register_byte(vdp, 18, value);
        *vdp->window_y = value & 0x1fu;
        return 1;
    case 19:
        set_register_byte(vdp, 19, value);
        *vdp->dma_length = (*vdp->dma_length & UINT32_C(0xffffff00)) | value;
        return 1;
    case 20:
        set_register_byte(vdp, 20, value);
        *vdp->dma_length = (*vdp->dma_length & UINT32_C(0xffff00ff)) |
            (uint32_t)value << 8;
        return 1;
    case 21:
        set_register_byte(vdp, 21, value);
        *vdp->dma_address = (*vdp->dma_address & UINT32_C(0xffffff00)) | value;
        return 1;
    case 22:
        set_register_byte(vdp, 22, value);
        *vdp->dma_address = (*vdp->dma_address & UINT32_C(0xffff00ff)) |
            (uint32_t)value << 8;
        return 1;
    case 23:
        set_register_byte(vdp, 23, value);
        *vdp->dma_address = (*vdp->dma_address & UINT32_C(0xff00ffff)) |
            (uint32_t)(value & 0x7fu) << 16;
        *vdp->control_dma_mode = value & 0xc0u;
        return 1;
    default:
        if (reg < 24u)
            set_register_byte(vdp, reg, value);
        return 1;
    }
}

int gens_vdp_register_callback(void *opaque, GensM68kBusState *state,
                               unsigned int reg, uint8_t value)
{
    return gens_vdp_set_register((GensVdpRegisters *)opaque, state, reg, value);
}
