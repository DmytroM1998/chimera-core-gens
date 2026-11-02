#include <assert.h>
#include <limits.h>
#include <stddef.h>

#include "gens_audio.h"

struct GuardedTable {
    unsigned int before;
    unsigned int table[312][2];
    unsigned int after;
};

static struct GuardedTable schedule;

static void check_schedule(int lines, int samples)
{
    int line;
    unsigned int offset;
    schedule.before = 0x12345678u;
    schedule.after = 0x87654321u;
    for (line = 0; line < lines; ++line)
    {
        schedule.table[line][0] = (unsigned int)(samples * line / lines);
        schedule.table[line][1] = (unsigned int)(samples * (line + 1) / lines) -
                                  schedule.table[line][0];
    }
    for (line = -1; line <= lines; ++line)
    {
        unsigned int expected = line + 1 >= lines ?
                                (unsigned int)samples :
                                (unsigned int)(samples * (line + 1) / lines);
        offset = 9999;
        assert(gens_audio_next_offset(line, lines, samples, schedule.table, 312, &offset)
               == GENS_AUDIO_OK);
        assert(offset == expected);
    }
    assert(schedule.before == 0x12345678u && schedule.after == 0x87654321u);
}

int main(void)
{
    unsigned int offset;
    check_schedule(262, 184);
    check_schedule(262, 368);
    check_schedule(262, 735);
    check_schedule(312, 220);
    check_schedule(312, 441);
    check_schedule(312, 882);

    offset = 123;
    assert(gens_audio_next_offset(0, 312, 882, 0, 312, &offset)
           == GENS_AUDIO_INVALID_ARGUMENT);
    assert(gens_audio_next_offset(0, 312, 882, schedule.table, 312, 0)
           == GENS_AUDIO_INVALID_ARGUMENT);
    assert(gens_audio_next_offset(-2, 312, 882, schedule.table, 312, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    assert(gens_audio_next_offset(INT_MAX, 312, 882, schedule.table, 312, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    assert(gens_audio_next_offset(0, 312, 735, schedule.table, 312, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    assert(gens_audio_next_offset(0, 312, 882, schedule.table, 311, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    schedule.table[1][0]++;
    assert(gens_audio_next_offset(0, 312, 882, schedule.table, 312, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    schedule.table[1][0]--;
    schedule.table[311][1]++;
    assert(gens_audio_next_offset(312, 312, 882, schedule.table, 312, &offset)
           == GENS_AUDIO_INVALID_TIMING);
    assert(offset == 123);
    return 0;
}
