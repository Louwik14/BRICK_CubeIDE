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

#ifdef ENABLE_LCD_UI
#ifndef _UI_HPP_
#define _UI_HPP_
#endif

#include "config.h"
#include "disp_plus.h"
#include "effect_modulated_delay.h"
#include "effect_stereo_mono.h"
#ifdef USE_PLATEREVERB
#include "effect_platervbstereo.h"
#else
#include "effect_freeverbf.h"
#endif
#include "dexed.h"

#include <LCDMenuLib2.h>
//#include <Encoder.h>
#include <MD_REncoder.h>

#define _LCDML_DISP_cols  LCD_cols
#define _LCDML_DISP_rows  LCD_rows

#ifdef I2C_DISPLAY
#define _LCDML_DISP_cfg_cursor                     0x7E   // cursor Symbol
#else
#define _LCDML_DISP_cfg_cursor                     0x8d   // cursor Symbol
#endif
#define _LCDML_DISP_cfg_scrollbar                  1      // enable a scrollbar

extern config_t configuration;
extern void set_volume(uint8_t v, uint8_t m);
extern bool load_sysex(uint8_t b, uint8_t v);
extern void generate_version_string(char* buffer, uint8_t len);
extern void initial_values_from_eeprom(bool init);
extern void _softRestart(void);
extern void eeprom_update_sys(void);
extern void eeprom_update_performance(void);
extern void eeprom_update_fx(void);
extern void eeprom_update_dexed(uint8_t instance_id);
extern float pseudo_log_curve(float value);
extern uint8_t selected_instance_id;
extern char receive_bank_filename[FILENAME_LEN];

#ifdef DISPLAY_LCD_SPI
extern void change_disp_sd(bool d);
#endif

extern AudioSourceMicroDexed*     MicroDexed[NUM_DEXED];
#if defined(USE_FX)
extern AudioSynthWaveform*        chorus_modulator[NUM_DEXED];
extern AudioEffectModulatedDelay* modchorus[NUM_DEXED];
extern AudioMixer4*               chorus_mixer[NUM_DEXED];
extern AudioMixer4*               delay_fb_mixer[NUM_DEXED];
extern AudioEffectDelay*          delay_fx[NUM_DEXED];
extern AudioMixer4*               delay_mixer[NUM_DEXED];
#endif
extern AudioEffectMonoStereo*     mono2stereo[NUM_DEXED];

extern AudioMixer4                microdexed_peak_mixer;
extern AudioAnalyzePeak           microdexed_peak;
#if defined(USE_FX)
extern AudioMixer4                reverb_mixer_r;
extern AudioMixer4                reverb_mixer_l;
#ifdef USE_PLATEREVERB
extern AudioEffectPlateReverb     reverb;
#else
extern AudioEffectFreeverb        freeverb_r;
extern AudioEffectFreeverb        freeverb_l;
#endif
#endif
extern AudioMixer4                master_mixer_r;
extern AudioMixer4                master_mixer_l;
extern AudioEffectStereoMono      stereo2mono;
extern AudioAnalyzePeak           master_peak_r;
extern AudioAnalyzePeak           master_peak_l;

extern char sd_string[LCD_cols + 1];
extern char g_voice_name[NUM_DEXED][VOICE_NAME_LEN];
extern char g_bank_name[NUM_DEXED][BANK_NAME_LEN];


/***********************************************************************
   GLOBAL
************************************************************************/
elapsedMillis back_from_volume;
uint8_t instance_num[8][8];
const char accepted_chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-abcdefghijklmnopqrstuvwxyz";

#ifdef I2C_DISPLAY
#include <LiquidCrystal_I2C.h>
Disp_Plus<LiquidCrystal_I2C> lcd(LCD_I2C_ADDRESS, _LCDML_DISP_cols, _LCDML_DISP_rows);
#endif

#ifdef U8X8_DISPLAY
#include <U8x8lib.h>
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
Disp_Plus<U8X8_DISPLAY_CLASS> lcd(/* cs=*/ U8X8_CS_PIN, /* dc=*/ U8X8_DC_PIN, /* reset=*/ U8X8_RESET_PIN);
//Disp_Plus<U8X8_DISPLAY_CLASS> lcd(U8X8_PIN_NONE);
#endif

const uint8_t scroll_bar[5][8] = {
  {B10001, B10001, B10001, B10001, B10001, B10001, B10001, B10001}, // scrollbar top
  {B11111, B11111, B10001, B10001, B10001, B10001, B10001, B10001}, // scroll state 1
  {B10001, B10001, B11111, B11111, B10001, B10001, B10001, B10001}, // scroll state 2
  {B10001, B10001, B10001, B10001, B11111, B11111, B10001, B10001}, // scroll state 3
  {B10001, B10001, B10001, B10001, B10001, B10001, B11111, B11111}  // scrollbar bottom
};

const uint8_t block_bar[5][8] = {
  {B10000, B10000, B10000, B10000, B10000, B10000, B10000, B10000},
  {B11000, B11000, B11000, B11000, B11000, B11000, B11000, B11000},
  {B11100, B11100, B11100, B11100, B11100, B11100, B11100, B11100},
  {B11110, B11110, B11110, B11110, B11110, B11110, B11110, B11110},
  {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111}
};

const uint8_t meter_bar[5][8] = {
  {B10000, B10000, B10000, B10000, B10000, B10000, B10000, B10000},
  {B01000, B01000, B01000, B01000, B01000, B01000, B01000, B01000},
  {B00100, B00100, B00100, B00100, B00100, B00100, B00100, B00100},
  {B00010, B00010, B00010, B00010, B00010, B00010, B00010, B00010},
  {B00001, B00001, B00001, B00001, B00001, B00001, B00001, B00001}
};

const uint8_t special_chars[18][8] = {
  {B11111, B11011, B10011, B11011, B11011, B11011, B11011, B11111}, //  [0] 1 small invers
  {B11111, B11011, B10101, B11101, B11011, B10111, B10001, B11111}, //  [1] 2 small invers
  {B11111, B11011, B10011, B11011, B11011, B11011, B11011, B11111}, //  [2] 1 OP invers
  {B11111, B11011, B10101, B11101, B11011, B10111, B10001, B11111}, //  [3] 2 OP invers
  {B11111, B10001, B11101, B11011, B11101, B10101, B11011, B11111}, //  [4] 3 OP invers
  {B11111, B10111, B10111, B10101, B10001, B11101, B11101, B11111}, //  [5] 4 OP invers
  {B11111, B10001, B10111, B10011, B11101, B11101, B10011, B11111}, //  [6] 5 OP invers
  {B11111, B11001, B10111, B10011, B10101, B10101, B11011, B11111}, //  [7] 6 OP invers
  {B00000, B00000, B00000, B00000, B00000, B00000, B00000, B11111}, //  [8] Level 1
  {B00000, B00000, B00000, B00000, B00000, B00000, B11111, B11111}, //  [9] Level 2
  {B00000, B00000, B00000, B00000, B00000, B11111, B11111, B11111}, // [10] Level 3
  {B00000, B00000, B00000, B00000, B11111, B11111, B11111, B11111}, // [11] Level 4
  {B00000, B00000, B00000, B11111, B11111, B11111, B11111, B11111}, // [12] Level 5
  {B00000, B00000, B11111, B11111, B11111, B11111, B11111, B11111}, // [13] Level 6
  {B00000, B11111, B11111, B11111, B11111, B11111, B11111, B11111}, // [14] Level 7
  {B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111}, // [15] Level 8
  {B00100, B00110, B00101, B00101, B01101, B11101, B11100, B11000}, // [16] Note
  //{B01110, B10001, B10001, B11111, B11011, B11011, B11111, B00000}  // [17] Disabled 2nd instance symbol
  {B01110, B10001, B10001, B01110, B00100, B00100, B00110, B00110}  // [17] Disabled 2nd instance symbol
};

enum { SCROLLBAR, BLOCKBAR, METERBAR };
enum { ENC_R, ENC_L };
enum {MENU_VOICE_BANK, MENU_VOICE_SOUND};

void lcdml_menu_display(void);
void lcdml_menu_clear(void);
void lcdml_menu_control(void);
#ifdef USE_FX
void UI_func_reverb_roomsize(uint8_t param);
void UI_func_reverb_damping(uint8_t param);
void UI_func_reverb_level(uint8_t param);
void UI_func_chorus_frequency(uint8_t param);
void UI_func_chorus_waveform(uint8_t param);
void UI_func_chorus_depth(uint8_t param);
void UI_func_chorus_level(uint8_t param);
void UI_func_delay_time(uint8_t param);
void UI_func_delay_feedback(uint8_t param);
void UI_func_delay_level(uint8_t param);
void UI_func_reverb_send(uint8_t param);
void UI_func_filter_cutoff(uint8_t param);
void UI_func_filter_resonance(uint8_t param);
#endif
void UI_func_transpose(uint8_t param);
void UI_func_tune(uint8_t param);
void UI_func_midi_channel(uint8_t param);
void UI_func_lowest_note(uint8_t param);
void UI_func_highest_note(uint8_t param);
void UI_func_sound_intensity(uint8_t param);
void UI_func_panorama(uint8_t param);
void UI_func_stereo_mono(uint8_t param);
void UI_func_note_refresh(uint8_t param);
void UI_func_polyphony(uint8_t param);
void UI_func_engine(uint8_t param);
void UI_func_mono_poly(uint8_t param);
void UI_func_pb_range(uint8_t param);
void UI_func_pb_step(uint8_t param);
void UI_func_mw_range(uint8_t param);
void UI_func_mw_assign(uint8_t param);
void UI_func_mw_mode(uint8_t param);
void UI_func_fc_range(uint8_t param);
void UI_func_fc_assign(uint8_t param);
void UI_func_fc_mode(uint8_t param);
void UI_func_bc_range(uint8_t param);
void UI_func_bc_assign(uint8_t param);
void UI_func_bc_mode(uint8_t param);
void UI_func_at_range(uint8_t param);
void UI_func_at_assign(uint8_t param);
void UI_func_at_mode(uint8_t param);
void UI_func_portamento_mode(uint8_t param);
void UI_func_portamento_glissando(uint8_t param);
void UI_func_portamento_time(uint8_t param);
void UI_handle_OP(uint8_t param);
void UI_func_information(uint8_t param);
void UI_func_volume(uint8_t param);
void UI_func_load_performance(uint8_t param);
void UI_func_save_performance(uint8_t param);
void UI_func_load_voiceconfig(uint8_t param);
void UI_func_save_voiceconfig(uint8_t param);
void UI_func_save_voice(uint8_t param);
void UI_func_load_fx(uint8_t param);
void UI_func_save_fx(uint8_t param);
void UI_func_eeprom_reset(uint8_t param);
void UI_func_midi_soft_thru(uint8_t param);
void UI_func_velocity_level(uint8_t param);
void UI_func_voice_select(uint8_t param);
void UI_func_sysex_send_voice(uint8_t param);
void UI_func_sysex_receive_bank(uint8_t param);
void UI_func_sysex_send_bank(uint8_t param);
void UI_func_eq_bass(uint8_t param);
void UI_func_eq_treble(uint8_t param);
void UI_function_not_enabled(void);
void UI_function_not_implemented(uint8_t param);
bool UI_select_name(uint8_t y, uint8_t x, char* edit_string, uint8_t len, bool init);
uint8_t search_accepted_char(uint8_t c);
void lcd_display_int(int16_t var, uint8_t size, bool zeros, bool brackets, bool sign);
void lcd_display_float(float var, uint8_t size_number, uint8_t size_fraction, bool zeros, bool brackets, bool sign);
void lcd_display_bar_int(const char* title, uint32_t value, float factor, int32_t min_value, int32_t max_value, uint8_t size, bool zeros, bool sign, bool init);
void lcd_display_bar_float(const char* title, float value, float factor, int32_t min_value, int32_t max_value, uint8_t size_number, uint8_t size_fraction, bool zeros, bool sign, bool init);
void lcd_display_meter_int(const char* title, uint32_t value, float factor, float offset, int32_t min_value, int32_t max_value, uint8_t size, bool zeros, bool sign, bool init);
void lcd_display_meter_float(const char* title, float value, float factor, float offset, int32_t min_value, int32_t max_value, uint8_t size_number, uint8_t size_fraction, bool zeros, bool sign, bool init);
void lcd_active_instance_number(uint8_t instance_id);
void lcd_OP_active_instance_number(uint8_t instance_id, uint8_t op);
void lcd_special_chars(uint8_t mode);
void eeprom_update_var(uint16_t pos, uint8_t val, const char* val_string);
void string_trim(char *s);

// normal menu
LCDMenuLib2_menu LCDML_0(255, 0, 0, NULL, NULL); // normal root menu element (do not change)
LCDMenuLib2 LCDML(LCDML_0, _LCDML_DISP_rows, _LCDML_DISP_cols, lcdml_menu_display, lcdml_menu_clear, lcdml_menu_control);

#if defined(USE_FX)
#include "UI_FX.h"
#else
#include "UI_NO_FX.h"
#endif

// create menu
LCDML_createMenu(_LCDML_DISP_cnt);

/***********************************************************************
   CONTROL
 ***********************************************************************/
class EncoderDirection
{
  public:
    EncoderDirection(void)
    {
      reset();
    }

    void reset(void)
    {
      button_short = false;
      button_long = false;
      button_pressed = false;
      left = false;
      right = false;
      up = false;
      down = false;
    }

    void ButtonShort(bool state)
    {
      button_short = state;
    }

    bool ButtonShort(void)
    {
      if (button_short == true)
      {
        button_short = false;
        return (true);
      }
      return (false);
    }

    void ButtonLong(bool state)
    {
      button_long = state;
    }

    bool ButtonLong(void)
    {
      if (button_long == true)
      {
        button_long = false;
        return (true);
      }
      return (false);
    }

    void ButtonPressed(bool state)
    {
      button_pressed = state;
    }

    bool ButtonPressed(void)
    {
      return (button_pressed);
    }

    void Left(bool state)
    {
      left = state;
    }

    bool Left(void)
    {
      if (left == true)
      {
        left = false;
        return (true);
      }
      return (false);
    }

    void Right(bool state)
    {
      right = state;
    }

    bool Right(void)
    {
      if (right == true)
      {
        right = false;
        return (true);
      }
      return (false);
    }

    void Up(bool state)
    {
      up = state;
    }

    bool Up(void)
    {
      if (up == true)
      {
        up = false;
        return (true);
      }
      return (false);
    }

    void Down(bool state)
    {
      down = state;
    }

    bool Down(void)
    {
      if (down == true)
      {
        down = false;
        return (true);
      }
      return (false);
    }

  private:
    bool button_short;
    bool button_long;
    bool button_pressed;
    bool left;
    bool right;
    bool up;
    bool down;
};

//Encoder ENCODER[NUM_ENCODER] = {Encoder(ENC_R_PIN_B, ENC_R_PIN_A), Encoder(ENC_L_PIN_B, ENC_L_PIN_A)};
MD_REncoder ENCODER[NUM_ENCODER] = {MD_REncoder(ENC_R_PIN_B, ENC_R_PIN_A), MD_REncoder(ENC_L_PIN_B, ENC_L_PIN_A)};
EncoderDirection encoderDir[NUM_ENCODER];

long  g_LCDML_CONTROL_button_press_time[NUM_ENCODER] = {0, 0};
bool  g_LCDML_CONTROL_button_prev[NUM_ENCODER] = {HIGH, HIGH};
uint8_t g_LCDML_CONTROL_prev[NUM_ENCODER] = {0, 0};
bool menu_init = true;

#ifdef U8X8_DISPLAY
const uint8_t * flipped_scroll_bar[5];
const uint8_t * flipped_block_bar[7];
const uint8_t * flipped_meter_bar[7];

uint8_t * rotTile(const uint8_t * tile)
{
  uint8_t * newt = new uint8_t[8];
  for (int x = 0; x < 8; x++) {
    uint8_t newb = 0;
    for (int y = 0 ; y < 8; y++) {
      newb |= (tile[y] << x) & 0x80;
      newb >>= 1;
    }
    newt[x] = newb;
  }
  return newt;
}
#endif

void setup_ui(void)
{
  // LCD Begin
#ifdef I2C_DISPLAY
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.noCursor();
#else
  lcd.begin();
  lcd.clear();
  lcd.setFont(u8x8_font_amstrad_cpc_extended_f);
#endif
  lcd.setCursor(3, 0);
  lcd.print(F("MicroDexed"));
  lcd.setCursor(0, 1);
  lcd.print(F("(c)parasiTstudio"));

  lcd_special_chars(SCROLLBAR);
  // LCDMenuLib Setup
  LCDML_setup(_LCDML_DISP_cnt);
  // Enable Menu Rollover
  //LCDML.MENU_enRollover();
  // Enable Screensaver (screensaver menu function, time to activate in ms)
  //LCDML.SCREEN_enable(UI_func_voice_select, VOICE_SELECTION_MS);
}

#ifdef DEBUG
void setup_debug_message(void)
{
  // LCD Begin
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print(F("* DEBUG MODE *"));
  lcd.setCursor(1, 1);
  lcd.print(F("ENABLE CONSOLE"));
  delay(300);
  lcd.setCursor(1, 1);
  lcd.print(_LCDML_VERSION);
  lcd.print(F("  "));
}
#endif

/***********************************************************************
   MENU CONTROL
 ***********************************************************************/
uint8_t get_current_cursor_id(void)
{
  LCDMenuLib2_menu *tmp;

  if ((tmp = LCDML.MENU_getCurrentObj()) != NULL)
    return (tmp->getChild(LCDML.MENU_getCursorPosAbs())->getID());
  else
    return (0);
}

