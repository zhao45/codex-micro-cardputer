#include <string.h>

#include "codex_control_transport.h"
#include "tusb.h"
#include "uac_descriptors.h"

enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_STREAMING_MIC,
    ITF_NUM_CODEX_HID,
    ITF_NUM_TOTAL,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_MICROPHONE,
    STRID_CODEX_HID,
};

#define CODEX_VID 0x303A
#define CODEX_PID 0x8360
#define CODEX_REPORT_DESCRIPTOR_SIZE 23
#define EPNUM_AUDIO_OUT 0x01
#define EPNUM_AUDIO_IN 0x82
#define EPNUM_AUDIO_FB 0x81
#define EPNUM_HID_OUT 0x03
#define EPNUM_HID_IN 0x83
#define CONFIG_TOTAL_LEN \
    (TUD_CONFIG_DESC_LEN + TUD_AUDIO_DEVICE_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const tusb_desc_device_t desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = CODEX_VID,
    .idProduct = CODEX_PID,
    .bcdDevice = 0x0400,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&desc_device;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    const codex_transport_profile_t *profile = codex_control_transport_profile();
    return profile->report_map_size == CODEX_REPORT_DESCRIPTOR_SIZE
               ? profile->report_map
               : NULL;
}

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_AUDIO_DESCRIPTOR(ITF_NUM_AUDIO_CONTROL, STRID_MICROPHONE,
                         EPNUM_AUDIO_OUT, EPNUM_AUDIO_IN, EPNUM_AUDIO_FB),
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_CODEX_HID, STRID_CODEX_HID,
                             HID_ITF_PROTOCOL_NONE,
                             CODEX_REPORT_DESCRIPTOR_SIZE,
                             EPNUM_HID_OUT, EPNUM_HID_IN,
                             CFG_TUD_HID_EP_BUFSIZE, 4),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

static const char *const string_desc_arr[] = {
    (const char[]){0x09, 0x04},
    "Community Project",
    "Codex Micro (Cardputer)",
    "CARDPUTER-ADV",
    "Cardputer ADV Microphone",
    "Codex Micro",
};

static uint16_t desc_string[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t count;
    if (index == STRID_LANGID) {
        memcpy(&desc_string[1], string_desc_arr[0], 2);
        count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char *string = string_desc_arr[index];
        count = (uint8_t)strlen(string);
        if (count > 31) count = 31;
        for (uint8_t i = 0; i < count; ++i) desc_string[1 + i] = string[i];
    }
    desc_string[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_string;
}
