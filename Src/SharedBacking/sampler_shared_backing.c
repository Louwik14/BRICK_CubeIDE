#include "IPC/audio_wavetable_registry_contract.h"
#include "IPC/multi_sample_audio_projection_contract.h"
#include "IPC/sample_classic_audio_projection_contract.h"
#include "IPC/rec_source_contract.h"
#include "IPC/sampler_ram_audio_projection_contract.h"
#include "IPC/sampler_ram_playhead_contract.h"
#include "Platform/memory_layout.h"

/* Physical storage only.  Initialization and publication remain in the
 * STORAGE/AUDIO owners declared by the contracts above. */
AUDIO_STATE_SHARED_SDRAM sample_classic_audio_source_t
    g_sample_classic_audio_source[SAMPLE_CLASSIC_CAPACITY];
AUDIO_STATE_SHARED_SDRAM rec_source_projection_t g_rec_source_projection;

D2_IPC multi_audio_instrument_t
    g_multi_audio_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
AUDIO_SHARED_MULTI_SDRAM multi_audio_zone_t
    g_multi_audio_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
AUDIO_SHARED_MULTI_SDRAM multi_sample_audio_source_t
    g_multi_audio_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];

AUDIO_M7_PRIVATE_SDRAM sampler_ram_audio_slot_t
    g_sampler_ram_audio_slots[SAMPLER_RAM_AUDIO_SLOT_COUNT];
AUDIO_M7_PRIVATE_SDRAM volatile uint16_t
    g_sampler_ram_audio_global_to_slot[SAMPLER_RAM_AUDIO_SLOT_COUNT];

D2_IPC sampler_ram_playhead_slot_t
    g_sampler_ram_playhead[BRICK_ENTITY_CAPACITY];

AUDIO_M7_PRIVATE_SDRAM audio_wavetable_registry_slot_t
    g_audio_wavetable_registry[WAVETABLE_POOL_MAX_SLOTS];

_Static_assert(sizeof(sample_classic_audio_snapshot_t) == 28U,
               "Classic snapshot ABI changed");
_Static_assert(sizeof(sample_classic_audio_source_t) == 60U,
               "Classic source ABI changed");
_Static_assert(sizeof(multi_audio_instrument_t) == 16U,
               "Multi instrument ABI changed");
_Static_assert(sizeof(multi_audio_zone_t) == 8U,
               "Multi zone ABI changed");
_Static_assert(sizeof(multi_sample_audio_source_t) == 40U,
               "Multi source ABI changed");
_Static_assert(sizeof(sampler_ram_audio_slot_t) == 36U,
               "RAM projection slot layout changed");
_Static_assert(sizeof(audio_wavetable_registry_slot_t) == 188U,
               "Wavetable registry slot layout changed");
