/* Controller wiring follows Gens-Rerecording io.asm and movie.cpp at the
 * pinned revision. The upstream component terms apply to derived portions.
 */
#include "gens_input.h"

#include "io.h"

#define PAD_POINTERS(pad) { \
    &Controller_##pad##_Up, &Controller_##pad##_Down, \
    &Controller_##pad##_Left, &Controller_##pad##_Right, \
    &Controller_##pad##_A, &Controller_##pad##_B, \
    &Controller_##pad##_C, &Controller_##pad##_Start, \
    &Controller_##pad##_X, &Controller_##pad##_Y, \
    &Controller_##pad##_Z, &Controller_##pad##_Mode \
}

static int valid_pad(unsigned int buttons)
{
    return buttons == 3 || buttons == 6;
}

static unsigned int pad_type(unsigned int buttons)
{
    return buttons == 6 ? 1u : 0u;
}

int gens_input_configure(const GensInputConfig *config)
{
    if (config == 0 ||
        (config->topology != GENS_INPUT_TWO_PADS &&
         config->topology != GENS_INPUT_TEAM_PLAYER) ||
        !valid_pad(config->player1_buttons) ||
        !valid_pad(config->player2_buttons) ||
        (config->topology == GENS_INPUT_TEAM_PLAYER &&
         (!valid_pad(config->player3_buttons) ||
          !valid_pad(config->team_port2_buttons) ||
          !valid_pad(config->team_pad4_buttons))))
        return 0;

    Controller_1_Type = pad_type(config->player1_buttons);
    Controller_1B_Type = 0;
    Controller_1C_Type = 0;
    Controller_1D_Type = 0;
    Controller_2B_Type = 0;
    Controller_2C_Type = 0;
    Controller_2D_Type = 0;
    if (config->topology == GENS_INPUT_TEAM_PLAYER)
    {
        Controller_1_Type |= 0x10u;
        Controller_1B_Type = pad_type(config->player2_buttons);
        Controller_1C_Type = pad_type(config->player3_buttons);
        Controller_1D_Type = pad_type(config->team_pad4_buttons);
        Controller_2_Type = pad_type(config->team_port2_buttons);
    }
    else
    {
        Controller_2_Type = pad_type(config->player2_buttons);
    }
    Make_IO_Table();
    return 1;
}

static void set_pad(unsigned int *const *pad, unsigned int pressed)
{
    unsigned int button;
    for (button = 0; button < GENS_INPUT_BUTTONS_PER_PAD; ++button)
        *pad[button] = ((pressed >> button) & 1u) == 0u;
}

void gens_input_apply(uint64_t packed)
{
    unsigned int *const p1[] = PAD_POINTERS(1);
    unsigned int *const p1b[] = PAD_POINTERS(1B);
    unsigned int *const p1c[] = PAD_POINTERS(1C);
    unsigned int *const p1d[] = PAD_POINTERS(1D);
    unsigned int *const p2[] = PAD_POINTERS(2);
    const unsigned int player1 = (unsigned int)((packed >> GENS_INPUT_PAD_BASE) & 0xfffu);
    const unsigned int player2 = (unsigned int)((packed >>
        (GENS_INPUT_PAD_BASE + GENS_INPUT_BUTTONS_PER_PAD)) & 0xfffu);
    const unsigned int player3 = (unsigned int)((packed >>
        (GENS_INPUT_PAD_BASE + 2 * GENS_INPUT_BUTTONS_PER_PAD)) & 0xfffu);

    set_pad(p1, player1);
    set_pad(p1d, 0);
    if ((Controller_1_Type & 0x10u) != 0u)
    {
        set_pad(p1b, player2);
        set_pad(p1c, player3);
        set_pad(p2, 0);
    }
    else
    {
        set_pad(p2, player2);
        set_pad(p1b, 0);
        set_pad(p1c, 0);
    }
}

int gens_input_power_requested(uint64_t packed)
{
    return (packed & (UINT64_C(1) << GENS_INPUT_POWER)) != 0;
}

int gens_input_reset_requested(uint64_t packed)
{
    return (packed & (UINT64_C(1) << GENS_INPUT_RESET)) != 0;
}
