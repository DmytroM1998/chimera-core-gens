#include <assert.h>
#include <stdint.h>

#include "gens_input.h"
#include "io.h"

#define DEFINE_PAD(pad) \
    unsigned int Controller_##pad##_Type; \
    unsigned int Controller_##pad##_Up, Controller_##pad##_Down; \
    unsigned int Controller_##pad##_Left, Controller_##pad##_Right; \
    unsigned int Controller_##pad##_A, Controller_##pad##_B; \
    unsigned int Controller_##pad##_C, Controller_##pad##_Start; \
    unsigned int Controller_##pad##_X, Controller_##pad##_Y; \
    unsigned int Controller_##pad##_Z, Controller_##pad##_Mode

DEFINE_PAD(1);
DEFINE_PAD(1B);
DEFINE_PAD(1C);
DEFINE_PAD(1D);
DEFINE_PAD(2);
unsigned int Controller_2B_Type, Controller_2C_Type, Controller_2D_Type;

static int tables_built;
void Make_IO_Table(void) { ++tables_built; }

#define PAD_POINTERS(pad) { \
    &Controller_##pad##_Up, &Controller_##pad##_Down, \
    &Controller_##pad##_Left, &Controller_##pad##_Right, \
    &Controller_##pad##_A, &Controller_##pad##_B, \
    &Controller_##pad##_C, &Controller_##pad##_Start, \
    &Controller_##pad##_X, &Controller_##pad##_Y, \
    &Controller_##pad##_Z, &Controller_##pad##_Mode \
}

static void check_levels(unsigned int *const *pad, unsigned int pressed)
{
    unsigned int button;
    for (button = 0; button < GENS_INPUT_BUTTONS_PER_PAD; ++button)
        assert(*pad[button] == (unsigned int)(((pressed >> button) & 1u) == 0u));
}

int main(void)
{
    GensInputConfig config = {GENS_INPUT_TWO_PADS, 6, 3, 0, 0, 0};
    unsigned int *const p1[] = PAD_POINTERS(1);
    unsigned int *const p1b[] = PAD_POINTERS(1B);
    unsigned int *const p1c[] = PAD_POINTERS(1C);
    unsigned int *const p1d[] = PAD_POINTERS(1D);
    unsigned int *const p2[] = PAD_POINTERS(2);
    unsigned int player, button;

    assert(!gens_input_configure(0));
    assert(gens_input_configure(&config));
    assert(tables_built == 1 && Controller_1_Type == 1 && Controller_2_Type == 0);
    for (player = 0; player < 3; ++player)
    {
        for (button = 0; button < 12; ++button)
        {
            uint64_t packed = UINT64_C(1) << (GENS_INPUT_PAD_BASE + player * 12 + button);
            gens_input_apply(packed);
            check_levels(p1, player == 0 ? 1u << button : 0);
            check_levels(p2, player == 1 ? 1u << button : 0);
            check_levels(p1b, 0);
            check_levels(p1c, 0);
            check_levels(p1d, 0);
        }
    }

    config.topology = GENS_INPUT_TEAM_PLAYER;
    config.player1_buttons = 3;
    config.player2_buttons = 6;
    config.player3_buttons = 3;
    config.team_port2_buttons = 6;
    config.team_pad4_buttons = 6;
    assert(gens_input_configure(&config));
    assert(tables_built == 2);
    assert(Controller_1_Type == 0x10 && Controller_1B_Type == 1);
    assert(Controller_1C_Type == 0 && Controller_1D_Type == 1 && Controller_2_Type == 1);
    for (player = 0; player < 3; ++player)
    {
        for (button = 0; button < 12; ++button)
        {
            uint64_t packed = UINT64_C(1) << (GENS_INPUT_PAD_BASE + player * 12 + button);
            gens_input_apply(packed);
            check_levels(p1, player == 0 ? 1u << button : 0);
            check_levels(p1b, player == 1 ? 1u << button : 0);
            check_levels(p1c, player == 2 ? 1u << button : 0);
            check_levels(p1d, 0);
            check_levels(p2, 0);
        }
    }
    gens_input_apply((UINT64_C(0xfff) << 2) |
                     (UINT64_C(0xfff) << 14) |
                     (UINT64_C(0xfff) << 26));
    check_levels(p1, 0xfff);
    check_levels(p1b, 0xfff);
    check_levels(p1c, 0xfff);
    check_levels(p1d, 0);
    check_levels(p2, 0);
    assert(!gens_input_power_requested(0));
    assert(!gens_input_reset_requested(0));
    assert(gens_input_power_requested(1));
    assert(!gens_input_reset_requested(1));
    assert(gens_input_reset_requested(2));
    assert(!gens_input_power_requested(2));
    assert(gens_input_power_requested(3) && gens_input_reset_requested(3));

    config.player3_buttons = 2;
    assert(!gens_input_configure(&config));
    assert(tables_built == 2 && Controller_1_Type == 0x10);
    config.player3_buttons = 3;
    config.team_port2_buttons = 2;
    assert(!gens_input_configure(&config));
    assert(tables_built == 2 && Controller_2_Type == 1);
    config.team_port2_buttons = 6;
    config.team_pad4_buttons = 2;
    assert(!gens_input_configure(&config));
    assert(tables_built == 2 && Controller_1D_Type == 1);
    config.topology = (enum GensInputTopology)99;
    assert(!gens_input_configure(&config));
    assert(tables_built == 2);
    return 0;
}
