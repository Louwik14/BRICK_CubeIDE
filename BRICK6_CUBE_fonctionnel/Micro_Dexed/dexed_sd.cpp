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

#include <Arduino.h>
#include "config.h"
#include <Wire.h>
#include <SD.h>
#include "dexed.h"
#include "dexed_sd.h"

extern void init_MIDI_send_CC(void);

/******************************************************************************
   SD BANK/VOICE LOADING
 ******************************************************************************/

bool load_sd_voice(uint8_t b, uint8_t v, uint8_t instance_id)
{
  v = constrain(v, 0, MAX_VOICES - 1);
  b = constrain(b, 0, MAX_BANKS - 1);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];
    char bank_name[BANK_NAME_LEN];
    uint8_t data[128];

    get_bank_name(b, bank_name, sizeof(bank_name));
    sprintf(filename, "/%d/%s.syx", b, bank_name);

    sysex = SD.open(filename);
    if (!sysex)
    {
#ifdef DEBUG
      Serial.print(F("E : Cannot open "));
      Serial.print(filename);
      Serial.println(F(" on SD."));
#endif
      return (false);
    }

    if (get_sd_voice(sysex, v, data))
    {
#ifdef DEBUG
      char voice_name[VOICE_NAME_LEN];
      get_voice_name(b, v, voice_name, sizeof(voice_name));
      Serial.print(F("Loading voice from "));
      Serial.print(filename);
      Serial.print(F(" ["));
      Serial.print(voice_name);
      Serial.println(F("]"));
#endif
      bool ret = MicroDexed[instance_id]->decodeVoice(data, MicroDexed[instance_id]->data);
#ifdef DEBUG
      show_patch(instance_id);
#endif

      configuration.dexed[instance_id].transpose = MicroDexed[instance_id]->data[DEXED_VOICE_OFFSET + DEXED_TRANSPOSE];

      sysex.close();

      uint8_t data_copy[155];
      MicroDexed[instance_id]->getVoiceData(data_copy);
      send_sysex_voice(configuration.dexed[instance_id].midi_channel, data_copy);
      init_MIDI_send_CC();
      return (ret);
    }
#ifdef DEBUG
    else
      Serial.println(F("E : Cannot load voice data"));
#endif
    sysex.close();
  }

  return (false);
}

bool save_sd_voice(uint8_t b, uint8_t v, uint8_t instance_id)
{
  v = constrain(v, 0, MAX_VOICES - 1);
  b = constrain(b, 0, MAX_BANKS - 1);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];
    char bank_name[BANK_NAME_LEN];
    uint8_t data[128];

    get_bank_name(b, bank_name, sizeof(bank_name));
    sprintf(filename, "/%d/%s.syx", b, bank_name);

    sysex = SD.open(filename, FILE_WRITE);
    if (!sysex)
    {
#ifdef DEBUG
      Serial.print(F("E : Cannot open "));
      Serial.print(filename);
      Serial.println(F(" on SD."));
#endif
      return (false);
    }

    MicroDexed[instance_id]->encodeVoice(data);

    if (put_sd_voice(sysex, v, data))
    {
#ifdef DEBUG
      char voice_name[VOICE_NAME_LEN];
      strncpy(voice_name, (char*)&MicroDexed[instance_id]->data[145], sizeof(voice_name));
      Serial.print(F("Saving voice to "));
      Serial.print(filename);
      Serial.print(F(" ["));
      Serial.print(voice_name);
      Serial.println(F("]"));
#endif
      sysex.close();

      return (true);
    }
#ifdef DEBUG
    else
      Serial.println(F("E : Cannot load voice data"));
#endif
    sysex.close();
  }

  return (false);
}

bool get_sd_voice(File sysex, uint8_t voice_number, uint8_t* data)
{
  uint16_t n;
  int32_t bulk_checksum_calc = 0;
  int8_t bulk_checksum;

  if (sysex.size() != 4104) // check sysex size
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx file size wrong."));
#endif
    return (false);
  }
  if (sysex.read() != 0xf0)  // check sysex start-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx start byte not found."));
