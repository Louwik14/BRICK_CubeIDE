#include "Seq/seq_engine.h"
#include "NoteFx/note_fx_engine.h"
#include "Platform/memory_layout.h"
#include "Platform/brick_media_clock.h"
#include "SD/sdmmc_async_transport.h"
#include "Seq/seq_bench_irq_probe.h"
#include "Seq/seq_occupancy_diag.h"
#include "Seq/seq_boundary_probe.h"
#include "NoteFx/note_fx_walker_probe.h"
#include "stm32h7xx.h"
#include <limits.h>
#include <string.h>

typedef enum { SLOT_FREE = 0, SLOT_WRITING, SLOT_READY, SLOT_READING } slot_state_t;
static SEQ_STATE_SDRAM seq_terminal_block_t g_terminal[SEQ_ENGINE_BLOCK_SLOTS];
static SEQ_STATE_D2 seq_engine_core_t g_core;
static volatile uint8_t g_slot_state[SEQ_ENGINE_BLOCK_SLOTS];
static volatile uint64_t g_service_now, g_publish_until;
static volatile uint64_t g_next_deadline = UINT64_MAX;
static volatile uint8_t g_pending;
static volatile uint8_t g_urgent_pending;
static int8_t g_audio_slot = -1;
static uint16_t g_audio_cursor;
static uint8_t g_audio_offset,g_audio_class;
static volatile uint16_t g_disarmed_tracks;
static uint32_t g_disarm_generation;
static volatile uint8_t g_force_stopped;
static volatile uint32_t g_missed_horizons;
static volatile uint64_t g_force_stop_sample;
static volatile uint32_t g_force_stop_epoch;
static seq_ingress_event_t g_ingress[SEQ_ENGINE_INGRESS_CAPACITY];
static volatile uint8_t g_ingress_head,g_ingress_tail,g_ingress_count;
static volatile uint8_t g_ingress_panic;
static uint64_t g_ingress_rate_window;
static uint8_t g_ingress_rate_count;
static struct {uint64_t total;uint32_t count,max,over50,over75,run50,run75,maxrun50,maxrun75;
    uint32_t histogram[32];} g_seq_perf;

#define SEQ_BOOT_BENCH_MAGIC UINT32_C(0x53514232)
#define SEQ_BOOT_BENCH_VERSION 7U
#define SEQ_BOOT_BENCH_WARMUP_BLOCKS 2048U
#define SEQ_BOOT_BENCH_ITERATIONS 8192U
#define SEQ_BOOT_BENCH_BUCKET_SHIFT 13U
#define SEQ_BOOT_BENCH_BUCKET_COUNT 512U
#define SEQ_BOOT_BENCH_STEP_SAMPLES 2400U
#define SEQ_BOOT_BENCH_LOGICAL_SOURCES 64U
#define SEQ_BOOT_BENCH_DROP_REASON_COUNT 10U

void seq_engine_drop_diag_reset(void);
void seq_engine_drop_diag_capture(uint32_t out[SEQ_BOOT_BENCH_DROP_REASON_COUNT]);

_Static_assert(NOTE_FX_SLOT_COUNT==4U,"SEQ reference bench requires four FX slots");
_Static_assert(((BRICK_ENTITY_TOP_LEVEL_COUNT-1U)*SEQ_PLAY_MAX_CAPACITY
    +BRICK_ENTITY_GROUP_CHILD_COUNT)==SEQ_BOOT_BENCH_LOGICAL_SOURCES,
    "SEQ reference bench topology changed");

CTRL_STATE __attribute__((used)) volatile seq_boot_bench_result_t g_seq_boot_bench;
#define g_seq_bench_core g_core
#define g_seq_bench_output (g_terminal[0])
SEQ_HOT_D1 static uint32_t g_seq_bench_histogram[SEQ_BOOT_BENCH_BUCKET_COUNT];
SEQ_HOT_D1 static uint32_t g_seq_bench_cpu_histogram[SEQ_BOOT_BENCH_BUCKET_COUNT];
volatile uint32_t g_seq_bench_irq_window_active;
volatile uint32_t g_seq_bench_irq_depth;
volatile uint32_t g_seq_bench_irq_started;
volatile uint32_t g_seq_bench_irq_cycles;

#define SEQ_OCCUPANCY_MAGIC UINT32_C(0x534F4331)
#define SEQ_OCCUPANCY_VERSION 1U
#define SEQ_OCCUPANCY_BURSTS_SAVED 8U
enum {SEQ_OCC_TRACE_BLOCK_START=1U,SEQ_OCC_TRACE_SEQ_START,
      SEQ_OCC_TRACE_SEQ_END,SEQ_OCC_TRACE_BLOCK_END};
typedef struct {uint32_t start,end,cpu,wall;} seq_occupancy_burst_t;
typedef struct {
 uint32_t block_index,anchor,period,total_cpu,total_wall,burst_count;
 uint32_t first_start,last_end,max_cpu,max_wall,max_gap,free_before,free_after;
 uint32_t preempt_cycles,previous_end,burst_started,segment_started,preempt_started;
 uint32_t burst_preempt,preempt_depth,preempt_sequence,saved_count,active,burst_overflow;
 seq_occupancy_burst_t burst[SEQ_OCCUPANCY_BURSTS_SAVED];
} seq_occupancy_state_t;
static SEQ_HOT_D1 seq_occupancy_state_t g_seq_occupancy_state;
SEQ_HOT_D1 __attribute__((used)) volatile uint32_t
 g_seq_occupancy_diag[SEQ_OCCUPANCY_DIAG_WORDS];
SEQ_STATE_SDRAM __attribute__((used)) volatile uint32_t
 g_seq_occupancy_trace[SEQ_OCCUPANCY_TRACE_WORDS];
static uint32_t g_seq_occupancy_trace_count,g_seq_occupancy_blocks;
static uint32_t g_seq_occupancy_gap_histogram[5];
static uint32_t g_seq_occupancy_total_bursts,g_seq_occupancy_total_preempt;
static uint32_t g_seq_occupancy_global_max_cpu,g_seq_occupancy_global_max_cpu_block;
static uint32_t g_seq_occupancy_global_max_contiguous,g_seq_occupancy_global_max_contiguous_block;
static uint32_t g_seq_occupancy_global_max_wall,g_seq_occupancy_global_max_gap;
static uint32_t g_seq_occupancy_min_slack;
static uint32_t g_seq_occupancy_start_overhead,g_seq_occupancy_end_overhead;
static uint32_t g_seq_occupancy_block_overhead;
static uint32_t g_seq_occupancy_trace_overflow;

