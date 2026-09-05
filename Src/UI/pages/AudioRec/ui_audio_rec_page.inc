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
        if(ev->id == (uint8_t)BTN_PAGE_1)
        {
            ui_hall_mode_flow_close_rec();
        }
        else if(ev->id == (uint8_t)BTN_PAGE_2)
        {
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_TOGGLE_USB
            });
        }
        else if(ev->id == (uint8_t)BTN_PAGE_3)
        {
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_TOGGLE_LINE
            });
        }
        else if(ev->id == (uint8_t)BTN_PAGE_4)
        {
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_TOGGLE_MIC
            });
        }
        return;
    }

    if((ev->type == UI_EVENT_HALL_PRESS) && (ev->id < SAMPLE_CAPTURE_TRACK_COUNT))
    {
        (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
            .operation = CONTROL_AUDIO_REC_TOGGLE_ROUTE,
            .value0 = ev->id
        });
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
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_ASSIGN
            });
            g_ui_rec_edit_assign_popup = 0U;
        }
        return;
    }

    if(ev->type == UI_EVENT_HALL_PRESS)
    {
        sample_capture_ui_state_t state;
        sample_capture_model_get_ui_state(&state);
        if((state.take_valid != 0U) && (state.edit_end_frame > state.edit_start_frame))
        {
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_AUDITION
            });
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
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_RETURN
            });
            ui_page_set(UI_PAGE_AUDIO_REC);
            break;

        case BTN_PAGE_2:
            if (control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                    .operation = CONTROL_AUDIO_REC_SAVE
                }) != 0U)
                g_ui_rec_edit_assign_popup = 1U;
            break;

        case BTN_PAGE_3:
            (void)control_domain_request_audio_rec(&(control_audio_rec_intent_t){
                .operation = CONTROL_AUDIO_REC_TOGGLE_ZCROSS
            });
            break;

        default:
            break;
    }
}

static void ui_page_audio_rec_tick(void)
{
    sample_capture_model_service();
    sample_capture_ui_state_t state;
    sample_capture_model_get_ui_state(&state);
    if(state.view == SAMPLE_CAPTURE_VIEW_AUDIO_REC)
    {
        ui_page_audio_rec_update_meter(state.live_peak_abs_pcm24);
    }
    if(state.view == SAMPLE_CAPTURE_VIEW_REC_EDIT && ui_page_get_id() != UI_PAGE_REC_EDIT)
    {
        ui_page_set(UI_PAGE_REC_EDIT);
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

