#include "tusb.h"
#include "usb_descriptors.h"

//--------------------------------------------------------------------+
// PID auto
//--------------------------------------------------------------------+
#define PID_MAP(itf, n)  ((CFG_TUD_##itf) ? (1 << (n)) : 0)
#define USB_PID (0x4000 | PID_MAP(CDC,0) | PID_MAP(MSC,1) | PID_MAP(HID,2) | \
                 PID_MAP(MIDI,3) | PID_MAP(AUDIO,4) | PID_MAP(VENDOR,5))

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
static tusb_desc_device_t const desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,

  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
  .idVendor           = 0xCafe,
  .idProduct          = USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,
  .bNumConfigurations = 0x01
};

uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Interfaces
//--------------------------------------------------------------------+
enum
{
  ITF_NUM_AUDIO_CONTROL = 0,
  ITF_NUM_AUDIO_STREAMING_OUT,
  ITF_NUM_AUDIO_STREAMING_IN,
  ITF_NUM_MIDI,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_TOTAL
};

#define EPNUM_AUDIO_OUT 0x01
#define EPNUM_AUDIO_IN  0x81
#define EPNUM_MIDI_OUT 0x02
#define EPNUM_MIDI_IN  0x82

#define AUDIO_ENTITY_SPK_INPUT_TERMINAL  0x01
#define AUDIO_ENTITY_SPK_FEATURE_UNIT    0x02
#define AUDIO_ENTITY_SPK_OUTPUT_TERMINAL 0x03
#define AUDIO_ENTITY_MIC_INPUT_TERMINAL  0x04
#define AUDIO_ENTITY_MIC_FEATURE_UNIT    0x05
#define AUDIO_ENTITY_MIC_OUTPUT_TERMINAL 0x06

#define TUD_AUDIO10_SPK_MIC_DESC_LEN(_nfreqs) (\
  + TUD_AUDIO10_DESC_STD_AC_LEN\
  + TUD_AUDIO10_DESC_CS_AC_LEN(2)\
  + TUD_AUDIO10_DESC_INPUT_TERM_LEN\
  + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN\
  + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1)\
  + TUD_AUDIO10_DESC_INPUT_TERM_LEN\
  + TUD_AUDIO10_DESC_OUTPUT_TERM_LEN\
  + TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1)\
  + TUD_AUDIO10_DESC_STD_AS_LEN\
  + TUD_AUDIO10_DESC_STD_AS_LEN\
  + TUD_AUDIO10_DESC_CS_AS_INT_LEN\
  + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(_nfreqs)\
  + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN\
  + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN\
  + TUD_AUDIO10_DESC_STD_AS_LEN\
  + TUD_AUDIO10_DESC_STD_AS_LEN\
  + TUD_AUDIO10_DESC_CS_AS_INT_LEN\
  + TUD_AUDIO10_DESC_TYPE_I_FORMAT_LEN(_nfreqs)\
  + TUD_AUDIO10_DESC_STD_AS_ISO_EP_LEN\
  + TUD_AUDIO10_DESC_CS_AS_ISO_EP_LEN)

