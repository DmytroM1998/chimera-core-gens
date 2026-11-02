/* Controller behavior follows io.asm in the pinned Gens-Rerecording tree.
 * The upstream component terms apply to derived portions.
 */
#include <stdint.h>

#include "gens_io.h"
#include "io.h"

_Static_assert(sizeof(unsigned int) == 4, "Gens controller globals require 32-bit unsigned int");

typedef struct Pad {
    unsigned int *type, *up, *down, *left, *right, *start, *mode;
    unsigned int *a, *b, *c, *x, *y, *z;
} Pad;

#define PAD(p) { \
    &Controller_##p##_Type, &Controller_##p##_Up, &Controller_##p##_Down, \
    &Controller_##p##_Left, &Controller_##p##_Right, &Controller_##p##_Start, \
    &Controller_##p##_Mode, &Controller_##p##_A, &Controller_##p##_B, \
    &Controller_##p##_C, &Controller_##p##_X, &Controller_##p##_Y, \
    &Controller_##p##_Z \
}

static const Pad pads1[4] = {PAD(1), PAD(1B), PAD(1C), PAD(1D)};
static const Pad pad2 = PAD(2);

enum TeamEntry {
    TEAM_UNDEFINED = 0,
    TEAM_BASE,
    TEAM_3F,
    TEAM_73,
    TEAM_TYPE_A,
    TEAM_TYPE_B,
    TEAM_TYPE_C,
    TEAM_TYPE_D,
    TEAM_PAD_BASE
};

static uint8_t team1[64] = {
    TEAM_BASE, TEAM_3F, TEAM_BASE, TEAM_73,
    TEAM_TYPE_A, TEAM_BASE, TEAM_TYPE_A, TEAM_BASE,
    TEAM_TYPE_C, TEAM_TYPE_B, TEAM_TYPE_C, TEAM_TYPE_B,
    TEAM_UNDEFINED, TEAM_TYPE_D, TEAM_UNDEFINED, TEAM_TYPE_D
};

static unsigned int read_pad(const Pad *pad, unsigned int counter, unsigned int state,
                             int special_port1)
{
    unsigned int phase = (*pad->type & 1u) != 0u ? counter & 3u : 0u;
    int high = (state & 0x40u) != 0u;
    unsigned int a, b;

    if (high && phase != 3u)
    {
        a = ((*pad->c << 2) | *pad->right) << 2;
        b = ((*pad->b << 2) | *pad->left) << 2;
        return ((a | *pad->down) * 2u) + (b | *pad->up) + 0x40u;
    }
    if (!high && phase < 2u)
        return (((*pad->start << 4) | *pad->down) * 2u) +
               ((*pad->a << 4) | *pad->up);
    if (!high && phase == 2u)
    {
        const Pad *source = special_port1 ? &pads1[0] : pad;
        return ((*source->start * 2u) + *source->a) << 4;
    }
    if (!high)
        return (((*pad->start * 2u) + *pad->a) << 4) | 0x0fu;
    a = ((*pad->c << 2) | *pad->mode) << 2;
    b = ((*pad->b << 2) | *pad->x) << 2;
    return ((a | *pad->y) * 2u) + (b | *pad->z) + 0x40u;
}

static unsigned int read_team(unsigned int state, unsigned int counter)
{
    unsigned int offset = ((state >> 3) & 0x0cu) + ((counter & 0x0f0000u) >> 12);
    unsigned int base = ((offset | ((offset & 4u) != 0u ? 2u : 0u)) << 3) & 0x70u;
    unsigned int entry = team1[offset >> 2];
    const Pad *pad;
    unsigned int group;

    switch (entry)
    {
    case TEAM_BASE: return base;
    case TEAM_3F: return 0x3fu;
    case TEAM_73: return 0x73u;
    case TEAM_TYPE_A: return base | (*pads1[0].type & 1u);
    case TEAM_TYPE_B: return base | (*pads1[1].type & 0xffffu);
    case TEAM_TYPE_C: return base | (*pads1[2].type & 0xffffu);
    case TEAM_TYPE_D: return base | (*pads1[3].type & 0xffffu);
    case TEAM_UNDEFINED: return base | 0x0fu;
    default: break;
    }

    entry -= TEAM_PAD_BASE;
    pad = &pads1[entry / 3u];
    group = entry % 3u;
    if (group == 0u)
        return ((((*pad->right * 2u + *pad->left) * 2u + *pad->down) * 2u +
                 *pad->up) + base);
    if (group == 1u)
        return ((((*pad->start * 2u + *pad->a) * 2u + *pad->c) * 2u +
                 *pad->b) + base);
    return ((((*pad->mode * 2u + *pad->x) * 2u + *pad->y) * 2u +
             *pad->z) + base);
}

