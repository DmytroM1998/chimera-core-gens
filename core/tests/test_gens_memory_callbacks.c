#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "gens_headless_machine_trace.h"

extern int Init(void);
extern void FrameAdvance(uint64_t packed);
extern const char *GetMemoryCallbackScopes(void);
extern int32_t GetMemoryCallbackReplaceable(void);
extern void SetMemoryCallback(uint64_t fn);
extern int32_t AddMemoryWatch(int32_t kind, int32_t scope, uint64_t address);
extern void RemoveMemoryWatch(int32_t kind, int32_t scope, uint64_t address);
extern int64_t GetRegisterValue(int32_t i);
extern int PeekBus(int bus, int address);
extern const uint8_t *ReadBus(int bus, int64_t address, int length);
extern int16_t *GetAudio(void);
extern int GetAudioSampleCount(void);
extern int InputWasRead(void);

struct Event { unsigned int kind, size; uint32_t address, value, pc; };
static struct Event events[64];
static unsigned int event_count, calls[3], replaced, remove_self;
static int inspecting;

static uintptr_t callback(uintptr_t kind, uintptr_t scope, uintptr_t address,
                          uintptr_t value, uintptr_t size, uintptr_t replacement_address)
{
    uint32_t pc = (uint32_t)GetRegisterValue(16);
    assert(!inspecting);
    inspecting = 1;
    assert(kind < 3 && scope == 0 && replacement_address == 0);
    assert(kind == 0 ? size == 0 : size == 1 || size == 2);
    assert(address <= 0xffffff);
    assert(GetRegisterValue(36) == pc && GetRegisterValue(37) == GetRegisterValue(17));
    assert(ReadBus(0, pc, 2) != 0);
    ++calls[kind];
    if (event_count < 64)
        events[event_count++] = (struct Event){(unsigned int)kind, (unsigned int)size, (uint32_t)address, (uint32_t)value, pc};
    if (kind == 0)
    {
        assert(value == 0 && pc == address);
        if (pc == 0x202) assert(GetRegisterValue(0) == 1);
        if (pc == 0x21a) assert(GetRegisterValue(8) == 0xff0100);
        if (pc == 0x21c) assert(GetRegisterValue(1) == 0x80);
        if (pc == 0x220) assert(GetRegisterValue(2) == 0x1234);
        if (pc == 0x224) assert(GetRegisterValue(3) == UINT32_C(0x89abcdef));
        if (pc == 0x22e) assert((GetRegisterValue(17) & 0x0f) == 8);
    }
    if (kind != 0)
    {
        if (size == 1) assert(value <= 0xff);
        else assert(value <= 0xffff);
    }
    if (kind == 2 && address >= 0xff0100 && address < 0xff0110)
        assert(PeekBus(0, (int)address) == (int)(size == 1 ? value : value >> 8));
    if (kind == 2 && pc == 0x212 && address == 0xff0104 && calls[0] < 20)
        assert(PeekBus(0, 0xff0106) == 0); /* the second word has not been written yet */
    if (remove_self)
    {
        RemoveMemoryWatch(0, 0, UINT64_MAX);
        remove_self = 0;
    }
    inspecting = 0;
    return 0;
}

static uintptr_t replacement(uintptr_t a, uintptr_t b, uintptr_t c,
                             uintptr_t d, uintptr_t e, uintptr_t f)
{
    ++replaced;
    return callback(a, b, c, d, e, f);
}

static void all_watches(int add)
{
    int k;
    for (k = 0; k < 3; ++k)
        if (add) assert(AddMemoryWatch(k, 0, UINT64_MAX));
        else RemoveMemoryWatch(k, 0, UINT64_MAX);
}

static void frame_trace(FILE *f, int frame)
{
    FrameAdvance(0);
    trace_frame(f, (unsigned int)frame);
    gens_trace_pcm(f, (unsigned int)frame, GetAudio(), (size_t)GetAudioSampleCount());
    fprintf(f, "lag %d\n", InputWasRead());
}

static void expect_event(unsigned int kind, uint32_t address, uint32_t value, uint32_t pc)
{
    unsigned int i;
    for (i = 0; i < event_count; ++i)
        if (events[i].kind == kind && events[i].address == address &&
            events[i].value == value && events[i].pc == pc)
        {
            assert(events[i].size == (kind == 0 ? 0 : address == 0xff0100 || address == 0xa10003 ? 1 : 2));
            return;
        }
    fprintf(stderr, "missing event %u %06x %08x pc=%06x\n", kind, address, value, pc);
    assert(0);
}

static void expect_pair(unsigned int kind, uint32_t first, uint32_t second, uint32_t pc)
{
    unsigned int i;
    for (i = 1; i < event_count; ++i)
        if (events[i - 1].kind == kind && events[i].kind == kind &&
            events[i - 1].address == first && events[i].address == second &&
            events[i - 1].pc == pc && events[i].pc == pc)
            return;
    assert(!"word accesses were missing or out of order");
}

