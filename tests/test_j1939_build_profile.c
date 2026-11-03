#include <assert.h>
#include <stdio.h>

#include "j1939_build_profile.h"

int main(void)
{
#if ENABLE_179_SCALAR
    assert(J1939_PROFILE_SIGNAL_CAPACITY == 179u);
    assert(J1939_PROFILE_PGN_CAPACITY == 60u);
    assert(J1939_PROFILE_IS_EXPERIMENTAL == 1u);
#elif ENABLE_100_SPNS
    assert(J1939_PROFILE_SIGNAL_CAPACITY == 100u);
    assert(J1939_PROFILE_PGN_CAPACITY == 60u);
    assert(J1939_PROFILE_IS_EXPERIMENTAL == 1u);
#else
    assert(J1939_PROFILE_SIGNAL_CAPACITY == 40u);
    assert(J1939_PROFILE_PGN_CAPACITY == 11u);
    assert(J1939_PROFILE_IS_EXPERIMENTAL == 0u);
#endif

    assert(PATTERN_GEN_MAX_SIGNALS == J1939_PROFILE_SIGNAL_CAPACITY);
    assert(J1939_SCHED_MAX_PGNS == J1939_PROFILE_PGN_CAPACITY);
    /* Waveform pool size does not follow the signal capacity (D-004);
     * constant-only builds have no pool at all. */
    assert(J1939_WAVEFORM_POOL_CAPACITY == (ENABLE_WAVEFORM ? 40u : 0u));

    printf("%s build-profile test: PASS\n", J1939_PROFILE_NAME);
    return 0;
}