static uint32_t seq_occupancy_period_cycles(void)
{return SystemCoreClock*SEQ_ENGINE_H743_PERIOD_SAMPLES/48000U;}
static uint32_t seq_occupancy_offset(uint32_t now)
{return now-g_seq_occupancy_state.anchor;}
static void seq_occupancy_trace_add(uint32_t type,uint32_t offset,uint32_t value)
{if(g_seq_occupancy_trace_count>=SEQ_OCCUPANCY_TRACE_ENTRIES){g_seq_occupancy_trace_overflow=1U;return;}
 const uint32_t base=g_seq_occupancy_trace_count++*4U;
 g_seq_occupancy_trace[base]=g_seq_occupancy_state.block_index;
 g_seq_occupancy_trace[base+1U]=type;g_seq_occupancy_trace[base+2U]=offset;
 g_seq_occupancy_trace[base+3U]=value;}
static void seq_occupancy_gap_add(uint32_t gap)
{uint8_t bin;if(gap<24000U)bin=0U;else if(gap<48000U)bin=1U;
 else if(gap<120000U)bin=2U;else if(gap<240000U)bin=3U;else bin=4U;
 ++g_seq_occupancy_gap_histogram[bin];
 if(gap>g_seq_occupancy_state.max_gap)g_seq_occupancy_state.max_gap=gap;
 if(gap>g_seq_occupancy_global_max_gap)g_seq_occupancy_global_max_gap=gap;}
static void seq_occupancy_save_block(uint32_t base)
{seq_occupancy_state_t*s=&g_seq_occupancy_state;
 g_seq_occupancy_diag[base]=s->block_index;g_seq_occupancy_diag[base+1U]=s->total_cpu;
 g_seq_occupancy_diag[base+2U]=s->total_wall;g_seq_occupancy_diag[base+3U]=s->burst_count;
 g_seq_occupancy_diag[base+4U]=s->first_start;g_seq_occupancy_diag[base+5U]=s->last_end;
 g_seq_occupancy_diag[base+6U]=s->max_cpu;g_seq_occupancy_diag[base+7U]=s->max_wall;
 g_seq_occupancy_diag[base+8U]=s->max_gap;g_seq_occupancy_diag[base+9U]=s->free_before;
 g_seq_occupancy_diag[base+10U]=s->free_after;g_seq_occupancy_diag[base+11U]=s->preempt_cycles;
 for(uint8_t i=0U;i<SEQ_OCCUPANCY_BURSTS_SAVED;++i){const uint32_t o=base+12U+4U*i;
  g_seq_occupancy_diag[o]=s->burst[i].start;g_seq_occupancy_diag[o+1U]=s->burst[i].end;
  g_seq_occupancy_diag[o+2U]=s->burst[i].cpu;g_seq_occupancy_diag[o+3U]=s->burst[i].wall;}}
static void seq_occupancy_publish(void)
{seq_occupancy_state_t*s=&g_seq_occupancy_state;volatile uint32_t*d=g_seq_occupancy_diag;
 d[0]=SEQ_OCCUPANCY_MAGIC;d[1]=SEQ_OCCUPANCY_VERSION;d[2]=sizeof(g_seq_occupancy_diag);
 d[3]=SystemCoreClock;d[4]=s->period;d[5]=g_seq_occupancy_blocks;
 d[6]=g_seq_occupancy_trace_count;d[7]=SEQ_OCCUPANCY_TRACE_ENTRIES;
 d[8]=g_seq_occupancy_trace_overflow;d[9]=s->block_index;
 d[10]=s->block_index;d[11]=s->total_cpu;d[12]=s->total_wall;d[13]=s->burst_count;
 d[14]=s->first_start;d[15]=s->last_end;d[16]=s->max_cpu;d[17]=s->max_wall;
 d[18]=s->max_gap;d[19]=s->free_before;d[20]=s->free_after;d[21]=s->preempt_cycles;
 d[22]=(uint32_t)(s->last_end>s->period);d[23]=s->burst_overflow;
 for(uint8_t i=0U;i<5U;++i)d[24U+i]=g_seq_occupancy_gap_histogram[i];
 d[29]=g_seq_occupancy_global_max_cpu;d[30]=g_seq_occupancy_global_max_cpu_block;
 d[31]=g_seq_occupancy_global_max_contiguous;d[32]=g_seq_occupancy_global_max_contiguous_block;
 d[33]=g_seq_occupancy_global_max_wall;d[34]=g_seq_occupancy_global_max_gap;
 d[35]=g_seq_occupancy_min_slack;d[36]=g_seq_occupancy_total_bursts;
 d[37]=g_seq_occupancy_total_preempt;d[38]=g_seq_occupancy_start_overhead;
 d[39]=g_seq_occupancy_end_overhead;d[40]=g_seq_occupancy_block_overhead;}
void seq_occupancy_reset(void)
{memset(&g_seq_occupancy_state,0,sizeof(g_seq_occupancy_state));
 memset((void*)g_seq_occupancy_diag,0,sizeof(g_seq_occupancy_diag));
 memset((void*)g_seq_occupancy_trace,0,sizeof(g_seq_occupancy_trace));
 memset(g_seq_occupancy_gap_histogram,0,sizeof(g_seq_occupancy_gap_histogram));
 g_seq_occupancy_trace_count=0U;g_seq_occupancy_blocks=0U;
 g_seq_occupancy_total_bursts=0U;g_seq_occupancy_total_preempt=0U;
 g_seq_occupancy_global_max_cpu=0U;g_seq_occupancy_global_max_contiguous=0U;
 g_seq_occupancy_global_max_wall=0U;g_seq_occupancy_global_max_gap=0U;
 g_seq_occupancy_global_max_cpu_block=0U;g_seq_occupancy_global_max_contiguous_block=0U;
 g_seq_occupancy_min_slack=UINT32_MAX;g_seq_occupancy_start_overhead=0U;
 g_seq_occupancy_end_overhead=0U;g_seq_occupancy_block_overhead=0U;
 g_seq_occupancy_trace_overflow=0U;seq_occupancy_publish();}