#endif
    return (false);
  }
  if (sysex.read() != 0x43)  // check sysex vendor is Yamaha
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx vendor not Yamaha."));
#endif
    return (false);
  }
  sysex.seek(4103);
  if (sysex.read() != 0xf7)  // check sysex end-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx end byte not found."));
#endif
    return (false);
  }
  sysex.seek(3);
  if (sysex.read() != 0x09)  // check for sysex type (0x09=32 voices)
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx type not 32 voices."));
#endif
    return (false);
  }
  sysex.seek(4102); // Bulk checksum
  bulk_checksum = sysex.read();

  sysex.seek(6); // start of bulk data
  for (n = 0; n < 4096; n++)
  {
    uint8_t d = sysex.read();
    if (n >= voice_number * 128 && n < (voice_number + 1) * 128)
      data[n - (voice_number * 128)] = d;
    bulk_checksum_calc -= d;
  }
  bulk_checksum_calc &= 0x7f;

#ifdef DEBUG
  Serial.print(F("Bulk checksum : 0x"));
  Serial.print(bulk_checksum_calc, HEX);
  Serial.print(F(" [0x"));
  Serial.print(bulk_checksum, HEX);
  Serial.println(F("]"));
#endif

  if (bulk_checksum_calc != bulk_checksum)
  {
#ifdef DEBUG
    Serial.print(F("E : Bulk checksum mismatch : 0x"));
    Serial.print(bulk_checksum_calc, HEX);
    Serial.print(F(" != 0x"));
    Serial.println(bulk_checksum, HEX);
#endif
    return (false);
  }

  MicroDexed[0]->render_time_max = 0;

  return (true);
}

bool put_sd_voice(File sysex, uint8_t voice_number, uint8_t* data)
{
  uint16_t n;
  int32_t bulk_checksum_calc = 0;

  sysex.seek(0);

  if (sysex.size() != 4104) // check sysex size
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx file size wrong."));
#endif
    return (false);
  }
  if (sysex.read() != 0xf0)  // check sysex start-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx start byte not found."));
#endif
    return (false);
  }
  if (sysex.read() != 0x43)  // check sysex vendor is Yamaha
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx vendor not Yamaha."));
#endif
    return (false);
  }
  sysex.seek(4103);
  if (sysex.read() != 0xf7)  // check sysex end-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx end byte not found."));
#endif
    return (false);
  }
  sysex.seek(3);
  if (sysex.read() != 0x09)  // check for sysex type (0x09=32 voices)
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx type not 32 voices."));
#endif
    return (false);
  }

  sysex.seek(6 + (voice_number * 128));
  sysex.write(data, 128);

  // checksum calculation
  sysex.seek(6); // start of bulk data
  for (n = 0; n < 4096; n++)
  {
    uint8_t d = sysex.read();
    bulk_checksum_calc -= d;
  }
  sysex.seek(4102); // Bulk checksum
  sysex.write(bulk_checksum_calc & 0x7f);

#ifdef DEBUG
  Serial.print(F("Bulk checksum : 0x"));
  Serial.println(bulk_checksum_calc & 0x7f, HEX);
#endif

  return (true);
}