void lcdml_menu_control(void)
{
  // If something must init, put in in the setup condition
  if (LCDML.BT_setup())
  {
    pinMode(BUT_R_PIN, INPUT_PULLUP);
    pinMode(BUT_L_PIN, INPUT_PULLUP);

    ENCODER[ENC_R].begin();
    ENCODER[ENC_L].begin();
  }

  if (back_from_volume > BACK_FROM_VOLUME_MS && LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_volume))
  {
    encoderDir[ENC_L].reset();
    encoderDir[ENC_R].reset();

    if (LCDML.MENU_getLastActiveFunctionID() < 0xff)
      LCDML.OTHER_jumpToID(LCDML.MENU_getLastActiveFunctionID());
    else
      LCDML.OTHER_setCursorToID(LCDML.MENU_getLastCursorPositionID());
    //LCDML.FUNC_goBackToMenu();
  }

  // Volatile Variables
  long g_LCDML_CONTROL_Encoder_position[NUM_ENCODER] = {ENCODER[ENC_R].read(), ENCODER[ENC_L].read()};
  bool button[NUM_ENCODER] = {digitalRead(BUT_R_PIN), digitalRead(BUT_L_PIN)};

  /************************************************************************************
    Basic encoder handling (from LCDMenuLib2)
   ************************************************************************************/

  // RIGHT
  if (g_LCDML_CONTROL_Encoder_position[ENC_R] <= -3)
  {
    if (!button[ENC_R])
    {
      LCDML.BT_left();
#ifdef DEBUG
      Serial.println(F("ENC-R left"));
#endif
      encoderDir[ENC_R].Left(true);
      g_LCDML_CONTROL_button_prev[ENC_R] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_R] = -1;
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("ENC-R down"));
#endif
      encoderDir[ENC_R].Down(true);
      LCDML.BT_down();
    }
    ENCODER[ENC_R].write(g_LCDML_CONTROL_Encoder_position[ENC_R] + 4);
  }
  else if (g_LCDML_CONTROL_Encoder_position[ENC_R] >= 3)
  {
    if (!button[ENC_R])
    {
#ifdef DEBUG
      Serial.println(F("ENC-R right"));
#endif
      encoderDir[ENC_R].Right(true);
      LCDML.BT_right();
      g_LCDML_CONTROL_button_prev[ENC_R] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_R] = -1;
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("ENC-R up"));
#endif
      encoderDir[ENC_R].Up(true);
      LCDML.BT_up();
    }
    ENCODER[ENC_R].write(g_LCDML_CONTROL_Encoder_position[ENC_R] - 4);
  }
  else
  {
    if (!button[ENC_R] && g_LCDML_CONTROL_button_prev[ENC_R]) //falling edge, button pressed
    {
      encoderDir[ENC_R].ButtonPressed(true);
      g_LCDML_CONTROL_button_prev[ENC_R] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_R] = millis();
    }
    else if (button[ENC_R] && !g_LCDML_CONTROL_button_prev[ENC_R]) //rising edge, button not active
    {
      encoderDir[ENC_R].ButtonPressed(false);
      g_LCDML_CONTROL_button_prev[ENC_R] = HIGH;

      if (g_LCDML_CONTROL_button_press_time[ENC_R] < 0)
      {
        g_LCDML_CONTROL_button_press_time[ENC_R] = millis();
        //Reset for left right action
      }
      else if ((millis() - g_LCDML_CONTROL_button_press_time[ENC_R]) >= LONG_BUTTON_PRESS)
      {
#ifdef DEBUG
        Serial.println("ENC-R long released");
#endif
        //LCDML.BT_quit();
        encoderDir[ENC_R].ButtonLong(true);
      }
      else if ((millis() - g_LCDML_CONTROL_button_press_time[ENC_R]) >= BUT_DEBOUNCE_MS)
      {
#ifdef DEBUG
        Serial.println(F("ENC-R short"));
#endif
        encoderDir[ENC_R].ButtonShort(true);

        LCDML.BT_enter();
      }
    }
  }

  if (encoderDir[ENC_R].ButtonPressed() == true && (millis() - g_LCDML_CONTROL_button_press_time[ENC_R]) >= LONG_BUTTON_PRESS)
  {
#ifdef DEBUG
    Serial.println("ENC-R long recognized");
#endif
    encoderDir[ENC_R].ButtonLong(true);

    if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_voice_select))
    {
      LCDML.BT_enter();
      LCDML.OTHER_updateFunc();
      LCDML.loop_menu();
      encoderDir[ENC_R].ButtonPressed(false);
      encoderDir[ENC_R].ButtonLong(false);
    }
    else
    {
      if (LCDML.FUNC_getID() < 0xff)
        LCDML.FUNC_setGBAToLastFunc();
      else
        LCDML.FUNC_setGBAToLastCursorPos();

      LCDML.OTHER_jumpToFunc(UI_func_voice_select);
      encoderDir[ENC_R].reset();
    }
  }

  // LEFT
  if (g_LCDML_CONTROL_Encoder_position[ENC_L] <= -3)
  {
    if (!button[ENC_L])
    {
#ifdef DEBUG
      Serial.println(F("ENC-L left"));
#endif
      encoderDir[ENC_L].Left(true);
      LCDML.BT_left();
      g_LCDML_CONTROL_button_prev[ENC_L] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_L] = -1;
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("ENC-L down"));
#endif
      encoderDir[ENC_L].Down(true);
      LCDML.BT_down();
      if (LCDML.FUNC_getID() != LCDML.OTHER_getIDFromFunction(UI_func_volume))
      {
        LCDML.OTHER_jumpToFunc(UI_func_volume);
      }
    }
    ENCODER[ENC_L].write(g_LCDML_CONTROL_Encoder_position[ENC_L] + 4);
  }
  else if (g_LCDML_CONTROL_Encoder_position[ENC_L] >= 3)
  {
    if (!button[ENC_L])
    {
#ifdef DEBUG
      Serial.println(F("ENC-L right"));
#endif
      encoderDir[ENC_L].Right(true);
      LCDML.BT_right();
      g_LCDML_CONTROL_button_prev[ENC_L] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_L] = -1;
    }
    else
    {
#ifdef DEBUG
      Serial.println(F("ENC-L up"));
#endif
      encoderDir[ENC_L].Up(true);
      LCDML.BT_up();
      if (LCDML.FUNC_getID() != LCDML.OTHER_getIDFromFunction(UI_func_volume))
      {
        LCDML.OTHER_jumpToFunc(UI_func_volume);
      }
    }
    ENCODER[ENC_L].write(g_LCDML_CONTROL_Encoder_position[ENC_L] - 4);
  }
  else
  {
    if (!button[ENC_L] && g_LCDML_CONTROL_button_prev[ENC_L]) //falling edge, button pressed
    {
      encoderDir[ENC_L].ButtonPressed(true);
      g_LCDML_CONTROL_button_prev[ENC_L] = LOW;
      g_LCDML_CONTROL_button_press_time[ENC_L] = millis();
    }
    else if (button[ENC_L] && !g_LCDML_CONTROL_button_prev[ENC_L]) //rising edge, button not active
    {
      encoderDir[ENC_L].ButtonPressed(false);
      g_LCDML_CONTROL_button_prev[ENC_L] = HIGH;

      if (g_LCDML_CONTROL_button_press_time[ENC_L] < 0)
      {
        g_LCDML_CONTROL_button_press_time[ENC_L] = millis();
        //Reset for left right action
      }
      else if ((millis() - g_LCDML_CONTROL_button_press_time[ENC_L]) >= LONG_BUTTON_PRESS)
      {
#ifdef DEBUG
        Serial.println(F("ENC-L long released"));
#endif
        //encoderDir[ENC_L].ButtonLong(true);
        //LCDML.BT_quit();
      }
      else if ((millis() - g_LCDML_CONTROL_button_press_time[ENC_L]) >= BUT_DEBOUNCE_MS)
      {
        //LCDML.BT_enter();
#ifdef DEBUG
        Serial.println(F("ENC-L short"));
#endif
        encoderDir[ENC_L].ButtonShort(true);

        if ((LCDML.MENU_getLastActiveFunctionID() == 0xff && LCDML.MENU_getLastCursorPositionID() == 0) || menu_init == true)
        {
          LCDML.MENU_goRoot();
          menu_init = false;
        }
        else if (LCDML.FUNC_getID() == LCDML.OTHER_getIDFromFunction(UI_func_volume))
        {
          encoderDir[ENC_L].reset();
          encoderDir[ENC_R].reset();

          if (LCDML.MENU_getLastActiveFunctionID() < 0xff)
            LCDML.OTHER_jumpToID(LCDML.MENU_getLastActiveFunctionID());
          else
            LCDML.OTHER_setCursorToID(LCDML.MENU_getLastCursorPositionID());
        }
        else
          LCDML.BT_quit();
      }
    }
  }

  if (encoderDir[ENC_L].ButtonPressed() == true && (millis() - g_LCDML_CONTROL_button_press_time[ENC_L]) >= LONG_BUTTON_PRESS)
  {
#ifdef DEBUG
    Serial.println(F("ENC-L long recognized"));
#endif
    for (uint8_t i = 0; i < NUM_DEXED; i++)
      MicroDexed[i]->panic();

    encoderDir[ENC_L].reset();
    encoderDir[ENC_R].reset();
  }
}

/***********************************************************************
   MENU DISPLAY
 ***********************************************************************/
void lcdml_menu_clear(void)
{
  lcd.clear();
  lcd.setCursor(0, 0);
}

void lcdml_menu_display(void)
{
  // update content
  // ***************
  if (LCDML.DISP_checkMenuUpdate()) {
    // clear menu
    // ***************
    LCDML.DISP_clear();

    // declaration of some variables
    // ***************
    // content variable
    char content_text[_LCDML_DISP_cols];  // save the content text of every menu element
    // menu element object
    LCDMenuLib2_menu *tmp;
    // some limit values
    uint8_t i = LCDML.MENU_getScroll();
    uint8_t maxi = _LCDML_DISP_rows + i;
    uint8_t n = 0;

    // check if this element has children
    if ((tmp = LCDML.MENU_getDisplayedObj()) != NULL)
    {
      // loop to display lines
      do
      {
        // check if a menu element has a condition and if the condition be true
        if (tmp->checkCondition())
        {
          // check the type off a menu element
          if (tmp->checkType_menu() == true)
          {
            // display normal content
            LCDML_getContent(content_text, tmp->getID());
            lcd.setCursor(1, n);
            lcd.print(content_text);
          }
          else
          {
            if (tmp->checkType_dynParam()) {
              tmp->callback(n);
            }
          }
          // increment some values
          i++;
          n++;
        }
        // try to go to the next sibling and check the number of displayed rows
      } while (((tmp = tmp->getSibling(1)) != NULL) && (i < maxi));
    }
  }

  if (LCDML.DISP_checkMenuCursorUpdate())
  {
    // init vars
    uint8_t n_max             = (LCDML.MENU_getChilds() >= _LCDML_DISP_rows) ? _LCDML_DISP_rows : (LCDML.MENU_getChilds());
    uint8_t scrollbar_min     = 0;
    uint8_t scrollbar_max     = LCDML.MENU_getChilds();
    uint8_t scrollbar_cur_pos = LCDML.MENU_getCursorPosAbs();
    uint8_t scroll_pos        = ((1.*n_max * _LCDML_DISP_rows) / (scrollbar_max - 1) * scrollbar_cur_pos);

    // display rows
    for (uint8_t n = 0; n < n_max; n++)
    {
      //set cursor
      lcd.setCursor(0, n);

      //set cursor char
      if (n == LCDML.MENU_getCursorPos()) {
        lcd.write(_LCDML_DISP_cfg_cursor);
      } else {
        lcd.write(' ');
      }

      // delete or reset scrollbar
      if (_LCDML_DISP_cfg_scrollbar == 1) {
        if (scrollbar_max > n_max) {
#ifdef I2C_DISPLAY
          lcd.setCursor((_LCDML_DISP_cols - 1), n);
          lcd.write((uint8_t)0);
#else
          lcd.drawTile((_LCDML_DISP_cols - 1), n, 1, flipped_scroll_bar[0]);
          lcd.setCursor((_LCDML_DISP_cols), n + 1);
#endif
        }
        else {
          lcd.setCursor((_LCDML_DISP_cols - 1), n);
          lcd.print(F(" "));
        }
      }
    }

    // display scrollbar
    if (_LCDML_DISP_cfg_scrollbar == 1) {
      if (scrollbar_max > n_max) {
        //set scroll position
        if (scrollbar_cur_pos == scrollbar_min) {
          // min pos
#ifdef I2C_DISPLAY
          lcd.setCursor((_LCDML_DISP_cols - 1), 0);
          lcd.write((uint8_t)1);
#else
          lcd.drawTile((_LCDML_DISP_cols - 1), 0, 1, flipped_scroll_bar[1]);
          lcd.setCursor((_LCDML_DISP_cols), 1);
#endif
        } else if (scrollbar_cur_pos == (scrollbar_max - 1)) {
          // max pos
#ifdef I2C_DISPLAY
          lcd.setCursor((_LCDML_DISP_cols - 1), (n_max - 1));
          lcd.write((uint8_t)4);
#else
          lcd.drawTile((_LCDML_DISP_cols - 1), (n_max - 1), 1, flipped_scroll_bar[4]);
          lcd.setCursor((_LCDML_DISP_cols), (n_max));
#endif
        } else {
          // between
#ifdef I2C_DISPLAY
          lcd.setCursor((_LCDML_DISP_cols - 1), scroll_pos / n_max);
          lcd.write((uint8_t)(scroll_pos % n_max) + 1);
#else
          lcd.drawTile((_LCDML_DISP_cols - 1), scroll_pos / n_max, 1, flipped_scroll_bar[(scroll_pos % n_max) + 1]);
          lcd.setCursor((_LCDML_DISP_cols), (scroll_pos / n_max) + 1);
#endif
        }
      }
    }
  }
}

//####################################################################################################################################################################################################

/***********************************************************************
   MENU
 ***********************************************************************/

#ifdef USE_FX
void UI_func_reverb_roomsize(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Reverb Room", configuration.fx.reverb_roomsize, 1.0, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 3, false, false, true);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
        configuration.fx.reverb_roomsize = constrain(configuration.fx.reverb_roomsize + ENCODER[ENC_R].speed(), REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.fx.reverb_roomsize = constrain(configuration.fx.reverb_roomsize - ENCODER[ENC_R].speed(), REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX);
    }
    lcd_display_bar_int("Reverb Room", configuration.fx.reverb_roomsize, 1.0, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 3, false, false, false);

#ifdef USE_PLATEREVERB
    reverb.size(mapfloat(configuration.fx.reverb_roomsize, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 0.0, 1.0));
#else
    freeverb_r.roomsize(mapfloat(configuration.fx.reverb_roomsize, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 0.0, 1.0));
    freeverb_l.roomsize(mapfloat(configuration.fx.reverb_roomsize, REVERB_ROOMSIZE_MIN, REVERB_ROOMSIZE_MAX, 0.0, 1.0));
#endif
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.reverb_roomsize), configuration.fx.reverb_roomsize);
  }
}

void UI_func_reverb_damping(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Reverb Damp.", configuration.fx.reverb_damping, 1.0, REVERB_DAMPING_MIN, REVERB_DAMPING_MAX, 3, false, false, true);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
        configuration.fx.reverb_damping = constrain(configuration.fx.reverb_damping + ENCODER[ENC_R].speed(), REVERB_DAMPING_MIN, REVERB_DAMPING_MAX);
      else if (LCDML.BT_checkUp())
        configuration.fx.reverb_damping = constrain(configuration.fx.reverb_damping - ENCODER[ENC_R].speed(), REVERB_DAMPING_MIN, REVERB_DAMPING_MAX);
    }

    lcd_display_bar_int("Reverb Damp.", configuration.fx.reverb_damping, 1.0, REVERB_DAMPING_MIN, REVERB_DAMPING_MAX, 3, false, false, false);

#ifdef USE_PLATEREVERB
    reverb.hidamp(mapfloat(configuration.fx.reverb_damping, REVERB_DAMPING_MIN, REVERB_DAMPING_MAX, 0.0, 1.0));
#else
    freeverb_l.damping(mapfloat(configuration.fx.reverb_damping, REVERB_DAMPING_MIN, REVERB_DAMPING_MAX, 0.0, 1.0));
    freeverb_r.damping(mapfloat(configuration.fx.reverb_damping, REVERB_DAMPING_MIN, REVERB_DAMPING_MAX, 0.0, 1.0));
#endif
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.reverb_damping), configuration.fx.reverb_damping);
  }
}

void UI_func_reverb_level(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Reverb Level", configuration.fx.reverb_level, 1.0, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 3, false, false, true);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
        configuration.fx.reverb_level = constrain(configuration.fx.reverb_level + ENCODER[ENC_R].speed(), REVERB_LEVEL_MIN, REVERB_LEVEL_MAX);
      else if (LCDML.BT_checkUp())
        configuration.fx.reverb_level = constrain(configuration.fx.reverb_level - ENCODER[ENC_R].speed(), REVERB_LEVEL_MIN, REVERB_LEVEL_MAX);
    }

    lcd_display_bar_int("Reverb Level", configuration.fx.reverb_level, 1.0, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 3, false, false, true);

    master_mixer_r.gain(3, pseudo_log_curve(mapfloat(configuration.fx.reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 0.0, 1.0)));
    master_mixer_l.gain(3, pseudo_log_curve(mapfloat(configuration.fx.reverb_level, REVERB_LEVEL_MIN, REVERB_LEVEL_MAX, 0.0, 1.0)));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.reverb_level), configuration.fx.reverb_level);
  }
}

void UI_func_chorus_frequency(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_float("Chorus Frq.", configuration.fx.chorus_frequency[selected_instance_id], 0.1, CHORUS_FREQUENCY_MIN, CHORUS_FREQUENCY_MAX, 2, 1, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.fx.chorus_frequency[selected_instance_id] = constrain(configuration.fx.chorus_frequency[selected_instance_id] + ENCODER[ENC_R].speed(), CHORUS_FREQUENCY_MIN, CHORUS_FREQUENCY_MAX);
      else if (LCDML.BT_checkUp())
        configuration.fx.chorus_frequency[selected_instance_id] = constrain(configuration.fx.chorus_frequency[selected_instance_id] - ENCODER[ENC_R].speed(), CHORUS_FREQUENCY_MIN, CHORUS_FREQUENCY_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }
    lcd_display_bar_float("Chorus Frq.", configuration.fx.chorus_frequency[selected_instance_id], 0.1, CHORUS_FREQUENCY_MIN, CHORUS_FREQUENCY_MAX, 2, 1, false, false, false);

    chorus_modulator[selected_instance_id]->frequency(configuration.fx.chorus_frequency[selected_instance_id] / 10.0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_frequency[0]), configuration.fx.chorus_frequency[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_frequency[1]), configuration.fx.chorus_frequency[1]);
#endif
  }
}

