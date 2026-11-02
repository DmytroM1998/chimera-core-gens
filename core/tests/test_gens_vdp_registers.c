#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gens_vdp_registers.h"
#include "gens_vdp_write.h"

static uint32_t regs[24], hcell, hwin_mul, hpix, hpix_begin;
static uint32_t hscroll_mask, hscroll_cmul, hscroll_cmask, vscroll_cmask;
static uint32_t window_x, window_y, dma_length, dma_address, control_dma_mode;
static uint8_t vram[65536], vram_flag, cram_flag, vscroll_mmask;
static uint8_t cram[256], vsram[256];
static int control_flag, control_data, control_access, control_address;
static int control_dma, write_dma_mode, write_status, write_dma_length, write_dma_address;
static int write_vram_flag, write_cram_flag, dmat_length, dmat_type;
static unsigned int write_set2, auto_increment;
static uint8_t *scr_a_addr, *scr_b_addr, *window_addr, *sprite_addr, *hscroll_addr;
static unsigned int irq_calls;
static int irq_result = 1;

static int update_irq(void *opaque, GensM68kBusState *state)
{
    (void)opaque;
    (void)state;
    ++irq_calls;
    return irq_result;
}

static GensVdpRegisters vdp = {
    regs, 24, vram, sizeof(vram), &vram_flag, &cram_flag, &vscroll_mmask,
    &scr_a_addr, &scr_b_addr, &window_addr, &sprite_addr, &hscroll_addr,
    &hcell, &hwin_mul, &hpix, &hpix_begin, &hscroll_mask,
    &hscroll_cmul, &hscroll_cmask, &vscroll_cmask, &window_x, &window_y,
    &dma_length, &dma_address, &control_dma_mode, 0, update_irq
};

static GensVdpWrite write_port = {
    vram, cram, vsram, sizeof(vram), sizeof(cram), sizeof(vsram),
    &control_flag, &control_data, &control_access, &control_address,
    &write_dma_mode, &control_dma, &write_status, &write_dma_length, &write_dma_address,
    &dmat_length, &dmat_type, &write_set2, &auto_increment,
    &write_vram_flag, &write_cram_flag, 0, &vdp, gens_vdp_register_callback, 0
};

int main(void)
{
    GensM68kBusState state;
    GensVdpRegisters invalid = vdp;

    memset(&state, 0, sizeof(state));
    assert(gens_vdp_registers_valid(&vdp));
    invalid.register_count = 23;
    assert(!gens_vdp_registers_valid(&invalid));

    regs[2] = UINT32_C(0x12345678);
    assert(gens_vdp_set_register(&vdp, &state, 2, 0x38));
    assert(regs[2] == UINT32_C(0x12345638) && scr_a_addr == vram + 0xe000);
    assert(gens_vdp_set_register(&vdp, &state, 3, 0x3e));
    assert(window_addr == vram + 0xf800);
    assert(gens_vdp_set_register(&vdp, &state, 4, 7));
    assert(scr_b_addr == vram + 0xe000);

    vram_flag = 1;
    assert(gens_vdp_set_register(&vdp, &state, 5, 0x7f));
    assert(vram_flag == 3 && sprite_addr == vram + 0xfe00);
    regs[7] = UINT32_C(0x12345678);
    assert(gens_vdp_set_register(&vdp, &state, 7, 0xff));
    assert(regs[7] == 0x3f && cram_flag == 1);
    assert(gens_vdp_set_register(&vdp, &state, 11, 0x07));
    assert(hscroll_mask == 0x1ff && vscroll_mmask == 0x7e);
    assert(gens_vdp_set_register(&vdp, &state, 13, 0x7f));
    assert(hscroll_addr == vram + 0xfc00);

    assert(gens_vdp_set_register(&vdp, &state, 12, 0));
    assert(hcell == 32 && hwin_mul == 5 && hpix == 256 && hpix_begin == 32);
    assert(gens_vdp_set_register(&vdp, &state, 17, 0x1f));
    assert(window_x == 32);
    assert(gens_vdp_set_register(&vdp, &state, 12, 0x81));
    assert(hcell == 40 && hwin_mul == 6 && hpix == 320 && hpix_begin == 0);
    assert(window_addr == vram + 0xf000 && sprite_addr == vram + 0xfc00);
    assert(window_x == 40 && cram_flag == 1);
    window_x = UINT32_C(0x123456ab);
    assert(gens_vdp_set_register(&vdp, &state, 12, 0x80));
    assert(window_addr == vram + 0xf000 && sprite_addr == vram + 0xfc00);
    assert(window_x == UINT32_C(0x12345628));
    assert(gens_vdp_set_register(&vdp, &state, 3, 0x3e));
    assert(gens_vdp_set_register(&vdp, &state, 5, 0x7f));
    assert(window_addr == vram + 0xf800 && sprite_addr == vram + 0xfe00);

    assert(gens_vdp_set_register(&vdp, &state, 18, 0xff));
    assert(window_y == 31);
    assert(gens_vdp_set_register(&vdp, &state, 16, 0x31));
    assert(hscroll_cmul == 6 && hscroll_cmask == 63 && vscroll_cmask == 63);
    assert(gens_vdp_set_register(&vdp, &state, 16, 0x22));
    assert(hscroll_cmul == 6 && hscroll_cmask == 63 && vscroll_cmask == 0);
    assert(gens_vdp_set_register(&vdp, &state, 16, 0x33));
    assert(hscroll_cmul == 7 && hscroll_cmask == 127 && vscroll_cmask == 31);
    assert(gens_vdp_set_register(&vdp, &state, 16, 0x13));
    assert(hscroll_cmul == 7 && hscroll_cmask == 127 && vscroll_cmask == 31);

    dma_length = UINT32_C(0xaabbccdd);
    assert(gens_vdp_set_register(&vdp, &state, 19, 0x34));
    assert((uint32_t)dma_length == UINT32_C(0xaabbcc34));
    assert(gens_vdp_set_register(&vdp, &state, 20, 0x12));
    assert((uint32_t)dma_length == UINT32_C(0xaabb1234));
    dma_address = UINT32_C(0x87654321);
    assert(gens_vdp_set_register(&vdp, &state, 21, 0xaa));
    assert(gens_vdp_set_register(&vdp, &state, 22, 0xbb));
    assert(gens_vdp_set_register(&vdp, &state, 23, 0xff));
    assert(dma_address == UINT32_C(0x877fbbaa) && control_dma_mode == 0xc0);
    assert(gens_vdp_set_register(&vdp, &state, 34, 0x38));
    assert(regs[2] == UINT32_C(0x12345638));
    assert(gens_vdp_set_register(&vdp, &state, 24, 0xff));

    regs[0] = UINT32_C(0x12345678);
    assert(gens_vdp_set_register(&vdp, &state, 0, 0x12));
    assert(regs[0] == UINT32_C(0x12345612) && irq_calls == 1);
    irq_result = 0;
    regs[1] = UINT32_C(0x87654321);
    assert(!gens_vdp_set_register(&vdp, &state, 1, 0x34));
    assert(regs[1] == UINT32_C(0x87654334) && irq_calls == 2);
    assert(!gens_vdp_set_register(&vdp, 0, 0, 0));
    assert(gens_vdp_register_callback(&vdp, &state, 2, 0x20));
    assert(regs[2] == UINT32_C(0x12345620) && scr_a_addr == vram + 0x8000);
    control_flag = 0;
    assert(gens_vdp_control_write(&write_port, &state, 0x8c81));
    assert(control_access == 5 && control_address == 0);
    assert(regs[12] == 0x81 && hcell == 40 && cram_flag == 1);
    return 0;
}
