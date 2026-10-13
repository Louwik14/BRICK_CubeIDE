#include "Harmony/master_harmony.h"

#include <stddef.h>
#include <string.h>
#include "Platform/memory_layout.h"
#include "stm32h7xx_hal.h"

static const uint16_t g_chord_masks[MASTER_CHORD_TYPE_COUNT] = {
    0x891U,0x091U,0x291U,0x4A9U,0x895U,0x095U,0x089U,0x289U,0x489U,
    0x449U,0x889U,0x48DU,0x08DU,0x491U,0x511U,0x493U,0x495U,0x499U,
    0x4D1U,0x591U,0x691U,0x4A1U,0x0A1U,0x049U,0x111U,0x295U,0x451U
};

uint16_t master_harmony_chord_mask(uint8_t chord_type)
{
    return (chord_type < MASTER_CHORD_TYPE_COUNT)
        ? g_chord_masks[chord_type] : 0U;
}

typedef struct {
    master_harmony_state_t state[12];
    uint8_t count;
} master_harmony_snapshot_t;

typedef struct {
    master_harmony_snapshot_t snapshot[2];
    volatile uint32_t active_index;
} master_harmony_publication_t;

static SEC_ATTR(".ram_seq_state_sram3") master_harmony_publication_t
    g_master_harmony;

#define MASTER_HARMONY_GATE_CAPACITY 128U
typedef struct {
    uint64_t start;
    uint64_t due_off;
    uint32_t token;
    uint8_t owner;
    uint8_t note;
    uint8_t references;
    uint8_t active;
} master_harmony_gate_t;

typedef struct {
    master_harmony_gate_t gates[MASTER_HARMONY_GATE_CAPACITY];
    master_harmony_state_t base;
    uint16_t active_count;
    uint8_t base_captured;
} master_harmony_gate_runtime_t;

static SEC_ATTR(".ram_seq_state_sram3") master_harmony_gate_runtime_t
    g_master_harmony_gate;

static uint8_t publish_notes(const uint8_t *notes, uint8_t count,
    uint32_t generation, uint64_t sample);

static uint16_t rotate_mask(uint16_t mask, uint8_t root)
{
    root %= 12U;
    return (uint16_t)(((mask << root) | (mask >> (12U - root))) & 0x0FFFU);
}

static void publish(const master_harmony_state_t *state)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    const uint32_t active = g_master_harmony.active_index & 1U;
    const uint32_t inactive = active ^ 1U;
    master_harmony_snapshot_t *const next =
        &g_master_harmony.snapshot[inactive];
    *next = g_master_harmony.snapshot[active];
    if (next->count >= 12U) {
        for (uint8_t i=1U;i<12U;++i)
            next->state[i-1U]=next->state[i];
        next->count=11U;
    }
    uint8_t insert=next->count;
    while(insert!=0U
            &&next->state[insert-1U].effective_sample
                >state->effective_sample){
        next->state[insert]=next->state[insert-1U];
        --insert;
    }
    next->state[insert]=*state;
    ++next->count;
    __DMB();
    g_master_harmony.active_index=inactive;
    __DMB();
    __set_PRIMASK(primask);
}

void master_harmony_reset(uint32_t generation, uint64_t sample)
{
    const master_harmony_state_t state = {
        .effective_sample=sample,.pattern_generation=generation,
        .root=0U,.type=MASTER_CHORD_MAJOR,.bass=0U,.valid=0U};
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    g_master_harmony_gate=(master_harmony_gate_runtime_t){0};
    const uint32_t inactive=(g_master_harmony.active_index&1U)^1U;
    master_harmony_snapshot_t *const next=
        &g_master_harmony.snapshot[inactive];
    next->count=1U;next->state[0]=state;
    __DMB();g_master_harmony.active_index=inactive;__DMB();
    __set_PRIMASK(primask);
}