int main(void)
{
    FILE *off = tmpfile(), *on = tmpfile();
    int i, a, b;
    unsigned int count;
    assert(off != 0 && on != 0);
    assert(strcmp(GetMemoryCallbackScopes(), "M68K BUS") == 0);
    assert(GetMemoryCallbackReplaceable() == 0);
    assert(!AddMemoryWatch(-1, 0, 0) && !AddMemoryWatch(3, 0, 0));
    assert(!AddMemoryWatch(0, -1, 0) && !AddMemoryWatch(0, 1, 0));
    assert(!AddMemoryWatch(0, 0, 0x1000000) && !AddMemoryWatch(0, 0, UINT64_MAX - 1));
    {
        int kind;
        for (kind = 0; kind < 3; ++kind)
        {
            for (i = 0; i < 64; ++i) assert(AddMemoryWatch(kind, 0, (uint64_t)i));
            assert(AddMemoryWatch(kind, 0, 0));
            assert(!AddMemoryWatch(kind, 0, 64));
        }
    }
    all_watches(1);
    assert(!AddMemoryWatch(0, 0, 65)); /* a wildcard does not swallow exact registrations */
    all_watches(0);
    {
        int kind;
        for (kind = 0; kind < 3; ++kind)
            for (i = 0; i < 64; ++i) RemoveMemoryWatch(kind, 0, (uint64_t)i);
    }
    RemoveMemoryWatch(-1, 0, 0);
    RemoveMemoryWatch(0, 1, 0);
    RemoveMemoryWatch(0, 0, UINT64_MAX - 1);
    assert(Init());
    for (i = 0; i < 5; ++i) frame_trace(off, i);
    all_watches(1);
    SetMemoryCallback((uint64_t)(uintptr_t)callback);
    assert(Init()); /* callback and watches were registered before initialization */
    for (i = 0; i < 5; ++i) frame_trace(on, i);
    assert(calls[0] > 100 && calls[1] > 100 && calls[2] > 100);
    expect_event(2, 0xff0100, 0x80, 0x208);
    expect_event(2, 0xff0102, 0x1234, 0x20c);
    expect_event(2, 0xff0104, 0x89ab, 0x212);
    expect_event(2, 0xff0106, 0xcdef, 0x212);
    expect_event(1, 0xff0100, 0x80, 0x21a);
    expect_event(1, 0xff0102, 0x1234, 0x21c);
    expect_event(1, 0xff0104, 0x89ab, 0x220);
    expect_event(1, 0xff0106, 0xcdef, 0x220);
    expect_event(2, 0xff010e, 0xcdef, 0x22a);
    expect_event(2, 0xff010c, 0x89ab, 0x22a);
    expect_event(1, 0xa10003, 0x7f, 0x230);
    expect_event(1, 0x80, 0, 0x236);
    expect_event(1, 0x82, 0x280, 0x236);
    expect_event(2, 0xa1000a, 0xcdef, 0x23e);
    expect_event(2, 0xa10008, 0x89ab, 0x23e);
    expect_event(1, 0x100, 0x5345, 0x240);
    expect_event(1, 0x102, 0x4741, 0x240);
    expect_pair(2, 0xff0104, 0xff0106, 0x212);
    expect_pair(1, 0xff0104, 0xff0106, 0x220);
    expect_pair(2, 0xff010e, 0xff010c, 0x22a);
    expect_pair(2, 0xa1000a, 0xa10008, 0x23e);
    expect_pair(1, 0x100, 0x102, 0x240);
    rewind(off); rewind(on);
    do { a = fgetc(off); b = fgetc(on); assert(a == b); } while (a != EOF);
    fclose(off); fclose(on);

    all_watches(0);
    memset(calls, 0, sizeof(calls));
    FrameAdvance(0);
    assert(calls[0] == 0 && calls[1] == 0 && calls[2] == 0);
    assert(AddMemoryWatch(0, 0, UINT64_MAX));
    assert(AddMemoryWatch(0, 0, 0x202));
    assert(AddMemoryWatch(0, 0, 0x202));
    RemoveMemoryWatch(0, 0, UINT64_MAX);
    FrameAdvance(0);
    assert(calls[0] > 0 && calls[1] == 0 && calls[2] == 0);
    count = calls[0];
    RemoveMemoryWatch(0, 0, UINT64_MAX);
    RemoveMemoryWatch(0, 0, 0x202);
    assert(AddMemoryWatch(0, 0, 0x203)); /* an instruction watch is exact, not overlapping */
    FrameAdvance(0);
    assert(calls[0] == count);
    RemoveMemoryWatch(0, 0, 0x203);
    assert(AddMemoryWatch(0, 0, 0x202));
    count = calls[0];
    SetMemoryCallback(0);
    FrameAdvance(0);
    assert(calls[0] == count);
    SetMemoryCallback((uint64_t)(uintptr_t)replacement);
    FrameAdvance(1); /* power must retain the watch */
    assert(replaced > 0 && calls[0] > count);
    count = calls[0];
    FrameAdvance(2); /* reset must retain the watch */
    assert(calls[0] > count);
    RemoveMemoryWatch(0, 0, 0x202);
    count = calls[0];
    FrameAdvance(0);
    assert(calls[0] == count);
    assert(AddMemoryWatch(0, 0, UINT64_MAX));
    remove_self = 1;
    FrameAdvance(0);
    assert(calls[0] == count + 1);
    SetMemoryCallback(0);
    puts("memory callbacks ok");
    return 0;
}