void UI_func_chorus_waveform(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Chorus Wavefrm"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if (LCDML.BT_checkDown() && encoderDir[ENC_R].Down())
      configuration.fx.chorus_waveform[selected_instance_id] = constrain(configuration.fx.chorus_waveform[selected_instance_id] + 1, CHORUS_WAVEFORM_MIN, CHORUS_WAVEFORM_MAX);
    else if (LCDML.BT_checkUp() && encoderDir[ENC_R].Up())
      configuration.fx.chorus_waveform[selected_instance_id] = constrain(configuration.fx.chorus_waveform[selected_instance_id] - 1, CHORUS_WAVEFORM_MIN, CHORUS_WAVEFORM_MAX);
#if NUM_DEXED > 1
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      selected_instance_id = !selected_instance_id;
      lcd_active_instance_number(selected_instance_id);
      lcd.setCursor(14, 0);
      lcd.write(0);
      lcd.setCursor(15, 0);
      lcd.write(1);
    }
#endif

    lcd.setCursor(0, 1);
    switch (configuration.fx.chorus_waveform[selected_instance_id])
    {
      case 0:
        chorus_modulator[selected_instance_id]->begin(WAVEFORM_TRIANGLE);
        lcd.print(F("[TRIANGLE]"));
        break;
      case 1:
        chorus_modulator[selected_instance_id]->begin(WAVEFORM_SINE);
        lcd.print(F("[SINE    ]"));
        break;
      default:
        chorus_modulator[selected_instance_id]->begin(WAVEFORM_TRIANGLE);
        lcd.print(F("[TRIANGLE]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_waveform[0]), configuration.fx.chorus_waveform[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_waveform[1]), configuration.fx.chorus_waveform[1]);
#endif
  }
}

void UI_func_chorus_depth(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Chorus Dpt.", configuration.fx.chorus_depth[selected_instance_id], 1.0, CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.fx.chorus_depth[selected_instance_id] = constrain(configuration.fx.chorus_depth[selected_instance_id] + ENCODER[ENC_R].speed(), CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX);
      else if (LCDML.BT_checkUp())
        configuration.fx.chorus_depth[selected_instance_id] = constrain(configuration.fx.chorus_depth[selected_instance_id] - ENCODER[ENC_R].speed(), CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Chorus Dpt.", configuration.fx.chorus_depth[selected_instance_id], 1.0, CHORUS_DEPTH_MIN, CHORUS_DEPTH_MAX, 3, false, false, false);

    chorus_modulator[selected_instance_id]->amplitude(configuration.fx.chorus_depth[selected_instance_id] / 100.0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_depth[0]), configuration.fx.chorus_depth[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_depth[1]), configuration.fx.chorus_depth[1]);
#endif
  }
}

void UI_func_chorus_level(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Chorus Lvl.", configuration.fx.chorus_level[selected_instance_id], 1.0, CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.chorus_level[selected_instance_id] = constrain(configuration.fx.chorus_level[selected_instance_id] + ENCODER[ENC_R].speed(), CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 93, configuration.fx.chorus_level[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.chorus_level[selected_instance_id] = constrain(configuration.fx.chorus_level[selected_instance_id] - ENCODER[ENC_R].speed(), CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 93, configuration.fx.chorus_level[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Chorus Lvl.", configuration.fx.chorus_level[selected_instance_id], 1.0, CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 3, false, false, false);

    //chorus_mixer[selected_instance_id]->gain(0, pseudo_log_curve(1.0 - mapfloat(configuration.fx.chorus_level[selected_instance_id], CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 0.0, 0.5)));
    //chorus_mixer[selected_instance_id]->gain(1, pseudo_log_curve(mapfloat(configuration.fx.chorus_level[selected_instance_id], CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 0.0, 0.5)));
    //chorus_mixer[selected_instance_id]->gain(0, 1.0 - mapfloat(configuration.fx.chorus_level[selected_instance_id], CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 0.0, 0.5));
    chorus_mixer[selected_instance_id]->gain(1, mapfloat(configuration.fx.chorus_level[selected_instance_id], CHORUS_LEVEL_MIN, CHORUS_LEVEL_MAX, 0.0, 0.5));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_level[0]), configuration.fx.chorus_level[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.chorus_level[1]), configuration.fx.chorus_level[1]);
#endif
  }
}

void UI_func_delay_time(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
#if DELAY_TIME_MAX >= 100
    lcd_display_bar_int("Delay Time", configuration.fx.delay_time[selected_instance_id], 10.0, DELAY_TIME_MIN, DELAY_TIME_MAX, 4, false, false, true);
#else
    lcd_display_bar_int("Delay Time", configuration.fx.delay_time[selected_instance_id], 10.0, DELAY_TIME_MIN, DELAY_TIME_MAX, 3, false, false, true);
#endif
    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.delay_time[selected_instance_id] = constrain(configuration.fx.delay_time[selected_instance_id] + ENCODER[ENC_R].speed(), DELAY_TIME_MIN, DELAY_TIME_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 105, configuration.fx.delay_time[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.delay_time[selected_instance_id] = constrain(configuration.fx.delay_time[selected_instance_id] - ENCODER[ENC_R].speed(), DELAY_TIME_MIN, DELAY_TIME_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 105, configuration.fx.delay_time[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

#if DELAY_TIME_MAX >= 100
    lcd_display_bar_int("Delay Time", configuration.fx.delay_time[selected_instance_id], 10.0, DELAY_TIME_MIN, DELAY_TIME_MAX, 4, false, false, true);
#else
    lcd_display_bar_int("Delay Time", configuration.fx.delay_time[selected_instance_id], 10.0, DELAY_TIME_MIN, DELAY_TIME_MAX, 3, false, false, true);
#endif
    if (configuration.fx.delay_time[selected_instance_id] <= DELAY_TIME_MIN)
      delay_fx[selected_instance_id]->disable(0);
    else
      delay_fx[selected_instance_id]->delay(0, constrain(configuration.fx.delay_time[selected_instance_id], DELAY_TIME_MIN, DELAY_TIME_MAX) * 10);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_time[0]), configuration.fx.delay_time[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_time[1]), configuration.fx.delay_time[1]);
#endif
  }
}

void UI_func_delay_feedback(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Delay Feedb.", configuration.fx.delay_feedback[selected_instance_id], 1.0, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.delay_feedback[selected_instance_id] = constrain(configuration.fx.delay_feedback[selected_instance_id] + ENCODER[ENC_R].speed(), DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 106, configuration.fx.delay_feedback[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.delay_feedback[selected_instance_id] = constrain(configuration.fx.delay_feedback[selected_instance_id] - ENCODER[ENC_R].speed(), DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 106, configuration.fx.delay_feedback[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Delay Feedb.", configuration.fx.delay_feedback[selected_instance_id], 1.0, DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, 3, false, false, false);

    delay_fb_mixer[selected_instance_id]->gain(1, pseudo_log_curve(mapfloat(configuration.fx.delay_feedback[selected_instance_id], DELAY_FEEDBACK_MIN, DELAY_FEEDBACK_MAX, 0.0, 1.0))); // amount of feedback
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_feedback[0]), configuration.fx.delay_feedback[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_feedback[1]), configuration.fx.delay_feedback[1]);
#endif
  }
}

void UI_func_delay_level(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Delay Lvl.", configuration.fx.delay_level[selected_instance_id], 1.0, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.delay_level[selected_instance_id] = constrain(configuration.fx.delay_level[selected_instance_id] + ENCODER[ENC_R].speed(), DELAY_LEVEL_MIN, DELAY_LEVEL_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 107, configuration.fx.delay_level[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.delay_level[selected_instance_id] = constrain(configuration.fx.delay_level[selected_instance_id] - ENCODER[ENC_R].speed(), DELAY_LEVEL_MIN, DELAY_LEVEL_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 107, configuration.fx.delay_level[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Delay Lvl.", configuration.fx.delay_level[selected_instance_id], 1.0, DELAY_LEVEL_MIN, DELAY_LEVEL_MAX, 3, false, false, false);

    /*if (configuration.fx.delay_level[selected_instance_id] <= DELAY_LEVEL_MIN)
      delay_fx[selected_instance_id]->disable(0);
      else
      delay_fx[selected_instance_id]->delay(0, constrain(configuration.fx.delay_time[selected_instance_id], DELAY_TIME_MIN, DELAY_TIME_MAX) * 10); */
    delay_mixer[selected_instance_id]->gain(1, pseudo_log_curve(mapfloat(configuration.fx.delay_level[selected_instance_id], DELAY_LEVEL_MIN, DELAY_LEVEL_MAX, 0.0, 1.0)));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_level[0]), configuration.fx.delay_level[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.delay_level[1]), configuration.fx.delay_level[1]);
#endif
  }
}

void UI_func_reverb_send(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Reverb Send", configuration.fx.reverb_send[selected_instance_id], 1.0, REVERB_SEND_MIN, REVERB_SEND_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.reverb_send[selected_instance_id] = constrain(configuration.fx.reverb_send[selected_instance_id] + ENCODER[ENC_R].speed(), REVERB_SEND_MIN, REVERB_SEND_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 91, configuration.fx.reverb_send[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.reverb_send[selected_instance_id] = constrain(configuration.fx.reverb_send[selected_instance_id] - ENCODER[ENC_R].speed(), REVERB_SEND_MIN, REVERB_SEND_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 91, configuration.fx.reverb_send[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Reverb Send", configuration.fx.reverb_send[selected_instance_id], 1.0, REVERB_SEND_MIN, REVERB_SEND_MAX, 3, false, false, false);

    reverb_mixer_r.gain(selected_instance_id, pseudo_log_curve(mapfloat(configuration.fx.reverb_send[selected_instance_id], REVERB_SEND_MIN, REVERB_SEND_MAX, 0.0, 1.0)));
    reverb_mixer_l.gain(selected_instance_id, pseudo_log_curve(mapfloat(configuration.fx.reverb_send[selected_instance_id], REVERB_SEND_MIN, REVERB_SEND_MAX, 0.0, 1.0)));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.reverb_send[0]), configuration.fx.reverb_send[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.reverb_send[1]), configuration.fx.reverb_send[1]);
#endif
  }
}

void UI_func_filter_cutoff(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Filter Cut", configuration.fx.filter_cutoff[selected_instance_id], 1.0, FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.filter_cutoff[selected_instance_id] = constrain(configuration.fx.filter_cutoff[selected_instance_id] + ENCODER[ENC_R].speed(), FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 104, configuration.fx.filter_cutoff[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.filter_cutoff[selected_instance_id] = constrain(configuration.fx.filter_cutoff[selected_instance_id] - ENCODER[ENC_R].speed(), FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 104, configuration.fx.filter_cutoff[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Filter Cut", configuration.fx.filter_cutoff[selected_instance_id], 1.0, FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX, 3, false, false, false);

    MicroDexed[selected_instance_id]->fx.Cutoff = mapfloat(configuration.fx.filter_cutoff[selected_instance_id], FILTER_CUTOFF_MIN, FILTER_CUTOFF_MAX, 1.0, 0.0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.filter_cutoff[0]), configuration.fx.filter_cutoff[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.filter_cutoff[1]), configuration.fx.filter_cutoff[1]);
#endif
  }
}

void UI_func_filter_resonance(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Filter Res", configuration.fx.filter_resonance[selected_instance_id], 1.0, FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.filter_resonance[selected_instance_id] = constrain(configuration.fx.filter_resonance[selected_instance_id] + ENCODER[ENC_R].speed(), FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 103, configuration.fx.filter_resonance[selected_instance_id]);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.filter_resonance[selected_instance_id] = constrain(configuration.fx.filter_resonance[selected_instance_id] - ENCODER[ENC_R].speed(), FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 103, configuration.fx.filter_resonance[selected_instance_id]);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Filter Res", configuration.fx.filter_resonance[selected_instance_id], 1.0, FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX, 3, false, false, false);

    MicroDexed[selected_instance_id]->fx.Reso = mapfloat(configuration.fx.filter_resonance[selected_instance_id], FILTER_RESONANCE_MIN, FILTER_RESONANCE_MAX, 1.0, 0.0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.filter_resonance[0]), configuration.fx.filter_resonance[0]);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.filter_resonance[1]), configuration.fx.filter_resonance[1]);
#endif
  }
}
#endif

void UI_func_transpose(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(METERBAR);
    lcd_display_meter_int("Transpose", configuration.dexed[selected_instance_id].transpose, 1.0, -24.0, TRANSPOSE_MIN, TRANSPOSE_MAX, 2, false, true, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].transpose = constrain(configuration.dexed[selected_instance_id].transpose + ENCODER[ENC_R].speed(), TRANSPOSE_MIN, TRANSPOSE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].transpose = constrain(configuration.dexed[selected_instance_id].transpose - ENCODER[ENC_R].speed(), TRANSPOSE_MIN, TRANSPOSE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_meter_int("Transpose", configuration.dexed[selected_instance_id].transpose, 1.0, -24.0, TRANSPOSE_MIN, TRANSPOSE_MAX, 2, false, true, true);

    MicroDexed[selected_instance_id]->data[DEXED_VOICE_OFFSET + DEXED_TRANSPOSE] = configuration.dexed[selected_instance_id].transpose;
    MicroDexed[selected_instance_id]->notesOff();
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 144, configuration.dexed[selected_instance_id].transpose, 0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].transpose), configuration.dexed[0].transpose);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].transpose), configuration.dexed[1].transpose);
#endif
  }
}

void UI_func_tune(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(METERBAR);
    lcd_display_meter_int("Fine Tune", configuration.dexed[selected_instance_id].tune, 1.0, -100.0, TUNE_MIN, TUNE_MAX, 3, false, true, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.dexed[selected_instance_id].tune = constrain(configuration.dexed[selected_instance_id].tune + ENCODER[ENC_R].speed(), TUNE_MIN, TUNE_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 94, configuration.dexed[selected_instance_id].tune);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.dexed[selected_instance_id].tune = constrain(configuration.dexed[selected_instance_id].tune - ENCODER[ENC_R].speed(), TUNE_MIN, TUNE_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 94, configuration.dexed[selected_instance_id].tune);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_meter_int("Fine Tune", configuration.dexed[selected_instance_id].tune, 1.0, -100.0, TUNE_MIN, TUNE_MAX, 3, false, true, false);

    MicroDexed[selected_instance_id]->controllers.masterTune = (int((configuration.dexed[selected_instance_id].tune - 100) / 100.0 * 0x4000) << 11) * (1.0 / 12);
    MicroDexed[selected_instance_id]->doRefreshVoice();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].tune), configuration.dexed[0].tune);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].tune), configuration.dexed[1].tune);
#endif
  }
}

void UI_func_midi_channel(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("MIDI Channel"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if (LCDML.BT_checkDown() && encoderDir[ENC_R].Down())
      configuration.dexed[selected_instance_id].midi_channel = constrain(configuration.dexed[selected_instance_id].midi_channel + ENCODER[ENC_R].speed(), MIDI_CHANNEL_MIN, MIDI_CHANNEL_MAX);
    else if (LCDML.BT_checkUp() && encoderDir[ENC_R].Up())
      configuration.dexed[selected_instance_id].midi_channel = constrain(configuration.dexed[selected_instance_id].midi_channel - ENCODER[ENC_R].speed(), MIDI_CHANNEL_MIN, MIDI_CHANNEL_MAX);
#if NUM_DEXED > 1
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      selected_instance_id = !selected_instance_id;
      lcd_active_instance_number(selected_instance_id);
      lcd.setCursor(14, 0);
      lcd.write(0);
      lcd.setCursor(15, 0);
      lcd.write(1);
    }
#endif

    lcd.setCursor(0, 1);
    if (configuration.dexed[selected_instance_id].midi_channel == 0)
    {
      lcd.print(F("[OMNI]"));
    }
    else
    {
      lcd_display_int(configuration.dexed[selected_instance_id].midi_channel, 4, false, true, false);
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].midi_channel), configuration.dexed[0].midi_channel);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].midi_channel), configuration.dexed[1].midi_channel);
#endif
  }
}

void getNoteName(char* noteName, uint8_t noteNumber)
{
  char notes [12][3] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};
  uint8_t oct_index = noteNumber - 12;

  noteNumber -= 21;
  sprintf(noteName, "%2s%1d", notes[noteNumber % 12], oct_index / 12);
}

void UI_func_lowest_note(uint8_t param)
{
  char note_name[4];

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    getNoteName(note_name, configuration.dexed[selected_instance_id].lowest_note);
    lcd.setCursor(0, 0);
    lcd.print(F("Lowest Note"));
    lcd.setCursor(0, 1);
    lcd.print(F("["));
    lcd.print(note_name);
    lcd.print(F("]"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].lowest_note = constrain(configuration.dexed[selected_instance_id].lowest_note + ENCODER[ENC_R].speed(), INSTANCE_LOWEST_NOTE_MIN, INSTANCE_LOWEST_NOTE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].lowest_note = constrain(configuration.dexed[selected_instance_id].lowest_note - ENCODER[ENC_R].speed(), INSTANCE_LOWEST_NOTE_MIN, INSTANCE_LOWEST_NOTE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    getNoteName(note_name, configuration.dexed[selected_instance_id].lowest_note);
    lcd.setCursor(1, 1);
    lcd.print(note_name);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].lowest_note), configuration.dexed[0].lowest_note);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].lowest_note), configuration.dexed[1].lowest_note);
#endif
  }
}

void UI_func_highest_note(uint8_t param)
{
  char note_name[4];

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    getNoteName(note_name, configuration.dexed[selected_instance_id].highest_note);
    lcd.setCursor(0, 0);
    lcd.print(F("Highest Note"));
    lcd.setCursor(0, 1);
    lcd.print(F("["));
    lcd.print(note_name);
    lcd.print(F("]"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].highest_note = constrain(configuration.dexed[selected_instance_id].highest_note + ENCODER[ENC_R].speed(), INSTANCE_HIGHEST_NOTE_MIN, INSTANCE_HIGHEST_NOTE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].highest_note = constrain(configuration.dexed[selected_instance_id].highest_note - ENCODER[ENC_R].speed(), INSTANCE_HIGHEST_NOTE_MIN, INSTANCE_HIGHEST_NOTE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    getNoteName(note_name, configuration.dexed[selected_instance_id].highest_note);
    lcd.setCursor(1, 1);
    lcd.print(note_name);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].highest_note), configuration.dexed[0].highest_note);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].highest_note), configuration.dexed[1].highest_note);
#endif
  }
}

void UI_func_sound_intensity(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Voice Level", configuration.dexed[selected_instance_id].sound_intensity, 1.0, SOUND_INTENSITY_MIN, SOUND_INTENSITY_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      encoderDir[ENC_R].reset();

      if (LCDML.BT_checkDown())
      {
        configuration.dexed[selected_instance_id].sound_intensity = constrain(configuration.dexed[selected_instance_id].sound_intensity + ENCODER[ENC_R].speed(), SOUND_INTENSITY_MIN, SOUND_INTENSITY_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 7, configuration.dexed[selected_instance_id].sound_intensity);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.dexed[selected_instance_id].sound_intensity = constrain(configuration.dexed[selected_instance_id].sound_intensity - ENCODER[ENC_R].speed(), SOUND_INTENSITY_MIN, SOUND_INTENSITY_MAX);
        MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 7, configuration.dexed[selected_instance_id].sound_intensity);
      }