void seq_occupancy_block_start(uint32_t block_index,uint32_t anchor_cycle)
{const uint32_t measure=DWT->CYCCNT;
 if(g_seq_occupancy_state.active){const uint32_t actual_period=
   anchor_cycle-g_seq_occupancy_state.anchor;
  g_seq_occupancy_state.period=actual_period;seq_occupancy_block_end();}
 memset(&g_seq_occupancy_state,0,sizeof(g_seq_occupancy_state));
 g_seq_occupancy_state.block_index=block_index;g_seq_occupancy_state.anchor=anchor_cycle;
 g_seq_occupancy_state.period=seq_occupancy_period_cycles();g_seq_occupancy_state.active=1U;
 seq_occupancy_trace_add(SEQ_OCC_TRACE_BLOCK_START,0U,g_seq_occupancy_state.period);
 const uint32_t overhead=DWT->CYCCNT-measure;if(overhead>g_seq_occupancy_block_overhead)
 g_seq_occupancy_block_overhead=overhead;}
void seq_occupancy_burst_start(void)
{const uint32_t measure=DWT->CYCCNT;seq_occupancy_state_t*s=&g_seq_occupancy_state;
 if(!s->active||s->burst_started)return;
 const uint32_t offset=seq_occupancy_offset(measure);
 const uint32_t gap=(offset>s->previous_end)?offset-s->previous_end:0U;seq_occupancy_gap_add(gap);
 if(s->burst_count==0U){s->first_start=offset;s->free_before=gap;}
 s->segment_started=measure;s->burst_preempt=0U;s->preempt_depth=0U;
 s->preempt_sequence=0U;s->burst_started=measure;
 seq_occupancy_trace_add(SEQ_OCC_TRACE_SEQ_START,offset,s->burst_count);
 const uint32_t overhead=DWT->CYCCNT-measure;if(overhead>g_seq_occupancy_start_overhead)
 g_seq_occupancy_start_overhead=overhead;}
void seq_occupancy_preempt_enter(void)
{seq_occupancy_state_t*s=&g_seq_occupancy_state;if(!s->active||!s->burst_started)return;
 if(s->preempt_depth++==0U){const uint32_t now=DWT->CYCCNT;
  const uint32_t run=now-s->segment_started;if(run>s->max_cpu)s->max_cpu=run;
  ++s->preempt_sequence;s->preempt_started=now;}}
void seq_occupancy_preempt_exit(void)
{seq_occupancy_state_t*s=&g_seq_occupancy_state;if(!s->active||!s->burst_started||!s->preempt_depth)return;
 if(--s->preempt_depth==0U){const uint32_t now=DWT->CYCCNT;
  const uint32_t elapsed=now-s->preempt_started;s->burst_preempt+=elapsed;
  s->preempt_cycles+=elapsed;s->segment_started=now;++s->preempt_sequence;}}
void seq_occupancy_burst_end(void)
{const uint32_t overhead_started=DWT->CYCCNT;uint32_t measure;
 seq_occupancy_state_t*s=&g_seq_occupancy_state;
 if(!s->active||!s->burst_started)return;
 uint32_t segment,preempt;
 if(s->preempt_depth!=0U){measure=s->preempt_started;segment=measure;
  preempt=s->burst_preempt;}
 else{uint32_t sequence;
  do{sequence=s->preempt_sequence;segment=s->segment_started;
   preempt=s->burst_preempt;measure=DWT->CYCCNT;
  }while(sequence!=s->preempt_sequence||(sequence&1U)!=0U);}
 const int32_t signed_run=(int32_t)(measure-segment);
 const uint32_t run=(signed_run>0)?(uint32_t)signed_run:0U;
 if(run>s->max_cpu)s->max_cpu=run;
 const uint32_t wall=measure-s->burst_started;
 const uint32_t cpu=(preempt<wall)?wall-preempt:0U;
 const uint32_t start=seq_occupancy_offset(s->burst_started),end=seq_occupancy_offset(measure);
 s->total_cpu+=cpu;s->total_wall+=wall;if(wall>s->max_wall)s->max_wall=wall;
 s->last_end=end;s->previous_end=end;++s->burst_count;++g_seq_occupancy_total_bursts;
 if(s->saved_count<SEQ_OCCUPANCY_BURSTS_SAVED)
  s->burst[s->saved_count++]=(seq_occupancy_burst_t){start,end,cpu,wall};
 else s->burst_overflow=1U;
 seq_occupancy_trace_add(SEQ_OCC_TRACE_SEQ_END,end,cpu);s->burst_started=0U;
 const uint32_t overhead=DWT->CYCCNT-overhead_started;if(overhead>g_seq_occupancy_end_overhead)
 g_seq_occupancy_end_overhead=overhead;}
void seq_occupancy_block_end(void)
{const uint32_t measure=DWT->CYCCNT;seq_occupancy_state_t*s=&g_seq_occupancy_state;
 if(!s->active)return;
 if(s->burst_started)seq_occupancy_burst_end();
 s->free_after=(s->last_end<s->period)?s->period-s->last_end:0U;
 seq_occupancy_gap_add(s->free_after);++g_seq_occupancy_blocks;
 g_seq_occupancy_total_preempt+=s->preempt_cycles;
 if(s->free_after<g_seq_occupancy_min_slack)g_seq_occupancy_min_slack=s->free_after;
 if(s->total_cpu>g_seq_occupancy_global_max_cpu){g_seq_occupancy_global_max_cpu=s->total_cpu;
  g_seq_occupancy_global_max_cpu_block=s->block_index;seq_occupancy_save_block(48U);}
 if(s->max_cpu>g_seq_occupancy_global_max_contiguous){g_seq_occupancy_global_max_contiguous=s->max_cpu;
  g_seq_occupancy_global_max_contiguous_block=s->block_index;seq_occupancy_save_block(96U);}
 if(s->max_wall>g_seq_occupancy_global_max_wall)g_seq_occupancy_global_max_wall=s->max_wall;
 seq_occupancy_trace_add(SEQ_OCC_TRACE_BLOCK_END,s->period,s->total_cpu);
 s->active=0U;seq_occupancy_publish();const uint32_t overhead=DWT->CYCCNT-measure;
 if(overhead>g_seq_occupancy_block_overhead)g_seq_occupancy_block_overhead=overhead;}

