#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "gens_vdp_write.h"

static uint8_t vram[65536], cram[256], vsram[256];
static int ctrl[5], ctrl_data, status, dma_length, dma_address, dmat_length, dmat_type;
static int vram_flag, cram_flag;
static unsigned int set2, auto_increment, last_register, last_dma, register_calls, dma_calls;
static uint8_t last_register_value;
static uint32_t last_dma_mode, last_dma_address;
static int register_result = 1, dma_result = 1;

static int set_register(void *opaque, GensM68kBusState *state,
                        unsigned int reg, uint8_t value)
{
    (void)opaque;
    (void)state;
    ++register_calls;
    last_register = reg;
    last_register_value = value;
    return register_result;
}

static int run_dma(void *opaque, GensM68kBusState *state, unsigned int dma,
                   uint32_t mode, uint32_t address)
{
    (void)opaque;
    (void)state;
    ++dma_calls;
    last_dma = dma;
    last_dma_mode = mode;
    last_dma_address = address;
    return dma_result;
}

static GensVdpWrite vdp = {
    vram, cram, vsram, sizeof(vram), sizeof(cram), sizeof(vsram),
    &ctrl[0], &ctrl_data, &ctrl[3], &ctrl[4], &ctrl[2], &ctrl[1],
    &status, &dma_length, &dma_address, &dmat_length, &dmat_type, &set2, &auto_increment,
    &vram_flag, &cram_flag, 0, 0, set_register, run_dma
};

static void copy_cases(GensM68kBusState *state)
{
    set2 = 0x10;
    ctrl[0] = 0;
    ctrl_data = 0;
    ctrl[1] = 0x23456789;
    ctrl[2] = 0x123456c0;
    ctrl[4] = 0x12340000;
    dma_length = 0x12340003;
    dma_address = 0x45670000;
    auto_increment = 1;
    vram_flag = 0x6789ab00;
    status = 0x56780001;
    vram[0] = 0xa5;
    vram[1] = 0xb6;
    vram[2] = 0xc7;
    vram[3] = 0xd8;
    assert(gens_vdp_control_write(&vdp, state, 0x0001));
    assert(gens_vdp_control_write(&vdp, state, 0x00c0));
    assert(vram[1] == 0xa5 && vram[2] == 0xa5 && vram[3] == 0xa5);
    assert(dma_length == 0 && dma_address == 3 && dmat_length == 3 && dmat_type == 3);
    assert(ctrl[4] == 0x12340004 && ctrl[1] == 0x23456789 && vram_flag == 1);
    assert(status == 0x56780003);

    ctrl[0] = 0;
    ctrl_data = 0;
    dma_length = 2;
    dma_address = 0x1234ffff;
    auto_increment = 2;
    vram[65535] = 0x12;
    vram[0] = 0x34;
    assert(gens_vdp_control_write(&vdp, state, 0x3fff));
    assert(gens_vdp_control_write(&vdp, state, 0x00c3));
    assert(vram[65535] == 0x12 && vram[1] == 0x34);
    assert(dma_address == 1 && ctrl[4] == 0x12340003);

    ctrl[0] = 0;
    ctrl_data = 0;
    dma_length = 0x12340000;
    vram_flag = 0x12345678;
    assert(gens_vdp_control_write(&vdp, state, 0x0000));
    assert(gens_vdp_control_write(&vdp, state, 0x00c0));
    assert(ctrl[1] == 0 && dma_length == 0x12340000 && vram_flag == 0x12345678);

    ctrl[0] = 0;
    ctrl_data = 0;
    ctrl[1] = 0x23456789;
    ctrl[2] = 0;
    dma_length = 1;
    vdp.run_dma = 0;
    assert(gens_vdp_control_write(&vdp, state, 0x4000));
    assert(!gens_vdp_control_write(&vdp, state, 0x00c0));
    assert(ctrl[1] == 0x23456789 && dma_length == 1);
    vdp.run_dma = run_dma;
}

