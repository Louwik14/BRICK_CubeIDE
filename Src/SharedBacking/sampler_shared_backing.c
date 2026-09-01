#include "IPC/audio_wavetable_registry_contract.h"
#include "IPC/multi_sample_audio_projection_contract.h"
#include "IPC/sample_classic_audio_projection_contract.h"
#include "IPC/sampler_ram_audio_projection_contract.h"
#include "Platform/memory_layout.h"
#include "Sampler/sample_page_cache_shared_contract.h"
#include "Sampler/sample_page_lease.h"

/* Physical storage only.  Initialization and publication remain in the
 * STORAGE/AUDIO owners declared by the contracts above. */
D2_IPC sample_page_lease_t
    g_sample_page_leases[SAMPLE_PAGE_LEASE_SLOT_COUNT];

CONTROL_STREAM_META_SDRAM sample_page_shared_descriptor_t
    g_sample_page_shared_descriptor[SAMPLE_PAGE_MAX_COUNT];
AUDIO_SHARED_PAGE_PAYLOAD_SDRAM float g_sample_page_shared_data
    [SAMPLE_PAGE_MAX_COUNT][SAMPLE_PAGE_SLOT_FLOAT_CAPACITY];
D2_IPC volatile uint16_t
    g_sample_page_shared_last_slot[SAMPLE_PAGE_CACHE_MAX_SAMPLES];
CONTROL_STREAM_INDEX_SDRAM sample_page_shared_index_entry_t
    g_sample_page_shared_index[SAMPLE_PAGE_INDEX_SIZE];

D2_IPC sample_classic_audio_source_t
    g_sample_classic_audio_source[SAMPLE_CLASSIC_CAPACITY];

D2_IPC multi_audio_instrument_t
    g_multi_audio_instruments[MULTI_SAMPLE_POOL_MAX_INSTRUMENTS];
AUDIO_SHARED_MULTI_SDRAM multi_audio_zone_t
    g_multi_audio_zones[MULTI_SAMPLE_POOL_MAX_ZONES];
AUDIO_SHARED_MULTI_SDRAM multi_sample_audio_source_t
    g_multi_audio_samples[MULTI_SAMPLE_POOL_MAX_SAMPLES];

AUDIO_SHARED_REGISTRY_SDRAM sampler_ram_audio_slot_t
    g_sampler_ram_audio_slots[SAMPLER_RAM_AUDIO_SLOT_COUNT];
D2_IPC volatile uint16_t
    g_sampler_ram_audio_global_to_slot[SAMPLER_RAM_AUDIO_SLOT_COUNT];

AUDIO_SHARED_REGISTRY_SDRAM audio_wavetable_registry_slot_t
    g_audio_wavetable_registry[WAVETABLE_POOL_MAX_SLOTS];

_Static_assert(sizeof(sample_page_shared_descriptor_t) == 56U,
               "Page descriptor ABI changed");
_Static_assert(sizeof(sample_page_shared_index_entry_t) == 12U,
               "Page index ABI changed");
_Static_assert(sizeof(sample_classic_audio_source_t) == 48U,
               "Classic source ABI changed");
_Static_assert(sizeof(multi_audio_instrument_t) == 16U,
               "Multi instrument ABI changed");
_Static_assert(sizeof(multi_audio_zone_t) == 8U,
               "Multi zone ABI changed");
_Static_assert(sizeof(multi_sample_audio_source_t) == 60U,
               "Multi source ABI changed");
_Static_assert(sizeof(sampler_ram_audio_slot_t) == 48U,
               "RAM projection slot ABI changed");
_Static_assert(sizeof(audio_wavetable_registry_slot_t)
                   == sizeof(audio_wavetable_descriptor_t) + 8U,
               "Wavetable registry ABI changed");
