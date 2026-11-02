/* Synthetic i386 reference for the pinned, unmodified main 68000 generator.
 * The upstream Starscream terms apply to the generated code linked with it.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Star_68k.h"

_Static_assert(sizeof(void *) == 4, "the original Starscream reference requires i386");
_Static_assert(sizeof(unsigned int) == 4, "the original context uses 32-bit unsigned integers");

extern int ref_cpu_init(void) __asm__("_main68k_init");
extern unsigned int ref_cpu_reset(void) __asm__("_main68k_reset");
extern unsigned int ref_cpu_exec(int target) __asm__("_main68k_exec");
extern int ref_cpu_interrupt(int level, int vector) __asm__("_main68k_interrupt");
extern int ref_cpu_context_size(void) __asm__("_main68k_GetContextSize");
extern void ref_cpu_get_context(void *context) __asm__("_main68k_GetContext");
extern void ref_cpu_set_context(void *context) __asm__("_main68k_SetContext");

unsigned char Ram_68k[65536];
unsigned char Rom_Data[65536];
unsigned int Rom_Size = sizeof(Rom_Data);
unsigned int ref_irq_ack_count, ref_irq_last_level;
unsigned int ref_hook_pc __asm__("_hook_pc");
unsigned int ref_hook_address __asm__("_hook_address");
unsigned int ref_hook_value __asm__("_hook_value");

enum { IO_BASE = 0x400000, IO_SIZE = 16, MAX_EVENTS = 512 };
enum EventType {
    EVENT_EXEC = 1, EVENT_READ_BYTE, EVENT_READ_WORD, EVENT_READ_LONG,
    EVENT_WRITE_BYTE, EVENT_WRITE_WORD, EVENT_WRITE_LONG,
    EVENT_BUS_READ_BYTE, EVENT_BUS_READ_WORD, EVENT_BUS_WRITE_BYTE, EVENT_BUS_WRITE_WORD
};
struct Event {
    unsigned int type, pc, address, value;
};
static struct Event events[MAX_EVENTS];
static unsigned int event_count;
static unsigned char io_bytes[IO_SIZE];
static unsigned int bus_read_byte_count, bus_read_word_count;
static unsigned int bus_write_byte_count, bus_write_word_count;

static _Noreturn void fail(const char *message)
{
    fprintf(stderr, "m68k reference: %s\n", message);
    exit(1);
}

static void record_event(unsigned int type, unsigned int pc,
                         unsigned int address, unsigned int value)
{
    if (event_count >= MAX_EVENTS)
        fail("event limit exceeded");
    events[event_count].type = type;
    events[event_count].pc = pc;
    events[event_count].address = address;
    events[event_count].value = value;
    ++event_count;
}

void ref_hook_exec(void) __asm__("_hook_exec");
void ref_hook_read_byte(void) __asm__("_hook_read_byte");
void ref_hook_read_word(void) __asm__("_hook_read_word");
void ref_hook_read_long(void) __asm__("_hook_read_dword");
void ref_hook_write_byte(void) __asm__("_hook_write_byte");
void ref_hook_write_word(void) __asm__("_hook_write_word");
void ref_hook_write_long(void) __asm__("_hook_write_dword");

static void hook(unsigned int type)
{
    ref_hook_pc &= 0xffffffu;
    record_event(type, ref_hook_pc, ref_hook_address, ref_hook_value);
}

void ref_hook_exec(void) { hook(EVENT_EXEC); }
void ref_hook_read_byte(void) { hook(EVENT_READ_BYTE); }
void ref_hook_read_word(void) { hook(EVENT_READ_WORD); }
void ref_hook_read_long(void) { hook(EVENT_READ_LONG); }
void ref_hook_write_byte(void) { hook(EVENT_WRITE_BYTE); }
void ref_hook_write_word(void) { hook(EVENT_WRITE_WORD); }
void ref_hook_write_long(void) { hook(EVENT_WRITE_LONG); }

unsigned int ref_bus_read_byte(unsigned int address)
{
    unsigned int value;
    address &= 0xffffffu;
    if (address < Rom_Size)
        value = Rom_Data[address ^ 1u];
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE)
        value = io_bytes[address - IO_BASE];
    else
        fail("unmapped byte read");
    ++bus_read_byte_count;
    record_event(EVENT_BUS_READ_BYTE, 0, address, value);
    return value;
}

unsigned int ref_bus_read_word(unsigned int address)
{
    unsigned int value;
    address &= 0xffffffu;
    if ((address & 1u) != 0u)
        fail("unaligned word read");
    if (address < Rom_Size - 1u)
        value = (unsigned int)Rom_Data[address] | ((unsigned int)Rom_Data[address + 1u] << 8);
    else if (address >= IO_BASE && address < IO_BASE + IO_SIZE - 1u)
        value = ((unsigned int)io_bytes[address - IO_BASE] << 8) | io_bytes[address - IO_BASE + 1u];
    else
        fail("unmapped word read");
    ++bus_read_word_count;
    record_event(EVENT_BUS_READ_WORD, 0, address, value);
    return value;
}

void ref_bus_write_byte(unsigned int address, unsigned int value)
{
    address &= 0xffffffu;
    if (address < IO_BASE || address >= IO_BASE + IO_SIZE)
        fail("unmapped byte write");
    io_bytes[address - IO_BASE] = (unsigned char)value;
    ++bus_write_byte_count;
    record_event(EVENT_BUS_WRITE_BYTE, 0, address, value & 0xffu);
}

void ref_bus_write_word(unsigned int address, unsigned int value)
{
    address &= 0xffffffu;
    if ((address & 1u) != 0u || address < IO_BASE || address >= IO_BASE + IO_SIZE - 1u)
        fail("unmapped or unaligned word write");
    io_bytes[address - IO_BASE] = (unsigned char)(value >> 8);
    io_bytes[address - IO_BASE + 1u] = (unsigned char)value;
    ++bus_write_word_count;
    record_event(EVENT_BUS_WRITE_WORD, 0, address, value & 0xffffu);
}

static void put_word(unsigned int address, unsigned int word)
{
    if ((address & 1u) != 0u || address >= Rom_Size - 1u)
        fail("invalid fixture word address");
    Rom_Data[address] = (unsigned char)word;
    Rom_Data[address + 1u] = (unsigned char)(word >> 8);
}

static void put_long(unsigned int address, unsigned int value)
{
    put_word(address, value >> 16);
    put_word(address + 2u, value & 0xffffu);
}

static void emit_trace(unsigned int step, unsigned int phase, unsigned int target,
                       unsigned int status, const struct S68000CONTEXT *context)
{
    static const char digits[] = "0123456789abcdef";
    unsigned int i;
    printf("{\"type\":\"step\",\"step\":%u,\"phase\":%u,\"target\":%u,"
           "\"status\":%u,\"pc\":%u,\"sr\":%u,\"odometer\":%u,\"asp\":%u,"
           "\"cycles_needed\":%u,\"cycles_leftover\":%u,\"io_cycle_counter\":%u,"
           "\"xflag\":%u,\"execinfo\":%u,\"trace_trickybit\":%u,"
           "\"interrupts\":[", step, phase, target, status, context->pc,
           (unsigned int)context->sr, context->odometer, context->asp,
           context->cycles_needed, context->cycles_leftover, context->io_cycle_counter,
           (unsigned int)context->xflag, (unsigned int)context->execinfo,
           (unsigned int)context->trace_trickybit);
    for (i = 0; i < 8; ++i)
        printf("%s%u", i ? "," : "", (unsigned int)context->interrupts[i]);
    printf("],\"d\":[");
    for (i = 0; i < 8; ++i)
        printf("%s%u", i ? "," : "", context->dreg[i]);
    printf("],\"a\":[");
    for (i = 0; i < 8; ++i)
        printf("%s%u", i ? "," : "", context->areg[i]);
    printf("],\"io\":[");
    for (i = 0; i < IO_SIZE; ++i)
        printf("%s%u", i ? "," : "", (unsigned int)io_bytes[i]);
    printf("],\"irq_ack_count\":%u,\"irq_last_level\":%u,\"events\":[",
           ref_irq_ack_count, ref_irq_last_level);
    for (i = 0; i < event_count; ++i)
        printf("%s[%u,%u,%u,%u]", i ? "," : "", events[i].type,
               events[i].pc, events[i].address, events[i].value);
    printf("],\"ram\":\"");
    for (i = 0; i < sizeof(Ram_68k); ++i)
    {
        unsigned int byte = Ram_68k[i ^ 1u];
        putchar(digits[byte >> 4]);
        putchar(digits[byte & 15u]);
    }
    printf("\"}\n");
    if (ferror(stdout))
        fail("could not write trace");
}

static void run_until_stop(unsigned int phase, unsigned int *step)
{
    struct S68000CONTEXT context;
    unsigned int attempts;
    ref_cpu_get_context(&context);
    for (attempts = 0; attempts < 64; ++attempts)
    {
        unsigned int before = context.odometer;
        unsigned int target, status;
        if (before >= 100000u)
            fail("unexpected cycle count");
        target = before + 1u;
        status = ref_cpu_exec((int)target);
        ref_cpu_get_context(&context);
        if (status != 0x80000000u || context.odometer <= before)
            fail("CPU did not complete the expected instruction step");
        emit_trace((*step)++, phase, target, status, &context);
        if ((context.interrupts[0] & 0x10u) != 0u)
            return;
    }
    fail("fixture did not reach STOP");
}

int main(void)
{
    static const unsigned short program[] = {
        0x7005, 0x7203, 0xd081, 0x23c0, 0x00ff, 0x0000,
        0x13fc, 0x005a, 0x0040, 0x0000,
        0x1439, 0x0040, 0x0000,
        0x33fc, 0xa55a, 0x0040, 0x0002,
        0x3639, 0x0040, 0x0002,
        0x4e72, 0x2000, 0x7a07, 0x4e72, 0x2700
    };
    static const unsigned short handler[] = {0x782a, 0x4e73};
    struct STARSCREAM_PROGRAMREGION fetch[2];
    struct STARSCREAM_DATAREGION data_end[1];
    struct S68000CONTEXT context;
    unsigned int i, step = 0;

    memset(&context, 0, sizeof(context));
    memset(fetch, 0, sizeof(fetch));
    memset(data_end, 0, sizeof(data_end));
    fetch[0].lowaddr = 0;
    fetch[0].highaddr = Rom_Size - 1u;
    fetch[0].offset = (unsigned int)(uintptr_t)Rom_Data;
    fetch[1].lowaddr = fetch[1].highaddr = 0xffffffffu;
    data_end[0].lowaddr = data_end[0].highaddr = 0xffffffffu;
    context.fetch = context.s_fetch = context.u_fetch = fetch;
    context.readbyte = context.s_readbyte = context.u_readbyte = data_end;
    context.readword = context.s_readword = context.u_readword = data_end;
    context.writebyte = context.s_writebyte = context.u_writebyte = data_end;
    context.writeword = context.s_writeword = context.u_writeword = data_end;
    context.io_cycle_counter = 0xffffffffu;

    put_long(0, 0x00ff1000u);
    put_long(4, 0x00000100u);
    put_long((0x18u + 4u) * 4u, 0x00000200u);
    for (i = 0; i < sizeof(program) / sizeof(program[0]); ++i)
        put_word(0x100u + i * 2u, program[i]);
    for (i = 0; i < sizeof(handler) / sizeof(handler[0]); ++i)
        put_word(0x200u + i * 2u, handler[i]);

    if (ref_cpu_context_size() != (int)sizeof(context))
        fail("generated CPU and C context sizes differ");
    ref_cpu_set_context(&context);
    if (ref_cpu_init() != 0 || ref_cpu_reset() != 0)
        fail("CPU initialization or reset failed");
    ref_cpu_get_context(&context);
    if (context.pc != 0x100u || context.areg[7] != 0x00ff1000u)
        fail("reset vectors were not loaded");

    printf("{\"type\":\"header\",\"format\":\"gens-m68k-micro-v1\","
           "\"baseline\":\"b948995d15034983cbf5562610586d93e0b25a9b\","
           "\"fixture\":\"synthetic-bus-irq-1\"}\n");
    run_until_stop(0, &step);
    ref_cpu_get_context(&context);
    if (context.pc != 0x12cu || context.dreg[0] != 8u || context.dreg[1] != 3u ||
        (context.dreg[2] & 0xffu) != 0x5au || (context.dreg[3] & 0xffffu) != 0xa55au ||
        io_bytes[0] != 0x5au || io_bytes[2] != 0xa5u || io_bytes[3] != 0x5au ||
        Ram_68k[0] != 0u || Ram_68k[1] != 0u ||
        Ram_68k[2] != 8u || Ram_68k[3] != 0u ||
        bus_read_byte_count == 0 || bus_read_word_count == 0 ||
        bus_write_byte_count == 0 || bus_write_word_count == 0)
        fail("arithmetic, bus callbacks, RAM, or first STOP did not match the fixture");
    if (ref_cpu_interrupt(4, -1) != 0)
        fail("interrupt request failed");
    run_until_stop(1, &step);
    ref_cpu_get_context(&context);
    if (context.pc != 0x132u || context.dreg[4] != 42u || context.dreg[5] != 7u ||
        context.areg[7] != 0x00ff1000u || ref_irq_ack_count != 1 || ref_irq_last_level != 4)
        fail("interrupt, RTE, or final STOP did not match the fixture");
    printf("{\"type\":\"end\",\"steps\":%u}\n", step);
    if (fflush(stdout) != 0 || ferror(stdout))
        fail("could not write final trace");
    return 0;
}
