#include "gens_vdp.h"

int gens_vdp_valid(const GensVdp *vdp)
{
    return vdp != 0 && vdp->control != 0 && vdp->control_count >= 7u &&
        gens_vdp_read_valid(&vdp->read) &&
        gens_vdp_registers_valid(&vdp->registers) &&
        vdp->registers.register_count >= 26u &&
        vdp->write.control_flag == &vdp->control[0] &&
        vdp->write.control_data == &vdp->control[1] &&
        vdp->write.control_access == &vdp->control[3] &&
        vdp->write.control_address == &vdp->control[4] &&
        vdp->write.control_dma_mode == &vdp->control[5] &&
        vdp->write.control_dma == &vdp->control[6] &&
        vdp->read.control_flag == vdp->write.control_flag &&
        vdp->read.control_access == vdp->write.control_access &&
        vdp->read.control_address == vdp->write.control_address &&
        gens_vdp_memory_dma_valid(&vdp->memory) &&
        vdp->memory.write == &vdp->write && vdp->memory.timing == &vdp->timing &&
        vdp->write.opaque == &vdp->memory &&
        vdp->write.register_opaque == &vdp->registers &&
        vdp->write.set_register == gens_vdp_register_callback &&
        vdp->write.run_dma == gens_vdp_memory_dma &&
        vdp->read.read_odometer != 0 && vdp->registers.update_irq != 0;
}

int gens_vdp_init(GensVdp *vdp, const GensVdpConfig *config)
{
    GensVdp next;
    const GensVdpRegisters *registers;
    uint32_t *slots;
    int *control;

    if (vdp == 0 || config == 0 || !gens_vdp_registers_valid(&config->registers) ||
        config->registers.register_count < 26u || config->control == 0 ||
        config->control_count < 7u || config->vram_flag == 0 || config->cram_flag == 0 ||
        config->read_odometer == 0 || config->registers.update_irq == 0)
        return 0;
    registers = &config->registers;
    slots = registers->registers;
    control = config->control;
    if (registers->dma_length != &slots[24] || registers->dma_address != &slots[25] ||
        registers->control_dma_mode != (uint32_t *)&control[5] ||
        registers->vram_flag != (uint8_t *)config->vram_flag ||
        registers->cram_flag != (uint8_t *)config->cram_flag)
        return 0;

    next.registers = *registers;
    next.control = control;
    next.control_count = config->control_count;
    next.read = (GensVdpRead){
        registers->vram, config->cram, config->vsram, config->h_counter,
        registers->vram_size, config->cram_size, config->vsram_size, config->h_counter_size,
        config->status, &control[0], &control[3], &control[4],
        &slots[1], &slots[12], &slots[15],
        config->cycles_m68k, config->cycles_per_line, config->current_line,
        config->odometer_opaque, config->read_odometer
    };
    next.write = (GensVdpWrite){
        registers->vram, config->cram, config->vsram,
        registers->vram_size, config->cram_size, config->vsram_size,
        &control[0], &control[1], &control[3], &control[4], &control[5], &control[6],
        config->status, (int *)&slots[24], (int *)&slots[25],
        config->dma_length, config->dma_type, &slots[1], &slots[15],
        config->vram_flag, config->cram_flag, &next.memory, &next.registers,
        gens_vdp_register_callback, gens_vdp_memory_dma
    };
    next.timing = (GensVdpDmaTiming){
        &slots[1], &slots[12], config->current_line, config->visible_lines,
        config->cycles_per_line, config->dma_length, config->dma_type, config->status
    };
    next.memory = (GensVdpMemoryDma){
        &next.write, &next.timing, config->rom, config->ram,
        config->rom_backing_size, config->ram_size, config->rom_size, config->sega_cd_started
    };
    if (!gens_vdp_valid(&next))
        return 0;

    *vdp = next;
    vdp->write.opaque = &vdp->memory;
    vdp->write.register_opaque = &vdp->registers;
    vdp->memory.write = &vdp->write;
    vdp->memory.timing = &vdp->timing;
    return 1;
}

int gens_vdp_device_read(void *opaque, GensM68kBusState *state,
                         enum GensGenesisDevice device, uint32_t address,
                         unsigned int size, uint16_t *value)
{
    GensVdp *vdp = opaque;
    return vdp != 0 && gens_vdp_genesis_read(&vdp->read, state, device, address, size, value);
}

int gens_vdp_device_write(void *opaque, GensM68kBusState *state,
                          enum GensGenesisDevice device, uint32_t address,
                          unsigned int size, uint16_t value)
{
    GensVdp *vdp = opaque;
    return vdp != 0 && gens_vdp_genesis_write(&vdp->write, state, device, address, size, value);
}

int gens_vdp_window_read(void *opaque, GensM68kBusState *state,
                         enum GensZ80Device device, uint32_t address, uint16_t *value)
{
    GensVdp *vdp = opaque;
    return vdp != 0 && gens_vdp_z80_read(&vdp->read, state, device, address, value);
}

int gens_vdp_native_device_read(void *opaque, GensM68kBusState *state,
                                enum GensZ80NativeOperation operation,
                                GensZ80Registers *registers)
{
    GensVdp *vdp = opaque;
    return vdp != 0 && gens_vdp_native_read(&vdp->read, state, operation, registers);
}

int gens_vdp_dma_tick(GensVdp *vdp, uint32_t *cycles)
{
    return vdp != 0 && gens_vdp_dma_update(&vdp->timing, cycles);
}