#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Voice Level", configuration.dexed[selected_instance_id].sound_intensity, 1.0, SOUND_INTENSITY_MIN, SOUND_INTENSITY_MAX, 3, false, false, false);
    MicroDexed[selected_instance_id]->fx.Gain = pseudo_log_curve(mapfloat(configuration.dexed[selected_instance_id].sound_intensity, SOUND_INTENSITY_MIN, SOUND_INTENSITY_MAX, 0.0, SOUND_INTENSITY_AMP_MAX));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].sound_intensity), configuration.dexed[0].sound_intensity);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].sound_intensity), configuration.dexed[1].sound_intensity);
#endif
  }
}

void UI_func_panorama(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    if (configuration.sys.mono > 0)
    {
      lcd.setCursor(0, 0);
      lcd.print(F("Panorama"));
      lcd.setCursor(0, 1);
      lcd.print(F("MONO-disabled"));
      return;
    }
    lcd_special_chars(METERBAR);
    lcd_display_meter_float("Panorama", configuration.dexed[selected_instance_id].pan, 0.05, -20.0, PANORAMA_MIN, PANORAMA_MAX, 1, 1, false, true, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if (LCDML.BT_checkDown() && encoderDir[ENC_R].Down() && configuration.sys.mono == 0)
    {
      configuration.dexed[selected_instance_id].pan = constrain(configuration.dexed[selected_instance_id].pan + ENCODER[ENC_R].speed(), PANORAMA_MIN, PANORAMA_MAX);
      MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 10, map(configuration.dexed[selected_instance_id].pan, PANORAMA_MIN, PANORAMA_MAX, 0, 127));
    }
    else if (LCDML.BT_checkUp() && encoderDir[ENC_R].Up() && configuration.sys.mono == 0)
    {
      configuration.dexed[selected_instance_id].pan = constrain(configuration.dexed[selected_instance_id].pan - ENCODER[ENC_R].speed(), PANORAMA_MIN, PANORAMA_MAX);
      MD_sendControlChange(configuration.dexed[selected_instance_id].midi_channel, 10, map(configuration.dexed[selected_instance_id].pan, PANORAMA_MIN, PANORAMA_MAX, 0, 127));
    }
#if NUM_DEXED > 1
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      selected_instance_id = !selected_instance_id;
      lcd_active_instance_number(selected_instance_id);
      lcd.setCursor(14, 0);
      lcd.write(0);
      lcd.setCursor(15, 0);
      lcd.write(1);
    }
#endif

    if (configuration.sys.mono == 0)
    {
      lcd_display_meter_float("Panorama", configuration.dexed[selected_instance_id].pan, 0.05, -20.0, PANORAMA_MIN, PANORAMA_MAX, 1, 1, false, true, false);
      mono2stereo[selected_instance_id]->panorama(mapfloat(configuration.dexed[selected_instance_id].pan, PANORAMA_MIN, PANORAMA_MAX, -1.0, 1.0));
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].pan), configuration.dexed[0].pan);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].pan), configuration.dexed[1].pan);
#endif
  }
}

void UI_func_stereo_mono(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Stereo/Mono"));
    lcd.setCursor(0, 1);
    switch (configuration.sys.mono)
    {
      case 0:
        lcd.print(F("[STEREO]"));
        stereo2mono.stereo(true);
        break;
      case 1:
        lcd.print(F("[MONO  ]"));
        stereo2mono.stereo(false);
        break;
      case 2:
        lcd.print(F("[MONO-R]"));
        stereo2mono.stereo(false);
        break;
      case 3:
        lcd.print(F("[MONO-L]"));
        stereo2mono.stereo(false);
        break;
    }
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if (LCDML.BT_checkDown())
      configuration.sys.mono = constrain(configuration.sys.mono + 1, MONO_MIN, MONO_MAX);
    else if (LCDML.BT_checkUp())
      configuration.sys.mono = constrain(configuration.sys.mono - 1, MONO_MIN, MONO_MAX);

    lcd.setCursor(0, 1);
    switch (configuration.sys.mono)
    {
      case 0:
        lcd.print(F("[STEREO]"));
        stereo2mono.stereo(true);
        break;
      case 1:
        lcd.print(F("[MONO  ]"));
        stereo2mono.stereo(false);
        break;
      case 2:
        lcd.print(F("[MONO-R]"));
        stereo2mono.stereo(false);
        break;
      case 3:
        lcd.print(F("[MONO-L]"));
        stereo2mono.stereo(false);
        break;
    }
    set_volume(configuration.sys.vol, configuration.sys.mono);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, sys.mono), configuration.sys.mono);
  }
}

void UI_func_polyphony(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Polyphony", configuration.dexed[selected_instance_id].polyphony, 1.0, POLYPHONY_MIN, POLYPHONY_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.dexed[selected_instance_id].polyphony = constrain(configuration.dexed[selected_instance_id].polyphony + 1, POLYPHONY_MIN, POLYPHONY_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        if (configuration.dexed[selected_instance_id].polyphony - 1 < 0)
          configuration.dexed[selected_instance_id].polyphony = 0;
        else
          configuration.dexed[selected_instance_id].polyphony = constrain(configuration.dexed[selected_instance_id].polyphony - 1, POLYPHONY_MIN, POLYPHONY_MAX);
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
      lcd_active_instance_number(selected_instance_id);
#endif
    }

    lcd_display_bar_int("Polyphony", configuration.dexed[selected_instance_id].polyphony, 1.0, POLYPHONY_MIN, POLYPHONY_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setMaxNotes(configuration.dexed[selected_instance_id].polyphony);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].polyphony), configuration.dexed[0].polyphony);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].polyphony), configuration.dexed[1].polyphony);
#endif
  }
}

void UI_func_engine(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Engine"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].engine = constrain(configuration.dexed[selected_instance_id].engine + 1, ENGINE_MIN, ENGINE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].engine = constrain(configuration.dexed[selected_instance_id].engine - 1, ENGINE_MIN, ENGINE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setEngineType(configuration.dexed[selected_instance_id].engine);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].engine)
    {
      case 0:
        lcd.print(F("[MODERN]"));
        break;
      case 1:
        lcd.print(F("[MARK 1]"));
        break;
      case 2:
        lcd.print(F("[OPL   ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].engine), configuration.dexed[0].engine);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].engine), configuration.dexed[1].engine);
#endif
  }
}

void UI_func_mono_poly(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Mono/Poly"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].monopoly = constrain(configuration.dexed[selected_instance_id].monopoly + 1, MONOPOLY_MIN, MONOPOLY_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].monopoly = constrain(configuration.dexed[selected_instance_id].monopoly - 1, MONOPOLY_MIN, MONOPOLY_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
      lcd_active_instance_number(selected_instance_id);
#endif
    }

    MicroDexed[selected_instance_id]->setMonoMode(!configuration.dexed[selected_instance_id].monopoly);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].monopoly)
    {
      case 0:
        lcd.print(F("[MONOPHONIC]"));
        break;
      case 1:
        lcd.print(F("[POLYPHONIC]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].monopoly), configuration.dexed[0].monopoly);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].monopoly), configuration.dexed[1].monopoly);
#endif
  }
}

void UI_func_note_refresh(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Note Refresh"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].note_refresh = constrain(configuration.dexed[selected_instance_id].note_refresh + 1, NOTE_REFRESH_MIN, NOTE_REFRESH_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].note_refresh = constrain(configuration.dexed[selected_instance_id].note_refresh - 1, NOTE_REFRESH_MIN, NOTE_REFRESH_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setRefreshMode(configuration.dexed[selected_instance_id].note_refresh);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].note_refresh)
    {
      case 0:
        lcd.print(F("[NORMAL     ]"));
        break;
      case 1:
        lcd.print(F("[RETRIGGERED]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].note_refresh), configuration.dexed[0].note_refresh);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].note_refresh), configuration.dexed[1].note_refresh);
#endif
  }
}

void UI_func_pb_range(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("PB Range", configuration.dexed[selected_instance_id].pb_range, 1.0, PB_RANGE_MIN, PB_RANGE_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].pb_range = constrain(configuration.dexed[selected_instance_id].pb_range + ENCODER[ENC_R].speed(), PB_RANGE_MIN, PB_RANGE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].pb_range = constrain(configuration.dexed[selected_instance_id].pb_range - ENCODER[ENC_R].speed(), PB_RANGE_MIN, PB_RANGE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("PB Range", configuration.dexed[selected_instance_id].pb_range, 1.0, PB_RANGE_MIN, PB_RANGE_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setPBController(configuration.dexed[selected_instance_id].pb_range, configuration.dexed[selected_instance_id].pb_step);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 65, configuration.dexed[selected_instance_id].pb_range, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].pb_range), configuration.dexed[0].pb_range);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].pb_range), configuration.dexed[1].pb_range);
#endif
  }
}

void UI_func_pb_step(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("PB Step", configuration.dexed[selected_instance_id].pb_step, 1.0, PB_STEP_MIN, PB_STEP_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].pb_step = constrain(configuration.dexed[selected_instance_id].pb_step + ENCODER[ENC_R].speed(), PB_STEP_MIN, PB_STEP_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].pb_step = constrain(configuration.dexed[selected_instance_id].pb_step - ENCODER[ENC_R].speed(), PB_STEP_MIN, PB_STEP_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("PB Step", configuration.dexed[selected_instance_id].pb_step, 1.0, PB_STEP_MIN, PB_STEP_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setPBController(configuration.dexed[selected_instance_id].pb_range, configuration.dexed[selected_instance_id].pb_step);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 66, configuration.dexed[selected_instance_id].pb_step, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].pb_step), configuration.dexed[0].pb_step);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].pb_step), configuration.dexed[1].pb_step);
#endif
  }
}

void UI_func_mw_range(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("MW Range", configuration.dexed[selected_instance_id].mw_range, 1.0, MW_RANGE_MIN, MW_RANGE_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].mw_range = constrain(configuration.dexed[selected_instance_id].mw_range + ENCODER[ENC_R].speed(), MW_RANGE_MIN, MW_RANGE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].mw_range = constrain(configuration.dexed[selected_instance_id].mw_range - ENCODER[ENC_R].speed(), MW_RANGE_MIN, MW_RANGE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("MW Range", configuration.dexed[selected_instance_id].mw_range, 1.0, MW_RANGE_MIN, MW_RANGE_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setMWController(configuration.dexed[selected_instance_id].mw_range, configuration.dexed[selected_instance_id].mw_assign, configuration.dexed[selected_instance_id].mw_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 70, configuration.dexed[selected_instance_id].mw_range, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].mw_range), configuration.dexed[0].mw_range);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].mw_range), configuration.dexed[1].mw_range);
#endif
  }
}

void UI_func_mw_assign(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("MW Assign"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].mw_assign = constrain(configuration.dexed[selected_instance_id].mw_assign + 1, MW_ASSIGN_MIN, MW_ASSIGN_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].mw_assign = constrain(configuration.dexed[selected_instance_id].mw_assign - 1, MW_ASSIGN_MIN, MW_ASSIGN_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setMWController(configuration.dexed[selected_instance_id].mw_range, configuration.dexed[selected_instance_id].mw_assign, configuration.dexed[selected_instance_id].mw_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 71, configuration.dexed[selected_instance_id].mw_assign, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].mw_assign)
    {
      case 0:
        lcd.print(F("[   NONE    ]"));
        break;
      case 1:
        lcd.print(F("[PTCH       ]"));
        break;
      case 2:
        lcd.print(F("[     AMP   ]"));
        break;
      case 3:
        lcd.print(F("[PTCH AMP   ]"));
        break;
      case 4:
        lcd.print(F("[         EG]"));
        break;
      case 5:
        lcd.print(F("[PTCH     EG]"));
        break;
      case 6:
        lcd.print(F("[     AMP EG]"));
        break;
      case 7:
        lcd.print(F("[PTCH AMP EG]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].mw_assign), configuration.dexed[0].mw_assign);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].mw_assign), configuration.dexed[1].mw_assign);
#endif
  }
}

void UI_func_mw_mode(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("MW Mode"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].mw_mode = constrain(configuration.dexed[selected_instance_id].mw_mode + 1, MW_MODE_MIN, MW_MODE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].mw_mode = constrain(configuration.dexed[selected_instance_id].mw_mode - 1, MW_MODE_MIN, MW_MODE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setMWController(configuration.dexed[selected_instance_id].mw_range, configuration.dexed[selected_instance_id].mw_assign, configuration.dexed[selected_instance_id].mw_mode);
    MicroDexed[selected_instance_id]->controllers.refresh();

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].mw_mode)
    {
      case 0:
        lcd.print(F("[LINEAR      ]"));
        break;
      case 1:
        lcd.print(F("[REVERSE LIN.]"));
        break;
      case 2:
        lcd.print(F("[DIRECT      ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].mw_mode), configuration.dexed[0].mw_mode);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].mw_mode), configuration.dexed[1].mw_mode);
#endif
  }
}

void UI_func_fc_range(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("FC Range", configuration.dexed[selected_instance_id].fc_range, 1.0, FC_RANGE_MIN, FC_RANGE_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].fc_range = constrain(configuration.dexed[selected_instance_id].fc_range + ENCODER[ENC_R].speed(), FC_RANGE_MIN, FC_RANGE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].fc_range = constrain(configuration.dexed[selected_instance_id].fc_range - ENCODER[ENC_R].speed(), FC_RANGE_MIN, FC_RANGE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("FC Range", configuration.dexed[selected_instance_id].fc_range, 1.0, FC_RANGE_MIN, FC_RANGE_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setFCController(configuration.dexed[selected_instance_id].fc_range, configuration.dexed[selected_instance_id].fc_assign, configuration.dexed[selected_instance_id].fc_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 72, configuration.dexed[selected_instance_id].fc_range, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].fc_range), configuration.dexed[0].fc_range);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].fc_range), configuration.dexed[1].fc_range);
#endif
  }
}

void UI_func_fc_assign(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("FC Assign"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].fc_assign = constrain(configuration.dexed[selected_instance_id].fc_assign + 1, FC_ASSIGN_MIN, FC_ASSIGN_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].fc_assign = constrain(configuration.dexed[selected_instance_id].fc_assign - 1, FC_ASSIGN_MIN, FC_ASSIGN_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setFCController(configuration.dexed[selected_instance_id].fc_range, configuration.dexed[selected_instance_id].fc_assign, configuration.dexed[selected_instance_id].fc_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 73, configuration.dexed[selected_instance_id].fc_assign, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].fc_assign)
    {
      case 0:
        lcd.print(F("[   NONE    ]"));
        break;
      case 1:
        lcd.print(F("[PTCH       ]"));
        break;
      case 2:
        lcd.print(F("[     AMP   ]"));
        break;
      case 3:
        lcd.print(F("[PTCH AMP   ]"));
        break;
      case 4:
        lcd.print(F("[         EG]"));
        break;
      case 5:
        lcd.print(F("[PTCH     EG]"));
        break;
      case 6:
        lcd.print(F("[     AMP EG]"));
        break;
      case 7:
        lcd.print(F("[PTCH AMP EG]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].fc_assign), configuration.dexed[0].fc_assign);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].fc_assign), configuration.dexed[1].fc_assign);
#endif
  }
}

void UI_func_fc_mode(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("FC Mode"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].fc_mode = constrain(configuration.dexed[selected_instance_id].fc_mode + 1, FC_MODE_MIN, FC_MODE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].fc_mode = constrain(configuration.dexed[selected_instance_id].fc_mode - 1, FC_MODE_MIN, FC_MODE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setFCController(configuration.dexed[selected_instance_id].fc_range, configuration.dexed[selected_instance_id].fc_assign, configuration.dexed[selected_instance_id].fc_mode);
    MicroDexed[selected_instance_id]->controllers.refresh();

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].fc_mode)
    {
      case 0:
        lcd.print(F("[LINEAR      ]"));
        break;
      case 1:
        lcd.print(F("[REVERSE LIN.]"));
        break;
      case 2:
        lcd.print(F("[DIRECT      ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].fc_mode), configuration.dexed[0].fc_mode);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].fc_mode), configuration.dexed[1].fc_mode);
#endif
  }
}

void UI_func_bc_range(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("BC Range", configuration.dexed[selected_instance_id].bc_range, 1.0, BC_RANGE_MIN, BC_RANGE_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].bc_range = constrain(configuration.dexed[selected_instance_id].bc_range + ENCODER[ENC_R].speed(), BC_RANGE_MIN, BC_RANGE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].bc_range = constrain(configuration.dexed[selected_instance_id].bc_range - ENCODER[ENC_R].speed(), BC_RANGE_MIN, BC_RANGE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("BC Range", configuration.dexed[selected_instance_id].bc_range, 1.0, BC_RANGE_MIN, BC_RANGE_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setBCController(configuration.dexed[selected_instance_id].bc_range, configuration.dexed[selected_instance_id].bc_assign, configuration.dexed[selected_instance_id].bc_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 74, configuration.dexed[selected_instance_id].bc_range, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].bc_range), configuration.dexed[0].bc_range);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].bc_range), configuration.dexed[1].bc_range);
#endif
  }
}