#define CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + \
   TUD_AUDIO10_SPK_MIC_DESC_LEN(1) + \
   TUD_MIDI_DESC_LEN)

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
uint8_t const desc_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // AUDIO (UAC1 SPEAKER OUT + MIC IN)
  TUD_AUDIO10_DESC_STD_AC(/*_itfnum*/ ITF_NUM_AUDIO_CONTROL,
                          /*_nEPs*/ 0x00,
                          /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_CS_AC(/*_bcdADC*/ 0x0100,
                         /*_totallen*/ (TUD_AUDIO10_DESC_INPUT_TERM_LEN +
                                        TUD_AUDIO10_DESC_OUTPUT_TERM_LEN +
                                        TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1) +
                                        TUD_AUDIO10_DESC_INPUT_TERM_LEN +
                                        TUD_AUDIO10_DESC_OUTPUT_TERM_LEN +
                                        TUD_AUDIO10_DESC_FEATURE_UNIT_LEN(1)),
                         /*_itf*/ ITF_NUM_AUDIO_STREAMING_OUT,
                         /*_itf*/ ITF_NUM_AUDIO_STREAMING_IN),
  // Speaker path (USB OUT -> Speaker)
  TUD_AUDIO10_DESC_INPUT_TERM(/*_termid*/ AUDIO_ENTITY_SPK_INPUT_TERMINAL,
                              /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,
                              /*_assocTerm*/ AUDIO_ENTITY_SPK_OUTPUT_TERMINAL,
                              /*_nchannels*/ 0x01,
                              /*_channelcfg*/ AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED,
                              /*_idxchannelnames*/ 0x00,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_OUTPUT_TERM(/*_termid*/ AUDIO_ENTITY_SPK_OUTPUT_TERMINAL,
                               /*_termtype*/ AUDIO_TERM_TYPE_OUT_GENERIC_SPEAKER,
                               /*_assocTerm*/ AUDIO_ENTITY_SPK_INPUT_TERMINAL,
                               /*_srcid*/ AUDIO_ENTITY_SPK_FEATURE_UNIT,
                               /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_FEATURE_UNIT(/*_unitid*/ AUDIO_ENTITY_SPK_FEATURE_UNIT,
                                /*_srcid*/ AUDIO_ENTITY_SPK_INPUT_TERMINAL,
                                /*_stridx*/ 0x00,
                                /*_ctrlmaster*/ (AUDIO10_FU_CONTROL_BM_MUTE |
                                                  AUDIO10_FU_CONTROL_BM_VOLUME),
                                /*_ctrlch1*/ (AUDIO10_FU_CONTROL_BM_MUTE |
                                              AUDIO10_FU_CONTROL_BM_VOLUME)),
  // Microphone path (Mic -> USB IN)
  TUD_AUDIO10_DESC_INPUT_TERM(/*_termid*/ AUDIO_ENTITY_MIC_INPUT_TERMINAL,
                              /*_termtype*/ AUDIO_TERM_TYPE_IN_GENERIC_MIC,
                              /*_assocTerm*/ AUDIO_ENTITY_MIC_OUTPUT_TERMINAL,
                              /*_nchannels*/ 0x01,
                              /*_channelcfg*/ AUDIO10_CHANNEL_CONFIG_NON_PREDEFINED,
                              /*_idxchannelnames*/ 0x00,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_OUTPUT_TERM(/*_termid*/ AUDIO_ENTITY_MIC_OUTPUT_TERMINAL,
                               /*_termtype*/ AUDIO_TERM_TYPE_USB_STREAMING,
                               /*_assocTerm*/ AUDIO_ENTITY_MIC_INPUT_TERMINAL,
                               /*_srcid*/ AUDIO_ENTITY_MIC_FEATURE_UNIT,
                               /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_FEATURE_UNIT(/*_unitid*/ AUDIO_ENTITY_MIC_FEATURE_UNIT,
                                /*_srcid*/ AUDIO_ENTITY_MIC_INPUT_TERMINAL,
                                /*_stridx*/ 0x00,
                                /*_ctrlmaster*/ (AUDIO10_FU_CONTROL_BM_MUTE |
                                                  AUDIO10_FU_CONTROL_BM_VOLUME),
                                /*_ctrlch1*/ (AUDIO10_FU_CONTROL_BM_MUTE |
                                              AUDIO10_FU_CONTROL_BM_VOLUME)),
  // Speaker Streaming Interface (OUT)
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ ITF_NUM_AUDIO_STREAMING_OUT,
                              /*_altset*/ 0x00,
                              /*_nEPs*/ 0x00,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ ITF_NUM_AUDIO_STREAMING_OUT,
                              /*_altset*/ 0x01,
                              /*_nEPs*/ 0x01,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_CS_AS_INT(/*_termid*/ AUDIO_ENTITY_SPK_INPUT_TERMINAL,
                             /*_delay*/ 0x01,
                             /*_formattype*/ AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(/*_nrchannels*/ 0x01,
                                 /*_subframesize*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX,
                                 /*_bitresolution*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX,
                                 /*_freq*/ CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE),
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(/*_ep*/ EPNUM_AUDIO_OUT,
                                 /*_attr*/ (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS |
                                                     (uint8_t)TUSB_ISO_EP_ATT_ADAPTIVE),
                                 /*_maxEPsize*/ CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX,
                                 /*_interval*/ 0x01,
                                 /*_sync_ep*/ 0x00),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ,
                                /*_lockdelayunits*/ AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC,
                                /*_lockdelay*/ 0x0001),
  // Microphone Streaming Interface (IN)
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ ITF_NUM_AUDIO_STREAMING_IN,
                              /*_altset*/ 0x00,
                              /*_nEPs*/ 0x00,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_STD_AS_INT(/*_itfnum*/ ITF_NUM_AUDIO_STREAMING_IN,
                              /*_altset*/ 0x01,
                              /*_nEPs*/ 0x01,
                              /*_stridx*/ 0x00),
  TUD_AUDIO10_DESC_CS_AS_INT(/*_termid*/ AUDIO_ENTITY_MIC_OUTPUT_TERMINAL,
                             /*_delay*/ 0x01,
                             /*_formattype*/ AUDIO10_DATA_FORMAT_TYPE_I_PCM),
  TUD_AUDIO10_DESC_TYPE_I_FORMAT(/*_nrchannels*/ 0x01,
                                 /*_subframesize*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_TX,
                                 /*_bitresolution*/ CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_TX,
                                 /*_freq*/ CFG_TUD_AUDIO_FUNC_1_MAX_SAMPLE_RATE),
  TUD_AUDIO10_DESC_STD_AS_ISO_EP(/*_ep*/ EPNUM_AUDIO_IN,
                                 /*_attr*/ (uint8_t) ((uint8_t)TUSB_XFER_ISOCHRONOUS |
                                                     (uint8_t)TUSB_ISO_EP_ATT_ASYNCHRONOUS),
                                 /*_maxEPsize*/ CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX,
                                 /*_interval*/ 0x01,
                                 /*_sync_ep*/ 0x00),
  TUD_AUDIO10_DESC_CS_AS_ISO_EP(/*_attr*/ AUDIO10_CS_AS_ISO_DATA_EP_ATT_SAMPLING_FRQ,
                                /*_lockdelayunits*/ AUDIO10_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC,
                                /*_lockdelay*/ 0x0001),

  // MIDI
  TUD_MIDI_DESCRIPTOR(
      ITF_NUM_MIDI,
      0,
      EPNUM_MIDI_OUT,
      EPNUM_MIDI_IN,
      64)
};

TU_VERIFY_STATIC(sizeof(desc_configuration) == CONFIG_TOTAL_LEN,
                 "Config size mismatch");

uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index;
  return desc_configuration;
}

//--------------------------------------------------------------------+
// Strings
//--------------------------------------------------------------------+
enum
{
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

char const* string_desc_arr[] =
{
  (const char[]){0x09,0x04},
  "PaniRCorp",
  "MicNode",
  "0001"
};

static uint16_t _desc_str[32];

uint16_t const * tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;
  uint8_t chr_count;

  if (index == 0)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }
  else
  {
    const char* str = string_desc_arr[index];
    chr_count = strlen(str);
    for(uint8_t i=0;i<chr_count;i++)
      _desc_str[1+i] = str[i];
  }

  _desc_str[0] = (TUSB_DESC_STRING << 8 ) | (2*chr_count + 2);
  return _desc_str;
}
