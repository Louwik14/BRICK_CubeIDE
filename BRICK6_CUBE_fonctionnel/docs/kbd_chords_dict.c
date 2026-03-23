/**
 * @file kbd_chords_dict.c
 * @brief Implémentation du dictionnaire d'accords + utilitaires de scale/transpose (inspiration Orchid).
 * @ingroup ui_apps
 */

#include "kbd_chords_dict.h"
#include <stddef.h>
#include <string.h>

typedef struct {
  uint8_t mask;
  const uint8_t *intervals;
  uint8_t count;
} kbd_chord_component_t;

/* Triades */
static const uint8_t TRIAD_MAJOR[] = {0, 4, 7};
static const uint8_t TRIAD_MINOR[] = {0, 3, 7};
static const uint8_t TRIAD_SUS4[]  = {0, 5, 7};
static const uint8_t TRIAD_DIM[]   = {0, 3, 6};

static const kbd_chord_component_t k_chord_bases[] = {
  { KBD_CH_BASE_MAJOR, TRIAD_MAJOR, (uint8_t)(sizeof(TRIAD_MAJOR) / sizeof(TRIAD_MAJOR[0])) },
  { KBD_CH_BASE_MINOR, TRIAD_MINOR, (uint8_t)(sizeof(TRIAD_MINOR) / sizeof(TRIAD_MINOR[0])) },
  { KBD_CH_BASE_SUS4,  TRIAD_SUS4,  (uint8_t)(sizeof(TRIAD_SUS4)  / sizeof(TRIAD_SUS4[0]))  },
  { KBD_CH_BASE_DIM,   TRIAD_DIM,   (uint8_t)(sizeof(TRIAD_DIM)   / sizeof(TRIAD_DIM[0]))   },
};

/* Extensions */
static const uint8_t EXT_7TH[]   = {10};
static const uint8_t EXT_MAJ7[]  = {11};
static const uint8_t EXT_6TH[]   = {9};
static const uint8_t EXT_9TH[]   = {14};

static const kbd_chord_component_t k_chord_exts[] = {
  { KBD_CH_EXT_7,    EXT_7TH,  (uint8_t)(sizeof(EXT_7TH)  / sizeof(EXT_7TH[0]))  },
  { KBD_CH_EXT_MAJ7, EXT_MAJ7, (uint8_t)(sizeof(EXT_MAJ7) / sizeof(EXT_MAJ7[0])) },
  { KBD_CH_EXT_6,    EXT_6TH,  (uint8_t)(sizeof(EXT_6TH)  / sizeof(EXT_6TH[0]))  },
  { KBD_CH_EXT_9,    EXT_9TH,  (uint8_t)(sizeof(EXT_9TH)  / sizeof(EXT_9TH[0]))  },
};

/* Utils set/tri */
static inline void add_unique(uint8_t *buf, uint8_t *n, uint8_t v, uint8_t maxn){
  if (*n >= maxn) return;
  for (uint8_t i=0;i<*n;++i) if (buf[i]==v) return;
  buf[(*n)++] = v;
}
static void add_all(const uint8_t *src, uint8_t m, uint8_t *dst, uint8_t *n, uint8_t maxn){
  for (uint8_t i=0;i<m;++i) add_unique(dst,n,src[i],maxn);
}
static void isort_u8(uint8_t *a, uint8_t n){
  for(uint8_t i=1;i<n;++i){ uint8_t k=a[i],j=i; while(j>0 && a[j-1]>k){a[j]=a[j-1];--j;} a[j]=k; }
}

static void add_components(uint8_t chord_mask,
                           const kbd_chord_component_t *components,
                           size_t component_count,
                           uint8_t *intervals,
                           uint8_t *count){
  for (size_t i = 0; i < component_count; ++i) {
    const kbd_chord_component_t *comp = &components[i];
    if ((chord_mask & comp->mask) != 0u) {
      add_all(comp->intervals, comp->count, intervals, count, 12);
    }
  }
}

/* API accords */
bool kbd_chords_dict_build(uint8_t chord_mask, uint8_t *intervals, uint8_t *count){
  if (!intervals || !count) return false;
  *count = 0;

  const uint8_t bases = (uint8_t)(chord_mask & KBD_CH_MASK_BASES);
  const uint8_t exts  = (uint8_t)(chord_mask & KBD_CH_MASK_EXTS);

  if (bases == 0) return false; /* extensions seules → invalide */

  add_components(bases, k_chord_bases,
                 sizeof(k_chord_bases) / sizeof(k_chord_bases[0]),
                 intervals, count);

  add_components(exts, k_chord_exts,
                 sizeof(k_chord_exts) / sizeof(k_chord_exts[0]),
                 intervals, count);

  isort_u8(intervals, *count);
  return true;
}

/* API gammes (Note Zone 8 slots) */
static const int8_t k_kbd_scale_offsets[KBD_SCALE_COUNT][KBD_SCALE_SLOT_COUNT] = {
  [KBD_SCALE_ID_MAJOR]      = {0, 2, 4, 5, 7, 9, 11, 12},
  [KBD_SCALE_ID_NAT_MINOR]  = {0, 2, 3, 5, 7, 8, 10, 12},
  [KBD_SCALE_ID_DORIAN]     = {0, 2, 3, 5, 7, 9, 10, 12},
  [KBD_SCALE_ID_MIXOLYDIAN] = {0, 2, 4, 5, 7, 9, 10, 12},
  [KBD_SCALE_ID_PENT_MAJOR] = {0, 2, 4, 7, 9, 12, 14, 16},
  [KBD_SCALE_ID_PENT_MINOR] = {0, 3, 5, 7, 10, 12, 15, 17},
  [KBD_SCALE_ID_CHROMATIC]  = {0, 1, 2, 3, 4, 5, 6, 12},
};

int8_t kbd_scale_slot_semitone_offset(uint8_t scale_id, uint8_t slot){
  const uint8_t safe_scale = (scale_id < KBD_SCALE_COUNT) ? scale_id : KBD_SCALE_ID_MAJOR;
  const uint8_t safe_slot = (uint8_t)(slot % KBD_SCALE_SLOT_COUNT);
  return k_kbd_scale_offsets[safe_scale][safe_slot];
}

#ifdef TEST
static bool arr_eq(const uint8_t *a,const uint8_t *b,uint8_t n){ return memcmp(a,b,n)==0; }
bool kbd_chords_dict_selftest(void){
  { uint8_t iv[12],n; if (kbd_chords_dict_build(KBD_CH_EXT_7,iv,&n)) return false; }
  { uint8_t iv[12],n; if (!kbd_chords_dict_build(KBD_CH_BASE_MAJOR,iv,&n)) return false;
    const uint8_t exp[]={0,4,7}; if (n!=3||!arr_eq(iv,exp,3)) return false; }
  { uint8_t iv[12],n; if (!kbd_chords_dict_build(KBD_CH_BASE_MINOR|KBD_CH_EXT_7,iv,&n)) return false;
    const uint8_t exp[]={0,3,7,10}; if (n!=4||!arr_eq(iv,exp,4)) return false; }
  { uint8_t iv[12],n; if (!kbd_chords_dict_build(KBD_CH_BASE_MAJOR|KBD_CH_EXT_MAJ7|KBD_CH_EXT_9,iv,&n)) return false;
    const uint8_t exp[]={0,4,7,11,14}; if (n!=5||!arr_eq(iv,exp,5)) return false; }
  return true;
}
#endif
