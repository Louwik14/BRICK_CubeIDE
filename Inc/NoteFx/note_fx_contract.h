#ifndef NOTE_FX_CONTRACT_H
#define NOTE_FX_CONTRACT_H

#define NOTE_FX_SLOT_COUNT 3U
#define NOTE_FX_PARAM_COUNT 4U
#define NOTE_FX_VALUE_COUNT (NOTE_FX_PARAM_COUNT + 1U)
#define NOTE_FX_MODEL_INDEX NOTE_FX_PARAM_COUNT
#define NOTE_FX_ORDER_COUNT 6U
#define NOTE_FX_CANONICAL_PARAM_COUNT \
    (NOTE_FX_SLOT_COUNT * NOTE_FX_VALUE_COUNT + 1U)

/* Fixed-chain contract.  The legacy slot constants above remain private to the
 * current executor until the runtime cut-over; new CONTROL state must use this
 * four-stage address space. */
#define NOTE_FX_CHAIN_STAGE_COUNT 4U
#define NOTE_FX_CHAIN_PARAM_COUNT 4U
#define NOTE_FX_CHAIN_CANONICAL_PARAM_COUNT \
    (NOTE_FX_CHAIN_STAGE_COUNT * NOTE_FX_CHAIN_PARAM_COUNT)

typedef enum
{
    NOTE_FX_CHAIN_STAGE_GENERATOR = 0,
    NOTE_FX_CHAIN_STAGE_VOICER,
    NOTE_FX_CHAIN_STAGE_SCALER,
    NOTE_FX_CHAIN_STAGE_TRIG
} note_fx_chain_stage_t;

typedef enum
{
    NOTE_FX_GENERATOR_OFF = 0,
    NOTE_FX_GENERATOR_ARP,
    NOTE_FX_GENERATOR_HOLD,
    NOTE_FX_GENERATOR_EUCLID,
    NOTE_FX_GENERATOR_MODE_COUNT
} note_fx_generator_mode_t;

typedef enum
{
    NOTE_FX_VOICER_SPREAD_CLOSED = 0,
    NOTE_FX_VOICER_SPREAD_1,
    NOTE_FX_VOICER_SPREAD_2,
    NOTE_FX_VOICER_SPREAD_ALT,
    NOTE_FX_VOICER_SPREAD_COUNT
} note_fx_voicer_spread_t;

typedef enum
{
    NOTE_FX_VOICER_INVERT_ROOT = 0,
    NOTE_FX_VOICER_INVERT_1,
    NOTE_FX_VOICER_INVERT_2,
    NOTE_FX_VOICER_INVERT_3,
    NOTE_FX_VOICER_INVERT_AUTO,
    NOTE_FX_VOICER_INVERT_COUNT
} note_fx_voicer_invert_t;

typedef enum
{
    NOTE_FX_VOICER_MODE_OFF = 0,
    NOTE_FX_VOICER_MODE_1,
    NOTE_FX_VOICER_MODE_2,
    NOTE_FX_VOICER_MODE_3,
    NOTE_FX_VOICER_MODE_4,
    NOTE_FX_VOICER_MODE_SEQ2,
    NOTE_FX_VOICER_MODE_SEQ3,
    NOTE_FX_VOICER_MODE_SEQ4,
    NOTE_FX_VOICER_MODE_UPDN2,
    NOTE_FX_VOICER_MODE_UPDN3,
    NOTE_FX_VOICER_MODE_UPDN4,
    NOTE_FX_VOICER_MODE_CLIMB2,
    NOTE_FX_VOICER_MODE_CLIMB3,
    NOTE_FX_VOICER_MODE_CLIMB4,
    NOTE_FX_VOICER_MODE_COUNT
} note_fx_voicer_mode_t;

typedef enum
{
    NOTE_FX_SCALER_STICK_FIXED_DOWN = 0,
    NOTE_FX_SCALER_STICK_FIXED_UP,
    NOTE_FX_SCALER_STICK_FIXED_DROP,
    NOTE_FX_SCALER_STICK_NEAREST,
    NOTE_FX_SCALER_STICK_WALK,
    NOTE_FX_SCALER_STICK_COUNT
} note_fx_scaler_stick_t;

/* OFF is distinct from CHROMATIC: CHROMATIC preserves transpose while
 * deliberately performing no scale quantization. */
typedef enum
{
    NOTE_FX_SCALER_SCALE_OFF = 0,
    NOTE_FX_SCALER_SCALE_CHROMATIC,
    NOTE_FX_SCALER_SCALE_MAJOR,
    NOTE_FX_SCALER_SCALE_NAT_MINOR,
    NOTE_FX_SCALER_SCALE_DORIAN,
    NOTE_FX_SCALER_SCALE_MIXOLYDIAN,
    NOTE_FX_SCALER_SCALE_PENT_MAJOR,
    NOTE_FX_SCALER_SCALE_PENT_MINOR,
    NOTE_FX_SCALER_SCALE_COUNT
} note_fx_scaler_scale_t;

/* GROUP is the existing group_id decision scope.  POLY adds branch identity.
 * Division-backed LOT values start at DIVISION_BASE. */
typedef enum
{
    NOTE_FX_TRIG_LOT_GROUP = 0,
    NOTE_FX_TRIG_LOT_POLY,
    NOTE_FX_TRIG_LOT_DIVISION_BASE
} note_fx_trig_lot_t;

/* Existing KEEP divisions retain their force-PASS meaning.  LOOP is a
 * separate loop-relative deterministic probability mode. */
typedef enum
{
    NOTE_FX_TRIG_KEEP_OFF = 0,
    NOTE_FX_TRIG_KEEP_LOOP,
    NOTE_FX_TRIG_KEEP_DIVISION_BASE
} note_fx_trig_keep_t;

#define NOTE_FX_TRIG_CHANCE_OFF 0U
#define NOTE_FX_TRIG_CHANCE_100 1U
#define NOTE_FX_TRIG_CHANCE_0 101U

typedef struct __attribute__((packed))
{
    uint8_t p1;
    uint8_t p2;
    uint8_t p3;
    uint8_t mode;
} note_fx_generator_state_t;

typedef struct __attribute__((packed))
{
    uint8_t type;
    uint8_t spread;
    uint8_t invert;
    uint8_t mode;
} note_fx_voicer_state_t;

typedef struct __attribute__((packed))
{
    uint8_t key;
    uint8_t stick;
    uint8_t transpose;
    uint8_t scale;
} note_fx_scaler_state_t;

typedef struct __attribute__((packed))
{
    uint8_t lot;
    uint8_t keep;
    uint8_t gate;
    uint8_t chance;
} note_fx_trig_state_t;

typedef struct __attribute__((packed))
{
    note_fx_generator_state_t generator;
    note_fx_voicer_state_t voicer;
    note_fx_scaler_state_t scaler;
    note_fx_trig_state_t trig;
} note_fx_chain_state_t;

_Static_assert(sizeof(note_fx_generator_state_t) == 4U,
               "fixed generator state budget");
_Static_assert(sizeof(note_fx_voicer_state_t) == 4U,
               "fixed voicer state budget");
_Static_assert(sizeof(note_fx_scaler_state_t) == 4U,
               "fixed scaler state budget");
_Static_assert(sizeof(note_fx_trig_state_t) == 4U,
               "fixed trig state budget");
_Static_assert(sizeof(note_fx_chain_state_t) == 16U,
               "fixed Note FX chain state budget");

#endif
