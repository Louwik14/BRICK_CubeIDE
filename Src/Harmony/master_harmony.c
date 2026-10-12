#include "Harmony/master_harmony.h"

#include <stddef.h>
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

static const uint16_t g_chord_masks[MASTER_CHORD_TYPE_COUNT] = {
    0x891U,0x091U,0x291U,0x4A9U,0x895U,0x095U,0x089U,0x289U,0x489U,
    0x449U,0x889U,0x48DU,0x08DU,0x491U,0x511U,0x493U,0x495U,0x499U,
    0x4D1U,0x591U,0x691U,0x4A1U,0x0A1U,0x049U,0x111U,0x295U,0x451U
};

typedef struct {
    volatile uint32_t sequence;
    master_harmony_state_t state[12];
    uint8_t count;
} master_harmony_publication_t;

static SEC_ATTR(".ram_seq_state_sram3") master_harmony_publication_t
    g_master_harmony;

static uint16_t rotate_mask(uint16_t mask, uint8_t root)
{
    root %= 12U;
    return (uint16_t)(((mask << root) | (mask >> (12U - root))) & 0x0FFFU);
}

static void publish(const master_harmony_state_t *state)
{
    ++g_master_harmony.sequence;
    __DMB();
    if (g_master_harmony.count >= 12U) {
        for (uint8_t i=1U;i<12U;++i)
            g_master_harmony.state[i-1U]=g_master_harmony.state[i];
        g_master_harmony.count=11U;
    }
    g_master_harmony.state[g_master_harmony.count++]=*state;
    __DMB();
    ++g_master_harmony.sequence;
}

void master_harmony_reset(uint32_t generation, uint64_t sample)
{
    const master_harmony_state_t state = {
        .effective_sample=sample,.pattern_generation=generation,
        .root=0U,.type=MASTER_CHORD_MAJOR,.bass=0U,.valid=0U};
    ++g_master_harmony.sequence;__DMB();
    g_master_harmony.count=1U;g_master_harmony.state[0]=state;
    __DMB();++g_master_harmony.sequence;
}

uint8_t master_harmony_publish_notes(const uint8_t *notes, uint8_t count,
    uint32_t generation, uint64_t sample)
{
    if (notes == NULL || count == 0U) return 0U;
    const master_harmony_state_t previous = g_master_harmony.state[
        (g_master_harmony.count!=0U)?(uint8_t)(g_master_harmony.count-1U):0U];
    master_harmony_state_t next = {.effective_sample=sample,
        .pattern_generation=generation,.root=(uint8_t)(notes[0]%12U),
        .type=previous.valid ? previous.type : MASTER_CHORD_MAJOR,
        .bass=(uint8_t)(notes[0]%12U),.valid=1U};
    uint16_t mask = 0U;
    uint8_t bass_note = notes[0];
    for (uint8_t i=0U;i<count;++i) {
        mask |= (uint16_t)(1U << (notes[i]%12U));
        if (notes[i] < bass_note) bass_note=notes[i];
    }
    next.bass=(uint8_t)(bass_note%12U);
    if ((mask & (uint16_t)(mask-1U)) != 0U) {
        int16_t best=-1; uint8_t best_root=0U,best_type=0U;
        for (uint8_t root=0U;root<12U;++root) for (uint8_t type=0U;
                type<MASTER_CHORD_TYPE_COUNT;++type) {
            if (rotate_mask(g_chord_masks[type],root)!=mask) continue;
            int16_t score=(root==next.bass)?8:0;
            if (previous.valid && root==previous.root) score+=4;
            if (previous.valid && type==previous.type) score+=2;
            score-=(int16_t)type;
            if (score>best) {best=score;best_root=root;best_type=type;}
        }
        if (best<0) return 0U;
        next.root=best_root;next.type=best_type;
    }
    publish(&next);
    return 1U;
}

uint8_t master_harmony_get_current(master_harmony_state_t *out)
{
    if(out==NULL)return 0U;
    uint32_t before,after;uint8_t count;
    do{before=g_master_harmony.sequence;if((before&1U)!=0U)continue;__DMB();
       count=g_master_harmony.count;
       *out=(count!=0U)?g_master_harmony.state[count-1U]
                       :(master_harmony_state_t){0};
       __DMB();after=g_master_harmony.sequence;
    }while(before!=after||(after&1U)!=0U);
    return out->valid;
}

uint8_t master_harmony_resolve_at(uint64_t sample, master_harmony_state_t *out)
{
    if (out==NULL) return 0U;
    uint32_t before,after;uint8_t count;
    do{before=g_master_harmony.sequence;if((before&1U)!=0U)continue;__DMB();
       count=g_master_harmony.count;*out=(master_harmony_state_t){0};
       for(uint8_t i=0U;i<count;++i)
           if(g_master_harmony.state[i].effective_sample<=sample)
               *out=g_master_harmony.state[i];
       __DMB();after=g_master_harmony.sequence;
    }while(before!=after||(after&1U)!=0U);
    return out->valid;
}
