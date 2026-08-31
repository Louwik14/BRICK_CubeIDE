static void ui_page_audio_rec_enter(void)
{
    sample_capture_model_set_view(SAMPLE_CAPTURE_VIEW_AUDIO_REC);
    g_ui_audio_rec_smoothed_peak = 0U;
}

static void ui_page_rec_edit_enter(void)
{
    sample_capture_model_set_view(SAMPLE_CAPTURE_VIEW_REC_EDIT);
    g_ui_rec_edit_assign_popup = 0U;
}

static void ui_page_audio_rec_handle_event(const ui_event_t *ev)
{
    if(ev == 0)
    {
        return;
    }

    if(ev->type == UI_EVENT_BUTTON_PRESS)
    {
        if(ev->id == (uint8_t)BTN_PAGE_3)
        {
            (void)sample_capture_model_toggle_line();
        }
        else if(ev->id == (uint8_t)BTN_PAGE_4)
        {
            (void)sample_capture_model_toggle_mic();
        }
        return;
    }

    if((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SAMPLE_CAPTURE_TRACK_COUNT))
    {
        (void)sample_capture_model_toggle_route(ev->id);
    }
}

static void ui_page_rec_edit_handle_event(const ui_event_t *ev)
{
    if(ev == 0)
    {
        return;
    }

    if(g_ui_rec_edit_assign_popup != 0U)
    {
        if(ev->type != UI_EVENT_BUTTON_PRESS)
        {
            return;
        }
        if(ev->id == (uint8_t)BTN_PAGE_1)
        {
            g_ui_rec_edit_assign_popup = 0U;
        }
        else if(ev->id == (uint8_t)BTN_PAGE_2)
        {
            (void)sample_capture_model_assign_saved_take_to_pool();
            g_ui_rec_edit_assign_popup = 0U;
        }
        return;
    }

    if(ev->type == UI_EVENT_HALL_PRESS)
    {
        sample_capture_state_t state;
        sample_capture_model_get_state(&state);
        if((state.take_valid != 0U) && (state.edit_end_frame > state.edit_start_frame))
        {
            (void)sample_capture_model_audition_trimmed();
        }
        return;
    }

    if(ev->type != UI_EVENT_BUTTON_PRESS)
    {
        return;
    }

    switch((button_id_t)ev->id)
    {
        case BTN_PAGE_1:
            (void)sample_capture_model_return_to_audio_rec();
            ui_page_set(UI_PAGE_AUDIO_REC);
            break;

        case BTN_PAGE_2:
            if(sample_capture_model_save_trimmed() != 0U)
            {
                g_ui_rec_edit_assign_popup = 1U;
            }
            break;

        case BTN_PAGE_3:
            (void)sample_capture_model_toggle_zcross();
            break;

        default:
            break;
    }
}

static void ui_page_audio_rec_tick(void)
{
    sample_capture_model_service();
    sample_capture_state_t state;
    sample_capture_model_get_state(&state);
    if(state.view == SAMPLE_CAPTURE_VIEW_AUDIO_REC)
    {
        ui_page_audio_rec_update_meter(state.live_peak_abs_pcm24);
    }
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT && ui_page_get_id() != UI_PAGE_REC_EDIT)
    {
        ui_page_set(UI_PAGE_REC_EDIT);
    }
}

static const char *ui_page_audio_rec_phase_label(sample_capture_phase_t phase)
{
    switch(phase)
    {
        case SAMPLE_CAPTURE_PHASE_IDLE: return "IDLE";
        case SAMPLE_CAPTURE_PHASE_ARMED: return "ARMED";
        case SAMPLE_CAPTURE_PHASE_WAIT_QUANT: return "WAIT";
        case SAMPLE_CAPTURE_PHASE_RECORDING: return "REC";
        case SAMPLE_CAPTURE_PHASE_STOPPING: return "STOP";
        case SAMPLE_CAPTURE_PHASE_REC_EDIT: return "EDIT";
        case SAMPLE_CAPTURE_PHASE_SAVED: return "SAVED";
        case SAMPLE_CAPTURE_PHASE_ERROR: return "ERROR";
        default: return "?";
    }
}

static const char *ui_page_audio_rec_quant_label(sample_capture_quant_t quant)
{
    switch(quant)
    {
        case SAMPLE_CAPTURE_QUANT_NOW: return "NOW";
        case SAMPLE_CAPTURE_QUANT_BAR: return "BAR";
        case SAMPLE_CAPTURE_QUANT_PATTERN: return "PAT";
        default: return "?";
    }
}

static const char *ui_page_audio_rec_error_label(sample_capture_error_t error)
{
    switch(error)
    {
        case SAMPLE_CAPTURE_ERROR_NONE: return "";
        case SAMPLE_CAPTURE_ERROR_INVALID_ARG: return "INVALID";
        case SAMPLE_CAPTURE_ERROR_NO_ROUTE: return "NO ROUTE";
        case SAMPLE_CAPTURE_ERROR_LOOPER_ACTIVE: return "LOOPER REC";
        case SAMPLE_CAPTURE_ERROR_SAMPLE_ACTIVE: return "REC BUSY";
        case SAMPLE_CAPTURE_ERROR_SD_BUSY: return "SD BUSY";
        case SAMPLE_CAPTURE_ERROR_SD_IO: return "SD IO";
        case SAMPLE_CAPTURE_ERROR_NO_TAKE: return "NO TAKE";
        case SAMPLE_CAPTURE_ERROR_NO_SLOT: return "POOL FULL";
        case SAMPLE_CAPTURE_ERROR_LOAD_FAIL: return "LOAD FAIL";
        case SAMPLE_CAPTURE_ERROR_PREVIEW_FAIL: return "PREVIEW";
        default: return "ERROR";
    }
}

