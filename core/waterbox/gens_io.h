#ifndef CHIMERA_GENS_IO_H
#define CHIMERA_GENS_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Optional. Called at every controller read, where Gens clears Lag_Frame. It must not
 * change machine state. */
extern void (*gens_io_poll_hook)(void);

uint16_t gens_io_read_word_1(void);
uint16_t gens_io_read_word_2(void);

#ifdef __cplusplus
}
#endif

#endif
