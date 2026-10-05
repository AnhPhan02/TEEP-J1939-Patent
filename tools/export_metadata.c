/* Read the firmware database directly so host exports cannot silently drift. */
#include "j1939_signal_definitions.h"
#include "j1939_pgn_timing.h"
#include "j1939_tx_scheduler.h"
#include <stdio.h>

static void json_string(const char *s)
{
    putchar('"');
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') putchar('\\');
        if ((unsigned char)*s < 32) printf("\\u%04x", (unsigned char)*s);
        else putchar(*s);
    }
    putchar('"');
}

int main(void)
{
    printf("{\"max_signals\":%u,\"max_pgns\":%u,\"signals\":[",
           PATTERN_GEN_MAX_SIGNALS, J1939_SCHED_MAX_PGNS);
    for (unsigned i = 0; i < J1939_Signal_Count; ++i) {
        const J1939_Signal_Definition_t *s = &J1939_Signal_Database[i];
        J1939_PGN_Timing_t t;
        J1939_PGN_Get_Timing(s->pgn, &t);
        if (i) putchar(',');
        printf("{\"spn\":%u,\"pgn\":%u,\"name\":", s->spn, s->pgn);
        json_string(s->name);
        printf(",\"unit\":"); json_string(s->unit);
        printf(",\"start_bit\":%u,\"bits\":%u,\"resolution\":%.9g,\"offset\":%.9g,"
               "\"min\":%.9g,\"max\":%.9g,\"priority\":%u,\"source_address\":%u}",
               (s->start_byte - 1u) * 8u + s->start_bit - 1u, s->num_bits,
               (double)s->resolution, (double)s->offset,
               (double)s->min_physical, (double)s->max_physical,
               t.priority, t.source_address);
    }
    puts("]}");
    return 0;
}
