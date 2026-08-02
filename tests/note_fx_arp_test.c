#include <assert.h>
#include "NoteFx/note_fx_arp.h"
static uint8_t arp_on(note_fx_arp_t *arp, uint8_t note, uint8_t velocity)
{
    static uint32_t token = 0U;
    return note_fx_arp_note_on(arp, note, velocity, ++token, 1U);
}
static uint8_t arp_off(note_fx_arp_t *arp, uint8_t note)
{
    return note_fx_arp_note_off(arp, 8U, 1U);
}

static void expect(note_fx_arp_t*a,note_fx_arp_style_t s,uint8_t r,uint8_t w){uint8_t n=255,v=0;assert(note_fx_arp_next(a,s,r,&n,&v));assert(n==w&&v);}
int main(void){note_fx_arp_t a,b;note_fx_arp_init(&a,123);assert(arp_on(&a,64,100));assert(arp_on(&a,60,90));assert(arp_on(&a,67,80));assert(note_fx_arp_note_on(&a,60,70,2U,1U));expect(&a,NOTE_FX_ARP_ORDER,1,64);expect(&a,NOTE_FX_ARP_ORDER,1,60);expect(&a,NOTE_FX_ARP_ORDER,1,67);a.phase=0;expect(&a,NOTE_FX_ARP_UP,2,60);expect(&a,NOTE_FX_ARP_UP,2,64);expect(&a,NOTE_FX_ARP_UP,2,67);expect(&a,NOTE_FX_ARP_UP,2,72);a.phase=0;expect(&a,NOTE_FX_ARP_DOWN,1,67);expect(&a,NOTE_FX_ARP_DOWN,1,64);a.phase=0;expect(&a,NOTE_FX_ARP_UP_DOWN,1,60);expect(&a,NOTE_FX_ARP_UP_DOWN,1,64);expect(&a,NOTE_FX_ARP_UP_DOWN,1,67);expect(&a,NOTE_FX_ARP_UP_DOWN,1,64);note_fx_arp_init(&a,77);note_fx_arp_init(&b,77);for(uint8_t n=0;n<4;n++){assert(arp_on(&a,60+n,100));assert(arp_on(&b,60+n,100));}for(uint8_t i=0;i<32;i++){uint8_t an,av,bn,bv;assert(note_fx_arp_next(&a,NOTE_FX_ARP_RANDOM,4,&an,&av));assert(note_fx_arp_next(&b,NOTE_FX_ARP_RANDOM,4,&bn,&bv));assert(an==bn&&an<=127);}uint32_t p=a.phase;assert(arp_on(&a,80,100));assert(a.phase==p);assert(arp_off(&a,61));assert(a.phase==p);note_fx_arp_init(&a,1);assert(arp_on(&a,127,1));for(uint8_t i=0;i<8;i++)expect(&a,NOTE_FX_ARP_UP,4,127);note_fx_arp_init(&a,1);for(uint8_t i=0;i<NOTE_FX_ARP_MAX_SOURCES;i++)assert(arp_on(&a,i,1));assert(!arp_on(&a,100,1));note_fx_arp_init(&a,9);assert(note_fx_arp_note_on(&a,60,100,101U,1U));assert(note_fx_arp_note_on(&a,60,110,102U,1U));assert(a.count==2U);assert(note_fx_arp_note_off(&a,101U,1U));assert(a.count==1U);assert(!note_fx_arp_note_off(&a,101U,1U));assert(note_fx_arp_note_off(&a,102U,1U));assert(a.count==0U);return 0;}