void UI_func_bc_assign(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("BC Assign"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].bc_assign = constrain(configuration.dexed[selected_instance_id].bc_assign + 1, BC_ASSIGN_MIN, BC_ASSIGN_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].bc_assign = constrain(configuration.dexed[selected_instance_id].bc_assign - 1, BC_ASSIGN_MIN, BC_ASSIGN_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setBCController(configuration.dexed[selected_instance_id].bc_range, configuration.dexed[selected_instance_id].bc_assign, configuration.dexed[selected_instance_id].bc_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 75, configuration.dexed[selected_instance_id].bc_assign, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].bc_assign)
    {
      case 0:
        lcd.print(F("[   NONE    ]"));
        break;
      case 1:
        lcd.print(F("[PTCH       ]"));
        break;
      case 2:
        lcd.print(F("[     AMP   ]"));
        break;
      case 3:
        lcd.print(F("[PTCH AMP   ]"));
        break;
      case 4:
        lcd.print(F("[         EG]"));
        break;
      case 5:
        lcd.print(F("[PTCH     EG]"));
        break;
      case 6:
        lcd.print(F("[     AMP EG]"));
        break;
      case 7:
        lcd.print(F("[PTCH AMP EG]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].bc_assign), configuration.dexed[0].bc_assign);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].bc_assign), configuration.dexed[1].bc_assign);
#endif
  }
}

void UI_func_bc_mode(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("BC Mode"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].bc_mode = constrain(configuration.dexed[selected_instance_id].bc_mode + 1, BC_MODE_MIN, BC_MODE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].bc_mode = constrain(configuration.dexed[selected_instance_id].bc_mode - 1, BC_MODE_MIN, BC_MODE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setBCController(configuration.dexed[selected_instance_id].bc_range, configuration.dexed[selected_instance_id].bc_assign, configuration.dexed[selected_instance_id].bc_mode);
    MicroDexed[selected_instance_id]->controllers.refresh();

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].bc_mode)
    {
      case 0:
        lcd.print(F("[LINEAR      ]"));
        break;
      case 1:
        lcd.print(F("[REVERSE LIN.]"));
        break;
      case 2:
        lcd.print(F("[DIRECT      ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].bc_mode), configuration.dexed[0].bc_mode);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].bc_mode), configuration.dexed[1].bc_mode);
#endif
  }
}

void UI_func_at_range(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("AT Range", configuration.dexed[selected_instance_id].at_range, 1.0, AT_RANGE_MIN, AT_RANGE_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].at_range = constrain(configuration.dexed[selected_instance_id].at_range + ENCODER[ENC_R].speed(), AT_RANGE_MIN, AT_RANGE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].at_range = constrain(configuration.dexed[selected_instance_id].at_range - ENCODER[ENC_R].speed(), AT_RANGE_MIN, AT_RANGE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("AT Range", configuration.dexed[selected_instance_id].at_range, 1.0, AT_RANGE_MIN, AT_RANGE_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setATController(configuration.dexed[selected_instance_id].at_range, configuration.dexed[selected_instance_id].at_assign, configuration.dexed[selected_instance_id].at_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 76, configuration.dexed[selected_instance_id].at_range, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].at_range), configuration.dexed[0].at_range);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].at_range), configuration.dexed[1].at_range);
#endif
  }
}

void UI_func_at_assign(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("AT Assign"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].at_assign = constrain(configuration.dexed[selected_instance_id].at_assign + 1, AT_ASSIGN_MIN, AT_ASSIGN_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].at_assign = constrain(configuration.dexed[selected_instance_id].at_assign - 1, AT_ASSIGN_MIN, AT_ASSIGN_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setATController(configuration.dexed[selected_instance_id].at_range, configuration.dexed[selected_instance_id].at_assign, configuration.dexed[selected_instance_id].at_mode);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 77, configuration.dexed[selected_instance_id].at_assign, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].at_assign)
    {
      case 0:
        lcd.print(F("[   NONE    ]"));
        break;
      case 1:
        lcd.print(F("[PTCH       ]"));
        break;
      case 2:
        lcd.print(F("[     AMP   ]"));
        break;
      case 3:
        lcd.print(F("[PTCH AMP   ]"));
        break;
      case 4:
        lcd.print(F("[         EG]"));
        break;
      case 5:
        lcd.print(F("[PTCH     EG]"));
        break;
      case 6:
        lcd.print(F("[     AMP EG]"));
        break;
      case 7:
        lcd.print(F("[PTCH AMP EG]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].at_assign), configuration.dexed[0].at_assign);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].at_assign), configuration.dexed[1].at_assign);
#endif
  }
}

void UI_func_at_mode(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("AT Mode"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].at_mode = constrain(configuration.dexed[selected_instance_id].at_mode + 1, AT_MODE_MIN, AT_MODE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].at_mode = constrain(configuration.dexed[selected_instance_id].at_mode - 1, AT_MODE_MIN, AT_MODE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setATController(configuration.dexed[selected_instance_id].at_range, configuration.dexed[selected_instance_id].at_assign, configuration.dexed[selected_instance_id].at_mode);
    MicroDexed[selected_instance_id]->controllers.refresh();

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].at_mode)
    {
      case 0:
        lcd.print(F("[LINEAR      ]"));
        break;
      case 1:
        lcd.print(F("[REVERSE LIN.]"));
        break;
      case 2:
        lcd.print(F("[DIRECT      ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].at_mode), configuration.dexed[0].at_mode);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].at_mode), configuration.dexed[1].at_mode);
#endif
  }
}

void UI_func_portamento_mode(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Port. Mode"));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].portamento_mode = constrain(configuration.dexed[selected_instance_id].portamento_mode + 1, PORTAMENTO_MODE_MIN, PORTAMENTO_MODE_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].portamento_mode = constrain(configuration.dexed[selected_instance_id].portamento_mode - 1, PORTAMENTO_MODE_MIN, PORTAMENTO_MODE_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setPortamentoMode(configuration.dexed[selected_instance_id].portamento_mode, configuration.dexed[selected_instance_id].portamento_glissando, configuration.dexed[selected_instance_id].portamento_time);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 67, configuration.dexed[selected_instance_id].portamento_mode, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].portamento_mode)
    {
      case 0:
        if (configuration.dexed[selected_instance_id].monopoly == 1)
          lcd.print(F("[RETAIN  ]"));
        else
          lcd.print(F("[FINGERED]"));
        break;
      case 1:
        if (configuration.dexed[selected_instance_id].monopoly == 1)
          lcd.print(F("[FOLLOW  ]"));
        else
          lcd.print(F("[FULL    ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].portamento_mode), configuration.dexed[0].portamento_mode);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].portamento_mode), configuration.dexed[1].portamento_mode);
#endif
  }
}

void UI_func_portamento_glissando(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Port. Gliss."));

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].portamento_glissando = constrain(configuration.dexed[selected_instance_id].portamento_glissando + 1, PORTAMENTO_GLISSANDO_MIN, PORTAMENTO_GLISSANDO_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].portamento_glissando = constrain(configuration.dexed[selected_instance_id].portamento_glissando - 1, PORTAMENTO_GLISSANDO_MIN, PORTAMENTO_GLISSANDO_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    MicroDexed[selected_instance_id]->setPortamentoMode(configuration.dexed[selected_instance_id].portamento_mode, configuration.dexed[selected_instance_id].portamento_glissando, configuration.dexed[selected_instance_id].portamento_time);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 68, configuration.dexed[selected_instance_id].portamento_glissando, 2);

    lcd.setCursor(0, 1);
    switch (configuration.dexed[selected_instance_id].portamento_glissando)
    {
      case 0:
        lcd.print(F("[OFF]"));
        break;
      case 1:
        lcd.print(F("[ON ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].portamento_glissando), configuration.dexed[0].portamento_glissando);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].portamento_glissando), configuration.dexed[1].portamento_glissando);
#endif
  }
}

void UI_func_portamento_time(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Port. Time", configuration.dexed[selected_instance_id].portamento_time, 1.0, PORTAMENTO_TIME_MIN, PORTAMENTO_TIME_MAX, 2, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].portamento_time = constrain(configuration.dexed[selected_instance_id].portamento_time + ENCODER[ENC_R].speed(), PORTAMENTO_TIME_MIN, PORTAMENTO_TIME_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].portamento_time = constrain(configuration.dexed[selected_instance_id].portamento_time - ENCODER[ENC_R].speed(), PORTAMENTO_TIME_MIN, PORTAMENTO_TIME_MAX);
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    lcd_display_bar_int("Portam. Time", configuration.dexed[selected_instance_id].portamento_time, 1.0, PORTAMENTO_TIME_MIN, PORTAMENTO_TIME_MAX, 2, false, false, false);

    MicroDexed[selected_instance_id]->setPortamentoMode(configuration.dexed[selected_instance_id].portamento_mode, configuration.dexed[selected_instance_id].portamento_glissando, configuration.dexed[selected_instance_id].portamento_time);
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 69, configuration.dexed[selected_instance_id].portamento_time, 2);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].portamento_time), configuration.dexed[0].portamento_time);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].portamento_time), configuration.dexed[1].portamento_time);
#endif
  }
}

void UI_handle_OP(uint8_t param)
{
  static uint8_t op_selected;

  lcd_OP_active_instance_number(selected_instance_id, configuration.dexed[selected_instance_id].op_enabled);

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("OP Enable"));
    lcd.setCursor(0, 1);
    for (uint8_t i = 2; i < 8; i++)
      lcd.write(i);

    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);

    lcd.setCursor(op_selected, 1);
    lcd.blink();
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if (LCDML.BT_checkUp() && encoderDir[ENC_R].Up())
    {
#if NUM_DEXED>1
      if (op_selected == 0)
      {
        selected_instance_id = !selected_instance_id;
        op_selected = 5;
        lcd_OP_active_instance_number(selected_instance_id, configuration.dexed[selected_instance_id].op_enabled);
      }
      else
#endif
        op_selected = constrain(op_selected - 1, 0, 5);
    }
    else if (LCDML.BT_checkDown() && encoderDir[ENC_R].Down())
    {
#if NUM_DEXED>1
      if (op_selected == 5)
      {
        selected_instance_id = !selected_instance_id;
        op_selected = 0;
        lcd_OP_active_instance_number(selected_instance_id, configuration.dexed[selected_instance_id].op_enabled);
      }
      else
#endif
        op_selected = constrain(op_selected + 1, 0, 5);
    }
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      if (bitRead(configuration.dexed[selected_instance_id].op_enabled, op_selected))
        bitClear(configuration.dexed[selected_instance_id].op_enabled, op_selected);
      else
        bitSet(configuration.dexed[selected_instance_id].op_enabled, op_selected);

      lcd_OP_active_instance_number(selected_instance_id, configuration.dexed[selected_instance_id].op_enabled);
    }

    lcd.setCursor(op_selected, 1);

    MicroDexed[selected_instance_id]->setOPs(configuration.dexed[selected_instance_id].op_enabled);
    MicroDexed[selected_instance_id]->doRefreshVoice();
    send_sysex_param(configuration.dexed[selected_instance_id].midi_channel, 155, configuration.dexed[selected_instance_id].op_enabled, 0);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd.noBlink();
    lcd.noCursor();
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].op_enabled), configuration.dexed[0].op_enabled);
#if NUM_DEXED > 1
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].op_enabled), configuration.dexed[1].op_enabled);
#endif
  }
}

void UI_func_information(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char version_string[LCD_cols + 1];

    encoderDir[ENC_R].reset();

    generate_version_string(version_string, sizeof(version_string));

    // setup function
    lcd.setCursor(0, 0);
    lcd.print(version_string);
    lcd.setCursor(0, 1);
    lcd.print(sd_string);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    ;
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();
  }
}

void UI_func_midi_soft_thru(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("MIDI Soft THRU"));
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
        configuration.sys.soft_midi_thru = constrain(configuration.sys.soft_midi_thru + 1, SOFT_MIDI_THRU_MIN, SOFT_MIDI_THRU_MAX);
      else if (LCDML.BT_checkUp())
        configuration.sys.soft_midi_thru = constrain(configuration.sys.soft_midi_thru - 1, SOFT_MIDI_THRU_MIN, SOFT_MIDI_THRU_MAX);
    }

    lcd.setCursor(0, 1);
    switch (configuration.sys.soft_midi_thru)
    {
      case 0:
        lcd.print(F("[OFF]"));
        break;
      case 1:
        lcd.print(F("[ON ]"));
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, sys.soft_midi_thru), configuration.sys.soft_midi_thru);
  }
}

void UI_func_velocity_level(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Velocity Lvl", configuration.dexed[selected_instance_id].velocity_level, 1.0, VELOCITY_LEVEL_MIN, VELOCITY_LEVEL_MAX, 3, false, false, true);

    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
        configuration.dexed[selected_instance_id].velocity_level = constrain(configuration.dexed[selected_instance_id].velocity_level + ENCODER[ENC_R].speed(), VELOCITY_LEVEL_MIN, VELOCITY_LEVEL_MAX);
      else if (LCDML.BT_checkUp())
        configuration.dexed[selected_instance_id].velocity_level = constrain(configuration.dexed[selected_instance_id].velocity_level - ENCODER[ENC_R].speed(), VELOCITY_LEVEL_MIN, VELOCITY_LEVEL_MAX);
    }
#if NUM_DEXED > 1
    else if (LCDML.BT_checkEnter())
    {
      selected_instance_id = !selected_instance_id;
      lcd_active_instance_number(selected_instance_id);
      lcd.setCursor(14, 0);
      lcd.write(0);
      lcd.setCursor(15, 0);
      lcd.write(1);
    }
#endif

    lcd_display_bar_int("Velocity Lvl", configuration.dexed[selected_instance_id].velocity_level, 1.0, VELOCITY_LEVEL_MIN, VELOCITY_LEVEL_MAX, 3, false, false, false);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    if (selected_instance_id == 0)
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[0].velocity_level), configuration.dexed[0].velocity_level);
#if NUM_DEXED > 1
    else
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, dexed[1].velocity_level), configuration.dexed[1].velocity_level);
#endif
  }
}

void UI_func_eeprom_reset(uint8_t param)
{
  static bool yesno = false;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    // setup function
    lcd.print("Reset EEPROM?");
    lcd.setCursor(0, 1);
    lcd.print("[NO ]");
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
        yesno = true;
      else if (LCDML.BT_checkUp())
        yesno = false;
      else if (LCDML.BT_checkEnter())
      {
        if (yesno == true)
        {
          LCDML.DISP_clear();
          lcd.print("EEPROM Reset:");

          initial_values_from_eeprom(true);

          lcd.setCursor(0, 1);
          lcd.print("Done.");
          delay(MESSAGE_WAIT_TIME);
          _softRestart();
        }
        else
        {
          lcd.setCursor(0, 1);
          lcd.print("Canceled.");
          delay(MESSAGE_WAIT_TIME);
          LCDML.FUNC_goBackToMenu();
        }
      }

      if (yesno == true)
      {
        lcd.setCursor(1, 1);
        lcd.print("YES");
      }
      else
      {
        lcd.setCursor(1, 1);
        lcd.print("NO ");
      }
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd.setCursor(0, 1);
    lcd.print("Canceled.");
    delay(MESSAGE_WAIT_TIME);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_voice_select(uint8_t param)
{
  static uint8_t menu_voice_select = MENU_VOICE_SOUND;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd_active_instance_number(selected_instance_id);

    char bank_name[BANK_NAME_LEN];
    char voice_name[VOICE_NAME_LEN];

    if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
      strcpy(bank_name, "*ERROR*");
    if (!get_voice_by_bank_name(configuration.performance.bank[selected_instance_id], bank_name, configuration.performance.voice[selected_instance_id], voice_name, sizeof(voice_name)))
      strcpy(voice_name, "*ERROR*");

    lcd.setCursor(14, 0);
    lcd.write(0);
    lcd.setCursor(15, 0);
    lcd.write(1);

#ifdef TEENSY3_6
    lcd.createChar(6, (uint8_t*)special_chars[16]); // MIDI activity note symbol
    lcd.createChar(7, (uint8_t*)special_chars[16]); // MIDI activity note symbol
#endif
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    char bank_name[BANK_NAME_LEN];
    char voice_name[VOICE_NAME_LEN];

    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && (encoderDir[ENC_R].ButtonShort() || encoderDir[ENC_R].ButtonLong())))
    {
      uint8_t bank_tmp;
      int8_t voice_tmp;

      if (LCDML.BT_checkUp())
      {
        switch (menu_voice_select)
        {
          case MENU_VOICE_BANK:
            memset(g_bank_name[selected_instance_id], 0, BANK_NAME_LEN);
            bank_tmp = constrain(configuration.performance.bank[selected_instance_id] - ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
            configuration.performance.bank[selected_instance_id] = bank_tmp;
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(false);
#endif
            load_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(true);
#endif
            break;
          case MENU_VOICE_SOUND:
            memset(g_voice_name[selected_instance_id], 0, VOICE_NAME_LEN);
            voice_tmp = configuration.performance.voice[selected_instance_id] - ENCODER[ENC_R].speed();
            if (voice_tmp < 0 && configuration.performance.bank[selected_instance_id] - 1 >= 0)
            {
              configuration.performance.bank[selected_instance_id]--;
              configuration.performance.bank[selected_instance_id] = constrain(configuration.performance.bank[selected_instance_id], 0, MAX_BANKS - 1);
            }
            else if (voice_tmp < 0 && configuration.performance.bank[selected_instance_id] - 1 <= 0)
            {
              voice_tmp = 0;
            }
            if (voice_tmp < 0)
              voice_tmp = MAX_VOICES + voice_tmp;
            configuration.performance.voice[selected_instance_id] = constrain(voice_tmp, 0, MAX_VOICES - 1);

#ifdef DISPLAY_LCD_SPI
            change_disp_sd(false);
#endif
            load_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(true);
#endif
            break;
        }
      }
      else if (LCDML.BT_checkDown())
      {
        switch (menu_voice_select)
        {
          case MENU_VOICE_BANK:
            memset(g_bank_name[selected_instance_id], 0, BANK_NAME_LEN);
            bank_tmp = constrain(configuration.performance.bank[selected_instance_id] + ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
            configuration.performance.bank[selected_instance_id] = bank_tmp;
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(false);
#endif
            load_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(true);
#endif
            break;
          case MENU_VOICE_SOUND:
            memset(g_voice_name[selected_instance_id], 0, VOICE_NAME_LEN);
            voice_tmp = configuration.performance.voice[selected_instance_id] + ENCODER[ENC_R].speed();
            if (voice_tmp >= MAX_VOICES && configuration.performance.bank[selected_instance_id] + 1 < MAX_BANKS)
            {
              voice_tmp %= MAX_VOICES;
              configuration.performance.bank[selected_instance_id]++;
              configuration.performance.bank[selected_instance_id] = constrain(configuration.performance.bank[selected_instance_id], 0, MAX_BANKS - 1);
            }
            else if (voice_tmp >= MAX_VOICES && configuration.performance.bank[selected_instance_id] + 1 >= MAX_BANKS)
            {
              voice_tmp = MAX_VOICES - 1;
            }
            configuration.performance.voice[selected_instance_id] =  constrain(voice_tmp, 0, MAX_VOICES - 1);

#ifdef DISPLAY_LCD_SPI
            change_disp_sd(false);
#endif
            load_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);
#ifdef DISPLAY_LCD_SPI
            change_disp_sd(true);
#endif
            break;
        }
      }
      else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonPressed())
      {
        if (menu_voice_select == MENU_VOICE_BANK)
          menu_voice_select = MENU_VOICE_SOUND;
        else
          menu_voice_select = MENU_VOICE_BANK;
      }
#if NUM_DEXED > 1
      else if (LCDML.BT_checkEnter())
      {
        selected_instance_id = !selected_instance_id;
        lcd_active_instance_number(selected_instance_id);
        lcd.setCursor(14, 0);
        lcd.write(0);
        lcd.setCursor(15, 0);
        lcd.write(1);
      }
#endif
    }

    if (strlen(g_bank_name[selected_instance_id]) > 0)
    {
      strcpy(bank_name, g_bank_name[selected_instance_id]);
    }
    else
    {
      if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
        strcpy(bank_name, "*ERROR*");
    }

    if (strlen(g_voice_name[selected_instance_id]) > 0)
    {
      strcpy(voice_name, g_voice_name[selected_instance_id]);
    }
    else
    {
      if (!get_voice_by_bank_name(configuration.performance.bank[selected_instance_id], bank_name, configuration.performance.voice[selected_instance_id], voice_name, sizeof(voice_name)))
        strcpy(voice_name, "*ERROR*");
    }

    lcd.show(0, 0, 2, configuration.performance.bank[selected_instance_id]);
    lcd.show(1, 0, 2, configuration.performance.voice[selected_instance_id] + 1);
    lcd.show(0, 3, 10, bank_name);
    lcd.show(1, 3, 10, voice_name);

    switch (menu_voice_select)
    {
      case MENU_VOICE_BANK:
        lcd.show(0, 2, 1, "[");
        lcd.show(0, 13, 1, "]");
        lcd.show(1, 2, 1, " ");
        lcd.show(1, 13, 1, " ");
        break;
      case MENU_VOICE_SOUND:
        lcd.show(0, 2, 1, " ");
        lcd.show(0, 13, 1, " ");
        lcd.show(1, 2, 1, "[");
        lcd.show(1, 13, 1, "]");
        break;
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    encoderDir[ENC_R].reset();
    if (selected_instance_id == 0)
    {
      //eeprom_update_var(offsetof(configuration_s, performance.voice[0]), configuration.performance.voice[0], "configuration.performance.voice[0]");
      //eeprom_update_var(offsetof(configuration_s, performance.bank[0]), configuration.performance.bank[0], "configuration.performance.bank[0]");
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voice[0]), configuration.performance.voice[0]);
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.bank[0]), configuration.performance.bank[0]);
    }
