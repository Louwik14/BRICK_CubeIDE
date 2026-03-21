/*
   MicroDexed

   MicroDexed is a port of the Dexed sound engine
   (https://github.com/asb2m10/dexed) for the Teensy-3.5/3.6/4.x with audio shield.
   Dexed ist heavily based on https://github.com/google/music-synthesizer-for-android

   (c)2018-2021 H. Wirtz <wirtz@parasitstudio.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef _UI_H_
#define _UI_H_

boolean COND_hide()  // hide a menu element
{
  return false;
}

LCDML_add(0, LCDML_0, 1, "Voice", NULL);
LCDML_add(1, LCDML_0_1, 1, "Select", UI_func_voice_select);
LCDML_add(2, LCDML_0_1, 2, "Audio", NULL);
LCDML_add(3, LCDML_0_1_2, 1, "Voice Level", UI_func_sound_intensity);
LCDML_add(4, LCDML_0_1_2, 2, "Panorama", UI_func_panorama);
LCDML_add(5, LCDML_0_1_2, 3, "Effects", NULL);
LCDML_add(6, LCDML_0_1_2_3, 1, "Chorus", NULL);
LCDML_add(7, LCDML_0_1_2_3_1, 1, "Frequency", UI_func_chorus_frequency);
LCDML_add(8, LCDML_0_1_2_3_1, 2, "Waveform", UI_func_chorus_waveform);
LCDML_add(9, LCDML_0_1_2_3_1, 3, "Depth", UI_func_chorus_depth);
LCDML_add(10, LCDML_0_1_2_3_1, 4, "Level", UI_func_chorus_level);
LCDML_add(11, LCDML_0_1_2_3, 2, "Delay", NULL);
LCDML_add(12, LCDML_0_1_2_3_2, 1, "Time", UI_func_delay_time);
LCDML_add(13, LCDML_0_1_2_3_2, 2, "Feedback", UI_func_delay_feedback);
LCDML_add(14, LCDML_0_1_2_3_2, 3, "Level", UI_func_delay_level);
LCDML_add(15, LCDML_0_1_2_3, 3, "Filter", NULL);
LCDML_add(16, LCDML_0_1_2_3_3, 1, "Cutoff", UI_func_filter_cutoff);
LCDML_add(17, LCDML_0_1_2_3_3, 2, "Resonance", UI_func_filter_resonance);
LCDML_add(18, LCDML_0_1_2_3, 4, "Reverb", NULL);
LCDML_add(19, LCDML_0_1_2_3_4, 1, "Roomsize", UI_func_reverb_roomsize);
LCDML_add(20, LCDML_0_1_2_3_4, 2, "Damping", UI_func_reverb_damping);
LCDML_add(21, LCDML_0_1_2_3_4, 3, "Level", UI_func_reverb_level);
LCDML_add(22, LCDML_0_1_2_3_4, 4, "Reverb Send", UI_func_reverb_send);
LCDML_add(23, LCDML_0_1_2_3, 5, "EQ", NULL);
LCDML_add(24, LCDML_0_1_2_3_5, 1, "Bass", UI_func_eq_bass);
LCDML_add(25, LCDML_0_1_2_3_5, 2, "Treble", UI_func_eq_treble);
LCDML_add(26, LCDML_0_1, 3, "Controller", NULL);
LCDML_add(27, LCDML_0_1_3, 1, "Pitchbend", NULL);
LCDML_add(28, LCDML_0_1_3_1, 1, "PB Range", UI_func_pb_range);
LCDML_add(29, LCDML_0_1_3_1, 2, "PB Step", UI_func_pb_step);
LCDML_add(30, LCDML_0_1_3, 2, "Mod Wheel", NULL);
LCDML_add(31, LCDML_0_1_3_2, 1, "MW Range", UI_func_mw_range);
LCDML_add(32, LCDML_0_1_3_2, 2, "MW Assign", UI_func_mw_assign);
LCDML_add(33, LCDML_0_1_3_2, 3, "MW Mode", UI_func_mw_mode);
LCDML_add(34, LCDML_0_1_3, 3, "Aftertouch", NULL);
LCDML_add(35, LCDML_0_1_3_3, 1, "AT Range", UI_func_at_range);
LCDML_add(36, LCDML_0_1_3_3, 2, "AT Assign", UI_func_at_assign);
LCDML_add(37, LCDML_0_1_3_3, 3, "AT Mode", UI_func_at_mode);
LCDML_add(38, LCDML_0_1_3, 4, "Foot Ctrl", NULL);
LCDML_add(39, LCDML_0_1_3_4, 1, "FC Range", UI_func_fc_range);
LCDML_add(40, LCDML_0_1_3_4, 2, "FC Assign", UI_func_fc_assign);
LCDML_add(41, LCDML_0_1_3_4, 3, "FC Mode", UI_func_fc_mode);
LCDML_add(42, LCDML_0_1_3, 5, "Breath Ctrl", NULL);
LCDML_add(43, LCDML_0_1_3_5, 1, "BC Range", UI_func_bc_range);
LCDML_add(44, LCDML_0_1_3_5, 2, "BC Assign", UI_func_bc_assign);
LCDML_add(45, LCDML_0_1_3_5, 3, "BC Mode", UI_func_bc_mode);
LCDML_add(46, LCDML_0_1, 4, "MIDI", NULL);
LCDML_add(47, LCDML_0_1_4, 1, "MIDI Channel", UI_func_midi_channel);
LCDML_add(48, LCDML_0_1_4, 2, "Lowest Note", UI_func_lowest_note);
LCDML_add(49, LCDML_0_1_4, 3, "Highest Note", UI_func_highest_note);
LCDML_add(50, LCDML_0_1_4, 4, "MIDI Send Voice", UI_func_sysex_send_voice);
LCDML_add(51, LCDML_0_1, 5, "Setup", NULL);
LCDML_add(52, LCDML_0_1_5, 1, "Portamento", NULL);
LCDML_add(53, LCDML_0_1_5_1, 1, "Port. Mode", UI_func_portamento_mode);
LCDML_add(54, LCDML_0_1_5_1, 2, "Port. Gliss", UI_func_portamento_glissando);
LCDML_add(55, LCDML_0_1_5_1, 3, "Port. Time", UI_func_portamento_time);
LCDML_add(56, LCDML_0_1_5, 2, "Polyphony", UI_func_polyphony);
LCDML_add(57, LCDML_0_1_5, 3, "Transpose", UI_func_transpose);
LCDML_add(58, LCDML_0_1_5, 4, "Fine Tune", UI_func_tune);
LCDML_add(59, LCDML_0_1_5, 5, "Mono/Poly", UI_func_mono_poly);
LCDML_add(60, LCDML_0_1, 6, "Internal", NULL);
LCDML_add(61, LCDML_0_1_6, 1, "Note Refresh", UI_func_note_refresh);
LCDML_add(62, LCDML_0_1_6, 2, "Velocity Lvl", UI_func_velocity_level);
LCDML_add(63, LCDML_0_1_6, 3, "Engine", UI_func_engine);
LCDML_add(64, LCDML_0_1, 7, "Operator", UI_handle_OP);
LCDML_add(65, LCDML_0_1, 8, "Save Voice", UI_func_save_voice);
LCDML_add(66, LCDML_0, 3, "Load/Save", NULL);
LCDML_add(67, LCDML_0_3, 1, "Performance", NULL);
LCDML_add(68, LCDML_0_3_1, 1, "Load Perf.", UI_func_load_performance);
LCDML_add(69, LCDML_0_3_1, 2, "Save Perf.", UI_func_save_performance);
LCDML_add(70, LCDML_0_3, 2, "Voice Config", NULL);
LCDML_add(71, LCDML_0_3_2, 1, "Load Voice Cfg", UI_func_load_voiceconfig);
LCDML_add(72, LCDML_0_3_2, 2, "Save Voice Cfg", UI_func_save_voiceconfig);
LCDML_add(73, LCDML_0_3, 3, "Effects", NULL);
LCDML_add(74, LCDML_0_3_3, 1, "Load Effects", UI_func_load_fx);
LCDML_add(75, LCDML_0_3_3, 2, "Save Effects", UI_func_save_fx);
LCDML_add(76, LCDML_0_3, 5, "MIDI", NULL);
LCDML_add(77, LCDML_0_3_5, 1, "MIDI Recv Bank", UI_func_sysex_receive_bank);
LCDML_add(78, LCDML_0_3_5, 2, "MIDI Snd Bank", UI_func_sysex_send_bank);
LCDML_add(79, LCDML_0_3_5, 3, "MIDI Snd Voice", UI_func_sysex_send_voice);
LCDML_add(80, LCDML_0, 4, "System", NULL);
//LCDML_add(81, LCDML_0_4, 1, "Volume", UI_func_volume);
LCDML_add(81, LCDML_0_4, 1, "Stereo/Mono", UI_func_stereo_mono);
LCDML_add(82, LCDML_0_4, 2, "MIDI Soft THRU", UI_func_midi_soft_thru);
LCDML_add(83, LCDML_0_4, 3, "EEPROM Reset", UI_func_eeprom_reset);
LCDML_add(84, LCDML_0, 6, "Info", UI_func_information);
LCDML_addAdvanced(85, LCDML_0, 7, COND_hide, "Volume", UI_func_volume, 0, _LCDML_TYPE_default);
#define _LCDML_DISP_cnt 85
#endif
