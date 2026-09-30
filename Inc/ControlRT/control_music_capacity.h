#ifndef CONTROL_MUSIC_CAPACITY_H
#define CONTROL_MUSIC_CAPACITY_H

/* Source-driven work and autonomous temporal NOTE-FX work are independent
 * producers in one 64-frame CONTROL horizon.  Static NOTE-FX admission keeps
 * each producer below the 64-output product polyphony.  A transition can
 * require the old output STOP followed by the new output START. */
#define CONTROL_MUSIC_SOURCE_MAX_HORIZON_BURST \
    256U
#define CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_OUTPUTS_PER_HORIZON \
    64U
#define CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_ACTIONS_PER_HORIZON \
    (2U * CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_OUTPUTS_PER_HORIZON)
#define CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST \
    (CONTROL_MUSIC_SOURCE_MAX_HORIZON_BURST \
        + CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_ACTIONS_PER_HORIZON)
#define CONTROL_MUSIC_EXTERNAL_STAGING_CAPACITY 128U

_Static_assert(CONTROL_MUSIC_SOURCE_MAX_HORIZON_BURST == 256U,
               "source music horizon proof changed");
_Static_assert(CONTROL_MUSIC_NOTE_FX_MAX_TEMPORAL_ACTIONS_PER_HORIZON == 128U,
               "temporal Note FX horizon proof changed");
_Static_assert(CONTROL_MUSIC_INTERNAL_MAX_HORIZON_BURST == 384U,
               "complete internal music horizon proof changed");

#endif