/* Fixed reference chains.  Parameters are deliberately ordinary musical
 * values: major triad; 1/16 Echo, two repeats, 32% decay; 75% Gate; Groove 3
 * at 25% timing and 100% velocity; Euclid 16/4 at 1/32. */
static void seq_boot_bench_fx(note_fx_track_state_t *state,uint8_t child)
{memset(state,0,sizeof(*state));
 if(child==0U){state->value[0][0]=0U;state->value[0][1]=0U;
  state->value[0][2]=0U;state->value[0][3]=NOTE_FX_MODEL_HARMONIZER;}
 else{state->value[0][0]=16U;state->value[0][1]=4U;
  state->value[0][2]=3U;state->value[0][3]=NOTE_FX_MODEL_EUCLID;}
 state->value[1][0]=2U;state->value[1][1]=2U;
 state->value[1][2]=32U;state->value[1][3]=NOTE_FX_MODEL_ECHO;
 state->value[2][0]=75U;state->value[2][1]=0U;
 state->value[2][2]=NOTE_FX_GATE_MODE_CLIP;state->value[2][3]=NOTE_FX_MODEL_GATE;
 state->value[3][0]=3U;state->value[3][1]=25U;
 state->value[3][2]=100U;state->value[3][3]=NOTE_FX_MODEL_GROOVE;}

static void seq_boot_bench_pattern_init(void)
{seq_pattern_t*p=seq_engine_control_bench_workspace();
 seq_lock_pattern_t*lock_pool[SEQ_LANE_CAPACITY];
 memcpy(lock_pool,p->lock_pool,sizeof(lock_pool));memset(p,0,sizeof(*p));
 memcpy(p->lock_pool,lock_pool,sizeof(lock_pool));
 p->generation=1U;p->running=1U;p->transport_epoch=1U;
 p->samples_per_step_q16=(uint32_t)SEQ_BOOT_BENCH_STEP_SAMPLES<<16U;
 for(uint8_t track=0U;track<SEQ_LANE_CAPACITY;++track){
  const uint8_t master=(uint8_t)(track==BRICK_ENTITY_GROUP_MASTER_ID);
  const uint8_t child=(uint8_t)(track>=BRICK_ENTITY_FIRST_GROUP_CHILD_ID);
  p->track_length[track]=1U;p->track_div[track]=1U;
  p->track_can_emit[track]=(uint8_t)(master==0U);p->track_note_enabled[track]=(uint8_t)(master==0U);
  p->track_fx_enabled[track]=(uint8_t)(master==0U);p->track_exec[track].logical_capacity=
      master?0U:(child?1U:8U);p->steps[track][0].trig_roll=3U;
  note_fx_track_state_t fx_state;seq_boot_bench_fx(&fx_state,child);
  (void)note_fx_plan_compile(&fx_state,0U,&p->fx_base_plan[track]);
  if(master)continue;
  const uint8_t voices=child?1U:8U;
  for(uint8_t voice=0U;voice<voices;++voice){seq_play_item_t*item=child
      ?&p->child_play[track-BRICK_ENTITY_FIRST_GROUP_CHILD_ID][0]
      :&p->top_play[track][0].items[voice];
   *item=(seq_play_item_t){.note=(uint8_t)(child?(40U+track):(36U+voice)),.velocity=112U,
      .length=64U,.microtiming=0,.present_mask=SEQ_STEP_PLAY_PRESENT_ALL};}}
}

static uint32_t seq_boot_bench_percentile(const uint32_t *histogram,
    uint32_t numerator,uint32_t denominator)
{const uint32_t target=(SEQ_BOOT_BENCH_ITERATIONS*numerator+denominator-1U)/denominator;
 uint32_t cumulative=0U;for(uint32_t i=0U;i<SEQ_BOOT_BENCH_BUCKET_COUNT;++i){
 cumulative+=histogram[i];if(cumulative>=target)
   return((i+1U)<<SEQ_BOOT_BENCH_BUCKET_SHIFT)-1U;}return UINT32_MAX;}

static uint8_t seq_boot_bench_service(uint64_t sample)
{seq_engine_core_process_block(&g_seq_bench_core,sample,SEQ_ENGINE_H743_PERIOD_SAMPLES,
   seq_engine_control_bench_workspace(),&g_seq_bench_output);
 return(uint8_t)(g_seq_bench_output.event_count>SEQ_ENGINE_TERMINAL_CAPACITY);}

static uint8_t seq_boot_bench_boundary(uint64_t sample)
{const uint32_t phase=(uint32_t)(sample%SEQ_BOOT_BENCH_STEP_SAMPLES);
 return(uint8_t)(phase==0U||phase+SEQ_ENGINE_H743_PERIOD_SAMPLES
     >SEQ_BOOT_BENCH_STEP_SAMPLES);}