#if NUM_DEXED > 1
    else
    {
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voice[1]), configuration.performance.voice[1]);
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.bank[1]), configuration.performance.bank[1]);
    }
#endif
  }
}

void UI_func_volume(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_L].reset();

    lcd_special_chars(BLOCKBAR);
    lcd_display_bar_int("Master Vol.", configuration.sys.vol, 1.0, VOLUME_MIN, VOLUME_MAX, 3, false, false, true);

    back_from_volume = 0;
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_L].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_L].Up()))
    {
      back_from_volume = 0;

      if (LCDML.BT_checkDown())
      {
        configuration.sys.vol = constrain(configuration.sys.vol + ENCODER[ENC_L].speed(), VOLUME_MIN, VOLUME_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.sys.vol = constrain(configuration.sys.vol - ENCODER[ENC_L].speed(), VOLUME_MIN, VOLUME_MAX);
      }
    }

    lcd_display_bar_int("Master Vol.", configuration.sys.vol, 1.0, VOLUME_MIN, VOLUME_MAX, 3, false, false, false);
    set_volume(configuration.sys.vol, configuration.sys.mono);
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, sys.vol), configuration.sys.vol);
    encoderDir[ENC_L].reset();
  }
}

void UI_func_load_performance(uint8_t param)
{
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[10];

    mode = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Load Perf. SD"));
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.sys.performance_number);
    lcd.print(tmp);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.sys.performance_number = constrain(configuration.sys.performance_number + ENCODER[ENC_L].speed(), PERFORMANCE_NUM_MIN, PERFORMANCE_NUM_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.sys.performance_number = constrain(configuration.sys.performance_number - ENCODER[ENC_L].speed(), PERFORMANCE_NUM_MIN, PERFORMANCE_NUM_MAX);
      }
      else if (LCDML.BT_checkEnter())
      {
        mode = 0xff;

        lcd.setCursor(0, 1);
        if (load_sd_performance(configuration.sys.performance_number) == false)
          lcd.print("Does not exist.");
        else
        {
          load_sd_voice(configuration.performance.bank[0], configuration.performance.voice[0], 0);
          load_sd_voiceconfig(configuration.performance.voiceconfig_number[0], 0);
          set_voiceconfig_params(0);
#if NUM_DEXED > 1
          set_voiceconfig_params(1);
          load_sd_voice(configuration.performance.bank[0], configuration.performance.voice[1], 1);
          load_sd_voiceconfig(configuration.performance.voiceconfig_number[1], 1);
#endif
          load_sd_fx(configuration.performance.fx_number);
          set_fx_params();

          lcd.print("Done.           ");
        }
        delay(MESSAGE_WAIT_TIME);

        LCDML.FUNC_goBackToMenu();
      }

      lcd.setCursor(0, 1);
      char tmp[10];
      sprintf(tmp, "[%2d]", configuration.sys.performance_number);
      lcd.print(tmp);
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }
    else
      eeprom_update_performance();

    encoderDir[ENC_R].reset();
  }
}

void UI_func_save_performance(uint8_t param)
{
  static bool overwrite;
  static bool yesno;
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[FILENAME_LEN];

    yesno = false;
    mode = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Save Perf. SD"));
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.sys.performance_number);
    lcd.print(tmp);

    sprintf(tmp, "/%s/%s%d.syx", PERFORMANCE_CONFIG_PATH, PERFORMANCE_CONFIG_NAME, configuration.sys.performance_number);
    if (SD.exists(tmp))
      overwrite = true;
    else
      overwrite = false;
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        if (mode == 0)
          configuration.sys.performance_number = constrain(configuration.sys.performance_number + ENCODER[ENC_L].speed(), PERFORMANCE_NUM_MIN, PERFORMANCE_NUM_MAX);
        else
          yesno = true;
      }
      else if (LCDML.BT_checkUp())
      {
        if (mode == 0)
          configuration.sys.performance_number = constrain(configuration.sys.performance_number - ENCODER[ENC_L].speed(), PERFORMANCE_NUM_MIN, PERFORMANCE_NUM_MAX);
        else
          yesno = false;
      }
      else if (LCDML.BT_checkEnter())
      {
        if (mode == 0 && overwrite == true)
        {
          mode = 1;
          lcd.setCursor(0, 1);
          lcd.print(F("Overwrite: [   ]"));
        }
        else
        {
          mode = 0xff;
          if (overwrite == false || yesno == true)
          {
            if (yesno == true)
            {
              char tmp[FILENAME_LEN];
              sprintf(tmp, "/%s/%s%d.syx", PERFORMANCE_CONFIG_PATH, PERFORMANCE_CONFIG_NAME, configuration.sys.performance_number);
              SD.remove(tmp);
            }
            save_sd_performance(configuration.sys.performance_number);
            lcd.show(1, 0, 16, "Done.");
            delay(MESSAGE_WAIT_TIME);
            LCDML.FUNC_goBackToMenu();
          }
          else if (overwrite == true && yesno == false)
          {
            char tmp[17];

            mode = 0;
            lcd.setCursor(0, 1);
            sprintf(tmp, "[%2d]            ", configuration.sys.performance_number);
            lcd.print(tmp);
          }
        }
      }

      if (mode == 0)
      {
        char tmp[FILENAME_LEN];
        sprintf(tmp, "/%s/%s%d.syx", PERFORMANCE_CONFIG_PATH, PERFORMANCE_CONFIG_NAME, configuration.sys.performance_number);
        if (SD.exists(tmp))
          overwrite = true;
        else
          overwrite = false;

        lcd.setCursor(0, 1);
        sprintf(tmp, "[%2d]", configuration.sys.performance_number);
        lcd.print(tmp);
      }
      else
      {
        lcd.setCursor(12, 1);
        if (yesno == true)
          lcd.print(F("YES"));
        else
          lcd.print(F("NO "));
      }
    }
    encoderDir[ENC_R].reset();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }

    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, sys.performance_number), configuration.sys.performance_number);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_load_voiceconfig(uint8_t param)
{
#if NUMDEXED > 1
  static int8_t selected_instance_id;
#else
  uint8_t selected_instance_id = 0;
#endif

  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[10];

    selected_instance_id = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Load VoiceCfg SD"));
#if NUMDEXED > 1
    mode = 0;
    lcd.setCursor(0, 1);
    lcd.print(F("Instance [0]"));
#else
    mode = 1;
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.performance.voiceconfig_number[selected_instance_id]);
    lcd.print(tmp);
#endif
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        if (mode == 0)
          selected_instance_id = (selected_instance_id + 1) % 2;
        else if (mode == 1)
          configuration.performance.voiceconfig_number[selected_instance_id] = constrain(configuration.performance.voiceconfig_number[selected_instance_id] + ENCODER[ENC_L].speed(), VOICECONFIG_NUM_MIN, VOICECONFIG_NUM_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        if (mode == 0)
          selected_instance_id = (selected_instance_id - 1) % 2;
        else if (mode == 1)
          configuration.performance.voiceconfig_number[selected_instance_id] = constrain(configuration.performance.voiceconfig_number[selected_instance_id] - ENCODER[ENC_L].speed(), VOICECONFIG_NUM_MIN, VOICECONFIG_NUM_MAX);
      }
      else if (LCDML.BT_checkEnter())
      {
        mode = 0xff;
        lcd.setCursor(0, 1);
        if (load_sd_voiceconfig(configuration.performance.voiceconfig_number[selected_instance_id], selected_instance_id) == false)
          lcd.print("Does not exist. ");
        else
          lcd.print("Done.           ");

        delay(MESSAGE_WAIT_TIME);

        LCDML.FUNC_goBackToMenu();
      }

      if (mode == 0)
      {
        lcd.setCursor(10, 1);
        lcd.print(selected_instance_id);
      }
      else if (mode == 1)
      {
        lcd.setCursor(0, 1);
        char tmp[10];
        sprintf(tmp, "[%2d]", configuration.performance.voiceconfig_number[selected_instance_id]);
        lcd.print(tmp);
      }
    }
    encoderDir[ENC_R].reset();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }
    else
      eeprom_update_dexed(selected_instance_id);

#if NUM_DEXED > 1
    if (selected_instance_id > 0)
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voiceconfig_number[1]), configuration.performance.voiceconfig_number[1]);
    else
#endif
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voiceconfig_number[0]), configuration.performance.voiceconfig_number[0]);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_save_voiceconfig(uint8_t param)
{
#if NUMDEXED > 1
  static int8_t selected_instance_id;
#else
  uint8_t selected_instance_id = 0;
#endif

  static bool overwrite;
  static bool yesno;
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[FILENAME_LEN];

    yesno = false;
    selected_instance_id = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Save VoiceCfg SD"));
#if NUMDEXED > 1
    mode = 0;
    lcd.setCursor(0, 1);
    lcd.print(F("Instance [0]"));
#else
    mode = 1;
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.performance.voiceconfig_number[selected_instance_id]);
    lcd.print(tmp);

    sprintf(tmp, "/%s/%s%d.syx", VOICE_CONFIG_PATH, VOICE_CONFIG_NAME, configuration.performance.voiceconfig_number[selected_instance_id]);
    if (SD.exists(tmp))
      overwrite = true;
    else
      overwrite = false;
#endif
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        if (mode == 0)
          selected_instance_id = (selected_instance_id + 1) % 2;
        else if (mode == 1)
          configuration.performance.voiceconfig_number[selected_instance_id] = constrain(configuration.performance.voiceconfig_number[selected_instance_id] + ENCODER[ENC_L].speed(), VOICECONFIG_NUM_MIN, VOICECONFIG_NUM_MAX);
        else
          yesno = true;
      }
      else if (LCDML.BT_checkUp())
      {
        if (mode == 0)
          selected_instance_id = (selected_instance_id - 1) % 2;
        else if (mode == 1)
          configuration.performance.voiceconfig_number[selected_instance_id] = constrain(configuration.performance.voiceconfig_number[selected_instance_id] - ENCODER[ENC_L].speed(), VOICECONFIG_NUM_MIN, VOICECONFIG_NUM_MAX);
        else
          yesno = false;
      }
      else if (LCDML.BT_checkEnter())
      {
        if (mode == 1 && overwrite == true)
        {
          mode = 2;
          lcd.setCursor(0, 1);
          lcd.print(F("Overwrite: [   ]"));
        }
        else
        {
          mode = 0xff;
          if (overwrite == false || yesno == true)
          {
            if (yesno == true)
            {
              char tmp[FILENAME_LEN];
              sprintf(tmp, "/%s/%s%d.syx", VOICE_CONFIG_PATH, VOICE_CONFIG_NAME, configuration.performance.voiceconfig_number[selected_instance_id]);
              SD.remove(tmp);
            }
            save_sd_voiceconfig(configuration.performance.voiceconfig_number[selected_instance_id], selected_instance_id);
            lcd.show(1, 0, 16, "Done.");
            delay(MESSAGE_WAIT_TIME);
            LCDML.FUNC_goBackToMenu();
          }
          else if (overwrite == true && yesno == false)
          {
            char tmp[17];

            mode = 1;
            lcd.setCursor(0, 1);
            sprintf(tmp, "[%2d]            ", configuration.performance.voiceconfig_number[selected_instance_id]);
            lcd.print(tmp);
          }
        }
      }

      if (mode == 0)
      {
        lcd.setCursor(10, 1);
        lcd.print(configuration.performance.voiceconfig_number[selected_instance_id]);
      }
      else if (mode == 1)
      {
        char tmp[FILENAME_LEN];

        sprintf(tmp, "/%s/%s%d.syx", VOICE_CONFIG_PATH, VOICE_CONFIG_NAME, configuration.performance.voiceconfig_number[selected_instance_id]);
        if (SD.exists(tmp))
          overwrite = true;
        else
          overwrite = false;

        lcd.setCursor(0, 1);
        sprintf(tmp, "[%2d]", configuration.performance.voiceconfig_number[selected_instance_id]);
        lcd.print(tmp);
      }
      else if (mode == 2)
      {
        lcd.setCursor(12, 1);
        if (yesno == true)
          lcd.print(F("YES"));
        else
          lcd.print(F("NO "));
      }
    }
    encoderDir[ENC_R].reset();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }

#if NUM_DEXED > 1
    if (selected_instance_id > 0)
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voiceconfig_number[1]), configuration.performance.voiceconfig_number[1]);
    else
#endif
      EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voiceconfig_number[0]), configuration.performance.voiceconfig_number[0]);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_load_fx(uint8_t param)
{
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[10];

    mode = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Load FX SD"));
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.performance.fx_number);
    lcd.print(tmp);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.performance.fx_number = constrain(configuration.performance.fx_number + ENCODER[ENC_L].speed(), FX_NUM_MIN, FX_NUM_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.performance.fx_number = constrain(configuration.performance.fx_number - ENCODER[ENC_L].speed(), FX_NUM_MIN, FX_NUM_MAX);
      }
      else if (LCDML.BT_checkEnter())
      {
        mode = 0xff;

        lcd.setCursor(0, 1);
        if (load_sd_fx(configuration.performance.fx_number) == false)
          lcd.print("Does not exist. ");
        else
          lcd.print("Done.           ");

        delay(MESSAGE_WAIT_TIME);

        LCDML.FUNC_goBackToMenu();
      }

      lcd.setCursor(0, 1);
      char tmp[10];
      sprintf(tmp, "[%2d]", configuration.performance.fx_number);
      lcd.print(tmp);
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }
    else
      eeprom_update_fx();

    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.fx_number), configuration.performance.fx_number);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_save_fx(uint8_t param)
{
  static bool overwrite;
  static bool yesno;
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    char tmp[FILENAME_LEN];

    yesno = false;
    mode = 0;

    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Save FX"));
    lcd.setCursor(0, 1);
    sprintf(tmp, "[%2d]", configuration.performance.fx_number);
    lcd.print(tmp);

    sprintf(tmp, "/%s/%s%d.syx", FX_CONFIG_PATH, FX_CONFIG_NAME, configuration.performance.fx_number);
    if (SD.exists(tmp))
      overwrite = true;
    else
      overwrite = false;
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()) || (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort()))
    {
      if (LCDML.BT_checkDown())
      {
        if (mode == 0)
          configuration.performance.fx_number = constrain(configuration.performance.fx_number + ENCODER[ENC_L].speed(), FX_NUM_MIN, FX_NUM_MAX);
        else
          yesno = true;
      }
      else if (LCDML.BT_checkUp())
      {
        if (mode == 0)
          configuration.performance.fx_number = constrain(configuration.performance.fx_number - ENCODER[ENC_L].speed(), FX_NUM_MIN, FX_NUM_MAX);
        else
          yesno = false;
      }
      else if (LCDML.BT_checkEnter())
      {
        if (mode == 0 && overwrite == true)
        {
          mode = 1;
          lcd.setCursor(0, 1);
          lcd.print(F("Overwrite: [   ]"));
        }
        else
        {
          mode = 0xff;
          if (overwrite == false || yesno == true)
          {
            if (yesno == true)
            {
              char tmp[FILENAME_LEN];
              sprintf(tmp, "/%s/%s%d.syx", FX_CONFIG_PATH, FX_CONFIG_NAME, configuration.performance.fx_number);
              SD.remove(tmp);
            }
            save_sd_fx(configuration.performance.fx_number);

            lcd.show(1, 0, 16, "Done.");
            LCDML.FUNC_goBackToMenu();
            delay(MESSAGE_WAIT_TIME);
          }
          else if (overwrite == true && yesno == false)
          {
            char tmp[17];

            mode = 0;
            lcd.setCursor(0, 1);
            sprintf(tmp, "[%2d]            ", configuration.performance.fx_number);
            lcd.print(tmp);
          }
        }
      }

      if (mode == 0)
      {
        char tmp[FILENAME_LEN];
        sprintf(tmp, "/%s/%s%d.syx", FX_CONFIG_PATH, FX_CONFIG_NAME, configuration.performance.fx_number);
        if (SD.exists(tmp))
          overwrite = true;
        else
          overwrite = false;

        lcd.setCursor(0, 1);
        sprintf(tmp, "[%2d]", configuration.performance.fx_number);
        lcd.print(tmp);
      }
      else
      {
        lcd.setCursor(12, 1);
        if (yesno == true)
          lcd.print(F("YES"));
        else
          lcd.print(F("NO "));
      }
    }
    encoderDir[ENC_R].reset();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }

    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.fx_number), configuration.performance.fx_number);

    encoderDir[ENC_R].reset();
  }
}

