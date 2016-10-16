/*
* *** Honeywell (Ademco) Door/Window Sensors (345.0Mhz) ***
*
* Tested with the Honeywell 5811 Wireless Door/Window transmitters
*
* 64 bit packets, repeated multiple times per open/close event
*
* Protocol whitepaper: "DEFCON 22: Home Insecurity" by Logan Lamb
*
* 0xfffe Preamble and sync bit
* 0x8 Unknown
* 0xYYYYY Device serial number
* 0xYY Event Information (Open/Close, Heartbeat, etc)
* 0xYYYY CRC
*
*/

#include "rtl_433.h"
#include "pulse_demod.h"
#include "util.h"

static int honeywell_callback(bitbuffer_t *bitbuffer) {
  char time_str[LOCAL_TIME_BUFLEN];
  const uint8_t *bb;
  char device_id[6];
  char hex[2];
  char binary [65];
  int heartbeat = 0;
  uint16_t crc_calculated = 0;
  uint16_t crc = 0;

  local_time_str(0, time_str);

  if(bitbuffer->num_rows != 1 || bitbuffer->bits_per_row[0] != 64)
    return 0; // Unrecognized data

  for(uint16_t i=0; i < 8; i++)
    bitbuffer->bb[0][i] = ~bitbuffer->bb[0][i];

  bb = bitbuffer->bb[0];

  crc_calculated = crc16_ccitt(bb, 6, 0x8005, 0xfffe);
  crc = (((uint16_t) bb[6]) << 8) + ((uint16_t) bb[7]);
  if(crc != crc_calculated)
    return 0; // Not a valid packet

  sprintf(hex, "%02x", bb[2]);
  device_id[0] = hex[1];
  sprintf(hex, "%02x", bb[3]);
  device_id[1] = hex[0];
  device_id[2] = hex[1];
  sprintf(hex, "%02x", bb[4]);
  device_id[3] = hex[0];
  device_id[4] = hex[1];
  device_id[5] = '\0';

  // Raw Binary
  for (uint16_t bit = 0; bit < 64; ++bit) {
      if (bb[bit/8] & (0x80 >> (bit % 8))) {
              binary[bit] = '1';
      } else {
              binary[bit] = '0';
      }
  }
  binary[64] = '\0';

  data_t *data = data_make(
                   "time",     "", DATA_STRING, time_str,
                   "id",       "", DATA_STRING, device_id,
                   "state",    "", DATA_STRING, ( (bb[5] & 0x80) == 0x00)? "closed":"open",
                   "heartbeat" , "", DATA_STRING, ( (bb[5] & 0x04) == 0x04)? "yes" : "no",
                   "crc", "", DATA_STRING, "ok",
                   "time_unix","", DATA_INT, time(NULL),
                   "binary",   "", DATA_STRING, binary,
                          NULL);

  data_acquired_handler(data);
  return 1;
}

static char *output_fields[] = {
        "time",
        "id",
        "state",
        "heartbeat",
        "crc",
        "time_unix",
        "binary",
        NULL
};

r_device honeywell = {
        .name                   = "Honeywell Door/Window Sensor",
        .modulation             = OOK_PULSE_MANCHESTER_ZEROBIT,
        .short_limit    =   39 * 4,
        .long_limit             = 0,
        .reset_limit    = 73 * 4,
        .json_callback  = &honeywell_callback,
        .disabled               = 0,
        .demod_arg              = 0,
        .fields                 = output_fields,
};