static void gate_publish_effective(uint64_t sample)
{
    uint16_t effective_count=0U;
    for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
        const master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
        if(gate->active!=0U&&gate->start<=sample&&gate->due_off>sample)
            ++effective_count;
    }
    if(effective_count==0U){
        if(g_master_harmony_gate.base_captured!=0U){
            master_harmony_state_t restored=g_master_harmony_gate.base;
            restored.effective_sample=sample;
            publish(&restored);
        }
        if(g_master_harmony_gate.active_count==0U)
            g_master_harmony_gate.base_captured=0U;
        return;
    }
    uint8_t notes[128U];uint8_t count=0U;
    for(uint16_t note=0U;note<128U;++note){
        for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
            const master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
            if(gate->active!=0U&&gate->start<=sample&&gate->due_off>sample
                    &&gate->note==(uint8_t)note){notes[count++]=(uint8_t)note;break;}
        }
    }
    uint32_t generation=g_master_harmony_gate.base.pattern_generation;
    for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i)
        if(g_master_harmony_gate.gates[i].active!=0U
                &&g_master_harmony_gate.gates[i].start<=sample
                &&g_master_harmony_gate.gates[i].due_off>sample){
            master_harmony_state_t current={0};
            if(master_harmony_resolve_at(sample,&current)!=0U)
                generation=current.pattern_generation;
            break;
        }
    (void)publish_notes(notes,count,generation,sample);
}

uint8_t master_harmony_gate_note_on(master_harmony_gate_owner_t owner,
    uint32_t token,uint8_t note,uint32_t generation,uint64_t sample,
    uint64_t due_off)
{
    if(owner>=MASTER_HARMONY_GATE_OWNER_COUNT||token==0U||note>=128U
            ||due_off<=sample)return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    int16_t free_index=-1;
    for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
        master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
        if(gate->active!=0U&&gate->owner==(uint8_t)owner
                &&gate->token==token&&gate->note==note){
            if(gate->references<UINT8_MAX)++gate->references;
            if(due_off>gate->due_off)gate->due_off=due_off;
            __set_PRIMASK(primask);return 1U;
        }
        if(gate->active==0U&&free_index<0)free_index=(int16_t)i;
    }
    if(free_index<0){__set_PRIMASK(primask);return 0U;}
    if(g_master_harmony_gate.active_count==0U){
        g_master_harmony_gate.base=(master_harmony_state_t){
            .effective_sample=sample,.pattern_generation=generation};
        (void)master_harmony_resolve_at(sample,&g_master_harmony_gate.base);
        g_master_harmony_gate.base_captured=1U;
    }
    g_master_harmony_gate.gates[(uint16_t)free_index]=(master_harmony_gate_t){
        .start=sample,.due_off=due_off,.token=token,.owner=(uint8_t)owner,.note=note,
        .references=1U,.active=1U};
    ++g_master_harmony_gate.active_count;
    gate_publish_effective(sample);
    __set_PRIMASK(primask);return 1U;
}

uint8_t master_harmony_gate_note_off(master_harmony_gate_owner_t owner,
    uint32_t token,uint64_t sample)
{
    if(owner>=MASTER_HARMONY_GATE_OWNER_COUNT||token==0U)return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
        master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
        if(gate->active==0U||gate->owner!=(uint8_t)owner||gate->token!=token)continue;
        if(gate->references>1U){--gate->references;__set_PRIMASK(primask);return 1U;}
        *gate=(master_harmony_gate_t){0};--g_master_harmony_gate.active_count;
        gate_publish_effective(sample);__set_PRIMASK(primask);return 1U;
    }
    __set_PRIMASK(primask);return 0U;
}

void master_harmony_gate_advance(uint64_t sample)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    for(;;){
        uint64_t due=UINT64_MAX;
        for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
            const master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
            if(gate->active!=0U&&gate->due_off<=sample&&gate->due_off<due)due=gate->due_off;
        }
        if(due==UINT64_MAX)break;
        for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
            master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
            if(gate->active!=0U&&gate->due_off==due){
                *gate=(master_harmony_gate_t){0};--g_master_harmony_gate.active_count;
            }
        }
        gate_publish_effective(due);
    }
    __set_PRIMASK(primask);
}

