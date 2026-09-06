static void ui_page_template_tone_update_synth_live_request(void);

static void ui_page_template_tone_enter(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_enter();
    ui_page_template_tone_update_synth_live_request();
}

static void ui_page_template_tone_update_synth_live_request(void)
{
    ui_template_edit_context_t context;
    uint8_t enabled = 0U;
    uint8_t mask = 0U;
    brick_entity_id_t entity = BRICK_ENTITY_INVALID_ID;
    synth_waveform_engine_t engine = SYNTH_WAVEFORM_ENGINE_NONE;
    if ((g_ui_template_tone_global_master == 0U)
            && (g_ui_template_tone_subset == 0U)
            && (ui_template_edit_context_resolve_active(&context) != 0U)
            && (ui_get_track_family(context.owner_entity) == TRACK_FAMILY_SYNTH))
    {
        const track_type_t type = ui_get_track_type(context.owner_entity);
        const uint8_t page = g_ui_template_tone_state.active_subpage;
        if (((type == TRACK_TYPE_WAVE) && (page < 3U))
                || ((type == TRACK_TYPE_PRISM) && (page < 4U)))
        {
            entity = context.owner_entity;
            enabled = 1U;
            engine = (type == TRACK_TYPE_WAVE)
                ? SYNTH_WAVEFORM_ENGINE_WAVE : SYNTH_WAVEFORM_ENGINE_PRISM;
            mask = (page == 0U) ? 0x01U : ((page == 1U) ? 0x02U : 0x03U);
        }
    }
    (void)control_audio_visual_synth_request(enabled, entity, engine, mask);
}

static void ui_page_template_tone_leave(void)
{
    (void)control_audio_visual_synth_request(0U, 0U,
                                            SYNTH_WAVEFORM_ENGINE_NONE, 0U);
    ui_template_page_leave();
    g_ui_template_tone_global_master = 0U;
}

static void ui_page_template_tone_handle_event(const ui_event_t *ev)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_handle_event(ev);
    ui_page_template_tone_sync_drum_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, g_ui_template_tone_state.active_subpage);
    ui_page_template_tone_update_synth_live_request();
}

static void ui_page_template_tone_sync_active_context(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_select_subpage(&g_ui_template_tone_state, g_ui_template_tone_state.active_subpage);
    ui_template_page_sync_active_track_context();
    ui_page_template_tone_update_synth_live_request();
}

static void ui_page_template_tone_render(void)
{
    ui_page_template_tone_sync_drum_family();
    ui_template_page_render();
}

const ui_page_t g_ui_page_template_tone = {
    .enter = ui_page_template_tone_enter,
    .leave = ui_page_template_tone_leave,
    .handle_event = ui_page_template_tone_handle_event,
    .sync_active_context = ui_page_template_tone_sync_active_context,
    .render = ui_page_template_tone_render,
    .context = &g_ui_template_tone_state,
};