void seq_engine_boot_bench_run(void)
{memset((void*)&g_seq_boot_bench,0,sizeof(g_seq_boot_bench));
 memset(g_seq_bench_histogram,0,sizeof(g_seq_bench_histogram));
 memset(g_seq_bench_cpu_histogram,0,sizeof(g_seq_bench_cpu_histogram));seq_boot_bench_pattern_init();
 CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0U;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
 seq_engine_drop_diag_reset();
 seq_engine_core_init(&g_seq_bench_core);uint64_t sample=SEQ_BOOT_BENCH_STEP_SAMPLES;
 uint32_t output_overflows=0U;
 for(uint32_t i=0U;i<SEQ_BOOT_BENCH_WARMUP_BLOCKS;++i,
       sample+=SEQ_ENGINE_H743_PERIOD_SAMPLES)
  output_overflows+=seq_boot_bench_service(sample);
 seq_occupancy_reset();
#if SEQ_FINE_DIAGNOSTICS
 seq_boundary_probe_reset();
 note_fx_walker_probe_reset();
#endif
 uint64_t total=0U,ordinary_total=0U,boundary_total=0U;
 uint64_t cpu_total=0U,cpu_ordinary_total=0U,cpu_boundary_total=0U,irq_total=0U;
 uint32_t max=0U,ordinary_max=0U,boundary_max=0U,ordinary_count=0U,boundary_count=0U;
 uint32_t cpu_max=0U,cpu_ordinary_max=0U,cpu_boundary_max=0U,irq_max=0U,preempted_blocks=0U;
 uint32_t source_peak=0U;
 uint32_t run50=0U,run75=0U,maxrun50=0U,maxrun75=0U;
 uint32_t over50=0U,over75=0U,overm750=0U,overm775=0U,output_peak=0U;
 for(uint32_t i=0U;i<SEQ_BOOT_BENCH_ITERATIONS;++i,sample+=SEQ_ENGINE_H743_PERIOD_SAMPLES){
  const uint8_t boundary=seq_boot_bench_boundary(sample);
#if SEQ_FINE_DIAGNOSTICS
  seq_boundary_probe_block_begin(boundary);
#endif
 g_seq_bench_irq_depth=0U;g_seq_bench_irq_cycles=0U;
  const uint32_t started=DWT->CYCCNT;g_seq_bench_irq_window_active=1U;
  output_overflows+=seq_boot_bench_service(sample);
#if SEQ_FINE_DIAGNOSTICS
  seq_boundary_probe_block_end();
#endif
  const uint32_t finished=DWT->CYCCNT;g_seq_bench_irq_window_active=0U;
  const uint32_t cycles=finished-started,irq_cycles=g_seq_bench_irq_cycles;
  const uint32_t cpu_cycles=(irq_cycles<=cycles)?cycles-irq_cycles:0U;
  total+=cycles;cpu_total+=cpu_cycles;irq_total+=irq_cycles;
  if(cycles>max)max=cycles;
  if(cpu_cycles>cpu_max)cpu_max=cpu_cycles;
  if(irq_cycles!=0U)++preempted_blocks;
  if(irq_cycles>irq_max)irq_max=irq_cycles;
  if(g_seq_bench_core.source_count>source_peak)source_peak=g_seq_bench_core.source_count;
  if(boundary!=0U){boundary_total+=cycles;cpu_boundary_total+=cpu_cycles;++boundary_count;
   if(cycles>boundary_max)boundary_max=cycles;
   if(cpu_cycles>cpu_boundary_max)cpu_boundary_max=cpu_cycles;}
  else{ordinary_total+=cycles;cpu_ordinary_total+=cpu_cycles;++ordinary_count;
   if(cycles>ordinary_max)ordinary_max=cycles;
   if(cpu_cycles>cpu_ordinary_max)cpu_ordinary_max=cpu_cycles;}
  uint32_t bucket=cycles>>SEQ_BOOT_BENCH_BUCKET_SHIFT;
  if(bucket>=SEQ_BOOT_BENCH_BUCKET_COUNT)bucket=SEQ_BOOT_BENCH_BUCKET_COUNT-1U;
  ++g_seq_bench_histogram[bucket];if(g_seq_bench_output.event_count>output_peak)output_peak=g_seq_bench_output.event_count;
  bucket=cpu_cycles>>SEQ_BOOT_BENCH_BUCKET_SHIFT;
  if(bucket>=SEQ_BOOT_BENCH_BUCKET_COUNT)bucket=SEQ_BOOT_BENCH_BUCKET_COUNT-1U;
  ++g_seq_bench_cpu_histogram[bucket];
  if(cycles>160000U){++over50;++run50;if(run50>maxrun50)maxrun50=run50;}else run50=0U;
  if(cycles>240000U){++over75;++run75;if(run75>maxrun75)maxrun75=run75;}else run75=0U;
  if(cycles>320000U)++overm750;
  if(cycles>480000U)++overm775;}
#if SEQ_FINE_DIAGNOSTICS
 seq_boundary_probe_publish();
 note_fx_walker_probe_publish();
#endif
 uint32_t drop_reason[SEQ_BOOT_BENCH_DROP_REASON_COUNT];
 note_fx_echo_diag_t echo_diag;
 seq_engine_drop_diag_capture(drop_reason);
 note_fx_engine_echo_diag_capture(&echo_diag);
 output_overflows+=drop_reason[3]+drop_reason[7];
 const uint32_t valid=(g_seq_bench_core.dropped_events==0U&&output_overflows==0U)?1U:0U;
 g_seq_boot_bench=(seq_boot_bench_result_t){.magic=SEQ_BOOT_BENCH_MAGIC,
  .version=SEQ_BOOT_BENCH_VERSION,
  .size=(uint16_t)sizeof(g_seq_boot_bench),.iterations=SEQ_BOOT_BENCH_ITERATIONS,
  .warmup_blocks=SEQ_BOOT_BENCH_WARMUP_BLOCKS,.core_hz=SystemCoreClock,
  .frames=SEQ_ENGINE_H743_PERIOD_SAMPLES,.logical_sources=SEQ_BOOT_BENCH_LOGICAL_SOURCES,
  .active_sources_peak=source_peak,
  .max_cycles=max,.mean_cycles=(uint32_t)(total/SEQ_BOOT_BENCH_ITERATIONS),
  .p99_cycles=seq_boot_bench_percentile(g_seq_bench_histogram,99U,100U),
  .p999_cycles=seq_boot_bench_percentile(g_seq_bench_histogram,999U,1000U),
  .blocks_over_m4_50=over50,.blocks_over_m4_75=over75,
  .max_consecutive_over_m4_50=maxrun50,.max_consecutive_over_m4_75=maxrun75,
  .blocks_over_m7_50=overm750,.blocks_over_m7_75=overm775,
  .technical_drops=g_seq_bench_core.dropped_events,.musical_rejections=0U,
  .output_overflows=output_overflows,.output_peak=output_peak,
  .max_cycles_ordinary=ordinary_max,
  .mean_cycles_ordinary=ordinary_count?(uint32_t)(ordinary_total/ordinary_count):0U,
  .max_cycles_boundary=boundary_max,
  .mean_cycles_boundary=boundary_count?(uint32_t)(boundary_total/boundary_count):0U,
  .ordinary_blocks=ordinary_count,.boundary_blocks=boundary_count,
  .drop_deferred_capacity=drop_reason[1],
  .drop_ledger_admission=drop_reason[2],
  .drop_terminal_output_capacity=drop_reason[3],
  .drop_source_capacity=drop_reason[4],.drop_fx_preprocess=drop_reason[5],
  .drop_source_transform=drop_reason[6],
  .drop_scheduled_output_capacity=drop_reason[7],
  .drop_fx_postprocess=drop_reason[8],
  .drop_plock_capacity=drop_reason[9],
  .echo_active_peak=echo_diag.active_peak,
  .echo_alloc_failures=echo_diag.alloc_failures,
  .cpu_max_cycles=cpu_max,.cpu_mean_cycles=(uint32_t)(cpu_total/SEQ_BOOT_BENCH_ITERATIONS),
  .cpu_p99_cycles=seq_boot_bench_percentile(g_seq_bench_cpu_histogram,99U,100U),
  .cpu_p999_cycles=seq_boot_bench_percentile(g_seq_bench_cpu_histogram,999U,1000U),
  .cpu_max_cycles_ordinary=cpu_ordinary_max,
  .cpu_mean_cycles_ordinary=ordinary_count?(uint32_t)(cpu_ordinary_total/ordinary_count):0U,
  .cpu_max_cycles_boundary=cpu_boundary_max,
  .cpu_mean_cycles_boundary=boundary_count?(uint32_t)(cpu_boundary_total/boundary_count):0U,
  .irq_cycles_max_block=irq_max,.preempted_blocks=preempted_blocks,
  .max_preemption_cycles=irq_max,.irq_cycles_total=irq_total,.valid=valid};
 seq_engine_irq_init();__DMB();g_seq_boot_bench.ready=1U;}

