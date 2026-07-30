#include <stddef.h>
#include "Storage/patch_sd_bank.h"

const unsigned long brick6_patch_abi_values[] = {
    sizeof(track_mod_lfo_state_t),
    sizeof(track_mod_env3_state_t),
    sizeof(track_mod_multi_state_t),
    sizeof(track_mod_slew_state_t),
    sizeof(track_mod_matrix_slot_t),
    sizeof(track_sound_state_t),
    sizeof(track_tone_sound_state_t),
    sizeof(patch_v1_asset_ref_t),
    sizeof(patch_v1_metadata_t),
    sizeof(patch_v1_member_t),
    sizeof(PatchSaveV1),
    sizeof(patch_sd_slot_header_t),
    offsetof(patch_v1_member_t, sound),
    offsetof(patch_v1_member_t, tone),
    offsetof(patch_v1_member_t, asset),
    offsetof(PatchSaveV1, members),
    offsetof(track_sound_state_t, mod_lfo),
    offsetof(track_sound_state_t, mod_env3),
    offsetof(track_sound_state_t, mod_matrix),
    offsetof(track_sound_state_t, mod_matrix_selected_slot),
    offsetof(track_tone_sound_state_t, prism),
    offsetof(track_tone_sound_state_t, stack),
    offsetof(track_tone_sound_state_t, wave),
    offsetof(track_tone_sound_state_t, deluge)
};

_Static_assert(sizeof(float) == 4U, "Patch ABI requires IEEE-754 binary32");
_Static_assert(sizeof(param_id_t) == 2U, "Patch ABI requires 16-bit param_id_t");
_Static_assert(sizeof(track_sound_state_t) == 260U, "track_sound_state_t ABI changed");
_Static_assert(sizeof(track_tone_sound_state_t) == 512U, "track_tone_sound_state_t ABI changed");
_Static_assert(sizeof(patch_v1_asset_ref_t) == 166U, "patch asset ABI changed");
_Static_assert(sizeof(patch_v1_metadata_t) == 40U, "patch metadata ABI changed");
_Static_assert(sizeof(patch_v1_member_t) == 952U, "patch member ABI changed");
_Static_assert(sizeof(PatchSaveV1) == 3848U, "PatchSaveV1 ABI changed");
_Static_assert(sizeof(patch_sd_slot_header_t) == 56U, "patch header ABI changed");
_Static_assert(offsetof(patch_v1_member_t, sound) == 12U, "member.sound offset changed");
_Static_assert(offsetof(patch_v1_member_t, tone) == 272U, "member.tone offset changed");
_Static_assert(offsetof(patch_v1_member_t, asset) == 784U, "member.asset offset changed");
_Static_assert(offsetof(PatchSaveV1, members) == 40U, "payload.members offset changed");
_Static_assert(offsetof(track_sound_state_t, mod_lfo) == 108U, "sound.mod_lfo offset changed");
_Static_assert(offsetof(track_sound_state_t, mod_env3) == 176U, "sound.mod_env3 offset changed");
_Static_assert(offsetof(track_sound_state_t, mod_matrix) == 192U, "sound.mod_matrix offset changed");
_Static_assert(offsetof(track_sound_state_t, mod_matrix_selected_slot) == 256U,
               "sound.mod_matrix_selected_slot offset changed");
_Static_assert(offsetof(track_tone_sound_state_t, prism) == 164U, "tone.prism offset changed");
_Static_assert(offsetof(track_tone_sound_state_t, stack) == 236U, "tone.stack offset changed");
_Static_assert(offsetof(track_tone_sound_state_t, wave) == 320U, "tone.wave offset changed");
_Static_assert(offsetof(track_tone_sound_state_t, deluge) == 400U, "tone.deluge offset changed");