bool save_sd_bank(const char* bank_filename, uint8_t* data)
{
  char tmp[FILENAME_LEN];
  char tmp2[FILENAME_LEN];
  int bank_number;
  File root, entry;

  if (sd_card > 0)
  {
#ifdef DEBUG
    Serial.print(F("Trying so store "));
    Serial.print(bank_filename);
    Serial.println(F("."));
#endif

    // first remove old bank
    sscanf(bank_filename, "/%d/%s", &bank_number, tmp);
    sprintf(tmp, "/%d", bank_number);
    root = SD.open(tmp);
    while (42 == 42)
    {
      entry = root.openNextFile();
      if (entry)
      {
        if (!entry.isDirectory())
        {
#ifdef DEBUG
          Serial.print(F("Removing "));
          Serial.print(tmp);
          Serial.print(F("/"));
          Serial.println(entry.name());
#endif
          sprintf(tmp2, "%s/%s", tmp, entry.name());
          entry.close();
#ifndef DEBUG
          SD.remove(tmp2);
#else
          bool r = SD.remove(tmp2);
          if (r == false)
          {
            Serial.print(F("E: cannot remove "));
            Serial.print(tmp2);
            Serial.println(F("."));
          }
#endif
          break;
        }
      }
      else
      {
        break;
      }
    }
    root.close();

    // store new bank at /<b>/<bank_name>.syx
#ifdef DEBUG
    Serial.print(F("Storing bank as "));
    Serial.print(bank_filename);
    Serial.print(F("..."));
#endif
    root = SD.open(bank_filename, FILE_WRITE);
    root.write(data, 4104);
    root.close();
#ifdef DEBUG
    Serial.println(F(" done."));
#endif
  }
  else
    return (false);

  return (true);
}

/******************************************************************************
   SD VOICECONFIG
 ******************************************************************************/
bool load_sd_voiceconfig(int8_t vc, uint8_t instance_id)
{
  if (vc < 0)
    return (false);

  vc = constrain(vc, 0, MAX_VOICECONFIG);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", VOICE_CONFIG_PATH, VOICE_CONFIG_NAME, vc);

    // first check if file exists...
    if (SD.exists(filename))
    {
      // ... and if: load
#ifdef DEBUG
      Serial.print(F("Found voice configuration ["));
      Serial.print(filename);
      Serial.println(F("]... loading..."));
#endif
      sysex = SD.open(filename);
      if (sysex)
      {
        get_sd_data(sysex, 0x42, (uint8_t*)&configuration.dexed[instance_id]);
        set_voiceconfig_params(instance_id);
        sysex.close();

        return (true);
      }
#ifdef DEBUG
      else
      {
        Serial.print(F("E : Cannot open "));
        Serial.print(filename);
        Serial.println(F(" on SD."));
      }
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("No "));
      Serial.print(filename);
      Serial.println(F(" available."));
#endif
    }
  }
  return (false);
}

bool save_sd_voiceconfig(uint8_t vc, uint8_t instance_id)
{
  vc = constrain(vc, 0, MAX_VOICECONFIG);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", VOICE_CONFIG_PATH, VOICE_CONFIG_NAME, vc);

#ifdef DEBUG
    Serial.print(F("Saving voice config "));
    Serial.print(vc);
    Serial.print(F("["));
    Serial.print(instance_id);
    Serial.print(F("]"));
    Serial.print(F(" to "));
    Serial.println(filename);
#endif

    sysex = SD.open(filename, FILE_WRITE);
    if (sysex)
    {
      if (write_sd_data(sysex, 0x42, (uint8_t*)&configuration.dexed[instance_id], sizeof(configuration.dexed[instance_id])))
      {
        sysex.close();
        return (true);
      }
      sysex.close();
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("E : Cannot open "));
      Serial.print(filename);
      Serial.println(F(" on SD."));
#endif
    }
  }

  return (false);
}

/******************************************************************************
   SD FX
 ******************************************************************************/
bool load_sd_fx(int8_t fx)
{
  if (fx < 0)
    return (false);

  fx = constrain(fx, 0, MAX_FX);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", FX_CONFIG_PATH, FX_CONFIG_NAME, fx);

    // first check if file exists...
    if (SD.exists(filename))
    {
      // ... and if: load
#ifdef DEBUG
      Serial.print(F("Found fx configuration ["));
      Serial.print(filename);
      Serial.println(F("]... loading..."));
#endif
      sysex = SD.open(filename);
      if (sysex)
      {
        get_sd_data(sysex, 0x43, (uint8_t*)&configuration.fx);
        set_fx_params();
        sysex.close();

        return (true);
      }
#ifdef DEBUG
      else
      {
        Serial.print(F("E : Cannot open "));
        Serial.print(filename);
        Serial.println(F(" on SD."));
      }
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("No "));
      Serial.print(filename);
      Serial.println(F(" available."));
#endif
    }
  }

  return (false);
}

