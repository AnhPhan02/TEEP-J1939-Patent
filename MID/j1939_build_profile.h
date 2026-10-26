/*
 * =============================================================================
 * FILE : j1939_build_profile.h
 * WHAT : Compile-time capacity profiles. Capacity is not a feature-completion
 *        claim; catalog coverage, timing, control keys and transport support
 *        have separate acceptance gates.
 * =============================================================================
 */

#ifndef J1939_BUILD_PROFILE_H
#define J1939_BUILD_PROFILE_H

#ifndef ENABLE_100_SPNS
#define ENABLE_100_SPNS  0
#endif

#if ((ENABLE_100_SPNS != 0) && (ENABLE_100_SPNS != 1))
#error "ENABLE_100_SPNS must be 0 or 1"
#endif

#if ENABLE_100_SPNS
#define J1939_PROFILE_NAME                    "100-SPN experimental capacity"
#define J1939_PROFILE_SIGNAL_CAPACITY         100u
#define J1939_PROFILE_PGN_CAPACITY             60u
#define J1939_PROFILE_IS_EXPERIMENTAL           1u
#else
#define J1939_PROFILE_NAME                    "40-SPN production"
#define J1939_PROFILE_SIGNAL_CAPACITY          40u
#define J1939_PROFILE_PGN_CAPACITY             11u
#define J1939_PROFILE_IS_EXPERIMENTAL           0u
#endif

/* Keep the old capacity names as the module-facing interface. Defining either
 * one to a different value is rejected so a binary cannot be mislabeled as a
 * validated profile. */
#ifndef PATTERN_GEN_MAX_SIGNALS
#define PATTERN_GEN_MAX_SIGNALS  J1939_PROFILE_SIGNAL_CAPACITY
#endif

#ifndef J1939_SCHED_MAX_PGNS
#define J1939_SCHED_MAX_PGNS     J1939_PROFILE_PGN_CAPACITY
#endif

#if PATTERN_GEN_MAX_SIGNALS != J1939_PROFILE_SIGNAL_CAPACITY
#error "PATTERN_GEN_MAX_SIGNALS does not match the selected J1939 profile"
#endif

#if J1939_SCHED_MAX_PGNS != J1939_PROFILE_PGN_CAPACITY
#error "J1939_SCHED_MAX_PGNS does not match the selected J1939 profile"
#endif

#endif /* J1939_BUILD_PROFILE_H */