void UI_func_save_voice(uint8_t param)
{
  static bool yesno;
  static uint8_t mode;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    yesno = false;
#if NUM_DEXED == 1
    mode = 1;
#else
    mode = 0;
#endif

#if NUM_DEXED == 1
    char bank_name[BANK_NAME_LEN];

    if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
      strcpy(bank_name, "*ERROR*");

    lcd.setCursor(0, 0);
    lcd.print(F("Save to Bank"));
    lcd.show(1, 0, 2, configuration.performance.bank[selected_instance_id]);
    lcd.show(1, 3, 10, bank_name);
    lcd.show(1, 2, 1, "[");
    lcd.show(1, 13, 1, "]");
#else
    lcd.setCursor(0, 0);
    lcd.print(F("Save Instance"));
    lcd_active_instance_number(selected_instance_id);
    lcd.setCursor(5, 1);
    lcd.write(0);
    lcd.setCursor(10, 1);
    lcd.write(1);
#endif
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    char bank_name[BANK_NAME_LEN];
    char voice_name[VOICE_NAME_LEN];

    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      switch (mode)
      {
        case 0: // Instance selection
          if (LCDML.BT_checkDown() || LCDML.BT_checkUp())
            selected_instance_id = !selected_instance_id;

          lcd_active_instance_number(selected_instance_id);
          lcd.setCursor(5, 1);
          lcd.write(0);
          lcd.setCursor(10, 1);
          lcd.write(1);
          break;
        case 1: // Bank selection
          if (LCDML.BT_checkDown())
            configuration.performance.bank[selected_instance_id] = constrain(configuration.performance.bank[selected_instance_id] + ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
          else if (LCDML.BT_checkUp() && configuration.performance.bank[selected_instance_id] > 0)
            configuration.performance.bank[selected_instance_id] = constrain(configuration.performance.bank[selected_instance_id] - ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);

          if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
            strcpy(bank_name, "*ERROR*");

          lcd.show(1, 0, 2, configuration.performance.bank[selected_instance_id]);
          lcd.show(1, 3, 10, bank_name);
          break;
        case 2: // Voice selection
          if (LCDML.BT_checkDown() && configuration.performance.voice[selected_instance_id] < MAX_VOICES - 1)
            configuration.performance.voice[selected_instance_id] = constrain(configuration.performance.voice[selected_instance_id] + ENCODER[ENC_R].speed(), 0, MAX_VOICES - 1);
          else if (LCDML.BT_checkUp() && configuration.performance.voice[selected_instance_id] > 0)
            configuration.performance.voice[selected_instance_id] = constrain(configuration.performance.voice[selected_instance_id] - ENCODER[ENC_R].speed(), 0, MAX_VOICES - 1);

          if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
            strncpy(bank_name, "*ERROR*", sizeof(bank_name));
          if (!get_voice_by_bank_name(configuration.performance.bank[selected_instance_id], bank_name, configuration.performance.voice[selected_instance_id], voice_name, sizeof(voice_name)))
            strncpy(voice_name, "*ERROR*", sizeof(voice_name));

          lcd.show(1, 0, 2, configuration.performance.voice[selected_instance_id] + 1);
          lcd.show(1, 3, 10, voice_name);
          break;
        case 3: // Yes/No selection
          yesno = !yesno;
          if (yesno == true)
          {
            lcd.show(1, 1, 3, "YES");
          }
          else
          {
            lcd.show(1, 1, 3, "NO");
          }
          break;
      }
    }
    else if (LCDML.BT_checkEnter())
    {
      if (encoderDir[ENC_R].ButtonShort())
        mode++;
      switch (mode)
      {
        case 1:
          if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
            strncpy(bank_name, "*ERROR*", sizeof(bank_name));
          lcd.setCursor(0, 0);
          lcd.print(F("Save to Bank"));
          lcd.show(1, 0, 2, configuration.performance.bank[selected_instance_id]);
          lcd.show(1, 3, 10, bank_name);
          lcd.show(1, 2, 2, " [");
          lcd.show(1, 14, 1, "]");
          break;
        case 2:
          if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
            strncpy(bank_name, "*ERROR*", sizeof(bank_name));
          if (!get_voice_by_bank_name(configuration.performance.bank[selected_instance_id], bank_name, configuration.performance.voice[selected_instance_id], voice_name, sizeof(voice_name)))
            strncpy(voice_name, "*ERROR*", sizeof(voice_name));

          lcd.show(0, 0, 16, "Save to Bank");
          lcd.show(0, 13, 2, configuration.performance.bank[selected_instance_id]);
          lcd.show(1, 0, 2, configuration.performance.voice[selected_instance_id] + 1);
          lcd.show(1, 3, 10, voice_name);
          break;
        case 3:
          lcd.show(0, 0, 16, "Overwrite?");
          lcd.show(1, 0, 15, "[NO");
          lcd.show(1, 4, 1, "]");
          break;
        default:
          if (yesno == true)
          {
#ifdef DEBUG
            bool ret = save_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);

            if (ret == true)
              Serial.println(F("Saving voice OK."));
            else
              Serial.println(F("Error while saving voice."));
#else
            save_sd_voice(configuration.performance.bank[selected_instance_id], configuration.performance.voice[selected_instance_id], selected_instance_id);
#endif

            lcd.show(1, 0, 16, "Done.");
            delay(MESSAGE_WAIT_TIME);

            mode = 0xff;
            break;
          }

          LCDML.FUNC_goBackToMenu();
      }
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);

    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }
    else
    {
      if (selected_instance_id == 0)
      {
        EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voice[0]), configuration.performance.voice[0]);
        EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.bank[0]), configuration.performance.bank[0]);
      }
#if NUM_DEXED > 1
      else
      {
        EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.voice[0]), configuration.performance.voice[1]);
        EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, performance.bank[0]), configuration.performance.bank[1]);
      }
#endif
    }
    encoderDir[ENC_R].reset();
  }
}

void UI_func_sysex_receive_bank(uint8_t param)
{
  static bool yesno;
  static uint8_t mode;
  static uint8_t bank_number;
  static uint8_t ui_select_name_state;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    yesno = false;
    mode = 0;
    bank_number = configuration.performance.bank[selected_instance_id];
    memset(receive_bank_filename, 0, sizeof(receive_bank_filename));

    lcd.setCursor(0, 0);
    lcd.print(F("MIDI Recv Bank"));
    lcd.setCursor(2, 1);
    lcd.print(F("["));
    lcd.setCursor(14, 1);
    lcd.print(F("]"));
    if (!get_bank_name(configuration.performance.bank[selected_instance_id], receive_bank_filename, sizeof(receive_bank_filename)))
      strcpy(receive_bank_filename, "*ERROR*");
    lcd.show(1, 0, 2, bank_number);
    lcd.show(1, 3, 10, receive_bank_filename);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
      {
        switch (mode)
        {
          case 0:
            bank_number = constrain(bank_number + ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
            if (!get_bank_name(bank_number, receive_bank_filename, sizeof(receive_bank_filename)))
              strcpy(receive_bank_filename, "*ERROR*");
            lcd.show(1, 0, 2, bank_number);
            lcd.show(1, 3, 10, receive_bank_filename);
            break;
          case 1:
            yesno = !yesno;
            if (yesno)
              lcd.show(1, 12, 3, "YES");
            else
              lcd.show(1, 12, 3, "NO");
            break;
          case 2:
            ui_select_name_state = UI_select_name(1, 1, receive_bank_filename, BANK_NAME_LEN - 1, false);
            break;
        }
      }
      else if (LCDML.BT_checkUp())
      {
        switch (mode)
        {
          case 0:
            bank_number = constrain(bank_number - ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
            if (!get_bank_name(bank_number, receive_bank_filename, sizeof(receive_bank_filename)))
              strcpy(receive_bank_filename, "*ERROR*");
            lcd.show(1, 0, 2, bank_number);
            lcd.show(1, 3, 10, receive_bank_filename);
            break;
          case 1:
            yesno = !yesno;
            if (yesno)
              lcd.show(1, 12, 3, "YES");
            else
              lcd.show(1, 12, 3, "NO");
            break;
          case 2:
            ui_select_name_state = UI_select_name(1, 1, receive_bank_filename, BANK_NAME_LEN - 1, false);
            break;
        }
      }
    }
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      if (mode == 0)
      {
        if (!strcmp(receive_bank_filename, "*ERROR*"))
        {
          yesno = true;
          strcpy(receive_bank_filename, "NONAME");
          mode = 2;
          lcd.setCursor(0, 1);
          lcd.print(F("[          ]    "));
          ui_select_name_state = UI_select_name(1, 1, receive_bank_filename, BANK_NAME_LEN - 1, true);
          lcd.blink();
        }
        else
        {
          mode = 1;
          lcd.setCursor(0, 1);
          lcd.print(F("Overwrite: [NO ]"));
        }
      }
      else if (mode == 1 && yesno == true)
      {
        mode = 2;
        lcd.setCursor(0, 1);
        lcd.print(F("[          ]    "));
        ui_select_name_state = UI_select_name(1, 1, receive_bank_filename, BANK_NAME_LEN - 1, true);
        lcd.blink();
      }
      else if (mode == 2)
      {
        ui_select_name_state = UI_select_name(1, 1, receive_bank_filename, BANK_NAME_LEN - 1, false);
        if (ui_select_name_state == true)
        {
          if (yesno == true)
          {
#ifdef DEBUG
            Serial.print(F("Bank name: ["));
            Serial.print(receive_bank_filename);
            Serial.println(F("]"));
#endif
            char tmp[FILENAME_LEN];
            strcpy(tmp, receive_bank_filename);
            sprintf(receive_bank_filename, "/%d/%s.syx", bank_number, tmp);
#ifdef DEBUG
            Serial.print(F("Receiving into bank "));
            Serial.print(bank_number);
            Serial.print(F(" as filename "));
            Serial.print(receive_bank_filename);
            Serial.println(F("."));
#endif
            mode = 0xff;
            lcd.noBlink();
            lcd.setCursor(0, 1);
            lcd.print(F("Waiting...      "));
            /// Storing is done in SYSEX code
          }
        }
      }
      else if (mode >= 1 && yesno == false)
      {
        Serial.println(mode, DEC);
        memset(receive_bank_filename, 0, sizeof(receive_bank_filename));
        mode = 0xff;
        lcd.noBlink();
        lcd.setCursor(0, 1);
        lcd.print(F("Canceled.       "));
        delay(MESSAGE_WAIT_TIME);
        LCDML.FUNC_goBackToMenu();
      }
    }
    encoderDir[ENC_R].reset();
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();

    memset(receive_bank_filename, 0, sizeof(receive_bank_filename));
    lcd.noBlink();

    if (mode < 0xff)
    {
      lcd.setCursor(0, 1);
      lcd.print(F("Canceled.       "));
      delay(MESSAGE_WAIT_TIME);
    }
  }
}

void UI_func_sysex_send_bank(uint8_t param)
{
  char bank_name[BANK_NAME_LEN];
  static uint8_t bank_number;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    bank_number = configuration.performance.bank[selected_instance_id];
    lcd.setCursor(0, 0);
    lcd.print(F("MIDI Send Bank"));
    if (!get_bank_name(configuration.performance.bank[selected_instance_id], bank_name, sizeof(bank_name)))
      strncpy(bank_name, "*ERROR*", sizeof(bank_name));
    lcd.show(1, 2, 1, "[");
    lcd.show(1, 14, 1, "]");
    lcd.show(1, 0, 2, configuration.performance.bank[selected_instance_id]);
    lcd.show(1, 3, 10, bank_name);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
      {
        bank_number = constrain(bank_number + ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);

      }
      else if (LCDML.BT_checkUp())
      {
        bank_number = constrain(bank_number - ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
      }
      if (!get_bank_name(bank_number, bank_name, sizeof(bank_name)))
        strcpy(bank_name, "*ERROR*");
      lcd.show(1, 0, 2, bank_number);
      lcd.show(1, 3, 10, bank_name);
    }
    else if (LCDML.BT_checkEnter() && encoderDir[ENC_R].ButtonShort())
    {
      File sysex;
      char filename[FILENAME_LEN];

      if (get_bank_name(bank_number, bank_name, sizeof(bank_name)))
      {
        sprintf(filename, "/%d/%s.syx", bank_number, bank_name);
#ifdef DEBUG
        Serial.print(F("Send bank "));
        Serial.print(filename);
        Serial.println(F(" from SD."));
#endif
        sysex = SD.open(filename);
        if (!sysex)
        {
#ifdef DEBUG
          Serial.println(F("Connot read from SD."));
#endif
          lcd.show(1, 0, 16, "Read error.");
          bank_number = 0xff;
        }
        else
        {
          uint8_t bank_data[4104];

          sysex.read(bank_data, 4104);
          sysex.close();

          lcd.show(1, 0, 16, "Sending Ch");
          if (configuration.dexed[selected_instance_id].midi_channel == MIDI_CHANNEL_OMNI)
          {
            lcd.show(1, 11, 2, "01");
            send_sysex_bank(1, bank_data);
          }
          else
          {
            lcd.show(1, 11, 2, configuration.dexed[selected_instance_id].midi_channel + 1);
            send_sysex_bank(configuration.dexed[selected_instance_id].midi_channel, bank_data);
          }
          lcd.show(1, 0, 16, "Done.");
          bank_number = 0xff;
        }
      }
      else
      {
        lcd.show(1, 0, 16, "No bank.");
        bank_number = 0xff;
      }

      delay(MESSAGE_WAIT_TIME);
      LCDML.FUNC_goBackToMenu();
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();

    if (bank_number < 0xff)
    {
      lcd.setCursor(0, 1);
      lcd.print(F("Canceled.       "));
      delay(MESSAGE_WAIT_TIME);
    }
  }
}

void UI_func_sysex_send_voice(uint8_t param)
{
  static uint8_t mode;
  static uint8_t bank_number;
  static uint8_t voice_number;

  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    mode = 0;
    bank_number = configuration.performance.bank[selected_instance_id];
    voice_number = configuration.performance.voice[selected_instance_id];

    char bank_name[BANK_NAME_LEN];

    if (!get_bank_name(bank_number, bank_name, sizeof(bank_name)))
      strcpy(bank_name, "*ERROR*");

    lcd.setCursor(0, 0);
    lcd.print(F("MIDI Send Voice"));
    lcd.show(1, 0, 2, bank_number);
    lcd.show(1, 3, 10, bank_name);
    lcd.show(1, 2, 1, "[");
    lcd.show(1, 13, 1, "]");
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    char bank_name[BANK_NAME_LEN];
    char voice_name[VOICE_NAME_LEN];

    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      switch (mode)
      {
        case 0: // Bank selection
          if (LCDML.BT_checkDown())
            bank_number = constrain(bank_number + ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);
          else if (LCDML.BT_checkUp() && bank_number > 0)
            bank_number = constrain(bank_number - ENCODER[ENC_R].speed(), 0, MAX_BANKS - 1);

          if (!get_bank_name(bank_number, bank_name, sizeof(bank_name)))
            strcpy(bank_name, "*ERROR*");

          lcd.show(1, 0, 2, bank_number);
          lcd.show(1, 3, 10, bank_name);
          break;
        case 1: // Voice selection
          if (LCDML.BT_checkDown() && voice_number < MAX_VOICES - 1)
            voice_number = constrain(voice_number + ENCODER[ENC_R].speed(), 0, MAX_VOICES - 1);
          else if (LCDML.BT_checkUp() && voice_number > 0)
            voice_number = constrain(voice_number - ENCODER[ENC_R].speed(), 0, MAX_VOICES - 1);
          if (!get_bank_name(bank_number, bank_name, sizeof(bank_name)))
            strncpy(bank_name, "*ERROR*", sizeof(bank_name));
          if (!get_voice_by_bank_name(bank_number, bank_name, voice_number, voice_name, sizeof(voice_name)))
            strncpy(voice_name, "*ERROR*", sizeof(voice_name));

          lcd.show(1, 0, 2, voice_number + 1);
          lcd.show(1, 3, 10, voice_name);
          break;
      }
    }
    else if (LCDML.BT_checkEnter())
    {
      if (encoderDir[ENC_R].ButtonShort())
        mode++;
      switch (mode)
      {
        case 1:
          if (!get_bank_name(bank_number, bank_name, sizeof(bank_name)))
            strncpy(bank_name, "*ERROR*", sizeof(bank_name));
          if (!get_voice_by_bank_name(bank_number, bank_name, voice_number, voice_name, sizeof(voice_name)))
            strncpy(voice_name, "*ERROR*", sizeof(voice_name));

          lcd.show(1, 0, 2, voice_number + 1);
          lcd.show(1, 3, 10, voice_name);
          break;
        case 2:
          File sysex;
          char filename[FILENAME_LEN];

          if (get_bank_name(bank_number, bank_name, sizeof(bank_name)))
          {
            sprintf(filename, "/%d/%s.syx", bank_number, bank_name);
#ifdef DEBUG
            Serial.print(F("Send voice "));
            Serial.print(voice_number);
            Serial.print(F(" of "));
            Serial.print(filename);
            Serial.println(F(" from SD."));
#endif
            sysex = SD.open(filename);
            if (!sysex)
            {
#ifdef DEBUG
              Serial.println(F("Connot read from SD."));
#endif
              lcd.show(1, 0, 16, "Read error.");
              bank_number = 0xff;
            }
            else
            {
              uint8_t voice_data[155];
              uint8_t encoded_voice_data[128];

              sysex.seek(6 + (voice_number * 128));
              sysex.read(encoded_voice_data, 128);

              MicroDexed[selected_instance_id]->decodeVoice(encoded_voice_data, voice_data);

              lcd.show(1, 0, 16, "Sending Ch");
              if (configuration.dexed[selected_instance_id].midi_channel == MIDI_CHANNEL_OMNI)
              {
                lcd.show(1, 11, 2, "01");
                send_sysex_voice(1, voice_data);
              }
              else
              {
                lcd.show(1, 11, 2, configuration.dexed[selected_instance_id].midi_channel + 1);
                send_sysex_voice(configuration.dexed[selected_instance_id].midi_channel, voice_data);
              }
              delay(MESSAGE_WAIT_TIME);
              lcd.show(1, 0, 16, "Done.");
              sysex.close();

              bank_number = 0xff;
            }
          }
          else
          {
            lcd.show(1, 0, 16, "No voice.");
            bank_number = 0xff;
          }

          mode = 0xff;
          delay(MESSAGE_WAIT_TIME);
          LCDML.FUNC_goBackToMenu();

          break;
      }
    }
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    if (mode < 0xff)
    {
      lcd.show(1, 0, 16, "Canceled.");
      delay(MESSAGE_WAIT_TIME);
    }
    encoderDir[ENC_R].reset();
  }
}