bool save_sd_fx(uint8_t fx)
{
  fx = constrain(fx, 0, MAX_FX);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", FX_CONFIG_PATH, FX_CONFIG_NAME, fx);

#ifdef DEBUG
    Serial.print(F("Saving fx config "));
    Serial.print(fx);
    Serial.print(F(" to "));
    Serial.println(filename);
#endif

    sysex = SD.open(filename, FILE_WRITE);
    if (sysex)
    {
      if (write_sd_data(sysex, 0x43, (uint8_t*)&configuration.fx, sizeof(configuration.fx)))
      {
        sysex.close();
        return (true);
      }
      sysex.close();
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("E : Cannot open "));
      Serial.print(filename);
      Serial.println(F(" on SD."));
#endif
      return (false);
    }
  }

  return (false);
}

/******************************************************************************
   SD PERFORMANCE
 ******************************************************************************/
bool load_sd_performance(int8_t p)
{
  if (p < 0)
    return (false);

  p = constrain(p, 0, MAX_PERFORMANCE);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", PERFORMANCE_CONFIG_PATH, PERFORMANCE_CONFIG_NAME, p);

    // first check if file exists...
    if (SD.exists(filename))
    {
      // ... and if: load
#ifdef DEBUG
      Serial.print(F("Found performance configuration ["));
      Serial.print(filename);
      Serial.println(F("]... loading..."));
#endif
      sysex = SD.open(filename);
      if (sysex)
      {
        get_sd_data(sysex, 0x44, (uint8_t*)&configuration.performance);
        sysex.close();

        for (uint8_t instance_id = 0; instance_id < NUM_DEXED; instance_id++)
        {
          load_sd_voice(configuration.performance.bank[instance_id], configuration.performance.voice[instance_id], instance_id);
          load_sd_voiceconfig(configuration.performance.voiceconfig_number[instance_id], instance_id);

          MicroDexed[instance_id]->controllers.refresh();
          MicroDexed[instance_id]->panic();
        }
        load_sd_fx(configuration.performance.fx_number);

        return (true);
      }
#ifdef DEBUG
      else
      {
        Serial.print(F("E : Cannot open "));
        Serial.print(filename);
        Serial.println(F(" on SD."));
      }
#endif
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("No "));
      Serial.print(filename);
      Serial.println(F(" available."));
#endif
    }
  }
  return (false);
}

bool save_sd_performance(uint8_t p)
{
  p = constrain(p, 0, MAX_PERFORMANCE);

  if (sd_card > 0)
  {
    File sysex;
    char filename[FILENAME_LEN];

    sprintf(filename, "/%s/%s%d.syx", PERFORMANCE_CONFIG_PATH, PERFORMANCE_CONFIG_NAME, p);

#ifdef DEBUG
    Serial.print(F("Saving performance config "));
    Serial.print(p);
    Serial.print(F(" to "));
    Serial.println(filename);
#endif

    sysex = SD.open(filename, FILE_WRITE);
    if (sysex)
    {
      if (write_sd_data(sysex, 0x44, (uint8_t*)&configuration.performance, sizeof(configuration.performance)))
      {
        sysex.close();
        return (true);
      }
      sysex.close();
    }
    else
    {
#ifdef DEBUG
      Serial.print(F("E : Cannot open "));
      Serial.print(filename);
      Serial.println(F(" on SD."));
#endif
      return (false);
    }
  }
  return (false);
}

/******************************************************************************
   HELPER FUNCTIONS
 ******************************************************************************/