static void seq_perf_record(uint32_t cycles)
{const uint32_t budget=SystemCoreClock*SEQ_ENGINE_H743_PERIOD_SAMPLES/48000U;
 ++g_seq_perf.count;g_seq_perf.total+=cycles;if(cycles>g_seq_perf.max)g_seq_perf.max=cycles;
 uint8_t bucket=0U;uint32_t value=cycles;while(value>1U&&bucket<31U){value>>=1U;++bucket;}
 ++g_seq_perf.histogram[bucket];
 if(cycles>budget/2U){++g_seq_perf.over50;++g_seq_perf.run50;
  if(g_seq_perf.run50>g_seq_perf.maxrun50)g_seq_perf.maxrun50=g_seq_perf.run50;}else g_seq_perf.run50=0U;
 if(cycles>(budget*3U)/4U){++g_seq_perf.over75;++g_seq_perf.run75;
  if(g_seq_perf.run75>g_seq_perf.maxrun75)g_seq_perf.maxrun75=g_seq_perf.run75;}else g_seq_perf.run75=0U;}

static uint32_t seq_perf_percentile(uint32_t numerator,uint32_t denominator)
{if(!g_seq_perf.count)return 0U;const uint32_t target=(g_seq_perf.count*numerator+denominator-1U)/denominator;
 uint32_t cumulative=0U;for(uint8_t i=0U;i<32U;++i){cumulative+=g_seq_perf.histogram[i];
  if(cumulative>=target)return UINT32_C(1)<<i;}return UINT32_MAX;}

void seq_engine_perf_capture(seq_engine_perf_snapshot_t *out)
{if(!out)return;const uint32_t primask=__get_PRIMASK();__disable_irq();
 *out=(seq_engine_perf_snapshot_t){.max_cycles=g_seq_perf.max,
  .mean_cycles=g_seq_perf.count?(uint32_t)(g_seq_perf.total/g_seq_perf.count):0U,
  .p99_cycles=seq_perf_percentile(99U,100U),.p999_cycles=seq_perf_percentile(999U,1000U),
  .blocks_over_50=g_seq_perf.over50,.blocks_over_75=g_seq_perf.over75,
  .max_consecutive_over_50=g_seq_perf.maxrun50,.max_consecutive_over_75=g_seq_perf.maxrun75,
  .missed_horizons=g_missed_horizons,
  };__set_PRIMASK(primask);}

void seq_engine_control_disarm_track(uint8_t track)
{
    if (track < SEQ_LANE_CAPACITY) {
        g_disarmed_tracks |= (uint16_t)(1U << track); __DMB();
    }
}

static uint16_t active_track_mask(const seq_terminal_block_t *block)
{
    return block ? (uint16_t)(block->emitter_tracks
        & (uint16_t)~g_disarmed_tracks) : 0U;
}

void seq_engine_irq_init(void)
{
    memset(g_terminal, 0, sizeof(g_terminal));
    memset((void *)g_slot_state, 0, sizeof(g_slot_state));
    g_audio_slot = -1; g_audio_cursor = SEQ_ENGINE_TERMINAL_INDEX_NONE;
    g_audio_offset=0U;g_audio_class=0U;g_pending = 0U; g_urgent_pending=0U;
    g_disarmed_tracks = 0U; g_disarm_generation = 0U;
    g_force_stopped = 0U; g_force_stop_epoch=0U; g_next_deadline = UINT64_MAX;
    g_missed_horizons=0U;
    g_ingress_head=0U;g_ingress_tail=0U;g_ingress_count=0U;g_ingress_panic=0U;
    g_ingress_rate_window=UINT64_MAX;g_ingress_rate_count=0U;
    memset(&g_seq_perf,0,sizeof(g_seq_perf));
    CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0U;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
    seq_engine_core_init(&g_core);
    NVIC_ClearPendingIRQ(TIM4_IRQn);
    NVIC_SetPriority(TIM4_IRQn, 2U);
    NVIC_EnableIRQ(TIM4_IRQn);
}

void seq_engine_audio_boundary(uint64_t block_start_sample, uint8_t recovering)
{
    seq_occupancy_block_start((uint32_t)(block_start_sample/SEQ_ENGINE_H743_PERIOD_SAMPLES),
        DWT->CYCCNT);
    uint8_t acquired=0U;
    if (g_audio_slot >= 0) {
        g_slot_state[(uint8_t)g_audio_slot] = SLOT_FREE; g_audio_slot = -1;
    }
    if (recovering != 0U)
        for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
            if (g_slot_state[i] == SLOT_READY) g_slot_state[i] = SLOT_FREE;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i) {
        if ((g_slot_state[i] == SLOT_READY)
                && (g_terminal[i].start_sample == block_start_sample)) {
            g_slot_state[i] = SLOT_READING; g_audio_slot = (int8_t)i;
            g_audio_cursor=SEQ_ENGINE_TERMINAL_INDEX_NONE;
            g_audio_offset=0U;g_audio_class=0U;acquired=1U;break;
        }
        if ((g_slot_state[i] == SLOT_READY)
                && (g_terminal[i].start_sample < block_start_sample)) {
            g_slot_state[i] = SLOT_FREE;
        }
    }
    if(recovering==0U&&acquired==0U)++g_missed_horizons;
    g_service_now = block_start_sample;
    g_publish_until = block_start_sample + SEQ_ENGINE_H743_PERIOD_SAMPLES;
    __DMB();
    g_pending = 1U; NVIC_SetPendingIRQ(TIM4_IRQn);
}