void UI_func_eq_bass(uint8_t param)
{
#ifndef SGTL5000_AUDIO_ENHANCE
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    lcd.setCursor(0, 0);
    lcd.print(F("EQ Bass"));
    lcd.setCursor(0, 1);
    lcd.print(F("Not implemented."));
  }
#else
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    lcd_special_chars(METERBAR);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.eq_bass = constrain(configuration.fx.eq_bass + ENCODER[ENC_R].speed(), EQ_BASS_MIN, EQ_BASS_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.eq_bass = constrain(configuration.fx.eq_bass - ENCODER[ENC_R].speed(), EQ_BASS_MIN, EQ_BASS_MAX);
      }
    }
    lcd_display_meter_float("EQ Bass", configuration.fx.eq_bass, 0.1, 0.0, EQ_BASS_MIN, EQ_BASS_MAX, 1, 1, false, true, true);
    sgtl5000_1.eqBands(mapfloat(configuration.fx.eq_bass, EQ_BASS_MIN, EQ_BASS_MAX, -1.0, 1.0), mapfloat(configuration.fx.eq_treble, EQ_TREBLE_MIN, EQ_TREBLE_MAX, -1.0, 1.0));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.eq_bass), configuration.fx.eq_bass);
  }
#endif
}

void UI_func_eq_treble(uint8_t param)
{
#ifndef SGTL5000_AUDIO_ENHANCE
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    lcd.setCursor(0, 0);
    lcd.print(F("EQ Bass"));
    lcd.setCursor(0, 1);
    lcd.print(F("Not implemented."));
  }
#else
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();
    lcd_special_chars(METERBAR);
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    if ((LCDML.BT_checkDown() && encoderDir[ENC_R].Down()) || (LCDML.BT_checkUp() && encoderDir[ENC_R].Up()))
    {
      if (LCDML.BT_checkDown())
      {
        configuration.fx.eq_treble = constrain(configuration.fx.eq_treble + ENCODER[ENC_R].speed(), EQ_TREBLE_MIN, EQ_TREBLE_MAX);
      }
      else if (LCDML.BT_checkUp())
      {
        configuration.fx.eq_treble = constrain(configuration.fx.eq_treble - ENCODER[ENC_R].speed(), EQ_TREBLE_MIN, EQ_TREBLE_MAX);
      }
    }
    lcd_display_meter_float("EQ TREBLE", configuration.fx.eq_treble, 0.1, 0.0, EQ_TREBLE_MIN, EQ_TREBLE_MAX, 1, 1, false, true, true);
    sgtl5000_1.eqBands(mapfloat(configuration.fx.eq_treble, EQ_TREBLE_MIN, EQ_TREBLE_MAX, -1.0, 1.0), mapfloat(configuration.fx.eq_treble, EQ_TREBLE_MIN, EQ_TREBLE_MAX, -1.0, 1.0));
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    lcd_special_chars(SCROLLBAR);
    encoderDir[ENC_R].reset();
    EEPROM.update(EEPROM_START_ADDRESS + offsetof(configuration_s, fx.eq_treble), configuration.fx.eq_treble);
  }
#endif
}

void UI_function_not_enabled(void)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Function not"));
    lcd.setCursor(0, 1);
    lcd.print(F("enbaled!"));
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    ;
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();
  }
}

void UI_function_not_implemented(uint8_t param)
{
  if (LCDML.FUNC_setup())         // ****** SETUP *********
  {
    encoderDir[ENC_R].reset();

    lcd.setCursor(0, 0);
    lcd.print(F("Function not"));
    lcd.setCursor(0, 1);
    lcd.print(F("implemented!"));
  }

  if (LCDML.FUNC_loop())          // ****** LOOP *********
  {
    ;
  }

  if (LCDML.FUNC_close())     // ****** STABLE END *********
  {
    encoderDir[ENC_R].reset();
  }
}

bool UI_select_name(uint8_t y, uint8_t x, char* edit_string, uint8_t len, bool init)
{
  static int8_t edit_pos;
  static bool edit_mode;
  static uint8_t edit_value;
  static uint8_t last_char_pos;

  if (init == true)
  {
    edit_mode = false;
    edit_pos = 0;
    edit_value = search_accepted_char(edit_string[edit_pos]);
    last_char_pos = strlen(edit_string);

    string_trim(edit_string); // just to be sure

    lcd.setCursor(x, y);
    lcd.print(edit_string);
    lcd.setCursor(x, y);

    return (false);
  }

  if (LCDML.BT_checkDown() || LCDML.BT_checkUp())
  {
    if (LCDML.BT_checkDown())
    {
      if (edit_mode == true)
      {
        edit_value = search_accepted_char(edit_string[edit_pos]);

        if (edit_value < sizeof(accepted_chars) - 2)
          edit_value++;
        if (edit_value == 0 && edit_string[constrain(edit_pos + 1, 0, len)] > 0)
          edit_value = 1;

        edit_string[edit_pos] = accepted_chars[edit_value];

        lcd.setCursor(x + edit_pos, y);
        lcd.print(edit_string[edit_pos]);
      }
      else
      {
        if (edit_string[edit_pos] != 0 && edit_string[edit_pos] != 32)
          edit_pos = constrain(edit_pos + 1, 0, len);
        else
        {
          if (edit_pos + 1 > last_char_pos)
            edit_pos = len;
        }

        if (edit_pos == len)
        {
          lcd.noBlink();
          lcd.setCursor(x - 1, y);
          lcd.print(F(" "));
          lcd.setCursor(x + len, y);
          lcd.print(F(" "));
          lcd.setCursor(x + len + 1, y);
          lcd.print(F("[OK]"));
        }
      }
    }
    else if (LCDML.BT_checkUp())
    {
      if (edit_mode == true)
      {
        edit_value = search_accepted_char(edit_string[edit_pos]);

        if (edit_value >= 1)
          edit_value--;
        if (edit_value == 0 && edit_string[constrain(edit_pos + 1, 0, len)] > 0)
          edit_value = 1;
        edit_string[edit_pos] = accepted_chars[edit_value];

        lcd.setCursor(x + edit_pos, y);
        lcd.print(edit_string[edit_pos]);
      }
      else
      {
        if (edit_pos - 1 > last_char_pos)
          edit_pos = last_char_pos;
        else
          edit_pos = constrain(edit_pos - 1, 0, len - 1);

        if (edit_pos == last_char_pos)
        {
          lcd.setCursor(x - 1, y);
          lcd.print(F("["));
          lcd.setCursor(x + len, y);
          lcd.print(F("]"));
          lcd.setCursor(x + len + 1, y);
          lcd.print(F("    "));
          lcd.blink();
        }
      }
    }
  }
  else if (LCDML.BT_checkEnter())
  {
    last_char_pos = strlen(edit_string);
    if (edit_pos >= len)
    {
      edit_pos = 0;
      edit_mode = false;

      return (true);
    }
    else
    {
      last_char_pos = strlen(edit_string);
      edit_mode = !edit_mode;
    }
    if (edit_mode == false && edit_pos < len && edit_string[edit_pos] != 0 && edit_string[edit_pos] != 32)
      edit_pos++;
    if (edit_mode == true)
    {
      lcd.setCursor(x + len + 1, y);
      lcd.print(F("*"));
    }
    else
    {
      lcd.setCursor(x + len + 1, y);
      lcd.print(F(" "));
    }

  }
  lcd.setCursor(x + edit_pos, y);
  encoderDir[ENC_R].reset();

  return (false);
}

uint8_t search_accepted_char(uint8_t c)
{
  //if (c == 0)
  //  c = 32;

  for (uint8_t i = 0; i < sizeof(accepted_chars) - 1; i++)
  {
    Serial.print(i, DEC);
    Serial.print(":");
    Serial.print(c);
    Serial.print("==");
    Serial.println(accepted_chars[i], DEC);
    if (c == accepted_chars[i])
      return (i);
  }
  return (0);
}

void lcd_display_int(int16_t var, uint8_t size, bool zeros, bool brackets, bool sign)
{
  lcd_display_float(float(var), size, 0, zeros, brackets, sign);
}

void lcd_display_float(float var, uint8_t size_number, uint8_t size_fraction, bool zeros, bool brackets, bool sign)
{
  char s[LCD_cols + 1];
  char f[LCD_cols + 1];

  if (size_fraction > 0)
  {
    if (zeros == true && sign == true)
      sprintf(f, "%%+0%d.%df", size_number + size_fraction + 2, size_fraction);
    else if (zeros == true && sign == false)
      sprintf(f, "%%+0%d.%df", size_number + size_fraction + 1, size_fraction);
    else if (zeros == false && sign == true)
      sprintf(f, "%%+%d.%df", size_number + size_fraction + 2, size_fraction);
    else if (zeros == false && sign == false)
      sprintf(f, "%%%d.%df", size_number + size_fraction + 1, size_fraction);

    sprintf(s, f, var);
  }
  else
  {
    if (zeros == true && sign == true)
      sprintf(f, "%%+0%dd", size_number + 1);
    else if (zeros == true && sign == false)
      sprintf(f, "%%%0dd", size_number);
    else if (zeros == false && sign == true)
      sprintf(f, "%%+%dd", size_number + 1);
    else if (zeros == false && sign == false)
      sprintf(f, "%%%dd", size_number);

    sprintf(s, f, int(var));
  }

  if (brackets == true)
  {
    char tmp[LCD_cols + 1];

    strcpy(tmp, s);
    sprintf(s, "[%s]", tmp);
  }

  lcd.print(s);
}

inline void lcd_display_bar_int(const char* title, uint32_t value, float factor, int32_t min_value, int32_t max_value, uint8_t size, bool zeros, bool sign, bool init)
{
  lcd_display_bar_float(title, float(value), factor, min_value, max_value, size, 0, zeros, sign, init);
}

void lcd_display_bar_float(const char* title, float value, float factor, int32_t min_value, int32_t max_value, uint8_t size_number, uint8_t size_fraction, bool zeros, bool sign, bool init)
{
  uint8_t size;
  float v;
  float _vi = 0.0;
  uint8_t vf;
  uint8_t vi;

  if (size_fraction == 0)
    size = size_number;
  else
    size = size_number + size_fraction + 1;
  if (sign == true)
    size++;

  v = float((value - min_value) * (LCD_cols - size)) / (max_value - min_value);
  vf = uint8_t(modff(v, &_vi) * 10.0 + 0.5);
  vi = uint8_t(_vi);

  if (sign == true)
    size += 1;

  // Title
  if (init == true)
    lcd.show(0, 0, LCD_cols - 3, title);

  // Value
  lcd.setCursor(LCD_cols - size, 1);
  lcd_display_float(value * factor, size_number, size_fraction, zeros, false, sign); // TBD

  // Bar
  lcd.setCursor(0, 1);

  if (vi == 0)
  {
    lcd.write((uint8_t)(vf / 2.0 - 0.5) + 2);
    for (uint8_t i = vi + 1; i < LCD_cols - size; i++)
      lcd.print(F(" ")); // empty block
  }
  else
  {
    for (uint8_t i = 0; i < vi; i++)
      lcd.write((uint8_t)4 + 2); // full block
    if (vi < LCD_cols - size)
      lcd.write((uint8_t)(vf / 2.0 - 0.5) + 2);
    for (uint8_t i = vi + 1; i < LCD_cols - size; i++)
      lcd.print(F(" ")); // empty block
  }
}

inline void lcd_display_meter_int(const char* title, uint32_t value, float factor, float offset, int32_t min_value, int32_t max_value, uint8_t size, bool zeros, bool sign, bool init)
{
  lcd_display_meter_float(title, float(value), factor, offset, min_value, max_value, size, 0, zeros, sign, init);
}

void lcd_display_meter_float(const char* title, float value, float factor, float offset, int32_t min_value, int32_t max_value, uint8_t size_number, uint8_t size_fraction, bool zeros, bool sign, bool init)
{
  uint8_t size = 0;
  float v;
  float _vi = 0.0;
  uint8_t vf;
  uint8_t vi;

  if (size_fraction == 0)
    size = size_number;
  else
    size = size_number + size_fraction + 1;
  if (sign == true)
    size++;

  v = float((value - min_value) * (LCD_cols - size)) / (max_value - min_value);
  vf = uint8_t(modff(v, &_vi) * 10.0 + 0.5);
  vi = uint8_t(_vi);

  if (init == true)
  {
    // Title
    lcd.setCursor(0, 0);
    lcd.print(title);
  }

  // Value
  lcd.setCursor(LCD_cols - size, 1);
  lcd_display_float((value + offset) * factor, size_number, size_fraction, zeros, false, sign);

  // Bar
  lcd.setCursor(0, 1);

  if (vi == 0)
  {
    lcd.write((uint8_t)(vf / 2.0) + 2);
    for (uint8_t i = 1; i < LCD_cols - size; i++)
      lcd.print(F(" ")); // empty block
  }
  else if (vi == LCD_cols - size)
  {
    for (uint8_t i = 0; i < LCD_cols - size - 1; i++)
      lcd.print(F(" ")); // empty block
    lcd.write(4 + 2);
  }
  else
  {
    for (uint8_t i = 0; i < LCD_cols - size; i++)
      lcd.print(F(" ")); // empty block
    lcd.setCursor(vi, 1);
    lcd.write((uint8_t)(vf / 2.0) + 2);
    for (uint8_t i = vi + 1; i < LCD_cols - size; i++)
      lcd.print(F(" ")); // empty block
  }
}

uint8_t bit_reverse8(uint8_t v)
{
  uint8_t result = 0;
  for ( ; v > 0; v >>= 1 )
    (result <<= 1) |= (v & 1);
  return (result);
}

void lcd_active_instance_number(uint8_t instance_id)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (instance_id == 0)
    {
      if (configuration.dexed[instance_id].polyphony == 0)
        instance_num[0][i] = bit_reverse8(special_chars[0][i]);
      else
        instance_num[0][i] = special_chars[0][i];

      if (configuration.dexed[!instance_id].polyphony == 0)
      {
        instance_num[1][i] = bit_reverse8(special_chars[1][i]);
        instance_num[1][i] = ~instance_num[1][i];
      }
      else
        instance_num[1][i] = ~special_chars[1][i];
    }
    else
    {
      if (configuration.dexed[!instance_id].polyphony == 0)
      {
        instance_num[0][i] = bit_reverse8(special_chars[0][i]);
        instance_num[0][i] = ~instance_num[0][i];
      }
      else
        instance_num[0][i] = ~special_chars[0][i];

      if (configuration.dexed[instance_id].polyphony == 0)
        instance_num[1][i] = bit_reverse8(special_chars[1][i]);
      else
        instance_num[1][i] = special_chars[1][i];
    }
  }

#if NUM_DEXED == 1
  lcd.createChar(0, instance_num[0]);
  lcd.createChar(1, (uint8_t*)special_chars[17]);
#else
  lcd.createChar(0, instance_num[0]);
  lcd.createChar(1, instance_num[1]);
#endif
}

void lcd_OP_active_instance_number(uint8_t instance_id, uint8_t op)
{
  uint8_t i, n;

  for (n = 2; n < 8; n++)
  {
    for (i = 0; i < 8; i++)
    {
      if (bitRead(op, n - 2))
        instance_num[n][i] = special_chars[n][i];
      else
        instance_num[n][i] = ~special_chars[n][i];
    }
    lcd.createChar(n, instance_num[n]);
  }

  for (i = 0; i < 8; i++)
  {
    if (instance_id == 0)
    {
      if (configuration.dexed[instance_id].polyphony == 0)
        instance_num[0][i] = bit_reverse8(special_chars[0][i]);
      else
        instance_num[0][i] = special_chars[0][i];

      if (configuration.dexed[!instance_id].polyphony == 0)
      {
        instance_num[1][i] = bit_reverse8(special_chars[1][i]);
        instance_num[1][i] = ~instance_num[1][i];
      }
      else
        instance_num[1][i] = ~special_chars[1][i];
    }
    else
    {
      if (configuration.dexed[!instance_id].polyphony == 0)
      {
        instance_num[0][i] = bit_reverse8(special_chars[0][i]);
        instance_num[0][i] = ~instance_num[0][i];
      }
      else
        instance_num[0][i] = ~special_chars[0][i];

      if (configuration.dexed[instance_id].polyphony == 0)
        instance_num[1][i] = bit_reverse8(special_chars[1][i]);
      else
        instance_num[1][i] = special_chars[1][i];
    }
  }

  lcd.createChar(0, instance_num[0]);
#if NUM_DEXED > 1
  lcd.createChar(1, instance_num[1]);
#else
  lcd.createChar(1, (uint8_t *)special_chars[17]);
#endif
}

void lcd_special_chars(uint8_t mode)
{
  switch (mode)
  {
    case SCROLLBAR:
      // set special chars for scrollbar
      for (uint8_t i = 0; i < 5; i++)
      {
#ifdef I2C_DISPLAY
        lcd.createChar(i, (uint8_t*)scroll_bar[i]);
#else
        flipped_scroll_bar[i] = rotTile(scroll_bar[i]);
#endif
      }
      break;
    case BLOCKBAR:
      // set special chars for volume-bar
      for (uint8_t i = 0; i < 7; i++)
      {
#ifdef I2C_DISPLAY
        lcd.createChar(i + 2, (uint8_t*)block_bar[i]);
#else
        flipped_block_bar[i] = rotTile(block_bar[i]);
#endif
      }
      break;
    case METERBAR:
      // set special chars for panorama-bar
      for (uint8_t i = 0; i < 7; i++)
      {
#ifdef I2C_DISPLAY
        lcd.createChar(i + 2, (uint8_t*)meter_bar[i]);
#else
        flipped_meter_bar[i] = rotTile(meter_bar[i]);
#endif
      }
      break;
  }
}

void eeprom_update_var(uint16_t pos, uint8_t val, const char* val_string)
{
#ifdef DEBUG
  char tmp[80];
  sprintf(tmp, "EEPROM update '%s' at position %d with value %d.", val_string, pos, val);
  Serial.println(tmp);
#endif
  EEPROM.update(EEPROM_START_ADDRESS + pos, val);
}

void string_trim(char *s)
{
  int i;

  while (isspace (*s)) s++;   // skip left side white spaces
  for (i = strlen (s) - 1; (isspace (s[i])); i--) ;   // skip right side white spaces
  s[i + 1] = '\0';
}

#endif
