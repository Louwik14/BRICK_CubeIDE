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
  ITF_NUM_AUDIO_STREAMING,
  ITF_NUM_MIDI,
  ITF_NUM_MIDI_STREAMING,
  ITF_NUM_TOTAL
};

#define EPNUM_AUDIO   0x01
#define EPNUM_MIDI_OUT 0x02
#define EPNUM_MIDI_IN  0x82

#define CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + \
   TUD_AUDIO10_MIC_ONE_CH_DESC_LEN(1) + \
   TUD_MIDI_DESC_LEN)

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+
uint8_t const desc_configuration[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

  // AUDIO (MIC + SPEAKER simulé via IN/OUT)
  TUD_AUDIO10_MIC_ONE_CH_DESCRIPTOR(
      ITF_NUM_AUDIO_CONTROL,
      0,
      2,
      16,
      0x80 | EPNUM_AUDIO,
      CFG_TUD_AUDIO10_FUNC_1_FORMAT_1_EP_SZ_IN,
      48000
  ),

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