static uint8_t event_is_audible(const seq_terminal_block_t *block,uint8_t kind,
                                const seq_terminal_event_t *event)
{
    if(event==0)return 0U;
    const uint8_t track=(kind==SEQ_ENGINE_EVENT_PARAM)
        ?event->param.track:event->note.track;
    if(track>=SEQ_LANE_CAPACITY)return 0U;
    if (kind == SEQ_ENGINE_EVENT_PARAM)
        return (uint8_t)((block->lock_tracks
            & (uint16_t)(1U << track)) != 0U);
    return (uint8_t)((active_track_mask(block)
        & (uint16_t)(1U << track)) != 0U);
}

static uint8_t audio_cursor_seek(seq_terminal_block_t *block)
{for(;;){if(g_audio_cursor!=SEQ_ENGINE_TERMINAL_INDEX_NONE)return 1U;
  while(g_audio_offset<block->frames){while(g_audio_class<SEQ_ENGINE_TERMINAL_CLASS_COUNT){
    const uint16_t head=block->head[g_audio_offset][g_audio_class];
    if(head!=SEQ_ENGINE_TERMINAL_INDEX_NONE){g_audio_cursor=head;return 1U;}
    ++g_audio_class;}++g_audio_offset;g_audio_class=0U;}return 0U;}}

static void audio_cursor_advance(const seq_terminal_block_t *block)
{if(g_audio_cursor!=SEQ_ENGINE_TERMINAL_INDEX_NONE)
    g_audio_cursor=block->next[g_audio_cursor];
 if(g_audio_cursor==SEQ_ENGINE_TERMINAL_INDEX_NONE)++g_audio_class;}

uint16_t seq_engine_audio_frames_until_due(uint64_t sample, uint16_t maximum)
{
    if ((g_audio_slot < 0) || (maximum == 0U)) return maximum;
    seq_terminal_block_t *const block = &g_terminal[(uint8_t)g_audio_slot];
    while(audio_cursor_seek(block)!=0U){
        const seq_terminal_event_t *const event=&block->events[g_audio_cursor];
        if(event_is_audible(block,g_audio_class,event)==0U){audio_cursor_advance(block);continue;}
        const uint64_t due = block->start_sample + g_audio_offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)) {
            audio_cursor_advance(block);continue;
        }
        if (due <= sample) return 0U;
        const uint64_t distance = due - sample;
        return (distance < maximum) ? (uint16_t)distance : maximum;
    }
    return maximum;
}

uint8_t seq_engine_audio_pop_due(uint64_t sample,uint8_t *out_kind,
    seq_terminal_event_t *out_event)
{
    if ((out_event == 0)||(out_kind==0)||(g_audio_slot < 0)) return 0U;
    seq_terminal_block_t *const block = &g_terminal[(uint8_t)g_audio_slot];
    while(audio_cursor_seek(block)!=0U){
        const seq_terminal_event_t event=block->events[g_audio_cursor];
        if(event_is_audible(block,g_audio_class,&event)==0U){audio_cursor_advance(block);continue;}
        const uint64_t due = block->start_sample + g_audio_offset;
        if ((g_force_stopped != 0U) && (due >= g_force_stop_sample)) {
            audio_cursor_advance(block);continue;
        }
        if (due > sample) return 0U;
        *out_kind=g_audio_class;*out_event=event;audio_cursor_advance(block);return 1U;
    }
    return 0U;
}

void seq_engine_audio_retire_occurrence(uint32_t occurrence_id)
{ (void)occurrence_id; }

uint16_t seq_engine_audio_track_mask(void)
{
    return (g_audio_slot >= 0)
        ? active_track_mask(&g_terminal[(uint8_t)g_audio_slot]) : 0U;
}

void seq_engine_audio_force_stop(uint64_t effective_sample)
{ g_force_stop_sample = effective_sample; g_force_stop_epoch=g_core.transport_epoch;
  g_force_stopped = 1U; }

uint8_t seq_engine_playhead_view(uint8_t track,uint8_t *out_running,
    uint8_t *out_step)
{
    if((track>=SEQ_LANE_CAPACITY)||(out_running==0)||(out_step==0))return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    *out_running=g_core.running;*out_step=g_core.play_step[track];
    __set_PRIMASK(primask);return 1U;
}

uint64_t seq_next_deadline(void) { return g_next_deadline; }

