$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_engine.h')
$seq = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine.c')
$fx = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$keyboard = Get-Content -Raw (Join-Path $root 'Src/Keyboard/keyboard_engine.c')
$capacity = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_capacity_contract.h')
$audio = Get-Content -Raw (Join-Path $root 'Src/Audio/audio_command_executor.c')

Describe 'SEQ/NoteFX bounded runtime contract' {
    It 'uses one 512-ticket scheduler with 24-byte tickets' {
        $header | Should Match '#define SEQ_ENGINE_SCHEDULER_CAPACITY 512U'
        $header | Should Match 'sizeof\(seq_ticket_t\) == 24U'
        ($header + $seq) | Should Not Match 'SEQ_ENGINE_(FUTURE|LIFETIME)_CAPACITY'
    }

    It 'keeps a 64-entry logical ledger and deterministic source-full priority' {
        $header | Should Match '#define SEQ_ENGINE_LEDGER_CAPACITY 64U'
        $seq | Should Match 'entry->original==0U'
        $seq | Should Match 'entry->admitted_sample<core->ledger'
    }

    It 'implements due-only bounded Echo with non-postponing retrigger' {
        $fx | Should Match 'g_echo\[SEQ_PRODUCT_ECHO_STATE_CAPACITY\]'
        $fx | Should Match 'lane\*SEQ_PRODUCT_HARMONY_FANOUT_MAX\+branch'
        $fx | Should Match 'BRICK_ENTITY_FIRST_GROUP_CHILD_ID'
        $fx | Should Match 'if\(promised<x->next_due\)x->next_due=promised'
        $fx | Should Match 'while\(x->active&&x->next_due<end\)'
        $fx | Should Not Match 'for\(uint8_t rep=1;rep<=r->p2'
    }

    It 'reuses one canonical Echo state per lane and harmonic branch' {
        $fx | Should Match 'if\(x->active\).*reuse_hits'
        $fx | Should Match 'g_echo_diag.active_peak'
        $fx | Should Match 'x->index>=x->repeats.*x->active=0U'
        $fx | Should Match 'lane>=SEQ_PRODUCT_MAX_EMITTING_VOICES'
        $header | Should Match 'echo_active_peak'
        $header | Should Match 'echo_alloc_failures'
    }

    It 'covers HARM-ECHO branch identity and same-lane retrigger' {
        $fx | Should Match 'lane\*SEQ_PRODUCT_HARMONY_FANOUT_MAX\+branch'
        $fx | Should Match 'const uint8_t was_active=x->active'
        $fx | Should Match 'promised=was_active\?x->next_due:UINT64_MAX'
        $fx | Should Match 'x\.dependency_mask=.*voice<<NOTE_EVENT_BRANCH_SHIFT'
    }

    It 'covers ECHO-HARM continuation and final-repeat release' {
        $fx | Should Match 'seed\.stage=stage'
        $seq | Should Match 'walker_resume\(event,event->stage\)'
        $fx | Should Match 'if\(x->index>=x->repeats\).*x->active=0U'
    }

    It 'maps the complete legal Echo capacity without alias or overflow' {
        $indices = @()
        foreach ($lane in 0..63) {
            foreach ($branch in 0..3) { $indices += 4 * $lane + $branch }
        }
        ($indices | Select-Object -Unique).Count | Should Be 256
        ($indices | Measure-Object -Minimum).Minimum | Should Be 0
        ($indices | Measure-Object -Maximum).Maximum | Should Be 255
        foreach ($child in 8..15) {
            (56..63) -contains (56 + ($child - 8)) | Should Be $true
        }
    }

    It 'bounds Groove resumes and clamps negative live timing' {
        $seq | Should Match '(?s)count>=SEQ_PRODUCT_GROOVE_RESUME_BATCHES_PER_LANE.*scheduler_release'
        $seq | Should Match 'event\[SEQ_PRODUCT_HARMONY_FANOUT_MAX\]'
        $fx | Should Match 'NOTE_EVENT_SOURCE_KEY\|\|e->provenance==NOTE_EVENT_SOURCE_MIDI'
        $fx | Should Match 'x.sample_abs<e->sample_abs\)x.sample_abs=e->sample_abs'
    }

    It 'sizes terminal publication from the legal fanout proof' {
        $header | Should Match '#define SEQ_ENGINE_EVENT_CAPACITY 4096U'
        $header | Should Match 'SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON'
        $capacity | Should Match 'SEQ_PRODUCT_TERMINAL_EVENTS_PER_HORIZON == 3648U'
        $capacity | Should Match 'SEQ_PRODUCT_ECHO_STATE_CAPACITY == 256U'
        $capacity | Should Match 'SEQ_PRODUCT_GROOVE_RESUME_EVENT_CAPACITY == 512U'
        $header | Should Match 'sizeof\(seq_event_t\) == 9U'
        $audio | Should Match 'seq_event_param_value\(event\)'
        $audio | Should Match 'event->reserved==SEQ_ENGINE_PARAM_TEMP'
    }

    It 'uses canonical held identity without lane or destination truncation' {
        $fx | Should Match 'g_held\[NOTE_FX_SLOT_COUNT\]\[SEQ_PRODUCT_HELD_STATE_CAPACITY\]'
        $fx | Should Match '\.destination_id=e->destination_id'
        $fx | Should Match 'lane_temporal\(t,lane\)'
        $fx | Should Not Match 'destination_id&0x0FU'
        $fx | Should Not Match 'temporal_index&0x03U'
    }

    It 'supports the complete source and held-state bounds' {
        $header | Should Match '#define SEQ_ENGINE_SOURCE_CAPACITY SEQ_PRODUCT_MAX_ACTIVE_SOURCES'
        $header | Should Match 'SEQ_ENGINE_SOURCE_CAPACITY == 192U'
        $capacity | Should Match 'SEQ_PRODUCT_HELD_STATE_CAPACITY == 256U'
        $capacity | Should Match 'SEQ_PRODUCT_HELD_TOTAL_CAPACITY == 1024U'
        $seq | Should Match 'g_seq_source_extension\[SEQ_ENGINE_SOURCE_CAPACITY-SEQ_ENGINE_LEDGER_CAPACITY\]'
        $seq | Should Not Match 'if\(owned>=quota\)target=oldest'
        (0..191).Count | Should Be 192
        $held = foreach ($slot in 0..3) {
            foreach ($lane in 0..63) {
                foreach ($branch in 0..3) { 256 * $slot + 4 * $lane + $branch }
            }
        }
        ($held | Select-Object -Unique).Count | Should Be 1024
        ($held | Where-Object { $_ -ge 32 }).Count | Should BeGreaterThan 8
    }

    It 'preserves intra-step generator phase' {
        $seq | Should Match 'track_phase_q16/div'
        $seq | Should Match 'core->track_div_phase\[t\]<<16U'
        $seq | Should Match 'core->play_step\[t\]<<16U'
        $seq | Should Match 'core->transport_step_serial<<16U'
        foreach ($bpm in 40, 120, 300) {
            $stepSamples = [uint64](48000 * 60 / ($bpm * 4))
            foreach ($offset in 1, 17, 63) {
                $position = [uint64](($offset * 65536) / $stepSamples)
                $period = [uint64]65536
                $remaining = $period - ($position % $period)
                $next = [uint64](($remaining * $stepSamples + 65535) / 65536)
                $next | Should BeGreaterThan 0
            }
        }
    }

    It 'commits terminal admission only after reserving output and note-off' {
        $seq | Should Match 'ledger_plan'
        $seq | Should Match 'off_ticket=scheduler_add'
        $seq | Should Match 'ledger_commit\(g_seq_fx_core,e,&plan\)'
        $seq | Should Match 'event_count\+required>g_seq_fx_event_limit'
        $seq | Should Match '(?s)off_ticket=scheduler_add.*if\(off_ticket==UINT16_MAX\)return.*ledger_commit'
        $seq | Should Match '(?s)if\(g_seq_fx_block->event_count>=g_seq_fx_event_limit\).*scheduler_add.*return.*ledger_release'
    }

    It 'bounds p-lock transitions and recovers event faults per block' {
        $header | Should Match '#define SEQ_ENGINE_PARAM_EVENT_CAPACITY 1024U'
        $capacity | Should Match 'SEQ_PRODUCT_PARAM_EVENTS_PER_HORIZON == 1024U'
        $seq | Should Match 'core->event_faulted=0U'
        $seq | Should Match 'core->plock_fault_tracks=0U'
        $seq | Should Not Match 'if\(core->event_faulted!=0U\)return'
        1024 | Should BeGreaterThan 256
    }

    It 'preserves complete Groove events and rejects GROUP master ingress' {
        $seq | Should Match 'note_event_t event\[SEQ_PRODUCT_HARMONY_FANOUT_MAX\]'
        $seq | Should Match 'generated_victim'
        $seq | Should Match 'event\.sample_abs=resume\.sample_abs'
        $keyboard = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine_port_h743.c')
        $keyboard | Should Match 'event->track==BRICK_ENTITY_GROUP_MASTER_ID'
    }

    It 'assigns distinct canonical lanes to concurrent live sources' {
        $seq | Should Match 'g_seq_live_lane\[SEQ_ENGINE_LEDGER_CAPACITY\]'
        $seq | Should Match 'event->temporal_index=g_seq_live_lane\[i\]\.lane'
        $seq | Should Match 'used\|=.*g_seq_live_lane\[i\]\.lane'
    }

    It 'bounds open live Echo repeats' {
        $fx | Should Match 'e\.duration_samples==NOTE_EVENT_DURATION_OPEN\)e\.duration_samples=x->delay'
    }

    It 'orders OFF before ON and original before generated' {
        $header | Should Match 'SEQ_ENGINE_EVENT_NOTE_OFF = 0'
        $seq | Should Match 'left->reserved>right->reserved'
        $seq | Should Match 'NOTE_EVENT_FLAG_GENERATED'
    }

    It 'exposes scheduler overflow and timing diagnostics' {
        $header | Should Match 'scheduler_overflow_count'
        $header | Should Match 'p999_cycles'
        $header | Should Match 'max_consecutive_over_75'
    }

    It 'records only after terminal admission' {
        $seq | Should Match 'seq_runtime_live_rec_submit_effective'
        $keyboard | Should Not Match 'seq_runtime_live_rec_note_(on|off)\('
        $seq | Should Match 'source->playback_stage==NOTE_EVENT_STAGE_TERMINAL'
    }
}
