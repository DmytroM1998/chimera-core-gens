/* Clearing ranges and register order follow the pinned Reset_VDP.
 * The upstream component terms apply to derived portions.
 */
#include "gens_vdp_reset.h"

#include <string.h>

int gens_vdp_reset_valid(const GensVdpReset *reset)
{
    return reset != 0 && gens_vdp_valid(reset->vdp) && gens_vdp_irq_valid(reset->irq) &&
        reset->irq->interrupt != 0 &&
        reset->irq->set1 == &reset->vdp->registers.registers[0] &&
        reset->irq->set2 == &reset->vdp->registers.registers[1] &&
        reset->vdp->registers.opaque == reset->irq &&
        reset->vdp->registers.update_irq == gens_vdp_irq_update &&
        reset->screen16 != 0 && reset->screen16_count >= 336u * 240u &&
        reset->screen32 != 0 && reset->screen32_count >= 336u * 240u &&
        reset->palette16 != 0 && reset->palette16_count >= 256u &&
        reset->palette32 != 0 && reset->palette32_count >= 256u &&
        reset->sprites != 0 && reset->sprite_count >= 2048u &&
        reset->visible != 0 && reset->visible_count >= 256u &&
        reset->vsram_over != 0 && reset->vsram_over_count >= 8u &&
        reset->palette_lock != 0 && reset->dma_tmp != 0;
}

int gens_vdp_reset(GensVdpReset *reset, GensM68kBusState *state)
{
    GensVdp *vdp;
    unsigned int reg;

    if (!gens_vdp_reset_valid(reset) || state == 0)
        return 0;
    vdp = reset->vdp;
    memset(reset->screen16, 0, 336u * 240u * sizeof(uint16_t));
    memset(reset->screen32, 0, 336u * 240u * sizeof(uint32_t));
    memset(vdp->write.vram, 0, 65536u);
    memset(vdp->write.cram, 0, 160u);
    memset(vdp->write.vsram, 0, 80u);
    if ((*reset->palette_lock & 1u) == 0u)
    {
        memset(reset->palette16, 0, 256u * sizeof(uint16_t));
        memset(reset->palette32, 0, 256u * sizeof(uint32_t));
    }
    memset(reset->sprites, 0, 800u * sizeof(uint32_t));
    memset(reset->visible, 0, 100u * sizeof(uint32_t));

    for (reg = 0; reg < 23u; ++reg)
        if (!gens_vdp_set_register(&vdp->registers, state, reg, 0))
            return 0;

    /* EBX still points 400 bytes past Sprite_Visible in the pinned reset. */
    reset->visible[124] = 0;
    reset->visible[125] = 0;
    reset->visible[126] = 0;
    *vdp->write.status = 0x0200;
    *reset->irq->pending = 0;
    *reset->dma_tmp = 0;
    *vdp->timing.length = 0;
    *vdp->timing.type = 0;
    *vdp->write.control_flag = 0;
    *vdp->write.control_data = 0;
    vdp->control[2] = 0;
    *vdp->write.control_access = 0;
    *vdp->write.control_address = 0;
    *vdp->write.control_dma_mode = 0;
    *vdp->write.control_dma = 0;
    *vdp->write.cram_flag = 1;
    *vdp->write.vram_flag = 1;
    if (!gens_vdp_h_counter_init(vdp->read.h_counter, vdp->read.h_counter_size))
        return 0;
    reset->vsram_over[7] = 0;
    return 1;
}
