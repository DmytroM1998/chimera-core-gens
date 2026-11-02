/* Pending-bit and register effects follow the pinned VDP interrupt routines.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_irq.h"

#include <string.h>

_Static_assert(sizeof(int) == 4 && sizeof(unsigned int) == 4, "Gens IRQ global widths");

static uint32_t bits(const void *source)
{
    uint32_t value;
    memcpy(&value, source, sizeof(value));
    return value;
}

int gens_vdp_irq_valid(const GensVdpIrq *irq)
{
    return irq != 0 && irq->set1 != 0 && irq->set2 != 0 &&
        irq->pending != 0 && irq->cpu_interrupts != 0;
}

int gens_vdp_irq_update(void *opaque, GensM68kBusState *state)
{
    GensVdpIrq *irq = opaque;
    uint32_t pending;
    unsigned int level;
    if (!gens_vdp_irq_valid(irq) || state == 0)
        return 0;
    pending = bits(irq->pending);
    if ((*irq->set2 & 0x20u) != 0u && (pending & 8u) != 0u)
        level = 6;
    else if ((*irq->set1 & 0x10u) != 0u && (pending & 4u) != 0u)
        level = 4;
    else
    {
        *irq->cpu_interrupts &= 0xf0u;
        return 1;
    }
    return irq->interrupt != 0 && irq->interrupt(irq->opaque, state, level, -1);
}

int gens_vdp_irq_ack(GensVdpIrq *irq, uint32_t *eax)
{
    uint32_t pending, horizontal;
    if (!gens_vdp_irq_valid(irq) || eax == 0)
        return 0;
    pending = bits(irq->pending);
    if ((*irq->set2 & 0x20u) != 0u && (pending & 8u) != 0u)
    {
        horizontal = (*irq->set1 & 0x10u) >> 2;
        pending &= UINT32_C(0xfffffff7);
        *eax = (*eax & UINT32_C(0xffff0000)) |
            horizontal << 8 | (pending & horizontal & 255u);
    }
    else
    {
        pending &= UINT32_C(0xffffff00);
        *eax &= UINT32_C(0xffffff00);
    }
    memcpy(irq->pending, &pending, sizeof(pending));
    return 1;
}
