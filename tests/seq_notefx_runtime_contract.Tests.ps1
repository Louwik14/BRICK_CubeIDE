$root = Split-Path -Parent $PSScriptRoot
$header = Get-Content -Raw (Join-Path $root 'Inc/Seq/seq_engine.h')
$seq = Get-Content -Raw (Join-Path $root 'Src/Seq/seq_engine.c')
$fx = Get-Content -Raw (Join-Path $root 'Src/NoteFx/note_fx_engine.c')
$keyboard = Get-Content -Raw (Join-Path $root 'Src/Keyboard/keyboard_engine.c')

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
        $fx | Should Match 'note_fx_echo_state_t g_echo\[64U\]'
        $fx | Should Match 'if\(promised<x->next_due\)x->next_due=promised'
        $fx | Should Match 'while\(x->active&&x->next_due<end\)'
        $fx | Should Not Match 'for\(uint8_t rep=1;rep<=r->p2'
    }

    It 'bounds Groove resumes and clamps negative live timing' {
        $seq | Should Match 'if\(count>=2U\).*scheduler_release'
        $fx | Should Match 'NOTE_EVENT_SOURCE_KEY\|\|e->provenance==NOTE_EVENT_SOURCE_MIDI'
        $fx | Should Match 'x.sample_abs<e->sample_abs\)x.sample_abs=e->sample_abs'
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
