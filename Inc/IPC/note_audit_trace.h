#ifndef NOTE_AUDIT_TRACE_H
#define NOTE_AUDIT_TRACE_H

#include <stdint.h>

/* Temporary RAM trace. Sequence is written last; zero means incomplete. */
typedef struct {
    uint32_t sequence;
    uint32_t tick;
    uint32_t id;
    uint32_t aux;
    uint16_t event;
    uint8_t track;
    uint8_t note;
    uint8_t detail;
    uint8_t held_count;
    uint16_t held_mask;
} note_audit_record_t;

enum {
    NOTE_AUDIT_HALL_EDGE = 1,
    NOTE_AUDIT_HALL_DROP,
    NOTE_AUDIT_HALL_POP,
    NOTE_AUDIT_KEY,
    NOTE_AUDIT_KEY_ON,
    NOTE_AUDIT_KEY_OFF,
    NOTE_AUDIT_OCCURRENCE,
    NOTE_AUDIT_INGRESS_FAIL,
    NOTE_AUDIT_OUTPUT,
    NOTE_AUDIT_VICTIM,
    NOTE_AUDIT_PUBLISH_FAIL,
    NOTE_AUDIT_PANIC,
    NOTE_AUDIT_AUDIO_COMMAND,
    NOTE_AUDIT_AUDIO_VOICE,
    NOTE_AUDIT_AUDIO_PANIC,
    NOTE_AUDIT_INGRESS,
    NOTE_AUDIT_WINDOW,
    NOTE_AUDIT_HALL_QUEUED,
    NOTE_AUDIT_OWNER,
    NOTE_AUDIT_AUDIO_TRANSPORT,
    NOTE_AUDIT_OUTPUT_DEATH
};

#define NOTE_AUDIT_CONTROL_CAPACITY 1024U
#define NOTE_AUDIT_AUDIO_CAPACITY 512U

extern volatile note_audit_record_t g_note_audit_control[NOTE_AUDIT_CONTROL_CAPACITY];
extern volatile uint32_t g_note_audit_control_sequence;
extern volatile note_audit_record_t g_note_audit_audio[NOTE_AUDIT_AUDIO_CAPACITY];
extern volatile uint32_t g_note_audit_audio_sequence;

void note_audit_control(uint16_t event, uint8_t track, uint8_t note,
                        uint8_t detail, uint8_t held_count,
                        uint16_t held_mask, uint32_t id, uint32_t aux);
void note_audit_audio(uint16_t event, uint8_t track, uint8_t note,
                      uint8_t detail, uint32_t id, uint32_t aux);

#endif