void master_harmony_gate_cancel_owner(master_harmony_gate_owner_t owner,
    uint64_t sample)
{
    if(owner>=MASTER_HARMONY_GATE_OWNER_COUNT)return;
    const uint32_t primask=__get_PRIMASK();__disable_irq();uint8_t changed=0U;
    for(uint16_t i=0U;i<MASTER_HARMONY_GATE_CAPACITY;++i){
        master_harmony_gate_t *const gate=&g_master_harmony_gate.gates[i];
        if(gate->active!=0U&&gate->owner==(uint8_t)owner){
            *gate=(master_harmony_gate_t){0};--g_master_harmony_gate.active_count;changed=1U;
        }
    }
    if(changed!=0U)gate_publish_effective(sample);
    __set_PRIMASK(primask);
}

void master_harmony_gate_cancel_all(uint64_t sample)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    if(g_master_harmony_gate.active_count!=0U){
        master_harmony_state_t restored=g_master_harmony_gate.base;
        restored.effective_sample=sample;
        const uint32_t inactive=(g_master_harmony.active_index&1U)^1U;
        master_harmony_snapshot_t *const next=
            &g_master_harmony.snapshot[inactive];
        next->count=1U;next->state[0]=restored;
        __DMB();g_master_harmony.active_index=inactive;__DMB();
        memset(g_master_harmony_gate.gates,0,sizeof(g_master_harmony_gate.gates));
        g_master_harmony_gate.active_count=0U;
        g_master_harmony_gate.base_captured=0U;
    }
    __set_PRIMASK(primask);
}

static uint8_t publish_notes(const uint8_t *notes, uint8_t count,
    uint32_t generation, uint64_t sample)
{
    if (notes == NULL || count == 0U) return 0U;
    master_harmony_state_t previous={0};
    (void)master_harmony_resolve_at(sample,&previous);
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
        int16_t best=INT16_MIN; uint8_t best_root=0U,best_type=0U,found=0U;
        for (uint8_t root=0U;root<12U;++root) for (uint8_t type=0U;
                type<MASTER_CHORD_TYPE_COUNT;++type) {
            if (rotate_mask(g_chord_masks[type],root)!=mask) continue;
            int16_t score=(root==next.bass)?8:0;
            if (previous.valid && root==previous.root) score+=4;
            if (previous.valid && type==previous.type) score+=2;
            score-=(int16_t)type;
            if (found==0U||score>best) {
                best=score;best_root=root;best_type=type;found=1U;
            }
        }
        if (found==0U) return 0U;
        next.root=best_root;next.type=best_type;
    }
    publish(&next);
    return 1U;
}

uint8_t master_harmony_get_current(master_harmony_state_t *out)
{
    if(out==NULL)return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    const uint32_t active=g_master_harmony.active_index&1U;__DMB();
    const master_harmony_snapshot_t *const snapshot=
        &g_master_harmony.snapshot[active];
    const uint8_t count=snapshot->count;
    *out=(count!=0U)?snapshot->state[count-1U]
                   :(master_harmony_state_t){0};
    __DMB();__set_PRIMASK(primask);
    return out->valid;
}

uint8_t master_harmony_resolve_at(uint64_t sample, master_harmony_state_t *out)
{
    if (out==NULL) return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    const uint32_t active=g_master_harmony.active_index&1U;__DMB();
    const master_harmony_snapshot_t *const snapshot=
        &g_master_harmony.snapshot[active];
    const uint8_t count=snapshot->count;*out=(master_harmony_state_t){0};
    for(uint8_t i=0U;i<count;++i)
        if(snapshot->state[i].effective_sample<=sample)
            *out=snapshot->state[i];
    __DMB();__set_PRIMASK(primask);
    return out->valid;
}