bool get_sd_data(File sysex, uint8_t format, uint8_t* conf)
{
  uint16_t n;
  int32_t bulk_checksum_calc = 0;
  int8_t bulk_checksum;

#ifdef DEBUG
  Serial.print(F("Reading "));
  Serial.print(sysex.size());
  Serial.println(F(" bytes."));
#endif

  if (sysex.read() != 0xf0)  // check sysex start-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx start byte not found."));
#endif
    return (false);
  }
  if (sysex.read() != 0x67)  // check sysex vendor is unofficial SYSEX-ID for MicroDexed
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx vendor not unofficial SYSEX-ID for MicroDexed."));
#endif
    return (false);
  }
  if (sysex.read() != format) // check for sysex type
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx type not found."));
#endif
    return (false);
  }
  sysex.seek(sysex.size() - 1);
  if (sysex.read() != 0xf7)  // check sysex end-byte
  {
#ifdef DEBUG
    Serial.println(F("E : SysEx end byte not found."));
#endif
    return (false);
  }

  sysex.seek(sysex.size() - 2); // Bulk checksum
  bulk_checksum = sysex.read();

  sysex.seek(3); // start of bulk data
  for (n = 0; n < sysex.size() - 6; n++)
  {
    uint8_t d = sysex.read();
    bulk_checksum_calc -= d;
#ifdef DEBUG
    Serial.print(F("SYSEX data read: 0x"));
    Serial.println(d, HEX);
#endif
  }
  bulk_checksum_calc &= 0x7f;

  if (int8_t(bulk_checksum_calc) != bulk_checksum)
  {
#ifdef DEBUG
    Serial.print(F("E : Bulk checksum mismatch : 0x"));
    Serial.print(int8_t(bulk_checksum_calc), HEX);
    Serial.print(F(" != 0x"));
    Serial.println(bulk_checksum, HEX);
#endif
    return (false);
  }
#ifdef DEBUG
  else
  {
    Serial.print(F("Bulk checksum : 0x"));
    Serial.print(int8_t(bulk_checksum_calc), HEX);
    Serial.print(F(" [0x"));
    Serial.print(bulk_checksum, HEX);
    Serial.println(F("]"));
  }
#endif

  sysex.seek(3); // start of bulk data
  for (n = 0; n < sysex.size() - 6; n++)
  {
    uint8_t d = sysex.read();
    *(conf++) = d;
  }

#ifdef DEBUG
  Serial.println(F("SD data loaded."));
#endif

  return (true);
}

bool write_sd_data(File sysex, uint8_t format, uint8_t* data, uint16_t len)
{
#ifdef DEBUG
  Serial.print(F("Storing SYSEX format 0x"));
  Serial.print(format, HEX);
  Serial.print(F(" with length of "));
  Serial.print(len, DEC);
  Serial.println(F(" bytes."));
#endif

  // write sysex start
  sysex.write(0xf0);
#ifdef DEBUG
  Serial.println(F("Write SYSEX start:    0xf0"));
#endif
  // write sysex vendor is unofficial SYSEX-ID for MicroDexed
  sysex.write(0x67);
#ifdef DEBUG
  Serial.println(F("Write SYSEX vendor:   0x67"));
#endif
  // write sysex format number
  sysex.write(format);
#ifdef DEBUG
  Serial.print(F("Write SYSEX format:   0x"));
  Serial.println(format, HEX);
#endif
  // write data
  sysex.write(data, len);
#ifdef DEBUG
  for (uint16_t i = 0; i < len; i++)
  {
    Serial.print(F("Write SYSEX data:     0x"));
    Serial.println(data[i], HEX);
  }
#endif
  // write checksum
  sysex.write(calc_checksum(data, len));
#ifdef DEBUG
  uint8_t checksum = calc_checksum(data, len);
  sysex.write(checksum);
  Serial.print(F("Write SYSEX checksum: 0x"));
  Serial.println(checksum, HEX);
#endif
  // write sysex end
  sysex.write(0xf7);
#ifdef DEBUG
  Serial.println(F("Write SYSEX end:      0xf7"));
#endif

  return (true);
}

