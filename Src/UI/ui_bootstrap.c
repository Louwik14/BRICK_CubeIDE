#include "ui_bootstrap.h"

#include "pages/ui_page_param_test.h"
#include "pages/ui_page_debug_hall.h"
#include "pages/ui_page_calibration.h"
#include "pages/ui_page_template_env.h"
#include "pages/ui_page_template_tone.h"
#include "pages/ui_page_template_mod.h"
#include "pages/ui_page_template_cfg.h"
#include "pages/ui_page_template_keyboard.h"
#include "pages/ui_page_midi_fx.h"
#include "pages/ui_page_template_seq.h"
#include "pages/ui_page_template_macro.h"
#include "pages/ui_page_template_mix.h"
#include "pages/ui_page_template_play.h"
#include "pages/ui_page_audio_rec.h"
#include "pages/ui_page_patch_assign.h"
#include "pages/ui_page_kit_assign.h"
#include "pages/ui_page_name_edit.h"
#include "pages/ui_page_settings.h"
#include "pages/ui_page_lowcost_button_test.h"
#include "pages/ui_page_stream_calibration.h"
#include "ui_page_manager.h"
#include "ui_template_page.h"
#include "lowcost_button_test_config.h"
#include "Core/stream_calibration.h"

/* Keep the stable page-ID slot formerly occupied by the removed standalone UI. */
static const ui_page_t g_ui_page_reserved_legacy_slot = { 0 };

void ui_bootstrap_init(void)
{
    ui_template_family_registry_init();
    ui_page_template_env_register_families();
    ui_page_template_cfg_register_families();
    ui_page_template_tone_register_families();
    ui_page_template_mod_register_families();
    ui_page_template_keyboard_register_families();
    ui_page_template_midi_fx_register_families();
    ui_page_template_seq_register_families();
    ui_page_template_mix_register_families();
    ui_page_template_play_register_families();

    ui_page_manager_init();

    /*
     * Register pages once at boot. Registration order defines stable page IDs
     * used by the navigation rule table.
     */
    ui_page_manager_register(&g_ui_page_param_test);
    ui_page_manager_register(&g_ui_page_debug_hall);
    ui_page_manager_register(&g_ui_page_calibration);
    ui_page_manager_register(&g_ui_page_user_calibration);
    ui_page_manager_register(&g_ui_page_template_env);
    ui_page_manager_register(&g_ui_page_template_cfg);
    ui_page_manager_register(&g_ui_page_template_rec_cfg);
    ui_page_manager_register(&g_ui_page_template_tone);
    ui_page_manager_register(&g_ui_page_template_mod);
    ui_page_manager_register(&g_ui_page_template_keyboard);
    ui_page_manager_register(&g_ui_page_midi_fx);
    ui_page_manager_register(&g_ui_page_template_seq);
    ui_page_manager_register(&g_ui_page_template_macro);
    ui_page_manager_register(&g_ui_page_template_mix);
    ui_page_manager_register(&g_ui_page_template_play);
#if BRICK6_STREAM_CALIBRATION
    ui_page_manager_register(&g_ui_page_stream_calibration);
#else
    ui_page_manager_register(&g_ui_page_reserved_legacy_slot);
#endif
    ui_page_manager_register(&g_ui_page_audio_rec);
    ui_page_manager_register(&g_ui_page_rec_edit);
    ui_page_manager_register(&g_ui_page_patch_assign);
    ui_page_manager_register(&g_ui_page_kit_assign);
    ui_page_manager_register(&g_ui_page_name_edit);
    ui_page_manager_register(&g_ui_page_settings);
#if LOWCOST_BUTTON_TEST_PAGE
    ui_page_manager_register(&g_ui_page_lowcost_button_test);
#else
    ui_page_manager_register(&g_ui_page_reserved_legacy_slot);
#endif
    ui_page_manager_register(&g_ui_page_reserved_legacy_slot);

#if BRICK6_STREAM_CALIBRATION
    ui_page_set(UI_PAGE_STREAM_CALIBRATION);
#else
    ui_page_set(UI_PAGE_CALIBRATION);
#endif
}
