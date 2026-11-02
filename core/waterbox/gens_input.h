#ifndef CHIMERA_GENS_INPUT_H
#define CHIMERA_GENS_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum GensInputTopology {
    GENS_INPUT_TWO_PADS = 0,
    GENS_INPUT_TEAM_PLAYER = 1
};

enum {
    GENS_INPUT_POWER = 0,
    GENS_INPUT_RESET = 1,
    GENS_INPUT_PAD_BASE = 2,
    GENS_INPUT_BUTTONS_PER_PAD = 12,
    GENS_INPUT_PLAYER_COUNT = 3,
    GENS_INPUT_BUTTON_COUNT = GENS_INPUT_PAD_BASE +
                              GENS_INPUT_BUTTONS_PER_PAD * GENS_INPUT_PLAYER_COUNT
};

typedef struct GensInputConfig {
    enum GensInputTopology topology;
    unsigned int player1_buttons;
    unsigned int player2_buttons;
    unsigned int player3_buttons;
    unsigned int team_port2_buttons;
    unsigned int team_pad4_buttons;
} GensInputConfig;

/* Call during initialization, after the machine has initialized its I/O. */
int gens_input_configure(const GensInputConfig *config);

/* Apply the packed Chimera input levels before running a frame. */
void gens_input_apply(uint64_t packed);

int gens_input_power_requested(uint64_t packed);
int gens_input_reset_requested(uint64_t packed);

#ifdef __cplusplus
}
#endif

#endif