uint8_t seq_ingress_submit(const seq_ingress_event_t *event)
{
    if((event==0)||(event->track>=SEQ_LANE_CAPACITY)||(event->note>=128U)
            ||(event->velocity>=128U)||(event->kind>NOTE_EVENT_KIND_ON)
            ||(event->provenance>=NOTE_EVENT_SOURCE_COUNT)
            ||(event->occurrence_id==0U))return 0U;
    const seq_pattern_t *const pattern=seq_engine_pattern_capture();
    if(event->track==BRICK_ENTITY_GROUP_MASTER_ID
            ||(pattern!=0&&pattern->track_exec[event->track].logical_capacity==0U))return 0U;
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    const uint64_t rate_window=event->capture_sample/SEQ_INGRESS_WINDOW_SAMPLES;
    if(g_ingress_rate_window==UINT64_MAX){g_ingress_rate_window=rate_window;
        g_ingress_rate_count=0U;}
    else if(rate_window>g_ingress_rate_window){g_ingress_rate_window=rate_window;
        g_ingress_rate_count=0U;}
    else if(rate_window<g_ingress_rate_window){__set_PRIMASK(primask);return 0U;}
    if((g_ingress_rate_count>=SEQ_INGRESS_EVENTS_PER_WINDOW_MAX)
            ||(g_ingress_count>=SEQ_ENGINE_INGRESS_CAPACITY)){
        __set_PRIMASK(primask);return 0U;}
    g_ingress[g_ingress_head]=*event;
    g_ingress_head=(uint8_t)((g_ingress_head+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
    ++g_ingress_count;
    ++g_ingress_rate_count;
    g_urgent_pending=1U;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);return 1U;
}

void seq_ingress_panic(void)
{
    const uint32_t primask=__get_PRIMASK();__disable_irq();
    g_ingress_count=0U;g_ingress_head=0U;g_ingress_tail=0U;g_ingress_panic=1U;
    __set_PRIMASK(primask);NVIC_SetPendingIRQ(TIM4_IRQn);
}

void seq_service(uint64_t now_sample, uint64_t publish_until_sample)
{
    uint8_t slot = SEQ_ENGINE_BLOCK_SLOTS;
    for (uint8_t i = 0U; i < SEQ_ENGINE_BLOCK_SLOTS; ++i)
        if (g_slot_state[i] == SLOT_FREE) { slot = i; break; }
    if ((slot == SEQ_ENGINE_BLOCK_SLOTS) || (publish_until_sample <= now_sample)
            || ((publish_until_sample - now_sample) > UINT16_MAX)) {
        return;
    }
    g_slot_state[slot] = SLOT_WRITING;
    seq_terminal_block_t *const block = &g_terminal[slot];
    const seq_pattern_t *const pattern = seq_engine_pattern_capture();
    if ((pattern != 0) && (pattern->generation != g_disarm_generation)) {
        g_disarmed_tracks = 0U; g_disarm_generation = pattern->generation;
    }
    if((g_force_stopped!=0U)&&(pattern!=0)&&(pattern->running!=0U)
            &&(pattern->transport_epoch!=g_force_stop_epoch))
        g_force_stopped=0U;
    const uint64_t start = publish_until_sample;
    const uint16_t frames = SEQ_ENGINE_H743_PERIOD_SAMPLES;
    if ((g_force_stopped != 0U) && (start >= g_force_stop_sample)) {
        seq_engine_core_process_block(&g_core,start,frames,0,block);
    } else {
        g_force_stopped = 0U;
        const uint32_t cycle_start=DWT->CYCCNT;
        seq_engine_core_process_block(&g_core,start,frames,pattern,block);
        seq_perf_record(DWT->CYCCNT-cycle_start);
        if(g_ingress_panic!=0U){g_ingress_panic=0U;
            seq_engine_core_init(&g_core);}
        while(g_ingress_count!=0U){
            const seq_ingress_event_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            uint64_t captured=in.capture_sample;
            const uint64_t due=(captured<start)?start:captured;
            const note_event_t event={.sample_abs=due,
                .duration_samples=1U,
                .source_id=in.occurrence_id,.occurrence_id=in.occurrence_id,
                .source_generation=pattern?pattern->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .note=in.note,
                .velocity=in.velocity,.kind=in.kind,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            (void)seq_engine_core_submit_live(&g_core,&event,pattern,start,start+frames,block);
        }
    }
    block->block_id = (uint32_t)(start / frames);
    g_next_deadline = start + frames; __DMB(); g_slot_state[slot] = SLOT_READY;
}

static void seq_service_urgent(uint64_t now_sample,uint64_t publish_until_sample)
{
    (void)now_sample;
    for(uint8_t slot=0U;slot<SEQ_ENGINE_BLOCK_SLOTS;++slot){
        const uint32_t primask=__get_PRIMASK();__disable_irq();
        if((g_slot_state[slot]!=SLOT_READY)
                ||(g_terminal[slot].start_sample!=publish_until_sample)){
            __set_PRIMASK(primask);continue;}
        g_slot_state[slot]=SLOT_WRITING;__DMB();__set_PRIMASK(primask);
        seq_terminal_block_t *const block=&g_terminal[slot];
        const uint64_t end=block->start_sample+block->frames;
        while(g_ingress_count!=0U){
            const seq_ingress_event_t in=g_ingress[g_ingress_tail];
            g_ingress_tail=(uint8_t)((g_ingress_tail+1U)%SEQ_ENGINE_INGRESS_CAPACITY);
            --g_ingress_count;
            const uint64_t due=(in.capture_sample<block->start_sample)
                ?block->start_sample:in.capture_sample;
            const note_event_t event={.sample_abs=due,
                .duration_samples=1U,
                .source_id=in.occurrence_id,.occurrence_id=in.occurrence_id,
                .source_generation=block->generation?block->generation:1U,
                .group_id=in.occurrence_id,.track=in.track,
                .note=in.note,
                .velocity=in.velocity,.kind=in.kind,
                .provenance=in.provenance,.stage=NOTE_EVENT_STAGE_SOURCE};
            const seq_pattern_t *const pattern=seq_engine_pattern_capture();
            (void)seq_engine_core_submit_live(&g_core,&event,pattern,
                block->start_sample,end,block);
        }
        __DMB();g_slot_state[slot]=SLOT_READY;return;
    }
}

void TIM4_IRQHandler(void)
{
    const uint8_t occupancy_preempting_bench=(uint8_t)(g_seq_bench_irq_window_active!=0U);
    seq_bench_irq_enter();
    sdmmc_async_transport_preempt_enter();
    if ((g_pending == 0U)&&(g_urgent_pending==0U)) {
        sdmmc_async_transport_preempt_exit(); seq_bench_irq_exit(); return;
    }
    uint64_t now = g_service_now;const uint64_t until = g_publish_until;
    const uint8_t urgent=g_urgent_pending,periodic=g_pending;
    if(urgent!=0U)(void)brick_media_clock_now_sample(&now);
    g_pending=0U;g_urgent_pending=0U;
    if(occupancy_preempting_bench==0U)seq_occupancy_burst_start();
    if(periodic!=0U)seq_service(now,until);else seq_service_urgent(now,until);
    if(occupancy_preempting_bench==0U)seq_occupancy_burst_end();
    if(g_urgent_pending!=0U)NVIC_SetPendingIRQ(TIM4_IRQn);
    sdmmc_async_transport_preempt_exit();
    seq_bench_irq_exit();
}