void (*gens_io_poll_hook)(void);

uint16_t gens_io_read_word_1(void)
{
    Lag_Frame = 0;
    if (gens_io_poll_hook != 0)
        gens_io_poll_hook();
    if ((Controller_1_Type & 0x10u) == 0u)
        return (uint16_t)read_pad(&pads1[0], Controller_1_Counter,
                                  Controller_1_State, 0);
    if ((Controller_2_State & 0x0cu) != 0u)
    {
        if ((Controller_2_State & 0x40u) != 0u)
            return 0x70u;
        return (uint16_t)read_pad(&pads1[(Controller_2_State & 0x30u) >> 4],
                                  Controller_1_Counter, Controller_1_State, 1);
    }
    return (uint16_t)read_team(Controller_1_State, Controller_1_Counter);
}

uint16_t gens_io_read_word_2(void)
{
    Lag_Frame = 0;
    if (gens_io_poll_hook != 0)
        gens_io_poll_hook();
    if ((Controller_2_Type & 0x10u) != 0u)
        return (uint16_t)read_team(Controller_2_State, Controller_2_Counter);
    return (uint16_t)read_pad(&pad2, Controller_2_Counter, Controller_2_State, 0);
}

unsigned char RD_Controller_1(void)
{
    return (unsigned char)gens_io_read_word_1();
}

unsigned char RD_Controller_2(void)
{
    return (unsigned char)gens_io_read_word_2();
}

static void increment_byte(unsigned int *value, unsigned int shift)
{
    unsigned int mask = 0xffu << shift;
    unsigned int byte = ((*value >> shift) + 1u) & 0xffu;
    *value = (*value & ~mask) | (byte << shift);
}

static void write_port(unsigned int *state, unsigned int *counter, unsigned int *delay,
                       unsigned int type, unsigned char data)
{
    if ((*state & 0x40u) == 0u && (data & 0x40u) != 0u)
        increment_byte(counter, 0);
    *delay &= ~0xffu;
    if ((type & 0x10u) != 0u && (*state & 0x20u) == 0u && (data & 0x20u) != 0u)
        increment_byte(counter, 16);
    *state = (*state & ~0xffu) | data;
}

unsigned char WR_Controller_1(unsigned char data)
{
    write_port(&Controller_1_State, &Controller_1_Counter, &Controller_1_Delay,
               Controller_1_Type, data);
    return data;
}

unsigned char WR_Controller_2(unsigned char data)
{
    if ((Controller_1_Type & 0x10u) != 0u && (data & 0x0cu) != 0u)
        Controller_1_Counter = 0;
    write_port(&Controller_2_State, &Controller_2_Counter, &Controller_2_Delay,
               Controller_2_Type, data);
    return data;
}

void Fix_Controllers(void)
{
    if ((Controller_1_Delay & 0xffu) > 25u)
        Controller_1_Counter = 0;
    else
        increment_byte(&Controller_1_Delay, 0);
    if ((Controller_2_Delay & 0xffu) > 25u)
        Controller_2_Counter = 0;
    else
        increment_byte(&Controller_2_Delay, 0);
}

void Make_IO_Table(void)
{
    unsigned int sequence = 1u, player, group;
    for (player = 0; player < 4u; ++player)
    {
        unsigned int groups = (*pads1[player].type & 1u) != 0u ? 3u : 2u;
        for (group = 0; group < groups; ++group, ++sequence)
        {
            unsigned int index = 12u + ((sequence & 0x0eu) << 1) +
                                 ((sequence & 1u) ^ 1u);
            team1[index] = (uint8_t)(TEAM_PAD_BASE + player * 3u + group);
        }
    }
}