uint8_t calc_checksum(uint8_t* data, uint16_t len)
{
  int32_t bulk_checksum_calc = 0;

  for (uint16_t n = 0; n < len; n++)
    bulk_checksum_calc -= data[n];

  return (bulk_checksum_calc & 0x7f);
}

void strip_extension(char* s, char* target, uint8_t len)
{
  char tmp[FILENAME_LEN];
  char* token;

  strcpy(tmp, s);
  token = strtok(tmp, ".");
  if (token == NULL)
    strcpy(target, "*ERROR*");
  else
    strcpy(target, token);

  target[len] = '\0';
}

bool get_bank_name(uint8_t b, char* name, uint8_t len)
{
  File sysex;

  if (sd_card > 0)
  {
    char bankdir[4];
    File entry;

    memset(name, 0, len);

    sprintf(bankdir, "/%d", b);

    // try to open directory
    sysex = SD.open(bankdir);
    if (!sysex)
      return (false);

    do
    {
      entry = sysex.openNextFile();
    } while (entry.isDirectory());

    if (entry.isDirectory())
    {
      entry.close();
      sysex.close();
      return (false);
    }

    strip_extension(entry.name(), name, len);

#ifdef DEBUG
    Serial.print(F("Found bank-name ["));
    Serial.print(name);
    Serial.print(F("] for bank ["));
    Serial.print(b);
    Serial.println(F("]"));
#endif

    entry.close();
    sysex.close();

    return (true);
  }

  return (false);
}

bool get_voice_name(uint8_t b, uint8_t v, char* name, uint8_t len)
{
  File sysex;

  if (sd_card > 0)
  {
    char bank_name[BANK_NAME_LEN];
    char filename[FILENAME_LEN];

    b = constrain(b, 0, MAX_BANKS - 1);
    v = constrain(v, 0, MAX_VOICES - 1);

    get_bank_name(b, bank_name, sizeof(bank_name));
    sprintf(filename, "/%d/%s.syx", b, bank_name);
#ifdef DEBUG
    Serial.print(F("Reading voice-name from ["));
    Serial.print(filename);
    Serial.println(F("]"));
#endif

    // try to open directory
    sysex = SD.open(filename);
    if (!sysex)
      return (false);

    memset(name, 0, len);
    sysex.seek(124 + (v * 128));
    sysex.read(name, min(len, 10));

#ifdef DEBUG
    Serial.print(F("Found voice-name ["));
    Serial.print(name);
    Serial.print(F("] for bank ["));
    Serial.print(b);
    Serial.print(F("] and voice ["));
    Serial.print(v);
    Serial.println(F("]"));
#endif

    sysex.close();

    return (true);
  }

  return (false);
}

bool get_voice_by_bank_name(uint8_t b, const char* bank_name, uint8_t v, char* voice_name, uint8_t len)
{
  File sysex;

  if (sd_card > 0)
  {
    char filename[FILENAME_LEN];

    sprintf(filename, "/%d/%s.syx", b, bank_name);
#ifdef DEBUG
    Serial.print(F("Reading voice-name from ["));
    Serial.print(filename);
    Serial.println(F("]"));
#endif

    // try to open directory
    sysex = SD.open(filename);
    if (!sysex)
      return (false);

    memset(voice_name, 0, len);
    sysex.seek(124 + (v * 128));
    sysex.read(voice_name, min(len, 10));

#ifdef DEBUG
    Serial.print(F("Found voice-name ["));
    Serial.print(voice_name);
    Serial.print(F("] for bank ["));
    Serial.print(b);
    Serial.print(F("|"));
    Serial.print(bank_name);
    Serial.print(F("] and voice ["));
    Serial.print(v);
    Serial.println(F("]"));
#endif

    sysex.close();

    return (true);
  }

  return (false);
}