int main(void)
{
    GensM68kBusState state;
    GensVdpWrite invalid = vdp;

    memset(&state, 0, sizeof(state));
    assert(gens_vdp_write_valid(&vdp));
    invalid.vram_size = 65535;
    assert(!gens_vdp_write_valid(&invalid));

    ctrl[0] = 0x12345600;
    ctrl_data = 0x76540000;
    ctrl[3] = 0x12345678;
    ctrl[4] = 0x12345678;
    assert(gens_vdp_control_write(&vdp, &state, 0x8f02));
    assert(register_calls == 1 && last_register == 15 && last_register_value == 2);
    assert(ctrl[0] == 0x12345600 && ctrl[3] == 5 && ctrl[4] == 0);

    vdp.set_register = 0;
    assert(!gens_vdp_control_write(&vdp, &state, 0x8f02));
    assert(ctrl[3] == 5 && ctrl[4] == 0);
    vdp.set_register = set_register;

    ctrl[0] = 0x12345600;
    ctrl_data = 0;
    ctrl[3] = (int)UINT32_C(0x12345678);
    ctrl[4] = (int)UINT32_C(0x98760000);
    assert(gens_vdp_control_write(&vdp, &state, 0x5234));
    assert(ctrl[0] == 0x12345601 && ctrl_data == 0x5234);
    assert(ctrl[3] == (int)UINT32_C(0x12345609));
    assert(ctrl[4] == (int)UINT32_C(0x98761234));
    ctrl[3] = 0;
    assert(gens_vdp_control_write(&vdp, &state, 0x0001));
    assert(ctrl[0] == 0x12345600 && ctrl_data == 0x00015234);
    assert(ctrl[3] == 9);
    assert(ctrl[4] == (int)UINT32_C(0x98765234));

    set2 = 0x10;
    ctrl[2] = 0;
    ctrl[1] = 0x12345678;
    ctrl[0] = 0;
    ctrl_data = 0;
    dma_length = 1;
    assert(gens_vdp_control_write(&vdp, &state, 0x4000));
    assert(gens_vdp_control_write(&vdp, &state, 0x00c0));
    assert(dma_calls == 1 && last_dma == 1 && last_dma_mode == 0 &&
           last_dma_address == 0);
    assert(ctrl[1] == 0x12345678);

    ctrl_data = 0;
    ctrl[0] = 0;
    dma_length = 0;
    assert(gens_vdp_control_write(&vdp, &state, 0x4000));
    assert(gens_vdp_control_write(&vdp, &state, 0x00c0));
    assert(dma_calls == 1 && ctrl[1] == 0);

    ctrl_data = 0;
    ctrl[0] = 0;
    dma_length = 1;
    assert(gens_vdp_control_write(&vdp, &state, 0x0000));
    assert(gens_vdp_control_write(&vdp, &state, 0x00c0));
    assert(dma_calls == 1 && ctrl[1] == 0);

    ctrl[1] = 0x12345678;
    ctrl_data = 0;
    ctrl[0] = 0;
    ctrl[2] = 0x80;
    assert(gens_vdp_control_write(&vdp, &state, 0x4000));
    assert(gens_vdp_control_write(&vdp, &state, 0x0081));
    assert(ctrl[1] == 0x12345605 && dma_calls == 1);
    ctrl[4] = 0x10;
    dma_length = 0;
    auto_increment = 1;
    assert(gens_vdp_genesis_write(&vdp, &state, GENS_GENESIS_VDP,
                                  0xc00000, 2, 0x5a12));
    assert(ctrl[1] == 0 && vram[0x11] == 0x5a && vram[0x10] == 0x5a);
    assert(dmat_length == 0xffff && ctrl[4] == 0x10);

    ctrl[0] = (int)UINT32_C(0x123456ab);
    ctrl[3] = 9;
    ctrl[4] = 0x0011;
    auto_increment = 2;
    vram_flag = 0;
    assert(gens_vdp_data_write(&vdp, 2, 0x1234));
    assert(ctrl[0] == (int)UINT32_C(0x12345600));
    assert(ctrl[4] == 0x0013);
    assert(vram[0x10] == 0x12 && vram[0x11] == 0x34 && vram_flag == 1);

    ctrl[4] = 0x21;
    auto_increment = 1;
    assert(gens_vdp_data_write(&vdp, 1, 0x00a5));
    assert(vram[0x20] == 0xa5 && vram[0x21] == 0xa5 && ctrl[4] == 0x22);

    ctrl[3] = 10;
    ctrl[4] = 0x123400ff;
    auto_increment = 3;
    cram_flag = 0;
    assert(gens_vdp_data_write(&vdp, 2, 0xabcd));
    assert(cram[0x7e] == 0xcd && cram[0x7f] == 0xab);
    assert(cram_flag == 1 && ctrl[4] == (int)UINT32_C(0x12340102));

    ctrl[3] = 11;
    ctrl[4] = 0x7e;
    auto_increment = 2;
    assert(gens_vdp_data_write(&vdp, 2, 0x9876));
    assert(vsram[0x7e] == 0x76 && vsram[0x7f] == 0x98 && ctrl[4] == 0x80);

    ctrl[0] = 0x76543211;
    ctrl[1] = 4;
    ctrl[3] = 0;
    ctrl[4] = 0x12340010;
    status = 0x56780001;
    dma_length = 1;
    auto_increment = 2;
    vram_flag = 0;
    assert(gens_vdp_data_write(&vdp, 1, 0x5a));
    assert(vram[0x11] == 0x5a && vram[0x10] == 0x5a && vram[0x12] == 0x5a);
    assert(dma_length == 0 && dmat_length == 1 && dmat_type == 2);
    assert(ctrl[1] == 0 && ctrl[0] == 0x76543200 &&
           ctrl[4] == (int)UINT32_C(0x12340014));
    assert(status == 0x56780003 && vram_flag == 1);

    ctrl[1] = 4;
    ctrl[4] = 0xffff;
    dma_length = 1;
    auto_increment = 2;
    assert(gens_vdp_data_write(&vdp, 2, 0x1234));
    assert(ctrl[4] == 3 && vram[0xffff] == 0x12 && vram[0xfffe] == 0x34);

    ctrl[1] = 0;
    ctrl[3] = 8;
    ctrl[4] = 0x1234;
    ctrl[0] = 0x12345678;
    /* A data write in a read mode is ignored after clearing Ctrl.Flag, as in the original. */
    assert(gens_vdp_data_write(&vdp, 2, 0xabcd));
    assert(ctrl[0] == 0x12345600 && ctrl[4] == 0x1234);

    ctrl[3] = 9;
    ctrl[4] = 0x20;
    assert(gens_vdp_genesis_write(&vdp, &state, GENS_GENESIS_VDP,
                                  0xc00003, 1, 0x66));
    assert(gens_vdp_genesis_write(&vdp, &state, GENS_GENESIS_VDP,
                                  0xc00004, 2, 0x1234));
    assert(!gens_vdp_genesis_write(&vdp, &state, GENS_GENESIS_VDP,
                                   0xc00008, 2, 0x1234));
    assert(!gens_vdp_genesis_write(&vdp, &state, GENS_GENESIS_PSG,
                                   0xc00000, 1, 0x12));
    copy_cases(&state);
    return 0;
}
